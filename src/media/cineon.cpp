#include "lumenis/media/cineon.h"

#include <QDateTime>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// Our Cineon writer and reader (cineon.h): Kodak's version 4.5 layout, offsets as the format's specification gives
// them.
namespace lumenis::media {

namespace {

constexpr std::uint32_t kMagic = 0x802A5FD7;
constexpr int kHeader = 2048; // the generic header and the motion-picture film header
constexpr double kBlack = 95.0;
constexpr double kWhite = 685.0;
constexpr double kPerDecade = 300.0; // 0.6 film gamma / 0.002 density a code

// Rec. 709's transfer function and its inverse, on 0-1.
double toLinear(double v) {
    return v < 0.081 ? v / 4.5 : std::pow((v + 0.099) / 1.099, 1.0 / 0.45);
}
double fromLinear(double l) {
    return l < 0.018 ? l * 4.5 : 1.099 * std::pow(l, 0.45) - 0.099;
}

// The log code of an 8-bit video value, and back: linear 0 is code 95, linear 1 code 685.
const std::array<std::uint16_t, 256>& codeTable() {
    static const std::array<std::uint16_t, 256> table = [] {
        std::array<std::uint16_t, 256> t{};
        const double offset = std::pow(10.0, (kBlack - kWhite) / kPerDecade);
        for (int v = 0; v < 256; ++v) {
            const double lin = toLinear(v / 255.0);
            const double code = kWhite + kPerDecade * std::log10(lin * (1.0 - offset) + offset);
            t[static_cast<std::size_t>(v)] = static_cast<std::uint16_t>(std::clamp(std::lround(code), 0L, 1023L));
        }
        return t;
    }();
    return table;
}

int valueOfCode(int code) {
    const double offset = std::pow(10.0, (kBlack - kWhite) / kPerDecade);
    const double lin = (std::pow(10.0, (code - kWhite) / kPerDecade) - offset) / (1.0 - offset);
    return static_cast<int>(std::clamp(std::lround(fromLinear(std::clamp(lin, 0.0, 1.0)) * 255.0), 0L, 255L));
}

void put32(QByteArray& b, int at, std::uint32_t v) {
    qToBigEndian(v, b.data() + at);
}
void putFloat(QByteArray& b, int at, float f) {
    std::uint32_t v = 0;
    std::memcpy(&v, &f, sizeof v);
    put32(b, at, v);
}
void putText(QByteArray& b, int at, int size, const QByteArray& text) {
    std::memcpy(b.data() + at, text.constData(), static_cast<std::size_t>(std::min<qsizetype>(text.size(), size)));
}
std::uint32_t get32(const QByteArray& b, int at) {
    return qFromBigEndian<std::uint32_t>(b.constData() + at);
}

} // namespace

QByteArray cineonData(const QImage& picture) {
    const QImage rgb = picture.convertToFormat(QImage::Format_RGB32);
    const int w = rgb.width();
    const int h = rgb.height();
    const qsizetype pixels = static_cast<qsizetype>(w) * h;
    QByteArray b(kHeader + pixels * 4, '\0');
    // File information (bytes 0-191).
    put32(b, 0, kMagic);
    put32(b, 4, kHeader);                                  // offset to the image data
    put32(b, 8, 1024);                                     // generic header length
    put32(b, 12, 1024);                                    // industry specific (motion-picture film) header length
    put32(b, 16, 0);                                       // user-defined header length
    put32(b, 20, static_cast<std::uint32_t>(b.size()));   // total file size
    putText(b, 24, 8, "V4.5");
    const QDateTime now = QDateTime::currentDateTimeUtc();
    putText(b, 132, 12, now.toString(QStringLiteral("yyyy:MM:dd")).toLatin1());
    putText(b, 144, 12, now.toString(QStringLiteral("HH:mm:ss")).toLatin1() + "UTC");
    // Image information (bytes 192-679): left to right, top to bottom; three channels of 10 bits.
    b[192] = 0;
    b[193] = 3;
    for (int c = 0; c < 3; ++c) {
        const int at = 196 + c * 28;
        b[at] = 0;                                  // universal metric
        b[at + 1] = static_cast<char>(c + 1);       // red, green, blue printing density
        b[at + 2] = 10;                             // bits per pixel
        put32(b, at + 4, static_cast<std::uint32_t>(w));
        put32(b, at + 8, static_cast<std::uint32_t>(h));
        putFloat(b, at + 12, 0.0f);                 // minimum data value
        putFloat(b, at + 16, 0.0f);                 // its quantity
        putFloat(b, at + 20, 1023.0f);              // maximum data value
        putFloat(b, at + 24, 2.046f);               // its quantity: density
    }
    // Image data format (bytes 680-711): pixel interleaved, packing 5 (32-bit words, left-justified), unsigned, positive.
    b[680] = 0;
    b[681] = 5;
    b[682] = 0;
    b[683] = 0;
    // The pixels: R in bits 31-22, G 21-12, B 11-2.
    const std::array<std::uint16_t, 256>& code = codeTable();
    char* out = b.data() + kHeader;
    for (int y = 0; y < h; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            const std::uint32_t word = static_cast<std::uint32_t>(code[static_cast<std::size_t>(qRed(p))]) << 22 |
                                       static_cast<std::uint32_t>(code[static_cast<std::size_t>(qGreen(p))]) << 12 |
                                       static_cast<std::uint32_t>(code[static_cast<std::size_t>(qBlue(p))]) << 2;
            qToBigEndian(word, out);
            out += 4;
        }
    }
    return b;
}

std::optional<QImage> readCineon(const QByteArray& data) {
    if (data.size() < kHeader || get32(data, 0) != kMagic) return std::nullopt;
    const std::uint32_t offset = get32(data, 4);
    if (data[193] != 3 || data[681] != 5 || data[198] != 10) return std::nullopt;
    const std::uint32_t w = get32(data, 200);
    const std::uint32_t h = get32(data, 204);
    if (w == 0 || h == 0 || w > 32768 || h > 32768) return std::nullopt;
    const qsizetype need = static_cast<qsizetype>(offset) + static_cast<qsizetype>(w) * h * 4;
    if (need > data.size()) return std::nullopt;
    QImage image(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGB32);
    std::array<int, 1024> value{};
    for (int c = 0; c < 1024; ++c) value[static_cast<std::size_t>(c)] = valueOfCode(c);
    const char* in = data.constData() + offset;
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const std::uint32_t word = qFromBigEndian<std::uint32_t>(in);
            in += 4;
            line[x] = qRgb(value[(word >> 22) & 0x3FF], value[(word >> 12) & 0x3FF], value[(word >> 2) & 0x3FF]);
        }
    }
    return image;
}

} // namespace lumenis::media
