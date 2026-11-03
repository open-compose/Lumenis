// The Effect Editor's Brush category, the eyedropper and the AniMatte matte (ROADMAP 6.9c; FX Guide p.89, p.397-399,
// p.412, p.418, p.493; docs/spec/intraframe.md): the Brush Shape buttons, the Brush Template icon dragged to a bin, the
// Brush Preview window (brush_preview.h), picking a colour parameter from a monitor's picture, the film projects'
// Previsualization Marker tool and Export Matte PICT.

#include "lumenis/ui/effect_editor.h"

#include "lumenis/media/pict.h"
#include "lumenis/model/intraframe.h"
#include "lumenis/ui/brush_preview.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QDrag>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

#include "lumenis/media/compositor.h"

namespace lumenis::ui {

namespace {

// A Brush Shape button's picture: the head drawn in the button.
QIcon shapeIcon(const QString& shape, const QColor& colour) {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QTransform t;
    t.translate(8.0, 8.0);
    t.scale(13.0, 13.0);
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawPath(t.map(media::intraframePath(model::brushShapePoints(shape), true, QSizeF(1.0, 1.0))));
    return QIcon(pixmap);
}

// The Brush Template icon (p.399): dragged to a bin it makes a brush template.
class BrushTemplateIcon : public QLabel {
public:
    BrushTemplateIcon(EffectEditor& editor, QWidget* parent) : QLabel(parent), editor_(editor) {
        setObjectName(QStringLiteral("brushTemplate"));
        setToolTip(EffectEditor::tr("Brush Template"));
        setFixedSize(20, 20);
        QPixmap pixmap(18, 18);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(palette().color(QPalette::WindowText), 1.5));
        // Ours: a brush's handle and bristles.
        p.drawLine(QPointF(14, 3), QPointF(8, 10));
        p.setBrush(palette().color(QPalette::WindowText));
        p.drawEllipse(QPointF(6, 12), 3.5, 3.5);
        setPixmap(pixmap);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) press_ = event->position().toPoint();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton) || (event->position().toPoint() - press_).manhattanLength() < QApplication::startDragDistance()) return;
        if (QMimeData* mime = editor_.brushTemplateMime()) {
            auto* drag = new QDrag(this);
            drag->setMimeData(mime);
            drag->setPixmap(pixmap());
            drag->exec(Qt::CopyAction);
        }
    }

private:
    EffectEditor& editor_;
    QPoint press_;
};

} // namespace

QJsonObject EffectEditor::shownBrush() const {
    return current_ ? current_->parameters.value(model::kBrushCategory).toObject() : QJsonObject{};
}

QWidget* EffectEditor::brushPanel(QWidget* parent) {
    auto* panel = new QWidget(parent);
    auto* row = new QHBoxLayout(panel);
    row->setContentsMargins(0, 0, 0, 0);
    auto* column = new QVBoxLayout;
    column->setSpacing(1);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(1);
    const QColor ink = palette().color(QPalette::WindowText);
    for (const QString& shape : model::brushShapes()) {
        auto* b = new QToolButton(panel);
        b->setObjectName(QStringLiteral("brushShape:") + shape);
        b->setToolTip(shape);
        b->setIcon(shapeIcon(shape, ink));
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [this, shape] { setBrushShape(shape); });
        buttons->addWidget(b);
    }
    column->addLayout(buttons);
    column->addWidget(new BrushTemplateIcon(*this, panel));
    column->addStretch(1);
    row->addLayout(column);
    auto* preview = new BrushPreview(panel);
    preview->setItemResolver([this](const QString& bin, const QString& item) { return timeline_.composer().resolve(bin, item); });
    preview->setBrush(shownBrush());
    connect(preview, &BrushPreview::headChanged, this, [this](const QList<model::ControlPoint>& head) { setBrushHead(head); });
    connect(preview, &BrushPreview::templateDropped, this, [this](const QJsonObject& brush) { applyBrushTemplate(brush); });
    row->addWidget(preview);
    row->addStretch(1);
    brushPreview_ = preview;
    return panel;
}

bool EffectEditor::setBrushShape(const QString& shape) {
    if (!current_) return false;
    QJsonObject brush = shownBrush();
    const QJsonObject before = brush;
    model::setBrushShape(brush, shape);
    if (brush == before) return false;
    return changeShown(tr("Brush Shape"), [&](model::Effect& e) { e.parameters.insert(model::kBrushCategory, brush); });
}

bool EffectEditor::setBrushHead(const QList<model::ControlPoint>& head) {
    if (!current_) return false;
    QJsonObject brush = shownBrush();
    model::setBrushHead(brush, head);
    return changeShown(tr("Brush Head"), [&](model::Effect& e) { e.parameters.insert(model::kBrushCategory, brush); });
}

bool EffectEditor::applyBrushTemplate(const QJsonObject& brush) {
    // Only an effect with a Brush category (the Paint Effect and AniMatte, p.493).
    if (!current_ || (!current_->parameters.contains(model::kBrushCategory) && !parameterInfo(model::kBrushCategory + QStringLiteral("/Size"))))
        return false;
    return changeShown(tr("Apply Brush Template"), [&](model::Effect& e) { e.parameters.insert(model::kBrushCategory, brush); });
}

QMimeData* EffectEditor::brushTemplateMime() const {
    if (!current_) return nullptr;
    auto* mime = new QMimeData;
    const QJsonObject o{{QStringLiteral("name"), QStringLiteral("Brush")}, {QStringLiteral("brush"), shownBrush()}};
    mime->setData(ui::brushTemplateMime(), QJsonDocument(o).toJson(QJsonDocument::Compact));
    mime->setText(QStringLiteral("Brush"));
    return mime;
}

bool EffectEditor::pickColorAt(const QString& category, const QString& parameter, const QPoint& global) {
    const auto colour = timeline_.composer().colorAtScreen(global);
    if (!colour) return false;
    setValue(category, parameter, colour->name());
    return true;
}

bool EffectEditor::pickPaintColor(const QPointF& fraction) {
    // "The color you pick becomes the new default color shown in the Color Preview window" (p.397): the Paint
    // Effect's colour.
    if (!current_ || current_->name != QLatin1String("Paint Effect")) return false;
    const auto colour = timeline_.composer().colorAt(MonitorId::Record, fraction);
    if (!colour) return false;
    setValue(QStringLiteral("Paint Effect Color Parameters"), QStringLiteral("Color controls"), colour->name());
    return true;
}

void EffectEditor::setFilmProject(bool film) {
    if (film == filmProject_) return;
    filmProject_ = film;
    syncIntraframeButtons();
}

void EffectEditor::setInterlacedProject(bool interlaced) {
    if (interlaced == interlacedProject_) return;
    interlacedProject_ = interlaced;
    shownName_.clear(); // the controls are made again, with or without Frame Processing
    refresh();
}

QImage EffectEditor::aniMatteImage(const QSize& size) const {
    if (!wholeIntraframe_ || !model::isAniMatte(*wholeIntraframe_) || size.isEmpty()) return {};
    // The foreground for Magic Mask's colours: ours, the Record monitor's picture as shown.
    const QImage shown = timeline_.composer().picture(MonitorId::Record, size);
    const media::Picture layer = shown.isNull() ? media::Picture(size.width(), size.height())
                                                : media::Picture::fromImage(shown, size.width(), size.height());
    return media::aniMatteImage(*wholeIntraframe_, layer, positionTime(), span_);
}

bool EffectEditor::exportMattePict(const QString& path, const QSize& size) const {
    const QImage matte = aniMatteImage(size);
    return !matte.isNull() && media::writePict(matte, path);
}

} // namespace lumenis::ui
