# Pan and Scan (ROADMAP 6.7b1)

The Reformat effect that keeps the part of the picture inside a wire frame of the output's aspect ratio (FX
Guide ch.9, p.230-238; reference p.480, p.564). Code: `src/media/pan_scan.cpp` (the picture and its geometry,
`media::panScanFrames`), the compositor's Pan and Scan branch, the wire frame's handles in
`src/media/effect_handles.cpp`, the Actions in `src/model/pan_scan.*` and `src/ui/effect_editor_handles.cpp`.
Tests: `tst_pan_scan`, `tst_compositor` (reformat), `tst_effect_keyframes_ui` (panAndScan), scenario
`32-pan-scan-subdivide.json`.

## From the guide

* "Pan and Scan lets you select a portion of the media to reformat"; "By selecting a portion of each frame in
  your media for output, you reformat without distorting the media" (p.230). It "usually works as a vertical
  effect that you apply to a layer above the rest of your video material and that affects all the video
  material beneath it" (p.229); "Apply the Pan and Scan effect to its own video track after you finish editing
  the sequence" (p.231), or directly on segments joined by transitions (p.238).
* "Source ratio specifies the aspect ratio of your incoming media. Target ratio specifies the aspect ratio of
  the final output" (p.232): Source 1.33 (4:3), 1.66, 1.78 (16:9), 1.85, 2.35, 15:9 anamorphic, 16:9
  anamorphic; Target 1.33 (4:3) or 16:9 anamorphic (p.564). Neither is keyframeable.
* "In Effect mode, the Effect Preview monitor displays your original media and the Pan and Scan wire frame ...
  In Source/Record mode, the Client monitor displays the media inside the Pan and Scan wire frame. The portion
  of the image inside the wire frame expands to fill the entire monitor ... The media is distorted if either
  the Source or Target aspect ratio is 16:9 Anamorphic"; "To see the results of the Pan and Scan effect while in
  Effect mode, click the Play button" (p.233-234).
* "Each Target aspect ratio has default position and scaling settings. You can return to these default
  settings at any time by selecting the aspect ratio again"; Establish Origin "Saves the current Scaling and
  Position values as a customized default setting"; Reset to Origin "Resets the Scaling and Position values to
  the current customized Origin values. If you have not established an origin, Reset to Origin resets the
  values to the default values for the current Target aspect ratio" (p.235, p.564).
* Subdivide Effect "splits the Pan and Scan effect based on the segment boundaries of the selected video tracks
  underneath the Pan and Scan track"; it "works on the selected Pan and Scan effect, not on the entire track"
  and "does not subdivide segments connected by transition effects" (p.235-236, p.238).
* The wire frame "functions similarly to the Picture-in-Picture wire frame": resized by its handles or the
  Scaling sliders, moved by dragging or the Position sliders; "You cannot drag the wire frame off the screen";
  "You cannot change the shape of the wire frame, which is determined by the Target aspect ratio. Media Composer
  maintains the shape during scaling"; dragging inside it, "Movement is restricted to the most important axis.
  For example, if you go from a wide aspect ratio (16:9) to a narrow aspect ratio (4:3), you can drag the wire
  frame from side to side. If you go from 4:3 to 16:9, you can drag the wire frame up and down"; "Shift-click
  inside the wire frame, and drag it to a new position. You can move the wireframe in all directions"
  (p.236-237).

## Ours

* Geometry (`media::panScanFrames`): the frame is taken to have square pixels. An anamorphic Source fills the
  frame, squeezed; another is the largest centred rectangle of its ratio in it. The wire frame has the Target's
  ratio as the source picture shows it; at Scaling 100 and Position 0 (the Target's defaults) it is the largest
  such rectangle in the source picture, centred. Scaling (percent) sizes it about its centre and Position moves
  it in pixels of a 1920-wide frame, Y down (as for Picture-in-Picture). The output: the wire frame's contents
  stretched over the whole frame for a 16:9 anamorphic Target, else over the largest centred rectangle of the
  Target's ratio with black around it.
* Until chosen, Source is 1.78 (16:9) and Target 1.33 (4:3).
* Effect mode: while the Effect Preview is shown and not playing, the Record monitor composes with
  `Composition::panScanOriginal`, which passes Pan and Scan effects by; the wire frame and its handles (centre,
  top, side, corner) are the Effect Preview's overlay (6.7a4).
* Handles: each resize handle scales both ways by one factor; Scaling X and Y are ganged in the Effect Editor.
  The move follows the axis the source is wider in than the Target (across when equal), Shift both; the wire
  frame stays inside the source picture (centred along an axis where it is larger); the Position sliders are not
  held back.
* Establish Origin keeps the values at the position under the category's "Origin"; Reset to Origin puts them
  where the controls would (the selected keyframes, a keyframe at the position, or the plain values). Choosing a
  Target (also the one chosen) removes Scaling, Position, their keyframes and the origin.
* Subdivide Effect: an Add Edit in the effect at each cut of the selected tracks below its own, strictly inside
  it, unless the cut has a transition; each piece keeps the effect as it was (keyframes, being elastic, span
  each piece). One undo step.

## Not yet

* The Effect Grid's Safe Action and Thirds inside the wire frame (p.234): the Grid is not built.
* Real-time limits when combined with other Reformat effects (p.230): ours plays on the fly.
* Multiformat titles (p.231): titles come with 6.8.
