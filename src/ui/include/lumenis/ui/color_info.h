#pragma once

// The Color Info tool (FX Guide p.696-697; docs/spec/color-correction.md, 6.10e2): "allows you to see the RGB values for
// the pixels located under the pointer on the screen ... without having to go to Color Correction Mode". Tools > Color
// Info Tool (command tools.color_info_tool) and the Color Correction tool's Color Info button open it, as the
// "ColorInfo" panel. It shows the colour under the pointer in two columns, Video and Video LUT (the colour after the
// Display Color Space; ours: the same until display LUTs come), on the scale its shortcut menu chooses: "RGB 8 bit"
// (0 to 255), "RGB 10 bit" (0 to 1024), "RGB Percent", "IRE" and "mVolts". "Active only for video monitors": the
// pointer counts only over the Source and Record monitors; without it, anywhere on the screen.
//
// MainWindow gives the sampler that reads a colour at a screen point (the Composer's picture over a monitor, the
// screen elsewhere); the tool asks it while it is shown, a few times a second.

#include <QColor>
#include <QWidget>

#include <functional>
#include <optional>

class QLabel;
class QMenu;
class QTimer;

namespace lumenis::ui {

class ColorInfoTool : public QWidget {
    Q_OBJECT

public:
    enum class Unit { Rgb8, Rgb10, Percent, Ire, Millivolts };
    // The colour at a global screen point; `monitorsOnly` asks for the video monitors' pictures only.
    using Sampler = std::function<std::optional<QColor>(const QPoint& global, bool monitorsOnly)>;

    explicit ColorInfoTool(QWidget* parent = nullptr);

    void setSampler(Sampler sampler) { sampler_ = std::move(sampler); }
    // Reads the colour at `global` (the pointer, every 100 ms while shown) and shows it.
    void sample(const QPoint& global);
    std::optional<QColor> shownColor() const { return color_; }

    bool activeOnlyForMonitors() const { return monitorsOnly_; }
    void setActiveOnlyForMonitors(bool on);
    bool showAfterDisplayColorSpace() const { return afterLut_; }
    void setShowAfterDisplayColorSpace(bool on);
    Unit unit() const { return unit_; }
    void setUnit(Unit unit);

    // The shortcut menu (p.697); the caller owns it.
    QMenu* shortcutMenu(QWidget* parent);
    // A channel's value (0 to 255) on a scale, as the tool writes it.
    static QString valueText(int value, Unit unit);
    static QString unitText(Unit unit);
    // The text a column shows ("R: 219\nG: 218\nB: 219"), for tests.
    QString columnText(bool lut) const;

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refresh();

    Sampler sampler_;
    QTimer* timer_ = nullptr;
    QLabel* unitLabels_[2] = {nullptr, nullptr};
    QLabel* values_[2] = {nullptr, nullptr};
    QWidget* lutColumn_ = nullptr;
    std::optional<QColor> color_;
    bool monitorsOnly_ = true;
    bool afterLut_ = true;
    Unit unit_ = Unit::Rgb8;
};

} // namespace lumenis::ui
