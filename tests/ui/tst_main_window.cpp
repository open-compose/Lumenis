#include "lumenis/core/branding.h"
#include "lumenis/core/project_format.h"
#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/marker.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/titler_panel.h"
#include "lumenis/model/project_bins.h"
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

    // The owner's report (ROADMAP 6.10f): Tools > Titler+, then Source/Record Mode, gives the Record monitor back its own
    // position bar with the sequence's markers.
    void titlerAndBackToSourceRecord() {
        Project project;
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.setProject(project.entry);
        window.show();
        settle();
        ui::BinDocument* doc = window.binContainer().activeDocument();
        QVERIFY(doc);
        model::BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        model::Sequence seq = model::Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {model::Segment::filler(50)});
        seq.storeIn(item);
        model::Marker marker;
        marker.id = model::newItemId();
        marker.color = QStringLiteral("Red");
        marker.track = QStringLiteral("V1");
        marker.frame = 30;
        model::setMarkers(item, {marker});
        doc->addItem(item);
        window.composer().load(ui::MonitorId::Record, doc->nodeId(), item);
        window.composer().moveTo(ui::MonitorId::Record, 10);
        settle();
        QCOMPARE(window.composer().shownMarkers(ui::MonitorId::Record).size(), 1);
        commands.action(QStringLiteral("tools.titler"))->trigger();
        settle();
        settle();
        QVERIFY(window.timeline().isEffectMode() && window.composer().effectPreview().on);
        // "I open Titler+ and close it and the Effect Editor windows": the Dashboard and the Effect Editor panel close.
        if (QWidget* dashboard = window.effectEditor().titlerDashboard()) dashboard->close();
        // With the floating window's close button, as on Windows.
        QWidget* editorWindow = window.effectEditor().window();
        if (editorWindow != &window) editorWindow->close();
        else window.panels().closePanel(window.panels().panel(QStringLiteral("EffectEditor")));
        settle();
        // The Timeline's own Source/Record Mode button, clicked.
        auto* button = window.timeline().findChild<QToolButton*>(QStringLiteral("edit.source_record_mode"));
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton);
        settle();
        QVERIFY(!window.composer().effectPreview().on);
        QCOMPARE(window.composer().shownMarkers(ui::MonitorId::Record).size(), 1);
    }

    // The Toggle Source/Record command (Esc on the default keyboard, the Command Palette's Edit tab)
    // shows the source clip in the Timeline as the Timeline's button does (p.480, p.603). It did
    // nothing before (owner's report, 2026-11-05).
    void toggleSourceRecordCommandShowsTheSource() {
        Project project;
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.setProject(project.entry);
        QVERIFY(!window.timeline().showsSource());
        auto* button = window.timeline().findChild<QToolButton*>(QStringLiteral("toggleSourceRecord"));
        QVERIFY(button);
        commands.action(QStringLiteral("edit.toggle_source_record"))->trigger();
        QVERIFY(window.timeline().showsSource());
        QVERIFY(button->isChecked());
        commands.action(QStringLiteral("edit.toggle_source_record"))->trigger();
        QVERIFY(!window.timeline().showsSource());
        QVERIFY(!button->isChecked());
        // Esc pressed in the Timeline goes through the editing keyboard to the same command.
        QTest::keyClick(&window.timeline(), Qt::Key_Escape);
        QVERIFY(window.timeline().showsSource());
        window.clearProject();
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

    // File > Open Bin with a Media Composer bin from another project
    // (p.296): it is read into a bin of ours, listed under Other Bins and
    // opened; its sequence's segments name its clips in that bin. Opening it
    // again opens the same bin; a file that is not a bin is an error.
    void openBinReadsAMediaComposerBin() {
        const QString corpus = qEnvironmentVariable("CUTTHULHU_INTEROP_CORPUS");
        const QString avb = QDir(corpus).filePath(QStringLiteral("pyavb/retimes/linear01.avb"));
        if (corpus.isEmpty() || !QFile::exists(avb)) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
        Project project;
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.setProject(project.entry);
        QVERIFY(commands.action(QStringLiteral("file.open_bin"))->isEnabled());
        ui::BinContainer& bins = window.binContainer();
        QString error;
        const QString id = window.openBinFile(avb, &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        const model::BinNode* node = bins.bins()->node(id);
        QVERIFY(node);
        QVERIFY(node->external);
        QCOMPARE(node->name, QStringLiteral("linear01"));
        const model::BinNode* folder = bins.bins()->parentOf(id);
        QVERIFY(folder && folder->kind == model::BinNode::Kind::OtherBins);
        ui::BinDocument* doc = bins.document(id);
        QVERIFY(doc);
        QVERIFY(!doc->isDirty()); // saved as it was read
        QVERIFY(doc->path().startsWith(QDir(project.entry.folder).absolutePath()));
        QCOMPARE(doc->bin().display.customColumns, QStringList{QStringLiteral("UNC Path")});
        QCOMPARE(doc->bin().items.size(), 2);
        const model::BinItem& clipItem = doc->bin().items.at(0);
        const model::BinItem& sequenceItem = doc->bin().items.at(1);
        QCOMPARE(clipItem.kind, model::BinItemKind::MasterClip);
        QCOMPARE(sequenceItem.kind, model::BinItemKind::Sequence);
        const model::Sequence sequence = model::Sequence::fromItem(sequenceItem);
        const model::Segment& s = sequence.track(QStringLiteral("V1"))->segments.first();
        QCOMPARE(s.binId, id);
        QCOMPARE(s.clipId, clipItem.id);
        const auto resolved = window.composer().resolve(s.binId, s.clipId);
        QVERIFY(resolved);
        QCOMPARE(resolved->mobId, clipItem.mobId);
        // The bin file holds the same.
        const auto file = model::BinFile::read(doc->path(), &error);
        QVERIFY2(file, qPrintable(error));
        QCOMPARE(file->items.size(), 2);

        QCOMPARE(window.openBinFile(avb), id);
        QCOMPARE(doc->bin().items.size(), 2);

        const QString junk = project.dir.filePath(QStringLiteral("Junk.avb"));
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a bin");
        f.close();
        error.clear();
        QVERIFY(window.openBinFile(junk, &error).isEmpty());
        QVERIFY(!error.isEmpty());

        // Taking it off the list removes our copy too: nothing else lists it. Another project's
        // own bin would stay where it is (p.296).
        const QString copy = doc->path();
        QVERIFY(QFile::exists(copy));
        QVERIFY(window.binContainer().deleteNode(id, &error));
        QVERIFY(!QFile::exists(copy));
        QVERIFY(QFile::exists(avb));
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
