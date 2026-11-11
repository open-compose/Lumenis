// Graphics on import (9.2b; docs/spec/graphics-import.md; Editing Guide p.217-218, p.1196, p.1231): files with an alpha
// channel as Matte Key clips, as the Import setting's Alpha Channel option says, and PICT files, which FFmpeg does not
// read, through our own reader (media/pict.h). importFiles() hands them here.
#include "lumenis/ui/main_window.h"

#include "lumenis/media/graphic_import.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/pict.h"
#include "lumenis/model/matte_key_clip.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/settings.h"

#include <QFileInfo>

namespace lumenis::ui {

media::AlphaHandling MainWindow::importAlphaHandling(const QJsonObject& importSettings) const {
    return media::alphaHandlingOf(importSettings.value(QStringLiteral("alphaChannel")).toString());
}

std::optional<model::BinItem> MainWindow::importMatteKeyClip(const model::BinItem& clip, const media::AlphaSource& source,
                                                             const QJsonObject& importSettings, media::MediaCreation creation,
                                                             BinDocument& doc, QString* error) {
    const media::AlphaHandling handling = importAlphaHandling(importSettings);
    const bool dilate = importSettings.value(QStringLiteral("dilateFill")).toBool();
    const Rational rate = linkOptions().editRate;
    // "Importing an image with alpha channel creates a matte key effect as a single frame" (p.1196) that lasts the
    // Frame Import Duration; a movie's frames one for one.
    const bool still = source.frames <= 1;
    const std::int64_t seconds = std::max(importSettings.value(QStringLiteral("frameImportDuration")).toInt(), 1);
    const std::int64_t length = still ? std::max<std::int64_t>(1, seconds * rate.num() / std::max<std::int64_t>(rate.den(), 1))
                                      : source.frames;
    const std::function<bool(double)> progress = creation.progress;
    // The fill, then the matte, as precomputes (RenderedEffect clips, hidden by the bin's default display, p.243).
    creation.progress = [&](double done) { return !progress || progress(done / 2); };
    const auto fill = media::renderedVideoClip(
        clip.name + QStringLiteral(".fill"), rate, source.frames,
        [&](std::int64_t f) { return media::fillPicture(source.frame(f), dilate); }, creation, model::BinItemKind::RenderedEffect,
        error);
    if (!fill) return std::nullopt;
    creation.progress = [&](double done) { return !progress || progress(0.5 + done / 2); };
    const auto matte = media::renderedVideoClip(
        clip.name + QStringLiteral(".matte"), rate, source.frames,
        [&](std::int64_t f) { return media::mattePicture(source.frame(f), handling); }, creation, model::BinItemKind::RenderedEffect,
        error);
    if (!matte) return std::nullopt;
    doc.addItem(*fill);
    doc.addItem(*matte);
    model::MatteKeyMedia media;
    media.binId = doc.nodeId();
    media.fill = fill->id;
    media.matte = matte->id;
    media.frames = source.frames;
    model::BinItem item = model::matteKeyClip(clip.name, rate, length, media);
    // The columns that say where it came from, for Reveal File and the bin (and the Import settings it used).
    for (const char* heading : {"Source File", "Source Path", "Drive"}) {
        const QString key = QString::fromLatin1(heading);
        if (clip.fields.contains(key)) item.fields.insert(key, clip.fields.value(key));
    }
    item.sourceSettings.insert(QStringLiteral("import"), importSettings);
    doc.addItem(item);
    return item;
}

std::optional<model::BinItem> MainWindow::importPictFile(const QString& path, const QJsonObject& importSettings,
                                                         media::MediaCreation creation, BinDocument& doc, QString* error) {
    const QImage picture = media::readPict(path);
    if (picture.isNull()) {
        if (error) *error = tr("%1 is not a PICT picture that can be read.").arg(QFileInfo(path).fileName());
        return std::nullopt;
    }
    const QFileInfo file(path);
    model::BinItem clip;
    clip.name = file.completeBaseName();
    clip.fields.insert(QStringLiteral("Source File"), file.fileName());
    clip.fields.insert(QStringLiteral("Source Path"), file.absolutePath());
    // With alpha (p.1231: PICT "2-, 4-, 8-, 16-, and 32-bit", alpha "Yes"), a Matte Key clip unless the alpha is ignored.
    if (importAlphaHandling(importSettings) != media::AlphaHandling::Ignore) {
        if (const auto source = media::alphaSourceOf(path)) return importMatteKeyClip(clip, *source, importSettings, creation, doc, error);
    }
    // Otherwise a master clip of the picture, as long as the Frame Import Duration.
    const Rational rate = linkOptions().editRate;
    const std::int64_t seconds = std::max(importSettings.value(QStringLiteral("frameImportDuration")).toInt(), 1);
    const std::int64_t length = std::max<std::int64_t>(1, seconds * rate.num() / std::max<std::int64_t>(rate.den(), 1));
    const QImage opaque = picture.convertToFormat(QImage::Format_RGB32);
    auto made = media::renderedVideoClip(clip.name, rate, length, [&](std::int64_t) { return opaque; }, creation,
                                         model::BinItemKind::MasterClip, error);
    if (!made) return std::nullopt;
    for (auto it = clip.fields.constBegin(); it != clip.fields.constEnd(); ++it) made->fields.insert(it.key(), it.value());
    made->sourceSettings.insert(QStringLiteral("import"), importSettings);
    doc.addItem(*made);
    return made;
}

} // namespace lumenis::ui
