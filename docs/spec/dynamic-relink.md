# MultiRez and Dynamic Relink

Editing Guide ch.31, p.1095-1121, and the Dynamic Relink setting, p.1169-1171. ROADMAP 8.6. Code:
`src/media/dynamic_relink.*`, `src/ui/main_window_relink.cpp`, the "Dynamic Relink" Project setting in
`src/ui/resources/settings.json`, Transcode's Create new clips in `src/ui/consolidate_dialog.*` and
`src/media/consolidate.*`.

## Media at several resolutions (8.6a)

"MultiRez ... lets you create a master clip that is associated with multiple media files of different resolutions"
(p.1095). "MultiRez is managed by the MediaCentral Production Index (Media Indexer)" (p.1096); we have none, and "In a
MultiRez environment, relinking is source based" (p.1099).

* Ours: our media database (`media::MediaDatabase`, docs/spec/native-model.md) stands in for the Media Indexer. A clip's
  media at other resolutions are the managed media sets whose files name its MobID as their material package
  (`MediaDatabase::setsOf`), as the source ID does in Media Composer.
* The clip's "affinity media" (p.1101-1102) is the link it was made with. While dynamic relink has the clip on other
  media, the link keeps it under `"multiRez"` (`media::affinityLink`), so the clip can go back to it.
* Candidates (`media::mediaCandidates`): the affinity media while its files are there, and each set naming the MobID that
  holds the clip's tracks for its whole length, at its start timecode. Newest first. Sets that hold only part of the clip
  ("Partial Clips, Multiple Resolutions", p.1102) wait for 8.6c.
* What quality matching reads of each (`media::MediaCandidate`): raster, frame rate, resolution name, codec family,
  container, sample rate and bit depth, bytes a clip frame, and when it was made. The media database's index (version 4)
  now records a file's raster, codec, sample rate and bit depth (`MediaFileInfo`). Linked affinity media are probed.

### Transcode's Create new clips (p.354-355)

"In a MediaCentral Production Management environment, with Dynamic Relink enabled, select this option to create new
master clips for the transcoded media. If you do not select this option, the existing master clip is associated with
both the original media file and the transcoded media file."

* The Consolidate/Transcode dialog's Transcode options have Create new clips (`createNewClips`), on, dimmed unless the
  Dynamic Relink setting is enabled (`ConsolidateDialog::setDynamicRelink`).
* Off (`ConsolidateOptions::newClips`): a selected master clip's new media name its MobID and no ".new" clip is made
  (`ConsolidateResult::associated`); MainWindow reads the media database again and relinks the clip by the setting.
  Ours: sequences' clips still get new clips, as their new media cover only what the sequence uses (8.6c).

## The Dynamic Relink setting (p.1106-1110, p.1169-1171)

A Project setting ("Dynamic Relink settings are project settings", p.1107) with three tabs, ours: Dynamic Relink (the
dialog's top area: Enable Dynamic Relink, Allow Mixed Frame Rate Media, Override Working Settings with Target Settings),
Working Settings and Target Settings. Each of the two holds:

* If no match is found: Use highest quality, Use lower bandwidth, Keep Existing Media, Offline (p.1170). Keep Existing
  Media to start with (ours; p.1107 suggests Offline for the Target tab "because this setting provides a clearer
  warning").
* Video Relink Method: Most Recent, Highest Quality, Minimal bandwidth, Specific Resolution; Specific Resolution's Relink
  Parameters Raster, Frame Rate, Codec Family, Compression Quality and Container; When multiple matches are found:
  Prefer target raster, then prefer highest bandwidth, or Prefer lowest bandwidth (p.1170).
* Audio Relink Method: Most Recent, Highest Quality, Most compressed, Specific Quality, with Codec Family, Bit Depth,
  Sample Rate and Container, and Prefer highest or lowest bitrate (p.1170-1171). Ch.31 names the audio methods "Highest
  Sample Rate, Highest Bit Depth, or Specific Quality" (p.1120); we follow the settings reference.
* Ours: the parameters are menus whose first choice is Any (no filter). Their values: rasters from 720x480 to
  4096x2160, frame rates 23.976 to 60, codec families Avid DNx, Apple ProRes, H.264, H.265, Uncompressed and Other, our
  DNx resolutions for Compression Quality, containers MXF, QuickTime, MP4 and Other; audio PCM, AAC, Other, 16/24/32 bits,
  32 to 96 kHz, MXF, WAV, Other.

### Quality matching (p.1120-1121; `media::chooseMedia`)

* Media at the project's frame rate are taken first. "In case of no media with current project frame rate, Media Composer
  uses the most recent online compression out of all available for that object" (p.1169). With Allow Mixed Frame Rate
  Media all rates are taken, and Specific Resolution's Frame Rate is a preference ("the Frame Rate pull down menu changes
  from exact values to preferred values", p.1110).
* Most Recent: the newest media. Highest Quality: "the least compressed and most highly defined media ... with a format
  that most closely matches the project settings" (p.1120); ours: the project's raster first, then more pixels, then more
  bytes a frame (our files do not record bit depth or chroma). Minimal bandwidth: the fewest bytes a frame.
* Specific Resolution: the media passing every parameter set; of several, the project's raster then the most bytes a
  frame, or the fewest. Sound-only clips go by the audio rules.
* With no match, If no match is found decides; Offline puts the clip offline ("Relinking to offline does not delete
  existing media", p.1170): its link is the affinity link marked unlinked with `"multiRezOffline"`, so it relinks when
  the rules change. A clip the user unlinked (p.368) is left alone.
* The seven queries of p.1121 (tape and timespan, size, field order, compression family, compression, bit depth, colour
  space) are covered by the MobID and the parameters; ours does not compare field order or colour space.

### When it relinks (`MainWindow::dynamicRelinkItem`, `dynamicRelinkLoaded`)

* "Media Composer performs a dynamic relink whenever clips are loaded into the source monitor or the Timeline" (p.1107):
  the Composer's load filter (`Composer::setLoadFilter`) relinks a clip, or every clip a loaded sequence uses, and stores
  it in its bin, so every sequence using it plays the new media.
* "Selecting this option and clicking 'Apply' or 'OK' will trigger immediate relink for any media loaded into Source
  Monitor or the Timeline" (p.1170): a change to the setting relinks what the monitors have loaded. Override Working
  Settings with Target Settings relinks to the Target tab; turning it off returns to the Working tab (p.1108-1109).
* Ours: turning Dynamic Relink off puts the loaded clips back on their affinity media.
* Not ours: Send to Playback, MediaCentral check-in, OMF alpha media (p.1106), Frame Chase editing (p.1111) and the
  Production Management relink rules (p.1111).

## The MultiRez button (8.6b; p.1111-1116)

`src/ui/timeline_multirez.cpp`; MainWindow answers its questions in `src/ui/main_window_relink.cpp`.

* The button is in the Timeline's bottom toolbar ("Right-click the MultiRez button at the bottom of the Timeline",
  p.1107); ours: after the Video Quality Menu button. Its displays (p.1112): "Gray - inactive", "Bottom half of diamond
  black - Show Mismatches display", "Top half of diamond black - Show Target Availability display". A click goes to the
  next display (ours: inactive, mismatches, target availability, inactive); "Clicking the MultiRez button changes only
  the display — it does not perform a dynamic relink". The drawing is ours: a diamond on a rounded square.
* Its colour (`Timeline::multiRezButtonColor`): in the Show Mismatches display "If the background is blue, some media
  does not match or is not available in the current settings"; in the Show Target Availability display "If the
  background is yellow, some media is not available in the target settings"; gray otherwise.
* A clip's state (`Timeline::segmentResolution`, `MainWindow::clipResolution`, kept until the media or the setting
  change): Show Mismatches compares the media the clip plays with what the rules in use choose (Offline when it plays
  none); "Clip coloring for the Show Mismatches display is not used when you select Use Highest Quality or Minimal
  bandwith as the relink method" (p.1113), so those methods mark nothing. Show Target Availability asks whether the
  Target tab's rules find a match, "the same information whether the clips are linked to the working settings or the
  target settings" (p.1113).
* Resolution Tracking in the Clip Color dialog (p.595-596, p.1112-1114): "This option is only available if you are
  working in a MultiRez environment": ours, offered while Dynamic Relink is enabled (`ClipColorDialog::setMultiRez`). Its
  colours: blue for a mismatch, red (the Offline colour) for offline clips, yellow for clips not available in the target
  settings, with swatches for blue and yellow and Default Colors resetting them. "If you enable clip coloring for
  MultiRez and then click the MultiRez button to display mismatches or target availability, this setting overrides any
  other Timeline clip coloring" (p.1113): it comes before the other options (`model::timelineClipColor`).
* The menu (p.1114-1116, in the table's order; `Timeline::multiRezMenu`): Enable Clip Coloring (the Clip Color
  dialog's Resolution Tracking), Show Mismatched Render Ranges, Show Mismatches, Show Target Availability, Find Next
  Mismatched or Unavailable Clip, Update Media Status, Enable Dynamic Relink, Override Working Settings with Target
  Settings (dimmed with Dynamic Relink off, ours), Dynamic Relink Settings (opens the Project setting). The two Dynamic
  Relink items change the setting, which relinks as 8.6a does.
* Show Mismatched Render Ranges: "A blue line appears along the top of the effect to indicate the portion that is
  rendered using a different resolution ... select the correct resolution in the Render tab in the Media Creation
  dialog box and then re-render" (p.1115): ours, a render's media against the Media Creation setting's Render tab
  resolution (`MainWindow::renderAtOtherResolution`), the whole effect marked.
* Find Next Mismatched or Unavailable Clip, "You can also use Ctrl+/" (p.1115; command
  `timeline.find_next_mismatched`): the position to the start of the next marked clip after it on the selected tracks
  ("This option works only on enabled tracks"), in the Show Mismatches display mismatched renders too. Inactive, it
  says to choose a display first (ours).
* Update Media Status (p.1115): the media database read again; "In the Show Mismatches display, relinks the clips
  displayed in the Timeline to the media that matches the working or target settings"; in the Show Target Availability
  display it only refreshes.
* Clip Text > Clip Resolution (p.1104; the Fast menu's item): the resolution of the media the clip plays now, so "the
  resolution of the clip (as shown in the text on the clip) is different" in working and target settings (p.1103).

## Partial and shared media (8.6c; p.1100-1102, p.1109-1110, p.1116-1120)

* Associated media (`media::associatedMedia`): the sets naming the clip's MobID, and those of other clips that share
  its source and some of its timecode span: "You can also associate two or more master clips with the same media if the
  clips share a source ID and timecode" (p.1100). Ours: the source is the clip's tape, else its source file, as our media
  files record them; spans are compared by timecode (`media::mediaCoverage`: Full, Partial or None).
* Only media holding the whole clip are played (`mediaCandidates`). Ours: Media Composer plays the shared section of
  partly online media and shows "offline frames" beyond it (p.1118); we leave partly online media out of Dynamic
  Relink, and the bin shows them as partly online.
* The MultiRez bin headings (p.1116; `media::multiRezHeadings`, `BinAppearance::multiRezHeadings`/`multiRezCell`):
  "Video resolutions" and "Audio sample rate". Ours: our resolutions' names (Avid DNx LB ... 444) and 32, 44.1, 48 and
  96 kHz. "These headings appear at the bottom of the Bin Column Selection list" (Choose Columns). Cells: "Filled circle
  Online, Half-filled circle Partially online, Empty circle Offline or Not Available" (●, ◐, ○), for master clips and
  audio clips; MainWindow keeps them until the media change (`MainWindow::multiRezCell`).
* "Video format: This column displays the resolution of the media files to which the clips ... are currently linked"
  (p.1116): Dynamic Relink sets the Video column to the resolution it links to and puts the clip's own back with its
  affinity media. The Drive column's blank for several drives is left as it is (ours).
* Transcode without Create new clips on a sequence (p.354-355, p.1118 "Consolidating and Transcoding a Portion of a
  Clip"): each clip keeps the media of the range the sequence uses, with handles, naming its MobID; no ".new" clips and
  the sequence is left as it is. The clip is then partly online at that resolution.
* Delete (p.1117, `media::affinityMediaFiles`): "For clips in a bin, you can delete only the media that was originally
  associated with the clip": the media naming the clip's MobID (dual ingest, batch capture, "Transcoded clip, one
  clip"), not the media of clips that only share its source ("Transcoded clip, multiple clips", "Clips that share
  timecode and source": original resolution only). With media at several resolutions the Delete dialog has a box for
  each (`deleteResolution:<name>`, ours: all ticked); "Multiple audio sample rates are not listed" is ours too only in
  that sound-only media are listed by sample rate.
* Cross Rate Dynamic Relink (p.1109-1110): with Specific Resolution's Frame Rate, media at a whole multiple of the
  project's rate count as the project's (25 and 50, not 23.976 and 25); with Allow Mixed Frame Rate Media, "In the case
  of no media with exact frame rate match, Dynamic Relink will try to find the closest multiple frame rate value".
  "The clips placed in the Source Monitor will not be linked to a different rate media": clips loading into the Source
  monitor relink without it (`chooseMedia`'s `crossRate`).

## Left for later

* Playing partly online media with offline frames beyond them (p.1118-1120), Viewing a Source Clip in the Timeline's
  offline portions (p.1119); Frame Chase editing and archive restores (MediaCentral, p.1111, p.1120).

## Tests

* `tst_dynamic_relink settingsFromValues`, `frameRates`, `multiRezClip`: the setting's values, the frame rate pool, and a
  linked clip transcoded twice without Create new clips chosen by Most Recent, Minimal bandwidth, Specific Resolution,
  each If no match is found, Offline and back, the affinity link kept through the media database.
* `tst_render_ui dynamicRelink`: the setting applied to the loaded clip, Offline, Override with the Target tab, relinking
  as a clip loads, off again, and Create new clips following the setting.
* `tst_dynamic_relink crossRate`, `partialAndShared`, `sequenceWithoutNewClips`: cross-rate choosing, partly online
  and shared media and their bin cells, Delete's affinity files, the Video column, a sequence transcoded without new
  clips.
* `tst_render_ui multiRezButton`: the bin's MultiRez cells and Delete's files, and the displays and colours, Resolution Tracking colours, Find Next Mismatched or
  Unavailable Clip, Update Media Status, Clip Text > Clip Resolution, the menu's Dynamic Relink items and the Clip Color
  dialog's Resolution Tracking. `tst_clip_colors resolutionTracking`.
