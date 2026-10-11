#include "module_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>

#include <algorithm>

namespace lumenis::app {

namespace {

std::filesystem::path toPath(const QString& s) {
    return std::filesystem::path(s.toStdU16String());
}

} // namespace

std::vector<std::filesystem::path> moduleSearchPaths() {
    std::vector<std::filesystem::path> paths;

    const QString env = qEnvironmentVariable("LUMENIS_MODULE_PATH");
    for (const QString& entry : env.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        paths.push_back(toPath(entry));
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
#if defined(Q_OS_MACOS)
    // <bundle>.app/Contents/MacOS -> <bundle>.app/Contents/PlugIns/modules
    paths.push_back(toPath(appDir.absoluteFilePath(QStringLiteral("../PlugIns/modules"))));
#elif defined(Q_OS_WIN)
    paths.push_back(toPath(appDir.absoluteFilePath(QStringLiteral("modules"))));
#else
    // <prefix>/bin -> <prefix>/lib/<exe>/modules
    paths.push_back(toPath(appDir.absoluteFilePath(
        QStringLiteral("../lib/%1/modules").arg(QCoreApplication::applicationName()))));
#endif
    // Build tree: modules are built next to the executable.
    paths.push_back(toPath(appDir.absoluteFilePath(QStringLiteral("modules"))));

    const QString userDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!userDir.isEmpty()) {
        paths.push_back(toPath(QDir(userDir).absoluteFilePath(QStringLiteral("modules"))));
    }

    // Drop duplicates (the Windows and build-tree entries coincide), keeping order.
    std::vector<std::filesystem::path> unique;
    for (auto& p : paths) {
        auto normal = p.lexically_normal();
        if (std::find(unique.begin(), unique.end(), normal) == unique.end()) {
            unique.push_back(std::move(normal));
        }
    }
    return unique;
}

} // namespace lumenis::app
