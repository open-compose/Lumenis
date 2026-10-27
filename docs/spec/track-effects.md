# Audio Track Effects (Phase 5.5a)

Audio Track Effects are effects inserted on audio tracks, up to five per
track (inserts a to e). They are processed in real time as the sequence
plays (Editing Guide p.790-795). AudioSuite (offline processing of clips,
p.796-805; docs/spec/audiosuite.md) and the CLAP plug-in host use the same
effects.

## The effects (`src/media/audio_fx.*`)

* **`media::AudioEffect`** is the interface every audio effect has,
  whoever made it:
  * its info: id, name, category, vendor, and its parameters (id, name,
    unit, range, default; or a list of choices);
  * `set`, `reset`, `process` (one mono channel at 48 kHz, in place);
  * `tail`: how long it goes on sounding after its input stops.
* **`audioEffects()`** lists the effects: the built-in ones, then those
  registered from elsewhere (`registerAudioEffects`), such as the CLAP
  host's plug-ins (docs/spec/clap-host.md). `makeAudioEffect(id)` makes one; an id nothing provides gives
  none.
* **The built-in effects are ours.** The guide describes Avid's core
  plug-ins by what they do (p.805-860). Ours do the same kinds of thing,
  under plain names, with textbook DSP:

  | Category | Effect | Parameters | Compare |
  |---|---|---|---|
  | Dynamics | Compressor/Limiter | threshold, ratio, attack, release, knee, gain | p.833 |
  | Dynamics | Expander/Gate | threshold, ratio, range, attack, release | p.842 |
  | Dynamics | De-Esser | frequency, threshold, range | p.837 |
  | EQ | 4-Band EQ | low shelf, two peaks, high shelf | p.841 |
  | Delay | Delay | time, feedback, mix | p.809, p.819 |
  | Modulation | Chorus | rate, depth, pre delay, feedback, mix | p.808 |
  | Reverb | Reverb | room size, damping, pre delay, mix | p.821, p.835 |
  | Harmonic | Lo-Fi | bits, sample rate, mix | p.845 |
  | Other | Trim | gain (-∞ to +12 dB), polarity | p.845, p.860 |

  * The EQ and the de-esser's sidechain use the Audio EQ Cookbook
    sections (`model::lowShelf`, `peaking`, `highPass` and the rest).
  * The dynamics are feed-forward, on peak levels in dB.
  * The reverb is Freeverb's design (Jezar at Dreampoint, public domain).

## The model (`model::Track::inserts`)

* An audio track has five slots, a to e. Each is empty or holds a
  `model::TrackInsert`:
  * the effect's id;
  * its name when it was inserted, so an effect that is not installed
    can still be named: "Unavailable Effect", with the effect's name and
    id (p.790);
  * its parameter values;
  * whether it is bypassed.
* They are saved with the track (`Sequence::toJson`, `inserts`). The
  Clipboard does not carry them.
* **Mono only.** "You can only apply mono plug-ins to mono audio tracks
  and stereo plug-ins to stereo tracks" (p.790). Our tracks and effects
  are mono. Moves and copies go only between tracks of the same format
  (p.794).
* **`Timeline` functions:**
  * `setTrackInsert` puts an insert in, replaces one or removes one;
  * `moveTrackInsert` moves or copies one (p.795);
  * `dropTrackEffect` drops an effect with the Select Insert dialog
    (p.792).
  Each is an undo step. Video tracks and locked tracks are refused.

## The sound (`media::AudioMix`)

* A track with active inserts (not bypassed) is mixed apart from the
  others:
  * its pieces are summed in mono with their gains (clip gain and volume
    automation);
  * the sum goes through the effects in order, a to e: "Media Composer
    processes Audio Track effects in order from left to right" (p.794);
  * then it is panned as its pieces are. A piece's stereo gains (l, r)
    are split into a gain √(l² + r²) and a pan (l, r)/gain. Where no
    piece plays (a reverb's tail in filler), the last piece's pan holds.
* Tracks without inserts mix exactly as before.
* After a seek, each track's effects are reset and first run over the
  tail's length (at most 4 s) before the play position. So play from the
  middle sounds the same as play that ran there.
* Effects that are not installed are skipped: the track plays without them.
* **Audio Mixdown** (5.1) renders the inserts of the tracks it mixes.
* **Bypass:** the tool's Bypass button bypasses the shown insert, saved
  with it. **Ctrl+Bypass** bypasses every track's effects in what plays
  (p.794). That is not saved (ours, like Bypass RT EQ).

## The UI

* **Tools > Audio Track Effect** (p.791, p.793) opens a panel
  ("AudioTrackEffect", `src/ui/audio_track_effect.*`). The buttons are in
  the picture on p.793:
  * **Select Track** and **Select Insert** choose the insert. Select
    Insert lists a to e with their effects, then "No Insert", which
    removes the shown one (p.795).
  * **Select Effect** has menus by category.
  * **Plug-In Settings:** Copy Settings and Paste Settings, onto the same
    effect only (p.794), and Reset to Defaults (ours).
  * **Bypass** (blue when bypassed, p.794).
  * Each parameter has a row: its name, a slider (log-scaled for
    frequencies and times) and its value.
  * The arrow keys work as on p.794: Up and Down change the track, Left
    and Right the insert.
  * **While a slider is dragged**, its value plays at once (p.793). The
    tool holds it as a preview, and MainWindow starts play again where it
    is to hear it. Letting go makes the undo step.
* **Right-click an audio Record Track button** (p.791, p.793):
  * Audio Track Effects [n] > Insert [a-e] > category > effect, with No
    Insert;
  * Audio Track Effect Tool, which opens the tool on that track.
* **The Effect Palette's Audio Track tab** (p.791-792) lists the effects
  by category:
  * Dragging one onto an audio track opens Select Insert, with a button
    per insert ("a: [Empty]", "b: Reverb") (picture on p.792).
  * Double-clicking one puts it in the first empty insert of each selected
    audio track (ours).

## Ours (on the watch list)

* The built-in effects, their names, parameters and DSP.
* Ctrl+Bypass is not saved.
* Locked tracks refuse insert changes.
* A double-click in the palette inserts the effect.
* A track's effect tail is panned as its last piece was.

## Checks

* `tst_audio_fx`:
  * the list, ranges and silence in, silence out;
  * a registered effect;
  * each effect against known signals (gain, compression ratio, gating,
    de-essing, the EQ's shelf, echoes, reverb decay, bit depth);
  * inserts in the mix: level, bypass, unavailable effects, a delay's tail
    in filler, the same in chunks and after a seek, and Audio Mixdown.
* `tst_track_effects`:
  * the Timeline's set, move, copy and remove with undo, and the refusals;
  * the track button's menu;
  * the tool: preview and commit, Bypass and Ctrl+Bypass, Copy and Paste
    Settings, the arrow keys, No Insert, Unavailable Effect;
  * the Effect Palette tab, drag data, Select Insert and double-click;
  * MainWindow's Tools item and what plays.
* Edit scenario: `tests/scenarios/10-track-effects.json` (`insert` and
  `move_insert` steps, `expect_inserts`).

## Later

* The Timeline's Track Control panel with its insert buttons (p.792, p.794-795)
  came with U1b1 (docs/spec/track-control.md); dragging an effect from the
  Effect Palette onto an insert button is still to come.
* The Audio Mixer's insert menus (p.790).
* Audio Track Effect templates in bins (p.795) and Save Effect (p.793).
* Compare, and the Plug-In Settings files: Save, Save As, Import, Delete,
  Lock (.tfx).
* Stereo and surround tracks and their effects.
* AudioSuite is docs/spec/audiosuite.md; the CLAP host is docs/spec/clap-host.md.
* Inserts in AAF, OTIO, FCP XML and `.avb` interchange.
