# Multiple Mixes: the Multi-Mix Tool (Phase 5.7b3)

Timeline > Mixdown > Audio > Multiple Mixes (Editing Guide p.732-734). It
mixes a sequence's audio tracks down to several mixes at once, one master
clip per mix. Code: `media::MultiMix` and `media::MixOutput` in
`src/media/mixdown.*`; `MultiMixDialog` in `src/ui/multi_mix_dialog.*`;
`MainWindow::multipleMixes`. Each mix is an Audio Mixdown
(docs/spec/audio-mixdown.md, `media::mixdownClip`).

## The setup (`media::MultiMix`)

* **Outputs.** Each output has a name, a format (Mono, Stereo, Dual Mono,
  Surround 5.1 or Surround 7.1), Master Fader on or off, and its inputs, the
  sequence's audio tracks mixed into it.
* **Limits.** "You can create up to 24 output channels for the mix" (p.732).
  * "The Dual Mono option counts as two tracks against the limit of 24
    output mixes. Only odd-numbered tracks can be set to Dual Mono" (p.734).
  * An output's track number counts the outputs before it, with Dual Mono
    as two (`multiMixTrackNumber`).
  * **Ours:** every other format counts as one track, a stereo or surround
    mix on one multichannel track (docs/spec/multichannel.md).
* **Mix is dimmed** while the setup has no outputs, has an output without
  inputs, has more than 24 tracks, or has Dual Mono on an even track
  (`multiMixProblem`). The reason shows under the matrix (ours).
* **The options.** Use Marks, Create New Sequence (with the new sequence's
  name), Exclude Video and Data Tracks.
* **A new setup** is one stereo output of every track (ours).
* **JSON.** `MultiMix::toJson`/`fromJson`, kept in the setting.

## The dialog (`MultiMixDialog`, p.733-734)

* **At the top:** Select Setting (Untitled, then the saved settings), Bin,
  Drive, Use Marks, Create New Sequence with its name field, Exclude Video
  and Data Tracks. Processing Options offers Application; Distributed
  Processing is dimmed (ours: there is no render farm).
* **The matrix:** a row for each audio track of the sequence, a column for
  each output.
  * Above each column is the Master Fader button: "Click the button above
    the Output channel to apply the Master Fader effects and gain value to
    the output mix" (p.733).
  * The column's number button opens the output's menu: Edit Mix, Set to
    Mono / Stereo / Dual Mono / Surround 5.1 / Surround 7.1, Add Output,
    Remove Output, Clear Mix. Set to Dual Mono is dimmed on an even track.
    "Right click the output channel and select Edit Mix" (p.734) opens the
    same menu.
  * The + button after the last column adds an output (p.733).
* **Inputs.** "Map the inputs for each output channel by clicking to enable
  the input buttons for each channel. Holding Shift + hovering over the
  inputs toggles them on and off. Alt + clicking on a column, toggles all
  inputs in that column" (p.733).
  * **Ours:** Alt-click turns every input of the column on, or all off when
    all are already on.
* **Edit Mix** (p.734): the output's name, mix mode and Master Fader. The
  inputs stay as they are.
* **Save Setting As** names the setup as a setting and chooses it in Select
  Setting. "The saved setting will appear in the Select Setting field, as
  well as an Audio Multi-Mix setting in the Project tab of the Settings
  dialog box" (p.734).
  * Save Setting saves under the chosen name. While the setting is Untitled,
    it asks for a name (ours).
* **Mix and Close.** Mix closes the dialog and mixes. Close cancels.

## Mixing (`MainWindow::multipleMixes`)

* The sequence in the Record monitor is mixed. Without one, a message says
  to load one.
* **Range.** With Use Marks, "Media Composer uses current IN and OUT points
  in the selected sequence to determine starting and ending frames for the
  mixdown" (p.734). Without it, or without marks, the whole sequence.
* AudioSuite effects on the inputs are rendered first, as for Audio
  Mixdown (p.800).
* **Clips.** "New master clips will be created in the selected bin for each
  mixdown specified in the setting" (p.734).
  * Each clip is named after its output.
  * **Ours:** an output without a name gives "<sequence> Mix <track>".
  * The media goes to the chosen drive.
* **Create New Sequence.** "These clips will also be built into a new
  sequence with a track for each mix. The video and data tracks are also
  copied into the new sequence unchanged, unless you selected Exclude Video
  and Data Tracks" (p.734).
  * Each mix sits at its range's place.
  * Dual Mono gives two mono tracks, one panned left and one panned right
    (p.734).
  * **Ours:** the sequence is named "<sequence>.MultiMix" unless a name is
    typed.

## Settings

* Project settings, kind "Audio Multi-Mix" (`settings.json`; "Audio Multiple
  Mix Settings", p.1148).
* "Opening a saved Audio Multi-Mix setting from the Project tab of the
  Settings dialog box allows you to make changes to the setting. You cannot
  perform a mix when selecting the setting from the Project tab" (p.734).
  The Settings window opens the dialog with Mix dimmed
  (`multipleMixes(false)`).
* The setting active in the Project settings is chosen first when the
  dialog opens.

## Later

* "Multi-Mix Tool options are also available when adding guide tracks to a
  ProTools Session export" (p.732): with a Pro Tools export (not planned
  yet).

## Tests

* `tst_multi_mix`: the rules, the JSON, the matrix and output menu, Edit
  Mix, settings.
* `tst_managed_media` `multipleMixes`: mixing in the application, with the
  clips, the new sequence, Dual Mono's two tracks and the marks.
