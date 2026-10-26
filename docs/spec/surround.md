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
* Mono tracks, stereo tracks and clips are panned between L and R, as
  before. In a 5.1 or 7.1 sequence, each channel of a multichannel track
  plays on its own speaker (Pro Tools order, docs/spec/multichannel.md).
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
* The mixdown null test (`tst_mixdown`) is unchanged: a stereo sequence's
  mix is what it was.

## Later

* 5.6b2: the Pan grid and Advanced Panner (p.704-710), the Unlinked /
  Stereo Linked / Stereo Mirror control (p.693), and LFE-only mono tracks
  (p.710).
* Direct Out and the Audio Project setting's output assignments
  (p.692, p.1150).
* Starting a 5.1 or 7.1 mix at another output position (p.692).
* The Audio Mixer's and Audio Tool's channel meters in the monitor format
  (p.687-688).
* A clip's own multichannel formats in the Source monitor.
