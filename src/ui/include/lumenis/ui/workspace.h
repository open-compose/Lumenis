#pragma once

#include <QString>
#include <QStringList>

class QMainWindow;
class QSettings;

namespace lumenis::ui {

// Named window layouts ("workspaces"): the main window's geometry plus the
// position, size and visibility of every docked or floating panel. Panels
// must have unique objectNames for their layout to be saved.
class WorkspaceStore {
public:
    explicit WorkspaceStore(QSettings& settings);

    void save(const QString& name, const QMainWindow& window);
    // Returns false if no workspace of that name exists.
    bool restore(const QString& name, QMainWindow& window) const;
    void remove(const QString& name);
    QStringList names() const;

    // Restored at startup and saved on exit.
    static QString lastUsedName() { return QStringLiteral("__last_session"); }

private:
    QSettings& settings_;
};

} // namespace lumenis::ui
