#include "lumenis/media/compositor.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

// Timecode Burn-In (ROADMAP 6.3d1; docs/spec/burn-in.md): "Lets you add timecode displays that update
// in real time, or text annotations, to the video in your sequences" (FX Guide p.463), with up to three
// Displays and a Notes Track (p.505-508). Each display's text comes from the sequence, the source clip
// under it, or the bin's columns; it is drawn over the segment's picture. The parameters are not
// keyframeable (p.505).
namespace lumenis::media {

namespace {

const QString kCategory = QStringLiteral("Timecode Burn-In Effect Parameters");

QJsonObject displayOf(const model::Effect& e, int display) {
    const QString key = display == 0 ? QStringLiteral("Notes") : QStringLiteral("Display %1").arg(display);
    return e.parameters.value(kCategory).toObject().value(key).toObject();
}

// A display's setting, or its value until set (ours, docs/spec/burn-in.md).
QJsonValue setting(const QJsonObject& d, int display, const QString& name) {
    if (d.contains(name)) return d.value(name);
    if (name == QLatin1String("Enable")) return display == 1;
    if (name == QLatin1String("Reader")) return QStringLiteral("Timecode");
    if (name == QLatin1String("Type")) return QStringLiteral("Current");
    if (name == QLatin1String("Display Label")) return true;
    if (name == QLatin1String("Source Track")) return QStringLiteral("Current Track");
    if (name == QLatin1String("X")) return 5;
    if (name == QLatin1String("Y")) return display == 0 ? 5 : 95 - 10 * display;
    if (name == QLatin1String("Font Size")) return 36;
    if (name == QLatin1String("Text")) return display == 0 ? QString() : QStringLiteral("#ffffff");
    if (name == QLatin1String("Text Color")) return QStringLiteral("#ffffff");
    if (name == QLatin1String("Text Opacity")) return 1.0;
    if (name == QLatin1String("Line Weight")) return 1;
    if (name == QLatin1String("Background")) return QStringLiteral("#000000");
    if (name == QLatin1String("Border Opacity")) return 0.5;
    return {};
}

// The label a display begins with when Display Label is on ("Mas TC1 01:00:01:16", p.506). Ours for
// the others.
QString labelFor(const QString& reader, const QString& type) {
    if (reader == QLatin1String("Frames")) return QStringLiteral("Frm");
    if (reader != QLatin1String("Timecode") && reader != QLatin1String("Source Timecode")) return {};
    const QString base = reader == QLatin1String("Timecode") ? QStringLiteral("Mas") : QStringLiteral("Src");
    if (type == QLatin1String("Current")) return base + QStringLiteral(" TC1");
    if (type == QLatin1String("End")) return base + QStringLiteral(" End");
    if (type == QLatin1String("Duration")) return base + QStringLiteral(" Dur");
    if (type == QLatin1String("Mark IN")) return base + QStringLiteral(" IN");
    if (type == QLatin1String("Mark OUT")) return base + QStringLiteral(" OUT");
    if (type == QLatin1String("I/O")) return base + QStringLiteral(" I/O");
    if (type == QLatin1String("Remaining")) return base + QStringLiteral(" Rem");
    if (type == QLatin1String("Absolute")) return base + QStringLiteral(" Abs");
    return base;
}

} // namespace

QJsonValue burnInSetting(const model::Effect& effect, int display, const QString& name) {
    return setting(displayOf(effect, display), display, name);
}

bool isBurnIn(const model::Effect& effect) {
    return effect.category == QLatin1String("Generator") && effect.name == QLatin1String("Timecode Burn-In");
}

QString burnInText(const model::Effect& e, int display, const Composition& c, const QString& track, std::int64_t frame) {
    const QJsonObject d = displayOf(e, display);
    if (display == 0) {
        // "The display itself appears as soon as you type text in the text box" (p.508).
        return d.value(QStringLiteral("Text")).toString();
    }
    if (!setting(d, display, QStringLiteral("Enable")).toBool() || !c.sequence) return {};
    const QString reader = setting(d, display, QStringLiteral("Reader")).toString();
    const QString type = setting(d, display, QStringLiteral("Type")).toString();
    const model::BinItem& seqItem = c.burnIn.sequence;
    const TimecodeFormat seqFormat = model::timecodeFormatOf(seqItem);
    auto tc = [](std::int64_t frames, const TimecodeFormat& format) {
        return QString::fromStdString(Timecode(std::max<std::int64_t>(frames, 0), format).toString());
    };
    // The segment the source readers look at: on the effect's own track, or the one chosen ("Current
    // Track, which refers to the track the Timecode effect is on, or any other video or audio by its
    // track name", p.507).
    QString sourceTrack = setting(d, display, QStringLiteral("Source Track")).toString();
    if (sourceTrack == QLatin1String("Current Track")) sourceTrack = track;
    const auto under = trackFrame(*c.sequence, sourceTrack, frame);
    const model::Segment* segment = under && under->segment && !under->segment->isFiller() ? under->segment : nullptr;
    const std::optional<model::BinItem> clip = segment && c.burnIn.clip ? c.burnIn.clip(*segment) : std::nullopt;
    const auto own = trackFrame(*c.sequence, track, frame);

    QString value;
    if (reader == QLatin1String("Timecode")) {
        // The sequence's timecode, as the tracking displays give it (Editing Guide p.385-387).
        const std::int64_t start = model::startFrameOf(seqItem);
        const std::int64_t duration = c.sequence->duration();
        const std::int64_t in = c.burnIn.markIn.value_or(0);
        const std::int64_t out = c.burnIn.markOut.value_or(duration - 1);
        if (type == QLatin1String("Current")) value = tc(start + frame, seqFormat);
        else if (type == QLatin1String("End")) value = tc(start + duration - 1, seqFormat);
        else if (type == QLatin1String("Duration")) value = tc(duration, seqFormat);
        else if (type == QLatin1String("Mark IN")) value = c.burnIn.markIn ? tc(start + in, seqFormat) : QString();
        else if (type == QLatin1String("Mark OUT")) value = c.burnIn.markOut ? tc(start + out, seqFormat) : QString();
        else if (type == QLatin1String("I/O")) value = tc(out - in + 1, seqFormat);
        else if (type == QLatin1String("Remaining")) value = tc(duration - frame - 1, seqFormat);
        else if (type == QLatin1String("Absolute")) value = tc(frame, seqFormat);
    } else if (reader == QLatin1String("Source Timecode")) {
        // "The timecode of the source clip at the current location" (p.505). Ours for the other Types:
        // the clip's End and Duration; the segment's first and last frames as its marks.
        if (!segment) return {};
        const TimecodeFormat format = clip ? model::timecodeFormatOf(*clip) : seqFormat;
        const std::int64_t start = clip ? model::startFrameOf(*clip) : 0;
        const std::int64_t duration = clip && clip->durationFrames > 0 ? clip->durationFrames : segment->sourceIn + segment->length;
        const std::int64_t at = under->frame;
        if (type == QLatin1String("Current")) value = tc(start + at, format);
        else if (type == QLatin1String("End")) value = tc(start + duration - 1, format);
        else if (type == QLatin1String("Duration")) value = tc(duration, format);
        else if (type == QLatin1String("Mark IN")) value = tc(start + segment->sourceIn, format);
        else if (type == QLatin1String("Mark OUT")) value = tc(start + segment->sourceIn + segment->length - 1, format);
        else if (type == QLatin1String("I/O")) value = tc(segment->length, format);
        else if (type == QLatin1String("Remaining")) value = tc(duration - at - 1, format);
        else if (type == QLatin1String("Absolute")) value = tc(at, format);
    } else if (reader == QLatin1String("Frames")) {
        // "Select Local Frame Count to display the frame number for the current clip. Deselect Local
        // Frame Count to display the frame number for the sequence" (p.507), counted from Start
        // Counting From (p.507).
        const std::int64_t from = setting(d, display, QStringLiteral("Start Counting From")).toInteger(0);
        const bool local = setting(d, display, QStringLiteral("Local Frame Count")).toBool();
        const std::int64_t n = local ? (own ? own->offset : 0) : frame;
        value = QString::number(n + from);
    } else if (reader == QLatin1String("Source Clip Name")) {
        value = segment ? (clip && !clip->name.isEmpty() ? clip->name : segment->name) : QString();
    } else if (reader == QLatin1String("Sequence Name")) {
        value = seqItem.name;
    } else if (reader == QLatin1String("Source Bin Column") || reader == QLatin1String("Sequence Bin Column")) {
        // "The information from the bin column you select" (p.506).
        const QString column = setting(d, display, QStringLiteral("Column Name")).toString();
        const bool source = reader == QLatin1String("Source Bin Column");
        if (source && !clip) return {};
        value = (source ? *clip : seqItem).fields.value(column);
    } else if (reader == QLatin1String("Timeline Clip Text")) {
        // "The comments will appear with the clip ... The effect will not show anything on clips where
        // there are no comments" (p.506): the segment's Timeline Clip Note.
        value = segment ? segment->note : QString();
    }
    // Edgecode, Source Edgecode and Other Counters need film material, which has none here.
    if (value.isEmpty()) return {};
    if (setting(d, display, QStringLiteral("Display Label")).toBool()) {
        const QString label = labelFor(reader, type);
        if (!label.isEmpty()) value = label + QLatin1Char(' ') + value;
    }
    return value;
}

Picture burnInPicture(const model::Effect& e, const Picture& layer, const Composition& c, const QString& track, std::int64_t frame) {
    const int width = layer.width();
    const int height = layer.height();
    if (layer.isNull()) return layer;
    QImage overlay(width, height, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    bool any = false;
    {
        QPainter p(&overlay);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        const double scale = width / 1920.0;
        for (int display = 1; display <= 4; ++display) {
            const int which = display == 4 ? 0 : display; // the Notes Track last, on top
            const QString text = burnInText(e, which, c, track, frame);
            if (text.isEmpty()) continue;
            any = true;
            const QJsonObject d = displayOf(e, which);
            // "Position — Use the X and Y sliders to set the position of the display's anchor point ...
            // (located at the top left corner of the display)"; "Font Size ... in absolute pixels";
            // Text colour and Text Opacity (0 to 1); Line Weight, the outline with Use Text Outline or
            // added thickness without; Background colour and Border Opacity (0 to 1) (p.507-508). Ours:
            // X and Y in percent of the picture; the font size in pixels of a picture 1920 wide.
            const double x = setting(d, which, QStringLiteral("X")).toDouble() / 100.0 * width;
            const double y = setting(d, which, QStringLiteral("Y")).toDouble() / 100.0 * height;
            const double size = std::max(setting(d, which, QStringLiteral("Font Size")).toDouble() * scale, 4.0);
            const QString colourKey = which == 0 ? QStringLiteral("Text Color") : QStringLiteral("Text");
            QColor colour(setting(d, which, colourKey).toString());
            colour.setAlphaF(static_cast<float>(std::clamp(setting(d, which, QStringLiteral("Text Opacity")).toDouble(), 0.0, 1.0)));
            QColor background(setting(d, which, QStringLiteral("Background")).toString());
            background.setAlphaF(static_cast<float>(std::clamp(setting(d, which, QStringLiteral("Border Opacity")).toDouble(), 0.0, 1.0)));
            const double weight = std::max(setting(d, which, QStringLiteral("Line Weight")).toDouble(), 0.0) * scale;
            QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            font.setPixelSize(static_cast<int>(std::lround(size)));
            const QFontMetricsF metrics(font);
            const double pad = size * 0.2;
            const QRectF box(x, y, metrics.horizontalAdvance(text) + 2 * pad, metrics.height() + 2 * pad);
            p.fillRect(box, background);
            QPainterPath path;
            path.addText(QPointF(x + pad, y + pad + metrics.ascent()), font, text);
            if (setting(d, which, QStringLiteral("Use Text Outline")).toBool()) {
                if (weight > 0) p.strokePath(path, QPen(QColor(0, 0, 0, colour.alpha()), weight * 2));
            } else if (weight > 0) {
                p.strokePath(path, QPen(colour, weight));
            }
            p.fillPath(path, colour);
        }
    }
    if (!any) return layer;
    return over(Picture::fromImage(overlay, width, height), layer);
}

} // namespace lumenis::media
