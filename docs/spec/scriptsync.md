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
(8.3b; the Script menu changes them for the open window only, p.570), and Pre-roll (8.3c, p.588-589).

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

## Script marks (p.582-589, 8.3c)

A take's marks (`ScriptTake::marks`) sync script lines to clip frames: "When you place a mark in the script, an IN point
also appears in the clip when you load it into a monitor" (p.582). A mark is drawn as a small bar across the take's line
in the middle of its line (p.583).

* Add Script Mark (the Command Palette's Other tab, p.582-583): after a double-click at a take and a line (which selects
  the take, highlights the line and loads the clip), the take's line is synced to the Source monitor's position; play
  stops. Ours: the take is the one last loaded from the window and the line the first selected one; without the take's
  clip in the Source monitor the Console says so.
* The Record button (p.583-584): the selected takes (without any, the takes over the selected lines, "load all the
  coverage for a range of lines", p.585) play one after another in the Source monitor (the Composer's clip loop, so Tab
  and Shift+Tab step takes and the space bar stops); the playing take's line is green. A click on a line marks the
  playing take there at the playing frame; a line with a mark gets the new frame; a line outside the take extends it.
  The Play button plays the same takes without marking.
* Double-click on a mark (p.585): the take is loaded, cued to the mark with an IN there; the Script setting's Pre-roll
  (seconds) puts the position that much earlier ("The pre-roll is included when you access the script mark", p.589).
* Moving and deleting (p.586): Ctrl+drag a mark to another line of the take (the frame stays); a click selects a mark,
  and Delete offers "Delete 1 mark(s)"; with takes selected, the marks on the selected lines are counted instead.
* Find Script (the Other tab, p.586): from the Source monitor's clip and position, the take whose mark is the last at or
  before it; the lines from that mark to the line before the next mark are selected. Find Bin (toolbar, p.586-587): the
  selected takes' bins are opened and their clips selected.
* Splicing a Script Range (p.588): Ctrl+Alt+double-click on a take splices the clip from the mark at or before the
  selected lines' first line to the frame before the next mark after them (the clip's end without one) at the Record
  monitor's IN or position; the first edit makes a sequence. A range before every mark splices nothing (ours).

## ScriptSync AI and Create Script from Clip (p.569-570, p.584-585, 8.3d)

`src/model/script_sync.*`, `src/ui/script_sync_dialog.*`, `MainWindow::scriptSyncAI`/`createScriptFromClips`.

* Script > ScriptSync AI works on the selected takes, else the takes over the selected lines ("Select one or more takes
  that include audio. Select all clips and takes in the Script that you want marked up", p.584). The dialog (p.585):
  Tracks (the takes' clips' audio tracks, all checked), Skip lines that only contain CAPITAL letters, Skip text in
  parentheses or brackets, Skip text before colon, Skip lines indented less than dialog with Dialog Indent (characters)
  and Select Dialog (the indent of the Script window's selected line), Overwrite existing marks, Sync between first and
  last mark. Ours: the options are kept for the session; the Dialog Indent starts at 10.
* "ScriptSync AI uses the PhraseFind AI engine to analyze the audio portion of a clip" (p.584). Ours: the clip's
  transcript (8.2) at the clip's frames is the analysis. The words of the take's lines and the transcript's are aligned
  by their longest common sequence, compared as PhraseFind compares words; a line is marked at the frame of its first
  matched word when at least half its words are matched. Without Overwrite, marked lines keep their marks. With Sync
  between first and last mark, only the lines and frames between the take's first and last marks are used. Clips
  without a transcript are queued for transcription and the Console says to run ScriptSync AI again; there is no
  progress tracker or Ctrl+. (the alignment takes moments), and alignments of more than 16 million word pairs are not
  tried.
* Transcript > Create Script from Clip on the bin's right-click menu (p.570): a script item named "<clip> Script" in the
  clips' bin, its lines the clips' transcript lines as "Speaker: words" with an empty line between clips, each clip a take
  (slate) over its lines with every line marked; it opens in the Script window. "Just click a line of text to jump to its
  corresponding location": in such a script (`Script::fromClips`) a click on a marked line loads the take cued there.

## Shared scripts (p.589-590, 8.3e)

A script is kept in its bin item, so it is shared through its bin's lock (3.7, docs/spec/bin-locking.md); Avid's "This
feature is enabled only when the Project is stored on an Avid NEXIS workspace" is ours for any shared storage, as for
bins.

* The script lock icon (the toolbar's `scriptLock`, shown only for a shared bin) takes the bin's lock colours: red,
  another user holds it; yellow, "the owner of the shared script has made changes" and the title says "(modified)" (ours:
  the bin tabs' wording); blue, released; green, this user holds it (ours: dark red for a protected bin).
* "If you click the yellow icon, the script refreshes and the new refreshed script displays the updated content and returns
  the icon red. ... If you click the blue icon, you now control the script and the icon turns green ... You can release
  the lock by Alt + clicking the green icon" (p.589): the bin's own lock button does it
  (`BinContainer::lockButtonClicked`); the script is read again afterwards (a released script only when unchanged here);
  releasing saves a changed script first.
* A script someone else holds can be changed but not saved. Closing it with changes (File > Close with the Script
  window active, or opening another script) asks Save Script As (a new script item, in the active bin when it can be
  changed, else in the first open bin that can), Don't Save or Cancel (p.590).

## The Find window (p.429-430, p.440-441, 8.3e)

The Script Text tab searches the current script or every script in the project, with its own Whole Words only, and a
result opens the script at its line; Scripts in Project on the Clips and Sequences tab finds the clips the scripts link
as takes (docs/spec/find.md).

## Tests

* `tst_script`: importing text, scene and page numbers, the script item, editing with the numbers following the lines,
  find and replace; slates and takes (adding, moving, lines, frames, indicators, Interpolate Position, JSON); script marks
  (adding, extending, Find Script's lines, marked frames, moving, deleting, through line edits); `scriptSync`: the skip
  options, alignment with a changed word, Overwrite, Sync between first and last mark, scripts from transcripts;
  `findInScripts`: Script Text with Whole Words and case, the clips scripts link.
* `tst_script_ui`: New Script into the active bin, AS/AP, the status bar and menus, Edit Mode, Find and Replace, the
  Delete dialog, Text Layout, Save Script, reopening, Save a Script Copy As; dropping clips, tabs, Ctrl+click and the
  lasso, the arrow keys, Set Offscreen and Set Color, Show All Takes and Show Frames, Enlarge Frame, moving slates,
  loading and playing takes, Delete Takes; `scriptMarks`: Add Script Mark, cueing with the Pre-roll, Find Script, the Record
  button marking the playing take, moving a mark, splicing a script range, Find Bin, Delete mark(s);
  `scriptSyncAndCreateScript`: the dialog's tracks and Select Dialog, marks from a transcript, Create Script from Clip and
  its click to jump; `sharedScripts`: two users on a shared bin, the lock icon's colours and clicks, "(modified)", closing a
  locked changed script (Cancel, Save Script As), the Find window's Script Text and Scripts in Project.
