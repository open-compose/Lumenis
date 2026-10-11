# Cutthulhu

*For the editor whose timeline has become an eldritch horror.*

A clean-room, cross-platform professional non-linear video editor that works
like Avid Media Composer. It is meant to be familiar to experienced editors
and to open and save their projects, bins and media. It runs on macOS (Apple
Silicon), Windows x64 and Linux x64. It is written in C++20 with Qt 6.

**Status:** Phase 0 is done: the foundations, build, CI and installers. See the
[roadmap](docs/ROADMAP.md) for the phased plan and its pass/fail gates.

* [Building](BUILDING.md): compiling on each platform.
* [Installing](INSTALL.md): installers and modules.
* [Extensions](docs/EXTENSIONS.md): codecs, plug-ins, licensed technology and
  hooks.
* [Roadmap](docs/ROADMAP.md): scope, clean-room rules and milestones.

The source code uses the internal code name `lumenis` for CMake targets, C++
namespaces and the module ABI. Users never see it.

## Clean-room policy

This project contains no Avid source code, decompiled code or leaked material.
Behaviour and file formats are implemented from public documentation, open
standards, and black-box observation of files. See ROADMAP §3.

Avid and Media Composer are trademarks of Avid Technology, Inc. This project
is not affiliated with or endorsed by Avid.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
