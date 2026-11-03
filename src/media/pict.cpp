// PICT reading and writing (pict.h). Opcodes and records from Inside Macintosh: Imaging With QuickDraw (1994), appendix
// A; everything big-endian.

#include "lumenis/media/pict.h"

#include <QFile>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace lumenis::media {

namespace {

void put16(QByteArray& out, int v) {
    out.append(static_cast<char>((v >> 8) & 0xff));
    out.append(static_cast<char>(v & 0xff));
}

void put32(QByteArray& out, std::uint32_t v) {
    put16(out, static_cast<int>(v >> 16));
    put16(out, static_cast<int>(v & 0xffff));
}

void putRect(QByteArray& out, int width, int height) {
    put16(out, 0);
    put16(out, 0);
    put16(out, height);
    put16(out, width);
}

// PackBits: runs of three or more equal bytes as (1 - n, byte), the rest as literals of up to 128 (count - 1, bytes).
QByteArray packBits(const unsigned char* data, int n) {
    QByteArray out;
    int i = 0;
    while (i < n) {
        int run = 1;
        while (i + run < n && run < 128 && data[i + run] == data[i]) ++run;
        if (run >= 3) {
            out.append(static_cast<char>(1 - run));
            out.append(static_cast<char>(data[i]));
            i += run;
            continue;
        }
        int start = i;
        int count = 0;
        while (i < n && count < 128) {
            if (i + 2 < n && data[i] == data[i + 1] && data[i] == data[i + 2]) break;
            ++i;
            ++count;
        }
        out.append(static_cast<char>(count - 1));
        out.append(reinterpret_cast<const char*>(data + start), count);
    }
    return out;
}

// Reads big-endian values from the picture; any read past the end fails the whole picture.
struct Reader {
    const QByteArray& data;
    qsizetype at = 0;
    bool ok = true;
    int u8() {
        if (at + 1 > data.size()) return ok = false, 0;
        return static_cast<unsigned char>(data.at(at++));
    }
    int u16() {
        const int hi = u8();
        return hi << 8 | u8();
    }
    std::int16_t s16() { return static_cast<std::int16_t>(u16()); }
    std::uint32_t u32() {
        const auto hi = static_cast<std::uint32_t>(u16());
        return hi << 16 | static_cast<std::uint32_t>(u16());
    }
    void skip(qsizetype n) {
        if (n < 0 || at + n > data.size()) ok = false;
        else at += n;
    }
    void align() {
        if (at % 2) ++at;
    }
};

std::optional<QByteArray> unpackBits(Reader& r, qsizetype packed, qsizetype want) {
    QByteArray out;
    const qsizetype end = r.at + packed;
    while (r.ok && r.at < end && out.size() < want) {
        const auto n = static_cast<std::int8_t>(r.u8());
        if (n >= 0) {
            for (int k = 0; k <= n; ++k) out.append(static_cast<char>(r.u8()));
        } else if (n != -128) {
            const char b = static_cast<char>(r.u8());
            out.append(QByteArray(1 - n, b));
        }
    }
    r.at = end;
    if (!r.ok || out.size() < want) return std::nullopt;
    out.truncate(want);
    return out;
}

} // namespace

QByteArray pictData(const QImage& source, bool alpha) {
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width(), h = image.height();
    QByteArray out(512, '\0'); // the file header, unused
    const qsizetype start = out.size();
    put16(out, 0); // picSize, set below
    putRect(out, w, h);
    put16(out, 0x0011); // VersionOp
    put16(out, 0x02ff); // version 2
    put16(out, 0x0c00); // HeaderOp, extended version 2
    put16(out, 0xfffe);
    put16(out, 0);
    put32(out, 0x00480000); // 72 dpi
    put32(out, 0x00480000);
    putRect(out, w, h);
    put32(out, 0);
    put16(out, 0x001e); // DefHilite
    put16(out, 0x0001); // Clip: a rectangular region
    put16(out, 10);
    putRect(out, w, h);
    put16(out, 0x009a); // DirectBitsRect
    put32(out, 0x000000ff); // baseAddr for direct pixels
    const int rowBytes = w * 4;
    put16(out, rowBytes | 0x8000);
    putRect(out, w, h);
    put16(out, 0); // pmVersion
    put16(out, 4); // packType: components packed separately
    put32(out, 0); // packSize
    put32(out, 0x00480000);
    put32(out, 0x00480000);
    put16(out, 16); // pixelType: RGBDirect
    put16(out, 32); // pixelSize
    const int components = alpha ? 4 : 3;
    put16(out, components);
    put16(out, 8); // cmpSize
    put32(out, 0); // planeBytes
    put32(out, 0); // pmTable
    put32(out, 0); // pmReserved
    putRect(out, w, h); // srcRect
    putRect(out, w, h); // dstRect
    put16(out, 0);      // mode: srcCopy
    std::vector<unsigned char> row(static_cast<std::size_t>(w) * static_cast<std::size_t>(components));
    for (int y = 0; y < h; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        int c = 0;
        // The planes of the row: alpha (when kept), red, green, blue.
        auto plane = [&](auto pick) {
            for (int x = 0; x < w; ++x) row[static_cast<std::size_t>(c * w + x)] = static_cast<unsigned char>(pick(line[x]));
            ++c;
        };
        if (alpha) plane([](QRgb p) { return qAlpha(p); });
        plane([](QRgb p) { return qRed(p); });
        plane([](QRgb p) { return qGreen(p); });
        plane([](QRgb p) { return qBlue(p); });
        const QByteArray packed = packBits(row.data(), static_cast<int>(row.size()));
        // "If rowBytes is greater than 250, the byte count is a word; otherwise it is a byte."
        if (rowBytes > 250) put16(out, static_cast<int>(packed.size()));
        else out.append(static_cast<char>(packed.size()));
        out.append(packed);
    }
    if ((out.size() - start) % 2) out.append('\0');
    put16(out, 0x00ff); // OpEndPic
    const auto size = static_cast<int>((out.size() - start) & 0xffff);
    out[start] = static_cast<char>((size >> 8) & 0xff);
    out[start + 1] = static_cast<char>(size & 0xff);
    return out;
}

bool writePict(const QImage& image, const QString& path, bool alpha) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray data = pictData(image, alpha);
    return f.write(data) == data.size();
}

QImage readPictData(const QByteArray& bytes) {
    // With the 512-byte file header or without: version 2's opcode follows the picture's size and frame.
    auto versionAt = [&](qsizetype base) {
        return bytes.size() >= base + 14 && static_cast<unsigned char>(bytes.at(base + 10)) == 0x00 &&
               static_cast<unsigned char>(bytes.at(base + 11)) == 0x11 && static_cast<unsigned char>(bytes.at(base + 12)) == 0x02 &&
               static_cast<unsigned char>(bytes.at(base + 13)) == 0xff;
    };
    qsizetype base = versionAt(512) ? 512 : versionAt(0) ? 0 : -1;
    if (base < 0) return {}; // version 1 pictures are not read
    Reader r{bytes, base + 14};
    while (r.ok) {
        r.align();
        const int op = r.u16();
        switch (op) {
        case 0x0000: // NOP
        case 0x001e: // DefHilite
            break;
        case 0x0c00: r.skip(24); break;
        case 0x0001: { // Clip
            const int size = r.u16();
            r.skip(size - 2);
            break;
        }
        case 0x001a: case 0x001b: case 0x001d: case 0x001f: r.skip(6); break; // colours
        case 0x00a0: r.skip(2); break; // ShortComment
        case 0x00a1: { // LongComment
            r.skip(2);
            r.skip(r.u16());
            break;
        }
        case 0x009a:
        case 0x009b: {
            r.skip(4); // baseAddr
            const int rowBytes = r.u16() & 0x3fff;
            const int top = r.s16(), left = r.s16(), bottom = r.s16(), right = r.s16();
            r.skip(2);
            const int packType = r.u16();
            r.skip(4 + 8);
            const int pixelType = r.u16(), pixelSize = r.u16(), components = r.u16(), cmpSize = r.u16();
            r.skip(12);
            r.skip(16); // srcRect, dstRect
            r.skip(2);  // mode
            if (op == 0x009b) r.skip(r.u16() - 2); // the mask region
            const int w = right - left, h = bottom - top;
            if (!r.ok || w <= 0 || h <= 0 || w > 32768 || h > 32768 || pixelType != 16 || pixelSize != 32 || cmpSize != 8 ||
                (components != 3 && components != 4))
                return {};
            QImage image(w, h, QImage::Format_ARGB32);
            for (int y = 0; y < h; ++y) {
                auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
                if (packType == 4) {
                    const qsizetype count = rowBytes > 250 ? r.u16() : r.u8();
                    const auto row = unpackBits(r, count, static_cast<qsizetype>(w) * components);
                    if (!row) return {};
                    const auto* d = reinterpret_cast<const unsigned char*>(row->constData());
                    const int o = components == 4 ? 1 : 0;
                    for (int x = 0; x < w; ++x) {
                        const int a = components == 4 ? d[x] : 255;
                        line[x] = qRgba(d[o * w + x], d[(o + 1) * w + x], d[(o + 2) * w + x], a);
                    }
                } else if (packType == 1 || packType == 2) {
                    // Unpacked: four bytes a pixel, the first alpha or unused (packType 2: three bytes a pixel).
                    for (int x = 0; x < w; ++x) {
                        const int a = packType == 1 ? r.u8() : 255;
                        const int red = r.u8(), green = r.u8(), blue = r.u8();
                        line[x] = qRgba(red, green, blue, components == 4 && packType == 1 ? a : 255);
                    }
                    if (packType == 1) r.skip(rowBytes - w * 4);
                } else {
                    return {};
                }
                if (!r.ok) return {};
            }
            return image;
        }
        default:
            return {}; // anything else is not read
        }
    }
    return {};
}

QImage readPict(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return readPictData(f.readAll());
}

} // namespace lumenis::media
