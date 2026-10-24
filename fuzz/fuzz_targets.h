#pragma once

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

// The fuzz targets for the interchange parsers (ROADMAP Phase 3 gate 4, fuzz/README.md): each takes
// one input, a whole file in memory, and runs a parser and everything built on it over the bytes. A
// target returns normally for any input, valid or not; a crash, a sanitizer report, a hang or a
// failed check (fuzzCheck, which aborts) is a bug. They are linked two ways (fuzz/CMakeLists.txt):
// into libFuzzer executables (libfuzzer_main.cpp, Clang only, the `fuzz` preset), and into the
// stand-alone driver lumenis_fuzz_replay (replay_main.cpp), which runs saved inputs through them on
// every platform (ctest fuzz_replay).
namespace lumenis::fuzz {

// The compound file container: cfb::Reader opens the bytes, every stream is read whole and in part,
// and the tree is written back with cfb::Writer and read again (target_cfb.cpp).
int fuzzCfb(const std::uint8_t* data, std::size_t size);
// AAF: aaf::File opens the bytes, every storage is read as an object with every property decoded,
// the Header is dumped, and the mobs are imported into bin items (target_aaf.cpp).
int fuzzAaf(const std::uint8_t* data, std::size_t size);
// Media Composer bins: avb::File loads the bytes, every object is parsed, the root is dumped and
// imported into bin items (target_avb.cpp).
int fuzzAvb(const std::uint8_t* data, std::size_t size);
// The bin writer: what avb::File reads is written back with avb::rewrite, which must read back with
// the same objects and rewrite to the same bytes, in either byte order (target_avb_rewrite.cpp).
int fuzzAvbRewrite(const std::uint8_t* data, std::size_t size);
// OpenTimelineIO: otio::importJson reads the bytes, and each sequence it makes is exported and must
// import again (target_otio.cpp).
int fuzzOtio(const std::uint8_t* data, std::size_t size);

// A target by its name ("cfb", "aaf", "avb", "avb_rewrite", "otio"), as the directories in
// fuzz/regressions and the executables fuzz_<name> are named.
struct Target {
    const char* name;
    int (*run)(const std::uint8_t* data, std::size_t size);
};
const std::vector<Target>& targets();

// Small inputs made by our own writers, one set per target: seeds for the fuzzer that need no
// download (scripts/fuzz.py seed writes them with lumenis_fuzz_replay --make-seeds), and inputs
// for ctest fuzz_replay (seeds.cpp).
struct Seed {
    QString name; // a file name, "tiny_v3.cfb"
    QByteArray data;
};
std::vector<Seed> seeds(const QString& target);
// An AAF file made smaller for the fuzzer: laid out again in 512-byte sectors (named
// <name>.v3.aaf), and that without its metadictionary (<name>.v3_nometa.aaf), which the reader does
// not need. scripts/fuzz.py seed makes these from the interchange corpus. Empty when the file does
// not read as AAF.
std::vector<Seed> compactAaf(const QString& name, const QByteArray& aaf);

// The input's bytes as a QByteArray without a copy, so that AddressSanitizer catches a read one
// byte past the end (a copy would have Qt's terminating 0 there). Valid while `data` is.
inline QByteArray view(const std::uint8_t* data, std::size_t size) {
    return QByteArray::fromRawData(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
}

// Checks what must hold for any input (a round trip reads back): when it does not, prints `what` and
// aborts, which both libFuzzer and the replay driver report as a crash with the input that caused it.
void fuzzCheck(bool ok, const char* target, const QString& what);

// Silences Qt's debug and warning messages (damaged input makes many), keeping fatal ones.
void quietQt();

} // namespace lumenis::fuzz
