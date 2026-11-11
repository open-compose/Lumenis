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

## Not yet

* 9.4b: the XML and WebLists templates (p.957-958); Show Icons (p.964).
* 9.4c: change lists (p.940, p.953-954, p.962-963, p.965).
* LFOA, Scan, Show Dupe Refs, Include Leader/Opticals, Place Separators, Page Breaks Between Opticals; the Formatting
  tab's Running As Mode, Master TC Track, Display Handles, Separate List for Each Channel, Show Prefix, Matchback Info,
  Disable Perf Slip. Aux ink numbers.

## Tests

`tst_cut_list` (film numbers, the assemble, optical, dupe, pull and scene assemble lists, short and jump cuts, key
frames, TabbedLists); `tst_cut_list_ui` (the List Tool's formats, the Cut List box, Preview, Save List, the film type).
