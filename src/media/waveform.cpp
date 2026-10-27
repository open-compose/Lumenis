// Waveform peaks (lumenis/media/waveform.h): read through AudioReader in quarter-second pieces,
// kept as signed bytes per 256 samples, saved in our own small binary format.
#include "lumenis/media/waveform.h"

#include "lumenis/media/media.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace lumenis::media {

namespace {

constexpr quint32 kMagic = 0x43545746; // "CTWF"
constexpr quint32 kVersion = 1;

std::int8_t quantise(float v, bool up) {
    const float scaled = std::clamp(v, -1.0f, 1.0f) * 127.0f;
    return static_cast<std::int8_t>(up ? std::ceil(scaled) : std::floor(scaled));
}

// The coarse peaks from the fine ones.
void makeCoarse(Waveform& w) {
    const std::size_t n = (w.low.size() + Waveform::kCoarse - 1) / Waveform::kCoarse;
    w.coarseLow.assign(n, 0);
    w.coarseHigh.assign(n, 0);
    for (std::size_t i = 0; i < w.low.size(); ++i) {
        const std::size_t c = i / Waveform::kCoarse;
        if (i % Waveform::kCoarse == 0) {
            w.coarseLow[c] = w.low[i];
            w.coarseHigh[c] = w.high[i];
        } else {
            w.coarseLow[c] = std::min(w.coarseLow[c], w.low[i]);
            w.coarseHigh[c] = std::max(w.coarseHigh[c], w.high[i]);
        }
    }
}

// What a cache file checks its source against.
std::pair<qint64, qint64> stamp(const QString& path) {
    const QFileInfo info(path);
    return {info.size(), info.lastModified().toMSecsSinceEpoch()};
}

} // namespace

QString WaveformKey::text() const {
    return path + QLatin1Char('|') + QString::number(stream) + QLatin1Char('|') + QString::number(channel);
}

std::pair<float, float> Waveform::range(std::int64_t from, std::int64_t to) const {
    from = std::max<std::int64_t>(from, 0);
    to = std::min(to, samples);
    if (to <= from || low.empty()) return {0.0f, 0.0f};
    auto first = static_cast<std::size_t>(from / kBucket);
    auto last = std::min(static_cast<std::size_t>((to - 1) / kBucket), low.size() - 1);
    std::int8_t lo = 127;
    std::int8_t hi = -127;
    // Whole coarse peaks in the middle when the range spans several of them.
    std::size_t i = first;
    while (i <= last) {
        if (i % kCoarse == 0 && i + kCoarse - 1 <= last && i / kCoarse < coarseLow.size()) {
            lo = std::min(lo, coarseLow[i / kCoarse]);
            hi = std::max(hi, coarseHigh[i / kCoarse]);
            i += kCoarse;
        } else {
            lo = std::min(lo, low[i]);
            hi = std::max(hi, high[i]);
            ++i;
        }
    }
    return {static_cast<float>(lo) / 127.0f, static_cast<float>(hi) / 127.0f};
}

std::shared_ptr<Waveform> computeWaveform(const WaveformKey& key, QString* error) {
    AudioReader reader;
    if (!reader.open(key.path, key.stream, error)) return nullptr;
    const int channels = std::max(reader.channels(), 1);
    if (key.channel < 0 || key.channel >= channels) {
        if (error) *error = QStringLiteral("no channel %1").arg(key.channel + 1);
        return nullptr;
    }
    auto w = std::make_shared<Waveform>();
    w->sampleRate = reader.sampleRate();
    // A quarter second at a time, a whole number of buckets.
    const int chunk = std::max(reader.sampleRate() / 4 / Waveform::kBucket, 1) * Waveform::kBucket;
    float lo = 0;
    float hi = 0;
    int inBucket = 0;
    for (std::int64_t at = 0;; at += chunk) {
        const std::vector<float> got = reader.samples(at, chunk);
        const std::size_t frames = got.size() / static_cast<std::size_t>(channels);
        for (std::size_t i = 0; i < frames; ++i) {
            const float v = got[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(key.channel)];
            if (inBucket == 0) {
                lo = v;
                hi = v;
            } else {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            if (++inBucket == Waveform::kBucket) {
                w->low.push_back(quantise(lo, false));
                w->high.push_back(quantise(hi, true));
                inBucket = 0;
            }
        }
        w->samples += static_cast<std::int64_t>(frames);
        if (frames < static_cast<std::size_t>(chunk)) break;
    }
    if (inBucket > 0) {
        w->low.push_back(quantise(lo, false));
        w->high.push_back(quantise(hi, true));
    }
    makeCoarse(*w);
    return w;
}

bool saveWaveform(const QString& file, const WaveformKey& key, const Waveform& waveform) {
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)) return false;
    QDataStream s(&out);
    const auto [size, modified] = stamp(key.path);
    s << kMagic << kVersion << key.text() << size << modified << static_cast<qint32>(waveform.sampleRate)
      << static_cast<qint64>(waveform.samples) << static_cast<quint64>(waveform.low.size());
    s.writeRawData(reinterpret_cast<const char*>(waveform.low.data()), static_cast<int>(waveform.low.size()));
    s.writeRawData(reinterpret_cast<const char*>(waveform.high.data()), static_cast<int>(waveform.high.size()));
    return s.status() == QDataStream::Ok && out.commit();
}

std::shared_ptr<Waveform> loadWaveform(const QString& file, const WaveformKey& key) {
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) return nullptr;
    QDataStream s(&in);
    quint32 magic = 0;
    quint32 version = 0;
    QString text;
    qint64 size = 0;
    qint64 modified = 0;
    qint32 rate = 0;
    qint64 samples = 0;
    quint64 count = 0;
    s >> magic >> version >> text >> size >> modified >> rate >> samples >> count;
    const auto [nowSize, nowModified] = stamp(key.path);
    if (s.status() != QDataStream::Ok || magic != kMagic || version != kVersion || text != key.text() || size != nowSize ||
        modified != nowModified || count > (1ull << 31)) {
        return nullptr;
    }
    auto w = std::make_shared<Waveform>();
    w->sampleRate = rate;
    w->samples = samples;
    w->low.resize(count);
    w->high.resize(count);
    if (s.readRawData(reinterpret_cast<char*>(w->low.data()), static_cast<int>(count)) != static_cast<int>(count) ||
        s.readRawData(reinterpret_cast<char*>(w->high.data()), static_cast<int>(count)) != static_cast<int>(count)) {
        return nullptr;
    }
    makeCoarse(*w);
    return w;
}

WaveformCache::WaveformCache(std::function<void()> ready) : ready_(std::move(ready)) {}

WaveformCache::~WaveformCache() {
    {
        const std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WaveformCache::setFolder(const QString& folder) {
    const std::lock_guard lock(mutex_);
    folder_ = folder;
}

QString WaveformCache::folder() const {
    const std::lock_guard lock(mutex_);
    return folder_;
}

std::shared_ptr<const Waveform> WaveformCache::get(const WaveformKey& key) {
    const QString text = key.text();
    const std::lock_guard lock(mutex_);
    if (const auto it = done_.constFind(text); it != done_.constEnd()) return it.value();
    if (failed_.contains(text) || queued_.contains(text) || key.path.isEmpty()) return nullptr;
    queued_.insert(text);
    queue_.push_back(key);
    if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
    wake_.notify_one();
    return nullptr;
}

void WaveformCache::clear() {
    const std::lock_guard lock(mutex_);
    done_.clear();
    failed_.clear();
}

void WaveformCache::wait() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && !busy_; });
}

QString WaveformCache::fileFor(const WaveformKey& key) const {
    if (folder_.isEmpty()) return {};
    const QByteArray hash = QCryptographicHash::hash(key.text().toUtf8(), QCryptographicHash::Sha1).toHex().left(20);
    return QDir(folder_).filePath(QString::fromLatin1(hash) + QStringLiteral(".wfm"));
}

void WaveformCache::run() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (stop_) return;
        const WaveformKey key = queue_.front();
        queue_.pop_front();
        busy_ = true;
        const QString file = fileFor(key);
        lock.unlock();
        std::shared_ptr<Waveform> w = file.isEmpty() ? nullptr : loadWaveform(file, key);
        if (!w) {
            w = computeWaveform(key);
            if (w && !file.isEmpty()) saveWaveform(file, key, *w);
        }
        lock.lock();
        busy_ = false;
        queued_.remove(key.text());
        if (w) {
            done_.insert(key.text(), w);
        } else {
            failed_.insert(key.text());
        }
        if (queue_.empty()) idle_.notify_all();
        if (stop_) return;
        if (w && ready_) {
            lock.unlock();
            ready_();
            lock.lock();
        }
    }
}

} // namespace lumenis::media
