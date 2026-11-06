// The OFX host's core (lumenis/media/ofx_host.h; docs/spec/ofx-host.md), with an OFX binary of our own
// (ofx_test_plugin.cpp, built as test-ofx/CutthulhuTest.ofx.bundle/Contents/<architecture>/CutthulhuTest.ofx): finding
// bundles, describing the plug-ins through our suites, leaving out what does not fit with its reason, the messages
// plug-ins send, and registering them.
#include "lumenis/media/ofx_host.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace lumenis::media;

namespace {

const QString kPlugin = QStringLiteral(LUMENIS_TEST_OFX);
const QString kFolder = QStringLiteral(LUMENIS_TEST_OFX_FOLDER);

const OfxPluginInfo* byId(const QList<OfxPluginInfo>& list, const char* id) {
    for (const OfxPluginInfo& p : list) {
        if (p.id == QLatin1String(id)) return &p;
    }
    return nullptr;
}

const OfxParamInfo* param(const OfxPluginInfo& p, const char* name) {
    for (const OfxParamInfo& i : p.params) {
        if (i.name == QLatin1String(name)) return &i;
    }
    return nullptr;
}

} // namespace

class TestOfxHost : public QObject {
    Q_OBJECT

private slots:
    void findBundles() {
        // Our own bundle, found to any depth; a bundle for another architecture and a folder without plug-ins are not.
        QCOMPARE(findOfxBundles({kFolder}), QStringList{QFileInfo(kPlugin).absoluteFilePath()});
        QTemporaryDir dir;
        const QString other = ofxArchitecture() == QLatin1String("Win64") ? QStringLiteral("MacOS") : QStringLiteral("Win64");
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("Vendor/Other.ofx.bundle/Contents/") + other)));
        QFile f(dir.filePath(QStringLiteral("Vendor/Other.ofx.bundle/Contents/") + other + QStringLiteral("/Other.ofx")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("Empty"))));
        QVERIFY(findOfxBundles({dir.path()}).isEmpty());
        // OFX_PLUGIN_PATH adds folders; only existing ones are searched.
        qputenv("OFX_PLUGIN_PATH", (dir.path() + QDir::listSeparator() + dir.filePath(QStringLiteral("missing"))).toLocal8Bit());
        const QStringList paths = ofxSearchPaths();
        QVERIFY(paths.contains(dir.path()));
        QVERIFY(!paths.contains(dir.filePath(QStringLiteral("missing"))));
        qunsetenv("OFX_PLUGIN_PATH");
        qputenv("LUMENIS_OFX_PATH", kFolder.toLocal8Bit());
        QVERIFY(ofxSearchPaths().contains(kFolder));
        QVERIFY(findOfxBundles(ofxSearchPaths()).contains(QFileInfo(kPlugin).absoluteFilePath()));
        qunsetenv("LUMENIS_OFX_PATH");
    }

    void describePlugins() {
        QStringList messages;
        setOfxMessageHandler([&](const QString& m) { messages << m; });
        QStringList errors;
        const QList<OfxPluginInfo> plugins = scanOfxPlugins({kPlugin}, &errors);
        setOfxMessageHandler({});
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        // The plug-in of another API is skipped.
        QCOMPARE(plugins.size(), 5);
        QVERIFY(!byId(plugins, "org.cutthulhu.test.other"));

        const OfxPluginInfo* tint = byId(plugins, "org.cutthulhu.test.tint");
        QVERIFY(tint);
        QVERIFY2(tint->usable(), qPrintable(tint->whyNot()));
        QCOMPARE(tint->label, QStringLiteral("Test Tint"));
        QCOMPARE(tint->versionMajor, 1);
        QCOMPARE(tint->versionMinor, 2);
        QCOMPARE(tint->grouping, QStringLiteral("Cutthulhu Tests/Colour"));
        // The vendor is the grouping's first part (p.157).
        QCOMPARE(tint->category(), QStringLiteral("Cutthulhu Tests"));
        QVERIFY(tint->segment());
        QVERIFY(!tint->transition());
        QCOMPARE(tint->describedContext, QStringLiteral("OfxImageEffectContextFilter"));
        QCOMPARE(tint->clips, (QStringList{QStringLiteral("Source"), QStringLiteral("Output")}));
        // The plug-in read the host's properties and ran four threads.
        QCOMPARE(tint->description, QStringLiteral("host org.open-compose.cutthulhu 1.5 threads 4"));
        QCOMPARE(tint->params.size(), 5);
        const OfxParamInfo* amount = param(*tint, "amount");
        QVERIFY(amount);
        QCOMPARE(amount->type, QStringLiteral("OfxParamTypeDouble"));
        QCOMPARE(amount->label, QStringLiteral("Amount"));
        QCOMPARE(amount->hint, QStringLiteral("How much tint"));
        QCOMPARE(amount->defaults, QVariantList{0.5});
        QVERIFY(amount->hasRange);
        QCOMPARE(amount->min, 0.0);
        QCOMPARE(amount->max, 1.0);
        QCOMPARE(amount->displayMax, 1.0);
        QVERIFY(amount->animates);
        const OfxParamInfo* channel = param(*tint, "channel");
        QVERIFY(channel);
        QCOMPARE(channel->options, (QStringList{QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")}));
        QCOMPARE(channel->defaults, QVariantList{2});
        const OfxParamInfo* invert = param(*tint, "invert");
        QVERIFY(invert);
        QCOMPARE(invert->defaults, QVariantList{1});
        QVERIFY(!invert->animates); // booleans do not animate unless the plug-in says so
        const OfxParamInfo* colour = param(*tint, "colour");
        QVERIFY(colour);
        QCOMPARE(colour->defaults, (QVariantList{1.0, 0.5, 0.25}));
        QVERIFY(param(*tint, "hidden")->secret);

        const OfxPluginInfo* wipe = byId(plugins, "org.cutthulhu.test.wipe");
        QVERIFY(wipe && wipe->usable());
        QVERIFY(wipe->transition());
        QVERIFY(!wipe->segment());
        QCOMPARE(wipe->category(), QStringLiteral("OFX Plug-Ins")); // no grouping: ours
        QCOMPARE(wipe->clips.size(), 3);

        // A Generator and General plug-in is described as a Generator, the first context we take.
        const OfxPluginInfo* noise = byId(plugins, "org.cutthulhu.test.noise");
        QVERIFY(noise && noise->usable());
        QCOMPARE(noise->describedContext, QStringLiteral("OfxImageEffectContextGenerator"));
        QCOMPARE(param(*noise, "context")->defaults, QVariantList{QStringLiteral("OfxImageEffectContextGenerator")});

        // Why the others were left out (p.163).
        const OfxPluginInfo* broken = byId(plugins, "org.cutthulhu.test.broken");
        QVERIFY(broken && !broken->usable());
        QCOMPARE(broken->whyNot(), QStringLiteral("its describe action failed (failed)"));
        const OfxPluginInfo* paint = byId(plugins, "org.cutthulhu.test.paint");
        QVERIFY(paint && !paint->usable());
        QCOMPARE(paint->whyNot(), QStringLiteral("it supports no Filter, Generator, Transition or General context"));

        // The messages, each with the plug-in it came from.
        QVERIFY2(messages.contains(QStringLiteral("org.cutthulhu.test.noise: noise has 3 octaves")), qPrintable(messages.join(QLatin1Char('\n'))));
        QVERIFY(messages.contains(QStringLiteral("org.cutthulhu.test.broken: error: no licence found")));

        // Registering keeps the usable ones findable by category and label.
        QCOMPARE(registerOfxPlugins(plugins), 3);
        QCOMPARE(ofxPlugins().size(), 5);
        const OfxPluginInfo* found = ofxPluginFor(QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint"));
        QVERIFY(found);
        QCOMPARE(found->id, QStringLiteral("org.cutthulhu.test.tint"));
        QVERIFY(!ofxPluginFor(QStringLiteral("OFX Plug-Ins"), QStringLiteral("Test Paint")));
    }

    void provider() {
        // The host as an extension provider: our bundle's folder scanned gives its five image effect plug-ins.
        OfxHostProvider p;
        QCOMPARE(p.info().id, std::string("org.openeffects.ofx-host"));
        QVERIFY(p.standard() == lumenis::ext::PluginStandard::OFX);
        const auto found = p.scan({std::filesystem::path(kFolder.toStdString())});
        QCOMPARE(found.size(), std::size_t(5));
        QCOMPARE(found.front().id, std::string("org.cutthulhu.test.tint"));
        QCOMPARE(found.front().vendor, std::string("Cutthulhu Tests"));
        QCOMPARE(found.front().version, std::string("1.2"));
    }

    void badBinaries() {
        // A file that is not a library, and a library without the OFX entry points, are named in the errors.
        QTemporaryDir dir;
        const QString junk = dir.filePath(QStringLiteral("Junk.ofx"));
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a library");
        f.close();
        QStringList errors;
        QVERIFY(scanOfxPlugins({junk}, &errors).isEmpty());
        QCOMPARE(errors.size(), 1);
        QVERIFY2(errors.first().startsWith(QStringLiteral("Junk.ofx could not be loaded")), qPrintable(errors.first()));
    }
};

QTEST_GUILESS_MAIN(TestOfxHost)
#include "tst_ofx_host.moc"
