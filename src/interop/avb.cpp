// Media Composer bin files (.avb): the container and the object classes
// (docs/spec/avb.md). Each class's read() is ported from pyavb (MIT, Mark
// Reid; credited in THIRD-PARTY-NOTICES.md), keeping its property names and
// the order it sets them in, so that `cutthulhu --dump-avb` and
// scripts/avb-dump.py print the same dump. pyavb's assertions are kept: here
// they make the one object unreadable (Object::error) instead of stopping.
//
// Every payload is read through a bounds-checked cursor (Reader); a read past
// the end of the payload, a wrong tag or a failed check throws ReadError,
// which File::object() catches. The dump is at the end.

#include "lumenis/interop/avb.h"

#include "lumenis/interop/aaf.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QTimeZone>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace lumenis::interop::avb {

namespace {

// Mac OS Roman, bytes 0x80-0xFF (as Python's mac_roman codec maps them).
constexpr std::array<char16_t, 128> kMacRoman{
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

const Value kNullValue;
const ValueList kEmptyList;
const Properties kEmptyProperties;

// A failed read: what was expected, and where.
struct ReadError {
    QString what;
};

// Sets a property as Python sets an attribute: a name already set keeps its
// place and takes the new value; a new one goes at the end.
void set(Properties& p, const QString& name, Value v) {
    for (Property& e : p) {
        if (e.name == name) {
            e.value = std::move(v);
            return;
        }
    }
    p.push_back({name, std::move(v)});
}

// A bounds-checked cursor over one payload, in the file's byte order
// (pyavb's AVBIOContext). Every read checks that its bytes are there.
class Reader {
public:
    Reader(const char* data, qsizetype size, bool little, std::uint32_t objectCount)
        : p_(data), size_(size), little_(little), count_(objectCount) {}

    qsizetype pos() const { return pos_; }
    qsizetype left() const { return size_ - pos_; }

    [[noreturn]] void fail(const QString& what) const {
        throw ReadError{QStringLiteral("%1 at byte %2").arg(what).arg(pos_)};
    }

    const char* take(qsizetype n) {
        if (n < 0 || n > left()) fail(QStringLiteral("%1 bytes wanted, %2 left").arg(n).arg(left()));
        const char* at = p_ + pos_;
        pos_ += n;
        return at;
    }
    QByteArray bytes(qsizetype n) {
        const char* at = take(n);
        return QByteArray(at, n);
    }

    std::uint8_t u8() { return static_cast<std::uint8_t>(*take(1)); }
    qint64 s8() { return static_cast<std::int8_t>(u8()); }
    bool boolean() { return u8() == 0x01; }
    std::uint16_t u16() {
        const char* at = take(2);
        return little_ ? qFromLittleEndian<quint16>(at) : qFromBigEndian<quint16>(at);
    }
    qint64 s16() { return static_cast<std::int16_t>(u16()); }
    std::uint32_t u32() {
        const char* at = take(4);
        return little_ ? qFromLittleEndian<quint32>(at) : qFromBigEndian<quint32>(at);
    }
    qint64 s32() { return static_cast<std::int32_t>(u32()); }
    std::uint64_t u64() {
        const char* at = take(8);
        return little_ ? qFromLittleEndian<quint64>(at) : qFromBigEndian<quint64>(at);
    }
    qint64 s64() { return static_cast<qint64>(u64()); }
    // pyavb reads doubles little-endian whatever the file's order.
    double f64() {
        const std::uint64_t bits = qFromLittleEndian<quint64>(take(8));
        double d = 0;
        std::memcpy(&d, &bits, sizeof d);
        return d;
    }
    // A fourcc: stored reversed in little-endian files.
    QByteArray fourcc() {
        QByteArray b = bytes(4);
        if (little_) std::reverse(b.begin(), b.end());
        return b;
    }

    // A one-byte tag pyavb asserts (read_assert_tag).
    void tag(std::uint8_t expected) {
        const qsizetype at = pos_;
        const std::uint8_t got = u8();
        if (got != expected) {
            throw ReadError{QStringLiteral("expected tag 0x%1, found 0x%2 at byte %3")
                                .arg(expected, 2, 16, QLatin1Char('0'))
                                .arg(got, 2, 16, QLatin1Char('0'))
                                .arg(at)};
        }
    }
    void check(bool ok, const char* what) const {
        if (!ok) fail(QString::fromLatin1(what));
    }

    // Extensions (iter_ext): while the next byte is 0x01, the byte after it
    // is an extension's tag.
    bool ext(std::uint8_t& tag) {
        if (left() < 1) fail(QStringLiteral("end of data where an extension or the end tag belongs"));
        if (static_cast<std::uint8_t>(p_[pos_]) != 0x01) return false;
        ++pos_;
        tag = u8();
        return true;
    }
    [[noreturn]] void unknownExt(const QString& cls, std::uint8_t tag) const {
        fail(QStringLiteral("%1: unknown ext tag 0x%2").arg(cls).arg(tag, 2, 16, QLatin1Char('0')));
    }

    // A string: u16 size (0xFFFF: none), then the bytes; NULs at either end
    // are dropped (pyavb strips them). Mac OS Roman or strict UTF-8.
    QString string(bool utf8 = false) {
        const std::uint16_t size = u16();
        if (size >= 0xFFFF) return {};
        const QByteArray raw = strip(bytes(size));
        if (!utf8) return fromMacRoman(raw);
        const auto s = fromUtf8Strict(raw);
        if (!s) fail(QStringLiteral("invalid UTF-8"));
        return *s;
    }
    static QByteArray strip(QByteArray b) {
        qsizetype from = 0;
        qsizetype to = b.size();
        while (from < to && b[from] == '\0') ++from;
        while (to > from && b[to - 1] == '\0') --to;
        return b.mid(from, to - from);
    }

    // A reference: u32 index; past the table it is an error, as in pyavb.
    Value ref() {
        const std::uint32_t index = u32();
        if (index > count_) fail(QStringLiteral("bad index: %1").arg(index));
        return Value::ref(index);
    }

    // UUIDs: Data1-3 as integers in the file's order, then Data4 as bytes.
    QUuid rawUuid() {
        const std::uint32_t d1 = u32();
        const std::uint16_t d2 = u16();
        const std::uint16_t d3 = u16();
        const char* d4 = take(8);
        return uuidOf(d1, d2, d3, d4);
    }
    // The tagged form (read_uuid): 72 Data1, 70 Data2, 70 Data3, 65 s32(8) Data4.
    QUuid taggedUuid() {
        tag(72);
        const std::uint32_t d1 = u32();
        tag(70);
        const std::uint16_t d2 = u16();
        tag(70);
        const std::uint16_t d3 = u16();
        tag(65);
        check(s32() == 8, "UUID Data4 length is not 8");
        return uuidOf(d1, d2, d3, take(8));
    }
    static QUuid uuidOf(std::uint32_t d1, std::uint16_t d2, std::uint16_t d3, const char* d4) {
        const auto* b = reinterpret_cast<const uchar*>(d4);
        return QUuid(d1, d2, d3, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    }

    // A MobID (read_mob_id): 65 s32(12) the 12-byte label, 68 length,
    // 68/68/68 the instance number, then the material number as a tagged UUID.
    MobId mobId() {
        tag(65);
        check(s32() == 12, "MobID label length is not 12");
        QByteArray aafForm = bytes(12); // AAF's 32-byte form: aaf::mobIdFromBytes
        for (int i = 0; i < 4; ++i) {
            tag(68);
            aafForm.append(static_cast<char>(u8()));
        }
        const QUuid m = taggedUuid();
        char le[8];
        qToLittleEndian<quint32>(m.data1, le);
        qToLittleEndian<quint16>(m.data2, le + 4);
        qToLittleEndian<quint16>(m.data3, le + 6);
        aafForm.append(le, 8);
        aafForm.append(reinterpret_cast<const char*>(m.data4), 8);
        return aaf::mobIdFromBytes(aafForm.constData());
    }

    // A number stored as a 32-bit mantissa and a 16-bit power of ten
    // (read_exp10_encoded_float): float(mantissa) * pow(10, exp), computed as
    // Python computes it, so that the doubles are the same to the bit.
    double exp10() {
        const qint64 mantissa = s32();
        const qint64 e = s16();
        double scale = 0;
        if (e >= 0) {
            // Python: an exact integer 10**e, rounded once to a double.
            if (e > 308) fail(QStringLiteral("exponent %1 overflows").arg(e));
            scale = QByteArray("1e").append(QByteArray::number(e)).toDouble();
        } else {
            scale = std::pow(10.0, static_cast<double>(e)); // Python: float pow
        }
        return static_cast<double>(mantissa) * scale;
    }

private:
    const char* p_;
    qsizetype size_;
    qsizetype pos_ = 0;
    bool little_;
    std::uint32_t count_;
};

// ---- The classes ------------------------------------------------------------
//
// Each read function appends to `p` what pyavb's read() sets, in its order.
// Base classes are read first, as pyavb's super().read() calls go; a class
// reads its end tag 0x03 only when it is the object's own class.

using Props = Properties;
const QString kName = QStringLiteral("name");

Value pair(qint64 a, qint64 b) { return Value::list({Value::integer(a), Value::integer(b)}); }

// Component (COMP, abstract): 02 03, two "bob" refs, the media kind, the edit
// rate, name and effect ID (empty: None), attributes, session attributes, the
// precompute; ext 01: 72 the parameter list.
void readComponent(Reader& r, Props& p, const QString& cls) {
    r.tag(0x02);
    r.tag(0x03);
    set(p, QStringLiteral("left_bob"), r.ref());
    set(p, QStringLiteral("right_bob"), r.ref());
    set(p, QStringLiteral("media_kind_id"), Value::integer(r.s16()));
    set(p, QStringLiteral("edit_rate"), Value::real(r.exp10()));
    const QString name = r.string();
    set(p, kName, name.isEmpty() ? Value() : Value::string(name));
    const QString effect = r.string();
    set(p, QStringLiteral("effect_id"), effect.isEmpty() ? Value() : Value::string(effect));
    set(p, QStringLiteral("attributes"), r.ref());
    set(p, QStringLiteral("session_attrs"), r.ref());
    set(p, QStringLiteral("precomputed"), r.ref());
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t != 0x01) r.unknownExt(cls, t);
        r.tag(72);
        set(p, QStringLiteral("param_list"), r.ref());
    }
}

// Clip (CLIP, abstract): 02 01, u32 length.
void readClip(Reader& r, Props& p, const QString& cls) {
    readComponent(r, p, cls);
    r.tag(0x02);
    r.tag(0x01);
    set(p, QStringLiteral("length"), Value::integer(r.u32()));
}

// SourceClip (SCLP): 02 03, the old 8-byte MobID (unused), track, start;
// ext 01: the MobID.
void readSourceClip(Reader& r, Props& p, const QString& cls) {
    readClip(r, p, cls);
    r.tag(0x02);
    r.tag(0x03);
    r.u32();
    r.u32();
    set(p, QStringLiteral("track_id"), Value::integer(r.s16()));
    set(p, QStringLiteral("start_time"), Value::integer(r.s32()));
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t != 0x01) r.unknownExt(cls, t);
        set(p, QStringLiteral("mob_id"), Value::mobId(r.mobId()));
    }
    r.tag(0x03);
}

// Timecode (TCCP): 02 01, u32 flags, u16 fps, 6 unused bytes, u32 start.
void readTimecode(Reader& r, Props& p, const QString& cls) {
    readClip(r, p, cls);
    r.tag(0x02);
    r.tag(0x01);
    set(p, QStringLiteral("flags"), Value::integer(r.u32()));
    set(p, QStringLiteral("fps"), Value::integer(r.u16()));
    r.take(6);
    set(p, QStringLiteral("start"), Value::integer(r.u32()));
    r.tag(0x03);
}

// Edgecode (ECCP): 02 01, an 8-byte header, film kind, code format, base
// perforation, 4 unused bytes, start.
void readEdgecode(Reader& r, Props& p, const QString& cls) {
    readClip(r, p, cls);
    r.tag(0x02);
    r.tag(0x01);
    set(p, QStringLiteral("header"), Value::bytes(r.bytes(8)));
    set(p, QStringLiteral("film_kind"), Value::integer(r.u8()));
    set(p, QStringLiteral("code_format"), Value::integer(r.u8()));
    set(p, QStringLiteral("base_perf"), Value::integer(r.u16()));
    r.u32();
    set(p, QStringLiteral("start_ec"), Value::integer(r.s32()));
    r.tag(0x03);
}

// TrackRef (TRKR): 02 01, relative scope and track.
void readTrackRef(Reader& r, Props& p, const QString& cls) {
    readClip(r, p, cls);
    r.tag(0x02);
    r.tag(0x01);
    set(p, QStringLiteral("relative_scope"), Value::integer(r.s16()));
    set(p, QStringLiteral("relative_track"), Value::integer(r.s16()));
    r.tag(0x03);
}

// Filler (FILL): 02 01 03.
void readFiller(Reader& r, Props& p, const QString& cls) {
    readClip(r, p, cls);
    r.tag(0x02);
    r.tag(0x01);
    r.tag(0x03);
}

// ParamClip (PRCL): 02 01, interpolation, value type (1 int, 2 double,
// 4 reference), the control points: offset (num, den), time scale, value,
// and point properties (code, type, an int or a double); ext 01: 71
// extrapolation; ext 02: 71 fields.
void readParamClip(Reader& r, Props& p, const QString& cls) {
    readClip(r, p, cls);
    r.tag(0x02);
    r.tag(0x01);
    set(p, QStringLiteral("interp_kind"), Value::integer(r.s32()));
    const qint64 valueType = r.s16();
    set(p, QStringLiteral("value_type"), Value::integer(valueType));
    r.check(valueType == 1 || valueType == 2 || valueType == 4, "unknown value type");
    const qint64 count = r.s32();
    r.check(count >= 0, "negative point count");
    ValueList points;
    for (qint64 i = 0; i < count; ++i) {
        Props cp;
        const qint64 num = r.s32();
        const qint64 den = r.s32();
        set(cp, QStringLiteral("offset"), pair(num, den));
        set(cp, QStringLiteral("timescale"), Value::integer(r.s32()));
        if (valueType == 1) {
            set(cp, QStringLiteral("value"), Value::integer(r.s32()));
        } else if (valueType == 2) {
            set(cp, QStringLiteral("value"), Value::real(r.f64()));
        } else {
            set(cp, QStringLiteral("value"), r.ref());
        }
        const qint64 ppCount = r.s16();
        r.check(ppCount >= 0, "negative point property count");
        ValueList pps;
        for (qint64 j = 0; j < ppCount; ++j) {
            Props pp;
            set(pp, QStringLiteral("code"), Value::integer(r.s16()));
            const qint64 type = r.s16();
            set(pp, QStringLiteral("type"), Value::integer(type));
            if (type == 2) {
                set(pp, QStringLiteral("value"), Value::real(r.f64()));
            } else if (type == 1) {
                set(pp, QStringLiteral("value"), Value::integer(r.s32()));
            } else {
                r.fail(QStringLiteral("unknown PP type: %1").arg(type));
            }
            pps.push_back(Value::record(std::move(pp)));
        }
        set(cp, QStringLiteral("pp"), Value::list(std::move(pps)));
        points.push_back(Value::record(std::move(cp)));
    }
    set(p, QStringLiteral("control_points"), Value::list(std::move(points)));
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t == 0x01) {
            r.tag(71);
            set(p, QStringLiteral("extrap_kind"), Value::integer(r.s32()));
        } else if (t == 0x02) {
            r.tag(71);
            set(p, QStringLiteral("fields"), Value::integer(r.s32()));
        } else {
            r.unknownExt(cls, t);
        }
    }
    r.tag(0x03);
}

// ControlClip (CTRL): 02 03, interpolation, the control points: offset,
// time scale, a "has value" flag (always 1), the value (num, den), and point
// properties (code, num, den).
void readControlClip(Reader& r, Props& p, const QString& cls) {
    readClip(r, p, cls);
    r.tag(0x02);
    r.tag(0x03);
    set(p, QStringLiteral("interp_kind"), Value::integer(r.s32()));
    const qint64 count = r.s32();
    ValueList points;
    for (qint64 i = 0; i < count; ++i) {
        Props cp;
        const qint64 a = r.s32();
        const qint64 b = r.s32();
        set(cp, QStringLiteral("offset"), pair(a, b));
        set(cp, QStringLiteral("time_scale"), Value::integer(r.s32()));
        r.check(r.boolean(), "control point without a value");
        const qint64 va = r.s32();
        const qint64 vb = r.s32();
        set(cp, QStringLiteral("value"), pair(va, vb));
        const qint64 ppCount = r.s16();
        r.check(ppCount >= 0, "negative point property count");
        ValueList pps;
        for (qint64 j = 0; j < ppCount; ++j) {
            Props pp;
            set(pp, QStringLiteral("code"), Value::integer(r.s16()));
            const qint64 pa = r.s32();
            const qint64 pb = r.s32();
            set(pp, QStringLiteral("value"), pair(pa, pb));
            pps.push_back(Value::record(std::move(pp)));
        }
        set(cp, QStringLiteral("pp"), Value::list(std::move(pps)));
        points.push_back(Value::record(std::move(cp)));
    }
    set(p, QStringLiteral("control_points"), Value::list(std::move(points)));
    r.tag(0x03);
}

// Sequence (SEQU): 02 03, u32 count, the components' refs.
void readSequence(Reader& r, Props& p, const QString& cls) {
    readComponent(r, p, cls);
    r.tag(0x02);
    r.tag(0x03);
    const std::uint32_t count = r.u32();
    ValueList components;
    for (std::uint32_t i = 0; i < count; ++i) components.push_back(r.ref());
    set(p, QStringLiteral("components"), Value::list(std::move(components)));
    r.tag(0x03);
}

// TrackGroup (TRKG): 02 08, multicam mode, length, scalars, the tracks. Each
// track has u16 flags saying which fields follow (bit 0 label, 1 attributes,
// 9 session attributes, 2 component, 3 filler proxy, 4 bob, 5 control code,
// 6 sub code, 7 start, 8 read only; 0xFC00 unknown); ext 01: a 69 lock
// number for every track. No end tag: subclasses go on.
void readTrackGroup(Reader& r, Props& p, const QString& cls) {
    readComponent(r, p, cls);
    r.tag(0x02);
    r.tag(0x08);
    set(p, QStringLiteral("mc_mode"), Value::integer(r.u8()));
    set(p, QStringLiteral("length"), Value::integer(r.s32()));
    set(p, QStringLiteral("num_scalars"), Value::integer(r.s32()));
    const qint64 count = r.s32();
    std::vector<Props> tracks;
    for (qint64 i = 0; i < count; ++i) {
        Props t;
        const std::uint16_t flags = r.u16();
        if (flags & (1u << 0)) set(t, QStringLiteral("index"), Value::integer(r.s16()));
        if (flags & (1u << 1)) set(t, QStringLiteral("attributes"), r.ref());
        if (flags & (1u << 9)) set(t, QStringLiteral("session_attr"), r.ref());
        if (flags & (1u << 2)) set(t, QStringLiteral("component"), r.ref());
        if (flags & (1u << 3)) set(t, QStringLiteral("filler_proxy"), r.ref());
        if (flags & (1u << 4)) set(t, QStringLiteral("bob_data"), r.ref());
        if (flags & (1u << 5)) set(t, QStringLiteral("control_code"), Value::integer(r.s16()));
        if (flags & (1u << 6)) set(t, QStringLiteral("control_sub_code"), Value::integer(r.s16()));
        if (flags & (1u << 7)) set(t, QStringLiteral("start_pos"), Value::integer(r.s32()));
        if (flags & (1u << 8)) set(t, QStringLiteral("read_only"), Value::boolean(r.boolean()));
        if (flags & 0xFC00u) r.fail(QStringLiteral("Unknown Track Flag: %1").arg(flags));
        tracks.push_back(std::move(t));
    }
    std::uint8_t tag = 0;
    while (r.ext(tag)) {
        if (tag != 0x01) r.unknownExt(cls, tag);
        for (Props& t : tracks) {
            r.tag(69);
            set(t, QStringLiteral("lock_number"), Value::integer(r.s16()));
        }
    }
    ValueList list;
    for (Props& t : tracks) list.push_back(Value::record(std::move(t)));
    set(p, QStringLiteral("tracks"), Value::list(std::move(list)));
}

// The global info effects share (TKFX after its 02 06, TNFX after 02 05).
void readEffectInfo(Reader& r, Props& p) {
    set(p, QStringLiteral("left_length"), Value::integer(r.s32()));
    set(p, QStringLiteral("right_length"), Value::integer(r.s32()));
    set(p, QStringLiteral("info_version"), Value::integer(r.s16()));
    set(p, QStringLiteral("info_current"), Value::integer(r.s32()));
    set(p, QStringLiteral("info_smooth"), Value::integer(r.s32()));
    set(p, QStringLiteral("info_color_item"), Value::integer(r.s16()));
    set(p, QStringLiteral("info_quality"), Value::integer(r.s16()));
    set(p, QStringLiteral("info_is_reversed"), Value::integer(r.s8()));
    set(p, QStringLiteral("info_aspect_on"), Value::boolean(r.boolean()));
    set(p, QStringLiteral("keyframes"), r.ref());
    set(p, QStringLiteral("info_force_software"), Value::boolean(r.boolean()));
    set(p, QStringLiteral("info_never_hardware"), Value::boolean(r.boolean()));
}

// TrackEffect (TKFX): 02 06, the effect info; ext 02: 72 the tracker manager.
void readTrackEffect(Reader& r, Props& p, const QString& cls) {
    readTrackGroup(r, p, cls);
    r.tag(0x02);
    r.tag(0x06);
    readEffectInfo(r, p);
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t != 0x02) r.unknownExt(cls, t);
        r.tag(72);
        set(p, QStringLiteral("trackman"), r.ref());
    }
    if (cls == QLatin1String("TKFX")) r.tag(0x03);
}

// PanVolumeEffect (PVOL): 02 05, level, pan, three flags; ext 01/02: 71
// separate clip gain support, trim gain effect.
void readPanVolume(Reader& r, Props& p, const QString& cls) {
    readTrackEffect(r, p, cls);
    r.tag(0x02);
    r.tag(0x05);
    set(p, QStringLiteral("level"), Value::integer(r.s32()));
    set(p, QStringLiteral("pan"), Value::integer(r.s32()));
    set(p, QStringLiteral("suppress_validation"), Value::boolean(r.boolean()));
    set(p, QStringLiteral("level_set"), Value::boolean(r.boolean()));
    set(p, QStringLiteral("pan_set"), Value::boolean(r.boolean()));
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t == 0x01) {
            r.tag(71);
            set(p, QStringLiteral("supports_seperate_gain"), Value::integer(r.s32()));
        } else if (t == 0x02) {
            r.tag(71);
            set(p, QStringLiteral("is_trim_gain_effect"), Value::integer(r.s32()));
        } else {
            r.unknownExt(cls, t);
        }
    }
    r.tag(0x03);
}

// EssenceGroup (RSET): 02 01; ext 01: 71 the set's type.
void readEssenceGroup(Reader& r, Props& p, const QString& cls) {
    readTrackGroup(r, p, cls);
    r.tag(0x02);
    r.tag(0x01);
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t != 0x01) r.unknownExt(cls, t);
        r.tag(71);
        set(p, QStringLiteral("rep_set_type"), Value::integer(r.s32()));
    }
    r.tag(0x03);
}

// TransitionEffect (TNFX): 02 01 cut point, 02 05 the effect info; ext 01:
// 72 the tracker manager.
void readTransition(Reader& r, Props& p, const QString& cls) {
    readTrackGroup(r, p, cls);
    r.tag(0x02);
    r.tag(0x01);
    set(p, QStringLiteral("cutpoint"), Value::integer(r.s32()));
    r.tag(0x02);
    r.tag(0x05);
    readEffectInfo(r, p);
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t != 0x01) r.unknownExt(cls, t);
        r.tag(72);
        set(p, QStringLiteral("trackman"), r.ref());
    }
    r.tag(0x03);
}

// TimeWarp (WARP, abstract): 02 02, the phase offset.
void readTimeWarp(Reader& r, Props& p, const QString& cls) {
    readTrackGroup(r, p, cls);
    r.tag(0x02);
    r.tag(0x02);
    set(p, QStringLiteral("phase_offset"), Value::integer(r.s32()));
}

// CaptureMask (MASK): 02 01, is double, u32 mask bits.
void readCaptureMask(Reader& r, Props& p, const QString& cls) {
    readTimeWarp(r, p, cls);
    r.tag(0x02);
    r.tag(0x01);
    set(p, QStringLiteral("is_double"), Value::boolean(r.boolean()));
    set(p, QStringLiteral("mask_bits"), Value::integer(r.u32()));
    r.tag(0x03);
}

// MotionEffect (SPED): 02 03, the rate (num, den); ext 01: 75 a double
// offset adjust; 02: 72 the source parameter list; 03: 66 new calculation.
void readMotionEffect(Reader& r, Props& p, const QString& cls) {
    readTimeWarp(r, p, cls);
    r.tag(0x02);
    r.tag(0x03);
    const qint64 num = r.s32();
    const qint64 den = r.s32();
    set(p, QStringLiteral("speed_ratio"), pair(num, den));
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t == 0x01) {
            r.tag(75);
            set(p, QStringLiteral("offset_adjust"), Value::real(r.f64()));
        } else if (t == 0x02) {
            r.tag(72);
            set(p, QStringLiteral("source_param_list"), r.ref());
        } else if (t == 0x03) {
            r.tag(66);
            set(p, QStringLiteral("new_source_calculation"), Value::boolean(r.boolean()));
        } else {
            r.unknownExt(cls, t);
        }
    }
    r.tag(0x03);
}

// Composition (CMPO), a mob: 02 02, the old 8-byte MobID (unused), last
// modified, mob type (1 composition, 2 master, 3 source), usage code, the
// descriptor; ext 01: 71 creation time; 02: the MobID.
void readComposition(Reader& r, Props& p, const QString& cls) {
    readTrackGroup(r, p, cls);
    r.tag(0x02);
    r.tag(0x02);
    r.u32();
    r.u32();
    set(p, QStringLiteral("last_modified"), Value::time(r.u32()));
    set(p, QStringLiteral("mob_type_id"), Value::integer(r.u8()));
    set(p, QStringLiteral("usage_code"), Value::integer(r.s32()));
    set(p, QStringLiteral("descriptor"), r.ref());
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t == 0x01) {
            r.tag(71);
            set(p, QStringLiteral("creation_time"), Value::time(r.u32()));
        } else if (t == 0x02) {
            set(p, QStringLiteral("mob_id"), Value::mobId(r.mobId()));
        } else {
            r.unknownExt(cls, t);
        }
    }
    r.tag(0x03);
}

// MediaDescriptor (MDES): 02 03, mob kind, the locator, intermediate, the
// physical media; ext 01: 65 s32(16) a UUID; 02: 65 s32 size, UTF-16 bytes;
// 03: 72 attributes.
void readMediaDescriptor(Reader& r, Props& p, const QString& cls) {
    r.tag(0x02);
    r.tag(0x03);
    set(p, QStringLiteral("mob_kind"), Value::integer(r.u8()));
    set(p, QStringLiteral("locator"), r.ref());
    set(p, QStringLiteral("intermediate"), Value::boolean(r.boolean()));
    set(p, QStringLiteral("physical_media"), r.ref());
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t == 0x01) {
            r.tag(65);
            r.check(r.s32() == 16, "UUID length is not 16");
            set(p, QStringLiteral("uuid"), Value::uuid(r.rawUuid()));
        } else if (t == 0x02) {
            r.tag(65);
            const qint64 size = r.s32();
            set(p, QStringLiteral("wchar"), Value::bytes(r.bytes(size)));
        } else if (t == 0x03) {
            r.tag(72);
            set(p, QStringLiteral("attributes"), r.ref());
        } else {
            r.unknownExt(cls, t);
        }
    }
    if (cls == QLatin1String("MDES")) r.tag(0x03);
}

// MediaFileDescriptor (MDFL): 02 03, edit rate, length, is OMFI, data offset.
void readMediaFileDescriptor(Reader& r, Props& p, const QString& cls) {
    readMediaDescriptor(r, p, cls);
    r.tag(0x02);
    r.tag(0x03);
    set(p, QStringLiteral("edit_rate"), Value::real(r.exp10()));
    set(p, QStringLiteral("length"), Value::integer(r.s32()));
    set(p, QStringLiteral("is_omfi"), Value::integer(r.s16()));
    set(p, QStringLiteral("data_offset"), Value::integer(r.s32()));
    if (cls == QLatin1String("MDFL")) r.tag(0x03);
}

// PCMADescriptor (PCMA): 02 01, the audio format; ext 01: 77 EBU time
// stamp; 03: 76 the timecode rate, a string.
void readPCMA(Reader& r, Props& p, const QString& cls) {
    readMediaFileDescriptor(r, p, cls);
    r.tag(0x02);
    r.tag(0x01);
    set(p, QStringLiteral("channels"), Value::integer(r.u16()));
    set(p, QStringLiteral("quantization_bits"), Value::integer(r.u16()));
    set(p, QStringLiteral("sample_rate"), Value::real(r.exp10()));
    set(p, QStringLiteral("locked"), Value::boolean(r.boolean()));
    set(p, QStringLiteral("audio_ref_level"), Value::integer(r.s16()));
    set(p, QStringLiteral("electro_spatial_formulation"), Value::integer(r.s32()));
    set(p, QStringLiteral("dial_norm"), Value::integer(r.u16()));
    set(p, QStringLiteral("coding_format"), Value::integer(r.u32()));
    set(p, QStringLiteral("block_align"), Value::integer(r.u32()));
    set(p, QStringLiteral("sequence_offset"), Value::integer(r.u16()));
    set(p, QStringLiteral("average_bps"), Value::integer(r.u32()));
    set(p, QStringLiteral("has_peak_envelope_data"), Value::boolean(r.boolean()));
    set(p, QStringLiteral("peak_envelope_version"), Value::integer(r.s32()));
    set(p, QStringLiteral("peak_envelope_format"), Value::integer(r.s32()));
    set(p, QStringLiteral("points_per_peak_value"), Value::integer(r.s32()));
    set(p, QStringLiteral("peak_envelope_block_size"), Value::integer(r.s32()));
    set(p, QStringLiteral("peak_channel_count"), Value::integer(r.s32()));
    set(p, QStringLiteral("peak_frame_count"), Value::integer(r.s32()));
    set(p, QStringLiteral("peak_of_peaks_offset"), Value::uint64(r.u64()));
    set(p, QStringLiteral("peak_envelope_timestamp"), Value::integer(r.s32()));
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t == 0x01) {
            r.tag(77);
            set(p, QStringLiteral("ebu_timestamp"), Value::integer(r.s64()));
        } else if (t == 0x03) {
            r.tag(76);
            set(p, QStringLiteral("timecode_framerate"), Value::string(r.string()));
        } else {
            r.unknownExt(cls, t);
        }
    }
    r.tag(0x03);
}

// Four (x, y) points, each a 71 x and a 71 y (DIDD's boxes).
Value box(Reader& r) {
    ValueList points;
    for (int i = 0; i < 4; ++i) {
        r.tag(71);
        const qint64 x = r.s32();
        r.tag(71);
        const qint64 y = r.s32();
        points.push_back(pair(x, y));
    }
    return Value::list(std::move(points));
}

// DIDDescriptor (DIDD): 02 02, the picture geometry, aspect ratio, line map,
// alpha, compression fourcc, resolution ID; many extensions (docs/spec/avb.md).
void readDIDD(Reader& r, Props& p, const QString& cls) {
    readMediaFileDescriptor(r, p, cls);
    r.tag(0x02);
    r.tag(0x02);
    for (const char* name : {"stored_height", "stored_width", "sampled_height", "sampled_width", "sampled_x_offset",
                             "sampled_y_offset", "display_height", "display_width", "display_x_offset",
                             "display_y_offset"}) {
        set(p, QString::fromLatin1(name), Value::integer(r.s32()));
    }
    set(p, QStringLiteral("frame_layout"), Value::integer(r.s16()));
    const qint64 num = r.s32();
    const qint64 den = r.s32();
    set(p, QStringLiteral("aspect_ratio"), pair(num, den));
    const qint64 lineMapBytes = r.s32();
    ValueList lineMap;
    for (qint64 i = 0; i < lineMapBytes / 4; ++i) lineMap.push_back(Value::integer(r.s32()));
    set(p, QStringLiteral("line_map"), Value::list(std::move(lineMap)));
    set(p, QStringLiteral("alpha_transparency"), Value::integer(r.s32()));
    set(p, QStringLiteral("uniformness"), Value::boolean(r.boolean()));
    set(p, QStringLiteral("did_image_size"), Value::integer(r.s32()));
    set(p, QStringLiteral("next_did_desc"), r.ref());
    set(p, QStringLiteral("compress_method"), Value::bytes(r.fourcc()));
    set(p, QStringLiteral("resolution_id"), Value::integer(r.s32()));
    set(p, QStringLiteral("image_alignment_factor"), Value::integer(r.s32()));
    std::uint8_t t = 0;
    auto int71 = [&](const char* name) {
        r.tag(71);
        set(p, QString::fromLatin1(name), Value::integer(r.s32()));
    };
    auto uuid80 = [&](const char* name) {
        r.tag(80);
        set(p, QString::fromLatin1(name), Value::uuid(r.rawUuid()));
    };
    while (r.ext(t)) {
        switch (t) {
        case 1:
            r.tag(69);
            set(p, QStringLiteral("frame_index_byte_order"), Value::integer(r.s16()));
            break;
        case 2: int71("frame_sample_size"); break;
        case 3: int71("first_frame_offset"); break;
        case 4:
            int71("client_fill_start");
            int71("client_fill_end");
            break;
        case 5: int71("offset_to_rle_frame_index"); break;
        case 6: int71("frame_start_offset"); break;
        case 8:
            set(p, QStringLiteral("valid_box"), box(r));
            set(p, QStringLiteral("essence_box"), box(r));
            set(p, QStringLiteral("source_box"), box(r));
            break;
        case 9:
            set(p, QStringLiteral("framing_box"), box(r));
            int71("reformatting_option");
            break;
        case 10: uuid80("transfer_characteristic"); break;
        case 11:
            uuid80("color_primaries");
            uuid80("coding_equations");
            break;
        case 12: uuid80("essence_compression"); break;
        case 14:
            r.tag(68);
            set(p, QStringLiteral("essence_element_size_kind"), Value::integer(r.u8()));
            break;
        case 15:
            r.tag(66);
            set(p, QStringLiteral("frame_checked_with_mapper"), Value::boolean(r.boolean()));
            break;
        default: r.unknownExt(cls, t);
        }
    }
    if (cls == QLatin1String("DIDD")) r.tag(0x03);
}

// CDCIDescriptor (CDCI): 02 02, subsampling, component width, siting,
// reference levels, frame index offset; ext 01/02: 72 alpha sampled width,
// ignore black and white levels.
void readCDCI(Reader& r, Props& p, const QString& cls) {
    readDIDD(r, p, cls);
    r.tag(0x02);
    r.tag(0x02);
    set(p, QStringLiteral("horizontal_subsampling"), Value::integer(r.u32()));
    set(p, QStringLiteral("vertical_subsampling"), Value::integer(r.u32()));
    set(p, QStringLiteral("component_width"), Value::integer(r.s32()));
    set(p, QStringLiteral("color_sitting"), Value::integer(r.s16()));
    set(p, QStringLiteral("black_ref_level"), Value::integer(r.u32()));
    set(p, QStringLiteral("white_ref_level"), Value::integer(r.u32()));
    set(p, QStringLiteral("color_range"), Value::integer(r.u32()));
    set(p, QStringLiteral("frame_index_offset"), Value::integer(r.s64()));
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t == 0x01) {
            r.tag(72);
            set(p, QStringLiteral("alpha_sampled_width"), Value::integer(r.u32()));
        } else if (t == 0x02) {
            r.tag(72);
            set(p, QStringLiteral("ignore_bw"), Value::integer(r.u32()));
        } else {
            r.unknownExt(cls, t);
        }
    }
    if (cls == QLatin1String("CDCI")) r.tag(0x03);
}

// RGBADescriptor (RGBA): 02 01, the pixel layout (codes, then sizes, as in
// AAF), an empty palette; ext 01: 77 frame index offset; 02: component
// reference levels; 03: alpha reference levels.
void readRGBA(Reader& r, Props& p, const QString& cls) {
    readDIDD(r, p, cls);
    r.tag(0x02);
    r.tag(0x01);
    const std::uint32_t layoutSize = r.u32();
    const QByteArray layout = r.bytes(layoutSize);
    const std::uint32_t structSize = r.u32();
    const QByteArray structure = r.bytes(structSize);
    r.check(layoutSize == structSize, "pixel layout and structure sizes differ");
    ValueList pixels;
    for (qsizetype i = 0; i < layout.size(); ++i) {
        Props e;
        set(e, QStringLiteral("Code"), Value::integer(static_cast<std::uint8_t>(layout[i])));
        set(e, QStringLiteral("Size"), Value::integer(static_cast<std::uint8_t>(structure[i])));
        pixels.push_back(Value::record(std::move(e)));
    }
    set(p, QStringLiteral("pixel_layout"), Value::list(std::move(pixels)));
    r.check(r.u32() == 0, "palette layout is not empty");
    r.check(r.u32() == 0, "palette structure is not empty");
    r.check(r.u32() == 0, "palette is not empty");
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t == 0x01) {
            r.tag(77);
            set(p, QStringLiteral("frame_index_offset"), Value::uint64(r.u64()));
        } else if (t == 0x02) {
            r.tag(66);
            set(p, QStringLiteral("has_comp_min_ref"), Value::boolean(r.boolean()));
            r.tag(72);
            set(p, QStringLiteral("comp_min_ref"), Value::integer(r.u32()));
            r.tag(66);
            set(p, QStringLiteral("has_comp_max_ref"), Value::boolean(r.boolean()));
            r.tag(72);
            set(p, QStringLiteral("comp_max_ref"), Value::integer(r.u32()));
        } else if (t == 0x03) {
            r.tag(72);
            set(p, QStringLiteral("alpha_min_ref"), Value::integer(r.u32()));
            r.tag(72);
            set(p, QStringLiteral("alpha_max_ref"), Value::integer(r.u32()));
        } else {
            r.unknownExt(cls, t);
        }
    }
    r.tag(0x03);
}

// FileLocator (FILE, WINF): 02 02, the path (Mac OS Roman); ext 01: 76 POSIX
// path; 02 and 03: 76 UTF-8 paths.
void readFileLocator(Reader& r, Props& p, const QString& cls) {
    r.tag(0x02);
    r.tag(0x02);
    set(p, QStringLiteral("path"), Value::string(r.string()));
    std::uint8_t t = 0;
    while (r.ext(t)) {
        r.tag(76);
        if (t == 0x01) {
            set(p, QStringLiteral("path_posix"), Value::string(r.string()));
        } else if (t == 0x02) {
            set(p, QStringLiteral("path_utf8"), Value::string(r.string(true)));
        } else if (t == 0x03) {
            set(p, QStringLiteral("path2_utf8"), Value::string(r.string(true)));
        } else {
            r.unknownExt(cls, t);
        }
    }
    r.tag(0x03);
}

// URLLocator (URLL): only the end tag.
void readURLLocator(Reader& r, Props&, const QString&) { r.tag(0x03); }

// GraphicEffect (GRFX): 02 01, s32 size, picture data.
void readGraphicEffect(Reader& r, Props& p, const QString&) {
    r.tag(0x02);
    r.tag(0x01);
    const qint64 size = r.s32();
    r.check(size >= 0, "negative picture size");
    set(p, QStringLiteral("pict_data"), Value::bytes(r.bytes(size)));
    r.tag(0x03);
}

// EffectParamList (FXPS): 02 12, original length, window offset, count,
// key frame size, then each key frame's fixed parameters.
void readEffectParamList(Reader& r, Props& p, const QString&) {
    r.tag(0x02);
    r.tag(0x12);
    set(p, QStringLiteral("orig_length"), Value::integer(r.s32()));
    set(p, QStringLiteral("window_offset"), Value::integer(r.s32()));
    const qint64 count = r.s32();
    set(p, QStringLiteral("keyframe_size"), Value::integer(r.s32()));
    ValueList params;
    for (qint64 i = 0; i < count; ++i) {
        Props e;
        for (const char* name : {"percent_time", "level", "pos_x", "floor_x", "ceil_x", "pos_y", "floor_y", "ceil_y",
                                 "scale_x", "scale_y", "crop_left", "crop_right", "crop_top", "crop_bottom"}) {
            set(e, QString::fromLatin1(name), Value::integer(r.s32()));
        }
        ValueList box;
        for (int j = 0; j < 4; ++j) box.push_back(Value::integer(r.s32()));
        set(e, QStringLiteral("box"), Value::list(std::move(box)));
        for (const char* name : {"box_xscale", "box_yscale", "box_xpos", "box_ypos"}) {
            set(e, QString::fromLatin1(name), Value::boolean(r.boolean()));
        }
        set(e, QStringLiteral("border_width"), Value::integer(r.s32()));
        set(e, QStringLiteral("border_soft"), Value::integer(r.s32()));
        for (const char* name : {"splill_gain2", "splill_gain", "splill_soft2", "splill_soft"}) {
            set(e, QString::fromLatin1(name), Value::integer(r.s16()));
        }
        set(e, QStringLiteral("enable_key_flags"), Value::integer(r.s8()));
        const qint64 colors = r.s32();
        r.check(colors >= 0, "negative color count");
        ValueList list;
        for (qint64 j = 0; j < colors; ++j) list.push_back(Value::integer(r.s32()));
        set(e, QStringLiteral("colors"), Value::list(std::move(list)));
        const qint64 size = r.s32();
        r.check(size >= 0, "negative user parameter size");
        set(e, QStringLiteral("user_param"), Value::bytes(r.bytes(size)));
        set(e, QStringLiteral("selected"), Value::boolean(r.boolean()));
        params.push_back(Value::record(std::move(e)));
    }
    set(p, QStringLiteral("parameters"), Value::list(std::move(params)));
    r.tag(0x03);
}

// ParameterItem (PRIT): 02 02, the parameter's UUID, value type and value
// (1 s32, 2 double, 4 reference), name, enabled, control track; ext 01: 66
// contributes to signature.
void readParameterItem(Reader& r, Props& p, const QString& cls) {
    r.tag(0x02);
    r.tag(0x02);
    set(p, QStringLiteral("uuid"), Value::uuid(r.rawUuid()));
    const qint64 type = r.s16();
    set(p, QStringLiteral("value_type"), Value::integer(type));
    if (type == 1) {
        set(p, QStringLiteral("value"), Value::integer(r.s32()));
    } else if (type == 2) {
        set(p, QStringLiteral("value"), Value::real(r.f64()));
    } else if (type == 4) {
        set(p, QStringLiteral("value"), r.ref());
    } else {
        r.fail(QStringLiteral("unknown value_type: %1").arg(type));
    }
    set(p, kName, Value::string(r.string()));
    set(p, QStringLiteral("enable"), Value::boolean(r.boolean()));
    set(p, QStringLiteral("control_track"), r.ref());
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t != 0x01) r.unknownExt(cls, t);
        r.tag(66);
        set(p, QStringLiteral("contribs_to_sig"), Value::boolean(r.boolean()));
    }
    r.tag(0x03);
}

// MSMLocator (MSML): 02 02, the old MobID (unused), last known volume; ext
// 01: 71 domain type; 02: the MobID; 03: 76 the volume in UTF-8.
void readMSMLocator(Reader& r, Props& p, const QString& cls) {
    r.tag(0x02);
    r.tag(0x02);
    r.u32();
    r.u32();
    set(p, QStringLiteral("last_known_volume"), Value::string(r.string()));
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t == 0x01) {
            r.tag(71);
            set(p, QStringLiteral("domain_type"), Value::integer(r.s32()));
        } else if (t == 0x02) {
            set(p, QStringLiteral("mob_id"), Value::mobId(r.mobId()));
        } else if (t == 0x03) {
            r.tag(76);
            set(p, QStringLiteral("last_known_volume_utf8"), Value::string(r.string(true)));
        } else {
            r.unknownExt(cls, t);
        }
    }
    r.tag(0x03);
}

// BinRef (MCBR): 02 01, the bin's ID (high, low) and name; ext 01: 76 the
// name in UTF-8.
void readBinRef(Reader& r, Props& p, const QString& cls) {
    r.tag(0x02);
    r.tag(0x01);
    set(p, QStringLiteral("uid_high"), Value::integer(r.s32()));
    set(p, QStringLiteral("uid_low"), Value::integer(r.s32()));
    set(p, kName, Value::string(r.string()));
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t != 0x01) r.unknownExt(cls, t);
        r.tag(76);
        set(p, QStringLiteral("name_utf8"), Value::string(r.string(true)));
    }
    r.tag(0x03);
}

// Attributes (ATTR): 02 01, u32 count, each a u32 type, a name and a value
// (1 s32, 2 string, 3 reference, 4 u32 size and bytes). A name given twice
// keeps its first place and its last value, as a Python dict does.
void readAttributes(Reader& r, Props& p, const QString&) {
    r.tag(0x02);
    r.tag(0x01);
    const std::uint32_t count = r.u32();
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t type = r.u32();
        const QString name = r.string();
        switch (type) {
        case 1: set(p, name, Value::integer(r.s32())); break;
        case 2: set(p, name, Value::string(r.string())); break;
        case 3: set(p, name, r.ref()); break;
        case 4: {
            const std::uint32_t size = r.u32();
            set(p, name, Value::bytes(r.bytes(size)));
            break;
        }
        default: r.fail(QStringLiteral("Unknown attr name: %1 type: %2").arg(name).arg(type));
        }
    }
    r.tag(0x03);
}

// ParameterList (PRLS): 02 01, s32 count, the refs.
void readParameterList(Reader& r, ValueList& items) {
    r.tag(0x02);
    r.tag(0x01);
    const qint64 count = r.s32();
    for (qint64 i = 0; i < count; ++i) items.push_back(r.ref());
    r.tag(0x03);
}

// The bin view setting (BVst), after Setting's 02 06 name, kind, attribute
// count and type, attributes: 02 0a, the columns (title, format, type,
// hidden); ext 01: 69 count, then each 69 column ID, 71 size, 76, 4 bytes,
// the UTF-8 format descriptor.
void readBinViewSetting(Reader& r, Props& p, const QString& cls) {
    r.tag(0x02);
    r.tag(0x06);
    set(p, kName, Value::string(r.string()));
    set(p, QStringLiteral("kind"), Value::string(r.string()));
    set(p, QStringLiteral("attr_count"), Value::integer(r.s16()));
    set(p, QStringLiteral("attr_type"), Value::integer(r.s16()));
    set(p, QStringLiteral("attributes"), r.ref());
    r.tag(0x02);
    r.tag(10);
    const std::uint16_t count = r.u16();
    ValueList columns;
    for (std::uint16_t i = 0; i < count; ++i) {
        Props c;
        set(c, QStringLiteral("title"), Value::string(r.string()));
        set(c, QStringLiteral("format"), Value::integer(r.s16()));
        set(c, QStringLiteral("type"), Value::integer(r.s16()));
        set(c, QStringLiteral("hidden"), Value::boolean(r.boolean()));
        columns.push_back(Value::record(std::move(c)));
    }
    set(p, QStringLiteral("columns"), Value::list(std::move(columns)));
    std::uint8_t t = 0;
    while (r.ext(t)) {
        if (t != 0x01) r.unknownExt(cls, t);
        r.tag(69);
        const qint64 n = r.s16();
        r.check(n >= 0, "negative format descriptor count");
        ValueList list;
        for (qint64 i = 0; i < n; ++i) {
            Props d;
            r.tag(69);
            set(d, QStringLiteral("vcid_free_column_id"), Value::integer(r.s16()));
            r.tag(71);
            const qint64 size = r.s32();
            r.tag(76);
            r.s32();
            const auto text = fromUtf8Strict(r.bytes(size));
            if (!text) r.fail(QStringLiteral("invalid UTF-8"));
            set(d, QStringLiteral("format_descriptor"), Value::string(*text));
            list.push_back(Value::record(std::move(d)));
        }
        set(p, QStringLiteral("format_descriptors"), Value::list(std::move(list)));
    }
    r.tag(0x03);
}

// Bin (ABIN): 02, version 0e (u16 item count) or 0f (u32, a large bin), the
// view setting, u64 ID, the items (mob, x, y, keyframe, user placed),
// display mask and mode, sifting (on, then six method/string/column), sort
// columns (direction, column), font, size, image scale, home rect, colours,
// QuickLook image scale, attributes, was iconic.
void readBin(Reader& r, Props& p, const QString& cls) {
    r.tag(0x02);
    const std::uint8_t version = r.u8();
    r.check(version == 0x0e || version == 0x0f, "unknown bin version");
    set(p, QStringLiteral("large_bin"), Value::boolean(version == 0x0f));
    set(p, QStringLiteral("view_setting"), r.ref());
    set(p, QStringLiteral("uid"), Value::uint64(r.u64()));
    const std::uint32_t count = version == 0x0e ? static_cast<std::uint32_t>(r.u16()) : r.u32();
    ValueList items;
    for (std::uint32_t i = 0; i < count; ++i) {
        Props e;
        set(e, QStringLiteral("mob"), r.ref());
        set(e, QStringLiteral("x"), Value::integer(r.s16()));
        set(e, QStringLiteral("y"), Value::integer(r.s16()));
        set(e, QStringLiteral("keyframe"), Value::integer(r.s32()));
        set(e, QStringLiteral("user_placed"), Value::boolean(r.boolean()));
        items.push_back(Value::record(std::move(e)));
    }
    set(p, QStringLiteral("items"), Value::list(std::move(items)));
    set(p, QStringLiteral("display_mask"), Value::integer(r.s32()));
    set(p, QStringLiteral("display_mode"), Value::integer(r.s16()));
    set(p, QStringLiteral("sifted"), Value::boolean(r.boolean()));
    ValueList sifted;
    for (int i = 0; i < 6; ++i) {
        Props s;
        set(s, QStringLiteral("method"), Value::integer(r.s16()));
        set(s, QStringLiteral("string"), Value::string(r.string()));
        set(s, QStringLiteral("column"), Value::string(r.string()));
        sifted.push_back(Value::record(std::move(s)));
    }
    set(p, QStringLiteral("sifted_settings"), Value::list(std::move(sifted)));
    const qint64 sortCount = r.s16();
    ValueList sort;
    for (qint64 i = 0; i < sortCount; ++i) {
        const qint64 direction = r.u8();
        sort.push_back(Value::list({Value::integer(direction), Value::string(r.string())}));
    }
    set(p, QStringLiteral("sort_columns"), Value::list(std::move(sort)));
    set(p, QStringLiteral("mac_font"), Value::integer(r.s16()));
    set(p, QStringLiteral("mac_font_size"), Value::integer(r.s16()));
    set(p, QStringLiteral("mac_image_scale"), Value::integer(r.s16()));
    r.check(r.s16() == 1, "rect version is not 1");
    ValueList rect;
    for (int i = 0; i < 4; ++i) rect.push_back(Value::integer(r.s16()));
    set(p, QStringLiteral("home_rect"), Value::list(std::move(rect)));
    for (const char* name : {"background_color", "forground_color"}) {
        r.check(r.s16() == 1, "colour version is not 1");
        ValueList rgb;
        for (int i = 0; i < 3; ++i) rgb.push_back(Value::integer(r.u16()));
        set(p, QString::fromLatin1(name), Value::list(std::move(rgb)));
    }
    set(p, QStringLiteral("ql_image_scale"), Value::integer(r.s16()));
    set(p, QStringLiteral("attributes"), r.ref());
    set(p, QStringLiteral("was_iconic"), Value::boolean(r.boolean()));
    if (cls == QLatin1String("ABIN")) r.tag(0x03);
}

using ReadFn = void (*)(Reader&, Props&, const QString&);

struct ClassEntry {
    const char* id;
    ReadFn read;
};

// The classes read, by class ID (pyavb's registered classes among them).
constexpr ClassEntry kClasses[] = {
    {"ABIN", readBin},
    {"BVst", readBinViewSetting},
    {"ATTR", readAttributes},
    {"SCLP", readSourceClip},
    {"TCCP", readTimecode},
    {"ECCP", readEdgecode},
    {"TRKR", readTrackRef},
    {"FILL", readFiller},
    {"PRCL", readParamClip},
    {"CTRL", readControlClip},
    {"SEQU", readSequence},
    {"TRKG", readTrackGroup},
    {"TKFX", readTrackEffect},
    {"PVOL", readPanVolume},
    {"RSET", readEssenceGroup},
    {"TNFX", readTransition},
    {"MASK", readCaptureMask},
    {"SPED", readMotionEffect},
    {"CMPO", readComposition},
    {"MDES", readMediaDescriptor},
    {"MDFL", readMediaFileDescriptor},
    {"PCMA", readPCMA},
    {"DIDD", readDIDD},
    {"CDCI", readCDCI},
    {"RGBA", readRGBA},
    {"FILE", readFileLocator},
    {"WINF", readFileLocator},
    {"URLL", readURLLocator},
    {"GRFX", readGraphicEffect},
    {"FXPS", readEffectParamList},
    {"PRIT", readParameterItem},
    {"MSML", readMSMLocator},
    {"MCBR", readBinRef},
};

ReadFn readerFor(const QString& cls) {
    for (const ClassEntry& c : kClasses) {
        if (cls == QLatin1String(c.id)) return c.read;
    }
    return nullptr;
}

} // namespace

// ---- Value and Object ---------------------------------------------------------

qint64 Value::toInt() const {
    if (const auto* i = std::get_if<qint64>(&data_)) return *i;
    if (const auto* b = std::get_if<bool>(&data_)) return *b ? 1 : 0;
    if (const auto* u = std::get_if<UInt64>(&data_)) return static_cast<qint64>(u->value);
    if (const auto* t = std::get_if<Time>(&data_)) return t->seconds;
    return 0;
}

double Value::toDouble() const {
    if (const auto* d = std::get_if<double>(&data_)) return *d;
    if (const auto* u = std::get_if<UInt64>(&data_)) return static_cast<double>(u->value);
    return static_cast<double>(toInt());
}

QString Value::toString() const {
    if (const auto* s = std::get_if<QString>(&data_)) return *s;
    return {};
}

QByteArray Value::toBytes() const {
    if (const auto* b = std::get_if<QByteArray>(&data_)) return *b;
    return {};
}

std::optional<MobId> Value::toMobId() const {
    if (const auto* m = std::get_if<MobId>(&data_)) return *m;
    return std::nullopt;
}

std::optional<QUuid> Value::toUuid() const {
    if (const auto* u = std::get_if<QUuid>(&data_)) return *u;
    return std::nullopt;
}

std::optional<Ref> Value::toRef() const {
    if (const auto* r = std::get_if<Ref>(&data_)) return *r;
    return std::nullopt;
}

std::optional<Time> Value::toTime() const {
    if (const auto* t = std::get_if<Time>(&data_)) return *t;
    return std::nullopt;
}

const ValueList& Value::toList() const {
    if (const auto* l = std::get_if<std::shared_ptr<const ValueList>>(&data_)) return **l;
    return kEmptyList;
}

const Properties& Value::toRecord() const {
    if (const auto* p = std::get_if<std::shared_ptr<const Properties>>(&data_)) return **p;
    return kEmptyProperties;
}

const Value& Value::member(const QString& name) const {
    const Value* v = find(toRecord(), name);
    return v ? *v : kNullValue;
}

QDateTime Time::toDateTime() const { return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::utc()); }

const Value* find(const Properties& properties, const QString& name) {
    for (const Property& p : properties) {
        if (p.name == name) return &p.value;
    }
    return nullptr;
}

const Value& Object::property(const QString& name) const {
    const Value* v = find(properties, name);
    return v ? *v : kNullValue;
}

// ---- Strings and numbers --------------------------------------------------------

QString fromMacRoman(const QByteArray& bytes) {
    QString out;
    out.reserve(bytes.size());
    for (const char c : bytes) {
        const auto b = static_cast<std::uint8_t>(c);
        out.append(QChar(b < 0x80 ? static_cast<char16_t>(b) : kMacRoman[static_cast<std::size_t>(b - 0x80)]));
    }
    return out;
}

std::optional<QString> fromUtf8Strict(const QByteArray& bytes) {
    // Validate as Python's strict decoder does (no overlong forms, no
    // surrogates, nothing past U+10FFFF), then let Qt convert.
    const auto* p = reinterpret_cast<const std::uint8_t*>(bytes.constData());
    const qsizetype n = bytes.size();
    for (qsizetype i = 0; i < n;) {
        const std::uint8_t b = p[i];
        int extra = 0;
        std::uint32_t lo = 0x80;
        std::uint32_t hi = 0xBF; // the range of the second byte
        if (b < 0x80) {
            ++i;
            continue;
        } else if (b >= 0xC2 && b <= 0xDF) {
            extra = 1;
        } else if (b >= 0xE0 && b <= 0xEF) {
            extra = 2;
            if (b == 0xE0) lo = 0xA0;
            if (b == 0xED) hi = 0x9F;
        } else if (b >= 0xF0 && b <= 0xF4) {
            extra = 3;
            if (b == 0xF0) lo = 0x90;
            if (b == 0xF4) hi = 0x8F;
        } else {
            return std::nullopt;
        }
        if (i + extra >= n) return std::nullopt;
        if (p[i + 1] < lo || p[i + 1] > hi) return std::nullopt;
        for (int k = 2; k <= extra; ++k) {
            if (p[i + k] < 0x80 || p[i + k] > 0xBF) return std::nullopt;
        }
        i += extra + 1;
    }
    return QString::fromUtf8(bytes);
}

QString pythonRepr(double value) {
    if (std::isnan(value)) return QStringLiteral("nan");
    if (std::isinf(value)) return value < 0 ? QStringLiteral("-inf") : QStringLiteral("inf");
    if (value == 0) return std::signbit(value) ? QStringLiteral("-0.0") : QStringLiteral("0.0");
    // The shortest digits that read back as the same double, as
    // "d.ddde+XX", then laid out as Python's float_repr_style 'short' does.
    const QString e = QString::number(std::fabs(value), 'e', QLocale::FloatingPointShortest);
    const qsizetype at = e.indexOf(QLatin1Char('e'));
    QString digits = e.left(at);
    digits.remove(QLatin1Char('.'));
    const int exponent = e.mid(at + 1).toInt();
    while (digits.size() > 1 && digits.endsWith(QLatin1Char('0'))) digits.chop(1);
    const auto nd = static_cast<int>(digits.size());
    const int decpt = exponent + 1; // value = 0.digits × 10^decpt
    QString out = std::signbit(value) ? QStringLiteral("-") : QString();
    if (decpt > -4 && decpt <= 16) {
        if (decpt <= 0) {
            out += QStringLiteral("0.") + QString(-decpt, QLatin1Char('0')) + digits;
        } else if (decpt >= nd) {
            out += digits + QString(decpt - nd, QLatin1Char('0')) + QStringLiteral(".0");
        } else {
            out += digits.left(decpt) + QLatin1Char('.') + digits.mid(decpt);
        }
    } else {
        out += digits.left(1);
        if (nd > 1) out += QLatin1Char('.') + digits.mid(1);
        out += QLatin1Char('e');
        out += QLatin1Char(exponent < 0 ? '-' : '+');
        out += QString::number(std::abs(exponent)).rightJustified(2, QLatin1Char('0'));
    }
    return out;
}

// ---- File -----------------------------------------------------------------------

bool File::open(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    return load(f.readAll(), error);
}

// The header (pyavb's AVBFile.__init__), in the file's byte order:
//   06 00 (little-endian) or 00 06 (big-endian), "Domain"
//   fourcc OBJD, string "AObjDoc", u8 0x04, string last save ("2022/07/01
//   06:07:17"), u32 object count, u32 root index, u32 0x49494949 or
//   0x4D4D4D4D, u32 last save time, 4 bytes, fourcc ATob, fourcc ATve,
//   string creator version, 16 reserved bytes
// then the object table: for each object a fourcc class ID, a u32 size and
// the payload.
bool File::load(QByteArray data, QString* error) {
    *this = File();
    auto refuse = [&](const QString& why) {
        if (error) *error = why;
        *this = File();
        return false;
    };
    if (data.size() < 8) return refuse(QStringLiteral("not a bin file (too short)"));
    if (data.startsWith(QByteArray("\x06\x00", 2))) {
        little_ = true;
    } else if (data.startsWith(QByteArray("\x00\x06", 2))) {
        little_ = false;
    } else {
        return refuse(QStringLiteral("not a bin file (no byte order mark)"));
    }
    if (data.mid(2, 6) != "Domain") return refuse(QStringLiteral("not a bin file (no \"Domain\")"));
    Reader r(data.constData() + 8, data.size() - 8, little_, 0);
    std::uint32_t count = 0;
    try {
        r.check(r.fourcc() == "OBJD", "no OBJD header");
        r.check(r.string() == QLatin1String("AObjDoc"), "no AObjDoc");
        r.check(r.u8() == 0x04, "header version is not 4");
        lastSaveText_ = r.string();
        count = r.u32();
        rootIndex_ = r.u32();
        const std::uint32_t mark = r.u32();
        r.check(mark == 0x49494949 || mark == 0x4D4D4D4D, "no II or MM mark");
        lastSave_ = Time{r.u32()};
        r.take(4);
        r.check(r.fourcc() == "ATob", "no ATob");
        r.check(r.fourcc() == "ATve", "no ATve");
        creatorVersion_ = r.string();
        r.take(16);
        // Each chunk takes at least 8 bytes: a count past that is a damaged
        // table, refused before anything is allocated for it.
        if (count > r.left() / 8) r.fail(QStringLiteral("object table of %1 objects cut short").arg(count));
        positions_.reserve(count);
        qsizetype at = 8 + r.pos();
        for (std::uint32_t i = 0; i < count; ++i) {
            if (data.size() - at < 8) {
                throw ReadError{QStringLiteral("object table cut short at object %1").arg(i + 1)};
            }
            positions_.push_back(at);
            const char* size = data.constData() + at + 4;
            const std::uint32_t n = little_ ? qFromLittleEndian<quint32>(size) : qFromBigEndian<quint32>(size);
            at += 8 + static_cast<qsizetype>(n);
        }
    } catch (const ReadError& e) {
        return refuse(QStringLiteral("not a readable bin: header: %1").arg(e.what));
    }
    if (rootIndex_ == 0 || rootIndex_ > count) return refuse(QStringLiteral("root index %1 is out of range").arg(rootIndex_));
    data_ = std::move(data);
    cache_.resize(positions_.size() + 1);
    return true;
}

QString File::classIdAt(std::uint32_t index) const {
    if (index == 0 || index > objectCount()) return {};
    QByteArray id = data_.mid(positions_[index - 1], 4);
    if (little_) std::reverse(id.begin(), id.end());
    return QString::fromLatin1(id);
}

const Object& File::object(std::uint32_t index) const {
    if (index == 0 || index > objectCount()) {
        static const Object invalid = [] {
            Object o;
            o.error = QStringLiteral("no such object");
            return o;
        }();
        return invalid;
    }
    std::unique_ptr<Object>& slot = cache_[index];
    if (slot) return *slot;
    slot = std::make_unique<Object>();
    Object& o = *slot;
    o.index = index;
    o.classId = classIdAt(index);
    const qsizetype at = positions_[index - 1];
    const char* sizeBytes = data_.constData() + at + 4;
    const std::uint32_t size = little_ ? qFromLittleEndian<quint32>(sizeBytes) : qFromBigEndian<quint32>(sizeBytes);
    const qsizetype available = data_.size() - (at + 8);
    if (static_cast<qsizetype>(size) > available) {
        o.error = QStringLiteral("%1: payload of %2 bytes cut short at %3").arg(o.classId).arg(size).arg(available);
        return o;
    }
    Reader r(data_.constData() + at + 8, size, little_, objectCount());
    try {
        if (o.classId == QLatin1String("PRLS")) {
            o.isList = true;
            readParameterList(r, o.items);
        } else if (const ReadFn read = readerFor(o.classId)) {
            read(r, o.properties, o.classId);
        } else {
            throw ReadError{QStringLiteral("class not read")};
        }
        if (r.left() != 0) r.fail(QStringLiteral("%1 bytes left over").arg(r.left()));
    } catch (const ReadError& e) {
        o.properties.clear();
        o.items.clear();
        o.error = QStringLiteral("%1: %2").arg(o.classId, e.what);
    }
    return o;
}

// ---- Dump -----------------------------------------------------------------------

namespace {

// Expands references depth first. `path` holds the objects being dumped
// above this one; one of them met again is a cycle, written {"ref": index}.
class Dumper {
public:
    explicit Dumper(const File& file) : file_(file) {}

    QJsonValue object(std::uint32_t index) {
        const Object& o = file_.object(index);
        if (!o.ok()) return QJsonObject{{QStringLiteral("class"), o.classId}, {QStringLiteral("error"), o.error}};
        // A guard against stack exhaustion on hostile files; Python's
        // recursion limit stops pyavb's dump far sooner.
        if (path_.size() > 400) {
            return QJsonObject{{QStringLiteral("class"), o.classId}, {QStringLiteral("error"), QStringLiteral("nested too deep")}};
        }
        if (++count_ > 2'000'000) {
            return QJsonObject{{QStringLiteral("class"), o.classId}, {QStringLiteral("error"), QStringLiteral("too many objects")}};
        }
        path_.push_back(index);
        QJsonObject out{{QStringLiteral("class"), o.classId}};
        if (o.isList) {
            QJsonArray items;
            for (const Value& v : o.items) items.append(value(v));
            out.insert(QStringLiteral("items"), items);
        } else {
            out.insert(QStringLiteral("properties"), record(o.properties));
        }
        path_.pop_back();
        return out;
    }

private:
    QJsonObject record(const Properties& props) {
        QJsonObject out;
        for (const Property& p : props) out.insert(p.name, value(p.value));
        return out;
    }

    QJsonValue value(const Value& v) {
        const Value::Data& d = v.data();
        if (const auto* b = std::get_if<bool>(&d)) return *b;
        if (const auto* i = std::get_if<qint64>(&d)) return *i;
        if (const auto* u = std::get_if<UInt64>(&d)) {
            if (u->value <= static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())) {
                return static_cast<qint64>(u->value);
            }
            return QJsonObject{{QStringLiteral("int"), QString::number(u->value)}};
        }
        if (const auto* f = std::get_if<double>(&d)) return QJsonObject{{QStringLiteral("float"), pythonRepr(*f)}};
        if (const auto* s = std::get_if<QString>(&d)) return *s;
        if (const auto* b = std::get_if<QByteArray>(&d)) {
            return QJsonObject{{QStringLiteral("bytes"), QString::fromLatin1(b->toHex())}};
        }
        if (const auto* m = std::get_if<MobId>(&d)) return QString::fromStdString(m->toString());
        if (const auto* u = std::get_if<QUuid>(&d)) return u->toString(QUuid::WithoutBraces);
        if (const auto* t = std::get_if<Time>(&d)) return static_cast<qint64>(t->seconds);
        if (const auto* r = std::get_if<Ref>(&d)) {
            if (r->index == 0) return QJsonValue();
            if (std::find(path_.begin(), path_.end(), r->index) != path_.end()) {
                return QJsonObject{{QStringLiteral("ref"), static_cast<qint64>(r->index)}};
            }
            return object(r->index);
        }
        if (const auto* l = std::get_if<std::shared_ptr<const ValueList>>(&d)) {
            QJsonArray out;
            for (const Value& e : **l) out.append(value(e));
            return out;
        }
        if (const auto* p = std::get_if<std::shared_ptr<const Properties>>(&d)) return record(**p);
        return QJsonValue();
    }

    const File& file_;
    std::vector<std::uint32_t> path_;
    qint64 count_ = 0;
};

} // namespace

QJsonValue dump(const File& file) { return dumpObject(file, file.rootIndex()); }

QJsonValue dumpObject(const File& file, std::uint32_t index) {
    if (!file.isOpen()) return QJsonValue();
    return Dumper(file).object(index);
}

} // namespace lumenis::interop::avb
