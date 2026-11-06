// AS-11 and AS-02 through bmxtranswrap (lumenis/media/as11.h).
#include "lumenis/media/as11.h"

#include "lumenis/core/timecode.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace lumenis::media {

const QList<As11Shim>& as11Shims() {
    // AMWA's published shims, with bmxtranswrap's descriptions of them.
    static const QList<As11Shim> all{
        {QStringLiteral("as11-x1"), QStringLiteral("AMWA AS-11 X1"), QStringLiteral("finished UHD programmes to UK DPP broadcasters"), true},
        {QStringLiteral("as11-x2"), QStringLiteral("AMWA AS-11 X2"), QStringLiteral("finished HD AVC Intra programmes")},
        {QStringLiteral("as11-x3"), QStringLiteral("AMWA AS-11 X3"), QStringLiteral("finished HD AVC Long GOP programmes")},
        {QStringLiteral("as11-x4"), QStringLiteral("AMWA AS-11 X4"), QStringLiteral("finished HD AVC Long GOP programmes")},
        {QStringLiteral("as11-x5"), QStringLiteral("AMWA AS-11 X5"), QStringLiteral("finished UHD commercials and promotions to UK DPP broadcasters"), true},
        {QStringLiteral("as11-x6"), QStringLiteral("AMWA AS-11 X6"), QStringLiteral("finished HD commercials and promotions to UK DPP broadcasters"), true},
        {QStringLiteral("as11-x7"), QStringLiteral("AMWA AS-11 X7"), QStringLiteral("finished SD D10 programmes")},
        {QStringLiteral("as11-x8"), QStringLiteral("AMWA AS-11 X8"), QStringLiteral("finished HD MPEG-2 programmes to NABA broadcasters")},
        {QStringLiteral("as11-x9"), QStringLiteral("AMWA AS-11 X9"), QStringLiteral("finished HD AVC programmes to NABA broadcasters")},
    };
    return all;
}

const As11Shim* findAs11Shim(const QString& id) {
    for (const As11Shim& s : as11Shims()) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

QString as11FrameworkText(const QList<QPair<QString, QString>>& properties) {
    QString out;
    for (const auto& [name, value] : properties) {
        const QString v = value.trimmed();
        if (v.isEmpty()) continue;
        // One property per line: a line break in a value would start another (ours: made spaces).
        out += name + QStringLiteral(": ") + QString(v).replace(QLatin1Char('\n'), QLatin1Char(' ')) + QLatin1Char('\n');
    }
    return out;
}

QString as11SegmentationText(const QList<As11Segment>& segments, std::int64_t startFrame, const Rational& rate, bool dropFrame) {
    const TimecodeFormat format = TimecodeFormat::forRate(rate, dropFrame);
    QString out;
    for (int i = 0; i < segments.size(); ++i) {
        const As11Segment& s = segments.at(i);
        out += QStringLiteral("%1/%2\t%3 %4\n")
                   .arg(i + 1)
                   .arg(segments.size())
                   .arg(QString::fromStdString(Timecode(startFrame + s.start, format).toString()),
                        QString::fromStdString(Timecode(s.duration, TimecodeFormat::forRate(rate, false)).toString()));
    }
    return out;
}

QString bmxTranswrapPath() {
    const QString env = qEnvironmentVariable("LUMENIS_BMXTRANSWRAP");
    if (!env.isEmpty()) return QFileInfo(env).isExecutable() ? env : QString();
    if (QCoreApplication::instance()) {
        const QString beside = QStandardPaths::findExecutable(QStringLiteral("bmxtranswrap"), {QCoreApplication::applicationDirPath()});
        if (!beside.isEmpty()) return beside;
    }
    return QStandardPaths::findExecutable(QStringLiteral("bmxtranswrap"));
}

bool writeAs11(const QString& op1a, const QString& path, const As11Options& o, QString* error) {
    auto fail = [&](const QString& text) {
        if (error) *error = text;
        QFile::remove(path);
        return false;
    };
    const QString tool = bmxTranswrapPath();
    if (tool.isEmpty()) return fail(QObject::tr("AS-11 export needs bmx's bmxtranswrap, which was not found (BUILDING.md, \"AS-11\")."));
    const As11Shim* shim = findAs11Shim(o.shim);
    if (!shim) return fail(QObject::tr("Unknown AS-11 shim %1.").arg(o.shim));
    // "This specification requires program segment markers" (p.891).
    if (o.segments.isEmpty()) return fail(QObject::tr("AS-11 needs programme segments: add spanned markers for them (p.414)."));
    QTemporaryDir dir;
    if (!dir.isValid()) return fail(QObject::tr("No temporary folder for the AS-11 metadata."));
    auto writeText = [&](const QString& name, const QString& text) {
        QFile f(dir.filePath(name));
        return f.open(QIODevice::WriteOnly | QIODevice::Text) && f.write(text.toUtf8()) >= 0;
    };
    QList<QPair<QString, QString>> core = o.core;
    core.prepend({QStringLiteral("ShimName"), shim->name});
    if (!writeText(QStringLiteral("core.txt"), as11FrameworkText(core)) ||
        !writeText(QStringLiteral("seg.txt"), as11SegmentationText(o.segments, o.startFrame, o.rate, o.dropFrame)))
        return fail(QObject::tr("The AS-11 metadata could not be written."));
    QStringList args{QStringLiteral("-t"), QStringLiteral("as11op1a"), QStringLiteral("--mtc"), QStringLiteral("--spec-id"), shim->id,
                     QStringLiteral("--dm-file"), QStringLiteral("as11"), dir.filePath(QStringLiteral("core.txt")),
                     QStringLiteral("--seg"), dir.filePath(QStringLiteral("seg.txt"))};
    if (shim->dpp && !o.dpp.isEmpty()) {
        if (!writeText(QStringLiteral("dpp.txt"), as11FrameworkText(o.dpp))) return fail(QObject::tr("The UK DPP metadata could not be written."));
        args << QStringLiteral("--dm-file") << QStringLiteral("dpp") << dir.filePath(QStringLiteral("dpp.txt"));
    }
    args << QStringLiteral("-o") << path << op1a;
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(tool, args);
    if (!process.waitForStarted()) return fail(QObject::tr("bmxtranswrap could not be started."));
    process.waitForFinished(-1);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !QFileInfo::exists(path)) {
        const QString output = QString::fromUtf8(process.readAll()).trimmed();
        return fail(QObject::tr("bmxtranswrap failed: %1").arg(output.right(600)));
    }
    return true;
}

bool writeAs02(const QString& op1a, const QString& bundle, const As02Options& o, QString* error) {
    auto fail = [&](const QString& text) {
        if (error) *error = text;
        return false;
    };
    const QString tool = bmxTranswrapPath();
    if (tool.isEmpty()) return fail(QObject::tr("AS-02 needs bmx's bmxtranswrap, which was not found (BUILDING.md, \"AS-11 and AS-02\")."));
    if (QFileInfo::exists(bundle)) return fail(QObject::tr("The bundle %1 is there already.").arg(QDir::toNativeSeparators(bundle)));
    QDir().mkpath(QFileInfo(bundle).absolutePath());
    QStringList args{QStringLiteral("-t"), QStringLiteral("as02"), QStringLiteral("--mtc")};
    if (!o.shimName.isEmpty()) args << QStringLiteral("--shim-name") << o.shimName;
    if (!o.shimId.isEmpty()) args << QStringLiteral("--shim-id") << o.shimId;
    if (!o.shimAnnotation.isEmpty()) args << QStringLiteral("--shim-annot") << o.shimAnnotation;
    args << QStringLiteral("-o") << bundle << op1a;
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(tool, args);
    if (!process.waitForStarted()) return fail(QObject::tr("bmxtranswrap could not be started."));
    process.waitForFinished(-1);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !QFileInfo(bundle).isDir()) {
        const QString output = QString::fromUtf8(process.readAll()).trimmed();
        QDir(bundle).removeRecursively();
        return fail(QObject::tr("bmxtranswrap failed: %1").arg(output.right(600)));
    }
    return true;
}

} // namespace lumenis::media
