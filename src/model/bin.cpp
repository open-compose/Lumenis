#include "lumenis/model/bin.h"

#include "lumenis/core/timecode.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
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
    if (!item.script.isEmpty()) o.insert(QStringLiteral("script"), item.script);
    if (!fields.isEmpty()) o.insert(QStringLiteral("fields"), fields);
    if (!item.composition.isEmpty()) o.insert(QStringLiteral("composition"), item.composition);
    if (!item.markers.isEmpty()) o.insert(QStringLiteral("markers"), item.markers);
    if (!item.link.isEmpty()) o.insert(QStringLiteral("link"), item.link);
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
    item.script = o.value(QStringLiteral("script")).toString();
    const QJsonObject fields = o.value(QStringLiteral("fields")).toObject();
    for (auto it = fields.begin(); it != fields.end(); ++it) item.fields.insert(it.key(), it.value().toString());
    item.composition = o.value(QStringLiteral("composition")).toObject();
    item.markers = o.value(QStringLiteral("markers")).toArray();
    item.link = o.value(QStringLiteral("link")).toObject();
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

const char* snapName(SnapToGrid s) {
    switch (s) {
    case SnapToGrid::Disable: return "disable";
    case SnapToGrid::Temporary: return "temporary";
    case SnapToGrid::Enable: return "enable";
    case SnapToGrid::Invisible: return "invisible";
    }
    return "disable";
}

SnapToGrid snapFromName(const QString& name) {
    for (SnapToGrid s : {SnapToGrid::Temporary, SnapToGrid::Enable, SnapToGrid::Invisible}) {
        if (name == QLatin1String(snapName(s))) return s;
    }
    return SnapToGrid::Disable;
}

QJsonArray siftRowsToJson(const QList<SiftRow>& rows) {
    QJsonArray a;
    for (const SiftRow& r : rows) a.append(QJsonObject{{QStringLiteral("text"), r.text}, {QStringLiteral("column"), r.column}});
    return a;
}

QList<SiftRow> siftRowsFromJson(const QJsonValue& v) {
    QList<SiftRow> rows;
    for (const QJsonValue& r : v.toArray()) {
        const QJsonObject o = r.toObject();
        rows.append({o.value(QStringLiteral("text")).toString(), o.value(QStringLiteral("column")).toString()});
    }
    return rows;
}

QString dateText(const QDateTime& when) {
    // As in the guide's pictures: 7/23/2020 12:52:49 PM.
    return when.isValid() ? QLocale::c().toString(when.toLocalTime(), QStringLiteral("M/d/yyyy h:mm:ss AP")) : QString();
}

// Natural order: runs of digits compare as numbers, the rest ignoring case.
int naturalCompare(const QString& a, const QString& b) {
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i).isDigit() && b.at(j).isDigit()) {
            qsizetype ei = i;
            qsizetype ej = j;
            while (ei < a.size() && a.at(ei).isDigit()) ++ei;
            while (ej < b.size() && b.at(ej).isDigit()) ++ej;
            const QString na = a.mid(i, ei - i);
            const QString nb = b.mid(j, ej - j);
            // Compare without leading zeros: by length, then digit by digit.
            auto strip = [](const QString& n) {
                qsizetype k = 0;
                while (k + 1 < n.size() && n.at(k) == QLatin1Char('0')) ++k;
                return n.mid(k);
            };
            const QString sa = strip(na);
            const QString sb = strip(nb);
            if (sa.size() != sb.size()) return sa.size() < sb.size() ? -1 : 1;
            if (const int c = sa.compare(sb); c != 0) return c < 0 ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        const QChar ca = a.at(i).toCaseFolded();
        const QChar cb = b.at(j).toCaseFolded();
        if (ca != cb) return ca < cb ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

// Hue (0-360, -1 for greys), saturation and value (0-1) of "#rrggbb".
struct Hsv {
    double h = -1;
    double s = 0;
    double v = 0;
};

Hsv hsvOf(const QString& hex) {
    bool ok = false;
    const uint rgb = QStringView(hex).mid(1).toUInt(&ok, 16);
    if (!ok || hex.size() != 7) return {};
    const double r = ((rgb >> 16) & 0xff) / 255.0;
    const double g = ((rgb >> 8) & 0xff) / 255.0;
    const double b = (rgb & 0xff) / 255.0;
    const double mx = std::max({r, g, b});
    const double mn = std::min({r, g, b});
    const double d = mx - mn;
    Hsv out;
    out.v = mx;
    out.s = mx > 0 ? d / mx : 0;
    if (d > 0) {
        double h = mx == r ? (g - b) / d : mx == g ? 2 + (b - r) / d : 4 + (r - g) / d;
        h *= 60;
        out.h = h < 0 ? h + 360 : h;
    }
    return out;
}

template <typename T> int threeWay(const T& a, const T& b) {
    return a < b ? -1 : (b < a ? 1 : 0);
}

bool rangeMatches(const BinItem& item, const SiftRow& row) {
    for (const SiftRange& r : siftRanges()) {
        if (r.name != row.column) continue;
        const auto value = timecodeFrames(row.text, item.editRate);
        const auto start = timecodeFrames(cellText(item, r.start), item.editRate);
        if (!value || !start) return false;
        std::int64_t end = 0;
        if (r.end.isEmpty()) {
            end = *start + std::max<std::int64_t>(item.durationFrames - 1, 0);
        } else {
            const auto e = timecodeFrames(cellText(item, r.end), item.editRate);
            if (!e) return false;
            end = *e;
        }
        // "clips that encompass the timecode" (p.291).
        return *value >= *start && *value <= end;
    }
    return false;
}

bool rowMatches(const BinItem& item, const SiftRow& row, const QStringList& columns) {
    if (row.column.endsWith(QLatin1String(" Range"))) return rangeMatches(item, row);
    const QStringList searched = row.column.isEmpty() ? columns : QStringList{row.column};
    for (const QString& heading : searched) {
        // Colours are found by their exact name (p.289).
        if (heading == QLatin1String("Color")) {
            if (clipColorName(item.color) == row.text) return true;
        } else if (cellText(item, heading).contains(row.text, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

// A group of rows matches when it has criteria and the item meets them all.
bool groupMatches(const BinItem& item, const QList<SiftRow>& rows, const QStringList& columns) {
    bool any = false;
    for (const SiftRow& r : rows) {
        if (r.text.isEmpty()) continue;
        any = true;
        if (!rowMatches(item, r, columns)) return false;
    }
    return any;
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

bool BinSift::isEmpty() const {
    if (!ids.isEmpty()) return false;
    auto hasText = [](const QList<SiftRow>& rows) {
        return std::any_of(rows.begin(), rows.end(), [](const SiftRow& r) { return !r.text.isEmpty(); });
    };
    return !hasText(find) && !hasText(also);
}

const QStringList& binHeadings() {
    // The table "Bin Column Heading" on p.273-278, in its order.
    static const QStringList headings = [] {
        QStringList h{
            QStringLiteral("Name"), QStringLiteral("AFD"), QStringLiteral("ASC_SAT"), QStringLiteral("ASC_SOP"),
            QStringLiteral("Audio Bit Depth"), QStringLiteral("Audio Format"), QStringLiteral("Audio SR"),
            QStringLiteral("Aux TC 24"), QStringLiteral("Auxiliary Ink")};
        for (int i = 1; i <= 5; ++i) h << QStringLiteral("Auxiliary TC%1").arg(i);
        h << QStringList{
            QStringLiteral("AuxInk Dur"), QStringLiteral("AuxInk Edge"), QStringLiteral("AuxInk End"),
            QStringLiteral("AuxInk Film"), QStringLiteral("Cadence"), QStringLiteral("Camera"),
            QStringLiteral("Camroll"), QStringLiteral("CFPS"), QStringLiteral("Color"),
            QStringLiteral("Color Framing"), QStringLiteral("Color Space"), QStringLiteral("Comments"),
            QStringLiteral("Creation Date"), QStringLiteral("Disk Label"), QStringLiteral("DPX"),
            QStringLiteral("Drive"), QStringLiteral("Duration"), QStringLiteral("End"),
            QStringLiteral("Field Motion"), QStringLiteral("Film TC"), QStringLiteral("Film Type"),
            QStringLiteral("Format"), QStringLiteral("FPS"), QStringLiteral("Frame"),
            QStringLiteral("Frame Count Duration"), QStringLiteral("Frame Count End"),
            QStringLiteral("Frame Count Start"), QStringLiteral("IData-Link"), QStringLiteral("Image Aspect Ratio"),
            QStringLiteral("IN-OUT"), QStringLiteral("Ink Dur"), QStringLiteral("Ink Edge"),
            QStringLiteral("Ink End"), QStringLiteral("Ink Film"), QStringLiteral("Ink Number"),
            QStringLiteral("Journalist"), QStringLiteral("KN Dur"), QStringLiteral("KN End"),
            QStringLiteral("KN Film"), QStringLiteral("KN IN-OUT"), QStringLiteral("KN Mark IN"),
            QStringLiteral("KN Mark OUT"), QStringLiteral("KN Start"), QStringLiteral("Labroll"),
            QStringLiteral("Lock"), QStringLiteral("LUT"), QStringLiteral("Mark IN"), QStringLiteral("Mark OUT"),
            QStringLiteral("Marker"), QStringLiteral("Master Dur"), QStringLiteral("Master Edge"),
            QStringLiteral("Master End"), QStringLiteral("Master Film"), QStringLiteral("Master Start"),
            QStringLiteral("Media File Path"), QStringLiteral("Media Status"), QStringLiteral("Modified Date"),
            QStringLiteral("Offline"), QStringLiteral("Perf"), QStringLiteral("Pixel Aspect Ratio"),
            QStringLiteral("Plug-In"), QStringLiteral("Production"), QStringLiteral("Project"),
            QStringLiteral("Pullin"), QStringLiteral("Pullout"), QStringLiteral("Reel"),
            QStringLiteral("Reformatting Options"), QStringLiteral("Scene"), QStringLiteral("Shoot date"),
            QStringLiteral("Slip"), QStringLiteral("Sound TC"), QStringLiteral("Soundroll"),
            QStringLiteral("Source File"), QStringLiteral("Source Path"), QStringLiteral("Start"),
            QStringLiteral("Take"), QStringLiteral("Tape"), QStringLiteral("TapeID"), QStringLiteral("TC 24"),
            QStringLiteral("TC 25"), QStringLiteral("TC 25PD"), QStringLiteral("TC 30"), QStringLiteral("TC 30NP"),
            QStringLiteral("TC 60"), QStringLiteral("Track Formats"), QStringLiteral("Tracks"),
            QStringLiteral("Transfer"), QStringLiteral("VFX"), QStringLiteral("VFX Reel"), QStringLiteral("Video"),
            QStringLiteral("Vendor"), QStringLiteral("Video Data Rate"), QStringLiteral("Video File Format"),
            QStringLiteral("VITC")};
        return h;
    }();
    return headings;
}

bool isEditableHeading(const QString& heading) {
    static const QStringList editable = [] {
        QStringList e{QStringLiteral("Name"),       QStringLiteral("Comments"),   QStringLiteral("Journalist"),
                      QStringLiteral("Production"), QStringLiteral("Shoot date"), QStringLiteral("Start"),
                      QStringLiteral("Take"),       QStringLiteral("TapeID"),     QStringLiteral("Frame Count Start"),
                      QStringLiteral("TC 24"),      QStringLiteral("TC 25"),      QStringLiteral("TC 25PD"),
                      QStringLiteral("TC 30"),      QStringLiteral("TC 30NP"),    QStringLiteral("TC 60")};
        for (int i = 1; i <= 5; ++i) e << QStringLiteral("Auxiliary TC%1").arg(i);
        return e;
    }();
    return editable.contains(heading);
}

const QList<BinViewPreset>& builtInBinViews() {
    // p.248 describes each view; the headings are our reading of it.
    static const QList<BinViewPreset> views{
        {QStringLiteral("Basic"), Bin::defaultColumns()},
        {QStringLiteral("Capture"),
         {QStringLiteral("Start"), QStringLiteral("End"), QStringLiteral("Tape"), QStringLiteral("Tracks"),
          QStringLiteral("Video")}},
        {QStringLiteral("Custom"), {}},
        {QStringLiteral("Film"),
         {QStringLiteral("KN Start"), QStringLiteral("KN End"), QStringLiteral("Ink Number"), QStringLiteral("Pullin")}},
        {QStringLiteral("Format"), {QStringLiteral("Format"), QStringLiteral("Video"), QStringLiteral("Project")}},
        // The Media tool (later) will supply its own headings.
        {QStringLiteral("Media Tool"), Bin::defaultColumns()},
        {QStringLiteral("Statistics"),
         {QStringLiteral("Start"), QStringLiteral("End"), QStringLiteral("Duration"), QStringLiteral("Video")}},
    };
    return views;
}

QString cellText(const BinItem& item, const QString& h) {
    if (h == QLatin1String("Name")) return item.name;
    if (h == QLatin1String("Creation Date")) return dateText(item.created);
    if (h == QLatin1String("Modified Date")) return dateText(item.modified);
    if (h == QLatin1String("Duration")) return durationText(item.durationFrames, item.editRate);
    if (h == QLatin1String("Lock")) return item.locked ? QStringLiteral("Locked") : QString();
    if (h == QLatin1String("Color")) return clipColorName(item.color);
    return item.fields.value(h);
}

int compareCells(const BinItem& a, const BinItem& b, const QString& h) {
    if (h == QLatin1String("Duration")) return threeWay(a.durationFrames, b.durationFrames);
    if (h == QLatin1String("Creation Date")) return threeWay(a.created, b.created);
    if (h == QLatin1String("Modified Date")) return threeWay(a.modified, b.modified);
    if (h == QLatin1String("Lock")) return threeWay(a.locked, b.locked);
    if (h == QLatin1String("Color")) {
        // No colour first, then by hue, saturation and value (p.248).
        if (a.color.isEmpty() || b.color.isEmpty()) return threeWay(!a.color.isEmpty(), !b.color.isEmpty());
        const Hsv x = hsvOf(a.color);
        const Hsv y = hsvOf(b.color);
        if (const int c = threeWay(x.h, y.h); c != 0) return c;
        if (const int c = threeWay(x.s, y.s); c != 0) return c;
        return threeWay(x.v, y.v);
    }
    return naturalCompare(cellText(a, h), cellText(b, h));
}

void sortItems(Bin& bin, const QList<BinSortKey>& keys) {
    if (keys.isEmpty()) return;
    std::stable_sort(bin.items.begin(), bin.items.end(), [&](const BinItem& a, const BinItem& b) {
        for (const BinSortKey& k : keys) {
            const int c = compareCells(a, b, k.heading);
            if (c != 0) return k.ascending ? c < 0 : c > 0;
        }
        return false;
    });
}

bool siftMatches(const BinItem& item, const BinSift& sift, const QStringList& columns) {
    if (!sift.ids.isEmpty()) return sift.ids.contains(item.id);
    return groupMatches(item, sift.find, columns) || groupMatches(item, sift.also, columns);
}

const QList<SiftRange>& siftRanges() {
    // The tables on p.290, without keycode and ink number ranges.
    static const QList<SiftRange> ranges = [] {
        QList<SiftRange> r{
            {QStringLiteral("Start to End Range"), QStringLiteral("Start"), QStringLiteral("End")},
            {QStringLiteral("Mark In to Out Range"), QStringLiteral("Mark IN"), QStringLiteral("Mark OUT")},
            {QStringLiteral("Film TC Range"), QStringLiteral("Film TC"), {}},
            {QStringLiteral("Sound TC Range"), QStringLiteral("Sound TC"), {}},
        };
        for (int i = 1; i <= 5; ++i) {
            r.append({QStringLiteral("Auxiliary TC%1 Range").arg(i), QStringLiteral("Auxiliary TC%1").arg(i), {}});
        }
        return r;
    }();
    return ranges;
}

QStringList siftRangesFor(const QStringList& columns) {
    QStringList out;
    for (const SiftRange& r : siftRanges()) {
        if (columns.contains(r.start) && (r.end.isEmpty() || columns.contains(r.end))) out << r.name;
    }
    return out;
}

std::optional<std::int64_t> timecodeFrames(const QString& text, const Rational& editRate) {
    // "you do not need to enter colons or semicolons, and you can omit the
    // leading zero" (p.290).
    QString digits;
    for (const QChar c : text) {
        if (c.isDigit()) {
            digits += c;
        } else if (!QStringLiteral(":;. ").contains(c)) {
            return std::nullopt;
        }
    }
    if (digits.isEmpty() || digits.size() > 8) return std::nullopt;
    digits = digits.rightJustified(8, QLatin1Char('0'));
    TimecodeFields f;
    f.hours = QStringView(digits).mid(0, 2).toInt();
    f.minutes = QStringView(digits).mid(2, 2).toInt();
    f.seconds = QStringView(digits).mid(4, 2).toInt();
    f.frames = QStringView(digits).mid(6, 2).toInt();
    const TimecodeFormat format = TimecodeFormat::forRate(editRate, text.contains(QLatin1Char(';')));
    if (f.minutes > 59 || f.seconds > 59 || f.frames >= format.nominalFps) return std::nullopt;
    return Timecode::fromFields(f, format).frameCount();
}

QString kindLabel(BinItemKind kind) {
    switch (kind) {
    case BinItemKind::MasterClip: return QStringLiteral("Master Clips");
    case BinItemKind::Subclip: return QStringLiteral("Subclips");
    case BinItemKind::AudioClip: return QStringLiteral("Audio Clips");
    case BinItemKind::Sequence: return QStringLiteral("Sequences");
    case BinItemKind::Source: return QStringLiteral("Sources");
    case BinItemKind::Effect: return QStringLiteral("Effects");
    case BinItemKind::MotionEffect: return QStringLiteral("Motion Effects");
    case BinItemKind::RenderedEffect: return QStringLiteral("Rendered Effects");
    case BinItemKind::Group: return QStringLiteral("Groups");
    case BinItemKind::Title: return QStringLiteral("Titles");
    }
    return {};
}

const QList<BinItemKind>& allKinds() {
    static const QList<BinItemKind> kinds{
        BinItemKind::MasterClip, BinItemKind::Subclip, BinItemKind::AudioClip,      BinItemKind::Sequence,
        BinItemKind::Source,     BinItemKind::Effect,  BinItemKind::MotionEffect,   BinItemKind::RenderedEffect,
        BinItemKind::Group,      BinItemKind::Title};
    return kinds;
}

const QList<ClipColor>& clipColors() {
    static const QList<ClipColor> colors{
        {QStringLiteral("Red"), QStringLiteral("#c83232")},     {QStringLiteral("Orange"), QStringLiteral("#d27828")},
        {QStringLiteral("Yellow"), QStringLiteral("#d2c832")},  {QStringLiteral("Lime"), QStringLiteral("#8cc83c")},
        {QStringLiteral("Green"), QStringLiteral("#32a046")},   {QStringLiteral("Teal"), QStringLiteral("#2ca096")},
        {QStringLiteral("Cyan"), QStringLiteral("#3cbed2")},    {QStringLiteral("Blue"), QStringLiteral("#3264c8")},
        {QStringLiteral("Violet"), QStringLiteral("#7846c8")},  {QStringLiteral("Magenta"), QStringLiteral("#be3cb4")},
        {QStringLiteral("Pink"), QStringLiteral("#e68cb4")},    {QStringLiteral("Brown"), QStringLiteral("#8c5a32")},
        {QStringLiteral("Gray"), QStringLiteral("#8c8c8c")},    {QStringLiteral("White"), QStringLiteral("#e6e6e6")},
    };
    return colors;
}

QString clipColorName(const QString& hex) {
    if (hex.isEmpty()) return {};
    for (const ClipColor& c : clipColors()) {
        if (c.hex.compare(hex, Qt::CaseInsensitive) == 0) return c.name;
    }
    return hex;
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
    bin.display.viewModified = d.value(QStringLiteral("viewModified")).toBool();
    bin.display.frameSize = std::clamp(d.value(QStringLiteral("frameSize")).toInt(BinDisplay::kDefaultFrameSize), 0,
                                       BinDisplay::kMaxFrameSize);
    const QJsonObject positions = d.value(QStringLiteral("framePositions")).toObject();
    for (auto it = positions.begin(); it != positions.end(); ++it) {
        const QJsonArray xy = it.value().toArray();
        bin.display.framePositions.insert(it.key(), QPoint(xy.at(0).toInt(), xy.at(1).toInt()));
    }
    bin.display.snap = snapFromName(d.value(QStringLiteral("snapToGrid")).toString());
    for (const QJsonValue& k : d.value(QStringLiteral("sort")).toArray()) {
        const QJsonObject ko = k.toObject();
        bin.display.sort.append({ko.value(QStringLiteral("heading")).toString(),
                                 ko.value(QStringLiteral("ascending")).toBool(true)});
    }
    const QJsonObject sift = d.value(QStringLiteral("sift")).toObject();
    bin.display.sift.find = siftRowsFromJson(sift.value(QStringLiteral("find")));
    bin.display.sift.also = siftRowsFromJson(sift.value(QStringLiteral("also")));
    bin.display.sift.ids = fromArray(sift.value(QStringLiteral("ids")));
    bin.display.sifted = d.value(QStringLiteral("sifted")).toBool();
    if (d.contains(QStringLiteral("hiddenKinds"))) {
        bin.display.hiddenKinds.clear();
        for (const QJsonValue& k : d.value(QStringLiteral("hiddenKinds")).toArray()) {
            if (const auto kind = kindFromName(k.toString())) bin.display.hiddenKinds << *kind;
        }
    }
    bin.display.showUserClips = d.value(QStringLiteral("showUserClips")).toBool(true);
    bin.display.showReferenceClips = d.value(QStringLiteral("showReferenceClips")).toBool();
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
    QJsonObject positions;
    for (auto it = bin.display.framePositions.cbegin(); it != bin.display.framePositions.cend(); ++it) {
        positions.insert(it.key(), QJsonArray{it.value().x(), it.value().y()});
    }
    QJsonArray sort;
    for (const BinSortKey& k : bin.display.sort) {
        sort.append(QJsonObject{{QStringLiteral("heading"), k.heading}, {QStringLiteral("ascending"), k.ascending}});
    }
    QJsonArray hidden;
    for (BinItemKind k : bin.display.hiddenKinds) hidden.append(kindName(k));
    const QJsonObject sift{
        {QStringLiteral("find"), siftRowsToJson(bin.display.sift.find)},
        {QStringLiteral("also"), siftRowsToJson(bin.display.sift.also)},
        {QStringLiteral("ids"), toArray(bin.display.sift.ids)},
    };
    const QJsonObject display{
        {QStringLiteral("mode"), QLatin1String(modeName(bin.display.mode))},
        {QStringLiteral("viewName"), bin.display.viewName},
        {QStringLiteral("viewModified"), bin.display.viewModified},
        {QStringLiteral("columns"), toArray(bin.display.columns)},
        {QStringLiteral("customColumns"), toArray(bin.display.customColumns)},
        {QStringLiteral("widths"), widths},
        {QStringLiteral("background"), bin.display.background},
        {QStringLiteral("frameSize"), bin.display.frameSize},
        {QStringLiteral("framePositions"), positions},
        {QStringLiteral("snapToGrid"), QLatin1String(snapName(bin.display.snap))},
        {QStringLiteral("sort"), sort},
        {QStringLiteral("sift"), sift},
        {QStringLiteral("sifted"), bin.display.sifted},
        {QStringLiteral("hiddenKinds"), hidden},
        {QStringLiteral("showUserClips"), bin.display.showUserClips},
        {QStringLiteral("showReferenceClips"), bin.display.showReferenceClips},
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

BinItem newSequence(const Bin& bin, const Rational& editRate, const QDateTime& when, const QString& start) {
    const QString base = QStringLiteral("Untitled Sequence");
    BinItem s;
    s.id = newItemId();
    s.kind = BinItemKind::Sequence;
    s.name = QStringLiteral("%1.%2").arg(base).arg(highestNumber(bin, base) + 1, 2, 10, QLatin1Char('0'));
    s.created = when;
    s.modified = when;
    s.editRate = editRate;
    const TimecodeFormat format = TimecodeFormat::forRate(editRate, false);
    const auto given = Timecode::parse(start.trimmed().toStdString(), format);
    const Timecode tc = given ? *given : Timecode(kDefaultStartHours * 3600 * format.nominalFps, format);
    s.fields.insert(QStringLiteral("Start"), QString::fromStdString(tc.toString()));
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
