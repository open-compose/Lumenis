#pragma once

#include "lumenis/model/locations.h"
#include "lumenis/model/project_store.h"

#include <QDialog>
#include <QHash>
#include <QList>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QWidget;

namespace lumenis::ui {

// The window shown at startup and after File > Close Project, where the user
// opens or creates a project (Media Composer Editing Guide p.61-69;
// docs/spec/ui-inventory.md §2). Accepted when a project is opened;
// rejected when the user quits. app/main.cpp shows it and then applies the
// chosen user profile (Settings::setUser) and project file. Projects are
// listed, created and deleted through model::ProjectStore; the formats come
// from projectFormatPresets() (src/core/.../project_format.h).
class SelectProjectDialog : public QDialog {
    Q_OBJECT

public:
    // Where projects are looked for: the Private and Shared folders of
    // model::Locations, or an External folder chosen with the browse button.
    enum class Location { Private, Shared, External };
    enum class Page { Projects, NewProject, WhatsNew };

    // Lists the user profiles in locations.users, creating one named after
    // the login on a first run, and the projects in the Private folder.
    explicit SelectProjectDialog(const model::Locations& locations, QWidget* parent = nullptr);

    // The project chosen with Open (valid after accept()).
    QString selectedProjectFile() const { return selectedFile_; }
    // The name chosen in the User Profile menu.
    QString userProfile() const;

    // Chooses a location in the menu, which lists its projects; ignored for
    // External until setExternalFolder() has given one.
    void setLocation(Location location);
    // Adds (or replaces) the External location, chooses it and lists it.
    void setExternalFolder(const QString& folder);
    Location location() const;
    // The folder of the chosen location.
    QString currentFolder() const;
    // Shows a page and checks its tab and sidebar button; the Projects and
    // New Project tabs are disabled on What's New.
    void showPage(Page page);
    Page page() const;

    // Selects a project in the list by name; false if it is not listed.
    bool selectProject(const QString& name);
    // The listed project names, in the list's sort order.
    QStringList listedProjects() const;

    // Creates a project from the New Project page's fields, then returns to
    // the Projects page with it selected (guide p.67). False, with the
    // reason shown on the page, if the name or settings are invalid.
    bool createProjectFromForm();
    // Deletes the selected project and its bins (guide p.69). With askFirst a
    // confirmation box comes first. False if nothing is selected, the user
    // cancels, or the store fails (shown in a warning box).
    bool deleteSelectedProject(bool askFirst = true);

    // Overrides how a location's folder is shown (for example to show a
    // stable path in screenshots). Empty restores the real path.
    void setLocationLabel(Location location, const QString& text);

    // Used by tests: Quit leaves without the Leave/Cancel question.
    void setConfirmQuit(bool confirm) { confirmQuit_ = confirm; }

public slots:
    // Re-reads the projects of the current folder into both views, keeping
    // the selected project if it is still there.
    void refresh();

protected:
    // Delete or Backspace in the project list deletes the selected project.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* buildSidebar();
    QWidget* buildProjectsPage();
    QWidget* buildNewProjectPage();
    QWidget* buildWhatsNewPage();
    void populateUsers();
    // Rebuilds the Location menu (labels may be overridden), keeping the
    // current choice where it still exists, without emitting the menu's
    // signals.
    void populateLocations();
    void populateFormats();
    // Updates the New Project fields that depend on the Format menu and
    // Choose for me: raster, frame rates, aspect, colour spaces, film.
    void updateFormatDependentFields();
    // Enables Film only where the chosen format and frame rate offer it
    // (p.67), unchecking it otherwise; called by the above and when the
    // frame rate changes.
    void updateFilmFields();
    // Accepts the dialog with the selected project, if any.
    void openSelected();
    // Quit, after the Leave/Cancel question unless setConfirmQuit(false).
    void quit();
    // The New Project form as project settings; on a problem sets *error
    // (when not null) and the result is incomplete.
    model::ProjectSettings settingsFromForm(QString* error) const;
    // The preset matching the Format menu and frame rate, a Custom format
    // from the raster boxes, or nullopt when the pair matches no preset.
    std::optional<ProjectFormat> chosenFormat() const;

    model::Locations locations_;
    QString externalFolder_;
    QHash<int, QString> locationLabels_; // Location -> shown folder text (setLocationLabel)
    QString selectedFile_;
    QList<model::ProjectEntry> entries_; // the projects listed, from the last refresh()
    bool confirmQuit_ = true;

    QLabel* userTitle_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QButtonGroup* tabs_ = nullptr;
    QButtonGroup* sidebar_ = nullptr;
    QComboBox* users_ = nullptr;
    QComboBox* location_ = nullptr; // item data: the Location as int

    // Projects page
    QStackedWidget* projectViews_ = nullptr; // text view (table_) or frame view (frames_)
    QTreeWidget* table_ = nullptr; // Name column's Qt::UserRole: the project file
    QListWidget* frames_ = nullptr;
    QSlider* frameSize_ = nullptr;
    QPushButton* open_ = nullptr;

    // New Project page
    QLineEdit* name_ = nullptr;
    QToolButton* settingsToggle_ = nullptr;
    QCheckBox* chooseForMe_ = nullptr;
    QWidget* settingsPanel_ = nullptr;
    QComboBox* format_ = nullptr;
    QSpinBox* rasterWidth_ = nullptr;
    QSpinBox* rasterHeight_ = nullptr;
    QComboBox* aspect_ = nullptr;
    QComboBox* frameRate_ = nullptr;
    QComboBox* colorSpace_ = nullptr;
    QComboBox* colorDepth_ = nullptr;
    QComboBox* stereoscopic_ = nullptr;
    QComboBox* languageHint_ = nullptr; // the Transcript setting's Language Hint (p.448)
    QCheckBox* film_ = nullptr;
    QComboBox* filmType_ = nullptr;
    QLabel* formError_ = nullptr;
};

} // namespace lumenis::ui
