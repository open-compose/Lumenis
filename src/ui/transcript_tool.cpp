// The Transcript Tool's window (lumenis/ui/transcript_tool.h).
#include "lumenis/ui/transcript_tool.h"

#include "lumenis/ui/theme.h"

#include <QAction>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lumenis::ui {

namespace {

// A word as search compares it: lower case, letters and digits only ("PhraseFind AI is case insensitive and ignores
// punctuation", p.441; the Transcript Tool's search the same, ours).
QString bare(const QString& word) {
    QString out;
    for (const QChar c : word.toLower()) {
        if (c.isLetterOrNumber()) out += c;
    }
    return out;
}

} // namespace

TranscriptTool::TranscriptTool(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("transcriptTool"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    // "If a clip is not transcribed, you may get a message in the viewer that says Clip not transcribed with a button to
    // Transcribe" (p.443).
    banner_ = new QWidget(this);
    auto* bannerLayout = new QHBoxLayout(banner_);
    bannerLayout->setContentsMargins(0, 0, 0, 0);
    bannerText_ = new QLabel(banner_);
    bannerText_->setObjectName(QStringLiteral("notTranscribed"));
    bannerText_->setWordWrap(true);
    transcribe_ = new QPushButton(tr("Transcribe"), banner_);
    transcribe_->setObjectName(QStringLiteral("transcribe"));
    bannerLayout->addWidget(bannerText_, 1);
    bannerLayout->addWidget(transcribe_);
    connect(transcribe_, &QPushButton::clicked, this, [this] { emit transcribeRequested(shown_.notTranscribedMobs); });
    layout->addWidget(banner_);
    view_ = new QTextEdit(this);
    view_->setObjectName(QStringLiteral("transcriptText"));
    view_->setReadOnly(true);
    view_->setAcceptRichText(false);
    view_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    view_->viewport()->installEventFilter(this);
    view_->installEventFilter(this);
    connect(view_, &QTextEdit::selectionChanged, this, &TranscriptTool::selectionChanged);
    layout->addWidget(view_, 1);
    // The bottom row: the fast menu and the gang button, search, the Source/Record and track buttons (p.443-444, p.449).
    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    fast_ = new QToolButton(this);
    fast_->setObjectName(QStringLiteral("transcriptFastMenu"));
    fast_->setIcon(glyphIcon(GlyphIcon::FastMenu));
    fast_->setToolTip(tr("Fast Menu"));
    connect(fast_, &QToolButton::clicked, this, [this] {
        QMenu* menu = fastMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(fast_->mapToGlobal(QPoint(0, fast_->height())));
    });
    gang_ = new QToolButton(this);
    gang_->setObjectName(QStringLiteral("transcriptGang"));
    gang_->setText(tr("Gang"));
    gang_->setToolTip(tr("Gang to the monitor"));
    gang_->setCheckable(true);
    gang_->setChecked(true);
    connect(gang_, &QToolButton::toggled, this, &TranscriptTool::setGanged);
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("transcriptSearch"));
    search_->setPlaceholderText(tr("Search"));
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) { search(text); });
    connect(search_, &QLineEdit::returnPressed, this, [this] { next(); });
    previous_ = new QToolButton(this);
    previous_->setObjectName(QStringLiteral("transcriptPrevious"));
    previous_->setArrowType(Qt::UpArrow);
    previous_->setToolTip(tr("Previous"));
    connect(previous_, &QToolButton::clicked, this, [this] { next(true); });
    nextButton_ = new QToolButton(this);
    nextButton_->setObjectName(QStringLiteral("transcriptNext"));
    nextButton_->setArrowType(Qt::DownArrow);
    nextButton_->setToolTip(tr("Next"));
    connect(nextButton_, &QToolButton::clicked, this, [this] { next(); });
    found_ = new QLabel(this);
    side_ = new QToolButton(this);
    side_->setObjectName(QStringLiteral("transcriptSide"));
    side_->setCheckable(true);
    side_->setChecked(true);
    side_->setToolTip(tr("Show the Source or Record side"));
    connect(side_, &QToolButton::clicked, this, [this](bool on) {
        setSourceSide(on);
        emit sideChosen(on);
    });
    tracks_ = new QToolButton(this);
    tracks_->setObjectName(QStringLiteral("transcriptTracks"));
    tracks_->setText(tr("Tracks"));
    tracks_->setToolTip(tr("The audio tracks whose transcripts are shown"));
    tracks_->setPopupMode(QToolButton::InstantPopup);
    tracks_->setMenu(new QMenu(tracks_));
    tracks_->hide();
    row->addWidget(fast_);
    row->addWidget(gang_);
    row->addWidget(search_, 1);
    row->addWidget(previous_);
    row->addWidget(nextButton_);
    row->addWidget(found_);
    row->addWidget(tracks_);
    row->addWidget(side_);
    layout->addLayout(row);
    refreshSideButton();
    clear(tr("No clip or sequence"));
}

void TranscriptTool::clear(const QString& message) {
    shown_ = {};
    message_ = message;
    rebuild();
}

void TranscriptTool::setTranscript(const model::ShownTranscript& shown, TimecodeText timecode) {
    shown_ = shown;
    timecode_ = std::move(timecode);
    message_.clear();
    rebuild();
}

// One block per line: "[timecode] Speaker: words", the words' places noted for clicks and highlights.
void TranscriptTool::rebuild() {
    updating_ = true;
    spans_.clear();
    matches_.clear();
    playhead_.reset();
    QTextDocument* doc = view_->document();
    doc->clear();
    QTextCursor cursor(doc);
    if (shown_.lines.isEmpty()) {
        cursor.insertText(message_.isEmpty() && shown_.notTranscribed.isEmpty() ? tr("No transcript") : message_);
    }
    QTextCharFormat plain;
    QTextCharFormat dim;
    dim.setForeground(palette().color(QPalette::PlaceholderText));
    QTextCharFormat speaker;
    speaker.setFontWeight(QFont::Bold);
    for (int l = 0; l < shown_.lines.size(); ++l) {
        const model::ShownLine& line = shown_.lines[l];
        if (l > 0) cursor.insertBlock();
        if (showTimecode_ && timecode_) cursor.insertText(timecode_(line.frame) + QStringLiteral("  "), dim);
        if (showSpeakers_) cursor.insertText(line.speaker + QStringLiteral(": "), speaker);
        for (int w = 0; w < line.words.size(); ++w) {
            if (w > 0) cursor.insertText(QStringLiteral(" "), plain);
            WordSpan span;
            span.line = l;
            span.word = w;
            span.start = cursor.position();
            cursor.insertText(line.words[w].text, plain);
            span.end = cursor.position();
            spans_ << span;
        }
    }
    updating_ = false;
    refreshBanner();
    // The track button: "This button appears when a sequence is loaded" (p.449).
    QMenu* menu = tracks_->menu();
    menu->clear();
    for (const QString& t : shown_.availableTracks) {
        QAction* a = menu->addAction(t);
        a->setCheckable(true);
        a->setChecked(shown_.tracks.contains(t));
        connect(a, &QAction::toggled, this, [this] {
            QStringList chosen;
            for (QAction* act : tracks_->menu()->actions()) {
                if (act->isChecked()) chosen << act->text();
            }
            emit tracksChosen(chosen);
        });
    }
    tracks_->setVisible(!shown_.availableTracks.isEmpty());
    if (!search_->text().isEmpty()) search(search_->text());
    refreshHighlights();
}

void TranscriptTool::refreshBanner() {
    const bool show = showNotTranscribed_ && !shown_.notTranscribed.isEmpty();
    banner_->setVisible(show);
    if (show) {
        bannerText_->setText(shown_.notTranscribed.size() == 1
                                 ? tr("Clip not transcribed: %1").arg(shown_.notTranscribed.first())
                                 : tr("Clips not transcribed: %1").arg(shown_.notTranscribed.join(QStringLiteral(", "))));
    }
}

void TranscriptTool::refreshSideButton() {
    side_->setText(source_ ? tr("Source") : tr("Record"));
    // "the button will appear green when the Source side is selected" (p.444).
    side_->setStyleSheet(source_ ? QStringLiteral("QToolButton { background-color: #2e8b3c; color: white; }") : QString());
}

void TranscriptTool::setSourceSide(bool source) {
    source_ = source;
    side_->setChecked(source);
    refreshSideButton();
    refreshHighlights();
}

void TranscriptTool::setGanged(bool on) {
    ganged_ = on;
    gang_->setChecked(on);
}

void TranscriptTool::setShowTimecode(bool on) {
    showTimecode_ = on;
    rebuild();
}

void TranscriptTool::setShowSpeakers(bool on) {
    showSpeakers_ = on;
    rebuild();
}

void TranscriptTool::setShowNotTranscribed(bool on) {
    showNotTranscribed_ = on;
    refreshBanner();
}

void TranscriptTool::setTextFont(const QFont& font) {
    view_->setFont(font);
}

void TranscriptTool::setPosition(std::int64_t frame) {
    if (!ganged_) return;
    const auto word = model::wordAt(shown_, frame);
    if (word == playhead_) return;
    playhead_ = word;
    refreshHighlights();
}

void TranscriptTool::setMarks(std::optional<std::int64_t> in, std::optional<std::int64_t> out) {
    if (!ganged_ || (in == in_ && out == out_)) return;
    in_ = in;
    out_ = out;
    // "Highlighted text, as well as In and Out marks, are always in sync when the Transcript Tool is ganged" (p.442):
    // the words from the IN to the OUT selected.
    int first = -1;
    int last = -1;
    if (in && out) {
        for (int i = 0; i < spans_.size(); ++i) {
            const model::ShownWord& w = shown_.lines[spans_[i].line].words[spans_[i].word];
            if (w.frame >= *in && w.frame <= *out) {
                if (first < 0) first = i;
                last = i;
            }
        }
    }
    updating_ = true;
    QTextCursor c(view_->document());
    if (first >= 0) {
        c.setPosition(spans_[first].start);
        c.setPosition(spans_[last].end, QTextCursor::KeepAnchor);
    } else {
        c.setPosition(view_->textCursor().position());
    }
    view_->setTextCursor(c);
    updating_ = false;
}

// The playhead word bold in the side's colour.
void TranscriptTool::refreshHighlights() {
    QList<QTextEdit::ExtraSelection> extras;
    if (playhead_) {
        const int i = spanIndex(playhead_->first, playhead_->second);
        if (i >= 0) {
            QTextEdit::ExtraSelection e;
            e.cursor = QTextCursor(view_->document());
            e.cursor.setPosition(spans_[i].start);
            e.cursor.setPosition(spans_[i].end, QTextCursor::KeepAnchor);
            e.format.setFontWeight(QFont::Bold);
            e.format.setForeground(source_ ? QColor(0x3c, 0xc0, 0x50) : QColor(0x4a, 0x90, 0xe2));
            extras << e;
            if (!view_->textCursor().hasSelection()) view_->ensureCursorVisible();
        }
    }
    view_->setExtraSelections(extras);
}

int TranscriptTool::spanIndex(int line, int word) const {
    for (int i = 0; i < spans_.size(); ++i) {
        if (spans_[i].line == line && spans_[i].word == word) return i;
    }
    return -1;
}

std::optional<int> TranscriptTool::spanAt(int position) const {
    for (int i = 0; i < spans_.size(); ++i) {
        if (position >= spans_[i].start && position <= spans_[i].end) return i;
    }
    return std::nullopt;
}

void TranscriptTool::selectionChanged() {
    if (updating_ || !ganged_) return;
    const QTextCursor c = view_->textCursor();
    if (!c.hasSelection()) return;
    // The words the selection touches.
    int first = -1;
    int last = -1;
    for (int i = 0; i < spans_.size(); ++i) {
        if (spans_[i].end > c.selectionStart() && spans_[i].start < c.selectionEnd()) {
            if (first < 0) first = i;
            last = i;
        }
    }
    if (first < 0) return;
    const model::ShownWord& a = shown_.lines[spans_[first].line].words[spans_[first].word];
    const model::ShownWord& b = shown_.lines[spans_[last].line].words[spans_[last].word];
    in_ = a.frame;
    out_ = std::max(a.frame, b.endFrame - 1);
    emit marksChosen(*in_, *out_);
}

void TranscriptTool::selectWords(int line, int from, int to) {
    const int a = spanIndex(line, from);
    const int b = spanIndex(line, std::max(from, to - 1));
    if (a < 0 || b < 0) return;
    QTextCursor c(view_->document());
    c.setPosition(spans_[a].start);
    c.setPosition(spans_[b].end, QTextCursor::KeepAnchor);
    view_->setTextCursor(c); // selectionChanged() emits the marks
}

void TranscriptTool::clickWord(int line, int word) {
    if (line < 0 || line >= shown_.lines.size() || word < 0 || word >= shown_.lines[line].words.size()) return;
    playhead_ = std::pair{line, word};
    refreshHighlights();
    if (ganged_) emit positionChosen(shown_.lines[line].words[word].frame);
}

// A click without a drag on a word moves the playhead there; Shift+Left/Right move the selection's end word by word
// ("Precise word selection is possible by holding down Shift and using the arrow keys", p.442).
bool TranscriptTool::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view_->viewport() && event->type() == QEvent::MouseButtonRelease) {
        const QTextCursor c = view_->textCursor();
        if (!c.hasSelection()) {
            if (const auto i = spanAt(c.position())) clickWord(spans_[*i].line, spans_[*i].word);
        }
    }
    if (watched == view_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Left || key->key() == Qt::Key_Right) && (key->modifiers() & Qt::ShiftModifier) && !spans_.isEmpty()) {
            const QTextCursor c = view_->textCursor();
            const int anchor = c.hasSelection() ? c.anchor() : (playhead_ ? spans_[std::max(0, spanIndex(playhead_->first, playhead_->second))].start : 0);
            const auto anchorSpan = spanAt(anchor).value_or(0);
            int end = spanAt(c.hasSelection() ? c.position() - 1 : anchor).value_or(anchorSpan);
            end = std::clamp(end + (key->key() == Qt::Key_Right ? 1 : -1), 0, static_cast<int>(spans_.size()) - 1);
            QTextCursor s(view_->document());
            s.setPosition(std::min(spans_[anchorSpan].start, spans_[end].start));
            s.setPosition(std::max(spans_[anchorSpan].end, spans_[end].end), QTextCursor::KeepAnchor);
            view_->setTextCursor(s);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

int TranscriptTool::search(const QString& text) {
    matches_.clear();
    QStringList wanted;
    for (const QString& w : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        const QString b = bare(w);
        if (!b.isEmpty()) wanted << b;
    }
    if (!wanted.isEmpty()) {
        for (int i = 0; i + wanted.size() <= spans_.size(); ++i) {
            bool match = true;
            for (int k = 0; k < wanted.size() && match; ++k) {
                const WordSpan& s = spans_[i + k];
                match = bare(shown_.lines[s.line].words[s.word].text) == wanted[k];
            }
            if (match) matches_ << std::pair{i, static_cast<int>(wanted.size())};
        }
    }
    found_->setText(wanted.isEmpty() ? QString() : tr("%n found", nullptr, static_cast<int>(matches_.size())));
    return static_cast<int>(matches_.size());
}

bool TranscriptTool::next(bool back) {
    if (matches_.isEmpty()) return false;
    const int here = playhead_ ? spanIndex(playhead_->first, playhead_->second) : -1;
    int pick = -1;
    if (back) {
        for (int m = static_cast<int>(matches_.size()) - 1; m >= 0; --m) {
            if (matches_[m].first < here) {
                pick = m;
                break;
            }
        }
        if (pick < 0) pick = static_cast<int>(matches_.size()) - 1;
    } else {
        for (int m = 0; m < matches_.size(); ++m) {
            if (matches_[m].first > here) {
                pick = m;
                break;
            }
        }
        if (pick < 0) pick = 0;
    }
    const WordSpan& s = spans_[matches_[pick].first];
    clickWord(s.line, s.word);
    return true;
}

QMenu* TranscriptTool::fastMenu(QWidget* parent) {
    auto* menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("transcriptFastMenuMenu"));
    auto toggle = [&](const QString& text, bool on, auto setter) {
        QAction* a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(on);
        connect(a, &QAction::toggled, this, setter);
    };
    toggle(tr("Show Timecode"), showTimecode_, [this](bool on) { setShowTimecode(on); });
    toggle(tr("Show Speaker ID"), showSpeakers_, [this](bool on) { setShowSpeakers(on); });
    toggle(tr("Show Not Transcribed"), showNotTranscribed_, [this](bool on) { setShowNotTranscribed(on); });
    menu->addSeparator();
    menu->addAction(tr("Set Font..."), this, [this] { emit fontRequested(); });
    // "Transcribe Remaining Clips, if transcripts are missing for some clips in groups, multigroups, or sequences, which
    // processes them in the foreground" (p.443).
    QAction* remaining = menu->addAction(tr("Transcribe Remaining Clips"), this, [this] { emit transcribeRequested(shown_.notTranscribedMobs); });
    remaining->setEnabled(!shown_.notTranscribedMobs.isEmpty());
    return menu;
}

QString TranscriptTool::plainText() const {
    return view_->toPlainText();
}

} // namespace lumenis::ui
