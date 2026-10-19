#pragma once

#include "lumenis/media/link.h"
#include "lumenis/model/bin.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QThreadPool>

#include <atomic>
#include <cstdint>
#include <map>
#include <set>

namespace lumenis::ui {

// The pictures of linked clips for the monitors, bins and the Source
// Browser. Monitors ask for frames as they need them; thumbnails are decoded
// on worker threads and announced when ready.
//
// MainWindow makes the one instance; bins (bin_view.cpp), the Timeline, the Source Browser and the
// monitors reach it through instance(). The decoding itself is media::FrameCache (src/media/link.*).
// All public functions are called on the GUI thread.
class MediaFrames : public QObject {
    Q_OBJECT

public:
    // The first one made becomes instance().
    explicit MediaFrames(QObject* parent = nullptr);
    // Drops the work not yet started and waits for the worker threads.
    ~MediaFrames() override;

    // The application's; null before one exists.
    static MediaFrames* instance();

    // A frame now, decoded if need be. `clipFrame` counts at the clip's edit rate; `size`, if given,
    // is what the picture is scaled to fit. Null for clips without media or offline media.
    QImage frame(const model::BinItem& item, std::int64_t clipFrame, QSize size = {});
    // Whether a clip is linked but its file cannot be read ("Media
    // Offline", p.367-368).
    bool isOffline(const model::BinItem& item);

    // A thumbnail if it is ready; otherwise null, and thumbnailReady()
    // follows once it is decoded. Null without a signal for clips that are not linked or were
    // unlinked. A clip without a picture stays null once decoded.
    QImage thumbnail(const model::BinItem& item, std::int64_t clipFrame, QSize size);
    // Waits for the thumbnails being decoded (tests).
    void waitForThumbnails();
    // During playback: decodes a frame on a thread of its own, in order, so
    // it is ready when shown. Requests beyond a few waiting are dropped.
    void prefetch(const model::BinItem& item, std::int64_t clipFrame, QSize size);

    // Forgets every frame and thumbnail and waits for the work in flight, e.g. after relinking,
    // when the files behind the clips have changed.
    void clear();

signals:
    // A thumbnail asked for by thumbnail() is decoded; `path` is the clip's media file ("path" in
    // BinItem::link). Views repaint and ask again.
    void thumbnailReady(const QString& path);

private:
    // The thumbnail's key: media file, frame and size.
    static QString key(const model::BinItem& item, std::int64_t clipFrame, QSize size);

    media::FrameCache cache_;
    QThreadPool pool_;
    QThreadPool ahead_; // one thread: frames in play order
    std::atomic<int> aheadWaiting_{0};
    std::map<QString, QImage> thumbnails_; // by key(); emptied when it grows past a limit
    std::set<QString> pending_;            // keys being decoded, so each is asked for once
};

} // namespace lumenis::ui
