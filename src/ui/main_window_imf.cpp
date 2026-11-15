// IMF in the application (11.5c; docs/spec/imf.md; Editing Guide p.333-340): Tools > IMF Window, the "IMF" panel, and
// a CPL dragged from it into a bin, which links the package's track files as clips and makes the composition a
// sequence of them.
#include "lumenis/ui/main_window.h"

#include "lumenis/interop/imf.h"
#include "lumenis/interop/imf_writer.h"
#include "lumenis/media/imf_export.h"
#include "lumenis/media/link.h"
#include "lumenis/model/multichannel.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/imf_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/tool_windows.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>

namespace lumenis::ui {

void MainWindow::setUpImf() {
    imfWindow_ = new ImfWindow;
    panels_->addPanel(new Panel(QStringLiteral("IMF"), tr("IMF Window"), imfWindow_));
    connect(imfWindow_, &ImfWindow::failed, this, [this](const QString& reason) { reportFailure(this, tr("IMF: %1").arg(reason)); });
    connect(imfWindow_, &ImfWindow::binItemsDropped, this,
            [this](const QString& binId, const QStringList& ids, int draft, const QString& root) { addImfSequences(binId, ids, draft, root); });
    connect(imfWindow_, &ImfWindow::exportRequested, this, [this](int draft) { exportImfDraft(draft); });
    // The left-hand menu of the audio mapping: the sequence's audio tracks with their formats (p.338).
    imfWindow_->setSequenceTracks([this](const ImfDraftCpl& cpl) {
        QList<std::pair<QString, QString>> out;
        BinDocument* doc = binContainer_->document(cpl.binId);
        const model::BinItem* item = doc ? doc->bin().item(cpl.itemId) : nullptr;
        if (!item) return out;
        const model::Sequence seq = model::Sequence::fromItem(*item);
        for (const model::Track& t : seq.tracks()) {
            if (t.kind == model::TrackKind::Audio) out << std::pair{t.name(), model::formatName(t.format)};
        }
        return out;
    });
    // A CPL is red while the project's format is not one the IMF Supported Formats table lists (p.340-341).
    imfWindow_->setCplCheck([this](const ImfDraftCpl& cpl) {
        if (!project_) return tr("Open a project");
        const ProjectFormat& f = project_->settings.format;
        QString why;
        if (media::imfFormatSupported(QSize(f.width, f.height), f.editRate, f.scan == ScanMode::Interlaced, cpl.profile, &why)) return QString();
        return why;
    });
}

int MainWindow::addImfSequences(const QString& binId, const QStringList& itemIds, int draft, const QString& packageRoot) {
    // "To add to an existing package ... drag the sequence to the root folder of the IMF window" (p.336): ours, into
    // a Supplemental of it, which can use the package's track files.
    if (!packageRoot.isEmpty()) {
        draft = imfWindow_->supplementOf(packageRoot);
        if (draft < 0) draft = imfWindow_->newSupplemental(packageRoot);
        if (draft < 0) return 0;
    }
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    QList<ImfDraftCpl> cpls;
    for (const QString& id : itemIds) {
        const model::BinItem* item = doc ? doc->bin().item(id) : nullptr;
        if (!item || item->kind != model::BinItemKind::Sequence) continue;
        ImfDraftCpl cpl;
        cpl.binId = binId;
        cpl.itemId = id;
        cpl.sequenceName = item->name;
        cpls << cpl;
    }
    if (cpls.isEmpty()) {
        reportFailure(this, tr("IMF: only sequences go into an IMF package."));
        return 0;
    }
    imfWindow_->addSequences(draft, cpls);
    return static_cast<int>(cpls.size());
}

// A Supplemental's picture (ours, docs/spec/imf.md "Supplementals"): V1's segments that still play an Original's track
// file as it is (a clip linked from it, no effect or transition, nothing on another video track over it) name that
// track file; the spans between are written new.
QList<MainWindow::ImfPictureRun> MainWindow::imfPictureRuns(const model::Sequence& seq, const QString& originalRoot) const {
    QList<ImfPictureRun> runs;
    const interop::imf::Package* original = nullptr;
    for (const interop::imf::Package& p : imfWindow_->packages()) {
        if (p.root == originalRoot) original = &p;
    }
    const model::Track* v1 = seq.track(QStringLiteral("V1"));
    const std::int64_t total = seq.duration();
    auto addNew = [&](std::int64_t start, std::int64_t length) {
        if (length <= 0) return;
        if (!runs.isEmpty() && runs.last().trackFile.isNull() && runs.last().start + runs.last().length == start) {
            runs.last().length += length;
        } else {
            runs << ImfPictureRun{start, length, QUuid(), 0, 0, Rational(24)};
        }
    };
    // Whether another video track has material over [start, start + length).
    auto covered = [&](std::int64_t start, std::int64_t length) {
        for (const model::Track& t : seq.tracks()) {
            if (t.kind != model::TrackKind::Video || t.name() == QLatin1String("V1")) continue;
            std::int64_t at = 0;
            for (const model::Segment& s : t.segments) {
                if (!s.isFiller() && at < start + length && at + s.length > start) return true;
                at += s.length;
            }
        }
        return false;
    };
    std::int64_t at = 0;
    if (v1 && original) {
        for (int n = 0; n < v1->segments.size(); ++n) {
            const model::Segment& s = v1->segments.at(n);
            const bool nextTransition = n + 1 < v1->segments.size() && v1->segments.at(n + 1).transition.has_value();
            QUuid file;
            if (!s.isFiller() && !s.effect && !s.transition && !nextTransition && s.nest.isEmpty() && !covered(at, s.length)) {
                BinDocument* doc = binContainer_->document(s.binId);
                const model::BinItem* clip = doc ? doc->bin().item(s.clipId) : nullptr;
                const QUuid id = clip ? interop::imf::uuidOf(clip->fields.value(QStringLiteral("IMF Track File"))) : QUuid();
                if (!id.isNull() && original->assets.contains(id)) file = id;
            }
            if (file.isNull()) {
                addNew(at, s.length);
            } else {
                // The track file's whole length and rate, from the Original's compositions.
                ImfPictureRun run{at, s.length, file, s.sourceIn, s.length, project_ ? project_->settings.format.editRate : Rational(24)};
                for (const interop::imf::Composition& c : original->compositions) {
                    for (const interop::imf::Segment& g : c.segments) {
                        for (const interop::imf::Sequence& q : g.sequences) {
                            for (const interop::imf::Resource& r : q.resources) {
                                if (r.trackFile != file) continue;
                                run.intrinsic = r.intrinsicDuration;
                                run.editRate = r.editRate;
                            }
                        }
                    }
                }
                runs << run;
            }
            at += s.length;
        }
    }
    addNew(at, total - at);
    return runs;
}

QString MainWindow::exportImfDraft(int draft) {
    if (!imfWindow_ || draft < 0 || draft >= imfWindow_->drafts().size() || !imfWindow_->draftReady(draft)) return {};
    const ImfDraft d = imfWindow_->drafts().at(draft);
    const QString root = QFileInfo(d.root()).absoluteFilePath();
    auto fail = [&](const QString& why, bool remove) {
        if (remove) QDir(root).removeRecursively();
        reportFailure(this, tr("IMF: %1").arg(why));
        return QString();
    };
    if (QFileInfo::exists(root)) return fail(tr("%1 already exists.").arg(QDir::toNativeSeparators(root)), false);
    if (!QDir().mkpath(root)) return fail(tr("%1 could not be made.").arg(QDir::toNativeSeparators(root)), false);

    interop::imf::NewPackage package;
    package.root = root;
    package.annotation = d.annotation;
    package.issuer = d.issuer;
    package.creator = d.creator;
    for (const ImfDraftImp& imp : d.imps) {
        if (imp.cpls.isEmpty()) continue;
        interop::imf::NewPackingList pkl;
        pkl.annotation = imp.annotation;
        pkl.issuer = imp.issuer;
        pkl.creator = imp.creator;
        for (const ImfDraftCpl& cpl : imp.cpls) {
            BinDocument* doc = binContainer_->document(cpl.binId);
            if (!doc) doc = binContainer_->openBin(cpl.binId);
            const model::BinItem* item = doc ? doc->bin().item(cpl.itemId) : nullptr;
            if (!item) return fail(tr("the sequence %1 is no longer in its bin.").arg(cpl.sequenceName), true);
            // "be sure all desired tracks are enabled ... any tracks not enabled will be skipped" (p.336): the tracks
            // enabled in the Timeline for the sequence in the Record monitor, every track otherwise.
            const model::Sequence seq = model::Sequence::fromItem(*item);
            QStringList enabled;
            const model::LoadedClip* shown = composer_->state(MonitorId::Record).current();
            if (shown && shown->item.id == item->id) {
                enabled = timeline_->patch().selectedRecordTracks(seq);
            } else {
                for (const model::Track& t : seq.tracks()) enabled << t.name();
            }
            QStringList videoTracks, audioTracks;
            for (const model::Track& t : seq.tracks()) {
                if (!enabled.contains(t.name())) continue;
                if (t.kind == model::TrackKind::Video) videoTracks << t.name();
                if (t.kind == model::TrackKind::Audio) audioTracks << t.name();
            }
            // One pass of exportMovieFile's sink for the given tracks and audio format.
            auto pass = [&](const QStringList& tracks, const QString& audioFormat,
                            std::function<bool(const media::MovieSource&, const media::MovieExportOptions&,
                                               const std::function<bool(double)>&, QString*)> sink, QString* why) {
                QJsonArray only;
                for (const QString& t : tracks) only << t;
                movieSink_ = std::move(sink);
                exportOverride_ = {{QStringLiteral("useMarks"), false},
                                   {QStringLiteral("useSelectedTracks"), false},
                                   {QStringLiteral("movieTracks"), QStringLiteral("both")},
                                   {QStringLiteral("movieAudioFormat"), audioFormat},
                                   {QStringLiteral("tracksOnly"), only}};
                const bool ok = exportMovieFile(*item, cpl.binId, QString(), QStringLiteral("sink"), why);
                exportOverride_ = {};
                movieSink_ = {};
                return ok;
            };
            QList<interop::imf::TrackFile> composed;
            QString error;
            // The picture: whole, or for a Supplemental the Original's track files where V1 still plays them.
            if (videoTracks.isEmpty()) return fail(tr("%1: there are no video tracks to export.").arg(cpl.contentTitle), true);
            const QList<ImfPictureRun> runs = d.isSupplemental() ? imfPictureRuns(seq, d.original) : QList<ImfPictureRun>{};
            const bool picture = pass(videoTracks, QStringLiteral("stereo"),
                [&](const media::MovieSource& source, const media::MovieExportOptions&, const std::function<bool(double)>& progress,
                    QString* sinkError) {
                    media::MovieSource still = source;
                    still.sound = {};
                    media::ImfTrackOptions options;
                    options.profile = cpl.profile;
                    options.name = cpl.contentTitle;
                    options.channels = 0;
                    // Spans not taken from the Original, each a track file of its own.
                    QList<ImfPictureRun> spans = runs;
                    if (spans.isEmpty()) spans << ImfPictureRun{0, source.frames, QUuid(), 0, 0, source.frameRate};
                    for (const ImfPictureRun& run : spans) {
                        if (run.length <= 0) continue;
                        if (!run.trackFile.isNull()) {
                            composed << interop::imf::TrackFile{run.trackFile, QString(), true, run.intrinsic, run.editRate,
                                                                run.entry, run.length, true};
                            continue;
                        }
                        media::MovieSource part = still;
                        part.frames = std::min(run.length, source.frames - run.start);
                        part.picture = [&source, start = run.start](std::int64_t f) { return source.picture(start + f); };
                        QList<media::ImfTrackFile> files;
                        if (!media::writeImfTrackFiles(root, part, options, &files, progress, sinkError)) return false;
                        for (const media::ImfTrackFile& f : files) {
                            composed << interop::imf::TrackFile{f.id, f.path, true, f.duration, f.editRate, 0, -1, false};
                        }
                    }
                    return true;
                }, &error);
            if (!picture) return fail(tr("%1: %2").arg(cpl.contentTitle, error), true);
            // The sound: each IMF audio track of the mapping mixed from its tracks in its format (p.338-340).
            for (const ImfAudioTrack& row : cpl.audio) {
                if (audioTracks.isEmpty()) break; // a sequence without sound has no IMF audio tracks
                QStringList feeding = audioTracks;
                bool master = true;
                if (row.source.startsWith(QLatin1String("track:"))) {
                    feeding = {row.source.mid(6)};
                } else if (row.source.startsWith(QLatin1String("group:"))) {
                    feeding.clear();
                    for (const ImfTrackGroup& g : cpl.groups) {
                        if (QStringLiteral("group:%1").arg(g.name) != row.source) continue;
                        feeding = g.tracks;
                        master = g.masterFader;
                    }
                }
                feeding.removeIf([&](const QString& t) { return !audioTracks.contains(t); });
                if (feeding.isEmpty()) return fail(tr("%1: an IMF audio track has no enabled tracks to mix.").arg(cpl.contentTitle), true);
                withoutMasterFader_ = !master;
                const bool ok = pass(feeding, row.format,
                    [&](const media::MovieSource& source, const media::MovieExportOptions&, const std::function<bool(double)>& progress,
                        QString* sinkError) {
                        media::ImfSoundOptions so;
                        so.format = row.format;
                        so.contentKind = row.contentKind;
                        so.language = row.language;
                        so.region = row.region;
                        so.name = cpl.contentTitle;
                        media::ImfTrackFile f;
                        if (!media::writeImfSoundFile(root, source, so, &f, progress, sinkError)) return false;
                        composed << interop::imf::TrackFile{f.id, f.path, false, f.duration, f.editRate, 0, -1, false};
                        return true;
                    }, &error);
                withoutMasterFader_ = false;
                if (!ok) return fail(tr("%1: %2").arg(cpl.contentTitle, error), true);
            }
            interop::imf::NewComposition c;
            c.playlist.contentTitle = cpl.contentTitle;
            c.playlist.contentKind = cpl.contentKind;
            c.playlist.issuer = cpl.issuer;
            c.playlist.creator = cpl.creator;
            c.playlist.annotation = cpl.annotation;
            c.playlist.editRate = project_->settings.format.editRate;
            c.files = composed;
            pkl.compositions << c;
        }
        package.imps << pkl;
    }
    QString error;
    if (!interop::imf::writePackageDocuments(package, &error)) return fail(error, true);
    imfWindow_->draftWritten(draft, root);
    return root;
}

QStringList MainWindow::importImfComposition(const QString& binId, const QString& cplFile) {
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    if (!doc || doc->isReadOnly()) {
        reportFailure(this, tr("IMF: choose a bin that can be changed."));
        return {};
    }
    // The package the window lists (with the others' track files, for a Supplemental), else the CPL's own folder.
    std::optional<interop::imf::Package> package = imfWindow_ ? imfWindow_->packageOf(cplFile) : std::nullopt;
    QString error;
    if (!package) package = interop::imf::readPackage(QFileInfo(cplFile).absolutePath(), &error);
    const QString file = QFileInfo(cplFile).absoluteFilePath();
    const interop::imf::Composition* composition = nullptr;
    if (package) {
        for (const interop::imf::Composition& c : package->compositions) {
            if (c.file == file) composition = &c;
        }
    }
    std::optional<interop::imf::Composition> alone;
    if (!composition) {
        alone = interop::imf::readComposition(cplFile, &error);
        if (!alone) {
            reportFailure(this, tr("IMF: %1").arg(error));
            return {};
        }
        composition = &*alone;
    }
    // Each track file linked once, as a clip of its own.
    QStringList ids;
    QHash<QUuid, model::BinItem> clips;
    QStringList missing;
    for (const QUuid& id : composition->trackFiles()) {
        const QString path = package ? package->assets.value(id) : QString();
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            missing << interop::imf::urn(id);
            continue;
        }
        const QList<model::BinItem> linked = media::linkFiles({path}, linkOptions());
        if (linked.isEmpty()) {
            missing << QFileInfo(path).fileName();
            continue;
        }
        model::BinItem clip = linked.first();
        clip.fields.insert(QStringLiteral("IMF Track File"), interop::imf::urn(id));
        doc->addItem(clip);
        ids << clip.id;
        clips.insert(id, clip);
    }
    QStringList warnings;
    const model::BinItem sequence = interop::imf::compositionSequence(*composition, [&](const QUuid& id) -> const model::BinItem* {
        const auto it = clips.constFind(id);
        return it == clips.constEnd() ? nullptr : &*it;
    }, binId, QDateTime::currentDateTimeUtc(), &warnings);
    doc->addItem(sequence);
    ids << sequence.id;
    if (BinView* view = binContainer_->viewFor(doc)) view->selectIds({sequence.id});
    if (!missing.isEmpty()) {
        reportFailure(this, tr("IMF: %1: track files not found or not linked: %2").arg(QFileInfo(cplFile).fileName(), missing.join(QStringLiteral(", "))));
    }
    // Console only: what the import left out (virtual tracks other than picture and sound).
    for (const QString& w : warnings) ConsoleLog::instance().append(tr("IMF: %1: %2").arg(QFileInfo(cplFile).fileName(), w));
    return ids;
}

} // namespace lumenis::ui
