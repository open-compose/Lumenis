#pragma once

#include "lumenis/media/link.h"
#include "lumenis/model/bin.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QThreadPool>

#include <cstdint>
#include <map>
#include <set>

namespace lumenis::ui {

// The pictures of linked clips for the monitors, bins and the Source
// Browser. Monitors ask for frames as they need them; thumbnails are decoded
// on worker threads and announced when ready.
class MediaFrames : public QObject {
    Q_OBJECT

public:
    explicit MediaFrames(QObject* parent = nullptr);
    ~MediaFrames() override;

    // The application's; null before one exists.
    static MediaFrames* instance();

    // A frame now, decoded if need be. Null for clips without media.
    QImage frame(const model::BinItem& item, std::int64_t clipFrame, QSize size = {});
    // Whether a clip is linked but its file cannot be read ("Media
    // Offline", p.367-368).
    bool isOffline(const model::BinItem& item);

    // A thumbnail if it is ready; otherwise null, and thumbnailReady()
    // follows once it is decoded.
    QImage thumbnail(const model::BinItem& item, std::int64_t clipFrame, QSize size);
    // Waits for the thumbnails being decoded (tests).
    void waitForThumbnails();

    void clear();

signals:
    void thumbnailReady(const QString& path);

private:
    static QString key(const model::BinItem& item, std::int64_t clipFrame, QSize size);

    media::FrameCache cache_;
    QThreadPool pool_;
    std::map<QString, QImage> thumbnails_;
    std::set<QString> pending_;
};

} // namespace lumenis::ui
