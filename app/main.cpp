#include "crash_report.h"
#include "module_paths.h"

#include "lumenis/core/branding.h"
#include "lumenis/ext/module_loader.h"
#include "lumenis/ext/provider.h"
#include "lumenis/interop/aaf.h"
#include "lumenis/interop/ale.h"
#include "lumenis/interop/aaf_export.h"
#include "lumenis/interop/aaf_import.h"
#include "lumenis/interop/avb.h"
#include "lumenis/interop/avb_export.h"
#include "lumenis/interop/avb_import.h"
#include "lumenis/interop/avb_writer.h"
#include "lumenis/interop/edl.h"
#include "lumenis/interop/fcp_xml.h"
#include "lumenis/interop/otio.h"
#include "lumenis/model/structure.h"
#include "lumenis/model/locations.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/capture.h"
#include "lumenis/media/clap_host.h"
#include "lumenis/media/ofx_host.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/command_palette.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/select_project_dialog.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/soak.h"
#include "lumenis/ui/translation.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/source_browser.h"
#include "lumenis/ui/menu_builder.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/platform_check.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"
#include "lumenis/ui/workspace.h"

#include <QApplication>
#include <tuple>
#include <QIcon>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimeZone>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QMenuBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <optional>
#include <QToolButton>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
#include <random>
#include <numeric>
#include <algorithm>
#include <string_view>

#if defined(LUMENIS_WIN32_ATTACH_CONSOLE)
// No min and max macros: they break std::min and std::max.
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

// The cutthulhu executable: main() sets up Qt, the provider registry and modules, then either runs
// one of the command-line tools below and exits, or opens the editor (Select Project, then
// lumenis::ui::MainWindow). The options are declared in main() and handled in this order:
// --list-providers, --probe, --dump-structure, --export-aaf, --export-avb, --export-edl, --export-ale,
// --export-otio, --export-fcpxml, --import-aaf, --import-avb, --import-edl, --import-ale, --import-otio,
// --import-fcpxml,
// --dump-aaf, --dump-avb, --rewrite-avb, --write-media and --framemd5 need no display (isHeadlessInvocation()); --dump-menus,
// --measure-playback, --screenshot and --smoke-test open the main window; --project or Select
// Project start a normal session. The scripts that run these are in scripts/ (decode-corpus.py,
// check-mxf.py, measure-playback.py, structdiff.py, aaf-dump.py, make-checklist.py,
// ci-screenshots.py); BUILDING.md describes them. Module search paths are in module_paths.cpp and
// the Windows crash report in crash_report.cpp.

namespace {

// Options that only print and exit; they must work without a display.
bool isHeadlessInvocation(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--version" || arg == "-v" || arg == "--help" || arg == "-h" || arg == "-?"
            || arg == "--list-providers" || arg == "--probe" || arg == "--framemd5" || arg == "--write-media"
            || arg == "--dump-structure" || arg == "--dump-aaf" || arg == "--dump-avb" || arg == "--import-aaf" || arg == "--import-avb"
            || arg == "--export-aaf" || arg == "--export-avb" || arg == "--rewrite-avb" || arg == "--import-edl"
            || arg == "--export-edl" || arg == "--import-ale" || arg == "--export-ale" || arg == "--import-otio"
            || arg == "--export-otio" || arg == "--import-fcpxml" || arg == "--export-fcpxml") {
            return true;
        }
    }
    return false;
}

// A frame rate given on the command line: "25", "29.97" (NTSC rates written short) or "24000/1001".
std::optional<lumenis::Rational> parseRate(const QString& text) {
    const qsizetype slash = text.indexOf(QLatin1Char('/'));
    if (slash > 0) {
        bool okNum = false;
        bool okDen = false;
        const qlonglong num = text.left(slash).toLongLong(&okNum);
        const qlonglong den = text.mid(slash + 1).toLongLong(&okDen);
        if (okNum && okDen && num > 0 && den > 0) return lumenis::Rational(num, den);
        return std::nullopt;
    }
    return lumenis::interop::ale::rateFromFps(text);
}

void attachParentConsole() {
#if defined(LUMENIS_WIN32_ATTACH_CONSOLE)
    // A GUI-subsystem program has no console; borrow the terminal it was
    // started from so command-line output is visible. Leave stdout alone when
    // it is already redirected to a pipe or file (CTest, scripts).
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
#endif
}

// --list-providers: each module found (loaded, or why not) and each provider with its category,
// licence tier and whether it is available here.
void printProviders(const lumenis::ext::ProviderRegistry& registry,
                    const lumenis::ext::ModuleLoader& loader) {
    std::printf("Modules:\n");
    if (loader.records().empty()) {
        std::printf("  (none)\n");
    }
    for (const auto& m : loader.records()) {
        if (m.loaded) {
            std::printf("  %s %s [%s] %s\n", m.id.c_str(), m.version.c_str(), m.license.c_str(),
                        m.path.string().c_str());
        } else {
            std::printf("  FAILED %s: %s\n", m.path.string().c_str(), m.error.c_str());
        }
    }
    std::printf("Providers:\n");
    if (registry.all().empty()) {
        std::printf("  (none)\n");
    }
    for (const auto& p : registry.all()) {
        const auto info = p->info();
        const auto availability = p->probe();
        std::printf("  %-40s %-20.*s %-17.*s %s%s\n", info.id.c_str(),
                    static_cast<int>(toString(info.category).size()), toString(info.category).data(),
                    static_cast<int>(toString(info.tier).size()), toString(info.tier).data(),
                    availability.available ? "available" : "unavailable: ",
                    availability.reason.c_str());
    }
}

} // namespace

// Order matters: the console and crash report first, the Wayland fallback before the
// QApplication exists (it reads QT_QPA_PLATFORM), the registry before any command-line tool.
int main(int argc, char** argv) {
    attachParentConsole();
    installCrashReport();

    const bool headless = isHeadlessInvocation(argc, argv);
    if (!headless) {
        // Incomplete Wayland support (BUILDING.md): use X11 quietly, with one
        // line saying what to install, rather than Qt's page of warnings.
        QString why;
        const QString platform = lumenis::ui::fallbackPlatformForThisSystem(&why);
        if (!platform.isEmpty()) {
            std::fprintf(stderr, "%s: %s. Running through X11 (XWayland) meanwhile.\n",
                         lumenis::branding::kExecutableName, qPrintable(why));
            qputenv("QT_QPA_PLATFORM", platform.toLatin1());
        } else if (!why.isEmpty()) {
            std::fprintf(stderr, "%s: %s. There is no X11 (DISPLAY is not set) to use instead.\n",
                         lumenis::branding::kExecutableName, qPrintable(why));
        }
    }
    // A QCoreApplication for the tools that only print, so they run without a display (CI, scripts).
    std::unique_ptr<QCoreApplication> app = headless
        ? std::make_unique<QCoreApplication>(argc, argv)
        : std::make_unique<QApplication>(argc, argv);

    QCoreApplication::setApplicationName(QString::fromUtf8(lumenis::branding::kExecutableName));
    QCoreApplication::setOrganizationName(QString::fromUtf8(lumenis::branding::kOrganization));
    QCoreApplication::setOrganizationDomain(QString::fromUtf8(lumenis::branding::kDomain));
    QCoreApplication::setApplicationVersion(QString::fromUtf8(lumenis::branding::kVersion));
    if (!headless) {
        QGuiApplication::setApplicationDisplayName(QString::fromUtf8(lumenis::branding::kProductName));
    }

    // The options. --help and --version are handled (and exit) in parser.process().
    QCommandLineParser parser;
    parser.setApplicationDescription(QString::fromUtf8(lumenis::branding::kDescription));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption listProviders(
        QStringLiteral("list-providers"), QStringLiteral("List loaded modules and extension providers, then exit."));
    const QCommandLineOption smokeTest(
        QStringLiteral("smoke-test"), QStringLiteral("Open the main window, verify it, and exit (used by CI)."));
    const QCommandLineOption dumpMenus(
        QStringLiteral("dump-menus"), QStringLiteral("Write the menu bar as JSON to <file> ('-' for stdout), then exit."),
        QStringLiteral("file"));
    const QCommandLineOption screenshot(
        QStringLiteral("screenshot"), QStringLiteral("Save a PNG of each window into <dir>, then exit."),
        QStringLiteral("dir"));
    const QCommandLineOption openProject(
        QStringLiteral("project"), QStringLiteral("Open the project file <file> instead of showing Select Project."),
        QStringLiteral("file"));
    const QCommandLineOption defaultLayout(
        QStringLiteral("default-layout"), QStringLiteral("Ignore the saved window layout and keyboard settings."));
    const QCommandLineOption probeMedia(
        QStringLiteral("probe"), QStringLiteral("Print what the media file <file> holds as JSON, then exit."),
        QStringLiteral("file"));
    const QCommandLineOption frameMd5(
        QStringLiteral("framemd5"),
        QStringLiteral("Print the MD5 of each decoded video frame of <file> (or an image sequence spec), then exit."),
        QStringLiteral("file"));
    const QCommandLineOption shuffle(
        QStringLiteral("shuffle"), QStringLiteral("With --framemd5: read the frames in a shuffled order, seeking."));
    const QCommandLineOption writeMedia(
        QStringLiteral("write-media"),
        QStringLiteral("Transcode <file> into managed MXF media under --drive (Avid MediaFiles/MXF/<n>/), "
                       "in each resolution of --resolution, then print the files as JSON and exit."),
        QStringLiteral("file"));
    const QCommandLineOption mediaDrive(
        QStringLiteral("drive"), QStringLiteral("With --write-media: the drive (folder) for the media."), QStringLiteral("folder"));
    const QCommandLineOption mediaResolution(
        QStringLiteral("resolution"),
        QStringLiteral("With --write-media: a resolution id (dnx_lb, dnx_sq, ...) or 'all' (default)."),
        QStringLiteral("id"), QStringLiteral("all"));
    const QCommandLineOption dumpStructure(
        QStringLiteral("dump-structure"),
        QStringLiteral("Print the structure of a bin (.cthbin) or project (.cthproj or its folder) as JSON, "
                       "for scripts/structdiff.py, then exit."),
        QStringLiteral("path"));
    parser.addOption(dumpStructure);
    const QCommandLineOption dumpAaf(
        QStringLiteral("dump-aaf"),
        QStringLiteral("Print every object of an AAF file below its Header, with every property, as JSON "
                       "(docs/spec/aaf.md), then exit. With --canonical, compact and sorted, as "
                       "scripts/aaf-dump.py prints it."),
        QStringLiteral("file"));
    const QCommandLineOption dumpAvb(
        QStringLiteral("dump-avb"),
        QStringLiteral("Print a Media Composer bin (.avb) from its root object, with every property and "
                       "every object it refers to, as JSON (docs/spec/avb.md), then exit. With --canonical, "
                       "compact and sorted, as scripts/avb-dump.py prints it."),
        QStringLiteral("file"));
    const QCommandLineOption canonical(QStringLiteral("canonical"),
                                       QStringLiteral("With --dump-aaf or --dump-avb: compact, sorted JSON."));
    parser.addOption(dumpAaf);
    parser.addOption(dumpAvb);
    const QCommandLineOption rewriteAvb(
        QStringLiteral("rewrite-avb"),
        QStringLiteral("Read a Media Composer bin (.avb) and write every object back, unchanged, to the file "
                       "given after it (docs/spec/avb.md, \"Writing\"), then exit: the same bytes for a bin "
                       "the reader reads in full."),
        QStringLiteral("file"));
    parser.addOption(rewriteAvb);
    const QCommandLineOption importAaf(
        QStringLiteral("import-aaf"),
        QStringLiteral("Import an AAF file into a bin and print the bin's structure dump "
                       "(docs/spec/structure.md), then exit."),
        QStringLiteral("file"));
    parser.addOption(importAaf);
    const QCommandLineOption importAvb(
        QStringLiteral("import-avb"),
        QStringLiteral("Read a Media Composer bin (.avb) into a bin and print the bin's structure dump "
                       "(docs/spec/structure.md), then exit."),
        QStringLiteral("file"));
    parser.addOption(importAvb);
    const QCommandLineOption exportAaf(
        QStringLiteral("export-aaf"),
        QStringLiteral("Export a bin (.cthbin) as AAF to the file --to names, then exit. An AAF file is "
                       "imported first: a round trip."),
        QStringLiteral("bin"));
    const QCommandLineOption exportTo(QStringLiteral("to"), QStringLiteral("With --export-aaf: the AAF file to write."),
                                      QStringLiteral("file"));
    parser.addOption(exportAaf);
    parser.addOption(exportTo);
    const QCommandLineOption exportAvb(
        QStringLiteral("export-avb"),
        QStringLiteral("Write a bin (.cthbin) as a Media Composer bin (.avb) to the file given after it "
                       "(docs/spec/avb.md, \"Export\"), then exit. Clips its sequences use from the project's "
                       "other bins (the .cthbin files beside it) go in as reference clips. A .avb is read "
                       "first: a round trip."),
        QStringLiteral("bin"));
    parser.addOption(exportAvb);
    const QCommandLineOption importEdl(
        QStringLiteral("import-edl"),
        QStringLiteral("Import an EDL (CMX 3600, File_129) into a bin: a source for each reel and the sequence. Print "
                       "the bin's structure dump (docs/spec/structure.md), then exit. --rate gives the frame rate."),
        QStringLiteral("file"));
    const QCommandLineOption edlRate(
        QStringLiteral("rate"),
        QStringLiteral("With --import-edl: the frame rate (25, 29.97, 24000/1001); guessed from the list when not given."),
        QStringLiteral("rate"));
    const QCommandLineOption exportEdl(
        QStringLiteral("export-edl"),
        QStringLiteral("Write the sequence named after the bin (.cthbin) as an EDL to the file given after the name, then "
                       "exit (docs/spec/edl.md). Clips come from the bin and the other bins beside it. An .edl is "
                       "imported first: a round trip (then the name may be left out). --edl-type picks the type."),
        QStringLiteral("bin"));
    const QCommandLineOption edlType(QStringLiteral("edl-type"),
                                     QStringLiteral("With --export-edl: CMX_3600 (default) or File_129."), QStringLiteral("type"),
                                     QStringLiteral("CMX_3600"));
    const QCommandLineOption importAle(
        QStringLiteral("import-ale"),
        QStringLiteral("Import a shot log (Avid Log Exchange, .ale) into a bin and print the bin's structure dump, then exit "
                       "(docs/spec/ale.md)."),
        QStringLiteral("file"));
    const QCommandLineOption exportAle(
        QStringLiteral("export-ale"),
        QStringLiteral("Write the master clips and subclips of a bin (.cthbin) as a shot log (.ale) to the file given "
                       "after it, then exit. An .ale is imported first: a round trip."),
        QStringLiteral("bin"));
    parser.addOption(importEdl);
    parser.addOption(edlRate);
    parser.addOption(exportEdl);
    parser.addOption(edlType);
    parser.addOption(importAle);
    parser.addOption(exportAle);
    const QCommandLineOption importOtio(
        QStringLiteral("import-otio"),
        QStringLiteral("Import an OpenTimelineIO file (.otio) into a bin: a master clip for each piece of media and a "
                       "sequence for each timeline. Print the bin's structure dump, then exit (docs/spec/otio.md)."),
        QStringLiteral("file"));
    const QCommandLineOption exportOtio(
        QStringLiteral("export-otio"),
        QStringLiteral("Write the sequence named after the bin (.cthbin) as an OpenTimelineIO file to the file given "
                       "after the name, then exit (docs/spec/otio.md). Clips come from the bin and the other bins "
                       "beside it. An .otio is imported first: a round trip (then the name may be left out)."),
        QStringLiteral("bin"));
    parser.addOption(importOtio);
    parser.addOption(exportOtio);
    const QCommandLineOption importFcpXml(
        QStringLiteral("import-fcpxml"),
        QStringLiteral("Import a Final Cut Pro 7 XML file (xmeml) into a bin, as --import-otio does. Print the bin's "
                       "structure dump, then exit (docs/spec/fcp-xml.md)."),
        QStringLiteral("file"));
    const QCommandLineOption exportFcpXml(
        QStringLiteral("export-fcpxml"),
        QStringLiteral("Write the sequence named after the bin (.cthbin) as Final Cut Pro 7 XML to the file given after "
                       "the name, then exit (docs/spec/fcp-xml.md). An FCP XML or .otio file is imported first."),
        QStringLiteral("bin"));
    parser.addOption(importFcpXml);
    parser.addOption(exportFcpXml);
    parser.addOption(canonical);
    parser.addOption(writeMedia);
    parser.addOption(mediaDrive);
    parser.addOption(mediaResolution);
    parser.addOption(listProviders);
    parser.addOption(probeMedia);
    parser.addOption(frameMd5);
    parser.addOption(shuffle);
    const QCommandLineOption measurePlayback(
        QStringLiteral("measure-playback"),
        QStringLiteral("Play the media file <file> in the Source monitor and print the frames shown and dropped and "
                       "the picture's offset from the sound as JSON, then exit."),
        QStringLiteral("file"));
    const QCommandLineOption playSeconds(
        QStringLiteral("seconds"), QStringLiteral("With --measure-playback: how long to play (default 20)."),
        QStringLiteral("seconds"), QStringLiteral("20"));
    parser.addOption(measurePlayback);
    parser.addOption(playSeconds);
    // The long-run stability check (ROADMAP 10.2; docs/spec/soak.md, scripts/soak-check.py).
    const QCommandLineOption soak(
        QStringLiteral("soak"),
        QStringLiteral("Loop playback, scrubbing, renders, edits and tool windows in a sample project for <hours> "
                       "(a fraction is fine), logging memory, handles, threads and dropped frames, then exit."),
        QStringLiteral("hours"));
    const QCommandLineOption soakInterval(
        QStringLiteral("soak-interval"), QStringLiteral("With --soak: seconds between log lines (default 60)."),
        QStringLiteral("seconds"), QStringLiteral("60"));
    const QCommandLineOption soakLog(
        QStringLiteral("soak-log"), QStringLiteral("With --soak: the log, JSON lines (default soak.jsonl)."),
        QStringLiteral("file"), QStringLiteral("soak.jsonl"));
    const QCommandLineOption soakSeed(
        QStringLiteral("soak-seed"), QStringLiteral("With --soak: the scrub positions' seed (default 1)."),
        QStringLiteral("number"), QStringLiteral("1"));
    const QCommandLineOption soakCycles(
        QStringLiteral("soak-cycles"), QStringLiteral("With --soak: also end after <n> cycles (default: only the hours)."),
        QStringLiteral("n"), QStringLiteral("0"));
    parser.addOption(soak);
    parser.addOption(soakCycles);
    parser.addOption(soakInterval);
    parser.addOption(soakLog);
    parser.addOption(soakSeed);
    parser.addOption(smokeTest);
    const QCommandLineOption sampleProject(
        QStringLiteral("sample-project"),
        QStringLiteral("With --dump-menus: open a new temporary project first, as an editor would."));
    // The interface's language (ROADMAP 10.4): a code from translations/languages.json, or "pseudo" (translation.h).
    const QCommandLineOption language(
        QStringLiteral("language"),
        QStringLiteral("Start in the language <code> (en, de, fr, it, ga, or pseudo for testing), whatever the Interface "
                       "setting says."),
        QStringLiteral("code"));
    parser.addOption(language);
    parser.addOption(dumpMenus);
    parser.addOption(sampleProject);
    parser.addOption(screenshot);
    parser.addOption(defaultLayout);
    parser.addOption(openProject);
    parser.process(*app);

    lumenis::ext::ProviderRegistry registry;
    // The built-in providers first, then modules (docs/EXTENSIONS.md).
    lumenis::media::registerProviders(registry);
    lumenis::ext::ModuleLoader loader(registry);
    loader.loadDirectories(lumenis::app::moduleSearchPaths());

    // --list-providers: modules, providers and the FFmpeg version.
    if (parser.isSet(listProviders)) {
        printProviders(registry, loader);
        std::printf("%s\n", qPrintable(lumenis::media::ffmpegVersion()));
        return 0;
    }
    // --probe <file>: MediaInfo::toJson() on stdout (media::probe(), src/media/media.cpp).
    if (parser.isSet(probeMedia)) {
        QString error;
        const auto info = lumenis::media::probe(parser.value(probeMedia), &error);
        if (!info) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(parser.value(probeMedia)), qPrintable(error));
            return 1;
        }
        const QByteArray json = QJsonDocument(info->toJson()).toJson();
        std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
        return 0;
    }

    if (parser.isSet(dumpStructure)) {
        // Phase 3 gate 1: what a bin or project holds, to compare
        // (docs/spec/structure.md).
        QString error;
        const QJsonObject structure = lumenis::model::structureOf(parser.value(dumpStructure), &error);
        if (structure.isEmpty()) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(parser.value(dumpStructure)), qPrintable(error));
            return 1;
        }
        const QByteArray json = QJsonDocument(structure).toJson();
        std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
        return 0;
    }

    // --export-aaf <bin> --to <file>: a .cthbin's items, or an AAF file's imported items (a round trip,
    // ctest aaf_round_trip), written as AAF by interop::aaf::exportItems() (docs/spec/aaf.md). Warnings
    // go to stderr; exit status 2 when --to is missing.
    if (parser.isSet(exportAaf)) {
        QString error;
        const QString from = parser.value(exportAaf);
        QList<lumenis::model::BinItem> items;
        if (from.endsWith(QLatin1String(".aaf"), Qt::CaseInsensitive)) {
            const auto imported = lumenis::interop::aaf::importFile(from, {}, &error);
            if (!imported) {
                std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(from), qPrintable(error));
                return 1;
            }
            items = imported->items;
        } else {
            const auto bin = lumenis::model::BinFile::read(from, &error);
            if (!bin) {
                std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(from), qPrintable(error));
                return 1;
            }
            items = bin->items;
        }
        if (!parser.isSet(exportTo)) {
            std::fprintf(stderr, "--export-aaf needs --to FILE\n");
            return 2;
        }
        const auto result = lumenis::interop::aaf::exportItems(items, parser.value(exportTo));
        for (const QString& w : result.warnings) std::fprintf(stderr, "export-aaf: %s\n", qPrintable(w));
        if (!result.ok) {
            std::fprintf(stderr, "cannot write %s: %s\n", qPrintable(parser.value(exportTo)), qPrintable(result.error));
            return 1;
        }
        return 0;
    }

    // --export-avb <bin> OUT: a .cthbin (with the other bins of its folder, the project's, to find the
    // clips its sequences use), or a Media Composer bin's import (a round trip, ctest
    // avb_export_round_trip), written by interop::avb::exportBin() (docs/spec/avb.md). Warnings go to
    // stderr; exit status 2 without OUT.
    if (parser.isSet(exportAvb)) {
        QString error;
        const QString from = parser.value(exportAvb);
        const QStringList rest = parser.positionalArguments();
        if (rest.size() != 1) {
            std::fprintf(stderr, "--export-avb needs the file to write after the bin to read\n");
            return 2;
        }
        lumenis::model::Bin bin;
        QList<lumenis::model::Bin> others;
        if (from.endsWith(QLatin1String(".avb"), Qt::CaseInsensitive)) {
            const auto imported = lumenis::interop::avb::importFile(from, {}, &error);
            if (!imported) {
                std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(from), qPrintable(error));
                return 1;
            }
            for (const QString& w : imported->warnings) std::fprintf(stderr, "import-avb: %s\n", qPrintable(w));
            bin.name = QFileInfo(from).completeBaseName();
            bin.items = imported->items;
            bin.display.customColumns = imported->customColumns;
        } else {
            const auto read = lumenis::model::BinFile::read(from, &error);
            if (!read) {
                std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(from), qPrintable(error));
                return 1;
            }
            bin = *read;
            const QFileInfo info(from);
            for (const QFileInfo& f : info.dir().entryInfoList({QStringLiteral("*") + lumenis::model::BinFile::suffix()}, QDir::Files)) {
                if (f.canonicalFilePath() == info.canonicalFilePath()) continue;
                if (auto other = lumenis::model::BinFile::read(f.filePath())) others << *other;
            }
        }
        lumenis::interop::avb::ExportOptions options;
        options.resolve = [&others](const lumenis::MobId& mob, const QString& clipId) -> std::optional<lumenis::model::BinItem> {
            for (const lumenis::model::Bin& b : others) {
                for (const lumenis::model::BinItem& item : b.items) {
                    if ((!clipId.isEmpty() && item.id == clipId) || lumenis::model::mobIdOf(item) == mob) return item;
                }
            }
            return std::nullopt;
        };
        const auto result = lumenis::interop::avb::exportBin(bin, rest.first(), options);
        for (const QString& w : result.warnings) std::fprintf(stderr, "export-avb: %s\n", qPrintable(w));
        if (!result.ok) {
            std::fprintf(stderr, "cannot write %s: %s\n", qPrintable(rest.first()), qPrintable(result.error));
            return 1;
        }
        return 0;
    }

    // --export-edl <bin> SEQUENCE OUT: a sequence of a .cthbin (its clips found in that bin and the other
    // bins of its folder, the project's) or of an EDL's import (a round trip), written by
    // interop::edl::exportFile() (docs/spec/edl.md). Warnings go to stderr; exit status 2 for wrong
    // arguments.
    if (parser.isSet(exportEdl)) {
        QString error;
        const QString from = parser.value(exportEdl);
        const QStringList rest = parser.positionalArguments();
        const auto type = lumenis::interop::edl::formatFromName(parser.value(edlType));
        if (!type) {
            std::fprintf(stderr, "--edl-type is CMX_3600 or File_129\n");
            return 2;
        }
        QList<lumenis::model::BinItem> items;
        const bool isEdl = from.endsWith(QLatin1String(".edl"), Qt::CaseInsensitive);
        if (isEdl) {
            lumenis::interop::edl::ImportOptions options;
            if (parser.isSet(edlRate)) options.rate = parseRate(parser.value(edlRate));
            const auto imported = lumenis::interop::edl::importFile(from, options, &error);
            if (!imported) {
                std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(from), qPrintable(error));
                return 1;
            }
            for (const QString& w : imported->warnings) std::fprintf(stderr, "import-edl: %s\n", qPrintable(w));
            items = imported->items;
        } else {
            const auto bin = lumenis::model::BinFile::read(from, &error);
            if (!bin) {
                std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(from), qPrintable(error));
                return 1;
            }
            items = bin->items;
            const QFileInfo info(from);
            for (const QFileInfo& f : info.dir().entryInfoList({QStringLiteral("*") + lumenis::model::BinFile::suffix()}, QDir::Files)) {
                if (f.canonicalFilePath() == info.canonicalFilePath()) continue;
                if (auto other = lumenis::model::BinFile::read(f.filePath())) items += other->items;
            }
        }
        if (rest.size() != 2 && !(isEdl && rest.size() == 1)) {
            std::fprintf(stderr, "--export-edl needs the sequence's name and the file to write after the bin\n");
            return 2;
        }
        const lumenis::model::BinItem* sequence = nullptr;
        for (const lumenis::model::BinItem& item : items) {
            if (item.kind != lumenis::model::BinItemKind::Sequence) continue;
            if (!sequence && (rest.size() == 1 || item.name == rest.first())) sequence = &item;
        }
        if (!sequence) {
            std::fprintf(stderr, "no sequence %s in %s\n", qPrintable(rest.size() == 2 ? rest.first() : QString()), qPrintable(from));
            return 1;
        }
        lumenis::interop::edl::ExportOptions options;
        options.format = *type;
        options.resolve = [&items](const lumenis::MobId& mob, const QString& clipId) -> std::optional<lumenis::model::BinItem> {
            for (const lumenis::model::BinItem& item : items) {
                if (item.kind == lumenis::model::BinItemKind::Sequence) continue;
                if ((!clipId.isEmpty() && item.id == clipId) || lumenis::model::mobIdOf(item) == mob) return item;
            }
            return std::nullopt;
        };
        const auto result = lumenis::interop::edl::exportFile(*sequence, rest.last(), options);
        for (const QString& w : result.warnings) std::fprintf(stderr, "export-edl: %s\n", qPrintable(w));
        if (!result.ok) {
            std::fprintf(stderr, "cannot write %s: %s\n", qPrintable(rest.last()), qPrintable(result.error));
            return 1;
        }
        return 0;
    }

    // --export-otio <bin> SEQUENCE OUT and --export-fcpxml (the same, written as FCP XML): a sequence
    // of a .cthbin (its clips found in that bin and the other bins of its folder), or of an .otio's or
    // an FCP XML file's import (a round trip, scripts/otio-check.py), written by
    // interop::otio::exportFile() or interop::fcpxml::exportFile() (docs/spec/otio.md,
    // docs/spec/fcp-xml.md). Warnings go to stderr; exit status 2 for wrong arguments.
    if (parser.isSet(exportOtio) || parser.isSet(exportFcpXml)) {
        QString error;
        const bool toFcp = parser.isSet(exportFcpXml);
        const char* option = toFcp ? "--export-fcpxml" : "--export-otio";
        const QString from = parser.value(toFcp ? exportFcpXml : exportOtio);
        const QStringList rest = parser.positionalArguments();
        QList<lumenis::model::BinItem> items;
        const bool fromFcp = lumenis::interop::fcpxml::isFcpXmlFile(from);
        const bool isOtio = fromFcp || from.endsWith(QLatin1String(".otio"), Qt::CaseInsensitive);
        if (isOtio) {
            const auto imported = fromFcp ? lumenis::interop::fcpxml::importFile(from, {}, &error)
                                          : lumenis::interop::otio::importFile(from, {}, &error);
            if (!imported) {
                std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(from), qPrintable(error));
                return 1;
            }
            for (const QString& w : imported->warnings) std::fprintf(stderr, "import: %s\n", qPrintable(w));
            items = imported->items;
        } else {
            const auto bin = lumenis::model::BinFile::read(from, &error);
            if (!bin) {
                std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(from), qPrintable(error));
                return 1;
            }
            items = bin->items;
            const QFileInfo info(from);
            for (const QFileInfo& f : info.dir().entryInfoList({QStringLiteral("*") + lumenis::model::BinFile::suffix()}, QDir::Files)) {
                if (f.canonicalFilePath() == info.canonicalFilePath()) continue;
                if (auto other = lumenis::model::BinFile::read(f.filePath())) items += other->items;
            }
        }
        if (rest.size() != 2 && !(isOtio && rest.size() == 1)) {
            std::fprintf(stderr, "%s needs the sequence's name and the file to write after the bin\n", option);
            return 2;
        }
        const lumenis::model::BinItem* sequence = nullptr;
        for (const lumenis::model::BinItem& item : items) {
            if (item.kind != lumenis::model::BinItemKind::Sequence) continue;
            if (!sequence && (rest.size() == 1 || item.name == rest.first())) sequence = &item;
        }
        if (!sequence) {
            std::fprintf(stderr, "no sequence %s in %s\n", qPrintable(rest.size() == 2 ? rest.first() : QString()), qPrintable(from));
            return 1;
        }
        lumenis::interop::otio::ExportOptions options;
        options.resolve = [&items](const lumenis::MobId& mob, const QString& clipId) -> std::optional<lumenis::model::BinItem> {
            for (const lumenis::model::BinItem& item : items) {
                if (item.kind == lumenis::model::BinItemKind::Sequence) continue;
                if ((!clipId.isEmpty() && item.id == clipId) || lumenis::model::mobIdOf(item) == mob) return item;
            }
            return std::nullopt;
        };
        const auto result = toFcp ? lumenis::interop::fcpxml::exportFile(*sequence, rest.last(), options)
                                  : lumenis::interop::otio::exportFile(*sequence, rest.last(), options);
        for (const QString& w : result.warnings) std::fprintf(stderr, "%s: %s\n", option + 2, qPrintable(w));
        if (!result.ok) {
            std::fprintf(stderr, "cannot write %s: %s\n", qPrintable(rest.last()), qPrintable(result.error));
            return 1;
        }
        return 0;
    }

    // --export-ale <bin> OUT: a .cthbin's master clips and subclips, or a shot log's import (a round
    // trip), written by interop::ale::exportFile() (docs/spec/ale.md).
    if (parser.isSet(exportAle)) {
        QString error;
        const QString from = parser.value(exportAle);
        const QStringList rest = parser.positionalArguments();
        if (rest.size() != 1) {
            std::fprintf(stderr, "--export-ale needs the file to write after the bin to read\n");
            return 2;
        }
        QList<lumenis::model::BinItem> items;
        lumenis::interop::ale::ExportOptions options;
        if (from.endsWith(QLatin1String(".ale"), Qt::CaseInsensitive)) {
            const auto imported = lumenis::interop::ale::importFile(from, {}, &error);
            if (!imported) {
                std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(from), qPrintable(error));
                return 1;
            }
            for (const QString& w : imported->warnings) std::fprintf(stderr, "import-ale: %s\n", qPrintable(w));
            items = imported->items;
        } else {
            const auto bin = lumenis::model::BinFile::read(from, &error);
            if (!bin) {
                std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(from), qPrintable(error));
                return 1;
            }
            items = bin->items;
        }
        const auto result = lumenis::interop::ale::exportFile(items, rest.first(), options);
        for (const QString& w : result.warnings) std::fprintf(stderr, "export-ale: %s\n", qPrintable(w));
        if (!result.ok) {
            std::fprintf(stderr, "cannot write %s: %s\n", qPrintable(rest.first()), qPrintable(result.error));
            return 1;
        }
        return 0;
    }

    // What an AAF file or a Media Composer bin imports as: the bin's structure (Phase 3 gate 1). Both
    // print the same form, so that either can be compared with a reference dump (structdiff.py).
    auto printImported = [](const QString& path, const QStringList& warnings, const char* option,
                            const QList<lumenis::model::BinItem>& items, const QStringList& customColumns) {
        for (const QString& w : warnings) std::fprintf(stderr, "%s: %s\n", option, qPrintable(w));
        lumenis::model::Bin bin;
        bin.name = QFileInfo(path).completeBaseName();
        bin.items = items;
        bin.display.customColumns = customColumns;
        const QJsonObject structure{{QStringLiteral("format"), QStringLiteral("cutthulhu-structure")},
                                    {QStringLiteral("version"), 1},
                                    {QStringLiteral("bins"), QJsonArray{lumenis::model::binStructure(bin)}}};
        const QByteArray json = QJsonDocument(structure).toJson();
        std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
    };
    if (parser.isSet(importAaf)) {
        QString error;
        const QString path = parser.value(importAaf);
        const auto imported = lumenis::interop::aaf::importFile(path, {}, &error);
        if (!imported) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(path), qPrintable(error));
            return 1;
        }
        printImported(path, imported->warnings, "import-aaf", imported->items, imported->customColumns);
        return 0;
    }
    if (parser.isSet(importEdl)) {
        QString error;
        const QString path = parser.value(importEdl);
        lumenis::interop::edl::ImportOptions options;
        if (parser.isSet(edlRate)) {
            options.rate = parseRate(parser.value(edlRate));
            if (!options.rate) {
                std::fprintf(stderr, "--rate %s is not a frame rate\n", qPrintable(parser.value(edlRate)));
                return 2;
            }
        }
        const auto imported = lumenis::interop::edl::importFile(path, options, &error);
        if (!imported) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(path), qPrintable(error));
            return 1;
        }
        printImported(path, imported->warnings, "import-edl", imported->items, {});
        return 0;
    }
    if (parser.isSet(importOtio)) {
        QString error;
        const QString path = parser.value(importOtio);
        const auto imported = lumenis::interop::otio::importFile(path, {}, &error);
        if (!imported) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(path), qPrintable(error));
            return 1;
        }
        printImported(path, imported->warnings, "import-otio", imported->items, {});
        return 0;
    }
    if (parser.isSet(importFcpXml)) {
        QString error;
        const QString path = parser.value(importFcpXml);
        const auto imported = lumenis::interop::fcpxml::importFile(path, {}, &error);
        if (!imported) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(path), qPrintable(error));
            return 1;
        }
        printImported(path, imported->warnings, "import-fcpxml", imported->items, {});
        return 0;
    }
    if (parser.isSet(importAle)) {
        QString error;
        const QString path = parser.value(importAle);
        const auto imported = lumenis::interop::ale::importFile(path, {}, &error);
        if (!imported) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(path), qPrintable(error));
            return 1;
        }
        printImported(path, imported->warnings, "import-ale", imported->items, imported->customColumns);
        return 0;
    }
    if (parser.isSet(importAvb)) {
        QString error;
        const QString path = parser.value(importAvb);
        const auto imported = lumenis::interop::avb::importFile(path, {}, &error);
        if (!imported) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(path), qPrintable(error));
            return 1;
        }
        printImported(path, imported->warnings, "import-avb", imported->items, imported->customColumns);
        return 0;
    }

    if (parser.isSet(dumpAaf)) {
        // What an AAF file holds, to compare with pyaaf2's reading of it.
        lumenis::interop::aaf::File file;
        QString error;
        if (!file.open(parser.value(dumpAaf), &error)) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(parser.value(dumpAaf)), qPrintable(error));
            return 1;
        }
        const QJsonValue dump = lumenis::interop::aaf::dump(file);
        const QByteArray json = parser.isSet(canonical) ? lumenis::interop::aaf::canonicalJson(dump) + '\n'
                                                        : QJsonDocument(dump.toObject()).toJson();
        std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
        return 0;
    }

    if (parser.isSet(dumpAvb)) {
        // What a bin holds, to compare with pyavb's reading of it.
        lumenis::interop::avb::File file;
        QString error;
        if (!file.open(parser.value(dumpAvb), &error)) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(parser.value(dumpAvb)), qPrintable(error));
            return 1;
        }
        const QJsonValue dump = lumenis::interop::avb::dump(file);
        const QByteArray json = parser.isSet(canonical) ? lumenis::interop::aaf::canonicalJson(dump) + '\n'
                                                        : QJsonDocument(dump.toObject()).toJson();
        std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
        return 0;
    }

    // --rewrite-avb IN OUT: the object layer's round trip (ctest avb_round_trip compares the bytes).
    if (parser.isSet(rewriteAvb)) {
        const QString from = parser.value(rewriteAvb);
        const QStringList rest = parser.positionalArguments();
        if (rest.size() != 1) {
            std::fprintf(stderr, "--rewrite-avb needs the file to write after the bin to read\n");
            return 2;
        }
        lumenis::interop::avb::File file;
        QString error;
        if (!file.open(from, &error)) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(from), qPrintable(error));
            return 1;
        }
        int unread = 0;
        for (std::uint32_t i = 1; i <= file.objectCount(); ++i) unread += file.object(i).ok() ? 0 : 1;
        if (unread > 0) std::fprintf(stderr, "rewrite-avb: %d objects not read, copied as they were\n", unread);
        const QByteArray bytes = lumenis::interop::avb::rewrite(file, &error);
        QSaveFile out(rest.first());
        if (bytes.isEmpty() || !out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
            std::fprintf(stderr, "cannot write %s: %s\n", qPrintable(rest.first()),
                         qPrintable(bytes.isEmpty() ? error : out.errorString()));
            return 1;
        }
        return 0;
    }

    if (parser.isSet(writeMedia)) {
        // MXF to check against a validator (ROADMAP Phase 2 gate 2,
        // scripts/check-mxf.py --make): what Import and Transcode write.
        const QString file = parser.value(writeMedia);
        const QString drive = parser.value(mediaDrive);
        if (drive.isEmpty()) {
            std::fprintf(stderr, "--write-media needs --drive <folder>\n");
            return 1;
        }
        QString error;
        const auto info = lumenis::media::probe(file, &error);
        if (!info) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(file), qPrintable(error));
            return 1;
        }
        // The file is linked as a clip at its own rate, then transcoded once per resolution at its own
        // raster, as Import does (media::transcodeClip(), src/media/managed.cpp).
        const lumenis::media::StreamInfo* video = info->firstVideo();
        lumenis::media::LinkOptions link;
        if (video && video->frameRate.num() > 0) link.editRate = video->frameRate;
        const auto clip = lumenis::media::linkClip(*info, link, &error);
        if (!clip) {
            std::fprintf(stderr, "cannot link %s: %s\n", qPrintable(file), qPrintable(error));
            return 1;
        }
        QList<lumenis::media::Resolution> chosen;
        if (parser.value(mediaResolution) == QLatin1String("all")) {
            chosen = lumenis::media::resolutions();
        } else if (const auto* r = lumenis::media::findResolution(parser.value(mediaResolution))) {
            chosen = {*r};
        } else {
            std::fprintf(stderr, "no resolution %s\n", qPrintable(parser.value(mediaResolution)));
            return 1;
        }
        QJsonArray made;
        for (const auto& resolution : chosen) {
            lumenis::media::MediaCreation creation;
            if (video) creation.raster = QSize(video->width, video->height);
            creation.resolution = resolution;
            creation.videoDrive = drive;
            creation.project = QStringLiteral("check-mxf");
            creation.when = QDateTime::currentDateTimeUtc();
            const auto result = lumenis::media::transcodeClip(*clip, 0, -1, creation, &error);
            if (!result) {
                std::fprintf(stderr, "%s: %s\n", qPrintable(resolution.id), qPrintable(error));
                return 1;
            }
            const auto media = lumenis::media::linkOf(*result);
            QJsonArray files;
            if (media && !media->path.isEmpty()) files.append(media->path);
            if (media) {
                for (const QString& audio : media->audioPaths) files.append(audio);
            }
            made.append(QJsonObject{{QStringLiteral("resolution"), resolution.id}, {QStringLiteral("files"), files}});
        }
        const QByteArray json = QJsonDocument(made).toJson();
        std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
        return 0;
    }

    if (parser.isSet(frameMd5)) {
        // The decode corpus (ROADMAP Phase 2 gate 1): "<frame> <md5>" for
        // every frame, by frame number, compared with FFmpeg's framemd5
        // (scripts/decode-corpus.py).
        lumenis::media::VideoReader reader;
        QString error;
        if (!reader.open(parser.value(frameMd5), -1, &error)) {
            std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(parser.value(frameMd5)), qPrintable(error));
            return 1;
        }
        // Hashes are read in the given (possibly shuffled, with a fixed seed so runs repeat) order but
        // printed by frame number; a frame that cannot be decoded prints "-" and makes the exit status 2.
        const std::int64_t count = reader.frameCount();
        std::vector<std::int64_t> order(static_cast<std::size_t>(std::max<std::int64_t>(count, 0)));
        std::iota(order.begin(), order.end(), std::int64_t{0});
        if (parser.isSet(shuffle)) {
            std::mt19937 random(20240617);
            std::shuffle(order.begin(), order.end(), random);
        }
        std::vector<QString> hashes(order.size());
        for (const std::int64_t n : order) hashes[static_cast<std::size_t>(n)] = reader.frameMd5(n);
        int missing = 0;
        for (std::size_t n = 0; n < hashes.size(); ++n) {
            if (hashes[n].isEmpty()) ++missing;
            std::printf("%zu %s\n", n, hashes[n].isEmpty() ? "-" : qPrintable(hashes[n]));
        }
        return missing == 0 ? 0 : 2;
    }

    // From here on the main window exists: only GUI invocations get this far, so app is a QApplication.
    auto& guiApp = static_cast<QApplication&>(*app);
    lumenis::ui::applyTheme(guiApp);
    // Warnings and errors also go to Tools > Console (p.101).
    lumenis::ui::ConsoleLog::installMessageHandler();
    // Title bars, the taskbar and the Dock while running (Linux uses it in
    // place of the installed SVG when the desktop file is not installed).
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/lumenis/ui/icons/app.png")));

    // CLAP audio plug-ins join the Audio Track Effects (docs/spec/clap-host.md). LUMENIS_NO_CLAP=1 skips
    // the scan, for a plug-in that crashes on loading.
    if (qEnvironmentVariableIntValue("LUMENIS_NO_CLAP") == 0) {
        QStringList errors;
        const auto plugins = lumenis::media::scanClapPlugins(
            lumenis::media::findClapFiles(lumenis::media::clapSearchPaths()), &errors);
        const int added = lumenis::media::registerClapPlugins(plugins);
        if (!plugins.isEmpty()) {
            lumenis::ui::ConsoleLog::instance().append(
                QObject::tr("CLAP: %1 plug-ins found, %2 added as Audio Track Effects.").arg(plugins.size()).arg(added));
        }
        for (const auto& p : plugins) {
            if (!p.usable()) lumenis::ui::ConsoleLog::instance().append(QObject::tr("CLAP: %1 left out: %2.").arg(p.name, p.whyNot()));
        }
        for (const QString& e : errors) lumenis::ui::ConsoleLog::instance().append(QObject::tr("CLAP: %1").arg(e));
    }

    // OFX image effect plug-ins join the Effect Palette (FX Guide p.157; docs/spec/ofx-host.md), before the palette's
    // catalog is first read. Why one did not load goes to the Console (p.163). LUMENIS_NO_OFX=1 skips the scan, for a
    // plug-in that crashes on loading.
    if (qEnvironmentVariableIntValue("LUMENIS_NO_OFX") == 0) {
        lumenis::media::setOfxMessageHandler(
            [](const QString& text) { lumenis::ui::ConsoleLog::instance().append(QObject::tr("OFX: %1").arg(text)); });
        QStringList errors;
        const auto plugins = lumenis::media::scanOfxPlugins(
            lumenis::media::findOfxBundles(lumenis::media::ofxSearchPaths()), &errors);
        const int added = lumenis::media::registerOfxPlugins(plugins);
        if (!plugins.isEmpty()) {
            lumenis::ui::ConsoleLog::instance().append(
                QObject::tr("OFX: %1 plug-ins found, %2 added to the Effect Palette.").arg(plugins.size()).arg(added));
        }
        for (const auto& p : plugins) {
            if (!p.usable()) lumenis::ui::ConsoleLog::instance().append(QObject::tr("OFX: %1 left out: %2.").arg(p.id, p.whyNot()));
        }
        for (const QString& e : errors) lumenis::ui::ConsoleLog::instance().append(QObject::tr("OFX: %1").arg(e));
    }

    // The language first: the commands' texts and every window are made in it. Tooling modes start in English unless
    // --language says otherwise, as they start from the default settings.
    {
        const bool toolingMode = parser.isSet(defaultLayout) || parser.isSet(dumpMenus) || parser.isSet(screenshot) ||
                                 parser.isSet(smokeTest) || parser.isSet(measurePlayback) || parser.isSet(soak);
        const QString code = lumenis::ui::startupLanguage(parser.value(language), !toolingMode);
        if (!lumenis::ui::installLanguage(guiApp, code)) {
            std::fprintf(stderr, "language %s is not built in; starting in English\n", qPrintable(code));
        }
    }
    lumenis::ui::CommandRegistry commands;
    commands.loadBuiltin();

    lumenis::ui::MainWindow window(commands, registry);

    // Test and tooling modes always start from the defaults so their output
    // does not depend on the user's saved settings.
    const bool useSavedSettings = !parser.isSet(defaultLayout) && !parser.isSet(dumpMenus)
        && !parser.isSet(screenshot) && !parser.isSet(smokeTest) && !parser.isSet(measurePlayback) && !parser.isSet(soak);
    QSettings settings;
    if (useSavedSettings) {
        commands.loadShortcuts(settings);
        window.restoreGeometry(settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
        // Workspaces are User settings now (p.86), loaded with the user.
        // Favorite Bins and bin views are User settings too (p.293, p.249); lists an older build
        // kept here move into the user's Bin setting.
        window.adoptLegacyBinLists(lumenis::ui::FavoriteBins::legacy(settings),
                                   lumenis::ui::BinViewLibrary::legacy(settings));
        window.sourceBrowser().restore(settings);
        // Saved as it changes too, so a crash does not lose the folder and settings; connected
        // after restore() so restoring does not write back what it just read.
        QObject::connect(&window.sourceBrowser(), &lumenis::ui::SourceBrowser::stateChanged, &guiApp,
                         [&] { window.sourceBrowser().save(settings); });
        QObject::connect(&guiApp, &QApplication::aboutToQuit, [&] {
            window.sourceBrowser().save(settings);
            settings.setValue(QStringLiteral("mainWindow/geometry"), window.saveGeometry());
            if (!window.legacyBinListsPending()) {
                lumenis::ui::FavoriteBins::forgetLegacy(settings);
                lumenis::ui::BinViewLibrary::forgetLegacy(settings);
            }
            commands.saveShortcuts(settings);
        });
    }

    if (parser.isSet(dumpMenus)) {
        // The commands an editor has with a project open: the Phase 1
        // gate 5 checklist is made from this (scripts/make-checklist.py).
        QTemporaryDir sampleDir;
        if (parser.isSet(sampleProject)) {
            lumenis::model::ProjectSettings s;
            s.name = QStringLiteral("Sample");
            s.format = lumenis::projectFormatPresets().front();
            const QString users = sampleDir.filePath(QStringLiteral("Users"));
            lumenis::model::UserProfiles(users).create(QStringLiteral("editor"));
            window.settings().setUser(users, QStringLiteral("editor"));
            if (auto entry = lumenis::model::ProjectStore(sampleDir.filePath(QStringLiteral("Projects"))).create(s)) {
                window.setProject(*entry);
            }
        }
        const QByteArray json =
            QJsonDocument(lumenis::ui::dumpMenus(window.menuBar(), parser.isSet(sampleProject) ? &commands : nullptr))
                .toJson();
        const QString target = parser.value(dumpMenus);
        if (target == QLatin1String("-")) {
            std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
            return 0;
        }
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly) || out.write(json) != json.size()) {
            std::fprintf(stderr, "cannot write %s\n", qPrintable(target));
            return 1;
        }
        return 0;
    }

    if (parser.isSet(soak)) {
        bool hoursOk = false;
        const double hours = parser.value(soak).toDouble(&hoursOk);
        if (!hoursOk || hours <= 0.0) {
            std::fprintf(stderr, "--soak needs a number of hours above 0\n");
            return 1;
        }
        lumenis::ui::SoakOptions options;
        options.hours = hours;
        options.intervalSeconds = std::max(parser.value(soakInterval).toInt(), 1);
        options.logPath = parser.value(soakLog);
        options.seed = static_cast<std::uint32_t>(parser.value(soakSeed).toUInt());
        options.cycles = std::max(parser.value(soakCycles).toInt(), 0);
        window.resize(1600, 900);
        window.show();
        lumenis::ui::Soak run(window, commands, options);
        QString error;
        if (!run.prepare(&error)) {
            std::fprintf(stderr, "soak: %s\n", qPrintable(error));
            return 1;
        }
        int code = 0;
        QObject::connect(&run, &lumenis::ui::Soak::finished, &guiApp, [&](int c) {
            code = c;
            QApplication::quit();
        });
        run.start();
        guiApp.exec();
        std::printf("soak: %d cycles, log %s\n", run.cycles(), qPrintable(options.logPath));
        std::fflush(stdout);
        // The project's files are in the soak's folder, which goes with `run`.
        window.clearProject();
        return code;
    }

    if (parser.isSet(measurePlayback)) {
        // Real-time playback and A/V sync (ROADMAP Phase 2 gates 3 and 4):
        // the file in a project of its own format, played at normal speed.
        const QString file = parser.value(measurePlayback);
        const auto info = lumenis::media::probe(file);
        const lumenis::media::StreamInfo* video = info ? info->firstVideo() : nullptr;
        if (!video) {
            std::fprintf(stderr, "%s has no picture\n", qPrintable(file));
            return 1;
        }
        QTemporaryDir dir;
        lumenis::model::ProjectSettings s;
        s.name = QStringLiteral("Playback");
        s.format = lumenis::projectFormatPresets().front();
        s.format.width = video->width;
        s.format.height = video->height;
        if (video->frameRate.num() > 0) s.format.editRate = video->frameRate;
        const QString users = dir.filePath(QStringLiteral("Users"));
        lumenis::model::UserProfiles(users).create(QStringLiteral("editor"));
        window.settings().setUser(users, QStringLiteral("editor"));
        const auto entry = lumenis::model::ProjectStore(dir.filePath(QStringLiteral("Projects"))).create(s);
        if (!entry) return 1;
        window.setProject(*entry);
        window.resize(1280, 780);
        window.show();
        const QString binId = window.binContainer().newBin();
        lumenis::ui::BinDocument* bin = window.binContainer().openBin(binId);
        const QStringList ids = bin ? window.linkFiles(binId, {file}) : QStringList{};
        if (ids.isEmpty()) {
            std::fprintf(stderr, "cannot link %s\n", qPrintable(file));
            return 1;
        }
        const lumenis::model::BinItem item = *bin->bin().item(ids.first());
        lumenis::ui::Composer& composer = window.composer();
        composer.loadDefault(binId, item);
        composer.setActiveMonitor(lumenis::ui::MonitorId::Source);
        // --seconds: how long to play, at least 1.
        const int seconds = std::max(parser.value(playSeconds).toInt(), 1);
        // Every 20 ms: where the picture is against the sound heard, in
        // frames, after half a second for the output to start.
        double low = 1e9;
        double high = -1e9;
        double sum = 0;
        std::int64_t samples = 0;
        QElapsedTimer clock;
        QTimer sampler;
        sampler.setInterval(20);
        sampler.setTimerType(Qt::PreciseTimer);
        QObject::connect(&sampler, &QTimer::timeout, [&] {
            if (!composer.isPlaying() || !window.isPlayingSound() || clock.elapsed() < 500) return;
            const double heard = lumenis::media::samplesToFrames(window.soundClock(), item.editRate);
            const double offset =
                static_cast<double>(composer.state(lumenis::ui::MonitorId::Source).position()) - heard;
            low = std::min(low, offset);
            high = std::max(high, offset);
            sum += offset;
            ++samples;
        });
        // Lip sync: each time the picture moves on to frame n, how far the
        // sound already is past n's start, in frames; 0 is perfect, and the
        // gate is half a frame.
        double lateMax = 0;
        double lateSum = 0;
        std::int64_t changes = 0;
        std::int64_t lastShown = -1;
        // Each lateness, for how many are over the gate and a percentile,
        // and when frames were dropped (seconds into the run), to match
        // against what else the machine was doing.
        std::vector<double> lates;
        QJsonArray dropTimes;
        std::int64_t droppedSoFar = 0;
        const QMetaObject::Connection watch = QObject::connect(&composer, &lumenis::ui::Composer::changed, &guiApp, [&] {
            const std::int64_t shownNow = composer.state(lumenis::ui::MonitorId::Source).position();
            if (shownNow == lastShown) return;
            lastShown = shownNow;
            // From the start: a drop while the sound starts counts too.
            if (composer.playStats().dropped > droppedSoFar) {
                droppedSoFar = composer.playStats().dropped;
                dropTimes.append(static_cast<double>(clock.elapsed()) / 1000.0);
            }
            if (!composer.isPlaying() || !window.isPlayingSound() || clock.elapsed() < 500) return;
            const double heard = lumenis::media::samplesToFrames(window.soundClock(), item.editRate);
            const double late = heard - static_cast<double>(shownNow);
            lateMax = std::max(lateMax, late);
            lateSum += late;
            ++changes;
            lates.push_back(late);
        });
        // As an editor would: the window drawn and the clip's frame shown
        // before Play. Played straight after show(), the window's first paint
        // (on macOS the surface and fonts, 100 ms or more) fell in the first
        // ticks and the picture skipped frames to catch up.
        {
            QEventLoop settle;
            QTimer::singleShot(1000, &settle, &QEventLoop::quit);
            settle.exec();
        }
        composer.play(1);
        clock.start();
        sampler.start();
        QTimer::singleShot(seconds * 1000, &guiApp, [&] {
            composer.stop();
            QApplication::quit();
        });
        guiApp.exec();
        // The lambdas above use this block's variables, which go before the
        // window does: nothing may reach them while it closes down.
        QObject::disconnect(watch);
        sampler.stop();
        const double mean = samples > 0 ? sum / static_cast<double>(samples) : 0.0;
        std::sort(lates.begin(), lates.end());
        const auto over = static_cast<qint64>(lates.end() - std::upper_bound(lates.begin(), lates.end(), 0.5));
        const double p999 = lates.empty() ? 0.0 : lates[static_cast<std::size_t>(0.999 * static_cast<double>(lates.size() - 1))];
        const QJsonObject result{
            {QStringLiteral("file"), file},
            {QStringLiteral("seconds"), seconds},
            {QStringLiteral("rate"), QString::number(item.editRate.toDouble(), 'f', 3)},
            {QStringLiteral("shown"), static_cast<qint64>(composer.playStats().shown)},
            {QStringLiteral("dropped"), static_cast<qint64>(composer.playStats().dropped)},
            {QStringLiteral("sound"), samples > 0},
            {QStringLiteral("offsetMin"), samples > 0 ? low : 0.0},
            {QStringLiteral("offsetMax"), samples > 0 ? high : 0.0},
            {QStringLiteral("offsetMean"), mean},
            // How late the picture changes after the sound enters a frame.
            {QStringLiteral("lipSyncError"), changes > 0 ? lateMax : 0.0},
            {QStringLiteral("lipSyncMean"), changes > 0 ? lateSum / static_cast<double>(changes) : 0.0},
            // How many picture changes were more than half a frame late, and
            // the 99.9th percentile: one late change or a pattern.
            {QStringLiteral("lipSyncOver"), over},
            {QStringLiteral("lipSyncP999"), p999},
            {QStringLiteral("dropTimes"), dropTimes},
        };
        const QByteArray json = QJsonDocument(result).toJson();
        std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
        // Now, so a failure while closing down still leaves the result.
        std::fflush(stdout);
        // Its files are in dir, which goes first (Windows keeps open files).
        window.clearProject();
        return 0;
    }

    // --screenshot <dir>: fixed sample projects, bins and sequences in a temporary folder, then a PNG of
    // each window and tool in a fixed state, saved through ui::capture(). The images are compared across
    // platforms (ui-consistency in CI, scripts/ci-screenshots.py), so everything shown must be the same
    // on every machine: fixed sizes, dates and labels, no focus or text cursor.
    if (parser.isSet(screenshot)) {
        // Same content size on every platform: the macOS menu bar lives
        // outside the window, elsewhere it takes window height.
        const int menuHeight = window.menuBar()->isNativeMenuBar() ? 0 : window.menuBar()->sizeHint().height();
        window.resize(1280, 780 + menuHeight);
        window.show();

        const QDir dir(parser.value(screenshot));
        if (!dir.mkpath(QStringLiteral("."))) {
            std::fprintf(stderr, "cannot create %s\n", qPrintable(dir.path()));
            return 1;
        }
        // Select Project with fixed sample content, so every platform shows
        // the same thing.
        QTemporaryDir fixture;
        lumenis::model::Locations sample;
        sample.privateProjects = fixture.filePath(QStringLiteral("Projects"));
        sample.sharedProjects = fixture.filePath(QStringLiteral("Shared"));
        sample.users = fixture.filePath(QStringLiteral("Users"));
        lumenis::model::UserProfiles(sample.users).create(QStringLiteral("editor"));
        const QDateTime when(QDate(2026, 1, 15), QTime(10, 30), QTimeZone::utc());
        const std::pair<const char*, const char*> samples[] = {
            {"Documentary", "1080p/23.976"}, {"Episode 101", "1080i/59.94"}, {"Music Video", "UHD 25p"}};
        std::optional<lumenis::model::ProjectEntry> documentary;
        for (const auto& [name, label] : samples) {
            lumenis::model::ProjectSettings s;
            s.name = QString::fromLatin1(name);
            for (const auto& f : lumenis::projectFormatPresets()) {
                if (f.label() == label) s.format = f;
            }
            s.created = when;
            s.modified = when;
            auto entry = lumenis::model::ProjectStore(sample.privateProjects).create(s);
            if (!documentary) documentary = entry;
        }
        // The main window shows the first project with a few bins.
        if (documentary) {
            lumenis::model::ProjectBins bins(documentary->folder, documentary->settings.name);
            bins.load();
            const QString main = bins.bins().first()->id;
            const QString folder = bins.createFolder();
            bins.rename(folder, QStringLiteral("Selects"));
            bins.move(bins.createBin(), folder);
            bins.rename(bins.createBin(folder), QStringLiteral("Interviews"));
            bins.openBins = {main};
            bins.save();
            auto bin = lumenis::model::BinFile::read(bins.binPath(main));
            const lumenis::Rational rate = documentary->settings.format.editRate;
            // Master clips, and sequences cut from them.
            const std::int64_t fps = 24;
            const std::pair<const char*, std::int64_t> clips[] = {
                {"Interview Harbor", 9 * 60 * fps}, {"Harbor Wide", 4 * 60 * fps}, {"Interview Market", 7 * 60 * fps},
                {"Market Detail", 3 * 60 * fps}, {"Ferry Arrival", 2 * 60 * fps}};
            QList<lumenis::model::BinItem> masters;
            for (const auto& [name, frames] : clips) {
                lumenis::model::BinItem c;
                c.id = lumenis::model::newItemId();
                c.kind = lumenis::model::BinItemKind::MasterClip;
                c.name = QString::fromLatin1(name);
                c.created = when;
                c.modified = when;
                c.durationFrames = frames;
                c.editRate = rate;
                c.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1 A1-2"));
                masters << c;
            }
            auto cut = [&](const QList<std::pair<int, std::int64_t>>& edits) {
                // Each edit: a master clip and its length in seconds, on V1,
                // A1 and A2, with a cutaway on V2 over the second edit.
                lumenis::model::Sequence seq = lumenis::model::Sequence::withDefaultTracks();
                std::int64_t at = 0;
                for (int i = 0; i < edits.size(); ++i) {
                    const auto& [index, seconds] = edits.at(i);
                    const lumenis::model::BinItem& c = masters.at(index);
                    for (const QString& t : {QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")}) {
                        lumenis::model::Segment s;
                        s.clipId = c.id;
                        s.binId = main;
                        s.name = c.name;
                        s.sourceTrack = t;
                        s.sourceIn = 30 * fps * i;
                        s.length = seconds * fps;
                        seq.splice(t, at, {s});
                    }
                    if (i == 1) {
                        const lumenis::model::BinItem& b = masters.at(3);
                        seq.ensureTrack(QStringLiteral("V2"));
                        lumenis::model::Segment s;
                        s.clipId = b.id;
                        s.binId = main;
                        s.name = b.name;
                        s.sourceTrack = QStringLiteral("V1");
                        s.length = seconds * fps / 2;
                        seq.overwrite(QStringLiteral("V2"), at + seconds * fps / 4, {s});
                    }
                    at += seconds * fps;
                }
                for (const QString& t : {QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")}) {
                    seq.track(t)->syncLocked = true;
                }
                return seq;
            };
            const std::pair<const char*, lumenis::model::Sequence> sequences[] = {
                {"Rough Cut", cut({{0, 95}, {1, 40}, {2, 120}, {4, 35}, {0, 80}, {3, 25}, {2, 140}, {1, 60}})},
                {"Opening", cut({{4, 30}, {1, 45}, {0, 20}})},
                {"Trailer", cut({{1, 15}, {3, 10}, {2, 20}, {4, 17}})}};
            for (const auto& [name, seq] : sequences) {
                lumenis::model::BinItem item = lumenis::model::newSequence(*bin, rate, when);
                item.name = QString::fromLatin1(name);
                seq.storeIn(item);
                bin->items << item;
            }
            // Markers on the rough cut (p.413-415): two on tracks, one span.
            {
                lumenis::model::BinItem& cutItem = bin->items[bin->items.size() - 3];
                const std::tuple<const char*, const char*, std::int64_t, std::int64_t> marks[] = {
                    {"Red", "V1", 40 * fps, 1}, {"Blue", "A1", 170 * fps, 1}, {"Green", "TC1", 290 * fps, 30 * fps}};
                for (const auto& [color, track, frame, length] : marks) {
                    lumenis::model::Marker m;
                    m.id = lumenis::model::newItemId();
                    m.color = QString::fromLatin1(color);
                    m.track = QString::fromLatin1(track);
                    m.frame = frame;
                    m.length = length;
                    lumenis::model::addMarker(cutItem, m);
                }
            }
            bin->items << masters;
            lumenis::model::BinFile::write(bins.binPath(main), *bin);
            window.settings().setUser(sample.users, QStringLiteral("editor"));
            window.setProject(*documentary);
            // The Composer with a clip in the Source monitor and the rough
            // cut in the Record monitor, marked, with master timecode above
            // them (p.380, p.384); the Timeline shows the cut (p.607).
            if (const lumenis::ui::BinDocument* doc = window.binContainer().document(main)) {
                lumenis::ui::Composer& composer = window.composer();
                const auto& items = doc->bin().items;
                composer.load(lumenis::ui::MonitorId::Source, main, items.at(3));
                composer.load(lumenis::ui::MonitorId::Record, main, items.at(0));
                composer.state(lumenis::ui::MonitorId::Source).setPosition(240);
                composer.state(lumenis::ui::MonitorId::Source).markIn();
                composer.state(lumenis::ui::MonitorId::Source).setPosition(1200);
                composer.state(lumenis::ui::MonitorId::Source).markOut();
                composer.state(lumenis::ui::MonitorId::Record).setPosition(150 * fps);
                composer.state(lumenis::ui::MonitorId::Record).markIn();
                composer.state(lumenis::ui::MonitorId::Record).setPosition(190 * fps);
                composer.setTracking(lumenis::ui::MonitorId::Source, lumenis::model::TrackingFormat::Master);
                composer.setTracking(lumenis::ui::MonitorId::Record, lumenis::model::TrackingFormat::Master);
                composer.setActiveMonitor(lumenis::ui::MonitorId::Source);
                window.timeline().patch().setRecordSelected(QStringLiteral("V2"), false);
                // The Lift/Overwrite segment tool with a segment selected (p.611).
                window.timeline().setSegmentTool(lumenis::model::EditKind::Overwrite, true);
                window.timeline().setSelection({{QStringLiteral("V1"), 95 * fps}});
            }
        }
        lumenis::ui::SelectProjectDialog selectProject(sample);
        // The fixture lives in a temporary folder whose path differs per machine.
        selectProject.setLocationLabel(lumenis::ui::SelectProjectDialog::Location::Private,
                                       QStringLiteral("Documents/Cutthulhu Projects"));
        selectProject.setLocationLabel(lumenis::ui::SelectProjectDialog::Location::Shared,
                                       QStringLiteral("Shared Cutthulhu Projects"));
        selectProject.selectProject(QStringLiteral("Episode 101"));
        selectProject.resize(980, 640);
        selectProject.show();

        // Grab after the first event-loop pass so layouts have settled. The
        // menu bar is excluded: on macOS it is not part of the window.
        QTimer::singleShot(0, app.get(), [&] {
            QWidget* content = window.centralWidget() ? window.centralWidget() : &window;
            // The Timeline zoomed to the first seven minutes of the cut.
            window.timeline().zoomTo(0, 7 * 60 * 24);
            bool ok = lumenis::ui::capture(content, dir, QStringLiteral("main_window"));
            // Trim mode: Big Trim display and dual rollers (p.648, p.655).
            {
                lumenis::ui::Timeline& timeline = window.timeline();
                lumenis::ui::Composer& composer = window.composer();
                const std::int64_t at = composer.state(lumenis::ui::MonitorId::Record).position();
                timeline.setSelection({});
                timeline.perform(QStringLiteral("trim.trim_mode"));
                QCoreApplication::processEvents();
                ok = lumenis::ui::capture(content, dir, QStringLiteral("trim_mode")) && ok;
                timeline.exitTrimMode();
                composer.moveTo(lumenis::ui::MonitorId::Record, at);
            }
            // The green drop zones while the Composer's title bar is dragged
            // over the Timeline's upper zone (p.75).
            {
                auto& panels = window.panels();
                lumenis::ui::PanelGroup* composer = panels.panel(QStringLiteral("Composer"))->group();
                lumenis::ui::PanelGroup* timeline = panels.panel(QStringLiteral("Timeline"))->group();
                panels.beginDrag(composer, nullptr);
                for (const auto& zone : panels.dropZones(panels.mainHost(), false)) {
                    if (zone.group == timeline && zone.side == lumenis::ui::DockSide::Top) {
                        panels.updateDrag(panels.mainHost()->mapToGlobal(zone.zone.center()), false);
                    }
                }
                ok = lumenis::ui::capture(content, dir, QStringLiteral("drop_zones")) && ok;
                panels.cancelDrag();
            }
            // The open bin in Frame and Script views (p.249-253).
            if (lumenis::ui::BinView* bin = window.binContainer().activeView()) {
                bin->setMode(lumenis::model::BinViewMode::Frame);
                QCoreApplication::processEvents();
                ok = lumenis::ui::capture(content, dir, QStringLiteral("bin_frame_view")) && ok;
                bin->setMode(lumenis::model::BinViewMode::Script);
                QCoreApplication::processEvents();
                ok = lumenis::ui::capture(content, dir, QStringLiteral("bin_script_view")) && ok;
                bin->setMode(lumenis::model::BinViewMode::Text);
            }
            // Two bins one above the other, and the green zone for docking a
            // third beside them (p.241).
            {
                lumenis::ui::BinContainer& bins = window.binContainer();
                const QString interviews = bins.bins()->bins().last()->id;
                if (lumenis::ui::BinDocument* doc = bins.openBin(interviews)) {
                    lumenis::ui::BinDockArea* area = bins.binArea();
                    bins.dockBin(doc, area->groups().first(), lumenis::ui::BinDropSide::Bottom);
                    QCoreApplication::processEvents();
                    lumenis::ui::BinTabGroup* top = area->groups().first();
                    const QRect r(top->mapTo(area, QPoint()), top->size());
                    area->setHoveredZone(area->zoneAt(QPoint(r.right() - 4, r.center().y()), false));
                    ok = lumenis::ui::capture(content, dir, QStringLiteral("bin_docking")) && ok;
                    area->setHoveredZone({});
                    bins.closeBin(doc);
                }
            }
            // File > Settings, User tab, with two versions of the Timeline
            // setting (p.1136-1139), and the Timeline Settings dialog
            // (p.1218).
            {
                lumenis::ui::Settings& userSettings = window.settings();
                lumenis::model::SettingsFile& user = userSettings.file(lumenis::model::SettingsScope::User);
                const int timeline = user.activeIndex(QStringLiteral("Timeline"));
                user.entries[timeline].name = QStringLiteral("Editing");
                user.duplicate(timeline);
                userSettings.commit(lumenis::model::SettingsScope::User);
                lumenis::ui::SettingsWindow* sw = window.showSettings();
                lumenis::ui::SettingsList* list = sw->list(lumenis::model::SettingsScope::User);
                list->selectEntries({user.activeIndex(QStringLiteral("Timeline"))});
                sw->resize(380, 480);
                QCoreApplication::processEvents();
                ok = lumenis::ui::capture(sw, dir, QStringLiteral("settings_window")) && ok;
                sw->hide();
                const auto* kind = lumenis::ui::SettingsSchema::builtIn().find(QStringLiteral("Timeline"));
                lumenis::ui::SettingsDialog dialog(*kind, user.entries.at(user.activeIndex(QStringLiteral("Timeline"))), &window);
                // A fixed size: its natural width follows the font, which
                // differs between platforms. Tall enough for every option:
                // at 580 the radio groups were squashed out of sight.
                dialog.setFixedSize(400, 800);
                dialog.show();
                QCoreApplication::processEvents();
                // macOS puts Cancel before OK: no highlighted default or
                // focused button, so the order does not show in the
                // text-free image. The button box makes OK the default
                // when shown, so this comes after show().
                for (auto* b : dialog.findChildren<QPushButton*>()) {
                    b->setDefault(false);
                    b->setAutoDefault(false);
                    b->clearFocus();
                }
                QCoreApplication::processEvents();
                ok = lumenis::ui::capture(&dialog, dir, QStringLiteral("timeline_settings")) && ok;
                dialog.hide();
            }
            // Tools > Command Palette, Move tab (picture on p.96), floating.
            if (lumenis::ui::Panel* palette = window.showTool(QStringLiteral("CommandPalette"))) {
                QWidget* floating = palette->window();
                floating->resize(820, 280);
                QCoreApplication::processEvents();
                // No text cursor in Quick Find: it blinks between grabs.
                window.commandPalette().quickFind()->clearFocus();
                QCoreApplication::processEvents();
                ok = lumenis::ui::capture(floating, dir, QStringLiteral("command_palette")) && ok;
                window.panels().closePanel(palette);
            }
            // The Keyboard setting's palette (picture on p.1202), floating.
            if (lumenis::ui::Panel* keys = window.showTool(QStringLiteral("Keyboard"))) {
                QWidget* floating = keys->window();
                floating->resize(720, 320);
                QCoreApplication::processEvents();
                ok = lumenis::ui::capture(floating, dir, QStringLiteral("keyboard")) && ok;
                window.panels().closePanel(keys);
            }
            // The Effect Palette, Filters tab, Illusion FX (picture on p.37),
            // and the Effect Editor showing a Picture-in-Picture effect on
            // the segment under the position (picture on p.84).
            {
                lumenis::ui::Timeline& timeline = window.timeline();
                const auto& record = window.composer().state(lumenis::ui::MonitorId::Record);
                timeline.setSelection({});
                timeline.setInsufficientSource([](const QString&) { return true; });
                if (const auto* pip = lumenis::ui::EffectCatalog::builtin().find(QStringLiteral("Blend"),
                                                                                 QStringLiteral("Picture-in-Picture"))) {
                    timeline.dropEffect(QStringLiteral("V1"), record.position(), *pip);
                }
                if (lumenis::ui::Panel* palette = window.showTool(QStringLiteral("EffectPalette"))) {
                    window.effectPalette().setCategory(QStringLiteral("Illusion FX"));
                    QWidget* floating = palette->window();
                    floating->resize(480, 420);
                    QCoreApplication::processEvents();
                    window.effectPalette().quickFind()->clearFocus();
                    QCoreApplication::processEvents();
                    ok = lumenis::ui::capture(floating, dir, QStringLiteral("effect_palette")) && ok;
                    window.panels().closePanel(palette);
                }
                if (lumenis::ui::Panel* editor = window.showTool(QStringLiteral("EffectEditor"))) {
                    QWidget* floating = editor->window();
                    floating->resize(340, 560);
                    QCoreApplication::processEvents();
                    ok = lumenis::ui::capture(floating, dir, QStringLiteral("effect_editor")) && ok;
                    window.panels().closePanel(editor);
                }
            }
            // The Audio Mixer in Clip mode (pictures on p.691 and p.693) and
            // the Audio Tool (picture on p.158).
            if (lumenis::ui::Panel* mixer = window.showTool(QStringLiteral("AudioMixer"))) {
                QWidget* floating = mixer->window();
                floating->resize(560, 440);
                QCoreApplication::processEvents();
                ok = lumenis::ui::capture(floating, dir, QStringLiteral("audio_mixer")) && ok;
                window.panels().closePanel(mixer);
            }
            if (lumenis::ui::Panel* tool = window.showTool(QStringLiteral("AudioTool"))) {
                QWidget* floating = tool->window();
                floating->resize(300, 340);
                QCoreApplication::processEvents();
                ok = lumenis::ui::capture(floating, dir, QStringLiteral("audio_tool")) && ok;
                window.panels().closePanel(tool);
            }
            ok = lumenis::ui::capture(&selectProject, dir, QStringLiteral("select_project")) && ok;
            selectProject.showPage(lumenis::ui::SelectProjectDialog::Page::NewProject);
            if (auto* toggle = selectProject.findChild<QToolButton*>(QStringLiteral("settingsToggle"))) {
                toggle->setChecked(true);
            }
            // No focus: a blinking text cursor would differ between grabs.
            // Focus on a button: a text field with focus draws a text cursor.
            if (auto* button = selectProject.findChild<QPushButton*>(QStringLiteral("createButton"))) button->setFocus();
            // A selection's width depends on the font; text-free images must not show it.
            for (auto* edit : selectProject.findChildren<QLineEdit*>()) edit->deselect();
            QCoreApplication::processEvents();
            ok = lumenis::ui::capture(&selectProject, dir, QStringLiteral("new_project")) && ok;
            std::printf("screenshot: %s\n", ok ? "ok" : "FAILED");
            std::fflush(stdout);
            QCoreApplication::exit(ok ? 0 : 1);
        });
        return app->exec();
    }

    // --smoke-test: the main window opens with the product's title, within 30 seconds (CI's installer
    // checks launch the installed application this way).
    if (parser.isSet(smokeTest)) {
        window.show();
        // Fail rather than hang if the event loop never gets going.
        QTimer::singleShot(30000, app.get(), [] {
            std::fprintf(stderr, "smoke-test: timed out\n");
            QCoreApplication::exit(2);
        });
        QTimer::singleShot(0, app.get(), [&window] {
            const bool ok = window.isVisible()
                && window.windowTitle() == QString::fromUtf8(lumenis::branding::kProductName);
            std::printf("smoke-test: %s\n", ok ? "ok" : "FAILED");
            std::fflush(stdout);
            QCoreApplication::exit(ok ? 0 : 1);
        });
        return app->exec();
    }

    // Normal start: Select Project first (guide p.57); after File > Close
    // Project, Select Project again; quitting there ends the application.
    const lumenis::model::Locations locations = lumenis::model::Locations::defaults();
    // Windows come and go while switching projects, so quitting is explicit:
    // closing the main window or quitting from Select Project.
    QApplication::setQuitOnLastWindowClosed(false);
    QObject::connect(&window, &lumenis::ui::MainWindow::closed, &guiApp, &QApplication::quit);

    // Site settings (p.1141), and the user chosen in Select Project
    // (p.1138).
    window.settings().setSiteFolder(locations.siteSettings);
    // Bins are backed up into the attic as they are saved (p.71).
    window.binContainer().setAttic(locations.attic);
    // Opens a project file in the main window; false (with the reason on stderr) if it cannot be read.
    auto openProjectFile = [&window](const QString& file) {
        QString error;
        const auto projectSettings = lumenis::model::ProjectStore::read(file, &error);
        if (!projectSettings) {
            std::fprintf(stderr, "cannot open project %s: %s\n", qPrintable(file), qPrintable(error));
            return false;
        }
        window.setProject({QFileInfo(file).absolutePath(), file, *projectSettings});
        window.show();
        return true;
    };

    // Shows a new Select Project dialog; accepting it opens the chosen project as the chosen user,
    // anything else quits.
    std::function<void()> showSelectProject = [&] {
        window.hide();
        window.clearProject();
        auto* dialog = new lumenis::ui::SelectProjectDialog(locations);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(dialog, &QDialog::finished, &guiApp, [&, dialog](int result) {
            if (result == QDialog::Accepted) window.settings().setUser(locations.users, dialog->userProfile());
            if (result != QDialog::Accepted || !openProjectFile(dialog->selectedProjectFile())) {
                QApplication::quit();
            }
        });
        dialog->show();
    };
    QObject::connect(&window, &lumenis::ui::MainWindow::closeProjectRequested, &guiApp, [&] { showSelectProject(); });

    // --project <file>: straight into the project, as the default user.
    if (parser.isSet(openProject)) {
        window.settings().setUser(locations.users, lumenis::model::UserProfiles::defaultName());
        if (!openProjectFile(parser.value(openProject))) {
            return 1;
        }
    } else {
        showSelectProject();
    }
    return app->exec();
}
