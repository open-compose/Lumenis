# Accessibility (10.3)

Ours: the guides say nothing about screen readers. 10.3 makes Cutthulhu usable with VoiceOver (macOS), Narrator and
NVDA (Windows) and Orca (Linux). Qt describes every widget to them through `QAccessibleInterface`; standard widgets
describe themselves, but a button that shows only an icon, or a widget we paint ourselves, has no name unless we give
it one, and a reader then says only "button".

## Names (10.3a)

* `installAccessibleNames()` (`src/ui/accessibility.*`), installed once by `MainWindow`, names every control that has
  no name of its own from what a sighted user sees: a button's text (Qt's own name), else its tooltip without rich text
  or mnemonics, else a line edit's placeholder; a spin box's own text field takes the spin box's name (its form label
  through Qt's interface). It follows the tooltip when it changes (a button mapped to another command). A name set in
  the code with `setAccessibleName()` is left alone.
* Named in the code, as they have no tooltip: the Composer, the Timeline and its scale bar, the bin tree, bin tabs and
  bin contents, the monitors' clip name menus and tracking displays, a blank Composer button ("Blank button"), the
  Audio Mixer and its tracks, fader and level, the Audio Punch-In's preroll and postroll, the Effect Palette's lists,
  the Effect Editor's parameters, the Command Palette's tabs, the Console, the Calculator, the Markers and Clip Notes
  tables, the Source Browser's places, folders and files, the List Tool's tabs and lists, the Background Queue's jobs,
  the Transcript Tool's text and the Dynamic Media Folders list. The names are ours (plain descriptions).
* `tst_accessibility` opens the main window with a project and every window of the Tools menu (closing any dialog
  that would wait for an answer) and fails for a control a user operates (a button, or anything that takes focus)
  without a name. 544 controls are checked.

## What the widgets we paint are (10.3b)

`installAccessibleNames()` also installs an accessibility factory (`QAccessible::installFactory`) for the widgets we
paint ourselves, which Qt would otherwise describe as a nameless "client":

* **The Timeline** (`TimelineView`) is a pane whose children are its tracks, top to bottom as shown; each track is a
  list ("Track V1, 3 segments", with its custom name if it has one) whose items are its segments: "Interview, starts
  00:00:00:00, length 00:00:02:00", or "Filler, ...". The interfaces read the sequence as it is each time they are
  asked (one per track and per segment index, kept and reused), so they never point at a segment that has gone.
  The pane's description gives the number of tracks and the sequence's length. Rectangles are the segments' places,
  so a reader's pointer and highlight follow them.
* **A position bar** (`PositionBar`) is a slider ("Source monitor position", "Record monitor position") whose value
  is the monitor's frame, from the first to the last frame it spans; a reader can set it, which moves the position.
* **A monitor's picture** (`MonitorPicture`) is a graphic ("Source monitor") whose description is what is loaded and
  where ("Cut 1, frame 11 of 150"), or "Empty".
* **The graphs, wheels, meters and previews** (Color wheel, Chroma curve, Histogram, Luma ranges, Vector wheel, Safe
  colors, EQ graph, Pan, Angle, Audio meters, Brush preview, Framing, Keyframes, Motion graph, Tracking data, Sequence
  map) are graphics named by what they are, unless the code names them. EqGraph, KeyframeTrack and TrackerTimeline
  have `Q_OBJECT` so their class names reach the factory; the Title Tool's canvas is named in the code.
* Also named in 10.3b: each Effect Editor parameter's control after its row's label, the Audio Mixer's pan dial and
  value, the Title Designer's Title Text and Dashboard, and an editable combo box's text field after the combo box.
* `tst_accessibility` reads a sequence back through the Timeline's interfaces, the Record monitor's position bar
  (and moves it) and picture, and fails for a painted widget that is shown without a role or a name.

## The keyboard (10.3c)

Tab is the guide's Play key (keyboard.json), so in the Composer and the Timeline it plays rather than moving the
focus; and 153 of the 409 commands are on buttons, palettes or right-click menus only, with no menu item or default
key. Ours, so a keyboard user reaches everything:

* **Between panels.** Cycle windows (Ctrl+Up / Ctrl+Down, p.78) and Next Tab (Ctrl+Tab) made a panel active without
  moving the keyboard focus; `PanelManager::focusInto` now puts the focus on the panel's first control in Tab order
  (else the panel), so the next key acts there.
* **Every command by name.** Help > Find Command... (Ctrl+Shift+P; `src/ui/find_command.*`) lists every command that
  can run now, with its place in the menus and its key; typing filters it (every word must appear), Up and Down
  choose, Enter runs the chosen command where the focus was, Esc closes. It is in the menu inventory as an addition.
* **Focus that can be seen.** The widgets we paint draw no focus of their own; when one of them gets the focus from the
  keyboard (Tab, Shift+Tab or a shortcut such as cycling the panels) a ring in a lighter Highlight colour
  (`focusRingColor`, 10.3d) is drawn over its
  edge, and it goes with the focus. A click shows none (as a browser's :focus-visible), so mouse users and the
  screenshots see nothing new.
* `tst_accessibility` checks that cycling the windows leaves the focus in the active panel, that the ring follows
  keyboard focus and not clicks, and that a command on no menu and no key runs from Find Command with Enter.

## Contrast and large fonts (10.3d)

* **Contrast** (WCAG 2.2, ours to follow: 4.5:1 for text, 1.4.3; 3:1 for a focus indicator, 1.4.11). The theme's text
  colours measure 6.8 to 10.5:1 on what they are drawn on, highlighted text 4.6:1. Two did not pass and changed: the
  placeholder text (drawn in the dimmed colour, 3.6:1) is #9a9a9a, about 5:1 on fields; the focus ring, the Highlight
  colour at 2.5:1 on the window, is that colour lighter by 40% (`focusRingColor`), above 3:1 on the window, fields and
  buttons. Dimmed text stays dim: WCAG exempts controls that cannot be used.
* **Large fonts.** Item view rows were a fixed 18 pixels, so a larger font chosen in the Interface settings (Override
  font size for all Bin Container Sidebars, Override all bin font sizes, p.1201) or with Set Font was cut. Rows now grow
  by as much as the font is taller than the application's (`rowHeightFor` in theme.h, used by the style's item view
  rows and the bin's table); the application's own font keeps the old heights on every platform, so screenshots match.
* `tst_accessibility` checks the palette's text pairs and the ring with `contrastRatio`, and that 28-point bin and
  Sidebar fonts make rows at least as tall as the text, and that the rows go back.
* **By hand:** docs/checklists/screen-readers.md, 23 tasks with what VoiceOver, Narrator or NVDA and Orca should say,
  for the owner to run on each platform.
