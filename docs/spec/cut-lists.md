# Film cut lists

ROADMAP 9.4. Editing Guide p.939-944 ("Understanding Cut Lists and Change Lists", the lists), p.960-962 ("Cut List:
List Options"), p.964 (formatting); FX Guide p.79 (the Effect Grid's position information) and p.412 (the
Previsualization Marker tool).

"Cut lists and change lists provide breakdowns of exactly which frames should be cut from the work print or original
negative ... film lists are read by assistant editors or negative cutters. Avid's cut lists also include
industry-standard optical list information" (p.939).

## 9.4a: cut lists in Columnar and TabbedLists

`interop::cutlist::exportText` (`src/interop/cut_list.*`) makes the list; the List Tool (`src/ui/list_tool.*`) offers
Columnar and TabbedLists in the Output Format menu, and its List Options tab shows a Cut List box for them ("The
options in the List Options and Formatting tabs change depending on which Output Format you select", p.944).

* **Frame references.** Each event names its source by key numbers (the bin's KN Start), else ink numbers (Ink
  Number), else timecode (Start; ours, for clips without film numbers, with a warning when none is there). A film
  number is the roll's prefix and feet+frames ("KZ 12 3456 1234+05"); frame counts such as "FXS32v01-000001" (p.966)
  are read when frames are counted.
* **Counting.** By the project's film type (p.107): 16 frames a foot for 35mm 4 perf (also for video projects), 40 for
  16mm; 35mm 3 perf counts frames (ours: its feet hold no whole number of frames). The master footage starts at
  "Start at" (p.964), in frames.
* **Assemble list** ("the order in which a sequence's clips, optical effects, or standard dissolves and fades are
  assembled ... also flags duplicate frames", p.941): one row per event, track by track, with its footage in and out,
  duration, first and last edge, camera and sound roll (Reel Names), clip name (Clip Names) and comments: the flags
  (Dupe n, Optical n, "Short cut", "Jump cut (... missing)") and the segment's Clip Note (Clip Comments). Mark Short
  Cuts and Mark Jump Cuts take their minimum in frames (p.961); a jump cut is a following event of the same clip and
  track with a few source frames left out.
* **Scene assemble list**: refused unless every event's clip has a Scene ("The Scene/Take column must be filled in
  within the bin", p.942); ours: sorted by scene and take, then reel, in assembly order within them.
* **Optical list** (p.942, p.961): each optical in sequence order, numbered as the assemble list flags them. A
  transition lists its outgoing shot (A) and incoming shot (B) over the effect; a segment effect its shot. Standard
  film masks are left out ("Cut lists do not describe standard film masks", p.942); custom masks are opticals. Key
  Frames lists each keyframed parameter's keyframes as footage (from the optical's start with Optical Footage).
  Previsualization markers list each marker's place at each keyframe "in terms of compass coordinates and X, Y
  coordinates" (FX Guide p.412): ours "N 0.25 E 0.40 (X 0.40, Y -0.25)" from the frame's centre, with the marker's
  colour.
* **Dupe list** (p.942, p.962): the Dupe Detection's sets (`model::dupes`, with Assume Handles), each entry's record
  footage and source edges widened by Print w/Handles.
* **Pull list** (p.943, p.961): every event sorted by First and Second Sort by (None, Labroll, Camroll, Soundroll,
  Scene and Take, Name, Reel #), then by edge number, Heads Out ascending or Tails Out descending; with the dupe set,
  without comments.
* **Layout.** Columnar: the title, then each list's name, its column heads, a rule and the rows, columns as wide as
  their widest cell. TabbedLists: the same rows, tab-separated. Save List writes the list as a `.txt` file.

## 9.4b: XML and WebLists

* **XML** (p.957: "a file that includes all the information about the list: it shows you every column heading and
  every custom column"): ours `<CutList title framesPerFoot>`, a `<List type name>` for each list, a `<Row event>` for
  each row with a `<Cell heading>` for each column and, for a row about an event, a `<Column heading>` for each of the
  clip's bin columns. Mark Short Cuts and Mark Jump Cuts are "Not included in XML output" (p.961). Save List writes
  an `.xml` file.
* **WebLists** (p.957-958): an HTML page for each list, CutAsm.htm for the assemble list ("the Assemble List is called
  CutAsm.htm") and ours CutScene.htm, CutOpt.htm, CutDupe.htm and CutPull.htm, with a navigation line between them
  and the style sheet in the Files sub folder (`Files/cutlist.css`). The links: the assemble list's Optical n and
  Dupe n to the optical and dupe lists ("Opticals in the Assemble List that are linked to their description in the
  Optical List", "Duplicates in the Assemble List that are linked to the entries in the Dupe List"), and the first
  column of the optical, dupe, pull and scene assemble lists back to the event in the assemble list. Preview shows
  CutAsm.htm's source; Save List asks where CutAsm.htm goes (suggesting "<title>/CutAsm.htm"), writes every page and
  the Files folder there and opens CutAsm.htm in the system's browser ("When the HTML file is created, the Browser
  automatically launches"). Ours: no banner, logo or script; one style sheet.

## 9.4c: change lists

"A change list is generated to simplify the process of updating conformed cuts to match and compare changes in
versions of the sequence" (p.962). `interop::cutlist::exportChangeList` compares each new sequence with its old one;
the List Tool's Change List button (enabled for the cut list types) shows an Old Sequence row with its own Load (the
Record monitor's sequence, as Load is) and the Change List options box.

* **Changes** (p.940): "Insertions for new material to be added", "Deletions of old material no longer needed",
  "Trims to be made to the heads, tails, or middles of the edits" (Insert Head, Delete Head, Insert Tail, Delete Tail,
  the names of p.965's icons) and "Moves that consist of a matched pair of deletions and insertions of an entire shot
  or shots". Ours: an old and a new event on one track are the same shot when they share frames of the same clip's
  track (each old event matched once, to the new one sharing most); the matched shots in the longest run that keeps
  their order are trimmed where their ends differ, the others moved; unmatched new events are insertions and
  unmatched old ones deletions, placed after the new place of the last kept shot before them.
* **Columns** (ours): #, Code (Preview Code), Change, Track, Footage (in the new sequence), Old Footage, Length, First
  and Last Edge of the frames concerned, Clip Name.
* **Options** (p.962-963): Show only changes (on; off adds "No change" lines for the shots left as they were), Combine
  Deletions (a run of deletions next to each other in the old sequence as one event, the clip names joined), Preview
  Code (ours: a Code column numbering the changes 001, 002 ...), Change Pull (the insertions, head and tail insertions
  and moves, sorted as the pull list), Change Discard (the deletions and head and tail deletions). The new sequence's
  optical list follows when Optical is on.
* **Reels** (p.953-954): several sequences pair by the Reel # column when all have one ("For each sequence, the old
  version and the new version must have exactly the same reel number"), else in their order; unequal counts are
  refused ("You must have an equal number of old and new sequences"). Each reel's lists are named "(REEL n)".
* **WebLists**: CutChg.htm, CutChgPull.htm and CutChgDisc.htm (ours), with the new and old assemble lists (CutAsm.htm,
  CutAsmOld.htm) that the changes link to ("Change lists that are hyper linked to the old and new Assemble and Pull
  Lists", p.958); later reels' pages end in the reel's number. XML lists name an old sequence's event `oldEvent`.

## Not yet

* Show Icons (p.964); dragging several sequences into the Old and New Sequences panes (the model takes several reels).
* LFOA, Scan, Show Dupe Refs, Include Leader/Opticals, Place Separators, Page Breaks Between Opticals; the Formatting
  tab's Running As Mode, Master TC Track, Display Handles, Separate List for Each Channel, Show Prefix, Matchback Info,
  Disable Perf Slip. Aux ink numbers.

## Tests

`tst_cut_list` (film numbers, the assemble, optical, dupe, pull and scene assemble lists, short and jump cuts, key
frames, TabbedLists, XML, WebLists and their links, change lists with trims, insertions, deletions, moves, Combine
Deletions, reels and their web links); `tst_cut_list_ui` (the List Tool's formats, the Cut List box,
Preview, Save List of text and web pages, the film type).
