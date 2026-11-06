#pragma once

// Hosting OpenFX image effect plug-ins (ROADMAP 6.11; docs/spec/ofx-host.md; OpenFX 1.5.1, BSD 3-Clause,
// third_party/openfx) in place of Avid's AVX plug-ins (FX Guide ch.6, p.157-164), whose SDK is Avid's. The OFX bundles
// on the system are found, each plug-in is loaded and described, and the usable ones become effects of the Effect
// Palette: "Third-party plug-in effects appear in the Effect Palette in their own category (usually the name of the
// plug-in vendor)" (p.157).
//
// 6.11a: the host's core. Its property, parameter, image effect, memory, multithread and message suites answer the
// describe actions (kOfxActionLoad, kOfxActionDescribe, kOfxImageEffectActionDescribeInContext); rendering comes in
// 6.11b, so the instance-only calls (parameter values, clip images) answer kOfxStatErrUnsupported for now.

#include "lumenis/ext/plugin_host.h"

#include <QList>
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
    QList<OfxParamInfo> params;
    QString error;         // why it could not be described; empty when it could
    // Whether it can be an effect here (a Filter, Generator, Transition or General context, described), and if not why.
    bool usable() const;
    QString whyNot() const;
    // The Effect Palette category: the first part of its grouping, the vendor (p.157), else "OFX Plug-Ins" (ours).
    QString category() const;
    // Segment effects: Filter, Generator and General plug-ins; transition effects: Transition ones.
    bool segment() const;
    bool transition() const;
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

// The OFX host as an extension provider (lumenis/ext/plugin_host.h): its search paths and scan.
class OfxHostProvider : public ext::PluginHostProvider {
public:
    ext::ProviderInfo info() const override;
    ext::PluginStandard standard() const override { return ext::PluginStandard::OFX; }
    std::vector<std::filesystem::path> defaultSearchPaths() const override;
    std::vector<ext::PluginDescriptor> scan(const std::vector<std::filesystem::path>& paths) override;
};

} // namespace lumenis::media
