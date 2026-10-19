#pragma once

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

enum class EntryType : std::uint8_t { Empty = 0, Storage = 1, Stream = 2, Root = 5 };

struct Entry {
    QString name;
    EntryType type = EntryType::Empty;
    Clsid clsid{};
    std::uint32_t stateBits = 0;
    std::uint64_t created = 0;  // FILETIME
    std::uint64_t modified = 0; // FILETIME
    std::uint32_t start = 0;    // first sector (or mini sector)
    std::uint64_t size = 0;     // bytes, for a stream
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

    bool open(const QString& path, QString* error = nullptr);
    void close();
    bool isOpen() const { return file_.isOpen(); }

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
    QString path(int id) const;

    // Up to `length` bytes of a stream from `offset`; fewer at its end. Sets
    // `error` and returns nothing when its sectors are damaged.
    QByteArray read(int stream, std::uint64_t offset, std::uint64_t length, QString* error = nullptr);
    QByteArray readAll(int stream, QString* error = nullptr) {
        return read(stream, 0, entry(stream).size, error);
    }

private:
    bool fail(QString* error, const QString& message);
    bool readHeader(QString* error);
    bool readFat(QString* error);
    bool readMiniFat(QString* error);
    bool readDirectory(QString* error);
    bool checkTree(QString* error);
    // The chain of sectors from `start`; false when it loops or leaves the table.
    bool chain(const std::vector<std::uint32_t>& table, std::uint32_t start, std::vector<std::uint32_t>& out,
               std::uint64_t limit, QString* error) const;
    bool readSector(std::uint32_t sector, char* data, QString* error);
    bool readChain(const std::vector<std::uint32_t>& sectors, QByteArray& out, QString* error);
    const std::vector<std::uint32_t>* streamChain(int stream, QString* error);
    bool isMini(int stream) const;

    QFile file_;
    std::uint64_t fileSize_ = 0;
    int major_ = 0;
    std::uint32_t sectorSize_ = 512;
    std::uint32_t miniCutoff_ = 4096;
    Clsid headerClsid_{};
    std::uint32_t dirStart_ = 0;
    std::uint32_t miniFatStart_ = 0;
    std::uint32_t miniFatCount_ = 0;
    std::uint32_t difatStart_ = 0;
    std::uint32_t difatCount_ = 0;
    std::uint32_t fatCount_ = 0;
    std::array<std::uint32_t, 109> headerDifat_{};
    std::vector<std::uint32_t> fat_;
    std::vector<std::uint32_t> miniFat_;
    std::vector<std::uint32_t> miniStream_; // the root's chain: where mini sectors live
    std::vector<Entry> entries_;
    std::vector<int> parent_;
    std::vector<QList<int>> kids_;
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
    void setClsid(int storage, const Clsid& clsid);
    void setTimes(int id, std::uint64_t created, std::uint64_t modified);
    void setStateBits(int id, std::uint32_t bits);
    // -1, with `error` set, when the name is not valid or already taken.
    int addStorage(int parent, const QString& name, QString* error = nullptr);
    int addStream(int parent, const QString& name, const QByteArray& data, QString* error = nullptr);
    void setData(int stream, const QByteArray& data);

    // The whole file; empty, with `error` set, when it cannot be written.
    QByteArray toBytes(QString* error = nullptr) const;
    bool save(const QString& path, QString* error = nullptr) const;

private:
    struct Node {
        Entry entry;
        int parent = -1;
        QList<int> children;
        QByteArray data;
    };
    int add(int parent, const QString& name, EntryType type, QString* error);

    std::uint32_t sectorSize_;
    Clsid headerClsid_{};
    std::vector<Node> nodes_;
};

} // namespace lumenis::interop::cfb
