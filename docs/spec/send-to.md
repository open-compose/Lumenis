# Send To templates (Phase 7.1c2)

File > Output > Send To (Editing Guide p.863-867): an export with a
template's Export setting values, its destination folder, and what happens
after it.

## The menu

"If you create custom Send To templates, they also appear as Send To menu
commands" (p.865). File > Output > Send To lists **Pro Tools**, then the
user's templates by name, then Make New and Make New BXF Template (BXF,
p.906, later). The list is read again each time the menu opens and when the
user changes (`MainWindow::refreshSendToMenu`; the entries are dynamic menu
items, like the workspaces in Windows > Workspaces).

Templates are `.stt` files (ours: JSON, `src/model/send_to.*`) in a
`Send_To_Templates` folder beside the user's settings, as the classic Title
Tool's Title_Templates is (p.360; ours). A template is named by its file
(p.866, "Name the new Send To (.stt) template").

## Sending (`MainWindow::sendTo`)

1. The active bin's selection, as Export to File takes it.
2. **Pro Tools** first asks for one of its options (p.863-864;
   `ProToolsOptionsDialog`), remembered for next time:

   | Option | What is written (ours, `model::proToolsExports`) |
   |---|---|
   | Avid Video - Consolidate Audio | an AAF, video as a Video Mixdown, audio consolidated |
   | QuickTime - Embed Audio | later: AAF's embedded media |
   | QuickTime - Consolidate Audio | a MOV of the picture (DNxHR LB, p.876) and an AAF of the sound, consolidated |
   | Link to Video and Audio | an AAF linking both |
   | Video Mixdown - Link to Audio | an AAF, video as a Video Mixdown, audio linked |
   | QuickTime - Link to Audio | a MOV of the picture and an AAF linking the sound |

   The consolidated and mixed down media go beside the AAF
   (docs/spec/aaf.md "Export Methods"), not inside it.
3. The **Send To dialog** (`SendToDialog`, p.864-865): the Filename (the
   selected item's name), the destination with Set ("the destination folder
   that you set last appears", kept in the template, or for Pro Tools in the
   application's settings), Auto Launch with its application (Add Item),
   Auto Load Exported File(s), Reveal file, the Export Setting Summary (every
   option of the Export setting with its value), Options (the Export
   setting's dialog for the template's values), Save As Template, OK.
4. On OK each export is written with the template's values in place of the
   Export setting's (`MainWindow::exportOverride_`, read through
   `exportSetting`); the Export setting itself is not changed. The files are
   `<folder>/<Filename>.<format's extension>`.
5. Auto Launch starts the application, with the exported files when Auto
   Load is on; Reveal file shows the first file in the file manager (p.866).

**Make New** (p.866-867): the Send To dialog with the Export setting's values;
OK asks where to save the `.stt`, and the menu lists it.

## Not done

* Disc > Authoring and One Step, Sorenson Squeeze and Avid DS (p.865-866):
  third-party products we do not have; the menu lists none of them (ours).
* Bin context menus' Send To (p.863): with the bin context menus' Output
  entries, later.
* Export by dragging to a folder (p.868-869): Qt can drag only files that
  exist, and exporting before the drop could take minutes; a deferred file
  drag needs each platform's file promise API (later, ours).
* A Pro Tools Session (.ptx, p.872-878): Avid's unpublished format, a
  provider hook only (docs/EXTENSIONS.md).

## Tests

`tst_send_to`: templates written and read back by file name, the Pro Tools
options' exports; Send To > Pro Tools with QuickTime - Link to Audio writing
a movie of the picture and an AAF of the sound into the folder set, Auto
Launch with the files and Reveal file; Make New saving a template the menu
lists (a dynamic item) that sends a Graphic still into its own folder while
the Export setting stays as it was.
