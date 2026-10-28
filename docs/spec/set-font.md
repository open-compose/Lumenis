# Windows > Set Font and Help > Shortcuts (UI completion pass U3c)

Pages are the Editing Guide's.

Code: `SetFontDialog` (`src/ui/set_font_dialog.*`), `MainWindow::chooseFont`, `BinDisplay::fontFamily`
and `fontSize` (`src/model/bin.*`), `Composer::setNameFont`, `Timeline::Display::clipFontFamily` and
`clipFontSize` with `Timeline::clipFont`; `ShortcutsWindow` (`src/ui/shortcuts_window.*`). Test:
`tst_set_font`.

## Set Font (p.81-82)

* "You can change the default font and point sizes of the Bin, Composer monitor, Script, and Timeline
  windows."
* "Click the Bin, Composer monitor, Script, or Timeline window to make it active. Select Windows > Set
  Font. The Set Font dialog box opens. Click the Font menu, and select a font. ... Type another point
  size for the font in the Size text box. Click OK. The new font and point size appear in the active
  window."
  * The dialog: a Font menu with the installed fonts and a Size box (ours: 6 to 72 points).
  * With no Bin, Composer or Timeline active, nothing opens (ours).
* Where each is kept (the table on p.81-82):
  * Bin: "Changes the font and point size of the text in the Bin window; saves as a Bin setting (not a
    Bin View setting)"; "Bin fonts are stored in each bin" (p.82). Kept in the bin file's display and
    used by its Text, Frame and Script views.
  * Composer monitor: "Changes the font and point size of the sequence or source clip name text; saves
    as a Composer setting." The two Clip Name menus' font, kept in the Composer setting.
  * Timeline: "Changes the font and point size of clip text; saves as a Timeline View setting." Kept in
    the Timeline's display, which Timeline Views save (docs/spec/timeline-views.md).
  * Script: there are no scripts yet.
* "Override all Bin fonts" and "Override all Bin font sizes" (Interface settings, p.82) come with the
  Interface settings.

## Help > Shortcuts (p.97)

* "For a list of other functions that use modifier keys, select Help > Shortcuts."
* Ours: a window listing every command that has a menu shortcut or a key in the Keyboard setting, with
  both, sorted by name, and a Find box that filters by command or key. It reads the keyboard each time it
  is shown.
