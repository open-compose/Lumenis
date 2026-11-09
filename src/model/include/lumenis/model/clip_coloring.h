#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

// Clip colours in the Timeline (Editing Guide p.594-598; docs/spec/timeline-colors.md): which colour,
// if any, a segment is drawn in. The Timeline Fast menu's Clip Color dialog picks the kinds of
// colouring; when several apply, the first in the dialog's order wins ("The order of the options in the
// Clip Color dialog box indicates the priority order", p.596). The UI works out the facts about a
// segment's clip (ClipFacts); this file only decides.
namespace lumenis::model {

// The Clip Color dialog's choices and colours. Colours are "#rrggbb", as bin clip colours are.
struct ClipColorOptions {
    // Resolution Tracking (MultiRez, p.1112-1115; 8.6b): segments coloured by the MultiRez button's display, from
    // ClipFacts::resolution. The MultiRez button menu's Enable Clip Coloring sets it too. Off at first (ours).
    bool resolution = false;
    // Proxy (h.264 proxies) needs media Cutthulhu does not have yet; it is on by default (p.598) and kept so.
    bool proxy = true;
    bool offline = true;    // ours: the guide does not give the defaults
    bool linked = false;
    bool mixedRates = false;
    bool sdHd = false;
    bool local = false;     // "Select Timeline Local, and then click OK" (p.598): off at first
    bool source = true;     // ours
    QString offlineColor;   // red by default (p.1113: "offline clips are red")
    QString linkedColor;
    QString sdHdColor;
    // Resolution Tracking's colours: "blue or red by default" for a mismatch with Keep Existing or Offline, "yellow by
    // default" for media not available in the target settings (p.1112-1113); offline clips take offlineColor (red).
    QString mismatchColor;
    QString unavailableColor;
    // Mixed Rates: "A different color is available for each frame rate" (p.596), by mixedRates().
    QMap<QString, QString> rateColors;

    // The defaults, colours included (Default Colors, p.597, resets the colours to these).
    static ClipColorOptions defaults();
    // Back to the default colours; the choices stay.
    void resetColors();
    QJsonObject toJson() const;
    // Missing members keep their defaults.
    static ClipColorOptions fromJson(const QJsonObject& o);
    bool operator==(const ClipColorOptions&) const = default;
};

// The frame rates Mixed Rates has a colour for, as a clip's FPS column writes them ("23.976", "25",
// "29.97" ...).
QStringList mixedRates();

// What the Timeline knows of a segment's clip.
struct ClipFacts {
    BinItemKind kind = BinItemKind::MasterClip;
    QString sourceColor;     // the clip's colour in its bin (BinItem::color)
    bool offline = false;    // the clip, or its media, cannot be found
    bool linked = false;     // linked (AMA) media, not managed
    QString rate;            // the clip's frame rate (mixedRates()) when it differs from the sequence's
    bool otherDefinition = false; // SD in an HD project, or HD in an SD one
    // What the MultiRez button's display says of the clip's media (p.1111-1115): nothing while the display is inactive.
    enum class Resolution { None, Matched, Mismatched, Offline, Unavailable };
    Resolution resolution = Resolution::None;
};

// The colour a segment is drawn in, or empty for its track's own. With Resolution Tracking, a clip the MultiRez display
// marks takes its colour before any other: "If you enable clip coloring for MultiRez and then click the MultiRez button
// to display mismatches or target availability, this setting overrides any other Timeline clip coloring" (p.1113). `clip` is nothing when the clip
// cannot be found (then it is offline). Filler has none. Source colours of sequences, groups, motion
// effects and titles are not shown (p.596).
QString timelineClipColor(const ClipColorOptions& options, const Segment& segment, const std::optional<ClipFacts>& clip);

} // namespace lumenis::model
