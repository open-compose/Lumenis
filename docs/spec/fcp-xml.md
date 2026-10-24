# FCP 7 XML (Phase 3.6c)

Final Cut Pro 7's XML Interchange Format ("xmeml") is the timeline format
Premiere Pro, DaVinci Resolve, Hiero and many conform and VFX tools read
and write. **Media Composer has no FCP XML feature**: the Editing Guide does
not mention it. This is Cutthulhu's own, for turnovers with those programs,
and is listed as such wherever it shows in the UI (ui-inventory §18, §19).

Cutthulhu reads and writes xmeml through its OpenTimelineIO import and
export (otio.md), as OpenTimelineIO's own FCP adapter does: an xmeml file is
turned into OTIO's JSON and imported (`interop::fcpxml::toOtioJson`, then
`otio::importJson`), and a sequence is exported as OTIO and written as
xmeml (`src/interop/fcp_xml.*`). So FCP XML and OTIO read into bins the same
way, and the same media is the same clip in both (`otio::mediaMobId`).

Sources:
* Apple's published *Final Cut Pro XML Interchange Format* (the xmeml
  elements, inheritance, the `id` attribute, transitions and the `-1`
  start and end, Time Remap).
* OpenTimelineIO's FCP 7 XML adapter (Apache-2.0,
  github.com/OpenTimelineIO/otio-fcp-adapter): read for how Premiere Pro and
  Hiero write the format in practice and how it maps to OTIO. Its sample
  files are in the interchange corpus (`scripts/interop-corpus.py`, source
  `otio-fcp`, not committed) and its reader checks ours
  (`scripts/otio-check.py --format fcpxml`). No code of it is copied.
  Credited in THIRD-PARTY-NOTICES.md.

## The format

```xml
<xmeml version="5">
  <sequence id="sequence-1">
    <name>Harbor</name> <duration>175</duration>
    <rate><timebase>25</timebase><ntsc>FALSE</ntsc></rate>
    <timecode><rate>...</rate><string>01:00:00:00</string><frame>90000</frame></timecode>
    <media>
      <video><track>
        <clipitem id="clipitem-1"><name>Wide</name>
          <start>0</start><end>-1</end><in>50</in><out>105</out>
          <file id="file-1"><name>wide.mov</name><pathurl>file:///media/wide.mov</pathurl>
            <duration>500</duration><timecode>...</timecode></file>
        </clipitem>
        <transitionitem><start>45</start><end>55</end><alignment>center</alignment>
          <effect><name>Cross Dissolve</name><effectid>Cross Dissolve</effectid></effect>
        </transitionitem>
        <clipitem id="clipitem-2">... <start>-1</start> ... <file id="file-1"/></clipitem>
      </track></video>
      <audio><track>...</track></audio>
    </media>
    <marker><name>...</name><in>100</in><out>-1</out></marker>
  </sequence>
</xmeml>
```

* **Where sequences are:** under `<xmeml>`, or in `<project>` and `<bin>`
  elements inside `<children>`. A `<sequence>` inside a `<clipitem>` is a
  nested sequence.
* **Inheritance:** an element without a value has that of the element it is
  in. A clip without `<rate>` has its track's, its sequence's. The lookup
  follows where an element is used.
* **`id`:** an element with an `id` given again later is the first one
  (a `<file>` is written in full once, then as `<file id="file-1"/>`).
* **Rates:** `<timebase>` frames a second, times 1000/1001 when `<ntsc>` is
  TRUE.
* **Items:** `<start>` and `<end>` place an item in its track. A clip's `<in>`
  and `<out>` are frames of its media counted from the media's first frame,
  whose timecode is the `<file>`'s `<timecode>`.
* **Transitions:** `<transitionitem>` with `<start>`, `<end>` and
  `<alignment>`. The cut is at the end for `end` and `end-black`, at the
  start for `start` and `start-black`, and halfway otherwise. A clip's
  `<start>` or `<end>` of -1 is the cut of the transition beside it, and its
  `<in>` then counts from the transition's start.
* **Exploded audio:** a stereo pair written as two tracks
  (`currentExplodedTrackIndex`, `totalExplodedTrackCount`).

## Import (`interop::fcpxml::importXml`, `importFile`)

xmeml is read into a small element tree (at most 256 deep and 4 million
elements; deeper or larger documents are refused) and turned into OTIO JSON,
which `otio::importJson` imports (otio.md, "Import"):

* Each sequence becomes a Timeline; several make a SerializableCollection.
  `global_start_time` is the sequence's `<timecode>` (its `<frame>`, else its
  `<string>`).
* Video tracks, then audio tracks. Of an exploded audio group only the first
  track is read, as OpenTimelineIO's adapter does.
* Items are laid end to end, with a gap where one starts after the item
  before ends. Overlapping items are placed one after another, with a
  warning.
* A clip's source starts at `<in>` plus its file's timecode plus, after a
  transition, how far the cut is into the transition. Its media is the
  `<file>`:
  * a `<pathurl>`: an ExternalReference, whose available range is the file's
    timecode and `<duration>`;
  * a `<mediaSource>`: a generator;
  * neither: a missing reference named after the file.
* A `<generatoritem>` is a generator, which the import reads as filler.
* `<enabled>FALSE</enabled>` disables a clip, so it is read as filler.
* A nested sequence becomes a Stack of its tracks of the clip's own kind (on
  a video track, its picture), trimmed to the clip's in and out. The import
  shows its top track with material.
* Time Remap (`effectid` `timeremap`) with a `speed` parameter, a
  percentage, negative when `reverse` is TRUE, becomes a Motion Effect.
  Other filters are listed as not read.
* Markers' `<in>` and `<out>` (-1 or not past the in: one frame) are counted
  in the clip's media. The sequence's markers go on TC1.
* **Media keys** (otio.md): `file://localhost/` is the same file as
  `file:///`. The same path given with another start timecode (Premiere
  writes a `<file>` for each clip) is other media: a clip of its own.
* A document whose root is not `<xmeml>` (Final Cut Pro X's `<fcpxml>` is
  another format) or that holds no sequence is refused.

## Export (`interop::fcpxml::exportXml`, `exportFile`)

The sequence goes through `otio::exportJson` (otio.md, "Export") and is
written as xmeml version 5:

* One `<sequence>` at the sequence's rate, its start as `<timecode>` (NDF),
  its `<duration>` the end of its last clip (xmeml has no gaps, so filler
  after the last clip is not kept).
* Video tracks, then audio tracks, each clip a `<clipitem>`.
  * Its `<in>` and `<out>` are counted from its media's start.
  * Its `<file>` is written in full the first time, with `<pathurl>`,
    `<duration>`, `<timecode>` and `<media>`, then by its `id`.
  * Audio clips name their `<sourcetrack>`.
* A dissolve between two clips is a `<transitionitem>`, aligned `center`,
  with the clips beside it ending and starting at -1. Video dissolves are
  Cross Dissolve and audio ones Cross Fade (+3dB).
  * A transition not centred on its cut is written centred on the same cut,
    with a warning.
  * A transition beside a gap (a fade from or to black) is left out, with a
    warning: xmeml would hold it next to nothing.
* Speed changes are Time Remap filters (speed in percent, reverse for a
  negative one). Markers go on their clips and the sequence. Marker colours
  are left out: FCP 7 markers have none.
* The warnings are the OTIO export's, then those of the xmeml writing.

## In the application

* **Import:** File > Input > Import Media and a drop into a bin take `.xml`
  files whose root is `<xmeml>` (`MainWindow::importFcpXml`), as they take
  `.otio` files (otio.md, "In the application").
* **Export:** File > Output > Export to File with the Export setting's Export
  As at "Final Cut Pro 7 XML" (ours, last in the list) writes the first
  sequence selected. Include All Video/Audio Tracks leave out that kind of
  track when off. The extension is `.xml`.

## Checks

* `tst_fcpxml`:
  * the rules above on a document written in the test;
  * refusals, the export and its round trip;
  * the media keys shared with the OTIO import;
  * a nested sequence and damaged documents;
  * the corpus's FCP XML files.
* `tst_interchange` `fcpXml`: Import Media, a drop and Export to File.
* ctest `fcpxml_round_trip` (`otio-check.py --round-trip --format
  fcpxml`): every corpus FCP XML and OTIO file exported as FCP XML, and the
  export exported again, gives the same document.
* ctest `fcpxml_python` (`otio-check.py --compare --format fcpxml`,
  skipped without `opentimelineio` and `otio-fcp-adapter`): OpenTimelineIO
  reads each export with its FCP adapter and finds, track by track, what it
  finds in the original, as for OTIO (otio.md, "Checks"). These are left out
  of both sides, as Cutthulhu reads them otherwise by design:
  * transitions beside a gap;
  * clips the original disables (the adapter keeps `<enabled>` only as
    metadata);
  * the spans of sequences nested in clips: the adapter (1.0.0) reads them
    as empty, looking for their tracks under `<sequence>` instead of under
    its `<media>`;
  * empty tracks.
* The fuzz target `fcpxml` (fuzz/README.md).
* By hand: `cutthulhu --import-fcpxml FILE.xml` and `cutthulhu
  --export-fcpxml BIN.cthbin SEQUENCE OUT.xml` (an FCP XML or `.otio` file
  in place of the bin is imported first). `--export-otio` also takes an FCP
  XML file: a conversion.

## Later

* Several sequences in one export (xmeml can hold them; Export to File
  writes the first one selected).
* Generators, titles and filters other than Time Remap; audio levels and
  pan; the `<logginginfo>` and `<labels>` of clips as bin columns.
* Final Cut Pro X's FCPXML, a different format.
