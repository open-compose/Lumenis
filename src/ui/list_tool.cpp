#include "lumenis/ui/list_tool.h"

#include "lumenis/interop/otio.h"
#include "lumenis/model/sequence.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <set>

// The List Tool (list_tool.h): its controls, laid out as the pictures on p.945 and p.947 show them,
// and the list made from them with interop::edl::exportText (src/interop/edl.*), or the OTIO file
// with interop::otio::exportJson (src/interop/otio.*, p.969). MainWindow creates
// it as a tool panel and answers Load with the Record monitor's sequence (main_window.cpp).

namespace lumenis::ui {

namespace {

// A fixed-width font whose family the system has. A family it lacks makes Qt build its family aliases
// when the font is first used, a quarter of a second on macOS with the window stalled, which held back
// playback (tst_playback; BUILDING.md). Asking for "Monospace" did that, and so does the system fixed
// font where the platform only names the generic family (the offscreen platform the tests use on
// macOS). QFontDatabase::hasFamily resolves aliases itself, so the families are looked up directly.
QFont fixedFont() {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QStringList families = QFontDatabase::families();
    if (!families.contains(font.family(), Qt::CaseInsensitive)) {
        for (const char* name : {"Menlo", "Consolas", "DejaVu Sans Mono", "Liberation Mono", "Courier New", "Courier"}) {
            if (families.contains(QLatin1String(name), Qt::CaseInsensitive)) {
                font.setFamily(QLatin1String(name));
                break;
            }
        }
    }
    font.setStyleHint(QFont::TypeWriter);
    return font;
}

} // namespace

namespace {

// The Output Format menu (p.945 picture, p.947 step 2): the EDL types, then the cut and change list
// types, then OTIO ("choose OTIO", p.969). CMX_3600, File_129 and OTIO are written so far.
struct OutputFormat {
    const char* name;
    bool works;
};
constexpr OutputFormat kFormats[] = {
    {"CMX_3600", true},    {"CMX_DigitalCut", false}, {"CMX_Transfer", false}, {"Cuedos", false},
    {"File_16", false},    {"File_32", false},        {"File_129", true},      {"Columnar", false},
    {"TabbedLists", false}, {"WebLists", false},      {"XML", false},            {"OTIO", true},
};

constexpr const char* kOtio = "OTIO";

void dim(QComboBox* box, int index) {
    if (auto* model = qobject_cast<QStandardItemModel*>(box->model())) {
        if (QStandardItem* item = model->item(index)) item->setEnabled(false);
    }
}

// A check box for an option the guide lists (p.958) that is not carried out yet: shown, dimmed.
QCheckBox* later(const QString& text, QWidget* parent) {
    auto* box = new QCheckBox(text, parent);
    box->setEnabled(false);
    return box;
}

// A menu with its first entry chosen and the rest dimmed (Formatting options not carried out).
QComboBox* menu(const QStringList& entries, QWidget* parent, bool firstWorks = true) {
    auto* box = new QComboBox(parent);
    box->addItems(entries);
    for (int i = firstWorks ? 1 : 0; i < entries.size(); ++i) dim(box, i);
    if (!firstWorks) box->setEnabled(false);
    return box;
}

} // namespace

ListTool::ListTool(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("listTool"));
    auto* outer = new QHBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    outer->addWidget(split);

    // The left side: setting, format, tabs (p.945).
    auto* left = new QWidget(split);
    auto* leftLayout = new QVBoxLayout(left);
    auto* top = new QFormLayout;
    // Two settings to recall (p.956): Default EDL and "Default OpenTimelineIO" (p.969 note), which
    // goes with the OTIO output format. Save As and Remove Current (p.956-957) come later.
    setting_ = new QComboBox(left);
    setting_->setObjectName(QStringLiteral("listActiveSetting"));
    setting_->addItem(tr("Default EDL"));
    setting_->addItem(tr("Default OpenTimelineIO"));
    top->addRow(tr("Active Setting:"), setting_);
    format_ = new QComboBox(left);
    format_->setObjectName(QStringLiteral("listOutputFormat"));
    for (int i = 0; i < static_cast<int>(std::size(kFormats)); ++i) {
        format_->addItem(QString::fromLatin1(kFormats[i].name));
        if (!kFormats[i].works) dim(format_, i);
    }
    top->addRow(tr("Output Format:"), format_);
    auto* kind = new QHBoxLayout;
    auto* list = new QRadioButton(tr("List"), left);
    list->setChecked(true);
    auto* change = new QRadioButton(tr("Change List"), left);
    change->setEnabled(false);
    kind->addWidget(list);
    kind->addWidget(change);
    kind->addStretch();
    top->addRow(QString(), kind);
    leftLayout->addLayout(top);

    auto* tabs = new QTabWidget(left);
    // Input (p.945): the sequence with Load and Clear, the tracks, the Title.
    auto* input = new QWidget(tabs);
    auto* inputLayout = new QVBoxLayout(input);
    inputLayout->addWidget(new QLabel(tr("Sequence:"), input));
    auto* seqRow = new QHBoxLayout;
    sequenceLabel_ = new QLabel(tr("Drop sequence or press load"), input);
    sequenceLabel_->setObjectName(QStringLiteral("listSequence"));
    sequenceLabel_->setFrameShape(QFrame::StyledPanel);
    load_ = new QPushButton(tr("Load"), input);
    load_->setObjectName(QStringLiteral("listLoad"));
    clear_ = new QPushButton(tr("Clear"), input);
    seqRow->addWidget(sequenceLabel_, 1);
    seqRow->addWidget(load_);
    seqRow->addWidget(clear_);
    inputLayout->addLayout(seqRow);
    inputLayout->addWidget(new QLabel(tr("Tracks:"), input));
    auto* scroll = new QScrollArea(input);
    scroll->setWidgetResizable(true);
    tracks_ = new QWidget(scroll);
    trackRow_ = new QHBoxLayout(tracks_);
    trackRow_->addStretch();
    scroll->setWidget(tracks_);
    inputLayout->addWidget(scroll, 1);
    auto* titleRow = new QFormLayout;
    title_ = new QLineEdit(input);
    title_->setObjectName(QStringLiteral("listTitle"));
    titleRow->addRow(tr("Title"), title_);
    inputLayout->addLayout(titleRow);
    tabs->addTab(input, tr("Input"));

    // List Options (p.958): Picture, Sound, and Both Picture and Sound.
    auto* options = new QWidget(tabs);
    auto* optionsLayout = new QVBoxLayout(options);
    auto* picture = new QGroupBox(tr("Picture"), options);
    auto* pictureLayout = new QVBoxLayout(picture);
    pictureDissolves_ = new QCheckBox(tr("Dissolves"), picture);
    pictureDissolves_->setChecked(true);
    pictureLayout->addWidget(pictureDissolves_);
    pictureLayout->addWidget(later(tr("Other Transitions"), picture));
    blackEdits_ = new QCheckBox(tr("Black Edits"), picture);
    pictureLayout->addWidget(blackEdits_);
    for (const QString& t : {tr("Spatial Adapters"), tr("Temporal Adapters"), tr("Color Effects"), tr("Other Segment Effects"),
                             tr("Color Decision Lists")}) {
        pictureLayout->addWidget(later(t, picture));
    }
    optionsLayout->addWidget(picture);
    auto* sound = new QGroupBox(tr("Sound"), options);
    auto* soundLayout = new QVBoxLayout(sound);
    soundDissolves_ = new QCheckBox(tr("Dissolves"), sound);
    soundDissolves_->setChecked(true);
    soundLayout->addWidget(soundDissolves_);
    for (const QString& t : {tr("Volume"), tr("Track Patching"), tr("EQ Effects")}) soundLayout->addWidget(later(t, sound));
    optionsLayout->addWidget(sound);
    auto* both = new QGroupBox(tr("Both Picture and Sound"), options);
    auto* bothLayout = new QVBoxLayout(both);
    clipNames_ = new QCheckBox(tr("Clip Names"), both);
    clipNames_->setChecked(true);
    bothLayout->addWidget(clipNames_);
    bothLayout->addWidget(later(tr("Source File Name"), both));
    markers_ = new QCheckBox(tr("Markers"), both);
    markers_->setChecked(true);
    bothLayout->addWidget(markers_);
    for (const QString& t : {tr("All Markers at End"), tr("Clip Comments"), tr("Reel Names"), tr("Frame Count")}) {
        bothLayout->addWidget(later(t, both));
    }
    optionsLayout->addWidget(both);
    optionsLayout->addStretch();
    auto* optionsScroll = new QScrollArea(tabs);
    optionsScroll->setWidgetResizable(true);
    optionsScroll->setWidget(options);
    tabs->addTab(optionsScroll, tr("List Options"));

    // Formatting (p.958-960).
    auto* formatting = new QWidget(tabs);
    auto* formLayout = new QFormLayout(formatting);
    optimize_ = new QCheckBox(tr("Optimize EDL"), formatting);
    formLayout->addRow(optimize_);
    // S3D Contributor (p.959): Monoscopic, S3D Left, S3D Right.
    s3dContributor_ = new QComboBox(formatting);
    s3dContributor_->setObjectName(QStringLiteral("listS3DContributor"));
    s3dContributor_->addItems({tr("Monoscopic"), tr("S3D Left"), tr("S3D Right")});
    formLayout->addRow(tr("S3D Contributor"), s3dContributor_);
    formLayout->addRow(tr("Source TC"), menu({tr("Start"), tr("VITC"), tr("Sound_TC"), tr("Film_TC"), tr("TC24")}, formatting));
    formLayout->addRow(tr("Record TC"), menu({tr("TC1")}, formatting));
    formLayout->addRow(tr("Reel ID Type"), menu({tr("Tape"), tr("Sound_Roll"), tr("Camera_Roll"), tr("Lab_Roll"), tr("Disk_Label")}, formatting));
    formLayout->addRow(tr("Sort Mode"), menu({tr("A (Record In)"), tr("B (Source, Record In)"), tr("C (Source, Source In)"),
                                              tr("D (Source, Record In, Effects at End)"), tr("E (Source, Source In, Effects at End)"),
                                              tr("S (Source Start)")},
                                             formatting));
    startEvent_ = new QSpinBox(formatting);
    startEvent_->setObjectName(QStringLiteral("listStartEvent"));
    startEvent_->setRange(0, 9999);
    startEvent_->setValue(1);
    formLayout->addRow(tr("Starting Event Number"), startEvent_);
    formLayout->addRow(tr("Tape or File Name truncation"), menu({tr("End"), tr("Middle"), tr("Beginning"), tr("First_N")}, formatting));
    formLayout->addRow(later(tr("Convert tape names to numbers"), formatting));
    formLayout->addRow(later(tr("Include source table with saved EDL"), formatting));
    tabs->addTab(formatting, tr("Formatting"));
    leftLayout->addWidget(tabs, 1);

    // The right side: the buttons and the list (p.946-947).
    auto* right = new QWidget(split);
    auto* rightLayout = new QVBoxLayout(right);
    auto* buttons = new QHBoxLayout;
    preview_ = new QPushButton(tr("Preview"), right);
    preview_->setObjectName(QStringLiteral("listPreview"));
    save_ = new QPushButton(tr("Save List"), right);
    save_->setObjectName(QStringLiteral("listSave"));
    clearList_ = new QPushButton(tr("Clear List"), right);
    auto* newList = new QPushButton(tr("New List"), right);
    newList->setEnabled(false);
    auto* cloneList = new QPushButton(tr("Clone List"), right);
    cloneList->setEnabled(false);
    for (QPushButton* b : {preview_, save_}) buttons->addWidget(b);
    buttons->addStretch();
    for (QPushButton* b : {clearList_, newList, cloneList}) buttons->addWidget(b);
    rightLayout->addLayout(buttons);
    listTabs_ = new QTabWidget(right);
    master_ = new QPlainTextEdit(listTabs_);
    master_->setObjectName(QStringLiteral("listMaster"));
    master_->setReadOnly(true);
    master_->setLineWrapMode(QPlainTextEdit::NoWrap);
    const QFont mono = fixedFont();
    master_->setFont(mono);
    sources_ = new QPlainTextEdit(listTabs_);
    sources_->setObjectName(QStringLiteral("listSources"));
    sources_->setReadOnly(true);
    sources_->setLineWrapMode(QPlainTextEdit::NoWrap);
    sources_->setFont(mono);
    listTabs_->addTab(master_, tr("Master EDL"));
    listTabs_->addTab(sources_, tr("Sources"));
    rightLayout->addWidget(listTabs_, 1);
    split->setStretchFactor(1, 1);

    // The setting and the format follow each other: Default OpenTimelineIO is the OTIO format, and
    // choosing OTIO makes it the active setting ("current user profiles can create one by setting
    // the output format to OTIO", p.969).
    connect(setting_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QSignalBlocker block(format_);
        if (index == 1) {
            format_->setCurrentText(QString::fromLatin1(kOtio));
        } else if (isOtio()) {
            format_->setCurrentIndex(0);
        }
        formatChanged();
    });
    connect(format_, &QComboBox::currentIndexChanged, this, [this] {
        const QSignalBlocker block(setting_);
        setting_->setCurrentIndex(isOtio() ? 1 : 0);
        formatChanged();
    });
    connect(load_, &QPushButton::clicked, this, &ListTool::loadRequested);
    connect(clear_, &QPushButton::clicked, this, &ListTool::clearSequence);
    connect(preview_, &QPushButton::clicked, this, [this] { preview(); });
    connect(save_, &QPushButton::clicked, this, &ListTool::saveClicked);
    connect(clearList_, &QPushButton::clicked, this, [this] {
        master_->clear();
        sources_->clear();
        made_.clear();
    });
    clearSequence();
}

bool ListTool::isOtio() const {
    return format_->currentText() == QLatin1String(kOtio);
}

// A list made in one format is not saved as another: the next Preview or Save List makes it again.
void ListTool::formatChanged() {
    made_.clear();
    master_->clear();
    sources_->clear();
}

QStringList ListTool::listedTracks() const {
    QStringList tracks;
    for (QToolButton* b : tracks_->findChildren<QToolButton*>()) {
        if (b->isChecked()) tracks << b->text();
    }
    // No track on lists nothing, rather than everything.
    if (tracks.isEmpty()) tracks << QStringLiteral("-");
    return tracks;
}

interop::otio::ExportOptions ListTool::otioOptions() const {
    interop::otio::ExportOptions o;
    o.tracks = listedTracks();
    o.resolve = resolve_;
    return o;
}

QToolButton* ListTool::trackButton(const QString& track) const {
    return tracks_->findChild<QToolButton*>(QStringLiteral("listTrack") + track);
}

void ListTool::setSequence(const model::BinItem& sequence, Resolver resolve) {
    sequence_ = sequence;
    resolve_ = std::move(resolve);
    sequenceLabel_->setText(sequence.name);
    // "The title of the List is automatically populated based on the sequence name" (p.948).
    title_->setText(sequence.name);
    rebuildTracks();
    preview_->setEnabled(true);
    save_->setEnabled(true);
    made_.clear();
}

void ListTool::clearSequence() {
    sequence_.reset();
    resolve_ = nullptr;
    sequenceLabel_->setText(tr("Drop sequence or press load"));
    title_->clear();
    rebuildTracks();
    preview_->setEnabled(false);
    save_->setEnabled(false);
    made_.clear();
}

// "By default, all video, audio and data tracks are enabled. Click to enable/disable the tracks"
// (p.948): one toggle button for each of the sequence's tracks.
void ListTool::rebuildTracks() {
    for (QToolButton* b : tracks_->findChildren<QToolButton*>()) delete b;
    if (!sequence_) return;
    const model::Sequence s = model::Sequence::fromItem(*sequence_);
    int at = 0;
    for (const QString& name : s.trackNames()) {
        auto* b = new QToolButton(tracks_);
        b->setObjectName(QStringLiteral("listTrack") + name);
        b->setText(name);
        b->setCheckable(true);
        b->setChecked(true);
        trackRow_->insertWidget(at++, b);
    }
}

interop::edl::ExportOptions ListTool::options() const {
    interop::edl::ExportOptions o;
    o.format = interop::edl::formatFromName(format_->currentText()).value_or(interop::edl::Format::Cmx3600);
    o.title = title_->text().trimmed();
    o.tracks = listedTracks();
    o.pictureDissolves = pictureDissolves_->isChecked();
    o.soundDissolves = soundDissolves_->isChecked();
    o.blackEdits = blackEdits_->isChecked();
    o.clipNames = clipNames_->isChecked();
    o.markers = markers_->isChecked();
    o.optimize = optimize_->isChecked();
    o.s3dContributor = s3dContributor_->currentIndex() == 1   ? model::S3DContributor::Left
                       : s3dContributor_->currentIndex() == 2 ? model::S3DContributor::Right
                                                              : model::S3DContributor::Monoscopic;
    o.startEvent = startEvent_->value();
    o.resolve = resolve_;
    return o;
}

QString ListTool::preview() {
    if (!sequence_) return {};
    QStringList warnings;
    if (isOtio()) {
        const interop::otio::ExportResult r = interop::otio::exportJson(*sequence_, otioOptions());
        if (!r.ok) {
            emit message(tr("List Tool: %1").arg(r.error));
            return {};
        }
        made_ = QString::fromUtf8(r.json);
        warnings = r.warnings;
    } else {
        if (!interop::edl::formatFromName(format_->currentText())) {
            emit message(tr("List Tool: %1 lists are not made yet; CMX_3600, File_129 and OTIO are.").arg(format_->currentText()));
            return {};
        }
        const interop::edl::ExportResult r = interop::edl::exportText(*sequence_, options());
        if (!r.ok) {
            emit message(tr("List Tool: %1").arg(r.error));
            return {};
        }
        made_ = r.text;
        warnings = r.warnings;
    }
    QString shown = made_;
    shown.remove(QLatin1Char('\r'));
    master_->setPlainText(shown);
    // Sources (p.950-951): a row for each reel the list names, with the source's name and MobID.
    QStringList rows;
    std::set<QString> seen;
    const model::Sequence s = model::Sequence::fromItem(*sequence_);
    const QStringList listed = listedTracks();
    for (const model::Track& t : s.tracks()) {
        if (!listed.contains(t.name())) continue;
        for (const model::Segment& seg : t.segments) {
            if (seg.isFiller()) continue;
            const auto clip = resolve_ ? resolve_(seg.sourceMobId(), seg.clipId) : std::nullopt;
            QString reel = clip ? clip->fields.value(QStringLiteral("Tape")) : QString();
            if (reel.isEmpty()) reel = clip ? clip->name : seg.name;
            const QString row = QStringLiteral("%1\t%2\t%3").arg(reel.toUpper(), clip ? clip->name : seg.name,
                                                                  QString::fromStdString(seg.sourceMobId().toString()));
            if (seen.insert(row).second) rows << row;
        }
    }
    sources_->setPlainText(rows.join(QLatin1Char('\n')));
    for (const QString& w : warnings) emit message(tr("List Tool: %1").arg(w));
    return made_;
}

bool ListTool::saveList(const QString& path, QString* error) {
    if (made_.isEmpty() && preview().isEmpty()) {
        if (error) *error = tr("There is no list to save.");
        return false;
    }
    if (isOtio()) {
        const interop::otio::ExportResult r = interop::otio::exportFile(*sequence_, path, otioOptions());
        if (!r.ok && error) *error = r.error;
        return r.ok;
    }
    const interop::edl::ExportResult r = interop::edl::exportFile(*sequence_, path, options());
    if (!r.ok) {
        if (error) *error = r.error;
        return false;
    }
    return true;
}

// Save List (step 9; p.969 step 3 for OTIO): to one file, asked for with the system's save dialog.
void ListTool::saveClicked() {
    if (!sequence_) return;
    QString base = title_->text().trimmed().isEmpty() ? sequence_->name : title_->text().trimmed();
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString suggested = base + (isOtio() ? QStringLiteral(".otio") : QStringLiteral(".edl"));
    const QString filter = isOtio() ? tr("OpenTimelineIO files (*.otio)") : tr("Edit decision lists (*.edl)");
    const QString path = savePath ? savePath(suggested) : QFileDialog::getSaveFileName(this, tr("Save List"), suggested, filter);
    if (path.isEmpty()) return;
    QString error;
    if (saveList(path, &error)) {
        emit message(tr("List Tool: saved %1").arg(QDir::toNativeSeparators(path)));
    } else {
        emit message(tr("List Tool: %1").arg(error));
    }
}

} // namespace lumenis::ui
