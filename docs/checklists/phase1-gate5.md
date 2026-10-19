# Phase 1 gate 5: the editor's checklist

<!-- Made by scripts/make-checklist.py from the application's menu bar
     (--dump-menus --sample-project) and tests/data/menu_inventory.json.
     Do not edit by hand: run the script; ctest checklist_current checks it. -->

Gate 5 (ROADMAP §5): an experienced Media Composer editor finds every window
and menu command without help. Copy this file, fill it in, and send it back.

**How to run the session**
* Install the latest build (the builds folder in Documents, BUILDING.md).
* Start with a new user and a new project; nothing else is open.
* The editor gets no help, documentation or hints during the session.
* For each entry, the editor finds it the way they would in Media Composer.
  * Tick **Found** if they found it without help.
  * In **Where**, write where they looked first, if it was not where they
    found it.
  * In **Notes**, write anything that differs from Media Composer.
* Items marked *later* are in their menu but dimmed: their feature comes in
  a later phase. They still count: the editor must find them.
* The gate passes when every entry is found.

Editor: ______  Date: ______  Platform: ______  Build: ______

## Windows

| Window | Where it is | Guide | Found | Where | Notes |
|---|---|---|---|---|---|
| Select Project | At startup, or File > Close Project | EG p.61-67 | ☐ | | |
| Bin Container and its sidebar | The main window; Tools > Project (Ctrl+9) | EG p.239-243 | ☐ | | |
| Bin views: Text, Frame, Script | The buttons at the bottom of a bin; Bin View menu | EG p.244-256 | ☐ | | |
| Composer: Source and Record monitors | The main window; the Composer menu | EG p.378-430 | ☐ | | |
| Timeline: tracks, patching, smart tools | The main window; Tools > Timeline (Ctrl+0) | EG p.591-645 | ☐ | | |
| Trim mode | The Trim Mode button or a lasso around a cut | EG p.647-665 | ☐ | | |
| Settings window and its dialogs | File > Settings | EG p.1136-1224 | ☐ | | |
| Keyboard setting | File > Settings > User > Keyboard | EG p.1202 | ☐ | | |
| Workspaces: W1-W12, Workspace View | Windows > Workspaces; the workspace bar | EG p.86-91 | ☐ | | |
| Tool Palette | File > Settings > User > Tool Palette | EG p.392 | ☐ | | |
| Command Palette and button mapping | Tools > Command Palette (Ctrl+3) | EG p.96-100 | ☐ | | |
| Calculator | Tools > Calculator (Ctrl+2) | EG p.100 | ☐ | | |
| Console | Tools > Console (Ctrl+6) | EG p.101 | ☐ | | |
| Effect Palette | Tools > Effect Palette (Ctrl+8) | FX p.37-38 | ☐ | | |
| Effect Editor | Tools > Effect Editor | FX p.83-88 | ☐ | | |
| Audio Mixer | Tools > Audio Mixer | EG p.690-726 | ☐ | | |
| Audio Tool | Tools > Audio Tool (Ctrl+1) | EG p.157-159 | ☐ | | |
| Media Tool | Tools > Media Tool | EG p.344-348 | ☐ | | |
| About: Configuration, Usage, Hardware | Help > About | EG p.94, p.102-105 | ☐ | | |

## File menu

| Command | Shortcut | Guide | Status | Found | Where | Notes |
|---|---|---|---|---|---|---|
| New > New Script |  | p.571 | *later* | ☐ | | |
| New > Volume for Export |  | p.902 | *later* | ☐ | | |
| Open Bin |  | p.296 | works | ☐ | | |
| Open Selected Bins |  | p.295 | works | ☐ | | |
| Open Selected Bins In One Window |  | p.295 | works | ☐ | | |
| Close | Ctrl+W | p.571 | works | ☐ | | |
| Close Bin |  | p.296 | works | ☐ | | |
| Close Project | Ctrl+Shift+W | p.69 | works | ☐ | | |
| Save Bin |  | p.299 | works | ☐ | | |
| Save All |  | p.299 | works | ☐ | | |
| Save All Bins |  | p.428 | works | ☐ | | |
| Save Script |  | p.571 | *later* | ☐ | | |
| Save a Script Copy As |  | p.571 | *later* | ☐ | | |
| Get Position Info |  | p.101 | works | ☐ | | |
| Reveal File |  | p.360 | *later* | ☐ | | |
| Input > Source Browser |  | p.132 | works | ☐ | | |
| Input > Import Media |  | p.969 | works | ☐ | | |
| Input > Link |  | p.328 | *later* | ☐ | | |
| Input > Tape Capture | Ctrl+7 | p.115 | *later* | ☐ | | |
| Input > Import EDL |  | p.951 | *later* | ☐ | | |
| Output > Export to File |  | p.129 | works | ☐ | | |
| Output > Export Bin |  | p.130 | *later* | ☐ | | |
| Output > Export to Device > P2 |  | p.901 | *later* | ☐ | | |
| Output > Export to Device > XDCAM |  | p.883 | *later* | ☐ | | |
| Output > Digital Cut |  | p.912 | *later* | ☐ | | |
| Output > Insert Edit Export |  | p.506 | *later* | ☐ | | |
| Output > Send To > Make New |  | p.866 | *later* | ☐ | | |
| Output > Send To > Make New BXF Template |  | p.906 | *later* | ☐ | | |
| Media > Media Creation Settings | Ctrl+5 | p.137 | works | ☐ | | |
| Media > Load Media Database |  | p.104 | works | ☐ | | |
| Media > Refresh Media Directories |  | p.359 | works | ☐ | | |
| Media > Unmount |  | p.344 | *later* | ☐ | | |
| Interplay Transfer > Clear Status |  | p.1094 | *later* | ☐ | | |
| Page Setup |  | p.292 | *later* | ☐ | | |
| Print Bin | Ctrl+P | p.288 | *later* | ☐ | | |
| Print Frame |  | p.292 | *later* | ☐ | | |
| Print Timeline |  | p.645 | *later* | ☐ | | |
| Settings |  | p.1136 | works | ☐ | | |
| Open Settings File |  | p.1141 | *later* | ☐ | | |
| Exit | Ctrl+Q | p.59 | works | ☐ | | |
| Export Matte PICT |  | p.418 | *later* | ☐ | | |

## Edit menu

| Command | Shortcut | Guide | Status | Found | Where | Notes |
|---|---|---|---|---|---|---|
| Undo | Ctrl+Z | p.466 | works | ☐ | | |
| Redo | Ctrl+R | p.466 | works | ☐ | | |
| Undo/Redo List |  | p.466 | works | ☐ | | |
| Copy | Ctrl+C | p.423 | works | ☐ | | |
| Paste | Ctrl+V | p.423 | works | ☐ | | |
| Delete |  | p.262 | works | ☐ | | |
| Duplicate | Ctrl+D | p.260 | works | ☐ | | |
| Select All | Ctrl+A | p.180 | works | ☐ | | |
| Select All Tracks |  | p.627 | works | ☐ | | |
| Find | Ctrl+F | p.417 | *later* | ☐ | | |
| Enlarge Frame | Ctrl+L | p.250 | works | ☐ | | |
| Reduce Frame | Ctrl+K | p.250 | works | ☐ | | |
| Enlarge HeadFrames |  | p.1016 | *later* | ☐ | | |
| Reduce HeadFrames |  | p.1016 | *later* | ☐ | | |
| Cut | Ctrl+X | p.355 | works | ☐ | | |
| Deselect All Keyframes |  | p.112 | *later* | ☐ | | |

## Bin menu

| Command | Shortcut | Guide | Status | Found | Where | Notes |
|---|---|---|---|---|---|---|
| Add Bin to Favorites |  | p.293 | works | ☐ | | |
| Align and Fill > Align to Grid | Ctrl+T | p.251 | works | ☐ | | |
| Align and Fill > Align Selected to Grid |  | p.251 | works | ☐ | | |
| Align and Fill > Align Columns |  | p.268 | works | ☐ | | |
| Align and Fill > Fill Sorted |  | p.251 | works | ☐ | | |
| Align and Fill > Fill Window |  | p.251 | works | ☐ | | |
| AutoSequence |  | p.500 | *later* | ☐ | | |
| Choose Columns |  | p.124 | works | ☐ | | |
| Hide Column |  | p.268 | works | ☐ | | |
| Go To Capture Mode |  | p.146 | *later* | ☐ | | |
| Select > Media Relatives |  | p.267 | *later* | ☐ | | |
| Select > Offline Items |  | p.266 | *later* | ☐ | | |
| Select > Reverse |  | p.260 | works | ☐ | | |
| Select > Sources |  | p.267 | *later* | ☐ | | |
| Select > Unreferenced Clips |  | p.267 | *later* | ☐ | | |
| Select > Unrendered Titles |  | p.359 | *later* | ☐ | | |
| Set Bin Display |  | p.288 | works | ☐ | | |
| Show Sidebar Only |  | p.243 | works | ☐ | | |
| Sift Bin Contents |  | p.288 | works | ☐ | | |
| Sift Selected Items |  | p.289 | works | ☐ | | |
| Show Sifted |  | p.289 | works | ☐ | | |
| Show Unsifted |  | p.289 | works | ☐ | | |
| Sort |  | p.247 | works | ☐ | | |
| Sort Again |  | p.247 | works | ☐ | | |
| Production Management > Check In Bin |  | p.1051 | *later* | ☐ | | |
| Production Management > Check In All Open Bins |  | p.1051 | *later* | ☐ | | |
| Production Management > Update Bin |  | p.1055 | *later* | ☐ | | |

## Clip menu

| Command | Shortcut | Guide | Status | Found | Where | Notes |
|---|---|---|---|---|---|---|
| Audio > Apply Gain |  | p.216 | *later* | ☐ | | |
| Audio > Center Pan |  | p.714 | *later* | ☐ | | |
| Audio > Change Sample Rate |  | p.731 | *later* | ☐ | | |
| Audio > Split All Tracks |  | p.736 | *later* | ☐ | | |
| Auto VO |  | p.754 | *later* | ☐ | | |
| AutoSequence |  | p.499 | *later* | ☐ | | |
| AutoSync |  | p.223 | *later* | ☐ | | |
| Batch Capture |  | p.179 | *later* | ☐ | | |
| Batch Import |  | p.1098 | *later* | ☐ | | |
| Batch Re-import |  | p.230 | *later* | ☐ | | |
| Consolidate/Transcode |  | p.350 | works | ☐ | | |
| Decompose |  | p.185 | *later* | ☐ | | |
| Extract DNxHD Data |  | p.975 | *later* | ☐ | | |
| Group Clips | Ctrl+Shift+G | p.1124 | *later* | ☐ | | |
| MultiGroup |  | p.1125 | *later* | ☐ | | |
| Edit Group |  | p.1125 | *later* | ☐ | | |
| Lock Bin Selection |  | p.265 | works | ☐ | | |
| Unlock Bin Selection |  | p.266 | works | ☐ | | |
| Lock Tracks |  | p.633 | works | ☐ | | |
| Unlock Tracks |  | p.634 | works | ☐ | | |
| Loop Selected Clips |  | p.393 | *later* | ☐ | | |
| Modify > Modify Clip |  | p.279 | *later* | ☐ | | |
| Modify > Modify Pulldown Phase |  | p.127 | *later* | ☐ | | |
| Modify > Unlink Media |  | p.210 | works | ☐ | | |
| Read Audio Timecode |  | p.668 | *later* | ☐ | | |
| Recreate Title Media |  | p.475 | *later* | ☐ | | |
| Refresh Sequence > Motion Adapters/Timewarps |  | p.545 | *later* | ☐ | | |
| Relink > Linked Media |  | p.364 | works | ☐ | | |
| Relink > Managed Media |  | p.361 | works | ☐ | | |
| Split All Tracks |  | p.637 | *later* | ☐ | | |
| New Title |  | p.327 | *later* | ☐ | | |

## Timeline menu

| Command | Shortcut | Guide | Status | Found | Where | Notes |
|---|---|---|---|---|---|---|
| New > Sequence |  | p.457 | works | ☐ | | |
| New > Video Track | Ctrl+Y | p.634 | works | ☐ | | |
| New > Audio Track > Mono |  | p.634 | works | ☐ | | |
| New > Audio Track > Stereo |  | p.634 | works | ☐ | | |
| New > Audio Track > 5.1 Surround |  | p.634 | works | ☐ | | |
| New > Audio Track > 7.1 Surround |  | p.634 | works | ☐ | | |
| New > Data Track |  | p.634 | works | ☐ | | |
| New > Title |  | p.343 | *later* | ☐ | | |
| Add Filler at Start |  | p.462 | *later* | ☐ | | |
| Find Black Hole |  | p.645 | *later* | ☐ | | |
| Find Flash Frame |  | p.645 | *later* | ☐ | | |
| Mixdown > Audio > To Sequence |  | p.732 | *later* | ☐ | | |
| Mixdown > Audio > Multiple Mixes |  | p.732 | *later* | ☐ | | |
| Mixdown > Data |  | p.982 | *later* | ☐ | | |
| Mixdown > Video |  | p.228 | *later* | ☐ | | |
| Remove Match Frame Edits |  | p.495 | works | ☐ | | |
| Render > ExpertRender In/Out |  | p.926 | *later* | ☐ | | |
| Render > Render In/Out |  | p.59 | *later* | ☐ | | |
| Render > Render at Position |  | p.58 | *later* | ☐ | | |
| Render > Clear Renders In/Out |  | p.60 | *later* | ☐ | | |
| Render > Clear Renders at Position |  | p.61 | *later* | ☐ | | |
| Restore Default Patch |  | p.630 | works | ☐ | | |
| Show Sequence Map |  | p.606 | *later* | ☐ | | |
| Split All Tracks |  | p.736 | *later* | ☐ | | |
| Create Unrendered Title Media |  | p.375 | *later* | ☐ | | |

## Composer menu

| Command | Shortcut | Guide | Status | Found | Where | Notes |
|---|---|---|---|---|---|---|
| Full Screen Playback | Ctrl+Shift+F | p.391 | works | ☐ | | |
| Load Filler |  | p.99 | *later* | ☐ | | |
| Remote Play |  | p.204 | *later* | ☐ | | |
| Show Single/Dual Monitor |  | p.381 | works | ☐ | | |
| Sync Point Overwrite |  | p.496 | *later* | ☐ | | |
| Freeze Frame > Other |  | p.562 | *later* | ☐ | | |
| Render On-the-Fly |  | p.370 | *later* | ☐ | | |

## Tools menu

| Command | Shortcut | Guide | Status | Found | Where | Notes |
|---|---|---|---|---|---|---|
| Audio EQ |  | p.736 | *later* | ☐ | | |
| Audio Mixer |  | p.686 | works | ☐ | | |
| Audio Punch-In |  | p.750 | *later* | ☐ | | |
| Audio Tool | Ctrl+1 | p.703 | works | ☐ | | |
| Audio Track Effect |  | p.791 | *later* | ☐ | | |
| AudioSuite |  | p.797 | *later* | ☐ | | |
| Background Queue Window |  | p.358 | works | ☐ | | |
| Background Services |  | p.376 | *later* | ☐ | | |
| Calculator | Ctrl+2 | p.100 | works | ☐ | | |
| Command Palette | Ctrl+3 | p.98 | works | ☐ | | |
| Console | Ctrl+6 | p.101 | works | ☐ | | |
| Dynamic Media Folders |  | p.372 | *later* | ☐ | | |
| Effect Palette | Ctrl+8 | p.791 | works | ☐ | | |
| IMF Window |  | p.335 | *later* | ☐ | | |
| List Tool |  | p.944 | *later* | ☐ | | |
| Markers |  | p.416 | works | ☐ | | |
| Media Tool |  | p.346 | works | ☐ | | |
| New Deck Controller |  | p.95 | *later* | ☐ | | |
| NRCS Tool |  | p.1006 | *later* | ☐ | | |
| Production Management Window |  | p.1079 | *later* | ☐ | | |
| Project | Ctrl+9 | p.243 | works | ☐ | | |
| Remote Assets |  | p.292 | *later* | ☐ | | |
| Restrictions |  | p.292 | *later* | ☐ | | |
| Source Browser |  | p.522 | works | ☐ | | |
| Timecode Window |  | p.389 | *later* | ☐ | | |
| Timeline Clip Notes |  | p.484 | *later* | ☐ | | |
| Video Input Tool |  | p.161 | *later* | ☐ | | |
| Video Output Tool |  | p.910 | *later* | ☐ | | |
| Titler+ |  | p.303 | *later* | ☐ | | |
| Color Info Tool |  | p.697 | *later* | ☐ | | |
| Effect Editor |  | p.54 | works | ☐ | | |
| Title Tool Application |  | p.343 | *later* | ☐ | | |
| Timeline | Ctrl+0 | p.766 | works | ☐ | | |

## Windows menu

| Command | Shortcut | Guide | Status | Found | Where | Notes |
|---|---|---|---|---|---|---|
| Float All Panels |  | p.78 | works | ☐ | | |
| Float Active Panel |  | p.79 | works | ☐ | | |
| Add Blank Panel |  | p.83 | works | ☐ | | |
| Workspaces > New Workspace |  | p.86 | works | ☐ | | |
| Workspaces > Delete Workspace |  | p.87 | works | ☐ | | |
| Workspaces > Restore to Saved |  | p.86 | works | ☐ | | |
| Workspaces > Restore Current to Default |  | p.86 | works | ☐ | | |
| Workspaces > Properties |  | p.93 | works | ☐ | | |
| Workspaces > Show Workspace Toolbar |  | p.89 | works | ☐ | | |
| Bin Layout > New Bin Layout |  | p.93 | *later* | ☐ | | |
| Bin Layout > Save Current |  | p.93 | *later* | ☐ | | |
| Bin Layout > Delete Bin Layout |  | p.93 | *later* | ☐ | | |
| Close All But Active |  | p.296 | *later* | ☐ | | |
| Close All Bins But Active |  | p.296 | works | ☐ | | |
| Center Current |  | p.601 | *later* | ☐ | | |
| Send Current Home | Ctrl+' | p.601 | *later* | ☐ | | |
| Set Font |  | p.82 | *later* | ☐ | | |
| Site Settings |  | p.1141 | works | ☐ | | |

## Script menu

| Command | Shortcut | Guide | Status | Found | Where | Notes |
|---|---|---|---|---|---|---|
| Add Scene |  | p.574 | *later* | ☐ | | |
| Add Page |  | p.574 | *later* | ☐ | | |
| Hold Slates Onscreen |  | p.578 | *later* | ☐ | | |
| Interpolate Position |  | p.577 | *later* | ☐ | | |
| Manage Transcripts |  | p.454 | *later* | ☐ | | |
| ScriptSync AI |  | p.584 | *later* | ☐ | | |
| Text Layout |  | p.571 | *later* | ☐ | | |

## Extensions menu

| Command | Shortcut | Guide | Status | Found | Where | Notes |
|---|---|---|---|---|---|---|

## Help menu

| Command | Shortcut | Guide | Status | Found | Where | Notes |
|---|---|---|---|---|---|---|
| About Cutthulhu |  |  | works | ☐ | | |
| Shortcuts |  | p.97 | *later* | ☐ | | |
| About Qt |  |  | works | ☐ | | |

## Summary

* 19 windows.
* 208 menu commands, 92 of them working with a project open; the
  rest come in later phases.
* Found: ____ of ____.
