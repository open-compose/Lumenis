#include "lumenis/ui/command_palette.h"

#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QCompleter>
#include <QDrag>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QRadioButton>
#include <QStringListModel>
#include <QTabWidget>
#include <QVBoxLayout>

// The Command Palette and the Tool palette (command_palette.h). The palette's tabs and buttons are data,
// in src/ui/resources/command_palette.json: {"tabs": [{"name", "workspaceMenus", "columns": [[ids]]}]},
// where a button is a command id, "" for a blank button, or {"id", "platforms": [...]} for a button shown
// only on some platforms. Mapping (dragging buttons, Menu to Button, the current mode, icons) belongs to
// ButtonMapper and MappableButton in button_mapping.cpp; this file builds the windows and runs commands
// through CommandRegistry's actions.

namespace lumenis::ui {

namespace {

// The width of a palette button in pixels; its height is kToolButtonHeight.
constexpr int kPaletteButtonWidth = 28;

// The name used in command_palette.json's "platforms" lists.
QString platformName() {
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

// The "tabs" array of the built-in command_palette.json.
QJsonArray paletteTabs() {
    QFile file(QStringLiteral(":/lumenis/ui/command_palette.json"));
    if (!file.open(QIODevice::ReadOnly)) qFatal("built-in command_palette.json is missing");
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("tabs")).toArray();
}

} // namespace

// ---------------------------------------------------------------- PaletteButton

PaletteButton::PaletteButton(const QString& commandId, QWidget* parent) : QToolButton(parent), command_(commandId) {
    setObjectName(commandId.isEmpty() ? QStringLiteral("palette:blank") : QStringLiteral("palette:") + commandId);
    setAutoRaise(false);
    setFixedSize(kPaletteButtonWidth, kToolButtonHeight);
    setIconSize(QSize(16, 16));
    setIcon(commandIcon(commandId));
    setToolTip(ButtonMapper::instance().text(commandId));
    connect(&ButtonMapper::instance(), &ButtonMapper::iconsChanged, this, [this] { setIcon(commandIcon(command_)); });
}

void PaletteButton::mousePressEvent(QMouseEvent* event) {
    pressAt_ = event->position().toPoint();
    QToolButton::mousePressEvent(event);
}

// In the other modes the button behaves as a plain tool button.
void PaletteButton::mouseMoveEvent(QMouseEvent* event) {
    // Button to Button: drag the button onto another palette (p.98).
    if (ButtonMapper::instance().mode() == ButtonMapper::Mode::ButtonToButton && (event->buttons() & Qt::LeftButton) &&
        (event->position().toPoint() - pressAt_).manhattanLength() >= QApplication::startDragDistance()) {
        setDown(false);
        auto* drag = new QDrag(this);
        drag->setMimeData(ButtonMapper::mimeFor(command_));
        drag->setPixmap(icon().pixmap(iconSize()));
        drag->exec(Qt::CopyAction);
        return;
    }
    QToolButton::mouseMoveEvent(event);
}

// ---------------------------------------------------------------- CommandPalette

CommandPalette::CommandPalette(CommandRegistry& commands, QWidget* parent) : QWidget(parent), commands_(commands) {
    setObjectName(QStringLiteral("commandPalette"));
    ButtonMapper::instance().setRegistry(&commands);

    // Quick Find, at the top right (pictures on p.100).
    find_ = new QLineEdit(this);
    find_->setObjectName(QStringLiteral("quickFind"));
    find_->setPlaceholderText(tr("Quick Find"));
    find_->setClearButtonEnabled(true);
    find_->setFixedWidth(220);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("commandPaletteTabs"));
    tabs_->setUsesScrollButtons(true);
    QStringList names;
    // One page per tab: its columns side by side, each a grid of label, button and (on the Workspaces tab)
    // a workspace menu per row. A command that appears on several tabs gets a button on each, but buttons_,
    // labels_ and tabOf_ keep the first.
    for (const QJsonValue& t : paletteTabs()) {
        const QJsonObject tab = t.toObject();
        auto* page = new QWidget;
        auto* columns = new QHBoxLayout(page);
        columns->addStretch(1);
        const int index = static_cast<int>(tabNames_.size());
        const bool menus = tab.value(QStringLiteral("workspaceMenus")).toBool();
        for (const QJsonValue& c : tab.value(QStringLiteral("columns")).toArray()) {
            auto* grid = new QGridLayout;
            grid->setHorizontalSpacing(4);
            grid->setVerticalSpacing(1);
            int row = 0;
            for (const QJsonValue& b : c.toArray()) {
                QString id = b.toString();
                if (b.isObject()) {
                    const QJsonObject o = b.toObject();
                    if (!o.value(QStringLiteral("platforms")).toArray().contains(platformName())) continue;
                    id = o.value(QStringLiteral("id")).toString();
                }
                if (!id.isEmpty() && !commands_.contains(id)) continue;
                const QString text = ButtonMapper::instance().text(id);
                auto* label = new QLabel(text, page);
                label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                auto* button = new PaletteButton(id, page);
                connect(button, &QToolButton::clicked, this, [this, id] {
                    if (ButtonMapper::instance().mode() == ButtonMapper::Mode::Active) activate(id);
                });
                grid->addWidget(label, row, 0);
                grid->addWidget(button, row, 1);
                if (menus) {
                    // The workspace the button switches to (p.91).
                    auto* menu = new QComboBox(page);
                    const int w = static_cast<int>(workspaceMenus_.size());
                    menu->setObjectName(QStringLiteral("workspaceMenu:%1").arg(w + 1));
                    menu->setMinimumWidth(110);
                    connect(menu, &QComboBox::currentIndexChanged, this, [this, menu, w] {
                        if (!fillingMenus_) emit workspaceAssigned(w, menu->currentData().toString());
                    });
                    grid->addWidget(menu, row, 2);
                    workspaceMenus_ << menu;
                } else if (id.startsWith(QLatin1String("timeline.view.tv"))) {
                    // The Timeline view the TV button switches to (p.605).
                    auto* menu = new QComboBox(page);
                    const int v = static_cast<int>(viewMenus_.size());
                    menu->setObjectName(QStringLiteral("timelineViewMenu:%1").arg(v + 1));
                    menu->setMinimumWidth(110);
                    connect(menu, &QComboBox::currentIndexChanged, this, [this, menu, v] {
                        if (!fillingMenus_) emit timelineViewAssigned(v, menu->currentData().toString());
                    });
                    grid->addWidget(menu, row, 2);
                    viewMenus_ << menu;
                }
                ++row;
                rows_.append(Row{id, label, button});
                if (!buttons_.contains(id)) {
                    buttons_.insert(id, button);
                    labels_.insert(id, label);
                    tabOf_.insert(id, index);
                    if (!id.isEmpty()) names << text;
                }
            }
            grid->setRowStretch(row, 1);
            columns->addLayout(grid);
            columns->addSpacing(16);
        }
        columns->addStretch(1);
        tabNames_ << tab.value(QStringLiteral("name")).toString();
        tabs_->addTab(page, tabNames_.constLast());
    }

    // As you type, matching buttons are listed (picture on p.100).
    auto* completer = new QCompleter(new QStringListModel(names, this), this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    find_->setCompleter(completer);
    connect(find_, &QLineEdit::textChanged, this, &CommandPalette::setQuickFind);
    // Choosing a completion shows that command's button.
    connect(completer, qOverload<const QString&>(&QCompleter::activated), this, [this](const QString& text) {
        for (auto it = buttons_.cbegin(); it != buttons_.cend(); ++it) {
            if (!it.key().isEmpty() && ButtonMapper::instance().text(it.key()) == text) {
                showCommand(it.key());
                return;
            }
        }
    });

    // The modes at the bottom (picture on p.96).
    modes_ = new QButtonGroup(this);
    auto* modeRow = new QHBoxLayout;
    const std::pair<ButtonMapper::Mode, QString> modes[] = {
        {ButtonMapper::Mode::ButtonToButton, tr("'Button to Button' Reassignment")},
        {ButtonMapper::Mode::Active, tr("Active Palette")},
        {ButtonMapper::Mode::MenuToButton, tr("'Menu to Button' Reassignment")}};
    for (const auto& [mode, text] : modes) {
        auto* r = new QRadioButton(text, this);
        r->setObjectName(mode == ButtonMapper::Mode::ButtonToButton ? QStringLiteral("modeButtonToButton")
                         : mode == ButtonMapper::Mode::Active       ? QStringLiteral("modeActive")
                                                                    : QStringLiteral("modeMenuToButton"));
        modes_->addButton(r, static_cast<int>(mode));
        modeRow->addWidget(r);
        modeRow->addStretch(1);
    }
    connect(modes_, &QButtonGroup::idClicked, this,
            [](int id) { ButtonMapper::instance().setMode(static_cast<ButtonMapper::Mode>(id)); });
    connect(&ButtonMapper::instance(), &ButtonMapper::modeChanged, this, &CommandPalette::syncMode);
    syncMode();
    refresh();

    auto* top = new QHBoxLayout;
    top->addStretch(1);
    top->addWidget(find_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(top);
    layout->addWidget(tabs_, 1);
    layout->addLayout(modeRow);
}

// Refills every W button's menu with None and the workspace names, selecting the assigned one (None if
// it no longer exists).
void CommandPalette::setWorkspaces(const QStringList& names, const QStringList& assigned) {
    fillingMenus_ = true;
    for (int i = 0; i < workspaceMenus_.size(); ++i) {
        QComboBox* menu = workspaceMenus_.at(i);
        menu->clear();
        menu->addItem(tr("None"), QString());
        for (const QString& n : names) menu->addItem(n, n);
        const int at = menu->findData(assigned.value(i));
        menu->setCurrentIndex(std::max(at, 0));
    }
    fillingMenus_ = false;
}

void CommandPalette::setTimelineViews(const QStringList& names, const QStringList& assigned) {
    fillingMenus_ = true;
    for (int i = 0; i < viewMenus_.size(); ++i) {
        QComboBox* menu = viewMenus_.at(i);
        menu->clear();
        menu->addItem(tr("None"), QString());
        for (const QString& n : names) menu->addItem(n, n);
        menu->setCurrentIndex(std::max(menu->findData(assigned.value(i)), 0));
    }
    fillingMenus_ = false;
}

void CommandPalette::refresh() {
    // Commands not available (yet, or without a project) are dimmed and
    // cannot be mapped.
    for (const Row& r : rows_) r.button->setEnabled(r.id.isEmpty() || commands_.hasHandler(r.id));
}

void CommandPalette::showEvent(QShowEvent* event) {
    refresh();
    QWidget::showEvent(event);
}

void CommandPalette::hideEvent(QHideEvent* event) {
    // Closed in Menu to Button mode, buttons elsewhere would keep picking
    // targets instead of working: back to the default mode.
    if (!isVisible() && ButtonMapper::instance().mode() == ButtonMapper::Mode::MenuToButton) {
        ButtonMapper::instance().setMode(ButtonMapper::Mode::ButtonToButton);
    }
    QWidget::hideEvent(event);
}

void CommandPalette::syncMode() {
    if (QAbstractButton* b = modes_->button(static_cast<int>(ButtonMapper::instance().mode()))) b->setChecked(true);
}

QStringList CommandPalette::tabNames() const {
    return tabNames_;
}

QStringList CommandPalette::commandsOn(const QString& tabName) const {
    const int index = static_cast<int>(tabNames_.indexOf(tabName));
    QStringList out;
    for (auto it = tabOf_.cbegin(); it != tabOf_.cend(); ++it) {
        if (it.value() == index) out << it.key();
    }
    out.sort();
    return out;
}

QStringList CommandPalette::matches(const QString& text) const {
    QStringList out;
    if (text.trimmed().isEmpty()) return out;
    for (auto it = buttons_.cbegin(); it != buttons_.cend(); ++it) {
        if (!it.key().isEmpty() && ButtonMapper::instance().text(it.key()).contains(text.trimmed(), Qt::CaseInsensitive)) {
            out << it.key();
        }
    }
    out.sort();
    return out;
}

void CommandPalette::setQuickFind(const QString& text) {
    if (find_->text() != text) find_->setText(text);
    const QStringList found = matches(text);
    const bool all = text.trimmed().isEmpty();
    // Only the tabs that include matches remain open (p.100). The guide says
    // the button options are "narrowed down" without saying how; here the
    // labels of the buttons that do not match are dimmed.
    QSet<int> tabsWithMatches;
    for (const QString& id : found) tabsWithMatches << tabOf_.value(id);
    for (int i = 0; i < tabs_->count(); ++i) tabs_->setTabVisible(i, all || tabsWithMatches.contains(i));
    // Only the labels: the buttons keep the enabled state refresh() gave them (available commands), so
    // a button that does not match still works and can still be mapped.
    for (const Row& r : rows_) r.label->setEnabled(all || found.contains(r.id));
    if (!all && !found.isEmpty() && !tabsWithMatches.contains(tabs_->currentIndex())) {
        tabs_->setCurrentIndex(tabOf_.value(found.first()));
    }
}

bool CommandPalette::showCommand(const QString& commandId) {
    if (!tabOf_.contains(commandId)) return false;
    tabs_->setTabVisible(tabOf_.value(commandId), true);
    tabs_->setCurrentIndex(tabOf_.value(commandId));
    buttons_.value(commandId)->setFocus();
    return true;
}

void CommandPalette::activate(const QString& commandId) {
    if (QAction* a = commands_.action(commandId); a && a->isEnabled()) a->trigger();
}

// ---------------------------------------------------------------- ToolPalette

// A kRows x kColumns grid of MappableButtons; commands can be mapped onto them from the Command Palette.
ToolPalette::ToolPalette(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("toolPalette"));
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(2);
    const QStringList defaults = defaultCommands();
    for (int i = 0; i < kRows * kColumns; ++i) {
        auto* b = new MappableButton(this);
        b->setObjectName(QStringLiteral("toolPaletteButton%1").arg(i));
        b->setIconSize(QSize(16, 16));
        b->setMinimumSize(kPaletteButtonWidth, kToolButtonHeight);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setCommand(defaults.value(i));
        connect(b, &MappableButton::commandClicked, this, &ToolPalette::commandClicked);
        connect(b, &MappableButton::commandMapped, this, &ToolPalette::commandsChanged);
        grid->addWidget(b, i / kColumns, i % kColumns);
        slots_ << b;
    }
    grid->setRowStretch(kRows, 1);
}

QStringList ToolPalette::defaultCommands() {
    // Our choice: the guide shows no default Tool palette.
    return {QStringLiteral("edit.mark_in"),         QStringLiteral("edit.mark_out"),
            QStringLiteral("edit.mark_clip"),       QStringLiteral("edit.clear_both_marks"),
            QStringLiteral("edit.splice_in"),       QStringLiteral("edit.overwrite"),
            QStringLiteral("edit.lift"),            QStringLiteral("edit.extract"),
            QStringLiteral("more.add_marker"),      QStringLiteral("other.match_frame"),
            QStringLiteral("edit.top"),             QStringLiteral("edit.tail")};
}

QStringList ToolPalette::commands() const {
    QStringList out;
    for (const MappableButton* b : slots_) out << b->command();
    return out;
}

// A button is set again only when its command changed, or its icon does not match whether it is blank.
void ToolPalette::setCommands(const QStringList& commands) {
    const QStringList defaults = defaultCommands();
    for (int i = 0; i < slots_.size(); ++i) {
        const QString id = i < commands.size() ? commands.at(i) : defaults.value(i);
        if (slots_.at(i)->command() != id || slots_.at(i)->icon().isNull() != id.isEmpty()) slots_.at(i)->setCommand(id);
    }
    setShowLabels(labels_);
}

void ToolPalette::setShowLabels(bool on) {
    labels_ = on;
    for (MappableButton* b : slots_) {
        b->setToolButtonStyle(on ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonIconOnly);
        b->setText(on && !b->isBlank() ? ButtonMapper::instance().text(b->command()) : QString());
        b->setFixedHeight(on ? 2 * kToolButtonHeight : kToolButtonHeight);
    }
}

} // namespace lumenis::ui
