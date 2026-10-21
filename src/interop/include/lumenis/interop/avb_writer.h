#pragma once

#include "lumenis/interop/avb.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

// Writing Media Composer bins (.avb), the object layer (Phase 3.4a,
// docs/spec/avb.md, "Writing"): the inverse of avb::File. Objects are the
// reader's: a class ID and properties named as pyavb names them, with
// references as indexes into the table being written. Each class's layout is
// ported from pyavb's write() methods (MIT, Mark Reid), so that every object
// the reader reads is written back to the same bytes: the property names are
// the contract between the two. What the bin means (clips, sequences) is
// the next layer (3.4b).
namespace lumenis::interop::avb {

// Builds the bytes of a bin: the header, then the object table in the order
// the objects were added (indexes from 1). Objects refer to one another by
// index, so a reference may point forward: add() returns the index at once
// and object() lets the caller fill in references later.
//
// Each class writes the properties its read() sets. A property it writes
// unconditionally that is missing is written as zero, empty or no reference
// (the defaults of an object made from scratch); an optional one (an
// extension: SCLP's mob_id, CMPO's creation_time, ...) is written only when
// it is set. Properties the class does not know are not written. A value of
// the wrong kind or out of its field's range is an error, not a crash.
class Builder {
public:
    explicit Builder(Header header = {});

    Header& header() { return header_; }
    const Header& header() const { return header_; }
    // The root object, the bin (ABIN); 0 (the default) for the last object.
    void setRoot(std::uint32_t index) { root_ = index; }
    std::uint32_t root() const { return root_ ? root_ : count(); }

    // A new object of a class the reader reads ("CMPO"), with its
    // properties; its index, from 1.
    std::uint32_t add(const QString& classId, Properties properties = {});
    // A parameter list (PRLS): references, Value::ref(index).
    std::uint32_t addList(ValueList items);
    // An object as File::object() gives it (the index it had is ignored).
    std::uint32_t add(const Object& object);
    // An object written as the given payload bytes, unread: one the reader
    // could not read, copied as it was (rewrite()). The payload is in the
    // byte order of the file it came from.
    std::uint32_t addOpaque(const QString& classId, QByteArray payload);

    std::uint32_t count() const { return static_cast<std::uint32_t>(entries_.size()); }
    // The object at an index from add(), to fill in later; an invalid index
    // is a programming error (asserted).
    Object& object(std::uint32_t index);

    // The whole file. Empty, with `error` set ("object 12 (CMPO): ..."), when
    // there is no object, the root is out of range, or an object cannot be
    // written.
    QByteArray toBytes(QString* error = nullptr) const;
    // Writes toBytes() to `path` through QSaveFile.
    bool save(const QString& path, QString* error = nullptr) const;

private:
    struct Entry {
        Object object;
        std::optional<QByteArray> opaque; // written as is
    };
    Header header_;
    std::uint32_t root_ = 0;
    std::vector<Entry> entries_;
};

// One object's payload (what follows its class ID and size in the table), in
// the byte order given, with references checked against `objectCount`.
// Empty, with `error` set, when it cannot be written.
QByteArray encodeObject(const Object& object, bool littleEndian, std::uint32_t objectCount, QString* error = nullptr);

// Every object of an open file written back as read, with its header and
// root: the same bytes as the file for every bin the reader reads in full
// (ctest avb_round_trip). Objects that could not be read are copied as they
// were stored, which needs the file's byte order; `littleEndian`, when
// given, writes the other order (then every object must have been read).
// Empty, with `error` set, on failure.
QByteArray rewrite(const File& file, QString* error = nullptr, std::optional<bool> littleEndian = std::nullopt);

} // namespace lumenis::interop::avb
