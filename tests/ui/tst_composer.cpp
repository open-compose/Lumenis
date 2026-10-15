#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/project_store.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/keyboard_map.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMimeData>
#include <QTableView>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;
using lumenis::model::TrackingFormat;

namespace {

BinItem clip(const QString& name, std::int64_t frames, BinItemKind kind = BinItemKind::MasterClip,
             const Rational& rate = Rational(25)) {
    BinItem i;
    i.id = model::newItemId();
    i.name = name;
    i.kind = kind;
    i.durationFrames = frames;
    i.editRate = rate;
    return i;
}

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

} // namespace

class TestComposer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void loadsClipsAndSequences() {
        Composer c;
        c.resize(900, 360);
        c.show();
        // Sequences to the Record monitor, clips to the Source monitor.
        c.loadDefault(QStringLiteral("bin"), clip(QStringLiteral("Rough Cut"), 500, BinItemKind::Sequence));
        c.loadDefault(QStringLiteral("bin"), clip(QStringLiteral("Interview"), 250));
        QCOMPARE(c.state(MonitorId::Record).current()->item.name, QStringLiteral("Rough Cut"));
        QCOMPARE(c.state(MonitorId::Source).current()->item.name, QStringLiteral("Interview"));
        QCOMPARE(c.activeMonitor(), MonitorId::Source);
        QVERIFY(c.clipNameButton(MonitorId::Source)->text().startsWith(QStringLiteral("Interview")));

        // The Clip Name menu: loaded clips, Clear Monitor and Clear Menu.
        c.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("B-roll"), 100));
        QMenu* menu = c.clipNameMenu(MonitorId::Source);
        QStringList texts;
        for (QAction* a : menu->actions()) {
            if (!a->isSeparator()) texts << a->text();
        }
        QCOMPARE(texts, (QStringList{"B-roll", "Interview", "Clear Monitor", "Clear Menu"}));
        menu->actions().at(1)->trigger();
        QCOMPARE(c.state(MonitorId::Source).current()->item.name, QStringLiteral("Interview"));
        c.clipNameMenu(MonitorId::Source)->actions().last()->trigger(); // Clear Menu
        QCOMPARE(c.state(MonitorId::Source).loadedCount(), 1);

        // No tracking data until a format is chosen (p.385).
        QVERIFY(c.trackingText(MonitorId::Record).isEmpty());
        c.setTracking(MonitorId::Record, TrackingFormat::Master);
        QCOMPARE(c.trackingText(MonitorId::Record), QStringLiteral("Mas  01:00:00:00"));
        QCOMPARE(c.trackingButton(MonitorId::Record)->text(), QStringLiteral("Mas  01:00:00:00"));
    }

    void buttonsBelowTheMonitors() {
        Composer c;
        // Two rows under each monitor, as pictured on p.380.
        for (MonitorId m : {MonitorId::Source, MonitorId::Record}) {
            for (const char* id : {"play.play", "move.step_backward_one_frame", "move.step_forward_one_frame",
                                   "edit.mark_in", "edit.mark_out", "edit.go_to_in_point", "edit.go_to_out_point",
                                   "play.play_in_to_out", "other.gang"}) {
                QVERIFY2(c.button(m, QLatin1String(id)), id);
            }
        }
        QVERIFY(c.button(MonitorId::Source, QStringLiteral("edit.splice_in")));
        QVERIFY(c.button(MonitorId::Source, QStringLiteral("edit.mark_clip")));
        // Editing is the Timeline's: the buttons ask for it.
        QVERIFY(c.button(MonitorId::Source, QStringLiteral("edit.overwrite"))->isEnabled());
        QSignalSpy requested(&c, &Composer::commandRequested);
        c.button(MonitorId::Source, QStringLiteral("edit.overwrite"))->click();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.at(0).at(0).toString(), QStringLiteral("edit.overwrite"));
        // Add Marker is the Timeline's too (it stores the marker).
        QVERIFY(c.button(MonitorId::Source, QStringLiteral("more.add_marker"))->isEnabled());
        // The Record monitor is always ganged (p.495): its Gang does nothing.
        QVERIFY(!c.button(MonitorId::Record, QStringLiteral("other.gang"))->isEnabled());
        QVERIFY(!c.findChildren<QToolButton*>(QStringLiteral("blankButton")).isEmpty());
    }

    void movesAndMarks() {
        Composer c;
        c.loadDefault({}, clip(QStringLiteral("Clip"), 250));
        c.perform(QStringLiteral("move.step_forward_10_frames"));
        QCOMPARE(c.state(MonitorId::Source).position(), 10);
        c.perform(QStringLiteral("move.step_backward_one_frame"));
        QCOMPARE(c.state(MonitorId::Source).position(), 9);
        c.perform(QStringLiteral("edit.mark_in"));
        c.perform(QStringLiteral("move.go_to_end"));
        c.perform(QStringLiteral("edit.mark_out"));
        QCOMPARE(c.centerDuration(), QStringLiteral("9:16")); // 241 frames at 25 fps
        c.perform(QStringLiteral("edit.go_to_in_point"));
        QCOMPARE(c.state(MonitorId::Source).position(), 9);
        c.perform(QStringLiteral("edit.clear_in_mark"));
        QVERIFY(!c.state(MonitorId::Source).in());
        c.perform(QStringLiteral("edit.mark_clip"));
        QCOMPARE(c.state(MonitorId::Source).in(), std::optional<std::int64_t>(0));
        QCOMPARE(c.state(MonitorId::Source).out(), std::optional<std::int64_t>(249));
        c.perform(QStringLiteral("edit.clear_both_marks"));
        QVERIFY(!c.state(MonitorId::Source).out());
        c.perform(QStringLiteral("move.go_to_previous_event"));
        QCOMPARE(c.state(MonitorId::Source).position(), 0);

        // 8 frames in 24-frame formats (p.398).
        Composer film;
        film.loadDefault({}, clip(QStringLiteral("Film"), 500, BinItemKind::MasterClip, Rational(24000, 1001)));
        film.perform(QStringLiteral("move.step_forward_10_frames"));
        QCOMPARE(film.state(MonitorId::Source).position(), 8);
    }

    void playsInRealTimeWithShuttleSpeeds() {
        Composer c;
        c.loadDefault({}, clip(QStringLiteral("Clip"), 1000));
        c.perform(QStringLiteral("play.play"));
        QVERIFY(c.isPlaying());
        c.advance(1000);
        QCOMPARE(c.state(MonitorId::Source).position(), 25);
        c.perform(QStringLiteral("play.play")); // stops
        QVERIFY(!c.isPlaying());
        // L again and again: 1, 2, 3 times (p.400).
        c.perform(QStringLiteral("play.play_forward"));
        c.perform(QStringLiteral("play.play_forward"));
        c.perform(QStringLiteral("play.play_forward"));
        QCOMPARE(c.speed(), 3);
        c.advance(1000);
        QCOMPARE(c.state(MonitorId::Source).position(), 100);
        c.perform(QStringLiteral("play.play_reverse"));
        QCOMPARE(c.speed(), -1);
        c.perform(QStringLiteral("play.pause"));
        QVERIFY(!c.isPlaying());

        // Play In to Out stops at the OUT mark.
        c.state(MonitorId::Source).setPosition(200);
        c.perform(QStringLiteral("edit.mark_in"));
        c.state(MonitorId::Source).setPosition(230);
        c.perform(QStringLiteral("edit.mark_out"));
        c.perform(QStringLiteral("play.play_in_to_out"));
        QCOMPARE(c.state(MonitorId::Source).position(), 200);
        c.advance(5000);
        QCOMPARE(c.state(MonitorId::Source).position(), 230);
        QVERIFY(!c.isPlaying());
        // Playback stops at the end.
        c.perform(QStringLiteral("play.play"));
        c.advance(60000);
        QCOMPARE(c.state(MonitorId::Source).position(), 999);
        QVERIFY(!c.isPlaying());
    }

    void keyboard() {
        CommandRegistry commands;
        commands.loadBuiltin();
        KeyboardMap keys;
        keys.loadBuiltin(commands);
        Composer c;
        c.setKeyResolver([&](const QKeySequence& k) { return keys.commandFor(k); });
        c.show();
        c.loadDefault({}, clip(QStringLiteral("Clip"), 1000));
        c.setFocus();
        QTest::keyClick(&c, Qt::Key_4);
        QCOMPARE(c.state(MonitorId::Source).position(), 1);
        QTest::keyClick(&c, Qt::Key_I);
        QCOMPARE(c.state(MonitorId::Source).in(), std::optional<std::int64_t>(1));
        QTest::keyClick(&c, Qt::Key_End);
        QTest::keyClick(&c, Qt::Key_O);
        QCOMPARE(c.state(MonitorId::Source).out(), std::optional<std::int64_t>(999));
        QTest::keyClick(&c, Qt::Key_Home);
        QCOMPARE(c.state(MonitorId::Source).position(), 0);
        QTest::keyClick(&c, Qt::Key_Space);
        QVERIFY(c.isPlaying());
        QTest::keyClick(&c, Qt::Key_K);
        QVERIFY(!c.isPlaying());
        // Hold K and tap L: one frame forward (p.401).
        QTest::keyPress(&c, Qt::Key_K);
        QTest::keyClick(&c, Qt::Key_L);
        QTest::keyRelease(&c, Qt::Key_K);
        QCOMPARE(c.state(MonitorId::Source).position(), 1);
        QVERIFY(!c.isPlaying());
    }

    void singleMonitorAndGang() {
        Composer c;
        c.show();
        c.loadDefault({}, clip(QStringLiteral("Clip"), 500));
        c.loadDefault({}, clip(QStringLiteral("Cut"), 500, BinItemKind::Sequence));
        // Gang: the monitors move together (p.495).
        c.setActiveMonitor(MonitorId::Source);
        c.perform(QStringLiteral("other.gang"));
        QVERIFY(c.isGanged());
        c.state(MonitorId::Source).setPosition(100);
        c.perform(QStringLiteral("move.step_forward_10_frames"));
        QCOMPARE(c.state(MonitorId::Source).position(), 110);
        QCOMPARE(c.state(MonitorId::Record).position(), 10);
        c.perform(QStringLiteral("other.gang"));
        QVERIFY(!c.isGanged());

        // Show Single/Dual Monitor: the Record monitor alone (p.381).
        c.perform(QStringLiteral("composer.show_single_dual_monitor"));
        QVERIFY(c.isSingleMonitor());
        QVERIFY(!c.picture(MonitorId::Source)->isVisible());
        QCOMPARE(c.activeMonitor(), MonitorId::Record);
        c.setActiveMonitor(MonitorId::Source);
        QCOMPARE(c.activeMonitor(), MonitorId::Record);
        c.perform(QStringLiteral("composer.show_single_dual_monitor"));
        QVERIFY(c.picture(MonitorId::Source)->isVisible());
    }

    void positionBarAndPicture() {
        Composer c;
        c.resize(1000, 400);
        c.show();
        c.loadDefault({}, clip(QStringLiteral("Clip"), 101));
        settle();
        PositionBar* bar = c.positionBar(MonitorId::Source);
        QCOMPARE(bar->frameAt(0), 0);
        QCOMPARE(bar->frameAt(bar->width() - 1), 100);
        QTest::mouseClick(bar, Qt::LeftButton, {}, QPoint(bar->xFor(50), bar->height() / 2));
        QCOMPARE(c.state(MonitorId::Source).position(), 50);
        // The picture keeps the project's aspect ratio (p.379).
        c.setAspectRatio(Rational(4, 3));
        const QRect r = c.picture(MonitorId::Source)->pictureRect();
        QVERIFY(std::abs(r.width() * 3 - r.height() * 4) <= 4);
    }

    void dropsFromBins() {
        Composer c;
        const BinItem one = clip(QStringLiteral("One"), 100);
        const BinItem two = clip(QStringLiteral("Two"), 100);
        c.setItemResolver([&](const QString& bin, const QString& id) -> std::optional<BinItem> {
            if (bin != QLatin1String("b")) return std::nullopt;
            if (id == one.id) return one;
            if (id == two.id) return two;
            return std::nullopt;
        });
        c.loadFromBin(MonitorId::Record, QStringLiteral("b"), {one.id, two.id, QStringLiteral("missing")});
        QCOMPARE(c.state(MonitorId::Record).loadedCount(), 2);
        QCOMPARE(c.state(MonitorId::Record).current()->item.name, QStringLiteral("Two"));
        QCOMPARE(c.activeMonitor(), MonitorId::Record);
    }

    void mainWindowLoadsFromBins() {
        QTemporaryDir dir;
        model::ProjectSettings s;
        s.name = QStringLiteral("Doc");
        s.format = projectFormatPresets().front();
        const auto entry = *model::ProjectStore(dir.path()).create(s);
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.show();
        window.setProject(entry);
        commands.action(QStringLiteral("timeline.new.sequence"))->trigger();
        BinContainer& bins = window.binContainer();
        BinView* view = bins.activeView();
        QVERIFY(view);
        // New sequences start at the default hour.
        QCOMPARE(view->document().bin().items.first().fields.value(QStringLiteral("Start")),
                 QStringLiteral("01:00:00:00"));
        // Double-click in the bin.
        emit view->table()->doubleClicked(view->table()->model()->index(0, BinItemModel::kNameColumn));
        Composer& composer = window.composer();
        QVERIFY(!composer.state(MonitorId::Record).isEmpty());
        // Menu commands reach the Composer.
        composer.state(MonitorId::Record).current();
        QVERIFY(commands.action(QStringLiteral("composer.show_single_dual_monitor"))->isEnabled());
        commands.action(QStringLiteral("composer.show_single_dual_monitor"))->trigger();
        QVERIFY(composer.isSingleMonitor());
        window.clearProject();
        QVERIFY(composer.state(MonitorId::Record).isEmpty());
    }
};

QTEST_MAIN(TestComposer)
#include "tst_composer.moc"
