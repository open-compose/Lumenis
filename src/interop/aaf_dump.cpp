#include "lumenis/interop/aaf.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <limits>

// Dumps of AAF objects as JSON (docs/spec/aaf.md, "Dumps"), for `cutthulhu --dump-aaf`. The form is the
// one scripts/aaf-dump.py prints from pyaaf2's reading of the same file, so the two readers can be
// compared property by property, or by the digest of canonicalJson (tst_aaf's corpusDumps, ctest
// aaf_pyaaf2).
namespace lumenis::interop::aaf {

namespace {

QJsonValue dumpObject(const File& file, const Object& o, int depth);

// A decoded value as JSON: numbers, Booleans and strings as themselves, AUIDs and MobIDs as text,
// undecoded bytes as {"bytes": hex}, and objects (strong references' targets) dumped in place.
QJsonValue dumpVariant(const File& file, const QVariant& v, int depth) {
    switch (v.typeId()) {
    case QMetaType::Bool: return v.toBool();
    case QMetaType::LongLong: return v.toLongLong();
    case QMetaType::ULongLong: return static_cast<qint64>(v.toULongLong());
    case QMetaType::QString: return v.toString();
    case QMetaType::QStringList: return QJsonArray::fromStringList(v.toStringList());
    case QMetaType::QUuid: return auidText(v.toUuid());
    case QMetaType::QByteArray: return QJsonObject{{QStringLiteral("bytes"), QString::fromLatin1(v.toByteArray().toHex())}};
    case QMetaType::QVariantMap: {
        QJsonObject out;
        const QVariantMap m = v.toMap();
        for (auto it = m.begin(); it != m.end(); ++it) out.insert(it.key(), dumpVariant(file, it.value(), depth));
        return out;
    }
    case QMetaType::QVariantList: {
        QJsonArray out;
        for (const QVariant& e : v.toList()) out.append(dumpVariant(file, e, depth));
        return out;
    }
    default: break;
    }
    if (v.userType() == qMetaTypeId<MobId>()) return QString::fromStdString(v.value<MobId>().toString());
    if (v.userType() == qMetaTypeId<Object>()) return dumpObject(file, v.value<Object>(), depth + 1);
    return QJsonValue();
}

// Whether the type is a set, renames followed.
bool isSetType(const MetaDictionary& meta, const QUuid& type) {
    const TypeDef* t = meta.typeDef(type);
    for (int i = 0; t && i < 16 && t->kind == TypeKind::Rename; ++i) t = meta.typeDef(t->element);
    return t && t->kind == TypeKind::Set;
}

// A property as JSON by its stored form: strong references as the objects they hold, weak references as
// {"weakref": the key as text}, streams as {"stream": size in bytes, or -1 when missing}, and data
// decoded by its type ({"bytes": hex} when the property is not in the metadictionary).
QJsonValue dumpProperty(const File& file, const Object& o, const RawProperty& p, int depth) {
    switch (static_cast<StoredForm>(p.form)) {
    case StoredForm::StrongRef: {
        const QList<Object> r = o.referenced(p);
        return r.isEmpty() ? QJsonValue() : dumpObject(file, r.first(), depth + 1);
    }
    case StoredForm::StrongRefVector:
    case StoredForm::StrongRefSet: {
        QJsonArray out;
        for (const Object& r : o.referenced(p)) out.append(dumpObject(file, r, depth + 1));
        return out;
    }
    case StoredForm::WeakRef: {
        const auto k = o.weakKey(p);
        return k ? QJsonValue(QJsonObject{{QStringLiteral("weakref"), k->text()}}) : QJsonValue();
    }
    case StoredForm::WeakRefVector:
    case StoredForm::WeakRefSet: {
        QJsonArray out;
        for (const WeakKey& k : o.weakKeys(p)) out.append(QJsonObject{{QStringLiteral("weakref"), k.text()}});
        return out;
    }
    case StoredForm::Stream: {
        const int s = o.streamEntry(p);
        return QJsonObject{{QStringLiteral("stream"), s >= 0 ? static_cast<qint64>(file.container().entry(s).size) : -1}};
    }
    default: break;
    }
    const PropertyDef* d = o.propertyDef(p.pid);
    if (!d) return QJsonObject{{QStringLiteral("bytes"), QString::fromLatin1(p.data.toHex())}};
    QJsonValue v = dumpVariant(file, file.decode(d->type, p.data), depth);
    // A set has no order: sorted, as pyaaf2's Python sets are made comparable.
    if (v.isArray() && isSetType(file.metaDictionary(), d->type)) {
        QJsonArray a = v.toArray();
        QList<QPair<QByteArray, QJsonValue>> keyed;
        for (const QJsonValue& e : a) keyed.append({canonicalJson(e), e});
        std::sort(keyed.begin(), keyed.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        QJsonArray sorted;
        for (const auto& e : keyed) sorted.append(e.second);
        v = sorted;
    }
    return v;
}

// An object as {"class": name, "properties": {name: value}}. The depth limit keeps damaged files that
// reference their own ancestors from recursing without end.
QJsonValue dumpObject(const File& file, const Object& o, int depth) {
    if (o.isNull() || depth > 200) return QJsonValue();
    QJsonObject properties;
    for (const RawProperty& p : o.properties()) properties.insert(o.propertyName(p.pid), dumpProperty(file, o, p, depth));
    return QJsonObject{{QStringLiteral("class"), o.className()}, {QStringLiteral("properties"), properties}};
}

// A JSON string as Python's json.dumps writes it with ensure_ascii: everything outside printable ASCII
// as a backslash-u escape of four hex digits (UTF-16 code units, so a pair for characters past U+FFFF).
void writeString(QByteArray& out, const QString& s) {
    out.append('"');
    for (const QChar c : s) {
        const char16_t u = c.unicode();
        switch (u) {
        case '"': out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\n': out.append("\\n"); break;
        case '\r': out.append("\\r"); break;
        case '\t': out.append("\\t"); break;
        case '\b': out.append("\\b"); break;
        case '\f': out.append("\\f"); break;
        default:
            if (u < 0x20 || u > 0x7e) {
                out.append(QStringLiteral("\\u%1").arg(static_cast<uint>(u), 4, 16, QLatin1Char('0')).toLatin1());
            } else {
                out.append(static_cast<char>(u));
            }
        }
    }
    out.append('"');
}

// Compact JSON with sorted keys; integers without a fraction, other numbers with 17 significant digits.
void writeCanonical(QByteArray& out, const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined: out.append("null"); break;
    case QJsonValue::Bool: out.append(v.toBool() ? "true" : "false"); break;
    case QJsonValue::Double: {
        const qint64 sentinel = std::numeric_limits<qint64>::min();
        const qint64 i = v.toInteger(sentinel);
        out.append(i != sentinel ? QByteArray::number(i) : QByteArray::number(v.toDouble(), 'g', 17));
        break;
    }
    case QJsonValue::String: writeString(out, v.toString()); break;
    case QJsonValue::Array: {
        out.append('[');
        bool first = true;
        for (const QJsonValue& e : v.toArray()) {
            if (!first) out.append(',');
            first = false;
            writeCanonical(out, e);
        }
        out.append(']');
        break;
    }
    case QJsonValue::Object: {
        const QJsonObject o = v.toObject();
        QStringList keys = o.keys();
        std::sort(keys.begin(), keys.end());
        out.append('{');
        bool first = true;
        for (const QString& k : keys) {
            if (!first) out.append(',');
            first = false;
            writeString(out, k);
            out.append(':');
            writeCanonical(out, o.value(k));
        }
        out.append('}');
        break;
    }
    }
}

} // namespace

QJsonValue dump(const File& file, const Object& from) {
    return dumpObject(file, from.isNull() ? file.header() : from, 0);
}

QByteArray canonicalJson(const QJsonValue& value) {
    QByteArray out;
    writeCanonical(out, value);
    return out;
}

} // namespace lumenis::interop::aaf
