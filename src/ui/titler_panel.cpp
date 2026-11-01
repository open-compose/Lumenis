// The Titler+ Dashboard and the Effect Editor's Titler+ controls (titler_panel.h; FX Guide p.302-324;
// docs/spec/titler.md). Each control reads the first selected object (or the title) and writes through TitlerTool.
#include "lumenis/ui/titler_panel.h"

#include "lumenis/ui/titler_tool.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

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
    auto* outer = across ? static_cast<QBoxLayout*>(new QHBoxLayout(this)) : static_cast<QBoxLayout*>(new QVBoxLayout(this));
    outer->setContentsMargins(2, 2, 2, 2);
    outer->setSpacing(4);
    auto section = [&](const QString& title) {
        auto* box = new QGroupBox(title, this);
        auto* form = new QFormLayout(box);
        form->setContentsMargins(4, 4, 4, 4);
        form->setSpacing(2);
        outer->addWidget(box);
        controls_.insert(QStringLiteral("section:") + title, box);
        return form;
    };
    auto named = [this](QWidget* w, const QString& name) {
        w->setObjectName(QStringLiteral("titler:") + name);
        controls_.insert(name, w);
        return w;
    };
    // A number field on the selected objects; `title` ones change the whole title.
    auto number = [&](QFormLayout* form, const QString& name, const QString& label, double min, double max, int decimals,
                      std::function<void(TitleObject&, double)> setObject, std::function<void(Title&, double)> setTitle = {}) {
        auto* box = new QDoubleSpinBox(this);
        box->setRange(min, max);
        box->setDecimals(decimals);
        box->setKeyboardTracking(false);
        named(box, name);
        form->addRow(label, box);
        // "click the ... number and type the desired value" (p.309-317): one undo step when the value is entered.
        connect(box, &QDoubleSpinBox::editingFinished, this, [this, box, label, setObject, setTitle] {
            if (syncing_) return;
            const double v = box->value();
            if (setTitle) tool_.modifyTitle(label, [&](Title& t) { setTitle(t, v); });
            else tool_.modifySelected(label, [&](TitleObject& o) { setObject(o, v); });
        });
        return box;
    };
    auto check = [&](QFormLayout* form, const QString& name, const QString& label, std::function<void(TitleObject&, bool)> setObject,
                     std::function<void(Title&, bool)> setTitle = {}) {
        auto* box = new QCheckBox(label, this);
        named(box, name);
        form->addRow(box);
        connect(box, &QCheckBox::toggled, this, [this, label, setObject, setTitle](bool on) {
            if (syncing_) return;
            if (setTitle) tool_.modifyTitle(label, [&](Title& t) { setTitle(t, on); });
            else tool_.modifySelected(label, [&](TitleObject& o) { setObject(o, on); });
        });
        return box;
    };
    auto menu = [&](QFormLayout* form, const QString& name, const QString& label, const QStringList& items, std::function<void(int)> chosen) {
        auto* box = new QComboBox(this);
        box->addItems(items);
        named(box, name);
        form->addRow(label, box);
        connect(box, &QComboBox::activated, this, [this, chosen](int i) {
            if (!syncing_) chosen(i);
        });
        return box;
    };
    auto swatch = [&](QFormLayout* form, const QString& which, const QString& label) {
        auto* b = new QToolButton(this);
        b->setFixedSize(28, 16);
        b->setToolTip(label);
        named(b, which + QStringLiteral("Color"));
        form->addRow(label, b);
        // "click the Fill Color Solid box ... The Select Color dialog opens" (p.315).
        connect(b, &QToolButton::clicked, this, [this, which, label] {
            const QColor c = QColorDialog::getColor(QColor(colorOf(which)), this, label, QColorDialog::ShowAlphaChannel);
            if (c.isValid()) chooseColor(which, c);
        });
        return b;
    };
    auto button = [&](QHBoxLayout* row, const QString& name, const QString& text, std::function<void()> clicked, bool checkable = false) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(text);
        b->setCheckable(checkable);
        named(b, name);
        row->addWidget(b);
        connect(b, &QToolButton::clicked, this, [clicked] { clicked(); });
        return b;
    };

    // The tools (p.303-306) and the arrangement (p.318-319).
    auto* tools = new QHBoxLayout;
    tools->setSpacing(1);
    button(tools, QStringLiteral("tool:selection"), tr("Selection Tool"), [this] { tool_.setTool(TitlerTool::Tool::Selection); }, true);
    button(tools, QStringLiteral("tool:text"), tr("Text Tool"), [this] { tool_.setTool(TitlerTool::Tool::Text); }, true);
    button(tools, QStringLiteral("tool:rectangle"), tr("Rectangle Tool"), [this] { tool_.setTool(TitlerTool::Tool::Rectangle); }, true);
    button(tools, QStringLiteral("tool:ellipse"), tr("Ellipse Tool"), [this] { tool_.setTool(TitlerTool::Tool::Ellipse); }, true);
    auto* arrange = new QHBoxLayout;
    arrange->setSpacing(1);
    button(arrange, QStringLiteral("forward"), tr("Bring Forward"), [this] { tool_.bringForward(); });
    button(arrange, QStringLiteral("backward"), tr("Send Backward"), [this] { tool_.sendBackward(); });
    button(arrange, QStringLiteral("group"), tr("Group"), [this] { tool_.group(); });
    button(arrange, QStringLiteral("ungroup"), tr("Ungroup"), [this] { tool_.ungroup(); });
    button(arrange, QStringLiteral("previous"), tr("Select Previous Object"), [this] { tool_.selectAdjacent(false); });
    button(arrange, QStringLiteral("next"), tr("Select Next Object"), [this] { tool_.selectAdjacent(true); });
    button(arrange, QStringLiteral("grid"), tr("Grid"), [this] { tool_.setGridShown(!tool_.gridShown()); }, true);
    // Keyframes on the selection, or on the whole title with nothing selected (6.8c1): the title's numbers are
    // keyframed through the Effect Editor (p.320).
    auto* keys = new QHBoxLayout;
    keys->setSpacing(1);
    button(keys, QStringLiteral("addKeyframe"), tr("Add Keyframe"), [this] { tool_.setKeyframes(true); });
    button(keys, QStringLiteral("deleteKeyframe"), tr("Delete Keyframe"), [this] { tool_.setKeyframes(false); });
    auto* head = new QVBoxLayout;
    head->addLayout(tools);
    head->addLayout(arrange);
    head->addLayout(keys);
    outer->addLayout(head);

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

    // Font (p.306-310, p.321-322).
    QFormLayout* font = section(tr("Font"));
    auto* family = new QFontComboBox(this);
    named(family, QStringLiteral("family"));
    font->addRow(tr("Font Family"), family);
    connect(family, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) {
        if (!syncing_) tool_.modifySelected(tr("Font Family"), [&](TitleObject& o) { o.font.family = f.family(); });
    });
    menu(font, QStringLiteral("style"), tr("Font Style"), {}, [this](int) {
        const auto* box = qobject_cast<QComboBox*>(controls_.value(QStringLiteral("style")));
        const QString style = box ? box->currentText() : QString();
        tool_.modifySelected(tr("Font Style"), [&](TitleObject& o) { o.font.style = style; });
    });
    number(font, QStringLiteral("size"), tr("Size"), 1, 2000, 0, [](TitleObject& o, double v) { o.font.size = v; });
    check(font, QStringLiteral("underline"), tr("Underline"), [](TitleObject& o, bool on) { o.font.underline = on; });
    auto* justify = new QHBoxLayout;
    const QList<std::pair<QString, model::HorizontalJustify>> horizontals = {
        {QStringLiteral("left"), model::HorizontalJustify::Left}, {QStringLiteral("center"), model::HorizontalJustify::Center},
        {QStringLiteral("right"), model::HorizontalJustify::Right}};
    for (const auto& [name, which] : horizontals) {
        const model::HorizontalJustify j = which;
        button(justify, QStringLiteral("justify:") + name, tr("Horizontal Justify %1").arg(name), [this, j] {
            tool_.modifySelected(tr("Justification"), [&](TitleObject& o) { o.font.horizontal = j; });
        }, true);
    }
    font->addRow(tr("Justification Horizontal"), justify);
    menu(font, QStringLiteral("vertical"), tr("Justification Vertical"), {tr("Vertical Top"), tr("Vertical Center"), tr("Vertical Bottom")}, [this](int i) {
        tool_.modifySelected(tr("Justification"), [&](TitleObject& o) { o.font.vertical = static_cast<model::VerticalJustify>(i); });
    });
    menu(font, QStringLiteral("orientation"), tr("Font Orientation"), {tr("Left To Right"), tr("Right To Left"), tr("Top To Bottom")}, [this](int i) {
        tool_.modifySelected(tr("Font Orientation"), [&](TitleObject& o) { o.font.orientation = static_cast<model::FontOrientation>(i); });
    });
    number(font, QStringLiteral("kerning"), tr("Kerning"), -200, 1000, 1, [](TitleObject& o, double v) { o.font.kerning = v; });
    number(font, QStringLiteral("leading"), tr("Leading"), -500, 1000, 1, [](TitleObject& o, double v) { o.font.leading = v; });
    number(font, QStringLiteral("skew"), tr("Skew"), -60, 60, 1, [](TitleObject& o, double v) { o.font.skew = v; });

    // Transform (p.310-314, p.322).
    QFormLayout* transform = section(tr("Transform"));
    number(transform, QStringLiteral("opacity"), tr("Opacity"), 0, 100, 0, [](TitleObject& o, double v) { o.transform.opacity = v; });
    number(transform, QStringLiteral("x"), tr("Position X"), -10000, 10000, 1, [](TitleObject& o, double v) { o.transform.position.setX(v); });
    number(transform, QStringLiteral("y"), tr("Position Y"), -10000, 10000, 1, [](TitleObject& o, double v) { o.transform.position.setY(v); });
    // "Horizontal and vertical controls are available separately, or as a fixed aspect ratio with the Scale Fixed
    // Aspect checkbox" (p.322).
    number(transform, QStringLiteral("scaleX"), tr("Scale X"), 1, 10000, 1, [](TitleObject& o, double v) {
        if (o.transform.fixedAspect && o.transform.scaleX > 0) o.transform.scaleY *= v / o.transform.scaleX;
        o.transform.scaleX = v;
    });
    number(transform, QStringLiteral("scaleY"), tr("Scale Y"), 1, 10000, 1, [](TitleObject& o, double v) {
        if (o.transform.fixedAspect && o.transform.scaleY > 0) o.transform.scaleX *= v / o.transform.scaleY;
        o.transform.scaleY = v;
    });
    check(transform, QStringLiteral("fixedAspect"), tr("Scale Fixed Aspect"), [](TitleObject& o, bool on) { o.transform.fixedAspect = on; });
    number(transform, QStringLiteral("rotation"), tr("Rotation"), -3600, 3600, 1, [](TitleObject& o, double v) { o.transform.rotation = v; });
    menu(transform, QStringLiteral("alignH"), tr("Align H."),
         {QString(), tr("Align Left"), tr("Align Center"), tr("Align Right"), tr("Align Horizontal Center")}, [this](int i) {
             static constexpr TitlerTool::Align kinds[] = {TitlerTool::Align::Left, TitlerTool::Align::Center, TitlerTool::Align::Right,
                                                           TitlerTool::Align::HorizontalCenter};
             if (i > 0) tool_.align(kinds[i - 1]);
             refresh();
         });
    menu(transform, QStringLiteral("alignV"), tr("Align V."),
         {QString(), tr("Align Top"), tr("Align Center"), tr("Align Bottom"), tr("Align Vertical Center")}, [this](int i) {
             static constexpr TitlerTool::Align kinds[] = {TitlerTool::Align::Top, TitlerTool::Align::Middle, TitlerTool::Align::Bottom,
                                                           TitlerTool::Align::VerticalCenter};
             if (i > 0) tool_.align(kinds[i - 1]);
             refresh();
         });
    menu(transform, QStringLiteral("anchor"), tr("Anchor Point"),
         {tr("Center"), tr("Right"), tr("Left"), tr("Top Center"), tr("Top Right"), tr("Top Left"), tr("Bottom Center"), tr("Bottom Right"),
          tr("Bottom Left"), tr("Custom")},
         [this](int i) {
             if (i <= static_cast<int>(TitlerTool::Anchor::BottomLeft)) tool_.setAnchor(static_cast<TitlerTool::Anchor>(i));
             refresh();
         });
    number(transform, QStringLiteral("anchorX"), tr("Horizontal Position"), -10000, 10000, 1, [](TitleObject& o, double v) { o.transform.anchor.setX(v); });
    number(transform, QStringLiteral("anchorY"), tr("Vertical Position"), -10000, 10000, 1, [](TitleObject& o, double v) { o.transform.anchor.setY(v); });

    // Style: Fill, Outline, Shadow (p.314-317, p.322-324).
    QFormLayout* fill = section(tr("Fill"));
    check(fill, QStringLiteral("fill"), tr("Fill"), [](TitleObject& o, bool on) { o.fill.on = on; });
    swatch(fill, QStringLiteral("fill"), tr("Solid Color"));
    check(fill, QStringLiteral("gradient"), tr("Color Gradient"), [](TitleObject& o, bool on) { o.fill.color.gradient = on; });
    swatch(fill, QStringLiteral("fill0"), tr("Top Left Color"));
    swatch(fill, QStringLiteral("fill1"), tr("Top Right Color"));
    swatch(fill, QStringLiteral("fill2"), tr("Bottom Left Color"));
    swatch(fill, QStringLiteral("fill3"), tr("Bottom Right Color"));
    number(fill, QStringLiteral("fillOpacity"), tr("Fill Opacity"), 0, 100, 0, [](TitleObject& o, double v) { o.fill.opacity = v; });
    number(fill, QStringLiteral("fillSoftness"), tr("Fill Softness"), 0, 200, 1, [](TitleObject& o, double v) { o.fill.softness = v; });
    QFormLayout* outline = section(tr("Outline"));
    check(outline, QStringLiteral("outline"), tr("Outline"), [](TitleObject& o, bool on) { o.outline.on = on; });
    swatch(outline, QStringLiteral("outline"), tr("Outline Solid Color"));
    number(outline, QStringLiteral("outlineOpacity"), tr("Opacity"), 0, 100, 0, [](TitleObject& o, double v) { o.outline.opacity = v; });
    number(outline, QStringLiteral("outlineSize"), tr("Size"), 0, 500, 1, [](TitleObject& o, double v) { o.outline.size = v; });
    number(outline, QStringLiteral("outlineSoftness"), tr("Softness"), 0, 200, 1, [](TitleObject& o, double v) { o.outline.softness = v; });
    menu(outline, QStringLiteral("outlineType"), tr("Type"), {tr("Outer"), tr("Inner"), tr("Border")}, [this](int i) {
        tool_.modifySelected(tr("Outline"), [&](TitleObject& o) { o.outline.type = static_cast<model::OutlineType>(i); });
    });
    menu(outline, QStringLiteral("outlineShape"), tr("Shape"), {tr("Round"), tr("Square")}, [this](int i) {
        tool_.modifySelected(tr("Outline"), [&](TitleObject& o) { o.outline.shape = static_cast<model::OutlineShape>(i); });
    });
    QFormLayout* shadow = section(tr("Shadow"));
    check(shadow, QStringLiteral("shadow"), tr("Shadow"), [](TitleObject& o, bool on) { o.shadow.on = on; });
    swatch(shadow, QStringLiteral("shadow"), tr("Shadow Solid Color"));
    number(shadow, QStringLiteral("shadowOpacity"), tr("Opacity"), 0, 100, 0, [](TitleObject& o, double v) { o.shadow.opacity = v; });
    number(shadow, QStringLiteral("shadowDistance"), tr("Distance"), 0, 1000, 1, [](TitleObject& o, double v) { o.shadow.distance = v; });
    number(shadow, QStringLiteral("shadowAngle"), tr("Angle"), 0, 360, 0, [](TitleObject& o, double v) { o.shadow.angle = v; });
    number(shadow, QStringLiteral("shadowSoftness"), tr("Softness"), 0, 200, 1, [](TitleObject& o, double v) { o.shadow.softness = v; });

    // The Global Group Parameters (p.317, p.324).
    QFormLayout* background = section(tr("Background"));
    check(background, QStringLiteral("background"), tr("Background"), {}, [](Title& t, bool on) { t.background.on = on; });
    swatch(background, QStringLiteral("background"), tr("Background Solid Color"));
    number(background, QStringLiteral("backgroundOpacity"), tr("Opacity"), 0, 100, 0, {}, [](Title& t, double v) { t.background.opacity = v; });
    QFormLayout* foreground = section(tr("Foreground"));
    number(foreground, QStringLiteral("fillOverlay"), tr("Fill Overlay"), 0, 100, 0, {}, [](Title& t, double v) { t.foreground.fillOverlay = v; });
    number(foreground, QStringLiteral("foregroundOpacity"), tr("Opacity"), 0, 100, 0, {}, [](Title& t, double v) { t.foreground.opacity = v; });
    number(foreground, QStringLiteral("cropTop"), tr("Top"), 0, 100, 1, {}, [](Title& t, double v) { t.foreground.cropTop = v; });
    number(foreground, QStringLiteral("cropBottom"), tr("Bottom"), 0, 100, 1, {}, [](Title& t, double v) { t.foreground.cropBottom = v; });
    number(foreground, QStringLiteral("cropLeft"), tr("Left"), 0, 100, 1, {}, [](Title& t, double v) { t.foreground.cropLeft = v; });
    number(foreground, QStringLiteral("cropRight"), tr("Right"), 0, 100, 1, {}, [](Title& t, double v) { t.foreground.cropRight = v; });
    if (!across) outer->addStretch(1);

    connect(&tool_, &TitlerTool::changed, this, &TitlerPanel::refresh);
    refresh();
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
    for (const QString& s : {tr("Transform"), tr("Fill"), tr("Outline"), tr("Shadow")}) {
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
    setWindowTitle(tr("Titler+"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    panel_ = new TitlerPanel(tool, true, this);
    layout->addWidget(panel_);
}

} // namespace lumenis::ui
