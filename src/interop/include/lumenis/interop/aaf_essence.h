#pragma once

#include "lumenis/core/mob_id.h"
#include "lumenis/core/rational.h"
#include "lumenis/interop/aaf.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <cstdint>
#include <optional>

// Media embedded in AAF files (docs/spec/aaf.md, "Embedded media"; Editing Guide p.871, p.1176, p.1197): the
// ContentStorage's EssenceData objects, each the media of the file source mob with its MobID, in a stream of the
// compound file. Import reads them out (aaf_import.cpp lists them in ImportResult::essence; the application makes
// managed media of them, src/ui/main_window_embedded.cpp); export writes them from files (ExportOptions::embed,
// aaf_export.cpp). Ours handles what Media Composer embeds: sound as PCM, WAVE or AIFC, and pictures in Avid DNx.
namespace lumenis::interop::aaf {

// The header of an Avid DNx (VC-3) frame: its compression ID, picture size, bit depth and scan (SMPTE ST 2019-1's
// frame header; the same fields pyaaf2's video.py reads).
struct DnxHeader {
    int cid = 0;
    int width = 0;
    int height = 0; // the frame's lines, both fields of an interlaced one
    int bitDepth = 8;
    bool interlaced = false;
};
// The header of the frame starting `frame` (at least 640 bytes); nothing when it is not a DNx frame.
std::optional<DnxHeader> readDnxHeader(const QByteArray& frame);
// The SMPTE compression label of a DNx compression ID (CDCIDescriptor's Compression); null when unknown.
QUuid dnxCompression(int cid);

// One file source mob's embedded media.
struct Essence {
    enum class Kind {
        Unsupported, // a descriptor ours does not read (named in `descriptor`)
        Pcm,         // PCMDescriptor or SoundDescriptor: interleaved little-endian samples
        Wave,        // WAVEDescriptor: a WAVE file
        Aifc,        // AIFCDescriptor: an AIFF-C file
        Dnx,         // CDCIDescriptor compressed in Avid DNx: the frames one after another
    };
    Kind kind = Kind::Unsupported;
    MobId mob;          // the file source mob, the EssenceData's MobID
    QString descriptor; // the descriptor's class name
    // The descriptor's SampleRate and Length: the media's edit units and how many.
    Rational rate{25};
    std::int64_t length = 0;
    // Sound: channels, samples per second, bits per sample.
    int channels = 0;
    int sampleRate = 0;
    int bits = 0;
    // Picture: the first frame's DNx header.
    DnxHeader dnx;
    // Where the bytes are: a stream of the open file (import), or a file (export: raw PCM or DNx frames).
    int stream = -1;
    QString file;
    std::uint64_t bytes = 0;
    // The WAVE or AIFC header kept in the descriptor's Summary (import), for data that starts without one.
    QByteArray summary;
};

// Every EssenceData of the file, with what its mob's descriptor says. Damaged ones (no mob, no stream) are left
// out, with a line each in `warnings`.
QList<Essence> embeddedEssence(const File& file, QStringList* warnings = nullptr);

// Writes an import's essence to `path` as a file media readers open: sound as a WAVE file (PCM gets a WAVE header;
// WAVE and AIFC data as they are, their header first from the Summary when the data has none), DNx as a raw VC-3
// stream. Read and written a megabyte at a time. False, with `error` set, when the stream cannot be read or the
// file written.
bool extractEssence(const File& file, const Essence& essence, const QString& path, QString* error = nullptr);

// The file suffix extractEssence() writes: "wav", "aif" or "dnxhd"; empty for Unsupported.
QString essenceSuffix(const Essence& essence);

} // namespace lumenis::interop::aaf
