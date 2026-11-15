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

## New IMF and Export (11.5c2)

* **New IMF > IMF Original** (p.334, p.337): "A new untitled folder appears in the sidebar (though it is not yet
  written to disk)", with an untitled IMP. Sequences dragged from a bin onto it (or onto the empty sidebar, which
  starts a new Original) are its CPLs, named "CPL_<Content Title>", the Content Title starting as the sequence's
  name. The first drag fills the untitled IMP; each later one makes a new IMP (p.337-338). IMF Supplemental is
  dimmed until 11.5c3. Deleting a draft's root takes it off the sidebar.
* **The right pane**, editable for a draft: the root's IMF Name, Directory (with Browse...), Issuer, Creator and
  Annotation; the IMP's Issuer, Creator and Annotation (its name "untitled" until written, p.335); the CPL's Content
  Title (the sidebar follows), Encoding Profile (J2K IMF YCrCb, J2K IMF RGB 12 bit, J2K IMF RGB 10 bit: the IMF
  resolutions of p.1256-1257), Content Kind (ST 429-7's kinds, ours), Issuer, Creator and Annotation. "Completing
  Issuer and Creator automatically populate the IMP and root folder required fields. You can change these
  independently" (p.336): they follow the CPL while they are empty or hold what it held.
* **Statuses** (p.334): red while required fields are missing (the tooltip names them), yellow when ready, green on
  disk. Ours, the required fields: the root's name, Directory, Issuer and Creator; an IMP's Issuer and Creator; a
  CPL's Content Title, Issuer and Creator, and the project's format in the IMF Supported Formats table (p.340-341:
  HD, 2K and UHD at 23.976 to 60p, 4K RGB only up to 30p; `media::imfFormatSupported`).
* **Export** (p.334) is enabled when the selected draft is ready; `MainWindow::exportImfDraft` writes it as the
  folder `<Directory>/<IMF Name>`, which must not exist, then lists the package on disk in the draft's place.

### What is written (ours)

* Each CPL's sequence as any export composes and mixes it (`exportMovieFile`'s sink), with "the tracks enabled"
  (p.336: the Timeline's selection for the sequence in the Record monitor, every track otherwise).
* The picture: one track file, JPEG 2000 in MXF OP1a at the project's raster, by FFmpeg's own `jpeg2000` encoder in
  4:2:2 10-bit Y'CbCr or 12- or 10-bit RGB as the profile says (`media::writeImfTrackFiles`, `src/media/imf_export.*`).
* The sound: the stereo mix as one 24-bit 48 kHz track file, wrapped by bmx's `raw2bmx -t op1a --audio-layout imf --track-map singlemca`
  (`LUMENIS_RAW2BMX`, beside the application, or PATH; BUILDING.md "IMF export"). FFmpeg's MXF muxer takes no
  sound-only file. Without raw2bmx a sequence with sound is not exported and the message says why.
* The documents (`interop::imf::writePackageDocuments`, `src/interop/imf_writer.*`), in the 2016 namespaces: a CPL
  per sequence with one segment holding each track file whole (sound in samples, "48000 1"), a PKL per IMP with
  each asset's SHA-1 in base64, size and original file name, and ASSETMAP.xml. The track files' ids are ours, not
  read from the MXF files' packages.

## Watch

* Neither the track files nor the documents are validated against ST 2067-2/3/5 or by a checker (Photon): what we
  check is that our reader and FFmpeg's IMF reader (`ffprobe -f imf`) take them.
* The guide lists "J2L IMF RGB 10 bit" once beside "J2K IMF RGB 10 bit"; we take the latter.

## Later

* 11.5c3: Supplementals (a sequence dragged onto an existing package), MultiChannel Audio Mapping and MCA descriptive
  metadata (p.338-340), the supported formats table (p.340-341).

## Tests

`tst_imf` (an Original and a Supplemental read, a composition into a sequence with repeats, sound in samples, missing
track files), `tst_imf_ui` (Tools > IMF Window, Explore, the sidebar and its metadata, a CPL dragged and imported into a
bin as linked clips and a sequence, removing a package), `tst_imf_ui formatsAndProfiles` and `newOriginalAndExport`
(a draft from New IMF and a drop, its statuses and fields, Export, the package read back and by `ffprobe -f imf`, a
second IMP with sound through a stand-in raw2bmx).
