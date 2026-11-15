// The track files of an IMF composition written (11.5c2; docs/spec/imf.md, "Export"; Editing Guide p.333-341). The
// picture is JPEG 2000 in MXF OP1a through FFmpeg's own jpeg2000 encoder, in one of the guide's IMF resolutions (J2K
// IMF YCrCb, J2K IMF RGB 12 bit and 10 bit, p.1256-1257); each sound track file is 24-bit 48 kHz WAVE wrapped as MXF
// by bmx's raw2bmx (https://github.com/bbc/bmx, BSD-3), since FFmpeg's MXF muxer needs a video stream. Ours: the
// files follow ST 2067-5's outline (one essence a file) but are not validated against it (docs/spec/imf.md "Watch").
#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/media/movie_export.h"

#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <cstdint>
#include <functional>

namespace lumenis::media {

// The encoding profiles a CPL's picture can have (p.1256-1257), by the names the guide gives.
QStringList imfEncodingProfiles();
// Whether a project's raster and rate are in the guide's IMF Supported Formats table (p.340-341): HD 1920 x 1080, 2K
// 2048 x 1080, UHD 3840 x 2160 and 4K 4096 x 2160, progressive at 23.976 to 60 (4K: RGB only, up to 30). `why` says what
// does not fit.
bool imfFormatSupported(QSize raster, const Rational& rate, bool interlaced, const QString& profile, QString* why = nullptr);
// raw2bmx: LUMENIS_RAW2BMX, else beside the application, else on PATH. Empty when not found.
QString raw2bmxPath();

struct ImfTrackOptions {
    QString profile = QStringLiteral("J2K IMF YCrCb");
    QString name;          // the files' base name (the composition's content title, made safe)
    // The sound: the mix's first `channels` channels as one track file (MultiChannel Audio Mapping, 11.5c3, makes more).
    int channels = 2;
    QString wrapTool;      // raw2bmx; empty: raw2bmxPath()
};
// A written track file: its asset id, absolute path, essence and duration in its own edit rate.
struct ImfTrackFile {
    QUuid id;
    QString path;
    bool picture = true;
    std::int64_t duration = 0;
    Rational editRate{24};
};

// Writes `source`'s picture, and its sound when it has some, into the existing folder `root`. `progress` gets 0 to 1
// and returns false to cancel. On failure what was written is removed and `error` says why.
bool writeImfTrackFiles(const QString& root, const MovieSource& source, const ImfTrackOptions& options,
                        QList<ImfTrackFile>* files, const std::function<bool(double)>& progress = {}, QString* error = nullptr);

} // namespace lumenis::media
