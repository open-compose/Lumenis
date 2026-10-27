#pragma once

#include "lumenis/model/clip_coloring.h"

#include <QDialog>
#include <QHash>
#include <QString>

#include <functional>

class QCheckBox;
class QMenu;
class QToolButton;

namespace lumenis::ui {

// A menu of clip colours: None, the palette of model::clipColors() and Other... (the system colour
// picker), calling `apply` with "#rrggbb" or empty for None. Added to `parent` as a submenu named
// `title`; the bins' Set Clip Color (p.264) and the Timeline's Set Local Clip Color (p.598) use it.
QMenu* addClipColorMenu(QMenu* parent, const QString& title, const std::function<void(const QString&)>& apply);

// The Timeline Fast menu's Clip Color dialog box (Editing Guide p.595-597; docs/spec/timeline-colors.md):
// the kinds of clip colouring in priority order, each with its swatch where its colour can be
// changed, and Default Colors. Resolution Tracking needs MultiRez and Proxy needs proxy media, so
// both are dimmed. The layout is ours: the guide's picture is not described.
class ClipColorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClipColorDialog(const model::ClipColorOptions& options, QWidget* parent = nullptr);

    // The choices and colours as they stand in the dialog.
    model::ClipColorOptions options() const;
    // The checkbox of an option by its key in model::ClipColorOptions::toJson() ("offline", "source"
    // ...), and the swatch of a colour ("offlineColor", "linkedColor", "sdHdColor", or a rate
    // "23.976"); null for others.
    QCheckBox* option(const QString& key) const { return checks_.value(key); }
    QToolButton* swatch(const QString& key) const { return swatches_.value(key); }
    // A colour as the swatch's picker grid sets it.
    void setColor(const QString& key, const QString& hex);
    // Default Colors (p.597): the colours back to their defaults.
    void defaultColors();

private:
    // A swatch button whose menu is the colour grid.
    QToolButton* makeSwatch(const QString& key, const QString& hex);

    model::ClipColorOptions options_;
    QHash<QString, QCheckBox*> checks_;
    QHash<QString, QToolButton*> swatches_;
};

} // namespace lumenis::ui
