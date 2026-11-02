#pragma once

// The classic Title Tool (ROADMAP 6.8d1; FX Guide p.327-341; docs/spec/title-tool.md): a window of its own with the
// title over a frame of the Record monitor or a colour, the safe title and action guidelines and the alignment grid,
// its toolbar at the bottom (Selection, Text, Video Placement, the drawing tools, the text formatting tools, the
// Background Selection tools) and its Edit, Object and Alignment menus ("title-related menu commands appear in the
// File, Edit, Object, and Alignment menus", p.330; ours: in the window's own menu bar). It edits a model::ClassicTitle;
// saving it as a title clip comes with 6.8d4.

#include "lumenis/model/classic_title.h"

#include <QHash>
#include <QImage>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <optional>

class QAction;
class QComboBox;
class QFontComboBox;
class QMenuBar;
class QScrollBar;
class QSpinBox;
class QToolButton;

namespace lumenis::ui {

class TitleCanvas;

class TitleToolWindow : public QWidget {
    Q_OBJECT

public:
    // The toolbar's tools (p.330-331, p.340).
    enum class Tool { Selection, Text, VideoPlacement, Box, Oval, Line };

    explicit TitleToolWindow(QWidget* parent = nullptr);

    // "If there is a sequence in the Record monitor, the Title tool opens with a video background. The location of the
    // position indicator ... determines the initial video frame" (p.333): where the frame comes from (the Record
    // monitor's picture at a size; null when there is none). "Click again in the Title tool to activate its window.
    // The background updates" (p.334): asked again whenever the window becomes active.
    void setFrameSource(std::function<QImage(QSize)> source);
    void updateBackground();
    // The title being made, and starting a new one (the session's default text attributes stay, p.337).
    const model::ClassicTitle& title() const { return title_; }
    void setTitle(const model::ClassicTitle& title);
    void newTitle();

    Tool tool() const { return tool_; }
    // Choosing a tool; `sticky` (a double-click on its button) keeps it after use: "After you use a tool in the
    // toolbar, the Title tool reverts to the Selection tool ... To prevent a tool from automatically reverting to
    // the Selection tool, double-click the tool's icon" (p.332).
    void setTool(Tool tool, bool sticky = false);
    const QStringList& selection() const { return selected_; }
    void select(const QStringList& ids);
    // The text object being typed in, its cursor and the other end of its highlight; empty when not typing.
    const QString& editing() const { return editing_; }
    qsizetype caret() const { return caret_; }
    qsizetype anchor() const { return anchor_; }

    // The drawing environment (p.332-335): Object > Preview, Safe Colors, Safe Title Area/Global Grid; Alignment > Show
    // Alignment Grid, Align to Grid; the Video Background button and the Background Color.
    bool preview() const { return preview_; }
    void setPreview(bool on);
    bool safeColors() const { return safeColors_; }
    void setSafeColors(bool on);
    bool safeAreas() const { return safeAreas_; }
    void setSafeAreas(bool on);
    bool gridShown() const { return grid_; }
    void setGridShown(bool on);
    bool alignToGrid() const { return snap_; }
    void setAlignToGrid(bool on);
    void setVideoBackground(bool on);
    void setBackgroundColor(const QString& color);
    // The Video Placement tool's pan of the background frame, in title pixels.
    QPointF videoOffset() const { return videoOffset_; }

    // The text formatting tools (p.336-340). With text objects selected they change those (the highlighted characters
    // while typing); with nothing selected they change the defaults new text takes ("Changes you make to the default
    // text attributes apply until you quit", p.337). One undo step each.
    void setFont(const QString& family);
    void setPointSize(double size);
    void setBold(bool on);
    void setItalic(bool on);
    void setJustify(model::ClassicJustify justify);
    void setKerning(double percent);
    void setLeading(double points);
    // "Loose ... Adds space between letters; Normal ... Leaves spacing unchanged; Tight ... Closes up the spacing"
    // (p.340): ours 10, 0 and -10 percent.
    static constexpr double kLooseKerning = 10.0;
    static constexpr double kTightKerning = -10.0;
    // The defaults new text takes.
    const model::ClassicObject& defaults() const { return defaults_; }

    // Edit menu (ours: the tool keeps its own undo steps while it is open).
    bool undo();
    bool redo();
    void cut();
    void copy();
    void paste();
    void deleteSelection();
    void selectAll();

    // Rolling and crawling titles (6.8d2; p.341-346). The Rolling Title and Crawling Title buttons: "The button changes
    // to green, and a Page Number text box appears", "the Background Selection tools are set to Video Background" and
    // Auto Size is on (p.343-344); clicking the lit button again makes the title still (ours). One undo step.
    void setMotion(model::ClassicMotion motion);
    // Object > Auto Size Mode (p.343): the pages follow the objects.
    void setAutoSize(bool on);
    // Object > Add Page ("not available when Auto Size mode is on"), Insert Page, Remove Page, Copy to Page, Move to
    // Page (the selected objects) and Make Crawl (the selected text objects), p.344-346; false when refused.
    bool addPage();
    bool insertPage(int page);
    bool removePage(int page);
    bool copyToPage(int page);
    bool moveToPage(int page);
    bool makeCrawl();
    // The view's place in a rolling or crawling title (the title point at the view's top-left), and the page shown, the
    // Page Number text box's (from 1).
    QPointF scroll() const { return scroll_; }
    void setScroll(const QPointF& at);
    int page() const;
    void setPage(int page);
    // The project's frame height, for the page limits (p.342; 1080 until set).
    void setFrameLines(int lines) { lines_ = lines; }
    // "If you attempt to change the text formatting in a way that would make the title exceed the page count limit, a
    // message box opens" (p.342): shown once each time (null until then), for tests.
    QWidget* limitMessage() const;

    // Title pixels and the canvas's widget pixels, both ways (the canvas shows the whole frame, letterboxed).
    QPointF toTitle(const QPointF& widget) const;
    QPointF toWidget(const QPointF& title) const;
    // The canvas, for tests that press and drag on it.
    QWidget* canvas() const;
    // A control by its object name ("title:<name>": "tool:text", "font", "size", "bold", "kerning" ...); null if none.
    QWidget* control(const QString& name) const;
    QMenuBar* menuBar() const { return menuBar_; }

    // Mouse and keys on the canvas, in title pixels (the canvas forwards them; tests may call them).
    void press(const QPointF& at, Qt::KeyboardModifiers modifiers);
    void move(const QPointF& at, Qt::KeyboardModifiers modifiers);
    void release(const QPointF& at, Qt::KeyboardModifiers modifiers);
    bool key(int key, Qt::KeyboardModifiers modifiers, const QString& text);

signals:
    // The title changed (an undo step was made).
    void titleChanged();

protected:
    bool event(QEvent* event) override;
    // Double-clicks on the tool buttons make the tool sticky.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class TitleCanvas;
    void buildToolbar();
    void buildMenus();
    void refreshControls();
    // Keeps `before` as an undo step when the title is different now.
    void commit(const model::ClassicTitle& before);
    void finishEditing();
    // The text objects formatting applies to: the selected ones (the edited one while typing).
    QList<model::ClassicObject*> formatTargets(model::ClassicTitle& t);
    // Changes the formatting of the targets, the highlighted characters, or the defaults.
    void format(const std::function<void(model::ClassicObject&)>& whole, const std::function<void(model::TitleObject&)>& characters);
    QString objectAt(const QPointF& at) const;
    // The handle of the single selected object under a point: 0-7 round a box or oval (corners then sides), 0-1 a
    // line's ends, 0-1 a text object's left and right sides; nothing elsewhere.
    std::optional<int> handleAt(const QPointF& at) const;
    QPointF snapped(const QPointF& at) const;
    QString pickColor(const QString& current, const QString& title);

    model::ClassicTitle title_;
    QList<model::ClassicTitle> undo_, redo_;
    model::ClassicTitle editStart_; // the title when typing began, for its undo step
    model::ClassicObject defaults_;
    Tool tool_ = Tool::Text; // "By default, the Text tool is active when you open the Title tool" (p.332)
    bool sticky_ = false;
    std::optional<Tool> temporary_; // Ctrl+Alt held: the Selection tool for now (p.332)
    QStringList selected_;
    QString editing_;
    qsizetype caret_ = 0;
    qsizetype anchor_ = 0;
    bool preview_ = true;    // ours: on until turned off
    bool safeColors_ = true; // "By default, Safe Colors is turned on" (p.333)
    bool safeAreas_ = true;  // "By default, the Title tool displays two outlined boxes" (p.333)
    bool grid_ = false;
    bool snap_ = false;
    QPointF videoOffset_;
    QPointF scroll_;
    int lines_ = 1080;
    QWidget* limitMessage_ = nullptr;
    QScrollBar* rollBar_ = nullptr;
    QScrollBar* crawlBar_ = nullptr;
    // Whether the title keeps within the page limit; the pages fitted to the objects with Auto Size.
    bool withinLimit() const;
    void fitPages();
    void showLimit();
    void syncScrollBars();
    std::function<QImage(QSize)> frames_;
    QImage frame_;
    QString clipboard_; // ours: objects copied inside the tool, as JSON

    // A press and its drag.
    enum class Drag { None, Move, Handle, Draw, Lasso, Pan, Highlight };
    Drag drag_ = Drag::None;
    QPointF dragFrom_;
    QPointF dragTo_;
    int handle_ = 0;
    model::ClassicTitle dragStart_;
    QPointF panStart_;

    TitleCanvas* canvas_ = nullptr;
    QMenuBar* menuBar_ = nullptr;
    QHash<QString, QWidget*> controls_;
    QHash<QString, QAction*> actions_;
    bool syncing_ = false;
};

// "Select Clip > New Title or Tools > Title Tool Application. The New Title dialog box opens" (p.327): Marquee or Title
// Tool, and Persist. Marquee is Avid's own application and stays dimmed (ours). Returns whether the Title Tool was
// chosen; `persist` tells whether Persist was selected.
bool askNewTitle(QWidget* parent, bool* persist);

} // namespace lumenis::ui
