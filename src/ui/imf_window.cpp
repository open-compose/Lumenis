// The IMF window (imf_window.h; docs/spec/imf.md; Editing Guide p.333-337). The sidebar is a QTreeWidget whose items
// keep the path they stand for (Qt::UserRole) and their kind (UserRole + 1); dragging a CPL out gives the CPL file's
// URL, so a bin takes it as it takes any file (BinItemModel's drops, MainWindow's routing to importImfComposition).
#include "lumenis/ui/imf_window.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace lumenis::ui {

namespace {

enum Kind { Root = 1, Imp = 2, Cpl = 3 };
constexpr int kPathRole = Qt::UserRole;
constexpr int kKindRole = Qt::UserRole + 1;

// The sidebar: a CPL drags as its file.
class ImfTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    QStringList mimeTypes() const override { return {QStringLiteral("text/uri-list")}; }
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
        QList<QUrl> urls;
        for (const QTreeWidgetItem* item : items) {
            if (item->data(0, kKindRole).toInt() == Cpl) urls << QUrl::fromLocalFile(item->data(0, kPathRole).toString());
        }
        if (urls.isEmpty()) return nullptr;
        auto* mime = new QMimeData;
        mime->setUrls(urls);
        return mime;
    }
};

QString rateText(const Rational& rate) {
    return QString::number(rate.toDouble(), 'g', 6);
}

} // namespace

ImfWindow::ImfWindow(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("imfWindow"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* buttons = new QHBoxLayout;
    explore_ = new QPushButton(tr("Explore"), this);
    explore_->setObjectName(QStringLiteral("imfExplore"));
    connect(explore_, &QPushButton::clicked, this, [this] { explore(); });
    buttons->addWidget(explore_);
    buttons->addStretch();
    layout->addLayout(buttons);

    auto* split = new QSplitter(Qt::Horizontal, this);
    tree_ = new ImfTree(split);
    tree_->setObjectName(QStringLiteral("imfSidebar"));
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("Name"), tr("Status")});
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->setDragEnabled(true);
    tree_->setDragDropMode(QAbstractItemView::DragOnly);
    tree_->setAccessibleName(tr("IMF packages")); // screen readers (10.3)
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { showSelection(); });
    // "The right pane can also be collapsed" (p.334): the splitter's handle.
    pane_ = new QWidget(split);
    form_ = new QFormLayout(pane_);
    split->setCollapsible(1, true);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);
}

bool ImfWindow::explore() {
    // "Click the Explore button and navigate to an existing IMF original" (p.335).
    const QString root = folderChooser_ ? folderChooser_() : QFileDialog::getExistingDirectory(this, tr("Explore"));
    if (root.isEmpty()) return false;
    QString error;
    if (!addPackage(root, &error)) {
        emit failed(error);
        return false;
    }
    return true;
}

bool ImfWindow::addPackage(const QString& root, QString* error) {
    QStringList warnings;
    const auto package = interop::imf::readPackage(root, error, &warnings);
    if (!package) return false;
    if (!warnings.isEmpty()) emit failed(warnings.join(QLatin1Char('\n')));
    bool replaced = false;
    for (interop::imf::Package& p : packages_) {
        if (p.root == package->root) {
            p = *package;
            replaced = true;
        }
    }
    if (!replaced) packages_ << *package;
    rebuild();
    select(package->root);
    return true;
}

void ImfWindow::removePackage(const QString& root) {
    const QString clean = QFileInfo(root).absoluteFilePath();
    packages_.removeIf([&](const interop::imf::Package& p) { return p.root == clean; });
    rebuild();
}

std::optional<interop::imf::Package> ImfWindow::packageOf(const QString& cplFile) const {
    const QString file = QFileInfo(cplFile).absoluteFilePath();
    for (const interop::imf::Package& p : packages_) {
        for (const interop::imf::Composition& c : p.compositions) {
            if (c.file != file) continue;
            interop::imf::Package out = p;
            for (const interop::imf::Package& other : packages_) {
                if (other.root != p.root) out.addAssets(other);
            }
            return out;
        }
    }
    return std::nullopt;
}

// Root folder > IMP (its packing list) > CPLs the IMP lists. "Green = has been exported and exists on disk": every
// package explored is on disk (New IMF's yellow and red come with 11.5c2).
void ImfWindow::rebuild() {
    tree_->clear();
    for (const interop::imf::Package& p : packages_) {
        auto* root = new QTreeWidgetItem(tree_, {p.name, QStringLiteral("●")});
        root->setData(0, kPathRole, p.root);
        root->setData(0, kKindRole, Root);
        root->setForeground(1, QColor(0x30, 0xb0, 0x40));
        root->setToolTip(1, tr("Exported, on disk"));
        root->setFlags(root->flags() & ~Qt::ItemIsDragEnabled);
        for (const interop::imf::PackingList& pkl : p.imps) {
            auto* imp = new QTreeWidgetItem(root, {QFileInfo(pkl.file).completeBaseName(), QStringLiteral("●")});
            imp->setData(0, kPathRole, pkl.file);
            imp->setData(0, kKindRole, Imp);
            imp->setForeground(1, QColor(0x30, 0xb0, 0x40));
            imp->setFlags(imp->flags() & ~Qt::ItemIsDragEnabled);
            for (const interop::imf::PackingList::Entry& e : pkl.assets) {
                for (const interop::imf::Composition& c : p.compositions) {
                    if (c.id != e.id) continue;
                    auto* cpl = new QTreeWidgetItem(imp, {c.contentTitle.isEmpty() ? QFileInfo(c.file).completeBaseName() : c.contentTitle,
                                                          QStringLiteral("●")});
                    cpl->setData(0, kPathRole, c.file);
                    cpl->setData(0, kKindRole, Cpl);
                    cpl->setForeground(1, QColor(0x30, 0xb0, 0x40));
                }
            }
        }
        root->setExpanded(true);
        for (int i = 0; i < root->childCount(); ++i) root->child(i)->setExpanded(true);
    }
}

bool ImfWindow::select(const QString& path) {
    const QString clean = QFileInfo(path).absoluteFilePath();
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        if ((*it)->data(0, kPathRole).toString() == clean) {
            tree_->setCurrentItem(*it);
            return true;
        }
    }
    return false;
}

QColor ImfWindow::statusColor(const QTreeWidgetItem* item) {
    return item ? item->foreground(1).color() : QColor();
}

// "Selecting the root folder will display the IMF name as it appears on disk and it's directory path, along with
// Issuer, Creator and Annotation metadata. Selecting an IMP displays the IMPname, Issuer, Creator and Annotation ...
// Selecting the CPL will display the Content Title and CPL file name on disk. Additional structural and descriptive
// metadata is displayed below that" (p.335). Read-only: "Because this package is read only, no metadata or parameters
// can be adjusted".
void ImfWindow::showSelection() {
    while (form_->rowCount() > 0) form_->removeRow(0);
    const QTreeWidgetItem* item = tree_->currentItem();
    if (!item) return;
    const QString path = item->data(0, kPathRole).toString();
    auto row = [this](const QString& label, const QString& value) {
        auto* field = new QLabel(value, pane_);
        field->setTextInteractionFlags(Qt::TextSelectableByMouse);
        field->setWordWrap(true);
        form_->addRow(label, field);
    };
    for (const interop::imf::Package& p : packages_) {
        if (item->data(0, kKindRole).toInt() == Root && p.root == path) {
            row(tr("IMF Name"), p.name);
            row(tr("Directory"), QDir::toNativeSeparators(p.root));
            row(tr("Issuer"), p.issuer);
            row(tr("Creator"), p.creator);
            row(tr("Annotation"), p.annotation);
            return;
        }
        for (const interop::imf::PackingList& pkl : p.imps) {
            if (item->data(0, kKindRole).toInt() == Imp && pkl.file == path) {
                row(tr("IMP Name"), QFileInfo(pkl.file).fileName());
                row(tr("Issuer"), pkl.issuer);
                row(tr("Creator"), pkl.creator);
                row(tr("Annotation"), pkl.annotation);
                return;
            }
        }
        for (const interop::imf::Composition& c : p.compositions) {
            if (item->data(0, kKindRole).toInt() != Cpl || c.file != path) continue;
            row(tr("Content Title"), c.contentTitle);
            row(tr("CPL File Name"), QFileInfo(c.file).fileName());
            row(tr("Content Kind"), c.contentKind);
            row(tr("Edit Rate"), rateText(c.editRate));
            std::int64_t frames = 0;
            int image = 0;
            int audio = 0;
            for (const interop::imf::Segment& s : c.segments) {
                std::int64_t longest = 0;
                for (const interop::imf::Sequence& q : s.sequences) {
                    std::int64_t length = 0;
                    for (const interop::imf::Resource& r : q.resources) {
                        length += r.editRate == c.editRate ? r.length()
                                                           : static_cast<std::int64_t>(std::llround(static_cast<double>(r.length()) *
                                                                                                    c.editRate.toDouble() / r.editRate.toDouble()));
                    }
                    if (q.isImage()) {
                        longest = std::max(longest, length);
                        ++image;
                    } else if (q.isAudio()) {
                        ++audio;
                    }
                }
                frames += longest;
            }
            row(tr("Duration"), tr("%n frame(s)", "", static_cast<int>(frames)));
            row(tr("Segments"), QString::number(c.segments.size()));
            row(tr("Image Sequences"), QString::number(image));
            row(tr("Audio Sequences"), QString::number(audio));
            row(tr("Track Files"), QString::number(c.trackFiles().size()));
            row(tr("Issuer"), c.issuer);
            row(tr("Creator"), c.creator);
            row(tr("Issue Date"), c.issueDate);
            row(tr("Annotation"), c.annotation);
            return;
        }
    }
}

QStringList ImfWindow::metadata() const {
    QStringList out;
    for (int i = 0; i < form_->rowCount(); ++i) {
        const auto* label = qobject_cast<QLabel*>(form_->itemAt(i, QFormLayout::LabelRole) ? form_->itemAt(i, QFormLayout::LabelRole)->widget() : nullptr);
        const auto* field = qobject_cast<QLabel*>(form_->itemAt(i, QFormLayout::FieldRole) ? form_->itemAt(i, QFormLayout::FieldRole)->widget() : nullptr);
        if (label && field) out << QStringLiteral("%1: %2").arg(label->text(), field->text());
    }
    return out;
}

void ImfWindow::keyPressEvent(QKeyEvent* event) {
    // Deleting a selected root folder takes the package off the sidebar (p.338).
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && tree_->currentItem() &&
        tree_->currentItem()->data(0, kKindRole).toInt() == Root) {
        removePackage(tree_->currentItem()->data(0, kPathRole).toString());
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace lumenis::ui
