#include "lumenis/ui/capture.h"
#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QLabel>
#include <QImage>
#include <QLineEdit>
#include <QProxyStyle>
#include <QScopeGuard>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QSlider>
#include <QStyleOption>
#include <QStyleOptionSlider>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis::ui;

class TestTheme : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void captureLeavesPalettesAsTheyWere() {
        // capture() swaps in a text-free palette and must put back each
        // widget's own: a child's custom text colour stays, and widgets that
        // inherited their palette still inherit it.
        QWidget root;
        auto* plain = new QLabel(QStringLiteral("plain"), &root);
        auto* red = new QLabel(QStringLiteral("red"), &root);
        red->move(0, 20);
        QPalette p = red->palette();
        p.setColor(QPalette::WindowText, Qt::red);
        red->setPalette(p);
        root.resize(80, 40);
        QTemporaryDir dir;
        QVERIFY(capture(&root, QDir(dir.path()), QStringLiteral("shot")));
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(QStringLiteral("shot.notext.png"))));
        QCOMPARE(red->palette().color(QPalette::WindowText), QColor(Qt::red));
        QVERIFY(red->testAttribute(Qt::WA_SetPalette));
        QVERIFY(!root.testAttribute(Qt::WA_SetPalette));
        QVERIFY(!plain->testAttribute(Qt::WA_SetPalette));
        QCOMPARE(plain->palette().color(QPalette::WindowText), QApplication::palette().color(QPalette::WindowText));
        // Still inheriting: a new root palette reaches the plain label.
        QPalette blue = root.palette();
        blue.setColor(QPalette::WindowText, Qt::blue);
        root.setPalette(blue);
        QCOMPARE(plain->palette().color(QPalette::WindowText), QColor(Qt::blue));
        QCOMPARE(red->palette().color(QPalette::WindowText), QColor(Qt::red));
    }

    void appliesStyleFontAndPalette() {
        const auto* proxy = qobject_cast<const QProxyStyle*>(QApplication::style());
        QVERIFY(proxy);
        QCOMPARE(proxy->baseStyle()->name(), QStringLiteral("fusion"));
        QCOMPARE(QString::fromLatin1(uiFontFamily()), QStringLiteral("Inter"));
        // The family actually resolved, not a substitute.
        QCOMPARE(QFontInfo(QApplication::font()).family(), QStringLiteral("Inter"));
        QCOMPARE(QApplication::font().pixelSize(), 12);
        QVERIFY(QApplication::palette().color(QPalette::Window).lightness() < 100);
    }

    // Control heights must not follow font metrics, which differ between
    // platforms' font engines: even a much larger font keeps them.
    void controlHeightsIgnoreTheFont() {
        QFont big = QApplication::font();
        big.setPixelSize(20);
        QPushButton button(QStringLiteral("OK"));
        QComboBox combo;
        combo.addItem(QStringLiteral("1080p/23.976"));
        QLineEdit edit(QStringLiteral("text"));
        QSpinBox spin;
        for (QWidget* w : std::initializer_list<QWidget*>{&button, &combo, &edit, &spin}) {
            QCOMPARE(w->sizeHint().height(), kControlHeight);
            w->setFont(big);
            QCOMPARE(w->sizeHint().height(), kControlHeight);
        }
    }

    void scrollBarThicknessIsFixed() {
        QCOMPARE(QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent), kScrollBarExtent);
    }

    // Disabled text looks the same on every platform (no etched shadow).
    void disabledTextIsPlain() {
        QCOMPARE(QApplication::style()->styleHint(QStyle::SH_EtchDisabledText), 0);
        QCOMPARE(QApplication::style()->styleHint(QStyle::SH_DitherDisabledText), 0);
    }

    void sizesDoNotFollowTheDpi() {
        // Gate 1: Fusion and QCommonStyle scale their sizes by the DPI the
        // style option's font metrics carry, against 96 (72 on macOS). The
        // offscreen platform says 96 everywhere, so macOS's screenshots came
        // out with narrower spin box fields and layouts moved by a few pixels
        // (CI 2026-11-16). Font metrics from a 128 DPI image make any
        // platform scale the way macOS did; nothing may change.
        auto device = [](int dpi) {
            QImage image(8, 8, QImage::Format_ARGB32);
            image.setDotsPerMeterX(qRound(dpi / 0.0254));
            image.setDotsPerMeterY(qRound(dpi / 0.0254));
            return image;
        };
        // QTEST_MAIN sets AA_Use96Dpi, which every platform but macOS
        // obeys; without it here, the check would pass anywhere else.
        const bool use96 = QCoreApplication::testAttribute(Qt::AA_Use96Dpi);
        QCoreApplication::setAttribute(Qt::AA_Use96Dpi, false);
        const auto restore = qScopeGuard([use96] { QCoreApplication::setAttribute(Qt::AA_Use96Dpi, use96); });
        const QImage low = device(96);
        const QImage high = device(128);
        QStyle* style = QApplication::style();
        QStyleOption a, b;
        a.fontMetrics = QFontMetrics(QApplication::font(), &low);
        b.fontMetrics = QFontMetrics(QApplication::font(), &high);
        QVERIFY(a.fontMetrics.fontDpi() != b.fontMetrics.fontDpi());
        for (int m = 0; m <= static_cast<int>(QStyle::PM_LineEditIconMargin); ++m) { // PM_CustomBase is negative as an int
            const auto metric = static_cast<QStyle::PixelMetric>(m);
            QVERIFY2(style->pixelMetric(metric, &a) == style->pixelMetric(metric, &b),
                     qPrintable(QStringLiteral("pixel metric %1 follows the DPI").arg(m)));
        }
        QSpinBox spin;
        spin.resize(80, 23);
        QComboBox combo;
        combo.resize(120, 23);
        QList<QRect> rects[2];
        QList<QSize> sizes[2];
        for (int i = 0; i < 2; ++i) {
            const QFontMetrics& fm = i == 0 ? a.fontMetrics : b.fontMetrics;
            QStyleOptionSpinBox s;
            s.initFrom(&spin);
            s.fontMetrics = fm;
            s.frame = true;
            s.buttonSymbols = QAbstractSpinBox::UpDownArrows;
            for (auto sub : {QStyle::SC_SpinBoxUp, QStyle::SC_SpinBoxDown, QStyle::SC_SpinBoxEditField})
                rects[i] << style->subControlRect(QStyle::CC_SpinBox, &s, sub, &spin);
            sizes[i] << style->sizeFromContents(QStyle::CT_SpinBox, &s, QSize(30, 17), &spin);
            QStyleOptionComboBox c;
            c.initFrom(&combo);
            c.fontMetrics = fm;
            c.frame = true;
            for (auto sub : {QStyle::SC_ComboBoxArrow, QStyle::SC_ComboBoxEditField})
                rects[i] << style->subControlRect(QStyle::CC_ComboBox, &c, sub, &combo);
            sizes[i] << style->sizeFromContents(QStyle::CT_ComboBox, &c, QSize(60, 15), &combo);
        }
        QCOMPARE(rects[1], rects[0]);
        QCOMPARE(sizes[1], sizes[0]);
        // And they are Fusion's own at 96 DPI.
        QCOMPARE(rects[0].at(0), QRect(65, 3, 14, 8));
        QCOMPARE(rects[0].at(2), QRect(3, 3, 60, 17));
        QCOMPARE(rects[0].at(3), QRect(99, 0, 19, 23));
        QCOMPARE(rects[0].at(4), QRect(4, 2, 95, 19));
    }

    void fusionSizesAreTheSameEverywhere() {
        // Gate 1: macOS scaled Fusion's sizes by 96/72 (larger check boxes
        // and slider handles); the style pins them on every platform.
        QStyle* style = QApplication::style();
        for (int m = 0; m <= static_cast<int>(QStyle::PM_LineEditIconMargin); ++m) { // PM_CustomBase is negative as an int
            const auto metric = static_cast<QStyle::PixelMetric>(m);
            const int fixed = fixedPixelMetric(metric);
            if (fixed == kNotFixed) continue;
            QCOMPARE(style->pixelMetric(metric), fixed);
        }
        QCOMPARE(style->pixelMetric(QStyle::PM_IndicatorWidth), 14);
        QCOMPARE(style->pixelMetric(QStyle::PM_SliderLength), 15);
        QCOMPARE(style->pixelMetric(QStyle::PM_ScrollBarExtent), kScrollBarExtent);
        // The slider groove too: 7 px, as Fusion draws it at 96 DPI.
        QSlider slider(Qt::Horizontal);
        slider.resize(120, 22);
        QStyleOptionSlider opt;
        opt.initFrom(&slider);
        opt.orientation = Qt::Horizontal;
        opt.minimum = 0;
        opt.maximum = 100;
        QCOMPARE(style->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, &slider).height(), 7);
        opt.orientation = Qt::Vertical;
        opt.rect = QRect(0, 0, 22, 120);
        QCOMPARE(style->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, &slider).width(), 7);
    }

    // Rendered from packaging/icon.svg by scripts/make-icons.py.
    void applicationIconIsBuiltIn() {
        const QImage icon(QStringLiteral(":/lumenis/ui/icons/app.png"));
        QVERIFY(!icon.isNull());
        QCOMPARE(icon.size(), QSize(256, 256));
    }

    void glyphIconsAreDrawn() {
        for (const auto which : {GlyphIcon::ListView, GlyphIcon::FrameView, GlyphIcon::FastMenu}) {
            const QIcon icon = glyphIcon(which);
            QVERIFY(!icon.isNull());
            QVERIFY(!icon.pixmap(16, 16).toImage().isNull());
        }
    }
};

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
