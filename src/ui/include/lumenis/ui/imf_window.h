#pragma once

#include "lumenis/interop/imf.h"

#include <QList>
#include <QString>
#include <QWidget>

#include <functional>
#include <optional>

class QFormLayout;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

// Tools > IMF Window (11.5c; docs/spec/imf.md; Editing Guide p.333-337): a sidebar of IMF packages, each the root
// folder with its IMPs and CPLs in a tree and a status column, and a right pane of the selection's metadata.
// "Explore" links an existing package by its root folder; a CPL is dragged from the sidebar into a bin "to create a
// Media Composer sequence" (it travels as the CPL file, which a bin's drop hands to MainWindow::importImfComposition).
// MainWindow puts it in the "IMF" panel and keeps nothing else: the packages are the window's, for the session (ours).
namespace lumenis::ui {

class ImfWindow : public QWidget {
    Q_OBJECT

public:
    explicit ImfWindow(QWidget* parent = nullptr);

    // Explore (p.335): a folder dialog, or the chooser (tests); the package at the folder chosen is read and listed.
    void setFolderChooser(std::function<QString()> chooser) { folderChooser_ = std::move(chooser); }
    bool explore();
    // Lists the package at `root`; false, with the reason in `error`, when it is not one. A package listed already is
    // read again in its place.
    bool addPackage(const QString& root, QString* error = nullptr);
    // "Committed and uncommitted packages can be removed from the sidebar by selecting the root folder and deleting.
    // Note that it is only removed from the UI and nothing is removed from disk" (p.338).
    void removePackage(const QString& root);
    const QList<interop::imf::Package>& packages() const { return packages_; }
    // The package a composition playlist file belongs to, with every other listed package's track files added (a
    // Supplemental's Original); nothing when no listed package has it.
    std::optional<interop::imf::Package> packageOf(const QString& cplFile) const;

    // The sidebar, its items and the right pane (tests).
    QTreeWidget* sidebar() const { return tree_; }
    QPushButton* exploreButton() const { return explore_; }
    // The right pane's rows as "label: value", in order.
    QStringList metadata() const;
    // Selects the root, IMP or CPL with this path (the root folder, a PKL or CPL file).
    bool select(const QString& path);
    // The status colour of an item (p.334): "Green = has been exported and exists on disk".
    static QColor statusColor(const QTreeWidgetItem* item);

signals:
    // A package could not be read (the reason): MainWindow shows it.
    void failed(const QString& reason);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuild();
    void showSelection();

    QList<interop::imf::Package> packages_;
    QTreeWidget* tree_ = nullptr;
    QPushButton* explore_ = nullptr;
    QWidget* pane_ = nullptr;
    QFormLayout* form_ = nullptr;
    std::function<QString()> folderChooser_;
};

} // namespace lumenis::ui
