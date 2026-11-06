// The OFX host's core (lumenis/media/ofx_host.h; docs/spec/ofx-host.md), with an OFX binary of our own
// (ofx_test_plugin.cpp, built as test-ofx/CutthulhuTest.ofx.bundle/Contents/<architecture>/CutthulhuTest.ofx): finding
// bundles, describing the plug-ins through our suites, leaving out what does not fit with its reason, the messages
// plug-ins send, and registering them; rendering (6.11b1) Filter, Generator and Transition plug-ins with the effect's
// values and keyframes, alone and through the compositor; their Effect Editor controls; instances made and destroyed.
#include "lumenis/media/compositor.h"
#include "lumenis/media/ofx_host.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QLibrary>
#include <QTemporaryDir>
#include <QtTest>

#include <array>
#include <cmath>

using namespace lumenis;
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

// Scans and registers our bundle (each test may run alone).
void registerTestPlugins() {
    QStringList errors;
    registerOfxPlugins(scanOfxPlugins({kPlugin}, &errors));
}

// A pixel's channels, 0 to 1.
std::array<float, 4> at(const Picture& p, int x, int y) {
    const float* v = p.pixel(x, y);
    return {v[0], v[1], v[2], v[3]};
}

bool approx(float a, double b) {
    return std::abs(static_cast<double>(a) - b) < 1e-3;
}

model::Segment clip(const QString& id, std::int64_t length) {
    model::Segment s;
    s.binId = QStringLiteral("bin");
    s.clipId = id;
    s.name = id;
    s.sourceTrack = QStringLiteral("V1");
    s.length = length;
    return s;
}

// The compositor over clips that are solid red, blue or grey.
Composition composition(const model::Sequence& seq) {
    Composition c;
    c.sequence = &seq;
    c.tracks = {QStringLiteral("V1")};
    c.size = QSize(16, 8);
    c.precision = EffectPrecision::Bits32;
    c.source = [](const model::Segment& s, std::int64_t, QSize size) -> std::optional<QImage> {
        QImage image(size, QImage::Format_RGB32);
        image.fill(s.clipId == QLatin1String("red") ? QColor(255, 0, 0) : s.clipId == QLatin1String("blue") ? QColor(0, 0, 255) : QColor(128, 128, 128));
        return image;
    };
    return c;
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
        QCOMPARE(plugins.size(), 6);
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
        QCOMPARE(registerOfxPlugins(plugins), 4);
        QCOMPARE(ofxPlugins().size(), 6);
        const OfxPluginInfo* found = ofxPluginFor(QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint"));
        QVERIFY(found);
        QCOMPARE(found->id, QStringLiteral("org.cutthulhu.test.tint"));
        QVERIFY(!ofxPluginFor(QStringLiteral("OFX Plug-Ins"), QStringLiteral("Test Paint")));
    }

    void provider() {
        // The host as an extension provider: our bundle's folder scanned gives its six image effect plug-ins.
        OfxHostProvider p;
        QCOMPARE(p.info().id, std::string("org.openeffects.ofx-host"));
        QVERIFY(p.standard() == lumenis::ext::PluginStandard::OFX);
        const auto found = p.scan({std::filesystem::path(kFolder.toStdString())});
        QCOMPARE(found.size(), std::size_t(6));
        QCOMPARE(found.front().id, std::string("org.cutthulhu.test.tint"));
        QCOMPARE(found.front().vendor, std::string("Cutthulhu Tests"));
        QCOMPARE(found.front().version, std::string("1.2"));
    }

    void editorParameters() {
        registerTestPlugins();
        const OfxPluginInfo* tint = ofxPluginFor(QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint"));
        QVERIFY(tint);
        QCOMPARE(ofxParameterCategory(*tint), QStringLiteral("Test Tint Plug-In Parameters"));
        const QList<OfxEditorParameter> params = ofxEditorParameters(*tint);
        QStringList keys;
        for (const OfxEditorParameter& p : params) keys << p.key;
        // The secret one has no control.
        QCOMPARE(keys, (QStringList{QStringLiteral("amount"), QStringLiteral("channel"), QStringLiteral("invert"), QStringLiteral("colour")}));
        const OfxEditorParameter& amount = params.at(0);
        QCOMPARE(amount.kind, QStringLiteral("slider"));
        QCOMPARE(amount.text, QStringLiteral("Amount"));
        QCOMPARE(amount.min, 0.0);
        QCOMPARE(amount.max, 1.0);
        QCOMPARE(amount.decimals, 3);
        QCOMPARE(amount.defaultValue.toDouble(), 0.5);
        QVERIFY(amount.keyframeable);
        QCOMPARE(params.at(1).kind, QStringLiteral("menu"));
        QCOMPARE(params.at(1).defaultValue.toString(), QStringLiteral("Blue"));
        QVERIFY(!params.at(1).keyframeable);
        QCOMPARE(params.at(2).kind, QStringLiteral("checkbox"));
        QCOMPARE(params.at(2).defaultValue.toBool(), true);
        QCOMPARE(params.at(3).kind, QStringLiteral("color"));
        QCOMPARE(params.at(3).defaultValue.toString(), QStringLiteral("#ff8040"));
        // The Transition parameter is the transition's progress, not a control; a Generator's String has none either.
        QVERIFY(ofxEditorParameters(*ofxPluginFor(QStringLiteral("OFX Plug-Ins"), QStringLiteral("Test Wipe"))).isEmpty());
        const QList<OfxEditorParameter> noise = ofxEditorParameters(*ofxPluginFor(QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Noise")));
        QCOMPARE(noise.size(), 1);
        QCOMPARE(noise.first().key, QStringLiteral("fill"));
        // The defaults through parameterDefault, as the Effect Editor shows them.
        const model::Effect e{QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint"), {}};
        QVERIFY(isOfxEffect(e));
        QCOMPARE(parameterDefault(e, ofxParameterCategory(*tint), QStringLiteral("amount")).toDouble(), 0.5);
        QVERIFY(!isOfxEffect(model::Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}}));
    }

    void renderFilter() {
        registerTestPlugins();
        model::Effect e{QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint"), {}};
        const QString category = ofxParameterCategory(*ofxPluginOf(e));
        const Picture source = Picture::solid(8, 4, QColor(64, 128, 192));
        const model::EffectSpan span(10);
        // The defaults: Blue set to 0.5, then inverted.
        auto out = at(ofxSegmentPicture(e, source, span.timeOf(0), span), 3, 2);
        QVERIFY2(approx(out[0], 1.0 - 64 / 255.0) && approx(out[1], 1.0 - 128 / 255.0) && approx(out[2], 0.5) && approx(out[3], 1.0),
                 qPrintable(QStringLiteral("%1 %2 %3 %4").arg(static_cast<double>(out[0])).arg(static_cast<double>(out[1])).arg(static_cast<double>(out[2])).arg(static_cast<double>(out[3]))));
        // The effect's own values: Red at 0.25, not inverted.
        e.parameters.insert(category, QJsonObject{{QStringLiteral("amount"), 0.25}, {QStringLiteral("channel"), QStringLiteral("Red")},
                                                  {QStringLiteral("invert"), false}});
        out = at(ofxSegmentPicture(e, source, span.timeOf(0), span), 0, 0);
        QVERIFY(approx(out[0], 0.25) && approx(out[1], 128 / 255.0) && approx(out[2], 192 / 255.0));
        // Keyframes: Amount from 0 at the first frame (an identity: the source as it is) to 1 at the last.
        model::setKeyframe(e, category, QStringLiteral("amount"), span.timeOf(0), span, 0.0);
        model::setKeyframe(e, category, QStringLiteral("amount"), span.timeOf(9), span, 1.0);
        QCOMPARE(ofxSegmentPicture(e, source, span.timeOf(0), span).data(), source.data());
        QVERIFY(approx(at(ofxSegmentPicture(e, source, span.timeOf(9), span), 0, 0)[0], 1.0));
        QVERIFY(approx(at(ofxSegmentPicture(e, source, span.timeOf(4.5), span), 0, 0)[0], 0.5));
    }

    void renderGeneratorAndTransition() {
        registerTestPlugins();
        // A Generator: its Fill colour, and its bottom row white (the standard's row 0 is the bottom).
        model::Effect noise{QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Noise"), {}};
        const model::EffectSpan span(10);
        Picture out = ofxSegmentPicture(noise, Picture::solid(6, 4, Qt::black), span.timeOf(0), span);
        QVERIFY(approx(at(out, 2, 0)[2], 0.75) && approx(at(out, 2, 0)[0], 0.25));
        QVERIFY(approx(at(out, 2, 3)[0], 1.0) && approx(at(out, 2, 3)[2], 1.0));
        noise.parameters.insert(QStringLiteral("Test Noise Plug-In Parameters"), QJsonObject{{QStringLiteral("fill"), QStringLiteral("#ff0000")}});
        out = ofxSegmentPicture(noise, Picture::solid(6, 4, Qt::black), span.timeOf(0), span);
        QVERIFY(approx(at(out, 2, 0)[0], 1.0) && approx(at(out, 2, 0)[2], 0.0));

        // A Transition: the incoming picture over the left part, as far as the progress.
        const model::Effect wipe{QStringLiteral("OFX Plug-Ins"), QStringLiteral("Test Wipe"), {}};
        const Picture red = Picture::solid(10, 2, QColor(255, 0, 0)), blue = Picture::solid(10, 2, QColor(0, 0, 255));
        out = ofxTransitionPicture(wipe, red, blue, 0.3, 0.3, model::EffectSpan(4, true));
        QVERIFY(approx(at(out, 2, 1)[2], 1.0));
        QVERIFY(approx(at(out, 3, 1)[0], 1.0));

        // Through the compositor: the Generator on a segment, the wipe on a transition.
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment grey = clip(QStringLiteral("grey"), 10);
        grey.effect = noise;
        seq.splice(QStringLiteral("V1"), 0, {grey});
        QImage image = composeImage(composition(seq), 0);
        QCOMPARE(image.pixelColor(8, 2), QColor(255, 0, 0));
        QCOMPARE(image.pixelColor(8, 7), QColor(255, 255, 255));

        model::Sequence cut = model::Sequence::withDefaultTracks();
        cut.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 10), clip(QStringLiteral("blue"), 10)});
        model::Transition t;
        t.effect = wipe;
        t.duration = 4;
        t.alignment = model::TransitionAlignment::CenteredOnCut;
        cut.track(QStringLiteral("V1"))->segments[1].transition = t;
        // Frames 8 to 11; at frame 9 the progress is 0.4: the left 6 of 16 columns blue.
        image = composeImage(composition(cut), 9);
        QCOMPARE(image.pixelColor(1, 4), QColor(0, 0, 255));
        QCOMPARE(image.pixelColor(14, 4), QColor(255, 0, 0));
    }

    void severalInputs() {
        // A General plug-in with optional inputs (p.161): described in General, its inputs and how many it can take.
        registerTestPlugins();
        const OfxPluginInfo* c = ofxPluginFor(QStringLiteral("Cutthulhu Mixers"), QStringLiteral("Test Composite"));
        QVERIFY(c);
        QCOMPARE(c->describedContext, QStringLiteral("OfxImageEffectContextGeneral"));
        QCOMPARE(c->inputs.size(), 3);
        QCOMPARE(c->inputs.at(1).label, QStringLiteral("Over Image"));
        QVERIFY(c->inputs.at(1).optional && !c->inputs.at(0).optional);
        QVERIFY(c->inputs.at(2).mask);
        QCOMPARE(c->requiredInputs(), 1);
        QCOMPARE(c->maxInputs(), 3);
        QVERIFY(c->variableInputs());
        QVERIFY(!ofxPluginFor(QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint"))->variableInputs());
        QCOMPARE(c->connectedInputs(2).size(), 2);
        QCOMPARE(c->connectedInputs(2).at(1).name, QStringLiteral("Over"));
        QCOMPARE(c->connectedInputs(9).size(), 3);

        // The Angle control and the push button among its controls.
        const QList<OfxEditorParameter> params = ofxEditorParameters(*c);
        QCOMPARE(params.size(), 2);
        QCOMPARE(params.at(0).kind, QStringLiteral("angle"));
        QCOMPARE(params.at(1).kind, QStringLiteral("button"));

        // The inputs chosen, kept in its tag.
        model::Effect e{QStringLiteral("Cutthulhu Mixers"), QStringLiteral("Test Composite"), {}};
        markOfxEffect(e, *c);
        QCOMPARE(ofxInputCount(e), 1);
        setOfxInputCount(e, 3);
        QCOMPARE(ofxInputCount(e), 3);

        // Rendering with Over shown through the Matte's red.
        const Picture red = Picture::solid(4, 2, QColor(255, 0, 0)), blue = Picture::solid(4, 2, QColor(0, 0, 255));
        Picture matte(4, 2);
        for (int x = 0; x < 2; ++x) {
            for (int y = 0; y < 2; ++y) {
                float* m = matte.pixel(x, y);
                m[0] = m[3] = 1.0f;
            }
        }
        const model::EffectSpan span(10);
        Picture out = ofxSegmentPicture(e, red, 0.0, span, {blue, matte});
        QVERIFY(approx(at(out, 0, 0)[2], 1.0) && approx(at(out, 0, 0)[0], 0.0)); // Over where the matte is
        QVERIFY(approx(at(out, 3, 0)[0], 1.0));                                   // Source elsewhere
        // Two inputs: Over over Source; an input without a picture is connected and empty.
        setOfxInputCount(e, 2);
        QVERIFY(approx(at(ofxSegmentPicture(e, red, 0.0, span, {blue}), 3, 1)[2], 1.0));
        QVERIFY(approx(at(ofxSegmentPicture(e, red, 0.0, span, {}), 3, 1)[0], 1.0));

        // The angle's value: inverted from 180 degrees; its keyframes through the parameter suite.
        const QString category = ofxParameterCategory(*c);
        e.parameters.insert(category, QJsonObject{{QStringLiteral("spin"), 200.0}});
        QVERIFY(approx(at(ofxSegmentPicture(e, red, 0.0, span, {}), 0, 0)[0], 0.0));
        // Angles go beyond a turn: -540 is less than 180.
        e.parameters.insert(category, QJsonObject{{QStringLiteral("spin"), -540.0}});
        QVERIFY(approx(at(ofxSegmentPicture(e, red, 0.0, span, {}), 0, 0)[0], 1.0));

        // The push button: the plug-in sets Spin to 90, which comes back for the Effect Editor to keep.
        const QJsonObject set = ofxPushButton(e, QStringLiteral("square"), 0.0, span);
        QCOMPARE(set, (QJsonObject{{QStringLiteral("spin"), 90.0}}));
        // Values set outside a button's action are not kept.
        QVERIFY(ofxPushButton(model::Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}}, QStringLiteral("x"), 0.0, span).isEmpty());
    }

    void severalInputsInTheCompositor() {
        // The inputs are the nest's tracks (p.161: "see Nesting Effects"): V1 the segment's own, V2 the Over image.
        registerTestPlugins();
        const OfxPluginInfo* c = ofxPluginFor(QStringLiteral("Cutthulhu Mixers"), QStringLiteral("Test Composite"));
        model::Effect e{QStringLiteral("Cutthulhu Mixers"), QStringLiteral("Test Composite"), {}};
        markOfxEffect(e, *c);
        setOfxInputCount(e, 2);
        model::Segment grey = clip(QStringLiteral("grey"), 10);
        grey.effect = e;
        QVERIFY(model::ensureNest(grey));
        model::Sequence nest = model::nestOf(grey);
        static_cast<void>(nest.addTrack(model::TrackKind::Video));
        nest.splice(QStringLiteral("V2"), 0, {clip(QStringLiteral("blue"), 10)});
        model::setNest(grey, nest);
        model::Sequence seq = model::Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {grey});
        QCOMPARE(composeImage(composition(seq), 3).pixelColor(8, 4), QColor(0, 0, 255));
        // One input: the nest composited as usual (V2 over V1), through the plug-in as its Source.
        setOfxInputCount(grey.effect.value(), 1);
        seq = model::Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {grey});
        QCOMPARE(composeImage(composition(seq), 3).pixelColor(8, 4), QColor(0, 0, 255));
    }

    void missingPlugin() {
        // A tagged effect whose plug-in is not installed passes its picture through, and says "Can't find effect" once
        // (p.163).
        registerTestPlugins();
        model::Effect e{QStringLiteral("Other Vendor"), QStringLiteral("Glow"), {}};
        e.parameters.insert(kOfxPluginTag, QJsonObject{{QStringLiteral("id"), QStringLiteral("com.example.glow")}, {QStringLiteral("label"), QStringLiteral("Glow")}});
        QCOMPARE(ofxTaggedPlugin(e), QStringLiteral("com.example.glow"));
        QVERIFY(isMissingOfxEffect(e));
        QVERIFY(!isOfxEffect(e));
        QStringList messages;
        setOfxMessageHandler([&](const QString& m) { messages << m; });
        const Picture source = Picture::solid(4, 4, QColor(10, 20, 30));
        QCOMPARE(ofxSegmentPicture(e, source, 0.0, model::EffectSpan(5)).data(), source.data());
        ofxSegmentPicture(e, source, 0.5, model::EffectSpan(5));
        setOfxMessageHandler({});
        QCOMPARE(messages, QStringList{QStringLiteral("Can't find effect: Glow (com.example.glow)")});
        // Tagged with an installed plug-in, it is that plug-in's even under another name.
        model::Effect renamed{QStringLiteral("Somewhere"), QStringLiteral("Else"), {}};
        markOfxEffect(renamed, *ofxPluginFor(QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint")));
        QVERIFY(isOfxEffect(renamed));
        QVERIFY(!isMissingOfxEffect(renamed));
        QVERIFY(!isMissingOfxEffect(model::Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}}));
    }

    void instances() {
        registerTestPlugins();
        QLibrary lib(kPlugin);
        QVERIFY(lib.load());
        using Live = int (*)();
        const auto live = reinterpret_cast<Live>(lib.resolve("CutthulhuTestLiveInstances"));
        QVERIFY(live);
        const model::Effect e{QStringLiteral("Cutthulhu Tests"), QStringLiteral("Test Tint"), {}};
        ofxSegmentPicture(e, Picture::solid(4, 4, Qt::white), 0.0, model::EffectSpan(10));
        QVERIFY(live() >= 1);
        // One instance for each plug-in rendered, all destroyed by closeOfxInstances; the next render makes one again.
        const int open = live();
        QCOMPARE(closeOfxInstances(), open);
        QCOMPARE(live(), 0);
        ofxSegmentPicture(e, Picture::solid(4, 4, Qt::white), 0.0, model::EffectSpan(10));
        QCOMPARE(live(), 1);
        closeOfxInstances();
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
