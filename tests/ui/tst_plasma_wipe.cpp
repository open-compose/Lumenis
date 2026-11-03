// PlasmaWipe effects (ROADMAP 6.9e; FX Guide ch.16, p.442-455, p.478; docs/spec/plasma-wipe.md): our gradient images,
// the threshold with softness and a border band, frame effects, the user's PlasmaWipes folders, and the effects in the
// Effect Palette's catalog.
#include "lumenis/media/plasma_wipe.h"
#include "lumenis/ui/effect_palette.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace lumenis;
using namespace lumenis::media;

namespace {

QColor at(const Picture& p, int x, int y) {
    return p.toImage().pixelColor(x, y);
}

bool near(const QColor& a, const QColor& b, int tolerance = 3) {
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance && std::abs(a.blue() - b.blue()) <= tolerance;
}

model::Effect basic(const QString& folder, const QString& name) {
    return model::Effect{QStringLiteral("PlasmaWipe Basic ") + folder, name, {}};
}

} // namespace

class TestPlasmaWipe : public QObject {
    Q_OBJECT

private slots:
    void ourGradients() {
        // Our own effects, the frame among them with the frame defaults ("border" and "frame" in its path, p.453).
        const PlasmaWipe* ramp = plasmaWipeOf(basic(QStringLiteral("Horiz"), QStringLiteral("Linear Ramp")));
        QVERIFY(ramp);
        QVERIFY(!ramp->frame);
        const PlasmaWipe* frame = plasmaWipeOf(basic(QStringLiteral("Borders"), QStringLiteral("Square Frame")));
        QVERIFY(frame && frame->frame);
        QVERIFY(!plasmaWipeOf(basic(QStringLiteral("Horiz"), QStringLiteral("No Such"))));
        QVERIFY(!isPlasmaWipe(model::Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}}));
        // Every gradient uses the whole range ("ensure that the image uses all the available gradient values", p.445).
        for (const PlasmaWipe& w : plasmaWipes()) {
            if (!w.path.isEmpty() || w.frame) continue;
            const QImage g = plasmaWipeGradient(w, QSize(72, 58));
            QVERIFY2(!g.isNull(), qPrintable(w.name));
            int lo = 255, hi = 0;
            for (int y = 0; y < g.height(); ++y) {
                for (int x = 0; x < g.width(); ++x) {
                    lo = std::min<int>(lo, g.constScanLine(y)[x]);
                    hi = std::max<int>(hi, g.constScanLine(y)[x]);
                }
            }
            QVERIFY2(lo <= 5 && hi >= 250, qPrintable(QStringLiteral("%1 %2-%3").arg(w.name).arg(lo).arg(hi)));
        }
    }

    void threshold() {
        // "gray tones ... lighter than the threshold point by outgoing video, and gray tones that are darker than the
        // threshold point by incoming video" (p.442): the Linear Ramp is dark on the left.
        model::Effect ramp = basic(QStringLiteral("Horiz"), QStringLiteral("Linear Ramp"));
        ramp.parameters.insert(QStringLiteral("Border"), QJsonObject{{QStringLiteral("Soft"), 0}});
        const Picture out = Picture::solid(64, 36, QColor(255, 0, 0));
        const Picture in = Picture::solid(64, 36, QColor(0, 0, 255));
        Picture half = plasmaWipePicture(ramp, out, in, 0.5, 0.5, model::EffectSpan(10, true));
        QVERIFY(near(at(half, 5, 18), QColor(0, 0, 255)));
        QVERIFY(near(at(half, 58, 18), QColor(255, 0, 0)));
        // The ends: only the outgoing, only the incoming, with the default softness too.
        model::Effect soft = basic(QStringLiteral("Horiz"), QStringLiteral("Linear Ramp"));
        QVERIFY(near(at(plasmaWipePicture(soft, out, in, 0.0, 0.0, model::EffectSpan(10, true)), 2, 18), QColor(255, 0, 0)));
        QVERIFY(near(at(plasmaWipePicture(soft, out, in, 1.0, 1.0, model::EffectSpan(10, true)), 61, 18), QColor(0, 0, 255)));
        // Softness blends at the edge.
        const QColor mid = at(plasmaWipePicture(soft, out, in, 0.5, 0.5, model::EffectSpan(10, true)), 32, 18);
        QVERIFY2(mid.red() > 40 && mid.blue() > 40, qPrintable(mid.name()));
        // A border band in the Border colour between the two (p.493).
        ramp.parameters.insert(QStringLiteral("Border"), QJsonObject{{QStringLiteral("Soft"), 0}, {QStringLiteral("Width"), 40},
                                                                     {QStringLiteral("Color"), QStringLiteral("#00ff00")},
                                                                     {QStringLiteral("Blend Color"), QStringLiteral("#00ff00")}});
        half = plasmaWipePicture(ramp, out, in, 0.5, 0.5, model::EffectSpan(10, true));
        int green = 0;
        for (int x = 0; x < 64; ++x) green += near(at(half, x, 18), QColor(0, 255, 0)) ? 1 : 0;
        QVERIFY2(green >= 3, qPrintable(QString::number(green)));
    }

    void frameEffect() {
        // A frame: the segment through the black middle, the tracks below through the white outside, the frame in the
        // Border colours (p.453-454).
        const model::Effect frame = basic(QStringLiteral("Borders"), QStringLiteral("Square Frame"));
        const Picture layer = Picture::solid(96, 72, QColor(0, 0, 255));
        const Picture below = Picture::solid(96, 72, QColor(255, 0, 0));
        const Picture shown = plasmaWipeSegmentPicture(frame, layer, below, 0.0, model::EffectSpan(10));
        QVERIFY(near(at(shown, 48, 36), QColor(0, 0, 255)));
        QVERIFY(near(at(shown, 0, 0), QColor(255, 0, 0)));
        // The band: grey towards white, neither picture.
        const QColor band = at(shown, 48, 2);
        QVERIFY2(band.red() == band.green() && band.green() == band.blue() && band.red() > 100, qPrintable(band.name()));
        // Not a transition effect: frames are segment effects only (p.452).
        const ui::EffectInfo* info = ui::EffectCatalog::builtin().find(frame.category, frame.name);
        QVERIFY(info && info->segment && !info->transition);
        const ui::EffectInfo* ramp = ui::EffectCatalog::builtin().find(QStringLiteral("PlasmaWipe Basic Horiz"), QStringLiteral("Linear Ramp"));
        QVERIFY(ramp && ramp->segment && ramp->transition);
        QVERIFY(ui::EffectCatalog::builtin().categories().contains(QStringLiteral("PlasmaWipe Basic Center")));
    }

    void folders() {
        // The user's own: PlasmaWipes/720x576/<folders>/<name>.raw, the folders in the category's name (p.450-451).
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString base = dir.filePath(QStringLiteral("PlasmaWipes/720x576/My Effects/Frames Border"));
        QVERIFY(QDir().mkpath(base));
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("PlasmaWipes/720x576/My Effects/Wipes"))));
        auto write = [](const QString& path, qsizetype size, char fill) {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) return false;
            return f.write(QByteArray(size, fill)) == size;
        };
        QVERIFY(write(base + QStringLiteral("/Big Frame.raw"), 720 * 576, '\x80'));
        QVERIFY(write(dir.filePath(QStringLiteral("PlasmaWipes/720x576/My Effects/Wipes/Fog.raw")), 720 * 486, '\x40'));
        QVERIFY(write(dir.filePath(QStringLiteral("PlasmaWipes/720x576/My Effects/Wipes/Broken.raw")), 1000, '\0'));
        setPlasmaWipeFolders({dir.filePath(QStringLiteral("PlasmaWipes"))});
        const PlasmaWipe* big = plasmaWipeOf(model::Effect{QStringLiteral("PlasmaWipe My Effects Frames Border"), QStringLiteral("Big Frame"), {}});
        QVERIFY(big);
        QVERIFY(big->frame);
        const PlasmaWipe* fog = plasmaWipeOf(model::Effect{QStringLiteral("PlasmaWipe My Effects Wipes"), QStringLiteral("Fog"), {}});
        QVERIFY(fog && !fog->frame);
        QCOMPARE(readPlasmaRaw(fog->path).size(), QSize(720, 486));
        const QImage g = plasmaWipeGradient(*fog, QSize(36, 24));
        QCOMPARE(g.size(), QSize(36, 24));
        QCOMPARE(int(g.constScanLine(5)[5]), 0x40);
        QVERIFY(!plasmaWipeOf(model::Effect{QStringLiteral("PlasmaWipe My Effects Wipes"), QStringLiteral("Broken"), {}}) ||
                readPlasmaRaw(dir.filePath(QStringLiteral("PlasmaWipes/720x576/My Effects/Wipes/Broken.raw"))).isNull());
        setPlasmaWipeFolders({});
        QVERIFY(!plasmaWipeOf(model::Effect{QStringLiteral("PlasmaWipe My Effects Wipes"), QStringLiteral("Fog"), {}}));
    }
};

QTEST_MAIN(TestPlasmaWipe)
#include "tst_plasma_wipe.moc"
