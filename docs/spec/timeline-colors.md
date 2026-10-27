# Clip colours in the Timeline

The Timeline can draw segments in colours that say something about their
clips: offline media, linked media, a frame rate or definition that does
not match, a colour given in the Timeline, or the clip's colour in its bin
(Editing Guide p.594-598). It can also colour the Track Selector's buttons
green for the source and blue for the record side (p.383). This was
postponed in 1.4 and done before Phase 5.6b2.

## The Clip Color dialog (`ui::ClipColorDialog`, `model::ClipColorOptions`)

Timeline Fast menu > Clip Color... (p.595-597). The options, in the
guide's order, which "indicates the priority order in which Media Composer
applies colors when you select more than one option" (p.596):

| Option | Colours | Here |
| --- | --- | --- |
| Resolution Tracking | MultiRez resolutions | dimmed: "only available if you are working in a MultiRez environment" |
| Offline | offline media | red by default ("offline clips are red", p.1113); a clip without media, unlinked, or whose files are missing, and a clip that cannot be found |
| Proxy | h.264 proxy clips | dimmed but on: "Proxy clip color is enabled by default" (p.598); yellow when proxies come |
| Linked Clips | linked (AMA) media | linked, not managed, media |
| Mixed Rates | "a different color is available for each frame rate" | the clip's FPS column against the sequence's rate; ten rates, 23.976 to 60, each with a swatch |
| SD/HD | SD clips in an HD project and the reverse | the clip's Format column against the project's raster; SD is up to 576 lines |
| Timeline Local | local colours (Set Local Clip Color) | |
| Source | the clip's colour in its bin | not for sequences, groups, motion effects and titles (p.596) |

* A swatch opens a colour grid, the bins' palette (p.596). Default Colors
  puts back the Offline, Mixed Rates and SD/HD colours (p.597); Linked
  Clips' too (ours).
* The choices are kept in the Timeline setting (key `clipColor`), and in
  each Timeline view (p.595; U1b2, docs/spec/timeline-views.md).
* Defaults (ours, on the watch list): Offline and Source on, so bin colours
  show as soon as they are set; Linked Clips, Mixed Rates, SD/HD and
  Timeline Local off ("Select Timeline Local, and then click OK", p.598).
  The colours other than offline red are ours.
* "Displaying clip colors overrides any track color" (p.594): a coloured
  segment is drawn in its colour in place of its track's.
* Whether a clip's media is online is looked up at most every two seconds
  per clip, so painting does not look at the disk each time.

## Set Local Clip Color (`Timeline::setLocalClipColor`, `Segment::localColor`)

* Select segments with a segment tool, right-click the Timeline Fast Menu
  button, Set Local Clip Color, and pick a colour or None (p.597-598). It
  is one undo step ("Set Local Clip Color").
* The colour is the segment's (`localColor` in the composition). The two
  sides of an Add Edit with different colours are not a match frame.

## The Select menu (p.613-614)

Right-click > Select, on the selected record tracks:
* Clips with Same Source Clip Color: "looks at all the selected clips,
  collects the source (bin) clip colors that are used by these clips and
  selects any other clips that use these colors";
* Clips With No Source Clip Color;
* Offline Clips;
* Clips With Same Local Clip Color (the selected clips' local colours).

## Show Source/Record colors in Timeline (p.383)

Interface settings, Timeline & Viewers. The selected source track buttons
turn green and the selected record ones blue, the colours of the
Composer's position bars (Show Source/Record colors in Composer). In
Source mode the source buttons keep their green.

## Checks

* `tst_clip_colors`:
  * the priority order, filler, the kinds without source colours, and the
    JSON round trip;
  * the dialog: its dimmed options, OK, Cancel and Default Colors;
  * Set Local Clip Color and its undo;
  * the Select menu's colour choices;
  * the blue record button.
* Scenario `14-local-clip-color.json`.

## Later

* Resolution Tracking (MultiRez, p.1112-1113) and Proxy colours.
* Track Color (p.597) and the Timeline Text Color setting (p.1219).
