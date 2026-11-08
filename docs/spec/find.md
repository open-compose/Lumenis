# The Find window (UI completion pass U2c)

Edit > Find (Ctrl+F) and Ctrl+G for the next occurrence. Pages are the Editing Guide's.

Code:
* Model: `src/model/text_find.*`: `textMatches`, `searchColumns`, `itemMatches`, `findItems`,
  `findMarkers`.
* The window: `FindWindow` (`src/ui/find_window.*`), opened by `MainWindow` (one window, kept).
  Ctrl+G is the keyboard-only command `edit.find_again`.
* Test: `tst_find_window`.

## Text find (p.428)

* "Text find allows you to enter text and search bins, Scripts and the Timeline for the information you
  enter ... Bins do not have to be open for the system to search in them."
  * Open bins are searched as they are; closed bins are read from their files. Nothing has to be
    indexed or saved first.
  * The Bin Index status always reads ready (ours). PhraseFind AI (8.2c) shares the Clips and
    Sequences tab; it is in docs/spec/transcripts.md, "PhraseFind AI".
* "The system searches through all available columns in your bins including metadata columns, even if
  they are not visible in your current bin."
  * Every standard heading and every custom column is searched.
* "The following characters: @#$%^&*()=+[]\ are recognized by text find." Quotes are matched as
  typed (ours).

## Clips and Sequences (p.429-430)

* The search runs over "Bins in Project", "Scripts in Project", "Bins and Scripts in Project" or
  "Current Bin".
  * Scripts in Project (8.3e) searches the clips the project's scripts link as takes (ours: the guide says only that
    clips are searched "in the current bin and script opened ... or all bins and scripts", p.440); Bins and Scripts
    searches the bins, which hold those clips too.
* "Current Bin: The system searches for the text criteria in the last active bin. The system then
  selects the first occurrence in the bin. Press Ctrl+G ... to take you to the next occurrence."
  * Nothing is listed in the Results (p.430).
* "If you would like to open the clip(s) in a Source monitor when you double-click a clip in the Results
  window, then select Load into monitor."
  * Ours: on by default. Without it, a double-click opens the bin and selects the clip there.
* Ignore Case (ours: on by default) and Whole Words only.
* Filters (p.417, p.430):
  * "Select a specific column from the Filter menu ... then enter additional text relating to that
    column. The column you are searching on does not have to display in the bin."
  * "Click the '+' button to add additional filters. Click the '-' button to remove filters. If the
    last filter appears, the '-' button removes the text, not the filter."
  * "The filter menu populates after the initial search": Any Column, then every column the found items
    have.
* Select Columns (p.417-418, p.429): the bin headings shown in the Results after Name and Bin.
  * Ours: Start, End and Duration by default.
* Effect templates and Source items are not listed (ours).

## Script Text (p.429-430)

* "Select this option if you want the system to search through your scripts. Then choose to search in the current
  script opened in your project or all scripts in the project regardless if they are opened or not" (p.440): Current
  Script (the Script window's) or Scripts in Project (8.3e, `model::findScriptText`).
* "Whole words only: Available when you select Script Text ... if you enter 'eat,' the system will only find eat. Not
  eating, eats, neat" (p.441): the tab's own Whole Words only; Ignore Case is shared.
* The Results list Script, Line and Text (ours: the guide does not name them); a double-click opens the script in the
  Script window with the line selected. The filter rows narrow the hits by their text (ours).

## Timeline and Monitors (p.430-431)

* "Markers: The system searches for clips in the Timeline that contain the marker text ... Clip Names:
  ... the clip name ... Timeline Text: ... based on what is displayed in the Clip Text pulldown menu."
  * Ours: Markers and Clip Names are on by default.
* The Results show Clip name, Track, Start, End, Duration, Comments and Position, as in the picture on
  p.431.
  * Ours: Comments is the marker's comment, or the clip's Timeline clip note. Position is the frame.
* "Double-click an item in the list. The blue position bar moves to the location."
* "Press Ctrl + G to move to the next location in the Timeline ... Ctrl+G always moves the blue bar to
  the next position that is not the current one."
* Ours: the Record monitor's sequence is searched.

## Markers (p.417-418)

* "You can search for text within the Marker Comment field across all sequences and master clips within
  the project, including opened or closed bins."
* Filters work as on Clips and Sequences; they apply to the clip or sequence the marker is in.
* "Double-click a Marker in the results window to load the respective master clip or sequence into the
  source monitor and position the blue bar at the selected Marker."
* Ours: the Results show Name, Bin, Marker Name, Color, Track, Position, Comment and User.
* Not yet: Set Font for the Find window (p.430), with Windows > Set Font in U3.
