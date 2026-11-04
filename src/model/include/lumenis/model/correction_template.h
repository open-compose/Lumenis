#pragma once

// Color Correction effect templates (FX Guide p.610-615; docs/spec/color-correction.md, 6.10d2a): "Like templates for
// other effects, Color Correction effect templates save all the adjustment values for a color correction so you can
// apply those values quickly to another segment" (p.610). "Media Composer saves all the Color Correction settings for
// the segment ... both the values set for each control and the status of each Enable button" (p.611), and "If you save
// a relationship color correction effect to a bin as an effect template, Media Composer saves the relationship type of
// the original effect as part of the template" (p.615).
//
// Ours: a template is an Effect item in a bin whose composition holds the segment's Source and Program corrections and
// their relationships under "colorCorrection". The Color Correction tool's buckets keep the same templates for the
// session (p.613).

#include "lumenis/model/bin.h"
#include "lumenis/model/color_correction.h"
#include "lumenis/model/sequence.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace lumenis::model {

struct CorrectionTemplate {
    ColorCorrection source;
    ColorCorrection program;
    CorrectionRelationship sourceRelationship = CorrectionRelationship::SourceSegment;
    CorrectionRelationship programRelationship = CorrectionRelationship::ProgramSegment;

    bool isEmpty() const { return source.isEmpty() && program.isEmpty(); }
    // The one adjustment the two sides make (mergedCorrection), for applying to one side.
    ColorCorrection merged() const;
    QJsonObject toJson() const;
    static CorrectionTemplate fromJson(const QJsonObject& o);
    bool operator==(const CorrectionTemplate&) const = default;
};

// A segment's corrections as a template (empty when it has none).
CorrectionTemplate correctionTemplateOf(const Segment& segment);
// The bin item for a template, "identified in the bin by its effect icon"; "By default, Media Composer names the template
// using the clip name of the segment" (p.613): the caller passes that name.
BinItem correctionTemplateItem(const CorrectionTemplate& t, const QString& name, const QDateTime& when);
// The template an Effect item holds; nothing for other items.
std::optional<CorrectionTemplate> correctionTemplateOf(const BinItem& item);

// "You can apply the values for the controls in a single tab in the Color Correction tool by dragging the template onto
// the tab that contains the group of controls you want to change" (p.610): the controls and Enable buttons at and
// under `path` ("hsl/controls/master", "curves", ...) in `target` become those of `from`; others stay. False when
// nothing changes.
bool applyCorrectionGroup(ColorCorrection& target, const ColorCorrection& from, const QString& path);

} // namespace lumenis::model
