#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QProxyStyle>
#include <QStyleFactory>

namespace lumenis::ui {

namespace {

bool g_fontLoaded = false;

// Fusion with control heights fixed rather than derived from the font.
class LumenisStyle : public QProxyStyle {
public:
    LumenisStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& size,
                           const QWidget* widget) const override {
        QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
        switch (type) {
        case CT_PushButton:
        case CT_ComboBox:
        case CT_LineEdit:
        case CT_SpinBox:
            s.setHeight(kControlHeight);
            break;
        case CT_ToolButton:
            s.setHeight(kToolButtonHeight);
            break;
        case CT_CheckBox:
        case CT_RadioButton:
            s.setHeight(kCheckBoxHeight);
            break;
        case CT_HeaderSection:
            s.setHeight(kHeaderHeight);
            break;
        case CT_ItemViewItem:
            s.setHeight(kRowHeight);
            break;
        case CT_TabBarTab:
            // Tab widths follow the text; rounding them keeps small
            // differences in text measurement from moving anything.
            s.setHeight(kTabHeight);
            s.setWidth((s.width() + 19) / 20 * 20);
            break;
        default:
            break;
        }
        return s;
    }

    // Qt's own tab close icon is a red bitmap; ours matches the rest.
    QIcon standardIcon(StandardPixmap icon, const QStyleOption* option, const QWidget* widget) const override {
        if (icon == SP_TabCloseButton) return glyphIcon(GlyphIcon::Close, QColor(0xb0, 0xb0, 0xb0));
        return QProxyStyle::standardIcon(icon, option, widget);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override {
        if (metric == PM_ScrollBarExtent) {
            return kScrollBarExtent; // macOS otherwise reports a wider native extent
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    // Disabled text is drawn plainly everywhere: some platforms otherwise
    // etch it with a light shadow, which looks different on each and shows
    // in text-free screenshots.
    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* returnData) const override {
        if (hint == SH_EtchDisabledText || hint == SH_DitherDisabledText) return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

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
    QApplication::setStyle(new LumenisStyle);
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

QIcon glyphIcon(GlyphIcon which, QColor color) {
    // Drawn at 2x and scaled down, so they are crisp on HiDPI screens.
    constexpr int size = 32;
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const QPen stroke(color, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    switch (which) {
    case GlyphIcon::ListView: // three rows with bullets
        for (int i = 0; i < 3; ++i) {
            p.drawRect(QRectF(6, 7 + i * 7, 4, 4));
            p.drawRect(QRectF(12, 7 + i * 7, 14, 4));
        }
        break;
    case GlyphIcon::FrameView: // 2x2 thumbnails
        for (int r = 0; r < 2; ++r) {
            for (int c = 0; c < 2; ++c) p.drawRect(QRectF(6 + c * 11, 6 + r * 11, 9, 9));
        }
        break;
    case GlyphIcon::FastMenu: // three lines and a small arrow
        for (int i = 0; i < 3; ++i) p.drawRect(QRectF(6, 7 + i * 6, 16, 3));
        {
            QPainterPath arrow;
            arrow.moveTo(20, 22);
            arrow.lineTo(28, 22);
            arrow.lineTo(24, 27);
            arrow.closeSubpath();
            p.drawPath(arrow);
        }
        break;
    case GlyphIcon::WorkspaceEdit: // two tracks of clips and a position bar
        p.drawRect(QRectF(4, 9, 11, 5));
        p.drawRect(QRectF(17, 9, 11, 5));
        p.drawRect(QRectF(4, 18, 16, 5));
        p.drawRect(QRectF(22, 18, 6, 5));
        p.drawRect(QRectF(15, 4, 2, 24));
        break;
    case GlyphIcon::WorkspaceColor: // eyedropper
        p.setPen(stroke);
        p.drawLine(QPointF(7, 25), QPointF(19, 13));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(22, 10), 5, 5);
        break;
    case GlyphIcon::WorkspaceEffects: { // wand with a spark
        p.setPen(stroke);
        p.drawLine(QPointF(6, 26), QPointF(19, 13));
        p.setPen(Qt::NoPen);
        QPainterPath star;
        star.moveTo(23, 3);
        star.lineTo(25, 8);
        star.lineTo(30, 9);
        star.lineTo(25, 11);
        star.lineTo(23, 16);
        star.lineTo(21, 11);
        star.lineTo(16, 9);
        star.lineTo(21, 8);
        star.closeSubpath();
        p.drawPath(star);
        break;
    }
    case GlyphIcon::WorkspaceAudio: { // waveform
        const int heights[] = {6, 12, 20, 14, 24, 10, 16, 8};
        for (int i = 0; i < 8; ++i) {
            p.drawRect(QRectF(3 + i * 3.5, 16 - heights[i] / 2.0, 2, heights[i]));
        }
        break;
    }
    case GlyphIcon::ScriptView: // a thumbnail beside lines of text
        p.drawRect(QRectF(5, 7, 9, 8));
        p.drawRect(QRectF(5, 18, 9, 8));
        for (int i = 0; i < 2; ++i) {
            p.drawRect(QRectF(17, 8 + i * 11, 10, 2.5));
            p.drawRect(QRectF(17, 12 + i * 11, 7, 2.5));
        }
        break;
    case GlyphIcon::Search: // magnifier
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(13, 13), 7, 7);
        p.drawLine(QPointF(18.5, 18.5), QPointF(26, 26));
        break;
    case GlyphIcon::Bin: { // an open box
        QPainterPath box;
        box.moveTo(5, 11);
        box.lineTo(27, 11);
        box.lineTo(25, 26);
        box.lineTo(7, 26);
        box.closeSubpath();
        p.drawPath(box);
        p.drawRect(QRectF(4, 7, 24, 3));
        break;
    }
    case GlyphIcon::Folder: {
        QPainterPath f;
        f.moveTo(4, 9);
        f.lineTo(12, 9);
        f.lineTo(15, 12);
        f.lineTo(28, 12);
        f.lineTo(28, 25);
        f.lineTo(4, 25);
        f.closeSubpath();
        p.drawPath(f);
        break;
    }
    case GlyphIcon::Trash:
        p.drawRect(QRectF(7, 10, 18, 17));
        p.drawRect(QRectF(5, 6, 22, 3));
        p.drawRect(QRectF(13, 3, 6, 3));
        break;
    case GlyphIcon::ItemClip: // a frame with sprocket holes
    case GlyphIcon::ItemSubclip:
    case GlyphIcon::ItemSource: {
        p.drawRect(QRectF(4, 8, 24, 16));
        p.setBrush(QColor(0x2a, 0x2a, 0x2a));
        for (int i = 0; i < 4; ++i) {
            p.drawRect(QRectF(6 + i * 6, 10, 3, 2.5));
            p.drawRect(QRectF(6 + i * 6, 19.5, 3, 2.5));
        }
        if (which == GlyphIcon::ItemSubclip) p.drawRect(QRectF(11, 13.5, 10, 5));
        if (which == GlyphIcon::ItemSource) p.drawEllipse(QPointF(16, 16), 2.5, 2.5);
        break;
    }
    case GlyphIcon::ItemAudio: {
        const int heights[] = {4, 10, 16, 8, 12, 6};
        for (int i = 0; i < 6; ++i) p.drawRect(QRectF(6 + i * 3.6, 16 - heights[i] / 2.0, 2.2, heights[i]));
        break;
    }
    case GlyphIcon::ItemSequence: // three tracks of clips
        p.drawRect(QRectF(4, 8, 10, 4));
        p.drawRect(QRectF(16, 8, 12, 4));
        p.drawRect(QRectF(4, 14, 16, 4));
        p.drawRect(QRectF(22, 14, 6, 4));
        p.drawRect(QRectF(4, 20, 24, 4));
        break;
    case GlyphIcon::ItemEffect:
        p.drawRect(QRectF(4, 8, 24, 16));
        p.setBrush(QColor(0x2a, 0x2a, 0x2a));
        p.drawEllipse(QPointF(16, 16), 5, 5);
        break;
    case GlyphIcon::ItemGroup:
        p.drawRect(QRectF(4, 6, 11, 9));
        p.drawRect(QRectF(17, 6, 11, 9));
        p.drawRect(QRectF(4, 17, 11, 9));
        p.drawRect(QRectF(17, 17, 11, 9));
        break;
    case GlyphIcon::ItemTitle:
        p.drawRect(QRectF(4, 8, 24, 16));
        p.setBrush(QColor(0x2a, 0x2a, 0x2a));
        p.drawRect(QRectF(9, 11, 14, 3));
        p.drawRect(QRectF(14.5, 11, 3, 10));
        break;
    case GlyphIcon::Close:
        p.setPen(QPen(color, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(10, 10), QPointF(22, 22));
        p.drawLine(QPointF(10, 22), QPointF(22, 10));
        break;
    case GlyphIcon::WorkspaceCustom: // four tiles
        for (int r = 0; r < 2; ++r) {
            for (int c = 0; c < 2; ++c) p.drawRoundedRect(QRectF(7 + c * 10, 7 + r * 10, 8, 8), 1.5, 1.5);
        }
        break;
    }
    p.end();
    pm.setDevicePixelRatio(2.0);
    return QIcon(pm);
}

const char* uiFontFamily() {
    return g_fontLoaded ? "Inter" : "";
}

} // namespace lumenis::ui
