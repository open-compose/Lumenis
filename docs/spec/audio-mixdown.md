# Audio Mixdown (Phase 5.1)

Timeline > Mixdown > Audio > To Sequence mixes the selected audio tracks
of the Record monitor's sequence into new sound, a master clip of its own,
and edits it into the sequence (Editing Guide p.731-732, "Mixing Down
Audio Tracks"). It is also ROADMAP Phase 5 gate 1's subject: the mixdown
must null against a reference render to -120 dBFS.

## What is mixed (`media::MixdownRenderer`, `src/media/mixdown.*`)

* The audio tracks selected in the Track Selector panel ("Click the Track
  buttons ... to select the audio tracks you want to mix down", p.731).
* Between the record marks; without marks, the whole sequence ("If you do
  not mark the section of audio you want to mix down, the system mixes
  down all of the selected audio tracks").
* The mix is the one playback hears (`media::audioPieces`, `AudioMix`): each
  segment's clip gain and pan (p.702); a centred pan follows its track's
  side, odd left and even right, unless the Audio setting's Default Pan for
  mono tracks is Centered (p.1147). The Master fader's gain is applied when
  Include Master Fader is on (p.732). Mute and solo are not: the tracks
  mixed are the ones selected.
* Formats (p.732): **Mono**, every track summed at its clip gain without its
  pan (ours: the guide does not say how a mono mix treats pan), and **Dual
  Mono**, "a stereo mix written to two mono output tracks, one for the left
  channel, panned left, and one for the right channel, panned right"
  (p.734). Stereo, 5.1 and 7.1 are dimmed: Cutthulhu's tracks hold one clip
  channel each, so a track of several channels waits for multichannel
  tracks.
* 48 kHz, 24-bit (or 16-bit, the Audio Project setting) PCM, one managed
  MXF OP-Atom file a track (`media::renderedAudioClip`), on the drive chosen
  (first the Media Creation setting's Mixdown & Transcode drive, p.1207).

## In the application (`MainWindow::audioMixdown`, `AudioMixdownDialog`)

The Audio Mixdown dialog box (p.732; its layout is ours):
* Source Tracks and Range: the tracks and the start and end timecodes.
* Mono, Dual Mono (Stereo, 5.1, 7.1 dimmed) and the target track: "A mono
  mixdown goes to the next available mono track ... If there are no
  appropriate tracks in the Timeline, the mixdown operation creates them."
  The menu lists the audio tracks with nothing in the range, not among the
  source tracks (`model::emptyAudioTracks`), first; for Dual Mono, pairs of
  them one after the other (ours); then New Track(s).
* Include Master Fader; a bin (the open bins that can be changed) and a
  drive; Save Premix Sequence, a duplicate of the sequence in the bin before
  the mix.
* OK: the clip ("<sequence> Mixdown", ours) goes into the bin and is
  overwritten into the target tracks at the range's start, Dual Mono's
  panned left and right, as one undo step (Undo leaves the clip and its
  media in the bin, as undo covers only the sequence).

## Checks

* `tst_mixdown` (gate 1, the null test): two sine-wave clips, with gains,
  pans, filler and a centred pan, mixed down Mono and Dual Mono between
  marks with the Master fader; the MXF media read back is compared sample by
  sample with the mix worked out in double precision from the sines. The
  residual is about -137 dBFS (24-bit rounding); the limit is -120 dBFS. A
  0.1 dB error in one gain fails it at about -45 dBFS.
* `tst_managed_media` (`audioMixdownIntoTheSequence`): the menu command
  through the dialog's hook: the premix copy and the clip in the bin, the
  new track A3, the level of the media, and Undo.
* Gate 1's reference renders from real Media Composer wait for the owner's
  seat; the test's reference stands in.

## Later

* Stereo, 5.1 and 7.1 mixdowns, with multichannel tracks.
* Mixdown > Audio > Multiple Mixes (the Multi-Mix tool, p.732-734).
* Track-level faders, automation and effects in the mix, as they come
  (Phase 5).
