// The performance benchmarks (ROADMAP 10.1; docs/spec/performance.md). Each benchmark times one thing an editor
// waits for, on material made here so every machine runs the same work: compositing a 1080p frame with a dissolve, a
// 3D Warp, a Blur and a Color Correction; encoding and decoding 1080p FFV1 frames (FFV1 is free of patents, so it is
// in every build, LUMENIS_PATENT_CODECS or not); mixing 16 tracks of sound; writing, reading, showing and sorting a
// bin of 10,000 clips; and painting a Timeline of 16 tracks of 400 segments each.
//
//   lumenis_bench [--json <file>] [--only <name>[,<name>...]] [--quick] [--runs <n>] [--list]
//
// Prints one line per benchmark (median and fastest of its runs, in milliseconds) and, with --json, writes
// {"benchmarks": {<name>: {"median_ms", "min_ms", "runs"}}, "machine": {...}} for scripts/perf.py, which compares
// it with the times recorded in tests/data/perf_baseline.json. --quick runs each benchmark once (ctest's check that
// they all still run); --runs sets every benchmark's number of runs (long runs to profile, BUILDING.md "Profiling").
// Exit status 1 when a benchmark fails to run.
#include "lumenis/core/project_format.h"
#include "lumenis/ext/provider.h"
#include "lumenis/media/compositor.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/intraframe.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/color_correction.h"
#include "lumenis/model/correction_keyframes.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <vector>

using namespace lumenis;

namespace {

// One benchmark: `setup` runs once, untimed; `run` is timed `runs` times after one untimed warm-up run. `run` returns
// false when the work failed (the benchmark is reported as failed, not timed).
struct Benchmark {
    QString name;
    QString what; // one line for the report
    int runs = 5;
    std::function<bool()> setup;
    std::function<bool()> run;
};

const QSize kHd(1920, 1080);

model::Segment clip(const QString& id, std::int64_t length, const QString& track = QStringLiteral("V1")) {
    model::Segment s;
    s.binId = QStringLiteral("bin");
    s.clipId = id;
    s.name = id;
    s.sourceTrack = track;
    s.length = length;
    return s;
}

// A frame with detail in it (gradients and a grid), so effects and codecs have real work to do; one per clip id.
QImage testFrame(const QString& id) {
    QImage image(kHd, QImage::Format_RGB32);
    QPainter p(&image);
    QLinearGradient g(0, 0, kHd.width(), kHd.height());
    const int hue = int(qHash(id) % 360);
    g.setColorAt(0, QColor::fromHsv(hue, 200, 230));
    g.setColorAt(1, QColor::fromHsv((hue + 120) % 360, 180, 60));
    p.fillRect(image.rect(), g);
    p.setPen(QColor(255, 255, 255, 120));
    for (int x = 0; x < kHd.width(); x += 48) p.drawLine(x, 0, x, kHd.height());
    for (int y = 0; y < kHd.height(); y += 48) p.drawLine(0, y, kHd.width(), y);
    return image;
}

// A composition of `seq` at 1080p whose clips are test frames (made once each, so the source costs nothing).
media::Composition hdComposition(const model::Sequence& seq, const QStringList& tracks) {
    media::Composition c;
    c.sequence = &seq;
    c.tracks = tracks;
    c.size = kHd;
    c.precision = media::EffectPrecision::Bits8;
    auto frames = std::make_shared<QHash<QString, QImage>>();
    c.source = [frames](const model::Segment& s, std::int64_t, QSize size) -> std::optional<QImage> {
        auto it = frames->find(s.clipId);
        if (it == frames->end()) it = frames->insert(s.clipId, testFrame(s.clipId));
        return size == kHd ? *it : it->scaled(size);
    };
    return c;
}

// A sequence of one segment on V1 carrying `effect` (or none), over a second clip on V1's transition when
// `dissolve`.
struct EffectCase {
    model::Sequence seq = model::Sequence::withDefaultTracks();
    media::Composition composition;
};

std::shared_ptr<EffectCase> effectCase(std::optional<model::Effect> effect, bool dissolve) {
    auto c = std::make_shared<EffectCase>();
    c->seq.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("a"), 50), clip(QStringLiteral("b"), 50)});
    model::Track* v1 = c->seq.track(QStringLiteral("V1"));
    if (dissolve) {
        model::Transition t;
        t.effect.category = QStringLiteral("Blend");
        t.effect.name = QStringLiteral("Dissolve");
        t.duration = 20;
        t.alignment = model::TransitionAlignment::CenteredOnCut;
        v1->segments[1].transition = t;
    }
    if (effect) v1->segments[0].effect = *effect;
    c->composition = hdComposition(c->seq, {QStringLiteral("V1")});
    return c;
}

// Times `b`: the median and fastest run in milliseconds, or nothing when it failed.
struct Result {
    bool ok = false;
    double median = 0;
    double fastest = 0;
    int runs = 0;
};

Result time(const Benchmark& b, int runs) {
    Result r;
    if (b.setup && !b.setup()) return r;
    if (!b.run()) return r; // warm-up: caches, first allocations, lazily loaded tables
    std::vector<double> ms;
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer clock;
        clock.start();
        if (!b.run()) return r;
        ms.push_back(double(clock.nsecsElapsed()) / 1e6);
    }
    if (ms.empty()) return r; // runs < 1
    std::sort(ms.begin(), ms.end());
    r.ok = true;
    r.median = ms[ms.size() / 2];
    r.fastest = ms.front();
    r.runs = runs;
    return r;
}

// The benchmarks. `dir` holds their files; `window` is a main window with a project open, for the UI benchmarks.
QList<Benchmark> benchmarks(const QString& dir, ui::MainWindow& window) {
    QList<Benchmark> list;

    // Compositing (lumenis/media/compositor.h): one frame each run.
    {
        auto c = effectCase(std::nullopt, true);
        list << Benchmark{QStringLiteral("compose_dissolve_1080p"), QStringLiteral("a 1080p frame in the middle of a dissolve"), 20, {},
                          [c] { return !media::composeFrame(c->composition, 50).toImage().isNull(); }};
    }
    {
        model::Effect warp{QStringLiteral("Blend"), QStringLiteral("3D Warp"), {}};
        warp.parameters.insert(QStringLiteral("Rotation"), QJsonObject{{QStringLiteral("Z"), 30}, {QStringLiteral("Y"), 20}});
        warp.parameters.insert(QStringLiteral("Scaling (3D)"), QJsonObject{{QStringLiteral("X"), 80}, {QStringLiteral("Y"), 80}});
        auto c = effectCase(warp, false);
        list << Benchmark{QStringLiteral("compose_3d_warp_1080p"), QStringLiteral("a 1080p frame through a rotated 3D Warp"), 10, {},
                          [c] { return !media::composeFrame(c->composition, 10).toImage().isNull(); }};
    }
    {
        model::Effect blur{QStringLiteral("Image"), QStringLiteral("Blur Effect"),
                           QJsonObject{{QStringLiteral("Blur Effect Blur Parameters"),
                                        QJsonObject{{QStringLiteral("Hor"), 40}, {QStringLiteral("Vert"), 40}}}}};
        // Within a rectangle over most of the frame: without an object the Blur Effect shows the picture as it is (FX
        // Guide p.392), and the benchmark timed only a copy.
        model::addIntraframeObject(blur, model::IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.1, 0.1, 0.8, 0.8)));
        auto c = effectCase(blur, false);
        list << Benchmark{QStringLiteral("compose_blur_1080p"), QStringLiteral("a 1080p frame through the Blur Effect"), 10, {},
                          [c] { return !media::composeFrame(c->composition, 10).toImage().isNull(); }};
    }
    {
        model::Effect cc;
        cc.category = QStringLiteral("Image");
        cc.name = QStringLiteral("Color Correction");
        model::ColorCorrection correction;
        const model::EffectSpan span(50);
        model::setCorrectionKeyframe(correction, QStringLiteral("hsl/controls/master/gain"), 0.0, span, 80);
        model::setCorrectionKeyframe(correction, QStringLiteral("hsl/controls/master/saturation"), 0.0, span, 120);
        model::setColorCorrection(cc, correction);
        auto c = effectCase(cc, false);
        list << Benchmark{QStringLiteral("compose_color_correction_1080p"), QStringLiteral("a 1080p frame through a Color Correction"),
                          10, {}, [c] { return !media::composeFrame(c->composition, 10).toImage().isNull(); }};
    }

    // Encoding and decoding (lumenis/media/media.h): 25 frames of 1080p FFV1.
    const QString movie = QDir(dir).filePath(QStringLiteral("bench.mkv"));
    auto frames = std::make_shared<std::vector<QImage>>();
    auto encode = [movie, frames] {
        if (frames->empty()) {
            for (int i = 0; i < 25; ++i) frames->push_back(testFrame(QStringLiteral("f%1").arg(i % 5)));
        }
        media::WriterSettings s;
        s.format = QStringLiteral("matroska");
        s.videoCodec = QStringLiteral("ffv1");
        s.width = kHd.width();
        s.height = kHd.height();
        media::MediaWriter writer;
        if (!writer.open(movie, s)) return false;
        for (const QImage& f : *frames) {
            if (!writer.writeVideo(f)) return false;
        }
        return writer.finish();
    };
    list << Benchmark{QStringLiteral("encode_ffv1_1080p"), QStringLiteral("encoding 25 1080p frames as FFV1"), 3, {}, encode};
    list << Benchmark{QStringLiteral("decode_ffv1_1080p"), QStringLiteral("decoding 25 1080p FFV1 frames in order"), 5, encode, [movie] {
                          media::VideoReader reader;
                          if (!reader.open(movie)) return false;
                          for (int i = 0; i < 25; ++i) {
                              if (reader.frame(i).isNull()) return false;
                          }
                          return true;
                      }};
    list << Benchmark{QStringLiteral("seek_ffv1_1080p"), QStringLiteral("10 frames out of order (scrubbing), FFV1 1080p"), 5, encode, [movie] {
                          media::VideoReader reader;
                          if (!reader.open(movie)) return false;
                          for (int i : {20, 3, 17, 8, 24, 1, 12, 6, 22, 15}) {
                              if (reader.frame(i).isNull()) return false;
                          }
                          return true;
                      }};

    // Sound (lumenis/media/playback.h): 16 tracks read from a 48 kHz stereo WAV file (each track one of its channels,
    // from its own place in the file) mixed to stereo, 10 seconds in 1024-sample blocks.
    const QString wav = QDir(dir).filePath(QStringLiteral("bench.wav"));
    auto writeWav = [wav] {
        if (QFile::exists(wav)) return true;
        media::WriterSettings s;
        s.format = QStringLiteral("wav");
        s.audioCodec = QStringLiteral("pcm_s16le");
        s.sampleRate = 48000;
        s.channels = 2;
        media::MediaWriter writer;
        if (!writer.open(wav, s)) return false;
        std::vector<float> block(2 * 4800);
        for (int n = 0; n < 200; ++n) { // 20 seconds of noise-like sound
            for (std::size_t i = 0; i < block.size(); ++i) block[i] = float(int((qHash(int(n * 9600 + int(i))) % 2001)) - 1000) / 4000.0f;
            if (!writer.writeAudio(block.data(), 4800)) return false;
        }
        return writer.finish();
    };
    list << Benchmark{QStringLiteral("audio_mix_16_tracks"), QStringLiteral("mixing 16 tracks from a WAV file into 10 seconds of 48 kHz stereo"), 5,
                      writeWav, [wav] {
                          std::vector<media::AudioPiece> pieces;
                          for (int t = 0; t < 16; ++t) {
                              media::AudioPiece p;
                              p.path = wav;
                              p.channel = t % 2;
                              p.sourceStart = 48000LL * (t % 8); // each track from its own place in the file
                              p.at = 0;
                              p.length = 48000 * 10;
                              p.track = QStringLiteral("A%1").arg(t + 1);
                              p.left = p.right = 0.25f;
                              pieces.push_back(p);
                          }
                          media::AudioMix mix(pieces);
                          std::vector<float> out(std::size_t(mix.channels()) * 1024);
                          for (std::int64_t at = 0; at < 48000 * 10; at += 1024) mix.render(at, 1024, out.data());
                          return true;
                      }};

    // Bins (lumenis/model/bin.h, lumenis/ui/bin_view.h): 10,000 clips.
    auto bin = std::make_shared<model::Bin>();
    bin->name = QStringLiteral("Dailies");
    for (int i = 0; i < 10000; ++i) {
        model::BinItem item;
        item.id = model::newItemId();
        item.kind = model::BinItemKind::MasterClip;
        item.name = QStringLiteral("A%1_C%2_%3").arg(i % 37, 3, 10, QLatin1Char('0')).arg(i, 5, 10, QLatin1Char('0')).arg(qHash(i) % 9973);
        item.editRate = Rational(25);
        item.durationFrames = 100 + i % 900;
        item.fields.insert(QStringLiteral("Start"), QStringLiteral("01:00:00:00"));
        item.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1 A1-2"));
        bin->items.push_back(item);
    }
    const QString binPath = QDir(dir).filePath(QStringLiteral("Dailies") + model::BinFile::suffix());
    list << Benchmark{QStringLiteral("bin_write_10k"), QStringLiteral("saving a bin of 10,000 clips"), 5, {},
                      [bin, binPath] { return model::BinFile::write(binPath, *bin); }};
    list << Benchmark{QStringLiteral("bin_read_10k"), QStringLiteral("reading a bin of 10,000 clips"), 5,
                      [bin, binPath] { return model::BinFile::write(binPath, *bin); },
                      [binPath] { return model::BinFile::read(binPath).has_value(); }};
    // The bin's document and the view on it, shared by the two benchmarks below. The view refers to the document, so
    // it always goes first (whichever benchmark's copy is destroyed last; ASan found the document freed under it).
    struct BinUi {
        std::unique_ptr<ui::BinDocument> document;
        std::unique_ptr<ui::BinView> view;
        void reset() {
            view.reset();
            document.reset();
        }
        ~BinUi() { reset(); }
    };
    auto shown = std::make_shared<BinUi>();
    list << Benchmark{QStringLiteral("bin_show_10k"), QStringLiteral("opening a bin of 10,000 clips in a Bin view"), 3, {}, [bin, binPath, shown] {
                          shown->reset();
                          shown->document = std::make_unique<ui::BinDocument>(QStringLiteral("bench"), binPath, *bin);
                          shown->view = std::make_unique<ui::BinView>(*shown->document, nullptr);
                          shown->view->resize(900, 700);
                          shown->view->show();
                          QCoreApplication::processEvents();
                          return true;
                      }};
    auto ascending = std::make_shared<bool>(true);
    list << Benchmark{QStringLiteral("bin_sort_10k"), QStringLiteral("sorting a bin of 10,000 clips by Name"), 5, {}, [shown, ascending] {
                          if (!shown->view) return false;
                          shown->view->sortOn(QStringLiteral("Name"), *ascending);
                          *ascending = !*ascending;
                          QCoreApplication::processEvents();
                          return true;
                      }};

    // The Timeline (lumenis/ui/timeline.h): 8 video and 8 audio tracks of 400 segments each, the whole sequence shown.
    list << Benchmark{QStringLiteral("timeline_paint_16x400"), QStringLiteral("painting a Timeline of 16 tracks of 400 segments"), 10,
                      [&window] {
                          const QString binId = window.binContainer().newBin();
                          ui::BinDocument* doc = window.binContainer().openBin(binId);
                          if (!doc) return false;
                          model::Sequence seq = model::Sequence::withDefaultTracks();
                          auto count = [&seq](QChar kind) {
                              int n = 0;
                              for (const QString& name : seq.trackNames()) n += name.startsWith(kind) ? 1 : 0;
                              return n;
                          };
                          while (count(QLatin1Char('V')) < 8) seq.addTrack(model::TrackKind::Video);
                          while (count(QLatin1Char('A')) < 8) seq.addTrack(model::TrackKind::Audio);
                          for (const QString& track : seq.trackNames()) {
                              QList<model::Segment> segments;
                              for (int i = 0; i < 400; ++i) {
                                  segments << clip(QStringLiteral("%1_%2").arg(track).arg(i % 23), 10 + (i * 7) % 40, track.startsWith(QLatin1Char('V')) ? QStringLiteral("V1") : QStringLiteral("A1"));
                              }
                              seq.splice(track, 0, segments);
                          }
                          model::BinItem item;
                          item.id = model::newItemId();
                          item.name = QStringLiteral("Long cut");
                          item.kind = model::BinItemKind::Sequence;
                          item.editRate = Rational(25);
                          seq.storeIn(item);
                          doc->addItem(item);
                          window.composer().load(ui::MonitorId::Record, binId, item);
                          QCoreApplication::processEvents();
                          return true;
                      },
                      [&window] {
                          ui::Timeline& timeline = window.timeline();
                          QImage image(timeline.size(), QImage::Format_ARGB32_Premultiplied);
                          timeline.render(&image);
                          return !image.isNull();
                      }};
    return list;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Cutthulhu's performance benchmarks (ROADMAP 10.1)."));
    parser.addHelpOption();
    const QCommandLineOption json(QStringLiteral("json"), QStringLiteral("Write the times to <file> as JSON."), QStringLiteral("file"));
    const QCommandLineOption only(QStringLiteral("only"), QStringLiteral("Run only these benchmarks (comma-separated)."), QStringLiteral("names"));
    const QCommandLineOption quick(QStringLiteral("quick"), QStringLiteral("Run each benchmark once (a check that they run)."));
    const QCommandLineOption list(QStringLiteral("list"), QStringLiteral("List the benchmarks."));
    const QCommandLineOption runs(QStringLiteral("runs"), QStringLiteral("Time each benchmark <n> times."), QStringLiteral("n"));
    parser.addOptions({json, only, quick, list, runs});
    parser.process(app);

    ui::applyTheme(app);
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    ext::ProviderRegistry providers;
    ui::CommandRegistry commands;
    commands.loadBuiltin();
    ui::MainWindow window(commands, providers);
    window.resize(1600, 900);
    model::ProjectSettings settings;
    settings.name = QStringLiteral("Bench");
    settings.format = projectFormatPresets().front();
    const QString users = QDir(dir.path()).filePath(QStringLiteral("Users"));
    model::UserProfiles(users).create(QStringLiteral("editor"));
    window.settings().setUser(users, QStringLiteral("editor"));
    const auto entry = model::ProjectStore(QDir(dir.path()).filePath(QStringLiteral("Projects"))).create(settings);
    if (!entry) return 1;
    window.setProject(*entry);
    window.show();
    QCoreApplication::processEvents();

    const QList<Benchmark> all = benchmarks(dir.path(), window);
    if (parser.isSet(list)) {
        for (const Benchmark& b : all) std::printf("%-32s %s\n", qPrintable(b.name), qPrintable(b.what));
        return 0;
    }
    const QStringList wanted = parser.value(only).split(QLatin1Char(','), Qt::SkipEmptyParts);
    QJsonObject results;
    int failed = 0;
    for (const Benchmark& b : all) {
        if (!wanted.isEmpty() && !wanted.contains(b.name)) continue;
        const int count = parser.isSet(quick) ? 1 : parser.isSet(runs) ? std::max(1, parser.value(runs).toInt()) : b.runs;
        const Result r = time(b, count);
        if (!r.ok) {
            std::printf("%-32s FAILED\n", qPrintable(b.name));
            ++failed;
            continue;
        }
        std::printf("%-32s %9.2f ms median %9.2f ms fastest (%d runs)  %s\n", qPrintable(b.name), r.median, r.fastest, r.runs,
                    qPrintable(b.what));
        std::fflush(stdout);
        results.insert(b.name, QJsonObject{{QStringLiteral("median_ms"), r.median}, {QStringLiteral("min_ms"), r.fastest},
                                           {QStringLiteral("runs"), r.runs}});
    }
    if (parser.isSet(json)) {
        const QJsonObject machine{{QStringLiteral("host"), QSysInfo::machineHostName()},
                                  {QStringLiteral("os"), QSysInfo::prettyProductName()},
                                  {QStringLiteral("cpu"), QSysInfo::currentCpuArchitecture()},
                                  {QStringLiteral("threads"), QThread::idealThreadCount()}};
        QFile out(parser.value(json));
        if (!out.open(QIODevice::WriteOnly)) return 1;
        out.write(QJsonDocument(QJsonObject{{QStringLiteral("benchmarks"), results}, {QStringLiteral("machine"), machine}})
                      .toJson(QJsonDocument::Indented));
    }
    return failed ? 1 : 0;
}
