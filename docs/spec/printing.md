# Printing (9.1)

Editing Guide p.292 "Printing Bins", p.424 "Printing the Contents of the Markers Window", p.645 "Printing the
Timeline"; p.288 (storyboards: "To print the storyboard, select File > Print Bin"), p.253 (the Script view's text "only
in printouts of the bin in Script view"), p.345 (Media Tool data prints "by using the same procedures for printing
bins"). Code: `src/ui/printing.*` (what goes on paper), `src/ui/main_window_print.cpp` (the commands). Qt Print Support
(part of Qt's qtbase, LGPL-3.0 like the rest of Qt) gives the printer and the system's Print and Page Setup dialogs.

## The commands

* **File > Page Setup** (p.292: "The Page Setup dialog box opens, reflecting the specific options for your printer"):
  the system's Page Setup dialog on the session's printer (`MainWindow::printer`). Its layout is used by every print
  until the application quits (ours: not kept as a setting).
* **File > Print Bin** (p.292: "Select the Text, Script, or Frame bin view of the bin you want to print ... The system
  prints the active bin"): the system's Print dialog, then the active bin as its view shows it (below). Ctrl+P (ks2009).
* **File > Print Frame** (p.292: "The system prints the frame currently displayed in the active monitor"): the active
  monitor's frame at its own size, scaled to fit the page (up to 8 times larger). Under it, ours: the clip's name and its
  master timecode at the position.
* **File > Print Timeline** (p.645: "Media Composer prints the current view of the Timeline. You can also use the Print
  Timeline command to print the Timeline in Heads view or in Heads Tails view"): the Timeline as it is on screen,
  whatever view it shows, fitted to the page's width and height (never enlarged), the sequence's name under it.
* **File > Print** (p.424: the Markers window, and the Console after Ctrl+Alt+P): while the Markers window or the
  Console is the window last used, File > Print Bin reads "Print" and prints that window instead: the Markers window's
  current view as a picture ("The system prints the current view of marker information"), the Console's log as text
  ("the marker information displayed in the Console window").
* **The Media Tool** (p.345: "You can print Media Tool data by using the same procedures for printing bins"): with the
  Media Tool last used, File > Print Bin prints its table as a bin's Text view prints.

Without a bin, a loaded clip or a sequence the command beeps. A printer that cannot start (none installed) beeps and
says so in the Console.

## What goes on paper (ours; the guide shows no printout)

Pages are laid out at 96 "page pixels" an inch, so text keeps its point size on any printer, and painted at the
printer's resolution (`PrintDocument`, `printDocument`, `renderPages` for checks).

* **Text view:** the bin's name, then a table of the columns shown, in the order shown, with the rows sorted and sifted
  as shown. The heading row repeats at the top of every page. The icon column is left out.
* **Frame view:** the bin's name, then the frames (1.67 x 0.94 inches) with their names, four to a row, in the order
  they read in the bin: rows top to bottom, each left to right. So a storyboard prints as it is arranged. Each frame is
  the one chosen to identify the clip (p.251).
* **Script view:** the bin's name, then a row an item: its frame, its name and the text of its script box.
* Offline items print a grey frame.
* Text is 9-point sans serif, the bin's name 12-point bold. The Console's log is 9-point monospace.

## Tests

`tst_printing`: pages and the picture fit; Text, Frame and Script views' contents; Page Setup's layout used by the next
prints; Print Frame and Print Timeline to PDF through `MainWindow::setPrintDialogHook`; nothing to print; a cancelled
Print dialog; Print following the Console, the Markers window and the Media Tool. `LUMENIS_SHOT_DIR` saves the bin pages as pictures.
