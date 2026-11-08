// MainWindow's scripts (8.3; Editing Guide ch.18, docs/spec/scriptsync.md): File > New > New Script, the Script window
// panel showing a script item, Save Script, Save a Script Copy As and Script > Text Layout; the slates' clips and frames,
// loading and playing takes and the Script menu's slate options (8.3b, p.576-582); script marks, screening, Find Script,
// Find Bin and splicing a script range (8.3c, p.582-588); ScriptSync AI and Create Script from Clip (8.3d, p.569-570,
// p.584-585).
#include "lumenis/ui/main_window.h"

#include "lumenis/model/script.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/script_sync_dialog.h"
#include "lumenis/ui/script_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/tool_windows.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QSpinBox>

namespace lumenis::ui {

void MainWindow::setUpScriptWindow() {
    scriptWindow_ = new ScriptWindow;
    auto* panel = new Panel(QStringLiteral("Script"), tr("Script"), scriptWindow_);
    panels_->addPanel(panel);
    // "When you make changes in the Script window, an asterisk appears in the title bar" (p.570).
    connect(scriptWindow_, &ScriptWindow::titleChanged, panel, &Panel::setTitle);
    // Clips dragged from bins: "Select the portion of the script that is covered by the first clip or clips" (p.576).
    // The Take column gives the tabs their numbers (p.580). Scripts and sequences are not takes (ours).
    scriptWindow_->setClipResolver([this](const QString& binId, const QString& itemId) -> std::optional<model::ScriptClip> {
        const BinDocument* doc = binContainer_->document(binId);
        const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr;
        if (!item || item->kind == model::BinItemKind::Script || item->kind == model::BinItemKind::Sequence) return std::nullopt;
        return model::ScriptClip{binId, itemId, item->name, item->fields.value(QStringLiteral("Take"))};
    });
    scriptWindow_->setFrameProvider([this](const model::ScriptTake& take, QSize size) -> QImage {
        const model::BinItem* item = scriptTakeItem(take);
        return item && frames_ ? frames_->frame(*item, take.frame, size) : QImage();
    });
    connect(scriptWindow_, &ScriptWindow::takeLoadRequested, this, &MainWindow::loadScriptTake);
    connect(scriptWindow_, &ScriptWindow::takesPlayRequested, this, [this](const QList<model::ScriptTake>& takes) {
        // "The clip loads and plays back in a continuous loop until you press the space bar. If you select more than
        // one take, each take plays in sequence" (p.581).
        QList<Composer::LoopClip> clips;
        for (const model::ScriptTake& t : takes)
            if (const model::BinItem* item = scriptTakeItem(t)) clips << Composer::LoopClip{t.binId, *item};
        if (!clips.isEmpty()) composer_->playClipLoop(clips, true);
    });
    // Script marks (8.3c). The playing clip's position while screening.
    scriptWindow_->setSourcePosition([this]() -> std::optional<std::pair<QString, std::int64_t>> {
        const model::LoadedClip* c = composer_->state(MonitorId::Source).current();
        if (!c) return std::nullopt;
        return std::pair{c->item.id, c->position};
    });
    // "Click the Record button ... Media Composer loads the clip into the Source monitor, and the clip begins to play ... As
    // each take reaches its end, Media Composer automatically loads and plays the next take ... To stop the playback
    // loop: Press the space bar" (p.584): the Composer's clip loop, Tab and Shift+Tab included.
    connect(scriptWindow_, &ScriptWindow::recordRequested, this, [this](const QList<model::ScriptTake>& takes) {
        QList<Composer::LoopClip> clips;
        for (const model::ScriptTake& t : takes)
            if (const model::BinItem* item = scriptTakeItem(t)) clips << Composer::LoopClip{t.binId, *item};
        if (clips.isEmpty()) {
            scriptWindow_->setRecordingTake({});
            return;
        }
        scriptRecordTakes_ = takes;
        composer_->playClipLoop(clips, true);
        followScriptRecording();
    });
    connect(composer_, &Composer::changed, this, &MainWindow::followScriptRecording);
    // "When the playback reaches the selected line of dialog, click the Add Script Mark button ... The line is marked in the
    // Script window with a small horizontal bar, and play stops" (p.583).
    connect(scriptWindow_, &ScriptWindow::scriptMarkAdded, this, [this] {
        if (!scriptWindow_->isRecording()) composer_->stop();
    });
    connect(scriptWindow_, &ScriptWindow::findBinRequested, this, &MainWindow::findScriptBins);
    connect(scriptWindow_, &ScriptWindow::spliceRequested, this, &MainWindow::spliceScriptRange);
    connect(scriptWindow_, &ScriptWindow::takeInfoRequested, this, [this](const model::ScriptTake& take) {
        // Ours: the Info window (p.572) is not built; its clip's details go to the Console.
        const model::BinItem* item = scriptTakeItem(take);
        if (!item) {
            ConsoleLog::instance().append(tr("Script: the clip of take %1 is not in its bin.").arg(take.clipName));
            return;
        }
        const BinDocument* doc = binContainer_->document(take.binId);
        ConsoleLog::instance().append(tr("Script take: %1, bin %2, take %3, lines %4-%5, %6 frames.")
                                          .arg(item->name, doc ? doc->name() : take.binId, take.take)
                                          .arg(take.first + 1)
                                          .arg(take.last + 1)
                                          .arg(item->durationFrames));
    });
}

const model::BinItem* MainWindow::scriptTakeItem(const model::ScriptTake& take) const {
    const BinDocument* doc = binContainer_->document(take.binId);
    return doc ? doc->bin().item(take.clipId) : nullptr;
}

// "Double-click any Takes tab" (p.581) and "Double-click any line in the take to select the take and load it into a
// monitor" (p.580). "When you set Interpolate Position, Media Composer matches the length of the take in the script to
// the length of the clip in the Source monitor ... wherever you double-click in the take" (p.577).
bool MainWindow::loadScriptTake(const model::ScriptTake& take, int line, double fraction) {
    const model::BinItem* item = scriptTakeItem(take);
    if (!item) {
        ConsoleLog::instance().append(tr("Script: the clip of take %1 is not in its bin.").arg(take.clipName));
        return false;
    }
    const model::BinItem clip = *item;
    composer_->load(MonitorId::Source, take.binId, clip);
    if (line >= 0 && take.marks.contains(line)) {
        // "Double-click the script mark ... Media Composer loads the take into the Source monitor, cues it to the synced
        // line of dialog, and places an IN point at the sync location" (p.585); "The pre-roll is included when you access
        // the script mark" (p.589).
        const std::int64_t in = take.marks.value(line);
        model::MonitorState& s = composer_->state(MonitorId::Source);
        s.clearBoth();
        s.setPosition(in);
        s.markIn();
        composer_->moveTo(MonitorId::Source, std::max<std::int64_t>(0, in - scriptPreRollFrames()));
        return true;
    }
    const std::int64_t frame = line >= 0 && scriptWindow_->slateOptions().interpolatePosition
                                   ? model::interpolatedFrame(take, line, fraction, clip.durationFrames)
                                   : take.frame;
    composer_->moveTo(MonitorId::Source, frame);
    return true;
}

// The Script setting's Pre-roll (seconds) in frames at the project's rate.
std::int64_t MainWindow::scriptPreRollFrames() const {
    const int seconds = settings_->value(QStringLiteral("Script"), QStringLiteral("preRoll")).toInt();
    const Rational rate = project_ ? project_->settings.format.editRate : Rational(25);
    return static_cast<std::int64_t>(std::llround(seconds * rate.toDouble()));
}

// The take screening plays now: the Source monitor's clip among the Record button's takes; none once the loop stopped.
void MainWindow::followScriptRecording() {
    if (!scriptWindow_->isRecording()) return;
    if (!composer_->isClipLooping()) {
        scriptWindow_->setRecordingTake({});
        scriptRecordTakes_.clear();
        return;
    }
    const model::LoadedClip* c = composer_->state(MonitorId::Source).current();
    for (const model::ScriptTake& t : std::as_const(scriptRecordTakes_)) {
        if (c && t.clipId == c->item.id) {
            scriptWindow_->setRecordingTake(t.id);
            return;
        }
    }
}

// Add Script Mark (the Other tab, p.582-583): the Script window's selected line of the take last loaded, synced to the
// Source monitor's position when it shows that take's clip.
bool MainWindow::addScriptMark() {
    if (!scriptWindow_->isOpen()) return false;
    const model::ScriptTake* take = model::takeOf(scriptWindow_->script(), scriptWindow_->markTake());
    const model::LoadedClip* c = composer_->state(MonitorId::Source).current();
    if (!take || !c || c->item.id != take->clipId) {
        ConsoleLog::instance().append(tr("Add Script Mark: load a take from the Script window first."));
        return false;
    }
    return scriptWindow_->addScriptMark(c->position);
}

// Find Script (the Other tab, p.586): "The Script window scrolls to and highlights the portion of script that most
// closely matches the clip location".
bool MainWindow::findScript() {
    const model::LoadedClip* c = composer_->state(MonitorId::Source).current();
    if (!scriptWindow_->isOpen() || !c) return false;
    const model::Script script = scriptWindow_->script();
    for (const model::ScriptSlate& s : script.slates) {
        for (const model::ScriptTake& t : s.takes) {
            if (t.clipId != c->item.id) continue;
            if (const auto lines = model::scriptLinesAt(t, c->position)) {
                scriptWindow_->showTakeLines(t.id, lines->first, lines->second);
                if (Panel* p = panels_->panel(QStringLiteral("Script"))) panels_->showPanel(p);
                return true;
            }
        }
    }
    ConsoleLog::instance().append(tr("Find Script: no script mark in the open script matches the clip's position."));
    return false;
}

// Find Bin (p.586-587): "Media Composer searches through bins linked to the project, opens the bin containing the linked
// clips, and highlights them in the bin."
int MainWindow::findScriptBins(const QList<model::ScriptTake>& takes) {
    QHash<QString, QStringList> byBin;
    for (const model::ScriptTake& t : takes)
        if (scriptTakeItem(t)) byBin[t.binId] << t.clipId;
    int found = 0;
    for (auto it = byBin.cbegin(); it != byBin.cend(); ++it) {
        BinDocument* doc = binContainer_->openBin(it.key());
        BinView* view = doc ? binContainer_->viewFor(doc) : nullptr;
        if (!view) continue;
        view->selectIds(it.value());
        found += static_cast<int>(it.value().size());
    }
    if (found == 0) ConsoleLog::instance().append(tr("Find Bin: the takes' clips are not in their bins."));
    return found;
}

// Splicing a Script Range (p.588): the marked section of the take's clip over script lines [first, last] spliced into the
// sequence at the Record monitor's IN or position.
bool MainWindow::spliceScriptRange(const model::ScriptTake& take, int first, int last) {
    const model::BinItem* item = scriptTakeItem(take);
    const auto frames = item ? model::markedFrames(take, first, last, item->durationFrames) : std::nullopt;
    if (!frames) {
        ConsoleLog::instance().append(tr("Splice: the take has no script marks for those lines."));
        return false;
    }
    composer_->load(MonitorId::Source, take.binId, *item);
    model::MonitorState& s = composer_->state(MonitorId::Source);
    s.clearBoth();
    s.setPosition(frames->second);
    s.markOut();
    s.setPosition(frames->first);
    s.markIn();
    composer_->moveTo(MonitorId::Source, frames->first);
    QAction* splice = commands_.action(QStringLiteral("edit.splice_in"));
    if (!splice) return false;
    splice->trigger();
    return true;
}

// The Script setting's slate options (p.1217); "If you make changes to options in the Script window or Script menu,
// those changes will not change the options in the Settings Window" (p.570).
void MainWindow::applyScriptSettings() {
    ScriptSlateOptions o = scriptWindow_->slateOptions();
    o.showFrames = settings_->flag(QStringLiteral("Script"), QStringLiteral("showFrames"));
    o.showAllTakes = settings_->flag(QStringLiteral("Script"), QStringLiteral("showAllTakes"));
    o.holdSlates = settings_->flag(QStringLiteral("Script"), QStringLiteral("holdSlates"));
    o.interpolatePosition = settings_->flag(QStringLiteral("Script"), QStringLiteral("interpolatePosition"));
    o.takeColor = settings_->value(QStringLiteral("Script"), QStringLiteral("takeColoring")).toString();
    if (!QColor::isValidColorName(o.takeColor)) o.takeColor = QStringLiteral("#3c78d2");
    scriptWindow_->setSlateOptions(o);
    refreshScriptMenu();
}

// The Script menu's check marks: "When Show Frames is enabled, a check mark appears to the left of the Show Frames
// command" (p.578), and the colour Script > Color chose (p.582).
void MainWindow::refreshScriptMenu() {
    const ScriptSlateOptions o = scriptWindow_->slateOptions();
    const std::pair<const char*, bool> checks[] = {{"script.show_frames", o.showFrames},
                                                   {"script.show_all_takes", o.showAllTakes},
                                                   {"script.hold_slates_onscreen", o.holdSlates},
                                                   {"script.interpolate_position", o.interpolatePosition}};
    for (const auto& [id, on] : checks) {
        if (QAction* a = commands_.action(QString::fromLatin1(id))) {
            a->setCheckable(true);
            a->setChecked(on);
        }
    }
    for (const auto& [id, color] : scriptIndicatorColors()) {
        if (QAction* a = commands_.action(id)) {
            a->setCheckable(true);
            a->setChecked(color.compare(scriptWindow_->indicatorColor(), Qt::CaseInsensitive) == 0);
        }
    }
}

// Script > Color > color (p.582). Ours: the guide does not list the colours.
QList<std::pair<QString, QString>> MainWindow::scriptIndicatorColors() {
    return {{QStringLiteral("script.color.red"), QStringLiteral("#d23c3c")},
            {QStringLiteral("script.color.orange"), QStringLiteral("#e08c28")},
            {QStringLiteral("script.color.yellow"), QStringLiteral("#d2c832")},
            {QStringLiteral("script.color.green"), QStringLiteral("#3cb446")},
            {QStringLiteral("script.color.blue"), QStringLiteral("#3c78d2")},
            {QStringLiteral("script.color.purple"), QStringLiteral("#8a3cc8")}};
}

void MainWindow::setScriptSlateOption(const QString& commandId) {
    ScriptSlateOptions o = scriptWindow_->slateOptions();
    if (commandId == QLatin1String("script.show_frames")) o.showFrames = !o.showFrames;
    else if (commandId == QLatin1String("script.show_all_takes")) o.showAllTakes = !o.showAllTakes;
    else if (commandId == QLatin1String("script.hold_slates_onscreen")) o.holdSlates = !o.holdSlates;
    else if (commandId == QLatin1String("script.interpolate_position")) o.interpolatePosition = !o.interpolatePosition;
    for (const auto& [id, color] : scriptIndicatorColors())
        if (id == commandId) scriptWindow_->setIndicatorColor(color);
    scriptWindow_->setSlateOptions(o);
    refreshScriptMenu();
}

// The Script setting's import options (p.1217): "The default font and font size for a script is established when you
// import the script, based on the current Script settings" (p.572), and the left margin and Word Wrap.
static model::ScriptLayout importLayout(const Settings& settings) {
    model::ScriptLayout l;
    l.font = settings.value(QStringLiteral("Script"), QStringLiteral("font")).toString();
    l.fontSize = settings.value(QStringLiteral("Script"), QStringLiteral("fontSize")).toInt(12);
    l.leftMargin = settings.value(QStringLiteral("Script"), QStringLiteral("leftMargin")).toInt(40);
    l.wordWrap = settings.flag(QStringLiteral("Script"), QStringLiteral("wordWrap"));
    return l;
}

bool MainWindow::newScript(const QString& path) {
    BinDocument* doc = binContainer_->activeDocument();
    if (!doc || doc->isReadOnly()) {
        ConsoleLog::instance().append(tr("New Script: open the bin to import the script into."));
        return false;
    }
    const QString file = !path.isEmpty() ? path
                                         : QFileDialog::getOpenFileName(this, tr("Open"), QString(), tr("Text (*.txt);;All files (*)"));
    if (file.isEmpty()) return false;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        ConsoleLog::instance().append(tr("New Script: %1 cannot be read.").arg(QDir::toNativeSeparators(file)));
        return false;
    }
    const model::Script script = model::scriptFromText(QString::fromUtf8(f.readAll()), importLayout(*settings_));
    const model::BinItem item = model::scriptItem(QFileInfo(file).completeBaseName(), script);
    doc->addItem(item);
    return openScript(doc->nodeId(), item.id);
}

bool MainWindow::openScript(const QString& binId, const QString& itemId) {
    BinDocument* doc = binContainer_->document(binId);
    const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr;
    const auto script = item ? model::scriptOf(*item) : std::nullopt;
    if (!script) return false;
    // Ours: a changed script is saved before another takes the window (bins save on their own the same way, p.570).
    if (scriptWindow_->isOpen() && scriptWindow_->isModified() && scriptWindow_->itemId() != itemId) saveScript();
    scriptWindow_->open(binId, itemId, item->name, *script);
    applyScriptSettings();
    scriptWindow_->setDisplay(settings_->flag(QStringLiteral("Script"), QStringLiteral("showLineNumbers")),
                              settings_->value(QStringLiteral("Script"), QStringLiteral("background")).toString() != QLatin1String("white"));
    if (Panel* p = panels_->panel(QStringLiteral("Script"))) panels_->showPanel(p);
    return true;
}

bool MainWindow::saveScript() {
    if (!scriptWindow_->isOpen()) return false;
    BinDocument* doc = binContainer_->document(scriptWindow_->binId());
    const model::BinItem* found = doc ? doc->bin().item(scriptWindow_->itemId()) : nullptr;
    if (!found || doc->isReadOnly()) {
        ConsoleLog::instance().append(tr("Save Script: the script's bin cannot be changed."));
        return false;
    }
    model::BinItem item = *found;
    model::setScript(item, scriptWindow_->script());
    item.modified = QDateTime::currentDateTimeUtc();
    storeClip(doc->nodeId(), item);
    scriptWindow_->markSaved();
    return true;
}

bool MainWindow::saveScriptCopyAs(const QString& name) {
    if (!scriptWindow_->isOpen()) return false;
    BinDocument* doc = binContainer_->document(scriptWindow_->binId());
    if (!doc || doc->isReadOnly()) return false;
    QString copyName = name;
    if (copyName.isEmpty()) {
        bool ok = false;
        copyName = QInputDialog::getText(this, tr("Save a Script Copy As"), tr("Name:"), QLineEdit::Normal,
                                         scriptWindow_->name() + tr(" Copy"), &ok);
        if (!ok || copyName.trimmed().isEmpty()) return false;
    }
    const model::BinItem copy = model::scriptItem(copyName.trimmed(), scriptWindow_->script());
    doc->addItem(copy);
    // The original keeps what was last saved; the copy is shown from now on.
    scriptWindow_->markSaved();
    return openScript(doc->nodeId(), copy.id);
}

// "Select Script > Text Layout. The Script Text Layout dialog box opens ... Type a new left margin size (in pixels) ...
// Type the desired text width (in pixels) ... Select the Word Wrap option" (p.571-572).
bool MainWindow::scriptTextLayout(std::optional<model::ScriptLayout> layout) {
    if (!scriptWindow_->isOpen()) return false;
    if (!layout) {
        model::ScriptLayout l = scriptWindow_->textLayout();
        QDialog d(this);
        d.setWindowTitle(tr("Script Text Layout"));
        auto* form = new QFormLayout(&d);
        auto* margin = new QSpinBox(&d);
        margin->setRange(0, 600);
        margin->setValue(l.leftMargin);
        auto* width = new QSpinBox(&d);
        width->setRange(50, 4000);
        width->setValue(l.textWidth);
        auto* wrap = new QCheckBox(tr("Word Wrap"), &d);
        wrap->setChecked(l.wordWrap);
        form->addRow(tr("Left Margin (pixels):"), margin);
        form->addRow(tr("Text Width (pixels):"), width);
        form->addRow(wrap);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
        connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        form->addRow(buttons);
        if (d.exec() != QDialog::Accepted) return false;
        l.leftMargin = margin->value();
        l.textWidth = width->value();
        l.wordWrap = wrap->isChecked();
        layout = l;
    }
    scriptWindow_->setTextLayout(*layout);
    return true;
}

// Script > ScriptSync AI (p.584-585): "Select one or more takes that include audio. Select all clips and takes in the
// Script that you want marked up. Select Script > ScriptSync AI." Ours: the transcripts of 8.2 are the analysis; clips not
// transcribed yet are queued, and ScriptSync AI is run again once they are.
int MainWindow::scriptSyncAI() {
    if (!scriptWindow_->isOpen()) return -1;
    const QList<model::ScriptTake> takes = scriptWindow_->chosenTakes();
    if (takes.isEmpty()) {
        ConsoleLog::instance().append(tr("ScriptSync AI: select the takes (or the lines) to mark up first."));
        return -1;
    }
    QStringList tracks;
    for (const model::ScriptTake& t : takes) {
        if (const model::BinItem* item = scriptTakeItem(t)) {
            for (const QString& track : model::sourceTracksOf(*item))
                if (track.startsWith(QLatin1Char('A')) && !tracks.contains(track)) tracks << track;
        }
    }
    ScriptSyncDialog dialog(tracks, scriptSyncOptions_, [this]() -> std::optional<int> {
        const auto sel = scriptWindow_->selectedLines();
        if (!sel) return std::nullopt;
        return model::indentOf(scriptWindow_->script().lines.value(sel->first));
    }, this);
    const bool accepted = scriptSyncHook_ ? scriptSyncHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!accepted) return -1;
    scriptSyncOptions_ = dialog.options();
    const QStringList chosen = dialog.tracks();
    int placed = 0;
    QStringList untranscribed;
    scriptWindow_->editScript([&](model::Script& script) {
        for (const model::ScriptTake& t : takes) {
            const model::BinItem* item = scriptTakeItem(t);
            if (!item) continue;
            const model::ShownTranscript shown = shownTranscriptOf(*item, chosen);
            for (const QString& mob : shown.notTranscribedMobs)
                if (!untranscribed.contains(mob)) untranscribed << mob;
            placed += model::scriptSyncTake(script, t.id, model::syncWords(shown), scriptSyncOptions_);
        }
        return placed > 0;
    });
    if (!untranscribed.isEmpty()) {
        transcribeNow(untranscribed);
        ConsoleLog::instance().append(tr("ScriptSync AI: %n clip(s) are being transcribed; run ScriptSync AI again when they are "
                                         "done.", nullptr, static_cast<int>(untranscribed.size())));
    }
    ConsoleLog::instance().append(tr("ScriptSync AI: %n script mark(s) placed.", nullptr, placed));
    return placed;
}

// "Right-click on one or more selected clips in a bin and choose Transcript > Create Script from Clip ... After
// transcribing your clip, the Script window appears with the text automatically aligned to your media" (p.570). Ours: the
// script item is named after the first clip with " Script" and goes into the clips' bin.
bool MainWindow::createScriptFromClips() {
    BinView* view = binContainer_->activeView();
    if (!view) return false;
    BinDocument& doc = view->document();
    QList<model::ScriptClip> clips;
    QList<model::ShownTranscript> transcripts;
    QStringList untranscribed;
    for (const QString& id : view->selectedIds()) {
        const model::BinItem* item = doc.bin().item(id);
        if (!item || item->kind == model::BinItemKind::Sequence || item->kind == model::BinItemKind::Script) continue;
        const model::ShownTranscript shown = shownTranscriptOf(*item, {});
        for (const QString& mob : shown.notTranscribedMobs)
            if (!untranscribed.contains(mob)) untranscribed << mob;
        clips << model::ScriptClip{doc.nodeId(), item->id, item->name, item->fields.value(QStringLiteral("Take"))};
        transcripts << shown;
    }
    if (!untranscribed.isEmpty()) transcribeNow(untranscribed);
    const model::Script script = model::scriptFromTranscripts(clips, transcripts, importLayout(*settings_));
    if (script.lines.isEmpty()) {
        ConsoleLog::instance().append(untranscribed.isEmpty()
                                          ? tr("Create Script from Clip: select clips with dialog first.")
                                          : tr("Create Script from Clip: the clips are being transcribed; try again when they are done."));
        return false;
    }
    if (doc.isReadOnly()) {
        ConsoleLog::instance().append(tr("Create Script from Clip: the bin cannot be changed."));
        return false;
    }
    const model::BinItem item = model::scriptItem(clips.first().name + tr(" Script"), script);
    doc.addItem(item);
    return openScript(doc.nodeId(), item.id);
}

} // namespace lumenis::ui
