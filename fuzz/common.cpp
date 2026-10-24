// What the fuzz targets share (fuzz_targets.h): the table of targets by name, the check that aborts,
// and the Qt message filter.
#include "fuzz_targets.h"

#include <QtGlobal>

#include <cstdio>
#include <cstdlib>

namespace lumenis::fuzz {

const std::vector<Target>& targets() {
    static const std::vector<Target> all{
        {"cfb", fuzzCfb},
        {"aaf", fuzzAaf},
        {"avb", fuzzAvb},
        {"avb_rewrite", fuzzAvbRewrite},
        {"otio", fuzzOtio},
    };
    return all;
}

void fuzzCheck(bool ok, const char* target, const QString& what) {
    if (ok) return;
    std::fprintf(stderr, "fuzz_%s: check failed: %s\n", target, qPrintable(what));
    std::fflush(stderr);
    std::abort();
}

void quietQt() {
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
        if (type != QtFatalMsg) return;
        std::fprintf(stderr, "%s\n", qPrintable(message));
        std::abort();
    });
}

} // namespace lumenis::fuzz
