#include "lumenis/model/locations.h"

#include "lumenis/core/branding.h"

#include <QDir>
#include <QStandardPaths>

// The default project, user and site settings folders (locations.h). The Select Project window
// (src/ui/select_project_dialog.*) lists projects and users from them; tests pass their own.
namespace lumenis::model {

Locations Locations::defaults() {
    const QString product = QString::fromUtf8(branding::kProductName);
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    Locations l;
    l.privateProjects = QDir(documents).filePath(product + QStringLiteral(" Projects"));
#if defined(Q_OS_WIN)
    QString publicDir = qEnvironmentVariable("PUBLIC");
    if (publicDir.isEmpty()) {
        publicDir = QStringLiteral("C:/Users/Public");
    }
    const QDir publicDocs(QDir(publicDir).filePath(QStringLiteral("Documents")));
    l.sharedProjects = publicDocs.filePath(QStringLiteral("Shared %1 Projects").arg(product));
    l.users = publicDocs.filePath(product + QStringLiteral("/Users"));
#elif defined(Q_OS_MACOS)
    const QDir shared(QStringLiteral("/Users/Shared/") + product);
    l.sharedProjects = shared.filePath(QStringLiteral("Shared %1 Projects").arg(product));
    l.users = shared.filePath(QStringLiteral("Users"));
#else
    l.sharedProjects = QStringLiteral("/srv/%1/Shared %1 Projects").arg(product);
    l.users = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("Users"));
#endif
    l.privateProjects = QDir::cleanPath(l.privateProjects);
    l.sharedProjects = QDir::cleanPath(l.sharedProjects);
    l.users = QDir::cleanPath(l.users);
    l.siteSettings = QDir::cleanPath(QDir(l.users).filePath(QStringLiteral("../Settings")));
    return l;
}

} // namespace lumenis::model
