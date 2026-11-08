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

} // namespace lumenis::model
