#include "lumenis/ui/button_mapping.h"

#include "lumenis/ui/command_registry.h"

#include <QDragEnterEvent>
#include <QHash>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QMouseEvent>

namespace lumenis::ui {

namespace {

QHash<QString, QIcon>& icons() {
    static QHash<QString, QIcon> all;
    return all;
}

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

MappableButton::MappableButton(QWidget* parent) : QToolButton(parent) {
    setAutoRaise(true);
    setAcceptDrops(true);
    connect(&ButtonMapper::instance(), &ButtonMapper::iconsChanged, this, [this] { setIcon(commandIcon(command_)); });
    connect(this, &QToolButton::clicked, this, [this] {
        if (!command_.isEmpty()) emit commandClicked(command_);
    });
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

void MappableButton::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasFormat(ButtonMapper::mimeType())) return;
    const QString id = QString::fromUtf8(event->mimeData()->data(ButtonMapper::mimeType()));
    event->setDropAction(Qt::CopyAction);
    event->accept();
    setCommand(id);
    emit commandMapped(id);
}

void MappableButton::paintEvent(QPaintEvent* event) {
    QToolButton::paintEvent(event);
    if (ButtonMapper::instance().target() == this) {
        QPainter p(this);
        p.setPen(QPen(palette().color(QPalette::Highlight), 2));
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
}

} // namespace lumenis::ui
