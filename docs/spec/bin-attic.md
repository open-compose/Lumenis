# The attic and auto-save (UI completion pass U3e)

Pages are the Editing Guide's.

Code: `model::saveToAttic`, `atticVersions` (`src/model/bin_attic.*`), `Locations::attic`
(`src/model/locations.*`); `BinContainer::setAttic`, `atticRoot`, `setAtticLimits`,
`setAutoSaveTiming`, `setSkipLockedPrompts` (`src/ui/bin_container.*`), applied from the Bin settings
in `MainWindow::applySettings`. Tests: `tst_bin_attic`, `tst_bin_attic_ui`.

## The attic (p.71-72, p.295, p.298-299)

* "The Avid Attic folder contains a folder for each project. Each project folder contains a Bins
  folder. When a bin is saved, a folder with the bin's name is created in the Bins folder and a copy of
  the bin file is stored in the folder with the bin name. The system adds a version number to the bin's
  file name. The bin file with the highest version number represents the latest copy" (p.71):
  `<attic>/<project>/Bins/<bin>/<bin>.<n>.cthbin`. Ours: the copy keeps the `.cthbin` suffix, so File >
  Open Bin opens it directly (the guide's copies are "Source Clips.1"); characters that a file name
  cannot hold (p.295's list) become `_`.
* Where: "Avid Attic" beside the shared Users folder (p.71, p.298); ours is "Cutthulhu Attic" beside
  our Users folder (`Locations::attic`). A project on shared storage keeps its copies in a "Cutthulhu
  Attic" folder beside the project folder (ours, for the guide's Unity Attic "at the top level of the
  shared workspace", p.301).
* "A corresponding bin file is placed in the Avid Projects folder, and a backup copy is placed in the
  Avid Attic folder" (p.295): a new bin is copied at once; every save of a project bin, by hand or
  auto-save, adds a copy. Favorite Bins from other projects are not copied (ours).
* "The oldest backup file is overwritten only if the second-oldest backup file is more than 2 hours
  old" (p.71). Ours: at "Max versions of a file in the attic" the oldest copy goes when the second-oldest
  is over two hours old; otherwise the newest copy is replaced.
* "Maximum files in a project's attic" (p.1153): over it, the project's oldest copies go, by file time.
  "Always keep at least one version in Attic" (p.1154) keeps each bin's last copy; without it any copy
  may go.
* "Setting to zero the maximum number of files ... as well as the maximum number of versions of a bin
  deletes existing files in the project folder in the Avid Attic folder and prevents any backup bins
  from being saved" (p.299). Ours: either one at zero alone keeps no new copies but deletes nothing.
* Retrieving copies is done with the system's file browser and File > Open Bin (p.71-72); there is no
  command for it.

## Auto-save (p.298-299, p.1152-1154)

* "Auto-Save interval ... The default is 15 minutes. To avoid interrupting an edit, Media Composer waits
  until your system is inactive before auto-saving" and "Inactivity period ... The default is 15 seconds"
  (p.1152): when the interval has passed, the auto-save (a Save All of bins with changes) waits until no
  key, mouse button or wheel has been pressed for the inactivity period.
* "Force Auto-Save at ... Once this time elapses, Media Composer auto-saves the project files even if it
  must interrupt an edit to do so. The default is 17 minutes" (p.1153): counted from the last auto-save.
  A force time shorter than the interval acts at the interval.
* "Skip prompts to save locked bins on Auto-Save" (p.1154): an auto-save that a bin's lock refuses
  (docs/spec/bin-locking.md) is not reported; without it the Save Bin message says so.
* An interval of 0 turns auto-save off (ours).
