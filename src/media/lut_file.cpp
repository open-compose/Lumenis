// External LUT files (Editing Guide p.541-542; 8.5d1, docs/spec/color-adapters.md; lut_file.h). The text formats:
// - IRIDAS/Resolve ".cube" and Mistika/IRIDAS ".itx": keywords (TITLE, LUT_1D_SIZE n, LUT_3D_SIZE n, DOMAIN_MIN, DOMAIN_MAX,
//   LUT_1D_INPUT_RANGE, LUT_3D_INPUT_RANGE) then one "r g b" line per entry, red changing fastest in a cube;
// - Kodak KDM and Lustre ".3dl": a line of the input grid's code values, then one integer "r g b" line per grid point
//   with blue changing fastest, the output's bit depth taken from its largest value;
// - Autodesk "LUT: <channels> <entries>": the entries of each channel in turn, one integer per line.
#include "lumenis/media/lut_file.h"

#include "ocio_colour.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <vector>

namespace lumenis::media {

namespace {

QList<double> numbers(const QString& line, bool* ok) {
    QList<double> out;
    *ok = true;
    for (const QString& part : line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        bool good = false;
        const double v = part.toDouble(&good);
        if (!good) {
            *ok = false;
            return {};
        }
        out << v;
    }
    return out;
}

// The largest code value of an integer output: 2^bits - 1 for the fewest bits that hold `max`.
double fullScale(double max) {
    double scale = 1.0;
    while (scale < max) scale = scale * 2.0 + 1.0;
    return std::max(scale, 1.0);
}

bool fail(QString* error, const QString& why) {
    if (error) *error = why;
    return false;
}

bool readCube(const QStringList& lines, LutFile& lut, QString* error) {
    int size1D = 0;
    QList<std::array<double, 3>> rows;
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QString key = line.section(QLatin1Char(' '), 0, 0, QString::SectionSkipEmpty);
        const QString rest = line.section(QLatin1Char(' '), 1, -1, QString::SectionSkipEmpty);
        bool ok = false;
        if (key == QLatin1String("TITLE")) {
            lut.title = rest.trimmed().remove(QLatin1Char('"'));
        } else if (key == QLatin1String("LUT_1D_SIZE")) {
            size1D = rest.toInt();
        } else if (key == QLatin1String("LUT_3D_SIZE")) {
            lut.size3D = rest.toInt();
        } else if (key == QLatin1String("DOMAIN_MIN") || key == QLatin1String("DOMAIN_MAX")) {
            const QList<double> v = numbers(rest, &ok);
            if (!ok || v.size() != 3) return fail(error, QObject::tr("%1 needs three values.").arg(key));
            auto& target = key == QLatin1String("DOMAIN_MIN") ? lut.domainMin : lut.domainMax;
            target = {v.at(0), v.at(1), v.at(2)};
        } else if (key == QLatin1String("LUT_1D_INPUT_RANGE") || key == QLatin1String("LUT_3D_INPUT_RANGE")) {
            const QList<double> v = numbers(rest, &ok);
            if (!ok || v.size() != 2) return fail(error, QObject::tr("%1 needs two values.").arg(key));
            lut.domainMin = {v.at(0), v.at(0), v.at(0)};
            lut.domainMax = {v.at(1), v.at(1), v.at(1)};
        } else {
            const QList<double> v = numbers(line, &ok);
            if (!ok) continue; // keywords we do not use
            if (v.size() != 3) return fail(error, QObject::tr("A table line needs three values: %1").arg(line));
            rows.append({v.at(0), v.at(1), v.at(2)});
        }
    }
    if (size1D < 2 && lut.size3D < 2) return fail(error, QObject::tr("The file gives no LUT_1D_SIZE or LUT_3D_SIZE."));
    if (lut.size3D > 256 || size1D > 65536) return fail(error, QObject::tr("The table is too large."));
    const qsizetype cube = lut.size3D >= 2 ? static_cast<qsizetype>(lut.size3D) * lut.size3D * lut.size3D : 0;
    const qsizetype first = size1D >= 2 ? size1D : 0;
    if (rows.size() != first + cube) {
        return fail(error, QObject::tr("The table has %1 lines where %2 are needed.").arg(rows.size()).arg(first + cube));
    }
    lut.table1D = rows.mid(0, first);
    lut.table3D = rows.mid(first);
    if (cube == 0) lut.size3D = 0;
    return true;
}

bool read3dl(const QStringList& lines, LutFile& lut, QString* error) {
    QList<double> grid;
    QList<std::array<double, 3>> rows;
    double largest = 0.0;
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('<'))) continue;
        bool ok = false;
        const QList<double> v = numbers(line, &ok);
        if (!ok) continue; // "Mesh", "LUT8" and other keywords
        // The first line of numbers is the input grid, however many points it has.
        if (grid.isEmpty()) {
            grid = v;
            continue;
        }
        if (v.size() != 3) continue;
        rows.append({v.at(0), v.at(1), v.at(2)});
        largest = std::max({largest, v.at(0), v.at(1), v.at(2)});
    }
    const int n = static_cast<int>(grid.size());
    if (n < 2) return fail(error, QObject::tr("The file gives no input grid."));
    if (n > 256) return fail(error, QObject::tr("The table is too large."));
    if (rows.size() != static_cast<qsizetype>(n) * n * n) {
        return fail(error, QObject::tr("The table has %1 lines where %2 are needed.").arg(rows.size()).arg(n * n * n));
    }
    const double scale = fullScale(largest);
    lut.size3D = n;
    lut.table3D.resize(rows.size());
    // Blue changes fastest in the file; red fastest in ours.
    for (int r = 0; r < n; ++r) {
        for (int g = 0; g < n; ++g) {
            for (int b = 0; b < n; ++b) {
                const auto& row = rows.at((r * n + g) * n + b);
                lut.table3D[r + g * n + b * n * n] = {row[0] / scale, row[1] / scale, row[2] / scale};
            }
        }
    }
    return true;
}

bool readAutodesk(const QStringList& lines, LutFile& lut, QString* error) {
    bool ok = false;
    const QList<double> head = numbers(lines.value(0).trimmed().mid(4), &ok);
    if (!ok || head.size() < 2) return fail(error, QObject::tr("The first line must give the channels and entries."));
    const int channels = static_cast<int>(head.at(0));
    const int entries = static_cast<int>(head.at(1));
    if (channels < 1 || channels > 4 || entries < 2 || entries > 65536) {
        return fail(error, QObject::tr("The table's size is not one we read."));
    }
    QList<double> values;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QList<double> v = numbers(line, &ok);
        if (!ok) return fail(error, QObject::tr("A table line is not a number: %1").arg(line));
        values += v;
    }
    if (values.size() < static_cast<qsizetype>(channels) * entries) {
        return fail(error, QObject::tr("The table has %1 values where %2 are needed.").arg(values.size()).arg(channels * entries));
    }
    const double scale = fullScale(*std::max_element(values.begin(), values.end()));
    lut.table1D.resize(entries);
    for (int e = 0; e < entries; ++e) {
        std::array<double, 3> row{};
        for (int c = 0; c < 3; ++c) {
            // One channel for all three; a fourth (alpha) is left out.
            const int from = channels >= 3 ? c : 0;
            row[static_cast<std::size_t>(c)] = values.at(from * entries + e) / scale;
        }
        lut.table1D[e] = row;
    }
    return true;
}

// Linear interpolation in a 1D table at x (0 to 1).
double lookup1D(const QList<std::array<double, 3>>& table, std::size_t channel, double x) {
    const double pos = std::clamp(x, 0.0, 1.0) * static_cast<double>(table.size() - 1);
    const auto i = static_cast<qsizetype>(std::floor(pos));
    const qsizetype j = std::min<qsizetype>(i + 1, table.size() - 1);
    const double f = pos - static_cast<double>(i);
    return table.at(i)[channel] * (1.0 - f) + table.at(j)[channel] * f;
}

} // namespace

std::array<double, 3> LutFile::apply(const std::array<double, 3>& rgb) const {
    std::array<double, 3> c{};
    for (std::size_t i = 0; i < 3; ++i) {
        const double span = domainMax[i] - domainMin[i];
        c[i] = span > 0 ? (rgb[i] - domainMin[i]) / span : rgb[i];
    }
    if (table1D.size() >= 2) {
        for (std::size_t i = 0; i < 3; ++i) c[i] = lookup1D(table1D, i, c[i]);
    }
    if (size3D >= 2 && table3D.size() == static_cast<qsizetype>(size3D) * size3D * size3D) {
        const int n = size3D;
        std::array<double, 3> pos{};
        std::array<int, 3> lo{};
        std::array<double, 3> f{};
        for (std::size_t i = 0; i < 3; ++i) {
            pos[i] = std::clamp(c[i], 0.0, 1.0) * (n - 1);
            lo[i] = std::min(static_cast<int>(std::floor(pos[i])), n - 2);
            f[i] = pos[i] - lo[i];
        }
        auto at = [&](int r, int g, int b) { return table3D.at(r + g * n + b * n * n); };
        std::array<double, 3> out{};
        for (std::size_t k = 0; k < 3; ++k) {
            double v = 0.0;
            for (int dr = 0; dr < 2; ++dr) {
                for (int dg = 0; dg < 2; ++dg) {
                    for (int db = 0; db < 2; ++db) {
                        const double w = (dr ? f[0] : 1 - f[0]) * (dg ? f[1] : 1 - f[1]) * (db ? f[2] : 1 - f[2]);
                        v += w * at(lo[0] + dr, lo[1] + dg, lo[2] + db)[k];
                    }
                }
            }
            out[k] = v;
        }
        c = out;
    }
    return c;
}

QString lutProduct(const QString& firstLine) {
    const QString l = firstLine.trimmed();
    if (l.startsWith(QLatin1String("AVID DS LUT"))) return QStringLiteral("Avid DS");
    if (l.startsWith(QLatin1String("LUT:"))) return QStringLiteral("Autodesk");
    if (l.startsWith(QLatin1String("LUT_1D_SIZE"))) return QStringLiteral("IRIDAS 1D");
    if (l.startsWith(QLatin1String("LUT_3D_SIZE"))) return QStringLiteral("IRIDAS 3D");
    if (l.startsWith(QLatin1String("# IDENTIFICATION: 3DMODEL-3DLUT"))) return QStringLiteral("Kodak KDM");
    if (l.startsWith(QLatin1String("#channels: c3"))) return QStringLiteral("LUTher");
    if (l.startsWith(QLatin1String("NUCODA_3D_CUBE"))) return QStringLiteral("Nucoda");
    return {};
}

std::optional<LutFile> readLutFile(const QString& path, QString* error) {
    // CLF: OpenColorIO reads it; we keep its transform as a 33-point cube (ours).
    if (QFileInfo(path).suffix().compare(QLatin1String("clf"), Qt::CaseInsensitive) == 0) {
        constexpr int kSize = 33;
        std::vector<float> rgb(static_cast<std::size_t>(kSize) * kSize * kSize * 3);
        QString why;
        if (!detail::ocioBakeFile(path, kSize, rgb.data(), &why)) {
            if (error) *error = why;
            return std::nullopt;
        }
        LutFile lut;
        lut.title = QFileInfo(path).completeBaseName();
        lut.size3D = kSize;
        lut.table3D.reserve(kSize * kSize * kSize);
        for (std::size_t i = 0; i + 2 < rgb.size(); i += 3) lut.table3D.append({static_cast<double>(rgb[i]), static_cast<double>(rgb[i + 1]), static_cast<double>(rgb[i + 2])});
        return lut;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QObject::tr("The file cannot be opened.");
        return std::nullopt;
    }
    if (file.size() > 64 * 1024 * 1024) {
        if (error) *error = QObject::tr("The file is too large for a LUT.");
        return std::nullopt;
    }
    auto lut = readLutData(file.readAll(), QFileInfo(path).suffix(), error);
    if (lut && lut->title.isEmpty()) lut->title = QFileInfo(path).completeBaseName();
    return lut;
}

std::optional<LutFile> readLutData(const QByteArray& data, const QString& fileSuffix, QString* error) {
    const QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    QString first;
    for (const QString& l : lines) {
        if (!l.trimmed().isEmpty()) {
            first = l;
            break;
        }
    }
    const QString product = lutProduct(first);
    QString suffix = fileSuffix.toLower();
    if (suffix.startsWith(QLatin1Char('.'))) suffix.remove(0, 1);
    LutFile lut;
    bool ok = false;
    if (product == QLatin1String("Avid DS") || product == QLatin1String("LUTher") || product == QLatin1String("Nucoda")) {
        if (error) *error = QObject::tr("%1 LUT files are not read yet.").arg(product);
        return std::nullopt;
    }
    if (product == QLatin1String("Autodesk")) {
        ok = readAutodesk(lines, lut, error);
    } else if (product == QLatin1String("Kodak KDM") || suffix == QLatin1String("3dl")) {
        ok = read3dl(lines, lut, error);
    } else {
        // .cube and .itx files may start with TITLE or comments before their size.
        ok = readCube(lines, lut, error);
    }
    if (!ok) return std::nullopt;
    return lut;
}

ColorTransformation lutTransformation(const LutFile& lut) {
    ColorStep step;
    step.mapping = [lut](const std::array<double, 3>& c) { return lut.apply(c); };
    return {step};
}

QString externalLutName(const QString& path) {
    return QStringLiteral("External ") + QFileInfo(path).completeBaseName();
}

QStringList lutFilesIn(const QString& folder) {
    QDir dir(folder);
    if (folder.isEmpty() || !dir.exists()) return {};
    QStringList out;
    const QStringList filters{QStringLiteral("*.cube"), QStringLiteral("*.itx"), QStringLiteral("*.3dl"), QStringLiteral("*.lut"),
                              QStringLiteral("*.txt"), QStringLiteral("*.clf")};
    for (const QFileInfo& f : dir.entryInfoList(filters, QDir::Files, QDir::Name | QDir::IgnoreCase)) out << f.absoluteFilePath();
    return out;
}

QStringList registerLutFolders(const QStringList& folders, QStringList* failures) {
    QStringList names;
    for (const QString& folder : folders) {
        for (const QString& path : lutFilesIn(folder)) {
            QString why;
            const auto lut = readLutFile(path, &why);
            if (!lut) {
                if (failures) *failures << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), why);
                continue;
            }
            const QString name = externalLutName(path);
            registerColorTransformation(name, lutTransformation(*lut));
            if (!names.contains(name)) names << name;
        }
    }
    return names;
}

QString projectLutFolder(const QString& projectFolder) {
    return projectFolder.isEmpty() ? QString() : QDir(projectFolder).filePath(QStringLiteral("LUTs"));
}

QString sharedLutFolder() {
    const QString named = qEnvironmentVariable("LUMENIS_SHARED_LUTS");
    if (!named.isEmpty()) return named;
    if (qEnvironmentVariable("LUMENIS_NO_SYSTEM_DRIVES") == QLatin1String("1")) return {};
    // The application's own data folder, which carries its name (cmake/Branding.cmake).
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("ColorManagement/LUTs"));
}

QString installLut(const QString& file, const QString& folder, QString* error) {
    const auto lut = readLutFile(file, error);
    if (!lut) return {};
    if (!QDir().mkpath(folder)) {
        if (error) *error = QObject::tr("The folder %1 cannot be made.").arg(folder);
        return {};
    }
    const QString target = QDir(folder).filePath(QFileInfo(file).fileName());
    if (QFileInfo(target).absoluteFilePath() != QFileInfo(file).absoluteFilePath()) {
        QFile::remove(target);
        if (!QFile::copy(file, target)) {
            if (error) *error = QObject::tr("The file cannot be copied to %1.").arg(folder);
            return {};
        }
    }
    const QString name = externalLutName(target);
    registerColorTransformation(name, lutTransformation(*lut));
    return name;
}

} // namespace lumenis::media
