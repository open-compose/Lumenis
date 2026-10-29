// The last Bin settings (Editing Guide p.1153-1154; docs/spec/bin-settings.md): Frame View border
// colours and icons, Show local media icons, and Save single floating Bin Containers in Workspaces.
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/workspace.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

struct Window {
    QTemporaryDir dir;
    ext::ProviderRegistry providers;
    CommandRegistry commands;
    std::unique_ptr<MainWindow> window;
    Window() {
        commands.loadBuiltin();
        window = std::make_unique<MainWindow>(commands, providers);
        window->resize(1100, 700);
        window->settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window->setProject(project);
        settle();
    }
};

} // namespace

class TestBinSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void frameView() {
        // "Blue - Precomputes and source side motion effects; Green - Master clips; Dark Green - Subclips
        // and Group clips; Red - Sequences" (p.1154).
        using K = model::BinItemKind;
        QVERIFY(BinAppearance::typeColor(K::MotionEffect).blue() > BinAppearance::typeColor(K::MotionEffect).red());
        QVERIFY(BinAppearance::typeColor(K::MasterClip).green() > BinAppearance::typeColor(K::MasterClip).red());
        QVERIFY(BinAppearance::typeColor(K::Subclip).green() < BinAppearance::typeColor(K::MasterClip).green());
        QCOMPARE(BinAppearance::typeColor(K::Subclip), BinAppearance::typeColor(K::Group));
        QVERIFY(BinAppearance::typeColor(K::Sequence).red() > BinAppearance::typeColor(K::Sequence).green());
        QVERIFY(!BinAppearance::typeColor(K::Title).isValid());

        Window w;
        Settings& s = w.window->settings();
        QCOMPARE(BinAppearance::current().frameBorders, BinAppearance::FrameBorders::Type);
        QVERIFY(BinAppearance::current().frameIcons);
        s.setValue(QStringLiteral("Bin"), QStringLiteral("frameBorders"), QStringLiteral("clip"));
        s.setValue(QStringLiteral("Bin"), QStringLiteral("frameIcons"), false);
        settle();
        QCOMPARE(BinAppearance::current().frameBorders, BinAppearance::FrameBorders::ClipColor);
        QVERIFY(!BinAppearance::current().frameIcons);
    }

    void localMediaIcons() {
        // "Clip icons will appear blue to indicate the media is local" (p.1154).
        Window w;
        QVERIFY(BinAppearance::mediaTint());
        const QString file = w.dir.filePath("clip.mov");
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        model::BinItem clip;
        clip.id = QStringLiteral("c");
        clip.kind = model::BinItemKind::MasterClip;
        clip.link = QJsonObject{{QStringLiteral("path"), file}};
        qputenv("LUMENIS_SHARED_BINS", "none");
        const QColor local = BinAppearance::mediaTint()(clip);
        QVERIFY(local.isValid());
        QVERIFY(local.blue() > local.red());
        // On shared storage, or with the file gone, the icon stays as it is.
        qputenv("LUMENIS_SHARED_BINS", "all");
        QVERIFY(!BinAppearance::mediaTint()(clip).isValid());
        qputenv("LUMENIS_SHARED_BINS", "none");
        QFile::remove(file);
        QVERIFY(!BinAppearance::mediaTint()(clip).isValid());
        qunsetenv("LUMENIS_SHARED_BINS");
        // Off by default; the setting turns it on.
        QVERIFY(!BinAppearance::current().localMediaIcons);
        w.window->settings().setValue(QStringLiteral("Bin"), QStringLiteral("localMediaIcons"), true);
        settle();
        QVERIFY(BinAppearance::current().localMediaIcons);
    }

    void singleFloatingInWorkspaces() {
        // "Select this option if you want a floating Bin Container that is not tabbed or paneled with other
        // tools or Bin Containers to be saved into a workspace" (p.1153).
        Window w;
        MainWindow& win = *w.window;
        BinContainer& bins = win.binContainer();
        const QString a = bins.newBin();
        settle();
        win.settings().setValue(QStringLiteral("Bin"), QStringLiteral("saveSingleFloating"), true);
        settle();
        bins.floatBin(bins.document(a), QRect(80, 80, 420, 320));
        settle();
        WorkspaceManager& spaces = win.workspaces();
        spaces.saveCurrent();
        const QString home = spaces.active();
        // Docked again, then the workspace is applied again: the bin floats where it was.
        bins.dockBin(bins.document(a), nullptr, BinDropSide::Tab);
        settle();
        QCOMPARE(bins.floatingContainers().size(), 0);
        QString other;
        for (const QString& n : spaces.names()) {
            if (n != home && other.isEmpty()) other = n;
        }
        QVERIFY(spaces.activate(other));
        spaces.restoreToSaved();
        QVERIFY(spaces.activate(home));
        spaces.restoreToSaved();
        settle();
        QCOMPARE(bins.floatingContainers().size(), 1);
        QCOMPARE(bins.floatingContainers().first()->geometry().size(), QSize(420, 320));
    }
};

QTEST_MAIN(TestBinSettings)
#include "tst_bin_settings.moc"
