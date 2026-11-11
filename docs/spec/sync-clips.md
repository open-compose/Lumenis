# Sync Clips (the guide's AutoSync)

ROADMAP 9.3. Editing Guide p.496-498, "Autosyncing Clips". The guide's Clip > AutoSync; Avid writes the name
"autosync™" (p.497), so ours is **Sync Clips** (ROADMAP 9.8, the trademark check). The dialog keeps the guide's
title, Sync Selection.

## What it does

"Autosyncing applies to audio and video clips that you capture separately, usually from two separate sources.
Autosyncing creates a new subclip that displays sync breaks in the Timeline as though the audio and video were captured
simultaneously" (p.496).

Clip > Sync Clips (command `clip.autosync`, `MainWindow::syncClips`) takes the bin's selected clips ("Highlight two or
more clips in the bin", p.497), opens the Sync Selection dialog (`src/ui/sync_clips_dialog.*`) and makes the synced
subclips with `model::syncClips` (`src/model/sync_clips.*`). They are added to the bin and selected. A selection that
cannot be synced says why in a message (and the Console).

## The dialog (p.497-498)

| Option | Ours |
|---|---|
| Film TC/Sound TC | the video clip's Film TC with the audio clips' Sound TC; dimmed outside 24p and 25p projects |
| Inpoints, Outpoints | the Source monitor's marks on a clip loaded there, else the bin's Mark IN / Mark OUT (as Group Clips) |
| Source Timecode | the Start columns; "The default option" |
| Auxiliary TC1-TC5 | the Auxiliary TC column chosen from the menu |
| Waveform Analysis | each clip's sound lined up with the first video clip's (`media::waveformOffset`, as Group Clips) |
| Keep audio on clip with video | the video clip's audio tracks Start to End; off: none of them |
| Include audio from audio-only clips | only the audio clips' tracks Start to End; off: all of them (ours: the guide does not say) |
| Collapse Audio Tracks | tracks without sound removed, the others moved up |
| When video tracks overlap: Create Group Clip instead of Subclip | dimmed, not built yet |

The dialog opens as it was last left (ours).

## The synced subclip

* "You can autosync audio clips with video clips only": the video clips are those with picture, the audio clips the
  others with sound. Without both, a message.
* With timecode or the waveform, "the later starting timecode becomes the start of the new subclip" and "the longer
  clip is truncated to the length of the shorter clip" (p.497): the subclip covers where the video clip and its audio
  clips overlap.
* With In or Out points, "the whole clip is taken into the subclip": all of the video clip, with filler where the
  audio clips have no sound. "Use this option if you sync two clips at a time": one video clip only.
* Several video clips (by timecode, "to Autosync multiple clips, use the Start timecode"): a subclip for each, with
  the audio clips that overlap it.
* "named by default after the video clip with the file name extension .sync.n" (p.498): "<video>.sync.1", ".sync.2" ...
* Its Start and End are the video clip's timecode from the subclip's first frame; its other columns the video
  clip's.
* Audio tracks: the video clip's kept tracks first, at their numbers within Start-End, then each audio clip's after
  them; trailing silent tracks are not kept.

Ours: a Subclip item whose composition holds a sequence (key "syncedSubclip") of the clips' segments, as a
multigroup holds its sequence. `model::composedSequence` gives it, with sequences' and Title Effect clips' own:
`sourceTracksOf`, `applyEdit` (an edit puts the clips' own segments, picture and sound in sync), the Source monitor's
picture, `media::audioPieces` and Match Frame (`MainWindow::frameAt`) use it. Its sources are the clips it syncs, the
video clip first.

## Not yet

* The group clip for overlapping video ("When video tracks overlap: Create Group Clip instead of Subclip").
* Perf slipping in 35mm projects (p.498).

## Tests

`tst_sync_clips` (the model: methods, range, name, timecode, track options, errors, edits). `tst_sync_clips_ui` (the
dialog, the command on linked picture and sound, the Source monitor).
