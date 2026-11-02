// The classic Title Tool's window (title_tool.h; FX Guide p.327-341): the canvas, the toolbar and the menus, and what
// the mouse and keys do there. Text layout and drawing are media's (media/classic_title.h).
#include "lumenis/ui/title_tool.h"

#include "lumenis/media/classic_title.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

using model::ClassicKind;
using model::ClassicObject;
using model::ClassicTitle;

namespace {

// The safe action and safe title areas: ours 90% and 80% of the frame, as Titler+'s grid (titler_tool.h).
QRectF safeArea(double share) {
    const double w = model::kClassicWidth * share, h = model::kClassicHeight * share;
    return QRectF((model::kClassicWidth - w) / 2.0, (model::kClassicHeight - h) / 2.0, w, h);
}

// The visible grid's spacing (ours: 16 by 9 squares) and the snap grid's: "The snap grid has four lines for each line
// in the visible grid" (p.333).
constexpr double kGrid = 120.0;
constexpr double kSnap = kGrid / 4.0;
// How near a handle or a line a press must be, in title pixels (ours).
constexpr double kReach = 16.0;

// A text object's Titler+ twin for its characters' formats (the classic kerning stays a percentage here).
model::TitleObject characterTwin(const ClassicObject& o) {
    model::TitleObject t;
    t.kind = model::TitleObjectKind::Text;
    t.text = o.text;
    t.font.family = o.family;
    t.font.style = model::classicStyle(o.bold, o.italic);
    t.font.size = o.size;
    t.font.kerning = o.kerning;
    t.runs = o.runs;
    return t;
}

void takeCharacters(ClassicObject& o, const model::TitleObject& t) {
    o.text = t.text;
    o.runs = t.runs;
}

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    const double t = len2 > 0.0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    const QPointF q = a + t * ab;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

} // namespace

// ---------------------------------------------------------------- canvas

// The frame with the title, its guides and the selection; it hands the mouse and keys to the window.
class TitleCanvas : public QWidget {
public:
    explicit TitleCanvas(TitleToolWindow& tool) : QWidget(&tool), tool_(tool) {
        setObjectName(QStringLiteral("title:canvas"));
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(false);
        setMinimumSize(320, 180);
    }

    // The frame's place in the widget, letterboxed.
    QRectF frameRect() const {
        const double w = width(), h = height();
        const double aspect = model::kClassicWidth / model::kClassicHeight;
        if (w / std::max(1.0, h) > aspect) return QRectF((w - h * aspect) / 2.0, 0.0, h * aspect, h);
        return QRectF(0.0, (h - w / aspect) / 2.0, w, w / aspect);
    }
    QTransform titleToWidget() const {
        const QRectF f = frameRect();
        QTransform m;
        m.translate(f.left(), f.top());
        m.scale(f.width() / model::kClassicWidth, f.height() / model::kClassicHeight);
        return m;
    }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override {
        setFocus();
        tool_.press(titleToWidget().inverted().map(e->position()), e->modifiers());
    }
    void mouseMoveEvent(QMouseEvent* e) override { tool_.move(titleToWidget().inverted().map(e->position()), e->modifiers()); }
    void mouseReleaseEvent(QMouseEvent* e) override { tool_.release(titleToWidget().inverted().map(e->position()), e->modifiers()); }
    void keyPressEvent(QKeyEvent* e) override {
        if (!tool_.key(e->key(), e->modifiers(), e->text())) QWidget::keyPressEvent(e);
    }
    void keyReleaseEvent(QKeyEvent* e) override {
        // Releasing Ctrl or Alt ends the temporary Selection tool (p.332).
        if (tool_.temporary_ && !((e->modifiers() & Qt::ControlModifier) && (e->modifiers() & Qt::AltModifier))) {
            tool_.temporary_.reset();
            update();
        }
        QWidget::keyReleaseEvent(e);
    }

private:
    TitleToolWindow& tool_;
};

void TitleCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));
    const QRectF f = frameRect();
    const ClassicTitle& t = tool_.title_;
    // The background: the Record monitor's frame, moved by the Video Placement tool, or the colour (p.333-335).
    if (t.videoBackground) {
        p.fillRect(f, Qt::black);
        if (!tool_.frame_.isNull()) {
            p.save();
            p.setClipRect(f);
            const QPointF by(tool_.videoOffset_.x() * f.width() / model::kClassicWidth, tool_.videoOffset_.y() * f.height() / model::kClassicHeight);
            p.drawImage(f.translated(by), tool_.frame_);
            p.restore();
        }
    } else {
        QColor bg(t.backgroundColor);
        p.fillRect(f, bg.isValid() ? bg : QColor(Qt::black));
    }
    // "When you click a text object for editing, the Title tool displays the text without anti-aliasing" (p.333).
    const bool smooth = tool_.preview_ && tool_.editing_.isEmpty();
    p.drawImage(f.topLeft(), media::classicTitleImage(t, f.size().toSize(), smooth));
    const QTransform m = titleToWidget();
    // The alignment grid and the safe areas (p.333).
    if (tool_.grid_) {
        p.setPen(QPen(QColor(255, 255, 255, 60), 1.0));
        for (double x = kGrid; x < model::kClassicWidth; x += kGrid) p.drawLine(m.map(QLineF(x, 0, x, model::kClassicHeight)));
        for (double y = kGrid; y < model::kClassicHeight; y += kGrid) p.drawLine(m.map(QLineF(0, y, model::kClassicWidth, y)));
    }
    if (tool_.safeAreas_) {
        p.setPen(QPen(QColor(255, 255, 255, 160), 1.0, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(m.map(QPolygonF(safeArea(0.9))));
        p.drawPolygon(m.map(QPolygonF(safeArea(0.8))));
    }
    // The selection's handles, the text cursor and its highlight.
    p.setPen(QPen(palette().color(QPalette::Highlight), 1.0));
    for (const QString& id : tool_.selected_) {
        const ClassicObject* o = t.object(id);
        if (!o) continue;
        const QRectF b = m.mapRect(media::classicObjectBounds(*o));
        p.setBrush(Qt::NoBrush);
        if (id != tool_.editing_) p.drawRect(b);
        p.setBrush(palette().color(QPalette::Highlight));
        QList<QPointF> handles;
        if (o->kind == ClassicKind::Line) {
            handles << m.map(o->rect.topLeft()) << m.map(o->rect.bottomRight());
        } else if (o->kind == ClassicKind::Text) {
            handles << QPointF(b.left(), b.center().y()) << QPointF(b.right(), b.center().y());
        } else {
            handles << b.topLeft() << b.topRight() << b.bottomLeft() << b.bottomRight() << QPointF(b.center().x(), b.top())
                    << QPointF(b.center().x(), b.bottom()) << QPointF(b.left(), b.center().y()) << QPointF(b.right(), b.center().y());
        }
        if (id != tool_.editing_) {
            for (const QPointF& h : handles) p.drawRect(QRectF(h - QPointF(3, 3), QSizeF(6, 6)));
        }
    }
    if (const ClassicObject* o = t.object(tool_.editing_)) {
        QColor highlight = palette().color(QPalette::Highlight);
        highlight.setAlpha(110);
        p.setBrush(highlight);
        p.setPen(Qt::NoPen);
        for (const QPolygonF& box : media::classicTextRange(*o, tool_.anchor_, tool_.caret_)) p.drawPolygon(m.map(box));
        p.setPen(QPen(palette().color(QPalette::BrightText), 2.0));
        p.drawLine(m.map(media::classicTextCaret(*o, tool_.caret_)));
    }
    // What a drag draws: the lasso, the new object.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(palette().color(QPalette::BrightText), 1.0, Qt::DashLine));
    if (tool_.drag_ == TitleToolWindow::Drag::Lasso) p.drawRect(m.mapRect(QRectF(tool_.dragFrom_, tool_.dragTo_).normalized()));
    if (tool_.drag_ == TitleToolWindow::Drag::Draw) {
        if (tool_.tool_ == TitleToolWindow::Tool::Line) p.drawLine(m.map(QLineF(tool_.dragFrom_, tool_.dragTo_)));
        else if (tool_.tool_ == TitleToolWindow::Tool::Oval) p.drawEllipse(m.mapRect(QRectF(tool_.dragFrom_, tool_.dragTo_).normalized()));
        else p.drawRect(m.mapRect(QRectF(tool_.dragFrom_, tool_.dragTo_).normalized()));
    }
}

// ---------------------------------------------------------------- window

TitleToolWindow::TitleToolWindow(QWidget* parent) : QWidget(parent, Qt::Window) {
    setObjectName(QStringLiteral("titleTool"));
    setWindowTitle(tr("Title Tool"));
    resize(960, 680);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    menuBar_ = new QMenuBar(this);
    layout->setMenuBar(menuBar_);
    canvas_ = new TitleCanvas(*this);
    layout->addWidget(canvas_, 1);
    // "When you first open the Title tool, the font listed is the preferred system font" (p.337).
    defaults_.family = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    buildMenus();
    buildToolbar();
    refreshControls();
}

void TitleToolWindow::buildMenus() {
    auto add = [&](QMenu* menu, const QString& name, const QString& text, std::function<void(bool)> triggered, bool checkable = false,
                   const QKeySequence& keys = {}) {
        QAction* a = menu->addAction(text);
        a->setObjectName(QStringLiteral("title:") + name);
        a->setCheckable(checkable);
        if (!keys.isEmpty()) {
            a->setShortcut(keys);
            a->setShortcutContext(Qt::WindowShortcut);
        }
        connect(a, &QAction::triggered, this, [triggered](bool on) { triggered(on); });
        actions_.insert(name, a);
        return a;
    };
    // Edit (ours: the tool's own steps).
    QMenu* edit = menuBar_->addMenu(tr("Edit"));
    add(edit, QStringLiteral("undo"), tr("Undo"), [this](bool) { undo(); }, false, QKeySequence::Undo);
    add(edit, QStringLiteral("redo"), tr("Redo"), [this](bool) { redo(); }, false, QKeySequence(Qt::CTRL | Qt::Key_R));
    edit->addSeparator();
    add(edit, QStringLiteral("cut"), tr("Cut"), [this](bool) { cut(); }, false, QKeySequence::Cut);
    add(edit, QStringLiteral("copy"), tr("Copy"), [this](bool) { copy(); }, false, QKeySequence::Copy);
    add(edit, QStringLiteral("paste"), tr("Paste"), [this](bool) { paste(); }, false, QKeySequence::Paste);
    add(edit, QStringLiteral("clear"), tr("Delete"), [this](bool) { deleteSelection(); });
    add(edit, QStringLiteral("selectAll"), tr("Select All"), [this](bool) { selectAll(); }, false, QKeySequence::SelectAll);
    // Object (p.334, p.338).
    QMenu* object = menuBar_->addMenu(tr("Object"));
    add(object, QStringLiteral("preview"), tr("Preview"), [this](bool on) { setPreview(on); }, true);
    add(object, QStringLiteral("safeColors"), tr("Safe Colors"), [this](bool on) { setSafeColors(on); }, true);
    add(object, QStringLiteral("safeTitleArea"), tr("Safe Title Area/Global Grid"), [this](bool on) { setSafeAreas(on); }, true);
    object->addSeparator();
    add(object, QStringLiteral("objectBold"), tr("Bold"), [this](bool on) { setBold(on); }, true);
    add(object, QStringLiteral("objectItalic"), tr("Italic"), [this](bool on) { setItalic(on); }, true);
    // Alignment (p.334).
    QMenu* alignment = menuBar_->addMenu(tr("Alignment"));
    add(alignment, QStringLiteral("showGrid"), tr("Show Alignment Grid"), [this](bool on) { setGridShown(on); }, true);
    add(alignment, QStringLiteral("alignToGrid"), tr("Align to Grid"), [this](bool on) { setAlignToGrid(on); }, true);
}

void TitleToolWindow::buildToolbar() {
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(4, 2, 4, 4);
    bar->setSpacing(2);
    static_cast<QVBoxLayout*>(layout())->addLayout(bar);
    auto named = [&](QWidget* w, const QString& name) {
        w->setObjectName(QStringLiteral("title:") + name);
        controls_.insert(name, w);
    };
    auto button = [&](const QString& name, const QString& text, std::function<void()> clicked, bool checkable = false) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(text);
        b->setCheckable(checkable);
        named(b, name);
        bar->addWidget(b);
        connect(b, &QToolButton::clicked, this, [this, clicked] {
            if (!syncing_) clicked();
        });
        return b;
    };
    // The tools (p.330-331, p.340).
    const std::pair<Tool, QString> tools[] = {{Tool::Selection, tr("Selection Tool")}, {Tool::Text, tr("Text Tool")},
                                             {Tool::VideoPlacement, tr("Video Placement Tool")}, {Tool::Box, tr("Square and Rectangle Tool")},
                                             {Tool::Oval, tr("Oval Tool")}, {Tool::Line, tr("Line Tool")}};
    const QStringList toolNames = {QStringLiteral("tool:selection"), QStringLiteral("tool:text"), QStringLiteral("tool:video"),
                                   QStringLiteral("tool:box"), QStringLiteral("tool:oval"), QStringLiteral("tool:line")};
    for (int i = 0; i < 6; ++i) {
        const Tool which = tools[i].first;
        QToolButton* b = button(toolNames.at(i), tools[i].second, [this, which] { setTool(which); }, true);
        b->setProperty("titleTool", static_cast<int>(which));
        b->installEventFilter(this);
    }
    bar->addSpacing(8);
    // The text formatting tools (p.336-340).
    auto* font = new QFontComboBox(this);
    named(font, QStringLiteral("font"));
    bar->addWidget(font);
    connect(font, &QFontComboBox::currentFontChanged, this, [this](const QFont& f) {
        if (!syncing_) setFont(f.family());
    });
    auto* size = new QDoubleSpinBox(this);
    size->setRange(5, 999);
    size->setDecimals(0);
    size->setKeyboardTracking(false);
    named(size, QStringLiteral("size"));
    bar->addWidget(size);
    connect(size, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!syncing_) setPointSize(v);
    });
    // "Click the Point Size button, and select a standard point size from the menu" (p.338; ours: these sizes).
    QToolButton* sizes = button(QStringLiteral("sizes"), tr("Point Size"), [] {});
    auto* sizeMenu = new QMenu(sizes);
    for (const int s : {12, 18, 24, 36, 48, 60, 72, 96, 120, 144, 200, 288}) {
        sizeMenu->addAction(QString::number(s), this, [this, s] { setPointSize(s); });
    }
    sizes->setMenu(sizeMenu);
    sizes->setPopupMode(QToolButton::InstantPopup);
    button(QStringLiteral("bold"), tr("Bold"), [this] { setBold(!defaults_.bold); }, true);
    button(QStringLiteral("italic"), tr("Italic"), [this] { setItalic(!defaults_.italic); }, true);
    button(QStringLiteral("justify:left"), tr("Left"), [this] { setJustify(model::ClassicJustify::Left); }, true);
    button(QStringLiteral("justify:center"), tr("Center"), [this] { setJustify(model::ClassicJustify::Center); }, true);
    button(QStringLiteral("justify:right"), tr("Right"), [this] { setJustify(model::ClassicJustify::Right); }, true);
    auto* kerning = new QDoubleSpinBox(this);
    kerning->setRange(-100, 500);
    kerning->setDecimals(0);
    kerning->setKeyboardTracking(false);
    kerning->setToolTip(tr("Kerning"));
    named(kerning, QStringLiteral("kerning"));
    bar->addWidget(kerning);
    connect(kerning, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!syncing_) setKerning(v);
    });
    QToolButton* kern = button(QStringLiteral("kerningMenu"), tr("Kerning"), [] {});
    auto* kernMenu = new QMenu(kern);
    // "Pair kerning works only on fonts that support kerning tables" (p.339): ours always uses the font's kerning.
    QAction* pair = kernMenu->addAction(tr("Pair Kerning"));
    pair->setCheckable(true);
    pair->setChecked(true);
    pair->setEnabled(false);
    kernMenu->addSeparator();
    kernMenu->addAction(tr("Loose"), this, [this] { setKerning(kLooseKerning); });
    kernMenu->addAction(tr("Normal"), this, [this] { setKerning(0.0); });
    kernMenu->addAction(tr("Tight"), this, [this] { setKerning(kTightKerning); });
    kern->setMenu(kernMenu);
    kern->setPopupMode(QToolButton::InstantPopup);
    auto* leading = new QDoubleSpinBox(this);
    leading->setRange(-500, 1000);
    leading->setDecimals(0);
    leading->setKeyboardTracking(false);
    leading->setToolTip(tr("Leading"));
    named(leading, QStringLiteral("leading"));
    bar->addWidget(leading);
    connect(leading, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!syncing_) setLeading(v);
    });
    bar->addSpacing(8);
    // The Background Selection tools (p.333-335).
    button(QStringLiteral("videoBackground"), tr("Video Background"), [this] { setVideoBackground(!title_.videoBackground); }, true);
    button(QStringLiteral("backgroundColor"), tr("Bg"), [this] {
        const QString c = pickColor(title_.backgroundColor, tr("Background Color"));
        if (!c.isEmpty()) setBackgroundColor(c);
    });
    bar->addStretch(1);
}

QWidget* TitleToolWindow::canvas() const {
    return canvas_;
}

QWidget* TitleToolWindow::control(const QString& name) const {
    return controls_.value(name);
}

QPointF TitleToolWindow::toTitle(const QPointF& widget) const {
    return canvas_->titleToWidget().inverted().map(widget);
}

QPointF TitleToolWindow::toWidget(const QPointF& title) const {
    return canvas_->titleToWidget().map(title);
}

void TitleToolWindow::setFrameSource(std::function<QImage(QSize)> source) {
    frames_ = std::move(source);
    updateBackground();
}

void TitleToolWindow::updateBackground() {
    frame_ = frames_ ? frames_(QSize(static_cast<int>(model::kClassicWidth / 2), static_cast<int>(model::kClassicHeight / 2))) : QImage();
    canvas_->update();
}

bool TitleToolWindow::event(QEvent* event) {
    // "Click again in the Title tool to activate its window. The background updates" (p.334).
    if (event->type() == QEvent::WindowActivate) updateBackground();
    return QWidget::event(event);
}

bool TitleToolWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonDblClick && watched->property("titleTool").isValid()) {
        setTool(static_cast<Tool>(watched->property("titleTool").toInt()), true);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void TitleToolWindow::setTitle(const ClassicTitle& title) {
    finishEditing();
    title_ = title;
    undo_.clear();
    redo_.clear();
    selected_.clear();
    refreshControls();
    canvas_->update();
}

void TitleToolWindow::newTitle() {
    setTitle(ClassicTitle{});
    setTool(Tool::Text);
}

void TitleToolWindow::setTool(Tool tool, bool sticky) {
    if (tool != Tool::Text) finishEditing();
    tool_ = tool;
    sticky_ = sticky;
    refreshControls();
    canvas_->update();
}

void TitleToolWindow::select(const QStringList& ids) {
    finishEditing();
    selected_.clear();
    for (const QString& id : ids) {
        if (title_.object(id)) selected_ << id;
    }
    refreshControls();
    canvas_->update();
}

void TitleToolWindow::setPreview(bool on) {
    preview_ = on;
    refreshControls();
    canvas_->update();
}

void TitleToolWindow::setSafeColors(bool on) {
    safeColors_ = on;
    refreshControls();
}

void TitleToolWindow::setSafeAreas(bool on) {
    safeAreas_ = on;
    refreshControls();
    canvas_->update();
}

void TitleToolWindow::setGridShown(bool on) {
    grid_ = on;
    refreshControls();
    canvas_->update();
}

void TitleToolWindow::setAlignToGrid(bool on) {
    snap_ = on;
    refreshControls();
}

void TitleToolWindow::setVideoBackground(bool on) {
    const ClassicTitle before = title_;
    title_.videoBackground = on;
    commit(before);
    if (on) updateBackground();
}

void TitleToolWindow::setBackgroundColor(const QString& color) {
    const ClassicTitle before = title_;
    title_.backgroundColor = safeColors_ ? model::safeColor(color) : color;
    title_.videoBackground = false;
    commit(before);
}

QString TitleToolWindow::pickColor(const QString& current, const QString& caption) {
    const QColor c = QColorDialog::getColor(QColor(current), this, caption);
    if (!c.isValid()) return {};
    return safeColors_ ? model::safeColor(c.name()) : c.name();
}

void TitleToolWindow::commit(const ClassicTitle& before) {
    if (title_ == before) {
        refreshControls();
        canvas_->update();
        return;
    }
    undo_ << before;
    redo_.clear();
    refreshControls();
    canvas_->update();
    emit titleChanged();
}

bool TitleToolWindow::undo() {
    finishEditing();
    if (undo_.isEmpty()) return false;
    redo_ << title_;
    title_ = undo_.takeLast();
    selected_.erase(std::remove_if(selected_.begin(), selected_.end(), [this](const QString& id) { return !title_.object(id); }), selected_.end());
    refreshControls();
    canvas_->update();
    emit titleChanged();
    return true;
}

bool TitleToolWindow::redo() {
    finishEditing();
    if (redo_.isEmpty()) return false;
    undo_ << title_;
    title_ = redo_.takeLast();
    refreshControls();
    canvas_->update();
    emit titleChanged();
    return true;
}

// ---- text

void TitleToolWindow::finishEditing() {
    if (editing_.isEmpty()) return;
    const QString id = editing_;
    editing_.clear();
    caret_ = anchor_ = 0;
    // A text object left empty goes (ours, as Titler+'s, p.303).
    if (const ClassicObject* o = title_.object(id); o && o->text.isEmpty()) {
        title_.objects.erase(std::remove_if(title_.objects.begin(), title_.objects.end(), [&](const ClassicObject& c) { return c.id == id; }),
                             title_.objects.end());
        selected_.removeAll(id);
    }
    commit(editStart_);
}

QList<ClassicObject*> TitleToolWindow::formatTargets(ClassicTitle& t) {
    QList<ClassicObject*> out;
    if (!editing_.isEmpty()) {
        if (ClassicObject* o = t.object(editing_)) out << o;
        return out;
    }
    for (const QString& id : selected_) {
        if (ClassicObject* o = t.object(id); o && o->kind == ClassicKind::Text) out << o;
    }
    return out;
}

void TitleToolWindow::format(const std::function<void(ClassicObject&)>& whole, const std::function<void(model::TitleObject&)>& characters) {
    // While typing with characters highlighted: those characters (p.336).
    if (ClassicObject* o = title_.object(editing_); o && caret_ != anchor_) {
        model::TitleObject twin = characterTwin(*o);
        model::changeCharacters(twin, std::min(caret_, anchor_), std::max(caret_, anchor_), characters);
        takeCharacters(*o, twin);
        refreshControls();
        canvas_->update();
        return;
    }
    const ClassicTitle before = title_;
    const QList<ClassicObject*> targets = formatTargets(title_);
    if (targets.isEmpty()) {
        // "Click in the Title tool but do not select any text. Change any of the text attributes. Whenever you start
        // typing a new text string, the Title tool uses your new defaults" (p.337).
        whole(defaults_);
        refreshControls();
        return;
    }
    // The whole object's attribute (`whole` also clears its characters' own).
    for (ClassicObject* o : targets) {
        whole(*o);
        model::TitleObject twin = characterTwin(*o);
        model::normalizeRuns(twin); // runs left with nothing set go
        takeCharacters(*o, twin);
    }
    if (editing_.isEmpty()) {
        commit(before);
    } else {
        refreshControls();
        canvas_->update();
    }
}

void TitleToolWindow::setFont(const QString& family) {
    format([&](ClassicObject& o) { o.family = family; for (model::TitleRun& r : o.runs) r.format.family.reset(); },
           [&](model::TitleObject& v) { v.font.family = family; });
}

void TitleToolWindow::setPointSize(double size) {
    size = std::clamp(size, 5.0, 999.0);
    format([&](ClassicObject& o) { o.size = size; for (model::TitleRun& r : o.runs) r.format.size.reset(); },
           [&](model::TitleObject& v) { v.font.size = size; });
}

void TitleToolWindow::setBold(bool on) {
    format([&](ClassicObject& o) { o.bold = on; for (model::TitleRun& r : o.runs) r.format.style.reset(); },
           [&](model::TitleObject& v) { v.font.style = model::classicStyle(on, model::styleIsItalic(v.font.style)); });
}

void TitleToolWindow::setItalic(bool on) {
    format([&](ClassicObject& o) { o.italic = on; for (model::TitleRun& r : o.runs) r.format.style.reset(); },
           [&](model::TitleObject& v) { v.font.style = model::classicStyle(model::styleIsBold(v.font.style), on); });
}

void TitleToolWindow::setJustify(model::ClassicJustify justify) {
    // Justification is the whole object's (p.338).
    const ClassicTitle before = title_;
    const QList<ClassicObject*> targets = formatTargets(title_);
    if (targets.isEmpty()) defaults_.justify = justify;
    for (ClassicObject* o : targets) o->justify = justify;
    if (editing_.isEmpty()) commit(before);
    refreshControls();
    canvas_->update();
}

void TitleToolWindow::setKerning(double percent) {
    format([&](ClassicObject& o) { o.kerning = percent; for (model::TitleRun& r : o.runs) r.format.kerning.reset(); },
           [&](model::TitleObject& v) { v.font.kerning = percent; });
}

void TitleToolWindow::setLeading(double points) {
    const ClassicTitle before = title_;
    const QList<ClassicObject*> targets = formatTargets(title_);
    if (targets.isEmpty()) defaults_.leading = points;
    for (ClassicObject* o : targets) o->leading = points;
    if (editing_.isEmpty()) commit(before);
    refreshControls();
    canvas_->update();
}

// ---- mouse

QString TitleToolWindow::objectAt(const QPointF& at) const {
    for (auto it = title_.objects.crbegin(); it != title_.objects.crend(); ++it) {
        if (it->kind == ClassicKind::Line) {
            if (distanceToSegment(at, it->rect.topLeft(), it->rect.bottomRight()) <= kReach) return it->id;
        } else if (media::classicObjectBounds(*it).adjusted(-2, -2, 2, 2).contains(at)) {
            return it->id;
        }
    }
    return {};
}

std::optional<int> TitleToolWindow::handleAt(const QPointF& at) const {
    if (selected_.size() != 1) return std::nullopt;
    const ClassicObject* o = title_.object(selected_.first());
    if (!o) return std::nullopt;
    QList<QPointF> handles;
    const QRectF b = media::classicObjectBounds(*o);
    if (o->kind == ClassicKind::Line) {
        handles << o->rect.topLeft() << o->rect.bottomRight();
    } else if (o->kind == ClassicKind::Text) {
        handles << QPointF(b.left(), b.center().y()) << QPointF(b.right(), b.center().y());
    } else {
        handles << b.topLeft() << b.topRight() << b.bottomLeft() << b.bottomRight() << QPointF(b.center().x(), b.top())
                << QPointF(b.center().x(), b.bottom()) << QPointF(b.left(), b.center().y()) << QPointF(b.right(), b.center().y());
    }
    for (int i = 0; i < handles.size(); ++i) {
        if (std::hypot(handles.at(i).x() - at.x(), handles.at(i).y() - at.y()) <= kReach) return i;
    }
    return std::nullopt;
}

QPointF TitleToolWindow::snapped(const QPointF& at) const {
    // "When you create or drag an object in the Title tool, the object snaps to the nearest grid line" (p.334).
    if (!snap_) return at;
    return {std::round(at.x() / kSnap) * kSnap, std::round(at.y() / kSnap) * kSnap};
}

void TitleToolWindow::press(const QPointF& at, Qt::KeyboardModifiers modifiers) {
    // "Press the Alt key (Windows) or Option key (Macintosh), and click anywhere in the Title tool" switches between
    // the Selection tool and the Text tool (p.332).
    const bool ctrl = modifiers & Qt::ControlModifier, alt = modifiers & Qt::AltModifier, shift = modifiers & Qt::ShiftModifier;
    if (alt && !ctrl) {
        setTool(tool_ == Tool::Text ? Tool::Selection : Tool::Text);
        return;
    }
    // "To use the Selection tool temporarily when another tool is active: Press and hold the Ctrl and Alt keys" (p.332).
    const Tool tool = ctrl && alt ? Tool::Selection : tool_;
    dragFrom_ = dragTo_ = at;
    dragStart_ = title_;
    switch (tool) {
    case Tool::Selection: {
        if (const auto h = handleAt(at)) {
            drag_ = Drag::Handle;
            handle_ = *h;
            return;
        }
        const QString hit = objectAt(at);
        finishEditing();
        dragStart_ = title_;
        if (hit.isEmpty()) {
            if (!shift) selected_.clear();
            drag_ = Drag::Lasso;
        } else {
            if (shift) {
                if (selected_.contains(hit)) selected_.removeAll(hit);
                else selected_ << hit;
            } else if (!selected_.contains(hit)) {
                selected_ = {hit};
            }
            drag_ = Drag::Move;
        }
        refreshControls();
        canvas_->update();
        return;
    }
    case Tool::Text: {
        const QString hit = objectAt(at);
        const ClassicObject* o = title_.object(hit);
        if (o && o->kind == ClassicKind::Text) {
            if (editing_ != hit) {
                finishEditing();
                editStart_ = title_;
                editing_ = hit;
                selected_ = {hit};
            }
            caret_ = media::classicTextIndexAt(*title_.object(hit), at);
            if (!shift) anchor_ = caret_;
            drag_ = Drag::Highlight;
        } else if (o) {
            finishEditing();
            selected_ = {hit};
        } else {
            // "Click the position in the frame where you want to add text. An insertion point appears" (p.335): a new
            // text object with the defaults, as wide as from there to the safe title area's right edge (ours).
            finishEditing();
            editStart_ = title_;
            ClassicObject t = defaults_;
            t.kind = ClassicKind::Text;
            t.id = title_.newId(ClassicKind::Text);
            t.text.clear();
            t.runs.clear();
            const QPointF p = snapped(at);
            t.rect = QRectF(p.x(), p.y(), std::max(120.0, safeArea(0.8).right() - p.x()), 0.0);
            title_.objects << t;
            editing_ = t.id;
            selected_ = {t.id};
            caret_ = anchor_ = 0;
        }
        refreshControls();
        canvas_->update();
        return;
    }
    case Tool::VideoPlacement:
        drag_ = Drag::Pan;
        panStart_ = videoOffset_;
        return;
    case Tool::Box:
    case Tool::Oval:
    case Tool::Line:
        finishEditing();
        dragStart_ = title_;
        dragFrom_ = dragTo_ = snapped(at);
        drag_ = Drag::Draw;
        return;
    }
}

void TitleToolWindow::move(const QPointF& at, Qt::KeyboardModifiers modifiers) {
    const bool shift = modifiers & Qt::ShiftModifier;
    switch (drag_) {
    case Drag::None: return;
    case Drag::Move: {
        title_ = dragStart_;
        QPointF by = at - dragFrom_;
        if (snap_ && !selected_.isEmpty()) {
            // The first selected object's corner on the snap grid; the rest keep their places to it.
            if (const ClassicObject* first = dragStart_.object(selected_.first())) {
                const QPointF corner = first->rect.topLeft() + by;
                by = snapped(corner) - first->rect.topLeft();
            }
        }
        for (const QString& id : selected_) {
            if (ClassicObject* o = title_.object(id)) o->rect.translate(by);
        }
        break;
    }
    case Drag::Handle: {
        title_ = dragStart_;
        ClassicObject* o = selected_.isEmpty() ? nullptr : title_.object(selected_.first());
        if (!o) break;
        const QPointF p = snapped(at);
        if (o->kind == ClassicKind::Line) {
            QPointF a = o->rect.topLeft(), b = o->rect.bottomRight();
            (handle_ == 0 ? a : b) = p;
            o->rect = QRectF(a, b);
        } else if (o->kind == ClassicKind::Text) {
            // "You can resize only the width of a text object" (p.336).
            QRectF r = o->rect;
            if (handle_ == 0) r.setLeft(std::min(p.x(), r.right() - 10));
            else r.setRight(std::max(p.x(), r.left() + 10));
            o->rect = r;
        } else {
            QRectF r = o->rect.normalized();
            switch (handle_) {
            case 0: r.setTopLeft(p); break;
            case 1: r.setTopRight(p); break;
            case 2: r.setBottomLeft(p); break;
            case 3: r.setBottomRight(p); break;
            case 4: r.setTop(p.y()); break;
            case 5: r.setBottom(p.y()); break;
            case 6: r.setLeft(p.x()); break;
            default: r.setRight(p.x()); break;
            }
            o->rect = r;
        }
        break;
    }
    case Drag::Draw: {
        QPointF p = snapped(at);
        if (shift) {
            const QPointF d = p - dragFrom_;
            if (tool_ == Tool::Line) {
                // "Shift-drag to constrain the line to a horizontal or vertical line" (p.341).
                if (std::abs(d.x()) >= std::abs(d.y())) p.setY(dragFrom_.y());
                else p.setX(dragFrom_.x());
            } else {
                // "Shift-drag to constrain the object to a square" / "to a circle" (p.341).
                const double side = std::max(std::abs(d.x()), std::abs(d.y()));
                p = dragFrom_ + QPointF(d.x() < 0 ? -side : side, d.y() < 0 ? -side : side);
            }
        }
        dragTo_ = p;
        break;
    }
    case Drag::Lasso: dragTo_ = at; break;
    case Drag::Pan: videoOffset_ = panStart_ + (at - dragFrom_); break;
    case Drag::Highlight:
        if (const ClassicObject* o = title_.object(editing_)) caret_ = media::classicTextIndexAt(*o, at);
        break;
    }
    canvas_->update();
}

void TitleToolWindow::release(const QPointF& at, Qt::KeyboardModifiers modifiers) {
    if (drag_ == Drag::None) return;
    move(at, modifiers);
    const Drag drag = drag_;
    drag_ = Drag::None;
    switch (drag) {
    case Drag::Move:
    case Drag::Handle: commit(dragStart_); break;
    case Drag::Draw: {
        const QRectF r(dragFrom_, dragTo_);
        const bool line = tool_ == Tool::Line;
        if (line ? std::hypot(r.width(), r.height()) >= 4.0 : (std::abs(r.width()) >= 4.0 && std::abs(r.height()) >= 4.0)) {
            ClassicObject o;
            o.kind = tool_ == Tool::Box ? ClassicKind::Box : tool_ == Tool::Oval ? ClassicKind::Oval : ClassicKind::Line;
            o.id = title_.newId(o.kind);
            o.rect = line ? r : r.normalized();
            title_.objects << o;
            selected_ = {o.id};
            commit(dragStart_);
        }
        // "By default, when you use a drawing tool and deselect the created object, the pointer reverts to the
        // Selection tool" (p.341).
        if (!sticky_) setTool(Tool::Selection);
        break;
    }
    case Drag::Lasso: {
        // "Click outside the objects, and drag to surround the objects with a lasso" (p.332).
        const QRectF area = QRectF(dragFrom_, dragTo_).normalized();
        if (area.width() > 2 || area.height() > 2) {
            for (const ClassicObject& o : title_.objects) {
                if (area.contains(media::classicObjectBounds(o)) && !selected_.contains(o.id)) selected_ << o.id;
            }
        }
        refreshControls();
        break;
    }
    default: break;
    }
    canvas_->update();
}

// ---- keys

bool TitleToolWindow::key(int key, Qt::KeyboardModifiers modifiers, const QString& text) {
    const bool ctrl = modifiers & Qt::ControlModifier, alt = modifiers & Qt::AltModifier, shift = modifiers & Qt::ShiftModifier;
    if ((key == Qt::Key_Control || key == Qt::Key_Alt) && ctrl && alt) {
        temporary_ = Tool::Selection;
        return true;
    }
    if (ClassicObject* o = title_.object(editing_)) {
        const qsizetype from = std::min(caret_, anchor_), to = std::max(caret_, anchor_);
        auto replace = [&](const QString& with) {
            model::TitleObject twin = characterTwin(*o);
            model::replaceText(twin, from, to, with);
            takeCharacters(*o, twin);
            caret_ = anchor_ = from + with.size();
        };
        switch (key) {
        case Qt::Key_Escape: finishEditing(); break;
        case Qt::Key_Return:
        case Qt::Key_Enter: replace(QStringLiteral("\n")); break; // "To insert a line return, press Enter" (p.335)
        case Qt::Key_Backspace:
            if (from == to && from > 0) {
                model::TitleObject twin = characterTwin(*o);
                model::replaceText(twin, from - 1, from, QString());
                takeCharacters(*o, twin);
                caret_ = anchor_ = from - 1;
            } else {
                replace(QString());
            }
            break;
        case Qt::Key_Delete:
            if (from == to && from < o->text.size()) {
                model::TitleObject twin = characterTwin(*o);
                model::replaceText(twin, from, from + 1, QString());
                takeCharacters(*o, twin);
            } else {
                replace(QString());
            }
            break;
        case Qt::Key_Left:
        case Qt::Key_Right:
            if (alt) {
                // "Press the Alt key ... and use the Left Arrow key or the Right Arrow key to increase or decrease the
                // kerning" (p.339): ours Right adds and Left takes away one percent, on the highlighted characters or
                // the one before the cursor (the pair it makes with the next).
                const qsizetype a = from == to ? std::max<qsizetype>(0, from - 1) : from;
                const qsizetype b = from == to ? from : to;
                if (b > a) {
                    model::TitleObject twin = characterTwin(*o);
                    const double now = model::styleAt(twin, a).font.kerning;
                    model::changeCharacters(twin, a, b, [&](model::TitleObject& v) { v.font.kerning = now + (key == Qt::Key_Right ? 1.0 : -1.0); });
                    takeCharacters(*o, twin);
                }
                break;
            }
            caret_ = std::clamp<qsizetype>(caret_ + (key == Qt::Key_Right ? 1 : -1), 0, o->text.size());
            if (!shift) anchor_ = caret_;
            break;
        case Qt::Key_Home:
        case Qt::Key_End:
            caret_ = key == Qt::Key_Home ? 0 : o->text.size();
            if (!shift) anchor_ = caret_;
            break;
        default:
            if (ctrl || text.isEmpty() || !text.at(0).isPrint()) return false;
            replace(text);
            break;
        }
        refreshControls();
        canvas_->update();
        return true;
    }
    switch (key) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        // "Use the arrow keys to move the object one pixel at a time" (p.336).
        if (selected_.isEmpty()) return false;
        const ClassicTitle before = title_;
        const QPointF by(key == Qt::Key_Left ? -1 : key == Qt::Key_Right ? 1 : 0, key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : 0);
        for (const QString& id : selected_) {
            if (ClassicObject* c = title_.object(id)) c->rect.translate(by);
        }
        commit(before);
        return true;
    }
    case Qt::Key_Delete:
    case Qt::Key_Backspace: deleteSelection(); return true;
    default: return false;
    }
}

// ---- Edit menu

void TitleToolWindow::copy() {
    if (const ClassicObject* o = title_.object(editing_)) {
        if (caret_ != anchor_) QApplication::clipboard()->setText(o->text.mid(std::min(caret_, anchor_), std::abs(caret_ - anchor_)));
        return;
    }
    ClassicTitle copied;
    for (const ClassicObject& o : title_.objects) {
        if (selected_.contains(o.id)) copied.objects << o;
    }
    if (copied.objects.isEmpty()) return;
    clipboard_ = QString::fromUtf8(QJsonDocument(copied.toJson()).toJson(QJsonDocument::Compact));
    QStringList texts;
    for (const ClassicObject& o : copied.objects) {
        if (!o.text.isEmpty()) texts << o.text;
    }
    QApplication::clipboard()->setText(texts.join(QLatin1Char('\n')));
}

void TitleToolWindow::cut() {
    copy();
    if (!editing_.isEmpty()) {
        if (caret_ != anchor_) key(Qt::Key_Delete, Qt::NoModifier, QString());
        return;
    }
    deleteSelection();
}

void TitleToolWindow::paste() {
    if (!editing_.isEmpty()) {
        // "The pasted text takes on the attributes currently set in the Title Tool toolbar" (p.335).
        const QString text = QApplication::clipboard()->text();
        if (!text.isEmpty()) key(0, Qt::NoModifier, text);
        return;
    }
    if (clipboard_.isEmpty()) return;
    const ClassicTitle copied = ClassicTitle::fromJson(QJsonDocument::fromJson(clipboard_.toUtf8()).object());
    const ClassicTitle before = title_;
    selected_.clear();
    for (ClassicObject o : copied.objects) {
        o.id = title_.newId(o.kind);
        title_.objects << o;
        selected_ << o.id;
    }
    commit(before);
}

void TitleToolWindow::deleteSelection() {
    if (!editing_.isEmpty()) {
        key(Qt::Key_Delete, Qt::NoModifier, QString());
        return;
    }
    if (selected_.isEmpty()) return;
    const ClassicTitle before = title_;
    title_.objects.erase(std::remove_if(title_.objects.begin(), title_.objects.end(), [this](const ClassicObject& o) { return selected_.contains(o.id); }),
                         title_.objects.end());
    selected_.clear();
    commit(before);
}

void TitleToolWindow::selectAll() {
    if (const ClassicObject* o = title_.object(editing_)) {
        anchor_ = 0;
        caret_ = o->text.size();
    } else {
        selected_.clear();
        for (const ClassicObject& c : title_.objects) selected_ << c.id;
    }
    refreshControls();
    canvas_->update();
}

// ---- controls

void TitleToolWindow::refreshControls() {
    syncing_ = true;
    const Tool shown = temporary_.value_or(tool_);
    const QStringList toolNames = {QStringLiteral("tool:selection"), QStringLiteral("tool:text"), QStringLiteral("tool:video"),
                                   QStringLiteral("tool:box"), QStringLiteral("tool:oval"), QStringLiteral("tool:line")};
    for (int i = 0; i < toolNames.size(); ++i) {
        if (auto* b = qobject_cast<QToolButton*>(controls_.value(toolNames.at(i)))) b->setChecked(static_cast<int>(shown) == i);
    }
    // "When you select a text object, the Font Selection button displays the name of the font in the text object"
    // (p.337): the first text object's (its first highlighted character's while typing), else the defaults.
    ClassicObject shownText = defaults_;
    {
        ClassicTitle t = title_;
        const QList<ClassicObject*> targets = formatTargets(t);
        if (!targets.isEmpty()) shownText = *targets.first();
        if (const ClassicObject* o = title_.object(editing_); o && !o->text.isEmpty()) {
            const model::TitleCharStyle s = model::styleAt(characterTwin(*o), std::min({caret_, anchor_, o->text.size() - 1}));
            shownText.family = s.font.family;
            shownText.size = s.font.size;
            shownText.bold = model::styleIsBold(s.font.style);
            shownText.italic = model::styleIsItalic(s.font.style);
            shownText.kerning = s.font.kerning;
        }
    }
    if (auto* f = qobject_cast<QFontComboBox*>(controls_.value(QStringLiteral("font")))) f->setCurrentFont(QFont(shownText.family));
    if (auto* s = qobject_cast<QDoubleSpinBox*>(controls_.value(QStringLiteral("size")))) s->setValue(shownText.size);
    if (auto* k = qobject_cast<QDoubleSpinBox*>(controls_.value(QStringLiteral("kerning")))) k->setValue(shownText.kerning);
    if (auto* l = qobject_cast<QDoubleSpinBox*>(controls_.value(QStringLiteral("leading")))) l->setValue(shownText.leading);
    auto tick = [&](const QString& name, bool on) {
        if (auto* b = qobject_cast<QToolButton*>(controls_.value(name))) b->setChecked(on);
        if (QAction* a = actions_.value(name)) a->setChecked(on);
    };
    tick(QStringLiteral("bold"), shownText.bold);
    tick(QStringLiteral("italic"), shownText.italic);
    tick(QStringLiteral("objectBold"), shownText.bold);
    tick(QStringLiteral("objectItalic"), shownText.italic);
    tick(QStringLiteral("justify:left"), shownText.justify == model::ClassicJustify::Left);
    tick(QStringLiteral("justify:center"), shownText.justify == model::ClassicJustify::Center);
    tick(QStringLiteral("justify:right"), shownText.justify == model::ClassicJustify::Right);
    // "The Video Background button is green when you are creating a title for display over a video background" (p.333).
    tick(QStringLiteral("videoBackground"), title_.videoBackground);
    if (QWidget* v = controls_.value(QStringLiteral("videoBackground"))) {
        v->setStyleSheet(title_.videoBackground ? QStringLiteral("background-color: #2e8b3e;") : QString());
    }
    if (QWidget* bg = controls_.value(QStringLiteral("backgroundColor"))) {
        bg->setStyleSheet(QStringLiteral("background-color: %1;").arg(QColor(title_.backgroundColor).name()));
    }
    tick(QStringLiteral("preview"), preview_);
    tick(QStringLiteral("safeColors"), safeColors_);
    tick(QStringLiteral("safeTitleArea"), safeAreas_);
    tick(QStringLiteral("showGrid"), grid_);
    tick(QStringLiteral("alignToGrid"), snap_);
    if (QAction* a = actions_.value(QStringLiteral("undo"))) a->setEnabled(!undo_.isEmpty() || !editing_.isEmpty());
    if (QAction* a = actions_.value(QStringLiteral("redo"))) a->setEnabled(!redo_.isEmpty());
    syncing_ = false;
}

// ---------------------------------------------------------------- New Title

bool askNewTitle(QWidget* parent, bool* persist) {
    QDialog box(parent);
    box.setObjectName(QStringLiteral("newTitle"));
    box.setWindowTitle(QObject::tr("New Title"));
    auto* layout = new QVBoxLayout(&box);
    layout->addWidget(new QLabel(QObject::tr("Create a new title with:"), &box));
    auto* buttons = new QHBoxLayout;
    auto* marquee = new QPushButton(QObject::tr("Marquee"), &box);
    marquee->setObjectName(QStringLiteral("newTitle:marquee"));
    // Ours: Marquee is Avid's own title application, which Cutthulhu does not have.
    marquee->setEnabled(false);
    marquee->setToolTip(QObject::tr("Marquee is not available"));
    auto* tool = new QPushButton(QObject::tr("Title Tool"), &box);
    tool->setObjectName(QStringLiteral("newTitle:titleTool"));
    tool->setDefault(true);
    buttons->addWidget(marquee);
    buttons->addWidget(tool);
    layout->addLayout(buttons);
    auto* keep = new QCheckBox(QObject::tr("Persist"), &box);
    keep->setObjectName(QStringLiteral("newTitle:persist"));
    layout->addWidget(keep);
    auto* cancel = new QDialogButtonBox(QDialogButtonBox::Cancel, &box);
    layout->addWidget(cancel);
    QObject::connect(cancel, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    QObject::connect(tool, &QPushButton::clicked, &box, &QDialog::accept);
    const bool chosen = box.exec() == QDialog::Accepted;
    if (persist) *persist = chosen && keep->isChecked();
    return chosen;
}

} // namespace lumenis::ui
