#pragma once

#include "lumenis/ext/provider.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

// Audio CDs on import (9.2d; docs/spec/audio-cd.md; Editing Guide p.215, p.1198; lumenis/ext/audio_cd.h): Windows'
// ".cda" track files, whether a file is on a disc (the Import setting's "CD only"), and the built-in providers that read
// a track off the disc.
namespace lumenis::media {

// A ".cda" file: a RIFF "CDDA" file of 44 bytes naming where its track is on the disc (the track number, its first
// sector and its length in 2352-byte CD-DA sectors of 1/75 s).
struct CdaTrack {
    int track = 0;
    std::uint32_t serial = 0; // the disc's volume serial number
    std::uint32_t startSector = 0;
    std::uint32_t sectors = 0;
    double seconds() const { return static_cast<double>(sectors) / 75.0; }
};
bool isCdaFile(const QString& path);
std::optional<CdaTrack> parseCda(const QByteArray& data);
std::optional<CdaTrack> readCdaFile(const QString& path);
// The bytes of a ".cda" file for `track` (what Windows shows; for tests).
QByteArray cdaFileData(const CdaTrack& track);

// Whether a file is on a CD or DVD (p.1198: "Select the CD only option if you only want to apply attenuation/gain to all
// of the music files on a CD"): a ".cda" file, a file on a volume of a disc's file system (cddafs, ISO 9660, UDF, CDFS),
// or a track gvfs shows from an audio CD ("cdda:" in its path).
bool isOnDisc(const QString& path);

// Writes 16-bit PCM (interleaved little-endian samples) as a WAV file.
bool writePcmWav(const QString& path, const QByteArray& pcm, int sampleRate = 44100, int channels = 2);

// The built-in audio CD providers: on Windows, reading ".cda" tracks through the CD-ROM driver.
void registerAudioCdProviders(ext::ProviderRegistry& registry);

} // namespace lumenis::media
