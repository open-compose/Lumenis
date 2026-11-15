// The IMF window (imf_window.h; docs/spec/imf.md; Editing Guide p.333-337). The sidebar is a QTreeWidget whose items
// keep the path they stand for (Qt::UserRole) and their kind (UserRole + 1); dragging a CPL out gives the CPL file's
// URL, so a bin takes it as it takes any file (BinItemModel's drops, MainWindow's routing to importImfComposition).
// Drafts (packages not yet written, 11.5c2) are items of kinds DraftRoot, DraftImp and DraftCpl whose path role holds
// their indexes "draft/imp/cpl"; their right pane is editable, and every edit recomputes the red and yellow statuses.
#include "lumenis/ui/imf_window.h"

#include "lumenis/media/imf_export.h"
#include "lumenis/ui/bin_dock.h"

#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace lumenis::ui {

namespace {

enum Kind { Root = 1, Imp = 2, Cpl = 3, DraftRoot = 4, DraftImp = 5, DraftCpl = 6 };
constexpr int kPathRole = Qt::UserRole;
constexpr int kKindRole = Qt::UserRole + 1;

// The sidebar: a CPL drags as its file; bin items dropped on it go to `onDrop` with the item under the pointer.
class ImfTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void(const QMimeData*, QTreeWidgetItem*)> onDrop;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasFormat(binItemsMime())) event->acceptProposedAction();
        else QTreeWidget::dragEnterEvent(event);
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        if (event->mimeData()->hasFormat(binItemsMime())) event->acceptProposedAction();
        else QTreeWidget::dragMoveEvent(event);
    }
    void dropEvent(QDropEvent* event) override {
        if (!event->mimeData()->hasFormat(binItemsMime())) return; // only bin items come in; the tree's own items stay put
        event->acceptProposedAction();
        if (onDrop) onDrop(event->mimeData(), itemAt(event->position().toPoint()));
    }
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

QString draftKey(int draft, int imp, int cpl) {
    return QStringLiteral("%1/%2/%3").arg(draft).arg(imp).arg(cpl);
}

std::array<int, 3> draftIndexes(const QTreeWidgetItem* item) {
    const QStringList parts = item ? item->data(0, kPathRole).toString().split(QLatin1Char('/')) : QStringList();
    if (parts.size() != 3) return {-1, -1, -1};
    return {parts[0].toInt(), parts[1].toInt(), parts[2].toInt()};
}

bool isDraftKind(int kind) {
    return kind == DraftRoot || kind == DraftImp || kind == DraftCpl;
}

} // namespace

QString ImfDraft::root() const {
    return directory.isEmpty() ? QString() : QDir(directory).filePath(name);
}

QColor ImfWindow::green() {
    return {0x30, 0xb0, 0x40};
}

QColor ImfWindow::yellow() {
    return {0xe0, 0xc0, 0x20};
}

QColor ImfWindow::red() {
    return {0xd0, 0x30, 0x30};
}

ImfWindow::ImfWindow(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("imfWindow"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* buttons = new QHBoxLayout;
    explore_ = new QPushButton(tr("Explore"), this);
    explore_->setObjectName(QStringLiteral("imfExplore"));
    connect(explore_, &QPushButton::clicked, this, [this] { explore(); });
    buttons->addWidget(explore_);
    // "New IMF ... Allows you to create a new IMF Original or Supplemental" (p.334). Supplementals come with 11.5c3.
    newImf_ = new QPushButton(tr("New IMF"), this);
    newImf_->setObjectName(QStringLiteral("imfNew"));
    newMenu_ = new QMenu(newImf_);
    newMenu_->addAction(tr("IMF Original"), this, [this] { newOriginal(); });
    QAction* supplemental = newMenu_->addAction(tr("IMF Supplemental"));
    supplemental->setEnabled(false);
    newImf_->setMenu(newMenu_);
    buttons->addWidget(newImf_);
    export_ = new QPushButton(tr("Export"), this);
    export_->setObjectName(QStringLiteral("imfExport"));
    export_->setEnabled(false);
    connect(export_, &QPushButton::clicked, this, [this] { exportSelected(); });
    buttons->addWidget(export_);
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
    tree_->setAcceptDrops(true);
    tree_->setDragDropMode(QAbstractItemView::DragDrop);
    tree_->viewport()->setAcceptDrops(true);
    static_cast<ImfTree*>(tree_)->onDrop = [this](const QMimeData* mime, QTreeWidgetItem* on) { dropped(mime, on); };
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
// package explored is on disk; drafts follow, red or yellow.
void ImfWindow::rebuild() {
    tree_->clear();
    for (const interop::imf::Package& p : packages_) {
        auto* root = new QTreeWidgetItem(tree_, {p.name, QStringLiteral("●")});
        root->setData(0, kPathRole, p.root);
        root->setData(0, kKindRole, Root);
        root->setForeground(1, green());
        root->setToolTip(1, tr("Exported, on disk"));
        root->setFlags(root->flags() & ~Qt::ItemIsDragEnabled);
        for (const interop::imf::PackingList& pkl : p.imps) {
            auto* imp = new QTreeWidgetItem(root, {QFileInfo(pkl.file).completeBaseName(), QStringLiteral("●")});
            imp->setData(0, kPathRole, pkl.file);
            imp->setData(0, kKindRole, Imp);
            imp->setForeground(1, green());
            imp->setFlags(imp->flags() & ~Qt::ItemIsDragEnabled);
            for (const interop::imf::PackingList::Entry& e : pkl.assets) {
                for (const interop::imf::Composition& c : p.compositions) {
                    if (c.id != e.id) continue;
                    auto* cpl = new QTreeWidgetItem(imp, {c.contentTitle.isEmpty() ? QFileInfo(c.file).completeBaseName() : c.contentTitle,
                                                          QStringLiteral("●")});
                    cpl->setData(0, kPathRole, c.file);
                    cpl->setData(0, kKindRole, Cpl);
                    cpl->setForeground(1, green());
                }
            }
        }
        root->setExpanded(true);
        for (int i = 0; i < root->childCount(); ++i) root->child(i)->setExpanded(true);
    }
    // Drafts: "A new untitled folder appears in the sidebar (though it is not yet written to disk)" (p.337), its IMPs
    // "untitled" and its CPLs "CPL_<sequence name>" (p.336).
    for (int d = 0; d < drafts_.size(); ++d) {
        const ImfDraft& draft = drafts_.at(d);
        auto* root = new QTreeWidgetItem(tree_, {draft.name, QStringLiteral("●")});
        root->setData(0, kPathRole, draftKey(d, -1, -1));
        root->setData(0, kKindRole, DraftRoot);
        root->setFlags(root->flags() & ~Qt::ItemIsDragEnabled);
        for (int i = 0; i < draft.imps.size(); ++i) {
            auto* imp = new QTreeWidgetItem(root, {tr("untitled"), QStringLiteral("●")});
            imp->setData(0, kPathRole, draftKey(d, i, -1));
            imp->setData(0, kKindRole, DraftImp);
            imp->setFlags(imp->flags() & ~Qt::ItemIsDragEnabled);
            for (int c = 0; c < draft.imps.at(i).cpls.size(); ++c) {
                auto* cpl = new QTreeWidgetItem(imp, {QString(), QStringLiteral("●")});
                cpl->setData(0, kPathRole, draftKey(d, i, c));
                cpl->setData(0, kKindRole, DraftCpl);
                cpl->setFlags(cpl->flags() & ~Qt::ItemIsDragEnabled);
            }
            imp->setExpanded(true);
        }
        root->setExpanded(true);
    }
    refreshStatuses();
}

// The drafts' names and statuses (red or yellow, with what is missing as the tooltip), and the Export button.
void ImfWindow::refreshStatuses() {
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        QTreeWidgetItem* item = *it;
        const int kind = item->data(0, kKindRole).toInt();
        if (!isDraftKind(kind)) continue;
        const auto [d, i, c] = draftIndexes(item);
        if (d < 0 || d >= drafts_.size()) continue;
        if (kind == DraftRoot) item->setText(0, drafts_.at(d).name);
        if (kind == DraftCpl) item->setText(0, QStringLiteral("CPL_%1").arg(drafts_.at(d).imps.at(i).cpls.at(c).contentTitle));
        const QStringList missing = missingFields(d, i, c);
        item->setForeground(1, missing.isEmpty() ? yellow() : red());
        item->setToolTip(1, missing.isEmpty() ? tr("Ready to be exported") : tr("Required: %1").arg(missing.join(QStringLiteral(", "))));
    }
    const int d = selectedDraft();
    export_->setEnabled(d >= 0 && draftReady(d));
}

int ImfWindow::newOriginal() {
    ImfDraft draft;
    draft.imps << ImfDraftImp{};
    drafts_ << draft;
    rebuild();
    selectDraft(static_cast<int>(drafts_.size()) - 1);
    return static_cast<int>(drafts_.size()) - 1;
}

int ImfWindow::addSequences(int draft, const QList<ImfDraftCpl>& sequences) {
    if (sequences.isEmpty()) return draft;
    if (draft < 0 || draft >= drafts_.size()) draft = newOriginal();
    ImfDraft& d = drafts_[draft];
    if (d.imps.isEmpty() || !d.imps.last().cpls.isEmpty()) d.imps << ImfDraftImp{};
    ImfDraftImp& imp = d.imps.last();
    for (ImfDraftCpl cpl : sequences) {
        if (cpl.contentTitle.isEmpty()) cpl.contentTitle = cpl.sequenceName;
        imp.cpls << cpl;
    }
    rebuild();
    selectDraft(draft, static_cast<int>(d.imps.size()) - 1, static_cast<int>(imp.cpls.size()) - 1);
    return draft;
}

void ImfWindow::setDraftField(int draft, int imp, int cpl, const QString& field, const QString& value) {
    if (draft < 0 || draft >= drafts_.size()) return;
    ImfDraft& d = drafts_[draft];
    if (imp < 0) {
        if (field == QLatin1String("name")) d.name = value;
        else if (field == QLatin1String("directory")) d.directory = value;
        else if (field == QLatin1String("issuer")) d.issuer = value;
        else if (field == QLatin1String("creator")) d.creator = value;
        else if (field == QLatin1String("annotation")) d.annotation = value;
    } else if (imp < d.imps.size() && cpl < 0) {
        ImfDraftImp& i = d.imps[imp];
        if (field == QLatin1String("issuer")) i.issuer = value;
        else if (field == QLatin1String("creator")) i.creator = value;
        else if (field == QLatin1String("annotation")) i.annotation = value;
    } else if (imp < d.imps.size() && cpl < d.imps.at(imp).cpls.size()) {
        ImfDraftImp& i = d.imps[imp];
        ImfDraftCpl& c = i.cpls[cpl];
        if (field == QLatin1String("contentTitle")) c.contentTitle = value;
        else if (field == QLatin1String("contentKind")) c.contentKind = value;
        else if (field == QLatin1String("profile")) c.profile = value;
        else if (field == QLatin1String("annotation")) c.annotation = value;
        else if (field == QLatin1String("issuer") || field == QLatin1String("creator")) {
            QString& own = field == QLatin1String("issuer") ? c.issuer : c.creator;
            const QString before = own;
            own = value;
            // "Completing Issuer and Creator automatically populate the IMP and root folder required fields. You can
            // change these independently ... the IMP and root folder populate based on the first CPL Issuer and
            // Creator fields being populated" (p.336-337): they follow the CPL while they are empty or still hold what
            // it held (as it is typed), so a value changed on the IMP or root stays.
            QString& impField = field == QLatin1String("issuer") ? i.issuer : i.creator;
            QString& rootField = field == QLatin1String("issuer") ? d.issuer : d.creator;
            if (impField.isEmpty() || impField == before) impField = value;
            if (rootField.isEmpty() || rootField == before) rootField = value;
        }
    }
    refreshStatuses();
}

QStringList ImfWindow::missingFields(int draft, int imp, int cpl) const {
    QStringList out;
    if (draft < 0 || draft >= drafts_.size()) return out;
    const ImfDraft& d = drafts_.at(draft);
    // Ours: the guide names Issuer, Creator and the directory as required (p.336-337); a root also needs a name, a
    // CPL a Content Title, and the IMF Supported Formats (the CPL check).
    if (imp < 0) {
        if (d.name.trimmed().isEmpty()) out << tr("IMF Name");
        if (d.directory.isEmpty()) out << tr("Directory");
        if (d.issuer.trimmed().isEmpty()) out << tr("Issuer");
        if (d.creator.trimmed().isEmpty()) out << tr("Creator");
        return out;
    }
    if (imp >= d.imps.size()) return out;
    const ImfDraftImp& i = d.imps.at(imp);
    if (cpl < 0) {
        if (i.issuer.trimmed().isEmpty()) out << tr("Issuer");
        if (i.creator.trimmed().isEmpty()) out << tr("Creator");
        return out;
    }
    if (cpl >= i.cpls.size()) return out;
    const ImfDraftCpl& c = i.cpls.at(cpl);
    if (c.contentTitle.trimmed().isEmpty()) out << tr("Content Title");
    if (c.issuer.trimmed().isEmpty()) out << tr("Issuer");
    if (c.creator.trimmed().isEmpty()) out << tr("Creator");
    if (cplCheck_) {
        if (const QString why = cplCheck_(c); !why.isEmpty()) out << why;
    }
    return out;
}

bool ImfWindow::draftReady(int draft) const {
    if (draft < 0 || draft >= drafts_.size() || !missingFields(draft).isEmpty()) return false;
    int cpls = 0;
    const ImfDraft& d = drafts_.at(draft);
    for (int i = 0; i < d.imps.size(); ++i) {
        if (d.imps.at(i).cpls.isEmpty()) continue; // an untitled IMP no sequence went into is left out
        if (!missingFields(draft, i).isEmpty()) return false;
        for (int c = 0; c < d.imps.at(i).cpls.size(); ++c) {
            if (!missingFields(draft, i, c).isEmpty()) return false;
            ++cpls;
        }
    }
    return cpls > 0;
}

int ImfWindow::selectedDraft() const {
    const QTreeWidgetItem* item = tree_->currentItem();
    if (!item || !isDraftKind(item->data(0, kKindRole).toInt())) return -1;
    return draftIndexes(item)[0];
}

void ImfWindow::exportSelected() {
    const int d = selectedDraft();
    // "This button becomes active only when a selection is made and all required fields for the Original or
    // Supplemental are satisfied (such as setting a target path)" (p.334).
    if (d < 0 || !draftReady(d)) return;
    emit exportRequested(d);
}

void ImfWindow::draftWritten(int draft, const QString& root) {
    if (draft < 0 || draft >= drafts_.size()) return;
    drafts_.removeAt(draft);
    QString error;
    if (!addPackage(root, &error)) {
        rebuild();
        emit failed(error);
    }
}

bool ImfWindow::selectDraft(int draft, int imp, int cpl) {
    const QString key = draftKey(draft, imp, cpl);
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        if (isDraftKind((*it)->data(0, kKindRole).toInt()) && (*it)->data(0, kPathRole).toString() == key) {
            tree_->setCurrentItem(*it);
            return true;
        }
    }
    return false;
}

// Bin items dropped: on a draft (any of its items: its root), on a package on disk (its root), or elsewhere (a new
// Original: "Or drag a sequence to the sidebar", p.337).
void ImfWindow::dropped(const QMimeData* mime, QTreeWidgetItem* on) {
    const QStringList lines = QString::fromUtf8(mime->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 2) return;
    int draft = -1;
    QString packageRoot;
    if (on) {
        while (on->parent()) on = on->parent();
        if (on->data(0, kKindRole).toInt() == DraftRoot) draft = draftIndexes(on)[0];
        else packageRoot = on->data(0, kPathRole).toString();
    }
    emit binItemsDropped(lines.first(), lines.mid(1), draft, packageRoot);
}

bool ImfWindow::select(const QString& path) {
    const QString clean = QFileInfo(path).absoluteFilePath();
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        if (!isDraftKind((*it)->data(0, kKindRole).toInt()) && (*it)->data(0, kPathRole).toString() == clean) {
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
    export_->setEnabled(selectedDraft() >= 0 && draftReady(selectedDraft()));
    if (isDraftKind(item->data(0, kKindRole).toInt())) {
        showDraft(item);
        return;
    }
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

// A draft's right pane: its fields editable (line edits and menus), each edit going through setDraftField. The IMP's
// name "will display as untitled" until it is written (p.335), and so does a CPL's file name.
void ImfWindow::showDraft(const QTreeWidgetItem* item) {
    // Plain ints rather than a structured binding: the lambdas below capture them (Clang's -Wshadow).
    const std::array<int, 3> at = draftIndexes(item);
    const int d = at[0];
    const int i = at[1];
    const int c = at[2];
    if (d < 0 || d >= drafts_.size()) return;
    const ImfDraft& draft = drafts_.at(d);
    auto label = [this](const QString& text, const QString& value) {
        auto* field = new QLabel(value, pane_);
        field->setWordWrap(true);
        form_->addRow(text, field);
    };
    auto edit = [this, d, i, c](const QString& text, const QString& field, const QString& value) {
        auto* line = new QLineEdit(value, pane_);
        line->setObjectName(QStringLiteral("imfField_%1").arg(field));
        connect(line, &QLineEdit::textEdited, this, [this, d, i, c, field](const QString& v) { setDraftField(d, i, c, field, v); });
        form_->addRow(text, line);
        return line;
    };
    auto menu = [this, d, i, c](const QString& text, const QString& field, const QStringList& values,
                                            const QStringList& labels, const QString& value) {
        auto* box = new QComboBox(pane_);
        box->setObjectName(QStringLiteral("imfField_%1").arg(field));
        for (int n = 0; n < values.size(); ++n) box->addItem(labels.value(n, values.at(n)), values.at(n));
        box->setCurrentIndex(std::max(0, static_cast<int>(values.indexOf(value))));
        connect(box, &QComboBox::activated, this, [this, d, i, c, field, box](int n) { setDraftField(d, i, c, field, box->itemData(n).toString()); });
        form_->addRow(text, box);
    };
    if (i < 0) {
        // "Rename the folder and set the Directory path" (p.337).
        edit(tr("IMF Name"), QStringLiteral("name"), draft.name);
        auto* directory = edit(tr("Directory"), QStringLiteral("directory"), QDir::toNativeSeparators(draft.directory));
        auto* browse = new QPushButton(tr("Browse..."), pane_);
        browse->setObjectName(QStringLiteral("imfBrowse"));
        connect(browse, &QPushButton::clicked, this, [this, d, directory] {
            const QString chosen = directoryChooser_ ? directoryChooser_() : QFileDialog::getExistingDirectory(this, tr("Directory"));
            if (chosen.isEmpty()) return;
            directory->setText(QDir::toNativeSeparators(chosen));
            setDraftField(d, -1, -1, QStringLiteral("directory"), chosen);
        });
        form_->addRow(QString(), browse);
        edit(tr("Issuer"), QStringLiteral("issuer"), draft.issuer);
        edit(tr("Creator"), QStringLiteral("creator"), draft.creator);
        edit(tr("Annotation"), QStringLiteral("annotation"), draft.annotation);
        return;
    }
    const ImfDraftImp& imp = draft.imps.at(i);
    if (c < 0) {
        label(tr("IMP Name"), tr("untitled"));
        edit(tr("Issuer"), QStringLiteral("issuer"), imp.issuer);
        edit(tr("Creator"), QStringLiteral("creator"), imp.creator);
        edit(tr("Annotation"), QStringLiteral("annotation"), imp.annotation);
        return;
    }
    const ImfDraftCpl& cpl = imp.cpls.at(c);
    // "If you change the Content Title field in the right pane, the name will update in the sidebar" (p.336).
    edit(tr("Content Title"), QStringLiteral("contentTitle"), cpl.contentTitle);
    label(tr("CPL File Name"), tr("untitled"));
    label(tr("Sequence"), cpl.sequenceName);
    // "modify the parameters in the right pane for the encoding profile" (p.336): the IMF resolutions (p.1256-1257).
    const QStringList profiles = media::imfEncodingProfiles();
    menu(tr("Encoding Profile"), QStringLiteral("profile"), profiles, profiles, cpl.profile);
    // Content Kind: ST 2067-3 takes ST 429-7's kinds (the same menu as the DCP export's).
    menu(tr("Content Kind"), QStringLiteral("contentKind"),
         {QStringLiteral("feature"), QStringLiteral("trailer"), QStringLiteral("short"), QStringLiteral("teaser"),
          QStringLiteral("advertisement"), QStringLiteral("test"), QStringLiteral("episode"), QStringLiteral("promotion")},
         {tr("Feature"), tr("Trailer"), tr("Short"), tr("Teaser"), tr("Advertisement"), tr("Test"), tr("Episode"), tr("Promotion")},
         cpl.contentKind);
    edit(tr("Issuer"), QStringLiteral("issuer"), cpl.issuer);
    edit(tr("Creator"), QStringLiteral("creator"), cpl.creator);
    edit(tr("Annotation"), QStringLiteral("annotation"), cpl.annotation);
}

QStringList ImfWindow::metadata() const {
    QStringList out;
    for (int i = 0; i < form_->rowCount(); ++i) {
        const auto* label = qobject_cast<QLabel*>(form_->itemAt(i, QFormLayout::LabelRole) ? form_->itemAt(i, QFormLayout::LabelRole)->widget() : nullptr);
        const QWidget* field = form_->itemAt(i, QFormLayout::FieldRole) ? form_->itemAt(i, QFormLayout::FieldRole)->widget() : nullptr;
        if (!label || !field) continue;
        QString value;
        if (const auto* text = qobject_cast<const QLabel*>(field)) value = text->text();
        else if (const auto* line = qobject_cast<const QLineEdit*>(field)) value = line->text();
        else if (const auto* box = qobject_cast<const QComboBox*>(field)) value = box->currentText();
        else continue;
        out << QStringLiteral("%1: %2").arg(label->text(), value);
    }
    return out;
}

void ImfWindow::keyPressEvent(QKeyEvent* event) {
    // Deleting a selected root folder takes the package off the sidebar (p.338).
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && tree_->currentItem()) {
        const QTreeWidgetItem* item = tree_->currentItem();
        if (item->data(0, kKindRole).toInt() == Root) {
            removePackage(item->data(0, kPathRole).toString());
            return;
        }
        if (item->data(0, kKindRole).toInt() == DraftRoot) { // an uncommitted package (p.338)
            drafts_.removeAt(draftIndexes(item)[0]);
            rebuild();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

} // namespace lumenis::ui
