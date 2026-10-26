// A CLAP file of our own for the CLAP host's tests (tests/media/tst_clap_host.cpp;
// docs/spec/clap-host.md). It offers three plug-ins:
// - "org.cutthulhu.test.gain": mono; Gain (dB, param 7) and Invert (stepped Off/On, param 9); a tail
//   of 4800 samples;
// - "org.cutthulhu.test.stereo": stereo in and out; Level (param 1) scales both sides;
// - "org.cutthulhu.test.synth": an instrument, which the host leaves out.
#include <clap/clap.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// The plug-in's own state behind clap_plugin::plugin_data.
struct Instance {
    clap_plugin_t plugin;
    const clap_host_t* host = nullptr;
    int kind = 0; // 0 gain, 1 stereo, 2 synth
    double gainDb = 0;
    double invert = 0;
    double level = 1;
    bool active = false;
    bool processing = false;
};

Instance* self(const clap_plugin_t* p) {
    return static_cast<Instance*>(p->plugin_data);
}

const char* const kGainFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};
const char* const kStereoFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, nullptr};
const char* const kSynthFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, nullptr};

const clap_plugin_descriptor_t kDescriptors[] = {
    {CLAP_VERSION_INIT, "org.cutthulhu.test.gain", "Test Gain", "Cutthulhu Tests", "", "", "", "1.0", "", kGainFeatures},
    {CLAP_VERSION_INIT, "org.cutthulhu.test.stereo", "Test Stereo", "Cutthulhu Tests", "", "", "", "1.0", "", kStereoFeatures},
    {CLAP_VERSION_INIT, "org.cutthulhu.test.synth", "Test Synth", "Cutthulhu Tests", "", "", "", "1.0", "", kSynthFeatures},
};

// ---- audio ports

uint32_t portCount(const clap_plugin_t* p, bool input) {
    return input && self(p)->kind == 2 ? 0 : 1;
}

bool portGet(const clap_plugin_t* p, uint32_t index, bool input, clap_audio_port_info_t* info) {
    if (index != 0 || (input && self(p)->kind == 2)) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = input ? 0 : 1;
    std::snprintf(info->name, sizeof(info->name), "%s", input ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = self(p)->kind == 0 ? 1 : 2;
    info->port_type = self(p)->kind == 0 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t kPorts{portCount, portGet};

// ---- parameters

uint32_t paramCount(const clap_plugin_t* p) {
    return self(p)->kind == 0 ? 2 : self(p)->kind == 1 ? 1 : 0;
}

bool paramInfo(const clap_plugin_t* p, uint32_t index, clap_param_info_t* info) {
    std::memset(info, 0, sizeof(*info));
    if (self(p)->kind == 0 && index == 0) {
        info->id = 7;
        std::snprintf(info->name, sizeof(info->name), "Gain");
        info->min_value = -60;
        info->max_value = 12;
        info->default_value = 0;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        return true;
    }
    if (self(p)->kind == 0 && index == 1) {
        info->id = 9;
        std::snprintf(info->name, sizeof(info->name), "Invert");
        info->min_value = 0;
        info->max_value = 1;
        info->default_value = 0;
        info->flags = CLAP_PARAM_IS_STEPPED;
        return true;
    }
    if (self(p)->kind == 1 && index == 0) {
        info->id = 1;
        std::snprintf(info->name, sizeof(info->name), "Level");
        info->min_value = 0;
        info->max_value = 2;
        info->default_value = 1;
        return true;
    }
    return false;
}

double* valueOf(Instance* in, clap_id id) {
    if (in->kind == 0 && id == 7) return &in->gainDb;
    if (in->kind == 0 && id == 9) return &in->invert;
    if (in->kind == 1 && id == 1) return &in->level;
    return nullptr;
}

bool paramValue(const clap_plugin_t* p, clap_id id, double* out) {
    const double* v = valueOf(self(p), id);
    if (!v) return false;
    *out = *v;
    return true;
}

bool paramText(const clap_plugin_t* p, clap_id id, double value, char* text, uint32_t size) {
    if (self(p)->kind == 0 && id == 9) {
        std::snprintf(text, size, "%s", value > 0.5 ? "On" : "Off");
        return true;
    }
    std::snprintf(text, size, "%.2f", value);
    return true;
}

bool paramFromText(const clap_plugin_t*, clap_id, const char*, double*) {
    return false;
}

void applyEvents(Instance* in, const clap_input_events_t* events) {
    if (!events) return;
    const uint32_t n = events->size(events);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* h = events->get(events, i);
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
        if (double* v = valueOf(in, e->param_id)) *v = e->value;
    }
}

void paramFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
    applyEvents(self(p), in);
}

const clap_plugin_params_t kParams{paramCount, paramInfo, paramValue, paramText, paramFromText, paramFlush};

// ---- tail

uint32_t tailGet(const clap_plugin_t*) {
    return 4800;
}

const clap_plugin_tail_t kTail{tailGet};

// ---- the plug-in

bool init(const clap_plugin_t*) {
    return true;
}

void destroy(const clap_plugin_t* p) {
    delete self(p);
}

bool activate(const clap_plugin_t* p, double rate, uint32_t, uint32_t) {
    self(p)->active = rate == 48000;
    return self(p)->active;
}

void deactivate(const clap_plugin_t* p) {
    self(p)->active = false;
}

bool startProcessing(const clap_plugin_t* p) {
    self(p)->processing = self(p)->active;
    return self(p)->processing;
}

void stopProcessing(const clap_plugin_t* p) {
    self(p)->processing = false;
}

void reset(const clap_plugin_t*) {}

clap_process_status process(const clap_plugin_t* p, const clap_process_t* process) {
    Instance* in = self(p);
    if (!in->processing || in->kind == 2) return CLAP_PROCESS_ERROR;
    applyEvents(in, process->in_events);
    const clap_audio_buffer_t& src = process->audio_inputs[0];
    clap_audio_buffer_t& dst = process->audio_outputs[0];
    const float k = in->kind == 0 ? static_cast<float>(std::pow(10.0, in->gainDb / 20.0) * (in->invert > 0.5 ? -1 : 1))
                                  : static_cast<float>(in->level);
    for (uint32_t c = 0; c < dst.channel_count; ++c) {
        for (uint32_t i = 0; i < process->frames_count; ++i) dst.data32[c][i] = src.data32[c][i] * k;
    }
    return CLAP_PROCESS_CONTINUE;
}

const void* extension(const clap_plugin_t* p, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0 && self(p)->kind != 2) return &kParams;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0 && self(p)->kind == 0) return &kTail;
    return nullptr;
}

void onMainThread(const clap_plugin_t*) {}

// ---- the factory and entry

uint32_t factoryCount(const clap_plugin_factory_t*) {
    return 3;
}

const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t index) {
    return index < 3 ? &kDescriptors[index] : nullptr;
}

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    for (int k = 0; k < 3; ++k) {
        if (std::strcmp(id, kDescriptors[k].id) != 0) continue;
        auto* in = new Instance;
        in->host = host;
        in->kind = k;
        in->plugin = clap_plugin_t{&kDescriptors[k], in, init, destroy, activate, deactivate, startProcessing,
                                   stopProcessing, reset, process, extension, onMainThread};
        return &in->plugin;
    }
    return nullptr;
}

const clap_plugin_factory_t kFactory{factoryCount, factoryDescriptor, factoryCreate};

bool entryInit(const char*) {
    return true;
}

void entryDeinit() {}

const void* entryFactory(const char* id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, entryInit, entryDeinit, entryFactory};
