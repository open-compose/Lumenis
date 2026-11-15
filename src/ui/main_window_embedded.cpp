// Media embedded in AAF files (docs/spec/aaf.md, "Embedded media"; Editing Guide p.871, p.1176-1177, p.1197). Import:
// "If you transferred an OMFI or an AAF file with embedded media, you can create a new project and bin and import the
// OMFI or the AAF file. The sequence automatically links to the media" (p.991): each clip's embedded media is read out
// of the file (interop/aaf_essence.h) and made managed media with the clip's own MobID, at the resolution the Import
// setting's OMFI/AAF tab says (p.1197). Export: Media Destinations' Embedded in AAF (p.871): the media an export method
// copies, consolidates or mixes down is written into the AAF file instead of a folder. importAaf() and writeExport()
// (main_window.cpp) call these.
#include "lumenis/ui/main_window.h"

#include "lumenis/interop/aaf_essence.h"
#include "lumenis/interop/aaf_import.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/tool_windows.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <map>

namespace lumenis::ui {

namespace {

// The track's number in its kind (A2: 2), 0 when the name is not a track's.
int trackNumber(const QString& track) {
    const auto ref = model::parseTrackName(track);
    return ref ? ref->number : 0;
}

bool isVideoTrack(const QString& track) {
    const auto ref = model::parseTrackName(track);
    return ref && ref->kind == model::TrackKind::Video;
}

} // namespace

// The Resolution choices (p.1197): "Use the source file's resolution" keeps DNx frames as they are (Consolidate);
// "Use the current import resolution" transcodes to the Media Creation setting's Import resolution; "Ask me ... for
// each file that is different" asks per clip when the two differ. Sound is written as PCM either way, which loses
// nothing. A picture that cannot be copied (not DNx) is transcoded.
int MainWindow::readEmbeddedMedia(const interop::aaf::File& file, interop::aaf::ImportResult& imported, QStringList& warnings) {
    if (imported.essence.isEmpty()) return 0;
    std::map<MobId, const interop::aaf::Essence*> byMob;
    for (const interop::aaf::Essence& e : imported.essence) byMob.emplace(e.mob, &e);
    const QString choice = settings_->text(QStringLiteral("Import"), QStringLiteral("omfResolution"));
    QTemporaryDir work;
    if (!work.isValid()) {
        warnings << tr("The embedded media cannot be read out: no temporary folder.");
        return 0;
    }
    int made = 0;
    int n = 0;
    for (model::BinItem& item : imported.items) {
        if (item.kind != model::BinItemKind::MasterClip && item.kind != model::BinItemKind::AudioClip) continue;
        // Media already on the drives (the same MobID, an AAF imported before) is used as it is.
        if (linkByMobId(item)) continue;
        // The clip's File sources whose media the file holds, picture first, then sound by track number.
        QList<const model::ClipSource*> sources;
        for (const model::ClipSource& s : item.sources) {
            if (s.kind == model::ClipSource::Kind::File && byMob.count(s.mob)) sources << &s;
        }
        if (sources.isEmpty()) continue;
        std::sort(sources.begin(), sources.end(), [](const model::ClipSource* a, const model::ClipSource* b) {
            if (isVideoTrack(a->track) != isVideoTrack(b->track)) return isVideoTrack(a->track);
            return trackNumber(a->track) < trackNumber(b->track);
        });
        // Each file read out: DNx frames into a QuickTime file at the media's rate (a raw stream has none), sound
        // as WAVE files.
        media::Link link;
        link.rate = item.editRate;
        link.frames = item.durationFrames;
        QString why;
        bool ok = true;
        for (const model::ClipSource* s : sources) {
            const interop::aaf::Essence& e = *byMob.at(s->mob);
            if (e.kind == interop::aaf::Essence::Kind::Unsupported) {
                ok = false;
                why = tr("%1 media is not read yet").arg(e.descriptor);
                break;
            }
            const QString base = QDir(work.path()).filePath(QStringLiteral("e%1").arg(++n));
            const QString raw = base + QLatin1Char('.') + interop::aaf::essenceSuffix(e);
            if (!interop::aaf::extractEssence(file, e, raw, &why)) {
                ok = false;
                break;
            }
            if (e.kind == interop::aaf::Essence::Kind::Dnx) {
                if (link.videoStream >= 0) continue; // one picture a clip
                const QString mov = base + QStringLiteral(".mov");
                media::WriterSettings ws;
                ws.format = QStringLiteral("mov");
                ws.sourceRate = e.rate;
                const std::int64_t frames = e.length > 0 ? e.length : item.durationFrames;
                if (!media::copyVideoFrames(raw, -1, 0, frames, mov, ws, &why)) {
                    ok = false;
                    break;
                }
                QFile::remove(raw);
                link.path = mov;
                link.rate = e.rate;
                link.frames = frames;
                link.videoStream = 0;
                link.topFieldFirst = true;
            } else {
                const auto info = media::probe(raw, &why);
                int channels = 0;
                if (info) {
                    for (const media::StreamInfo& st : info->streams) {
                        if (st.channels > 0 && channels == 0) channels = st.channels;
                    }
                }
                if (channels <= 0) {
                    ok = false;
                    if (why.isEmpty()) why = tr("its sound cannot be read");
                    break;
                }
                link.audioPaths << raw;
                link.audioStreams << 0;
                link.streamChannels << channels;
                link.channels += channels;
                if (link.path.isEmpty()) link.path = raw;
            }
        }
        if (!ok) {
            warnings << tr("%1: the embedded media was not read: %2").arg(item.name, why);
            continue;
        }
        model::BinItem read = item;
        read.link = link.toJson();
        media::MediaCreation creation = mediaCreation(QStringLiteral("import"));
        creation.master = item.mobId;
        if (creation.videoDrive.isEmpty()) {
            warnings << tr("%1: the embedded media was not read: no media drive is chosen.").arg(item.name);
            continue;
        }
        // The picture's resolution, which the media keeps when it is copied (the Video column, as Consolidate keeps it).
        QString source;
        if (link.videoStream >= 0) {
            if (const auto info = media::probe(link.path)) {
                for (const media::StreamInfo& st : info->streams) {
                    if (st.index != link.videoStream) continue;
                    if (const media::Resolution* r = media::resolutionOfProbed(st.codec, st.profile)) source = r->name;
                }
            }
            if (!source.isEmpty()) read.fields.insert(QStringLiteral("Video"), source);
        }
        // Which resolution: the source's when it can be kept as it is.
        bool keep = media::canConsolidate(read);
        if (keep && link.videoStream >= 0 && choice != QLatin1String("source")) {
            const QString current = creation.resolution.name;
            if (source != current) {
                if (choice == QLatin1String("current")) {
                    keep = false;
                } else if (embeddedResolutionHook_) {
                    keep = embeddedResolutionHook_(item, source, current);
                } else {
                    QMessageBox box(QMessageBox::Question, tr("Import"),
                                    tr("The media of %1 is %2, not the current import resolution, %3. Which resolution should "
                                       "the imported media have?")
                                        .arg(item.name, source.isEmpty() ? tr("another resolution") : source, current),
                                    QMessageBox::NoButton, this);
                    QPushButton* useSource = box.addButton(tr("Source Resolution"), QMessageBox::AcceptRole);
                    box.addButton(tr("Current Import Resolution"), QMessageBox::RejectRole);
                    box.setDefaultButton(useSource);
                    box.exec();
                    keep = box.clickedButton() == useSource;
                }
            }
        }
        const auto result = keep ? media::consolidateClip(read, 0, -1, creation, &why) : media::transcodeClip(read, 0, -1, creation, &why);
        if (!result) {
            warnings << tr("%1: the embedded media was not read: %2").arg(item.name, why);
            continue;
        }
        // The clip keeps what the AAF said of it (name, MobID, tape, columns); it takes the new files and what
        // describes them. The new File sources come from the AAF's tape or import source as the old ones did.
        QList<model::ClipSource> files;
        QList<model::ClipSource> before;
        for (model::ClipSource s : result->sources) {
            if (s.kind != model::ClipSource::Kind::File) continue;
            for (const model::ClipSource& old : item.sources) {
                if (old.kind == model::ClipSource::Kind::File && old.track == s.track) s.from = old.from;
            }
            files << s;
        }
        for (const model::ClipSource& s : item.sources) {
            if (s.kind != model::ClipSource::Kind::File) before << s;
        }
        item.sources = files + before;
        item.link = result->link;
        for (const char* heading : {"Video", "Format", "Color Space", "Pixel Aspect Ratio", "Drive", "Audio SR", "Audio Bit Depth",
                                    "Audio Format", "Video File Format"}) {
            const QString key = QString::fromLatin1(heading);
            if (result->fields.contains(key)) item.fields.insert(key, result->fields.value(key));
        }
        ++made;
    }
    return made;
}

// Embedded in AAF (p.871): the clip's file for this track as the AAF's EssenceData takes it. Pictures in Avid DNx have
// their frames copied out as they are; other pictures stay linked (ours: the guide's Transcode Video to option, which
// would make DNx of them, is not there yet). Sound is decoded and written as 24-bit PCM, the channel the track is.
std::optional<interop::aaf::Essence> MainWindow::essenceToEmbed(const model::BinItem& clip, const model::ClipSource& source,
                                                               const QString& folder, QStringList& warnings) {
    const auto link = media::linkOf(clip);
    if (!link || link->unlinked || source.path.isEmpty()) return std::nullopt;
    static int counter = 0;
    const QString base = QDir(folder).filePath(QStringLiteral("embed%1").arg(++counter));
    interop::aaf::Essence e;
    e.mob = source.mob;
    QString why;
    if (isVideoTrack(source.track)) {
        const auto info = media::probe(source.path, &why);
        int picture = -1;
        QString codec;
        if (info) {
            for (const media::StreamInfo& st : info->streams) {
                if (st.width > 0 && picture < 0) {
                    picture = st.index;
                    codec = st.codec;
                }
            }
        }
        if (picture < 0 || codec != QLatin1String("dnxhd")) {
            warnings << tr("%1: only Avid DNx pictures are embedded; the picture stays linked").arg(clip.name);
            return std::nullopt;
        }
        e.file = base + QStringLiteral(".dnxhd");
        media::WriterSettings ws;
        ws.format = QStringLiteral("dnxhd");
        const std::int64_t frames = link->frames > 0 ? link->frames : clip.durationFrames;
        if (!media::copyVideoFrames(source.path, picture, 0, frames, e.file, ws, &why)) {
            warnings << tr("%1: the picture could not be embedded: %2").arg(clip.name, why);
            return std::nullopt;
        }
        QFile f(e.file);
        if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
        const auto header = interop::aaf::readDnxHeader(f.read(640));
        if (!header) return std::nullopt;
        e.kind = interop::aaf::Essence::Kind::Dnx;
        e.dnx = *header;
        e.rate = link->rate;
        e.length = frames;
        e.bytes = static_cast<std::uint64_t>(f.size());
        return e;
    }
    // Sound: the track's stream and channel, as playback finds them.
    const int track = trackNumber(source.track) - 1;
    const auto channel = track >= 0 ? link->channelOf(track) : std::nullopt;
    if (!channel) return std::nullopt;
    media::AudioReader reader;
    if (!reader.open(link->audioFile(track), channel->first, &why)) {
        warnings << tr("%1: the sound could not be embedded: %2").arg(clip.name, why);
        return std::nullopt;
    }
    e.file = base + QStringLiteral(".pcm");
    QFile out(e.file);
    if (!out.open(QIODevice::WriteOnly)) return std::nullopt;
    const int channels = std::max(1, reader.channels());
    const int which = std::clamp(channel->second, 0, channels - 1);
    constexpr int kBlock = 48000;
    std::int64_t written = 0;
    QByteArray bytes;
    for (;;) {
        const std::vector<float> s = reader.samples(written, kBlock);
        const auto count = static_cast<std::int64_t>(s.size()) / channels;
        if (count <= 0) break;
        bytes.resize(static_cast<qsizetype>(count) * 3);
        for (std::int64_t i = 0; i < count; ++i) {
            const double v = std::clamp(static_cast<double>(s[static_cast<std::size_t>(i * channels + which)]), -1.0, 1.0);
            const auto q = static_cast<qint32>(std::lround(v * 8388607.0));
            char* d = bytes.data() + i * 3;
            d[0] = static_cast<char>(q & 0xFF);
            d[1] = static_cast<char>((q >> 8) & 0xFF);
            d[2] = static_cast<char>((q >> 16) & 0xFF);
        }
        if (out.write(bytes) != bytes.size()) return std::nullopt;
        written += count;
        if (count < kBlock) break;
    }
    e.kind = interop::aaf::Essence::Kind::Pcm;
    e.channels = 1;
    e.sampleRate = reader.sampleRate();
    e.bits = 24;
    e.rate = Rational(e.sampleRate);
    e.length = written;
    e.bytes = static_cast<std::uint64_t>(out.size());
    return e;
}

// Embedded files are read out once each; a file a clip's sources share (several tracks in one file) is embedded for
// each file mob that names it.
void MainWindow::embedMedia(QList<model::BinItem>& items, const QString& folder, const QString& fallback,
                            std::map<MobId, interop::aaf::Essence>& essences, QStringList& warnings) {
    const QString root = QDir(folder).absolutePath() + QLatin1Char('/');
    const QString essenceFolder = QDir(folder).filePath(QStringLiteral("essence"));
    QDir().mkpath(essenceFolder);
    QHash<QString, QString> moved; // a file that stays linked: where it went
    for (model::BinItem& item : items) {
        for (model::ClipSource& s : item.sources) {
            if (s.kind != model::ClipSource::Kind::File || !QFileInfo(s.path).absoluteFilePath().startsWith(root)) continue;
            if (!essences.count(s.mob)) {
                if (auto e = essenceToEmbed(item, s, essenceFolder, warnings)) essences.emplace(s.mob, *e);
            }
            if (essences.count(s.mob)) continue;
            // Not embedded: the file goes beside the AAF, which links to it there.
            const QString from = QFileInfo(s.path).absoluteFilePath();
            if (!moved.contains(from)) {
                QString to = QDir(fallback).filePath(QFileInfo(from).fileName());
                for (int k = 2; QFileInfo::exists(to); ++k) {
                    to = QDir(fallback).filePath(QStringLiteral("%1 %2.%3").arg(QFileInfo(from).completeBaseName()).arg(k).arg(QFileInfo(from).suffix()));
                }
                if (!QFile::rename(from, to) && !QFile::copy(from, to)) to.clear();
                moved.insert(from, to);
                if (!to.isEmpty()) {
                    warnings << tr("%1: linked to %2 instead of embedded").arg(item.name, QDir::toNativeSeparators(to));
                } else {
                    warnings << tr("%1: its media could not be embedded or kept").arg(item.name);
                }
            }
            if (!moved.value(from).isEmpty()) s.path = moved.value(from);
        }
    }
}

} // namespace lumenis::ui
