// Fields, pulldown and format conversions (FX Guide p.181-186, p.203-205; lumenis/media/fields.h): field
// pictures and weaving, the 2:3 cadence and finding its phase, repeated frames, adaptive deinterlacing and
// whole conversions of Timewarp effects.
#include "lumenis/media/fields.h"

#include <QTest>

#include <cmath>

using namespace lumenis::media;

namespace {

// A 16x8 frame whose even lines are grey `even` and odd lines grey `odd`.
QImage lines(int even, int odd) {
    QImage image(16, 8, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        const int v = (y % 2 == 0) ? even : odd;
        for (int x = 0; x < image.width(); ++x) image.setPixel(x, y, qRgb(v, v, v));
    }
    return image;
}

int greyAt(const QImage& image, int y) {
    return qRed(image.pixel(4, y));
}

} // namespace

class TestFields : public QObject {
    Q_OBJECT

private slots:
    void fieldPictures() {
        const QImage frame = lines(100, 200);
        // Upper field first: the first field is the even lines.
        const QImage first = fieldPicture(frame, 0, true, FieldFill::Duplicate);
        for (int y = 0; y < 8; ++y) QCOMPARE(greyAt(first, y), 100);
        const QImage second = fieldPicture(frame, 1, true, FieldFill::Interpolate);
        for (int y = 0; y < 8; ++y) QCOMPARE(greyAt(second, y), 200);
        // Lower field first: the first field is the odd lines.
        QCOMPARE(greyAt(fieldPicture(frame, 0, false, FieldFill::Duplicate), 0), 200);
        // Interpolation averages the lines around a missing one.
        QImage ramp(4, 5, QImage::Format_ARGB32);
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 4; ++x) ramp.setPixel(x, y, qRgb(y * 40, y * 40, y * 40));
        }
        const QImage filled = fieldPicture(ramp, 0, true, FieldFill::Interpolate);
        QCOMPARE(qRed(filled.pixel(0, 1)), 40); // (0 + 80) / 2
        QCOMPARE(qRed(filled.pixel(0, 2)), 80);
        // VTR-Style: each pair of lines shows the field's line of the pair.
        const QImage vtr = fieldPicture(ramp, 1, true, FieldFill::ShiftLine);
        QCOMPARE(qRed(vtr.pixel(0, 0)), 40);
        QCOMPARE(qRed(vtr.pixel(0, 1)), 40);
        QCOMPARE(qRed(vtr.pixel(0, 2)), 120);
        // Duplicated Field of the second field copies the line above instead: a line lower.
        const QImage dup = fieldPicture(ramp, 1, true, FieldFill::Duplicate);
        QCOMPARE(qRed(dup.pixel(0, 2)), 40);
    }

    void weaving() {
        const QImage woven = weaveFields(lines(10, 10), lines(90, 90), true);
        QCOMPARE(greyAt(woven, 0), 10);
        QCOMPARE(greyAt(woven, 1), 90);
        QCOMPARE(greyAt(weaveFields(lines(10, 10), lines(90, 90), false), 0), 90);
        // The halves of a frame woven back are the frame.
        const QImage frame = lines(30, 60);
        const QImage back = weaveFields(fieldPicture(frame, 0, true, FieldFill::Duplicate), fieldPicture(frame, 1, true, FieldFill::Duplicate), true);
        for (int y = 0; y < 8; ++y) QCOMPARE(greyAt(back, y), greyAt(frame, y));
        QVERIFY(std::abs(greyAt(blendImages(lines(0, 0), lines(200, 200), 0.25), 0) - 50) <= 1);
    }

    void pulldownCadence() {
        // AA BB BC CD DD: film frames by field from an A frame.
        const int expected[] = {0, 0, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4};
        for (int g = 0; g < 12; ++g) QCOMPARE(pulldownFilmFrame(g, 0), expected[g]);
        QCOMPARE(pulldownFirstField(0, 0), 0);
        QCOMPARE(pulldownFirstField(1, 0), 2);
        QCOMPARE(pulldownFirstField(2, 0), 5);
        QCOMPARE(pulldownFirstField(3, 0), 7);
        QCOMPARE(pulldownFirstField(4, 0), 10);
        QCOMPARE(pulldownWholeFrame(0, 0), 0);
        QCOMPARE(pulldownWholeFrame(1, 0), 1);
        QVERIFY(!pulldownWholeFrame(2, 0));
        QVERIFY(!pulldownWholeFrame(3, 0));
        QCOMPARE(pulldownWholeFrame(4, 0), 3);
        // A clip starting on the BC frame: its first film frame is B, its first field B's last.
        QCOMPARE(pulldownFilmFrame(0, 2), 0);
        QCOMPARE(pulldownFilmFrame(1, 2), 1);
        QCOMPARE(pulldownFirstField(1, 2), 1);
        QCOMPARE(pulldownFirstField(0, 2), -2); // B's fields are the cycle's 2-4, the clip starting on 4
        QVERIFY(!pulldownWholeFrame(0, 2));
        for (int p = 0; p < 5; ++p) {
            for (int film = 0; film < 9; ++film) QCOMPARE(pulldownFilmFrame(pulldownFirstField(film, p), p), film);
        }
    }

    void pulldownDetection() {
        // Film frames of grey 20k laid down in 2:3 from phase 3 (a CD frame first): split frames comb.
        const int phase = 3;
        const FrameGetter video = [&](std::int64_t frame) {
            const auto a = pulldownFilmFrame(2 * frame, phase);
            const auto b = pulldownFilmFrame(2 * frame + 1, phase);
            return lines(static_cast<int>(20 * (a + 1)), static_cast<int>(20 * (b + 1)));
        };
        QCOMPARE(detectPulldownPhase(video, 15, true), phase);
        // Nothing moving: no phase can be told.
        QCOMPARE(detectPulldownPhase([](std::int64_t) { return lines(50, 50); }, 15, true), 0);
        // Pulldown Removal: the film frames come back whole.
        FieldConversion removal;
        removal.source = SourceFormat::FilmPulldown;
        removal.output = OutputFormat::Progressive;
        removal.phase = phase;
        // Output frame t shows film position t * 4/5 (30 frames a second of 24 film frames), sampled at the
        // source position 1.25 output frames each film frame.
        const auto realTime = [](double t) { return t; };
        for (int t = 0; t < 10; ++t) {
            const QImage out = convertedFrame(video, realTime, t, removal);
            QCOMPARE(greyAt(out, 0), greyAt(out, 1)); // whole: one film frame
        }
        // Pulldown Insertion: progressive frames into 2:3 cadence fields (the third frame is split).
        FieldConversion insertion;
        insertion.source = SourceFormat::Progressive;
        insertion.output = OutputFormat::FilmPulldown;
        const FrameGetter film = [](std::int64_t k) { return lines(static_cast<int>(10 * (k + 1)), static_cast<int>(10 * (k + 1))); };
        const auto filmTime = [](double t) { return t * 0.8; }; // the source's frames, 24 to 30
        const QImage split = convertedFrame(film, filmTime, 2, insertion);
        QCOMPARE(greyAt(split, 0), 20); // B
        QCOMPARE(greyAt(split, 1), 30); // C
        const QImage whole = convertedFrame(film, filmTime, 4, insertion);
        QCOMPARE(greyAt(whole, 0), 40);
        QCOMPARE(greyAt(whole, 1), 40);
    }

    void repeatedFrames() {
        const FrameGetter threes = [](std::int64_t f) { return lines(static_cast<int>(10 * (f / 3)), static_cast<int>(10 * (f / 3))); };
        QCOMPARE(detectRepeatCount(threes, 30), 3);
        QCOMPARE(detectRepeatCount([](std::int64_t f) { return lines(static_cast<int>(f), static_cast<int>(f)); }, 30), 1);
        // Progressive Repeated with blending: in-between pictures between the unique ones.
        FieldConversion c;
        c.source = SourceFormat::ProgressiveRepeated;
        c.output = OutputFormat::Progressive;
        c.repeat = 3;
        c.rendering = FieldRendering::BlendedInterpolated;
        const QImage mid = convertedFrame(threes, [](double t) { return t; }, 1.5, c);
        QVERIFY(std::abs(greyAt(mid, 0) - 5) <= 1); // half way between pictures 0 and 10
    }

    void interlacedSources() {
        // An interlaced source whose frame n has fields at grey 20n (first) and 20n + 10 (second).
        const FrameGetter source = [](std::int64_t n) { return lines(static_cast<int>(20 * n), static_cast<int>(20 * n + 10)); };
        FieldConversion c;
        c.source = SourceFormat::Interlaced;
        c.output = OutputFormat::Progressive;
        const auto half = [](double t) { return t * 0.5; }; // 50% speed
        // Interpolated Field: each output frame one field, in time order.
        c.rendering = FieldRendering::InterpolatedField;
        QCOMPARE(greyAt(convertedFrame(source, half, 0, c), 3), 0);
        QCOMPARE(greyAt(convertedFrame(source, half, 1, c), 3), 10);
        QCOMPARE(greyAt(convertedFrame(source, half, 2, c), 3), 20);
        // Duplicated Field: the first field only.
        c.rendering = FieldRendering::DuplicatedField;
        QCOMPARE(greyAt(convertedFrame(source, half, 1, c), 3), 0);
        // Both Fields: whole frames.
        c.rendering = FieldRendering::BothFields;
        QCOMPARE(greyAt(convertedFrame(source, half, 1, c), 1), 10);
        // Interlaced output at 100%: the source frame itself.
        c.output = OutputFormat::Interlaced;
        c.rendering = FieldRendering::InterpolatedField;
        const QImage same = convertedFrame(source, [](double t) { return t; }, 3, c);
        QCOMPARE(greyAt(same, 0), 60);
        QCOMPARE(greyAt(same, 1), 70);
        // Blended Interpolated at 25% of a field: between field pictures.
        c.output = OutputFormat::Progressive;
        c.rendering = FieldRendering::BlendedInterpolated;
        QVERIFY(std::abs(greyAt(convertedFrame(source, [](double t) { return t * 0.125; }, 1, c), 2) - 2.5) <= 1.0); // a quarter of 10
    }

    void adaptive() {
        // A still picture keeps both fields; a moving one has its other field interpolated.
        const QImage still = lines(80, 120);
        const QImage kept = adaptiveDeinterlace(still, still, still, 0, true);
        QCOMPARE(greyAt(kept, 1), 120);
        const QImage moving = adaptiveDeinterlace(lines(0, 0), still, lines(250, 250), 0, true);
        QCOMPARE(greyAt(moving, 1), 80);
    }
};

QTEST_GUILESS_MAIN(TestFields)
#include "tst_fields.moc"
