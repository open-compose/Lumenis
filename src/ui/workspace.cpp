#include "lumenis/ui/workspace.h"

#include "lumenis/ui/panels.h"

#include <QJsonDocument>
#include <QSettings>

#include <algorithm>

// WorkspaceManager (workspace.h): named panel layouts. Each workspace keeps two PanelManager layouts:
// `saved` (Save Current) and `lastKnown` (how it was when the user last left it). Which one switching
// back uses depends on the "When moving to a workspace" option. With neither, a workspace falls back to the
// default layout of the built-in workspace it is based on; MainWindow sets those defaults
// (setDefaultLayout) and stores the workspaces as Workspace View user settings. The toolbar and the New
// Workspace dialog are in workspace_bar.cpp; the Windows > Workspaces menu is built in main_window.cpp.

namespace lumenis::ui {

namespace {

// Names as on the workspace toolbar (p.87-89). The guide's text calls the
// second one "Color Correction" (p.85); the 2025 toolbar shows "COLOR".
const QStringList kBuiltins = {QStringLiteral("Edit"), QStringLiteral("Color"), QStringLiteral("Effects"),
                               QStringLiteral("Audio")};

} // namespace

const QStringList& WorkspaceManager::builtinNames() { return kBuiltins; }

namespace {

// Bump when the stored form changes incompatibly; older data is ignored.
constexpr int kStoreVersion = 1;
constexpr auto kGroup = "workspaces";

// The built-in workspace's spelling of a name compared case-insensitively, as find() does; empty for
// a user workspace.
QString builtinName(const QString& name) {
    for (const QString& b : kBuiltins) {
        if (b.compare(name, Qt::CaseInsensitive) == 0) return b;
    }
    return {};
}

// Layouts are stored in QSettings as compact JSON text; an empty layout as an empty string.
QString toText(const QJsonObject& o) {
    return o.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonObject fromText(const QString& text) {
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

} // namespace

WorkspaceManager::WorkspaceManager(PanelManager& panels, QObject* parent) : QObject(parent), panels_(panels) {
    for (const QString& name : kBuiltins) {
        workspaces_.push_back({name, {}, true, {}, {}});
    }
    active_ = kBuiltins.first();
}

void WorkspaceManager::setDefaultLayout(const QString& name, const QJsonObject& layout) {
    if (firstDefault_.isEmpty()) firstDefault_ = name;
    defaults_.insert(name, layout);
}

QStringList WorkspaceManager::names() const {
    QStringList out;
    for (const Workspace& w : workspaces_) out << w.name;
    return out;
}

bool WorkspaceManager::contains(const QString& name) const {
    return find(name) != nullptr;
}

bool WorkspaceManager::isBuiltin(const QString& name) const {
    const Workspace* w = find(name);
    return w && w->builtin;
}

WorkspaceManager::Workspace* WorkspaceManager::find(const QString& name) {
    for (Workspace& w : workspaces_) {
        if (w.name.compare(name, Qt::CaseInsensitive) == 0) return &w;
    }
    return nullptr;
}

const WorkspaceManager::Workspace* WorkspaceManager::find(const QString& name) const {
    return const_cast<WorkspaceManager*>(this)->find(name);
}

// The guard stops a cycle of "based on" names.
QJsonObject WorkspaceManager::defaultFor(const QString& name) const {
    // Follow "based on" back to a built-in workspace.
    QString n = name;
    for (int guard = 0; guard < kMaxWorkspaces; ++guard) {
        const Workspace* w = find(n);
        if (!w || w->builtin || w->basedOn.isEmpty()) break;
        n = w->basedOn;
    }
    if (defaults_.contains(n)) return defaults_.value(n);
    return defaults_.value(firstDefault_);
}

void WorkspaceManager::apply(const QJsonObject& layout) {
    panels_.restoreLayout(layout);
    if (restoreExtras_) restoreExtras_(layout.value(QStringLiteral("extras")).toObject());
}

QJsonObject WorkspaceManager::capture() const {
    QJsonObject layout = panels_.saveLayout();
    if (saveExtras_) {
        if (const QJsonObject extras = saveExtras_(); !extras.isEmpty()) layout.insert(QStringLiteral("extras"), extras);
    }
    return layout;
}

// The layout used is the first that exists of: last known (with Last Known State), saved, default.
bool WorkspaceManager::activate(const QString& name) {
    Workspace* target = find(name);
    if (!target) return false;
    if (target->name == active_) return true;
    if (Workspace* current = find(active_)) {
        current->lastKnown = capture();
    }
    QJsonObject layout = moveBehavior_ == MoveBehavior::LastKnownState ? target->lastKnown : QJsonObject();
    if (layout.isEmpty()) layout = target->saved;
    if (layout.isEmpty()) layout = defaultFor(target->name);
    active_ = target->name;
    apply(layout);
    emit changed();
    return true;
}

// Without `basedOn`, the new workspace saves the current arrangement and the screen does not change; it
// records the active workspace as what it is based on. With `basedOn`, it takes that workspace's saved
// layout (or default) and applies it.
bool WorkspaceManager::create(const QString& requested, const QString& basedOn, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    const QString name = requested.trimmed();
    if (name.isEmpty()) return fail(tr("Type a name for the workspace."));
    if (find(name)) return fail(tr("A workspace named \"%1\" already exists.").arg(name));
    if (static_cast<int>(workspaces_.size()) >= kMaxWorkspaces) {
        return fail(tr("You can have up to %1 workspaces.").arg(kMaxWorkspaces));
    }
    const Workspace* base = basedOn.isEmpty() ? nullptr : find(basedOn);
    if (!basedOn.isEmpty() && !base) return fail(tr("There is no workspace named \"%1\".").arg(basedOn));

    Workspace w;
    w.name = name;
    w.basedOn = base ? base->name : active_;
    if (base) {
        w.saved = base->saved.isEmpty() ? defaultFor(base->name) : base->saved;
    } else {
        w.saved = capture();
    }
    if (Workspace* current = find(active_)) current->lastKnown = capture();
    workspaces_.push_back(w);
    active_ = name;
    if (base) apply(w.saved);
    emit changed();
    return true;
}

bool WorkspaceManager::canRemoveActive() const {
    const Workspace* w = find(active_);
    return w && !w->builtin;
}

bool WorkspaceManager::removeActive() {
    if (!canRemoveActive()) return false;
    const QString removed = active_;
    std::erase_if(workspaces_, [&](const Workspace& w) { return w.name == removed; });
    // Back to the first workspace, without recording the deleted one.
    active_.clear();
    Workspace& first = workspaces_.front();
    QJsonObject layout = moveBehavior_ == MoveBehavior::LastKnownState ? first.lastKnown : QJsonObject();
    if (layout.isEmpty()) layout = first.saved.isEmpty() ? defaultFor(first.name) : first.saved;
    active_ = first.name;
    apply(layout);
    emit changed();
    return true;
}

// Also makes it the last known layout, so switching back shows what was saved.
void WorkspaceManager::saveCurrent() {
    if (Workspace* w = find(active_)) {
        w->saved = capture();
        w->lastKnown = w->saved;
        emit changed();
    }
}

// Forgets the last known layout so the saved one is used next time too.
void WorkspaceManager::restoreToSaved() {
    if (Workspace* w = find(active_)) {
        const QJsonObject layout = w->saved.isEmpty() ? defaultFor(w->name) : w->saved;
        w->lastKnown = {};
        apply(layout);
        emit changed();
    }
}

// Forgets both layouts, so the workspace follows its default from now on.
void WorkspaceManager::restoreCurrentToDefault() {
    if (Workspace* w = find(active_)) {
        w->saved = {};
        w->lastKnown = {};
        apply(defaultFor(w->name));
        emit changed();
    }
}

void WorkspaceManager::setBarMode(BarMode mode) {
    if (barMode_ == mode) return;
    barMode_ = mode;
    emit changed();
}

// Not const: it first records the current arrangement as the active workspace's last known layout.
QList<WorkspaceManager::Entry> WorkspaceManager::entries() {
    if (Workspace* w = find(active_)) w->lastKnown = capture();
    QList<Entry> out;
    for (const Workspace& w : workspaces_) out << Entry{w.name, w.basedOn, w.saved, w.lastKnown};
    return out;
}

// Names are trimmed; entries with an empty or repeated name (case-insensitive, as find() compares) and
// those past the 12th (built-ins included) are dropped. A name that is a built-in's in another case is that built-in, with its
// spelling. Missing built-in workspaces are put back near their usual places.
std::vector<WorkspaceManager::Workspace> WorkspaceManager::tidied(const std::vector<Workspace>& workspaces) {
    std::vector<Workspace> out;
    for (Workspace w : workspaces) {
        w.name = w.name.trimmed();
        if (w.name.isEmpty() || static_cast<int>(out.size()) >= kMaxWorkspaces) continue;
        if (std::any_of(out.begin(), out.end(),
                        [&](const Workspace& o) { return o.name.compare(w.name, Qt::CaseInsensitive) == 0; })) {
            continue;
        }
        const QString builtin = builtinName(w.name);
        w.builtin = !builtin.isEmpty();
        if (w.builtin) w.name = builtin;
        out.push_back(std::move(w));
    }
    for (const QString& name : kBuiltins) {
        const bool present = std::any_of(out.begin(), out.end(), [&](const Workspace& w) { return w.name == name; });
        if (!present) {
            const auto at = std::min<std::ptrdiff_t>(kBuiltins.indexOf(name), static_cast<std::ptrdiff_t>(out.size()));
            out.insert(out.begin() + at, Workspace{name, {}, true, {}, {}});
        }
    }
    // Put-back built-ins count towards the 12: the last user workspaces make room for them.
    for (auto it = out.end(); static_cast<int>(out.size()) > kMaxWorkspaces && it != out.begin();) {
        --it;
        if (!it->builtin) it = out.erase(it);
    }
    return out;
}

// An unknown `active` picks the first workspace.
void WorkspaceManager::setEntries(const QList<Entry>& entries, const QString& active) {
    std::vector<Workspace> loaded;
    for (const Entry& e : entries) loaded.push_back({e.name, e.basedOn, false, e.saved, e.lastKnown});
    workspaces_ = tidied(loaded);
    active_ = find(active) ? find(active)->name : workspaces_.front().name;
    const Workspace* w = find(active_);
    QJsonObject layout = w->lastKnown;
    if (layout.isEmpty()) layout = w->saved;
    if (layout.isEmpty()) layout = defaultFor(w->name);
    apply(layout);
    emit changed();
}

// Even when nothing valid is stored, the active workspace's layout is applied.
void WorkspaceManager::load(QSettings& settings) {
    settings.beginGroup(QLatin1String(kGroup));
    if (settings.value(QStringLiteral("version")).toInt() == kStoreVersion) {
        std::vector<Workspace> loaded;
        const int n = settings.beginReadArray(QStringLiteral("list"));
        for (int i = 0; i < n; ++i) {
            settings.setArrayIndex(i);
            Workspace w;
            w.name = settings.value(QStringLiteral("name")).toString();
            w.basedOn = settings.value(QStringLiteral("basedOn")).toString();
            w.saved = fromText(settings.value(QStringLiteral("saved")).toString());
            w.lastKnown = fromText(settings.value(QStringLiteral("lastKnown")).toString());
            loaded.push_back(w);
        }
        settings.endArray();
        // The same rules as setEntries(); the built-in workspaces always exist.
        workspaces_ = tidied(loaded);
        const int bar = settings.value(QStringLiteral("barMode"), 0).toInt();
        barMode_ = bar == 1 ? BarMode::IconOnly : bar == 2 ? BarMode::Hidden : BarMode::IconAndText;
        moveBehavior_ = settings.value(QStringLiteral("moveBehavior"), 0).toInt() == 1 ? MoveBehavior::LastSavedState
                                                                                        : MoveBehavior::LastKnownState;
        panels_.setBlankPanelOnClose(settings.value(QStringLiteral("blankPanelOnClose"), false).toBool());
        const QString active = settings.value(QStringLiteral("active")).toString();
        active_ = find(active) ? find(active)->name : workspaces_.front().name;
    }
    settings.endGroup();

    // Where the last session left off.
    const Workspace* w = find(active_);
    QJsonObject layout = w->lastKnown;
    if (layout.isEmpty()) layout = w->saved;
    if (layout.isEmpty()) layout = defaultFor(w->name);
    apply(layout);
    emit changed();
}

void WorkspaceManager::save(QSettings& settings) {
    if (Workspace* w = find(active_)) w->lastKnown = capture();
    settings.remove(QLatin1String(kGroup));
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QStringLiteral("version"), kStoreVersion);
    settings.beginWriteArray(QStringLiteral("list"), static_cast<int>(workspaces_.size()));
    for (int i = 0; i < static_cast<int>(workspaces_.size()); ++i) {
        const Workspace& w = workspaces_[static_cast<std::size_t>(i)];
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), w.name);
        settings.setValue(QStringLiteral("basedOn"), w.basedOn);
        settings.setValue(QStringLiteral("saved"), toText(w.saved));
        settings.setValue(QStringLiteral("lastKnown"), toText(w.lastKnown));
    }
    settings.endArray();
    settings.setValue(QStringLiteral("active"), active_);
    settings.setValue(QStringLiteral("barMode"), static_cast<int>(barMode_));
    settings.setValue(QStringLiteral("moveBehavior"), static_cast<int>(moveBehavior_));
    settings.setValue(QStringLiteral("blankPanelOnClose"), panels_.blankPanelOnClose());
    settings.endGroup();
}

} // namespace lumenis::ui
