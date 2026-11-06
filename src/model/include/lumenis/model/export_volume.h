// AS-02 Export Volumes (Phase 7.2d; docs/spec/as02.md; Editing Guide p.901-904): "a specification for grouping
// multiple versions of program content into one single bundle". An Export Volume is a bin (Bin::exportVolume) holding
// copies of sequences; committing it writes each sequence as an AS-02 bundle in the volume's asset folder (through
// bmx's bmxtranswrap, lumenis/media/as11.h) and marks it in the bin's Media Status column.
#pragma once

#include "lumenis/model/bin.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace lumenis::model {

struct ExportVolume {
    QString type = QStringLiteral("AS-02"); // Volume Type (p.903): AS-02 is the only one
    QString path;           // where the volume's asset folder is made (Set, p.903)
    QString name;           // the volume's name, its folder's name
    QString shimFile;       // the shim template chosen (Set, p.903), or empty
    // From the shim template, or bmxtranswrap's defaults when empty.
    QString shimName;
    QString shimId;
    QString shimAnnotation;
    // The essence's compression (a media::movieCompressions() id). "AS-02 supports J2K, Uncompressed 10b RGB, DNxHD,
    // AVCI, and Uncompressed 8b for SD" (p.903); "the video is encoded to the J2K codec" by default (p.904).
    QString compression = QStringLiteral("j2k");

    // The asset folder: <path>/<name>.
    QString folder() const;
    QJsonObject toJson() const;
    static ExportVolume fromJson(const QJsonObject& o);
    bool operator==(const ExportVolume&) const = default;
};

// The bin's volume, or nothing for an ordinary bin.
std::optional<ExportVolume> exportVolumeOf(const Bin& bin);

// The Media Status column: "Green indicates the asset is committed. Yellow indicates the asset is not committed. Red
// indicates the asset is offline" (p.904).
inline const QString kMediaStatusHeading = QStringLiteral("Media Status");
inline const QString kCommitted = QStringLiteral("Committed");
inline const QString kNotCommitted = QStringLiteral("Not Committed");

// Reads an AS-02 shim template (an XML file with ShimName, ShimID and AnnotationText elements, as AS-02's shim.xml
// has them) into the volume's shim fields. False when the file cannot be read as XML.
bool readShimTemplate(const QString& path, ExportVolume& volume);

} // namespace lumenis::model
