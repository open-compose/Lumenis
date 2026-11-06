// OFX plug-in effects in the Effect Palette (FX Guide p.157; docs/spec/ofx-host.md): our test bundle
// (tests/media/ofx_test_plugin.cpp) registered before the catalog is first read, as app/main.cpp does. The usable
// plug-ins are listed in their vendor's category with the plug-in icon; the others are not.
#include "lumenis/media/ofx_host.h"
#include "lumenis/ui/effect_palette.h"

#include <QApplication>
#include <QListWidget>
#include <QtTest>

using namespace lumenis;
using namespace lumenis::ui;

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
