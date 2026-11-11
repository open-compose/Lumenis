// The List Tool's cut lists (9.4a; Editing Guide p.944-948, p.960-964; docs/spec/cut-lists.md): Columnar and
// TabbedLists in the Output Format menu, the Cut List options shown only for them, Preview and Save List, and the
// project's film type counting the feet.
#include "lumenis/model/sequence.h"
#include "lumenis/ui/list_tool.h"
#include "lumenis/ui/theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

class TestCutListUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void cutListsInTheTool() {
        model::BinItem a;
        a.id = QStringLiteral("A");
        a.name = QStringLiteral("Scene 4");
        a.editRate = Rational(24);
        a.durationFrames = 500;
        a.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1"));
        a.fields.insert(QStringLiteral("KN Start"), QStringLiteral("KZ 12 3456 0100+00"));
        model::Sequence seq;
        seq.setTimecodeTrack(false);
        model::Segment s;
        s.clipId = a.id;
        s.binId = QStringLiteral("bin");
        s.sourceTrack = QStringLiteral("V1");
        s.sourceIn = 40;
        s.length = 80;
        seq.ensureTrack(QStringLiteral("V1"))->segments = {s};
        model::BinItem item;
        item.id = QStringLiteral("seq");
        item.name = QStringLiteral("Reel 2");
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(24);
        seq.storeIn(item);

        ListTool tool;
        tool.show();
        tool.setSequence(item, [a](const MobId&, const QString& id) -> std::optional<model::BinItem> {
            if (id != a.id) return std::nullopt;
            return a;
        });
        // The cut list types are written.
        auto* formats = qobject_cast<QStandardItemModel*>(tool.outputFormat()->model());
        QVERIFY(formats->item(tool.outputFormat()->findText(QStringLiteral("Columnar")))->isEnabled());
        QVERIFY(formats->item(tool.outputFormat()->findText(QStringLiteral("TabbedLists")))->isEnabled());
        QVERIFY(formats->item(tool.outputFormat()->findText(QStringLiteral("XML")))->isEnabled());
        QVERIFY(formats->item(tool.outputFormat()->findText(QStringLiteral("WebLists")))->isEnabled());
        // "The options in the List Options and Formatting tabs change depending on which Output Format you select".
        QVERIFY(tool.cutListOptionsBox()->isHidden());
        tool.outputFormat()->setCurrentIndex(tool.outputFormat()->findText(QStringLiteral("Columnar")));
        QVERIFY(!tool.cutListOptionsBox()->isHidden());
        QVERIFY(tool.isCutList());
        tool.previewButton()->click();
        QString list = tool.masterList()->toPlainText();
        QVERIFY2(list.startsWith(QStringLiteral("Reel 2\n\nASSEMBLE LIST")), qPrintable(list));
        // 35mm 4 perf by default: 16 frames a foot, 80 frames = 5+00, from key number 100+00 + 40 frames.
        QVERIFY2(list.contains(QStringLiteral("5+00")) && list.contains(QStringLiteral("KZ 12 3456 102+08")), qPrintable(list));
        // 16mm counts 40 frames a foot.
        tool.setFilmType(QStringLiteral("16mm"));
        tool.findChild<QCheckBox*>(QStringLiteral("listPull"))->setChecked(true);
        tool.previewButton()->click();
        list = tool.masterList()->toPlainText();
        QVERIFY2(list.contains(QStringLiteral("2+00")) && list.contains(QStringLiteral("PULL LIST")), qPrintable(list));
        // Save List writes the list as shown.
        QTemporaryDir dir;
        const QString out = dir.filePath(QStringLiteral("reel2.txt"));
        QString suggested;
        tool.savePath = [&](const QString& name) {
            suggested = name;
            return out;
        };
        tool.saveButton()->click();
        QCOMPARE(suggested, QStringLiteral("Reel 2.txt"));
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()).trimmed(), list.trimmed());
        // WebLists: the pages saved in the chosen folder and the assemble list shown (p.957-958).
        tool.outputFormat()->setCurrentIndex(tool.outputFormat()->findText(QStringLiteral("WebLists")));
        QString opened;
        tool.openPage = [&](const QString& page) { opened = page; };
        const QString web = dir.filePath(QStringLiteral("web/CutAsm.htm"));
        tool.savePath = [&](const QString& name) {
            suggested = name;
            return web;
        };
        tool.saveButton()->click();
        QCOMPARE(suggested, QStringLiteral("Reel 2/CutAsm.htm"));
        QCOMPARE(opened, web);
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("web/Files/cutlist.css"))));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("web/CutPull.htm"))));
        // XML previews as the XML text.
        tool.outputFormat()->setCurrentIndex(tool.outputFormat()->findText(QStringLiteral("XML")));
        tool.previewButton()->click();
        QVERIFY(tool.masterList()->toPlainText().startsWith(QStringLiteral("<?xml")));
        // Back to an EDL type: the Cut List options go.
        tool.outputFormat()->setCurrentIndex(tool.outputFormat()->findText(QStringLiteral("CMX_3600")));
        QVERIFY(tool.cutListOptionsBox()->isHidden());
    }
};

QTEST_MAIN(TestCutListUi)
#include "tst_cut_list_ui.moc"
