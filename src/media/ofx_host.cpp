// The OpenFX host's core (ROADMAP 6.11a; docs/spec/ofx-host.md). See lumenis/media/ofx_host.h.
//
// The host gives plug-ins the suites of the OpenFX headers (third_party/openfx) as C function tables. Every handle a
// plug-in sees is a pointer to one of the objects below: OfxPropertySetHandle a PropertySet, OfxImageEffectHandle an
// Effect, OfxParamSetHandle an Effect's ParamSet, OfxParamHandle a Param. Plug-ins are called from one thread at a time
// during the scan.

#include "lumenis/media/ofx_host.h"

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMemory.h>
#include <ofxMessage.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxParametricParam.h>
#include <ofxProperty.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLibrary>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace lumenis::media {

namespace {

// ---------------------------------------------------------------- property sets (ofxProperty.h)

// One property: a type fixed by its first set, and its values, one per dimension.
struct Property {
    enum class Type { Int, Double, String, Pointer } type = Type::Int;
    std::vector<int> ints;
    std::vector<double> doubles;
    std::vector<std::string> strings;
    std::vector<void*> pointers;
    std::size_t size() const {
        switch (type) {
        case Type::Int: return ints.size();
        case Type::Double: return doubles.size();
        case Type::String: return strings.size();
        case Type::Pointer: return pointers.size();
        }
        return 0;
    }
};

struct PropertySet {
    std::map<std::string, Property> props;

    // Values set by the host; a plug-in's later sets keep the type.
    void setInt(const std::string& name, std::vector<int> v) {
        Property& p = props[name];
        p.type = Property::Type::Int;
        p.ints = std::move(v);
    }
    void setDouble(const std::string& name, std::vector<double> v) {
        Property& p = props[name];
        p.type = Property::Type::Double;
        p.doubles = std::move(v);
    }
    void setString(const std::string& name, std::vector<std::string> v) {
        Property& p = props[name];
        p.type = Property::Type::String;
        p.strings = std::move(v);
    }
    void setPointer(const std::string& name, std::vector<void*> v) {
        Property& p = props[name];
        p.type = Property::Type::Pointer;
        p.pointers = std::move(v);
    }
    const Property* find(const std::string& name) const {
        const auto it = props.find(name);
        return it == props.end() ? nullptr : &it->second;
    }
    QString string(const char* name, int index = 0) const {
        const Property* p = find(name);
        if (!p || p->type != Property::Type::String || index >= static_cast<int>(p->strings.size())) return {};
        return QString::fromStdString(p->strings[static_cast<std::size_t>(index)]);
    }
    QStringList strings(const char* name) const {
        QStringList out;
        if (const Property* p = find(name); p && p->type == Property::Type::String) {
            for (const std::string& s : p->strings) out << QString::fromStdString(s);
        }
        return out;
    }
    std::vector<double> numbers(const char* name) const {
        std::vector<double> out;
        if (const Property* p = find(name)) {
            if (p->type == Property::Type::Double) out = p->doubles;
            if (p->type == Property::Type::Int) {
                for (int v : p->ints) out.push_back(v);
            }
        }
        return out;
    }
    int integer(const char* name, int fallback) const {
        const std::vector<double> v = numbers(name);
        return v.empty() ? fallback : static_cast<int>(v.front());
    }
};

PropertySet* set(OfxPropertySetHandle h) {
    return reinterpret_cast<PropertySet*>(h);
}
OfxPropertySetHandle handle(PropertySet* s) {
    return reinterpret_cast<OfxPropertySetHandle>(s);
}

// Gets a property of type `type` with at least `index + 1` values, creating it if a plug-in sets one the host did not.
Property* settable(OfxPropertySetHandle h, const char* name, Property::Type type, std::size_t index) {
    if (!h || !name) return nullptr;
    PropertySet* s = set(h);
    auto it = s->props.find(name);
    if (it == s->props.end()) {
        it = s->props.emplace(name, Property{}).first;
        it->second.type = type;
    }
    Property& p = it->second;
    if (p.type != type) return nullptr;
    switch (type) {
    case Property::Type::Int:
        if (p.ints.size() <= index) p.ints.resize(index + 1, 0);
        break;
    case Property::Type::Double:
        if (p.doubles.size() <= index) p.doubles.resize(index + 1, 0.0);
        break;
    case Property::Type::String:
        if (p.strings.size() <= index) p.strings.resize(index + 1);
        break;
    case Property::Type::Pointer:
        if (p.pointers.size() <= index) p.pointers.resize(index + 1, nullptr);
        break;
    }
    return &p;
}

const Property* gettable(OfxPropertySetHandle h, const char* name, Property::Type type, std::size_t index, OfxStatus& status) {
    status = kOfxStatOK;
    if (!h || !name) {
        status = kOfxStatErrBadHandle;
        return nullptr;
    }
    const Property* p = set(h)->find(name);
    if (!p) {
        status = kOfxStatErrUnknown;
        return nullptr;
    }
    if (p->type != type) {
        status = kOfxStatErrValue;
        return nullptr;
    }
    if (index >= p->size()) {
        status = kOfxStatErrBadIndex;
        return nullptr;
    }
    return p;
}

OfxStatus propSetPointer(OfxPropertySetHandle h, const char* name, int index, void* value) {
    Property* p = index < 0 ? nullptr : settable(h, name, Property::Type::Pointer, static_cast<std::size_t>(index));
    if (!p) return kOfxStatErrValue;
    p->pointers[static_cast<std::size_t>(index)] = value;
    return kOfxStatOK;
}
OfxStatus propSetString(OfxPropertySetHandle h, const char* name, int index, const char* value) {
    Property* p = index < 0 ? nullptr : settable(h, name, Property::Type::String, static_cast<std::size_t>(index));
    if (!p) return kOfxStatErrValue;
    p->strings[static_cast<std::size_t>(index)] = value ? value : "";
    return kOfxStatOK;
}
OfxStatus propSetDouble(OfxPropertySetHandle h, const char* name, int index, double value) {
    Property* p = index < 0 ? nullptr : settable(h, name, Property::Type::Double, static_cast<std::size_t>(index));
    if (!p) return kOfxStatErrValue;
    p->doubles[static_cast<std::size_t>(index)] = value;
    return kOfxStatOK;
}
OfxStatus propSetInt(OfxPropertySetHandle h, const char* name, int index, int value) {
    Property* p = index < 0 ? nullptr : settable(h, name, Property::Type::Int, static_cast<std::size_t>(index));
    // A plug-in may set an integer on a double property (both are numbers to it).
    if (!p) return propSetDouble(h, name, index, value);
    p->ints[static_cast<std::size_t>(index)] = value;
    return kOfxStatOK;
}
OfxStatus propSetPointerN(OfxPropertySetHandle h, const char* name, int count, void* const* value) {
    for (int i = 0; i < count; ++i) {
        if (const OfxStatus s = propSetPointer(h, name, i, value[i]); s != kOfxStatOK) return s;
    }
    return kOfxStatOK;
}
OfxStatus propSetStringN(OfxPropertySetHandle h, const char* name, int count, const char* const* value) {
    for (int i = 0; i < count; ++i) {
        if (const OfxStatus s = propSetString(h, name, i, value[i]); s != kOfxStatOK) return s;
    }
    return kOfxStatOK;
}
OfxStatus propSetDoubleN(OfxPropertySetHandle h, const char* name, int count, const double* value) {
    for (int i = 0; i < count; ++i) {
        if (const OfxStatus s = propSetDouble(h, name, i, value[i]); s != kOfxStatOK) return s;
    }
    return kOfxStatOK;
}
OfxStatus propSetIntN(OfxPropertySetHandle h, const char* name, int count, const int* value) {
    for (int i = 0; i < count; ++i) {
        if (const OfxStatus s = propSetInt(h, name, i, value[i]); s != kOfxStatOK) return s;
    }
    return kOfxStatOK;
}
OfxStatus propGetPointer(OfxPropertySetHandle h, const char* name, int index, void** value) {
    OfxStatus status = kOfxStatOK;
    const Property* p = index < 0 ? nullptr : gettable(h, name, Property::Type::Pointer, static_cast<std::size_t>(index), status);
    if (!p) return index < 0 ? kOfxStatErrBadIndex : status;
    *value = p->pointers[static_cast<std::size_t>(index)];
    return kOfxStatOK;
}
OfxStatus propGetString(OfxPropertySetHandle h, const char* name, int index, char** value) {
    OfxStatus status = kOfxStatOK;
    const Property* p = index < 0 ? nullptr : gettable(h, name, Property::Type::String, static_cast<std::size_t>(index), status);
    if (!p) return index < 0 ? kOfxStatErrBadIndex : status;
    // The string stays valid while the property set keeps it (the standard's rule).
    *value = const_cast<char*>(p->strings[static_cast<std::size_t>(index)].c_str());
    return kOfxStatOK;
}
OfxStatus propGetDouble(OfxPropertySetHandle h, const char* name, int index, double* value) {
    OfxStatus status = kOfxStatOK;
    const Property* p = index < 0 ? nullptr : gettable(h, name, Property::Type::Double, static_cast<std::size_t>(index), status);
    if (!p) {
        // An integer property read as a double.
        const Property* i = index < 0 ? nullptr : gettable(h, name, Property::Type::Int, static_cast<std::size_t>(index), status);
        if (!i) return index < 0 ? kOfxStatErrBadIndex : status;
        *value = i->ints[static_cast<std::size_t>(index)];
        return kOfxStatOK;
    }
    *value = p->doubles[static_cast<std::size_t>(index)];
    return kOfxStatOK;
}
OfxStatus propGetInt(OfxPropertySetHandle h, const char* name, int index, int* value) {
    OfxStatus status = kOfxStatOK;
    const Property* p = index < 0 ? nullptr : gettable(h, name, Property::Type::Int, static_cast<std::size_t>(index), status);
    if (!p) return index < 0 ? kOfxStatErrBadIndex : status;
    *value = p->ints[static_cast<std::size_t>(index)];
    return kOfxStatOK;
}
OfxStatus propGetPointerN(OfxPropertySetHandle h, const char* name, int count, void** value) {
    for (int i = 0; i < count; ++i) {
        if (const OfxStatus s = propGetPointer(h, name, i, &value[i]); s != kOfxStatOK) return s;
    }
    return kOfxStatOK;
}
OfxStatus propGetStringN(OfxPropertySetHandle h, const char* name, int count, char** value) {
    for (int i = 0; i < count; ++i) {
        if (const OfxStatus s = propGetString(h, name, i, &value[i]); s != kOfxStatOK) return s;
    }
    return kOfxStatOK;
}
OfxStatus propGetDoubleN(OfxPropertySetHandle h, const char* name, int count, double* value) {
    for (int i = 0; i < count; ++i) {
        if (const OfxStatus s = propGetDouble(h, name, i, &value[i]); s != kOfxStatOK) return s;
    }
    return kOfxStatOK;
}
OfxStatus propGetIntN(OfxPropertySetHandle h, const char* name, int count, int* value) {
    for (int i = 0; i < count; ++i) {
        if (const OfxStatus s = propGetInt(h, name, i, &value[i]); s != kOfxStatOK) return s;
    }
    return kOfxStatOK;
}
OfxStatus propReset(OfxPropertySetHandle h, const char* name) {
    if (!h || !name) return kOfxStatErrBadHandle;
    PropertySet* s = set(h);
    const auto it = s->props.find(name);
    if (it == s->props.end()) return kOfxStatErrUnknown;
    Property& p = it->second;
    p.ints.assign(p.ints.size(), 0);
    p.doubles.assign(p.doubles.size(), 0.0);
    p.strings.assign(p.strings.size(), std::string());
    p.pointers.assign(p.pointers.size(), nullptr);
    return kOfxStatOK;
}
OfxStatus propGetDimension(OfxPropertySetHandle h, const char* name, int* count) {
    if (!h || !name) return kOfxStatErrBadHandle;
    const Property* p = set(h)->find(name);
    if (!p) return kOfxStatErrUnknown;
    *count = static_cast<int>(p->size());
    return kOfxStatOK;
}

const OfxPropertySuiteV1 kPropertySuite{propSetPointer, propSetString,  propSetDouble,  propSetInt,     propSetPointerN,  propSetStringN,
                                        propSetDoubleN, propSetIntN,    propGetPointer, propGetString,  propGetDouble,    propGetInt,
                                        propGetPointerN, propGetStringN, propGetDoubleN, propGetIntN,   propReset,        propGetDimension};

// ---------------------------------------------------------------- effects, clips and parameters

struct Param {
    std::string type;
    std::string name;
    PropertySet props;
};

struct ParamSet {
    PropertySet props;
    std::vector<std::unique_ptr<Param>> params; // in the order defined
    Param* find(const std::string& name) {
        for (auto& p : params) {
            if (p->name == name) return p.get();
        }
        return nullptr;
    }
};

struct Clip {
    std::string name;
    PropertySet props;
};

// An effect descriptor: the describe action's, or one context's.
struct Effect {
    PropertySet props;
    ParamSet params;
    std::vector<std::unique_ptr<Clip>> clips;
};

Effect* effect(OfxImageEffectHandle h) {
    return reinterpret_cast<Effect*>(h);
}

// The defaults the standard gives a new parameter's properties, by type (ofxParam.h); the plug-in then sets its own.
void defaultParamProps(PropertySet& p, const std::string& type, const std::string& name) {
    p.setString(kOfxPropType, {kOfxTypeParameter});
    p.setString(kOfxPropName, {name});
    p.setString(kOfxPropLabel, {name});
    p.setString(kOfxParamPropType, {type});
    p.setString(kOfxParamPropHint, {""});
    p.setString(kOfxParamPropParent, {""});
    p.setInt(kOfxParamPropSecret, {0});
    p.setInt(kOfxParamPropAnimates, {1});
    p.setInt(kOfxParamPropCanUndo, {1});
    p.setInt(kOfxParamPropEvaluateOnChange, {1});
    if (type == kOfxParamTypeDouble || type == kOfxParamTypeDouble2D || type == kOfxParamTypeDouble3D) {
        const std::size_t n = type == kOfxParamTypeDouble ? 1 : type == kOfxParamTypeDouble2D ? 2 : 3;
        p.setDouble(kOfxParamPropDefault, std::vector<double>(n, 0.0));
        p.setDouble(kOfxParamPropMin, std::vector<double>(n, -1e300));
        p.setDouble(kOfxParamPropMax, std::vector<double>(n, 1e300));
        p.setDouble(kOfxParamPropDisplayMin, std::vector<double>(n, -1e300));
        p.setDouble(kOfxParamPropDisplayMax, std::vector<double>(n, 1e300));
        p.setString(kOfxParamPropDoubleType, {kOfxParamDoubleTypePlain});
    } else if (type == kOfxParamTypeInteger || type == kOfxParamTypeInteger2D || type == kOfxParamTypeInteger3D) {
        const std::size_t n = type == kOfxParamTypeInteger ? 1 : type == kOfxParamTypeInteger2D ? 2 : 3;
        p.setInt(kOfxParamPropDefault, std::vector<int>(n, 0));
        p.setInt(kOfxParamPropMin, std::vector<int>(n, -2147483647));
        p.setInt(kOfxParamPropMax, std::vector<int>(n, 2147483647));
        p.setInt(kOfxParamPropDisplayMin, std::vector<int>(n, -2147483647));
        p.setInt(kOfxParamPropDisplayMax, std::vector<int>(n, 2147483647));
    } else if (type == kOfxParamTypeBoolean || type == kOfxParamTypeChoice) {
        p.setInt(kOfxParamPropDefault, {0});
        if (type == kOfxParamTypeChoice) p.setString(kOfxParamPropChoiceOption, {});
        if (type == kOfxParamTypeBoolean) p.setInt(kOfxParamPropAnimates, {0});
    } else if (type == kOfxParamTypeRGB || type == kOfxParamTypeRGBA) {
        p.setDouble(kOfxParamPropDefault, std::vector<double>(type == kOfxParamTypeRGB ? 3 : 4, 0.0));
    } else if (type == kOfxParamTypeString) {
        p.setString(kOfxParamPropDefault, {""});
        p.setString(kOfxParamPropStringMode, {kOfxParamStringIsSingleLine});
        p.setInt(kOfxParamPropAnimates, {0});
    }
}

OfxStatus paramDefine(OfxParamSetHandle paramSet, const char* type, const char* name, OfxPropertySetHandle* props) {
    if (!paramSet || !type || !name) return kOfxStatErrBadHandle;
    auto* ps = reinterpret_cast<ParamSet*>(paramSet);
    if (ps->find(name)) return kOfxStatErrExists;
    static const char* const known[] = {kOfxParamTypeInteger,   kOfxParamTypeDouble,   kOfxParamTypeBoolean,    kOfxParamTypeChoice,
                                        kOfxParamTypeRGBA,      kOfxParamTypeRGB,      kOfxParamTypeDouble2D,   kOfxParamTypeInteger2D,
                                        kOfxParamTypeDouble3D,  kOfxParamTypeInteger3D, kOfxParamTypeString,    kOfxParamTypeCustom,
                                        kOfxParamTypeGroup,     kOfxParamTypePage,     kOfxParamTypePushButton, kOfxParamTypeStrChoice,
                                        kOfxParamTypeParametric, kOfxParamTypeBytes};
    if (std::none_of(std::begin(known), std::end(known), [&](const char* k) { return std::string(k) == type; })) return kOfxStatErrUnknown;
    auto p = std::make_unique<Param>();
    p->type = type;
    p->name = name;
    defaultParamProps(p->props, p->type, p->name);
    if (props) *props = handle(&p->props);
    ps->params.push_back(std::move(p));
    return kOfxStatOK;
}

OfxStatus paramGetHandle(OfxParamSetHandle paramSet, const char* name, OfxParamHandle* param, OfxPropertySetHandle* props) {
    if (!paramSet || !name) return kOfxStatErrBadHandle;
    Param* p = reinterpret_cast<ParamSet*>(paramSet)->find(name);
    if (!p) return kOfxStatErrUnknown;
    if (param) *param = reinterpret_cast<OfxParamHandle>(p);
    if (props) *props = handle(&p->props);
    return kOfxStatOK;
}

OfxStatus paramSetGetPropertySet(OfxParamSetHandle paramSet, OfxPropertySetHandle* props) {
    if (!paramSet || !props) return kOfxStatErrBadHandle;
    *props = handle(&reinterpret_cast<ParamSet*>(paramSet)->props);
    return kOfxStatOK;
}

OfxStatus paramGetPropertySet(OfxParamHandle param, OfxPropertySetHandle* props) {
    if (!param || !props) return kOfxStatErrBadHandle;
    *props = handle(&reinterpret_cast<Param*>(param)->props);
    return kOfxStatOK;
}

// Values and keyframes belong to instances, which come with rendering (6.11b).
OfxStatus paramUnsupported(OfxParamHandle, ...) {
    return kOfxStatErrUnsupported;
}
OfxStatus paramAtTimeUnsupported(OfxParamHandle, OfxTime, ...) {
    return kOfxStatErrUnsupported;
}
OfxStatus paramIntegralUnsupported(OfxParamHandle, OfxTime, OfxTime, ...) {
    return kOfxStatErrUnsupported;
}
OfxStatus paramSetValueAtTimeUnsupported(OfxParamHandle, OfxTime, ...) {
    return kOfxStatErrUnsupported;
}
OfxStatus paramGetNumKeys(OfxParamHandle, unsigned int* n) {
    if (n) *n = 0;
    return kOfxStatOK;
}
OfxStatus paramGetKeyTime(OfxParamHandle, unsigned int, OfxTime*) {
    return kOfxStatErrBadIndex;
}
OfxStatus paramGetKeyIndex(OfxParamHandle, OfxTime, int, int*) {
    return kOfxStatFailed;
}
OfxStatus paramDeleteKey(OfxParamHandle, OfxTime) {
    return kOfxStatErrUnsupported;
}
OfxStatus paramDeleteAllKeys(OfxParamHandle) {
    return kOfxStatErrUnsupported;
}
OfxStatus paramCopy(OfxParamHandle, OfxParamHandle, OfxTime, const OfxRangeD*) {
    return kOfxStatErrUnsupported;
}
OfxStatus paramEditBegin(OfxParamSetHandle, const char*) {
    return kOfxStatOK;
}
OfxStatus paramEditEnd(OfxParamSetHandle) {
    return kOfxStatOK;
}

const OfxParameterSuiteV1 kParameterSuite{paramDefine,           paramGetHandle,
                                          paramSetGetPropertySet, paramGetPropertySet,
                                          paramUnsupported,       paramAtTimeUnsupported,
                                          paramAtTimeUnsupported, paramIntegralUnsupported,
                                          paramUnsupported,       paramSetValueAtTimeUnsupported,
                                          paramGetNumKeys,        paramGetKeyTime,
                                          paramGetKeyIndex,       paramDeleteKey,
                                          paramDeleteAllKeys,     paramCopy,
                                          paramEditBegin,         paramEditEnd};

OfxStatus getPropertySet(OfxImageEffectHandle e, OfxPropertySetHandle* props) {
    if (!e || !props) return kOfxStatErrBadHandle;
    *props = handle(&effect(e)->props);
    return kOfxStatOK;
}

OfxStatus getParamSet(OfxImageEffectHandle e, OfxParamSetHandle* params) {
    if (!e || !params) return kOfxStatErrBadHandle;
    *params = reinterpret_cast<OfxParamSetHandle>(&effect(e)->params);
    return kOfxStatOK;
}

OfxStatus clipDefine(OfxImageEffectHandle e, const char* name, OfxPropertySetHandle* props) {
    if (!e || !name) return kOfxStatErrBadHandle;
    auto c = std::make_unique<Clip>();
    c->name = name;
    c->props.setString(kOfxPropType, {kOfxTypeClip});
    c->props.setString(kOfxPropName, {name});
    c->props.setString(kOfxPropLabel, {name});
    c->props.setString(kOfxImageEffectPropSupportedComponents, {});
    c->props.setInt(kOfxImageClipPropOptional, {0});
    c->props.setInt(kOfxImageClipPropIsMask, {0});
    c->props.setInt(kOfxImageEffectPropTemporalClipAccess, {0});
    c->props.setInt(kOfxImageEffectPropSupportsTiles, {1});
    c->props.setString(kOfxImageClipPropFieldExtraction, {kOfxImageFieldDoubled});
    if (props) *props = handle(&c->props);
    effect(e)->clips.push_back(std::move(c));
    return kOfxStatOK;
}

// Clips, images and memory of instances come with rendering (6.11b).
OfxStatus clipGetHandle(OfxImageEffectHandle, const char*, OfxImageClipHandle*, OfxPropertySetHandle*) {
    return kOfxStatErrUnsupported;
}
OfxStatus clipGetPropertySet(OfxImageClipHandle, OfxPropertySetHandle*) {
    return kOfxStatErrUnsupported;
}
OfxStatus clipGetImage(OfxImageClipHandle, OfxTime, const OfxRectD*, OfxPropertySetHandle*) {
    return kOfxStatErrUnsupported;
}
OfxStatus clipReleaseImage(OfxPropertySetHandle) {
    return kOfxStatErrUnsupported;
}
OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle, OfxTime, OfxRectD*) {
    return kOfxStatErrUnsupported;
}
int abortEffect(OfxImageEffectHandle) {
    return 0;
}

// Image memory: a plain allocation behind the handle; locks are counted and the memory does not move (ours).
struct ImageMemory {
    void* data = nullptr;
    int locks = 0;
};
OfxStatus imageMemoryAlloc(OfxImageEffectHandle, size_t bytes, OfxImageMemoryHandle* memory) {
    if (!memory) return kOfxStatErrBadHandle;
    auto* m = new ImageMemory;
    m->data = std::calloc(std::max<size_t>(bytes, 1), 1);
    if (!m->data) {
        delete m;
        *memory = nullptr;
        return kOfxStatErrMemory;
    }
    *memory = reinterpret_cast<OfxImageMemoryHandle>(m);
    return kOfxStatOK;
}
OfxStatus imageMemoryFree(OfxImageMemoryHandle memory) {
    if (!memory) return kOfxStatErrBadHandle;
    auto* m = reinterpret_cast<ImageMemory*>(memory);
    std::free(m->data);
    delete m;
    return kOfxStatOK;
}
OfxStatus imageMemoryLock(OfxImageMemoryHandle memory, void** pointer) {
    if (!memory || !pointer) return kOfxStatErrBadHandle;
    auto* m = reinterpret_cast<ImageMemory*>(memory);
    ++m->locks;
    *pointer = m->data;
    return kOfxStatOK;
}
OfxStatus imageMemoryUnlock(OfxImageMemoryHandle memory) {
    if (!memory) return kOfxStatErrBadHandle;
    auto* m = reinterpret_cast<ImageMemory*>(memory);
    m->locks = std::max(m->locks - 1, 0);
    return kOfxStatOK;
}

const OfxImageEffectSuiteV1 kImageEffectSuite{getPropertySet,   getParamSet,     clipDefine,       clipGetHandle,
                                              clipGetPropertySet, clipGetImage,  clipReleaseImage, clipGetRegionOfDefinition,
                                              abortEffect,      imageMemoryAlloc, imageMemoryFree, imageMemoryLock,
                                              imageMemoryUnlock};

// ---------------------------------------------------------------- memory, threads and messages

OfxStatus memoryAlloc(void*, size_t bytes, void** data) {
    if (!data) return kOfxStatErrBadHandle;
    *data = std::malloc(std::max<size_t>(bytes, 1));
    return *data ? kOfxStatOK : kOfxStatErrMemory;
}
OfxStatus memoryFree(void* data) {
    std::free(data);
    return kOfxStatOK;
}
const OfxMemorySuiteV1 kMemorySuite{memoryAlloc, memoryFree};

thread_local unsigned int tThreadIndex = 0;
thread_local bool tSpawned = false;

// Runs `func` on `nThreads` threads (at most the processor count at once) and waits for them all.
OfxStatus multiThread(OfxThreadFunctionV1 func, unsigned int nThreads, void* arg) {
    if (!func) return kOfxStatErrBadHandle;
    if (nThreads <= 1) {
        func(0, 1, arg);
        return kOfxStatOK;
    }
    std::vector<std::thread> threads;
    threads.reserve(nThreads);
    for (unsigned int i = 0; i < nThreads; ++i) {
        threads.emplace_back([func, i, nThreads, arg] {
            tThreadIndex = i;
            tSpawned = true;
            func(i, nThreads, arg);
        });
    }
    for (std::thread& t : threads) t.join();
    return kOfxStatOK;
}
OfxStatus multiThreadNumCPUs(unsigned int* n) {
    if (!n) return kOfxStatErrBadHandle;
    *n = std::max(1u, std::thread::hardware_concurrency());
    return kOfxStatOK;
}
OfxStatus multiThreadIndex(unsigned int* index) {
    if (!index) return kOfxStatErrBadHandle;
    *index = tThreadIndex;
    return kOfxStatOK;
}
int multiThreadIsSpawnedThread() {
    return tSpawned ? 1 : 0;
}
OfxStatus mutexCreate(OfxMutexHandle* mutex, int lockCount) {
    if (!mutex) return kOfxStatErrBadHandle;
    auto* m = new std::recursive_mutex;
    for (int i = 0; i < lockCount; ++i) m->lock();
    *mutex = reinterpret_cast<OfxMutexHandle>(m);
    return kOfxStatOK;
}
OfxStatus mutexDestroy(const OfxMutexHandle mutex) {
    if (!mutex) return kOfxStatErrBadHandle;
    delete reinterpret_cast<std::recursive_mutex*>(mutex);
    return kOfxStatOK;
}
OfxStatus mutexLock(const OfxMutexHandle mutex) {
    if (!mutex) return kOfxStatErrBadHandle;
    reinterpret_cast<std::recursive_mutex*>(mutex)->lock();
    return kOfxStatOK;
}
OfxStatus mutexUnLock(const OfxMutexHandle mutex) {
    if (!mutex) return kOfxStatErrBadHandle;
    reinterpret_cast<std::recursive_mutex*>(mutex)->unlock();
    return kOfxStatOK;
}
OfxStatus mutexTryLock(const OfxMutexHandle mutex) {
    if (!mutex) return kOfxStatErrBadHandle;
    return reinterpret_cast<std::recursive_mutex*>(mutex)->try_lock() ? kOfxStatOK : kOfxStatFailed;
}
const OfxMultiThreadSuiteV1 kMultiThreadSuite{multiThread, multiThreadNumCPUs, multiThreadIndex, multiThreadIsSpawnedThread,
                                              mutexCreate, mutexDestroy,       mutexLock,        mutexUnLock,
                                              mutexTryLock};

std::function<void(const QString&)>& messageHandler() {
    static std::function<void(const QString&)> handler;
    return handler;
}
// The plug-in a message comes from, while one is being called ("<plug-in>: <text>").
QString& currentPlugin() {
    static QString name;
    return name;
}

OfxStatus message(void*, const char* type, const char*, const char* format, ...) {
    char text[1024] = {};
    va_list args;
    va_start(args, format);
    if (format) std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    const QString kind = type && std::string(type) == kOfxMessageError ? QStringLiteral("error: ") : QString();
    const QString line = QStringLiteral("%1: %2%3").arg(currentPlugin(), kind, QString::fromUtf8(text));
    if (messageHandler()) messageHandler()(line);
    else qWarning("OFX %s", qPrintable(line));
    // A question has no one to answer it during the scan.
    return type && std::string(type) == kOfxMessageQuestion ? kOfxStatReplyNo : kOfxStatOK;
}
const OfxMessageSuiteV1 kMessageSuite{message};

// ---------------------------------------------------------------- the host

PropertySet& hostProperties() {
    static PropertySet p = [] {
        PropertySet h;
        h.setString(kOfxPropType, {kOfxTypeImageEffectHost});
        h.setString(kOfxPropName, {"org.open-compose.cutthulhu"});
        h.setString(kOfxPropLabel, {QCoreApplication::applicationName().isEmpty() ? std::string("Cutthulhu")
                                                                                   : QCoreApplication::applicationName().toStdString()});
        h.setInt(kOfxPropAPIVersion, {1, 5});
        h.setInt(kOfxPropVersion, {0, 0, 0});
        h.setString(kOfxPropVersionLabel, {"0"});
        h.setInt(kOfxImageEffectHostPropIsBackground, {0});
        h.setInt(kOfxImageEffectPropSupportsOverlays, {0});
        h.setInt(kOfxImageEffectPropSupportsMultiResolution, {0});
        h.setInt(kOfxImageEffectPropSupportsTiles, {0});
        h.setInt(kOfxImageEffectPropTemporalClipAccess, {0});
        h.setString(kOfxImageEffectPropSupportedComponents, {kOfxImageComponentRGBA});
        h.setString(kOfxImageEffectPropSupportedContexts,
                    {kOfxImageEffectContextFilter, kOfxImageEffectContextGenerator, kOfxImageEffectContextTransition, kOfxImageEffectContextGeneral});
        h.setString(kOfxImageEffectPropSupportedPixelDepths, {kOfxBitDepthFloat});
        h.setInt(kOfxImageEffectPropSupportsMultipleClipDepths, {0});
        h.setInt(kOfxImageEffectPropSupportsMultipleClipPARs, {0});
        h.setInt(kOfxImageEffectPropSetableFrameRate, {0});
        h.setInt(kOfxImageEffectPropSetableFielding, {0});
        h.setInt(kOfxImageEffectInstancePropSequentialRender, {0});
        h.setInt(kOfxParamHostPropSupportsCustomInteract, {0});
        h.setInt(kOfxParamHostPropSupportsCustomAnimation, {0});
        h.setInt(kOfxParamHostPropSupportsStringAnimation, {0});
        h.setInt(kOfxParamHostPropSupportsChoiceAnimation, {0});
        h.setInt(kOfxParamHostPropSupportsBooleanAnimation, {0});
        h.setInt(kOfxParamHostPropSupportsParametricAnimation, {0});
        h.setInt(kOfxParamHostPropMaxParameters, {-1});
        h.setInt(kOfxParamHostPropMaxPages, {0});
        h.setInt(kOfxParamHostPropPageRowColumnCount, {0, 0});
        return h;
    }();
    return p;
}

const void* fetchSuite(OfxPropertySetHandle, const char* name, int version) {
    if (!name) return nullptr;
    const std::string n(name);
    if (version != 1) return nullptr;
    if (n == kOfxPropertySuite) return &kPropertySuite;
    if (n == kOfxParameterSuite) return &kParameterSuite;
    if (n == kOfxImageEffectSuite) return &kImageEffectSuite;
    if (n == kOfxMemorySuite) return &kMemorySuite;
    if (n == kOfxMultiThreadSuite) return &kMultiThreadSuite;
    if (n == kOfxMessageSuite) return &kMessageSuite;
    return nullptr;
}

OfxHost& theHost() {
    static OfxHost h{handle(&hostProperties()), fetchSuite};
    return h;
}

// The properties the standard gives a plug-in's descriptor before its describe action (ofxImageEffect.h).
void defaultEffectProps(PropertySet& p) {
    p.setString(kOfxPropType, {kOfxTypeImageEffect});
    p.setString(kOfxPropLabel, {""});
    p.setString(kOfxPropShortLabel, {""});
    p.setString(kOfxPropLongLabel, {""});
    p.setString(kOfxPropPluginDescription, {""});
    p.setString(kOfxImageEffectPluginPropGrouping, {""});
    p.setString(kOfxImageEffectPropSupportedContexts, {});
    p.setString(kOfxImageEffectPropSupportedPixelDepths, {});
    p.setInt(kOfxImageEffectPluginPropSingleInstance, {0});
    p.setString(kOfxImageEffectPluginRenderThreadSafety, {kOfxImageEffectRenderInstanceSafe});
    p.setInt(kOfxImageEffectPluginPropHostFrameThreading, {0});
    p.setInt(kOfxImageEffectPropSupportsMultiResolution, {1});
    p.setInt(kOfxImageEffectPropSupportsTiles, {1});
    p.setInt(kOfxImageEffectPropTemporalClipAccess, {0});
    p.setInt(kOfxImageEffectPluginPropFieldRenderTwiceAlways, {1});
    p.setInt(kOfxImageEffectPropSupportsMultipleClipDepths, {0});
    p.setInt(kOfxImageEffectPropSupportsMultipleClipPARs, {0});
}

bool succeeded(OfxStatus s) {
    return s == kOfxStatOK || s == kOfxStatReplyDefault;
}

QString statusText(OfxStatus s) {
    switch (s) {
    case kOfxStatFailed: return QStringLiteral("failed");
    case kOfxStatErrFatal: return QStringLiteral("fatal error");
    case kOfxStatErrUnknown: return QStringLiteral("unknown");
    case kOfxStatErrMissingHostFeature: return QStringLiteral("missing host feature");
    case kOfxStatErrUnsupported: return QStringLiteral("unsupported");
    case kOfxStatErrExists: return QStringLiteral("exists");
    case kOfxStatErrFormat: return QStringLiteral("format");
    case kOfxStatErrMemory: return QStringLiteral("out of memory");
    case kOfxStatErrBadHandle: return QStringLiteral("bad handle");
    case kOfxStatErrBadIndex: return QStringLiteral("bad index");
    case kOfxStatErrValue: return QStringLiteral("bad value");
    default: return QStringLiteral("status %1").arg(s);
    }
}

QVariantList valuesOf(const PropertySet& p, const char* name) {
    QVariantList out;
    const Property* prop = p.find(name);
    if (!prop) return out;
    switch (prop->type) {
    case Property::Type::Int:
        for (int v : prop->ints) out << v;
        break;
    case Property::Type::Double:
        for (double v : prop->doubles) out << v;
        break;
    case Property::Type::String:
        for (const std::string& v : prop->strings) out << QString::fromStdString(v);
        break;
    case Property::Type::Pointer: break;
    }
    return out;
}

OfxParamInfo paramInfo(const Param& param) {
    OfxParamInfo info;
    const PropertySet& p = param.props;
    info.type = QString::fromStdString(param.type);
    info.name = QString::fromStdString(param.name);
    info.label = p.string(kOfxPropLabel);
    if (info.label.isEmpty()) info.label = info.name;
    info.hint = p.string(kOfxParamPropHint);
    info.parent = p.string(kOfxParamPropParent);
    info.defaults = valuesOf(p, kOfxParamPropDefault);
    info.options = p.strings(kOfxParamPropChoiceOption);
    info.animates = p.integer(kOfxParamPropAnimates, 1) != 0;
    info.secret = p.integer(kOfxParamPropSecret, 0) != 0;
    const std::vector<double> mins = p.numbers(kOfxParamPropMin), maxs = p.numbers(kOfxParamPropMax);
    const std::vector<double> dmins = p.numbers(kOfxParamPropDisplayMin), dmaxs = p.numbers(kOfxParamPropDisplayMax);
    if (!mins.empty() && !maxs.empty()) {
        info.hasRange = true;
        info.min = mins.front();
        info.max = maxs.front();
        info.displayMin = dmins.empty() ? info.min : dmins.front();
        info.displayMax = dmaxs.empty() ? info.max : dmaxs.front();
    }
    return info;
}

// The loaded binaries and the plug-ins registered from them.
std::vector<std::unique_ptr<QLibrary>>& libraries() {
    static std::vector<std::unique_ptr<QLibrary>> l;
    return l;
}
QList<OfxPluginInfo>& registered() {
    static QList<OfxPluginInfo> r;
    return r;
}

// Describes one plug-in: kOfxActionLoad, kOfxActionDescribe, then kOfxImageEffectActionDescribeInContext for the first
// of its contexts we take (Filter, Generator, Transition, General).
OfxPluginInfo describe(OfxPlugin* plugin, const QString& file) {
    OfxPluginInfo info;
    info.file = file;
    info.id = QString::fromUtf8(plugin->pluginIdentifier ? plugin->pluginIdentifier : "");
    info.versionMajor = static_cast<int>(plugin->pluginVersionMajor);
    info.versionMinor = static_cast<int>(plugin->pluginVersionMinor);
    currentPlugin() = info.id;
    struct Clear {
        ~Clear() { currentPlugin().clear(); }
    } clearAfter;
    if (!plugin->mainEntry) {
        info.error = QStringLiteral("it has no main entry");
        return info;
    }
    if (plugin->setHost) plugin->setHost(&theHost());
    if (const OfxStatus s = plugin->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr); !succeeded(s)) {
        info.error = QStringLiteral("its load action failed (%1)").arg(statusText(s));
        return info;
    }
    Effect descriptor;
    defaultEffectProps(descriptor.props);
    if (const OfxStatus s = plugin->mainEntry(kOfxActionDescribe, reinterpret_cast<OfxImageEffectHandle>(&descriptor), nullptr, nullptr);
        !succeeded(s)) {
        info.error = QStringLiteral("its describe action failed (%1)").arg(statusText(s));
        return info;
    }
    info.label = descriptor.props.string(kOfxPropLabel);
    if (info.label.isEmpty()) info.label = info.id;
    info.grouping = descriptor.props.string(kOfxImageEffectPluginPropGrouping);
    info.description = descriptor.props.string(kOfxPropPluginDescription);
    info.contexts = descriptor.props.strings(kOfxImageEffectPropSupportedContexts);
    for (const char* context : {kOfxImageEffectContextFilter, kOfxImageEffectContextGenerator, kOfxImageEffectContextTransition,
                                kOfxImageEffectContextGeneral}) {
        if (!info.contexts.contains(QLatin1String(context))) continue;
        Effect inContext;
        inContext.props = descriptor.props;
        PropertySet in;
        in.setString(kOfxImageEffectPropContext, {context});
        const OfxStatus s = plugin->mainEntry(kOfxImageEffectActionDescribeInContext, reinterpret_cast<OfxImageEffectHandle>(&inContext),
                                              handle(&in), nullptr);
        if (!succeeded(s)) {
            info.error = QStringLiteral("its describe in context action failed for %1 (%2)").arg(QLatin1String(context), statusText(s));
            continue;
        }
        info.error.clear();
        info.describedContext = QLatin1String(context);
        for (const auto& c : inContext.clips) info.clips << QString::fromStdString(c->name);
        for (const auto& p : inContext.params.params) info.params << paramInfo(*p);
        break;
    }
    return info;
}

} // namespace

bool OfxPluginInfo::usable() const {
    return error.isEmpty() && !describedContext.isEmpty();
}

QString OfxPluginInfo::whyNot() const {
    if (!error.isEmpty()) return error;
    if (describedContext.isEmpty()) return QStringLiteral("it supports no Filter, Generator, Transition or General context");
    return {};
}

QString OfxPluginInfo::category() const {
    const QString vendor = grouping.section(QLatin1Char('/'), 0, 0).trimmed();
    return vendor.isEmpty() ? QStringLiteral("OFX Plug-Ins") : vendor;
}

bool OfxPluginInfo::segment() const {
    return contexts.contains(QLatin1String(kOfxImageEffectContextFilter)) || contexts.contains(QLatin1String(kOfxImageEffectContextGenerator)) ||
           contexts.contains(QLatin1String(kOfxImageEffectContextGeneral));
}

bool OfxPluginInfo::transition() const {
    return contexts.contains(QLatin1String(kOfxImageEffectContextTransition));
}

QString ofxArchitecture() {
#if defined(Q_OS_WIN)
    return QStringLiteral("Win64");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("MacOS");
#elif defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("Linux-aarch64");
#else
    return QStringLiteral("Linux-x86-64");
#endif
}

QStringList ofxSearchPaths() {
    QStringList candidates;
#if defined(Q_OS_WIN)
    const QString common = qEnvironmentVariable("CommonProgramFiles", QStringLiteral("C:/Program Files/Common Files"));
    candidates << QDir::fromNativeSeparators(common) + QStringLiteral("/OFX/Plugins");
#elif defined(Q_OS_MACOS)
    candidates << QStringLiteral("/Library/OFX/Plugins");
#else
    candidates << QStringLiteral("/usr/OFX/Plugins");
#endif
    for (const char* variable : {"OFX_PLUGIN_PATH", "LUMENIS_OFX_PATH"}) {
        for (const QString& p : qEnvironmentVariable(variable).split(QDir::listSeparator(), Qt::SkipEmptyParts)) candidates << p;
    }
    QStringList out;
    for (const QString& c : candidates) {
        if (QFileInfo(c).isDir() && !out.contains(c)) out << c;
    }
    return out;
}

QStringList findOfxBundles(const QStringList& folders) {
    QStringList out;
    const QString arch = ofxArchitecture();
    for (const QString& folder : folders) {
        QDirIterator it(folder, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString dir = it.next();
            const QString name = QFileInfo(dir).fileName();
            if (!name.endsWith(QLatin1String(".ofx.bundle"), Qt::CaseInsensitive)) continue;
            const QString binary = dir + QStringLiteral("/Contents/") + arch + QLatin1Char('/') + name.left(name.size() - 7);
            if (QFileInfo(binary).isFile() && !out.contains(binary)) out << binary;
        }
    }
    out.sort();
    return out;
}

QList<OfxPluginInfo> scanOfxPlugins(const QStringList& binaries, QStringList* errors) {
    QList<OfxPluginInfo> out;
    for (const QString& file : binaries) {
        auto lib = std::make_unique<QLibrary>(file);
        if (!lib->load()) {
            if (errors) *errors << QStringLiteral("%1 could not be loaded: %2").arg(QFileInfo(file).fileName(), lib->errorString());
            continue;
        }
        using Count = int (*)();
        using Get = OfxPlugin* (*)(int);
        const auto count = reinterpret_cast<Count>(lib->resolve("OfxGetNumberOfPlugins"));
        const auto get = reinterpret_cast<Get>(lib->resolve("OfxGetPlugin"));
        if (!count || !get) {
            if (errors) *errors << QStringLiteral("%1 has no OFX entry points").arg(QFileInfo(file).fileName());
            lib->unload();
            continue;
        }
        const int n = count();
        for (int i = 0; i < n; ++i) {
            OfxPlugin* plugin = get(i);
            if (!plugin) continue;
            // Only image effects (other APIs, such as OFX's own hosts' extensions, are not ours).
            if (!plugin->pluginApi || std::string(plugin->pluginApi) != kOfxImageEffectPluginApi || plugin->apiVersion != kOfxImageEffectPluginApiVersion) {
                continue;
            }
            out << describe(plugin, file);
        }
        libraries().push_back(std::move(lib));
    }
    return out;
}

int registerOfxPlugins(const QList<OfxPluginInfo>& plugins) {
    registered() = plugins;
    return static_cast<int>(std::count_if(plugins.begin(), plugins.end(), [](const OfxPluginInfo& p) { return p.usable(); }));
}

const QList<OfxPluginInfo>& ofxPlugins() {
    return registered();
}

const OfxPluginInfo* ofxPluginFor(const QString& category, const QString& label) {
    for (const OfxPluginInfo& p : registered()) {
        if (p.usable() && p.category() == category && p.label == label) return &p;
    }
    return nullptr;
}

void setOfxMessageHandler(std::function<void(const QString&)> handler) {
    messageHandler() = std::move(handler);
}

ext::ProviderInfo OfxHostProvider::info() const {
    ext::ProviderInfo i;
    i.id = "org.openeffects.ofx-host";
    i.displayName = "OpenFX plug-in host";
    i.category = ext::Category::EffectPluginHost;
    i.tier = ext::LicenseTier::OpenSource;
    i.vendor = "Cutthulhu";
    i.version = "OpenFX 1.5.1";
    return i;
}

std::vector<std::filesystem::path> OfxHostProvider::defaultSearchPaths() const {
    std::vector<std::filesystem::path> out;
    for (const QString& d : ofxSearchPaths()) out.emplace_back(d.toStdString());
    return out;
}

std::vector<ext::PluginDescriptor> OfxHostProvider::scan(const std::vector<std::filesystem::path>& paths) {
    QStringList folders;
    for (const auto& p : paths) folders << QString::fromStdString(p.string());
    std::vector<ext::PluginDescriptor> out;
    for (const OfxPluginInfo& p : scanOfxPlugins(findOfxBundles(folders))) {
        ext::PluginDescriptor d;
        d.standard = ext::PluginStandard::OFX;
        d.id = p.id.toStdString();
        d.name = p.label.toStdString();
        d.vendor = p.category().toStdString();
        d.version = std::to_string(p.versionMajor) + "." + std::to_string(p.versionMinor);
        d.location = p.file.toStdString();
        out.push_back(std::move(d));
    }
    return out;
}

} // namespace lumenis::media
