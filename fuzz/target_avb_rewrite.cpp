// The bin writer's fuzz target (fuzz_targets.h): whatever avb::File reads is written back with
// avb::rewrite (`cutthulhu --rewrite-avb`). Writing may refuse an object (an error, not a crash), but
// what it writes must be a bin: it loads, with the same objects of the same classes and root, and it
// rewrites to the same bytes, since the writer's layouts are the reader's inverse. When every object
// was read, the bin is also written in the other byte order, read, and written back in the first,
// which must give the same bytes again.
#include "fuzz_targets.h"

#include "lumenis/interop/avb.h"
#include "lumenis/interop/avb_writer.h"

namespace lumenis::fuzz {

namespace avb = interop::avb;

namespace {

// Checks that `bytes`, written from `from`, load as the same table of objects; returns how many
// objects of it could be read.
std::uint32_t checkReadBack(const avb::File& from, const QByteArray& bytes, avb::File& back, const char* what) {
    QString error;
    fuzzCheck(back.load(bytes, &error), "avb_rewrite", QStringLiteral("%1 does not load: %2").arg(QLatin1String(what), error));
    fuzzCheck(back.objectCount() == from.objectCount() && back.rootIndex() == from.rootIndex(), "avb_rewrite",
              QStringLiteral("%1 has %2 objects, root %3; the bin read has %4, root %5")
                  .arg(QLatin1String(what)).arg(back.objectCount()).arg(back.rootIndex()).arg(from.objectCount()).arg(from.rootIndex()));
    std::uint32_t read = 0;
    for (std::uint32_t i = 1; i <= from.objectCount(); ++i) {
        fuzzCheck(back.classIdAt(i) == from.classIdAt(i), "avb_rewrite",
                  QStringLiteral("%1: object %2 is %3, was %4").arg(QLatin1String(what)).arg(i).arg(back.classIdAt(i), from.classIdAt(i)));
        if (!from.object(i).ok()) continue;
        fuzzCheck(back.object(i).ok(), "avb_rewrite",
                  QStringLiteral("%1: object %2 (%3) was read but does not read back: %4")
                      .arg(QLatin1String(what)).arg(i).arg(from.classIdAt(i), back.object(i).error));
        ++read;
    }
    return read;
}

} // namespace

int fuzzAvbRewrite(const std::uint8_t* data, std::size_t size) {
    avb::File file;
    if (!file.load(view(data, size))) return 0;
    std::uint32_t read = 0;
    for (std::uint32_t i = 1; i <= file.objectCount(); ++i) read += file.object(i).ok() ? 1u : 0u;

    QString error;
    const QByteArray bytes = avb::rewrite(file, &error);
    if (bytes.isEmpty()) return 0; // an object the writer refuses: reported, not written
    avb::File back;
    checkReadBack(file, bytes, back, "the rewrite");
    fuzzCheck(avb::rewrite(back, &error) == bytes, "avb_rewrite", QStringLiteral("the rewrite does not rewrite to itself"));

    if (read != file.objectCount()) return 0; // unread objects are copied as stored: one byte order only
    const QByteArray other = avb::rewrite(file, &error, !file.littleEndian());
    if (other.isEmpty()) return 0;
    avb::File swapped;
    checkReadBack(file, other, swapped, "the rewrite in the other byte order");
    fuzzCheck(avb::rewrite(swapped, &error, file.littleEndian()) == bytes, "avb_rewrite",
              QStringLiteral("the rewrite in the other byte order does not rewrite to the first"));
    return 0;
}

} // namespace lumenis::fuzz
