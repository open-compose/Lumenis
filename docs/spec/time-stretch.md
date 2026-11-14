# The time-stretch engine (ROADMAP 11.2)

AudioSuite's Pitch Shift (Editing Guide p.850-851), Time Compression
Expansion (p.856-857) and Time Shift (p.857-860) change a sound's length,
its pitch, or both. Avid's plug-ins are its own; ours run on an engine of
our own, `src/media/time_stretch.*`, by the method the owner chose:
**WSOLA**, waveform-similarity overlap-add.

## The method (`media::timeStretch`)

* The output is built from windows a crossfade apart, each two crossfades
  long, Hann-shaped so that neighbours add up to one.
* Each window is read from where the stretch puts it in the input (its
  output place over the ratio), moved within a search range to the place
  whose start sounds most like where the window before it carries on: the
  highest normalised cross-correlation over the crossfade. So the
  waveform's periods line up at every join, and the pitch stays while the
  length changes.
* The guide's fine controls (p.857) set the window and the search:

  | Control | Ours |
  |---|---|
  | Crossfade (Auto, 1-200 ms) | half the window. Auto: 30 ms for small changes down to 10 ms at 4x or 1/4x ("small narrow-range time (length) changes require longer crossfades while larger changes in length require shorter crossfades") |
  | Min Pitch (40-1000 Hz) | the search reaches one period of it each way |
  | Accuracy (sound to rhythm) | how much of that reach is used: towards rhythm a window stays near its place, keeping the timing; towards sound it moves further for a smoother join |
  | Ratio (0.25 to 4.0, p.857) | output length over input length; values outside are held to the range |

* Channels are stretched together, the joins found on their sum, so a
  stereo pair keeps its image ("processing the two sides of the stereo
  signal relative to each other", p.856).
* Ours: the correlation uses every other sample, which is enough to tell
  the best join and halves the work.

## Rhythmic material (`media::rhythmicStretch`)

Time Shift's Rhythmic mode (p.858-860). The sound is cut at its
transients, found in 5 ms blocks: a block 6 dB or more above the one before
it and above the Threshold (0 to -40 dB below the loudest block, default
-6 dB, p.859), at least 30 ms after the last (ours). Each piece is moved to
its stretched start and kept as it is, so hits are neither smeared nor
doubled. A piece longer than its new place fades out over the Decay Rate
before the next hit; after a shorter one there is silence. Without
transients it is the WSOLA stretch.

## Pitch (`media::pitchShift`, `media::varispeed`)

* Varispeed is a resample (swresample, through `media::Resampler`): the
  sound read as if recorded faster, so shorter and higher together, "like a
  tape recorder that is varispeeding" (p.851).
* A pitch shift with Time Correction is a stretch by the pitch factor and
  then varispeed by it: the pitch moves and the length is back. A stretch
  past 4.0 or under 0.25 (Time Shift's two octaves with a length change) is
  done in steps.

## The processes (`media::audioSuitePlugins`)

All three are for creating new master clips only, as the guide has them
("Create new media with a longer or shorter duration than the source
media", p.801), and the new clip is as long as what they made, in whole
frames ("the plug-ins automatically lengthen or shorten the clip", p.803).

* **Pitch Shift** (p.850-851): Gain, Coarse (semitones, an octave each
  way), Fine (cents), Crossfade, Min Pitch, Accuracy, Time Correction (on
  by default; off varispeeds). Ours: the Ratio slider, linked to Coarse and
  Fine in the guide, is not shown, nor the Reference Pitch tone.
* **Time Compression Expansion** (p.856-857): Ratio, Crossfade, Min Pitch,
  Accuracy. Ours: the Source and Destination lengths in other units, and
  tempo and bars:beats, are not shown; Ratio sets the length.
* **Time Shift** (p.857-860):

  | Control | Ours |
  |---|---|
  | Mode: Monophonic, Polyphonic (default), Rhythmic, Varispeed | Monophonic: windows three periods of the range's lowest pitch, the search towards sound. Polyphonic: the Window control's length. Rhythmic: cut at transients (above). Varispeed: time and pitch linked |
  | Range: Low, Mid, High, Wide | the lowest pitch for the joins: 40, 80, 160 and 40 Hz (ours) |
  | Gain (0 to -6 dB) | attenuates the input |
  | Time Shift (25% to 400%) | the speed: 25% is four times the length |
  | Window (6 to 185 ms, default 18) | Polyphonic's window |
  | Threshold (0 to -40 dB, default -6), Decay Rate | Rhythmic's transients and fades (Decay Rate's 10-1000 ms range is ours) |
  | Transpose (-24 to +24 semitones) | the pitch, the length kept (not in Varispeed) |

  Ours: the Formant shift, the Follow envelope and the Shift percentage
  beside Transpose wait; so do the Units menu and the Processed End,
  Length and Tempo boxes (the Time Shift percentage sets the length).

## Tests

`tst_time_stretch`: a tone stretched from 1/4 to 4 times keeps its pitch and
stays pure (a stretch without the search scores far worse); a stereo pair
keeps its relation sample for sample; Pitch Shift with and without Time
Correction, and two octaves with a length change; hits a quarter second
apart come half a second apart when stretched twice; the three processes'
lengths and pitches. `tst_audio_suite newMasterClip`: the new clip's length
after Time Compression Expansion and Time Shift.
