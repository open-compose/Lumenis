#include "lumenis/model/structure.h"

#include "lumenis/model/project_bins.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>

namespace lumenis::model {

namespace {

constexpr auto kFormat = "cutthulhu-structure";
constexpr int kVersion = 1;

QString mobText(const MobId& mob) {
    return mob.isNull() ? QString() : QString::fromStdString(mob.toString());
}

QJsonObject segmentStructure(const Segment& s) {
    QJsonObject o;
    if (s.isFiller()) {
        o.insert(QStringLiteral("filler"), static_cast<qint64>(s.length));
    } else {
        o.insert(QStringLiteral("source"), mobText(s.sourceMobId()));
        o.insert(QStringLiteral("name"), s.name);
        o.insert(QStringLiteral("track"), s.sourceTrack);
        o.insert(QStringLiteral("in"), static_cast<qint64>(s.sourceIn));
        o.insert(QStringLiteral("length"), static_cast<qint64>(s.length));
    }
    if (s.effect) o.insert(QStringLiteral("effect"), s.effect->category + QLatin1Char('/') + s.effect->name);
    if (s.transition) {
        o.insert(QStringLiteral("transition"), s.transition->effect.category + QLatin1Char('/') + s.transition->effect.name);
    }
    return o;
}

QJsonObject itemStructure(const BinItem& item, const QStringList& custom) {
    QJsonObject o{
        {QStringLiteral("kind"), kindName(item.kind)},
        {QStringLiteral("name"), item.name},
        {QStringLiteral("mobId"), mobText(mobIdOf(item))},
        {QStringLiteral("editRate"), QString::fromStdString(item.editRate.toString())},
        {QStringLiteral("duration"), static_cast<qint64>(item.durationFrames)},
        {QStringLiteral("start"), item.fields.value(QStringLiteral("Start"))},
        {QStringLiteral("end"), item.fields.value(QStringLiteral("End"))},
    };
    if (item.fields.contains(QStringLiteral("Tracks"))) o.insert(QStringLiteral("tracks"), item.fields.value(QStringLiteral("Tracks")));
    // Custom columns (p.269) and the other headings, each by name.
    QJsonObject columns;
    QJsonObject fields;
    const QStringList& standard = binHeadings();
    for (auto it = item.fields.cbegin(); it != item.fields.cend(); ++it) {
        if (custom.contains(it.key()) || !standard.contains(it.key())) {
            columns.insert(it.key(), it.value());
        } else if (it.key() != QLatin1String("Start") && it.key() != QLatin1String("End") &&
                   it.key() != QLatin1String("Tracks")) {
            fields.insert(it.key(), it.value());
        }
    }
    if (!columns.isEmpty()) o.insert(QStringLiteral("columns"), columns);
    if (!fields.isEmpty()) o.insert(QStringLiteral("fields"), fields);
    if (!item.sources.isEmpty()) {
        QJsonArray sources;
        for (const ClipSource& s : item.sources) {
            QJsonObject so{{QStringLiteral("kind"), sourceKindName(s.kind)}, {QStringLiteral("mob"), mobText(s.mob)}};
            if (!s.track.isEmpty()) so.insert(QStringLiteral("track"), s.track);
            if (!s.name.isEmpty()) so.insert(QStringLiteral("name"), s.name);
            if (!s.startTimecode.isEmpty()) so.insert(QStringLiteral("start"), s.startTimecode);
            if (s.offset != 0) so.insert(QStringLiteral("offset"), static_cast<qint64>(s.offset));
            sources.append(so);
        }
        o.insert(QStringLiteral("sources"), sources);
    }
    if (item.kind == BinItemKind::Sequence) {
        QJsonArray tracks;
        const Sequence sequence = Sequence::fromItem(item);
        for (const Track& t : sequence.tracks()) {
            QJsonArray segments;
            for (const Segment& s : t.segments) segments.append(segmentStructure(s));
            tracks.append(QJsonObject{{QStringLiteral("name"), t.name()}, {QStringLiteral("segments"), segments}});
        }
        o.insert(QStringLiteral("sequence"), QJsonObject{{QStringLiteral("tracks"), tracks}});
    }
    return o;
}

QJsonObject document(const QJsonArray& bins) {
    return {{QStringLiteral("format"), QString::fromLatin1(kFormat)},
            {QStringLiteral("version"), kVersion},
            {QStringLiteral("bins"), bins}};
}

} // namespace

QJsonObject binStructure(const Bin& bin) {
    QJsonArray items;
    for (const BinItem& item : bin.items) items.append(itemStructure(item, bin.display.customColumns));
    QStringList custom = bin.display.customColumns;
    std::sort(custom.begin(), custom.end());
    return {{QStringLiteral("name"), bin.name},
            {QStringLiteral("customColumns"), QJsonArray::fromStringList(custom)},
            {QStringLiteral("items"), items}};
}

QJsonObject projectStructure(const QString& folder, const QString& name, QString* error) {
    ProjectBins project(folder, name);
    if (!project.load(error)) return {};
    QJsonArray bins;
    for (const BinNode* node : project.bins()) {
        const auto bin = BinFile::read(project.binPath(node->id), error);
        if (!bin) return {};
        QJsonObject b = binStructure(*bin);
        b.insert(QStringLiteral("name"), node->name);
        bins.append(b);
    }
    QJsonObject o = document(bins);
    o.insert(QStringLiteral("project"), name);
    return o;
}

QJsonObject structureOf(const QString& path, QString* error) {
    const QFileInfo info(path);
    if (info.isFile() && path.endsWith(BinFile::suffix())) {
        const auto bin = BinFile::read(path, error);
        if (!bin) return {};
        return document(QJsonArray{binStructure(*bin)});
    }
    QString projectFile = path;
    if (info.isDir()) {
        const QStringList found =
            QDir(path).entryList({QLatin1Char('*') + ProjectStore::fileSuffix()}, QDir::Files, QDir::Name);
        if (found.isEmpty()) {
            if (error) *error = QStringLiteral("no project in %1").arg(path);
            return {};
        }
        projectFile = QDir(path).filePath(found.first());
    }
    if (!projectFile.endsWith(ProjectStore::fileSuffix()) || !QFileInfo(projectFile).isFile()) {
        if (error) *error = QStringLiteral("%1 is not a bin or a project").arg(path);
        return {};
    }
    const QFileInfo project(projectFile);
    return projectStructure(project.absolutePath(), project.completeBaseName(), error);
}

} // namespace lumenis::model
