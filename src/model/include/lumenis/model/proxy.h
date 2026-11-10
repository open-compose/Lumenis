#pragma once

#include "lumenis/model/bin.h"

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Proxy media (Editing Guide ch.16, p.508-513; 8.7a, docs/spec/proxy-media.md). "Proxies are multiformat and utilize either
// Avid DNx LB or H.264, depending on quality setting in the Timeline" (p.508): a clip with proxies keeps both files'
// links in its Source Settings under "proxy"; the Play button's proxy mode (p.509) chooses whether a clip plays its own
// media or a proxy, and which proxy by the Timeline's video quality ("When the Timeline Video Quality is set to Full
// Quality (8-bit or greater), an associated proxy will display using the H.264 format. If Draft Quality or Best
// Performance is selected, the Avid DNx LB format will be used", p.510). MainWindow::playedClip applies it; the files are
// made by media::makeProxyMedia (lumenis/media/proxy_media.h).
namespace lumenis::model {

// A clip's proxies: the H.264 and Avid DNx LB files' links (media::Link JSON, as a linked clip keeps its link), where
// they are, and whether they carry sound ("Proxy Audio shows AAC if there is audio associated with the proxy", p.510).
struct ProxyMedia {
    QJsonObject h264Link;
    QJsonObject lbLink;
    QString folder; // "Proxy Path shows the location where the proxy media resides" (p.510)
    bool audio = false;
    QSize raster;
    QJsonObject toJson() const;
    static std::optional<ProxyMedia> fromJson(const QJsonObject& o);
    // The file path of one of the links.
    static QString pathOf(const QJsonObject& link);
};

std::optional<ProxyMedia> proxyOf(const BinItem& item);
inline bool hasProxy(const BinItem& item) { return proxyOf(item).has_value(); }
void setProxy(BinItem& item, const ProxyMedia& proxy);
// "If proxies have been deleted, icons will no longer be shown as orange" (p.509): drops the proxies.
bool removeProxy(BinItem& item);

// Whether an item can have proxies made: "Select the master clips, subclips, groups or sequences in your bin" (p.508);
// subclips, groups and sequences give their master clips (MainWindow::createProxies).
bool canHaveProxy(const BinItem& item);

// The Play button's right-click choices (p.509): "High-Resolution Only", "Proxy Preferred", "High-Resolution Preferred".
enum class ProxyPlayback { HighResolutionOnly, ProxyPreferred, HighResolutionPreferred };
QString proxyPlaybackText(ProxyPlayback mode);

// The link a clip plays (p.509-510): its own (empty) unless
// * Proxy Preferred and a proxy is there ("If the proxy is offline or not available but the high-resolution, local media
//   is, then the high-resolution media will be played"), or
// * High-Resolution Preferred, the clip's own media offline and a proxy there.
// The proxy is the H.264 file at Full Quality and the DNx LB file at Draft Quality and Best Performance; ours: the other
// one when that one is gone. `exists` says whether a file is there.
QJsonObject playedProxyLink(const BinItem& clip, ProxyPlayback mode, bool fullQuality, bool ownMediaOnline,
                            const std::function<bool(const QString& path)>& exists);

// The bin's proxy columns (p.509-510), at the bottom of Choose Columns as the MultiRez headings are (ours).
inline const QStringList& proxyHeadings() {
    static const QStringList h{QStringLiteral("Proxy Offline"), QStringLiteral("Proxy Video"), QStringLiteral("Proxy Audio"),
                               QStringLiteral("Proxy Path")};
    return h;
}
// A clip's cell under a proxy heading: Proxy Offline names what of the proxy is missing ("V", "A", "V A"; ours, as the
// Offline column, "similar to the original Offline column"), Proxy Video the format shown now ("H.264" at Full Quality,
// "Avid DNx LB" otherwise), Proxy Audio "AAC" when the proxy has sound, Proxy Path its folder. Empty for clips without
// proxies.
QString proxyCellText(const BinItem& clip, const QString& heading, bool fullQuality,
                      const std::function<bool(const QString& path)>& exists);

} // namespace lumenis::model
