#include "lumenis/core/mob_id.h"

#include <mutex>
#include <random>

// MobId: generating, parsing and printing SMPTE ST 330 UMIDs. The model (src/model/bin.*, sequence.*)
// stores them on bin items and segments, managed media (src/media/managed.*) writes them as MXF
// package UIDs, and AAF import and export (src/interop/aaf_*) map them to AAF MobIDs. See
// docs/spec/native-model.md for how they tie clips, sources and media files together.
namespace lumenis {

namespace {

// Bytes 0-11 of every MobID made here: the UMID universal label with material type 0x0f and
// methods 0x10 (see mob_id.h).
constexpr std::array<std::uint8_t, 12> kLabel{0x06, 0x0a, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x0f, 0x10};

// The value of one hex digit, or -1 if c is not one.
int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

MobId MobId::generate() {
    // One generator for the process, seeded once from the system.
    static std::mutex lock;
    static std::mt19937_64 random{std::random_device{}()};
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    {
        const std::lock_guard<std::mutex> guard(lock);
        high = random();
        low = random();
    }
    std::array<std::uint8_t, 16> material{};
    for (int i = 0; i < 8; ++i) {
        material[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(high >> (56 - 8 * i));
        material[8 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(low >> (56 - 8 * i));
    }
    // RFC 4122 version 4 and variant bits in the UUID.
    material[6] = static_cast<std::uint8_t>((material[6] & 0x0f) | 0x40);
    material[8] = static_cast<std::uint8_t>((material[8] & 0x3f) | 0x80);
    return withMaterial(material);
}

MobId MobId::withMaterial(const std::array<std::uint8_t, 16>& material) {
    Bytes b{};
    for (std::size_t i = 0; i < kLabel.size(); ++i) b[i] = kLabel[i];
    b[12] = 0x13;
    // 13-15: instance number 0.
    for (std::size_t i = 0; i < material.size(); ++i) b[16 + i] = material[i];
    return MobId(b);
}

// Strips an optional "urn:smpte:umid:" prefix (any case), then reads exactly 64 hex digits; dots
// anywhere are ignored. Anything else, too few or too many digits gives nullopt.
std::optional<MobId> MobId::fromString(std::string_view text) {
    constexpr std::string_view kPrefix = "urn:smpte:umid:";
    if (text.size() >= kPrefix.size()) {
        bool prefixed = true;
        for (std::size_t i = 0; i < kPrefix.size(); ++i) {
            const char c = text[i];
            if ((c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c) != kPrefix[i]) {
                prefixed = false;
                break;
            }
        }
        if (prefixed) text.remove_prefix(kPrefix.size());
    }
    Bytes b{};
    std::size_t n = 0;
    int high = -1;
    for (const char c : text) {
        if (c == '.') continue;
        const int v = hexValue(c);
        if (v < 0 || n >= b.size()) return std::nullopt;
        if (high < 0) {
            high = v;
        } else {
            b[n++] = static_cast<std::uint8_t>(high << 4 | v);
            high = -1;
        }
    }
    if (n != b.size() || high >= 0) return std::nullopt;
    return MobId(b);
}

std::string MobId::toString() const {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string s = "urn:smpte:umid:";
    s.reserve(s.size() + 64 + 7);
    for (std::size_t i = 0; i < bytes_.size(); ++i) {
        if (i > 0 && i % 4 == 0) s += '.';
        s += kDigits[bytes_[i] >> 4];
        s += kDigits[bytes_[i] & 0x0f];
    }
    return s;
}

bool MobId::isNull() const {
    for (const std::uint8_t v : bytes_) {
        if (v != 0) return false;
    }
    return true;
}

} // namespace lumenis
