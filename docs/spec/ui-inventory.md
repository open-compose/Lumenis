# User-interface inventory

This is the specification for Phase 1: every window, menu, command and default
keyboard shortcut a Media Composer 2024/2025 user expects.

**Sources.** Both are publicly available from Avid.

| Short name | Document | URL |
|---|---|---|
| Editing Guide (EG) | Media Composer Editing Guide, version 2025.12 (1,317 pages) | `https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_Editing_Guide.pdf` |
| FX Guide (FX) | Media Composer Effects Guide, version 2025.12 (762 pages) | `https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_FX_Guide.pdf` |

A plain page number ("p.61") refers to the Editing Guide. FX Guide pages are
written "FX p.303".

**Clean-room rules (ROADMAP §3).** Entries record names, behaviour and layout
only. Avid's artwork, icons and prose are never copied into this project, and
the guide itself is never committed.

**What the guide does and doesn't settle.** The guide names commands, windows,
fields and some shortcuts. It does not show complete menus in order, nor the
full default keyboard. Those parts are marked *unverified*. They will be
confirmed against a licensed Media Composer (screenshots from the project's
test contact) and then locked by the tests.

| Part | File | Test |
|---|---|---|
| Menu bar | `tests/data/menu_inventory.json` | `tst_menu_inventory` |
| Default keyboard | `tests/data/keyboard_inventory.json` | `tst_keyboard` |
| Window appearance | CI screenshots | `ui-consistency` CI job |

---

## 1. Starting, quitting and project files (ch. 2–3)

* **Start (p.57–58).** The application starts with the **Select Project**
  window.
* **Quit (p.59).**
  * macOS: *Media Composer > Quit*. Windows: *File > Exit*.
  * *File > Close Project* returns to Select Project. It is also
    **Ctrl+Shift+W** (p.69).
  * In Select Project, **Quit** asks *Leave* / *Cancel*.
* **Project files (p.67–68).** A project is a folder named after the project
  in an *Avid Projects* folder. It contains:
  * a project file (`.avp`);
  * a project settings file (`.xml`);
  * a bin file (`.avb`).

  More `.avb` files are added as bins are created.
* **Default project locations (p.67).**

| | Private | Shared |
|---|---|---|
| Windows | `Users\<user>\Documents\Avid Projects` | `Users\Public\Documents\Shared Avid Projects` |
| macOS | `/Users/<login>/Documents/Avid Projects` | `/Users/Shared/Avid Media Composer/Shared Avid Projects` |

  Cutthulhu uses equivalent locations, under its own names, on each platform.
  Projects in the Avid locations can be opened with **Browse**.
* **User profiles (p.68).** A user folder in *Avid Users* contains a profile
  file (`.ave`) and a user settings file (`.xml`).
* **Naming limits (p.63).**
  * Project names have at most 56 characters; bin names at most 64.
  * For cross-platform names, avoid `/ \ : * ? " < > |` and leading or
    trailing spaces or periods.
* **Delete a project (p.69).** Select it in Select Project and press
  **Delete**, then confirm. This deletes the project and its bins, but not
  its media.
* **Rename (p.70).** Renaming is not possible inside the application; it is
  done by renaming files on disk.
* **Auto-open (p.69).** *Interface settings > "Automatically launch last
  project at startup"*.
* **Avid Attic (p.71–73).** Each save keeps a numbered backup copy of every
  bin (`Bin.1`, `Bin.2`, …) per project. The oldest copy is overwritten only
  if the second-oldest is more than 2 hours old.

## 2. Select Project window (p.61–67)

The layout is taken from the screenshots on p.61–64; behaviour from the text.

* **Left column:** *Select Project* and *What's New* buttons (What's New lists
  features by release).
* **Header:** "Welcome to …" and the user profile name.
* **Two tabs:** **Projects** | **New Project**.
* **User Profile** drop-down, and **Location** drop-down with a folder
  (browse) button. Locations: Private, Shared, External (a browsed folder,
  remembered).
* **Projects tab.**
  * The project list has two views, *text/column view* and *frame view*,
    switched by buttons at the top right, with a size slider.
  * Columns: **Name, Frame** (poster frame), **Project Type, Frame Rate,
    Color Space, Raster Dimension, Stereoscopic, Creation Date, Modified
    Date**.
  * The fast menu changes the font and the columns shown.
* **Poster frame:** set from the Composer monitor's context menu with "Save as
  project poster frame".
* **Buttons:** **Quit** and **Open**. Double-clicking a project opens it.
* **New Project tab.**
  * **Project name** field, then **Settings** (expandable).
  * **Choose for Me** check box: no raster or edit rate is fixed until the
    first clip is added, then a *Project Properties* dialog appears with
    *Raster* and *Edit Rate* (p.64).
  * **Format** preset: a combination of video format, frame size, colour
    space, aspect ratio and frame rate. **Custom** raster: even values from
    256×120 to 8192×8192. Stereoscopic rasters need width divisible by 8 and
    height divisible by 4. Custom presets have **Save Preset** and **Manage
    Presets**, and appear as *My Presets* (p.65).
  * **Raster Dimension, Aspect Ratio** (SD 4:3/16:9, HD 16:9, high-res
    computed) and **Frame Rate** (custom: 23.98 to 60 fps).
  * **Color Space:** SD uses YCC; HD uses YCC 709 or RGB 709; high-res has
    several options.
  * **Color Depth.**
  * **Stereoscopic:** Off, Leading Eye, Left Eye Only, Right Eye Only, Side by
    Side, Over/Under, Full.
  * **Film:** for 23.976p, 24p, 25p, 720p and 1080p, with a film gauge type.
  * **OK** creates the project folder and files and selects the new project
    (p.67).
* **Opening a project (p.68)** shows the **Bin Container**, the **Composer**
  and the **Timeline**, with the user's settings.
* **"Choose for Me" projects (p.64)** get two bins, a green source bin and a
  blue record bin, and a new sequence. Empty bins and the empty Timeline show
  hints ("Drag … to add clips", "Drag clips here to start editing").

**Implementation status (1.2a).** All of the above is built, except:
* **Save Preset / Manage Presets:** not yet.
* **Poster frames:** shown as placeholders until the Composer exists.
* **The project list's fast menu:** offers column choice; its font option is
  not yet built.
* **Colour-space and colour-depth labels:** ours. The guide names the
  families (709, 2020, DCI-P3, HDR HLG/PQ, ACES; p.520–558) but not the menu
  text.
* **Film types:** 16mm, 35mm 3-perf and 35mm 4-perf, from p.107.
* **Project files:** Cutthulhu's own `.cthproj` (JSON). Media Composer's
  `.avp` is Phase 3.

## 3. Panels, docking and workspaces (ch. 4)

* **Default layout (p.74).** Single monitor: Bin Container top left, Composer
  in the middle, Workspace toolbar on the right, Timeline at the bottom.
* **Window chrome (p.74).** Every window has a **vertical title bar** with a
  tab showing its name.
* **Docked, floating and tabbed (p.75–78).**
  * Drag a title bar to dock it. Valid targets show **green drop zones**, and
    a white area previews the result.
  * **Alt-drag** onto another title bar to tab windows together.
  * A tab group is moved by its grey area or grey box.
* **Keyboard (p.78).**
  * **Ctrl+Up/Down** (Cmd on macOS) cycles windows.
  * **Ctrl+Tab** cycles tabs.
* **Menus (p.78–79):** *Windows > Float All Panels* and *Windows > Float
  Active Panel*.
* **Tool palettes (p.79–80)** can be docked and saved in workspaces. Named
  palettes are listed in the Tools menu.
* **Interface settings (p.80–83).**
  * Brightness: dark or light.
  * Button highlight colour.
  * Timecode window brightness.
  * Timeline and viewer brightness.
  * Bin font overrides.
  * *Windows > Set Font* sets the font of the Bin, Composer, Script and
    Timeline windows.
* **Blank panel (p.83).** "Closing a docked tool will replace it with a blank
  panel"; also available from the Windows menu.
* **Multiple monitors (p.84).** A *Host Panels* submenu in Windows has one
  host panel per display.
* **Extensions (p.85).**
  * Third-party panels appear in the **Extensions** menu and can be docked
    like any window.
  * In Cutthulhu, this maps onto the module/provider system
    (`docs/EXTENSIONS.md`).
* **Workspaces (p.85–93).**
  * Defaults: **Edit, Effects, Color Correction, Audio**. Up to 12 workspace
    buttons. Workspaces are user settings.
  * *Windows > Workspaces*: New Workspace (optionally "Based on Workspace"),
    Delete Workspace, Restore to Saved, Restore Current to Default,
    Properties, Show Workspace Toolbar.
  * "When moving to a workspace", a setting chooses between loading the last
    known state and the last saved state. Another setting chooses what
    happens "when changing monitor configuration": prompt, use the same, or
    duplicate.
  * Some tools can't be part of a workspace: Hardware, Communication Ports and
    Media tool.
* **Workspace toolbar (p.87–89).**
  * Sits on the right edge.
  * Right-click shows icon and text, icon only, or hides the bar. When
    hidden, a workspace icon at the top right brings it back.
* **Linked settings (p.89).** Settings named like a workspace load with it.

## 4. Tools and the Command Palette (ch. 5)

* **Tools as tabs (p.94–95).** Tools open in their own windows. They can be
  dragged together as tabs, with previous/next buttons, a tab menu and close
  buttons. Tools cannot be tabbed into bin windows.
* **Command Palette (p.96–100).**
  * Tabs: **Move, Play, Edit, Trim, FX, 3D, CC, MCam, Other, More, Smart
    Tool**.
  * Modes at the bottom: **Button to Button Reassignment**, **Menu to Button
    Reassignment** and **Active Palette**.
  * **Quick Find** field.
  * *Other* tab: a **Blank** button, plus modifier buttons (Add Alt Key on
    Windows; Add Control Key and Add Option Key on macOS).
  * Right-click a button and choose *What's This?* for help.
* **Where mappings are stored (p.97):**
  * Keyboard palette → Keyboard settings;
  * Composer tool palette → Composer settings;
  * trim-mode palettes → Trim settings;
  * Tool palette → Interface settings.
* **Menu to Button.** A mapped menu command shows its initials on the button,
  for example *Composer > Load Filler* shows "LF" (p.98–99). Bin fast menu
  commands can also be mapped to keys.

## 5. Menus

The full inventory, with a page for every item, is
`tests/data/menu_inventory.json`. In summary:

* **Menu bar order (p.74 screenshot):** File, Edit, Bin, Clip, Timeline,
  Composer, Tools, Windows, Script, Help.
* **Extensions** (p.85): its position is unverified.
* **Avid Link** (p.59) is excluded, because it is Avid's licensing service.
* **macOS** also has the application menu, which holds Quit and About.
* **Item order within each menu, and separators:** unverified.
* **Additions not in Media Composer:** *Help > About Qt* (Qt's LGPL notice).
* **Renamed:** *Tools > Avid Titler+* (FX p.303) is called *Titler+*, because
  "Avid" is a trademark.
* **Where the two guides differ, the Editing Guide's spelling is used:**
  * "Recreate Title Media" (p.475), not "Re-create" (FX p.374);
  * "Tools > Background Queue Window" (p.358), not "Background Queue"
    (FX p.59).
* **Contextual items, not part of the static menus:**
  * While the legacy Title Tool is open, the File menu offers *Save Title*,
    *Save Title As* and *Export Title* (FX p.358–364).
  * The Windows menu offers *Title Tool* (FX p.327).
  * The Edit menu's Undo item names the last action, for example *Undo Clear
    Renders* (FX p.61).

## 6. Documented shortcuts

Only shortcuts the guide states are listed. Ctrl is Command on macOS. The full
default keyboard is still to come (§7).

| Shortcut | Command | Page |
|---|---|---|
| Ctrl+Shift+W | File > Close Project | 69 |
| Ctrl+Z | Edit > Undo | 466 |
| Ctrl+R | Edit > Redo | 466 |
| Ctrl+C / Ctrl+V | Edit > Copy / Paste | 423 |
| Ctrl+A | Edit > Select All | FX p.153 |
| Ctrl+F | Edit > Find | 417 |
| Ctrl+G | Find next occurrence | 429 |
| Ctrl+L / Ctrl+K | Edit > Enlarge / Reduce (Frame in bins, Track in the Timeline) | 250, 594 |
| Ctrl+I | Get Info (monitor or bin item) | 388 |
| Ctrl+Shift+F | Composer > Full Screen Playback | 391 |
| Ctrl+9 | Tools > Project | 243 |
| Ctrl+Up / Ctrl+Down | Cycle windows | 78 |
| Ctrl+Tab | Cycle tabs | 78 |
| Ctrl+. (period) | Cancel waveform redraw or render | 678, 805 |
| Ctrl+X | Cut (segment, script text) | 573, 622 |

## 7. Default editing keyboard

**Machine-readable form:** `tests/data/keyboard_inventory.json`, enforced by
`tst_keyboard`.

**What the guides state.** Every key below is documented in text, US
English layout:

| Keys | Command | Page |
|---|---|---|
| \`, Tab, 5, Space | Play (start/stop) | p.400 |
| 1 / 2 | Step Backward / Forward 10 frames (8 frames in progressive formats) | p.400 |
| 3 / 4, Left / Right | Step Backward / Forward one frame | p.400 |
| Home / End | Go to start / end of clip or sequence | p.400 |
| J / K / L | Play Reverse / Pause / Play Forward | p.400–401 |
| N / ; | Mouse jog / mouse shuttle | p.403 |
| I / O | Mark IN / Mark OUT | p.405–406 |
| Alt+I | Go to IN Point | p.97 |
| V / B | Splice-in / Overwrite | p.624 |

**Shuttle and contextual keys (p.400–401, 117–195, 1132):**
* Pressing L repeatedly plays at 2×, 3×, 5× and 8×.
* K+L and K+J give slow forward and backward.
* Holding K and tapping J or L steps one frame.
* Alt+J and Alt+L change speed one step at a time.
* In the Capture tool: F4, F1/F2, Esc and Tab.
* In MultiCamera: keypad 1–9 choose camera angles.

**Not yet confirmed.** The Keyboard settings illustration (p.1202) shows the
whole default layout, but as icons only. Keys not listed above stay unmapped
until their functions are confirmed in text or on a licensed seat.

**Other keyboard settings (p.1202).** Media Composer also ships Keyboard
settings named "MC1 English", "NC English", "Premiere Pro" and "Resolve".

## 8. Still to document
* The window-by-window details for Bins (ch. 10), the Composer (ch. 13), the
  Timeline (ch. 19), the Audio tools (ch. 21) and Settings (ch. 33).
* Menu order and separators, confirmed against a licensed seat.
