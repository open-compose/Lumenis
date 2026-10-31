#pragma once

#include "lumenis/model/bin.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace lumenis::model {

// What a tracking display above a monitor shows (guide p.385-387).
enum class TrackingFormat {
    None,     // "By default, the tracking information area displays no data"
    Master,   // master timecode at the present location
    Duration, // total duration
    InOut,    // duration between IN and OUT
    Absolute, // absolute time at the present position
    Remain,   // time remaining from the present position
    ClipName, // the clip's name
};

// The menu text of a tracking format, and every format in menu order (None last).
QString trackingLabel(TrackingFormat format);
const QList<TrackingFormat>& trackingFormats();

// A clip or sequence loaded into a monitor, with its marks and position.
struct LoadedClip {
    // The bin the item came from; empty for a clip from the Source Browser that is in no bin yet.
    QString binId;
    // A copy of the bin item, kept up to date with replaceItem().
    BinItem item;
    // Position and marks in frames from the item's first frame (0-based). The marks are inclusive
    // frames; nullopt when not set.
    std::int64_t position = 0;
    std::optional<std::int64_t> in;
    std::optional<std::int64_t> out;
};

// The state of one monitor (Source, Record or pop-up): what is loaded, what
// is shown, the position and the marks (guide p.393-407).
// Pure state with no Qt widgets; the Composer (src/ui/composer.*) owns one per monitor and draws
// it. Functions that act on "the clip" act on the shown clip and do nothing when none is shown.
class MonitorState {
public:
    // Loading a clip already in the menu shows it again, marks and all.
    // Loaded clips are matched by BinItem::id. A new clip starts at frame 0 with no marks.
    void load(const QString& binId, const BinItem& item);
    // The bin changed a loaded item: it is updated wherever it is in the
    // menu, keeping its marks. Returns false if it is not loaded.
    bool replaceItem(const BinItem& item);
    bool isEmpty() const { return current() == nullptr; }
    // The shown clip was put into a bin (a clip from the Source Browser,
    // committed when it is edited into a sequence, p.316).
    void setCurrentBin(const QString& binId);
    // Removes linked clips that are in no bin (loaded from the Source
    // Browser); returns how many (p.315, "Clear Source Monitor Upon
    // Closing Source Browser").
    int removeUncommitted();
    // Takes the item out of the menu (a nest stepped out of, lumenis/ui/timeline.h). The shown clip stays
    // shown unless it is the one taken out.
    bool remove(const QString& itemId);
    // The shown clip, or null when the monitor is empty (cleared or nothing loaded).
    const LoadedClip* current() const;

    // The Clip Name menu (p.394): every loaded clip, alphabetically, or in
    // the order loaded (Alt held).
    QStringList menuNames(bool loadOrder = false) const;
    // Shows the clip at `index` in menuNames(loadOrder).
    bool select(int index, bool loadOrder = false);
    // Clear Monitor: black, the clips stay loaded. Clear Menu: only the shown
    // clip stays (p.395).
    void clearMonitor();
    void clearMenu();
    int loadedCount() const { return static_cast<int>(loaded_.size()); }
    // The loaded clip with BinItem::id `itemId`, shown or not (with its marks); null if not loaded.
    const LoadedClip* loaded(const QString& itemId) const;

    // Position, 0 to lastPosition().
    std::int64_t duration() const;
    std::int64_t position() const;
    // duration - 1 for clips; for sequences the end, after the last frame.
    std::int64_t lastPosition() const;
    // Clamped to 0..lastPosition().
    void setPosition(std::int64_t frame);
    void step(std::int64_t frames);
    void goToStart() { setPosition(0); }
    void goToEnd() { setPosition(lastPosition()); }

    // Marks (p.405-407).
    // Marks the position. A new IN after the OUT clears the OUT; a new OUT before the IN clears the IN.
    void markIn();
    void markOut();
    void clearIn();
    void clearOut();
    void clearBoth();
    std::optional<std::int64_t> in() const;
    std::optional<std::int64_t> out() const;
    bool goToIn();
    bool goToOut();
    // Marked material, or the whole clip where a mark is missing.
    std::int64_t markedStart() const;
    std::int64_t markedEnd() const; // inclusive
    std::int64_t inOutDuration() const;

    // The clip's starting timecode in frames (its Start column, or the
    // sequence default) and what a tracking display shows.
    std::int64_t startFrame() const;
    // tracking() is empty when nothing is shown. Timecodes use the item's edit rate, drop-frame
    // when the item's Start timecode is (model::isDropFrame()).
    QString tracking(TrackingFormat format) const;
    // As bins show durations (p.246-255): "0:00", "1:11", "4:24:14".
    QString durationText(std::int64_t frames) const;

private:
    LoadedClip* currentMutable();

    QList<LoadedClip> loaded_; // in load order
    // Index into loaded_.
    int current_ = -1;         // -1: nothing shown
};

} // namespace lumenis::model
