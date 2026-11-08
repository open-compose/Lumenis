// MainWindow's scripts (8.3; Editing Guide ch.18, docs/spec/scriptsync.md): File > New > New Script, the Script window
// panel showing a script item, Save Script, Save a Script Copy As and Script > Text Layout; the slates' clips and frames,
// loading and playing takes and the Script menu's slate options (8.3b, p.576-582).
#include "lumenis/ui/main_window.h"

#include "lumenis/model/script.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/panels.h"
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
    const std::int64_t frame = line >= 0 && scriptWindow_->slateOptions().interpolatePosition
                                   ? model::interpolatedFrame(take, line, fraction, clip.durationFrames)
                                   : take.frame;
    composer_->moveTo(MonitorId::Source, frame);
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

} // namespace lumenis::ui
