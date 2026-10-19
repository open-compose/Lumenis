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

class Builder {
public:
    using Ref = int; // an object being built; -1 for none

    explicit Builder(MetaDictionary meta = MetaDictionary::builtIn());

    const MetaDictionary& metaDictionary() const { return meta_; }

    // A new object of a concrete class, not yet referenced from anywhere.
    Ref create(const QString& className);
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
    // A weak reference to the object with `key` (its unique identifier:
    // an AUID, or a MobID) in the set the property's type targets; or one
    // more in a weak reference vector or set.
    bool setWeak(Ref object, const QString& property, const QUuid& key);
    bool appendWeak(Ref object, const QString& property, const QUuid& key);

    // The Header, its ContentStorage and Dictionary, made by the constructor.
    Ref header() const { return header_; }
    Ref content() const { return content_; }
    Ref dictionary() const { return dictionary_; }

    // What a reader would find wrong: required properties not set, by
    // class and property name ("SourceClip: Length").
    QStringList missing() const;
    QStringList errors() const { return errors_; }

    QByteArray toBytes(QString* error = nullptr);
    bool save(const QString& path, QString* error = nullptr);

private:
    enum class Kind { Data, Strong, StrongVector, StrongSet, Weak, WeakVector, WeakSet };
    struct Property {
        Kind kind = Kind::Data;
        QString name;
        QByteArray data;
        QList<Ref> children;
        QList<QByteArray> keys; // weak references' targets
        QUuid type;             // the property's type
    };
    struct Object {
        QUuid classAuid;
        std::map<int, Property> properties; // by pid
        Ref owner = -1;
    };

    const PropertyDef* property(Ref object, const QString& name);
    bool fail(const QString& message);
    QByteArray encode(const QUuid& type, const QVariant& value, bool* ok) const;
    Property* referenceProperty(Ref object, const QString& name, Kind single, Kind vector, Kind set);
    int weakTable(const QUuid& weakRefType);
    int pidOfProperty(const QUuid& propertyAuid) const;
    QByteArray uniqueKey(Ref object) const;
    // The pid and size of the key of the class a reference type names.
    std::pair<int, int> keyOf(const QUuid& referenceType) const;
    void addMetaDictionary(Ref meta);

    MetaDictionary meta_;
    QList<Object> objects_;
    QStringList errors_;
    QList<QList<quint16>> tables_;
    Ref root_ = -1;
    Ref header_ = -1;
    Ref content_ = -1;
    Ref dictionary_ = -1;
};

} // namespace lumenis::interop::aaf
