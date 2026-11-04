// The Timeline's Color Correction effect templates (FX Guide p.610-615; docs/spec/color-correction.md, 6.10d2a):
// templates from the current segment, and applying them to it, to one tab's controls, to a segment a template is
// dropped on and to the selected segments. Declared in lumenis/ui/timeline.h.

#include "lumenis/model/correction_relations.h"
#include "lumenis/model/correction_template.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/timeline.h"

#include <QJsonDocument>
#include <QMimeData>

namespace lumenis::ui {

namespace {

// Puts `t` on the segment at `index` of `track`: with the template's relationships, each side it has and none of the
// sides it lacks; else its merged values on `relationship`'s side with that relationship.
bool putTemplate(model::Sequence& seq, const QString& track, int index, const model::CorrectionTemplate& t,
                 std::optional<model::CorrectionRelationship> relationship, const model::CorrectionClipLookup& lookup) {
    using model::CorrectionSide;
    if (relationship) return model::applyRelationshipCorrection(seq, track, index, *relationship, t.merged(), lookup);
    bool changed = false;
    if (!t.source.isEmpty()) changed |= model::applyRelationshipCorrection(seq, track, index, t.sourceRelationship, t.source, lookup);
    if (!t.program.isEmpty()) changed |= model::applyRelationshipCorrection(seq, track, index, t.programRelationship, t.program, lookup);
    model::Segment& s = seq.track(track)->segments[index];
    for (const CorrectionSide side : {CorrectionSide::Source, CorrectionSide::Program}) {
        const bool has = side == CorrectionSide::Source ? !t.source.isEmpty() : !t.program.isEmpty();
        model::ColorCorrection& c = model::correctionOf(s, side);
        if (has || c.isEmpty()) continue;
        c = {};
        (side == CorrectionSide::Source ? s.sourceRelationship : s.programRelationship) = model::segmentRelationship(side);
        changed = true;
    }
    return changed;
}

} // namespace

std::optional<model::CorrectionTemplate> Timeline::currentCorrectionTemplate() const {
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return std::nullopt;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const model::Track* t = seq.track(correctionTrack());
    const int i = t ? t->segmentAt(composer_.state(MonitorId::Record).position()) : -1;
    if (i < 0 || t->segments.at(i).isFiller()) return std::nullopt;
    return model::correctionTemplateOf(t->segments.at(i));
}

QString Timeline::currentCorrectionName() const {
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return {};
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const model::Track* t = seq.track(correctionTrack());
    const int i = t ? t->segmentAt(composer_.state(MonitorId::Record).position()) : -1;
    if (i < 0 || t->segments.at(i).isFiller()) return {};
    const model::Segment& s = t->segments.at(i);
    const auto clip = composer_.resolveForSegment(s);
    return clip ? clip->name : s.name;
}

bool Timeline::applyCorrectionTemplate(const model::CorrectionTemplate& t, bool templateRelationships) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return fail(tr("Load a sequence first."));
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    const model::Track* trk = seq.track(correctionTrack());
    const int i = trk ? trk->segmentAt(composer_.state(MonitorId::Record).position()) : -1;
    if (i < 0 || trk->segments.at(i).isFiller()) return fail(tr("There is no segment to correct at the position."));
    if (t.isEmpty()) return fail(tr("The template has no correction."));
    if (!putTemplate(seq, correctionTrack(), i, t, templateRelationships ? std::nullopt : std::optional(correctionRelationship_),
                     correctionClipLookup())) {
        return false;
    }
    const QList<model::SegmentRef> keep = selection_;
    const bool ok = commitSequence(tr("Apply Color Correction"), seq);
    selection_ = keep;
    view_->update();
    return ok;
}

bool Timeline::applyCorrectionTemplateGroup(const model::CorrectionTemplate& t, const QString& path) {
    message_.clear();
    const auto current = currentCorrection();
    if (!current) return fail(tr("There is no segment to correct at the position."));
    model::ColorCorrection c = *current;
    if (!model::applyCorrectionGroup(c, t.merged(), path)) return false;
    return setCurrentCorrection(c, tr("Apply Color Correction"));
}

bool Timeline::dropCorrectionTemplate(const QString& track, std::int64_t frame, const model::CorrectionTemplate& t) {
    message_.clear();
    if (showSource_ || !recordSequence()) return false;
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    const model::Track* trk = seq.track(track);
    if (!trk) return false;
    if (trk->kind != model::TrackKind::Video) return fail(tr("Color corrections go on video tracks."));
    if (trk->locked) return fail(tr("The track is locked."));
    const int i = trk->segmentAt(frame);
    if (i < 0 || trk->segments.at(i).isFiller()) return fail(tr("There is no segment to correct there."));
    if (t.isEmpty() || !putTemplate(seq, track, i, t, std::nullopt, correctionClipLookup())) return false;
    return commitSequence(tr("Apply Color Correction"), seq);
}

bool Timeline::applyCorrectionTemplateToSelection(const model::CorrectionTemplate& t) {
    message_.clear();
    if (!recordSequence()) return false;
    if (t.isEmpty()) return fail(tr("The template has no correction."));
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    bool any = false;
    bool changed = false;
    for (const model::SegmentRef& ref : selection_) {
        const model::Track* trk = seq.track(ref.track);
        if (!trk || trk->kind != model::TrackKind::Video || trk->locked) continue;
        const int i = trk->segmentAt(ref.start);
        if (i < 0 || trk->segments.at(i).isFiller()) continue;
        any = true;
        changed |= putTemplate(seq, ref.track, i, t, correctionRelationship_, correctionClipLookup());
    }
    if (!any) return fail(tr("Select segments in the Timeline first."));
    if (!changed) return false;
    const QList<model::SegmentRef> keep = selection_;
    const bool ok = commitSequence(tr("Apply Color Correction"), seq);
    selection_ = keep;
    view_->update();
    return ok;
}

std::optional<model::CorrectionTemplate> Timeline::droppedCorrectionTemplate(const QMimeData* mime) const {
    if (!mime) return std::nullopt;
    if (mime->hasFormat(correctionTemplateMime())) {
        const QJsonObject o = QJsonDocument::fromJson(mime->data(correctionTemplateMime())).object();
        return model::CorrectionTemplate::fromJson(o.value(QStringLiteral("template")).toObject());
    }
    if (!mime->hasFormat(binItemsMime())) return std::nullopt;
    const QStringList lines = QString::fromUtf8(mime->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() != 2) return std::nullopt;
    const auto item = composer_.resolve(lines.at(0), lines.at(1));
    return item ? model::correctionTemplateOf(*item) : std::nullopt;
}

} // namespace lumenis::ui
