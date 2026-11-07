#pragma once

#include "lumenis/model/project_store.h"
#include "lumenis/ui/settings.h"

#include <QDialog>
#include <QTreeWidget>
#include <QWidget>

#include <functional>
#include <optional>

class QComboBox;
class QFormLayout;
class QLineEdit;
class QMenu;
class QTabWidget;

namespace lumenis::ui {

// A setting's dialog box, made from its options (guide ch.33): check
// boxes, number boxes, menus and radio buttons, on tabs when the setting
// has several. Options not available yet are shown dimmed.
// SettingsList::openEntry() shows it; it does not change the entry itself.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // `kind` must outlive the dialog (normally it is SettingsSchema::builtIn()'s).
    // Controls start from `entry`'s values, or the defaults where it has none.
    SettingsDialog(const SettingKind& kind, const model::SettingEntry& entry, QWidget* parent = nullptr);

    // Read-only values for "info" options (General settings' Project
    // Format, p.1192).
    void setInfo(const QString& key, const QString& text);
    // The entry's values with the dialog's changes. Options not available
    // yet and "info" options keep what the entry had; a timecode that does
    // not parse at the edit rate becomes the option's default.
    QJsonObject values() const;
    // The project's edit rate, for checking timecodes (default 30 fps, when
    // no project is open). Written with ';' a timecode is drop-frame.
    void setEditRate(const Rational& rate) { editRate_ = rate; }
    // The editor of an option: a QCheckBox, QSpinBox, QComboBox, QLineEdit
    // or, for radio buttons, the QGroupBox (or QWidget) holding them.
    QWidget* editor(const QString& key) const;
    // The tabs, or nullptr when the setting has only one.
    QTabWidget* tabs() const { return tabs_; }

signals:
    // A "button" option was clicked (its key: Transcript's manageSpeakers, p.445).
    void buttonClicked(const QString& key);

private:
    const SettingKind& kind_;
    QJsonObject values_;
    QHash<QString, QWidget*> editors_;
    QTabWidget* tabs_ = nullptr;
    Rational editRate_{30};
};

// The Settings list of one scope (p.1136-1141): one row per setting, its
// versions under it when it has several, each with a check box for the
// active one; Setting and Name columns. Double-click a setting to open its
// dialog, or its Name to name it. Settings are dragged between lists to
// copy them. The list edits Settings::file(scope) directly, commits after
// each change and rebuilds itself on Settings::changed.
class SettingsList : public QTreeWidget {
    Q_OBJECT

public:
    // Item data on the Setting column: the entry's index in the file. A
    // group row (a kind with several versions) has none.
    static constexpr int kEntryRole = Qt::UserRole + 1;
    static constexpr int kSettingColumn = 0;
    static constexpr int kNameColumn = 1;
    // The drag MIME type: {"settings": [...]} as a settings file writes it.
    static QString mimeType() { return QStringLiteral("application/x-cutthulhu-settings"); }

    SettingsList(Settings& settings, model::SettingsScope scope, QWidget* parent = nullptr);
    model::SettingsScope scope() const { return scope_; }

    // Rebuilds the rows from the file, keeping expanded groups and the
    // selection.
    void refresh();
    // Entries (indices into the file) of the selected rows, ascending.
    QList<int> selectedEntries() const;
    // The row of an entry, or nullptr.
    QTreeWidgetItem* itemFor(int entry) const;
    // Selects exactly these entries' rows, expanding their groups.
    void selectEntries(const QList<int>& entries);

    // Edit > Duplicate (p.1138).
    void duplicateSelection();
    // Edit > Delete or the Delete key (p.1140). In Project and User settings
    // a kind left with no version gets a default one again.
    void deleteSelection();
    // Restore to Default (p.1140): Copy & Restore keeps a copy first.
    enum class Restore { Ask, Restore, CopyAndRestore };
    // Ask shows a message box with both choices and Cancel.
    void restoreSelection(Restore how = Restore::Ask);
    // Makes the entry the active version of its kind; out-of-range is ignored.
    void setActive(int entry);
    // Sets the entry's custom name (trimmed); out-of-range is ignored.
    void rename(int entry, const QString& name);
    // Copies settings dropped from another list (p.1141). Into Project or
    // User settings, `replace` replaces the active versions; otherwise they
    // are added. Kinds that do not belong in this scope are skipped.
    // Returns how many were copied.
    int copyIn(const QList<model::SettingEntry>& entries, bool replace);
    // Opens a setting's dialog; true if it was changed. A setting that opens
    // a tool is made active and emits toolRequested instead (returns false).
    bool openEntry(int entry);
    // A new Duplicate / Delete / Restore to Default menu for the selection,
    // parented to the list; the caller shows it.
    QMenu* contextMenu();

    // The dialog openEntry() shows, for tests: called with it before exec.
    std::function<void(SettingsDialog&)> dialogHook;
    // The project format, for General settings.
    QString projectFormat;
    // The project's edit rate, for the dialogs' timecodes; none without a
    // project (SettingsDialog::setEditRate).
    std::optional<Rational> projectEditRate;

signals:
    // The user changed the file from this list (after the commit).
    void edited();
    // A setting that opens a tool was opened (the Tool Palette).
    void toolRequested(const QString& tool);

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    model::SettingsFile& file() const { return settings_.file(scope_); }
    // Saves the scope's file (Settings::commit) and emits edited.
    void commit();
    // True for drag data in mimeType().
    bool accepts(const QMimeData* mime) const;

    Settings& settings_;
    model::SettingsScope scope_;
    bool filling_ = false; // refresh() is rebuilding the rows: ignore itemChanged
};

// File > Settings (p.1136): the Format, Project, User and Site tabs. The
// User tab has the User Profile menu (p.1143-1145). A tool window that
// MainWindow::showSettings() creates once and keeps.
class SettingsWindow : public QWidget {
    Q_OBJECT

public:
    // In tab order.
    enum class Tab { Format, Project, User, Site };

    SettingsWindow(Settings& settings, QWidget* parent = nullptr);

    // Fills the Format tab from the open project; nullopt when none is open,
    // which also disables the Project tab.
    void setProject(const std::optional<model::ProjectEntry>& project);
    void showTab(Tab tab);
    Tab currentTab() const;
    // The list of a scope's tab.
    SettingsList* list(model::SettingsScope scope) const;
    // The list on the current tab, if it has one.
    SettingsList* currentList() const;
    // The User Profile menu: the profiles, then Create, Update and Reveal.
    QComboBox* userProfiles() const { return profiles_; }
    QFormLayout* formatForm() const { return format_; }

    // The User Profile menu's commands. selectUser loads another profile's
    // settings; false if `name` is empty or already the user.
    bool selectUser(const QString& name);
    // Creates a profile and switches to it. False when settings are kept in
    // memory only, or (after a warning box) when the profile cannot be made.
    bool createUser(const QString& name);
    // Rebuilds the three lists and the User Profile menu.
    void refresh();

signals:
    // A setting that opens a tool was opened (the Tool Palette, p.392).
    void toolRequested(const QString& tool);

private:
    // Rebuilds the User Profile menu and selects the current user.
    void fillProfiles();

    Settings& settings_;
    QTabWidget* tabs_ = nullptr;
    QFormLayout* format_ = nullptr;
    QComboBox* profiles_ = nullptr;
    SettingsList* project_ = nullptr;
    SettingsList* user_ = nullptr;
    SettingsList* site_ = nullptr;
    std::optional<model::ProjectEntry> entry_;
    bool filling_ = false; // fillProfiles() is running: ignore the menu's signals
};

// Windows > Site Settings (p.1141): the site settings, which settings are
// dragged into.
class SiteSettingsWindow : public QWidget {
    Q_OBJECT

public:
    explicit SiteSettingsWindow(Settings& settings, QWidget* parent = nullptr);
    SettingsList* list() const { return list_; }

private:
    SettingsList* list_ = nullptr;
};

// Create User Profile (p.1143): the profile's default settings and name.
class CreateUserProfileDialog : public QDialog {
    Q_OBJECT

public:
    explicit CreateUserProfileDialog(QWidget* parent = nullptr);
    // The name typed, trimmed; OK is enabled only when it is not empty.
    QString profileName() const;
    QLineEdit* nameEdit() const { return name_; }

private:
    QLineEdit* name_ = nullptr;
};

} // namespace lumenis::ui
