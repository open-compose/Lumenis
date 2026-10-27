// The Multi-Mix Tool (Editing Guide p.732-734; docs/spec/multi-mix.md): the setup's rules (the 24
// track limit, Dual Mono on odd tracks only, outputs without inputs), its JSON, and the dialog's
// matrix, output menu, Alt-click column, Shift-hover, Edit Mix and Save Setting. Mixing in the
// application is in tst_managed_media (multipleMixes).
#include "lumenis/media/mixdown.h"
#include "lumenis/ui/multi_mix_dialog.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QPushButton>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using media::MixdownFormat;
using media::MixOutput;
using media::MultiMix;

namespace {

MixOutput output(MixdownFormat format, QStringList inputs) {
    MixOutput o;
    o.format = format;
    o.inputs = std::move(inputs);
    return o;
}

MultiMixDialog::Setup setup() {
    MultiMixDialog::Setup s;
    s.tracks = {QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3")};
    s.bins = {{QStringLiteral("b1"), QStringLiteral("Clips")}};
    s.drives = {{QStringLiteral("Media"), QStringLiteral("/media")}};
    return s;
}

QAction* action(QMenu& menu, const QString& text) {
    for (QAction* a : menu.actions()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

} // namespace

class TestMultiMix : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void rules() {
        MultiMix m;
        QVERIFY(!media::multiMixProblem(m).isEmpty()); // no outputs
        m.outputs << output(MixdownFormat::DualMono, {QStringLiteral("A1")});
        QVERIFY(media::multiMixProblem(m).isEmpty());
        QCOMPARE(media::multiMixTracks(m), 2);
        m.outputs << output(MixdownFormat::Stereo, {});
        QVERIFY(media::multiMixProblem(m).contains(QLatin1String("no inputs")));
        m.outputs.last().inputs = {QStringLiteral("A2")};
        QCOMPARE(media::multiMixTrackNumber(m, 1), 3);
        // Dual Mono on track 4 is refused, on track 5 taken.
        m.outputs << output(MixdownFormat::DualMono, {QStringLiteral("A1")});
        QVERIFY(media::multiMixProblem(m).contains(QLatin1String("odd-numbered")));
        m.outputs.insert(2, output(MixdownFormat::Mono, {QStringLiteral("A3")}));
        QVERIFY(media::multiMixProblem(m).isEmpty());
        // At most 24 tracks.
        while (media::multiMixTracks(m) < 24) m.outputs << output(MixdownFormat::Mono, {QStringLiteral("A1")});
        QVERIFY(media::multiMixProblem(m).isEmpty());
        m.outputs << output(MixdownFormat::Mono, {QStringLiteral("A1")});
        QVERIFY(media::multiMixProblem(m).contains(QLatin1String("24")));
    }

    void json() {
        MultiMix m;
        MixOutput o = output(MixdownFormat::Surround51, {QStringLiteral("A1"), QStringLiteral("A3")});
        o.name = QStringLiteral("Music");
        o.masterFader = true;
        m.outputs << o << output(MixdownFormat::Mono, {QStringLiteral("A2")});
        m.useMarks = false;
        m.sequenceName = QStringLiteral("Stems");
        m.excludeVideoAndData = true;
        QCOMPARE(MultiMix::fromJson(m.toJson()), m);
        QCOMPARE(MultiMix::fromJson({}).useMarks, true);
        QCOMPARE(media::mixdownFormatFromName(QStringLiteral("Dual Mono")), std::optional(MixdownFormat::DualMono));
    }

    // The matrix and the output menu (p.733-734).
    void matrix() {
        MultiMixDialog d(setup());
        // A new mix: one stereo output of every track.
        QCOMPARE(d.mix().outputs.size(), 1);
        QCOMPARE(d.mix().outputs.first().inputs.size(), 3);
        QToolButton* cell = d.inputButton(0, QStringLiteral("A2"));
        QVERIFY(cell && cell->isChecked());
        cell->click();
        QCoreApplication::processEvents(); // a button's edit waits for its click to finish
        QCOMPARE(d.mix().outputs.first().inputs, (QStringList{QStringLiteral("A1"), QStringLiteral("A3")}));
        // The + button and the menu's items.
        QToolButton* plus = d.findChild<QToolButton*>(QStringLiteral("addOutput"));
        QVERIFY(plus);
        plus->click();
        QCoreApplication::processEvents();
        QCOMPARE(d.mix().outputs.size(), 2);
        QMenu menu;
        d.fillOutputMenu(&menu, 1);
        QAction* dual = action(menu, QStringLiteral("Set to Dual Mono"));
        QVERIFY(dual && !dual->isEnabled()); // output 2 is an even track
        QAction* mono = action(menu, QStringLiteral("Set to Mono"));
        QVERIFY(mono);
        mono->trigger();
        QCOMPARE(d.mix().outputs.at(1).format, MixdownFormat::Mono);
        // Output 2 has no inputs yet: Mix is dimmed.
        QVERIFY(!d.mixButton()->isEnabled());
        d.toggleColumn(1);
        QCOMPARE(d.mix().outputs.at(1).inputs.size(), 3);
        QVERIFY(d.mixButton()->isEnabled());
        d.toggleColumn(1); // all on: all off
        QVERIFY(d.mix().outputs.at(1).inputs.isEmpty());
        d.toggleInput(1, QStringLiteral("A3"));
        d.toggleInput(1, QStringLiteral("A1"));
        QCOMPARE(d.mix().outputs.at(1).inputs, (QStringList{QStringLiteral("A1"), QStringLiteral("A3")}));
        d.clearMix(1);
        QVERIFY(d.mix().outputs.at(1).inputs.isEmpty());
        // The Master Fader button above an output.
        QToolButton* master = d.masterButton(0);
        QVERIFY(master);
        master->click();
        QCoreApplication::processEvents();
        QVERIFY(d.mix().outputs.first().masterFader);
        d.removeOutput(1);
        QCOMPARE(d.mix().outputs.size(), 1);
    }

    // Edit Mix's name, mix and Master Fader; the inputs stay (p.734).
    void editMix() {
        MultiMixDialog d(setup());
        d.editMixAsk = [](const MixOutput& o) {
            MixOutput e = o;
            e.name = QStringLiteral("Dialogue");
            e.format = MixdownFormat::Surround51;
            e.inputs.clear(); // ignored
            return std::optional(e);
        };
        QVERIFY(d.editMix(0));
        QCOMPARE(d.mix().outputs.first().name, QStringLiteral("Dialogue"));
        QCOMPARE(d.mix().outputs.first().format, MixdownFormat::Surround51);
        QCOMPARE(d.mix().outputs.first().inputs.size(), 3);
    }

    // Save Setting As names a setting and chooses it; choosing a saved setting loads it.
    void settings() {
        MultiMixDialog::Setup s = setup();
        MultiMix saved;
        saved.outputs << output(MixdownFormat::Mono, {QStringLiteral("A2")});
        s.settings.insert(QStringLiteral("Dialogue only"), saved);
        MultiMixDialog d(s);
        QString savedName;
        MultiMix savedMix;
        d.save = [&](const QString& name, const MultiMix& m) {
            savedName = name;
            savedMix = m;
        };
        d.nameAsk = [] { return std::optional(QStringLiteral("Everything")); };
        QVERIFY(d.saveSetting()); // Untitled: asks for a name
        QCOMPARE(savedName, QStringLiteral("Everything"));
        QCOMPARE(d.settingName(), QStringLiteral("Everything"));
        QCOMPARE(savedMix.outputs.first().inputs.size(), 3);
        d.settingMenu()->setCurrentIndex(d.settingMenu()->findData(QStringLiteral("Dialogue only")));
        QCOMPARE(d.mix(), saved);
        // Opened from the Settings window: no Mix (p.734).
        s.canMix = false;
        MultiMixDialog edit(s);
        QVERIFY(!edit.mixButton()->isEnabled());
    }
};

QTEST_MAIN(TestMultiMix)
#include "tst_multi_mix.moc"
