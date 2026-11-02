# The classic Title Tool

ROADMAP 6.8d. Sources: FX Guide p.326-364 (2025.12 and 2026.8 editions, same pages). Code: `src/model/classic_title.*`
(the title), `src/media/classic_title.*` (its picture), `src/ui/title_tool.*` (the window), `MainWindow::openTitleTool`.
Tests: `tst_classic_title`, `tst_title_tool`.

## 6.8d1: the window, the drawing environment, text and graphic objects

### From the guide
* "Select Clip > New Title or Tools > Title Tool Application. The New Title dialog box opens" with Marquee and Title
  Tool; "select Persist before you click Marquee or Title Tool. For the rest of your editing session, Media Composer
  automatically opens the title tool you selected" (p.327). "The Text tool is automatically selected" (p.327).
* "The Title Tool application is not supported with greater than HD projects" (p.328).
* The components (p.329-331): a video or colour background, the title, the safe title and safe action guidelines,
  "The toolbar at the bottom of the screen", and "title-related menu commands appear in the File, Edit, Object, and
  Alignment menus". The toolbar: Selection tool, Text tool, Video Placement tool ("lets you pan around the entire video
  clip"), the drawing tools, the text formatting tools, colour and transparency, shadows, styles, line attributes,
  templates, the Background Selection tools, the Crawling and Rolling Title buttons.
* Selection and Text tools (p.331-332): click to select, Shift+click or a lasso for more; "Press the Alt key ... and
  click anywhere in the Title tool" to switch between them; "Press and hold the Ctrl and Alt keys" for the Selection
  tool for now; "After you use a tool in the toolbar, the Title tool reverts to the Selection tool ... double-click the
  tool's icon" to keep it.
* The drawing environment (p.332-335): anti-aliased Preview ("When you click a text object for editing, the Title tool
  displays the text without anti-aliasing"), Safe Colors ("By default, Safe Colors is turned on"), the safe title and
  safe action guidelines (on by default), the alignment grid ("The snap grid has four lines for each line in the visible
  grid"), the Video Background button ("green when you are creating a title for display over a video background"; the
  frame at the Record monitor's position; "Click again in the Title tool to activate its window. The background
  updates"), the Background Color window ("The default background color is black"). Object > Preview, Safe Colors,
  Safe Title Area/Global Grid; Alignment > Show Alignment Grid, Align to Grid.
* Text (p.335-340): click and type, Enter for a line return, word wrap at the object's width; "You can resize only the
  width of a text object"; arrow keys move a selected object a pixel; Cut, Copy and Paste; the Font Selection button,
  the point size ("between 5 and 999", a menu of standard sizes, arrows), Bold and Italic (also Object > Bold, Italic),
  Left, Center and Right justification, kerning ("Values are a percentage of the point size"; Alt+Left/Right on
  characters; Loose, Normal, Tight, Pair Kerning), leading ("measured in points, from baseline to baseline ... Positive
  leading values add space"). With the Selection tool a click formats the whole object; with the Text tool highlighted
  characters. Default text attributes: "Click in the Title tool but do not select any text. Change any of the text
  attributes" (p.337).
* Graphic objects (p.340-341): the Square and Rectangle, Oval and Line tools; Shift-drag constrains to a square, a
  circle, a horizontal or vertical line.

### Ours
* A title is objects back to front in pixels of a 1920 x 1080 frame from its top-left, with its background
  (`model::ClassicTitle`); points are those pixels. Text objects keep their left, top and width; their height follows
  the text. Formatted characters are Titler+ runs (titler.h) with the kerning in percent.
* Text is laid out by Titler+'s layout (`media::classicTextObject`), so wrapping, kerning, leading and formatted
  characters are drawn alike in both titlers. A line is two frame pixels wide until 6.8d3's line attributes; objects
  are white and opaque until 6.8d3's colour controls.
* The window's own menu bar holds Edit (Undo, Redo, Cut, Copy, Paste, Delete, Select All: the tool's own steps while it
  is open), Object and Alignment. Pair Kerning is shown on and dimmed: the font's kerning is always used. Loose and
  Tight are 10 and -10 percent; Alt+Right adds and Alt+Left takes away one percent on the highlighted characters, or
  on the character before the cursor.
* A new text object reaches from the click to the safe title area's right edge (at least 120 pixels). An empty text
  object goes when typing ends. The safe action and title areas are 90% and 80% of the frame; the visible grid is 16 by
  9 squares of 120 pixels, the snap grid 30 pixels. Safe Colors holds saturation and brightness to 75% and 90%.
* The New Title dialog's Marquee button is dimmed: Marquee is Avid's own application. The Marquee Title setting's
  Ask Me, Windows > Title Tool and the Font Replacement dialog are not ours yet.
* Saving the title (File > Save Title ...) comes with 6.8d4; until then the window keeps the title while it is open.

## 6.8d2: rolling and crawling titles

### From the guide
* "Rolling titles scroll vertically ... Crawling titles scroll horizontally"; "Rolling and crawling titles are
  non-real-time effects" (p.341-342). "You cannot save a rolling or crawling title with a color background" (p.342).
* Pages: "A page in a rolling or crawling title is a unit the size of a PAL, NTSC, or HD frame", with limits (1080:
  29 rolling, 4 crawling; 720p: 44 and 6; NTSC 67 and 11; PAL 56 and 11); "If you reach the page count limit while you
  are typing text, the title stops getting bigger and text entry stops"; a formatting change that would pass it opens
  a message box (p.342).
* Auto Size mode "adds or removes pages automatically ... on by default when you click the Rolling Title button or the
  Crawling Title button" (Object > Auto Size Mode, p.343). Typing: "lines of text wrap within the safe title area, and
  the title scrolls down as you type"; the button "changes to green, and a Page Number text box appears"; the
  Background Selection tools are set to Video Background (p.343-344). Timeline > New > Title opens the tool (p.343).
* Only the width of a rolling or crawling title resizes (p.344). Object > Make Crawl makes "a single-line text object"
  on "the lower third of the screen" (p.344).
* Scrolling: a scroll bar on the right for a roll, at the bottom for a crawl; Home and End, Page Up and Page Down, and
  for rolls the Up and Down Arrows a line at a time (p.344).
* Pages (p.344-346): the Page Number text box; Object > Add Page ("not available when Auto Size mode is on"), Insert
  Page ("only if the left boundary of an object appears on the page"), Remove Page (an empty one), Copy to Page and
  Move to Page ("automatically adds pages if necessary").

### Ours
* `ClassicTitle::motion`, `pages` and `autoSize`. Pages lie under each other (roll) or side by side (crawl), page 1 at
  the origin (`model::classicPageOrigin`); objects keep title coordinates across all pages. Page limits follow the
  project's frame height (`model::classicPageLimit`; 1080 lines until set).
* The page operations measure text and so are media's: `media::classicContentPages`, `insertClassicPage` (refused where
  an object reaches across the page's start), `removeClassicPage`, `placeOnClassicPage`, `makeClassicCrawl` (its top
  two thirds down the frame, as wide as its one line). `classicTitleImage` draws from any place in the title.
* In the window, every change goes through the limit: a change past it is taken back (typing stops; other changes show
  the message box `title:pageLimit`). The dashed lines are the page edges. The Up and Down Arrows scroll a roll by 54
  pixels when nothing is selected (with a selection they move it). Clicking the lit Rolling or Crawling Title button
  makes the title still again. Saving and playing rolls and crawls come with 6.8d4 and 6.8e.


## 6.8d3a: line attributes, colour and transparency, shadows

### From the guide
* The line attribute tools (p.347-349): the Box Corner button (standard roundings, Custom Radius "whole number (in
  pixels) ... The maximum custom radius is 200"), the Border Width button ("change the width of lines created with the
  drawing tools or to apply a border outline to text or graphic objects"; Remove Border first, Custom Width last), the
  Arrowhead button (Remove Arrowhead, standard styles, Edit Arrowhead with the Arrowhead Size dialog box whose handle
  drags "vertically or horizontally to resize and reshape it. The shape of the arrowhead is in proportion to the line
  width"). "If you do not select an object and you select a line attribute, the Title tool makes that attribute the
  default for any new objects that you create" (p.347).
* Colour and transparency (p.349-352): Color Selection boxes and Transparency Level boxes for Fill, Shad and Bord; the
  Color and Transparency Blend tools "appear when you select a Fill or Border Color Selection box or Transparency Level
  box ... You cannot blend the shadow"; two blend boxes and the Blend Direction box, dragged "clockwise or
  counterclockwise ... The change takes effect as soon as you release the mouse button"; the Transparency Level box
  "displays the text "Hi" with a background that changes from black to white", with a slider (p.352).
* Shadows (p.353-355): depth ("extends from the edges of the object, as though the object were three-dimensional") or
  drop ("offset from the object itself"); the Drop and Depth Shadow button, the Shadow Depth and Direction button
  (Shift-drag for 45-degree angles), Shift+arrow keys, the Shadow Depth text box. Object > Soften Shadow: "integer
  values between 4 and 40 ... If you set the value to zero, shadows on title objects have no softening", Apply and OK,
  Shift+Alt+Up/Down by one; the value "becomes the new default". Depth 0 with softness makes a glow (p.355).

### Ours
* The model: `ClassicPaint` (colour, second colour, transparency, second transparency, two directions) for the fill and
  the border; `ClassicObject::lineWidth`, `borderWidth`, `cornerRadius`, `arrows`/`arrowSize` (in line widths),
  `depthShadow`, `shadowDepth`/`shadowAngle` (`shadowOffset()`), `shadowSoftness`, `shadowColor`,
  `shadowTransparency`. The JSON keeps the old "color" and "transparency" keys for the fill.
* The picture (`media::classicObjectShape`, `classicBorderShape`, `classicTitleImage`): each object's shape (text
  through Titler+'s `titleTextPath`) as a coverage mask; the border is a stroke twice its width outside the fill; the
  shadow is the silhouette (fill and border) drawn once at the offset (drop) or every half pixel along the way (depth),
  coloured and softened (`softenImage`, Titler+'s blur) by the softness in pixels; then the border, then the fill. A
  blend runs across the coat's bounds in its direction, degrees clockwise from left to right. Preview off draws the
  masks without anti-aliasing.
* The window: a row above the toolbar with the Box Corner (Square, radii 10, 20, 30, 50, 80, Custom Radius...), Border
  Width (Remove Border, 1, 2, 3, 4, 6, 8, 10, Custom Width...) and Arrowhead buttons, the Fill/Shad/Bord colour and
  "Hi" boxes (the chosen one framed green), the blend tools and transparency slider, the shadow pad, Shadow Depth box
  and Drop/Depth button. Clicking a colour box chooses it and opens QColorDialog (its Pick Screen Color is the
  eyedropper); clicking a blend box chooses a blend value (a colour opens the dialog; a transparency is set with the
  slider). Remove Border leaves a line's width; a width chosen with nothing selected becomes both the line and the
  border default. Shift+arrows move the shadow a pixel (the object stays); Shift+Alt+Up/Down change the softness; a
  softness of 1 to 3 goes to 4 when rising and to 0 when falling. The softness default lasts for the application's
  session (future sessions: not kept).
