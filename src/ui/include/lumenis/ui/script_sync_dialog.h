#pragma once

// The ScriptSync AI dialog (Editing Guide p.584-585; docs/spec/scriptsync.md, 8.3d): Script > ScriptSync AI's Tracks and
// options. MainWindow::scriptSyncAI runs it and places the marks (lumenis/model/script_sync.h).

#include "lumenis/model/script_sync.h"

#include <QDialog>
#include <QStringList>

#include <functional>
#include <optional>

class QCheckBox;
class QListWidget;
class QSpinBox;

namespace lumenis::ui {

class ScriptSyncDialog : public QDialog {
    Q_OBJECT

public:
    // `tracks`: the takes' audio tracks, all checked at first. `selectDialog`: the Select Dialog button's "select a line
    // of dialog from the Script so Media Composer can automatically infer the correct number of characters" (p.585) —
    // the indent of the Script window's selected line, nothing without one.
    ScriptSyncDialog(const QStringList& tracks, const model::ScriptSyncOptions& options,
                     std::function<std::optional<int>()> selectDialog, QWidget* parent = nullptr);
    QStringList tracks() const;
    void setTracks(const QStringList& checked);
    model::ScriptSyncOptions options() const;
    void setOptions(const model::ScriptSyncOptions& options);
    // The Select Dialog button.
    void selectDialog();

private:
    QListWidget* tracks_ = nullptr;
    QCheckBox* capitals_ = nullptr;
    QCheckBox* bracketed_ = nullptr;
    QCheckBox* colon_ = nullptr;
    QCheckBox* indented_ = nullptr;
    QSpinBox* indent_ = nullptr;
    QCheckBox* overwrite_ = nullptr;
    QCheckBox* between_ = nullptr;
    std::function<std::optional<int>()> selectDialog_;
};

} // namespace lumenis::ui
