# The Clip menu's audio commands (Phase 5.7b)

Clip > Audio > Apply Gain, Center Pan and Change Sample Rate, Clip > Auto
VO, Clip > Read Audio Timecode, and Timeline > Mixdown > Audio > Multiple
Mixes. 5.7b1 is Apply Gain, Center Pan and Auto VO. Pages are the Editing
Guide's.

## A clip's own gain and pan (`BinItem::audioGain`, `audioPan`; `src/model/clip_audio.*`)

* **Clip > Audio > Apply Gain (p.216).** It works on the master clips and
  subclips selected in the bin; sequences and other items are left alone.
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

## Checks

* `tst_clip_audio`:
  * the gain overrides and is clamped; Center Pan; sequences refused; the bin
    file;
  * Auto VO both ways;
  * the dialogs;
  * the three commands in the application.
* Scenario `17-clip-gain-pan.json`: an edit carries the clip's gain and pan
  into its audio segments.
