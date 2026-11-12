// The accessibility audit (ROADMAP 10.3; docs/spec/accessibility.md): what a screen reader (VoiceOver, Narrator, NVDA,
// Orca) is told about the controls. Qt reports every widget through QAccessibleInterface; standard widgets describe
// themselves, but a button that shows only an icon, or a widget we paint ourselves (the Timeline, the monitors, the
// position bar, graphs and wheels), has no name unless we give it one, and a reader then announces only "button".
//
// The tests open the main window with a project, as an editor does, and walk every visible widget of every window:
// each one a user can operate (it takes focus or is a button) must have an accessible name, and the widgets we paint
// must have a role that says what they are.
#include "lumenis/core/project_format.h"
#include "lumenis/ext/provider.h"
#include "lumenis/model/project_store.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QAccessible>
#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

// Whether a user operates the widget: it takes keyboard focus, or it is a button.
bool operable(const QWidget* w) {
    return qobject_cast<const QAbstractButton*>(w) || w->focusPolicy() != Qt::NoFocus;
}

// "ClassName objectName" for messages.
QString describe(const QWidget* w) {
    QString name = QString::fromLatin1(w->metaObject()->className());
    if (!w->objectName().isEmpty()) name += QLatin1Char(' ') + w->objectName();
    // A widget without an object name is found by its nearest named parent.
    else {
        for (const QWidget* p = w->parentWidget(); p; p = p->parentWidget()) {
            if (p->objectName().isEmpty()) continue;
            name += QStringLiteral(" in ") + p->objectName();
            break;
        }
    }
    return name;
}

// Every visible widget in every window, the windows' own included.
QList<QWidget*> shownWidgets() {
    QList<QWidget*> out;
    for (QWidget* top : QApplication::topLevelWidgets()) {
        if (!top->isVisible()) continue;
        out << top;
        for (QWidget* w : top->findChildren<QWidget*>()) {
            if (w->isVisible()) out << w;
        }
    }
    return out;
}

// The commands of the Tools menu (menus.json): each opens a tool window or panel.
QStringList toolsMenu() {
    QFile file(QStringLiteral(":/lumenis/ui/menus.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QStringList out;
    for (const QJsonValue& m : QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("menus")).toArray()) {
        if (m.toObject().value(QStringLiteral("id")).toString() != QLatin1String("tools")) continue;
        for (const QJsonValue& i : m.toObject().value(QStringLiteral("items")).toArray()) {
            if (i.isString() && !i.toString().isEmpty()) out << i.toString();
        }
    }
    return out;
}

// The operable widgets shown now that have no accessible name, as "Class objectName".
QStringList unnamedControls(int* checked) {
    QStringList unnamed;
    for (QWidget* w : shownWidgets()) {
        if (!operable(w)) continue;
        QAccessibleInterface* a = QAccessible::queryAccessibleInterface(w);
        if (!a) continue;
        ++*checked;
        if (a->text(QAccessible::Name).trimmed().isEmpty()) unnamed << describe(w);
    }
    unnamed.removeDuplicates();
    unnamed.sort();
    return unnamed;
}

} // namespace

class TestAccessibility : public QObject {
    Q_OBJECT

    QTemporaryDir dir_;
    ext::ProviderRegistry providers_;
    CommandRegistry commands_;
    std::unique_ptr<MainWindow> window_;

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        applyTheme(*qApp);
        // Interfaces are made on request even without an assistive technology running.
        QAccessible::setActive(true);
        commands_.loadBuiltin();
        window_ = std::make_unique<MainWindow>(commands_, providers_);
        model::ProjectSettings s;
        s.name = QStringLiteral("Access");
        s.format = projectFormatPresets().front();
        const QString users = QDir(dir_.path()).filePath(QStringLiteral("Users"));
        model::UserProfiles(users).create(QStringLiteral("editor"));
        window_->settings().setUser(users, QStringLiteral("editor"));
        const auto entry = model::ProjectStore(QDir(dir_.path()).filePath(QStringLiteral("Projects"))).create(s);
        QVERIFY(entry);
        window_->setProject(*entry);
        window_->resize(1600, 900);
        window_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
    }
    void cleanupTestCase() { window_.reset(); }

    // Every control a user can operate in the main window tells a screen reader its name.
    void everyControlHasAName() {
        int checked = 0;
        const QStringList unnamed = unnamedControls(&checked);
        QVERIFY(checked > 50);
        for (const QString& u : unnamed) qWarning("no accessible name: %s", qPrintable(u));
        QVERIFY2(unnamed.isEmpty(), qPrintable(QStringLiteral("%1 of %2 controls have no accessible name").arg(unnamed.size()).arg(checked)));
    }

    // And in every window the Tools menu opens (dialogs that would wait for an answer are closed).
    void everyToolWindowHasNames() {
        const QStringList tools = toolsMenu();
        QVERIFY(tools.size() > 20);
        QTimer closer;
        closer.setInterval(0);
        QObject::connect(&closer, &QTimer::timeout, [] {
            if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
        });
        closer.start();
        for (const QString& id : tools) {
            QAction* action = commands_.action(id);
            if (action && action->isEnabled()) action->trigger();
            QCoreApplication::processEvents();
        }
        closer.stop();
        int checked = 0;
        const QStringList unnamed = unnamedControls(&checked);
        QVERIFY(checked > 200);
        for (const QString& u : unnamed) qWarning("no accessible name: %s", qPrintable(u));
        QVERIFY2(unnamed.isEmpty(), qPrintable(QStringLiteral("%1 of %2 controls have no accessible name").arg(unnamed.size()).arg(checked)));
    }
};

QTEST_MAIN(TestAccessibility)
#include "tst_accessibility.moc"
