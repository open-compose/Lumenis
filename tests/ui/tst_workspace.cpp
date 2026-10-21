#include "lumenis/ui/panels.h"
#include "lumenis/ui/workspace.h"
#include "lumenis/ui/workspace_bar.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis::ui;

namespace {

QJsonObject tabs(const QString& id) {
    return {{QStringLiteral("tabs"), QJsonArray{id}}, {QStringLiteral("current"), 0}};
}

QJsonObject standardLayout() {
    return {{QStringLiteral("version"), 1},
            {QStringLiteral("main"),
             QJsonObject{{QStringLiteral("split"), QStringLiteral("v")},
                         {QStringLiteral("sizes"), QJsonArray{1, 1}},
                         {QStringLiteral("children"), QJsonArray{tabs(QStringLiteral("Composer")),
                                                                 tabs(QStringLiteral("Timeline"))}}}}};
}

struct Fixture {
    QWidget owner;
    PanelManager panels{&owner};
    WorkspaceManager workspaces{panels};
    Panel* composer = new Panel(QStringLiteral("Composer"), QStringLiteral("Composer"));
    Panel* timeline = new Panel(QStringLiteral("Timeline"), QStringLiteral("Timeline"));

    Fixture() {
        auto* layout = new QHBoxLayout(&owner);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(panels.mainHost());
        panels.addPanel(composer);
        panels.addPanel(timeline);
        for (const QString& name : workspaces.names()) workspaces.setDefaultLayout(name, standardLayout());
        panels.restoreLayout(standardLayout());
        owner.resize(800, 600);
        owner.show();
    }

    // Tabs the Timeline into the Composer's window: a recognisable change.
    void rearrange() {
        panels.tab(timeline, composer->group());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    bool rearranged() const { return composer->group() == timeline->group(); }
};

} // namespace

class TestWorkspace : public QObject {
    Q_OBJECT

private slots:
    void startsWithTheDefaultWorkspaces() {
        Fixture f;
        QCOMPARE(f.workspaces.names(), (QStringList{"Edit", "Color", "Effects", "Audio"}));
        QCOMPARE(f.workspaces.active(), QStringLiteral("Edit"));
        for (const QString& n : f.workspaces.names()) QVERIFY(f.workspaces.isBuiltin(n));
        QVERIFY(!f.workspaces.canRemoveActive());
        QCOMPARE(f.workspaces.barMode(), WorkspaceManager::BarMode::IconAndText);
    }

    void returnsToTheLastKnownState() {
        Fixture f;
        f.rearrange();
        QVERIFY(f.workspaces.activate(QStringLiteral("Color")));
        QVERIFY(!f.rearranged()); // Color has its default layout
        QVERIFY(f.workspaces.activate(QStringLiteral("Edit")));
        QVERIFY(f.rearranged()); // where Edit was left
        QVERIFY(!f.workspaces.activate(QStringLiteral("No Such Workspace")));
    }

    void orToTheLastSavedState() {
        Fixture f;
        f.workspaces.setMoveBehavior(WorkspaceManager::MoveBehavior::LastSavedState);
        f.rearrange();
        f.workspaces.activate(QStringLiteral("Audio"));
        f.workspaces.activate(QStringLiteral("Edit"));
        QVERIFY(!f.rearranged());

        f.rearrange();
        f.workspaces.saveCurrent();
        f.workspaces.activate(QStringLiteral("Audio"));
        f.workspaces.activate(QStringLiteral("Edit"));
        QVERIFY(f.rearranged());
    }

    void restoresToSavedAndToDefault() {
        Fixture f;
        f.rearrange();
        f.workspaces.saveCurrent();
        f.panels.dock(f.timeline, f.composer->group(), DockSide::Right);
        QVERIFY(!f.rearranged());
        f.workspaces.restoreToSaved();
        QVERIFY(f.rearranged());
        f.workspaces.restoreCurrentToDefault();
        QVERIFY(!f.rearranged());
        // The saved layout went too.
        f.workspaces.restoreToSaved();
        QVERIFY(!f.rearranged());
    }

    void createsAndDeletesWorkspaces() {
        Fixture f;
        QSignalSpy changed(&f.workspaces, &WorkspaceManager::changed);
        f.rearrange();
        QVERIFY(f.workspaces.create(QStringLiteral("Logging")));
        QCOMPARE(f.workspaces.active(), QStringLiteral("Logging"));
        QVERIFY(f.workspaces.canRemoveActive());
        QVERIFY(changed.count() > 0);

        QString error;
        QVERIFY(!f.workspaces.create(QStringLiteral("logging"), {}, &error)); // names ignore case
        QVERIFY(!error.isEmpty());
        QVERIFY(!f.workspaces.create(QStringLiteral("  "), {}, &error));
        QVERIFY(!f.workspaces.create(QStringLiteral("X"), QStringLiteral("Nowhere"), &error));

        // Based on another workspace: starts from that one's layout.
        QVERIFY(f.workspaces.create(QStringLiteral("Grading"), QStringLiteral("Color")));
        QVERIFY(!f.rearranged());
        f.workspaces.activate(QStringLiteral("Logging"));
        QVERIFY(f.rearranged());
        // Restore Current to Default goes back to the default it came from.
        f.workspaces.restoreCurrentToDefault();
        QVERIFY(!f.rearranged());

        QVERIFY(f.workspaces.removeActive());
        QVERIFY(!f.workspaces.contains(QStringLiteral("Logging")));
        QCOMPARE(f.workspaces.active(), QStringLiteral("Edit"));
        QVERIFY(!f.workspaces.removeActive()); // built-in
    }

    void allowsTwelveWorkspaces() {
        Fixture f;
        for (int i = static_cast<int>(f.workspaces.names().size()); i < WorkspaceManager::kMaxWorkspaces; ++i) {
            QVERIFY(f.workspaces.create(QStringLiteral("W%1").arg(i)));
        }
        QString error;
        QVERIFY(!f.workspaces.create(QStringLiteral("One Too Many"), {}, &error));
        QVERIFY(error.contains(QStringLiteral("12")));
    }

    void savesAndLoadsUserSettings() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        {
            Fixture f;
            f.workspaces.create(QStringLiteral("Logging"));
            f.rearrange();
            f.workspaces.setBarMode(WorkspaceManager::BarMode::IconOnly);
            f.workspaces.setMoveBehavior(WorkspaceManager::MoveBehavior::LastSavedState);
            f.panels.setBlankPanelOnClose(true);
            f.workspaces.save(settings);
        }
        Fixture g;
        QVERIFY(!g.rearranged());
        g.workspaces.load(settings);
        QCOMPARE(g.workspaces.names(), (QStringList{"Edit", "Color", "Effects", "Audio", "Logging"}));
        QCOMPARE(g.workspaces.active(), QStringLiteral("Logging"));
        QVERIFY(g.rearranged()); // the last session's arrangement
        QCOMPARE(g.workspaces.barMode(), WorkspaceManager::BarMode::IconOnly);
        QCOMPARE(g.workspaces.moveBehavior(), WorkspaceManager::MoveBehavior::LastSavedState);
        QVERIFY(g.panels.blankPanelOnClose());
    }

    void loadTidiesNamesAsSetEntriesDoes() {
        // Names that differ only in case are one workspace (find() is
        // case-insensitive); a built-in in another case is that built-in.
        const QStringList stored{"edit", "Logging", "logging", " ", "Audio"};
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("workspaces"));
        settings.setValue(QStringLiteral("version"), 1);
        settings.beginWriteArray(QStringLiteral("list"), static_cast<int>(stored.size()));
        for (int i = 0; i < stored.size(); ++i) {
            settings.setArrayIndex(i);
            settings.setValue(QStringLiteral("name"), stored.at(i));
        }
        settings.endArray();
        settings.setValue(QStringLiteral("active"), QStringLiteral("LOGGING"));
        settings.endGroup();
        Fixture f;
        f.workspaces.load(settings);
        const QStringList want{"Edit", "Color", "Effects", "Logging", "Audio"};
        QCOMPARE(f.workspaces.names(), want);
        QVERIFY(f.workspaces.isBuiltin(QStringLiteral("edit")));
        QVERIFY(!f.workspaces.isBuiltin(QStringLiteral("Logging")));
        QCOMPARE(f.workspaces.active(), QStringLiteral("Logging"));

        QList<WorkspaceManager::Entry> entries;
        for (const QString& n : stored) entries << WorkspaceManager::Entry{n, {}, {}, {}};
        Fixture g;
        g.workspaces.setEntries(entries, QStringLiteral("LOGGING"));
        QCOMPARE(g.workspaces.names(), want);
        QVERIFY(g.workspaces.isBuiltin(QStringLiteral("Edit")));

        // Built-ins put back count towards the 12.
        QList<WorkspaceManager::Entry> many;
        for (int i = 0; i < WorkspaceManager::kMaxWorkspaces; ++i) many << WorkspaceManager::Entry{QStringLiteral("W%1").arg(i), {}, {}, {}};
        g.workspaces.setEntries(many, {});
        QCOMPARE(static_cast<int>(g.workspaces.names().size()), WorkspaceManager::kMaxWorkspaces);
        for (const char* b : {"Edit", "Color", "Effects", "Audio"}) QVERIFY(g.workspaces.isBuiltin(QString::fromLatin1(b)));
    }

    void ignoresSettingsFromAnOlderVersion() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("workspaces/Editing/state"), QByteArray("old"));
        Fixture f;
        f.workspaces.load(settings);
        QCOMPARE(f.workspaces.names(), (QStringList{"Edit", "Color", "Effects", "Audio"}));
        QVERIFY(f.panels.isOpen(f.composer));
    }

    void toolbarSwitchesWorkspaces() {
        Fixture f;
        WorkspaceBar bar(f.workspaces);
        bar.resize(bar.sizeHint());
        bar.show();
        QCOMPARE(bar.width(), 60);
        QTest::mouseClick(&bar, Qt::LeftButton, {}, bar.itemRect(2).center());
        QCOMPARE(f.workspaces.active(), QStringLiteral("Effects"));

        // The height hint ends below the last item, with or without labels.
        QCOMPARE(bar.sizeHint().height(), bar.itemRect(3).bottom() + 1);
        f.workspaces.setBarMode(WorkspaceManager::BarMode::IconOnly);
        QCOMPARE(bar.width(), 36);
        QCOMPARE(bar.sizeHint().height(), bar.itemRect(3).bottom() + 1);
        QVERIFY(bar.itemRect(3).height() < 58);
        f.workspaces.setBarMode(WorkspaceManager::BarMode::Hidden);
        QVERIFY(bar.isHidden());
        f.workspaces.setBarMode(WorkspaceManager::BarMode::IconAndText);
        QVERIFY(bar.isVisible());
    }

    void newWorkspaceDialog() {
        Fixture f;
        f.workspaces.create(QStringLiteral("Logging"));
        NewWorkspaceDialog dialog(f.workspaces);
        auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("workspaceName"));
        auto* basedOnBox = dialog.findChild<QCheckBox*>(QStringLiteral("basedOnBox"));
        auto* basedOn = dialog.findChild<QComboBox*>(QStringLiteral("basedOn"));
        QPushButton* ok = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        QVERIFY(name && basedOnBox && basedOn && ok);
        QVERIFY(!ok->isEnabled());
        name->setText(QStringLiteral("Edit"));
        QVERIFY(!ok->isEnabled()); // exists
        name->setText(QStringLiteral("Review"));
        QVERIFY(ok->isEnabled());
        // The active workspace is not offered as a base.
        QCOMPARE(basedOn->findText(QStringLiteral("Logging")), -1);
        QVERIFY(!basedOn->isEnabled());
        QVERIFY(dialog.basedOn().isEmpty());
        basedOnBox->setChecked(true);
        basedOn->setCurrentText(QStringLiteral("Audio"));
        QCOMPARE(dialog.name(), QStringLiteral("Review"));
        QCOMPARE(dialog.basedOn(), QStringLiteral("Audio"));
    }
};

QTEST_MAIN(TestWorkspace)
#include "tst_workspace.moc"
