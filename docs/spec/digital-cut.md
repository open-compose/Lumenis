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

## Output formats (p.932-938, p.1298-1299; 8.7c2b)

* "Click the Output Format menu, and select a play rate. A brief description of
  each output format is displayed in the Digital Cut tool" (p.933): the menu
  (`dcOutputFormat`, `DigitalCutTool::setOutputFormats`) holds
  `model::outputFormats` of the project (`src/model/output_format.*`), the
  project's own play rate first, and the description under it.
* SD 23.976p and 24p NTSC projects: 23.976 (NTSC), 24 (NTSC), 29.97 (NTSC); SD
  24p and 25p PAL projects: 24 (PAL), 25 (PAL) (the table on p.933-935).
  "Depending on the type of project you are working with, NTSC or PAL, the system
  will only display output options for one format" (p.933). 1080 projects get the
  table on p.1299 (1080p23.976, 1080p24 and 1080p25 to each other and to 1080i59.94;
  1080i50 to PAL 25i, 1080i59.94 to NTSC 30i at their SD rasters); other projects
  their own format alone.
* Each format has a play rate (the sequence's frames play at it) and a tape rate,
  progressive or with fields. Each tape field shows the frame playing at its
  middle (`model::outputSourceFrame`): 23.976 frames onto 59.94 fields give
  Standard 2:3:2:3 (AA BBB CC DDD); 24 (PAL) gives "one extra pulldown field
  occurring every 12th and 24th frame" (p.934); 24 (NTSC) adds fields and drops
  a frame now and then; 29.97 and 25 frame for frame. Frames split over two
  fields are woven (`media::weaveFields`; NTSC SD tapes lower field first).
* The Video Pulldown Cadence menu, "Standard 2:3:2:3" or "Advanced 2:3:3:2"
  (p.937), for 2:3 pulldown of 23.976 frames; "Type A or B in the Pullin column"
  (p.938): a sequence whose Pullin is B starts the cycle on a B frame.
* The sound plays at the play rate (`model::outputSpeed`: "0.1% slowdown",
  "4.1% speedup" ..., p.935), resampled onto the tape's 48 kHz.
* "If you select one of the three NTSC output formats, you need to indicate the
  timecode format for output: drop-frame or non-drop-frame" (p.935): the RS422
  Output menu (`dcRs422`), "By default ... the timecode format of the sequence"
  (p.935). The tape's IN at Sequence Time keeps the sequence's start timecode's
  label at the tape's rate (`model::sameTimecodeFrames`: 01:00:00:00 at 24 fps is
  01:00:00:00 at 29.97).
* 24 (NTSC)'s Dest. TC Rate menu, 29.97 or 30.00 fps (p.936), and the Duration
  display (`dcDuration`, `MainWindow::updateDigitalCutDuration`): "If you select
  30.00 fps, the sequence duration in the Digital Cut tool matches the sequence
  duration in the Timeline" (p.936).

## Not yet

The LTC Output menu and LTC at the destination rate (p.935-936), the SD
downconvert resize options (p.1299), the Output Mode menu following the play
rate (p.932), a real-time digital cut through video output hardware, Deck
Settings, the Capture tool's deck controller.

## Tests

`tst_digital_cut outputFormats`: a 1080p/24 project's sequence as 1080i59.94/NTSC
with 2:3 pulldown (woven fields, 01:00:00:00 at 29.97) and as 1080p25/PAL; the
menus each format shows; 24 (NTSC)'s duration at 30.00 fps.
`tst_sequence_format outputFormatsAndPulldown`: the formats by project, the
cadences, Pullin B, the tape frames and the sound's speed.

`tst_digital_cut`: No Deck; a preview leaving the tape blank; an insert edit at
Sequence Time with black at the tail; an insert at a Mark IN with a frame offset,
video only; a sound-only crash record with Ignore Time; an assemble edit of the
marked frames blanking the tape after its OUT; Local mode.
