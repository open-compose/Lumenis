#pragma once

// Accessible names for screen readers (ROADMAP 10.3; docs/spec/accessibility.md). Qt describes every widget to
// VoiceOver, Narrator, NVDA and Orca through QAccessibleInterface, and a button's name is its text. Most of our
// buttons show only an icon, with what they do in the tooltip, so a reader would announce just "button".
//
// installAccessibleNames() fills in the name of every control that has none of its own, from what a sighted user sees:
// a button's text, else its tooltip (without rich text), else a line edit's placeholder. It follows the tooltip when it
// changes. A name set with setAccessibleName() is left as it is. MainWindow installs it once for the application.

#include <QColor>
#include <QPalette>
#include <QString>

class QApplication;
class QWidget;

namespace lumenis::ui {

void installAccessibleNames(QApplication& app);
// The name installAccessibleNames() would give `w` now (empty when it has nothing to go by); for tests.
QString derivedAccessibleName(const QWidget* w);
// The colour of the keyboard focus ring on the widgets we paint (10.3c): the highlight, lightened so it keeps 3:1
// against the window and field backgrounds (WCAG 2's non-text contrast, 10.3d).
QColor focusRingColor(const QPalette& palette);
// WCAG 2's contrast ratio of two colours, 1 to 21 (relative luminance of sRGB).
double contrastRatio(const QColor& a, const QColor& b);

} // namespace lumenis::ui
