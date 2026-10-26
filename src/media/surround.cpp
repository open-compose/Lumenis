#include "lumenis/media/surround.h"

#include <QObject>

// Surround mixing (surround.h). Each fold is a matrix of gains, one row for each speaker (or output
// channel) it makes and one column for each speaker of the bus; outputMatrix() multiplies the
// monitor's by the sequence's.
namespace lumenis::media {

namespace {

constexpr float kHalf = 0.70710678f; // 3 dB down: one side of an equal-power pan to the middle

using S = Speaker;

constexpr int at(Speaker s) {
    return static_cast<int>(s);
}

SpeakerGains only(Speaker s) {
    SpeakerGains g{};
    g[static_cast<std::size_t>(at(s))] = 1.0f;
    return g;
}

SpeakerGains& add(SpeakerGains& g, Speaker s, float gain) {
    g[static_cast<std::size_t>(at(s))] += gain;
    return g;
}

// Stereo's left or right (p.687-688): its own side, C and Lfe in the middle, the surrounds of its side
// 3 dB down.
SpeakerGains stereoSide(bool left) {
    SpeakerGains g = only(left ? S::L : S::R);
    add(g, S::C, kHalf);
    add(g, S::Lfe, kHalf);
    add(g, left ? S::Lss : S::Rss, kHalf);
    add(g, left ? S::Lsr : S::Rsr, kHalf);
    return g;
}

// A 5.1 speaker playing 7.1 (p.688): the side surrounds in the middle of the L and Lsr (R and Rsr)
// pairs; the other speakers as they are.
SpeakerGains fiveOne(Speaker s) {
    SpeakerGains g = only(s);
    if (s == S::L || s == S::Lsr) add(g, S::Lss, kHalf);
    if (s == S::R || s == S::Rsr) add(g, S::Rss, kHalf);
    return g;
}

// A sequence's fold: eight rows, a 7.1 sequence's the identity.
std::vector<SpeakerGains> sequenceFold(model::AudioFormat format) {
    std::vector<SpeakerGains> rows(kSpeakers, SpeakerGains{});
    for (int s = 0; s < kSpeakers; ++s) {
        const auto speaker = static_cast<Speaker>(s);
        switch (format) {
        case model::AudioFormat::Surround71: rows[static_cast<std::size_t>(s)] = only(speaker); break;
        case model::AudioFormat::Surround51:
            if (speaker != S::Lss && speaker != S::Rss) rows[static_cast<std::size_t>(s)] = fiveOne(speaker);
            break;
        case model::AudioFormat::Mono:
        case model::AudioFormat::Stereo:
            if (speaker == S::L || speaker == S::R) rows[static_cast<std::size_t>(s)] = stereoSide(speaker == S::L);
            break;
        }
    }
    return rows;
}

// The monitor's rows, from all eight speakers.
std::vector<SpeakerGains> monitorFold(MonitorMix mix) {
    switch (mix) {
    case MonitorMix::Stereo: return {stereoSide(true), stereoSide(false)};
    case MonitorMix::Mono: {
        SpeakerGains g{};
        g.fill(kHalf);
        return {g};
    }
    case MonitorMix::LrcLfe: {
        // "Rear and side channels are mixed at -3dB into front left and right" (p.691).
        SpeakerGains l = only(S::L);
        SpeakerGains r = only(S::R);
        add(add(l, S::Lss, kHalf), S::Lsr, kHalf);
        add(add(r, S::Rss, kHalf), S::Rsr, kHalf);
        return {l, r, only(S::C), only(S::Lfe)};
    }
    case MonitorMix::Lrcs: {
        // "Lfe is mixed at -3dB into front left and right. Rear channels panned into the single rear
        // center. Side channels ... are panned between the single rear channel and the corresponding
        // front channel" (p.691).
        SpeakerGains l = only(S::L);
        SpeakerGains r = only(S::R);
        add(add(l, S::Lfe, kHalf), S::Lss, kHalf);
        add(add(r, S::Lfe, kHalf), S::Rss, kHalf);
        SpeakerGains rear{};
        for (Speaker s : {S::Lsr, S::Rsr, S::Lss, S::Rss}) add(rear, s, kHalf);
        return {l, r, only(S::C), rear};
    }
    case MonitorMix::Quad: {
        // "Center, Left Center, and Right Center are panned to the centers of their corresponding
        // speaker pairs. Lfe is mixed at -3dB into front left and right" (p.691).
        SpeakerGains l = only(S::L);
        SpeakerGains r = only(S::R);
        add(add(add(l, S::C, kHalf), S::Lfe, kHalf), S::Lss, kHalf);
        add(add(add(r, S::C, kHalf), S::Lfe, kHalf), S::Rss, kHalf);
        SpeakerGains ls = only(S::Lsr);
        SpeakerGains rs = only(S::Rsr);
        add(ls, S::Lss, kHalf);
        add(rs, S::Rss, kHalf);
        return {l, r, ls, rs};
    }
    case MonitorMix::ProTools51: return {fiveOne(S::L), fiveOne(S::C), fiveOne(S::R), fiveOne(S::Lsr), fiveOne(S::Rsr), fiveOne(S::Lfe)};
    case MonitorMix::Smpte51: return {fiveOne(S::L), fiveOne(S::R), fiveOne(S::C), fiveOne(S::Lfe), fiveOne(S::Lsr), fiveOne(S::Rsr)};
    case MonitorMix::ProTools71:
        return {only(S::L), only(S::C), only(S::R), only(S::Lss), only(S::Rss), only(S::Lsr), only(S::Rsr), only(S::Lfe)};
    case MonitorMix::Ext71:
        return {only(S::L), only(S::R), only(S::C), only(S::Lfe), only(S::Lsr), only(S::Rsr), only(S::Lss), only(S::Rss)};
    case MonitorMix::SmpteDs71:
        return {only(S::L), only(S::R), only(S::C), only(S::Lfe), only(S::Lss), only(S::Rss), only(S::Lsr), only(S::Rsr)};
    }
    return {stereoSide(true), stereoSide(false)};
}

// rows × fold: each row's gains through the fold.
std::vector<SpeakerGains> through(const std::vector<SpeakerGains>& rows, const std::vector<SpeakerGains>& fold) {
    std::vector<SpeakerGains> out(rows.size(), SpeakerGains{});
    for (std::size_t r = 0; r < rows.size(); ++r) {
        for (std::size_t mid = 0; mid < fold.size(); ++mid) {
            const float g = rows[r][mid];
            if (g == 0.0f) continue;
            for (std::size_t s = 0; s < kSpeakers; ++s) out[r][s] += g * fold[mid][s];
        }
    }
    return out;
}

} // namespace

QString speakerName(Speaker speaker) {
    static const char* const names[] = {"L", "R", "C", "Lfe", "Lss", "Rss", "Lsr", "Rsr"};
    return QString::fromLatin1(names[at(speaker)]);
}

QList<Speaker> trackSpeakers(model::AudioFormat format) {
    switch (format) {
    case model::AudioFormat::Mono: return {S::L};
    case model::AudioFormat::Stereo: return {S::L, S::R};
    case model::AudioFormat::Surround51: return {S::L, S::C, S::R, S::Lsr, S::Rsr, S::Lfe};
    case model::AudioFormat::Surround71: return {S::L, S::C, S::R, S::Lss, S::Rss, S::Lsr, S::Rsr, S::Lfe};
    }
    return {S::L};
}

QList<MonitorMix> monitorMixes() {
    return {MonitorMix::Stereo,     MonitorMix::Mono,    MonitorMix::LrcLfe,     MonitorMix::Lrcs,  MonitorMix::Quad,
            MonitorMix::ProTools51, MonitorMix::Smpte51, MonitorMix::ProTools71, MonitorMix::Ext71, MonitorMix::SmpteDs71};
}

QString monitorMixId(MonitorMix mix) {
    switch (mix) {
    case MonitorMix::Stereo: return QStringLiteral("stereo");
    case MonitorMix::Mono: return QStringLiteral("mono");
    case MonitorMix::LrcLfe: return QStringLiteral("lrclfe");
    case MonitorMix::Lrcs: return QStringLiteral("lrcs");
    case MonitorMix::Quad: return QStringLiteral("quad");
    case MonitorMix::ProTools51: return QStringLiteral("5.1-protools");
    case MonitorMix::Smpte51: return QStringLiteral("5.1-smpte");
    case MonitorMix::ProTools71: return QStringLiteral("7.1-protools");
    case MonitorMix::Ext71: return QStringLiteral("7.1-ext");
    case MonitorMix::SmpteDs71: return QStringLiteral("7.1-smpte-ds");
    }
    return QStringLiteral("stereo");
}

std::optional<MonitorMix> monitorMixFromId(const QString& id) {
    for (MonitorMix m : monitorMixes()) {
        if (monitorMixId(m) == id) return m;
    }
    return std::nullopt;
}

QString monitorMixName(MonitorMix mix) {
    switch (mix) {
    case MonitorMix::Stereo: return QObject::tr("Stereo");
    case MonitorMix::Mono: return QObject::tr("Mono");
    case MonitorMix::LrcLfe: return QObject::tr("4-channel LRCLfe");
    case MonitorMix::Lrcs: return QObject::tr("4-channel LRCS");
    case MonitorMix::Quad: return QObject::tr("4-channel Quadraphonic");
    case MonitorMix::ProTools51: return QObject::tr("5.1 Pro Tools: L C R Ls Rs Lfe");
    case MonitorMix::Smpte51: return QObject::tr("5.1 SMPTE: L R C Lfe Ls Rs");
    case MonitorMix::ProTools71: return QObject::tr("7.1 Pro Tools: L C R Lss Rss Lsr Rsr Lfe");
    case MonitorMix::Ext71: return QObject::tr("7.1 EXT: L R C Lfe Lsr Rsr Lss Rss");
    case MonitorMix::SmpteDs71: return QObject::tr("7.1 SMPTE DS: L R C Lfe Lss Rss Lsr Rsr");
    }
    return QObject::tr("Stereo");
}

QString monitorMixShortName(MonitorMix mix) {
    switch (mix) {
    case MonitorMix::Stereo: return QObject::tr("Stereo");
    case MonitorMix::Mono: return QObject::tr("Mono");
    case MonitorMix::LrcLfe: return QObject::tr("LRCLfe");
    case MonitorMix::Lrcs: return QObject::tr("LRCS");
    case MonitorMix::Quad: return QObject::tr("Quad");
    case MonitorMix::ProTools51: return QObject::tr("5.1 PT");
    case MonitorMix::Smpte51: return QObject::tr("5.1 SMPTE");
    case MonitorMix::ProTools71: return QObject::tr("7.1 PT");
    case MonitorMix::Ext71: return QObject::tr("7.1 EXT");
    case MonitorMix::SmpteDs71: return QObject::tr("7.1 SMPTE DS");
    }
    return QObject::tr("Stereo");
}

QStringList monitorChannelNames(MonitorMix mix) {
    switch (mix) {
    case MonitorMix::Stereo: return {QStringLiteral("L"), QStringLiteral("R")};
    case MonitorMix::Mono: return {QStringLiteral("M")};
    case MonitorMix::LrcLfe: return QStringLiteral("L R C Lfe").split(QLatin1Char(' '));
    case MonitorMix::Lrcs: return QStringLiteral("L R C S").split(QLatin1Char(' '));
    case MonitorMix::Quad: return QStringLiteral("L R Ls Rs").split(QLatin1Char(' '));
    case MonitorMix::ProTools51: return QStringLiteral("L C R Ls Rs Lfe").split(QLatin1Char(' '));
    case MonitorMix::Smpte51: return QStringLiteral("L R C Lfe Ls Rs").split(QLatin1Char(' '));
    case MonitorMix::ProTools71: return QStringLiteral("L C R Lss Rss Lsr Rsr Lfe").split(QLatin1Char(' '));
    case MonitorMix::Ext71: return QStringLiteral("L R C Lfe Lsr Rsr Lss Rss").split(QLatin1Char(' '));
    case MonitorMix::SmpteDs71: return QStringLiteral("L R C Lfe Lss Rss Lsr Rsr").split(QLatin1Char(' '));
    }
    return {QStringLiteral("L"), QStringLiteral("R")};
}

int monitorChannels(MonitorMix mix) {
    return static_cast<int>(monitorChannelNames(mix).size());
}

std::vector<SpeakerGains> outputMatrix(model::AudioFormat sequenceFormat, MonitorMix monitor) {
    return through(monitorFold(monitor), sequenceFold(sequenceFormat));
}

std::vector<SpeakerGains> trackMatrix(model::AudioFormat sequenceFormat, model::AudioFormat format) {
    switch (format) {
    case model::AudioFormat::Mono: return outputMatrix(sequenceFormat, MonitorMix::Mono);
    case model::AudioFormat::Stereo: return outputMatrix(sequenceFormat, MonitorMix::Stereo);
    case model::AudioFormat::Surround51: return outputMatrix(sequenceFormat, MonitorMix::ProTools51);
    case model::AudioFormat::Surround71: return outputMatrix(sequenceFormat, MonitorMix::ProTools71);
    }
    return outputMatrix(sequenceFormat, MonitorMix::Stereo);
}

} // namespace lumenis::media
