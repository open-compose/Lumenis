// Scripts (lumenis/model/script.h; Editing Guide ch.18).
#include "lumenis/model/script.h"

#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace lumenis::model {

namespace {

QString atOrBefore(const QMap<int, QString>& marks, int line) {
    auto it = marks.upperBound(line);
    if (it == marks.begin()) return {};
    return std::prev(it).value();
}

QJsonObject marksToJson(const QMap<int, QString>& marks) {
    QJsonObject o;
    for (auto it = marks.cbegin(); it != marks.cend(); ++it) o.insert(QString::number(it.key()), it.value());
    return o;
}

QMap<int, QString> marksFromJson(const QJsonObject& o, int lines) {
    QMap<int, QString> out;
    for (auto it = o.begin(); it != o.end(); ++it) {
        bool ok = false;
        const int line = it.key().toInt(&ok);
        if (ok && line >= 0 && line < lines && !it.value().toString().isEmpty()) out.insert(line, it.value().toString());
    }
    return out;
}

int deleteIn(QMap<int, QString>& marks, int from, int to) {
    int n = 0;
    for (auto it = marks.begin(); it != marks.end();) {
        if (it.key() >= from && it.key() <= to) {
            it = marks.erase(it);
            ++n;
        } else {
            ++it;
        }
    }
    return n;
}

void shiftMarks(QMap<int, QString>& marks, int from, int to, int newCount) {
    QMap<int, QString> out;
    const int delta = newCount - (to - from);
    for (auto it = marks.cbegin(); it != marks.cend(); ++it) {
        if (it.key() < from) {
            out.insert(it.key(), it.value());
        } else if (it.key() < to) {
            if (newCount > 0 && !out.contains(from)) out.insert(from, it.value());
        } else {
            out.insert(it.key() + delta, it.value());
        }
    }
    marks = out;
}

} // namespace

QString Script::sceneAt(int line) const { return atOrBefore(scenes, line); }
QString Script::pageAt(int line) const { return atOrBefore(pages, line); }

QJsonObject Script::toJson() const {
    QJsonObject o{{QStringLiteral("lines"), QJsonArray::fromStringList(lines)},
                  {QStringLiteral("leftMargin"), layout.leftMargin},
                  {QStringLiteral("textWidth"), layout.textWidth},
                  {QStringLiteral("wordWrap"), layout.wordWrap},
                  {QStringLiteral("fontSize"), layout.fontSize}};
    if (!layout.font.isEmpty()) o.insert(QStringLiteral("font"), layout.font);
    if (!scenes.isEmpty()) o.insert(QStringLiteral("scenes"), marksToJson(scenes));
    if (!pages.isEmpty()) o.insert(QStringLiteral("pages"), marksToJson(pages));
    if (!slates.isEmpty()) {
        auto ranges = [](const QList<ScriptRange>& list) {
            QJsonArray a;
            for (const ScriptRange& r : list) {
                QJsonObject ro{{QStringLiteral("first"), r.first}, {QStringLiteral("last"), r.last}};
                if (!r.color.isEmpty()) ro.insert(QStringLiteral("color"), r.color);
                a.append(ro);
            }
            return a;
        };
        QJsonArray a;
        for (const ScriptSlate& s : slates) {
            QJsonArray takes;
            for (const ScriptTake& t : s.takes) {
                QJsonObject to{{QStringLiteral("id"), t.id},
                               {QStringLiteral("binId"), t.binId},
                               {QStringLiteral("clipId"), t.clipId},
                               {QStringLiteral("clipName"), t.clipName},
                               {QStringLiteral("first"), t.first},
                               {QStringLiteral("last"), t.last},
                               {QStringLiteral("frame"), static_cast<qint64>(t.frame)}};
                if (!t.take.isEmpty()) to.insert(QStringLiteral("take"), t.take);
                if (!t.offscreen.isEmpty()) to.insert(QStringLiteral("offscreen"), ranges(t.offscreen));
                if (!t.colors.isEmpty()) to.insert(QStringLiteral("colors"), ranges(t.colors));
                if (!t.marks.isEmpty()) {
                    QJsonObject marks;
                    for (auto it = t.marks.cbegin(); it != t.marks.cend(); ++it)
                        marks.insert(QString::number(it.key()), static_cast<qint64>(it.value()));
                    to.insert(QStringLiteral("marks"), marks);
                }
                takes.append(to);
            }
            a.append(QJsonObject{{QStringLiteral("id"), s.id},
                                 {QStringLiteral("line"), s.line},
                                 {QStringLiteral("x"), s.x},
                                 {QStringLiteral("active"), s.active},
                                 {QStringLiteral("takes"), takes}});
        }
        o.insert(QStringLiteral("slates"), a);
    }
    return o;
}

Script Script::fromJson(const QJsonObject& o) {
    Script s;
    for (const QJsonValue& v : o.value(QStringLiteral("lines")).toArray()) s.lines << v.toString();
    const auto count = static_cast<int>(s.lines.size());
    s.scenes = marksFromJson(o.value(QStringLiteral("scenes")).toObject(), count);
    s.pages = marksFromJson(o.value(QStringLiteral("pages")).toObject(), count);
    s.layout.leftMargin = std::max(0, o.value(QStringLiteral("leftMargin")).toInt(40));
    s.layout.textWidth = std::max(50, o.value(QStringLiteral("textWidth")).toInt(600));
    s.layout.wordWrap = o.value(QStringLiteral("wordWrap")).toBool(true);
    s.layout.font = o.value(QStringLiteral("font")).toString();
    s.layout.fontSize = std::clamp(o.value(QStringLiteral("fontSize")).toInt(12), 4, 144);
    const int last = std::max(0, count - 1);
    auto ranges = [last](const QJsonValue& v) {
        QList<ScriptRange> out;
        for (const QJsonValue& r : v.toArray()) {
            const QJsonObject ro = r.toObject();
            ScriptRange range{std::clamp(ro.value(QStringLiteral("first")).toInt(), 0, last),
                              std::clamp(ro.value(QStringLiteral("last")).toInt(), 0, last), ro.value(QStringLiteral("color")).toString()};
            if (range.last >= range.first) out << range;
        }
        return out;
    };
    for (const QJsonValue& v : o.value(QStringLiteral("slates")).toArray()) {
        const QJsonObject so = v.toObject();
        ScriptSlate slate;
        slate.id = so.value(QStringLiteral("id")).toString();
        slate.line = std::clamp(so.value(QStringLiteral("line")).toInt(), 0, last);
        slate.x = so.value(QStringLiteral("x")).toInt();
        for (const QJsonValue& tv : so.value(QStringLiteral("takes")).toArray()) {
            const QJsonObject to = tv.toObject();
            ScriptTake t;
            t.id = to.value(QStringLiteral("id")).toString();
            t.binId = to.value(QStringLiteral("binId")).toString();
            t.clipId = to.value(QStringLiteral("clipId")).toString();
            t.clipName = to.value(QStringLiteral("clipName")).toString();
            t.take = to.value(QStringLiteral("take")).toString();
            t.first = std::clamp(to.value(QStringLiteral("first")).toInt(), 0, last);
            t.last = std::clamp(to.value(QStringLiteral("last")).toInt(), t.first, last);
            t.frame = std::max<qint64>(0, to.value(QStringLiteral("frame")).toInteger());
            t.offscreen = ranges(to.value(QStringLiteral("offscreen")));
            t.colors = ranges(to.value(QStringLiteral("colors")));
            const QJsonObject marks = to.value(QStringLiteral("marks")).toObject();
            for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
                bool ok = false;
                const int line = it.key().toInt(&ok);
                if (ok && line >= t.first && line <= t.last) t.marks.insert(line, std::max<qint64>(0, it.value().toInteger()));
            }
            if (!t.id.isEmpty()) slate.takes << t;
        }
        slate.active = std::clamp(so.value(QStringLiteral("active")).toInt(), 0, std::max(0, static_cast<int>(slate.takes.size()) - 1));
        if (!slate.id.isEmpty() && !slate.takes.isEmpty()) s.slates << slate;
    }
    return s;
}

Script scriptFromText(const QString& text, const ScriptLayout& layout) {
    Script s;
    s.layout = layout;
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (t.startsWith(QChar(0xFEFF))) t.remove(0, 1);
    for (QString line : t.split(QLatin1Char('\n'))) {
        // Tabs to the next multiple of 8 columns.
        QString expanded;
        for (const QChar c : line) {
            if (c == QLatin1Char('\t')) {
                expanded += QString(8 - expanded.size() % 8, QLatin1Char(' '));
            } else {
                expanded += c;
            }
        }
        s.lines << expanded;
    }
    // A text ending with a line break has no last empty row.
    if (s.lines.size() > 1 && s.lines.last().isEmpty()) s.lines.removeLast();
    return s;
}

bool isScript(const BinItem& item) {
    return item.kind == BinItemKind::Script;
}

std::optional<Script> scriptOf(const BinItem& item) {
    if (!isScript(item)) return std::nullopt;
    return Script::fromJson(item.composition.value(QLatin1String(kScriptKey)).toObject());
}

void setScript(BinItem& item, const Script& script) {
    item.composition.insert(QLatin1String(kScriptKey), script.toJson());
}

BinItem scriptItem(const QString& name, const Script& script) {
    BinItem item;
    item.id = newItemId();
    item.name = name;
    item.kind = BinItemKind::Script;
    item.created = item.modified = QDateTime::currentDateTimeUtc();
    setScript(item, script);
    return item;
}

void setScene(Script& script, int line, const QString& number) {
    if (line < 0 || line >= script.lines.size()) return;
    if (number.trimmed().isEmpty()) script.scenes.remove(line);
    else script.scenes.insert(line, number.trimmed());
}

void setPage(Script& script, int line, const QString& number) {
    if (line < 0 || line >= script.lines.size()) return;
    if (number.trimmed().isEmpty()) script.pages.remove(line);
    else script.pages.insert(line, number.trimmed());
}

int deleteScenes(Script& script, int from, int to) { return deleteIn(script.scenes, from, to); }
int deletePages(Script& script, int from, int to) { return deleteIn(script.pages, from, to); }

void replaceLines(Script& script, int from, int to, const QStringList& replacement) {
    const auto count = static_cast<int>(script.lines.size());
    from = std::clamp(from, 0, count);
    to = std::clamp(to, from, count);
    QStringList rows;
    for (const QString& r : replacement) rows += r.split(QLatin1Char('\n'));
    const auto newCount = static_cast<int>(rows.size());
    shiftMarks(script.scenes, from, to, newCount);
    shiftMarks(script.pages, from, to, newCount);
    // Lines and take ranges: before the span they stay, after it they move by the change; inside it they shrink to what
    // is left (ours).
    const int delta = newCount - (to - from);
    auto shift = [&](int line, bool end) {
        if (line < from) return line;
        if (line >= to) return line + delta;
        return end ? from + std::max(0, newCount - 1) : from;
    };
    const int lastLine = std::max(0, static_cast<int>(script.lines.size()) + delta - 1);
    for (ScriptSlate& s : script.slates) {
        s.line = std::clamp(shift(s.line, false), 0, lastLine);
        for (ScriptTake& t : s.takes) {
            t.first = std::clamp(shift(t.first, false), 0, lastLine);
            t.last = std::clamp(shift(t.last, true), t.first, lastLine);
            for (QList<ScriptRange>* list : {&t.offscreen, &t.colors}) {
                for (ScriptRange& r : *list) {
                    r.first = std::clamp(shift(r.first, false), 0, lastLine);
                    r.last = std::clamp(shift(r.last, true), r.first, lastLine);
                }
            }
            QMap<int, std::int64_t> marks;
            for (auto it = t.marks.cbegin(); it != t.marks.cend(); ++it)
                marks.insert(std::clamp(shift(it.key(), false), t.first, t.last), it.value());
            t.marks = marks;
        }
    }
    script.lines = script.lines.mid(0, from) + rows + script.lines.mid(to);
}

std::optional<ScriptMatch> findInScript(const Script& script, const QString& text, int line, int column, bool backward,
                                        bool ignoreCase) {
    if (text.isEmpty() || script.lines.isEmpty()) return std::nullopt;
    const Qt::CaseSensitivity cs = ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive;
    const auto n = static_cast<int>(script.lines.size());
    line = std::clamp(line, 0, n - 1);
    // Every line once, starting with the given one (twice for it: after the column, then before it on the way round).
    for (int step = 0; step <= n; ++step) {
        const int l = backward ? ((line - step) % n + n) % n : (line + step) % n;
        const QString& s = script.lines[l];
        qsizetype at = -1;
        if (!backward) {
            const qsizetype from = step == 0 ? column : 0;
            at = step == n ? s.indexOf(text, 0, cs) : s.indexOf(text, from, cs);
            if (step == n && at >= column) at = -1;
        } else {
            const qsizetype from = step == 0 ? column - 1 : s.size();
            at = from < 0 ? -1 : s.lastIndexOf(text, from, cs);
        }
        if (at >= 0) return ScriptMatch{l, static_cast<int>(at), static_cast<int>(text.size())};
    }
    return std::nullopt;
}

int replaceInScript(Script& script, const QString& text, const QString& replacement, bool ignoreCase) {
    if (text.isEmpty()) return 0;
    const Qt::CaseSensitivity cs = ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive;
    int n = 0;
    for (QString& line : script.lines) {
        const auto before = line.count(text, cs);
        if (before == 0) continue;
        line.replace(text, replacement, cs);
        n += static_cast<int>(before);
    }
    return n;
}

// ---------------------------------------------------------------- slates and takes (8.3b)

namespace {

int lastLine(const Script& s) { return std::max(0, static_cast<int>(s.lines.size()) - 1); }

ScriptTake makeTake(const Script& s, int first, int last, const ScriptClip& clip) {
    ScriptTake t;
    t.id = newItemId();
    t.binId = clip.binId;
    t.clipId = clip.clipId;
    t.clipName = clip.name;
    t.take = clip.take;
    t.first = std::clamp(std::min(first, last), 0, lastLine(s));
    t.last = std::clamp(std::max(first, last), t.first, lastLine(s));
    return t;
}

ScriptTake* takeIn(Script& s, const QString& id, ScriptSlate** slate = nullptr) {
    for (ScriptSlate& sl : s.slates) {
        for (ScriptTake& t : sl.takes) {
            if (t.id == id) {
                if (slate) *slate = &sl;
                return &t;
            }
        }
    }
    return nullptr;
}

// Adds [first, last] to (on) or takes it out of (off) a take's ranges of one colour (empty: off-screen).
void markRange(QList<ScriptRange>& ranges, int first, int last, const QString& color, bool on) {
    QList<ScriptRange> out;
    for (const ScriptRange& r : ranges) {
        if (r.color != color || r.last < first || r.first > last) {
            out << r;
            continue;
        }
        // The part outside [first, last] stays.
        if (r.first < first) out.append({r.first, first - 1, r.color});
        if (r.last > last) out.append({last + 1, r.last, r.color});
    }
    if (on) {
        // A colour replaces any other colour on those lines.
        if (!color.isEmpty()) {
            QList<ScriptRange> kept;
            for (const ScriptRange& r : out) {
                if (r.last < first || r.first > last) {
                    kept << r;
                    continue;
                }
                if (r.first < first) kept.append({r.first, first - 1, r.color});
                if (r.last > last) kept.append({last + 1, r.last, r.color});
            }
            out = kept;
        }
        out.append({first, last, color});
    }
    std::sort(out.begin(), out.end(), [](const ScriptRange& a, const ScriptRange& b) { return a.first < b.first; });
    ranges = out;
}

} // namespace

QString addSlate(Script& script, int first, int last, const QList<ScriptClip>& clips) {
    if (clips.isEmpty() || script.lines.isEmpty()) return {};
    ScriptSlate slate;
    slate.id = newItemId();
    for (const ScriptClip& c : clips) slate.takes << makeTake(script, first, last, c);
    slate.line = slate.takes.first().first;
    script.slates << slate;
    return slate.id;
}

QString addTake(Script& script, const QString& slateId, int first, int last, const ScriptClip& clip) {
    for (ScriptSlate& s : script.slates) {
        if (s.id != slateId) continue;
        s.takes << makeTake(script, first, last, clip);
        return s.takes.last().id;
    }
    return {};
}

const ScriptSlate* slateOf(const Script& script, const QString& takeId) {
    for (const ScriptSlate& s : script.slates) {
        for (const ScriptTake& t : s.takes) {
            if (t.id == takeId) return &s;
        }
    }
    return nullptr;
}

const ScriptTake* takeOf(const Script& script, const QString& takeId) {
    for (const ScriptSlate& s : script.slates) {
        for (const ScriptTake& t : s.takes) {
            if (t.id == takeId) return &t;
        }
    }
    return nullptr;
}

int deleteTakes(Script& script, const QStringList& takeIds) {
    int n = 0;
    for (ScriptSlate& s : script.slates) {
        const QString active = s.takes.isEmpty() ? QString() : s.takes.value(s.active).id;
        n += static_cast<int>(s.takes.removeIf([&](const ScriptTake& t) { return takeIds.contains(t.id); }));
        const auto at = std::find_if(s.takes.cbegin(), s.takes.cend(), [&](const ScriptTake& t) { return t.id == active; });
        s.active = at == s.takes.cend() ? 0 : static_cast<int>(at - s.takes.cbegin());
    }
    script.slates.removeIf([](const ScriptSlate& s) { return s.takes.isEmpty(); });
    return n;
}

void moveSlate(Script& script, const QString& slateId, int lines, int x, bool withTakes) {
    const int end = lastLine(script);
    for (ScriptSlate& s : script.slates) {
        if (s.id != slateId) continue;
        s.x = std::max(0, s.x + x);
        if (!withTakes) {
            s.line = std::clamp(s.line + lines, 0, end);
            return;
        }
        // "As you move the slate, the takes continue to cover the same number of lines in the script": the move stops
        // where the first or last take would leave the script.
        int lo = s.line;
        int hi = s.line;
        for (const ScriptTake& t : s.takes) {
            lo = std::min(lo, t.first);
            hi = std::max(hi, t.last);
        }
        const int d = std::clamp(lines, -lo, end - hi);
        s.line += d;
        for (ScriptTake& t : s.takes) {
            t.first += d;
            t.last += d;
            for (QList<ScriptRange>* list : {&t.offscreen, &t.colors}) {
                for (ScriptRange& r : *list) {
                    r.first += d;
                    r.last += d;
                }
            }
            QMap<int, std::int64_t> marks;
            for (auto it = t.marks.cbegin(); it != t.marks.cend(); ++it) marks.insert(it.key() + d, it.value());
            t.marks = marks;
        }
        return;
    }
}

void setTakeLines(Script& script, const QString& takeId, int first, int last) {
    ScriptTake* t = takeIn(script, takeId);
    if (!t) return;
    t->first = std::clamp(std::min(first, last), 0, lastLine(script));
    t->last = std::clamp(std::max(first, last), t->first, lastLine(script));
    // Indicators outside the new lines go.
    for (QList<ScriptRange>* list : {&t->offscreen, &t->colors}) {
        QList<ScriptRange> kept;
        for (ScriptRange r : *list) {
            r.first = std::max(r.first, t->first);
            r.last = std::min(r.last, t->last);
            if (r.last >= r.first) kept << r;
        }
        *list = kept;
    }
    t->marks.removeIf([&](const QMap<int, std::int64_t>::iterator& it) { return it.key() < t->first || it.key() > t->last; });
}

void stepTakeFrames(Script& script, const QStringList& takeIds, std::int64_t frames) {
    for (const QString& id : takeIds) {
        ScriptSlate* slate = nullptr;
        ScriptTake* t = takeIn(script, id, &slate);
        if (!t) continue;
        t->frame = std::max<std::int64_t>(0, t->frame + frames);
    }
    if (!takeIds.isEmpty()) setActiveTake(script, takeIds.first());
}

void setActiveTake(Script& script, const QString& takeId) {
    for (ScriptSlate& s : script.slates) {
        for (int i = 0; i < s.takes.size(); ++i) {
            if (s.takes[i].id == takeId) s.active = i;
        }
    }
}

bool isOffscreen(const ScriptTake& take, int line) {
    return std::any_of(take.offscreen.cbegin(), take.offscreen.cend(), [line](const ScriptRange& r) { return line >= r.first && line <= r.last; });
}

QString colorAt(const ScriptTake& take, int line) {
    for (const ScriptRange& r : take.colors) {
        if (line >= r.first && line <= r.last) return r.color;
    }
    return {};
}

bool toggleOffscreen(Script& script, const QStringList& takeIds, int first, int last) {
    const ScriptTake* lead = takeIds.isEmpty() ? nullptr : takeOf(script, takeIds.first());
    if (!lead) return false;
    const int a = std::max(std::min(first, last), lead->first);
    const bool on = !isOffscreen(*lead, std::clamp(a, lead->first, lead->last));
    for (const QString& id : takeIds) {
        ScriptTake* t = takeIn(script, id);
        if (!t) continue;
        const int f = std::max(std::min(first, last), t->first);
        const int l = std::min(std::max(first, last), t->last);
        if (l >= f) markRange(t->offscreen, f, l, QString(), on);
    }
    return on;
}

bool toggleColor(Script& script, const QStringList& takeIds, int first, int last, const QString& color) {
    const ScriptTake* lead = takeIds.isEmpty() ? nullptr : takeOf(script, takeIds.first());
    if (!lead || color.isEmpty()) return false;
    // "The first take in the selected region determines the color indicator status" (p.582).
    const int a = std::clamp(std::min(first, last), lead->first, lead->last);
    const bool on = colorAt(*lead, a) != color;
    for (const QString& id : takeIds) {
        ScriptTake* t = takeIn(script, id);
        if (!t) continue;
        const int f = std::max(std::min(first, last), t->first);
        const int l = std::min(std::max(first, last), t->last);
        if (l < f) continue;
        if (on) {
            markRange(t->colors, f, l, color, true);
        } else {
            markRange(t->colors, f, l, color, false);
        }
    }
    return on;
}

std::int64_t interpolatedFrame(const ScriptTake& take, int line, double fraction, std::int64_t clipFrames) {
    const int lines = take.last - take.first + 1;
    const double at = (std::clamp(line, take.first, take.last) - take.first + std::clamp(fraction, 0.0, 1.0)) / lines;
    return std::clamp<std::int64_t>(static_cast<std::int64_t>(at * static_cast<double>(clipFrames)), 0, std::max<std::int64_t>(0, clipFrames - 1));
}

bool addScriptMark(Script& script, const QString& takeId, int line, std::int64_t frame) {
    ScriptTake* t = takeIn(script, takeId);
    if (!t || script.lines.isEmpty()) return false;
    line = std::clamp(line, 0, lastLine(script));
    t->first = std::min(t->first, line);
    t->last = std::max(t->last, line);
    t->marks.insert(line, std::max<std::int64_t>(0, frame));
    return true;
}

bool moveScriptMark(Script& script, const QString& takeId, int from, int to) {
    ScriptTake* t = takeIn(script, takeId);
    if (!t || !t->marks.contains(from)) return false;
    to = std::clamp(to, t->first, t->last);
    if (to == from) return false;
    const std::int64_t frame = t->marks.take(from);
    t->marks.insert(to, frame);
    return true;
}

int deleteScriptMarks(Script& script, const QStringList& takeIds, int first, int last) {
    int n = 0;
    for (const QString& id : takeIds) {
        if (ScriptTake* t = takeIn(script, id))
            n += static_cast<int>(t->marks.removeIf([&](const QMap<int, std::int64_t>::iterator& it) {
                return it.key() >= first && it.key() <= last;
            }));
    }
    return n;
}

int countScriptMarks(const Script& script, const QStringList& takeIds, int first, int last) {
    int n = 0;
    for (const QString& id : takeIds) {
        if (const ScriptTake* t = takeOf(script, id))
            for (auto it = t->marks.cbegin(); it != t->marks.cend(); ++it) n += it.key() >= first && it.key() <= last ? 1 : 0;
    }
    return n;
}

std::optional<std::pair<int, int>> scriptLinesAt(const ScriptTake& take, std::int64_t frame) {
    // Marks by frame; the line of the last one at or before the frame, to the line before the next one by frame.
    std::optional<std::pair<int, std::int64_t>> at;
    std::optional<std::pair<int, std::int64_t>> next;
    for (auto it = take.marks.cbegin(); it != take.marks.cend(); ++it) {
        if (it.value() <= frame && (!at || it.value() >= at->second)) at = std::pair{it.key(), it.value()};
    }
    if (!at) return std::nullopt;
    for (auto it = take.marks.cbegin(); it != take.marks.cend(); ++it) {
        if (it.value() > frame && it.key() > at->first && (!next || it.value() < next->second)) next = std::pair{it.key(), it.value()};
    }
    return std::pair{at->first, next ? std::max(at->first, next->first - 1) : take.last};
}

std::optional<std::pair<std::int64_t, std::int64_t>> markedFrames(const ScriptTake& take, int first, int last,
                                                                  std::int64_t clipFrames) {
    if (take.marks.isEmpty()) return std::nullopt;
    const auto in = take.marks.upperBound(first);
    std::int64_t start = 0;
    if (in != take.marks.cbegin()) {
        start = std::prev(in).value();
    } else if (in != take.marks.cend() && in.key() <= last) {
        start = in.value(); // the range begins before every mark: from its first mark
    } else {
        return std::nullopt;
    }
    const auto after = take.marks.upperBound(last);
    std::int64_t end = after == take.marks.cend() ? clipFrames - 1 : after.value() - 1;
    end = std::max(start, std::min(end, std::max<std::int64_t>(start, clipFrames - 1)));
    return std::pair{start, end};
}

} // namespace lumenis::model
