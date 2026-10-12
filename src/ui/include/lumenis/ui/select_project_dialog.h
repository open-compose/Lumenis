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
// rejected when the user quits.
class SelectProjectDialog : public QDialog {
    Q_OBJECT

public:
    enum class Location { Private, Shared, External };
    enum class Page { Projects, NewProject, WhatsNew };

    explicit SelectProjectDialog(const model::Locations& locations, QWidget* parent = nullptr);

    // The project chosen with Open (valid after accept()).
    QString selectedProjectFile() const { return selectedFile_; }
    QString userProfile() const;

    void setLocation(Location location);
    void setExternalFolder(const QString& folder);
    Location location() const;
    QString currentFolder() const;
    void showPage(Page page);
    Page page() const;

    // Selects a project in the list by name; false if it is not listed.
    bool selectProject(const QString& name);
    QStringList listedProjects() const;

    // Creates a project from the New Project page's fields, then returns to
    // the Projects page with it selected (guide p.67). False, with the
    // reason shown on the page, if the name or settings are invalid.
    bool createProjectFromForm();
    // Deletes the selected project and its bins (guide p.69).
    bool deleteSelectedProject(bool askFirst = true);

    // Overrides how a location's folder is shown (for example to show a
    // stable path in screenshots). Empty restores the real path.
    void setLocationLabel(Location location, const QString& text);

    // Used by tests: Quit leaves without the Leave/Cancel question.
    void setConfirmQuit(bool confirm) { confirmQuit_ = confirm; }

public slots:
    void refresh();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* buildSidebar();
    QWidget* buildProjectsPage();
    QWidget* buildNewProjectPage();
    QWidget* buildWhatsNewPage();
    void populateUsers();
    void populateLocations();
    void populateFormats();
    void updateFormatDependentFields();
    void openSelected();
    void quit();
    model::ProjectSettings settingsFromForm(QString* error) const;
    std::optional<ProjectFormat> chosenFormat() const;

    model::Locations locations_;
    QString externalFolder_;
    QHash<int, QString> locationLabels_;
    QString selectedFile_;
    QList<model::ProjectEntry> entries_;
    bool confirmQuit_ = true;

    QLabel* userTitle_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QButtonGroup* tabs_ = nullptr;
    QButtonGroup* sidebar_ = nullptr;
    QComboBox* users_ = nullptr;
    QComboBox* location_ = nullptr;

    // Projects page
    QStackedWidget* projectViews_ = nullptr;
    QTreeWidget* table_ = nullptr;
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
    QCheckBox* film_ = nullptr;
    QComboBox* filmType_ = nullptr;
    QLabel* formError_ = nullptr;
};

} // namespace lumenis::ui
