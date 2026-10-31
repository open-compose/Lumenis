# The Illusion FX effects (Phase 6.3d)

Pages are the FX Guide's. "The Illusion FX category contains a variety of transition and segment
effects" (p.463), described in a sentence each (p.464-467), with their parameters on p.508-527. The
reference rarely gives a range and never a formula, so:

* the slider ranges the reference leaves out are ours, marked `"ours"` in effect_parameters.json;
* the values until set are ours, in `media::parameterDefault` (shown by the Effect Editor too), chosen
  so that each effect shows itself when it is applied;
* the pictures are ours, as described below. Random effects are the same for the same Random Seed on
  every platform (our own hash and generator).

Sizes "in pixels" are pixels of a picture 1920 wide, scaled to the picture, so the monitors show what an
HD output would; positions are percent of the picture. The reference's parameter-category line "For all
effects: Color Mix Effect Input Parameters" (p.465; "Iris Effect Input Parameters", p.467) is read as
each effect's own Input Parameters, as the parameter chapter has them (effects.json).

## 6.3d2: colour, texture and drawing (segment effects)

Code: `detail::isIllusionSegmentEffect`, `illusionDefault`, `illusionSegmentPicture`
(`src/media/illusion_fx.cpp`), reached through `media::imageEffectPicture`. Test: `tst_illusion_fx`.

* Color Mix "lets you blend a solid color with the image": Color Level "0 (no color) to 100 (maximum
  color)" (p.508). Until set: orange at 50.
* Film Grain "adds a grainy texture ... such as 16mm stock" (p.464): Level, Size, Color ("Higher values
  add more color imperfections and fewer luminance imperfections"), Hilite Grain and Shadow Grain (in
  the brighter and darker areas). The grain changes every frame; Field Mode (every field) has no effect
  on our progressive pictures.
* Crystal "uses polygons to create a mosaic effect": Size "the radius of the polygons in pixels",
  Jitter ("0 creates regular hexagons"), Random Seed (p.509). Each polygon takes the colour under its
  centre.
* Flare "creates a random ring flare": Brightness (0 to 100), Size (the ring's diameter, percent of the
  width), Width (the ring's thickness), Amount (its distortion), Random Seed, X and Y (p.510). A soft
  warm ring and a faint glow inside it, added as light.
* Pattern Generator "creates a clip using a solid color, a variety of color bars, or a grid. The frames
  can be numbered automatically, and a circle can be animated" (p.464, p.520-521): the Pattern Type and
  Axis (X or Y) menus, the colour; the Number (the frame number plus Number Offset) and the Circle with
  their opacity, size, stroke or width, colour and position. The bars are the usual test patterns at
  their usual levels (75% bars at 191 of 255); EIA bars are drawn as 75% colour bars; Grid's background
  "is black if Base Color has 50% or greater luminance, or white if Base Color has less". Position is
  two sliders, X and Y. The number and circle are off (Opacity 0) until set.
* Lightning "creates a random lightning pattern": Amount (how much of the bolt shows), Branch
  Probability, Random Seed, Render Mode, the Core's and Glow's width ("radius ... in pixels") and colour
  (p.513-514). The bolt runs from the top of the picture to the bottom; it is always drawn anti-aliased.
* Sparkler "creates random sparks or fireworks" (p.525-526): Birth Rate a frame, Lifetime in frames, the
  centre, Length (the frames of each path shown), Speed Bias, Speed, Gravity, Air Resistance, Wind
  (only with Air Resistance) and Turbulence; the core and glow radii and colour. Sparks are simulated
  frame by frame from the effect's start; Link Speed waits for the centre's keyframes to be followed.
* Kaleidoscope: Symmetry ("the number of mirrors"), Angle, Scale, the Center and Offset, Anti-alias
  (p.513), and the Illusion FX Border (p.508). The picture is folded into Symmetry wedges about the
  centre, each mirrored; Offset 50 is none; the Border's Left, Right, Top and Bottom (percent) keep the
  source's edges out.
* Motion Blur "lets you blur the edges of moving objects": Amount "the length of the blur", Direction "0
  degrees is 3 o'clock. Positive values move counterclockwise" (p.515). A blur along the direction,
  centred on each pixel.

## 6.3d3: the distortions (segment effects)

Code: the same file; `warp` maps each pixel to the source point it shows, held inside `SourceBox`, the
Illusion FX Border. Test: `tst_illusion_fx`.

* The Border's Left, Right, Top and Bottom "prevent black edges of the source clip from warping into
  the effect" (p.508): ours, each insets the source by percent of the picture (0 to 50), and every
  sample a distortion takes is held inside what is left. Kaleidoscope and Crystal (p.465) use it too.
* Anti-alias "Sets whether or not transformed areas are smoothed" (p.521-528): on, bilinear samples
  (until set); off, the nearest pixel.
* Pinch "pinches the image in toward, or pushes it out from a user-defined point": Amount, Size "the
  diameter of the affected area", Position (p.464, p.521). Ours: Amount -100 to 100; Size is percent of
  the picture's height (60 until set); Position is two sliders, X and Y. A pixel at d (its distance over
  the circle's radius) shows the source at d(1 + k(1 - d)), k = Amount/100: toward the centre for
  positive values, out from it for negative.
* Sphere: Amount "Negative values distort the image inward. Positive values distort it outward"; Size
  "the diameter of the circle as a normalized proportion of the vertical size", ours in percent (p.526-
  527). A lens: the source at d(1 - k sqrt(1 - d^2)/2).
* Swirl: Amount "Negative values swirl the image counterclockwise. Positive values swirl it clockwise";
  Size as Sphere's (p.527). Ours: Amount 100 is a whole turn at the centre, easing as (1 - d)^2 to none
  at the circle's edge.
* Twist "twisting the two ends in opposite directions", with the Axis menu, X axis or Y axis (p.464,
  p.527). Ours: the picture is a ribbon along the axis, turned about it in opposite directions either
  side of the centre by up to Amount/100 half turns at the picture's ends; edge on it is gone
  (transparent), and turned past a quarter it shows mirrored.
* Wave: "a user-defined number of waves", Amount, Minimum and Maximum Size (the wavelengths, ours in
  pixels), Horizontal and Vertical Factor "0 (no distortion) to 100 (maximum distortion)", Number of
  Waves "superimposed upon each other", Speed "the progress of the effect", Random Seed (p.465, p.528).
  Ours: each wave has a random direction, wavelength and phase; at Amount 100 it moves pixels by up to a
  quarter of its wavelength (divided by the square root of the number of waves); Speed 100 is four
  periods. The reference's Amount text is Swirl's ("the scale of swirl"), read as the waves' scale.
* Radial Blur "blurs the image inward or outward from a user-defined point, and around a user-defined
  rotation": Zoom "Smaller values zoom in and larger values zoom out", Angle, Position, and the Render
  Mode menu, where "Locate shows the center marker" (p.464, p.522). Ours: Zoom -100 to 100 (0 none)
  blurs over up to a quarter of each pixel's distance from the centre, outward for positive values;
  Angle in degrees; Low, Medium (until set) and High Quality take 8, 16 and 32 samples; Locate shows the
  picture with a white cross at the centre.
* Ripple "creates ripples on the image, as if you dropped a stone into a pond" (p.464, p.523-524). Ours:
  each of Number of Ripples starts at a random point (Random Seed) and at a random Amount in its first
  third; its front moves out with Amount, at Wave Speed 100 across the diagonal twice over Amount 0 to
  100; Ripple Spread is the band behind the front (100: half the diagonal, at least one Length), with
  peaks Length pixels apart and Strength 100 moving pixels by half of Length. Reflections add each
  ripple mirrored in the four edges, as waves off a wall. The light (colour, Angle with 0 at 3 o'clock
  and counterclockwise positive) brightens the slopes facing it and darkens the others. Amount is 50
  until set, so the effect shows; keyframe it from 0 to 100 to drop the stone.

## 6.3d4: the transitions

Code: `media::isIllusionTransition` and `illusionTransitionPicture` (`src/media/illusion_transitions.cpp`),
reached from the compositor's `transitionPicture`; the helpers they share with the segment effects are
in `illusion_common.h`. Test: `tst_illusion_fx`.

* The foreground is the outgoing clip and the background the incoming one. Each effect's Swap Sources
  "Specifies whether to apply the effect to incoming or outgoing video" (p.512-525): it swaps them.
* Amount: "Typical keyframing has a value of 0 at the first keyframe and 100 at the last keyframe"
  (p.512). Ours: until it is set or keyframed, Amount follows the transition from 0 to 100, so every
  transition runs from the outgoing clip to the incoming one when applied. Pagecurl has no Amount and
  always follows the transition.
* Iris "wipes the foreground clip away to reveal the background clip, using a circle that radiates out
  from the center": Amount, Softness, X and Y (p.466, p.512). Ours: at Amount 100 the circle reaches
  the farthest corner; Softness 100 is a quarter of the width (5 until set).
* Melt: "The foreground clip 'melts' downward off the screen, while being distorted in a random 'liquid'
  manner along the horizontal lines; the vertical lines are not distorted": Bleed, Melt Up, Amount,
  Strength, Random Seed, Anti-alias, and the Border (p.466, p.515). Ours: each column falls by its own
  smoothly random distance (four sines across the picture), at Amount 100 at least the height; Strength
  100 lets columns differ by the height (30 until set); Bleed smears the falling picture's top edge
  down behind it, fading, gone by Amount 100.
* Pagecurl "treats the foreground video channel as though it were a sheet of paper being rolled over to
  reveal another page beneath ... adds a highlight along the curled edge": Turn Left, Reverse
  ("incoming video peels on"), Swap Sources (p.466, p.515-516; the table's pictures are not described).
  Ours: the page lifts from the bottom left corner toward the top right at 45 degrees, or from the
  bottom right toward the top left with Turn Left; it curls over a cylinder a tenth of the height
  across and lies back flat over itself, its back showing paler; the curl's outer edge is highlighted.
  Reverse runs the curl backward with the incoming clip as the page.
* Rollup "rolls up the foreground clip to reveal the background clip": Amount, Curl Size, Direction
  ("0 degrees is 3 o'clock. Positive values move counterclockwise"), Anti-alias, the Border (p.466,
  p.524). Ours: the page winds round a cylinder moving toward Direction, the outermost turn showing;
  Curl Size 100 is a roll a quarter of the height across (30 until set).
* Particle Blast, Particle Orbit and Particle Wind divide "the foreground image into circular particles"
  (Size their radius, Spacing between their centres, in pixels; Bumpiness "a shadow to the edge"), which
  Blast "blows away ... from the center outward", Wind "blows ... away directionally" (Angle "the source
  direction of the wind") and Orbit moves "around their original position" (p.466, p.517-519). Ours:
  Size 12 and Spacing 12 until set, so the particles overlap at rest; Bumpiness 0 until set (the
  reference's example starts it at 0 "so the transition begins smoothly", p.518). Blast and Wind: a
  particle starts up to 0.9 of Amount late at Front Speed 100 (80 until set) by its distance from the
  centre or from the side the wind comes from, plus up to a quarter at random with Front Randomness;
  Speed 100 moves four times as far as the farthest a particle must go (half the diagonal, or across
  the picture with the wind); Speed Randomness varies that by up to half; Acceleration 100 is full speed
  at once, 0 over a third of Amount. Orbit: each particle circles up to three spacings out, Speed 100
  being two turns over Amount, and shrinks to nothing with Amount, as the reference's example
  keyframes Size (p.518).
* Random Blend "uses a random pattern to blend the image of the incoming and outgoing video": Amount,
  Random Scale "This allows Media Composer to blend different portions of the image at different
  rates" (p.466, p.522). Ours: cells two pixels of a 1920 picture square; at Random Scale 0 a plain
  dissolve; the higher, the more each cell's moment varies and the quicker it blends (50 until set).
* Shear "divides the foreground clip into strips and moves alternate strips away in opposite
  directions": Amount, Min and Max Width, Width Randomness, Spread ("0 causes all strips to start at the
  same time"), Angle, Random Seed, Anti-alias, the Border (p.466, p.524-525). Ours: the strips run
  along Angle, between 20 and 80 pixels wide until set; Width Randomness 0 makes them all midway;
  Spread 100 starts them over the first half of Amount; each then leaves the picture by the end.

## 6.5d3b: FluidBlur, FluidColorMap and FluidMorph

FX Guide p.467, p.510-512. Code: `src/media/fluid_fx.*` (`fluidBlur`, `fluidColorMap`, `fluidMorph`) on our
optical flow (`src/media/optical_flow.*`, 6.5d1), called by the compositor with the frames around the one
shown. Tests: `tst_fluid_fx`, `tst_compositor`.

* FluidBlur: "The amount of blur is based on the amount of motion in the clip. Only objects that are moving
  from one frame to the next are blurred ... it blurs in the direction of travel" (p.467). Each pixel is
  averaged along its motion to the next frame. Blur Amount is the blur's length as a percent of the motion,
  ramped "from 0 to the Blur Amount value as the pixel motion increases from the Lower Motion Threshold value
  to the Upper Motion Threshold value", with nothing blurred outside them (p.510). Thresholds count pixels
  of a picture 1920 wide (ours). Trail adds a reach back along the previous frame's motion; Sharpen Leading
  Edge puts the whole blur behind the motion. At a segment's last frame the motion is the one from the frame
  before.
* FluidColorMap: "Direction of movement maps to unique chroma (color) values ... the Vectorscope layout ...
  as a compass of color values. Motion magnitude maps to luminance (brightness) and color saturation"
  (p.467). Ours: the direction (up the picture as up the scope) is the angle of Cb across and Cr up;
  brightness rises to 70% at the Max Motion Threshold, so the fastest motion keeps its colour; saturation
  rises from Constant Saturation to full, so at its maximum "motion has no effect on color saturation"
  (p.511). Where nothing moves the picture is black.
* FluidMorph: the Source menu's Still->Still, Stream->Stream (the default, ours), Stream->Still and
  Still->Stream (p.511): a still is the outgoing clip's frame at the transition's end, or the incoming
  clip's at its start. Feature Match (on until set, ours) moves both pictures along the flow between them
  and mixes them (media::interpolateFrames); without it "the morph [is] based on the brightness of the two
  images": each pixel changes over in a quarter of the transition, earlier the brighter the incoming
  picture is there. Swap Sources swaps them.
* Progressive Source (all three, p.510-512) is kept but changes nothing: ours always finds motion between
  frames. The Illusion FX Border and Grid are not used by these three yet. "You must render this effect to
  see the result" (p.467): ours shows them unrendered too, slowly.
