#include "lumenis/model/bin.h"

#include "lumenis/core/timecode.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace lumenis::model {

namespace {

constexpr int kFileVersion = 1;
constexpr auto kFileFormat = "cutthulhu-bin";

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

const std::pair<BinItemKind, const char*> kKinds[] = {
    {BinItemKind::MasterClip, "masterClip"},
    {BinItemKind::Subclip, "subclip"},
    {BinItemKind::AudioClip, "audioClip"},
    {BinItemKind::Sequence, "sequence"},
    {BinItemKind::Source, "source"},
    {BinItemKind::Effect, "effect"},
    {BinItemKind::MotionEffect, "motionEffect"},
    {BinItemKind::RenderedEffect, "renderedEffect"},
    {BinItemKind::Group, "group"},
    {BinItemKind::Title, "title"},
};

QString rateText(const Rational& r) {
    return QString::fromStdString(r.toString());
}

Rational parseRate(const QString& text) {
    const QStringList parts = text.split(QLatin1Char('/'));
    bool okNum = false;
    bool okDen = true;
    const qint64 num = parts.value(0).toLongLong(&okNum);
    const qint64 den = parts.size() > 1 ? parts.value(1).toLongLong(&okDen) : 1;
    if (!okNum || !okDen || num <= 0 || den <= 0) return Rational(24000, 1001);
    return Rational(num, den);
}

QJsonObject itemToJson(const BinItem& item) {
    QJsonObject fields;
    for (auto it = item.fields.cbegin(); it != item.fields.cend(); ++it) fields.insert(it.key(), it.value());
    QJsonObject o{
        {QStringLiteral("id"), item.id},
        {QStringLiteral("kind"), kindName(item.kind)},
        {QStringLiteral("name"), item.name},
        {QStringLiteral("created"), item.created.toUTC().toString(Qt::ISODate)},
        {QStringLiteral("modified"), item.modified.toUTC().toString(Qt::ISODate)},
        {QStringLiteral("duration"), static_cast<qint64>(item.durationFrames)},
        {QStringLiteral("editRate"), rateText(item.editRate)},
    };
    if (!item.color.isEmpty()) o.insert(QStringLiteral("color"), item.color);
    if (item.locked) o.insert(QStringLiteral("locked"), true);
    if (!fields.isEmpty()) o.insert(QStringLiteral("fields"), fields);
    return o;
}

std::optional<BinItem> itemFromJson(const QJsonObject& o) {
    BinItem item;
    item.id = o.value(QStringLiteral("id")).toString();
    const auto kind = kindFromName(o.value(QStringLiteral("kind")).toString());
    item.name = o.value(QStringLiteral("name")).toString();
    if (item.id.isEmpty() || !kind) return std::nullopt;
    item.kind = *kind;
    item.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
    item.modified = QDateTime::fromString(o.value(QStringLiteral("modified")).toString(), Qt::ISODate);
    item.durationFrames = o.value(QStringLiteral("duration")).toInteger();
    item.editRate = parseRate(o.value(QStringLiteral("editRate")).toString());
    item.color = o.value(QStringLiteral("color")).toString();
    item.locked = o.value(QStringLiteral("locked")).toBool();
    const QJsonObject fields = o.value(QStringLiteral("fields")).toObject();
    for (auto it = fields.begin(); it != fields.end(); ++it) item.fields.insert(it.key(), it.value().toString());
    return item;
}

QJsonArray toArray(const QStringList& list) {
    QJsonArray a;
    for (const QString& s : list) a.append(s);
    return a;
}

QStringList fromArray(const QJsonValue& v) {
    QStringList out;
    for (const QJsonValue& s : v.toArray()) out << s.toString();
    return out;
}

const char* modeName(BinViewMode m) {
    switch (m) {
    case BinViewMode::Text: return "text";
    case BinViewMode::Frame: return "frame";
    case BinViewMode::Script: return "script";
    }
    return "text";
}

// The highest NN in names "<base>.NN" (0 if none).
int highestNumber(const Bin& bin, const QString& base) {
    const QRegularExpression re(QStringLiteral("^%1\\.(\\d+)$").arg(QRegularExpression::escape(base)));
    int highest = 0;
    for (const BinItem& i : bin.items) {
        const auto m = re.match(i.name);
        if (m.hasMatch()) highest = std::max(highest, m.captured(1).toInt());
    }
    return highest;
}

} // namespace

QString kindName(BinItemKind kind) {
    for (const auto& [k, name] : kKinds) {
        if (k == kind) return QString::fromLatin1(name);
    }
    return {};
}

std::optional<BinItemKind> kindFromName(const QString& name) {
    for (const auto& [k, n] : kKinds) {
        if (name == QLatin1String(n)) return k;
    }
    return std::nullopt;
}

const BinItem* Bin::item(const QString& id) const {
    for (const BinItem& i : items) {
        if (i.id == id) return &i;
    }
    return nullptr;
}

BinItem* Bin::item(const QString& id) {
    for (BinItem& i : items) {
        if (i.id == id) return &i;
    }
    return nullptr;
}

QStringList Bin::defaultColumns() {
    // "Name, Duration, Drive, IN-OUT, Creation Date, TapeID, etc." (p.248).
    return {QStringLiteral("Creation Date"), QStringLiteral("Duration"), QStringLiteral("Drive"),
            QStringLiteral("IN-OUT"), QStringLiteral("TapeID")};
}

std::optional<Bin> BinFile::read(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        fail(error, f.errorString());
        return std::nullopt;
    }
    QJsonParseError parseError{};
    const QJsonObject o = QJsonDocument::fromJson(f.readAll(), &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        fail(error, parseError.errorString());
        return std::nullopt;
    }
    if (o.value(QStringLiteral("format")).toString() != QLatin1String(kFileFormat)) {
        fail(error, QStringLiteral("not a bin file"));
        return std::nullopt;
    }
    if (o.value(QStringLiteral("version")).toInt() > kFileVersion) {
        fail(error, QStringLiteral("bin was saved by a newer version"));
        return std::nullopt;
    }
    Bin bin;
    bin.name = o.value(QStringLiteral("name")).toString();
    for (const QJsonValue& v : o.value(QStringLiteral("items")).toArray()) {
        if (auto item = itemFromJson(v.toObject())) bin.items.append(*item);
    }
    const QJsonObject d = o.value(QStringLiteral("display")).toObject();
    const QString mode = d.value(QStringLiteral("mode")).toString();
    bin.display.mode = mode == QLatin1String("frame")    ? BinViewMode::Frame
                       : mode == QLatin1String("script") ? BinViewMode::Script
                                                         : BinViewMode::Text;
    bin.display.viewName = d.value(QStringLiteral("viewName")).toString();
    bin.display.columns =
        d.contains(QStringLiteral("columns")) ? fromArray(d.value(QStringLiteral("columns"))) : Bin::defaultColumns();
    bin.display.customColumns = fromArray(d.value(QStringLiteral("customColumns")));
    const QJsonObject widths = d.value(QStringLiteral("widths")).toObject();
    for (auto it = widths.begin(); it != widths.end(); ++it) bin.display.widths.insert(it.key(), it.value().toInt());
    bin.display.background = d.value(QStringLiteral("background")).toString();
    bin.display.frameScale = std::clamp(d.value(QStringLiteral("frameScale")).toInt(), -7, 7);
    bin.display.statusBar = d.value(QStringLiteral("statusBar")).toBool();
    return bin;
}

bool BinFile::write(const QString& path, const Bin& bin, QString* error) {
    QJsonArray items;
    for (const BinItem& i : bin.items) items.append(itemToJson(i));
    QJsonObject widths;
    for (auto it = bin.display.widths.cbegin(); it != bin.display.widths.cend(); ++it) {
        widths.insert(it.key(), it.value());
    }
    const QJsonObject display{
        {QStringLiteral("mode"), QLatin1String(modeName(bin.display.mode))},
        {QStringLiteral("viewName"), bin.display.viewName},
        {QStringLiteral("columns"), toArray(bin.display.columns)},
        {QStringLiteral("customColumns"), toArray(bin.display.customColumns)},
        {QStringLiteral("widths"), widths},
        {QStringLiteral("background"), bin.display.background},
        {QStringLiteral("frameScale"), bin.display.frameScale},
        {QStringLiteral("statusBar"), bin.display.statusBar},
    };
    const QJsonObject o{
        {QStringLiteral("format"), QLatin1String(kFileFormat)},
        {QStringLiteral("version"), kFileVersion},
        {QStringLiteral("name"), bin.name},
        {QStringLiteral("items"), items},
        {QStringLiteral("display"), display},
    };
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return fail(error, f.errorString());
    f.write(QJsonDocument(o).toJson());
    if (!f.commit()) return fail(error, f.errorString());
    return true;
}

QString newItemId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

BinItem newSequence(const Bin& bin, const Rational& editRate, const QDateTime& when) {
    const QString base = QStringLiteral("Untitled Sequence");
    BinItem s;
    s.id = newItemId();
    s.kind = BinItemKind::Sequence;
    s.name = QStringLiteral("%1.%2").arg(base).arg(highestNumber(bin, base) + 1, 2, 10, QLatin1Char('0'));
    s.created = when;
    s.modified = when;
    s.editRate = editRate;
    return s;
}

BinItem duplicateItem(const Bin& bin, const BinItem& original, const QDateTime& when) {
    BinItem copy = original;
    copy.id = newItemId();
    const QString base = original.name + QStringLiteral(".Copy");
    copy.name = QStringLiteral("%1.%2").arg(base).arg(highestNumber(bin, base) + 1, 2, 10, QLatin1Char('0'));
    copy.created = when;
    copy.modified = when;
    copy.locked = false;
    return copy;
}

QString durationText(std::int64_t frames, const Rational& editRate) {
    const TimecodeFormat format = TimecodeFormat::forRate(editRate, false);
    const TimecodeFields f = Timecode(std::max<std::int64_t>(frames, 0), format).fields();
    auto two = [](int v) { return QStringLiteral("%1").arg(v, 2, 10, QLatin1Char('0')); };
    if (f.hours > 0) return QStringLiteral("%1:%2:%3:%4").arg(f.hours).arg(two(f.minutes), two(f.seconds), two(f.frames));
    if (f.minutes > 0) return QStringLiteral("%1:%2:%3").arg(f.minutes).arg(two(f.seconds), two(f.frames));
    return QStringLiteral("%1:%2").arg(f.seconds).arg(two(f.frames));
}

} // namespace lumenis::model
