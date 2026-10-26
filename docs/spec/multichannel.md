# Multichannel audio tracks (Phase 5.6a)

Stereo, 5.1 and 7.1 tracks carry several channels in one track (Editing
Guide p.624, p.670). 5.6a makes them work in clips and sequences. 5.6b
adds surround mixing: the Sequence Mix Format and Monitor Mix Format
buttons, panning, and 5.1/7.1 mixdown (p.685-689).

## Clips (`src/model/multichannel.*`)

* **Channels and formats.** A clip's audio channels are its Tracks
  column's audio tracks, A1 to An. Its **Track Formats** column groups
  consecutive channels into multichannel tracks. The guide's form is
  "Stereo A1A2" (p.671); several groups are separated by "; " (ours), as in
  "Stereo A1A2; 5.1 A3A4A5A6A7A8". Mono channels are not listed.
* **Set Multichannel Audio** (Clip > Modify > Modify Clip, p.279-281,
  p.671-672; `ui::ModifyDialog`):
  * The Modify Options menu lists the twelve options of the table on
    p.280-281. Set Multichannel Audio is the one made so far; the others
    are dimmed.
  * The dialog shows the selected clips' tracks, A1 to An, with a Format
    button beneath each pair. A click takes the next format that fits:
    Mono tracks, Stereo tracks, 5.1, 7.1. A 5.1 group takes three pairs and
    a 7.1 group four; their other buttons are dimmed. The button's menu
    picks one directly.
  * With several clips that differ on a pair, its button starts as Mixed
    tracks, which leaves that pair as each clip has it (p.672).
  * Stereo groups start on odd tracks, under the pairs (ours, from the
    buttons' placement).
  * "You can modify audio formats for master clips only" (p.670): master
    clips and audio clips. Sequences, subclips and group clips are left
    out; their names, and clips the formats do not fit, go to the Console
    and a message.
* **Source tracks.** A clip offers its groups and its ungrouped channels as
  source tracks, numbered in order. With "Stereo A1A2" on four channels,
  A1 is the stereo pair, A2 the third channel and A3 the fourth. The guide
  does not say how the source side numbers them; this is ours.

## Sequences

* **Segments.** A segment on a multichannel track names its first channel
  (`Segment::sourceTrack`, "A1"); the track's format says how many follow.
  Changing a clip's formats later leaves its segments as they are ("This
  does not cause a problem with editing, playback, or any other
  operation", p.671).
* **Patching and editing.** "You can only patch multichannel audio tracks
  to multichannel audio tracks, or mono tracks to mono tracks" (p.629).
  * Timeline::patchTracks refuses a patch to an existing record track of
    another format.
  * An edit whose pairs do not match is refused with the reason
    (`model::editFormatMismatch`).
  * A record track the edit adds takes the source track's format.
  * Segments move only between tracks of the same format ("you can move
    stereo audio tracks only to stereo tracks", p.610-611;
    `model::moveSegments`). A track a move adds takes the source track's
    format.
  * The Track Selector does not dim incompatible buttons yet (p.629).
* **Playback** (`media::audioPieces`). Each channel of a segment on a
  multichannel track is a piece of its own.
  * Channel order is Pro Tools order: L C R Ls Rs Lfe for 5.1, L C R Lss
    Rss Lsr Rsr Lfe for 7.1. The guide gives it as the default for surround
    (p.685).
  * On stereo speakers the channels go where the guide's table puts them
    (p.687-688): a stereo track's channels left and right (p.627); a
    surround track's L and R to their sides, C and LFE in the centre, the
    surround channels to their sides 3 dB down.
  * The segment's pan moves every channel toward one side (ours). Its
    volume automation applies to every channel; its pan automation is not
    used on multichannel tracks.
  * Audio Track Effects run on each channel through a copy of their own
    (`media::channelTrackKey`), so a stereo track stays stereo through its
    inserts.
  * AudioSuite effects are not set on multichannel tracks yet.
* **Split Track to Mono** (p.734-736; `model::splitToMono`,
  `Timeline::splitTracksToMono`):
  * From the track button's menu.
  * Split All Tracks to Mono from the Timeline's right-click menu (p.637),
    Timeline > Split All Tracks, Clip > Split All Tracks and Clip > Audio >
    Split All Tracks.
  * The track keeps its first channel and becomes mono. A mono track for
    each other channel goes below it, and the tracks after it are
    renumbered.
  * As the guide lists (p.735):
    * the track's Audio Track Effects are removed;
    * AudioSuite effects stay but lose their renders;
    * volume automation stays;
    * tracks are panned odd left, even right. Their pan automation is
      dropped (ours: the guide says pan automation is applied to the new
      tracks, but not how).
  * A copy of the sequence as it was goes into its bin as
    "<name>.Copy.<n>" (p.735).
  * The guide's limits (24 tracks, and keeping left and right on odd and
    even numbers) are ours to come: we stop at 99 tracks and do not move
    tracks to keep left channels on odd numbers.

## Later

* Track Formats when linking and importing (Set Multichannel Audio in the
  Link and Import settings, p.214, p.330).
* Split Track to Mono on clips in bins.
* Multichannel tracks in AAF, OTIO and `.avb` exports; they are written as
  their first channel.
* Waveforms of every channel (p.677) and the Audio Mixer's channel meters
  (p.687).

## Tests

* `tst_multichannel` (model): formats, source tracks, editing onto tracks
  of their format, Split Track to Mono and Split All.
* `tst_multichannel_audio` (media): stereo and surround channels on stereo
  speakers, and inserts per channel.
* `tst_multichannel_ui`: the Modify dialog, Modify Clip on a bin's
  selection, and the copy Split All leaves.
* Scenario `12-multichannel.json`.
