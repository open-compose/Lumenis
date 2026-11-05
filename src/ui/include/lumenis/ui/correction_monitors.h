#pragma once

#include "lumenis/model/color_correction.h"

#include <QPointF>
#include <QMimeData>
#include <QList>
#include <QRectF>
#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

class QToolButton;

namespace lumenis::ui {

class Composer;

// What a Color Correction mode monitor shows, its Source menu (FX Guide p.591): a frame, or one of the waveform and
// vectorscope displays of the active monitor's frame (p.686-691; lumenis/media/scopes.h, 6.10c1).
enum class CorrectionSource {
    Empty,
    EntireSequence,
    Reference,
    Current,
    Previous,
    Next,
    SecondPrevious,
    SecondNext,
    QuadDisplay,
    RgbHistogram,
    RgbParade,
    Vectorscope,
    YWaveform,
    YWaveformHdr1000,
    YWaveformHdr10000,
    YcWaveform,
    YcbcrHistogram,
    YcbcrParade,
};
QString correctionSourceText(CorrectionSource source);
// Whether a source is a waveform or vectorscope display.
bool isScopeSource(CorrectionSource source);

// Color Correction mode's monitor window (FX Guide p.589-594; docs/spec/color-correction.md, 6.10b1): three monitors
// in place of the Source and Record monitors, by default "the last frame of the segment preceding the current segment",
// the frame at the position and "the first frame of the segment following" (p.590). Each has a Source menu above it
// and the Dual Split, Go to Previous/Next Uncorrected Shot and Remove Effect buttons under it (p.592). The Composer owns
// it and shows it while the Timeline is in Color Correction mode (Composer::setCorrectionMode); pictures are the Record
// monitor's (Composer::pictureAt), Dual Split's left part without corrections (Composer::uncorrectedPictures).
class CorrectionMonitors : public QWidget {
    Q_OBJECT

public:
    static constexpr int kMonitors = 3;
    // The shot `step` shots from the current one on the correction track (0 the current one, -2 to 2), as its
    // [start, end) frames in the sequence; nothing past the track's ends (model::correctionShotStart).
    using ShotResolver = std::function<std::optional<std::pair<std::int64_t, std::int64_t>>(int step)>;

    explicit CorrectionMonitors(Composer& composer, QWidget* parent = nullptr);

    void setShotResolver(ShotResolver resolver) { shots_ = std::move(resolver); }

    // The keyframe markers in the position bar below the Current monitor (FX Guide p.686; 6.10d3c): the current
    // segment's frames in the sequence, its correction's keyframes as frames into it and which are selected. The
    // Timeline gives them (Timeline::correctionKeyframeMarkers); without them the bar is the segment and the position.
    struct KeyframeMarkers {
        std::int64_t start = 0;
        std::int64_t length = 0;
        QList<double> frames;
        QList<bool> selected;
    };
    using KeyframeResolver = std::function<std::optional<KeyframeMarkers>()>;
    void setKeyframeResolver(KeyframeResolver resolver) { keyframes_ = std::move(resolver); }
    std::optional<KeyframeMarkers> keyframeMarkers() const { return keyframes_ ? keyframes_() : std::nullopt; }
    // The bar under a monitor; shown only under the one whose Source menu is Current.
    QWidget* keyframeBar(int monitor) const;

    // The monitors' Source menus, 0 left to 2 right. "Current ... is not available in the Source menu if another
    // monitor is already set to Current" (p.591): setting it there is refused.
    CorrectionSource source(int monitor) const { return monitors_.at(static_cast<std::size_t>(monitor)).source; }
    bool setSource(int monitor, CorrectionSource source);
    // The sequence frame a monitor shows; nothing for Empty, a Reference before Reference Current, or a shot past the
    // track's ends. A waveform or vectorscope monitor gives the active monitor's frame, whose information it shows.
    std::optional<std::int64_t> frameOf(int monitor) const;

    // The active monitor (p.590: "activating monitors ... are the same"); the centre one at first. A monitor showing a
    // waveform or vectorscope is not made active: "Media Composer displays the information for the currently active
    // monitor" (p.686).
    int activeMonitor() const { return active_; }
    void setActiveMonitor(int monitor);

    // Reference Current (p.591, p.594): locks the frame at the position as the reference, shown in the right monitor
    // (the left one when that is already Reference), whose Source menu changes to Reference.
    void referenceCurrent();
    std::optional<std::int64_t> referenceFrame() const { return reference_; }
    // The Escape key "to switch quickly between the center and right monitors" (p.593).
    void switchCurrentAndReference();

    // Dual Split (p.592): "the uncorrected image appears on the left and the image with currently active corrections
    // applied appears on the right"; the uncorrected part is a box with triangular handles at two corners (ours: the
    // left half of the picture at first), as fractions of the picture.
    bool dualSplit(int monitor) const { return monitors_.at(static_cast<std::size_t>(monitor)).dualSplit; }
    void setDualSplit(int monitor, bool on);
    QRectF splitBox(int monitor) const { return monitors_.at(static_cast<std::size_t>(monitor)).box; }
    void setSplitBox(int monitor, const QRectF& box);

    QWidget* pictureWidget(int monitor) const;
    QToolButton* sourceButton(int monitor) const;
    QToolButton* monitorButton(int monitor, const QString& commandId) const;

    // Draws what changed: the position moved, a correction changed.
    void refresh();

    // The Luma Ranges tab's preview in the active monitor (FX Guide p.642-643): Input "Displays the uncorrected
    // image", Three-tone the uncorrected image "using white, gray, and black to represent the highlights, midtones,
    // and shadows luminance ranges" of `ranges` (the correction whose Luma Ranges curves count), Output the corrected
    // image (the monitors' usual picture).
    // The Channels tab's previews (p.645-646) add one colour channel in monochrome, uncorrected (Input) or corrected
    // (Output).
    enum class Preview { Output, Input, ThreeTone, RedInput, GreenInput, BlueInput, RedOutput, GreenOutput, BlueOutput };
    void setPreview(Preview preview, std::function<model::ColorCorrection()> ranges = {});
    Preview preview() const { return preview_; }

    // The Secondary tab's Color Selection buttons (FX Guide p.678-679): while sampling, a drag over a monitor's
    // picture samples "every color value that the tip of the syringe passes over" (Drag), or a click picks one colour
    // (Click); colorsSampled() gives them when the mouse is let go.
    enum class Sampling { None, Drag, Click };
    void setSampling(Sampling sampling) { sampling_ = sampling; }
    Sampling sampling() const { return sampling_; }
    // The Correction setting's Features (Editing Guide p.1166; FX Guide p.608): "Eyedropper 3 x 3 Averaging ...
    // averaging the values of a 3 x 3 sample of pixels centered on the eyedropper's position" (ours: for every
    // colour the monitors sample), and "Dual-Split remains Active During Playback" (the guide says no more; ours: off,
    // the box is not drawn while the Composer plays).
    void setEyedropperAveraging(bool on) { averaging_ = on; }
    bool eyedropperAveraging() const { return averaging_; }
    void setDualSplitDuringPlayback(bool on);
    // The YC Waveform's scale: PAL (millivolts) or NTSC (IRE), by the project's format (p.690).
    void setPal(bool pal);
    bool isPal() const { return pal_; }
    bool dualSplitDuringPlayback() const { return dualSplitPlayback_; }
    // A Color Correction effect template dropped "to the monitor containing the current segment" (FX Guide p.614): the
    // hook says whether a drag carries one (`apply` false) and applies it (true), with the template's relationships
    // (p.615). Ours: a drop anywhere on the mode's monitors. MainWindow passes the Timeline's.
    using TemplateDrop = std::function<bool(const QMimeData* mime, bool apply)>;
    void setTemplateDrop(TemplateDrop drop) {
        templateDrop_ = std::move(drop);
        setAcceptDrops(static_cast<bool>(templateDrop_));
    }

signals:
    // The Go to Previous/Next Uncorrected Shot and Remove Effect buttons, for the Timeline.
    void commandRequested(const QString& commandId);
    void activeMonitorChanged(int monitor);
    void colorsSampled(const QList<QColor>& colors);
    // The keyframe markers (p.686): one clicked (`extend` with Ctrl, "Ctrl-click ... to deselect it"), the selected
    // ones Alt-dragged `delta` frames, and the Delete key pressed over the monitors.
    void keyframePressed(double frame, bool extend);
    void keyframesDragged(double delta);
    void keyframesDeleteRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    friend class CorrectionPicture;
    friend class CorrectionKeyframeBar;
    KeyframeResolver keyframes_;
    TemplateDrop templateDrop_;
    struct Monitor {
        CorrectionSource source = CorrectionSource::Current;
        bool dualSplit = false;
        QRectF box{0.0, 0.0, 0.5, 1.0};
        QWidget* picture = nullptr;
        QToolButton* menu = nullptr;
        QWidget* buttons = nullptr;
        QWidget* bar = nullptr;
    };
    void syncMenu(int monitor);
    Composer& composer_;
    std::array<Monitor, kMonitors> monitors_;
    int active_ = 1;
    std::optional<std::int64_t> reference_;
    ShotResolver shots_;
    Preview preview_ = Preview::Output;
    Sampling sampling_ = Sampling::None;
    bool averaging_ = false;
    bool dualSplitPlayback_ = true;
    bool pal_ = false;
    std::function<model::ColorCorrection()> ranges_;
};

} // namespace lumenis::ui
