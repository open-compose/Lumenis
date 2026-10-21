// Writing Media Composer bins (.avb): the header, the object table and each
// class's payload (docs/spec/avb.md, "Writing"). Each class's write is
// ported from pyavb's write() methods (MIT, Mark Reid; credited in
// THIRD-PARTY-NOTICES.md) and is the inverse of its read in avb.cpp: the
// same parts, versions, extensions and end tags, from the properties the
// reader sets, under the same names. Where pyavb writes what its reader
// skips (the old 8-byte MobIDs, unused bytes) it is written as pyavb writes
// it, which gives back Media Composer's bytes for every bin of the corpus.
//
// Payloads are built through Out, which checks each value against its
// field's range; a value of the wrong kind or out of range throws
// WriteError, which encodeObject() turns into an error.

#include "lumenis/interop/avb_writer.h"

#include "lumenis/interop/aaf.h"

#include <QSaveFile>
#include <QtEndian>

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

namespace lumenis::interop::avb {

namespace {

struct WriteError {
    QString what;
};

[[noreturn]] void fail(const QString& what) { throw WriteError{what}; }

QString nameOf(const char* name) { return QString::fromLatin1(name); }

// A growing payload in one byte order (pyavb's AVBIOContext, writing).
class Out {
public:
    Out(bool little, std::uint32_t objectCount) : little_(little), count_(objectCount) {}

    QByteArray data;
    bool little() const { return little_; }

    void raw(const QByteArray& b) { data.append(b); }
    void zeros(int n) { data.append(QByteArray(n, '\0')); }

    // Integers, each checked against its field's range (Python's struct.pack
    // refuses what does not fit).
    void u8(qint64 v, const char* name = "u8") {
        check(v, 0, 0xFF, name);
        data.append(static_cast<char>(v));
    }
    void s8(qint64 v, const char* name = "s8") {
        check(v, -0x80, 0x7F, name);
        data.append(static_cast<char>(static_cast<std::int8_t>(v)));
    }
    void u16(qint64 v, const char* name = "u16") {
        check(v, 0, 0xFFFF, name);
        put<quint16>(static_cast<quint16>(v));
    }
    void s16(qint64 v, const char* name = "s16") {
        check(v, -0x8000, 0x7FFF, name);
        put<quint16>(static_cast<quint16>(static_cast<std::int16_t>(v)));
    }
    void u32(qint64 v, const char* name = "u32") {
        check(v, 0, 0xFFFFFFFFLL, name);
        put<quint32>(static_cast<quint32>(v));
    }
    void s32(qint64 v, const char* name = "s32") {
        check(v, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max(), name);
        put<quint32>(static_cast<quint32>(static_cast<std::int32_t>(v)));
    }
    void u64(std::uint64_t v) { put<quint64>(v); }
    void s64(qint64 v) { put<quint64>(static_cast<quint64>(v)); }
    // Doubles are little-endian whatever the file's order, as pyavb writes
    // (and reads) them.
    void f64(double d) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &d, sizeof bits);
        char b[8];
        qToLittleEndian<quint64>(bits, b);
        data.append(b, 8);
    }
    void boolean(bool b) { data.append(b ? '\x01' : '\x00'); }
    // A fourcc: reversed in little-endian files.
    void fourcc(QByteArray b, const char* name = "fourcc") {
        if (b.size() != 4) fail(QStringLiteral("%1: a fourcc is 4 bytes, not %2").arg(nameOf(name)).arg(b.size()));
        if (little_) std::reverse(b.begin(), b.end());
        data.append(b);
    }
    void tag(std::uint8_t t) { data.append(static_cast<char>(t)); }
    // An extension's start: 01 and its tag.
    void ext(std::uint8_t t) {
        tag(0x01);
        tag(t);
    }

    // A string (write_string): u16 size and the bytes, size 0 for an empty
    // string. UTF-8 strings start with two NULs, which the reader strips.
    void string(const QString& s, const char* name, bool utf8 = false) {
        if (s.isEmpty()) {
            u16(0);
            return;
        }
        QByteArray b;
        if (utf8) {
            b = QByteArray(2, '\0') + s.toUtf8();
        } else {
            const auto roman = toMacRoman(s);
            if (!roman) fail(QStringLiteral("%1: \"%2\" has characters Mac OS Roman cannot hold").arg(nameOf(name), s));
            b = *roman;
        }
        if (b.size() >= 0xFFFF) fail(QStringLiteral("%1: a string of %2 bytes is too long").arg(nameOf(name)).arg(b.size()));
        u16(b.size());
        raw(b);
    }
    // A string that may be absent (Component's name and effect ID, PRIT's
    // name): size 0xFFFF for none or empty.
    void optionalString(const QString& s, const char* name) {
        if (s.isEmpty()) {
            u16(0xFFFF);
        } else {
            string(s, name);
        }
    }

    // A reference: the index, 0 for none; one past the table is an error,
    // as the reader would refuse it.
    void ref(std::uint32_t index, const char* name) {
        if (index > count_) fail(QStringLiteral("%1: reference to object %2 of %3").arg(nameOf(name)).arg(index).arg(count_));
        u32(index);
    }

    // A UUID's Data1-3 in the file's order, then Data4 (write_raw_uuid).
    void rawUuid(const QUuid& u) {
        u32(u.data1);
        u16(u.data2);
        u16(u.data3);
        data.append(reinterpret_cast<const char*>(u.data4), 8);
    }
    // The tagged form (write_uuid): 72 Data1, 70 Data2, 70 Data3, 65 s32(8) Data4.
    void taggedUuid(std::uint32_t d1, std::uint16_t d2, std::uint16_t d3, const char* d4) {
        tag(72);
        u32(d1);
        tag(70);
        u16(d2);
        tag(70);
        u16(d3);
        tag(65);
        s32(8);
        data.append(d4, 8);
    }

    // A number as a 32-bit mantissa and a 16-bit power of ten
    // (write_exp10_encoded_float): multiplied by ten until it is whole, at
    // most six times or while the mantissa fits; 59.94 is written 59940e-3,
    // as Media Composer writes it.
    void exp10(double value, const char* name) {
        if (!std::isfinite(value)) fail(QStringLiteral("%1: %2 is not a finite number").arg(nameOf(name)).arg(value));
        int exponent = 0;
        while (std::trunc(value) != value) {
            if (std::fabs(value * 10) >= 0x7FFFFFFF) break;
            if (exponent <= -6) break;
            value *= 10;
            exponent -= 1;
        }
        if (value == 5994) {
            s32(59940, name);
            s16(-3, name);
            return;
        }
        const double mantissa = std::trunc(value);
        if (mantissa < std::numeric_limits<std::int32_t>::min() || mantissa > std::numeric_limits<std::int32_t>::max()) {
            fail(QStringLiteral("%1: %2 does not fit a 32-bit mantissa").arg(nameOf(name)).arg(value));
        }
        s32(static_cast<qint64>(mantissa), name);
        s16(exponent, name);
    }

private:
    static void check(qint64 v, qint64 lo, qint64 hi, const char* name) {
        if (v < lo || v > hi) fail(QStringLiteral("%1: %2 is out of range [%3, %4]").arg(nameOf(name)).arg(v).arg(lo).arg(hi));
    }
    template <typename T>
    void put(T v) {
        char b[sizeof(T)];
        if (little_) {
            qToLittleEndian<T>(v, b);
        } else {
            qToBigEndian<T>(v, b);
        }
        data.append(b, sizeof(T));
    }

    bool little_;
    std::uint32_t count_;
};

// A MobID's AAF form split as bins store it (read_mob_id): the 12-byte
// label, length, instance number, and the material number's Data1-3, Data4.
struct MobParts {
    QByteArray aaf; // 32 bytes: aaf::mobIdBytes
    std::uint32_t d1 = 0;
    std::uint16_t d2 = 0;
    std::uint16_t d3 = 0;
};

MobParts partsOf(const MobId& m) {
    MobParts p;
    p.aaf = aaf::mobIdBytes(m);
    p.d1 = qFromLittleEndian<quint32>(p.aaf.constData() + 16);
    p.d2 = qFromLittleEndian<quint16>(p.aaf.constData() + 20);
    p.d3 = qFromLittleEndian<quint16>(p.aaf.constData() + 22);
    return p;
}

// write_mob_id: 65 s32(12) the label, 68 length, 68/68/68 the instance
// number, the material number as a tagged UUID.
void writeMobId(Out& o, const MobId& m) {
    const MobParts p = partsOf(m);
    o.tag(65);
    o.s32(12);
    o.raw(p.aaf.left(12));
    for (int i = 12; i < 16; ++i) {
        o.tag(68);
        o.u8(static_cast<std::uint8_t>(p.aaf[i]));
    }
    o.taggedUuid(p.d1, p.d2, p.d3, p.aaf.constData() + 24);
}

// The old 8-byte MobID before a newer one in SCLP, CMPO and MSML: pyavb
// writes the material number's Data1, then Data2 + Data3 << 16; zeros
// without a MobID.
void writeOldMobId(Out& o, const std::optional<MobId>& m) {
    if (!m) {
        o.zeros(8);
        return;
    }
    const MobParts p = partsOf(*m);
    o.u32(p.d1);
    o.u32(static_cast<qint64>(p.d2) + (static_cast<qint64>(p.d3) << 16));
}

// The properties of one object or record being written, read by name with
// the kind of value each field takes. A missing (or null) property is zero,
// empty or no reference.
class In {
public:
    In(Out& out, const Properties& p) : o(out), p_(p) {}

    Out& o;

    const Value* get(const char* name) const { return find(p_, QLatin1String(name)); }
    bool has(const char* name) const { return get(name) != nullptr; }

    qint64 integer(const char* name) const { return integerOf(get(name), name); }
    static qint64 integerOf(const Value* v, const char* name) {
        if (!v || v->isNull()) return 0;
        const Value::Data& d = v->data();
        if (const auto* i = std::get_if<qint64>(&d)) return *i;
        if (const auto* b = std::get_if<bool>(&d)) return *b ? 1 : 0;
        if (const auto* t = std::get_if<Time>(&d)) return t->seconds;
        if (const auto* u = std::get_if<UInt64>(&d)) {
            if (u->value > static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())) {
                fail(QStringLiteral("%1: %2 is out of range").arg(nameOf(name)).arg(u->value));
            }
            return static_cast<qint64>(u->value);
        }
        fail(QStringLiteral("%1: not an integer").arg(nameOf(name)));
    }
    std::uint64_t unsigned64(const char* name) const {
        const Value* v = get(name);
        if (v) {
            if (const auto* u = std::get_if<UInt64>(&v->data())) return u->value;
        }
        const qint64 i = integer(name);
        if (i < 0) fail(QStringLiteral("%1: %2 is negative").arg(nameOf(name)).arg(i));
        return static_cast<std::uint64_t>(i);
    }
    double real(const char* name) const {
        const Value* v = get(name);
        if (v) {
            if (const auto* d = std::get_if<double>(&v->data())) return *d;
        }
        return static_cast<double>(integer(name));
    }
    bool boolean(const char* name) const { return integer(name) != 0; }
    QString string(const char* name) const {
        const Value* v = get(name);
        if (!v || v->isNull()) return {};
        if (const auto* s = std::get_if<QString>(&v->data())) return *s;
        fail(QStringLiteral("%1: not a string").arg(nameOf(name)));
    }
    QByteArray bytes(const char* name) const {
        const Value* v = get(name);
        if (!v || v->isNull()) return {};
        if (const auto* b = std::get_if<QByteArray>(&v->data())) return *b;
        fail(QStringLiteral("%1: not bytes").arg(nameOf(name)));
    }
    std::uint32_t ref(const char* name) const { return refOf(get(name), name); }
    static std::uint32_t refOf(const Value* v, const char* name) {
        if (!v || v->isNull()) return 0;
        if (const auto* r = std::get_if<Ref>(&v->data())) return r->index;
        fail(QStringLiteral("%1: not a reference").arg(nameOf(name)));
    }
    std::optional<MobId> mobId(const char* name) const {
        const Value* v = get(name);
        if (!v || v->isNull()) return std::nullopt;
        if (const auto* m = std::get_if<MobId>(&v->data())) return *m;
        fail(QStringLiteral("%1: not a MobID").arg(nameOf(name)));
    }
    QUuid uuid(const char* name) const {
        const Value* v = get(name);
        if (!v || v->isNull()) return {};
        if (const auto* u = std::get_if<QUuid>(&v->data())) return *u;
        fail(QStringLiteral("%1: not a UUID").arg(nameOf(name)));
    }
    const ValueList& list(const char* name) const { return listOf(get(name), name); }
    static const ValueList& listOf(const Value* v, const char* name) {
        static const ValueList empty;
        if (!v || v->isNull()) return empty;
        if (std::holds_alternative<std::shared_ptr<const ValueList>>(v->data())) return v->toList();
        fail(QStringLiteral("%1: not a list").arg(nameOf(name)));
    }
    static const Properties& recordOf(const Value& v, const char* name) {
        static const Properties empty;
        if (v.isNull()) return empty;
        if (std::holds_alternative<std::shared_ptr<const Properties>>(v.data())) return v.toRecord();
        fail(QStringLiteral("%1: not a record").arg(nameOf(name)));
    }
    // A pair (a rational: num, den), as the reader keeps them: a list of two integers.
    std::pair<qint64, qint64> pair(const char* name) const { return pairOf(get(name), name); }
    static std::pair<qint64, qint64> pairOf(const Value* v, const char* name) {
        const ValueList& l = listOf(v, name);
        if (l.empty()) return {0, 0};
        if (l.size() != 2) fail(QStringLiteral("%1: not a pair").arg(nameOf(name)));
        return {integerOf(&l[0], name), integerOf(&l[1], name)};
    }

    // Shorthands: a field written from the property of the same name.
    void u8(const char* n) { o.u8(integer(n), n); }
    void s8(const char* n) { o.s8(integer(n), n); }
    void u16(const char* n) { o.u16(integer(n), n); }
    void s16(const char* n) { o.s16(integer(n), n); }
    void u32(const char* n) { o.u32(integer(n), n); }
    void s32(const char* n) { o.s32(integer(n), n); }
    void s64(const char* n) { o.s64(integer(n)); }
    void u64(const char* n) { o.u64(unsigned64(n)); }
    void f64(const char* n) { o.f64(real(n)); }
    void flag(const char* n) { o.boolean(boolean(n)); }
    void str(const char* n, bool utf8 = false) { o.string(string(n), n, utf8); }
    void reference(const char* n) { o.ref(ref(n), n); }
    void exp10(const char* n) { o.exp10(real(n), n); }
    void time(const char* n) { o.u32(integer(n), n); }

private:
    const Properties& p_;
};

// ---- The classes ------------------------------------------------------------
//
// Each write function writes what the read function of the same class in
// avb.cpp reads, in its order: base classes first, then the class's own
// part, its extensions (only those set) and, for the object's own class, the
// end tag 03.

void part(Out& o, std::uint8_t version) {
    o.tag(0x02);
    o.tag(version);
}

// Component: 02 03, bobs, media kind, edit rate, name, effect ID,
// attributes, session attributes, precompute; ext 01: 72 the parameter list.
void writeComponent(In& in, const QString&) {
    Out& o = in.o;
    part(o, 0x03);
    in.reference("left_bob");
    in.reference("right_bob");
    in.s16("media_kind_id");
    in.exp10("edit_rate");
    o.optionalString(in.string("name"), "name");
    o.optionalString(in.string("effect_id"), "effect_id");
    in.reference("attributes");
    in.reference("session_attrs");
    in.reference("precomputed");
    if (in.has("param_list")) {
        o.ext(0x01);
        o.tag(72);
        in.reference("param_list");
    }
}

// Clip: 02 01, u32 length.
void writeClip(In& in, const QString& cls) {
    writeComponent(in, cls);
    part(in.o, 0x01);
    in.u32("length");
}

// SourceClip (SCLP): 02 03, the old MobID, track, start; ext 01: the MobID.
void writeSourceClip(In& in, const QString& cls) {
    writeClip(in, cls);
    Out& o = in.o;
    part(o, 0x03);
    const auto mob = in.mobId("mob_id");
    writeOldMobId(o, mob);
    in.s16("track_id");
    in.s32("start_time");
    if (mob) {
        o.ext(0x01);
        writeMobId(o, *mob);
    }
    o.tag(0x03);
}

// Timecode (TCCP): 02 01, flags, fps, 6 zero bytes, start.
void writeTimecode(In& in, const QString& cls) {
    writeClip(in, cls);
    Out& o = in.o;
    part(o, 0x01);
    in.u32("flags");
    in.u16("fps");
    o.zeros(6);
    in.u32("start");
    o.tag(0x03);
}

// Edgecode (ECCP): 02 01, the 8-byte header, film kind, code format, base
// perforation, 4 zero bytes, start.
void writeEdgecode(In& in, const QString& cls) {
    writeClip(in, cls);
    Out& o = in.o;
    part(o, 0x01);
    QByteArray header = in.bytes("header");
    if (!in.has("header")) header = QByteArray(8, '\0');
    if (header.size() != 8) fail(QStringLiteral("header: %1 bytes, not 8").arg(header.size()));
    o.raw(header);
    in.u8("film_kind");
    in.u8("code_format");
    in.u16("base_perf");
    o.zeros(4);
    in.s32("start_ec");
    o.tag(0x03);
}

// TrackRef (TRKR): 02 01, relative scope and track.
void writeTrackRef(In& in, const QString& cls) {
    writeClip(in, cls);
    part(in.o, 0x01);
    in.s16("relative_scope");
    in.s16("relative_track");
    in.o.tag(0x03);
}

// Filler (FILL): 02 01 03.
void writeFiller(In& in, const QString& cls) {
    writeClip(in, cls);
    part(in.o, 0x01);
    in.o.tag(0x03);
}

// ParamClip (PRCL): 02 01, interpolation, value type, the control points;
// ext 01: 71 extrapolation; 02: 71 fields.
void writeParamClip(In& in, const QString& cls) {
    writeClip(in, cls);
    Out& o = in.o;
    part(o, 0x01);
    in.s32("interp_kind");
    const qint64 valueType = in.integer("value_type");
    if (valueType != 1 && valueType != 2 && valueType != 4) fail(QStringLiteral("value_type: unknown type %1").arg(valueType));
    o.s16(valueType, "value_type");
    const ValueList& points = in.list("control_points");
    o.s32(static_cast<qint64>(points.size()), "control_points");
    for (const Value& point : points) {
        In cp(o, In::recordOf(point, "control_points"));
        const auto [num, den] = cp.pair("offset");
        o.s32(num, "offset");
        o.s32(den, "offset");
        cp.s32("timescale");
        if (valueType == 1) {
            cp.s32("value");
        } else if (valueType == 2) {
            cp.f64("value");
        } else {
            cp.reference("value");
        }
        const ValueList& pps = cp.list("pp");
        o.s16(static_cast<qint64>(pps.size()), "pp");
        for (const Value& v : pps) {
            In pp(o, In::recordOf(v, "pp"));
            pp.s16("code");
            const qint64 type = pp.integer("type");
            o.s16(type, "type");
            if (type == 2) {
                pp.f64("value");
            } else if (type == 1) {
                pp.s32("value");
            } else {
                fail(QStringLiteral("pp: unknown type %1").arg(type));
            }
        }
    }
    if (in.has("extrap_kind")) {
        o.ext(0x01);
        o.tag(71);
        in.s32("extrap_kind");
    }
    if (in.has("fields")) {
        o.ext(0x02);
        o.tag(71);
        in.s32("fields");
    }
    o.tag(0x03);
}

// ControlClip (CTRL): 02 03, interpolation, the control points (offset,
// time scale, 1, value, point properties).
void writeControlClip(In& in, const QString& cls) {
    writeClip(in, cls);
    Out& o = in.o;
    part(o, 0x03);
    in.s32("interp_kind");
    const ValueList& points = in.list("control_points");
    o.s32(static_cast<qint64>(points.size()), "control_points");
    for (const Value& point : points) {
        In cp(o, In::recordOf(point, "control_points"));
        const auto [a, b] = cp.pair("offset");
        o.s32(a, "offset");
        o.s32(b, "offset");
        cp.s32("time_scale");
        o.boolean(true);
        const auto [va, vb] = cp.pair("value");
        o.s32(va, "value");
        o.s32(vb, "value");
        const ValueList& pps = cp.list("pp");
        o.s16(static_cast<qint64>(pps.size()), "pp");
        for (const Value& v : pps) {
            In pp(o, In::recordOf(v, "pp"));
            pp.s16("code");
            const auto [pa, pb] = pp.pair("value");
            o.s32(pa, "value");
            o.s32(pb, "value");
        }
    }
    o.tag(0x03);
}

// Sequence (SEQU): 02 03, u32 count, the components.
void writeSequence(In& in, const QString& cls) {
    writeComponent(in, cls);
    Out& o = in.o;
    part(o, 0x03);
    const ValueList& components = in.list("components");
    o.u32(static_cast<qint64>(components.size()), "components");
    for (const Value& c : components) o.ref(In::refOf(&c, "components"), "components");
    o.tag(0x03);
}

// TrackGroup (TRKG): 02 08, multicam mode, length, scalars, the tracks, each
// with flags saying which of its fields are set; ext 01: each track's lock
// number (0 when not set), written whenever there are tracks. No end tag.
void writeTrackGroup(In& in, const QString& cls) {
    writeComponent(in, cls);
    Out& o = in.o;
    part(o, 0x08);
    in.u8("mc_mode");
    in.s32("length");
    in.s32("num_scalars");
    const ValueList& tracks = in.list("tracks");
    o.s32(static_cast<qint64>(tracks.size()), "tracks");
    for (const Value& v : tracks) {
        In t(o, In::recordOf(v, "tracks"));
        // The flags (TRACK_*_FLAG): bit 0 label, 1 attributes, 2 component,
        // 3 filler proxy, 4 bob, 5 control code, 6 sub code, 7 start, 8 read
        // only, 9 session attributes.
        static constexpr std::pair<const char*, int> kFlags[] = {
            {"index", 0},        {"attributes", 1},       {"component", 2}, {"filler_proxy", 3},
            {"bob_data", 4},     {"control_code", 5},     {"control_sub_code", 6},
            {"start_pos", 7},    {"read_only", 8},        {"session_attr", 9},
        };
        std::uint32_t flags = 0;
        for (const auto& [name, bit] : kFlags) {
            if (t.has(name)) flags |= 1u << bit;
        }
        o.u16(flags);
        if (t.has("index")) t.s16("index");
        if (t.has("attributes")) t.reference("attributes");
        if (t.has("session_attr")) t.reference("session_attr");
        if (t.has("component")) t.reference("component");
        if (t.has("filler_proxy")) t.reference("filler_proxy");
        if (t.has("bob_data")) t.reference("bob_data");
        if (t.has("control_code")) t.s16("control_code");
        if (t.has("control_sub_code")) t.s16("control_sub_code");
        if (t.has("start_pos")) t.s32("start_pos");
        if (t.has("read_only")) t.flag("read_only");
    }
    if (!tracks.empty()) {
        o.ext(0x01);
        for (const Value& v : tracks) {
            In t(o, In::recordOf(v, "tracks"));
            o.tag(69);
            t.s16("lock_number");
        }
    }
}

// The effect info TKFX (after 02 06) and TNFX (after 02 05) share.
void writeEffectInfo(In& in) {
    in.s32("left_length");
    in.s32("right_length");
    in.s16("info_version");
    in.s32("info_current");
    in.s32("info_smooth");
    in.s16("info_color_item");
    in.s16("info_quality");
    in.s8("info_is_reversed");
    in.flag("info_aspect_on");
    in.reference("keyframes");
    in.flag("info_force_software");
    in.flag("info_never_hardware");
}

// TrackEffect (TKFX): 02 06, the effect info; ext 02: 72 the tracker manager.
void writeTrackEffect(In& in, const QString& cls) {
    writeTrackGroup(in, cls);
    Out& o = in.o;
    part(o, 0x06);
    writeEffectInfo(in);
    if (in.has("trackman")) {
        o.ext(0x02);
        o.tag(72);
        in.reference("trackman");
    }
    if (cls == QLatin1String("TKFX")) o.tag(0x03);
}

// PanVolumeEffect (PVOL): 02 05, level, pan, three flags; ext 01, 02: 71.
void writePanVolume(In& in, const QString& cls) {
    writeTrackEffect(in, cls);
    Out& o = in.o;
    part(o, 0x05);
    in.s32("level");
    in.s32("pan");
    in.flag("suppress_validation");
    in.flag("level_set");
    in.flag("pan_set");
    if (in.has("supports_seperate_gain")) {
        o.ext(0x01);
        o.tag(71);
        in.s32("supports_seperate_gain");
    }
    if (in.has("is_trim_gain_effect")) {
        o.ext(0x02);
        o.tag(71);
        in.s32("is_trim_gain_effect");
    }
    o.tag(0x03);
}

// EssenceGroup (RSET): 02 01; ext 01: 71 the type.
void writeEssenceGroup(In& in, const QString& cls) {
    writeTrackGroup(in, cls);
    Out& o = in.o;
    part(o, 0x01);
    if (in.has("rep_set_type")) {
        o.ext(0x01);
        o.tag(71);
        in.s32("rep_set_type");
    }
    o.tag(0x03);
}

// TransitionEffect (TNFX): 02 01 cut point, 02 05 the effect info; ext 01:
// 72 the tracker manager.
void writeTransition(In& in, const QString& cls) {
    writeTrackGroup(in, cls);
    Out& o = in.o;
    part(o, 0x01);
    in.s32("cutpoint");
    part(o, 0x05);
    writeEffectInfo(in);
    if (in.has("trackman")) {
        o.ext(0x01);
        o.tag(72);
        in.reference("trackman");
    }
    o.tag(0x03);
}

// TimeWarp: 02 02, the phase offset.
void writeTimeWarp(In& in, const QString& cls) {
    writeTrackGroup(in, cls);
    part(in.o, 0x02);
    in.s32("phase_offset");
}

// CaptureMask (MASK): 02 01, is double, mask bits.
void writeCaptureMask(In& in, const QString& cls) {
    writeTimeWarp(in, cls);
    part(in.o, 0x01);
    in.flag("is_double");
    in.u32("mask_bits");
    in.o.tag(0x03);
}

// MotionEffect (SPED): 02 03, the rate; ext 01: 75 offset adjust; 02: 72
// source parameter list; 03: 66 new calculation.
void writeMotionEffect(In& in, const QString& cls) {
    writeTimeWarp(in, cls);
    Out& o = in.o;
    part(o, 0x03);
    const auto [num, den] = in.pair("speed_ratio");
    o.s32(num, "speed_ratio");
    o.s32(den, "speed_ratio");
    if (in.has("offset_adjust")) {
        o.ext(0x01);
        o.tag(75);
        in.f64("offset_adjust");
    }
    if (in.has("source_param_list")) {
        o.ext(0x02);
        o.tag(72);
        in.reference("source_param_list");
    }
    if (in.has("new_source_calculation")) {
        o.ext(0x03);
        o.tag(66);
        in.flag("new_source_calculation");
    }
    o.tag(0x03);
}

// Composition (CMPO): 02 02, the old MobID, last modified, mob type, usage
// code, descriptor; ext 01: 71 creation time; 02: the MobID.
void writeComposition(In& in, const QString& cls) {
    writeTrackGroup(in, cls);
    Out& o = in.o;
    part(o, 0x02);
    const auto mob = in.mobId("mob_id");
    writeOldMobId(o, mob);
    in.time("last_modified");
    in.u8("mob_type_id");
    in.s32("usage_code");
    in.reference("descriptor");
    if (in.has("creation_time")) {
        o.ext(0x01);
        o.tag(71);
        in.time("creation_time");
    }
    if (mob) {
        o.ext(0x02);
        writeMobId(o, *mob);
    }
    o.tag(0x03);
}

// MediaDescriptor (MDES): 02 03, mob kind, locator, intermediate, physical
// media; ext 01: 65 s32(16) UUID; 02: 65 UTF-16 bytes; 03: 72 attributes.
void writeMediaDescriptor(In& in, const QString& cls) {
    Out& o = in.o;
    part(o, 0x03);
    in.u8("mob_kind");
    in.reference("locator");
    in.flag("intermediate");
    in.reference("physical_media");
    if (in.has("uuid")) {
        o.ext(0x01);
        o.tag(65);
        o.s32(16);
        o.rawUuid(in.uuid("uuid"));
    }
    if (in.has("wchar")) {
        const QByteArray w = in.bytes("wchar");
        o.ext(0x02);
        o.tag(65);
        o.s32(w.size(), "wchar");
        o.raw(w);
    }
    if (in.has("attributes")) {
        o.ext(0x03);
        o.tag(72);
        in.reference("attributes");
    }
    if (cls == QLatin1String("MDES")) o.tag(0x03);
}

// TapeDescriptor (MDTP): the MDES part, then 02 02, s16 cframe.
void writeTapeDescriptor(In& in, const QString& cls) {
    writeMediaDescriptor(in, cls);
    part(in.o, 0x02);
    in.s16("cframe");
    in.o.tag(0x03);
}

// MediaFileDescriptor (MDFL): 02 03, edit rate, length, is OMFI, data offset.
void writeMediaFileDescriptor(In& in, const QString& cls) {
    writeMediaDescriptor(in, cls);
    part(in.o, 0x03);
    in.exp10("edit_rate");
    in.s32("length");
    in.s16("is_omfi");
    in.s32("data_offset");
    if (cls == QLatin1String("MDFL")) in.o.tag(0x03);
}

// PCMADescriptor (PCMA): 02 01, the audio format; ext 01: 77 EBU time
// stamp; 03: 76 the timecode rate.
void writePCMA(In& in, const QString& cls) {
    writeMediaFileDescriptor(in, cls);
    Out& o = in.o;
    part(o, 0x01);
    in.u16("channels");
    in.u16("quantization_bits");
    in.exp10("sample_rate");
    in.flag("locked");
    in.s16("audio_ref_level");
    in.s32("electro_spatial_formulation");
    in.u16("dial_norm");
    in.u32("coding_format");
    in.u32("block_align");
    in.u16("sequence_offset");
    in.u32("average_bps");
    in.flag("has_peak_envelope_data");
    in.s32("peak_envelope_version");
    in.s32("peak_envelope_format");
    in.s32("points_per_peak_value");
    in.s32("peak_envelope_block_size");
    in.s32("peak_channel_count");
    in.s32("peak_frame_count");
    in.u64("peak_of_peaks_offset");
    in.s32("peak_envelope_timestamp");
    if (in.has("ebu_timestamp")) {
        o.ext(0x01);
        o.tag(77);
        in.s64("ebu_timestamp");
    }
    if (in.has("timecode_framerate")) {
        o.ext(0x03);
        o.tag(76);
        in.str("timecode_framerate");
    }
    o.tag(0x03);
}

// Four (x, y) points, each a 71 x and a 71 y (DIDD's boxes).
void writeBox(In& in, const char* name) {
    const ValueList& points = in.list(name);
    if (points.size() != 4) fail(QStringLiteral("%1: %2 points, not 4").arg(nameOf(name)).arg(points.size()));
    for (const Value& p : points) {
        const auto [x, y] = In::pairOf(&p, name);
        in.o.tag(71);
        in.o.s32(x, name);
        in.o.tag(71);
        in.o.s32(y, name);
    }
}

// DIDDescriptor (DIDD): 02 02, the picture geometry and the rest; the
// extensions in order, each when its properties are set.
void writeDIDD(In& in, const QString& cls) {
    writeMediaFileDescriptor(in, cls);
    Out& o = in.o;
    part(o, 0x02);
    for (const char* name : {"stored_height", "stored_width", "sampled_height", "sampled_width", "sampled_x_offset",
                             "sampled_y_offset", "display_height", "display_width", "display_x_offset",
                             "display_y_offset"}) {
        in.s32(name);
    }
    in.s16("frame_layout");
    const auto [num, den] = in.pair("aspect_ratio");
    o.s32(num, "aspect_ratio");
    o.s32(den, "aspect_ratio");
    const ValueList& lineMap = in.list("line_map");
    o.s32(static_cast<qint64>(lineMap.size()) * 4, "line_map");
    for (const Value& v : lineMap) o.s32(In::integerOf(&v, "line_map"), "line_map");
    in.s32("alpha_transparency");
    in.flag("uniformness");
    in.s32("did_image_size");
    in.reference("next_did_desc");
    QByteArray compress = in.bytes("compress_method");
    if (!in.has("compress_method")) compress = QByteArray(4, '\0');
    o.fourcc(compress, "compress_method");
    in.s32("resolution_id");
    in.s32("image_alignment_factor");
    auto int71 = [&](const char* name) {
        o.tag(71);
        in.s32(name);
    };
    auto uuid80 = [&](const char* name) {
        o.tag(80);
        o.rawUuid(in.uuid(name));
    };
    if (in.has("frame_index_byte_order")) {
        o.ext(1);
        o.tag(69);
        in.s16("frame_index_byte_order");
    }
    if (in.has("frame_sample_size")) {
        o.ext(2);
        int71("frame_sample_size");
    }
    if (in.has("first_frame_offset")) {
        o.ext(3);
        int71("first_frame_offset");
    }
    if (in.has("client_fill_start")) {
        o.ext(4);
        int71("client_fill_start");
        int71("client_fill_end");
    }
    if (in.has("offset_to_rle_frame_index")) {
        o.ext(5);
        int71("offset_to_rle_frame_index");
    }
    if (in.has("frame_start_offset")) {
        o.ext(6);
        int71("frame_start_offset");
    }
    if (in.has("valid_box") && in.has("essence_box") && in.has("source_box")) {
        o.ext(8);
        writeBox(in, "valid_box");
        writeBox(in, "essence_box");
        writeBox(in, "source_box");
    }
    if (in.has("framing_box") && in.has("reformatting_option")) {
        o.ext(9);
        writeBox(in, "framing_box");
        int71("reformatting_option");
    }
    if (in.has("transfer_characteristic")) {
        o.ext(10);
        uuid80("transfer_characteristic");
    }
    if (in.has("color_primaries") && in.has("coding_equations")) {
        o.ext(11);
        uuid80("color_primaries");
        uuid80("coding_equations");
    }
    if (in.has("essence_compression")) {
        o.ext(12);
        uuid80("essence_compression");
    }
    if (in.has("essence_element_size_kind")) {
        o.ext(14);
        o.tag(68);
        in.u8("essence_element_size_kind");
    }
    if (in.has("frame_checked_with_mapper")) {
        o.ext(15);
        o.tag(66);
        in.flag("frame_checked_with_mapper");
    }
    if (cls == QLatin1String("DIDD")) o.tag(0x03);
}

// CDCIDescriptor (CDCI): 02 02, subsampling, width, siting, levels, frame
// index offset; ext 01, 02: 72.
void writeCDCI(In& in, const QString& cls) {
    writeDIDD(in, cls);
    Out& o = in.o;
    part(o, 0x02);
    in.u32("horizontal_subsampling");
    in.u32("vertical_subsampling");
    in.s32("component_width");
    in.s16("color_sitting");
    in.u32("black_ref_level");
    in.u32("white_ref_level");
    in.u32("color_range");
    in.s64("frame_index_offset");
    if (in.has("alpha_sampled_width")) {
        o.ext(0x01);
        o.tag(72);
        in.u32("alpha_sampled_width");
    }
    if (in.has("ignore_bw")) {
        o.ext(0x02);
        o.tag(72);
        in.u32("ignore_bw");
    }
    if (cls == QLatin1String("CDCI")) o.tag(0x03);
}

// RGBADescriptor (RGBA): 02 01, the pixel layout (codes, then sizes), an
// empty palette; ext 01: 77 frame index offset; 02, 03: reference levels.
void writeRGBA(In& in, const QString& cls) {
    writeDIDD(in, cls);
    Out& o = in.o;
    part(o, 0x01);
    QByteArray codes;
    QByteArray sizes;
    for (const Value& v : in.list("pixel_layout")) {
        In e(o, In::recordOf(v, "pixel_layout"));
        const qint64 code = e.integer("Code");
        const qint64 size = e.integer("Size");
        if (code < 0 || code > 0xFF || size < 0 || size > 0xFF) fail(QStringLiteral("pixel_layout: out of range"));
        codes.append(static_cast<char>(code));
        sizes.append(static_cast<char>(size));
    }
    o.u32(codes.size());
    o.raw(codes);
    o.u32(sizes.size());
    o.raw(sizes);
    o.zeros(12); // palette layout, structure and palette: all empty
    if (in.has("frame_index_offset")) {
        o.ext(0x01);
        o.tag(77);
        in.u64("frame_index_offset");
    }
    if (in.has("has_comp_min_ref")) {
        o.ext(0x02);
        o.tag(66);
        in.flag("has_comp_min_ref");
        o.tag(72);
        in.u32("comp_min_ref");
        o.tag(66);
        in.flag("has_comp_max_ref");
        o.tag(72);
        in.u32("comp_max_ref");
    }
    if (in.has("alpha_min_ref")) {
        o.ext(0x03);
        o.tag(72);
        in.u32("alpha_min_ref");
        o.tag(72);
        in.u32("alpha_max_ref");
    }
    o.tag(0x03);
}

// FileLocator (FILE, WINF): 02 02, the path; ext 01: 76 POSIX path; 02, 03:
// 76 UTF-8 paths.
void writeFileLocator(In& in, const QString&) {
    Out& o = in.o;
    part(o, 0x02);
    in.str("path");
    if (in.has("path_posix")) {
        o.ext(0x01);
        o.tag(76);
        in.str("path_posix");
    }
    if (in.has("path_utf8")) {
        o.ext(0x02);
        o.tag(76);
        in.str("path_utf8", true);
    }
    if (in.has("path2_utf8")) {
        o.ext(0x03);
        o.tag(76);
        in.str("path2_utf8", true);
    }
    o.tag(0x03);
}

// URLLocator (URLL): only the end tag.
void writeURLLocator(In& in, const QString&) { in.o.tag(0x03); }

// GraphicEffect (GRFX): 02 01, s32 size, the picture data.
void writeGraphicEffect(In& in, const QString&) {
    Out& o = in.o;
    part(o, 0x01);
    const QByteArray pict = in.bytes("pict_data");
    o.s32(pict.size(), "pict_data");
    o.raw(pict);
    o.tag(0x03);
}

// EffectParamList (FXPS): 02 12, original length, window offset, count, key
// frame size, then each key frame's fixed parameters.
void writeEffectParamList(In& in, const QString&) {
    Out& o = in.o;
    part(o, 0x12);
    in.s32("orig_length");
    in.s32("window_offset");
    const ValueList& params = in.list("parameters");
    o.s32(static_cast<qint64>(params.size()), "parameters");
    in.s32("keyframe_size");
    for (const Value& v : params) {
        In e(o, In::recordOf(v, "parameters"));
        for (const char* name : {"percent_time", "level", "pos_x", "floor_x", "ceil_x", "pos_y", "floor_y", "ceil_y",
                                 "scale_x", "scale_y", "crop_left", "crop_right", "crop_top", "crop_bottom"}) {
            e.s32(name);
        }
        const ValueList& box = e.list("box");
        if (!box.empty() && box.size() != 4) fail(QStringLiteral("box: %1 values, not 4").arg(box.size()));
        for (std::size_t j = 0; j < 4; ++j) o.s32(j < box.size() ? In::integerOf(&box[j], "box") : 0, "box");
        for (const char* name : {"box_xscale", "box_yscale", "box_xpos", "box_ypos"}) e.flag(name);
        e.s32("border_width");
        e.s32("border_soft");
        for (const char* name : {"splill_gain2", "splill_gain", "splill_soft2", "splill_soft"}) e.s16(name);
        e.s8("enable_key_flags");
        const ValueList& colors = e.list("colors");
        o.s32(static_cast<qint64>(colors.size()), "colors");
        for (const Value& c : colors) o.s32(In::integerOf(&c, "colors"), "colors");
        const QByteArray user = e.bytes("user_param");
        o.s32(user.size(), "user_param");
        o.raw(user);
        e.flag("selected");
    }
    o.tag(0x03);
}

// ParameterItem (PRIT): 02 02, UUID, value type and value, name, enabled,
// control track; ext 01: 66 contributes to signature.
void writeParameterItem(In& in, const QString&) {
    Out& o = in.o;
    part(o, 0x02);
    o.rawUuid(in.uuid("uuid"));
    const qint64 type = in.integer("value_type");
    o.s16(type, "value_type");
    if (type == 1) {
        in.s32("value");
    } else if (type == 2) {
        in.f64("value");
    } else if (type == 4) {
        in.reference("value");
    } else {
        fail(QStringLiteral("value_type: unknown type %1").arg(type));
    }
    o.optionalString(in.string("name"), "name");
    in.flag("enable");
    in.reference("control_track");
    if (in.has("contribs_to_sig")) {
        o.ext(0x01);
        o.tag(66);
        in.flag("contribs_to_sig");
    }
    o.tag(0x03);
}

// MSMLocator (MSML): 02 02, the old MobID, last known volume; ext 01: 71
// domain type; 02: the MobID; 03: 76 the volume in UTF-8.
void writeMSMLocator(In& in, const QString&) {
    Out& o = in.o;
    part(o, 0x02);
    const auto mob = in.mobId("mob_id");
    writeOldMobId(o, mob);
    in.str("last_known_volume");
    if (in.has("domain_type")) {
        o.ext(0x01);
        o.tag(71);
        in.s32("domain_type");
    }
    if (mob) {
        o.ext(0x02);
        writeMobId(o, *mob);
    }
    if (in.has("last_known_volume_utf8")) {
        o.ext(0x03);
        o.tag(76);
        in.str("last_known_volume_utf8", true);
    }
    o.tag(0x03);
}

// BinRef (MCBR): 02 01, the bin's ID and name; ext 01: 76 the name in UTF-8
// (pyavb always writes it; here only when set, as the reader reads it).
void writeBinRef(In& in, const QString&) {
    Out& o = in.o;
    part(o, 0x01);
    in.s32("uid_high");
    in.s32("uid_low");
    in.str("name");
    if (in.has("name_utf8")) {
        o.ext(0x01);
        o.tag(76);
        in.str("name_utf8", true);
    }
    o.tag(0x03);
}

// Attributes (ATTR): 02 01, u32 count, each attribute's type (by its value:
// 1 an integer, 2 a string, 3 a reference, 4 bytes), name and value.
void writeAttributes(In& in, const QString&, const Properties& attributes) {
    Out& o = in.o;
    part(o, 0x01);
    o.u32(static_cast<qint64>(attributes.size()));
    for (const Property& a : attributes) {
        const Value::Data& d = a.value.data();
        const QByteArray name = a.name.toLatin1();
        if (std::holds_alternative<qint64>(d) || std::holds_alternative<bool>(d)) {
            o.u32(1);
            o.string(a.name, "attribute name");
            o.s32(In::integerOf(&a.value, name.constData()), name.constData());
        } else if (const auto* s = std::get_if<QString>(&d)) {
            o.u32(2);
            o.string(a.name, "attribute name");
            o.string(*s, name.constData());
        } else if (const auto* r = std::get_if<Ref>(&d)) {
            o.u32(3);
            o.string(a.name, "attribute name");
            o.ref(r->index, name.constData());
        } else if (const auto* b = std::get_if<QByteArray>(&d)) {
            o.u32(4);
            o.string(a.name, "attribute name");
            o.u32(b->size());
            o.raw(*b);
        } else {
            fail(QStringLiteral("attribute %1: not an integer, string, reference or bytes").arg(a.name));
        }
    }
    o.tag(0x03);
}

// ParameterList (PRLS): 02 01, s32 count, the references.
void writeParameterList(Out& o, const ValueList& items) {
    part(o, 0x01);
    o.s32(static_cast<qint64>(items.size()), "items");
    for (const Value& v : items) o.ref(In::refOf(&v, "items"), "items");
    o.tag(0x03);
}

// The bin view setting (BVst): Setting's 02 06 (name, kind, attribute count
// and type, attributes), then 02 0a, the columns; ext 01: 69 count, then
// each 69 column ID, 71 size, 76 s16(size + 2) s16(0), the UTF-8 format
// descriptor.
void writeBinViewSetting(In& in, const QString&) {
    Out& o = in.o;
    part(o, 0x06);
    in.str("name");
    in.str("kind");
    in.s16("attr_count");
    in.s16("attr_type");
    in.reference("attributes");
    part(o, 10);
    const ValueList& columns = in.list("columns");
    o.u16(static_cast<qint64>(columns.size()), "columns");
    for (const Value& v : columns) {
        In c(o, In::recordOf(v, "columns"));
        c.str("title");
        c.s16("format");
        c.s16("type");
        c.flag("hidden");
    }
    if (in.has("format_descriptors")) {
        const ValueList& list = in.list("format_descriptors");
        o.ext(0x01);
        o.tag(69);
        o.u16(static_cast<qint64>(list.size()), "format_descriptors");
        for (const Value& v : list) {
            In d(o, In::recordOf(v, "format_descriptors"));
            o.tag(69);
            d.s16("vcid_free_column_id");
            const QByteArray text = d.string("format_descriptor").toUtf8();
            o.tag(71);
            o.s32(text.size(), "format_descriptor");
            o.tag(76);
            o.s16(text.size() + 2, "format_descriptor");
            o.s16(0);
            o.raw(text);
        }
    }
    o.tag(0x03);
}

// Bin (ABIN): 02, 0e (u16 item count) or 0f (u32, a large bin, also when
// the items do not fit 16 bits), the view, ID, items, display, sifting,
// sorting, font, rect, colours, QuickLook scale, attributes, was iconic.
void writeBin(In& in, const QString& cls) {
    Out& o = in.o;
    const ValueList& items = in.list("items");
    const bool large = in.boolean("large_bin") || items.size() > 0xFFFF;
    o.tag(0x02);
    o.tag(large ? 0x0f : 0x0e);
    in.reference("view_setting");
    in.u64("uid");
    if (large) {
        o.u32(static_cast<qint64>(items.size()), "items");
    } else {
        o.u16(static_cast<qint64>(items.size()), "items");
    }
    for (const Value& v : items) {
        In e(o, In::recordOf(v, "items"));
        e.reference("mob");
        e.s16("x");
        e.s16("y");
        e.s32("keyframe");
        e.flag("user_placed");
    }
    in.s32("display_mask");
    in.s16("display_mode");
    in.flag("sifted");
    const ValueList& sifted = in.list("sifted_settings");
    if (sifted.size() > 6) fail(QStringLiteral("sifted_settings: %1 settings, not 6").arg(sifted.size()));
    for (std::size_t i = 0; i < 6; ++i) {
        static const Properties none;
        In s(o, i < sifted.size() ? In::recordOf(sifted[i], "sifted_settings") : none);
        s.s16("method");
        s.str("string");
        s.str("column");
    }
    const ValueList& sort = in.list("sort_columns");
    o.s16(static_cast<qint64>(sort.size()), "sort_columns");
    for (const Value& v : sort) {
        const ValueList& c = In::listOf(&v, "sort_columns");
        if (c.size() != 2) fail(QStringLiteral("sort_columns: not a (direction, column) pair"));
        o.u8(In::integerOf(&c[0], "sort_columns"), "sort_columns");
        o.string(c[1].toString(), "sort_columns");
    }
    in.s16("mac_font");
    in.s16("mac_font_size");
    in.s16("mac_image_scale");
    auto values = [&](const char* name, std::size_t n, bool unsignedValues) {
        const ValueList& l = in.list(name);
        if (!l.empty() && l.size() != n) fail(QStringLiteral("%1: %2 values, not %3").arg(nameOf(name)).arg(l.size()).arg(n));
        o.s16(1); // the version
        for (std::size_t i = 0; i < n; ++i) {
            const qint64 v = i < l.size() ? In::integerOf(&l[i], name) : 0;
            if (unsignedValues) {
                o.u16(v, name);
            } else {
                o.s16(v, name);
            }
        }
    };
    values("home_rect", 4, false);
    values("background_color", 3, true);
    values("forground_color", 3, true);
    in.s16("ql_image_scale");
    in.reference("attributes");
    in.flag("was_iconic");
    if (cls == QLatin1String("ABIN")) o.tag(0x03);
}

using WriteFn = void (*)(In&, const QString&);

struct ClassEntry {
    const char* id;
    WriteFn write;
};

// The classes written: those avb.cpp reads (its kClasses), but ATTR and
// PRLS, which encodeObject() writes from the object's properties and items.
constexpr ClassEntry kClasses[] = {
    {"ABIN", writeBin},
    {"BVst", writeBinViewSetting},
    {"SCLP", writeSourceClip},
    {"TCCP", writeTimecode},
    {"ECCP", writeEdgecode},
    {"TRKR", writeTrackRef},
    {"FILL", writeFiller},
    {"PRCL", writeParamClip},
    {"CTRL", writeControlClip},
    {"SEQU", writeSequence},
    {"TRKG", writeTrackGroup},
    {"TKFX", writeTrackEffect},
    {"PVOL", writePanVolume},
    {"RSET", writeEssenceGroup},
    {"TNFX", writeTransition},
    {"MASK", writeCaptureMask},
    {"SPED", writeMotionEffect},
    {"CMPO", writeComposition},
    {"MDES", writeMediaDescriptor},
    {"MDTP", writeTapeDescriptor},
    {"MDFL", writeMediaFileDescriptor},
    {"PCMA", writePCMA},
    {"DIDD", writeDIDD},
    {"CDCI", writeCDCI},
    {"RGBA", writeRGBA},
    {"FILE", writeFileLocator},
    {"WINF", writeFileLocator},
    {"URLL", writeURLLocator},
    {"GRFX", writeGraphicEffect},
    {"FXPS", writeEffectParamList},
    {"PRIT", writeParameterItem},
    {"MSML", writeMSMLocator},
    {"MCBR", writeBinRef},
};

WriteFn writerFor(const QString& cls) {
    for (const ClassEntry& c : kClasses) {
        if (cls == QLatin1String(c.id)) return c.write;
    }
    return nullptr;
}

// The class ID as its four bytes, for the table.
std::optional<QByteArray> classBytes(const QString& classId) {
    const QByteArray b = classId.toLatin1();
    if (b.size() != 4 || classId.size() != 4) return std::nullopt;
    return b;
}

} // namespace

// ---- Objects --------------------------------------------------------------------

QByteArray encodeObject(const Object& object, bool littleEndian, std::uint32_t objectCount, QString* error) {
    Out o(littleEndian, objectCount);
    try {
        In in(o, object.properties);
        if (!object.ok()) fail(QStringLiteral("an object that was not read: %1").arg(object.error));
        if (object.classId == QLatin1String("PRLS")) {
            writeParameterList(o, object.items);
        } else if (object.classId == QLatin1String("ATTR")) {
            writeAttributes(in, object.classId, object.properties);
        } else if (const WriteFn write = writerFor(object.classId)) {
            write(in, object.classId);
        } else {
            fail(QStringLiteral("class not written"));
        }
    } catch (const WriteError& e) {
        if (error) *error = QStringLiteral("%1: %2").arg(object.classId, e.what);
        return {};
    }
    return o.data;
}

// ---- Builder --------------------------------------------------------------------

Builder::Builder(Header header) : header_(std::move(header)) {}

std::uint32_t Builder::add(const QString& classId, Properties properties) {
    Entry e;
    e.object.classId = classId;
    e.object.properties = std::move(properties);
    entries_.push_back(std::move(e));
    entries_.back().object.index = count();
    return count();
}

std::uint32_t Builder::addList(ValueList items) {
    Entry e;
    e.object.classId = QStringLiteral("PRLS");
    e.object.isList = true;
    e.object.items = std::move(items);
    entries_.push_back(std::move(e));
    entries_.back().object.index = count();
    return count();
}

std::uint32_t Builder::add(const Object& object) {
    entries_.push_back({object, std::nullopt});
    entries_.back().object.index = count();
    return count();
}

std::uint32_t Builder::addOpaque(const QString& classId, QByteArray payload) {
    Entry e;
    e.object.classId = classId;
    e.opaque = std::move(payload);
    entries_.push_back(std::move(e));
    entries_.back().object.index = count();
    return count();
}

Object& Builder::object(std::uint32_t index) {
    assert(index >= 1 && index <= count());
    return entries_[index - 1].object;
}

// The header (pyavb's write_header), the inverse of File::load: byte order,
// "Domain", OBJD, "AObjDoc", 04, the last save text, object count, root,
// II or MM, the last save time, 4 bytes, ATob, ATve, the creator in a
// 30-byte field padded with spaces, 16 reserved bytes; then the table.
QByteArray Builder::toBytes(QString* error) const {
    auto refuse = [&](const QString& why) {
        if (error) *error = why;
        return QByteArray();
    };
    if (entries_.empty()) return refuse(QStringLiteral("no objects"));
    const std::uint32_t rootIndex = root();
    if (rootIndex == 0 || rootIndex > count()) return refuse(QStringLiteral("root index %1 is out of range").arg(rootIndex));
    const bool little = header_.littleEndian;
    Out o(little, count());
    try {
        o.raw(little ? QByteArray("\x06\x00", 2) : QByteArray("\x00\x06", 2));
        o.raw("Domain");
        o.fourcc("OBJD");
        o.string(QStringLiteral("AObjDoc"), "header");
        o.u8(0x04);
        QString saved = header_.lastSaveText;
        if (saved.isEmpty()) {
            saved = QDateTime::fromSecsSinceEpoch(header_.lastSave.seconds).toString(QStringLiteral("yyyy/MM/dd HH:mm:ss"));
        }
        o.string(saved, "last save");
        o.u32(count());
        o.u32(rootIndex);
        o.u32(little ? 0x49494949 : 0x4D4D4D4D);
        o.u32(header_.lastSave.seconds);
        if (!header_.unknown.isEmpty() && header_.unknown.size() != 4) fail(QStringLiteral("header: 4 unknown bytes wanted"));
        o.raw(header_.unknown.isEmpty() ? QByteArray(4, '\0') : header_.unknown);
        o.fourcc("ATob");
        o.fourcc("ATve");
        const auto creator = toMacRoman(header_.creatorVersion);
        if (!creator) fail(QStringLiteral("creator: \"%1\" has characters Mac OS Roman cannot hold").arg(header_.creatorVersion));
        QByteArray field = creator->left(30);
        field.append(QByteArray(30 - field.size(), ' '));
        o.u16(30);
        o.raw(field);
        if (!header_.reserved.isEmpty() && header_.reserved.size() != 16) fail(QStringLiteral("header: 16 reserved bytes wanted"));
        o.raw(header_.reserved.isEmpty() ? QByteArray(16, '\0') : header_.reserved);
    } catch (const WriteError& e) {
        return refuse(QStringLiteral("header: %1").arg(e.what));
    }
    QByteArray out = std::move(o.data);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        const auto index = static_cast<std::uint32_t>(i + 1);
        const auto id = classBytes(e.object.classId);
        if (!id) return refuse(QStringLiteral("object %1: class ID \"%2\" is not 4 characters").arg(index).arg(e.object.classId));
        QByteArray payload;
        if (e.opaque) {
            payload = *e.opaque;
        } else {
            QString why;
            payload = encodeObject(e.object, little, count(), &why);
            if (payload.isEmpty()) return refuse(QStringLiteral("object %1 (%2)").arg(index).arg(why));
        }
        Out chunk(little, count());
        chunk.fourcc(*id);
        if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
            return refuse(QStringLiteral("object %1: payload too large").arg(index));
        }
        chunk.u32(payload.size());
        out.append(chunk.data);
        out.append(payload);
    }
    return out;
}

bool Builder::save(const QString& path, QString* error) const {
    const QByteArray bytes = toBytes(error);
    if (bytes.isEmpty()) return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

// ---- Rewriting ------------------------------------------------------------------

QByteArray rewrite(const File& file, QString* error, std::optional<bool> littleEndian) {
    auto refuse = [&](const QString& why) {
        if (error) *error = why;
        return QByteArray();
    };
    if (!file.isOpen()) return refuse(QStringLiteral("no file"));
    Header header = file.header();
    const bool sameOrder = !littleEndian || *littleEndian == file.littleEndian();
    if (littleEndian) header.littleEndian = *littleEndian;
    Builder b(header);
    for (std::uint32_t i = 1; i <= file.objectCount(); ++i) {
        const Object& o = file.object(i);
        if (o.ok()) {
            b.add(o);
            continue;
        }
        // Not read (a class not read yet, or damage): copied as stored.
        if (!sameOrder) return refuse(QStringLiteral("object %1 (%2) was not read, so it cannot change byte order").arg(i).arg(o.error));
        const QByteArray payload = file.payload(i);
        if (payload.isEmpty()) return refuse(QStringLiteral("object %1 (%2) is cut short").arg(i).arg(o.error));
        b.addOpaque(file.classIdAt(i), payload);
    }
    b.setRoot(file.rootIndex());
    return b.toBytes(error);
}

} // namespace lumenis::interop::avb
