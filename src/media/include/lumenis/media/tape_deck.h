// The built-in record deck (Phase 7.3a; docs/spec/digital-cut.md): a "tape" kept in a folder, for laying a digital cut
// onto files and for testing the Digital Cut tool without a deck. The folder's tape.json lists the recordings in the
// order they were made, each with its tape timecode IN, length, tracks and its file (rec_<n>.mov: PNG pictures and
// 24-bit PCM sound). A later recording covers what it overlaps, as an insert edit does; an assemble edit also blanks the
// tape after its OUT.
#pragma once

#include "lumenis/ext/deck.h"

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace lumenis::media {

// The folders to offer as decks: set here or in LUMENIS_TAPE_DECKS (separated like PATH). A folder that does not exist
// is listed offline ("If a deck name appears in italics ...", p.928).
void setTapeDeckFolders(const QStringList& folders);
QStringList tapeDeckFolders();

struct TapeRecording {
    std::int64_t in = 0;     // tape timecode frames
    std::int64_t length = 0;
    QString file;            // relative to the tape folder
    bool video = true;
    QList<int> audio;        // tape audio tracks, from 1
    ext::DeckRecordMode mode = ext::DeckRecordMode::Insert;
};

// The tape's recordings, in the order made; empty for an empty or unreadable tape.
QList<TapeRecording> tapeRecordings(const QString& folder);
// The picture on the tape at `frame`: the last recording with video covering it; null where the tape is blank.
QImage tapeFrameAt(const QString& folder, std::int64_t frame);
// The tape's position when it was last stopped.
std::int64_t tapePosition(const QString& folder);

class FolderTapeDeckProvider : public ext::DeckProvider {
public:
    ext::ProviderInfo info() const override;
    std::vector<ext::DeckInfo> decks() const override;
    std::unique_ptr<ext::DeckSession> open(const std::string& id, std::string* error) override;
};

} // namespace lumenis::media
