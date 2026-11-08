# Script-based editing and ScriptSync AI (8.3)

Editing Guide ch.18, p.566-590, and the Script setting, p.1217. Pages are the Editing Guide's. 8.3 is in five parts
(ROADMAP); this page grows with them.

Code:
* Model: `src/model/script.*`: `Script` (lines, scene and page numbers by the line they start at, `ScriptLayout`),
  `scriptFromText`, the script item (`BinItemKind::Script`, `scriptOf`/`setScript`/`scriptItem`), `setScene`/`setPage`,
  `deleteScenes`/`deletePages`, `replaceLines`, `findInScript`/`replaceInScript`.
* The window: `ScriptWindow` (`src/ui/script_window.*`) as the "Script" panel; `main_window_script.cpp` with
  `MainWindow::newScript`, `openScript`, `saveScript`, `saveScriptCopyAs`, `scriptTextLayout`.
* Tests: `tst_script`, `tst_script_ui`.

## Scripts and where they are kept (8.3a)

* "You begin the script integration process by importing a script into a project. The script appears in a script bin,
  and opens in a Script window" (p.570). File > New > New Script: "Open the bin to which you want to import the script
  ... Navigate to the script you want to import" (p.571): the text file becomes a script item in the active bin, named
  after the file, and opens.
* "The imported script must be in text format ... 'Text Only with Line Breaks'" (p.571): a line for each row of the
  file, any platform's line ends; ours: tabs become spaces to the next multiple of 8 columns.
* "Media Composer saves Script window files in the project folder along with bins, and stores backup copies
  automatically in the Avid Attic folder ... the saved file has an .avc file name extension" (p.570). Ours: the .avc
  format is undocumented, so the script is kept inside its bin item (`kScriptKey` in the item's composition), saved,
  auto-saved and kept in the attic with the bin. Double-clicking the item opens it.
* "When you make changes in the Script window, an asterisk appears in the title bar" (p.570): the panel's title. File >
  Save Script writes the window's script into its item; File > Save a Script Copy As makes a new item in the same bin
  (named in a dialog) and shows it, the original keeping what was last saved. Ours: a changed script is saved when
  another one is opened into the window.

## The Script setting (p.1217)

User setting "Script": Font, Size (12), Left Margin (40 pixels), Word Wrap, Take Coloring, Pre-roll, Show Frames, Show
All Takes, Show Line Number, Interpolate Position, Hold Slates Onscreen, Script Window Background (Interface Setting:
Rows, White). "Changes that you make to the Script Settings window will only apply to newly created scripts" (p.570):
the font, size, left margin and Word Wrap are taken when a script is imported; Show Line Number and the background when
a script is opened, as are Take Coloring, Show Frames, Show All Takes, Interpolate Position and Hold Slates Onscreen
(8.3b; the Script menu changes them for the open window only, p.570). Pre-roll is dimmed until 8.3c.

## The Script window (p.570-576)

* The left margin shows scene numbers ("The scene number appears in the left margin", p.574) and, with Show Line
  Number, line numbers; the right margin page numbers ("The page number appears in the right margin next to the first
  line of the selected region"); with Rows, every other line is shaded.
* Script > Text Layout (p.571-572): Left Margin and Text Width in pixels and Word Wrap ("If Word Wrap is not checked, the
  Text Width value is ignored"); kept with the script. Ours: a text width of 600 pixels to start with.
* Navigation (p.571): the scroll bar, Page Up/Down, Home/End; out of Edit Mode the Up and Down Arrow keys move the line
  selection, and a selection covers whole lines.
* Add Scene (AS) and Add Page (AP), the toolbar's buttons and Script > Add Scene/Add Page (p.574): a dialog asks for the
  number of the scene or page beginning at the first selected line; giving a line's number again changes it. "Each
  scene or page number continues throughout the script until you mark another line": the status bar shows the scene,
  page and line at the cursor (ours: "Scene 33   Page 12   Line 4"). An empty number removes it (ours).
* "Press the Delete key. The Delete dialog box opens. Select the options for Delete scene(s) or Delete page break(s)"
  (p.574) over the selected lines.
* "Select the Scene or Page pulldown menu and select the page or scene you want to access ... The Script window scrolls
  to the selected page or scene, and the text is highlighted" (p.575): the lines up to the next number.
* Edit Mode (p.573): "The Script Window outlines in purple"; text is typed, cut, copied and pasted; "If you use the
  enter key while editing text, a new row is created". The scene and page numbers stay with their lines (the changed
  lines replace the old ones, `model::replaceLines`). Ours: undo is the editor's own (Ctrl+Z in the window), not Edit >
  Undo Edit Script Text.
* Show Find Tools (p.575-576): Find with the arrows (going round, ignoring case), Replace and Replace All.

## Slates and takes (p.576-582, 8.3b)

A script holds slates (`model::ScriptSlate`), each with its takes (`model::ScriptTake`): a clip (its bin, item, name and
Take column), the lines it covers, its representative frame, and its off-screen and colour ranges (`ScriptRange`).

* "Select the portion of the script that is covered by the first clip or clips ... and drag them to the highlighted
  text. A slate frame appears above the text, with one or more of the takes covering the scene as lines" (p.576): bin
  items dropped on the Script window make a slate with a take for each over the selected lines; dropped on a slate, they
  are new takes of it (p.580). Scripts and sequences are not taken (ours).
* Drawing: the slate's frame (Show Frames) and the active take's clip name, the tabs below with the Take column's numbers
  (p.580), and each take's line down through the lines it covers, ending in a mark. Ours: slates start halfway across the
  text column, a new slate right of those already over its lines; the slate stands above its line, and while a script
  has slates the text starts a slate's height down so the first lines have room. Take lines are drawn in Take Coloring
  (ours: blue to start with); selected takes in red ("The outline of the take changes to red", p.580).
* Selecting (p.578, p.580): a click on a tab selects the take and shows it in the slate; Ctrl+click and Shift+click add
  or remove takes; a click on the slate selects its active take (Shift: adds it); a lasso dragged from a free part of the
  slate area selects every slate and take it touches. Show All Takes off shows only the active take of slates with no
  selected take.
* Moving (p.579): dragging a slate moves it (the take lines stay over their text); Ctrl+drag moves the take lines with
  it, covering as many lines. Ctrl+drag at a take's end moves that end to the line under the pointer (p.581).
* Delete (p.579-580): the Delete dialog's Delete Takes deletes the selected takes; a slate left without takes goes; the
  clips stay in their bins. Shift+clicking every tab selects a whole slate (`ScriptWindow::selectSlate`).
* "Press the appropriate arrow keys or step keys ... to advance the footage displayed in the slate" (p.581): Left and
  Right step the selected takes' frames by one (ours: Shift by ten), never before the first frame.
* Loading (p.580-581): double-click on a tab loads the take into the Source monitor at its representative frame (several
  selected: each in turn, the clicked one shown); double-click on its line too, and with Interpolate Position the monitor
  goes where that point falls in the clip, the take's lines matched to the clip's length (p.577,
  `model::interpolatedFrame`). The toolbar's Play button plays the selected takes one after another in a loop
  (`Composer::playClipLoop`) until stopped.
* Set Offscreen and Set Color (p.581-582), toolbar buttons: on the selected lines of the selected takes, a jagged line
  (off-screen dialog) or a coloured band in the colour Script > Color chose; clicking again takes it off. "The first take
  in the selected region determines the color indicator status that displays in the Set Color button". Ours: Script >
  Color's colours are Red, Orange, Yellow (to start with), Green, Blue and Purple.
* Script > Show Frames, Show All Takes, Hold Slates Onscreen ("each slate remains on screen as long as the take lines to
  which it is linked remain on screen", p.577) and Interpolate Position carry check marks (p.578); Edit > Enlarge Frame and
  Reduce Frame (Ctrl+L, Ctrl+K) with the Script panel active resize the slates.
* Alt+click on a tab: the guide opens the Info window (p.572). Ours: the Info window is not built yet, so the clip's
  name, bin, take, lines and length go to the Console.

## Later

* 8.3c: script marks, Find Script and Find Bin, splicing a script range.
* 8.3d: ScriptSync AI and Create Script from Clip.
* 8.3e: shared scripts' locks; the Find window's Script Text tab.

## Tests

* `tst_script`: importing text, scene and page numbers, the script item, editing with the numbers following the lines,
  find and replace; slates and takes (adding, moving, lines, frames, indicators, Interpolate Position, JSON).
* `tst_script_ui`: New Script into the active bin, AS/AP, the status bar and menus, Edit Mode, Find and Replace, the
  Delete dialog, Text Layout, Save Script, reopening, Save a Script Copy As; dropping clips, tabs, Ctrl+click and the
  lasso, the arrow keys, Set Offscreen and Set Color, Show All Takes and Show Frames, Enlarge Frame, moving slates,
  loading and playing takes, Delete Takes.
