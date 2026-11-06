#pragma once

// The Titler+ controls (ROADMAP 6.8b2; FX Guide p.302-324; docs/spec/titler.md): the tools, the Font, Transform and
// Style sections of the Dashboard ("The Avid Titler+ Dashboard design includes dedicated Font, Transform, and Style
// sections", p.302) with the Background and Foreground, layer order, groups and the Grid button. One widget serves
// both the Dashboard (across) and the Effect Editor (down, with the Title Text box, p.321). Its controls show the
// first selected object and change every selected one through the TitlerTool, one undo step each.

#include "lumenis/model/titler.h"

#include <QHash>
#include <QWidget>

class QPlainTextEdit;

namespace lumenis::ui {

class TitlerTool;

class TitlerPanel : public QWidget {
    Q_OBJECT

public:
    // `tool` must outlive the panel. `across`: the Dashboard's layout; else the Effect Editor's.
    TitlerPanel(TitlerTool& tool, bool across, QWidget* parent = nullptr);

    // Brings every control up to date with the title and the selection.
    void refresh();
    // A control by its name (objectName "titler:<name>"), e.g. "size", "fill", "fillColor", "alignH"; null if none.
    QWidget* control(const QString& name) const;
    // A colour swatch's choice without its colour dialog (`which`: fill, fill0-3 for the gradient's corners, outline,
    // shadow, background).
    bool chooseColor(const QString& which, const QColor& color);
    // A Titler+ template dropped as a preset (p.320, 6.8c4b) where a drop at `at` (the panel's coordinates) lands:
    // on the Font, Transform, Fill, Outline or Shadow section that section, elsewhere all of them but Transform.
    model::TitlePresetPart presetPartAt(const QPoint& at) const;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    TitlerTool& tool_;
    QHash<QString, QWidget*> controls_;
    QPlainTextEdit* text_ = nullptr;
    bool syncing_ = false;
    // The colour of a swatch from the shown object or title.
    QString colorOf(const QString& which) const;
};

// The Dashboard (p.302): the panel across, as a window of its own that stays over the others. The panel's sections
// in a row are wider than many screens (2700 pixels with the default font), so they scroll across inside the window:
// it opened wider than a Windows 11 screen before (owner's report, 2026-11-05; BUILDING.md).
class TitlerDashboard : public QWidget {
    Q_OBJECT

public:
    explicit TitlerDashboard(TitlerTool& tool, QWidget* parent = nullptr);
    TitlerPanel* panel() const { return panel_; }
    // Places the window the first time it opens, its top left near `at` (global): as wide as its controls, but no wider
    // than the screen (fitOnScreen). Later openings keep where the user put it.
    void placeNear(const QPoint& at);

signals:
    // The Save or Save As button (p.319-320): the template being edited is updated, or a copy made.
    void saveRequested(bool saveAs);

private:
    TitlerPanel* panel_;
    QWidget* scroller_ = nullptr;
    bool placed_ = false;
};

} // namespace lumenis::ui
