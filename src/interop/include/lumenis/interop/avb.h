#pragma once

#include "lumenis/core/mob_id.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QUuid>

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

// Media Composer's bin files (.avb), the object layer (Phase 3.3a,
// docs/spec/avb.md). A bin is a table of objects ("chunks"), each a class
// ID (a fourcc such as CMPO or SCLP), a size and a payload, referring to one
// another by their 1-based index in the table. The reader keeps the file in
// memory, parses objects on demand and caches them.
//
// Objects are generic: a class ID and an ordered list of named properties,
// named as pyavb (MIT, Mark Reid) names them, so that a dump of either
// reader can be compared with the other's (scripts/avb-dump.py). Each
// class's layout is ported from pyavb's read() methods; what pyavb asserts
// is checked, and a failed check makes that object unreadable (Object::error)
// rather than the whole file. Reading what the bin means (clips, sequences,
// sources) into the native model is the next layer (3.3b).
// The writer, avb_writer.h, is the inverse of this reader (3.4a).
namespace lumenis::interop::avb {

class Value;
struct Property;
using ValueList = std::vector<Value>;
using Properties = std::vector<Property>;

// A reference to another object of the same file: its index in the object
// table, 0 for none.
struct Ref {
    std::uint32_t index = 0;
    friend bool operator==(const Ref&, const Ref&) = default;
};

// A time as bins store it: seconds since 1970-01-01 UTC (unsigned 32 bits).
struct Time {
    std::uint32_t seconds = 0;
    QDateTime toDateTime() const;
    friend bool operator==(const Time&, const Time&) = default;
};

// An unsigned 64-bit integer: kept apart from qint64, which cannot hold the
// upper half of its range.
struct UInt64 {
    std::uint64_t value = 0;
    friend bool operator==(const UInt64&, const UInt64&) = default;
};

// One property value. Lists hold values (a column list, a sequence's
// components as Refs); records hold named values (the helper records pyavb
// keeps in lists: bin items, tracks, control points; and its dicts).
class Value {
public:
    using Data = std::variant<std::monostate, bool, qint64, UInt64, double, QString, QByteArray, MobId, QUuid, Time,
                              Ref, std::shared_ptr<const ValueList>, std::shared_ptr<const Properties>>;

    Value() = default;
    static Value boolean(bool b) { return Value(Data(b)); }
    static Value integer(qint64 i) { return Value(Data(i)); }
    static Value uint64(std::uint64_t u) { return Value(Data(UInt64{u})); }
    static Value real(double d) { return Value(Data(d)); }
    static Value string(QString s) { return Value(Data(std::move(s))); }
    static Value bytes(QByteArray b) { return Value(Data(std::move(b))); }
    static Value mobId(const MobId& m) { return Value(Data(m)); }
    static Value uuid(const QUuid& u) { return Value(Data(u)); }
    static Value time(std::uint32_t seconds) { return Value(Data(Time{seconds})); }
    static Value ref(std::uint32_t index) { return Value(Data(Ref{index})); }
    static Value list(ValueList l) { return Value(Data(std::make_shared<const ValueList>(std::move(l)))); }
    static Value record(Properties p) { return Value(Data(std::make_shared<const Properties>(std::move(p)))); }

    const Data& data() const { return data_; }
    bool isNull() const { return std::holds_alternative<std::monostate>(data_); }
    // The integer, Boolean or unsigned value as a qint64 (unsigned values
    // past its range wrap), else 0.
    qint64 toInt() const;
    double toDouble() const;      // a double or an integer, else 0
    QString toString() const;     // a string, else empty
    QByteArray toBytes() const;   // bytes, else empty
    std::optional<MobId> toMobId() const;
    std::optional<QUuid> toUuid() const;
    std::optional<Ref> toRef() const;
    std::optional<Time> toTime() const;
    const ValueList& toList() const;     // empty unless a list
    const Properties& toRecord() const;  // empty unless a record
    // A record's member by name; null when there is none.
    const Value& member(const QString& name) const;

private:
    explicit Value(Data d) : data_(std::move(d)) {}
    Data data_;
};

struct Property {
    QString name;
    Value value;
};

// The value of a property by name in an ordered property list, or nullptr.
const Value* find(const Properties& properties, const QString& name);
// Sets a property as Python sets an attribute (and as the reader sets
// them): a name already set keeps its place and takes the new value; a new
// one goes at the end. The writer (avb_writer.h) finds properties by name,
// so the order matters only to the dump.
void set(Properties& properties, const QString& name, Value value);

// The file header (docs/spec/avb.md, "The container"), less the object
// count and the root index, which belong to the table. What File reads and
// what the writer (avb_writer.h) writes.
struct Header {
    bool littleEndian = true;
    // The last save as text, "2022/07/01 06:07:17", as written (Media
    // Composer writes local time). Empty when writing: lastSave in local time.
    QString lastSaveText;
    Time lastSave;
    // The 4 bytes after the last save time: not zero in Media Composer's
    // bins, meaning unknown; pyavb skips them and writes zeros. Written as
    // they are; empty: zeros.
    QByteArray unknown;
    // The creator, "Media Composer 18.9.0.49510", stored in a 30-byte field
    // padded with spaces (the reader keeps the padding).
    QString creatorVersion;
    // 16 reserved bytes (zeros in every bin seen); empty: zeros.
    QByteArray reserved;
};

// One object of the table.
struct Object {
    std::uint32_t index = 0;  // its place in the table, from 1
    QString classId;          // the fourcc, "CMPO"
    // Its properties in the order pyavb sets them. An attribute list (ATTR)
    // has its attributes here, by attribute name.
    Properties properties;
    // A parameter list (PRLS) is a plain list of references instead.
    bool isList = false;
    ValueList items;
    // Why it could not be read ("CMPO: expected tag 0x03, found 0x01 at
    // byte 77"); empty when it was. An unreadable object has no properties.
    QString error;

    bool ok() const { return error.isEmpty(); }
    // A property by name; a null Value when it is not set.
    const Value& property(const QString& name) const;
};

// A bin file. Opening reads the header and the object table; objects are
// parsed when they are first asked for. It is made for files from anywhere,
// damaged ones included: whatever the bytes, reading never goes past the
// data, and loops in references are left to the caller (dump guards them).
class File {
public:
    File() = default;
    // Reads `path` (or `data`, the whole file). False, with the reason in
    // `error`, when it is not a bin or its object table is cut short.
    bool open(const QString& path, QString* error = nullptr);
    bool load(QByteArray data, QString* error = nullptr);

    bool isOpen() const { return !data_.isEmpty(); }
    bool littleEndian() const { return little_; }
    QString lastSaveText() const { return lastSaveText_; } // "2022/07/01 06:07:17", as written
    Time lastSave() const { return lastSave_; }
    QString creatorVersion() const { return creatorVersion_; }
    std::uint32_t rootIndex() const { return rootIndex_; }
    // The whole header, for writing the file again (avb_writer.h).
    Header header() const;

    // The number of objects in the table; valid indexes are 1..objectCount().
    std::uint32_t objectCount() const { return static_cast<std::uint32_t>(positions_.size()); }
    // An object's class ID without parsing it ("" for an invalid index).
    QString classIdAt(std::uint32_t index) const;
    // The object at `index`, parsed on first use. An invalid index gives an
    // object with an error. References stay valid while the File lives.
    const Object& object(std::uint32_t index) const;
    const Object& object(const Ref& ref) const { return object(ref.index); }
    // The root object: the Bin (ABIN).
    const Object& root() const { return object(rootIndex_); }
    // An object's payload as stored (after its class ID and size): what the
    // writer copies for an object that could not be read. Empty for an
    // invalid index or a payload cut short.
    QByteArray payload(std::uint32_t index) const;

private:
    QByteArray data_;
    bool little_ = true;
    QString lastSaveText_;
    Time lastSave_;
    QString creatorVersion_;
    QByteArray headerUnknown_;
    QByteArray headerReserved_;
    std::uint32_t rootIndex_ = 0;
    std::vector<qsizetype> positions_; // each object's chunk header, from index 1
    mutable std::vector<std::unique_ptr<Object>> cache_;
};

// Strings as bins store them: Mac OS Roman (pyavb's default).
QString fromMacRoman(const QByteArray& bytes);
// The inverse, for writing: nullopt when a character has no Mac OS Roman
// byte (Python's encode('macroman') raises then).
std::optional<QByteArray> toMacRoman(const QString& text);
// Strict UTF-8, as Python decodes it: nullopt for invalid input.
std::optional<QString> fromUtf8Strict(const QByteArray& bytes);
// A double as Python's repr() writes it: "25.0", "23.976", "1e-05", "inf".
QString pythonRepr(double value);

// The root object with everything it refers to, as JSON (docs/spec/avb.md,
// "Dumps"): the form scripts/avb-dump.py makes from pyavb's reading. Write it
// with aaf::canonicalJson to compare dumps byte for byte.
QJsonValue dump(const File& file);
// Any object of the file, in the same form.
QJsonValue dumpObject(const File& file, std::uint32_t index);

} // namespace lumenis::interop::avb
