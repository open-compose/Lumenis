#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;
class QTreeWidget;

// The Batch Import dialog box (9.2a; docs/spec/batch-import.md; Editing Guide p.235-237): the clips Clip > Batch Import
// and Batch Re-import will import again, found ones in black and the others in red, Skip This Clip and Set File Location;
// the Import Target (Video Resolution, the video and audio drives with the Single/Dual Drives button); the Import
// Options. It only collects the choices: MainWindow::batchImport() imports.
namespace lumenis::ui {

class BatchImportDialog : public QDialog {
    Q_OBJECT

public:
    // A clip to import again: where its bin item is, its name, the file it was last imported from and, when found, the
    // file it imports from now (empty: not found, shown in red).
    struct Entry {
        QString binId;
        QString itemId;
        QString name;
        QString lastPath;
        QString path;
    };

    explicit BatchImportDialog(QList<Entry> entries, QWidget* parent = nullptr);

    // The clips left in the list (Skip This Clip takes them out), in order.
    const QList<Entry>& entries() const { return entries_; }
    // How many of them have a file to import from.
    int foundCount() const;
    QTreeWidget* list() const { return list_; }
    // The caption over the list (p.236: "summarizes the total number of clips shown and how many of them are
    // available for import"): "<found> of <shown> clips found".
    QString caption() const;

    // "Removes any clips that you select in the list so that they are not reimported."
    void skipSelected();
    // Set File Location for the selected clips: `chosen` is the file picked for the first of them; the others are looked
    // for beside it and then at the same relative place (media::relocatedSource). Returns how many were found.
    int setFileLocation(const QString& chosen);
    // Replaces the Locate File dialog (tests): given the first selected clip's last path, returns the file picked, or an
    // empty string for Cancel.
    void setLocateHook(std::function<QString(const QString& lastPath)> hook) { locateHook_ = std::move(hook); }

    // The Import Target: a media::Resolution id and the drives' roots (the audio drive is the video drive unless the
    // Single/Dual Drives button shows two).
    QString resolution() const;
    void setResolution(const QString& id);
    QString videoDrive() const;
    QString audioDrive() const;
    void setDrive(const QString& root);
    void setDualDrives(bool dual);
    bool dualDrives() const;
    // "Override clip settings with current settings" (p.237).
    bool overrideSettings() const;
    void setOverrideSettings(bool on);

signals:
    // The Current Settings button: "Opens the Import Settings dialog box, allowing you to change the current import
    // options."
    void currentSettingsRequested();

private:
    void refill();
    void locate();

    QList<Entry> entries_;
    QLabel* caption_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QComboBox* resolution_ = nullptr;
    QToolButton* dual_ = nullptr;
    QComboBox* videoDrive_ = nullptr;
    QComboBox* audioDrive_ = nullptr;
    QCheckBox* sourceCompression_ = nullptr;
    QCheckBox* override_ = nullptr;
    QPushButton* import_ = nullptr;
    std::function<QString(const QString&)> locateHook_;
};

} // namespace lumenis::ui
