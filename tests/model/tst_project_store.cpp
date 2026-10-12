#include "lumenis/model/project_store.h"

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
