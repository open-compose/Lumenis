#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

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
    enum class BarMode { IconAndText, IconOnly, Hidden };
    // Interface settings, "When moving to a workspace" (p.86).
    enum class MoveBehavior { LastKnownState, LastSavedState };

    static constexpr int kMaxWorkspaces = 12;

    explicit WorkspaceManager(PanelManager& panels, QObject* parent = nullptr);

    // The layout a built-in workspace starts with. Workspaces without their
    // own default use the first one set.
    void setDefaultLayout(const QString& name, const QJsonObject& layout);

    QStringList names() const;
    bool contains(const QString& name) const;
    bool isBuiltin(const QString& name) const;
    QString active() const { return active_; }

    // Windows > Workspaces > name. Applies the workspace's last known or
    // last saved layout, depending on moveBehavior().
    bool activate(const QString& name);
    // Windows > Workspaces > New Workspace: saves the current arrangement
    // (or `basedOn`'s) under a new name and switches to it.
    bool create(const QString& name, const QString& basedOn = {}, QString* error = nullptr);
    // Windows > Workspaces > Delete Workspace: deletes the active workspace.
    // Built-in workspaces cannot be deleted.
    bool canRemoveActive() const;
    bool removeActive();
    // Save Current (workspace menu, p.89).
    void saveCurrent();
    // Windows > Workspaces > Restore to Saved.
    void restoreToSaved();
    // Windows > Workspaces > Restore Current to Default: back to the default
    // of the built-in workspace it was based on.
    void restoreCurrentToDefault();

    BarMode barMode() const { return barMode_; }
    void setBarMode(BarMode mode);
    MoveBehavior moveBehavior() const { return moveBehavior_; }
    void setMoveBehavior(MoveBehavior behavior) { moveBehavior_ = behavior; }

    // User settings. load() also re-applies the layout of the last session.
    void load(QSettings& settings);
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
    void changed();

private:
    struct Workspace {
        QString name;
        QString basedOn; // for Restore Current to Default
        bool builtin = false;
        QJsonObject saved;
        QJsonObject lastKnown;
    };

    Workspace* find(const QString& name);
    const Workspace* find(const QString& name) const;
    QJsonObject defaultFor(const QString& name) const;
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
