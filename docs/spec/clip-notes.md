# Timeline Clip Notes (UI completion pass U1c2)

Notes on the clips of a sequence: Add Timeline Clip Note, Clip Text > Timeline Clip Notes, and the
Timeline Clip Notes window (Tools > Timeline Clip Notes). Pages are the Editing Guide's.

Code:
* `model::Segment::note` in `src/model/sequence.*`.
* `Timeline::setClipNote` and `addClipNote` in `src/ui/timeline.cpp`.
* `ClipNotesWindow` in `src/ui/clip_notes.*`.

## Adding notes (p.483)

* "Click one of the Segment buttons ... and highlight the clip to which you want to add a note in the
  Timeline. Right click and select Add Timeline Clip Note. The Timeline Clip Note dialog box opens.
  Type your notes in the text box, and click OK" (p.483).
  * The note goes on the selected segments, or on the one right-clicked when none is selected (ours).
    It is one undo step.
  * The dialog starts with the first segment's note, so the same item also changes a note (ours).
  * Filler takes no note.
* The note is kept on the segment in the sequence (`note` in the composition). A segment cut in two
  keeps it on both parts (ours).
* "You must enable the Timeline Fast menu > Clip Text > Timeline Clip Notes to display the notes in the
  Timeline" (p.483). The note is drawn after the other Clip Text, in the menu's order.

## The Timeline Clip Notes window (p.484-486)

* "Select Tools > Timeline Clip Notes. All the Timeline notes for the selected sequence appear in the
  window" (p.484): the Record monitor's sequence, a row for each clip with a note.
* **Columns** (the picture on p.484): Clip Name, Start, Track, End, Duration and Timeline Clip Note.
  * Start and End are record timecodes. End is the clip's last frame: in the picture, Start plus
    Duration less a frame.
  * "Choose Columns" chooses which show (p.484). Frame, "A representative frame of the clip" (p.485),
    is one of them and starts hidden.
  * "You can choose to display the Duration column information as either Timecode or Frames"
    (p.484): Show as Frames and Show as Timecode.
  * "Click the column heading. To reverse the order, click the column heading again"; "Click the column
    heading and drag it to the desired location" (p.485).
    * Ours: the rows start sorted by Start.
    * Timecodes and durations sort by frame.
* **Editing.** "Double-click the note you want to edit in the Timeline Clip Note column, type new text
  and press Enter. The clip note is updated in the Timeline Clip Notes window and in the sequence"
  (p.484). Only that column can be edited.
* **Deleting.** "Right-click the note and select Delete. Or, click to highlight the note and press
  Delete" (p.484). Backspace too (ours).
* **Gang Mode.** "Click the Gang button at the bottom of the window. It will highlight green when in
  gang mode. Select and multi-select items ... The corresponding clips are highlighted and selected
  in the Timeline ... If one clip note is selected, the blue bar will move to the beginning of that
  clip" (p.485).
* **Export to Text** writes a tab-delimited .txt of the shown rows and columns in their order on
  screen (p.485). Ours: a row of headings first; the Frame column is left out.
* **The Frame column's size:** Enlarge Frame and Reduce Frame from the right-click menu, Ctrl+L and
  Ctrl+K (Edit > Enlarge/Reduce Frame while the window is active) (p.485-486).
  * Ours: 36 pixels high to start, doubling or halving, from 18 to 144.
* **Quick Find:** "The window displays any clips that include the text entered in the find field in any
  column" (p.486).
* **Set Font:** "Right click in the Timeline Clip Notes window and select Set Font", or from the
  window's Fast menu (p.486). Windows > Set Font comes with U3.
* **Fast menu** (the picture on p.484, bottom left).
  * Ours: Choose Columns, Show as Frames, Show as Timecode, Enlarge Frame, Reduce Frame, Export to
    Text and Set Font.
  * The right-click menu is the same with Delete first.
* The window follows the record sequence. Only a change of its contents refills the rows, which keep
  their selection; moving the position does not refill them.

## Later

* Notes nested below effects: "Additional Notes may be present in nest below" (p.484) comes with
  nesting.
* Notes in EDLs and cut lists (p.483) and in the Timecode Burn-In effect (FX guide).

## Tests

* `tst_clip_notes`:
  * the JSON;
  * Add Timeline Clip Note, with undo, Cancel and filler;
  * Clip Text;
  * the window: columns, timecodes, Show as Frames, sorting, editing, Quick Find, Export to Text,
    Gang mode, frames, Delete;
  * Tools > Timeline Clip Notes.
* Scenario `20-clip-notes.json`.
