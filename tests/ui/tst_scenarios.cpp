// Scripted edit scenarios (ROADMAP Phase 4 gates 1 and 2; docs/spec/edit-scenarios.md). Each file in
// tests/scenarios is a JSON list of scenarios: clips, a starting sequence, steps (loading clips,
// marks, positions, tracks, segment selections, trims and commands by their id, as the user would
// give them) and the sequence expected at the end. A Composer and Timeline of their own run them.
// After the last step every scenario is undone step by step back to its starting sequence and
// redone to its end, and each state is compared by hash with the one recorded on the way (gate 2).
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::ui;
using model::BinItem;
using model::BinItemKind;

namespace {

const Rational kRate(25);

// The folders of scenario files: LUMENIS_SCENARIOS (a list, as PATH writes it) when it is set,
// else tests/scenarios.
QStringList scenarioDirs() {
    const QString env = qEnvironmentVariable("LUMENIS_SCENARIOS");
    if (!env.isEmpty()) return env.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    return {QStringLiteral(LUMENIS_SCENARIO_DIR)};
}

// What a scenario starts from and runs in.
struct Run {
    Composer composer;
    Timeline timeline{composer};
    QHash<QString, BinItem> clips; // by name
    QHash<QString, BinItem> items; // by id: clips and the sequences stored
    QString error;

    Run() {
        composer.setItemResolver([this](const QString&, const QString& id) -> std::optional<BinItem> {
            if (items.contains(id)) return items.value(id);
            return std::nullopt;
        });
        timeline.setSequenceStore([this](const QString&, const BinItem& item, bool) {
            items.insert(item.id, item);
            return true;
        });
        timeline.setSequenceMaker([]() -> std::optional<std::pair<QString, BinItem>> {
            BinItem s;
            s.id = model::newItemId();
            s.name = QStringLiteral("Untitled Sequence.01");
            s.kind = BinItemKind::Sequence;
            s.editRate = kRate;
            return std::make_pair(QStringLiteral("bin"), s);
        });
        timeline.resize(900, 300);
    }

    bool fail(const QString& why) {
        if (error.isEmpty()) error = why;
        return false;
    }

    MonitorId monitorOf(const QJsonValue& v) const {
        return v.toString() == QLatin1String("source") ? MonitorId::Source : MonitorId::Record;
    }

    // ["A", sourceIn, length] or ["filler", length], with `track` the clip's track used.
    model::Segment segmentOf(const QJsonArray& a, const QString& track) {
        if (a.at(0).toString() == QLatin1String("filler")) return model::Segment::filler(a.at(1).toInteger());
        const BinItem c = clips.value(a.at(0).toString());
        model::Segment s;
        s.clipId = c.id;
        s.binId = QStringLiteral("bin");
        s.name = c.name;
        s.sourceTrack = a.size() > 3 ? a.at(3).toString() : track;
        s.sourceIn = a.at(1).toInteger();
        s.length = a.at(2).toInteger();
        return s;
    }

    bool setUp(const QJsonObject& scenario) {
        for (const QJsonValue& v : scenario.value(QStringLiteral("clips")).toArray()) {
            const QJsonObject o = v.toObject();
            BinItem i;
            i.id = model::newItemId();
            i.name = o.value(QStringLiteral("name")).toString();
            i.kind = BinItemKind::MasterClip;
            i.durationFrames = o.value(QStringLiteral("frames")).toInteger(100);
            i.editRate = kRate;
            i.fields.insert(QStringLiteral("Tracks"), o.value(QStringLiteral("tracks")).toString(QStringLiteral("V1 A1-2")));
            clips.insert(i.name, i);
            items.insert(i.id, i);
        }
        const QJsonObject seq = scenario.value(QStringLiteral("sequence")).toObject();
        if (seq.isEmpty()) return true;
        BinItem s;
        s.id = model::newItemId();
        s.name = QStringLiteral("Cut");
        s.kind = BinItemKind::Sequence;
        s.editRate = kRate;
        model::Sequence sequence = model::Sequence::withDefaultTracks();
        const QJsonObject tracks = seq.value(QStringLiteral("tracks")).toObject();
        for (auto it = tracks.begin(); it != tracks.end(); ++it) {
            const QString name = it.key();
            while (!sequence.track(name)) {
                const model::TrackKind kind = name.startsWith(QLatin1Char('V'))   ? model::TrackKind::Video
                                              : name.startsWith(QLatin1Char('A')) ? model::TrackKind::Audio
                                                                                  : model::TrackKind::Data;
                if (!sequence.addTrack(kind)) return fail(QStringLiteral("cannot add track %1").arg(name));
            }
            QList<model::Segment> material;
            for (const QJsonValue& m : it.value().toArray()) material << segmentOf(m.toArray(), name);
            sequence.splice(name, 0, material);
        }
        sequence.storeIn(s);
        items.insert(s.id, s);
        composer.load(MonitorId::Record, QStringLiteral("bin"), s);
        return true;
    }

    bool step(const QJsonObject& o) {
        if (o.contains(QStringLiteral("load"))) {
            const QString name = o.value(QStringLiteral("clip")).toString();
            if (!clips.contains(name)) return fail(QStringLiteral("no clip %1").arg(name));
            composer.load(monitorOf(o.value(QStringLiteral("load"))), QStringLiteral("bin"), clips.value(name));
            return true;
        }
        if (o.contains(QStringLiteral("mark"))) {
            model::MonitorState& m = composer.state(monitorOf(o.value(QStringLiteral("mark"))));
            m.clearBoth();
            if (o.contains(QStringLiteral("in"))) {
                m.setPosition(o.value(QStringLiteral("in")).toInteger());
                m.markIn();
            }
            if (o.contains(QStringLiteral("out"))) {
                m.setPosition(o.value(QStringLiteral("out")).toInteger());
                m.markOut();
            }
            if (o.contains(QStringLiteral("at"))) m.setPosition(o.value(QStringLiteral("at")).toInteger());
            return true;
        }
        if (o.contains(QStringLiteral("activate"))) {
            composer.setActiveMonitor(monitorOf(o.value(QStringLiteral("activate"))));
            return true;
        }
        if (o.contains(QStringLiteral("position"))) {
            composer.state(monitorOf(o.value(QStringLiteral("position")))).setPosition(o.value(QStringLiteral("frame")).toInteger());
            return true;
        }
        if (o.contains(QStringLiteral("source_tracks")) || o.contains(QStringLiteral("record_tracks"))) {
            const bool source = o.contains(QStringLiteral("source_tracks"));
            const QJsonArray on = o.value(source ? QStringLiteral("source_tracks") : QStringLiteral("record_tracks")).toArray();
            QStringList wanted;
            for (const QJsonValue& v : on) wanted << v.toString();
            model::TrackPatch& p = timeline.patch();
            const QStringList all = source ? p.sourceTracks() : timeline.shown().trackNames();
            for (const QString& t : all) {
                const bool selected = source ? p.isSourceSelected(t) : p.isRecordSelected(t);
                if (selected != wanted.contains(t)) source ? timeline.toggleSourceTrack(t) : timeline.toggleRecordTrack(t);
            }
            return true;
        }
        if (o.contains(QStringLiteral("patch"))) {
            const QJsonArray a = o.value(QStringLiteral("patch")).toArray();
            if (!timeline.patchTracks(a.at(0).toString(), a.at(1).toString())) return fail(QStringLiteral("cannot patch"));
            return true;
        }
        if (o.contains(QStringLiteral("sync_lock"))) {
            timeline.toggleSyncLock(o.value(QStringLiteral("sync_lock")).toString());
            return true;
        }
        if (o.contains(QStringLiteral("select"))) {
            QList<model::SegmentRef> refs;
            for (const QJsonValue& v : o.value(QStringLiteral("select")).toArray()) {
                const QJsonArray a = v.toArray();
                refs << model::SegmentRef{a.at(0).toString(), a.at(1).toInteger()};
            }
            timeline.setSelection(refs);
            return true;
        }
        if (o.contains(QStringLiteral("move"))) {
            // A segment-mode drag: frames, tracks up, and the red (overwrite) or yellow (splice) tool.
            const QJsonArray a = o.value(QStringLiteral("move")).toArray();
            const model::EditKind kind = o.value(QStringLiteral("tool")).toString() == QLatin1String("extract")
                                             ? model::EditKind::SpliceIn
                                             : model::EditKind::Overwrite;
            if (!timeline.moveSelection(a.at(0).toInteger(), a.at(1).toInt(), kind, o.value(QStringLiteral("copy")).toBool())) {
                return fail(QStringLiteral("the move was refused: %1").arg(timeline.lastMessage()));
            }
            return true;
        }
        if (o.contains(QStringLiteral("rollers"))) {
            QList<model::TrimRoller> rollers;
            for (const QJsonValue& v : o.value(QStringLiteral("rollers")).toArray()) {
                const QJsonArray a = v.toArray();
                model::TrimRoller r;
                r.track = a.at(0).toString();
                r.frame = a.at(1).toInteger();
                const QString side = a.at(2).toString(QStringLiteral("both"));
                r.side = side == QLatin1String("a") ? model::TrimSide::A : side == QLatin1String("b") ? model::TrimSide::B : model::TrimSide::Both;
                rollers << r;
            }
            const model::TrimKind kind = o.value(QStringLiteral("kind")).toString() == QLatin1String("overwrite")
                                             ? model::TrimKind::Overwrite
                                             : model::TrimKind::Ripple;
            if (!timeline.isTrimMode() && !timeline.enterTrimMode()) return fail(QStringLiteral("no trim mode"));
            timeline.setRollers(rollers, kind);
            return true;
        }
        if (o.contains(QStringLiteral("slip"))) {
            // Slip (or with "slide", Slide) the segments in Trim mode; then {"trim": n} moves them.
            QList<model::SegmentRef> refs;
            for (const QJsonValue& v : o.value(QStringLiteral("slip")).toArray()) {
                const QJsonArray a = v.toArray();
                refs << model::SegmentRef{a.at(0).toString(), a.at(1).toInteger()};
            }
            timeline.setSlip(refs, o.value(QStringLiteral("slide")).toBool());
            return true;
        }
        if (o.contains(QStringLiteral("lock"))) {
            timeline.setTracksLocked({o.value(QStringLiteral("lock")).toString()}, true);
            return true;
        }
        if (o.contains(QStringLiteral("trim"))) {
            const bool ok = timeline.trimBy(o.value(QStringLiteral("trim")).toInteger());
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the trim was not refused")
                               : QStringLiteral("the trim was refused: %1").arg(timeline.lastMessage()));
            }
            return true;
        }
        if (o.contains(QStringLiteral("command"))) {
            const QString id = o.value(QStringLiteral("command")).toString();
            // The Timeline's perform() is true for every command it handles; a refused edit leaves
            // its reason in lastMessage().
            const bool ok = Timeline::handles(id) ? timeline.perform(id) && timeline.lastMessage().isEmpty() : composer.perform(id);
            // A step may expect a command to be refused (an edit the guide does not allow).
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(QStringLiteral("%1 %2: %3").arg(id, ok ? QStringLiteral("was not refused") : QStringLiteral("failed"),
                                                             timeline.lastMessage()));
            }
            return true;
        }
        return fail(QStringLiteral("unknown step %1").arg(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact))));
    }

    // The Record monitor's sequence, as the scenario files write it.
    QJsonObject recordTracks() const {
        QJsonObject out;
        const model::LoadedClip* c = composer.state(MonitorId::Record).current();
        if (!c) return out;
        const model::Sequence s = model::Sequence::fromItem(c->item);
        for (const model::Track& t : s.tracks()) {
            QJsonArray segs;
            for (const model::Segment& g : t.segments) {
                if (g.isFiller()) {
                    segs.append(QJsonArray{QStringLiteral("filler"), qint64(g.length)});
                } else {
                    QJsonArray a{g.name, qint64(g.sourceIn), qint64(g.length)};
                    if (g.sourceTrack != t.name()) a.append(g.sourceTrack);
                    // A motion effect's speed (Fit to Fill), rounded to 1/1000.
                    if (g.effect) {
                        if (const auto speed = model::motionSpeed(*g.effect)) {
                            a.append(QJsonObject{{QStringLiteral("speed"), std::round(*speed * 1000.0) / 1000.0}});
                        }
                    }
                    segs.append(a);
                }
            }
            // Trailing filler is not part of a track's material.
            while (!segs.isEmpty() && segs.last().toArray().at(0).toString() == QLatin1String("filler")) segs.removeLast();
            out.insert(t.name(), segs);
        }
        return out;
    }

    QByteArray hash() const {
        // Before the first edit makes a sequence there is none; undoing that edit leaves the new
        // sequence empty (undo does not cover the bin), which is the same state.
        const model::LoadedClip* c = composer.state(MonitorId::Record).current();
        const QJsonObject j = c ? model::Sequence::fromItem(c->item).toJson() : model::Sequence::withDefaultTracks().toJson();
        return QCryptographicHash::hash(QJsonDocument(j).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
    }
};

} // namespace

class TestScenarios : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void scenarios_data() {
        QTest::addColumn<QJsonObject>("scenario");
        for (const QString& folder : scenarioDirs()) {
            const QDir dir(folder);
            const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
            QVERIFY2(!files.isEmpty(), qPrintable(dir.path()));
            for (const QString& file : files) {
                QFile f(dir.filePath(file));
                QVERIFY(f.open(QIODevice::ReadOnly));
                QJsonParseError e{};
                const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &e);
                QVERIFY2(e.error == QJsonParseError::NoError, qPrintable(file + QStringLiteral(": ") + e.errorString()));
                for (const QJsonValue& v : d.array()) {
                    const QJsonObject o = v.toObject();
                    const QString name = file.chopped(5) + QStringLiteral(": ") + o.value(QStringLiteral("name")).toString();
                    QTest::newRow(qPrintable(name)) << o;
                }
            }
        }
    }

    void scenarios() {
        QFETCH(QJsonObject, scenario);
        Run run;
        QVERIFY2(run.setUp(scenario), qPrintable(run.error));
        // Gate 2: each state on the way (by the undo count it was reached at), to compare with undo
        // and redo.
        QList<std::pair<int, QByteArray>> states{{run.timeline.history().undoCount(), run.hash()}};
        for (const QJsonValue& v : scenario.value(QStringLiteral("steps")).toArray()) {
            QVERIFY2(run.step(v.toObject()), qPrintable(run.error));
            // A change that is not an undo step (a sync lock, a track's lock) is part of the state at
            // that undo level.
            const int count = run.timeline.history().undoCount();
            if (count != states.last().first) {
                states.append({count, run.hash()});
            } else {
                states.last().second = run.hash();
            }
        }
        const QJsonObject expected = scenario.value(QStringLiteral("expect")).toObject();
        const QJsonObject actual = run.recordTracks();
        // Where the monitors are, and what the Source monitor shows (Match Frame, p.432-434).
        const QJsonObject monitors = scenario.value(QStringLiteral("expect_monitors")).toObject();
        for (const QString& side : {QStringLiteral("source"), QStringLiteral("record")}) {
            if (!monitors.contains(side)) continue;
            const QJsonObject want = monitors.value(side).toObject();
            const model::MonitorState& m = run.composer.state(run.monitorOf(side));
            if (want.contains(QStringLiteral("position"))) QCOMPARE(m.position(), want.value(QStringLiteral("position")).toInteger());
            if (want.contains(QStringLiteral("clip"))) {
                QVERIFY(m.current());
                QCOMPARE(m.current()->item.name, want.value(QStringLiteral("clip")).toString());
            }
            if (want.contains(QStringLiteral("in"))) {
                const QJsonValue in = want.value(QStringLiteral("in"));
                QCOMPARE(m.in().has_value(), !in.isNull());
                if (m.in()) QCOMPARE(*m.in(), in.toInteger());
            }
            if (want.contains(QStringLiteral("out"))) {
                const QJsonValue out = want.value(QStringLiteral("out"));
                QCOMPARE(m.out().has_value(), !out.isNull());
                if (m.out()) QCOMPARE(*m.out(), out.toInteger());
            }
        }
        for (auto it = expected.begin(); it != expected.end(); ++it) {
            const QByteArray got = QJsonDocument(actual.value(it.key()).toArray()).toJson(QJsonDocument::Compact);
            const QByteArray want = QJsonDocument(it.value().toArray()).toJson(QJsonDocument::Compact);
            QVERIFY2(got == want, qPrintable(QStringLiteral("%1: got %2, expected %3")
                                                 .arg(it.key(), QString::fromUtf8(got), QString::fromUtf8(want))));
        }
        // Undo back to the start, then redo to the end: the same states, in turn.
        for (qsizetype i = states.size() - 1; i > 0; --i) {
            while (run.timeline.history().undoCount() > states.at(i - 1).first) {
                QVERIFY(run.timeline.perform(QStringLiteral("edit.undo")));
            }
            QVERIFY2(run.hash() == states.at(i - 1).second, qPrintable(QStringLiteral("undo to state %1").arg(i - 1)));
        }
        for (qsizetype i = 1; i < states.size(); ++i) {
            while (run.timeline.history().undoCount() < states.at(i).first) {
                QVERIFY(run.timeline.perform(QStringLiteral("edit.redo")));
            }
            QVERIFY2(run.hash() == states.at(i).second, qPrintable(QStringLiteral("redo to state %1").arg(i)));
        }
    }
};

QTEST_MAIN(TestScenarios)
#include "tst_scenarios.moc"
