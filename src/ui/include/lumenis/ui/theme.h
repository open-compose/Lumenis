#pragma once

#include <QIcon>

class QApplication;

namespace lumenis::ui {

// Control sizes that do not depend on font metrics. Each platform's font
// engine (FreeType, Core Text, DirectWrite) measures the same font slightly
// differently; sizing controls from those measurements made windows lay
// out differently per platform (Phase 1 gate 1).
inline constexpr int kControlHeight = 23;   // buttons, combo boxes, line edits, spin boxes
inline constexpr int kToolButtonHeight = 22;
inline constexpr int kCheckBoxHeight = 21;
inline constexpr int kHeaderHeight = 20;
inline constexpr int kRowHeight = 18;       // item views

// Small interface icons drawn in code, so they look identical everywhere
// (Unicode symbols fall back to different fonts on each platform).
enum class GlyphIcon { ListView, FrameView, FastMenu };
QIcon glyphIcon(GlyphIcon which);

// Applies the application's look: the Fusion style, the bundled Inter font
// and a dark palette. Using the same style and font everywhere is what makes
// windows render the same on macOS, Windows and Linux (Phase 1 gate 1);
// native pieces such as the macOS menu bar and file dialogs stay native.
void applyTheme(QApplication& app);

// Family name of the bundled UI font ("Inter"), or empty if it failed to load.
const char* uiFontFamily();

} // namespace lumenis::ui
