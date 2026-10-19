#pragma once

#include "lumenis/media/link.h"
#include "lumenis/model/bin.h"

#include <QAbstractTableModel>
#include <QList>
#include <QPair>
#include <QStringList>
#include <QThreadPool>
#include <QWidget>

#include <cstdint>
#include <optional>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFileSystemModel;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QMenu;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSettings;
class QSlider;
class QSortFilterProxyModel;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QTreeView;

namespace lumenis::ui {

// What the Source Browser shows of a folder (p.314): its folders and files,
// and for each file what probing it found, as the clip linking it would be.
// One row per entry; the columns are columns(). Folders and files that are
// not media show only their name.
class SourceMediaModel : public QAbstractTableModel {
    Q_OBJECT

public:
    // Data roles on every column: the absolute path (QString) and the
    // Entry's folder, linkable and probed flags (bool).
    static constexpr int kPathRole = Qt::UserRole + 1;
    static constexpr int kFolderRole = Qt::UserRole + 2;
    static constexpr int kLinkableRole = Qt::UserRole + 3;
    static constexpr int kProbedRole = Qt::UserRole + 4;

    struct Entry {
        QString path;          // absolute
        bool folder = false;
        bool probed = false;   // the probe has finished (files only)
        bool linkable = false; // the probe found media that links as a clip
        model::BinItem clip;        // as linked, once probed
        std::int64_t frame = 0;     // the frame shown in Frame view (p.316)
    };

    explicit SourceMediaModel(QObject* parent = nullptr);

    // The Text view's columns: Name, then bin headings (p.274-278).
    static const QStringList& columns();

    // Replaces the listing (a model reset).
    void setEntries(const QList<Entry>& entries);
    // The options setProbed() links clips with; applies to later probes only.
    void setOptions(const media::LinkOptions& options) { options_ = options; }
    // A probe finished; ignored if the listing changed since. nullopt means
    // the file could not be probed.
    void setProbed(const QString& path, const std::optional<media::MediaInfo>& info);
    // Sets the row's Frame view frame (clip frames from 0); ignores bad rows.
    void setFrame(int row, std::int64_t frame);

    // The entry of a model row, or nullptr.
    const Entry* entry(int row) const;
    // The model row of a path, or -1.
    int rowOf(const QString& path) const;
    // How many files are still being probed.
    int pending() const;
    int count() const { return static_cast<int>(entries_.size()); }
    const QList<Entry>& entries() const { return entries_; }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    // Drags carry the files' paths in mediaFilesMime() (bin_dock.h), one per
    // line, as bins, monitors and the Timeline accept them.
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

private:
    QList<Entry> entries_;
    media::LinkOptions options_;
};

// File > Input > Source Browser (Editing Guide p.312-318): navigate to
// media, preview it, and link or import it into a bin (p.213-214).
// MainWindow shows it in a panel and acts on its request signals; it does
// not change bins itself. Files are probed on a pool of two threads
// (media::probe) and linked as clips with media::linkClip (src/media/link.*).
class SourceBrowser : public QWidget {
    Q_OBJECT

public:
    explicit SourceBrowser(QWidget* parent = nullptr);
    // Drops the probes not yet started and waits for those running.
    ~SourceBrowser() override;

    // Navigation (p.313).
    // Shows a folder (made absolute and clean); ignored if it is not a
    // directory. A folder other than the current one joins the history;
    // every call moves it to the top of Recent and emits stateChanged.
    void setFolder(const QString& path);
    // The folder shown; empty before the first setFolder().
    QString folder() const { return folder_; }
    bool canGoBack() const { return position_ > 0; }
    bool canGoForward() const { return position_ + 1 < history_.size(); }
    // Move through the history without changing it.
    void back();
    void forward();
    // The parent folder, and the user's home folder.
    void up();
    void home();
    // The folder's path from the root, one name per folder; the root is its
    // native path ("C:\" or "/").
    QStringList breadcrumbs() const;
    // Add or Remove Favorites: the current folder.
    void toggleFavorite();
    QStringList favorites() const { return favorites_; }
    // The folders visited, most recent first, at most 12.
    QStringList recent() const { return recent_; }
    // Media Folder View: every media file in the folder and the folders in
    // it, as clips; nothing else (p.313).
    void setMediaFolderView(bool on);
    bool isMediaFolderView() const;

    // The display (p.314).
    // Frame view (thumbnails) or Text view (columns); the thumbnail slider
    // works in Frame view only.
    void setFrameView(bool frames);
    bool isFrameView() const;
    // Shows only entries whose name contains the text (case-insensitive).
    void setSearch(const QString& text);
    // Thumbnail sizes, 0 to kMaxThumbnailStep; Ctrl+L enlarges (p.316),
    // Ctrl+K reduces as in bins (p.250).
    static constexpr int kMaxThumbnailStep = 5;
    // Clamped to 0..kMaxThumbnailStep.
    void setThumbnailStep(int step);
    int thumbnailStep() const;
    // The frame size of the current step, 16:9, in pixels.
    QSize thumbnailSize() const;
    // Shows frame `frame` of a clip in Frame view, as hovering over it does
    // (p.316). `frame` is clamped to the clip; ignored for anything that is
    // not a linkable file in the listing.
    void scrubTo(const QString& path, std::int64_t frame);

    // The unfiltered listing (the views show it through a sort/filter proxy).
    SourceMediaModel* media() const { return model_; }
    // Waits for the probes of the current folder (tests).
    void waitForProbes();
    // Selects the rows of these paths (files or folders), in both views.
    void selectPaths(const QStringList& paths);
    // The selected entries' paths, folders included.
    QStringList selectedPaths() const;

    // Source Browser Settings (p.315).
    enum class DoubleClick { LinkOrImport, LoadInSourceMonitor };
    void setDoubleClick(DoubleClick action);
    DoubleClick doubleClick() const;
    void setCloseAfterLink(bool on);
    bool closesAfterLink() const;
    void setClearSourceOnClose(bool on);
    bool clearsSourceOnClose() const;
    // The Collapse/Expand button: hides the settings for more clips (p.316).
    void setSettingsCollapsed(bool collapsed);
    bool isSettingsCollapsed() const;

    // The link options for the clips shown (project rate, Link settings).
    // Lists the folder again so every file is probed with the new options.
    void setLinkOptions(const media::LinkOptions& options);
    // Target Bin (p.317): the project's bins, id and name, and the one to
    // select. An empty `select` keeps the current target if it is still
    // there; otherwise the first bin is chosen. Link is disabled without bins.
    void setBins(const QList<QPair<QString, QString>>& bins, const QString& select = {});
    // Ignored for an id that is not listed.
    void setTargetBin(const QString& id);
    // The target bin's id; empty when there are no bins.
    QString targetBin() const;

    // The Link button (p.317): the selected files, or with nothing selected
    // every file shown (linking a volume, p.317-318). Emits linkRequested or,
    // in Import mode, importRequested; nothing without files or a target bin.
    void linkSelection();
    // The files Link would link: the selected files, or else every file
    // shown that is media or not yet probed.
    QStringList pathsToLink() const;
    // Link or Import (p.213): Import makes new media at the resolution
    // chosen beside it.
    void setImportMode(bool import);
    bool isImportMode() const;
    // A media::Resolution id (media::resolutions()); unknown ids are ignored.
    void setImportResolution(const QString& id);
    QString importResolution() const;

    // The folder, favorites, recent folders, views and Source Browser
    // settings, in the "sourceBrowser" group. app/main.cpp restores them at
    // startup and saves them on quit.
    void save(QSettings& settings) const;
    void restore(QSettings& settings);

    // A tool button by object name ("back", "forward", "up", "home",
    // "favorite", "mediaFolderView", "textView", ...), or nullptr.
    QToolButton* button(const QString& name) const;
    QTreeView* textView() const { return text_; }
    QListView* frameView() const { return frames_; }
    QTreeView* explorer() const { return tree_; }

signals:
    // Link these files into the bin (p.317).
    void linkRequested(const QString& binId, const QStringList& paths);
    // Import these files into the bin at a resolution (p.214).
    void importRequested(const QString& binId, const QStringList& paths, const QString& resolution);
    // Link every media file in a folder into a new bin named after it
    // ("Add volume Bin", p.318).
    void volumeBinRequested(const QString& folder);
    // Load these files into the Source monitor without a bin (p.315).
    void loadRequested(const QStringList& paths);
    // The Settings button: Link settings (p.317), Import settings when
    // importing (p.214).
    void settingsRequested();
    // "Close Source Browser after Link or Import" (p.315).
    void closeRequested();
    // The folder, view or settings changed (to save them).
    void stateChanged();

protected:
    // Hover scrubbing over Frame view thumbnails.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildNavigation(QHBoxLayout* row);
    QWidget* buildDisplay();
    QWidget* buildLowerPane();
    // Lists folder_ into the model and starts probing its files.
    void list();
    // Probes every file of the listing on pool_; results of an older
    // generation (the folder changed since) are dropped.
    void startProbes(int generation);
    // Enables Back, Forward and Up, checks Favorite and rebuilds the crumbs.
    void updateNavigation();
    // Shows "Processing media" while probes are pending.
    void updateProgress();
    void addRecent(const QString& path);
    // Refills the Favorites and Recent lists.
    void refreshLists();
    // A double-click: opens a folder, or links, imports or loads a file.
    void activate(const QModelIndex& index);
    // The context menu of the Explore tree or of the media views.
    void showMenu(QWidget* view, const QPoint& pos);
    // The selected entries' paths; files only with mediaOnly.
    QStringList selectedRows(bool mediaOnly) const;
    // Emits linkRequested or importRequested for the target bin, then
    // closeRequested if the browser closes after linking.
    void commit(const QStringList& paths);
    // The Link button's text and the Resolution menu follow Link or Import.
    void updateMode();

    QString folder_;
    QStringList history_; // folders visited, for Back and Forward
    int position_ = -1;   // folder_'s index in history_; -1 before any
    QStringList favorites_;
    QStringList recent_;
    int generation_ = 0;  // bumped by each list(), to drop stale probe results
    QThreadPool pool_;

    SourceMediaModel* model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QFileSystemModel* folders_ = nullptr;
    QTabWidget* places_ = nullptr;
    QTreeView* tree_ = nullptr;
    QListWidget* favoriteList_ = nullptr;
    QListWidget* recentList_ = nullptr;
    QHBoxLayout* crumbs_ = nullptr;
    QWidget* crumbBar_ = nullptr;
    QToolButton* backButton_ = nullptr;
    QToolButton* forwardButton_ = nullptr;
    QToolButton* upButton_ = nullptr;
    QToolButton* homeButton_ = nullptr;
    QToolButton* collapseButton_ = nullptr;
    QToolButton* favoriteButton_ = nullptr;
    QToolButton* volumeButton_ = nullptr;
    QStackedWidget* views_ = nullptr;
    QTreeView* text_ = nullptr;
    QListView* frames_ = nullptr;
    QToolButton* textButton_ = nullptr;
    QToolButton* frameButton_ = nullptr;
    QLineEdit* search_ = nullptr;
    QSlider* thumbnails_ = nullptr;
    QToolButton* expandButton_ = nullptr;
    QFrame* settingsBox_ = nullptr;
    QComboBox* doubleClick_ = nullptr;
    QCheckBox* closeAfter_ = nullptr;
    QCheckBox* clearSource_ = nullptr;
    QRadioButton* linkRadio_ = nullptr;
    QRadioButton* importRadio_ = nullptr;
    QPushButton* settingsButton_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QComboBox* bins_ = nullptr;
    QPushButton* commit_ = nullptr;
    QLabel* resolutionLabel_ = nullptr;
    QComboBox* resolution_ = nullptr;
};

} // namespace lumenis::ui
