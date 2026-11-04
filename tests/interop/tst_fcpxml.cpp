// Tests for the FCP 7 XML reader and writer (Phase 3.6c, docs/spec/fcp-xml.md): a document written
// here covers each rule (files and their timecode, a file given again by id, start and end of -1 at
// a transition's cut, gaps, generators, disabled clips, Time Remap, markers, exploded audio, a nested
// sequence); refusals, the export and its round trip, the media keys it shares with the OTIO import,
// damaged documents, and the FCP XML files of the interchange corpus (scripts/interop-corpus.py).
// scripts/otio-check.py --format fcpxml (ctests fcpxml_round_trip, fcpxml_python) checks the corpus
// against OpenTimelineIO's FCP adapter.
#include "corpus.h"
#include "lumenis/interop/fcp_xml.h"
#include "lumenis/interop/otio.h"

#include "lumenis/model/marker.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QRandomGenerator>
#include <QtTest>

using namespace lumenis;
namespace fcpxml = interop::fcpxml;
namespace otio = interop::otio;

namespace {

QByteArray rate(int timebase, bool ntsc = false) {
    return "<rate><timebase>" + QByteArray::number(timebase) + "</timebase><ntsc>" + (ntsc ? "TRUE" : "FALSE") + "</ntsc></rate>";
}

// The document of rules(), at 25 fps from 01:00:00:00. V1: a.mov (frames 50-100 of it, its timecode
// 01:00:00:00), a 10-frame dissolve centred on frame 50, b.mov from its frame 20 at half speed, a
// gap, a.mov again by its file id; a generator and a disabled clip on V2; A1 and A2 an exploded
// stereo pair of a.mov (A2 is not read); markers on a clip and on the sequence.
QByteArray rulesXml() {
    const QByteArray fileA = "<file id=\"f1\"><name>a.mov</name><pathurl>file://localhost/media/a.mov</pathurl>" + rate(25) +
                             "<duration>500</duration><timecode>" + rate(25) +
                             "<string>01:00:00:00</string><frame>90000</frame><displayformat>NDF</displayformat></timecode></file>";
    const QByteArray fileB = "<file id=\"f2\"><name>b.mov</name><pathurl>file:///media/b.mov</pathurl>" + rate(25) +
                             "<duration>300</duration><timecode>" + rate(25) + "<frame>0</frame></timecode></file>";
    const QByteArray remap = "<filter><effect><name>Time Remap</name><effectid>timeremap</effectid>"
                             "<parameter><parameterid>speed</parameterid><value>50</value></parameter>"
                             "<parameter><parameterid>reverse</parameterid><value>FALSE</value></parameter></effect></filter>";
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE xmeml>\n<xmeml version=\"5\"><sequence id=\"s1\">"
           "<name>Rules</name><duration>175</duration>" +
           rate(25) + "<timecode>" + rate(25) + "<string>01:00:00:00</string><frame>90000</frame></timecode><media><video><track>"
           "<clipitem id=\"c1\"><name>a</name>" + rate(25) + "<start>0</start><end>-1</end><in>50</in><out>105</out>" + fileA +
           "<marker><name>on a</name><comment>note</comment><in>60</in><out>-1</out></marker></clipitem>"
           "<transitionitem><start>45</start><end>55</end><alignment>center</alignment>" + rate(25) +
           "<effect><name>Cross Dissolve</name><effectid>Cross Dissolve</effectid><effecttype>transition</effecttype></effect>"
           "</transitionitem>"
           "<clipitem id=\"c2\"><name>b</name>" + rate(25) + "<start>-1</start><end>120</end><in>20</in><out>90</out>" + fileB + remap +
           "</clipitem>"
           "<clipitem id=\"c3\"><name>a again</name>" + rate(25) + "<start>150</start><end>175</end><in>10</in><out>35</out>"
           "<file id=\"f1\"/></clipitem>"
           "</track><track>"
           "<generatoritem><name>Slug</name><start>0</start><end>20</end><in>0</in><out>20</out>"
           "<effect><name>Slug</name><effectid>slug</effectid><effecttype>generator</effecttype></effect></generatoritem>"
           "<clipitem><name>off</name><enabled>FALSE</enabled><start>20</start><end>40</end><in>0</in><out>20</out><file id=\"f2\"/></clipitem>"
           "</track></video><audio>"
           "<track currentExplodedTrackIndex=\"0\" totalExplodedTrackCount=\"2\"><clipitem><name>a</name><start>0</start><end>50</end>"
           "<in>50</in><out>100</out><file id=\"f1\"/></clipitem></track>"
           "<track currentExplodedTrackIndex=\"1\" totalExplodedTrackCount=\"2\"><clipitem><name>a</name><start>0</start><end>50</end>"
           "<in>50</in><out>100</out><file id=\"f1\"/></clipitem></track>"
           "</audio></media>"
           "<marker><name>on the sequence</name><in>100</in><out>110</out></marker>"
           "</sequence></xmeml>\n";
}

const model::BinItem* named(const QList<model::BinItem>& items, const QString& name) {
    for (const model::BinItem& i : items) {
        if (i.name == name) return &i;
    }
    return nullptr;
}

const model::BinItem* sequenceOf(const QList<model::BinItem>& items) {
    for (const model::BinItem& i : items) {
        if (i.kind == model::BinItemKind::Sequence) return &i;
    }
    return nullptr;
}

QString corpusDir() { return lumenis::test::interopCorpus(); }

} // namespace

class TestFcpXml : public QObject {
    Q_OBJECT

private slots:
    void rules();
    void refusals();
    void exportsAndRoundTrip();
    void sharesMediaWithOtio();
    void nested();
    void damaged();
    void corpus();
};

void TestFcpXml::rules() {
    QString error;
    const auto result = fcpxml::importXml(rulesXml(), {}, &error);
    QVERIFY2(result, qPrintable(error));
    // a and b, once each: the third clip names a by its file's id.
    const model::BinItem* a = named(result->items, QStringLiteral("a.mov"));
    const model::BinItem* b = named(result->items, QStringLiteral("b.mov"));
    QVERIFY(a && b);
    QCOMPARE(a->kind, model::BinItemKind::MasterClip);
    QCOMPARE(a->fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:00"));
    QCOMPARE(a->durationFrames, std::int64_t{500});
    QCOMPARE(a->mobId, otio::mediaMobId(QStringLiteral("file:///media/a.mov"))); // localhost: the same file
    QCOMPARE(result->items.size(), 3);
    const model::BinItem* seq = sequenceOf(result->items);
    QVERIFY(seq);
    QCOMPARE(seq->name, QStringLiteral("Rules"));
    QCOMPARE(seq->fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:00"));
    const model::Sequence s = model::Sequence::fromItem(*seq);
    QCOMPARE(s.trackNames(), (QStringList{QStringLiteral("V1"), QStringLiteral("V2"), QStringLiteral("A1")}));
    const model::Track* v1 = s.track(QStringLiteral("V1"));
    QVERIFY(v1);
    QCOMPARE(v1->segments.size(), 4);
    // a ends at the dissolve's cut (frame 50); its in is frames from its media's start.
    QCOMPARE(v1->segments[0].clipId, a->id);
    QCOMPARE(v1->segments[0].length, std::int64_t{50});
    QCOMPARE(v1->segments[0].sourceIn, std::int64_t{50});
    // b starts at the cut, 5 frames into its in (the dissolve's start is its in), half speed.
    QCOMPARE(v1->segments[1].clipId, b->id);
    QCOMPARE(v1->segments[1].length, std::int64_t{70});
    QCOMPARE(v1->segments[1].sourceIn, std::int64_t{25});
    QVERIFY(v1->segments[1].transition);
    QCOMPARE(v1->segments[1].transition->duration, std::int64_t{10});
    QCOMPARE(v1->segments[1].transition->alignment, model::TransitionAlignment::CenteredOnCut);
    QVERIFY(v1->segments[1].effect);
    // The gap, then a again by its id.
    QVERIFY(v1->segments[2].isFiller());
    QCOMPARE(v1->segments[2].length, std::int64_t{30});
    QCOMPARE(v1->segments[3].clipId, a->id);
    QCOMPARE(v1->segments[3].sourceIn, std::int64_t{10});
    // V2: the generator and the disabled clip are filler.
    const model::Track* v2 = s.track(QStringLiteral("V2"));
    QVERIFY(v2);
    for (const model::Segment& seg : v2->segments) QVERIFY(seg.isFiller());
    // One audio track of the exploded pair.
    QVERIFY(!s.track(QStringLiteral("A2")));
    // The clip's marker 10 frames into it; the sequence's on TC1.
    const auto markers = model::markersOf(*seq);
    QCOMPARE(markers.size(), 2);
    bool onClip = false;
    bool onSequence = false;
    for (const model::Marker& m : markers) {
        if (m.name == QLatin1String("on a")) onClip = m.track == QLatin1String("V1") && m.frame == 10 && m.comment == QLatin1String("note");
        if (m.name == QLatin1String("on the sequence")) onSequence = m.track == QLatin1String("TC1") && m.frame == 100 && m.length == 10;
    }
    QVERIFY(onClip);
    QVERIFY(onSequence);
    const QString warnings = result->warnings.join(QLatin1Char('\n'));
    QVERIFY2(warnings.contains(QStringLiteral("generators")), qPrintable(warnings));
    QVERIFY2(warnings.contains(QStringLiteral("disabled clips")), qPrintable(warnings));
}

void TestFcpXml::refusals() {
    QString error;
    QVERIFY(!fcpxml::importXml("not xml at all", {}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!fcpxml::importXml("<?xml version=\"1.0\"?><fcpxml version=\"1.9\"/>", {}, &error));
    QVERIFY2(error.contains(QStringLiteral("xmeml")), qPrintable(error)); // Final Cut Pro X's format is not this one
    QVERIFY(!fcpxml::importXml("<xmeml version=\"5\"><bin><name>empty</name></bin></xmeml>", {}, &error));
    QCOMPARE(error, QStringLiteral("no sequence in it"));
    QVERIFY(!fcpxml::importXml("<xmeml><sequence>", {}, &error)); // cut short
    QVERIFY(fcpxml::isFcpXml(rulesXml().left(200)));
    QVERIFY(!fcpxml::isFcpXml("<?xml version=\"1.0\"?><filter/>"));
    QVERIFY(!fcpxml::isFcpXml("{\"OTIO_SCHEMA\": \"Timeline.1\"}"));
}

void TestFcpXml::exportsAndRoundTrip() {
    const auto first = fcpxml::importXml(rulesXml());
    QVERIFY(first);
    const model::BinItem* seq = sequenceOf(first->items);
    QVERIFY(seq);
    otio::ExportOptions options;
    const QList<model::BinItem> items = first->items;
    options.resolve = [items](const MobId& mob, const QString& id) -> std::optional<model::BinItem> {
        for (const model::BinItem& i : items) {
            if (i.kind != model::BinItemKind::Sequence && (i.id == id || model::mobIdOf(i) == mob)) return i;
        }
        return std::nullopt;
    };
    const otio::ExportResult out = fcpxml::exportXml(*seq, options);
    QVERIFY2(out.ok, qPrintable(out.error));
    QVERIFY(fcpxml::isFcpXml(out.json));
    const QString xml = QString::fromUtf8(out.json);
    QVERIFY(xml.contains(QStringLiteral("<transitionitem>")));
    QVERIFY(xml.contains(QStringLiteral("<effectid>timeremap</effectid>")));
    QVERIFY(xml.contains(QStringLiteral("<value>50</value>")));
    // a's file is written in full once, then named by its id.
    QCOMPARE(xml.count(QStringLiteral("<pathurl>file:///media/a.mov</pathurl>")), 1);
    QVERIFY(xml.contains(QStringLiteral("<file id=\"file-1\"/>")));
    // The two clips beside the dissolve end and start at -1.
    QCOMPARE(xml.count(QStringLiteral("<end>-1</end>")), 1);
    QCOMPARE(xml.count(QStringLiteral("<start>-1</start>")), 1);
    // Read back: the same cut.
    const auto again = fcpxml::importXml(out.json);
    QVERIFY(again);
    const model::BinItem* back = sequenceOf(again->items);
    QVERIFY(back);
    const model::Sequence a = model::Sequence::fromItem(*seq);
    const model::Sequence b = model::Sequence::fromItem(*back);
    const model::Track* v1 = a.track(QStringLiteral("V1"));
    const model::Track* w1 = b.track(QStringLiteral("V1"));
    QVERIFY(v1 && w1);
    QCOMPARE(w1->segments.size(), v1->segments.size());
    for (qsizetype i = 0; i < v1->segments.size(); ++i) {
        QCOMPARE(w1->segments[i].length, v1->segments[i].length);
        QCOMPARE(w1->segments[i].sourceIn, v1->segments[i].sourceIn);
        QCOMPARE(w1->segments[i].sourceMobId(), v1->segments[i].sourceMobId());
        QCOMPARE(w1->segments[i].transition.has_value(), v1->segments[i].transition.has_value());
        QCOMPARE(w1->segments[i].effect.has_value(), v1->segments[i].effect.has_value());
    }
    QCOMPARE(back->fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:00"));
    // Not a sequence: refused.
    const model::BinItem* clip = named(first->items, QStringLiteral("a.mov"));
    QVERIFY(clip);
    QVERIFY(!fcpxml::exportXml(*clip).ok);
}

// The FCP XML import goes through the OTIO one: the same media is the same clip in both.
void TestFcpXml::sharesMediaWithOtio() {
    QString error;
    const auto json = fcpxml::toOtioJson(rulesXml(), &error);
    QVERIFY2(json, qPrintable(error));
    const auto viaOtio = otio::importJson(*json);
    const auto viaFcp = fcpxml::importXml(rulesXml());
    QVERIFY(viaOtio && viaFcp);
    const model::BinItem* one = named(viaOtio->items, QStringLiteral("a.mov"));
    const model::BinItem* two = named(viaFcp->items, QStringLiteral("a.mov"));
    QVERIFY(one && two);
    QCOMPARE(one->mobId, two->mobId);
}

// A sequence nested in a clip: its video tracks, trimmed to the clip's in and out, the top one with
// material shown; the audio tracks of a sequence nested on a video track are not part of its picture.
void TestFcpXml::nested() {
    const QByteArray file = "<file id=\"n\"><name>n.mov</name><pathurl>file:///n.mov</pathurl>" + rate(24) + "<duration>1000</duration></file>";
    const QByteArray xml = "<xmeml version=\"4\"><project><children><sequence id=\"outer\"><name>Outer</name>" + rate(24) +
                           "<media><video><track><clipitem><name>nest</name><start>0</start><end>20</end><in>100</in><out>120</out>"
                           "<sequence id=\"inner\"><name>Inner</name>" + rate(24) + "<media><video>"
                           "<track><clipitem><name>low</name><start>0</start><end>500</end><in>0</in><out>500</out>" + file +
                           "</clipitem></track>"
                           "<track><clipitem><name>top</name><start>90</start><end>200</end><in>300</in><out>410</out><file id=\"n\"/>"
                           "</clipitem></track></video>"
                           "<audio><track><clipitem><name>sound</name><start>0</start><end>500</end><in>0</in><out>500</out>"
                           "<file id=\"n\"/></clipitem></track></audio></media></sequence>"
                           "</clipitem></track></video></media></sequence></children></project></xmeml>";
    QString error;
    const auto result = fcpxml::importXml(xml, {}, &error);
    QVERIFY2(result, qPrintable(error));
    const model::BinItem* outer = sequenceOf(result->items);
    QVERIFY(outer);
    const model::Sequence s = model::Sequence::fromItem(*outer);
    const model::Track* v1 = s.track(QStringLiteral("V1"));
    QVERIFY(v1);
    QCOMPARE(v1->segments.size(), 1);
    // The top track's clip, frames 100-120 of the inner sequence: 10 frames into its in of 300.
    QCOMPARE(v1->segments[0].name, QStringLiteral("top"));
    QCOMPARE(v1->segments[0].sourceIn, std::int64_t{310});
    QCOMPARE(v1->segments[0].length, std::int64_t{20});
}

// Damaged documents: cut short, numbers out of range, -1 with no transition, references to ids never
// given, deep nesting. Each is read or refused, without a crash.
void TestFcpXml::damaged() {
    const QByteArray good = rulesXml();
    for (int cut = 0; cut < good.size(); cut += 97) fcpxml::importXml(good.left(cut));
    QByteArray huge = good;
    huge.replace("<in>50</in>", "<in>1e308</in>").replace("<end>120</end>", "<end>9223372036854775807</end>");
    huge.replace("<frame>90000</frame>", "<frame>-1e300</frame>").replace("<timebase>25</timebase>", "<timebase>0</timebase>");
    QVERIFY(fcpxml::importXml(huge));
    QByteArray minus = good;
    minus.replace("<transitionitem>", "<transitionitemx>").replace("</transitionitem>", "</transitionitemx>");
    const auto lone = fcpxml::importXml(minus);
    QVERIFY(lone);
    QVERIFY(lone->warnings.join(QLatin1Char('\n')).contains(QStringLiteral("-1")));
    QByteArray missing = good;
    missing.replace("<file id=\"f2\"><name>b.mov", "<file id=\"other\"><name>b.mov");
    QVERIFY(fcpxml::importXml(missing));
    QByteArray deep = "<xmeml><sequence><media><video><track><clipitem><start>0</start><end>10</end>";
    for (int i = 0; i < 40; ++i) deep += "<sequence><media><video><track><clipitem><start>0</start><end>10</end>";
    QVERIFY(fcpxml::importXml(deep + "</clipitem></track></video></media></sequence></xmeml>").has_value() || true);
    QByteArray deeper = "<xmeml>";
    for (int i = 0; i < 400; ++i) deeper += "<bin>";
    QString error;
    QVERIFY(!fcpxml::importXml(deeper, {}, &error));
    QRandomGenerator random(7);
    for (int i = 0; i < 200; ++i) {
        QByteArray noisy = good;
        for (int k = 0; k < 8; ++k) noisy[random.bounded(static_cast<int>(noisy.size()))] = static_cast<char>(random.bounded(256));
        fcpxml::importXml(noisy);
    }
}

void TestFcpXml::corpus() {
    const QString dir = corpusDir();
    if (dir.isEmpty() || !QDir(dir + QStringLiteral("/otio-fcp")).exists()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    const QDir fcp(dir + QStringLiteral("/otio-fcp"));
    int read = 0;
    for (const QString& name : fcp.entryList({QStringLiteral("*.xml")}, QDir::Files)) {
        QString error;
        const QString path = fcp.filePath(name);
        QVERIFY2(fcpxml::isFcpXmlFile(path), qPrintable(name));
        const auto result = fcpxml::importFile(path, {}, &error);
        QVERIFY2(result, qPrintable(name + QStringLiteral(": ") + error));
        QVERIFY2(sequenceOf(result->items), qPrintable(name));
        // Nothing the reader should understand is listed as left out.
        for (const QString& w : result->warnings) {
            QVERIFY2(!w.contains(QStringLiteral("without a start and end")) && !w.contains(QStringLiteral("rate read at 24")),
                     qPrintable(name + QStringLiteral(": ") + w));
        }
        ++read;
    }
    QCOMPARE(read, 5);
}

QTEST_GUILESS_MAIN(TestFcpXml)
#include "tst_fcpxml.moc"
