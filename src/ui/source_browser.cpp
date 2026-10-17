#include "lumenis/ui/source_browser.h"

#include "lumenis/media/managed.h"

#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

namespace lumenis::ui {

namespace {

constexpr int kMaxRecent = 12;
constexpr int kLabelHeight = 18;

// Thumbnail widths for the slider's steps; 16:9 frames.
QSize thumbnailFor(int step) {
    static constexpr int kWidths[SourceBrowser::kMaxThumbnailStep + 1] = {96, 120, 144, 176, 216, 264};
    const int w = kWidths[std::clamp(step, 0, SourceBrowser::kMaxThumbnailStep)];
    return {w, w * 9 / 16};
}

QToolButton* navButton(QWidget* parent, const QString& name, GlyphIcon glyph, const QString& tip) {
    auto* b = new QToolButton(parent);
    b->setObjectName(name);
    b->setIcon(glyphIcon(glyph));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setFixedSize(26, kToolButtonHeight);
    return b;
}

// Draws Frame view cells: the clip's frame, its file name below (p.315).
class ThumbnailDelegate : public QStyledItemDelegate {
public:
    explicit ThumbnailDelegate(SourceBrowser* browser) : QStyledItemDelegate(browser), browser_(browser) {}

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        const QSize t = browser_->thumbnailSize();
        return {t.width() + 16, t.height() + kLabelHeight + 12};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        p->save();
        const QSize t = browser_->thumbnailSize();
        const QRect cell = option.rect;
        const QRect frame(cell.left() + (cell.width() - t.width()) / 2, cell.top() + 6, t.width(), t.height());
        const bool selected = option.state & QStyle::State_Selected;
        if (selected) p->fillRect(frame.adjusted(-3, -3, 3, 3), option.palette.color(QPalette::Highlight));
        p->fillRect(frame, QColor(12, 12, 12));
        const auto* source = qobject_cast<const QSortFilterProxyModel*>(index.model());
        const auto* model = source ? qobject_cast<const SourceMediaModel*>(source->sourceModel()) : nullptr;
        const SourceMediaModel::Entry* e = model ? model->entry(source->mapToSource(index).row()) : nullptr;
        QImage picture;
        if (e && e->linkable && MediaFrames::instance()) picture = MediaFrames::instance()->thumbnail(e->clip, e->frame, t);
        if (!picture.isNull()) {
            const QSize fit = picture.size().scaled(t, Qt::KeepAspectRatio);
            p->drawImage(QRect(frame.left() + (t.width() - fit.width()) / 2, frame.top() + (t.height() - fit.height()) / 2,
                               fit.width(), fit.height()),
                         picture);
        } else {
            p->setPen(QColor(70, 70, 70));
            p->drawRect(frame.adjusted(0, 0, -1, -1));
            const GlyphIcon g = e && e->folder ? GlyphIcon::Folder
                                : e && e->clip.kind == model::BinItemKind::AudioClip ? GlyphIcon::ItemAudio
                                                                                      : GlyphIcon::ItemClip;
            const int s = std::max(12, t.height() / 3);
            glyphIcon(g).paint(p, QRect(frame.center() - QPoint(s / 2, s / 2), QSize(s, s)), Qt::AlignCenter,
                               e && (e->folder || e->linkable) ? QIcon::Normal : QIcon::Disabled);
        }
        const QRect text(cell.left() + 2, frame.bottom() + 4, cell.width() - 4, kLabelHeight);
        p->setPen(option.palette.color(selected ? QPalette::BrightText : QPalette::WindowText));
        p->drawText(text, Qt::AlignHCenter | Qt::AlignTop,
                    option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, text.width()));
        p->restore();
    }

private:
    SourceBrowser* browser_;
};

// Folders first, then files, each by name; in a media volume only what
// turned out to be media (p.313).
class BrowserSortModel : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setMediaOnly(bool on) {
        mediaOnly_ = on;
        refilter();
    }
    void refilter() {
        // Qt 6.10 replaced invalidateFilter() with begin/endFilterChange().
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange(Direction::Rows);
#else
        invalidateFilter();
#endif
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (mediaOnly_) {
            const QModelIndex i = sourceModel()->index(row, 0, parent);
            if (i.data(SourceMediaModel::kProbedRole).toBool() && !i.data(SourceMediaModel::kLinkableRole).toBool()) {
                return false;
            }
        }
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }


    bool lessThan(const QModelIndex& a, const QModelIndex& b) const override {
        const bool fa = a.data(SourceMediaModel::kFolderRole).toBool();
        const bool fb = b.data(SourceMediaModel::kFolderRole).toBool();
        if (fa != fb) return fa;
        return QSortFilterProxyModel::lessThan(a, b);
    }

private:
    bool mediaOnly_ = false;
};

} // namespace

// ---------------------------------------------------------------- SourceMediaModel

SourceMediaModel::SourceMediaModel(QObject* parent) : QAbstractTableModel(parent) {}

const QStringList& SourceMediaModel::columns() {
    static const QStringList c{
        QStringLiteral("Name"),     QStringLiteral("Duration"), QStringLiteral("Start"),   QStringLiteral("End"),
        QStringLiteral("Tracks"),   QStringLiteral("Video"),    QStringLiteral("FPS"),     QStringLiteral("Format"),
        QStringLiteral("Audio SR"), QStringLiteral("Tape"),     QStringLiteral("Plug-In"), QStringLiteral("Source Path"),
    };
    return c;
}

void SourceMediaModel::setEntries(const QList<Entry>& entries) {
    beginResetModel();
    entries_ = entries;
    endResetModel();
}

void SourceMediaModel::setProbed(const QString& path, const std::optional<media::MediaInfo>& info) {
    const int row = rowOf(path);
    if (row < 0) return;
    Entry& e = entries_[row];
    e.probed = true;
    std::optional<model::BinItem> clip;
    if (info && media::isLinkable(*info)) clip = media::linkClip(*info, options_);
    e.linkable = clip.has_value();
    if (clip) e.clip = *clip;
    emit dataChanged(index(row, 0), index(row, columnCount() - 1));
}

void SourceMediaModel::setFrame(int row, std::int64_t frame) {
    if (row < 0 || row >= entries_.size() || entries_.at(row).frame == frame) return;
    entries_[row].frame = frame;
    emit dataChanged(index(row, 0), index(row, 0));
}

const SourceMediaModel::Entry* SourceMediaModel::entry(int row) const {
    return row >= 0 && row < entries_.size() ? &entries_.at(row) : nullptr;
}

int SourceMediaModel::rowOf(const QString& path) const {
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_.at(i).path == path) return i;
    }
    return -1;
}

int SourceMediaModel::pending() const {
    return static_cast<int>(
        std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.folder && !e.probed; }));
}

int SourceMediaModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

int SourceMediaModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(columns().size());
}

QVariant SourceMediaModel::data(const QModelIndex& index, int role) const {
    const Entry* e = entry(index.row());
    if (!e) return {};
    switch (role) {
    case kPathRole: return e->path;
    case kFolderRole: return e->folder;
    case kLinkableRole: return e->linkable;
    case kProbedRole: return e->probed;
    case Qt::DecorationRole:
        if (index.column() != 0) return {};
        if (e->folder) return glyphIcon(GlyphIcon::Folder);
        return glyphIcon(e->clip.kind == model::BinItemKind::AudioClip ? GlyphIcon::ItemAudio : GlyphIcon::ItemClip,
                         e->linkable ? QColor(0xdc, 0xdc, 0xdc) : QColor(0x70, 0x70, 0x70));
    case Qt::DisplayRole: {
        const QString heading = columns().at(index.column());
        if (heading == QLatin1String("Name")) return QFileInfo(e->path).fileName();
        if (e->folder) return {};
        // Files no plug-in can read (p.322).
        if (heading == QLatin1String("Plug-In") && e->probed && !e->linkable) return tr("Not detected");
        if (!e->linkable) return {};
        return model::cellText(e->clip, heading);
    }
    case Qt::ToolTipRole: return QDir::toNativeSeparators(e->path);
    default: return {};
    }
}

QVariant SourceMediaModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    return columns().value(section);
}

Qt::ItemFlags SourceMediaModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    const Entry* e = entry(index.row());
    // Files are dragged to a bin, a monitor or the Timeline (p.316-317).
    if (e && !e->folder) f |= Qt::ItemIsDragEnabled;
    return f;
}

QStringList SourceMediaModel::mimeTypes() const {
    return {mediaFilesMime()};
}

QMimeData* SourceMediaModel::mimeData(const QModelIndexList& indexes) const {
    QStringList paths;
    for (const QModelIndex& i : indexes) {
        const Entry* e = entry(i.row());
        if (e && !e->folder && !paths.contains(e->path)) paths << e->path;
    }
    auto* mime = new QMimeData;
    mime->setData(mediaFilesMime(), paths.join(QLatin1Char('\n')).toUtf8());
    return mime;
}

// ---------------------------------------------------------------- SourceBrowser

SourceBrowser::SourceBrowser(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("sourceBrowser"));
    pool_.setMaxThreadCount(2);
    model_ = new SourceMediaModel(this);
    proxy_ = new BrowserSortModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setFilterKeyColumn(0);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxy_->sort(0);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    // Top: navigation tools and breadcrumbs (p.313).
    auto* nav = new QHBoxLayout;
    nav->setSpacing(2);
    buildNavigation(nav);
    outer->addLayout(nav);

    // Middle: the places on the left, the media on the right (p.313-314);
    // below, the settings, which the splitter or the Collapse/Expand
    // button hides (p.316-317).
    auto* vertical = new QSplitter(Qt::Vertical, this);
    vertical->setObjectName(QStringLiteral("sourceBrowserSplit"));
    vertical->setChildrenCollapsible(true);
    auto* middle = new QSplitter(Qt::Horizontal, vertical);
    middle->setChildrenCollapsible(false);

    places_ = new QTabWidget(middle);
    places_->setObjectName(QStringLiteral("sourceBrowserPlaces"));
    folders_ = new QFileSystemModel(this);
    folders_->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    folders_->setRootPath(QString());
    tree_ = new QTreeView(places_);
    tree_->setObjectName(QStringLiteral("sourceBrowserExplore"));
    tree_->setModel(folders_);
    for (int c = 1; c < folders_->columnCount(); ++c) tree_->hideColumn(c);
    tree_->setHeaderHidden(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeView::clicked, this, [this](const QModelIndex& i) { setFolder(folders_->filePath(i)); });
    connect(tree_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showMenu(tree_, pos); });
    favoriteList_ = new QListWidget(places_);
    favoriteList_->setObjectName(QStringLiteral("sourceBrowserFavorites"));
    recentList_ = new QListWidget(places_);
    recentList_->setObjectName(QStringLiteral("sourceBrowserRecent"));
    for (QListWidget* l : {favoriteList_, recentList_}) {
        connect(l, &QListWidget::itemClicked, this,
                [this](QListWidgetItem* item) { setFolder(item->data(Qt::UserRole).toString()); });
    }
    places_->addTab(tree_, tr("Explore"));
    places_->addTab(favoriteList_, tr("Favorites"));
    places_->addTab(recentList_, tr("Recent"));
    middle->addWidget(places_);
    middle->addWidget(buildDisplay());
    middle->setStretchFactor(1, 3);
    middle->setSizes({220, 560});
    vertical->addWidget(middle);

    settingsBox_ = new QFrame(vertical);
    settingsBox_->setObjectName(QStringLiteral("sourceBrowserSettings"));
    settingsBox_->setFrameShape(QFrame::StyledPanel);
    auto* settings = new QVBoxLayout(settingsBox_);
    settings->setContentsMargins(8, 6, 8, 6);
    settings->addWidget(new QLabel(tr("Source Browser Settings"), settingsBox_));
    auto* options = new QHBoxLayout;
    options->addWidget(new QLabel(tr("Double Click to:"), settingsBox_));
    doubleClick_ = new QComboBox(settingsBox_);
    doubleClick_->setObjectName(QStringLiteral("doubleClick"));
    doubleClick_->addItem(tr("Link or Import"));
    doubleClick_->addItem(tr("Load Clips in Source Monitor"));
    options->addWidget(doubleClick_);
    options->addSpacing(16);
    auto* checks = new QVBoxLayout;
    closeAfter_ = new QCheckBox(tr("Close Source Browser after Link or Import"), settingsBox_);
    closeAfter_->setObjectName(QStringLiteral("closeAfterLink"));
    clearSource_ = new QCheckBox(tr("Clear Source Monitor Upon Closing Source Browser"), settingsBox_);
    clearSource_->setObjectName(QStringLiteral("clearSourceOnClose"));
    checks->addWidget(closeAfter_);
    checks->addWidget(clearSource_);
    options->addLayout(checks);
    options->addStretch();
    settings->addLayout(options);
    vertical->addWidget(settingsBox_);
    vertical->setStretchFactor(0, 1);
    vertical->setStretchFactor(1, 0);
    outer->addWidget(vertical, 1);
    for (QWidget* w : std::initializer_list<QWidget*>{doubleClick_, closeAfter_, clearSource_}) {
        if (auto* c = qobject_cast<QComboBox*>(w)) connect(c, &QComboBox::currentIndexChanged, this, &SourceBrowser::stateChanged);
        if (auto* c = qobject_cast<QCheckBox*>(w)) connect(c, &QCheckBox::toggled, this, &SourceBrowser::stateChanged);
    }

    outer->addWidget(buildLowerPane());

    // Ctrl+L (Command+L) enlarges thumbnails (p.316); Ctrl+K reduces them,
    // as in bins (p.250).
    auto* enlarge = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
    enlarge->setContext(Qt::WidgetWithChildrenShortcut);
    connect(enlarge, &QShortcut::activated, this, [this] { setThumbnailStep(thumbnailStep() + 1); });
    auto* reduce = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    reduce->setContext(Qt::WidgetWithChildrenShortcut);
    connect(reduce, &QShortcut::activated, this, [this] { setThumbnailStep(thumbnailStep() - 1); });

    if (MediaFrames* frames = MediaFrames::instance()) {
        connect(frames, &MediaFrames::thumbnailReady, frames_->viewport(), qOverload<>(&QWidget::update));
    }
    updateNavigation();
    updateProgress();
}

SourceBrowser::~SourceBrowser() {
    pool_.clear();
    pool_.waitForDone();
}

void SourceBrowser::buildNavigation(QHBoxLayout* row) {
    backButton_ = navButton(this, QStringLiteral("back"), GlyphIcon::ArrowBack, tr("Back"));
    forwardButton_ = navButton(this, QStringLiteral("forward"), GlyphIcon::ArrowForward, tr("Forward"));
    upButton_ = navButton(this, QStringLiteral("up"), GlyphIcon::ArrowUp, tr("Up One Level"));
    homeButton_ = navButton(this, QStringLiteral("home"), GlyphIcon::Home, tr("Home"));
    collapseButton_ = navButton(this, QStringLiteral("collapseDirectories"), GlyphIcon::CollapseTree,
                                tr("Collapse Directories"));
    favoriteButton_ = navButton(this, QStringLiteral("favorite"), GlyphIcon::Star, tr("Add or Remove Favorites"));
    favoriteButton_->setCheckable(true);
    volumeButton_ = navButton(this, QStringLiteral("mediaFolderView"), GlyphIcon::MediaVolume, tr("Media Folder View"));
    volumeButton_->setCheckable(true);
    connect(backButton_, &QToolButton::clicked, this, &SourceBrowser::back);
    connect(forwardButton_, &QToolButton::clicked, this, &SourceBrowser::forward);
    connect(upButton_, &QToolButton::clicked, this, &SourceBrowser::up);
    connect(homeButton_, &QToolButton::clicked, this, &SourceBrowser::home);
    connect(collapseButton_, &QToolButton::clicked, this, [this] { tree_->collapseAll(); });
    connect(favoriteButton_, &QToolButton::clicked, this, &SourceBrowser::toggleFavorite);
    connect(volumeButton_, &QToolButton::toggled, this, &SourceBrowser::setMediaFolderView);
    for (QToolButton* b : {backButton_, forwardButton_, upButton_}) row->addWidget(b);
    row->addSpacing(6);
    for (QToolButton* b : {homeButton_, collapseButton_, favoriteButton_}) row->addWidget(b);
    row->addSpacing(6);
    row->addWidget(volumeButton_);
    row->addSpacing(6);
    crumbBar_ = new QWidget(this);
    crumbBar_->setObjectName(QStringLiteral("breadcrumbs"));
    crumbs_ = new QHBoxLayout(crumbBar_);
    crumbs_->setContentsMargins(0, 0, 0, 0);
    crumbs_->setSpacing(0);
    row->addWidget(crumbBar_, 1);
}

QWidget* SourceBrowser::buildDisplay() {
    auto* box = new QWidget(this);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    views_ = new QStackedWidget(box);

    text_ = new QTreeView(views_);
    text_->setObjectName(QStringLiteral("sourceBrowserText"));
    text_->setModel(proxy_);
    text_->setRootIsDecorated(false);
    text_->setUniformRowHeights(true);
    text_->setSortingEnabled(true);
    text_->sortByColumn(0, Qt::AscendingOrder);
    text_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    text_->setSelectionBehavior(QAbstractItemView::SelectRows);
    text_->setDragEnabled(true);
    text_->setDragDropMode(QAbstractItemView::DragOnly);
    text_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    text_->header()->setStretchLastSection(false);
    text_->header()->resizeSection(0, 200);
    text_->setContextMenuPolicy(Qt::CustomContextMenu);

    frames_ = new QListView(views_);
    frames_->setObjectName(QStringLiteral("sourceBrowserFrames"));
    frames_->setModel(proxy_);
    frames_->setSelectionModel(text_->selectionModel());
    frames_->setViewMode(QListView::IconMode);
    frames_->setResizeMode(QListView::Adjust);
    frames_->setMovement(QListView::Static);
    frames_->setWrapping(true);
    frames_->setUniformItemSizes(true);
    frames_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    frames_->setDragEnabled(true);
    frames_->setDragDropMode(QAbstractItemView::DragOnly);
    frames_->setItemDelegate(new ThumbnailDelegate(this));
    frames_->setMouseTracking(true);
    frames_->viewport()->installEventFilter(this);
    frames_->setContextMenuPolicy(Qt::CustomContextMenu);

    for (QAbstractItemView* view : std::initializer_list<QAbstractItemView*>{text_, frames_}) {
        connect(view, &QAbstractItemView::doubleClicked, this, &SourceBrowser::activate);
        connect(view, &QWidget::customContextMenuRequested, this,
                [this, view](const QPoint& pos) { showMenu(view->viewport(), pos); });
    }
    views_->addWidget(text_);
    views_->addWidget(frames_);
    v->addWidget(views_, 1);

    // Below the media: Text and Frame view, search, thumbnail size (p.314).
    auto* bar = new QHBoxLayout;
    bar->setSpacing(2);
    textButton_ = navButton(box, QStringLiteral("textView"), GlyphIcon::ListView, tr("Text View"));
    frameButton_ = navButton(box, QStringLiteral("frameView"), GlyphIcon::FrameView, tr("Frame View"));
    for (QToolButton* b : {textButton_, frameButton_}) {
        b->setCheckable(true);
        bar->addWidget(b);
    }
    auto* group = new QButtonGroup(box);
    group->addButton(textButton_);
    group->addButton(frameButton_);
    textButton_->setChecked(true);
    connect(textButton_, &QToolButton::clicked, this, [this] { setFrameView(false); });
    connect(frameButton_, &QToolButton::clicked, this, [this] { setFrameView(true); });
    search_ = new QLineEdit(box);
    search_->setObjectName(QStringLiteral("sourceBrowserSearch"));
    search_->setPlaceholderText(tr("Search"));
    search_->addAction(glyphIcon(GlyphIcon::Search), QLineEdit::LeadingPosition);
    search_->setClearButtonEnabled(true);
    search_->setMaximumWidth(220);
    connect(search_, &QLineEdit::textChanged, this, &SourceBrowser::setSearch);
    bar->addSpacing(8);
    bar->addWidget(search_);
    bar->addStretch();
    thumbnails_ = new QSlider(Qt::Horizontal, box);
    thumbnails_->setObjectName(QStringLiteral("thumbnailSize"));
    thumbnails_->setRange(0, kMaxThumbnailStep);
    thumbnails_->setValue(1);
    thumbnails_->setFixedWidth(120);
    thumbnails_->setToolTip(tr("Thumbnail size"));
    connect(thumbnails_, &QSlider::valueChanged, this, [this] {
        frames_->doItemsLayout();
        frames_->viewport()->update();
        emit stateChanged();
    });
    bar->addWidget(thumbnails_);
    v->addLayout(bar);
    return box;
}

QWidget* SourceBrowser::buildLowerPane() {
    // Link or Import, Settings, the progress of processing media, Target
    // Bin and the Link button (p.315, p.317).
    auto* box = new QWidget(this);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    expandButton_ = navButton(box, QStringLiteral("collapseExpand"), GlyphIcon::ChevronDown, tr("Collapse/Expand"));
    connect(expandButton_, &QToolButton::clicked, this, [this] { setSettingsCollapsed(!isSettingsCollapsed()); });
    row->addWidget(expandButton_);
    linkRadio_ = new QRadioButton(tr("Link"), box);
    linkRadio_->setObjectName(QStringLiteral("linkMode"));
    importRadio_ = new QRadioButton(tr("Import"), box);
    importRadio_->setObjectName(QStringLiteral("importMode"));
    linkRadio_->setChecked(true);
    row->addWidget(linkRadio_);
    row->addWidget(importRadio_);
    // Import's Resolution menu (p.214, step 4).
    resolutionLabel_ = new QLabel(tr("Resolution:"), box);
    resolution_ = new QComboBox(box);
    resolution_->setObjectName(QStringLiteral("importResolution"));
    for (const media::Resolution& r : media::resolutions()) resolution_->addItem(r.name, r.id);
    resolution_->setCurrentIndex(resolution_->findData(media::defaultResolution().id));
    row->addWidget(resolutionLabel_);
    row->addWidget(resolution_);
    connect(importRadio_, &QRadioButton::toggled, this, [this] {
        updateMode();
        emit stateChanged();
    });
    connect(resolution_, &QComboBox::currentIndexChanged, this, &SourceBrowser::stateChanged);
    settingsButton_ = new QPushButton(tr("Settings..."), box);
    settingsButton_->setObjectName(QStringLiteral("linkSettings"));
    connect(settingsButton_, &QPushButton::clicked, this, &SourceBrowser::settingsRequested);
    row->addWidget(settingsButton_);
    row->addSpacing(12);
    progressLabel_ = new QLabel(tr("Processing media"), box);
    progress_ = new QProgressBar(box);
    progress_->setObjectName(QStringLiteral("processingMedia"));
    progress_->setFixedWidth(140);
    progress_->setTextVisible(false);
    row->addWidget(progressLabel_);
    row->addWidget(progress_);
    row->addStretch();
    row->addWidget(new QLabel(tr("Target Bin:"), box));
    bins_ = new QComboBox(box);
    bins_->setObjectName(QStringLiteral("targetBin"));
    bins_->setMinimumWidth(160);
    row->addWidget(bins_);
    commit_ = new QPushButton(tr("Link"), box);
    commit_->setObjectName(QStringLiteral("linkButton"));
    connect(commit_, &QPushButton::clicked, this, &SourceBrowser::linkSelection);
    row->addWidget(commit_);
    updateMode();
    return box;
}

QToolButton* SourceBrowser::button(const QString& name) const {
    return findChild<QToolButton*>(name);
}

// ---- Navigation

void SourceBrowser::setFolder(const QString& path) {
    const QString p = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (!QFileInfo(p).isDir()) return;
    if (p != folder_) {
        // A new place drops the forward history, as in file browsers.
        history_ = history_.mid(0, position_ + 1);
        history_ << p;
        position_ = static_cast<int>(history_.size()) - 1;
    }
    folder_ = p;
    addRecent(p);
    const QModelIndex at = folders_->index(p);
    if (at.isValid()) {
        tree_->setCurrentIndex(at);
        tree_->scrollTo(at);
    }
    list();
    updateNavigation();
    emit stateChanged();
}

void SourceBrowser::back() {
    if (!canGoBack()) return;
    --position_;
    folder_ = history_.at(position_);
    list();
    updateNavigation();
}

void SourceBrowser::forward() {
    if (!canGoForward()) return;
    ++position_;
    folder_ = history_.at(position_);
    list();
    updateNavigation();
}

void SourceBrowser::up() {
    QDir d(folder_);
    if (d.cdUp()) setFolder(d.absolutePath());
}

void SourceBrowser::home() {
    setFolder(QDir::homePath());
}

QStringList SourceBrowser::breadcrumbs() const {
    QStringList parts;
    QDir d(folder_);
    if (folder_.isEmpty()) return parts;
    for (;;) {
        parts.prepend(d.isRoot() ? QDir::toNativeSeparators(d.absolutePath()) : d.dirName());
        if (d.isRoot() || !d.cdUp()) break;
    }
    return parts;
}

void SourceBrowser::toggleFavorite() {
    if (folder_.isEmpty()) return;
    if (favorites_.contains(folder_)) {
        favorites_.removeAll(folder_);
    } else {
        favorites_ << folder_;
    }
    refreshLists();
    updateNavigation();
    emit stateChanged();
}

void SourceBrowser::setMediaFolderView(bool on) {
    if (volumeButton_->isChecked() != on) volumeButton_->setChecked(on);
    static_cast<BrowserSortModel*>(proxy_)->setMediaOnly(on);
    list();
    emit stateChanged();
}

bool SourceBrowser::isMediaFolderView() const {
    return volumeButton_->isChecked();
}

void SourceBrowser::addRecent(const QString& path) {
    recent_.removeAll(path);
    recent_.prepend(path);
    while (recent_.size() > kMaxRecent) recent_.removeLast();
    refreshLists();
}

void SourceBrowser::refreshLists() {
    auto fill = [](QListWidget* list, const QStringList& paths) {
        list->clear();
        for (const QString& p : paths) {
            auto* item = new QListWidgetItem(glyphIcon(GlyphIcon::Folder), QDir(p).dirName().isEmpty() ? p : QDir(p).dirName(),
                                             list);
            item->setData(Qt::UserRole, p);
            item->setToolTip(QDir::toNativeSeparators(p));
        }
    };
    fill(favoriteList_, favorites_);
    fill(recentList_, recent_);
}

void SourceBrowser::updateNavigation() {
    backButton_->setEnabled(canGoBack());
    forwardButton_->setEnabled(canGoForward());
    upButton_->setEnabled(!folder_.isEmpty() && !QDir(folder_).isRoot());
    favoriteButton_->setChecked(favorites_.contains(folder_));
    // Breadcrumbs (p.313): each folder on the way, clickable.
    while (QLayoutItem* item = crumbs_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QStringList parts = breadcrumbs();
    QDir d(folder_);
    QStringList paths;
    for (;;) {
        paths.prepend(d.absolutePath());
        if (d.isRoot() || !d.cdUp()) break;
    }
    for (int i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            auto* sep = new QLabel(QStringLiteral(">"), crumbBar_);
            sep->setContentsMargins(3, 0, 3, 0);
            crumbs_->addWidget(sep);
        }
        auto* b = new QToolButton(crumbBar_);
        b->setObjectName(QStringLiteral("crumb"));
        b->setAutoRaise(true);
        b->setText(parts.at(i));
        b->setIcon(glyphIcon(GlyphIcon::Folder));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setFixedHeight(kToolButtonHeight);
        const QString target = paths.value(i);
        connect(b, &QToolButton::clicked, this, [this, target] { setFolder(target); });
        crumbs_->addWidget(b);
    }
    crumbs_->addStretch();
}

// ---- Listing and probing

void SourceBrowser::list() {
    ++generation_;
    pool_.clear();
    QList<SourceMediaModel::Entry> entries;
    if (!folder_.isEmpty()) {
        if (isMediaFolderView()) {
            // The folder as a media volume: every file in it and below,
            // as clips; probing leaves out what is not media (p.313).
            QDirIterator it(folder_, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                SourceMediaModel::Entry e;
                e.path = it.next();
                entries << e;
            }
        } else {
            const QFileInfoList infos =
                QDir(folder_).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::DirsFirst);
            for (const QFileInfo& fi : infos) {
                SourceMediaModel::Entry e;
                e.path = fi.absoluteFilePath();
                e.folder = fi.isDir();
                entries << e;
            }
        }
    }
    model_->setEntries(entries);
    startProbes(generation_);
    updateProgress();
}

void SourceBrowser::startProbes(int generation) {
    for (const SourceMediaModel::Entry& e : model_->entries()) {
        if (e.folder) continue;
        const QString path = e.path;
        pool_.start([this, path, generation] {
            auto info = media::probe(path);
            QMetaObject::invokeMethod(
                this,
                [this, path, generation, info] {
                    if (generation != generation_) return;
                    model_->setProbed(path, info);
                    // In a media volume only media is shown (p.313).
                    if (isMediaFolderView()) static_cast<BrowserSortModel*>(proxy_)->refilter();
                    updateProgress();
                },
                Qt::QueuedConnection);
        });
    }
}

void SourceBrowser::waitForProbes() {
    pool_.waitForDone();
    QCoreApplication::sendPostedEvents(this);
}

void SourceBrowser::updateProgress() {
    const auto& entries = model_->entries();
    const int files = static_cast<int>(
        std::count_if(entries.begin(), entries.end(), [](const SourceMediaModel::Entry& e) { return !e.folder; }));
    const int pending = model_->pending();
    progress_->setRange(0, std::max(files, 1));
    progress_->setValue(files - pending);
    progress_->setVisible(pending > 0);
    progressLabel_->setVisible(pending > 0);
}

// ---- Display

void SourceBrowser::setFrameView(bool frames) {
    views_->setCurrentWidget(frames ? static_cast<QWidget*>(frames_) : static_cast<QWidget*>(text_));
    (frames ? frameButton_ : textButton_)->setChecked(true);
    thumbnails_->setEnabled(frames);
    emit stateChanged();
}

bool SourceBrowser::isFrameView() const {
    return views_->currentWidget() == frames_;
}

void SourceBrowser::setSearch(const QString& text) {
    if (search_->text() != text) search_->setText(text);
    proxy_->setFilterFixedString(text);
}

void SourceBrowser::setThumbnailStep(int step) {
    thumbnails_->setValue(std::clamp(step, 0, kMaxThumbnailStep));
}

int SourceBrowser::thumbnailStep() const {
    return thumbnails_->value();
}

QSize SourceBrowser::thumbnailSize() const {
    return thumbnailFor(thumbnailStep());
}

void SourceBrowser::scrubTo(const QString& path, std::int64_t frame) {
    const int row = model_->rowOf(path);
    const SourceMediaModel::Entry* e = model_->entry(row);
    if (!e || !e->linkable) return;
    model_->setFrame(row, std::clamp<std::int64_t>(frame, 0, std::max<std::int64_t>(e->clip.durationFrames - 1, 0)));
}

bool SourceBrowser::eventFilter(QObject* watched, QEvent* event) {
    // Hovering across a thumbnail previews the clip: the left edge is its
    // first frame, the right edge its last (p.316).
    if (watched == frames_->viewport() && event->type() == QEvent::MouseMove) {
        const auto* move = static_cast<QMouseEvent*>(event);
        const QPoint at = move->position().toPoint();
        const QModelIndex index = frames_->indexAt(at);
        if (index.isValid() && !(move->buttons() & Qt::LeftButton)) {
            const QModelIndex source = proxy_->mapToSource(index);
            const SourceMediaModel::Entry* e = model_->entry(source.row());
            const QRect cell = frames_->visualRect(index);
            if (e && e->linkable && cell.width() > 0) {
                const double fraction = std::clamp(static_cast<double>(at.x() - cell.left()) / cell.width(), 0.0, 1.0);
                scrubTo(e->path, static_cast<std::int64_t>(fraction * static_cast<double>(e->clip.durationFrames - 1)));
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

QStringList SourceBrowser::selectedRows(bool mediaOnly) const {
    QStringList paths;
    for (const QModelIndex& i : text_->selectionModel()->selectedRows()) {
        const SourceMediaModel::Entry* e = model_->entry(proxy_->mapToSource(i).row());
        if (!e || (mediaOnly && e->folder)) continue;
        paths << e->path;
    }
    return paths;
}

void SourceBrowser::selectPaths(const QStringList& paths) {
    QItemSelection selection;
    for (const QString& p : paths) {
        const int row = model_->rowOf(p);
        if (row < 0) continue;
        const QModelIndex i = proxy_->mapFromSource(model_->index(row, 0));
        if (i.isValid()) selection.select(i, i.siblingAtColumn(proxy_->columnCount() - 1));
    }
    text_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

QStringList SourceBrowser::selectedPaths() const {
    return selectedRows(false);
}

void SourceBrowser::activate(const QModelIndex& index) {
    const SourceMediaModel::Entry* e = model_->entry(proxy_->mapToSource(index).row());
    if (!e) return;
    if (e->folder) {
        setFolder(e->path);
        return;
    }
    // Double Click to: Link or Import, or Load Clips in Source Monitor
    // (p.315).
    if (doubleClick() == DoubleClick::LoadInSourceMonitor) {
        emit loadRequested({e->path});
    } else if (!targetBin().isEmpty()) {
        commit({e->path});
    }
}

void SourceBrowser::showMenu(QWidget* view, const QPoint& pos) {
    QMenu menu(this);
    if (view == tree_) {
        const QModelIndex i = tree_->indexAt(pos);
        if (!i.isValid()) return;
        const QString path = folders_->filePath(i);
        // "Add volume Bin" (p.318): the folder's media into a new bin.
        menu.addAction(tr("Add Volume Bin"), this, [this, path] { emit volumeBinRequested(path); });
    } else {
        const QStringList files = selectedRows(true);
        QAction* add = menu.addAction(tr("Add to Bin"), this, [this, files] { commit(files); });
        add->setEnabled(!files.isEmpty() && !targetBin().isEmpty());
        QAction* load = menu.addAction(tr("Load in Source Monitor"), this, [this, files] { emit loadRequested(files); });
        load->setEnabled(!files.isEmpty());
        QStringList folders;
        for (const QString& p : selectedRows(false)) {
            if (QFileInfo(p).isDir()) folders << p;
        }
        if (folders.size() == 1) {
            const QString f = folders.first();
            menu.addAction(tr("Add Volume Bin"), this, [this, f] { emit volumeBinRequested(f); });
        }
    }
    menu.exec(view->mapToGlobal(pos));
}

// ---- Settings and linking

void SourceBrowser::setDoubleClick(DoubleClick action) {
    doubleClick_->setCurrentIndex(action == DoubleClick::LoadInSourceMonitor ? 1 : 0);
}

SourceBrowser::DoubleClick SourceBrowser::doubleClick() const {
    return doubleClick_->currentIndex() == 1 ? DoubleClick::LoadInSourceMonitor : DoubleClick::LinkOrImport;
}

void SourceBrowser::setCloseAfterLink(bool on) {
    closeAfter_->setChecked(on);
}

bool SourceBrowser::closesAfterLink() const {
    return closeAfter_->isChecked();
}

void SourceBrowser::setClearSourceOnClose(bool on) {
    clearSource_->setChecked(on);
}

bool SourceBrowser::clearsSourceOnClose() const {
    return clearSource_->isChecked();
}

void SourceBrowser::setSettingsCollapsed(bool collapsed) {
    settingsBox_->setVisible(!collapsed);
    expandButton_->setIcon(glyphIcon(collapsed ? GlyphIcon::ChevronUp : GlyphIcon::ChevronDown));
    emit stateChanged();
}

bool SourceBrowser::isSettingsCollapsed() const {
    return settingsBox_->isHidden();
}

void SourceBrowser::setLinkOptions(const media::LinkOptions& options) {
    model_->setOptions(options);
    list();
}

void SourceBrowser::setBins(const QList<QPair<QString, QString>>& bins, const QString& select) {
    const QString keep = select.isEmpty() ? targetBin() : select;
    const QSignalBlocker block(bins_);
    bins_->clear();
    for (const auto& [id, name] : bins) bins_->addItem(name, id);
    const int i = bins_->findData(keep);
    bins_->setCurrentIndex(i >= 0 ? i : 0);
    commit_->setEnabled(bins_->count() > 0);
}

void SourceBrowser::setTargetBin(const QString& id) {
    const int i = bins_->findData(id);
    if (i >= 0) bins_->setCurrentIndex(i);
}

QString SourceBrowser::targetBin() const {
    return bins_->currentData().toString();
}

QStringList SourceBrowser::pathsToLink() const {
    QStringList paths = selectedRows(true);
    if (!paths.isEmpty()) return paths;
    for (int r = 0; r < proxy_->rowCount(); ++r) {
        const SourceMediaModel::Entry* e = model_->entry(proxy_->mapToSource(proxy_->index(r, 0)).row());
        if (e && !e->folder && (!e->probed || e->linkable)) paths << e->path;
    }
    return paths;
}

void SourceBrowser::linkSelection() {
    const QStringList paths = pathsToLink();
    if (paths.isEmpty() || targetBin().isEmpty()) return;
    commit(paths);
}

void SourceBrowser::commit(const QStringList& paths) {
    // Link, or Import at the chosen resolution (p.213-214).
    if (isImportMode()) {
        emit importRequested(targetBin(), paths, importResolution());
    } else {
        emit linkRequested(targetBin(), paths);
    }
    if (closesAfterLink()) emit closeRequested();
}

void SourceBrowser::setImportMode(bool import) {
    (import ? importRadio_ : linkRadio_)->setChecked(true);
}

bool SourceBrowser::isImportMode() const {
    return importRadio_->isChecked();
}

void SourceBrowser::setImportResolution(const QString& id) {
    const int i = resolution_->findData(id);
    if (i >= 0) resolution_->setCurrentIndex(i);
}

QString SourceBrowser::importResolution() const {
    return resolution_->currentData().toString();
}

void SourceBrowser::updateMode() {
    const bool import = isImportMode();
    commit_->setText(import ? tr("Import") : tr("Link"));
    resolutionLabel_->setVisible(import);
    resolution_->setVisible(import);
    settingsButton_->setToolTip(import ? tr("Import settings") : tr("Link settings"));
}

void SourceBrowser::save(QSettings& settings) const {
    settings.beginGroup(QStringLiteral("sourceBrowser"));
    settings.setValue(QStringLiteral("folder"), folder_);
    settings.setValue(QStringLiteral("favorites"), favorites_);
    settings.setValue(QStringLiteral("recent"), recent_);
    settings.setValue(QStringLiteral("frameView"), isFrameView());
    settings.setValue(QStringLiteral("mediaFolderView"), isMediaFolderView());
    settings.setValue(QStringLiteral("thumbnailStep"), thumbnailStep());
    settings.setValue(QStringLiteral("doubleClickLoads"), doubleClick() == DoubleClick::LoadInSourceMonitor);
    settings.setValue(QStringLiteral("closeAfterLink"), closesAfterLink());
    settings.setValue(QStringLiteral("clearSourceOnClose"), clearsSourceOnClose());
    settings.setValue(QStringLiteral("settingsCollapsed"), isSettingsCollapsed());
    settings.setValue(QStringLiteral("import"), isImportMode());
    settings.setValue(QStringLiteral("importResolution"), importResolution());
    settings.endGroup();
}

void SourceBrowser::restore(QSettings& settings) {
    settings.beginGroup(QStringLiteral("sourceBrowser"));
    favorites_ = settings.value(QStringLiteral("favorites")).toStringList();
    recent_ = settings.value(QStringLiteral("recent")).toStringList();
    setFrameView(settings.value(QStringLiteral("frameView"), false).toBool());
    setThumbnailStep(settings.value(QStringLiteral("thumbnailStep"), 1).toInt());
    setDoubleClick(settings.value(QStringLiteral("doubleClickLoads"), false).toBool() ? DoubleClick::LoadInSourceMonitor
                                                                                       : DoubleClick::LinkOrImport);
    setCloseAfterLink(settings.value(QStringLiteral("closeAfterLink"), false).toBool());
    setClearSourceOnClose(settings.value(QStringLiteral("clearSourceOnClose"), false).toBool());
    setSettingsCollapsed(settings.value(QStringLiteral("settingsCollapsed"), false).toBool());
    setImportMode(settings.value(QStringLiteral("import"), false).toBool());
    setImportResolution(settings.value(QStringLiteral("importResolution")).toString());
    {
        const QSignalBlocker block(volumeButton_);
        volumeButton_->setChecked(settings.value(QStringLiteral("mediaFolderView"), false).toBool());
    }
    static_cast<BrowserSortModel*>(proxy_)->setMediaOnly(volumeButton_->isChecked());
    const QString folder = settings.value(QStringLiteral("folder")).toString();
    settings.endGroup();
    refreshLists();
    if (!folder.isEmpty() && QFileInfo(folder).isDir()) {
        setFolder(folder);
    } else {
        updateNavigation();
    }
}

} // namespace lumenis::ui
