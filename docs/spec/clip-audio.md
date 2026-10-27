# The Clip menu's audio commands (Phase 5.7b)

Clip > Audio > Apply Gain, Center Pan and Change Sample Rate, Clip > Auto
VO, Clip > Read Audio Timecode, and Timeline > Mixdown > Audio > Multiple
Mixes. 5.7b1 is Apply Gain, Center Pan and Auto VO; 5.7b2 Read Audio
Timecode and Change Sample Rate; 5.7b3, Multiple Mixes, is in
docs/spec/multi-mix.md. Pages are the Editing Guide's.

## A clip's own gain and pan (`BinItem::audioGain`, `audioPan`; `src/model/clip_audio.*`)

* **Clip > Audio > Apply Gain (p.216).** It works on the master clips (with
  or without picture) and subclips selected in the bin; sequences and other
  items are left alone.
  * The Apply Clip Gain dialog takes a level from 12 to -96 dB, typed or
    stepped with the Up and Down arrows.
  * It starts at the first selected clip's gain (ours).
  * "If there was a gain previously associated with the clip, the new gain
    value overrides it": the gain is set, not added.
* **Clip > Audio > Center Pan (p.713-714).** "A dialog box opens and asks you
  to confirm the pan". On OK the selected clips take a centred pan of their
  own.
* **Where they are heard.**
  * When the clip plays in the Source monitor, its channels play at its gain.
    Center Pan puts them all in the middle; without it, a single channel
    plays in the middle and more alternate left and right, as before.
  * "Levels set in master clips carry across to the sequence after you edit
    the clips" (p.711): an edit gives the new audio segments the clip's gain
    and, after Center Pan, its pan (`Segment::hasPan`).
  * Segments already in sequences keep theirs: "To adjust a clip's gain in a
    sequence, you must use the Audio Mix tool" (p.216).
* **Bin file.** The bin file keeps them as `audioGain` and `audioPan`.
  * AAF and `.avb` export do not write them yet (ours).
  * Import's "Apply attenuation/gain effect on import" (p.216) is 5.7c.

## Clip > Auto VO (p.753-754; `model::autoVo`)

* One sequence selected in the bin opens the Auto VO dialog:
  * the Using track menu (the sequence's audio tracks);
  * Extract segments or Keep segments.
* **Extract segments** removes the time of every material segment on that
  track from all the sequence's tracks.
* **Keep segments** removes all the other time.
* The result is a new sequence in the same bin, named with ".vo" added. The
  original "is retained and is unaffected".
* Extracting leaves cuts where the time was taken out, as Extract does.

## Clip > Read Audio Timecode (p.668-669; `src/media/ltc.*`)

* **The decoder.** LTC (SMPTE ST 12-1) is read by our own decoder; no library
  is used.
  * It finds the transitions of the biphase mark code, with hysteresis at a
    quarter of the peak, so either polarity and any level work.
  * Whole-bit and half-bit intervals at the clip's nominal rate (24, 25 or
    30) make 0s and 1s.
  * A frame counts where its sync word (bits 64-79) is found.
  * The time fields, the drop-frame flag and the 32 user bits are read.
  * `ltc::encode` writes LTC for the tests.
* **The dialog (p.668-669):**
  * User Bit Timecode;
  * the Audio Timecode Source (the audio tracks; A1 the default);
  * the Destination Track (Auxiliary TC1-5; TC1 the default);
  * Fill Undecodeable Frames (on by default).
* **What it writes.** For each selected master clip or subclip, the
  timecode of the clip's first frame goes into the chosen Auxiliary TC
  column. Other selected items get the guide's message that they will be
  ignored.
  * With Fill Undecodeable Frames, the first frame that decodes is worked
    back to the clip's start, counting on as the guide's example does.
  * Without it, the clip's first frame must decode, else the clip is
    reported and left (ours: our columns hold one start timecode, not a
    timecode a frame).
* **User bits (ours).** User Bit Timecode reads the user bits as BCD
  timecode in groups 1-8: frame units and tens, then seconds, minutes and
  hours, the order of the time fields. The guide says only that they "must
  be timecode only".
* A frame is read when the transition after its last bit comes, so the very
  last frame of a recording is not read.

## Clip > Audio > Change Sample Rate (p.731; `media::resampleClipAudio`)

* **The dialog:** Sample Rate (32, 44.1 or 48 kHz; 48 to start), Quality
  (High, Medium, Low), Delete Original Media, and Target Drive.
* **What it converts.** For the selected audio clips, and for the clips that
  the selected sequences' audio segments use, each clip's audio tracks get
  new files at the rate, as long as before. Its picture stays as it is. The
  clip is relinked to the new files and its Audio SR column updated.
* **Quality** is swresample's filter length: 64 (High), 32 (Medium, its
  default) and 8 (Low) (ours).
* **File format (ours).** FFmpeg's MXF OP-Atom muxer takes only 48 kHz:
  * 48 kHz media is managed MXF, as Consolidate/Transcode makes;
  * 32 and 44.1 kHz media is WAV in the same `Avid MediaFiles/MXF/<n>`
    folder.
  * The media database and the Media Tool list MXF files only, so those WAV
    files are not in them.
* **Delete Original Media** deletes the managed files the old audio used.
  A linked clip's own files are left alone (ours).
* A clip with picture that was relinked to media starting earlier
  (`Link::first`) is refused: picture and sound share that offset.

## Checks

* `tst_clip_audio`:
  * the gain overrides and is clamped; Center Pan; sequences refused; the bin
    file;
  * Auto VO both ways;
  * the dialogs;
  * the three commands in the application.
* `tst_ltc` (media): encode and decode at 24, 25 and 29.97 drop frame, in
  uneven pieces; inverted, quiet, noisy and offset; user bits; a linked
  clip's LTC with and without Fill; Change Sample Rate to 44.1 kHz (WAV,
  right length and level) and back to 48 kHz (MXF, the 44.1 kHz file as the
  original to delete).
* `tst_clip_audio` also checks the two dialogs, and both commands in the
  application: LTC on a linked WAV into Auxiliary TC2, then Change Sample
  Rate to 32 kHz keeping the timecode.
* Scenario `17-clip-gain-pan.json`: an edit carries the clip's gain and pan
  into its audio segments.
