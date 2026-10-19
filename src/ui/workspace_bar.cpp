#include "lumenis/ui/workspace_bar.h"

#include "lumenis/ui/theme.h"
#include "lumenis/ui/workspace.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

// The workspace toolbar and the New Workspace dialog (workspace_bar.h). Both only present
// WorkspaceManager (workspace.cpp), which does the switching and keeps the layouts. The icons are drawn
// in theme.cpp (glyphIcon).

namespace lumenis::ui {

namespace {

// Bar geometry in pixels: the space above the first item, an item's height with and without its label,
// and the icon size.
constexpr int kTop = 8;
constexpr int kItemHeightWide = 58;
constexpr int kItemHeightNarrow = 40;
constexpr int kIconSize = 24;

bool wide(const WorkspaceManager& w) {
    return w.barMode() == WorkspaceManager::BarMode::IconAndText;
}

} // namespace

WorkspaceBar::WorkspaceBar(WorkspaceManager& workspaces, QWidget* parent) : QWidget(parent), workspaces_(workspaces) {
    setObjectName(QStringLiteral("workspaceBar"));
    connect(&workspaces_, &WorkspaceManager::changed, this, &WorkspaceBar::sync);
    sync();
}

void WorkspaceBar::sync() {
    setFixedWidth(wide(workspaces_) ? kWorkspaceBarWide : kWorkspaceBarNarrow);
    setVisible(workspaces_.barMode() != WorkspaceManager::BarMode::Hidden);
    update();
}

QSize WorkspaceBar::sizeHint() const {
    return {width(), kTop + static_cast<int>(workspaces_.names().size()) * kItemHeightWide};
}

QRect WorkspaceBar::itemRect(int index) const {
    const int h = wide(workspaces_) ? kItemHeightWide : kItemHeightNarrow;
    return QRect(0, kTop + index * h, width(), h);
}

QIcon WorkspaceBar::iconFor(const QString& name, bool active) {
    const QColor color = active ? QColor::fromRgba(kAccentColor) : QColor(0xc8, 0xc8, 0xc8);
    if (name == QLatin1String("Edit")) return glyphIcon(GlyphIcon::WorkspaceEdit, color);
    if (name == QLatin1String("Color")) return glyphIcon(GlyphIcon::WorkspaceColor, color);
    if (name == QLatin1String("Effects")) return glyphIcon(GlyphIcon::WorkspaceEffects, color);
    if (name == QLatin1String("Audio")) return glyphIcon(GlyphIcon::WorkspaceAudio, color);
    return glyphIcon(GlyphIcon::WorkspaceCustom, color);
}

// Labels use the palette's WindowText so the text-free screenshots hide them.
void WorkspaceBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x2e, 0x2e, 0x2e));
    p.setPen(QColor::fromRgba(kSplitterColor));
    p.drawLine(0, 0, 0, height());

    QFont labelFont = font();
    labelFont.setPixelSize(9);
    labelFont.setWeight(QFont::DemiBold);
    p.setFont(labelFont);
    const QStringList names = workspaces_.names();
    for (int i = 0; i < names.size(); ++i) {
        const QRect r = itemRect(i);
        const bool active = names[i] == workspaces_.active();
        const QRect icon(r.left() + (r.width() - kIconSize) / 2, r.top() + 6, kIconSize, kIconSize);
        iconFor(names[i], active).paint(&p, icon);
        if (wide(workspaces_)) {
            p.setPen(palette().color(QPalette::WindowText));
            const QRect label(r.left() + 2, icon.bottom() + 4, r.width() - 4, 14);
            p.drawText(label, Qt::AlignHCenter | Qt::AlignTop,
                       p.fontMetrics().elidedText(names[i].toUpper(), Qt::ElideRight, label.width()));
        }
    }
}

void WorkspaceBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const QStringList names = workspaces_.names();
    for (int i = 0; i < names.size(); ++i) {
        if (itemRect(i).contains(event->position().toPoint())) {
            workspaces_.activate(names[i]);
            return;
        }
    }
}

// The right-click menu chooses the bar mode (p.87-89).
void WorkspaceBar::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.addSection(tr("WORKSPACE BAR"));
    auto* group = new QActionGroup(&menu);
    const std::pair<QString, WorkspaceManager::BarMode> modes[] = {
        {tr("Icon and Text"), WorkspaceManager::BarMode::IconAndText},
        {tr("Icon only"), WorkspaceManager::BarMode::IconOnly},
        {tr("Hide"), WorkspaceManager::BarMode::Hidden},
    };
    for (const auto& [text, mode] : modes) {
        QAction* a = menu.addAction(text);
        a->setCheckable(true);
        a->setChecked(workspaces_.barMode() == mode);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, m = mode] { workspaces_.setBarMode(m); });
    }
    menu.exec(event->globalPos());
}

// ---------------------------------------------------------------- NewWorkspaceDialog

NewWorkspaceDialog::NewWorkspaceDialog(const WorkspaceManager& workspaces, QWidget* parent)
    : QDialog(parent), workspaces_(workspaces) {
    setObjectName(QStringLiteral("newWorkspaceDialog"));
    setWindowTitle(tr("New Workspace"));
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("workspaceName"));
    basedOnBox_ = new QCheckBox(tr("Based on Workspace"), this);
    basedOnBox_->setObjectName(QStringLiteral("basedOnBox"));
    basedOn_ = new QComboBox(this);
    basedOn_->setObjectName(QStringLiteral("basedOn"));
    // "select a different workspace from the active one" (p.86).
    for (const QString& n : workspaces.names()) {
        if (n != workspaces.active()) basedOn_->addItem(n);
    }
    basedOn_->setEnabled(false);
    basedOnBox_->setEnabled(basedOn_->count() > 0);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);

    auto* form = new QFormLayout;
    form->addRow(tr("Workspace Name:"), name_);
    auto* based = new QHBoxLayout;
    based->addWidget(basedOnBox_);
    based->addWidget(basedOn_, 1);
    form->addRow(based);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(name_, &QLineEdit::textChanged, this, &NewWorkspaceDialog::validate);
    connect(basedOnBox_, &QCheckBox::toggled, basedOn_, &QWidget::setEnabled);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    validate();
}

QString NewWorkspaceDialog::name() const {
    return name_->text().trimmed();
}

QString NewWorkspaceDialog::basedOn() const {
    return basedOnBox_->isChecked() ? basedOn_->currentText() : QString();
}

void NewWorkspaceDialog::validate() {
    const QString n = name();
    ok_->setEnabled(!n.isEmpty() && !workspaces_.contains(n));
}

} // namespace lumenis::ui
