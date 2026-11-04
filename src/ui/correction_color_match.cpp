// The Color Correction tool's Color Match controls (correction_tool.h; FX Guide p.624-631): the swatches, the Match Type
// menu with NaturalMatch, and the Match Color button, one control per group (p.624). The correction itself is
// media::matchColor.

#include "lumenis/ui/correction_tool.h"

#include "lumenis/ui/timeline.h"

#include <QColorDialog>
#include <QEvent>
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
        QAction* natural = menu->addAction(tr("NaturalMatch"));
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
    return tr("R: %1 G: %2 B: %3").arg(level(c.redF())).arg(level(c.greenF())).arg(level(c.blueF()));
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
            // "The pointer changes to an eyedropper" (p.625): the next colour sampled in a monitor fills the swatch.
            emit pickRequested(output);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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
    return control;
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
