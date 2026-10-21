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
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QSlider>
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

    void fusionSizesAreTheSameEverywhere() {
        // Gate 1: macOS scaled Fusion's sizes by 96/72 (larger check boxes
        // and slider handles); the style pins them on every platform.
        QStyle* style = QApplication::style();
        for (int m = 0; m < static_cast<int>(QStyle::PM_CustomBase); ++m) {
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
