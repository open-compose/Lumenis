# The compositor (Phase 6.1)

Pages are the FX Guide's unless marked "Editing Guide".

Code: `media::composeFrame`, `composeImage`, `trackFrame`, `Picture` and the blends `mix`, `over`,
`faded`, `filmMix` (`src/media/compositor.*`); `MainWindow::shownVideoTracks` and the Composer's
picture resolver in `MainWindow::createPanels`. Test: `tst_compositor`.

## Making a sequence frame

* Effects are transition effects at a cut, or segment effects on a segment, single-layer or
  multilayer (p.33). A sequence frame is made from its video tracks bottom to top: each track gives
  the picture of its segment at that frame, through the segment's effect, or within a transition the
  blend of the outgoing and incoming segments; each track's picture goes over the ones below.
* Pictures are red, green, blue and alpha in floating point, premultiplied. Filler is transparent, so
  lower tracks show through it; where no track has a picture the frame is black. A clip's frame is
  fitted inside the frame, keeping its shape.
* Which tracks: "When there are multiple video tracks, all tracks below the monitored track are active
  during playback" and "Unmonitored tracks are not included in playback" (Editing Guide p.627). The
  Record monitor shows the video tracks up to the top monitored one, leaving out unmonitored ones; the
  Source monitor shows every video track.
* Effects processing: after each effect the picture is rounded to 8 or 16 bits a channel, or kept in
  32-bit floating point (p.151; the Render settings' Effects Processing, 2026.8). The monitors use 16
  bits until the setting comes with rendering (6.4).
* When the top track with a picture shows one plain segment (no effect, no transition), the clip's frame
  is shown as it is, without compositing (ours, for speed).
* Offline media shows the monitors' Media Offline; a clip that never had media is black.

## Transitions

* A transition lies on the cut where its segment starts (`model::Segment::transition`), `before()`
  frames on the outgoing side and `after()` on the incoming; the outgoing segment's frames past its end
  and the incoming one's before its start are the handles (p.48).
* Ours: the progress at a frame is (frames into the transition + 1) / (its length + 1), so that neither
  end frame shows only one side.
* Dissolve "blends images from the outgoing video with the incoming video over time"; Fade to Color
  "fades from the outgoing video to any color"; Fade from Color "fades from any color to incoming
  video"; Dip to Color "fades from the outgoing video to any color and then fades up to the incoming
  video" (p.457), through the colour at the middle. The colour is the Background Color parameter, black
  by default.
* Film Dissolve and Film Fade "emulate film's response to light" (p.462). Ours: the blend is done in
  density (log10 of the light), which darkens the middle of a dissolve between bright pictures as film
  does; black ends stay black.
* Swap Sources "Swaps the image source for the foreground and background layers of the effect" (p.495).
* The 2D transitions (Box Wipe to Squeeze) are in docs/spec/wipes.md, the geometric image effects
  (Picture-in-Picture, Resize, masks, Reformat) in docs/spec/image-effects.md, the keys in
  docs/spec/keys.md. A transition effect not built
  yet is a cut at the cut point until its step.

## Segment effects

* Superimpose "Blends the image from the top video track (where you apply the effect) with the image from
  the bottom video track ... When Level=100, only the top image is visible, and when Level=0, only the
  bottom image is visible" (p.458). Ours: 50 until a level is set; keyframed levels come in 6.2.
* Other segment effects leave the picture as it is until their step.

## Not built yet

* Keyframed parameters, the Fade Effect button (p.46) and Reverse Animation (6.2).
* Gate 1's comparison with Media Composer's frames (M).
