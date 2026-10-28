# The Timecode window and Loop Selected Clips (UI completion pass U2b)

Tools > Timecode Window and Clip > Loop Selected Clips. Pages are the Editing Guide's.

Code:
* `TimecodeWindow` (`src/ui/timecode_window.*`), opened by `MainWindow` (one window, kept).
* `Composer::playClipLoop`, `stepClipLoop` (`src/ui/composer.*`), started by `MainWindow`'s
  `clip.loop_selected_clips` handler.
* Test: `tst_timecode_window`.

## The Timecode window (p.388-389)

* "Each monitor has two lines available to display timecode ... In addition, the Timecode window lets
  you display up to 48 lines of timecode in a separate window."
* "Select Tools > Timecode Window. The Timecode window opens. Click in the Timecode window, and select
  an option."
* The Timecode menu, from the picture on p.389:
  * Sequence and Source, each with the tracking formats (Master, Duration, In/Out, Absolute, Remain,
    Clip Name);
  * None;
  * Add Line;
  * Size;
  * "Sequence Timecode :" with the sequence's values.
  * Sequence is the Record monitor's item and Source the Source monitor's. Ours: a submenu is dimmed
    while its monitor is empty.
* "To add an additional line of timecode, click Add Line, then click the new line and select an
  option." A new line shows nothing until an option is chosen. Add Line is dimmed at 48 lines.
* "To change the size of the font displayed in the Timecode window, select Size > font size."
  * Ours: the sizes are 9, 10, 12, 14 (the default), 18, 24, 36 and 48 points.
* Each line has a short label and its value, in green on black as in the picture.
  * The label is the format's first three letters ("Mas"); ours: "I/O" for In/Out, "Nam" for Clip
    Name, and "S " in front for a Source line.
* The window starts with one line, the sequence's Master timecode, as in the picture. Lines are not
  kept between sessions (ours).
* Not yet: the 24p and 25p output and source timecodes (TC 24, TC 25, TC 25P, TC 30; p.388). They
  need the timecode tracks of film projects (Phase 9).

## Loop Selected Clips (p.393)

* "You can view several clips one after another in a continuous loop by selecting Clip > Loop Selected
  Clips."
* "Select the clips in the bin that you want to play in a loop ... The clips begin playing in the Source
  monitor from the IN point to the OUT point."
  * The active bin's selected clips play in the bin's order. A clip without marks plays whole.
* "Press the space bar to stop the play loop." Any stop ends it.
* "While playing the loop, you can jump to the next clip by pressing the Tab key or jump to the
  previous clip by pressing Shift+Tab." The keys work anywhere in the application while the loop plays.
* "If you want to play the clips from start to end, press the Alt key (Windows) or the Option key
  (Mac) while performing this procedure."
