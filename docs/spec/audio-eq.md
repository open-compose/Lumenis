# Audio EQ (Phase 5.3)

The Audio EQ tool equalises whole audio segments with three bands: a low
shelf, a parametric midrange and a high shelf (Editing Guide p.736-745).
Playback, scrubbing and Audio Mixdown hear the EQ.

## The model (`src/model/audio_eq.*`, `Segment::eq`)

* **An EQ** (`model::AudioEq`) has these values:
  * the three gains, from +15 dB to -20 dB (p.739), in steps of 0.1 dB;
  * the low shelf's turnover: 50, 80, 120 or 240 Hz;
  * the high shelf's turnover: 6, 8, 12 or 15 kHz (p.738);
  * the midrange's centre, from 50 Hz to 15 kHz (the EQ Range slider's
    ends in the picture on p.737);
  * the midrange's bandwidth, 2 octaves or 1/4 octave;
  * In: whether the effect is on (p.738).
  `clampEq` puts values in range, and turnovers go to the nearest of
  their four.
* **Defaults** follow the picture on p.737: 0 dB, 50 Hz, 2 octaves, 15 kHz,
  and the midrange at 500 Hz.
* **Ours: the filters.** The guide names the controls but not the filter
  shapes. We use the widely published "Audio EQ Cookbook" second-order
  sections (Robert Bristow-Johnson):
  * shelves with a slope of 1;
  * a peaking filter whose bandwidth is in octaves.
  A band at 0 dB is no filter at all, so a flat EQ changes nothing. The
  high shelf's turnover is capped at 0.45 × the sample rate.
* `eqResponse` gives the gain at a frequency for the tool's graph.
* `setEq` sets an EQ on, or removes it from, every whole segment that
  overlaps a range of record frames: "Apply Audio EQ to entire segments
  only", and segments that fall within the marks "either in part or whole"
  get it (p.738). Filler and locked tracks are left alone.
* Segments save it as `eq` (Sequence::toJson). An EQ keeps an Add Edit from
  being a match frame, unless both halves have the same EQ.

## The sound (`src/media/playback.*`)

* `media::audioPieces` hands each segment's EQ to its `AudioPiece`.
* `AudioMix` reads each piece's samples, runs its three sections
  (transposed direct form II, in double precision), and then applies the
  gains.
* Each piece keeps its filter state from one render to the next, so a
  sound played in chunks is the same as one rendered at once.
* A render that does not follow on (a seek, or playing backwards) resets
  the state and warms it up on up to 0.1 s of the samples before.
* An EQ that is not In, or is flat, is not run.
* **Bypass RT EQ** (p.737) takes the EQ off what plays
  (`MainWindow::soundOf`). **Ours:** Audio Mixdown still renders it, as
  "rendered EQ effects still play correctly" (p.738).

## The tool (`src/ui/audio_eq.*`)

* **Opening:** Tools > Audio EQ (p.736), a panel ("AudioEQ"). Opening it
  from an Audio tool's Effect Mode Selector menu comes later.
* **The top row** (table on p.736-737):
  * the Effect icon (templates, below);
  * Audio Loop Play (docs/spec/audio-mixer.md) and Render Effect (later);
  * the Fast menu;
  * the Track Selection Menu button. Its menu lists the audio tracks, and
    choosing one selects or deselects it in the Timeline. With several
    selected, they carry plus signs, and so does the button.
  * In, and Bypass RT EQ.
* **The tool's track** is the first selected audio track (p.739). The
  values shown are those of its segment under the Record position.
* **Ours:** a segment without an EQ shows the last values set, so that an
  EQ can be built before it is applied.
* **The bottom part** (picture on p.737):
  * the EQ Parameter display: three value boxes;
  * three sliders with their scales;
  * the 3-band buttons (low turnover, bandwidth, high turnover), each
    click taking the next value;
  * the EQ Range slider, on a log scale, with its readout;
  * the EQ Parameter graph: 20 Hz to 20 kHz, with lines at 50 Hz, 100 Hz,
    500 Hz, 3 kHz and 12 kHz.
* **Setting values** (p.739):
  * click a number on a slider's edge;
  * drag a slider (the value is set when it is let go);
  * type on a slider: digits build a value until Enter;
  * type in a value box.
* **Each finished change** sets the EQ on the segment under the position on
  every selected audio track, as one undo step. **Ours:** the guide's step
  8 then applies it with Set EQ, but its example on p.741 shows a single
  segment that already has an EQ, so the change itself sets that segment.
* **The Fast menu** (p.740-741, picture on p.744):
  * **Set EQ - In/Out** copies the EQ of the tool's track's segment under
    the position onto that track's whole segments between the marks: IN to
    OUT, IN to the end, or the whole track. If that segment has no EQ, the
    range's EQs are removed. Only the tool's track is changed (p.741).
    **Ours:** an OUT alone acts from the start to the OUT, as in the Audio
    Mixer.
  * **Remove EQs On Track - In/Out** and **On Enabled Tracks - In/Out**
    remove EQs over the same range, on the tool's track or on every
    selected audio track.
  * The twelve predefined templates (p.743-744, names from the picture on
    p.744). Choosing one puts it on the segment under the position on the
    selected audio tracks, as an undo step (p.745). Its values go to the
    Console, as the note on p.744 says Media Composer's do.
* **Templates' values** are in a table, `src/ui/resources/eq_templates.json`,
  with each entry's source and a note. The guide gives none of them: it
  sends the reader to the Console of a seat.
  * **Telephone EQ "A"** is *observed*. The owner read it from the tool in
    a public video of Media Composer: low shelf -18 dB at 210 Hz,
    midrange +11 dB at 1222 Hz and 3/4 octave wide, high shelf -17 dB at
    6 kHz.
  * **Tape Hiss Filter** is *documented* as far as the guide goes: it
    "rolls off frequencies above 4 kHz" (p.743). The amount is a guess.
  * **The others are guesses** from their names. The NTSC and PAL Hum
    Busters "cut the bass on frequencies that often cause hum" (p.743),
    with a narrow cut at 60 or 50 Hz.
  Replace a guess in the table when better values turn up, with its source.
  `tst_audio_eq_ui` checks that each entry has a source and a note.
* **A template keeps values the tool's controls do not offer** (210 Hz,
  4 kHz, 3/4 octave): `AudioEq::preset` names it, and `midOctaves` holds a
  width other than 2 or 1/4. Its turnovers and centre may be anywhere from
  20 Hz to 20 kHz, and its width from 1/16 to 4 octaves.
* **The tool shows a template's values but does not change them**: "you
  cannot change the parameters of a predefined EQ template" (p.744). The
  value controls are dimmed with a note saying so; In still works. Set EQ -
  In/Out copies the template as it is. On a segment without an EQ, the
  tool's values go back to the defaults.
* **Remove Effect** (p.741), with no video effect at the position, removes
  the EQ there on the selected audio tracks.
* **The Timeline** shows an EQ icon on segments that have one (p.740). It is
  teal, with three bars and no text, and grey when the EQ is not In.

## EQ templates in bins (5.7c2; p.740, p.745)

* **The Effect icon** carries the values the tool shows
  (`AudioEqTool::iconTemplate`, `eqTemplateMime()`).
  * "To save EQ settings in a bin: Drag the effect icon in the Audio EQ tool
    to a bin." The bin makes an Effect item holding the EQ
    (`model::eqTemplateItem`, `BinItem::composition["eq"]`).
  * **Ours:** it is named "Audio EQ", or the predefined template's name.
    "Rename the template by clicking the text and typing a new name"
    (p.745).
* **Onto the Timeline.** "To copy the settings to another audio clip: Drag
  the effect icon in the Audio EQ tool to another audio clip in the
  Timeline" (p.740).
  * The EQ goes on that whole segment as one undo step
    (`Timeline::dropEq`).
  * An EQ template dragged from a bin onto an audio clip does the same
    (`Timeline::droppedEq`; "Working with Effect Templates").
  * Video tracks and filler refuse it.
* **Site_EQs_Bin.** "You can add your own EQ templates to the Audio EQ Tool
  Fast menu by storing your EQ templates in the same bin as the predefined
  templates" (p.745).
  * **Ours:**
    * It is the native bin `Site_Effects/Site_EQs_Bin.cthbin` in the site
      settings folder (`MainWindow::siteEqBinPath`), made empty if it is
      missing, so File > Open Bin finds it.
    * Our predefined templates come from `eq_templates.json`, not from
      that bin.
  * The Fast menu lists the bin's EQ templates after the predefined ones.
    It reads the open bin as it is, else the file.
  * Choosing one applies it like a predefined template, with its values in
    the Console. Unlike a predefined template, its values can be changed
    in the tool.

## Checks

* `tst_audio_eq` (media):
  * the limits and the filter shapes;
  * `setEq` on segments, saving and match frames;
  * the sound against the same filters run in double precision, whole, in
    chunks and after a seek. The residual is about -144 dBFS.
* `tst_audio_eq_ui`:
  * the controls, typing on a slider and the scales;
  * several tracks and the Track Selection menu;
  * the Fast menu, the templates table, applying a template, Remove Effect;
  * Tools > Audio EQ, and Bypass RT EQ in what plays;
  * EQ templates: the effect icon into a bin and onto a Timeline clip, a
    bin's template onto a clip, Site_EQs_Bin in the Fast menu.
* Edit scenario `18-eq-drop.json`: the effect icon dropped on clips, the
  refusals and Undo (step `eq_drop`).
* Edit scenarios: `tests/scenarios/08-audio-eq.json` (Set EQ over the
  marks, a splice, Remove Effect, Undo). It uses the `eq` and `eq_menu`
  steps and the `eq` field in `expect`.

## Later

* The predefined templates' real values, when a seat or other observation
  gives them.
* Audio Loop Play while adjusting (p.745), and Render Effect.
* EQ on source clips through Toggle Source/Record in Timeline (p.739).
* Bypass RT EQ in the Audio Mixer and the Audio Project setting's Output
  tab (p.737). It is kept in the tool and not saved yet.
* EQ in AAF, OTIO, FCP XML and `.avb` interchange.
