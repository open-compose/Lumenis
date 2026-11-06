// Tools > Video Output Tool (7.3b; docs/spec/video-output.md, Editing Guide p.910-917, p.1225-1227): not available
// without hardware; with a device (a fake provider here) its sync source, output signal and calibration reach the
// device; the preset buttons; the test patterns; the values kept as the project's Video Output setting, Save As making
// a named version; the Audio Project setting's Output Gain.
#include "lumenis/ext/video_output.h"
#include "lumenis/media/test_patterns.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/video_output_tool.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>

#include <map>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

// What the fake device was told.
struct Calls {
    std::string sync;
    std::string signal;
    std::map<std::string, double> calibration;
    int frames = 0;
    QImage last;
};

class FakeSession : public ext::VideoOutputSession {
public:
    explicit FakeSession(Calls& calls) : calls_(calls) {}
    void setSyncSource(const std::string& s) override { calls_.sync = s; }
    void setOutputSignal(const std::string& s) override { calls_.signal = s; }
    void setCalibration(const std::string& name, double value) override { calls_.calibration[name] = value; }
    void showFrame(const unsigned char* rgba, int w, int h, int stride) override {
        ++calls_.frames;
        calls_.last = QImage(rgba, w, h, stride, QImage::Format_RGBA8888).copy();
    }

private:
    Calls& calls_;
};

class FakeProvider : public ext::VideoOutputProvider {
public:
    explicit FakeProvider(Calls& calls) : calls_(calls) {}
    ext::ProviderInfo info() const override { return {"test.video-output", "Test output", ext::Category::VideoIoDevice, ext::LicenseTier::OpenSource, "Test", "1"}; }
    std::vector<ext::VideoOutputDeviceInfo> devices() const override {
        ext::VideoOutputDeviceInfo d;
        d.id = "box";
        d.name = "Test Box";
        d.syncSources = {"Internal", "Reference", "TriLevel"};
        return {d};
    }
    std::unique_ptr<ext::VideoOutputSession> open(const std::string&, std::string*) override { return std::make_unique<FakeSession>(calls_); }

private:
    Calls& calls_;
};

} // namespace

class TestVideoOutput : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void testPatterns() {
        // SMPTE bars: 75% white at the left of the top, blue at the right; full-field bars at 75% and 100%.
        const QImage smpte = media::testPattern(QStringLiteral("SMPTE_Bars"), QSize(700, 480));
        QCOMPARE(smpte.size(), QSize(700, 480));
        QCOMPARE(smpte.pixelColor(10, 10), QColor(191, 191, 191));
        QCOMPARE(smpte.pixelColor(690, 10), QColor(0, 0, 191));
        QCOMPARE(media::testPattern(QStringLiteral("ColorBars"), QSize(70, 10)).pixelColor(15, 5), QColor(191, 191, 0));
        QCOMPARE(media::testPattern(QStringLiteral("ColorBars_100"), QSize(70, 10)).pixelColor(15, 5), QColor(255, 255, 0));
        QVERIFY(media::testPattern(QStringLiteral("Nothing"), QSize(70, 10)).isNull());
    }

    void tool() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Harbor"));
        project.settings.name = QStringLiteral("Harbor");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        VideoOutputTool& tool = window.videoOutputTool();

        // Software only: not available (p.1225).
        commands.action(QStringLiteral("tools.video_output_tool"))->trigger();
        QVERIFY(!tool.isAvailable());
        QVERIFY(tool.findChild<QLabel*>(QStringLiteral("voUnavailable"))->isVisibleTo(&tool));

        Calls calls;
        providers.add(std::make_shared<FakeProvider>(calls));
        commands.action(QStringLiteral("tools.video_output_tool"))->trigger();
        QVERIFY(tool.isAvailable());
        QCOMPARE(calls.sync, std::string("Internal"));
        QCOMPARE(calls.signal, std::string("Component"));
        QCOMPARE(calls.calibration.at("Black"), 0.0);

        // Sync Lock and the SD output.
        tool.findChild<QComboBox*>(QStringLiteral("voSyncLock"))->setCurrentText(QStringLiteral("Reference"));
        QCOMPARE(calls.sync, std::string("Reference"));
        tool.findChild<QComboBox*>(QStringLiteral("voSdOutput"))->setCurrentText(QStringLiteral("Composite"));
        QCOMPARE(calls.signal, std::string("Composite"));

        // The preset buttons (p.913): lit at first; clicking the slider dims it and shows the manual level (0 at first);
        // moving it sets the level; the lit preset brings back the factory level, and the manual one is kept.
        QVERIFY(tool.presetLit(QStringLiteral("Hue")));
        tool.moveSlider(QStringLiteral("Hue"), 40);
        QVERIFY(!tool.presetLit(QStringLiteral("Hue")));
        QCOMPARE(tool.sliderValue(QStringLiteral("Hue")), 0.0);
        tool.moveSlider(QStringLiteral("Hue"), 40);
        QCOMPARE(tool.sliderValue(QStringLiteral("Hue")), 40.0);
        QCOMPARE(calls.calibration.at("Hue"), 40.0);
        tool.clickPreset(QStringLiteral("Hue"));
        QVERIFY(tool.presetLit(QStringLiteral("Hue")));
        QCOMPARE(calls.calibration.at("Hue"), 0.0);
        tool.clickPreset(QStringLiteral("Hue"));
        QCOMPARE(calls.calibration.at("Hue"), 40.0);

        // A test pattern goes to the output (the Client monitor, p.915).
        const int before = calls.frames;
        tool.chooseTestPattern(QStringLiteral("SMPTE_Bars"));
        QVERIFY(calls.frames > before);
        QCOMPARE(calls.last.size(), QSize(1920, 1080));

        // The values are the project's Video Output setting; Save As makes a version of that name.
        const model::SettingsFile& f = window.settings().file(model::SettingsScope::Project);
        const int active = f.activeIndex(QStringLiteral("Video Output"));
        QVERIFY(active >= 0);
        QCOMPARE(f.entries.at(active).values.value(QStringLiteral("syncLock")).toString(), QStringLiteral("Reference"));
        QCOMPARE(f.entries.at(active).values.value(QStringLiteral("calibration")).toObject().value(QStringLiteral("Hue")).toObject()
                     .value(QStringLiteral("manual")).toInt(), 40);
        window.storeVideoOutput(tool.values(), QStringLiteral("Edit Suite 2"));
        const model::SettingsFile& g = window.settings().file(model::SettingsScope::Project);
        QCOMPARE(g.entries.at(g.activeIndex(QStringLiteral("Video Output"))).name, QStringLiteral("Edit Suite 2"));

        // The tool opened again reads them back.
        tool.setValues({});
        QCOMPARE(tool.sliderValue(QStringLiteral("Hue")), 0.0);
        commands.action(QStringLiteral("tools.video_output_tool"))->trigger();
        QCOMPARE(tool.sliderValue(QStringLiteral("Hue")), 40.0);

        // Output Gain (p.1150).
        QCOMPARE(window.outputGainDb(), 0.0);
        window.settings().setValue(QStringLiteral("Audio Project"), QStringLiteral("outputGain"), -6.0);
        QCOMPARE(window.outputGainDb(), -6.0);
    }
};

QTEST_MAIN(TestVideoOutput)
#include "tst_video_output.moc"
