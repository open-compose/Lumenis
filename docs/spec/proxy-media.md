# Proxy media (8.7a)

Editing Guide ch.16, "Working with Proxy Media" (p.508-516). 8.7a1 is the proxies and their playback; 8.7a2 Copy Media
(p.512-513) and the hook for shared workspaces and Distributed Processing (p.510-511, p.514-516).

## Making proxies (p.508-510; 8.7a1)

* "To create proxy media in Media Composer: 1. Select the master clips, subclips, groups or sequences in your bin. 2. Open
  the Create Proxies window by doing one of the following: Right-click on the asset(s) and select Create Proxies. Select
  Create Proxies from the Bin Fast menu" (p.508): `bin.create_proxies` on the bin's right-click menu (`createProxies`)
  and in the Bin menu, which is the Bin Fast menu. Ours: subclips, groups and sequences give the master clips they use,
  found in the open bins (`MainWindow::proxyClips`).
* The Create Proxies window (`src/ui/proxy_dialogs.*`): "Select either a local drive or choose an Avid NEXIS workspace";
  "Processing Options for creating proxies include 'Application' or 'Background Process'" (p.508). Ours: the media drives
  stand for both; Background Process queues one Background Queue job per clip, and the clip takes its proxies when the
  job is done (`MainWindow::makeProxies`).
* "Proxies are multiformat and utilize either Avid DNx LB or H.264, depending on quality setting in the Timeline" (p.508);
  "Proxy Audio shows AAC if there is audio associated with the proxy" (p.510): `media::makeProxyMedia`
  (`src/media/proxy_media.*`) writes an H.264 MP4 and an Avid DNx LB MOV, each with AAC sound, through the movie export.
  Ours: at half the project's raster, at least 256 by 144 (Avid DNx LB takes nothing smaller than 256 by 120); the sound is the clip's first two channels when its media is at 48 kHz; the files
  go in `Avid MediaFiles/Proxy/1` on the drive ("Proxy media is placed in the Proxy folder based on the next available
  number", p.513, said of Copy Media). They need the patent codecs (docs/EXTENSIONS.md).
* What a clip keeps (`model::ProxyMedia` in `src/model/proxy.*`): both files' links, their folder, whether they have
  sound and their raster, in its Source Settings under "proxy".

## In the bin (p.509-510; 8.7a1)

* "The proxy media appears in the bin with an orange clip icon" (p.508); "If proxies have been deleted, icons will no
  longer be shown as orange" (p.509): the icon column draws clips with proxies orange.
* "Four additional columns have been added to display information about proxies, including Proxy Offline status, Proxy
  Video format, Proxy Audio format, and Proxy Path" (p.509; `model::proxyHeadings`, at the bottom of Choose Columns after
  the MultiRez headings, ours): Proxy Offline "shows any video or audio media that is offline for a clip" (ours: "V", or
  "V A" when the proxy has sound, while the proxy for the current quality is missing); Proxy Video "will update depending
  on the options selected in the Timeline Video Quality menu ... Full Quality ... H.264 format. If Draft Quality or Best
  Performance is selected, the Avid DNx LB format will be used"; Proxy Audio "AAC"; Proxy Path the folder (p.510;
  `model::proxyCellText` through `MainWindow::proxyCell`).

## Playing proxies (p.509, p.511-512; 8.7a1)

* "Right-click the Play button and select Proxy Preferred": the Play button's right-click menu (`Composer::
  proxyPlaybackMenu`) with High-Resolution Only, Proxy Preferred and High-Resolution Preferred (`Composer::
  setProxyPlayback`). "The play button changes to orange indicating you are in Proxy mode" (ours: for both preferred
  modes; the mode is kept for the session).
* "When 'Proxy Preferred' is selected, proxy is the preferred playback format. If the proxy is offline or not available
  but the high-resolution, local media is, then the high-resolution media will be played. When 'High-Resolution
  Preferred' is selected, high-resolution, local media is the preferred playback format. However, if the high-resolution
  media is offline or not available but the proxy is, then the proxy will be played" (p.509): `model::playedProxyLink`,
  applied to every frame the monitors and the Timeline's sequences play (`MainWindow::playedClip` in `monoFrame`), at
  the Timeline's video quality (H.264 at Full Quality, DNx LB at Draft Quality and Best Performance; ours: the other file
  when that one is gone). Ours: the sound plays from the clip's own media.
* Sequences play their clips' proxies the same way (p.512).

## Shared workspaces and Distributed Processing (p.508, p.510-511, p.514-516; 8.7a2)

* "Select a local drive or choose an Avid NEXIS workspace to host assets and temporary files for the job. (Optional) Select
  the Distributed Processing option ... The Distributed Processing option is only available after you select an Avid
  NEXIS workspace" (p.510). Avid NEXIS, its client, Avid NEXIS | EDGE and Distributed Processing are Avid's servers and
  protocols, not published (ROADMAP's "NEXIS / MediaCentral" row), so ours is a hook: `ext::SharedStorageProvider`
  (`src/ext/include/lumenis/ext/shared_storage.h`, category SharedStorage). A storage module lists its mounted workspaces
  (`workspaces()`), which follow the media drives in the Create Proxies and Copy Media drive menus
  (`MainWindow::mediaTargets`, `ui::MediaTarget`); proxies written to a workspace go under its root as on a drive.
* The Distributed Processing box (`proxyDistributed`) is on only for a workspace whose storage takes jobs; with it,
  Processing Options is dimmed (ours: the jobs run on the workers). `MainWindow::submitDistributedProxies` sends one job per
  master clip (`ext::ProxyJob`: the clip's JSON and the proxy raster); `pollDistributedJobs`, every two seconds while jobs
  are out, gives a clip its proxies (`ext::ProxyJobStatus::proxyJson`, a `model::ProxyMedia`) when its job is done and says
  why in the Console when it failed. "You can monitor the job progress through the Distributed Process Job Status app"
  (p.510) is the vendor's.
* The browser's Create Proxies and the NEXIS | EDGE Process app (p.510-511), and the remote workflow's prerequisites
  (p.514-516), are outside the application.

## Copy Media (p.512-513; 8.7a2)

* "Select the items in the bin you want to copy or select the desired bin(s) in the Bin Container Sidebar ... Right-click and
  select Copy Media" (p.512): `copyMedia` on the bin's right-click menu (`bin.copy_media`, commands.json; the guide names
  no menu bar place, so it is in no menu) and `sidebarCopyMedia` on the Sidebar's right-click menu for bins and folders
  (`BinContainer::copyMediaRequested`). "Selecting a folder is the same as selecting all items in the folder"; "If you
  select bins that are closed, they will be opened at the time the copy function begins" (`MainWindow::copyMediaRequest`).
  Ours: items selected in a bin copy that bin under Bins; bins with changes are saved first.
* What is copied: the master clips the items give, as Create Proxies finds them (`MainWindow::proxyClips`, with a
  sequence's subclips, groups and nests followed: "all of the sources for Group clips are copied", p.513); "If you select a
  sequence to copy, the Copy Media workflow copies the entire media file of the original master clip" (p.512). Managed
  media only: "Copy Media copies managed media. It will not copy Linked media" (p.512). Proxies are both proxy files;
  renders are the precompute clips of the selected sequences' effects and transitions (and those kept from other proxy
  modes). The Copy Media license note ("Media Composer | Ultimate", p.512) does not apply.
* The window (`CopyMediaDialog` in `src/ui/proxy_dialogs.*`, laid out as the pictures on p.513): Bins; Managed media,
  Proxy media, Renders; "Calculating Size..." then "Media size required: N MB" and "Bins size required: N MB" (and "All
  selected files already exist at the target location"); Copy Media to: Media Drive (with Drive:) or Folder (with Set);
  "Available Space: N GB"; Copy Bins to: with Set, "Use same as above" for a folder, and its own Available Space. "As soon
  as the Copy Media window opens, size calculation begins based on the enabled checkboxes. Enabling or disabling checkboxes
  changes the calculation and updates the total ... If there is not enough space, copying will not be allowed" (p.513): OK
  is dimmed then, and while nothing is left to copy. Ours: Copy Bins to starts at the first drive; offline files are
  counted and named in the Console; a line says when there is not enough space.
* Where the files go (`media::planCopyMedia`, `src/media/copy_media.*`): on a drive, managed media and renders in
  "Avid MediaFiles/MXF/<next number>" and proxies in "Avid MediaFiles/Proxy/<next number>" ("Managed media is placed into
  a new folder within MXF and the folder is named using the next available number ... If no Avid MediaFiles folder or MXF
  folder exists at the selected location, an Avid MediaFiles folder is created and the media is placed inside the MXF '1'
  folder. Proxy media is placed in the Proxy folder based on the next available number", p.513); in a folder, "the media
  appears in an 'Avid Copied Media' folder at the target location" (ours: proxies too); bins in "Avid Copied Bins" under
  Copy Bins to. Ours: a file of the same name and size already there (in the drive's MXF and Proxy folders, or the folder's
  Avid Copied Media) is not copied again; two files of one name going to one folder get " 2" on the second.
* `MainWindow::runCopyMedia` copies what the window chose and reports in the Console.

Tests: `tst_proxy` (the data, the modes, the columns, the files), `tst_render_ui proxyMedia` (Create Proxies,
the columns, the Play button's menu and orange, the monitor, Background Process), `tst_copy_media` (the plan on a drive and
in a folder, the next numbers, files already there, sizes), `tst_render_ui copyMedia` (the menus, the files a selection
gives, the window, the copies), `tst_render_ui distributedProcessing` (a storage module's workspace and jobs).
