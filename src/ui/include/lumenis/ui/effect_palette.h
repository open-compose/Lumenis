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

    model::Effect effect() const { return model::Effect{category, name, {}}; }
};

// The effects that come with the application (FX guide p.456-490).
class EffectCatalog {
public:
    static const EffectCatalog& builtin();

    struct Tab {
        QString name;
        QString kind; // "video", "transitions", "audioTrack", "audioClip"
    };
    const QList<Tab>& tabs() const { return tabs_; }
    QStringList categories() const;
    QList<EffectInfo> effectsIn(const QString& category) const;
    QList<EffectInfo> all() const { return effects_; }
    const EffectInfo* find(const QString& category, const QString& name) const;

private:
    QList<Tab> tabs_;
    QStringList categories_;
    QList<EffectInfo> effects_;
};

// Dragging an effect from the palette onto the Timeline (p.44).
inline constexpr char kEffectMimeType[] = "application/x-cutthulhu-effect";
QMimeData* effectMime(const EffectInfo& effect);
std::optional<EffectInfo> effectFromMime(const QMimeData* mime);
// The effect's icon: ours, a tile split for transitions; with the green
// real-time dot for the palette (p.36).
QIcon effectIcon(bool transition, bool realTimeDot);

// The palette's list of effects; its items drag effects.
class EffectList : public QListWidget {
    Q_OBJECT

public:
    using QListWidget::QListWidget;

protected:
    void startDrag(Qt::DropActions actions) override;
};

// Tools > Effect Palette, Ctrl+8 (FX guide p.37-38): tabs for video
// (Filters, Transitions) and audio (Audio Track, Audio Clip) effects, a
// Quick Find field, the categories on the left and their effects on the
// right. Effects are dragged onto the Timeline or double-clicked to apply
// them to the selection (p.44).
class EffectPalette : public QWidget {
    Q_OBJECT

public:
    explicit EffectPalette(QWidget* parent = nullptr);

    QTabBar* tabBar() const { return tabs_; }
    QLineEdit* quickFind() const { return find_; }
    QListWidget* categoryList() const { return categories_; }
    EffectList* effectList() const { return effects_; }

    void setTab(int index);
    // "All" lists every category's effects.
    void setCategory(const QString& name);
    QString category() const;
    // Quick Find (p.37): searches all categories, or the selected one.
    void setQuickFind(const QString& text);
    // The effects shown, by name.
    QStringList shownEffects() const;
    std::optional<EffectInfo> effectAt(int row) const;

signals:
    // An effect was double-clicked.
    void effectActivated(const EffectInfo& effect);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void fillCategories();
    void fillEffects();
    bool tabShows(const EffectInfo& effect) const;

    QTabBar* tabs_ = nullptr;
    QLineEdit* find_ = nullptr;
    QListWidget* categories_ = nullptr;
    EffectList* effects_ = nullptr;
};

} // namespace lumenis::ui
