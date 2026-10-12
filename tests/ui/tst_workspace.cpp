#include "lumenis/ui/workspace.h"

#include <QDockWidget>
#include <QMainWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using lumenis::ui::WorkspaceStore;

namespace {

struct Window {
    QMainWindow main;
    QDockWidget* dock = new QDockWidget(QStringLiteral("Bin"), &main);

    Window() {
        main.setCentralWidget(new QWidget(&main));
        dock->setObjectName(QStringLiteral("BinPanel"));
        main.addDockWidget(Qt::LeftDockWidgetArea, dock);
        main.resize(800, 600);
        main.show();
    }
};

} // namespace

class TestWorkspace : public QObject {
    Q_OBJECT

private slots:
    void savesAndRestoresLayout() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
        WorkspaceStore store(settings);

        Window a;
        QVERIFY(QTest::qWaitForWindowExposed(&a.main));
        a.main.addDockWidget(Qt::RightDockWidgetArea, a.dock);
        a.dock->hide();
        store.save(QStringLiteral("Editing"), a.main);

        Window b;
        QVERIFY(QTest::qWaitForWindowExposed(&b.main));
        QVERIFY(b.dock->isVisible());
        QVERIFY(store.restore(QStringLiteral("Editing"), b.main));
        QCOMPARE(b.main.dockWidgetArea(b.dock), Qt::RightDockWidgetArea);
        QVERIFY(!b.dock->isVisible());
    }

    void listsAndRemovesNames() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
        WorkspaceStore store(settings);
        Window w;

        store.save(QStringLiteral("Color"), w.main);
        store.save(QStringLiteral("Audio"), w.main);
        store.save(WorkspaceStore::lastUsedName(), w.main);
        QStringList names = store.names();
        names.sort();
        QCOMPARE(names, (QStringList{"Audio", "Color"}));

        store.remove(QStringLiteral("Color"));
        QCOMPARE(store.names(), QStringList{"Audio"});
        QVERIFY(!store.restore(QStringLiteral("Color"), w.main));
    }
};

QTEST_MAIN(TestWorkspace)
#include "tst_workspace.moc"
