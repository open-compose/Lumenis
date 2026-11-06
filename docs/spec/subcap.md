# The SubCap effect and caption files (Phase 7.4a)

FX Guide ch.14, "Working with the SubCap Effect" (p.378-388), and the SubCap
parameters (p.501-504). "The SubCap effect lets you add text to the video in your
sequences, either by entering text directly or by importing text from files
formatted in standard captioning formats" (p.378).

## The effect (`src/model/subcap.*`, `src/media/subcap.cpp`)

* SubCap is a segment effect from the Generator category. A caption is the
  segment's text, kept in the Caption Text category's text box
  (`model::kCaptionTextKey`); rows are separated by line breaks.
* The picture (`media::subcapPicture`, from the compositor before Burn-In): the
  text in the Text Appearance font, size, colour and opacity, with its outline
  and shadow, on the Box Appearance box, placed by the Position and Alignment
  parameters (p.501-503). Bypass Effect, or no text, leaves the picture as it is.
  Ours where the guide gives only ranges (`media::subcapDefault`): white 48-pixel
  sans text, centred, on a half-opaque black box whose bottom centre is 120 pixels
  above the bottom of a 1920 x 1080 frame.

## Caption files

* **Avid DS caption file** (p.379; `parseDsCaptions`, `writeDsCaptions`): a line
  "01:00:02:00 01:00:03:00" (in and out timecode), then the caption's rows, the
  captions apart by an empty line.
* **EBU N19** (`.stl`, EBU Tech 3264; `parseStl`, `writeStl`): the 1024-byte
  General Subtitle Information block and one 128-byte TTI block per caption.
  STL25.01 or STL30.01 by the sequence's rate. Ours: one TTI block per caption,
  its text field cut at 112 bytes; ASCII only, other characters written as `?`;
  rows joined by the standard's line break 0x8A. The GSI's titles, names,
  publisher, country, language code, user data and dates come from the Global
  Properties (`StlInfo`, "the character limits ... are imposed by the EBU N19
  (.stl) caption file standard", p.504).

## Commands in the Effect Editor (`EffectEditor::subcapActionRequested`, `MainWindow::subcapAction`)

* **Import Caption Data** (p.379-380): the file's timecode is the sequence's
  timecode; "Media Composer subdivides the effect at the timecode points
  specified by the file. This creates a series of separate effects, one for each
  caption" (`model::importCaptions`). The frames between captions keep the
  effect without text (ours). A caption outside the SubCap segment stops the
  import with the reason in the Console.
* **Export Caption Data** (p.386): as `.stl` or Avid DS by the file's
  extension, the caption tracks chosen in `ExportCaptionsDialog`; "a caption file
  can have only one caption for any given timecode range", so where two tracks'
  captions overlap the higher or the lower track wins, as chosen
  (`model::sequenceCaptions`).
* **Edit Master Caption List** (p.384): `MasterCaptionListDialog` lists the
  track, timecode range and text of each SubCap segment (`subcapEntries`), empty
  ones too; edited text goes back to the segments as one undo step.
* **Global Properties** (p.387-388, p.504): `SubcapPropertiesDialog`, tabs
  Contacts, Custom, Program, Stylesheets and Synchronize.
  * Stylesheets "are XML files and are stored in a folder called Caption Effect
    Stylesheets" (p.387): ours beside the user settings
    (`MainWindow::subcapStylesheetFolder`), one `<name>.xml` per stylesheet
    holding the effect's parameters but its text, and `default.txt` naming the
    default, which new SubCap effects take (`applySubcapStylesheet`, the effect
    seed). Ours: the XML is a flat list of parameter elements.
  * **Synchronize** (p.384-385; `model::synchronizeSubCap`): each parameter or
    information item is Clip, Track or Sequence; a change to one SubCap effect
    goes to the others on its track or in the sequence. Ours: Sequence until
    chosen. The caption text is always each clip's own.

## Not yet

Closed captions (SCC import into caption data tracks, SCC and TTML export, the
caption preview) are 7.4b. "SRT" in the Editing Guide is the Secure Reliable
Transport streaming protocol, not SubRip subtitles, so SubRip is not planned.

Tests: `tst_subcap` (files, import, export choices, synchronizing),
`tst_subcap_ui` (the picture, the commands in the application).
