#include "lumenis/model/bin.h"
#include "lumenis/model/settings.h"

#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

class TestSettings : public QObject {
    Q_OBJECT

private slots:
    void seededHasOneActiveVersionOfEachKind() {
        const SettingsFile f = SettingsFile::seeded({QStringLiteral("Timeline"), QStringLiteral("Bin")});
        QCOMPARE(f.kinds(), (QStringList{QStringLiteral("Bin"), QStringLiteral("Timeline")}));
        QCOMPARE(f.versions(QStringLiteral("Bin")).size(), 1);
        QVERIFY(f.activeIndex(QStringLiteral("Bin")) >= 0);
        QVERIFY(f.activeValues(QStringLiteral("Bin")).isEmpty());
        QCOMPARE(f.activeIndex(QStringLiteral("Trim")), -1);
    }

    void siteVersionsReplaceDefaults() {
        // p.1141: new users and projects start from the site settings.
        SettingsFile site;
        site.entries << SettingEntry{QStringLiteral("Bin"), QStringLiteral("Fast"), true,
                                     {{QStringLiteral("autoSaveMinutes"), 5}}};
        site.entries << SettingEntry{QStringLiteral("Bin"), QStringLiteral("Slow"), false,
                                     {{QStringLiteral("autoSaveMinutes"), 30}}};
        const SettingsFile f = SettingsFile::seeded({QStringLiteral("Bin"), QStringLiteral("Trim")}, &site);
        QCOMPARE(f.versions(QStringLiteral("Bin")).size(), 2);
        QCOMPARE(f.activeValues(QStringLiteral("Bin")).value(QStringLiteral("autoSaveMinutes")).toInt(), 5);
        QCOMPARE(f.versions(QStringLiteral("Trim")).size(), 1);
    }

    void writesAndReadsBack() {
        QTemporaryDir dir;
        SettingsFile f = SettingsFile::seeded({QStringLiteral("Bin")});
        f.entries[0].name = QStringLiteral("Editing");
        f.entries[0].values.insert(QStringLiteral("autoSaveMinutes"), 7);
        const QString path = dir.filePath(QStringLiteral("user/") + SettingsFile::fileName());
        QString error;
        QVERIFY2(f.write(path, &error), qPrintable(error));
        const auto back = SettingsFile::read(path, &error);
        QVERIFY2(back, qPrintable(error));
        QCOMPARE(back->entries, f.entries);
        QVERIFY(!SettingsFile::read(dir.filePath(QStringLiteral("missing.json"))));
    }

    void activeVersionIsExclusive() {
        SettingsFile f = SettingsFile::seeded({QStringLiteral("Bin")});
        const int copy = f.duplicate(0);
        QCOMPARE(copy, 1);
        QVERIFY(!f.entries.at(copy).active);
        f.setActive(copy);
        QVERIFY(f.entries.at(copy).active);
        QVERIFY(!f.entries.at(0).active);
        QCOMPARE(f.activeIndex(QStringLiteral("Bin")), copy);
    }

    void duplicateNumbersCustomNames() {
        // p.1139: a period and a version number after a custom name.
        SettingsFile f = SettingsFile::seeded({QStringLiteral("Bin"), QStringLiteral("Trim")});
        f.entries[0].name = QStringLiteral("Edit");
        const int a = f.duplicate(0);
        QCOMPARE(f.entries.at(a).name, QStringLiteral("Edit.1"));
        const int b = f.duplicate(a);
        QCOMPARE(f.entries.at(b).name, QStringLiteral("Edit.2"));
        // Versions stay together: the Trim setting is still last.
        QCOMPARE(f.entries.constLast().kind, QStringLiteral("Trim"));
        // Without a custom name the copy has none.
        const int t = f.duplicate(static_cast<int>(f.entries.size()) - 1);
        QVERIFY(f.entries.at(t).name.isEmpty());
    }

    void removingTheActiveVersionActivatesAnother() {
        SettingsFile f = SettingsFile::seeded({QStringLiteral("Bin")});
        f.duplicate(0);
        f.remove({0});
        QCOMPARE(f.entries.size(), 1);
        QVERIFY(f.entries.at(0).active);
        f.remove({0});
        QVERIFY(f.versions(QStringLiteral("Bin")).isEmpty());
        QCOMPARE(f.ensure({QStringLiteral("Bin"), QStringLiteral("Trim")}), 2);
        QCOMPARE(f.ensure({QStringLiteral("Bin")}), 0);
    }

    void addOrReplace() {
        // p.1141: Add keeps the current setting; Replace overwrites it.
        SettingsFile f = SettingsFile::seeded({QStringLiteral("Bin")});
        const SettingEntry dropped{QStringLiteral("Bin"), QStringLiteral("Site"), true,
                                   {{QStringLiteral("autoSaveMinutes"), 3}}};
        const int added = f.add(dropped);
        QVERIFY(!f.entries.at(added).active);
        QVERIFY(f.activeValues(QStringLiteral("Bin")).isEmpty());
        const int replaced = f.add(dropped, true);
        QCOMPARE(replaced, 0);
        QCOMPARE(f.activeValues(QStringLiteral("Bin")).value(QStringLiteral("autoSaveMinutes")).toInt(), 3);
        // A kind the file did not have is active at once.
        const int trim = f.add(SettingEntry{QStringLiteral("Trim"), {}, false, {}});
        QVERIFY(f.entries.at(trim).active);
    }

    void newSequenceStartTimecode() {
        // General settings, Default Starting TC (p.1192).
        const Bin bin;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QCOMPARE(newSequence(bin, Rational(24, 1), now).fields.value(QStringLiteral("Start")),
                 QStringLiteral("01:00:00:00"));
        QCOMPARE(newSequence(bin, Rational(24, 1), now, QStringLiteral("10:00:00:00"))
                     .fields.value(QStringLiteral("Start")),
                 QStringLiteral("10:00:00:00"));
        QCOMPARE(newSequence(bin, Rational(24, 1), now, QStringLiteral("nonsense"))
                     .fields.value(QStringLiteral("Start")),
                 QStringLiteral("01:00:00:00"));
        // A drop-frame start stays drop-frame where the rate allows it.
        QCOMPARE(newSequence(bin, Rational(30000, 1001), now, QStringLiteral("00:59:59;29"))
                     .fields.value(QStringLiteral("Start")),
                 QStringLiteral("00:59:59;29"));
        QCOMPARE(newSequence(bin, Rational(25, 1), now, QStringLiteral("10:00:00;00"))
                     .fields.value(QStringLiteral("Start")),
                 QStringLiteral("10:00:00:00"));
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
