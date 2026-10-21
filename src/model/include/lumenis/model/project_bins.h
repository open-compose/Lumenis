#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace lumenis::model {

// An entry in the Bin Container sidebar (guide p.294-299): a bin, a folder,
// the Trash, or the "Other Bins" folder of bins opened from other projects.
struct BinNode {
    enum class Kind { Folder, Bin, Trash, OtherBins };

    // A UUID that names the entry in the tree, in openBins and in the UI; it does not change when the
    // entry is renamed or moved.
    QString id;
    Kind kind = Kind::Bin;
    // The name shown in the sidebar; for a bin, also its file name without ".cthbin".
    QString name;
    // Bins: the file name inside the project folder, or an absolute path for
    // a bin from another project.
    QString file;
    // True for a bin from another project (listed under Other Bins); `file` is then absolute.
    bool external = false;
    // The contents of a folder, the Trash or Other Bins, in display order; empty for bins.
    QList<BinNode> children;

    bool isContainer() const { return kind != Kind::Bin; }
};

// The bins of one project and how the sidebar organises them. Bins are
// files in the project folder ("<name>.cthbin"); folders, the Trash and
// Other Bins exist only in the tree, which is saved as "<project>.cthbins".
class ProjectBins {
public:
    static QString suffix() { return QStringLiteral(".cthbins"); }

    // Nothing is read until load(); a new project starts with createInitialBin() and save().
    ProjectBins(QString projectFolder, QString projectName);

    const QString& folder() const { return folder_; }
    const QString& projectName() const { return projectName_; }

    // Reads the tree. Bin files the tree does not know are added at the top
    // level; entries whose bin file is gone are dropped.
    bool load(QString* error = nullptr);
    // Writes the tree and openBins to "<project>.cthbins" (format "cutthulhu-bins", version 1),
    // replacing the file atomically. Bin contents are saved separately (BinFile in bin.h).
    bool save(QString* error = nullptr) const;

    // Top-level entries.
    const QList<BinNode>& nodes() const { return roots_; }
    // The entry with this id anywhere in the tree, or null. The pointer is valid until the tree changes.
    const BinNode* node(const QString& id) const;
    // The folder holding `id`, or null at the top level.
    const BinNode* parentOf(const QString& id) const;
    // Every bin, depth first; Trash contents only when asked.
    QList<const BinNode*> bins(bool includeTrash = false) const;
    // True for entries inside the Trash (not for the Trash itself).
    bool isInTrash(const QString& id) const;
    // The Trash, or null until something has been deleted.
    const BinNode* trash() const;
    // The bin's file as an absolute path, or empty if `id` is not a bin.
    QString binPath(const QString& id) const;
    // The bin whose file is `path`, or empty.
    QString binForPath(const QString& path) const;

    // A new project's first bin, named after the project (p.294).
    // The functions that create entries return the new entry's id, or empty (with *error set where
    // there is one) on failure. Changes to the tree are kept in memory until save().
    QString createInitialBin(QString* error = nullptr);
    // New Bin: "<project>.N", numbered on (p.295). `parent` is a folder id
    // or empty for the top level.
    QString createBin(const QString& parent = {}, QString* error = nullptr);
    // New Folder (p.297).
    QString createFolder(const QString& parent = {});
    // Bins: renames the file too. Names follow the project naming rules,
    // up to 64 characters (p.295).
    bool rename(const QString& id, const QString& name, QString* error = nullptr);
    // Moves an entry into a folder (empty = top level) at `index` (-1 = end).
    bool move(const QString& id, const QString& parent, int index = -1, QString* error = nullptr);
    // Delete: bins and folders go to the Trash; a bin from another project
    // is only removed from the list (p.296-298), except a copy we made of a
    // Media Composer bin (in the folder's "Other Bins/"), which is deleted.
    bool remove(const QString& id, QString* error = nullptr);
    // Empty Trash: deletes the bin files in it (p.298).
    bool emptyTrash(QString* error = nullptr);
    // File > Open Bin: lists a bin from elsewhere under Other Bins (p.296).
    QString addOtherBin(const QString& path, QString* error = nullptr);

    // Bins open when the project was last closed.
    // Their ids (BinNode::id).
    QStringList openBins;

private:
    // The child list of a container (empty parent: the top level), or null if parent is not one.
    QList<BinNode>* childrenOf(const QString& parent);
    BinNode* find(const QString& id);
    // Removes the entry from wherever it is in the tree and moves it into *out.
    bool takeNode(const QString& id, BinNode* out);
    // base, unless taken (or numberFirst), else the first free "base.N" from N = 1.
    QString uniqueBinName(const QString& base, bool numberFirst) const;
    // Case-insensitive: another of this project's bins has the name or, when not renaming (except
    // empty), a bin file of that name exists in the folder.
    bool binNameTaken(const QString& name, const QString& except = {}) const;
    // The "<project>.cthbins" file in the project folder.
    QString treeFile() const;

    QString folder_;
    QString projectName_;
    QList<BinNode> roots_;
};

} // namespace lumenis::model
