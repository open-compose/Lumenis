// The Digital Cut tool's output formats (output_format.h; Editing Guide p.932-937, p.1298-1299).
#include "lumenis/model/output_format.h"

#include <QObject>

#include <algorithm>
#include <array>
#include <cmath>

namespace lumenis::model {

namespace {

const Rational k23976(24000, 1001);
const Rational k2997(30000, 1001);

OutputFormat format(const QString& label, const QString& description, const Rational& play, const Rational& tape,
                    bool interlaced) {
    OutputFormat f;
    f.label = label;
    f.description = description;
    f.playRate = play;
    f.tapeRate = tape;
    f.interlaced = interlaced;
    f.ntsc = tape == k2997;
    // 23.976 frames onto 59.94 fields: 2:3 pulldown.
    f.cadence = interlaced && play == k23976 && tape == k2997;
    return f;
}

QString plays(const Rational& rate) {
    return QObject::tr("Plays back the sequence at %1 fps.").arg(QString::fromStdString(frameRateText(rate)));
}

// The table on p.933-935, NTSC or PAL by the project.
QList<OutputFormat> filmProjectFormats(bool ntsc) {
    QList<OutputFormat> out;
    if (ntsc) {
        out << format(QObject::tr("23.976 (NTSC)"),
                      QObject::tr("Plays back the sequence at 23.976 fps (film rate) with 2:3 pulldown, for NTSC video "
                                  "output such as broadcast masters. Pulldown is set to On (0.99)."),
                      k23976, k2997, true);
        OutputFormat film = format(QObject::tr("24 (NTSC)"),
                                   QObject::tr("Plays back the sequence at 24 fps (film rate), for audio in sync with film "
                                               "projection; the video is for reference only. Pulldown is set to Off (1.00)."),
                                   Rational(24), k2997, true);
        film.destinationRate = true;
        out << film;
        out << format(QObject::tr("29.97 (NTSC)"),
                      QObject::tr("Plays back the sequence at 29.97 fps without adding pulldown fields, for animations and "
                                  "tape-to-film transfers. Pulldown is set to On (0.99)."),
                      k2997, k2997, true);
    } else {
        out << format(QObject::tr("24 (PAL)"),
                      QObject::tr("Plays back the sequence at 24 fps, for audio for film projection; the video is for "
                                  "reference only. Pulldown is set to Off (1.00)."),
                      Rational(24), Rational(25), true);
        out << format(QObject::tr("25 (PAL)"),
                      QObject::tr("Plays back the sequence at 25 fps, for PAL video output such as a broadcast master. "
                                  "There are no pulldown frames. Pulldown is set to Off (1.00)."),
                      Rational(25), Rational(25), true);
    }
    return out;
}

OutputFormat downconvert(const QString& label, const Rational& rate, int width, int height) {
    OutputFormat f = format(label, plays(rate) + QLatin1Char(' ') + QObject::tr("Downconverted to standard definition."), rate,
                            rate, true);
    f.width = width;
    f.height = height;
    return f;
}

// HD Universal Mastering's table (p.1299).
QList<OutputFormat> universalMasteringFormats(const ProjectFormat& project) {
    const Rational rate = project.editRate;
    QList<OutputFormat> out;
    if (project.scan == ScanMode::Interlaced) {
        if (rate == Rational(25)) {
            out << format(QStringLiteral("1080i50"), plays(rate), rate, rate, true);
            out << downconvert(QStringLiteral("PAL 25i"), rate, 720, 576);
        } else if (rate == k2997) {
            out << format(QStringLiteral("1080i59.94"), plays(rate), rate, rate, true);
            out << downconvert(QStringLiteral("NTSC 30i"), rate, 720, 486);
        }
        return out;
    }
    auto progressive = [&](const QString& label, const Rational& play) {
        return format(label, plays(play), play, play, false);
    };
    auto ntsc = [&](const QString& label) {
        return format(label, plays(k23976) + QLatin1Char(' ') + QObject::tr("2:3 pulldown onto 59.94 fields."), k23976, k2997,
                      true);
    };
    if (rate == k23976) {
        out << progressive(QStringLiteral("1080p23.976"), k23976) << progressive(QStringLiteral("1080p24"), Rational(24))
            << progressive(QStringLiteral("1080p25/PAL"), Rational(25)) << ntsc(QStringLiteral("1080i59.94/NTSC"));
    } else if (rate == Rational(24)) {
        out << progressive(QStringLiteral("1080p24"), Rational(24)) << progressive(QStringLiteral("1080p23.976"), k23976)
            << progressive(QStringLiteral("1080p25/PAL"), Rational(25)) << ntsc(QStringLiteral("1080i59.94/NTSC"));
    } else if (rate == Rational(25)) {
        out << progressive(QStringLiteral("1080p25 PAL"), Rational(25)) << progressive(QStringLiteral("1080p23.976"), k23976)
            << progressive(QStringLiteral("1080p24"), Rational(24)) << ntsc(QStringLiteral("1080i59.94 NTSC"));
    }
    return out;
}

// The 2:3:3:2 cycle's frames for its ten fields: A2 B3 C3 D2.
constexpr std::array<int, 10> kAdvanced{0, 0, 1, 1, 1, 2, 2, 2, 3, 3};

std::int64_t fieldsPerFrame(const OutputFormat& f) { return f.interlaced ? 2 : 1; }

} // namespace

QList<OutputFormat> outputFormats(const ProjectFormat& project) {
    QList<OutputFormat> out;
    const Rational rate = project.editRate;
    if (project.family == FormatFamily::SD && project.scan == ScanMode::Progressive) {
        if (project.group == "NTSC" && (rate == k23976 || rate == Rational(24))) out = filmProjectFormats(true);
        if (project.group == "PAL" && (rate == Rational(24) || rate == Rational(25))) out = filmProjectFormats(false);
    } else if (project.family == FormatFamily::HD && project.group.rfind("HD 1080", 0) == 0) {
        out = universalMasteringFormats(project);
    }
    // The project's own play rate first (for SD 23.976p, 24p and 25p projects, the format that plays the sequence at its
    // own rate).
    const auto own = std::find_if(out.begin(), out.end(), [&](const OutputFormat& f) { return f.playRate == rate; });
    if (own != out.end() && own != out.begin()) std::rotate(out.begin(), own, own + 1);
    if (out.isEmpty()) {
        out << format(QString::fromStdString(project.label()), plays(rate), rate, rate, project.scan == ScanMode::Interlaced);
    }
    return out;
}

double outputSpeed(const OutputFormat& format, const Rational& sequenceRate) {
    return sequenceRate.num() == 0 ? 1.0 : format.playRate.toDouble() / sequenceRate.toDouble();
}

std::int64_t outputSourceFrame(const OutputFormat& format, std::int64_t field, PulldownCadence cadence, bool pullinB) {
    if (field < 0) return 0;
    const bool cycle = format.cadence;
    // A B frame first: the cycle two fields (A's) on, one frame back.
    const std::int64_t at = cycle && pullinB ? field + 2 : field;
    const std::int64_t back = cycle && pullinB ? 1 : 0;
    if (cycle && cadence == PulldownCadence::Advanced) return (at / 10) * 4 + kAdvanced[static_cast<std::size_t>(at % 10)] - back;
    // The frame playing at the field's middle: floor((field + 1/2) x play rate / field rate), exactly.
    const Rational fieldRate = format.tapeRate * Rational(fieldsPerFrame(format));
    const Rational ratio = format.playRate / fieldRate;
    const std::int64_t num = (2 * at + 1) * ratio.num();
    const std::int64_t den = 2 * ratio.den();
    return num / den - back;
}

std::int64_t outputFrameCount(const OutputFormat& format, std::int64_t frames, PulldownCadence cadence, bool pullinB) {
    if (frames <= 0) return 0;
    const std::int64_t perFrame = fieldsPerFrame(format);
    const double fieldRate = format.tapeRate.toDouble() * static_cast<double>(perFrame);
    // From an estimate, the first field past the last frame.
    std::int64_t field = std::max<std::int64_t>(
        0, static_cast<std::int64_t>(std::floor(static_cast<double>(frames) * fieldRate / format.playRate.toDouble())) - 4);
    while (outputSourceFrame(format, field, cadence, pullinB) < frames) ++field;
    while (field > 0 && outputSourceFrame(format, field - 1, cadence, pullinB) >= frames) --field;
    return (field + perFrame - 1) / perFrame;
}

std::int64_t sameTimecodeFrames(std::int64_t frames, const Rational& from, const Rational& to) {
    const std::int64_t f = from.round();
    const std::int64_t t = to.round();
    if (f <= 0 || f == t) return frames;
    return static_cast<std::int64_t>(std::llround(static_cast<double>(frames) * static_cast<double>(t) / static_cast<double>(f)));
}

} // namespace lumenis::model
