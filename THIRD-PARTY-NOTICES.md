# Third-party components

The application includes or links the following components. Their licences
are compatible with this project's GPL-3.0-or-later licence and allow
commercial distribution.

| Component | Version | Licence | Use |
|---|---|---|---|
| [Qt](https://www.qt.io) | 6.8 (6.4+ on Linux distributions) | LGPL-3.0, linked dynamically | GUI toolkit |
| [Inter](https://github.com/rsms/inter) | 4.1 | SIL Open Font License 1.1 (`src/ui/resources/fonts/Inter-LICENSE.txt`) | Interface font, embedded as a resource |

Qt's source is available from https://download.qt.io. Because Qt is linked
dynamically, users can replace the Qt libraries that ship with the
application with their own build.

When a component is added, also add it to this table and install its licence
text (see `cmake/Packaging.cmake`).
