#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/ext/provider.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace lumenis::ext {

// Extension point for decoding and encoding compressed essence.
//
// Implementations planned (see docs/EXTENSIONS.md):
//   OpenSource       FFmpeg/libavcodec (LGPL build), bmx for MXF essence
//   OperatingSystem  VideoToolbox (macOS), Media Foundation (Windows), VA-API (Linux)
//   ThirdParty       vendor codec packs, if and when licensed
//
// The frame and packet types are deliberately small; Phase 2 extends them
// (colour metadata, hardware surfaces) without changing the provider shape.

enum class MediaKind { Video, Audio };

// Canonical codec names, lower case: "dnxhd", "dnxhr", "prores", "h264",
// "hevc", "avc-intra", "xdcam-hd422", "pcm_s24le", "aac", ...
struct CodecQuery {
    MediaKind kind = MediaKind::Video;
    std::string codec;
    std::string profile; // e.g. "dnxhr_hq", "prores_422_hq"; empty = any
    int width = 0;
    int height = 0;
    Rational editRate;
};

struct Packet {
    std::span<const std::uint8_t> data;
    std::int64_t pts = 0; // in edit units
    bool keyframe = true;
};

struct FramePlane {
    std::vector<std::uint8_t> data;
    int stride = 0;
};

struct DecodedFrame {
    MediaKind kind = MediaKind::Video;
    std::int64_t pts = 0;
    std::string pixelFormat; // "yuv422p10le", "rgba64le", or a sample format for audio
    int width = 0;
    int height = 0;
    int sampleCount = 0;
    std::vector<FramePlane> planes;
};

enum class CodecStatus { Ok, NeedMoreInput, EndOfStream, Unsupported, Error };

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual CodecStatus send(const Packet& packet) = 0;
    virtual CodecStatus receive(DecodedFrame& frame) = 0;
    virtual CodecStatus flush() = 0;
};

class Encoder {
public:
    virtual ~Encoder() = default;
    virtual CodecStatus send(const DecodedFrame& frame) = 0;
    virtual CodecStatus receive(std::vector<std::uint8_t>& packet, std::int64_t& pts, bool& keyframe) = 0;
    virtual CodecStatus flush() = 0;
};

class CodecProvider : public Provider {
public:
    virtual bool canDecode(const CodecQuery& query) const = 0;
    virtual bool canEncode(const CodecQuery& query) const = 0;
    virtual std::unique_ptr<Decoder> createDecoder(const CodecQuery& query) = 0;
    virtual std::unique_ptr<Encoder> createEncoder(const CodecQuery& query) = 0;
};

} // namespace lumenis::ext
