#include "lumenis/model/locations.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/mask_margins.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

ProjectSettings settings(const QString& name) {
    ProjectSettings s;
    s.name = name;
    s.format = projectFormatPresets().front();
    s.aspect = s.format.defaultAspect();
    s.colorSpace = QStringLiteral("RGB 2020");
    s.colorDepth = QStringLiteral("12 bit");
    return s;
}

} // namespace

class TestProjectStore : public QObject {
    Q_OBJECT

private slots:
    // "Automatically launch last project at startup" (p.69, p.1200): the user and project opened last, kept in the users
    // folder; none at first, and none from a damaged file.
    void lastProject() {
        QTemporaryDir dir;
        Locations l;
        l.users = dir.filePath(QStringLiteral("Users"));
        QVERIFY(!readLastProject(l));
        QVERIFY(writeLastProject(l, {QStringLiteral("ed"), dir.filePath(QStringLiteral("Film/Film.cthproj"))}));
        const auto last = readLastProject(l);
        QVERIFY(last);
        QCOMPARE(last->user, QStringLiteral("ed"));
        QCOMPARE(last->file, dir.filePath(QStringLiteral("Film/Film.cthproj")));
        QFile f(QDir(l.users).filePath(QStringLiteral("Last Project.json")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("not json");
        f.close();
        QVERIFY(!readLastProject(l));
    }

    void createsListsAndReadsBack() {
        QTemporaryDir dir;
        const ProjectStore store(dir.filePath("Projects")); // root does not exist yet
        QVERIFY(store.list().isEmpty());

        ProjectSettings s = settings(QStringLiteral("Episode 2"));
        s.stereoscopic = QStringLiteral("Side by Side");
        s.filmType = QStringLiteral("35mm, 4 perf");
        QString error;
        const auto entry = store.create(s, &error);
        QVERIFY2(entry, qPrintable(error));
        QVERIFY(QFile::exists(dir.filePath("Projects/Episode 2/Episode 2.cthproj")));
        QVERIFY(entry->settings.created.isValid());
        QVERIFY(store.create(settings(QStringLiteral("episode 1"))));

        const QList<ProjectEntry> all = store.list();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[0].settings.name, QStringLiteral("episode 1")); // case-insensitive order
        const ProjectSettings& back = all[1].settings;
        QCOMPARE(back.name, QStringLiteral("Episode 2"));
        QCOMPARE(back.format, s.format);
        QCOMPARE(back.aspect, s.aspect);
        QCOMPARE(back.colorSpace, s.colorSpace);
        QCOMPARE(back.colorDepth, s.colorDepth);
        QCOMPARE(back.stereoscopic, s.stereoscopic);
        QCOMPARE(back.filmType, s.filmType);
        QCOMPARE(back.created.toSecsSinceEpoch(), entry->settings.created.toSecsSinceEpoch());
    }

    void refusesBadNamesAndDuplicates() {
        QTemporaryDir dir;
        const ProjectStore store(dir.path());
        QString error;
        QVERIFY(!store.create(settings(QStringLiteral("a:b")), &error));
        QVERIFY(error.contains(QLatin1Char(':')));
        QVERIFY(store.create(settings(QStringLiteral("Show"))));
        QVERIFY(!store.create(settings(QStringLiteral("SHOW")), &error));
        QVERIFY(error.contains(QStringLiteral("already exists")));
    }

    void ignoresFoldersThatAreNotProjects() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("Random folder");
        QFile junk(dir.filePath("Broken/Broken.cthproj"));
        QDir(dir.path()).mkpath("Broken");
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("{ not json");
        junk.close();
        QVERIFY(ProjectStore(dir.path()).list().isEmpty());
    }

    void removesOnlyProjects() {
        QTemporaryDir dir;
        const ProjectStore store(dir.path());
        QVERIFY(store.create(settings(QStringLiteral("Doomed"))));
        QDir(dir.path()).mkpath("Not a project");
        QString error;
        QVERIFY(!store.remove(QStringLiteral("Not a project"), &error));
        QVERIFY(QDir(dir.filePath("Not a project")).exists());
        QVERIFY(!store.remove(QStringLiteral(".."), &error));
        QVERIFY(store.remove(QStringLiteral("Doomed"), &error));
        QVERIFY(!QDir(dir.filePath("Doomed")).exists());
    }

    void rejectsNewerFiles() {
        QTemporaryDir dir;
        QFile f(dir.filePath("p.cthproj"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"format":"cutthulhu-project","version":99})");
        f.close();
        QString error;
        QVERIFY(!ProjectStore::read(f.fileName(), &error));
        QVERIFY(error.contains(QStringLiteral("newer")));
    }

    // The Format tab's Proxy (p.526-527): high-resolution projects only, never under 256 x 120 (p.355), kept in the file.
    void proxyModes() {
        lumenis::ProjectFormat hd = projectFormatPresets().front();
        for (const lumenis::ProjectFormat& f : projectFormatPresets()) {
            if (f.family == lumenis::FormatFamily::HD) hd = f;
        }
        QCOMPARE(lumenis::model::proxyModes(hd), QList<int>{0});
        lumenis::ProjectFormat uhd = hd;
        uhd.family = lumenis::FormatFamily::HighRes;
        uhd.width = 3840;
        uhd.height = 2160;
        QCOMPARE(lumenis::model::proxyModes(uhd), (QList<int>{0, 4, 16}));
        lumenis::ProjectFormat small = uhd;
        small.width = 960;
        small.height = 540;
        QCOMPARE(lumenis::model::proxyModes(small), (QList<int>{0, 4}));
        ProjectSettings s = settings(QStringLiteral("Proxy"));
        s.format = uhd;
        QCOMPARE(lumenis::model::proxyRaster(s), QSize(3840, 2160));
        s.proxy = 4;
        QCOMPARE(lumenis::model::proxyRaster(s), QSize(1920, 1080));
        s.proxy = 16;
        QCOMPARE(lumenis::model::proxyRaster(s), QSize(960, 540));
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("p.cthproj"));
        QVERIFY(ProjectStore::write(file, s));
        QCOMPARE(ProjectStore::read(file)->proxy, 16);
        s.format = hd;
        QVERIFY(ProjectStore::write(file, s));
        QCOMPARE(ProjectStore::read(file)->proxy, 0);
    }

    // Mask margins (p.550-551): the presets of the Target Settings dialog, letterboxed or pillarboxed, kept in the file.
    void maskMargins() {
        QCOMPARE(maskPresets().first(), QStringLiteral("4:3"));
        QCOMPARE(maskPresets().last(), customMaskPreset());
        QCOMPARE(maskPresets().size(), 16);
        const double hd = 16.0 / 9.0;
        const MaskMargins scope = maskForPreset(QStringLiteral("2.39:1"), hd);
        QVERIFY(qAbs(scope.top - (1.0 - hd / 2.39) * 50.0) < 1e-9 && scope.top == scope.bottom && scope.left == 0.0);
        const MaskMargins academy = maskForPreset(QStringLiteral("4:3"), hd);
        QVERIFY(qAbs(academy.left - 12.5) < 1e-9 && academy.right == academy.left && academy.top == 0.0);
        QVERIFY(maskForPreset(QStringLiteral("16:9"), hd).isEmpty());
        QVERIFY(maskForPreset(customMaskPreset(), hd).isEmpty());
        QCOMPARE(maskedArea(QRectF(0, 0, 200, 100), academy), QRectF(25, 0, 150, 100));
        QCOMPARE(targetMaskFrom(targetMaskValue(TargetMask::BlackMask)), TargetMask::BlackMask);
        QCOMPARE(targetMaskFrom(QStringLiteral("nonsense")), TargetMask::None);
        ProjectSettings s = settings(QStringLiteral("Masked"));
        s.mask = scope;
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("p.cthproj"));
        QVERIFY(ProjectStore::write(file, s));
        QCOMPARE(ProjectStore::read(file)->mask, scope);
    }

    // ACES projects' Output Color Space (p.559): kept in the file, Rec709 for an unknown one.
    void acesOutputColorSpace() {
        ProjectSettings s = settings(QStringLiteral("Aces"));
        s.colorSpace = QStringLiteral("ACES RGB");
        QVERIFY(isAcesColorSpace(s.colorSpace));
        QCOMPARE(s.outputColorSpace, QStringLiteral("Rec709"));
        QCOMPARE(acesOutputColorSpaces().size(), 4);
        s.outputColorSpace = QStringLiteral("DCI P3");
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("p.cthproj"));
        QVERIFY(ProjectStore::write(file, s));
        QCOMPARE(ProjectStore::read(file)->outputColorSpace, QStringLiteral("DCI P3"));
        s.outputColorSpace = QStringLiteral("nonsense");
        QVERIFY(ProjectStore::write(file, s));
        QCOMPARE(ProjectStore::read(file)->outputColorSpace, QStringLiteral("Rec709"));
    }

    void userProfiles() {
        QTemporaryDir dir;
        const UserProfiles users(dir.filePath("Users"));
        QVERIFY(users.list().isEmpty());
        QVERIFY(users.create(QStringLiteral("editor")));
        QVERIFY(users.create(QStringLiteral("Assistant")));
        QVERIFY(!users.create(QStringLiteral("bad/name")));
        QCOMPARE(users.list(), (QStringList{"Assistant", "editor"}));
        QVERIFY(!UserProfiles::defaultName().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestProjectStore)
#include "tst_project_store.moc"
