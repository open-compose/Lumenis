#include "lumenis/model/automation.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/timeline.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// Volume and pan automation in the Timeline (Editing Guide p.714-723; docs/spec/automation.md):
// the Audio Data graphs TimelineView draws over audio segments, the keyframe hit test, and
// Timeline's keyframe editing, which the Audio Mixer's Auto mode (audio_tools.cpp) also uses.
// Every edit follows the Timeline's pattern (timeline.cpp): read the Record monitor's sequence,
// change it with a function from lumenis/model/automation.h, commit it as one undo step.
namespace lumenis::ui {

using model::AutomationParam;
using model::KeyRef;

namespace {

constexpr QRgb kGraph = 0xffe0e0e0;      // the automation line
constexpr QRgb kClipLine = 0xff9aa0ff;   // the clip gain's line
constexpr QRgb kKeyframe = 0xffffffff;
constexpr QRgb kKeySelected = 0xffe87fb8; // "The keyframe highlights pink" (p.721)
constexpr QRgb kHiddenData = 0xffe87fb8;  // the pink triangle (p.715)
constexpr int kKeyHit = 5;                // pixels around a keyframe that grab it

// The lines of the scale a Ctrl-drag snaps to (p.716): the fader's marks (picture on p.693), or
// the pan's L100, L50, MID, R50, R100 (ours).
QList<double> scaleLines(AutomationParam param) {
    if (param == AutomationParam::Pan) return {-100, -50, 0, 50, 100};
    return {12, 9, 6, 3, 0, -3, -7, -11, -15, -30, -45};
}

// The Record monitor's sequence; empty without one.
model::Sequence recordSeq(const Composer& composer) {
    const model::LoadedClip* rec = composer.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return {};
    return model::Sequence::fromItem(rec->item);
}

// The value of the keyframe a ref names, if it is there.
std::optional<double> keyValue(const model::Sequence& seq, const KeyRef& ref, AutomationParam param) {
    const model::Track* t = seq.track(ref.track);
    if (!t) return std::nullopt;
    std::int64_t at = 0;
    for (const model::Segment& s : t->segments) {
        const std::int64_t start = at;
        at += s.length;
        if (s.isFiller()) continue;
        for (const model::AudioKey& k : model::keysOf(s, param)) {
            const double record = static_cast<double>(start) + k.frame - static_cast<double>(s.sourceIn);
            if (std::abs(record - ref.frame) < 1e-6) return k.value;
        }
    }
    return std::nullopt;
}

bool isAudio(const QString& track) {
    const auto ref = model::parseTrackName(track);
    return ref && ref->kind == model::TrackKind::Audio;
}

} // namespace

// ---------------------------------------------------------------- TimelineView: the graphs

double TimelineView::yForValue(const Row& row, AutomationParam param, double value) const {
    const double top = row.top + 3;
    const double bottom = row.top + row.height - 4;
    const double frac = param == AutomationParam::Volume
                            ? static_cast<double>(fader::position(value)) / fader::kRange
                            : (model::kPanMax - std::clamp(value, -double(model::kPanMax), double(model::kPanMax))) /
                                  (2.0 * model::kPanMax);
    return bottom - frac * (bottom - top);
}

double TimelineView::valueForY(const Row& row, AutomationParam param, double y) const {
    const double top = row.top + 3;
    const double bottom = row.top + row.height - 4;
    const double frac = bottom > top ? std::clamp((bottom - y) / (bottom - top), 0.0, 1.0) : 0.0;
    if (param == AutomationParam::Volume) return fader::gain(static_cast<int>(std::lround(frac * fader::kRange)));
    return std::round(model::kPanMax - frac * 2.0 * model::kPanMax);
}

std::optional<KeyRef> TimelineView::keyframeAt(const QPoint& pos) const {
    const auto param = timeline_.display().automation;
    if (!param || timeline_.showsSource() || pos.x() < headerWidth()) return std::nullopt;
    const auto row = rowAt(pos);
    if (!row || !row->exists || !isAudio(row->track)) return std::nullopt;
    const model::Track* t = timeline_.shown().track(row->track);
    if (!t) return std::nullopt;
    std::optional<KeyRef> best;
    double distance = kKeyHit + 1;
    std::int64_t at = 0;
    for (const model::Segment& s : t->segments) {
        const std::int64_t start = at;
        at += s.length;
        if (s.isFiller()) continue;
        for (const model::AudioKey& k : model::keysOf(s, *param)) {
            if (k.frame < static_cast<double>(s.sourceIn) || k.frame >= static_cast<double>(s.sourceIn + s.length)) continue;
            const double record = static_cast<double>(start) + k.frame - static_cast<double>(s.sourceIn);
            const double x = xFor(start) + (k.frame - static_cast<double>(s.sourceIn)) * timeline_.scale();
            const double y = yForValue(*row, *param, k.value);
            const double d = std::max(std::abs(x - pos.x()), std::abs(y - pos.y()));
            if (d <= kKeyHit && d < distance) {
                distance = d;
                best = KeyRef{row->track, record};
            }
        }
    }
    return best;
}

// Per segment: the clip gain's line (Clip Volume), the shown graph from the segment's first
// frame to its last with a square at each keyframe on its material, or, with automation not
// shown, the pink triangle at the segment's top left (p.715).
void TimelineView::paintAudioData(QPainter& p, const Row& r, const model::Track& t) const {
    const Timeline::Display& d = timeline_.display();
    const double scale = timeline_.scale();
    const int side = model::sidePan(t.number, timeline_.alternatingPan());
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    std::int64_t at = 0;
    for (const model::Segment& s : t.segments) {
        const std::int64_t start = at;
        at += s.length;
        if (s.isFiller()) continue;
        const double x0 = xFor(start);
        const double x1 = xFor(start + s.length);
        if (x1 < headerWidth() || x0 > width()) continue;
        if (d.clipVolume) {
            p.setPen(QPen(QColor(kClipLine), 1, Qt::DashLine));
            const double y = yForValue(r, AutomationParam::Volume, s.gain);
            p.drawLine(QPointF(x0, y), QPointF(x1, y));
        }
        if (!d.automation) {
            if (s.hasAutomation()) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(kHiddenData));
                p.drawPolygon(QPolygonF{QPointF(x0 + 1, r.top + 2), QPointF(x0 + 8, r.top + 2), QPointF(x0 + 1, r.top + 9)});
            }
            continue;
        }
        const AutomationParam param = *d.automation;
        auto xOf = [&](double sourceFrame) { return x0 + (sourceFrame - static_cast<double>(s.sourceIn)) * scale; };
        auto yOf = [&](double sourceFrame) {
            return yForValue(r, param, model::automationValue(s, param, sourceFrame, side));
        };
        const double first = static_cast<double>(s.sourceIn);
        const double last = static_cast<double>(s.sourceIn + s.length);
        QPolygonF line;
        line << QPointF(x0, yOf(first));
        for (const model::AudioKey& k : model::keysOf(s, param)) {
            if (k.frame > first && k.frame < last) line << QPointF(xOf(k.frame), yOf(k.frame));
        }
        line << QPointF(x1, yOf(last));
        p.setPen(QPen(QColor(kGraph), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(line);
        for (const model::AudioKey& k : model::keysOf(s, param)) {
            if (k.frame < first || k.frame >= last) continue;
            const KeyRef ref{r.track, static_cast<double>(start) + k.frame - first};
            const bool selected = timeline_.keySelection().contains(ref);
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(QColor(selected ? kKeySelected : kKeyframe));
            p.drawRect(QRectF(xOf(k.frame) - 2.5, yForValue(r, param, k.value) - 2.5, 5, 5));
        }
    }
    p.restore();
}

// ---------------------------------------------------------------- Timeline: keyframe editing

AutomationParam Timeline::automationParam() const {
    return display_.automation.value_or(AutomationParam::Volume);
}

void Timeline::setAudioData(bool clipVolume, std::optional<AutomationParam> automation) {
    if (display_.automation != automation) keySelection_.clear();
    display_.clipVolume = clipVolume;
    display_.automation = automation;
    syncScrollBars();
    view_->update();
}

QStringList Timeline::selectedAudioTracks(const model::Sequence& seq) const {
    QStringList out;
    for (const QString& t : patch_.selectedRecordTracks(seq)) {
        if (isAudio(t)) out << t;
    }
    return out;
}

std::pair<double, double> Timeline::markedRange(const model::Sequence& seq) const {
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    std::int64_t end = 0;
    for (const model::Track& t : seq.tracks()) end = std::max(end, t.length());
    const double from = static_cast<double>(record.in().value_or(0));
    const double to = record.out() ? static_cast<double>(*record.out() + 1) : static_cast<double>(end);
    return {from, to};
}

bool Timeline::addKeyframe() {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    const QStringList tracks = selectedAudioTracks(seq);
    if (tracks.isEmpty()) return fail(tr("Select the audio tracks for the keyframe."));
    const double at = static_cast<double>(composer_.state(MonitorId::Record).position());
    if (model::addKeyframes(seq, tracks, at, automationParam(), alternatingPan_) == 0) {
        return fail(tr("There is no audio on the selected tracks at the position, or it has a keyframe already."));
    }
    if (!display_.automation) setAudioData(display_.clipVolume, AutomationParam::Volume);
    return commitSequence(tr("Add Keyframe"), seq);
}

bool Timeline::addKeyframeAt(const QString& track, double frame) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    if (model::addKeyframes(seq, {track}, frame, automationParam(), alternatingPan_) == 0) {
        return fail(tr("There is no audio there, or it has a keyframe already."));
    }
    if (!display_.automation) setAudioData(display_.clipVolume, AutomationParam::Volume);
    return commitSequence(tr("Add Keyframe"), seq);
}

void Timeline::setKeySelection(const QList<KeyRef>& keys) {
    keySelection_ = keys;
    view_->update();
}

void Timeline::clickKeyframe(const KeyRef& key, bool toggle) {
    if (!toggle) {
        setKeySelection({key});
        return;
    }
    // Shift-click on a selected keyframe deselects it (p.722).
    if (keySelection_.contains(key)) {
        keySelection_.removeAll(key);
    } else {
        keySelection_ << key;
    }
    view_->update();
}

QList<KeyRef> Timeline::keyframesMovedWith(const KeyRef& grabbed) const {
    // A selection of several keyframes moves together (p.723); one clicked keyframe moves as an
    // unselected one does.
    if (keySelection_.contains(grabbed) && keySelection_.size() > 1) return keySelection_;
    const model::Sequence seq = recordSeq(composer_);
    QStringList tracks = selectedAudioTracks(seq);
    if (!tracks.contains(grabbed.track)) tracks.prepend(grabbed.track);
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    if (record.in() || record.out()) {
        const auto [from, to] = markedRange(seq);
        if (grabbed.frame >= from && grabbed.frame < to) {
            return model::keyframesIn(seq, tracks, from, to, automationParam());
        }
    }
    // The same place on the other enabled tracks (p.716).
    QList<KeyRef> out{grabbed};
    for (const KeyRef& k : model::keyframesIn(seq, tracks, grabbed.frame, grabbed.frame + 1e-3, automationParam())) {
        if (!out.contains(k)) out << k;
    }
    return out;
}

bool Timeline::dragKeyframes(const KeyRef& grabbed, double frames, double by, bool snap) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    const AutomationParam param = automationParam();
    const QList<KeyRef> keys = keyframesMovedWith(grabbed);
    if (snap) {
        if (const auto value = keyValue(seq, grabbed, param)) {
            const double target = *value + by;
            const QList<double> lines = scaleLines(param);
            const double line = *std::min_element(lines.begin(), lines.end(), [target](double a, double b) {
                return std::abs(a - target) < std::abs(b - target);
            });
            by = line - *value;
        }
    }
    if (model::moveKeyframes(seq, keys, param, frames, by) == 0) return fail(tr("There is no keyframe there to move."));
    QList<KeyRef> moved;
    for (const KeyRef& k : keys) moved << KeyRef{k.track, k.frame + frames};
    keySelection_ = moved;
    return commitSequence(tr("Move Keyframes"), seq);
}

bool Timeline::deleteKeyframeAt(const KeyRef& key) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    // Identical keyframes on the other selected tracks go too (p.717).
    QList<KeyRef> keys{key};
    for (const KeyRef& k : model::keyframesIn(seq, selectedAudioTracks(seq), key.frame, key.frame + 1e-3, automationParam())) {
        if (!keys.contains(k)) keys << k;
    }
    if (model::deleteKeyframes(seq, keys, automationParam()) == 0) return fail(tr("There is no keyframe there."));
    for (const KeyRef& k : keys) keySelection_.removeAll(k);
    return commitSequence(tr("Delete Keyframe"), seq);
}

bool Timeline::nudgeKeyframes(double frames, double by) {
    message_.clear();
    if (keySelection_.isEmpty()) return fail(tr("Select the keyframes to nudge."));
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    if (model::moveKeyframes(seq, keySelection_, automationParam(), frames, by) == 0) {
        return fail(tr("The selected keyframes are gone."));
    }
    for (KeyRef& k : keySelection_) k.frame += frames;
    return commitSequence(tr("Nudge Keyframes"), seq);
}

bool Timeline::editAutomation(AutomationEdit edit, std::optional<AutomationParam> param, double by, bool peaksOnly) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    const QStringList tracks = selectedAudioTracks(seq);
    if (tracks.isEmpty()) return fail(tr("Select the audio tracks first."));
    const auto [from, to] = markedRange(seq);
    int changed = 0;
    QList<AutomationParam> params;
    if (param) {
        params << *param;
    } else {
        params << AutomationParam::Volume << AutomationParam::Pan;
    }
    QString description;
    for (AutomationParam p : params) {
        switch (edit) {
        case AutomationEdit::Filter:
            changed += model::filterAutomation(seq, tracks, from, to, p, peaksOnly);
            description = tr("Filter Automation");
            break;
        case AutomationEdit::Adjust:
            changed += model::adjustAutomation(seq, tracks, from, to, p, by, alternatingPan_);
            description = tr("Adjust Automation");
            break;
        case AutomationEdit::Remove:
            changed += model::removeAutomation(seq, tracks, from, to, p);
            description = tr("Remove Automation");
            break;
        case AutomationEdit::RemoveHidden:
            changed += model::removeHiddenKeyframes(seq, tracks, from, to, p);
            description = tr("Remove Hidden Keyframes");
            break;
        }
    }
    if (changed == 0) return fail(tr("There is no automation there to change."));
    keySelection_.clear();
    return commitSequence(description, seq);
}

std::optional<double> Timeline::automationAt(const QString& track, AutomationParam param) const {
    const model::Sequence seq = recordSeq(composer_);
    const model::Track* t = seq.track(track);
    if (!t) return std::nullopt;
    const std::int64_t position = composer_.state(MonitorId::Record).position();
    std::int64_t start = 0;
    const int i = t->segmentAt(position, &start);
    if (i < 0 || t->segments.at(i).isFiller()) return std::nullopt;
    const model::Segment& s = t->segments.at(i);
    return model::automationValue(s, param, static_cast<double>(s.sourceIn + (position - start)),
                                  model::sidePan(t->number, alternatingPan_));
}

bool Timeline::setAutomationValue(const QString& track, AutomationParam param, double value) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    const model::Track* t = seq.track(track);
    const std::int64_t position = composer_.state(MonitorId::Record).position();
    std::int64_t start = 0;
    const int i = t ? t->segmentAt(position, &start) : -1;
    if (i < 0 || t->segments.at(i).isFiller() || t->locked) return fail(tr("There is no audio segment there to adjust."));
    const model::Segment s = t->segments.at(i);
    const double here = static_cast<double>(position);
    const double sourceHere = static_cast<double>(s.sourceIn + (position - start));
    const double current = model::automationValue(s, param, sourceHere, model::sidePan(t->number, alternatingPan_));
    const double by = value - current;
    const QString description = param == AutomationParam::Volume ? tr("Volume Automation") : tr("Pan Automation");
    // The keyframe at the position.
    if (!model::keyframesIn(seq, {track}, here, here + 1e-3, param).isEmpty()) {
        model::setKeyframes(seq, {KeyRef{track, here}}, param, value);
        return commitSequence(description, seq);
    }
    // Every keyframe in the marked region, moved together (p.716).
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    if (record.in() || record.out()) {
        const auto [from, to] = markedRange(seq);
        const QList<KeyRef> inRange = model::keyframesIn(seq, {track}, from, to, param);
        if (here >= from && here < to && !inRange.isEmpty()) {
            model::moveKeyframes(seq, inRange, param, 0, by);
            return commitSequence(description, seq);
        }
    }
    // The keyframes either side (all of the segment's keyframes count, hidden ones too).
    const QList<KeyRef> all = model::keyframesIn(seq, {track}, static_cast<double>(start) - 1e9,
                                                 static_cast<double>(start + s.length) + 1e9, param, true);
    std::optional<KeyRef> before;
    std::optional<KeyRef> after;
    for (const KeyRef& k : all) {
        if (k.frame < here) before = k;
        if (k.frame > here && !after) after = k;
    }
    QList<KeyRef> around;
    if (before) around << *before;
    if (after) around << *after;
    if (!around.isEmpty()) {
        model::moveKeyframes(seq, around, param, 0, by);
        return commitSequence(description, seq);
    }
    // None yet: a first keyframe at the position, which sets the whole segment (p.716).
    model::addKeyframes(seq, {track}, here, param, alternatingPan_);
    model::setKeyframes(seq, {KeyRef{track, here}}, param, value);
    return commitSequence(description, seq);
}

bool Timeline::beginAutomationRecording() {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    recordBefore_ = rec->item;
    return true;
}

// Each write stores the sequence without an undo step; endAutomationRecording() makes the step.
void Timeline::writeAutomation(const QString& track, AutomationParam param, double value) {
    if (!recordBefore_) return;
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    const double here = static_cast<double>(composer_.state(MonitorId::Record).position());
    model::addKeyframes(seq, {track}, here, param, alternatingPan_);
    if (model::setKeyframes(seq, {KeyRef{track, here}}, param, value) == 0) return;
    model::BinItem after = rec->item;
    seq.storeIn(after);
    restore(rec->binId, after);
}

void Timeline::endAutomationRecording(bool cancel) {
    if (!recordBefore_) return;
    const model::BinItem before = *recordBefore_;
    recordBefore_.reset();
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!rec || rec->item.id != before.id) return;
    if (cancel) {
        restore(rec->binId, before);
        return;
    }
    if (rec->item.composition == before.composition) return;
    model::BinItem after = rec->item;
    after.modified = QDateTime::currentDateTimeUtc();
    commit(tr("Record Automation"), before, after);
}

bool Timeline::copyKeyframes() {
    const model::Sequence seq = recordSeq(composer_);
    if (seq.tracks().isEmpty() || showSource_) return false;
    const AutomationParam param = automationParam();
    if (!keySelection_.isEmpty()) {
        keyClipboard_ = model::copyKeyframes(seq, param == AutomationParam::Volume ? keySelection_ : QList<KeyRef>{},
                                             param == AutomationParam::Pan ? keySelection_ : QList<KeyRef>{});
        keysCopiedLast_ = true;
        return true;
    }
    keysCopiedLast_ = false;
    if (!display_.automation) return false;
    // The marked region on the selected tracks, else the selected audio segments (p.722).
    QStringList tracks = selectedAudioTracks(seq);
    double from = 0;
    double to = 0;
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    if (record.in() || record.out()) {
        std::tie(from, to) = markedRange(seq);
    } else {
        bool any = false;
        tracks.clear();
        for (const model::SegmentRef& ref : selection_) {
            const auto seg = model::segmentFor(seq, ref);
            if (!seg || !isAudio(ref.track)) continue;
            if (!tracks.contains(ref.track)) tracks << ref.track;
            from = any ? std::min(from, static_cast<double>(ref.start)) : static_cast<double>(ref.start);
            to = any ? std::max(to, static_cast<double>(ref.start + seg->length)) : static_cast<double>(ref.start + seg->length);
            any = true;
        }
        if (!any) return false;
        std::sort(tracks.begin(), tracks.end());
    }
    keyClipboard_ = model::copyKeyframes(seq, tracks, from, to);
    return false;
}

bool Timeline::pasteKeyframes(bool volume, bool pan) {
    message_.clear();
    if (keyClipboard_.isEmpty()) return fail(tr("Copy some audio keyframes first."));
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    const QStringList tracks = selectedAudioTracks(seq);
    if (tracks.isEmpty()) return fail(tr("Select the audio tracks to paste the keyframes onto."));
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    const double at = static_cast<double>(record.in().value_or(record.position()));
    if (model::pasteKeyframes(seq, tracks, at, keyClipboard_, volume, pan) == 0) {
        return fail(tr("There is no audio there for the keyframes."));
    }
    if (!display_.automation) setAudioData(display_.clipVolume, volume ? AutomationParam::Volume : AutomationParam::Pan);
    return commitSequence(tr("Paste Audio Keyframes"), seq);
}

std::optional<std::int64_t> Timeline::nextKeyframe(int direction) const {
    const model::Sequence seq = recordSeq(composer_);
    const std::int64_t position = composer_.state(MonitorId::Record).position();
    std::optional<std::int64_t> best;
    for (const KeyRef& k : model::keyframesIn(seq, selectedAudioTracks(seq), -1e12, 1e12, automationParam())) {
        const auto frame = static_cast<std::int64_t>(std::floor(k.frame + 1e-6));
        if (direction > 0 && frame > position && (!best || frame < *best)) best = frame;
        if (direction < 0 && frame < position && (!best || frame > *best)) best = frame;
    }
    return best;
}

namespace {

// The Paste Audio Keyframes dialog (p.722): Automation Gain and Automation Pan, both ticked.
std::optional<std::pair<bool, bool>> askPasteKeys(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("pasteAudioKeyframes"));
    dialog.setWindowTitle(QObject::tr("Paste Audio Keyframes"));
    auto* gain = new QCheckBox(QObject::tr("Automation Gain"), &dialog);
    auto* pan = new QCheckBox(QObject::tr("Automation Pan"), &dialog);
    gain->setChecked(true);
    pan->setChecked(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(gain);
    layout->addWidget(pan);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted || (!gain->isChecked() && !pan->isChecked())) return std::nullopt;
    return std::pair{gain->isChecked(), pan->isChecked()};
}

} // namespace

bool Timeline::pasteAudioKeyframes() {
    const auto which = pasteKeysAsk_ ? pasteKeysAsk_() : askPasteKeys(this);
    if (!which) return false;
    return pasteKeyframes(which->first, which->second);
}

} // namespace lumenis::ui
