// Timecode Burn-In (ROADMAP 6.3d1; docs/spec/burn-in.md): the displays' text from the sequence, the
// source clips and the bins, and the displays drawn over the picture. A GUI test: drawing text needs
// fonts.
#include "lumenis/media/compositor.h"
#include "lumenis/model/sequence.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::media;
using namespace lumenis::model;

namespace {

Segment clip(const QString& id, std::int64_t length, std::int64_t in = 0) {
    Segment s;
    s.binId = QStringLiteral("bin");
    s.clipId = id;
    s.name = id;
    s.sourceTrack = QStringLiteral("V1");
    s.length = length;
    s.sourceIn = in;
    return s;
}

// V1: "red" (frames 0-9, from frame 5 of its clip) then "blue" (10-19); V2: filler with the effect.
struct Rig {
    Sequence seq = Sequence::withDefaultTracks();
    Composition c;
    Effect effect{QStringLiteral("Generator"), QStringLiteral("Timecode Burn-In"), {}};

    Rig() {
        seq.addTrack(TrackKind::Video);
        seq.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 10, 5), clip(QStringLiteral("blue"), 10)});
        Segment top = Segment::filler(20);
        top.effect = effect;
        seq.splice(QStringLiteral("V2"), 0, {top});
        seq.track(QStringLiteral("V1"))->segments[0].note = QStringLiteral("check focus");
        c.sequence = &seq;
        c.tracks = {QStringLiteral("V1"), QStringLiteral("V2")};
        c.size = QSize(192, 108);
        c.precision = EffectPrecision::Bits32;
        c.burnIn.sequence.name = QStringLiteral("Cut 3");
        c.burnIn.sequence.kind = BinItemKind::Sequence;
        c.burnIn.sequence.editRate = Rational{25, 1};
        c.burnIn.sequence.fields.insert(QStringLiteral("Start"), QStringLiteral("10:00:00:00"));
        c.burnIn.sequence.fields.insert(QStringLiteral("Comments"), QStringLiteral("for review"));
        c.burnIn.clip = [](const Segment& s) -> std::optional<BinItem> {
            BinItem item;
            item.name = s.clipId + QStringLiteral(" clip");
            item.editRate = Rational{25, 1};
            item.durationFrames = 100;
            item.fields.insert(QStringLiteral("Start"), QStringLiteral("01:00:00:00"));
            item.fields.insert(QStringLiteral("Tape"), s.clipId == QLatin1String("red") ? QStringLiteral("A001") : QStringLiteral("B002"));
            return item;
        };
        c.source = [](const Segment& s, std::int64_t, QSize size) -> std::optional<QImage> {
            QImage image(size, QImage::Format_RGB32);
            image.fill(s.clipId == QLatin1String("red") ? QColor(255, 0, 0) : QColor(0, 0, 255));
            return image;
        };
    }
    // Display 1's settings.
    void set(const QJsonObject& display1, const QJsonObject& notes = {}) {
        QJsonObject all{{QStringLiteral("Display 1"), display1}};
        if (!notes.isEmpty()) all.insert(QStringLiteral("Notes"), notes);
        effect.parameters = QJsonObject{{QStringLiteral("Timecode Burn-In Effect Parameters"), all}};
        seq.track(QStringLiteral("V2"))->segments[0].effect = effect;
    }
    QString text(std::int64_t frame, int display = 1) {
        return burnInText(*seq.track(QStringLiteral("V2"))->segments[0].effect, display, c, QStringLiteral("V2"), frame);
    }
};

} // namespace

class TestBurnIn : public QObject {
    Q_OBJECT

private slots:
    void timecode() {
        // Display 1 shows the sequence's timecode with its label until set (ours; "Mas TC1 01:00:01:16",
        // p.506).
        Rig rig;
        QCOMPARE(rig.text(3), QStringLiteral("Mas TC1 10:00:00:03"));
        QCOMPARE(rig.text(3, 2), QString()); // Displays 2 and 3 are off
        rig.set({{QStringLiteral("Display Label"), false}});
        QCOMPARE(rig.text(3), QStringLiteral("10:00:00:03"));
        // The Type menu (p.506).
        rig.set({{QStringLiteral("Display Label"), false}, {QStringLiteral("Type"), QStringLiteral("Duration")}});
        QCOMPARE(rig.text(3), QStringLiteral("00:00:00:20"));
        rig.set({{QStringLiteral("Display Label"), false}, {QStringLiteral("Type"), QStringLiteral("Remaining")}});
        QCOMPARE(rig.text(3), QStringLiteral("00:00:00:16"));
        rig.set({{QStringLiteral("Display Label"), false}, {QStringLiteral("Type"), QStringLiteral("Absolute")}});
        QCOMPARE(rig.text(3), QStringLiteral("00:00:00:03"));
        rig.set({{QStringLiteral("Display Label"), false}, {QStringLiteral("Type"), QStringLiteral("Mark IN")}});
        QCOMPARE(rig.text(3), QString()); // no IN
        rig.c.burnIn.markIn = 4;
        QCOMPARE(rig.text(3), QStringLiteral("10:00:00:04"));
        // Disabled: nothing.
        rig.set({{QStringLiteral("Enable"), false}});
        QCOMPARE(rig.text(3), QString());
    }

    void sourceReaders() {
        Rig rig;
        // Source Timecode "of the source clip at the current location", from V1 (p.505, p.507).
        rig.set({{QStringLiteral("Reader"), QStringLiteral("Source Timecode")}, {QStringLiteral("Source Track"), QStringLiteral("V1")}});
        QCOMPARE(rig.text(2), QStringLiteral("Src TC1 01:00:00:07"));
        QCOMPARE(rig.text(12), QStringLiteral("Src TC1 01:00:00:02"));
        // The Current Track (V2) is filler: nothing.
        rig.set({{QStringLiteral("Reader"), QStringLiteral("Source Timecode")}});
        QCOMPARE(rig.text(2), QString());
        // Source Clip Name, Sequence Name, the bin columns, Timeline Clip Text (p.506).
        rig.set({{QStringLiteral("Reader"), QStringLiteral("Source Clip Name")}, {QStringLiteral("Source Track"), QStringLiteral("V1")}});
        QCOMPARE(rig.text(12), QStringLiteral("blue clip"));
        rig.set({{QStringLiteral("Reader"), QStringLiteral("Sequence Name")}});
        QCOMPARE(rig.text(0), QStringLiteral("Cut 3"));
        rig.set({{QStringLiteral("Reader"), QStringLiteral("Source Bin Column")}, {QStringLiteral("Source Track"), QStringLiteral("V1")},
                 {QStringLiteral("Column Name"), QStringLiteral("Tape")}});
        QCOMPARE(rig.text(1), QStringLiteral("A001"));
        rig.set({{QStringLiteral("Reader"), QStringLiteral("Sequence Bin Column")}, {QStringLiteral("Column Name"), QStringLiteral("Comments")}});
        QCOMPARE(rig.text(1), QStringLiteral("for review"));
        rig.set({{QStringLiteral("Reader"), QStringLiteral("Timeline Clip Text")}, {QStringLiteral("Source Track"), QStringLiteral("V1")}});
        QCOMPARE(rig.text(1), QStringLiteral("check focus"));
        QCOMPARE(rig.text(15), QString()); // "will not show anything on clips where there are no comments"
        // Frames: the sequence's, or the clip's with Local Frame Count, from Start Counting From (p.507).
        rig.set({{QStringLiteral("Reader"), QStringLiteral("Frames")}, {QStringLiteral("Display Label"), false}});
        QCOMPARE(rig.text(12), QStringLiteral("12"));
        rig.set({{QStringLiteral("Reader"), QStringLiteral("Frames")}, {QStringLiteral("Display Label"), false},
                 {QStringLiteral("Start Counting From"), 1001}});
        QCOMPARE(rig.text(12), QStringLiteral("1013"));
        // Edgecode needs film material: nothing.
        rig.set({{QStringLiteral("Reader"), QStringLiteral("Edgecode")}});
        QCOMPARE(rig.text(1), QString());
        // The Notes Track shows its text (p.508).
        rig.set({}, {{QStringLiteral("Text"), QStringLiteral("DRAFT")}});
        QCOMPARE(rig.text(1, 0), QStringLiteral("DRAFT"));
    }

    void drawn() {
        // The display is drawn over the picture: its background box at its anchor (5%, 85% until set),
        // the rest of the frame as V1 shows it.
        Rig rig;
        const QImage frame = composeImage(rig.c, 3);
        QCOMPARE(frame.pixelColor(150, 20), QColor(255, 0, 0));
        bool changed = false;
        for (int x = 10; x < 60 && !changed; ++x) changed = frame.pixelColor(x, 95) != QColor(255, 0, 0);
        QVERIFY(changed);
        // Disabled: nothing drawn.
        rig.set({{QStringLiteral("Enable"), false}});
        const QImage plain = composeImage(rig.c, 3);
        for (int x = 0; x < 192; x += 7) QCOMPARE(plain.pixelColor(x, 95), QColor(255, 0, 0));
    }
};

QTEST_MAIN(TestBurnIn)
#include "tst_burn_in.moc"
