// The Titler+ Dashboard and the Effect Editor's Titler+ controls (titler_panel.h; FX Guide p.302-324;
// docs/spec/titler.md). Each control reads the first selected object (or the title) and writes through TitlerTool.
#include "lumenis/ui/titler_panel.h"
#include "lumenis/ui/theme.h"

#include "lumenis/ui/panels.h"

#include "lumenis/ui/titler_tool.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QAbstractButton>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <optional>
#include <tuple>

#include <algorithm>
#include <functional>

namespace lumenis::ui {

namespace {

using model::Title;
using model::TitleColor;
using model::TitleObject;
using model::TitleObjectKind;

// Commits a Title Text box when it loses the focus (p.321: "Allows you to input or edit text for the selected text
// object").
class FocusOut : public QObject {
public:
    FocusOut(QObject* parent, std::function<void()> done) : QObject(parent), done_(std::move(done)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::FocusOut) done_();
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> done_;
};

// The colour a swatch names on an object or title, and a way to set it.
TitleColor* colorField(TitleObject& o, const QString& which, int* corner) {
    *corner = -1;
    if (which.startsWith(QLatin1String("fill"))) {
        if (which.size() == 5) *corner = which.at(4).digitValue();
        return &o.fill.color;
    }
    if (which == QLatin1String("outline")) return &o.outline.color;
    if (which == QLatin1String("shadow")) return &o.shadow.color;
    return nullptr;
}

QString& cornerOf(TitleColor& c, int corner) {
    switch (corner) {
    case 0: return c.topLeft;
    case 1: return c.topRight;
    case 2: return c.bottomLeft;
    case 3: return c.bottomRight;
    default: return c.color;
    }
}

} // namespace

TitlerPanel::TitlerPanel(TitlerTool& tool, bool across, QWidget* parent) : QWidget(parent), tool_(tool) {
    setObjectName(QStringLiteral("titlerPanel"));
    setAcceptDrops(true); // templates as presets (p.320)
    // Two layouts of the same controls (7.6, the owner's report): the Dashboard as pictured on p.302 (two columns of
    // tool icons, then the Font, Transform and Style sections side by side, compact) and the Effect Editor's
    // categories under disclosure triangles as pictured on p.321. Controls the Dashboard does not show (the Effect
    // Editor's Foreground, Effect and Tracking categories, vertical justification ...) are kept in a hidden holder, so
    // refresh() and the tests find every control in both.
    auto* outer = across ? static_cast<QBoxLayout*>(new QHBoxLayout(this)) : static_cast<QBoxLayout*>(new QVBoxLayout(this));
    outer->setContentsMargins(2, 2, 2, 2);
    outer->setSpacing(across ? 6 : 2);
    auto* holder = new QWidget(this);
    holder->hide();
    auto* held = new QVBoxLayout(holder);
    static const QHash<QString, model::TitlePresetPart> parts = {{QStringLiteral("Font"), model::TitlePresetPart::Font},
                                                                {QStringLiteral("Transform"), model::TitlePresetPart::Transform},
                                                                {QStringLiteral("Fill"), model::TitlePresetPart::Fill},
                                                                {QStringLiteral("Outline"), model::TitlePresetPart::Outline},
                                                                {QStringLiteral("Shadow"), model::TitlePresetPart::Shadow}};
    // A section: in the Dashboard a framed column with its contents in `grid`; in the Effect Editor a category with a
    // disclosure triangle (expanded, ours) over a form.
    auto section = [&](const QString& title, QLayout* into) -> QWidget* {
        auto* box = new QFrame(this);
        box->setFrameShape(across ? QFrame::StyledPanel : QFrame::NoFrame);
        if (const auto p = parts.find(title); p != parts.end()) box->setProperty("presetPart", static_cast<int>(*p));
        controls_.insert(QStringLiteral("section:") + title, box);
        if (into) into->addWidget(box);
        return box;
    };
    auto formIn = [&](QWidget* box, const QString& title) {
        auto* v = new QVBoxLayout(box);
        v->setContentsMargins(across ? 4 : 0, 2, 2, 2);
        v->setSpacing(1);
        auto* body = new QWidget(box);
        if (!across) {
            auto* header = new QToolButton(box);
            header->setObjectName(QStringLiteral("titlerHeader:") + title);
            header->setText(title);
            header->setCheckable(true);
            header->setChecked(true);
            header->setArrowType(Qt::DownArrow);
            header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            header->setAutoRaise(true);
            v->addWidget(header);
            connect(header, &QToolButton::toggled, body, [header, body](bool open) {
                header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
                body->setVisible(open);
            });
        }
        auto* form = new QFormLayout(body);
        form->setContentsMargins(across ? 0 : 16, 0, 0, 0);
        form->setSpacing(2);
        v->addWidget(body);
        return form;
    };
    auto named = [this](QWidget* w, const QString& name) {
        w->setObjectName(QStringLiteral("titler:") + name);
        controls_.insert(name, w);
        return w;
    };
    // A number field on the selected objects; `title` ones change the whole title. Compact in the Dashboard.
    auto number = [&](const QString& name, const QString& label, double min, double max, int decimals,
                      std::function<void(TitleObject&, double)> setObject, std::function<void(Title&, double)> setTitle = {}) {
        auto* box = new QDoubleSpinBox(this);
        box->setRange(min, max);
        box->setDecimals(decimals);
        box->setKeyboardTracking(false);
        box->setToolTip(label);
        if (across) {
            box->setButtonSymbols(QAbstractSpinBox::NoButtons);
            box->setAlignment(Qt::AlignRight);
            box->setFixedWidth(52);
        }
        named(box, name);
        // "click the ... number and type the desired value" (p.309-317): one undo step when the value is entered.
        connect(box, &QDoubleSpinBox::editingFinished, this, [this, box, label, setObject, setTitle] {
            if (syncing_) return;
            const double v = box->value();
            if (setTitle) tool_.modifyTitle(label, [&](Title& t) { setTitle(t, v); });
            else tool_.modifySelected(label, [&](TitleObject& o) { setObject(o, v); });
        });
        return box;
    };
    // An on/off control: a check box in the Effect Editor; in the Dashboard a toggle (p.302) or a glyph button.
    auto check = [&](const QString& name, const QString& label, std::function<void(TitleObject&, bool)> setObject,
                     std::function<void(Title&, bool)> setTitle = {}, std::optional<GlyphIcon> glyph = std::nullopt) {
        QAbstractButton* box = nullptr;
        if (across) {
            auto* b = new QToolButton(this);
            b->setCheckable(true);
            b->setAutoRaise(true);
            if (glyph) b->setIcon(glyphIcon(*glyph, palette().color(QPalette::ButtonText)));
            else b->setText(label);
            b->setToolTip(label);
            box = b;
        } else {
            box = new QCheckBox(label, this);
        }
        named(box, name);
        connect(box, &QAbstractButton::toggled, this, [this, label, setObject, setTitle](bool on) {
            if (syncing_) return;
            if (setTitle) tool_.modifyTitle(label, [&](Title& t) { setTitle(t, on); });
            else tool_.modifySelected(label, [&](TitleObject& o) { setObject(o, on); });
        });
        return box;
    };
    auto menu = [&](const QString& name, const QStringList& items, std::function<void(int)> chosen) {
        auto* box = new QComboBox(this);
        box->addItems(items);
        named(box, name);
        connect(box, &QComboBox::activated, this, [this, chosen](int i) {
            if (!syncing_) chosen(i);
        });
        return box;
    };
    auto swatch = [&](const QString& which, const QString& label) {
        auto* b = new QToolButton(this);
        b->setFixedSize(across ? 18 : 28, 16);
        b->setToolTip(label);
        named(b, which + QStringLiteral("Color"));
        // "click the Fill Color Solid box ... The Select Color dialog opens" (p.315).
        connect(b, &QToolButton::clicked, this, [this, which, label] {
            const QColor c = QColorDialog::getColor(QColor(colorOf(which)), this, label, QColorDialog::ShowAlphaChannel);
            if (c.isValid()) chooseColor(which, c);
        });
        return b;
    };
    // A glyph button with its name as the tooltip (our own drawings, theme.h).
    auto button = [&](const QString& name, const QString& tip, GlyphIcon glyph, std::function<void()> clicked, bool checkable = false) {
        auto* b = new QToolButton(this);
        b->setIcon(glyphIcon(glyph, palette().color(QPalette::ButtonText)));
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(24, 24);
        b->setAutoRaise(true);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setCheckable(checkable);
        named(b, name);
        connect(b, &QToolButton::clicked, this, [clicked] { clicked(); });
        return b;
    };
    auto caption = [this](const QString& text) {
        auto* l = new QLabel(text, this);
        QFont f = l->font();
        f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1));
        l->setFont(f);
        return l;
    };

    // The tools (p.303-306) and the arrangement (p.318-319): two columns of icons, as pictured on p.302; the keyframe
    // buttons (6.8c1) are ours, in a last row.
    auto* tools = new QGridLayout;
    tools->setSpacing(1);
    const QList<std::tuple<QString, QString, GlyphIcon, std::function<void()>, bool>> toolButtons = {
        {QStringLiteral("tool:text"), tr("Text Tool"), GlyphIcon::TextTool, [this] { tool_.setTool(TitlerTool::Tool::Text); }, true},
        {QStringLiteral("tool:selection"), tr("Selection Tool"), GlyphIcon::SelectionTool, [this] { tool_.setTool(TitlerTool::Tool::Selection); }, true},
        {QStringLiteral("tool:rectangle"), tr("Rectangle Tool"), GlyphIcon::RectangleTool, [this] { tool_.setTool(TitlerTool::Tool::Rectangle); }, true},
        {QStringLiteral("tool:ellipse"), tr("Ellipse Tool"), GlyphIcon::EllipseTool, [this] { tool_.setTool(TitlerTool::Tool::Ellipse); }, true},
        {QStringLiteral("forward"), tr("Bring Forward"), GlyphIcon::BringForward, [this] { tool_.bringForward(); }, false},
        {QStringLiteral("backward"), tr("Send Backward"), GlyphIcon::SendBackward, [this] { tool_.sendBackward(); }, false},
        {QStringLiteral("group"), tr("Group"), GlyphIcon::Group, [this] { tool_.group(); }, false},
        {QStringLiteral("ungroup"), tr("Ungroup"), GlyphIcon::Ungroup, [this] { tool_.ungroup(); }, false},
        {QStringLiteral("previous"), tr("Select Previous Object"), GlyphIcon::PreviousObject, [this] { tool_.selectAdjacent(false); }, false},
        {QStringLiteral("next"), tr("Select Next Object"), GlyphIcon::NextObject, [this] { tool_.selectAdjacent(true); }, false},
        {QStringLiteral("grid"), tr("Grid"), GlyphIcon::Grid, [this] { tool_.setGridShown(!tool_.gridShown()); }, true},
        {QString(), QString(), GlyphIcon::Grid, {}, false},
        // Keyframes on the selection, or on the whole title with nothing selected (6.8c1): the title's numbers are
        // keyframed through the Effect Editor (p.320).
        {QStringLiteral("addKeyframe"), tr("Add Keyframe"), GlyphIcon::AddKeyframe, [this] { tool_.setKeyframes(true); }, false},
        {QStringLiteral("deleteKeyframe"), tr("Delete Keyframe"), GlyphIcon::DeleteKeyframe, [this] { tool_.setKeyframes(false); }, false}};
    // In the Effect Editor the same icons run in rows of seven (its Toolbar, p.318-319).
    const int columns = across ? 2 : 7;
    int at = 0;
    for (const auto& [name, tip, glyph, clicked, checkable] : toolButtons) {
        if (!name.isEmpty()) tools->addWidget(button(name, tip, glyph, clicked, checkable), at / columns, at % columns);
        if (!name.isEmpty() || across) ++at;
    }
    if (across) tools->setRowStretch(tools->rowCount(), 1);
    else tools->setColumnStretch(columns, 1);
    outer->addLayout(tools);

    // The Effect Editor's Title Text (p.321).
    if (!across) {
        text_ = new QPlainTextEdit(this);
        text_->setMaximumHeight(60);
        named(text_, QStringLiteral("text"));
        outer->addWidget(text_);
        text_->installEventFilter(new FocusOut(this, [this] {
            const QString text = text_->toPlainText();
            tool_.modifySelected(tr("Title Text"), [&](TitleObject& o) {
                if (o.kind == TitleObjectKind::Text) o.text = text;
            });
        }));
    }

    // ---- the controls
    // Font (p.306-310, p.321-322).
    auto* family = new QFontComboBox(this);
    named(family, QStringLiteral("family"));
    family->setToolTip(tr("Font Family"));
    // Sized for about 16 characters, not the longest installed font's name (which made the Dashboard wider on Windows,
    // where many fonts are installed; CI run 347).
    family->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    family->setMinimumContentsLength(16);
    connect(family, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) {
        if (!syncing_) tool_.modifySelected(tr("Font Family"), [&](TitleObject& o) { o.font.family = f.family(); });
    });
    QComboBox* style = menu(QStringLiteral("style"), {}, [this](int) {
        const auto* box = qobject_cast<QComboBox*>(controls_.value(QStringLiteral("style")));
        const QString chosen = box ? box->currentText() : QString();
        tool_.modifySelected(tr("Font Style"), [&](TitleObject& o) { o.font.style = chosen; });
    });
    style->setToolTip(tr("Font Style"));
    QDoubleSpinBox* size = number(QStringLiteral("size"), tr("Size"), 1, 2000, 0, [](TitleObject& o, double v) { o.font.size = v; });
    // "Click the Underline button to the right of the Font Style submenu in the dashboard, or the Underline checkbox
    // beneath the Size slider in the Effect Editor" (p.307).
    QAbstractButton* underline = check(QStringLiteral("underline"), tr("Underline"), [](TitleObject& o, bool on) { o.font.underline = on; }, {},
                                       GlyphIcon::Underline);
    auto* justify = new QHBoxLayout;
    justify->setSpacing(1);
    const QList<std::tuple<QString, model::HorizontalJustify, GlyphIcon>> horizontals = {
        {QStringLiteral("left"), model::HorizontalJustify::Left, GlyphIcon::JustifyLeft},
        {QStringLiteral("center"), model::HorizontalJustify::Center, GlyphIcon::JustifyCenter},
        {QStringLiteral("right"), model::HorizontalJustify::Right, GlyphIcon::JustifyRight}};
    for (const auto& [name, which, glyph] : horizontals) {
        const model::HorizontalJustify j = which;
        justify->addWidget(button(QStringLiteral("justify:") + name, tr("Horizontal Justify %1").arg(name), glyph, [this, j] {
            tool_.modifySelected(tr("Justification"), [&](TitleObject& o) { o.font.horizontal = j; });
        }, true));
    }
    QComboBox* vertical = menu(QStringLiteral("vertical"), {tr("Vertical Top"), tr("Vertical Center"), tr("Vertical Bottom")}, [this](int i) {
        tool_.modifySelected(tr("Justification"), [&](TitleObject& o) { o.font.vertical = static_cast<model::VerticalJustify>(i); });
    });
    QComboBox* orientation = menu(QStringLiteral("orientation"), {tr("Left To Right"), tr("Right To Left"), tr("Top To Bottom")}, [this](int i) {
        tool_.modifySelected(tr("Font Orientation"), [&](TitleObject& o) { o.font.orientation = static_cast<model::FontOrientation>(i); });
    });
    QDoubleSpinBox* kerning = number(QStringLiteral("kerning"), tr("Kerning"), -200, 1000, 1, [](TitleObject& o, double v) { o.font.kerning = v; });
    QDoubleSpinBox* leading = number(QStringLiteral("leading"), tr("Leading"), -500, 1000, 1, [](TitleObject& o, double v) { o.font.leading = v; });
    QDoubleSpinBox* skew = number(QStringLiteral("skew"), tr("Skew"), -60, 60, 1, [](TitleObject& o, double v) { o.font.skew = v; });

    // Transform (p.310-314, p.322).
    QDoubleSpinBox* opacity = number(QStringLiteral("opacity"), tr("Opacity"), 0, 100, 0, [](TitleObject& o, double v) { o.transform.opacity = v; });
    QDoubleSpinBox* x = number(QStringLiteral("x"), tr("Position X"), -10000, 10000, 1, [](TitleObject& o, double v) { o.transform.position.setX(v); });
    QDoubleSpinBox* y = number(QStringLiteral("y"), tr("Position Y"), -10000, 10000, 1, [](TitleObject& o, double v) { o.transform.position.setY(v); });
    // "Horizontal and vertical controls are available separately, or as a fixed aspect ratio with the Scale Fixed
    // Aspect checkbox" (p.322); the Dashboard's lock between Scale X and Y (the picture on p.302).
    QDoubleSpinBox* scaleX = number(QStringLiteral("scaleX"), tr("Scale X"), 1, 10000, 1, [](TitleObject& o, double v) {
        if (o.transform.fixedAspect && o.transform.scaleX > 0) o.transform.scaleY *= v / o.transform.scaleX;
        o.transform.scaleX = v;
    });
    QDoubleSpinBox* scaleY = number(QStringLiteral("scaleY"), tr("Scale Y"), 1, 10000, 1, [](TitleObject& o, double v) {
        if (o.transform.fixedAspect && o.transform.scaleY > 0) o.transform.scaleX *= v / o.transform.scaleY;
        o.transform.scaleY = v;
    });
    QAbstractButton* fixedAspect = check(QStringLiteral("fixedAspect"), tr("Scale Fixed Aspect"), [](TitleObject& o, bool on) { o.transform.fixedAspect = on; },
                                         {}, GlyphIcon::Lock);
    QDoubleSpinBox* rotation = number(QStringLiteral("rotation"), tr("Rotation"), -3600, 3600, 1, [](TitleObject& o, double v) { o.transform.rotation = v; });
    static constexpr TitlerTool::Align horizontalAligns[] = {TitlerTool::Align::Left, TitlerTool::Align::Center, TitlerTool::Align::Right,
                                                             TitlerTool::Align::HorizontalCenter};
    static constexpr TitlerTool::Align verticalAligns[] = {TitlerTool::Align::Top, TitlerTool::Align::Middle, TitlerTool::Align::Bottom,
                                                           TitlerTool::Align::VerticalCenter};
    QComboBox* alignH = menu(QStringLiteral("alignH"), {QString(), tr("Align Left"), tr("Align Center"), tr("Align Right"), tr("Align Horizontal Center")},
                             [this](int i) {
                                 if (i > 0) tool_.align(horizontalAligns[i - 1]);
                                 refresh();
                             });
    QComboBox* alignV = menu(QStringLiteral("alignV"), {QString(), tr("Align Top"), tr("Align Center"), tr("Align Bottom"), tr("Align Vertical Center")},
                             [this](int i) {
                                 if (i > 0) tool_.align(verticalAligns[i - 1]);
                                 refresh();
                             });
    QComboBox* anchor = menu(QStringLiteral("anchor"),
                             {tr("Center"), tr("Right"), tr("Left"), tr("Top Center"), tr("Top Right"), tr("Top Left"), tr("Bottom Center"),
                              tr("Bottom Right"), tr("Bottom Left"), tr("Custom")},
                             [this](int i) {
                                 if (i <= static_cast<int>(TitlerTool::Anchor::BottomLeft)) tool_.setAnchor(static_cast<TitlerTool::Anchor>(i));
                                 refresh();
                             });
    QDoubleSpinBox* anchorX = number(QStringLiteral("anchorX"), tr("Horizontal Position"), -10000, 10000, 1, [](TitleObject& o, double v) { o.transform.anchor.setX(v); });
    QDoubleSpinBox* anchorY = number(QStringLiteral("anchorY"), tr("Vertical Position"), -10000, 10000, 1, [](TitleObject& o, double v) { o.transform.anchor.setY(v); });

    // Style: Fill, Outline, Shadow (p.314-317, p.322-324).
    QAbstractButton* fillOn = check(QStringLiteral("fill"), tr("Fill"), [](TitleObject& o, bool on) { o.fill.on = on; });
    QToolButton* fillColor = swatch(QStringLiteral("fill"), tr("Solid Color"));
    QAbstractButton* gradient = check(QStringLiteral("gradient"), tr("Color Gradient"), [](TitleObject& o, bool on) { o.fill.color.gradient = on; });
    QToolButton* corners[4] = {swatch(QStringLiteral("fill0"), tr("Top Left Color")), swatch(QStringLiteral("fill1"), tr("Top Right Color")),
                               swatch(QStringLiteral("fill2"), tr("Bottom Left Color")), swatch(QStringLiteral("fill3"), tr("Bottom Right Color"))};
    QDoubleSpinBox* fillOpacity = number(QStringLiteral("fillOpacity"), tr("Fill Opacity"), 0, 100, 0, [](TitleObject& o, double v) { o.fill.opacity = v; });
    QDoubleSpinBox* fillSoftness = number(QStringLiteral("fillSoftness"), tr("Fill Softness"), 0, 200, 1, [](TitleObject& o, double v) { o.fill.softness = v; });
    QAbstractButton* outlineOn = check(QStringLiteral("outline"), tr("Outline"), [](TitleObject& o, bool on) { o.outline.on = on; });
    QToolButton* outlineColor = swatch(QStringLiteral("outline"), tr("Outline Solid Color"));
    QDoubleSpinBox* outlineOpacity = number(QStringLiteral("outlineOpacity"), tr("Opacity"), 0, 100, 0, [](TitleObject& o, double v) { o.outline.opacity = v; });
    QDoubleSpinBox* outlineSize = number(QStringLiteral("outlineSize"), tr("Size"), 0, 500, 1, [](TitleObject& o, double v) { o.outline.size = v; });
    QDoubleSpinBox* outlineSoftness = number(QStringLiteral("outlineSoftness"), tr("Softness"), 0, 200, 1, [](TitleObject& o, double v) { o.outline.softness = v; });
    QComboBox* outlineType = menu(QStringLiteral("outlineType"), {tr("Outer"), tr("Inner"), tr("Border")}, [this](int i) {
        tool_.modifySelected(tr("Outline"), [&](TitleObject& o) { o.outline.type = static_cast<model::OutlineType>(i); });
    });
    QComboBox* outlineShape = menu(QStringLiteral("outlineShape"), {tr("Round"), tr("Square")}, [this](int i) {
        tool_.modifySelected(tr("Outline"), [&](TitleObject& o) { o.outline.shape = static_cast<model::OutlineShape>(i); });
    });
    QAbstractButton* shadowOn = check(QStringLiteral("shadow"), tr("Shadow"), [](TitleObject& o, bool on) { o.shadow.on = on; });
    QToolButton* shadowColor = swatch(QStringLiteral("shadow"), tr("Shadow Solid Color"));
    QDoubleSpinBox* shadowOpacity = number(QStringLiteral("shadowOpacity"), tr("Opacity"), 0, 100, 0, [](TitleObject& o, double v) { o.shadow.opacity = v; });
    QDoubleSpinBox* shadowDistance = number(QStringLiteral("shadowDistance"), tr("Distance"), 0, 1000, 1, [](TitleObject& o, double v) { o.shadow.distance = v; });
    QDoubleSpinBox* shadowAngle = number(QStringLiteral("shadowAngle"), tr("Angle"), 0, 360, 0, [](TitleObject& o, double v) { o.shadow.angle = v; });
    QDoubleSpinBox* shadowSoftness = number(QStringLiteral("shadowSoftness"), tr("Softness"), 0, 200, 1, [](TitleObject& o, double v) { o.shadow.softness = v; });

    // The Global Group Parameters (p.317, p.324).
    QAbstractButton* backgroundOn = check(QStringLiteral("background"), tr("Background"), {}, [](Title& t, bool on) { t.background.on = on; });
    QToolButton* backgroundColor = swatch(QStringLiteral("background"), tr("Background Solid Color"));
    QDoubleSpinBox* backgroundOpacity = number(QStringLiteral("backgroundOpacity"), tr("Opacity"), 0, 100, 0, {}, [](Title& t, double v) { t.background.opacity = v; });
    QDoubleSpinBox* fillOverlay = number(QStringLiteral("fillOverlay"), tr("Fill Overlay"), 0, 100, 0, {}, [](Title& t, double v) { t.foreground.fillOverlay = v; });
    QDoubleSpinBox* foregroundOpacity = number(QStringLiteral("foregroundOpacity"), tr("Opacity"), 0, 100, 0, {}, [](Title& t, double v) { t.foreground.opacity = v; });
    QDoubleSpinBox* crops[4] = {number(QStringLiteral("cropTop"), tr("Top"), 0, 100, 1, {}, [](Title& t, double v) { t.foreground.cropTop = v; }),
                                number(QStringLiteral("cropBottom"), tr("Bottom"), 0, 100, 1, {}, [](Title& t, double v) { t.foreground.cropBottom = v; }),
                                number(QStringLiteral("cropLeft"), tr("Left"), 0, 100, 1, {}, [](Title& t, double v) { t.foreground.cropLeft = v; }),
                                number(QStringLiteral("cropRight"), tr("Right"), 0, 100, 1, {}, [](Title& t, double v) { t.foreground.cropRight = v; })};
    // Roll / Crawl (p.324-325, 6.8c3): through the tool, turning it on also gives the Effect Position its linear
    // keyframes (p.325). A check box in both layouts: it is not in the Dashboard.
    auto* rollCrawl = new QCheckBox(tr("Roll / Crawl"), this);
    named(rollCrawl, QStringLiteral("rollCrawl"));
    connect(rollCrawl, &QCheckBox::toggled, this, [this](bool on) {
        if (!syncing_) tool_.setRollCrawl(on);
    });
    QComboBox* rollType = menu(QStringLiteral("rollType"), {tr("Roll"), tr("Crawl Left"), tr("Crawl Right")}, [this](int i) {
        tool_.modifyTitle(tr("Effect Type"), [&](Title& t) { t.rollCrawl.type = static_cast<model::RollCrawlType>(i); });
    });
    QDoubleSpinBox* effectPosition = number(QStringLiteral("effectPosition"), tr("Effect Position"), 0, 100, 1, {}, [](Title& t, double v) { t.rollCrawl.position = v; });
    auto* editMode = new QCheckBox(tr("Edit Mode"), this);
    named(editMode, QStringLiteral("editMode"));
    connect(editMode, &QCheckBox::toggled, this, [this](bool on) {
        if (!syncing_) tool_.modifyTitle(tr("Edit Mode"), [&](Title& t) { t.rollCrawl.editMode = on; });
    });
    // Tracking for the Selected Object (p.326).
    auto* trackingOn = new QCheckBox(tr("Tracking"), this);
    named(trackingOn, QStringLiteral("tracking"));
    connect(trackingOn, &QCheckBox::toggled, this, [this](bool on) {
        if (!syncing_) tool_.modifySelected(tr("Tracking"), [&](TitleObject& o) { o.tracking = on; });
    });
    QComboBox* tracker = menu(QStringLiteral("tracker"), {}, [this](int) {
        const auto* box = qobject_cast<QComboBox*>(controls_.value(QStringLiteral("tracker")));
        const QString name = box ? box->currentText() : QString();
        tool_.modifySelected(tr("Tracker"), [&](TitleObject& o) {
            o.tracker = name;
            o.tracking = !name.isEmpty();
        });
    });

    if (across) {
        // ---- the Dashboard (p.302): Font, Transform and Style side by side.
        auto grid = [&](QWidget* box) {
            auto* g = new QGridLayout(box);
            g->setContentsMargins(4, 4, 4, 4);
            g->setHorizontalSpacing(4);
            g->setVerticalSpacing(2);
            return g;
        };
        QGridLayout* f = grid(section(tr("Font"), outer));
        f->addWidget(family, 0, 0, 1, 4);
        f->addWidget(style, 1, 0, 1, 3);
        f->addWidget(underline, 1, 3);
        f->addWidget(caption(tr("Size")), 2, 0);
        f->addWidget(size, 2, 1);
        f->addLayout(justify, 2, 2, 1, 2);
        int row = 3;
        for (const auto& [label, box] : {std::pair{tr("Kerning"), kerning}, std::pair{tr("Leading"), leading}, std::pair{tr("Skew"), skew}}) {
            f->addWidget(caption(label), row, 0);
            f->addWidget(box, row++, 1);
        }
        f->setRowStretch(row, 1);
        // A Font section for the Dashboard's controls only; vertical justification and orientation are the Effect
        // Editor's (p.321).
        held->addWidget(vertical);
        held->addWidget(orientation);

        QGridLayout* t = grid(section(tr("Transform"), outer));
        t->addWidget(caption(tr("Opacity")), 0, 0);
        t->addWidget(opacity, 0, 2);
        t->addWidget(caption(tr("Position X, Y")), 1, 0);
        t->addWidget(x, 1, 1);
        t->addWidget(y, 1, 2);
        t->addWidget(caption(tr("Scale X, Y")), 2, 0);
        auto* scaleRow = new QHBoxLayout;
        scaleRow->setSpacing(1);
        scaleRow->addWidget(scaleX);
        scaleRow->addWidget(fixedAspect);
        t->addLayout(scaleRow, 2, 1);
        t->addWidget(scaleY, 2, 2);
        t->addWidget(caption(tr("Rotation")), 3, 0);
        t->addWidget(rotation, 3, 2);
        // "Align H." and "Align V.": four buttons each (the picture on p.302).
        for (int v = 0; v < 2; ++v) {
            t->addWidget(caption(v == 0 ? tr("Align H.") : tr("Align V.")), 4 + v, 0);
            auto* aligns = new QHBoxLayout;
            aligns->setSpacing(1);
            static const GlyphIcon glyphs[2][4] = {
                {GlyphIcon::AlignLeft, GlyphIcon::AlignHorizontalCenter, GlyphIcon::AlignRight, GlyphIcon::AlignFrameHorizontal},
                {GlyphIcon::AlignTop, GlyphIcon::AlignVerticalCenter, GlyphIcon::AlignBottom, GlyphIcon::AlignFrameVertical}};
            QComboBox* menuOf = v == 0 ? alignH : alignV;
            for (int i = 0; i < 4; ++i) {
                const TitlerTool::Align kind = v == 0 ? horizontalAligns[i] : verticalAligns[i];
                aligns->addWidget(button(QStringLiteral("align%1:%2").arg(v == 0 ? QLatin1Char('H') : QLatin1Char('V')).arg(i),
                                         menuOf->itemText(i + 1), glyphs[v][i], [this, kind] {
                                             tool_.align(kind);
                                             refresh();
                                         }));
            }
            t->addLayout(aligns, 4 + v, 1, 1, 2);
        }
        t->setRowStretch(6, 1);
        for (QWidget* w : std::initializer_list<QWidget*>{alignH, alignV, anchor, anchorX, anchorY}) held->addWidget(w);

        // Style: a row each for Fill, Outline, Shadow and BG (the picture on p.302); the gradient corners and the
        // outline's type and shape are the Effect Editor's.
        auto* styleColumn = new QVBoxLayout;
        styleColumn->setSpacing(2);
        auto styleRow = [&](const QString& title, QAbstractButton* on, QToolButton* color, QDoubleSpinBox* amount,
                            QList<std::pair<QString, QWidget*>> more) {
            QWidget* box = section(title, styleColumn);
            auto* r = new QHBoxLayout(box);
            r->setContentsMargins(4, 2, 4, 2);
            r->setSpacing(4);
            on->setText(title == tr("Background") ? tr("BG") : title);
            r->addWidget(on);
            r->addWidget(color);
            r->addWidget(amount);
            r->addWidget(caption(QStringLiteral("%")));
            for (const auto& [label, w] : more) {
                r->addWidget(caption(label));
                r->addWidget(w);
            }
            r->addStretch(1);
        };
        styleRow(tr("Fill"), fillOn, fillColor, fillOpacity, {{tr("Soft."), fillSoftness}});
        styleRow(tr("Outline"), outlineOn, outlineColor, outlineOpacity, {{tr("Size"), outlineSize}, {tr("Soft."), outlineSoftness}});
        styleRow(tr("Shadow"), shadowOn, shadowColor, shadowOpacity,
                 {{tr("Dist."), shadowDistance}, {tr("Ang."), shadowAngle}, {tr("Soft."), shadowSoftness}});
        styleRow(tr("Background"), backgroundOn, backgroundColor, backgroundOpacity, {});
        styleColumn->addStretch(1);
        outer->addLayout(styleColumn);
        held->addWidget(gradient);
        for (QWidget* w : corners) held->addWidget(w);
        held->addWidget(outlineType);
        held->addWidget(outlineShape);
        // The Effect Editor's Foreground, Effect and Tracking categories (p.321) are not on the Dashboard.
        for (const QString& title : {tr("Foreground"), tr("Roll / Crawl"), tr("Tracking")}) section(title, held);
        for (QWidget* w : std::initializer_list<QWidget*>{fillOverlay, foregroundOpacity, crops[0], crops[1], crops[2], crops[3], rollCrawl, rollType,
                                                          effectPosition, editMode, trackingOn, tracker}) {
            held->addWidget(w);
        }
    } else {
        // ---- the Effect Editor (p.321): categories with disclosure triangles.
        QFormLayout* f = formIn(section(tr("Font"), outer), tr("Font"));
        f->addRow(tr("Font Family"), family);
        f->addRow(tr("Font Style"), style);
        f->addRow(tr("Size"), size);
        f->addRow(underline);
        f->addRow(tr("Justification Horizontal"), justify);
        f->addRow(tr("Justification Vertical"), vertical);
        f->addRow(tr("Font Orientation"), orientation);
        f->addRow(tr("Kerning"), kerning);
        f->addRow(tr("Leading"), leading);
        f->addRow(tr("Skew"), skew);
        QFormLayout* t = formIn(section(tr("Transform"), outer), tr("Transform"));
        t->addRow(tr("Opacity"), opacity);
        t->addRow(tr("Position X"), x);
        t->addRow(tr("Position Y"), y);
        t->addRow(tr("Scale X"), scaleX);
        t->addRow(tr("Scale Y"), scaleY);
        t->addRow(fixedAspect);
        t->addRow(tr("Rotation"), rotation);
        t->addRow(tr("Align H."), alignH);
        t->addRow(tr("Align V."), alignV);
        t->addRow(tr("Anchor Point"), anchor);
        t->addRow(tr("Horizontal Position"), anchorX);
        t->addRow(tr("Vertical Position"), anchorY);
        QFormLayout* fill = formIn(section(tr("Fill"), outer), tr("Fill"));
        fill->addRow(fillOn);
        fill->addRow(tr("Solid Color"), fillColor);
        fill->addRow(gradient);
        fill->addRow(tr("Top Left Color"), corners[0]);
        fill->addRow(tr("Top Right Color"), corners[1]);
        fill->addRow(tr("Bottom Left Color"), corners[2]);
        fill->addRow(tr("Bottom Right Color"), corners[3]);
        fill->addRow(tr("Fill Opacity"), fillOpacity);
        fill->addRow(tr("Fill Softness"), fillSoftness);
        QFormLayout* outline = formIn(section(tr("Outline"), outer), tr("Outline"));
        outline->addRow(outlineOn);
        outline->addRow(tr("Outline Solid Color"), outlineColor);
        outline->addRow(tr("Opacity"), outlineOpacity);
        outline->addRow(tr("Size"), outlineSize);
        outline->addRow(tr("Softness"), outlineSoftness);
        outline->addRow(tr("Type"), outlineType);
        outline->addRow(tr("Shape"), outlineShape);
        QFormLayout* shadow = formIn(section(tr("Shadow"), outer), tr("Shadow"));
        shadow->addRow(shadowOn);
        shadow->addRow(tr("Shadow Solid Color"), shadowColor);
        shadow->addRow(tr("Opacity"), shadowOpacity);
        shadow->addRow(tr("Distance"), shadowDistance);
        shadow->addRow(tr("Angle"), shadowAngle);
        shadow->addRow(tr("Softness"), shadowSoftness);
        QFormLayout* background = formIn(section(tr("Background"), outer), tr("Background"));
        background->addRow(backgroundOn);
        background->addRow(tr("Background Solid Color"), backgroundColor);
        background->addRow(tr("Opacity"), backgroundOpacity);
        QFormLayout* foreground = formIn(section(tr("Foreground"), outer), tr("Foreground"));
        foreground->addRow(tr("Fill Overlay"), fillOverlay);
        foreground->addRow(tr("Opacity"), foregroundOpacity);
        foreground->addRow(tr("Top"), crops[0]);
        foreground->addRow(tr("Bottom"), crops[1]);
        foreground->addRow(tr("Left"), crops[2]);
        foreground->addRow(tr("Right"), crops[3]);
        QFormLayout* roll = formIn(section(tr("Roll / Crawl"), outer), tr("Roll / Crawl"));
        roll->addRow(rollCrawl);
        roll->addRow(tr("Effect Type"), rollType);
        roll->addRow(tr("Effect Position"), effectPosition);
        roll->addRow(editMode);
        QFormLayout* tracking = formIn(section(tr("Tracking"), outer), tr("Tracking"));
        tracking->addRow(trackingOn);
        tracking->addRow(tr("Tracker"), tracker);
        outer->addStretch(1);
    }

    connect(&tool_, &TitlerTool::changed, this, &TitlerPanel::refresh);
    refresh();
}

model::TitlePresetPart TitlerPanel::presetPartAt(const QPoint& at) const {
    for (const QWidget* w = childAt(at); w && w != this; w = w->parentWidget()) {
        const QVariant part = w->property("presetPart");
        if (part.isValid()) return static_cast<model::TitlePresetPart>(part.toInt());
    }
    return model::TitlePresetPart::All;
}

void TitlerPanel::dragEnterEvent(QDragEnterEvent* event) {
    dragMoveEvent(event); // a QDragEnterEvent is a QDragMoveEvent
}

void TitlerPanel::dragMoveEvent(QDragMoveEvent* event) {
    const auto t = tool_.droppedTemplate(event->mimeData());
    if (t && model::isTitlerEffect(t->effect) && !tool_.selection().isEmpty()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TitlerPanel::dropEvent(QDropEvent* event) {
    const auto t = tool_.droppedTemplate(event->mimeData());
    if (t && tool_.applyPreset(*t, presetPartAt(event->position().toPoint()))) event->acceptProposedAction();
}

QWidget* TitlerPanel::control(const QString& name) const {
    return controls_.value(name);
}

QString TitlerPanel::colorOf(const QString& which) const {
    const Title t = tool_.shownTitle();
    if (which == QLatin1String("background")) return t.background.color.color;
    const std::optional<TitleObject> shown = tool_.shownObject();
    const TitleObject* o = shown ? &*shown : nullptr;
    if (!o) return QStringLiteral("#ffffff");
    TitleObject copy = *o;
    int corner = -1;
    TitleColor* c = colorField(copy, which, &corner);
    return c ? cornerOf(*c, corner) : QStringLiteral("#ffffff");
}

bool TitlerPanel::chooseColor(const QString& which, const QColor& color) {
    const QString name = color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb);
    bool done = false;
    if (which == QLatin1String("background")) {
        done = tool_.modifyTitle(tr("Background Color"), [&](Title& t) { t.background.color.color = name; });
    } else {
        done = tool_.modifySelected(tr("Color"), [&](TitleObject& o) {
            int corner = -1;
            if (TitleColor* c = colorField(o, which, &corner)) {
                cornerOf(*c, corner) = name;
                // A solid colour sets the gradient's corners too, until they are set apart (ours).
                if (corner < 0 && !c->gradient) c->topLeft = c->topRight = c->bottomLeft = c->bottomRight = name;
            }
        });
    }
    refresh();
    return done;
}

void TitlerPanel::refresh() {
    syncing_ = true;
    const Title t = tool_.shownTitle();
    // The first selected object, or the highlighted characters' format (6.8c2).
    const std::optional<TitleObject> shown = tool_.shownObject();
    const TitleObject* o = shown ? &*shown : nullptr;
    const bool text = o && o->kind == TitleObjectKind::Text;
    auto set = [&](const QString& name, double v) {
        if (auto* box = qobject_cast<QDoubleSpinBox*>(controls_.value(name)); box && !box->hasFocus()) box->setValue(v);
    };
    auto tick = [&](const QString& name, bool on) {
        if (auto* box = qobject_cast<QAbstractButton*>(controls_.value(name))) box->setChecked(on);
    };
    auto swatchColor = [&](const QString& which) {
        if (auto* b = controls_.value(which + QStringLiteral("Color"))) b->setStyleSheet(QStringLiteral("background-color: %1;").arg(QColor(colorOf(which)).name()));
    };
    // The tool buttons, the sections that apply.
    tick(QStringLiteral("tool:selection"), tool_.tool() == TitlerTool::Tool::Selection);
    tick(QStringLiteral("tool:text"), tool_.tool() == TitlerTool::Tool::Text);
    tick(QStringLiteral("tool:rectangle"), tool_.tool() == TitlerTool::Tool::Rectangle);
    tick(QStringLiteral("tool:ellipse"), tool_.tool() == TitlerTool::Tool::Ellipse);
    tick(QStringLiteral("grid"), tool_.gridShown());
    // "Available parameters in the Effect Editor will also change, depending on the currently selected object" (p.320).
    for (const QString& s : {tr("Font")}) {
        if (QWidget* w = controls_.value(QStringLiteral("section:") + s)) w->setEnabled(text);
    }
    for (const QString& s : {tr("Transform"), tr("Fill"), tr("Outline"), tr("Shadow"), tr("Tracking")}) {
        if (QWidget* w = controls_.value(QStringLiteral("section:") + s)) w->setEnabled(o != nullptr);
    }
    if (text_) {
        text_->setEnabled(text);
        if (!text_->hasFocus()) text_->setPlainText(text ? o->text : QString());
    }
    if (o) {
        if (text) {
            if (auto* family = qobject_cast<QFontComboBox*>(controls_.value(QStringLiteral("family")))) {
                family->setCurrentFont(QFont(o->font.family.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::GeneralFont).family() : o->font.family));
                if (auto* style = qobject_cast<QComboBox*>(controls_.value(QStringLiteral("style")))) {
                    style->clear();
                    style->addItems(QFontDatabase::styles(family->currentFont().family()));
                    style->setCurrentText(o->font.style);
                }
            }
            set(QStringLiteral("size"), o->font.size);
            tick(QStringLiteral("underline"), o->font.underline);
            tick(QStringLiteral("justify:left"), o->font.horizontal == model::HorizontalJustify::Left);
            tick(QStringLiteral("justify:center"), o->font.horizontal == model::HorizontalJustify::Center);
            tick(QStringLiteral("justify:right"), o->font.horizontal == model::HorizontalJustify::Right);
            if (auto* v = qobject_cast<QComboBox*>(controls_.value(QStringLiteral("vertical")))) v->setCurrentIndex(static_cast<int>(o->font.vertical));
            if (auto* v = qobject_cast<QComboBox*>(controls_.value(QStringLiteral("orientation")))) v->setCurrentIndex(static_cast<int>(o->font.orientation));
            set(QStringLiteral("kerning"), o->font.kerning);
            set(QStringLiteral("leading"), o->font.leading);
            set(QStringLiteral("skew"), o->font.skew);
        }
        set(QStringLiteral("opacity"), o->transform.opacity);
        set(QStringLiteral("x"), o->transform.position.x());
        set(QStringLiteral("y"), o->transform.position.y());
        set(QStringLiteral("scaleX"), o->transform.scaleX);
        set(QStringLiteral("scaleY"), o->transform.scaleY);
        tick(QStringLiteral("fixedAspect"), o->transform.fixedAspect);
        set(QStringLiteral("rotation"), o->transform.rotation);
        set(QStringLiteral("anchorX"), o->transform.anchor.x());
        set(QStringLiteral("anchorY"), o->transform.anchor.y());
        if (auto* a = qobject_cast<QComboBox*>(controls_.value(QStringLiteral("anchor")))) {
            // "Custom (appears automatically after manually moving the sliders)" (p.313).
            const auto preset = tool_.anchorOf(*o);
            a->setCurrentIndex(preset ? static_cast<int>(*preset) : a->count() - 1);
        }
        tick(QStringLiteral("fill"), o->fill.on);
        tick(QStringLiteral("gradient"), o->fill.color.gradient);
        set(QStringLiteral("fillOpacity"), o->fill.opacity);
        set(QStringLiteral("fillSoftness"), o->fill.softness);
        tick(QStringLiteral("outline"), o->outline.on);
        set(QStringLiteral("outlineOpacity"), o->outline.opacity);
        set(QStringLiteral("outlineSize"), o->outline.size);
        set(QStringLiteral("outlineSoftness"), o->outline.softness);
        if (auto* v = qobject_cast<QComboBox*>(controls_.value(QStringLiteral("outlineType")))) v->setCurrentIndex(static_cast<int>(o->outline.type));
        if (auto* v = qobject_cast<QComboBox*>(controls_.value(QStringLiteral("outlineShape")))) v->setCurrentIndex(static_cast<int>(o->outline.shape));
        tick(QStringLiteral("shadow"), o->shadow.on);
        set(QStringLiteral("shadowOpacity"), o->shadow.opacity);
        set(QStringLiteral("shadowDistance"), o->shadow.distance);
        set(QStringLiteral("shadowAngle"), o->shadow.angle);
        set(QStringLiteral("shadowSoftness"), o->shadow.softness);
        for (const QString& which : {QStringLiteral("fill"), QStringLiteral("fill0"), QStringLiteral("fill1"), QStringLiteral("fill2"), QStringLiteral("fill3"),
                                     QStringLiteral("outline"), QStringLiteral("shadow")}) {
            swatchColor(which);
        }
        for (int k = 0; k < 4; ++k) {
            if (QWidget* w = controls_.value(QStringLiteral("fill%1Color").arg(k))) w->setEnabled(o->fill.color.gradient);
        }
    }
    // Roll / Crawl and the tracked object's tracker.
    tick(QStringLiteral("rollCrawl"), t.rollCrawl.on);
    if (auto* v = qobject_cast<QComboBox*>(controls_.value(QStringLiteral("rollType")))) v->setCurrentIndex(static_cast<int>(t.rollCrawl.type));
    set(QStringLiteral("effectPosition"), t.rollCrawl.position);
    tick(QStringLiteral("editMode"), t.rollCrawl.editMode);
    for (const QString& name : {QStringLiteral("rollType"), QStringLiteral("effectPosition"), QStringLiteral("editMode")}) {
        if (QWidget* w = controls_.value(name)) w->setEnabled(t.rollCrawl.on);
    }
    if (auto* trackers = qobject_cast<QComboBox*>(controls_.value(QStringLiteral("tracker")))) {
        trackers->clear();
        trackers->addItem(QString());
        trackers->addItems(tool_.trackerNames());
        trackers->setCurrentText(o ? o->tracker : QString());
    }
    tick(QStringLiteral("tracking"), o && o->tracking);
    tick(QStringLiteral("background"), t.background.on);
    swatchColor(QStringLiteral("background"));
    set(QStringLiteral("backgroundOpacity"), t.background.opacity);
    set(QStringLiteral("fillOverlay"), t.foreground.fillOverlay);
    set(QStringLiteral("foregroundOpacity"), t.foreground.opacity);
    set(QStringLiteral("cropTop"), t.foreground.cropTop);
    set(QStringLiteral("cropBottom"), t.foreground.cropBottom);
    set(QStringLiteral("cropLeft"), t.foreground.cropLeft);
    set(QStringLiteral("cropRight"), t.foreground.cropRight);
    for (const QString& name : {QStringLiteral("alignH"), QStringLiteral("alignV")}) {
        if (auto* box = qobject_cast<QComboBox*>(controls_.value(name))) box->setCurrentIndex(0);
    }
    syncing_ = false;
}

TitlerDashboard::TitlerDashboard(TitlerTool& tool, QWidget* parent) : QWidget(parent, Qt::Tool) {
    setObjectName(QStringLiteral("titlerDashboard"));
    setWindowTitle(tr("Title Designer"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("titlerDashboardScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    panel_ = new TitlerPanel(tool, true, scroll);
    scroll->setWidget(panel_);
    scroller_ = scroll;
    layout->addWidget(scroll, 1);
    // "At the bottom left, you'll find options to: Save: Update the current template with new changes. Save As:
    // Create a duplicate with the new changes" (p.319-320).
    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(4, 0, 4, 4);
    for (const bool as : {false, true}) {
        auto* b = new QPushButton(as ? tr("Save As") : tr("Save"), this);
        b->setObjectName(as ? QStringLiteral("titler:saveAs") : QStringLiteral("titler:save"));
        connect(b, &QPushButton::clicked, this, [this, as] { emit saveRequested(as); });
        buttons->addWidget(b);
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);
}

void TitlerDashboard::placeNear(const QPoint& at) {
    if (placed_) return;
    placed_ = true;
    // The controls' own size, with room for a horizontal scroll bar and the Save buttons below.
    const auto* scroll = static_cast<QScrollArea*>(scroller_);
    const QSize content = panel_->sizeHint();
    const int bar = scroll->horizontalScrollBar()->sizeHint().height();
    const int below = sizeHint().height() - scroll->sizeHint().height();
    setGeometry(fitOnScreen(QRect(at, QSize(content.width(), content.height() + bar + std::max(below, 0)))));
}

} // namespace lumenis::ui
