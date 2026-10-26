#include "lumenis/media/audio_input.h"

#include <miniaudio.h>

#include <algorithm>
#include <chrono>

// Recording from an input (audio_input.h). miniaudio opens the capture device and calls push() on its
// own thread with 48 kHz float frames; recordings are appended to one vector per channel under a
// mutex held only for the copy. Monitoring passes a mono mix through a lock-free ring to the output's
// thread (AudioPlayer::pull).
namespace lumenis::media {

namespace {

constexpr int kRate = 48000;
constexpr std::size_t kMonitorRing = 1 << 15; // about 0.7 s at 48 kHz
constexpr std::size_t kMonitorLag = kRate / 20; // 50 ms

void captureCallback(ma_device* device, void*, const void* input, ma_uint32 frames) {
    auto* in = static_cast<AudioInput*>(device->pUserData);
    in->push(static_cast<const float*>(input), static_cast<int>(frames));
}

} // namespace

struct AudioInput::Device {
    ma_context context{};
    ma_device device{};
    bool contextOpen = false;
    bool open = false;
    ~Device() {
        if (open) ma_device_uninit(&device);
        if (contextOpen) ma_context_uninit(&context);
    }
};

QList<AudioInputDevice> audioInputDevices() {
    QList<AudioInputDevice> out;
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) return out;
    ma_device_info* capture = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, nullptr, nullptr, &capture, &count) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            AudioInputDevice d;
            d.name = QString::fromUtf8(capture[i].name);
            d.isDefault = capture[i].isDefault != 0;
            ma_device_info info;
            if (ma_context_get_device_info(&context, ma_device_type_capture, &capture[i].id, &info) == MA_SUCCESS) {
                for (ma_uint32 f = 0; f < info.nativeDataFormatCount; ++f) {
                    d.channels = std::max(d.channels, static_cast<int>(info.nativeDataFormats[f].channels));
                }
            }
            out << d;
        }
    }
    ma_context_uninit(&context);
    return out;
}

AudioInput::AudioInput() : monitor_(kMonitorRing, 0.0f) {}

AudioInput::~AudioInput() {
    stop();
}

void AudioInput::reset(int channels) {
    stop();
    std::lock_guard lock(mutex_);
    channels_ = std::max(channels, 1);
    recorded_.assign(static_cast<std::size_t>(channels_), {});
    // A minute ahead, so the device's thread rarely waits on a reallocation.
    for (std::vector<float>& v : recorded_) v.reserve(static_cast<std::size_t>(60 * kRate));
    // The ring stays where it is: the output's thread may be reading it.
    monitorRead_.store(monitorWrite_.load());
    latency_ = 0;
}

bool AudioInput::start(const QString& name, int channels) {
    reset(channels);
    auto d = std::make_unique<Device>();
    if (ma_context_init(nullptr, 0, nullptr, &d->context) != MA_SUCCESS) return false;
    d->contextOpen = true;
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = static_cast<ma_uint32>(channels_);
    config.sampleRate = kRate;
    config.dataCallback = captureCallback;
    config.pUserData = this;
    // The named device, else the default one.
    ma_device_info* capture = nullptr;
    ma_uint32 count = 0;
    ma_device_id id{};
    if (!name.isEmpty() && ma_context_get_devices(&d->context, nullptr, nullptr, &capture, &count) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            if (QString::fromUtf8(capture[i].name) == name) {
                id = capture[i].id;
                config.capture.pDeviceID = &id;
            }
        }
    }
    if (ma_device_init(&d->context, &config, &d->device) != MA_SUCCESS) return false;
    d->open = true;
    const ma_uint32 rate = d->device.capture.internalSampleRate;
    if (rate > 0) {
        latency_ = static_cast<double>(d->device.capture.internalPeriodSizeInFrames) *
                   static_cast<double>(d->device.capture.internalPeriods) / static_cast<double>(rate);
    }
    name_ = QString::fromUtf8(d->device.capture.name);
    running_ = true;
    if (ma_device_start(&d->device) != MA_SUCCESS) {
        running_ = false;
        return false;
    }
    device_ = std::move(d);
    return true;
}

bool AudioInput::startSimulated(int channels, Generator generator) {
    reset(channels);
    name_ = QStringLiteral("Simulated");
    running_ = true;
    generator_ = std::thread([this, make = std::move(generator)] {
        constexpr int kBlock = kRate / 100;
        std::vector<float> block(static_cast<std::size_t>(kBlock * channels_));
        std::int64_t n = 0;
        const auto start = std::chrono::steady_clock::now();
        while (running_) {
            // Every block whose time has come, so the count follows the wall clock.
            const auto due = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start)
                                 .count() * kRate / 1'000'000;
            while (n + kBlock <= due && running_) {
                for (int i = 0; i < kBlock; ++i) {
                    for (int c = 0; c < channels_; ++c) {
                        block[static_cast<std::size_t>(i * channels_ + c)] = make(c, n + i);
                    }
                }
                push(block.data(), kBlock);
                n += kBlock;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    return true;
}

void AudioInput::stop() {
    running_ = false;
    // Stops the callbacks before anything they use goes.
    device_.reset();
    if (generator_.joinable()) generator_.join();
}

std::int64_t AudioInput::recorded() const {
    std::lock_guard lock(mutex_);
    return recorded_.empty() ? 0 : static_cast<std::int64_t>(recorded_.front().size());
}

std::vector<float> AudioInput::samples(int channel) const {
    std::lock_guard lock(mutex_);
    if (channel < 0 || channel >= static_cast<int>(recorded_.size())) return {};
    return recorded_[static_cast<std::size_t>(channel)];
}

void AudioInput::push(const float* interleaved, int frames) {
    if (frames <= 0 || channels_ <= 0) return;
    {
        std::lock_guard lock(mutex_);
        for (int c = 0; c < channels_; ++c) {
            std::vector<float>& v = recorded_[static_cast<std::size_t>(c)];
            for (int i = 0; i < frames; ++i) v.push_back(interleaved[i * channels_ + c]);
        }
    }
    std::size_t w = monitorWrite_.load();
    for (int i = 0; i < frames; ++i, ++w) {
        float sum = 0;
        for (int c = 0; c < channels_; ++c) sum += interleaved[i * channels_ + c];
        monitor_[w % kMonitorRing] = sum / static_cast<float>(channels_);
    }
    monitorWrite_.store(w);
}

int AudioInput::addMonitor(float* out, int frames) {
    if (frames <= 0) return 0;
    const std::size_t w = monitorWrite_.load();
    std::size_t r = monitorRead_.load();
    // Too far behind (or overrun): skip to 50 ms before the newest.
    if (w - r > kMonitorLag) r = w - kMonitorLag;
    const auto n = static_cast<int>(std::min<std::size_t>(w - r, static_cast<std::size_t>(frames)));
    for (int i = 0; i < n; ++i, ++r) {
        const float s = monitor_[r % kMonitorRing];
        out[2 * i] += s;
        out[2 * i + 1] += s;
    }
    monitorRead_.store(r);
    return n;
}

} // namespace lumenis::media
