// The Timecode window (Editing Guide p.388-389) and Clip > Loop Selected Clips (p.393;
// docs/spec/timecode-window.md): lines of tracking information, the Timecode menu, Add Line and Size;
// clips played one after another in the Source monitor, Tab and Shift+Tab, Alt for whole clips.
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timecode_window.h"

#include <QAction>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;
using namespace lumenis::ui;

namespace {

BinItem clip(const QString& name, std::int64_t frames) {
    BinItem c;
    c.id = name;
    c.name = name;
    c.kind = BinItemKind::MasterClip;
    c.editRate = Rational(25);
    c.durationFrames = frames;
    return c;
}

QAction* find(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

} // namespace

class TestTimecodeWindow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void lines() {
        Composer composer;
        BinItem item = clip(QStringLiteral("Cut"), 0);
        item.kind = BinItemKind::Sequence;
        Sequence seq = Sequence::withDefaultTracks();
        Segment s;
        s.clipId = QStringLiteral("A");
        s.name = QStringLiteral("A");
        s.sourceTrack = QStringLiteral("V1");
        s.length = 250;
        seq.splice(QStringLiteral("V1"), 0, {s});
        seq.storeIn(item);
        composer.load(MonitorId::Record, QStringLiteral("bin"), item);
        composer.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("C"), 100));
        composer.moveTo(MonitorId::Record, 25);
        TimecodeWindow window(composer);
        window.show();
        // One line to start with: the sequence's master timecode (picture on p.389).
        QCOMPARE(window.lines().size(), 1);
        QCOMPARE(window.labelText(0), QStringLiteral("Mas"));
        QCOMPARE(window.valueText(0), composer.state(MonitorId::Record).tracking(TrackingFormat::Master));
        // It follows the position.
        composer.moveTo(MonitorId::Record, 50);
        QCOMPARE(window.findChildren<QLabel*>(QStringLiteral("value")).first()->text(),
                 composer.state(MonitorId::Record).tracking(TrackingFormat::Master));
        // The Timecode menu: Sequence, Source, None, Add Line, Size, and the sequence's values.
        std::unique_ptr<QMenu> menu(window.menuFor(0));
        QVERIFY(find(menu.get(), QStringLiteral("Sequence")) && find(menu.get(), QStringLiteral("Source")));
        QVERIFY(find(menu.get(), QStringLiteral("None")) && find(menu.get(), QStringLiteral("Add Line")));
        QVERIFY(find(menu.get(), QStringLiteral("Size")));
        QVERIFY(find(menu.get(), QStringLiteral("Sequence Timecode :")));
        find(menu.get(), QStringLiteral("Add Line"))->trigger();
        QCOMPARE(window.lines().size(), 2);
        QVERIFY(window.labelText(1).isEmpty());
        std::unique_ptr<QMenu> second(window.menuFor(1));
        QMenu* source = find(second.get(), QStringLiteral("Source"))->menu();
        find(source, trackingLabel(TrackingFormat::Duration))->trigger();
        QCOMPARE(window.lines().at(1), (TimecodeWindow::Line{MonitorId::Source, TrackingFormat::Duration}));
        QCOMPARE(window.valueText(1), composer.state(MonitorId::Source).tracking(TrackingFormat::Duration));
        QVERIFY(window.labelText(1).startsWith(QStringLiteral("S ")));
        // Size > font size.
        std::unique_ptr<QMenu> third(window.menuFor(0));
        find(find(third.get(), QStringLiteral("Size"))->menu(), QStringLiteral("24"))->trigger();
        QCOMPARE(window.fontSize(), 24);
        QCOMPARE(window.findChildren<QLabel*>(QStringLiteral("value")).first()->font().pointSize(), 24);
        // Up to 48 lines.
        while (window.addLine()) {
        }
        QCOMPARE(window.lines().size(), TimecodeWindow::kMaxLines);
    }

    void loopSelectedClips() {
        Composer composer;
        BinItem a = clip(QStringLiteral("A"), 100);
        BinItem b = clip(QStringLiteral("B"), 100);
        composer.load(MonitorId::Source, QStringLiteral("bin"), a);
        model::MonitorState& s = composer.state(MonitorId::Source);
        s.setPosition(10);
        s.markIn();
        s.setPosition(19);
        s.markOut();
        a = s.current()->item;
        composer.playClipLoop({{QStringLiteral("bin"), a}, {QStringLiteral("bin"), b}}, false);
        QVERIFY(composer.isClipLooping() && composer.isPlaying());
        QCOMPARE(s.current()->item.id, QStringLiteral("A"));
        QCOMPARE(s.position(), 10);
        // At A's OUT, B plays (it has no marks: all of it), then A again.
        composer.advance(1000);
        QCOMPARE(s.current()->item.id, QStringLiteral("B"));
        QCOMPARE(s.position(), 0);
        composer.advance(5000);
        QCOMPARE(s.current()->item.id, QStringLiteral("A"));
        // Tab and Shift+Tab go to the next and previous clip (p.393).
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(&composer, &tab);
        QCOMPARE(s.current()->item.id, QStringLiteral("B"));
        QKeyEvent back(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        QApplication::sendEvent(&composer, &back);
        QCOMPARE(s.current()->item.id, QStringLiteral("A"));
        // The space bar (stop) ends the loop.
        composer.stop();
        QVERIFY(!composer.isClipLooping() && !composer.isPlaying());
        // Alt: start to end.
        composer.playClipLoop({{QStringLiteral("bin"), a}}, true);
        QCOMPARE(s.position(), 0);
        composer.stop();
    }

    void inTheApplication() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        // Tools > Timecode Window (p.389).
        commands.action(QStringLiteral("tools.timecode_window"))->trigger();
        QVERIFY(window.timecodeWindow() && window.timecodeWindow()->isVisible());
        window.timecodeWindow()->close();
        // Clip > Loop Selected Clips from the active bin.
        const QString binId = window.binContainer().newBin();
        BinDocument* doc = window.binContainer().openBin(binId);
        QVERIFY(doc);
        doc->addItem(clip(QStringLiteral("A"), 50));
        doc->addItem(clip(QStringLiteral("B"), 50));
        BinView* view = window.binContainer().viewFor(doc);
        view->selectIds({QStringLiteral("A"), QStringLiteral("B")});
        commands.action(QStringLiteral("clip.loop_selected_clips"))->trigger();
        QVERIFY(window.composer().isClipLooping());
        window.composer().stop();
    }
};

QTEST_MAIN(TestTimecodeWindow)
#include "tst_timecode_window.moc"
