#include "lumenis/ui/theme.h"

#include <algorithm>
#include <array>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QProxyStyle>
#include <QStyleOption>
#include <QStyleFactory>

#include <cmath>
#include <numbers>

// The application's look (theme.h): a proxy of Qt's Fusion style with sizes that do not depend on the
// platform, the dark palette, the bundled Inter font (embedded as a Qt resource, see
// THIRD-PARTY-NOTICES.md), and the small icons drawn in code. All of it serves Phase 1 gate 1: the same
// window looks the same on macOS, Windows and Linux, checked by CI's screenshot comparison (capture.cpp).
// Colours and sizes of the panel chrome are constants in theme.h.

namespace lumenis::ui {

namespace {

// Whether applyTheme() loaded both Inter faces; uiFontFamily() reports it.
bool g_fontLoaded = false;
// Slider groove thickness in pixels, see LumenisStyle::subControlRect().
constexpr int kSliderGroove = 7;

} // namespace

// Fusion scales its constant sizes by the screen's DPI against a baseline
// of 96, or 72 on macOS. With the offscreen platform's 96 DPI everything on
// macOS came out a third larger: 18 px check boxes instead of 14, larger
// slider handles and dials. The values Fusion gives at its baseline,
// identical everywhere; high-density screens scale them by the device
// pixel ratio as usual.
int fixedPixelMetric(QStyle::PixelMetric metric) {
    switch (metric) {
    case QStyle::PM_IndicatorWidth:
    case QStyle::PM_IndicatorHeight:
    case QStyle::PM_ExclusiveIndicatorWidth:
    case QStyle::PM_ExclusiveIndicatorHeight: return 14;
    case QStyle::PM_SliderThickness:
    case QStyle::PM_SliderLength: return 15;
    case QStyle::PM_SliderTickmarkOffset: return 4;
    case QStyle::PM_ScrollBarSliderMin: return 26;
    case QStyle::PM_SplitterWidth:
    case QStyle::PM_DockWidgetHandleExtent: return 4;
    case QStyle::PM_SmallIconSize:
    case QStyle::PM_ButtonIconSize: return 16;
    case QStyle::PM_ListViewIconSize: return 24;
    case QStyle::PM_MessageBoxIconSize: return 48;
    case QStyle::PM_TabBarTabVSpace: return 12;
    case QStyle::PM_TabCloseIndicatorWidth:
    case QStyle::PM_TabCloseIndicatorHeight: return 20;
    case QStyle::PM_ToolBarHandleExtent: return 9;
    case QStyle::PM_ToolBarItemSpacing: return 1;
    case QStyle::PM_ToolBarItemMargin:
    case QStyle::PM_ToolBarFrameWidth: return 2;
    case QStyle::PM_HeaderMargin:
    case QStyle::PM_ToolTipLabelFrameWidth: return 2;
    case QStyle::PM_SpinBoxFrameWidth: return 3;
    case QStyle::PM_MenuBarItemSpacing: return 6;
    case QStyle::PM_DockWidgetTitleMargin: return 1;
    case QStyle::PM_TabBarTabOverlap: return 1;
    case QStyle::PM_TabBarBaseOverlap: return 2;
    // Qt's common style scales these the same way.
    case QStyle::PM_TabBarTabHSpace: return 24;
    case QStyle::PM_TabBarScrollButtonWidth: return 16;
    case QStyle::PM_TabBar_ScrollButtonOverlap: return 1;
    case QStyle::PM_TabBarTabShiftVertical: return 2;
    case QStyle::PM_TabBarIconSize: return 16;
    case QStyle::PM_MenuButtonIndicator: return 12;
    case QStyle::PM_CheckBoxLabelSpacing:
    case QStyle::PM_RadioButtonLabelSpacing: return 6;
    case QStyle::PM_ButtonMargin: return 6;
    case QStyle::PM_ToolBarExtensionExtent: return 12;
    case QStyle::PM_ToolBarIconSize: return 24;
    case QStyle::PM_HeaderMarkSize: return 16;
    case QStyle::PM_LargeIconSize:
    case QStyle::PM_IconViewIconSize: return 32;
    case QStyle::PM_TreeViewIndentation: return 20;
    case QStyle::PM_FocusFrameHMargin:
    case QStyle::PM_FocusFrameVMargin: return 2;
    default: return kNotFixed;
    }
}

namespace {

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
            // Fixed so rows match on every platform, but never shorter than a larger font a user chose (the Interface
            // settings' sidebar and bin font sizes, Set Font; 10.3d): the default 12 px font fits in kRowHeight.
            s.setHeight(option ? rowHeightFor(option->fontMetrics) : kRowHeight);
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
        if (const int fixed = fixedPixelMetric(metric); fixed != kNotFixed) return fixed;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    // Fusion's slider groove is 7 px thick at its baseline DPI; it scales
    // the thickness like the pixel metrics (fixedPixelMetric).
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option, SubControl sub,
                         const QWidget* widget) const override {
        QRect r = QProxyStyle::subControlRect(control, option, sub, widget);
        if (control == CC_Slider && sub == SC_SliderGroove) {
            if (const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option)) {
                const QPoint centre = slider->rect.center();
                if (slider->orientation == Qt::Horizontal) {
                    r.setHeight(kSliderGroove);
                    r.moveTop(centre.y() - kSliderGroove / 2);
                } else {
                    r.setWidth(kSliderGroove);
                    r.moveLeft(centre.x() - kSliderGroove / 2);
                }
            }
        }
        return r;
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
    // Placeholders ("Type part of a command's name") are read, so they keep 4.5:1 against the fields (WCAG 2 AA, 10.3d;
    // docs/spec/accessibility.md); disabled text is exempt and stays dimmer.
    const QColor placeholderText(0x9a, 0x9a, 0x9a);
    const QColor highlight(0x4f, 0x77, 0xa8);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, placeholderText);
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

int rowHeightFor(const QFontMetrics& metrics) {
    const int base = QFontMetrics(QApplication::font()).height();
    return kRowHeight + std::max(0, metrics.height() - base);
}

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

// Each icon is drawn on a 32 px grid, filled with `color` or stroked 3 px wide.
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
    case GlyphIcon::ItemScript:
        p.drawRect(QRectF(8, 4, 16, 24));
        p.setPen(QPen(QColor(0x2a, 0x2a, 0x2a), 1.6));
        for (int y = 9; y <= 23; y += 4) p.drawLine(QPointF(11, y), QPointF(21, y));
        break;
    case GlyphIcon::Close:
        p.setPen(QPen(color, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(10, 10), QPointF(22, 22));
        p.drawLine(QPointF(10, 22), QPointF(22, 10));
        break;
    case GlyphIcon::ArrowBack:
    case GlyphIcon::ArrowForward:
    case GlyphIcon::ArrowUp: {
        // A shaft and a head, pointing left; turned for the others.
        p.translate(16, 16);
        p.rotate(which == GlyphIcon::ArrowBack ? 0 : which == GlyphIcon::ArrowForward ? 180 : 90);
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(-9, 0), QPointF(10, 0));
        p.drawPolyline(QPolygonF({QPointF(-1, -8), QPointF(-9, 0), QPointF(-1, 8)}));
        break;
    }
    case GlyphIcon::Home: {
        QPainterPath h;
        h.moveTo(16, 5);
        h.lineTo(28, 16);
        h.lineTo(24, 16);
        h.lineTo(24, 27);
        h.lineTo(19, 27);
        h.lineTo(19, 20);
        h.lineTo(13, 20);
        h.lineTo(13, 27);
        h.lineTo(8, 27);
        h.lineTo(8, 16);
        h.lineTo(4, 16);
        h.closeSubpath();
        p.drawPath(h);
        break;
    }
    case GlyphIcon::CollapseTree: // a parent row and two indented children
        p.drawRect(QRectF(5, 6, 22, 4));
        p.drawRect(QRectF(11, 14, 16, 4));
        p.drawRect(QRectF(11, 22, 16, 4));
        p.drawRect(QRectF(6, 10, 2, 14));
        break;
    case GlyphIcon::Star: {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double r = (i % 2) ? 5.0 : 12.0;
            const double a = -std::numbers::pi / 2 + i * std::numbers::pi / 5;
            star << QPointF(16 + r * std::cos(a), 17 + r * std::sin(a));
        }
        p.drawPolygon(star);
        break;
    }
    case GlyphIcon::Lock: { // a padlock: the shackle, then the body
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(10, 5, 12, 14), 0, 180 * 16);
        p.drawLine(QPointF(10, 12), QPointF(10, 15));
        p.drawLine(QPointF(22, 12), QPointF(22, 15));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(7, 15, 18, 13), 2, 2);
        break;
    }
    case GlyphIcon::TextTool: { // a serif T
        p.drawRect(QRectF(6, 5, 20, 4));
        p.drawRect(QRectF(14, 5, 4, 22));
        p.drawRect(QRectF(11, 24, 10, 3));
        break;
    }
    case GlyphIcon::SelectionTool: { // four arrows from the centre
        p.setPen(stroke);
        p.drawLine(QPointF(16, 5), QPointF(16, 27));
        p.drawLine(QPointF(5, 16), QPointF(27, 16));
        p.setPen(Qt::NoPen);
        for (const auto& [x, y, dx, dy] : {std::array<double, 4>{16, 3, 0, 1}, {16, 29, 0, -1}, {3, 16, 1, 0}, {29, 16, -1, 0}}) {
            QPainterPath a;
            a.moveTo(x, y);
            a.lineTo(x + dx * 6 + dy * 4, y + dy * 6 + dx * 4);
            a.lineTo(x + dx * 6 - dy * 4, y + dy * 6 - dx * 4);
            a.closeSubpath();
            p.drawPath(a);
        }
        break;
    }
    case GlyphIcon::RectangleTool:
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(6, 8, 20, 16));
        break;
    case GlyphIcon::EllipseTool:
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(6, 7, 20, 18));
        break;
    case GlyphIcon::BringForward:
    case GlyphIcon::SendBackward: { // two overlapping squares, the front one filled or outlined
        const bool forward = which == GlyphIcon::BringForward;
        p.setPen(QPen(color, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(5, 5, 14, 14));
        p.setBrush(forward ? QBrush(color) : QBrush(Qt::NoBrush));
        p.drawRect(QRectF(13, 13, 14, 14));
        if (!forward) {
            p.setBrush(color);
            p.drawRect(QRectF(5, 5, 8, 8));
        }
        break;
    }
    case GlyphIcon::Group:
    case GlyphIcon::Ungroup: { // two squares, inside a dashed frame when grouped
        p.drawRect(QRectF(8, 8, 8, 8));
        p.drawRect(QRectF(17, 17, 8, 8));
        QPen frame(color, 2.0, which == GlyphIcon::Group ? Qt::SolidLine : Qt::DashLine);
        p.setPen(frame);
        p.setBrush(Qt::NoBrush);
        if (which == GlyphIcon::Group) p.drawRect(QRectF(4, 4, 25, 25));
        else {
            p.drawRect(QRectF(4, 4, 15, 15));
            p.drawRect(QRectF(13, 13, 15, 15));
        }
        break;
    }
    case GlyphIcon::PreviousObject:
    case GlyphIcon::NextObject: { // an arrow to a bar
        const bool next = which == GlyphIcon::NextObject;
        p.setPen(stroke);
        p.drawLine(QPointF(next ? 25 : 7, 7), QPointF(next ? 25 : 7, 25));
        p.drawLine(QPointF(next ? 5 : 27, 16), QPointF(next ? 21 : 11, 16));
        p.drawPolyline(QPolygonF({QPointF(next ? 16 : 16, 11), QPointF(next ? 21 : 11, 16), QPointF(16, 21)}));
        break;
    }
    case GlyphIcon::Grid:
        p.setPen(QPen(color, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(5, 5, 22, 22));
        for (int i = 1; i < 3; ++i) {
            p.drawLine(QPointF(5 + i * 22.0 / 3, 5), QPointF(5 + i * 22.0 / 3, 27));
            p.drawLine(QPointF(5, 5 + i * 22.0 / 3), QPointF(27, 5 + i * 22.0 / 3));
        }
        break;
    case GlyphIcon::AddKeyframe:
    case GlyphIcon::DeleteKeyframe: { // a keyframe diamond with a plus or a minus
        QPainterPath d;
        d.moveTo(12, 6);
        d.lineTo(20, 14);
        d.lineTo(12, 22);
        d.lineTo(4, 14);
        d.closeSubpath();
        p.drawPath(d);
        p.drawRect(QRectF(19, 23, 10, 3));
        if (which == GlyphIcon::AddKeyframe) p.drawRect(QRectF(22.5, 19.5, 3, 10));
        break;
    }
    case GlyphIcon::Underline: // a U over a line
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF({QPointF(9, 5), QPointF(9, 16)}));
        p.drawPolyline(QPolygonF({QPointF(23, 5), QPointF(23, 16)}));
        p.drawArc(QRectF(9, 9, 14, 14), 180 * 16, 180 * 16);
        p.drawLine(QPointF(7, 27), QPointF(25, 27));
        break;
    case GlyphIcon::JustifyLeft:
    case GlyphIcon::JustifyCenter:
    case GlyphIcon::JustifyRight: { // lines of text, short ones aligned
        const double widths[] = {22, 14, 22, 14};
        for (int i = 0; i < 4; ++i) {
            const double w = widths[i];
            const double x = which == GlyphIcon::JustifyLeft ? 5 : which == GlyphIcon::JustifyRight ? 27 - w : 16 - w / 2;
            p.drawRect(QRectF(x, 6 + i * 6, w, 3));
        }
        break;
    }
    case GlyphIcon::AlignLeft:
    case GlyphIcon::AlignHorizontalCenter:
    case GlyphIcon::AlignRight:
    case GlyphIcon::AlignTop:
    case GlyphIcon::AlignVerticalCenter:
    case GlyphIcon::AlignBottom: { // two bars against a line
        const bool vertical = which == GlyphIcon::AlignTop || which == GlyphIcon::AlignVerticalCenter || which == GlyphIcon::AlignBottom;
        const int place = (which == GlyphIcon::AlignLeft || which == GlyphIcon::AlignTop) ? 0
                          : (which == GlyphIcon::AlignRight || which == GlyphIcon::AlignBottom) ? 2 : 1;
        const double line = place == 0 ? 5 : place == 2 ? 27 : 16;
        auto bar = [&](double along, double length) {
            const double start = place == 0 ? 7 : place == 2 ? 25 - length : 16 - length / 2;
            p.drawRect(vertical ? QRectF(along, start, 6, length) : QRectF(start, along, length, 6));
        };
        bar(7, 18);
        bar(18, 11);
        p.drawRect(vertical ? QRectF(4, line - 1, 24, 2) : QRectF(line - 1, 4, 2, 24));
        break;
    }
    case GlyphIcon::AlignFrameHorizontal:
    case GlyphIcon::AlignFrameVertical: { // a block centred in a frame
        p.setPen(QPen(color, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(4, 4, 24, 24));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRect(which == GlyphIcon::AlignFrameHorizontal ? QRectF(11, 9, 10, 14) : QRectF(9, 11, 14, 10));
        break;
    }
    case GlyphIcon::Reduce:
    case GlyphIcon::Enlarge: { // a frame with arrows in or out
        p.setPen(QPen(color, 2.0));
        p.setBrush(Qt::NoBrush);
        const double s = which == GlyphIcon::Enlarge ? 22 : 12;
        p.drawRect(QRectF(16 - s / 2, 16 - s / 2, s, s));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRect(QRectF(14, 14, 4, 4));
        break;
    }
    case GlyphIcon::DualSplit:
        p.setPen(QPen(color, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(4, 7, 24, 18));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRect(QRectF(4, 7, 12, 18));
        break;
    case GlyphIcon::Play:
    case GlyphIcon::PlayLoop: {
        QPainterPath a;
        a.moveTo(10, 7);
        a.lineTo(24, 16);
        a.lineTo(10, 25);
        a.closeSubpath();
        p.drawPath(a);
        if (which == GlyphIcon::PlayLoop) {
            p.setPen(QPen(color, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(3, 3, 26, 26), 30 * 16, 300 * 16);
        }
        break;
    }
    case GlyphIcon::HighQuality: { // the letters, drawn as strokes
        p.setPen(stroke);
        p.drawLine(QPointF(5, 9), QPointF(5, 23));
        p.drawLine(QPointF(13, 9), QPointF(13, 23));
        p.drawLine(QPointF(5, 16), QPointF(13, 16));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(17, 9, 10, 14));
        p.drawLine(QPointF(23, 20), QPointF(28, 25));
        break;
    }
    case GlyphIcon::TrackingTool: // a target
        p.setPen(QPen(color, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(6, 6, 20, 20));
        p.drawLine(QPointF(16, 2), QPointF(16, 11));
        p.drawLine(QPointF(16, 21), QPointF(16, 30));
        p.drawLine(QPointF(2, 16), QPointF(11, 16));
        p.drawLine(QPointF(21, 16), QPointF(30, 16));
        break;
    case GlyphIcon::Render: // a frame with a dot: an effect made into media
        p.drawRoundedRect(QRectF(4, 8, 24, 16), 2, 2);
        p.setBrush(QColor(20, 20, 20));
        p.drawEllipse(QPointF(16, 16), 4, 4);
        break;
    case GlyphIcon::MediaVolume: // a drive with a light
        p.drawRoundedRect(QRectF(4, 10, 24, 12), 3, 3);
        p.setBrush(QColor(20, 20, 20));
        p.drawEllipse(QPointF(23, 16), 2, 2);
        break;
    case GlyphIcon::ChevronDown:
    case GlyphIcon::ChevronUp: {
        p.setPen(stroke);
        p.setBrush(Qt::NoBrush);
        const double y0 = which == GlyphIcon::ChevronDown ? 12 : 20;
        const double y1 = which == GlyphIcon::ChevronDown ? 20 : 12;
        p.drawPolyline(QPolygonF({QPointF(8, y0), QPointF(16, y1), QPointF(24, y0)}));
        break;
    }
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
