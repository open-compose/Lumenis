# IMF packages (Phase 11.5c)

"The Interoperable Master Format (IMF) is an international standard for the file-based interchange of multi-version,
finished audio-visual works. You can link to and create IMF originals or supplementals using the IMF Window" (Editing
Guide p.333). Sources: the guide's "IMF Original and Supplemental Support" (p.333-340), and SMPTE's published structure
of the package's documents: the asset map (ST 429-9), the packing list (ST 2067-2) and the composition playlist (ST
2067-3), whose XML schemas SMPTE publishes. No Avid material beyond the guide is used.

## The package (`src/interop/imf.*`)

* A package is a root folder holding `ASSETMAP.xml`, which maps asset ids (`urn:uuid:...`) to files, one or more
  packing lists (`PKL_*.xml`: an IMP, each asset's id, type, size and hash) and composition playlists (`CPL_*.xml`:
  segments of virtual tracks, each a list of resources naming track files by id with an entry point and a duration).
  `interop::imf::readPackage` reads the asset map, every packing list it marks as one and every composition those list.
* Element names are matched without their namespace, so the 2013, 2016 and 2020 editions read alike (ours).
* A composition's resources count in their own edit rate: picture in the composition's frames, sound in samples (48000
  1). `Resource::length` is the source duration times the repeat count.
* **Supplementals:** a package whose compositions name track files its own asset map lacks is a Supplemental; they are
  its Original's (`Package::isSupplemental`, `missingTrackFiles`, `addAssets`).

## Into a bin (`interop::imf::compositionSequence`, `MainWindow::importImfComposition`)

"Drag the CPL from the sidebar into a Bin to create a Media Composer sequence. You can modify this sequence without
affecting the original CPL" (p.336). The CPL travels as its file (a URL), so a bin takes it through its file drop, and
File > Import takes one too. Ours:

* Each track file the composition uses is linked once (as any file is, `media::linkFiles`) into the bin, with an "IMF
  Track File" column holding its id. A Supplemental's Original's track files come from the other packages the window
  lists; a track file not found becomes filler, named in a message.
* The sequence, named for the Content Title, at the composition's edit rate: the MainImageSequence's resources one
  after another on V1 (each repeat a segment of its own), each MainAudioSequence on audio tracks from A1, one track per
  channel of its clips. Other virtual tracks (subtitles, markers) are left out, said in the Console.

## The IMF window (`src/ui/imf_window.*`, the "IMF" panel)

Tools > IMF Window (p.335):

* **Explore** (p.334-335): "navigate the file system to link to an existing IMF package by selecting the root folder";
  the package is listed in the sidebar.
* **Sidebar:** the root folder, its IMPs (packing lists by file name) and their CPLs (by Content Title) in a tree with a
  Status column; packages read from disk are green ("has been exported and exists on disk", p.334). Deleting a selected
  root takes it off the sidebar only (p.338). Ours: the list lasts for the session.
* **Right pane** (p.335), read-only for packages on disk: the root's IMF name, directory, Issuer, Creator and
  Annotation; an IMP's name, Issuer, Creator and Annotation; a CPL's Content Title and CPL file name, then Content Kind,
  Edit Rate, Duration, the segments, virtual tracks and track files, Issuer, Creator, Issue Date and Annotation (ours:
  the guide says "Additional structural and descriptive metadata").

## Later

* 11.5c2: New IMF, the metadata to complete, the yellow and red statuses and Export: an IMF Original written from a
  sequence (track files, CPL, PKL, asset map).
* 11.5c3: Supplementals (a sequence dragged onto an existing package), MultiChannel Audio Mapping and MCA descriptive
  metadata (p.338-340), the supported formats table (p.340-341).

## Tests

`tst_imf` (an Original and a Supplemental read, a composition into a sequence with repeats, sound in samples, missing
track files), `tst_imf_ui` (Tools > IMF Window, Explore, the sidebar and its metadata, a CPL dragged and imported into a
bin as linked clips and a sequence, removing a package).
