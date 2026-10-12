#include "lumenis/ui/workspace.h"

#include <QMainWindow>
#include <QSettings>

namespace lumenis::ui {

namespace {

// Bump when panel objectNames change incompatibly; old layouts are ignored.
constexpr int kLayoutVersion = 1;

QString group(const QString& name) {
    return QStringLiteral("workspaces/") + name;
}

} // namespace

WorkspaceStore::WorkspaceStore(QSettings& settings) : settings_(settings) {}

void WorkspaceStore::save(const QString& name, const QMainWindow& window) {
    settings_.beginGroup(group(name));
    settings_.setValue(QStringLiteral("geometry"), window.saveGeometry());
    settings_.setValue(QStringLiteral("state"), window.saveState(kLayoutVersion));
    settings_.endGroup();
}

bool WorkspaceStore::restore(const QString& name, QMainWindow& window) const {
    settings_.beginGroup(group(name));
    const QByteArray geometry = settings_.value(QStringLiteral("geometry")).toByteArray();
    const QByteArray state = settings_.value(QStringLiteral("state")).toByteArray();
    settings_.endGroup();
    if (geometry.isEmpty()) {
        return false;
    }
    window.restoreGeometry(geometry);
    window.restoreState(state, kLayoutVersion);
    return true;
}

void WorkspaceStore::remove(const QString& name) {
    settings_.remove(group(name));
}

QStringList WorkspaceStore::names() const {
    settings_.beginGroup(QStringLiteral("workspaces"));
    QStringList result = settings_.childGroups();
    settings_.endGroup();
    result.removeAll(lastUsedName());
    return result;
}

} // namespace lumenis::ui
