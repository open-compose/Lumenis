// Graphics on import (9.2b; docs/spec/graphics-import.md; Editing Guide p.217-218, p.1196, p.1231): files with an alpha
// channel as Matte Key clips, as the Import setting's Alpha Channel option says, and PICT files, which FFmpeg does not
// read, through our own reader (media/pict.h); and audio CD tracks Windows shows as ".cda" files, read off the disc by an
// audio CD provider (9.2d, docs/spec/audio-cd.md); and multilayer Photoshop files as a clip a layer and a sequence of them
// (9.2e, p.218-221). importFiles() hands them here.
#include "lumenis/ui/main_window.h"

#include "lumenis/ext/audio_cd.h"
#include "lumenis/media/audio_cd.h"
#include "lumenis/media/graphic_import.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/pict.h"
#include "lumenis/media/psd.h"
#include "lumenis/model/matte_key_clip.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/tool_windows.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>

namespace lumenis::ui {

media::AlphaHandling MainWindow::importAlphaHandling(const QJsonObject& importSettings) const {
    return media::alphaHandlingOf(importSettings.value(QStringLiteral("alphaChannel")).toString());
}

std::optional<model::BinItem> MainWindow::importMatteKeyClip(const model::BinItem& clip, const media::AlphaSource& source,
                                                             const QJsonObject& importSettings, media::MediaCreation creation,
                                                             BinDocument& doc, QString* error, double level) {
    media::AlphaHandling handling = importAlphaHandling(importSettings);
    // A Photoshop layer's transparency is its own, not an alpha channel the setting may ignore (p.219: "Media Composer
    // imports each layer as an individual matte key with alpha channel"); kept as Photoshop keeps it, white = opaque.
    if (handling == media::AlphaHandling::Ignore) handling = media::AlphaHandling::Invert;
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
    model::BinItem item = model::matteKeyClip(clip.name, rate, length, media, level);
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

MainWindow::PsdLayersChoice MainWindow::askPsdLayersChoice(const QStringList& files) {
    if (psdChoiceHook_) return psdChoiceHook_(files);
    // The message box of p.221, step 4: Sequence of Layers, Flattened Image or Select Layers.
    QMessageBox box(QMessageBox::Question, tr("Import"),
                    files.size() == 1 ? tr("%1 has layers. Import it as:").arg(QFileInfo(files.first()).fileName())
                                      : tr("%n of the files have layers. Import them as:", "", static_cast<int>(files.size())),
                    QMessageBox::NoButton, this);
    QPushButton* sequence = box.addButton(tr("Sequence of Layers"), QMessageBox::AcceptRole);
    QPushButton* flattened = box.addButton(tr("Flattened Image"), QMessageBox::AcceptRole);
    QPushButton* select = box.addButton(tr("Select Layers"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(sequence);
    box.exec();
    if (box.clickedButton() == sequence) return PsdLayersChoice::SequenceOfLayers;
    if (box.clickedButton() == flattened) return PsdLayersChoice::FlattenedImage;
    if (box.clickedButton() == select) return PsdLayersChoice::SelectLayers;
    return PsdLayersChoice::Cancel;
}

std::optional<QList<int>> MainWindow::askPsdLayers(const QString& file, const QStringList& layers) {
    if (psdSelectHook_) return psdSelectHook_(file, layers);
    // The Select Layers dialog (p.221, step 5): the file's layers, top first as Photoshop lists them, all selected.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Select Layers"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QFileInfo(file).fileName(), &dialog));
    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (qsizetype i = layers.size() - 1; i >= 0; --i) {
        auto* row = new QListWidgetItem(layers.at(i), list);
        row->setData(Qt::UserRole, static_cast<int>(i));
        row->setSelected(true);
    }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    QList<int> chosen;
    for (const QListWidgetItem* row : list->selectedItems()) chosen << row->data(Qt::UserRole).toInt();
    std::sort(chosen.begin(), chosen.end());
    return chosen;
}

std::optional<model::BinItem> MainWindow::linkedMatteKeyClip(const model::BinItem& linked, BinDocument& doc) {
    const QString option = settings_ ? settings_->text(QStringLiteral("Link"), QStringLiteral("alphaChannel")) : QString();
    const media::AlphaHandling handling = media::alphaHandlingOf(option);
    if (handling == media::AlphaHandling::Ignore) return std::nullopt;
    const auto link = media::linkOf(linked);
    if (!link || link->videoStream < 0 || !media::alphaSourceOf(link->path)) return std::nullopt;
    // The linked clip itself, which the Matte Key clip plays, kept beside it as precomputes are (hidden by the bin's
    // default display, p.243; ours).
    model::BinItem source = linked;
    source.name = linked.name + QStringLiteral(".link");
    source.kind = model::BinItemKind::RenderedEffect;
    doc.addItem(source);
    model::MatteKeyMedia media;
    media.binId = doc.nodeId();
    media.fill = source.id;
    media.matte = source.id;
    media.frames = std::max<std::int64_t>(linked.durationFrames, 1);
    media.linkedAlpha = handling == media::AlphaHandling::Invert ? QStringLiteral("invert") : QStringLiteral("noInvert");
    model::BinItem item = model::matteKeyClip(linked.name, linked.editRate, linked.durationFrames, media);
    for (const char* heading : {"Source File", "Source Path", "Drive"}) {
        const QString key = QString::fromLatin1(heading);
        if (linked.fields.contains(key)) item.fields.insert(key, linked.fields.value(key));
    }
    doc.addItem(item);
    return item;
}

QStringList MainWindow::importPsdLayers(const QString& path, const media::PsdFile& file, const QList<int>& chosen,
                                        const QJsonObject& importSettings, media::MediaCreation creation, BinDocument& doc) {
    const QList<media::PsdLayer> layers = media::importableLayers(file);
    const QFileInfo info(path);
    const Rational rate = linkOptions().editRate;
    const std::int64_t seconds = std::max(importSettings.value(QStringLiteral("frameImportDuration")).toInt(), 1);
    const std::int64_t length = std::max<std::int64_t>(1, seconds * rate.num() / std::max<std::int64_t>(rate.den(), 1));
    const std::function<bool(double)> progress = creation.progress;
    QStringList ids;
    QList<QList<model::Segment>> tracks; // bottom to top, each layer's material for the sequence
    for (int n = 0; n < chosen.size(); ++n) {
        const int index = chosen.at(n);
        if (index < 0 || index >= layers.size()) continue;
        const media::PsdLayer& layer = layers.at(index);
        creation.progress = [&](double done) { return !progress || progress((n + done) / static_cast<double>(chosen.size())); };
        model::BinItem clip;
        clip.name = layer.name.isEmpty() ? tr("Layer %1").arg(index + 1) : layer.name;
        clip.fields.insert(QStringLiteral("Source File"), info.fileName());
        clip.fields.insert(QStringLiteral("Source Path"), info.absolutePath());
        const QImage picture = media::psdLayerPicture(file, layer);
        QString error;
        std::optional<model::BinItem> made;
        if (!layer.transparency) {
            // "The graphic uses a background image, so the system creates the background image as a master clip" (p.219).
            const QImage opaque = picture.convertToFormat(QImage::Format_RGB32);
            made = media::renderedVideoClip(clip.name, rate, length, [&](std::int64_t) { return opaque; }, creation,
                                            model::BinItemKind::MasterClip, &error);
            if (made) {
                for (auto it = clip.fields.constBegin(); it != clip.fields.constEnd(); ++it) made->fields.insert(it.key(), it.value());
                made->sourceSettings.insert(QStringLiteral("import"), importSettings);
                doc.addItem(*made);
                model::Segment s;
                s.clipId = made->id;
                s.sourceMob = made->mobId;
                s.binId = doc.nodeId();
                s.name = made->name;
                s.sourceTrack = QStringLiteral("V1");
                s.length = length;
                tracks << QList<model::Segment>{s};
            }
        } else {
            // Each other layer, hidden ones too, a matte key: "Importing converts Opacity to Foreground level" (p.220).
            media::AlphaSource source;
            source.frames = 1;
            source.rate = rate;
            source.size = file.size;
            source.frame = [picture](std::int64_t) { return picture; };
            made = importMatteKeyClip(clip, source, importSettings, creation, doc, &error, layer.opacity * 100.0 / 255.0);
            if (made) {
                const model::Sequence keyed = model::Sequence::fromItem(*made);
                if (const model::Track* v1 = keyed.track(QStringLiteral("V1"))) tracks << v1->segments;
            }
        }
        if (!made) {
            reportFailure(this, tr("Import: %1: %2: %3").arg(info.fileName(), clip.name, error));
            if (creation.progress && !creation.progress(1.0)) break;
            continue;
        }
        ids << made->id;
    }
    if (tracks.isEmpty()) return ids;
    // "Media Composer creates a sequence with each layer on a separate video track ... This sequence preserves the names
    // and order of the layers" (p.219); past 24 tracks the layers are only in the bin (p.221).
    model::BinItem item = model::newSequence(doc.bin(), rate, QDateTime::currentDateTimeUtc());
    model::Sequence sequence = model::Sequence::fromItem(item);
    for (int t = 0; t < std::min<int>(static_cast<int>(tracks.size()), kMaxPsdTracks); ++t) {
        sequence.ensureTrack(QStringLiteral("V%1").arg(t + 1))->segments = tracks.at(t);
    }
    item.name = info.completeBaseName();
    sequence.storeIn(item);
    item.fields.insert(QStringLiteral("Source File"), info.fileName());
    item.fields.insert(QStringLiteral("Source Path"), info.absolutePath());
    doc.addItem(item);
    ids << item.id;
    return ids;
}

QHash<QString, QString> MainWindow::readCdTracks(const QStringList& cdaPaths, const QString& folder) {
    QHash<QString, QString> out;
    if (cdaPaths.isEmpty()) return out;
    const auto readers = providers_.usable<ext::AudioCdProvider>();
    QProgressDialog progress(tr("Reading the CD..."), tr("Cancel"), 0, 1000, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    for (int i = 0; i < cdaPaths.size(); ++i) {
        const QString& cda = cdaPaths.at(i);
        std::shared_ptr<ext::AudioCdProvider> reader;
        for (const auto& r : readers) {
            if (r->readsTrack(cda.toStdString())) {
                reader = r;
                break;
            }
        }
        if (!reader) {
            reportFailure(this, 
                tr("Import: %1: no reader of audio CDs on this system can read this track.").arg(QFileInfo(cda).fileName()));
            continue;
        }
        const QString wav = QDir::cleanPath(QDir(folder).filePath(QFileInfo(cda).completeBaseName() + QStringLiteral(".wav")));
        progress.setLabelText(tr("Reading %1 (%2 of %3)").arg(QFileInfo(cda).fileName()).arg(i + 1).arg(cdaPaths.size()));
        std::string error;
        const bool ok = reader->readTrack(
            cda.toStdString(), wav.toStdString(),
            [&](double done) {
                progress.setValue(static_cast<int>((i + done) / static_cast<double>(cdaPaths.size()) * 1000));
                QCoreApplication::processEvents();
                return !progress.wasCanceled();
            },
            &error);
        if (!ok) {
            reportFailure(this, tr("Import: %1: %2").arg(QFileInfo(cda).fileName(), QString::fromStdString(error)));
            if (progress.wasCanceled()) break;
            continue;
        }
        out.insert(wav, cda);
    }
    return out;
}

} // namespace lumenis::ui
