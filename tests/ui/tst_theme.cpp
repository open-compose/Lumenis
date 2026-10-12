#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QComboBox>
#include <QFontInfo>
#include <QLineEdit>
#include <QProxyStyle>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTest>

using namespace lumenis::ui;

class TestTheme : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

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
