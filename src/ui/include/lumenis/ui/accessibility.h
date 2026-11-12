#pragma once

// Accessible names for screen readers (ROADMAP 10.3; docs/spec/accessibility.md). Qt describes every widget to
// VoiceOver, Narrator, NVDA and Orca through QAccessibleInterface, and a button's name is its text. Most of our
// buttons show only an icon, with what they do in the tooltip, so a reader would announce just "button".
//
// installAccessibleNames() fills in the name of every control that has none of its own, from what a sighted user sees:
// a button's text, else its tooltip (without rich text), else a line edit's placeholder. It follows the tooltip when it
// changes. A name set with setAccessibleName() is left as it is. MainWindow installs it once for the application.

#include <QString>

class QApplication;
class QWidget;

namespace lumenis::ui {

void installAccessibleNames(QApplication& app);
// The name installAccessibleNames() would give `w` now (empty when it has nothing to go by); for tests.
QString derivedAccessibleName(const QWidget* w);

} // namespace lumenis::ui
