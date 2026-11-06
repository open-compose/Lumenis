# The Timeline Quick Find (Phase 7.4b2)

Editing Guide "Searching for Text in the Timeline" (p.645-646) and "Searching for
Closed Captions Text in the Timeline" (p.985). `src/ui/timeline_find.cpp`.

* At the foot of the Timeline, after the View Menu button: the **Timeline Quick
  Find** field, then **Find to the left** and **Find to the right** (the picture on
  p.646). Ours: the field's magnifier opens the menu; Enter finds to the right.
* The field's drop-down menu (`Timeline::findOptionsMenu`): **None**, **Visible
  Timeline Text** (checked at first, as pictured), **Resolution**, **Clip Name**,
  **Timeline Clip Notes**, **Markers** and **All**; "All, or any combination of
  these search options". None and All set every option.
* "The Timeline blue bar moves to the start of the next Timeline segment whose
  metadata contains a match for the search string" (`Timeline::findNext`). What
  each option searches (ours where the guide names only the option):
  * Visible Timeline Text: the Clip Text the segment shows (p.592), and the closed
    captions D1 shows, each found at its own start (p.985);
  * Resolution: the clip's Video column;
  * Clip Name: the segment's and its clip's names;
  * Timeline Clip Notes: the segment's note;
  * Markers: the marker's name and comment, found at its frame.
* Matching ignores case. With no match further on, the position stays (ours).
  The options are not kept between sessions (ours).

Tests: `tst_timeline_find`; caption text in `tst_closed_captions_ui`.
