#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QString>

class QMenuBar;

namespace lumenis::ui {

class CommandRegistry;

// Builds the menu bar from a menus.json document:
//   {"menus": [{"id": "file", "title": "&File",
//               "items": ["file.exit", "-", {"id": "...", "title": "...", "items": [...]}]}]}
// A string item is a command id, "-" is a separator, an object is a submenu.
// Returns false and sets *error for malformed input or unknown command ids.
bool buildMenus(QMenuBar* bar, CommandRegistry& commands, const QByteArray& json, QString* error = nullptr);

// Builds from the application's built-in menus.json.
void buildBuiltinMenus(QMenuBar* bar, CommandRegistry& commands);

// Menu items made at run time (the workspace names in Windows > Workspaces)
// carry this property; dumpMenus() leaves them out, because the inventory
// lists only fixed items.
inline constexpr char kDynamicMenuItem[] = "lumenisDynamic";

// Describes the menu bar as it actually exists, for the inventory test:
//   [{"text": "File", "items": [{"text": "Exit", "shortcut": "Ctrl+Q",
//     "enabled": true}, {"separator": true}, {"text": "...", "items": [...]}]}]
// Mnemonic markers are removed and shortcuts use portable text, so the
// result is the same on every platform.
QJsonArray dumpMenus(const QMenuBar* bar);

} // namespace lumenis::ui
