// Test patterns (lumenis/media/test_patterns.h).
#include "lumenis/media/test_patterns.h"

#include <QPainter>

#include <array>

namespace lumenis::media {

namespace {

// The seven bars at `level` (0-255): white, yellow, cyan, green, magenta, red, blue.
std::array<QColor, 7> bars(int level) {
    return {QColor(level, level, level), QColor(level, level, 0), QColor(0, level, level), QColor(0, level, 0),
            QColor(level, 0, level),     QColor(level, 0, 0),     QColor(0, 0, level)};
}

void drawRow(QPainter& p, const QList<QColor>& colours, const QList<int>& widths, int total, int y, int height, int width) {
    double x = 0;
    for (int i = 0; i < colours.size(); ++i) {
        const double w = static_cast<double>(width) * widths.at(i) / total;
        p.fillRect(QRectF(x, y, w + 1, height), colours.at(i));
        x += w;
    }
}

} // namespace

QStringList testPatternNames() { return {QStringLiteral("SMPTE_Bars"), QStringLiteral("ColorBars"), QStringLiteral("ColorBars_100")}; }

QImage testPattern(const QString& name, QSize size) {
    if (size.isEmpty() || !testPatternNames().contains(name)) return {};
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter p(&image);
    const int w = size.width();
    const int h = size.height();
    if (name == QLatin1String("ColorBars") || name == QLatin1String("ColorBars_100")) {
        // Full-field bars at 75% (191) or 100% (255); 75% bars keep white at 100% (ours, as the common 100/75 bars).
        const int level = name == QLatin1String("ColorBars") ? 191 : 255;
        auto colours = bars(level);
        colours[0] = QColor(255, 255, 255);
        drawRow(p, {colours.begin(), colours.end()}, {1, 1, 1, 1, 1, 1, 1}, 7, 0, h, w);
        return image;
    }
    // SMPTE bars (SMPTE EG 1, approximated in RGB): the 75% bars over two thirds of the height; a strip of the reverse
    // blue bars; then -I, 100% white, +Q, black and the PLUGE (below black, black, above black), black.
    const int top = h * 2 / 3;
    const int strip = h / 12;
    const auto colours = bars(191);
    drawRow(p, {colours.begin(), colours.end()}, {1, 1, 1, 1, 1, 1, 1}, 7, 0, top, w);
    const QColor black(16, 16, 16);
    drawRow(p, {colours[6], black, colours[4], black, colours[2], black, colours[0]}, {1, 1, 1, 1, 1, 1, 1}, 7, top, strip, w);
    drawRow(p,
            {QColor(0, 33, 76), QColor(255, 255, 255), QColor(50, 0, 106), black, QColor(7, 7, 7), black, QColor(25, 25, 25), black},
            {5, 5, 5, 5, 1, 1, 1, 5}, 28, top + strip, h - top - strip, w);
    return image;
}

} // namespace lumenis::media
