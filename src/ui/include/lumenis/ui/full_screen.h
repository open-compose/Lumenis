#pragma once

#include "lumenis/model/mask_margins.h"
#include <QString>
#include <QWidget>

namespace lumenis::ui {

class Composer;

// Composer > Full Screen Playback (Editing Guide p.391): the active
// monitor's picture on a whole screen, the second one when there is one.
// Shift+Ctrl+F (Shift+Command+F) or Esc leaves it.
//
// A top-level window owned by MainWindow, which makes it on first use and applies the Full Screen
// Playback settings to it. It draws the Composer's active monitor and repaints on Composer::changed;
// other keys go to the Composer, so playback keys keep working.
class FullScreenView : public QWidget {
    Q_OBJECT

public:
    // Full Screen Playback settings, Reformat (p.1190).
    enum class Reformat { Stretch, Letterbox, Crop, KeepSize, RawPixel };
    // The setting's stored value ("stretch", "crop", "keepSize", "rawPixel"); anything else is
    // Letterbox.
    static Reformat reformatFrom(const QString& value);

    // Follows `composer`, which must outlive it.
    explicit FullScreenView(Composer& composer, QWidget* parent = nullptr);

    // How a picture of another size than the screen is fitted; repaints.
    void setReformat(Reformat reformat);
    Reformat reformat() const { return reformat_; }
    // "Full Screen" (p.1191): off, the picture is shown with as little
    // scaling as possible.
    void setFullScreenScaling(bool on);
    // The Full Screen Playback setting's Target Mask (p.1191; 8.5e3) over the Composer's mask margins.
    void setTargetMask(model::TargetMask mask);
    // Where the picture goes in a widget of `area`, for an image of
    // `image` size, in widget pixels. Empty for an empty image.
    QRect pictureRect(QSize area, QSize image) const;

    // Shows it full screen, on another screen than `near`'s if there is one.
    void open(QWidget* near);

signals:
    // The window was closed, by the keys or otherwise.
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    Composer& composer_;
    Reformat reformat_ = Reformat::Letterbox;
    model::TargetMask targetMask_ = model::TargetMask::None;
    bool fullScreen_ = true;
};

} // namespace lumenis::ui
