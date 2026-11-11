// Photoshop's layers (psd.h; Editing Guide p.218-221; Adobe Photoshop File Formats Specification).
#include "lumenis/media/psd.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QtEndian>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace lumenis::media {

namespace {

// A big-endian reader that stops at the end instead of reading past it.
class Reader {
public:
    explicit Reader(const QByteArray& d) : d_(d) {}
    bool ok() const { return ok_; }
    qsizetype pos() const { return pos_; }
    qsizetype left() const { return d_.size() - pos_; }
    void seek(qsizetype p) {
        if (p < 0 || p > d_.size()) ok_ = false;
        else pos_ = p;
    }
    void skip(qsizetype n) { seek(pos_ + n); }
    template <typename T>
    T get() {
        if (!ok_ || left() < static_cast<qsizetype>(sizeof(T))) {
            ok_ = false;
            return T{};
        }
        const T v = qFromBigEndian<T>(d_.constData() + pos_);
        pos_ += static_cast<qsizetype>(sizeof(T));
        return v;
    }
    QByteArray bytes(qsizetype n) {
        if (!ok_ || n < 0 || left() < n) {
            ok_ = false;
            return {};
        }
        const QByteArray b = d_.mid(pos_, n);
        pos_ += n;
        return b;
    }
    const uchar* data() const { return reinterpret_cast<const uchar*>(d_.constData()); }

private:
    const QByteArray& d_;
    qsizetype pos_ = 0;
    bool ok_ = true;
};

// PackBits: n >= 0 copies n + 1 bytes, n < 0 repeats the next byte 1 - n times (-128 does nothing).
bool unpackBits(const uchar* in, qsizetype size, uchar* out, qsizetype want) {
    qsizetype i = 0, o = 0;
    while (o < want && i < size) {
        const auto n = static_cast<std::int8_t>(in[i++]);
        if (n >= 0) {
            const qsizetype count = n + 1;
            if (i + count > size || o + count > want) return false;
            std::copy(in + i, in + i + count, out + o);
            i += count;
            o += count;
        } else if (n != -128) {
            const qsizetype count = 1 - n;
            if (i >= size || o + count > want) return false;
            std::fill(out + o, out + o + count, in[i++]);
            o += count;
        }
    }
    return o == want;
}

// One channel's samples (8-bit; 16-bit kept by their high byte) for `rows` x `columns`, from `data` of `length` bytes
// (compression word included). False for what cannot be read.
bool channelSamples(const uchar* data, qsizetype length, int rows, int columns, int depth, bool psb, QByteArray* out) {
    const qsizetype bytesPerSample = depth == 16 ? 2 : 1;
    const qsizetype rowBytes = static_cast<qsizetype>(columns) * bytesPerSample;
    const qsizetype total = rowBytes * rows;
    if (rows == 0 || columns == 0) {
        out->clear();
        return true;
    }
    // Before anything is allocated: PackBits makes at most 128 bytes of two, so a channel of fewer bytes cannot be read.
    if (length < 2 || total / 64 > length) return false;
    *out = QByteArray(static_cast<qsizetype>(rows) * columns, '\0');
    const int compression = qFromBigEndian<std::uint16_t>(data);
    data += 2;
    length -= 2;
    QByteArray raw;
    if (compression == 0) {
        if (length < total) return false;
        raw = QByteArray(reinterpret_cast<const char*>(data), total);
    } else if (compression == 1) {
        const qsizetype countBytes = psb ? 4 : 2;
        if (length < countBytes * rows) return false;
        raw.resize(total);
        qsizetype at = countBytes * rows;
        for (int r = 0; r < rows; ++r) {
            const qsizetype n = psb ? static_cast<qsizetype>(qFromBigEndian<std::uint32_t>(data + countBytes * r))
                                    : static_cast<qsizetype>(qFromBigEndian<std::uint16_t>(data + countBytes * r));
            if (at + n > length) return false;
            if (!unpackBits(data + at, n, reinterpret_cast<uchar*>(raw.data()) + rowBytes * r, rowBytes)) return false;
            at += n;
        }
    } else {
        return false; // ZIP (2, 3): not read
    }
    char* o = out->data();
    for (qsizetype i = 0; i < static_cast<qsizetype>(rows) * columns; ++i) o[i] = raw[i * bytesPerSample];
    return true;
}

bool fail(QString* error, const QString& why) {
    if (error) *error = why;
    return false;
}

void put16(QByteArray& d, std::uint16_t v) {
    char b[2];
    qToBigEndian(v, b);
    d.append(b, 2);
}
void put32(QByteArray& d, std::uint32_t v) {
    char b[4];
    qToBigEndian(v, b);
    d.append(b, 4);
}

} // namespace

bool isPsdFile(const QString& path) { return QFileInfo(path).suffix().compare(QLatin1String("psd"), Qt::CaseInsensitive) == 0; }

std::optional<PsdFile> readPsdData(const QByteArray& data, QString* error) {
    Reader r(data);
    if (data.size() < 26 || !data.startsWith("8BPS")) {
        fail(error, QStringLiteral("not a Photoshop file"));
        return std::nullopt;
    }
    r.skip(4);
    const int version = r.get<std::uint16_t>();
    const bool psb = version == 2;
    if (version != 1 && version != 2) {
        fail(error, QStringLiteral("an unknown Photoshop file version"));
        return std::nullopt;
    }
    r.skip(6);
    r.get<std::uint16_t>(); // the merged picture's channels
    PsdFile file;
    const auto height = r.get<std::uint32_t>();
    const auto width = r.get<std::uint32_t>();
    file.depth = r.get<std::uint16_t>();
    file.mode = r.get<std::uint16_t>();
    // "Graphics must be RGB 8 or 16 bits, or grayscale" (p.220); duotone files "are loaded as grayscale" (p.1231).
    if (!r.ok() || width == 0 || height == 0 || width > 300000 || height > 300000) {
        fail(error, QStringLiteral("a Photoshop file of no size"));
        return std::nullopt;
    }
    if ((file.depth != 8 && file.depth != 16) || (file.mode != 1 && file.mode != 3 && file.mode != 8)) {
        fail(error, QStringLiteral("a Photoshop colour mode or depth that cannot be imported (RGB or grayscale, 8 or 16 bits)"));
        return std::nullopt;
    }
    file.size = QSize(static_cast<int>(width), static_cast<int>(height));
    const bool gray = file.mode != 3;
    r.skip(r.get<std::uint32_t>()); // colour mode data
    r.skip(r.get<std::uint32_t>()); // image resources
    const qsizetype layerAndMask = psb ? static_cast<qsizetype>(r.get<std::uint64_t>()) : static_cast<qsizetype>(r.get<std::uint32_t>());
    if (!r.ok() || layerAndMask > r.left()) {
        fail(error, QStringLiteral("a broken Photoshop file"));
        return std::nullopt;
    }
    if (layerAndMask <= 0) return file; // flattened
    const qsizetype layerInfo = psb ? static_cast<qsizetype>(r.get<std::uint64_t>()) : static_cast<qsizetype>(r.get<std::uint32_t>());
    if (!r.ok() || layerInfo <= 0) return file; // flattened
    if (layerInfo > r.left()) {
        fail(error, QStringLiteral("a broken Photoshop layer section"));
        return std::nullopt;
    }
    const qsizetype infoEnd = r.pos() + layerInfo;
    const int count = std::abs(static_cast<int>(r.get<std::int16_t>()));
    struct Channel {
        int id;
        qsizetype length;
    };
    struct Record {
        PsdLayer layer;
        QList<Channel> channels;
    };
    QList<Record> records;
    for (int i = 0; i < count && r.ok(); ++i) {
        Record rec;
        const auto top = r.get<std::int32_t>(), left = r.get<std::int32_t>(), bottom = r.get<std::int32_t>(), right = r.get<std::int32_t>();
        if (bottom < top || right < left || static_cast<std::int64_t>(bottom) - top > 300000 || static_cast<std::int64_t>(right) - left > 300000) {
            fail(error, QStringLiteral("a broken Photoshop layer"));
            return std::nullopt;
        }
        rec.layer.rect = QRect(left, top, right - left, bottom - top);
        const int channels = r.get<std::uint16_t>();
        if (channels > 56) {
            fail(error, QStringLiteral("a broken Photoshop layer"));
            return std::nullopt;
        }
        for (int c = 0; c < channels; ++c) {
            Channel ch;
            ch.id = r.get<std::int16_t>();
            ch.length = psb ? static_cast<qsizetype>(r.get<std::uint64_t>()) : static_cast<qsizetype>(r.get<std::uint32_t>());
            if (ch.length < 0) r.seek(-1);
            rec.channels << ch;
            if (ch.id == -1) rec.layer.transparency = true;
        }
        if (r.bytes(4) != "8BIM") {
            fail(error, QStringLiteral("a broken Photoshop layer"));
            return std::nullopt;
        }
        r.skip(4); // blend mode: only normal is supported, and import ignores the others (p.220)
        rec.layer.opacity = r.get<std::uint8_t>();
        r.get<std::uint8_t>(); // clipping
        const int flags = r.get<std::uint8_t>();
        rec.layer.visible = (flags & 0x02) == 0; // bit 1: hidden
        r.get<std::uint8_t>();
        const auto extra = r.get<std::uint32_t>();
        const qsizetype extraEnd = r.pos() + static_cast<qsizetype>(extra);
        if (!r.ok() || extraEnd > infoEnd) {
            fail(error, QStringLiteral("a broken Photoshop layer"));
            return std::nullopt;
        }
        r.skip(r.get<std::uint32_t>()); // layer mask
        r.skip(r.get<std::uint32_t>()); // blending ranges
        const int nameLength = r.get<std::uint8_t>();
        rec.layer.name = QString::fromLatin1(r.bytes(nameLength));
        r.skip((4 - (1 + nameLength) % 4) % 4);
        // Additional layer information: the Unicode name and the section (group) markers.
        while (r.ok() && r.pos() + 12 <= extraEnd) {
            const QByteArray sig = r.bytes(4);
            if (sig != "8BIM" && sig != "8B64") break;
            const QByteArray key = r.bytes(4);
            const qsizetype length = static_cast<qsizetype>(r.get<std::uint32_t>());
            const qsizetype next = r.pos() + length;
            if (!r.ok() || next > extraEnd) break;
            if (key == "luni" && length >= 4) {
                const auto chars = r.get<std::uint32_t>();
                if (chars <= static_cast<std::uint32_t>((length - 4) / 2)) {
                    QString name;
                    for (std::uint32_t c = 0; c < chars; ++c) name += QChar(r.get<std::uint16_t>());
                    if (!name.isEmpty()) rec.layer.name = name;
                }
            } else if (key == "lsct" && length >= 4) {
                const auto type = r.get<std::uint32_t>();
                rec.layer.divider = type >= 1 && type <= 3;
            }
            r.seek(next);
        }
        r.seek(extraEnd);
        records << rec;
    }
    if (!r.ok()) {
        fail(error, QStringLiteral("a broken Photoshop layer section"));
        return std::nullopt;
    }
    // The channels' pixels, layer after layer.
    for (Record& rec : records) {
        const int w = rec.layer.rect.width(), h = rec.layer.rect.height();
        QByteArray planes[4]; // R, G, B (gray: R), A
        bool have[4] = {false, false, false, false};
        for (const Channel& ch : rec.channels) {
            const qsizetype at = r.pos();
            if (ch.length < 0 || ch.length > r.left()) {
                fail(error, QStringLiteral("a broken Photoshop layer's pixels"));
                return std::nullopt;
            }
            const int slot = ch.id == -1 ? 3 : (ch.id >= 0 && ch.id <= (gray ? 0 : 2) ? ch.id : -1);
            if (slot >= 0) {
                if (!channelSamples(r.data() + at, ch.length, h, w, file.depth, psb, &planes[slot])) {
                    fail(error, QStringLiteral("a Photoshop layer's pixels that cannot be read"));
                    return std::nullopt;
                }
                have[slot] = true;
            }
            r.seek(at + ch.length); // masks (-2, -3) and other channels are passed over
        }
        if (w <= 0 || h <= 0 || !have[0]) continue;
        QImage image(w, h, QImage::Format_RGBA8888);
        for (int y = 0; y < h; ++y) {
            uchar* line = image.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const qsizetype i = static_cast<qsizetype>(y) * w + x;
                const auto red = static_cast<uchar>(planes[0][i]);
                line[4 * x] = red;
                line[4 * x + 1] = gray || !have[1] ? red : static_cast<uchar>(planes[1][i]);
                line[4 * x + 2] = gray || !have[2] ? red : static_cast<uchar>(planes[2][i]);
                line[4 * x + 3] = have[3] ? static_cast<uchar>(planes[3][i]) : 255;
            }
        }
        rec.layer.image = image;
    }
    for (const Record& rec : records) file.layers << rec.layer;
    return file;
}

std::optional<PsdFile> readPsdFile(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("cannot be opened"));
        return std::nullopt;
    }
    return readPsdData(f.readAll(), error);
}

QList<PsdLayer> importableLayers(const PsdFile& file) {
    QList<PsdLayer> out;
    for (const PsdLayer& l : file.layers) {
        if (!l.divider && !l.image.isNull() && !l.rect.isEmpty()) out << l;
    }
    return out;
}

QImage psdLayerPicture(const PsdFile& file, const PsdLayer& layer) {
    QImage canvas(file.size, QImage::Format_RGBA8888);
    canvas.fill(Qt::transparent);
    if (layer.image.isNull()) return canvas;
    // Copied, not blended: the layer's own alpha is its matte.
    const QRect visible = layer.rect.intersected(QRect(QPoint(0, 0), file.size));
    for (int y = visible.top(); y <= visible.bottom(); ++y) {
        const uchar* in = layer.image.constScanLine(y - layer.rect.top());
        uchar* out = canvas.scanLine(y);
        for (int x = visible.left(); x <= visible.right(); ++x) {
            std::copy(in + 4 * (x - layer.rect.left()), in + 4 * (x - layer.rect.left()) + 4, out + 4 * x);
        }
    }
    return canvas;
}

QByteArray psdData(const PsdFile& file, bool rle) {
    QByteArray d("8BPS");
    put16(d, 1);
    d.append(6, '\0');
    put16(d, 3);
    put32(d, static_cast<std::uint32_t>(file.size.height()));
    put32(d, static_cast<std::uint32_t>(file.size.width()));
    put16(d, 8);
    put16(d, 3);
    put32(d, 0); // colour mode data
    put32(d, 0); // image resources
    // Layer records, then their channels.
    QByteArray records;
    QByteArray pixels;
    put16(records, static_cast<std::uint16_t>(file.layers.size()));
    for (const PsdLayer& l : file.layers) {
        const QImage img = l.image.convertToFormat(QImage::Format_RGBA8888);
        const bool alpha = l.transparency;
        put32(records, static_cast<std::uint32_t>(l.rect.top()));
        put32(records, static_cast<std::uint32_t>(l.rect.left()));
        put32(records, static_cast<std::uint32_t>(l.rect.bottom() + 1));
        put32(records, static_cast<std::uint32_t>(l.rect.right() + 1));
        const QList<int> ids = alpha ? QList<int>{-1, 0, 1, 2} : QList<int>{0, 1, 2};
        put16(records, static_cast<std::uint16_t>(ids.size()));
        for (int id : ids) {
            put16(records, static_cast<std::uint16_t>(static_cast<std::int16_t>(id)));
            const int component = id == -1 ? 3 : id;
            QByteArray plane;
            put16(plane, rle ? 1 : 0);
            QList<QByteArray> rows;
            for (int y = 0; y < img.height(); ++y) {
                const uchar* line = img.constScanLine(y);
                QByteArray row;
                for (int x = 0; x < img.width(); ++x) row.append(static_cast<char>(line[4 * x + component]));
                if (rle) {
                    // PackBits: runs of a repeated byte, literals between them.
                    QByteArray packed;
                    for (qsizetype i = 0; i < row.size();) {
                        qsizetype run = 1;
                        while (i + run < row.size() && run < 128 && row[i + run] == row[i]) ++run;
                        if (run >= 2) {
                            packed.append(static_cast<char>(1 - run));
                            packed.append(row[i]);
                            i += run;
                        } else {
                            qsizetype n = 1;
                            while (i + n < row.size() && n < 128 && (i + n + 1 >= row.size() || row[i + n] != row[i + n + 1])) ++n;
                            packed.append(static_cast<char>(n - 1));
                            packed.append(row.mid(i, n));
                            i += n;
                        }
                    }
                    row = packed;
                }
                rows << row;
            }
            if (rle) {
                for (const QByteArray& row : rows) put16(plane, static_cast<std::uint16_t>(row.size()));
            }
            for (const QByteArray& row : rows) plane.append(row);
            put32(records, static_cast<std::uint32_t>(plane.size()));
            pixels.append(plane);
        }
        records.append("8BIMnorm");
        records.append(static_cast<char>(l.opacity));
        records.append('\0');
        records.append(static_cast<char>(l.visible ? 0x00 : 0x02));
        records.append('\0');
        QByteArray extra;
        put32(extra, 0); // mask
        put32(extra, 0); // blending ranges
        const QByteArray name = l.name.toLatin1().left(255);
        extra.append(static_cast<char>(name.size()));
        extra.append(name);
        extra.append((4 - (1 + name.size()) % 4) % 4, '\0');
        // The Unicode name.
        QByteArray luni;
        put32(luni, static_cast<std::uint32_t>(l.name.size()));
        for (const QChar c : l.name) put16(luni, c.unicode());
        if (luni.size() % 4) luni.append(4 - luni.size() % 4, '\0');
        extra.append("8BIMluni");
        put32(extra, static_cast<std::uint32_t>(luni.size()));
        extra.append(luni);
        put32(records, static_cast<std::uint32_t>(extra.size()));
        records.append(extra);
    }
    QByteArray layerInfo = records + pixels;
    if (layerInfo.size() % 2) layerInfo.append('\0');
    QByteArray section;
    put32(section, static_cast<std::uint32_t>(layerInfo.size()));
    section.append(layerInfo);
    put32(section, 0); // global layer mask info
    put32(d, static_cast<std::uint32_t>(section.size()));
    d.append(section);
    // The merged picture: the layers over white, raw, planar.
    QImage merged(file.size, QImage::Format_RGB32);
    merged.fill(Qt::white);
    {
        QPainter p(&merged);
        for (const PsdLayer& l : file.layers) {
            if (!l.visible) continue;
            p.setOpacity(l.opacity / 255.0);
            p.drawImage(l.rect.topLeft(), l.image);
        }
    }
    put16(d, 0);
    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < merged.height(); ++y) {
            const auto* line = reinterpret_cast<const QRgb*>(merged.constScanLine(y));
            for (int x = 0; x < merged.width(); ++x) {
                const QRgb px = line[x];
                d.append(static_cast<char>(c == 0 ? qRed(px) : c == 1 ? qGreen(px) : qBlue(px)));
            }
        }
    }
    return d;
}

} // namespace lumenis::media
