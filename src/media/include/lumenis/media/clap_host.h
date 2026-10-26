#pragma once

#include "lumenis/ext/plugin_host.h"
#include "lumenis/media/audio_fx.h"

#include <QList>
#include <QString>
#include <QStringList>

// Hosting CLAP audio plug-ins (docs/spec/clap-host.md; CLAP 1.2.9, MIT, third_party/clap). The CLAP
// files on the system are found, their effect plug-ins read, and the ones that fit registered as
// audio effects (lumenis/media/audio_fx.h): they then appear with the built-in effects as Audio
// Track Effects (Editing Guide p.790-795), under the id "clap:<plug-in id>".
//
// What fits: a plug-in with the "audio-effect" feature whose main input and output ports have one or
// two channels. Our tracks are mono: a stereo plug-in gets the track on both inputs, and its two
// outputs are averaged (ours).
namespace lumenis::media {

// One plug-in a CLAP file offers.
struct ClapPluginInfo {
    QString file;       // the .clap file or bundle
    QString id;         // the plug-in's own id ("org.example.reverb")
    QString name;
    QString vendor;
    QString version;
    QStringList features;
    int inputChannels = 0;  // the main input port's, 0 if none
    int outputChannels = 0; // the main output port's
    bool latency = false;   // reports latency (not compensated yet)
    QList<AudioEffectParam> params; // ids are the CLAP param ids as text
    // Whether it can be an Audio Track Effect, and if not why.
    bool usable() const;
    QString whyNot() const;
    // The Effect Palette category its features give: "Dynamics", "EQ", "Delay", "Reverb",
    // "Modulation", "Harmonic", else "CLAP".
    QString category() const;
};

// Where CLAP plug-ins are installed, as the CLAP specification lists them (entry.h), then the folders
// in the environment variable CLAP_PATH. Only folders that exist.
QStringList clapSearchPaths();
// The .clap files and bundles in `folders`, searched to any depth, sorted.
QStringList findClapFiles(const QStringList& folders);
// Loads each file and reads what its plug-ins are; each plug-in is made once to read its ports and
// parameters. A file that cannot be loaded, or a plug-in that cannot be made, is named in `errors`.
QList<ClapPluginInfo> scanClapPlugins(const QStringList& files, QStringList* errors = nullptr);
// Registers the usable plug-ins as audio effects (registerAudioEffects). Returns how many.
int registerClapPlugins(const QList<ClapPluginInfo>& plugins);
// The plug-ins registered so far, usable or not, as the last scan found them.
const QList<ClapPluginInfo>& clapPlugins();

// The CLAP host as an extension provider (lumenis/ext/plugin_host.h): its search paths and scan.
class ClapHostProvider : public ext::PluginHostProvider {
public:
    ext::ProviderInfo info() const override;
    ext::PluginStandard standard() const override { return ext::PluginStandard::CLAP; }
    std::vector<std::filesystem::path> defaultSearchPaths() const override;
    std::vector<ext::PluginDescriptor> scan(const std::vector<std::filesystem::path>& paths) override;
};

} // namespace lumenis::media
