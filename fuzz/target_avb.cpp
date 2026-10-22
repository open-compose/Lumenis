// The bin fuzz target (fuzz_targets.h): avb::File on arbitrary bytes. Every object of the table is
// parsed and its payload copied, the root is dumped as `cutthulhu --dump-avb` dumps it, and a root
// that is a bin is imported into bin items as File > Open Bin does.
#include "fuzz_targets.h"

#include "lumenis/interop/aaf.h"
#include "lumenis/interop/avb.h"
#include "lumenis/interop/avb_import.h"

#include <QDateTime>
#include <QTimeZone>

#include <stdexcept>

namespace lumenis::fuzz {

namespace avb = interop::avb;

int fuzzAvb(const std::uint8_t* data, std::size_t size) {
    avb::File file;
    if (!file.load(view(data, size))) return 0;
    (void)file.header();
    for (std::uint32_t i = 0; i <= file.objectCount() + 1; ++i) { // 0 and one past the end are invalid
        (void)file.classIdAt(i);
        (void)file.object(i);
        (void)file.payload(i);
    }
    (void)interop::aaf::canonicalJson(avb::dump(file));

    const avb::Object& root = file.root();
    if (root.classId != QLatin1String("ABIN") || !root.ok()) return 0;
    avb::ImportOptions options;
    options.binId = QStringLiteral("fuzz");
    options.when = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::utc());
    try {
        (void)avb::importBin(file, options);
    } catch (const std::overflow_error&) {
        // Documented: rates or lengths past what Rational holds (importFile reports it as an error).
    }
    return 0;
}

} // namespace lumenis::fuzz
