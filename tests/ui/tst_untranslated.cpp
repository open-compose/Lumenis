// Every text the user sees goes through translation (ROADMAP 10.4b; docs/spec/localisation.md). The test starts in the
// pseudo-locale, where every translated text is in brackets ("[Ŝáṽé ~~]"), opens the main window with a project and
// every window of the Tools menu, and reads what the widgets show: labels, buttons, tabs, menus, combo box items, table
// and tree headings, window titles and tooltips. A text that, its bracketed parts left out, still has a word of plain
// letters was never translated. User data (bin, clip and project names) and names that are the same in every language
// (formats, codecs, the product) are in kPassing.
#include "lumenis/core/project_format.h"
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/project_store.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/source_browser.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/translation.h"
#include "lumenis/ui/workspace.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSet>
#include <QTabBar>
#include <QTreeView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

// Words that are the same in every language: names, formats, units. Kept short; a new entry needs a reason.
const QSet<QString> kPassing = {
    // The product and the test's own data (the project, bin and user names below).
    QStringLiteral("Cutthulhu"), QStringLiteral("Words"), QStringLiteral("Untranslated"), QStringLiteral("editor"),
    // Units.
    QStringLiteral("kHz"),
};

// Widgets whose texts are names, the same in every language: the List Tool's output formats (EDL types and cut list
// templates, as files and other programs name them), the Source Browser's folder crumbs (the computer's folder
// names), the Audio Punch-In's input devices (named by the system), media drives (the computer's paths).
const QSet<QString> kNameWidgets = {
    QStringLiteral("listOutputFormat"),
    QStringLiteral("crumb"),
    QStringLiteral("inputSource"),
    QStringLiteral("targetDrive"),
};

// A text's plain words: what is left once the pseudo-locale's bracketed parts are taken out, as words of three
// letters or more (shorter ones are track names, units and the like: V1, A2, dB).
QStringList plainWords(const QString& text) {
    QString rest;
    int depth = 0;
    for (const QChar c : text) {
        if (c == QLatin1Char('[')) ++depth;
        else if (c == QLatin1Char(']') && depth > 0) --depth;
        else if (depth == 0) rest += c;
    }
    static const QRegularExpression word(QStringLiteral("[A-Za-z]{3,}"));
    QStringList out;
    for (QRegularExpressionMatchIterator it = word.globalMatch(rest); it.hasNext();) {
        const QString w = it.next().captured();
        if (!kPassing.contains(w)) out << w;
    }
    return out;
}

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

// "Class objectName: text" for each shown text with plain words, once each.
class Collector {
public:
    void add(const QObject* where, const QString& what, const QString& text) {
        if (text.isEmpty() || kNameWidgets.contains(where->objectName()) || plainWords(text).isEmpty()) return;
        const QString line = QStringLiteral("%1 %2 %3: %4").arg(QString::fromLatin1(where->metaObject()->className()),
                                                                 where->objectName(), what, text);
        if (!seen_.contains(line)) {
            seen_.insert(line);
            lines_ << line;
        }
    }
    QStringList lines() const { return lines_; }

private:
    QSet<QString> seen_;
    QStringList lines_;
};

void collectMenu(Collector& c, const QMenu* menu) {
    c.add(menu, QStringLiteral("menu"), menu->title());
    for (const QAction* a : menu->actions()) {
        if (a->isSeparator()) continue;
        c.add(menu, QStringLiteral("item"), a->text());
        if (a->menu()) collectMenu(c, a->menu());
    }
}

void collect(Collector& c, QWidget* top) {
    QList<QWidget*> widgets = top->findChildren<QWidget*>();
    widgets.prepend(top);
    for (QWidget* w : widgets) {
        if (!w->isVisible()) continue;
        if (w->isWindow()) c.add(w, QStringLiteral("title"), w->windowTitle());
        c.add(w, QStringLiteral("tooltip"), w->toolTip());
        if (auto* l = qobject_cast<QLabel*>(w)) c.add(w, QStringLiteral("label"), l->text());
        if (auto* b = qobject_cast<QAbstractButton*>(w)) c.add(w, QStringLiteral("button"), b->text());
        if (auto* g = qobject_cast<QGroupBox*>(w)) c.add(w, QStringLiteral("group"), g->title());
        if (auto* e = qobject_cast<QLineEdit*>(w)) c.add(w, QStringLiteral("placeholder"), e->placeholderText());
        if (auto* t = qobject_cast<QTabBar*>(w)) {
            for (int i = 0; i < t->count(); ++i) c.add(w, QStringLiteral("tab"), t->tabText(i));
        }
        if (auto* m = qobject_cast<QComboBox*>(w); m && !m->isEditable()) {
            for (int i = 0; i < m->count(); ++i) c.add(w, QStringLiteral("choice"), m->itemText(i));
        }
        if (auto* h = qobject_cast<QHeaderView*>(w); h && h->model()) {
            for (int i = 0; i < h->count(); ++i) {
                c.add(w, QStringLiteral("heading"), h->model()->headerData(i, h->orientation()).toString());
            }
        }
        if (auto* bar = qobject_cast<QMenuBar*>(w)) {
            for (const QAction* a : bar->actions()) {
                if (a->menu()) collectMenu(c, a->menu());
            }
        }
    }
}

} // namespace

class TestUntranslated : public QObject {
    Q_OBJECT

    QTemporaryDir dir_;
    ext::ProviderRegistry providers_;
    CommandRegistry commands_;
    std::unique_ptr<MainWindow> window_;

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        applyTheme(*qApp);
        // Before anything is made: the commands' texts and every window are made in the language installed.
        QVERIFY(installLanguage(*qApp, QStringLiteral("pseudo")));
        commands_.loadBuiltin();
        window_ = std::make_unique<MainWindow>(commands_, providers_);
        model::ProjectSettings s;
        s.name = QStringLiteral("Words");
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

    void plainWordsAreFound() {
        QVERIFY(plainWords(QStringLiteral("Save")).contains(QStringLiteral("Save")));
        QVERIFY(plainWords(pseudoText(QStringLiteral("Save"))).isEmpty());
        // A translated text with user data put in by arg() passes: the data is inside the brackets.
        QVERIFY(plainWords(pseudoText(QStringLiteral("Open %1")).arg(QStringLiteral("Harbour"))).isEmpty());
        QVERIFY(plainWords(QStringLiteral("V1 A2 dB")).isEmpty());
    }

    // names.json holds every built-in name shown through translatedResource: lupdate reads them from it, so a name
    // added in the code but not there would never reach the translators.
    void namesJsonHasEveryBuiltInName() {
        QFile file(QStringLiteral(":/lumenis/ui/names.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject names = QJsonDocument::fromJson(file.readAll()).object();
        const auto listed = [&](const char* context) {
            QSet<QString> out;
            for (const QJsonValue& v : names.value(QLatin1String(context)).toArray()) out.insert(v.toString());
            return out;
        };
        const QSet<QString> columns = listed("binColumns");
        for (const QString& h : model::binHeadings()) QVERIFY2(columns.contains(h), qPrintable(h));
        for (const QString& h : SourceMediaModel::columns()) QVERIFY2(columns.contains(h), qPrintable(h));
        const QSet<QString> views = listed("binViews");
        for (const model::BinViewPreset& v : model::builtInBinViews()) QVERIFY2(views.contains(v.name), qPrintable(v.name));
        const QSet<QString> workspaces = listed("workspaces");
        for (const QString& w : WorkspaceManager::builtinNames()) QVERIFY2(workspaces.contains(w), qPrintable(w));
    }

    // Longer texts must not squeeze user data out of sight: the Sidebar's bin names keep most of its width however
    // long the Transcription column's "Transcription On" is (it took the whole Sidebar in Irish).
    void longerTextsLeaveBinNamesRoom() {
        QCoreApplication::processEvents(); // the Sidebar sizes its columns when the event loop runs
        const auto trees = window_->findChildren<QTreeView*>(QStringLiteral("binTree"));
        QVERIFY(!trees.isEmpty());
        for (QTreeView* tree : trees) {
            if (!tree->isVisible()) continue;
            QVERIFY2(tree->header()->sectionSize(0) >= tree->viewport()->width() * 3 / 5,
                     qPrintable(QStringLiteral("%1 of %2").arg(tree->header()->sectionSize(0)).arg(tree->viewport()->width())));
        }
    }

    // Everything shown with the main window and the Tools menu's windows open.
    void everyShownTextIsTranslated() {
        QTimer closer;
        closer.setInterval(0);
        QStringList dialogs;
        Collector found;
        QObject::connect(&closer, &QTimer::timeout, [&] {
            // A tool that asks first: its question read, then closed.
            if (QWidget* modal = QApplication::activeModalWidget()) {
                collect(found, modal);
                modal->close();
            }
        });
        closer.start();
        for (const QString& id : toolsMenu()) {
            QAction* action = commands_.action(id);
            if (action && action->isEnabled()) action->trigger();
            QCoreApplication::processEvents();
        }
        closer.stop();
        for (QWidget* top : QApplication::topLevelWidgets()) {
            if (top->isVisible()) collect(found, top);
        }
        const QStringList lines = found.lines();
        for (const QString& l : lines) qWarning("untranslated: %s", qPrintable(l));
        QVERIFY2(lines.isEmpty(), qPrintable(QStringLiteral("%1 texts not translated").arg(lines.size())));
    }
};

QTEST_MAIN(TestUntranslated)
#include "tst_untranslated.moc"
