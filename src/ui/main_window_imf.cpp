// IMF in the application (11.5c; docs/spec/imf.md; Editing Guide p.333-340): Tools > IMF Window, the "IMF" panel, and
// a CPL dragged from it into a bin, which links the package's track files as clips and makes the composition a
// sequence of them.
#include "lumenis/ui/main_window.h"

#include "lumenis/interop/imf.h"
#include "lumenis/interop/imf_writer.h"
#include "lumenis/media/imf_export.h"
#include "lumenis/media/link.h"
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
    if (!packageRoot.isEmpty()) {
        // "To add to an existing package ... drag the sequence to the root folder" (p.336) makes a Supplemental (11.5c3).
        reportFailure(this, tr("IMF: a sequence added to a package on disk makes a Supplemental, which this version cannot write yet."));
        return 0;
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
            QJsonArray tracks;
            const model::Sequence seq = model::Sequence::fromItem(*item);
            const model::LoadedClip* shown = composer_->state(MonitorId::Record).current();
            if (shown && shown->item.id == item->id) {
                for (const QString& t : timeline_->patch().selectedRecordTracks(seq)) tracks << t;
            } else {
                for (const model::Track& t : seq.tracks()) tracks << t.name();
            }
            QList<media::ImfTrackFile> files;
            media::ImfTrackOptions options;
            options.profile = cpl.profile;
            options.name = cpl.contentTitle;
            movieSink_ = [&](const media::MovieSource& source, const media::MovieExportOptions& o,
                             const std::function<bool(double)>& progress, QString* sinkError) {
                media::MovieSource used = source;
                if (!o.audio) used.sound = {};
                return media::writeImfTrackFiles(root, used, options, &files, progress, sinkError);
            };
            exportOverride_ = {{QStringLiteral("useMarks"), false},
                               {QStringLiteral("useSelectedTracks"), false},
                               {QStringLiteral("movieTracks"), QStringLiteral("both")},
                               {QStringLiteral("movieAudioFormat"), QStringLiteral("stereo")},
                               {QStringLiteral("tracksOnly"), tracks}};
            QString error;
            const bool ok = exportMovieFile(*item, cpl.binId, QString(), QStringLiteral("sink"), &error);
            exportOverride_ = {};
            movieSink_ = {};
            if (!ok) return fail(tr("%1: %2").arg(cpl.contentTitle, error), true);
            interop::imf::NewComposition c;
            c.playlist.contentTitle = cpl.contentTitle;
            c.playlist.contentKind = cpl.contentKind;
            c.playlist.issuer = cpl.issuer;
            c.playlist.creator = cpl.creator;
            c.playlist.annotation = cpl.annotation;
            c.playlist.editRate = project_->settings.format.editRate;
            for (const media::ImfTrackFile& f : files) c.files << interop::imf::TrackFile{f.id, f.path, f.picture, f.duration, f.editRate};
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
