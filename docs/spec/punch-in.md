# Audio Punch-In (Phase 5.4)

Audio Punch-In records voice-over from an audio input straight into the
sequence while it plays (Editing Guide p.746-753).

## The input (`src/media/audio_input.*`)

* `media::AudioInput` records 48 kHz float samples from a capture device
  through miniaudio. Each channel is kept in memory until the take ends.
  `audioInputDevices()` lists the inputs for the Input Source menu (p.749).
* `startSimulated` makes known samples at the real rate on a thread of its
  own. The tests use it, and so can machines with no microphone.
* **Monitoring:** a mono mix of the input goes through a small lock-free
  ring. `AudioPlayer::setMonitor` adds it to the output (p.748-749, p.753).
  The ring keeps at most 50 ms of delay; anything older is dropped.

## The edit (`Timeline::punchIn`)

* One take, from the first play to Stop, is **one master clip**: "Media
  Composer creates one master clip, regardless of how many punch-ins you
  perform" (p.751). It has an audio track per input channel selected.
* Each punch-in is a record frame, a length and the clip frame recorded
  there. Channel k goes to the k-th Timeline Track choice:
  * an existing mono audio track that is not locked, which is overwritten
    (p.749);
  * or New Track, which is added with the first punch-in.
* **Undo:** each punch-in is an undo step of its own: "The first undo
  removes the most recent punch-in, the second undo removes the
  second-to-last" (p.751).
* **Ours:** a new track is removed with the first punch-in's undo.
* The guide's third undo removes "all the punch-ins". Ours keeps one step per
  punch-in, so it takes as many undos as there were punch-ins.

## The tool (`src/ui/audio_punch_in.*`)

* **Opening:** Tools > Audio Punch-In (p.750), a panel ("AudioPunchIn").
* **Layout** (picture and table on p.748-749):
  * Play In/Out, Record, Stop and Go to Mark In; Cancel and the Audio Tool
    button;
  * the Monitor Mode Input button: Automatic, On, Off or Manual;
  * Stop at end or at mark out; Pre/Post (sec); Handles (sec); Input
    Source;
  * CH 1-4, each with its Timeline Track menu (New Track or a mono audio
    track that is not locked); Alt+click picks another input channel;
  * Target Drive and Target Bin.
  **Ours:** text glyphs on the buttons.
* **Keys:** V is Play In/Out and B is Record while the tool has the keyboard
  (p.750-751). The space bar stops play, which ends the take as Stop does.
* **A take** starts with Play In/Out or Record:
  * the input opens;
  * play starts from the range's start, backed up by the preroll ("you can
    hear the audio during preroll", p.749);
  * the input records all the while, preroll and postroll included, so the
    clip has handles around every punch-in.
* **Play In/Out** loops IN to OUT. Without an IN the position is used
  (scenario 1, p.749); without an OUT, the end. "The play loops ... but
  stops looping once recording completes" (p.748). While it plays without
  recording, the user is rehearsing (p.747).
* **Record:**
  * **Stopped:** recording starts at the IN, or at the position, once play
    reaches it ("If you set an In point and Out point, recording
    automatically starts at the In point", p.748). With no preroll it
    starts at once.
  * **During the preroll:** recording starts at once ("The Record button
    takes precedence over preroll", p.749).
  * **During play:** each press punches in, and the next punches out
    (scenarios 1 and 3, p.749-750). Ours: the punch-out is at the frame
    before the position.
* **Stop at end or at mark out**, on by default: a recording ends after the
  OUT (or the last frame). The take goes on through the postroll or the
  handles, whichever is longer (p.749), and then ends.
* **Unchecked:** recording goes on past the OUT (Extended Audio Punch-In,
  p.751-752). It cannot go past the end of the sequence yet, since play
  stops there.
* **Stop** (p.748) ends the take and keeps it, and any open punch-in ends
  at the position. **Cancel** throws the take away. Play that runs to the
  end of the sequence ends the take as Stop does.
* **Go to Mark In:** the IN, else where the take started, else the start
  (p.748).
* **Where the recording falls:** a punch-in at record frame r takes the
  frame of the recording that came in when play was at r. That is what has
  been recorded so far, less the frames played since r.
  **Ours:** no compensation for the devices' delays yet.
* **The clip:** MainWindow writes the take as managed media on the target
  drive (`media::renderedAudioClip`) and adds it to the target bin. It is
  named "<sequence> Voice Over.<n>" (picture on p.751), where n is one more
  than the bin's highest n. Each channel goes on a track of its own.
  Punch-ins past the end of the recording are cut to it.
* **While a take runs** the existing target tracks are silent ("silences
  that portion during playback", p.749). Ours: the whole track is silenced,
  not only the punched part.
* **Monitoring** (p.748-749, p.753): On plays the input through the output;
  Off does not; Automatic does while the input's own delay is under
  100 ms; Manual leaves it to the hardware. The other tracks can be
  monitored in a mono mix with the Audio Project setting's Output tab set to
  Mono (p.752), which the Audio Mixer already has.

## macOS, Windows and Linux

The microphone needs the system's permission. macOS needs the bundle's
`NSMicrophoneUsageDescription` (packaging/macos/Info.plist.in, checked by
CI's installer gate) and, for Developer ID builds, the audio-input
entitlement. BUILDING.md, "Audio Punch-In records silence", has the steps.

## Checks

* `tst_audio_punch_in`:
  * the simulated input: rate, channels, monitoring ring, and the output
    adding it;
  * `Timeline::punchIn`: new and existing tracks, undo per punch-in,
    refusals;
  * the tool's controls and menus;
  * the scenarios played with the Composer: IN to OUT, several punch-ins
    with B, preroll and Cancel, loop play with the postroll, extended
    recording, and play to the end;
  * Tools > Audio Punch-In and the "Voice Over.n" clips written as managed
    media.
* Edit scenarios: `tests/scenarios/09-audio-punch-in.json` uses the
  `punch_in` step. The harness records the undo levels between that a
  step with several undo steps makes.

## Later

* Capture tool voice-over (p.746-747) comes with tape capture.
* Recording past the end of the sequence (Extended Audio Punch-In).
* The Audio tool's meters showing the input with an orange I (p.751).
* Peak Hold while recording (p.753).
* GPI devices (p.776).
* Latency compensation. Until then, Stop waits (at most a second plus the device's latency) for
  the input to bring in the sound through the last punch-in's end before it stops the input
  (ours): input runs behind play, and a busy machine left takes a few frames short.
* Silencing only the punched part of a target track.
* The Audio Project setting's Input tab (p.749 note).
