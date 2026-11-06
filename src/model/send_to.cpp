// Send To templates (lumenis/model/send_to.h): the JSON of a .stt file and the Pro Tools options' exports.
#include "lumenis/model/send_to.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QObject>
#include <QSaveFile>

#include <algorithm>

namespace lumenis::model {

QJsonObject SendToTemplate::toJson() const {
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("export"), exportValues},
                       {QStringLiteral("folder"), folder},
                       {QStringLiteral("autoLaunch"), autoLaunch},
                       {QStringLiteral("application"), application},
                       {QStringLiteral("autoLoad"), autoLoad},
                       {QStringLiteral("reveal"), reveal}};
}

std::optional<SendToTemplate> SendToTemplate::fromJson(const QJsonObject& o) {
    SendToTemplate t;
    t.name = o.value(QStringLiteral("name")).toString().trimmed();
    if (t.name.isEmpty()) return std::nullopt;
    t.exportValues = o.value(QStringLiteral("export")).toObject();
    t.folder = o.value(QStringLiteral("folder")).toString();
    t.autoLaunch = o.value(QStringLiteral("autoLaunch")).toBool();
    t.application = o.value(QStringLiteral("application")).toString();
    t.autoLoad = o.value(QStringLiteral("autoLoad")).toBool(true);
    t.reveal = o.value(QStringLiteral("reveal")).toBool();
    return t;
}

QList<SendToTemplate> loadSendToTemplates(const QString& folder) {
    QList<SendToTemplate> out;
    const QFileInfoList files = QDir(folder).entryInfoList({QStringLiteral("*.stt")}, QDir::Files, QDir::Name);
    for (const QFileInfo& f : files) {
        QFile file(f.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) continue;
        QJsonObject o = doc.object();
        // A template is named by its file (p.866: "Name the new Send To (.stt) template").
        o.insert(QStringLiteral("name"), f.completeBaseName());
        if (auto t = SendToTemplate::fromJson(o)) out << *t;
    }
    std::sort(out.begin(), out.end(), [](const SendToTemplate& a, const SendToTemplate& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return out;
}

bool saveSendToTemplate(const QString& path, const SendToTemplate& t, QString* error) {
    QString to = path;
    if (!to.endsWith(QLatin1String(".stt"), Qt::CaseInsensitive)) to += QStringLiteral(".stt");
    QDir().mkpath(QFileInfo(to).absolutePath());
    QSaveFile file(to);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    SendToTemplate named = t;
    named.name = QFileInfo(to).completeBaseName();
    file.write(QJsonDocument(named.toJson()).toJson());
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

const QList<ProToolsOption>& proToolsOptions() {
    // The labels are the guide's (p.863-864).
    static const QList<ProToolsOption> all{
        {QStringLiteral("avidVideoConsolidateAudio"), QObject::tr("Avid Video - Consolidate Audio")},
        {QStringLiteral("quicktimeEmbedAudio"), QObject::tr("QuickTime - Embed Audio"), false},
        {QStringLiteral("quicktimeConsolidateAudio"), QObject::tr("QuickTime - Consolidate Audio")},
        {QStringLiteral("linkVideoAudio"), QObject::tr("Link to Video and Audio")},
        {QStringLiteral("videoMixdownLinkAudio"), QObject::tr("Video Mixdown - Link to Audio")},
        {QStringLiteral("quicktimeLinkAudio"), QObject::tr("QuickTime - Link to Audio")},
    };
    return all;
}

QList<QJsonObject> proToolsExports(const QString& option, const QJsonObject& base) {
    auto with = [&](std::initializer_list<std::pair<const char*, QJsonValue>> values) {
        QJsonObject o = base;
        for (const auto& [k, v] : values) o.insert(QLatin1String(k), v);
        return o;
    };
    // "Media Composer creates a video mixdown of the tracks, consolidates the audio, and saves the audio into the AAF
    // file" (p.863-864); ours keeps consolidated media beside the AAF (docs/spec/aaf.md "Export Methods").
    const QJsonObject aaf = with({{"exportAs", QStringLiteral("aaf")}, {"includeVideo", true}, {"includeAudio", true}});
    // The QuickTime options' movie of the video: DNxHR LB as a Pro Tools Session's default movie (p.876; ours here).
    const QJsonObject movie = with({{"exportAs", QStringLiteral("mov")}, {"movieTracks", QStringLiteral("video")},
                                    {"movieCompression", QStringLiteral("dnxhr_lb")}});
    auto aafWith = [&](const char* video, const char* audio, bool includeVideo) {
        QJsonObject o = aaf;
        o.insert(QStringLiteral("videoExportMethod"), QLatin1String(video));
        o.insert(QStringLiteral("audioExportMethod"), QLatin1String(audio));
        o.insert(QStringLiteral("includeVideo"), includeVideo);
        return o;
    };
    if (option == QLatin1String("avidVideoConsolidateAudio")) return {aafWith("mixdown", "consolidate", true)};
    if (option == QLatin1String("quicktimeConsolidateAudio")) return {movie, aafWith("link", "consolidate", false)};
    if (option == QLatin1String("linkVideoAudio")) return {aafWith("link", "link", true)};
    if (option == QLatin1String("videoMixdownLinkAudio")) return {aafWith("mixdown", "link", true)};
    if (option == QLatin1String("quicktimeLinkAudio")) return {movie, aafWith("link", "link", false)};
    return {};
}

} // namespace lumenis::model
