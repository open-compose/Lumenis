#include "lumenis/ui/media_frames.h"

#include <QMetaObject>

// MediaFrames: the application's source of clip pictures (media_frames.h). Monitors call frame() on
// the GUI thread; bins, the Timeline and the Source Browser call thumbnail(), which decodes on a small
// thread pool and posts the result back; playback calls prefetch() so the next frames are already in
// media::FrameCache (src/media/link.*) when the Composer shows them (playback in src/media/playback.*).

namespace lumenis::ui {

namespace {
MediaFrames* g_instance = nullptr;
// Thumbnails kept; a bin of a few hundred clips in Frame view.
constexpr std::size_t kMaxThumbnails = 2000;
} // namespace

// The cache keeps 8 files open and the last 48 frames. Two threads decode thumbnails; one decodes
// ahead for playback, so those frames come in play order.
MediaFrames::MediaFrames(QObject* parent) : QObject(parent), cache_(8, 48) {
    pool_.setMaxThreadCount(2);
    ahead_.setMaxThreadCount(1);
    if (!g_instance) g_instance = this;
}

MediaFrames::~MediaFrames() {
    ahead_.clear();
    ahead_.waitForDone();
    pool_.clear();
    pool_.waitForDone();
    if (g_instance == this) g_instance = nullptr;
}

MediaFrames* MediaFrames::instance() {
    return g_instance;
}

QImage MediaFrames::frame(const model::BinItem& item, std::int64_t clipFrame, QSize size) {
    return cache_.frame(item, clipFrame, size);
}

bool MediaFrames::isOffline(const model::BinItem& item) {
    return media::isLinked(item) && !cache_.isOnline(item);
}

QString MediaFrames::key(const model::BinItem& item, std::int64_t clipFrame, QSize size) {
    // One arg() call: a sequence's path holds a %04d of its own.
    return QStringLiteral("%1|%2|%3x%4")
        .arg(item.link.value(QStringLiteral("path")).toString(), QString::number(clipFrame),
             QString::number(size.width()), QString::number(size.height()));
}

// Returns a decoded thumbnail, or starts decoding it once (pending_) and returns null; the result is
// stored on the GUI thread and announced with thumbnailReady().
QImage MediaFrames::thumbnail(const model::BinItem& item, std::int64_t clipFrame, QSize size) {
    if (!media::isLinked(item) || media::isUnlinked(item)) return {};
    const QString k = key(item, clipFrame, size);
    if (const auto it = thumbnails_.find(k); it != thumbnails_.end()) return it->second;
    if (!pending_.insert(k).second) return {};
    const QString path = item.link.value(QStringLiteral("path")).toString();
    pool_.start([this, item, clipFrame, size, k, path] {
        // The worker threads share the cache, which locks itself.
        const QImage image = cache_.frame(item, clipFrame, size);
        // Queued to this object's thread; dropped if it is gone by then (it
        // waits for its workers first, so it is not).
        QMetaObject::invokeMethod(
            this,
            [this, k, path, image] {
                pending_.erase(k);
                if (thumbnails_.size() >= kMaxThumbnails) thumbnails_.clear();
                // A null image is remembered too: the file has no picture.
                thumbnails_[k] = image;
                emit thumbnailReady(path);
            },
            Qt::QueuedConnection);
    });
    return {};
}

// Decodes into the frame cache ahead of playback. At most three requests wait; more are dropped, so a
// slow decoder does not fall further and further behind the play head.
void MediaFrames::prefetch(const model::BinItem& item, std::int64_t clipFrame, QSize size) {
    if (!media::isLinked(item) || media::isUnlinked(item) || aheadWaiting_.load() >= 3) return;
    ++aheadWaiting_;
    ahead_.start([this, item, clipFrame, size] {
        // Kept in the frame cache, where frame() finds it.
        cache_.frame(item, clipFrame, size);
        --aheadWaiting_;
    });
}

void MediaFrames::waitForThumbnails() {
    ahead_.waitForDone();
    pool_.waitForDone();
}

void MediaFrames::clear() {
    ahead_.clear();
    ahead_.waitForDone();
    aheadWaiting_ = 0;
    pool_.clear();
    pool_.waitForDone();
    cache_.clear();
    thumbnails_.clear();
    pending_.clear();
}

} // namespace lumenis::ui
