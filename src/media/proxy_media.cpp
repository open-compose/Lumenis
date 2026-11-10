// Making proxy media (proxy_media.h; Editing Guide p.508-510, p.513).
#include "lumenis/media/proxy_media.h"

#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/media/movie_export.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <map>
#include <memory>

namespace lumenis::media {

QString proxyFolder(const QString& drive) {
    return QDir(drive).filePath(QStringLiteral("Avid MediaFiles/Proxy/1"));
}

QSize proxyRaster(QSize projectRaster) {
    // Avid DNx LB (DNxHR) takes nothing smaller than 256 by 120; ours: at least 256 by 144.
    auto half = [](int v, int least) { return std::max(least, (v / 2) & ~1); };
    if (!projectRaster.isValid()) return {960, 540};
    return {half(projectRaster.width(), 256), half(projectRaster.height(), 144)};
}

namespace {

// A file name from the clip's name: letters, digits, "_" and "-" kept, the rest "_".
QString fileStem(const model::BinItem& clip) {
    QString stem = clip.name;
    stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    if (stem.isEmpty()) stem = QStringLiteral("clip");
    return stem + QLatin1Char('_') + clip.id.left(8);
}

// The clip's first two sound channels at 48 kHz, read as the movie export asks for them; nothing when the clip has
// no sound at that rate.
struct ProxySound {
    struct Channel {
        std::shared_ptr<AudioReader> reader;
        int channel = 0;
        int channels = 1;
    };
    std::vector<Channel> channels; // left, right

    static std::optional<ProxySound> of(const Link& link) {
        if (link.channels <= 0) return std::nullopt;
        ProxySound s;
        std::map<std::pair<QString, int>, std::shared_ptr<AudioReader>> open;
        for (int out = 0; out < 2; ++out) {
            const int track = std::min(out, link.channels - 1);
            const auto where = link.channelOf(track);
            const QString file = link.audioFile(track);
            if (!where || file.isEmpty()) return std::nullopt;
            auto& reader = open[{file, where->first}];
            if (!reader) {
                reader = std::make_shared<AudioReader>();
                if (!reader->open(file, where->first) || reader->sampleRate() != 48000) return std::nullopt;
            }
            s.channels.push_back({reader, where->second, std::max(1, reader->channels())});
        }
        return s;
    }

    void read(std::int64_t at, int count, std::vector<std::vector<float>>& out) {
        out.assign(2, std::vector<float>(static_cast<std::size_t>(count), 0.0f));
        for (std::size_t c = 0; c < channels.size() && c < 2; ++c) {
            const Channel& ch = channels[c];
            const std::vector<float> samples = ch.reader->samples(at, count);
            const auto frames = samples.size() / static_cast<std::size_t>(ch.channels);
            for (std::size_t i = 0; i < frames && i < out[c].size(); ++i) {
                out[c][i] = samples[i * static_cast<std::size_t>(ch.channels) + static_cast<std::size_t>(ch.channel)];
            }
        }
    }
};

} // namespace

std::optional<model::ProxyMedia> makeProxyMedia(const model::BinItem& clip, const QString& drive, QSize projectRaster,
                                                const std::function<bool(double)>& progress, QString* error) {
    auto fail = [error](const QString& why) -> std::optional<model::ProxyMedia> {
        if (error) *error = why;
        return std::nullopt;
    };
    const auto link = linkOf(clip);
    if (!link || link->videoStream < 0 || clip.durationFrames <= 0) {
        return fail(QObject::tr("%1 has no picture to make proxies of.").arg(clip.name));
    }
    if (!patentCodecsAllowed()) return fail(QObject::tr("H.264 and Avid DNx are not available in this build."));
    const QString folder = proxyFolder(drive);
    if (!QDir().mkpath(folder)) return fail(QObject::tr("The proxy folder %1 cannot be made.").arg(folder));

    const QSize raster = proxyRaster(projectRaster);
    auto frames = std::make_shared<FrameCache>(2, 4);
    auto sound = std::make_shared<std::optional<ProxySound>>(ProxySound::of(*link));
    MovieSource source;
    source.frames = clip.durationFrames;
    source.frameRate = clip.editRate;
    source.raster = raster;
    source.picture = [frames, clip, raster](std::int64_t f) { return frames->frame(clip, f, raster); };
    if (*sound) {
        source.sound = [sound](std::int64_t at, int count, std::vector<std::vector<float>>& out) { (*sound)->read(at, count, out); };
    }

    model::ProxyMedia proxy;
    proxy.folder = folder;
    proxy.audio = sound->has_value();
    proxy.raster = raster;
    // H.264 in an MP4, then DNx LB in a MOV, each with AAC; each half of the progress.
    struct Output {
        QString suffix;
        MovieContainer container;
        QString compression;
        QJsonObject* link;
    };
    const Output outputs[] = {{QStringLiteral(".proxy.mp4"), MovieContainer::Mp4, QStringLiteral("h264"), &proxy.h264Link},
                              {QStringLiteral(".proxy_lb.mov"), MovieContainer::Mov, QStringLiteral("dnxhr_lb"), &proxy.lbLink}};
    for (int i = 0; i < 2; ++i) {
        const Output& o = outputs[i];
        const QString path = QDir(folder).filePath(fileStem(clip) + o.suffix);
        MovieExportOptions options;
        options.container = o.container;
        options.compression = o.compression;
        options.size = raster;
        options.audio = proxy.audio;
        options.aac = true;
        options.audioChannels = 2;
        QString why;
        const bool ok = exportMovie(path, source, options, [&progress, i](double done) {
            return !progress || progress((i + done) / 2.0);
        }, &why);
        if (!ok) return fail(QObject::tr("The proxy %1 could not be written: %2").arg(QFileInfo(path).fileName(), why));
        const auto info = probe(path, &why);
        if (!info) return fail(why);
        LinkOptions linkOptions;
        const auto linked = linkClip(*info, linkOptions, &why);
        if (!linked) return fail(why);
        *o.link = linked->link;
    }
    return proxy;
}

} // namespace lumenis::media
