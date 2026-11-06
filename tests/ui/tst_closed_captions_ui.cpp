// Closed captions in the application (7.4b; docs/spec/closed-captions.md; Editing Guide p.232, p.983-985): an .scc file
// imported through the AncData Import dialog as a D-track only clip, spliced into a sequence's D1, shown in the Source
// and Record monitors with Show Captions and in the Timeline's D1 row (with a broken caption's cut), then exported as
// SCC and TTML.
#include "lumenis/ext/provider.h"
#include "lumenis/model/closed_captions.h"
#include "lumenis/ui/ancdata_import_dialog.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/export_dialog.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

class TestClosedCaptionsUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void captionsThroughTheApplication() {
        QTemporaryDir dir;
        // Two pop-on captions, written as an .scc file at 01:00:00;00 drop frame.
        const TimecodeFormat df{30, true};
        const std::int64_t hour = Timecode::parse("01:00:00;00", df)->frameCount();
        const QByteArray source = model::popOnCaptionData({{30, 60, QStringLiteral("First caption")}, {90, 120, QStringLiteral("Second\nrow two")}}, 150);
        const QString scc = dir.filePath(QStringLiteral("Show.scc"));
        {
            QFile f(scc);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(model::writeScc(source, hour, df).toLatin1());
        }
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Captions"));
        project.settings.name = QStringLiteral("Captions");
        project.settings.format.editRate = Rational(30000, 1001);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);

        // Import: "The starting timecode and drop frame checkbox will be automatically populated" (p.232).
        std::optional<std::int64_t> offered;
        bool offeredDrop = false;
        window.setAncDataImportHook([&](AncDataImportDialog& d) {
            offered = d.startFrame();
            offeredDrop = d.dropFrame();
            return true;
        });
        const QStringList ids = window.importFiles(binId, {scc}, QString());
        QCOMPARE(ids.size(), 1);
        QVERIFY(offered);
        QCOMPARE(*offered, hour + model::captionLoadingSpans(source).first().first);
        QVERIFY(offeredDrop);
        const model::BinItem clip = *bin->bin().item(ids.first());
        QCOMPARE(clip.fields.value(QStringLiteral("Tracks")), QStringLiteral("D1"));
        QVERIFY(model::hasCaptionData(clip));
        QCOMPARE(clip.fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00;%1").arg(model::captionLoadingSpans(source).first().first, 2, 10, QLatin1Char('0')));
        const std::int64_t shift = model::captionLoadingSpans(source).first().first; // the clip starts at the first line

        // Show Captions in the Source monitor (p.983).
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), clip);
        composer.state(MonitorId::Source).setPosition(40 - shift);
        QVERIFY(!composer.shownCaption(MonitorId::Source));
        {
            QMenu* menu = composer.contextMenu(MonitorId::Source);
            QAction* show = menu->findChild<QAction*>(QStringLiteral("showCaptions"));
            QVERIFY(show);
            QVERIFY(show->isCheckable());
            show->trigger();
            delete menu;
        }
        QVERIFY(composer.showsCaptions(MonitorId::Source));
        QVERIFY(!composer.showsCaptions(MonitorId::Record));
        auto caption = composer.shownCaption(MonitorId::Source);
        QVERIFY(caption);
        QCOMPARE(caption->text(), QStringLiteral("First caption"));
        composer.picture(MonitorId::Source)->repaint(); // paints the caption

        // Into a new sequence's D1.
        composer.state(MonitorId::Source).setPosition(0);
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        const model::LoadedClip* record = composer.state(MonitorId::Record).current();
        QVERIFY(record);
        model::Sequence seq = model::Sequence::fromItem(record->item);
        QVERIFY(seq.track(QStringLiteral("D1")));
        QCOMPARE(seq.duration(), clip.durationFrames);
        composer.setShowCaptions(MonitorId::Record, true);
        composer.state(MonitorId::Record).setPosition(100 - shift);
        caption = composer.shownCaption(MonitorId::Record);
        QVERIFY(caption);
        QCOMPARE(caption->text(), QStringLiteral("Second\nrow two"));
        QCOMPARE(caption->rows.last().row, 15);
        const Timeline::CaptionTrack& cc = window.timeline().captionTrack();
        QCOMPARE(cc.shown.size(), 2);
        QVERIFY(cc.broken.isEmpty());
        window.timeline().repaint();

        // "You can use the Timeline Quick Find text box to search for Closed Caption text" (p.985).
        Timeline& timeline = window.timeline();
        QVERIFY(timeline.findOptions().visibleText);
        timeline.setFindText(QStringLiteral("ROW TWO"));
        composer.state(MonitorId::Record).setPosition(0);
        QVERIFY(timeline.findNext(true));
        QCOMPARE(composer.state(MonitorId::Record).position(), cc.shown.last().start);
        QVERIFY(!timeline.findNext(true));
        timeline.setFindText(QStringLiteral("first"));
        QVERIFY(timeline.findNext(false));
        QCOMPARE(composer.state(MonitorId::Record).position(), cc.shown.first().start);
        timeline.setFindOptions({false, false, false, false, false}); // None
        composer.state(MonitorId::Record).setPosition(0);
        QVERIFY(!timeline.findNext(true));
        timeline.setFindOptions({});

        // A cut in the second caption's loading phase breaks it (p.984).
        const auto spans = model::captionLoadingSpans(model::clipCaptionData(clip));
        const auto& loading = *std::max_element(spans.begin() + 1, spans.end(), [](const auto& a, const auto& b) {
            return a.second - a.first < b.second - b.first;
        });
        const std::int64_t cut = (loading.first + loading.second) / 2;
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("Cut"), [&](model::Sequence& s) {
            model::Track* d = s.track(QStringLiteral("D1"));
            model::Segment second = d->segments.first();
            d->segments.first().length = cut;
            second.sourceIn += cut;
            second.length -= cut;
            d->segments.append(second);
            return true;
        }));
        QCOMPARE(window.timeline().captionTrack().broken, (QList<std::int64_t>{cut}));
        QCOMPARE(window.timeline().captionTrack().shown.size(), 2); // the same data, so the same captions
        window.timeline().repaint();

        // Export the closed caption text as SCC and TTML (p.985).
        window.binContainer().saveAll();
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({composer.state(MonitorId::Record).current()->item.id});
        auto exportAs = [&](const QString& format, const QString& to) {
            window.settings().setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), format);
            window.setExportHook([to](ExportAsDialog& dialog) {
                dialog.setPath(to);
                return true;
            });
            commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        };
        const QString sccOut = dir.filePath(QStringLiteral("Out.scc"));
        exportAs(QStringLiteral("scc"), sccOut);
        QFile back(sccOut);
        QVERIFY(back.open(QIODevice::ReadOnly));
        const auto parsed = model::parseScc(QString::fromLatin1(back.readAll()));
        QVERIFY(parsed);
        const QByteArray again = model::sccCaptionData(*parsed, parsed->startFrame(), Rational(30000, 1001));
        QCOMPARE(model::decodeCaptions(again).size(), 2);
        QCOMPARE(model::decodeCaptions(again).first().screen.text(), QStringLiteral("First caption"));

        const QString ttml = dir.filePath(QStringLiteral("Out"));
        exportAs(QStringLiteral("ttml"), ttml);
        QFile t(ttml + QStringLiteral(".ttml"));
        QVERIFY(t.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(t.readAll());
        QVERIFY2(text.contains(QStringLiteral(">First caption</p>")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral(">Second<br/>row two</p>")));
    }
};

QTEST_MAIN(TestClosedCaptionsUi)
#include "tst_closed_captions_ui.moc"
