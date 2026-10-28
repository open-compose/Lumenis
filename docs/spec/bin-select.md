# Bin > Select, Reveal File and Close All But Active (UI completion pass U3a)

Bin > Select's Offline Items, Media Relatives, Sources and Unreferenced Clips (p.266-267), File >
Reveal File (p.360) and Windows > Close All But Active (p.296). Pages are the Editing Guide's.

Code:
* `model::mediaRelatives`, `sources`, `unreferencedClips` (`src/model/bin_relations.*`).
* `MainWindow::binSelect`, `revealFile` (`src/ui/main_window.cpp`).
* Test: `tst_bin_select`.

## How objects refer to each other (ours)

A master clip or audio clip holds media; a subclip refers to its master clip (its Clip source,
`BinItem::sources`); a sequence refers to the clips of its segments, by MobID and by clip id. Effect
clips and titles count as clips. Tapes and media files are not bin objects here.

## Offline Items (p.266)

* "Offline items are clips, subclips, or sequences that are missing some or all of their original media
  files or that have never been captured. ... Select Bin > Select > Offline Items ... The bin highlights
  all items that are missing media files."
* In the active bin: master clips, subclips and audio clips with no media, unlinked media or a missing
  file (as the Timeline's offline colour decides, docs/spec/timeline-colors.md), and sequences with a
  segment from such a clip (ours: looked up as the monitors look clips up).

## Media Relatives (p.266-267)

* "Media Composer highlights all other clips linked to the selected clip, such as subclips or other
  sequences. ... The system highlights all related objects in all open bins."
* Every object in the open bins that uses media the selected objects use (a master clip's own, a
  subclip's master's, a sequence's clips' media through their subclips), with the selected ones. Each
  open bin's selection becomes what was found in it.

## Sources (p.267)

* "The Select Sources command identifies all the sources used by a particular object. For example, if
  you select a sequence as the object, the Select Sources command identifies every master clip, subclip,
  tape, and media file that is a source for that sequence. ... All sources for the selected objects in
  all open bins highlight."
* The clips the selected objects use: a sequence's segments' clips and subclips, a subclip's master clip
  (and a used subclip's master clip). Tapes and media files are not bin objects, so they are not shown.

## Unreferenced Clips (p.267)

* "Media Composer highlights all clips not currently referenced by clips or sequences that are in the
  open bins. Any master clips, subclips, or effect clips you edited into sequences in the bins do not
  highlight." "A message informs you that unreferenced clips highlight in open bins only (items in
  closed bins do not display)." The message's wording is ours.

## Reveal File (p.360)

* "The Reveal File command lets you select a clip in a bin and automatically open its related media file.
  ... The system searches all available drives, opens Windows Explorer or the folder (macOS), and
  highlights related media files."
* The first selected clip's first media file that exists: Explorer (`explorer /select,`) or the Finder
  (`open -R`) select it; on Linux its folder opens (ours: file managers have no common way to select a
  file). With no such file a message says so (ours).

## Close All But Active (p.296)

* "To close all but active bins, scripts, and pop-up monitors: Hold down the Command key (macOS) or the
  Control key (Windows). Select Windows > Close All But Active." There are no scripts or pop-up monitors
  yet, so it closes every bin but the active one, as Close All Bins But Active does.

## Not built

* Enlarge and Reduce HeadFrames (p.1016) belong to the NRCS Tool (News option), which is not planned.
