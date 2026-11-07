// Digital Cinema Packages (Phase 7.5; docs/spec/dcp.md). The Editing Guide only names "Output to DCP requirements for
// theatrical releases" among the outputs of a high-resolution master (p.519); the package is SMPTE's (ST 429-2 to
// 429-9). We write the picture as JPEG 2000 codestreams in X'Y'Z' (FFmpeg's OpenJPEG encoder with the DCI cinema
// profile) and the sound as 24-bit 48 kHz WAVE; asdcplib's asdcp-wrap (https://github.com/cinecert/asdcplib, BSD-3)
// wraps both as SMPTE MXF track files, and we write the Composition Playlist, Packing List, Asset Map and Volume Index.
// FFmpeg cannot write the multichannel OP-Atom sound a DCP holds, so without asdcp-wrap there is no DCP export.
#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/media/movie_export.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QSize>
#include <QString>

#include <cstdint>
#include <functional>

namespace lumenis::media {

struct DcpOptions {
    QString title;              // ContentTitleText; the CPL's annotation and the package folder's name
    QString kind = QStringLiteral("feature"); // ContentKind (ST 429-7): feature, trailer, short, test, advertisement, ...
    QString container = QStringLiteral("flat"); // "flat" (1.85), "scope" (2.39) or "full" (1.90)
    bool fourK = false;
    int channels = 6;           // 2 (L, R) or 6 (5.1: L, R, C, LFE, Ls, Rs)
    QString issuer = QStringLiteral("Cutthulhu");
    QDateTime issueDate;        // empty: now
    QString wrapTool;           // asdcp-wrap; empty: asdcpWrapPath()
};

// The DCI container for a choice: Flat 1998 x 1080, Scope 2048 x 858, Full 2048 x 1080, doubled for 4K.
QSize dcpContainerSize(const QString& container, bool fourK);
// The DCP frame rate of an edit rate: 24, 25, 30, 48, 50 or 60 frames a second (23.976 plays at 24, 29.97 at 30).
int dcpFrameRate(const Rational& editRate);
// Whether FFmpeg has the OpenJPEG encoder (libopenjpeg), the only one that writes X'Y'Z' codestreams; FFmpeg's own
// jpeg2000 encoder cannot. Builds without it (Homebrew's FFmpeg, for one) cannot export DCPs.
bool dcpEncoderAvailable();
// asdcp-wrap: LUMENIS_ASDCP_WRAP, else beside the application, else on PATH. Empty when not found.
QString asdcpWrapPath();

// One file of the package, for the Packing List and Asset Map.
struct DcpAsset {
    QString id{};      // a UUID without braces
    QString file{};    // its name in the package folder
    QString type{};    // "picture", "sound" or "cpl" ("pkl" for the Packing List itself)
    std::int64_t duration = 0; // frames, track files only
    qint64 size = 0;
    QByteArray sha1{}; // the file's SHA-1
};
struct DcpComposition {
    QString cplId, pklId, assetMapId, reelId;
    DcpOptions options;
    int frameRate = 24;
    QSize container;
    QList<DcpAsset> assets; // picture and sound, then the CPL
};
// The XML documents (ST 429-7 CPL, 429-8 PKL, 429-9 Asset Map and Volume Index), UTF-8.
QByteArray dcpCplXml(const DcpComposition& c);
QByteArray dcpPklXml(const DcpComposition& c);
QByteArray dcpAssetMapXml(const DcpComposition& c, const DcpAsset& pkl);
QByteArray dcpVolumeIndexXml();

// Writes `source` as a DCP in the new folder `folder` (made; must not exist yet). `progress` gets 0 to 1 and returns
// false to cancel; on failure the folder is removed and `error` says why.
bool writeDcp(const QString& folder, const MovieSource& source, const DcpOptions& options,
              const std::function<bool(double)>& progress = {}, QString* error = nullptr);

} // namespace lumenis::media
