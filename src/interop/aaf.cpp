#include "lumenis/interop/aaf.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace lumenis::interop::aaf {

namespace {

// The tables of aaf_model.inc.
struct ModelClass {
    const char* name;
    const char* auid;
    const char* parent;
    bool concrete;
    int firstProperty;
    int propertyCount;
};
struct ModelProperty {
    const char* name;
    const char* auid;
    int pid;
    const char* type;
    bool optional;
    bool unique;
};
struct ModelType {
    TypeKind kind;
    const char* name;
    const char* auid;
    const char* element;
    int size;
    bool isSigned;
    int firstMember;
    int memberCount;
};
struct ModelMember {
    const char* name;
    const char* value;
};

#include "aaf_model.inc"

// Classes of the metadictionary's own objects (AAF Object Specification).
const QUuid kRootClass = auidFromText("b3b398a5-1c90-11d4-8053-080036210804");
const QUuid kAuidType = auidFromText("01030100-0000-0000-060e-2b3401040101");
const QUuid kMobIdType = auidFromText("01030200-0000-0000-060e-2b3401040101");
const QUuid kBooleanType = auidFromText("01040100-0000-0000-060e-2b3401040101");
const QUuid kCharacterType = auidFromText("01100100-0000-0000-060e-2b3401040101");

const QHash<QUuid, TypeKind>& typeDefClasses() {
    static const QHash<QUuid, TypeKind> classes{
        {auidFromText("0d010101-0204-0000-060e-2b3402060101"), TypeKind::Int},
        {auidFromText("0d010101-0205-0000-060e-2b3402060101"), TypeKind::StrongRef},
        {auidFromText("0d010101-0206-0000-060e-2b3402060101"), TypeKind::WeakRef},
        {auidFromText("0d010101-0207-0000-060e-2b3402060101"), TypeKind::Enum},
        {auidFromText("0d010101-0208-0000-060e-2b3402060101"), TypeKind::FixedArray},
        {auidFromText("0d010101-0209-0000-060e-2b3402060101"), TypeKind::VarArray},
        {auidFromText("0d010101-020a-0000-060e-2b3402060101"), TypeKind::Set},
        {auidFromText("0d010101-020b-0000-060e-2b3402060101"), TypeKind::String},
        {auidFromText("0d010101-020c-0000-060e-2b3402060101"), TypeKind::Stream},
        {auidFromText("0d010101-020d-0000-060e-2b3402060101"), TypeKind::Record},
        {auidFromText("0d010101-020e-0000-060e-2b3402060101"), TypeKind::Rename},
        {auidFromText("0d010101-0220-0000-060e-2b3402060101"), TypeKind::ExtEnum},
        {auidFromText("0d010101-0221-0000-060e-2b3402060101"), TypeKind::Indirect},
        {auidFromText("0d010101-0222-0000-060e-2b3402060101"), TypeKind::Opaque},
        {auidFromText("0d010101-0223-0000-060e-2b3402060101"), TypeKind::Character},
        {auidFromText("0e040101-0000-0000-060e-2b3402060101"), TypeKind::GenericCharacter},
    };
    return classes;
}

// Property IDs of the metadictionary's objects.
enum : quint16 {
    PidMetaDictionary = 0x0001,
    PidHeader = 0x0002,
    PidClassDefinitions = 0x0003,
    PidTypeDefinitions = 0x0004,
    PidIdentification = 0x0005,
    PidName = 0x0006,
    PidParentClass = 0x0008,
    PidProperties = 0x0009,
    PidIsConcrete = 0x000A,
    PidType = 0x000B,
    PidIsOptional = 0x000C,
    PidLocalId = 0x000D,
    PidIsUniqueIdentifier = 0x000E,
    PidIntSize = 0x000F,
    PidIntSigned = 0x0010,
    PidStrongRefType = 0x0011,
    PidWeakRefType = 0x0012,
    PidWeakRefTargetSet = 0x0013,
    PidEnumType = 0x0014,
    PidEnumNames = 0x0015,
    PidEnumValues = 0x0016,
    PidFixedArrayType = 0x0017,
    PidFixedArrayCount = 0x0018,
    PidVarArrayType = 0x0019,
    PidSetType = 0x001A,
    PidStringType = 0x001B,
    PidRecordTypes = 0x001C,
    PidRecordNames = 0x001D,
    PidRenameType = 0x001E,
    PidExtEnumNames = 0x001F,
    PidExtEnumValues = 0x0020,
};

constexpr quint8 kLittleEndian = 0x4c;

QString utf16(const QByteArray& data) {
    qsizetype n = data.size() / 2;
    const char* p = data.constData();
    while (n > 0 && qFromLittleEndian<quint16>(p + 2 * (n - 1)) == 0) --n;
    QString s(n, Qt::Uninitialized);
    for (qsizetype i = 0; i < n; ++i) s[i] = QChar(qFromLittleEndian<quint16>(p + 2 * i));
    return s;
}

QStringList utf16List(const QByteArray& data) {
    QStringList out;
    QString current;
    const char* p = data.constData();
    for (qsizetype i = 0; i + 1 < data.size(); i += 2) {
        const quint16 c = qFromLittleEndian<quint16>(p + i);
        if (c == 0) {
            out.append(current);
            current.clear();
        } else {
            current.append(QChar(c));
        }
    }
    return out;
}

QString hex(const QByteArray& data) { return QString::fromLatin1(data.toHex()); }

} // namespace

// ---- AUIDs and MobIDs -------------------------------------------------------

QUuid auidFromBytes(const char* p) {
    const auto* b = reinterpret_cast<const uchar*>(p);
    return QUuid(qFromLittleEndian<quint32>(p), qFromLittleEndian<quint16>(p + 4), qFromLittleEndian<quint16>(p + 6),
                 b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

QByteArray auidBytes(const QUuid& auid) {
    QByteArray out(16, '\0');
    qToLittleEndian<quint32>(auid.data1, out.data());
    qToLittleEndian<quint16>(auid.data2, out.data() + 4);
    qToLittleEndian<quint16>(auid.data3, out.data() + 6);
    std::memcpy(out.data() + 8, auid.data4, 8);
    return out;
}

QString auidText(const QUuid& auid) { return auid.toString(QUuid::WithoutBraces); }

QUuid auidFromText(const char* text) { return QUuid::fromString(QLatin1String(text)); }

MobId mobIdFromBytes(const char* p) {
    MobId::Bytes b{};
    std::memcpy(b.data(), p, 16);
    const auto* u = reinterpret_cast<const std::uint8_t*>(p);
    // The material number is a GUID (Data1-3 little-endian, Data4 as is).
    // When it is a SMPTE label instead (the UMID's own label ends 00 and
    // Data4 starts 06 0e 2b 34 7f 7f), its halves are swapped: Data4 comes
    // first in the UMID. pyaaf2 reads them so (MobID.urn).
    std::array<std::uint8_t, 8> d123{u[19], u[18], u[17], u[16], u[21], u[20], u[23], u[22]};
    const bool label = u[11] == 0x00 && u[24] == 0x06 && u[25] == 0x0e && u[26] == 0x2b && u[27] == 0x34 &&
                       u[28] == 0x7f && u[29] == 0x7f;
    if (label) {
        std::memcpy(b.data() + 16, p + 24, 8);
        std::memcpy(b.data() + 24, d123.data(), 8);
    } else {
        std::memcpy(b.data() + 16, d123.data(), 8);
        std::memcpy(b.data() + 24, p + 24, 8);
    }
    return MobId(b);
}

QByteArray mobIdBytes(const MobId& mob) {
    const MobId::Bytes& b = mob.bytes();
    QByteArray out(32, '\0');
    std::memcpy(out.data(), b.data(), 16);
    const bool label = b[11] == 0x00 && b[16] == 0x06 && b[17] == 0x0e && b[18] == 0x2b && b[19] == 0x34 &&
                       b[20] == 0x7f && b[21] == 0x7f;
    const std::uint8_t* d123 = label ? b.data() + 24 : b.data() + 16;
    const std::uint8_t* d4 = label ? b.data() + 16 : b.data() + 24;
    const std::array<std::uint8_t, 8> le{d123[3], d123[2], d123[1], d123[0], d123[5], d123[4], d123[7], d123[6]};
    std::memcpy(out.data() + 16, le.data(), 8);
    std::memcpy(out.data() + 24, d4, 8);
    return out;
}

QString WeakKey::text() const {
    if (key.size() == 16) return auidText(auidFromBytes(key.constData()));
    if (key.size() == 32) return QString::fromStdString(mobIdFromBytes(key.constData()).toString());
    return hex(key);
}

// ---- MetaDictionary ---------------------------------------------------------

MetaDictionary MetaDictionary::builtIn() {
    MetaDictionary m;
    for (const ModelClass& c : kModelClasses) {
        ClassDef d;
        d.auid = auidFromText(c.auid);
        d.name = QString::fromLatin1(c.name);
        if (c.parent) d.parent = auidFromText(c.parent);
        d.concrete = c.concrete;
        for (int i = c.firstProperty; i < c.firstProperty + c.propertyCount; ++i) {
            const ModelProperty& p = kModelProperties[i];
            d.properties.append({auidFromText(p.auid), QString::fromLatin1(p.name), p.pid, auidFromText(p.type),
                                 p.optional, p.unique});
        }
        m.addClass(d);
    }
    for (const ModelType& t : kModelTypes) {
        TypeDef d;
        d.auid = auidFromText(t.auid);
        d.name = QString::fromLatin1(t.name);
        d.kind = t.kind;
        d.size = t.size;
        d.isSigned = t.isSigned;
        if (t.element) d.element = auidFromText(t.element);
        for (int i = t.firstMember; i < t.firstMember + t.memberCount; ++i) {
            const ModelMember& mm = kModelMembers[i];
            const QString name = QString::fromLatin1(mm.name);
            switch (t.kind) {
            case TypeKind::Enum: d.values.insert(QByteArray(mm.value).toLongLong(), name); break;
            case TypeKind::ExtEnum: d.extValues.insert(auidFromText(mm.value), name); break;
            case TypeKind::Record: d.members.append({name, auidFromText(mm.value)}); break;
            case TypeKind::WeakRef: d.targetPath.append(auidFromText(mm.value)); break;
            default: break;
            }
        }
        m.addType(d);
    }
    // The root object's class and its two references, which the model leaves out.
    const QUuid headerRef = auidFromText("05022800-0000-0000-060e-2b3401040101");
    const QUuid metaRef = auidFromText("05022700-0000-0000-060e-2b3401040101");
    m.addType({headerRef, QStringLiteral("HeaderStrongReference"), TypeKind::StrongRef, 0, false,
               auidFromText("0d010101-0101-2f00-060e-2b3402060101"), {}, {}, {}, {}});
    m.addType({metaRef, QStringLiteral("MetaDictionaryStrongReference"), TypeKind::StrongRef, 0, false,
               auidFromText("0d010101-0225-0000-060e-2b3402060101"), {}, {}, {}, {}});
    ClassDef root{kRootClass, QStringLiteral("Root"), {}, true, {}};
    root.properties.append({auidFromText("0d010301-0102-0100-060e-2b3401010102"), QStringLiteral("Header"), PidHeader,
                            headerRef, false, false});
    root.properties.append({auidFromText("0d010301-0101-0100-060e-2b3401010102"), QStringLiteral("MetaDictionary"),
                            PidMetaDictionary, metaRef, false, false});
    m.addClass(root);
    return m;
}

void MetaDictionary::addClass(const ClassDef& c) {
    auto it = classes_.find(c.auid);
    if (it == classes_.end()) {
        classes_.insert(c.auid, c);
    } else {
        // The file's definition: its name and the local IDs it gave.
        ClassDef& d = it.value();
        if (!c.name.isEmpty()) {
            classNames_.remove(d.name);
            d.name = c.name;
        }
        if (!c.parent.isNull()) d.parent = c.parent;
        d.concrete = c.concrete;
        for (const PropertyDef& p : c.properties) {
            auto same = std::find_if(d.properties.begin(), d.properties.end(),
                                     [&](const PropertyDef& q) { return q.auid == p.auid; });
            if (same == d.properties.end()) d.properties.append(p);
            else *same = p;
        }
    }
    classNames_.insert(c.name, c.auid);
}

void MetaDictionary::addType(const TypeDef& t) {
    auto it = types_.find(t.auid);
    if (it == types_.end()) {
        types_.insert(t.auid, t);
    } else {
        TypeDef& d = it.value();
        // Enumerations gain the elements the file adds.
        for (auto v = t.values.begin(); v != t.values.end(); ++v) d.values.insert(v.key(), v.value());
        for (auto v = t.extValues.begin(); v != t.extValues.end(); ++v) d.extValues.insert(v.key(), v.value());
    }
    typeNames_.insert(t.name, t.auid);
}

const ClassDef* MetaDictionary::classDef(const QUuid& auid) const {
    auto it = classes_.constFind(auid);
    return it == classes_.constEnd() ? nullptr : &it.value();
}

const ClassDef* MetaDictionary::classDef(const QString& name) const {
    auto it = classNames_.constFind(name);
    return it == classNames_.constEnd() ? nullptr : classDef(it.value());
}

const TypeDef* MetaDictionary::typeDef(const QUuid& auid) const {
    auto it = types_.constFind(auid);
    return it == types_.constEnd() ? nullptr : &it.value();
}

const TypeDef* MetaDictionary::typeDef(const QString& name) const {
    auto it = typeNames_.constFind(name);
    return it == typeNames_.constEnd() ? nullptr : typeDef(it.value());
}

const PropertyDef* MetaDictionary::property(const QUuid& classAuid, int pid) const {
    QUuid at = classAuid;
    for (int depth = 0; depth < 64 && !at.isNull(); ++depth) {
        const ClassDef* c = classDef(at);
        if (!c) return nullptr;
        for (const PropertyDef& p : c->properties) {
            if (p.pid == pid) return &p;
        }
        if (c->parent == c->auid) return nullptr;
        at = c->parent;
    }
    return nullptr;
}

const PropertyDef* MetaDictionary::property(const QUuid& classAuid, const QString& name) const {
    QUuid at = classAuid;
    for (int depth = 0; depth < 64 && !at.isNull(); ++depth) {
        const ClassDef* c = classDef(at);
        if (!c) return nullptr;
        for (const PropertyDef& p : c->properties) {
            if (p.name == name) return &p;
        }
        if (c->parent == c->auid) return nullptr;
        at = c->parent;
    }
    return nullptr;
}

bool MetaDictionary::isA(const QUuid& auid, const QUuid& base) const {
    QUuid at = auid;
    for (int depth = 0; depth < 64 && !at.isNull(); ++depth) {
        if (at == base) return true;
        const ClassDef* c = classDef(at);
        if (!c || c->parent == c->auid) return false;
        at = c->parent;
    }
    return false;
}

int MetaDictionary::byteSize(const QUuid& type) const {
    const TypeDef* t = typeDef(type);
    for (int depth = 0; t && depth < 32; ++depth) {
        switch (t->kind) {
        case TypeKind::Int: return t->size;
        case TypeKind::Character: return 2;
        case TypeKind::GenericCharacter: return t->size;
        case TypeKind::ExtEnum: return 16;
        case TypeKind::Enum:
        case TypeKind::Rename: t = typeDef(t->element); continue;
        case TypeKind::FixedArray: return t->size * byteSize(t->element);
        case TypeKind::Record: {
            int size = 0;
            for (const auto& m : t->members) {
                const int s = byteSize(m.second);
                if (s <= 0) return 0;
                size += s;
            }
            return size;
        }
        default: return 0;
        }
    }
    return 0;
}

// ---- Object -----------------------------------------------------------------

QString Object::path() const { return isNull() ? QString() : file_->container().path(entry_); }

QUuid Object::classAuid() const {
    if (isNull()) return {};
    const cfb::Clsid& c = file_->container().entry(entry_).clsid;
    return auidFromBytes(reinterpret_cast<const char*>(c.data()));
}

const ClassDef* Object::classDef() const { return isNull() ? nullptr : file_->metaDictionary().classDef(classAuid()); }

QString Object::className() const {
    const ClassDef* c = classDef();
    return c ? c->name : auidText(classAuid());
}

bool Object::isA(const QString& name) const {
    if (isNull()) return false;
    const ClassDef* base = file_->metaDictionary().classDef(name);
    return base && file_->metaDictionary().isA(classAuid(), base->auid);
}

QList<RawProperty> Object::properties() const { return isNull() ? QList<RawProperty>{} : file_->propertiesOf(entry_); }

std::optional<RawProperty> Object::raw(int pid) const {
    if (isNull()) return std::nullopt;
    for (const RawProperty& p : file_->propertiesOf(entry_)) {
        if (p.pid == pid) return p;
    }
    return std::nullopt;
}

std::optional<RawProperty> Object::raw(const QString& name) const {
    if (isNull()) return std::nullopt;
    const PropertyDef* d = file_->metaDictionary().property(classAuid(), name);
    if (!d || d->pid < 0) return std::nullopt;
    return raw(d->pid);
}

const PropertyDef* Object::propertyDef(int pid) const {
    return isNull() ? nullptr : file_->metaDictionary().property(classAuid(), pid);
}

QString Object::propertyName(int pid) const {
    const PropertyDef* d = propertyDef(pid);
    return d ? d->name : QStringLiteral("0x%1").arg(pid, 4, 16, QLatin1Char('0'));
}

QList<Object> Object::referenced(const RawProperty& p) const {
    QList<Object> out;
    if (isNull()) return out;
    cfb::Reader& c = file_->container();
    const QString name = utf16(p.data);
    switch (static_cast<StoredForm>(p.form)) {
    case StoredForm::StrongRef: {
        const int child = c.child(entry_, name);
        if (child >= 0 && c.entry(child).isStorage()) out.append(Object(file_, child));
        break;
    }
    case StoredForm::StrongRefVector:
    case StoredForm::StrongRefSet: {
        const int index = c.child(entry_, name + QLatin1String(" index"));
        if (index < 0) break;
        const QByteArray data = c.readAll(index);
        if (data.size() < 12) break;
        const quint32 count = qFromLittleEndian<quint32>(data.constData());
        const bool set = p.form == static_cast<quint16>(StoredForm::StrongRefSet);
        qsizetype at = 12;
        int keySize = 0;
        if (set) {
            if (data.size() < 15) break;
            keySize = static_cast<quint8>(data[14]);
            at = 15;
        }
        const qsizetype step = set ? 8 + keySize : 4;
        for (quint32 i = 0; i < count && at + step <= data.size(); ++i, at += step) {
            const quint32 local = qFromLittleEndian<quint32>(data.constData() + at);
            const int child = c.child(entry_, QStringLiteral("%1{%2}").arg(name).arg(local, 0, 16));
            if (child >= 0 && c.entry(child).isStorage()) out.append(Object(file_, child));
        }
        break;
    }
    default: break;
    }
    return out;
}

int Object::streamEntry(const RawProperty& p) const {
    if (isNull() || p.form != static_cast<quint16>(StoredForm::Stream)) return -1;
    // A byte order (0x55, unspecified), then the stream's name.
    const int child = file_->container().child(entry_, utf16(p.data.mid(1)));
    return child >= 0 && file_->container().entry(child).isStream() ? child : -1;
}

std::optional<WeakKey> Object::weakKey(const RawProperty& p) const {
    if (p.form != static_cast<quint16>(StoredForm::WeakRef) || p.data.size() < 5) return std::nullopt;
    const char* d = p.data.constData();
    WeakKey k;
    k.table = qFromLittleEndian<quint16>(d);
    k.keyPid = qFromLittleEndian<quint16>(d + 2);
    const int size = static_cast<quint8>(d[4]);
    if (p.data.size() < 5 + size) return std::nullopt;
    k.key = p.data.mid(5, size);
    return k;
}

QList<WeakKey> Object::weakKeys(const RawProperty& p) const {
    QList<WeakKey> out;
    if (isNull() || (p.form != static_cast<quint16>(StoredForm::WeakRefVector) &&
                     p.form != static_cast<quint16>(StoredForm::WeakRefSet))) {
        return out;
    }
    cfb::Reader& c = file_->container();
    const int index = c.child(entry_, utf16(p.data) + QLatin1String(" index"));
    if (index < 0) return out;
    const QByteArray data = c.readAll(index);
    if (data.size() < 9) return out;
    const char* d = data.constData();
    const quint32 count = qFromLittleEndian<quint32>(d);
    const quint16 table = qFromLittleEndian<quint16>(d + 4);
    const quint16 keyPid = qFromLittleEndian<quint16>(d + 6);
    const int size = static_cast<quint8>(d[8]);
    qsizetype at = 9;
    for (quint32 i = 0; i < count && size > 0 && at + size <= data.size(); ++i, at += size) {
        out.append({table, keyPid, data.mid(at, size)});
    }
    return out;
}

std::optional<WeakKey> Object::weakKey(const QString& name) const {
    const auto p = raw(name);
    return p ? weakKey(*p) : std::nullopt;
}

QVariant Object::value(const RawProperty& p) const {
    if (isNull()) return {};
    switch (static_cast<StoredForm>(p.form)) {
    case StoredForm::StrongRef: {
        const QList<Object> o = referenced(p);
        return o.isEmpty() ? QVariant() : QVariant::fromValue(o.first());
    }
    case StoredForm::StrongRefVector:
    case StoredForm::StrongRefSet: {
        QVariantList out;
        for (const Object& o : referenced(p)) out.append(QVariant::fromValue(o));
        return out;
    }
    case StoredForm::WeakRef: {
        const auto k = weakKey(p);
        if (!k) return {};
        const Object target = file_->resolve(*k);
        return target ? QVariant::fromValue(target) : QVariant(k->text());
    }
    case StoredForm::WeakRefVector:
    case StoredForm::WeakRefSet: {
        QVariantList out;
        for (const WeakKey& k : weakKeys(p)) {
            const Object target = file_->resolve(k);
            out.append(target ? QVariant::fromValue(target) : QVariant(k.text()));
        }
        return out;
    }
    case StoredForm::Stream: return {};
    default: break;
    }
    const PropertyDef* d = propertyDef(p.pid);
    return d ? file_->decode(d->type, p.data) : QVariant(p.data);
}

QVariant Object::value(const QString& name) const {
    const auto p = raw(name);
    return p ? value(*p) : QVariant();
}

Object Object::object(const QString& name) const {
    const auto p = raw(name);
    if (!p) return {};
    const QList<Object> o = referenced(*p);
    return o.isEmpty() ? Object() : o.first();
}

QList<Object> Object::objects(const QString& name) const {
    const auto p = raw(name);
    return p ? referenced(*p) : QList<Object>{};
}

QString Object::string(const QString& name, const QString& fallback) const {
    const QVariant v = value(name);
    return v.typeId() == QMetaType::QString ? v.toString() : fallback;
}

qint64 Object::integer(const QString& name, qint64 fallback) const {
    const QVariant v = value(name);
    if (v.typeId() == QMetaType::LongLong) return v.toLongLong();
    if (v.typeId() == QMetaType::ULongLong) return static_cast<qint64>(v.toULongLong());
    return fallback;
}

MobId Object::mobId(const QString& name) const {
    const QVariant v = value(name);
    return v.userType() == qMetaTypeId<MobId>() ? v.value<MobId>() : MobId();
}

QUuid Object::auid(const QString& name) const {
    const QVariant v = value(name);
    return v.typeId() == QMetaType::QUuid ? v.toUuid() : QUuid();
}

// ---- File -------------------------------------------------------------------

File::File() : cfb_(std::make_unique<cfb::Reader>()), meta_(std::make_unique<MetaDictionary>(MetaDictionary::builtIn())) {}

File::~File() = default;

void File::close() {
    cfb_->close();
    properties_.clear();
    targets_.clear();
    referenced_.clear();
    meta_ = std::make_unique<MetaDictionary>(MetaDictionary::builtIn());
}

bool File::open(const QString& path, QString* error) {
    close();
    error_.clear();
    if (!cfb_->open(path, &error_)) {
        if (error) *error = error_;
        return false;
    }
    if (root().classAuid() != kRootClass) {
        error_ = QStringLiteral("%1: not an AAF file (the root is not an AAF root object)").arg(path);
        cfb_->close();
        if (error) *error = error_;
        return false;
    }
    if (!readReferencedProperties()) {
        error_ = QStringLiteral("%1: the referenced properties are damaged").arg(path);
        cfb_->close();
        if (error) *error = error_;
        return false;
    }
    readMetaDictionary();
    return true;
}

bool File::readReferencedProperties() {
    const int entry = cfb_->find(QStringLiteral("/referenced properties"));
    if (entry < 0) return true; // a file without weak references
    const QByteArray data = cfb_->readAll(entry);
    if (data.size() < 7 || static_cast<quint8>(data[0]) != kLittleEndian) return false;
    const quint16 paths = qFromLittleEndian<quint16>(data.constData() + 1);
    const quint32 pids = qFromLittleEndian<quint32>(data.constData() + 3);
    QList<quint16> path;
    for (quint32 i = 0; i < pids && 7 + 2 * static_cast<qsizetype>(i) + 2 <= data.size(); ++i) {
        const quint16 pid = qFromLittleEndian<quint16>(data.constData() + 7 + 2 * i);
        if (pid == 0) {
            referenced_.append(path);
            path.clear();
        } else {
            path.append(pid);
        }
    }
    return referenced_.size() == paths;
}

const QList<RawProperty>& File::propertiesOf(int entry) const {
    auto it = properties_.constFind(entry);
    if (it != properties_.constEnd()) return it.value();
    QList<RawProperty> list;
    const int stream = cfb_->child(entry, QStringLiteral("properties"));
    if (stream >= 0) {
        const QByteArray data = cfb_->readAll(stream);
        if (data.size() >= 4 && static_cast<quint8>(data[0]) == kLittleEndian) {
            const quint16 count = qFromLittleEndian<quint16>(data.constData() + 2);
            qsizetype at = 4 + 6 * static_cast<qsizetype>(count);
            if (at <= data.size()) {
                for (quint16 i = 0; i < count; ++i) {
                    const char* e = data.constData() + 4 + 6 * i;
                    RawProperty p;
                    p.pid = qFromLittleEndian<quint16>(e);
                    p.form = qFromLittleEndian<quint16>(e + 2);
                    const quint16 size = qFromLittleEndian<quint16>(e + 4);
                    if (at + size > data.size()) break;
                    p.data = data.mid(at, size);
                    at += size;
                    list.append(p);
                }
            }
        }
    }
    return properties_.insert(entry, list).value();
}

Object File::header() const { return root().object(QStringLiteral("Header")); }
Object File::content() const { return header().object(QStringLiteral("Content")); }
Object File::dictionary() const { return header().object(QStringLiteral("Dictionary")); }
QList<Object> File::mobs() const { return content().objects(QStringLiteral("Mobs")); }

Object File::resolve(const WeakKey& key) const {
    if (key.table < 0 || key.table >= referenced_.size()) return {};
    auto it = targets_.find(key.table);
    if (it == targets_.end()) {
        // Follow the path of strong references from the root to the set.
        QHash<QByteArray, Object> targets;
        const QList<quint16>& path = referenced_[key.table];
        Object at = root();
        for (qsizetype i = 0; i + 1 < path.size() && at; ++i) {
            const auto p = at.raw(path[i]);
            const QList<Object> next = p ? at.referenced(*p) : QList<Object>{};
            at = next.isEmpty() ? Object() : next.first();
        }
        if (at && !path.isEmpty()) {
            const auto set = at.raw(path.last());
            if (set) {
                // The set's index lists each element's key with its local key.
                const int index = cfb_->child(at.entry(), utf16(set->data) + QLatin1String(" index"));
                const QByteArray data = index >= 0 ? cfb_->readAll(index) : QByteArray();
                if (data.size() >= 15) {
                    const quint32 count = qFromLittleEndian<quint32>(data.constData());
                    const int keySize = static_cast<quint8>(data[14]);
                    qsizetype pos = 15;
                    const QString name = utf16(set->data);
                    for (quint32 i = 0; i < count && pos + 8 + keySize <= data.size(); ++i, pos += 8 + keySize) {
                        const quint32 local = qFromLittleEndian<quint32>(data.constData() + pos);
                        const int child = cfb_->child(at.entry(), QStringLiteral("%1{%2}").arg(name).arg(local, 0, 16));
                        if (child >= 0) targets.insert(data.mid(pos + 8, keySize), Object(this, child));
                    }
                }
            }
        }
        it = targets_.insert(key.table, targets);
    }
    return it.value().value(key.key);
}

QVariant File::decode(const QUuid& type, const QByteArray& data) const {
    const TypeDef* t = meta_->typeDef(type);
    if (!t) return data;
    switch (t->kind) {
    case TypeKind::Int: {
        if (data.size() != t->size) return data;
        const char* p = data.constData();
        switch (t->size) {
        case 1: return t->isSigned ? QVariant(qint64(static_cast<qint8>(p[0]))) : QVariant(quint64(static_cast<quint8>(p[0])));
        case 2: return t->isSigned ? QVariant(qint64(qFromLittleEndian<qint16>(p))) : QVariant(quint64(qFromLittleEndian<quint16>(p)));
        case 4: return t->isSigned ? QVariant(qint64(qFromLittleEndian<qint32>(p))) : QVariant(quint64(qFromLittleEndian<quint32>(p)));
        case 8: return t->isSigned ? QVariant(qFromLittleEndian<qint64>(p)) : QVariant(qFromLittleEndian<quint64>(p));
        default: return data;
        }
    }
    case TypeKind::Character: return utf16(data);
    case TypeKind::GenericCharacter: return QString::fromLatin1(data);
    case TypeKind::String: {
        const TypeDef* e = meta_->typeDef(t->element);
        if (e && e->kind == TypeKind::GenericCharacter) {
            qsizetype n = data.size();
            while (n > 0 && data[n - 1] == '\0') --n;
            return QString::fromLatin1(data.left(n));
        }
        return utf16(data);
    }
    case TypeKind::Enum: {
        if (type == kBooleanType) return data.size() == 1 ? QVariant(data[0] == 1) : QVariant(data);
        const QVariant v = decode(t->element, data);
        if (v.typeId() != QMetaType::LongLong && v.typeId() != QMetaType::ULongLong) return data;
        const qint64 n = v.toLongLong();
        return t->values.contains(n) ? QVariant(t->values.value(n)) : QVariant(n);
    }
    case TypeKind::ExtEnum: {
        if (data.size() != 16) return data;
        const QUuid u = auidFromBytes(data.constData());
        return t->extValues.contains(u) ? QVariant(t->extValues.value(u)) : QVariant(u);
    }
    case TypeKind::Record: {
        if (type == kAuidType) return data.size() == 16 ? QVariant(auidFromBytes(data.constData())) : QVariant(data);
        if (type == kMobIdType) {
            return data.size() == 32 ? QVariant::fromValue(mobIdFromBytes(data.constData())) : QVariant(data);
        }
        QVariantMap out;
        qsizetype at = 0;
        for (const auto& m : t->members) {
            const int size = meta_->byteSize(m.second);
            if (size <= 0 || at + size > data.size()) return data;
            out.insert(m.first, decode(m.second, data.mid(at, size)));
            at += size;
        }
        return at == data.size() ? QVariant(out) : QVariant(data);
    }
    case TypeKind::FixedArray:
    case TypeKind::VarArray:
    case TypeKind::Set: {
        if (t->kind == TypeKind::VarArray && t->element == kCharacterType) return utf16List(data);
        const int size = meta_->byteSize(t->element);
        if (size <= 0 || data.size() % size != 0) return data;
        if (t->kind == TypeKind::FixedArray && data.size() != static_cast<qsizetype>(t->size) * size) return data;
        QVariantList out;
        for (qsizetype at = 0; at < data.size(); at += size) out.append(decode(t->element, data.mid(at, size)));
        return out;
    }
    case TypeKind::Rename: return decode(t->element, data);
    case TypeKind::Indirect:
    case TypeKind::Opaque: {
        if (data.size() < 17 || static_cast<quint8>(data[0]) != kLittleEndian) return data;
        const QUuid inner = auidFromBytes(data.constData() + 1);
        if (!meta_->typeDef(inner)) return data;
        return decode(inner, data.mid(17));
    }
    default: return data;
    }
}

TypeDef File::readTypeDef(const Object& o) const {
    TypeDef t;
    t.kind = typeDefClasses().value(o.classAuid(), TypeKind::Opaque);
    auto data = [&](quint16 pid) {
        const auto p = o.raw(pid);
        return p ? p->data : QByteArray();
    };
    // A reference to another definition: a weak reference, or the bare AUID.
    auto ref = [&](quint16 pid) -> QUuid {
        const auto p = o.raw(pid);
        if (!p) return {};
        if (p->form == static_cast<quint16>(StoredForm::WeakRef)) {
            const auto k = o.weakKey(*p);
            return k && k->key.size() == 16 ? auidFromBytes(k->key.constData()) : QUuid();
        }
        return p->data.size() == 16 ? auidFromBytes(p->data.constData()) : QUuid();
    };
    const QByteArray id = data(PidIdentification);
    if (id.size() == 16) t.auid = auidFromBytes(id.constData());
    t.name = utf16(data(PidName));
    switch (t.kind) {
    case TypeKind::Int: {
        const QByteArray s = data(PidIntSize);
        const QByteArray sg = data(PidIntSigned);
        t.size = s.size() == 1 ? static_cast<quint8>(s[0]) : 0;
        t.isSigned = sg.size() == 1 && sg[0] == 1;
        break;
    }
    case TypeKind::StrongRef: t.element = ref(PidStrongRefType); break;
    case TypeKind::WeakRef: {
        t.element = ref(PidWeakRefType);
        const QByteArray path = data(PidWeakRefTargetSet);
        for (qsizetype at = 0; at + 16 <= path.size(); at += 16) t.targetPath.append(auidFromBytes(path.constData() + at));
        break;
    }
    case TypeKind::Enum: {
        t.element = ref(PidEnumType);
        const QStringList names = utf16List(data(PidEnumNames));
        const QByteArray values = data(PidEnumValues);
        for (qsizetype i = 0; i < names.size() && 8 * i + 8 <= values.size(); ++i) {
            t.values.insert(qFromLittleEndian<qint64>(values.constData() + 8 * i), names[i]);
        }
        break;
    }
    case TypeKind::FixedArray: {
        t.element = ref(PidFixedArrayType);
        const QByteArray c = data(PidFixedArrayCount);
        t.size = c.size() == 4 ? static_cast<int>(qFromLittleEndian<quint32>(c.constData())) : 0;
        break;
    }
    case TypeKind::VarArray: t.element = ref(PidVarArrayType); break;
    case TypeKind::Set: t.element = ref(PidSetType); break;
    case TypeKind::String: t.element = ref(PidStringType); break;
    case TypeKind::Rename: t.element = ref(PidRenameType); break;
    case TypeKind::Record: {
        const QStringList names = utf16List(data(PidRecordNames));
        const auto types = o.raw(PidRecordTypes);
        const QList<WeakKey> keys = types ? o.weakKeys(*types) : QList<WeakKey>{};
        for (qsizetype i = 0; i < names.size() && i < keys.size(); ++i) {
            t.members.append({names[i], keys[i].key.size() == 16 ? auidFromBytes(keys[i].key.constData()) : QUuid()});
        }
        break;
    }
    case TypeKind::ExtEnum: {
        const QStringList names = utf16List(data(PidExtEnumNames));
        const QByteArray values = data(PidExtEnumValues);
        for (qsizetype i = 0; i < names.size() && 16 * i + 16 <= values.size(); ++i) {
            t.extValues.insert(auidFromBytes(values.constData() + 16 * i), names[i]);
        }
        break;
    }
    case TypeKind::GenericCharacter: {
        // Its size has a local ID of the file's choosing: the one 1-byte property.
        for (const RawProperty& p : o.properties()) {
            if (p.pid >= 0x8000 && p.data.size() == 1) t.size = static_cast<quint8>(p.data[0]);
        }
        if (t.size == 0) t.size = 1;
        break;
    }
    default: break;
    }
    return t;
}

void File::readMetaDictionary() {
    const Object meta = root().object(QStringLiteral("MetaDictionary"));
    if (!meta) return;
    auto bytes = [](const Object& o, quint16 pid) {
        const auto p = o.raw(pid);
        return p ? p->data : QByteArray();
    };
    auto flag = [&](const Object& o, quint16 pid, bool fallback) {
        const QByteArray b = bytes(o, pid);
        return b.size() == 1 ? b[0] == 1 : fallback;
    };
    // Types first: the classes' properties name them.
    if (const auto types = meta.raw(PidTypeDefinitions)) {
        for (const Object& o : meta.referenced(*types)) {
            const TypeDef t = readTypeDef(o);
            if (!t.auid.isNull()) meta_->addType(t);
        }
    }
    if (const auto classes = meta.raw(PidClassDefinitions)) {
        for (const Object& o : meta.referenced(*classes)) {
            ClassDef c;
            const QByteArray id = bytes(o, PidIdentification);
            if (id.size() != 16) continue;
            c.auid = auidFromBytes(id.constData());
            c.name = utf16(bytes(o, PidName));
            c.concrete = flag(o, PidIsConcrete, false);
            if (const auto parent = o.raw(PidParentClass)) {
                const auto k = o.weakKey(*parent);
                if (k && k->key.size() == 16) c.parent = auidFromBytes(k->key.constData());
                if (c.parent == c.auid) c.parent = QUuid();
            }
            if (const auto props = o.raw(PidProperties)) {
                for (const Object& po : o.referenced(*props)) {
                    PropertyDef p;
                    const QByteArray pid = bytes(po, PidIdentification);
                    if (pid.size() != 16) continue;
                    p.auid = auidFromBytes(pid.constData());
                    p.name = utf16(bytes(po, PidName));
                    const QByteArray local = bytes(po, PidLocalId);
                    p.pid = local.size() == 2 ? qFromLittleEndian<quint16>(local.constData()) : -1;
                    const QByteArray type = bytes(po, PidType);
                    if (type.size() == 16) {
                        p.type = auidFromBytes(type.constData());
                    } else if (const auto tp = po.raw(PidType)) {
                        const auto k = po.weakKey(*tp);
                        if (k && k->key.size() == 16) p.type = auidFromBytes(k->key.constData());
                    }
                    p.optional = flag(po, PidIsOptional, true);
                    p.unique = flag(po, PidIsUniqueIdentifier, false);
                    c.properties.append(p);
                }
            }
            meta_->addClass(c);
        }
    }
}

} // namespace lumenis::interop::aaf
