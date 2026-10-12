#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QFontInfo>
#include <QStyle>
#include <QTest>

class TestTheme : public QObject {
    Q_OBJECT

private slots:
    void appliesStyleFontAndPalette() {
        lumenis::ui::applyTheme(*qApp);
        QCOMPARE(QApplication::style()->name(), QStringLiteral("fusion"));
        QCOMPARE(QString::fromLatin1(lumenis::ui::uiFontFamily()), QStringLiteral("Inter"));
        // The family actually resolved, not a substitute.
        QCOMPARE(QFontInfo(QApplication::font()).family(), QStringLiteral("Inter"));
        QCOMPARE(QApplication::font().pixelSize(), 12);
        QVERIFY(QApplication::palette().color(QPalette::Window).lightness() < 100);
    }
};

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
