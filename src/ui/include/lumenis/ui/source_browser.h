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
class SourceMediaModel : public QAbstractTableModel {
    Q_OBJECT

public:
    static constexpr int kPathRole = Qt::UserRole + 1;
    static constexpr int kFolderRole = Qt::UserRole + 2;
    static constexpr int kLinkableRole = Qt::UserRole + 3;
    static constexpr int kProbedRole = Qt::UserRole + 4;

    struct Entry {
        QString path;
        bool folder = false;
        bool probed = false;
        bool linkable = false;
        model::BinItem clip;        // as linked, once probed
        std::int64_t frame = 0;     // the frame shown in Frame view (p.316)
    };

    explicit SourceMediaModel(QObject* parent = nullptr);

    // The Text view's columns: Name, then bin headings (p.274-278).
    static const QStringList& columns();

    void setEntries(const QList<Entry>& entries);
    void setOptions(const media::LinkOptions& options) { options_ = options; }
    // A probe finished; ignored if the listing changed since.
    void setProbed(const QString& path, const std::optional<media::MediaInfo>& info);
    void setFrame(int row, std::int64_t frame);

    const Entry* entry(int row) const;
    int rowOf(const QString& path) const;
    int pending() const;
    int count() const { return static_cast<int>(entries_.size()); }
    const QList<Entry>& entries() const { return entries_; }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

private:
    QList<Entry> entries_;
    media::LinkOptions options_;
};

// File > Input > Source Browser (Editing Guide p.312-318): navigate to
// media, preview it, and link it into a bin. Import comes with Phase 2.4.
class SourceBrowser : public QWidget {
    Q_OBJECT

public:
    explicit SourceBrowser(QWidget* parent = nullptr);
    ~SourceBrowser() override;

    // Navigation (p.313).
    void setFolder(const QString& path);
    QString folder() const { return folder_; }
    bool canGoBack() const { return position_ > 0; }
    bool canGoForward() const { return position_ + 1 < history_.size(); }
    void back();
    void forward();
    void up();
    void home();
    QStringList breadcrumbs() const;
    // Add or Remove Favorites: the current folder.
    void toggleFavorite();
    QStringList favorites() const { return favorites_; }
    QStringList recent() const { return recent_; }
    // Media Folder View: every media file in the folder and the folders in
    // it, as clips; nothing else (p.313).
    void setMediaFolderView(bool on);
    bool isMediaFolderView() const;

    // The display (p.314).
    void setFrameView(bool frames);
    bool isFrameView() const;
    void setSearch(const QString& text);
    // Thumbnail sizes, 0 to kMaxThumbnailStep; Ctrl+L enlarges (p.316),
    // Ctrl+K reduces as in bins (p.250).
    static constexpr int kMaxThumbnailStep = 5;
    void setThumbnailStep(int step);
    int thumbnailStep() const;
    QSize thumbnailSize() const;
    // Shows frame `frame` of a clip in Frame view, as hovering over it does
    // (p.316).
    void scrubTo(const QString& path, std::int64_t frame);

    SourceMediaModel* media() const { return model_; }
    // Waits for the probes of the current folder (tests).
    void waitForProbes();
    void selectPaths(const QStringList& paths);
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
    void setLinkOptions(const media::LinkOptions& options);
    // Target Bin (p.317): the project's bins, id and name, and the one to
    // select.
    void setBins(const QList<QPair<QString, QString>>& bins, const QString& select = {});
    void setTargetBin(const QString& id);
    QString targetBin() const;

    // The Link button (p.317): the selected files, or with nothing selected
    // every file shown (linking a volume, p.317-318).
    void linkSelection();
    // The files Link would link.
    QStringList pathsToLink() const;

    void save(QSettings& settings) const;
    void restore(QSettings& settings);

    QToolButton* button(const QString& name) const;
    QTreeView* textView() const { return text_; }
    QListView* frameView() const { return frames_; }
    QTreeView* explorer() const { return tree_; }

signals:
    // Link these files into the bin (p.317).
    void linkRequested(const QString& binId, const QStringList& paths);
    // Link every media file in a folder into a new bin named after it
    // ("Add volume Bin", p.318).
    void volumeBinRequested(const QString& folder);
    // Load these files into the Source monitor without a bin (p.315).
    void loadRequested(const QStringList& paths);
    // The Settings button: Link settings (p.317).
    void settingsRequested();
    // "Close Source Browser after Link or Import" (p.315).
    void closeRequested();
    // The folder, view or settings changed (to save them).
    void stateChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildNavigation(QHBoxLayout* row);
    QWidget* buildDisplay();
    QWidget* buildLowerPane();
    void list();
    void startProbes(int generation);
    void updateNavigation();
    void updateProgress();
    void addRecent(const QString& path);
    void refreshLists();
    void activate(const QModelIndex& index);
    void showMenu(QWidget* view, const QPoint& pos);
    QStringList selectedRows(bool mediaOnly) const;

    QString folder_;
    QStringList history_;
    int position_ = -1;
    QStringList favorites_;
    QStringList recent_;
    int generation_ = 0;
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
};

} // namespace lumenis::ui
