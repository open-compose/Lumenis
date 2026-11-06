// An OFX binary of our own for the OFX host's tests (tests/media/tst_ofx_host.cpp; docs/spec/ofx-host.md), built as
// CutthulhuTest.ofx inside test-ofx/CutthulhuTest.ofx.bundle/Contents/<architecture>/. It offers seven plug-ins:
// - "org.cutthulhu.test.tint": a Filter in "Cutthulhu Tests/Colour" with Amount (double, 0-1, default 0.5), Channel (a
//   choice of Red, Green, Blue), Invert (a boolean), Colour (RGB) and a secret one; its description names the host and
//   the threads the multithread suite ran. It renders the source with the chosen channel set to Amount (times the
//   alpha), then inverted if Invert is on; with Amount 0 and Invert off it is an identity of Source;
// - "org.cutthulhu.test.wipe": a Transition, no grouping. It renders the incoming picture left of the Transition
//   parameter's fraction of the width and the outgoing one right of it;
// - "org.cutthulhu.test.noise": a Generator and General plug-in, which also sends a message while described. It
//   renders its Fill colour (RGB, default 0.25, 0.5, 0.75) and a white bottom row, which shows the rows' order;
// - "org.cutthulhu.test.broken": its describe action sends an error message and fails;
// - "org.cutthulhu.test.paint": only the Paint context, which the host leaves out;
// - "org.cutthulhu.test.composite" (6.11c1): a General plug-in in "Cutthulhu Mixers" with Source, an optional Over and
//   an optional Matte (a mask), a Spin angle (degrees) and a Square push button that sets Spin to 90. It renders
//   Source, with Over shown through the Matte's red where both are connected, or Over over Source with Over alone;
//   its colours are inverted while Spin is 180 or more. 6.11c2: a Centre (Double2D in canonical coordinates, default
//   960, 540), marked white in the picture at the render scale; and an overlay (V2, the Draw suite) drawing a yellow
//   square of 40 units round the Centre with the text "centre", which a press within 30 units of it takes and drags.
// - "org.cutthulhu.test.halve" (6.11c3): a Filter in "Cutthulhu Tests/Colour" asking for OCIO colour management. It
//   prefers its Source in "lin_rec709_srgb", then the basic scene linear, says its output is in the Source's colour
//   space ("OfxColourspace_Source"), and halves the colour of what it is given. Its description is the host's colour
//   management style.
// Plus one plug-in of another API, which the host skips.
#include <ofxColour.h>
#include <ofxCore.h>
#include <ofxDrawSuite.h>
#include <ofxInteract.h>
#include <ofxImageEffect.h>
#include <ofxMessage.h>
#include <ofxMultiThread.h>
#include <ofxParam.h>
#include <ofxProperty.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

const OfxHost* gHost = nullptr;
const OfxPropertySuiteV1* gProps = nullptr;
const OfxImageEffectSuiteV1* gEffects = nullptr;
const OfxParameterSuiteV1* gParams = nullptr;
const OfxMessageSuiteV1* gMessages = nullptr;
const OfxMultiThreadSuiteV1* gThreads = nullptr;
const OfxDrawSuiteV1* gDraw = nullptr;
const OfxInteractSuiteV1* gInteract = nullptr;

std::atomic<int> gThreadCalls{0};
std::atomic<int> gLiveInstances{0};
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
    gDraw = static_cast<const OfxDrawSuiteV1*>(gHost->fetchSuite(gHost->host, kOfxDrawSuite, 1));
    gInteract = static_cast<const OfxInteractSuiteV1*>(gHost->fetchSuite(gHost->host, kOfxInteractSuite, 1));
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
    gParams->paramDefine(params, kOfxParamTypeRGB, "fill", &p);
    gProps->propSetString(p, kOfxPropLabel, 0, "Fill");
    const double fill[] = {0.25, 0.5, 0.75};
    gProps->propSetDoubleN(p, kOfxParamPropDefault, 3, fill);
    return kOfxStatOK;
}

OfxStatus brokenDescribe(OfxImageEffectHandle) {
    gMessages->message(nullptr, kOfxMessageError, nullptr, "no licence found");
    return kOfxStatFailed;
}

OfxStatus compositeOverlay(const char* action, const void* handle, OfxPropertySetHandle in, OfxPropertySetHandle out);

OfxStatus compositeDescribe(OfxImageEffectHandle e) {
    OfxPropertySetHandle p = effectProps(e);
    gProps->propSetString(p, kOfxPropLabel, 0, "Test Composite");
    gProps->propSetString(p, kOfxImageEffectPluginPropGrouping, 0, "Cutthulhu Mixers");
    gProps->propSetString(p, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextGeneral);
    gProps->propSetPointer(p, kOfxImageEffectPluginPropOverlayInteractV2, 0, reinterpret_cast<void*>(&compositeOverlay));
    return kOfxStatOK;
}

OfxStatus compositeDescribeInContext(OfxImageEffectHandle e) {
    defineClip(e, kOfxImageEffectSimpleSourceClipName);
    OfxPropertySetHandle c = nullptr;
    gEffects->clipDefine(e, "Over", &c);
    gProps->propSetInt(c, kOfxImageClipPropOptional, 0, 1);
    gProps->propSetString(c, kOfxPropLabel, 0, "Over Image");
    gEffects->clipDefine(e, "Matte", &c);
    gProps->propSetInt(c, kOfxImageClipPropOptional, 0, 1);
    gProps->propSetInt(c, kOfxImageClipPropIsMask, 0, 1);
    defineClip(e, kOfxImageEffectOutputClipName);
    OfxParamSetHandle params = nullptr;
    gEffects->getParamSet(e, &params);
    OfxPropertySetHandle p = nullptr;
    gParams->paramDefine(params, kOfxParamTypeDouble, "spin", &p);
    gProps->propSetString(p, kOfxPropLabel, 0, "Spin");
    gProps->propSetString(p, kOfxParamPropDoubleType, 0, kOfxParamDoubleTypeAngle);
    gParams->paramDefine(params, kOfxParamTypePushButton, "square", &p);
    gProps->propSetString(p, kOfxPropLabel, 0, "Square");
    gParams->paramDefine(params, kOfxParamTypeDouble2D, "centre", &p);
    gProps->propSetString(p, kOfxPropLabel, 0, "Centre");
    gProps->propSetString(p, kOfxParamPropDoubleType, 0, kOfxParamDoubleTypeXYAbsolute);
    const double centre[] = {960.0, 540.0};
    gProps->propSetDoubleN(p, kOfxParamPropDefault, 2, centre);
    const double lo[] = {0.0, 0.0}, hi[] = {1920.0, 1080.0};
    gProps->propSetDoubleN(p, kOfxParamPropDisplayMin, 2, lo);
    gProps->propSetDoubleN(p, kOfxParamPropDisplayMax, 2, hi);
    return kOfxStatOK;
}

OfxStatus paintDescribe(OfxImageEffectHandle e) {
    OfxPropertySetHandle p = effectProps(e);
    gProps->propSetString(p, kOfxPropLabel, 0, "Test Paint");
    gProps->propSetString(p, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextPaint);
    return kOfxStatOK;
}

// ---------------------------------------------------------------- rendering

// ---------------------------------------------------------------- halve (6.11c3)

OfxStatus halveDescribe(OfxImageEffectHandle e) {
    OfxPropertySetHandle p = effectProps(e);
    gProps->propSetString(p, kOfxPropLabel, 0, "Test Halve");
    gProps->propSetString(p, kOfxImageEffectPluginPropGrouping, 0, "Cutthulhu Tests/Colour");
    gProps->propSetString(p, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    gProps->propSetString(p, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);
    gProps->propSetString(p, kOfxImageEffectPropColourManagementStyle, 0, kOfxImageEffectColourManagementOCIO);
    gProps->propSetString(p, kOfxImageEffectPropColourManagementAvailableConfigs, 0, kOfxConfigIdentifier);
    char* style = nullptr;
    gProps->propGetString(gHost->host, kOfxImageEffectPropColourManagementStyle, 0, &style);
    gProps->propSetString(p, kOfxPropPluginDescription, 0, style ? style : "none");
    return kOfxStatOK;
}

OfxStatus halveDescribeInContext(OfxImageEffectHandle e) {
    defineClip(e, kOfxImageEffectSimpleSourceClipName);
    defineClip(e, kOfxImageEffectOutputClipName);
    return kOfxStatOK;
}

OfxStatus halveClipPreferences(OfxPropertySetHandle out) {
    const char* const prefs[] = {kOfxColourspaceLinRec709Srgb, kOfxColourspaceOfxSceneLinear};
    return gProps->propSetStringN(out, "OfxImageClipPropPreferredColourspaces_Source", 2, prefs);
}

OfxStatus halveOutputColourspace(OfxPropertySetHandle out) {
    return gProps->propSetString(out, kOfxImageClipPropColourspace, 0, "OfxColourspace_Source");
}

// An image the host gave: its pixels, bounds and row bytes.
struct Pixels {
    OfxPropertySetHandle props = nullptr;
    char* data = nullptr;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    int rowBytes = 0;
    float* at(int x, int y) const { return reinterpret_cast<float*>(data + static_cast<long>(y - y1) * rowBytes) + (x - x1) * 4; }
};

bool fetch(OfxImageEffectHandle e, const char* clipName, OfxTime time, Pixels& out) {
    OfxImageClipHandle clip = nullptr;
    if (gEffects->clipGetHandle(e, clipName, &clip, nullptr) != kOfxStatOK) return false;
    if (gEffects->clipGetImage(clip, time, nullptr, &out.props) != kOfxStatOK) return false;
    void* data = nullptr;
    gProps->propGetPointer(out.props, kOfxImagePropData, 0, &data);
    out.data = static_cast<char*>(data);
    int b[4] = {};
    gProps->propGetIntN(out.props, kOfxImagePropBounds, 4, b);
    out.x1 = b[0];
    out.y1 = b[1];
    out.x2 = b[2];
    out.y2 = b[3];
    gProps->propGetInt(out.props, kOfxImagePropRowBytes, 0, &out.rowBytes);
    return out.data != nullptr;
}

OfxParamHandle paramOf(OfxImageEffectHandle e, const char* name) {
    OfxParamSetHandle params = nullptr;
    gEffects->getParamSet(e, &params);
    OfxParamHandle p = nullptr;
    gParams->paramGetHandle(params, name, &p, nullptr);
    return p;
}

double timeOf(OfxPropertySetHandle in) {
    double t = 0;
    gProps->propGetDouble(in, kOfxPropTime, 0, &t);
    return t;
}

OfxStatus tintIsIdentity(OfxImageEffectHandle e, OfxPropertySetHandle in, OfxPropertySetHandle out) {
    const double t = timeOf(in);
    double amount = 1;
    int invert = 1;
    gParams->paramGetValueAtTime(paramOf(e, "amount"), t, &amount);
    gParams->paramGetValueAtTime(paramOf(e, "invert"), t, &invert);
    if (amount != 0.0 || invert) return kOfxStatReplyDefault;
    gProps->propSetString(out, kOfxPropName, 0, kOfxImageEffectSimpleSourceClipName);
    return kOfxStatOK;
}

OfxStatus tintRender(OfxImageEffectHandle e, OfxPropertySetHandle in) {
    const double t = timeOf(in);
    Pixels src, dst;
    if (!fetch(e, kOfxImageEffectSimpleSourceClipName, t, src) || !fetch(e, kOfxImageEffectOutputClipName, t, dst)) return kOfxStatFailed;
    double amount = 0;
    int channel = 0, invert = 0;
    gParams->paramGetValueAtTime(paramOf(e, "amount"), t, &amount);
    gParams->paramGetValueAtTime(paramOf(e, "channel"), t, &channel);
    gParams->paramGetValueAtTime(paramOf(e, "invert"), t, &invert);
    for (int y = dst.y1; y < dst.y2; ++y) {
        for (int x = dst.x1; x < dst.x2; ++x) {
            const float* s = src.at(x, y);
            float* d = dst.at(x, y);
            std::copy(s, s + 4, d);
            d[channel] = static_cast<float>(amount) * s[3];
            if (invert) {
                for (int c = 0; c < 3; ++c) d[c] = s[3] - d[c];
            }
        }
    }
    gEffects->clipReleaseImage(src.props);
    gEffects->clipReleaseImage(dst.props);
    return kOfxStatOK;
}

OfxStatus halveRender(OfxImageEffectHandle e, OfxPropertySetHandle in) {
    const double t = timeOf(in);
    Pixels src, dst;
    if (!fetch(e, kOfxImageEffectSimpleSourceClipName, t, src) || !fetch(e, kOfxImageEffectOutputClipName, t, dst)) return kOfxStatFailed;
    for (int y = dst.y1; y < dst.y2; ++y) {
        for (int x = dst.x1; x < dst.x2; ++x) {
            const float* s = src.at(x, y);
            float* d = dst.at(x, y);
            for (int c = 0; c < 3; ++c) d[c] = s[c] * 0.5f;
            d[3] = s[3];
        }
    }
    gEffects->clipReleaseImage(src.props);
    gEffects->clipReleaseImage(dst.props);
    return kOfxStatOK;
}

OfxStatus noiseRender(OfxImageEffectHandle e, OfxPropertySetHandle in) {
    const double t = timeOf(in);
    Pixels dst;
    if (!fetch(e, kOfxImageEffectOutputClipName, t, dst)) return kOfxStatFailed;
    double r = 0, g = 0, b = 0;
    gParams->paramGetValueAtTime(paramOf(e, "fill"), t, &r, &g, &b);
    for (int y = dst.y1; y < dst.y2; ++y) {
        for (int x = dst.x1; x < dst.x2; ++x) {
            float* d = dst.at(x, y);
            const bool bottom = y == dst.y1;
            d[0] = bottom ? 1.0f : static_cast<float>(r);
            d[1] = bottom ? 1.0f : static_cast<float>(g);
            d[2] = bottom ? 1.0f : static_cast<float>(b);
            d[3] = 1.0f;
        }
    }
    gEffects->clipReleaseImage(dst.props);
    return kOfxStatOK;
}

OfxStatus wipeRender(OfxImageEffectHandle e, OfxPropertySetHandle in) {
    const double t = timeOf(in);
    Pixels from, to, dst;
    if (!fetch(e, kOfxImageEffectTransitionSourceFromClipName, t, from) || !fetch(e, kOfxImageEffectTransitionSourceToClipName, t, to) ||
        !fetch(e, kOfxImageEffectOutputClipName, t, dst)) {
        return kOfxStatFailed;
    }
    double progress = 0;
    gParams->paramGetValue(paramOf(e, kOfxImageEffectTransitionParamName), &progress);
    const int edge = dst.x1 + static_cast<int>(progress * (dst.x2 - dst.x1));
    for (int y = dst.y1; y < dst.y2; ++y) {
        for (int x = dst.x1; x < dst.x2; ++x) {
            const float* s = x < edge ? to.at(x, y) : from.at(x, y);
            std::copy(s, s + 4, dst.at(x, y));
        }
    }
    gEffects->clipReleaseImage(from.props);
    gEffects->clipReleaseImage(to.props);
    gEffects->clipReleaseImage(dst.props);
    return kOfxStatOK;
}

// Whether a clip is connected (kOfxImageClipPropConnected).
bool connected(OfxImageEffectHandle e, const char* name) {
    OfxImageClipHandle clip = nullptr;
    OfxPropertySetHandle props = nullptr;
    if (gEffects->clipGetHandle(e, name, &clip, &props) != kOfxStatOK) return false;
    int on = 0;
    gProps->propGetInt(props, kOfxImageClipPropConnected, 0, &on);
    return on != 0;
}

OfxStatus compositeRender(OfxImageEffectHandle e, OfxPropertySetHandle in) {
    const double t = timeOf(in);
    Pixels src, over, matte, dst;
    if (!fetch(e, kOfxImageEffectSimpleSourceClipName, t, src) || !fetch(e, kOfxImageEffectOutputClipName, t, dst)) return kOfxStatFailed;
    const bool hasOver = connected(e, "Over") && fetch(e, "Over", t, over);
    const bool hasMatte = connected(e, "Matte") && fetch(e, "Matte", t, matte);
    double spin = 0;
    gParams->paramGetValueAtTime(paramOf(e, "spin"), t, &spin);
    for (int y = dst.y1; y < dst.y2; ++y) {
        for (int x = dst.x1; x < dst.x2; ++x) {
            const float* s = src.at(x, y);
            float* d = dst.at(x, y);
            for (int c = 0; c < 4; ++c) {
                if (hasOver && hasMatte) {
                    const float m = matte.at(x, y)[0];
                    d[c] = s[c] * (1.0f - m) + over.at(x, y)[c] * m;
                } else if (hasOver) {
                    d[c] = over.at(x, y)[c] + s[c] * (1.0f - over.at(x, y)[3]);
                } else {
                    d[c] = s[c];
                }
            }
            if (spin >= 180.0) {
                for (int c = 0; c < 3; ++c) d[c] = d[3] - d[c];
            }
        }
    }
    // The Centre, in canonical coordinates, marked at the render scale.
    double cx = 0, cy = 0, scale[2] = {1, 1};
    gParams->paramGetValueAtTime(paramOf(e, "centre"), t, &cx, &cy);
    gProps->propGetDoubleN(in, kOfxImageEffectPropRenderScale, 2, scale);
    const int px = static_cast<int>(cx * scale[0]), py = static_cast<int>(cy * scale[1]);
    if (px >= dst.x1 && px < dst.x2 && py >= dst.y1 && py < dst.y2) {
        float* d = dst.at(px, py);
        d[0] = d[1] = d[2] = d[3] = 1.0f;
    }
    for (Pixels* p : {&src, &dst}) gEffects->clipReleaseImage(p->props);
    if (hasOver) gEffects->clipReleaseImage(over.props);
    if (hasMatte) gEffects->clipReleaseImage(matte.props);
    return kOfxStatOK;
}

// The Square button sets Spin to 90 (as a plug-in's own dialog or button would set values).
OfxStatus compositeInstanceChanged(OfxImageEffectHandle e, OfxPropertySetHandle in) {
    char* name = nullptr;
    gProps->propGetString(in, kOfxPropName, 0, &name);
    if (name && std::strcmp(name, "square") == 0) gParams->paramSetValue(paramOf(e, "spin"), 90.0);
    return kOfxStatOK;
}

// The overlay: a square round the Centre, dragged by a press near it.
bool gDragging = false;

OfxStatus compositeOverlay(const char* action, const void* handle, OfxPropertySetHandle in, OfxPropertySetHandle) {
    if (std::strcmp(action, kOfxActionDescribe) == 0 || std::strcmp(action, kOfxActionCreateInstance) == 0 ||
        std::strcmp(action, kOfxActionDestroyInstance) == 0) {
        return kOfxStatOK;
    }
    if (!in) return kOfxStatReplyDefault;
    void* instance = nullptr;
    gProps->propGetPointer(in, kOfxPropEffectInstance, 0, &instance);
    auto* e = static_cast<OfxImageEffectHandle>(instance);
    const double t = timeOf(in);
    double cx = 0, cy = 0;
    gParams->paramGetValueAtTime(paramOf(e, "centre"), t, &cx, &cy);
    if (std::strcmp(action, kOfxInteractActionDraw) == 0) {
        void* context = nullptr;
        gProps->propGetPointer(in, kOfxInteractPropDrawContext, 0, &context);
        auto* dc = static_cast<OfxDrawContextHandle>(context);
        const OfxRGBAColourF yellow{1.0f, 1.0f, 0.0f, 1.0f};
        gDraw->setColour(dc, &yellow);
        gDraw->setLineWidth(dc, 2.0f);
        const OfxPointD square[] = {{cx - 20, cy - 20}, {cx + 20, cy - 20}, {cx + 20, cy + 20}, {cx - 20, cy + 20}};
        gDraw->draw(dc, kOfxDrawPrimitiveLineLoop, square, 4);
        const OfxPointD at{cx, cy - 30};
        gDraw->drawText(dc, "centre", &at, 32);
        return kOfxStatOK;
    }
    double pen[2] = {0, 0};
    gProps->propGetDoubleN(in, kOfxInteractPropPenPosition, 2, pen);
    if (std::strcmp(action, kOfxInteractActionPenDown) == 0) {
        gDragging = std::abs(pen[0] - cx) <= 30 && std::abs(pen[1] - cy) <= 30;
        return gDragging ? kOfxStatOK : kOfxStatReplyDefault;
    }
    if (!gDragging) return kOfxStatReplyDefault;
    gParams->paramSetValue(paramOf(e, "centre"), pen[0], pen[1]);
    if (std::strcmp(action, kOfxInteractActionPenUp) == 0) gDragging = false;
    gInteract->interactRedraw(static_cast<OfxInteractHandle>(const_cast<void*>(handle)));
    return kOfxStatOK;
}

template <int Kind>
OfxStatus mainEntry(const char* action, const void* handle, OfxPropertySetHandle in, OfxPropertySetHandle out) {
    auto* e = static_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
    if (std::strcmp(action, kOfxActionLoad) == 0) return load();
    if (std::strcmp(action, kOfxActionDescribe) == 0) {
        switch (Kind) {
        case 0: return tintDescribe(e);
        case 1: return wipeDescribe(e);
        case 2: return noiseDescribe(e);
        case 3: return brokenDescribe(e);
        case 5: return compositeDescribe(e);
        case 6: return halveDescribe(e);
        default: return paintDescribe(e);
        }
    }
    if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
        switch (Kind) {
        case 0: return tintDescribeInContext(e);
        case 1: return wipeDescribeInContext(e);
        case 2: return noiseDescribeInContext(e, in);
        case 5: return compositeDescribeInContext(e);
        case 6: return halveDescribeInContext(e);
        default: return kOfxStatOK;
        }
    }
    if (std::strcmp(action, kOfxActionCreateInstance) == 0) {
        ++gLiveInstances;
        return kOfxStatOK;
    }
    if (std::strcmp(action, kOfxActionDestroyInstance) == 0) {
        --gLiveInstances;
        return kOfxStatOK;
    }
    if (std::strcmp(action, kOfxImageEffectActionIsIdentity) == 0 && Kind == 0) {
        return tintIsIdentity(e, in, out);
    }
    if (std::strcmp(action, kOfxImageEffectActionRender) == 0) {
        switch (Kind) {
        case 0: return tintRender(e, in);
        case 1: return wipeRender(e, in);
        case 2: return noiseRender(e, in);
        case 5: return compositeRender(e, in);
        case 6: return halveRender(e, in);
        default: return kOfxStatFailed;
        }
    }
    if (std::strcmp(action, kOfxActionInstanceChanged) == 0 && Kind == 5) return compositeInstanceChanged(e, in);
    if (std::strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0 && Kind == 6) return halveClipPreferences(out);
    if (std::strcmp(action, kOfxImageEffectActionGetOutputColourspace) == 0 && Kind == 6) return halveOutputColourspace(out);
    return kOfxStatReplyDefault;
}

OfxPlugin gPlugins[] = {
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.tint", 1, 2, setHost, mainEntry<0>},
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.wipe", 1, 0, setHost, mainEntry<1>},
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.noise", 1, 0, setHost, mainEntry<2>},
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.broken", 1, 0, setHost, mainEntry<3>},
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.paint", 1, 0, setHost, mainEntry<4>},
    {"SomeOtherApi", 1, "org.cutthulhu.test.other", 1, 0, setHost, mainEntry<4>},
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.composite", 1, 0, setHost, mainEntry<5>},
    {kOfxImageEffectPluginApi, kOfxImageEffectPluginApiVersion, "org.cutthulhu.test.halve", 1, 0, setHost, mainEntry<6>},
};

} // namespace

OfxExport int OfxGetNumberOfPlugins(void) {
    return static_cast<int>(sizeof gPlugins / sizeof gPlugins[0]);
}

OfxExport OfxPlugin* OfxGetPlugin(int nth) {
    return nth >= 0 && nth < OfxGetNumberOfPlugins() ? &gPlugins[nth] : nullptr;
}

// For the test: how many instances the host has made and not destroyed.
#ifdef _WIN32
#define CUTTHULHU_TEST_EXPORT extern "C" __declspec(dllexport)
#else
#define CUTTHULHU_TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif
CUTTHULHU_TEST_EXPORT int CutthulhuTestLiveInstances(void) {
    return gLiveInstances.load();
}
