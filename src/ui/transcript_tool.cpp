// The Transcript Tool's window (lumenis/ui/transcript_tool.h).
#include "lumenis/ui/transcript_tool.h"

#include "lumenis/ui/theme.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
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
    view_->setAccessibleName(tr("Transcript")); // screen readers (10.3)
    view_->setReadOnly(true);
    view_->setAcceptRichText(false);
    view_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    view_->viewport()->installEventFilter(this);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view_, &QWidget::customContextMenuRequested, this, &TranscriptTool::showContextMenu);
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
    display_ = model::groupedLines(shown_, groupSpeakers_, static_cast<std::int64_t>(newLineAfter_ * fps_));
    speakerSpans_.clear();
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
    for (int l = 0; l < display_.lines.size(); ++l) {
        const model::ShownLine& line = display_.lines[l];
        if (l > 0) cursor.insertBlock();
        if (showTimecode_ && timecode_) cursor.insertText(timecode_(line.frame) + QStringLiteral("  "), dim);
        if (showSpeakers_) {
            SpeakerSpan sp;
            sp.line = l;
            sp.start = cursor.position();
            cursor.insertText(line.speaker, speaker);
            sp.end = cursor.position();
            speakerSpans_ << sp;
            cursor.insertText(QStringLiteral(": "), speaker);
        }
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
    const auto word = model::wordAt(display_, frame);
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
            const model::ShownWord& w = display_.lines[spans_[i].line].words[spans_[i].word];
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
    const model::ShownWord& a = display_.lines[spans_[first].line].words[spans_[first].word];
    const model::ShownWord& b = display_.lines[spans_[last].line].words[spans_[last].word];
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
    if (line < 0 || line >= display_.lines.size() || word < 0 || word >= display_.lines[line].words.size()) return;
    playhead_ = std::pair{line, word};
    refreshHighlights();
    if (ganged_) emit positionChosen(display_.lines[line].words[word].frame);
}

// A click without a drag on a word moves the playhead there; Shift+Left/Right move the selection's end word by word
// ("Precise word selection is possible by holding down Shift and using the arrow keys", p.442).
bool TranscriptTool::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view_->viewport() && event->type() == QEvent::MouseButtonDblClick) {
        // "Double-click a word to edit it in place"; "double-click on the speaker name" to rename it (p.445).
        const int pos = view_->cursorForPosition(static_cast<QMouseEvent*>(event)->pos()).position();
        if (const auto sp = speakerAt(pos)) {
            const int line = speakerSpans_[*sp].line;
            beginEdit(speakerSpans_[*sp].start, speakerSpans_[*sp].end, display_.lines[line].speaker,
                      [this, line](const QString& text) { renameSpeaker(line, text); });
            return true;
        }
        if (const auto w = spanAt(pos)) {
            const QTextCursor c = view_->textCursor();
            int first = *w;
            int last = *w;
            if (c.hasSelection()) {
                for (int i = 0; i < spans_.size(); ++i) {
                    if (spans_[i].end > c.selectionStart() && spans_[i].start < c.selectionEnd() && spans_[i].line == spans_[*w].line) {
                        first = std::min(first, i);
                        last = std::max(last, i);
                    }
                }
            }
            QStringList text;
            for (int i = first; i <= last; ++i) text << display_.lines[spans_[i].line].words[spans_[i].word].text;
            const int line = spans_[first].line;
            const int from = spans_[first].word;
            const int to = spans_[last].word + 1;
            beginEdit(spans_[first].start, spans_[last].end, text.join(QLatin1Char(' ')),
                      [this, line, from, to](const QString& t) { editWords(line, from, to, t); });
            return true;
        }
    }
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
                match = bare(display_.lines[s.line].words[s.word].text) == wanted[k];
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
    // 2026.8 guide p.446-447.
    toggle(tr("Group Speaker Lines"), groupSpeakers_, [this](bool on) { setGroupSpeakers(on); });
    menu->addAction(tr("New Line After (%1 seconds)...").arg(newLineAfter_), this, [this] {
        bool ok = false;
        const int seconds = QInputDialog::getInt(this, tr("New Line After"), tr("Seconds:"), newLineAfter_, 1, 3600, 1, &ok);
        if (ok) setNewLineAfter(seconds);
    });
    menu->addSeparator();
    // "A Find and Replace feature is available in the Transcript Tool fast menu" (p.446).
    menu->addAction(tr("Find and Replace..."), this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Find and Replace"));
        auto* form = new QFormLayout(&dialog);
        auto* find = new QLineEdit(search_->text(), &dialog);
        auto* replace = new QLineEdit(&dialog);
        form->addRow(tr("Find:"), find);
        form->addRow(tr("Replace with:"), replace);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
        buttons->addButton(tr("Replace All"), QDialogButtonBox::AcceptRole);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        form->addRow(buttons);
        if (dialog.exec() == QDialog::Accepted) replaceAll(find->text(), replace->text());
    });
    QAction* revert = menu->addAction(tr("Revert Transcript"), this, [this] { revertTranscript(); });
    revert->setEnabled(!display_.lines.isEmpty());
    // "Choose 'Delete Transcript'. The currently loaded transcript is immediately deleted" (2026.8 guide p.456): every
    // transcript shown.
    QAction* remove = menu->addAction(tr("Delete Transcript"), this, [this] { emit deleteRequested(shown_.speakers.keys()); });
    remove->setObjectName(QStringLiteral("deleteTranscript"));
    remove->setEnabled(!shown_.speakers.isEmpty());
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

namespace lumenis::ui {

void TranscriptTool::setFrameRate(double fps) {
    if (fps > 0) fps_ = fps;
}

void TranscriptTool::setGroupSpeakers(bool on) {
    groupSpeakers_ = on;
    rebuild();
}

void TranscriptTool::setNewLineAfter(int seconds) {
    newLineAfter_ = std::max(1, seconds);
    rebuild();
}

std::optional<int> TranscriptTool::speakerAt(int position) const {
    for (int i = 0; i < speakerSpans_.size(); ++i) {
        if (position >= speakerSpans_[i].start && position < speakerSpans_[i].end) return i;
    }
    return std::nullopt;
}

void TranscriptTool::editWords(int line, int from, int to, const QString& text) {
    if (line < 0 || line >= display_.lines.size()) return;
    const QList<model::ShownWord>& words = display_.lines[line].words;
    from = std::clamp(from, 0, static_cast<int>(words.size()));
    to = std::clamp(to, from, static_cast<int>(words.size()));
    if (from == to) return;
    // "Only one sentence can be edited at a time to maintain timing accuracy" (p.446): the words of the first word's
    // transcript line.
    const model::ShownWord& first = words[from];
    int last = first.word;
    for (int i = from + 1; i < to; ++i) {
        if (words[i].mob != first.mob || words[i].line != first.line) break;
        last = words[i].word;
    }
    const int tline = first.line;
    const int a = first.word;
    const int b = last + 1;
    emit editRequested(first.mob, [tline, a, b, text](model::Transcript& t) { return model::editWords(t, tline, a, b, text); });
}

bool TranscriptTool::editSelection(const QString& text) {
    const QTextCursor c = view_->textCursor();
    if (!c.hasSelection()) return false;
    int first = -1;
    int last = -1;
    for (int i = 0; i < spans_.size(); ++i) {
        if (spans_[i].end > c.selectionStart() && spans_[i].start < c.selectionEnd()) {
            if (first < 0) first = i;
            if (spans_[i].line == spans_[first].line) last = i;
        }
    }
    if (first < 0) return false;
    editWords(spans_[first].line, spans_[first].word, spans_[last].word + 1, text);
    return true;
}

void TranscriptTool::renameSpeaker(int line, const QString& name) {
    if (line < 0 || line >= display_.lines.size()) return;
    const model::ShownLine& l = display_.lines[line];
    const int id = l.speakerId;
    emit editRequested(l.mob, [id, name](model::Transcript& t) { return model::renameSpeaker(t, id, name); });
}

void TranscriptTool::identifySpeaker(int line, const QString& name) {
    if (line < 0 || line >= display_.lines.size()) return;
    const model::ShownLine& l = display_.lines[line];
    const int tline = l.line;
    emit editRequested(l.mob, [tline, name](model::Transcript& t) { return model::identifySpeaker(t, tline, name); });
}

void TranscriptTool::correctSpeaker(int line, int speaker) {
    if (line < 0 || line >= display_.lines.size()) return;
    const model::ShownLine& l = display_.lines[line];
    const int tline = l.line;
    emit editRequested(l.mob, [tline, speaker](model::Transcript& t) { return model::correctSpeaker(t, tline, speaker); });
}

void TranscriptTool::startNewLine(int line, int word) {
    if (line < 0 || line >= display_.lines.size() || word <= 0 || word >= display_.lines[line].words.size()) return;
    const model::ShownWord& w = display_.lines[line].words[word];
    // A paragraph of several lines (Group Speaker Lines) splits where its line splits.
    const int tline = w.line;
    const int at = w.word;
    emit editRequested(w.mob, [tline, at](model::Transcript& t) { return model::startNewLine(t, tline, at); });
}

void TranscriptTool::revertLine(int line) {
    if (line < 0 || line >= display_.lines.size() || display_.lines[line].words.isEmpty()) return;
    const model::ShownWord& w = display_.lines[line].words.first();
    const int tline = w.line;
    emit editRequested(w.mob, [tline](model::Transcript& t) { return model::revertLine(t, tline); });
}

void TranscriptTool::revertTranscript() {
    for (const QString& mob : display_.speakers.keys()) {
        emit editRequested(mob, [](model::Transcript& t) { return model::revertTranscript(t); });
    }
}

void TranscriptTool::replaceAll(const QString& find, const QString& replace) {
    for (const QString& mob : display_.speakers.keys()) {
        emit editRequested(mob, [find, replace](model::Transcript& t) { return model::replaceWords(t, find, replace) > 0; });
    }
}

void TranscriptTool::beginEdit(int start, int end, const QString& text, std::function<void(const QString&)> done) {
    delete editor_;
    QTextCursor c(view_->document());
    c.setPosition(start);
    const QRect from = view_->cursorRect(c);
    c.setPosition(end);
    const QRect to = view_->cursorRect(c);
    editor_ = new QLineEdit(text, view_->viewport());
    editor_->setObjectName(QStringLiteral("transcriptEditor"));
    const int width = std::max(120, (to.top() == from.top() ? to.right() - from.left() : 240) + 40);
    editor_->setGeometry(from.left(), from.top() - 2, width, from.height() + 6);
    editor_->selectAll();
    QPointer<QLineEdit> editor = editor_;
    connect(editor_, &QLineEdit::returnPressed, this, [this, editor, done] {
        if (!editor) return;
        const QString value = editor->text();
        editor->deleteLater();
        editor_ = nullptr;
        done(value);
    });
    connect(editor_, &QLineEdit::editingFinished, this, [this, editor] {
        if (editor && !editor->hasFocus()) {
            editor->deleteLater();
            if (editor_ == editor) editor_ = nullptr;
        }
    });
    editor_->show();
    editor_->setFocus();
}

QMenu* TranscriptTool::contextMenuAt(int position, QWidget* parent) {
    auto* menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("transcriptContextMenu"));
    // On a speaker's name: "Identify Speaker" and "Correct Speaker ID" (p.445).
    if (const auto sp = speakerAt(position)) {
        const int line = speakerSpans_[*sp].line;
        const model::ShownLine& l = display_.lines[line];
        QMenu* identify = menu->addMenu(tr("Identify Speaker"));
        identify->setObjectName(QStringLiteral("identifySpeaker"));
        QStringList names = knownSpeakers_;
        for (const auto& list : display_.speakers) {
            for (const auto& [id, name] : list) {
                if (!names.contains(name) && !name.startsWith(QStringLiteral("Speaker "))) names << name;
            }
        }
        names.sort(Qt::CaseInsensitive);
        for (const QString& name : names) {
            connect(identify->addAction(name), &QAction::triggered, this, [this, line, name] { identifySpeaker(line, name); });
        }
        if (!names.isEmpty()) identify->addSeparator();
        identify->addAction(tr("Clear Name"), this, [this, line] { renameSpeaker(line, QString()); });
        identify->addAction(tr("Rename..."), this, [this, sp = *sp, line] {
            beginEdit(speakerSpans_[sp].start, speakerSpans_[sp].end, display_.lines[line].speaker,
                      [this, line](const QString& text) { renameSpeaker(line, text); });
        });
        QMenu* correct = menu->addMenu(tr("Correct Speaker ID"));
        correct->setObjectName(QStringLiteral("correctSpeaker"));
        for (const auto& speaker : display_.speakers.value(l.mob)) {
            const int id = speaker.first;
            QAction* a = correct->addAction(speaker.second);
            a->setCheckable(true);
            a->setChecked(id == l.speakerId);
            connect(a, &QAction::triggered, this, [this, line, id] { correctSpeaker(line, id); });
        }
        correct->addSeparator();
        correct->addAction(tr("Add New Speaker"), this, [this, line] { correctSpeaker(line, 0); });
        // "Assign each identified speaker to a corresponding camera angle" (2026.8 guide p.451). Ours: on the speaker's
        // menu, with a group shown.
        if (!cameraAngles_.isEmpty()) {
            QMenu* assign = menu->addMenu(tr("Assign to Camera Angle"));
            assign->setObjectName(QStringLiteral("assignCameraAngle"));
            const QString speaker = l.speaker;
            const int current = speakerAngles_.value(speaker, -1);
            for (int a = -1; a < cameraAngles_.size(); ++a) {
                QAction* action = assign->addAction(a < 0 ? tr("None") : cameraAngles_[a]);
                action->setCheckable(true);
                action->setChecked(a == current);
                connect(action, &QAction::triggered, this, [this, speaker, a] { emit speakerAngleChosen(speaker, a); });
                if (a < 0) assign->addSeparator();
            }
        }
        return menu;
    }
    // "The right-click menu now displays only Edit Text and Copy when text is selected" (p.446).
    if (view_->textCursor().hasSelection()) {
        menu->addAction(tr("Edit Text"), this, [this] {
            const QTextCursor c = view_->textCursor();
            beginEdit(c.selectionStart(), c.selectionEnd(), c.selectedText(), [this](const QString& text) { editSelection(text); });
        });
        menu->addAction(tr("Copy"), view_, &QTextEdit::copy);
        return menu;
    }
    if (const auto w = spanAt(position)) {
        const int line = spans_[*w].line;
        const int word = spans_[*w].word;
        menu->addAction(tr("Edit Text"), this, [this, w = *w, line, word] {
            beginEdit(spans_[w].start, spans_[w].end, display_.lines[line].words[word].text,
                      [this, line, word](const QString& text) { editWords(line, word, word + 1, text); });
        });
        // 2026.8 guide p.447.
        QAction* split = menu->addAction(tr("Start New Line"), this, [this, line, word] { startNewLine(line, word); });
        split->setEnabled(word > 0);
        menu->addAction(tr("Revert"), this, [this, line] { revertLine(line); });
    }
    return menu;
}

void TranscriptTool::showContextMenu(const QPoint& pos) {
    const int position = view_->cursorForPosition(pos).position();
    QMenu* menu = contextMenuAt(position, this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (menu->isEmpty()) {
        delete menu;
        return;
    }
    menu->popup(view_->viewport()->mapToGlobal(pos));
}

} // namespace lumenis::ui
