// The check for unconnected items (9.6; docs/spec/ui-inventory.md "Unconnected items"): every command the user can
// reach (a menu item, a key, a Composer, Timeline, Tool palette or Command Palette button) must run something once a
// project is open. A command without a handler stays visible but dimmed (command_registry.h); that is allowed only for
// the commands listed in tests/data/dimmed_commands.json, each with the reason it is dimmed on purpose (hardware,
// Avid's shared-storage services, features not made yet). The list must stay current: a listed command that has a
// handler, or one that is not declared, fails too.

#include "lumenis/ext/provider.h"
#include "lumenis/core/project_format.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/bin.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/button_mapping.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/command_palette.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/keyboard_map.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

// The dimmed-on-purpose commands, by id, with their reasons.
QHash<QString, QString> dimmedCommands() {
    QFile file(QStringLiteral(DIMMED_COMMANDS));
    if (!file.open(QIODevice::ReadOnly)) qFatal("cannot read %s", DIMMED_COMMANDS);
    QHash<QString, QString> out;
    const QJsonArray list = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("commands")).toArray();
    for (const QJsonValue& v : list) {
        const QJsonObject o = v.toObject();
        out.insert(o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("reason")).toString());
    }
    return out;
}

} // namespace

class TestCommandWiring : public QObject {
    Q_OBJECT

    QTemporaryDir dir_;
    ext::ProviderRegistry providers_;
    CommandRegistry commands_;
    std::unique_ptr<MainWindow> window_;
    QHash<QString, QString> dimmed_;

    // A command reaches something when it has a handler, or is dimmed on purpose.
    QStringList unreached(const QStringList& ids) const {
        QStringList out;
        for (const QString& id : ids) {
            if (id.isEmpty() || commands_.hasHandler(id) || dimmed_.contains(id)) continue;
            if (!out.contains(id)) out << id;
        }
        // QVERIFY2 cuts long messages: each one on its own line too.
        for (const QString& id : out) qWarning("unreached: %s (%s)", qPrintable(id), qPrintable(commands_.text(id)));
        return out;
    }

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        dimmed_ = dimmedCommands();
        QVERIFY(!dimmed_.isEmpty());
        commands_.loadBuiltin();
        window_ = std::make_unique<MainWindow>(commands_, providers_);
        // A new project, as an editor opens one (app/main.cpp's --sample-project).
        model::ProjectSettings s;
        s.name = QStringLiteral("Wiring");
        s.format = projectFormatPresets().front();
        const QString users = QDir(dir_.path()).filePath(QStringLiteral("Users"));
        model::UserProfiles(users).create(QStringLiteral("editor"));
        window_->settings().setUser(users, QStringLiteral("editor"));
        const auto entry = model::ProjectStore(QDir(dir_.path()).filePath(QStringLiteral("Projects"))).create(s);
        QVERIFY(entry);
        window_->setProject(*entry);
    }
    void cleanupTestCase() { window_.reset(); }

    // Every declared command has a handler or is listed as dimmed on purpose.
    void everyCommandReachesSomething() {
        const QStringList missing = unreached(commands_.ids());
        QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("commands with no handler, not in dimmed_commands.json: ") +
                                                missing.join(QStringLiteral(", "))));
    }

    // The dimmed list names declared commands that really have no handler.
    void dimmedListIsCurrent() {
        QStringList stale;
        for (auto it = dimmed_.cbegin(); it != dimmed_.cend(); ++it) {
            QVERIFY2(!it.value().isEmpty(), qPrintable(it.key() + QStringLiteral(" has no reason")));
            if (!commands_.contains(it.key())) stale << it.key() + QStringLiteral(" (not declared)");
            else if (commands_.hasHandler(it.key())) stale << it.key() + QStringLiteral(" (has a handler)");
        }
        stale.sort();
        QVERIFY2(stale.isEmpty(), qPrintable(QStringLiteral("dimmed_commands.json is out of date: ") + stale.join(QStringLiteral(", "))));
        // A dimmed command's menu item is dimmed.
        for (auto it = dimmed_.cbegin(); it != dimmed_.cend(); ++it) {
            if (QAction* a = commands_.action(it.key())) QVERIFY2(!a->isEnabled(), qPrintable(it.key()));
        }
    }

    // Every default key reaches its command.
    void everyKeyReachesSomething() {
        QStringList ids;
        for (const QKeySequence& key : window_->keyboard().keys()) {
            const QString id = window_->keyboard().commandFor(key);
            QVERIFY2(commands_.contains(id), qPrintable(id));
            ids << id;
        }
        QVERIFY(!ids.isEmpty());
        const QStringList missing = unreached(ids);
        QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("keys that reach nothing: ") + missing.join(QStringLiteral(", "))));
    }

    // Every button mapped to a command (the Composer's, the Timeline's, the Tool palette's) and every
    // Command Palette button
    // reaches it.
    void everyButtonReachesSomething() {
        QStringList ids;
        QList<QWidget*> roots{window_.get()};
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (!roots.contains(w)) roots << w;
        }
        for (QWidget* root : roots) {
            for (const MappableButton* b : root->findChildren<MappableButton*>()) {
                if (b->isBlank()) continue;
                QVERIFY2(commands_.contains(b->command()), qPrintable(b->command()));
                ids << b->command();
            }
            for (const PaletteButton* b : root->findChildren<PaletteButton*>()) {
                if (b->command().isEmpty()) continue;
                QVERIFY2(commands_.contains(b->command()), qPrintable(b->command()));
                ids << b->command();
            }
        }
        QVERIFY(ids.size() > 20);
        // The Tool palette's default buttons, whatever the setting holds.
        ids << ToolPalette::defaultCommands();
        const QStringList missing = unreached(ids);
        QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("buttons that reach nothing: ") + missing.join(QStringLiteral(", "))));
    }

    // A key mapped to a menu command runs it in the Timeline as in the Composer (it did nothing there before 9.6).
    void menuKeysWorkInTheTimeline() {
        const QString id = QStringLiteral("tools.markers");
        QVERIFY(commands_.hasHandler(id));
        const QKeySequence key(Qt::Key_F7);
        window_->keyboard().assign(key, id);
        QSignalSpy ran(commands_.action(id), &QAction::triggered);
        QTest::keyClick(&window_->timeline(), Qt::Key_F7);
        QCOMPARE(ran.count(), 1);
        window_->keyboard().assign(key, QString());
    }

    // The Make Subclip button (p.408): a subclip of the Source monitor's marked frames in the active bin.
    void makeSubclip() {
        const QString binId = window_->binContainer().newBin();
        BinDocument* bin = window_->binContainer().openBin(binId);
        QVERIFY(bin);
        model::BinItem clip;
        clip.id = model::newItemId();
        clip.kind = model::BinItemKind::MasterClip;
        clip.name = QStringLiteral("Take 1");
        clip.editRate = projectFormatPresets().front().editRate; // the project's: no motion adapter
        clip.durationFrames = 100;
        clip.fields.insert(QStringLiteral("Start"), QStringLiteral("01:00:00:00"));
        clip.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1 A1"));
        bin->addItem(clip);
        Composer& composer = window_->composer();
        composer.loadDefault(binId, clip);
        model::MonitorState& s = composer.state(MonitorId::Source);
        s.setPosition(10);
        s.markIn();
        s.setPosition(29);
        s.markOut();
        commands_.action(QStringLiteral("edit.make_subclip"))->trigger();
        const model::BinItem* sub = nullptr;
        for (const model::BinItem& i : bin->bin().items) {
            if (i.kind == model::BinItemKind::Subclip) sub = &i;
        }
        QVERIFY(sub);
        QCOMPARE(sub->durationFrames, std::int64_t{20});
        QVERIFY(sub->name.startsWith(QStringLiteral("Take 1.Sub")));
        QCOMPARE(sub->fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:10"));
    }

    // Composer > MultiCamera Mode is a working menu item (it stayed dimmed before 9.6).
    void multiCameraModeIsEnabled() {
        QAction* mode = commands_.action(QStringLiteral("composer.multicamera_mode"));
        QVERIFY(mode && mode->isEnabled() && mode->isCheckable());
        mode->trigger();
        QVERIFY(window_->composer().multiCameraMode());
        mode->trigger();
        QVERIFY(!window_->composer().multiCameraMode());
    }
};

QTEST_MAIN(TestCommandWiring)
#include "tst_command_wiring.moc"
