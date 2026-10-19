#pragma once

#include "lumenis/interop/aaf.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <map>

// Writing AAF (docs/spec/aaf.md, "Writing"): objects built in memory by
// class and property name, their values encoded by the metadictionary's
// types, then written as one compound file with a metadictionary that
// defines every class and type, as the reader expects of any writer.
namespace lumenis::interop::aaf {

// Builds an AAF file. The constructor makes the Root, the Header, its ContentStorage (with an empty Mobs
// set) and its Dictionary; the caller adds objects with create() and links them in with the setters, and
// toBytes() or save() writes the file. Errors are kept (errors()), and a builder with any writes nothing.
// aaf_export.cpp uses it to write bins.
class Builder {
public:
    using Ref = int; // an object being built; -1 for none

    // Gives every property of the model that has no local ID one (MetaDictionary::assignDynamicPids).
    explicit Builder(MetaDictionary meta = MetaDictionary::builtIn());

    // The model the builder encodes and defines.
    const MetaDictionary& metaDictionary() const { return meta_; }

    // A new object of a concrete class, not yet referenced from anywhere.
    Ref create(const QString& className);
    // The object's class name; empty for no object.
    QString className(Ref object) const;

    // A data property, encoded by its type: the forms File::decode returns
    // (integers, bool, enumeration element names, QString, QUuid, MobId,
    // QVariantMap records, QVariantList arrays). Records' members may be
    // given as ints. False, with an error kept, when the class has no such
    // property or the value does not fit its type.
    bool set(Ref object, const QString& property, const QVariant& value);
    // An indirect property's value, of the named type.
    bool setIndirect(Ref object, const QString& property, const QString& typeName, const QVariant& value);
    // A strong reference to `child`, or one more in a strong reference
    // vector or set. Each object has one owner.
    bool setObject(Ref object, const QString& property, Ref child);
    bool append(Ref object, const QString& property, Ref child);
    // An empty strong reference vector or set (a required one may be empty).
    bool setEmpty(Ref object, const QString& property);
    // A weak reference to the object with `key` (its unique identifier,
    // an AUID: references to definitions) in the set the property's type
    // targets; or one more in a weak reference vector or set.
    bool setWeak(Ref object, const QString& property, const QUuid& key);
    bool appendWeak(Ref object, const QString& property, const QUuid& key);

    // The Header, its ContentStorage and Dictionary, made by the constructor.
    Ref header() const { return header_; }
    Ref content() const { return content_; }
    Ref dictionary() const { return dictionary_; }

    // What a reader would find wrong: required properties not set, by
    // class and property name ("SourceClip: Length").
    QStringList missing() const;
    // Every error kept by the calls above, in order.
    QStringList errors() const { return errors_; }

    // The whole file: the objects from the Root down, the `referenced properties` table, and a
    // MetaDictionary defining every class and type of the model, which is made anew on each call. Empty,
    // with `error` set, when any call has failed or a reference cannot be written.
    QByteArray toBytes(QString* error = nullptr);
    // Writes toBytes() to `path` through QSaveFile.
    bool save(const QString& path, QString* error = nullptr);

private:
    // How a property is stored: data, or a strong or weak reference, vector or set.
    enum class Kind { Data, Strong, StrongVector, StrongSet, Weak, WeakVector, WeakSet };
    // A property set on an object being built: data's bytes, already encoded, or the objects (strong) or
    // keys (weak) it references.
    struct Property {
        Kind kind = Kind::Data;
        QString name;
        QByteArray data;
        QList<Ref> children;
        QList<QByteArray> keys; // weak references' targets
        QUuid type;             // the property's type
    };
    // An object being built: its class, its properties by pid (so they are written in pid order), and the
    // object that references it.
    struct Object {
        QUuid classAuid;
        std::map<int, Property> properties; // by pid
        Ref owner = -1;
    };

    // The property's definition in the object's class or its parents; null, with an error kept, when
    // there is none or it has no local ID.
    const PropertyDef* property(Ref object, const QString& name);
    bool fail(const QString& message);
    // A value as bytes by type, as File::decode reads them back; `ok` is set false when it does not fit.
    QByteArray encode(const QUuid& type, const QVariant& value, bool* ok) const;
    // The property, made ready to hold a reference of the right kind (single, vector or set by its type);
    // null, with an error kept, when its type is not a reference of the `single` kind (strong or weak).
    Property* referenceProperty(Ref object, const QString& name, Kind single, Kind vector, Kind set);
    // The index in tables_ of the weak reference type's target path, as pids, added when new; -1 when a
    // step has no pid.
    int weakTable(const QUuid& weakRefType);
    // The local ID of a property, found by its AUID in any class.
    int pidOfProperty(const QUuid& propertyAuid) const;
    // The value of the object's unique identifier property (its key in a set); empty when not set.
    QByteArray uniqueKey(Ref object) const;
    // The pid and size of the key of the class a reference type names.
    std::pair<int, int> keyOf(const QUuid& referenceType) const;
    // Adds a ClassDefinition for every class (but Root) and a TypeDefinition for every type to `meta`.
    void addMetaDictionary(Ref meta);

    MetaDictionary meta_;
    QList<Object> objects_;
    QStringList errors_;
    // The weak reference tables: each a path of pids, as `referenced properties` stores them.
    QList<QList<quint16>> tables_;
    Ref root_ = -1;
    Ref header_ = -1;
    Ref content_ = -1;
    Ref dictionary_ = -1;
};

} // namespace lumenis::interop::aaf
