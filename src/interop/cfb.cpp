#include "lumenis/interop/cfb.h"

#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <functional>

namespace lumenis::interop::cfb {

namespace {

// Special sector numbers ([MS-CFB] 2.1).
constexpr std::uint32_t kMaxRegular = 0xFFFFFFFA;
constexpr std::uint32_t kDifSect = 0xFFFFFFFC;
constexpr std::uint32_t kFatSect = 0xFFFFFFFD;
constexpr std::uint32_t kEndOfChain = 0xFFFFFFFE;
constexpr std::uint32_t kFreeSect = 0xFFFFFFFF;
constexpr std::uint32_t kNoStream = 0xFFFFFFFF;

constexpr std::array<std::uint8_t, 8> kSignature{0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
constexpr int kHeaderSize = 512;
constexpr int kEntrySize = 128;
constexpr int kHeaderDifat = 109;
constexpr std::uint32_t kMiniSector = 64;
constexpr std::uint32_t kMiniCutoff = 4096;
constexpr int kMaxName = 31;

std::uint16_t u16(const char* p) { return qFromLittleEndian<quint16>(p); }
std::uint32_t u32(const char* p) { return qFromLittleEndian<quint32>(p); }
std::uint64_t u64(const char* p) { return qFromLittleEndian<quint64>(p); }

void put16(QByteArray& b, qsizetype at, std::uint16_t v) { qToLittleEndian<quint16>(v, b.data() + at); }
void put32(QByteArray& b, qsizetype at, std::uint32_t v) { qToLittleEndian<quint32>(v, b.data() + at); }
void put64(QByteArray& b, qsizetype at, std::uint64_t v) { qToLittleEndian<quint64>(v, b.data() + at); }

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

bool Reader::fail(QString* error, const QString& message) {
    if (error) *error = QStringLiteral("%1: %2").arg(file_.fileName(), message);
    close();
    return false;
}

void Reader::close() {
    file_.close();
    fat_.clear();
    miniFat_.clear();
    miniStream_.clear();
    entries_.clear();
    parent_.clear();
    kids_.clear();
    chains_.clear();
}

bool Reader::open(const QString& path, QString* error) {
    close();
    file_.setFileName(path);
    if (!file_.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, file_.errorString());
        return false;
    }
    fileSize_ = static_cast<std::uint64_t>(file_.size());
    return readHeader(error) && readFat(error) && readMiniFat(error) && readDirectory(error) && checkTree(error);
}

bool Reader::readHeader(QString* error) {
    const QByteArray h = file_.read(kHeaderSize);
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

bool Reader::readSector(std::uint32_t sector, char* data, QString* error) {
    const std::uint64_t at = (static_cast<std::uint64_t>(sector) + 1) * sectorSize_;
    if (!isRegular(sector) || at >= fileSize_) {
        if (error) *error = QStringLiteral("%1: sector %2 is past the end of the file").arg(file_.fileName()).arg(sector);
        return false;
    }
    if (!file_.seek(static_cast<qint64>(at))) {
        if (error) *error = QStringLiteral("%1: %2").arg(file_.fileName(), file_.errorString());
        return false;
    }
    // The last sector may be cut short: what is missing reads as zeros.
    const qint64 got = file_.read(data, sectorSize_);
    if (got < 0) {
        if (error) *error = QStringLiteral("%1: %2").arg(file_.fileName(), file_.errorString());
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
            if (error) *error = QStringLiteral("%1: a sector chain from %2 is broken or loops").arg(file_.fileName()).arg(start);
            return false;
        }
        if (out.size() < limit) out.push_back(s);
        ++steps;
        s = table[s];
    }
    return true;
}

bool Reader::readChain(const std::vector<std::uint32_t>& sectors, QByteArray& out, QString* error) {
    out.resize(static_cast<qsizetype>(sectors.size() * sectorSize_));
    for (std::size_t i = 0; i < sectors.size(); ++i) {
        if (!readSector(sectors[i], out.data() + i * sectorSize_, error)) return false;
    }
    return true;
}

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

int Reader::child(int storage, const QString& name) const {
    for (const int id : children(storage)) {
        if (compareNames(entries_[static_cast<std::size_t>(id)].name, name) == 0) return id;
    }
    return -1;
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

const std::vector<std::uint32_t>* Reader::streamChain(int stream, QString* error) {
    auto it = chains_.find(stream);
    if (it != chains_.end()) return &it.value();
    const Entry& e = entries_[static_cast<std::size_t>(stream)];
    const bool mini = isMini(stream);
    const std::uint64_t unit = mini ? kMiniSector : sectorSize_;
    const std::uint64_t needed = (e.size + unit - 1) / unit;
    std::vector<std::uint32_t> sectors;
    if (needed > 0) {
        if (!chain(mini ? miniFat_ : fat_, e.start, sectors, needed, error)) return nullptr;
        if (sectors.size() < needed) {
            if (error) *error = QStringLiteral("%1: stream %2 is shorter than its size").arg(file_.fileName(), path(stream));
            return nullptr;
        }
    }
    return &chains_.insert(stream, std::move(sectors)).value();
}

QByteArray Reader::read(int stream, std::uint64_t offset, std::uint64_t length, QString* error) {
    if (stream < 0 || static_cast<std::size_t>(stream) >= entries_.size() || !entries_[static_cast<std::size_t>(stream)].isStream()) {
        if (error) *error = QStringLiteral("%1: no such stream").arg(file_.fileName());
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
            const std::uint64_t inMini = static_cast<std::uint64_t>((*sectors)[index]) * kMiniSector + inner;
            const std::uint64_t regular = inMini / sectorSize_;
            if (regular >= miniStream_.size()) {
                if (error) *error = QStringLiteral("%1: stream %2 lies past the mini stream").arg(file_.fileName(), path(stream));
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
        if (at + count > fileSize_ || !file_.seek(static_cast<qint64>(at)) ||
            file_.read(to, static_cast<qint64>(count)) != static_cast<qint64>(count)) {
            if (error) *error = QStringLiteral("%1: stream %2 is cut short").arg(file_.fileName(), path(stream));
            return {};
        }
        to += count;
        pos += count;
    }
    return out;
}

// ---- Writer -----------------------------------------------------------------

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
    n.entry.size = static_cast<std::uint64_t>(data.size());
}

QByteArray Writer::toBytes(QString* error) const {
    const std::uint32_t ss = sectorSize_;
    const std::uint32_t perSector = ss / 4;
    // For files past 2 GB with 4096-byte sectors, the sector holding byte
    // 0x7FFFFF00 is the range lock sector: kept out of every chain.
    const std::uint32_t rangeLock = ss == 4096 ? 0x7FFFFF00u / ss - 1 : kMaxRegular;
    std::vector<std::uint32_t> fat;
    QByteArray out(static_cast<qsizetype>(ss), '\0'); // the header, filled in last

    auto nextSector = [&]() {
        auto s = static_cast<std::uint32_t>(fat.size());
        if (s == rangeLock) {
            fat.push_back(kEndOfChain);
            out.append(QByteArray(static_cast<qsizetype>(ss), '\0'));
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
            QByteArray sector = data.mid(at, ss);
            sector.append(QByteArray(static_cast<qsizetype>(ss) - sector.size(), '\0'));
            out.append(sector);
        }
        return first;
    };

    std::vector<Entry> entries(nodes_.size());
    for (std::size_t i = 0; i < nodes_.size(); ++i) entries[i] = nodes_[i].entry;

    // Large streams in sectors of their own, in the order they were added.
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        Entry& e = entries[i];
        if (!e.isStream()) continue;
        if (sectorSize_ == 512 && e.size > 0xFFFFFFFFu) {
            if (error) *error = QStringLiteral("%1: a stream larger than 4 GB needs 4096-byte sectors").arg(e.name);
            return {};
        }
        if (e.size >= kMiniCutoff) e.start = writeChain(nodes_[i].data);
    }
    // Small streams in the mini stream, 64 bytes at a time.
    QByteArray mini;
    std::vector<std::uint32_t> miniFat;
    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        Entry& e = entries[i];
        if (!e.isStream() || e.size >= kMiniCutoff) continue;
        e.start = kEndOfChain;
        const QByteArray& data = nodes_[i].data;
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
    out.append(fatBytes);
    for (std::uint32_t d = 0; d < difatCount; ++d) {
        QByteArray sector(static_cast<qsizetype>(ss), '\0');
        for (std::uint32_t k = 0; k + 1 < perSector; ++k) {
            const std::size_t f = kHeaderDifat + static_cast<std::size_t>(d) * (perSector - 1) + k;
            put32(sector, 4 * static_cast<qsizetype>(k), f < fatSectors.size() ? fatSectors[f] : kFreeSect);
        }
        put32(sector, 4 * static_cast<qsizetype>(perSector - 1), d + 1 < difatCount ? difatSectors[d + 1] : kEndOfChain);
        out.append(sector);
    }

    // The header ([MS-CFB] 2.2).
    QByteArray& h = out;
    std::memcpy(h.data(), kSignature.data(), kSignature.size());
    std::memcpy(h.data() + 8, headerClsid_.data(), headerClsid_.size());
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
    return out;
}

bool Writer::save(const QString& path, QString* error) const {
    const QByteArray bytes = toBytes(error);
    if (bytes.isEmpty()) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace lumenis::interop::cfb
