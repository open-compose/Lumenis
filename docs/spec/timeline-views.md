# Timeline views and View Type (UI completion pass U1b2)

Saved Timeline views with the View Menu button and the TV1-TV8 buttons, and the Fast menu's View Type:
Heads and Heads Tails. Pages are the Editing Guide's.

Code:
* `Timeline` in `src/ui/timeline.cpp`:
  * `displayToJson`, `displayFromJson`, `setDisplay`;
  * `viewMenu`, `setViews`;
  * `setViewType`, `headsSpans`, `moveHeads`.
* `TimelineView::paintHeads`.
* `MainWindow::saveTimelineView`, `chooseTimelineView`, `timelineViews`, `timelineViewButtons`.
* `model::Sequence::headsSpans` and `moveSpan`.

## Timeline views (p.604-605)

* "You can save a customized Timeline view. Timeline views appear in the Settings list. You can
  save, rename, and copy multiple views. Media Composer saves the Timeline information from the
  Timeline Fast menu with each view" (p.604).
  * A view is a **Timeline View** User setting: the Fast menu's display options
    (`Timeline::Display`) and the Clip Color choices.
  * "Clip color options are saved when you save a customized Timeline View" (p.595).
  * The Settings list's Duplicate, rename and Delete work on them as on other settings (p.1139).
  * Opening one from the Settings list makes it the view in use. "You cannot modify ... Settings that
    are only modifiable from within the tools in which they are used, such as Timeline views"
    (p.1137), so it has no dialog.
* **Not in a view:**
  * "the Zoom and Focus functions. You cannot save these as part of a Timeline view" (p.592);
  * "You cannot save specific per track settings in a custom Timeline view" (p.677). Allow Per Track
    Settings is in it.
* **The View Menu button** in the bottom toolbar shows the view in use. "The Timeline view is
  labeled Untitled until you name and save a customized Timeline view" (p.604).
  * Its menu lists the views, then Save As (the picture on p.604).
  * Choosing a view applies it.
  * "The View Name dialog box opens. Type a name for the view, and click OK."
    * Ours: a name already used replaces that view.
  * "Press and hold the Alt key ... while you click the View Menu button to display the list of saved
    view names, each appended with the Replace command." Choosing one keeps the current settings
    under that name and makes it the view in use (p.604).
* **Ours:**
  * A view's settings change as soon as you change them in the Fast menu. The view keeps them only
    when you Replace it.
  * The label still names the view.
* **Default Setup** "Returns Timeline display settings to the system default settings" (p.592, p.604).

## Timeline View buttons TV1-TV8 (p.605)

* "The More tab of the Command Palette contains eight Timeline View buttons" (p.605): the commands
  `timeline.view.tv1` to `tv8`. They can be mapped like other buttons.
* "The Timeline View buttons are assigned to your Timeline views in the order that they appear on the
  View menu ... Media Composer sorts the Timeline views alphabetically" (p.605).
  * Each button also has a Timeline View menu beside it in the Command Palette (None and the views).
  * A view chosen there takes the button's place in the order.
  * The choices are kept in the Timeline setting (`viewButtons`).
* "You must create at least one Timeline view to use the Timeline View buttons": a button with no
  view does nothing.
* A button shows the first two characters of its view, as the W buttons do (p.90). The characters
  are italic when the view is gone.

## View Type: Heads and Heads Tails (p.592, p.638-639)

* "Click the Track buttons to select the tracks to be edited. Click the Timeline Fast Menu button, and
  select View Type > Heads or Heads Tails" (p.639).
  * Ours: choosing the one in use again goes back to the tracks. The guide names no item for that.
  * Default Setup also goes back to the tracks.
* **The clips.** "If you rearrange a split edit ... the system cuts all tracks to the same edit point"
  (p.638): the clips are the record sequence cut at every edit point of the selected tracks.
  * Ours: with no track selected, every track's.
* "Heads view shows the Head frame for each clip. Heads Tails view shows both the head and tail frames
  for each clip" (p.639).
  * The frame is the top selected video track's.
  * A clip without a picture shows its name.
  * The Track Selector shows "H" (the picture on p.639).
  * Ours: the boxes are 16:9 frames 56 pixels high, in one row, made narrower when they do not fit.
  * The clip under the position indicator is outlined in the indicator's colour.
* **Rearranging.** "Press and hold the Alt key ... click the frames representing the clip you want to
  move, and drag the clip to its new position. The sequence is rearranged" (p.639).
  * The move takes one undo step ("Move Clip").
  * It is refused on a locked track.
  * A click without Alt moves the position to the clip's head (ours).
* "Heads and Tails view is disabled when you are displaying material from the Source monitor" (p.603):
  the Timeline shows the tracks then, and View Type is dimmed.

## Later

* Show Adapters (p.599) and Render Ranges (FX guide) come with Phase 6, with the adapters and
  partial renders they show.
* Printing the Timeline in Heads views (p.640-641) comes with printing (Phase 9).

## Tests

* `tst_timeline_views`:
  * the clips and moves in the model;
  * the display's JSON;
  * Save As, choosing and Replace;
  * the setting;
  * TV1-TV8 in order and from the Command Palette;
  * Heads and Heads Tails: clicks, Alt+drag, undo, selected tracks only, the Source monitor.
* Scenario `19-heads-move.json`.
