#pragma once

#include "lumenis/model/frame_flex.h"

#include <QDialog>
#include <QImage>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

// The Source Settings dialog's FrameFlex tab (Editing Guide p.527-535; 8.5b1, docs/spec/frameflex.md): the framing box of
// the selected clips edited with fields and in the top viewer, the result in the bottom viewer, and the Reformat
// attribute. MainWindow opens it from the bin's, the Timeline's and the Source monitor's right-click Source Settings
// (`bin.source_settings`) and stores what Apply gives back on every selected clip.
namespace lumenis::ui {

// The top viewer (p.529-532): the clip's whole frame with the framing box over it. Dragging inside the box moves it, a
// corner resizes it proportionally, and the handle above its top edge rotates it ("you can use the center handlebar in
// the top viewer to rotate the image", p.532).
class FramingViewer : public QWidget {
    Q_OBJECT
public:
    explicit FramingViewer(QWidget* parent = nullptr);
    void setFrame(const QImage& frame);
    void setFrameFlex(const model::FrameFlex& flex);
    const model::FrameFlex& frameFlex() const { return flex_; }
    // Where a point of the frame (its pixels) is in the widget, for tests.
    QPointF toWidget(QPointF framePoint) const;
    QSize sizeHint() const override { return {360, 200}; }

signals:
    void frameFlexDragged(const model::FrameFlex& flex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QRectF imageRect() const;
    QPointF toFrame(QPointF widgetPoint) const;
    QPointF rotationHandle() const; // widget pixels
    enum class Drag { None, Move, Resize, Rotate };
    QImage frame_;
    model::FrameFlex flex_;
    Drag drag_ = Drag::None;
    QPointF pressFrame_;
    model::FrameFlex pressFlex_;
};

class SourceSettingsDialog : public QDialog {
    Q_OBJECT
public:
    // `frame` is the first selected clip's frame at its own size; `clipCount` how many clips Apply sets.
    SourceSettingsDialog(const QString& clipName, const QImage& frame, QSize projectRaster, const model::SpatialAdapter& adapter,
                         int clipCount, QWidget* parent = nullptr);

    // The settings as the fields show them.
    model::SpatialAdapter adapter() const;
    void setAdapter(const model::SpatialAdapter& adapter);
    // "Show Viewers" (p.529).
    void setViewersShown(bool shown);
    bool viewersShown() const;
    FramingViewer* framingViewer() const { return framing_; }
    // The bottom viewer's picture: the framed area "as it would appear within the actual project frame" (p.531).
    QImage resultImage() const;

    // The buttons (p.530-532): Size matches project raster, Image Size = raster dimension, Rotate left and right 90
    // degrees, Reset, Revert ("Reverses any changes you made since the last time the Apply button was clicked") and
    // Apply ("Applies all selections ... so that you can see the changes in the viewers"; the clips take them).
    void sizeMatchesRaster();
    void imageSizeToRaster();
    void rotate(double degrees);
    void reset();
    void revert();
    void apply();

    // The Playback Rate tab (p.545): the clip's original rate and the project's as the menu shows them, which one it plays
    // at, and whether it can change (with why not).
    void setPlaybackRates(const QString& original, const QString& project, bool atProjectRate, bool allowed,
                          const QString& whyNot = {});
    bool playbackAtProjectRate() const;

    // Apply's settings for the clips (the adapter and whether they play at the project's rate); OK applies first.
    using Applier = std::function<void(const model::SpatialAdapter&, bool atProjectRate)>;
    void setApplier(Applier applier) { applier_ = std::move(applier); }

private:
    void fill(const model::SpatialAdapter& adapter);
    void refresh();
    QImage frame_;
    QSize raster_;
    model::SpatialAdapter applied_;
    Applier applier_;
    bool appliedRate_ = false;
    QComboBox* playbackRate_ = nullptr;
    QLabel* playbackNote_ = nullptr;
    bool filling_ = false;
    QCheckBox* showViewers_ = nullptr;
    QWidget* viewers_ = nullptr;
    FramingViewer* framing_ = nullptr;
    QLabel* result_ = nullptr;
    QLabel* rasterLabel_ = nullptr;
    QSpinBox* imageWidth_ = nullptr;
    QSpinBox* imageHeight_ = nullptr;
    QDoubleSpinBox* imageAspect_ = nullptr;
    QDoubleSpinBox* pixelAspect_ = nullptr;
    QDoubleSpinBox* frameAspect_ = nullptr;
    QDoubleSpinBox* x_ = nullptr;
    QDoubleSpinBox* y_ = nullptr;
    QDoubleSpinBox* size_ = nullptr;
    QDoubleSpinBox* rotation_ = nullptr;
    QComboBox* color_ = nullptr;
    QComboBox* reformat_ = nullptr;
};

} // namespace lumenis::ui
