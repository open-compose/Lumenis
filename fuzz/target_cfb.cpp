// The compound file fuzz target (fuzz_targets.h): cfb::Reader on arbitrary bytes. Beyond not
// crashing, what it reads must agree with itself: a stream read whole is its size, a piece read at
// an offset is that piece of the whole, every child is found by its name, and the tree written
// again with cfb::Writer reads back with the same paths and streams.
#include "fuzz_targets.h"

#include "lumenis/interop/cfb.h"

#include <QHash>

#include <algorithm>
#include <functional>

namespace lumenis::fuzz {

namespace cfb = interop::cfb;

namespace {

// Copies the tree below `from` (an entry of `r`) under `to` (a node of `w`); false when a name is
// one the Writer refuses (invalid, or the same as a sibling's but for case), which no round trip
// can keep.
bool copyTree(cfb::Reader& r, int from, cfb::Writer& w, int to, QHash<QString, QByteArray>& streams) {
    for (const int id : r.children(from)) {
        const cfb::Entry& e = r.entry(id);
        if (e.isStorage()) {
            const int s = w.addStorage(to, e.name);
            if (s < 0) return false;
            w.setClsid(s, e.clsid);
            w.setStateBits(s, e.stateBits);
            w.setTimes(s, e.created, e.modified);
            if (!copyTree(r, id, w, s, streams)) return false;
        } else {
            QString error;
            const QByteArray data = r.readAll(id, &error);
            if (!error.isEmpty()) return false; // a damaged stream: nothing to compare with
            if (w.addStream(to, e.name, data) < 0) return false;
            streams.insert(r.path(id), data);
        }
    }
    return true;
}

} // namespace

int fuzzCfb(const std::uint8_t* data, std::size_t size) {
    cfb::Reader r;
    if (!r.openData(view(data, size))) return 0;
    std::uint64_t total = 0;
    for (int i = 0; i < r.entryCount(); ++i) {
        const cfb::Entry& e = r.entry(i);
        (void)r.path(i); // entries outside the tree too
        if (e.isStorage()) {
            for (const int kid : r.children(i)) {
                fuzzCheck(r.child(i, r.entry(kid).name) >= 0, "cfb", QStringLiteral("child %1 of %2 not found by name").arg(kid).arg(i));
            }
            continue;
        }
        if (!e.isStream()) continue;
        QString error;
        const QByteArray all = r.readAll(i, &error);
        if (!error.isEmpty()) continue;
        fuzzCheck(static_cast<std::uint64_t>(all.size()) == e.size, "cfb",
                  QStringLiteral("stream %1 read %2 bytes of %3").arg(i).arg(all.size()).arg(e.size));
        total += e.size;
        // A piece from the middle, across sector boundaries.
        const std::uint64_t offset = e.size / 3;
        const std::uint64_t length = std::min<std::uint64_t>(e.size - offset, 700);
        const QByteArray piece = r.read(i, offset, length, &error);
        fuzzCheck(piece == all.mid(static_cast<qsizetype>(offset), static_cast<qsizetype>(length)), "cfb",
                  QStringLiteral("stream %1: a piece at %2 is not that piece of the whole").arg(i).arg(offset));
    }
    const QStringList paths = r.paths();

    // The round trip, for files of a reasonable size (the target must stay quick).
    if (total > (8u << 20)) return 0;
    cfb::Writer w(r.sectorSize());
    w.setHeaderClsid(r.headerClsid());
    w.setClsid(cfb::Writer::root(), r.entry(cfb::Reader::root()).clsid);
    QHash<QString, QByteArray> streams;
    if (!copyTree(r, cfb::Reader::root(), w, cfb::Writer::root(), streams)) return 0;
    QString error;
    const QByteArray bytes = w.toBytes(&error);
    fuzzCheck(!bytes.isEmpty(), "cfb", QStringLiteral("the tree read cannot be written: %1").arg(error));
    cfb::Reader back;
    fuzzCheck(back.openData(bytes, QStringLiteral("<written>"), &error), "cfb",
              QStringLiteral("what the Writer wrote does not read back: %1").arg(error));
    QStringList a = paths;
    QStringList b = back.paths();
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    fuzzCheck(a == b, "cfb", QStringLiteral("the paths written are not the paths read"));
    for (auto it = streams.cbegin(); it != streams.cend(); ++it) {
        const int id = back.find(it.key());
        fuzzCheck(id >= 0 && back.readAll(id) == it.value(), "cfb", QStringLiteral("stream %1 does not read back").arg(it.key()));
    }
    return 0;
}

} // namespace lumenis::fuzz
