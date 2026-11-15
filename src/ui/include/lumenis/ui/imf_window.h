#pragma once

#include "lumenis/interop/imf.h"

#include <QList>
#include <QString>
#include <QWidget>

#include <functional>
#include <optional>

class QFormLayout;
class QMenu;
class QMimeData;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

// Tools > IMF Window (11.5c; docs/spec/imf.md; Editing Guide p.333-337): a sidebar of IMF packages, each the root
// folder with its IMPs and CPLs in a tree and a status column, and a right pane of the selection's metadata.
// "Explore" links an existing package by its root folder; a CPL is dragged from the sidebar into a bin "to create a
// Media Composer sequence" (it travels as the CPL file, which a bin's drop hands to MainWindow::importImfComposition).
// "New IMF" (11.5c2, p.334, p.337) starts an IMF Original that is not on disk yet: sequences dragged from a bin onto it
// become CPLs in an IMP, their metadata is completed in the right pane (red: required fields missing; yellow: ready),
// and Export asks MainWindow to write it (MainWindow::exportImfDraft), after which it is listed as the package on disk
// (green). MainWindow puts it in the "IMF" panel and keeps nothing else: the packages are the window's, for the session
// (ours).
namespace lumenis::ui {

// A sequence in a package not yet written: the bin item it came from and its CPL's metadata.
struct ImfDraftCpl {
    QString binId;
    QString itemId;
    QString sequenceName;
    QString contentTitle;      // the sequence's name at first; the sidebar shows "CPL_<Content Title>" (p.336)
    QString contentKind = QStringLiteral("feature");
    QString profile = QStringLiteral("J2K IMF YCrCb");
    QString issuer, creator, annotation;
};
// An IMP not yet written: its CPLs and metadata. Its name is "untitled" until it is written (p.335).
struct ImfDraftImp {
    QString issuer, creator, annotation;
    QList<ImfDraftCpl> cpls;
};
// An IMF Original not yet written: the root folder's name and the directory it goes in, its metadata and IMPs.
struct ImfDraft {
    QString name = QStringLiteral("untitled");
    QString directory;
    QString issuer, creator, annotation;
    QList<ImfDraftImp> imps;
    // The root folder the package is written as: `directory`/`name`.
    QString root() const;
};

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

    // ---- packages not yet written (11.5c2)
    // New IMF > IMF Original (p.337): an untitled root folder and IMP; its index in drafts().
    int newOriginal();
    // Sequences dragged onto a draft's root folder (`draft`; -1: a new Original, as a drag onto the empty sidebar).
    // The first drag fills the draft's untitled IMP, each later one makes a new IMP ("if you add additional sequences
    // after the first drag and drop into the folder, a new IMP is created", p.337-338). The draft's index.
    int addSequences(int draft, const QList<ImfDraftCpl>& sequences);
    const QList<ImfDraft>& drafts() const { return drafts_; }
    // Changes one of a draft's fields as the right pane does: `imp` -1 for the root, `cpl` -1 for the IMP. Fields:
    // "name", "directory", "issuer", "creator", "annotation", "contentTitle", "contentKind", "profile". Completing a
    // CPL's Issuer or Creator fills the IMP's and the root's when they are empty (p.336).
    void setDraftField(int draft, int imp, int cpl, const QString& field, const QString& value);
    // What a CPL still needs before it can be written, beyond its fields (MainWindow: the project's format against
    // the IMF Supported Formats, p.340-341); empty when it can be.
    void setCplCheck(std::function<QString(const ImfDraftCpl&)> check) { cplCheck_ = std::move(check); rebuild(); }
    // The directory chooser of the root's Directory field (tests).
    void setDirectoryChooser(std::function<QString()> chooser) { directoryChooser_ = std::move(chooser); }
    // The fields a draft, IMP or CPL still needs (p.334, "Red = required fields must be completed"); empty: yellow.
    QStringList missingFields(int draft, int imp = -1, int cpl = -1) const;
    // Whether the draft is ready to be written: every field of it, its IMPs and CPLs complete, and at least one CPL.
    bool draftReady(int draft) const;
    // Export (p.334): asks for the selected draft to be written (exportRequested); enabled only when it is ready.
    void exportSelected();
    // MainWindow, once a draft is written to `root`: the draft goes and the package on disk is listed, green.
    void draftWritten(int draft, const QString& root);
    // The draft the current selection belongs to; -1 for none.
    int selectedDraft() const;

    // The sidebar, its items and the right pane (tests).
    QTreeWidget* sidebar() const { return tree_; }
    QPushButton* exploreButton() const { return explore_; }
    QPushButton* newImfButton() const { return newImf_; }
    QPushButton* exportButton() const { return export_; }
    // The right pane's rows as "label: value", in order.
    QStringList metadata() const;
    // Selects the root, IMP or CPL with this path (the root folder, a PKL or CPL file).
    bool select(const QString& path);
    // Selects a draft's root (imp -1), IMP (cpl -1) or CPL.
    bool selectDraft(int draft, int imp = -1, int cpl = -1);
    // The status colour of an item (p.334): "Green = has been exported and exists on disk", yellow ready to be
    // written, red with required fields missing.
    static QColor statusColor(const QTreeWidgetItem* item);
    static QColor green();
    static QColor yellow();
    static QColor red();

signals:
    // A package could not be read (the reason): MainWindow shows it.
    void failed(const QString& reason);
    // Bin items dropped on the sidebar: on a draft's root (its index) or elsewhere (-1, a new Original), or on a
    // package on disk (`packageRoot`; Supplementals, 11.5c3). MainWindow keeps the sequences and calls addSequences.
    void binItemsDropped(const QString& binId, const QStringList& itemIds, int draft, const QString& packageRoot);
    // Export was clicked for a ready draft: MainWindow writes it.
    void exportRequested(int draft);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuild();
    void showSelection();
    void showDraft(const QTreeWidgetItem* item);
    void refreshStatuses();
    void dropped(const QMimeData* mime, QTreeWidgetItem* on);

    QList<interop::imf::Package> packages_;
    QList<ImfDraft> drafts_;
    std::function<QString(const ImfDraftCpl&)> cplCheck_;
    std::function<QString()> directoryChooser_;
    QPushButton* newImf_ = nullptr;
    QPushButton* export_ = nullptr;
    QMenu* newMenu_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QPushButton* explore_ = nullptr;
    QWidget* pane_ = nullptr;
    QFormLayout* form_ = nullptr;
    std::function<QString()> folderChooser_;
};

} // namespace lumenis::ui
