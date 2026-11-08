// Scripts (8.3a; Editing Guide ch.18, lumenis/model/script.h): importing a text file, scene and page numbers, the
// script kept in its bin item, editing lines with the numbers following them, find and replace.
#include "lumenis/model/script.h"
#include "lumenis/model/script_sync.h"

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

    // Slates and takes (p.576-582): clips linked to lines, takes added and deleted, slates moved with and without their
    // lines, take lengths, representative frames, off-screen and colour indicators, kept in the item and through edits.
    void slatesAndTakes() {
        Script s = scriptFromText(QStringLiteral("0\n1\n2\n3\n4\n5\n6\n7\n8\n9"));
        QVERIFY(addSlate(s, 2, 5, {}).isEmpty());
        const QString slate = addSlate(s, 5, 2, {{QStringLiteral("b"), QStringLiteral("c1"), QStringLiteral("33/1"), QStringLiteral("1")},
                                                 {QStringLiteral("b"), QStringLiteral("c2"), QStringLiteral("33/2"), QStringLiteral("2")}});
        QVERIFY(!slate.isEmpty());
        QCOMPARE(s.slates.size(), qsizetype(1));
        QCOMPARE(s.slates[0].line, 2);
        const QString t1 = s.slates[0].takes[0].id;
        const QString t2 = s.slates[0].takes[1].id;
        QCOMPARE(s.slates[0].takes[0].first, 2);
        QCOMPARE(s.slates[0].takes[0].last, 5);
        QCOMPARE(s.slates[0].takes[1].take, QStringLiteral("2"));
        const QString t3 = addTake(s, slate, 3, 8, {QStringLiteral("b"), QStringLiteral("c3"), QStringLiteral("33A/1"), {}});
        QCOMPARE(slateOf(s, t3)->id, slate);
        QCOMPARE(takeOf(s, t3)->last, 8);

        // Moved alone, the lines stay; with Ctrl, they move too, stopping at the script's end.
        moveSlate(s, slate, 3, 40, false);
        QCOMPARE(s.slates[0].line, 5);
        QCOMPARE(s.slates[0].x, 40);
        QCOMPARE(takeOf(s, t1)->first, 2);
        moveSlate(s, slate, 5, 0, true);
        QCOMPARE(takeOf(s, t3)->last, 9); // only 1 line further: t3 reaches the last line
        QCOMPARE(takeOf(s, t1)->first, 3);
        moveSlate(s, slate, -1, 0, true);

        setTakeLines(s, t2, 6, 1);
        QCOMPARE(takeOf(s, t2)->first, 1);
        QCOMPARE(takeOf(s, t2)->last, 6);

        stepTakeFrames(s, {t2}, 5);
        stepTakeFrames(s, {t2}, -9);
        QCOMPARE(takeOf(s, t2)->frame, std::int64_t(0));
        QCOMPARE(s.slates[0].active, 1);

        // Off-screen: toggled by the first take; colours replace each other.
        QVERIFY(toggleOffscreen(s, {t1, t2}, 3, 4));
        QVERIFY(isOffscreen(*takeOf(s, t1), 3));
        QVERIFY(isOffscreen(*takeOf(s, t2), 4));
        QVERIFY(!isOffscreen(*takeOf(s, t2), 5));
        QVERIFY(!toggleOffscreen(s, {t1}, 3, 3));
        QVERIFY(!isOffscreen(*takeOf(s, t1), 3));
        QVERIFY(isOffscreen(*takeOf(s, t1), 4));
        QVERIFY(toggleColor(s, {t2}, 2, 5, QStringLiteral("#d2c832")));
        QVERIFY(toggleColor(s, {t2}, 3, 3, QStringLiteral("#c83232")));
        QCOMPARE(colorAt(*takeOf(s, t2), 2), QStringLiteral("#d2c832"));
        QCOMPARE(colorAt(*takeOf(s, t2), 3), QStringLiteral("#c83232"));
        QVERIFY(!toggleColor(s, {t2}, 3, 3, QStringLiteral("#c83232")));
        QCOMPARE(colorAt(*takeOf(s, t2), 3), QString());

        // Interpolate Position: line 4 of 1-6 halfway down is 3.5 lines of 6 into a 120-frame clip.
        QCOMPARE(interpolatedFrame(*takeOf(s, t2), 4, 0.5, 120), std::int64_t(70));

        // Kept in the item.
        const Script back = Script::fromJson(s.toJson());
        QCOMPARE(back, s);

        // Edits move the takes with their lines.
        Script e = s;
        replaceLines(e, 0, 0, {QStringLiteral("new")});
        QCOMPARE(takeOf(e, t1)->first, takeOf(s, t1)->first + 1);
        QCOMPARE(e.slates[0].line, s.slates[0].line + 1);

        // Deleting takes; a slate without takes goes.
        QCOMPARE(deleteTakes(s, {t1, t3}), 2);
        QCOMPARE(s.slates[0].takes.size(), qsizetype(1));
        QCOMPARE(s.slates[0].active, 0);
        QCOMPARE(deleteTakes(s, {t2}), 1);
        QVERIFY(s.slates.isEmpty());
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

    // Script marks (p.582-588).
    void scriptMarks() {
        Script s = scriptFromText(QStringLiteral("0\n1\n2\n3\n4\n5\n6\n7\n8\n9"));
        addSlate(s, 2, 6, {{QStringLiteral("b"), QStringLiteral("c1"), QStringLiteral("33/1"), QStringLiteral("1")}});
        const QString t = s.slates[0].takes[0].id;
        QVERIFY(!addScriptMark(s, QStringLiteral("nope"), 3, 10));
        QVERIFY(addScriptMark(s, t, 3, 40));
        QVERIFY(addScriptMark(s, t, 5, 90));
        QVERIFY(addScriptMark(s, t, 3, 30)); // replaces the line's mark
        QCOMPARE(takeOf(s, t)->marks.value(3), 30);
        // A line past the take's end extends it (p.584).
        QVERIFY(addScriptMark(s, t, 8, 150));
        QCOMPARE(takeOf(s, t)->last, 8);
        // Find Script: the frame's lines.
        QCOMPARE(scriptLinesAt(*takeOf(s, t), 10), std::nullopt);
        QCOMPARE(scriptLinesAt(*takeOf(s, t), 30), (std::pair{3, 4}));
        QCOMPARE(scriptLinesAt(*takeOf(s, t), 100), (std::pair{5, 7}));
        QCOMPARE(scriptLinesAt(*takeOf(s, t), 200), (std::pair{8, 8}));
        // The marked frames of a range: lines 4-5 run from line 3's mark to before line 8's.
        QCOMPARE(markedFrames(*takeOf(s, t), 4, 5, 300), (std::pair<std::int64_t, std::int64_t>{30, 149}));
        QCOMPARE(markedFrames(*takeOf(s, t), 8, 8, 300), (std::pair<std::int64_t, std::int64_t>{150, 299}));
        QCOMPARE(markedFrames(*takeOf(s, t), 2, 2, 300), std::nullopt); // before every mark
        QCOMPARE(markedFrames(*takeOf(s, t), 2, 3, 300), (std::pair<std::int64_t, std::int64_t>{30, 89}));
        // Moving a mark keeps its frame (p.586); the JSON keeps the marks.
        QVERIFY(moveScriptMark(s, t, 5, 6));
        QCOMPARE(takeOf(s, t)->marks.value(6), 90);
        QVERIFY(!takeOf(s, t)->marks.contains(5));
        QCOMPARE(Script::fromJson(s.toJson()), s);
        // Lines inserted above move the marks with their lines.
        replaceLines(s, 0, 0, {QStringLiteral("new")});
        QCOMPARE(takeOf(s, t)->marks.value(4), 30);
        // Delete Mark(s) on a region of the take.
        QCOMPARE(countScriptMarks(s, {t}, 0, 6), 1);
        QCOMPARE(deleteScriptMarks(s, {t}, 0, 6), 1);
        QCOMPARE(takeOf(s, t)->marks.size(), qsizetype(2));
        // Shortening the take drops marks outside it.
        setTakeLines(s, t, 3, 7);
        QCOMPARE(takeOf(s, t)->marks.size(), qsizetype(1));
    }

    // ScriptSync AI (p.584-585) and Create Script from Clip (p.569-570).
    void scriptSync() {
        // The skip options.
        ScriptSyncOptions o;
        QCOMPARE(spokenText(QStringLiteral("MARY SUE"), o), QStringLiteral("MARY SUE"));
        o.skipCapitals = true;
        QCOMPARE(spokenText(QStringLiteral("MARY SUE"), o), QString());
        o.skipBracketed = true;
        QCOMPARE(spokenText(QStringLiteral("Coffee (beat) please [aside]"), o), QStringLiteral("Coffee please"));
        o.skipBeforeColon = true;
        QCOMPARE(spokenText(QStringLiteral("MARY: Coffee?"), o), QStringLiteral("Coffee?"));
        o.skipIndented = true;
        o.dialogIndent = 4;
        QCOMPARE(spokenText(QStringLiteral("  Action line."), o), QString());
        QCOMPARE(spokenText(QStringLiteral("\tCoffee?"), o), QStringLiteral("Coffee?"));
        QCOMPARE(indentOf(QStringLiteral("  \tx")), 8);

        Script s = scriptFromText(QStringLiteral("INT. DINER - NIGHT\nMARY SUE\nCoffee, please.\nWAITRESS\nComing right up, hon.\nMARY SUE\nThanks a lot."));
        addSlate(s, 1, 6, {{QStringLiteral("b"), QStringLiteral("c1"), QStringLiteral("33/1"), QStringLiteral("1")}});
        const QString t = s.slates[0].takes[0].id;
        // What was said, with a cough and a changed word: "Thanks a bunch" still has two of its three words.
        const QList<SyncWord> words{{QStringLiteral("cough"), 2},   {QStringLiteral("Coffee"), 10}, {QStringLiteral("please"), 14},
                                    {QStringLiteral("Coming"), 40}, {QStringLiteral("right"), 44},  {QStringLiteral("up"), 47},
                                    {QStringLiteral("hon"), 50},    {QStringLiteral("Thanks"), 80}, {QStringLiteral("a"), 84},
                                    {QStringLiteral("bunch"), 86}};
        ScriptSyncOptions sync;
        sync.skipCapitals = true;
        QCOMPARE(scriptSyncTake(s, t, words, sync), 3);
        QCOMPARE(takeOf(s, t)->marks, (QMap<int, std::int64_t>{{2, 10}, {4, 40}, {6, 80}}));
        // Existing marks stay without Overwrite; with it they are updated.
        addScriptMark(s, t, 4, 39);
        QCOMPARE(scriptSyncTake(s, t, words, sync), 0);
        QCOMPARE(takeOf(s, t)->marks.value(4), 39);
        sync.overwrite = true;
        QCOMPARE(scriptSyncTake(s, t, words, sync), 1);
        QCOMPARE(takeOf(s, t)->marks.value(4), 40);
        // Between the first and last marks only: line 6's mark is the last, so nothing after it changes.
        deleteScriptMarks(s, {t}, 4, 4);
        sync.betweenMarks = true;
        QCOMPARE(scriptSyncTake(s, t, words, sync), 1);
        QCOMPARE(takeOf(s, t)->marks.value(4), 40);

        // Create Script from Clip: lines from the transcript, the clip's take over them, every line marked.
        ShownTranscript shown;
        ShownLine l1;
        l1.speaker = QStringLiteral("Mary");
        l1.frame = 5;
        l1.words = {{QStringLiteral("Coffee?"), 5, 9, {}, 0, 0}};
        ShownLine l2;
        l2.speaker = QStringLiteral("Speaker 2");
        l2.frame = 30;
        l2.words = {{QStringLiteral("Coming"), 30, 34, {}, 1, 0}, {QStringLiteral("up."), 34, 38, {}, 1, 1}};
        shown.lines = {l1, l2};
        const Script made = scriptFromTranscripts({{QStringLiteral("b"), QStringLiteral("c1"), QStringLiteral("33/1"), {}},
                                                   {QStringLiteral("b"), QStringLiteral("c2"), QStringLiteral("33/2"), {}}},
                                                  {shown, ShownTranscript{}}, {});
        QCOMPARE(made.lines, (QStringList{QStringLiteral("Mary: Coffee?"), QStringLiteral("Speaker 2: Coming up.")}));
        QCOMPARE(made.slates.size(), qsizetype(1));
        QCOMPARE(made.slates[0].takes[0].clipId, QStringLiteral("c1"));
        QCOMPARE(made.slates[0].takes[0].marks, (QMap<int, std::int64_t>{{0, 5}, {1, 30}}));
    }
};

QTEST_GUILESS_MAIN(TestScript)
#include "tst_script.moc"
