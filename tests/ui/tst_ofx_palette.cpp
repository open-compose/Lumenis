// OFX plug-in effects in the Effect Palette (FX Guide p.157; docs/spec/ofx-host.md): our test bundle
// (tests/media/ofx_test_plugin.cpp) registered before the catalog is first read, as app/main.cpp does. The usable
// plug-ins are listed in their vendor's category with the plug-in icon; the others are not. Their parameters are the
// Effect Editor's controls (6.11b1).
#include "lumenis/media/ofx_host.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/effect_template.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/effect_palette.h"

#include <QApplication>
#include <QDateTime>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonObject>
#include <QSlider>
#include <QListWidget>
#include <QtTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

// A sequence of one clip, 100 frames on V1, with `effect` on it if given.
model::BinItem sequence(std::optional<model::Effect> effect = std::nullopt) {
    model::BinItem s;
    s.id = model::newItemId();
    s.name = QStringLiteral("One");
    s.kind = model::BinItemKind::Sequence;
    s.editRate = Rational(25);
    model::Sequence seq = model::Sequence::withDefaultTracks();
    model::Segment a;
    a.clipId = QStringLiteral("a");
    a.name = QStringLiteral("A");
    a.sourceTrack = QStringLiteral("V1");
    a.length = 100;
    a.effect = effect;
    seq.splice(QStringLiteral("V1"), 0, {a});
    seq.storeIn(s);
    return s;
}

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

} // namespace

class TestOfxPalette : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Before EffectCatalog::builtin(): it reads the plug-ins once.
        QStringList errors;
        const auto plugins = media::scanOfxPlugins({QStringLiteral(LUMENIS_TEST_OFX)}, &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        QCOMPARE(media::registerOfxPlugins(plugins), 3);
    }

    void catalog() {
        const EffectCatalog& c = EffectCatalog::builtin();
        QVERIFY(c.categories().contains(QStringLiteral("Cutthulhu Tests")));
        QVERIFY(c.categories().contains(QStringLiteral("OFX Plug-Ins")));
        // After our own categories.
        QVERIFY(c.categories().indexOf(QStringLiteral("Cutthulhu Tests")) > c.categories().indexOf(QStringLiteral("Blend")));
        const EffectInfo* tint = c.find(QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint"));
        QVERIFY(tint);
        QCOMPARE(tint->ofxPlugin, QStringLiteral("org.cutthulhu.test.tint"));
        QVERIFY(tint->segment);
        QVERIFY(!tint->transition);
        QVERIFY(!tint->realTime);
        const EffectInfo* noise = c.find(QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Noise"));
        QVERIFY(noise && noise->segment);
        const EffectInfo* wipe = c.find(QStringLiteral("OFX Plug-Ins"), QStringLiteral("Test Wipe"));
        QVERIFY(wipe && wipe->transition && !wipe->segment);
        // Left out: the failing and the Paint-only plug-ins.
        QVERIFY(!c.find(QStringLiteral("OFX Plug-Ins"), QStringLiteral("Test Paint")));
        int fromPlugins = 0;
        for (const EffectInfo& e : c.all()) fromPlugins += e.ofxPlugin.isEmpty() ? 0 : 1;
        QCOMPARE(fromPlugins, 3);
        // Its parameters, a category of the Effect Editor's.
        QCOMPARE(tint->parameters, QStringList{QStringLiteral("Test Tint Plug-In Parameters")});
        const QList<EffectParameter>* params = effectParameters(QStringLiteral("Test Tint Plug-In Parameters"));
        QVERIFY(params);
        QCOMPARE(params->size(), 4);
        QCOMPARE(params->at(0).name, QStringLiteral("amount"));
        QCOMPARE(params->at(0).text, QStringLiteral("Amount"));
        QCOMPARE(params->at(0).kind, QStringLiteral("slider"));
        QCOMPARE(params->at(0).decimals, 3);
        QCOMPARE(params->at(1).options, (QStringList{QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")}));
        // The wipe's only parameter is the transition's progress: no category.
        QVERIFY(wipe->parameters.isEmpty());
        // A drag carries the plug-in effect like ours.
        std::unique_ptr<QMimeData> mime(effectMime(*tint));
        const auto back = effectFromMime(mime.get());
        QVERIFY(back);
        QCOMPARE(back->ofxPlugin, tint->ofxPlugin);
    }

    void icon() {
        // The plug-in icon differs from the effect icon (p.157).
        const QImage plain = effectIcon(false, false).pixmap(26, 18).toImage();
        const QImage plugin = effectIcon(false, false, true).pixmap(26, 18).toImage();
        QVERIFY(plain != plugin);
        QCOMPARE(effectIcon(false, false, false).pixmap(26, 18).toImage(), plain);
    }

    void editor() {
        // Applied from the palette, the plug-in's parameters are the Effect Editor's controls, and changing one is kept
        // in the effect under its category.
        Composer composer;
        Timeline timeline{composer};
        EffectEditor editor{timeline};
        timeline.setSequenceStore([](const QString&, const model::BinItem&, bool) { return true; });
        timeline.resize(900, 300);
        timeline.show();
        settle();
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        settle();
        const EffectInfo* tint = EffectCatalog::builtin().find(QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint"));
        QVERIFY(tint);
        QVERIFY(timeline.dropEffect(QStringLiteral("V1"), 10, *tint));
        composer.moveTo(MonitorId::Record, 10);
        settle();
        QCOMPARE(editor.effectName(), QStringLiteral("Test Tint"));
        const QString category = QStringLiteral("Test Tint Plug-In Parameters");
        auto* amount = editor.findChild<QSlider*>(QStringLiteral("param:") + category + QStringLiteral("/amount"));
        QVERIFY(amount);
        QCOMPARE(amount->value(), 500); // 0.5 with 3 decimals
        auto* channel = editor.findChild<QComboBox*>(QStringLiteral("param:") + category + QStringLiteral("/channel"));
        QVERIFY(channel);
        QVERIFY(editor.findChild<QCheckBox*>(QStringLiteral("param:") + category + QStringLiteral("/invert")));
        channel->setCurrentText(QStringLiteral("Green"));
        settle();
        const model::Sequence seq = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        const model::Segment& s = seq.track(QStringLiteral("V1"))->segments.at(0);
        QVERIFY(s.effect);
        QCOMPARE(s.effect->parameters.value(category).toObject().value(QStringLiteral("channel")).toString(), QStringLiteral("Green"));
        // Applied, it is tagged with its plug-in (6.11b2).
        QCOMPARE(media::ofxTaggedPlugin(*s.effect), QStringLiteral("org.cutthulhu.test.tint"));
        QCOMPARE(s.effect->parameters.value(media::kOfxPluginTag).toObject().value(QStringLiteral("version")).toString(), QStringLiteral("1.2"));

        // An effect template of it (p.157) keeps the plug-in and its values.
        const auto t = model::segmentTemplate(s, Rational(25), false);
        QVERIFY(t);
        const auto back = model::effectTemplateOf(model::effectTemplateItem(*t, QStringLiteral("Tint"), QDateTime::currentDateTime()));
        QVERIFY(back);
        const model::Effect applied = model::adaptedEffect(*back, Rational(25));
        QVERIFY(media::isOfxEffect(applied));
        QCOMPARE(media::ofxPluginOf(applied)->id, QStringLiteral("org.cutthulhu.test.tint"));
        QCOMPARE(applied.parameters.value(category).toObject().value(QStringLiteral("channel")).toString(), QStringLiteral("Green"));
    }

    void missingPluginIcon() {
        // The Timeline's effect icon: filled for a plug-in effect, blank (its outline only) when the plug-in is missing
        // (p.163). The tile's fill colour counts the filled pixels.
        auto filled = [](const model::Effect& e) {
            Composer composer;
            Timeline timeline{composer};
            timeline.setSequenceStore([](const QString&, const model::BinItem&, bool) { return true; });
            timeline.resize(900, 300);
            timeline.show();
            settle();
            composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(e));
            settle();
            const QImage image = timeline.view()->grab().toImage();
            int n = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) n += image.pixel(x, y) == 0xff8f5fc8 ? 1 : 0;
            }
            return n;
        };
        model::Effect present{QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint"), {}};
        media::markOfxEffect(present, *media::ofxPluginFor(QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint")));
        QVERIFY(!media::isMissingOfxEffect(present));
        QVERIFY(filled(present) > 10);
        model::Effect missing{QStringLiteral("Other Vendor"), QStringLiteral("Glow"), {}};
        missing.parameters.insert(media::kOfxPluginTag, QJsonObject{{QStringLiteral("id"), QStringLiteral("com.example.glow")},
                                                                    {QStringLiteral("label"), QStringLiteral("Glow")}});
        QVERIFY(media::isMissingOfxEffect(missing));
        QCOMPARE(filled(missing), 0);
    }

    void closeAllPlugins() {
        // The Console's closeallofxplugins (and Avid's closeallavxplugins, p.158) close the plug-in instances.
        model::Effect e{QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint"), {}};
        media::ofxSegmentPicture(e, media::Picture::solid(4, 4, Qt::white), 0.0, model::EffectSpan(10));
        ConsoleWindow console;
        console.run(QStringLiteral("closeallofxplugins"));
        QVERIFY2(ConsoleLog::instance().lines().constLast().startsWith(QStringLiteral("Closed 1 OFX")), qPrintable(ConsoleLog::instance().lines().constLast()));
        console.run(QStringLiteral("closeallavxplugins"));
        QVERIFY(ConsoleLog::instance().lines().constLast().startsWith(QStringLiteral("Closed 0 OFX")));
    }

    void palette() {
        // The Filters tab's category lists the plug-ins.
        EffectPalette palette;
        palette.setCategory(QStringLiteral("Cutthulhu Tests"));
        auto* list = palette.findChild<QListWidget*>(QStringLiteral("effectList"));
        QVERIFY(list);
        QStringList names;
        for (int i = 0; i < list->count(); ++i) names << list->item(i)->text();
        QCOMPARE(names, (QStringList{QStringLiteral("Test Tint"), QStringLiteral("Test Noise")}));
    }
};

QTEST_MAIN(TestOfxPalette)
#include "tst_ofx_palette.moc"
