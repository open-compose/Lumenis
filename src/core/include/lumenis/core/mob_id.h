#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lumenis {

// A MobID: the SMPTE ST 330 basic UMID that names a clip, a sequence or a
// source (a file, a tape) in AAF and MXF, and so in Media Composer's bins
// (docs/spec/native-model.md). 32 bytes:
//   0-11  universal label 06 0a 2b 34 01 01 01 01 01 01 <type> <methods>
//   12    length of what follows, 0x13
//   13-15 instance number (0 for an original)
//   16-31 material number
// The ones made here have material type 0x0f ("not identified", as AAF
// writers use) and methods 0x10: a UUID material number (ST 330 method 1),
// local registration of the instance (method 0).
class MobId {
public:
    // The raw 32 bytes, in the order they are written to AAF and MXF files.
    using Bytes = std::array<std::uint8_t, 32>;

    MobId() = default; // the null MobID: all zeros
    // Wraps bytes read from a file as they are; no check is made that they form a valid UMID.
    explicit MobId(const Bytes& bytes) : bytes_(bytes) {}

    // A new, unique MobID (a random version 4 UUID as the material number).
    static MobId generate();
    // One of ours with a given material number (a UUID, ST 330 method 1).
    static MobId withMaterial(const std::array<std::uint8_t, 16>& material);
    // "urn:smpte:umid:060a2b34.01010101.01010f10.13000000.xxxxxxxx.xxxxxxxx.xxxxxxxx.xxxxxxxx"
    // (bmx and AAF tools print them so); 64 hex digits without the prefix
    // and dots are taken too. Case does not matter.
    static std::optional<MobId> fromString(std::string_view text);

    // The lower-case "urn:smpte:umid:" form above, with a dot after every four bytes.
    std::string toString() const;
    // True for the all-zero MobID, which stands for "no MobID" (for example, a segment with no source).
    bool isNull() const;
    const Bytes& bytes() const { return bytes_; }

    // Byte-wise comparison, so MobIDs can be map keys.
    friend bool operator==(const MobId&, const MobId&) = default;
    friend auto operator<=>(const MobId&, const MobId&) = default;

private:
    Bytes bytes_{};
};

} // namespace lumenis
