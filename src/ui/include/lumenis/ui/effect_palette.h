#pragma once

#include "lumenis/model/sequence.h"

#include <QIcon>
#include <QListWidget>
#include <QWidget>

#include <optional>

class QLineEdit;
class QMimeData;
class QTabBar;

namespace lumenis::ui {

// An effect of the Effect Palette (FX guide p.37) and where it can be
// applied, from resources/effects.json.
struct EffectInfo {
    QString category;
    QString name;
    int page = 0;              // in the FX guide's Effects Reference
    bool transition = false;   // can be applied to a transition
    bool segment = false;      // can be applied to a segment
    bool realTime = true;      // the green dot (p.36)
    QStringList parameters;    // parameter categories (p.491)
    // An Audio Track Effect (p.792): the id of a media::audioEffects() effect; empty for video ones.
    QString audioEffect;

    // The effect as the sequence stores it, without parameters.
    model::Effect effect() const { return model::Effect{category, name, {}}; }
};

// The effects that come with the application (FX guide p.456-490).
// Read once from resources/effects.json; empty if the resource is missing.
class EffectCatalog {
public:
    // The one catalog, loaded on first use.
    static const EffectCatalog& builtin();

    // A tab of the Effect Palette.

    struct Tab {
        QString name;
        QString kind; // "video", "transitions", "audioTrack", "audioClip"
    };
    const QList<Tab>& tabs() const { return tabs_; }
    // Every category, in the file's order.
    QStringList categories() const;
    // The effects of one category, in the file's order; empty for an unknown one.
    QList<EffectInfo> effectsIn(const QString& category) const;
    // Every effect, category by category.
    QList<EffectInfo> all() const { return effects_; }
    // The effect named `name` in `category`; null if there is none. The pointer lives as long
    // as the catalog, i.e. the program.
    const EffectInfo* find(const QString& category, const QString& name) const;

private:
    QList<Tab> tabs_;
    QStringList categories_;
    QList<EffectInfo> effects_;
};

// Dragging an effect from the palette onto the Timeline (p.44).
// The data is a small JSON object of the effect's category and name.
inline constexpr char kEffectMimeType[] = "application/x-cutthulhu-effect";
// The drag data for `effect`; the caller (or the QDrag it is given to) owns it.
QMimeData* effectMime(const EffectInfo& effect);
// The Audio Track tab's effects: media::audioEffects() as palette entries (EffectInfo::audioEffect).
QList<EffectInfo> audioTrackEffects();
// The built-in effect a drag carries; nothing if `mime` is null, not an effect, or names an effect
// not in the catalog.
std::optional<EffectInfo> effectFromMime(const QMimeData* mime);
// The effect's icon: ours, a tile split for transitions; with the green
// real-time dot for the palette (p.36).
QIcon effectIcon(bool transition, bool realTimeDot);

// The effect templates of one open bin, as the Effect Palette lists them: "If you save any effect templates to
// a bin, and have that bin open, those templates also display in the Effect Palette" (FX Guide p.37).
struct TemplateBin {
    struct Entry {
        QString itemId;
        QString name;
        bool transition = false; // a transition template gets the transition icon
        bool operator==(const Entry&) const = default;
    };
    QString binId;
    QString name;
    QList<Entry> templates;
    bool operator==(const TemplateBin&) const = default;
};

// The palette's list of effects; its items drag effects, or, for a bin's templates, the template item
// (binItemsMime: the bin's id, then the item's).
class EffectList : public QListWidget {
    Q_OBJECT

public:
    using QListWidget::QListWidget;

protected:
    // Drags the current item's effect (effectMime()) as a copy.
    void startDrag(Qt::DropActions actions) override;
};

// Tools > Effect Palette, Ctrl+8 (FX guide p.37-38): tabs for video
// (Filters, Transitions) and audio (Audio Track, Audio Clip) effects, a
// Quick Find field, the categories on the left and their effects on the
// right. Effects are dragged onto the Timeline or double-clicked to apply
// them to the selection (p.44).
//
// A panel's contents ("EffectPalette"), made by MainWindow, which applies effectActivated() effects
// to the Timeline's selection. The Timeline takes the drops (effectFromMime()).
class EffectPalette : public QWidget {
    Q_OBJECT

public:
    explicit EffectPalette(QWidget* parent = nullptr);

    QTabBar* tabBar() const { return tabs_; }
    QLineEdit* quickFind() const { return find_; }
    QListWidget* categoryList() const { return categories_; }
    EffectList* effectList() const { return effects_; }

    // The tab by its place in EffectCatalog::tabs().
    void setTab(int index);
    // "All" lists every category's effects. A category the tab does not list is ignored.
    void setCategory(const QString& name);
    // The selected category; "All" when none is selected.
    QString category() const;
    // Quick Find (p.37): searches all categories, or the selected one.
    void setQuickFind(const QString& text);
    // The effects shown, by name.
    QStringList shownEffects() const;
    // The effect in row `row` of the effect list; nothing if out of range or a template.
    std::optional<EffectInfo> effectAt(int row) const;

    // The open bins with effect templates, listed below the categories (p.38: "Click the bin name below
    // the effect category list to view the effect templates in the bin"). Bins without templates are left
    // out; the bin selected stays selected while it is listed.
    void setTemplateBins(const QList<TemplateBin>& bins);
    const QList<TemplateBin>& templateBins() const { return templateBins_; }
    QListWidget* templateBinList() const { return bins_; }
    // Shows a bin's templates on the right (by id; empty: the categories again).
    void setTemplateBin(const QString& binId);
    QString templateBin() const;
    // The template item in row `row` of the effect list: {bin id, item id}; nothing for an effect.
    std::optional<std::pair<QString, QString>> templateAt(int row) const;

signals:
    // An effect was double-clicked.
    void effectActivated(const EffectInfo& effect);
    // A bin's effect template was double-clicked.
    void templateActivated(const QString& binId, const QString& itemId);

protected:
    // The palette's menu: Set Font (p.38), for both lists.
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // Lists "All" and the categories the tab shows, keeping the selected one if it is still
    // there, then fills the effects.
    void fillCategories();
    // Lists the effects of the tab and category that match Quick Find.
    void fillEffects();
    // Whether the current tab lists `effect`.
    bool tabShows(const EffectInfo& effect) const;
    // The effects the current tab draws from.
    QList<EffectInfo> tabEffects() const;

    QTabBar* tabs_ = nullptr;
    QLineEdit* find_ = nullptr;
    QListWidget* categories_ = nullptr;
    EffectList* effects_ = nullptr;
    QListWidget* bins_ = nullptr;
    QList<TemplateBin> templateBins_;
};

} // namespace lumenis::ui
