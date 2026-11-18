# Volume and pan automation (Phase 5.2)

Volume and pan automation changes a segment's level and pan inside it,
with keyframes and linear ramps between them (Editing Guide p.714-723).
The Timeline shows them as a graph, and the Audio Mixer's Auto mode sets
and records them. Playback, scrubbing and Audio Mixdown play them.

## The model (`src/model/automation.*`, `Segment::volumeKeys`, `panKeys`)

* **A keyframe** (`model::AudioKey`) is a place and a value:
  * The place is in frames of the source clip, from its start. A fraction
    is a sub-frame keyframe (p.720-721). Ours: counting in the clip's frames
    means keyframes move with their material. A trim hides or shows them
    ("hidden keyframes", p.723), and both halves of an Add Edit keep them all.
    Halves with the same keyframes still join as a match frame.
  * Volume values are dB added to the clip gain: "values set by the Audio
    Mixer tool are additive to the system clip volume values" (p.701).
    `kGainOff` is -∞.
  * Pan values run from L100 to R100. **Ours:** pan automation takes the
    place of the clip pan instead of adding to it (the guide does not say).
    A pan keyframe made where the clip pan is centred takes the track's side
    (odd left, even right, unless the Audio setting centres mono tracks,
    p.627, p.1147), so adding one changes nothing that is heard.
* **Values between keyframes** follow "a linear ramp" (p.714): in dB for
  volume, and the first keyframe's value before it, the last one's after.
  **Ours:** a -∞ keyframe counts as -96 dB in the ramps to and from it, and
  is silent only where it is.
* Segments save their keyframes as `volumeKeys` and `panKeys`:
  `[[frame, value], ...]` (Sequence::toJson).
* **Functions:**
  * `addKeyframes`: Add Keyframe adds one holding the value that plays.
  * `setKeyframes`, `moveKeyframes`, `deleteKeyframes`: dragging, nudging and
    Delete. A keyframe moved in time stays on its material and takes the
    place of a keyframe it lands on.
  * The Auto mode Fast menu (p.719-720): `filterAutomation`,
    `adjustAutomation`, `removeAutomation` and `removeHiddenKeyframes`.
    * Filter removes the points on straight lines between their neighbours
      ("redundant points and points on a linear ramp"). Then it removes every
      other point that is not a peak, until about half are gone. With Alt it
      keeps only the first, the last and the peaks.
    * Adjust adds to the keyframes in the range. A segment there without
      keyframes gets one that sets the whole segment.
    * Remove Hidden first puts a keyframe at the edge of the material with
      the value that plays there (ours), so what is heard stays the same.
  * `copyKeyframes` and `pasteKeyframes`: by keyframe, or by region (p.721-722).

## The Timeline (`timeline_automation.cpp`)

* **Audio Data** in the Fast menu (p.677-680):
  * Clip Volume draws the clip gain's line. Volume or Pan draws that
    graph, one at a time (p.715).
  * Volume uses the Audio Mixer fader's scale (+12 dB at the top, -∞ at
    the bottom). Pan runs from L100 at the top to R100 at the bottom (ours).
  * A segment with automation that is not shown gets a pink triangle (p.715).
  * **Ours:** Audio Data acts on every audio track. The Track Control
    panel's per-track Clip Volume/Pan buttons and Allow Per Track Settings
    come later. Audio rows are taller while a graph or line shows (Enlarge
    Track comes later).
* **Add Keyframe** (the ' key, p.716; `fx.add_keyframe`, also on the Command
  Palette's FX tab): adds a keyframe at the Record position on each
  selected audio track. Without a graph shown it adds a volume keyframe and
  shows the Volume graph.
* **Mouse:**
  * Click a keyframe to select it (pink), and Shift-click to add or remove
    one (p.721-722). Clicking elsewhere deselects.
  * Drag a keyframe up or down. A keyframe at the same place on another
    selected track moves too (p.716). With the position in a marked region,
    all the region's keyframes move. A multi-keyframe selection moves
    together (p.723).
  * Ctrl-drag snaps the dragged keyframe to the scale's lines: the fader's
    marks, or L100/L50/MID/R50/R100. Alt-drag moves it in time by whole
    frames, and Ctrl+Alt between frames (p.716, p.721).
  * Ctrl+Shift+click on a track adds a keyframe there, and Ctrl+Shift+Alt+click
    adds a sub-frame one (p.721).
  * **Ours:** dragging the value of a keyframe also changes the hidden copy
    an Add Edit left at the same place in the other half, so the ramp
    across the edit stays continuous. Moving in time moves only the visible
    one.
* **Keys:** Delete over a keyframe deletes it and its twins on the other
  selected tracks (p.717). Delete with keyframes selected deletes them.
  Ctrl+Shift+Up/Down moves the selection 1 dB, and Ctrl+Shift+Left/Right one
  frame (p.723).
* **Copy and paste** (p.721-722):
  * Ctrl+C with keyframes selected copies only them. Ctrl+V then pastes
    them at the IN, else the position, onto the selected audio tracks.
  * With a graph shown, Ctrl+C also copies the keyframes of the marked
    region, or else of the selected segments, alongside the segments.
    Edit > Paste Audio Keyframes (Shift+Ctrl+V) pastes these.
  * The paste dialog asks for Automation Gain, Automation Pan, or both.

## The Audio Mixer's Auto mode (`audio_tools.cpp`)

* **Faders and pan** show the automation at the position (p.716). The fader
  area is blue (p.717). The level display says "Clip" when the segment has
  clip volume, and in Clip mode "Auto" when it has automation (p.694).
* **Setting a fader or pan** sets the keyframe at the position. Otherwise:
  * with the position inside the marked region, every keyframe in it moves;
  * else the keyframes either side of the position move ("you can adjust
    the volume between keyframes"), or the only one does;
  * else a first keyframe is made, which sets the whole segment (p.716).

  They all move by the same amount, so the position gets the value set.
  Ganged faders move together (p.704).
* **Record** (the Rec button, or B in the mixer, p.718):
  * It plays from the IN, else the position, backed up by the preroll in
    seconds (p.717).
  * Each fader or pan touched writes its value at every frame the position
    reaches between the marks, as frame-accurate keyframes (p.721).
  * It stops at the OUT plus the postroll, when playback stops, or on Rec
    again. That keeps the recording as one undo step; Cancel discards it.
* **The Fast menu** acts on the selected tracks between the marks: IN to
  OUT, IN to the end, the start to OUT, or the whole tracks (p.719-720):
  * Filter volume automation (with Alt: only the peaks) and Filter Pan.
  * Adjust Volume/Pan on Track opens a dialog: Volume Adjustment in dB,
    and Pan Adjustment (ours).
  * Remove volume automation, Remove Pan and Remove Volume/Pan.
  * Remove Hidden Volume Automation, Pan, and Pan/Volume. The "Global" items
    act on the marked area or, without marks, the whole track (p.723).
  * Calibrate Hardware Sliders and Set Display Options come later.
* **Go to Next/Previous Event** with the Audio Mixer the active window in
  Auto mode goes to the next or previous keyframe on the selected tracks
  (p.720).

## The sound (`src/media/playback.cpp`, `mixdown.cpp`)

`media::audioPieces` hands each segment's keyframes to its `AudioPiece`, in
output samples. `AudioMix` then works out the gains sample by sample:
* the clip gain plus the volume automation;
* the pan automation, else the clip pan;
* the equal-power pan law.
A mono mixdown takes the volume alone.

## Surround pans (11.4d2a)

"You can view the volume and pan automation values in the Timeline, including surround sound pan values for sequences
using a surround sound mix" (p.679). In a 5.1 or 7.1 sequence the Clip Volume/Pan menu of a mono or stereo track has, in
place of Pan, "Pan L > [speaker layout]" and, for a stereo track, "Pan R > [speaker layout]" (p.680), with the layouts
of p.679:

| Layout | Member of the channel's pan (`model::SurroundPan`) | Graph |
|---|---|---|
| Front speaker position | `front`, -100 (left) to 100 | left at the top, as Pan |
| Rear speaker position | `rear`, -100 to 100 | the same |
| Front and rear speaker positions | `frontRear`, 100 (front) to -100 (rear) | the front at the top |
| Center speaker position, displayed as a percentage | `center`, 0 to 100 % | 100 % at the top |

* **Ours:** a mono track's one channel is "Pan L" (the guide names L and R only); a 5.1 or 7.1 track keeps Pan, its
  channels having their own speakers.
* **The model:** `Segment::surroundKeys`, a `SurroundKeys` per channel with a keyframe list per layout, counted in
  the clip's frames as the other keyframes. They are `AutomationParam`s FrontL ... CenterR, so Add Keyframe, drags,
  Delete, Remove, Adjust, Filter and Remove Hidden on Track work on them as on Pan ("If you add a keyframe for pan, the
  keyframe applies only to the automation value displayed in the Timeline", p.716). A new keyframe holds the member's
  value that plays there. `model::surroundPanAt` gives the channel's pan at a frame: each member with keyframes takes
  their straight-line value; the others keep the segment's pan.
* **The sound:** `AudioPiece::surroundKeys` and `surroundAt`; the speaker gains (`media::surroundGains`) are worked
  out every 32 samples. An inactive track plays without them (p.602).
* Kept in Timeline views and per track settings by `model::automationKey` ("L.front", "R.center" ...;
  `Track::showSurround`).
* **The Audio Mixer's Auto mode (11.4d2b):** the Pan grids and the Advanced Panner are no longer dimmed. They show the
  pans at the Record position (`Timeline::surroundPansAt`). A finished move sets each member that changed as automation
  there, as a fader does (p.716; `Timeline::setSurroundAutomation`, one undo step "Surround Pan Automation"), with a
  stereo track's other channel as Stereo Link or Mirror says. While recording (p.717-719) every move is written at
  the position, and the members moved (only those) are written again as the position moves
  (`Timeline::writeSurroundAutomation` from `AudioMixer::recordTick`).
* **The Fast menu:** in a 5.1 or 7.1 sequence, Filter, Remove and Remove Hidden Pan on Track take the surround pans'
  keyframes too (ours); Adjust Pan on Track stays on the stereo pan.
* **Not yet:** copying and pasting surround keyframes.

## Checks

* `tst_automation` covers the model: ramps, sub-frames, -∞, keyframes
  through splices, Add Edit and saving, moves, deletes, and the Fast menu
  commands, hidden keyframes, and copy and paste; `surroundPans` the surround parameters.
* `tst_surround automatedPlayback` plays a pan moved from the left front to the left rear;
  `tst_surround_ui timelineSurroundGraphs` the Clip Volume/Pan menu's Pan L and Pan R and Add Keyframe on them;
  `tst_surround_ui autoModeGrids` the mixer's Auto mode, recording and Remove Pan on Track.
* `tst_automation_ui` covers the Timeline and the mixer:
  * Add Keyframe and Audio Data;
  * the mouse (hit test, drag, Ctrl+Shift+click);
  * copy and paste with the Paste Audio Keyframes dialog;
  * the Auto mode faders, recording (and Cancel, the marks, the postroll
    and B), the Fast menu, and Go to Next/Previous Event.
* `tst_mixdown`, gate 1's null test, now has two automated cases. They have
  ramps, a sub-frame keyframe, a hidden keyframe, a fade from -∞ and pan
  sweeps, mono and dual mono. Each is checked against a reference worked out
  apart from the model's code. The residual is about -137 dBFS.
* Edit scenarios (`tests/scenarios/07-volume-automation.json`):
  * keyframes stay with their material through a splice;
  * Add Edit, twin drags, and time moves of the visible keyframe only;
  * `53-surround-pan-automation.json`: Front and Rear keyframes through a splice.

## Later

* The Track Control panel's per-track buttons, Allow Per Track Settings,
  waveforms and Enlarge/Reduce Track.
* Lassoing keyframes (p.722).
* Delete Hidden Left/Right on a transition's right-click menu (p.723).
* Surround pans copied and pasted.
* Live Mix mode's Set Live Mix to Automation / as Automation (p.725-726).
* Avid controllers and fader hardware (p.718).
* Keyframes in AAF, OTIO, FCP XML and `.avb` interchange. They are not
  written yet, and imports do not read them yet.
