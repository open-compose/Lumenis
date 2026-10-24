// Tests for the OpenTimelineIO reader and writer (Phase 3.6b, docs/spec/otio.md): a timeline written
// here covers each rule (media to master clips, clips, gaps, transitions, speed changes, markers,
// rates and the start timecode); collections, refusals, OTIO's non-finite numbers, the export and its
// round trip, reuseClips() and damaged values; and OpenTimelineIO's own sample files from the
// interchange corpus (scripts/interop-corpus.py), read without warnings they should not give.
// scripts/otio-check.py (ctests otio_round_trip, otio_python) checks the corpus against
// OpenTimelineIO's Python package.
#include "lumenis/interop/otio.h"

#include "lumenis/model/marker.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace lumenis;
namespace otio = interop::otio;

namespace {

// A RationalTime and a TimeRange as OTIO writes them.
QJsonObject rt(double value, double rate = 24.0) {
    return QJsonObject{{"OTIO_SCHEMA", "RationalTime.1"}, {"value", value}, {"rate", rate}};
}
QJsonObject span(double start, double duration, double rate = 24.0) {
    return QJsonObject{{"OTIO_SCHEMA", "TimeRange.1"}, {"start_time", rt(start, rate)}, {"duration", rt(duration, rate)}};
}
QJsonObject external(const QString& url, double start, double duration) {
    return QJsonObject{{"OTIO_SCHEMA", "ExternalReference.1"}, {"name", ""}, {"target_url", url}, {"available_range", span(start, duration)}};
}
QJsonObject clip1(const QString& name, double start, double duration, const QJsonObject& ref) {
    return QJsonObject{{"OTIO_SCHEMA", "Clip.1"}, {"name", name}, {"source_range", span(start, duration)}, {"media_reference", ref}};
}
QJsonObject clip2(const QString& name, double start, double duration, const QJsonObject& ref) {
    return QJsonObject{{"OTIO_SCHEMA", "Clip.2"},
                       {"name", name},
                       {"source_range", span(start, duration)},
                       {"media_references", QJsonObject{{"DEFAULT_MEDIA", ref}}},
                       {"active_media_reference_key", "DEFAULT_MEDIA"}};
}
QJsonObject gap(double duration) {
    return QJsonObject{{"OTIO_SCHEMA", "Gap.1"}, {"source_range", span(0, duration)}};
}
QJsonObject marker(const QString& name, const QString& color, double frame, double length = 0) {
    return QJsonObject{{"OTIO_SCHEMA", "Marker.2"}, {"name", name}, {"color", color}, {"comment", name + " note"},
                       {"marked_range", span(frame, length)}};
}
QJsonObject track(const QString& kind, const QJsonArray& children, const QJsonArray& markers = {}) {
    return QJsonObject{{"OTIO_SCHEMA", "Track.1"}, {"kind", kind}, {"children", children}, {"markers", markers}};
}
QJsonObject timeline(const QString& name, const QJsonArray& tracks, const QJsonArray& stackMarkers = {}, double start = 86400) {
    return QJsonObject{{"OTIO_SCHEMA", "Timeline.1"},
                       {"name", name},
                       {"global_start_time", rt(start)},
                       {"tracks", QJsonObject{{"OTIO_SCHEMA", "Stack.1"}, {"children", tracks}, {"markers", stackMarkers}}}};
}
QByteArray json(const QJsonObject& o) {
    return QJsonDocument(o).toJson();
}

// The timeline of rules(): V1 a (frames 10-30 of a.mov, from 01:00:00:00), a dissolve of 5 + 5,
// b at double speed, a gap, c (no media); A1 a again; markers on a clip, a track and the stack.
QJsonObject rulesTimeline() {
    QJsonObject b = clip2("b", 0, 30, external("file:///media/b.mov", 0, 200));
    b.insert("effects", QJsonArray{QJsonObject{{"OTIO_SCHEMA", "LinearTimeWarp.1"}, {"time_scalar", 2.0}}});
    QJsonObject a = clip1("a", 86410, 20, external("file:///media/a.mov", 86400, 100));
    a.insert("markers", QJsonArray{marker("on a", "GREEN", 86415)});
    const QJsonObject dissolve{{"OTIO_SCHEMA", "Transition.1"}, {"transition_type", "SMPTE_Dissolve"},
                               {"in_offset", rt(5)}, {"out_offset", rt(5)}};
    const QJsonObject missing{{"OTIO_SCHEMA", "MissingReference.1"}, {"name", "c"}};
    return timeline("Rules",
                    {track("Video", {a, dissolve, b, gap(10), clip2("c", 0, 12, missing)}, {marker("on V1", "BLUE", 25, 4)}),
                     track("Audio", {clip1("a", 86410, 20, external("file:///media/a.mov", 86400, 100))})},
                    {marker("on the stack", "PINK", 40)});
}

const model::BinItem* named(const QList<model::BinItem>& items, const QString& name) {
    for (const model::BinItem& i : items) {
        if (i.name == name) return &i;
    }
    return nullptr;
}

} // namespace

class TestOtio : public QObject {
    Q_OBJECT
private slots:
    void rules();
    void collections();
    void refusals();
    void nonFinite();
    void exportsAndRoundTrip();
    void reusesClips();
    void damaged();
    void corpus();
};

void TestOtio::rules() {
    QString error;
    otio::ImportOptions options;
    options.binId = QStringLiteral("bin1");
    const auto result = otio::importJson(json(rulesTimeline()), options, &error);
    QVERIFY2(result, qPrintable(error));
    QVERIFY2(result->warnings.isEmpty(), qPrintable(result->warnings.join('\n')));
    QCOMPARE(result->items.size(), 4); // a, b, c, the sequence

    // One master clip for each piece of media, its file kept as its import source (Relink, p.969).
    const model::BinItem* a = named(result->items, "a");
    QVERIFY(a);
    QCOMPARE(a->kind, model::BinItemKind::MasterClip);
    QCOMPARE(a->mobId, otio::mediaMobId("file:///media/a.mov"));
    QCOMPARE(a->fields.value("Start"), QStringLiteral("01:00:00:00"));
    QCOMPARE(a->durationFrames, 100);
    QCOMPARE(a->fields.value("Tracks"), QStringLiteral("V1 A1"));
    QCOMPARE(a->sources.size(), 1);
    QCOMPARE(a->sources.first().kind, model::ClipSource::Kind::Import);
    QCOMPARE(a->sources.first().path, QStringLiteral("/media/a.mov"));
    QVERIFY(a->link.isEmpty()); // offline until relinked
    const model::BinItem* c = named(result->items, "c");
    QVERIFY(c);
    QVERIFY(c->sources.isEmpty());

    const model::BinItem& seq = result->items.last();
    QCOMPARE(seq.kind, model::BinItemKind::Sequence);
    QCOMPARE(seq.name, QStringLiteral("Rules"));
    QCOMPARE(seq.editRate, Rational(24));
    QCOMPARE(seq.fields.value("Start"), QStringLiteral("01:00:00:00"));
    const model::Sequence s = model::Sequence::fromItem(seq);
    QCOMPARE(s.trackNames(), (QStringList{"V1", "A1"}));
    const model::Track* v1 = s.track("V1");
    QCOMPARE(v1->segments.size(), 4);
    QCOMPARE(v1->segments[0].clipId, a->id);
    QCOMPARE(v1->segments[0].binId, QStringLiteral("bin1"));
    QCOMPARE(v1->segments[0].sourceIn, 10);
    QCOMPARE(v1->segments[0].length, 20);
    // The dissolve: on b, 5 frames before the cut and 5 after; b at double speed.
    const model::Segment& sb = v1->segments[1];
    QCOMPARE(sb.name, QStringLiteral("b"));
    QVERIFY(sb.transition);
    QCOMPARE(sb.transition->effect.name, QStringLiteral("Dissolve"));
    QCOMPARE(sb.transition->duration, 10);
    QCOMPARE(sb.transition->alignment, model::TransitionAlignment::CenteredOnCut);
    QVERIFY(sb.effect);
    QCOMPARE(sb.effect->name, QStringLiteral("Motion Effect"));
    QVERIFY(v1->segments[2].isFiller());
    QCOMPARE(v1->segments[2].length, 10);
    QCOMPARE(v1->segments[3].name, QStringLiteral("c"));
    QCOMPARE(s.track("A1")->segments.first().sourceTrack, QStringLiteral("A1"));

    // Markers: the clip's at its place in the sequence, the track's on it, the stack's on TC1.
    const QList<model::Marker> markers = model::markersOf(seq);
    QCOMPARE(markers.size(), 3);
    auto find = [&](const QString& name) {
        return std::find_if(markers.begin(), markers.end(), [&](const model::Marker& m) { return m.name == name; });
    };
    QCOMPARE(find("on a")->frame, 5);
    QCOMPARE(find("on a")->track, QStringLiteral("V1"));
    QCOMPARE(find("on a")->color, QStringLiteral("Green"));
    QCOMPARE(find("on V1")->frame, 25);
    QCOMPARE(find("on V1")->length, 4);
    QCOMPARE(find("on the stack")->track, QStringLiteral("TC1"));
    QCOMPARE(find("on the stack")->color, QStringLiteral("Pink"));
}

void TestOtio::collections() {
    // A collection of two timelines that share a.mov: one clip, two sequences.
    const QJsonObject one = timeline("One", {track("Video", {clip1("a", 0, 10, external("file:///a.mov", 0, 50))})}, {}, 0);
    const QJsonObject two = timeline("Two", {track("Video", {clip1("a", 20, 10, external("file:///a.mov", 0, 50))})}, {}, 0);
    const QJsonObject collection{{"OTIO_SCHEMA", "SerializableCollection.1"}, {"children", QJsonArray{one, two}}};
    const auto result = otio::importJson(json(collection));
    QVERIFY(result);
    QCOMPARE(result->items.size(), 3);
    QCOMPARE(result->items[1].name, QStringLiteral("One"));
    QCOMPARE(result->items[2].name, QStringLiteral("Two"));
    // A rate given only by a clip's range (no global_start_time), and the file's name for a timeline
    // without one.
    QJsonObject unnamed = timeline("", {track("Video", {clip1("x", 0, 10, external("x.mov", 0, 10))})}, {}, 0);
    unnamed.remove("global_start_time");
    otio::ImportOptions options;
    options.name = QStringLiteral("from-the-file");
    const auto r2 = otio::importJson(json(unnamed), options);
    QVERIFY(r2);
    QCOMPARE(r2->items.last().name, QStringLiteral("from-the-file"));
    QCOMPARE(r2->items.last().editRate, Rational(24));
}

void TestOtio::refusals() {
    QString error;
    QVERIFY(!otio::importJson("not json", {}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!otio::importJson(json(QJsonObject{{"OTIO_SCHEMA", "Clip.1"}}), {}, &error));
    QCOMPARE(error, QStringLiteral("no timeline in it"));
    QVERIFY(!otio::importJson("[1, 2]", {}, &error));
    // An unknown track kind and item: warnings, not failures.
    const auto result = otio::importJson(json(timeline("T", {track("Data", {}), track("Video", {QJsonObject{{"OTIO_SCHEMA", "Widget.1"}, {"source_range", span(0, 5)}}})})));
    QVERIFY(result);
    QVERIFY(result->warnings.join('\n').contains("tracks of kind \"Data\" left out"));
    QVERIFY(result->warnings.join('\n').contains("Widget items read as gaps"));
    QCOMPARE(model::Sequence::fromItem(result->items.last()).duration(), 5);
}

void TestOtio::nonFinite() {
    // OpenTimelineIO writes Inf, -Infinity and NaN (big_int.otio holds them, in metadata), which JSON
    // does not allow; inside strings the words are kept.
    QByteArray text = json(timeline("Inf", {track("Video", {clip1("NaN", 0, 10, external("Infinity.mov", 0, 10))})}, {}, 0));
    text.replace("\"Timeline.1\"", "\"Timeline.1\", \"metadata\": {\"a\": Inf, \"b\": -Infinity, \"c\": NaN}");
    QString error;
    const auto result = otio::importJson(text, {}, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->items.first().name, QStringLiteral("NaN"));
    QCOMPARE(result->items.first().sources.first().path, QStringLiteral("Infinity.mov"));
}

void TestOtio::exportsAndRoundTrip() {
    const auto imported = otio::importJson(json(rulesTimeline()));
    QVERIFY(imported);
    const QList<model::BinItem> items = imported->items;
    otio::ExportOptions options;
    options.resolve = [&items](const MobId& mob, const QString& clipId) -> std::optional<model::BinItem> {
        for (const model::BinItem& i : items) {
            if (i.kind != model::BinItemKind::Sequence && (i.id == clipId || model::mobIdOf(i) == mob)) return i;
        }
        return std::nullopt;
    };
    const otio::ExportResult out = otio::exportJson(items.last(), options);
    QVERIFY2(out.ok, qPrintable(out.error));
    QCOMPARE(out.clips, 4);
    const QJsonObject tl = QJsonDocument::fromJson(out.json).object();
    QCOMPARE(tl.value("OTIO_SCHEMA").toString(), QStringLiteral("Timeline.1"));
    QCOMPARE(tl.value("global_start_time").toObject().value("value").toDouble(), 86400.0);
    const QJsonArray tracks = tl.value("tracks").toObject().value("children").toArray();
    QCOMPARE(tracks.size(), 2);
    QCOMPARE(tracks[0].toObject().value("kind").toString(), QStringLiteral("Video"));
    const QJsonArray v1 = tracks[0].toObject().value("children").toArray();
    QCOMPARE(v1.size(), 5); // a, the dissolve, b, the gap, c
    const QJsonObject a = v1[0].toObject();
    QCOMPARE(a.value("OTIO_SCHEMA").toString(), QStringLiteral("Clip.2"));
    QCOMPARE(a.value("source_range").toObject().value("start_time").toObject().value("value").toDouble(), 86410.0);
    const QJsonObject ref = a.value("media_references").toObject().value("DEFAULT_MEDIA").toObject();
    QCOMPARE(ref.value("OTIO_SCHEMA").toString(), QStringLiteral("ExternalReference.1"));
    QCOMPARE(ref.value("target_url").toString(), QStringLiteral("file:///media/a.mov"));
    const QJsonObject dissolve = v1[1].toObject();
    QCOMPARE(dissolve.value("transition_type").toString(), QStringLiteral("SMPTE_Dissolve"));
    QCOMPARE(dissolve.value("in_offset").toObject().value("value").toDouble(), 5.0);
    QCOMPARE(v1[2].toObject().value("effects").toArray().first().toObject().value("time_scalar").toDouble(), 2.0);
    QCOMPARE(v1[3].toObject().value("OTIO_SCHEMA").toString(), QStringLiteral("Gap.1"));
    QCOMPARE(v1[4].toObject().value("media_references").toObject().value("DEFAULT_MEDIA").toObject().value("OTIO_SCHEMA").toString(),
             QStringLiteral("MissingReference.1"));

    // Read back: the same sequence and markers.
    const auto again = otio::importJson(out.json);
    QVERIFY(again);
    const model::Sequence before = model::Sequence::fromItem(items.last());
    const model::Sequence after = model::Sequence::fromItem(again->items.last());
    QCOMPARE(after.trackNames(), before.trackNames());
    for (const QString& name : before.trackNames()) {
        const QList<model::Segment>& x = before.track(name)->segments;
        const QList<model::Segment>& y = after.track(name)->segments;
        QCOMPARE(y.size(), x.size());
        for (qsizetype i = 0; i < x.size(); ++i) {
            QCOMPARE(y[i].length, x[i].length);
            QCOMPARE(y[i].sourceIn, x[i].sourceIn);
            QCOMPARE(y[i].name, x[i].name);
            QCOMPARE(y[i].sourceMob, x[i].sourceMob);
            QCOMPARE(y[i].transition, x[i].transition);
            QCOMPARE(y[i].effect, x[i].effect);
        }
    }
    QCOMPARE(model::markersOf(again->items.last()).size(), 3);
    QCOMPARE(again->items.last().fields.value("Start"), QStringLiteral("01:00:00:00"));

    // Without a resolver: named by the segment, counted from 0, with a warning. Not a sequence: fails.
    const otio::ExportResult unresolved = otio::exportJson(items.last());
    QVERIFY(unresolved.ok);
    QVERIFY(unresolved.warnings.join('\n').contains("clips not found"));
    QVERIFY(!otio::exportJson(items.first()).ok);
}

void TestOtio::reusesClips() {
    auto first = otio::importJson(json(rulesTimeline()));
    auto second = otio::importJson(json(rulesTimeline()));
    QVERIFY(first && second);
    const QList<model::BinItem> existing = first->items.mid(0, 3);
    otio::reuseClips(*second, existing);
    QCOMPARE(second->items.size(), 1); // only the sequence: a, b and c were there
    const model::Sequence s = model::Sequence::fromItem(second->items.first());
    QCOMPARE(s.track("V1")->segments.first().clipId, existing.first().id);
}

void TestOtio::damaged() {
    // Numbers past any real length, negative ones, missing ranges and rates: read without overflow
    // (clamped to detail::kMaxFrames), never a crash.
    QJsonObject huge = clip1("huge", 1e300, 1e300, external("h.mov", -1e300, 1e300));
    QJsonObject negative = clip1("negative", -50, -10, external("n.mov", 0, 10));
    QJsonObject noRange{{"OTIO_SCHEMA", "Clip.1"}, {"name", "none"}};
    QJsonObject zeroRate = clip1("zero", 0, 10, external("z.mov", 0, 10));
    zeroRate.insert("source_range", QJsonObject{{"start_time", rt(0, 0)}, {"duration", rt(10, 0)}});
    const QJsonObject badTransition{{"OTIO_SCHEMA", "Transition.1"}, {"in_offset", rt(-1e300)}, {"out_offset", rt(1e300)}};
    const auto result = otio::importJson(json(timeline("Damaged", {track("Video", {huge, badTransition, negative, noRange, zeroRate,
                                                                                   QJsonObject{{"OTIO_SCHEMA", "Track.1"}}})},
                                                       {}, 1e300)));
    QVERIFY(result);
    const model::BinItem& seq = result->items.last();
    QVERIFY(model::Sequence::fromItem(seq).duration() >= 0);
    // Nesting past the limit is left out.
    QJsonObject deep = track("Video", {clip1("leaf", 0, 5, external("l.mov", 0, 5))});
    for (int i = 0; i < 40; ++i) deep = track("Video", {deep});
    const auto nested = otio::importJson(json(timeline("Deep", {deep})));
    QVERIFY(nested);
    QVERIFY(nested->warnings.join('\n').contains("nested too deep"));
}

void TestOtio::corpus() {
    const QString folder = qEnvironmentVariable("CUTTHULHU_INTEROP_CORPUS");
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    const QDir dir(folder + QStringLiteral("/otio"));
    const QStringList files = dir.entryList({QStringLiteral("*.otio")}, QDir::Files);
    if (files.isEmpty()) QSKIP("no OpenTimelineIO samples in the corpus");
    for (const QString& f : files) {
        QString error;
        const auto result = otio::importFile(dir.filePath(f), {}, &error);
        QVERIFY2(result, qPrintable(f + QStringLiteral(": ") + error));
        QVERIFY2(!result->items.isEmpty() && result->items.last().kind == model::BinItemKind::Sequence, qPrintable(f));
        QVERIFY2(model::Sequence::fromItem(result->items.last()).duration() > 0, qPrintable(f));
    }
    // What the samples hold, as OpenTimelineIO reads them (otio-check.py compares the rest).
    const auto simple = otio::importFile(dir.filePath("simple_cut.otio"));
    QVERIFY(simple);
    QCOMPARE(simple->items.size(), 5); // four clips and the sequence
    QVERIFY(simple->warnings.isEmpty());
    const auto transition = otio::importFile(dir.filePath("transition.otio"));
    QVERIFY(transition);
    int transitions = 0;
    const model::Sequence withTransition = model::Sequence::fromItem(transition->items.last()); // kept: the loop reads its track
    for (const model::Segment& s : withTransition.track("V1")->segments) transitions += s.transition ? 1 : 0;
    QCOMPARE(transitions, 1);
    const auto premiere = otio::importFile(dir.filePath("premiere_example.otio"));
    QVERIFY(premiere);
    QCOMPARE(model::markersOf(premiere->items.last()).size(), 4);
    QCOMPARE(premiere->items.last().fields.value("Tracks"), QStringLiteral("V1-4 A1-4 TC1"));
}

QTEST_GUILESS_MAIN(TestOtio)
#include "tst_otio.moc"
