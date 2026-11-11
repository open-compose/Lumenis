// Printing (9.1; docs/spec/printing.md; Editing Guide p.292, p.424, p.645): what Print Bin puts on paper for the Text,
// Frame and Script views, pages and their heading rows, Print Frame's caption, Print Timeline, Page Setup's layout, and
// Print following the Console, the Markers window and the Media Tool. The Print dialog is replaced by a hook that prints to a PDF file.
#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/marker_dialogs.h"
#include "lumenis/ui/media_tool.h"
#include "lumenis/ui/printing.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QPageSize>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

bool writeMovie(const QString& path, QColor color) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 36;
    s.frameRate = Rational(25);
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    for (int i = 0; i < 10; ++i) {
        QImage image(64, 36, QImage::Format_RGBA8888);
        image.fill(color);
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

// Pixels that are not white: something was printed.
int inked(const QImage& page) {
    int n = 0;
    for (int y = 0; y < page.height(); ++y) {
        for (int x = 0; x < page.width(); ++x) {
            if (page.pixel(x, y) != qRgb(255, 255, 255)) ++n;
        }
    }
    return n;
}

QPageLayout letter() {
    return QPageLayout(QPageSize(QPageSize::Letter), QPageLayout::Portrait, QMarginsF(36, 36, 36, 36));
}

// A PDF file that a print wrote.
bool isPdf(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) && f.read(5) == "%PDF-" && f.size() > 500;
}

} // namespace

class TestPrinting : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // Pages of a long text, laid out at 96 page pixels an inch.
    void pages() {
        QString lines;
        for (int i = 0; i < 400; ++i) lines += QStringLiteral("line %1\n").arg(i);
        PrintDocument text = consoleDocument(lines);
        const QSizeF page = printablePageSize(letter());
        // 8.5 x 11 inches less half an inch all round.
        QCOMPARE(page.toSize(), QSize(720, 960));
        const int count = text.pageCount(page);
        QVERIFY2(count >= 4, qPrintable(QString::number(count)));
        const QList<QImage> images = renderPages(text, letter(), 48);
        QCOMPARE(images.size(), count);
        QCOMPARE(images.first().size(), QSize(408, 528)); // the full page at 48 dpi
        for (const QImage& image : images) QVERIFY(inked(image) > 0);
        // A picture fits one page, centred, with its caption.
        QImage wide(2000, 300, QImage::Format_RGB32);
        wide.fill(Qt::black);
        PrintDocument picture = PrintDocument::fromPicture(QStringLiteral("Wide"), wide, QStringLiteral("Caption"));
        QCOMPARE(picture.pageCount(page), 1);
        const QImage one = renderPages(picture, letter(), 48).value(0);
        // Scaled to the page's width: 720 page pixels are 360 pixels at 48 dpi, from x = 24 to 384.
        QCOMPARE(QColor(one.pixel(30, 30)), QColor(Qt::black));
        QCOMPARE(QColor(one.pixel(10, 30)), QColor(Qt::white));
        QCOMPARE(QColor(one.pixel(398, 30)), QColor(Qt::white));
        QCOMPARE(picture.text(), QStringLiteral("Caption"));
        QVERIFY(PrintDocument().isEmpty());
    }

    void printCommands() {
        QTemporaryDir dir;
        const QString harbor = dir.filePath(QStringLiteral("Harbor.mov"));
        const QString bridge = dir.filePath(QStringLiteral("Bridge.mov"));
        QVERIFY(writeMovie(harbor, Qt::darkBlue));
        QVERIFY(writeMovie(bridge, Qt::darkRed));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Print"));
        project.settings.name = QStringLiteral("Print");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();

        // The Print dialog's stand-in: prints to a PDF, counted.
        int dialogs = 0;
        int setups = 0;
        QString output;
        window.setPrintDialogHook([&](QPrinter& printer, bool pageSetup) {
            if (pageSetup) {
                ++setups;
                printer.setPageOrientation(QPageLayout::Landscape);
                return true;
            }
            ++dialogs;
            output = dir.filePath(QStringLiteral("print-%1.pdf").arg(dialogs));
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(output);
            return true;
        });
        auto run = [&](const char* id) {
            QAction* action = commands.action(QString::fromLatin1(id));
            QVERIFY(action);
            action->trigger();
            QCoreApplication::processEvents();
        };

        // Nothing to print yet: no bin, no clip, no sequence (they beep).
        window.binContainer().closeBin(window.binContainer().activeDocument());
        QVERIFY(!window.printFrame());
        QVERIFY(!window.printTimeline());
        QCOMPARE(dialogs, 0);

        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {harbor, bridge});
        QCOMPARE(ids.size(), 2);
        model::BinItem first = *bin->bin().item(ids.at(0));
        first.script = QStringLiteral("Wide of the harbor\nGulls");
        bin->replaceItem(first);
        BinView* view = window.binContainer().activeView();
        QVERIFY(view);

        // Text view: the bin's name, the headings, then the rows as shown.
        view->setMode(model::BinViewMode::Text);
        PrintDocument text = binDocument(*view);
        QVERIFY(!text.isEmpty());
        QVERIFY(text.text().startsWith(bin->name()));
        QVERIFY(text.text().contains(QStringLiteral("Name")));
        const qsizetype harborAt = text.text().indexOf(QStringLiteral("Harbor"));
        const qsizetype bridgeAt = text.text().indexOf(QStringLiteral("Bridge"));
        QVERIFY(harborAt > 0 && bridgeAt > 0);
        QCOMPARE(harborAt < bridgeAt, view->visibleIds().indexOf(ids.at(0)) < view->visibleIds().indexOf(ids.at(1)));
        // The script box's text is only on paper in Script view (p.253).
        QVERIFY(!text.text().contains(QStringLiteral("Gulls")));
        run("file.print_bin");
        QCOMPARE(dialogs, 1);
        QVERIFY(isPdf(output));

        // Frame view: the frames and their names.
        view->setMode(model::BinViewMode::Frame);
        PrintDocument frames = binDocument(*view);
        QVERIFY(frames.text().contains(QStringLiteral("Harbor")) && frames.text().contains(QStringLiteral("Bridge")));
        const QImage framePage = renderPages(frames, letter(), 96).value(0);
        // Harbor's dark blue frame is on the page.
        bool blue = false;
        for (int y = 0; y < framePage.height() && !blue; y += 2) {
            for (int x = 0; x < framePage.width() && !blue; x += 2) {
                const QColor c(framePage.pixel(x, y));
                blue = c.blue() > 100 && c.red() < 40 && c.green() < 40;
            }
        }
        QVERIFY(blue);

        // Script view: the script box's text.
        view->setMode(model::BinViewMode::Script);
        PrintDocument script = binDocument(*view);
        // With LUMENIS_SHOT_DIR, the first pages as pictures, to look at.
        if (const QString shots = qEnvironmentVariable("LUMENIS_SHOT_DIR"); !shots.isEmpty()) {
            renderPages(text, letter(), 96).value(0).save(shots + QStringLiteral("/print-text.png"));
            renderPages(frames, letter(), 96).value(0).save(shots + QStringLiteral("/print-frame.png"));
            renderPages(script, letter(), 96).value(0).save(shots + QStringLiteral("/print-script.png"));
        }
        QVERIFY(script.text().contains(QStringLiteral("Wide of the harbor")));
        QVERIFY(script.text().contains(QStringLiteral("Gulls")));

        // Page Setup changes the layout every print then uses.
        run("file.page_setup");
        QCOMPARE(setups, 1);
        QCOMPARE(window.printer().pageLayout().orientation(), QPageLayout::Landscape);

        // Print Frame: the active monitor's frame with the clip's name and timecode.
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), *bin->bin().item(ids.at(0)));
        composer.setActiveMonitor(MonitorId::Source);
        composer.state(MonitorId::Source).setPosition(3);
        run("file.print_frame");
        QCOMPARE(dialogs, 2);
        QVERIFY(isPdf(output));

        // Print Timeline: a sequence in the Record monitor.
        QVERIFY(!window.printTimeline());
        composer.state(MonitorId::Source).setPosition(0);
        QVERIFY(window.timeline().perform(QStringLiteral("edit.overwrite")) ||
                window.timeline().perform(QStringLiteral("edit.splice_in")));
        QVERIFY(composer.state(MonitorId::Record).current());
        run("file.print_timeline");
        QCOMPARE(dialogs, 3);
        QVERIFY(isPdf(output));

        // A cancelled Print dialog prints nothing.
        window.setPrintDialogHook([&](QPrinter&, bool) {
            ++dialogs;
            return false;
        });
        QVERIFY(!window.printTimeline());
        QCOMPARE(dialogs, 4);

        // In the Console the command reads Print and prints the log (p.424).
        run("tools.console");
        ConsoleWindow* console = nullptr;
        for (QWidget* w : QApplication::allWidgets()) {
            if (auto* c = qobject_cast<ConsoleWindow*>(w)) console = c;
        }
        QVERIFY(console);
        QApplication::setActiveWindow(console->window());
        console->log()->setFocus();
        QCoreApplication::processEvents();
        QTRY_COMPARE(window.printTarget(), MainWindow::PrintTarget::Console);
        QCOMPARE(commands.action(QStringLiteral("file.print_bin"))->text(), QStringLiteral("Print"));
        view->setFocus();
        QApplication::setActiveWindow(view->window());
        view->setFocus();
        QTRY_COMPARE(window.printTarget(), MainWindow::PrintTarget::Bin);
        QCOMPARE(commands.action(QStringLiteral("file.print_bin"))->text(), QStringLiteral("Print Bin"));

        // The Markers window prints its current view (p.424).
        run("tools.markers");
        MarkersWindow* markers = window.findChild<MarkersWindow*>();
        QVERIFY(markers);
        QApplication::setActiveWindow(markers);
        markers->table()->setFocus();
        QTRY_COMPARE(window.printTarget(), MainWindow::PrintTarget::Markers);
        int printed = 0;
        window.setPrintDialogHook([&](QPrinter& printer, bool) {
            ++printed;
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(dir.filePath(QStringLiteral("markers.pdf")));
            return true;
        });
        QVERIFY(window.printActive());
        QCOMPARE(printed, 1);
        QVERIFY(isPdf(dir.filePath(QStringLiteral("markers.pdf"))));

        // The Media Tool prints its table as a bin's Text view (p.345); the command keeps its name.
        window.setMediaToolDisplayHook([](MediaToolDisplayDialog&) { return true; });
        run("tools.media_tool");
        MediaTool& tool = window.mediaTool();
        QApplication::setActiveWindow(tool.window());
        tool.table()->setFocus();
        QTRY_COMPARE(window.printTarget(), MainWindow::PrintTarget::MediaTool);
        QCOMPARE(commands.action(QStringLiteral("file.print_bin"))->text(), QStringLiteral("Print Bin"));
        QVERIFY(tableDocument(QStringLiteral("Media Tool"), *tool.table()).text().startsWith(QStringLiteral("Media Tool")));
        QVERIFY(window.printActive());
        QCOMPARE(printed, 2);
    }
};

QTEST_MAIN(TestPrinting)
#include "tst_printing.moc"
