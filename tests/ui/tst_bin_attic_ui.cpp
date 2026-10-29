// The Bin Container's attic copies and auto-save timing (Editing Guide p.71, p.295, p.298-299,
// p.1152-1154; docs/spec/bin-attic.md): new bins and saved bins are copied into the attic, and an
// auto-save waits for the inactivity period unless "Force Auto-Save at" has passed.
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin_attic.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

model::BinItem clip(const QString& name) {
    model::BinItem item;
    item.id = name;
    item.name = name;
    return item;
}

// Presses a key every 20 ms for `ms` milliseconds.
void typeFor(QWidget* target, int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QTest::keyClick(target, Qt::Key_Shift);
        QTest::qWait(20);
    }
}

} // namespace

class TestBinAtticUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void atticAndAutoSave() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        BinContainer& bins = window.binContainer();
        const QString attic = dir.filePath("Attic");
        bins.setAttic(attic);
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        QCOMPARE(bins.atticRoot(), attic);

        // "A corresponding bin file is placed in the Avid Projects folder, and a backup copy is placed in
        // the Avid Attic folder" (p.295).
        const QString id = bins.newBin();
        settle();
        BinDocument* doc = bins.document(id);
        QVERIFY(doc);
        const QString name = doc->name();
        QCOMPARE(model::atticVersions(attic, "Film", name).size(), 1);
        // Each save adds a copy.
        doc->addItem(clip("a"));
        QVERIFY(bins.saveBin(doc));
        QCOMPARE(model::atticVersions(attic, "Film", name).size(), 2);

        // Auto-save: due after 50 ms, but typing holds it off until 150 ms pass without input.
        bins.setAutoSaveTiming(50, 150, 60000);
        doc->addItem(clip("b"));
        typeFor(&window, 300);
        QVERIFY(doc->isDirty());
        QTRY_VERIFY_WITH_TIMEOUT(!doc->isDirty(), 2000);
        QVERIFY(bins.lastAutoSave().isValid());
        QCOMPARE(model::atticVersions(attic, "Film", name).size(), 3);

        // "Force Auto-Save at": saved while typing goes on (p.1153).
        bins.setAutoSaveTiming(40, 60000, 200);
        doc->addItem(clip("c"));
        QElapsedTimer t;
        t.start();
        while (doc->isDirty() && t.elapsed() < 2000) typeFor(&window, 40);
        QVERIFY(!doc->isDirty());

        // From the Bin settings: an interval of 0 turns auto-save off.
        window.settings().setValue(QStringLiteral("Bin"), QStringLiteral("autoSaveMinutes"), 0);
        bins.setAutoSaveTiming(0, 0, 0);
        doc->addItem(clip("d"));
        QTest::qWait(150);
        QVERIFY(doc->isDirty());
        QVERIFY(bins.saveBin(doc));
    }
};

QTEST_MAIN(TestBinAtticUi)
#include "tst_bin_attic_ui.moc"
