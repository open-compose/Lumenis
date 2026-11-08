// The Script window (lumenis/ui/script_window.h; Editing Guide p.570-576).
#include "lumenis/ui/script_window.h"

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
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lumenis::ui {

namespace {
constexpr int kRightMargin = 60; // the page numbers' column (ours)
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

    ScriptWindow& window_;
    QWidget* left_ = nullptr;
    QWidget* right_ = nullptr;
    bool lineNumbers_ = false;
    bool rows_ = true;
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
    if (!hasScenes && !hasPages) return;
    std::optional<std::pair<bool, bool>> choice;
    if (askDelete_) {
        choice = askDelete_(hasScenes, hasPages);
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
        v->addWidget(scenes);
        v->addWidget(pages);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        v->addWidget(buttons);
        if (d.exec() == QDialog::Accepted) choice = std::pair{scenes->isChecked(), pages->isChecked()};
    }
    if (choice) deleteNumbers(choice->first, choice->second);
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

bool ScriptWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view_ && event->type() == QEvent::KeyPress && !editMode_) {
        const auto* k = static_cast<QKeyEvent*>(event);
        if (k->key() == Qt::Key_Delete || k->key() == Qt::Key_Backspace) {
            askDelete();
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
