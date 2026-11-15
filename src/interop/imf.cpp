// IMF packages read and made into sequences (imf.h; docs/spec/imf.md). The documents are small XML files: QXmlStreamReader
// walks each one, matching elements by their local names; the asset map ties asset ids to the files in the package.
#include "lumenis/interop/imf.h"

#include "lumenis/model/multichannel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

namespace lumenis::interop::imf {

namespace {

// "24 1" or "48000 1" as a rate; `fallback` for anything else.
Rational rateOf(const QString& text, const Rational& fallback) {
    const QStringList parts = text.simplified().split(QLatin1Char(' '));
    if (parts.size() != 2) return fallback;
    bool a = false;
    bool b = false;
    const qint64 num = parts.at(0).toLongLong(&a);
    const qint64 den = parts.at(1).toLongLong(&b);
    if (!a || !b || num <= 0 || den <= 0) return fallback;
    return Rational(num, den);
}

// The document's root element's local name, or empty when it is not XML.
QString rootName(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {}; // not readable: not a document of ours
    QXmlStreamReader x(&f);
    while (!x.atEnd()) {
        if (x.readNextStartElement()) return x.name().toString();
    }
    return {};
}

// Reads the text of every direct child element of the current element into `values` by name, and calls `child` for
// the ones that have children of their own (reader positioned on their start).
void readElement(QXmlStreamReader& x, QHash<QString, QString>& values, const std::function<void(QXmlStreamReader&)>& child = {}) {
    while (x.readNextStartElement()) {
        const QString name = x.name().toString();
        if (child && (name == QLatin1String("SegmentList") || name == QLatin1String("SequenceList") ||
                      name == QLatin1String("ResourceList") || name == QLatin1String("AssetList") ||
                      name == QLatin1String("ChunkList") || name == QLatin1String("Segment") ||
                      name == QLatin1String("Asset") || name == QLatin1String("Resource") ||
                      name == QLatin1String("Chunk") || name.endsWith(QLatin1String("Sequence")))) {
            child(x);
            continue;
        }
        // Mixed content is not expected in these documents; nested elements we do not read are skipped whole.
        values.insert(name, x.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
    }
}

std::int64_t number(const QHash<QString, QString>& v, const char* key, std::int64_t fallback) {
    bool ok = false;
    const std::int64_t n = v.value(QLatin1String(key)).toLongLong(&ok);
    return ok ? n : fallback;
}

Resource readResource(QXmlStreamReader& x, const Rational& rate) {
    QHash<QString, QString> v;
    readElement(x, v);
    Resource r;
    r.id = uuidOf(v.value(QStringLiteral("Id")));
    r.trackFile = uuidOf(v.value(QStringLiteral("TrackFileId")));
    r.editRate = rateOf(v.value(QStringLiteral("EditRate")), rate);
    r.intrinsicDuration = number(v, "IntrinsicDuration", 0);
    r.entryPoint = number(v, "EntryPoint", 0);
    r.sourceDuration = number(v, "SourceDuration", r.intrinsicDuration - r.entryPoint);
    r.repeatCount = number(v, "RepeatCount", 1);
    return r;
}

Sequence readSequence(QXmlStreamReader& x, const Rational& rate) {
    Sequence s;
    s.kind = x.name().toString();
    QHash<QString, QString> v;
    readElement(x, v, [&](QXmlStreamReader& r) {
        if (r.name() == QLatin1String("ResourceList")) {
            while (r.readNextStartElement()) {
                if (r.name() == QLatin1String("Resource")) s.resources << readResource(r, rate);
                else r.skipCurrentElement();
            }
        } else {
            r.skipCurrentElement();
        }
    });
    s.id = uuidOf(v.value(QStringLiteral("Id")));
    s.trackId = uuidOf(v.value(QStringLiteral("TrackId")));
    return s;
}

} // namespace

QUuid uuidOf(const QString& text) {
    QString t = text.trimmed();
    if (t.startsWith(QLatin1String("urn:uuid:"), Qt::CaseInsensitive)) t = t.mid(9);
    return QUuid::fromString(t);
}

QString urn(const QUuid& id) {
    return QStringLiteral("urn:uuid:") + id.toString(QUuid::WithoutBraces);
}

QList<QUuid> Composition::trackFiles() const {
    QList<QUuid> out;
    for (const Segment& s : segments) {
        for (const Sequence& q : s.sequences) {
            for (const Resource& r : q.resources) {
                if (!r.trackFile.isNull() && !out.contains(r.trackFile)) out << r.trackFile;
            }
        }
    }
    return out;
}

bool isComposition(const QString& path) {
    return path.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive) && rootName(path) == QLatin1String("CompositionPlaylist");
}

std::optional<Composition> readComposition(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, f.errorString());
        return std::nullopt;
    }
    QXmlStreamReader x(&f);
    if (!x.readNextStartElement() || x.name() != QLatin1String("CompositionPlaylist")) {
        if (error) *error = QStringLiteral("%1 is not a composition playlist").arg(path);
        return std::nullopt;
    }
    Composition c;
    c.file = QFileInfo(path).absoluteFilePath();
    QHash<QString, QString> v;
    // EditRate comes before the SegmentList (ST 2067-3's element order), so the resources can default to it.
    readElement(x, v, [&](QXmlStreamReader& r) {
        if (r.name() != QLatin1String("SegmentList")) {
            r.skipCurrentElement();
            return;
        }
        c.editRate = rateOf(v.value(QStringLiteral("EditRate")), c.editRate);
        while (r.readNextStartElement()) {
            if (r.name() != QLatin1String("Segment")) {
                r.skipCurrentElement();
                continue;
            }
            Segment seg;
            while (r.readNextStartElement()) {
                if (r.name() == QLatin1String("Id")) {
                    seg.id = uuidOf(r.readElementText());
                } else if (r.name() == QLatin1String("SequenceList")) {
                    while (r.readNextStartElement()) seg.sequences << readSequence(r, c.editRate);
                } else {
                    r.skipCurrentElement();
                }
            }
            c.segments << seg;
        }
    });
    if (x.hasError()) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, x.errorString());
        return std::nullopt;
    }
    c.id = uuidOf(v.value(QStringLiteral("Id")));
    c.contentTitle = v.value(QStringLiteral("ContentTitle"));
    c.annotation = v.value(QStringLiteral("Annotation"));
    c.issuer = v.value(QStringLiteral("Issuer"));
    c.creator = v.value(QStringLiteral("Creator"));
    c.issueDate = v.value(QStringLiteral("IssueDate"));
    c.contentKind = v.value(QStringLiteral("ContentKind"));
    c.editRate = rateOf(v.value(QStringLiteral("EditRate")), c.editRate);
    return c;
}

namespace {

std::optional<PackingList> readPackingList(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, f.errorString());
        return std::nullopt;
    }
    QXmlStreamReader x(&f);
    if (!x.readNextStartElement() || x.name() != QLatin1String("PackingList")) {
        if (error) *error = QStringLiteral("%1 is not a packing list").arg(path);
        return std::nullopt;
    }
    PackingList p;
    p.file = QFileInfo(path).absoluteFilePath();
    QHash<QString, QString> v;
    readElement(x, v, [&](QXmlStreamReader& r) {
        if (r.name() != QLatin1String("AssetList")) {
            r.skipCurrentElement();
            return;
        }
        while (r.readNextStartElement()) {
            QHash<QString, QString> a;
            readElement(r, a);
            PackingList::Entry e;
            e.id = uuidOf(a.value(QStringLiteral("Id")));
            e.type = a.value(QStringLiteral("Type"));
            e.hash = a.value(QStringLiteral("Hash"));
            e.size = a.value(QStringLiteral("Size")).toLongLong();
            e.originalFileName = a.value(QStringLiteral("OriginalFileName"));
            p.assets << e;
        }
    });
    if (x.hasError()) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, x.errorString());
        return std::nullopt;
    }
    p.id = uuidOf(v.value(QStringLiteral("Id")));
    p.annotation = v.value(QStringLiteral("Annotation"), v.value(QStringLiteral("AnnotationText")));
    p.issuer = v.value(QStringLiteral("Issuer"));
    p.creator = v.value(QStringLiteral("Creator"));
    return p;
}

} // namespace

std::optional<Package> readPackage(const QString& root, QString* error, QStringList* warnings) {
    const QDir dir(root);
    QString mapPath = dir.filePath(QStringLiteral("ASSETMAP.xml"));
    if (!QFileInfo::exists(mapPath)) mapPath = dir.filePath(QStringLiteral("ASSETMAP"));
    QFile f(mapPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("%1 has no ASSETMAP.xml").arg(QDir::toNativeSeparators(root));
        return std::nullopt;
    }
    QXmlStreamReader x(&f);
    if (!x.readNextStartElement() || x.name() != QLatin1String("AssetMap")) {
        if (error) *error = QStringLiteral("%1 is not an asset map").arg(mapPath);
        return std::nullopt;
    }
    Package p;
    p.root = QFileInfo(root).absoluteFilePath();
    p.name = QFileInfo(p.root).fileName();
    QList<QUuid> packingLists;
    QHash<QString, QString> v;
    readElement(x, v, [&](QXmlStreamReader& r) {
        if (r.name() != QLatin1String("AssetList")) {
            r.skipCurrentElement();
            return;
        }
        while (r.readNextStartElement()) {
            QUuid id;
            QString path;
            bool pkl = false;
            while (r.readNextStartElement()) {
                if (r.name() == QLatin1String("Id")) {
                    id = uuidOf(r.readElementText());
                } else if (r.name() == QLatin1String("PackingList")) {
                    pkl = r.readElementText().trimmed().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
                } else if (r.name() == QLatin1String("ChunkList")) {
                    while (r.readNextStartElement()) {
                        while (r.readNextStartElement()) {
                            if (r.name() == QLatin1String("Path") && path.isEmpty()) path = r.readElementText().trimmed();
                            else r.skipCurrentElement();
                        }
                    }
                } else {
                    r.skipCurrentElement();
                }
            }
            if (id.isNull() || path.isEmpty()) continue;
            p.assets.insert(id, QFileInfo(dir.filePath(path)).absoluteFilePath());
            if (pkl) packingLists << id;
        }
    });
    if (x.hasError()) {
        if (error) *error = QStringLiteral("%1: %2").arg(mapPath, x.errorString());
        return std::nullopt;
    }
    p.assetMapId = uuidOf(v.value(QStringLiteral("Id")));
    p.annotation = v.value(QStringLiteral("AnnotationText"), v.value(QStringLiteral("Annotation")));
    p.issuer = v.value(QStringLiteral("Issuer"));
    p.creator = v.value(QStringLiteral("Creator"));
    // The packing lists, and every composition they list.
    for (const QUuid& id : packingLists) {
        QString why;
        const auto pkl = readPackingList(p.assets.value(id), &why);
        if (!pkl) {
            if (warnings) *warnings << why;
            continue;
        }
        p.imps << *pkl;
        for (const PackingList::Entry& e : pkl->assets) {
            const QString path = p.assets.value(e.id);
            if (path.isEmpty() || !e.type.contains(QLatin1String("xml")) || !isComposition(path)) continue;
            if (std::any_of(p.compositions.begin(), p.compositions.end(), [&](const Composition& c) { return c.id == e.id; })) continue;
            const auto cpl = readComposition(path, &why);
            if (cpl) p.compositions << *cpl;
            else if (warnings) *warnings << why;
        }
    }
    return p;
}

bool Package::isSupplemental() const {
    return !missingTrackFiles().isEmpty();
}

QList<QUuid> Package::missingTrackFiles() const {
    QList<QUuid> out;
    for (const Composition& c : compositions) {
        for (const QUuid& id : c.trackFiles()) {
            if (!assets.contains(id) && !out.contains(id)) out << id;
        }
    }
    return out;
}

void Package::addAssets(const Package& other) {
    for (auto it = other.assets.cbegin(); it != other.assets.cend(); ++it) {
        if (!assets.contains(it.key())) assets.insert(it.key(), it.value());
    }
}

model::BinItem compositionSequence(const Composition& composition, const std::function<const model::BinItem*(const QUuid&)>& clipOf,
                                   const QString& binId,
                                   const QDateTime& when, QStringList* warnings) {
    auto warn = [&](const QString& text) {
        if (warnings && !warnings->contains(text)) *warnings << text;
    };
    const Rational rate = composition.editRate;
    // A resource's frames at the composition's rate: audio resources count samples at their own edit rate.
    auto toFrames = [&](std::int64_t units, const Rational& unitRate) {
        if (unitRate == rate || unitRate.num() <= 0) return units;
        return static_cast<std::int64_t>(std::llround(static_cast<double>(units) * rate.toDouble() / unitRate.toDouble()));
    };
    // Each audio virtual track (by TrackId) gets as many tracks as its clips have channels, from A1 on.
    QList<QUuid> audioTracks;
    QHash<QUuid, int> channels;
    for (const Segment& seg : composition.segments) {
        for (const Sequence& q : seg.sequences) {
            if (!q.isAudio()) continue;
            if (!audioTracks.contains(q.trackId)) audioTracks << q.trackId;
            for (const Resource& r : q.resources) {
                if (const model::BinItem* clip = clipOf(r.trackFile)) {
                    channels[q.trackId] = std::max(channels.value(q.trackId), model::audioChannelsOf(*clip));
                }
            }
        }
    }
    QHash<QUuid, int> firstChannel;
    int next = 1;
    for (const QUuid& t : audioTracks) {
        firstChannel.insert(t, next);
        next += std::max(channels.value(t), 1);
    }

    model::Sequence seq;
    seq.ensureTrack(QStringLiteral("V1"));
    for (int a = 1; a < next; ++a) seq.ensureTrack(QStringLiteral("A%1").arg(a));
    std::int64_t at = 0;
    for (const Segment& seg : composition.segments) {
        std::int64_t segmentLength = 0;
        for (const Sequence& q : seg.sequences) {
            if (!q.isImage() && !q.isAudio()) {
                warn(QStringLiteral("%1 virtual tracks left out").arg(q.kind));
                continue;
            }
            std::int64_t pos = at;
            for (const Resource& r : q.resources) {
                const model::BinItem* clip = clipOf(r.trackFile);
                const std::int64_t length = toFrames(r.sourceDuration, r.editRate);
                for (std::int64_t rep = 0; rep < std::max<std::int64_t>(r.repeatCount, 1); ++rep) {
                    if (length <= 0) break;
                    if (!clip) {
                        warn(QStringLiteral("track files that are not linked left as filler"));
                        pos += length;
                        continue;
                    }
                    const std::int64_t entry = toFrames(r.entryPoint, r.editRate);
                    const int tracks = q.isImage() ? 1 : std::max(model::audioChannelsOf(*clip), 1);
                    for (int k = 0; k < tracks; ++k) {
                        model::Segment s;
                        s.binId = binId;
                        s.clipId = clip->id;
                        s.sourceMob = model::mobIdOf(*clip);
                        s.name = clip->name;
                        s.sourceTrack = q.isImage() ? QStringLiteral("V1") : QStringLiteral("A%1").arg(k + 1);
                        s.sourceIn = entry;
                        s.length = length;
                        const QString track = q.isImage() ? QStringLiteral("V1") : QStringLiteral("A%1").arg(firstChannel.value(q.trackId) + k);
                        seq.overwrite(track, pos, {s});
                    }
                    pos += length;
                }
            }
            segmentLength = std::max(segmentLength, pos - at);
        }
        at += segmentLength;
    }
    model::BinItem item;
    item.id = model::newItemId();
    item.mobId = MobId::generate();
    item.kind = model::BinItemKind::Sequence;
    item.name = composition.contentTitle.isEmpty() ? QFileInfo(composition.file).completeBaseName() : composition.contentTitle;
    item.created = item.modified = when;
    item.editRate = rate;
    seq.storeIn(item);
    return item;
}

} // namespace lumenis::interop::imf
