// The SubCap effect's captions (7.4a; docs/spec/subcap.md, FX Guide ch.14): the Avid DS and EBU N19 (.stl) caption
// files read and written, a sequence's captions with overlaps resolved, a caption file imported into a SubCap segment
// subdividing it (and refused outside it), and synchronizing a change to the track or the sequence.
#include "lumenis/model/subcap.h"

#include <QJsonObject>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

Effect subcap(const QString& text = {}) {
    Effect e{QStringLiteral("Generator"), QStringLiteral("SubCap"), {}};
    if (!text.isEmpty()) setCaptionText(e, text);
    return e;
}

Segment filler(std::int64_t length, const std::optional<Effect>& effect = std::nullopt) {
    Segment s;
    s.length = length;
    s.effect = effect;
    return s;
}

} // namespace

class TestSubCap : public QObject {
    Q_OBJECT

private slots:
    void dsFiles() {
        // The example of p.379.
        const TimecodeFormat f = TimecodeFormat::forRate(Rational(25), false);
        const QString text = QStringLiteral("00:00:02:00 00:00:03:00\nBob, are you there?\n\n00:00:04:18 00:00:06:13\nYes, Sam.\nJust behind the car.\n");
        const auto captions = parseDsCaptions(text, f);
        QVERIFY(captions);
        QCOMPARE(captions->size(), 2);
        QCOMPARE(captions->at(0), (Caption{50, 75, QStringLiteral("Bob, are you there?")}));
        QCOMPARE(captions->at(1), (Caption{118, 163, QStringLiteral("Yes, Sam.\nJust behind the car.")}));
        QCOMPARE(parseDsCaptions(writeDsCaptions(*captions, f), f), captions);
        QString error;
        QVERIFY(!parseDsCaptions(QStringLiteral("00:00:03:00 00:00:02:00\nBackwards\n"), f, &error));
        QVERIFY(!error.isEmpty());
    }

    void stlFiles() {
        const QList<Caption> captions{{50, 75, QStringLiteral("Bob, are you there?")}, {118, 163, QStringLiteral("Yes, Sam.\nJust behind the car.")}};
        StlInfo info;
        info.originalProgramTitle = QStringLiteral("Harbor");
        info.publisher = QStringLiteral("Cutthulhu Pictures");
        info.countryOfOrigin = QStringLiteral("GBR");
        info.languageCode = QStringLiteral("09");
        info.userData = QStringLiteral("Notes");
        info.creationDate = QDate(2026, 11, 6);
        info.revisionDate = QDate(2026, 11, 7);
        const QByteArray data = writeStl(captions, 25, info);
        // The GSI block and a TTI block for each caption (EBU Tech 3264).
        QCOMPARE(data.size(), 1024 + 2 * 128);
        QCOMPARE(data.mid(3, 8), QByteArray("STL25.01"));
        QCOMPARE(data.mid(238, 5), QByteArray("00002"));
        int fps = 0;
        StlInfo back;
        const auto read = parseStl(data, &fps, &back);
        QVERIFY(read);
        QCOMPARE(fps, 25);
        QCOMPARE(*read, captions);
        QCOMPARE(back, info);
        // 30 frames a second; a short file refused.
        QCOMPARE(writeStl(captions, 30).mid(3, 8), QByteArray("STL30.01"));
        QVERIFY(!parseStl(QByteArray(100, ' ')));
    }

    void sequenceCaptionsAndImport() {
        Sequence s = Sequence::withDefaultTracks();
        QVERIFY(s.addTrack(TrackKind::Video));
        // V2: one SubCap effect over 200 frames; V1: a caption from 40 to 80.
        s.track(QStringLiteral("V2"))->segments = {filler(200, subcap())};
        s.track(QStringLiteral("V1"))->segments = {filler(40), filler(40, subcap(QStringLiteral("Lower"))), filler(120)};
        // Captions outside the effect: no import (p.380).
        QString error;
        QVERIFY(!importCaptions(s, QStringLiteral("V2"), 10, {{150, 250, QStringLiteral("Late")}}, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(s.track(QStringLiteral("V2"))->segments.size(), 1);
        QVERIFY(!importCaptions(s, QStringLiteral("V1"), 10, {{10, 20, QStringLiteral("Filler")}}, &error)); // no SubCap there
        // "Media Composer subdivides the effect at the timecode points specified by the file."
        QVERIFY(importCaptions(s, QStringLiteral("V2"), 10, {{50, 75, QStringLiteral("One")}, {118, 163, QStringLiteral("Two")}}, &error));
        const Track* v2 = s.track(QStringLiteral("V2"));
        QCOMPARE(v2->segments.size(), 5);
        QCOMPARE(captionText(*v2->segments[1].effect), QStringLiteral("One"));
        QCOMPARE(v2->segments[1].length, 25);
        QCOMPARE(captionText(*v2->segments[2].effect), QString());
        QCOMPARE(captionText(*v2->segments[3].effect), QStringLiteral("Two"));
        // The sequence's captions: V2's "One" (50-75) wins over V1's overlapping "Lower" (40-80) with the top winning;
        // the lower track's with the bottom winning.
        QList<Caption> all = sequenceCaptions(s);
        QCOMPARE(all.size(), 2);
        QCOMPARE(all.first().text, QStringLiteral("One"));
        all = sequenceCaptions(s, {}, false);
        QCOMPARE(all.first().text, QStringLiteral("Lower"));
        QCOMPARE(all.size(), 2); // Lower and Two
        QCOMPARE(sequenceCaptions(s, {QStringLiteral("V1")}).size(), 1);
    }

    void synchronize() {
        Sequence s = Sequence::withDefaultTracks();
        QVERIFY(s.addTrack(TrackKind::Video));
        s.track(QStringLiteral("V1"))->segments = {filler(10, subcap(QStringLiteral("a"))), filler(10, subcap(QStringLiteral("b")))};
        s.track(QStringLiteral("V2"))->segments = {filler(10, subcap(QStringLiteral("c")))};
        auto change = [&](const QString& name, const QJsonValue& v, const QString& mode) {
            Effect& e = *s.track(QStringLiteral("V1"))->segments[0].effect;
            const Effect before = e;
            QJsonObject p = e.parameters.value(QLatin1String(kSubCapCategory)).toObject();
            p.insert(name, v);
            e.parameters.insert(QLatin1String(kSubCapCategory), p);
            QJsonObject props = e.parameters.value(QLatin1String(kSubCapPropertiesCategory)).toObject();
            props.insert(QLatin1String(kSynchronizeKey), QJsonObject{{name, mode}});
            e.parameters.insert(QLatin1String(kSubCapPropertiesCategory), props);
            return synchronizeSubCap(s, QStringLiteral("V1"), 0, before);
        };
        auto valueOn = [&](const QString& track, int i, const QString& name) {
            return s.track(track)->segments[i].effect->parameters.value(QLatin1String(kSubCapCategory)).toObject().value(name);
        };
        // Sequence: every other SubCap effect; the caption text stays each one's own.
        QCOMPARE(change(QStringLiteral("Font Size"), 60, QStringLiteral("Sequence")), 2);
        QCOMPARE(valueOn(QStringLiteral("V2"), 0, QStringLiteral("Font Size")).toInt(), 60);
        QCOMPARE(captionText(*s.track(QStringLiteral("V2"))->segments[0].effect), QStringLiteral("c"));
        // Track: the other on V1 only.
        change(QStringLiteral("Row Count"), 3, QStringLiteral("Track"));
        QCOMPARE(valueOn(QStringLiteral("V1"), 1, QStringLiteral("Row Count")).toInt(), 3);
        QVERIFY(valueOn(QStringLiteral("V2"), 0, QStringLiteral("Row Count")).isUndefined());
        // Clip: none.
        change(QStringLiteral("X"), 100, QStringLiteral("Clip"));
        QVERIFY(valueOn(QStringLiteral("V1"), 1, QStringLiteral("X")).isUndefined());
        QCOMPARE(subcapSyncMode(*s.track(QStringLiteral("V2"))->segments[0].effect, QStringLiteral("X")), QStringLiteral("Clip"));
    }
};

QTEST_MAIN(TestSubCap)
#include "tst_subcap.moc"
