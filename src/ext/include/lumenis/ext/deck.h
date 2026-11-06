// The extension point behind the Digital Cut tool's decks (Phase 7.3a; docs/spec/digital-cut.md; Editing Guide
// p.926-938): a record deck the application cues and records onto, frame by frame, at tape timecode. The built-in
// provider keeps a tape in a folder (media::FolderTapeDeckProvider); a deck under RS-422 control or one behind AJA or
// Blackmagic hardware comes from a module. Header-only, standard types only (docs/EXTENSIONS.md).
#pragma once

#include "lumenis/ext/provider.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lumenis::ext {

struct DeckInfo {
    std::string id;
    std::string name;
    // "If a deck name appears in italics on the menu, the deck has lost power or has been disconnected" (p.928).
    bool online = true;
    bool insertEdit = true; // takes insert edits; "If your deck does not support insert editing, the Crash Record
                            // option appears automatically" (p.930)
};

// "Record by using either assemble edit, insert edit, or crash record" (p.926).
enum class DeckRecordMode { Insert, Assemble, Crash };

// What a recording carries: the raster and rate of the frames, the sound's channels at 48 kHz, and which of the tape's
// tracks it records onto (the Enable Track buttons, p.930).
struct DeckRecording {
    DeckRecordMode mode = DeckRecordMode::Insert;
    std::int64_t in = -1;     // tape timecode in frames (timecode frames at the rate); -1: where the deck is cued
    int width = 0;
    int height = 0;
    int rateNum = 25;
    int rateDen = 1;
    int channels = 2;
    bool video = true;        // record onto the tape's video track
    std::vector<int> audio{1, 2}; // the tape's audio tracks recorded onto, from 1
};

// An open deck. Not thread-safe; calls come from the user interface thread.
class DeckSession {
public:
    virtual ~DeckSession() = default;
    // The tape's position, in timecode frames.
    virtual std::int64_t position() const = 0;
    virtual bool cue(std::int64_t frame) = 0;
    // Starts recording; then each frame (RGBA, 8 bits a channel, `stride` bytes a row) with its sound (`samples`
    // interleaved frames of the recording's channels); then finish(). False with the reason in `error`.
    virtual bool beginRecord(const DeckRecording& recording, std::string* error) = 0;
    virtual bool writeFrame(const unsigned char* rgba, int stride, const float* sound, int samples, std::string* error) = 0;
    virtual bool finish(std::string* error) = 0;
    // Halt Digital Cut: what was recorded so far stays (p.930).
    virtual void halt() = 0;
};

class DeckProvider : public Provider {
public:
    // "The Deck Selection menu in the Digital Cut tool contains a list of all decks that were connected to the system,
    // turned on, and initialized when you opened the Digital Cut tool" (p.927). Check Decks lists them again.
    virtual std::vector<DeckInfo> decks() const = 0;
    virtual std::unique_ptr<DeckSession> open(const std::string& id, std::string* error) = 0;
};

} // namespace lumenis::ext
