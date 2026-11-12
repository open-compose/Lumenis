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

## Still to do

* 10.3b: roles and contents for the widgets we paint (the Timeline's tracks and segments, the monitors' pictures and
  position bars, the graphs and wheels), so a reader can say what they are and what they show.
* 10.3c: the keyboard: focus order in each window, every command reachable without the mouse (the Keyboard setting
  covers commands; the panels and lists need Tab order), and focus that can be seen.
* 10.3d: contrast and large fonts against the Interface settings, and the owner's checks with VoiceOver, Narrator or
  NVDA and Orca (a checklist in docs/checklists/).
