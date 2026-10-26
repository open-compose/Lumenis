#include "lumenis/media/clap_host.h"

#include <clap/clap.h>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLibrary>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

// The CLAP host (clap_host.h). A CLAP file is a shared library (a bundle on macOS) that exports
// `clap_entry`; its plug-in factory lists the plug-ins and makes them. Each Audio Track Effect insert
// of a CLAP plug-in is one plug-in instance, activated at 48 kHz on the thread that makes it and
// processing on the thread that plays (CLAP's main and audio threads). The host offers the plug-ins
// no extensions of its own; requests for a restart or a callback are not acted on.
namespace lumenis::media {

namespace {

constexpr double kRate = 48000;
constexpr uint32_t kMaxBlock = 1024;

// ---- the host every plug-in instance is given

const void* hostExtension(const clap_host_t*, const char*) {
    return nullptr;
}
void hostRequest(const clap_host_t*) {}

const clap_host_t* host() {
    static const clap_host_t h{CLAP_VERSION_INIT, nullptr, "Cutthulhu", "Cutthulhu", "https://github.com/open-compose/Lumenis",
                               "5.5", hostExtension, hostRequest, hostRequest, hostRequest};
    return &h;
}

// ---- loaded files

// A loaded CLAP file: its library and initialised entry. Kept for the program's life once loaded
// (the entry is not deinitialised: plug-in instances may outlive any one owner).
struct ClapLibrary {
    QLibrary library;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
};

// The binary inside a macOS bundle (Contents/MacOS/<name>); the file itself otherwise.
QString binaryOf(const QString& file) {
    const QFileInfo fi(file);
    if (fi.isDir()) return fi.absoluteFilePath() + QStringLiteral("/Contents/MacOS/") + fi.completeBaseName();
    return fi.absoluteFilePath();
}

std::shared_ptr<ClapLibrary> load(const QString& file, QString* error) {
    static std::mutex mutex;
    static std::map<QString, std::shared_ptr<ClapLibrary>> loaded;
    std::lock_guard lock(mutex);
    if (auto it = loaded.find(file); it != loaded.end()) return it->second;
    auto lib = std::make_shared<ClapLibrary>();
    lib->library.setFileName(binaryOf(file));
    if (!lib->library.load()) {
        if (error) *error = lib->library.errorString();
        return nullptr;
    }
    // clap_entry is data, not a function: its address comes back as a function pointer.
    const QFunctionPointer symbol = lib->library.resolve("clap_entry");
    const void* address = nullptr;
    static_assert(sizeof(symbol) == sizeof(address));
    std::memcpy(&address, &symbol, sizeof(address));
    lib->entry = static_cast<const clap_plugin_entry_t*>(address);
    if (!lib->entry) {
        if (error) *error = QStringLiteral("no clap_entry");
        return nullptr;
    }
    if (!clap_version_is_compatible(lib->entry->clap_version)) {
        if (error) *error = QStringLiteral("CLAP %1.%2.%3 is not supported").arg(lib->entry->clap_version.major)
                                .arg(lib->entry->clap_version.minor).arg(lib->entry->clap_version.revision);
        return nullptr;
    }
    if (!lib->entry->init(QFile::encodeName(QFileInfo(file).absoluteFilePath()).constData())) {
        if (error) *error = QStringLiteral("its entry did not initialise");
        return nullptr;
    }
    lib->factory = static_cast<const clap_plugin_factory_t*>(lib->entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!lib->factory) {
        if (error) *error = QStringLiteral("no plug-in factory");
        return nullptr;
    }
    loaded[file] = lib;
    return lib;
}

// The main audio port's channel count on one side: the port flagged main, else the first; 0 if none.
int mainChannels(const clap_plugin_t* plugin, bool input) {
    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (!ports) return 0;
    const uint32_t count = ports->count(plugin, input);
    int first = 0;
    for (uint32_t i = 0; i < count; ++i) {
        clap_audio_port_info_t info{};
        if (!ports->get(plugin, i, input, &info)) continue;
        if (i == 0) first = static_cast<int>(info.channel_count);
        if (info.flags & CLAP_AUDIO_PORT_IS_MAIN) return static_cast<int>(info.channel_count);
    }
    return first;
}

QList<AudioEffectParam> paramsOf(const clap_plugin_t* plugin) {
    QList<AudioEffectParam> out;
    const auto* params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params) return out;
    const uint32_t count = params->count(plugin);
    for (uint32_t i = 0; i < count; ++i) {
        clap_param_info_t info{};
        if (!params->get_info(plugin, i, &info)) continue;
        // Hidden and read-only parameters are not for the user to set.
        if (info.flags & (CLAP_PARAM_IS_HIDDEN | CLAP_PARAM_IS_READONLY)) continue;
        AudioEffectParam p;
        p.id = QString::number(info.id);
        p.name = QString::fromUtf8(info.name);
        p.min = info.min_value;
        p.max = info.max_value;
        p.defaultValue = std::clamp(info.default_value, p.min, p.max);
        // A stepped parameter with few values is a choice, named as the plug-in names them.
        if ((info.flags & CLAP_PARAM_IS_STEPPED) && p.max - p.min < 64) {
            p.steps = static_cast<int>(std::lround(p.max - p.min)) + 1;
            for (int s = 0; s < p.steps; ++s) {
                char text[256] = {};
                const double v = p.min + s;
                const bool named = params->value_to_text && params->value_to_text(plugin, info.id, v, text, sizeof(text));
                p.choices << (named ? QString::fromUtf8(text) : QString::number(v));
            }
        }
        out << p;
    }
    return out;
}

// ---- registered plug-ins

struct Registered {
    ClapPluginInfo clap;
    AudioEffectInfo effect;
};

std::map<QString, Registered>& registered() {
    static std::map<QString, Registered> r;
    return r;
}

QList<ClapPluginInfo>& known() {
    static QList<ClapPluginInfo> k;
    return k;
}

// One Audio Track Effect insert of a CLAP plug-in.
class ClapEffect : public AudioEffect {
public:
    ClapEffect(const Registered& r, std::shared_ptr<ClapLibrary> lib) : info_(r.effect), lib_(std::move(lib)) {
        channelsIn_ = r.clap.inputChannels;
        channelsOut_ = r.clap.outputChannels;
        plugin_ = lib_->factory->create_plugin(lib_->factory, host(), r.clap.id.toUtf8().constData());
        if (!plugin_) return;
        if (!plugin_->init(plugin_)) {
            plugin_->destroy(plugin_);
            plugin_ = nullptr;
            return;
        }
        tail_ = static_cast<const clap_plugin_tail_t*>(plugin_->get_extension(plugin_, CLAP_EXT_TAIL));
        active_ = plugin_->activate(plugin_, kRate, 1, kMaxBlock);
        for (const AudioEffectParam& p : info_.params) set(p.id, p.defaultValue);
        for (std::vector<float>& c : out_) c.assign(kMaxBlock, 0.0f);
    }
    ~ClapEffect() override {
        if (!plugin_) return;
        if (started_) plugin_->stop_processing(plugin_);
        if (active_) plugin_->deactivate(plugin_);
        plugin_->destroy(plugin_);
    }
    bool ok() const { return plugin_ && active_; }
    const AudioEffectInfo& info() const override { return info_; }

    void set(const QString& id, double value) override {
        const AudioEffectParam* p = info_.param(id);
        if (!p) return;
        value = std::clamp(value, p->min, p->max);
        if (p->steps > 0) value = std::round(value);
        std::lock_guard lock(mutex_);
        pending_[id.toUInt()] = value;
    }
    void reset() override {
        if (started_) plugin_->reset(plugin_);
    }
    int tail() const override {
        if (!tail_) return static_cast<int>(kRate); // ours: a second when the plug-in does not say
        const uint32_t t = tail_->get(plugin_);
        return t == UINT32_MAX ? static_cast<int>(4 * kRate) : static_cast<int>(std::min<uint32_t>(t, 4 * 48000));
    }

    // In blocks of at most kMaxBlock; a plug-in that fails a block leaves the sound as it was.
    void process(float* samples, int count) override {
        if (!ok()) return;
        if (!started_) started_ = plugin_->start_processing(plugin_);
        if (!started_) return;
        for (int at = 0; at < count; at += static_cast<int>(kMaxBlock)) {
            const auto n = static_cast<uint32_t>(std::min<int>(count - at, static_cast<int>(kMaxBlock)));
            float* in = samples + at;
            std::vector<clap_event_param_value_t> events;
            {
                std::lock_guard lock(mutex_);
                for (const auto& [id, value] : pending_) {
                    clap_event_param_value_t e{};
                    e.header.size = sizeof(e);
                    e.header.time = 0;
                    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    e.header.type = CLAP_EVENT_PARAM_VALUE;
                    e.param_id = id;
                    e.note_id = -1;
                    e.port_index = -1;
                    e.channel = -1;
                    e.key = -1;
                    e.value = value;
                    events.push_back(e);
                }
                pending_.clear();
            }
            const clap_input_events_t inEvents{&events, eventCount, eventAt};
            const clap_output_events_t outEvents{nullptr, dropEvent};
            float* inputs[2] = {in, in};
            float* outputs[2] = {out_[0].data(), out_[1].data()};
            clap_audio_buffer_t inBuffer{inputs, nullptr, static_cast<uint32_t>(channelsIn_), 0, 0};
            clap_audio_buffer_t outBuffer{outputs, nullptr, static_cast<uint32_t>(channelsOut_), 0, 0};
            clap_process_t p{};
            p.steady_time = steady_;
            p.frames_count = n;
            p.audio_inputs = &inBuffer;
            p.audio_outputs = &outBuffer;
            p.audio_inputs_count = 1;
            p.audio_outputs_count = 1;
            p.in_events = &inEvents;
            p.out_events = &outEvents;
            steady_ += n;
            if (plugin_->process(plugin_, &p) == CLAP_PROCESS_ERROR) continue;
            for (uint32_t i = 0; i < n; ++i) {
                in[i] = channelsOut_ == 1 ? out_[0][i] : 0.5f * (out_[0][i] + out_[1][i]);
            }
        }
    }

private:
    static uint32_t eventCount(const clap_input_events_t* list) {
        return static_cast<uint32_t>(static_cast<const std::vector<clap_event_param_value_t>*>(list->ctx)->size());
    }
    static const clap_event_header_t* eventAt(const clap_input_events_t* list, uint32_t index) {
        return &(*static_cast<const std::vector<clap_event_param_value_t>*>(list->ctx))[index].header;
    }
    static bool dropEvent(const clap_output_events_t*, const clap_event_header_t*) { return true; }

    const AudioEffectInfo& info_;
    std::shared_ptr<ClapLibrary> lib_;
    const clap_plugin_t* plugin_ = nullptr;
    const clap_plugin_tail_t* tail_ = nullptr;
    bool active_ = false;
    bool started_ = false;
    int channelsIn_ = 1;
    int channelsOut_ = 1;
    int64_t steady_ = 0;
    std::vector<float> out_[2];
    std::mutex mutex_;
    std::map<clap_id, double> pending_;
};

std::unique_ptr<AudioEffect> makeClapEffect(const QString& id) {
    const auto it = registered().find(id);
    if (it == registered().end()) return nullptr;
    auto lib = load(it->second.clap.file, nullptr);
    if (!lib) return nullptr;
    auto fx = std::make_unique<ClapEffect>(it->second, std::move(lib));
    if (!fx->ok()) return nullptr;
    return fx;
}

} // namespace

bool ClapPluginInfo::usable() const {
    return whyNot().isEmpty();
}

QString ClapPluginInfo::whyNot() const {
    if (!features.contains(QLatin1String(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT))) return QStringLiteral("not an audio effect");
    if (inputChannels < 1 || inputChannels > 2) return QStringLiteral("its main input has %1 channels").arg(inputChannels);
    if (outputChannels < 1 || outputChannels > 2) return QStringLiteral("its main output has %1 channels").arg(outputChannels);
    return {};
}

QString ClapPluginInfo::category() const {
    auto has = [this](const char* f) { return features.contains(QLatin1String(f)); };
    if (has(CLAP_PLUGIN_FEATURE_COMPRESSOR) || has(CLAP_PLUGIN_FEATURE_LIMITER) || has(CLAP_PLUGIN_FEATURE_GATE) ||
        has(CLAP_PLUGIN_FEATURE_EXPANDER) || has(CLAP_PLUGIN_FEATURE_TRANSIENT_SHAPER)) {
        return QStringLiteral("Dynamics");
    }
    if (has(CLAP_PLUGIN_FEATURE_EQUALIZER) || has(CLAP_PLUGIN_FEATURE_FILTER)) return QStringLiteral("EQ");
    if (has(CLAP_PLUGIN_FEATURE_DELAY)) return QStringLiteral("Delay");
    if (has(CLAP_PLUGIN_FEATURE_REVERB)) return QStringLiteral("Reverb");
    if (has(CLAP_PLUGIN_FEATURE_CHORUS) || has(CLAP_PLUGIN_FEATURE_FLANGER) || has(CLAP_PLUGIN_FEATURE_PHASER) ||
        has(CLAP_PLUGIN_FEATURE_TREMOLO) || has(CLAP_PLUGIN_FEATURE_PITCH_SHIFTER) || has(CLAP_PLUGIN_FEATURE_FREQUENCY_SHIFTER)) {
        return QStringLiteral("Modulation");
    }
    if (has(CLAP_PLUGIN_FEATURE_DISTORTION)) return QStringLiteral("Harmonic");
    return QStringLiteral("CLAP");
}

// The CLAP specification's locations (entry.h), then CLAP_PATH.
QStringList clapSearchPaths() {
    QStringList dirs;
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString home = QDir::homePath();
#if defined(Q_OS_MACOS)
    dirs << home + QStringLiteral("/Library/Audio/Plug-Ins/CLAP") << QStringLiteral("/Library/Audio/Plug-Ins/CLAP");
#elif defined(Q_OS_WIN)
    if (env.contains(QStringLiteral("LOCALAPPDATA"))) {
        dirs << env.value(QStringLiteral("LOCALAPPDATA")) + QStringLiteral("/Programs/Common/CLAP");
    }
    if (env.contains(QStringLiteral("COMMONPROGRAMFILES"))) {
        dirs << env.value(QStringLiteral("COMMONPROGRAMFILES")) + QStringLiteral("/CLAP");
    }
#else
    dirs << home + QStringLiteral("/.clap") << QStringLiteral("/usr/lib/clap");
#endif
    dirs += env.value(QStringLiteral("CLAP_PATH")).split(QDir::listSeparator(), Qt::SkipEmptyParts);
    QStringList out;
    for (const QString& d : dirs) {
        const QString clean = QDir::cleanPath(d);
        if (QFileInfo(clean).isDir() && !out.contains(clean)) out << clean;
    }
    return out;
}

// A bundle (a folder ending .clap) is one plug-in file; its insides are not searched.
QStringList findClapFiles(const QStringList& folders) {
    QStringList out;
    for (const QString& folder : folders) {
        QDirIterator it(folder, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        QStringList bundles;
        while (it.hasNext()) {
            const QString path = it.next();
            if (std::any_of(bundles.begin(), bundles.end(), [&](const QString& b) { return path.startsWith(b + QLatin1Char('/')); })) {
                continue;
            }
            if (!path.endsWith(QLatin1String(".clap"), Qt::CaseInsensitive)) continue;
            if (it.fileInfo().isDir()) bundles << path;
            out << QDir::cleanPath(path);
        }
    }
    out.removeDuplicates();
    std::sort(out.begin(), out.end());
    return out;
}

QList<ClapPluginInfo> scanClapPlugins(const QStringList& files, QStringList* errors) {
    QList<ClapPluginInfo> out;
    for (const QString& file : files) {
        QString error;
        const auto lib = load(file, &error);
        if (!lib) {
            if (errors) *errors << QStringLiteral("%1: %2").arg(file, error);
            continue;
        }
        const uint32_t count = lib->factory->get_plugin_count(lib->factory);
        for (uint32_t i = 0; i < count; ++i) {
            const clap_plugin_descriptor_t* d = lib->factory->get_plugin_descriptor(lib->factory, i);
            if (!d || !d->id || !d->name) continue;
            ClapPluginInfo info;
            info.file = file;
            info.id = QString::fromUtf8(d->id);
            info.name = QString::fromUtf8(d->name);
            info.vendor = d->vendor ? QString::fromUtf8(d->vendor) : QString();
            info.version = d->version ? QString::fromUtf8(d->version) : QString();
            for (const char* const* f = d->features; f && *f; ++f) info.features << QString::fromUtf8(*f);
            // Made once to read its ports and parameters.
            const clap_plugin_t* plugin = lib->factory->create_plugin(lib->factory, host(), d->id);
            if (!plugin || !plugin->init(plugin)) {
                if (plugin) plugin->destroy(plugin);
                if (errors) *errors << QStringLiteral("%1: %2 could not be made").arg(file, info.id);
                continue;
            }
            info.inputChannels = mainChannels(plugin, true);
            info.outputChannels = mainChannels(plugin, false);
            info.latency = plugin->get_extension(plugin, CLAP_EXT_LATENCY) != nullptr;
            info.params = paramsOf(plugin);
            plugin->destroy(plugin);
            out << info;
        }
    }
    return out;
}

int registerClapPlugins(const QList<ClapPluginInfo>& plugins) {
    QList<AudioEffectInfo> effects;
    for (const ClapPluginInfo& p : plugins) {
        known() << p;
        if (!p.usable()) continue;
        const QString id = QStringLiteral("clap:") + p.id;
        if (registered().count(id) || findAudioEffect(id)) continue;
        Registered r;
        r.clap = p;
        r.effect.id = id;
        r.effect.name = p.name;
        r.effect.category = p.category();
        r.effect.vendor = p.vendor;
        r.effect.params = p.params;
        effects << r.effect;
        registered().emplace(id, std::move(r));
    }
    if (!effects.isEmpty()) registerAudioEffects(effects, makeClapEffect);
    return static_cast<int>(effects.size());
}

const QList<ClapPluginInfo>& clapPlugins() {
    return known();
}

ext::ProviderInfo ClapHostProvider::info() const {
    ext::ProviderInfo i;
    i.id = "org.cleveraudio.clap-host";
    i.displayName = "CLAP plug-in host";
    i.category = ext::Category::AudioPluginHost;
    i.tier = ext::LicenseTier::OpenSource;
    i.vendor = "Cutthulhu";
    i.version = "CLAP " + std::to_string(CLAP_VERSION_MAJOR) + "." + std::to_string(CLAP_VERSION_MINOR) + "." +
                std::to_string(CLAP_VERSION_REVISION);
    return i;
}

std::vector<std::filesystem::path> ClapHostProvider::defaultSearchPaths() const {
    std::vector<std::filesystem::path> out;
    for (const QString& d : clapSearchPaths()) out.emplace_back(d.toStdString());
    return out;
}

std::vector<ext::PluginDescriptor> ClapHostProvider::scan(const std::vector<std::filesystem::path>& paths) {
    QStringList folders;
    for (const auto& p : paths) folders << QString::fromStdString(p.string());
    std::vector<ext::PluginDescriptor> out;
    for (const ClapPluginInfo& p : scanClapPlugins(findClapFiles(folders))) {
        ext::PluginDescriptor d;
        d.standard = ext::PluginStandard::CLAP;
        d.id = p.id.toStdString();
        d.name = p.name.toStdString();
        d.vendor = p.vendor.toStdString();
        d.version = p.version.toStdString();
        d.location = p.file.toStdString();
        out.push_back(std::move(d));
    }
    return out;
}

} // namespace lumenis::media
