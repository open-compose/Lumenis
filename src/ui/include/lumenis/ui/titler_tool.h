#pragma once

// Editing a Titler+ title in the Record monitor (ROADMAP 6.8b1; FX Guide p.303-319; docs/spec/titler.md): the Text,
// Selection, Rectangle and Ellipse tools working on the Effect Preview through Composer::OverlayInput, the selection
// and its bounding boxes, typing, moving, scaling and rotating with their modifier keys, and the keyboard shortcuts.
// The Effect Editor owns it and hands it the title shown; changes go back through the editor as previews (while a
// drag or typing goes on) and undo steps.

#include "lumenis/model/titler.h"
#include "lumenis/ui/composer.h"

#include <QObject>
#include <QStringList>

#include <functional>

#include <optional>

namespace lumenis::ui {

class EffectEditor;

class TitlerTool : public QObject, public Composer::OverlayInput {
    Q_OBJECT

public:
    // The tools of the Dashboard and the Effect Editor's toolbar (p.303-305).
    enum class Tool { Selection, Text, Rectangle, Ellipse };

    explicit TitlerTool(EffectEditor& editor);

    Tool tool() const { return tool_; }
    // Switching tools ends typing.
    void setTool(Tool tool);
    // The selected objects (ids), the object being typed into and its cursor and selection [anchor, caret).
    const QStringList& selection() const { return selected_; }
    void select(const QStringList& ids);
    const QString& editing() const { return editing_; }
    qsizetype caret() const { return caret_; }
    qsizetype selectionAnchor() const { return anchor_; }
    // Ends typing (the text becomes one undo step; an empty text object goes, p.303).
    void finishEditing();
    // Forgets the selection and any typing, for another effect.
    void reset();
    // A drag or typing is under way (its title not yet an undo step).
    bool busy() const { return working_.has_value(); }

    // The commands on the selection (p.318-319): one undo step each; false when nothing changes.
    bool bringForward();
    bool sendBackward();
    bool group();
    bool ungroup();
    bool removeSelected();
    // "Select Previous Object (Page Down ...) or Select Next Object (Page Up ...)" (p.319), in layer order.
    bool selectAdjacent(bool next);
    // Add Keyframe / Delete Keyframe at the position (6.8c1) for the selected objects' numbers, or the title's own with
    // nothing selected (EffectEditor::setTitleKeyframes).
    bool setKeyframes(bool add);

    // Changes to the selected objects (each of them; a group as a whole) or to the whole title, one undo step each
    // (the Dashboard's and the Effect Editor's controls, p.306-324); false when nothing changes.
    bool modifySelected(const QString& description, const std::function<void(model::TitleObject&)>& change);
    bool modifyTitle(const QString& description, const std::function<void(model::Title&)>& change);
    // "Options for Align H include Align Left, Align Center, Align Right, and Align Horizontal Center. Options for
    // Align V include Align Top, Align Center, Align Bottom, and Align Vertical Center" (p.312).
    enum class Align { Left, Center, Right, HorizontalCenter, Top, Middle, Bottom, VerticalCenter };
    bool align(Align how);
    // The Anchor Point presets (p.313): where on the selected objects' own bounds their anchor point goes.
    enum class Anchor { Center, Right, Left, TopCenter, TopRight, TopLeft, BottomCenter, BottomRight, BottomLeft };
    bool setAnchor(Anchor where);
    // The preset an object's anchor point is at, or nothing ("Custom ... after manually moving the sliders").
    std::optional<Anchor> anchorOf(const model::TitleObject& object) const;
    // The Grid button (p.319): the Safe Action and Safe Title areas over the Record monitor (ours: 90% and 80%).
    bool gridShown() const { return grid_; }
    void setGridShown(bool shown);
    // Tells the panels the shown title may have changed (an undo, another effect shown).
    void notifyShown() { emit changed(); }
    // The title as the panels show it (a drag's or the typing's in progress, else the stored one).
    model::Title shownTitle() const { return title(); }

    // The selection's boxes, handles, anchors and the text cursor for the Effect Preview.
    void fillOverlay(Composer::EffectPreview& preview) const;

    // Composer::OverlayInput.
    bool overlayPress(const QPointF& at, Qt::KeyboardModifiers modifiers, bool doubleClick) override;
    void overlayMove(const QPointF& at, Qt::KeyboardModifiers modifiers) override;
    void overlayRelease(const QPointF& at, Qt::KeyboardModifiers modifiers) override;
    bool overlayKey(QKeyEvent* event) override;
    bool overlayWantsKey(const QKeyEvent* event) const override;

    // Fractions of the picture to title pixels and back (titler.h's coordinates).
    QPointF toTitle(const QPointF& fraction) const;
    QPointF toFraction(const QPointF& title) const;

signals:
    void changed(); // the tool, the selection or the text cursor

private:
    // The title being worked on: the one in progress during a drag or typing, else the shown one.
    model::Title title() const;
    void preview(const model::Title& title);
    bool commit(const QString& description, const model::Title& title);
    // The topmost object under a title point (top level; inside groups with `deep`).
    QString objectAt(const model::Title& title, const QPointF& point, bool deep) const;
    // A corner handle (0-3) of the single selected object under a title point; or the rotation zone just outside one.
    std::optional<int> cornerAt(const model::Title& title, const QPointF& point, bool* rotate) const;
    void startEditing(const model::Title& title, const QString& id, std::optional<qsizetype> at = std::nullopt);

    enum class Drag { None, Move, Scale, Rotate, Draw };
    EffectEditor& editor_;
    Tool tool_ = Tool::Text;
    QStringList selected_;
    QString editing_;
    qsizetype caret_ = 0;
    qsizetype anchor_ = 0;
    std::optional<model::Title> working_; // a drag's or the typing's title, not yet an undo step
    Drag drag_ = Drag::None;
    QPointF dragFrom_;
    int corner_ = 0;
    model::Title dragStart_;
    bool grid_ = false;
};

} // namespace lumenis::ui
