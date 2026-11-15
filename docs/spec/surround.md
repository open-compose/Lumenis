# Surround mixing (Phase 5.6b)

A sequence is mixed in a format of its own, Stereo, 5.1 or 7.1. The
speakers that listen can be another format: Media Composer folds the mix
into them (Editing Guide p.685-692). 5.6b1 is the mix: the formats, the
folds, every output order of the Mix Mode Selection button, and Stereo,
5.1 and 7.1 Audio Mixdowns. 5.6b2 adds the Pan grid and the Advanced
Panner (p.704-710), which pan mono and stereo tracks to every speaker.

## The bus (`src/media/surround.*`, `media::AudioMix`)

* Playback mixes every sequence onto eight speakers, the 7.1 set: L, R,
  C, Lfe, Lss, Rss, Lsr, Rsr (`media::Speaker`). A 5.1 mix's surrounds,
  Ls and Rs, are Lsr and Rsr. The guide says this when a 7.1 mix plays on
  5.1 speakers: "C, L, R, Lsr, Rsr, and LFE channels panned to appropriate
  speakers" (p.688).
* Clips, and mono and stereo tracks in a stereo sequence, are panned
  between L and R, as before. In a 5.1 or 7.1 sequence mono and stereo
  tracks follow their surround pans (below), and each channel of a
  multichannel track plays on its own speaker (Pro Tools order, docs/spec/multichannel.md).
  It plays at the segment's gain, without a pan, and its volume
  automation applies. In a stereo sequence a multichannel track keeps its
  stereo placement (docs/spec/multichannel.md).
* `AudioMix::setOutput` takes a row of gains from the bus for each output
  channel. `media::outputMatrix(sequence format, monitor)` makes them from
  two folds.

## The Sequence Mix Format (`model::Sequence::mixFormat`)

* The Audio Mixer's Sequence Mix Format button, at the top of the Master
  strip (p.693), offers Stereo Sequence, 5.1 Sequence and 7.1 Sequence
  (p.686).
* The format is kept in the sequence (composition key `mix`; absent for
  Stereo). Changing it is an undo step, "Sequence Mix Format".
* The sequence's fold (ours; the guide's table on p.687-688 read as a
  fold of the mix):
  * A stereo sequence folds C and Lfe into the middle, 3 dB down on each
    side, and every surround into its own side, 3 dB down.
  * A 5.1 sequence folds Lss and Rss into the middle of the L/Lsr and
    R/Rsr speaker pairs.
  * A 7.1 sequence keeps all eight speakers.
* "3 dB down" is one side of an equal-power pan to the middle (0.7071,
  −3.01 dB), here and in the stereo placement of multichannel tracks.

## The Monitor Mix Format (the Mix Mode Selection button)

The button at the left of the Audio Mixer's top bar (p.691-692). The
guide also calls it the Monitor Mix Format button (p.689). Its choice is
the Audio Project setting's Mix Mode Selection (p.1150). The menu:

| Mix | Output channels | Fold (p.687-692) |
| --- | --- | --- |
| Stereo | L R | C and Lfe in the middle; the surrounds to their sides 3 dB down |
| Mono | one | every channel in the middle (3 dB down, ours); pans are ignored, so panned tracks play at their own gain |
| 4-channel LRCLfe | L R C Lfe | the rear and side surrounds into L and R 3 dB down |
| 4-channel LRCS | L R C S | Lfe into L and R 3 dB down; the rear surrounds into S (3 dB down, ours); the side surrounds between S and their front speaker |
| 4-channel Quadraphonic | L R Ls Rs | C in the middle of L/R; Lss and Rss in the middle of their side's pair; Lfe into L and R 3 dB down |
| 5.1 Pro Tools | L C R Ls Rs Lfe | Lss and Rss in the middle of L/Ls and R/Rs |
| 5.1 SMPTE | L R C Lfe Ls Rs | as 5.1 Pro Tools |
| 7.1 Pro Tools | L C R Lss Rss Lsr Rsr Lfe | none; Lss and Rss are silent for a 5.1 mix |
| 7.1 EXT | L R C Lfe Lsr Rsr Lss Rss | as 7.1 Pro Tools |
| 7.1 SMPTE DS | L R C Lfe Lss Rss Lsr Rsr | as 7.1 Pro Tools |

* "Stereo sequences are output only to L and R" (p.691).
* The device opens with the mix's channels in the mix's order.
  miniaudio is told each channel's position, so a system with fewer
  speakers mixes the extra ones down itself.
* Audio Punch-In's monitored input goes to the channels that L and R reach.
* Clips in the Source monitor play as a stereo sequence does, in their
  captured channel order (p.689).
* The Master fader applies to automated pieces too (the `scale`), as it
  already did in mixdowns.

## Audio Mixdown (p.732)

* Stereo, 5.1 and 7.1 can now be chosen. Each makes one clip whose
  channels are one track of that format (its Track Formats).
* The channels are the sequence's mix folded into that format
  (`media::trackMatrix`): Stereo as the Stereo mix, 5.1 and 7.1 in Pro
  Tools order.
* "A stereo or surround sound mixdown goes to the next available stereo or
  surround sound track": the target menu lists the empty tracks of the
  format, then New Track, which adds a track of the format. Mono and Dual
  Mono now list only mono tracks.
* A multichannel track's inserts are mixed in for every channel. Before,
  only the first channel's were.

## Surround panning (5.6b2; `model::SurroundPan`, `src/model/surround_pan.*`, `src/ui/surround_panner.*`)

* **The pan.** Each channel of a mono or stereo segment has the Advanced
  Panner's values (p.705-706): Front Position and Rear Position
  (left/right, L100 to R100), F/R Position (F100 front to R100 rear),
  Center percentage, Side percentage (7.1) and LFE (0-100). The Pan
  Location cursor sits on the trajectory from the front position to the
  rear one. Dragging the cursor in a grid sets both positions to its
  left/right place (an X/Y pan). Segments keep their pans (`surround` in
  the composition). Pans are used only in 5.1 and 7.1 sequences.
* **Defaults (ours).** With no pan of its own, a channel sits at the front:
  * a mono segment where its stereo pan puts it, including the track's
    side (odd tracks left, even right, p.1147), so a centred mono track
    plays from C;
  * a stereo segment's channels at the front left and right.
  The guide's picture shows L50, L50, F100, 100 and 100 for one channel;
  the guide does not give the defaults.
* **The pan law (ours, `media::surroundGains`).** The guide gives none.
  * The F/R position crossfades, with equal power, between the front row
    and the rear surrounds; in 7.1 between the front, the side and the
    rear rows.
  * In the front row the front position pans across L, C and R; Center
    percentage blends that with a phantom centre between L and R.
  * The rear row pans the rear position between the rear surrounds; 7.1's
    side row pans the cursor between the side surrounds; Side percentage
    moves the side row's share to the front and rear rows.
  * So the Position controls are heard "from just those two speakers"
    (p.707), and grid pans in the middle reach more.
  * The gains have unit power; LFE adds its percentage to the LFE channel.
  * Surround pan automation (11.4d2, docs/spec/automation.md "Surround
    pans") moves the pans over time; Live mode does not apply to them yet.
    Volume automation does.
* **The Pan grid (p.705-706).** In a 5.1 or 7.1 sequence each mono or
  stereo track's mixer strip shows a small grid for each channel, with the
  speakers around it, instead of the pan knob. 5.1 and 7.1 tracks show no
  pan, because their channels have their own speakers. In Clip mode,
  dragging the cursor sets the pan of the segment under the position when
  the drag ends ("Surround Pan", one undo step). In Auto and Live mode the
  grids are dimmed in Live mode (ours); in Auto mode they set and record
  surround pan automation (11.4d2b, docs/spec/automation.md "Surround pans").
* **The Advanced Panner (p.705-710).** The strip's Adv button opens it for
  that track:
  * a large grid for each channel, with the trajectory;
  * double-clicking a speaker snaps the cursor to it (p.707); snapping to
    C or a side speaker also sets its percentage to 100 (ours);
  * the front, rear, f/r, center and (7.1) side knobs with their values
    ("L50", "F100", "100");
  * the LFE slider (0-100, as the table says; the picture shows a
    fader in dB);
  * the Stereo Link and Stereo Mirror buttons for stereo tracks;
  * Close.
  It follows the segment under the position.
* **Stereo Link and Mirror (p.693, p.705).** The mixer's Unlinked / Stereo
  Linked / Stereo Mirror button and the panner's buttons share one
  setting. Linked moves the other channel the same way. Mirrored moves it
  the other way left to right and the same way front to back
  (`model::linkedPan`).
* **LFE only (p.710).** Right-click a strip > "This track is LFE only", for
  mono tracks. The strip shows LFE and no panner, and the track plays only
  on the LFE channel of a surround mix (folded as LFE is in a stereo one).
  Split Track to Mono marks the LFE channel's new track LFE only. It is an
  undo step.

## Checks

* `tst_surround` (media):
  * the sequence format's round trip;
  * the menu's mixes and channel orders;
  * the p.687-688 table speaker by speaker, and the four-channel mixes;
  * 5.1 and 7.1 tracks in 5.1 and 7.1 sequences, on every order of
    speakers;
  * panned mono and stereo tracks;
  * Stereo, 5.1, 7.1 and Dual Mono mixdowns read back.
* `tst_multichannel_ui`:
  * the Sequence Mix Format button and its undo;
  * the Mix Mode Selection menu, its setting, and the mix playback uses;
  * the mixdown dialog's targets for each format.
* Scenario `13-surround.json`: the format change as an undo step, and a
  5.1 edit in a 5.1 sequence.
* `tst_surround` also checks the pan law (unit power, the speakers, the
  percentages, the Position controls' two speakers), panned and LFE-only
  playback, and the pan model (defaults, JSON, links, Split Track to Mono).
* `tst_surround_ui`:
  * the grids in place of the knob;
  * storing pans and undo, Link and Mirror;
  * the Advanced Panner's knobs, LFE and speaker snapping;
  * LFE only.
* Scenario `15-surround-pan.json`.
* The mixdown null test (`tst_mixdown`) is unchanged: a stereo sequence's
  mix is what it was.

## Later

* Live mode panning of surround pans; 3-Knob mode and typed
  Position values (p.706).
* Direct Out and the Audio Project setting's output assignments
  (p.692, p.1150).
* Starting a 5.1 or 7.1 mix at another output position (p.692).
* The Audio Mixer's and Audio Tool's channel meters in the monitor format
  (p.687-688).
* A clip's own multichannel formats in the Source monitor.
