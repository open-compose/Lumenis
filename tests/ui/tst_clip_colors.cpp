// Clip colours in the Timeline (Editing Guide p.383, p.594-598, p.613-614;
// docs/spec/timeline-colors.md): which colour a segment takes and in which priority
// (model/clip_coloring.h), the Clip Color dialog, Set Local Clip Color as an undo step, the Select
// menu's colour choices, and Show Source/Record colors in Timeline.
#include "lumenis/model/bin.h"
#include "lumenis/model/clip_coloring.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/clip_color_dialog.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QCheckBox>
#include <QHash>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;
using lumenis::model::ClipColorOptions;
using lumenis::model::ClipFacts;

class TestClipColors : public QObject {
    Q_OBJECT

private:
    // A Record monitor with a sequence of clips A (red in its bin), B (no colour) and A again on V1.
    struct Rig {
        Composer composer;
        Timeline timeline{composer};
        QHash<QString, BinItem> items;
        BinItem sequence;

        Rig() {
            composer.setItemResolver([this](const QString&, const QString& id) -> std::optional<BinItem> {
                if (items.contains(id)) return items.value(id);
                return std::nullopt;
            });
            timeline.setSequenceStore([this](const QString&, const BinItem& item, bool) {
                items.insert(item.id, item);
                return true;
            });
            BinItem a = clip("A", "#c83232");
            BinItem b = clip("B", "");
            model::Sequence seq;
            seq.ensureTrack(QStringLiteral("V1"))->segments = {segment(a, 10), segment(b, 10), segment(a, 10)};
            sequence.id = model::newItemId();
            sequence.name = QStringLiteral("Seq");
            sequence.kind = BinItemKind::Sequence;
            sequence.editRate = Rational(25);
            seq.storeIn(sequence);
            items.insert(sequence.id, sequence);
            composer.load(MonitorId::Record, QStringLiteral("bin"), sequence);
            timeline.resize(900, 300);
        }

        BinItem clip(const char* name, const char* color) {
            BinItem c;
            c.id = model::newItemId();
            c.name = QString::fromLatin1(name);
            c.kind = BinItemKind::MasterClip;
            c.editRate = Rational(25);
            c.durationFrames = 100;
            c.color = QString::fromLatin1(color);
            c.fields.insert(QStringLiteral("FPS"), QStringLiteral("25"));
            items.insert(c.id, c);
            return c;
        }

        static model::Segment segment(const BinItem& c, std::int64_t length) {
            model::Segment s;
            s.clipId = c.id;
            s.binId = QStringLiteral("bin");
            s.name = c.name;
            s.sourceTrack = QStringLiteral("V1");
            s.length = length;
            return s;
        }

        model::Sequence shown() const {
            return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        }
        const model::Segment& at(const model::Sequence& s, int i) const { return s.track(QStringLiteral("V1"))->segments.at(i); }
    };

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // The dialog's order is the priority (p.596); filler has none; Source skips sequences, groups,
    // motion effects and titles.
    void priority() {
        model::Segment s;
        s.clipId = QStringLiteral("c");
        s.length = 5;
        ClipColorOptions o = ClipColorOptions::defaults();
        QVERIFY(o.offline && o.proxy && o.source && !o.local && !o.linked && !o.mixedRates && !o.sdHd);
        QCOMPARE(o.offlineColor, QStringLiteral("#c83232"));
        ClipFacts f;
        f.sourceColor = QStringLiteral("#3264c8");
        QCOMPARE(model::timelineClipColor(o, s, f), f.sourceColor);
        // A missing clip, or offline media, is offline and wins.
        QCOMPARE(model::timelineClipColor(o, s, std::nullopt), o.offlineColor);
        f.offline = true;
        QCOMPARE(model::timelineClipColor(o, s, f), o.offlineColor);
        f.offline = false;
        // Timeline Local over Source, once it is on.
        s.localColor = QStringLiteral("#8cc83c");
        QCOMPARE(model::timelineClipColor(o, s, f), f.sourceColor);
        o.local = true;
        QCOMPARE(model::timelineClipColor(o, s, f), s.localColor);
        // Linked, Mixed Rates and SD/HD come before Timeline Local.
        f.otherDefinition = true;
        o.sdHd = true;
        QCOMPARE(model::timelineClipColor(o, s, f), o.sdHdColor);
        f.rate = QStringLiteral("29.97");
        o.mixedRates = true;
        QCOMPARE(model::timelineClipColor(o, s, f), o.rateColors.value(QStringLiteral("29.97")));
        f.linked = true;
        o.linked = true;
        QCOMPARE(model::timelineClipColor(o, s, f), o.linkedColor);
        // Nothing on: the track's own colour.
        ClipColorOptions none = o;
        none.offline = none.linked = none.mixedRates = none.sdHd = none.local = none.source = false;
        QVERIFY(model::timelineClipColor(none, s, f).isEmpty());
        QVERIFY(model::timelineClipColor(o, model::Segment::filler(5), f).isEmpty());
        // No source colour for a title or a sequence.
        ClipFacts title;
        title.kind = BinItemKind::Title;
        title.sourceColor = QStringLiteral("#3264c8");
        model::Segment plain;
        plain.clipId = QStringLiteral("t");
        QVERIFY(model::timelineClipColor(ClipColorOptions::defaults(), plain, title).isEmpty());
        // The choices round-trip, and a missing member keeps its default.
        QCOMPARE(ClipColorOptions::fromJson(o.toJson()), o);
        QCOMPARE(ClipColorOptions::fromJson({}), ClipColorOptions::defaults());
        QCOMPARE(model::mixedRates().size(), 10);
        // The local colour is kept with the segment.
        model::Sequence seq;
        seq.ensureTrack(QStringLiteral("V1"))->segments = {s};
        QCOMPARE(model::Sequence::fromJson(seq.toJson()).track(QStringLiteral("V1"))->segments.first().localColor, s.localColor);
    }

    // Resolution Tracking (p.1112-1114; 8.6b): off at first; on, the MultiRez display's marks come first, blue, red and
    // yellow; matched clips and an inactive display fall through to the other options.
    void resolutionTracking() {
        model::Segment s;
        s.clipId = QStringLiteral("c");
        s.length = 5;
        ClipColorOptions o = ClipColorOptions::defaults();
        QVERIFY(!o.resolution);
        ClipFacts f;
        f.sourceColor = QStringLiteral("#8cc83c");
        f.resolution = ClipFacts::Resolution::Mismatched;
        QCOMPARE(model::timelineClipColor(o, s, f), f.sourceColor);
        o.resolution = true;
        QCOMPARE(model::timelineClipColor(o, s, f), o.mismatchColor);
        f.resolution = ClipFacts::Resolution::Unavailable;
        QCOMPARE(model::timelineClipColor(o, s, f), o.unavailableColor);
        f.resolution = ClipFacts::Resolution::Offline;
        QCOMPARE(model::timelineClipColor(o, s, f), o.offlineColor);
        f.resolution = ClipFacts::Resolution::Matched;
        QCOMPARE(model::timelineClipColor(o, s, f), f.sourceColor);
        // Over every other option, offline included.
        f.resolution = ClipFacts::Resolution::Unavailable;
        f.offline = true;
        QCOMPARE(model::timelineClipColor(o, s, f), o.unavailableColor);
        QCOMPARE(ClipColorOptions::fromJson(o.toJson()), o);
        QCOMPARE(ClipColorOptions::defaults().mismatchColor, QStringLiteral("#2d6ee0"));
    }

    // Bin colours show with Source on (the default); the dialog turns kinds on and off and changes
    // colours (p.595-597).
    void dialogAndPainting() {
        Rig rig;
        const model::Sequence seq = rig.shown();
        // Clips without media are offline: red, over their bin colour.
        QCOMPARE(rig.timeline.segmentColor(rig.at(seq, 0)), QStringLiteral("#c83232"));
        QVERIFY(rig.timeline.clipFacts(rig.at(seq, 0))->offline);
        ClipColorOptions o = rig.timeline.clipColorOptions();
        o.offline = false;
        rig.timeline.setClipColorOptions(o);
        QCOMPARE(rig.timeline.segmentColor(rig.at(seq, 0)), QStringLiteral("#c83232")); // A's own red
        QVERIFY(rig.timeline.segmentColor(rig.at(seq, 1)).isEmpty());                    // B has none
        QSignalSpy changed(&rig.timeline, &Timeline::clipColorOptionsChanged);
        rig.timeline.setClipColorAsk([](ClipColorDialog& d) {
            QCheckBox* resolution = d.option(QStringLiteral("resolution"));
            QCheckBox* proxy = d.option(QStringLiteral("proxy"));
            QCheckBox* source = d.option(QStringLiteral("source"));
            QCheckBox* offline = d.option(QStringLiteral("offline"));
            if (!resolution || !proxy || !source || !offline) return false;
            // Resolution Tracking and Proxy are dimmed; Proxy stays on (p.598).
            if (resolution->isEnabled() || proxy->isEnabled() || !proxy->isChecked()) return false;
            source->setChecked(false);
            offline->setChecked(true);
            d.setColor(QStringLiteral("offlineColor"), QStringLiteral("#d2c832"));
            d.setColor(QStringLiteral("25"), QStringLiteral("#3264c8"));
            return true;
        });
        rig.timeline.openClipColorDialog();
        QCOMPARE(changed.count(), 1);
        QVERIFY(!rig.timeline.clipColorOptions().source);
        QCOMPARE(rig.timeline.segmentColor(rig.at(seq, 0)), QStringLiteral("#d2c832"));
        QCOMPARE(rig.timeline.clipColorOptions().rateColors.value(QStringLiteral("25")), QStringLiteral("#3264c8"));
        // Default Colors puts the colours back but not the choices; Cancel changes nothing.
        rig.timeline.setClipColorAsk([](ClipColorDialog& d) {
            d.defaultColors();
            return true;
        });
        rig.timeline.openClipColorDialog();
        QCOMPARE(rig.timeline.clipColorOptions().offlineColor, QStringLiteral("#c83232"));
        QVERIFY(!rig.timeline.clipColorOptions().source);
        rig.timeline.setClipColorAsk([](ClipColorDialog& d) {
            if (QCheckBox* source = d.option(QStringLiteral("source"))) source->setChecked(true);
            return false;
        });
        rig.timeline.openClipColorDialog();
        QVERIFY(!rig.timeline.clipColorOptions().source);
        // The Fast menu's entry is there and works.
        QMenu* fast = rig.timeline.fastMenu();
        bool found = false;
        for (QAction* a : fast->actions()) found = found || (a->text() == QLatin1String("Clip Color...") && a->isEnabled());
        QVERIFY(found);
    }

    // Set Local Clip Color (p.598): the selected segments, one undo step; None takes it off.
    void localColors() {
        Rig rig;
        ClipColorOptions o = rig.timeline.clipColorOptions();
        o.offline = false;
        o.local = true;
        rig.timeline.setClipColorOptions(o);
        QVERIFY(!rig.timeline.setLocalClipColor(QStringLiteral("#8cc83c"))); // nothing selected
        QVERIFY(!rig.timeline.lastMessage().isEmpty());
        rig.timeline.setSelection({{QStringLiteral("V1"), 10}});
        QVERIFY(rig.timeline.setLocalClipColor(QStringLiteral("#8cc83c")));
        model::Sequence seq = rig.shown();
        QCOMPARE(rig.at(seq, 1).localColor, QStringLiteral("#8cc83c"));
        QCOMPARE(rig.timeline.segmentColor(rig.at(seq, 1)), QStringLiteral("#8cc83c"));
        // Over the bin colour too.
        rig.timeline.setSelection({{QStringLiteral("V1"), 0}});
        QVERIFY(rig.timeline.setLocalClipColor(QStringLiteral("#3cbed2")));
        QCOMPARE(rig.timeline.segmentColor(rig.at(rig.shown(), 0)), QStringLiteral("#3cbed2"));
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(rig.at(rig.shown(), 0).localColor.isEmpty());
        QCOMPARE(rig.timeline.segmentColor(rig.at(rig.shown(), 0)), QStringLiteral("#c83232"));
        // None.
        rig.timeline.setSelection({{QStringLiteral("V1"), 10}});
        QVERIFY(rig.timeline.setLocalClipColor(QString()));
        QVERIFY(rig.at(rig.shown(), 1).localColor.isEmpty());
    }

    // The Select menu's colour choices (p.613-614).
    void selectByColor() {
        Rig rig;
        if (!rig.timeline.patch().isRecordSelected(QStringLiteral("V1"))) rig.timeline.toggleRecordTrack(QStringLiteral("V1"));
        const QList<model::SegmentRef> as{{QStringLiteral("V1"), 0}, {QStringLiteral("V1"), 20}};
        rig.timeline.setSelection({{QStringLiteral("V1"), 0}});
        rig.timeline.selectSegments(Timeline::SelectWhere::SameSourceColor);
        QCOMPARE(rig.timeline.selection(), as);
        rig.timeline.selectSegments(Timeline::SelectWhere::NoSourceColor);
        QCOMPARE(rig.timeline.selection(), (QList<model::SegmentRef>{{QStringLiteral("V1"), 10}}));
        // None of them has media.
        rig.timeline.selectSegments(Timeline::SelectWhere::Offline);
        QCOMPARE(rig.timeline.selection().size(), 3);
        rig.timeline.setSelection({{QStringLiteral("V1"), 20}});
        QVERIFY(rig.timeline.setLocalClipColor(QStringLiteral("#8cc83c")));
        rig.timeline.setSelection({{QStringLiteral("V1"), 20}});
        rig.timeline.selectSegments(Timeline::SelectWhere::SameLocalColor);
        QCOMPARE(rig.timeline.selection(), (QList<model::SegmentRef>{{QStringLiteral("V1"), 20}}));
    }

    // Show Source/Record colors in Timeline (p.383).
    void sourceRecordButtons() {
        Rig rig;
        QVERIFY(!rig.timeline.sourceRecordColors());
        rig.timeline.setSourceRecordColors(true);
        QVERIFY(rig.timeline.sourceRecordColors());
        rig.timeline.show();
        QVERIFY(QTest::qWaitForWindowExposed(&rig.timeline));
        const QImage shot = rig.timeline.grab().toImage();
        // The selected record button is blue (the record bar's colour).
        bool blue = false;
        for (int y = 0; y < shot.height() && !blue; ++y) {
            for (int x = 0; x < 80 && !blue; ++x) blue = shot.pixel(x, y) == qRgb(0x2b, 0x55, 0x76);
        }
        if (rig.timeline.patch().isRecordSelected(QStringLiteral("V1"))) QVERIFY(blue);
    }
};

QTEST_MAIN(TestClipColors)
#include "tst_clip_colors.moc"
