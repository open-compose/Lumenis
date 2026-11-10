#pragma once

#include "lumenis/model/classic_title.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

// Font replacement for classic titles (Editing Guide p.993-994; 8.7d1, docs/spec/title-tool.md "Font replacement"): "If
// a title opens and the system does not recognize the font, you see a dialog box that lets you substitute the font in
// the title for one that is installed on the system. This replacement creates a font mapping, which is saved as a Site
// setting." The title keeps what was replaced (ClassicTitle::fontReplacements) so Object > Font Replacement can redefine
// it.
namespace lumenis::model {

// Original font family -> the installed family that replaces it.
using FontMap = QMap<QString, QString>;

QJsonObject fontMapToJson(const FontMap& map);
FontMap fontMapFromJson(const QJsonObject& o);

// The font families the title's text objects and their formatted characters use, without the empty one (the system's),
// each once, in the order met.
QStringList titleFamilies(const ClassicTitle& title);

// The families `title` uses that are not in `installed` (compared without case), in the order met.
QStringList unknownFonts(const ClassicTitle& title, const QStringList& installed);

// Replaces `original` with `replacement` in the title: the objects and characters set in `original`, and, when the title
// replaced `original` before, those set in its earlier replacement ("The system updates the font in the open title",
// p.994). Records the replacement in the title's fontReplacements. False when nothing changes.
bool replaceFont(ClassicTitle& title, const QString& original, const QString& replacement);

// The Site setting's mapping on every unknown font it knows ("If you have the same font on both systems, the equivalent
// font appears automatically", p.993); the unknown fonts left, in the order met.
QStringList applyFontMap(ClassicTitle& title, const FontMap& map, const QStringList& installed);

} // namespace lumenis::model
