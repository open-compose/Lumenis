# The calibration tone (Phase 5.7c1)

The Audio Tool's PH (Peak Hold) menu items Play Calibration Tone and Create
Tone Media, and the meters that show what plays. Pages are the Editing
Guide's.

Code:
* `src/media/tone.*`: `media::Tone`, `renderTone`, `toneClipName`.
* `AudioPiece::tone` in `playback.h` and `AudioPlayer::takePeaks`.
* `CreateToneMediaDialog` and the PH menu in `audio_tools.*`.
* `MainWindow::playCalibrationTone`, `createToneMedia` and `updateMeters`.

## The tone (`media::Tone`)

* **Level and frequency.** "The default tone playback is –20 dB (digital
  scale) ... with a 1000-Hz signal" (p.920).
  * Set Calibration Tone changes both: the level from −90 to 0 dB, the
    frequency from 1 to 20000 Hz.
  * The level is the peak of each sample: −20 dB is a peak of 0.1 of full
    scale (ours; the meters are peak meters, p.157).
* **Noise and sweep.** For tone media, "A value of 0 generates random noise.
  A value of –777 generates a tone sweep" (p.159).
  * **Ours:** the noise is white and never louder than the level.
  * **Ours:** the sweep rises logarithmically from 20 Hz to 20 kHz over ten
    seconds, then starts again.
* The same sample number always gives the same value (ours), so the tone
  cuts anywhere and renders in pieces.

## Play Calibration Tone (p.920-921, p.925)

* "Click the PH (Peak Hold) menu, and select Play Calibration Tone." The tone
  plays on the output's left and right channels at its level.
* Playback in a monitor stops it.
* **Reset Peak** "resets the current maximum peak measurements and stops the
  playback of the internal calibration tone" (p.157).
* **Ours:** without an audio output, a Console line says so.

## The meters (p.157, p.920)

* "To check the adjusted tone level in the meters: Switch the In/Out toggle
  buttons to O for Output" (p.920).
* While sound plays (the tone, a monitor, a scrub), the meters show each
  output channel's peak about 20 times a second (`AudioPlayer::takePeaks`).
  When it stops they fall back to the bottom.
* A channel switched to I (Input) shows nothing: there is no capture
  (ours).
* The output has two channels unless a surround monitor mix is chosen
  (docs/spec/surround.md), so the tone shows on meters 1 and 2.

## Create Tone Media (p.158-159)

* "Open a bin", then PH > Create Tone Media. Without a bin that can be
  changed, a message says to open one (ours).
* **The dialog:**
  * Tone Level and Frequency, starting at the calibration tone. "You can
    also use the default output tone of –20 dB (digital scale) with a
    1000-Hz signal."
  * Number of Tracks: "up to 8 tracks".
  * Target Bin and Target Drive: "select a target bin for the tone master
    clip and a target drive for the tone media file".
  * **Ours:**
    * Length, 60 seconds by default (the guide gives none).
    * OK is dimmed for a frequency other than 1-20000 Hz, 0 or −777.
    * The bin is the active one at first, and the drive is the Media
      Creation setting's audio drive.
* **OK.** "Media Composer creates the media file and a master clip appears in
  the target bin."
  * Each track is a mono 48 kHz managed media file
    (`media::renderedAudioClip`), at the project's edit rate.
* **The name.** "The default name reflects the options you selected. You
  can rename the clip by typing a new name." **Ours:**
  * "Tone 1000 Hz −20 dB", "Noise −20 dB" or "Sweep −20 dB";
  * with "N Tracks" after it for more than one track.

## Tests

* `tst_tone`: the tone's level and frequency, noise and sweep, rendering in
  pieces, the names, and a tone piece through the player with its peaks.
* `tst_audio_media` `calibrationTone`:
  * the menu items;
  * playing, the meters and Reset Peak;
  * Create Tone Media's dialog, the clip and its media.
