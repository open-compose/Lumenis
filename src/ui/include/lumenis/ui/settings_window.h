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
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(const SettingKind& kind, const model::SettingEntry& entry, QWidget* parent = nullptr);

    // Read-only values for "info" options (General settings' Project
    // Format, p.1192).
    void setInfo(const QString& key, const QString& text);
    // The entry's values with the dialog's changes.
    QJsonObject values() const;
    // The editor of an option: a QCheckBox, QSpinBox, QComboBox, QLineEdit
    // or, for radio buttons, the QGroupBox (or QWidget) holding them.
    QWidget* editor(const QString& key) const;
    QTabWidget* tabs() const { return tabs_; }

private:
    const SettingKind& kind_;
    QJsonObject values_;
    QHash<QString, QWidget*> editors_;
    QTabWidget* tabs_ = nullptr;
};

// The Settings list of one scope (p.1136-1141): one row per setting, its
// versions under it when it has several, each with a check box for the
// active one; Setting and Name columns. Double-click a setting to open its
// dialog, or its Name to name it. Settings are dragged between lists to
// copy them.
class SettingsList : public QTreeWidget {
    Q_OBJECT

public:
    static constexpr int kEntryRole = Qt::UserRole + 1;
    static constexpr int kSettingColumn = 0;
    static constexpr int kNameColumn = 1;
    static QString mimeType() { return QStringLiteral("application/x-cutthulhu-settings"); }

    SettingsList(Settings& settings, model::SettingsScope scope, QWidget* parent = nullptr);
    model::SettingsScope scope() const { return scope_; }

    void refresh();
    // Entries (indices into the file) of the selected rows.
    QList<int> selectedEntries() const;
    QTreeWidgetItem* itemFor(int entry) const;
    void selectEntries(const QList<int>& entries);

    // Edit > Duplicate (p.1138).
    void duplicateSelection();
    // Edit > Delete or the Delete key (p.1140).
    void deleteSelection();
    // Restore to Default (p.1140): Copy & Restore keeps a copy first.
    enum class Restore { Ask, Restore, CopyAndRestore };
    void restoreSelection(Restore how = Restore::Ask);
    void setActive(int entry);
    void rename(int entry, const QString& name);
    // Copies settings dropped from another list (p.1141). Into Project or
    // User settings, `replace` replaces the active versions; otherwise they
    // are added. Kinds that do not belong in this scope are skipped.
    // Returns how many were copied.
    int copyIn(const QList<model::SettingEntry>& entries, bool replace);
    // Opens a setting's dialog; true if it was changed.
    bool openEntry(int entry);
    QMenu* contextMenu();

    // The dialog openEntry() shows, for tests: called with it before exec.
    std::function<void(SettingsDialog&)> dialogHook;
    // The project format, for General settings.
    QString projectFormat;

signals:
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
    void commit();
    bool accepts(const QMimeData* mime) const;

    Settings& settings_;
    model::SettingsScope scope_;
    bool filling_ = false;
};

// File > Settings (p.1136): the Format, Project, User and Site tabs. The
// User tab has the User Profile menu (p.1143-1145).
class SettingsWindow : public QWidget {
    Q_OBJECT

public:
    enum class Tab { Format, Project, User, Site };

    SettingsWindow(Settings& settings, QWidget* parent = nullptr);

    void setProject(const std::optional<model::ProjectEntry>& project);
    void showTab(Tab tab);
    Tab currentTab() const;
    SettingsList* list(model::SettingsScope scope) const;
    // The list on the current tab, if it has one.
    SettingsList* currentList() const;
    QComboBox* userProfiles() const { return profiles_; }
    QFormLayout* formatForm() const { return format_; }

    // The User Profile menu's commands.
    bool selectUser(const QString& name);
    bool createUser(const QString& name);
    void refresh();

signals:
    // A setting that opens a tool was opened (the Tool Palette, p.392).
    void toolRequested(const QString& tool);

private:
    void fillProfiles();

    Settings& settings_;
    QTabWidget* tabs_ = nullptr;
    QFormLayout* format_ = nullptr;
    QComboBox* profiles_ = nullptr;
    SettingsList* project_ = nullptr;
    SettingsList* user_ = nullptr;
    SettingsList* site_ = nullptr;
    std::optional<model::ProjectEntry> entry_;
    bool filling_ = false;
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
    QString profileName() const;
    QLineEdit* nameEdit() const { return name_; }

private:
    QLineEdit* name_ = nullptr;
};

} // namespace lumenis::ui
