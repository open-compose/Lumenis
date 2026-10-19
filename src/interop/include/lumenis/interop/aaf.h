#pragma once

#include "lumenis/core/mob_id.h"
#include "lumenis/interop/cfb.h"

#include <QByteArray>
#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QString>
#include <QUuid>
#include <QVariant>

#include <memory>
#include <optional>

// AAF's object layer (docs/spec/aaf.md): objects stored as storages of a
// compound file, their properties, and the metadictionary that says what
// classes, properties and types there are. Written after the AAF Low-Level
// Container and Object Specifications, with pyaaf2 (MIT) as the reference.
namespace lumenis::interop::aaf {

// AUIDs are kept as QUuids. In files they are stored as GUIDs are: the first
// three fields little-endian.
QUuid auidFromBytes(const char* sixteen);
QByteArray auidBytes(const QUuid& auid);
// "0d010101-0101-0100-060e-2b3402060101", as pyaaf2 and the AAF SDK print them.
QString auidText(const QUuid& auid);
QUuid auidFromText(const char* text);

// A MobID as AAF stores it (32 bytes: the UMID's first 16, then its
// material number as a GUID) and as SMPTE ST 330 orders it.
MobId mobIdFromBytes(const char* thirtyTwo);
QByteArray mobIdBytes(const MobId& mob);

enum class TypeKind {
    Int, Character, GenericCharacter, String, Enum, ExtEnum, Record, FixedArray, VarArray, Set,
    Rename, Indirect, Opaque, Stream, StrongRef, WeakRef,
};

struct TypeDef {
    QUuid auid;
    QString name;
    TypeKind kind = TypeKind::Int;
    int size = 0;           // Int, GenericCharacter: bytes; FixedArray: elements
    bool isSigned = false;  // Int
    QUuid element;          // the element, renamed, string's or enumeration's type; a reference's class
    QList<QPair<QString, QUuid>> members; // Record
    QMap<qint64, QString> values;         // Enum
    QHash<QUuid, QString> extValues;      // ExtEnum
    QList<QUuid> targetPath;              // WeakRef: the properties leading to its target set
};

struct PropertyDef {
    QUuid auid;
    QString name;
    int pid = -1; // -1: not given a local ID yet (an extension the file does not use)
    QUuid type;
    bool optional = true;
    bool unique = false;
};

struct ClassDef {
    QUuid auid;
    QString name;
    QUuid parent; // null for the root of the hierarchy
    bool concrete = false;
    QList<PropertyDef> properties;
};

// The classes and types: AAF's baseline and Avid's extensions, built in
// (aaf_model.inc), then what a file's own metadictionary adds.
class MetaDictionary {
public:
    static MetaDictionary builtIn();

    const ClassDef* classDef(const QUuid& auid) const;
    const ClassDef* classDef(const QString& name) const;
    const TypeDef* typeDef(const QUuid& auid) const;
    const TypeDef* typeDef(const QString& name) const;
    // A property of the class or one of the classes it derives from.
    const PropertyDef* property(const QUuid& classAuid, int pid) const;
    const PropertyDef* property(const QUuid& classAuid, const QString& name) const;
    // Whether `auid` is the class `base` or derives from it.
    bool isA(const QUuid& auid, const QUuid& base) const;
    // The fixed size of a value of the type in bytes; 0 when it varies.
    int byteSize(const QUuid& type) const;

    void addClass(const ClassDef& c);
    void addType(const TypeDef& t);
    int classCount() const { return static_cast<int>(classes_.size()); }
    int typeCount() const { return static_cast<int>(types_.size()); }

private:
    QHash<QUuid, ClassDef> classes_;
    QHash<QString, QUuid> classNames_;
    QHash<QUuid, TypeDef> types_;
    QHash<QString, QUuid> typeNames_;
};

// How a property is stored (the AAF Low-Level Container Specification's
// stored forms).
enum class StoredForm : quint16 {
    Data = 0x82,
    Stream = 0x42,
    StrongRef = 0x22,
    StrongRefVector = 0x32,
    StrongRefSet = 0x3A,
    WeakRef = 0x02,
    WeakRefVector = 0x12,
    WeakRefSet = 0x1A,
    WeakRefStoredObjectId = 0x03,
    UniqueObjectId = 0x86,
    OpaqueStream = 0x40,
    DataVector = 0xD2,
    DataSet = 0xDA,
};

struct RawProperty {
    quint16 pid = 0;
    quint16 form = 0;
    QByteArray data;
};

// A weak reference: the key of the object it names, in its target set.
struct WeakKey {
    int table = -1; // index in the file's referenced properties
    quint16 keyPid = 0;
    QByteArray key; // 16 bytes (an AUID) or 32 (a MobID)
    QString text() const;
};

class File;

// An object in a file: a cheap handle, valid while its File is open.
class Object {
public:
    Object() = default;
    Object(const File* file, int entry) : file_(file), entry_(entry) {}

    bool isNull() const { return !file_ || entry_ < 0; }
    explicit operator bool() const { return !isNull(); }
    int entry() const { return entry_; }
    QString path() const;

    QUuid classAuid() const;
    QString className() const;
    const ClassDef* classDef() const;
    // Whether the object's class is `name` or derives from it.
    bool isA(const QString& name) const;

    QList<RawProperty> properties() const;
    std::optional<RawProperty> raw(int pid) const;
    std::optional<RawProperty> raw(const QString& name) const;
    bool has(const QString& name) const { return raw(name).has_value(); }
    // The property's name from the metadictionary, or "0x" and its hex pid.
    QString propertyName(int pid) const;
    const PropertyDef* propertyDef(int pid) const;

    // A property's value (see File::decode); a strong reference's object or
    // objects; a weak reference's target when it can be found. Empty when
    // the object has no such property.
    QVariant value(const QString& name) const;
    QVariant value(const RawProperty& p) const;
    Object object(const QString& name) const;
    QList<Object> objects(const QString& name) const;
    std::optional<WeakKey> weakKey(const QString& name) const;
    QList<WeakKey> weakKeys(const RawProperty& p) const;
    std::optional<WeakKey> weakKey(const RawProperty& p) const;
    // Typed shortcuts; the fallback when the property is missing or not that type.
    QString string(const QString& name, const QString& fallback = {}) const;
    qint64 integer(const QString& name, qint64 fallback = 0) const;
    MobId mobId(const QString& name) const;
    QUuid auid(const QString& name) const;

    // A strong reference's objects, from the property's stored form.
    QList<Object> referenced(const RawProperty& p) const;
    // A stream property's compound file stream, or -1.
    int streamEntry(const RawProperty& p) const;

    friend bool operator==(const Object& a, const Object& b) { return a.file_ == b.file_ && a.entry_ == b.entry_; }

private:
    const File* file_ = nullptr;
    int entry_ = -1;
};

// An AAF file, read. Objects are read when they are first asked for.
class File {
public:
    File();
    ~File();
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    bool open(const QString& path, QString* error = nullptr);
    void close();
    QString errorString() const { return error_; }

    Object root() const { return Object(this, cfb::Reader::root()); }
    Object header() const;
    Object content() const;     // the Header's Content (ContentStorage)
    Object dictionary() const;  // the Header's Dictionary
    QList<Object> mobs() const; // the Content's Mobs

    const MetaDictionary& metaDictionary() const { return *meta_; }
    cfb::Reader& container() const { return *cfb_; }

    // A data property's value as its type says:
    // * integers: qint64, or quint64 when unsigned;
    // * Boolean: bool; other enumerations: the element's name (its value
    //   when it has none); extensible enumerations: the name, or the AUID;
    // * strings and characters: QString; arrays of strings: QStringList;
    // * AUID: QUuid; MobIDType: MobId; other records: QVariantMap of members;
    // * arrays and sets: QVariantList; indirect values: what they hold.
    // A value that cannot be decoded comes back as its bytes (QByteArray).
    QVariant decode(const QUuid& type, const QByteArray& data) const;

    // The object a weak reference names, if it is there.
    Object resolve(const WeakKey& key) const;

    // Parsed properties of a storage; cached.
    const QList<RawProperty>& propertiesOf(int entry) const;

private:
    bool readReferencedProperties();
    void readMetaDictionary();
    TypeDef readTypeDef(const Object& o) const;

    std::unique_ptr<cfb::Reader> cfb_;
    std::unique_ptr<MetaDictionary> meta_;
    QString error_;
    QList<QList<quint16>> referenced_;
    mutable QHash<int, QList<RawProperty>> properties_;
    mutable QHash<int, QHash<QByteArray, Object>> targets_;
};

// The objects below the Header (or `from`), with every property, as JSON:
// the form scripts/aaf-dump.py makes with pyaaf2, for comparing the two
// (docs/spec/aaf.md, "Dumps").
QJsonValue dump(const File& file, const Object& from = {});
// JSON as Python's json.dumps(sort_keys=True, separators=(",", ":")) writes
// it (ensure_ascii), so that dumps can be compared by digest.
QByteArray canonicalJson(const QJsonValue& value);

} // namespace lumenis::interop::aaf

Q_DECLARE_METATYPE(lumenis::interop::aaf::Object)
Q_DECLARE_METATYPE(lumenis::MobId)
