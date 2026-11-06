// The SubCap effect's picture (lumenis/media/compositor.h; docs/spec/subcap.md; FX Guide p.378-388, p.501-504).
#include "lumenis/media/compositor.h"

#include "lumenis/model/subcap.h"

#include <QFont>
#include <QFontMetricsF>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace lumenis::media {

QJsonValue subcapDefault(const QString& name) {
    // Ours where the reference gives only ranges: white text in a 48-pixel sans font, centred, on a half-opaque black
    // box anchored at its bottom centre 120 pixels above the bottom of a 1920 x 1080 frame.
    static const QHash<QString, QJsonValue> defaults{
        {QStringLiteral("Bypass Effect"), false},
        {QStringLiteral("Color"), QStringLiteral("#ffffff")},
        {QStringLiteral("Opacity"), 1.0},
        {QStringLiteral("Font Face"), QStringLiteral("Sans Serif")},
        {QStringLiteral("Font Size"), 48},
        {QStringLiteral("Line Weight"), 0},
        {QStringLiteral("Outline Weight"), 2},
        {QStringLiteral("Outline"), QStringLiteral("No Outline")},
        {QStringLiteral("Text Appearance: Color"), QStringLiteral("#000000")},
        {QStringLiteral("Text Shadow"), QStringLiteral("No Shadow")},
        {QStringLiteral("Radius"), 0.2},
        {QStringLiteral("Angle"), 0.8},
        {QStringLiteral("Text Alignment"), QStringLiteral("Center")},
        {QStringLiteral("Box Appearance: Color"), QStringLiteral("#000000")},
        {QStringLiteral("Box Appearance: Opacity"), 0.5},
        {QStringLiteral("Box Style"), QStringLiteral("One Box")},
        {QStringLiteral("Anchor Box to"), QStringLiteral("Anchor to Bottom of Text")},
        {QStringLiteral("Justify Anchor"), QStringLiteral("Anchor Center")},
        {QStringLiteral("X"), 0},
        {QStringLiteral("Y"), -420},
        {QStringLiteral("Box Width"), QStringLiteral("Each row as wide as needed")},
        {QStringLiteral("Box Width thumbwheel"), 1600},
        {QStringLiteral("Number of Rows of Text"), QStringLiteral("As Many Rows as Needed")},
        {QStringLiteral("Row Count"), 2},
        {QStringLiteral("Vertical"), 8},
        {QStringLiteral("Horizontal"), 16},
        {QStringLiteral("Row Spacing"), 0},
        {QString::fromLatin1(model::kCaptionTextKey), QString()},
    };
    return defaults.value(name);
}

namespace {

QJsonValue value(const QJsonObject& p, const QString& name) {
    const QJsonValue v = p.value(name);
    return v.isUndefined() || v.isNull() ? subcapDefault(name) : v;
}

QColor colour(const QJsonObject& p, const QString& name, double opacity) {
    QColor c(value(p, name).toString());
    if (!c.isValid()) c = Qt::white;
    c.setAlphaF(static_cast<float>(std::clamp(opacity, 0.0, 1.0)));
    return c;
}

// The rows of `text` for a box `width` pixels wide (0: no limit): broken at line breaks, and with Fixed Width (Wrap) at
// spaces to fit.
QStringList rowsOf(const QString& text, const QFontMetricsF& metrics, double width, bool wrap) {
    QStringList out;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        if (!wrap || width <= 0 || metrics.horizontalAdvance(line) <= width) {
            out << line;
            continue;
        }
        QString row;
        for (const QString& word : line.split(QLatin1Char(' '))) {
            const QString tried = row.isEmpty() ? word : row + QLatin1Char(' ') + word;
            if (!row.isEmpty() && metrics.horizontalAdvance(tried) > width) {
                out << row;
                row = word;
            } else {
                row = tried;
            }
        }
        out << row;
    }
    return out;
}

} // namespace

Picture subcapPicture(const model::Effect& e, const Picture& layer) {
    if (layer.isNull()) return layer;
    const QJsonObject p = e.parameters.value(QLatin1String(model::kSubCapCategory)).toObject();
    // "Bypass Effect: Turns the display of the subtitle on or off" (p.501).
    if (value(p, QStringLiteral("Bypass Effect")).toBool()) return layer;
    const QString text = model::captionText(e);
    if (text.trimmed().isEmpty()) return layer;
    const int width = layer.width();
    const int height = layer.height();
    const double scale = width / 1920.0;

    // Text Appearance (p.501-502): the font, its colour and opacity, outline, shadow and alignment.
    QFont font(value(p, QStringLiteral("Font Face")).toString());
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(std::clamp(value(p, QStringLiteral("Font Size")).toDouble(), 0.0, 999.0) * scale))));
    const QFontMetricsF metrics(font);
    const QColor textColour = colour(p, QStringLiteral("Color"), value(p, QStringLiteral("Opacity")).toDouble());
    const QColor outlineColour = colour(p, QStringLiteral("Text Appearance: Color"), static_cast<double>(textColour.alphaF()));
    const QColor boxColour = colour(p, QStringLiteral("Box Appearance: Color"), value(p, QStringLiteral("Box Appearance: Opacity")).toDouble());
    const bool outline = value(p, QStringLiteral("Outline")).toString() == QLatin1String("Basic Outline");
    const double lineWeight = std::clamp(value(p, QStringLiteral("Line Weight")).toDouble(), 0.0, 999.0) * scale;
    const double outlineWeight = std::clamp(value(p, QStringLiteral("Outline Weight")).toDouble(), 0.0, 999.0) * scale;
    const QString align = value(p, QStringLiteral("Text Alignment")).toString();

    // Box Appearance (p.502-504): the rows, the boxes and the anchor.
    const QString widthMode = value(p, QStringLiteral("Box Width")).toString();
    const double fixedWidth = std::clamp(value(p, QStringLiteral("Box Width thumbwheel")).toDouble(), 0.0, 2000.0) * scale;
    const bool fixed = widthMode.startsWith(QLatin1String("Fixed Width"));
    const double hpad = std::clamp(value(p, QStringLiteral("Horizontal")).toDouble(), 0.0, 999.0) * scale;
    const double vpad = std::clamp(value(p, QStringLiteral("Vertical")).toDouble(), 0.0, 999.0) * scale;
    QStringList rows = rowsOf(text, metrics, fixed ? fixedWidth - 2 * hpad : 0, widthMode == QLatin1String("Fixed Width (Wrap)"));
    // "Exactly N Rows ... If there is not enough text to fill the number of rows, one or more empty rows display";
    // "At Most N Rows" keeps only that many.
    const QString rowMode = value(p, QStringLiteral("Number of Rows of Text")).toString();
    const int rowCount = std::clamp(value(p, QStringLiteral("Row Count")).toInt(), 1, 25);
    if (rowMode != QLatin1String("As Many Rows as Needed")) {
        while (rows.size() > rowCount) rows.removeLast();
        if (rowMode == QLatin1String("Exactly N Rows")) {
            while (rows.size() < rowCount) rows << QString();
        }
    }
    const double rowHeight = metrics.height() + std::clamp(value(p, QStringLiteral("Row Spacing")).toDouble(), -10.0, 50.0) * scale;
    double widest = 0;
    for (const QString& r : rows) widest = std::max(widest, metrics.horizontalAdvance(r));
    auto rowWidth = [&](const QString& r) {
        if (fixed) return fixedWidth;
        if (widthMode == QLatin1String("Each row as wide as widest")) return widest + 2 * hpad;
        return metrics.horizontalAdvance(r) + 2 * hpad;
    };
    const double boxWidth = fixed ? fixedWidth : widest + 2 * hpad;
    const double boxHeight = static_cast<double>(rows.size()) * rowHeight + 2 * vpad;
    // "Anchor Position ... Values range from -999 to +999, where 0 is the center of the screen" (ours: pixels, Y up).
    const double ax = width / 2.0 + value(p, QStringLiteral("X")).toDouble() * scale;
    const double ay = height / 2.0 - value(p, QStringLiteral("Y")).toDouble() * scale;
    const QString justify = value(p, QStringLiteral("Justify Anchor")).toString();
    const double left = justify == QLatin1String("Anchor Left") ? ax : justify == QLatin1String("Anchor Right") ? ax - boxWidth : ax - boxWidth / 2;
    const double top = value(p, QStringLiteral("Anchor Box to")).toString() == QLatin1String("Anchor to Top of Text") ? ay : ay - boxHeight;
    const QRectF box(left, top, boxWidth, boxHeight);

    QImage overlay(width, height, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    {
        QPainter painter(&overlay);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        // Fixed Width (Clip): "Caption text overflows the box and is not visible when it exceeds the width of the box".
        if (widthMode == QLatin1String("Fixed Width (Clip)")) painter.setClipRect(box);
        const bool separate = value(p, QStringLiteral("Box Style")).toString() == QLatin1String("Separate Boxes for Each Row");
        if (!separate) painter.fillRect(box, boxColour);
        const QString shadowMode = value(p, QStringLiteral("Text Shadow")).toString();
        QPointF shadowOffset;
        if (shadowMode == QLatin1String("Standard Shadow")) shadowOffset = QPointF(3, 3) * scale;
        if (shadowMode == QLatin1String("Custom Shadow")) {
            // "Radius controls the shadow offset ... 0 to 1"; "Angle ... 0 and 6.3 place the shadow directly to the right
            // of the text and 3.1 ... directly to the left" (ours: 20 pixels at 1, angles turning downwards).
            const double r = std::clamp(value(p, QStringLiteral("Radius")).toDouble(), 0.0, 1.0) * 20 * scale;
            const double a = value(p, QStringLiteral("Angle")).toDouble();
            shadowOffset = QPointF(std::cos(a), std::sin(a)) * r;
        }
        // "The shadow color is calculated automatically based on the hue and opacity set for the text."
        QColor shadow = textColour.darker(400);
        shadow.setAlphaF(textColour.alphaF() * 0.7f);
        for (int i = 0; i < rows.size(); ++i) {
            const QString& row = rows.at(i);
            const double y = top + vpad + static_cast<double>(i) * rowHeight;
            const double rw = rowWidth(row);
            double rowLeft = left;
            if (separate) {
                // Each row's box placed in the whole box as the text is aligned.
                rowLeft = align == QLatin1String("Left") ? left : align == QLatin1String("Right") ? left + boxWidth - rw : left + (boxWidth - rw) / 2;
                if (!row.isEmpty()) painter.fillRect(QRectF(rowLeft, y - vpad, rw, rowHeight + 2 * vpad), boxColour);
            }
            const double textWidth = metrics.horizontalAdvance(row);
            const double inner = separate ? rw : boxWidth;
            const double x = (align == QLatin1String("Left")    ? rowLeft + hpad
                              : align == QLatin1String("Right") ? rowLeft + inner - hpad - textWidth
                                                                : rowLeft + (inner - textWidth) / 2);
            QPainterPath path;
            path.addText(QPointF(x, y + metrics.ascent()), font, row);
            if (!shadowOffset.isNull()) painter.fillPath(path.translated(shadowOffset), shadow);
            // Basic Outline: Outline Weight outside the characters, Line Weight into them (drawn as a wider outline,
            // ours); No Outline: Line Weight thickens the characters.
            if (outline && outlineWeight + lineWeight > 0) painter.strokePath(path, QPen(outlineColour, 2 * (outlineWeight + lineWeight), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (!outline && lineWeight > 0) painter.strokePath(path, QPen(textColour, lineWeight, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.fillPath(path, textColour);
        }
    }
    return over(Picture::fromImage(overlay, width, height), layer);
}

} // namespace lumenis::media
