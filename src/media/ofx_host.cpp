// The OpenFX host's core (ROADMAP 6.11a; docs/spec/ofx-host.md). See lumenis/media/ofx_host.h.
//
// The host gives plug-ins the suites of the OpenFX headers (third_party/openfx) as C function tables. Every handle a
// plug-in sees is a pointer to one of the objects below: OfxPropertySetHandle a PropertySet, OfxImageEffectHandle an
// Effect (a descriptor, or an Instance), OfxParamSetHandle an Effect's ParamSet, OfxParamHandle a Param,
// OfxImageClipHandle a Clip. Plug-ins are called from one thread at a time: during the scan, and during renders, which
// one mutex keeps apart (renderMutex).
//
// Rendering (6.11b1, the second half of the file): each plug-in gets one Instance, made at its first render. A render
// sets the instance's RenderContext (the effect, its span and the input pictures), then runs the isIdentity, begin
// sequence, render and end sequence actions. The plug-in reads its parameters through the parameter suite, which
// answers from the effect's stored values and keyframes, and its clips' images, which are the pictures copied into
// float RGBA buffers with their rows bottom to top (the standard's image layout).

#include "lumenis/media/ofx_host.h"

#include <ofxCore.h>
#include <ofxDrawSuite.h>
#include <ofxInteract.h>
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
#include <QColor>
#include <QJsonObject>
#include <QLibrary>
#include <QMap>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <optional>
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

struct Effect;

struct Param {
    std::string type;
    std::string name;
    PropertySet props;
    Effect* owner = nullptr; // its effect: a descriptor's, or an instance's
    std::string text;        // a String parameter's value, kept while the plug-in reads it
};

struct ParamSet {
    PropertySet props;
    Effect* owner = nullptr;
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
    Effect* owner = nullptr;
};

struct Instance;

// An effect descriptor (the describe action's, or one context's), or the effect part of an Instance.
struct Effect {
    PropertySet props;
    ParamSet params;
    std::vector<std::unique_ptr<Clip>> clips;
    Instance* instance = nullptr; // set for an Instance
    Effect() { params.owner = this; }
    Effect(const Effect&) = delete; // its parameters and clips point back to it
    Effect& operator=(const Effect&) = delete;
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
    p->owner = ps->owner;
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

// Values and keyframes: for an instance's parameters during a render, from the effect (defined with the rendering below);
// otherwise the defaults.
OfxStatus paramGetValue(OfxParamHandle param, ...);
OfxStatus paramGetValueAtTime(OfxParamHandle param, OfxTime time, ...);
OfxStatus paramGetDerivative(OfxParamHandle param, OfxTime time, ...);
OfxStatus paramGetIntegral(OfxParamHandle param, OfxTime time1, OfxTime time2, ...);
OfxStatus paramGetNumKeys(OfxParamHandle param, unsigned int* n);
OfxStatus paramGetKeyTime(OfxParamHandle param, unsigned int nthKey, OfxTime* time);
OfxStatus paramGetKeyIndex(OfxParamHandle param, OfxTime time, int direction, int* index);
// Setting values (plug-ins do so in their instance changed action, which we do not send) is left to the effect's
// controls: accepted and not kept (ours).
// During a push button's instance changed action (ofxPushButton) the values set are kept for the Effect Editor.
OfxStatus paramSetValue(OfxParamHandle param, ...);
OfxStatus paramSetValueAtTime(OfxParamHandle param, OfxTime time, ...);
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
                                          paramGetValue,          paramGetValueAtTime,
                                          paramGetDerivative,     paramGetIntegral,
                                          paramSetValue,          paramSetValueAtTime,
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
    c->owner = effect(e);
    if (props) *props = handle(&c->props);
    effect(e)->clips.push_back(std::move(c));
    return kOfxStatOK;
}

// An instance's clips and their images (defined with the rendering below).
OfxStatus clipGetHandle(OfxImageEffectHandle e, const char* name, OfxImageClipHandle* clip, OfxPropertySetHandle* props);
OfxStatus clipGetPropertySet(OfxImageClipHandle clip, OfxPropertySetHandle* props);
OfxStatus clipGetImage(OfxImageClipHandle clip, OfxTime time, const OfxRectD* region, OfxPropertySetHandle* image);
OfxStatus clipReleaseImage(OfxPropertySetHandle image);
OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle clip, OfxTime time, OfxRectD* bounds);
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
        h.setInt(kOfxImageEffectPropSupportsOverlays, {1}); // through the Draw suite (6.11c2)
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

// The Interact and Draw suites (6.11c2), defined with the overlays below.
extern const OfxInteractSuiteV1 kInteractSuite;
extern const OfxDrawSuiteV1 kDrawSuite;

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
    if (n == kOfxInteractSuite) return &kInteractSuite;
    if (n == kOfxDrawSuite) return &kDrawSuite;
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
    info.doubleType = p.string(kOfxParamPropDoubleType);
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

// What rendering needs of a described plug-in: its entry points, the context it was described in and that context's
// descriptor (whose clips and parameters an instance copies). Descriptors stay alive for as long as the program, even
// when a later scan describes the plug-in again, since a plug-in may keep their handles.
struct Described {
    OfxPlugin* plugin = nullptr;
    std::string context;
    Effect* descriptor = nullptr;
};
std::map<QString, Described>& described() {
    static std::map<QString, Described> d;
    return d;
}
std::vector<std::unique_ptr<Effect>>& keptDescriptors() {
    static std::vector<std::unique_ptr<Effect>> k;
    return k;
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
    // Each context we take is described; the first of them in the order Filter, Generator, Transition, General is used,
    // or General when it has more inputs (a plug-in whose extra inputs are General's, p.161).
    struct Candidate {
        std::string context;
        Effect* descriptor = nullptr;
        int inputs = 0;
    };
    std::vector<Candidate> candidates;
    for (const char* context : {kOfxImageEffectContextFilter, kOfxImageEffectContextGenerator, kOfxImageEffectContextTransition,
                                kOfxImageEffectContextGeneral}) {
        if (!info.contexts.contains(QLatin1String(context))) continue;
        auto kept = std::make_unique<Effect>();
        Effect& inContext = *kept;
        inContext.props = descriptor.props;
        PropertySet in;
        in.setString(kOfxImageEffectPropContext, {context});
        const OfxStatus s = plugin->mainEntry(kOfxImageEffectActionDescribeInContext, reinterpret_cast<OfxImageEffectHandle>(&inContext),
                                              handle(&in), nullptr);
        if (!succeeded(s)) {
            info.error = QStringLiteral("its describe in context action failed for %1 (%2)").arg(QLatin1String(context), statusText(s));
            continue;
        }
        int inputs = 0;
        for (const auto& c : inContext.clips) inputs += c->name == kOfxImageEffectOutputClipName ? 0 : 1;
        candidates.push_back(Candidate{context, kept.get(), inputs});
        keptDescriptors().push_back(std::move(kept));
    }
    if (candidates.empty()) return info;
    Candidate chosen = candidates.front();
    for (const Candidate& c : candidates) {
        if (c.context == kOfxImageEffectContextGeneral && c.inputs > chosen.inputs) chosen = c;
    }
    info.error.clear();
    info.describedContext = QString::fromStdString(chosen.context);
    for (const auto& c : chosen.descriptor->clips) {
        info.clips << QString::fromStdString(c->name);
        if (c->name == kOfxImageEffectOutputClipName) continue;
        OfxClipInfo clipInfo;
        clipInfo.name = QString::fromStdString(c->name);
        clipInfo.label = c->props.string(kOfxPropLabel);
        if (clipInfo.label.isEmpty()) clipInfo.label = clipInfo.name;
        clipInfo.optional = c->props.integer(kOfxImageClipPropOptional, 0) != 0;
        clipInfo.mask = c->props.integer(kOfxImageClipPropIsMask, 0) != 0;
        info.inputs << clipInfo;
    }
    for (const auto& p : chosen.descriptor->params.params) info.params << paramInfo(*p);
    described()[info.id] = Described{plugin, chosen.context, chosen.descriptor};
    return info;
}

} // namespace

int OfxPluginInfo::requiredInputs() const {
    return static_cast<int>(std::count_if(inputs.begin(), inputs.end(), [](const OfxClipInfo& c) { return !c.optional; }));
}

QList<OfxClipInfo> OfxPluginInfo::connectedInputs(int count) const {
    int optionalLeft = std::clamp(count, requiredInputs(), maxInputs()) - requiredInputs();
    QList<OfxClipInfo> out;
    for (const OfxClipInfo& c : inputs) {
        if (!c.optional) {
            out << c;
        } else if (optionalLeft > 0) {
            out << c;
            --optionalLeft;
        }
    }
    return out;
}

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

// ================================================================ rendering (6.11b1)

namespace {

double gFrameRate = 30.0;

// What one render gives the plug-in: the effect whose values its parameters take, the effect's span, the render's
// frame, a transition's progress, and the pictures of its input clips by clip name.
struct RenderContext {
    const model::Effect* effect = nullptr;
    const OfxPluginInfo* info = nullptr;
    model::EffectSpan span;
    double frame = 0.0;
    double progress = -1.0; // a transition's; -1 for segments
    int width = 0, height = 0;        // the pictures, in pixels
    double projectWidth = 0.0, projectHeight = 0.0; // the project in canonical coordinates (setOfxProjectSize)
    double scaleX = 1.0, scaleY = 1.0; // the render scale: pixels per canonical unit
    std::map<std::string, const Picture*> inputs;
};

QSize gProjectSize;

// The project's size in canonical coordinates and the render scale for `r`'s pictures (6.11c2): the project set by
// setOfxProjectSize, else the pictures' own size (scale 1). A context without pictures (a push button, an overlay)
// works at the project's size.
void fitProject(RenderContext& r) {
    if (gProjectSize.isValid() && !gProjectSize.isEmpty()) {
        r.projectWidth = gProjectSize.width();
        r.projectHeight = gProjectSize.height();
    } else {
        r.projectWidth = r.width > 0 ? r.width : 1920.0;
        r.projectHeight = r.height > 0 ? r.height : 1080.0;
    }
    if (r.width <= 0 || r.height <= 0) {
        r.width = static_cast<int>(r.projectWidth);
        r.height = static_cast<int>(r.projectHeight);
    }
    r.scaleX = r.width / r.projectWidth;
    r.scaleY = r.height / r.projectHeight;
}

// An image the host gave the plug-in: its properties and its pixels, float RGBA with rows bottom to top.
struct Image {
    PropertySet props;
    std::vector<float> data;
};

// A plug-in's overlay interact (6.11c2; ofxInteract.h): its properties and the plug-in's entry point for it.
struct Interact {
    PropertySet props;
    OfxPluginEntryPoint* entry = nullptr;
    bool redraw = false; // the plug-in asked for a redraw (interactRedraw)
};

// A plug-in's one instance, made at its first render (kOfxActionCreateInstance).
struct Instance : Effect {
    QString id;
    OfxPlugin* plugin = nullptr;
    std::string context;
    RenderContext* render = nullptr; // during a render
    std::vector<std::unique_ptr<Image>> images; // given out and not yet released
    Image* output = nullptr; // the Output clip's image during a render
    bool failed = false;     // creating it failed: the effect passes its picture through
    bool reported = false;   // a render failure was reported once
    std::unique_ptr<Interact> overlay; // its overlay interact, made when first drawn (6.11c2)
    bool noOverlay = false;  // the plug-in has none we can draw (no V2 overlay, or creating it failed)
};

std::map<QString, std::unique_ptr<Instance>>& instances() {
    static std::map<QString, std::unique_ptr<Instance>> i;
    return i;
}

std::recursive_mutex& renderMutex() {
    static std::recursive_mutex m;
    return m;
}

Param* param(OfxParamHandle h) {
    return reinterpret_cast<Param*>(h);
}
Clip* clip(OfxImageClipHandle h) {
    return reinterpret_cast<Clip*>(h);
}

// The plug-in's parameter description by name.
const OfxParamInfo* paramInfoOf(const OfxPluginInfo& plugin, const std::string& name) {
    const QString n = QString::fromStdString(name);
    for (const OfxParamInfo& p : plugin.params) {
        if (p.name == n) return &p;
    }
    return nullptr;
}

// How many values a parameter type has.
int dimensionOf(const std::string& type) {
    if (type == kOfxParamTypeDouble2D || type == kOfxParamTypeInteger2D) return 2;
    if (type == kOfxParamTypeDouble3D || type == kOfxParamTypeInteger3D || type == kOfxParamTypeRGB) return 3;
    if (type == kOfxParamTypeRGBA) return 4;
    return 1;
}

const char* const kAxes[] = {".x", ".y", ".z"};

// A parameter's values at `frame` of the render: from the effect's stored values and keyframes, else the defaults.
std::vector<double> valuesAt(const Param& p, const RenderContext* r, double frame) {
    const int n = dimensionOf(p.type);
    std::vector<double> out = p.props.numbers(kOfxParamPropDefault);
    out.resize(static_cast<std::size_t>(n), 0.0);
    if (!r || !r->effect || !r->info) return out;
    // The Transition context's own parameter is the transition's progress.
    if (r->progress >= 0.0 && p.name == kOfxImageEffectTransitionParamName) return {r->progress};
    const OfxParamInfo* info = paramInfoOf(*r->info, p.name);
    if (!info) return out;
    const model::Effect& e = *r->effect;
    const QString category = ofxParameterCategory(*r->info);
    const QString key = info->name;
    const double t = r->span.timeOf(frame);
    if (p.type == kOfxParamTypeBoolean) {
        const QJsonValue v = e.parameters.value(category).toObject().value(key);
        if (v.isBool()) out[0] = v.toBool() ? 1.0 : 0.0;
    } else if (p.type == kOfxParamTypeChoice) {
        const QJsonValue v = e.parameters.value(category).toObject().value(key);
        if (v.isString()) {
            const qsizetype i = info->options.indexOf(v.toString());
            if (i >= 0) out[0] = static_cast<double>(i);
        }
    } else if (p.type == kOfxParamTypeRGB || p.type == kOfxParamTypeRGBA) {
        if (const auto rgb = model::colorAt(e, category, key, t, r->span)) {
            out[0] = rgb->red;
            out[1] = rgb->green;
            out[2] = rgb->blue;
        }
    } else if (n == 1) {
        if (const auto v = model::numberAt(e, category, key, t, r->span)) out[0] = *v;
    } else {
        for (int i = 0; i < n; ++i) {
            if (const auto v = model::numberAt(e, category, key + QLatin1String(kAxes[i]), t, r->span)) out[static_cast<std::size_t>(i)] = *v;
        }
    }
    if (p.type == kOfxParamTypeInteger || p.type == kOfxParamTypeInteger2D || p.type == kOfxParamTypeInteger3D) {
        for (double& v : out) v = std::round(v);
    }
    return out;
}

const RenderContext* contextOf(const Param& p) {
    return p.owner && p.owner->instance ? p.owner->instance->render : nullptr;
}

// Writes a parameter's values to the plug-in's pointers, as its type takes them (ofxParam.h's getValue).
OfxStatus writeValues(Param& p, double frame, va_list ap) {
    const std::string& type = p.type;
    if (type == kOfxParamTypeString || type == kOfxParamTypeStrChoice || type == kOfxParamTypeCustom) {
        // Ours: strings keep their defaults.
        p.text = p.props.string(kOfxParamPropDefault).toStdString();
        *va_arg(ap, const char**) = p.text.c_str();
        return kOfxStatOK;
    }
    if (type == kOfxParamTypeGroup || type == kOfxParamTypePage || type == kOfxParamTypePushButton || type == kOfxParamTypeParametric ||
        type == kOfxParamTypeBytes) {
        return kOfxStatErrBadHandle;
    }
    const std::vector<double> v = valuesAt(p, contextOf(p), frame);
    const bool integral = type == kOfxParamTypeInteger || type == kOfxParamTypeInteger2D || type == kOfxParamTypeInteger3D ||
                          type == kOfxParamTypeBoolean || type == kOfxParamTypeChoice;
    for (double x : v) {
        if (integral) *va_arg(ap, int*) = static_cast<int>(x);
        else *va_arg(ap, double*) = x;
    }
    return kOfxStatOK;
}

double renderFrame(const Param& p) {
    const RenderContext* r = contextOf(p);
    return r ? r->frame : 0.0;
}

} // namespace

// The parameter suite's value calls (declared above, in the first anonymous namespace).
namespace {

OfxStatus paramGetValue(OfxParamHandle h, ...) {
    if (!h) return kOfxStatErrBadHandle;
    va_list ap;
    va_start(ap, h);
    const OfxStatus s = writeValues(*param(h), renderFrame(*param(h)), ap);
    va_end(ap);
    return s;
}

OfxStatus paramGetValueAtTime(OfxParamHandle h, OfxTime time, ...) {
    if (!h) return kOfxStatErrBadHandle;
    va_list ap;
    va_start(ap, time);
    const OfxStatus s = writeValues(*param(h), time, ap);
    va_end(ap);
    return s;
}

// The derivative and integral of a numeric parameter, worked out from its values a frame apart (ours).
OfxStatus paramGetDerivative(OfxParamHandle h, OfxTime time, ...) {
    if (!h) return kOfxStatErrBadHandle;
    Param& p = *param(h);
    const std::vector<double> a = valuesAt(p, contextOf(p), time - 0.5), b = valuesAt(p, contextOf(p), time + 0.5);
    va_list ap;
    va_start(ap, time);
    for (std::size_t i = 0; i < a.size(); ++i) *va_arg(ap, double*) = b[i] - a[i];
    va_end(ap);
    return kOfxStatOK;
}

OfxStatus paramGetIntegral(OfxParamHandle h, OfxTime time1, OfxTime time2, ...) {
    if (!h) return kOfxStatErrBadHandle;
    Param& p = *param(h);
    std::vector<double> sum(static_cast<std::size_t>(dimensionOf(p.type)), 0.0);
    const int steps = std::clamp(static_cast<int>(std::abs(time2 - time1)) * 4, 1, 4096);
    const double dt = (time2 - time1) / steps;
    for (int i = 0; i < steps; ++i) {
        const std::vector<double> v = valuesAt(p, contextOf(p), time1 + (i + 0.5) * dt);
        for (std::size_t k = 0; k < sum.size(); ++k) sum[k] += v[k] * dt;
    }
    va_list ap;
    va_start(ap, time2);
    for (double x : sum) *va_arg(ap, double*) = x;
    va_end(ap);
    return kOfxStatOK;
}

// The keyframes of an instance's parameter during a render: those of its stored key in the effect (the first axis's
// for a 2D or 3D one), at their frames.
std::vector<double> keyFrames(const Param& p) {
    const RenderContext* r = contextOf(p);
    std::vector<double> out;
    if (!r || !r->effect || !r->info) return out;
    const OfxParamInfo* info = paramInfoOf(*r->info, p.name);
    if (!info) return out;
    const QString key = dimensionOf(p.type) > 1 && p.type != kOfxParamTypeRGB && p.type != kOfxParamTypeRGBA ? info->name + QLatin1String(".x") : info->name;
    const model::ParameterKeyframes keys = model::keyframesOf(*r->effect, ofxParameterCategory(*r->info), key);
    for (const model::EffectKeyframe& k : keys.keys) out.push_back(r->span.frameOf(model::keyframeTime(keys, k, r->span)));
    std::sort(out.begin(), out.end());
    return out;
}

OfxStatus paramGetNumKeys(OfxParamHandle h, unsigned int* n) {
    if (!h || !n) return kOfxStatErrBadHandle;
    *n = static_cast<unsigned int>(keyFrames(*param(h)).size());
    return kOfxStatOK;
}

OfxStatus paramGetKeyTime(OfxParamHandle h, unsigned int nth, OfxTime* time) {
    if (!h || !time) return kOfxStatErrBadHandle;
    const std::vector<double> keys = keyFrames(*param(h));
    if (nth >= keys.size()) return kOfxStatErrBadIndex;
    *time = keys[nth];
    return kOfxStatOK;
}

OfxStatus paramGetKeyIndex(OfxParamHandle h, OfxTime time, int direction, int* index) {
    if (!h || !index) return kOfxStatErrBadHandle;
    const std::vector<double> keys = keyFrames(*param(h));
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const std::size_t k = direction < 0 ? keys.size() - 1 - i : i;
        const bool hit = direction == 0 ? std::abs(keys[k] - time) < 1e-6 : direction < 0 ? keys[k] < time : keys[k] > time;
        if (hit) {
            *index = static_cast<int>(k);
            return kOfxStatOK;
        }
    }
    return kOfxStatFailed;
}

// ---------------------------------------------------------------- clips and images

OfxStatus clipGetHandle(OfxImageEffectHandle e, const char* name, OfxImageClipHandle* handleOut, OfxPropertySetHandle* props) {
    if (!e || !name) return kOfxStatErrBadHandle;
    for (const auto& c : effect(e)->clips) {
        if (c->name == name) {
            if (handleOut) *handleOut = reinterpret_cast<OfxImageClipHandle>(c.get());
            if (props) *props = handle(&c->props);
            return kOfxStatOK;
        }
    }
    return kOfxStatErrUnknown;
}

OfxStatus clipGetPropertySet(OfxImageClipHandle c, OfxPropertySetHandle* props) {
    if (!c || !props) return kOfxStatErrBadHandle;
    *props = handle(&clip(c)->props);
    return kOfxStatOK;
}

void setImageProps(Image& image, const std::string& clipName, double frame, int width, int height, double scaleX, double scaleY) {
    PropertySet& p = image.props;
    p.setString(kOfxPropType, {kOfxTypeImage});
    p.setString(kOfxImageEffectPropPixelDepth, {kOfxBitDepthFloat});
    p.setString(kOfxImageEffectPropComponents, {kOfxImageComponentRGBA});
    p.setString(kOfxImageEffectPropPreMultiplication, {kOfxImagePreMultiplied});
    p.setDouble(kOfxImageEffectPropRenderScale, {scaleX, scaleY});
    p.setDouble(kOfxImagePropPixelAspectRatio, {1.0});
    p.setPointer(kOfxImagePropData, {image.data.data()});
    p.setInt(kOfxImagePropBounds, {0, 0, width, height});
    p.setInt(kOfxImagePropRegionOfDefinition, {0, 0, width, height});
    p.setInt(kOfxImagePropRowBytes, {width * 4 * static_cast<int>(sizeof(float))});
    p.setString(kOfxImagePropField, {kOfxImageFieldNone});
    p.setString(kOfxImagePropUniqueIdentifier, {clipName + ":" + std::to_string(frame)});
}

OfxStatus clipGetImage(OfxImageClipHandle c, OfxTime time, const OfxRectD*, OfxPropertySetHandle* imageOut) {
    if (!c || !imageOut) return kOfxStatErrBadHandle;
    Clip& cl = *clip(c);
    Instance* inst = cl.owner ? cl.owner->instance : nullptr;
    if (!inst || !inst->render) return kOfxStatFailed;
    const RenderContext& r = *inst->render;
    if (cl.name == kOfxImageEffectOutputClipName) {
        if (!inst->output) return kOfxStatFailed;
        *imageOut = handle(&inst->output->props);
        return kOfxStatOK;
    }
    const auto it = r.inputs.find(cl.name);
    if (it == r.inputs.end() || !it->second) return kOfxStatFailed; // not connected
    // Ours: every time of an input clip is the render's picture (no temporal clip access).
    const Picture& picture = *it->second;
    auto image = std::make_unique<Image>();
    const int w = r.width, h = r.height;
    image->data.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0.0f);
    if (picture.width() == w && picture.height() == h) {
        for (int y = 0; y < h; ++y) {
            // The standard's row 0 is the bottom of the picture.
            std::copy_n(picture.pixel(0, h - 1 - y), static_cast<std::size_t>(w) * 4, &image->data[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4]);
        }
    }
    setImageProps(*image, cl.name, time, w, h, r.scaleX, r.scaleY);
    *imageOut = handle(&image->props);
    inst->images.push_back(std::move(image));
    return kOfxStatOK;
}

OfxStatus clipReleaseImage(OfxPropertySetHandle image) {
    if (!image) return kOfxStatErrBadHandle;
    for (auto& [id, inst] : instances()) {
        auto& list = inst->images;
        const auto it = std::find_if(list.begin(), list.end(), [&](const std::unique_ptr<Image>& i) { return handle(&i->props) == image; });
        if (it != list.end()) {
            list.erase(it);
            return kOfxStatOK;
        }
        if (inst->output && handle(&inst->output->props) == image) return kOfxStatOK; // the host keeps the output
    }
    return kOfxStatErrBadHandle;
}

OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle c, OfxTime, OfxRectD* bounds) {
    if (!c || !bounds) return kOfxStatErrBadHandle;
    Instance* inst = clip(c)->owner ? clip(c)->owner->instance : nullptr;
    // In canonical coordinates: the project.
    const RenderContext* r = inst ? inst->render : nullptr;
    *bounds = OfxRectD{0.0, 0.0, r ? r->projectWidth : 0.0, r ? r->projectHeight : 0.0};
    return kOfxStatOK;
}

// ---------------------------------------------------------------- instances and the render actions

// The instance's properties a render changes: the project's size and rate, the effect's length.
void setProjectProps(Instance& inst, const RenderContext& r) {
    PropertySet& p = inst.props;
    const double w = r.projectWidth > 0.0 ? r.projectWidth : r.width, h = r.projectHeight > 0.0 ? r.projectHeight : r.height;
    p.setDouble(kOfxImageEffectPropProjectSize, {w, h});
    p.setDouble(kOfxImageEffectPropProjectExtent, {w, h});
    p.setDouble(kOfxImageEffectPropProjectOffset, {0.0, 0.0});
    p.setDouble(kOfxImageEffectPropProjectPixelAspectRatio, {1.0});
    p.setDouble(kOfxImageEffectInstancePropEffectDuration, {static_cast<double>(r.span.length)});
    p.setDouble(kOfxImageEffectPropFrameRate, {gFrameRate});
    for (const auto& c : inst.clips) {
        PropertySet& cp = c->props;
        const bool connected = c->name == kOfxImageEffectOutputClipName || r.inputs.count(c->name) > 0;
        cp.setInt(kOfxImageClipPropConnected, {connected ? 1 : 0});
        cp.setDouble(kOfxImageEffectPropFrameRate, {gFrameRate});
        cp.setDouble(kOfxImageEffectPropFrameRange, {0.0, static_cast<double>(std::max<std::int64_t>(r.span.length - 1, 0))});
        cp.setDouble(kOfxImageEffectPropUnmappedFrameRate, {gFrameRate});
        cp.setDouble(kOfxImageEffectPropUnmappedFrameRange, {0.0, static_cast<double>(std::max<std::int64_t>(r.span.length - 1, 0))});
    }
}

// The plug-in's instance, made when first asked for; null if it cannot be made.
Instance* instanceFor(const OfxPluginInfo& info) {
    auto& all = instances();
    if (auto it = all.find(info.id); it != all.end()) return it->second->failed ? nullptr : it->second.get();
    const auto d = described().find(info.id);
    if (d == described().end() || !d->second.plugin || !d->second.descriptor) return nullptr;
    auto inst = std::make_unique<Instance>();
    inst->instance = inst.get();
    inst->id = info.id;
    inst->plugin = d->second.plugin;
    inst->context = d->second.context;
    const Effect& desc = *d->second.descriptor;
    inst->props = desc.props;
    inst->props.setString(kOfxPropType, {kOfxTypeImageEffectInstance});
    inst->props.setString(kOfxImageEffectPropContext, {inst->context});
    inst->props.setPointer(kOfxPropInstanceData, {nullptr});
    inst->props.setInt(kOfxPropIsInteractive, {0});
    inst->props.setInt(kOfxImageEffectInstancePropSequentialRender, {0});
    inst->props.setInt(kOfxImageEffectPropSupportsTiles, {0});
    for (const auto& c : desc.clips) {
        auto copy = std::make_unique<Clip>();
        copy->name = c->name;
        copy->props = c->props;
        copy->owner = inst.get();
        PropertySet& cp = copy->props;
        cp.setString(kOfxImageEffectPropPixelDepth, {kOfxBitDepthFloat});
        cp.setString(kOfxImageEffectPropComponents, {kOfxImageComponentRGBA});
        cp.setString(kOfxImageClipPropUnmappedPixelDepth, {kOfxBitDepthFloat});
        cp.setString(kOfxImageClipPropUnmappedComponents, {kOfxImageComponentRGBA});
        cp.setString(kOfxImageEffectPropPreMultiplication, {kOfxImagePreMultiplied});
        cp.setDouble(kOfxImagePropPixelAspectRatio, {1.0});
        cp.setString(kOfxImageClipPropFieldOrder, {kOfxImageFieldNone});
        cp.setInt(kOfxImageClipPropContinuousSamples, {0});
        inst->clips.push_back(std::move(copy));
    }
    for (const auto& p : desc.params.params) {
        auto copy = std::make_unique<Param>();
        copy->type = p->type;
        copy->name = p->name;
        copy->props = p->props;
        copy->owner = inst.get();
        inst->params.params.push_back(std::move(copy));
    }
    inst->params.props = desc.params.props;
    currentPlugin() = info.id;
    const OfxStatus s = inst->plugin->mainEntry(kOfxActionCreateInstance, reinterpret_cast<OfxImageEffectHandle>(inst.get()), nullptr, nullptr);
    currentPlugin().clear();
    Instance* out = inst.get();
    if (!succeeded(s)) {
        out->failed = true;
        message(nullptr, kOfxMessageError, nullptr, "%s could not be started (%s)", info.id.toUtf8().constData(), statusText(s).toUtf8().constData());
        out = nullptr;
    }
    all[info.id] = std::move(inst);
    return out;
}

// Renders the plug-in's Output clip for `r`; nothing when it fails (said once in the message handler's output).
std::optional<Picture> render(const OfxPluginInfo& info, RenderContext& r) {
    std::lock_guard lock(renderMutex());
    Instance* inst = instanceFor(info);
    if (!inst || r.width <= 0 || r.height <= 0) return std::nullopt;
    inst->render = &r;
    setProjectProps(*inst, r);
    currentPlugin() = info.id;
    auto handleOf = reinterpret_cast<OfxImageEffectHandle>(inst);
    const std::vector<double> renderScale{r.scaleX, r.scaleY};
    const std::vector<int> window{0, 0, r.width, r.height};

    std::optional<Picture> result;
    // An identity: the plug-in says a clip's picture is the output (kOfxImageEffectActionIsIdentity).
    PropertySet identityIn, identityOut;
    identityIn.setDouble(kOfxPropTime, {r.frame});
    identityIn.setString(kOfxImageEffectPropFieldToRender, {kOfxImageFieldNone});
    identityIn.setInt(kOfxImageEffectPropRenderWindow, window);
    identityIn.setDouble(kOfxImageEffectPropRenderScale, renderScale);
    identityOut.setString(kOfxPropName, {""});
    identityOut.setDouble(kOfxPropTime, {r.frame});
    if (inst->plugin->mainEntry(kOfxImageEffectActionIsIdentity, handleOf, handle(&identityIn), handle(&identityOut)) == kOfxStatOK) {
        const std::string name = identityOut.string(kOfxPropName).toStdString();
        if (const auto it = r.inputs.find(name); it != r.inputs.end() && it->second) result = *it->second;
    }
    if (!result) {
        Image output;
        output.data.assign(static_cast<std::size_t>(r.width) * static_cast<std::size_t>(r.height) * 4, 0.0f);
        setImageProps(output, kOfxImageEffectOutputClipName, r.frame, r.width, r.height, r.scaleX, r.scaleY);
        inst->output = &output;
        PropertySet seq;
        seq.setDouble(kOfxImageEffectPropFrameRange, {r.frame, r.frame});
        seq.setDouble(kOfxImageEffectPropFrameStep, {1.0});
        seq.setInt(kOfxPropIsInteractive, {0});
        seq.setDouble(kOfxImageEffectPropRenderScale, renderScale);
        seq.setInt(kOfxImageEffectPropSequentialRenderStatus, {0});
        seq.setInt(kOfxImageEffectPropInteractiveRenderStatus, {0});
        PropertySet in;
        in.setDouble(kOfxPropTime, {r.frame});
        in.setString(kOfxImageEffectPropFieldToRender, {kOfxImageFieldNone});
        in.setInt(kOfxImageEffectPropRenderWindow, window);
        in.setDouble(kOfxImageEffectPropRenderScale, renderScale);
        in.setInt(kOfxImageEffectPropSequentialRenderStatus, {0});
        in.setInt(kOfxImageEffectPropInteractiveRenderStatus, {0});
        in.setInt(kOfxImageEffectPropRenderQualityDraft, {0});
        const OfxStatus begin = inst->plugin->mainEntry(kOfxImageEffectActionBeginSequenceRender, handleOf, handle(&seq), nullptr);
        const OfxStatus s = succeeded(begin) || begin == kOfxStatReplyDefault
                                ? inst->plugin->mainEntry(kOfxImageEffectActionRender, handleOf, handle(&in), nullptr)
                                : begin;
        inst->plugin->mainEntry(kOfxImageEffectActionEndSequenceRender, handleOf, handle(&seq), nullptr);
        inst->output = nullptr;
        if (succeeded(s)) {
            Picture out(r.width, r.height);
            for (int y = 0; y < r.height; ++y) {
                std::copy_n(&output.data[static_cast<std::size_t>(y) * static_cast<std::size_t>(r.width) * 4], static_cast<std::size_t>(r.width) * 4,
                            out.pixel(0, r.height - 1 - y));
            }
            result = std::move(out);
        } else if (!inst->reported) {
            inst->reported = true;
            message(nullptr, kOfxMessageError, nullptr, "the render failed (%s)", statusText(s).toUtf8().constData());
        }
    }
    // Images the plug-in did not release go with the render.
    inst->images.clear();
    inst->render = nullptr;
    currentPlugin().clear();
    return result;
}

} // namespace

void markOfxEffect(model::Effect& effect, const OfxPluginInfo& plugin) {
    effect.parameters.insert(kOfxPluginTag, QJsonObject{{QStringLiteral("id"), plugin.id},
                                                        {QStringLiteral("label"), plugin.label},
                                                        {QStringLiteral("version"), QStringLiteral("%1.%2").arg(plugin.versionMajor).arg(plugin.versionMinor)}});
}

QString ofxTaggedPlugin(const model::Effect& effect) {
    return effect.parameters.value(kOfxPluginTag).toObject().value(QStringLiteral("id")).toString();
}

bool isMissingOfxEffect(const model::Effect& effect) {
    return !ofxTaggedPlugin(effect).isEmpty() && !ofxPluginOf(effect);
}

namespace {

// Says once per plug-in that a tagged effect's plug-in cannot be found (p.163).
void reportMissing(const model::Effect& effect) {
    static std::set<QString> reported;
    std::lock_guard lock(renderMutex());
    const QString id = ofxTaggedPlugin(effect);
    if (!reported.insert(id).second) return;
    const QString label = effect.parameters.value(kOfxPluginTag).toObject().value(QStringLiteral("label")).toString();
    const QString text = QStringLiteral("Can't find effect: %1 (%2)").arg(label.isEmpty() ? effect.name : label, id);
    if (messageHandler()) messageHandler()(text);
    else qWarning("OFX %s", qPrintable(text));
}

// Values plug-ins set during a push button's instance changed action (ofxPushButton), by parameter name; null
// otherwise, when values set are not kept.
std::map<std::string, std::vector<double>>* gSetValues = nullptr;

// Reads the values of a parameter type from setValue's arguments (ofxParam.h).
std::vector<double> readValues(const Param& p, va_list ap) {
    std::vector<double> out;
    const std::string& type = p.type;
    const bool integral = type == kOfxParamTypeInteger || type == kOfxParamTypeInteger2D || type == kOfxParamTypeInteger3D ||
                          type == kOfxParamTypeBoolean || type == kOfxParamTypeChoice;
    const int n = dimensionOf(type);
    for (int i = 0; i < n; ++i) out.push_back(integral ? static_cast<double>(va_arg(ap, int)) : va_arg(ap, double));
    return out;
}

bool keepsValues(const Param& p) {
    const std::string& t = p.type;
    return gSetValues && t != kOfxParamTypeString && t != kOfxParamTypeStrChoice && t != kOfxParamTypeCustom && t != kOfxParamTypeGroup &&
           t != kOfxParamTypePage && t != kOfxParamTypePushButton && t != kOfxParamTypeParametric && t != kOfxParamTypeBytes;
}

OfxStatus paramSetValue(OfxParamHandle h, ...) {
    if (!h) return kOfxStatErrBadHandle;
    Param& p = *param(h);
    if (!keepsValues(p)) return kOfxStatOK; // ours: not kept outside a push button's action
    va_list ap;
    va_start(ap, h);
    (*gSetValues)[p.name] = readValues(p, ap);
    va_end(ap);
    return kOfxStatOK;
}

OfxStatus paramSetValueAtTime(OfxParamHandle h, OfxTime time, ...) {
    if (!h) return kOfxStatErrBadHandle;
    Param& p = *param(h);
    if (!keepsValues(p)) return kOfxStatOK;
    va_list ap;
    va_start(ap, time);
    // Ours: a value set at a time is kept as the parameter's value (keyframes are the Effect Editor's).
    (*gSetValues)[p.name] = readValues(p, ap);
    va_end(ap);
    return kOfxStatOK;
}

// The values a plug-in set (gSetValues) as the Effect Editor's controls keep them (ofxEditorParameters), by stored key.
QJsonObject keptValues(const OfxPluginInfo& plugin, const std::map<std::string, std::vector<double>>& set) {
    const OfxPluginInfo* info = &plugin;
    QJsonObject out;
    for (const auto& [name, values] : set) {
        const OfxParamInfo* p = paramInfoOf(*info, name);
        if (!p || values.empty()) continue;
        const std::string type = p->type.toStdString();
        if (type == kOfxParamTypeBoolean) {
            out.insert(p->name, values[0] != 0.0);
        } else if (type == kOfxParamTypeChoice) {
            const int i = static_cast<int>(values[0]);
            if (i >= 0 && i < p->options.size()) out.insert(p->name, p->options.at(i));
        } else if (type == kOfxParamTypeRGB || type == kOfxParamTypeRGBA) {
            if (values.size() >= 3) {
                out.insert(p->name, QColor::fromRgbF(static_cast<float>(std::clamp(values[0], 0.0, 1.0)), static_cast<float>(std::clamp(values[1], 0.0, 1.0)),
                                                     static_cast<float>(std::clamp(values[2], 0.0, 1.0)))
                                        .name());
            }
        } else if (values.size() == 1) {
            out.insert(p->name, values[0]);
        } else {
            for (std::size_t i = 0; i < values.size() && i < 3; ++i) out.insert(p->name + QLatin1String(kAxes[i]), values[i]);
        }
    }
    return out;
}

} // namespace

const OfxPluginInfo* ofxPluginOf(const model::Effect& effect) {
    // By the tag: the plug-in whatever its palette name; a tag no registered plug-in has is a missing plug-in.
    if (const QString id = ofxTaggedPlugin(effect); !id.isEmpty()) {
        for (const OfxPluginInfo& p : registered()) {
            if (p.usable() && p.id == id) return &p;
        }
        return nullptr;
    }
    for (const OfxPluginInfo& p : registered()) {
        if (!p.usable() || p.category() != effect.category) continue;
        if (effect.name == p.label || effect.name == QStringLiteral("%1 (%2)").arg(p.label, p.id)) return &p;
    }
    return nullptr;
}

QString ofxParameterCategory(const OfxPluginInfo& plugin) {
    return plugin.label + QStringLiteral(" Plug-In Parameters");
}

QList<OfxEditorParameter> ofxEditorParameters(const OfxPluginInfo& plugin) {
    QList<OfxEditorParameter> out;
    // Groups' labels by name, for their parameters' subcategory.
    QMap<QString, QString> groups;
    for (const OfxParamInfo& p : plugin.params) {
        if (p.type == QLatin1String(kOfxParamTypeGroup)) groups.insert(p.name, p.label);
    }
    const bool transition = plugin.describedContext == QLatin1String(kOfxImageEffectContextTransition);
    for (const OfxParamInfo& p : plugin.params) {
        if (p.secret) continue;
        if (transition && p.name == QLatin1String(kOfxImageEffectTransitionParamName)) continue;
        if (p.type == QLatin1String(kOfxParamTypePushButton)) {
            OfxEditorParameter b;
            b.key = p.name;
            b.text = p.label;
            b.kind = QStringLiteral("button");
            b.subcategory = groups.value(p.parent);
            b.keyframeable = false;
            out << b;
            continue;
        }
        const std::string type = p.type.toStdString();
        const int n = dimensionOf(type);
        OfxEditorParameter e;
        e.key = p.name;
        e.text = p.label;
        e.subcategory = groups.value(p.parent);
        e.keyframeable = p.animates;
        auto number = [&](int i) { return i < p.defaults.size() ? p.defaults.at(i).toDouble() : 0.0; };
        if (type == kOfxParamTypeBoolean) {
            e.kind = QStringLiteral("checkbox");
            e.keyframeable = false;
            e.defaultValue = number(0) != 0.0;
            out << e;
        } else if (type == kOfxParamTypeChoice) {
            e.kind = QStringLiteral("menu");
            e.keyframeable = false;
            e.options = p.options;
            const int i = static_cast<int>(number(0));
            if (i >= 0 && i < p.options.size()) e.defaultValue = p.options.at(i);
            if (!e.options.isEmpty()) out << e;
        } else if (type == kOfxParamTypeRGB || type == kOfxParamTypeRGBA) {
            e.kind = QStringLiteral("color");
            e.defaultValue = QColor::fromRgbF(static_cast<float>(std::clamp(number(0), 0.0, 1.0)), static_cast<float>(std::clamp(number(1), 0.0, 1.0)),
                                              static_cast<float>(std::clamp(number(2), 0.0, 1.0)))
                                 .name();
            out << e;
        } else if (type == kOfxParamTypeDouble || type == kOfxParamTypeInteger || n > 1) {
            if (type != kOfxParamTypeDouble && type != kOfxParamTypeInteger && type != kOfxParamTypeDouble2D && type != kOfxParamTypeInteger2D &&
                type != kOfxParamTypeDouble3D && type != kOfxParamTypeInteger3D) {
                continue;
            }
            e.kind = type == kOfxParamTypeDouble && p.doubleType == QLatin1String(kOfxParamDoubleTypeAngle) ? QStringLiteral("angle")
                                                                                                                : QStringLiteral("slider");
            // The display range, else the range, else 0 to 100 (the 6.11a sentinels mean "no range").
            auto usable = [](double lo, double hi) { return std::isfinite(lo) && std::isfinite(hi) && hi > lo && hi - lo < 1e9; };
            if (p.hasRange && usable(p.displayMin, p.displayMax)) {
                e.min = p.displayMin;
                e.max = p.displayMax;
            } else if (p.hasRange && usable(p.min, p.max)) {
                e.min = p.min;
                e.max = p.max;
            }
            const bool integral = type == kOfxParamTypeInteger || type == kOfxParamTypeInteger2D || type == kOfxParamTypeInteger3D;
            e.decimals = integral ? 0 : e.max - e.min <= 2.0 ? 3 : 2;
            for (int i = 0; i < n; ++i) {
                OfxEditorParameter axis = e;
                if (n > 1) {
                    axis.key = p.name + QLatin1String(kAxes[i]);
                    axis.text = p.label + QLatin1Char(' ') + QChar(QLatin1Char('X').unicode() + i);
                }
                axis.defaultValue = integral ? QJsonValue(static_cast<int>(std::lround(number(i)))) : QJsonValue(number(i));
                out << axis;
            }
        }
    }
    return out;
}

QJsonValue ofxParameterDefault(const model::Effect& effect, const QString& category, const QString& key) {
    const OfxPluginInfo* p = ofxPluginOf(effect);
    if (!p || category != ofxParameterCategory(*p)) return {};
    for (const OfxEditorParameter& e : ofxEditorParameters(*p)) {
        if (e.key == key) return e.defaultValue;
    }
    return {};
}

int ofxInputCount(const model::Effect& effect) {
    const OfxPluginInfo* p = ofxPluginOf(effect);
    const int required = p ? p->requiredInputs() : 1;
    const QJsonValue v = effect.parameters.value(kOfxPluginTag).toObject().value(QStringLiteral("inputs"));
    const int chosen = v.isDouble() ? v.toInt() : required;
    return p ? std::clamp(chosen, required, p->maxInputs()) : chosen;
}

void setOfxInputCount(model::Effect& effect, int count) {
    QJsonObject tag = effect.parameters.value(kOfxPluginTag).toObject();
    tag.insert(QStringLiteral("inputs"), count);
    effect.parameters.insert(kOfxPluginTag, tag);
}

Picture ofxSegmentPicture(const model::Effect& effect, const Picture& source, double time, const model::EffectSpan& span,
                          const QList<Picture>& more) {
    const OfxPluginInfo* info = ofxPluginOf(effect);
    if (!info) {
        if (isMissingOfxEffect(effect)) reportMissing(effect);
        return source;
    }
    RenderContext r;
    r.effect = &effect;
    r.info = info;
    r.span = span;
    r.frame = span.frameOf(time);
    r.width = source.width();
    r.height = source.height();
    fitProject(r);
    // Filter and General plug-ins take the segment as their first input (Source); a Generator has none. Further inputs
    // come from `more`, an empty picture where there is none.
    const QList<OfxClipInfo> connected = info->connectedInputs(ofxInputCount(effect));
    std::vector<Picture> empties;
    empties.reserve(static_cast<std::size_t>(connected.size()));
    for (qsizetype i = 0; i < connected.size(); ++i) {
        const std::string name = connected.at(i).name.toStdString();
        if (i == 0) {
            r.inputs[name] = &source;
        } else if (i - 1 < more.size() && more.at(i - 1).width() == source.width() && more.at(i - 1).height() == source.height()) {
            r.inputs[name] = &more.at(i - 1);
        } else {
            empties.emplace_back(source.width(), source.height());
            r.inputs[name] = &empties.back();
        }
    }
    return render(*info, r).value_or(source);
}

Picture ofxTransitionPicture(const model::Effect& effect, const Picture& from, const Picture& to, double progress, double time,
                             const model::EffectSpan& span) {
    const OfxPluginInfo* info = ofxPluginOf(effect);
    if (!info) {
        // Ours: a missing plug-in's transition dissolves.
        if (isMissingOfxEffect(effect)) reportMissing(effect);
        return mix(from, to, progress);
    }
    RenderContext r;
    r.effect = &effect;
    r.info = info;
    r.span = span;
    r.frame = span.frameOf(time);
    r.progress = std::clamp(progress, 0.0, 1.0);
    r.width = from.width();
    r.height = from.height();
    fitProject(r);
    if (info->describedContext == QLatin1String(kOfxImageEffectContextTransition)) {
        r.inputs[kOfxImageEffectTransitionSourceFromClipName] = &from;
        r.inputs[kOfxImageEffectTransitionSourceToClipName] = &to;
    } else if (info->clips.contains(QLatin1String(kOfxImageEffectSimpleSourceClipName))) {
        // Ours: a segment plug-in on a transition works on the incoming picture over the outgoing one.
        r.inputs[kOfxImageEffectSimpleSourceClipName] = &to;
        const auto out = render(*info, r);
        return out ? over(*out, from) : mix(from, to, progress);
    }
    return render(*info, r).value_or(mix(from, to, progress));
}

QJsonObject ofxPushButton(const model::Effect& effect, const QString& key, double time, const model::EffectSpan& span) {
    const OfxPluginInfo* info = ofxPluginOf(effect);
    if (!info) return {};
    std::lock_guard lock(renderMutex());
    Instance* inst = instanceFor(*info);
    if (!inst) return {};
    RenderContext r;
    r.effect = &effect;
    r.info = info;
    r.span = span;
    r.frame = span.frameOf(time);
    fitProject(r);
    inst->render = &r;
    std::map<std::string, std::vector<double>> set;
    gSetValues = &set;
    currentPlugin() = info->id;
    PropertySet in;
    in.setString(kOfxPropType, {kOfxTypeParameter});
    in.setString(kOfxPropName, {key.toStdString()});
    in.setString(kOfxPropChangeReason, {kOfxChangeUserEdited});
    in.setDouble(kOfxPropTime, {r.frame});
    in.setDouble(kOfxImageEffectPropRenderScale, {1.0, 1.0});
    auto* h = reinterpret_cast<OfxImageEffectHandle>(inst);
    inst->plugin->mainEntry(kOfxActionBeginInstanceChanged, h, nullptr, nullptr);
    inst->plugin->mainEntry(kOfxActionInstanceChanged, h, handle(&in), nullptr);
    inst->plugin->mainEntry(kOfxActionEndInstanceChanged, h, nullptr, nullptr);
    gSetValues = nullptr;
    inst->render = nullptr;
    currentPlugin().clear();
    return keptValues(*info, set);
}

// ---------------------------------------------------------------- overlays (6.11c2)

namespace {

// The shapes of one draw action, and the drawing state the plug-in sets.
struct DrawContext {
    QList<OfxOverlayShape>* shapes = nullptr;
    double projectWidth = 1920.0, projectHeight = 1080.0;
    QColor colour = Qt::white;
    double width = 0.0;
    bool dashed = false;
};

DrawContext* drawContext(OfxDrawContextHandle h) {
    return reinterpret_cast<DrawContext*>(h);
}

QPointF fractionOf(const DrawContext& dc, const OfxPointD& p) {
    return QPointF(p.x / dc.projectWidth, 1.0 - p.y / dc.projectHeight);
}

// The host's colours for the standard uses (ours).
OfxStatus drawGetColour(OfxDrawContextHandle h, OfxStandardColour which, OfxRGBAColourF* colour) {
    if (!h || !colour) return kOfxStatFailed;
    QColor c;
    switch (which) {
    case kOfxStandardColourOverlayBackground: c = QColor(0, 0, 0); break;
    case kOfxStandardColourOverlayActive: c = QColor(255, 210, 60); break;
    case kOfxStandardColourOverlaySelected: c = QColor(255, 255, 255); break;
    case kOfxStandardColourOverlayDeselected: c = QColor(160, 160, 160); break;
    case kOfxStandardColourOverlayMarqueeFG: c = QColor(255, 255, 255); break;
    case kOfxStandardColourOverlayMarqueeBG: c = QColor(0, 0, 0); break;
    case kOfxStandardColourOverlayText: c = QColor(255, 255, 255); break;
    default: return kOfxStatErrValue;
    }
    *colour = OfxRGBAColourF{static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()), 1.0f};
    return kOfxStatOK;
}

OfxStatus drawSetColour(OfxDrawContextHandle h, const OfxRGBAColourF* colour) {
    if (!h || !colour) return kOfxStatFailed;
    drawContext(h)->colour = QColor::fromRgbF(std::clamp(colour->r, 0.0f, 1.0f), std::clamp(colour->g, 0.0f, 1.0f), std::clamp(colour->b, 0.0f, 1.0f),
                                              std::clamp(colour->a, 0.0f, 1.0f));
    return kOfxStatOK;
}

OfxStatus drawSetLineWidth(OfxDrawContextHandle h, float width) {
    if (!h) return kOfxStatFailed;
    drawContext(h)->width = static_cast<double>(std::max(0.0f, width));
    return kOfxStatOK;
}

OfxStatus drawSetLineStipple(OfxDrawContextHandle h, OfxDrawLineStipplePattern pattern) {
    if (!h) return kOfxStatFailed;
    // Ours: every pattern but solid is drawn dashed.
    drawContext(h)->dashed = pattern != kOfxDrawLineStipplePatternSolid;
    return kOfxStatOK;
}

OfxStatus drawPrimitive(OfxDrawContextHandle h, OfxDrawPrimitive primitive, const OfxPointD* points, int count) {
    if (!h) return kOfxStatFailed;
    DrawContext& dc = *drawContext(h);
    if (!points || count < 1) return kOfxStatErrValue;
    OfxOverlayShape s;
    switch (primitive) {
    case kOfxDrawPrimitiveLines: s.kind = OfxOverlayShape::Kind::Lines; break;
    case kOfxDrawPrimitiveLineStrip: s.kind = OfxOverlayShape::Kind::LineStrip; break;
    case kOfxDrawPrimitiveLineLoop: s.kind = OfxOverlayShape::Kind::LineLoop; break;
    case kOfxDrawPrimitiveRectangle: s.kind = OfxOverlayShape::Kind::Rectangle; break;
    case kOfxDrawPrimitivePolygon: s.kind = OfxOverlayShape::Kind::Polygon; break;
    case kOfxDrawPrimitiveEllipse: s.kind = OfxOverlayShape::Kind::Ellipse; break;
    default: return kOfxStatErrValue;
    }
    const bool twoCorners = s.kind == OfxOverlayShape::Kind::Rectangle || s.kind == OfxOverlayShape::Kind::Ellipse;
    if ((twoCorners && count != 2) || (s.kind == OfxOverlayShape::Kind::Lines && count % 2 != 0)) return kOfxStatErrValue;
    s.colour = dc.colour;
    s.width = dc.width;
    s.dashed = dc.dashed;
    for (int i = 0; i < count; ++i) s.points << fractionOf(dc, points[i]);
    if (dc.shapes) dc.shapes->append(s);
    return kOfxStatOK;
}

OfxStatus drawText(OfxDrawContextHandle h, const char* text, const OfxPointD* pos, int alignment) {
    if (!h) return kOfxStatFailed;
    if (!text || !pos) return kOfxStatErrValue;
    DrawContext& dc = *drawContext(h);
    OfxOverlayShape s;
    s.kind = OfxOverlayShape::Kind::Text;
    s.colour = dc.colour;
    s.points << fractionOf(dc, *pos);
    s.text = QString::fromUtf8(text);
    s.alignment = alignment;
    if (dc.shapes) dc.shapes->append(s);
    return kOfxStatOK;
}

Interact* interact(OfxInteractHandle h) {
    return reinterpret_cast<Interact*>(h);
}

OfxStatus interactSwapBuffers(OfxInteractHandle h) {
    return h ? kOfxStatOK : kOfxStatErrBadHandle;
}

OfxStatus interactRedraw(OfxInteractHandle h) {
    if (!h) return kOfxStatErrBadHandle;
    interact(h)->redraw = true;
    return kOfxStatOK;
}

OfxStatus interactGetPropertySet(OfxInteractHandle h, OfxPropertySetHandle* props) {
    if (!h || !props) return kOfxStatErrBadHandle;
    *props = handle(&interact(h)->props);
    return kOfxStatOK;
}

} // namespace

namespace {

const OfxInteractSuiteV1 kInteractSuite{interactSwapBuffers, interactRedraw, interactGetPropertySet};
const OfxDrawSuiteV1 kDrawSuite{drawGetColour, drawSetColour, drawSetLineWidth, drawSetLineStipple, drawPrimitive, drawText};

// The instance's overlay interact, described and made when first wanted; null when the plug-in has none we draw.
Interact* overlayFor(Instance& inst) {
    if (inst.overlay) return inst.overlay.get();
    if (inst.noOverlay) return nullptr;
    void* entry = nullptr;
    if (const Property* p = inst.props.find(kOfxImageEffectPluginPropOverlayInteractV2);
        p && p->type == Property::Type::Pointer && !p->pointers.empty()) {
        entry = p->pointers.front();
    }
    if (!entry) {
        inst.noOverlay = true;
        return nullptr;
    }
    auto* main = reinterpret_cast<OfxPluginEntryPoint*>(entry);
    // Its descriptor (kOfxActionDescribe), then its instance (kOfxActionCreateInstance).
    Interact descriptor;
    descriptor.entry = main;
    descriptor.props.setInt(kOfxInteractPropHasAlpha, {1});
    descriptor.props.setInt(kOfxInteractPropBitDepth, {8});
    currentPlugin() = inst.id;
    main(kOfxActionDescribe, reinterpret_cast<OfxInteractHandle>(&descriptor), nullptr, nullptr);
    auto overlay = std::make_unique<Interact>();
    overlay->entry = main;
    overlay->props = descriptor.props;
    overlay->props.setPointer(kOfxPropEffectInstance, {&inst});
    overlay->props.setPointer(kOfxPropInstanceData, {nullptr});
    overlay->props.setDouble(kOfxInteractPropPixelScale, {1.0, 1.0});
    overlay->props.setDouble(kOfxInteractPropBackgroundColour, {0.0, 0.0, 0.0});
    overlay->props.setDouble(kOfxInteractPropSuggestedColour, {1.0, 1.0, 0.0});
    overlay->props.setString(kOfxInteractPropSlaveToParam, {});
    const OfxStatus s = main(kOfxActionCreateInstance, reinterpret_cast<OfxInteractHandle>(overlay.get()), nullptr, nullptr);
    currentPlugin().clear();
    if (!succeeded(s)) {
        inst.noOverlay = true;
        return nullptr;
    }
    inst.overlay = std::move(overlay);
    return inst.overlay.get();
}

// The arguments every interact action gets.
void interactArgs(PropertySet& in, Instance& inst, const RenderContext& r, double pixelScale) {
    in.setPointer(kOfxPropEffectInstance, {&inst});
    in.setDouble(kOfxInteractPropPixelScale, {pixelScale, pixelScale});
    in.setDouble(kOfxInteractPropBackgroundColour, {0.0, 0.0, 0.0});
    in.setDouble(kOfxPropTime, {r.frame});
    in.setDouble(kOfxImageEffectPropRenderScale, {1.0, 1.0});
}

} // namespace

bool ofxHasOverlay(const model::Effect& effect) {
    const OfxPluginInfo* info = ofxPluginOf(effect);
    if (!info) return false;
    std::lock_guard lock(renderMutex());
    Instance* inst = instanceFor(*info);
    return inst && overlayFor(*inst);
}

QList<OfxOverlayShape> ofxOverlayDraw(const model::Effect& effect, double time, const model::EffectSpan& span, double pixelScale) {
    const OfxPluginInfo* info = ofxPluginOf(effect);
    if (!info) return {};
    std::lock_guard lock(renderMutex());
    Instance* inst = instanceFor(*info);
    Interact* overlay = inst ? overlayFor(*inst) : nullptr;
    if (!overlay) return {};
    RenderContext r;
    r.effect = &effect;
    r.info = info;
    r.span = span;
    r.frame = span.frameOf(time);
    fitProject(r);
    inst->render = &r;
    QList<OfxOverlayShape> shapes;
    DrawContext dc;
    dc.shapes = &shapes;
    dc.projectWidth = r.projectWidth;
    dc.projectHeight = r.projectHeight;
    PropertySet in;
    interactArgs(in, *inst, r, pixelScale);
    in.setPointer(kOfxInteractPropDrawContext, {&dc});
    currentPlugin() = info->id;
    overlay->entry(kOfxInteractActionDraw, reinterpret_cast<OfxInteractHandle>(overlay), handle(&in), nullptr);
    currentPlugin().clear();
    overlay->redraw = false;
    inst->render = nullptr;
    return shapes;
}

OfxPenResult ofxOverlayPen(const model::Effect& effect, OfxPen pen, const QPointF& at, double time, const model::EffectSpan& span,
                           double pixelScale) {
    OfxPenResult out;
    const OfxPluginInfo* info = ofxPluginOf(effect);
    if (!info) return out;
    std::lock_guard lock(renderMutex());
    Instance* inst = instanceFor(*info);
    Interact* overlay = inst ? overlayFor(*inst) : nullptr;
    if (!overlay) return out;
    RenderContext r;
    r.effect = &effect;
    r.info = info;
    r.span = span;
    r.frame = span.frameOf(time);
    fitProject(r);
    inst->render = &r;
    std::map<std::string, std::vector<double>> set;
    gSetValues = &set;
    PropertySet in;
    interactArgs(in, *inst, r, pixelScale);
    const double x = at.x() * r.projectWidth, y = (1.0 - at.y()) * r.projectHeight;
    in.setDouble(kOfxInteractPropPenPosition, {x, y});
    in.setInt(kOfxInteractPropPenViewportPosition, {static_cast<int>(std::lround(x / pixelScale)), static_cast<int>(std::lround((r.projectHeight - y) / pixelScale))});
    in.setDouble(kOfxInteractPropPenPressure, {pen == OfxPen::Up ? 0.0 : 1.0});
    const char* action = pen == OfxPen::Down ? kOfxInteractActionPenDown : pen == OfxPen::Motion ? kOfxInteractActionPenMotion : kOfxInteractActionPenUp;
    currentPlugin() = info->id;
    const OfxStatus s = overlay->entry(action, reinterpret_cast<OfxInteractHandle>(overlay), handle(&in), nullptr);
    currentPlugin().clear();
    gSetValues = nullptr;
    inst->render = nullptr;
    out.taken = s == kOfxStatOK;
    out.values = keptValues(*info, set);
    return out;
}

void setOfxProjectSize(const QSize& size) {
    gProjectSize = size;
}

QSize ofxProjectSize() {
    return gProjectSize.isValid() && !gProjectSize.isEmpty() ? gProjectSize : QSize(1920, 1080);
}

void setOfxFrameRate(double rate) {
    if (rate > 0.0) gFrameRate = rate;
}

int closeOfxInstances() {
    std::lock_guard lock(renderMutex());
    int closed = 0;
    for (auto& [id, inst] : instances()) {
        if (inst->failed) continue;
        currentPlugin() = id;
        if (inst->overlay) {
            inst->overlay->entry(kOfxActionDestroyInstance, reinterpret_cast<OfxInteractHandle>(inst->overlay.get()), nullptr, nullptr);
            inst->overlay.reset();
        }
        inst->plugin->mainEntry(kOfxActionDestroyInstance, reinterpret_cast<OfxImageEffectHandle>(inst.get()), nullptr, nullptr);
        ++closed;
    }
    currentPlugin().clear();
    instances().clear();
    return closed;
}

} // namespace lumenis::media
