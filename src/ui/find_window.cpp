#include "lumenis/ui/find_window.h"

#include "lumenis/core/timecode.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/timeline.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

// The Find window (find_window.h; Editing Guide p.417, p.428-431). Bins are read as they are now: an
// open bin from its document, a closed one from its file, so nothing needs indexing first (ours; the
// guide's Bin Index light is always full). The Timeline and Monitors tab reads the Record monitor's
// sequence; the Clip Text the Timeline shows comes from Timeline::clipText. PhraseFind AI (8.2c) searches the
// transcripts of the bins' clips through model::findPhrase.

namespace lumenis::ui {

namespace {

// A result row: where it leads.
struct Result {
    QString binId;
    model::BinItem item;
    std::int64_t frame = -1; // markers, Timeline and PhraseFind results: where the position goes
    bool phrase = false;     // a PhraseFind hit: the IN mark goes at `frame` too
};

} // namespace

struct FindWindow::Page {
    QComboBox* find = nullptr;
    QPushButton* findButton = nullptr;
    QVBoxLayout* filters = nullptr;
    QList<QWidget*> filterRows;
    QStringList filterColumns{QString()}; // "" is Any Column; the rest come with the first search
    QTableWidget* table = nullptr;
    QList<Result> rows;
};

FindWindow::FindWindow(BinContainer& bins, Composer& composer, Timeline& timeline, QWidget* parent)
    : QWidget(parent, Qt::Tool), bins_(bins), composer_(composer), timeline_(timeline) {
    setObjectName(QStringLiteral("findWindow"));
    setWindowTitle(tr("Find"));
    columns_ = {QStringLiteral("Start"), QStringLiteral("End"), QStringLiteral("Duration")};
    auto* v = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    v->addWidget(tabs_);
    for (int tab : {ClipsTab, ScriptTab, TimelineTab, MarkersTab}) {
        pages_ << new Page;
        const QStringList names{tr("Clips and Sequences"), tr("Script Text"), tr("Timeline and Monitors"), tr("Markers")};
        tabs_->addTab(buildPage(tab), names.at(tab));
    }
    // Script Text waits for scripts (ScriptSync, Phase 9).
    tabs_->setTabEnabled(ScriptTab, false);
    // The Bin Index status (p.429): ours, always indexed. The PhraseFind AI Index (p.437, p.441) beside it, and the
    // Settings button (p.441).
    auto* status = new QHBoxLayout;
    auto* index = new QLabel(tr("Bin Index: ready"), this);
    index->setObjectName(QStringLiteral("binIndex"));
    status->addWidget(index);
    status->addWidget(new QLabel(tr("PhraseFind AI Index"), this));
    phraseIndex_ = new QProgressBar(this);
    phraseIndex_->setObjectName(QStringLiteral("phraseIndex"));
    phraseIndex_->setTextVisible(false);
    phraseIndex_->setFixedWidth(120);
    phraseIndex_->setRange(0, 1);
    phraseIndex_->setValue(1);
    // Green as the guide's status.
    QPalette green = phraseIndex_->palette();
    green.setColor(QPalette::Highlight, QColor(0x3c, 0xa0, 0x46));
    phraseIndex_->setPalette(green);
    status->addWidget(phraseIndex_);
    found_ = new QLabel(this);
    found_->setObjectName(QStringLiteral("found"));
    status->addWidget(found_);
    status->addStretch(1);
    auto* settings = new QToolButton(this);
    settings->setObjectName(QStringLiteral("searchSettings"));
    settings->setText(tr("Settings"));
    settings->setPopupMode(QToolButton::InstantPopup);
    settings->setMenu(settingsMenu(settings));
    connect(settings->menu(), &QMenu::aboutToShow, this, [this, settings] {
        // Rebuilt each time so that Stop/Start reads the current state.
        QMenu* menu = settings->menu();
        QMenu* fresh = settingsMenu(this);
        menu->clear();
        for (QAction* a : fresh->actions()) {
            a->setParent(menu);
            menu->addAction(a);
        }
        delete fresh;
    });
    status->addWidget(settings);
    v->addLayout(status);
    indexTimer_ = new QTimer(this);
    indexTimer_->setInterval(2000);
    connect(indexTimer_, &QTimer::timeout, this, [this] {
        if (isVisible()) refreshIndexStatus();
    });
    indexTimer_->start();
    resize(720, 420);
}

FindWindow::~FindWindow() {
    qDeleteAll(pages_);
}

FindWindow::Page& FindWindow::page(int tab) const {
    return *pages_.at(tab);
}

QComboBox* FindWindow::findBox() const {
    return page(tabs_->currentIndex()).find;
}

QTableWidget* FindWindow::results() const {
    return page(tabs_->currentIndex()).table;
}

// Each page: "Find:" with the text box and the Find button, the filter rows, the tab's own options and
// the Results list, as in the pictures on p.429 and p.431.
QWidget* FindWindow::buildPage(int tab) {
    Page& p = page(tab);
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    auto* findRow = new QHBoxLayout;
    findRow->addWidget(new QLabel(tr("Find:"), w));
    p.find = new QComboBox(w);
    p.find->setEditable(true);
    p.find->setInsertPolicy(QComboBox::InsertAtTop);
    p.find->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    findRow->addWidget(p.find, 1);
    p.findButton = new QPushButton(tr("Find"), w);
    findRow->addWidget(p.findButton);
    if (tab == ClipsTab) {
        // "Click the PhraseFind AI button to begin the search" (p.437).
        auto* phrase = new QPushButton(tr("PhraseFind AI"), w);
        phrase->setObjectName(QStringLiteral("phraseFindButton"));
        findRow->addWidget(phrase);
        connect(phrase, &QPushButton::clicked, this, [this] { phraseFind(); });
    }
    v->addLayout(findRow);
    connect(p.findButton, &QPushButton::clicked, this, [this] { find(); });
    connect(p.find->lineEdit(), &QLineEdit::returnPressed, this, [this] { find(); });
    if (tab != TimelineTab) {
        p.filters = new QVBoxLayout;
        v->addLayout(p.filters);
        addFilterRow(tab);
    }
    auto* options = new QHBoxLayout;
    if (tab == ClipsTab) {
        options->addWidget(new QLabel(tr("Search:"), w));
        scope_ = new QComboBox(w);
        scope_->addItems({tr("Bins in Project"), tr("Scripts in Project"), tr("Bins and Scripts in Project"), tr("Current Bin")});
        // No scripts yet: Scripts in Project is dimmed; with them, Bins and Scripts searches the bins.
        if (auto* model = qobject_cast<QStandardItemModel*>(scope_->model())) model->item(1)->setEnabled(false);
        options->addWidget(scope_);
        ignoreCase_ = new QCheckBox(tr("Ignore Case"), w);
        ignoreCase_->setChecked(true);
        wholeWords_ = new QCheckBox(tr("Whole Words only"), w);
        loadIntoMonitor_ = new QCheckBox(tr("Load into monitor"), w);
        loadIntoMonitor_->setChecked(true);
        options->addWidget(ignoreCase_);
        options->addWidget(wholeWords_);
        options->addWidget(loadIntoMonitor_);
        options->addStretch(1);
        auto* select = new QPushButton(tr("Select Columns"), w);
        connect(select, &QPushButton::clicked, this, &FindWindow::chooseColumns);
        options->addWidget(select);
    } else if (tab == TimelineTab) {
        options->addWidget(new QLabel(tr("Search:"), w));
        markers_ = new QCheckBox(tr("Markers"), w);
        clipNames_ = new QCheckBox(tr("Clip Names"), w);
        timelineText_ = new QCheckBox(tr("Timeline Text"), w);
        markers_->setChecked(true);
        clipNames_->setChecked(true);
        options->addWidget(markers_);
        options->addWidget(clipNames_);
        options->addWidget(timelineText_);
        options->addStretch(1);
    } else if (tab == MarkersTab) {
        // The Markers tab shares Ignore Case with the others (p.417).
        options->addWidget(new QLabel(tr("Searches the comments of every marker in the project."), w));
        options->addStretch(1);
    }
    v->addLayout(options);
    p.table = new QTableWidget(w);
    p.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    p.table->setSelectionBehavior(QAbstractItemView::SelectRows);
    p.table->setSelectionMode(QAbstractItemView::SingleSelection);
    p.table->verticalHeader()->hide();
    p.table->horizontalHeader()->setStretchLastSection(true);
    connect(p.table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { openResult(row); });
    v->addWidget(p.table, 1);
    return w;
}

// A filter row (p.417, p.430): the column menu, its text, and "+" and "-". "If the last filter appears,
// the '-' button removes the text, not the filter."
void FindWindow::addFilterRow(int tab) {
    Page& p = page(tab);
    auto* row = new QWidget(this);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(new QLabel(tr("Filter"), row));
    auto* column = new QComboBox(row);
    column->setObjectName(QStringLiteral("column"));
    for (const QString& c : p.filterColumns) column->addItem(c.isEmpty() ? tr("Any Column") : c, c);
    auto* text = new QLineEdit(row);
    text->setObjectName(QStringLiteral("text"));
    auto* plus = new QPushButton(QStringLiteral("+"), row);
    auto* minus = new QPushButton(QStringLiteral("-"), row);
    plus->setFixedWidth(28);
    minus->setFixedWidth(28);
    h->addWidget(column);
    h->addWidget(text, 1);
    h->addWidget(plus);
    h->addWidget(minus);
    p.filters->addWidget(row);
    p.filterRows << row;
    connect(plus, &QPushButton::clicked, this, [this, tab] { addFilterRow(tab); });
    connect(minus, &QPushButton::clicked, this, [this, tab, row] {
        const auto i = static_cast<int>(page(tab).filterRows.indexOf(row));
        const int current = tabs_->currentIndex();
        tabs_->setCurrentIndex(tab);
        removeFilter(i);
        tabs_->setCurrentIndex(current);
    });
}

int FindWindow::filterCount() const {
    return static_cast<int>(page(tabs_->currentIndex()).filterRows.size());
}

void FindWindow::setFilter(int index, const QString& column, const QString& text) {
    const int tab = tabs_->currentIndex();
    Page& p = page(tab);
    if (!p.filters) return;
    while (p.filterRows.size() <= index) addFilterRow(tab);
    QWidget* row = p.filterRows.at(index);
    auto* box = row->findChild<QComboBox*>(QStringLiteral("column"));
    if (box->findData(column) < 0) box->addItem(column.isEmpty() ? tr("Any Column") : column, column);
    box->setCurrentIndex(box->findData(column));
    row->findChild<QLineEdit*>(QStringLiteral("text"))->setText(text);
}

void FindWindow::removeFilter(int index) {
    Page& p = page(tabs_->currentIndex());
    if (index < 0 || index >= p.filterRows.size()) return;
    if (p.filterRows.size() == 1) {
        p.filterRows.first()->findChild<QLineEdit*>(QStringLiteral("text"))->clear();
        return;
    }
    delete p.filterRows.takeAt(index);
}

QList<model::FindFilter> FindWindow::filters(int tab) const {
    QList<model::FindFilter> out;
    for (QWidget* row : page(tab).filterRows) {
        const QString text = row->findChild<QLineEdit*>(QStringLiteral("text"))->text();
        if (!text.isEmpty()) out.append({row->findChild<QComboBox*>(QStringLiteral("column"))->currentData().toString(), text});
    }
    return out;
}

model::TextFindOptions FindWindow::options() const {
    return {ignoreCase_->isChecked(), wholeWords_->isChecked()};
}

void FindWindow::setText(const QString& text) {
    findBox()->setEditText(text);
}

void FindWindow::setScope(Scope scope) {
    scope_->setCurrentIndex(static_cast<int>(scope));
}

void FindWindow::setIgnoreCase(bool on) {
    ignoreCase_->setChecked(on);
}

void FindWindow::setWholeWords(bool on) {
    wholeWords_->setChecked(on);
}

void FindWindow::setLoadIntoMonitor(bool on) {
    loadIntoMonitor_->setChecked(on);
}

void FindWindow::setTimelineKinds(bool markers, bool clipNames, bool timelineText) {
    markers_->setChecked(markers);
    clipNames_->setChecked(clipNames);
    timelineText_->setChecked(timelineText);
}

// "Bins do not have to be open for the system to search in them" (p.428): open bins from their
// documents, the others from their files. The Trash is left out.
QList<model::FindBin> FindWindow::projectBins() const {
    QList<model::FindBin> out;
    const model::ProjectBins* tree = bins_.bins();
    if (!tree) return out;
    for (const model::BinNode* node : tree->bins()) {
        if (const BinDocument* doc = bins_.document(node->id)) {
            out.append({node->id, node->name, doc->bin()});
        } else if (const auto bin = model::BinFile::read(tree->binPath(node->id))) {
            out.append({node->id, node->name, *bin});
        }
    }
    return out;
}

QString FindWindow::timecode(std::int64_t frame, const model::BinItem& item) const {
    model::MonitorState s;
    s.load(QString(), item);
    const TimecodeFormat format = TimecodeFormat::forRate(item.editRate, model::isDropFrame(item));
    return QString::fromStdString(Timecode(s.startFrame() + frame, format).toString());
}

int FindWindow::find() {
    const int tab = tabs_->currentIndex();
    Page& p = page(tab);
    const QString text = p.find->currentText().trimmed();
    if (!text.isEmpty() && p.find->findText(text) < 0) p.find->insertItem(0, text);
    p.rows.clear();
    p.table->clearContents();
    p.table->setRowCount(0);
    int found = 0;
    if (tab == ClipsTab && scope_->currentIndex() == static_cast<int>(Scope::CurrentBin)) {
        // "The system searches for the text criteria in the last active bin. The system then selects the
        // first occurrence in the bin" (p.429); the results are not listed (p.430).
        currentMatches_.clear();
        currentIndex_ = -1;
        BinDocument* doc = bins_.activeDocument();
        if (!doc) return 0;
        currentBin_ = doc->nodeId();
        const auto matches = model::findItems({{doc->nodeId(), doc->name(), doc->bin()}}, text, filters(tab), options());
        for (const model::ItemFound& f : matches) currentMatches_ << f.item.id;
        found = static_cast<int>(currentMatches_.size());
        if (found > 0) findNext();
    } else if (tab == ClipsTab) {
        const auto matches = model::findItems(projectBins(), text, filters(tab), options());
        fillItems(matches);
        found = static_cast<int>(matches.size());
    } else if (tab == MarkersTab) {
        const auto matches = model::findMarkers(projectBins(), text, filters(tab), options());
        fillMarkers(matches);
        found = static_cast<int>(matches.size());
    } else if (tab == TimelineTab) {
        found = fillTimeline(text);
    }
    if (tab == ClipsTab) lastPhrase_ = false;
    populateFilters(tab, {});
    found_->setText(tr("Found: %1").arg(found));
    return found;
}

void FindWindow::populateFilters(int tab, const QStringList& extra) {
    Page& p = page(tab);
    if (!p.filters || p.rows.isEmpty()) return;
    QStringList columns{QString()};
    columns << extra;
    for (const Result& r : p.rows) {
        for (const QString& c : model::searchColumns(r.item)) {
            if (!columns.contains(c)) columns << c;
        }
    }
    p.filterColumns = columns;
    for (QWidget* row : p.filterRows) {
        auto* box = row->findChild<QComboBox*>(QStringLiteral("column"));
        const QString current = box->currentData().toString();
        box->clear();
        for (const QString& c : columns) box->addItem(c.isEmpty() ? tr("Any Column") : c, c);
        box->setCurrentIndex(std::max(box->findData(current), 0));
    }
}

void FindWindow::setPhraseFind(PhraseFindSource source) {
    phrase_ = std::move(source);
    refreshIndexStatus();
}

// PhraseFind AI (p.437): the bins of the search's scope (the project's, or the current bin), their master clips,
// subclips and groups searched in their transcripts. Ours: the search is quick enough to need no Cancel.
int FindWindow::phraseFind() {
    tabs_->setCurrentIndex(ClipsTab);
    Page& p = page(ClipsTab);
    const QString text = p.find->currentText().trimmed();
    if (!text.isEmpty() && p.find->findText(text) < 0) p.find->insertItem(0, text);
    p.rows.clear();
    p.table->clearContents();
    p.table->setRowCount(0);
    QList<model::FindBin> bins;
    if (scope_->currentIndex() == static_cast<int>(Scope::CurrentBin)) {
        if (BinDocument* doc = bins_.activeDocument()) bins.append({doc->nodeId(), doc->name(), doc->bin()});
    } else {
        bins = projectBins();
    }
    const auto hits = model::findPhrase(bins, phrase_.transcriptFor, text, filters(ClipsTab), options());
    fillPhrases(hits);
    lastPhrase_ = true;
    populateFilters(ClipsTab, {QStringLiteral("Score"), QStringLiteral("Transcription")});
    refreshIndexStatus();
    const int found = static_cast<int>(hits.size());
    found_->setText(tr("Found: %1").arg(found));
    return found;
}

// "The Score and Transcription columns always display in PhraseFind AI" (p.439), after Name and Bin ("The Icon, Name
// and Bin columns always display").
void FindWindow::fillPhrases(const QList<model::PhraseHit>& found) {
    Page& p = page(ClipsTab);
    QStringList headers{tr("Name"), tr("Bin"), tr("Score"), tr("Transcription")};
    headers << columns_;
    p.table->setColumnCount(static_cast<int>(headers.size()));
    p.table->setHorizontalHeaderLabels(headers);
    p.table->setRowCount(static_cast<int>(found.size()));
    for (int r = 0; r < found.size(); ++r) {
        const model::PhraseHit& h = found.at(r);
        p.rows << Result{h.binId, h.item, h.frame, true};
        p.table->setItem(r, 0, new QTableWidgetItem(h.item.name));
        p.table->setItem(r, 1, new QTableWidgetItem(h.binName));
        auto* score = new QTableWidgetItem;
        score->setData(Qt::DisplayRole, h.score);
        p.table->setItem(r, 2, score);
        p.table->setItem(r, 3, new QTableWidgetItem(h.text));
        for (int c = 0; c < columns_.size(); ++c) p.table->setItem(r, c + 4, new QTableWidgetItem(model::cellText(h.item, columns_.at(c))));
    }
}

void FindWindow::refreshIndexStatus() {
    const auto [done, total] = phrase_.indexStatus ? phrase_.indexStatus() : std::pair{0, 0};
    // Nothing to transcribe reads as indexed.
    phraseIndex_->setRange(0, std::max(total, 1));
    phraseIndex_->setValue(total > 0 ? std::min(done, total) : 1);
    const bool stopped = phrase_.indexing && !phrase_.indexing();
    phraseIndex_->setToolTip(total > 0 ? tr("%1 of %2 clips indexed%3").arg(done).arg(total).arg(stopped ? tr(" (indexing stopped)") : QString())
                                       : tr("Nothing to index"));
}

// The Settings button (p.441): "change the location of the Search Data Directory, stop or start the Find or PhraseFind
// AI indexing, or Manage Transcripts". Ours: the transcripts' folder is the store's (LUMENIS_TRANSCRIPTS), shown dimmed.
QMenu* FindWindow::settingsMenu(QWidget* parent) {
    auto* menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("searchSettingsMenu"));
    const bool running = !phrase_.indexing || phrase_.indexing();
    QAction* toggle = menu->addAction(running ? tr("Stop PhraseFind AI Indexing") : tr("Start PhraseFind AI Indexing"));
    toggle->setObjectName(QStringLiteral("toggleIndexing"));
    toggle->setEnabled(static_cast<bool>(phrase_.setIndexing));
    connect(toggle, &QAction::triggered, this, [this, running] {
        if (phrase_.setIndexing) phrase_.setIndexing(!running);
        refreshIndexStatus();
    });
    menu->addAction(tr("Search Data Directory: %1").arg(model::TranscriptStore::defaultFolder()))->setEnabled(false);
    QAction* manage = menu->addAction(tr("Manage Transcripts..."));
    manage->setObjectName(QStringLiteral("manageTranscripts"));
    manage->setEnabled(static_cast<bool>(phrase_.manageTranscripts));
    connect(manage, &QAction::triggered, this, [this] {
        if (phrase_.manageTranscripts) phrase_.manageTranscripts();
    });
    return menu;
}

QString FindWindow::foundText() const {
    return found_->text();
}

void FindWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refreshIndexStatus();
}

void FindWindow::fillItems(const QList<model::ItemFound>& found) {
    Page& p = page(ClipsTab);
    QStringList headers{tr("Name"), tr("Bin")};
    headers << columns_;
    p.table->setColumnCount(static_cast<int>(headers.size()));
    p.table->setHorizontalHeaderLabels(headers);
    p.table->setRowCount(static_cast<int>(found.size()));
    for (int r = 0; r < found.size(); ++r) {
        const model::ItemFound& f = found.at(r);
        p.rows << Result{f.binId, f.item, -1};
        p.table->setItem(r, 0, new QTableWidgetItem(f.item.name));
        p.table->setItem(r, 1, new QTableWidgetItem(f.binName));
        for (int c = 0; c < columns_.size(); ++c) p.table->setItem(r, c + 2, new QTableWidgetItem(model::cellText(f.item, columns_.at(c))));
    }
}

void FindWindow::fillMarkers(const QList<model::MarkerFound>& found) {
    Page& p = page(MarkersTab);
    const QStringList headers{tr("Name"), tr("Bin"), tr("Marker Name"), tr("Color"), tr("Track"), tr("Position"), tr("Comment"), tr("User")};
    p.table->setColumnCount(static_cast<int>(headers.size()));
    p.table->setHorizontalHeaderLabels(headers);
    p.table->setRowCount(static_cast<int>(found.size()));
    for (int r = 0; r < found.size(); ++r) {
        const model::MarkerFound& f = found.at(r);
        p.rows << Result{f.binId, f.item, f.marker.frame};
        const QStringList cells{f.item.name, f.binName, f.marker.name, f.marker.color, f.marker.track,
                                timecode(f.marker.frame, f.item), f.marker.comment, f.marker.user};
        for (int c = 0; c < cells.size(); ++c) p.table->setItem(r, c, new QTableWidgetItem(cells.at(c)));
    }
}

// "Markers: The system searches for clips in the Timeline that contain the marker text ... Clip Names:
// ... the clip name ... Timeline Text: ... Timeline text based on what is displayed in the Clip Text
// pulldown menu" (p.430). Rows as in the picture on p.431: Clip name, Track, Start, End, Duration,
// Comments, Position.
int FindWindow::fillTimeline(const QString& text) {
    Page& p = page(TimelineTab);
    const QStringList headers{tr("Clip name"), tr("Track"), tr("Start"), tr("End"), tr("Duration"), tr("Comments"), tr("Position")};
    p.table->setColumnCount(static_cast<int>(headers.size()));
    p.table->setHorizontalHeaderLabels(headers);
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    const model::LoadedClip* rec = record.current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return 0;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const model::TextFindOptions opts = options();
    struct Row {
        QString clip, track;
        std::int64_t start = 0, end = 0;
        QString comment;
        std::int64_t position = 0;
    };
    QList<Row> rows;
    auto segmentAt = [&seq](const QString& track, std::int64_t frame, std::int64_t* start) -> const model::Segment* {
        const model::Track* t = seq.track(track);
        if (!t) return nullptr;
        const int i = t->segmentAt(frame, start);
        return i >= 0 ? &t->segments.at(i) : nullptr;
    };
    if (markers_->isChecked()) {
        for (const model::Marker& m : model::markersOf(rec->item)) {
            if (!model::textMatches(m.comment, text, opts)) continue;
            std::int64_t start = 0;
            const model::Segment* s = segmentAt(m.track, m.frame, &start);
            rows.append({s ? s->name : QString(), m.track, s ? start : m.frame, s ? start + s->length : m.end(), m.comment, m.frame});
        }
    }
    for (const model::Track& t : seq.tracks()) {
        std::int64_t at = 0;
        for (const model::Segment& s : t.segments) {
            const bool name = clipNames_->isChecked() && !s.isFiller() && model::textMatches(s.name, text, opts);
            const bool clipText = timelineText_->isChecked() && !s.isFiller() && model::textMatches(timeline_.clipText(t, s), text, opts);
            if (name || clipText) rows.append({s.name, t.name(), at, at + s.length, s.note, at});
            at += s.length;
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.position < b.position; });
    p.table->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < rows.size(); ++r) {
        const Row& row = rows.at(r);
        p.rows << Result{rec->binId, rec->item, row.position};
        const QStringList cells{row.clip, row.track, timecode(row.start, rec->item), timecode(row.end, rec->item),
                                record.durationText(row.end - row.start), row.comment, QString::number(row.position)};
        for (int c = 0; c < cells.size(); ++c) p.table->setItem(r, c, new QTableWidgetItem(cells.at(c)));
    }
    return static_cast<int>(rows.size());
}

// Clips and Sequences: into the Source monitor with Load into monitor, else selected in its bin. Markers:
// "load the respective master clip or sequence into the source monitor and position the blue bar at the
// selected Marker" (p.418). Timeline: "The blue position bar moves to the location" (p.431).
void FindWindow::openResult(int row) {
    const int tab = tabs_->currentIndex();
    Page& p = page(tab);
    if (row < 0 || row >= p.rows.size()) return;
    p.table->selectRow(row);
    const Result& r = p.rows.at(row);
    if (tab == TimelineTab) {
        composer_.setActiveMonitor(MonitorId::Record);
        composer_.moveTo(MonitorId::Record, r.frame);
        return;
    }
    if (tab == ClipsTab && !loadIntoMonitor_->isChecked()) {
        emit showInBin(r.binId, r.item.id);
        return;
    }
    composer_.load(MonitorId::Source, r.binId, r.item);
    if (r.phrase) {
        // "places the blue position bar on the frame immediately before the dialogue starts. A Mark IN point displays
        // on the frame immediately preceding the audio dialogue, the bin opens and the clip highlights inside the bin"
        // (p.437).
        const std::int64_t in = r.frame > 0 ? r.frame - 1 : 0;
        model::MonitorState& s = composer_.state(MonitorId::Source);
        s.clearBoth();
        s.setPosition(in);
        s.markIn();
        composer_.moveTo(MonitorId::Source, in);
        emit showInBin(r.binId, r.item.id);
        return;
    }
    if (r.frame >= 0) composer_.moveTo(MonitorId::Source, r.frame);
}

bool FindWindow::findNext() {
    const int tab = tabs_->currentIndex();
    if (tab == ClipsTab && scope_->currentIndex() == static_cast<int>(Scope::CurrentBin)) {
        BinDocument* doc = bins_.document(currentBin_);
        BinView* view = doc ? bins_.viewFor(doc) : nullptr;
        if (!view || currentMatches_.isEmpty()) return false;
        currentIndex_ = (currentIndex_ + 1) % static_cast<int>(currentMatches_.size());
        view->selectIds({currentMatches_.at(currentIndex_)});
        return true;
    }
    Page& p = page(tab);
    if (p.rows.isEmpty()) return false;
    const auto n = static_cast<int>(p.rows.size());
    int row = p.table->currentRow();
    // "Ctrl+G always moves the blue bar to the next position that is not the current one" (p.431).
    const std::int64_t here = composer_.state(MonitorId::Record).position();
    for (int step = 1; step <= n; ++step) {
        const int next = (row + step) % n;
        if (tab == TimelineTab && p.rows.at(next).frame == here) continue;
        p.table->setCurrentCell(next, 0);
        openResult(next);
        return true;
    }
    return false;
}

void FindWindow::setResultColumns(const QStringList& headings) {
    columns_ = headings;
}

// Select Columns (p.417-418, p.429): the bin headings to show after Name and Bin.
void FindWindow::chooseColumns() {
    std::optional<QStringList> chosen;
    if (columnsAsk_) {
        chosen = columnsAsk_(columns_);
    } else {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Select Columns"));
        auto* v = new QVBoxLayout(&dialog);
        auto* list = new QListWidget(&dialog);
        for (const QString& h : model::binHeadings()) {
            if (h == QLatin1String("Name")) continue;
            auto* item = new QListWidgetItem(h, list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(columns_.contains(h) ? Qt::Checked : Qt::Unchecked);
        }
        v->addWidget(list);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        v->addWidget(buttons);
        if (dialog.exec() == QDialog::Accepted) {
            QStringList picked;
            for (int i = 0; i < list->count(); ++i) {
                if (list->item(i)->checkState() == Qt::Checked) picked << list->item(i)->text();
            }
            chosen = picked;
        }
    }
    if (!chosen) return;
    columns_ = *chosen;
    if (!page(ClipsTab).rows.isEmpty()) lastPhrase_ ? phraseFind() : find();
}

} // namespace lumenis::ui
