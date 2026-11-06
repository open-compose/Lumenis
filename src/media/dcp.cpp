// Digital Cinema Packages (lumenis/media/dcp.h; docs/spec/dcp.md).
#include "lumenis/media/dcp.h"

#include "lumenis/media/media.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QXmlStreamWriter>

#include <cmath>

namespace lumenis::media {

namespace {

constexpr char kCplNs[] = "http://www.smpte-ra.org/schemas/429-7/2006/CPL";
constexpr char kPklNs[] = "http://www.smpte-ra.org/schemas/429-8/2007/PKL";
constexpr char kAmNs[] = "http://www.smpte-ra.org/schemas/429-9/2007/AM";

QString newUuid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString urn(const QString& uuid) {
    return QStringLiteral("urn:uuid:") + uuid;
}

QString issueDate(const DcpOptions& o) {
    const QDateTime when = o.issueDate.isValid() ? o.issueDate : QDateTime::currentDateTimeUtc();
    return when.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) + QStringLiteral("+00:00");
}

void element(QXmlStreamWriter& x, const char* name, const QString& text) {
    x.writeTextElement(QString::fromLatin1(name), text);
}

QByteArray sha1Of(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return h.result();
}

} // namespace

QSize dcpContainerSize(const QString& container, bool fourK) {
    const QSize two = container == QLatin1String("scope") ? QSize(2048, 858)
                      : container == QLatin1String("full") ? QSize(2048, 1080)
                                                           : QSize(1998, 1080);
    return fourK ? two * 2 : two;
}

int dcpFrameRate(const Rational& editRate) {
    const double fps = editRate.den() > 0 ? static_cast<double>(editRate.num()) / static_cast<double>(editRate.den()) : 24.0;
    int best = 24;
    for (int r : {24, 25, 30, 48, 50, 60}) {
        if (std::abs(fps - r) < std::abs(fps - best)) best = r;
    }
    return best;
}

QString asdcpWrapPath() {
    const QString env = qEnvironmentVariable("LUMENIS_ASDCP_WRAP");
    if (!env.isEmpty()) return QFileInfo(env).isExecutable() ? env : QString();
    if (QCoreApplication::instance()) {
        const QString beside = QStandardPaths::findExecutable(QStringLiteral("asdcp-wrap"), {QCoreApplication::applicationDirPath()});
        if (!beside.isEmpty()) return beside;
    }
    return QStandardPaths::findExecutable(QStringLiteral("asdcp-wrap"));
}

QByteArray dcpCplXml(const DcpComposition& c) {
    QByteArray out;
    QXmlStreamWriter x(&out);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeDefaultNamespace(QString::fromLatin1(kCplNs));
    x.writeStartElement(QStringLiteral("CompositionPlaylist"));
    element(x, "Id", urn(c.cplId));
    element(x, "AnnotationText", c.options.title);
    element(x, "IssueDate", issueDate(c.options));
    element(x, "Issuer", c.options.issuer);
    element(x, "Creator", QStringLiteral("Cutthulhu"));
    element(x, "ContentTitleText", c.options.title);
    element(x, "ContentKind", c.options.kind);
    x.writeStartElement(QStringLiteral("ContentVersion"));
    element(x, "Id", urn(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    element(x, "LabelText", c.options.title);
    x.writeEndElement();
    x.writeEmptyElement(QStringLiteral("RatingList"));
    x.writeStartElement(QStringLiteral("ReelList"));
    x.writeStartElement(QStringLiteral("Reel"));
    element(x, "Id", urn(c.reelId));
    x.writeStartElement(QStringLiteral("AssetList"));
    const QString rate = QStringLiteral("%1 1").arg(c.frameRate);
    for (const DcpAsset& a : c.assets) {
        if (a.type != QLatin1String("picture") && a.type != QLatin1String("sound")) continue;
        x.writeStartElement(a.type == QLatin1String("picture") ? QStringLiteral("MainPicture") : QStringLiteral("MainSound"));
        element(x, "Id", urn(a.id));
        element(x, "AnnotationText", a.file);
        element(x, "EditRate", rate);
        element(x, "IntrinsicDuration", QString::number(a.duration));
        element(x, "EntryPoint", QStringLiteral("0"));
        element(x, "Duration", QString::number(a.duration));
        if (a.type == QLatin1String("picture")) {
            element(x, "FrameRate", rate);
            element(x, "ScreenAspectRatio", QStringLiteral("%1 %2").arg(c.container.width()).arg(c.container.height()));
        }
        x.writeEndElement();
    }
    x.writeEndElement(); // AssetList
    x.writeEndElement(); // Reel
    x.writeEndElement(); // ReelList
    x.writeEndElement();
    x.writeEndDocument();
    return out;
}

QByteArray dcpPklXml(const DcpComposition& c) {
    QByteArray out;
    QXmlStreamWriter x(&out);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeDefaultNamespace(QString::fromLatin1(kPklNs));
    x.writeStartElement(QStringLiteral("PackingList"));
    element(x, "Id", urn(c.pklId));
    element(x, "AnnotationText", c.options.title);
    element(x, "IssueDate", issueDate(c.options));
    element(x, "Issuer", c.options.issuer);
    element(x, "Creator", QStringLiteral("Cutthulhu"));
    x.writeStartElement(QStringLiteral("AssetList"));
    for (const DcpAsset& a : c.assets) {
        x.writeStartElement(QStringLiteral("Asset"));
        element(x, "Id", urn(a.id));
        element(x, "AnnotationText", a.file);
        element(x, "Hash", QString::fromLatin1(a.sha1.toBase64()));
        element(x, "Size", QString::number(a.size));
        element(x, "Type", a.type == QLatin1String("cpl") ? QStringLiteral("text/xml") : QStringLiteral("application/mxf"));
        element(x, "OriginalFileName", a.file);
        x.writeEndElement();
    }
    x.writeEndElement();
    x.writeEndElement();
    x.writeEndDocument();
    return out;
}

QByteArray dcpAssetMapXml(const DcpComposition& c, const DcpAsset& pkl) {
    QByteArray out;
    QXmlStreamWriter x(&out);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeDefaultNamespace(QString::fromLatin1(kAmNs));
    x.writeStartElement(QStringLiteral("AssetMap"));
    element(x, "Id", urn(c.assetMapId));
    element(x, "AnnotationText", c.options.title);
    element(x, "Creator", QStringLiteral("Cutthulhu"));
    element(x, "VolumeCount", QStringLiteral("1"));
    element(x, "IssueDate", issueDate(c.options));
    element(x, "Issuer", c.options.issuer);
    x.writeStartElement(QStringLiteral("AssetList"));
    QList<DcpAsset> all{pkl};
    all += c.assets;
    for (const DcpAsset& a : all) {
        x.writeStartElement(QStringLiteral("Asset"));
        element(x, "Id", urn(a.id));
        if (a.type == QLatin1String("pkl")) element(x, "PackingList", QStringLiteral("true"));
        x.writeStartElement(QStringLiteral("ChunkList"));
        x.writeStartElement(QStringLiteral("Chunk"));
        element(x, "Path", a.file);
        element(x, "VolumeIndex", QStringLiteral("1"));
        element(x, "Offset", QStringLiteral("0"));
        element(x, "Length", QString::number(a.size));
        x.writeEndElement();
        x.writeEndElement();
        x.writeEndElement();
    }
    x.writeEndElement();
    x.writeEndElement();
    x.writeEndDocument();
    return out;
}

QByteArray dcpVolumeIndexXml() {
    QByteArray out;
    QXmlStreamWriter x(&out);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeDefaultNamespace(QString::fromLatin1(kAmNs));
    x.writeStartElement(QStringLiteral("VolumeIndex"));
    element(x, "Index", QStringLiteral("1"));
    x.writeEndElement();
    x.writeEndDocument();
    return out;
}

bool writeDcp(const QString& folder, const MovieSource& source, const DcpOptions& options,
              const std::function<bool(double)>& progress, QString* error) {
    QDir dir(folder);
    auto fail = [&](const QString& text) {
        if (error) *error = text;
        if (dir.exists()) dir.removeRecursively();
        return false;
    };
    if (source.frames <= 0 || !source.picture) return fail(QObject::tr("There is nothing to export."));
    const QString tool = options.wrapTool.isEmpty() ? asdcpWrapPath() : options.wrapTool;
    if (tool.isEmpty()) return fail(QObject::tr("DCP export needs asdcplib's asdcp-wrap, which was not found (BUILDING.md, \"DCP\")."));
    if (dir.exists()) {
        if (error) *error = QObject::tr("%1 already exists.").arg(QDir::toNativeSeparators(folder));
        return false;
    }
    if (!QDir().mkpath(folder)) return fail(QObject::tr("%1 could not be made.").arg(QDir::toNativeSeparators(folder)));
    QTemporaryDir scratch;
    if (!scratch.isValid()) return fail(QObject::tr("No temporary folder for the DCP's frames."));

    DcpComposition c;
    c.options = options;
    c.frameRate = dcpFrameRate(source.frameRate);
    c.container = dcpContainerSize(options.container, options.fourK);
    c.cplId = newUuid();
    c.pklId = newUuid();
    c.assetMapId = newUuid();
    c.reelId = newUuid();

    // The picture: each frame letterboxed into the container, as JPEG 2000 codestreams in X'Y'Z' (FFmpeg converts the
    // RGB into DCI's 12-bit X'Y'Z'), with the DCI cinema profile at 24 and 48 frames a second.
    const QString frames = scratch.filePath(QStringLiteral("j2c"));
    QDir().mkpath(frames);
    {
        WriterSettings w;
        w.format = QStringLiteral("image2");
        w.videoCodec = QStringLiteral("libopenjpeg");
        w.width = c.container.width();
        w.height = c.container.height();
        w.frameRate = Rational(c.frameRate);
        w.pixelFormat = QStringLiteral("xyz12le");
        w.audioCodec.clear();
        w.channels = 0;
        if (c.frameRate == 24 || c.frameRate == 48) {
            w.videoOptions << qMakePair(QStringLiteral("profile"), options.fourK ? QStringLiteral("cinema4k") : QStringLiteral("cinema2k"))
                           << qMakePair(QStringLiteral("cinema_mode"),
                                        options.fourK ? QStringLiteral("4k_24") : c.frameRate == 48 ? QStringLiteral("2k_48") : QStringLiteral("2k_24"));
        }
        MediaWriter writer;
        QString why;
        if (!writer.open(QDir(frames).filePath(QStringLiteral("%06d.j2c")), w, &why)) return fail(QObject::tr("The JPEG 2000 encoder could not start: %1").arg(why));
        for (std::int64_t f = 0; f < source.frames; ++f) {
            const QImage picture = fitMoviePicture(source.picture(f), c.container, MovieScaling::Letterbox);
            if (!writer.writeVideo(picture)) return fail(QObject::tr("Frame %1 could not be encoded.").arg(f));
            if (progress && !progress(0.8 * static_cast<double>(f + 1) / static_cast<double>(source.frames))) return fail(QObject::tr("Cancelled."));
        }
        if (!writer.finish()) return fail(QObject::tr("The JPEG 2000 frames could not be finished."));
    }

    // The sound: 24-bit 48 kHz WAVE in the DCP's channels, the mix's first channels into L and R (5.1: L, R, C, LFE,
    // Ls, Rs; ours: a stereo mix leaves the others silent).
    const QString wav = scratch.filePath(QStringLiteral("sound.wav"));
    const bool sound = static_cast<bool>(source.sound);
    if (sound) {
        MovieSource padded = source;
        const int want = options.channels;
        padded.sound = [&source, want](std::int64_t at, int count, std::vector<std::vector<float>>& out) {
            source.sound(at, count, out);
            out.resize(static_cast<std::size_t>(want), std::vector<float>(static_cast<std::size_t>(count), 0.0f));
        };
        AudioFileOptions a;
        a.channels = want;
        a.sampleRate = 48000;
        a.bitDepth = 24;
        QString why;
        if (!exportAudioFile(wav, padded, a, {}, &why)) return fail(QObject::tr("The sound could not be written: %1").arg(why));
    }

    // asdcp-wrap: "-L" SMPTE labels, "-a" the asset's UUID, "-p" the picture rate.
    auto wrap = [&](const QStringList& inputs, const QString& out, const QString& id, const QStringList& extra) {
        QStringList args{QStringLiteral("-L"), QStringLiteral("-a"), id, QStringLiteral("-p"), QString::number(c.frameRate)};
        args << extra << inputs << out;
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(tool, args);
        if (!process.waitForStarted()) return QObject::tr("asdcp-wrap could not be started.");
        process.waitForFinished(-1);
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !QFileInfo::exists(out)) {
            return QObject::tr("asdcp-wrap failed: %1").arg(QString::fromUtf8(process.readAll()).trimmed().right(600));
        }
        return QString();
    };
    DcpAsset picture{newUuid(), QStringLiteral("picture.mxf"), QStringLiteral("picture"), source.frames};
    if (const QString why = wrap({frames}, dir.filePath(picture.file), picture.id, {}); !why.isEmpty()) return fail(why);
    c.assets << picture;
    if (sound) {
        DcpAsset audio{newUuid(), QStringLiteral("sound.mxf"), QStringLiteral("sound"), source.frames};
        const QStringList label = options.channels == 6 ? QStringList{QStringLiteral("-l"), QStringLiteral("5.1")} : QStringList{};
        if (const QString why = wrap({wav}, dir.filePath(audio.file), audio.id, label); !why.isEmpty()) return fail(why);
        c.assets << audio;
    }
    if (progress && !progress(0.95)) return fail(QObject::tr("Cancelled."));

    auto writeFile = [&](const QString& name, const QByteArray& bytes) {
        QFile f(dir.filePath(name));
        return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    };
    auto measure = [&](DcpAsset& a) {
        a.size = QFileInfo(dir.filePath(a.file)).size();
        a.sha1 = sha1Of(dir.filePath(a.file));
    };
    for (DcpAsset& a : c.assets) measure(a);
    DcpAsset cpl{c.cplId, QStringLiteral("cpl_%1.xml").arg(c.cplId), QStringLiteral("cpl")};
    if (!writeFile(cpl.file, dcpCplXml(c))) return fail(QObject::tr("The Composition Playlist could not be written."));
    measure(cpl);
    c.assets << cpl;
    DcpAsset pkl{c.pklId, QStringLiteral("pkl_%1.xml").arg(c.pklId), QStringLiteral("pkl")};
    if (!writeFile(pkl.file, dcpPklXml(c))) return fail(QObject::tr("The Packing List could not be written."));
    measure(pkl);
    if (!writeFile(QStringLiteral("ASSETMAP.xml"), dcpAssetMapXml(c, pkl)) || !writeFile(QStringLiteral("VOLINDEX.xml"), dcpVolumeIndexXml())) {
        return fail(QObject::tr("The Asset Map could not be written."));
    }
    if (progress) progress(1.0);
    return true;
}

} // namespace lumenis::media
