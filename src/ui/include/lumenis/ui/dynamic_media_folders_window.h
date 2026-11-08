#pragma once

#include "lumenis/media/dynamic_media_folders.h"

#include <QDialog>
#include <QHash>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class QTreeWidget;

// Tools > Dynamic Media Folders and the Profile Editor (Editing Guide p.372-376; 8.4c, docs/spec/background-services.md).
// The window lists the folders with Enable, their profile and the Acquire column; the Profile Editor edits one
// profile's Link Settings and Actions. Both work on copies of media::DynamicMediaFolders; MainWindow keeps the data
// (main_window_dmf.cpp) and acquires assets into bins.
namespace lumenis::ui {

// The Profile Editor (p.372-375): "the Menu bars" open the profile summary, Link Settings and Actions; Actions + adds
// an action; each action row's Menu button chooses Copy to Folder, Consolidate or Transcode; rows are reordered by
// dragging; Save asks for the profile's name.
class DmfProfileEditor : public QDialog {
    Q_OBJECT
public:
    // `drives` are the media drives' roots for the Video Drive and Audio Drive menus.
    DmfProfileEditor(const media::DmfProfile& profile, const QStringList& drives, QWidget* parent = nullptr);

    // The profile as the controls show it (with the name Save chose).
    media::DmfProfile profile() const;
    // The menu bars (p.372): "summary", "link" or "actions"; open shows the section.
    void openSection(const QString& section, bool open);
    bool isSectionOpen(const QString& section) const;
    // Actions + (p.374): a Copy to Folder action at the end, selected.
    void addAction();
    // The selected row's Menu button choice; the row keeps its place.
    void setActionKind(int row, media::DmfAction::Kind kind);
    // Dragging a row above or below another (p.375).
    void moveAction(int from, int to);
    void removeAction(int row);
    void selectAction(int row);
    // Save (p.375): asks for the name (the chooser, or a text dialog), then accepts. False when the name is empty or
    // the question was cancelled.
    bool save();
    // Tests answer the name question here; the chooser gets the current name and returns the new one ("" cancels).
    void setNameChooser(std::function<QString(const QString&)> chooser) { nameChooser_ = std::move(chooser); }
    // Copy to's Set button (p.374): a folder dialog, or the chooser.
    void setFolderChooser(std::function<QString()> chooser) { folderChooser_ = std::move(chooser); }

private:
    void build(const QStringList& drives);
    void showProfile();
    void storeAction(int row);
    void showAction(int row);
    void refreshRows();
    void refreshSummary();

    media::DmfProfile profile_;
    std::function<QString(const QString&)> nameChooser_;
    std::function<QString()> folderChooser_;
    QHash<QString, QToolButton*> bars_;
    QHash<QString, QWidget*> sections_;
    QWidget* summary_ = nullptr;
    // Link Settings.
    QComboBox* plugin_ = nullptr;
    QComboBox* bin_ = nullptr;
    QComboBox* naming_ = nullptr;
    QLineEdit* binName_ = nullptr;
    QCheckBox* multichannel_ = nullptr;
    QLineEdit* trackFormats_ = nullptr;
    QCheckBox* audioStartTime_ = nullptr;
    QComboBox* reel_ = nullptr;
    QSpinBox* reelLevel_ = nullptr;
    QComboBox* frameCount_ = nullptr;
    QComboBox* timecode_ = nullptr;
    QComboBox* fps_ = nullptr;
    // Actions.
    QListWidget* actions_ = nullptr;
    QToolButton* actionMenu_ = nullptr;
    QStackedWidget* actionPages_ = nullptr;
    QLineEdit* copyTo_ = nullptr;
    QCheckBox* autoRelink_ = nullptr;
    QCheckBox* checkin_ = nullptr;
    QCheckBox* skipOnTarget_ = nullptr;
    QCheckBox* relinkBeforeSkip_ = nullptr;
    QComboBox* resolution_ = nullptr;
    QCheckBox* reformat_ = nullptr;
    QCheckBox* colorTransforms_ = nullptr;
    QComboBox* sampleRate_ = nullptr;
    QComboBox* bitDepth_ = nullptr;
    QComboBox* audioFormat_ = nullptr;
    QComboBox* videoDrive_ = nullptr;
    QComboBox* audioDrive_ = nullptr;
    int shownAction_ = -1;
    bool filling_ = false;
};

// The Dynamic Media Folders window (p.372, p.375-376): a row per folder with Enable, the folder, its profile menu, and
// the Acquire column, green when assets are ready (dimmed while they are still being processed); + adds a folder,
// - removes the selected one, Profile Editor edits the selected folder's profile (a new one when it has none).
class DynamicMediaFoldersWindow : public QWidget {
    Q_OBJECT
public:
    explicit DynamicMediaFoldersWindow(QWidget* parent = nullptr);

    // What an Acquire cell shows for a folder.
    struct AssetCount {
        int ready = 0;      // assets whose processing ended with clips
        int processing = 0; // still on the Background Queue
    };
    // The folders and profiles to show, and each folder's assets by path.
    void setFolders(const media::DynamicMediaFolders& folders, const QHash<QString, AssetCount>& assets);
    const media::DynamicMediaFolders& folders() const { return folders_; }

    // The + icon (p.372): the Select Folder window (a folder dialog, or the chooser), then a row for the folder.
    void addFolder();
    void removeSelectedFolder();
    void selectFolder(int row);
    int selectedFolder() const;
    // A row's Enable box and profile menu.
    void setEnabled(int row, bool enabled);
    void setProfile(int row, const QString& profile);
    // The Acquire cell's button (p.376), enabled while the folder has assets.
    QPushButton* acquireButton(int row) const;
    void setFolderChooser(std::function<QString()> chooser) { folderChooser_ = std::move(chooser); }

signals:
    // The folders changed here (added, removed, enabled, a profile chosen).
    void foldersChanged(const media::DynamicMediaFolders& folders);
    // Profile Editor for the folder (empty profile: a new one).
    void profileEditorRequested(const QString& folderPath, const QString& profile);
    // "Click the green icon in the Acquire column" (p.376).
    void acquireRequested(const QString& folderPath);

private:
    void refresh();
    media::DynamicMediaFolders folders_;
    QHash<QString, AssetCount> assets_;
    std::function<QString()> folderChooser_;
    QTreeWidget* list_ = nullptr;
    QPushButton* profileEditor_ = nullptr;
    QPushButton* remove_ = nullptr;
    bool filling_ = false;
};

} // namespace lumenis::ui
