#include "lumenis/model/project_bins.h"

#include "lumenis/core/project_format.h"
#include "lumenis/model/bin.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <functional>

// The Bin Container's tree of bins, folders, Trash and Other Bins for one project
// (project_bins.h), and its file "<project>.cthbins". The bins themselves are BinFile files
// (bin.*); this file creates, renames and deletes them on disk as the tree changes. The Bin
// Container (src/ui/bin_container.*) and the main window edit the tree; ProjectStore::create() makes a new
// project's first bin here; structure dumps (structure.*) walk it.
namespace lumenis::model {

namespace {

// The tree file's format tag and the newest version this build reads and writes.
constexpr int kFileVersion = 1;
constexpr auto kFileFormat = "cutthulhu-bins";

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

const char* kindText(BinNode::Kind k) {
    switch (k) {
    case BinNode::Kind::Folder: return "folder";
    case BinNode::Kind::Bin: return "bin";
    case BinNode::Kind::Trash: return "trash";
    case BinNode::Kind::OtherBins: return "otherBins";
    }
    return "bin";
}

std::optional<BinNode::Kind> parseKind(const QString& s) {
    for (const auto k : {BinNode::Kind::Folder, BinNode::Kind::Bin, BinNode::Kind::Trash, BinNode::Kind::OtherBins}) {
        if (s == QLatin1String(kindText(k))) return k;
    }
    return std::nullopt;
}

// One tree entry as JSON: {id, kind, name} plus {file, external?} for bins or {children} for the
// containers. nodeFromJson() drops entries with an unknown kind, no id or no name.
QJsonObject nodeToJson(const BinNode& n) {
    QJsonObject o{{QStringLiteral("id"), n.id},
                  {QStringLiteral("kind"), QLatin1String(kindText(n.kind))},
                  {QStringLiteral("name"), n.name}};
    if (n.kind == BinNode::Kind::Bin) {
        o.insert(QStringLiteral("file"), n.file);
        if (n.external) o.insert(QStringLiteral("external"), true);
    } else {
        QJsonArray children;
        for (const BinNode& c : n.children) children.append(nodeToJson(c));
        o.insert(QStringLiteral("children"), children);
    }
    return o;
}

std::optional<BinNode> nodeFromJson(const QJsonObject& o) {
    BinNode n;
    const auto kind = parseKind(o.value(QStringLiteral("kind")).toString());
    n.id = o.value(QStringLiteral("id")).toString();
    n.name = o.value(QStringLiteral("name")).toString();
    if (!kind || n.id.isEmpty() || n.name.isEmpty()) return std::nullopt;
    n.kind = *kind;
    n.file = o.value(QStringLiteral("file")).toString();
    n.external = o.value(QStringLiteral("external")).toBool();
    for (const QJsonValue& c : o.value(QStringLiteral("children")).toArray()) {
        if (auto child = nodeFromJson(c.toObject())) n.children.append(*child);
    }
    return n;
}

// Appends the bins in `nodes` depth first, in display order, skipping the Trash unless asked.
void collectBins(const QList<BinNode>& nodes, bool includeTrash, QList<const BinNode*>& out) {
    for (const BinNode& n : nodes) {
        if (n.kind == BinNode::Kind::Bin) {
            out << &n;
        } else if (includeTrash || n.kind != BinNode::Kind::Trash) {
            collectBins(n.children, includeTrash, out);
        }
    }
}

// True if `id` is n itself or anywhere below it.
bool contains(const BinNode& n, const QString& id) {
    if (n.id == id) return true;
    return std::any_of(n.children.begin(), n.children.end(), [&](const BinNode& c) { return contains(c, id); });
}

} // namespace

ProjectBins::ProjectBins(QString projectFolder, QString projectName)
    : folder_(QDir::cleanPath(std::move(projectFolder))), projectName_(std::move(projectName)) {}

QString ProjectBins::treeFile() const {
    return QDir(folder_).filePath(projectName_ + suffix());
}

// A missing tree file is not an error: the tree is then rebuilt from the bin files in the folder.
// Open bins that no longer exist or are in the Trash are forgotten.
bool ProjectBins::load(QString* error) {
    roots_.clear();
    openBins.clear();
    QFile f(treeFile());
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly)) return fail(error, f.errorString());
        QJsonParseError parseError{};
        const QJsonObject o = QJsonDocument::fromJson(f.readAll(), &parseError).object();
        if (parseError.error != QJsonParseError::NoError) return fail(error, parseError.errorString());
        if (o.value(QStringLiteral("format")).toString() != QLatin1String(kFileFormat)) {
            return fail(error, QStringLiteral("not a bin list"));
        }
        if (o.value(QStringLiteral("version")).toInt() > kFileVersion) {
            return fail(error, QStringLiteral("bin list was saved by a newer version"));
        }
        for (const QJsonValue& v : o.value(QStringLiteral("tree")).toArray()) {
            if (auto n = nodeFromJson(v.toObject())) roots_.append(*n);
        }
        for (const QJsonValue& v : o.value(QStringLiteral("open")).toArray()) openBins << v.toString();
    }

    // Drop entries whose file is gone.
    std::function<void(QList<BinNode>&)> prune = [&](QList<BinNode>& nodes) {
        nodes.removeIf([&](const BinNode& n) {
            return n.kind == BinNode::Kind::Bin && !QFileInfo::exists(n.external ? n.file : QDir(folder_).filePath(n.file));
        });
        for (BinNode& n : nodes) prune(n.children);
    };
    prune(roots_);

    // Add bin files the tree does not know about.
    const QDir dir(folder_);
    // File names are compared case-insensitively, as on Windows and macOS.
    QStringList known;
    for (const BinNode* b : bins(true)) {
        if (!b->external) known << b->file.toLower();
    }
    QStringList files = dir.entryList({QStringLiteral("*") + BinFile::suffix()}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString& file : files) {
        if (known.contains(file.toLower())) continue;
        BinNode n;
        n.id = newId();
        n.kind = BinNode::Kind::Bin;
        n.name = file.chopped(BinFile::suffix().size());
        n.file = file;
        roots_.append(n);
    }
    openBins.removeIf([&](const QString& id) { return !node(id) || isInTrash(id); });
    return true;
}

bool ProjectBins::save(QString* error) const {
    QJsonArray tree;
    for (const BinNode& n : roots_) tree.append(nodeToJson(n));
    QJsonArray open;
    for (const QString& id : openBins) open.append(id);
    const QJsonObject o{{QStringLiteral("format"), QLatin1String(kFileFormat)},
                        {QStringLiteral("version"), kFileVersion},
                        {QStringLiteral("tree"), tree},
                        {QStringLiteral("open"), open}};
    QSaveFile f(treeFile());
    if (!f.open(QIODevice::WriteOnly)) return fail(error, f.errorString());
    f.write(QJsonDocument(o).toJson());
    if (!f.commit()) return fail(error, f.errorString());
    return true;
}

BinNode* ProjectBins::find(const QString& id) {
    std::function<BinNode*(QList<BinNode>&)> search = [&](QList<BinNode>& nodes) -> BinNode* {
        for (BinNode& n : nodes) {
            if (n.id == id) return &n;
            if (BinNode* found = search(n.children)) return found;
        }
        return nullptr;
    };
    return id.isEmpty() ? nullptr : search(roots_);
}

const BinNode* ProjectBins::node(const QString& id) const {
    return const_cast<ProjectBins*>(this)->find(id);
}

const BinNode* ProjectBins::parentOf(const QString& id) const {
    std::function<const BinNode*(const QList<BinNode>&, const BinNode*)> search =
        [&](const QList<BinNode>& nodes, const BinNode* parent) -> const BinNode* {
        for (const BinNode& n : nodes) {
            if (n.id == id) return parent;
            if (const BinNode* found = search(n.children, &n)) return found;
        }
        return nullptr;
    };
    return search(roots_, nullptr);
}

QList<BinNode>* ProjectBins::childrenOf(const QString& parent) {
    if (parent.isEmpty()) return &roots_;
    BinNode* p = find(parent);
    return p && p->isContainer() ? &p->children : nullptr;
}

QList<const BinNode*> ProjectBins::bins(bool includeTrash) const {
    QList<const BinNode*> out;
    collectBins(roots_, includeTrash, out);
    return out;
}

const BinNode* ProjectBins::trash() const {
    for (const BinNode& n : roots_) {
        if (n.kind == BinNode::Kind::Trash) return &n;
    }
    return nullptr;
}

bool ProjectBins::isInTrash(const QString& id) const {
    const BinNode* t = trash();
    return t && t->id != id && contains(*t, id);
}

QString ProjectBins::binPath(const QString& id) const {
    const BinNode* n = node(id);
    if (!n || n->kind != BinNode::Kind::Bin) return {};
    return n->external ? n->file : QDir(folder_).filePath(n->file);
}

QString ProjectBins::binForPath(const QString& path) const {
    const QString wanted = QFileInfo(path).absoluteFilePath();
    for (const BinNode* b : bins(true)) {
        if (QFileInfo(binPath(b->id)).absoluteFilePath().compare(wanted, Qt::CaseInsensitive) == 0) return b->id;
    }
    return {};
}

// Bins from other projects do not count: they live in other folders.
bool ProjectBins::binNameTaken(const QString& name, const QString& except) const {
    for (const BinNode* b : bins(true)) {
        if (b->id != except && !b->external && b->name.compare(name, Qt::CaseInsensitive) == 0) return true;
    }
    return QFileInfo::exists(QDir(folder_).filePath(name + BinFile::suffix())) && except.isEmpty();
}

QString ProjectBins::uniqueBinName(const QString& base, bool numberFirst) const {
    if (!numberFirst && !binNameTaken(base)) return base;
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1.%2").arg(base).arg(n);
        if (!binNameTaken(candidate)) return candidate;
    }
}

// Writes an empty bin with the default columns and puts it first in the tree.
QString ProjectBins::createInitialBin(QString* error) {
    BinNode n;
    n.id = newId();
    n.kind = BinNode::Kind::Bin;
    n.name = uniqueBinName(projectName_, false);
    n.file = n.name + BinFile::suffix();
    Bin bin;
    bin.name = n.name;
    bin.display.columns = Bin::defaultColumns();
    if (!BinFile::write(QDir(folder_).filePath(n.file), bin, error)) return {};
    roots_.prepend(n);
    return n.id;
}

// Writes an empty bin "<project>.N" with the default columns, at the end of `parent`.
QString ProjectBins::createBin(const QString& parent, QString* error) {
    QList<BinNode>* list = childrenOf(parent);
    if (!list || isInTrash(parent)) {
        fail(error, QStringLiteral("no such folder"));
        return {};
    }
    const BinNode* p = node(parent);
    if (p && p->kind == BinNode::Kind::OtherBins) {
        fail(error, QStringLiteral("new bins cannot go in Other Bins"));
        return {};
    }
    BinNode n;
    n.id = newId();
    n.kind = BinNode::Kind::Bin;
    n.name = uniqueBinName(projectName_, true);
    n.file = n.name + BinFile::suffix();
    Bin bin;
    bin.name = n.name;
    bin.display.columns = Bin::defaultColumns();
    if (!BinFile::write(QDir(folder_).filePath(n.file), bin, error)) return {};
    // Fetched again: nothing has moved, but the compiler cannot know it
    // (GCC 15 -Wnull-dereference).
    list = childrenOf(parent);
    if (!list) {
        fail(error, QStringLiteral("no such folder"));
        return {};
    }
    // Above the Trash and Other Bins at the top level.
    int index = static_cast<int>(list->size());
    if (parent.isEmpty()) {
        while (index > 0 && (*list)[index - 1].kind != BinNode::Kind::Bin && (*list)[index - 1].kind != BinNode::Kind::Folder) {
            --index;
        }
    }
    list->insert(index, n);
    return n.id;
}

// "Untitled Folder", then "Untitled Folder.1", ... until the name is free among its siblings.
// Folders may only go in folders or the top level, never in the Trash or Other Bins.
QString ProjectBins::createFolder(const QString& parent) {
    QList<BinNode>* list = childrenOf(parent);
    const BinNode* p = node(parent);
    if (!list || isInTrash(parent) || (p && p->kind != BinNode::Kind::Folder)) return {};
    QString name = QStringLiteral("Untitled Folder");
    for (int n = 1; std::any_of(list->begin(), list->end(), [&](const BinNode& c) { return c.name == name; }); ++n) {
        name = QStringLiteral("Untitled Folder.%1").arg(n);
    }
    BinNode f;
    f.id = newId();
    f.kind = BinNode::Kind::Folder;
    f.name = name;
    int index = static_cast<int>(list->size());
    if (parent.isEmpty()) {
        while (index > 0 && (*list)[index - 1].kind != BinNode::Kind::Bin && (*list)[index - 1].kind != BinNode::Kind::Folder) {
            --index;
        }
    }
    list->insert(index, f);
    return f.id;
}

// Folders and Other Bins only change their name. A bin is rewritten under its new file name (with
// the new name inside) and its old file removed; a bin from another project is renamed in its
// own folder.
bool ProjectBins::rename(const QString& id, const QString& requested, QString* error) {
    BinNode* n = find(id);
    if (!n) return fail(error, QStringLiteral("no such bin or folder"));
    const QString name = requested;
    if (n->kind == BinNode::Kind::Trash) return fail(error, QStringLiteral("the Trash cannot be renamed"));
    if (n->kind != BinNode::Kind::Bin) {
        if (name.trimmed().isEmpty()) return fail(error, QStringLiteral("name is empty"));
        n->name = name.trimmed();
        return true;
    }
    if (const auto problem = validateName(name.toStdString(), kMaxBinNameLength)) {
        return fail(error, QString::fromStdString(*problem));
    }
    if (name == n->name) return true;
    const QString oldPath = binPath(id);
    QString newPath;
    if (n->external) {
        newPath = QFileInfo(oldPath).dir().filePath(name + BinFile::suffix());
    } else {
        if (binNameTaken(name, id)) return fail(error, QStringLiteral("a bin named \"%1\" already exists").arg(name));
        newPath = QDir(folder_).filePath(name + BinFile::suffix());
    }
    auto bin = BinFile::read(oldPath, error);
    if (!bin) return false;
    bin->name = name;
    const bool sameFile = oldPath.compare(newPath, Qt::CaseInsensitive) == 0;
    if (!sameFile && QFileInfo::exists(newPath)) return fail(error, QStringLiteral("%1 already exists").arg(newPath));
    // Write the new file before letting go of the old one. A change of
    // case only needs the old file out of the way first on file systems
    // that ignore case.
    const QString parked = oldPath + QStringLiteral(".renaming");
    if (sameFile && !QFile::rename(oldPath, parked)) return fail(error, QStringLiteral("cannot rename %1").arg(oldPath));
    if (!BinFile::write(newPath, *bin, error)) {
        if (sameFile) QFile::rename(parked, oldPath);
        return false;
    }
    QFile::remove(sameFile ? parked : oldPath);
    n->name = name;
    n->file = n->external ? newPath : name + BinFile::suffix();
    return true;
}

bool ProjectBins::takeNode(const QString& id, BinNode* out) {
    std::function<bool(QList<BinNode>&)> take = [&](QList<BinNode>& nodes) {
        for (qsizetype i = 0; i < nodes.size(); ++i) {
            if (nodes[i].id == id) {
                *out = nodes.takeAt(i);
                return true;
            }
            if (take(nodes[i].children)) return true;
        }
        return false;
    };
    return take(roots_);
}

// The Trash and Other Bins stay where they are, and only bins from other projects may go in
// Other Bins. `index` counts in the destination list after the entry has been taken out.
bool ProjectBins::move(const QString& id, const QString& parent, int index, QString* error) {
    const BinNode* n = node(id);
    if (!n) return fail(error, QStringLiteral("no such bin or folder"));
    if (n->kind == BinNode::Kind::Trash || n->kind == BinNode::Kind::OtherBins) {
        return fail(error, QStringLiteral("%1 cannot be moved").arg(n->name));
    }
    if (!parent.isEmpty()) {
        const BinNode* p = node(parent);
        if (!p || !p->isContainer()) return fail(error, QStringLiteral("not a folder"));
        if (contains(*n, parent)) return fail(error, QStringLiteral("a folder cannot go inside itself"));
        if (p->kind == BinNode::Kind::OtherBins && !n->external) {
            return fail(error, QStringLiteral("only bins from other projects go in Other Bins"));
        }
    }
    BinNode taken;
    if (!takeNode(id, &taken)) return false;
    QList<BinNode>* list = childrenOf(parent);
    if (!list) {
        roots_.append(taken); // not lost, whatever happened
        return fail(error, QStringLiteral("no such folder"));
    }
    if (index < 0 || index > list->size()) index = static_cast<int>(list->size());
    list->insert(index, taken);
    return true;
}

// Nothing is deleted from disk here: bins and folders move into the Trash (made on first use),
// and their bins are closed. emptyTrash() deletes the files.
bool ProjectBins::remove(const QString& id, QString* error) {
    const BinNode* n = node(id);
    if (!n) return fail(error, QStringLiteral("no such bin or folder"));
    if (n->kind == BinNode::Kind::Trash) return fail(error, QStringLiteral("use Empty Trash"));
    if (n->external || n->kind == BinNode::Kind::OtherBins) {
        // Only the pointers go; the files stay where they are (p.296).
        BinNode taken;
        takeNode(id, &taken);
        roots_.removeIf([](const BinNode& r) { return r.kind == BinNode::Kind::OtherBins && r.children.isEmpty(); });
        openBins.removeAll(id);
        return true;
    }
    if (isInTrash(id)) return fail(error, QStringLiteral("already in the Trash"));
    // The Trash appears on the first delete (p.297). Its id is read without
    // going through a pointer the compiler cannot prove set (GCC 15
    // -Wnull-dereference).
    QString trashId;
    if (const BinNode* existing = trash()) {
        trashId = existing->id;
    } else {
        BinNode t;
        t.id = newId();
        t.kind = BinNode::Kind::Trash;
        t.name = QStringLiteral("Trash");
        trashId = t.id;
        roots_.append(t);
    }
    BinNode taken;
    takeNode(id, &taken);
    // Bins from other projects inside a deleted folder are only unlisted.
    std::function<void(QList<BinNode>&)> dropExternal = [&](QList<BinNode>& nodes) {
        nodes.removeIf([](const BinNode& c) { return c.external; });
        for (BinNode& c : nodes) dropExternal(c.children);
    };
    dropExternal(taken.children);
    // Looked up after taking the node out: that moves the list's entries.
    if (QList<BinNode>* trashContents = childrenOf(trashId)) {
        trashContents->append(taken);
    } else {
        roots_.append(taken);
    }
    std::function<void(const BinNode&)> close = [&](const BinNode& c) {
        openBins.removeAll(c.id);
        for (const BinNode& g : c.children) close(g);
    };
    close(taken);
    return true;
}

// Stops at the first file that cannot be deleted, leaving the Trash in the tree.
bool ProjectBins::emptyTrash(QString* error) {
    const BinNode* t = trash();
    if (!t) return true;
    QList<const BinNode*> doomed;
    collectBins(t->children, true, doomed);
    for (const BinNode* b : doomed) {
        const QString path = QDir(folder_).filePath(b->file);
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            return fail(error, QStringLiteral("cannot delete %1").arg(path));
        }
    }
    roots_.removeIf([](const BinNode& r) { return r.kind == BinNode::Kind::Trash; });
    return true;
}

// The file must be a readable bin. A bin already in the tree (by path) returns its existing id.
// Other Bins is made on first use, just before the Trash (or at the end when there is none).
QString ProjectBins::addOtherBin(const QString& path, QString* error) {
    const QFileInfo info(path);
    if (!info.isFile()) {
        fail(error, QStringLiteral("%1 is not a file").arg(path));
        return {};
    }
    if (!BinFile::read(path, error)) return {};
    if (const QString existing = binForPath(path); !existing.isEmpty()) return existing;
    BinNode* other = nullptr;
    for (BinNode& r : roots_) {
        if (r.kind == BinNode::Kind::OtherBins) other = &r;
    }
    if (!other) {
        BinNode o;
        o.id = newId();
        o.kind = BinNode::Kind::OtherBins;
        o.name = QStringLiteral("Other Bins");
        // Just above the Trash, wherever it is (move() can put entries after it), or last.
        const auto trashAt = std::find_if(roots_.cbegin(), roots_.cend(),
                                          [](const BinNode& r) { return r.kind == BinNode::Kind::Trash; });
        roots_.insert(trashAt - roots_.cbegin(), o);
        for (BinNode& r : roots_) {
            if (r.kind == BinNode::Kind::OtherBins) other = &r;
        }
    }
    BinNode n;
    n.id = newId();
    n.kind = BinNode::Kind::Bin;
    n.name = info.completeBaseName();
    n.file = info.absoluteFilePath();
    n.external = true;
    other->children.append(n);
    return n.id;
}

} // namespace lumenis::model
