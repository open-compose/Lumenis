#pragma once

#include <QHash>
#include <QSet>
#include <QString>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// Audio waveforms for the Timeline (Editing Guide p.677-680; docs/spec/waveforms.md): the peaks of
// one channel of a file's audio stream, worked out once on a thread of their own and kept, in
// memory and in the project's WaveformCache folder ("Media Composer saves cached waveforms for
// projects. This allows the waveform to draw faster the next time you open the project", p.677).
// The Timeline draws them (src/ui/timeline.cpp, TimelineView::paintWaveform).
namespace lumenis::media {

// Which sound: a file's audio stream and the channel in it.
struct WaveformKey {
    QString path;
    int stream = -1; // as AudioReader::open takes it
    int channel = 0;
    bool operator==(const WaveformKey&) const = default;
    QString text() const; // path|stream|channel, the key of the caches
};

// The peaks of one channel: for every kBucket samples (at the file's own rate) the lowest and the
// highest sample, as -127 to 127 of full scale (rounded outwards, so a peak is never drawn smaller),
// and the same for every kCoarse buckets, which zoomed-out views read.
struct Waveform {
    static constexpr int kBucket = 256;
    static constexpr int kCoarse = 16;
    int sampleRate = 0;
    std::int64_t samples = 0;
    std::vector<std::int8_t> low;
    std::vector<std::int8_t> high;
    std::vector<std::int8_t> coarseLow;
    std::vector<std::int8_t> coarseHigh;
    // The lowest and highest sample (full scale is 1) over file samples [from, to); {0, 0} outside the
    // sound. Reads the coarse peaks when the range spans many of them.
    std::pair<float, float> range(std::int64_t from, std::int64_t to) const;
};

// Reads the whole channel and works out its peaks; null, with the reason, if it cannot be read.
std::shared_ptr<Waveform> computeWaveform(const WaveformKey& key, QString* error = nullptr);
// The cache file of a waveform: our own format ("CTWF", a version, the source file's size and
// modification time, the rate, the length, then the peaks). load() refuses a file of another
// version or one whose source has changed since.
bool saveWaveform(const QString& file, const WaveformKey& key, const Waveform& waveform);
std::shared_ptr<Waveform> loadWaveform(const QString& file, const WaveformKey& key);

// The waveforms asked for so far. get() returns a waveform at once if it is in memory; otherwise it
// is read from the folder, or worked out, on the cache's own thread, and `ready` is called (on that
// thread) when it is, so the Timeline can draw again. A sound that cannot be read is not asked for
// again until clear().
class WaveformCache {
public:
    explicit WaveformCache(std::function<void()> ready = {});
    ~WaveformCache(); // stops the thread; what is half done is dropped
    WaveformCache(const WaveformCache&) = delete;
    WaveformCache& operator=(const WaveformCache&) = delete;

    // Where waveforms are kept on disk (the project's WaveformCache folder); empty: in memory only.
    void setFolder(const QString& folder);
    QString folder() const;
    std::shared_ptr<const Waveform> get(const WaveformKey& key);
    // Forgets every waveform in memory (the files stay).
    void clear();
    // Waits until nothing is queued (tests).
    void wait();

private:
    void run();
    QString fileFor(const WaveformKey& key) const;

    std::function<void()> ready_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    QString folder_;
    QHash<QString, std::shared_ptr<const Waveform>> done_;
    QSet<QString> failed_;
    QSet<QString> queued_;
    std::deque<WaveformKey> queue_;
    bool busy_ = false;
    bool stop_ = false;
    std::thread thread_;
};

} // namespace lumenis::media
