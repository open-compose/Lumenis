#pragma once

// Hosting OpenFX image effect plug-ins (ROADMAP 6.11; docs/spec/ofx-host.md; OpenFX 1.5.1, BSD 3-Clause,
// third_party/openfx) in place of Avid's AVX plug-ins (FX Guide ch.6, p.157-164), whose SDK is Avid's. The OFX bundles
// on the system are found, each plug-in is loaded and described, and the usable ones become effects of the Effect
// Palette: "Third-party plug-in effects appear in the Effect Palette in their own category (usually the name of the
// plug-in vendor)" (p.157).
//
// 6.11a: the host's core. Its property, parameter, image effect, memory, multithread and message suites answer the
// describe actions (kOfxActionLoad, kOfxActionDescribe, kOfxImageEffectActionDescribeInContext).
//
// 6.11b1: rendering. An effect of a registered plug-in (ofxPluginOf) is rendered by an instance of the plug-in in the
// context it was described in: its parameters' values come from the effect's own (keyframed like ours), its input
// clips are the segment's picture (Filter, General) or the transition's two (Transition), and the Output clip comes
// back as a Picture. The plug-in's parameters are the Effect Editor's controls (ofxEditorParameters).

#include "lumenis/ext/plugin_host.h"
#include "lumenis/media/compositor.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"

#include <QColor>
#include <QJsonValue>
#include <QPointF>
#include <QSize>

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>

namespace lumenis::media {

// One parameter a plug-in described (ofxParam.h): its type (kOfxParamType..., e.g. "OfxParamTypeDouble"), name (the
// plug-in's identifier) and label, and what the Effect Editor needs to show it.
struct OfxParamInfo {
    QString type;
    QString name;
    QString label;
    QString hint;
    QString parent;      // the group it is in, if any
    QVariantList defaults; // one value per dimension (a colour has three or four)
    double min = 0.0, max = 0.0;
    double displayMin = 0.0, displayMax = 0.0;
    bool hasRange = false;
    QStringList options; // a choice's
    bool animates = true; // kOfxParamPropAnimates
    bool secret = false;  // kOfxParamPropSecret: not shown
    QString doubleType;   // kOfxParamPropDoubleType ("OfxParamDoubleTypeAngle" for an angle, 6.11c1)
};

// One input clip a plug-in described (6.11c1): its name ("Source", "SourceFrom", a General context's own), label,
// and whether it may be left unconnected (kOfxImageClipPropOptional) or is a mask (kOfxImageClipPropIsMask).
struct OfxClipInfo {
    QString name;
    QString label;
    bool optional = false;
    bool mask = false;
};

// One plug-in an OFX bundle offers.
struct OfxPluginInfo {
    QString file;     // the bundle's binary
    QString id;       // the plug-in's identifier ("com.example.blur")
    int versionMajor = 0, versionMinor = 0;
    QString label;
    QString grouping; // kOfxImageEffectPluginPropGrouping, "Vendor/Sub" or empty
    QString description;
    QStringList contexts;  // kOfxImageEffectContext... it supports
    QString describedContext; // the context its clips and parameters below were described in
    QStringList clips;     // their names, Output included
    QList<OfxClipInfo> inputs; // the input clips (all but Output), in the order defined
    QList<OfxParamInfo> params;
    QString error;         // why it could not be described; empty when it could
    // Colour management (6.11c3): the style it asked for (kOfxImageEffectPropColourManagementStyle, empty for none)
    // and the native configs it knows (kOfxImageEffectPropColourManagementAvailableConfigs).
    QString colourStyle;
    QStringList colourConfigs;
    // Whether it can be an effect here (a Filter, Generator, Transition or General context, described), and if not why.
    bool usable() const;
    QString whyNot() const;
    // The Effect Palette category: the first part of its grouping, the vendor (p.157), else "OFX Plug-Ins" (ours).
    QString category() const;
    // Segment effects: Filter, Generator and General plug-ins; transition effects: Transition ones.
    bool segment() const;
    bool transition() const;
    // Several inputs (6.11c1; "Some plug-in effects take an arbitrary number of tracks as input", p.161): how many
    // inputs must be connected and how many can be; a plug-in with optional inputs asks for its Number of Tracks when
    // applied (the OFX Optional Inputs dialog).
    int requiredInputs() const;
    int maxInputs() const { return static_cast<int>(inputs.size()); }
    bool variableInputs() const { return maxInputs() > requiredInputs(); }
    // The inputs connected when `count` are chosen (clamped to the range above): every required one, then optional
    // ones in their order, as they come in the inputs' order. The first is the segment's own picture (the nest's V1),
    // the next the nest's V2 and so on.
    QList<OfxClipInfo> connectedInputs(int count) const;
};

// Where OFX plug-ins are installed, as the OpenFX standard lists them ("C:\Program Files\Common Files\OFX\Plugins",
// "/Library/OFX/Plugins", "/usr/OFX/Plugins"), then the folders in OFX_PLUGIN_PATH and in LUMENIS_OFX_PATH (ours, for
// tests). Only folders that exist.
QStringList ofxSearchPaths();
// The binaries of the ".ofx.bundle" folders in `folders`, searched to any depth, sorted: each bundle's
// Contents/<architecture>/<name>.ofx ("Win64", "MacOS", "Linux-x86-64" or "Linux-aarch64").
QStringList findOfxBundles(const QStringList& folders);
// The architecture folder of a bundle built for this platform.
QString ofxArchitecture();
// Loads each binary and describes its image effect plug-ins. A binary that cannot be loaded, or has no OFX entry
// points, is named in `errors`; a plug-in whose describe action fails is listed with its error ("The Console window
// might display messages about why a plug-in did not load correctly", p.163). Binaries stay loaded.
QList<OfxPluginInfo> scanOfxPlugins(const QStringList& binaries, QStringList* errors = nullptr);
// Makes the usable plug-ins the Effect Palette's (EffectCatalog reads them once, so before it is first used). Returns
// how many.
int registerOfxPlugins(const QList<OfxPluginInfo>& plugins);
// The plug-ins the last scan found, usable or not.
const QList<OfxPluginInfo>& ofxPlugins();
// The registered usable plug-in with that Effect Palette category and label; null if none.
const OfxPluginInfo* ofxPluginFor(const QString& category, const QString& label);
// Messages plug-ins send through the message suite (kOfxMessageError ...), as "<plug-in>: <text>". The application
// passes them to the Console; without a handler they go to qWarning.
void setOfxMessageHandler(std::function<void(const QString&)> handler);

// ---------------------------------------------------------------- effects and their parameters (6.11b1)

// The registered usable plug-in an effect is of: the one its plug-in tag names (markOfxEffect), else the one with its
// Effect Palette category and name (the label, or "<label> (<identifier>)" for a second plug-in of that label); null
// for other effects, and for a tagged effect whose plug-in is missing.
const OfxPluginInfo* ofxPluginOf(const model::Effect& effect);
inline bool isOfxEffect(const model::Effect& effect) {
    return ofxPluginOf(effect) != nullptr;
}

// A plug-in effect's tag (6.11b2): the parameter category "OFX Plug-In" with the plug-in's "id", "label" and "version",
// written when the effect is applied, so that a sequence still knows it is a plug-in effect, and which, where the
// plug-in is not installed ("you must install matching AVX plug-ins to see the effects", p.157). Not shown in the
// Effect Editor (it is not one of the effect's parameter categories).
inline const QString kOfxPluginTag = QStringLiteral("OFX Plug-In");
void markOfxEffect(model::Effect& effect, const OfxPluginInfo& plugin);
// The tagged plug-in's identifier; empty for an effect without the tag.
QString ofxTaggedPlugin(const model::Effect& effect);
// A tagged effect whose plug-in is not registered: "If the effect icon in the Timeline is blank, Media Composer could
// not find the plug-in" (p.163). Its pictures pass through unchanged.
bool isMissingOfxEffect(const model::Effect& effect);

// The Effect Editor's parameter category of a plug-in's parameters: "<label> Plug-In Parameters" (ours).
QString ofxParameterCategory(const OfxPluginInfo& plugin);

// One control the Effect Editor shows for a plug-in parameter (its EffectParameter): the stored key in the effect's
// parameters under ofxParameterCategory, the text beside it, and the control. Ours, as the standard leaves the host's
// controls to it:
// - Double and Integer: a slider over the display range (else the range, else 0 to 100), Double with 2 decimals (3
//   when the range is at most 2 wide); 2D and 3D ones a slider each, "<name>.x", ".y", ".z", texts "<label> X" ...;
// - Boolean: a checkbox; Choice: a menu of its options, stored as the option's text;
// - RGB and RGBA: a colour ("#rrggbb"; RGBA's alpha stays its default);
// - Group: the subcategory of the parameters in it (its label).
// Secret parameters, the Transition context's own "Transition" (the transition's progress), and the types we do not
// show yet (String, Custom, Page, Parametric, Bytes, StrChoice) have no control; they keep their defaults.
// 6.11c1: a Double of the angle type is an "angle" control ("The Angle control lets you alter an effect's angle or
// direction value by dragging within a graphic representation of a circle. Values can be positive or negative and are
// not limited to a single 360º rotation", p.162), in degrees; a Push Button is a "button" (ofxPushButton).
struct OfxEditorParameter {
    QString key;
    QString text;
    QString kind; // "slider", "checkbox", "menu", "color", "angle", "button"
    QString subcategory;
    double min = 0.0, max = 100.0;
    int decimals = 0;
    QJsonValue defaultValue;
    QStringList options;
    bool keyframeable = true; // kOfxParamPropAnimates (sliders and colours only)
};
QList<OfxEditorParameter> ofxEditorParameters(const OfxPluginInfo& plugin);
// A plug-in effect's parameter default by its stored key; null when the key is not one of its controls.
QJsonValue ofxParameterDefault(const model::Effect& effect, const QString& category, const QString& key);

// How many inputs the effect's plug-in was given (6.11c1): the number chosen in the OFX Optional Inputs dialog, kept
// in its tag as "inputs", else the plug-in's required inputs. setOfxInputCount keeps one.
int ofxInputCount(const model::Effect& effect);
void setOfxInputCount(model::Effect& effect, int count);

// A segment effect's picture: the plug-in rendered at `time` of the effect (model::EffectSpan's time) over `source`
// (unused by a Generator). With several inputs (ofxInputCount), `source` is the first and `more` the others, in the
// order of OfxPluginInfo::connectedInputs (the nest's V2, V3 ...); an input without a picture there is connected and
// empty. A plug-in that fails to render leaves `source` as it was and says why in its message handler's output once
// ("Media Composer Cannot Render the Plug-In ... check the Console window", p.163).
Picture ofxSegmentPicture(const model::Effect& effect, const Picture& source, double time, const model::EffectSpan& span,
                          const QList<Picture>& more = {});
// A transition effect's picture between `from` (outgoing) and `to` (incoming), `progress` 0 to 1 as the plug-in's
// Transition parameter.
Picture ofxTransitionPicture(const model::Effect& effect, const Picture& from, const Picture& to, double progress, double time,
                             const model::EffectSpan& span);
// A missing plug-in's effect (isMissingOfxEffect) asked to render says "Can't find effect: <label> (<identifier>)"
// in the message handler's output, once per plug-in ("look for the message 'Can't find effect.' The message identifies
// the plug-in", p.163); the picture passes through.
// Presses a plug-in effect's push button `key` (6.11c1): the instance is told (kOfxActionInstanceChanged, the user's
// edit) at `time` of the effect, as plug-ins open their own dialogs or change values ("Custom dialog box only. You
// typically access this dialog box by clicking the Other Options button or another custom button in the Effect
// Editor", p.161). Returns the values the plug-in set meanwhile, by stored key under ofxParameterCategory, for the
// Effect Editor to keep; empty when it set none or the effect is not a plug-in's.
QJsonObject ofxPushButton(const model::Effect& effect, const QString& key, double time, const model::EffectSpan& span);

// ---------------------------------------------------------------- overlays (6.11c2)
// "Some plug-in effects let you adjust an image by using controls that appear as overlays, such as handles and wire
// frames, in the Effect Preview monitor. You access these overlays by clicking the Outline/Path button" (FX Guide
// p.162). A plug-in's overlay interact (kOfxImageEffectPluginPropOverlayInteractV2, OpenFX 1.5) draws through the
// Draw suite, which the host records as shapes; its pen actions get the Effect Preview's pointer. Ours: only V2
// interacts (V1 ones draw with OpenGL, which the Effect Preview does not give them).

// One shape an overlay drew: its kind (kOfxDrawPrimitive..., or Text), colour, line width (0 a single pixel) and
// dashes, and its points as fractions of the picture with (0, 0) its top left (the plug-in's canonical coordinates,
// y up, turned round). Text has one point, the text and its kOfxDrawTextAlignment flags.
struct OfxOverlayShape {
    enum class Kind { Lines, LineStrip, LineLoop, Rectangle, Polygon, Ellipse, Text };
    Kind kind = Kind::Lines;
    QColor colour = Qt::white;
    double width = 0.0;
    bool dashed = false;
    QList<QPointF> points;
    QString text;
    int alignment = 0;
    bool operator==(const OfxOverlayShape&) const = default;
};

// Whether the effect's plug-in has an overlay we can draw.
bool ofxHasOverlay(const model::Effect& effect);
// The overlay at `time` of the effect; `pixelScale` is canonical units per screen pixel (kOfxInteractPropPixelScale).
QList<OfxOverlayShape> ofxOverlayDraw(const model::Effect& effect, double time, const model::EffectSpan& span, double pixelScale);

// The pointer in the Effect Preview at `at` (a fraction of the picture, as OfxOverlayShape's): pressed, moved or
// released. `taken` when the plug-in used it (kOfxStatOK; then the Effect Preview leaves it alone); `values` the
// parameter values it set meanwhile, for the Effect Editor to keep as edits of its controls (as ofxPushButton's).
enum class OfxPen { Down, Motion, Up };
struct OfxPenResult {
    bool taken = false;
    QJsonObject values;
};
OfxPenResult ofxOverlayPen(const model::Effect& effect, OfxPen pen, const QPointF& at, double time, const model::EffectSpan& span,
                           double pixelScale);

// The project's size in canonical coordinates (kOfxImageEffectPropProjectSize; the application sets the project's
// format): pictures of other sizes render at the matching render scale, and overlays draw in it. Until set (or when
// empty), a picture's own size, and 1920 x 1080 for overlays.
void setOfxProjectSize(const QSize& size);
// The size set, or 1920 x 1080.
QSize ofxProjectSize();

// The frame rate plug-ins are told (kOfxImageEffectPropFrameRate); the application sets the project's. 30 until set.
void setOfxFrameRate(double rate);

// ---------------------------------------------------------------- colour management (6.11c3)
// The OFX 1.5 colour management (ofxColour.h; docs/spec/ofx-host.md "Colour management"). Plug-ins are told the
// colour space of each clip's images. With OpenColorIO (optional; ofxColourConversion()) the host converts input
// images into the colour space a plug-in prefers (kOfxImageClipPropPreferredColourspaces) and its output back into
// the project's; without it every image is in the project's colour space and says so.

// The project's colour space, one of colorSpacesFor()'s texts ("YCbCr 709" until set); the application sets it.
void setOfxColourSpace(const QString& projectColorSpace);
// The OFX native colour space (ofx-native-v1.5_aces-v1.3_ocio-v2.3.h) a project colour space's pictures are in
// (ours): Rec.1886 Rec.709 for 601 and 709, Rec.1886 Rec.2020, Rec.2100 PQ or HLG, P3-DCI, ACES2065-1.
QString ofxNativeColourspace(const QString& projectColorSpace);
// The style the host offers (kOfxImageEffectPropColourManagementStyle): OCIO when OpenColorIO has a config, else Core.
QString ofxHostColourStyle();
// The style used with `plugin`: the higher of the two styles both sides have (OCIO > Full > Core > Basic), or
// kOfxImageEffectColourManagementNone when the plug-in asks for none.
QString ofxColourStyle(const OfxPluginInfo& plugin);
// The version of OpenColorIO this build uses ("2.1.3"); empty in a build without it.
QString ofxOcioVersion();
// Whether images are converted: this build has OpenColorIO and it found a config (ofxOcioConfig()).
bool ofxColourConversion();
// OpenColorIO's config: a file or an ocio:// URI, empty for automatic (the OCIO environment variable's, else the
// library's built-in ACES studio config). LUMENIS_NO_OCIO=1 turns conversion off. Instances made before keep their
// properties until the next render.
void setOfxOcioConfig(const QString& config);
// The config in use, empty when there is none.
QString ofxOcioConfig();
// The colour space of each of `plugin`'s clips (Output included) at its last render, as the plug-in was told
// (kOfxImageClipPropColourspace); empty when it was rendered without colour management or not at all.
QMap<QString, QString> ofxClipColourspaces(const OfxPluginInfo& plugin);
// Destroys every plug-in instance (kOfxActionDestroyInstance); the next render makes them again. Returns how many.
int closeOfxInstances();

// The OFX host as an extension provider (lumenis/ext/plugin_host.h): its search paths and scan.
class OfxHostProvider : public ext::PluginHostProvider {
public:
    ext::ProviderInfo info() const override;
    ext::PluginStandard standard() const override { return ext::PluginStandard::OFX; }
    std::vector<std::filesystem::path> defaultSearchPaths() const override;
    std::vector<ext::PluginDescriptor> scan(const std::vector<std::filesystem::path>& paths) override;
};

} // namespace lumenis::media
