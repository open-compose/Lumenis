# Proxy media (8.7a)

Editing Guide ch.16, "Working with Proxy Media" (p.508-516). 8.7a1 is the proxies and their playback; 8.7a2 Copy Media
(p.512-513) and the hook for Avid NEXIS | EDGE (ROADMAP).

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

Tests: `tst_proxy` (the data, the modes, the columns, the files), `tst_render_ui proxyMedia` (Create Proxies,
the columns, the Play button's menu and orange, the monitor, Background Process).
