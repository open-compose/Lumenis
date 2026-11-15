// The Timeline's film track (11.5b; Editing Guide p.593, p.642): "You can use the film track to examine each frame of
// the sequence in a linear display, much as you would when looking at a strand of film on a flatbed". Show Track > Film
// puts a row of frames at the top of the Timeline, as many as fit; Zoom > Show Every Frame zooms so every frame has a
// frame of the film track to itself. The pictures are the shown monitor's (Composer::pictureAt), so "The film track
// displays frames for the topmost video track only" holds as the monitor shows the top track's picture.
#include "lumenis/ui/composer.h"
#include "lumenis/ui/timeline.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

namespace {

constexpr int kFilmMargin = 2; // above and below the frames
constexpr int kFilmGap = 1;    // between frames, the film's frame lines (ours)

} // namespace

bool Timeline::filmTrackShown() const {
    if (!hasShown()) return false;
    return std::any_of(shown().tracks().begin(), shown().tracks().end(),
                       [](const model::Track& t) { return t.kind == model::TrackKind::Video; });
}

void Timeline::setFilmTrack(bool on) {
    display_.filmTrack = on;
    syncScrollBars();
    view_->update();
}

double Timeline::filmFrameWidth() const {
    return std::max(8.0, (trackHeight(QStringLiteral("Film")) - 2 * kFilmMargin) * pictureAspect_);
}

bool Timeline::showEveryFrame() {
    if (!display_.filmTrack || !filmTrackShown()) return false;
    setScale(filmFrameWidth());
    return true;
}

// Each drawn frame stands for the frames up to the next one: every frame when zoomed so far in, else every n-th, where
// n frames are as wide as a film-track frame (ours: the picture at the first of them).
void TimelineView::paintFilmTrack(QPainter& p, const Row& r, std::int64_t firstVisible, std::int64_t lastVisible) const {
    const double header = headerWidth();
    p.fillRect(QRectF(header, r.top, width() - header, r.height), QColor(0x20, 0x20, 0x20));
    const MonitorId monitor = timeline_.showsSource() ? MonitorId::Source : MonitorId::Record;
    Composer& composer = timeline_.composer();
    const model::LoadedClip* loaded = composer.state(monitor).current();
    if (!loaded) return;
    const double frameWidth = timeline_.filmFrameWidth();
    const double scale = std::max(timeline_.scale(), 1e-9);
    const auto step = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(frameWidth / scale - 1e-6)));
    const std::int64_t end = std::min<std::int64_t>(lastVisible + 1, timeline_.shown().duration());
    const int h = r.height - 2 * kFilmMargin;
    const QSize size(std::max(1, static_cast<int>(frameWidth) - kFilmGap), std::max(1, h));
    const QString item = loaded->item.id + QLatin1Char('/') + loaded->item.modified.toString(Qt::ISODateWithMs);
    for (std::int64_t f = std::max<std::int64_t>(0, firstVisible / step * step); f < end; f += step) {
        const QString key = QStringLiteral("%1/%2/%3x%4").arg(item).arg(f).arg(size.width()).arg(size.height());
        QImage* cached = filmFrames_.object(key);
        if (!cached) {
            QImage picture = composer.pictureAt(monitor, f, size);
            if (picture.isNull()) {
                picture = QImage(size, QImage::Format_RGB32);
                picture.fill(Qt::black);
            }
            cached = new QImage(picture);
            filmFrames_.insert(key, cached);
        }
        const double x = xFor(f);
        const double w = std::min(frameWidth, static_cast<double>(step) * scale) - kFilmGap;
        if (w < 1) continue;
        p.drawImage(QRectF(x, r.top + kFilmMargin, w, h), *cached);
    }
}

} // namespace lumenis::ui
