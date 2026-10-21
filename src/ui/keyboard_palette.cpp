#include "lumenis/ui/keyboard_palette.h"

#include "lumenis/ui/keyboard_map.h"
#include "lumenis/ui/theme.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QRadioButton>
#include <QVBoxLayout>

// The Keyboard palette (KeyboardPalette, keyboard_palette.h): a picture of a US keyboard whose keys
// are KeyButtons (MappableButtons from button_mapping.cpp) showing the commands of the editing
// keyboard (KeyboardMap, keyboard_map.cpp). Commands dragged from the Command Palette
// (command_palette.cpp) onto a key change the map; MainWindow saves it in the Keyboard setting.
// The key layout is the static tables rows() and sideRows() below.

namespace lumenis::ui {

namespace {

constexpr int kUnit = 34;  // a key's width, in pixels
constexpr int kKeyHeight = 34;

struct KeySpec {
    const char* key;   // portable text; empty for a gap or a key that maps nothing
    const char* label; // shown on the key; empty: the key text
    double width = 1;  // in key units
};

// A US English keyboard (the layout of the picture on p.1202; the picture
// shows icons only, so the key names are ours).
const QList<QList<KeySpec>>& rows() {
    static const QList<QList<KeySpec>> all{
        {{"Esc", "", 1}, {"", "", 1}, {"F1", "", 1}, {"F2", "", 1}, {"F3", "", 1}, {"F4", "", 1}, {"", "", 0.5},
         {"F5", "", 1}, {"F6", "", 1}, {"F7", "", 1}, {"F8", "", 1}, {"", "", 0.5}, {"F9", "", 1}, {"F10", "", 1},
         {"F11", "", 1}, {"F12", "", 1}},
        {{"`", "", 1}, {"1", "", 1}, {"2", "", 1}, {"3", "", 1}, {"4", "", 1}, {"5", "", 1}, {"6", "", 1},
         {"7", "", 1}, {"8", "", 1}, {"9", "", 1}, {"0", "", 1}, {"-", "", 1}, {"=", "", 1}, {"Backspace", "Bksp", 2}},
        {{"Tab", "", 1.5}, {"Q", "", 1}, {"W", "", 1}, {"E", "", 1}, {"R", "", 1}, {"T", "", 1}, {"Y", "", 1},
         {"U", "", 1}, {"I", "", 1}, {"O", "", 1}, {"P", "", 1}, {"[", "", 1}, {"]", "", 1}, {"\\", "", 1.5}},
        {{"", "Caps", 1.75}, {"A", "", 1}, {"S", "", 1}, {"D", "", 1}, {"F", "", 1}, {"G", "", 1}, {"H", "", 1},
         {"J", "", 1}, {"K", "", 1}, {"L", "", 1}, {";", "", 1}, {"'", "", 1}, {"Return", "Enter", 2.25}},
        {{"", "Shift", 2.25}, {"Z", "", 1}, {"X", "", 1}, {"C", "", 1}, {"V", "", 1}, {"B", "", 1}, {"N", "", 1},
         {"M", "", 1}, {",", "", 1}, {".", "", 1}, {"/", "", 1}, {"", "Shift", 2.75}},
        {{"", "", 3.75}, {"Space", "", 6.25}},
    };
    return all;
}

// The navigation block beside the keys (picture on p.1202).
const QList<QList<KeySpec>>& sideRows() {
    static const QList<QList<KeySpec>> all{
        {{"", "", 1}},
        {{"Home", "Hom", 1}, {"PgUp", "PUp", 1}},
        {{"End", "", 1}, {"PgDown", "PDn", 1}},
        {{"", "", 1}},
        {{"", "", 1}, {"Up", "↑", 1}},
        {{"Left", "←", 1}, {"Down", "↓", 1}, {"Right", "→", 1}},
    };
    return all;
}

} // namespace

// ---------------------------------------------------------------- KeyButton

KeyButton::KeyButton(const QString& keyText, const QString& label, QWidget* parent)
    : MappableButton(parent), key_(keyText), label_(label.isEmpty() ? keyText : label) {
    setObjectName(QStringLiteral("key:") + keyText);
    setAutoRaise(false);
    setIconSize(QSize(16, 16));
}

void KeyButton::paintEvent(QPaintEvent* event) {
    MappableButton::paintEvent(event);
    // The key's name in a corner, in the text colour (hidden in text-free
    // screenshots).
    QPainter p(this);
    QFont f = font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(rect().adjusted(3, 0, -3, -2), Qt::AlignLeft | Qt::AlignBottom, label_);
}

// ---------------------------------------------------------------- KeyboardPalette

// Builds the main and navigation key blocks from the KeySpec tables, the Standard / Map Foreign
// Keyboard choice under them, and shows the current map.
KeyboardPalette::KeyboardPalette(KeyboardMap& keyboard, QWidget* parent) : QWidget(parent), keyboard_(keyboard) {
    setObjectName(QStringLiteral("keyboardPalette"));
    setFocusPolicy(Qt::StrongFocus);

    // One block of rows; a key w units wide also covers the w - 1 two-pixel gaps it spans, so
    // wide keys line up with the rows above and below.
    auto block = [this](const QList<QList<KeySpec>>& spec) {
        auto* w = new QWidget(this);
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        for (const QList<KeySpec>& row : spec) {
            auto* h = new QHBoxLayout;
            h->setSpacing(2);
            for (const KeySpec& k : row) {
                const int width = static_cast<int>(k.width * kUnit + (k.width - 1) * 2);
                const QString keyText = QString::fromUtf8(k.key);
                const QString label = QString::fromUtf8(k.label);
                if (keyText.isEmpty()) {
                    // A gap, or a key that maps nothing (Shift, Caps Lock).
                    auto* gap = new QLabel(label, w);
                    gap->setFixedSize(width, kKeyHeight);
                    gap->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
                    gap->setEnabled(false);
                    h->addWidget(gap);
                    continue;
                }
                auto* b = new KeyButton(keyText, label, w);
                b->setFixedSize(width, kKeyHeight);
                connect(b, &MappableButton::commandMapped, this,
                        [this, b](const QString& id) { mapCommand(b, id); });
                h->addWidget(b);
                keys_ << b;
            }
            h->addStretch(1);
            v->addLayout(h);
        }
        return w;
    };
    auto* keyboardRow = new QHBoxLayout;
    keyboardRow->setSpacing(12);
    keyboardRow->addWidget(block(rows()));
    keyboardRow->addWidget(block(sideRows()));
    keyboardRow->addStretch(1);

    // Standard, or Map Foreign Keyboard (p.1202), which needs Windows'
    // French or German regional settings; not available yet.
    standard_ = new QRadioButton(tr("Standard"), this);
    standard_->setObjectName(QStringLiteral("keyboardStandard"));
    standard_->setChecked(true);
    foreign_ = new QRadioButton(tr("Map Foreign Keyboard"), this);
    foreign_->setObjectName(QStringLiteral("keyboardForeign"));
    foreign_->setEnabled(false);
    foreign_->setToolTip(tr("Not available yet"));
    auto* modes = new QHBoxLayout;
    modes->addWidget(standard_);
    modes->addSpacing(40);
    modes->addWidget(foreign_);
    modes->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(keyboardRow);
    layout->addLayout(modes);
    layout->addStretch(1);
    refresh();
}

KeyButton* KeyboardPalette::key(const QString& keyText) const {
    for (KeyButton* k : keys_) {
        if (k->keyText() == keyText) return k;
    }
    return nullptr;
}

QKeySequence KeyboardPalette::sequenceFor(const KeyButton* key) const {
    const QString text = shift_ ? QStringLiteral("Shift+") + key->keyText() : key->keyText();
    return QKeySequence::fromString(text, QKeySequence::PortableText);
}

void KeyboardPalette::setShiftLayer(bool on) {
    if (shift_ == on) return;
    shift_ = on;
    refresh();
}

void KeyboardPalette::refresh() {
    for (KeyButton* k : keys_) k->setCommand(keyboard_.commandFor(sequenceFor(k)));
}

void KeyboardPalette::mapCommand(KeyButton* key, const QString& commandId) {
    const QKeySequence seq = sequenceFor(key);
    // The modifier key buttons (p.97): the key takes the function it has
    // with that modifier held, e.g. I with Add Alt Key becomes Go to IN, and
    // the key's own function moves to the modifier: Alt+I then marks IN, as
    // the guide's note says the default function stays under the modifier.
    // The two are swapped, so nothing is lost even when the modified key had
    // no function (the key is then unassigned and its function is on Alt).
    Qt::KeyboardModifier modifier = Qt::NoModifier;
    if (commandId == QLatin1String("other.add_alt_key") || commandId == QLatin1String("other.add_option_key")) {
        modifier = Qt::AltModifier;
    } else if (commandId == QLatin1String("other.add_control_key")) {
        modifier = Qt::MetaModifier; // the Control key on macOS
    }
    if (modifier != Qt::NoModifier) {
        const QKeyCombination base = seq[0];
        const QKeySequence modifiedKey(QKeyCombination(base.keyboardModifiers() | modifier, base.key()));
        const QString own = keyboard_.commandFor(seq);
        keyboard_.assign(seq, keyboard_.commandFor(modifiedKey));
        keyboard_.assign(modifiedKey, own);
    } else {
        // A command, or Blank to leave the key unassigned.
        keyboard_.assign(seq, commandId);
    }
    refresh();
    emit keyboardChanged();
}

void KeyboardPalette::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Shift) setShiftLayer(true);
    QWidget::keyPressEvent(event);
}

void KeyboardPalette::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Shift) setShiftLayer(false);
    QWidget::keyReleaseEvent(event);
}

} // namespace lumenis::ui
