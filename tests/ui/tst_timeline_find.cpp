// The Timeline Quick Find (7.4b2; docs/spec/timeline-find.md; Editing Guide p.645-646): two clips spliced into a
// sequence, found by Visible Timeline Text, Clip Name, Resolution, Timeline Clip Notes and Markers, to the right and
// to the left, and the search options menu's None, the options and All.
#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/model/marker.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QLineEdit>
#include <QMenu>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

bool writeMovie(const QString& path) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 36;
    s.frameRate = Rational(25);
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    for (int i = 0; i < 10; ++i) {
        QImage image(64, 36, QImage::Format_RGBA8888);
        image.fill(Qt::darkGray);
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

} // namespace

class TestTimelineFind : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void quickFind() {
        QTemporaryDir dir;
        const QString harbor = dir.filePath(QStringLiteral("Harbor.mov"));
        const QString bridge = dir.filePath(QStringLiteral("Bridge.mov"));
        QVERIFY(writeMovie(harbor));
        QVERIFY(writeMovie(bridge));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Find"));
        project.settings.name = QStringLiteral("Find");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {harbor, bridge});
        QCOMPARE(ids.size(), 2);
        // Bridge's resolution, as the Video column says it.
        model::BinItem second = *bin->bin().item(ids.at(1));
        second.fields.insert(QStringLiteral("Video"), QStringLiteral("DNxHR SQ"));
        bin->replaceItem(second);
        Composer& composer = window.composer();
        Timeline& timeline = window.timeline();
        for (const QString& id : ids) {
            composer.loadDefault(bin->nodeId(), *bin->bin().item(id));
            composer.state(MonitorId::Source).setPosition(0);
            QVERIFY(timeline.perform(QStringLiteral("edit.overwrite")) || timeline.perform(QStringLiteral("edit.splice_in")));
            composer.state(MonitorId::Record).setPosition(composer.state(MonitorId::Record).duration());
        }
        QVERIFY(composer.state(MonitorId::Record).current());
        QCOMPARE(composer.state(MonitorId::Record).duration(), std::int64_t(20));
        // A clip note on Bridge, a marker at frame 4.
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Note"), [](model::Sequence& s) {
            s.track(QStringLiteral("V1"))->segments.last().note = QStringLiteral("check the flare");
            return true;
        }));
        timeline.setMarkerDialogEnabled(false);
        composer.setActiveMonitor(MonitorId::Record);
        composer.state(MonitorId::Record).setPosition(4);
        QVERIFY(timeline.addMarker());
        {
            const model::LoadedClip* record = composer.state(MonitorId::Record).current();
            QList<model::Marker> markers = model::markersOf(record->item);
            QCOMPARE(markers.size(), 1);
            markers.first().comment = QStringLiteral("Seagull");
            QVERIFY(timeline.changeMarker(MonitorId::Record, markers.first()));
        }

        // The field and the buttons (p.646).
        auto* field = timeline.findChild<QLineEdit*>(QStringLiteral("timelineSearch"));
        QVERIFY(field && field->isEnabled());
        auto* right = timeline.findChild<QToolButton*>(QStringLiteral("findRight"));
        auto* left = timeline.findChild<QToolButton*>(QStringLiteral("findLeft"));
        QVERIFY(right && right->isEnabled() && left && left->isEnabled());

        auto at = [&] { return composer.state(MonitorId::Record).position(); };
        auto go = [&](std::int64_t frame) { composer.state(MonitorId::Record).setPosition(frame); };

        // Visible Timeline Text: the clip names the Timeline shows.
        field->setText(QStringLiteral("bridge"));
        go(0);
        right->click();
        QCOMPARE(at(), std::int64_t(10));
        go(15);
        left->click();
        QCOMPARE(at(), std::int64_t(10));
        go(12);
        QVERIFY(!timeline.findNext(true)); // nothing further on: the position stays
        QCOMPARE(at(), std::int64_t(12));

        // The menu: None, then one option at a time, then All.
        std::unique_ptr<QMenu> menu(timeline.findOptionsMenu(&timeline));
        menu->findChild<QAction*>(QStringLiteral("findNone"))->trigger();
        QCOMPARE(timeline.findOptions(), (Timeline::FindOptions{false, false, false, false, false}));
        go(0);
        QVERIFY(!timeline.findNext(true));
        menu.reset(timeline.findOptionsMenu(&timeline));
        QVERIFY(!menu->findChild<QAction*>(QStringLiteral("findVisibleText"))->isChecked());
        menu->findChild<QAction*>(QStringLiteral("findResolution"))->trigger();
        QVERIFY(timeline.findOptions().resolution);
        timeline.setFindText(QStringLiteral("dnxhr"));
        QVERIFY(timeline.findNext(true));
        QCOMPARE(at(), std::int64_t(10));
        timeline.setFindOptions({false, false, false, true, false});
        timeline.setFindText(QStringLiteral("FLARE"));
        go(0);
        QVERIFY(timeline.findNext(true));
        QCOMPARE(at(), std::int64_t(10));
        timeline.setFindOptions({false, false, false, false, true});
        timeline.setFindText(QStringLiteral("seagull"));
        go(15);
        QVERIFY(timeline.findNext(false));
        QCOMPARE(at(), std::int64_t(4));
        timeline.setFindOptions({false, false, true, false, false});
        timeline.setFindText(QStringLiteral("Harbor"));
        go(15);
        QVERIFY(timeline.findNext(false));
        QCOMPARE(at(), std::int64_t(0));
        menu->findChild<QAction*>(QStringLiteral("findAll"))->trigger();
        QCOMPARE(timeline.findOptions(), (Timeline::FindOptions{true, true, true, true, true}));
    }
};

QTEST_MAIN(TestTimelineFind)
#include "tst_timeline_find.moc"
