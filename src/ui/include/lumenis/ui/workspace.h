#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QSettings;

namespace lumenis::ui {

class PanelManager;

// Workspaces (guide p.85-89): named arrangements of windows, recalled from
// Windows > Workspaces or the workspace toolbar. The defaults are Edit,
// Color, Effects and Audio; a user can have up to 12. Each workspace keeps
// the layout last saved for it and the layout it was last left in.
class WorkspaceManager : public QObject {
    Q_OBJECT

public:
    // How the workspace toolbar (WorkspaceBar) is shown; stored as 0, 1, 2.
    enum class BarMode { IconAndText, IconOnly, Hidden };
    // Interface settings, "When moving to a workspace" (p.86).
    enum class MoveBehavior { LastKnownState, LastSavedState };

    static constexpr int kMaxWorkspaces = 12;
    // The built-in workspaces' names (Edit, Color, Effects, Audio), as stored; shown through
    // translatedResource("workspaces", name) (names.json).
    static const QStringList& builtinNames();

    // Starts with the built-in workspaces, Edit active. A workspace's layout is a PanelManager::saveLayout()
    // object, applied with restoreLayout().
    explicit WorkspaceManager(PanelManager& panels, QObject* parent = nullptr);

    // What a layout keeps besides the panels: `save` gives an object stored under "extras" each time a
    // layout is taken, and `restore` gets it back (an empty object when the layout has none) each time
    // one is applied. MainWindow keeps single floating Bin Containers this way (Bin settings, p.1153).
    void setExtras(std::function<QJsonObject()> save, std::function<void(const QJsonObject&)> restore) {
        saveExtras_ = std::move(save);
        restoreExtras_ = std::move(restore);
    }

    // The layout a built-in workspace starts with. Workspaces without their
    // own default use the first one set.
    void setDefaultLayout(const QString& name, const QJsonObject& layout);

    // In order: as created, or as loaded.
    QStringList names() const;
    // Names compare case-insensitively.
    bool contains(const QString& name) const;
    bool isBuiltin(const QString& name) const;
    // The active workspace's name, as stored (not as the caller spelled it).
    QString active() const { return active_; }

    // Windows > Workspaces > name. Applies the workspace's last known or
    // last saved layout, depending on moveBehavior().
    // Records the current arrangement as the old workspace's last known layout first. Returns false for an
    // unknown name; true (doing nothing) for the active one. Emits changed().
    bool activate(const QString& name);
    // Windows > Workspaces > New Workspace: saves the current arrangement
    // (or `basedOn`'s) under a new name and switches to it.
    // Returns false with a message in *error for an empty or existing name, a 13th workspace or an unknown
    // `basedOn`. Emits changed().
    bool create(const QString& name, const QString& basedOn = {}, QString* error = nullptr);
    // Windows > Workspaces > Delete Workspace: deletes the active workspace.
    // Built-in workspaces cannot be deleted.
    bool canRemoveActive() const;
    // Switches to the first workspace afterwards. Emits changed().
    bool removeActive();
    // Save Current (workspace menu, p.89).
    void saveCurrent();
    // Windows > Workspaces > Restore to Saved.
    void restoreToSaved();
    // Windows > Workspaces > Restore Current to Default: back to the default
    // of the built-in workspace it was based on.
    void restoreCurrentToDefault();

    BarMode barMode() const { return barMode_; }
    // Emits changed() when the mode differs.
    void setBarMode(BarMode mode);
    MoveBehavior moveBehavior() const { return moveBehavior_; }
    // Takes effect on the next activate(); emits nothing.
    void setMoveBehavior(MoveBehavior behavior) { moveBehavior_ = behavior; }

    // User settings. load() also re-applies the layout of the last session.
    // These use the "workspaces" group of a QSettings file and also keep the bar mode, the move behaviour
    // and the PanelManager's blank-panel-on-close option; data of another store version is ignored. The
    // application itself keeps workspaces as User settings through entries() and setEntries()
    // (main_window.cpp); the tests use load() and save().
    void load(QSettings& settings);
    // Replaces the whole group.
    void save(QSettings& settings);

    // The workspaces as kept in the user's settings: one Workspace View
    // setting each (p.89-90, picture on p.1136).
    struct Entry {
        QString name;
        QString basedOn;
        QJsonObject saved;
        QJsonObject lastKnown;
    };
    // The active workspace's last known layout is the current one.
    QList<Entry> entries();
    // Replaces the workspaces (built-in ones always exist) and shows the
    // active one where it was left.
    void setEntries(const QList<Entry>& entries, const QString& active);

signals:
    // Fires after any change to the list, the active workspace or the bar mode, and after a layout is applied.
    void changed();

private:
    // The panels' layout with the extras.
    QJsonObject capture() const;
    std::function<QJsonObject()> saveExtras_;
    std::function<void(const QJsonObject&)> restoreExtras_;
    struct Workspace {
        QString name;
        QString basedOn; // for Restore Current to Default
        bool builtin = false;
        QJsonObject saved;
        QJsonObject lastKnown;
    };

    // The list load() and setEntries() keep: trimmed, unique names
    // (case-insensitive), at most 12, built-in ones always there.
    static std::vector<Workspace> tidied(const std::vector<Workspace>& workspaces);
    // Case-insensitive; null if there is none.
    Workspace* find(const QString& name);
    const Workspace* find(const QString& name) const;
    // The default layout of the built-in workspace `name` is based on, or the first default set.
    QJsonObject defaultFor(const QString& name) const;
    // Restores `layout` in the PanelManager.
    void apply(const QJsonObject& layout);

    PanelManager& panels_;
    std::vector<Workspace> workspaces_;
    QHash<QString, QJsonObject> defaults_;
    QString firstDefault_;
    QString active_;
    BarMode barMode_ = BarMode::IconAndText;
    MoveBehavior moveBehavior_ = MoveBehavior::LastKnownState;
};

} // namespace lumenis::ui
