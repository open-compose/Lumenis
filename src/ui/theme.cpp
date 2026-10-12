#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace lumenis::ui {

namespace {

bool g_fontLoaded = false;

// Neutral greys so that colour judgements on pictures are not skewed by the
// interface around them.
QPalette darkPalette() {
    const QColor window(0x3a, 0x3a, 0x3a);
    const QColor base(0x2a, 0x2a, 0x2a);
    const QColor alternate(0x32, 0x32, 0x32);
    const QColor button(0x47, 0x47, 0x47);
    const QColor text(0xdc, 0xdc, 0xdc);
    const QColor disabledText(0x80, 0x80, 0x80);
    const QColor highlight(0x4f, 0x77, 0xa8);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabledText);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, button.lighter(130));
    p.setColor(QPalette::Midlight, button.lighter(115));
    p.setColor(QPalette::Mid, button.darker(130));
    p.setColor(QPalette::Dark, button.darker(160));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, highlight.lighter(140));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, disabledText);
    }
    return p;
}

} // namespace

void applyTheme(QApplication& app) {
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(darkPalette());

    const int regular = QFontDatabase::addApplicationFont(QStringLiteral(":/lumenis/ui/fonts/Inter-Regular.ttf"));
    const int semibold = QFontDatabase::addApplicationFont(QStringLiteral(":/lumenis/ui/fonts/Inter-SemiBold.ttf"));
    g_fontLoaded = regular >= 0 && semibold >= 0;
    if (g_fontLoaded) {
        QFont font(QStringLiteral("Inter"));
        // Pixel size, not points: points scale with each OS's logical DPI,
        // which would make layouts differ between platforms.
        font.setPixelSize(12);
        font.setHintingPreference(QFont::PreferNoHinting);
        app.setFont(font);
    } else {
        qWarning("bundled Inter font failed to load; using the system font");
    }
}

const char* uiFontFamily() {
    return g_fontLoaded ? "Inter" : "";
}

} // namespace lumenis::ui
