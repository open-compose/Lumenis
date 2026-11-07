#pragma once

#include "lumenis/model/phrase_find.h"
#include "lumenis/model/text_find.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QProgressBar;
class QTimer;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QVBoxLayout;

namespace lumenis::ui {

class BinContainer;
class Composer;
class Timeline;

// The Find window (Edit > Find, Ctrl+F; Editing Guide p.417, p.428-431; docs/spec/find.md). Four tabs:
// * Clips and Sequences: items of the project's bins, open or not, or of the current bin, matching
//   the text in any column, with filters;
// * Script Text: dimmed until scripts exist;
// * Timeline and Monitors: the Record monitor's sequence's markers, clip names and Timeline text;
// * Markers: marker comments in every sequence and clip of the project.
// Double-clicking a result loads it or moves to it; findNext() (Ctrl+G) goes to the next one.
// PhraseFind AI (p.436-441, 8.2c) shares the Clips and Sequences tab: its button searches the dialogue of the clips'
// transcripts (model::findPhrase) through what setPhraseFind() gives, listing Score and Transcription.
// MainWindow opens it, one window, kept.
class FindWindow : public QWidget {
    Q_OBJECT

public:
    // The three must outlive the window.
    FindWindow(BinContainer& bins, Composer& composer, Timeline& timeline, QWidget* parent = nullptr);

    enum Tab { ClipsTab, ScriptTab, TimelineTab, MarkersTab };
    // "Bins in Project", "Scripts in Project", "Bins and Scripts in Project", "Current Bin" (p.429).
    enum class Scope { BinsInProject, ScriptsInProject, BinsAndScripts, CurrentBin };

    QTabWidget* tabs() const { return tabs_; }
    // The Find text box (an editable list of recent searches) of the current tab.
    QComboBox* findBox() const;
    void setText(const QString& text);
    void setScope(Scope scope);
    void setIgnoreCase(bool on);
    void setWholeWords(bool on);
    void setLoadIntoMonitor(bool on);
    // Timeline and Monitors: what is searched (p.430).
    void setTimelineKinds(bool markers, bool clipNames, bool timelineText);
    // Filter rows of the current tab: the column ("" = Any Column) and text of row `index`, adding rows
    // as needed (the "+" button); removeFilter is the "-" button.
    void setFilter(int index, const QString& column, const QString& text);
    void removeFilter(int index);
    int filterCount() const;

    // Find (the button, or Enter): searches and fills the Results list. Returns the number found.
    int find();
    QTableWidget* results() const;
    // Double-clicking result `row` (p.418, p.429, p.431).
    void openResult(int row);
    // Ctrl+G: the next occurrence (p.429, p.431): the next result after the current one, opened; in
    // Current Bin, the next match selected in the bin. False with nothing found.
    bool findNext();
    // The Results columns (Select Columns, p.417-418, p.429): the bin headings shown for Clips and
    // Sequences.
    void setResultColumns(const QStringList& headings);
    QStringList resultColumns() const { return columns_; }

    // PhraseFind AI (8.2c). What the window needs from the application: the transcripts, the index's state (clips
    // transcribed, clips to transcribe) and whether indexing runs (the Settings button's Stop/Start, p.441).
    struct PhraseFindSource {
        model::TranscriptFor transcriptFor;
        std::function<std::pair<int, int>()> indexStatus;
        std::function<bool()> indexing;
        std::function<void(bool)> setIndexing;
        std::function<void()> manageTranscripts; // the Settings button's Manage Transcripts (8.2d)
    };
    void setPhraseFind(PhraseFindSource source);
    // The PhraseFind AI button: searches the dialogue and fills the Results (Clips and Sequences tab). Returns the
    // number found.
    int phraseFind();
    // The "PhraseFind AI Index" status (p.437, p.441): "Full green ... ready to search. Partial green ... the index is
    // in process"; read again every two seconds while the window shows.
    QProgressBar* phraseIndex() const { return phraseIndex_; }
    void refreshIndexStatus();
    // The Settings button's menu (p.441); the caller owns it.
    QMenu* settingsMenu(QWidget* parent);
    // "Found" and "after filtering" (p.441).
    QString foundText() const;

    // A test hook: the Select Columns dialog's answer instead of the dialog.
    using ColumnsAsk = std::function<std::optional<QStringList>(const QStringList& current)>;
    void setColumnsAsk(ColumnsAsk ask) { columnsAsk_ = std::move(ask); }

public slots:
    // Select Columns (p.417-418, p.429): the dialog, or setColumnsAsk's answer; a search shown again.
    void chooseColumns();

signals:
    // A result asks to be shown in its bin (Clips and Sequences without Load into monitor).
    void showInBin(const QString& binId, const QString& itemId);

private:
    struct Page;
    Page& page(int tab) const;
    QWidget* buildPage(int tab);
    void addFilterRow(int tab);
    QList<model::FindFilter> filters(int tab) const;
    model::TextFindOptions options() const;
    QList<model::FindBin> projectBins() const;
    void fillItems(const QList<model::ItemFound>& found);
    void fillPhrases(const QList<model::PhraseHit>& found);
    // "The filter menu populates after the initial search" (p.430): Any Column, `extra` and the found items' columns.
    void populateFilters(int tab, const QStringList& extra);
    void fillMarkers(const QList<model::MarkerFound>& found);
    int fillTimeline(const QString& text);
    QString timecode(std::int64_t frame, const model::BinItem& item) const;

    BinContainer& bins_;
    Composer& composer_;
    Timeline& timeline_;
    QTabWidget* tabs_ = nullptr;
    QList<Page*> pages_;
    QComboBox* scope_ = nullptr;
    QCheckBox* ignoreCase_ = nullptr;
    QCheckBox* wholeWords_ = nullptr;
    QCheckBox* loadIntoMonitor_ = nullptr;
    QCheckBox* markers_ = nullptr;
    QCheckBox* clipNames_ = nullptr;
    QCheckBox* timelineText_ = nullptr;
    QStringList columns_;
    ColumnsAsk columnsAsk_;
    PhraseFindSource phrase_;
    QProgressBar* phraseIndex_ = nullptr;
    QLabel* found_ = nullptr;
    QTimer* indexTimer_ = nullptr;
    bool lastPhrase_ = false; // the Clips tab's results are PhraseFind's
    // Current Bin: the matches selected in it, and which one Ctrl+G selected last.
    QString currentBin_;
    QStringList currentMatches_;
    int currentIndex_ = -1;

protected:
    void showEvent(QShowEvent* event) override;

public:
    ~FindWindow() override;
};

} // namespace lumenis::ui
