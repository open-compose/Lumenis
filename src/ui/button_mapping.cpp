#include "lumenis/ui/button_mapping.h"

#include "lumenis/ui/command_registry.h"

#include <QDragEnterEvent>
#include <QHash>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QMouseEvent>

// Button mapping (button_mapping.h): the application-wide ButtonMapper that holds the Command
// Palette's mode and the Menu to Button target, the command picture registry, and MappableButton,
// the user-selectable button used by the Tool palette, the Composer and the Keyboard palette
// (KeyButton derives from it).
//
// The Command Palette (command_palette.cpp) starts drags with ButtonMapper::mimeFor() and sets the
// mode; MainWindow forwards menu commands to mapMenuCommand() in Menu to Button mode. Windows that
// draw their own button pictures (Composer, Timeline) register them with registerCommandIcon().

namespace lumenis::ui {

namespace {

// Pictures registered with registerCommandIcon(), by command id.
QHash<QString, QIcon>& icons() {
    static QHash<QString, QIcon> all;
    return all;
}

// A 32 x 32 transparent picture with the letters centred in bold light grey; smaller type for
// more than two letters.
QIcon initialsIcon(const QString& initials, bool italic = false) {
    constexpr int size = 32;
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f = p.font();
    f.setPixelSize(initials.size() > 2 ? 11 : 14);
    f.setBold(true);
    f.setItalic(italic);
    p.setFont(f);
    p.setPen(QColor(0xc8, 0xc8, 0xc8));
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, initials);
    return QIcon(pm);
}

} // namespace

// ---------------------------------------------------------------- ButtonMapper

ButtonMapper& ButtonMapper::instance() {
    static ButtonMapper mapper;
    return mapper;
}

QMimeData* ButtonMapper::mimeFor(const QString& commandId) {
    auto* mime = new QMimeData;
    mime->setData(mimeType(), commandId.toUtf8());
    return mime;
}

void ButtonMapper::setMode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    setTarget(nullptr);
    emit modeChanged(mode);
}

void ButtonMapper::setRegistry(CommandRegistry* registry) {
    registry_ = registry;
}

QString ButtonMapper::text(const QString& commandId) const {
    if (commandId.isEmpty()) return tr("Blank");
    const QString t = registry_ ? registry_->text(commandId) : QString();
    return t.isEmpty() ? commandId : t;
}

void ButtonMapper::setTarget(MappableButton* button) {
    MappableButton* old = target_;
    target_ = button;
    if (old) old->update();
    if (button) button->update();
    emit targetChanged();
}

bool ButtonMapper::mapMenuCommand(const QString& commandId) {
    MappableButton* button = target_;
    if (!button) return false;
    setTarget(nullptr);
    button->setCommand(commandId);
    emit button->commandMapped(commandId);
    return true;
}

// ---------------------------------------------------------------- icons

QIcon labelIcon(const QString& text, bool italic) {
    return initialsIcon(text, italic);
}

void registerCommandIcon(const QString& commandId, const QIcon& icon) {
    icons().insert(commandId, icon);
}

QString commandInitials(const QString& text) {
    QString out;
    for (const QString& word : text.split(QRegularExpression(QStringLiteral("[\\s/]+")), Qt::SkipEmptyParts)) {
        const QChar c = word.at(0);
        if (c.isLetterOrNumber()) out += c.toUpper();
    }
    return out.left(3);
}

QIcon commandIcon(const QString& commandId) {
    if (commandId.isEmpty()) return {};
    if (const auto it = icons().constFind(commandId); it != icons().constEnd()) return *it;
    return initialsIcon(commandInitials(ButtonMapper::instance().text(commandId)));
}

// ---------------------------------------------------------------- MappableButton

// Reloads its picture whenever ButtonMapper::iconsChanged fires, and turns a click into
// commandClicked unless the button is Blank.
MappableButton::MappableButton(QWidget* parent) : QToolButton(parent) {
    setAutoRaise(true);
    setAcceptDrops(true);
    connect(&ButtonMapper::instance(), &ButtonMapper::iconsChanged, this, [this] { setIcon(commandIcon(command_)); });
    connect(&ButtonMapper::instance(), &ButtonMapper::marksChanged, this, qOverload<>(&QWidget::update));
    connect(this, &QToolButton::clicked, this, [this] {
        if (!command_.isEmpty()) emit commandClicked(command_);
    });
}

void ButtonMapper::setMarked(const QString& commandId, bool on) {
    if (on == marked_.contains(commandId)) return;
    if (on) {
        marked_.insert(commandId);
    } else {
        marked_.remove(commandId);
    }
    emit marksChanged();
}

void MappableButton::setCommand(const QString& commandId) {
    command_ = commandId;
    setIcon(commandIcon(commandId));
    setToolTip(commandId.isEmpty() ? QString() : ButtonMapper::instance().text(commandId));
    setProperty("command", commandId);
    update();
}

void MappableButton::mousePressEvent(QMouseEvent* event) {
    // Menu to Button: this is the target; the next menu command goes here.
    if (ButtonMapper::instance().mode() == ButtonMapper::Mode::MenuToButton) {
        ButtonMapper::instance().setTarget(this);
        event->accept();
        return;
    }
    QToolButton::mousePressEvent(event);
}

void MappableButton::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(ButtonMapper::mimeType())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

// A command dropped from the Command Palette replaces this button's command.
void MappableButton::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasFormat(ButtonMapper::mimeType())) return;
    const QString id = QString::fromUtf8(event->mimeData()->data(ButtonMapper::mimeType()));
    event->setDropAction(Qt::CopyAction);
    event->accept();
    setCommand(id);
    emit commandMapped(id);
}

// The Menu to Button target is outlined in the highlight colour.
void MappableButton::paintEvent(QPaintEvent* event) {
    QToolButton::paintEvent(event);
    if (!command_.isEmpty() && ButtonMapper::instance().isMarked(command_)) {
        // The orange mark of a mode that is on (p.496): a small triangle in the top-left corner.
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xff, 0x8c, 0x1a));
        p.drawPolygon(QPolygonF{QPointF(1, 1), QPointF(8, 1), QPointF(1, 8)});
    }
    if (ButtonMapper::instance().target() == this) {
        QPainter p(this);
        p.setPen(QPen(palette().color(QPalette::Highlight), 2));
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
}

} // namespace lumenis::ui
