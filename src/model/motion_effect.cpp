#include "lumenis/model/motion_effect.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

// Traditional motion effect clips (motion_effect.h).
namespace lumenis::model {

QString fieldMotionName(FieldMotion f) {
    switch (f) {
    case FieldMotion::DuplicatedField: return QStringLiteral("duplicated");
    case FieldMotion::BothFields: return QStringLiteral("both");
    case FieldMotion::InterpolatedField: return QStringLiteral("interpolated");
    case FieldMotion::VtrStyle: return QStringLiteral("vtr");
    }
    return QStringLiteral("duplicated");
}

FieldMotion fieldMotionFromName(const QString& name) {
    if (name == QLatin1String("both")) return FieldMotion::BothFields;
    if (name == QLatin1String("interpolated")) return FieldMotion::InterpolatedField;
    if (name == QLatin1String("vtr")) return FieldMotion::VtrStyle;
    return FieldMotion::DuplicatedField;
}

// Strobe holds the frame at each multiple of `strobe`; the speed then steps through the source, rounded
// toward the start so that a slow clip holds each source frame for whole frames ("Media Composer creates
// slow-motion effects by duplicating frames in the original media. It creates fast-motion effects by
// eliminating frames", p.168). The small nudge keeps exact products (0.5 x 4 = 2) from falling a frame
// short through rounding.
std::int64_t MotionEffect::sourceFrame(std::int64_t frame) const {
    const std::int64_t held = strobe > 1 ? frame - frame % strobe : frame;
    const double moved = static_cast<double>(held) * speed;
    const double nudge = moved >= 0 ? 1e-6 : -1e-6;
    return sourceStart + static_cast<std::int64_t>(std::trunc(moved + nudge));
}

std::optional<MotionEffect> motionEffectOf(const BinItem& item) {
    if (item.kind != BinItemKind::MotionEffect) return std::nullopt;
    const QJsonObject m = item.composition.value(QStringLiteral("motion")).toObject();
    if (m.isEmpty()) return std::nullopt;
    MotionEffect out;
    out.sourceBin = m.value(QStringLiteral("bin")).toString();
    out.sourceClip = m.value(QStringLiteral("clip")).toString();
    out.sourceStart = static_cast<std::int64_t>(m.value(QStringLiteral("start")).toDouble());
    out.speed = m.value(QStringLiteral("speed")).toDouble(1.0);
    out.strobe = std::max(1, m.value(QStringLiteral("strobe")).toInt(1));
    out.field = fieldMotionFromName(m.value(QStringLiteral("field")).toString());
    return out;
}

void setMotionEffect(BinItem& item, const MotionEffect& motion) {
    item.composition.insert(QStringLiteral("motion"),
                            QJsonObject{{QStringLiteral("bin"), motion.sourceBin},
                                        {QStringLiteral("clip"), motion.sourceClip},
                                        {QStringLiteral("start"), static_cast<double>(motion.sourceStart)},
                                        {QStringLiteral("speed"), motion.speed},
                                        {QStringLiteral("strobe"), motion.strobe},
                                        {QStringLiteral("field"), fieldMotionName(motion.field)}});
}

double motionFps(double speed, const Rational& rate) {
    return speed * rate.toDouble();
}

namespace {

// A new motion effect clip of `source` (picture only, at its rate, kept with the bin's other clips).
BinItem baseClip(const BinItem& source, const QString& name, std::int64_t duration, const QDateTime& now) {
    BinItem item;
    item.id = newItemId();
    item.kind = BinItemKind::MotionEffect;
    item.name = name;
    item.created = now;
    item.modified = now;
    item.editRate = source.editRate;
    item.durationFrames = std::max<std::int64_t>(duration, 1);
    item.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1"));
    return item;
}

// A rate as the clip name shows it: up to two decimals, no trailing zeros.
QString fpsText(double fps) {
    QString text = QString::number(std::abs(fps) < 0.005 ? 0.0 : fps, 'f', 2);
    while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.')))) text.chop(1);
    return text;
}

} // namespace

BinItem freezeFrameClip(const BinItem& source, const QString& sourceBin, std::int64_t frame, std::int64_t duration,
                        FieldMotion field, const QDateTime& now) {
    BinItem item = baseClip(source, source.name + QStringLiteral(" FF"), duration, now);
    MotionEffect m;
    m.sourceBin = sourceBin;
    m.sourceClip = source.id;
    m.sourceStart = frame;
    m.speed = 0.0;
    m.field = field;
    setMotionEffect(item, m);
    return item;
}

std::int64_t motionEffectLength(std::int64_t in, std::int64_t out, double speed) {
    const std::int64_t length = std::max<std::int64_t>(out - in + 1, 1);
    if (std::abs(speed) < 1e-9) return length;
    return std::max<std::int64_t>(std::llround(static_cast<double>(length) / std::abs(speed)), 1);
}

BinItem motionEffectClip(const BinItem& source, const QString& sourceBin, std::int64_t in, std::int64_t out, double speed,
                         int strobe, FieldMotion field, const QDateTime& now) {
    QString name = source.name;
    if (std::abs(speed - 1.0) > 1e-9) name += QStringLiteral(" (%1FPS)").arg(fpsText(motionFps(speed, source.editRate)));
    if (strobe > 1) {
        name += std::abs(speed - 1.0) > 1e-9 ? QStringLiteral(" (Strobe: %1)").arg(strobe) : QStringLiteral(" Strobe (%1)").arg(strobe);
    }
    BinItem item = baseClip(source, name, motionEffectLength(in, out, speed), now);
    MotionEffect m;
    m.sourceBin = sourceBin;
    m.sourceClip = source.id;
    m.sourceStart = speed < 0 ? out : in;
    m.speed = speed;
    m.strobe = std::max(strobe, 1);
    m.field = field;
    setMotionEffect(item, m);
    return item;
}

} // namespace lumenis::model
