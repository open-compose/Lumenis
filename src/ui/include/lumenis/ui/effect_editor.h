#pragma once

#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/effect_template.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/sequence.h"

#include <QHash>
#include <QSet>
#include <QJsonValue>
#include <QWidget>

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
    int min = 0;   // a slider's range; min <= max
    int max = 100;
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
class EffectEditor : public QWidget {
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
    // The active parameter (the control last used, p.107); empty if none.
    QString activeParameter() const { return activeParameter_; }
    void setActiveParameter(const QString& parameter);
    // The shown effect's time at the Record monitor's position, and how its frames map to time.
    double positionTime() const;
    const model::EffectSpan& span() const { return span_; }

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
    // The Effect Editor shortcut menu changed options().
    void optionsChanged();
    // The Render Effect button was clicked, with these keys held (FX Guide p.58, p.61).
    void renderRequested(Qt::KeyboardModifiers modifiers);

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
    // Pops up the Add Keyframe Mode menu (or with `remove` the Delete one) for the Add Keyframe button.
    void addKeyframeButton(bool remove);

    friend class KeyframeTrack;

    Timeline& timeline_;
    std::optional<model::EffectRef> shown_;
    QString shownName_;           // "category/name" of the effect shown, to notice a replacement
    bool transitionOnly_ = false; // only the Transition parameters are shown (Trim mode)
    QWidget* body_ = nullptr;
    QVBoxLayout* groups_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* titleIcon_ = nullptr;
    QToolButton* promote3D_ = nullptr;
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
