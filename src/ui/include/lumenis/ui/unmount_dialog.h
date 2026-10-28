#pragma once

#include <QDialog>

class QListWidget;

namespace lumenis::ui {

// File > Media > Unmount's "Unmount Disk or Drives" dialog (Editing Guide p.343-344;
// docs/spec/media-unmount.md): "The list displays all drives currently available", several may be
// selected (Ctrl+click), and Unmount makes them unavailable (media::unmountDrives). Ours: a Mount All
// button brings every unmounted drive back ("You can also remount all the drives", p.343).
class UnmountDialog : public QDialog {
    Q_OBJECT

public:
    explicit UnmountDialog(QWidget* parent = nullptr);

    QListWidget* list() const { return list_; }
    // The roots of the selected drives.
    QStringList selectedRoots() const;
    // Mount All was clicked (the dialog is then accepted with nothing to unmount).
    bool mountAll() const { return mountAll_; }

private:
    QListWidget* list_ = nullptr;
    bool mountAll_ = false;
};

} // namespace lumenis::ui
