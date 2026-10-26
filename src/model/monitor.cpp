#include "lumenis/model/monitor.h"

#include "lumenis/core/timecode.h"

#include <algorithm>

// MonitorState (monitor.h): what the Source, Record and pop-up monitors have loaded, the shown
// clip's position and marks, and the tracking displays above them. The Composer
// (src/ui/composer.*) owns and drives them and paints the frames (src/ui/media_frames.*).
namespace lumenis::model {

namespace {

// The timecode format of an item: its edit rate, drop-frame when its Start is (isDropFrame()).
TimecodeFormat formatOf(const BinItem& item) {
    return TimecodeFormat::forRate(item.editRate, isDropFrame(item));
}

} // namespace

QString trackingLabel(TrackingFormat format) {
    // The option names of the table on p.387.
    switch (format) {
    case TrackingFormat::None: return QStringLiteral("None");
    case TrackingFormat::Master: return QStringLiteral("Master");
    case TrackingFormat::Duration: return QStringLiteral("Duration");
    case TrackingFormat::InOut: return QStringLiteral("In/Out");
    case TrackingFormat::Absolute: return QStringLiteral("Absolute");
    case TrackingFormat::Remain: return QStringLiteral("Remain");
    case TrackingFormat::ClipName: return QStringLiteral("Clip Name");
    }
    return {};
}

const QList<TrackingFormat>& trackingFormats() {
    static const QList<TrackingFormat> all{TrackingFormat::Master,   TrackingFormat::Duration, TrackingFormat::InOut,
                                           TrackingFormat::Absolute, TrackingFormat::Remain,   TrackingFormat::ClipName,
                                           TrackingFormat::None};
    return all;
}

void MonitorState::load(const QString& binId, const BinItem& item) {
    for (int i = 0; i < loaded_.size(); ++i) {
        if (loaded_.at(i).item.id == item.id) {
            loaded_[i].item = item; // the bin may have changed it
            loaded_[i].binId = binId;
            current_ = i;
            setPosition(position());
            return;
        }
    }
    LoadedClip c;
    c.binId = binId;
    c.item = item;
    loaded_.append(c);
    current_ = static_cast<int>(loaded_.size()) - 1;
}

// The item may have become shorter, so the position is clamped and marks past the new end dropped.
bool MonitorState::replaceItem(const BinItem& item) {
    for (LoadedClip& c : loaded_) {
        if (c.item.id != item.id) continue;
        c.item = item;
        const std::int64_t last = c.item.kind == BinItemKind::Sequence ? c.item.durationFrames
                                                                        : std::max<std::int64_t>(c.item.durationFrames - 1, 0);
        c.position = std::clamp<std::int64_t>(c.position, 0, std::max<std::int64_t>(last, 0));
        if (c.in && *c.in > last) c.in.reset();
        if (c.out && *c.out > last) c.out.reset();
        return true;
    }
    return false;
}

const LoadedClip* MonitorState::current() const {
    return current_ >= 0 && current_ < loaded_.size() ? &loaded_.at(current_) : nullptr;
}

const LoadedClip* MonitorState::loaded(const QString& itemId) const {
    for (const LoadedClip& c : loaded_) {
        if (c.item.id == itemId) return &c;
    }
    return nullptr;
}

LoadedClip* MonitorState::currentMutable() {
    return current_ >= 0 && current_ < loaded_.size() ? &loaded_[current_] : nullptr;
}

QStringList MonitorState::menuNames(bool loadOrder) const {
    QStringList names;
    for (const LoadedClip& c : loaded_) names << c.item.name;
    if (!loadOrder) {
        std::stable_sort(names.begin(), names.end(),
                         [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    }
    return names;
}

// `index` counts in menuNames(loadOrder): the same case-insensitive stable sort maps it back to
// loaded_.
bool MonitorState::select(int index, bool loadOrder) {
    if (index < 0 || index >= loaded_.size()) return false;
    if (loadOrder) {
        current_ = index;
        return true;
    }
    QList<int> order;
    for (int i = 0; i < loaded_.size(); ++i) order << i;
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        return loaded_.at(a).item.name.compare(loaded_.at(b).item.name, Qt::CaseInsensitive) < 0;
    });
    current_ = order.at(index);
    return true;
}

void MonitorState::clearMonitor() {
    current_ = -1;
}

// With nothing shown, every clip is unloaded.
void MonitorState::clearMenu() {
    const LoadedClip* shown = current();
    if (!shown) {
        loaded_.clear();
        return;
    }
    const LoadedClip keep = *shown;
    loaded_ = {keep};
    current_ = 0;
}

std::int64_t MonitorState::duration() const {
    const LoadedClip* c = current();
    return c ? std::max<std::int64_t>(c->item.durationFrames, 0) : 0;
}

std::int64_t MonitorState::position() const {
    const LoadedClip* c = current();
    return c ? c->position : 0;
}

void MonitorState::setPosition(std::int64_t frame) {
    if (LoadedClip* c = currentMutable()) c->position = std::clamp<std::int64_t>(frame, 0, lastPosition());
}

std::int64_t MonitorState::lastPosition() const {
    const LoadedClip* c = current();
    if (!c) return 0;
    // A sequence's position can sit just past its last frame, where the next
    // edit goes (p.464).
    if (c->item.kind == BinItemKind::Sequence) return duration();
    return std::max<std::int64_t>(duration() - 1, 0);
}

void MonitorState::step(std::int64_t frames) {
    setPosition(position() + frames);
}

void MonitorState::markIn() {
    if (LoadedClip* c = currentMutable()) {
        c->in = c->position;
        // An IN after the OUT clears the OUT.
        if (c->out && *c->out < *c->in) c->out.reset();
    }
}

void MonitorState::markOut() {
    if (LoadedClip* c = currentMutable()) {
        c->out = c->position;
        if (c->in && *c->in > *c->out) c->in.reset();
    }
}

void MonitorState::clearIn() {
    if (LoadedClip* c = currentMutable()) c->in.reset();
}

void MonitorState::clearOut() {
    if (LoadedClip* c = currentMutable()) c->out.reset();
}

void MonitorState::clearBoth() {
    clearIn();
    clearOut();
}

std::optional<std::int64_t> MonitorState::in() const {
    const LoadedClip* c = current();
    return c ? c->in : std::nullopt;
}

std::optional<std::int64_t> MonitorState::out() const {
    const LoadedClip* c = current();
    return c ? c->out : std::nullopt;
}

bool MonitorState::goToIn() {
    if (!in()) return false;
    setPosition(*in());
    return true;
}

bool MonitorState::goToOut() {
    if (!out()) return false;
    setPosition(*out());
    return true;
}

std::int64_t MonitorState::markedStart() const {
    return in().value_or(0);
}

std::int64_t MonitorState::markedEnd() const {
    return out().value_or(std::max<std::int64_t>(duration() - 1, 0));
}

std::int64_t MonitorState::inOutDuration() const {
    if (isEmpty()) return 0;
    return std::max<std::int64_t>(markedEnd() - markedStart() + 1, 0);
}

void MonitorState::setCurrentBin(const QString& binId) {
    if (LoadedClip* c = currentMutable()) c->binId = binId;
}

// Uncommitted clips are linked (a link, no bin). The shown clip stays shown if it survives;
// otherwise the monitor goes black.
int MonitorState::removeUncommitted() {
    const QString shown = current() ? current()->item.id : QString();
    int removed = 0;
    for (int i = static_cast<int>(loaded_.size()) - 1; i >= 0; --i) {
        if (!loaded_.at(i).binId.isEmpty() || loaded_.at(i).item.link.isEmpty()) continue;
        loaded_.removeAt(i);
        ++removed;
    }
    current_ = -1;
    for (int i = 0; i < loaded_.size(); ++i) {
        if (loaded_.at(i).item.id == shown) current_ = i;
    }
    return removed;
}

// The Start column is parsed as timecode at the item's edit rate.
std::int64_t MonitorState::startFrame() const {
    const LoadedClip* c = current();
    if (!c) return 0;
    const QString start = c->item.fields.value(QStringLiteral("Start"));
    if (const auto frames = timecodeFrames(start, c->item.editRate)) return *frames;
    if (c->item.kind == BinItemKind::Sequence) {
        return kDefaultStartHours * 3600 * formatOf(c->item).nominalFps;
    }
    return 0;
}

QString MonitorState::durationText(std::int64_t frames) const {
    const LoadedClip* c = current();
    return c ? model::durationText(frames, c->item.editRate) : QString();
}

QString MonitorState::tracking(TrackingFormat format) const {
    // "There is no display of tracking data when there is no material loaded
    // in the monitor" (p.385).
    const LoadedClip* c = current();
    if (!c) return {};
    auto tc = [&](std::int64_t frames) {
        return QString::fromStdString(Timecode(std::max<std::int64_t>(frames, 0), formatOf(c->item)).toString());
    };
    switch (format) {
    case TrackingFormat::None: return {};
    case TrackingFormat::Master: return tc(startFrame() + position());
    case TrackingFormat::Duration: return tc(duration());
    case TrackingFormat::InOut: return tc(inOutDuration());
    case TrackingFormat::Absolute: return tc(position());
    case TrackingFormat::Remain: return tc(std::max<std::int64_t>(duration() - position() - 1, 0));
    case TrackingFormat::ClipName: return c->item.name;
    }
    return {};
}

} // namespace lumenis::model
