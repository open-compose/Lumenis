# The Digital Cut tool and decks (Phase 7.3a)

File > Output > Digital Cut: "The Digital Cut tool provides controls when you
record a sequence to tape" (Editing Guide p.926-938).

## Decks (`ext::DeckProvider`, lumenis/ext/deck.h)

A deck provider lists its decks (online or not) and opens one as a
`DeckSession`: its position in timecode frames, `cue`, and a recording made
frame by frame (`beginRecord` with an Insert, Assemble or Crash mode, the tape
IN, the raster and rate, the tape tracks; `writeFrame` with RGBA pictures and
interleaved 48 kHz sound; `finish`; `halt` keeps what was recorded).

The built-in deck (`media::FolderTapeDeckProvider`) is a tape kept in a folder:
`media::setTapeDeckFolders` or `LUMENIS_TAPE_DECKS`; a folder that is not there
is listed offline, in italics (p.928). Each recording is a MOV of PNG pictures
and 24-bit PCM (`rec_<n>.mov`) listed in `tape.json` with its IN, length, tracks
and mode. A later recording covers what it overlaps, as an insert edit; an
assemble edit also blanks the tape after its OUT; the tape's position is kept.
`media::tapeFrameAt` reads the picture at a tape frame back. Decks under RS-422
or behind AJA and Blackmagic hardware are modules (7.3c).

## The tool (`DigitalCutTool`, `MainWindow::openDigitalCut`, `digitalCut`)

* Output Mode (ours: Full Quality only, at the project's raster) and Bit Depth.
* Entire Sequence ("ignore any IN or OUT points"; off: the Record monitor's
  marks), Digital Cut Safe Mode (on by default; ours has no real-time effects to
  warn about, every frame is composed), Stop on Dropped Frames (no frames are
  dropped onto a folder tape), Add Black at Tail with its duration, LTC out
  during preroll (with LTC output, 7.3b).
* Deck Control: Remote or Local; the Deck Selection menu with Adjust Deck,
  Auto-configure and Check Decks (p.927); Insert Edit, Assemble Edit or Crash
  Record; Sequence Time, Record Deck Time, Mark In Time, and with Crash Record
  Ignore Time (p.929-931); Custom Preroll; Custom Frame Offset -5 to 5. Local
  mode records where the deck is, as a crash record (ours: the folder deck has
  no buttons of its own).
* The Sequence Track buttons (the tracks in the cut) and the Enable Track
  buttons V and A1-A4 (the tape's tracks recorded onto, Remote only). Our mix is
  stereo: the first two audio tracks enabled take it.
* Preview Digital Cut cues the deck and says the tape range it would record
  ("does not actually change the master tape", p.928); Play Digital Cut records;
  Halt Digital Cut stops between frames, keeping what was recorded.

The frames come from the export's picture and sound (`exportMovieFile` with the
"sink" format and the tool's tracks), as fast as they can be made rather than in
real time (ours: the folder deck needs no real time; a hardware deck will).

## Not yet

Output formats and pulldown for 23.976p, 24p and 25p projects (p.932-938), the
destination timecode rate, LTC, a real-time digital cut through video output
hardware, Deck Settings, the Capture tool's deck controller.

## Tests

`tst_digital_cut`: No Deck; a preview leaving the tape blank; an insert edit at
Sequence Time with black at the tail; an insert at a Mark IN with a frame offset,
video only; a sound-only crash record with Ignore Time; an assemble edit of the
marked frames blanking the tape after its OUT; Local mode.
