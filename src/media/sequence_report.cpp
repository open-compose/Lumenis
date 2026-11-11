// The Sequence Report (sequence_report.h; Editing Guide p.368-371, p.237-238).
#include "lumenis/media/sequence_report.h"

#include "lumenis/core/timecode.h"
#include "lumenis/media/ofx_host.h"
#include "lumenis/model/color_correction.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/model/nest.h"

#include <QDateTime>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <limits>

namespace lumenis::media {

namespace {

QString kindText(model::BinItemKind kind) {
    switch (kind) {
    case model::BinItemKind::MasterClip: return QStringLiteral("Master Clip");
    case model::BinItemKind::Subclip: return QStringLiteral("Subclip");
    case model::BinItemKind::AudioClip: return QStringLiteral("Audio Clip");
    case model::BinItemKind::Sequence: return QStringLiteral("Sequence");
    case model::BinItemKind::Source: return QStringLiteral("Source");
    case model::BinItemKind::Effect: return QStringLiteral("Effect");
    case model::BinItemKind::MotionEffect: return QStringLiteral("Motion Effect");
    case model::BinItemKind::RenderedEffect: return QStringLiteral("Rendered Effect");
    case model::BinItemKind::Group: return QStringLiteral("Group");
    case model::BinItemKind::Title: return QStringLiteral("Title");
    case model::BinItemKind::Script: return QStringLiteral("Script");
    }
    return {};
}

// Where the walk is: the track's label and how nested.
struct Place {
    QString track;
    int depth = 0;
};

// A clip the sequence uses, where.
struct ReportedClip {
    QString track;
    std::int64_t start = 0;
    model::BinItem clip;
    bool offline = false;
    bool grouped = false; // one of a group clip's angles
    bool selectedAngle = true;
};

class Walker {
public:
    Walker(const SequenceReportInput& input, const SequenceReportOptions& options) : in_(input), opt_(options) {}

    void walk() {
        for (const QString& name : in_.sequence.trackNames()) {
            if (opt_.enabledTracksOnly && !in_.enabledTracks.isEmpty() && !in_.enabledTracks.contains(name)) continue;
            if (const model::Track* t = in_.sequence.track(name)) walkTrack(*t, {name, 0}, 0, 0, std::numeric_limits<std::int64_t>::max());
        }
    }

    QList<ReportedEffect> effects;
    QList<ReportedClip> clips;

private:
    // Whether sequence frames [a, b] are in the report's range.
    bool inRange(std::int64_t a, std::int64_t b) const {
        if (!opt_.useMarks) return true;
        if (in_.markIn && b < *in_.markIn) return false;
        if (in_.markOut && a > *in_.markOut) return false;
        return true;
    }

    void addEffect(const Place& at, std::int64_t a, std::int64_t b, const model::Effect& e, bool renderable = true) {
        if (!inRange(a, b)) return;
        ReportedEffect r;
        r.track = at.track;
        r.start = a;
        r.end = b;
        r.name = e.name;
        r.category = e.category;
        r.depth = at.depth;
        r.renderable = renderable;
        if (!ofxTaggedPlugin(e).isEmpty() || isOfxEffect(e)) {
            r.plugin = true;
            const QJsonObject tag = e.parameters.value(kOfxPluginTag).toObject();
            r.pluginId = tag.value(QStringLiteral("id")).toString();
            r.pluginVersion = tag.value(QStringLiteral("version")).toString();
            if (const OfxPluginInfo* p = ofxPluginOf(e)) {
                if (r.pluginId.isEmpty()) r.pluginId = p->id;
                if (r.pluginVersion.isEmpty()) r.pluginVersion = QStringLiteral("%1.%2").arg(p->versionMajor).arg(p->versionMinor);
                r.pluginVendor = p->grouping.section(QLatin1Char('/'), 0, 0);
            }
            r.missing = isMissingOfxEffect(e);
        }
        effects << r;
    }

    // `origin` is where the track's frame `nestIn` is in the sequence; frames after `last` (a nest's parent's end) are
    // not seen.
    void walkTrack(const model::Track& track, const Place& at, std::int64_t origin, std::int64_t nestIn, std::int64_t last) {
        std::int64_t pos = 0;
        for (const model::Segment& s : track.segments) {
            // The segment's frames in the sequence.
            const std::int64_t a = origin + pos - nestIn;
            const std::int64_t b = a + std::max<std::int64_t>(s.length, 1) - 1;
            pos += s.length;
            if (b < origin) continue;
            if (a > last) break;
            if (s.transition) {
                addEffect(at, a - s.transition->before(), a + std::max<std::int64_t>(s.transition->after(), 1) - 1, s.transition->effect);
            }
            if (s.effect) addEffect(at, a, b, *s.effect);
            if (s.eq) {
                model::Effect eq{QStringLiteral("Audio"), QStringLiteral("Audio EQ"), {}};
                addEffect(at, a, b, eq, false);
            }
            if (s.audioSuite) {
                model::Effect suite{QStringLiteral("Audio"), QStringLiteral("AudioSuite"), {}};
                addEffect(at, a, b, suite);
            }
            // "Non-renderable effects, such as pan/volume effects" (p.370).
            if (track.kind == model::TrackKind::Audio && (s.hasAutomation() || s.gain != 0.0 || s.ownPan())) {
                model::Effect panVolume{QStringLiteral("Audio"), QStringLiteral("Pan/Volume"), {}};
                addEffect(at, a, b, panVolume, false);
            }
            if (s.hasCorrection()) {
                const std::size_t before = static_cast<std::size_t>(effects.size());
                model::Effect cc{QStringLiteral("Color Correction"), QStringLiteral("Color Correction"), {}};
                addEffect(at, a, b, cc);
                if (static_cast<std::size_t>(effects.size()) > before) {
                    // A correction applied through a relationship, not to this segment alone (p.616).
                    const bool source = !s.sourceCorrection.isEmpty() && model::isSegmentOnly(s.sourceRelationship);
                    const bool program = !s.programCorrection.isEmpty() && model::isSegmentOnly(s.programRelationship);
                    effects.last().relationshipOnly = !source && !program;
                }
            }
            if (!s.isFiller() && inRange(a, b)) addClips(at, a, s);
            // "Effects that are nested inside of other effects show the parent effect track they are applied to with the
            // track name in parentheses and indented to show the nesting relationship" (p.370).
            if (s.hasNest()) {
                const model::Sequence nest = model::nestOf(s);
                for (const QString& name : nest.trackNames()) {
                    if (const model::Track* t = nest.track(name)) {
                        walkTrack(*t, {QStringLiteral("(%1) %2").arg(at.track, name), at.depth + 1}, a, s.nestIn, b);
                    }
                }
            }
        }
    }

    void addClips(const Place& at, std::int64_t start, const model::Segment& s) {
        const auto clip = in_.clip ? in_.clip(s) : std::nullopt;
        model::BinItem item;
        if (clip) {
            item = *clip;
        } else {
            item.name = s.name;
            item.mobId = s.sourceMob;
        }
        // A group clip: its angles' clips, the selected one marked.
        if (clip) {
            if (const auto group = model::groupOf(*clip); group && !group->angles.isEmpty()) {
                const int selected = s.groupAngle.value_or(group->selected);
                for (int i = 0; i < group->angles.size(); ++i) {
                    ReportedClip r;
                    r.track = at.track;
                    r.start = start;
                    r.clip = group->angles.at(i).clip;
                    r.offline = in_.offline && in_.offline(r.clip);
                    r.grouped = true;
                    r.selectedAngle = i == selected;
                    clips << r;
                }
                return;
            }
        }
        ReportedClip r;
        r.track = at.track;
        r.start = start;
        r.clip = item;
        r.offline = !clip || (in_.offline && in_.offline(item));
        clips << r;
    }

    const SequenceReportInput& in_;
    const SequenceReportOptions& opt_;
};

QString csvField(const QString& text) {
    if (!text.contains(QLatin1Char(',')) && !text.contains(QLatin1Char('"')) && !text.contains(QLatin1Char('\n'))) return text;
    QString quoted = text;
    quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

// Rows of fields, written as text (columns separated by two spaces, the first row a heading) or as CSV.
class Table {
public:
    explicit Table(bool csv) : csv_(csv) {}
    void title(const QString& text) {
        if (!out_.isEmpty()) out_ += QLatin1Char('\n');
        out_ += csv_ ? csvField(text) + QLatin1Char('\n') : text + QLatin1Char('\n');
    }
    void line(const QString& text) { out_ += csv_ ? csvField(text) + QLatin1Char('\n') : QStringLiteral("  ") + text + QLatin1Char('\n'); }
    void row(const QStringList& fields, int indent = 0) {
        if (csv_) {
            QStringList quoted;
            for (const QString& f : fields) quoted << csvField(f);
            out_ += quoted.join(QLatin1Char(',')) + QLatin1Char('\n');
        } else {
            out_ += QStringLiteral("  ") + QString(indent * 2, QLatin1Char(' ')) + fields.join(QStringLiteral("  ")) + QLatin1Char('\n');
        }
    }
    QString text() const { return out_; }

private:
    bool csv_;
    QString out_;
};

bool keep(const ReportedEffect& e, const SequenceReportOptions& o) {
    if (o.skipNonRenderable && !e.renderable) return false;
    if (o.skipRelationshipOnly && e.relationshipOnly) return false;
    if (o.nestedOnly && e.depth == 0) return false;
    if (o.missingOnly && !e.missing) return false;
    return true;
}

} // namespace

QList<ReportedEffect> reportedEffects(const SequenceReportInput& input, const SequenceReportOptions& options) {
    Walker w(input, options);
    w.walk();
    QList<ReportedEffect> out;
    for (const ReportedEffect& e : w.effects) {
        if (keep(e, options)) out << e;
    }
    return out;
}

QString sequenceReport(const SequenceReportInput& input, const SequenceReportOptions& options) {
    const TimecodeFormat format = TimecodeFormat::forRate(input.rate, input.dropFrame);
    auto tc = [&](std::int64_t frame) { return QString::fromStdString(Timecode(input.startFrame + frame, format).toString()); };
    Walker w(input, options);
    w.walk();
    Table t(options.csv);
    t.title(QStringLiteral("Sequence Report"));
    t.row({QStringLiteral("Bin"), input.binName});
    t.row({QStringLiteral("Sequence"), input.name});
    t.row({QStringLiteral("Starting TC"), tc(0)});
    if (!input.project.isEmpty()) t.row({QStringLiteral("Project"), input.project});
    t.row({QStringLiteral("Tracks"), options.enabledTracksOnly && !input.enabledTracks.isEmpty() ? input.enabledTracks.join(QStringLiteral(" ")) : QStringLiteral("All")});
    if (options.useMarks && (input.markIn || input.markOut)) {
        t.row({QStringLiteral("Marks"), QStringLiteral("%1 - %2").arg(input.markIn ? tc(*input.markIn) : QString(), input.markOut ? tc(*input.markOut) : QString())});
    }
    t.row({QStringLiteral("Created"), QDateTime::currentDateTime().toString(Qt::ISODate)});

    QList<ReportedEffect> effects;
    for (const ReportedEffect& e : w.effects) {
        if (keep(e, options)) effects << e;
    }
    auto effectName = [](const ReportedEffect& e) { return e.missing ? QStringLiteral("Unavailable Effect (%1)").arg(e.name) : e.name; };
    if (options.effectSummary) {
        // "The types of effects and how many were found in your sequence, the breakdown by effect type, and an effect
        // plug-in summary" (p.370).
        t.title(QStringLiteral("Effect Summary"));
        t.line(QStringLiteral("Effects found: %1").arg(effects.size()));
        QMap<QString, QMap<QString, int>> byType;
        for (const ReportedEffect& e : effects) ++byType[e.category][effectName(e)];
        t.row({QStringLiteral("Type"), QStringLiteral("Effect"), QStringLiteral("Count")});
        for (auto c = byType.cbegin(); c != byType.cend(); ++c) {
            for (auto n = c->cbegin(); n != c->cend(); ++n) t.row({c.key(), n.key(), QString::number(n.value())});
        }
        // "The name, the vendor, the version and the ID of the plug-in" (p.368).
        QStringList seen;
        QList<ReportedEffect> plugins;
        for (const ReportedEffect& e : effects) {
            if (e.plugin && !seen.contains(e.pluginId + e.name)) {
                seen << e.pluginId + e.name;
                plugins << e;
            }
        }
        if (!plugins.isEmpty()) {
            t.line(QStringLiteral("Plug-in effects: %1").arg(plugins.size()));
            t.row({QStringLiteral("Plug-in"), QStringLiteral("Vendor"), QStringLiteral("Version"), QStringLiteral("ID")});
            for (const ReportedEffect& e : plugins) t.row({effectName(e), e.pluginVendor, e.pluginVersion, e.pluginId});
        }
    }
    if (options.effectLocations) {
        // "Track, start timecode, end timecode and effect name" (p.370).
        t.title(QStringLiteral("Effect Location List"));
        t.row({QStringLiteral("Track"), QStringLiteral("Start"), QStringLiteral("End"), QStringLiteral("Effect")});
        for (const ReportedEffect& e : effects) t.row({e.track, tc(e.start), tc(e.end + 1), effectName(e)}, e.depth);
    }
    QList<ReportedClip> clips;
    for (const ReportedClip& c : w.clips) {
        if (options.offlineOnly && !c.offline) continue;
        if (options.skipNonSelectedGroupClips && c.grouped && !c.selectedAngle) continue;
        clips << c;
    }
    if (options.clipSummary) {
        // "The number of clips found, type of clip, track, offline information, clip name, and clip Mob ID" (p.370).
        t.title(QStringLiteral("Clip Summary"));
        t.line(QStringLiteral("Clips found: %1").arg(clips.size()));
        QStringList heading{QStringLiteral("Type"), QStringLiteral("Track"), QStringLiteral("Start"), QStringLiteral("Offline"), QStringLiteral("Name")};
        if (options.showUid) heading << QStringLiteral("Mob ID");
        t.row(heading);
        for (const ReportedClip& c : clips) {
            QStringList row{kindText(c.clip.kind) + (c.grouped ? (c.selectedAngle ? QStringLiteral(" (group, selected)") : QStringLiteral(" (group)")) : QString()),
                            c.track, tc(c.start), c.offline ? QStringLiteral("Offline") : QString(), c.clip.name};
            if (options.showUid) row << QString::fromStdString(c.clip.mobId.toString());
            t.row(row);
        }
    }
    if (options.sourceSummary) {
        // "The number of tape-based sources found, project name, tape name, tape ID, and tape Mob ID. It also displays a
        // list of import paths for any imported clips, such as graphics" (p.370).
        struct Tape {
            QString name;
            QString mob;
        };
        QList<Tape> tapes;
        QStringList seenTapes;
        QStringList paths;
        for (const ReportedClip& c : clips) {
            const QString tape = c.clip.fields.value(QStringLiteral("Tape"));
            if (!tape.isEmpty() && !seenTapes.contains(tape)) {
                seenTapes << tape;
                QString mob;
                for (const model::ClipSource& s : c.clip.sources) {
                    if (s.kind == model::ClipSource::Kind::Tape) mob = QString::fromStdString(s.mob.toString());
                }
                tapes.append({tape, mob});
            }
            const QString file = c.clip.fields.value(QStringLiteral("Source File"));
            if (!file.isEmpty()) {
                const QString folder = c.clip.fields.value(QStringLiteral("Source Path"));
                const QString path = folder.isEmpty() ? file : folder + QLatin1Char('/') + file;
                if (!paths.contains(path)) paths << path;
            }
        }
        t.title(QStringLiteral("Source Summary"));
        t.line(QStringLiteral("Tape sources found: %1").arg(tapes.size()));
        if (!input.project.isEmpty()) t.line(QStringLiteral("Project: %1").arg(input.project));
        QStringList heading{QStringLiteral("Tape"), QStringLiteral("Tape ID")};
        if (options.showUid) heading << QStringLiteral("Tape Mob ID");
        t.row(heading);
        for (int i = 0; i < tapes.size(); ++i) {
            QStringList row{tapes.at(i).name, QString::number(i + 1)};
            if (options.showUid) row << tapes.at(i).mob;
            t.row(row);
        }
        t.line(QStringLiteral("Import paths: %1").arg(paths.size()));
        for (const QString& p : paths) t.row({p});
    }
    return t.text();
}

} // namespace lumenis::media
