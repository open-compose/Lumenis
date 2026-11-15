#include "lumenis/interop/aaf_essence.h"

#include "lumenis/interop/cfb.h"

#include <QFile>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <map>

// Embedded media in AAF (aaf_essence.h; docs/spec/aaf.md, "Embedded media"). The EssenceData objects of the
// ContentStorage name their file source mob by MobID and keep its media in their Data stream; the mob's descriptor
// says what the bytes are. The layout is the AAF Object Specification's, as pyaaf2 (essence.py, mobs.py) reads and
// writes it: PCM as interleaved samples, WAVE and AIFC as whole files, DNx as the frames one after another.
namespace lumenis::interop::aaf {

namespace {

// The SMPTE labels of the DNx compression IDs (SMPTE RP 224; the same table pyaaf2's video.py has): DNxHD's
// 04010202-71xx-0000-060e-2b340401010a and DNxHR's ...-2b340401010d.
struct DnxLabel {
    int cid;
    const char* label;
};
constexpr DnxLabel kDnxLabels[] = {
    {1235, "04010202-7101-0000-060e-2b340401010a"}, {1236, "04010202-7102-0000-060e-2b340401010a"},
    {1237, "04010202-7103-0000-060e-2b340401010a"}, {1238, "04010202-7104-0000-060e-2b340401010a"},
    {1241, "04010202-7107-0000-060e-2b340401010a"}, {1242, "04010202-7108-0000-060e-2b340401010a"},
    {1243, "04010202-7109-0000-060e-2b340401010a"}, {1244, "04010202-710a-0000-060e-2b340401010a"},
    {1250, "04010202-7110-0000-060e-2b340401010a"}, {1251, "04010202-7111-0000-060e-2b340401010a"},
    {1252, "04010202-7112-0000-060e-2b340401010a"}, {1253, "04010202-7113-0000-060e-2b340401010a"},
    {1256, "04010202-7116-0000-060e-2b340401010a"}, {1257, "04010202-7117-0000-060e-2b340401010a"},
    {1258, "04010202-7118-0000-060e-2b340401010a"}, {1259, "04010202-7119-0000-060e-2b340401010a"},
    {1260, "04010202-711a-0000-060e-2b340401010a"}, {1270, "04010202-7124-0000-060e-2b340401010d"},
    {1271, "04010202-7125-0000-060e-2b340401010d"}, {1272, "04010202-7126-0000-060e-2b340401010d"},
    {1273, "04010202-7127-0000-060e-2b340401010d"}, {1274, "04010202-7128-0000-060e-2b340401010d"},
};

// A Rational record as a Rational; `fallback` when missing or not positive.
Rational rational(const QVariant& v, const Rational& fallback) {
    const QVariantMap m = v.toMap();
    const qint64 num = m.value(QStringLiteral("Numerator")).toLongLong();
    const qint64 den = m.value(QStringLiteral("Denominator")).toLongLong();
    if (num <= 0 || den <= 0) return fallback;
    return Rational(num, den);
}

// A DataValue (an array of bytes) as File::decode gives it: bytes, or a list of numbers.
QByteArray bytesOf(const QVariant& v) {
    if (v.typeId() == QMetaType::QByteArray) return v.toByteArray();
    QByteArray out;
    for (const QVariant& b : v.toList()) out.append(static_cast<char>(b.toInt()));
    return out;
}

void put16(QByteArray& b, quint16 v) {
    char d[2];
    qToLittleEndian<quint16>(v, d);
    b.append(d, 2);
}
void put32(QByteArray& b, quint32 v) {
    char d[4];
    qToLittleEndian<quint32>(v, d);
    b.append(d, 4);
}

// A canonical WAVE header for `bytes` of PCM: RIFF, the fmt chunk (format 1), then the data chunk's header.
QByteArray waveHeader(int channels, int sampleRate, int bits, std::uint64_t bytes) {
    const int block = channels * ((bits + 7) / 8);
    const auto size = static_cast<quint32>(std::min<std::uint64_t>(bytes, 0xFFFFFFFFu - 36));
    QByteArray h("RIFF");
    put32(h, 36 + size);
    h += "WAVEfmt ";
    put32(h, 16);
    put16(h, 1);
    put16(h, static_cast<quint16>(channels));
    put32(h, static_cast<quint32>(sampleRate));
    put32(h, static_cast<quint32>(sampleRate * block));
    put16(h, static_cast<quint16>(block));
    put16(h, static_cast<quint16>(bits));
    h += "data";
    put32(h, size);
    return h;
}

} // namespace

// The fields at their places in the frame header (SMPTE ST 2019-1 section 5): the header prefix in the first five
// bytes (DNxHD's 00 00 02 80 01; DNxHR's 00 00 0x xx 03 with the header size in bytes 2-3), the scan in byte 5,
// the lines (of a field, when interlaced) and samples per line at 24 and 26, the bit depth in byte 33's top three bits, the compression ID at 40.
std::optional<DnxHeader> readDnxHeader(const QByteArray& frame) {
    if (frame.size() < 640) return std::nullopt;
    const auto* d = reinterpret_cast<const uchar*>(frame.constData());
    const bool hd = d[0] == 0 && d[1] == 0 && d[2] == 0x02 && d[3] == 0x80 && d[4] == 0x01;
    const int offset = (d[2] << 8) | d[3];
    const bool hr = d[0] == 0 && d[1] == 0 && d[4] == 0x03 && offset >= 0x0280 && offset <= 0x2170 && (offset & 3) == 0;
    if (!hd && !hr) return std::nullopt;
    DnxHeader h;
    h.interlaced = (d[5] & 2) != 0;
    h.height = qFromBigEndian<quint16>(d + 24);
    h.width = qFromBigEndian<quint16>(d + 26);
    const int depth = d[33] >> 5;
    h.bitDepth = depth == 2 ? 10 : depth == 3 ? 12 : 8;
    h.cid = static_cast<int>(qFromBigEndian<quint32>(d + 40));
    // An interlaced frame's header counts the lines of one field (540 for 1080i).
    if (h.interlaced) h.height *= 2;
    if (h.width <= 0 || h.height <= 0) return std::nullopt;
    return h;
}

QUuid dnxCompression(int cid) {
    for (const DnxLabel& l : kDnxLabels) {
        if (l.cid == cid) return auidFromText(l.label);
    }
    return {};
}

QList<Essence> embeddedEssence(const File& file, QStringList* warnings) {
    QList<Essence> out;
    std::map<MobId, Object> mobs;
    for (const Object& m : file.mobs()) mobs.emplace(m.mobId(QStringLiteral("MobID")), m);
    for (const Object& data : file.content().objects(QStringLiteral("EssenceData"))) {
        Essence e;
        e.mob = data.mobId(QStringLiteral("MobID"));
        const auto raw = data.raw(QStringLiteral("Data"));
        e.stream = raw ? data.streamEntry(*raw) : -1;
        const auto found = mobs.find(e.mob);
        const Object mob = found == mobs.end() ? Object() : found->second;
        if (e.stream < 0 || !mob) {
            if (warnings) *warnings << QStringLiteral("embedded media with no stream or no mob left out");
            continue;
        }
        e.bytes = file.container().entry(e.stream).size;
        const Object d = mob.object(QStringLiteral("EssenceDescription"));
        e.descriptor = d.className();
        e.rate = rational(d.value(QStringLiteral("SampleRate")), Rational(25));
        e.length = std::max<qint64>(0, d.integer(QStringLiteral("Length")));
        if (d.isA(QStringLiteral("WAVEDescriptor")) || d.isA(QStringLiteral("AIFCDescriptor"))) {
            e.kind = d.isA(QStringLiteral("WAVEDescriptor")) ? Essence::Kind::Wave : Essence::Kind::Aifc;
            e.summary = bytesOf(d.value(QStringLiteral("Summary")));
        } else if (d.isA(QStringLiteral("SoundDescriptor"))) {
            // PCM (and the older SoundDescriptor alone): the samples as the descriptor says.
            e.kind = Essence::Kind::Pcm;
            e.channels = static_cast<int>(d.integer(QStringLiteral("Channels"), 1));
            e.sampleRate = static_cast<int>(rational(d.value(QStringLiteral("AudioSamplingRate")), e.rate).toDouble() + 0.5);
            e.bits = static_cast<int>(d.integer(QStringLiteral("QuantizationBits"), 16));
            if (e.channels <= 0 || e.sampleRate <= 0 || (e.bits != 8 && e.bits != 16 && e.bits != 24 && e.bits != 32)) {
                e.kind = Essence::Kind::Unsupported;
            }
        } else if (d.isA(QStringLiteral("CDCIDescriptor"))) {
            // Avid DNx is told by its frames' headers, whatever the Compression label says.
            const QByteArray first = file.container().read(e.stream, 0, 640);
            if (const auto h = readDnxHeader(first)) {
                e.kind = Essence::Kind::Dnx;
                e.dnx = *h;
            }
        }
        out << e;
    }
    return out;
}

QString essenceSuffix(const Essence& essence) {
    switch (essence.kind) {
    case Essence::Kind::Pcm:
    case Essence::Kind::Wave: return QStringLiteral("wav");
    case Essence::Kind::Aifc: return QStringLiteral("aif");
    case Essence::Kind::Dnx: return QStringLiteral("dnxhd");
    case Essence::Kind::Unsupported: break;
    }
    return {};
}

bool extractEssence(const File& file, const Essence& essence, const QString& path, QString* error) {
    auto failed = [&](const QString& text) {
        if (error) *error = text;
        return false;
    };
    if (essence.kind == Essence::Kind::Unsupported) return failed(QStringLiteral("%1 media cannot be read").arg(essence.descriptor));
    cfb::Reader& c = file.container();
    if (essence.stream < 0) return failed(QStringLiteral("the media has no stream"));
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) return failed(QStringLiteral("%1: %2").arg(path, out.errorString()));
    // What comes before the data: PCM's WAVE header; a WAVE or AIFC file's own header from the Summary when the
    // data does not start with one (the AAF Object Specification keeps the header there, and some writers leave it
    // out of the data).
    const QByteArray start = c.read(essence.stream, 0, 4);
    QByteArray head;
    if (essence.kind == Essence::Kind::Pcm) {
        head = waveHeader(essence.channels, essence.sampleRate, essence.bits, essence.bytes);
    } else if (essence.kind == Essence::Kind::Wave && start != "RIFF") {
        head = essence.summary;
    } else if (essence.kind == Essence::Kind::Aifc && start != "FORM") {
        head = essence.summary;
    }
    if (!head.isEmpty() && out.write(head) != head.size()) return failed(QStringLiteral("%1: %2").arg(path, out.errorString()));
    constexpr std::uint64_t kChunk = 1 << 20;
    for (std::uint64_t at = 0; at < essence.bytes; at += kChunk) {
        QString why;
        const QByteArray piece = c.read(essence.stream, at, std::min(kChunk, essence.bytes - at), &why);
        if (!why.isEmpty() || piece.isEmpty()) return failed(why.isEmpty() ? QStringLiteral("the media's stream ends early") : why);
        if (out.write(piece) != piece.size()) return failed(QStringLiteral("%1: %2").arg(path, out.errorString()));
    }
    if (!out.commit()) return failed(QStringLiteral("%1: %2").arg(path, out.errorString()));
    return true;
}

} // namespace lumenis::interop::aaf
