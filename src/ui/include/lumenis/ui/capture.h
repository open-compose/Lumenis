#pragma once

#include <QJsonArray>
#include <QString>

class QDir;
class QWidget;

namespace lumenis::ui {

// Support for Phase 1 gate 1 (windows look the same on every platform).

// Every visible widget under `root`, depth first, with its geometry
// relative to root:
//   [{"path": "SelectProjectDialog/QStackedWidget[0]/QPushButton:openButton",
//     "x": 0, "y": 0, "w": 0, "h": 0}, ...]
// Paths use objectNames where set and class plus sibling index otherwise,
// so the same widget has the same path on every platform.
QJsonArray describeLayout(const QWidget* root);

// Saves three files for `widget` into `dir`:
//   <name>.png         as rendered
//   <name>.notext.png  with all text drawn transparent, so platform font
//                      rasterisers (FreeType, Core Text, DirectWrite) do not
//                      count as differences
//   <name>.layout.json describeLayout(widget)
// Returns false if a file could not be written.
bool capture(QWidget* widget, const QDir& dir, const QString& name);

} // namespace lumenis::ui
