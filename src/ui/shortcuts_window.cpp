#include "lumenis/ui/shortcuts_window.h"

#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/keyboard_map.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

// The Help > Shortcuts window (shortcuts_window.h).

namespace lumenis::ui {

ShortcutsWindow::ShortcutsWindow(const CommandRegistry& commands, const KeyboardMap& keyboard, QWidget* parent)
    : QWidget(parent, Qt::Window), commands_(commands), keyboard_(keyboard) {
    setObjectName(QStringLiteral("shortcutsWindow"));
    setWindowTitle(tr("Shortcuts"));
    auto* layout = new QVBoxLayout(this);
    filter_ = new QLineEdit(this);
    filter_->setObjectName(QStringLiteral("filter"));
    filter_->setPlaceholderText(tr("Find"));
    filter_->setClearButtonEnabled(true);
    layout->addWidget(filter_);
    table_ = new QTableWidget(0, 3, this);
    table_->setObjectName(QStringLiteral("shortcuts"));
    table_->setHorizontalHeaderLabels({tr("Command"), tr("Menu Shortcut"), tr("Keyboard")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_);
    connect(filter_, &QLineEdit::textChanged, this, &ShortcutsWindow::refresh);
    resize(560, 480);
}

void ShortcutsWindow::showEvent(QShowEvent* event) {
    refresh();
    QWidget::showEvent(event);
}

void ShortcutsWindow::refresh() {
    struct Row {
        QString command;
        QString menu;
        QString keys;
    };
    QList<Row> rows;
    const QString find = filter_->text().trimmed();
    for (const QString& id : commands_.ids()) {
        const QString menu = commands_.shortcut(id).toString(QKeySequence::NativeText);
        QStringList keys;
        for (const QKeySequence& k : keyboard_.keysFor(id)) keys << k.toString(QKeySequence::NativeText);
        if (menu.isEmpty() && keys.isEmpty()) continue;
        QString name = commands_.text(id);
        name.remove(QLatin1Char('&'));
        const Row row{name, menu, keys.join(QStringLiteral(", "))};
        if (!find.isEmpty() && !row.command.contains(find, Qt::CaseInsensitive) && !row.menu.contains(find, Qt::CaseInsensitive) &&
            !row.keys.contains(find, Qt::CaseInsensitive)) {
            continue;
        }
        rows << row;
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.command.localeAwareCompare(b.command) < 0; });
    table_->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < rows.size(); ++r) {
        table_->setItem(r, 0, new QTableWidgetItem(rows.at(r).command));
        table_->setItem(r, 1, new QTableWidgetItem(rows.at(r).menu));
        table_->setItem(r, 2, new QTableWidgetItem(rows.at(r).keys));
    }
}

} // namespace lumenis::ui
