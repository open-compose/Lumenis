// AS-11 export (Phase 7.2c; docs/spec/as11.md; Editing Guide p.891-893) and AS-02 bundles (7.2d; docs/spec/as02.md,
// p.901-904): "AS-11 is an OP1A MXF file format for the
// delivery of finished programming. This specification requires program segment markers." Our OP1a file
// (movie_export.h) is rewrapped as AS-11 OP1a by bmx's bmxtranswrap (https://github.com/bbc/bmx, BSD-3), which writes
// the AS-11 core and UK DPP descriptive metadata frameworks and the segmentation from the text files made here. FFmpeg
// writes no descriptive metadata, so without bmxtranswrap there is no AS-11 export.
#pragma once

#include "lumenis/core/rational.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace lumenis::media {

// One AS-11 shim (AMWA's AS-11 X1 to X9): its bmxtranswrap --spec-id and its name for the ShimName property.
struct As11Shim {
    QString id;    // "as11-x8"
    QString name;  // "AMWA AS-11 X8"
    QString about; // what it delivers
    bool dpp = false; // a UK DPP shim, which takes the UK DPP framework too
};
const QList<As11Shim>& as11Shims();
const As11Shim* findAs11Shim(const QString& id);

// A programme segment: [start, start + duration) in frames of the export, from a spanned marker.
struct As11Segment {
    std::int64_t start = 0;
    std::int64_t duration = 0;
};

// "Name: value" lines, as bmxtranswrap's --dm-file reads them; empty values are left out.
QString as11FrameworkText(const QList<QPair<QString, QString>>& properties);
// The segmentation file of --seg: "<n>/<count>\t<start timecode> <duration timecode>" per segment, in order, the start
// at the export's own timecode (`startFrame`, timecode frames at `rate`).
QString as11SegmentationText(const QList<As11Segment>& segments, std::int64_t startFrame, const Rational& rate, bool dropFrame);

// bmxtranswrap: LUMENIS_BMXTRANSWRAP, else beside the application, else on PATH; empty when there is none.
QString bmxTranswrapPath();

struct As11Options {
    QString shim;                                // an as11Shims() id
    QList<QPair<QString, QString>> core;         // AS-11 core framework properties but ShimName (set from the shim)
    QList<QPair<QString, QString>> dpp;          // UK DPP framework properties (DPP shims)
    QList<As11Segment> segments;
    std::int64_t startFrame = 0;                 // the export's start timecode, in frames
    Rational rate{25, 1};
    bool dropFrame = false;
};

// Rewraps the OP1a file `op1a` as the AS-11 file `path`. False with the reason in `error` (bmxtranswrap's own output
// included); nothing is left at `path` then.
bool writeAs11(const QString& op1a, const QString& path, const As11Options& options, QString* error = nullptr);

// An AS-02 bundle (7.2d): "The Asset.mxf file is the sequence (version). The Manifest.xml file lists ... all the files
// and folders in the bundle. The Shim.xml file is used as a template ... The Media folder contains all the media
// files" (p.902), written by bmxtranswrap -t as02 from the OP1a file into the folder `bundle`. Empty shim fields take
// bmxtranswrap's defaults.
struct As02Options {
    QString shimName;
    QString shimId;
    QString shimAnnotation;
};
bool writeAs02(const QString& op1a, const QString& bundle, const As02Options& options, QString* error = nullptr);

} // namespace lumenis::media
