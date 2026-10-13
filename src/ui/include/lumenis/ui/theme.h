#pragma once

#include <QColor>
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
inline constexpr int kScrollBarExtent = 14;

// Panel chrome (guide p.74-77). The colours are ours; the arrangement
// follows the guide's pictures.
inline constexpr int kTitleBarWidth = 20;      // vertical title bar
inline constexpr int kSplitterHandle = 3;      // gap between docked panels
inline constexpr int kWorkspaceBarWide = 60;   // icon and text
inline constexpr int kWorkspaceBarNarrow = 36; // icon only
inline constexpr QRgb kTitleBarColor = 0xff8a8a8a;
inline constexpr QRgb kTitleBarActiveColor = 0xffa4a4a4;
inline constexpr QRgb kTabColor = 0xff5e5e5e;
inline constexpr QRgb kTabCurrentColor = 0xff7b3a9c;
inline constexpr QRgb kAccentColor = 0xff9b59d0;   // active workspace
inline constexpr QRgb kDropZoneColor = 0xff2fb36b; // green drop zones
inline constexpr QRgb kHostBackground = 0xff262626;
inline constexpr QRgb kSplitterColor = 0xff1c1c1c;

// Small interface icons drawn in code, so they look identical everywhere
// (Unicode symbols fall back to different fonts on each platform).
enum class GlyphIcon {
    ListView,
    FrameView,
    FastMenu,
    // Workspace toolbar (guide p.87). Our own drawings.
    WorkspaceEdit,
    WorkspaceColor,
    WorkspaceEffects,
    WorkspaceAudio,
    WorkspaceCustom,
};
QIcon glyphIcon(GlyphIcon which, QColor color = QColor(0xdc, 0xdc, 0xdc));

// Applies the application's look: the Fusion style, the bundled Inter font
// and a dark palette. Using the same style and font everywhere is what makes
// windows render the same on macOS, Windows and Linux (Phase 1 gate 1);
// native pieces such as the macOS menu bar and file dialogs stay native.
void applyTheme(QApplication& app);

// Family name of the bundled UI font ("Inter"), or empty if it failed to load.
const char* uiFontFamily();

} // namespace lumenis::ui
