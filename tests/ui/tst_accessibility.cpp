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
#include "lumenis/model/bin.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/find_command.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QLabel>
#include <QAccessibleValueInterface>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
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
        bool named = !a->text(QAccessible::Name).trimmed().isEmpty();
        // A drop-down list: Qt 6.4 names it by its current item, Qt 6.8 (Windows, macOS) does not; ours must name it
        // whatever Qt does (its own name, a tooltip, or a form label that is its buddy).
        if (const auto* combo = qobject_cast<const QComboBox*>(w); combo && combo->accessibleName().isEmpty()) {
            bool labelled = false;
            for (const QLabel* l : w->window()->findChildren<QLabel*>()) labelled = labelled || l->buddy() == w;
            named = labelled;
        }
        if (!named) unnamed << describe(w);
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

    // The widgets we paint say what they are and show (10.3b): the Timeline as tracks of segments, the position bar as a
    // slider at the monitor's position, the monitor's picture with what is loaded.
    void paintedWidgetsDescribeThemselves() {
        const QString binId = window_->binContainer().newBin();
        BinDocument* doc = window_->binContainer().openBin(binId);
        QVERIFY(doc);
        auto seg = [](const QString& name, std::int64_t length) {
            model::Segment s;
            s.binId = QStringLiteral("bin");
            s.clipId = name;
            s.name = name;
            s.sourceTrack = QStringLiteral("V1");
            s.length = length;
            return s;
        };
        model::Sequence seq = model::Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("Interview"), 50), model::Segment::filler(25), seg(QStringLiteral("Harbour"), 75)});
        model::BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut 1");
        item.kind = model::BinItemKind::Sequence;
        item.editRate = projectFormatPresets().front().editRate;
        seq.storeIn(item);
        doc->addItem(item);
        window_->composer().load(MonitorId::Record, binId, item);
        window_->composer().moveTo(MonitorId::Record, 60);
        QCoreApplication::processEvents();

        // The Timeline: a pane of tracks, V1 a list of its three segments.
        auto* view = window_->findChild<TimelineView*>(QStringLiteral("timelineView"));
        QVERIFY(view);
        QAccessibleInterface* timeline = QAccessible::queryAccessibleInterface(view);
        QVERIFY(timeline);
        QCOMPARE(timeline->role(), QAccessible::Pane);
        QCOMPARE(timeline->text(QAccessible::Name), QStringLiteral("Timeline"));
        QVERIFY(timeline->childCount() >= 2);
        QAccessibleInterface* v1 = nullptr;
        for (int i = 0; i < timeline->childCount(); ++i) {
            QAccessibleInterface* c = timeline->child(i);
            QVERIFY(c && c->isValid());
            QCOMPARE(timeline->indexOfChild(c), i);
            if (c->text(QAccessible::Name).startsWith(QStringLiteral("Track V1"))) v1 = c;
        }
        QVERIFY(v1);
        QCOMPARE(v1->role(), QAccessible::List);
        QCOMPARE(v1->childCount(), 3);
        QCOMPARE(v1->parent(), timeline);
        QAccessibleInterface* first = v1->child(0);
        QVERIFY(first->text(QAccessible::Name).startsWith(QStringLiteral("Interview, starts 00:00:00:00")));
        QVERIFY(v1->child(1)->text(QAccessible::Name).startsWith(QStringLiteral("Filler")));
        QVERIFY(v1->child(2)->text(QAccessible::Name).contains(QStringLiteral("Harbour")));
        QCOMPARE(first->role(), QAccessible::ListItem);
        QCOMPARE(first->parent(), v1);
        QCOMPARE(v1->indexOfChild(v1->child(2)), 2);
        // The same interface each time it is asked for.
        QCOMPARE(v1->child(0), first);

        // The Record monitor's position bar: a slider at frame 60 of 0 to 149.
        PositionBar* bar = nullptr;
        for (PositionBar* b : window_->findChildren<PositionBar*>()) {
            if (b->monitor() == MonitorId::Record) bar = b;
        }
        QVERIFY(bar);
        QAccessibleInterface* slider = QAccessible::queryAccessibleInterface(bar);
        QCOMPARE(slider->role(), QAccessible::Slider);
        QCOMPARE(slider->text(QAccessible::Name), QStringLiteral("Record monitor position"));
        QAccessibleValueInterface* value = slider->valueInterface();
        QVERIFY(value);
        QCOMPARE(value->currentValue().toLongLong(), 60);
        QCOMPARE(value->minimumValue().toLongLong(), 0);
        QCOMPARE(value->maximumValue().toLongLong(), 149);
        // A reader can move it.
        value->setCurrentValue(10);
        QCOMPARE(window_->composer().state(MonitorId::Record).position(), 10);

        // The Record monitor's picture: what is loaded and where.
        MonitorPicture* picture = nullptr;
        for (MonitorPicture* m : window_->findChildren<MonitorPicture*>()) {
            if (m->monitor() == MonitorId::Record) picture = m;
        }
        QVERIFY(picture);
        QAccessibleInterface* shown = QAccessible::queryAccessibleInterface(picture);
        QCOMPARE(shown->role(), QAccessible::Graphic);
        QCOMPARE(shown->text(QAccessible::Name), QStringLiteral("Record monitor"));
        QCOMPARE(shown->text(QAccessible::Description), QStringLiteral("Cut 1, frame 11 of 150"));
    }

    // Every widget we paint that is shown says what it is (a role other than the generic Client) and has a name.
    void everyPaintedWidgetHasARole() {
        static const QStringList painted{
            QStringLiteral("lumenis::ui::TimelineView"),     QStringLiteral("lumenis::ui::PositionBar"),
            QStringLiteral("lumenis::ui::MonitorPicture"),   QStringLiteral("lumenis::ui::AngleDial"),
            QStringLiteral("lumenis::ui::AudioMeters"),      QStringLiteral("lumenis::ui::BrushPreview"),
            QStringLiteral("lumenis::ui::ChromaCurveGraph"), QStringLiteral("lumenis::ui::ChromaWheel"),
            QStringLiteral("lumenis::ui::EqGraph"),          QStringLiteral("lumenis::ui::FramingViewer"),
            QStringLiteral("lumenis::ui::Histogram"),        QStringLiteral("lumenis::ui::KeyframeTrack"),
            QStringLiteral("lumenis::ui::LumaRangesGraph"),  QStringLiteral("lumenis::ui::MotionGraph"),
            QStringLiteral("lumenis::ui::PanGrid"),          QStringLiteral("lumenis::ui::SafeColorGraph"),
            QStringLiteral("lumenis::ui::SequenceMap"),      QStringLiteral("lumenis::ui::TrackerTimeline"),
            QStringLiteral("lumenis::ui::VectorWheel")};
        QStringList wrong;
        int checked = 0;
        for (QWidget* w : shownWidgets()) {
            if (!painted.contains(QString::fromLatin1(w->metaObject()->className()))) continue;
            ++checked;
            QAccessibleInterface* a = QAccessible::queryAccessibleInterface(w);
            if (!a || a->role() == QAccessible::Client || a->text(QAccessible::Name).trimmed().isEmpty()) wrong << describe(w);
        }
        QVERIFY(checked >= 3); // the Timeline, the position bars and the monitors at least
        wrong.removeDuplicates();
        QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("painted widgets without a role or name: ") + wrong.join(QStringLiteral(", "))));
    }

    // The keyboard (10.3c): cycling the windows (Ctrl+Up/Down, p.78) and the tabs (Ctrl+Tab) puts the focus in the
    // panel that becomes active, so the next key acts there.
    void cyclingPanelsMovesTheFocus() {
        PanelManager& panels = window_->panels();
        QAction* next = commands_.action(QStringLiteral("windows.next_window"));
        QVERIFY(next);
        QSet<Panel*> visited;
        for (int i = 0; i < 8; ++i) {
            next->trigger();
            QCoreApplication::processEvents();
            Panel* active = panels.activePanel();
            QVERIFY(active);
            QWidget* focus = QApplication::focusWidget();
            QVERIFY2(focus && (focus == active || active->isAncestorOf(focus)),
                     qPrintable(QStringLiteral("focus outside the active panel %1").arg(active->id())));
            visited.insert(active);
        }
        QVERIFY(visited.size() >= 2);
    }

    // A widget we paint shows where the keyboard focus is when it came from the keyboard, not from a click.
    void focusRingFollowsKeyboardFocus() {
        auto* view = window_->findChild<TimelineView*>(QStringLiteral("timelineView"));
        QVERIFY(view);
        auto ring = [view] {
            QWidget* r = view->findChild<QWidget*>(QStringLiteral("focusRing"));
            return r && r->isVisible() ? r : nullptr;
        };
        window_->activateWindow();
        view->setFocus(Qt::ShortcutFocusReason);
        QCoreApplication::processEvents();
        QVERIFY(view->hasFocus());
        QWidget* shown = ring();
        QVERIFY(shown);
        QCOMPARE(shown->geometry(), view->rect());
        // Away: the ring goes.
        auto* other = window_->findChild<QLineEdit*>(QStringLiteral("binFilter"));
        QVERIFY(other);
        other->setFocus(Qt::TabFocusReason);
        QCoreApplication::processEvents();
        QVERIFY(!ring());
        // A click: no ring.
        view->setFocus(Qt::MouseFocusReason);
        QCoreApplication::processEvents();
        QVERIFY(!ring());
    }

    // Help > Find Command (ours): a command that is only on a button runs from the keyboard.
    void findCommandRunsAnyCommand() {
        const QString id = QStringLiteral("timeline.enlarge_waveform"); // on no menu and no default key
        QAction* action = commands_.action(id);
        QVERIFY(action && action->isEnabled() && commands_.hasHandler(id));
        QSignalSpy ran(action, &QAction::triggered);
        commands_.action(QStringLiteral("help.find_command"))->trigger();
        QCoreApplication::processEvents();
        FindCommandWindow* find = window_->findCommandWindow();
        QVERIFY(find && find->isVisible());
        // Every command that can run is listed, but Find Command itself.
        const QStringList all = find->shownIds();
        QVERIFY(all.contains(id));
        QVERIFY(!all.contains(QStringLiteral("help.find_command")));
        auto* search = find->findChild<QLineEdit*>(QStringLiteral("findCommandSearch"));
        QVERIFY(search && search->hasFocus());
        QTest::keyClicks(search, action->text().remove(QLatin1Char('&')));
        QVERIFY(find->shownIds().contains(id));
        QVERIFY(find->shownIds().size() < all.size());
        // Down moves to it when more than one matches.
        int guard = 0;
        auto* list = find->findChild<QListWidget*>(QStringLiteral("findCommandList"));
        QVERIFY(list);
        while (list->currentItem() && list->currentItem()->toolTip() != id && guard++ < 50) QTest::keyClick(search, Qt::Key_Down);
        QListWidgetItem* chosen = list->currentItem();
        QVERIFY(chosen);
        QCOMPARE(chosen->toolTip(), id);
        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(ran.count(), 1);
        QVERIFY(!find->isVisible());
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
        // The tool windows' graphs and wheels too.
        everyPaintedWidgetHasARole();
    }
};

QTEST_MAIN(TestAccessibility)
#include "tst_accessibility.moc"
