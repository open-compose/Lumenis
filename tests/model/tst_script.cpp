// Scripts (8.3a; Editing Guide ch.18, lumenis/model/script.h): importing a text file, scene and page numbers, the
// script kept in its bin item, editing lines with the numbers following them, find and replace.
#include "lumenis/model/script.h"

#include <QTest>

using namespace lumenis::model;

class TestScript : public QObject {
    Q_OBJECT

private slots:
    void importsText() {
        const Script s = scriptFromText(QStringLiteral("INT. DINER - NIGHT\r\n\tMARY SUE\r\nCoffee?\r\n"));
        QCOMPARE(s.lines, QStringList({"INT. DINER - NIGHT", "        MARY SUE", "Coffee?"}));
        QCOMPARE(scriptFromText(QStringLiteral("one\rtwo")).lines, QStringList({"one", "two"}));
        QCOMPARE(scriptFromText(QString()).lines, QStringList{QString()});
    }

    void scenesAndPages() {
        Script s = scriptFromText(QStringLiteral("a\nb\nc\nd\ne"));
        setScene(s, 0, QStringLiteral("33"));
        setScene(s, 3, QStringLiteral("34"));
        setPage(s, 1, QStringLiteral("12"));
        QCOMPARE(s.sceneAt(2), QStringLiteral("33"));
        QCOMPARE(s.sceneAt(4), QStringLiteral("34"));
        QCOMPARE(s.pageAt(0), QString());
        QCOMPARE(s.pageAt(4), QStringLiteral("12"));
        setScene(s, 3, QString()); // an empty number removes it
        QCOMPARE(s.sceneAt(4), QStringLiteral("33"));
        setScene(s, 9, QStringLiteral("x")); // past the end: nothing
        QCOMPARE(s.scenes.size(), qsizetype(1));
        QCOMPARE(deleteScenes(s, 0, 4), 1);
        QCOMPARE(deletePages(s, 2, 4), 0);
        QCOMPARE(deletePages(s, 0, 4), 1);

        // Kept in the item, with the layout.
        Script t = scriptFromText(QStringLiteral("x\ny"), {60, 500, false, QStringLiteral("Courier"), 14});
        setScene(t, 1, QStringLiteral("2A"));
        const BinItem item = scriptItem(QStringLiteral("Pilot"), t);
        QVERIFY(isScript(item));
        QCOMPARE(kindName(item.kind), QStringLiteral("script"));
        QCOMPARE(*scriptOf(item), t);
        QVERIFY(!scriptOf(BinItem{}));
    }

    void editingKeepsNumbersWithLines() {
        Script s = scriptFromText(QStringLiteral("a\nb\nc\nd"));
        setScene(s, 1, QStringLiteral("1"));
        setScene(s, 3, QStringLiteral("2"));
        setPage(s, 2, QStringLiteral("5"));
        // Line b becomes two lines: the scene stays on the first, the later ones move down.
        replaceLines(s, 1, 2, {QStringLiteral("b1\nb2")});
        QCOMPARE(s.lines, QStringList({"a", "b1", "b2", "c", "d"}));
        QCOMPARE(s.scenes.value(1), QStringLiteral("1"));
        QCOMPARE(s.pages.value(3), QStringLiteral("5"));
        QCOMPARE(s.scenes.value(4), QStringLiteral("2"));
        // Lines b1..c removed: their numbers go, the later scene moves up.
        replaceLines(s, 1, 4, {});
        QCOMPARE(s.lines, QStringList({"a", "d"}));
        QVERIFY(!s.pages.contains(1));
        QCOMPARE(s.scenes.value(1), QStringLiteral("2"));
    }

    void findAndReplace() {
        Script s = scriptFromText(QStringLiteral("Coffee?\nMore coffee.\nNo."));
        auto m = findInScript(s, QStringLiteral("coffee"), 0, 0);
        QVERIFY(m);
        QCOMPARE(m->line, 0);
        m = findInScript(s, QStringLiteral("coffee"), 0, 1);
        QCOMPARE(m->line, 1);
        QCOMPARE(m->column, 5);
        m = findInScript(s, QStringLiteral("coffee"), 1, 11); // going round
        QCOMPARE(m->line, 0);
        m = findInScript(s, QStringLiteral("coffee"), 1, 5, true);
        QCOMPARE(m->line, 0);
        QVERIFY(!findInScript(s, QStringLiteral("tea"), 0, 0));
        QVERIFY(!findInScript(s, QStringLiteral("coffee"), 0, 0, false, false) || findInScript(s, QStringLiteral("coffee"), 0, 0, false, false)->line == 1);
        QCOMPARE(replaceInScript(s, QStringLiteral("coffee"), QStringLiteral("tea")), 2);
        QCOMPARE(s.lines, QStringList({"tea?", "More tea.", "No."}));
    }
};

QTEST_GUILESS_MAIN(TestScript)
#include "tst_script.moc"
