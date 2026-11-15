// IMF documents written (imf_writer.h; docs/spec/imf.md, "Export"), with QXmlStreamWriter in the element order SMPTE's
// schemas give.
#include "lumenis/interop/imf_writer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QXmlStreamWriter>

#include <algorithm>

namespace lumenis::interop::imf {

namespace {

constexpr char kCplNs[] = "http://www.smpte-ra.org/schemas/2067-3/2016";
constexpr char kCoreNs[] = "http://www.smpte-ra.org/schemas/2067-2/2016";
constexpr char kPklNs[] = "http://www.smpte-ra.org/schemas/2067-2/2016/PKL";
constexpr char kAmNs[] = "http://www.smpte-ra.org/schemas/429-9/2007/AM";
constexpr char kXsiNs[] = "http://www.w3.org/2001/XMLSchema-instance";

QString rate(const Rational& r) {
    return QStringLiteral("%1 %2").arg(r.num()).arg(r.den());
}

QString date(const QDateTime& when) {
    return (when.isValid() ? when : QDateTime::currentDateTimeUtc()).toUTC().toString(Qt::ISODate);
}

void text(QXmlStreamWriter& x, const char* name, const QString& value) {
    x.writeTextElement(QLatin1String(name), value);
}

} // namespace

QByteArray compositionXml(const Composition& c, const QDateTime& issueDate) {
    QByteArray out;
    QXmlStreamWriter x(&out);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeDefaultNamespace(QLatin1String(kCplNs));
    x.writeNamespace(QLatin1String(kCoreNs), QStringLiteral("cc"));
    x.writeNamespace(QLatin1String(kXsiNs), QStringLiteral("xsi"));
    x.writeStartElement(QLatin1String(kCplNs), QStringLiteral("CompositionPlaylist"));
    text(x, "Id", urn(c.id));
    if (!c.annotation.isEmpty()) text(x, "Annotation", c.annotation);
    text(x, "IssueDate", date(issueDate));
    if (!c.issuer.isEmpty()) text(x, "Issuer", c.issuer);
    if (!c.creator.isEmpty()) text(x, "Creator", c.creator);
    text(x, "ContentTitle", c.contentTitle);
    if (!c.contentKind.isEmpty()) text(x, "ContentKind", c.contentKind);
    text(x, "EditRate", rate(c.editRate));
    x.writeStartElement(QStringLiteral("SegmentList"));
    for (const Segment& s : c.segments) {
        x.writeStartElement(QStringLiteral("Segment"));
        text(x, "Id", urn(s.id));
        x.writeStartElement(QStringLiteral("SequenceList"));
        for (const Sequence& q : s.sequences) {
            x.writeStartElement(QLatin1String(kCoreNs), q.kind);
            x.writeTextElement(QLatin1String(kCoreNs), QStringLiteral("Id"), urn(q.id));
            x.writeTextElement(QLatin1String(kCoreNs), QStringLiteral("TrackId"), urn(q.trackId));
            x.writeStartElement(QLatin1String(kCoreNs), QStringLiteral("ResourceList"));
            for (const Resource& r : q.resources) {
                x.writeStartElement(QLatin1String(kCoreNs), QStringLiteral("Resource"));
                x.writeAttribute(QLatin1String(kXsiNs), QStringLiteral("type"), QStringLiteral("cc:TrackFileResourceType"));
                x.writeTextElement(QLatin1String(kCoreNs), QStringLiteral("Id"), urn(r.id));
                x.writeTextElement(QLatin1String(kCoreNs), QStringLiteral("EditRate"), rate(r.editRate));
                x.writeTextElement(QLatin1String(kCoreNs), QStringLiteral("IntrinsicDuration"), QString::number(r.intrinsicDuration));
                x.writeTextElement(QLatin1String(kCoreNs), QStringLiteral("EntryPoint"), QString::number(r.entryPoint));
                x.writeTextElement(QLatin1String(kCoreNs), QStringLiteral("SourceDuration"), QString::number(r.sourceDuration));
                x.writeTextElement(QLatin1String(kCoreNs), QStringLiteral("RepeatCount"), QString::number(std::max<std::int64_t>(r.repeatCount, 1)));
                x.writeTextElement(QLatin1String(kCoreNs), QStringLiteral("TrackFileId"), urn(r.trackFile));
                x.writeEndElement();
            }
            x.writeEndElement(); // ResourceList
            x.writeEndElement(); // the sequence
        }
        x.writeEndElement(); // SequenceList
        x.writeEndElement(); // Segment
    }
    x.writeEndElement(); // SegmentList
    x.writeEndElement();
    x.writeEndDocument();
    return out;
}

QByteArray packingListXml(const PackingList& p, const QDateTime& issueDate) {
    QByteArray out;
    QXmlStreamWriter x(&out);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeDefaultNamespace(QLatin1String(kPklNs));
    x.writeStartElement(QLatin1String(kPklNs), QStringLiteral("PackingList"));
    text(x, "Id", urn(p.id));
    if (!p.annotation.isEmpty()) text(x, "Annotation", p.annotation);
    text(x, "IssueDate", date(issueDate));
    text(x, "Issuer", p.issuer);
    text(x, "Creator", p.creator);
    x.writeStartElement(QStringLiteral("AssetList"));
    for (const PackingList::Entry& e : p.assets) {
        x.writeStartElement(QStringLiteral("Asset"));
        text(x, "Id", urn(e.id));
        text(x, "Hash", e.hash);
        text(x, "Size", QString::number(e.size));
        text(x, "Type", e.type);
        if (!e.originalFileName.isEmpty()) text(x, "OriginalFileName", e.originalFileName);
        x.writeEmptyElement(QStringLiteral("HashAlgorithm"));
        x.writeAttribute(QStringLiteral("Algorithm"), QStringLiteral("http://www.w3.org/2000/09/xmldsig#sha1"));
        x.writeEndElement();
    }
    x.writeEndElement(); // AssetList
    x.writeEndElement();
    x.writeEndDocument();
    return out;
}

QByteArray assetMapXml(const QUuid& id, const QString& annotation, const QString& issuer, const QString& creator,
                       const QList<std::pair<QUuid, QString>>& assets, const QList<QUuid>& packingLists,
                       const QDateTime& issueDate) {
    QByteArray out;
    QXmlStreamWriter x(&out);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeDefaultNamespace(QLatin1String(kAmNs));
    x.writeStartElement(QLatin1String(kAmNs), QStringLiteral("AssetMap"));
    text(x, "Id", urn(id));
    if (!annotation.isEmpty()) text(x, "AnnotationText", annotation);
    text(x, "Creator", creator);
    text(x, "VolumeCount", QStringLiteral("1"));
    text(x, "IssueDate", date(issueDate));
    text(x, "Issuer", issuer);
    x.writeStartElement(QStringLiteral("AssetList"));
    for (const auto& [asset, path] : assets) {
        x.writeStartElement(QStringLiteral("Asset"));
        text(x, "Id", urn(asset));
        if (packingLists.contains(asset)) text(x, "PackingList", QStringLiteral("true"));
        x.writeStartElement(QStringLiteral("ChunkList"));
        x.writeStartElement(QStringLiteral("Chunk"));
        text(x, "Path", path);
        text(x, "VolumeIndex", QStringLiteral("1"));
        x.writeEndElement();
        x.writeEndElement();
        x.writeEndElement();
    }
    x.writeEndElement(); // AssetList
    x.writeEndElement();
    x.writeEndDocument();
    return out;
}

QString fileHash(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {}; // unreadable: no hash, which the caller reports
    QCryptographicHash h(QCryptographicHash::Sha1);
    if (!h.addData(&f)) return {};
    return QString::fromLatin1(h.result().toBase64());
}

bool writePackageDocuments(NewPackage& p, QString* error) {
    auto fail = [&](const QString& text) {
        if (error) *error = text;
        return false;
    };
    const QDir root(p.root);
    const QDateTime when = p.issueDate.isValid() ? p.issueDate : QDateTime::currentDateTimeUtc();
    if (p.assetMapId.isNull()) p.assetMapId = QUuid::createUuid();
    QList<std::pair<QUuid, QString>> mapped;
    QList<QUuid> packingLists;
    auto writeFile = [](const QString& path, const QByteArray& bytes) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.flush();
    };
    auto entryOf = [&](const QUuid& id, const QString& path, const QString& type, PackingList::Entry* e) {
        e->id = id;
        e->type = type;
        e->hash = fileHash(path);
        e->size = QFileInfo(path).size();
        e->originalFileName = QFileInfo(path).fileName();
        return !e->hash.isEmpty();
    };
    for (NewPackingList& imp : p.imps) {
        if (imp.id.isNull()) imp.id = QUuid::createUuid();
        PackingList pkl;
        pkl.id = imp.id;
        pkl.annotation = imp.annotation;
        pkl.issuer = imp.issuer;
        pkl.creator = imp.creator;
        for (NewComposition& nc : imp.compositions) {
            Composition& c = nc.playlist;
            if (c.id.isNull()) c.id = QUuid::createUuid();
            // One segment with each track file whole: the picture as the MainImageSequence, each sound file a
            // MainAudioSequence of its own (an IMF audio track).
            Segment s;
            s.id = QUuid::createUuid();
            for (TrackFile& f : nc.files) {
                if (f.id.isNull()) f.id = QUuid::createUuid();
                Sequence q;
                q.kind = f.picture ? QStringLiteral("MainImageSequence") : QStringLiteral("MainAudioSequence");
                q.id = QUuid::createUuid();
                q.trackId = QUuid::createUuid();
                Resource r;
                r.id = QUuid::createUuid();
                r.trackFile = f.id;
                r.editRate = f.editRate;
                r.intrinsicDuration = f.duration;
                r.sourceDuration = f.duration;
                q.resources << r;
                s.sequences << q;
                PackingList::Entry e;
                if (!entryOf(f.id, f.path, QStringLiteral("application/mxf"), &e)) {
                    return fail(QObject::tr("%1 could not be read.").arg(QDir::toNativeSeparators(f.path)));
                }
                pkl.assets << e;
                mapped << std::pair{f.id, root.relativeFilePath(f.path)};
            }
            c.segments = {s};
            const QString file = root.filePath(QStringLiteral("CPL_%1.xml").arg(c.id.toString(QUuid::WithoutBraces)));
            if (!writeFile(file, compositionXml(c, when))) return fail(QObject::tr("The composition playlist could not be written."));
            c.file = file;
            PackingList::Entry e;
            if (!entryOf(c.id, file, QStringLiteral("text/xml"), &e)) return fail(QObject::tr("The composition playlist could not be read."));
            pkl.assets << e;
            mapped << std::pair{c.id, root.relativeFilePath(file)};
        }
        const QString file = root.filePath(QStringLiteral("PKL_%1.xml").arg(pkl.id.toString(QUuid::WithoutBraces)));
        if (!writeFile(file, packingListXml(pkl, when))) return fail(QObject::tr("The packing list could not be written."));
        mapped << std::pair{pkl.id, root.relativeFilePath(file)};
        packingLists << pkl.id;
    }
    if (!writeFile(root.filePath(QStringLiteral("ASSETMAP.xml")),
                   assetMapXml(p.assetMapId, p.annotation, p.issuer, p.creator, mapped, packingLists, when))) {
        return fail(QObject::tr("The asset map could not be written."));
    }
    return true;
}

} // namespace lumenis::interop::imf
