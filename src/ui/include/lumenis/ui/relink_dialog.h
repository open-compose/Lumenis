#pragma once

#include "lumenis/media/relink.h"
#include "lumenis/model/bin.h"

#include <QDialog>
#include <QList>
#include <QString>

#include <functional>

class QCheckBox;
class QComboBox;
class QRadioButton;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace lumenis::ui {

// Clip > Relink > Linked Media (Editing Guide p.364-365): the linked clips,
// offline ones in red, filtered to "Offline only" or "All media" and by a
// search, and Locate Media, which looks in a folder for the clips' files
// and relinks those that fit.
//
// MainWindow gives it copies of the clips linked in place (the selected ones, or those of the
// selected or open bins) and stores each clip that relinked() reports back in its bin. The
// matching itself is media::locateMedia and media::relinkClip (src/media/link.*).
class LinkedMediaDialog : public QDialog {
    Q_OBJECT

public:
    // A clip and the bin it is in.
    struct Clip {
        QString binId;
        model::BinItem item;
    };
    // Lists `clips`, offline only at first. The dialog relinks its own copies.
    explicit LinkedMediaDialog(const QList<Clip>& clips, QWidget* parent = nullptr);

    // Where Locate Media asks for the folder; a folder dialog unless set
    // (tests).
    std::function<QString()> chooseFolder;

    // The filter menu: "Offline only" or "All media".
    void setOfflineOnly(bool offlineOnly);
    // The search: rows whose cells contain `text`, ignoring case; empty shows all.
    void setSearch(const QString& text);
    // The rows shown, by clip name.
    QStringList shownNames() const;
    // Locate Media (p.365): the selected clips, or all those shown. Returns
    // how many were relinked.
    int locateMedia(const QString& folder);
    // What the last Locate Media did: "3 relinked, 1 not found".
    QString summary() const;

    QTreeWidget* list() const { return list_; }

signals:
    // A clip was relinked during locateMedia(); `item` is the clip as changed, for bin `binId`.
    void relinked(const QString& binId, const model::BinItem& item);
    // A clip whose media was not found or does not fit, and why.
    void notRelinked(const QString& name, const QString& reason);

private:
    // Refills the list from the filter and the search, and the "Viewing: n/m" count.
    void refresh();
    // Unlinked, or its file is missing.
    bool isOffline(const model::BinItem& item) const;

    QList<Clip> clips_;
    QComboBox* filter_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QLabel* viewing_ = nullptr;
    QLabel* result_ = nullptr;
    QPushButton* locate_ = nullptr;
};

// Clip > Relink > Managed Media: the Relink dialog (p.361-364).
//
// Only collects the options: MainWindow passes options() to media::relinkManaged
// (src/media/relink.*) for the selected clips, or those of all open bins (allOpenBins()). Choices
// not implemented yet are disabled with the tooltip "Not available yet".
class ManagedRelinkDialog : public QDialog {
    Q_OBJECT

public:
    // `project`: the current project's name, for "Relink only to media from the current project".
    explicit ManagedRelinkDialog(const QString& project, QWidget* parent = nullptr);

    // The choices as media::RelinkOptions. No drives in it means All Available Drives.
    media::RelinkOptions options() const;
    // "Selected items in ALL open bins".
    bool allOpenBins() const;
    // A check box or menu by its object name (e.g. "masterClips", "relinkSourceName"); null if
    // there is none. For options() and tests.
    QCheckBox* check(const QString& objectName) const;
    QComboBox* menu(const QString& objectName) const;
    QRadioButton* allDrives() const { return allDrives_; }
    // The project's raster and rate, for Relink to: Video format of current project only (p.363).
    void setProjectFormat(QSize raster, const Rational& rate) {
        projectRaster_ = raster;
        projectRate_ = rate;
    }

private:
    // Enables the drive menu for "Media on drive", and the quality and resolution for
    // "Specific Resolution".
    void update();

    QString project_;
    QRadioButton* allDrives_ = nullptr;
    QRadioButton* oneDrive_ = nullptr;
    QComboBox* drive_ = nullptr;
    QComboBox* method_ = nullptr;
    QComboBox* quality_ = nullptr;
    QComboBox* resolution_ = nullptr;
    QLineEdit* ignoreAfter_ = nullptr;
    QSize projectRaster_;
    Rational projectRate_{25, 1};
};

} // namespace lumenis::ui
