#include "lumenis/model/settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace lumenis::model {

namespace {

constexpr int kFileVersion = 1;
constexpr auto kFileFormat = "cutthulhu-settings";

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

} // namespace

QJsonObject SettingsFile::toJson() const {
    QJsonArray list;
    for (const SettingEntry& e : entries) {
        QJsonObject o{{QStringLiteral("kind"), e.kind}, {QStringLiteral("active"), e.active}};
        if (!e.name.isEmpty()) o.insert(QStringLiteral("name"), e.name);
        if (!e.values.isEmpty()) o.insert(QStringLiteral("values"), e.values);
        list.append(o);
    }
    return {{QStringLiteral("format"), QString::fromLatin1(kFileFormat)},
            {QStringLiteral("version"), kFileVersion},
            {QStringLiteral("settings"), list}};
}

std::optional<SettingsFile> SettingsFile::fromJson(const QJsonObject& o) {
    if (o.value(QStringLiteral("format")).toString() != QLatin1String(kFileFormat)) return std::nullopt;
    if (o.value(QStringLiteral("version")).toInt() > kFileVersion) return std::nullopt;
    SettingsFile file;
    for (const QJsonValue& v : o.value(QStringLiteral("settings")).toArray()) {
        const QJsonObject e = v.toObject();
        SettingEntry entry;
        entry.kind = e.value(QStringLiteral("kind")).toString();
        if (entry.kind.isEmpty()) continue;
        entry.name = e.value(QStringLiteral("name")).toString();
        entry.active = e.value(QStringLiteral("active")).toBool(true);
        entry.values = e.value(QStringLiteral("values")).toObject();
        file.entries << entry;
    }
    file.normalize();
    return file;
}

std::optional<SettingsFile> SettingsFile::read(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        fail(error, f.errorString());
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        fail(error, parseError.errorString());
        return std::nullopt;
    }
    auto file = fromJson(doc.object());
    if (!file) fail(error, QStringLiteral("not a settings file"));
    return file;
}

bool SettingsFile::write(const QString& path, QString* error) const {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return fail(error, QStringLiteral("cannot create the folder"));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return fail(error, f.errorString());
    f.write(QJsonDocument(toJson()).toJson());
    if (!f.commit()) return fail(error, f.errorString());
    return true;
}

SettingsFile SettingsFile::seeded(const QStringList& kinds, const SettingsFile* site) {
    SettingsFile file;
    for (const QString& kind : kinds) {
        const QList<int> fromSite = site ? site->versions(kind) : QList<int>{};
        if (fromSite.isEmpty()) {
            file.entries << SettingEntry{kind, {}, true, {}};
        } else {
            for (int i : fromSite) file.entries << site->entries.at(i);
        }
    }
    file.normalize();
    return file;
}

QStringList SettingsFile::kinds() const {
    QStringList out;
    for (const SettingEntry& e : entries) {
        if (!out.contains(e.kind)) out << e.kind;
    }
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QList<int> SettingsFile::versions(const QString& kind) const {
    QList<int> out;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).kind == kind) out << i;
    }
    return out;
}

int SettingsFile::activeIndex(const QString& kind) const {
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).kind == kind && entries.at(i).active) return i;
    }
    return -1;
}

QJsonObject SettingsFile::activeValues(const QString& kind) const {
    const int i = activeIndex(kind);
    return i < 0 ? QJsonObject{} : entries.at(i).values;
}

void SettingsFile::setActive(int index) {
    if (index < 0 || index >= entries.size()) return;
    const QString kind = entries.at(index).kind;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).kind == kind) entries[i].active = i == index;
    }
}

int SettingsFile::duplicate(int index) {
    if (index < 0 || index >= entries.size()) return -1;
    SettingEntry copy = entries.at(index);
    copy.active = false;
    if (!copy.name.isEmpty()) {
        // "Edit.2" duplicates as "Edit.3": the next number after the highest
        // version of that name.
        static const QRegularExpression numbered(QStringLiteral("^(.*)\\.(\\d+)$"));
        const QRegularExpressionMatch m = numbered.match(copy.name);
        const QString base = m.hasMatch() ? m.captured(1) : copy.name;
        int highest = 0;
        for (int i : versions(copy.kind)) {
            const QRegularExpressionMatch other = numbered.match(entries.at(i).name);
            if (other.hasMatch() && other.captured(1) == base) highest = std::max(highest, other.captured(2).toInt());
        }
        copy.name = QStringLiteral("%1.%2").arg(base).arg(highest + 1);
    }
    const int at = versions(copy.kind).constLast() + 1;
    entries.insert(at, copy);
    return at;
}

void SettingsFile::remove(QList<int> indices) {
    std::sort(indices.begin(), indices.end(), std::greater<>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (int i : indices) {
        if (i >= 0 && i < entries.size()) entries.removeAt(i);
    }
    normalize();
}

int SettingsFile::add(SettingEntry entry, bool replace) {
    const int active = activeIndex(entry.kind);
    if (replace && active >= 0) {
        entries[active].values = entry.values;
        entries[active].name = entry.name;
        return active;
    }
    entry.active = active < 0;
    const QList<int> existing = versions(entry.kind);
    const int at = existing.isEmpty() ? static_cast<int>(entries.size()) : existing.constLast() + 1;
    entries.insert(at, entry);
    return at;
}

int SettingsFile::ensure(const QStringList& kinds) {
    int added = 0;
    for (const QString& kind : kinds) {
        if (versions(kind).isEmpty()) {
            entries << SettingEntry{kind, {}, true, {}};
            ++added;
        }
    }
    return added;
}

void SettingsFile::normalize() {
    // Exactly one active version of each kind.
    for (const QString& kind : kinds()) {
        const QList<int> all = versions(kind);
        int active = -1;
        for (int i : all) {
            if (entries.at(i).active && active < 0) {
                active = i;
            } else {
                entries[i].active = false;
            }
        }
        if (active < 0) entries[all.constFirst()].active = true;
    }
}

} // namespace lumenis::model
