// The classic Title Tool's window (title_tool.h; FX Guide p.327-341): the canvas, the toolbar and the menus, and what
// the mouse and keys do there. Text layout and drawing are media's (media/classic_title.h).
#include "lumenis/ui/title_tool.h"

#include "lumenis/media/classic_title.h"
#include "lumenis/media/managed.h"
#include "lumenis/ui/bin_dock.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFontComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

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

// The session's shadow softness for new objects (p.354; ours: kept while the application runs).
int sessionSoftness = 0;

// "Shadow softness values must be integer values between 4 and 40 ... If you set the value to zero, shadows on title
// objects have no softening" (p.354): 1 to 3 go up to 4 from below, down to 0 from above (ours).
int legalSoftness(int value, int previous) {
    value = std::clamp(value, 0, 40);
    if (value > 0 && value < 4) return value > previous ? 4 : 0;
    return value;
}

// The Shadow Depth and Direction button (p.353): a sample with its shadow; dragging the sample's shadow sets the
// offset ("Shift-click ... to restrict shadow placement to 45-degree angles"), shown softened by the softness.
class ShadowPad : public QWidget {
public:
    ShadowPad(QWidget* parent, std::function<void(QPointF)> changed) : QWidget(parent), changed_(std::move(changed)) {
        setFixedSize(56, 56);
        setToolTip(QObject::tr("Shadow Depth and Direction"));
    }
    void display(const QPointF& offset, int softness) {
        offset_ = offset;
        softness_ = softness;
        update();
    }

protected:
    // A pad pixel is a title pixel, the sample's shadow kept within the pad.
    QPointF shown() const {
        const double reach = 20.0, length = std::hypot(offset_.x(), offset_.y());
        return length > reach ? offset_ * (reach / length) : offset_;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Mid));
        const QRectF sample(width() / 2.0 - 8, height() / 2.0 - 8, 16, 16);
        // The softness as a halo of fainter squares (ours).
        const int rings = std::min(softness_ / 4, 5);
        for (int r = rings; r >= 0; --r) {
            QColor c = palette().color(QPalette::Shadow);
            c.setAlpha(rings ? 60 + (255 - 60) * (rings - r) / rings : 255);
            p.fillRect(sample.translated(shown()).adjusted(-r, -r, r, r), c);
        }
        p.fillRect(sample, palette().color(QPalette::Light));
    }
    void mousePressEvent(QMouseEvent* e) override { mouseMoveEvent(e); }
    void mouseMoveEvent(QMouseEvent* e) override {
        QPointF d = e->position() - QPointF(width() / 2.0, height() / 2.0);
        if (e->modifiers() & Qt::ShiftModifier) {
            const double a = std::round(std::atan2(d.y(), d.x()) / (std::numbers::pi / 4.0)) * (std::numbers::pi / 4.0);
            const double length = std::hypot(d.x(), d.y());
            d = QPointF(std::cos(a) * length, std::sin(a) * length);
        }
        offset_ = QPointF(std::round(d.x()), std::round(d.y()));
        update();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        mouseMoveEvent(e);
        changed_(offset_);
    }

private:
    std::function<void(QPointF)> changed_;
    QPointF offset_;
    int softness_ = 0;
};

// The Blend Direction box (p.352): the blend between two colours (or transparencies as greys); "Click the Blend
// Direction box, and drag clockwise or counterclockwise ... The change takes effect as soon as you release the mouse
// button" (the direction where the pointer is from the box's centre).
class BlendBox : public QWidget {
public:
    BlendBox(QWidget* parent, std::function<void(double)> changed) : QWidget(parent), changed_(std::move(changed)) {
        setFixedSize(48, 48);
        setToolTip(QObject::tr("Blend Direction"));
    }
    void display(const QColor& a, const QColor& b, double degrees) {
        a_ = a;
        b_ = b;
        angle_ = degrees;
        update();
    }
    double angle() const { return angle_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const double a = angle_ * std::numbers::pi / 180.0;
        const QPointF c = rect().center(), d(std::cos(a) * width() / 2.0, std::sin(a) * height() / 2.0);
        QLinearGradient g(c - d, c + d);
        g.setColorAt(0, a_);
        g.setColorAt(1, b_);
        p.fillRect(rect(), g);
        p.setPen(palette().color(QPalette::Dark));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        const QPointF d = e->position() - QPointF(rect().center());
        if (std::hypot(d.x(), d.y()) < 2.0) return;
        angle_ = std::atan2(d.y(), d.x()) * 180.0 / std::numbers::pi;
        update();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        mouseMoveEvent(e);
        changed_(angle_);
    }

private:
    std::function<void(double)> changed_;
    QColor a_ = Qt::white, b_ = Qt::black;
    double angle_ = 0.0;
};

// The Arrowhead Size dialog box (p.349): an arrow with a handle; "drag it vertically or horizontally to resize and
// reshape it. The shape of the arrowhead is in proportion to the line width": the handle's distance back along the
// arrow is its length, its distance from the arrow's middle half its width, both in line widths (ours: a line width is
// 8 of the dialog's pixels).
class ArrowheadSizeDialog : public QDialog {
public:
    ArrowheadSizeDialog(QWidget* parent, const QSizeF& size) : QDialog(parent), size_(size) {
        setObjectName(QStringLiteral("title:arrowheadSize"));
        setWindowTitle(QObject::tr("Arrowhead Size"));
        auto* layout = new QVBoxLayout(this);
        layout->addSpacing(110);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        setMinimumWidth(220);
    }
    QSizeF arrowSize() const { return size_; }

protected:
    static constexpr double kUnit = 8.0;
    QPointF tip() const { return QPointF(width() - 24.0, 60.0); }
    QPointF handle() const { return tip() + QPointF(-size_.width() * kUnit, -size_.height() * kUnit / 2.0); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(QRectF(8, 8, width() - 16, 104), palette().color(QPalette::Base));
        p.setPen(palette().color(QPalette::Text));
        const QPointF t = tip(), h = handle();
        const double half = kUnit / 2.0;
        const QPolygonF arrow{QPointF(24, t.y() - half), QPointF(h.x(), t.y() - half), h, t, QPointF(h.x(), 2 * t.y() - h.y()),
                              QPointF(h.x(), t.y() + half), QPointF(24, t.y() + half)};
        p.drawPolygon(arrow);
        p.setBrush(palette().color(QPalette::Text));
        p.drawRect(QRectF(h - QPointF(3, 3), QSizeF(6, 6)));
    }
    void mousePressEvent(QMouseEvent* e) override { dragging_ = QLineF(e->position(), handle()).length() <= 8.0; }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!dragging_) return;
        const QPointF d = tip() - e->position();
        size_ = QSizeF(std::clamp(d.x() / kUnit, 1.0, 12.0), std::clamp(2.0 * d.y() / kUnit, 1.0, 12.0));
        update();
    }
    void mouseReleaseEvent(QMouseEvent*) override { dragging_ = false; }

private:
    QSizeF size_;
    bool dragging_ = false;
};

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
        setAcceptDrops(true);
    }

    // The frame's place in the widget, letterboxed.
    QRectF frameRect() const {
        const double w = width(), h = height();
        const double aspect = model::kClassicWidth / model::kClassicHeight;
        if (w / std::max(1.0, h) > aspect) return QRectF((w - h * aspect) / 2.0, 0.0, h * aspect, h);
        return QRectF(0.0, (h - w / aspect) / 2.0, w, w / aspect);
    }
    // One frame's pixels to the widget's; with the view's place in a rolling or crawling title, the title's.
    QTransform frameToWidget() const {
        const QRectF f = frameRect();
        QTransform m;
        m.translate(f.left(), f.top());
        m.scale(f.width() / model::kClassicWidth, f.height() / model::kClassicHeight);
        return m;
    }
    QTransform titleToWidget() const { return QTransform::fromTranslate(-tool_.scroll_.x(), -tool_.scroll_.y()) * frameToWidget(); }

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
    // A Title Effect clip dragged from a bin opens for revising (p.360).
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->mimeData()->hasFormat(binItemsMime()) && tool_.openClip_) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent* e) override {
        const QStringList lines = QString::fromUtf8(e->mimeData()->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.size() < 2 || !tool_.openClip_) return;
        for (qsizetype i = 1; i < lines.size(); ++i) {
            if (tool_.openClip_(lines.first(), lines.at(i))) {
                e->acceptProposedAction();
                return;
            }
        }
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
    p.drawImage(f.topLeft(), media::classicTitleImage(t, f.size().toSize(), smooth, tool_.scroll_));
    const QTransform m = titleToWidget();
    const QTransform view = frameToWidget();
    // The alignment grid and the safe areas (p.333), on the frame shown.
    if (tool_.grid_) {
        p.setPen(QPen(QColor(255, 255, 255, 60), 1.0));
        for (double x = kGrid; x < model::kClassicWidth; x += kGrid) p.drawLine(view.map(QLineF(x, 0, x, model::kClassicHeight)));
        for (double y = kGrid; y < model::kClassicHeight; y += kGrid) p.drawLine(view.map(QLineF(0, y, model::kClassicWidth, y)));
    }
    if (tool_.safeAreas_) {
        p.setPen(QPen(QColor(255, 255, 255, 160), 1.0, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(view.map(QPolygonF(safeArea(0.9))));
        p.drawPolygon(view.map(QPolygonF(safeArea(0.8))));
    }
    // A rolling or crawling title's page edges (ours: dashed).
    if (t.motion != model::ClassicMotion::Still) {
        p.save();
        p.setClipRect(f);
        p.setPen(QPen(QColor(255, 255, 255, 120), 1.0, Qt::DashLine));
        for (int page = 2; page <= t.pages + 1; ++page) {
            const QPointF o = model::classicPageOrigin(t.motion, page);
            p.drawLine(m.map(t.motion == model::ClassicMotion::Rolling ? QLineF(0, o.y(), model::kClassicWidth, o.y()) : QLineF(o.x(), 0, o.x(), model::kClassicHeight)));
        }
        p.restore();
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
            // "A locked object displays hollow selection handles" (p.356).
            if (o->locked) p.setBrush(Qt::NoBrush);
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
    // "a scroll bar appears along the right side of the Title tool video background for moving vertically through the
    // rolling title, and at the bottom for moving horizontally through a crawling title" (p.344).
    auto* view = new QGridLayout;
    view->setSpacing(0);
    view->addWidget(canvas_, 0, 0);
    rollBar_ = new QScrollBar(Qt::Vertical, this);
    rollBar_->setObjectName(QStringLiteral("title:rollScroll"));
    crawlBar_ = new QScrollBar(Qt::Horizontal, this);
    crawlBar_->setObjectName(QStringLiteral("title:crawlScroll"));
    view->addWidget(rollBar_, 0, 1);
    view->addWidget(crawlBar_, 1, 0);
    layout->addLayout(view, 1);
    connect(rollBar_, &QScrollBar::valueChanged, this, [this](int v) {
        if (!syncing_) setScroll(QPointF(scroll_.x(), v));
    });
    connect(crawlBar_, &QScrollBar::valueChanged, this, [this](int v) {
        if (!syncing_) setScroll(QPointF(v, scroll_.y()));
    });
    // "When you first open the Title tool, the font listed is the preferred system font" (p.337).
    defaults_.family = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    defaults_.shadowSoftness = sessionSoftness;
    buildMenus();
    buildToolbar();
    buildAttributeBar();
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
    // File (p.358-364): "While the legacy Title Tool is open, the File menu offers Save Title, Save Title As and Export
    // Title" (ours: in the window's own menu bar).
    QMenu* file = menuBar_->addMenu(tr("File"));
    add(file, QStringLiteral("saveTitle"), tr("Save Title"), [this](bool) { saveTitle(); });
    add(file, QStringLiteral("saveTitleAs"), tr("Save Title As..."), [this](bool) { saveTitleAs(); }, false,
        QKeySequence(Qt::SHIFT | Qt::CTRL | Qt::Key_S));
    add(file, QStringLiteral("exportTitle"), tr("Export Title..."), [this](bool) {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export Title"), QString(), tr("Images (*.png *.tif *.tiff)"));
        if (!path.isEmpty() && !exportTitle(path)) QMessageBox::warning(this, tr("Export Title"), tr("The title could not be exported."));
    });
    file->addSeparator();
    add(file, QStringLiteral("close"), tr("Close"), [this](bool) { closeTitle(); });
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
    add(edit, QStringLiteral("deselectAll"), tr("Deselect All"), [this](bool) { deselectAll(); });
    add(edit, QStringLiteral("duplicate"), tr("Duplicate"), [this](bool) { duplicate(); });
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
    // Rolling and crawling titles' pages (p.343-346).
    object->addSeparator();
    add(object, QStringLiteral("autoSize"), tr("Auto Size Mode"), [this](bool on) { setAutoSize(on); }, true);
    add(object, QStringLiteral("addPage"), tr("Add Page"), [this](bool) { addPage(); });
    auto askPage = [this](const QString& title) {
        bool ok = false;
        const int n = QInputDialog::getInt(this, title, tr("Page:"), page(), 1, model::classicPageLimit(title_.motion, lines_), 1, &ok);
        return ok ? n : 0;
    };
    add(object, QStringLiteral("insertPage"), tr("Insert Page..."), [this, askPage](bool) {
        if (const int n = askPage(tr("Insert Page"))) insertPage(n);
    });
    add(object, QStringLiteral("removePage"), tr("Remove Page..."), [this, askPage](bool) {
        if (const int n = askPage(tr("Remove Page"))) removePage(n);
    });
    add(object, QStringLiteral("copyToPage"), tr("Copy to Page..."), [this, askPage](bool) {
        if (const int n = askPage(tr("Copy to Page"))) copyToPage(n);
    });
    add(object, QStringLiteral("moveToPage"), tr("Move to Page..."), [this, askPage](bool) {
        if (const int n = askPage(tr("Move to Page"))) moveToPage(n);
    });
    add(object, QStringLiteral("makeCrawl"), tr("Make Crawl"), [this](bool) { makeCrawl(); });
    // Shadows (p.354).
    object->addSeparator();
    add(object, QStringLiteral("softenShadow"), tr("Soften Shadow..."), [this](bool) { openSoftenShadow(); });
    // Layers, groups and locks (p.356).
    object->addSeparator();
    add(object, QStringLiteral("bringToFront"), tr("Bring To Front"), [this](bool) { reorder(model::ClassicLayer::ToFront); });
    add(object, QStringLiteral("sendToBack"), tr("Send To Back"), [this](bool) { reorder(model::ClassicLayer::ToBack); });
    add(object, QStringLiteral("bringForward"), tr("Bring Forward"), [this](bool) { reorder(model::ClassicLayer::Forward); });
    add(object, QStringLiteral("sendBackward"), tr("Send Backward"), [this](bool) { reorder(model::ClassicLayer::Backward); });
    object->addSeparator();
    add(object, QStringLiteral("group"), tr("Group"), [this](bool) { group(); });
    add(object, QStringLiteral("ungroup"), tr("UnGroup"), [this](bool) { ungroup(); });
    add(object, QStringLiteral("lock"), tr("Lock"), [this](bool) { setLocked(true); });
    add(object, QStringLiteral("unlock"), tr("Unlock"), [this](bool) { setLocked(false); });
    // Aligning and distributing (p.356-357).
    alignment->addSeparator();
    using media::ClassicAlign;
    const std::tuple<QString, QString, ClassicAlign, QKeySequence> aligns[] = {
        {QStringLiteral("frameLeft"), tr("Align To Frame Left"), ClassicAlign::FrameLeft, {}},
        {QStringLiteral("frameRight"), tr("Align To Frame Right"), ClassicAlign::FrameRight, {}},
        {QStringLiteral("frameTop"), tr("Align To Frame Top"), ClassicAlign::FrameTop, {}},
        {QStringLiteral("frameBottom"), tr("Align To Frame Bottom"), ClassicAlign::FrameBottom, QKeySequence(Qt::SHIFT | Qt::CTRL | Qt::Key_Z)},
        {QStringLiteral("centerHorizontal"), tr("Center in Frame Horiz."), ClassicAlign::FrameCenterH, QKeySequence(Qt::SHIFT | Qt::CTRL | Qt::Key_C)},
        {QStringLiteral("centerVertical"), tr("Center in Frame Vert."), ClassicAlign::FrameCenterV, {}},
        {QStringLiteral("objectsLeft"), tr("Align Objects Left"), ClassicAlign::ObjectsLeft, {}},
        {QStringLiteral("objectsRight"), tr("Align Objects Right"), ClassicAlign::ObjectsRight, {}},
        {QStringLiteral("objectsTop"), tr("Align Objects Top"), ClassicAlign::ObjectsTop, {}},
        {QStringLiteral("objectsBottom"), tr("Align Objects Bottom"), ClassicAlign::ObjectsBottom, {}}};
    for (const auto& [name, text, how, keys] : aligns) {
        if (name == QStringLiteral("objectsLeft")) alignment->addSeparator();
        const ClassicAlign which = how;
        add(alignment, name, text, [this, which](bool) { align(which); }, false, keys);
    }
    alignment->addSeparator();
    add(alignment, QStringLiteral("distributeLeftToRight"), tr("Distribute Left to Right"), [this](bool) { distribute(media::ClassicDistribute::LeftToRight); });
    add(alignment, QStringLiteral("distributeTopToBottom"), tr("Distribute Top to Bottom"), [this](bool) { distribute(media::ClassicDistribute::TopToBottom); });
    add(alignment, QStringLiteral("distributeFirstToLast"), tr("Distribute First to Last"), [this](bool) { distribute(media::ClassicDistribute::FirstToLast); });
    // Templates (p.360-361; ours: in the window's own menu bar).
    QMenu* templates = menuBar_->addMenu(tr("Templates"));
    const QString filter = tr("Title Templates (*.ttpl)");
    add(templates, QStringLiteral("saveTemplate"), tr("Save Template..."), [this, filter](bool) {
        QDir().mkpath(templateFolder_);
        QString path = QFileDialog::getSaveFileName(this, tr("Save Template"), templateFolder_, filter);
        if (path.isEmpty()) return;
        if (!path.endsWith(QLatin1String(".ttpl"))) path += QLatin1String(".ttpl");
        if (!saveTemplate(path)) QMessageBox::warning(this, tr("Save Template"), tr("The template could not be saved."));
    });
    add(templates, QStringLiteral("includeTemplate"), tr("Include Template..."), [this, filter](bool) {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open"), templateFolder_, filter);
        if (!path.isEmpty() && !includeTemplate(path)) QMessageBox::warning(this, tr("Include Template"), tr("The template could not be read."));
    });
    add(templates, QStringLiteral("moveableTemplates"), tr("Moveable Templates"), [this](bool on) { setMoveableTemplates(on); }, true);
}

void TitleToolWindow::buildAttributeBar() {
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(4, 0, 4, 2);
    bar->setSpacing(2);
    static_cast<QVBoxLayout*>(layout())->insertLayout(layout()->count() - 1, bar);
    auto named = [&](QWidget* w, const QString& name) {
        w->setObjectName(QStringLiteral("title:") + name);
        controls_.insert(name, w);
    };
    auto menuButton = [&](const QString& name, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setToolTip(tip);
        b->setPopupMode(QToolButton::InstantPopup);
        b->setMenu(new QMenu(b));
        named(b, name);
        bar->addWidget(b);
        return b;
    };
    // The Styles button and the Save Style Parameter button (p.361-363): their menus list the styles when opened.
    QToolButton* stylesButton = menuButton(QStringLiteral("styles"), tr("Styles"));
    stylesButton->setText(tr("Styles"));
    QToolButton* saveStyleButton = menuButton(QStringLiteral("saveStyle"), tr("Save Style Parameter"));
    saveStyleButton->setText(QStringLiteral("+"));
    connect(stylesButton->menu(), &QMenu::aboutToShow, this, &TitleToolWindow::fillStyleMenus);
    connect(saveStyleButton->menu(), &QMenu::aboutToShow, this, &TitleToolWindow::fillStyleMenus);
    fillStyleMenus();
    bar->addSpacing(8);
    // The line attribute tools (p.347-349). Ours: the standard radii and widths below, as many as the guide pictures.
    QToolButton* corner = menuButton(QStringLiteral("boxCorner"), tr("Box Corner"));
    corner->menu()->addAction(tr("Square"), this, [this] { setCornerRadius(0); });
    for (const int r : {10, 20, 30, 50, 80}) corner->menu()->addAction(tr("Radius %1").arg(r), this, [this, r] { setCornerRadius(r); });
    corner->menu()->addAction(tr("Custom Radius..."), this, [this] {
        bool ok = false;
        const int r = QInputDialog::getInt(this, tr("Enter New Corner Radius"), tr("Corner radius (pixels):"),
                                           static_cast<int>(shownObject().cornerRadius), 0, 200, 1, &ok);
        if (ok) setCornerRadius(r);
    });
    QToolButton* width = menuButton(QStringLiteral("borderWidth"), tr("Border Width"));
    width->menu()->addAction(tr("Remove Border"), this, [this] { setLineWidth(0); });
    for (const int w : {1, 2, 3, 4, 6, 8, 10}) width->menu()->addAction(tr("%1 pixels").arg(w), this, [this, w] { setLineWidth(w); });
    width->menu()->addAction(tr("Custom Width..."), this, [this] {
        bool ok = false;
        const ClassicObject& o = shownObject();
        const int w = QInputDialog::getInt(this, tr("Enter New Line Width"), tr("Width (pixels):"),
                                           static_cast<int>(o.kind == ClassicKind::Line ? o.lineWidth : o.borderWidth), 1, 200, 1, &ok);
        if (ok) setLineWidth(w);
    });
    QToolButton* arrows = menuButton(QStringLiteral("arrowhead"), tr("Arrowhead"));
    arrows->menu()->addAction(tr("Remove Arrowhead"), this, [this] { setArrows(model::ClassicArrows::None); });
    arrows->menu()->addAction(QString(QChar(0x2192)), this, [this] { setArrows(model::ClassicArrows::End); });
    arrows->menu()->addAction(QString(QChar(0x2190)), this, [this] { setArrows(model::ClassicArrows::Start); });
    arrows->menu()->addAction(QString(QChar(0x2194)), this, [this] { setArrows(model::ClassicArrows::Both); });
    arrows->menu()->addAction(tr("Edit Arrowhead..."), this, [this] {
        ArrowheadSizeDialog box(this, shownObject().arrowSize);
        if (box.exec() == QDialog::Accepted) setArrowSize(box.arrowSize());
    });
    bar->addSpacing(8);
    // The Color Selection and Transparency Level boxes (p.350): Fill, Shad, Bord.
    auto* boxes = new QGridLayout;
    boxes->setSpacing(1);
    bar->addLayout(boxes);
    const std::pair<Coat, QString> coats[] = {{Coat::Fill, tr("Fill")}, {Coat::Shadow, tr("Shad")}, {Coat::Border, tr("Bord")}};
    const QStringList coatNames = {QStringLiteral("fill"), QStringLiteral("shadow"), QStringLiteral("border")};
    for (int i = 0; i < 3; ++i) {
        const Coat coat = coats[i].first;
        boxes->addWidget(new QLabel(coats[i].second, this), i, 0);
        auto* color = new QToolButton(this);
        color->setToolTip(tr("%1 Color").arg(coats[i].second));
        color->setFixedSize(28, 16);
        named(color, coatNames.at(i) + QStringLiteral("Color"));
        boxes->addWidget(color, i, 1);
        connect(color, &QToolButton::clicked, this, [this, coat] {
            chooseBox(coat, false);
            const ClassicObject& o = shownObject();
            const QString now = coat == Coat::Fill ? o.fill.color : coat == Coat::Border ? o.border.color : o.shadowColor;
            const QString c = pickColor(now, tr("Color"));
            if (!c.isEmpty()) setColor(coat, c);
        });
        auto* level = new QToolButton(this);
        level->setText(QStringLiteral("Hi"));
        level->setToolTip(tr("%1 Transparency").arg(coats[i].second));
        level->setFixedSize(28, 16);
        named(level, coatNames.at(i) + QStringLiteral("Transparency"));
        boxes->addWidget(level, i, 2);
        connect(level, &QToolButton::clicked, this, [this, coat] { chooseBox(coat, true); });
    }
    // The Color and Transparency Blend tools (p.350-352): the two blend values over the Blend Direction box, and the
    // transparency control's slider.
    auto* blend = new QWidget(this);
    named(blend, QStringLiteral("blendTools"));
    auto* blendLayout = new QGridLayout(blend);
    blendLayout->setContentsMargins(0, 0, 0, 0);
    blendLayout->setSpacing(1);
    for (int which = 0; which < 2; ++which) {
        auto* b = new QToolButton(blend);
        b->setFixedSize(22, 14);
        b->setToolTip(tr("Blend Selection"));
        named(b, QStringLiteral("blend%1").arg(which + 1));
        blendLayout->addWidget(b, 0, which);
        connect(b, &QToolButton::clicked, this, [this, which] {
            chooseBox(activeCoat_, activeTransparency_, which);
            if (activeTransparency_) return; // the slider sets it
            const ClassicObject& o = shownObject();
            const model::ClassicPaint& paint = activeCoat_ == Coat::Border ? o.border : o.fill;
            const QString c = pickColor(which == 0 || paint.color2.isEmpty() ? paint.color : paint.color2, tr("Blend Color"));
            if (!c.isEmpty()) setBlendColor(activeCoat_, which, c);
        });
    }
    auto* direction = new BlendBox(blend, [this](double degrees) { setBlendAngle(activeCoat_, activeTransparency_, degrees); });
    named(direction, QStringLiteral("blendDirection"));
    blendLayout->addWidget(direction, 1, 0, 1, 2);
    bar->addWidget(blend);
    auto* slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(0, 100);
    slider->setFixedWidth(100);
    slider->setToolTip(tr("Opaque - Transparent"));
    named(slider, QStringLiteral("transparencySlider"));
    bar->addWidget(slider);
    auto applySlider = [this, slider] {
        if (syncing_) return;
        if (activeBlend_ >= 0 && activeCoat_ != Coat::Shadow) setBlendTransparency(activeCoat_, activeBlend_, slider->value());
        else setTransparency(activeCoat_, slider->value());
    };
    connect(slider, &QSlider::sliderReleased, this, applySlider);
    connect(slider, &QSlider::valueChanged, this, [slider, applySlider] {
        if (!slider->isSliderDown()) applySlider();
    });
    bar->addSpacing(8);
    // The shadow tools (p.353).
    auto* pad = new ShadowPad(this, [this](QPointF offset) { setShadowOffset(offset); });
    named(pad, QStringLiteral("shadowDirection"));
    bar->addWidget(pad);
    auto* depth = new QSpinBox(this);
    depth->setRange(0, 500);
    depth->setKeyboardTracking(false);
    depth->setToolTip(tr("Shadow Depth"));
    named(depth, QStringLiteral("shadowDepth"));
    bar->addWidget(depth);
    connect(depth, &QSpinBox::valueChanged, this, [this](int v) {
        if (!syncing_) setShadowDepth(v);
    });
    auto* kind = new QToolButton(this);
    kind->setCheckable(true);
    kind->setToolTip(tr("Drop and Depth Shadow"));
    named(kind, QStringLiteral("shadowType"));
    bar->addWidget(kind);
    connect(kind, &QToolButton::clicked, this, [this] {
        if (!syncing_) setDepthShadow(!shownObject().depthShadow);
    });
    bar->addStretch(1);
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
    bar->addSpacing(8);
    // The Crawling Title and Rolling Title buttons and the Page Number text box (p.343-345).
    button(QStringLiteral("crawl"), tr("Crawling Title"), [this] {
        setMotion(title_.motion == model::ClassicMotion::Crawling ? model::ClassicMotion::Still : model::ClassicMotion::Crawling);
    }, true);
    button(QStringLiteral("roll"), tr("Rolling Title"), [this] {
        setMotion(title_.motion == model::ClassicMotion::Rolling ? model::ClassicMotion::Still : model::ClassicMotion::Rolling);
    }, true);
    auto* pageBox = new QSpinBox(this);
    pageBox->setRange(1, 1);
    pageBox->setKeyboardTracking(false);
    pageBox->setToolTip(tr("Page Number"));
    named(pageBox, QStringLiteral("page"));
    bar->addStretch(1);
    bar->addWidget(pageBox);
    connect(pageBox, &QSpinBox::valueChanged, this, [this](int v) {
        if (!syncing_) setPage(v);
    });
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

void TitleToolWindow::closeEvent(QCloseEvent* event) {
    if (closeTitle()) event->accept();
    else event->ignore();
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
    savedTitle_ = title;
    undo_.clear();
    redo_.clear();
    selected_.clear();
    refreshControls();
    canvas_->update();
}

void TitleToolWindow::newTitle() {
    setTitle(ClassicTitle{});
    clipBin_.clear();
    clipId_.clear();
    clipName_.clear();
    setTool(Tool::Text);
}

// ---- saving (6.8d4a)

void TitleToolWindow::openClip(const ClassicTitle& title, const QString& bin, const QString& itemId, const QString& name, bool fastSaved) {
    setTitle(title);
    clipBin_ = bin;
    clipId_ = itemId;
    clipName_ = name;
    // Fast Save stays on "until you ... load a title that was saved without Fast Save selected" (p.358).
    if (!fastSaved) fastSave_ = false;
    setTool(Tool::Selection);
}

std::optional<TitleToolWindow::SaveRequest> TitleToolWindow::askSaveRequest(const SaveRequest& suggestion) {
    if (askSave_) return askSave_(suggestion);
    // The Save Title dialog box (p.358).
    QDialog box(this);
    box.setObjectName(QStringLiteral("title:saveTitle"));
    box.setWindowTitle(tr("Save Title"));
    auto* form = new QFormLayout(&box);
    auto* name = new QLineEdit(suggestion.name, &box);
    name->setObjectName(QStringLiteral("title:saveName"));
    form->addRow(tr("Name:"), name);
    auto* bin = new QComboBox(&box);
    bin->setObjectName(QStringLiteral("title:saveBin"));
    if (saver_.bins) {
        for (const auto& [id, binName] : saver_.bins()) bin->addItem(binName, id);
    }
    bin->setCurrentIndex(std::max(0, bin->findData(suggestion.bin)));
    form->addRow(tr("Bin:"), bin);
    auto* drive = new QComboBox(&box);
    drive->setObjectName(QStringLiteral("title:saveDrive"));
    for (const media::MediaDrive& d : media::mediaDrives()) drive->addItem(d.name.isEmpty() ? d.root : d.name, d.root);
    drive->setCurrentIndex(std::max(0, drive->findData(suggestion.drive)));
    form->addRow(tr("Drive:"), drive);
    auto* resolution = new QComboBox(&box);
    resolution->setObjectName(QStringLiteral("title:saveResolution"));
    for (const media::Resolution& r : media::resolutions()) resolution->addItem(r.name, r.id);
    resolution->setCurrentIndex(std::max(0, resolution->findData(suggestion.resolution)));
    form->addRow(tr("Resolution:"), resolution);
    auto* fast = new QCheckBox(tr("Fast Save (Unrendered)"), &box);
    fast->setObjectName(QStringLiteral("title:fastSave"));
    fast->setChecked(suggestion.fastSave);
    form->addRow(fast);
    // Ours: the title aspect ratios (multiformat 24p and 25p projects) and Place in Timeline (6.8e) are not offered yet.
    auto* place = new QCheckBox(tr("Place in Timeline (In -> Out)"), &box);
    place->setEnabled(false);
    form->addRow(place);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &box);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    if (box.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) return std::nullopt;
    SaveRequest r;
    r.name = name->text().trimmed();
    r.bin = bin->currentData().toString();
    r.drive = drive->currentData().toString();
    r.resolution = resolution->currentData().toString();
    r.fastSave = fast->isChecked();
    return r;
}

bool TitleToolWindow::save(const SaveRequest& request, bool replace) {
    finishEditing();
    if (!saver_.save) return false;
    // "You cannot save a rolling or crawling title with a color background" (p.342).
    if (title_.motion != model::ClassicMotion::Still && !title_.videoBackground) {
        QMessageBox::warning(this, tr("Save Title"), tr("A rolling or crawling title cannot be saved with a color background."));
        return false;
    }
    QString error;
    const QString id = saver_.save(title_, request, replace ? clipId_ : QString(), &error);
    if (id.isEmpty()) {
        if (!error.isEmpty()) QMessageBox::warning(this, tr("Save Title"), error);
        return false;
    }
    savedTitle_ = title_;
    clipBin_ = request.bin;
    clipId_ = id;
    clipName_ = request.name;
    fastSave_ = request.fastSave;
    lastSave_ = request;
    emit titleSaved(request.bin, id);
    return true;
}

bool TitleToolWindow::saveTitle() {
    // "Save the title with the same name and media parameters ... by choosing File > Save Title. The revised Title
    // Effect clip replaces the previous clip in the bin" (p.360).
    if (!clipId_.isEmpty() && !lastSave_.name.isEmpty() && lastSave_.bin == clipBin_) {
        SaveRequest r = lastSave_;
        r.name = clipName_;
        return save(r, true);
    }
    if (!clipId_.isEmpty()) {
        SaveRequest r = saver_.defaults ? saver_.defaults() : SaveRequest{};
        r.name = clipName_;
        r.bin = clipBin_;
        r.fastSave = fastSave_;
        return save(r, true);
    }
    return saveTitleAs();
}

bool TitleToolWindow::saveTitleAs() {
    SaveRequest suggestion = saver_.defaults ? saver_.defaults() : SaveRequest{};
    if (!lastSave_.bin.isEmpty()) {
        // "If you are saving a new title, the resolution you select becomes the default for future new title saves"
        // (p.358).
        suggestion.bin = lastSave_.bin;
        suggestion.drive = lastSave_.drive;
        suggestion.resolution = lastSave_.resolution;
    }
    suggestion.name = clipName_.isEmpty() ? tr("Title") : clipName_;
    suggestion.fastSave = fastSave_;
    const auto request = askSaveRequest(suggestion);
    if (!request) return false;
    // Keeping the clip's name makes a numbered revision (p.360): the saver gives it its number.
    return save(*request, false);
}

bool TitleToolWindow::closeTitle() {
    finishEditing();
    if (isModified() && !title_.objects.isEmpty()) {
        const int answer = askClose_ ? askClose_()
                                     : QMessageBox::question(this, tr("Title Tool"), tr("Save the changes to this title?"),
                                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) return false;
        if (answer == QMessageBox::Save && !saveTitle()) return false;
    }
    // "Fast Save remains in effect until you ... exit the Title tool" (p.358).
    fastSave_ = false;
    hide();
    return true;
}

bool TitleToolWindow::exportTitle(const QString& path) {
    finishEditing();
    const QImage image = media::classicTitleImage(title_, QSize(static_cast<int>(model::kClassicWidth), static_cast<int>(model::kClassicHeight)), true,
                                                  model::classicPageOrigin(title_.motion, page()));
    return image.convertToFormat(QImage::Format_ARGB32).save(path);
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
    selected_ = title_.withGroups(ids);
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

bool TitleToolWindow::withinLimit() const {
    return media::classicContentPages(title_) <= model::classicPageLimit(title_.motion, lines_);
}

void TitleToolWindow::fitPages() {
    if (title_.motion != model::ClassicMotion::Still && title_.autoSize) title_.pages = media::classicContentPages(title_);
    if (title_.motion == model::ClassicMotion::Still) title_.pages = 1;
}

void TitleToolWindow::showLimit() {
    // "a message box opens. To cancel the operation and keep the title within the page count limit, click OK" (p.342).
    auto* box = new QMessageBox(QMessageBox::Information, tr("Title Tool"),
                                tr("This would make the title longer than its %1 pages.").arg(model::classicPageLimit(title_.motion, lines_)),
                                QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("title:pageLimit"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    // A QPointer, not a connection to destroyed(): the box is a child that dies inside the window's own destructor,
    // when a slot on the window would run on a half-destroyed object.
    limitMessage_ = box;
    box->open();
}

QWidget* TitleToolWindow::limitMessage() const {
    return limitMessage_;
}

void TitleToolWindow::commit(const ClassicTitle& before) {
    // "You cannot change the object until you unlock it" (p.356).
    model::keepLockedObjects(title_, before);
    // "The template prevents you from changing the position, colors, shadows, or graphics" (p.360).
    model::keepTemplateObjects(title_, before, moveableTemplates_);
    if (!withinLimit()) {
        title_ = before;
        showLimit();
    }
    fitPages();
    syncScrollBars();
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

// ---- line attributes, colour and transparency, shadows (6.8d3)

void TitleToolWindow::attribute(const std::function<void(ClassicObject&)>& change) {
    const ClassicTitle before = title_;
    QList<ClassicObject*> targets;
    if (ClassicObject* edited = title_.object(editing_)) {
        targets << edited;
    } else {
        for (const QString& id : selected_) {
            if (ClassicObject* o = title_.object(id)) targets << o;
        }
    }
    if (targets.isEmpty()) {
        change(defaults_);
        refreshControls();
        return;
    }
    for (ClassicObject* o : targets) change(*o);
    if (editing_.isEmpty()) {
        commit(before);
    } else {
        // Typing makes one undo step when it ends (finishEditing).
        refreshControls();
        canvas_->update();
    }
}

const ClassicObject& TitleToolWindow::shownObject() const {
    if (const ClassicObject* o = title_.object(editing_)) return *o;
    for (const QString& id : selected_) {
        if (const ClassicObject* o = title_.object(id)) return *o;
    }
    return defaults_;
}

ClassicObject TitleToolWindow::freshObject(ClassicKind kind) const {
    ClassicObject o = defaults_;
    o.kind = kind;
    o.text.clear();
    o.runs.clear();
    o.id = title_.newId(kind);
    return o;
}

void TitleToolWindow::setCornerRadius(double pixels) {
    pixels = std::clamp(pixels, 0.0, 200.0);
    attribute([&](ClassicObject& o) { o.cornerRadius = pixels; });
}

void TitleToolWindow::setLineWidth(double pixels) {
    pixels = std::clamp(pixels, 0.0, 200.0);
    attribute([&](ClassicObject& o) {
        // "If you selected a line for modification, the width of the line changes. If you selected an object for
        // modification, the width of the border changes" (p.348); a line keeps its width on Remove Border (ours).
        if (o.kind != ClassicKind::Line || &o == &defaults_) o.borderWidth = pixels;
        if ((o.kind == ClassicKind::Line || &o == &defaults_) && pixels > 0.0) o.lineWidth = pixels;
    });
}

void TitleToolWindow::setArrows(model::ClassicArrows arrows) {
    attribute([&](ClassicObject& o) {
        if (o.kind == ClassicKind::Line || &o == &defaults_) o.arrows = arrows;
    });
}

void TitleToolWindow::setArrowSize(const QSizeF& size) {
    const QSizeF clamped(std::clamp(size.width(), 0.5, 20.0), std::clamp(size.height(), 0.5, 20.0));
    attribute([&](ClassicObject& o) {
        if (o.kind == ClassicKind::Line || &o == &defaults_) o.arrowSize = clamped;
    });
}

void TitleToolWindow::setColor(Coat coat, const QString& color) {
    const QString c = safeColors_ ? model::safeColor(color) : color;
    attribute([&](ClassicObject& o) {
        if (coat == Coat::Shadow) {
            o.shadowColor = c;
            return;
        }
        model::ClassicPaint& paint = coat == Coat::Fill ? o.fill : o.border;
        paint.color = c;
        paint.color2.clear();
    });
}

void TitleToolWindow::setTransparency(Coat coat, double transparency) {
    transparency = std::clamp(transparency, 0.0, 100.0);
    attribute([&](ClassicObject& o) {
        if (coat == Coat::Shadow) {
            o.shadowTransparency = transparency;
            return;
        }
        model::ClassicPaint& paint = coat == Coat::Fill ? o.fill : o.border;
        paint.transparency = transparency;
        paint.transparency2.reset();
    });
}

void TitleToolWindow::setBlendColor(Coat coat, int which, const QString& color) {
    if (coat == Coat::Shadow) return; // "You cannot blend the shadow for a title object" (p.350)
    const QString c = safeColors_ ? model::safeColor(color) : color;
    attribute([&](ClassicObject& o) {
        model::ClassicPaint& paint = coat == Coat::Fill ? o.fill : o.border;
        if (paint.color2.isEmpty()) paint.color2 = paint.color;
        (which == 0 ? paint.color : paint.color2) = c;
    });
}

void TitleToolWindow::setBlendTransparency(Coat coat, int which, double transparency) {
    if (coat == Coat::Shadow) return;
    transparency = std::clamp(transparency, 0.0, 100.0);
    attribute([&](ClassicObject& o) {
        model::ClassicPaint& paint = coat == Coat::Fill ? o.fill : o.border;
        if (!paint.transparency2) paint.transparency2 = paint.transparency;
        if (which == 0) paint.transparency = transparency;
        else paint.transparency2 = transparency;
    });
}

void TitleToolWindow::setBlendAngle(Coat coat, bool transparency, double degrees) {
    if (coat == Coat::Shadow) return;
    degrees = std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
    attribute([&](ClassicObject& o) {
        model::ClassicPaint& paint = coat == Coat::Fill ? o.fill : o.border;
        (transparency ? paint.transparencyAngle : paint.colorAngle) = degrees;
    });
}

void TitleToolWindow::chooseBox(Coat coat, bool transparency, int blend) {
    activeCoat_ = coat;
    activeTransparency_ = transparency;
    activeBlend_ = coat == Coat::Shadow ? -1 : blend;
    refreshControls();
}

void TitleToolWindow::setDepthShadow(bool depth) {
    attribute([&](ClassicObject& o) { o.depthShadow = depth; });
}

void TitleToolWindow::setShadowDepth(double pixels) {
    pixels = std::clamp(pixels, 0.0, 500.0);
    attribute([&](ClassicObject& o) { o.shadowDepth = pixels; });
}

void TitleToolWindow::setShadowOffset(const QPointF& offset) {
    attribute([&](ClassicObject& o) { o.setShadowOffset(offset); });
}

void TitleToolWindow::setShadowSoftness(int softness) {
    // "The value that you set applies immediately to any selected title. The value also becomes the new default"
    // (p.354).
    const int value = legalSoftness(softness, shownObject().shadowSoftness);
    sessionSoftness = value;
    defaults_.shadowSoftness = value;
    attribute([&](ClassicObject& o) { o.shadowSoftness = value; });
}

int TitleToolWindow::defaultShadowSoftness() {
    return sessionSoftness;
}

void TitleToolWindow::openSoftenShadow() {
    // "Select Object > Soften Shadow. The Soft Shadow dialog box opens. Type a number in the Shadow Softness text box
    // ... Click Apply ... Click OK" (p.354).
    QDialog box(this);
    box.setObjectName(QStringLiteral("title:softShadow"));
    box.setWindowTitle(tr("Soft Shadow"));
    auto* layout = new QVBoxLayout(&box);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Shadow Softness:"), &box));
    auto* value = new QSpinBox(&box);
    value->setObjectName(QStringLiteral("title:shadowSoftness"));
    value->setRange(0, 40);
    value->setValue(shownObject().shadowSoftness);
    row->addWidget(value);
    layout->addLayout(row);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &box);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this, value] {
        setShadowSoftness(value->value());
        value->setValue(shownObject().shadowSoftness);
    });
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    if (box.exec() == QDialog::Accepted) setShadowSoftness(value->value());
}

// ---- templates and styles (6.8d4b)

bool TitleToolWindow::saveTemplate(const QString& path) {
    finishEditing();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    // The template is the title itself, its objects no longer marked as another template's.
    ClassicTitle templ = title_;
    for (ClassicObject& o : templ.objects) o.fromTemplate = false;
    return file.write(QJsonDocument(model::classicTemplateJson(templ)).toJson()) > 0;
}

bool TitleToolWindow::includeTemplate(const QString& path) {
    finishEditing();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto templ = model::classicTemplateFrom(QJsonDocument::fromJson(file.readAll()).object());
    if (!templ) return false;
    const ClassicTitle before = title_;
    selected_ = model::includeClassicTemplate(title_, *templ);
    commit(before);
    return true;
}

void TitleToolWindow::setMoveableTemplates(bool on) {
    moveableTemplates_ = on;
    refreshControls();
}

QList<model::ClassicTitleStyle> TitleToolWindow::styles() const {
    return styleStore_.styles ? styleStore_.styles() : ownStyles_;
}

bool TitleToolWindow::applyStyle(const QString& name) {
    for (const model::ClassicTitleStyle& style : styles()) {
        if (style.name != name) continue;
        attribute([&](ClassicObject& o) { model::applyClassicStyle(o, style); });
        return true;
    }
    return false;
}

model::ClassicTitleStyle TitleToolWindow::styleOfSelection(const QString& name) const {
    return model::classicStyleOf(shownObject(), name);
}

void TitleToolWindow::saveStyle(const model::ClassicTitleStyle& style) {
    if (style.name.trimmed().isEmpty()) return;
    if (styleStore_.save) {
        styleStore_.save(style);
    } else {
        ownStyles_.erase(std::remove_if(ownStyles_.begin(), ownStyles_.end(), [&](const model::ClassicTitleStyle& s) { return s.name == style.name; }),
                         ownStyles_.end());
        ownStyles_ << style;
    }
    fillStyleMenus();
}

void TitleToolWindow::saveStyleAs() {
    // "Select Save As. The Title Style Sheet dialog box opens" (p.362).
    const model::ClassicTitleStyle suggestion = styleOfSelection(QString());
    const auto style = askStyle_ ? askStyle_(suggestion) : titleStyleSheet(this, suggestion);
    if (style) saveStyle(*style);
}

void TitleToolWindow::fillStyleMenus() {
    auto* stylesButton = qobject_cast<QToolButton*>(controls_.value(QStringLiteral("styles")));
    auto* saveButton = qobject_cast<QToolButton*>(controls_.value(QStringLiteral("saveStyle")));
    if (!stylesButton || !saveButton) return;
    QMenu* stylesMenu = stylesButton->menu();
    QMenu* saveMenu = saveButton->menu();
    stylesMenu->clear();
    saveMenu->clear();
    // "The menu contains a scaled visual representation of each style" (p.363): ours, "Aa" in the style.
    for (const model::ClassicTitleStyle& style : styles()) {
        model::ClassicObject sample = defaults_;
        sample.kind = ClassicKind::Text;
        sample.text = QStringLiteral("Aa");
        sample.size = 72;
        sample.rect = QRectF(20, 10, 400, 0);
        model::applyClassicStyle(sample, style);
        sample.size = 72;
        ClassicTitle t;
        t.objects << sample;
        QPixmap icon(48, 27);
        icon.fill(Qt::darkGray);
        {
            QPainter p(&icon);
            p.drawImage(0, 0, media::classicTitleImage(t, icon.size()));
        }
        const QString label = style.functionKey > 0 ? QStringLiteral("%1\tF%2").arg(style.name).arg(style.functionKey) : style.name;
        const QString name = style.name;
        stylesMenu->addAction(QIcon(icon), label, this, [this, name] { applyStyle(name); });
        saveMenu->addAction(label, this, [this, name] { applyStyle(name); });
    }
    if (!saveMenu->isEmpty()) saveMenu->addSeparator();
    saveMenu->addAction(tr("Save As..."), this, &TitleToolWindow::saveStyleAs)->setObjectName(QStringLiteral("title:saveStyleAs"));
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
    if (!o || o->locked || o->fromTemplate) return std::nullopt;
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
            // An object's group comes with it (p.356).
            const QStringList unit = title_.withGroups({hit});
            if (shift) {
                if (selected_.contains(hit)) {
                    for (const QString& id : unit) selected_.removeAll(id);
                } else {
                    for (const QString& id : unit) {
                        selected_.removeAll(id);
                        selected_ << id;
                    }
                }
            } else if (!selected_.contains(hit)) {
                selected_ = unit;
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
        if (o && o->kind == ClassicKind::Text && !o->locked) {
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
            selected_ = title_.withGroups({hit});
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
            // In a rolling or crawling title, "lines of text wrap within the safe title area" (p.343) of the page.
            const QPointF page = model::classicPageOrigin(title_.motion, model::classicPageAt(title_.motion, p));
            t.rect = QRectF(p.x(), p.y(), std::max(120.0, page.x() + safeArea(0.8).right() - p.x()), 0.0);
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
            if (ClassicObject* o = title_.object(id); o && !o->locked && (!o->fromTemplate || moveableTemplates_)) o->rect.translate(by);
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
            ClassicObject o = freshObject(tool_ == Tool::Box ? ClassicKind::Box : tool_ == Tool::Oval ? ClassicKind::Oval : ClassicKind::Line);
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
            selected_ = title_.withGroups(selected_);
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
    // "If the style is assigned to a function key, press the appropriate function key" (p.364).
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12 && !ctrl && !alt && !shift) {
        const int number = key - Qt::Key_F1 + 1;
        for (const model::ClassicTitleStyle& style : styles()) {
            if (style.functionKey == number) return applyStyle(style.name);
        }
        return false;
    }
    if ((key == Qt::Key_Control || key == Qt::Key_Alt) && ctrl && alt) {
        temporary_ = Tool::Selection;
        return true;
    }
    if (ClassicObject* o = title_.object(editing_)) {
        const qsizetype from = std::min(caret_, anchor_), to = std::max(caret_, anchor_);
        // "If you reach the page count limit while you are typing text, the title stops getting bigger and text entry
        // stops" (p.342): a change that would pass it is taken back.
        const ClassicObject saved = *o;
        const qsizetype savedCaret = caret_, savedAnchor = anchor_;
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
        if (ClassicObject* now = title_.object(editing_); now && !withinLimit()) {
            *now = saved;
            caret_ = savedCaret;
            anchor_ = savedAnchor;
        }
        if (ClassicObject* now = title_.object(editing_)) {
            fitPages();
            // "the title scrolls down as you type" (p.343): the cursor kept in view.
            if (title_.motion == model::ClassicMotion::Rolling) {
                const QLineF c = media::classicTextCaret(*now, caret_);
                if (c.y2() > scroll_.y() + model::kClassicHeight) setScroll(QPointF(scroll_.x(), c.y2() - model::kClassicHeight * 0.8));
                if (c.y1() < scroll_.y()) setScroll(QPointF(scroll_.x(), c.y1() - model::kClassicHeight * 0.2));
            }
            syncScrollBars();
        }
        refreshControls();
        canvas_->update();
        return true;
    }
    // "Press the Shift key, and use the arrow keys to move the shadow to any position around the title" (p.353); with
    // Alt too, Up and Down change the shadow softness by one (p.354).
    if (shift && (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down)) {
        if (alt) {
            if (key == Qt::Key_Up || key == Qt::Key_Down) setShadowSoftness(shownObject().shadowSoftness + (key == Qt::Key_Up ? 1 : -1));
            return true;
        }
        const QPointF by(key == Qt::Key_Left ? -1 : key == Qt::Key_Right ? 1 : 0, key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : 0);
        const QPointF now = shownObject().shadowOffset();
        setShadowOffset(QPointF(std::round(now.x()), std::round(now.y())) + by);
        return true;
    }
    // Scrolling a rolling or crawling title (p.344): Home and End, Page Up and Page Down by a page, and (rolling titles,
    // ours: with nothing selected) the Up and Down Arrows by a line (ours: 54 pixels, a twentieth of the frame).
    if (title_.motion != model::ClassicMotion::Still) {
        const bool roll = title_.motion == model::ClassicMotion::Rolling;
        const double pageLength = roll ? model::kClassicHeight : model::kClassicWidth;
        auto along = [&](double v) { setScroll(roll ? QPointF(scroll_.x(), v) : QPointF(v, scroll_.y())); };
        const double now = roll ? scroll_.y() : scroll_.x();
        switch (key) {
        case Qt::Key_Home: along(0.0); return true;
        case Qt::Key_End: along((title_.pages - 1) * pageLength); return true;
        case Qt::Key_PageDown: along(now + pageLength); return true;
        case Qt::Key_PageUp: along(now - pageLength); return true;
        case Qt::Key_Down:
        case Qt::Key_Up:
            if (roll && selected_.isEmpty()) {
                along(now + (key == Qt::Key_Down ? 1 : -1) * model::kClassicHeight / 20.0);
                return true;
            }
            break;
        default: break;
        }
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
            if (ClassicObject* c = title_.object(id); c && !c->locked) c->rect.translate(by);
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
    insertCopies(ClassicTitle::fromJson(QJsonDocument::fromJson(clipboard_.toUtf8()).object()).objects, QPointF());
}

void TitleToolWindow::insertCopies(const QList<ClassicObject>& objects, const QPointF& by) {
    const ClassicTitle before = title_;
    selected_.clear();
    QHash<QString, QString> groups; // the copied groups' new ids
    for (ClassicObject o : objects) {
        o.id = title_.newId(o.kind);
        o.rect.translate(by);
        o.locked = false;
        if (!o.group.isEmpty()) {
            if (!groups.contains(o.group)) groups.insert(o.group, title_.newGroupId());
            o.group = groups.value(o.group);
        }
        title_.objects << o;
        selected_ << o.id;
    }
    commit(before);
}

void TitleToolWindow::duplicate() {
    finishEditing();
    QList<ClassicObject> copies;
    for (const ClassicObject& o : title_.objects) {
        if (selected_.contains(o.id)) copies << o;
    }
    if (!copies.isEmpty()) insertCopies(copies, QPointF(20.0, 20.0));
}

void TitleToolWindow::deselectAll() {
    select({});
}

bool TitleToolWindow::reorder(model::ClassicLayer layer) {
    finishEditing();
    const ClassicTitle before = title_;
    if (!model::reorderClassicObjects(title_, selected_, layer)) return false;
    commit(before);
    return true;
}

bool TitleToolWindow::group() {
    finishEditing();
    const ClassicTitle before = title_;
    if (!model::groupClassicObjects(title_, selected_)) return false;
    commit(before);
    return title_ != before;
}

bool TitleToolWindow::ungroup() {
    finishEditing();
    const ClassicTitle before = title_;
    if (!model::ungroupClassicObjects(title_, selected_)) return false;
    commit(before);
    return title_ != before;
}

bool TitleToolWindow::setLocked(bool locked) {
    finishEditing();
    const ClassicTitle before = title_;
    for (const QString& id : selected_) {
        if (ClassicObject* o = title_.object(id)) o->locked = locked;
    }
    commit(before);
    return title_ != before;
}

bool TitleToolWindow::align(media::ClassicAlign how) {
    finishEditing();
    const ClassicTitle before = title_;
    // "Aligning to the frame and distributing refers to the safe title area" (p.355), of the page shown.
    const QRectF area = safeArea(0.8).translated(model::classicPageOrigin(title_.motion, page()));
    if (!media::alignClassicObjects(title_, selected_, how, area)) return false;
    commit(before);
    return true;
}

bool TitleToolWindow::distribute(media::ClassicDistribute how) {
    finishEditing();
    const ClassicTitle before = title_;
    const QRectF area = safeArea(0.8).translated(model::classicPageOrigin(title_.motion, page()));
    if (!media::distributeClassicObjects(title_, selected_, how, area)) return false;
    commit(before);
    return true;
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

// ---- rolling and crawling titles

void TitleToolWindow::setMotion(model::ClassicMotion motion) {
    finishEditing();
    const ClassicTitle before = title_;
    title_.motion = motion;
    if (motion != model::ClassicMotion::Still) {
        title_.videoBackground = true;
        title_.autoSize = true;
    }
    scroll_ = {};
    commit(before);
    if (title_.videoBackground) updateBackground();
}

void TitleToolWindow::setAutoSize(bool on) {
    const ClassicTitle before = title_;
    title_.autoSize = on;
    commit(before);
}

bool TitleToolWindow::addPage() {
    if (title_.motion == model::ClassicMotion::Still || title_.autoSize) return false;
    if (title_.pages + 1 > model::classicPageLimit(title_.motion, lines_)) {
        showLimit();
        return false;
    }
    const ClassicTitle before = title_;
    ++title_.pages;
    commit(before);
    return true;
}

bool TitleToolWindow::insertPage(int page) {
    finishEditing();
    const ClassicTitle before = title_;
    if (!media::insertClassicPage(title_, page, lines_)) return false;
    commit(before);
    return true;
}

bool TitleToolWindow::removePage(int page) {
    finishEditing();
    const ClassicTitle before = title_;
    if (!media::removeClassicPage(title_, page)) return false;
    commit(before);
    return true;
}

bool TitleToolWindow::copyToPage(int page) {
    finishEditing();
    const ClassicTitle before = title_;
    QStringList copies;
    for (const QString& id : selected_) {
        const int count = static_cast<int>(title_.objects.size());
        if (media::placeOnClassicPage(title_, id, page, true, lines_)) copies << title_.objects.at(count).id;
    }
    if (copies.isEmpty()) return false;
    selected_ = copies;
    commit(before);
    return true;
}

bool TitleToolWindow::moveToPage(int page) {
    finishEditing();
    const ClassicTitle before = title_;
    bool any = false;
    for (const QString& id : selected_) any = media::placeOnClassicPage(title_, id, page, false, lines_) || any;
    if (!any) return false;
    commit(before);
    return true;
}

bool TitleToolWindow::makeCrawl() {
    finishEditing();
    const ClassicTitle before = title_;
    bool any = false;
    for (const QString& id : selected_) {
        if (ClassicObject* o = title_.object(id)) any = media::makeClassicCrawl(*o) || any;
    }
    if (!any) return false;
    commit(before);
    return true;
}

void TitleToolWindow::setScroll(const QPointF& at) {
    // Within the title's pages, and only along its motion.
    const double maxY = title_.motion == model::ClassicMotion::Rolling ? (title_.pages - 1) * model::kClassicHeight : 0.0;
    const double maxX = title_.motion == model::ClassicMotion::Crawling ? (title_.pages - 1) * model::kClassicWidth : 0.0;
    scroll_ = QPointF(std::clamp(at.x(), 0.0, maxX), std::clamp(at.y(), 0.0, maxY));
    syncScrollBars();
    refreshControls();
    canvas_->update();
}

int TitleToolWindow::page() const {
    return model::classicPageAt(title_.motion, scroll_ + QPointF(1, 1));
}

void TitleToolWindow::setPage(int page) {
    // "Type a page number for a specific page ... The selected page appears" (p.345).
    setScroll(model::classicPageOrigin(title_.motion, std::clamp(page, 1, title_.pages)));
}

void TitleToolWindow::syncScrollBars() {
    if (!rollBar_ || !crawlBar_) return;
    const bool wasSyncing = syncing_;
    syncing_ = true;
    const bool roll = title_.motion == model::ClassicMotion::Rolling, crawl = title_.motion == model::ClassicMotion::Crawling;
    rollBar_->setVisible(roll);
    crawlBar_->setVisible(crawl);
    rollBar_->setRange(0, roll ? static_cast<int>((title_.pages - 1) * model::kClassicHeight) : 0);
    rollBar_->setPageStep(static_cast<int>(model::kClassicHeight));
    rollBar_->setSingleStep(static_cast<int>(model::kClassicHeight / 20.0));
    rollBar_->setValue(static_cast<int>(scroll_.y()));
    crawlBar_->setRange(0, crawl ? static_cast<int>((title_.pages - 1) * model::kClassicWidth) : 0);
    crawlBar_->setPageStep(static_cast<int>(model::kClassicWidth));
    crawlBar_->setSingleStep(static_cast<int>(model::kClassicWidth / 20.0));
    crawlBar_->setValue(static_cast<int>(scroll_.x()));
    syncing_ = wasSyncing;
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
    const bool moving = title_.motion != model::ClassicMotion::Still;
    tick(QStringLiteral("roll"), title_.motion == model::ClassicMotion::Rolling);
    tick(QStringLiteral("crawl"), title_.motion == model::ClassicMotion::Crawling);
    for (const QString& name : {QStringLiteral("roll"), QStringLiteral("crawl")}) {
        if (auto* b = qobject_cast<QToolButton*>(controls_.value(name))) b->setStyleSheet(b->isChecked() ? QStringLiteral("background-color: #2e8b3e;") : QString());
    }
    if (auto* p = qobject_cast<QSpinBox*>(controls_.value(QStringLiteral("page")))) {
        p->setVisible(moving);
        p->setRange(1, std::max(1, title_.pages));
        p->setValue(page());
    }
    tick(QStringLiteral("autoSize"), title_.autoSize);
    for (const QString& name : {QStringLiteral("autoSize"), QStringLiteral("insertPage"), QStringLiteral("removePage"), QStringLiteral("copyToPage"),
                                QStringLiteral("moveToPage")}) {
        if (QAction* a = actions_.value(name)) a->setEnabled(moving);
    }
    if (QAction* a = actions_.value(QStringLiteral("addPage"))) a->setEnabled(moving && !title_.autoSize);
    // The line attribute tools, colour and transparency boxes and shadow tools show the first selected object's.
    const ClassicObject& shownAll = shownObject();
    if (auto* b = qobject_cast<QToolButton*>(controls_.value(QStringLiteral("boxCorner")))) {
        b->setText(shownAll.cornerRadius > 0 ? QString(QChar(0x256F)) + QString::number(shownAll.cornerRadius) : QString(QChar(0x2518)));
    }
    if (auto* b = qobject_cast<QToolButton*>(controls_.value(QStringLiteral("borderWidth")))) {
        const double w = shownAll.kind == ClassicKind::Line ? shownAll.lineWidth : shownAll.borderWidth;
        b->setText(w > 0 ? QString::number(w) : QString(QChar(0x2508)));
    }
    if (auto* b = qobject_cast<QToolButton*>(controls_.value(QStringLiteral("arrowhead")))) {
        const QChar arrows[] = {QChar(0x2014), QChar(0x2192), QChar(0x2190), QChar(0x2194)};
        b->setText(QString(arrows[static_cast<int>(shownAll.arrows)]));
    }
    auto swatch = [&](const QString& name, const QString& color, bool active) {
        if (QWidget* w = controls_.value(name)) {
            w->setStyleSheet(QStringLiteral("background-color: %1; border: 2px solid %2;")
                                 .arg(QColor(color).name(), active ? QStringLiteral("#2e8b3e") : QStringLiteral("#606060")));
        }
    };
    auto level = [&](const QString& name, double transparency, bool active) {
        // "The Transparency Level box displays the text "Hi" with a background that changes from black to white. A
        // completely white background represents a fully transparent object" (p.352).
        const int grey = static_cast<int>(std::lround(std::clamp(transparency, 0.0, 100.0) * 2.55));
        if (QWidget* w = controls_.value(name)) {
            w->setStyleSheet(QStringLiteral("background-color: rgb(%1,%1,%1); color: %2; border: 2px solid %3;")
                                 .arg(grey)
                                 .arg(grey > 128 ? QStringLiteral("black") : QStringLiteral("white"), active ? QStringLiteral("#2e8b3e") : QStringLiteral("#606060")));
        }
    };
    const bool colorBox = !activeTransparency_;
    swatch(QStringLiteral("fillColor"), shownAll.fill.color, colorBox && activeCoat_ == Coat::Fill);
    swatch(QStringLiteral("shadowColor"), shownAll.shadowColor, colorBox && activeCoat_ == Coat::Shadow);
    swatch(QStringLiteral("borderColor"), shownAll.border.color, colorBox && activeCoat_ == Coat::Border);
    level(QStringLiteral("fillTransparency"), shownAll.fill.transparency, !colorBox && activeCoat_ == Coat::Fill);
    level(QStringLiteral("shadowTransparency"), shownAll.shadowTransparency, !colorBox && activeCoat_ == Coat::Shadow);
    level(QStringLiteral("borderTransparency"), shownAll.border.transparency, !colorBox && activeCoat_ == Coat::Border);
    // "The Color and Transparency Blend tools appear when you select a Fill or Border Color Selection box or
    // Transparency Level box" (p.350); the slider with a Transparency Level box (p.352).
    const bool blending = activeCoat_ != Coat::Shadow;
    if (QWidget* w = controls_.value(QStringLiteral("blendTools"))) w->setVisible(blending);
    if (QWidget* w = controls_.value(QStringLiteral("transparencySlider"))) w->setVisible(activeTransparency_);
    if (blending) {
        const model::ClassicPaint& paint = activeCoat_ == Coat::Border ? shownAll.border : shownAll.fill;
        QColor a, b;
        if (activeTransparency_) {
            const double t1 = paint.transparency, t2 = paint.transparency2.value_or(t1);
            a = QColor::fromRgbF(static_cast<float>(t1 / 100.0), static_cast<float>(t1 / 100.0), static_cast<float>(t1 / 100.0));
            b = QColor::fromRgbF(static_cast<float>(t2 / 100.0), static_cast<float>(t2 / 100.0), static_cast<float>(t2 / 100.0));
        } else {
            a = QColor(paint.color);
            b = QColor(paint.color2.isEmpty() ? paint.color : paint.color2);
        }
        swatch(QStringLiteral("blend1"), a.name(), activeBlend_ == 0);
        swatch(QStringLiteral("blend2"), b.name(), activeBlend_ == 1);
        if (auto* d = dynamic_cast<BlendBox*>(controls_.value(QStringLiteral("blendDirection")))) {
            d->display(a, b, activeTransparency_ ? paint.transparencyAngle : paint.colorAngle);
        }
    }
    if (auto* slider = qobject_cast<QSlider*>(controls_.value(QStringLiteral("transparencySlider")))) {
        double value = shownAll.shadowTransparency;
        if (activeCoat_ != Coat::Shadow) {
            const model::ClassicPaint& paint = activeCoat_ == Coat::Border ? shownAll.border : shownAll.fill;
            value = activeBlend_ == 1 ? paint.transparency2.value_or(paint.transparency) : paint.transparency;
        }
        slider->setValue(static_cast<int>(std::lround(value)));
    }
    if (auto* pad = dynamic_cast<ShadowPad*>(controls_.value(QStringLiteral("shadowDirection")))) pad->display(shownAll.shadowOffset(), shownAll.shadowSoftness);
    if (auto* d = qobject_cast<QSpinBox*>(controls_.value(QStringLiteral("shadowDepth")))) d->setValue(static_cast<int>(std::lround(shownAll.shadowDepth)));
    if (auto* k = qobject_cast<QToolButton*>(controls_.value(QStringLiteral("shadowType")))) {
        k->setChecked(shownAll.depthShadow);
        k->setText(shownAll.depthShadow ? tr("Depth") : tr("Drop"));
    }
    if (QAction* a = actions_.value(QStringLiteral("undo"))) a->setEnabled(!undo_.isEmpty() || !editing_.isEmpty());
    if (QAction* a = actions_.value(QStringLiteral("redo"))) a->setEnabled(!redo_.isEmpty());
    syncing_ = false;
}

// ---------------------------------------------------------------- New Title

std::optional<model::ClassicTitleStyle> titleStyleSheet(QWidget* parent, const model::ClassicTitleStyle& style) {
    QDialog box(parent);
    box.setObjectName(QStringLiteral("title:styleSheet"));
    box.setWindowTitle(QObject::tr("Title Style Sheet"));
    auto* layout = new QVBoxLayout(&box);
    // The sample window: "Aa" in the style's checked attributes.
    auto* sample = new QLabel(&box);
    sample->setObjectName(QStringLiteral("title:styleSample"));
    sample->setFixedSize(256, 144);
    layout->addWidget(sample, 0, Qt::AlignHCenter);
    auto* grid = new QGridLayout;
    QList<QCheckBox*> boxes;
    for (int i = 0; i < model::kClassicStyleFields; ++i) {
        const auto field = static_cast<model::ClassicStyleField>(i);
        auto* check = new QCheckBox(model::classicStyleFieldName(field), &box);
        check->setObjectName(QStringLiteral("title:styleField%1").arg(i));
        check->setChecked(style.includes(field));
        grid->addWidget(check, i % 9, i / 9);
        boxes << check;
    }
    layout->addLayout(grid);
    auto* all = new QPushButton(QObject::tr("Select All"), &box);
    auto* none = new QPushButton(QObject::tr("Select None"), &box);
    auto* revert = new QPushButton(QObject::tr("Revert"), &box);
    auto* marquee = new QPushButton(QObject::tr("Convert to Marquee"), &box);
    marquee->setEnabled(false); // ours: no Marquee
    auto* row = new QHBoxLayout;
    for (QPushButton* b : {all, none, revert, marquee}) row->addWidget(b);
    layout->addLayout(row);
    auto* form = new QFormLayout;
    auto* key = new QSpinBox(&box);
    key->setObjectName(QStringLiteral("title:styleKey"));
    key->setRange(0, 12);
    key->setSpecialValueText(QObject::tr("None"));
    key->setValue(style.functionKey);
    form->addRow(QObject::tr("Function Key:"), key);
    auto* name = new QLineEdit(style.name, &box);
    name->setObjectName(QStringLiteral("title:styleName"));
    form->addRow(QObject::tr("Style Name:"), name);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(&box);
    QPushButton* done = buttons->addButton(QObject::tr("Done"), QDialogButtonBox::AcceptRole);
    done->setObjectName(QStringLiteral("title:styleDone"));
    buttons->addButton(QObject::tr("Close"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    auto current = [&] {
        model::ClassicTitleStyle s = style;
        s.fields.clear();
        for (int i = 0; i < boxes.size(); ++i) {
            if (boxes.at(i)->isChecked()) s.fields << static_cast<model::ClassicStyleField>(i);
        }
        s.functionKey = key->value();
        s.name = name->text().trimmed();
        return s;
    };
    auto refresh = [&] {
        model::ClassicObject o;
        o.kind = ClassicKind::Text;
        o.text = QStringLiteral("Aa");
        o.size = 400;
        o.rect = QRectF(560, 300, 1400, 0);
        model::applyClassicStyle(o, current());
        ClassicTitle t;
        t.objects << o;
        QPixmap pm(sample->size());
        pm.fill(Qt::darkGray);
        QPainter p(&pm);
        p.drawImage(0, 0, media::classicTitleImage(t, pm.size()));
        p.end();
        sample->setPixmap(pm);
    };
    for (QCheckBox* c : boxes) QObject::connect(c, &QCheckBox::toggled, &box, refresh);
    QObject::connect(all, &QPushButton::clicked, &box, [&] {
        for (QCheckBox* c : boxes) c->setChecked(true);
    });
    QObject::connect(none, &QPushButton::clicked, &box, [&] {
        for (QCheckBox* c : boxes) c->setChecked(false);
    });
    QObject::connect(revert, &QPushButton::clicked, &box, [&] {
        for (int i = 0; i < boxes.size(); ++i) boxes.at(i)->setChecked(style.includes(static_cast<model::ClassicStyleField>(i)));
        key->setValue(style.functionKey);
        name->setText(style.name);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &box, [&] {
        // "Close ... A message box opens, asking whether you want to cancel or save your changes" (p.363).
        if (current() == style) {
            box.reject();
            return;
        }
        const auto answer = QMessageBox::question(&box, QObject::tr("Title Style Sheet"), QObject::tr("Save the changes to this style?"),
                                                  QMessageBox::Save | QMessageBox::Cancel);
        if (answer == QMessageBox::Save) box.accept();
        else box.reject();
    });
    refresh();
    if (box.exec() != QDialog::Accepted) return std::nullopt;
    const model::ClassicTitleStyle result = current();
    if (result.name.isEmpty()) return std::nullopt;
    return result;
}

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
