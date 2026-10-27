#pragma once

#include "lumenis/model/sequence.h"
#include "lumenis/model/surround_pan.h"

#include <QDialog>
#include <QList>
#include <QWidget>

class QDial;
class QLabel;
class QSlider;
class QToolButton;

namespace lumenis::ui {

// The Pan grid (Editing Guide p.705-706, picture on p.705; docs/spec/surround.md): the listening room
// seen from above, front at the top, with the speakers of a 5.1 or 7.1 sequence around it and the Pan
// Location cursor. Dragging the cursor pans in the X/Y grid (front and rear positions together);
// double-clicking a speaker snaps the cursor to it (p.707). The Advanced Panner's large grid also draws
// the trajectory from the front to the rear position. The grid only shows and reports; the caller
// stores the pan.
class PanGrid : public QWidget {
    Q_OBJECT

public:
    explicit PanGrid(bool large, QWidget* parent = nullptr);

    // The sequence's mix format: 5.1 or 7.1 (7.1 adds the side speakers).
    void setFormat(model::AudioFormat format);
    model::AudioFormat format() const { return format_; }
    // Shows a pan without signalling.
    void setPan(const model::SurroundPan& pan);
    const model::SurroundPan& pan() const { return pan_; }
    // The speakers a cursor can snap to, with their grid positions (x -100..100, y -100 rear .. 100
    // front): L C R, Lss Rss (7.1), Lsr Rsr.
    struct SpeakerSpot {
        QString name;
        int x;
        int y;
    };
    QList<SpeakerSpot> speakers() const;
    // The widget point of a grid position, and back.
    QPointF pointFor(double x, double y) const;
    std::pair<double, double> gridAt(const QPointF& point) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    // The cursor was dragged (`done` once the drag ends) or snapped to a speaker (done).
    void panMoved(const model::SurroundPan& pan, bool done);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QRectF area() const; // the grid inside its border, where the speakers sit on the edges
    void dragTo(const QPointF& point, bool done);

    bool large_;
    model::AudioFormat format_ = model::AudioFormat::Surround51;
    model::SurroundPan pan_;
    bool dragging_ = false;
};

// The Advanced Panner (p.705-710): for one mono or stereo track, a large X/Y grid for each channel, the
// Front, Rear and F/R Position controls and the Center and (7.1) Side percentages with their values, an
// LFE slider (0-100), and for stereo tracks the Stereo Link and Stereo Mirror buttons. Close returns to
// the mixer. Like the grid it only shows and reports.
class AdvancedPanner : public QDialog {
    Q_OBJECT

public:
    AdvancedPanner(const QString& track, model::AudioFormat sequenceFormat, const QList<model::SurroundPan>& pans,
                   QWidget* parent = nullptr);

    const QString& track() const { return track_; }
    int channels() const { return static_cast<int>(pans_.size()); }
    const QList<model::SurroundPan>& pans() const { return pans_; }
    // Shows pans without signalling (the segment under the position changed).
    void setPans(const QList<model::SurroundPan>& pans);
    // The channel the controls below the grids act on (stereo tracks: 0 left, 1 right).
    int currentChannel() const { return current_; }
    void setCurrentChannel(int channel);
    model::StereoLink link() const { return link_; }
    void setLink(model::StereoLink link);

    PanGrid* grid(int channel) const { return grids_.value(channel); }
    QDial* frontDial() const { return front_; }
    QDial* rearDial() const { return rear_; }
    QDial* frontRearDial() const { return frontRear_; }
    QDial* centerDial() const { return center_; }
    QDial* sideDial() const { return side_; }
    QSlider* lfeSlider() const { return lfe_; }
    QToolButton* linkButton() const { return linkButton_; }
    QToolButton* mirrorButton() const { return mirrorButton_; }
    // Sets channel `channel` as the controls would, moving the other channel as the link says, and
    // reports it.
    void movePan(int channel, const model::SurroundPan& pan, bool done);

signals:
    // A channel's pan changed; for a linked stereo track `other` is the other channel's new pan.
    void panChanged(int channel, const model::SurroundPan& pan, const std::optional<model::SurroundPan>& other, bool done);

private:
    // Shows the current channel's values on the controls.
    void syncControls();

    QString track_;
    model::AudioFormat format_;
    QList<model::SurroundPan> pans_;
    QList<PanGrid*> grids_;
    int current_ = 0;
    model::StereoLink link_ = model::StereoLink::Unlinked;
    QDial* front_ = nullptr;
    QDial* rear_ = nullptr;
    QDial* frontRear_ = nullptr;
    QDial* center_ = nullptr;
    QDial* side_ = nullptr;
    QSlider* lfe_ = nullptr;
    QList<QLabel*> values_; // front, rear, f/r, center, side
    QLabel* lfeValue_ = nullptr;
    QToolButton* linkButton_ = nullptr;
    QToolButton* mirrorButton_ = nullptr;
    bool syncing_ = false;
};

} // namespace lumenis::ui
