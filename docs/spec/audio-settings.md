# The audio settings' options (Phase 5.7c3)

The options of the Audio, Audio Project and Import settings that waited for
audio. Pages are the Editing Guide's (chapter 33). Options and their defaults
are in `src/ui/resources/settings.json`; an option still marked `later` is
shown dimmed.

## Audio settings (User, p.1147-1148)

* **Play Buffer Size in Samples** and **Tool Buffer Size in Samples.** These
  set the output device's buffer (`AudioPlayer::PlayOptions::bufferFrames`).
  * The play buffer is used "during playback and digital cut".
  * The tool buffer is used for "audio loop play and audio tools play": Audio
    Loop Play and the calibration tone (docs/spec/tone.md).
  * "Avid strongly recommends that you use the default 'recommended sample'
    setting, which is determined by the host audio device" (p.689). **Ours:**
    the default is 0, shown as "Recommended": the device chooses.
  * "To return this option to its default value, click the rs (recommended
    sample) button": the rs button after each box (the settings schema's
    `resetButton`, with `specialText` for 0).
  * **Ours:** no warning dialog when the value changes (p.689 mentions one).
* **Pitch Correction during Shuttle.** "When you shuttle at speeds above sound
  speed, a pitch shift is applied to restore original pitch."
  * Without it, 2 and 3 times speed average every n-th sample and the pitch
    rises, as on tape (2.3b).
  * With it (`PlayOptions::pitchCorrect`), the sound is overlap-added from
    Hann-windowed grains of 1024 samples, one every 512 samples of output,
    each read 2 or 3 times further along. It keeps its pitch and level,
    forward and backward. **Ours:** the method; the guide gives none.
* **Display Channel Order** orders the channels of 5.1 and 7.1 tracks "in the
  Audio Mixer tool and in the waveform display in the Timeline": the
  Timeline's waveforms follow it (U1a, docs/spec/waveforms.md). The mixer
  shows no per-channel labels yet.

## Audio Project settings (Project, p.1148-1149)

* **Sample Rate** (44.1, 48 or 96 kHz; 48 by default): "Defines the audio
  sample rate for the entire system for playing and recording"
  (`MainWindow::projectSampleRate`).
  * New audio media is made at it (`MediaCreation::sampleRate`): Import,
    Audio Mixdown and Multiple Mixes, AudioSuite renders, Audio Punch-In and
    tone media.
  * Media Cutthulhu renders (`renderedAudioClip`) is rendered at 48 kHz and
    resampled.
  * 48 kHz is managed MXF; other rates are WAV beside it (ours: FFmpeg's
    OP-Atom muxer takes only 48 kHz; docs/spec/clip-audio.md).
  * Playback itself runs at 48 kHz and resamples what it reads.
  * Consolidate copies media at its own rate.
* **Convert Sample Rates When Playing:** "Never: Media Composer does not
  perform sample rate conversion (segments play as silence). Always: Media
  Composer attempts to perform a sample rate conversion."
  * Never makes files at another rate than the project's play as silence
    (`AudioMix::setRequiredRate`). The calibration tone always plays.
  * **Ours:** Always by default, so nothing goes silent unasked (the guide
    gives no default).
* **Show Mismatched Sample Rates as Different Color.** The waveforms of clips
  at the project's rate are drawn black, those of other rates white (p.680;
  docs/spec/waveforms.md). (5.7c3 first coloured the whole segment; p.680
  shows the plot, so U1a changed it.)
* **Remove Extra Filler After Punchin** stays dimmed. The extra filler comes
  from Extended Audio Punch-In past the end of the sequence, which is not
  done yet (docs/spec/punch-in.md).

## Import settings: Audio tab (p.1198-1199)

`MainWindow::importFiles` sets these for each file it imports.

* **Sample rate convert source sample rate to project sample rate** (on by
  default). On, the media is made at the project's rate. Off, "Media
  Composer imports audio media at the source sample rate. If Media Composer
  does not support the source sample rate, it autoconverts". **Ours:** 32,
  44.1, 48, 88.2 and 96 kHz are kept (`media::isImportRate`).
* **Do not convert sources with pullup or pulldown rates** (on by default,
  with the option above). Sound at 1001/1000 or 1000/1001 of the project's
  rate (`media::isPullRate`: 48048 and 47952, 44144 and 44056) is written
  unconverted at the project's rate: "The length and pitch of the imported
  files are changed by plus or minus .1%" (`MediaCreation::relabelPullRates`).
  The clip keeps its length in frames, so the sound ends 0.1% early or is
  cut 0.1% short.
* **Convert source bit depth to project bit depth** (off by default). Off,
  16- and 24-bit sources keep their depth; others take the project's.
* **Apply attenuation/gain effect on import**, its gain and CD only came in
  9.2d with importing from CDs (docs/spec/audio-cd.md).
* **Automatically center pan monophonic clips.** One-track imported clips get
  a centred pan of their own (`model::centerClipPan`, as Clip > Audio >
  Center Pan does).
* **Autodetect Broadcast Wave Monophonic Groups.** "Media Composer imports
  multichannel, monophonic BWF files as a single master clip ...
  (filename_1.wav is associated with track A1, filename_2.wav is associated
  with track A2)" (`media::groupMonoWavs`).
  * Files named `<name>_<n>.wav` (or `.bwf`), one channel each, in one folder
    make a clip named `<name>` with a track per file in number order.
  * A name with one file is left alone.
  * The joined clip links each track to its file (`Link::audioPaths`).
* **Subframe Alignment to Broadcast WAV Start Time.** "Imported broadcast WAV
  files have either silence added to the beginning, or up to half a frame of
  audio deleted from the beginning, in order to be in alignment with the
  audio start time specified in the original Broadcast Wave media file."
  * Linking a Broadcast WAV takes its Start from its time reference (the
    "bext" chunk; `media::bwfTimeReference`). The time is frame accurate:
    the frame its first sample falls in.
  * With the option, `media::alignToFrame` picks the nearest frame. A sound
    starting in the first half of a frame gets that much silence first; one
    in the second half starts on the next frame with the samples before it
    left out (`MediaCreation::audioShift`).

## Tests

* `tst_audio_project` (media):
  * pitch correction at 2 and 3 times speed and backward;
  * Never's silence;
  * rendered media at 44.1, 96 and 48 kHz;
  * pull rates, alignment, BWF time stamps, silence and trimming, groups.
* `tst_settings_window`: the buffer boxes, Recommended and rs.
* `tst_audio_media`:
  * `projectSampleRate`: tone media at 44.1 kHz, the setting passed to
    the Timeline (the colours are in `tst_waveform_ui`);
  * `importAudioSettings`: rates and bit depths kept or converted,
    centring, a BWF group aligned to its time stamp.
