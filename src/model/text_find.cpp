#include "lumenis/model/text_find.h"

#include <QRegularExpression>
#include <QSet>

// Text find (text_find.h). Matching is plain substring search; whole words put \b around the escaped
// query, so punctuation in the query ("@#$%^&*()=+[]\", p.428) is found as typed.

namespace lumenis::model {

bool textMatches(const QString& text, const QString& query, const TextFindOptions& options) {
    if (query.isEmpty()) return true;
    const Qt::CaseSensitivity cs = options.ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive;
    if (!options.wholeWords) return text.contains(query, cs);
    QRegularExpression word(QStringLiteral("(?<!\\w)%1(?!\\w)").arg(QRegularExpression::escape(query)),
                            options.ignoreCase ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);
    return word.match(text).hasMatch();
}

QStringList searchColumns(const BinItem& item) {
    QStringList columns = binHeadings();
    QStringList own = item.fields.keys();
    own.sort();
    for (const QString& c : own) {
        if (!columns.contains(c)) columns << c;
    }
    return columns;
}

bool itemMatches(const BinItem& item, const QString& query, const QList<FindFilter>& filters, const TextFindOptions& options) {
    const QStringList columns = searchColumns(item);
    auto anyColumn = [&](const QString& text) {
        return std::any_of(columns.begin(), columns.end(),
                           [&](const QString& c) { return textMatches(cellText(item, c), text, options); });
    };
    if (!query.isEmpty() && !anyColumn(query)) return false;
    for (const FindFilter& f : filters) {
        if (f.text.isEmpty()) continue;
        if (f.column.isEmpty() ? !anyColumn(f.text) : !textMatches(cellText(item, f.column), f.text, options)) return false;
    }
    return true;
}

namespace {

bool searched(const BinItem& item) {
    return item.kind != BinItemKind::Effect && item.kind != BinItemKind::Source;
}

} // namespace

QList<ItemFound> findItems(const QList<FindBin>& bins, const QString& query, const QList<FindFilter>& filters,
                           const TextFindOptions& options) {
    QList<ItemFound> out;
    for (const FindBin& b : bins) {
        for (const BinItem& item : b.bin.items) {
            if (searched(item) && itemMatches(item, query, filters, options)) out.append({b.id, b.name, item});
        }
    }
    return out;
}

QList<MarkerFound> findMarkers(const QList<FindBin>& bins, const QString& query, const QList<FindFilter>& filters,
                               const TextFindOptions& options) {
    QList<MarkerFound> out;
    for (const FindBin& b : bins) {
        for (const BinItem& item : b.bin.items) {
            if (!searched(item) || !itemMatches(item, {}, filters, options)) continue;
            for (const Marker& m : markersOf(item)) {
                if (textMatches(m.comment, query, options)) out.append({b.id, b.name, item, m});
            }
        }
    }
    return out;
}

} // namespace lumenis::model
