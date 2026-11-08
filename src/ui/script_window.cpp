// The Script window (lumenis/ui/script_window.h; Editing Guide p.570-582).
#include "lumenis/ui/script_window.h"

#include "lumenis/ui/bin_dock.h"

#include <QAbstractTextDocumentLayout>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextFrame>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

namespace {
constexpr int kRightMargin = 60; // the page numbers' column (ours)
constexpr int kSlateGap = 8;     // between slates side by side (ours)
constexpr int kFrameWidth = 96;  // a slate's frame at Enlarge/Reduce Frame's 1.0 (ours; 16:9)
constexpr int kEndGrab = 5;      // how near a take's end Ctrl+drag takes hold of it (pixels, ours)

// Where a slate and its takes are drawn, in viewport coordinates.
struct SlateGeometry {
    QString slateId;
    QRect box;   // the frame and clip name
    QRect frame; // empty without Show Frames
    QRect name;
    struct Take {
        QString id;
        QRect tab;
        int x = 0; // the take line's column
        int top = 0;
        int bottom = 0;
    };
    QList<Take> takes;
};

// What a point in the viewport is on.
struct SlateHit {
    enum Kind { None, Box, Tab, Line, Mark } kind = None;
    QString slateId;
    QString takeId;
    int line = -1;          // the script line under the point (Line)
    double fraction = 0.0;  // how far down that line
    int end = 0;            // -1 the take's beginning mark, 1 its end mark, 0 neither (Line)
};
} // namespace

// The text with its margins: scene (and line) numbers on the left, page numbers on the right, shaded rows.
class ScriptTextView : public QTextEdit {
public:
    explicit ScriptTextView(ScriptWindow& window) : QTextEdit(&window), window_(window) {
        setObjectName(QStringLiteral("scriptText"));
        setAcceptRichText(false);
        setReadOnly(true);
        setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        left_ = new QWidget(this);
        right_ = new QWidget(this);
        left_->installEventFilter(this);
        right_->installEventFilter(this);
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
            left_->update();
            right_->update();
        });
        connect(document(), &QTextDocument::contentsChanged, this, [this] {
            left_->update();
            right_->update();
        });
    }
    void setLineNumbers(bool on) {
        lineNumbers_ = on;
        layoutMargins();
    }
    void setShadedRows(bool on) {
        rows_ = on;
        viewport()->update();
    }
    bool lineNumbers() const { return lineNumbers_; }
    void layoutMargins() {
        const int left = window_.script_.layout.leftMargin + (lineNumbers_ ? fontMetrics().horizontalAdvance(QStringLiteral("00000")) : 0);
        setViewportMargins(left, 0, kRightMargin, 0);
        const QRect r = contentsRect();
        left_->setGeometry(r.left(), r.top(), left, r.height());
        right_->setGeometry(r.right() - kRightMargin + 1, r.top(), kRightMargin, r.height());
    }
    // The block's top in viewport coordinates.
    int blockTop(const QTextBlock& b) const {
        return static_cast<int>(document()->documentLayout()->blockBoundingRect(b).top()) - verticalScrollBar()->value();
    }
    // A script line's top and bottom in viewport coordinates (clamped to the script).
    std::pair<int, int> lineSpan(int line) const {
        const QTextBlock b = document()->findBlockByNumber(std::clamp(line, 0, std::max(0, document()->blockCount() - 1)));
        const QRectF r = document()->documentLayout()->blockBoundingRect(b);
        const int top = static_cast<int>(r.top()) - verticalScrollBar()->value();
        return {top, top + std::max(1, static_cast<int>(r.height()))};
    }
    // The script line at viewport height `y`, and how far down it `y` is.
    std::pair<int, double> lineAt(int y) const {
        const int pos = document()->documentLayout()->hitTest(QPointF(1, y + verticalScrollBar()->value()), Qt::FuzzyHit);
        const int line = document()->findBlock(std::max(0, pos)).blockNumber();
        const auto [top, bottom] = lineSpan(line);
        return {std::max(0, line), std::clamp(static_cast<double>(y - top) / std::max(1, bottom - top), 0.0, 1.0)};
    }
    // Where slates start: "A slate frame appears above the text, with one or more of the takes covering the scene as
    // lines" (p.576), moved "to avoid blocking words" (p.578). Ours: halfway across the visible text column.
    int slateBase() const {
        const int text = window_.script_.layout.wordWrap ? window_.script_.layout.textWidth
                                                         : static_cast<int>(document()->idealWidth());
        return std::min(text, viewport()->width()) / 2 - horizontalScrollBar()->value();
    }
    // The slates as drawn now.
    QList<SlateGeometry> slateGeometry() const {
        QList<SlateGeometry> out;
        const model::Script& s = window_.script_;
        const ScriptSlateOptions& o = window_.slateOptions_;
        const QFontMetrics fm = fontMetrics();
        const int frameW = o.showFrames ? std::max(24, static_cast<int>(kFrameWidth * o.frameScale)) : 0;
        const int frameH = frameW * 9 / 16;
        const int nameH = fm.height() + 2;
        const int tabW = std::max(22, fm.horizontalAdvance(QStringLiteral("000")) + 6);
        const int tabH = fm.height() + 2;
        const int base = slateBase();
        for (const model::ScriptSlate& slate : s.slates) {
            SlateGeometry g;
            g.slateId = slate.id;
            // "Show only one take for each nonactive slate" (p.578): a slate is active while one of its takes is selected.
            const bool active = std::any_of(slate.takes.cbegin(), slate.takes.cend(),
                                            [&](const model::ScriptTake& t) { return window_.selectedTakes_.contains(t.id); });
            QList<int> shown;
            for (int i = 0; i < slate.takes.size(); ++i)
                if (o.showAllTakes || active || i == slate.active) shown << i;
            const int width = std::max({frameW, static_cast<int>(shown.size()) * tabW, 60});
            // "A slate frame appears above the text" (p.576): the tabs end where the slate's line begins, though not above
            // the script's first line.
            const int documentTop = -verticalScrollBar()->value();
            int top = std::max(documentTop, lineSpan(slate.line).first - (frameH + nameH + tabH));
            if (o.holdSlates && top < 0) {
                // "as you scroll a script ... each slate remains on screen as long as the take lines to which it is linked
                // remain on screen" (p.577).
                int last = 0;
                for (const model::ScriptTake& t : slate.takes) last = std::max(last, lineSpan(t.last).second);
                top = std::max(top, std::min(0, last - (frameH + nameH + tabH)));
            }
            g.box = QRect(base + slate.x, top, width, frameH + nameH);
            if (frameW > 0) g.frame = QRect(g.box.left() + (width - frameW) / 2, top, frameW, frameH);
            g.name = QRect(g.box.left(), top + frameH, width, nameH);
            for (int k = 0; k < shown.size(); ++k) {
                const model::ScriptTake& t = slate.takes[shown[k]];
                SlateGeometry::Take gt;
                gt.id = t.id;
                gt.tab = QRect(g.box.left() + k * tabW, g.box.bottom() + 1, tabW, tabH);
                gt.x = gt.tab.center().x();
                const auto first = lineSpan(t.first);
                const auto last = lineSpan(t.last);
                // The line runs down from the tab when the slate stands over the take's lines, else over its lines.
                gt.top = gt.tab.bottom() >= first.first && gt.tab.bottom() < last.second ? gt.tab.bottom() + 1 : first.first;
                gt.bottom = last.second;
                g.takes << gt;
            }
            out << g;
        }
        return out;
    }
    SlateHit hitAt(const QPoint& p) const {
        SlateHit h;
        const QList<SlateGeometry> geometry = slateGeometry();
        // Slates are drawn last-on-top, so the last one under the point wins.
        for (auto g = geometry.crbegin(); g != geometry.crend(); ++g) {
            for (const SlateGeometry::Take& t : g->takes) {
                if (t.tab.contains(p)) return SlateHit{SlateHit::Tab, g->slateId, t.id, -1, 0.0, 0};
            }
            if (g->box.contains(p)) return SlateHit{SlateHit::Box, g->slateId, {}, -1, 0.0, 0};
        }
        for (auto g = geometry.crbegin(); g != geometry.crend(); ++g) {
            for (const SlateGeometry::Take& t : g->takes) {
                if (std::abs(p.x() - t.x) <= 4 && p.y() >= t.top - kEndGrab && p.y() <= t.bottom + kEndGrab) {
                    h = SlateHit{SlateHit::Line, g->slateId, t.id, -1, 0.0, 0};
                    const auto [line, fraction] = lineAt(p.y());
                    h.line = line;
                    h.fraction = fraction;
                    if (const model::ScriptTake* take = model::takeOf(window_.script_, t.id)) {
                        h.line = std::clamp(line, take->first, take->last);
                        if (line < take->first) h.fraction = 0.0;
                        if (line > take->last) h.fraction = 1.0;
                    }
                    // A script mark's bar is in the middle of its line (p.583).
                    if (const model::ScriptTake* take = model::takeOf(window_.script_, t.id); take && take->marks.contains(h.line)) {
                        const auto [lt, lb] = lineSpan(h.line);
                        if (std::abs(p.y() - (lt + lb) / 2) <= 4) {
                            h.kind = SlateHit::Mark;
                            return h;
                        }
                    }
                    if (std::abs(p.y() - t.top) <= kEndGrab) h.end = -1;
                    else if (std::abs(p.y() - t.bottom) <= kEndGrab) h.end = 1;
                    return h;
                }
            }
        }
        return h;
    }
    void clearFrames() { frames_.clear(); }
    // A slate's height with its tabs, as the options draw it.
    int slateHeight() const {
        const ScriptSlateOptions& o = window_.slateOptions_;
        const int frameW = o.showFrames ? std::max(24, static_cast<int>(kFrameWidth * o.frameScale)) : 0;
        return frameW * 9 / 16 + 2 * (fontMetrics().height() + 2);
    }
    // Room above the first line for slates standing over it (ours): the document's top margin while there are slates.
    void updateTopMargin() {
        const int margin = window_.script_.slates.isEmpty() ? 0 : slateHeight();
        QTextFrameFormat f = document()->rootFrame()->frameFormat();
        if (static_cast<int>(f.topMargin()) == margin) return;
        f.setTopMargin(margin);
        document()->rootFrame()->setFrameFormat(f);
    }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QTextEdit::resizeEvent(e);
        layoutMargins();
    }
    void paintEvent(QPaintEvent* e) override {
        if (rows_) {
            // "Interface Setting: Rows, the script appears with alternating shaded rows" (p.1217).
            QPainter p(viewport());
            QColor shade = palette().color(QPalette::AlternateBase);
            for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
                if (b.blockNumber() % 2 == 0) continue;
                const QRectF r = document()->documentLayout()->blockBoundingRect(b);
                const int top = static_cast<int>(r.top()) - verticalScrollBar()->value();
                if (top > viewport()->height()) break;
                if (top + r.height() < 0) continue;
                p.fillRect(QRect(0, top, viewport()->width(), static_cast<int>(r.height())), shade);
            }
        }
        QTextEdit::paintEvent(e);
        paintSlates();
    }
    void mousePressEvent(QMouseEvent* e) override {
        const QPoint p = e->position().toPoint();
        const SlateHit h = hitAt(p);
        const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);
        const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
        const bool alt = e->modifiers().testFlag(Qt::AltModifier);
        if (e->button() != Qt::LeftButton || (h.kind == SlateHit::None && p.x() < slateBase())) {
            // "As you hear a line of dialog ... click the matching line in the Script window. A script mark appears at that
            // location in the take, and the clip continues to play" (p.584).
            if (e->button() == Qt::LeftButton && window_.isRecording()) window_.markPlayingLine(lineAt(p.y()).first);
            QTextEdit::mousePressEvent(e);
            return;
        }
        drag_ = {};
        drag_.start = p;
        drag_.now = p;
        const model::ScriptSlate* slate = nullptr;
        for (const model::ScriptSlate& s : window_.script_.slates)
            if (s.id == h.slateId) slate = &s;
        switch (h.kind) {
        case SlateHit::Tab:
            if (alt) {
                // "Press the Alt key ... and click the Takes tab" opens the Info window (p.572).
                if (const model::ScriptTake* t = model::takeOf(window_.script_, h.takeId)) emit window_.takeInfoRequested(*t);
                return;
            }
            pick(h.takeId, ctrl || shift);
            break;
        case SlateHit::Box:
            // "Click a slate to select it. Shift+click additional slates to select all the active takes" (p.578).
            if (slate && !slate->takes.isEmpty()) pick(slate->takes[std::clamp(slate->active, 0, static_cast<int>(slate->takes.size()) - 1)].id, shift || ctrl);
            drag_.kind = Drag::Slate;
            drag_.slateId = h.slateId;
            drag_.withTakes = ctrl;
            break;
        case SlateHit::Mark:
            // "Click once on a script mark to select it"; Ctrl+drag moves it (p.586).
            window_.selectMark(h.takeId, h.line);
            if (ctrl) {
                drag_.kind = Drag::Mark;
                drag_.takeId = h.takeId;
                drag_.end = h.line;
            }
            break;
        case SlateHit::Line:
            if (ctrl && h.end != 0) {
                // "Notice the movement icon that appears when you place the pointer at either end of the take" (p.581).
                drag_.kind = Drag::TakeEnd;
                drag_.takeId = h.takeId;
                drag_.end = h.end;
                break;
            }
            pick(h.takeId, ctrl || shift);
            break;
        case SlateHit::None:
            // "Drag a lasso through a region of the script containing slates" (p.578).
            if (!ctrl && !shift) window_.selectTakes({});
            drag_.kind = Drag::Lasso;
            break;
        }
        viewport()->update();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (drag_.kind == Drag::None) {
            const SlateHit h = hitAt(e->position().toPoint());
            viewport()->setCursor(h.kind == SlateHit::Line && h.end != 0 && e->modifiers().testFlag(Qt::ControlModifier)
                                      ? Qt::SizeVerCursor
                                      : (h.kind == SlateHit::None ? Qt::IBeamCursor : Qt::ArrowCursor));
            QTextEdit::mouseMoveEvent(e);
            return;
        }
        drag_.now = e->position().toPoint();
        viewport()->update();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (drag_.kind == Drag::None) {
            QTextEdit::mouseReleaseEvent(e);
            return;
        }
        const Drag d = drag_;
        drag_ = {};
        const QPoint p = e->position().toPoint();
        if (d.kind == Drag::Slate && (p - d.start).manhattanLength() >= 3) {
            // The slate moves by the drag's distance in lines (the slate itself may stand above the first line).
            int line = 0;
            for (const model::ScriptSlate& s : window_.script_.slates)
                if (s.id == d.slateId) line = s.line;
            const auto [top, bottom] = lineSpan(line);
            const int lines = static_cast<int>(std::lround(static_cast<double>(p.y() - d.start.y()) / std::max(1, bottom - top)));
            window_.moveSlate(d.slateId, lines, p.x() - d.start.x(), d.withTakes);
        } else if (d.kind == Drag::Mark) {
            window_.moveScriptMark(d.takeId, d.end, lineAt(p.y()).first);
        } else if (d.kind == Drag::TakeEnd) {
            if (const model::ScriptTake* t = model::takeOf(window_.script_, d.takeId)) {
                const int line = lineAt(p.y()).first;
                window_.setTakeLines(d.takeId, d.end < 0 ? line : t->first, d.end > 0 ? line : t->last);
            }
        } else if (d.kind == Drag::Lasso) {
            const QRect lasso = QRect(d.start, p).normalized();
            QStringList ids = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier) ? window_.selectedTakes_ : QStringList();
            // "All slates and takes within the lasso are selected" (p.578).
            for (const SlateGeometry& g : slateGeometry()) {
                const bool slateIn = lasso.intersects(g.box);
                for (const SlateGeometry::Take& t : g.takes) {
                    if ((slateIn || lasso.intersects(t.tab) || lasso.intersects(QRect(t.x - 2, t.top, 5, t.bottom - t.top)))
                        && !ids.contains(t.id))
                        ids << t.id;
                }
            }
            window_.selectTakes(ids);
        }
        viewport()->update();
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        const SlateHit h = hitAt(e->position().toPoint());
        if (h.kind == SlateHit::None) {
            QTextEdit::mouseDoubleClickEvent(e);
            return;
        }
        drag_ = {};
        if (h.kind == SlateHit::Tab || h.kind == SlateHit::Box) {
            QString id = h.takeId;
            if (id.isEmpty()) {
                for (const model::ScriptSlate& s : window_.script_.slates)
                    if (s.id == h.slateId && !s.takes.isEmpty()) id = s.takes[std::clamp(s.active, 0, static_cast<int>(s.takes.size()) - 1)].id;
            }
            // "Select multiple takes, and then double-click any take you selected" loads them all (p.581).
            const QStringList ids = window_.selectedTakes_.contains(id) ? window_.selectedTakes_ : QStringList{id};
            for (const QString& t : ids)
                if (t != id) window_.loadTake(t);
            if (!id.isEmpty()) window_.loadTake(id);
        } else if (e->modifiers().testFlag(Qt::ControlModifier) && e->modifiers().testFlag(Qt::AltModifier)) {
            // "Press the Ctrl and Alt keys ... Double-click the preferred take within the range of dialog that is marked
            // with script marks. The marked section of the clip is spliced into the sequence" (p.588).
            window_.spliceTake(h.takeId, h.line);
        } else {
            // "Double-click any line in the take to select the take and load it into a monitor" (p.580); "Double-click in
            // the Script window at the intersection of a take and the line of dialog ... the selected line of the dialog
            // is highlighted" (p.583); on a mark, the take is cued there (p.585).
            window_.selectLines(h.line, h.line);
            window_.loadTake(h.takeId, h.line, h.fraction);
        }
    }
    void dragEnterEvent(QDragEnterEvent* e) override {
        if (e->mimeData()->hasFormat(binItemsMime())) {
            e->acceptProposedAction();
            return;
        }
        QTextEdit::dragEnterEvent(e);
    }
    void dragMoveEvent(QDragMoveEvent* e) override {
        if (e->mimeData()->hasFormat(binItemsMime())) {
            e->acceptProposedAction();
            return;
        }
        QTextEdit::dragMoveEvent(e);
    }
    void dropEvent(QDropEvent* e) override {
        if (!e->mimeData()->hasFormat(binItemsMime())) {
            QTextEdit::dropEvent(e);
            return;
        }
        // "Select the portion of the script that is covered by the first clip or clips ... and drag them to the
        // highlighted text" (p.576); "Drag the clip to the slate" adds a take to it (p.580).
        const QStringList rows = QString::fromUtf8(e->mimeData()->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (rows.size() < 2 || !window_.clipResolver_) return;
        QList<model::ScriptClip> clips;
        for (qsizetype i = 1; i < rows.size(); ++i)
            if (auto clip = window_.clipResolver_(rows.first(), rows[i])) clips << *clip;
        const SlateHit h = hitAt(e->position().toPoint());
        if (window_.linkClips(clips, h.kind == SlateHit::Box || h.kind == SlateHit::Tab ? h.slateId : QString())) e->acceptProposedAction();
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Paint && (watched == left_ || watched == right_)) {
            paintMargin(watched == left_);
            return true;
        }
        return QTextEdit::eventFilter(watched, event);
    }

private:
    void paintMargin(bool left) {
        QWidget* w = left ? left_ : right_;
        QPainter p(w);
        p.fillRect(w->rect(), palette().color(QPalette::Window));
        p.setPen(palette().color(QPalette::WindowText));
        p.setFont(font());
        const model::Script& s = window_.script_;
        const int lineHeight = fontMetrics().height();
        for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
            const int top = blockTop(b);
            if (top > w->height()) break;
            if (top + lineHeight < 0) continue;
            const int line = b.blockNumber();
            const QRect cell(4, top, w->width() - 8, lineHeight);
            if (left) {
                // "The scene number appears in the left margin" (p.574).
                if (s.scenes.contains(line)) p.drawText(cell, Qt::AlignLeft | Qt::AlignVCenter, s.scenes.value(line));
                if (lineNumbers_) p.drawText(cell, Qt::AlignRight | Qt::AlignVCenter, QString::number(line + 1));
            } else if (s.pages.contains(line)) {
                // "The page number appears in the right margin next to the first line of the selected region".
                p.drawText(cell, Qt::AlignLeft | Qt::AlignVCenter, s.pages.value(line));
            }
        }
    }

    // Selects a take (the slate shows it from now on); `add` toggles it in the selection (Ctrl+click, p.580).
    void pick(const QString& takeId, bool add) {
        window_.selectedMark_.reset();
        QStringList ids = add ? window_.selectedTakes_ : QStringList();
        if (add && ids.contains(takeId)) ids.removeAll(takeId);
        else ids << takeId;
        model::setActiveTake(window_.script_, takeId);
        window_.selectTakes(ids);
    }
    QImage frameOf(const model::ScriptTake& take, QSize size) {
        if (!window_.frameProvider_ || size.isEmpty()) return {};
        const QString key = QStringLiteral("%1/%2/%3").arg(take.id).arg(take.frame).arg(size.width());
        auto it = frames_.find(key);
        if (it == frames_.end()) it = frames_.insert(key, window_.frameProvider_(take, size));
        return *it;
    }
    // A take's line from `top` to `bottom` at `x`: straight, jagged where it is off-screen dialog (p.581), with its colour
    // ranges beside it (p.582).
    void paintTakeLine(QPainter& p, const model::ScriptTake& take, const SlateGeometry::Take& g, const QColor& color, int width) {
        for (const model::ScriptRange& r : take.colors) {
            const int top = std::max(g.top, lineSpan(r.first).first);
            const int bottom = std::min(g.bottom, lineSpan(r.last).second);
            if (bottom > top) p.fillRect(QRect(g.x - 4, top, 9, bottom - top), QColor(r.color));
        }
        p.setPen(QPen(color, width));
        for (int line = take.first; line <= take.last; ++line) {
            const auto [lt, lb] = lineSpan(line);
            const int top = std::max(g.top, lt);
            const int bottom = std::min(g.bottom, lb);
            if (bottom <= top || bottom < 0 || top > viewport()->height()) continue;
            if (model::isOffscreen(take, line)) {
                // "a jagged line next to the dialog indicate off-screen dialog" (p.581).
                QPolygon zig;
                int side = -3;
                for (int y = top; y <= bottom; y += 4, side = -side) zig << QPoint(g.x + side, y);
                p.drawPolyline(zig);
            } else {
                p.drawLine(g.x, top, g.x, bottom);
            }
        }
        // The beginning and end marks.
        p.drawLine(g.x - 4, g.bottom, g.x + 4, g.bottom);
    }
    // "The line is marked in the Script window with a small horizontal bar" (p.583); a selected mark is drawn larger.
    void paintMarks(QPainter& p, const model::ScriptTake& take, const SlateGeometry::Take& g) {
        for (auto it = take.marks.cbegin(); it != take.marks.cend(); ++it) {
            const auto [top, bottom] = lineSpan(it.key());
            const int y = (top + bottom) / 2;
            if (y < 0 || y > viewport()->height()) continue;
            const bool sel = window_.selectedMark_ && window_.selectedMark_->first == take.id && window_.selectedMark_->second == it.key();
            const int half = sel ? 8 : 6;
            p.fillRect(QRect(g.x - half, y - (sel ? 2 : 1), 2 * half + 1, sel ? 5 : 3), palette().color(QPalette::BrightText));
        }
    }
    void paintSlates() {
        if (window_.script_.slates.isEmpty()) return;
        QPainter p(viewport());
        p.setFont(font());
        const QList<SlateGeometry> geometry = slateGeometry();
        const QColor takeColor(window_.slateOptions_.takeColor);
        const QColor selected(220, 40, 40); // "The outline of the take changes to red" (p.580)
        for (const SlateGeometry& g : geometry) {
            const model::ScriptSlate* slate = nullptr;
            for (const model::ScriptSlate& s : window_.script_.slates)
                if (s.id == g.slateId) slate = &s;
            if (!slate) continue;
            for (const SlateGeometry::Take& t : g.takes) {
                if (const model::ScriptTake* take = model::takeOf(window_.script_, t.id)) {
                    const bool sel = window_.selectedTakes_.contains(t.id);
                    // "The first selected take changes to green in the Script window" while screening (p.584).
                    const bool playing = t.id == window_.recordingTake_;
                    paintTakeLine(p, *take, t, playing ? QColor(40, 190, 70) : (sel ? selected : takeColor), sel || playing ? 3 : 2);
                    paintMarks(p, *take, t);
                }
            }
            if (g.box.bottom() < 0 || g.box.top() > viewport()->height()) continue;
            const int active = std::clamp(slate->active, 0, static_cast<int>(slate->takes.size()) - 1);
            const model::ScriptTake& shown = slate->takes[active];
            p.fillRect(g.box, palette().color(QPalette::Base));
            if (!g.frame.isEmpty()) {
                const QImage img = frameOf(shown, g.frame.size());
                if (img.isNull()) p.fillRect(g.frame, palette().color(QPalette::Dark));
                else p.drawImage(g.frame, img);
            }
            p.setPen(palette().color(QPalette::WindowText));
            p.drawText(g.name.adjusted(3, 0, -3, 0), Qt::AlignLeft | Qt::AlignVCenter,
                       fontMetrics().elidedText(shown.clipName, Qt::ElideRight, g.name.width() - 6));
            p.setPen(palette().color(QPalette::Mid));
            p.drawRect(g.box.adjusted(0, 0, -1, -1));
            for (const SlateGeometry::Take& t : g.takes) {
                const model::ScriptTake* take = model::takeOf(window_.script_, t.id);
                if (!take) continue;
                const bool sel = window_.selectedTakes_.contains(t.id);
                p.fillRect(t.tab, take->id == shown.id ? palette().color(QPalette::Button) : palette().color(QPalette::Base));
                p.setPen(QPen(sel ? selected : palette().color(QPalette::Mid), sel ? 2 : 1));
                p.drawRect(t.tab.adjusted(0, 0, -1, -1));
                p.setPen(palette().color(QPalette::WindowText));
                // "Numbers in the Take column appear in the tabs for each take" (p.580).
                p.drawText(t.tab, Qt::AlignCenter, take->take);
            }
        }
        // The drags: the slate's outline where it goes, the lasso.
        if (drag_.kind == Drag::Slate) {
            for (const SlateGeometry& g : geometry) {
                if (g.slateId != drag_.slateId) continue;
                p.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DashLine));
                p.drawRect(g.box.translated(drag_.now - drag_.start));
            }
        } else if (drag_.kind == Drag::Lasso) {
            p.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DashLine));
            p.drawRect(QRect(drag_.start, drag_.now).normalized());
        } else if (drag_.kind == Drag::TakeEnd) {
            p.setPen(QPen(selected, 1, Qt::DashLine));
            p.drawLine(0, drag_.now.y(), viewport()->width(), drag_.now.y());
        }
    }

    struct Drag {
        enum Kind { None, Slate, TakeEnd, Lasso, Mark } kind = None;
        QPoint start;
        QPoint now;
        QString slateId;
        QString takeId;
        bool withTakes = false;
        int end = 0;
    };

    ScriptWindow& window_;
    QWidget* left_ = nullptr;
    QWidget* right_ = nullptr;
    bool lineNumbers_ = false;
    bool rows_ = true;
    Drag drag_;
    QHash<QString, QImage> frames_; // the slates' frames, by take, frame and width
};

ScriptWindow::ScriptWindow(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("scriptWindow"));
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(2, 2, 2, 2);
    auto* bar = new QHBoxLayout;
    auto tool = [this, bar](const QString& text, const QString& name, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setObjectName(name);
        b->setToolTip(tip);
        bar->addWidget(b);
        return b;
    };
    editButton_ = tool(tr("Edit"), QStringLiteral("editMode"), tr("Edit Mode"));
    editButton_->setCheckable(true);
    connect(editButton_, &QToolButton::toggled, this, &ScriptWindow::setEditMode);
    findButton_ = tool(tr("Find"), QStringLiteral("showFindTools"), tr("Show Find Tools"));
    findButton_->setCheckable(true);
    connect(findButton_, &QToolButton::toggled, this, &ScriptWindow::setFindToolsShown);
    connect(tool(tr("AS"), QStringLiteral("addScene"), tr("Add Scene")), &QToolButton::clicked, this, &ScriptWindow::askScene);
    connect(tool(tr("AP"), QStringLiteral("addPage"), tr("Add Page")), &QToolButton::clicked, this, &ScriptWindow::askPage);
    // "click the Play button at the top of the Script window" (p.581), Set Offscreen (p.581) and Set Color (p.582).
    // Ours: lettered like the other buttons.
    connect(tool(QStringLiteral("\u25B6"), QStringLiteral("playTakes"), tr("Play")), &QToolButton::clicked, this, &ScriptWindow::playSelected);
    // "Click the Record button in the Script window toolbar" (p.583-584), "Click the Find Bin button in the Script window
    // toolbar" (p.586). Ours: a dot and lettered.
    connect(tool(QStringLiteral("\u25CF"), QStringLiteral("recordTakes"), tr("Record")), &QToolButton::clicked, this, &ScriptWindow::recordSelected);
    connect(tool(tr("FB"), QStringLiteral("findBin"), tr("Find Bin")), &QToolButton::clicked, this, &ScriptWindow::findBins);
    offscreenButton_ = tool(tr("OS"), QStringLiteral("setOffscreen"), tr("Set Offscreen"));
    offscreenButton_->setCheckable(true);
    connect(offscreenButton_, &QToolButton::clicked, this, [this] { setOffscreen(); });
    colorButton_ = tool(tr("SC"), QStringLiteral("setColor"), tr("Set Color"));
    colorButton_->setCheckable(true);
    connect(colorButton_, &QToolButton::clicked, this, [this] { setColor(); });
    bar->addSpacing(8);
    bar->addWidget(new QLabel(tr("Scene"), this));
    scenes_ = new QComboBox(this);
    scenes_->setObjectName(QStringLiteral("sceneMenu"));
    bar->addWidget(scenes_);
    bar->addWidget(new QLabel(tr("Page"), this));
    pages_ = new QComboBox(this);
    pages_->setObjectName(QStringLiteral("pageMenu"));
    bar->addWidget(pages_);
    bar->addStretch(1);
    v->addLayout(bar);
    connect(scenes_, &QComboBox::activated, this, [this](int i) { goToScene(scenes_->itemText(i)); });
    connect(pages_, &QComboBox::activated, this, [this](int i) { goToPage(pages_->itemText(i)); });

    findBar_ = new QWidget(this);
    auto* f = new QHBoxLayout(findBar_);
    f->setContentsMargins(0, 0, 0, 0);
    f->addWidget(new QLabel(tr("Find"), findBar_));
    find_ = new QLineEdit(findBar_);
    find_->setObjectName(QStringLiteral("findText"));
    f->addWidget(find_, 1);
    auto* back = new QToolButton(findBar_);
    back->setArrowType(Qt::LeftArrow);
    auto* forward = new QToolButton(findBar_);
    forward->setArrowType(Qt::RightArrow);
    f->addWidget(back);
    f->addWidget(forward);
    f->addWidget(new QLabel(tr("Replace"), findBar_));
    replace_ = new QLineEdit(findBar_);
    replace_->setObjectName(QStringLiteral("replaceText"));
    f->addWidget(replace_, 1);
    auto* all = new QToolButton(findBar_);
    all->setText(tr("Replace All"));
    f->addWidget(all);
    connect(back, &QToolButton::clicked, this, [this] { findNext(true); });
    connect(forward, &QToolButton::clicked, this, [this] { findNext(false); });
    connect(find_, &QLineEdit::returnPressed, this, [this] { findNext(false); });
    connect(all, &QToolButton::clicked, this, &ScriptWindow::replaceAll);
    findBar_->hide();
    v->addWidget(findBar_);

    view_ = new ScriptTextView(*this);
    view_->installEventFilter(this);
    v->addWidget(view_, 1);
    connect(view_, &QTextEdit::textChanged, this, &ScriptWindow::textEdited);
    connect(view_, &QTextEdit::cursorPositionChanged, this, &ScriptWindow::refreshStatus);
    connect(view_, &QTextEdit::selectionChanged, this, &ScriptWindow::expandSelectionToLines);
    connect(view_, &QTextEdit::selectionChanged, this, &ScriptWindow::refreshIndicatorButtons);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("scriptStatus"));
    v->addWidget(status_);
    setEnabled(false);
}

ScriptWindow::~ScriptWindow() = default;

void ScriptWindow::open(const QString& binId, const QString& itemId, const QString& name, const model::Script& script) {
    binId_ = binId;
    itemId_ = itemId;
    name_ = name;
    script_ = script;
    selectedTakes_.clear();
    selectedMark_.reset();
    markTake_.clear();
    recordTakes_.clear();
    recordingTake_.clear();
    view_->clearFrames();
    setEditMode(false);
    rebuild();
    setEnabled(true);
    modified_ = true; // so that setModified emits
    setModified(false);
}

void ScriptWindow::close() {
    binId_.clear();
    itemId_.clear();
    name_.clear();
    script_ = {};
    selectedTakes_.clear();
    view_->clearFrames();
    rebuild();
    setEnabled(false);
    setModified(false);
}

void ScriptWindow::markSaved() { setModified(false); }

QString ScriptWindow::title() const {
    if (!isOpen()) return tr("Script");
    return name_ + (modified_ ? QStringLiteral("*") : QString());
}

void ScriptWindow::setModified(bool on) {
    if (modified_ == on) return;
    modified_ = on;
    emit modifiedChanged(on);
    emit titleChanged(title());
}

// The document from the script: one block per line, the layout's font, width and wrapping.
void ScriptWindow::rebuild() {
    updating_ = true;
    QFont font = view_->font();
    if (!script_.layout.font.isEmpty()) font.setFamily(script_.layout.font);
    font.setPointSize(script_.layout.fontSize);
    view_->setFont(font);
    view_->document()->setDefaultFont(font);
    view_->setPlainText(script_.lines.join(QLatin1Char('\n')));
    if (script_.layout.wordWrap) {
        view_->setLineWrapMode(QTextEdit::FixedPixelWidth);
        view_->setLineWrapColumnOrWidth(script_.layout.textWidth);
    } else {
        // "If Word Wrap is not selected, lines will increase in length and not wrap" (p.572).
        view_->setLineWrapMode(QTextEdit::NoWrap);
    }
    view_->document()->clearUndoRedoStacks();
    view_->updateTopMargin();
    view_->layoutMargins();
    updating_ = false;
    refreshMenus();
    refreshStatus();
}

void ScriptWindow::setDisplay(bool lineNumbers, bool shadedRows) {
    view_->setLineNumbers(lineNumbers);
    view_->setShadedRows(shadedRows);
}

void ScriptWindow::setTextLayout(const model::ScriptLayout& layout) {
    if (script_.layout == layout) return;
    const auto sel = selectedLines();
    script_.layout = layout;
    rebuild();
    if (sel) selectLines(sel->first, sel->second);
    setModified(true);
}

// "click the Edit Mode button in the Script Window. The Script Window outlines in purple" (p.573).
void ScriptWindow::setEditMode(bool on) {
    editMode_ = on;
    {
        const QSignalBlocker block(editButton_);
        editButton_->setChecked(on);
    }
    view_->setReadOnly(!on);
    // Clips are dropped on the script in and out of Edit Mode (a read-only text edit takes none by itself).
    view_->setAcceptDrops(true);
    view_->viewport()->setAcceptDrops(true);
    view_->setTextInteractionFlags(on ? Qt::TextEditorInteraction : (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard));
    view_->setStyleSheet(on ? QStringLiteral("QTextEdit#scriptText { border: 2px solid #8a3cc8; }") : QString());
}

bool ScriptWindow::findToolsShown() const { return findBar_->isVisibleTo(this); }

void ScriptWindow::setFindToolsShown(bool on) {
    const QSignalBlocker block(findButton_);
    findButton_->setChecked(on);
    findBar_->setVisible(on);
}

// In Edit Mode, the lines as typed: the changed span (between the common first and last lines) replaces the old one, so
// that the scene and page numbers stay with their lines.
void ScriptWindow::textEdited() {
    if (updating_ || !isOpen()) return;
    const QStringList now = view_->toPlainText().split(QLatin1Char('\n'));
    const QStringList& before = script_.lines;
    if (now == before) return;
    qsizetype head = 0;
    while (head < now.size() && head < before.size() && now[head] == before[head]) ++head;
    qsizetype tail = 0;
    while (tail < now.size() - head && tail < before.size() - head && now[now.size() - 1 - tail] == before[before.size() - 1 - tail]) ++tail;
    model::replaceLines(script_, static_cast<int>(head), static_cast<int>(before.size() - tail),
                        now.mid(head, now.size() - tail - head));
    refreshMenus();
    refreshStatus();
    setModified(true);
}

// Out of Edit Mode a selection covers whole lines ("Select the line of the script", p.574).
void ScriptWindow::expandSelectionToLines() {
    if (editMode_ || updating_) return;
    QTextCursor c = view_->textCursor();
    if (!c.hasSelection()) return;
    QTextDocument* d = view_->document();
    const QTextBlock first = d->findBlock(c.selectionStart());
    const QTextBlock last = d->findBlock(std::max(c.selectionStart(), c.selectionEnd() - 1));
    const int start = first.position();
    const int end = last.position() + last.length() - 1;
    if (c.selectionStart() == start && c.selectionEnd() == end) return;
    updating_ = true;
    const bool forward = c.position() >= c.anchor();
    c.setPosition(forward ? start : end);
    c.setPosition(forward ? end : start, QTextCursor::KeepAnchor);
    view_->setTextCursor(c);
    updating_ = false;
}

std::optional<std::pair<int, int>> ScriptWindow::selectedLines() const {
    if (!isOpen() || script_.lines.isEmpty()) return std::nullopt;
    const QTextCursor c = view_->textCursor();
    QTextDocument* d = view_->document();
    const int first = d->findBlock(c.selectionStart()).blockNumber();
    const int last = c.hasSelection() ? d->findBlock(std::max(c.selectionStart(), c.selectionEnd() - 1)).blockNumber() : first;
    return std::pair{first, last};
}

void ScriptWindow::selectLines(int first, int last) {
    QTextDocument* d = view_->document();
    const QTextBlock a = d->findBlockByNumber(std::clamp(first, 0, d->blockCount() - 1));
    const QTextBlock b = d->findBlockByNumber(std::clamp(last, first, d->blockCount() - 1));
    QTextCursor c(d);
    c.setPosition(a.position());
    c.setPosition(b.position() + b.length() - 1, QTextCursor::KeepAnchor);
    updating_ = true;
    view_->setTextCursor(c);
    view_->ensureCursorVisible();
    updating_ = false;
    refreshStatus();
}

bool ScriptWindow::addScene(const QString& number) {
    const auto sel = selectedLines();
    if (!sel) return false;
    model::setScene(script_, sel->first, number);
    refreshMenus();
    refreshStatus();
    view_->viewport()->update();
    setModified(true);
    return true;
}

bool ScriptWindow::addPage(const QString& number) {
    const auto sel = selectedLines();
    if (!sel) return false;
    model::setPage(script_, sel->first, number);
    refreshMenus();
    refreshStatus();
    view_->viewport()->update();
    setModified(true);
    return true;
}

int ScriptWindow::deleteNumbers(bool scenes, bool pages) {
    const auto sel = selectedLines();
    if (!sel) return 0;
    int n = 0;
    if (scenes) n += model::deleteScenes(script_, sel->first, sel->second);
    if (pages) n += model::deletePages(script_, sel->first, sel->second);
    if (n > 0) {
        refreshMenus();
        refreshStatus();
        view_->update();
        setModified(true);
    }
    return n;
}

// "A dialog box opens. Type the number for the scene or page, and click OK" (p.574); changing one is the same.
void ScriptWindow::askScene() {
    const auto sel = selectedLines();
    if (!sel) return;
    const QString current = script_.scenes.value(sel->first);
    std::optional<QString> number;
    if (askNumber_) {
        number = askNumber_(tr("Add Scene"), current);
    } else {
        bool ok = false;
        const QString text = QInputDialog::getText(this, tr("Add Scene"), tr("Scene:"), QLineEdit::Normal, current, &ok);
        if (ok) number = text;
    }
    if (number) addScene(*number);
}

void ScriptWindow::askPage() {
    const auto sel = selectedLines();
    if (!sel) return;
    const QString current = script_.pages.value(sel->first);
    std::optional<QString> number;
    if (askNumber_) {
        number = askNumber_(tr("Add Page"), current);
    } else {
        bool ok = false;
        const QString text = QInputDialog::getText(this, tr("Add Page"), tr("Page:"), QLineEdit::Normal, current, &ok);
        if (ok) number = text;
    }
    if (number) addPage(*number);
}

// "Press the Delete key. The Delete dialog box opens. Select the options for Delete scene(s) or Delete page break(s) as
// appropriate, and click OK" (p.574).
void ScriptWindow::askDelete() {
    const auto sel = selectedLines();
    if (!sel) return;
    bool hasScenes = false;
    bool hasPages = false;
    for (int l = sel->first; l <= sel->second; ++l) {
        hasScenes = hasScenes || script_.scenes.contains(l);
        hasPages = hasPages || script_.pages.contains(l);
    }
    const int takes = static_cast<int>(selectedTakes_.size());
    // "Click once on a script mark to select it. You can select multiple script marks for removal by highlighting an entire
    // region of text and selecting the takes containing the script marks" (p.586).
    const int marks = selectedMark_ ? 1 : model::countScriptMarks(script_, selectedTakes_, sel->first, sel->second);
    if (!hasScenes && !hasPages && takes == 0 && marks == 0) return;
    std::optional<ScriptDeleteChoice> choice;
    if (askDelete_) {
        choice = askDelete_(hasScenes, hasPages, takes, marks);
    } else {
        QDialog d(this);
        d.setWindowTitle(tr("Delete"));
        auto* v = new QVBoxLayout(&d);
        auto* scenes = new QCheckBox(tr("Delete scene(s)"), &d);
        auto* pages = new QCheckBox(tr("Delete page break(s)"), &d);
        scenes->setEnabled(hasScenes);
        scenes->setChecked(hasScenes);
        pages->setEnabled(hasPages);
        pages->setChecked(hasPages);
        // "Select Delete Takes, and click OK" (p.579-580).
        auto* takeBox = new QCheckBox(tr("Delete Takes"), &d);
        takeBox->setEnabled(takes > 0);
        takeBox->setChecked(takes > 0 && !hasScenes && !hasPages);
        // "Select Delete 1 mark(s), and click OK" (p.586).
        auto* markBox = new QCheckBox(tr("Delete %1 mark(s)").arg(marks), &d);
        markBox->setEnabled(marks > 0);
        markBox->setChecked(marks > 0 && selectedMark_.has_value());
        if (markBox->isChecked()) takeBox->setChecked(false);
        v->addWidget(scenes);
        v->addWidget(pages);
        v->addWidget(takeBox);
        v->addWidget(markBox);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        v->addWidget(buttons);
        if (d.exec() == QDialog::Accepted)
            choice = ScriptDeleteChoice{scenes->isChecked(), pages->isChecked(), takeBox->isChecked(), markBox->isChecked()};
    }
    if (!choice) return;
    deleteNumbers(choice->scenes, choice->pages);
    if (choice->marks) {
        // "When you remove a script mark, you do not delete the marked portion of the take, only the sync point" (p.586).
        const int n = selectedMark_ ? model::deleteScriptMarks(script_, {selectedMark_->first}, selectedMark_->second, selectedMark_->second)
                                    : model::deleteScriptMarks(script_, selectedTakes_, sel->first, sel->second);
        selectedMark_.reset();
        if (n > 0) scriptChanged();
    }
    if (choice->takes && model::deleteTakes(script_, selectedTakes_) > 0) {
        // "When you delete slates and takes from the Script window, the captured source clips remain in the source
        // bins" (p.579).
        selectedTakes_.clear();
        scriptChanged();
    }
}

bool ScriptWindow::goToScene(const QString& number) {
    for (auto it = script_.scenes.cbegin(); it != script_.scenes.cend(); ++it) {
        if (it.value() == number) {
            // "The Script window scrolls to the selected page or scene, and the text is highlighted" (p.575).
            const auto next = std::next(it);
            selectLines(it.key(), next == script_.scenes.cend() ? it.key() : std::max(it.key(), next.key() - 1));
            return true;
        }
    }
    return false;
}

bool ScriptWindow::goToPage(const QString& number) {
    for (auto it = script_.pages.cbegin(); it != script_.pages.cend(); ++it) {
        if (it.value() == number) {
            const auto next = std::next(it);
            selectLines(it.key(), next == script_.pages.cend() ? it.key() : std::max(it.key(), next.key() - 1));
            return true;
        }
    }
    return false;
}

void ScriptWindow::setFindText(const QString& find, const QString& replace) {
    find_->setText(find);
    replace_->setText(replace);
}

bool ScriptWindow::findNext(bool backward) {
    const QTextCursor c = view_->textCursor();
    QTextDocument* d = view_->document();
    const int pos = backward ? c.selectionStart() : c.selectionEnd();
    const QTextBlock b = d->findBlock(pos);
    const auto match = model::findInScript(script_, find_->text(), b.blockNumber(), pos - b.position(), backward);
    if (!match) return false;
    const QTextBlock at = d->findBlockByNumber(match->line);
    QTextCursor m(d);
    m.setPosition(at.position() + match->column);
    m.setPosition(at.position() + match->column + match->length, QTextCursor::KeepAnchor);
    updating_ = true;
    view_->setTextCursor(m);
    view_->ensureCursorVisible();
    updating_ = false;
    refreshStatus();
    return true;
}

// "You can also choose to replace all instances of the text" (p.576): one change the editor can undo.
int ScriptWindow::replaceAll() {
    model::Script changed = script_;
    const int n = model::replaceInScript(changed, find_->text(), replace_->text());
    if (n == 0) return 0;
    QTextCursor c(view_->document());
    c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(changed.lines.join(QLatin1Char('\n')));
    c.endEditBlock();
    return n;
}

bool ScriptWindow::editLine(int line, const QString& text) {
    if (!editMode_ || line < 0 || line >= script_.lines.size()) return false;
    const QTextBlock b = view_->document()->findBlockByNumber(line);
    QTextCursor c(b);
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    c.insertText(text);
    return true;
}

void ScriptWindow::refreshMenus() {
    scenes_->clear();
    pages_->clear();
    for (const QString& s : script_.scenes) scenes_->addItem(s);
    for (const QString& p : script_.pages) pages_->addItem(p);
}

// "Scene and page numbers both appear in the status bar at the bottom of the Script window and reflect your current
// position within the script" (p.574).
QString ScriptWindow::statusText() const {
    if (!isOpen()) return {};
    const int line = view_->textCursor().blockNumber();
    QStringList parts;
    if (const QString s = script_.sceneAt(line); !s.isEmpty()) parts << tr("Scene %1").arg(s);
    if (const QString p = script_.pageAt(line); !p.isEmpty()) parts << tr("Page %1").arg(p);
    parts << tr("Line %1").arg(line + 1);
    return parts.join(QStringLiteral("   "));
}

void ScriptWindow::refreshStatus() {
    status_->setText(statusText());
    const QString s = script_.sceneAt(view_->textCursor().blockNumber());
    const QString p = script_.pageAt(view_->textCursor().blockNumber());
    const QSignalBlocker a(scenes_);
    const QSignalBlocker b(pages_);
    scenes_->setCurrentIndex(scenes_->findText(s));
    pages_->setCurrentIndex(pages_->findText(p));
}

void ScriptWindow::scriptChanged() {
    view_->updateTopMargin();
    view_->clearFrames();
    view_->viewport()->update();
    refreshIndicatorButtons();
    setModified(true);
}

void ScriptWindow::setSlateOptions(const ScriptSlateOptions& options) {
    slateOptions_ = options;
    view_->updateTopMargin();
    view_->clearFrames();
    view_->viewport()->update();
}

bool ScriptWindow::linkClips(const QList<model::ScriptClip>& clips, const QString& slateId) {
    const auto sel = selectedLines();
    if (!sel || clips.isEmpty()) return false;
    QStringList added;
    const bool toSlate = std::any_of(script_.slates.cbegin(), script_.slates.cend(),
                                     [&](const model::ScriptSlate& s) { return s.id == slateId; });
    if (toSlate) {
        for (const model::ScriptClip& clip : clips) {
            const QString id = model::addTake(script_, slateId, sel->first, sel->second, clip);
            if (!id.isEmpty()) added << id;
        }
    } else {
        // Ours: a new slate stands right of the slates already over its lines.
        const int top = view_->lineSpan(sel->first).first;
        const int bottom = view_->lineSpan(sel->second).second;
        int x = 0;
        for (const SlateGeometry& g : view_->slateGeometry()) {
            int reach = g.box.bottom();
            for (const SlateGeometry::Take& t : g.takes) reach = std::max(reach, t.bottom);
            if (g.box.top() < bottom && reach > top) x = std::max(x, g.box.right() + 1 + kSlateGap - view_->slateBase());
        }
        const QString id = model::addSlate(script_, sel->first, sel->second, clips);
        if (!id.isEmpty()) script_.slates.last().x = x;
        for (const model::ScriptSlate& s : script_.slates)
            if (s.id == id)
                for (const model::ScriptTake& t : s.takes) added << t.id;
    }
    if (added.isEmpty()) return false;
    selectedTakes_ = added;
    scriptChanged();
    return true;
}

void ScriptWindow::selectTakes(const QStringList& takeIds) {
    QStringList ids;
    for (const QString& id : takeIds)
        if (model::takeOf(script_, id) && !ids.contains(id)) ids << id;
    selectedTakes_ = ids;
    view_->viewport()->update();
    refreshIndicatorButtons();
}

void ScriptWindow::selectSlate(const QString& slateId) {
    QStringList ids = selectedTakes_;
    for (const model::ScriptSlate& s : script_.slates)
        if (s.id == slateId)
            for (const model::ScriptTake& t : s.takes)
                if (!ids.contains(t.id)) ids << t.id;
    selectTakes(ids);
}

void ScriptWindow::moveSlate(const QString& slateId, int lines, int x, bool withTakes) {
    const model::Script before = script_;
    model::moveSlate(script_, slateId, lines, x, withTakes);
    if (script_ != before) scriptChanged();
}

void ScriptWindow::setTakeLines(const QString& takeId, int first, int last) {
    const model::Script before = script_;
    model::setTakeLines(script_, takeId, first, last);
    if (script_ != before) scriptChanged();
}

void ScriptWindow::stepFrames(std::int64_t frames) {
    if (selectedTakes_.isEmpty()) return;
    const model::Script before = script_;
    model::stepTakeFrames(script_, selectedTakes_, frames);
    if (script_ != before) scriptChanged();
}

bool ScriptWindow::setOffscreen() {
    const auto sel = selectedLines();
    if (!sel || selectedTakes_.isEmpty()) {
        refreshIndicatorButtons();
        return false;
    }
    const bool on = model::toggleOffscreen(script_, selectedTakes_, sel->first, sel->second);
    scriptChanged();
    return on;
}

bool ScriptWindow::setColor() {
    const auto sel = selectedLines();
    if (!sel || selectedTakes_.isEmpty()) {
        refreshIndicatorButtons();
        return false;
    }
    const bool on = model::toggleColor(script_, selectedTakes_, sel->first, sel->second, indicatorColor_);
    scriptChanged();
    return on;
}

// "The first take in the selected region determines the color indicator status that displays in the Set Color button"
// (p.582); the Set Offscreen button the same (ours).
void ScriptWindow::refreshIndicatorButtons() {
    if (!offscreenButton_ || !colorButton_) return;
    const auto sel = selectedLines();
    const model::ScriptTake* first = selectedTakes_.isEmpty() ? nullptr : model::takeOf(script_, selectedTakes_.first());
    const QSignalBlocker a(offscreenButton_);
    const QSignalBlocker b(colorButton_);
    offscreenButton_->setChecked(first && sel && model::isOffscreen(*first, sel->first));
    colorButton_->setChecked(first && sel && !model::colorAt(*first, sel->first).isEmpty());
    colorButton_->setStyleSheet(QStringLiteral("QToolButton#setColor { border-bottom: 3px solid %1; }").arg(indicatorColor_));
}

void ScriptWindow::loadTake(const QString& takeId, int line, double fraction) {
    const model::ScriptTake* take = model::takeOf(script_, takeId);
    if (!take) return;
    model::setActiveTake(script_, takeId);
    markTake_ = takeId; // Add Script Mark marks the take last loaded (p.583)
    if (!selectedTakes_.contains(takeId)) selectTakes({takeId});
    view_->viewport()->update();
    emit takeLoadRequested(*take, line, fraction);
}

void ScriptWindow::playSelected() {
    const QList<model::ScriptTake> takes = takesOverSelection();
    if (!takes.isEmpty()) emit takesPlayRequested(takes);
}

// The selected takes; without any, "all the coverage for a range of lines": the takes over the selected lines (p.585).
QList<model::ScriptTake> ScriptWindow::takesOverSelection() const {
    QList<model::ScriptTake> takes;
    for (const QString& id : selectedTakes_)
        if (const model::ScriptTake* t = model::takeOf(script_, id)) takes << *t;
    if (!takes.isEmpty()) return takes;
    const auto sel = selectedLines();
    if (!sel) return takes;
    for (const model::ScriptSlate& s : script_.slates)
        for (const model::ScriptTake& t : s.takes)
            if (t.first <= sel->second && t.last >= sel->first) takes << t;
    return takes;
}

bool ScriptWindow::addScriptMark(std::int64_t frame) {
    const auto sel = selectedLines();
    const model::ScriptTake* take = model::takeOf(script_, markTake_);
    if (!sel || !take) return false;
    if (!model::addScriptMark(script_, markTake_, sel->first, frame)) return false;
    scriptChanged();
    emit scriptMarkAdded(*model::takeOf(script_, markTake_), sel->first);
    return true;
}

void ScriptWindow::selectMark(const QString& takeId, int line) {
    const model::ScriptTake* t = model::takeOf(script_, takeId);
    if (t && t->marks.contains(line)) selectedMark_ = std::pair{takeId, line};
    else selectedMark_.reset();
    view_->viewport()->update();
}

void ScriptWindow::moveScriptMark(const QString& takeId, int from, int to) {
    if (!model::moveScriptMark(script_, takeId, from, to)) return;
    if (selectedMark_ && selectedMark_->first == takeId && selectedMark_->second == from) selectedMark_->second =
        model::takeOf(script_, takeId)->marks.contains(to) ? to : from;
    scriptChanged();
}

void ScriptWindow::recordSelected() {
    const QList<model::ScriptTake> takes = takesOverSelection();
    if (takes.isEmpty()) return;
    recordTakes_.clear();
    for (const model::ScriptTake& t : takes) recordTakes_ << t.id;
    recordingTake_ = recordTakes_.first();
    view_->viewport()->update();
    emit recordRequested(takes);
}

void ScriptWindow::setRecordingTake(const QString& takeId) {
    if (takeId.isEmpty()) recordTakes_.clear();
    if (recordingTake_ == takeId) return;
    recordingTake_ = takeId;
    view_->viewport()->update();
}

bool ScriptWindow::markPlayingLine(int line) {
    if (!isRecording() || recordingTake_.isEmpty() || !sourcePosition_) return false;
    const model::ScriptTake* take = model::takeOf(script_, recordingTake_);
    const auto at = sourcePosition_();
    if (!take || !at || at->first != take->clipId) return false;
    // "Click a line in the script before or after the range of the existing take line to add the mark and extend the take
    // line to include the new line" (p.584).
    if (!model::addScriptMark(script_, recordingTake_, line, at->second)) return false;
    scriptChanged();
    return true;
}

void ScriptWindow::showTakeLines(const QString& takeId, int first, int last) {
    if (!model::takeOf(script_, takeId)) return;
    // "The Script window scrolls to and highlights the portion of script that most closely matches the clip location".
    selectLines(first, last);
    selectTakes({takeId});
}

void ScriptWindow::findBins() {
    const QList<model::ScriptTake> takes = takesOverSelection();
    if (!takes.isEmpty()) emit findBinRequested(takes);
}

void ScriptWindow::spliceTake(const QString& takeId, int line) {
    const model::ScriptTake* take = model::takeOf(script_, takeId);
    if (!take) return;
    auto sel = selectedLines();
    if (!sel || line < sel->first || line > sel->second) sel = std::pair{line, line};
    emit spliceRequested(*take, sel->first, sel->second);
}

QRect ScriptWindow::slateRect(const QString& slateId) const {
    for (const SlateGeometry& g : view_->slateGeometry())
        if (g.slateId == slateId) return g.box;
    return {};
}

QPoint ScriptWindow::takeLinePoint(const QString& takeId, int line) const {
    for (const SlateGeometry& g : view_->slateGeometry())
        for (const SlateGeometry::Take& t : g.takes)
            if (t.id == takeId) {
                const auto [top, bottom] = view_->lineSpan(line);
                return {t.x, (top + bottom) / 2};
            }
    return {};
}

QRect ScriptWindow::takeTabRect(const QString& takeId) const {
    for (const SlateGeometry& g : view_->slateGeometry())
        for (const SlateGeometry::Take& t : g.takes)
            if (t.id == takeId) return t.tab;
    return {};
}

bool ScriptWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view_ && event->type() == QEvent::KeyPress && !editMode_) {
        const auto* k = static_cast<QKeyEvent*>(event);
        if (k->key() == Qt::Key_Delete || k->key() == Qt::Key_Backspace) {
            askDelete();
            return true;
        }
        // "Press the appropriate arrow keys or step keys on the keyboard to advance the footage displayed in the slate"
        // (p.581): Left and Right a frame, with Shift ten (ours, as the step keys elsewhere).
        if ((k->key() == Qt::Key_Left || k->key() == Qt::Key_Right) && !selectedTakes_.isEmpty()) {
            const int step = k->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
            stepFrames(k->key() == Qt::Key_Left ? -step : step);
            return true;
        }
        // "Press the Up Arrow or Down Arrow key to move your line selection up or down by one line" (p.571).
        if ((k->key() == Qt::Key_Up || k->key() == Qt::Key_Down) && k->modifiers() == Qt::NoModifier) {
            const auto sel = selectedLines();
            if (sel) {
                const int line = std::clamp(sel->first + (k->key() == Qt::Key_Up ? -1 : 1), 0, static_cast<int>(script_.lines.size()) - 1);
                selectLines(line, line);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace lumenis::ui
