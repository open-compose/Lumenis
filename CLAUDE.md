# Notes for Claude sessions working on this repository

Cutthulhu is a clean-room, cross-platform work-alike of Avid Media Composer
(C++20, Qt 6). Start with `docs/ROADMAP.md` (plan, decisions, phase gates) and
`docs/EXTENSIONS.md` (codecs, plug-ins, licensed technology, module hooks).

## Ground rules (from the project owner)

- **Clean room.** Never use decompiled, disassembled or leaked Avid code or
  restricted SDKs. Use only public Avid documentation, open standards and
  black-box observation (ROADMAP §3). Never copy Avid artwork or icons.
- **Spec before UI.** Menu, window and keyboard details come from the public
  Media Composer 2024/2025 Editing Guide, cited by page in
  `docs/spec/ui-inventory.md`. Never enter them from memory.
  - Editing Guide: https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_Editing_Guide.pdf
  - FX Guide (effects, titles, colour correction): https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_FX_Guide.pdf
  - Supported video file formats (Phase 2): https://resources.avid.com/SupportFiles/attach/Avid_Supported_Video_File_Formats.pdf (summarised in docs/spec/media-formats.md)
  - Keyboard shortcuts, 2009, third-party (Moviola). A secondary source; mark entries ks2009: https://resources.avid.com/supportfiles/attach/keyboard%20shortcuts.pdf
  - Download it into the scratchpad only. Never commit Avid documents.
- **Licensed technology.** Use an open-source implementation if a legal one
  exists. Otherwise use what the OS or a driver licenses. Otherwise add a
  provider hook so a licensed module can be added later.
- **Dependencies.** Only licences compatible with GPLv3 and commercial
  distribution. Add each new dependency to `THIRD-PARTY-NOTICES.md` and
  install its licence text.
- **Names.** The product name is **Cutthulhu**, set in
  `cmake/Branding.cmake`. The internal code name `lumenis` (targets,
  namespaces, module ABI) stays.
- **Support.** macOS arm64 (Xcode 15 and 26), Windows x64 (MSVC 2022) and
  Linux x64 (Ubuntu 24.04 system Qt 6.4). Release builds use Qt 6.8 LTS.

## Working practice

- Build and test with `cmake --preset ci && cmake --build --preset ci && ctest --preset ci`.
  The `ci` preset turns warnings into errors.
- Also build once with `-DCMAKE_CXX_COMPILER=clang++`, because CI's macOS
  jobs use Clang.
- Actions minutes are limited (private repository; macOS costs 10×). Pushes
  to `claude/**` run Linux and Windows only; macOS runs on `main`, pull
  requests and by hand. Run it by hand (`gh workflow run ci.yml --ref
  <branch>`) once at the end of each phase, and batch fixes into few pushes.
  The owner's M1 Mac runs the macOS job on every push as a self-hosted
  runner (`scripts/setup-mac-runner.sh`, BUILDING.md: hidden user
  `ghrunner`, launchd daemon, no window server, Qt in its home). The
  `macos-local` job runs while the repository variable `MAC_RUNNER` is
  `on`. The hosted macOS jobs (Xcode 15 and 26) still need the by-hand run
  at the end of each phase; `-f hosted_macos=false` skips them. While the
  repository variable `HOSTED_CI` is `off` (minutes used up), only the Mac
  runs: build and test Linux locally (ci preset, clang, GCC 15 check) with
  extra care, since nothing in CI covers Linux or Windows then.
- Push only after a local build and test pass, then check CI. CI covers every
  platform, also builds on the newest Ubuntu (26.04: GCC 15, Qt 6.10) with
  warnings as errors, and checks:
  - the installers (install, launch, uninstall), and that the AppImage
    opens natively on a headless Wayland compositor;
  - that PowerShell scripts parse;
  - cross-platform screenshot consistency (`ui-consistency`).
- Everything the user can invoke is a command declared in
  `src/ui/resources/commands.json` and placed in `menus.json`.
  `tests/data/menu_inventory.json` is the spec the menu bar is tested against.
- Qt 6.4's moc cannot parse GCC 13's C++20 headers when given compiler
  predefines. The top-level CMakeLists turns them off for Qt < 6.5.
- The owner tests on real Mac and Windows machines and reports problems.
  Fix the root cause, add a troubleshooting entry to `BUILDING.md`, and add a
  check to the scripts or CI so it can't recur silently.

## Current state

See the status lines in `docs/ROADMAP.md`. Phase 0 is done, except the manual
Linux check. Phase 1.1 (the UI framework) is done. The menu inventory and the window
specs for Select Project, panels, workspaces and the Command Palette are
entered in `docs/spec/ui-inventory.md`, with page citations. Phase 1.2a, the Select Project window (`src/ui/select_project_dialog.*`, `src/model/`), is
done. Phase 1.2b, the paneled main layout and workspaces (`src/ui/panels.*`,
`panel_manager.cpp`, `workspace*`, `main_window.*`), is done. Phase 1.3a,
the Bin Container (`src/model/bin.*`, `project_bins.*`, `src/ui/bin_*`), is
done. Phase 1.3b, the bin views, columns, sorting and sifting
(`bin_view.*`, `bin_dialogs.*`), is done. Phase 1.3c, bin docking
(`bin_dock.*`), the Project Bin Container and Favorite Bins, is done. Phase 1.4a, the Composer
(`src/model/monitor.*`, `src/ui/composer.*`), is done. Phase 1.4b, the
Timeline and editing (`src/model/sequence.*`, `src/ui/timeline.*`), is
done. Phase 1.4c, segment editing, locks and the Clipboard, is done.
Phase 1.4d, Trim mode and markers (`src/model/marker.*`,
`src/ui/marker_dialogs.*`), is done. Next is 1.5: palettes, tools and the
Settings dialogs (ROADMAP); spec section 11 lists editing details left
for later.

Panels are our own framework, not QDockWidget, because Qt cannot draw
vertical title bars, green drop zones or Alt-drag tabbing. Custom-painted
widgets draw text with palette colours (`WindowText`, `BrightText`) so the
text-free screenshots hide it.

The application icon is a ray-marched scene, `packaging/icon/scene.frag`.
`scripts/render-icon.js` renders it and `scripts/make-icons.py` makes every
platform's icon and the About box picture from the render; see `BUILDING.md`.

To read the guide, download it into the scratchpad and run `pdftotext`. To see
a page's layout, render it with `pdftoppm -r 80 -f N -l N`.
