// MainWindow's scripts (8.3; Editing Guide ch.18, docs/spec/scriptsync.md): File > New > New Script, the Script window
// panel showing a script item, Save Script, Save a Script Copy As and Script > Text Layout.
#include "lumenis/ui/main_window.h"

#include "lumenis/model/script.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
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
