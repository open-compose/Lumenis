#pragma once

#include "lumenis/media/effect_handles.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/effect_template.h"
#include "lumenis/model/intraframe.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/titler.h"
#include "lumenis/model/tracking.h"
#include "lumenis/ui/brush_preview.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/intraframe_tool.h"

#include <QHash>
#include <QImage>
#include <QSet>
#include <QJsonValue>
#include <QPointer>
#include <QWidget>

#include <cmath>
#include <functional>
#include <optional>

class QCheckBox;
class QMenu;
class QScrollBar;
class QLabel;
class QMimeData;
class QLineEdit;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace lumenis::ui {

class KeyframeTrack;
class Timeline;
class TitlerDashboard;
class TitlerPanel;
class TitlerTool;
class IntraframeTool;
class TrackingWindow;

// A control of the Effect Editor (FX guide p.491-586), from
// resources/effect_parameters.json.
struct EffectParameter {
    // The parameter's key in model::Effect::parameters: its name as printed, or, when an earlier
    // parameter of its category has that name, "<subcategory>: <name>" (ours; e.g. the Color Effect's
    // second Invert, "Chroma Adjust Parameters: Invert").
    QString name;
    QString text; // the name as printed, shown beside its control
    QString kind; // slider, checkbox, color, menu, button, other
    QString subcategory;
    double min = 0.0; // a slider's range in the parameter's units; min <= max
    double max = 100.0;
    // Decimal places a slider's values have (Avid Pan & Zoom's Zoom Factor, 0.1 to 20, p.529); 0 for whole
    // numbers. The slider itself counts in steps of 10^-decimals.
    int decimals = 0;
    double scale() const { return std::pow(10.0, decimals); }
    // A value as the slider's position, and back (a whole number with no decimals).
    int sliderPosition(double value) const { return static_cast<int>(std::lround(value * scale())); }
    QJsonValue fromSlider(int position) const { return decimals > 0 ? QJsonValue(position / scale()) : QJsonValue(position); }
    // A value as the slider's label shows it.
    QString shown(double value) const { return QString::number(value, 'f', decimals); }
    // A value rounded to the slider's steps.
    double rounded(double value) const { return std::round(value * scale()) / scale(); }
    QJsonValue defaultValue; // as in the file; may be missing
    QStringList options;     // a menu's entries
    // "Effect parameters are keyframeable unless otherwise noted" (FX Guide p.491); the file marks the
    // others. Only sliders and colours take keyframes for now (ours: flags and menus wait).
    bool keyframeable = true;
};

// The parameter categories, by name as printed (p.491).
// The parameters of one category, in order; null for a category not in the file. The list lives as
// long as the program.
const QList<EffectParameter>* effectParameters(const QString& category);

// Tools > Effect Editor (FX guide p.83-88): the effect at the Record
// monitor's position on the selected tracks (p.84; blank otherwise), its
// parameter categories with triangular openers, sliders and their values,
// the buttons down the right side and the Transition parameters at the
// bottom (p.500). Changes are undoable edits of the sequence. Playback and
// rendering come later (ROADMAP Phase 6), so their buttons are off.
//
// Advanced keyframes (ch.4, p.94-131; docs/spec/effect-keyframes.md): the Show/Hide Keyframe Graphs
// button shows a keyframe track beside the effect's title row (the effect track), each parameter
// category (group tracks) and each keyframeable parameter (parameter tracks), whose openers show the
// parameter's keyframe graph. Keyframes are selected by clicking (Ctrl+click adds or takes away),
// moved by dragging, and changed through the tracks' shortcut menus and the Add Keyframe, Go to
// Previous Event and Go to Next Event buttons; sliders show and set the values at the position.
//
// A panel's contents ("EffectEditor"), made by MainWindow. It follows Timeline::changed and
// Composer::changed, and makes every change through the Timeline (changeEffect, setTransitions).
// An effect's parameter values live in model::Effect::parameters as {category: {parameter: value}}.
class EffectEditor : public QWidget, public IntraframeHost {
    Q_OBJECT

public:
    // The Add Keyframe Mode and Delete Keyframe Mode menus' commands (p.107-111): which parameters the
    // Add Keyframe button adds keyframes to, and Alt+click deletes the selected keyframes from.
    enum class KeyframeMode { ActiveParameter, ActiveGroup, OpenGroups, EnabledGroups, OpenGraphs, AllParameters };
    // A keyframe: a parameter ("Category/Name") and its index in model::ParameterKeyframes::keys.
    struct KeyframeId {
        QString parameter;
        int index = 0;
        bool operator==(const KeyframeId&) const = default;
    };
    // How dragging a Bézier direction handle changes the other one (p.122-123): both alike
    // (Symmetrical, the default), the other only turning (Asymmetrical), or not at all (Independent).
    enum class HandleMode { Symmetrical, Asymmetrical, Independent };
    // The Effect Editor settings (Editing Guide p.1171-1172; FX Guide p.131-132) that are built.
    struct Options {
        bool indentRows = true; // ours: the guide gives no default
        bool largeText = false;
        bool setPositionToKeyframe = true;
        bool updatePositionWhilePlaying = false; // "deselected by default"
        bool showModeMenu = true;
        KeyframeMode mode = KeyframeMode::OpenGroups;
        bool operator==(const Options&) const = default;
    };

    // `timeline` must outlive the editor.
    explicit EffectEditor(Timeline& timeline, QWidget* parent = nullptr);

    // Shows the effect under the position, or the Transition parameters of
    // cuts with trim rollers, or nothing.
    // The controls are rebuilt only when the effect shown changes; otherwise their values are
    // brought up to date.
    void refresh();
    // The effect shown; nothing when blank or showing only the Transition parameters.
    const std::optional<model::EffectRef>& shown() const { return shown_; }
    // Neither an effect nor Transition parameters are shown.
    bool isBlank() const;
    // The shown effect's name; empty if none.
    QString effectName() const;
    // The shown effect's parameter categories, in order.
    QStringList categories() const;
    // Whether a category's opener is open; false for one not shown.
    bool isOpen(const QString& category) const;
    void setOpen(const QString& category, bool open);
    // A parameter's control; null if it is not shown or is of another kind.
    QSlider* slider(const QString& category, const QString& parameter) const;
    QCheckBox* checkBox(const QString& category, const QString& parameter) const;
    QLineEdit* durationBox() const { return duration_; }
    QToolButton* alignmentButton() const { return alignment_; }
    // Transition Effect Alignment and Duration (p.500).
    // Both apply to the transition shown, or to the trim rollers' video cuts in Trim mode; they do
    // nothing without either. The duration is "seconds:frames" or frames; setDuration() returns
    // false, and shows the old value again, when it cannot be read or applied.
    void setAlignment(model::TransitionAlignment alignment);
    bool setDuration(const QString& text);
    // The Custom Grid's More Options (FX Guide p.494): the grid of a Matrix Wipe, Sawtooth Wipe or some
    // Shape Wipes, in columns and rows, at least 2 x 2 (default 8 x 6). The button asks for them;
    // setCustomGrid() sets them on the shown effect.
    void setCustomGrid(int columns, int rows);
    // Timecode Burn-In (FX Guide p.505-508; docs/spec/burn-in.md): its Display 1-3 and Notes tabs
    // stand in for the generic controls. setBurnIn() sets one setting of a display (1 to 3; 0 the
    // Notes) on the shown effect; burnInControl() is the control for it ("Display 1/Reader",
    // "Notes/Text"), null when not shown.
    void setBurnIn(int display, const QString& name, const QJsonValue& value);
    QWidget* burnInControl(const QString& which) const { return burnInControls_.value(which); }
    // A parameter's ("Category/Name") value until it is set: the effect's own default
    // (media::parameterDefault), else the parameter's (effect_parameters.json); undefined if neither.
    QJsonValue defaultValue(const model::Effect& effect, const QString& parameter) const;

    // ---- Advanced keyframes
    // The Show/Hide Keyframe Graphs button (p.96): the keyframe tracks and graphs, hidden at first.
    bool keyframeGraphsShown() const { return graphsShown_; }
    void setKeyframeGraphsShown(bool shown);
    // A keyframe track or graph, by "effect", "group:<category>", "<category>/<name>" (a parameter
    // track) or "graph:<category>/<name>"; null when not shown.
    QWidget* keyframeTrack(const QString& which) const;
    // Where time `t` of the effect is in a track (its widget's coordinates), and a value in a graph.
    QPoint keyframePoint(const QString& which, double t, double value = 0.0) const;
    // A parameter's keyframe graph (the opener left of its track, p.96-97).
    bool isGraphOpen(const QString& parameter) const { return openGraphs_.contains(parameter); }
    void setGraphOpen(const QString& parameter, bool open);
    // The keyframeable parameters of the shown effect ("Category/Name"), in order.
    QStringList keyframeableParameters() const;
    // keyframeableParameters and a title's keyframed numbers: what the Effect Preview's position bar and Go to
    // Previous/Next Event go through.
    QStringList positionBarParameters() const { return keyframeableParameters() + titleKeyframed(); }
    // The active parameter (the control last used, p.107); empty if none.
    QString activeParameter() const { return activeParameter_; }
    void setActiveParameter(const QString& parameter);
    // The shown effect's time at the Record monitor's position, and how its frames map to time.
    double positionTime() const override;
    const model::EffectSpan& span() const override { return span_; }

    // The selection (p.111-112); the last one selected is the reference for Align and Slip (p.115).
    const QList<KeyframeId>& selectedKeyframes() const { return selection_; }
    // Selects a keyframe: alone, or with `extend` (Ctrl+click) added or, if selected, taken away.
    void selectKeyframe(const KeyframeId& id, bool extend);
    void deselectAllKeyframes(const QStringList& parameters = {});

    // The default Add Keyframe command (p.105, p.131) and whether the button shows its menu first
    // (Show Add Keyframe Mode Menu, p.132).
    KeyframeMode keyframeMode() const { return mode_; }
    void setKeyframeMode(KeyframeMode mode) { mode_ = mode; }
    bool showsKeyframeModeMenu() const { return showModeMenu_; }
    void setShowsKeyframeModeMenu(bool show) { showModeMenu_ = show; }
    // Set Position To Keyframe (p.132): clicking a keyframe moves the position to it.
    bool setsPositionToKeyframe() const { return positionToKeyframe_; }
    void setSetsPositionToKeyframe(bool on) { positionToKeyframe_ = on; }
    // All of them at once. The Effect Editor shortcut menu changes them too and then says so with
    // optionsChanged(), so that the setting can follow.
    Options options() const;
    void setOptions(const Options& options);
    // The parameters a mode covers.
    QStringList parametersFor(KeyframeMode mode) const;
    // Adds keyframes at the position to the mode's parameters, with their values there; deletes the
    // selected keyframes of the mode's parameters. Each is one undo step; false if nothing changed.
    bool addKeyframes(KeyframeMode mode);
    bool deleteKeyframes(KeyframeMode mode);
    // Go to Previous Event and Go to Next Event (p.128): the position to the nearest keyframe before or
    // after it, which becomes selected. False when there is none.
    bool goToEvent(bool next);

    // The shortcut menu of a keyframe track or graph (p.130), for `which` as keyframeTrack() takes it.
    // The caller owns the menu. Group and effect tracks say "Apply to Group" or "Apply to All" while
    // it is open (p.104).
    QMenu* keyframeMenu(const QString& which, QWidget* parent = nullptr);
    // The Effect Editor shortcut menu (p.131-132; right-click outside the tracks), owned by the caller.
    QMenu* effectEditorMenu();
    // The parameters a track covers: its parameter, its category's or all of them.
    QStringList parametersOf(const QString& which) const;

    // A drag of the keyframe `id` from a track or graph (p.112-113) by `frames` in time and `value` in
    // its parameter's units; with `previewOnly` it is shown but not stored. Horizontal moves take every
    // selected keyframe with them, stopping a frame short of unselected ones; vertical ones change the
    // selected keyframes of the dragged keyframe's parameter, within its range. finishDrag() stores the
    // previewed drag as one undo step.
    void dragKeyframes(const KeyframeId& id, double frames, double value, bool previewOnly);
    void finishDrag();

    // ---- Keyframe graph views (p.97-101)
    // The time shown across the tracks and graphs: from viewStart() over viewSpan(); the whole effect
    // (0, 1) at first. The horizontal scale bar zooms it around the position indicator (p.99), the
    // scroll thumbwheel and Alt+drag in a graph move it (p.101).
    double viewStart() const { return viewStart_; }
    double viewSpan() const { return viewSpan_; }
    QSlider* horizontalScaleBar() const { return hScale_; }
    QScrollBar* scrollThumbwheel() const { return hScroll_; }
    void scrollView(double by);
    // A graph's range of values shown, its parameter's range at first (p.97); its vertical scale bar
    // zooms it and may go beyond that range (p.99).
    std::pair<double, double> graphRange(const QString& parameter) const;
    void setGraphRange(const QString& parameter, double low, double high);
    // Zoom to Curve Height (p.100): the curve, and the direction handles of a selected Bézier keyframe,
    // fill the graph over the time shown.
    void zoomToCurveHeight(const QString& parameter);
    // Reset All Graph Views (p.101): the whole effect and every graph's full range.
    void resetGraphViews();

    // ---- Copy and paste (p.116-119)
    // Edit > Copy with keyframes selected: copies them, keeping their distance from the leftmost one.
    // False with none selected.
    bool copyKeyframes();
    // Copy All Values at Position (p.118, Ctrl+Alt+C in a graph): every keyframeable parameter's value
    // at the position.
    void copyAllValues();
    // Edit > Paste: values copied from one keyframe go to the selected keyframes of its parameter;
    // otherwise the keyframes are made (or replaced) with the leftmost at the position. One undo step;
    // false when nothing was copied or nothing changed.
    bool pasteKeyframes();
    bool hasCopiedKeyframes() const { return !clipboard_.isEmpty(); }

    // ---- Bézier direction handles (p.122-123)
    HandleMode handleMode() const { return handleMode_; }
    void setHandleMode(HandleMode mode) { handleMode_ = mode; }
    // Sets keyframe `id`'s handles (in its parameter's time and value), previewed or as one undo step.
    void setKeyframeHandles(const KeyframeId& id, const model::BezierHandle& in, const model::BezierHandle& out, bool previewOnly);

    // ---- 3D (FX Guide ch.11; docs/spec/3d-effects.md)
    // The 3D Promote button (p.86): "Promotes the effect to a 3D effect" (model::promoteTo3D), one undo step;
    // enabled only for an effect that can be. False when the shown effect cannot be promoted.
    bool promoteTo3D();
    QToolButton* promote3DButton() const { return promote3D_; }
    // Stamp's buttons (p.585): a Stamp Image or a Clear at the position's time in the shown 3D effect, one undo
    // step each (media::stampTimes). False when no 3D effect is shown.
    bool stampImage();
    bool clearStamps();

    // ---- Direct manipulation (p.92-93, p.292-293; media/effect_handles.h, docs/spec/3d-effects.md)
    // In Effect mode the Effect Preview monitor shows the shown effect's wire frame and handles: always for a
    // 2D effect that places a picture (Picture-in-Picture, Resize, the masks), and for a 3D effect those of
    // the handle button selected. "Click the image manipulation button a second time to turn off the
    // handles" (p.292): setHandleTool(None). The buttons show only for a 3D effect; another effect turns them
    // off.
    media::HandleTool handleTool() const { return handleTool_; }
    void setHandleTool(media::HandleTool tool);
    QToolButton* handleButton(media::HandleTool tool) const { return handleButtons_.value(static_cast<int>(tool)); }
    // The Outline/Path button (p.84, p.92-93): the outline and "the path a moving effect takes on the
    // screen", with a dot at each keyframe; enabled for an effect that has an outline.
    bool outlineShown() const { return outlineShown_; }
    void setOutlineShown(bool shown);
    QToolButton* outlineButton() const { return outline_; }
    // The picture the handles are worked out in: 1920 pixels wide at the project's aspect ratio.
    QSizeF previewSize() const;

    // ---- Titler+ (6.8b; titler_tool.h)
    // The tools working on a shown Titler+ title in the Record monitor; made with the editor.
    TitlerTool* titlerTool() const { return titler_; }
    // The Dashboard (FX Guide p.302), made when first asked for; and the Effect Editor's own Titler+ controls while a
    // title is shown (null otherwise).
    TitlerDashboard* titlerDashboard();
    TitlerPanel* titlerPanel() const { return titlerPanel_; }
    // The shown Titler+ effect's title (with a drag or typing in progress); nothing when another effect is shown.
    std::optional<model::Title> shownTitle() const;
    // The title as stored (without any preview).
    std::optional<model::Title> storedTitle() const;
    // Shows `title` in the Record monitor and the parameters without an undo step yet; drops that again; stores it as
    // one undo step.
    void previewTitle(const model::Title& title);
    void cancelTitlePreview();
    bool commitTitle(const QString& description, const model::Title& title);
    // Add Keyframe / Delete Keyframe for a title (6.8c1): keyframes at the position for every number of the selected
    // objects (`wholeTitle`: the title's Background and Foreground), or removing those there; one undo step.
    bool setTitleKeyframes(bool add, bool wholeTitle);
    // The title's keyframed numbers ("Titler:<id>/<name>", "Titler/<name>"), shown in the Effect Preview's position bar
    // with the catalog's parameters.
    QStringList titleKeyframed() const;
    // Roll / Crawl on or off for the shown title (model::setRollCrawl), one undo step; the shown effect's trackers'
    // names; whether the Tracking window is open (6.8c3).
    bool setTitleRollCrawl(bool on);
    QStringList trackerNames() const;
    bool trackingWindowShown() const;
    // A template dragged from a bin or the effect icon (Timeline::droppedTemplate), for Titler+ presets.
    std::optional<model::EffectTemplate> droppedTemplate(const QMimeData* mime) const;
    // The shown Titler+ effect as it is stored, and its length in frames; nothing when another effect is shown.
    std::optional<std::pair<model::Effect, std::int64_t>> shownTitlerEffect() const;
    void refreshTitleOverlay() { syncPreview(); }

    // ---- Intraframe objects (ROADMAP 6.9b; intraframe_tool.h, docs/spec/intraframe.md)
    // The drawing tools in the Effect Preview, for an Intraframe effect at the outermost level.
    IntraframeTool* intraframeTool() const { return intraframe_; }
    // The shown Intraframe effect with all its objects as stored; nothing for other effects.
    std::optional<model::Effect> intraframeEffect() const override { return wholeIntraframe_; }
    // The effect as a drag shows it in the Record monitor before its undo step (pictureOverride); nothing ends it.
    void previewIntraframe(const std::optional<model::Effect>& effect) override;
    // The whole effect replaced by `effect` as one undo step.
    bool changeIntraframe(const QString& description, const model::Effect& effect) override;
    // The objects selected: with one, the editor's controls show and change that object's parameters ("Select the
    // object ... and perform any of the following procedures", p.404); with none or several, the effect's own, which
    // new objects take.
    void setIntraframeSelection(const QStringList& ids) override;

    // ---- Brushes and the eyedropper (ROADMAP 6.9c1; effect_editor_brush.cpp, brush_preview.h)
    // The Brush category shown (the selected object's, or the effect's).
    QJsonObject shownBrush() const;
    // "click one of the Brush Shape buttons. The default shape of the brush head changes to your selection" (p.397).
    bool setBrushShape(const QString& shape);
    // A custom head from the Brush Preview window (p.398).
    bool setBrushHead(const QList<model::ControlPoint>& head);
    // A brush template dropped on the Brush Preview window: "The brush in the Brush Preview window assumes the same
    // parameters as the brush template you saved to the bin" (p.399).
    bool applyBrushTemplate(const QJsonObject& brush);
    // The brush template the Brush Template icon drags to a bin (brushTemplateMime).
    QMimeData* brushTemplateMime() const;
    // The eyedropper (p.89, p.397): a colour parameter set to the colour under a point of the screen, or of the Record
    // monitor's picture at a fraction; false where there is no picture.
    bool pickColorAt(const QString& category, const QString& parameter, const QPoint& global);
    bool pickPaintColor(const QPointF& fraction) override;
    BrushPreview* brushPreview() const { return brushPreview_; }
    // A 24-fps film project: the Paint Effect has the Previsualization Marker tool, which "does not appear in the
    // Effect Editor for the Paint Effect in any other project types" (p.412). Set by MainWindow from the project.
    void setFilmProject(bool film);
    bool filmProject() const { return filmProject_; }
    // Two-field media: Scratch Removal's Frame Processing button shows (FX p.421, p.547). Set by MainWindow.
    void setInterlacedProject(bool interlaced);
    // Export Matte PICT (p.418): the shown AniMatte effect's matte at the position, `size` large (media::aniMatteImage);
    // a null image when the editor shows no AniMatte effect. exportMattePict writes it as a PICT file.
    QImage aniMatteImage(const QSize& size) const;
    bool exportMattePict(const QString& path, const QSize& size) const;
    const QStringList& intraframeSelection() const { return intraframeSelection_; }
    // The Effect Preview picture's width over its height.
    double previewAspect() const override;
    void refreshIntraframeOverlay() override { syncPreview(); }
    // The shown effect as previewed, for the Record monitor's picture: its place and the effect, while a preview is
    // under way at the outermost level.
    std::optional<std::pair<model::EffectRef, model::Effect>> pictureOverride() const;
    // Dragging handle `id` from `from` to `to` (fractions of the picture, as Composer::effectHandleDragged
    // gives them): the shown effect as it was when the drag began, with the values media::dragEffectHandle
    // gives, rounded and kept to the controls' ranges, put on the selected keyframes of their parameters (or
    // a keyframe at the position when they are keyframed; as the controls do). Previewed until `finished`,
    // then stored as one undo step. False when nothing changes.
    // `shift`: Pan and Scan's wire frame moves in all directions (p.237).
    bool dragHandle(const QString& id, const QPointF& from, const QPointF& to, bool finished, bool shift = false);

    // ---- Pan and Scan (p.230-238, p.564; docs/spec/pan-scan.md)
    // The Actions buttons, each one undo step; false when no Pan and Scan is shown (or, for Subdivide Effect,
    // nothing to split). Establish Origin keeps the Scaling and Position at the position (model::establishOrigin);
    // Reset to Origin puts the origin's values (or the Target's defaults) where the controls would; Subdivide
    // Effect splits the effect at the cuts of the selected video tracks below it (model::subdivideEffect).
    // Choosing a Target, also the one chosen, resets the wire frame to the Target's defaults (p.235).
    bool establishOrigin();
    bool resetToOrigin();
    bool subdivideEffect();
    bool chooseTarget(const QString& target);

    // ---- Avid Pan & Zoom (p.239-247, p.528-531; docs/spec/pan-zoom.md)
    // The Import Image button's file (its Other Options button, p.240): kept with the shown effect as an absolute
    // path, one undo step; false (and nothing done) when it is not an image or no Pan & Zoom is shown. "If you move
    // an image file after you associate it with the effect ... The next time you open the effect a dialog box
    // opens asking you to locate the file" (p.239): a file dialog named "panZoomLocate", not modal.
    bool importImage(const QString& path);

    // ---- Motion tracking (p.248-275; docs/spec/tracking.md)
    // Where tracking's frames come from: the picture tracked at Record frame `frame` for the effect `effect`, the
    // tracks below its track ("you almost always want to track the background") or, with `foreground`, its own
    // segment without the effect (p.262). MainWindow sets it; without it Start Tracking does nothing.
    using TrackingFrames = std::function<QImage(const model::EffectRef& effect, std::int64_t frame, bool foreground)>;
    void setTrackingFrames(TrackingFrames frames) { trackingFrames_ = std::move(frames); }
    // The shown effect's trackers and a parameter's mapping (empty when no effect is shown).
    model::TrackingSetup tracking() const;
    model::TrackerMapping trackerMapping(model::TrackedParameter parameter) const;
    // The Effect Editor's tracking controls, each one undo step, false when nothing changes: a parameter's enable
    // button (with `selector`, one of the Tracking category's selectors; p.257-258), which opens the Tracking
    // window ("Once you enable tracking for a parameter, the Tracking window opens"); a selector's Fast menu
    // (p.260); Position's Axis menu (p.275); Stabilize Tracking (p.577-578).
    bool setParameterTracking(model::TrackedParameter parameter, bool on, int selector = -1);
    bool mapTracker(model::TrackedParameter parameter, int selector, const QString& tracker);
    bool setTrackingAxis(const QString& axis);
    bool setStabilizeTracking(model::TrackedParameter parameter, bool on);
    // Changes the shown effect's trackers as one undo step; false when nothing changes.
    bool changeTracking(const QString& description, const std::function<void(model::TrackingSetup&)>& change);
    bool renameTracker(const QString& from, const QString& to);
    bool deleteTracker(const QString& name);
    // The Tracking window (p.253-255), made when first asked for; the Tracking Tool button shows it.
    TrackingWindow* trackingWindow();
    void showTrackingWindow();
    // The Tracking window's Point Range and Display menus (p.268-269): which data points the Effect Preview
    // shows, and whether it shows tracking data or only the effect's results.
    enum class PointRange { Current, InToOut, All };
    PointRange pointRange() const { return pointRange_; }
    void setPointRange(PointRange range);
    bool showsTrackingData() const { return showTrackingData_; }
    void setShowsTrackingData(bool show);
    // Start Tracking (p.264-265): each enabled tracker tracked from the position to the end of its region (or of
    // the effect) with the Tracking Engine chosen; at a region's first frame it "steps back one frame from the
    // region boundary". Data already there is replaced; one undo step at the end. The space bar, Ctrl+period
    // or a click stops it (stopTracking). False when nothing was tracked.
    bool startTracking();
    void stopTracking() { stopTracking_ = true; }
    bool isTracking() const { return tracking_; }
    // The effect's frame at the position (tracking data's frames), and moving the position to one.
    std::int64_t positionFrame() const;
    void moveToFrame(std::int64_t frame);

    // ---- Editing tracking data (p.265-274)
    // The selected data points of tracker `index` (by frame, in order). Clicking a point selects it alone,
    // Shift+click the range from the last one selected, a lasso the points inside it (Shift adds); double-clicking
    // also goes to its frame (p.271). The selection is shown circled in the Effect Preview and white on the tracker
    // timeline (p.267).
    QList<std::int64_t> selectedPoints(int index) const { return pointSelection_.value(index); }
    void selectPoints(int index, const QList<std::int64_t>& frames, bool extend = false);
    void clearPointSelection();
    void lassoPoints(const QRectF& area, bool extend);
    // Delete with points selected: onto the straight line between the points around them (p.272); one undo step.
    bool deleteSelectedPoints();
    // Stretch Points Mode (p.273): dragging a selected point stretches the selection instead of moving it.
    bool stretchMode() const { return stretchMode_; }
    void setStretchMode(bool on);
    // The tracker timeline's right-click commands at the position (p.266, p.269), one undo step each, and the
    // Go to Previous Region and Go to Next Region buttons (p.266); false when nothing changes.
    bool addTrackerRegion(int index);
    bool deleteTrackerRegion(int index);
    bool setReferenceFrame(int index);
    bool goToRegion(bool next);
    // ---- Stabilizing (p.275-279)
    // Auto-Zoom (the button in Stabilize's Position Tracking): Position and Scaling that remove the exposed edges,
    // kept up to date while the tracking data changes; a warning without data or when the picture moves too far
    // (objectName "autoZoomWarning"). Adjusting Position or Scaling by hand turns it off, with a message
    // ("autoZoomOff"). False when nothing changed.
    bool autoZoom();
    // AutoStabilize (p.277) on the Stabilize effect of `track`'s segment starting at `frame`: Point A enabled in
    // Position, the FluidStabilizer engine, SteadyGlide, tracking from the first frame (the space bar or a click
    // stops it, keeping the setup) and Auto-Zoom; one undo step. Opens the Tracking window.
    bool autoStabilize(const QString& track, std::int64_t frame);
    // Copy and Paste with the Tracking window active (p.283-284): the enabled trackers with their data, kept for the
    // session; pasted below the shown effect's trackers (one undo step). False when there is nothing to copy or
    // paste. `trackingWindowActive` says whether Copy and Paste are the Tracking window's.
    bool copyTrackers();
    bool pasteTrackers();
    bool hasCopiedTrackers() const;
    bool trackingWindowActive() const;
    // The Display Markers (p.268): the range In to Out shows, frames of the effect; the whole effect at first.
    std::pair<std::int64_t, std::int64_t> displayRange() const;
    void setDisplayRange(std::int64_t in, std::int64_t out);

    // ---- Nested effects (p.221-222; docs/spec/nesting.md)
    // "Nested effects on a segment are fully available in the Effect Editor": the shown segment effect's
    // nested effects (model::nestedEffects at the position), outermost first, each with a title row and
    // opener. One is open at a time (ours): its parameter groups and keyframes are shown under its row.
    // One name, or none, when nothing is nested.
    QStringList nestedEffects() const;
    int nestedLevel() const { return level_; }
    // Opens the nested effect at `level` (0: the segment's own).
    void setNestedLevel(int level);
    // Dragging a nested effect's title row onto another's (p.222): the order changes, one undo step. "If
    // you attempt a change in order that is not possible, a message box opens that explains why" (a
    // window named "nestedOrderRefused", not modal); false then.
    bool moveNestedEffect(int from, int to);

    // ---- Effect templates (p.52-54; docs/spec/effect-templates.md)
    // "Drag the effect icon from the Effect Editor to a bin": the template of the effect shown, with its
    // source media when `withSource` (Alt held, a segment effect only); nothing when blank.
    std::optional<model::EffectTemplate> shownTemplate(bool withSource) const;
    // What dragging the effect icon carries (effectTemplateMime); null when blank. The caller owns it.
    QMimeData* templateMime(bool withSource) const;
    // "drag it to the specific parameter group in the Effect Editor ... Media Composer applies the effect
    // template only to the effect parameter group you selected" (p.54): the group `category` of the shown
    // effect takes the template's values, one undo step. False when blank, when the template's effect has
    // no such group or nothing changes.
    bool applyTemplateGroup(const QString& category, const model::EffectTemplate& t);

signals:
    // An eyedropper started picking a colour from a Color Preview window (FX Guide p.89), for the Color Info tool.
    void colorPickingStarted();
    // The Effect Editor shortcut menu changed options().
    void optionsChanged();
    // The Render Effect button was clicked, with these keys held (FX Guide p.58, p.61).
    void renderRequested(Qt::KeyboardModifiers modifiers);
    // The Titler+ Dashboard's Save or Save As (FX Guide p.319-320; MainWindow keeps templates).
    void titleTemplateSaveRequested(bool saveAs);
    // The Edit Title button of a classic Title effect (FX Guide p.372): the segment to revise in the Title Tool.
    void editTitleRequested(const model::SegmentRef& segment);

protected:
    // Shift+Left and Shift+Right on a slider move it by ten; clicking a control makes its parameter
    // the active one.
    bool eventFilter(QObject* watched, QEvent* event) override;
    // Delete deletes the selected keyframes (p.105); Ctrl+Alt+C copies all values at the position.
    void keyPressEvent(QKeyEvent* event) override;
    // The keyframe tracks keep a share of the width.
    void resizeEvent(QResizeEvent* event) override;
    // The Effect Editor shortcut menu (p.129, p.131-132): the default Add Keyframe command, Show Add
    // Keyframe Mode Menu and Set Position To Keyframe.
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // Replaces the controls with those of `effect`'s parameter categories (none for null).
    void rebuild(const model::Effect* effect);
    // Sets the controls to the values stored in `effect`, without storing them again.
    void syncValues(const model::Effect& effect);
    // A Levels or Curves Enable button clicked in Color Correction mode: the correction's button (p.685).
    bool setCorrectionEnabled(const QString& path, bool on);
    // Enables and fills the Transition alignment and duration.
    void syncTransition();
    // Stores a parameter's value in the shown effect, as an undoable "Modify Effect".
    void setValue(const QString& category, const QString& parameter, const QJsonValue& value);
    // The cuts the Transition parameters apply to: the shown transition's, or in Trim mode the
    // video rollers when no effect is shown; empty otherwise.
    QList<model::TrimRoller> transitionCuts() const;
    // The video rollers in Trim mode; empty outside it.
    QList<model::TrimRoller> trimCuts() const;
    // `frames` as seconds:frames at the project's nominal rate.
    QString durationText(std::int64_t frames) const;
    // The Record monitor's sequence; an empty one when nothing is loaded.
    model::Sequence recordSequence() const;
    // The shown effect, with a drag previewed on it; null when none is shown.
    const model::Effect* shownEffect() const;
    // A parameter's value at time `t` of `effect`: its keyframes, its stored value, else its default.
    QJsonValue valueAt(const model::Effect& effect, const QString& parameter, double t) const;
    // The catalogue entry of "Category/Name"; null if unknown.
    const EffectParameter* parameterInfo(const QString& parameter) const;
    // Changes the shown effect's keyframes as one undo step; false if there is none or nothing changed.
    bool changeKeyframes(const QString& description, const std::function<bool(model::Effect&)>& change);
    // Moves the Record monitor's position to time `t` of the shown effect.
    void moveToTime(double t);
    // Shows or hides the tracks and graphs, and repaints them; then syncPreview().
    void updateTracks();
    // In Effect mode, gives the Composer's Effect Preview monitor the shown effect, its keyframes (the
    // effect track's indicators) and their selection; outside it, or with no effect shown, none.
    void syncPreview();
    // A keyframe indicator in the Effect Preview monitor's position bar was clicked (p.103).
    void previewKeyframePressed(double frame, bool extend);
    // In Color Correction mode, the markers selected below the Current monitor (Timeline::correctionKeyframesSelected):
    // every keyframe at those frames, frames into the segment.
    void selectCorrectionKeyframes(const QList<double>& frames);
    // Pops up the Add Keyframe Mode menu (or with `remove` the Delete one) for the Add Keyframe button.
    void addKeyframeButton(bool remove);

    friend class KeyframeTrack;

    Timeline& timeline_;
    std::optional<model::EffectRef> shown_;
    // Color Correction mode shows the current correction as an effect (model::correctionEffect), kept here; its changes
    // go to Timeline::setStoredCorrection.
    bool correctionShown_ = false;
    model::Effect correctionEffect_;
    // The Levels and Curves rows' Enable buttons, by the correction path they turn on and off (6.10d3c).
    QHash<QString, QCheckBox*> correctionEnables_;
    QString shownName_;           // "category/name" of the effect shown, to notice a replacement
    bool transitionOnly_ = false; // only the Transition parameters are shown (Trim mode)
    QWidget* body_ = nullptr;
    QVBoxLayout* groups_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* titleIcon_ = nullptr;
    QToolButton* promote3D_ = nullptr;
    // Direct manipulation.
    QHash<int, QToolButton*> handleButtons_; // media::HandleTool -> its button
    media::HandleTool handleTool_ = media::HandleTool::None;
    QToolButton* outline_ = nullptr;
    bool outlineShown_ = false;
    std::optional<model::Effect> handleStart_; // the effect as it was when a handle drag began
    // Motion tracking.
    TrackingFrames trackingFrames_;
    TrackingWindow* trackingWindow_ = nullptr;
    TitlerTool* titler_ = nullptr;
    IntraframeTool* intraframe_ = nullptr;
    std::optional<model::Effect> wholeIntraframe_;     // the shown Intraframe effect, all objects
    std::optional<model::Effect> intraframePreview_;   // a drag's effect before its undo step
    QStringList intraframeSelection_;
    QHash<int, QToolButton*> intraframeButtons_;       // IntraframeTool::Tool -> its button
    QToolButton* bringForward_ = nullptr;
    QToolButton* sendBackward_ = nullptr;
    QToolButton* objectMenu_ = nullptr;
    QPointer<BrushPreview> brushPreview_; // in the Brush category, rebuilt with the controls
    bool filmProject_ = false;
    bool interlacedProject_ = false;
    // The Brush category's Shape buttons, Brush Template icon and Brush Preview window.
    QWidget* brushPanel(QWidget* parent);
    // Shows the drawing tool buttons for the shown effect and checks the active one.
    void syncIntraframeButtons();
    TitlerDashboard* titlerDashboard_ = nullptr;
    TitlerPanel* titlerPanel_ = nullptr;
    PointRange pointRange_ = PointRange::All; // "All ... This is the default option" (p.268)
    bool showTrackingData_ = true;
    bool tracking_ = false;
    bool stopTracking_ = false;
    QHash<QString, QWidget*> trackingControls_; // "tracking:<Parameter>[:T<n>|:Axis|:Stabilize]" -> control
    QHash<int, QList<std::int64_t>> pointSelection_; // tracker index -> selected frames
    std::optional<std::pair<int, std::int64_t>> lastPoint_; // the point last clicked, for Shift+click ranges
    bool stretchMode_ = false;
    std::optional<std::pair<std::int64_t, std::int64_t>> displayRange_;
    // A data point's click or drag (dragHandle's "trackpoint:<n>:<frame>").
    bool dragTrackPoint(const QString& id, const QPointF& from, const QPointF& to, bool finished, bool shift);
    // Runs Start Tracking on `edited` (startTracking's work, without the undo step); true when it tracked anything.
    bool runTracking(model::Effect& edited);
    // After `before` became `after` in an undo step: a Stabilize effect with Auto-Zoom follows new tracking data, and
    // Auto-Zoom goes off when Position or Scaling changed by hand (`autoZoomOff_` then says to tell).
    void keepAutoZoom(const model::Effect& before, model::Effect& after);
    bool autoZooming_ = false;
    bool autoZoomOff_ = false;
    double frameAspect() const;
    // The frames of a tracker's data points the Point Range menu shows (p.268).
    QList<std::int64_t> shownPointFrames(const model::Tracker& tracker) const;
    void selectionChanged();
    // The tracking controls of one parameter in the editor (enable, selectors, Axis), and keeping them in step.
    QWidget* trackingControls(model::TrackedParameter parameter, QWidget* parent);
    void syncTracking(const model::Effect& effect);
    // Changes the shown effect as one undo step (`change` returns whether it changed anything).
    bool changeEffectParameters(const QString& description, const std::function<bool(model::Effect&)>& change);
    // The tracker overlay of the Effect Preview (fillOverlay).
    void fillTrackers(const model::Effect& effect, Composer::EffectPreview& preview) const;
    // A tracking box drag (dragHandle's "track:<n>:..." handles).
    bool dragTrackerBox(const QString& id, const QPointF& from, const QPointF& to, bool finished);
    // Shows the handle buttons for a 3D effect only, and Outline/Path for an effect with an outline.
    void syncHandleButtons();
    // The Effect Preview overlay of `effect` at the position (syncPreview()).
    void fillOverlay(const model::Effect& effect, Composer::EffectPreview& preview) const;
    // Puts a value on `effect` as a control would: on the parameter's selected keyframes, a keyframe at the
    // position when it has keyframes but none is selected, else the plain value.
    void putValue(model::Effect& effect, const QString& category, const QString& parameter, const QJsonValue& value) const;
    // The nested effects at the position (model::nestedEffects), the one open, and the position's offset into
    // the outer segment, from the last refresh().
    QList<model::NestedEffect> chain_;
    int level_ = 0;
    std::int64_t nestOffset_ = 0;
    QPoint nestedPress_; // where a drag of a nested effect's title row began
    int nestedPressLevel_ = -1;
    // Changes the shown effect (the open nested one) as one undo step: Timeline::changeEffect or
    // changeNestedEffect.
    bool changeShown(const QString& description, const std::function<void(model::Effect&)>& change);
    QPoint iconPress_; // where a drag of the effect icon began; null when none
    QLabel* blank_ = nullptr;
    QToolButton* alignment_ = nullptr;
    QLineEdit* duration_ = nullptr;
    QStringList categoryOrder_;              // the categories shown, in order
    QHash<QString, QWidget*> groupBodies_;   // category -> its controls
    QHash<QString, QToolButton*> openers_;   // category -> its opener
    QHash<QString, QWidget*> controls_;      // "category/parameter" -> control
    QHash<QString, QWidget*> burnInControls_; // "Display 1/Reader", "Notes/Text", ... (Timecode Burn-In)
    QWidget* burnInPanel(QWidget* parent);
    void syncBurnIn(const model::Effect& effect);
    QHash<QString, QLabel*> valueLabels_;    // "category/parameter" -> value
    model::TransitionAlignment shownAlignment_ = model::TransitionAlignment::CenteredOnCut;
    bool syncing_ = false; // controls are being set from the model; their changes are not stored

    // Keyframes.
    bool graphsShown_ = false;
    QToolButton* graphsButton_ = nullptr;
    QToolButton* addKeyframe_ = nullptr;
    QToolButton* prevEvent_ = nullptr;
    QToolButton* nextEvent_ = nullptr;
    QHash<QString, QWidget*> tracks_;   // keyframeTrack()'s names -> the track or graph
    QHash<QString, QToolButton*> graphOpeners_; // parameter -> its graph's opener
    QSet<QString> openGraphs_;
    QString activeParameter_;
    QList<KeyframeId> selection_;
    KeyframeMode mode_ = KeyframeMode::OpenGroups; // ours: the guide's example (p.107)
    bool showModeMenu_ = true;
    bool positionToKeyframe_ = true;
    model::EffectSpan span_;
    std::int64_t effectStart_ = 0; // the Record frame of the shown effect's first frame
    std::optional<model::Effect> current_; // the shown effect as stored, from the last refresh()
    std::optional<model::Effect> preview_; // the shown effect with a drag not yet stored
    QString applyTo_; // the track whose shortcut menu is open, which shows "Apply to ..."

    // Graph views.
    double viewStart_ = 0.0;
    double viewSpan_ = 1.0;
    QSlider* hScale_ = nullptr;
    QScrollBar* hScroll_ = nullptr;
    QHash<QString, std::pair<double, double>> graphRanges_; // set by zooming; else the parameter's range
    QHash<QString, std::pair<double, double>> graphZoomBase_; // the range the vertical scale bar zooms from
    // Keeps the scroll thumbwheel's range and value with the view.
    void syncScrollBar();

    // Copied keyframes: a parameter, the frames after the leftmost copied keyframe, and the value.
    struct CopiedKeyframe {
        QString parameter;
        double frames = 0.0;
        QJsonValue value;
    };
    QList<CopiedKeyframe> clipboard_;
    bool copiedOne_ = false; // one keyframe copied: its value pastes onto selected keyframes

    HandleMode handleMode_ = HandleMode::Symmetrical;
    bool indentRows_ = true;
    bool largeText_ = false;
    bool updateWhilePlaying_ = false;
    // Applies Indent Rows and Large Text to the controls built.
    void applyLook();
};

// One keyframe track or graph of the Effect Editor (FX Guide p.95-97): the effect track (gray), a
// parameter group track (darker blue), a parameter track (lighter blue) or an open keyframe graph
// (very dark grey; darker when its parameter is the active one). Time runs across the effect from its
// first frame to its last; a graph's height is its parameter's range. Made and owned by EffectEditor,
// which it asks for everything it shows and changes.
class KeyframeTrack : public QWidget {
public:
    enum class Kind { Effect, Group, Parameter, Graph };
    // `which` as EffectEditor::keyframeTrack() names it; empty for the track of a parameter that is not
    // keyframeable, which only moves the position (p.96).
    KeyframeTrack(EffectEditor& editor, Kind kind, const QString& which, QWidget* parent);

    Kind kind() const { return kind_; }
    const QString& which() const { return which_; }
    // The x of time `t` and the time at `x`; a graph's y of a value and the value at `y`.
    double xFor(double t) const;
    double timeAt(double x) const;
    double yFor(double value) const;
    double valueAt(double y) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    // Ctrl+Alt+C in a graph copies all values at the position (p.118).
    void keyPressEvent(QKeyEvent* event) override;

private:
    // The keyframes shown, by time: one per keyframe in a parameter track or graph, and in a group or
    // effect track one indicator for all the keyframes at a time (p.102).
    struct Column {
        double t = 0.0;
        double value = 0.0; // a graph's keyframe value
        bool fixed = false;
        QList<EffectEditor::KeyframeId> keys;
    };
    QList<Column> columns() const;
    // The column under a point, or -1.
    int columnAt(const QPointF& point, const QList<Column>& columns) const;
    // The graph's parameter's range.
    std::pair<double, double> range() const;

    EffectEditor& editor_;
    Kind kind_;
    QString which_;
    // A press on a keyframe: the keyframe dragged and where the press was; on empty track the position
    // indicator is dragged instead.
    std::optional<EffectEditor::KeyframeId> pressed_;
    QPointF pressPoint_;
    bool dragging_ = false;
    bool movingPosition_ = false;
    // Alt+drag on a graph away from keyframes scrolls it (p.101); its bottom border resizes it (p.98).
    bool scrolling_ = false;
    bool resizing_ = false;
    // A Bézier direction handle being dragged: its keyframe and side (true: the way out), and both
    // handles at the press.
    std::optional<EffectEditor::KeyframeId> handleKey_;
    bool handleOut_ = false;
    model::BezierHandle pressIn_;
    model::BezierHandle pressOut_;
    // The selected Bézier keyframes of a graph and their handles' places, for drawing and hitting.
    struct HandlePoints {
        EffectEditor::KeyframeId key;
        QPointF at, in, out;
    };
    QList<HandlePoints> handlePoints() const;
};

} // namespace lumenis::ui
