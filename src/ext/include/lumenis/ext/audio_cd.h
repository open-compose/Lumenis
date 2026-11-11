// The extension point behind importing tracks of audio CDs (9.2d; docs/spec/audio-cd.md; Editing Guide p.215, p.1198):
// "you import audio from a CD". macOS shows an audio CD's tracks as AIFF files and Linux desktops (gvfs) as WAV files,
// which import as any sound file; Windows shows them as ".cda" files that hold no sound, only where the track is on the
// disc. A provider reads such a track off the disc as sound. The built-in one on Windows reads the disc through the
// system's CD-ROM driver (media::WindowsAudioCdProvider); other ways (a CD drive on a network, a disc image) can come
// from modules. Header-only, standard types only (docs/EXTENSIONS.md).
#pragma once

#include "lumenis/ext/provider.h"

#include <functional>
#include <string>

namespace lumenis::ext {

class AudioCdProvider : public Provider {
public:
    // Whether `path` (a file the user chose to import) is an audio CD track this provider reads.
    virtual bool readsTrack(const std::string& path) const = 0;
    // Reads the track as 44.1 kHz, 16-bit stereo PCM into a WAV file at `wavPath`. `progress` gets the fraction done and
    // returns false to cancel. False with the reason in `error`.
    virtual bool readTrack(const std::string& path, const std::string& wavPath, const std::function<bool(double)>& progress,
                           std::string* error) = 0;
};

} // namespace lumenis::ext
