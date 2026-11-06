// AS-02 Export Volumes (lumenis/model/export_volume.h).
#include "lumenis/model/export_volume.h"

#include <QDir>
#include <QFile>
#include <QXmlStreamReader>

namespace lumenis::model {

QString ExportVolume::folder() const { return QDir(path).filePath(name); }

QJsonObject ExportVolume::toJson() const {
    return {{QStringLiteral("type"), type},           {QStringLiteral("path"), path},
            {QStringLiteral("name"), name},           {QStringLiteral("shimFile"), shimFile},
            {QStringLiteral("shimName"), shimName},   {QStringLiteral("shimId"), shimId},
            {QStringLiteral("shimAnnotation"), shimAnnotation}, {QStringLiteral("compression"), compression}};
}

ExportVolume ExportVolume::fromJson(const QJsonObject& o) {
    ExportVolume v;
    v.type = o.value(QStringLiteral("type")).toString(v.type);
    v.path = o.value(QStringLiteral("path")).toString();
    v.name = o.value(QStringLiteral("name")).toString();
    v.shimFile = o.value(QStringLiteral("shimFile")).toString();
    v.shimName = o.value(QStringLiteral("shimName")).toString();
    v.shimId = o.value(QStringLiteral("shimId")).toString();
    v.shimAnnotation = o.value(QStringLiteral("shimAnnotation")).toString();
    v.compression = o.value(QStringLiteral("compression")).toString(v.compression);
    return v;
}

std::optional<ExportVolume> exportVolumeOf(const Bin& bin) {
    if (bin.exportVolume.isEmpty()) return std::nullopt;
    return ExportVolume::fromJson(bin.exportVolume);
}

bool readShimTemplate(const QString& path, ExportVolume& volume) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QXmlStreamReader xml(&file);
    QString name, id, annotation;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) continue;
        // Element names without their namespace prefix.
        const QStringView element = xml.name();
        if (element == QLatin1String("ShimName")) name = xml.readElementText().trimmed();
        else if (element == QLatin1String("ShimID")) id = xml.readElementText().trimmed();
        else if (element == QLatin1String("AnnotationText")) annotation = xml.readElementText().trimmed();
    }
    if (xml.hasError()) return false;
    volume.shimFile = path;
    volume.shimName = name;
    volume.shimId = id;
    volume.shimAnnotation = annotation;
    return true;
}

} // namespace lumenis::model
