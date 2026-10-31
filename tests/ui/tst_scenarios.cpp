// Scripted edit scenarios (ROADMAP Phase 4 gates 1 and 2; docs/spec/edit-scenarios.md). Each file in
// tests/scenarios is a JSON list of scenarios: clips, a starting sequence, steps (loading clips,
// marks, positions, tracks, segment selections, trims and commands by their id, as the user would
// give them) and the sequence expected at the end. A Composer and Timeline of their own run them.
// After the last step every scenario is undone step by step back to its starting sequence and
// redone to its end, and each state is compared by hash with the one recorded on the way (gate 2).
#include "lumenis/core/project_format.h"
#include "lumenis/model/audio_eq.h"
#include "lumenis/model/automation.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/multichannel.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/surround_pan.h"
#include "lumenis/ui/audio_eq.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/audio_suite.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QJsonObject>
#include <QMimeData>
#include <QLabel>
#include <QMap>
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

// An Audio EQ as scenarios write it: only the values that differ from model::AudioEq's defaults.
QJsonObject eqToJson(const model::AudioEq& e) {
    const model::AudioEq d;
    QJsonObject o;
    if (e.low != d.low) o.insert(QStringLiteral("low"), e.low);
    if (e.mid != d.mid) o.insert(QStringLiteral("mid"), e.mid);
    if (e.high != d.high) o.insert(QStringLiteral("high"), e.high);
    if (e.lowTurnover != d.lowTurnover) o.insert(QStringLiteral("lowTurnover"), e.lowTurnover);
    if (e.highTurnover != d.highTurnover) o.insert(QStringLiteral("highTurnover"), e.highTurnover);
    if (e.midFrequency != d.midFrequency) o.insert(QStringLiteral("midFrequency"), e.midFrequency);
    if (e.wideMid != d.wideMid) o.insert(QStringLiteral("wideMid"), e.wideMid);
    if (e.in != d.in) o.insert(QStringLiteral("in"), e.in);
    if (!e.preset.isEmpty()) o.insert(QStringLiteral("preset"), e.preset);
    if (e.midOctaves > 0) o.insert(QStringLiteral("midOctaves"), e.midOctaves);
    return o;
}

model::AudioEq eqFromJson(const QJsonObject& o) {
    model::AudioEq e;
    e.low = o.value(QStringLiteral("low")).toDouble(e.low);
    e.mid = o.value(QStringLiteral("mid")).toDouble(e.mid);
    e.high = o.value(QStringLiteral("high")).toDouble(e.high);
    e.lowTurnover = o.value(QStringLiteral("lowTurnover")).toInt(e.lowTurnover);
    e.highTurnover = o.value(QStringLiteral("highTurnover")).toInt(e.highTurnover);
    e.midFrequency = o.value(QStringLiteral("midFrequency")).toDouble(e.midFrequency);
    e.wideMid = o.value(QStringLiteral("wideMid")).toBool(e.wideMid);
    e.in = o.value(QStringLiteral("in")).toBool(e.in);
    e.preset = o.value(QStringLiteral("preset")).toString();
    e.midOctaves = o.value(QStringLiteral("midOctaves")).toDouble();
    return e;
}

// What a scenario starts from and runs in.
struct Run {
    Composer composer;
    Timeline timeline{composer};
    AudioEqTool eq{timeline};
    AudioSuiteTool suite{timeline};
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
        // Clips at another rate than the sequences' are mixed rate clips (motion_adapter.h).
        composer.setAdapterOptions(kRate, false, true);
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
        s.sourceTrack = a.size() > 3 && a.at(3).isString() ? a.at(3).toString() : track;
        s.sourceIn = a.at(1).toInteger();
        s.length = a.at(2).toInteger();
        // A trailing object can give the segment an effect, as model::Effect::toJson writes it.
        if (a.last().isObject() && a.last().toObject().contains(QStringLiteral("effect"))) {
            s.effect = model::Effect::fromJson(a.last().toObject().value(QStringLiteral("effect")).toObject());
        }
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
            // "rate": a mixed rate clip's own frame rate, "50" or "30000/1001" (p.469).
            if (o.contains(QStringLiteral("rate"))) {
                const QStringList r = o.value(QStringLiteral("rate")).toString().split(QLatin1Char('/'));
                i.editRate = Rational(r.value(0).toLongLong(), r.size() > 1 ? r.value(1).toLongLong() : 1);
            }
            i.fields.insert(QStringLiteral("Tracks"), o.value(QStringLiteral("tracks")).toString(QStringLiteral("V1 A1-2")));
            // Multichannel groups, as the Track Formats column has them (p.671).
            if (o.contains(QStringLiteral("formats"))) i.fields.insert(QStringLiteral("Track Formats"), o.value(QStringLiteral("formats")).toString());
            // Apply Gain and Center Pan (p.216, p.714).
            i.audioGain = o.value(QStringLiteral("gain")).toDouble();
            if (o.contains(QStringLiteral("pan"))) i.audioPan = o.value(QStringLiteral("pan")).toInt();
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
            const bool ok = timeline.patchTracks(a.at(0).toString(), a.at(1).toString());
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the patch was not refused") : QStringLiteral("cannot patch: ") + timeline.lastMessage());
            }
            return true;
        }
        if (o.contains(QStringLiteral("split_mono"))) {
            // Split Track to Mono on a track, or "" for Split All Tracks to Mono (p.734-736).
            const bool ok = timeline.splitTracksToMono(o.value(QStringLiteral("split_mono")).toString());
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the split was not refused") : timeline.lastMessage());
            }
            return true;
        }
        if (o.contains(QStringLiteral("mix_format"))) {
            // The Audio Mixer's Sequence Mix Format button (p.686): "stereo", "5.1" or "7.1".
            const auto format = model::formatFromName(o.value(QStringLiteral("mix_format")).toString());
            if (!format) return fail(QStringLiteral("no such mix format"));
            AudioMixer mixer(timeline);
            const bool ok = mixer.setSequenceMixFormat(*format);
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the mix format change was not refused") : QStringLiteral("nothing changed"));
            }
            return true;
        }
        if (o.contains(QStringLiteral("timeline_setting"))) {
            // A Timeline setting's option (p.1218-1221), by its Timeline::Options name.
            Timeline::Options options = timeline.options();
            const QString name = o.value(QStringLiteral("timeline_setting")).toString();
            const bool on = o.value(QStringLiteral("value")).toBool(true);
            if (name == QLatin1String("segmentDragSyncLocks")) {
                options.segmentDragSyncLocks = on;
            } else if (name == QLatin1String("defaultSyncLocks")) {
                options.defaultSyncLocks = on;
            } else {
                return fail(QStringLiteral("unknown Timeline setting %1").arg(name));
            }
            timeline.setOptions(options);
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
        if (o.contains(QStringLiteral("surround_pan"))) {
            // A surround pan at the Record position (p.704-706): [track, channel, pan as in the
            // composition JSON].
            const QJsonArray a = o.value(QStringLiteral("surround_pan")).toArray();
            const bool ok = timeline.setSurroundPan(a.at(0).toString(), a.at(1).toInt(),
                                                    model::surroundPanFromJson(a.at(2).toObject()));
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the pan was not refused") : timeline.lastMessage());
            }
            return true;
        }
        if (o.contains(QStringLiteral("clip_pan"))) {
            // A Clip mode pan of the segment under the Record position (p.702): [track, pan], null
            // for none of its own (Remove Pan).
            const QJsonArray a = o.value(QStringLiteral("clip_pan")).toArray();
            const int pan = a.at(1).isNull() ? model::kDefaultPan : a.at(1).toInt();
            if (!timeline.setClipAudio(a.at(0).toString(), std::nullopt, pan, false, QStringLiteral("Clip Pan"))) {
                return fail(timeline.lastMessage());
            }
            return true;
        }
        if (o.contains(QStringLiteral("default_clip_pan"))) {
            // Commit (true) or Remove (false) Default Clip Pan on Track (p.711): [tracks, commit].
            const QJsonArray a = o.value(QStringLiteral("default_clip_pan")).toArray();
            QStringList tracks;
            for (const QJsonValue& v : a.at(0).toArray()) tracks << v.toString();
            const bool ok = timeline.setDefaultClipPan(tracks, a.at(1).toBool());
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the default clip pan was not refused") : timeline.lastMessage());
            }
            return true;
        }
        if (o.contains(QStringLiteral("adjust_clip_audio"))) {
            // Adjust Pan/Vols on Track (p.711): [tracks, dB, pan].
            const QJsonArray a = o.value(QStringLiteral("adjust_clip_audio")).toArray();
            QStringList tracks;
            for (const QJsonValue& v : a.at(0).toArray()) tracks << v.toString();
            if (!timeline.adjustClipAudio(tracks, a.at(1).toDouble(), a.at(2).toInt())) return fail(timeline.lastMessage());
            return true;
        }
        if (o.contains(QStringLiteral("lfe_only"))) {
            // "This track is LFE only" (p.710): [track, on].
            const QJsonArray a = o.value(QStringLiteral("lfe_only")).toArray();
            const bool ok = timeline.setLfeOnly(a.at(0).toString(), a.at(1).toBool());
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("LFE only was not refused") : timeline.lastMessage());
            }
            return true;
        }
        if (o.contains(QStringLiteral("clip_note"))) {
            // Add Timeline Clip Note on the selected segments (p.483); "" deletes the note (p.484).
            const bool ok = timeline.setClipNote(timeline.selection(), o.value(QStringLiteral("clip_note")).toString());
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the clip note was not refused") : QStringLiteral("the clip note was refused"));
            }
            return true;
        }
        if (o.contains(QStringLiteral("heads_move"))) {
            // Heads view's Alt+drag (p.639): [clip, before clip], over the selected record tracks.
            const QJsonArray a = o.value(QStringLiteral("heads_move")).toArray();
            const bool ok = timeline.moveHeads(a.at(0).toInt(), a.at(1).toInt());
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the heads move was not refused") : timeline.lastMessage());
            }
            return true;
        }
        if (o.contains(QStringLiteral("local_color"))) {
            // Set Local Clip Color on the selected segments (p.598); "" is None.
            const bool ok = timeline.setLocalClipColor(o.value(QStringLiteral("local_color")).toString());
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the local color was not refused") : timeline.lastMessage());
            }
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
        if (o.contains(QStringLiteral("source_track"))) {
            // [track, start, source track]: the segment's right-click Change Source Track (p.595).
            const QJsonArray a = o.value(QStringLiteral("source_track")).toArray();
            QMenu* menu = timeline.contextMenu({}, model::SegmentRef{a.at(0).toString(), a.at(1).toInteger()});
            menu->deleteLater();
            QMenu* change = menu->findChild<QMenu*>(QStringLiteral("changeSourceTrack"));
            if (!change) return fail(QStringLiteral("no Change Source Track on %1 at %2").arg(a.at(0).toString()).arg(a.at(1).toInteger()));
            for (QAction* act : change->actions()) {
                if (act->text() == a.at(2).toString()) {
                    act->trigger();
                    return true;
                }
            }
            return fail(QStringLiteral("Change Source Track does not offer %1").arg(a.at(2).toString()));
        }
        if (o.contains(QStringLiteral("audio_data"))) {
            // The graph keyframe steps act on (Audio Data, p.715): "volume" or "pan".
            const bool pan = o.value(QStringLiteral("audio_data")).toString() == QLatin1String("pan");
            timeline.setAudioData(false, pan ? model::AutomationParam::Pan : model::AutomationParam::Volume);
            return true;
        }
        if (o.contains(QStringLiteral("key_drag"))) {
            // [track, record frame, frames later, value up]: a keyframe drag (p.716).
            const QJsonArray a = o.value(QStringLiteral("key_drag")).toArray();
            const bool ok = timeline.dragKeyframes(model::KeyRef{a.at(0).toString(), a.at(1).toDouble()}, a.at(2).toDouble(),
                                                   a.at(3).toDouble(), false);
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the keyframe drag was not refused") : timeline.lastMessage());
            }
            return true;
        }
        if (o.contains(QStringLiteral("eq"))) {
            // The Audio EQ tool's values, those not given at their defaults (p.739).
            eq.setEq(eqFromJson(o.value(QStringLiteral("eq")).toObject()));
            return true;
        }
        if (o.contains(QStringLiteral("eq_drop"))) {
            // The Audio EQ tool's effect icon dropped on an audio clip in the Timeline (p.740): the
            // tool's values, as the icon carries them, on the segment at [track, record frame].
            const QJsonArray at = o.value(QStringLiteral("eq_drop")).toArray();
            QMimeData mime;
            mime.setData(eqTemplateMime(), eq.iconTemplate());
            const auto dropped = timeline.droppedEq(&mime);
            const bool ok = dropped && timeline.dropEq(at.at(0).toString(), at.at(1).toInteger(), *dropped);
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the EQ drop was not refused") : QStringLiteral("the EQ drop was refused"));
            }
            return true;
        }
        if (o.contains(QStringLiteral("eq_menu"))) {
            // An item of the Audio EQ tool's Fast menu (p.740).
            for (QAction* a : eq.fastMenu()->actions()) {
                if (a->text() == o.value(QStringLiteral("eq_menu")).toString()) {
                    a->trigger();
                    return true;
                }
            }
            return fail(QStringLiteral("no %1 in the Audio EQ Fast menu").arg(o.value(QStringLiteral("eq_menu")).toString()));
        }
        if (o.contains(QStringLiteral("audiosuite"))) {
            // An AudioSuite plug-in chosen in the Plug-In Selection menu (null: None), on the segments
            // under the position on the selected tracks (p.797), then `params` set as its dialog's OK
            // does (p.798).
            const QJsonValue id = o.value(QStringLiteral("audiosuite"));
            bool ok = suite.choosePlugin(id.isNull() ? QString() : id.toString());
            if (ok && o.contains(QStringLiteral("params"))) {
                QMap<QString, double> values = suite.current() ? suite.current()->params : QMap<QString, double>{};
                const QJsonObject params = o.value(QStringLiteral("params")).toObject();
                for (auto it = params.begin(); it != params.end(); ++it) values.insert(it.key(), it.value().toDouble());
                ok = suite.setValues(values);
            }
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the AudioSuite plug-in was not refused") : suite.messageLabel()->text());
            }
            return true;
        }
        if (o.contains(QStringLiteral("audiosuite_menu"))) {
            // An item of the AudioSuite tool's Fast menu (p.799).
            for (QAction* a : suite.fastMenu()->actions()) {
                if (a->text() == o.value(QStringLiteral("audiosuite_menu")).toString()) {
                    a->trigger();
                    return true;
                }
            }
            return fail(QStringLiteral("no %1 in the AudioSuite Fast menu").arg(o.value(QStringLiteral("audiosuite_menu")).toString()));
        }
        if (o.contains(QStringLiteral("punch_in"))) {
            // A recorded take edited in by Audio Punch-In (p.749-751): {"clip": name, "targets": [track or
            // "" for New Track, ...], "punches": [[record frame, length, clip frame], ...]}.
            const QJsonObject p = o.value(QStringLiteral("punch_in")).toObject();
            const QString name = p.value(QStringLiteral("clip")).toString();
            if (!clips.contains(name)) return fail(QStringLiteral("no clip %1").arg(name));
            QStringList targets;
            for (const QJsonValue& t : p.value(QStringLiteral("targets")).toArray()) targets << t.toString();
            QList<Timeline::PunchIn> punches;
            for (const QJsonValue& v : p.value(QStringLiteral("punches")).toArray()) {
                const QJsonArray a = v.toArray();
                punches << Timeline::PunchIn{a.at(0).toInteger(), a.at(1).toInteger(), a.at(2).toInteger()};
            }
            const bool ok = timeline.punchIn(QStringLiteral("bin"), clips.value(name), targets, punches).has_value();
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the punch-in was not refused") : timeline.lastMessage());
            }
            return true;
        }
        if (o.contains(QStringLiteral("insert"))) {
            // [track, slot, effect id or null]: an Audio Track Effect put in or taken out (p.791-795).
            const QJsonArray a = o.value(QStringLiteral("insert")).toArray();
            const std::optional<model::TrackInsert> in =
                a.at(2).isNull() ? std::nullopt : std::optional(Timeline::newInsert(a.at(2).toString()));
            const bool ok = timeline.setTrackInsert(a.at(0).toString(), a.at(1).toInt(), in, QStringLiteral("insert"));
            if (ok == o.value(QStringLiteral("refused")).toBool()) {
                return fail(ok ? QStringLiteral("the insert was not refused") : timeline.lastMessage());
            }
            return true;
        }
        if (o.contains(QStringLiteral("move_insert"))) {
            // [from track, slot, to track, slot, copy]: an insert button dragged (p.795).
            const QJsonArray a = o.value(QStringLiteral("move_insert")).toArray();
            if (!timeline.moveTrackInsert(a.at(0).toString(), a.at(1).toInt(), a.at(2).toString(), a.at(3).toInt(),
                                          a.at(4).toBool())) {
                return fail(timeline.lastMessage());
            }
            return true;
        }
        if (o.contains(QStringLiteral("lock"))) {
            timeline.setTracksLocked({o.value(QStringLiteral("lock")).toString()}, true);
            return true;
        }
        if (o.contains(QStringLiteral("bin_edit"))) {
            // Bin editing into the Timeline (p.623-624): the clip from the bin, spliced in or
            // overwritten at a frame (a drag) or at the Record monitor's marks (V and B).
            const QString name = o.value(QStringLiteral("clip")).toString();
            if (!clips.contains(name)) return fail(QStringLiteral("no clip %1").arg(name));
            const auto kind = o.value(QStringLiteral("bin_edit")).toString() == QLatin1String("splice") ? model::EditKind::SpliceIn
                                                                                                      : model::EditKind::Overwrite;
            std::optional<std::int64_t> at;
            if (o.contains(QStringLiteral("at"))) at = o.value(QStringLiteral("at")).toInteger();
            if (!timeline.editClip(kind, QStringLiteral("bin"), clips.value(name), at)) {
                return fail(QStringLiteral("the bin edit was not made: %1").arg(timeline.lastMessage()));
            }
            return true;
        }
        if (o.contains(QStringLiteral("jkl"))) {
            // J-K-L Trim (p.658-659): L (or J for a negative count) plays the Record monitor from the
            // transition, which is moved n frames on before K stops it.
            const std::int64_t n = o.value(QStringLiteral("jkl")).toInteger();
            composer.setActiveMonitor(MonitorId::Record);
            composer.perform(n < 0 ? QStringLiteral("play.play_reverse") : QStringLiteral("play.play_forward"));
            composer.moveTo(MonitorId::Record, composer.state(MonitorId::Record).position() + n);
            composer.perform(QStringLiteral("play.pause"));
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
    // A nest's tracks: [clip, source IN, length] and ["filler", length], without trailing filler.
    static QJsonObject plainTracks(const model::Sequence& seq) {
        QJsonObject out;
        for (const model::Track& t : seq.tracks()) {
            QJsonArray segs;
            for (const model::Segment& g : t.segments) {
                segs.append(g.isFiller() ? QJsonArray{QStringLiteral("filler"), qint64(g.length)}
                                         : QJsonArray{g.name, qint64(g.sourceIn), qint64(g.length)});
            }
            while (!segs.isEmpty() && segs.last().toArray().at(0).toString() == QLatin1String("filler")) segs.removeLast();
            if (!segs.isEmpty()) out.insert(t.name(), segs);
        }
        return out;
    }

    QJsonObject recordTracks() const {
        QJsonObject out;
        const model::LoadedClip* c = composer.state(MonitorId::Record).current();
        if (!c) return out;
        const model::Sequence s = model::Sequence::fromItem(c->item);
        for (const model::Track& t : s.tracks()) {
            QJsonArray segs;
            for (const model::Segment& g : t.segments) {
                if (g.isFiller()) {
                    QJsonArray f{QStringLiteral("filler"), qint64(g.length)};
                    // A Submaster's nest (FX Guide p.226): its effect and tracks as [clip, in, length].
                    if (g.hasNest()) f.append(QJsonObject{{QStringLiteral("effect"), g.effect ? g.effect->name : QString()},
                                                          {QStringLiteral("nest"), plainTracks(model::nestOf(g))}});
                    segs.append(f);
                } else {
                    QJsonArray a{g.name, qint64(g.sourceIn), qint64(g.length)};
                    if (g.sourceTrack != t.name()) a.append(g.sourceTrack);
                    // A motion effect's speed (Fit to Fill), rounded to 1/1000, and volume and pan
                    // automation keyframes as [[clip frame, value], ...].
                    QJsonObject extra;
                    if (g.effect) {
                        if (const auto speed = model::motionSpeed(*g.effect)) {
                            extra.insert(QStringLiteral("speed"), std::round(*speed * 1000.0) / 1000.0);
                        }
                    }
                    for (const auto& [name, keys] : {std::pair{"volume", &g.volumeKeys}, std::pair{"pan", &g.panKeys}}) {
                        if (keys->isEmpty()) continue;
                        QJsonArray list;
                        for (const model::AudioKey& k : *keys) list.append(QJsonArray{k.frame, k.value});
                        extra.insert(QLatin1String(name), list);
                    }
                    if (g.eq) extra.insert(QStringLiteral("eq"), eqToJson(*g.eq));
                    // A Motion Adapter: the clip's own rate (p.469).
                    if (g.adapter) extra.insert(QStringLiteral("adapter"), QString::fromStdString(frameRateText(g.adapter->clipRate)));
                    // Effect keyframes by parameter as [[position, value], ...]: frames for Fixed
                    // keyframes, else the effect's time rounded to 1/1000 (FX Guide p.124).
                    if (g.effect) {
                        QJsonObject keyed;
                        for (const QString& k : model::keyframedParameters(*g.effect)) {
                            const model::ParameterKeyframes p =
                                model::keyframesOf(*g.effect, k.section(QLatin1Char('/'), 0, 0), k.section(QLatin1Char('/'), 1));
                            QJsonArray list;
                            for (const model::EffectKeyframe& key : p.keys) {
                                list.append(QJsonArray{p.fixed ? key.at : std::round(key.at * 1000.0) / 1000.0, key.value});
                            }
                            keyed.insert(k, list);
                        }
                        if (!keyed.isEmpty()) extra.insert(QStringLiteral("keyframes"), keyed);
                    }
                    if (g.audioSuite) {
                        // [plug-in, {the values not at their defaults}].
                        QJsonObject params;
                        const auto info = media::findAudioSuitePlugin(g.audioSuite->effect);
                        for (auto it = g.audioSuite->params.cbegin(); it != g.audioSuite->params.cend(); ++it) {
                            const media::AudioEffectParam* p = info ? info->param(it.key()) : nullptr;
                            if (!p || p->defaultValue != it.value()) params.insert(it.key(), it.value());
                        }
                        extra.insert(QStringLiteral("audiosuite"), QJsonArray{g.audioSuite->effect, params});
                    }
                    if (!extra.isEmpty()) a.append(extra);
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
        // Gate 2: the state at each undo level (QMap: by level). An undo or redo among the steps
        // must give back the state recorded at its level; a new edit replaces the levels above.
        QMap<int, QByteArray> states{{run.timeline.history().undoCount(), run.hash()}};
        // A step that makes several undo steps (Audio Punch-In, one per punch-in) leaves the levels
        // between unrecorded: each is recorded the first time undo or redo reaches it, and compared
        // after that.
        auto same = [&](int level) {
            if (!states.contains(level)) states.insert(level, run.hash());
            return run.hash() == states.value(level);
        };
        for (const QJsonValue& v : scenario.value(QStringLiteral("steps")).toArray()) {
            const QJsonObject o = v.toObject();
            const int before = run.timeline.history().undoCount();
            QVERIFY2(run.step(o), qPrintable(run.error));
            const int count = run.timeline.history().undoCount();
            const QString id = o.value(QStringLiteral("command")).toString();
            if (count != before && (id == QLatin1String("edit.undo") || id == QLatin1String("edit.redo"))) {
                QVERIFY2(same(count), qPrintable(QStringLiteral("%1 to level %2").arg(id).arg(count)));
            } else {
                // A new step, or a change that is not an undo step (a sync lock, a track's lock),
                // which is part of the state at its level.
                if (count != before) {
                    while (!states.isEmpty() && states.lastKey() > count) states.remove(states.lastKey());
                }
                states.insert(count, run.hash());
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
        // Track formats ("mono", "stereo", "5.1", "7.1") and the channels the material comes from.
        const QJsonObject formats = scenario.value(QStringLiteral("expect_formats")).toObject();
        const QJsonObject channels = scenario.value(QStringLiteral("expect_channels")).toObject();
        if (!formats.isEmpty() || !channels.isEmpty()) {
            const model::Sequence s = model::Sequence::fromItem(run.composer.state(MonitorId::Record).current()->item);
            for (auto it = formats.begin(); it != formats.end(); ++it) {
                const model::Track* t = s.track(it.key());
                QVERIFY2(t, qPrintable(it.key()));
                QCOMPARE(model::formatName(t->format).toLower(), it.value().toString());
            }
            for (auto it = channels.begin(); it != channels.end(); ++it) {
                const model::Track* t = s.track(it.key());
                QVERIFY2(t, qPrintable(it.key()));
                QJsonArray got;
                for (const model::Segment& g : t->segments) {
                    if (!g.isFiller()) got.append(g.sourceTrack);
                }
                QCOMPARE(got, it.value().toArray());
            }
        }
        // Local clip colours: per track, each material segment's ("" for none).
        const QJsonObject locals = scenario.value(QStringLiteral("expect_local_colors")).toObject();
        if (!locals.isEmpty()) {
            const model::Sequence s = model::Sequence::fromItem(run.composer.state(MonitorId::Record).current()->item);
            for (auto it = locals.begin(); it != locals.end(); ++it) {
                const model::Track* t = s.track(it.key());
                QVERIFY2(t, qPrintable(it.key()));
                QJsonArray got;
                for (const model::Segment& g : t->segments) {
                    if (!g.isFiller()) got.append(g.localColor);
                }
                QCOMPARE(got, it.value().toArray());
            }
        }
        // Timeline Clip Notes: per track, each material segment's ("" for none).
        const QJsonObject notes = scenario.value(QStringLiteral("expect_notes")).toObject();
        if (!notes.isEmpty()) {
            const model::Sequence s = model::Sequence::fromItem(run.composer.state(MonitorId::Record).current()->item);
            for (auto it = notes.begin(); it != notes.end(); ++it) {
                const model::Track* t = s.track(it.key());
                QVERIFY2(t, qPrintable(it.key()));
                QJsonArray got;
                for (const model::Segment& g : t->segments) {
                    if (!g.isFiller()) got.append(g.note);
                }
                QCOMPARE(got, it.value().toArray());
            }
        }
        // Surround pans: per track, each material segment's pans (as the composition JSON writes them).
        const QJsonObject pans = scenario.value(QStringLiteral("expect_surround")).toObject();
        if (!pans.isEmpty()) {
            const model::Sequence s = model::Sequence::fromItem(run.composer.state(MonitorId::Record).current()->item);
            for (auto it = pans.begin(); it != pans.end(); ++it) {
                const model::Track* t = s.track(it.key());
                QVERIFY2(t, qPrintable(it.key()));
                QJsonArray got;
                for (const model::Segment& g : t->segments) {
                    if (g.isFiller()) continue;
                    QJsonArray each;
                    for (const model::SurroundPan& p : g.surround) each.append(model::surroundPanToJson(p));
                    got.append(each);
                }
                QCOMPARE(got, it.value().toArray());
            }
        }
        // Clip gain and pan: per track, each material segment's [gain, pan], the pan null without
        // one of its own.
        const QJsonObject clipAudio = scenario.value(QStringLiteral("expect_clip_audio")).toObject();
        if (!clipAudio.isEmpty()) {
            const model::Sequence s = model::Sequence::fromItem(run.composer.state(MonitorId::Record).current()->item);
            for (auto it = clipAudio.begin(); it != clipAudio.end(); ++it) {
                const model::Track* t = s.track(it.key());
                QVERIFY2(t, qPrintable(it.key()));
                QJsonArray got;
                for (const model::Segment& g : t->segments) {
                    if (!g.isFiller()) got.append(QJsonArray{g.gain, g.ownPan() ? QJsonValue(g.pan) : QJsonValue()});
                }
                QCOMPARE(got, it.value().toArray());
            }
        }
        // LFE-only tracks.
        if (scenario.contains(QStringLiteral("expect_lfe_only"))) {
            const model::Sequence s = model::Sequence::fromItem(run.composer.state(MonitorId::Record).current()->item);
            QJsonArray got;
            for (const model::Track& t : s.tracks()) {
                if (t.lfeOnly) got.append(t.name());
            }
            QCOMPARE(got, scenario.value(QStringLiteral("expect_lfe_only")).toArray());
        }
        // The Sequence Mix Format.
        if (scenario.contains(QStringLiteral("expect_mix"))) {
            const model::Sequence s = model::Sequence::fromItem(run.composer.state(MonitorId::Record).current()->item);
            QCOMPARE(model::formatName(s.mixFormat()).toLower(), scenario.value(QStringLiteral("expect_mix")).toString());
        }
        // Audio Track Effect inserts: per track, the effect in each slot a-e, or null.
        const QJsonObject inserts = scenario.value(QStringLiteral("expect_inserts")).toObject();
        if (!inserts.isEmpty()) {
            const model::Sequence s = model::Sequence::fromItem(run.composer.state(MonitorId::Record).current()->item);
            for (auto it = inserts.begin(); it != inserts.end(); ++it) {
                const model::Track* t = s.track(it.key());
                QVERIFY2(t, qPrintable(it.key()));
                QJsonArray got;
                for (const auto& in : t->inserts) got.append(in ? QJsonValue(in->effect) : QJsonValue());
                QCOMPARE(got, it.value().toArray());
            }
        }
        for (auto it = expected.begin(); it != expected.end(); ++it) {
            const QByteArray got = QJsonDocument(actual.value(it.key()).toArray()).toJson(QJsonDocument::Compact);
            const QByteArray want = QJsonDocument(it.value().toArray()).toJson(QJsonDocument::Compact);
            QVERIFY2(got == want, qPrintable(QStringLiteral("%1: got %2, expected %3")
                                                 .arg(it.key(), QString::fromUtf8(got), QString::fromUtf8(want))));
        }
        // Undo back to the start, then redo as far as it goes: each level's state, in turn.
        while (run.timeline.history().canUndo()) {
            QVERIFY(run.timeline.perform(QStringLiteral("edit.undo")));
            const int level = run.timeline.history().undoCount();
            QVERIFY2(same(level), qPrintable(QStringLiteral("undo to level %1").arg(level)));
        }
        while (run.timeline.history().canRedo()) {
            QVERIFY(run.timeline.perform(QStringLiteral("edit.redo")));
            const int level = run.timeline.history().undoCount();
            QVERIFY2(same(level), qPrintable(QStringLiteral("redo to level %1").arg(level)));
        }
    }
};

QTEST_MAIN(TestScenarios)
#include "tst_scenarios.moc"
