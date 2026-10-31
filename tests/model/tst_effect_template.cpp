// Effect templates (FX Guide p.52-55; lumenis/model/effect_template.h): kept in bin items, applied to
// segments and transitions, with their source media, to one parameter group, across frame rates, and the
// Promote Effects copy's name.
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/effect_template.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/sequence.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

Segment clip(const QString& id, std::int64_t in, std::int64_t length) {
    Segment s;
    s.clipId = id;
    s.binId = QStringLiteral("bin");
    s.name = id;
    s.sourceTrack = QStringLiteral("V1");
    s.sourceIn = in;
    s.length = length;
    return s;
}

// A Picture-in-Picture with a position and a size, and Fixed keyframes on its Scale.
Effect pip() {
    Effect e{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
    e.parameters.insert(QStringLiteral("Position"), QJsonObject{{QStringLiteral("X"), 120}, {QStringLiteral("Y"), -40}});
    e.parameters.insert(QStringLiteral("Scaling"), QJsonObject{{QStringLiteral("X"), 50}});
    ParameterKeyframes keys;
    keys.fixed = true;
    keys.keys = {EffectKeyframe{0.0, 20}, EffectKeyframe{24.0, 80}};
    setKeyframes(e, QStringLiteral("Scaling"), QStringLiteral("Y"), keys);
    return e;
}

} // namespace

class TestEffectTemplate : public QObject {
    Q_OBJECT

private slots:
    void keptInABinItem() {
        Segment s = clip(QStringLiteral("a"), 5, 30);
        QVERIFY(!segmentTemplate(s, Rational(24), false)); // no effect
        s.effect = pip();
        const auto t = segmentTemplate(s, Rational(24), false);
        QVERIFY(t);
        QCOMPARE(t->effect, pip());
        QCOMPARE(t->length, 30);
        QVERIFY(!t->hasSource());
        const BinItem item = effectTemplateItem(*t, QString(), QDateTime::currentDateTimeUtc());
        // "identified in the bin by its effect icon" (p.53): an Effect item, named after the effect.
        QCOMPARE(item.kind, BinItemKind::Effect);
        QCOMPARE(item.name, QStringLiteral("Picture-in-Picture"));
        QCOMPARE(effectTemplateOf(item), t);
        // Through the bin's JSON.
        QCOMPARE(effectTemplateOf(binItemFromJson(binItemToJson(item)).value()), t);
        // Other Effect items (Audio EQ templates) are not video effect templates.
        BinItem other = item;
        other.composition = {};
        QVERIFY(!effectTemplateOf(other));
        // A transition's.
        const Transition dissolve{Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}}, 12,
                                  TransitionAlignment::StartingAtCut, 0};
        const EffectTemplate tt = transitionTemplate(dissolve, Rational(24));
        QCOMPARE(EffectTemplate::fromJson(tt.toJson()), tt);
        QVERIFY(tt.transition);
        QCOMPARE(tt.duration, 12);
    }

    void appliedToSegmentsAndTransitions() {
        Sequence seq = Sequence::withDefaultTracks();
        seq.overwrite(QStringLiteral("V1"), 0, {clip(QStringLiteral("a"), 50, 20), clip(QStringLiteral("b"), 50, 20)});
        Segment fx = clip(QStringLiteral("x"), 0, 10);
        fx.effect = pip();
        const EffectTemplate t = *segmentTemplate(fx, Rational(24), false);
        // "apply an effect template with all its parameters directly to segments" (p.53).
        QVERIFY(applyTemplate(seq, QStringLiteral("V1"), 25, t, Rational(24)));
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.at(1).effect, pip());
        QVERIFY(!seq.track(QStringLiteral("V1"))->segments.at(0).effect);
        // With nest, inside the effect already there.
        QVERIFY(applyTemplate(seq, QStringLiteral("V1"), 25, t, Rational(24), true));
        QVERIFY(seq.track(QStringLiteral("V1"))->segments.at(1).hasNest());
        // "or transitions": on the cut with its duration and alignment.
        const Transition dissolve{Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}}, 6,
                                  TransitionAlignment::StartingAtCut, 0};
        QVERIFY(applyTemplate(seq, QStringLiteral("V1"), 20, transitionTemplate(dissolve, Rational(24)), Rational(24)));
        const auto& placed = seq.track(QStringLiteral("V1"))->segments.at(1).transition;
        QVERIFY(placed);
        QCOMPARE(placed->duration, 6);
        QCOMPARE(placed->alignment, TransitionAlignment::StartingAtCut);
        // Too long for the material: refused, or sized to fit.
        const Transition longer{dissolve.effect, 200, TransitionAlignment::CenteredOnCut, 0};
        auto room = [](const Segment&) -> std::optional<std::int64_t> { return 80; };
        QVERIFY(!applyTemplate(seq, QStringLiteral("V1"), 20, transitionTemplate(longer, Rational(24)), Rational(24), false, false, room));
        QVERIFY(applyTemplate(seq, QStringLiteral("V1"), 20, transitionTemplate(longer, Rational(24)), Rational(24), false, true, room));
        QVERIFY(seq.track(QStringLiteral("V1"))->segments.at(1).transition->duration < 200);
        // Not on audio or locked tracks.
        QVERIFY(!applyTemplate(seq, QStringLiteral("A1"), 5, t, Rational(24)));
        seq.track(QStringLiteral("V1"))->locked = true;
        QVERIFY(!applyTemplate(seq, QStringLiteral("V1"), 5, t, Rational(24)));
    }

    void withSourceMedia() {
        // "save a segment effect with its source media" (p.53): editing the template in puts the clip there.
        Segment matte = clip(QStringLiteral("matte"), 100, 15);
        matte.effect = Effect{QStringLiteral("Key"), QStringLiteral("Matte Key"), {}};
        const EffectTemplate t = *segmentTemplate(matte, Rational(24), true);
        QVERIFY(t.hasSource());
        const BinItem item = effectTemplateItem(t, QStringLiteral("Logo key"), QDateTime::currentDateTimeUtc());
        QCOMPARE(item.durationFrames, 15);
        Sequence seq = Sequence::withDefaultTracks();
        seq.addTrack(TrackKind::Video);
        seq.overwrite(QStringLiteral("V1"), 0, {clip(QStringLiteral("bg"), 0, 60)});
        QVERIFY(applyTemplate(seq, QStringLiteral("V2"), 10, t, Rational(24)));
        const Track* v2 = seq.track(QStringLiteral("V2"));
        std::int64_t start = 0;
        const Segment& placed = v2->segments.at(v2->segmentAt(12, &start));
        QCOMPARE(start, 10);
        QCOMPARE(placed.clipId, QStringLiteral("matte"));
        QCOMPARE(placed.sourceIn, 100);
        QCOMPARE(placed.length, 15);
        QCOMPARE(placed.effect->name, QStringLiteral("Matte Key"));
    }

    void oneParameterGroup() {
        // "Media Composer applies the effect template only to the effect parameter group you selected" (p.54).
        Effect target = pip();
        target.parameters.insert(QStringLiteral("Position"), QJsonObject{{QStringLiteral("X"), 0}});
        target.parameters.insert(QStringLiteral("Scaling"), QJsonObject{{QStringLiteral("X"), 90}});
        Segment fx = clip(QStringLiteral("x"), 0, 10);
        fx.effect = pip();
        const EffectTemplate t = *segmentTemplate(fx, Rational(24), false);
        QVERIFY(applyTemplateGroup(target, t, QStringLiteral("Position"), Rational(24)));
        QCOMPARE(target.parameters.value(QStringLiteral("Position")).toObject().value(QStringLiteral("X")).toInt(), 120);
        QCOMPARE(target.parameters.value(QStringLiteral("Scaling")).toObject().value(QStringLiteral("X")).toInt(), 90);
        QVERIFY(!applyTemplateGroup(target, t, QStringLiteral("Position"), Rational(24))); // nothing changes now
        // The group's keyframes come with it.
        Effect bare{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
        QVERIFY(applyTemplateGroup(bare, t, QStringLiteral("Scaling"), Rational(24)));
        QCOMPARE(keyframesOf(bare, QStringLiteral("Scaling"), QStringLiteral("Y")).keys.size(), 2);
        QVERIFY(!bare.parameters.contains(QStringLiteral("Position")));
    }

    void acrossFrameRates() {
        // "Media Composer adjusts keyframes if necessary to account for differences in frame rate" (Editing
        // Guide p.474): Fixed keyframes a second in at 24 fps are a second in at 48.
        Segment fx = clip(QStringLiteral("x"), 0, 48);
        fx.effect = pip();
        const EffectTemplate t = *segmentTemplate(fx, Rational(24), false);
        const Effect at48 = adaptedEffect(t, Rational(48));
        QCOMPARE(keyframesOf(at48, QStringLiteral("Scaling"), QStringLiteral("Y")).keys.at(1).at, 48.0);
        QCOMPARE(adaptedEffect(t, Rational(24)), t.effect);
        QCOMPARE(adaptedEffect(t, Rational(0, 1)), t.effect);
    }

    void promotedName() {
        // "MyColorEffect.Promoted.01" (p.55).
        QCOMPARE(promotedTemplateName(QStringLiteral("MyColorEffect"), {}), QStringLiteral("MyColorEffect.Promoted.01"));
        QCOMPARE(promotedTemplateName(QStringLiteral("MyColorEffect"), {QStringLiteral("MyColorEffect.Promoted.01")}),
                 QStringLiteral("MyColorEffect.Promoted.02"));
    }
};

QTEST_GUILESS_MAIN(TestEffectTemplate)
#include "tst_effect_template.moc"
