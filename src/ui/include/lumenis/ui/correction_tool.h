#pragma once

#include "lumenis/media/auto_correct.h"
#include "lumenis/media/color_match.h"
#include "lumenis/media/custom_color.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/color_correction.h"

#include <QDoubleSpinBox>
#include <QImage>
#include <QWidget>

#include <array>
#include <functional>
#include <optional>

class QButtonGroup;
class QMimeData;
class QLabel;
class QStackedWidget;
class QToolButton;

namespace lumenis::ui {

class Timeline;
class ChromaWheel;
class LumaRangesGraph;
class ChromaCurveGraph;
class Histogram;
class VectorWheel;

// The Color Match control of a Color Correction group (FX Guide p.624-631; 6.10c3a): "input color swatch (top left),
// output color swatch (top right), Match Type button (center), Match Color button (bottom)" (p.625). Pressing a swatch
// asks for an eyedropper colour (pickRequested; the tool samples the monitors); double-clicking opens the system colour
// dialog (p.625-626). With Show Eyedropper Info the swatches show their RGB values (p.608, p.631).
class ColorMatchControl : public QWidget {
    Q_OBJECT

public:
    ColorMatchControl(media::MatchGroup group, QWidget* parent = nullptr);
    media::MatchGroup group() const { return group_; }
    QColor color(bool output) const { return output ? output_ : input_; }
    void setColor(bool output, const QColor& color);
    QString matchType() const { return type_; }
    void setMatchType(const QString& type);
    bool naturalMatch() const { return natural_; }
    void setNaturalMatch(bool on);
    void setShowInfo(bool on);
    bool showInfo() const { return showInfo_; }
    QToolButton* swatch(bool output) const { return output ? outSwatch_ : inSwatch_; }
    QToolButton* typeButton() const { return typeButton_; }
    QToolButton* matchButton() const { return matchButton_; }
    // The swatch's text: its 8-bit RGB values on the video scale ("R: 61 G: 62 B: 66", p.630), or nothing.
    QString infoText(bool output) const;

    // Custom colours (p.630-631; 6.10c3b): "Alt+drag (Windows) or Option+drag (Macintosh) the swatch" into a bin saves
    // its colour, named by the Saved Color Labels setting; a custom colour item dragged from a bin onto a swatch, or
    // another swatch's colour, loads it. Bin items are looked up through the resolver.
    void setLabels(media::SavedColorLabels labels) { labels_ = labels; }
    using ItemResolver = std::function<std::optional<model::BinItem>(const QString& bin, const QString& id)>;
    void setItemResolver(ItemResolver resolver) { resolve_ = std::move(resolver); }
    // The drag a swatch starts (its colour and name), for the bin.
    QMimeData* colorMime(bool output) const;
    // Takes a dropped colour onto a swatch; false when the data holds none.
    bool dropColor(bool output, const QMimeData* dropped);
    // "Eyedropper Picks from Anywhere in Application" (p.608, p.625): pressing a swatch and letting go anywhere over the
    // application picks the colour drawn there (pickAt), not only in the monitors.
    void setPickAnywhere(bool on) { anywhere_ = on; }
    bool pickAnywhere() const { return anywhere_; }
    // The colour the application draws at a point on the screen; invalid outside its windows.
    static QColor colorAt(const QPoint& global);

signals:
    void pickRequested(bool output);
    void matchRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void sync();
    media::MatchGroup group_;
    QColor input_, output_;
    QString type_;
    bool natural_ = false;
    bool showInfo_ = true;
    bool anywhere_ = false;
    media::SavedColorLabels labels_ = media::SavedColorLabels::NameAndRgb;
    ItemResolver resolve_;
    QPoint pressAt_;
    bool pressAlt_ = false;
    bool tracking_ = false;
    QToolButton* inSwatch_ = nullptr;
    QToolButton* outSwatch_ = nullptr;
    QToolButton* typeButton_ = nullptr;
    QToolButton* matchButton_ = nullptr;
};

// An automatic correction or Remove Color Cast button of the tool (p.620-622): a small text button named `name`, with
// the button's full name as its tool tip.
QToolButton* correctionActionButton(QWidget* parent, const QString& name, const QString& text, const QString& tip);

// A value box of the Color Correction tool that keeps a value on the 8-bit scale and shows it on the Correction
// setting's unit (model::toCorrectionUnit; FX Guide p.606): 10 Bit, 8 Bit, Percent, IRE or mVolts. Typed numbers are on
// that unit too.
class UnitSpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    using QDoubleSpinBox::QDoubleSpinBox;
    void setUnit(model::CorrectionUnit unit, bool difference = false);
    model::CorrectionUnit unit() const { return unit_; }

protected:
    QString textFromValue(double value) const override;
    double valueFromText(const QString& text) const override;
    QValidator::State validate(QString& text, int& pos) const override;

private:
    model::CorrectionUnit unit_ = model::CorrectionUnit::EightBit;
    bool difference_ = false;
};

// The Color Correction tool (FX Guide p.595-605, p.631-644; docs/spec/color-correction.md, 6.10b2): the Correction Type
// button, the group tabs (HSL, Channels, Levels, Curves, Sec) and the HSL group's subdividing tabs, every one with its
// Enable button, and the HSL group's panes: the Controls tab's sliders for the Master, Highlights, Midtones and Shadows
// ranges, the Hue Offsets tab's ChromaWheels with Gain, Gamma and Setup, and the Luma Ranges graph. 6.10b3a adds the
// Channels pane (p.645-647), the Levels pane with its subdividing tabs, histograms and ChromaCurve graph (p.648-659) and
// the Curves pane (p.659-663) (correction_tool_panes.cpp); 6.10b3b the Secondary pane (p.668-683,
// correction_tool_secondary.cpp).
//
// The tool shows the correction of the segment at the position on the Timeline's correction track (Timeline::
// currentCorrection) and changes it through Timeline::setCurrentCorrection, an undo step each time a control is let go
// of (ours: the monitors show the change then). MainWindow shows it as the "ColorCorrection" panel while the Timeline
// is in Color Correction mode.
class CorrectionTool : public QWidget {
    Q_OBJECT

public:
    explicit CorrectionTool(Timeline& timeline, QWidget* parent = nullptr);

    // The Correction setting's options for the tool (FX Guide p.606-608; Editing Guide p.1164-1166; 6.10b4a). Tabs:
    // "Define which group tabs appear in the Color Correction tool" (HSL, Channels, Levels, Curves, Secondary; ours: HSL
    // stays when none is chosen). Units: the scales the boxes show. Levels: Solid, Color and Dynamic Histograms and Use
    // Full Ranges. Features: which vectors the Secondary group's Input wheel shows, and the colour backgrounds of the Hue
    // Offsets ChromaWheels and the Curves graphs. MainWindow applies them from the setting.
    struct Options {
        std::array<bool, 5> groups{true, true, true, true, true};
        model::CorrectionUnits units;
        bool solidHistograms = true;
        bool colorHistograms = false;
        bool dynamicHistograms = false;
        bool fullRanges = false;
        enum class Vectors { All, Standard, Custom, Enabled };
        Vectors vectors = Vectors::Standard;
        bool backgrounds = false;
        // "the numerical RGB values appear on the color swatches in the Color Match controls" (p.608); "By default, the
        // color swatches in the Color Match control display the RGB values" (p.631).
        bool eyedropperInfo = true;
        media::SavedColorLabels savedColorLabels = media::SavedColorLabels::NameAndRgb;
        bool eyedropperAnywhere = false;
        bool operator==(const Options&) const = default;
    };
    const Options& options() const { return options_; }
    void setOptions(const Options& options);
    // Whether a group's tab is shown (the Tabs tab).
    bool groupShown(const QString& group) const;
    // Shows the tab at a path as shownPath() gives it ("hsl/lumaRanges/highlights", "levels/red", "secondary"), for the
    // Command Palette's Tab buttons (p.601); false for a path of no tab, or one the Tabs setting hides.
    bool showPath(const QString& path);

    // The buttons on the right (FX Guide p.605): "Effect Mode button, Color Correction Effect Template button,
    // Correction Mode Settings button, Safe Color Settings button, Comments button", by the names "effectMode",
    // "template", "settings", "safeColors" and "comments". The template button comes with 6.10d and the Safe Color
    // Settings with 6.10e; they are dimmed until then.
    QToolButton* button(const QString& name) const;
    // The Comments button (p.609-610): "If the position indicator is on a segment that has a comment, the icon on the
    // Comments button is yellow". Clicking it opens the Comments dialog for the comment at the position; OK keeps
    // the text, Remove takes the comment off. Tests answer the dialog through the asker: nothing for Cancel, the text
    // for OK, an empty text for Remove.
    using CommentAsker = std::function<std::optional<QString>(const QString& current)>;
    void setCommentAsker(CommentAsker asker) { askComment_ = std::move(asker); }
    void editComment();
    bool hasComment() const { return hasComment_; }

    // Reads the correction at the position again (the position moved, the sequence changed).
    void reload();
    const model::ColorCorrection& correction() const { return correction_; }

    // The tabs shown: the group ("hsl", "channels", "levels", "curves", "secondary"), the HSL group's subdividing tab
    // ("controls", "hueOffsets", "lumaRanges") and the second level ("master", "highlights", "midtones", "shadows").
    QString group() const { return group_; }
    void showGroup(const QString& group);
    QString hslTab() const { return hslTab_; }
    void showHslTab(const QString& tab);
    QString rangeTab() const { return range_; }
    void showRangeTab(const QString& range);
    // The path of the tab shown at the deepest level ("hsl/controls/midtones"), whose controls Alt+click resets.
    QString shownPath() const;

    // An Enable button clicked (p.603-605): on and off, or with Alt "All controls linked to that button return to their
    // default values" when its controls are displayed; a tab whose controls are not displayed is displayed instead.
    void clickEnable(const QString& path, bool alt);
    // Whether the button at `path` is lit: on, and something under it set (ours).
    bool enableLit(const QString& path) const;

    // Sets a control and commits it as an undo step.
    void setControl(const QString& path, double value);
    // The Levels group's subdividing tab: "composite", "luma", "red", "green", "blue", "master" (p.653).
    QString levelsTab() const { return levelsTab_; }
    void showLevelsTab(const QString& tab);
    // The frame at the position without (false) or with (true) the corrections, for the histograms (p.654); MainWindow
    // takes it from the Composer.
    using PictureSource = std::function<QImage(bool corrected)>;
    void setPictureSource(PictureSource source) { pictures_ = std::move(source); }
    // The Channels tab's Preview pane (p.645-646): an input or output channel in monochrome, or the master image.
    enum class ChannelPreview { RedInput, GreenInput, BlueInput, MasterInput, RedOutput, GreenOutput, BlueOutput, MasterOutput };
    ChannelPreview channelPreview() const { return channelPreview_; }
    void setChannelPreview(ChannelPreview preview);
    ChromaCurveGraph* levelsGraph() const { return levelsGraph_; }
    Histogram* histogram(bool output) const { return output ? outHistogram_ : inHistogram_; }
    ChromaCurveGraph* curveGraph(const QString& graph) const;
    // The Secondary pane (p.668-683): the selected vector (kept in the correction for Isolate, "secondary/selected"),
    // Lock Vectors (p.676) and the Color Selection buttons (p.678-679), which make a click or drag in a monitor
    // sample colours for applySamples.
    QString selectedVector() const;
    void selectVector(const QString& vector);
    bool lockVectors() const { return lockVectors_; }
    void setLockVectors(bool on);
    // RemoveColorCast is a Remove Color Cast button (6.10c2): the next colour sampled is made neutral. MatchColor is a
    // Color Match swatch's eyedropper (6.10c3a): the colour sampled fills the swatch.
    enum class ColorSelection { None, Syringe, AddToSelection, SubtractFromSelection, RemoveColorCast, MatchColor };
    ColorSelection colorSelection() const { return colorSelection_; }
    void setColorSelection(ColorSelection selection);
    // Colours sampled in a monitor with the Color Selection button that is on: the syringe defines a vector from them
    // all (model::syringeVector), the eyedroppers add or subtract the first (p.678-679). One undo step.
    void applySamples(const QList<QColor>& colors);
    VectorWheel* vectorWheel(bool output) const { return output ? outputWheel_ : inputWheel_; }
    // The automatic corrections (FX Guide p.618-622; media::autoCorrect): the Auto buttons of the Hue Offsets and
    // Controls > Master tabs and the Curves tab, and the Command Palette's (p.621), measuring the uncorrected frame at the
    // position through the correction; one undo step. A Remove Color Cast button "appears highlighted" and the monitors
    // sample a colour: "Drag the pointer into the image ... release the mouse button" (p.622); applySamples makes it
    // neutral (media::removeColorCast) and the button goes off.
    void autoCorrect(media::AutoCorrection kind, bool hueOffsets = true);
    void startRemoveColorCast(media::CastRemoval where);
    std::optional<media::CastRemoval> castRemoval() const { return castRemoval_; }
    // The groups' Color Match controls by name: "controls", "hueOffsets", "channels", "levels", "curves", "secondary".
    ColorMatchControl* colorMatch(const QString& name) const;
    // The Match Color button (p.626): the control's input colour to its output by its Match Type, one undo step; the
    // Controls tab's control works on the range tab shown, the Secondary one on the selected vector.
    void matchColor(ColorMatchControl* control);
    // How the Color Match swatches find bin items dragged onto them (MainWindow: the Composer's resolver).
    void setItemResolver(ColorMatchControl::ItemResolver resolver);
    // The Luma Ranges tab's preview (p.642-643).
    enum class LumaPreview { Input, ThreeTone, Output };
    LumaPreview lumaPreview() const { return lumaPreview_; }
    void setLumaPreview(LumaPreview preview);

    QWidget* control(const QString& path) const; // a slider row, wheel or graph by its path (for tests)
    QToolButton* typeButton() const { return type_; }
    QToolButton* enableButton(const QString& path) const;
    QToolButton* tabButton(const QString& path) const;
    ChromaWheel* wheel(const QString& range) const;
    LumaRangesGraph* lumaGraph() const { return lumaGraph_; }

signals:
    // The Effect Mode and Correction Mode Settings buttons, for MainWindow (p.605-606).
    void effectModeRequested();
    void settingsRequested();
    // The Luma Ranges preview changed, for the monitors.
    void lumaPreviewChanged(LumaPreview preview);
    void channelPreviewChanged(ChannelPreview preview);
    void colorSelectionChanged(ColorSelection selection);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QWidget* tab(QWidget* parent, const QString& path, const QString& text);
    QWidget* controlsPane();
    QWidget* hueOffsetsPane();
    QWidget* lumaRangesPane();
    QWidget* sliderRow(QWidget* parent, const QString& path, const QString& label);
    QWidget* channelsPane();
    QWidget* levelsPane();
    QWidget* curvesPane();
    QWidget* secondaryPane();
    void syncPanes();
    void syncSecondary();
    void applyUnits();
    // Dynamic Histograms: the output histogram of the levels tab shown while a slider or point is dragged, from the
    // uncorrected frame through `tentative` (ours: this side's correction only).
    void dragHistogram(const model::ColorCorrection& tentative);
    void commit(const QString& step);
    void sync();
    void syncControls();

    Timeline& timeline_;
    Options options_;
    CommentAsker askComment_;
    // The shot and track the Correction Type button last followed (reload).
    std::optional<std::pair<std::int64_t, std::int64_t>> shownShot_;
    QString shownTrack_;
    bool hasComment_ = false;
    model::ColorCorrection correction_;
    bool hasSegment_ = false;
    QString group_ = QStringLiteral("hsl");
    QString hslTab_ = QStringLiteral("hueOffsets"); // "The default display shows the controls in the Hue Offsets tab" (p.595)
    QString range_ = QStringLiteral("master");
    LumaPreview lumaPreview_ = LumaPreview::Output;
    QToolButton* type_ = nullptr;
    QStackedWidget* groups_ = nullptr;
    QStackedWidget* hslPages_ = nullptr;
    QWidget* rangeColumn_ = nullptr;
    QWidget* lumaColumn_ = nullptr;
    QStackedWidget* controlPages_ = nullptr;
    LumaRangesGraph* lumaGraph_ = nullptr;
    QDoubleSpinBox* lumaIn_ = nullptr;
    QDoubleSpinBox* lumaOut_ = nullptr;
    QString levelsTab_ = QStringLiteral("master");
    PictureSource pictures_;
    ChannelPreview channelPreview_ = ChannelPreview::MasterOutput;
    ChromaCurveGraph* levelsGraph_ = nullptr;
    Histogram* inHistogram_ = nullptr;
    Histogram* outHistogram_ = nullptr;
    bool lockVectors_ = false;
    std::optional<media::CastRemoval> castRemoval_;
    ColorMatchControl* picking_ = nullptr; // the control whose swatch takes the next sample
    bool pickingOutput_ = false;
    ColorMatchControl* colorMatchFor(media::MatchGroup group, QWidget* parent);
    ColorSelection colorSelection_ = ColorSelection::None;
    VectorWheel* inputWheel_ = nullptr;
    VectorWheel* outputWheel_ = nullptr;
    QStackedWidget* inputPages_ = nullptr;
    QStackedWidget* outputPages_ = nullptr;
};

// The Secondary tab's colour wheels (p.670-673, p.675, p.680): the Input wheel shows the vectors, "Standard vectors
// always appear as wedges ... Custom vectors always appear as ellipses", with "Center points for standard vectors are
// filled circles. Center points for custom vectors are crosshairs"; the Output wheel the selected vector's output.
// Clicking a centre point selects its vector; dragging the selected one moves it.
class VectorWheel : public QWidget {
    Q_OBJECT

public:
    struct Shown {
        QString name;
        model::SecondaryVector vector;
        bool selected = false;
    };
    explicit VectorWheel(bool output, QWidget* parent = nullptr);
    void setVectors(const QList<Shown>& vectors);
    const QList<Shown>& vectors() const { return vectors_; }
    QPointF pointFor(double hue, double saturation) const;
    std::pair<double, double> placeAt(const QPointF& point) const;
    QSize sizeHint() const override { return {150, 150}; }

signals:
    void vectorClicked(const QString& name);
    void centreMoved(double hue, double saturation); // let go: commit

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPointF centreOf(const Shown& s) const;
    bool output_;
    QList<Shown> vectors_;
    bool dragging_ = false;
    QPointF dragged_;
};

// A ChromaCurve graph (p.655-663): input 0-255 across, output up, the curve through its control points drawn from
// `mapping`. In the Curves tab points are added by clicking the curve, dragged, typed and deleted (two to sixteen,
// p.662-663); in the Levels tab its black, gray and white points are only dragged (p.656-657).
class ChromaCurveGraph : public QWidget {
    Q_OBJECT

public:
    explicit ChromaCurveGraph(bool editable, QWidget* parent = nullptr);
    void setCurve(const QList<QPointF>& points, std::function<double(double)> mapping);
    // Show ChromaWheel and ChromaCurve Graphs (p.608): a colour background, the graph's channel from black at the left
    // to full at the right (ours; grey for Master).
    void setBackground(const QColor& color) { background_ = color; update(); }
    const QList<QPointF>& points() const { return points_; }
    int selectedPoint() const { return selected_; }
    void selectPoint(int index);
    QPointF toWidget(const QPointF& value) const;
    QPointF toValue(const QPointF& point) const;
    QSize sizeHint() const override { return {130, 130}; }

signals:
    void curveChanged(const QList<QPointF>& points); // let go, or a point deleted: commit
    void curveDragged(const QList<QPointF>& points); // while dragging (Dynamic Histograms)
    void selectionChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    bool editable_;
    QColor background_;
    QList<QPointF> points_;
    std::function<double(double)> mapping_;
    int selected_ = -1;
    bool dragging_ = false;
};

// A histogram (p.654-655): how many pixels have each value, 0-255 across, with a background grid at the quartiles, and
// below it the triangular sliders of the black, gray and white points (p.656), dragged to set them.
class Histogram : public QWidget {
    Q_OBJECT

public:
    // What it counts: a colour channel on the 8-bit video scale, the three together (Master), Y' (Luma) or Y' and the
    // chroma's swing together (Composite, ours).
    enum class Channel { Red, Green, Blue, Master, Luma, Composite };
    explicit Histogram(QWidget* parent = nullptr);
    void setImage(const QImage& image, Channel channel);
    Channel channel() const { return channel_; }
    void setChannel(Channel channel); // what the axis and colours are for, before the counts come
    const std::array<int, 256>& counts() const { return counts_; }
    void setSliders(const QList<double>& values); // black, (gray,) white
    const QList<double>& sliders() const { return sliders_; }
    double sliderX(int index) const;
    // The Correction setting's Levels tab (p.606-607): Solid (filled, else a line graph), Color (a single channel's in
    // its colour) and Use Full Ranges, "the full 10-bit range of level values on the horizontal axis" for the Red,
    // Green, Blue and Master histograms: 0 to 1024 in 10-bit units, -128 to 384 on the 8-bit scale.
    void setStyle(bool solid, bool color, bool fullRange);
    bool solid() const { return solid_; }
    // The values across the horizontal axis, on the 8-bit scale.
    std::pair<double, double> axis() const;
    QSize sizeHint() const override { return {130, 80}; }

signals:
    void sliderMoved(int index, double value); // let go: commit
    void sliderDragged(int index, double value); // while dragging (Dynamic Histograms)

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    double xFor(double value) const;
    std::array<int, 256> counts_{};
    Channel channel_ = Channel::Master;
    bool solid_ = true;
    bool color_ = false;
    bool fullRange_ = false;
    QList<double> sliders_;
    int dragging_ = -1;
};

// A ChromaWheel (p.635-638): hue around the circumference with red near the top, as on a vectorscope, saturation (the
// Amount, 0 to 100) along the radius; a crosshair pointer of its own shape for each wheel. Dragging moves the pointer;
// with Shift it moves a quarter as far for "more precise control ... in the central area" (ours: the fraction).
class ChromaWheel : public QWidget {
    Q_OBJECT

public:
    ChromaWheel(int pointer, QWidget* parent = nullptr);
    void setOffset(double hue, double amount);
    // Show ChromaWheel and ChromaCurve Graphs (p.608): the wheel filled with the hues around it (ours: through
    // media::wheelColor, paler to the centre).
    void setBackground(bool on);
    bool background() const { return background_; }
    double hue() const { return hue_; }
    double amount() const { return amount_; }
    // Where a hue and amount fall in the widget, and back.
    QPointF pointFor(double hue, double amount) const;
    std::pair<double, double> offsetAt(const QPointF& point) const;
    QSize sizeHint() const override { return {120, 120}; }

signals:
    void moved(double hue, double amount);    // while dragging
    void released(double hue, double amount); // let go: commit

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int pointer_;
    bool background_ = false;
    QImage backgroundImage_;
    double hue_ = 0, amount_ = 0;
    bool dragging_ = false;
    QPointF last_;
};

// The Luma Ranges graph (p.641-643): brightness 0-255 across, percent up, the three ranges' curves, the active one with
// its control points. Clicking the active curve adds a point, dragging moves it, Delete removes the selected one.
class LumaRangesGraph : public QWidget {
    Q_OBJECT

public:
    explicit LumaRangesGraph(QWidget* parent = nullptr);
    void setCurves(const std::array<QList<QPointF>, 3>& curves, int active); // shadows, midtones, highlights
    const QList<QPointF>& activeCurve() const { return curves_.at(static_cast<std::size_t>(active_)); }
    int selectedPoint() const { return selected_; }
    void selectPoint(int index);
    QPointF toWidget(const QPointF& value) const;
    QPointF toValue(const QPointF& point) const;
    QSize sizeHint() const override { return {260, 150}; }

signals:
    void curveChanged(const QList<QPointF>& curve); // let go: commit
    void selectionChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    std::array<QList<QPointF>, 3> curves_;
    int active_ = 2;
    int selected_ = -1;
    bool dragging_ = false;
};

} // namespace lumenis::ui
