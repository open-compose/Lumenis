// Proxy media's data, the Play button's proxy mode and the bin's proxy columns (proxy.h; Editing Guide p.508-510).
#include "lumenis/model/proxy.h"

#include <QJsonValue>

namespace lumenis::model {

namespace {

const QString kProxyKey = QStringLiteral("proxy");

} // namespace

QJsonObject ProxyMedia::toJson() const {
    return QJsonObject{{QStringLiteral("h264"), h264Link},
                       {QStringLiteral("lb"), lbLink},
                       {QStringLiteral("folder"), folder},
                       {QStringLiteral("audio"), audio},
                       {QStringLiteral("width"), raster.width()},
                       {QStringLiteral("height"), raster.height()}};
}

std::optional<ProxyMedia> ProxyMedia::fromJson(const QJsonObject& o) {
    ProxyMedia p;
    p.h264Link = o.value(QStringLiteral("h264")).toObject();
    p.lbLink = o.value(QStringLiteral("lb")).toObject();
    if (p.h264Link.isEmpty() && p.lbLink.isEmpty()) return std::nullopt;
    p.folder = o.value(QStringLiteral("folder")).toString();
    p.audio = o.value(QStringLiteral("audio")).toBool();
    p.raster = QSize(o.value(QStringLiteral("width")).toInt(), o.value(QStringLiteral("height")).toInt());
    return p;
}

QString ProxyMedia::pathOf(const QJsonObject& link) {
    return link.value(QStringLiteral("path")).toString();
}

std::optional<ProxyMedia> proxyOf(const BinItem& item) {
    const QJsonValue v = item.sourceSettings.value(kProxyKey);
    return v.isObject() ? ProxyMedia::fromJson(v.toObject()) : std::nullopt;
}

void setProxy(BinItem& item, const ProxyMedia& proxy) {
    item.sourceSettings.insert(kProxyKey, proxy.toJson());
}

bool removeProxy(BinItem& item) {
    if (!item.sourceSettings.contains(kProxyKey)) return false;
    item.sourceSettings.remove(kProxyKey);
    return true;
}

bool canHaveProxy(const BinItem& item) {
    return item.kind == BinItemKind::MasterClip || item.kind == BinItemKind::Subclip || item.kind == BinItemKind::Group ||
           item.kind == BinItemKind::Sequence;
}

QString proxyPlaybackText(ProxyPlayback mode) {
    switch (mode) {
    case ProxyPlayback::ProxyPreferred: return QStringLiteral("Proxy Preferred");
    case ProxyPlayback::HighResolutionPreferred: return QStringLiteral("High-Resolution Preferred");
    case ProxyPlayback::HighResolutionOnly: break;
    }
    return QStringLiteral("High-Resolution Only");
}

QJsonObject playedProxyLink(const BinItem& clip, ProxyPlayback mode, bool fullQuality, bool ownMediaOnline,
                            const std::function<bool(const QString& path)>& exists) {
    if (mode == ProxyPlayback::HighResolutionOnly) return {};
    if (mode == ProxyPlayback::HighResolutionPreferred && ownMediaOnline) return {};
    const auto proxy = proxyOf(clip);
    if (!proxy) return {};
    const QJsonObject& first = fullQuality ? proxy->h264Link : proxy->lbLink;
    const QJsonObject& second = fullQuality ? proxy->lbLink : proxy->h264Link;
    for (const QJsonObject* link : {&first, &second}) {
        const QString path = ProxyMedia::pathOf(*link);
        if (!path.isEmpty() && (!exists || exists(path))) return *link;
    }
    return {};
}

QString proxyCellText(const BinItem& clip, const QString& heading, bool fullQuality,
                      const std::function<bool(const QString& path)>& exists) {
    const auto proxy = proxyOf(clip);
    if (!proxy) return {};
    if (heading == QLatin1String("Proxy Video")) return fullQuality ? QStringLiteral("H.264") : QStringLiteral("Avid DNx LB");
    if (heading == QLatin1String("Proxy Audio")) return proxy->audio ? QStringLiteral("AAC") : QString();
    if (heading == QLatin1String("Proxy Path")) return proxy->folder;
    if (heading == QLatin1String("Proxy Offline")) {
        // The proxy shown at this quality: its picture and, when it has any, its sound.
        const QString path = ProxyMedia::pathOf(fullQuality ? proxy->h264Link : proxy->lbLink);
        const bool there = !path.isEmpty() && (!exists || exists(path));
        if (there) return {};
        return proxy->audio ? QStringLiteral("V A") : QStringLiteral("V");
    }
    return {};
}

} // namespace lumenis::model
