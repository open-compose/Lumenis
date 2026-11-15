// IMF track files (lumenis/media/imf_export.h; docs/spec/imf.md, "Export").
#include "lumenis/media/imf_export.h"

#include "lumenis/media/media.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>

namespace lumenis::media {

namespace {

// The pixel format FFmpeg's encoder writes for a profile: 4:2:2 10-bit Y'CbCr, or RGB at 12 or 10 bits.
QString pixelFormatOf(const QString& profile) {
    if (profile == QLatin1String("J2K IMF RGB 12 bit")) return QStringLiteral("gbrp12le");
    if (profile == QLatin1String("J2K IMF RGB 10 bit")) return QStringLiteral("gbrp10le");
    return QStringLiteral("yuv422p10le");
}

QString safeName(QString name) {
    for (QChar& c : name) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_')) c = QLatin1Char('_');
    }
    return name.isEmpty() ? QStringLiteral("untitled") : name;
}

} // namespace

QStringList imfEncodingProfiles() {
    // "J2K IMF YCrCb", "J2K IMF RGB 12 bit" and the 10-bit RGB one (p.1256-1257 prints it "J2L IMF RGB 10 bit" once and
    // "J2K IMF RGB 10 bit" elsewhere; we take the latter).
    return {QStringLiteral("J2K IMF YCrCb"), QStringLiteral("J2K IMF RGB 12 bit"), QStringLiteral("J2K IMF RGB 10 bit")};
}

bool imfFormatSupported(QSize raster, const Rational& rate, bool interlaced, const QString& profile, QString* why) {
    auto fail = [&](const QString& text) {
        if (why) *why = text;
        return false;
    };
    if (interlaced) return fail(QObject::tr("IMF takes progressive formats only."));
    static const QList<Rational> rates{Rational(24000, 1001), Rational(24), Rational(25), Rational(30000, 1001),
                                       Rational(30), Rational(50), Rational(60000, 1001), Rational(60)};
    const bool fourK = raster == QSize(4096, 2160);
    if (raster != QSize(1920, 1080) && raster != QSize(2048, 1080) && raster != QSize(3840, 2160) && !fourK) {
        return fail(QObject::tr("IMF takes HD, 2K, UHD and 4K rasters only (this project is %1 x %2).").arg(raster.width()).arg(raster.height()));
    }
    if (!rates.contains(rate) || (fourK && rate > Rational(30))) {
        return fail(QObject::tr("IMF does not take this project's frame rate in this raster."));
    }
    if (fourK && !profile.contains(QLatin1String("RGB"))) return fail(QObject::tr("IMF takes 4K in RGB only."));
    return true;
}

QString raw2bmxPath() {
    const QString env = qEnvironmentVariable("LUMENIS_RAW2BMX");
    if (!env.isEmpty()) return QFileInfo(env).isExecutable() ? env : QString();
    if (QCoreApplication::instance()) {
        const QString beside = QStandardPaths::findExecutable(QStringLiteral("raw2bmx"), {QCoreApplication::applicationDirPath()});
        if (!beside.isEmpty()) return beside;
    }
    return QStandardPaths::findExecutable(QStringLiteral("raw2bmx"));
}

int imfSoundChannels(const QString& format) {
    if (format == QLatin1String("mono")) return 1;
    if (format == QLatin1String("5.1")) return 6;
    if (format == QLatin1String("7.1")) return 8;
    return 2;
}

QString imfLanguageTag(const QString& language, const QString& region) {
    if (language.isEmpty()) return {};
    return region.isEmpty() ? language : QStringLiteral("%1-%2").arg(language, region);
}

QStringList imfLanguages() {
    return {QStringLiteral("en"), QStringLiteral("fr"), QStringLiteral("de"), QStringLiteral("it"), QStringLiteral("es"),
            QStringLiteral("pt"), QStringLiteral("nl"), QStringLiteral("sv"), QStringLiteral("da"), QStringLiteral("nb"),
            QStringLiteral("fi"), QStringLiteral("pl"), QStringLiteral("cs"), QStringLiteral("hu"), QStringLiteral("tr"),
            QStringLiteral("el"), QStringLiteral("ru"), QStringLiteral("ar"), QStringLiteral("he"), QStringLiteral("hi"),
            QStringLiteral("ja"), QStringLiteral("ko"), QStringLiteral("zh"), QStringLiteral("th"), QStringLiteral("ga")};
}

QStringList imfRegions(const QString& language) {
    // "Selecting an MCA Language will also populate the respective regions if available" (p.340): the regions where
    // the language is commonly given its own version (ours).
    static const QHash<QString, QStringList> regions{
        {QStringLiteral("en"), {QStringLiteral("US"), QStringLiteral("GB"), QStringLiteral("CA"), QStringLiteral("AU"), QStringLiteral("IE")}},
        {QStringLiteral("fr"), {QStringLiteral("FR"), QStringLiteral("CA"), QStringLiteral("BE"), QStringLiteral("CH")}},
        {QStringLiteral("de"), {QStringLiteral("DE"), QStringLiteral("AT"), QStringLiteral("CH")}},
        {QStringLiteral("it"), {QStringLiteral("IT"), QStringLiteral("CH")}},
        {QStringLiteral("es"), {QStringLiteral("ES"), QStringLiteral("MX"), QStringLiteral("419")}},
        {QStringLiteral("pt"), {QStringLiteral("PT"), QStringLiteral("BR")}},
        {QStringLiteral("nl"), {QStringLiteral("NL"), QStringLiteral("BE")}},
        {QStringLiteral("zh"), {QStringLiteral("CN"), QStringLiteral("TW"), QStringLiteral("HK")}},
        {QStringLiteral("ar"), {QStringLiteral("SA"), QStringLiteral("EG"), QStringLiteral("AE")}},
    };
    return regions.value(language);
}

QString imfMcaLabels(const ImfSoundOptions& options) {
    // One track (index 0), its channels in the mix's order (mixdown.h: 5.1 L C R Ls Rs LFE, 7.1 L C R Lss Rss Lrs Rrs
    // LFE), the soundfield group, and the group of groups with the language.
    QStringList channels;
    QString group;
    if (options.format == QLatin1String("mono")) {
        channels = {QStringLiteral("chM1")};
        group = QStringLiteral("sgM");
    } else if (options.format == QLatin1String("5.1")) {
        channels = {QStringLiteral("chL"), QStringLiteral("chC"), QStringLiteral("chR"), QStringLiteral("chLs"), QStringLiteral("chRs"),
                    QStringLiteral("chLFE")};
        group = QStringLiteral("sg51");
    } else if (options.format == QLatin1String("7.1")) {
        channels = {QStringLiteral("chL"), QStringLiteral("chC"), QStringLiteral("chR"), QStringLiteral("chLss"), QStringLiteral("chRss"),
                    QStringLiteral("chLrs"), QStringLiteral("chRrs"), QStringLiteral("chLFE")};
        group = QStringLiteral("sg71");
    } else {
        channels = {QStringLiteral("chL"), QStringLiteral("chR")};
        group = QStringLiteral("sgST");
    }
    QString out = QStringLiteral("0\n");
    for (int c = 0; c < channels.size(); ++c) out += QStringLiteral("%1, chan=%2\n").arg(channels.at(c)).arg(c);
    out += group + QLatin1Char('\n');
    const QString kind = options.contentKind == QLatin1String("dvs")      ? QStringLiteral("ggDVS")
                         : options.contentKind == QLatin1String("dialog") ? QStringLiteral("ggDcm")
                                                                          : QStringLiteral("ggMPg");
    const QString tag = imfLanguageTag(options.language, options.region);
    out += tag.isEmpty() ? kind + QLatin1Char('\n') : QStringLiteral("%1, lang=%2\n").arg(kind, tag);
    return out;
}

bool writeImfSoundFile(const QString& root, const MovieSource& source, const ImfSoundOptions& options, ImfTrackFile* file,
                       const std::function<bool(double)>& progress, QString* error) {
    auto fail = [&](const QString& text) {
        if (error) *error = text;
        return false;
    };
    if (source.frames <= 0 || !source.sound) return fail(QObject::tr("There is no sound to export."));
    const QString tool = options.wrapTool.isEmpty() ? raw2bmxPath() : options.wrapTool;
    if (tool.isEmpty()) {
        return fail(QObject::tr("IMF export of sound needs bmx's raw2bmx, which was not found (BUILDING.md, \"IMF export\")."));
    }
    // 24-bit 48 kHz WAVE of the mix, wrapped by raw2bmx as an MXF OP1a track file with all its channels in one track
    // ("singlemca") and its MCA labels. raw2bmx writes into a scratch folder and the one MXF file it makes is moved into
    // the package, so its naming of the output does not matter.
    QTemporaryDir scratch;
    if (!scratch.isValid()) return fail(QObject::tr("No temporary folder for the sound."));
    const QString wav = scratch.filePath(QStringLiteral("sound.wav"));
    AudioFileOptions a;
    a.channels = imfSoundChannels(options.format);
    a.sampleRate = 48000;
    a.bitDepth = 24;
    QString why;
    if (!exportAudioFile(wav, source, a, progress, &why)) return fail(QObject::tr("The sound could not be written: %1").arg(why));
    const QString labels = scratch.filePath(QStringLiteral("labels.txt"));
    {
        QFile f(labels);
        const QByteArray text = imfMcaLabels(options).toUtf8();
        if (!f.open(QIODevice::WriteOnly) || f.write(text) != text.size()) return fail(QObject::tr("No temporary folder for the sound."));
    }
    const QString outDir = scratch.filePath(QStringLiteral("out"));
    QDir().mkpath(outDir);
    const QString base = safeName(options.name);
    // "-t op1a" with "--audio-layout imf" (the IMF MCA label framework) and "--track-mca-labels": bmx documents its MCA
    // labels for the OP1a writer, not for "-t imf".
    const QStringList args{QStringLiteral("-t"), QStringLiteral("op1a"),
                           QStringLiteral("--audio-layout"), QStringLiteral("imf"),
                           QStringLiteral("-f"), QStringLiteral("%1/%2").arg(source.frameRate.num()).arg(source.frameRate.den()),
                           QStringLiteral("--track-map"), QStringLiteral("singlemca"),
                           QStringLiteral("--track-mca-labels"), QStringLiteral("imf"), labels,
                           QStringLiteral("--clip"), options.name.isEmpty() ? base : options.name,
                           QStringLiteral("-o"), QDir(outDir).filePath(QStringLiteral("sound.mxf")),
                           QStringLiteral("--wave"), wav};
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(tool, args);
    if (!process.waitForStarted()) return fail(QObject::tr("raw2bmx could not be started."));
    process.waitForFinished(-1);
    const QStringList made = QDir(outDir).entryList({QStringLiteral("*.mxf")}, QDir::Files);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || made.size() != 1) {
        return fail(QObject::tr("raw2bmx failed: %1").arg(QString::fromUtf8(process.readAll()).trimmed().right(600)));
    }
    ImfTrackFile audio;
    audio.id = QUuid::createUuid();
    audio.path = QDir(root).filePath(QStringLiteral("%1_A_%2.mxf").arg(base, audio.id.toString(QUuid::WithoutBraces)));
    audio.picture = false;
    audio.editRate = Rational(48000);
    // The samples of the frames: frames x 48000 / rate.
    audio.duration = static_cast<std::int64_t>(std::llround(static_cast<double>(source.frames) * 48000.0 / source.frameRate.toDouble()));
    if (!QFile::rename(QDir(outDir).filePath(made.first()), audio.path) &&
        !QFile::copy(QDir(outDir).filePath(made.first()), audio.path)) {
        return fail(QObject::tr("The sound's track file could not be moved into the package."));
    }
    if (file) *file = audio;
    return true;
}

bool writeImfTrackFiles(const QString& root, const MovieSource& source, const ImfTrackOptions& options,
                        QList<ImfTrackFile>* files, const std::function<bool(double)>& progress, QString* error) {
    QList<ImfTrackFile> written;
    auto fail = [&](const QString& text) {
        if (error) *error = text;
        for (const ImfTrackFile& f : written) QFile::remove(f.path);
        return false;
    };
    if (source.frames <= 0 || !source.picture) return fail(QObject::tr("There is nothing to export."));
    const QDir dir(root);
    const bool sound = static_cast<bool>(source.sound) && options.channels > 0;
    const QString tool = options.wrapTool.isEmpty() ? raw2bmxPath() : options.wrapTool;
    if (sound && tool.isEmpty()) {
        return fail(QObject::tr("IMF export of sound needs bmx's raw2bmx, which was not found (BUILDING.md, \"IMF export\")."));
    }
    const QString base = safeName(options.name);

    // The picture: JPEG 2000 in MXF OP1a, the frames at the project's raster.
    ImfTrackFile picture;
    picture.id = QUuid::createUuid();
    picture.path = dir.filePath(QStringLiteral("%1_V_%2.mxf").arg(base, picture.id.toString(QUuid::WithoutBraces)));
    picture.duration = source.frames;
    picture.editRate = source.frameRate;
    {
        WriterSettings w;
        w.format = QStringLiteral("mxf");
        w.videoCodec = QStringLiteral("jpeg2000");
        w.width = source.raster.width();
        w.height = source.raster.height();
        w.frameRate = source.frameRate;
        w.pixelFormat = pixelFormatOf(options.profile);
        w.audioCodec.clear();
        w.channels = 0;
        MediaWriter writer;
        QString why;
        if (!writer.open(picture.path, w, &why)) return fail(QObject::tr("The JPEG 2000 encoder could not start: %1").arg(why));
        written << picture;
        const double share = sound ? 0.85 : 1.0;
        for (std::int64_t f = 0; f < source.frames; ++f) {
            const QImage frame = fitMoviePicture(source.picture(f), source.raster, MovieScaling::Letterbox);
            if (!writer.writeVideo(frame)) return fail(QObject::tr("Frame %1 could not be encoded.").arg(f));
            if (progress && !progress(share * static_cast<double>(f + 1) / static_cast<double>(source.frames))) {
                return fail(QObject::tr("Cancelled."));
            }
        }
        if (!writer.finish()) return fail(QObject::tr("The picture's track file could not be finished."));
    }

    // The sound: the stereo mix as one track file (writeImfSoundFile).
    if (sound) {
        ImfSoundOptions so;
        so.format = options.channels == 1 ? QStringLiteral("mono") : QStringLiteral("stereo");
        so.name = options.name;
        so.wrapTool = tool;
        ImfTrackFile audio;
        QString why;
        if (!writeImfSoundFile(root, source, so, &audio, {}, &why)) return fail(why);
        written << audio;
    }
    if (progress) progress(1.0);
    if (files) *files = written;
    return true;
}

} // namespace lumenis::media
