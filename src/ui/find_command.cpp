// Help > Find Command (find_command.h; ROADMAP 10.3c, docs/spec/accessibility.md).

#include "lumenis/ui/find_command.h"

#include "lumenis/ui/command_registry.h"

#include <QAction>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QVBoxLayout>

namespace lumenis::ui {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;

QString plain(QString text) {
    return text.remove(QLatin1Char('&'));
}

// Each action's place in the menus, "Menu > Submenu".
void menuPaths(const QMenu* menu, const QString& path, QHash<const QAction*, QString>& out) {
    for (QAction* a : menu->actions()) {
        if (a->isSeparator()) continue;
        if (a->menu()) {
            menuPaths(a->menu(), path + QStringLiteral(" > ") + plain(a->text()), out);
            continue;
        }
        if (!out.contains(a)) out.insert(a, path);
    }
}

} // namespace

FindCommandWindow::FindCommandWindow(CommandRegistry& commands, const QMenuBar* menus, QWidget* parent)
    : QDialog(parent), commands_(commands), menus_(menus) {
    setObjectName(QStringLiteral("findCommand"));
    setWindowTitle(tr("Find Command"));
    auto* layout = new QVBoxLayout(this);
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("findCommandSearch"));
    search_->setPlaceholderText(tr("Type part of a command's name"));
    search_->setClearButtonEnabled(true);
    search_->installEventFilter(this);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("findCommandList"));
    list_->setAccessibleName(tr("Commands"));
    layout->addWidget(search_);
    layout->addWidget(list_, 1);
    resize(560, 420);
    connect(search_, &QLineEdit::textChanged, this, &FindCommandWindow::filter);
    connect(list_, &QListWidget::itemActivated, this, [this] { runChosen(); });
}

void FindCommandWindow::showFor(QWidget* previousFocus) {
    previousFocus_ = previousFocus;
    refill();
    search_->clear();
    filter(QString());
    show();
    raise();
    activateWindow();
    search_->setFocus(Qt::OtherFocusReason);
}

void FindCommandWindow::refill() {
    list_->clear();
    QHash<const QAction*, QString> paths;
    if (menus_) {
        for (QAction* a : menus_->actions()) {
            if (a->menu()) menuPaths(a->menu(), plain(a->text()), paths);
        }
    }
    for (const QString& id : commands_.ids()) {
        QAction* action = commands_.action(id);
        if (!action || !action->isEnabled() || !commands_.hasHandler(id) || id == QLatin1String("help.find_command")) continue;
        const QString text = plain(action->text());
        if (text.isEmpty()) continue;
        QString line = text;
        const QString path = paths.value(action);
        if (!path.isEmpty()) line += QStringLiteral("    ") + path;
        const QKeySequence key = action->shortcut();
        if (!key.isEmpty()) line += QStringLiteral("    ") + key.toString(QKeySequence::NativeText);
        auto* item = new QListWidgetItem(line, list_);
        item->setData(kIdRole, id);
        item->setToolTip(id);
    }
    list_->sortItems();
}

void FindCommandWindow::filter(const QString& text) {
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QListWidgetItem* first = nullptr;
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        bool shown = true;
        for (const QString& w : words) shown = shown && item->text().contains(w, Qt::CaseInsensitive);
        item->setHidden(!shown);
        if (shown && !first) first = item;
    }
    list_->setCurrentItem(first);
}

QStringList FindCommandWindow::shownIds() const {
    QStringList out;
    for (int i = 0; i < list_->count(); ++i) {
        if (!list_->item(i)->isHidden()) out << list_->item(i)->data(kIdRole).toString();
    }
    return out;
}

bool FindCommandWindow::runChosen() {
    QListWidgetItem* item = list_->currentItem();
    if (!item || item->isHidden()) return false;
    QAction* action = commands_.action(item->data(kIdRole).toString());
    if (!action) return false;
    hide();
    // The command acts where the user was: give the focus back first.
    QPointer<QWidget> back = previousFocus_;
    if (back) {
        back->activateWindow();
        back->setFocus(Qt::OtherFocusReason);
    }
    action->trigger();
    return true;
}

// Up, Down, Page Up and Page Down move through the list while typing; Enter runs; Esc closes (QDialog).
bool FindCommandWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == search_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            QCoreApplication::sendEvent(list_, event);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            runChosen();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace lumenis::ui
