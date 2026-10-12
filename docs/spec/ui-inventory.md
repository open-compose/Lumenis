# User-interface inventory

This is the specification for Phase 1: every window, menu, command and default
keyboard shortcut a Media Composer 2024/2025 user expects. Each entry cites
its source in the public **Media Composer Editing Guide**, or in another public
Avid document, by chapter and page. Following the clean-room rules
(ROADMAP §3), entries record behaviour and names only, never Avid artwork or
text beyond item names.

The machine-readable parts are enforced by tests:

| Part | File | Test |
|---|---|---|
| Menu bar | `tests/data/menu_inventory.json` | `tst_menu_inventory` |
| Default keyboard | `tests/data/keyboard_inventory.json` (to come) | `tst_keyboard_inventory` (to come) |
| Window appearance | CI screenshots | `ui-consistency` CI job |

**Status:** the source documents could not yet be read from the development
environment (its network policy blocks Avid's documentation site), so the
detailed entries below are still to be filled in. Nothing is entered from
memory: every item must cite the guide.

## Windows and dialogs

The window list comes from the roadmap. Each window gets its own section with
layout, controls, menus, context menus and keyboard behaviour, plus page
references.

| # | Window | Sub-step | Status |
|---|---|---|---|
| 1 | Select Project dialog (user profile, private/shared/external projects, New Project: format, frame rate, raster, colour space) | 1.2 | to document |
| 2 | Project window: Bins, Settings, Format, Usage, Info, Volumes | 1.2 | to document |
| 3 | Bin: Frame, Script, Text (column) views, bin map, bin containers/tabs | 1.3 | to document |
| 4 | Composer: source/record, single/dual monitor, button bars, info displays | 1.4 | to document |
| 5 | Timeline: tracks, patching, track selectors, smart tool, timeline view settings | 1.4 | to document |
| 6 | Tool Palette, Command Palette (button-to-button and menu-to-button mapping) | 1.5 | to document |
| 7 | Effect Palette, Effect Editor | 1.5 | to document |
| 8 | Audio Mixer, Audio Tool, Audio Track Effects | 1.5 | to document |
| 9 | Media Tool | 1.5 | to document |
| 10 | Settings dialogs (each Settings-tab entry) | 1.5 | to document |
| 11 | Keyboard settings, workspaces, window layouts | 1.6 | to document |

## Menus

To be entered from the Editing Guide, menu by menu, with page references, and
mirrored in `tests/data/menu_inventory.json`.

## Default keyboard

To be entered from the published default keyboard layout, key by key, and
mirrored in `tests/data/keyboard_inventory.json`.
