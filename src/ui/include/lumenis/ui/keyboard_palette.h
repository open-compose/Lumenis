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
    // `keyText` is Qt's portable key text; `label` is drawn on the key, or the
    // key text when empty. The object name is "key:<keyText>".
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
// are saved in the Keyboard setting. MainWindow shows it in the "Keyboard"
// panel and stores keyboardChanged() as the setting's "keys" value
// (KeyboardMap::differences()). The palette edits the KeyboardMap in place;
// the map must outlive it.
class KeyboardPalette : public QWidget {
    Q_OBJECT

public:
    explicit KeyboardPalette(KeyboardMap& keyboard, QWidget* parent = nullptr);

    // The key with this portable text, or nullptr if the palette has none.
    KeyButton* key(const QString& keyText) const;
    // Every mappable key (not gaps, Shift or Caps Lock), in layout order.
    QList<KeyButton*> keys() const { return keys_; }
    // The Shift layer: keys show and take their Shift+key functions.
    void setShiftLayer(bool on);
    bool isShiftLayer() const { return shift_; }
    // Maps a command onto a key as dropping it there does, in the current
    // layer. "" leaves the key unassigned; the modifier key buttons swap the
    // key's function with its Alt or Control function (p.97). Emits
    // keyboardChanged.
    void mapCommand(KeyButton* key, const QString& commandId);
    // Shows the keyboard's current commands. Call it after changing the
    // KeyboardMap from outside (a restored setting).
    void refresh();

signals:
    // A key was mapped; the keyboard map has changed.
    void keyboardChanged();

protected:
    // Pressing and releasing Shift switch the Shift layer on and off.
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    // The key's sequence in the current layer ("Shift+<key>" in the Shift layer).
    QKeySequence sequenceFor(const KeyButton* key) const;

    KeyboardMap& keyboard_;
    QList<KeyButton*> keys_;
    QRadioButton* standard_ = nullptr;
    QRadioButton* foreign_ = nullptr;
    bool shift_ = false;
};

} // namespace lumenis::ui
