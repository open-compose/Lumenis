#include "lumenis/model/project_store.h"

#include "lumenis/model/project_bins.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

// Projects on disk (project_store.h): listing, creating, deleting, reading and writing the
// "<name>.cthproj" project file, and user profile folders. The Select Project window
// (src/ui/select_project_dialog.*) drives it; the project's bins are managed by ProjectBins
// (project_bins.*), which create() asks for the project's first bin.
namespace lumenis::model {

namespace {

constexpr int kFileVersion = 1;
constexpr auto kFileFormat = "cutthulhu-project";

bool fail(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
    return false;
}

// Rationals are stored as "num/den" text; parseRational() also takes a bare integer.
QString rationalText(const Rational& r) {
    return QString::fromStdString(r.toString());
}

std::optional<Rational> parseRational(const QString& text) {
    const QStringList parts = text.split(QLatin1Char('/'));
    bool okNum = false;
    bool okDen = true;
    const qint64 num = parts.value(0).toLongLong(&okNum);
    const qint64 den = parts.size() > 1 ? parts.value(1).toLongLong(&okDen) : 1;
    if (!okNum || !okDen || den == 0 || parts.size() > 2) {
        return std::nullopt;
    }
    return Rational(num, den);
}

QString familyText(FormatFamily f) {
    switch (f) {
    case FormatFamily::SD: return QStringLiteral("SD");
    case FormatFamily::HD: return QStringLiteral("HD");
    case FormatFamily::HighRes: return QStringLiteral("HighRes");
    }
    return {};
}

std::optional<FormatFamily> parseFamily(const QString& s) {
    if (s == QLatin1String("SD")) return FormatFamily::SD;
    if (s == QLatin1String("HD")) return FormatFamily::HD;
    if (s == QLatin1String("HighRes")) return FormatFamily::HighRes;
    return std::nullopt;
}

// The project file's JSON layout. Dates are ISO 8601 in UTC; filmType is written only when set.
QJsonObject toJson(const ProjectSettings& s) {
    QJsonObject format{
        {QStringLiteral("group"), QString::fromStdString(s.format.group)},
        {QStringLiteral("family"), familyText(s.format.family)},
        {QStringLiteral("width"), s.format.width},
        {QStringLiteral("height"), s.format.height},
        {QStringLiteral("editRate"), rationalText(s.format.editRate)},
        {QStringLiteral("scan"), s.format.scan == ScanMode::Interlaced ? QStringLiteral("interlaced")
                                                                        : QStringLiteral("progressive")},
    };
    QJsonObject o{
        {QStringLiteral("format"), QString::fromLatin1(kFileFormat)},
        {QStringLiteral("version"), kFileVersion},
        {QStringLiteral("name"), s.name},
        {QStringLiteral("chooseForMe"), s.chooseForMe},
        {QStringLiteral("projectFormat"), format},
        {QStringLiteral("aspect"), rationalText(s.aspect)},
        {QStringLiteral("colorSpace"), s.colorSpace},
        {QStringLiteral("colorDepth"), s.colorDepth},
        {QStringLiteral("stereoscopic"), s.stereoscopic},
        {QStringLiteral("created"), s.created.toUTC().toString(Qt::ISODate)},
        {QStringLiteral("modified"), s.modified.toUTC().toString(Qt::ISODate)},
    };
    if (!s.filmType.isEmpty()) {
        o.insert(QStringLiteral("filmType"), s.filmType);
    }
    if (s.proxy != 0) o.insert(QStringLiteral("proxy"), s.proxy);
    if (!s.mask.isEmpty() || !s.mask.preset.isEmpty()) o.insert(QStringLiteral("maskMargins"), s.mask.toJson());
    return o;
}

} // namespace

QList<int> proxyModes(const ProjectFormat& format) {
    QList<int> modes{0};
    if (format.family != FormatFamily::HighRes) return modes;
    for (const int mode : {4, 16}) {
        const int divide = mode == 4 ? 2 : 4;
        if (format.width / divide >= 256 && format.height / divide >= 120) modes << mode;
    }
    return modes;
}

QSize proxyRaster(const ProjectSettings& settings) {
    const QSize full(settings.format.width, settings.format.height);
    if (settings.proxy == 0 || !proxyModes(settings.format).contains(settings.proxy)) return full;
    const int divide = settings.proxy == 4 ? 2 : 4;
    return QSize(full.width() / divide / 2 * 2, full.height() / divide / 2 * 2);
}

ProjectStore::ProjectStore(QString root) : root_(QDir::cleanPath(std::move(root))) {}

QList<ProjectEntry> ProjectStore::list() const {
    QList<ProjectEntry> result;
    const QDir dir(root_);
    for (const QString& folder : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString file = dir.filePath(folder + QLatin1Char('/') + folder + fileSuffix());
        if (auto settings = read(file)) {
            result.append(ProjectEntry{dir.filePath(folder), file, *settings});
        }
    }
    std::sort(result.begin(), result.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
        return a.settings.name.compare(b.settings.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

// The project folder is removed again if the project file or the first bin cannot be written.
std::optional<ProjectEntry> ProjectStore::create(ProjectSettings settings, QString* error) const {
    if (const auto problem = validateName(settings.name.toStdString(), kMaxProjectNameLength)) {
        fail(error, QString::fromStdString(*problem));
        return std::nullopt;
    }
    const QDir rootDir(root_);
    if (!rootDir.mkpath(QStringLiteral("."))) {
        fail(error, QStringLiteral("cannot create %1").arg(root_));
        return std::nullopt;
    }
    // Compare case-insensitively: Windows and macOS file systems usually are.
    for (const QString& existing : rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (existing.compare(settings.name, Qt::CaseInsensitive) == 0) {
            fail(error, QStringLiteral("a project named \"%1\" already exists").arg(existing));
            return std::nullopt;
        }
    }
    const QString folder = rootDir.filePath(settings.name);
    if (!QDir().mkpath(folder)) {
        fail(error, QStringLiteral("cannot create %1").arg(folder));
        return std::nullopt;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!settings.created.isValid()) {
        settings.created = now;
    }
    if (!settings.modified.isValid()) {
        settings.modified = settings.created;
    }
    const QString file = QDir(folder).filePath(settings.name + fileSuffix());
    if (!write(file, settings, error)) {
        QDir(folder).removeRecursively();
        return std::nullopt;
    }
    // "When you create a project, Media Composer automatically creates a bin
    // with the name of the new project" (guide p.294).
    ProjectBins bins(folder, settings.name);
    if (bins.createInitialBin(error).isEmpty() || !bins.save(error)) {
        QDir(folder).removeRecursively();
        return std::nullopt;
    }
    return ProjectEntry{folder, file, settings};
}

// The name must be a plain folder name, so nothing outside the root can be deleted.
bool ProjectStore::remove(const QString& name, QString* error) const {
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
        || name == QLatin1String("..") || name == QLatin1String(".")) {
        return fail(error, QStringLiteral("invalid project name"));
    }
    QDir folder(QDir(root_).filePath(name));
    // Refuse to delete a folder that is not a project.
    if (!QFile::exists(folder.filePath(name + fileSuffix()))) {
        return fail(error, QStringLiteral("no project named \"%1\"").arg(name));
    }
    if (!folder.removeRecursively()) {
        return fail(error, QStringLiteral("could not delete all of %1").arg(folder.path()));
    }
    return true;
}

std::optional<ProjectSettings> ProjectStore::read(const QString& file, QString* error) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        fail(error, f.errorString());
        return std::nullopt;
    }
    QJsonParseError parseError{};
    const QJsonObject o = QJsonDocument::fromJson(f.readAll(), &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        fail(error, parseError.errorString());
        return std::nullopt;
    }
    if (o.value(QStringLiteral("format")).toString() != QLatin1String(kFileFormat)) {
        fail(error, QStringLiteral("not a project file"));
        return std::nullopt;
    }
    if (o.value(QStringLiteral("version")).toInt() > kFileVersion) {
        fail(error, QStringLiteral("project was saved by a newer version"));
        return std::nullopt;
    }

    ProjectSettings s;
    s.name = o.value(QStringLiteral("name")).toString();
    s.chooseForMe = o.value(QStringLiteral("chooseForMe")).toBool();
    const QJsonObject pf = o.value(QStringLiteral("projectFormat")).toObject();
    s.format.group = pf.value(QStringLiteral("group")).toString().toStdString();
    const auto family = parseFamily(pf.value(QStringLiteral("family")).toString());
    const auto rate = parseRational(pf.value(QStringLiteral("editRate")).toString());
    const auto aspect = parseRational(o.value(QStringLiteral("aspect")).toString());
    if (s.name.isEmpty() || !family || !rate || !aspect || rate->num() <= 0) {
        fail(error, QStringLiteral("project file is incomplete"));
        return std::nullopt;
    }
    s.format.family = *family;
    s.format.width = pf.value(QStringLiteral("width")).toInt();
    s.format.height = pf.value(QStringLiteral("height")).toInt();
    s.format.editRate = *rate;
    s.format.scan = pf.value(QStringLiteral("scan")).toString() == QLatin1String("interlaced") ? ScanMode::Interlaced
                                                                                                : ScanMode::Progressive;
    s.aspect = *aspect;
    s.colorSpace = o.value(QStringLiteral("colorSpace")).toString();
    s.colorDepth = o.value(QStringLiteral("colorDepth")).toString();
    s.stereoscopic = o.value(QStringLiteral("stereoscopic")).toString(QStringLiteral("Off"));
    s.filmType = o.value(QStringLiteral("filmType")).toString();
    // A proxy mode the format does not offer is none.
    s.proxy = o.value(QStringLiteral("proxy")).toInt();
    if (!proxyModes(s.format).contains(s.proxy)) s.proxy = 0;
    s.mask = MaskMargins::fromJson(o.value(QStringLiteral("maskMargins")).toObject());
    s.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
    s.modified = QDateTime::fromString(o.value(QStringLiteral("modified")).toString(), Qt::ISODate);
    return s;
}

bool ProjectStore::write(const QString& file, const ProjectSettings& settings, QString* error) {
    // QSaveFile writes to a temporary file and renames it, so a crash never
    // leaves a half-written project file.
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        return fail(error, f.errorString());
    }
    f.write(QJsonDocument(toJson(settings)).toJson());
    if (!f.commit()) {
        return fail(error, f.errorString());
    }
    return true;
}

UserProfiles::UserProfiles(QString root) : root_(QDir::cleanPath(std::move(root))) {}

QStringList UserProfiles::list() const {
    QStringList names = QDir(root_).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool UserProfiles::create(const QString& name, QString* error) const {
    if (const auto problem = validateName(name.toStdString(), kMaxProjectNameLength)) {
        return fail(error, QString::fromStdString(*problem));
    }
    if (!QDir(root_).mkpath(name)) {
        return fail(error, QStringLiteral("cannot create the profile folder"));
    }
    return true;
}

// Falls back to "editor" when neither variable is set.
QString UserProfiles::defaultName() {
    QString name = qEnvironmentVariable("USERNAME"); // Windows
    if (name.isEmpty()) {
        name = qEnvironmentVariable("USER"); // macOS, Linux
    }
    return name.isEmpty() ? QStringLiteral("editor") : name;
}

} // namespace lumenis::model
