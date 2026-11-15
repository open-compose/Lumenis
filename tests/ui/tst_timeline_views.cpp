// Timeline views and View Type (Editing Guide p.592, p.604-605, p.638-639; docs/spec/timeline-views.md):
// views saved as Timeline View User settings from the View Menu button, chosen from it, replaced with
// Alt, and switched with TV1-TV8; Heads and Heads Tails views, and rearranging clips in them.
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/command_palette.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QMenu>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <cmath>
#include <memory>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

QAction* menuItem(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions()) {
        if (a->text() == text) return a;
        if (a->menu()) {
            if (QAction* found = menuItem(a->menu(), text)) return found;
        }
    }
    return nullptr;
}

model::Segment clip(const QString& name, const QString& track, std::int64_t length) {
    model::Segment g;
    g.binId = QStringLiteral("bin");
    g.clipId = name;
    g.name = name;
    g.sourceTrack = track;
    g.length = length;
    return g;
}

// The names of a track's segments, in order.
QStringList names(const model::Sequence& seq, const QString& track) {
    QStringList out;
    for (const model::Segment& s : seq.track(track)->segments) out << (s.isFiller() ? QStringLiteral("-") : s.name);
    return out;
}

model::Sequence recorded(MainWindow& window) {
    return model::Sequence::fromItem(window.composer().state(MonitorId::Record).current()->item);
}

struct Fixture {
    QTemporaryDir dir;
    ext::ProviderRegistry providers;
    CommandRegistry commands;
    std::unique_ptr<MainWindow> window;

    Fixture() {
        commands.loadBuiltin();
        window = std::make_unique<MainWindow>(commands, providers);
        window->resize(1200, 760);
        window->settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window->setProject(project);
        settle();
        // V1 and A1: clips a (10 frames), b (20) and c (30), cut at the same frames.
        model::BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        model::Sequence seq = model::Sequence::withDefaultTracks();
        for (const QString& t : {QStringLiteral("V1"), QStringLiteral("A1")}) {
            seq.splice(t, 0, {clip(QStringLiteral("a"), t, 10), clip(QStringLiteral("b"), t, 20), clip(QStringLiteral("c"), t, 30)});
        }
        seq.storeIn(item);
        window->composer().load(MonitorId::Record, QStringLiteral("bin"), item);
        Timeline& timeline = window->timeline();
        timeline.resize(1000, 400);
        timeline.show();
        timeline.showEntireSequence();
        settle();
    }
};

} // namespace

class TestTimelineViews : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // Show Track > Film and Zoom > Show Every Frame (p.593, p.642; 11.5b): a row of the sequence's frames at the top of
    // the Timeline, kept with Timeline views; Show Every Frame needs it and zooms to a film frame a frame.
    void filmTrack() {
        Fixture f;
        Timeline& timeline = f.window->timeline();
        // Every frame's picture its own colour, as the monitor shows it.
        f.window->composer().setPictureResolver([](MonitorId, std::int64_t frame, QSize size, bool*) {
            QImage image(size.isValid() ? size : QSize(16, 9), QImage::Format_RGB32);
            image.fill(QColor::fromHsv(static_cast<int>(frame * 6 % 360), 200, 220));
            return image;
        });
        QVERIFY(!timeline.showEveryFrame()); // without the film track
        QMenu* menu = timeline.fastMenu(); // the Timeline's own, rebuilt each time
        QAction* film = menuItem(menu, QStringLiteral("Film"));
        QAction* every = menuItem(menu, QStringLiteral("Show Every Frame"));
        QVERIFY(film && every);
        QVERIFY(film->isCheckable() && !film->isChecked());
        QVERIFY(!every->isEnabled());
        film->trigger();
        QVERIFY(timeline.display().filmTrack);
        QVERIFY(timeline.filmTrackShown());
        menu = timeline.fastMenu();
        QVERIFY(menuItem(menu, QStringLiteral("Show Every Frame"))->isEnabled());
        // Kept with Timeline views.
        QVERIFY(Timeline::displayFromJson(Timeline::displayToJson(timeline.display())).filmTrack);
        // Show Every Frame: one film frame for each frame.
        QVERIFY(timeline.showEveryFrame());
        QCOMPARE(timeline.scale(), timeline.filmFrameWidth());
        // The row is at the top, under the ruler, with the frames drawn in it.
        timeline.setFirstFrame(0);
        settle();
        QWidget* view = timeline.findChild<QWidget*>(QStringLiteral("timelineView"));
        QVERIFY(view);
        const QImage shot = view->grab().toImage();
        const QColor expected = QColor::fromHsv(6, 200, 220);
        bool found = false;
        for (int x = 0; x < shot.width() && !found; ++x) {
            for (int y = 0; y < 80 && !found; ++y) {
                const QColor c = shot.pixelColor(x, y);
                found = std::abs(c.red() - expected.red()) < 8 && std::abs(c.green() - expected.green()) < 8 &&
                        std::abs(c.blue() - expected.blue()) < 8;
            }
        }
        QVERIFY2(found, "frame 1's picture in the film track");
        // Off again: no row, Show Every Frame does nothing.
        timeline.setFilmTrack(false);
        QVERIFY(!timeline.showEveryFrame());
    }

    // "the system cuts all tracks to the same edit point" (p.638).
    void spans() {
        model::Sequence seq = model::Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("a"), QStringLiteral("V1"), 10), clip(QStringLiteral("b"), QStringLiteral("V1"), 20)});
        seq.splice(QStringLiteral("A1"), 0, {clip(QStringLiteral("x"), QStringLiteral("A1"), 15), clip(QStringLiteral("y"), QStringLiteral("A1"), 15)});
        const QStringList both{QStringLiteral("V1"), QStringLiteral("A1")};
        const auto spans = seq.headsSpans(both);
        QCOMPARE(spans.size(), 3);
        QCOMPARE(spans.at(0), (std::pair<std::int64_t, std::int64_t>{0, 10}));
        QCOMPARE(spans.at(1), (std::pair<std::int64_t, std::int64_t>{10, 15}));
        QCOMPARE(spans.at(2), (std::pair<std::int64_t, std::int64_t>{15, 30}));
        // Moving the first clip to the end moves it on both tracks, cutting A1's x at frame 10.
        QVERIFY(seq.moveSpan(both, 0, 10, 30));
        QCOMPARE(names(seq, QStringLiteral("V1")), (QStringList{"b", "a"}));
        QCOMPARE(names(seq, QStringLiteral("A1")), (QStringList{"x", "y", "x"}));
        QCOMPARE(seq.track(QStringLiteral("V1"))->length(), 30);
        // Not into itself, nor on a locked track.
        QVERIFY(!seq.moveSpan(both, 0, 20, 10));
        seq.track(QStringLiteral("A1"))->locked = true;
        QVERIFY(!seq.moveSpan(both, 20, 10, 0));
    }

    void displayJson() {
        Timeline::Display d;
        d.waveform = true;
        d.automation = model::AutomationParam::Pan;
        d.hiddenMarkers = {QStringLiteral("Red")};
        d.segmentSelectionColor = QStringLiteral("#112233");
        d.viewType = Timeline::ViewType::HeadsTails;
        d.waveformZoom = 2;
        d.trackControlPanel = true;
        QCOMPARE(Timeline::displayFromJson(Timeline::displayToJson(d)), d);
        QCOMPARE(Timeline::displayFromJson({}), Timeline::Display());
    }

    void views() {
        Fixture f;
        MainWindow& window = *f.window;
        Timeline& timeline = window.timeline();
        QCOMPARE(timeline.currentView(), QString());
        QToolButton* button = timeline.toolButton(QStringLiteral("viewMenu"));
        QVERIFY(button && button->isEnabled());
        QCOMPARE(button->text(), QStringLiteral("Untitled"));

        // Save As from the View Menu (p.604).
        timeline.setWaveform(true);
        window.saveTimelineView(QStringLiteral("2waves"), false);
        settle();
        QCOMPARE(timeline.currentView(), QStringLiteral("2waves"));
        QCOMPARE(button->text(), QStringLiteral("2waves"));
        timeline.setDisplay(Timeline::Display());
        timeline.setTrackControlPanel(true);
        window.saveTimelineView(QStringLiteral("1basic"), false);
        settle();
        // "Media Composer sorts the Timeline views alphabetically" (p.605).
        QCOMPARE(window.timelineViews(), (QStringList{"1basic", "2waves"}));
        QCOMPARE(timeline.views(), (QStringList{"1basic", "2waves"}));

        // Choosing one applies it.
        std::unique_ptr<QMenu> menu(timeline.viewMenu(false, &timeline));
        QVERIFY(menuItem(menu.get(), QStringLiteral("1basic"))->isChecked());
        QVERIFY(menuItem(menu.get(), QStringLiteral("Save As...")));
        menuItem(menu.get(), QStringLiteral("2waves"))->trigger();
        settle();
        QCOMPARE(timeline.currentView(), QStringLiteral("2waves"));
        QVERIFY(timeline.display().waveform);
        QVERIFY(!timeline.display().trackControlPanel);

        // Alt: "<view> Replace" (p.604).
        timeline.setTrackControlPanel(true);
        std::unique_ptr<QMenu> replace(timeline.viewMenu(true, &timeline));
        QVERIFY(!menuItem(replace.get(), QStringLiteral("Save As...")));
        menuItem(replace.get(), QStringLiteral("2waves Replace"))->trigger();
        settle();
        timeline.setDisplay(Timeline::Display());
        window.chooseTimelineView(QStringLiteral("1basic"));
        settle();
        QVERIFY(!timeline.display().waveform);
        window.chooseTimelineView(QStringLiteral("2waves"));
        settle();
        QVERIFY(timeline.display().waveform && timeline.display().trackControlPanel);

        // The view is a Timeline View User setting (p.604, p.1137).
        const model::SettingsFile& user = window.settings().file(model::SettingsScope::User);
        QCOMPARE(user.entries.at(user.activeIndex(QStringLiteral("Timeline View"))).name, QStringLiteral("2waves"));

        // TV1-TV8: the views in order unless chosen in the Command Palette (p.605).
        QCOMPARE(window.timelineViewButtons().mid(0, 3), (QStringList{"1basic", "2waves", ""}));
        f.commands.action(QStringLiteral("timeline.view.tv1"))->trigger();
        settle();
        QCOMPARE(timeline.currentView(), QStringLiteral("1basic"));
        QComboBox* tv1 = window.commandPalette().timelineViewMenu(0);
        QVERIFY(tv1);
        QCOMPARE(tv1->count(), 3); // None and the two views
        tv1->setCurrentIndex(tv1->findData(QStringLiteral("2waves")));
        settle();
        QCOMPARE(window.timelineViewButtons().at(0), QStringLiteral("2waves"));
        f.commands.action(QStringLiteral("timeline.view.tv1"))->trigger();
        settle();
        QCOMPARE(timeline.currentView(), QStringLiteral("2waves"));
    }

    void heads() {
        Fixture f;
        MainWindow& window = *f.window;
        Timeline& timeline = window.timeline();
        TimelineView* view = timeline.view();
        QVERIFY(!timeline.showsHeads());
        QMenu* types = timeline.fastMenu()->findChild<QMenu*>(QStringLiteral("viewType"));
        QVERIFY(types && types->isEnabled());
        menuItem(types, QStringLiteral("Heads"))->trigger();
        QVERIFY(timeline.showsHeads());
        QCOMPARE(timeline.headsSpans().size(), 3);
        // A click goes to the clip's head.
        QTest::mouseClick(view, Qt::LeftButton, {}, view->headsBox(1, 3).center());
        QCOMPARE(window.composer().state(MonitorId::Record).position(), 10);
        // Alt+drag clip a after c (p.639): every selected track, one undo step.
        const QPoint from = view->headsBox(0, 3).center();
        const QPoint to(view->headsBox(2, 3).right() + 3, from.y());
        QTest::mousePress(view, Qt::LeftButton, Qt::AltModifier, from);
        QMouseEvent move(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
        QCoreApplication::sendEvent(view, &move);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::AltModifier, to);
        QCOMPARE(names(recorded(window), QStringLiteral("V1")), (QStringList{"b", "c", "a"}));
        QCOMPARE(names(recorded(window), QStringLiteral("A1")), (QStringList{"b", "c", "a"}));
        timeline.perform(QStringLiteral("edit.undo"));
        QCOMPARE(names(recorded(window), QStringLiteral("V1")), (QStringList{"a", "b", "c"}));
        // Only the selected tracks are cut and moved.
        timeline.patch().setRecordSelected(QStringLiteral("A1"), false);
        QVERIFY(timeline.moveHeads(2, 0));
        QCOMPARE(names(recorded(window), QStringLiteral("V1")), (QStringList{"c", "a", "b"}));
        QCOMPARE(names(recorded(window), QStringLiteral("A1")), (QStringList{"a", "b", "c"}));
        // Heads Tails, then back to the tracks by choosing it again.
        menuItem(timeline.fastMenu()->findChild<QMenu*>(QStringLiteral("viewType")), QStringLiteral("Heads Tails"))->trigger();
        QCOMPARE(timeline.display().viewType, Timeline::ViewType::HeadsTails);
        QVERIFY(view->headsBox(0, 3).width() > view->headsBox(0, 3).height() * 3);
        QAction* again = menuItem(timeline.fastMenu()->findChild<QMenu*>(QStringLiteral("viewType")), QStringLiteral("Heads Tails"));
        QVERIFY(again->isChecked());
        again->trigger();
        QCOMPARE(timeline.display().viewType, Timeline::ViewType::Standard);
        // "Heads and Tails view is disabled when you are displaying material from the Source monitor" (p.603).
        timeline.setViewType(Timeline::ViewType::Heads);
        timeline.setShowSource(true);
        QVERIFY(!timeline.showsHeads());
        QVERIFY(!timeline.fastMenu()->findChild<QMenu*>(QStringLiteral("viewType"))->isEnabled());
    }
};

QTEST_MAIN(TestTimelineViews)
#include "tst_timeline_views.moc"
