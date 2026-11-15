# Closed captions on the Data track (Phase 7.4b)

Editing Guide: "Importing Scenarist Closed Caption Files" (p.232), "Preserving HD
Closed Captioning and Ancillary Data" (p.973-976), "Previewing Closed Captioning"
(p.983-984), "Searching for Closed Captions Text in the Timeline" and "Exporting
Closed Captions Text" (p.985).

## The data (`src/model/closed_captions.*`)

* An imported caption clip "is a D-track only clip that contains only the 608
  compatibility bytes from EIA-708" (p.232). Ours: the clip keeps CEA-608 field 1
  byte pairs, two bytes a frame with their odd parity, 0x80 0x80 where nothing is
  sent, in its composition under "closedCaptions" (`model::clipCaptionData`),
  with Tracks "D1". Avid keeps them as SMPTE 436M ANC in an MXF file; ours stays
  in the bin.
* A sequence's caption data is its D1 track's segments' data, null pairs in
  filler (`model::sequenceCaptionData`).
* **Decoding** (`model::decodeCaptions`): channel CC1 of field 1. "Pop-on,
  Roll-up and Paint-on captions are supported" (p.983): RCL, EOC, ENM, EDM, RU2-4,
  CR, RDC, BS, DER, PACs with their indents, Tab Offsets, mid-row codes (a space),
  the basic, special and extended characters. A repeated control code is ignored
  as 608 sends them twice. Colours, underline, italics and flash are not kept
  (ours). A frame's pair shows on that frame.
* **Our encoder** (`model::popOnCaptionData`, for tests and seeds): pop-on
  captions, each loading in the frames before it (RCL, ENM, a PAC per row,
  centred, its text, EOC on the caption's first frame), erased by EDM at its end;
  control codes are sent once.

## Import (`MainWindow::importScc`, `AncDataImportDialog`)

File > Input > Source Browser's Import, or a drop on a bin, with a `.scc` file:
"The AncData Import dialog box opens. The starting timecode and drop frame checkbox
will be automatically populated with the applicable information from the selected
.scc file" (p.232). The file's first line gives both (a `;` before the frames is
drop frame). The clip starts at the starting timecode; each line's pairs go on
consecutive frames from its timecode (`model::sccCaptionData`). Ours: lines before
the starting timecode are left out; in a project not at 29.97, lines land at their
time and pairs that would share a frame move to the next free one.

## Editing

The clip edits into the sequence's D1 like any other ("It can be added to the
Timeline just like any other clip", p.232; a Data track cannot be patched, p.974).

## The Timeline's D1 row (`TimelineView::paintCaptions`, `Timeline::captionTrack`)

"The D-track provides visual information on when the caption is loading,
displaying and switching to the next caption" (p.984): the loading phase as a
black line with bracket ends, the playing phase as a grey line, each caption's
text above its line when the row is 30 pixels or taller (the clip name goes under
it). "If you do break a caption, an orange triangle icon appears in the Timeline":
ours at a cut where the segment's clip sends pairs on both sides of it
(`model::brokenCaptionCuts`).

## Show Captions (`Composer::contextMenu(MonitorId)`, `Composer::shownCaption`)

"Right-click the monitor where you want to preview the Closed Captions, and
select Show Captions" (p.983), for the Source and Record monitors each: the
caption on the frame, from a sequence's D1 or a caption clip's own data
(`MainWindow::captionOn`). Ours: 608's grid of 15 rows of 32 columns over the
middle 80% of the picture, white monospaced text on black boxes. Pop-up monitors
have no Show Captions yet.

## Export (`MainWindow::writeExport`)

The Export setting's Export As **SCC** or **TTML** (p.985), of the first sequence
selected:

* SCC: "Scenarist_SCC V1.0", then a line for each run of pairs at the sequence's
  timecode (drop frame as the sequence is), so the data comes back as it was.
* TTML (W3C TTML1): one `<p>` per caption with its rows split by `<br/>`, times as
  media time from the sequence's first frame; white monospaced text on black in a
  region over the middle 80%, aligned to its bottom (ours).

## Data Mixdown (11.5a; `MainWindow::dataMixdown`)

Timeline > Mixdown > Data: "a data mixdown lets you combine several clips into one new master clip ... your end result
is a new master clip made up of all the data clips on the data track. That track becomes one clip" (p.982). The Record
sequence's D1 data between the marks, or all of it without marks, becomes a D-track only master clip named "<sequence>
Data Mixdown" (ours) starting at the range's timecode, in the target bin chosen (the bin choice of
`MainWindow::setBinChoiceHook`). **Ours:** the data stays with the clip in the bin, as an imported caption clip's does,
so there is no target drive to choose. D1 need not be monitored: the sequence has one Data track.

## Extract DNxHD Data (11.5a; `MainWindow::extractDnxhdData`)

Clip > Extract DNxHD Data, or the bin's right-click menu (p.974-975): after "The Confirm dialog box appears ... Click
Continue or Cancel", each selected master clip gains D1 ("The clip in the bin displays D1 in the Track column") holding
the CEA-608 field 1 pairs its media's frames carry. The guide's requirements are checked: "The clip must be a master
clip; The clip should not have a Data track associated with it already; ... The media needs to be online"; a clip that
fails one is named in the message and the Console.

**Ours:** "The media needs to be DNxHD format" is not checked. How the Legacy method embedded ancillary data in DNxHD
media is not published, so ours reads what the media carries as ATSC A/53 caption data, which FFmpeg's MPEG-2 and
H.264 decoders give as each frame's side data (`media::VideoReader::captionPair`); a clip whose media carries none is
refused with that reason. The writer can carry such data too (`media::MediaWriter::setNextCaption`, A/53 side data the
MPEG-2 and H.264 encoders put in their caption user data), which the tests use.

Not yet: searching caption text with the Timeline Quick Find (p.985, through
p.645), which waits for the Quick Find itself (7.4b2); the Legacy embedded method's own
media, AFD and output through hardware (p.973-981; 7.3c); linking MXF files with SMPTE 436M ancillary data (p.333,
p.977).

Tests: `tst_closed_captions` (SCC, decoding, our encoder, sequences, TTML),
`tst_closed_captions_ui` (import, Show Captions, D1, a broken caption, export; `dataTrackCommands`: Data Mixdown and
Extract DNxHD Data from an MPEG-2 movie with A/53 captions);
fuzz target `scc`.
