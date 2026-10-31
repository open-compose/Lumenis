#pragma once

#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"

#include <QWidget>

#include <cstdint>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QMimeData;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace lumenis::ui {

class Timeline;

// A control graph of the Motion Effect Editor (FX Guide p.180, p.187-190): the Speed graph (percent
// speed over output frames) or the Position graph (input position, source frames from the segment's
// source IN, over output frames), drawn as a line with the position indicator. The active graph shows
// its keyframes, which are selected by clicking (pink when active, p.187) and dragged up and down to
// change their value, or sideways with Alt ("Alt+dragging ... keyframes horizontally", p.188); a click
// elsewhere moves the position. The inactive graph shows what the active one makes ("the Motion Effect
// Editor represents changes in the inactive graph as well", p.176).
class MotionGraph : public QWidget {
    Q_OBJECT

public:
    enum class Kind { Speed, Position };
    explicit MotionGraph(Kind kind, QWidget* parent = nullptr);

    // What to draw: the Timewarp, its segment's length, the position (frames into it; -1 for none),
    // whether this graph is the active one, and the keyframe selected (-1 for none).
    void setData(const std::optional<model::Effect>& effect, std::int64_t length, std::int64_t position, bool active, int selected);
    Kind kind() const { return kind_; }
    // The value drawn at each output frame.
    const QList<double>& values() const { return values_; }
    // Where a keyframe or frame is drawn, and the value at a height (tests and the mouse).
    QPointF pointFor(double frame, double value) const;
    double valueAt(double y) const;
    double frameAt(double x) const;

signals:
    void positionPicked(std::int64_t offset);
    void keyframeSelected(int index);
    // A keyframe dragged to `time` (the effect's time; nothing when only its value changes) and `value`;
    // `finished` on release, when the change is to be kept.
    void keyframeDragged(int index, std::optional<double> time, double value, bool finished);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    QSize sizeHint() const override { return {320, 120}; }

private:
    QRectF plot() const;
    int keyframeNear(const QPointF& at) const;

    Kind kind_;
    std::optional<model::Effect> effect_;
    std::int64_t length_ = 1;
    std::int64_t position_ = -1;
    bool active_ = false;
    int selected_ = -1;
    QList<double> values_;
    QList<QPointF> keys_; // (frame, value) of the active graph's keyframes
    double low_ = 0.0, high_ = 100.0;
    int dragging_ = -1;
    bool dragTime_ = false;
};

// The Motion Effect Editor (FX Guide p.178-189; ROADMAP 6.5c; docs/spec/motion-effects.md): the motion
// effect of the segment at the Record monitor's position, on the topmost selected video track that has
// one there (all video tracks when none is selected). "The initial appearance of the Motion Effect
// Editor depends on whether there is no motion effect, a Timewarp effect, or a traditional motion
// effect in the segment at the position indicator" (p.182): blank, the Timewarp's controls, or the
// traditional effect's speed, strobe and duration with the Promote button.
//
// A panel's contents ("MotionEffectEditor"), made by MainWindow, which also answers its toolbar's
// requests (Render Effect, Remove Effect, Match Frame, Play Loop, Play, Trim Mode). Changes are undoable
// edits of the sequence through the Timeline.
class MotionEffectEditor : public QWidget {
    Q_OBJECT

public:
    // Adapter: a mixed rate clip's Motion Adapter, "with the Type list and the Promote button active, and
    // other parameters inactive" (Editing Guide p.471).
    enum class Shown { None, Timewarp, Traditional, Adapter };
    explicit MotionEffectEditor(Timeline& timeline, QWidget* parent = nullptr);
    // Drags the Timewarp icon (timewarpTemplateMime).
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Follows the Record monitor's sequence and position.
    void refresh();
    Shown shown() const { return shownKind_; }
    // The Render setting's Timewarps Render Using ("original" or a Type's name, model::timewarpTypeName):
    // other than Original Preference it fixes the Type unless Ignore Render Setting is on (p.183).
    void setRenderUsing(const QString& type);
    // The project's timecode rate (TimecodeFormat::nominalFps), scan and whether it is a Film project: what
    // the Source and Output menus offer (model::sourceFormats, outputFormats, FX Guide p.183, p.205).
    void setProjectFormat(int nominalFps, bool interlaced, bool film);

    // The controls (tests).
    MotionGraph* speedGraph() const { return speedGraph_; }
    MotionGraph* positionGraph() const { return positionGraph_; }
    QToolButton* speedDisplayButton() const { return speedButton_; }
    QToolButton* positionDisplayButton() const { return positionButton_; }
    QComboBox* editGraphMenu() const { return editGraph_; }
    QComboBox* typeMenu() const { return type_; }
    QCheckBox* ignoreRenderSettingBox() const { return ignore_; }
    QDoubleSpinBox* anchorField() const { return anchor_; }
    QSpinBox* strobeField() const { return strobe_; }
    QComboBox* sourceMenu() const { return source_; }
    QComboBox* outputMenu() const { return outputFormat_; }
    QCheckBox* adaptiveDeinterlaceBox() const { return adaptive_; }
    QCheckBox* autoPatternBox() const { return autoPattern_; }
    QComboBox* pulldownPhaseMenu() const { return phase_; }
    QSpinBox* repeatCountField() const { return repeat_; }
    QLabel* durationLabel() const { return duration_; }
    QLabel* speedLabel() const { return speedText_; }
    QPushButton* promoteButton() const { return promote_; }
    QDoubleSpinBox* valueField() const { return value_; }
    QSpinBox* outputField() const { return output_; }
    int selectedKeyframe() const { return selected_; }
    // "Timewarp Effect icon: Drag the icon to a bin to create a Timewarp Effect template from the current
    // settings in the Speed graph and Position graph" (p.182; docs/spec/effect-templates.md). The icon, on
    // only while a Timewarp is shown, and what dragging it carries (effectTemplateMime; null without a
    // Timewarp, owned by the caller).
    QLabel* timewarpIcon() const { return timewarpIcon_; }
    QMimeData* timewarpTemplateMime() const;

    // The active graph's toolbar (p.184-185): "Creates a keyframe at the current position. Alt+click
    // ... to delete a keyframe"; Go to Previous/Next Event; the Fast menu's interpolations (p.120).
    void addKeyframe(bool remove = false);
    void goToEvent(bool next);
    void setInterpolation(model::Interpolation interpolation);
    void selectKeyframe(int index);
    // "Click to set the Source Anchor Frame at the active keyframe" (p.183).
    void anchorAtActiveKeyframe();
    // "Click to promote a traditional motion effect to a Timewarp effect" (p.183); a Motion Adapter too
    // (Editing Guide p.472).
    void promote();
    // Source menu: the format chosen, the Output kept when it is still offered (else the first that is),
    // and with Auto the pulldown phase or repeat count found in the clip (media::detectPulldownPhase,
    // detectRepeatCount).
    void setSource(const QString& format);

signals:
    void renderRequested();
    void removeRequested();
    void matchFrameRequested();
    void playLoopRequested(std::int64_t start, std::int64_t end);
    void playRequested();
    void trimRequested();

private:
    // The segment shown: its track, index and first Record frame.
    struct Target {
        QString track;
        int index = -1;
        std::int64_t start = 0;
    };
    std::optional<Target> findTarget(const model::Sequence& seq, std::int64_t position) const;
    // Changes the shown segment's Timewarp as one undoable edit.
    void editTimewarp(const QString& description, const std::function<void(model::Effect&, std::int64_t length)>& change);
    const model::Segment* targetSegment(const model::Sequence& seq) const;
    QString activeCategory() const;
    QString activeParameter() const;
    void syncControls();
    // The format controls dimmed and hidden, for anything but a Timewarp.
    void hideFormats();

    Timeline& timeline_;
    std::optional<Target> target_;
    std::optional<model::Effect> effect_; // the Timewarp shown
    std::int64_t length_ = 1;
    std::int64_t position_ = 0; // frames into the segment
    Shown shownKind_ = Shown::None;
    int selected_ = -1;
    bool syncing_ = false;
    QString renderUsing_ = QStringLiteral("original");

    QLabel* blank_ = nullptr;
    QLabel* timewarpIcon_ = nullptr;
    QPoint iconPress_; // where a drag of the Timewarp icon began; null when none
    QWidget* body_ = nullptr;
    QToolButton* speedButton_ = nullptr;
    QToolButton* positionButton_ = nullptr;
    QComboBox* editGraph_ = nullptr;
    QComboBox* type_ = nullptr;
    QCheckBox* ignore_ = nullptr;
    QComboBox* source_ = nullptr;
    QComboBox* outputFormat_ = nullptr;
    QCheckBox* adaptive_ = nullptr;
    QCheckBox* autoPattern_ = nullptr;
    QComboBox* phase_ = nullptr;
    QSpinBox* repeat_ = nullptr;
    int nominalFps_ = 30;
    bool interlaced_ = false;
    bool film_ = false;
    QDoubleSpinBox* anchor_ = nullptr;
    QToolButton* anchorButton_ = nullptr;
    QSpinBox* strobe_ = nullptr;
    QLabel* duration_ = nullptr;
    QLabel* speedText_ = nullptr;
    QPushButton* promote_ = nullptr;
    MotionGraph* speedGraph_ = nullptr;
    MotionGraph* positionGraph_ = nullptr;
    QWidget* graphTools_ = nullptr;
    QDoubleSpinBox* value_ = nullptr;
    QSpinBox* output_ = nullptr;
};

} // namespace lumenis::ui
