// Stereo Master Clips and the project's Stereoscopic option (stereo_clip.h; Editing Guide p.65-66, p.243).
#include "lumenis/model/stereo_clip.h"

#include <QJsonValue>

#include <algorithm>

namespace lumenis::model {

namespace {

const QString kStereoKey = QStringLiteral("stereo");

QString eyeText(Eye eye) {
    return eye == Eye::Right ? QStringLiteral("Right") : QStringLiteral("Left");
}

} // namespace

QJsonObject StereoClip::toJson() const {
    return QJsonObject{{QStringLiteral("left"), leftLink},
                       {QStringLiteral("right"), rightLink},
                       {QStringLiteral("leftName"), leftName},
                       {QStringLiteral("rightName"), rightName},
                       {QStringLiteral("leftId"), leftId},
                       {QStringLiteral("rightId"), rightId},
                       {QStringLiteral("leftMob"), QString::fromStdString(leftMob.toString())},
                       {QStringLiteral("rightMob"), QString::fromStdString(rightMob.toString())},
                       {QStringLiteral("leadingEye"), eyeText(leadingEye)}};
}

std::optional<StereoClip> StereoClip::fromJson(const QJsonObject& o) {
    StereoClip s;
    s.leftLink = o.value(QStringLiteral("left")).toObject();
    s.rightLink = o.value(QStringLiteral("right")).toObject();
    if (s.leftLink.isEmpty() || s.rightLink.isEmpty()) return std::nullopt;
    s.leftName = o.value(QStringLiteral("leftName")).toString();
    s.rightName = o.value(QStringLiteral("rightName")).toString();
    s.leftId = o.value(QStringLiteral("leftId")).toString();
    s.rightId = o.value(QStringLiteral("rightId")).toString();
    s.leftMob = MobId::fromString(o.value(QStringLiteral("leftMob")).toString().toStdString()).value_or(MobId{});
    s.rightMob = MobId::fromString(o.value(QStringLiteral("rightMob")).toString().toStdString()).value_or(MobId{});
    s.leadingEye = o.value(QStringLiteral("leadingEye")).toString() == QLatin1String("Right") ? Eye::Right : Eye::Left;
    return s;
}

std::optional<StereoClip> stereoClipOf(const BinItem& item) {
    if (item.kind != BinItemKind::MasterClip) return std::nullopt;
    const QJsonValue v = item.sourceSettings.value(kStereoKey);
    return v.isObject() ? StereoClip::fromJson(v.toObject()) : std::nullopt;
}

std::optional<BinItem> makeStereoClip(const BinItem& left, const BinItem& right, QString* error) {
    auto fail = [error](const QString& why) -> std::optional<BinItem> {
        if (error) *error = why;
        return std::nullopt;
    };
    for (const BinItem* eye : {&left, &right}) {
        if (eye->kind != BinItemKind::MasterClip || eye->link.isEmpty()) {
            return fail(QStringLiteral("%1 is not a master clip with media.").arg(eye->name));
        }
        if (isStereoClip(*eye)) return fail(QStringLiteral("%1 is a stereoscopic clip already.").arg(eye->name));
    }
    if (!(left.editRate == right.editRate)) return fail(QStringLiteral("The left and right eye clips have different frame rates."));
    if (left.id == right.id) return fail(QStringLiteral("Choose two different clips for the left and right eyes."));
    BinItem out = left;
    out.id = newItemId();
    out.mobId = MobId::generate();
    out.name = left.name + QStringLiteral(" S3D");
    out.durationFrames = std::min(left.durationFrames, right.durationFrames);
    out.markers = {};
    StereoClip s;
    s.leftLink = left.link;
    s.rightLink = right.link;
    s.leftName = left.name;
    s.rightName = right.name;
    s.leftId = left.id;
    s.rightId = right.id;
    s.leftMob = mobIdOf(left);
    s.rightMob = mobIdOf(right);
    out.sourceSettings.insert(kStereoKey, s.toJson());
    out.fields.insert(QLatin1String(kLeadingEyeHeading), eyeText(s.leadingEye));
    out.link = s.leftLink;
    return out;
}

bool setLeadingEye(BinItem& item, Eye eye) {
    auto s = stereoClipOf(item);
    if (!s) return false;
    s->leadingEye = eye;
    item.sourceSettings.insert(kStereoKey, s->toJson());
    item.fields.insert(QLatin1String(kLeadingEyeHeading), eyeText(eye));
    item.link = eye == Eye::Right ? s->rightLink : s->leftLink;
    return true;
}

EyeSource eyeSource(const StereoClip& stereo, Eye eye) {
    return eye == Eye::Right ? EyeSource{stereo.rightName, stereo.rightId, stereo.rightMob}
                             : EyeSource{stereo.leftName, stereo.leftId, stereo.leftMob};
}

Eye contributingEye(const StereoClip& stereo, S3DContributor contributor) {
    if (contributor == S3DContributor::Left) return Eye::Left;
    if (contributor == S3DContributor::Right) return Eye::Right;
    return stereo.leadingEye;
}

StereoMode stereoModeOf(const QString& text) {
    if (text == QLatin1String("Leading Eye")) return StereoMode::LeadingEye;
    if (text == QLatin1String("Left Eye Only")) return StereoMode::LeftEyeOnly;
    if (text == QLatin1String("Right Eye Only")) return StereoMode::RightEyeOnly;
    if (text == QLatin1String("Side by Side")) return StereoMode::SideBySide;
    if (text == QLatin1String("Over / Under")) return StereoMode::OverUnder;
    if (text == QLatin1String("Full")) return StereoMode::Full;
    return StereoMode::Off;
}

BinItem eyeClip(const BinItem& item, Eye eye) {
    const auto s = stereoClipOf(item);
    if (!s) return item;
    BinItem out = item;
    out.link = eye == Eye::Right ? s->rightLink : s->leftLink;
    return out;
}

Eye shownEye(StereoMode mode, const BinItem& item) {
    if (mode == StereoMode::LeftEyeOnly) return Eye::Left;
    if (mode == StereoMode::RightEyeOnly) return Eye::Right;
    const auto s = stereoClipOf(item);
    return s ? s->leadingEye : Eye::Left;
}

} // namespace lumenis::model
