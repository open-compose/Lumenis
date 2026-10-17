#pragma once

#include "lumenis/model/bin.h"

#include <QDialog>
#include <QList>
#include <QString>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace lumenis::ui {

// Clip > Relink > Linked Media (Editing Guide p.364-365): the linked clips,
// offline ones in red, filtered to "Offline only" or "All media" and by a
// search, and Locate Media, which looks in a folder for the clips' files
// and relinks those that fit.
class LinkedMediaDialog : public QDialog {
    Q_OBJECT

public:
    struct Clip {
        QString binId;
        model::BinItem item;
    };
    explicit LinkedMediaDialog(const QList<Clip>& clips, QWidget* parent = nullptr);

    // Where Locate Media asks for the folder; a folder dialog unless set
    // (tests).
    std::function<QString()> chooseFolder;

    void setOfflineOnly(bool offlineOnly);
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
    void relinked(const QString& binId, const model::BinItem& item);
    // A clip whose media was not found or does not fit, and why.
    void notRelinked(const QString& name, const QString& reason);

private:
    void refresh();
    bool isOffline(const model::BinItem& item) const;

    QList<Clip> clips_;
    QComboBox* filter_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QLabel* viewing_ = nullptr;
    QLabel* result_ = nullptr;
    QPushButton* locate_ = nullptr;
};

} // namespace lumenis::ui
