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
  - Guide URL: https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_Editing_Guide.pdf
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
- Push only after a local build and test pass, then check CI. CI covers every
  platform and checks:
  - the installers (install, launch, uninstall);
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
entered in `docs/spec/ui-inventory.md`, with page citations. Next is Phase 1.2:
the Select Project window and the paneled main layout (Bin Container,
Composer, Timeline, workspace toolbar).

To read the guide, download it into the scratchpad and run `pdftotext`. To see
a page's layout, render it with `pdftoppm -r 80 -f N -l N`.
