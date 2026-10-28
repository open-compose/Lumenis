# AutoSequence (UI completion pass U2d)

Bin > AutoSequence and Clip > AutoSequence. Pages are the Editing Guide's.

Code:
* `model::autoSequence` (`src/model/auto_sequence.*`).
* `MainWindow::autoSequence` (`src/ui/main_window.cpp`).
* Test: `tst_autosequence`.

## What it does (p.499-500)

* "To establish sync with the original videotape, use filler to add where gaps in audio or video exist
  in the sequence."
  * The active bin's selected clips are placed whole, each at its Start timecode less the earliest
    one's, with filler between.
  * The sequence starts at the earliest clip's timecode.
  * Each clip's tracks go to the tracks of the same name.
* "Press and hold the Alt ... key while you select Bin > AutoSequence to build a sequence without
  filler. The system creates a sequence without gaps by placing the clips in ascending timecode order."
* "Use only master clips, subclips, autosynced subclips, and/or group clips."
  * Ours: master clips, subclips and audio clips; others are left out. A selection with none of them is
    refused.
* "If you select two unrelated clips with overlapping timecodes, a message box indicates you cannot do
  this operation. If the clips are related (for example, one clip is a subclip of the other master
  clip), then one of the clips is selected automatically."
  * Ours: the master; of two subclips of one master, the longer.
* "The system removes and ignores points in the clips": whole clips are used.
  * Ours: there is no question first, because our bin clips carry no marks.
* "Synchronized sequences are named from the Tape Name column for tape-based media and the Source File
  column for file-based media", "with a .xx (.01, .02, .03) extension" (p.500).
  * Ours: the clip's name when it has neither. The number is the first one free in the bin.
* "The new sequence appears in the Record monitor and in the Timeline. The sequence also appears in the
  bin."
* "Both AutoSequence and Spot to Timecode can use the Camera column in a bin to distinguish between
  different sources and automatically place the corresponding clips on separate tracks."
  * Ours: clips without a Camera value come first. Each further Camera value, in sort order, gets
    video and audio tracks after those of the groups before it.
* Not yet: group clips (multicamera, Phase 9) and Spot to Timecode.
