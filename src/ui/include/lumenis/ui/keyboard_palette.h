#pragma once

#include "lumenis/ui/button_mapping.h"

#include <QKeySequence>
#include <QWidget>

class QRadioButton;

namespace lumenis::ui {

class KeyboardMap;

// A key of the Keyboard palette: the command it runs, with the key's name
// under it.
class KeyButton : public MappableButton {
    Q_OBJECT

public:
    KeyButton(const QString& keyText, const QString& label, QWidget* parent = nullptr);
    // The key in Qt's portable text ("I", "F5", "Space").
    const QString& keyText() const { return key_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString key_;
    QString label_;
};

// The Keyboard setting (guide p.1202): a keyboard whose keys are
// user-selectable buttons. Buttons are dragged onto keys from the Command
// Palette (p.98), and the modifier key buttons give a key its Alt (Option,
// Control) function (p.97). Holding Shift shows the Shift layer. Changes
// are saved in the Keyboard setting.
class KeyboardPalette : public QWidget {
    Q_OBJECT

public:
    explicit KeyboardPalette(KeyboardMap& keyboard, QWidget* parent = nullptr);

    KeyButton* key(const QString& keyText) const;
    QList<KeyButton*> keys() const { return keys_; }
    // The Shift layer: keys show and take their Shift+key functions.
    void setShiftLayer(bool on);
    bool isShiftLayer() const { return shift_; }
    // Maps a command onto a key as dropping it there does.
    void mapCommand(KeyButton* key, const QString& commandId);
    // Shows the keyboard's current commands.
    void refresh();

signals:
    // A key was mapped; the keyboard map has changed.
    void keyboardChanged();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    QKeySequence sequenceFor(const KeyButton* key) const;

    KeyboardMap& keyboard_;
    QList<KeyButton*> keys_;
    QRadioButton* standard_ = nullptr;
    QRadioButton* foreign_ = nullptr;
    bool shift_ = false;
};

} // namespace lumenis::ui
