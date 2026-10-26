# AudioSuite (Phase 5.5c)

AudioSuite processes stretches of sound offline, rather than as they play
(Editing Guide p.796-805). The guide offers two ways to use a plug-in
(p.796): on a clip in the Timeline, rendered into new media, or on master
clips, making new master clips. 5.5c1 did the first; 5.5c2 the second and
AudioSuite effect templates.

## Which plug-ins

* Avid's AudioSuite plug-ins come from Avid's own plug-in format, whose
  SDK is not public. Cutthulhu does not host them (docs/EXTENSIONS.md).
* The Plug-In Selection menu (`media::audioSuitePlugins()`) lists:
  * **Our processes** ("Process"), named after the guide's AudioSuite-only
    plug-ins and doing what it says they do. Ranges are ours unless cited.

    | Process | Parameters | Guide |
    |---|---|---|
    | Gain | gain, -∞ to +24 dB | p.844 |
    | Normalize | level (-40 to 0 dB), calibration (Peak or RMS), processing ("Levels On All Chans-Tracks", "Level On Each Chan-Track") | p.850, p.799, p.803 |
    | Invert | none | p.845 |
    | Reverse | none | p.853 |
    | DC Offset Removal | none; takes away the mean | p.836 |
    | Signal Generator | frequency (20 Hz to 20 kHz), level (-95 to 0 dB), signal (Sine, Square, Sawtooth, Triangle, White Noise, Pink Noise), calibration | p.855 |

    * Peak and RMS calibration scale the result so its peak, or its RMS,
      is the level asked for (p.844, p.850, p.856).
    * Noise is the same each time (a fixed seed), so a render can be made
      again exactly. Pink noise is Paul Kellet's filter of white noise.
  * **Every audio effect** (`media::audioEffects()`: the built-in Audio
    Track Effects and CLAP plug-ins), run over the sound offline, as the
    guide's "Audio Track Effect and AudioSuite" plug-ins are (p.805). The
    sound past the segment's end (a reverb's tail) is cut, as a segment
    keeps its length.
* **Duplicate** (p.839) copies the clip between its marks into a new master
  clip; it is offered only in master clip mode ("This plug-in applies only
  when you use the Create New Master Clips features").
* Later: Pitch Shift (p.850), Time Compression Expansion (p.856) and Time
  Shift (p.857), which need a time stretch, and change the length (master
  clips only, p.805).

## On segments (`model::AudioSuiteEffect`, `Segment::audioSuite`)

* An audio segment has at most one AudioSuite effect: the process's id,
  its name when applied, its parameter values, and its render.
* **Rendering.** "You need to render all AudioSuite plug-ins before you can
  play back the effect" (p.800).
  * `media::renderAudioSuite` reads the segment's source sound
    (`media::segmentSource`: its clip's track from its source frame, one
    frame longer than the segment), runs the process over it, and writes
    one mono PCM file of managed media on the target drive, named "<clip>
    <effect>".
  * The segment keeps the file, its stream and channel, and a key of what
    it was made from (`model::audioSuiteKey`).
* **The render order** (p.700): clip volume, AudioSuite, EQ, fades and
  dissolves, automation.
  * The render holds the clip gain. Playback plays the rendered file at
    0 dB clip gain; the pan, EQ and automation still apply.
  * A render is out of date when the clip, track, source frame, length,
    clip gain, process or values change ("if you change the clip volume
    ... the system unrenders the AudioSuite plug-in", p.700). Automation,
    EQ and pan do not unrender it.
  * An out-of-date or unrendered effect plays the source unprocessed (ours;
    the guide says only that it must be rendered to be heard).
* **Audio Mixdown** renders what is not rendered on its tracks and range
  first, as an undo step of its own, on the mixdown's drive ("Media
  Composer automatically renders the effect before it creates an audio
  mixdown", p.800).
* Remove Effect takes the AudioSuite effect off the segment under the
  position on the selected tracks, with its EQ.
* The Timeline shows a plum icon on a segment with an effect, with a blue
  dot until it is rendered. With an EQ there too, only the EQ's icon shows
  (p.700).
* Sequences keep it as `audioSuite` (`model::audioSuiteToJson`).

## The tool (`ui::AudioSuiteTool`, Tools > AudioSuite)

The controls follow the picture on p.797:

* **Track Selection Menu**: the sequence's audio tracks. Choosing one
  selects it alone in the Timeline; Shift adds or removes it. With several,
  the button shows "+" (p.797).
* **Plug-In Selection** menu: the plug-ins by category, then None (ours).
  Choosing one puts it, with its defaults, on the segment under the
  position on every selected track (p.797), an undo step.
* **Activate Current Plug-In** opens the plug-in's dialog
  (`AudioSuitePluginDialog`, p.798-799):
  * the common buttons: OK (saves the values), Cancel, Render (saves and
    renders); Preview, Bypass and Find Level dimmed;
  * Plug-In Settings and Compare, dimmed;
  * a slider or a menu for each parameter (Input Mode is Normalize's
    processing menu).
* **Render Effect** renders the effects under the position on the selected
  tracks, one undo step.
* **Fast menu** (p.799): Set, Render and Remove AudioSuite, named for the
  marks: "Global" without marks, "IN/OUT" with an OUT, "From IN" with an IN
  alone. Set copies the shown plug-in and its values onto every segment of
  the selected tracks between the marks.
* **Status display**: the tool's track and its segment's effect, rendered
  or not.
* **Target Drive** menu: where renders go; it starts at the Media Creation
  setting's audio drive.
* **Effect icon**: dragged to a bin, it saves the shown plug-in and its
  values as a template, named after the plug-in (p.798); in master clip
  mode the template carries the clip too.
* **Fast menu > Templates** (p.799): "Apply an existing AudioSuite
  template". Media Composer lists the templates of its Site AudioSuite bin
  (p.804); ours lists the AudioSuite templates in the open bins. Applying
  one puts its plug-in and values on the segments under the position, one
  undo step, or in master clip mode makes them the next render's.
* Dimmed until later: Audio Loop Play and the Nested Level Selection menu
  (real-time EQ in AudioSuite, p.800).

## Creating new master clips (p.800-804)

* **Entering the mode.** Dropping master clips or subclips with sound on
  the window adds them to the Clip Selection menu, makes the last one
  active, turns on Toggle Master Clip Mode (yellow, p.802) and shows the
  controls (p.803). Display/Hide Master Clip Controls shows or hides them.
  Other items are refused, with a message.
* **The controls** (picture and table on p.801-802):

  | Control | Ours |
  |---|---|
  | Clip Selection menu | the dropped clips; choosing one makes it active |
  | Input Source Track selectors | the active clip's audio tracks (its Tracks column), all on at first |
  | Processing Mode Selection menu | Normalize's two modes (p.803); "Mono" for the rest, which work on each track alone |
  | Target Bin for New Master Clip | the open bins that can be changed; the Source monitor's clip's bin at first |
  | Status display | the clip, how many of its tracks are used, the plug-in |
  | Load Result | loads the new clip into the Source monitor |
  | Handle Length (seconds) | silence added after the material, rounded up to whole frames, for a tail to ring into |
  | Load In Source Monitor | loads the active clip into the Source monitor, to mark it |
  | Toggle Master Clip Mode | yellow while on; off goes back to the Timeline |
  | Mark IN to OUT indicators | green when the clip has a mark in the Source monitor |
  | Find Source From Effect | active once a template is dropped |

  The preview track (Alt+click, the blue Speaker icon) waits for Preview.
* **Render Effect** (p.803, `media::audioSuiteClip`) reads the chosen
  tracks between the clip's marks (the whole clip without them), adds the
  handle, runs the plug-in over all the tracks together, and writes them as
  managed media on the target drive: a new audio clip with one track per
  track used, starting at the clip's timecode at its IN. Then (p.803-804):
  * the new clip goes in the target bin, named "<clip>_<effect>" on
    Windows and Linux, "<clip>.<effect>" on macOS;
  * an AudioSuite effect template goes beside it, named "<clip> -
    AudioSuite Plug-In Effect: <effect>" on Windows and Linux,
    "<clip>.<effect>.effect" on macOS (the guide's Mac example,
    "QuietClip.NormalizeQuietClip.Normalize.effect", repeats itself; we
    read it as "QuietClip.Normalize.effect").
* **Templates** (`model::AudioSuiteTemplate`, p.804) are Effect items in
  bins. They keep the effect and its values and, when made from a master
  clip, that clip (its bin and id), the tracks used, the marks then and the
  handle.
  * Dropped on the window, a template turns on Find Source From Effect. It
    loads the template's clip as the active clip, with the template's
    plug-in, values, tracks and handle; its marks stand until the clip is
    marked in the Source monitor. "A template in the AudioSuite window must
    reference an existing master clip": otherwise it says the clip cannot
    be found.

## Later

* The Site AudioSuite bin (p.804) and dragging a template onto a segment.
* Several clips rendered at once, and stereo processing of track pairs
  (p.803-805).
* Preview, Bypass, Find Level, Plug-In Settings and Compare (p.798-799).
* Nested levels and real-time EQ in the tool (p.800).
* AudioSuite effects on multichannel tracks (docs/spec/multichannel.md).
* Cancelling a render (p.805), and a progress bar while it runs.
* Rendered files belong to no clip: the Media Tool lists them with the
  other media files, and Delete Media can delete them (the segment then
  plays unprocessed until rendered again).

## Tests

* `tst_audio_suite` (media): the processes, an effect offline equal to it
  live, the effect on segments and its render key, and a render played
  back at the gain the render holds; templates and their names; new
  master clips from a clip's tracks, marks and handle, and Duplicate.
* `tst_audio_suite_ui`: the tool's menus, dialog, Render Effect and Fast
  menu; master clip mode's controls and what a render asks for; Find Source
  From Effect and templates; in the application, a render that plays, Audio
  Mixdown rendering first, a new master clip with its template, and a
  template dropped in a bin.
* Scenario `11-audiosuite.json`.
