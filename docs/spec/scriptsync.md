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
a script is opened. Take Coloring, Pre-roll, Show Frames, Show All Takes, Interpolate Position and Hold Slates
Onscreen are dimmed until 8.3b-8.3c.

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

## Later

* 8.3b: slates and takes, indicators, Show Frames and the other slate settings.
* 8.3c: script marks, Find Script and Find Bin, splicing a script range.
* 8.3d: ScriptSync AI and Create Script from Clip.
* 8.3e: shared scripts' locks; the Find window's Script Text tab.

## Tests

* `tst_script`: importing text, scene and page numbers, the script item, editing with the numbers following the lines,
  find and replace.
* `tst_script_ui`: New Script into the active bin, AS/AP, the status bar and menus, Edit Mode, Find and Replace, the
  Delete dialog, Text Layout, Save Script, reopening, Save a Script Copy As.
