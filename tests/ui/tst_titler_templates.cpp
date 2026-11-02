// Titler+ templates in the application (ROADMAP 6.8c4a; FX Guide p.319-320; MainWindow::editTitleTemplate,
// saveTitleTemplate): a template opens in the Record monitor (Edit Template, or dragged there), the Dashboard's Save
// updates it and Save As makes a copy, and opening another sequence asks to save unsaved changes.
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/effect_template.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/titler.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/titler_tool.h"

#include <QDir>
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

// A template of a red "Lower Third", 50 frames at 25 fps.
model::BinItem titleTemplate() {
    model::Effect e = model::titlerEffect();
    model::Title t;
    t.objects << model::textObject(t, QStringLiteral("Lower Third"), QPointF(-400, 300));
    t.objects[0].fill.color.color = QStringLiteral("#ff0000");
    model::setTitle(e, t);
    model::EffectTemplate tpl;
    tpl.effect = e;
    tpl.length = 50;
    tpl.editRate = Rational(25);
    return model::effectTemplateItem(tpl, QStringLiteral("Lower Third"), QDateTime::currentDateTimeUtc());
}

QString fillOf(const model::BinItem& item) {
    const auto t = model::effectTemplateOf(item);
    return t ? model::titleOf(t->effect).objects.value(0).fill.color.color : QString();
}

} // namespace

class TestTitlerTemplates : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void editAndSave() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        BinContainer& bins = window.binContainer();
        const QString binId = bins.newBin();
        settle();
        BinDocument* doc = bins.document(binId);
        QVERIFY(doc);
        const model::BinItem tpl = titleTemplate();
        doc->addItem(tpl);
        QVERIFY(model::isTitleTemplate(tpl));

        // "Either method opens the template in the Record monitor" (p.320).
        QVERIFY(window.editTitleTemplate(binId, tpl.id));
        settle();
        Composer& composer = window.composer();
        const model::LoadedClip* rec = composer.state(MonitorId::Record).current();
        QVERIFY(rec);
        QCOMPARE(rec->item.name, QStringLiteral("Lower Third"));
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.size(), 1);
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.at(0).length, 50);
        EffectEditor& editor = window.effectEditor();
        QVERIFY(editor.shownTitlerEffect());

        // A change, then Save: the template takes it.
        TitlerTool* tool = editor.titlerTool();
        tool->select({QStringLiteral("Text 1")});
        QVERIFY(tool->modifySelected(QStringLiteral("Color"), [](model::TitleObject& o) { o.fill.color.color = QStringLiteral("#00ff00"); }));
        settle();
        QCOMPARE(fillOf(*doc->bin().item(tpl.id)), QStringLiteral("#ff0000"));
        QVERIFY(window.saveTitleTemplate(false));
        QCOMPARE(fillOf(*doc->bin().item(tpl.id)), QStringLiteral("#00ff00"));

        // Save As: a copy with the changes, edited from then on.
        tool->select({QStringLiteral("Text 1")});
        QVERIFY(tool->modifySelected(QStringLiteral("Color"), [](model::TitleObject& o) { o.fill.color.color = QStringLiteral("#0000ff"); }));
        settle();
        QVERIFY(window.saveTitleTemplate(true));
        QCOMPARE(doc->bin().items.size(), 2);
        const model::BinItem* copy = nullptr;
        for (const model::BinItem& i : doc->bin().items) {
            if (i.id != tpl.id) copy = &i;
        }
        QVERIFY(copy);
        QCOMPARE(copy->name, QStringLiteral("Lower Third Copy"));
        QCOMPARE(fillOf(*copy), QStringLiteral("#0000ff"));
        QCOMPARE(fillOf(*doc->bin().item(tpl.id)), QStringLiteral("#00ff00"));
        const QString copyId = copy->id;

        // "Open another sequence in the Record monitor, which prompts you to save".
        tool->select({QStringLiteral("Text 1")});
        QVERIFY(tool->modifySelected(QStringLiteral("Color"), [](model::TitleObject& o) { o.fill.color.color = QStringLiteral("#ffff00"); }));
        settle();
        QStringList asked;
        window.setTemplateSavePrompt([&](const QString& name) {
            asked << name;
            return true;
        });
        model::BinItem other;
        other.id = model::newItemId();
        other.name = QStringLiteral("Cut");
        other.kind = model::BinItemKind::Sequence;
        other.editRate = Rational(25);
        model::Sequence::withDefaultTracks().storeIn(other);
        doc->addItem(other);
        composer.load(MonitorId::Record, binId, other);
        settle();
        QCOMPARE(asked, QStringList{QStringLiteral("Lower Third Copy")});
        QCOMPARE(fillOf(*doc->bin().item(copyId)), QStringLiteral("#ffff00"));
        // Unchanged: no question.
        composer.loadFromBin(MonitorId::Record, binId, {tpl.id}); // "Drag the template to the Record monitor"
        settle();
        QCOMPARE(composer.state(MonitorId::Record).current()->item.name, QStringLiteral("Lower Third"));
        composer.load(MonitorId::Record, binId, other);
        settle();
        QCOMPARE(asked.size(), 1);
        // Not a template: nothing opens.
        QVERIFY(!window.editTitleTemplate(binId, other.id));
    }
};

QTEST_MAIN(TestTitlerTemplates)
#include "tst_titler_templates.moc"
