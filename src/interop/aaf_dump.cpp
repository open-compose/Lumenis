#include "lumenis/interop/aaf.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace lumenis::interop::aaf {

namespace {

QJsonValue dumpObject(const File& file, const Object& o, int depth);

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

bool isSetType(const MetaDictionary& meta, const QUuid& type) {
    const TypeDef* t = meta.typeDef(type);
    for (int i = 0; t && i < 16 && t->kind == TypeKind::Rename; ++i) t = meta.typeDef(t->element);
    return t && t->kind == TypeKind::Set;
}

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

QJsonValue dumpObject(const File& file, const Object& o, int depth) {
    if (o.isNull() || depth > 200) return QJsonValue();
    QJsonObject properties;
    for (const RawProperty& p : o.properties()) properties.insert(o.propertyName(p.pid), dumpProperty(file, o, p, depth));
    return QJsonObject{{QStringLiteral("class"), o.className()}, {QStringLiteral("properties"), properties}};
}

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
