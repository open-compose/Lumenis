#pragma once

#include "lumenis/media/copy_media.h"

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

// The proxy media windows (Editing Guide ch.16; 8.7a, docs/spec/proxy-media.md): Create Proxies (p.508-510) and Copy
// Media (p.512-513). MainWindow opens them (main_window_proxy.cpp).
namespace lumenis::ui {

// A place media can go: a media drive, or a shared workspace a storage module lists (ext::SharedStorageProvider).
struct MediaTarget {
    QString name;
    QString root;
    // Workspaces only: the provider's workspace id, and whether it takes Distributed Processing jobs.
    QString workspaceId;
    bool distributedProcessing = false;
    bool isWorkspace() const { return !workspaceId.isEmpty(); }
};

// The Create Proxies window (p.508): "Select either a local drive or choose an Avid NEXIS workspace ... Processing Options
// for creating proxies include 'Application' or 'Background Process'"; "(Optional) Select the Distributed Processing option
// ... The Distributed Processing option is only available after you select an Avid NEXIS workspace" (p.510). Ours: the
// media drives, then the workspaces of the storage modules.
class CreateProxiesDialog : public QDialog {
    Q_OBJECT

public:
    // The drive menu's entries; the first is chosen.
    explicit CreateProxiesDialog(const QList<MediaTarget>& targets, QWidget* parent = nullptr);

    // The chosen entry.
    MediaTarget target() const;
    // Its root.
    QString drive() const;
    void setTarget(int index);
    // "Background Process" rather than "Application".
    bool background() const;
    void setBackground(bool on);
    // Distributed Processing: on, and the chosen entry a workspace that takes jobs.
    bool distributed() const;
    void setDistributed(bool on);

private:
    void updateDistributed();

    QList<MediaTarget> targets_;
    QComboBox* drives_ = nullptr;
    QComboBox* processing_ = nullptr;
    QCheckBox* distributed_ = nullptr;
};

// The Copy Media window (p.512-513): the Bins, Managed media, Proxy media and Renders boxes, the size the copy needs
// ("As soon as the Copy Media window opens, size calculation begins based on the enabled checkboxes. Enabling or disabling
// checkboxes changes the calculation and updates the total"), Copy Media to a Media Drive or a Folder with its Set button,
// Copy Bins to with Set and, for a folder, "Use same as above", and the Available Space under each ("If there is not enough
// space, copying will not be allowed"). The files come in the request; request() gives it back with the choices made.
class CopyMediaDialog : public QDialog {
    Q_OBJECT

public:
    // `request`'s file lists and binsTarget (the Copy Bins to folder at first) are kept; its boxes start checked.
    CopyMediaDialog(const QList<MediaTarget>& drives, media::CopyMediaRequest request, QWidget* parent = nullptr);

    media::CopyMediaRequest request() const;
    const media::CopyMediaPlan& plan() const { return plan_; }
    // The size lines under the boxes.
    QString status() const;
    // Whether OK is on: something to copy, a target, and room for it.
    bool canCopy() const;

    // As the controls do (for the window's own Set buttons, and for tests).
    void setChecked(const QString& box, bool on); // "bins", "managed", "proxies", "renders"
    void setCopyToFolder(bool folder);
    void setDrive(int index);
    void setFolder(const QString& folder);
    void setBinsFolder(const QString& folder);
    void setBinsSameAsAbove(bool same);
    // Works the plan out now (the window does it after each change, once the "Calculating Size..." line has shown).
    void recalculate();

private:
    void scheduleRecalculate();
    void updateControls();

    QList<MediaTarget> drives_;
    media::CopyMediaRequest request_;
    media::CopyMediaPlan plan_;
    QString folder_;
    QString binsFolder_;
    bool pending_ = false;
    QCheckBox* bins_ = nullptr;
    QCheckBox* managed_ = nullptr;
    QCheckBox* proxies_ = nullptr;
    QCheckBox* renders_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* copyTo_ = nullptr;
    QLabel* driveLabel_ = nullptr;
    QComboBox* drive_ = nullptr;
    QPushButton* setFolder_ = nullptr;
    QLabel* folderPath_ = nullptr;
    QLabel* space_ = nullptr;
    QCheckBox* sameAsAbove_ = nullptr;
    QPushButton* setBins_ = nullptr;
    QLabel* binsPath_ = nullptr;
    QLabel* binsSpace_ = nullptr;
    QPushButton* ok_ = nullptr;
};

} // namespace lumenis::ui
