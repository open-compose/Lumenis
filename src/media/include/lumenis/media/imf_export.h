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

// A written track file: its asset id, absolute path, essence and duration in its own edit rate.
struct ImfTrackFile {
    QUuid id;
    QString path;
    bool picture = true;
    std::int64_t duration = 0;
    Rational editRate{24};
};

// One IMF audio track (MultiChannel Audio Mapping, 11.5c3, p.338-340): its format and its MCA descriptive metadata.
struct ImfSoundOptions {
    QString format = QStringLiteral("stereo"); // "mono", "stereo", "5.1" or "7.1"
    // MCA Content Kind, as the label of the soundfield group of groups (ST 2067-8): "main" (Main Program, ggMPg),
    // "dvs" (Descriptive Video Service, ggDVS) or "dialog" (Dialog Centric Mix, ggDcm). Ours: the guide does not
    // list its menu's values.
    QString contentKind = QStringLiteral("main");
    QString language;      // the MCA Language, an RFC 5646 language subtag ("en"); empty: none
    QString region;        // the region subtag ("US"); with a language, the tag is "en-US"
    QString name;          // the files' base name
    QString wrapTool;      // raw2bmx; empty: raw2bmxPath()
};
// The channels of a format (mono 1, stereo 2, 5.1 6, 7.1 8).
int imfSoundChannels(const QString& format);
// raw2bmx's labels file for a track (bmx's docs/mca_labels_format.md): one channel label a channel (chM1; chL, chR;
// chL, chR, chC, chLFE, chLs, chRs; 7.1 with chLss, chRss, chLrs, chRrs), the soundfield group (sgM, sgST, sg51,
// sg71) and the group of groups with the language.
QString imfMcaLabels(const ImfSoundOptions& options);
// RFC 5646's tag of a language and region ("en-US"), empty without a language.
QString imfLanguageTag(const QString& language, const QString& region);
// The languages offered for MCA Language (RFC 5646 subtags) and, for one, its regions (ours: a short list).
QStringList imfLanguages();
QStringList imfRegions(const QString& language);

// Writes `source`'s sound (its picture is not used) as one IMF sound track file in `root`, MXF OP1a through raw2bmx
// with the MCA labels; `file` gets it. Same failure contract as writeImfTrackFiles.
bool writeImfSoundFile(const QString& root, const MovieSource& source, const ImfSoundOptions& options, ImfTrackFile* file,
                       const std::function<bool(double)>& progress = {}, QString* error = nullptr);

struct ImfTrackOptions {
    QString profile = QStringLiteral("J2K IMF YCrCb");
    QString name;          // the files' base name (the composition's content title, made safe)
    // The sound: the mix's first `channels` channels as one stereo track file; 0 for none (MultiChannel Audio Mapping
    // writes its tracks with writeImfSoundFile).
    int channels = 2;
    QString wrapTool;      // raw2bmx; empty: raw2bmxPath()
};

// Writes `source`'s picture, and its sound when it has some, into the existing folder `root`. `progress` gets 0 to 1
// and returns false to cancel. On failure what was written is removed and `error` says why.
bool writeImfTrackFiles(const QString& root, const MovieSource& source, const ImfTrackOptions& options,
                        QList<ImfTrackFile>* files, const std::function<bool(double)>& progress = {}, QString* error = nullptr);

} // namespace lumenis::media
