#include "lumenis/interop/aaf_writer.h"

#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>

// The AAF writer (docs/spec/aaf.md, "Writing"). aaf::Builder keeps objects in memory by class AUID and
// properties by pid, encodes data properties by the metadictionary's types (the reverse of File::decode
// in aaf.cpp), and toBytes() lays them out with cfb::Writer: each object a storage named after the
// property that holds it, its `properties` stream, the index streams of its vectors and sets, the
// `referenced properties` table of weak references, and a MetaDictionary defining the whole model.
// Storage names and layouts follow Media Composer's and pyaaf2's files. aaf_export.cpp builds bins with
// it; tests/interop/tst_aaf.cpp (exportRoundTrip) and ctest aaf_round_trip read what it writes back.
namespace lumenis::interop::aaf {

namespace {

constexpr quint8 kLittleEndian = 0x4c;
// The `properties` stream's version byte, as written.
constexpr quint8 kPropertyVersion = 32;

// The header class ID AAF files with 4096-byte sectors carry (as pyaaf2
// and the AAF SDK write them).
const QUuid kHeaderClsid = auidFromText("0d010201-0200-0000-060e-2b3403020101");

void append16(QByteArray& b, quint16 v) {
    char d[2];
    qToLittleEndian<quint16>(v, d);
    b.append(d, 2);
}
void append32(QByteArray& b, quint32 v) {
    char d[4];
    qToLittleEndian<quint32>(v, d);
    b.append(d, 4);
}
void append64(QByteArray& b, quint64 v) {
    char d[8];
    qToLittleEndian<quint64>(v, d);
    b.append(d, 8);
}

// UTF-16LE with a terminating 0, as AAF stores strings and storage names in property values.
QByteArray utf16z(const QString& s) {
    QByteArray out;
    for (const QChar c : s) append16(out, c.unicode());
    append16(out, 0);
    return out;
}

// A storage name of at most `size` - 1 characters: the property's name,
// squeezed in the middle when too long, then "-" and its pid in hex.
QString mangle(const QString& name, int pid, int size) {
    const QString p = QString::number(pid, 16);
    const int room = size - static_cast<int>(p.size()) - 2;
    QString n = name;
    if (n.size() > room) {
        const int half = room / 2;
        n = name.left(half) + QLatin1Char('-') + name.right(room - half - 1);
    }
    return n + QLatin1Char('-') + p;
}

// An AUID as a compound file class ID: the same 16 bytes.
cfb::Clsid clsidOf(const QUuid& auid) {
    const QByteArray b = auidBytes(auid);
    cfb::Clsid c{};
    std::memcpy(c.data(), b.constData(), c.size());
    return c;
}

// The type, renames followed.
const TypeDef* resolved(const MetaDictionary& meta, const QUuid& type) {
    const TypeDef* t = meta.typeDef(type);
    for (int i = 0; t && t->kind == TypeKind::Rename && i < 16; ++i) t = meta.typeDef(t->element);
    return t;
}

} // namespace

// The skeleton every file has: Root → Header → ContentStorage (Mobs, empty) and Dictionary. Dynamic pids
// are given first, so each property has one.
Builder::Builder(MetaDictionary meta) : meta_(std::move(meta)) {
    meta_.assignDynamicPids();
    root_ = create(QStringLiteral("Root"));
    header_ = create(QStringLiteral("Header"));
    content_ = create(QStringLiteral("ContentStorage"));
    dictionary_ = create(QStringLiteral("Dictionary"));
    setObject(root_, QStringLiteral("Header"), header_);
    setObject(header_, QStringLiteral("Content"), content_);
    setObject(header_, QStringLiteral("Dictionary"), dictionary_);
    setEmpty(content_, QStringLiteral("Mobs"));
}

bool Builder::fail(const QString& message) {
    errors_ << message;
    return false;
}

Builder::Ref Builder::create(const QString& className) {
    const ClassDef* c = meta_.classDef(className);
    if (!c || !c->concrete) {
        fail(QStringLiteral("%1: no such concrete class").arg(className));
        return -1;
    }
    objects_.append(Object{c->auid, {}, -1});
    return static_cast<Ref>(objects_.size() - 1);
}

QString Builder::className(Ref object) const {
    if (object < 0 || object >= objects_.size()) return {};
    const ClassDef* c = meta_.classDef(objects_[object].classAuid);
    return c ? c->name : QString();
}

const PropertyDef* Builder::property(Ref object, const QString& name) {
    if (object < 0 || object >= objects_.size()) {
        fail(QStringLiteral("%1: no object").arg(name));
        return nullptr;
    }
    const PropertyDef* p = meta_.property(objects_[object].classAuid, name);
    if (!p || p->pid < 0) fail(QStringLiteral("%1 has no property %2").arg(className(object), name));
    return p && p->pid >= 0 ? p : nullptr;
}

// The reverse of File::decode. Integers take any number (or a bool) and are cut to the type's size;
// enumerations take an element name, a number or a bool; extensible enumerations a name or an AUID;
// strings are written with a terminating 0; records other than AUID and MobIDType are a QVariantMap with
// every member; string arrays are each string with its 0. Indirect, opaque, stream and reference types
// are not data, and fail.
QByteArray Builder::encode(const QUuid& type, const QVariant& value, bool* ok) const {
    const TypeDef* t = meta_.typeDef(type);
    if (!t) {
        *ok = false;
        return {};
    }
    QByteArray out;
    switch (t->kind) {
    case TypeKind::Int: {
        bool good = false;
        const qint64 v = value.toLongLong(&good);
        if (!good && value.typeId() != QMetaType::Bool) {
            *ok = false;
            return {};
        }
        const auto u = static_cast<quint64>(value.typeId() == QMetaType::ULongLong ? value.toULongLong() : static_cast<quint64>(v));
        switch (t->size) {
        case 1: out.append(static_cast<char>(u & 0xff)); break;
        case 2: append16(out, static_cast<quint16>(u)); break;
        case 4: append32(out, static_cast<quint32>(u)); break;
        case 8: append64(out, u); break;
        default: *ok = false;
        }
        return out;
    }
    case TypeKind::Character: {
        const QString s = value.toString();
        append16(out, s.isEmpty() ? 0 : s.at(0).unicode());
        return out;
    }
    case TypeKind::GenericCharacter: return value.toString().toLatin1().left(1);
    case TypeKind::String: {
        const TypeDef* e = meta_.typeDef(t->element);
        if (e && e->kind == TypeKind::GenericCharacter) return value.toString().toLatin1() + '\0';
        return utf16z(value.toString());
    }
    case TypeKind::Enum: {
        if (value.typeId() == QMetaType::Bool) {
            out.append(value.toBool() ? '\1' : '\0');
            return out;
        }
        qint64 number = 0;
        if (value.typeId() == QMetaType::QString) {
            const qint64 none = std::numeric_limits<qint64>::min();
            number = none;
            for (auto it = t->values.begin(); it != t->values.end(); ++it) {
                if (it.value() == value.toString()) number = it.key();
            }
            if (number == none) {
                *ok = false;
                return {};
            }
        } else {
            number = value.toLongLong();
        }
        return encode(t->element, number, ok);
    }
    case TypeKind::ExtEnum: {
        QUuid u = value.typeId() == QMetaType::QUuid ? value.toUuid() : QUuid();
        if (value.typeId() == QMetaType::QString) {
            for (auto it = t->extValues.begin(); it != t->extValues.end(); ++it) {
                if (it.value() == value.toString()) u = it.key();
            }
        }
        if (u.isNull()) {
            *ok = false;
            return {};
        }
        return auidBytes(u);
    }
    case TypeKind::Record: {
        if (t->name == QLatin1String("AUID")) {
            if (value.typeId() != QMetaType::QUuid) break;
            return auidBytes(value.toUuid());
        }
        if (t->name == QLatin1String("MobIDType")) {
            if (value.userType() != qMetaTypeId<MobId>()) break;
            return mobIdBytes(value.value<MobId>());
        }
        const QVariantMap m = value.toMap();
        for (const auto& member : t->members) {
            if (!m.contains(member.first)) {
                *ok = false;
                return {};
            }
            out += encode(member.second, m.value(member.first), ok);
            if (!*ok) return {};
        }
        return out;
    }
    case TypeKind::FixedArray:
    case TypeKind::VarArray:
    case TypeKind::Set: {
        const TypeDef* e = meta_.typeDef(t->element);
        if (t->kind == TypeKind::VarArray && e && e->kind == TypeKind::Character) {
            for (const QString& s : value.toStringList()) out += utf16z(s);
            return out;
        }
        const QVariantList list = value.toList();
        if (t->kind == TypeKind::FixedArray && list.size() != t->size) break;
        for (const QVariant& v : list) {
            out += encode(t->element, v, ok);
            if (!*ok) return {};
        }
        return out;
    }
    case TypeKind::Rename: return encode(t->element, value, ok);
    default: break;
    }
    *ok = false;
    return {};
}

// A data property. References, streams and indirect values have their own setters; arrays of references
// are refused here too.
bool Builder::set(Ref object, const QString& name, const QVariant& value) {
    const PropertyDef* p = property(object, name);
    if (!p) return false;
    const TypeDef* t = resolved(meta_, p->type);
    if (!t || t->kind == TypeKind::StrongRef || t->kind == TypeKind::WeakRef || t->kind == TypeKind::Indirect ||
        t->kind == TypeKind::Opaque || t->kind == TypeKind::Stream) {
        return fail(QStringLiteral("%1.%2 is not a data property").arg(className(object), name));
    }
    if ((t->kind == TypeKind::VarArray || t->kind == TypeKind::Set)) {
        const TypeDef* e = resolved(meta_, t->element);
        if (e && (e->kind == TypeKind::StrongRef || e->kind == TypeKind::WeakRef)) {
            return fail(QStringLiteral("%1.%2 holds references").arg(className(object), name));
        }
    }
    bool ok = true;
    const QByteArray data = encode(p->type, value, &ok);
    if (!ok) return fail(QStringLiteral("%1.%2: %3 does not fit its type").arg(className(object), name, value.toString()));
    Property& prop = objects_[object].properties[p->pid];
    prop = Property{Kind::Data, name, data, {}, {}, p->type};
    return true;
}

// An indirect value: the byte order, the AUID of the value's type, then the value encoded by that type.
bool Builder::setIndirect(Ref object, const QString& name, const QString& typeName, const QVariant& value) {
    const PropertyDef* p = property(object, name);
    if (!p) return false;
    const TypeDef* t = resolved(meta_, p->type);
    const TypeDef* inner = meta_.typeDef(typeName);
    if (!t || (t->kind != TypeKind::Indirect && t->kind != TypeKind::Opaque) || !inner) {
        return fail(QStringLiteral("%1.%2 cannot hold a %3").arg(className(object), name, typeName));
    }
    bool ok = true;
    const QByteArray data = encode(inner->auid, value, &ok);
    if (!ok) return fail(QStringLiteral("%1.%2: the value is not a %3").arg(className(object), name, typeName));
    QByteArray out;
    out.append(static_cast<char>(kLittleEndian));
    out += auidBytes(inner->auid) + data;
    objects_[object].properties[p->pid] = Property{Kind::Data, name, out, {}, {}, p->type};
    return true;
}

// Finds the property and checks that its type is a reference of the wanted kind (strong or weak), alone
// or in a variable array (vector) or set. A property of another kind already there is replaced, emptied;
// the kept `type` is the reference type itself (the element type for vectors and sets), which keyOf and
// weakTable read.
Builder::Property* Builder::referenceProperty(Ref object, const QString& name, Kind single, Kind vector, Kind set) {
    const PropertyDef* p = property(object, name);
    if (!p) return nullptr;
    const TypeDef* t = resolved(meta_, p->type);
    const TypeKind want = single == Kind::Strong ? TypeKind::StrongRef : TypeKind::WeakRef;
    Kind kind;
    QUuid element = p->type;
    if (t && t->kind == want) {
        kind = single;
    } else if (t && (t->kind == TypeKind::VarArray || t->kind == TypeKind::Set) && resolved(meta_, t->element) &&
               resolved(meta_, t->element)->kind == want) {
        kind = t->kind == TypeKind::VarArray ? vector : set;
        element = t->element;
    } else {
        fail(QStringLiteral("%1.%2 is not that kind of reference").arg(className(object), name));
        return nullptr;
    }
    Property& prop = objects_[object].properties[p->pid];
    if (prop.name.isEmpty() || prop.kind != kind) prop = Property{kind, name, {}, {}, {}, element};
    return &prop;
}

bool Builder::setObject(Ref object, const QString& name, Ref child) {
    if (child < 0 || child >= objects_.size()) return fail(QStringLiteral("%1: no object to reference").arg(name));
    Property* p = referenceProperty(object, name, Kind::Strong, Kind::StrongVector, Kind::StrongSet);
    if (!p) return false;
    if (p->kind != Kind::Strong) return fail(QStringLiteral("%1.%2 holds several objects").arg(className(object), name));
    p->children = {child};
    objects_[child].owner = object;
    return true;
}

bool Builder::append(Ref object, const QString& name, Ref child) {
    if (child < 0 || child >= objects_.size()) return fail(QStringLiteral("%1: no object to reference").arg(name));
    Property* p = referenceProperty(object, name, Kind::Strong, Kind::StrongVector, Kind::StrongSet);
    if (!p) return false;
    if (p->kind == Kind::Strong) return fail(QStringLiteral("%1.%2 holds one object").arg(className(object), name));
    p->children.append(child);
    objects_[child].owner = object;
    return true;
}

bool Builder::setEmpty(Ref object, const QString& name) {
    Property* p = referenceProperty(object, name, Kind::Strong, Kind::StrongVector, Kind::StrongSet);
    if (!p) return false;
    if (p->kind == Kind::Strong) return fail(QStringLiteral("%1.%2 holds one object").arg(className(object), name));
    return true;
}

bool Builder::setWeak(Ref object, const QString& name, const QUuid& key) {
    Property* p = referenceProperty(object, name, Kind::Weak, Kind::WeakVector, Kind::WeakSet);
    if (!p) return false;
    if (p->kind != Kind::Weak) return fail(QStringLiteral("%1.%2 holds several references").arg(className(object), name));
    p->keys = {auidBytes(key)};
    return true;
}

bool Builder::appendWeak(Ref object, const QString& name, const QUuid& key) {
    Property* p = referenceProperty(object, name, Kind::Weak, Kind::WeakVector, Kind::WeakSet);
    if (!p) return false;
    if (p->kind == Kind::Weak) return fail(QStringLiteral("%1.%2 holds one reference").arg(className(object), name));
    p->keys.append(auidBytes(key));
    return true;
}

int Builder::pidOfProperty(const QUuid& propertyAuid) const {
    const PropertyDef* p = meta_.propertyByAuid(propertyAuid);
    return p ? p->pid : -1;
}

// The weak reference's table: its type's target path (the properties from the root to the set its targets
// are in) as pids. Equal paths share one table.
int Builder::weakTable(const QUuid& weakRefType) {
    const TypeDef* t = resolved(meta_, weakRefType);
    if (!t) return -1;
    QList<quint16> path;
    for (const QUuid& step : t->targetPath) {
        const int pid = pidOfProperty(step);
        if (pid < 0) return -1;
        path.append(static_cast<quint16>(pid));
    }
    const qsizetype at = tables_.indexOf(path);
    if (at >= 0) return static_cast<int>(at);
    tables_.append(path);
    return static_cast<int>(tables_.size() - 1);
}

// Looks for the first unique property in the class and then its parents; the object's value of it, or
// nothing when that property is not set.
QByteArray Builder::uniqueKey(Ref object) const {
    QUuid at = objects_[object].classAuid;
    for (int depth = 0; depth < 64 && !at.isNull(); ++depth) {
        const ClassDef* c = meta_.classDef(at);
        if (!c) break;
        for (const PropertyDef& p : c->properties) {
            if (!p.unique) continue;
            const auto it = objects_[object].properties.find(p.pid);
            return it == objects_[object].properties.end() ? QByteArray() : it->second.data;
        }
        if (c->parent == c->auid) break;
        at = c->parent;
    }
    return {};
}

// The unique identifier of the class the reference type names (or one of its parents): its pid and its
// fixed size in bytes; {0, 0} when there is none.
std::pair<int, int> Builder::keyOf(const QUuid& referenceType) const {
    const TypeDef* t = resolved(meta_, referenceType);
    QUuid at = t ? t->element : QUuid();
    for (int depth = 0; depth < 64 && !at.isNull(); ++depth) {
        const ClassDef* c = meta_.classDef(at);
        if (!c) break;
        for (const PropertyDef& p : c->properties) {
            if (p.unique) return {p.pid, meta_.byteSize(p.type)};
        }
        if (c->parent == c->auid) break;
        at = c->parent;
    }
    return {0, 0};
}

// Required properties not set, of every object but the Root, by the class and its parents.
QStringList Builder::missing() const {
    QStringList out;
    for (qsizetype i = 0; i < objects_.size(); ++i) {
        // The root's MetaDictionary is made when the file is written.
        if (i == root_) continue;
        const Object& o = objects_[i];
        QUuid at = o.classAuid;
        const ClassDef* own = meta_.classDef(at);
        for (int depth = 0; depth < 64 && !at.isNull(); ++depth) {
            const ClassDef* c = meta_.classDef(at);
            if (!c) break;
            for (const PropertyDef& p : c->properties) {
                // ObjClass is the storage's class ID; Root has no other.
                if (p.optional || p.name == QLatin1String("ObjClass") || o.properties.count(p.pid)) continue;
                const QString line = QStringLiteral("%1: %2").arg(own ? own->name : QString(), p.name);
                if (!out.contains(line)) out << line;
            }
            if (c->parent == c->auid) break;
            at = c->parent;
        }
    }
    return out;
}

// The metadictionary pyaaf2's files carry: every class of the model (but Root, which is the file itself)
// with its property definitions, and every type, each as the TypeDefinition class of its kind with the
// properties that kind has. In AUID order, so the same model always writes the same objects. References
// to other definitions are weak references by AUID.
void Builder::addMetaDictionary(Ref meta) {
    auto define = [&](Ref def, const QUuid& auid, const QString& name) {
        set(def, QStringLiteral("Identification"), auid);
        set(def, QStringLiteral("Name"), name);
    };
    QList<QUuid> classes = meta_.classAuids();
    std::sort(classes.begin(), classes.end());
    for (const QUuid& id : classes) {
        const ClassDef* c = meta_.classDef(id);
        if (c->name == QLatin1String("Root")) continue;
        const Ref def = create(QStringLiteral("ClassDefinition"));
        define(def, c->auid, c->name);
        // The hierarchy's roots name themselves as parent.
        setWeak(def, QStringLiteral("ParentClass"), c->parent.isNull() ? c->auid : c->parent);
        set(def, QStringLiteral("IsConcrete"), c->concrete);
        for (const PropertyDef& p : c->properties) {
            const Ref pd = create(QStringLiteral("PropertyDefinition"));
            define(pd, p.auid, p.name);
            set(pd, QStringLiteral("Type"), p.type);
            set(pd, QStringLiteral("IsOptional"), p.optional);
            set(pd, QStringLiteral("LocalIdentification"), p.pid);
            if (p.unique) set(pd, QStringLiteral("IsUniqueIdentifier"), true);
            append(def, QStringLiteral("Properties"), pd);
        }
        append(meta, QStringLiteral("ClassDefinitions"), def);
    }
    static const QHash<TypeKind, QString> classOf{
        {TypeKind::Int, QStringLiteral("TypeDefinitionInteger")},
        {TypeKind::Character, QStringLiteral("TypeDefinitionCharacter")},
        {TypeKind::GenericCharacter, QStringLiteral("TypeDefinitionGenericCharacter")},
        {TypeKind::String, QStringLiteral("TypeDefinitionString")},
        {TypeKind::Enum, QStringLiteral("TypeDefinitionEnumeration")},
        {TypeKind::ExtEnum, QStringLiteral("TypeDefinitionExtendibleEnumeration")},
        {TypeKind::Record, QStringLiteral("TypeDefinitionRecord")},
        {TypeKind::FixedArray, QStringLiteral("TypeDefinitionFixedArray")},
        {TypeKind::VarArray, QStringLiteral("TypeDefinitionVariableArray")},
        {TypeKind::Set, QStringLiteral("TypeDefinitionSet")},
        {TypeKind::Rename, QStringLiteral("TypeDefinitionRename")},
        {TypeKind::Indirect, QStringLiteral("TypeDefinitionIndirect")},
        {TypeKind::Opaque, QStringLiteral("TypeDefinitionOpaque")},
        {TypeKind::Stream, QStringLiteral("TypeDefinitionStream")},
        {TypeKind::StrongRef, QStringLiteral("TypeDefinitionStrongObjectReference")},
        {TypeKind::WeakRef, QStringLiteral("TypeDefinitionWeakObjectReference")},
    };
    QList<QUuid> types = meta_.typeAuids();
    std::sort(types.begin(), types.end());
    for (const QUuid& id : types) {
        const TypeDef* t = meta_.typeDef(id);
        // The root's own references are not part of the model.
        if (t->name == QLatin1String("HeaderStrongReference") || t->name == QLatin1String("MetaDictionaryStrongReference")) {
            continue;
        }
        const Ref def = create(classOf.value(t->kind));
        define(def, t->auid, t->name);
        switch (t->kind) {
        case TypeKind::Int:
            set(def, QStringLiteral("Size"), t->size);
            set(def, QStringLiteral("IsSigned"), t->isSigned);
            break;
        case TypeKind::GenericCharacter: set(def, QStringLiteral("CharacterSize"), t->size); break;
        case TypeKind::StrongRef: setWeak(def, QStringLiteral("ReferencedType"), t->element); break;
        case TypeKind::WeakRef: {
            setWeak(def, QStringLiteral("ReferencedType"), t->element);
            QVariantList path;
            for (const QUuid& step : t->targetPath) path << step;
            set(def, QStringLiteral("TargetSet"), path);
            break;
        }
        case TypeKind::Enum: {
            setWeak(def, QStringLiteral("ElementType"), t->element);
            QStringList names;
            QVariantList values;
            for (auto it = t->values.begin(); it != t->values.end(); ++it) {
                names << it.value();
                values << it.key();
            }
            set(def, QStringLiteral("ElementNames"), names);
            set(def, QStringLiteral("ElementValues"), values);
            break;
        }
        case TypeKind::ExtEnum: {
            QList<QUuid> keys = t->extValues.keys();
            std::sort(keys.begin(), keys.end());
            QStringList names;
            QVariantList values;
            for (const QUuid& k : keys) {
                names << t->extValues.value(k);
                values << k;
            }
            set(def, QStringLiteral("ElementNames"), names);
            set(def, QStringLiteral("ElementValues"), values);
            break;
        }
        case TypeKind::FixedArray:
            setWeak(def, QStringLiteral("ElementType"), t->element);
            set(def, QStringLiteral("ElementCount"), t->size);
            break;
        case TypeKind::VarArray:
        case TypeKind::Set:
        case TypeKind::String: setWeak(def, QStringLiteral("ElementType"), t->element); break;
        case TypeKind::Rename: setWeak(def, QStringLiteral("RenamedType"), t->element); break;
        case TypeKind::Record: {
            QStringList names;
            for (const auto& m : t->members) {
                names << m.first;
                appendWeak(def, QStringLiteral("MemberTypes"), m.second);
            }
            set(def, QStringLiteral("MemberNames"), names);
            break;
        }
        default: break;
        }
        append(meta, QStringLiteral("TypeDefinitions"), def);
    }
}

// Writes the file. The MetaDictionary is made first, under the Root; then the objects are written depth
// first from the Root, each into its storage; then `referenced properties`. On every way out the
// metadictionary objects, the Root's reference to them, the tables and any errors made here are dropped
// again (`restore`), so that the builder is left as it was and can be written more than once.
QByteArray Builder::toBytes(QString* error) {
    const qsizetype errorsBefore = errors_.size();
    const Ref meta = create(QStringLiteral("MetaDictionary"));
    auto restore = [&] {
        objects_[root_].properties.erase(pidOfProperty(auidFromText("0d010301-0101-0100-060e-2b3401010102")));
        objects_.resize(meta);
        tables_.clear();
        errors_.resize(errorsBefore);
    };
    setObject(root_, QStringLiteral("MetaDictionary"), meta);
    addMetaDictionary(meta);
    if (!errors_.isEmpty()) {
        if (error) *error = errors_.join(QLatin1Char('\n'));
        restore();
        return {};
    }

    cfb::Writer w(4096);
    w.setHeaderClsid(clsidOf(kHeaderClsid));
    QString failure;
    // One object into `storage`: the class ID, the child storages and index streams of its references,
    // then its `properties` stream (see File::propertiesOf in aaf.cpp): the byte order, the version, the
    // count, then each property's pid, stored form and size, then the values.
    std::function<void(Ref, int)> write = [&](Ref ref, int storage) {
        Object& o = objects_[ref];
        w.setClsid(storage, clsidOf(o.classAuid));
        QByteArray index;
        QByteArray values;
        int count = 0;
        for (auto& [pid, p] : o.properties) {
            QByteArray data;
            quint16 form = 0;
            const bool many = p.kind == Kind::StrongVector || p.kind == Kind::StrongSet || p.kind == Kind::WeakVector ||
                              p.kind == Kind::WeakSet;
            // A vector's or set's storage name leaves room for "{index}" and " index" within 31
            // characters.
            const QString name = mangle(p.name, pid, many ? 22 : 32);
            switch (p.kind) {
            case Kind::Data:
                form = static_cast<quint16>(StoredForm::Data);
                data = p.data;
                break;
            case Kind::Strong: {
                form = static_cast<quint16>(StoredForm::StrongRef);
                data = utf16z(name);
                const int child = w.addStorage(storage, name, &failure);
                if (child >= 0) write(p.children.first(), child);
                break;
            }
            case Kind::StrongVector:
            case Kind::StrongSet: {
                const bool set = p.kind == Kind::StrongSet;
                form = static_cast<quint16>(set ? StoredForm::StrongRefSet : StoredForm::StrongRefVector);
                data = utf16z(name);
                // The index stream (see Object::referenced in aaf.cpp): the count, the next free local
                // key (the count) and the last free key (none), and for a set the key's pid and size;
                // then each element's local key (its position), and for a set its reference count (1) and
                // key. Each element's storage is `<name>{<hex key>}`.
                QByteArray list;
                append32(list, static_cast<quint32>(p.children.size()));
                append32(list, static_cast<quint32>(p.children.size()));
                append32(list, 0xFFFFFFFF);
                if (set) {
                    const auto [keyPid, keySize] = keyOf(p.type);
                    append16(list, static_cast<quint16>(keyPid));
                    list.append(static_cast<char>(keySize));
                }
                for (qsizetype i = 0; i < p.children.size(); ++i) {
                    append32(list, static_cast<quint32>(i));
                    if (set) {
                        append32(list, 1);
                        const QByteArray key = uniqueKey(p.children[i]);
                        if (key.size() != keyOf(p.type).second) {
                            failure = QStringLiteral("%1.%2: an element has no key").arg(className(ref), p.name);
                        }
                        list += key;
                    }
                    const int child = w.addStorage(storage, QStringLiteral("%1{%2}").arg(name).arg(i, 0, 16), &failure);
                    if (child >= 0) write(p.children[i], child);
                }
                w.addStream(storage, name + QLatin1String(" index"), list, &failure);
                break;
            }
            case Kind::Weak:
            case Kind::WeakVector:
            case Kind::WeakSet: {
                const int table = weakTable(p.type);
                // The key property of the class the reference names.
                const int keyPid = keyOf(p.type).first;
                if (table < 0 || keyPid == 0) {
                    failure = QStringLiteral("%1.%2: the reference's target cannot be named").arg(className(ref), p.name);
                    continue;
                }
                // A weak reference: the table, the key's pid, the key's size and the key. A vector or set
                // is stored as an index stream instead: the count, the table, the key's pid, the key's
                // size, then the keys.
                if (p.kind == Kind::Weak) {
                    form = static_cast<quint16>(StoredForm::WeakRef);
                    append16(data, static_cast<quint16>(table));
                    append16(data, static_cast<quint16>(keyPid));
                    data.append(static_cast<char>(p.keys.first().size()));
                    data += p.keys.first();
                } else {
                    form = static_cast<quint16>(p.kind == Kind::WeakSet ? StoredForm::WeakRefSet : StoredForm::WeakRefVector);
                    data = utf16z(name);
                    QByteArray list;
                    append32(list, static_cast<quint32>(p.keys.size()));
                    append16(list, static_cast<quint16>(table));
                    append16(list, static_cast<quint16>(keyPid));
                    list.append(static_cast<char>(p.keys.isEmpty() ? 16 : p.keys.first().size()));
                    for (const QByteArray& k : p.keys) list += k;
                    w.addStream(storage, name + QLatin1String(" index"), list, &failure);
                }
                break;
            }
            }
            if (data.size() > 0xFFFF) {
                failure = QStringLiteral("%1.%2 is too large for a property").arg(className(ref), p.name);
                continue;
            }
            append16(index, static_cast<quint16>(pid));
            append16(index, form);
            append16(index, static_cast<quint16>(data.size()));
            values += data;
            ++count;
        }
        QByteArray stream;
        stream.append(static_cast<char>(kLittleEndian));
        stream.append(static_cast<char>(kPropertyVersion));
        append16(stream, static_cast<quint16>(count));
        stream += index + values;
        w.addStream(storage, QStringLiteral("properties"), stream, &failure);
    };
    write(root_, cfb::Writer::root());

    // `referenced properties` (see File::readReferencedProperties in aaf.cpp): the tables' paths, each
    // ended by a 0.
    QByteArray referenced;
    referenced.append(static_cast<char>(kLittleEndian));
    append16(referenced, static_cast<quint16>(tables_.size()));
    quint32 pids = 0;
    for (const auto& path : tables_) pids += static_cast<quint32>(path.size() + 1);
    append32(referenced, pids);
    for (const auto& path : tables_) {
        for (const quint16 pid : path) append16(referenced, pid);
        append16(referenced, 0);
    }
    w.addStream(cfb::Writer::root(), QStringLiteral("referenced properties"), referenced, &failure);

    // The metadictionary is rebuilt on each save.
    restore();
    if (!failure.isEmpty()) {
        if (error) *error = failure;
        return {};
    }
    return w.toBytes(error);
}

bool Builder::save(const QString& path, QString* error) {
    const QByteArray bytes = toBytes(error);
    if (bytes.isEmpty()) return false;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, out.errorString());
        return false;
    }
    return true;
}

} // namespace lumenis::interop::aaf
