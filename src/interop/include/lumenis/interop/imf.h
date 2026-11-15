#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <cstdint>
#include <functional>
#include <optional>

// Interoperable Master Format packages (11.5c; docs/spec/imf.md; Editing Guide p.333-340, "IMF Original and
// Supplemental Support"): reading a package's documents from its root folder, on SMPTE's published structure for them
// (the asset map of ST 429-9, the packing list and composition playlist of ST 2067-2 and 2067-3, whose XML schemas are
// public), and making a native sequence of a composition playlist. Element names are matched without their namespace,
// so the 2013, 2016 and 2020 editions all read.
namespace lumenis::interop::imf {

// One resource of a virtual track: frames [entryPoint, entryPoint + sourceDuration) of a track file, repeated.
struct Resource {
    QUuid id;
    QUuid trackFile;              // TrackFileId
    Rational editRate;            // the resource's, else the composition's
    std::int64_t intrinsicDuration = 0;
    std::int64_t entryPoint = 0;
    std::int64_t sourceDuration = 0; // IntrinsicDuration - EntryPoint when not given
    std::int64_t repeatCount = 1;
    std::int64_t length() const { return sourceDuration * std::max<std::int64_t>(repeatCount, 1); }
};

// A virtual track's piece in one segment: MainImageSequence, MainAudioSequence, or another kind (subtitles, markers),
// by its element's name.
struct Sequence {
    QString kind;
    QUuid id;
    QUuid trackId;
    QList<Resource> resources;
    bool isImage() const { return kind == QLatin1String("MainImageSequence"); }
    bool isAudio() const { return kind == QLatin1String("MainAudioSequence"); }
};

struct Segment {
    QUuid id;
    QList<Sequence> sequences;
};

// A composition playlist (ST 2067-3).
struct Composition {
    QUuid id;
    QString file;          // its path
    QString contentTitle;
    QString annotation;
    QString issuer;
    QString creator;
    QString issueDate;
    QString contentKind;
    Rational editRate{24};
    QList<Segment> segments;
    // Every track file a resource names, in order of first use.
    QList<QUuid> trackFiles() const;
};

// A packing list (ST 2067-2): an IMP.
struct PackingList {
    QUuid id;
    QString file;
    QString annotation;
    QString issuer;
    QString creator;
    struct Entry {
        QUuid id;
        QString type;  // "text/xml", "application/mxf"
        QString hash;  // base64 SHA-1
        std::int64_t size = 0;
        QString originalFileName;
    };
    QList<Entry> assets;
};

// A package as its root folder holds it: the asset map's assets, its IMPs and their CPLs.
struct Package {
    QString root;          // the folder
    QString name;          // the folder's name
    QUuid assetMapId;
    QString annotation;    // the asset map's
    QString issuer;
    QString creator;
    QHash<QUuid, QString> assets; // every asset by id, its absolute path
    QList<PackingList> imps;
    QList<Composition> compositions;
    // A Supplemental's CPLs name track files that are not in its own asset map (they are its Original's);
    // `missingTrackFiles` lists them, and they resolve through `assets` once the Original's map is added (addAssets).
    bool isSupplemental() const;
    QList<QUuid> missingTrackFiles() const;
    void addAssets(const Package& other);
};

// The package at `root`: its ASSETMAP.xml, every packing list and composition playlist the map lists. Nothing, with
// `error` set, when there is no asset map or it cannot be read; damaged documents beyond it are left out with a line
// each in `warnings`.
std::optional<Package> readPackage(const QString& root, QString* error = nullptr, QStringList* warnings = nullptr);
// One document by itself (tests, and the CPL a drag carries).
std::optional<Composition> readComposition(const QString& path, QString* error = nullptr);
// Whether a file is a composition playlist: an .xml whose root element is CompositionPlaylist.
bool isComposition(const QString& path);

// The native sequence of a composition (docs/spec/imf.md, "Into a bin"): MainImageSequence resources one after another
// on V1, each MainAudioSequence (a virtual track) on audio tracks from A1, a track per channel of its track files'
// clips. `clipOf` gives the clip linked to a track file (null when it is not linked: the resource becomes filler, with
// a warning). The sequence is at the composition's edit rate.
model::BinItem compositionSequence(const Composition& composition, const std::function<const model::BinItem*(const QUuid&)>& clipOf,
                                   const QDateTime& when, QStringList* warnings = nullptr);

// "urn:uuid:..." to a UUID and back.
QUuid uuidOf(const QString& text);
QString urn(const QUuid& id);

} // namespace lumenis::interop::imf
