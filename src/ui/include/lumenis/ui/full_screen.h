#pragma once

#include <QString>
#include <QWidget>

namespace lumenis::ui {

class Composer;

// Composer > Full Screen Playback (Editing Guide p.391): the active
// monitor's picture on a whole screen, the second one when there is one.
// Shift+Ctrl+F (Shift+Command+F) or Esc leaves it.
class FullScreenView : public QWidget {
    Q_OBJECT

public:
    // Full Screen Playback settings, Reformat (p.1190).
    enum class Reformat { Stretch, Letterbox, Crop, KeepSize, RawPixel };
    static Reformat reformatFrom(const QString& value);

    explicit FullScreenView(Composer& composer, QWidget* parent = nullptr);

    void setReformat(Reformat reformat);
    Reformat reformat() const { return reformat_; }
    // "Full Screen" (p.1191): off, the picture is shown with as little
    // scaling as possible.
    void setFullScreenScaling(bool on);
    // Where the picture goes in a widget of `area`, for an image of
    // `image` size.
    QRect pictureRect(QSize area, QSize image) const;

    // Shows it full screen, on another screen than `near`'s if there is one.
    void open(QWidget* near);

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    Composer& composer_;
    Reformat reformat_ = Reformat::Letterbox;
    bool fullScreen_ = true;
};

} // namespace lumenis::ui
