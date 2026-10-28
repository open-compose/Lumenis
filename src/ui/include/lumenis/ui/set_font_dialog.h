#pragma once

#include <QDialog>

class QFontComboBox;
class QSpinBox;

namespace lumenis::ui {

// Windows > Set Font's dialog (Editing Guide p.81-82; docs/spec/set-font.md): "Click the Font menu,
// and select a font. ... Type another point size for the font in the Size text box." MainWindow applies
// the answer to the active Bin, Composer or Timeline.
class SetFontDialog : public QDialog {
    Q_OBJECT

public:
    SetFontDialog(const QFont& current, QWidget* parent = nullptr);

    QFontComboBox* fontBox() const { return font_; }
    QSpinBox* sizeBox() const { return size_; }
    QString family() const;
    int pointSize() const;

private:
    QFontComboBox* font_ = nullptr;
    QSpinBox* size_ = nullptr;
};

} // namespace lumenis::ui
