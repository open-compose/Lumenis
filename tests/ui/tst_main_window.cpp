#include "lumenis/core/branding.h"
#include "lumenis/core/project_format.h"
#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/menu_builder.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/workspace.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QMenuBar>
#include <QToolButton>

#include <functional>
#include <memory>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;

namespace {

// A project in a temporary folder.
struct Project {
    QTemporaryDir dir;
    model::ProjectEntry entry;

    Project() {
        model::ProjectSettings s;
        s.name = QStringLiteral("Doc");
        s.format = projectFormatPresets().front();
        entry = *model::ProjectStore(dir.path()).create(s);
    }
};

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

model::BinItem clip(const QString& name) {
    model::BinItem i;
    i.id = model::newItemId();
    i.name = name;
    i.durationFrames = 100;
    i.editRate = Rational(25);
    return i;
}

} // namespace

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void projectInTitleAndCloseProject() {
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        QAction* close = commands.action(QStringLiteral("file.close_project"));
        QVERIFY(close);
        QVERIFY(!close->isEnabled()); // no project yet

        model::ProjectEntry entry;
        entry.settings.name = QStringLiteral("Feature Film");
        window.setProject(entry);
        QVERIFY(window.windowTitle().contains(QStringLiteral("Feature Film")));
        QVERIFY(close->isEnabled());
        QCOMPARE(close->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+W")));

        QSignalSpy requested(&window, &ui::MainWindow::closeProjectRequested);
        close->trigger();
        QCOMPARE(requested.count(), 1);

        window.clearProject();
        QCOMPARE(window.windowTitle(), QString::fromUtf8(branding::kProductName));
        QVERIFY(!close->isEnabled());
    }

    // Guide p.74: Bin Container top left, Composer beside it, Timeline at
    // the bottom, workspace toolbar on the right.
    void opensWithTheDefaultPanels() {
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.resize(1280, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        ui::PanelManager& panels = window.panels();
        QStringList ids;
        for (const ui::PanelGroup* g : panels.mainHost()->groups()) ids << g->current()->id();
        QCOMPARE(ids, (QStringList{"BinContainer", "Composer", "Timeline"}));
        auto rect = [&](const char* id) {
            const QWidget* g = panels.panel(QString::fromLatin1(id))->group();
            return QRect(g->mapTo(&window, QPoint()), g->size());
        };
        QVERIFY(rect("BinContainer").right() < rect("Composer").left());
        QVERIFY(rect("Composer").bottom() < rect("Timeline").top());
        auto* bar = window.findChild<QWidget*>(QStringLiteral("workspaceBar"));
        QVERIFY(bar && bar->isVisible());
        QVERIFY(bar->geometry().left() >= rect("Timeline").right());
        // Same structure; sizes are pixels once laid out.
        std::function<QJsonObject(QJsonObject)> structure = [&](QJsonObject node) {
            node.remove(QStringLiteral("sizes"));
            QJsonArray children;
            for (const QJsonValue& c : node.value(QStringLiteral("children")).toArray()) {
                children.append(structure(c.toObject()));
            }
            if (!children.isEmpty()) node.insert(QStringLiteral("children"), children);
            return node;
        };
        QCOMPARE(structure(panels.saveLayout().value(QStringLiteral("main")).toObject()),
                 structure(ui::MainWindow::defaultLayout().value(QStringLiteral("main")).toObject()));
    }

    void windowsMenuCommands() {
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        ui::PanelManager& panels = window.panels();
        auto run = [&](const char* id) {
            QAction* a = commands.action(QString::fromLatin1(id));
            QVERIFY2(a && a->isEnabled(), id);
            a->trigger();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        };

        // File > Close closes the active window; Tools > Timeline reopens it.
        panels.activate(panels.panel(QStringLiteral("Timeline")));
        run("file.close");
        QVERIFY(!panels.isOpen(panels.panel(QStringLiteral("Timeline"))));
        run("tools.timeline");
        QVERIFY(panels.isOpen(panels.panel(QStringLiteral("Timeline"))));

        run("windows.add_blank_panel");
        QVERIFY(panels.activePanel()->isBlank());
        run("windows.float_active_panel");
        QCOMPARE(panels.floatingWindows().size(), 2); // the reopened Timeline and the blank panel
        run("windows.float_all_panels");
        QVERIFY(panels.mainHost()->isEmpty());

        // Workspaces: the list in Windows > Workspaces, and the commands.
        auto* menu = window.menuBar()->findChild<QMenu*>(QStringLiteral("menu:windows_workspaces"));
        QVERIFY(menu);
        QStringList listed;
        for (QAction* a : menu->actions()) {
            if (a->property(ui::kDynamicMenuItem).toBool() && !a->isSeparator()) listed << a->text();
        }
        QCOMPARE(listed, (QStringList{"Edit", "Color", "Effects", "Audio"}));
        QVERIFY(!commands.action(QStringLiteral("windows.workspaces.delete_workspace"))->isEnabled());
        run("windows.workspaces.restore_to_saved");
        QCOMPARE(panels.mainHost()->groups().size(), 3);
        QVERIFY(panels.floatingWindows().isEmpty());

        // Hiding the toolbar shows the workspace button instead (p.88).
        QAction* showBar = commands.action(QStringLiteral("windows.workspaces.show_workspace_toolbar"));
        QVERIFY(showBar->isCheckable() && showBar->isChecked());
        auto* button = window.findChild<QToolButton*>(QStringLiteral("workspaceButton"));
        QVERIFY(button && button->isHidden());
        showBar->trigger();
        QCOMPARE(window.workspaces().barMode(), ui::WorkspaceManager::BarMode::Hidden);
        QVERIFY(window.findChild<QWidget*>(QStringLiteral("workspaceBar"))->isHidden());
        QVERIFY(button->isVisible());
        QVERIFY(!button->menu()->actions().isEmpty());
        showBar->trigger();
        QVERIFY(button->isHidden());

        // The workspace list changes with the workspaces.
        window.workspaces().create(QStringLiteral("Logging"));
        QVERIFY(commands.action(QStringLiteral("windows.workspaces.delete_workspace"))->isEnabled());
        int dynamic = 0;
        for (QAction* a : menu->actions()) dynamic += a->property(ui::kDynamicMenuItem).toBool() ? 1 : 0;
        QCOMPARE(dynamic, 6); // five workspaces and a separator
        // ...but they are not part of the fixed inventory.
        const QByteArray dumped = QJsonDocument(ui::dumpMenus(window.menuBar())).toJson();
        QVERIFY(!dumped.contains("Logging"));
    }

    void aboutBoxShowsThePicture() {
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        std::unique_ptr<QMessageBox> box(window.createAboutBox());
        QVERIFY(box->text().contains(QString::fromUtf8(branding::kProductName)));
        const QPixmap picture = box->iconPixmap();
        QVERIFY(!picture.isNull());
        QCOMPARE(picture.deviceIndependentSize().toSize(),
                 QSize(ui::MainWindow::kAboutPictureSize, ui::MainWindow::kAboutPictureSize));
        QVERIFY(commands.action(QStringLiteral("help.about"))->isEnabled());
    }

    // Favorite Bins (p.293) and saved bin views (p.249) are the user's: they
    // are kept in the User settings and come back in the next session.
    void favoritesAndBinViewsAreUserSettings() {
        Project project;
        QTemporaryDir users;
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::FavoriteBins::paths().clear();
        ui::BinViewLibrary::views().clear();
        QString favorite;
        {
            ui::MainWindow window(commands, providers);
            window.settings().setUser(users.path(), QStringLiteral("ed"));
            window.setProject(project.entry);
            ui::BinContainer& bins = window.binContainer();
            QVERIFY(bins.addToFavorites(bins.activeDocument()));
            favorite = ui::FavoriteBins::paths().value(0);
            bins.activeView()->saveViewAs(QStringLiteral("Logging"));
            QCOMPARE(window.settings().value(QStringLiteral("Bin"), QStringLiteral("favoriteBins")).toArray(),
                     QJsonArray{favorite});
            window.clearProject();
        }
        ui::FavoriteBins::paths().clear();
        ui::BinViewLibrary::views().clear();
        {
            ui::MainWindow window(commands, providers);
            window.settings().setUser(users.path(), QStringLiteral("ed"));
            QCOMPARE(ui::FavoriteBins::paths(), QStringList{favorite});
            QVERIFY(ui::BinViewLibrary::find(QStringLiteral("Logging")));
            window.setProject(project.entry);
            ui::BinContainer& bins = window.binContainer();
            QVERIFY(bins.treeModel()->indexOf(ui::FavoriteBins::folderId()).isValid());
            QVERIFY(bins.activeView()->viewMenu()->findData(QStringLiteral("Logging")) >= 0);
            // Taking it off the list is saved too.
            QVERIFY(bins.deleteNode(ui::FavoriteBins::idFor(favorite)));
            QVERIFY(window.settings().value(QStringLiteral("Bin"), QStringLiteral("favoriteBins")).toArray().isEmpty());
            window.clearProject();
        }
        ui::FavoriteBins::paths().clear();
        ui::BinViewLibrary::views().clear();
    }

    // Lists an older build kept in QSettings move into the user's Bin setting once a user is
    // active, and a user who has lists of their own keeps them.
    void legacyBinListsMoveIntoUserSettings() {
        QTemporaryDir users;
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::FavoriteBins::paths().clear();
        ui::BinViewLibrary::views().clear();
        QSettings old(users.filePath(QStringLiteral("old.ini")), QSettings::IniFormat);
        old.setValue(QStringLiteral("favoriteBins/paths"), QStringList{QStringLiteral("/p/Bins/A.cthbin")});
        old.beginWriteArray(QStringLiteral("binViews"), 1);
        old.setArrayIndex(0);
        old.setValue(QStringLiteral("name"), QStringLiteral("Logging"));
        old.setValue(QStringLiteral("columns"), QStringList{QStringLiteral("Name"), QStringLiteral("Tape")});
        old.endArray();
        {
            ui::MainWindow window(commands, providers);
            window.adoptLegacyBinLists(ui::FavoriteBins::legacy(old), ui::BinViewLibrary::legacy(old));
            QVERIFY(window.legacyBinListsPending()); // no user yet: nowhere to write them
            window.settings().setUser(users.path(), QStringLiteral("ed"));
            QVERIFY(!window.legacyBinListsPending());
            QCOMPARE(window.settings().value(QStringLiteral("Bin"), QStringLiteral("favoriteBins")).toArray(),
                     QJsonArray{QStringLiteral("/p/Bins/A.cthbin")});
            QCOMPARE(ui::FavoriteBins::paths(), QStringList{QStringLiteral("/p/Bins/A.cthbin")});
            const auto* view = ui::BinViewLibrary::find(QStringLiteral("Logging"));
            QVERIFY(view);
            QCOMPARE(view->columns, (QStringList{QStringLiteral("Name"), QStringLiteral("Tape")}));
        }
        {
            // The user now has lists of their own: the old ones are not written over them.
            ui::MainWindow window(commands, providers);
            window.settings().setUser(users.path(), QStringLiteral("ed"));
            window.settings().setValue(QStringLiteral("Bin"), QStringLiteral("favoriteBins"), QJsonArray{});
            window.adoptLegacyBinLists(ui::FavoriteBins::legacy(old), QJsonValue());
            QVERIFY(!window.legacyBinListsPending());
            QVERIFY(window.settings().value(QStringLiteral("Bin"), QStringLiteral("favoriteBins")).toArray().isEmpty());
        }
        ui::FavoriteBins::forgetLegacy(old);
        ui::BinViewLibrary::forgetLegacy(old);
        QVERIFY(ui::FavoriteBins::legacy(old).isNull());
        QVERIFY(ui::BinViewLibrary::legacy(old).isNull());
        ui::FavoriteBins::paths().clear();
        ui::BinViewLibrary::views().clear();
    }

    // Playback hears a track only when it is not muted, and only soloed tracks once any is (p.698).
    void soloAndMuteDecideWhatIsHeard() {
        Project project;
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.setProject(project.entry);
        ui::BinDocument* doc = window.binContainer().activeDocument();
        QVERIFY(doc);
        model::BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        model::Sequence::withDefaultTracks().storeIn(item);
        doc->addItem(item);
        window.composer().load(ui::MonitorId::Record, doc->nodeId(), item);
        ui::AudioMixer& mixer = window.audioMixer();
        mixer.refresh();
        QVERIFY(mixer.strip(QStringLiteral("A1")) && mixer.strip(QStringLiteral("A2")));
        QVERIFY(window.isHeard(QStringLiteral("A1")) && window.isHeard(QStringLiteral("A2")));
        mixer.strip(QStringLiteral("A1"))->muteButton()->click();
        QVERIFY(!window.isHeard(QStringLiteral("A1")));
        QVERIFY(window.isHeard(QStringLiteral("A2")));
        mixer.strip(QStringLiteral("A1"))->muteButton()->click();
        // A soloed track is the only one heard.
        mixer.strip(QStringLiteral("A2"))->soloButton()->click();
        QVERIFY(!window.isHeard(QStringLiteral("A1")));
        QVERIFY(window.isHeard(QStringLiteral("A2")));
        window.clearProject();
    }

    // Mark Clip with segments selected in the Timeline marks from the start
    // of the first to the end of the last (p.622).
    void markClipMarksTheSelectedSegments() {
        Project project;
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.setProject(project.entry);
        ui::BinDocument* doc = window.binContainer().activeDocument();
        QVERIFY(doc);
        model::BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        model::Sequence seq = model::Sequence::withDefaultTracks();
        for (const QString& t : seq.trackNames()) {
            QList<model::Segment> parts;
            for (const char* id : {"a", "b", "c"}) {
                model::Segment g;
                g.clipId = QLatin1String(id);
                g.sourceTrack = t;
                g.length = 40;
                parts << g;
            }
            seq.splice(t, 0, parts);
        }
        seq.storeIn(item);
        doc->addItem(item);
        ui::Composer& composer = window.composer();
        composer.load(ui::MonitorId::Record, doc->nodeId(), item);
        const model::MonitorState& state = composer.state(ui::MonitorId::Record);
        // Nothing selected: the segment under the position.
        composer.perform(QStringLiteral("edit.mark_clip"));
        QCOMPARE(state.in(), std::optional<std::int64_t>(0));
        QCOMPARE(state.out(), std::optional<std::int64_t>(39));
        window.timeline().setSelection({{QStringLiteral("V1"), 40}, {QStringLiteral("A1"), 80}});
        composer.perform(QStringLiteral("edit.mark_clip"));
        QCOMPARE(state.in(), std::optional<std::int64_t>(40));
        QCOMPARE(state.out(), std::optional<std::int64_t>(119));
        window.clearProject();
    }

    // Deleting asks about the managed clips among the selection: the count
    // is theirs, not every selected item's (p.359).
    void deleteCountsTheManagedClips() {
        Project project;
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.setProject(project.entry);
        ui::BinContainer& bins = window.binContainer();
        ui::BinDocument* doc = bins.activeDocument();
        model::BinItem managed = clip(QStringLiteral("Managed"));
        media::Link link;
        link.path = project.dir.filePath(QStringLiteral("missing.mxf"));
        link.mediaId = QStringLiteral("m1");
        managed.link = link.toJson();
        const model::BinItem plain = clip(QStringLiteral("Plain"));
        const model::BinItem other = clip(QStringLiteral("Other"));
        doc->addItem(managed);
        doc->addItem(plain);
        doc->addItem(other);
        int clips = -1;
        int files = -1;
        window.setBinDeleteHook([&](int c, int f) -> std::optional<ui::MainWindow::BinDeleteChoice> {
            clips = c;
            files = f;
            return ui::MainWindow::BinDeleteChoice{true, false};
        });
        bins.activeView()->selectIds({managed.id, plain.id, other.id});
        QCOMPARE(bins.deleteSelection(), 3);
        QCOMPARE(clips, 1);
        QCOMPARE(files, 1);
        window.clearProject();
    }

    // A bin that cannot be saved as it closes stays open, and the user is
    // told (without a modal dialog, which auto-save could not wait for).
    void failedBinSaveIsShown() {
        Project project;
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.show();
        window.setProject(project.entry);
        ui::BinContainer& bins = window.binContainer();
        ui::BinDocument* doc = bins.activeDocument();
        doc->addItem(clip(QStringLiteral("Unsaved")));
        // A directory where the bin file should be: the write fails.
        QVERIFY(QFile::remove(doc->path()));
        QVERIFY(QDir().mkpath(doc->path() + QStringLiteral("/blocked")));
        commands.action(QStringLiteral("file.close_bin"))->trigger();
        settle();
        QCOMPARE(bins.activeDocument(), doc);
        auto* box = window.findChild<QMessageBox*>(QStringLiteral("binSaveFailed"));
        QVERIFY(box && box->isVisible());
        QVERIFY(box->text().contains(doc->name()));
        box->close();
        QVERIFY(QDir(doc->path()).removeRecursively());
        window.clearProject();
    }

    void closingTheWindowIsReported() {
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.show();
        QSignalSpy closed(&window, &ui::MainWindow::closed);
        window.close();
        QCOMPARE(closed.count(), 1);
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_main_window.moc"
