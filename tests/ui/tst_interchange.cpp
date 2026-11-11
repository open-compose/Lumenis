#include "lumenis/ext/provider.h"
#include "lumenis/interop/edl.h"
#include "lumenis/interop/fcp_xml.h"
#include "lumenis/interop/otio.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/export_dialog.h"
#include "lumenis/ui/interchange_dialogs.h"
#include "lumenis/ui/list_tool.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/tool_windows.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QUrl>

// The EDL and shot log entry points of the application (Phase 3.6a): File > Input > Import EDL
// (p.951), Tools > List Tool (p.944-960), shot logs imported through the Source Browser's Import
// (p.131-132), File > Output > Export Bin (p.130) and Export to File as Avid Log Exchange (p.1174);
// and OpenTimelineIO (Phase 3.6b, p.969): Import Media, a drop into a bin, and the List Tool's OTIO
// output with the Default OpenTimelineIO setting; and FCP 7 XML (Phase 3.6c, ours): Import Media, a
// drop into a bin and Export to File.

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// A window with a 25 fps project open in `dir`.
struct Session {
    ext::ProviderRegistry providers;
    CommandRegistry commands;
    std::unique_ptr<MainWindow> window;

    explicit Session(const QTemporaryDir& dir) {
        commands.loadBuiltin();
        window = std::make_unique<MainWindow>(commands, providers);
        window->resize(1200, 760);
        window->show();
        window->settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Harbor"));
        project.settings.name = QStringLiteral("Harbor");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window->setProject(project);
        QCoreApplication::processEvents();
    }
};

const QByteArray kEdl = QByteArrayLiteral(
    "TITLE:   Harbor Cut\r\n"
    "FCM: NON-DROP FRAME\r\n"
    "001  DAY1     AA/V  C        10:00:00:00 10:00:02:00 01:00:00:00 01:00:02:00\r\n"
    "* FROM CLIP NAME:  Wide\r\n"
    "002  DAY2     V     C        11:00:00:00 11:00:01:00 01:00:02:00 01:00:03:00\r\n"
    "* FROM CLIP NAME:  Close\r\n");

const QByteArray kLog = QByteArrayLiteral(
    "Heading\r\nFIELD_DELIM\tTABS\r\nVIDEO_FORMAT\t1080\r\nFPS\t25\r\n\r\n"
    "Column\r\nName\tTracks\tStart\tEnd\tTape\tScene\tCircled\r\n\r\nData\r\n"
    "Take 1\tVA1A2\t10:00:00:00\t10:00:10:00\tDAY1\t4\tyes\r\n"
    "Take 2\tVA1A2\t10:00:10:00\t10:00:20:00\tDAY1\t4\t\r\n");

// An OTIO timeline at 25 fps: two clips of two files on V1, joined by a 10-frame dissolve.
const QByteArray kOtio = QByteArrayLiteral(
    "{\n"
    "  \"OTIO_SCHEMA\": \"Timeline.1\", \"name\": \"Harbor Turnover\",\n"
    "  \"global_start_time\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 25, \"value\": 90000},\n"
    "  \"tracks\": {\"OTIO_SCHEMA\": \"Stack.1\", \"name\": \"tracks\", \"children\": [\n"
    "    {\"OTIO_SCHEMA\": \"Track.1\", \"name\": \"V1\", \"kind\": \"Video\", \"children\": [\n"
    "      {\"OTIO_SCHEMA\": \"Clip.1\", \"name\": \"Wide\",\n"
    "       \"source_range\": {\"OTIO_SCHEMA\": \"TimeRange.1\",\n"
    "         \"start_time\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 25, \"value\": 50},\n"
    "         \"duration\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 25, \"value\": 100}},\n"
    "       \"media_reference\": {\"OTIO_SCHEMA\": \"ExternalReference.1\", \"target_url\": \"file:///media/wide.mov\",\n"
    "         \"available_range\": {\"OTIO_SCHEMA\": \"TimeRange.1\",\n"
    "           \"start_time\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 25, \"value\": 0},\n"
    "           \"duration\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 25, \"value\": 500}}}},\n"
    "      {\"OTIO_SCHEMA\": \"Transition.1\", \"transition_type\": \"SMPTE_Dissolve\",\n"
    "       \"in_offset\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 25, \"value\": 5},\n"
    "       \"out_offset\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 25, \"value\": 5}},\n"
    "      {\"OTIO_SCHEMA\": \"Clip.1\", \"name\": \"Close\",\n"
    "       \"source_range\": {\"OTIO_SCHEMA\": \"TimeRange.1\",\n"
    "         \"start_time\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 25, \"value\": 0},\n"
    "         \"duration\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 25, \"value\": 75}},\n"
    "       \"media_reference\": {\"OTIO_SCHEMA\": \"ExternalReference.1\", \"target_url\": \"file:///media/close.mov\",\n"
    "         \"available_range\": {\"OTIO_SCHEMA\": \"TimeRange.1\",\n"
    "           \"start_time\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 25, \"value\": 0},\n"
    "           \"duration\": {\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": 25, \"value\": 300}}}}\n"
    "    ]}\n"
    "  ]}\n"
    "}\n");

// An FCP 7 XML sequence at 25 fps: two clips of two files on V1, joined by a 10-frame dissolve.
const QByteArray kFcpXml = QByteArrayLiteral(
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE xmeml>\n<xmeml version=\"5\"><sequence id=\"s\">"
    "<name>Harbor XML</name><rate><timebase>25</timebase><ntsc>FALSE</ntsc></rate>"
    "<timecode><rate><timebase>25</timebase></rate><frame>90000</frame></timecode><media><video><track>"
    "<clipitem id=\"a\"><name>Wide</name><start>0</start><end>-1</end><in>50</in><out>155</out>"
    "<file id=\"f1\"><name>wide.mov</name><pathurl>file:///media/wide.mov</pathurl><duration>500</duration></file></clipitem>"
    "<transitionitem><start>95</start><end>105</end><alignment>center</alignment>"
    "<effect><name>Cross Dissolve</name><effectid>Cross Dissolve</effectid></effect></transitionitem>"
    "<clipitem id=\"b\"><name>Close</name><start>-1</start><end>175</end><in>0</in><out>80</out>"
    "<file id=\"f2\"><name>close.mov</name><pathurl>file:///media/close.mov</pathurl><duration>300</duration></file></clipitem>"
    "</track></video></media></sequence></xmeml>\n");

int countOf(const BinDocument& bin, model::BinItemKind kind) {
    return static_cast<int>(std::count_if(bin.bin().items.begin(), bin.bin().items.end(),
                                          [kind](const model::BinItem& i) { return i.kind == kind; }));
}

} // namespace

class TestInterchange : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }
    // A font family asked for by a name the system lacks makes Qt build its family aliases (a quarter
    // of a second on macOS), which stalls the window: use QFontDatabase::systemFont (list_tool.cpp).
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("Populating font family aliases"))); }

    // File > Input > Import EDL: the dialog's file, Frame Rate and bin; a sequence and a source for
    // each reel go into the bin. Imported again, the reels' sources are used again.
    void importEdl() {
        QTemporaryDir dir;
        const QString edl = dir.filePath(QStringLiteral("cut.edl"));
        writeFile(edl, kEdl);
        Session s(dir);
        MainWindow& window = *s.window;
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        QAction* action = s.commands.action(QStringLiteral("file.input.import_edl"));
        QVERIFY(action && action->isEnabled());
        Rational offered;
        window.setImportEdlHook([&](ImportEdlDialog& dialog) {
            offered = dialog.rate();
            // The project's bins are offered, and New Bin.
            if (dialog.binMenu()->findData(binId) < 0 || dialog.binMenu()->itemText(0) != QLatin1String("New Bin")) return false;
            dialog.setPath(edl);
            dialog.setBin(binId);
            return true;
        });
        action->trigger();
        QCOMPARE(offered, Rational(25)); // the project's rate first
        QCOMPARE(bin->bin().items.size(), 3);
        const model::BinItem* sequence = nullptr;
        int sources = 0;
        for (const model::BinItem& item : bin->bin().items) {
            if (item.kind == model::BinItemKind::Sequence) sequence = &item;
            if (item.kind == model::BinItemKind::Source) ++sources;
        }
        QVERIFY(sequence);
        QCOMPARE(sources, 2);
        QCOMPARE(sequence->name, QStringLiteral("Harbor Cut"));
        QCOMPARE(sequence->editRate, Rational(25));
        const model::Sequence cut = model::Sequence::fromItem(*sequence);
        QCOMPARE(cut.trackNames(), (QStringList{QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")}));
        QCOMPARE(cut.track(QStringLiteral("V1"))->segments.first().binId, binId);
        // The sequence is selected in the bin.
        QCOMPARE(window.binContainer().viewFor(bin)->selectedIds(), QStringList{sequence->id});
        // The second time, the same tapes' sources serve.
        action->trigger();
        QCOMPARE(bin->bin().items.size(), 4);
        // New Bin makes a bin for it.
        const int before = static_cast<int>(window.binContainer().bins()->bins().size());
        window.setImportEdlHook([&](ImportEdlDialog& dialog) {
            dialog.setPath(edl);
            dialog.setBin(QString());
            return true;
        });
        QCOMPARE(window.importEdl().size(), 3);
        QCOMPARE(static_cast<int>(window.binContainer().bins()->bins().size()), before + 1);
        // Cancel imports nothing.
        window.setImportEdlHook([](ImportEdlDialog&) { return false; });
        QVERIFY(window.importEdl().isEmpty());
    }

    // Tools > List Tool: the selected sequence is loaded (p.948 step 3); Preview makes the list in the
    // chosen Output Format, of the tracks switched on; Save List writes it.
    void listTool() {
        QTemporaryDir dir;
        const QString edl = dir.filePath(QStringLiteral("cut.edl"));
        writeFile(edl, kEdl);
        Session s(dir);
        MainWindow& window = *s.window;
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        window.setImportEdlHook([&](ImportEdlDialog& dialog) {
            dialog.setPath(edl);
            dialog.setBin(binId);
            return true;
        });
        window.importEdl();
        QAction* action = s.commands.action(QStringLiteral("tools.list_tool"));
        QVERIFY(action && action->isEnabled());
        action->trigger();
        Panel* panel = window.panels().panel(QStringLiteral("ListTool"));
        QVERIFY(panel && window.panels().isOpen(panel));
        ListTool& tool = window.listTool();
        QVERIFY(tool.hasSequence());
        QCOMPARE(tool.title()->text(), QStringLiteral("Harbor Cut"));
        QVERIFY(tool.trackButton(QStringLiteral("A2")));
        // The formats not written are dimmed.
        auto* formats = qobject_cast<QStandardItemModel*>(tool.outputFormat()->model());
        QVERIFY(formats);
        QVERIFY(formats->item(tool.outputFormat()->findText(QStringLiteral("CMX_3600")))->isEnabled());
        QVERIFY(formats->item(tool.outputFormat()->findText(QStringLiteral("File_129")))->isEnabled());
        QVERIFY(!formats->item(tool.outputFormat()->findText(QStringLiteral("Cuedos")))->isEnabled());
        tool.previewButton()->click();
        const QString list = tool.masterList()->toPlainText();
        QVERIFY2(list.startsWith(QStringLiteral("TITLE:   HARBOR CUT\nFCM: NON-DROP FRAME\n")), qPrintable(list));
        QVERIFY2(list.contains(QStringLiteral("001  DAY1     V     C        10:00:00:00 10:00:02:00 01:00:00:00 01:00:02:00")),
                 qPrintable(list));
        QVERIFY(tool.sourcesList()->toPlainText().contains(QStringLiteral("DAY2")));
        // Optimize EDL, and only V1 and A1.
        tool.optimizeBox()->setChecked(true);
        tool.trackButton(QStringLiteral("A2"))->setChecked(false);
        tool.startEvent()->setValue(10);
        tool.previewButton()->click();
        QVERIFY2(tool.masterList()->toPlainText().contains(QStringLiteral("010  DAY1     B     C")), qPrintable(tool.masterList()->toPlainText()));
        // Save List, as File_129.
        tool.outputFormat()->setCurrentIndex(tool.outputFormat()->findText(QStringLiteral("File_129")));
        const QString out = dir.filePath(QStringLiteral("out.edl"));
        QString suggested;
        tool.savePath = [&](const QString& name) {
            suggested = name;
            return out;
        };
        tool.saveButton()->click();
        QCOMPARE(suggested, QStringLiteral("Harbor Cut.edl"));
        const QByteArray saved = readFile(out);
        QVERIFY2(saved.contains("010  DAY1     B     C"), saved.constData());
        QVERIFY(ConsoleLog::instance().lines().last().contains(QStringLiteral("saved")));
        // What it saved reads back as the sequence's video and first audio track.
        const auto back = interop::edl::importFile(out, {});
        QVERIFY(back);
        QCOMPARE(model::Sequence::fromItem(*back->sequence()).trackNames(), (QStringList{QStringLiteral("V1"), QStringLiteral("A1")}));
        // Clear empties the tool; Load takes the Record monitor's sequence.
        tool.clearSequence();
        QVERIFY(!tool.hasSequence());
        for (const model::BinItem& item : bin->bin().items) {
            if (item.kind == model::BinItemKind::Sequence) window.composer().load(MonitorId::Record, binId, item);
        }
        tool.loadButton()->click();
        QVERIFY(tool.hasSequence());
        Q_UNUSED(bin);
    }

    // Shot logs: imported through the Source Browser's Import (p.131-132), written by Export Bin
    // (p.130) and by Export to File with Avid Log Exchange (p.1174).
    void shotLogs() {
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("Day 1.ale"));
        writeFile(log, kLog);
        Session s(dir);
        MainWindow& window = *s.window;
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        const QStringList ids = window.importFiles(binId, {log});
        QCOMPARE(ids.size(), 2);
        QCOMPARE(bin->bin().items.size(), 2);
        const model::BinItem& take = bin->bin().items.first();
        QCOMPARE(take.name, QStringLiteral("Take 1"));
        QCOMPARE(take.fields.value(QStringLiteral("Tape")), QStringLiteral("DAY1"));
        QCOMPARE(take.fields.value(QStringLiteral("Tracks")), QStringLiteral("V1 A1-2"));
        QCOMPARE(take.durationFrames, std::int64_t{250});
        QVERIFY(bin->bin().display.customColumns.contains(QStringLiteral("Circled")));

        // File > Output > Export Bin.
        QAction* exportBin = s.commands.action(QStringLiteral("file.output.export_bin"));
        QVERIFY(exportBin && exportBin->isEnabled());
        const QString binLog = dir.filePath(QStringLiteral("bin.ale"));
        QString proposed;
        window.setExportBinHook([&](ExportBinDialog& dialog) {
            proposed = dialog.path();
            if (dialog.format() != QLatin1String("ale")) return false;
            dialog.setPath(binLog);
            return true;
        });
        exportBin->trigger();
        QVERIFY2(proposed.endsWith(QLatin1String(".ale")), qPrintable(proposed));
        const QByteArray written = readFile(binLog);
        QVERIFY2(written.startsWith("Heading\r\nFIELD_DELIM\tTABS\r\n"), written.constData());
        QVERIFY(written.contains("Take 2\tVA1A2\t10:00:10:00\t10:00:20:00"));
        QVERIFY(written.contains("Circled"));

        // Export to File with the Export setting's Export As at Avid Log Exchange: the selection.
        window.settings().setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("ale"));
        window.binContainer().viewFor(bin)->selectIds({ids.last()});
        const QString selection = dir.filePath(QStringLiteral("selection.ale"));
        window.setExportHook([&](ExportAsDialog& dialog) {
            proposed = dialog.pathEdit()->text();
            dialog.setPath(selection);
            return true;
        });
        s.commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        QVERIFY2(proposed.endsWith(QLatin1String("Take 2.ale")), qPrintable(proposed));
        const QByteArray one = readFile(selection);
        QVERIFY(one.contains("Take 2"));
        QVERIFY(!one.contains("Take 1"));

        // Options (p.869-870): the Export Settings dialog box with Save and Save As; Save As makes a new Export setting
        // with the Setting Name given, chosen in the Export Setting menu and in use for the export.
        const QString viaOptions = dir.filePath(QStringLiteral("options.ale"));
        QStringList menu;
        bool hadButtons = false;
        window.setExportOptionsHook([&](SettingsDialog& options) {
            hadButtons = options.findChild<QPushButton*>(QStringLiteral("settingSave")) &&
                         options.findChild<QPushButton*>(QStringLiteral("settingSaveAs"));
            options.saveAs(QStringLiteral("Shot Logs"));
            return true;
        });
        window.setExportHook([&](ExportAsDialog& dialog) {
            dialog.optionsButton()->click();
            auto* settingMenu = dialog.findChild<QComboBox*>();
            for (int i = 0; settingMenu && i < settingMenu->count(); ++i) menu << settingMenu->itemText(i);
            if (!settingMenu || settingMenu->currentText() != QLatin1String("Shot Logs")) return false;
            dialog.setPath(viaOptions);
            return true;
        });
        s.commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        QVERIFY(hadButtons);
        QVERIFY2(menu.contains(QStringLiteral("Shot Logs")), qPrintable(menu.join(QStringLiteral(", "))));
        QVERIFY(readFile(viaOptions).contains("Take 2"));
        const model::SettingsFile& user = window.settings().file(model::SettingsScope::User);
        const int now = user.activeIndex(QStringLiteral("Export"));
        QVERIFY(now >= 0);
        QCOMPARE(user.entries.at(now).name, QStringLiteral("Shot Logs"));
        QCOMPARE(user.entries.at(now).values.value(QStringLiteral("exportAs")).toString(), QStringLiteral("ale"));
        window.setExportOptionsHook({});
    }

    // OpenTimelineIO (p.969): Import Media and a drop into a bin import a sequence and offline master
    // clips; the List Tool writes the sequence with the OTIO output format, which goes with the
    // Default OpenTimelineIO setting.
    void otio() {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("turnover.otio"));
        writeFile(file, kOtio);
        Session s(dir);
        MainWindow& window = *s.window;
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);

        // File > Input > Import Media (p.969 step 2).
        const QStringList ids = window.importFiles(binId, {file});
        QCOMPARE(ids.size(), 3);
        QCOMPARE(countOf(*bin, model::BinItemKind::MasterClip), 2);
        QCOMPARE(countOf(*bin, model::BinItemKind::Sequence), 1);
        const model::BinItem* sequence = nullptr;
        for (const model::BinItem& item : bin->bin().items) {
            if (item.kind == model::BinItemKind::Sequence) sequence = &item;
            if (item.kind == model::BinItemKind::MasterClip) {
                // Offline, with its file kept for Relink.
                QVERIFY(item.link.isEmpty());
                QVERIFY(!item.fields.value(QStringLiteral("Source File")).isEmpty());
            }
        }
        QVERIFY(sequence);
        // A copy: the drop below adds items, which may move the bin's list.
        const model::BinItem turnover = *sequence;
        QCOMPARE(turnover.name, QStringLiteral("Harbor Turnover"));
        const model::Sequence cut = model::Sequence::fromItem(turnover);
        QCOMPARE(cut.trackNames(), (QStringList{QStringLiteral("V1")}));
        QVERIFY(ConsoleLog::instance().lines().join(QLatin1Char('\n')).contains(QStringLiteral("turnover.otio: 1 sequence(s), 2 clip(s)")));

        // Dropped into the bin from the desktop (step 1): another sequence, the same two clips.
        QMimeData drop;
        drop.setUrls({QUrl::fromLocalFile(file)});
        BinItemModel* model = window.binContainer().viewFor(bin)->itemModel();
        QVERIFY(model->canDropMimeData(&drop, Qt::CopyAction, -1, -1, {}));
        QVERIFY(model->dropMimeData(&drop, Qt::CopyAction, -1, -1, {}));
        QCoreApplication::processEvents(); // the bin hands the files on once the drag has finished
        QCOMPARE(countOf(*bin, model::BinItemKind::Sequence), 2);
        QCOMPARE(countOf(*bin, model::BinItemKind::MasterClip), 2);

        // The List Tool: the Default OpenTimelineIO setting chooses the OTIO output format.
        window.composer().load(MonitorId::Record, binId, turnover);
        s.commands.action(QStringLiteral("tools.list_tool"))->trigger();
        ListTool& tool = window.listTool();
        tool.loadButton()->click();
        QVERIFY(tool.hasSequence());
        QCOMPARE(tool.activeSetting()->currentText(), QStringLiteral("Default EDL"));
        tool.activeSetting()->setCurrentText(QStringLiteral("Default OpenTimelineIO"));
        QCOMPARE(tool.outputFormat()->currentText(), QStringLiteral("OTIO"));
        QVERIFY(tool.isOtio());
        tool.previewButton()->click();
        QVERIFY2(tool.masterList()->toPlainText().contains(QStringLiteral("\"Timeline.1\"")), qPrintable(tool.masterList()->toPlainText()));
        // Save List (step 3): an .otio file that reads back as the same cut.
        const QString out = dir.filePath(QStringLiteral("out.otio"));
        QString suggested;
        tool.savePath = [&](const QString& name) {
            suggested = name;
            return out;
        };
        tool.saveButton()->click();
        QCOMPARE(suggested, QStringLiteral("Harbor Turnover.otio"));
        const auto back = interop::otio::importFile(out);
        QVERIFY(back);
        const model::BinItem& written = back->items.last();
        QCOMPARE(written.kind, model::BinItemKind::Sequence);
        const model::Sequence again = model::Sequence::fromItem(written);
        QCOMPARE(again.trackNames(), cut.trackNames());
        QCOMPARE(again.tracks().first().segments.size(), cut.tracks().first().segments.size());
        // The media keeps its file through the round trip: the clips are the same MobIDs.
        QCOMPARE(back->items.first().mobId, bin->bin().items.first().mobId);
        // An EDL format makes Default EDL the setting again; choosing OTIO makes it Default OpenTimelineIO.
        tool.outputFormat()->setCurrentText(QStringLiteral("CMX_3600"));
        QCOMPARE(tool.activeSetting()->currentText(), QStringLiteral("Default EDL"));
        tool.outputFormat()->setCurrentText(QStringLiteral("OTIO"));
        QCOMPARE(tool.activeSetting()->currentText(), QStringLiteral("Default OpenTimelineIO"));
    }

    // FCP 7 XML (ours, docs/spec/fcp-xml.md): Import Media and a drop into a bin import it as an
    // OTIO file is; Export to File with Export As at Final Cut Pro 7 XML writes the sequence selected.
    void fcpXml() {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("turnover.xml"));
        writeFile(file, kFcpXml);
        QVERIFY(interop::fcpxml::isFcpXmlFile(file));
        Session s(dir);
        MainWindow& window = *s.window;
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QCOMPARE(window.importFiles(binId, {file}).size(), 3);
        QCOMPARE(countOf(*bin, model::BinItemKind::MasterClip), 2);
        QCOMPARE(countOf(*bin, model::BinItemKind::Sequence), 1);
        QMimeData drop;
        drop.setUrls({QUrl::fromLocalFile(file)});
        BinItemModel* model = window.binContainer().viewFor(bin)->itemModel();
        QVERIFY(model->dropMimeData(&drop, Qt::CopyAction, -1, -1, {}));
        QCoreApplication::processEvents(); // the bin hands the files on once the drag has finished
        QCOMPARE(countOf(*bin, model::BinItemKind::Sequence), 2);
        QCOMPARE(countOf(*bin, model::BinItemKind::MasterClip), 2);

        // File > Output > Export to File, as Final Cut Pro 7 XML.
        QString sequenceId;
        for (const model::BinItem& item : bin->bin().items) {
            if (item.kind == model::BinItemKind::Sequence) sequenceId = item.id;
        }
        window.settings().setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("fcpxml"));
        window.binContainer().viewFor(bin)->selectIds({sequenceId});
        const QString out = dir.filePath(QStringLiteral("out.xml"));
        QString proposed;
        window.setExportHook([&](ExportAsDialog& dialog) {
            proposed = dialog.pathEdit()->text();
            dialog.setPath(out);
            return true;
        });
        s.commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        QVERIFY2(proposed.endsWith(QLatin1String("Harbor XML.xml")), qPrintable(proposed));
        QVERIFY(interop::fcpxml::isFcpXmlFile(out));
        const auto back = interop::fcpxml::importFile(out);
        QVERIFY(back);
        QCOMPARE(back->items.size(), 3);
        const QByteArray written = readFile(out);
        QVERIFY(written.contains("<transitionitem>"));
        QVERIFY(written.contains("file:///media/wide.mov"));
    }
};

QTEST_MAIN(TestInterchange)
#include "tst_interchange.moc"
