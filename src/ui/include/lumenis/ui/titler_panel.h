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

private:
    TitlerTool& tool_;
    QHash<QString, QWidget*> controls_;
    QPlainTextEdit* text_ = nullptr;
    bool syncing_ = false;
    // The colour of a swatch from the shown object or title.
    QString colorOf(const QString& which) const;
};

// The Dashboard (p.302): the panel across, as a window of its own that stays over the others.
class TitlerDashboard : public QWidget {
    Q_OBJECT

public:
    explicit TitlerDashboard(TitlerTool& tool, QWidget* parent = nullptr);
    TitlerPanel* panel() const { return panel_; }

signals:
    // The Save or Save As button (p.319-320): the template being edited is updated, or a copy made.
    void saveRequested(bool saveAs);

private:
    TitlerPanel* panel_;
};

} // namespace lumenis::ui
