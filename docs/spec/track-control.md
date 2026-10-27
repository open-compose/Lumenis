# The Track Control panel and track display (UI completion pass U1b1)

The Timeline Fast menu's Track Control Panel, Allow Per Track Settings, Track Color, Segment Selection,
Mark IO Selection and Show Markers, and Enlarge and Reduce Track. Pages are the Editing Guide's.

Code:
* `Timeline` and `TimelineView` in `src/ui/timeline.cpp`: the per track settings section, the panel's
  painting (`paintControls`) and clicks.
* The settings on `model::Track` (`src/model/include/lumenis/model/sequence.h`).
* Inactive tracks in playback (`src/media/playback.cpp`).

## The Track Control panel (p.601-602)

* **Shown** from the Fast menu's Track Control Panel, or with "the Track Control Panel button above
  the Timeline" (p.602). Ours: a small arrow at the right of the Track Selector's corner above the
  tracks, pointing left while the panel is shown.
* **Where:** right of the Track Selector, on audio tracks (the picture on p.602). Video tracks have
  no buttons (ours: the guide describes the panel as "useful when you edit audio tracks").
* "The Track Control panel arranges components in two rows of tools" (p.602). Ours:
  * Waveform, Clip Volume/Pan, Inactive and Solo on the top line;
  * the inserts a-e and Mute below.
  * Audio rows are made tall enough for both lines while the panel shows.
  * "If you reduce the size of the Timeline tracks, you might not see the Track Control panel tools"
    (p.602): a row too short for both lines shows only the top line.
* **Waveform** "Turns on or off the waveform display for individual tracks" (p.602).
  * "Alt+click ... a Waveform or Clip Volume/Pan button to display all waveforms or pan displays"
    (p.677): every audio track.
* **Clip Volume/Pan** "Turns on or off the clip volume and pan display for audio tracks" (p.602).
  * A click opens a menu of Clip Volume, Volume and Pan (p.679-680).
  * "If you want to view both clip volume and volume values, repeat the previous step and select an
    additional volume value" (p.680): Clip Volume goes with Volume or Pan. Volume and Pan are one
    or the other, as in the Audio Data menu.
  * Alt+click: every audio track.
* **A click on Waveform or Clip Volume/Pan turns Allow Per Track Settings on.** Ours: the guide has
  it turned on first (p.678), and a click that showed nothing would look broken.
* **Inactive** "Disables a track so you can play back your sequence without processing the plug-in
  effects or automation for the inactive track" (p.602).
  * Playback, mixdowns and renders leave out the track's inserts (`media::trackInserts`) and its
  volume and pan keyframes (`media::audioPieces`). The clip gain and pan stay.
  * Ours: the track's segments are drawn darker.
* **Solo and Mute** are the Audio Mixer's (p.698-699). "Allows you to monitor a single track of audio
  without deselecting other tracks" and "mute a single track of audio without deselecting it"
  (p.602). The panel's buttons and the mixer's strips show the same state
  (`AudioMixer::setSoloMute`).
* **The inserts a-e** show the first two letters of each effect (ours; the pictures on p.602 and p.795
  show "EQ", "Dy" ...). Lit while the insert is on, dark when it is empty or bypassed.
  * A click opens the Audio Track Effect tool on that track and insert: "When you select an insert
    button in the Track Control panel ... the controls for the plug-in appear in the Audio Track
    Effect tool window" (p.792). On an empty button it is the tool, to insert one (p.602).
  * A drag to another insert button moves the insert, replacing what is there; Alt+drag copies it
    (p.795, `Timeline::moveTrackInsert`).

## Per track settings (p.677-680)

* "Media Composer stores per track settings with the sequence and does not apply them to other
  sequences" (p.677).
  * Kept on `model::Track` (`showWaveform`, `showClipVolume`, `showVolume`, `showPan`, `inactive`),
    in the composition's `display` object and `inactive`.
  * Changing them takes no undo step, as for track locks (ours).
* **Audio Data > Allow Per Track Settings** shows them. Turned off, "This disables the display of
  waveforms, but it does not change the per track settings. Enabling per track settings again
  restores your per track waveform displays" (p.679).
* A track shows a waveform when the Audio Data menu's Waveform is on, or when the per track settings
  are allowed and its own is (`Timeline::showsWaveform`); Clip Volume likewise. A track's own Volume
  or Pan takes the place of the Audio Data menu's (`Timeline::automationShown`).
* Keyframes are edited on the graph their track shows. With none chosen in the Audio Data menu, the
  graph of the track clicked last is the one edited (ours).
* The Clipboard holds material, not these settings (`Sequence::excerpt`).

## Track heights (p.594, p.601)

* "Select the tracks ... Press Ctrl+L ... to enlarge the track, or Ctrl+K ... to reduce the track,
  which changes the height of all highlighted tracks" (p.594).
  * The keys are Edit > Enlarge Frame and Reduce Frame's (`edit.enlarge_frame`, `edit.reduce_frame`).
    They act on the Timeline when it is the active panel, on a bin otherwise (p.250).
  * With no track selected, nothing happens, with a message.
* **Ours:**
  * each step is 8 pixels;
  * from 4 steps smaller to 8 steps larger than the track's default height;
  * the height is kept with the sequence (`Track::heightStep`).
* The waveform grows with its track (p.678).

## Colours (p.593, p.597)

* **Track Color** "Displays a palette for changing the color of the tracks in the Timeline" (p.593).
  * The palette is the clip colours' (`addClipColorMenu`), with Default first.
  * The colour is kept with the sequence (`Track::color`).
  * It goes on the selected record tracks. "A track must be selected in the Timeline for the Track
    Color command to appear" (p.593): it is dimmed without one.
  * "press the Alt key ... The Select Color dialog box opens allowing you to pick a basic or custom
    color" (p.597). Ours: the palette's Other... item opens the system colour dialog.
  * "Displaying clip colors overrides any track color" (p.595).
* **Segment Selection** "Allows you to set the segment selection to a default color or displays a
  palette for changing the color of the selected segments" (p.593).
  * Ours: selected segments are filled with that colour; the default draws them lighter.
* **Mark IO Selection** likewise for "the marked IO section". Ours: the colour is drawn
  see-through over the selected tracks.
* Both are Timeline display settings, not kept with the sequence; a Timeline view keeps them
  (docs/spec/timeline-views.md).

## Show Markers (p.599)

* "When you select a color from the Show Markers submenu, only markers of that color appear in the
  Timeline. You can select All from the Show Markers submenu to display all the markers, or you can
  select None" (p.599).
* Ours: each colour is a check item; any number can be on. All and None set every colour.
* "Show Markers affects only how the marker icons display in the Timeline and does not affect the
  markers" (p.599).

## Later

* Pan L and Pan R > [speaker layout] in the Clip Volume/Pan menu for surround sequences (p.679-680).
* Dragging an effect from the Effect Palette onto an insert button (p.792).
* Ctrl+Bypass turning the insert buttons blue (p.794).
* Inactive video tracks.

## Tests

`tst_track_control`:
* the settings' JSON and the Clipboard;
* inactive tracks without inserts in playback;
* the panel, shown from the Fast menu and its button;
* Waveform and Alt+click;
* Allow Per Track Settings;
* the Clip Volume/Pan menu;
* Inactive, Solo and Mute with the Audio Mixer;
* insert clicks and drags;
* Enlarge and Reduce Track and their limits;
* Track Color in pixels;
* Segment Selection's colour;
* Show Markers.
