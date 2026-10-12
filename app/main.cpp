#include "module_paths.h"

#include "lumenis/core/branding.h"
#include "lumenis/ext/module_loader.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/menu_builder.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/workspace.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMenuBar>
#include <QSettings>
#include <QTimer>

#include <cstdio>
#include <cstring>
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
    const QCommandLineOption defaultLayout(
        QStringLiteral("default-layout"), QStringLiteral("Ignore the saved window layout and keyboard settings."));
    parser.addOption(listProviders);
    parser.addOption(smokeTest);
    parser.addOption(dumpMenus);
    parser.addOption(screenshot);
    parser.addOption(defaultLayout);
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

    lumenis::ui::CommandRegistry commands;
    commands.loadBuiltin();

    lumenis::ui::MainWindow window(commands, registry);

    // Test and tooling modes always start from the defaults so their output
    // does not depend on the user's saved settings.
    const bool useSavedSettings = !parser.isSet(defaultLayout) && !parser.isSet(dumpMenus)
        && !parser.isSet(screenshot) && !parser.isSet(smokeTest);
    QSettings settings;
    lumenis::ui::WorkspaceStore workspaces(settings);
    if (useSavedSettings) {
        commands.loadShortcuts(settings);
        workspaces.restore(lumenis::ui::WorkspaceStore::lastUsedName(), window);
        QObject::connect(&guiApp, &QApplication::aboutToQuit, [&] {
            workspaces.save(lumenis::ui::WorkspaceStore::lastUsedName(), window);
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
    }
    window.show();

    if (parser.isSet(screenshot)) {
        const QDir dir(parser.value(screenshot));
        if (!dir.mkpath(QStringLiteral("."))) {
            std::fprintf(stderr, "cannot create %s\n", qPrintable(dir.path()));
            return 1;
        }
        // Grab after the first event-loop pass so layouts have settled. The
        // menu bar is excluded: on macOS it is not part of the window.
        QTimer::singleShot(0, app.get(), [&window, dir] {
            QWidget* content = window.centralWidget() ? window.centralWidget() : &window;
            const bool ok = content->grab().save(dir.filePath(QStringLiteral("main_window.png")));
            std::printf("screenshot: %s\n", ok ? "ok" : "FAILED");
            std::fflush(stdout);
            QCoreApplication::exit(ok ? 0 : 1);
        });
        return app->exec();
    }

    if (parser.isSet(smokeTest)) {
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
    }

    return app->exec();
}
