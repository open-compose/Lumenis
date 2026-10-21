#include "lumenis/model/project_store.h"
#include "lumenis/ui/select_project_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using lumenis::ui::SelectProjectDialog;

namespace {

struct Fixture {
    QTemporaryDir dir;
    model::Locations locations;

    Fixture() {
        locations.privateProjects = dir.filePath("Private");
        locations.sharedProjects = dir.filePath("Shared");
        locations.users = dir.filePath("Users");
    }

    void addProject(const QString& folderRoot, const QString& name) {
        model::ProjectSettings s;
        s.name = name;
        s.format = projectFormatPresets().front();
        QVERIFY(model::ProjectStore(folderRoot).create(s));
    }
};

template <typename T>
T* child(QWidget& w, const char* name) {
    T* c = w.findChild<T*>(QString::fromLatin1(name));
    if (!c) qFatal("no child %s", name);
    return c;
}

void chooseFormat(SelectProjectDialog& d, const QString& label) {
    auto* format = child<QComboBox>(d, "format");
    const int i = format->findText(label);
    if (i < 0) qFatal("no format %s", qPrintable(label));
    format->setCurrentIndex(i);
}

} // namespace

class TestSelectProjectDialog : public QObject {
    Q_OBJECT

private slots:
    void listsProjectsOfTheChosenLocation() {
        Fixture fx;
        fx.addProject(fx.locations.privateProjects, QStringLiteral("Zebra"));
        fx.addProject(fx.locations.privateProjects, QStringLiteral("apple"));
        fx.addProject(fx.locations.sharedProjects, QStringLiteral("Team Show"));

        SelectProjectDialog d(fx.locations);
        QCOMPARE(d.location(), SelectProjectDialog::Location::Private);
        QCOMPARE(d.listedProjects(), (QStringList{"apple", "Zebra"}));
        d.setLocation(SelectProjectDialog::Location::Shared);
        QCOMPARE(d.listedProjects(), QStringList{"Team Show"});

        QTemporaryDir external;
        fx.addProject(external.path(), QStringLiteral("Archive"));
        d.setExternalFolder(external.path());
        QCOMPARE(d.location(), SelectProjectDialog::Location::External);
        QCOMPARE(d.listedProjects(), QStringList{"Archive"});
    }

    void firstRunCreatesAUserProfile() {
        Fixture fx;
        SelectProjectDialog d(fx.locations);
        QVERIFY(!d.userProfile().isEmpty());
        QCOMPARE(model::UserProfiles(fx.locations.users).list(), QStringList{d.userProfile()});
        QCOMPARE(child<QLabel>(d, "userTitle")->text(), d.userProfile());
    }

    void createsAProjectAndSelectsIt() {
        Fixture fx;
        SelectProjectDialog d(fx.locations);
        d.show();
        QVERIFY(QTest::qWaitForWindowExposed(&d));
        d.showPage(SelectProjectDialog::Page::NewProject);
        child<QLineEdit>(d, "projectName")->setText(QStringLiteral("Pilot"));
        chooseFormat(d, QStringLiteral("1080i  (1920 x 1080)"));
        child<QComboBox>(d, "frameRate")->setCurrentText(QStringLiteral("29.97"));
        child<QComboBox>(d, "colorSpace")->setCurrentText(QStringLiteral("RGB 709"));
        QTest::mouseClick(child<QPushButton>(d, "createButton"), Qt::LeftButton);

        QVERIFY2(d.page() == SelectProjectDialog::Page::Projects, qPrintable(child<QLabel>(d, "formError")->text()));
        QCOMPARE(d.listedProjects(), QStringList{"Pilot"});
        const auto entries = model::ProjectStore(fx.locations.privateProjects).list();
        QCOMPARE(entries.size(), 1);
        const model::ProjectSettings& s = entries.front().settings;
        QCOMPARE(s.format.label(), std::string("1080i/59.94"));
        QCOMPARE(s.format.editRate, Rational(30000, 1001));
        QCOMPARE(s.colorSpace, QStringLiteral("RGB 709"));
        QCOMPARE(s.aspect, Rational(16, 9));
    }

    void reportsInvalidInputAndStaysOnTheForm() {
        Fixture fx;
        SelectProjectDialog d(fx.locations);
        d.showPage(SelectProjectDialog::Page::NewProject);
        child<QLineEdit>(d, "projectName")->setText(QStringLiteral("bad:name"));
        QVERIFY(!d.createProjectFromForm());
        QCOMPARE(d.page(), SelectProjectDialog::Page::NewProject);
        QVERIFY(!child<QLabel>(d, "formError")->text().isEmpty());

        // Custom rasters must be even (guide p.65).
        child<QLineEdit>(d, "projectName")->setText(QStringLiteral("Custom one"));
        chooseFormat(d, QStringLiteral("Custom"));
        child<QSpinBox>(d, "rasterWidth")->setValue(1921);
        QVERIFY(!d.createProjectFromForm());
        child<QSpinBox>(d, "rasterWidth")->setValue(2000);
        QVERIFY(d.createProjectFromForm());
        const auto s = model::ProjectStore(fx.locations.privateProjects).list().front().settings;
        QCOMPARE(s.format.width, 2000);
        QCOMPARE(s.format.family, FormatFamily::HighRes);
    }

    void fieldsFollowTheFormat() {
        Fixture fx;
        SelectProjectDialog d(fx.locations);
        auto* aspect = child<QComboBox>(d, "aspectRatio");
        auto* stereo = child<QComboBox>(d, "stereoscopic");
        auto* film = child<QCheckBox>(d, "film");
        auto* rate = child<QComboBox>(d, "frameRate");

        // SD: choose 4:3 or 16:9, no stereoscopic (p.65-66).
        chooseFormat(d, QStringLiteral("PAL interlaced  (720 x 576)"));
        QVERIFY(aspect->isEnabled());
        QCOMPARE(aspect->count(), 2);
        QVERIFY(!stereo->isEnabled());
        QVERIFY(!film->isEnabled());

        // HD 1080p: 16:9 fixed; film only at 23.976, 24 and 25 (p.67).
        chooseFormat(d, QStringLiteral("1080p  (1920 x 1080)"));
        QVERIFY(!aspect->isEnabled());
        QCOMPARE(aspect->currentText(), QStringLiteral("16:9"));
        QVERIFY(stereo->isEnabled());
        rate->setCurrentText(QStringLiteral("23.976"));
        QVERIFY(film->isEnabled());
        film->setChecked(true);
        rate->setCurrentText(QStringLiteral("29.97"));
        QVERIFY(!film->isEnabled());
        // A frame rate without film also unchecks it, as a format change
        // does, so no film type is recorded.
        QVERIFY(!film->isChecked());
        QVERIFY(!child<QComboBox>(d, "filmType")->isEnabled());

        // Choose for Me defers the format (p.64).
        child<QCheckBox>(d, "chooseForMe")->setChecked(true);
        QVERIFY(!child<QComboBox>(d, "format")->isEnabled());
        QVERIFY(!rate->isEnabled());
    }

    void deletesTheSelectedProject() {
        Fixture fx;
        fx.addProject(fx.locations.privateProjects, QStringLiteral("Keep"));
        fx.addProject(fx.locations.privateProjects, QStringLiteral("Remove me"));
        SelectProjectDialog d(fx.locations);
        QVERIFY(d.selectProject(QStringLiteral("Remove me")));
        QVERIFY(d.deleteSelectedProject(false));
        QCOMPARE(d.listedProjects(), QStringList{"Keep"});
        QVERIFY(!QDir(QDir(fx.locations.privateProjects).filePath("Remove me")).exists());
    }

    void opensTheSelectedProject() {
        Fixture fx;
        fx.addProject(fx.locations.privateProjects, QStringLiteral("Feature"));
        SelectProjectDialog d(fx.locations);
        auto* open = child<QPushButton>(d, "openButton");
        QVERIFY(!open->isEnabled()); // nothing selected yet
        QVERIFY(d.selectProject(QStringLiteral("Feature")));
        QVERIFY(open->isEnabled());
        QTest::mouseClick(open, Qt::LeftButton);
        QCOMPARE(d.result(), int(QDialog::Accepted));
        QVERIFY(d.selectedProjectFile().endsWith(QStringLiteral("Feature/Feature.cthproj")));
    }

    void quitRejects() {
        Fixture fx;
        SelectProjectDialog d(fx.locations);
        d.setConfirmQuit(false);
        QTest::mouseClick(child<QPushButton>(d, "quitButton"), Qt::LeftButton);
        QCOMPARE(d.result(), int(QDialog::Rejected));
    }
};

QTEST_MAIN(TestSelectProjectDialog)
#include "tst_select_project_dialog.moc"
