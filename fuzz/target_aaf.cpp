// The AAF fuzz target (fuzz_targets.h): aaf::File on arbitrary bytes, then everything that reads an
// open file. Every storage of the compound file is taken as an object (whether or not anything
// refers to it) and each of its properties is decoded, followed and resolved; the Header is dumped
// as `cutthulhu --dump-aaf` dumps it; and the mobs are imported into bin items as File > Import does.
#include "fuzz_targets.h"

#include "lumenis/interop/aaf.h"
#include "lumenis/interop/aaf_import.h"

#include <QDateTime>
#include <QTimeZone>

#include <stdexcept>

namespace lumenis::fuzz {

namespace aaf = interop::aaf;

int fuzzAaf(const std::uint8_t* data, std::size_t size) {
    aaf::File file;
    if (!file.openData(view(data, size))) return 0;

    const interop::cfb::Reader& container = file.container();
    for (int i = 0; i < container.entryCount(); ++i) {
        if (!container.entry(i).isStorage()) continue;
        const aaf::Object o(&file, i);
        (void)o.path();
        (void)o.className();
        (void)o.isA(QStringLiteral("Mob"));
        for (const aaf::RawProperty& p : o.properties()) {
            (void)o.propertyName(p.pid);
            (void)o.value(p); // decodes data, follows strong references, resolves weak ones
            (void)o.weakKeys(p);
            (void)o.streamEntry(p);
        }
    }
    // The dump, without writing it as text: canonicalJson only formats JSON, which no input can
    // break, and on files this size it took a sixth of the target's time.
    (void)aaf::dump(file);

    aaf::ImportOptions options;
    options.binId = QStringLiteral("fuzz");
    options.when = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::utc()); // the same every run
    try {
        (void)aaf::importMobs(file, options);
    } catch (const std::overflow_error&) {
        // Documented: rates or lengths past what Rational holds (importFile reports it as an error).
    }
    return 0;
}

} // namespace lumenis::fuzz
