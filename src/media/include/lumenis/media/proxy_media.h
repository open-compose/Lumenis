#pragma once

#include "lumenis/model/proxy.h"

#include <QSize>
#include <QString>

#include <functional>
#include <optional>

// Making proxy media (Editing Guide p.508-510; 8.7a, docs/spec/proxy-media.md): a clip's frames and sound written again at
// a small raster as an H.264 MP4 and an Avid DNx LB MOV, each with AAC sound ("Proxies are multiformat and utilize either
// Avid DNx LB or H.264", p.508; "Proxy Audio shows AAC", p.510), in `<drive>/Avid MediaFiles/Proxy/<n>/` ("Proxy media is
// placed in the Proxy folder based on the next available number", p.513).
namespace lumenis::media {

// The folder proxies are written in on `drive` (a media drive's root): Avid MediaFiles/Proxy/1 (ours: one numbered
// folder, as the managed media's MXF/1).
QString proxyFolder(const QString& drive);

// The proxy raster for a project raster: half its width and height, even and at least 256 by 144, the smallest Avid DNx
// LB takes with room (ours: the guide gives none).
QSize proxyRaster(QSize projectRaster);

// Writes `clip`'s proxies into proxyFolder(drive) and returns what the clip keeps of them (model::setProxy). The picture
// is the clip's own frames from frame 0 to its end; the sound its first two channels when its media is at 48 kHz (ours:
// none otherwise). Nothing, with `error`, when the clip has no picture, H.264 or DNx is not in this build, or a file
// cannot be written. `progress` gets 0 to 1 and returns false to cancel. Runs on any thread.
std::optional<model::ProxyMedia> makeProxyMedia(const model::BinItem& clip, const QString& drive, QSize projectRaster,
                                                const std::function<bool(double)>& progress = {}, QString* error = nullptr);

} // namespace lumenis::media
