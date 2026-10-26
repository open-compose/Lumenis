#pragma once

#include <QList>
#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Recording sound from an input (Audio Punch-In, Editing Guide p.746-753; docs/spec/punch-in.md):
// the system's audio inputs through miniaudio, or a simulated input that makes known samples at the
// real rate (tests, and machines with no microphone). What is recorded is kept in memory until the
// caller writes it as media (media::renderedAudioClip); a copy goes to a small ring that the output
// (AudioPlayer::setMonitor) plays, so the speaker can hear the input (p.748-749, Monitor Mode).
namespace lumenis::media {

// An input the system offers: its name (as the Input Source menu shows it, p.749) and how many
// channels it records.
struct AudioInputDevice {
    QString name;
    int channels = 2;
    bool isDefault = false;
};
// The capture devices miniaudio finds; empty without any.
QList<AudioInputDevice> audioInputDevices();

class AudioInput {
public:
    // Makes channel `channel`'s sample `n` (counted from the start of the recording) for a simulated
    // input.
    using Generator = std::function<float(int channel, std::int64_t n)>;

    AudioInput();
    ~AudioInput();
    AudioInput(const AudioInput&) = delete;
    AudioInput& operator=(const AudioInput&) = delete;

    // Starts recording `channels` channels at 48 kHz from the named device (empty: the default),
    // dropping anything recorded before. False if it cannot be opened.
    bool start(const QString& device, int channels);
    // The same from a generator, run on a thread of its own in 10 ms blocks at the real rate.
    bool startSimulated(int channels, Generator generator);
    // Stops the device or the generator; what was recorded stays until the next start().
    void stop();
    bool isRunning() const { return running_; }
    int channels() const { return channels_; }
    // The name of the device recording ("Simulated" for a generator).
    QString deviceName() const { return name_; }
    // The device's own delay in seconds, as miniaudio reports its buffering (0 if unknown).
    double latency() const { return latency_; }

    // Samples recorded so far, per channel.
    std::int64_t recorded() const;
    // A copy of channel `channel`'s samples.
    std::vector<float> samples(int channel) const;

    // Adds `frames` interleaved frames of channels() channels (the device's callback and the
    // generator call this; tests may too).
    void push(const float* interleaved, int frames);
    // Adds up to `frames` frames of what came in since the last call to stereo `out` (the
    // channels' average on both sides), for monitoring. Called from the output's own thread.
    // Returns how many were added; what is more than 50 ms behind is dropped.
    int addMonitor(float* out, int frames);

private:
    struct Device;
    void reset(int channels);

    std::unique_ptr<Device> device_;
    std::thread generator_;
    std::atomic<bool> running_{false};
    int channels_ = 0;
    QString name_;
    double latency_ = 0;
    mutable std::mutex mutex_;
    std::vector<std::vector<float>> recorded_;
    // A single-producer, single-consumer ring of mono samples for monitoring.
    std::vector<float> monitor_;
    std::atomic<std::size_t> monitorRead_{0};
    std::atomic<std::size_t> monitorWrite_{0};
};

} // namespace lumenis::media
