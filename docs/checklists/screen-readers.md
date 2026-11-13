# Screen readers: the owner's hands-on check (10.3d)

`tst_accessibility` checks what Cutthulhu tells a screen reader. This checklist checks what the readers actually say,
which only a person can hear. Run it once with each reader (docs/spec/accessibility.md has what each part should be).
Copy this file, fill it in and send it back, or commit it as `screen-readers-<platform>.md`.

**Before you start**
* Install the latest build (the builds folder in Documents, BUILDING.md).
* Start the screen reader before Cutthulhu:
  * **macOS, VoiceOver.** Cmd+F5. VO is Ctrl+Option. VO+Right moves to the next item; VO+Shift+Down goes into a group,
    VO+Shift+Up comes out of it.
  * **Windows, Narrator.** Ctrl+Win+Enter. Caps Lock is the Narrator key; Narrator+Right moves to the next item. Scan
    mode (Narrator+Space) must be off inside Cutthulhu.
  * **Windows, NVDA** (free, nvaccess.org). Insert is the NVDA key; use Tab and the arrow keys, and NVDA+Num 5 (the
    object navigation keys, NVDA+Num 4/6/8/2) for the painted widgets. Browse mode should stay off by itself.
  * **Linux, Orca.** Super+Alt+S under GNOME. Orca needs the session's accessibility bus: if Orca says nothing in
    Cutthulhu, start it with `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 cutthulhu` and note it below. Under X11 and Wayland
    alike.
* Make a new project with one bin, link or import two short clips, and edit them one after another into a sequence
  (the sample project from `cutthulhu --sample-project` will do).
* For each row: do it with the keyboard only, then tick **Heard** if the reader says what the **Expected** column
  says (the words can differ a little: each reader phrases roles its own way). In **Said**, write what it said if it
  was different, or nothing.

Tester: ______  Date: ______  Platform and version: ______  Reader and version: ______  Build: ______

## The main window

| # | Do | Expected | Heard | Said |
|---|---|---|---|---|
| 1 | Start Cutthulhu and open the project | The window title with the project's name | ☐ | |
| 2 | Ctrl+Down until the bin is active | Its name ("Bin ..."), then the first control in it | ☐ | |
| 3 | In the bin's list, Down through the clips | Each clip's name and its columns | ☐ | |
| 4 | Ctrl+Down to the Composer | "Composer", then a control in it; a visible ring around the focused control | ☐ | |
| 5 | Move to a Composer button that shows only an icon (Mark IN) | "Mark IN, button" (its tooltip), not just "button" | ☐ | |
| 6 | Move to the Record monitor's picture (VoiceOver: VO+arrows; NVDA: object navigation) | "Record monitor", graphic, with "Cut 1, frame N of M" or "Empty" | ☐ | |
| 7 | Move to the Record monitor's position bar | "Record monitor position", slider, with the frame | ☐ | |
| 8 | Change the slider with the reader's increase/decrease (VO+Up/Down; NVDA and Narrator: arrow keys) | The new value; the monitor shows the new frame | ☐ | |

## The Timeline

| # | Do | Expected | Heard | Said |
|---|---|---|---|---|
| 9 | Ctrl+Down to the Timeline | "Timeline", with its number of tracks and length | ☐ | |
| 10 | Go into it (VoiceOver: VO+Shift+Down; NVDA: NVDA+Num 2) | "Track V1, N segments", list | ☐ | |
| 11 | Go into V1 and move through its segments | "<clip name>, starts 00:00:00:00, length ...", then the next one; a filler is "Filler, ..." | ☐ | |
| 12 | Make an edit (Ctrl+Z, then Shift+Ctrl+Z) and read V1 again | The segments as they are now, no stale or missing one | ☐ | |
| 13 | Move the position with Home and End, then read the position bar | The new frame | ☐ | |

## Commands, settings and windows

| # | Do | Expected | Heard | Said |
|---|---|---|---|---|
| 14 | Ctrl+Shift+P (Help > Find Command...) and type "audio mixer" | The search field, then the first command found and its menu place | ☐ | |
| 15 | Down, then Enter | Each command as it is chosen; Enter opens the Audio Mixer | ☐ | |
| 16 | Tab through the Audio Mixer | Each track's fader and level, pan and buttons by name | ☐ | |
| 17 | File > Settings, open Interface, Tab through it | Each option with its label: check boxes, menus, numbers | ☐ | |
| 18 | Open the Effect Palette (Ctrl+8), choose an effect and move through the list | "Effect Palette" lists and each effect's name | ☐ | |
| 19 | Open a dialog that asks something (File > Close Project with changes, or delete a clip) | Its title, its message, its buttons | ☐ | |
| 20 | Make something fail (export to a folder you may not write to) | The warning's text is read (it also goes in the Console) | ☐ | |

## Seeing and reading

| # | Do | Expected | Heard | Said |
|---|---|---|---|---|
| 21 | Turn on the OS's larger text or zoom (macOS: Display zoom; Windows: Text size 150%; GNOME: Large Text) | Cutthulhu is usable; note what is cut off or too small | ☐ | |
| 22 | File > Settings > Interface: override the bin and Sidebar font sizes with 24 | Taller bin and Sidebar rows; no text is cut | ☐ | |
| 23 | Turn on the OS's high contrast (Windows: Contrast themes; macOS: Increase contrast) | Note what cannot be read | ☐ | |

## Anything else

Write here anything the reader says that is confusing, anything it cannot reach, and any key that does something
unexpected while the reader is on (some readers take keys such as Caps Lock, Insert or Ctrl+Option for themselves).

____________________________________________________________________________________________________________________
