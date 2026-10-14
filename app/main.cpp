#include "module_paths.h"

#include "lumenis/core/branding.h"
#include "lumenis/ext/module_loader.h"
#include "lumenis/ext/provider.h"
#include "lumenis/model/locations.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/capture.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/select_project_dialog.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/menu_builder.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/platform_check.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/workspace.h"

#include <QApplication>
#include <QIcon>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTimeZone>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QMenuBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <optional>
#include <QToolButton>

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string_view>

#if defined(LUMENIS_WIN32_ATTACH_CONSOLE)
#  include <windows.h>
#endif

namespace {

// Options that only print and exit; they must work without a display.
bool isHeadlessInvocation(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--version" || arg == "-v" || arg == "--help" || arg == "-h" || arg == "-?"
            || arg == "--list-providers") {
            return true;
        }
    }
    return false;
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

int main(int argc, char** argv) {
    attachParentConsole();

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
    parser.addOption(listProviders);
    parser.addOption(smokeTest);
    parser.addOption(dumpMenus);
    parser.addOption(screenshot);
    parser.addOption(defaultLayout);
    parser.addOption(openProject);
    parser.process(*app);

    lumenis::ext::ProviderRegistry registry;
    lumenis::ext::ModuleLoader loader(registry);
    loader.loadDirectories(lumenis::app::moduleSearchPaths());

    if (parser.isSet(listProviders)) {
        printProviders(registry, loader);
        return 0;
    }

    auto& guiApp = static_cast<QApplication&>(*app);
    lumenis::ui::applyTheme(guiApp);
    // Title bars, the taskbar and the Dock while running (Linux uses it in
    // place of the installed SVG when the desktop file is not installed).
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/lumenis/ui/icons/app.png")));

    lumenis::ui::CommandRegistry commands;
    commands.loadBuiltin();

    lumenis::ui::MainWindow window(commands, registry);

    // Test and tooling modes always start from the defaults so their output
    // does not depend on the user's saved settings.
    const bool useSavedSettings = !parser.isSet(defaultLayout) && !parser.isSet(dumpMenus)
        && !parser.isSet(screenshot) && !parser.isSet(smokeTest);
    QSettings settings;
    if (useSavedSettings) {
        commands.loadShortcuts(settings);
        window.restoreGeometry(settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
        window.workspaces().load(settings);
        lumenis::ui::BinViewLibrary::load(settings);
        lumenis::ui::FavoriteBins::load(settings);
        QObject::connect(&guiApp, &QApplication::aboutToQuit, [&] {
            settings.setValue(QStringLiteral("mainWindow/geometry"), window.saveGeometry());
            window.workspaces().save(settings);
            lumenis::ui::BinViewLibrary::save(settings);
            lumenis::ui::FavoriteBins::save(settings);
            commands.saveShortcuts(settings);
        });
    }

    if (parser.isSet(dumpMenus)) {
        const QByteArray json = QJsonDocument(lumenis::ui::dumpMenus(window.menuBar())).toJson();
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
            bin->items << masters;
            lumenis::model::BinFile::write(bins.binPath(main), *bin);
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

    std::function<void()> showSelectProject = [&] {
        window.hide();
        window.clearProject();
        auto* dialog = new lumenis::ui::SelectProjectDialog(locations);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(dialog, &QDialog::finished, &guiApp, [&, dialog](int result) {
            if (result != QDialog::Accepted || !openProjectFile(dialog->selectedProjectFile())) {
                QApplication::quit();
            }
        });
        dialog->show();
    };
    QObject::connect(&window, &lumenis::ui::MainWindow::closeProjectRequested, &guiApp, [&] { showSelectProject(); });

    if (parser.isSet(openProject)) {
        if (!openProjectFile(parser.value(openProject))) {
            return 1;
        }
    } else {
        showSelectProject();
    }
    return app->exec();
}
