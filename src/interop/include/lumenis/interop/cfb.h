#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QList>
#include <QString>

#include <array>
#include <cstdint>
#include <vector>

// The Compound File Binary format ([MS-CFB]): the container AAF files are
// stored in, a file system of storages (folders) and streams (files) inside
// one file (docs/spec/aaf.md). Written after the public specification, with
// pyaaf2's cfb.py (MIT) as the reference implementation.
namespace lumenis::interop::cfb {

using Clsid = std::array<std::uint8_t, 16>; // as stored: a GUID, little-endian fields

// A directory entry's type, as stored in byte 66 of its entry: unused, a storage (a folder), a stream (a
// file) or the root storage, which is always entry 0.
enum class EntryType : std::uint8_t { Empty = 0, Storage = 1, Stream = 2, Root = 5 };

// One 128-byte directory entry, decoded (Reader) or to be written (Writer).
struct Entry {
    QString name;
    EntryType type = EntryType::Empty;
    Clsid clsid{};
    // User-defined flags, kept as stored.
    std::uint32_t stateBits = 0;
    std::uint64_t created = 0;  // FILETIME
    std::uint64_t modified = 0; // FILETIME
    std::uint32_t start = 0;    // first sector (or mini sector)
    std::uint64_t size = 0;     // bytes, for a stream
    // Entry indexes, 0xFFFFFFFF for none: the left and right siblings in the parent storage's binary
    // tree, and, for a storage, the root of its own children's tree.
    std::uint32_t left = 0xFFFFFFFF;
    std::uint32_t right = 0xFFFFFFFF;
    std::uint32_t child = 0xFFFFFFFF;
    bool isStorage() const { return type == EntryType::Storage || type == EntryType::Root; }
    bool isStream() const { return type == EntryType::Stream; }
};

// The order of siblings in the directory: shorter names first, then by
// upper-cased UTF-16 code unit ([MS-CFB] 2.6.4). Negative, 0 or positive.
int compareNames(const QString& a, const QString& b);
// Whether a storage or stream may have this name: 1 to 31 UTF-16 code units,
// none of '/', '\\', ':' or '!'.
bool isValidName(const QString& name);

// Reads a compound file. Only the header, the FATs and the directory are
// read when it is opened; streams are read on demand, so the file can be
// larger than memory. Damaged files are refused with an error, never read
// past their end.
class Reader {
public:
    Reader() = default;
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    // Opens `path` and reads the header, the FAT, the mini FAT and the directory, and checks the
    // directory tree. False, with `error` set to the file's name and what is broken, when any of them is
    // damaged.
    bool open(const QString& path, QString* error = nullptr);
    // The same for a whole file in memory (the fuzz targets, fuzz/README.md, and tests); `name` stands
    // for the file's name in errors. The data is shared, not copied (QByteArray is implicitly shared).
    bool openData(const QByteArray& data, const QString& name = QStringLiteral("<memory>"), QString* error = nullptr);
    // Closes the file and forgets everything read from it.
    void close();
    bool isOpen() const { return io_ && io_->isOpen(); }

    // The header's major version (3 or 4), its sector size (512 or 4096 bytes) and its class ID.
    int majorVersion() const { return major_; }
    int sectorSize() const { return static_cast<int>(sectorSize_); }
    const Clsid& headerClsid() const { return headerClsid_; }

    // Entry 0 is the root storage.
    static constexpr int root() { return 0; }
    int entryCount() const { return static_cast<int>(entries_.size()); }
    const Entry& entry(int id) const { return entries_[static_cast<std::size_t>(id)]; }
    // A storage's children in directory order.
    QList<int> children(int storage) const;
    // A child by name (case does not matter, as in [MS-CFB]); -1 if none.
    int child(int storage, const QString& name) const;
    // "/a/b/c" from the root; -1 if there is none.
    int find(const QString& path) const;
    // The path of every entry below the root, depth first ("/a", "/a/b").
    QStringList paths() const;
    // The path of one entry: "/a/b", or "/" for the root.
    QString path(int id) const;

    // Up to `length` bytes of a stream from `offset`; fewer at its end. Sets
    // `error` and returns nothing when its sectors are damaged.
    QByteArray read(int stream, std::uint64_t offset, std::uint64_t length, QString* error = nullptr);
    // The whole stream.
    QByteArray readAll(int stream, QString* error = nullptr) {
        return read(stream, 0, entry(stream).size, error);
    }

private:
    // Sets `error` to the file's name and `message`, closes the file and returns false.
    bool fail(QString* error, const QString& message);
    // The steps of open(), in order; each fails through fail().
    bool readHeader(QString* error);
    bool readFat(QString* error);
    bool readMiniFat(QString* error);
    bool readDirectory(QString* error);
    // Builds parent_ and kids_ from the directory's binary trees, refusing loops and shared entries.
    bool checkTree(QString* error);
    // The chain of sectors from `start`; false when it loops or leaves the table.
    // `limit` caps how many sectors are kept in `out`; the chain is followed to its end regardless.
    bool chain(const std::vector<std::uint32_t>& table, std::uint32_t start, std::vector<std::uint32_t>& out,
               std::uint64_t limit, QString* error) const;
    // One whole sector into `data`; a last sector cut short is padded with zeros.
    bool readSector(std::uint32_t sector, char* data, QString* error);
    // The sectors' contents one after another.
    bool readChain(const std::vector<std::uint32_t>& sectors, QByteArray& out, QString* error);
    // A stream's chain (of mini sectors when it is small), cut to its size and cached in chains_. Null,
    // with `error` set, when the chain is broken or shorter than the size.
    const std::vector<std::uint32_t>* streamChain(int stream, QString* error);
    // Whether the stream lives in the mini stream: under the cutoff, and not the root (whose own chain is
    // the mini stream).
    bool isMini(int stream) const;

    // What open() and openData() read from: file_ or buffer_ (io_ points to the one in use), and the
    // name errors give.
    bool start(QString* error);
    QFile file_;
    QBuffer buffer_;
    QIODevice* io_ = nullptr;
    QString name_;
    // In bytes, for bounds checks.
    std::uint64_t fileSize_ = 0;
    int major_ = 0;
    std::uint32_t sectorSize_ = 512;
    // The header's fields ([MS-CFB] 2.2): the mini stream cutoff, the first sectors and sector counts of
    // the directory, the mini FAT, the DIFAT and the FAT, and the header's 109 DIFAT entries (FAT sector
    // numbers).
    std::uint32_t miniCutoff_ = 4096;
    Clsid headerClsid_{};
    std::uint32_t dirStart_ = 0;
    std::uint32_t miniFatStart_ = 0;
    std::uint32_t miniFatCount_ = 0;
    std::uint32_t difatStart_ = 0;
    std::uint32_t difatCount_ = 0;
    std::uint32_t fatCount_ = 0;
    std::array<std::uint32_t, 109> headerDifat_{};
    // Entry n of either table is the sector (or mini sector) after sector n in its chain.
    std::vector<std::uint32_t> fat_;
    std::vector<std::uint32_t> miniFat_;
    std::vector<std::uint32_t> miniStream_; // the root's chain: where mini sectors live
    // The directory, by entry index; parent_ and kids_ give each entry's parent storage and each
    // storage's children in directory order (made by checkTree).
    std::vector<Entry> entries_;
    std::vector<int> parent_;
    std::vector<QList<int>> kids_;
    // Large storages' children by name key (child()), made on first lookup.
    mutable QHash<int, QHash<QString, int>> names_;
    // Streams' chains, made on first read.
    QHash<int, std::vector<std::uint32_t>> chains_;
};

// Writes a compound file. Storages and streams are added to a tree, then the
// whole file is written at once: streams' sectors first, in the order they
// were added, then the mini stream, the mini FAT, the directory and the FAT.
// Siblings form a balanced binary tree with every node black, which
// [MS-CFB] 2.6.4 allows.
class Writer {
public:
    // 512-byte sectors make a version 3 file, 4096-byte a version 4 one.
    explicit Writer(int sectorSize = 4096);

    static constexpr int root() { return 0; }
    void setHeaderClsid(const Clsid& clsid) { headerClsid_ = clsid; }
    // The class ID of a storage (AAF: the AUID of the object's class); ignored for streams.
    void setClsid(int storage, const Clsid& clsid);
    // FILETIMEs; ignored for streams, and the root's creation time is not kept.
    void setTimes(int id, std::uint64_t created, std::uint64_t modified);
    // Ignored for streams.
    void setStateBits(int id, std::uint32_t bits);
    // -1, with `error` set, when the name is not valid or already taken.
    int addStorage(int parent, const QString& name, QString* error = nullptr);
    int addStream(int parent, const QString& name, const QByteArray& data, QString* error = nullptr);
    // Replaces a stream's data; ignored for storages.
    void setData(int stream, const QByteArray& data);
    // A stream whose bytes are the file at `path`, read only when the compound file is written (save()
    // copies it a sector at a time). -1, with `error` set, when the file cannot be read or the name is
    // not valid or taken.
    int addFileStream(int parent, const QString& name, const QString& path, QString* error = nullptr);
    // A stream's size in bytes.
    std::uint64_t size(int stream) const;

    // Writes the whole file to `device`, which must be open for writing and seekable (the header is
    // written last); false, with `error` set, when it cannot be written or a file stream cannot be read.
    bool write(QIODevice& device, QString* error = nullptr) const;

    // The whole file; empty, with `error` set, when it cannot be written.
    QByteArray toBytes(QString* error = nullptr) const;
    // Writes the file to `path` through QSaveFile, so a failed write leaves any old file as it was.
    bool save(const QString& path, QString* error = nullptr) const;

private:
    // One entry being built: its directory entry (its siblings, child and first sector are filled in by
    // toBytes), its parent, its children in the order they were added, and a stream's data.
    struct Node {
        Entry entry;
        int parent = -1;
        QList<int> children;
        QByteArray data;
        QString file; // addFileStream's file, when the data lives there
    };
    // Adds a child of `type` under `parent`, which must be a storage; -1, with `error` set, when the name
    // is not valid or a sibling already has it (case does not matter).
    int add(int parent, const QString& name, EntryType type, QString* error);

    std::uint32_t sectorSize_;
    Clsid headerClsid_{};
    // Node 0 is the root storage.
    std::vector<Node> nodes_;
};

} // namespace lumenis::interop::cfb
