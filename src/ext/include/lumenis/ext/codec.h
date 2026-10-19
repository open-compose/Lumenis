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

// Whether a query, frame or codec is about pictures or sound.
enum class MediaKind { Video, Audio };

// Canonical codec names, lower case: "dnxhd", "dnxhr", "prores", "h264",
// "hevc", "avc-intra", "xdcam-hd422", "pcm_s24le", "aac", ...
// A CodecQuery says what the caller wants decoded or encoded; width, height and editRate may be
// left at 0 when they are not known.
struct CodecQuery {
    MediaKind kind = MediaKind::Video;
    std::string codec;
    std::string profile; // e.g. "dnxhr_hq", "prores_422_hq"; empty = any
    int width = 0;
    int height = 0;
    Rational editRate;
};

// One compressed access unit handed to a Decoder. data is borrowed: it need only stay valid for
// the send() call.
struct Packet {
    std::span<const std::uint8_t> data;
    std::int64_t pts = 0; // in edit units
    bool keyframe = true;
};

// One plane of a decoded picture (Y, Cb, Cr, ...) or one channel of planar audio; stride is the
// bytes per picture row.
struct FramePlane {
    std::vector<std::uint8_t> data;
    int stride = 0;
};

// An uncompressed picture or block of audio samples. Video uses width, height and pixelFormat;
// audio uses sampleCount and a sample format in pixelFormat. pts is in edit units, as in Packet.
struct DecodedFrame {
    MediaKind kind = MediaKind::Video;
    std::int64_t pts = 0;
    std::string pixelFormat; // "yuv422p10le", "rgba64le", or a sample format for audio
    int width = 0;
    int height = 0;
    int sampleCount = 0;
    std::vector<FramePlane> planes;
};

// Result of a codec call: Ok, NeedMoreInput (receive() has nothing until more is sent),
// EndOfStream (flushed and drained), Unsupported (the stream or option cannot be handled) or Error.
enum class CodecStatus { Ok, NeedMoreInput, EndOfStream, Unsupported, Error };

// A send/receive decoder: send() packets, then call receive() until it returns NeedMoreInput;
// flush() at the end, then receive() until EndOfStream.
class Decoder {
public:
    virtual ~Decoder() = default;
    virtual CodecStatus send(const Packet& packet) = 0;
    virtual CodecStatus receive(DecodedFrame& frame) = 0;
    virtual CodecStatus flush() = 0;
};

// The encoding counterpart of Decoder: send() frames and receive() packets, with their pts and
// keyframe flag.
class Encoder {
public:
    virtual ~Encoder() = default;
    virtual CodecStatus send(const DecodedFrame& frame) = 0;
    virtual CodecStatus receive(std::vector<std::uint8_t>& packet, std::int64_t& pts, bool& keyframe) = 0;
    virtual CodecStatus flush() = 0;
};

// A provider of decoders and encoders (Category::VideoCodec or AudioCodec). canDecode() and
// canEncode() answer cheaply without opening anything; the create functions return null when the
// query cannot be served. src/media/media.cpp registers the FFmpeg provider.
class CodecProvider : public Provider {
public:
    virtual bool canDecode(const CodecQuery& query) const = 0;
    virtual bool canEncode(const CodecQuery& query) const = 0;
    virtual std::unique_ptr<Decoder> createDecoder(const CodecQuery& query) = 0;
    virtual std::unique_ptr<Encoder> createEncoder(const CodecQuery& query) = 0;
};

} // namespace lumenis::ext
