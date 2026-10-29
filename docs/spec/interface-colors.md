# Interface settings: colours and fonts (UI completion pass U3g)

Pages are the Editing Guide's.

Code: the "color" and "font" option types in `SettingsDialog` (`src/ui/settings_window.cpp`);
`BinAppearance` and `BinView::refreshAppearance` (`src/ui/bin_view.*`); `BinContainer::setBinAppearance`,
`setSidebarAppearance`, `setNewBinDefaults`, `BinDockArea::setTabColor`; `Composer::setViewerBackground`;
`Timeline::Options::background`, `blendBackground` and `Timeline::backgroundColor`; all applied in
`MainWindow::applySettings`. Options in `src/ui/resources/settings.json`. Test: `tst_interface_colors`.

## Controls

* "you can choose a color from the color picker" (p.1201): a colour option is a swatch button that opens
  the system colour picker; its menu also has No Color where the option may be empty. Ours: each colour
  is an option of its own under the check box that turns it on.
* A font option lists the system's font families after Default (the application's font). Sizes are in
  points.

## Bins tab (p.1201)

* "Use a custom background color for all Bin Container Sidebars", "Override Bin Container Sidebar font",
  "Override font size for all Bin Container Sidebars": the Sidebar's list in every Bin Container, floating
  ones and the Project Bin Container's included. The quick filter keeps the theme's look (ours).
* "Allow custom bin backgrounds ... You can also choose to apply the bin background color to tabs only":
  off, bins show the theme's background whatever Set Background Color chose (p.263); with tabs only, the
  bin shows the theme's and its tab a swatch of its colour (ours: a small square icon).
* "Default background color for new bins", "Default font for new bins": a new bin starts with them, saved
  in its file as if chosen with Set Background Color and Set Font (p.81-82).
* "Override all bin fonts", "Override all bin font sizes": every bin is shown in that font or size; the
  bins' own fonts stay in their files.
* "Bin icons and text brightness" and "Bin highlight brightness" are not built.

## Timeline and Viewers tab (p.1201-1202)

* "Use custom viewer background ... the background of the Source/Record Monitors": the area around the
  pictures, the pictures' black left as it is (ours).
* "Use Timeline Background Color": the tracks area's background. "Use Blending Based on Skin: When
  selected, the Timeline background color will be blended with the current skin's background gray for a
  softer look. When deselected, the custom Timeline background color will not be blended." Ours: half
  and half.
* "Timeline and Composer icon brightness", "Metadata text brightness" and "Default Timeline V/A/TC
  Tracks" are not built.
