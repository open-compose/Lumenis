# Windows > Bin Layout (UI completion pass U3d)

Pages are the Editing Guide's.

Code: `BinContainer::binLayout`, `applyBinLayout` (`src/ui/bin_container.*`); `MainWindow::binLayouts`,
`newBinLayout`, `saveCurrentBinLayout`, `deleteBinLayout`, `openBinLayout` and `linkSettings`; the
"Bin Layout" Project setting and the Workspace View's Bin Layout option
(`src/ui/resources/settings.json`). Test: `tst_bin_layout`.

## Layouts (p.92-93)

* "You can arrange and save bin window configurations independently of workspaces, including the
  contents of bin windows containing tabbed bins." A layout holds the bins open in the main Bin
  Container and in each floating Bin Container, in tab order, with each floating container's place and
  size.
* "When you open a bin layout, Media Composer opens all bins saved in the bin layout and places them in
  the position configured in the layout." Ours: bins the layout does not name are closed; bins that no
  longer exist are skipped; each floating container's bins are tabbed together. Tab groups split side by
  side within an area are not kept: an area's bins come back as tabs of one group.
* "If you close the application, the position of bins in your current configuration is saved but not the
  bin layout setting. To save a custom bin layout, you must use the Bin Layout menu."
* "Bin layouts are project settings": one "Bin Layout" Project setting per layout, named with it.
  Double-clicking one in the Settings window opens it (ours).
* "If you delete a bin layout, the layout is deleted from your project."

## The Bin Layout menu (p.92-93)

* "To open a bin layout ... Select Windows > Bin Layout > bin layout": the layouts are listed above the
  menu's commands, the one last opened or made checked.
* "Select Windows > Bin Layout > New Bin Layout. The New Bin Layout dialog box opens. Type a name for
  the bin layout, and then click OK. The application saves the bin layout, and the layout appears in the
  Bin Layout menu and in the Settings list." A name already used replaces that layout (ours).
* "To modify a bin layout: Arrange and size your bins. Select Windows > Bin Layout > Save Current." Ours:
  into the layout last opened or made; with none, a message says to open or make one.
* "Select Windows > Bin Layout > Delete Bin Layout. The Delete Bin Layout dialog box opens. Click OK."
  Ours: the dialog lists the layouts, the current one chosen.

## Linking to a workspace (p.91, p.93)

* "Click the Bin Layout menu and select a layout you want to link to the workspace": the Workspace View
  setting's Bin Layout box names a layout, which opens when the workspace does. Ours: a text box (the
  guide's picture shows a menu). "you can access these layout assignments only when you work in a project
  containing a bin layout with the same name" (p.92): a name the project lacks opens nothing.

## Not built

* Bin layout buttons on the Command Palette's Workspaces tab (p.92) and on the keyboard.
