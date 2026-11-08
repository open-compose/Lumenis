// The Source Settings dialog's FrameFlex tab and its top viewer (Editing Guide p.527-535; 8.5b1,
// docs/spec/frameflex.md). The fields and the viewer edit one model::SpatialAdapter; the bottom viewer shows it through
// media::spatialAdaptedImage; Apply hands it to MainWindow for the selected clips.
#include "lumenis/ui/source_settings_dialog.h"

#include "lumenis/media/color_transform.h"
#include "lumenis/media/frame_flex.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>
#include <numbers>

namespace lumenis::ui {

namespace {

// The box's outline colours (p.530, "Set color of framing box outline in the viewer"; ours: these).
const QList<std::pair<const char*, const char*>> kColors{
    {"Yellow", "#ffd200"}, {"Red", "#ff3030"}, {"Green", "#30d040"}, {"Blue", "#3080ff"}, {"White", "#ffffff"}};

QPointF centreOf(const QPolygonF& p) {
    QPointF c;
    for (const QPointF& v : p) c += v;
    return p.isEmpty() ? c : c / static_cast<double>(p.size());
}

double distance(QPointF a, QPointF b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

QDoubleSpinBox* spin(const QString& name, double min, double max, int decimals, double step) {
    auto* s = new QDoubleSpinBox;
    s->setObjectName(name);
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setKeyboardTracking(false);
    return s;
}

} // namespace

// ---------------------------------------------------------------- FramingViewer

FramingViewer::FramingViewer(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("framingViewer"));
    setMinimumSize(240, 135);
    setMouseTracking(false);
}

void FramingViewer::setFrame(const QImage& frame) {
    frame_ = frame;
    update();
}

void FramingViewer::setFrameFlex(const model::FrameFlex& flex) {
    flex_ = flex;
    update();
}

QRectF FramingViewer::imageRect() const {
    if (frame_.isNull()) return QRectF(rect());
    const QSizeF s = QSizeF(frame_.size()).scaled(QSizeF(size()) - QSizeF(16, 16), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

QPointF FramingViewer::toWidget(QPointF p) const {
    const QRectF r = imageRect();
    if (frame_.isNull()) return p;
    return QPointF(r.left() + p.x() * r.width() / frame_.width(), r.top() + p.y() * r.height() / frame_.height());
}

QPointF FramingViewer::toFrame(QPointF p) const {
    const QRectF r = imageRect();
    if (frame_.isNull() || r.isEmpty()) return p;
    return QPointF((p.x() - r.left()) * frame_.width() / r.width(), (p.y() - r.top()) * frame_.height() / r.height());
}

QPointF FramingViewer::rotationHandle() const {
    const QPolygonF box = media::frameFlexBox(frame_.size(), flex_);
    if (box.size() < 4) return {};
    // Above the middle of the top edge (the box's corners start at its top left), 18 pixels out.
    const QPointF top = toWidget((box[0] + box[1]) / 2);
    const QPointF centre = toWidget(centreOf(box));
    const double d = distance(top, centre);
    return d > 0 ? top + (top - centre) * (18.0 / d) : top;
}

void FramingViewer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Base));
    if (frame_.isNull()) return;
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(imageRect(), frame_);
    const QPolygonF box = media::frameFlexBox(frame_.size(), flex_);
    QPolygonF shown;
    for (const QPointF& v : box) shown << toWidget(v);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(flex_.color), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(shown);
    // Corner handles and the rotation handle with its bar.
    p.setBrush(QColor(flex_.color));
    for (const QPointF& v : shown) p.drawRect(QRectF(v.x() - 3, v.y() - 3, 6, 6));
    if (shown.size() >= 4) {
        const QPointF top = (shown[0] + shown[1]) / 2;
        const QPointF handle = rotationHandle();
        p.drawLine(top, handle);
        p.drawEllipse(handle, 4, 4);
    }
}

void FramingViewer::mousePressEvent(QMouseEvent* event) {
    if (frame_.isNull() || event->button() != Qt::LeftButton) return;
    const QPointF at = event->position();
    const QPolygonF box = media::frameFlexBox(frame_.size(), flex_);
    pressFrame_ = toFrame(at);
    pressFlex_ = flex_;
    drag_ = Drag::None;
    if (distance(at, rotationHandle()) <= 7) {
        drag_ = Drag::Rotate;
    } else {
        for (const QPointF& v : box) {
            if (distance(at, toWidget(v)) <= 7) drag_ = Drag::Resize;
        }
        if (drag_ == Drag::None && box.containsPoint(pressFrame_, Qt::OddEvenFill)) drag_ = Drag::Move;
    }
}

void FramingViewer::mouseMoveEvent(QMouseEvent* event) {
    if (drag_ == Drag::None || frame_.isNull()) return;
    const QPointF now = toFrame(event->position());
    const QSize image = pressFlex_.imageSize.isEmpty() ? frame_.size() : pressFlex_.imageSize;
    const double sx = static_cast<double>(image.width()) / frame_.width();
    const double sy = static_cast<double>(image.height()) / frame_.height();
    const QPointF centre = centreOf(media::frameFlexBox(frame_.size(), pressFlex_));
    model::FrameFlex f = pressFlex_;
    if (drag_ == Drag::Move) {
        f.x = pressFlex_.x + (now.x() - pressFrame_.x()) * sx;
        f.y = pressFlex_.y + (now.y() - pressFrame_.y()) * sy;
    } else if (drag_ == Drag::Resize) {
        const double before = distance(pressFrame_, centre);
        if (before > 1e-6) f.size = std::clamp(pressFlex_.size * distance(now, centre) / before, 0.01, 10.0);
    } else {
        // Screen y runs down, so an anticlockwise turn is a falling angle.
        const double a0 = std::atan2(pressFrame_.y() - centre.y(), pressFrame_.x() - centre.x());
        const double a1 = std::atan2(now.y() - centre.y(), now.x() - centre.x());
        double degrees = pressFlex_.rotation - (a1 - a0) * 180.0 / std::numbers::pi;
        degrees = std::fmod(degrees + 540.0, 360.0) - 180.0;
        f.rotation = degrees;
    }
    flex_ = f;
    update();
    emit frameFlexDragged(flex_);
}

void FramingViewer::mouseReleaseEvent(QMouseEvent*) {
    drag_ = Drag::None;
}

// ---------------------------------------------------------------- SourceSettingsDialog

SourceSettingsDialog::SourceSettingsDialog(const QString& clipName, const QImage& frame, QSize projectRaster,
                                           const model::SpatialAdapter& adapter, int clipCount, QWidget* parent)
    : QDialog(parent), frame_(frame), raster_(projectRaster), applied_(adapter) {
    setObjectName(QStringLiteral("sourceSettingsDialog"));
    setWindowTitle(clipCount > 1 ? tr("Source Settings - %n clips", "", clipCount) : tr("Source Settings - %1").arg(clipName));
    auto* layout = new QVBoxLayout(this);
    showViewers_ = new QCheckBox(tr("Show Viewers"));
    showViewers_->setObjectName(QStringLiteral("showViewers"));
    showViewers_->setChecked(true);
    layout->addWidget(showViewers_);
    viewers_ = new QWidget;
    auto* viewerLayout = new QVBoxLayout(viewers_);
    viewerLayout->setContentsMargins(0, 0, 0, 0);
    framing_ = new FramingViewer;
    framing_->setFrame(frame);
    viewerLayout->addWidget(framing_, 1);
    result_ = new QLabel;
    result_->setObjectName(QStringLiteral("resultViewer"));
    result_->setAlignment(Qt::AlignCenter);
    result_->setMinimumHeight(120);
    viewerLayout->addWidget(result_, 1);
    layout->addWidget(viewers_, 1);
    connect(showViewers_, &QCheckBox::toggled, viewers_, &QWidget::setVisible);

    auto* tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("sourceSettingsTabs"));
    auto* flexTab = new QWidget;
    auto* form = new QFormLayout(flexTab);
    rasterLabel_ = new QLabel(QStringLiteral("%1 x %2").arg(frame.width()).arg(frame.height()));
    rasterLabel_->setObjectName(QStringLiteral("rasterDimension"));
    form->addRow(tr("Raster Dimension"), rasterLabel_);
    imageWidth_ = new QSpinBox;
    imageWidth_->setObjectName(QStringLiteral("imageWidth"));
    imageHeight_ = new QSpinBox;
    imageHeight_->setObjectName(QStringLiteral("imageHeight"));
    for (QSpinBox* s : {imageWidth_, imageHeight_}) {
        s->setRange(1, 16384);
        s->setKeyboardTracking(false);
    }
    auto* toRaster = new QPushButton(QStringLiteral("="));
    toRaster->setObjectName(QStringLiteral("imageSizeToRaster"));
    toRaster->setToolTip(tr("Set the Image Size to the raster dimension"));
    connect(toRaster, &QPushButton::clicked, this, &SourceSettingsDialog::imageSizeToRaster);
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(imageWidth_);
    sizeRow->addWidget(new QLabel(QStringLiteral("x")));
    sizeRow->addWidget(imageHeight_);
    sizeRow->addWidget(toRaster);
    form->addRow(tr("Image Size"), sizeRow);
    imageAspect_ = spin(QStringLiteral("imageAspect"), 0, 10, 3, 0.01);
    imageAspect_->setSpecialValueText(tr("Image's own"));
    form->addRow(tr("Image aspect ratio"), imageAspect_);
    pixelAspect_ = spin(QStringLiteral("pixelAspect"), 0.1, 4, 3, 0.01);
    form->addRow(tr("Pixel aspect ratio"), pixelAspect_);
    frameAspect_ = spin(QStringLiteral("frameAspect"), 0, 10, 3, 0.01);
    frameAspect_->setSpecialValueText(tr("Image's own"));
    form->addRow(tr("Frame aspect ratio"), frameAspect_);
    x_ = spin(QStringLiteral("framingX"), -16384, 16384, 1, 1);
    y_ = spin(QStringLiteral("framingY"), -16384, 16384, 1, 1);
    auto* xy = new QHBoxLayout;
    xy->addWidget(x_);
    xy->addWidget(y_);
    form->addRow(tr("X, Y"), xy);
    size_ = spin(QStringLiteral("framingSize"), 0.01, 10, 3, 0.01);
    form->addRow(tr("Size"), size_);
    rotation_ = spin(QStringLiteral("zRotation"), -180, 180, 1, 1);
    auto* left = new QPushButton(tr("Rotate Left"));
    left->setObjectName(QStringLiteral("rotateLeft"));
    connect(left, &QPushButton::clicked, this, [this] { rotate(90); });
    auto* right = new QPushButton(tr("Rotate Right"));
    right->setObjectName(QStringLiteral("rotateRight"));
    connect(right, &QPushButton::clicked, this, [this] { rotate(-90); });
    auto* rotationRow = new QHBoxLayout;
    rotationRow->addWidget(rotation_);
    rotationRow->addWidget(left);
    rotationRow->addWidget(right);
    form->addRow(tr("Z Rotation"), rotationRow);
    auto* matches = new QPushButton(tr("Size matches project raster"));
    matches->setObjectName(QStringLiteral("sizeMatchesRaster"));
    connect(matches, &QPushButton::clicked, this, &SourceSettingsDialog::sizeMatchesRaster);
    form->addRow(QString(), matches);
    color_ = new QComboBox;
    color_->setObjectName(QStringLiteral("framingColor"));
    for (const auto& [name, rgb] : kColors) color_->addItem(tr(name), QString::fromLatin1(rgb));
    form->addRow(tr("Color"), color_);
    auto* resetButton = new QPushButton(tr("Reset"));
    resetButton->setObjectName(QStringLiteral("reset"));
    connect(resetButton, &QPushButton::clicked, this, &SourceSettingsDialog::reset);
    form->addRow(QString(), resetButton);
    reformat_ = new QComboBox;
    reformat_->setObjectName(QStringLiteral("reformat"));
    for (model::Reformat r : {model::Reformat::Stretch, model::Reformat::Letterbox, model::Reformat::CenterCrop,
                              model::Reformat::CenterKeepSize}) {
        reformat_->addItem(model::reformatText(r), static_cast<int>(r));
    }
    form->addRow(tr("Reformat"), reformat_);
    tabs->addTab(flexTab, tr("FrameFlex"));
    // Playback Rate (p.545): "Select the desired Playback Frame Rate".
    auto* rateTab = new QWidget;
    auto* rateForm = new QFormLayout(rateTab);
    playbackRate_ = new QComboBox;
    playbackRate_->setObjectName(QStringLiteral("playbackRate"));
    playbackRate_->setEnabled(false);
    rateForm->addRow(tr("Playback Frame Rate"), playbackRate_);
    playbackNote_ = new QLabel;
    playbackNote_->setObjectName(QStringLiteral("playbackNote"));
    playbackNote_->setWordWrap(true);
    rateForm->addRow(playbackNote_);
    tabs->addTab(rateTab, tr("Playback Rate"));
    // Color Encoding (p.537-540).
    auto* colorTab = new QWidget;
    auto* colorLayout = new QVBoxLayout(colorTab);
    auto* spaceRow = new QHBoxLayout;
    spaceRow->addWidget(new QLabel(tr("Source color space")));
    sourceSpace_ = new QComboBox;
    sourceSpace_->setObjectName(QStringLiteral("sourceColorSpace"));
    sourceSpace_->addItems(model::sourceColorSpaces());
    spaceRow->addWidget(sourceSpace_, 1);
    auto* autoButton = new QPushButton(tr("Auto"));
    autoButton->setObjectName(QStringLiteral("colorAuto"));
    connect(autoButton, &QPushButton::clicked, this, &SourceSettingsDialog::autoTransformations);
    spaceRow->addWidget(autoButton);
    colorLayout->addLayout(spaceRow);
    colorLayout->addWidget(new QLabel(tr("Color transformations")));
    transformations_ = new QListWidget;
    transformations_->setObjectName(QStringLiteral("colorTransformations"));
    transformations_->setDragDropMode(QAbstractItemView::InternalMove);
    connect(transformations_->model(), &QAbstractItemModel::rowsMoved, this, &SourceSettingsDialog::colorChanged);
    colorLayout->addWidget(transformations_, 1);
    auto* addRow = new QHBoxLayout;
    transformationChoice_ = new QComboBox;
    transformationChoice_->setObjectName(QStringLiteral("transformationChoice"));
    transformationChoice_->addItems(media::colorTransformationNames());
    addRow->addWidget(transformationChoice_, 1);
    auto* addButton = new QPushButton(tr("Add"));
    addButton->setObjectName(QStringLiteral("addTransformation"));
    connect(addButton, &QPushButton::clicked, this, [this] { addTransformation(transformationChoice_->currentText()); });
    addRow->addWidget(addButton);
    auto* deleteButton = new QPushButton(tr("Delete"));
    deleteButton->setObjectName(QStringLiteral("deleteTransformation"));
    connect(deleteButton, &QPushButton::clicked, this, [this] { deleteTransformation(transformations_->currentRow()); });
    addRow->addWidget(deleteButton);
    colorLayout->addLayout(addRow);
    bypass_ = new QCheckBox(tr("Bypass all color transformations"));
    bypass_->setObjectName(QStringLiteral("bypassColor"));
    colorLayout->addWidget(bypass_);
    connect(sourceSpace_, &QComboBox::currentIndexChanged, this, &SourceSettingsDialog::colorChanged);
    connect(bypass_, &QCheckBox::toggled, this, &SourceSettingsDialog::colorChanged);
    tabs->addTab(colorTab, tr("Color Encoding"));
    layout->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton* revertButton = buttons->addButton(tr("Revert"), QDialogButtonBox::ResetRole);
    revertButton->setObjectName(QStringLiteral("revert"));
    QPushButton* applyButton = buttons->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
    applyButton->setObjectName(QStringLiteral("apply"));
    connect(revertButton, &QPushButton::clicked, this, &SourceSettingsDialog::revert);
    connect(applyButton, &QPushButton::clicked, this, &SourceSettingsDialog::apply);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Every field change shows at once in both viewers.
    for (QSpinBox* s : {imageWidth_, imageHeight_}) connect(s, &QSpinBox::valueChanged, this, &SourceSettingsDialog::refresh);
    for (QDoubleSpinBox* s : {imageAspect_, pixelAspect_, frameAspect_, x_, y_, size_, rotation_}) {
        connect(s, &QDoubleSpinBox::valueChanged, this, &SourceSettingsDialog::refresh);
    }
    connect(color_, &QComboBox::currentIndexChanged, this, &SourceSettingsDialog::refresh);
    connect(reformat_, &QComboBox::currentIndexChanged, this, &SourceSettingsDialog::refresh);
    connect(framing_, &FramingViewer::frameFlexDragged, this, [this](const model::FrameFlex& f) {
        model::SpatialAdapter a = this->adapter();
        a.frameFlex = f;
        fill(a);
    });
    fill(adapter);
    resize(560, 760);
}

void SourceSettingsDialog::fill(const model::SpatialAdapter& adapter) {
    filling_ = true;
    const model::FrameFlex f = adapter.frameFlex.value_or(model::FrameFlex{});
    const QSize image = f.imageSize.isEmpty() ? frame_.size() : f.imageSize;
    imageWidth_->setValue(image.width());
    imageHeight_->setValue(image.height());
    imageAspect_->setValue(f.imageAspect);
    pixelAspect_->setValue(f.pixelAspect);
    frameAspect_->setValue(f.frameAspect);
    x_->setValue(f.x);
    y_->setValue(f.y);
    size_->setValue(f.size);
    rotation_->setValue(f.rotation);
    color_->setCurrentIndex(std::max(0, color_->findData(f.color)));
    reformat_->setCurrentIndex(std::max(0, reformat_->findData(static_cast<int>(adapter.reformat))));
    filling_ = false;
    refresh();
}

model::SpatialAdapter SourceSettingsDialog::adapter() const {
    model::SpatialAdapter a;
    a.reformat = static_cast<model::Reformat>(reformat_->currentData().toInt());
    model::FrameFlex f;
    const QSize image(imageWidth_->value(), imageHeight_->value());
    if (image != frame_.size()) f.imageSize = image;
    f.imageAspect = imageAspect_->value();
    f.pixelAspect = pixelAspect_->value();
    f.frameAspect = frameAspect_->value();
    f.x = x_->value();
    f.y = y_->value();
    f.size = size_->value();
    f.rotation = rotation_->value();
    f.color = color_->currentData().toString();
    if (!f.isFullImage()) a.frameFlex = f;
    return a;
}

void SourceSettingsDialog::setAdapter(const model::SpatialAdapter& a) {
    fill(a);
}

void SourceSettingsDialog::refresh() {
    if (filling_) return;
    const model::SpatialAdapter a = adapter();
    framing_->setFrameFlex(a.frameFlex.value_or(model::FrameFlex{}));
    result_->setPixmap(QPixmap::fromImage(resultImage()));
}

QImage SourceSettingsDialog::resultImage() const {
    if (frame_.isNull() || raster_.isEmpty()) return {};
    const QSize shown = raster_.scaled(QSize(320, 180), Qt::KeepAspectRatio);
    return media::spatialAdaptedImage(shownFrame(), adapter(), raster_, shown);
}

void SourceSettingsDialog::setViewersShown(bool shown) {
    showViewers_->setChecked(shown);
}

bool SourceSettingsDialog::viewersShown() const {
    return showViewers_->isChecked();
}

void SourceSettingsDialog::sizeMatchesRaster() {
    model::SpatialAdapter a = adapter();
    a.frameFlex = model::sizeMatchingRaster(a.frameFlex.value_or(model::FrameFlex{}), frame_.size(), raster_);
    fill(a);
}

void SourceSettingsDialog::imageSizeToRaster() {
    // "To set the Image Size equal to the raster dimension, click the button to the right of the Image size" (p.531).
    imageWidth_->setValue(frame_.width());
    imageHeight_->setValue(frame_.height());
}

void SourceSettingsDialog::rotate(double degrees) {
    double r = rotation_->value() + degrees;
    r = std::fmod(r + 540.0, 360.0) - 180.0;
    rotation_->setValue(r);
}

void SourceSettingsDialog::reset() {
    // "Resets the framing to the original size" (p.530); the Reformat attribute stays.
    model::SpatialAdapter a = adapter();
    a.frameFlex.reset();
    fill(a);
}

void SourceSettingsDialog::setPlaybackRates(const QString& original, const QString& project, bool atProjectRate, bool allowed,
                                            const QString& whyNot) {
    playbackRate_->clear();
    playbackRate_->addItem(tr("Original: %1").arg(original), false);
    playbackRate_->addItem(tr("Project: %1").arg(project), true);
    playbackRate_->setCurrentIndex(atProjectRate ? 1 : 0);
    playbackRate_->setEnabled(allowed);
    playbackNote_->setText(whyNot);
    appliedRate_ = atProjectRate;
}

bool SourceSettingsDialog::playbackAtProjectRate() const {
    return playbackRate_->currentData().toBool();
}

void SourceSettingsDialog::revert() {
    fill(applied_);
    fillColor(appliedColor_);
    if (playbackRate_->count() > 1) playbackRate_->setCurrentIndex(appliedRate_ ? 1 : 0);
}

void SourceSettingsDialog::apply() {
    applied_ = adapter();
    appliedRate_ = playbackAtProjectRate();
    appliedColor_ = colorEncoding();
    if (applier_) applier_(applied_, appliedRate_, appliedColor_);
}

void SourceSettingsDialog::setColorEncoding(const model::ColorEncoding& encoding) {
    appliedColor_ = encoding;
    fillColor(encoding);
}

void SourceSettingsDialog::fillColor(const model::ColorEncoding& encoding) {
    filling_ = true;
    int space = sourceSpace_->findText(encoding.sourceSpace);
    if (space < 0) {
        // A space the menu does not list, kept as it is.
        sourceSpace_->addItem(encoding.sourceSpace);
        space = sourceSpace_->count() - 1;
    }
    sourceSpace_->setCurrentIndex(space);
    transformations_->clear();
    transformations_->addItems(encoding.transformations);
    bypass_->setChecked(encoding.bypass);
    filling_ = false;
    colorChanged();
}

model::ColorEncoding SourceSettingsDialog::colorEncoding() const {
    model::ColorEncoding e;
    e.sourceSpace = sourceSpace_->currentText();
    for (int i = 0; i < transformations_->count(); ++i) e.transformations << transformations_->item(i)->text();
    e.bypass = bypass_->isChecked();
    return e;
}

void SourceSettingsDialog::addTransformation(const QString& name) {
    if (name.isEmpty()) return;
    transformations_->addItem(name);
    transformations_->setCurrentRow(transformations_->count() - 1);
    colorChanged();
}

void SourceSettingsDialog::deleteTransformation(int row) {
    if (row < 0 || row >= transformations_->count()) return;
    delete transformations_->takeItem(row);
    colorChanged();
}

void SourceSettingsDialog::moveTransformation(int from, int to) {
    const int n = transformations_->count();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    QListWidgetItem* item = transformations_->takeItem(from);
    transformations_->insertItem(to, item);
    transformations_->setCurrentRow(to);
    colorChanged();
}

void SourceSettingsDialog::autoTransformations() {
    model::ColorEncoding e = colorEncoding();
    e.transformations = model::autoTransformations(e.sourceSpace);
    fillColor(e);
}

QImage SourceSettingsDialog::shownFrame() const {
    return media::applyColorTransformations(frame_, colorEncoding().applied());
}

// "The selection is added to the Color transformations list and applied to the image in the viewer" (p.539).
void SourceSettingsDialog::colorChanged() {
    if (filling_) return;
    framing_->setFrame(shownFrame());
    refresh();
}

} // namespace lumenis::ui
