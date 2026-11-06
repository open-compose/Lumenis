// An OFX binary of our own for the OFX host's tests (tests/media/tst_ofx_host.cpp; docs/spec/ofx-host.md), built as
// CutthulhuTest.ofx inside test-ofx/CutthulhuTest.ofx.bundle/Contents/<architecture>/. It offers five plug-ins:
// - "org.cutthulhu.test.tint": a Filter in "Cutthulhu Tests/Colour" with Amount (double, 0-1, default 0.5), Channel (a
//   choice of Red, Green, Blue), Invert (a boolean), Colour (RGB) and a secret one; its description names the host and
//   the threads the multithread suite ran;
// - "org.cutthulhu.test.wipe": a Transition, no grouping;
// - "org.cutthulhu.test.noise": a Generator and General plug-in, which also sends a message while described;
// - "org.cutthulhu.test.broken": its describe action sends an error message and fails;
// - "org.cutthulhu.test.paint": only the Paint context, which the host leaves out.
// Plus one plug-in of another API, which the host skips.
#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxMessage.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxProperty.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

const OfxHost* gHost = nullptr;
const OfxPropertySuiteV1* gProps = nullptr;
const OfxImageEffectSuiteV1* gEffects = nullptr;
const OfxParameterSuiteV1* gParams = nullptr;
const OfxMessageSuiteV1* gMessages = nullptr;
const OfxMultiThreadSuiteV1* gThreads = nullptr;

std::atomic<int> gThreadCalls{0};
void countThread(unsigned int, unsigned int, void*) {
    ++gThreadCalls;
}

void setHost(OfxHost* host) {
    gHost = host;
}

OfxStatus load() {
    if (!gHost) return kOfxStatErrMissingHostFeature;
    gProps = static_cast<const OfxPropertySuiteV1*>(gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1));
    gEffects = static_cast<const OfxImageEffectSuiteV1*>(gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1));
    gParams = static_cast<const OfxParameterSuiteV1*>(gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1));
    gMessages = static_cast<const OfxMessageSuiteV1*>(gHost->fetchSuite(gHost->host, kOfxMessageSuite, 1));
    gThreads = static_cast<const OfxMultiThreadSuiteV1*>(gHost->fetchSuite(gHost->host, kOfxMultiThreadSuite, 1));
    // A suite the host does not have answers null.
    if (gHost->fetchSuite(gHost->host, "OfxNoSuchSuite", 1)) return kOfxStatFailed;
    return gProps && gEffects && gParams && gMessages && gThreads ? kOfxStatOK : kOfxStatErrMissingHostFeature;
}

OfxPropertySetHandle effectProps(OfxImageEffectHandle e) {
    OfxPropertySetHandle p = nullptr;
    gEffects->getPropertySet(e, &p);
    return p;
}

void defineClip(OfxImageEffectHandle e, const char* name) {
    OfxPropertySetHandle p = nullptr;
    gEffects->clipDefine(e, name, &p);
    gProps->propSetString(p, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
}

// ---------------------------------------------------------------- tint

OfxStatus tintDescribe(OfxImageEffectHandle e) {
    OfxPropertySetHandle p = effectProps(e);
    gProps->propSetString(p, kOfxPropLabel, 0, "Test Tint");
    gProps->propSetString(p, kOfxImageEffectPluginPropGrouping, 0, "Cutthulhu Tests/Colour");
    gProps->propSetString(p, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    gProps->propSetString(p, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);
    // The host's name and the threads its multithread suite ran, read back by the test.
    char* hostName = nullptr;
    gProps->propGetString(gHost->host, kOfxPropName, 0, &hostName);
    int apiMajor = 0, apiMinor = 0;
    gProps->propGetInt(gHost->host, kOfxPropAPIVersion, 0, &apiMajor);
    gProps->propGetInt(gHost->host, kOfxPropAPIVersion, 1, &apiMinor);
    gThreadCalls = 0;
    gThreads->multiThread(countThread, 4, nullptr);
    char text[256];
    std::snprintf(text, sizeof text, "host %s %d.%d threads %d", hostName ? hostName : "?", apiMajor, apiMinor, gThreadCalls.load());
    gProps->propSetString(p, kOfxPropPluginDescription, 0, text);
    return kOfxStatOK;
}

OfxStatus tintDescribeInContext(OfxImageEffectHandle e) {
    defineClip(e, kOfxImageEffectSimpleSourceClipName);
    defineClip(e, kOfxImageEffectOutputClipName);
    OfxParamSetHandle params = nullptr;
    gEffects->getParamSet(e, &params);
    OfxPropertySetHandle p = nullptr;
    gParams->paramDefine(params, kOfxParamTypeDouble, "amount", &p);
    gProps->propSetString(p, kOfxPropLabel, 0, "Amount");
    gProps->propSetString(p, kOfxParamPropHint, 0, "How much tint");
    gProps->propSetDouble(p, kOfxParamPropDefault, 0, 0.5);
    gProps->propSetDouble(p, kOfxParamPropMin, 0, 0.0);
    gProps->propSetDouble(p, kOfxParamPropMax, 0, 1.0);
    gProps->propSetDouble(p, kOfxParamPropDisplayMin, 0, 0.0);
    gProps->propSetDouble(p, kOfxParamPropDisplayMax, 0, 1.0);
    gParams->paramDefine(params, kOfxParamTypeChoice, "channel", &p);
    gProps->propSetString(p, kOfxPropLabel, 0, "Channel");
    const char* const options[] = {"Red", "Green", "Blue"};
    gProps->propSetStringN(p, kOfxParamPropChoiceOption, 3, options);
    gProps->propSetInt(p, kOfxParamPropDefault, 0, 2);
    gParams->paramDefine(params, kOfxParamTypeBoolean, "invert", &p);
    gProps->propSetString(p, kOfxPropLabel, 0, "Invert");
    gProps->propSetInt(p, kOfxParamPropDefault, 0, 1);
    gParams->paramDefine(params, kOfxParamTypeRGB, "colour", &p);
    gProps->propSetString(p, kOfxPropLabel, 0, "Colour");
    const double colour[] = {1.0, 0.5, 0.25};
    gProps->propSetDoubleN(p, kOfxParamPropDefault, 3, colour);
    gParams->paramDefine(params, kOfxParamTypeInteger, "hidden", &p);
    gProps->propSetInt(p, kOfxParamPropSecret, 0, 1);
    // Defining a name twice, and an unknown type, fail.
    if (gParams->paramDefine(params, kOfxParamTypeDouble, "amount", nullptr) != kOfxStatErrExists) return kOfxStatFailed;
    if (gParams->paramDefine(params, "NoSuchType", "other", nullptr) == kOfxStatOK) return kOfxStatFailed;
    // A defined parameter is found by its name.
    OfxParamHandle handle = nullptr;
    if (gParams->paramGetHandle(params, "channel", &handle, nullptr) != kOfxStatOK || !handle) return kOfxStatFailed;
    // Reading what is not there.
    double d = 0;
    if (gProps->propGetDouble(p, "NoSuchProperty", 0, &d) != kOfxStatErrUnknown) return kOfxStatFailed;
    int n = 0;
    gProps->propGetDimension(effectProps(e), kOfxImageEffectPropSupportedContexts, &n);
    return n == 1 ? kOfxStatOK : kOfxStatFailed;
}

// ---------------------------------------------------------------- wipe, noise, broken, paint

OfxStatus wipeDescribe(OfxImageEffectHandle e) {
    OfxPropertySetHandle p = effectProps(e);
    gProps->propSetString(p, kOfxPropLabel, 0, "Test Wipe");
    gProps->propSetString(p, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextTransition);
    return kOfxStatOK;
}

OfxStatus wipeDescribeInContext(OfxImageEffectHandle e) {
    defineClip(e, kOfxImageEffectTransitionSourceFromClipName);
    defineClip(e, kOfxImageEffectTransitionSourceToClipName);
    defineClip(e, kOfxImageEffectOutputClipName);
    OfxParamSetHandle params = nullptr;
    gEffects->getParamSet(e, &params);
    gParams->paramDefine(params, kOfxParamTypeDouble, kOfxImageEffectTransitionParamName, nullptr);
    return kOfxStatOK;
}

OfxStatus noiseDescribe(OfxImageEffectHandle e) {
    OfxPropertySetHandle p = effectProps(e);
    gProps->propSetString(p, kOfxPropLabel, 0, "Test Noise");
    gProps->propSetString(p, kOfxImageEffectPluginPropGrouping, 0, "Cutthulhu Tests");
    const char* const contexts[] = {kOfxImageEffectContextGeneral, kOfxImageEffectContextGenerator};
    gProps->propSetStringN(p, kOfxImageEffectPropSupportedContexts, 2, contexts);
    gMessages->message(nullptr, kOfxMessageMessage, nullptr, "noise has %d octaves", 3);
    return kOfxStatOK;
}

OfxStatus noiseDescribeInContext(OfxImageEffectHandle e, OfxPropertySetHandle in) {
    // Described in the first of its contexts the host takes: Generator before General.
    char* context = nullptr;
    gProps->propGetString(in, kOfxImageEffectPropContext, 0, &context);
    defineClip(e, kOfxImageEffectOutputClipName);
    OfxParamSetHandle params = nullptr;
    gEffects->getParamSet(e, &params);
    OfxPropertySetHandle p = nullptr;
    gParams->paramDefine(params, kOfxParamTypeString, "context", &p);
    gProps->propSetString(p, kOfxParamPropDefault, 0, context ? context : "");
    return kOfxStatOK;
}

OfxStatus brokenDescribe(OfxImageEffectHandle) {
    gMessages->message(nullptr, kOfxMessageError, nullptr, "no licence found");
    return kOfxStatFailed;
}

OfxStatus paintDescribe(OfxImageEffectHandle e) {
    OfxPropertySetHandle p = effectProps(e);
    gProps->propSetString(p, kOfxPropLabel, 0, "Test Paint");
    gProps->propSetString(p, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextPaint);
    return kOfxStatOK;
}

template <int Kind>
OfxStatus mainEntry(const char* action, const void* handle, OfxPropertySetHandle in, OfxPropertySetHandle) {
    auto* e = static_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
    if (std::strcmp(action, kOfxActionLoad) == 0) return load();
    if (std::strcmp(action, kOfxActionDescribe) == 0) {
        switch (Kind) {
        case 0: return tintDescribe(e);
        case 1: return wipeDescribe(e);
        case 2: return noiseDescribe(e);
        case 3: return brokenDescribe(e);
        default: return paintDescribe(e);
        }
    }
    if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
        switch (Kind) {
        case 0: return tintDescribeInContext(e);
        case 1: return wipeDescribeInContext(e);
        case 2: return noiseDescribeInContext(e, in);
        default: return kOfxStatOK;
        }
    }
    return kOfxStatReplyDefault;
}

OfxPlugin gPlugins[] = {
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.tint", 1, 2, setHost, mainEntry<0>},
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.wipe", 1, 0, setHost, mainEntry<1>},
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.noise", 1, 0, setHost, mainEntry<2>},
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.broken", 1, 0, setHost, mainEntry<3>},
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.paint", 1, 0, setHost, mainEntry<4>},
    {"SomeOtherApi", 1, "org.cutthulhu.test.other", 1, 0, setHost, mainEntry<4>},
};

} // namespace

OfxExport int OfxGetNumberOfPlugins(void) {
    return static_cast<int>(sizeof gPlugins / sizeof gPlugins[0]);
}

OfxExport OfxPlugin* OfxGetPlugin(int nth) {
    return nth >= 0 && nth < OfxGetNumberOfPlugins() ? &gPlugins[nth] : nullptr;
}
