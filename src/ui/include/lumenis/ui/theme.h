#pragma once

class QApplication;

namespace lumenis::ui {

// Applies the application's look: the Fusion style, the bundled Inter font
// and a dark palette. Using the same style and font everywhere is what makes
// windows render the same on macOS, Windows and Linux (Phase 1 gate 1);
// native pieces such as the macOS menu bar and file dialogs stay native.
void applyTheme(QApplication& app);

// Family name of the bundled UI font ("Inter"), or empty if it failed to load.
const char* uiFontFamily();

} // namespace lumenis::ui
