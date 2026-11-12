// The Color Correction tool's Color Match controls (correction_tool.h; FX Guide p.624-631): the swatches, the Match Type
// menu with NaturalMatch, and the Match Color button, one control per group (p.624). The correction itself is
// media::matchColor.

#include "lumenis/ui/correction_tool.h"

#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QColorDialog>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QGridLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QToolButton>

#include <cmath>

namespace lumenis::ui {

ColorMatchControl::ColorMatchControl(media::MatchGroup group, QWidget* parent) : QWidget(parent), group_(group) {
    type_ = media::matchTypes(group).value(0);
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 2, 0, 2);
    grid->setSpacing(2);
    auto swatch = [this](const QString& name, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setMinimumSize(64, 30);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setAcceptDrops(true);
        b->installEventFilter(this);
        return b;
    };
    inSwatch_ = swatch(QStringLiteral("matchInput"), tr("Input color (press and drag into a monitor; double-click for colors)"));
    outSwatch_ = swatch(QStringLiteral("matchOutput"), tr("Output color (press and drag into a monitor; double-click for colors)"));
    grid->addWidget(inSwatch_, 0, 0);
    grid->addWidget(outSwatch_, 0, 1);
    typeButton_ = new QToolButton(this);
    typeButton_->setObjectName(QStringLiteral("matchType"));
    typeButton_->setPopupMode(QToolButton::InstantPopup);
    typeButton_->setToolTip(tr("Match Type"));
    auto* menu = new QMenu(typeButton_);
    for (const QString& key : media::matchTypes(group)) {
        QAction* a = menu->addAction(media::matchTypeText(key));
        a->setData(key);
        connect(a, &QAction::triggered, this, [this, key] { setMatchType(key); });
    }
    if (media::hasNaturalMatch(group)) {
        // "Choose NaturalMatch to select or deselect the NaturalMatch feature" (p.627).
        menu->addSeparator();
        QAction* natural = menu->addAction(tr("Keep Luminance"));
        natural->setObjectName(QStringLiteral("naturalMatch"));
        natural->setCheckable(true);
        connect(natural, &QAction::triggered, this, [this](bool on) { setNaturalMatch(on); });
    }
    typeButton_->setMenu(menu);
    grid->addWidget(typeButton_, 1, 0, 1, 2);
    matchButton_ = new QToolButton(this);
    matchButton_->setObjectName(QStringLiteral("matchColor"));
    matchButton_->setText(tr("Match Color"));
    matchButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(matchButton_, &QToolButton::clicked, this, &ColorMatchControl::matchRequested);
    grid->addWidget(matchButton_, 2, 0, 1, 2);
    sync();
}

void ColorMatchControl::setColor(bool output, const QColor& color) {
    (output ? output_ : input_) = color;
    sync();
}

void ColorMatchControl::setMatchType(const QString& type) {
    if (!media::matchTypes(group_).contains(type)) return;
    type_ = type;
    sync();
}

void ColorMatchControl::setNaturalMatch(bool on) {
    natural_ = on && media::hasNaturalMatch(group_);
    sync();
}

void ColorMatchControl::setShowInfo(bool on) {
    showInfo_ = on;
    sync();
}

QString ColorMatchControl::infoText(bool output) const {
    const QColor c = output ? output_ : input_;
    if (!showInfo_ || !c.isValid()) return {};
    auto level = [](float v) { return static_cast<int>(std::lround(16.0 + 219.0 * static_cast<double>(v))); };
    const std::array<bool, 3> warn = rgbWarnings(output);
    auto value = [&](float v, std::size_t channel) {
        return warn.at(channel) ? QStringLiteral("%1 \u26A0").arg(level(v)) : QString::number(level(v));
    };
    return tr("R: %1 G: %2 B: %3").arg(value(c.redF(), 0), value(c.greenF(), 1), value(c.blueF(), 2));
}

void ColorMatchControl::setSafeColors(const model::SafeColorLimits& limits) {
    if (limits == safeColors_) return;
    safeColors_ = limits;
    sync();
}

std::array<bool, 3> ColorMatchControl::rgbWarnings(bool output) const {
    const QColor c = output ? output_ : input_;
    if (!c.isValid() || !safeColors_.warnRgb) return {false, false, false};
    // "Warnings appear in the Color Match control for RGB Gamut limits only" (p.724).
    const model::SafeViolations v =
        model::safeViolations(model::pictureLevel(c.red()), model::pictureLevel(c.green()), model::pictureLevel(c.blue()), safeColors_);
    return {v.rgbLow[0] || v.rgbHigh[0], v.rgbLow[1] || v.rgbHigh[1], v.rgbLow[2] || v.rgbHigh[2]};
}

void ColorMatchControl::sync() {
    for (bool output : {false, true}) {
        QToolButton* b = output ? outSwatch_ : inSwatch_;
        const QColor c = output ? output_ : input_;
        QPalette p = b->palette();
        p.setColor(QPalette::Button, c.isValid() ? c : palette().color(QPalette::Base));
        p.setColor(QPalette::ButtonText, c.isValid() && c.lightnessF() > 0.55f ? Qt::black : Qt::white);
        b->setPalette(p);
        b->setAutoFillBackground(true);
        b->setText(infoText(output));
    }
    typeButton_->setText(media::matchTypeText(type_, natural_));
    if (auto* natural = typeButton_->menu()->findChild<QAction*>(QStringLiteral("naturalMatch"))) natural->setChecked(natural_);
    for (QAction* a : typeButton_->menu()->actions()) {
        if (!a->data().toString().isEmpty()) a->setText(media::matchTypeText(a->data().toString(), natural_));
    }
    matchButton_->setEnabled(input_.isValid() && output_.isValid());
}

bool ColorMatchControl::eventFilter(QObject* watched, QEvent* event) {
    auto* b = qobject_cast<QToolButton*>(watched);
    if (b && (b == inSwatch_ || b == outSwatch_)) {
        const bool output = b == outSwatch_;
        if (event->type() == QEvent::MouseButtonDblClick) {
            // "double-click the output color swatch and select a color from the Windows Color dialog box or the
            // Macintosh Colors panel" (p.626).
            const QColor chosen = QColorDialog::getColor(color(output), this, output ? tr("Output Color") : tr("Input Color"));
            if (chosen.isValid()) setColor(output, chosen);
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            auto* press = static_cast<QMouseEvent*>(event);
            pressAt_ = press->position().toPoint();
            // Alt (Option) drags the colour out to a bin (p.630).
            pressAlt_ = press->modifiers().testFlag(Qt::AltModifier);
            if (pressAlt_) return true;
            if (anywhere_) {
                // Picks from anywhere: the colour where the mouse is let go.
                tracking_ = true;
                return true;
            }
            // "The pointer changes to an eyedropper" (p.625): the next colour sampled in a monitor fills the swatch.
            emit pickRequested(output);
            return true;
        }
        if (event->type() == QEvent::MouseMove && pressAlt_) {
            auto* move = static_cast<QMouseEvent*>(event);
            if ((move->position().toPoint() - pressAt_).manhattanLength() < QApplication::startDragDistance() || !color(output).isValid()) return true;
            pressAlt_ = false;
            // "While you drag, the pointer changes to a hand, and a rectangular outline appears" (p.630).
            auto* drag = new QDrag(b);
            drag->setMimeData(colorMime(output));
            QPixmap pm(32, 20);
            pm.fill(color(output));
            drag->setPixmap(pm);
            drag->exec(Qt::CopyAction);
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            pressAlt_ = false;
            if (tracking_) {
                tracking_ = false;
                const QColor picked = colorAt(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
                if (picked.isValid()) setColor(output, picked);
            }
            return true;
        }
        if (event->type() == QEvent::DragEnter) {
            auto* enter = static_cast<QDragEnterEvent*>(event);
            const QMimeData* dragged = enter->mimeData();
            if (dragged->hasFormat(customColorMime()) || dragged->hasFormat(binItemsMime())) enter->acceptProposedAction();
            return true;
        }
        if (event->type() == QEvent::Drop) {
            auto* drop = static_cast<QDropEvent*>(event);
            if (dropColor(output, drop->mimeData())) drop->acceptProposedAction();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QMimeData* ColorMatchControl::colorMime(bool output) const {
    const QColor c = color(output);
    auto* mime = new QMimeData;
    QJsonObject o{{QStringLiteral("color"), c.name(QColor::HexRgb)}, {QStringLiteral("name"), media::customColorName(c, labels_)}};
    mime->setData(customColorMime(), QJsonDocument(o).toJson(QJsonDocument::Compact));
    return mime;
}

bool ColorMatchControl::dropColor(bool output, const QMimeData* dropped) {
    const QMimeData* mime = dropped;
    if (!mime) return false;
    if (mime->hasFormat(customColorMime())) {
        const QColor c = QColor::fromString(QJsonDocument::fromJson(mime->data(customColorMime())).object().value(QStringLiteral("color")).toString());
        if (!c.isValid()) return false;
        setColor(output, c);
        return true;
    }
    // "Drag the color icon from the bin to the appropriate color swatch in the Color Match control" (p.631): the bin's
    // node id, then one item id a line (binItemsMime); the first custom colour counts.
    if (!mime->hasFormat(binItemsMime()) || !resolve_) return false;
    const QStringList lines = QString::fromUtf8(mime->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        if (const auto item = resolve_(lines.at(0), lines.at(i))) {
            if (const auto c = media::customColorOf(*item)) {
                setColor(output, *c);
                return true;
            }
        }
    }
    return false;
}

QColor ColorMatchControl::colorAt(const QPoint& global) {
    // The application's window under the point: the system's answer, else the last shown top-level window holding it
    // (some platforms, the offscreen one among them, do not say).
    QWidget* window = nullptr;
    if (QWidget* under = QApplication::widgetAt(global)) window = under->window();
    if (!window) {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (w->isVisible() && QRect(w->mapToGlobal(QPoint(0, 0)), w->size()).contains(global)) window = w;
        }
    }
    if (!window) return {};
    // What the window draws there, as the eyedropper sees it.
    const QImage shot = window->grab(QRect(window->mapFromGlobal(global), QSize(1, 1))).toImage();
    return shot.isNull() ? QColor() : shot.pixelColor(0, 0);
}

// ---------------------------------------------------------------- the tool's side

ColorMatchControl* CorrectionTool::colorMatchFor(media::MatchGroup group, QWidget* parent) {
    static const char* const names[] = {"controls", "hueOffsets", "channels", "levels", "curves", "secondary"};
    auto* control = new ColorMatchControl(group, parent);
    control->setObjectName(QStringLiteral("colorMatch:") + QLatin1String(names[static_cast<int>(group)]));
    connect(control, &ColorMatchControl::pickRequested, this, [this, control](bool output) {
        picking_ = control;
        pickingOutput_ = output;
        setColorSelection(ColorSelection::MatchColor);
    });
    connect(control, &ColorMatchControl::matchRequested, this, [this, control] { matchColor(control); });
    control->setShowInfo(options_.eyedropperInfo);
    control->setLabels(options_.savedColorLabels);
    control->setPickAnywhere(options_.eyedropperAnywhere);
    return control;
}

void CorrectionTool::setItemResolver(ColorMatchControl::ItemResolver resolver) {
    for (ColorMatchControl* m : findChildren<ColorMatchControl*>()) m->setItemResolver(resolver);
}

ColorMatchControl* CorrectionTool::colorMatch(const QString& name) const {
    return findChild<ColorMatchControl*>(QStringLiteral("colorMatch:") + name);
}

void CorrectionTool::matchColor(ColorMatchControl* control) {
    if (!control || !hasSegment_) return;
    media::ColorMatch match;
    match.group = control->group();
    match.type = control->matchType();
    match.natural = control->naturalMatch();
    match.range = range_;
    match.vector = selectedVector();
    // "Media Composer adjusts the current segment and resets the group controls to reflect the adjustment" (p.626).
    if (media::matchColor(correction_, control->color(false), control->color(true), match)) commit(tr("Match Color"));
}

} // namespace lumenis::ui
