#pragma once

#include "lumenis/model/bin.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

// Scripts for script-based editing (Editing Guide ch.18, p.566-590; docs/spec/scriptsync.md, 8.3): "You begin the script
// integration process by importing a script into a project. The script appears in a script bin, and opens in a Script
// window" (p.570). A script is lines of text (the imported file's rows) with scene and page numbers starting at lines.
// Ours: the script is kept in its bin item (BinItemKind::Script) under kScriptKey, so that it is saved, auto-saved and
// kept in the attic with its bin (p.570: "The Script window has the same auto-save functionality as bins ... saves
// Script window files in the project folder along with bins, and stores backup copies automatically in the Avid Attic
// folder"); Avid's .avc file is undocumented. Slates, takes and script marks come in 8.3b-8.3c.
namespace lumenis::model {

inline constexpr char kScriptKey[] = "script";

// The Script Text Layout (p.571-572) and the font a script was made with (the Script setting, p.1217).
struct ScriptLayout {
    int leftMargin = 40;  // pixels; "The default is 40 pixels" (p.1217)
    int textWidth = 600;  // pixels, used with wordWrap (ours: the guide gives no default)
    bool wordWrap = true; // ours: on
    QString font;         // empty: the application's font
    int fontSize = 12;    // "The default is 12 points" (p.1217)
    bool operator==(const ScriptLayout&) const = default;
};

struct Script {
    QStringList lines;
    // "Each scene or page number continues throughout the script until you mark another line as the beginning of a
    // new scene or page" (p.574): the number by the line it starts at.
    QMap<int, QString> scenes;
    QMap<int, QString> pages;
    ScriptLayout layout;

    // The scene or page a line is in; empty before the first.
    QString sceneAt(int line) const;
    QString pageAt(int line) const;
    bool operator==(const Script&) const = default;

    QJsonObject toJson() const;
    static Script fromJson(const QJsonObject& o);
};

// "The imported script must be in text format ... export the script from your word processor by using the 'Text Only
// with Line Breaks' option" (p.571): a line for each row of the text, line ends of any platform, tabs as 8 columns
// (ours).
Script scriptFromText(const QString& text, const ScriptLayout& layout = {});

// A script item and the script it holds.
bool isScript(const BinItem& item);
std::optional<Script> scriptOf(const BinItem& item);
void setScript(BinItem& item, const Script& script);
// A new script item named `name`.
BinItem scriptItem(const QString& name, const Script& script);

// Add Scene and Add Page (p.574): `number` begins at `line` (replacing any there); an empty number removes it.
void setScene(Script& script, int line, const QString& number);
void setPage(Script& script, int line, const QString& number);
// The Delete dialog's "Delete scene(s)" and "Delete page break(s)" (p.574) over lines [from, to]; how many went.
int deleteScenes(Script& script, int from, int to);
int deletePages(Script& script, int from, int to);

// Editing the text (p.573): lines [from, to) become `replacement` (rows split at line breaks). Scene and page numbers on
// the replaced lines move to the first new line (or are dropped with no lines left); later ones move with their lines.
void replaceLines(Script& script, int from, int to, const QStringList& replacement);

// Find and Replace in the Script window (p.575-576).
struct ScriptMatch {
    int line = 0;
    int column = 0;
    int length = 0;
};
// The next match of `text` after (or with `backward` before) line `line`, column `column`, going round; nothing without
// one.
std::optional<ScriptMatch> findInScript(const Script& script, const QString& text, int line, int column, bool backward = false,
                                        bool ignoreCase = true);
// "replace all instances of the text": how many were replaced.
int replaceInScript(Script& script, const QString& text, const QString& replacement, bool ignoreCase = true);

} // namespace lumenis::model
