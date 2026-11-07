#pragma once

// The Transcript Tool (Editing Guide p.442-450; docs/spec/transcripts.md, 8.2b): "a dockable window that provides direct
// access to transcripts for each clip and sequence" (p.442), opened by Tools > Transcript Tool (command
// tools.transcript_tool) as the "Transcript" panel. MainWindow gives it what the Source or Record monitor shows
// (model::shownTranscript) and follows it:
//
// * "ganged to the Source or Record monitor's playhead, which displays as a bold, green (for Source) or blue (for
//   Record) word" (p.442): setPosition();
// * "Clicking on a word in a transcript will also move the playhead ... selecting text in the Transcript Tool will set In
//   and Out marks", Shift and the arrow keys moving the selection word by word: positionChosen(), marksChosen();
// * the fast menu (p.443): Show Timecode, Show Speaker ID, Show Not Transcribed, Set Font, Transcribe Remaining Clips;
//   "Clip not transcribed" with a Transcribe button;
// * the gang button (p.444), the search field with its arrows, the Source/Record button ("green when the Source side is
//   selected") and, for sequences and groups, the audio track button (p.449).
//
// The text is plain ("uses plain text formatting for consistent pasting across applications", p.442): Ctrl+C copies it.

#include "lumenis/model/transcript_view.h"

#include <QFont>
#include <QStringList>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>

class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTextEdit;
class QToolButton;

namespace lumenis::ui {

class TranscriptTool : public QWidget {
    Q_OBJECT

public:
    explicit TranscriptTool(QWidget* parent = nullptr);

    // What to show; `timecode` writes a frame as the monitor's timecode (Show Timecode). Keeps the scroll position when
    // the same lines come again.
    using TimecodeText = std::function<QString(std::int64_t frame)>;
    void setTranscript(const model::ShownTranscript& shown, TimecodeText timecode = {});
    const model::ShownTranscript& transcript() const { return shown_; }
    // Nothing loaded on the shown side.
    void clear(const QString& message);

    // The monitor's position and marks; the word there is shown bold in the side's colour, the marked words selected.
    void setPosition(std::int64_t frame);
    void setMarks(std::optional<std::int64_t> in, std::optional<std::int64_t> out);
    // The playhead word as {line, word}, for tests.
    std::optional<std::pair<int, int>> playheadWord() const { return playhead_; }

    // The Source/Record button: true while the Source side is shown (the button green).
    bool sourceSide() const { return source_; }
    void setSourceSide(bool source);
    bool ganged() const { return ganged_; }
    void setGanged(bool on);

    bool showTimecode() const { return showTimecode_; }
    void setShowTimecode(bool on);
    bool showSpeakers() const { return showSpeakers_; }
    void setShowSpeakers(bool on);
    bool showNotTranscribed() const { return showNotTranscribed_; }
    void setShowNotTranscribed(bool on);
    // Set Font (p.443): the family and size of the transcript's text.
    void setTextFont(const QFont& font);

    // Search (p.444): every place the words of `text` follow one another, ignoring case and punctuation; the matches'
    // count. next() goes to the next (or with back, the previous) one after the playhead word, going round, and returns
    // false when there is none.
    int search(const QString& text);
    bool next(bool back = false);
    int matchCount() const { return static_cast<int>(matches_.size()); }

    // Selects the words [from, to) of line `line` as the user would (for tests and Shift+arrows): emits marksChosen().
    void selectWords(int line, int from, int to);
    // Clicks a word: emits positionChosen() with its frame.
    void clickWord(int line, int word);

    // The fast menu; the caller owns it.
    QMenu* fastMenu(QWidget* parent);
    QTextEdit* view() const { return view_; }
    // The text as shown (Ctrl+C copies the selection of it).
    QString plainText() const;

signals:
    void positionChosen(std::int64_t frame);
    // The marks for the selected words: IN at the first word, OUT on the last frame of the last.
    void marksChosen(std::int64_t in, std::int64_t out);
    void sideChosen(bool source);
    void tracksChosen(const QStringList& tracks);
    // The Transcribe button and Transcribe Remaining Clips: the master clips to transcribe now, by MobID.
    void transcribeRequested(const QStringList& mobs);
    void fontRequested();

private:
    struct WordSpan {
        int line = 0;
        int word = 0;
        int start = 0; // in the document
        int end = 0;
    };
    void rebuild();
    void refreshHighlights();
    void refreshBanner();
    void refreshSideButton();
    std::optional<int> spanAt(int position) const;
    int spanIndex(int line, int word) const;
    void selectionChanged();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QTextEdit* view_ = nullptr;
    QWidget* banner_ = nullptr;
    QLabel* bannerText_ = nullptr;
    QPushButton* transcribe_ = nullptr;
    QToolButton* fast_ = nullptr;
    QToolButton* gang_ = nullptr;
    QLineEdit* search_ = nullptr;
    QToolButton* previous_ = nullptr;
    QToolButton* nextButton_ = nullptr;
    QLabel* found_ = nullptr;
    QToolButton* side_ = nullptr;
    QToolButton* tracks_ = nullptr;

    model::ShownTranscript shown_;
    TimecodeText timecode_;
    QList<WordSpan> spans_;
    std::optional<std::pair<int, int>> playhead_;
    std::optional<std::int64_t> in_;
    std::optional<std::int64_t> out_;
    QList<std::pair<int, int>> matches_; // {first span, span count}
    QString message_;
    bool source_ = true;
    bool ganged_ = true;
    bool showTimecode_ = true;
    bool showSpeakers_ = true;
    bool showNotTranscribed_ = true;
    bool updating_ = false; // setting the selection ourselves
};

} // namespace lumenis::ui
