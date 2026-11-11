# Audio marks and split edits (9.6b)

Editing Guide p.411, "Marking Audio Clips": "You can mark audio and video separately for an edit by using the Audio
Mark buttons. This feature is useful for creating an overlap edit for an audio clip." The Audio Mark IN and Audio
Mark OUT buttons are on the Command Palette's Edit tab (p.100); "The Audio Marks appear in the Timeline and in the
position bar beneath the monitors". "To remove audio IN and OUT points: Shift+click the Clear IN Mark, Clear OUT Mark,
or Clear Both Marks button." The overlap edit section (p.661) points back to them.

## What we do

* **Marks.** `model::LoadedClip::audioIn`/`audioOut` beside the IN and OUT, kept with each loaded clip like them;
  `MonitorState::markAudioIn`/`markAudioOut`/`clearAudioIn`/`clearAudioOut`/`goToAudioIn`/`goToAudioOut`. A new audio
  IN after the audio OUT clears the OUT and the other way round, as the marks do.
* **Commands.** `edit.audio_mark_in`, `edit.audio_mark_out` (Edit tab), `move.go_to_audio_mark_in`,
  `move.go_to_audio_mark_out` (Move tab, only in its picture on p.96), carried out by `Composer::perform` on the active
  monitor. Shift with Clear IN Mark, Clear OUT Mark or Clear Both Marks clears the audio marks instead.
* **Drawing.** Green brackets in the position bar (the colour in the picture on p.411) and in the Timeline's ruler.
* **Split edits (ours: the guide says only that the marks are "for an edit").** When the Source or Record monitor
  has an audio mark, `Timeline::edit` makes two edits in one undo step: the video tracks by the marks, the audio
  tracks by the marks with each audio mark in place of its kind's (`model::withAudioMarks`), each half with only its
  kind's source tracks (`model::patchOfKind`). A Splice-in whose halves differ in length is refused, because each half
  pushes its tracks on by its own length and the tracks would go out of sync; Overwrite makes any split. Sync Point
  editing and Fit to Fill ignore the audio marks. An edit uses up the Record monitor's audio marks with its marks.
* **Go to Previous/Next Trim** (only pictured, p.96): ours, the same as Select Previous/Next Transition (A and S,
  p.651).

Tests: scenario `51-audio-marks.json`; `tst_command_wiring` (the commands reach something).
