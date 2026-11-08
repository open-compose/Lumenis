# Background services and Dynamic Media Folders (8.4)

Editing Guide p.371-377. 8.4a (done): the background services, the Background Queue following them and the Timeline's
progress indicator. 8.4b: Dynamic Media Folders and their profiles, watched for new files. 8.4c: the Dynamic Media
Folders window, the Profile Editor and Acquire.

## The background services (p.376-377, 8.4a)

"Media Composer installs a Background Transcode service, a Background Render service, and a Dynamic Media Folder
service. These services are off by default." Ours: the services are the Background Queue's state
(`BackgroundQueue::ServiceState`), in the application; there is no separate service process and no taskbar or menu bar
icon (the guide's "Avid Background Services Manager"). Jobs (background Consolidate/Transcode, renders, transcription,
later Dynamic Media Folders) start only while the services are Active; a job already running finishes.

* Tools > Background Services (`BackgroundServicesDialog`, `MainWindow::backgroundServices`): the state ("Active,
  Inactive or Paused"), Start ("Immediately starts the background services if they are currently stopped"), Stop, Pause
  ("for the specified amount of time"; ours: in minutes, 10 to start with), Always Start Avid Background Services at
  Launch, Always Stop Avid Background Services upon Exit and Enable Pausing of Background Service. Start, Stop and
  Pause act at once; OK keeps the three options in the user's Interface setting (keys outside its schema).
* Ours: Always Start ... at Launch is on to start with, so that the background work earlier steps queue runs as before;
  the guide's services are off by default.
* Pausing (p.377): explicit, or "automatically performed during critical UI operations such as playback" (ours:
  while the Composer plays); "If the pause time period ends and you are in the middle of a play operation, the pause
  time period is extended" (ours: checked again each second). Unchecking Enable Pausing and clicking OK clears the pause.
* Always Stop ... upon Exit: the services stop as the main window closes, so that no waiting job starts then (ours: they
  end with the application anyway).

## Dynamic Media Folders and profiles (p.371-375, 8.4b)

"Dynamic Media Folders (DMFs) are user-created folders that allow you to work more efficiently with file-based media"
(p.371). `media::DynamicMediaFolders` (src/media/dynamic_media_folders.*) holds the folders (path, profile, Enable)
and the profiles; MainWindow keeps them in `Dynamic_Media_Folders.json` beside the user's settings (ours, as
Title_Templates are; the guide's service keeps them outside any project) through `main_window_dmf.cpp`.

* A profile's Link Settings (p.373-374), `DmfLinkSettings`:
  * Link Using: kept; ours links through FFmpeg whatever is chosen.
  * Bins: Use active bin, or Create a new bin ("This is the default option") with the default naming convention
    ("<project> <n>", the first number free), the volume's name, or a bin name (`dmfBinName`; the bins are used when
    the assets are acquired, 8.4c).
  * Multichannel Audio: the clips' Track Formats, as the Track Formats column writes them ("Stereo A1A2"); the guide
    edits them in the Multiple Mixes dialog (8.4c).
  * AIS: Audio Start-Time Option (a Broadcast WAV starts at the frame its time stamp falls in, `alignToFrame`); Reel
    name for Labroll from the file's name (a numbered still's without its number; digits only give none) or a folder's,
    one to eight up; Camroll from a DPX file's Input Device; the DPX column as the Labroll prefix and a 7-digit count
    from 0, from 1, the Start timecode as frames, or the file name's number; Start from the file or the file name (ours:
    four pairs of digits separated by `: _ . ; -`, or eight digits); Default FPS for numbered stills.
* Actions (p.374-375), `DmfAction`, run in the list's order:
  * Copy to Folder: the entry is copied under the folder with its own name; Auto Relink when complete points the clips
    at the copies. Checkin to Production Management is kept and does nothing (no MediaCentral).
  * Consolidate and Transcode through `media::consolidate` (2.4a): the clips keep the linked media and ".new" clips
    take the new (p.376); Skip media files already on the target drive, Convert Audio Sample Rate and Bit Depth, the
    Video and Audio Drives and Transcode's Video Resolution. Convert Audio Format is kept as a name (our managed audio
    is MXF PCM); Relink selected clips ... before skipping, Apply Reformatting and Apply color transformations are kept
    (our clips carry no framing or colour transformations).
* Watching (ours): every 2 seconds each enabled folder with a profile is looked in; each new entry (a file, or a folder
  whose files are linked together, so that "each group of consecutive dpx files in that folder" is a master clip,
  p.376) is queued once it looks the same on two looks (the files' count, size and newest change), because a copy by
  "a third-party application" (p.371) gives no sign that it has ended. Hidden entries are left out. Each entry is a
  Background Queue job "Dynamic Media Folder: <entry>" (`media::processDmfEntry`); it is then done, kept in the
  folder's `done` list, whether it worked or not (a failure is in the error report). What it made waits as an asset,
  and the indicator shows the green dot.

## The Dynamic Media Folders window and the Profile Editor (p.372-376, 8.4c)

* Tools > Dynamic Media Folders (`DynamicMediaFoldersWindow`, the "DynamicMediaFolders" panel; also the indicator's
  right-click Dynamic Media Folders): a row per folder with Enable, the folder, its profile menu ("choosing the profile
  from the drop down list", p.375) and the Acquire column. "+" opens the Select Folder window (p.372); ours: "-"
  removes the selected folder. Profile Editor edits the selected folder's profile, or a new one.
* The Profile Editor (`DmfProfileEditor`): the menu bars Profile Summary, Link Settings and Actions open and close
  their sections (p.372; ours: all open to start with, the summary in words); Actions + adds a Copy to Folder action;
  a row's Menu button chooses Copy to Folder, Consolidate or Transcode; rows are reordered by dragging (p.375); Set
  chooses Copy to's folder; Save asks for the profile's name and gives the profile to the folder (p.375). Ours: the
  Multichannel Audio's Track Formats are typed ("Stereo A1A2") where the guide's Edit opens the Multiple Mixes dialog;
  Checkin to Production Management is dimmed.
* Acquire (p.376; `MainWindow::acquireDmfAssets`): green with the count when assets are ready, dimmed while they are
  still being processed. "The new assets will populate the Media Composer Bin according to the Bin setting": the
  active bin, or a new bin named by the profile (also when no bin is active, ours). Consolidated and transcoded clips go
  in as ".new" clips, numbered again when the bin has the name. An entry still being processed goes in as an offline
  master clip named after it (ours); "When they are ready, the Acquire icon will redisplay", and acquiring again
  replaces the offline clip with the entry's clips. Acquired assets leave the green dot. An entry that made nothing (an
  error) has nothing to acquire; its error is in the error report.

## The progress indicator (p.375-376, 8.4a)

`BackgroundIndicator` in the Timeline's toolbar (ours: before the Quick Find field), drawn by us after the guide's table:
spinning while background processing is in progress; a solid green dot when Dynamic Media Folder assets wait to be
acquired (8.4c); grey when the services are stopped; yellow when paused; not spinning and no dot when idle; a yellow
triangle after an error. Its right-click menu: Dynamic Media Folders (8.4c) and Generate Error Report in Console, which
writes each failed job since the last report to the Console and clears the triangle ("The yellow triangle will remain
until you generate an error report").

## Tests

* `tst_dmf`: the folders and profiles through JSON, new entries, bin names, timecode in file names, a DPX folder as one
  clip with its AIS columns, Copy to Folder with and without Auto Relink, Multichannel, Transcode and cancelling.
* `tst_background_services`: jobs waiting while stopped and paused, a timed pause ending, the pause extended during play,
  Enable Pausing off, the error report; the indicator's states; Tools > Background Services in the application with its
  options kept and applied for the next user's settings; an enabled folder's file linked and copied in the
  application, kept beside the user's settings, the green dot and a failing entry's error; the window's +, Enable,
  profile and -, the Profile Editor's sections, actions and Save, Acquire into a new named bin and the active bin,
  offline while processing and replaced when ready.
