#include "lumenis/interop/cfb.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <functional>

// The compound file container (docs/spec/aaf.md, "The compound file"). cfb::Reader opens an existing
// file: it reads the header, the FAT, the mini FAT and the directory, checks the directory tree, and then
// reads streams on demand. cfb::Writer lays a tree of storages and streams out as a new file in one go.
// Every number in the format is little-endian. The AAF layer (aaf.cpp, aaf_writer.cpp) keeps its objects
// in these storages and streams. The reference implementation is pyaaf2's cfb.py;
// tests/interop/tst_cfb.cpp checks both halves, on files with every edge, damaged files and the corpus.
namespace lumenis::interop::cfb {

namespace {

// Special sector numbers ([MS-CFB] 2.1).
constexpr std::uint32_t kMaxRegular = 0xFFFFFFFA;
constexpr std::uint32_t kDifSect = 0xFFFFFFFC;
constexpr std::uint32_t kFatSect = 0xFFFFFFFD;
constexpr std::uint32_t kEndOfChain = 0xFFFFFFFE;
constexpr std::uint32_t kFreeSect = 0xFFFFFFFF;
constexpr std::uint32_t kNoStream = 0xFFFFFFFF;

// The header's signature, and fixed sizes: the header, a directory entry, the DIFAT entries the header
// holds, a mini sector, the mini stream cutoff (streams smaller than this live in the mini stream) and
// the longest name in UTF-16 code units, without its terminating 0.
constexpr std::array<std::uint8_t, 8> kSignature{0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
constexpr int kHeaderSize = 512;
constexpr int kEntrySize = 128;
constexpr int kHeaderDifat = 109;
constexpr std::uint32_t kMiniSector = 64;
constexpr std::uint32_t kMiniCutoff = 4096;
constexpr int kMaxName = 31;

// Little-endian reads and writes.
std::uint16_t u16(const char* p) { return qFromLittleEndian<quint16>(p); }
std::uint32_t u32(const char* p) { return qFromLittleEndian<quint32>(p); }
std::uint64_t u64(const char* p) { return qFromLittleEndian<quint64>(p); }

void put16(QByteArray& b, qsizetype at, std::uint16_t v) { qToLittleEndian<quint16>(v, b.data() + at); }
void put32(QByteArray& b, qsizetype at, std::uint32_t v) { qToLittleEndian<quint32>(v, b.data() + at); }
void put64(QByteArray& b, qsizetype at, std::uint64_t v) { qToLittleEndian<quint64>(v, b.data() + at); }

// Whether a sector number names a sector rather than one of the special values above.
bool isRegular(std::uint32_t sector) { return sector <= kMaxRegular; }

} // namespace

int compareNames(const QString& a, const QString& b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (qsizetype i = 0; i < a.size(); ++i) {
        const char16_t x = a.at(i).toUpper().unicode();
        const char16_t y = b.at(i).toUpper().unicode();
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

bool isValidName(const QString& name) {
    if (name.isEmpty() || name.size() > kMaxName) return false;
    for (const QChar c : name) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':') || c == QLatin1Char('!')) {
            return false;
        }
    }
    return true;
}

// ---- Reader -----------------------------------------------------------------

// Sets `error`, closes the file and returns false, so a failed open leaves nothing half read.
bool Reader::fail(QString* error, const QString& message) {
    if (error) *error = QStringLiteral("%1: %2").arg(name_, message);
    close();
    return false;
}

void Reader::close() {
    file_.close();
    buffer_.close();
    buffer_.setData(QByteArray());
    io_ = nullptr;
    fat_.clear();
    miniFat_.clear();
    miniStream_.clear();
    entries_.clear();
    parent_.clear();
    kids_.clear();
    names_.clear();
    chains_.clear();
}

bool Reader::open(const QString& path, QString* error) {
    close();
    file_.setFileName(path);
    name_ = path;
    if (!file_.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, file_.errorString());
        return false;
    }
    io_ = &file_;
    return start(error);
}

bool Reader::openData(const QByteArray& data, const QString& name, QString* error) {
    close();
    name_ = name;
    buffer_.setData(data);
    buffer_.open(QIODevice::ReadOnly); // cannot fail for a read-only buffer
    io_ = &buffer_;
    return start(error);
}

// What open() and openData() share, once io_ is open.
bool Reader::start(QString* error) {
    fileSize_ = static_cast<std::uint64_t>(io_->size());
    return readHeader(error) && readFat(error) && readMiniFat(error) && readDirectory(error) && checkTree(error);
}

// The 512-byte header ([MS-CFB] 2.2): the signature at 0, the class ID at 8, the major version at 26, the
// byte order mark 0xFFFE at 28, the sector shift at 30 (9: 512 bytes, 12: 4096), the mini sector shift at
// 32 (6: 64 bytes), the number of FAT sectors at 44, the directory's first sector at 48, the mini stream
// cutoff at 56, the mini FAT's first sector and sector count at 60 and 64, the first DIFAT sector and the
// DIFAT sector count at 68 and 72, and the first 109 DIFAT entries from 76. Only version 3 with 512-byte
// sectors and version 4 with 4096-byte sectors are taken.
bool Reader::readHeader(QString* error) {
    const QByteArray h = io_->read(kHeaderSize);
    if (h.size() < kHeaderSize) return fail(error, QStringLiteral("too short for a compound file"));
    const char* p = h.constData();
    if (std::memcmp(p, kSignature.data(), kSignature.size()) != 0) {
        return fail(error, QStringLiteral("not a compound file (no signature)"));
    }
    std::memcpy(headerClsid_.data(), p + 8, headerClsid_.size());
    major_ = u16(p + 26);
    if (u16(p + 28) != 0xFFFE) return fail(error, QStringLiteral("not little-endian"));
    const std::uint16_t shift = u16(p + 30);
    if (!((major_ == 3 && shift == 9) || (major_ == 4 && shift == 12))) {
        return fail(error, QStringLiteral("version %1 with sector shift %2 is not one [MS-CFB] defines").arg(major_).arg(shift));
    }
    sectorSize_ = 1u << shift;
    if (u16(p + 32) != 6) return fail(error, QStringLiteral("mini sectors are not 64 bytes"));
    fatCount_ = u32(p + 44);
    dirStart_ = u32(p + 48);
    miniCutoff_ = u32(p + 56);
    if (miniCutoff_ != kMiniCutoff) return fail(error, QStringLiteral("mini stream cutoff is not 4096"));
    miniFatStart_ = u32(p + 60);
    miniFatCount_ = u32(p + 64);
    difatStart_ = u32(p + 68);
    difatCount_ = u32(p + 72);
    for (int i = 0; i < kHeaderDifat; ++i) headerDifat_[static_cast<std::size_t>(i)] = u32(p + 76 + 4 * i);
    return true;
}

// One sector into `data`, which has room for one. Sector n starts at byte (n + 1) × the sector size: the
// first sector's worth of the file is the header.
bool Reader::readSector(std::uint32_t sector, char* data, QString* error) {
    const std::uint64_t at = (static_cast<std::uint64_t>(sector) + 1) * sectorSize_;
    if (!isRegular(sector) || at >= fileSize_) {
        if (error) *error = QStringLiteral("%1: sector %2 is past the end of the file").arg(name_).arg(sector);
        return false;
    }
    if (!io_->seek(static_cast<qint64>(at))) {
        if (error) *error = QStringLiteral("%1: %2").arg(name_, io_->errorString());
        return false;
    }
    // The last sector may be cut short: what is missing reads as zeros.
    const qint64 got = io_->read(data, sectorSize_);
    if (got < 0) {
        if (error) *error = QStringLiteral("%1: %2").arg(name_, io_->errorString());
        return false;
    }
    std::memset(data + got, 0, sectorSize_ - static_cast<std::uint32_t>(got));
    return true;
}

bool Reader::chain(const std::vector<std::uint32_t>& table, std::uint32_t start, std::vector<std::uint32_t>& out,
                   std::uint64_t limit, QString* error) const {
    out.clear();
    if (start == kEndOfChain || start == kFreeSect) return true;
    // Followed to its end, so that a loop is found even past what is kept:
    // no chain is longer than the table.
    std::uint32_t s = start;
    std::size_t steps = 0;
    while (s != kEndOfChain) {
        if (s >= table.size() || steps >= table.size()) {
            if (error) *error = QStringLiteral("%1: a sector chain from %2 is broken or loops").arg(name_).arg(start);
            return false;
        }
        if (out.size() < limit) out.push_back(s);
        ++steps;
        s = table[s];
    }
    return true;
}

// The sectors' contents one after another; fails at the first that cannot be read.
bool Reader::readChain(const std::vector<std::uint32_t>& sectors, QByteArray& out, QString* error) {
    out.resize(static_cast<qsizetype>(sectors.size() * sectorSize_));
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        if (!readSector(sectors[i], out.data() + i * sectorSize_, error)) return false;
    }
    return true;
}

// The FAT. Its sectors are listed in the header's 109 DIFAT entries, then in DIFAT sectors: each holds
// one sector number fewer than it can, and its last entry names the next DIFAT sector. Each FAT sector
// holds sectorSize / 4 entries, and entry n is the sector after sector n in its chain (or ENDOFCHAIN,
// FREESECT, FATSECT or DIFSECT).
bool Reader::readFat(QString* error) {
    const std::uint64_t sectorsInFile = fileSize_ / sectorSize_; // header included: an upper bound
    if (fatCount_ > sectorsInFile) return fail(error, QStringLiteral("more FAT sectors than the file holds"));
    std::vector<std::uint32_t> fatSectors;
    for (int i = 0; i < kHeaderDifat && fatSectors.size() < fatCount_; ++i) {
        fatSectors.push_back(headerDifat_[static_cast<std::size_t>(i)]);
    }
    const std::uint32_t perSector = sectorSize_ / 4;
    std::uint32_t d = difatStart_;
    QByteArray buffer(static_cast<qsizetype>(sectorSize_), Qt::Uninitialized);
    // `n` is bounded by the file's sector count too, so a DIFAT chain that loops ends.
    for (std::uint32_t n = 0; n < difatCount_ && fatSectors.size() < fatCount_; ++n) {
        if (n > sectorsInFile || !readSector(d, buffer.data(), error)) {
            return fail(error, QStringLiteral("the DIFAT is broken"));
        }
        for (std::uint32_t i = 0; i + 1 < perSector && fatSectors.size() < fatCount_; ++i) {
            fatSectors.push_back(u32(buffer.constData() + 4 * i));
        }
        d = u32(buffer.constData() + 4 * (perSector - 1));
    }
    if (fatSectors.size() < fatCount_) return fail(error, QStringLiteral("the DIFAT lists too few FAT sectors"));
    fat_.resize(static_cast<std::size_t>(fatCount_) * perSector);
    for (std::size_t i = 0; i < fatSectors.size(); ++i) {
        QString why;
        if (!readSector(fatSectors[i], buffer.data(), &why)) return fail(error, QStringLiteral("a FAT sector is missing"));
        for (std::uint32_t k = 0; k < perSector; ++k) fat_[i * perSector + k] = u32(buffer.constData() + 4 * k);
    }
    return true;
}

// The mini FAT: a chain of regular sectors from the header's mini FAT start, read whole. Entry n is the
// mini sector after mini sector n in its chain.
bool Reader::readMiniFat(QString* error) {
    std::vector<std::uint32_t> sectors;
    QString why;
    if (!chain(fat_, miniFatStart_, sectors, fat_.size(), &why)) return fail(error, QStringLiteral("the mini FAT's chain is broken"));
    QByteArray data;
    if (!readChain(sectors, data, &why)) return fail(error, QStringLiteral("the mini FAT is missing"));
    miniFat_.resize(static_cast<std::size_t>(data.size() / 4));
    for (std::size_t i = 0; i < miniFat_.size(); ++i) miniFat_[i] = u32(data.constData() + 4 * i);
    return true;
}

// The directory: a chain of 128-byte entries ([MS-CFB] 2.6.1). Each holds the name (UTF-16, up to 32 code
// units with its terminating 0) at 0, the name's length in bytes, the 0 included, at 64, the type at 66,
// the colour at 67 (not read: the tree is checked as a tree, not as a red-black one), the left sibling,
// right sibling and child at 68, 72 and 76, the class ID at 80, the state bits at 96, the creation and
// modification times at 100 and 108, the first sector at 116 and the size at 120. Entries of an unknown
// type read as empty.
bool Reader::readDirectory(QString* error) {
    std::vector<std::uint32_t> sectors;
    QString why;
    if (!chain(fat_, dirStart_, sectors, fat_.size(), &why) || sectors.empty()) {
        return fail(error, QStringLiteral("the directory's chain is broken"));
    }
    QByteArray data;
    if (!readChain(sectors, data, &why)) return fail(error, QStringLiteral("the directory is missing"));
    const qsizetype count = data.size() / kEntrySize;
    entries_.resize(static_cast<std::size_t>(count));
    for (qsizetype i = 0; i < count; ++i) {
        const char* p = data.constData() + i * kEntrySize;
        Entry& e = entries_[static_cast<std::size_t>(i)];
        const int nameBytes = std::min<int>(u16(p + 64), 64);
        const int chars = std::max(0, nameBytes / 2 - 1);
        e.name = QString(chars, Qt::Uninitialized);
        for (int c = 0; c < chars; ++c) e.name[c] = QChar(u16(p + 2 * c));
        const std::uint8_t type = static_cast<std::uint8_t>(p[66]);
        e.type = type == 1 || type == 2 || type == 5 ? static_cast<EntryType>(type) : EntryType::Empty;
        e.left = u32(p + 68);
        e.right = u32(p + 72);
        e.child = u32(p + 76);
        std::memcpy(e.clsid.data(), p + 80, e.clsid.size());
        e.stateBits = u32(p + 96);
        e.created = u64(p + 100);
        e.modified = u64(p + 108);
        e.start = u32(p + 116);
        e.size = u64(p + 120);
        // Version 3 files may hold anything in the size's high half ([MS-CFB] 2.6.1).
        if (major_ == 3) e.size &= 0xFFFFFFFFu;
        // Storages have no size; the root's is the mini stream's.
        if (!e.isStream()) {
            if (e.type != EntryType::Root) e.size = 0;
        }
    }
    if (entries_.empty() || entries_[0].type != EntryType::Root) {
        return fail(error, QStringLiteral("the directory has no root"));
    }
    // The mini stream is the root's chain of regular sectors.
    const Entry& rootEntry = entries_[0];
    if (!chain(fat_, rootEntry.start, miniStream_, fat_.size(), &why)) {
        return fail(error, QStringLiteral("the mini stream's chain is broken"));
    }
    if (static_cast<std::uint64_t>(miniStream_.size()) * sectorSize_ < rootEntry.size) {
        return fail(error, QStringLiteral("the mini stream is shorter than the root says"));
    }
    return true;
}

// Builds parent_ and kids_ from the directory. Each storage's `child` is the root of a binary tree of its
// children, linked by `left` and `right`; an in-order walk (with an explicit stack) gives them in
// directory order. Every entry may be reached once only, so a tree that loops or shares an entry, or that
// names an empty entry, the root or an index past the directory, is an error rather than a hang or a
// crash.
bool Reader::checkTree(QString* error) {
    const std::size_t n = entries_.size();
    parent_.assign(n, -1);
    kids_.assign(n, {});
    std::vector<bool> seen(n, false);
    seen[0] = true;
    // Each storage's children: an in-order walk of its subtree.
    std::vector<int> storages{0};
    while (!storages.empty()) {
        const int s = storages.back();
        storages.pop_back();
        std::vector<std::uint32_t> stack;
        std::uint32_t node = entries_[static_cast<std::size_t>(s)].child;
        QList<int>& kids = kids_[static_cast<std::size_t>(s)];
        while (node != kNoStream || !stack.empty()) {
            while (node != kNoStream) {
                if (node >= n || seen[node] || entries_[node].type == EntryType::Empty ||
                    entries_[node].type == EntryType::Root) {
                    return fail(error, QStringLiteral("the directory tree is broken at entry %1").arg(node));
                }
                seen[node] = true;
                stack.push_back(node);
                node = entries_[node].left;
            }
            const std::uint32_t at = stack.back();
            stack.pop_back();
            kids.append(static_cast<int>(at));
            parent_[at] = s;
            if (entries_[at].isStorage()) storages.push_back(static_cast<int>(at));
            node = entries_[at].right;
        }
    }
    return true;
}

QList<int> Reader::children(int storage) const {
    if (storage < 0 || static_cast<std::size_t>(storage) >= kids_.size()) return {};
    return kids_[static_cast<std::size_t>(storage)];
}

namespace {

// A name with each UTF-16 code unit upper-cased, as compareNames compares them: names that compare
// equal have the same key.
QString nameKey(const QString& name) {
    QString key(name.size(), Qt::Uninitialized);
    for (qsizetype i = 0; i < name.size(); ++i) key[i] = name.at(i).toUpper();
    return key;
}

} // namespace

// compareNames ignores case, as the tree's order does. A small storage is searched; a large one (an
// AAF set of thousands of mobs, or a damaged file's thousands of siblings) through an index of its
// names made on first use, since the AAF layer looks children up by name for every object and a
// search each time made reading such a file quadratic (found while fuzzing: a third of fuzz_aaf's
// time went here). The first child of a name wins, as the search finds it.
int Reader::child(int storage, const QString& name) const {
    const QList<int> kids = children(storage);
    constexpr qsizetype kSearched = 16;
    if (kids.size() <= kSearched) {
        for (const int id : kids) {
            if (compareNames(entries_[static_cast<std::size_t>(id)].name, name) == 0) return id;
        }
        return -1;
    }
    auto it = names_.find(storage);
    if (it == names_.end()) {
        QHash<QString, int> index;
        index.reserve(kids.size());
        // insert() replaces, so in reverse order: each name keeps its first child.
        for (auto k = kids.crbegin(); k != kids.crend(); ++k) index.insert(nameKey(entries_[static_cast<std::size_t>(*k)].name), *k);
        it = names_.insert(storage, std::move(index));
    }
    return it->value(nameKey(name), -1);
}

int Reader::find(const QString& path) const {
    int at = root();
    for (const QString& part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        at = child(at, part);
        if (at < 0) return -1;
    }
    return at;
}

QString Reader::path(int id) const {
    QStringList parts;
    while (id > 0) {
        parts.prepend(entries_[static_cast<std::size_t>(id)].name);
        id = parent_[static_cast<std::size_t>(id)];
    }
    return QLatin1Char('/') + parts.join(QLatin1Char('/'));
}

QStringList Reader::paths() const {
    QStringList out;
    std::function<void(int)> walk = [&](int storage) {
        for (const int id : children(storage)) {
            out.append(path(id));
            if (entries_[static_cast<std::size_t>(id)].isStorage()) walk(id);
        }
    };
    if (!entries_.empty()) walk(root());
    return out;
}

bool Reader::isMini(int stream) const {
    return stream != root() && entries_[static_cast<std::size_t>(stream)].size < miniCutoff_;
}

// Only as many sectors as the size needs are kept, but chain() follows the chain to its end, so a loop
// past the size is still found.
const std::vector<std::uint32_t>* Reader::streamChain(int stream, QString* error) {
    auto it = chains_.find(stream);
    if (it != chains_.end()) return &it.value();
    const Entry& e = entries_[static_cast<std::size_t>(stream)];
    const bool mini = isMini(stream);
    const std::uint64_t unit = mini ? kMiniSector : sectorSize_;
    // Rounded up without overflow: a version 4 file's 64-bit size can be anything, and
    // (size + unit - 1) wrapped to 0 sectors for 0xFFFFFFFFFFFFFF93, leaving read() an empty chain
    // to index (found by fuzz_cfb, fuzz/regressions/cfb). A size past the chain is refused below.
    const std::uint64_t needed = e.size / unit + (e.size % unit != 0 ? 1 : 0);
    std::vector<std::uint32_t> sectors;
    if (needed > 0) {
        if (!chain(mini ? miniFat_ : fat_, e.start, sectors, needed, error)) return nullptr;
        if (sectors.size() < needed) {
            if (error) *error = QStringLiteral("%1: stream %2 is shorter than its size").arg(name_, path(stream));
            return nullptr;
        }
    }
    return &chains_.insert(stream, std::move(sectors)).value();
}

// Reads in pieces. A small stream's mini sectors each lie at their place in the mini stream, itself a
// chain of regular sectors, so they are read one at a time. A large stream is read a run of consecutive
// sectors at a time, so that a stream laid out in one run (as the writer lays them out) is read with one
// seek.
QByteArray Reader::read(int stream, std::uint64_t offset, std::uint64_t length, QString* error) {
    if (stream < 0 || static_cast<std::size_t>(stream) >= entries_.size() || !entries_[static_cast<std::size_t>(stream)].isStream()) {
        if (error) *error = QStringLiteral("%1: no such stream").arg(name_);
        return {};
    }
    const Entry& e = entries_[static_cast<std::size_t>(stream)];
    if (offset >= e.size) return {};
    length = std::min(length, e.size - offset);
    const std::vector<std::uint32_t>* sectors = streamChain(stream, error);
    if (!sectors) return {};
    QByteArray out(static_cast<qsizetype>(length), Qt::Uninitialized);
    char* to = out.data();
    const bool mini = isMini(stream);
    const std::uint64_t unit = mini ? kMiniSector : sectorSize_;
    std::uint64_t pos = offset;
    const std::uint64_t end = offset + length;
    while (pos < end) {
        const std::uint64_t index = pos / unit;
        const std::uint64_t inner = pos % unit;
        std::uint64_t at = 0;    // byte offset in the file
        std::uint64_t count = 0; // bytes that lie together there
        if (mini) {
            // The mini sector's offset in the mini stream, then the regular sector of the mini stream
            // that holds it.
            const std::uint64_t inMini = static_cast<std::uint64_t>((*sectors)[index]) * kMiniSector + inner;
            const std::uint64_t regular = inMini / sectorSize_;
            if (regular >= miniStream_.size()) {
                if (error) *error = QStringLiteral("%1: stream %2 lies past the mini stream").arg(name_, path(stream));
                return {};
            }
            at = (static_cast<std::uint64_t>(miniStream_[regular]) + 1) * sectorSize_ + inMini % sectorSize_;
            count = std::min(end - pos, kMiniSector - inner);
        } else {
            // Read runs of consecutive sectors at once.
            std::uint64_t last = index;
            while ((last + 1) * unit < end && (*sectors)[last + 1] == (*sectors)[last] + 1) ++last;
            at = (static_cast<std::uint64_t>((*sectors)[index]) + 1) * sectorSize_ + inner;
            count = std::min(end, (last + 1) * unit) - pos;
        }
        if (at + count > fileSize_ || !io_->seek(static_cast<qint64>(at)) ||
            io_->read(to, static_cast<qint64>(count)) != static_cast<qint64>(count)) {
            if (error) *error = QStringLiteral("%1: stream %2 is cut short").arg(name_, path(stream));
            return {};
        }
        to += count;
        pos += count;
    }
    return out;
}

// ---- Writer -----------------------------------------------------------------

// Node 0 is the root storage, "Root Entry". A sector size other than 512 is taken as 4096.
Writer::Writer(int sectorSize) : sectorSize_(sectorSize == 512 ? 512u : 4096u) {
    Node rootNode;
    rootNode.entry.name = QStringLiteral("Root Entry");
    rootNode.entry.type = EntryType::Root;
    nodes_.push_back(rootNode);
}

void Writer::setClsid(int storage, const Clsid& clsid) {
    Node& n = nodes_[static_cast<std::size_t>(storage)];
    if (n.entry.isStorage()) n.entry.clsid = clsid;
}

void Writer::setTimes(int id, std::uint64_t created, std::uint64_t modified) {
    Node& n = nodes_[static_cast<std::size_t>(id)];
    // Streams carry no times, and the root no creation time ([MS-CFB] 2.6.1).
    if (!n.entry.isStorage()) return;
    if (n.entry.type != EntryType::Root) n.entry.created = created;
    n.entry.modified = modified;
}

void Writer::setStateBits(int id, std::uint32_t bits) {
    Node& n = nodes_[static_cast<std::size_t>(id)];
    if (n.entry.isStorage()) n.entry.stateBits = bits;
}

// Adds a child node; the name must be valid and not taken by a sibling (case does not matter).
int Writer::add(int parent, const QString& name, EntryType type, QString* error) {
    if (parent < 0 || static_cast<std::size_t>(parent) >= nodes_.size() ||
        !nodes_[static_cast<std::size_t>(parent)].entry.isStorage()) {
        if (error) *error = QStringLiteral("%1: the parent is not a storage").arg(name);
        return -1;
    }
    if (!isValidName(name)) {
        if (error) *error = QStringLiteral("%1: not a valid compound file name").arg(name);
        return -1;
    }
    for (const int sibling : nodes_[static_cast<std::size_t>(parent)].children) {
        if (compareNames(nodes_[static_cast<std::size_t>(sibling)].entry.name, name) == 0) {
            if (error) *error = QStringLiteral("%1: the name is taken").arg(name);
            return -1;
        }
    }
    Node n;
    n.entry.name = name;
    n.entry.type = type;
    n.parent = parent;
    const int id = static_cast<int>(nodes_.size());
    nodes_.push_back(n);
    nodes_[static_cast<std::size_t>(parent)].children.append(id);
    return id;
}

int Writer::addStorage(int parent, const QString& name, QString* error) {
    return add(parent, name, EntryType::Storage, error);
}

int Writer::addStream(int parent, const QString& name, const QByteArray& data, QString* error) {
    const int id = add(parent, name, EntryType::Stream, error);
    if (id >= 0) setData(id, data);
    return id;
}

void Writer::setData(int stream, const QByteArray& data) {
    Node& n = nodes_[static_cast<std::size_t>(stream)];
    if (!n.entry.isStream()) return;
    n.data = data;
    n.file.clear();
    n.entry.size = static_cast<std::uint64_t>(data.size());
}

// The file's size now; it is read when the compound file is written (AAF's embedded media, which may not
// fit in memory).
int Writer::addFileStream(int parent, const QString& name, const QString& path, QString* error) {
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) {
        if (error) *error = QStringLiteral("%1: cannot read %2").arg(name, path);
        return -1;
    }
    const int id = add(parent, name, EntryType::Stream, error);
    if (id < 0) return -1;
    Node& n = nodes_[static_cast<std::size_t>(id)];
    n.file = info.absoluteFilePath();
    n.entry.size = static_cast<std::uint64_t>(info.size());
    return id;
}

std::uint64_t Writer::size(int stream) const {
    return nodes_[static_cast<std::size_t>(stream)].entry.size;
}

// Lays the whole file out in memory. `out` starts with one sector's room for the header (512 bytes of it
// used; with 4096-byte sectors the rest is padding), so that sector n lands at byte (n + 1) × ss, and
// `fat` gets one entry for each sector appended. The order is: streams of 4096 bytes or more, the mini
// stream (the root's chain), the mini FAT, the directory, then the FAT and the DIFAT; the header is
// filled in last, when every count and first sector is known.
bool Writer::write(QIODevice& device, QString* error) const {
    const std::uint32_t ss = sectorSize_;
    const std::uint32_t perSector = ss / 4;
    // For files past 2 GB with 4096-byte sectors, the sector holding byte
    // 0x7FFFFF00 is the range lock sector: kept out of every chain.
    const std::uint32_t rangeLock = ss == 4096 ? 0x7FFFFF00u / ss - 1 : kMaxRegular;
    std::vector<std::uint32_t> fat;
    // Bytes go to the device as they are laid out; the first sector is the header's room, filled in last.
    bool failedWrite = false;
    auto emitBytes = [&](const char* data, qint64 size) {
        if (!failedWrite && device.write(data, size) != size) failedWrite = true;
    };
    const QByteArray zeros(static_cast<qsizetype>(ss), '\0');
    emitBytes(zeros.constData(), ss);

    // The next sector's number, its FAT entry ENDOFCHAIN until a chain links it on. The range lock sector
    // is skipped: left zeroed, marked ENDOFCHAIN, and part of no chain. ENDOFCHAIN (not FREESECT) is what
    // [MS-CFB] 2.2 asks of a file past 2 GB: the sector is allocated in the FAT, so that no writer reuses
    // it, yet holds no stream's data (no chain names it). Readers such as olefile only follow chains.
    auto nextSector = [&]() {
        auto s = static_cast<std::uint32_t>(fat.size());
        if (s == rangeLock) {
            fat.push_back(kEndOfChain);
            emitBytes(zeros.constData(), ss);
            ++s;
        }
        fat.push_back(kEndOfChain);
        return s;
    };
    // Appends `data` as a chain of new sectors; returns its first, or ENDOFCHAIN.
    auto writeChain = [&](const QByteArray& data) {
        std::uint32_t first = kEndOfChain;
        std::uint32_t previous = kEndOfChain;
        for (qsizetype at = 0; at < data.size(); at += ss) {
            const std::uint32_t s = nextSector();
            if (previous == kEndOfChain) first = s;
            else fat[previous] = s;
            previous = s;
            const qsizetype n = std::min<qsizetype>(ss, data.size() - at);
            emitBytes(data.constData() + at, n);
            if (n < static_cast<qsizetype>(ss)) emitBytes(zeros.constData(), static_cast<qint64>(ss) - n);
        }
        return first;
    };
    // The same for a stream kept in a file, read a sector at a time; false when it cannot be read whole.
    auto writeFileChain = [&](const QString& path, std::uint64_t size, std::uint32_t* first) {
        *first = kEndOfChain;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        std::uint32_t previous = kEndOfChain;
        QByteArray sector(static_cast<qsizetype>(ss), '\0');
        for (std::uint64_t at = 0; at < size; at += ss) {
            const std::uint32_t s = nextSector();
            if (previous == kEndOfChain) *first = s;
            else fat[previous] = s;
            previous = s;
            const auto want = static_cast<qint64>(std::min<std::uint64_t>(ss, size - at));
            if (f.read(sector.data(), want) != want) return false;
            std::memset(sector.data() + want, 0, static_cast<std::size_t>(ss - static_cast<std::uint64_t>(want)));
            emitBytes(sector.constData(), ss);
        }
        return true;
    };
    // A small stream's bytes, from memory or its file.
    auto dataOf = [&](const Node& n) {
        if (n.file.isEmpty()) return n.data;
        QFile f(n.file);
        return f.open(QIODevice::ReadOnly) ? f.read(static_cast<qint64>(n.entry.size)) : QByteArray();
    };

    std::vector<Entry> entries(nodes_.size());
    for (std::size_t i = 0; i < nodes_.size(); ++i) entries[i] = nodes_[i].entry;

    // Large streams in sectors of their own, in the order they were added.
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        Entry& e = entries[i];
        if (!e.isStream()) continue;
        if (sectorSize_ == 512 && e.size > 0xFFFFFFFFu) {
            if (error) *error = QStringLiteral("%1: a stream larger than 4 GB needs 4096-byte sectors").arg(e.name);
            return false;
        }
        if (e.size < kMiniCutoff) continue;
        if (nodes_[i].file.isEmpty()) {
            e.start = writeChain(nodes_[i].data);
        } else if (!writeFileChain(nodes_[i].file, e.size, &e.start)) {
            if (error) *error = QStringLiteral("%1: cannot read %2").arg(e.name, nodes_[i].file);
            return false;
        }
    }
    // Small streams in the mini stream, 64 bytes at a time.
    QByteArray mini;
    std::vector<std::uint32_t> miniFat;
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        Entry& e = entries[i];
        if (!e.isStream() || e.size >= kMiniCutoff) continue;
        e.start = kEndOfChain;
        const QByteArray data = dataOf(nodes_[i]);
        if (static_cast<std::uint64_t>(data.size()) != e.size) {
            if (error) *error = QStringLiteral("%1: cannot read %2").arg(e.name, nodes_[i].file);
            return false;
        }
        for (qsizetype at = 0; at < data.size(); at += kMiniSector) {
            const auto m = static_cast<std::uint32_t>(miniFat.size());
            if (at == 0) e.start = m;
            else miniFat.back() = m;
            miniFat.push_back(kEndOfChain);
            QByteArray piece = data.mid(at, kMiniSector);
            piece.append(QByteArray(static_cast<qsizetype>(kMiniSector) - piece.size(), '\0'));
            mini.append(piece);
        }
    }
    entries[0].start = writeChain(mini);
    entries[0].size = static_cast<std::uint64_t>(mini.size());
    // The mini FAT, padded to whole sectors with FREESECT.
    QByteArray miniFatBytes;
    if (!miniFat.empty()) {
        const std::size_t padded = (miniFat.size() + perSector - 1) / perSector * perSector;
        miniFatBytes = QByteArray(static_cast<qsizetype>(padded * 4), '\0');
        for (std::size_t i = 0; i < padded; ++i) {
            put32(miniFatBytes, static_cast<qsizetype>(4 * i), i < miniFat.size() ? miniFat[i] : kFreeSect);
        }
    }
    const std::uint32_t miniFatStart = writeChain(miniFatBytes);
    const auto miniFatCount = static_cast<std::uint32_t>(miniFatBytes.size() / ss);

    // The directory: each storage's children as a balanced tree, all black.
    std::function<std::uint32_t(const QList<int>&, qsizetype, qsizetype)> build =
        [&](const QList<int>& sorted, qsizetype lo, qsizetype hi) -> std::uint32_t {
        if (lo > hi) return kNoStream;
        const qsizetype mid = lo + (hi - lo) / 2;
        Entry& e = entries[static_cast<std::size_t>(sorted[mid])];
        e.left = build(sorted, lo, mid - 1);
        e.right = build(sorted, mid + 1, hi);
        return static_cast<std::uint32_t>(sorted[mid]);
    };
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (!entries[i].isStorage()) continue;
        QList<int> sorted = nodes_[i].children;
        std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
            return compareNames(entries[static_cast<std::size_t>(a)].name, entries[static_cast<std::size_t>(b)].name) < 0;
        });
        entries[i].child = build(sorted, 0, sorted.size() - 1);
    }
    // The directory's entries in node order, so that node i is entry i, padded to whole sectors with
    // empty entries (no siblings, no child). The layout is the one readDirectory reads; every node is
    // black (colour 1), and storages other than the root have no first sector or size.
    const std::size_t perDirSector = ss / kEntrySize;
    const std::size_t dirEntries = (entries.size() + perDirSector - 1) / perDirSector * perDirSector;
    QByteArray dir(static_cast<qsizetype>(dirEntries * kEntrySize), '\0');
    for (std::size_t i = 0; i < dirEntries; ++i) {
        const auto at = static_cast<qsizetype>(i * kEntrySize);
        if (i >= entries.size()) {
            put32(dir, at + 68, kNoStream);
            put32(dir, at + 72, kNoStream);
            put32(dir, at + 76, kNoStream);
            continue;
        }
        const Entry& e = entries[i];
        for (qsizetype c = 0; c < e.name.size(); ++c) put16(dir, at + 2 * c, e.name.at(c).unicode());
        put16(dir, at + 64, static_cast<std::uint16_t>((e.name.size() + 1) * 2));
        dir[at + 66] = static_cast<char>(e.type);
        dir[at + 67] = 1; // black
        put32(dir, at + 68, e.left);
        put32(dir, at + 72, e.right);
        put32(dir, at + 76, e.child);
        std::memcpy(dir.data() + at + 80, e.clsid.data(), e.clsid.size());
        put32(dir, at + 96, e.stateBits);
        put64(dir, at + 100, e.created);
        put64(dir, at + 108, e.modified);
        put32(dir, at + 116, e.isStorage() && e.type != EntryType::Root ? 0 : e.start);
        put64(dir, at + 120, e.isStorage() && e.type != EntryType::Root ? 0 : e.size);
    }
    const std::uint32_t dirStart = writeChain(dir);
    const auto dirCount = static_cast<std::uint32_t>(dir.size() / ss);

    // The FAT and DIFAT come last; they must cover themselves.
    // The FAT's and the DIFAT's own sectors need FAT entries too, so their counts are found by growing
    // them until they cover everything, themselves included. When the file grows past the range lock
    // sector, it takes one entry more. DIFAT sectors are needed past the header's 109 FAT sector numbers,
    // each holding perSector - 1 of them.
    std::uint32_t fatCount = 0;
    std::uint32_t difatCount = 0;
    for (;;) {
        std::uint64_t total = fat.size() + fatCount + difatCount;
        if (fat.size() <= rangeLock && total > rangeLock) ++total;
        const auto needFat = static_cast<std::uint32_t>((total + perSector - 1) / perSector);
        const std::uint32_t needDifat =
            needFat > kHeaderDifat ? (needFat - kHeaderDifat + perSector - 2) / (perSector - 1) : 0;
        if (needFat == fatCount && needDifat == difatCount) break;
        fatCount = std::max(fatCount, needFat);
        difatCount = std::max(difatCount, needDifat);
    }
    std::vector<std::uint32_t> fatSectors;
    for (std::uint32_t i = 0; i < fatCount; ++i) {
        const std::uint32_t s = nextSector();
        fat[s] = kFatSect;
        fatSectors.push_back(s);
    }
    std::vector<std::uint32_t> difatSectors;
    for (std::uint32_t i = 0; i < difatCount; ++i) {
        const std::uint32_t s = nextSector();
        fat[s] = kDifSect;
        difatSectors.push_back(s);
    }
    QByteArray fatBytes(static_cast<qsizetype>(fatCount) * ss, '\0');
    for (std::size_t i = 0; i < static_cast<std::size_t>(fatCount) * perSector; ++i) {
        put32(fatBytes, static_cast<qsizetype>(4 * i), i < fat.size() ? fat[i] : kFreeSect);
    }
    emitBytes(fatBytes.constData(), fatBytes.size());
    // DIFAT sectors: the FAT sector numbers from the 110th on, then the next DIFAT sector (ENDOFCHAIN in
    // the last one).
    for (std::uint32_t d = 0; d < difatCount; ++d) {
        QByteArray sector(static_cast<qsizetype>(ss), '\0');
        for (std::uint32_t k = 0; k + 1 < perSector; ++k) {
            const std::size_t f = kHeaderDifat + static_cast<std::size_t>(d) * (perSector - 1) + k;
            put32(sector, 4 * static_cast<qsizetype>(k), f < fatSectors.size() ? fatSectors[f] : kFreeSect);
        }
        put32(sector, 4 * static_cast<qsizetype>(perSector - 1), d + 1 < difatCount ? difatSectors[d + 1] : kEndOfChain);
        emitBytes(sector.constData(), sector.size());
    }

    // The header ([MS-CFB] 2.2).
    QByteArray h(static_cast<qsizetype>(ss), '\0');
    std::memcpy(h.data(), kSignature.data(), kSignature.size());
    std::memcpy(h.data() + 8, headerClsid_.data(), headerClsid_.size());
    // The minor version (0x3E) at 24, the version and the sector shift, the mini sector shift at 32, and
    // the number of directory sectors at 40, which version 3 files leave 0. The transaction signature at
    // 52 is 0. The other fields are as readHeader reads them.
    put16(h, 24, 0x003E);
    put16(h, 26, ss == 512 ? 3 : 4);
    put16(h, 28, 0xFFFE);
    put16(h, 30, ss == 512 ? 9 : 12);
    put16(h, 32, 6);
    put32(h, 40, ss == 512 ? 0 : dirCount);
    put32(h, 44, fatCount);
    put32(h, 48, dirStart);
    put32(h, 52, 0);
    put32(h, 56, kMiniCutoff);
    put32(h, 60, miniFatStart);
    put32(h, 64, miniFatCount);
    put32(h, 68, difatCount ? difatSectors.front() : kEndOfChain);
    put32(h, 72, difatCount);
    for (int i = 0; i < kHeaderDifat; ++i) {
        const auto k = static_cast<std::size_t>(i);
        put32(h, 76 + 4 * i, k < fatSectors.size() ? fatSectors[k] : kFreeSect);
    }
    if (!failedWrite && (!device.seek(0) || device.write(h) != h.size())) failedWrite = true;
    if (failedWrite) {
        if (error) *error = device.errorString().isEmpty() ? QStringLiteral("cannot write the file") : device.errorString();
        return false;
    }
    return true;
}

QByteArray Writer::toBytes(QString* error) const {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !write(buffer, error)) return {};
    return bytes;
}


// Writes atomically: the file is replaced only when every byte is written. Straight to the file, so
// streams kept in files (addFileStream) never have to fit in memory.
bool Writer::save(const QString& path, QString* error) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    QString why;
    if (!write(file, &why) || !file.commit()) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, why.isEmpty() ? file.errorString() : why);
        return false;
    }
    return true;
}

} // namespace lumenis::interop::cfb
