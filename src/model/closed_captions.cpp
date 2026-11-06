// Closed captions on the Data track (lumenis/model/closed_captions.h; docs/spec/closed-captions.md). The decoder
// follows CEA-608's field 1 data channel 1 (CC1): Pop-on (RCL ... EOC), Roll-up (RU2-RU4 and CR) and Paint-on (RDC)
// captions on a grid of 15 rows of 32 columns, with the basic, special and extended character sets.
#include "lumenis/model/closed_captions.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace lumenis::model {

namespace {

constexpr int kRows = 15;
constexpr int kColumns = 32;

// Byte at `i` (two per frame) without its parity bit.
int stripped(const QByteArray& data, qsizetype i) {
    return static_cast<unsigned char>(data.at(i)) & 0x7f;
}

bool isNullWord(const QByteArray& data, std::int64_t frame) {
    const qsizetype i = static_cast<qsizetype>(frame) * 2;
    if (frame < 0 || i + 1 >= data.size()) return true;
    return stripped(data, i) == 0 && stripped(data, i + 1) == 0;
}

// The basic character set: ASCII but for nine characters (CEA-608 Table 50).
QChar basicChar(int c) {
    switch (c) {
    case 0x2a: return QChar(0x00e1); // á
    case 0x5c: return QChar(0x00e9); // é
    case 0x5e: return QChar(0x00ed); // í
    case 0x5f: return QChar(0x00f3); // ó
    case 0x60: return QChar(0x00fa); // ú
    case 0x7b: return QChar(0x00e7); // ç
    case 0x7c: return QChar(0x00f7); // ÷
    case 0x7d: return QChar(0x00d1); // Ñ
    case 0x7e: return QChar(0x00f1); // ñ
    case 0x7f: return QChar(0x2588); // solid block
    default: return QChar(c);
    }
}

// Special characters: 0x11 (0x19 on channel 2) then 0x30-0x3F. 0x39 is the transparent space.
const std::array<char16_t, 16> kSpecial{0x00ae, 0x00b0, 0x00bd, 0x00bf, 0x2122, 0x00a2, 0x00a3, 0x266a,
                                        0x00e0, 0x0020, 0x00e8, 0x00e2, 0x00ea, 0x00ee, 0x00f4, 0x00fb};
// Extended characters: 0x12 and 0x13 then 0x20-0x3F; each replaces the character sent before it.
const std::array<char16_t, 32> kExtended12{0x00c1, 0x00c9, 0x00d3, 0x00da, 0x00dc, 0x00fc, 0x2018, 0x00a1,
                                          0x002a, 0x0027, 0x2014, 0x00a9, 0x2120, 0x2022, 0x201c, 0x201d,
                                          0x00c0, 0x00c2, 0x00c7, 0x00c8, 0x00ca, 0x00cb, 0x00eb, 0x00ce,
                                          0x00cf, 0x00ef, 0x00d4, 0x00d9, 0x00f9, 0x00db, 0x00ab, 0x00bb};
const std::array<char16_t, 32> kExtended13{0x00c3, 0x00e3, 0x00cd, 0x00cc, 0x00ec, 0x00d2, 0x00f2, 0x00d5,
                                          0x00f5, 0x007b, 0x007d, 0x005c, 0x005e, 0x005f, 0x007c, 0x007e,
                                          0x00c4, 0x00e4, 0x00d6, 0x00f6, 0x00df, 0x00a5, 0x00a4, 0x00a6,
                                          0x00c5, 0x00e5, 0x00d8, 0x00f8, 0x250c, 0x2510, 0x2514, 0x2518};

// A Preamble Address Code's row: c1's low three bits and c2's 0x20 bit (CEA-608 Table 53).
int pacRow(int c1, int c2) {
    static const std::array<std::pair<int, int>, 8> rows{
        std::pair{11, 11}, std::pair{1, 2}, std::pair{3, 4}, std::pair{12, 13},
        std::pair{14, 15}, std::pair{5, 6}, std::pair{7, 8}, std::pair{9, 10}};
    const auto& r = rows[static_cast<std::size_t>(c1 & 0x07)];
    return (c2 & 0x20) ? r.second : r.first;
}

using Grid = std::array<std::array<QChar, kColumns>, kRows>;

void clear(Grid& g) {
    for (auto& row : g) row.fill(QChar());
}

CaptionScreen screenOf(const Grid& g) {
    CaptionScreen s;
    for (int r = 0; r < kRows; ++r) {
        const auto& row = g[static_cast<std::size_t>(r)];
        int first = -1;
        int last = -1;
        for (int c = 0; c < kColumns; ++c) {
            if (!row[static_cast<std::size_t>(c)].isNull()) {
                if (first < 0) first = c;
                last = c;
            }
        }
        if (first < 0) continue;
        QString text;
        for (int c = first; c <= last; ++c) {
            const QChar ch = row[static_cast<std::size_t>(c)];
            text += ch.isNull() ? QChar(u' ') : ch;
        }
        // A row of spaces shows nothing.
        if (text.trimmed().isEmpty()) continue;
        s.rows << CaptionRow{r + 1, first, text};
    }
    return s;
}

// The CC1 decoder's state.
class Decoder {
public:
    Decoder() {
        clear(shown_);
        clear(hidden_);
    }

    // One frame's byte pair.
    void word(int c1, int c2) {
        if (c1 == 0 && c2 == 0) {
            lastControl_ = -1;
            return;
        }
        if (c1 >= 0x10 && c1 <= 0x1f) {
            // A control code: sent twice, "the second ... is ignored".
            const int code = (c1 << 8) | c2;
            if (code == lastControl_) {
                lastControl_ = -1;
                return;
            }
            lastControl_ = code;
            channel1_ = (c1 & 0x08) == 0;
            if (channel1_) control(c1 & 0x17, c2);
            return;
        }
        lastControl_ = -1;
        if (c1 < 0x10) return; // XDS and the like, on field 2
        if (!channel1_) return;
        put(basicChar(c1));
        if (c2 >= 0x20) put(basicChar(c2));
    }

    const Grid& shown() const { return shown_; }

private:
    enum class Mode { None, PopOn, RollUp, PaintOn };

    Grid& target() { return mode_ == Mode::PopOn || mode_ == Mode::None ? hidden_ : shown_; }

    void put(QChar ch) {
        Grid& g = target();
        g[static_cast<std::size_t>(row_)][static_cast<std::size_t>(column_)] = ch;
        if (column_ < kColumns - 1) ++column_;
    }

    void control(int c1, int c2) {
        if (c2 >= 0x40 && c2 <= 0x7f) {
            // A Preamble Address Code: the row and the indent.
            const int row = pacRow(c1, c2) - 1;
            if (mode_ == Mode::RollUp && row != row_) {
                // The roll-up window moves to the new base row (ours: its rows move with it).
                Grid moved;
                clear(moved);
                for (int i = 0; i < rollRows_; ++i) {
                    const int from = row_ - i;
                    const int to = row - i;
                    if (from >= 0 && to >= 0) moved[static_cast<std::size_t>(to)] = shown_[static_cast<std::size_t>(from)];
                }
                shown_ = moved;
            }
            row_ = row;
            const int v = c2 & 0x1f;
            column_ = v >= 0x10 ? ((v - 0x10) >> 1) * 4 : 0;
            return;
        }
        if (c1 == 0x11 && c2 >= 0x20 && c2 <= 0x2f) {
            put(QChar(u' ')); // a mid-row code shows as a space
            return;
        }
        if (c1 == 0x11 && c2 >= 0x30 && c2 <= 0x3f) {
            put(QChar(kSpecial[static_cast<std::size_t>(c2 - 0x30)]));
            return;
        }
        if ((c1 == 0x12 || c1 == 0x13) && c2 >= 0x20 && c2 <= 0x3f) {
            if (column_ > 0) --column_;
            const auto& table = c1 == 0x12 ? kExtended12 : kExtended13;
            put(QChar(table[static_cast<std::size_t>(c2 - 0x20)]));
            return;
        }
        if (c1 == 0x17 && c2 >= 0x21 && c2 <= 0x23) {
            column_ = std::min(kColumns - 1, column_ + (c2 - 0x20)); // Tab Offsets 1-3
            return;
        }
        if ((c1 == 0x14 || c1 == 0x15) && c2 >= 0x20 && c2 <= 0x2f) misc(c2);
        // Background and foreground attributes (0x10 and 0x17 0x2D-0x2F) change only colours: ours, ignored.
    }

    void misc(int c2) {
        switch (c2) {
        case 0x20: mode_ = Mode::PopOn; break; // RCL
        case 0x21: // BS
            if (column_ > 0) {
                --column_;
                target()[static_cast<std::size_t>(row_)][static_cast<std::size_t>(column_)] = QChar();
            }
            break;
        case 0x24: // DER
            for (int c = column_; c < kColumns; ++c) target()[static_cast<std::size_t>(row_)][static_cast<std::size_t>(c)] = QChar();
            break;
        case 0x25:
        case 0x26:
        case 0x27: // RU2-RU4
            if (mode_ != Mode::RollUp) {
                clear(shown_);
                clear(hidden_);
                row_ = kRows - 1;
            }
            mode_ = Mode::RollUp;
            rollRows_ = c2 - 0x23;
            column_ = 0;
            break;
        case 0x29: mode_ = Mode::PaintOn; break; // RDC
        case 0x2c: clear(shown_); break;         // EDM
        case 0x2d: // CR
            if (mode_ == Mode::RollUp) {
                const int top = std::max(0, row_ - rollRows_ + 1);
                for (int r = 0; r < kRows; ++r) {
                    if (r < top - 1 || r > row_) shown_[static_cast<std::size_t>(r)].fill(QChar());
                }
                for (int r = top; r < row_; ++r) shown_[static_cast<std::size_t>(r)] = shown_[static_cast<std::size_t>(r + 1)];
                if (top > 0) shown_[static_cast<std::size_t>(top - 1)].fill(QChar());
                shown_[static_cast<std::size_t>(row_)].fill(QChar());
            } else if (row_ < kRows - 1) {
                ++row_;
            }
            column_ = 0;
            break;
        case 0x2e: clear(hidden_); break; // ENM
        case 0x2f:                         // EOC
            std::swap(shown_, hidden_);
            mode_ = Mode::PopOn;
            break;
        default: break; // AOF, AON, FON, TR, RTD
        }
    }

    Grid shown_;
    Grid hidden_;
    Mode mode_ = Mode::None;
    int row_ = kRows - 1;
    int column_ = 0;
    int rollRows_ = 2;
    int lastControl_ = -1;
    bool channel1_ = true;
};

// The byte pair for a character: one basic byte, or a special or extended code (with the basic character a decoder
// without them shows first, for extended ones). Characters 608 does not have become '?'.
QList<std::array<int, 2>> codesFor(QChar ch, bool* basic, int* basicByte) {
    *basic = false;
    for (int c = 0x20; c <= 0x7f; ++c) {
        if (basicChar(c) == ch) {
            *basic = true;
            *basicByte = c;
            return {};
        }
    }
    for (std::size_t i = 0; i < kSpecial.size(); ++i) {
        if (i != 9 && kSpecial[i] == ch.unicode()) return {{0x11, 0x30 + static_cast<int>(i)}};
    }
    for (int table = 0x12; table <= 0x13; ++table) {
        const auto& t = table == 0x12 ? kExtended12 : kExtended13;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i] == ch.unicode()) return {{0x20, 0x00}, {table, 0x20 + static_cast<int>(i)}};
        }
    }
    *basic = true;
    *basicByte = '?';
    return {};
}

void appendWord(QByteArray& out, int c1, int c2) {
    out += withParity(c1);
    out += withParity(c2);
}

// The words that write `text` from the cursor: basic characters two to a word.
QList<QByteArray> textWords(const QString& text) {
    QList<QByteArray> words;
    int pending = -1;
    auto flush = [&] {
        if (pending < 0) return;
        QByteArray w;
        appendWord(w, pending, 0);
        words << w;
        pending = -1;
    };
    for (const QChar ch : text) {
        bool basic = false;
        int byte = 0;
        const QList<std::array<int, 2>> codes = codesFor(ch, &basic, &byte);
        if (basic) {
            if (pending < 0) {
                pending = byte;
            } else {
                QByteArray w;
                appendWord(w, pending, byte);
                words << w;
                pending = -1;
            }
            continue;
        }
        flush();
        for (const auto& code : codes) {
            QByteArray w;
            appendWord(w, code[0], code[1]);
            words << w;
        }
    }
    flush();
    return words;
}

QByteArray controlWord(int c1, int c2) {
    QByteArray w;
    appendWord(w, c1, c2);
    return w;
}

// The PAC for a row (1-15) with an indent of `indent` columns (a multiple of 4).
QByteArray pacWord(int row, int indent) {
    static const std::array<int, 16> c1Of{0, 0x11, 0x11, 0x12, 0x12, 0x15, 0x15, 0x16, 0x16, 0x17, 0x17, 0x10, 0x13, 0x13, 0x14, 0x14};
    static const std::array<bool, 16> second{false, false, true, false, true, false, true, false, true, false, true, false, false, true, false, true};
    const auto r = static_cast<std::size_t>(std::clamp(row, 1, 15));
    const int c2 = 0x50 + ((indent / 4) << 1) + (second[r] ? 0x20 : 0x00);
    return controlWord(c1Of[r], c2);
}

} // namespace

char withParity(int byte) {
    const int b = byte & 0x7f;
    return static_cast<char>(std::popcount(static_cast<unsigned>(b)) % 2 == 0 ? b | 0x80 : b);
}

QByteArray clipCaptionData(const BinItem& item) {
    const QJsonObject cc = item.composition.value(QStringLiteral("closedCaptions")).toObject();
    return QByteArray::fromBase64(cc.value(QStringLiteral("data")).toString().toLatin1());
}

bool hasCaptionData(const BinItem& item) {
    return item.kind != BinItemKind::Sequence && item.composition.contains(QStringLiteral("closedCaptions"));
}

void setClipCaptionData(BinItem& item, const QByteArray& data) {
    QJsonObject cc;
    cc.insert(QStringLiteral("data"), QString::fromLatin1(data.toBase64()));
    item.composition.insert(QStringLiteral("closedCaptions"), cc);
    item.durationFrames = data.size() / 2;
}

// ---------------------------------------------------------------- SCC

std::optional<SccFile> parseScc(const QString& text, QString* error) {
    auto fail = [&](const QString& why) -> std::optional<SccFile> {
        if (error) *error = why;
        return std::nullopt;
    };
    QStringList lines = text.split(QRegularExpression(QStringLiteral("\r\n|\r|\n")));
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty()) lines.removeFirst();
    if (lines.isEmpty() || !lines.first().trimmed().startsWith(QLatin1String("Scenarist_SCC"))) {
        return fail(QStringLiteral("not a Scenarist Closed Caption file (no Scenarist_SCC header)"));
    }
    SccFile file;
    bool sawDrop = false;
    const QRegularExpression lineRe(QStringLiteral("^(\\d\\d:\\d\\d:\\d\\d)([:;.,])(\\d\\d)\\s+(.*)$"));
    QList<std::pair<QString, QString>> raw;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QString l = lines.at(i).trimmed();
        if (l.isEmpty()) continue;
        const QRegularExpressionMatch m = lineRe.match(l);
        if (!m.hasMatch()) return fail(QStringLiteral("line %1: no timecode").arg(i + 1));
        if (m.captured(2) == QLatin1String(";") || m.captured(2) == QLatin1String(",")) sawDrop = true;
        raw << std::pair{m.captured(1) + QLatin1Char(':') + m.captured(3), m.captured(4)};
    }
    file.dropFrame = sawDrop;
    const TimecodeFormat format{30, sawDrop};
    for (const auto& [tc, words] : raw) {
        const auto t = Timecode::parse(tc.toStdString(), format);
        if (!t) return fail(QStringLiteral("bad timecode %1").arg(tc));
        SccLine line;
        line.frame = t->frameCount();
        for (const QString& w : words.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            bool ok = false;
            const uint v = w.toUInt(&ok, 16);
            if (!ok || w.size() != 4) return fail(QStringLiteral("bad byte pair %1 at %2").arg(w, tc));
            line.bytes += static_cast<char>((v >> 8) & 0xff);
            line.bytes += static_cast<char>(v & 0xff);
        }
        file.lines << line;
    }
    return file;
}

QByteArray sccCaptionData(const SccFile& file, std::int64_t startFrame, const Rational& rate) {
    // The file's frames run at 30000/1001 a second (drop frame or not, 608's rate).
    const bool sameRate = rate.num() * 1001 == rate.den() * 30000 || (rate.num() == 30 * rate.den());
    auto toFrame = [&](std::int64_t sccFrames) -> std::int64_t {
        if (sameRate) return sccFrames;
        const double seconds = static_cast<double>(sccFrames) * 1001.0 / 30000.0;
        return static_cast<std::int64_t>(std::llround(seconds * static_cast<double>(rate.num()) / static_cast<double>(rate.den())));
    };
    QByteArray out;
    for (const SccLine& line : file.lines) {
        if (line.frame < startFrame) continue;
        std::int64_t at = std::max<std::int64_t>(toFrame(line.frame - startFrame), out.size() / 2);
        while (out.size() < at * 2) out += kNullCaptionWord;
        out += line.bytes;
    }
    return out;
}

QString writeScc(const QByteArray& data, std::int64_t firstFrame, const TimecodeFormat& format) {
    QString out = QStringLiteral("Scenarist_SCC V1.0\n\n");
    for (const auto& [start, end] : captionLoadingSpans(data)) {
        out += QString::fromStdString(Timecode(firstFrame + start, format).toString()) + QLatin1Char('\t');
        QStringList words;
        for (std::int64_t f = start; f < end; ++f) {
            const auto i = static_cast<qsizetype>(f * 2);
            words << QStringLiteral("%1%2")
                         .arg(static_cast<unsigned char>(data.at(i)), 2, 16, QLatin1Char('0'))
                         .arg(static_cast<unsigned char>(data.at(i + 1)), 2, 16, QLatin1Char('0'));
        }
        out += words.join(QLatin1Char(' ')) + QStringLiteral("\n\n");
    }
    return out;
}

// ---------------------------------------------------------------- Decoding

QString CaptionScreen::text() const {
    QStringList out;
    for (const CaptionRow& r : rows) out << r.text.trimmed();
    return out.join(QLatin1Char('\n'));
}

QList<ShownCaption> decodeCaptions(const QByteArray& data) {
    QList<ShownCaption> out;
    Decoder decoder;
    CaptionScreen current;
    std::int64_t since = 0;
    const std::int64_t frames = data.size() / 2;
    for (std::int64_t f = 0; f < frames; ++f) {
        const auto i = static_cast<qsizetype>(f * 2);
        decoder.word(stripped(data, i), stripped(data, i + 1));
        // Only a frame with a byte pair can change the screen.
        if (stripped(data, i) == 0 && stripped(data, i + 1) == 0) continue;
        const CaptionScreen now = screenOf(decoder.shown());
        if (now == current) continue;
        if (!current.isEmpty()) out << ShownCaption{since, f, current};
        current = now;
        since = f;
    }
    if (!current.isEmpty()) out << ShownCaption{since, frames, current};
    return out;
}

std::optional<CaptionScreen> captionAt(const QList<ShownCaption>& captions, std::int64_t frame) {
    const auto it = std::upper_bound(captions.begin(), captions.end(), frame,
                                     [](std::int64_t f, const ShownCaption& c) { return f < c.start; });
    if (it == captions.begin()) return std::nullopt;
    const ShownCaption& c = *std::prev(it);
    if (frame >= c.end) return std::nullopt;
    return c.screen;
}

QList<std::pair<std::int64_t, std::int64_t>> captionLoadingSpans(const QByteArray& data) {
    QList<std::pair<std::int64_t, std::int64_t>> out;
    const std::int64_t frames = data.size() / 2;
    std::int64_t start = -1;
    for (std::int64_t f = 0; f <= frames; ++f) {
        const bool sends = f < frames && !isNullWord(data, f);
        if (sends && start < 0) start = f;
        if (!sends && start >= 0) {
            out << std::pair{start, f};
            start = -1;
        }
    }
    return out;
}

QByteArray popOnCaptionData(const QList<Caption>& captions, std::int64_t length) {
    QList<QByteArray> words; // one per frame, empty where nothing is sent
    auto ensure = [&](std::int64_t frame) {
        while (words.size() <= frame) words << QByteArray();
    };
    auto freeAt = [&](std::int64_t frame) { return frame >= words.size() || words.at(static_cast<qsizetype>(frame)).isEmpty(); };
    std::int64_t minimum = 0; // after the last caption's EOC
    for (qsizetype k = 0; k < captions.size(); ++k) {
        const Caption& c = captions.at(k);
        QList<QByteArray> load{controlWord(0x14, 0x20), controlWord(0x14, 0x2e)};
        QStringList rows = c.text.split(QLatin1Char('\n'));
        for (QString& r : rows) r = r.left(kColumns);
        const int first = kRows - static_cast<int>(rows.size()) + 1;
        for (qsizetype r = 0; r < rows.size(); ++r) {
            const QString& text = rows.at(r);
            const int centre = std::max(0, (kColumns - static_cast<int>(text.size())) / 2);
            const int indent = std::min(28, centre / 4 * 4);
            load << pacWord(first + static_cast<int>(r), indent);
            if (centre - indent > 0) load << controlWord(0x17, 0x20 + std::min(3, centre - indent));
            load << textWords(text);
        }
        // The first frame to load from, so that EOC lands on the caption's start where frames are free.
        std::int64_t p = c.start - static_cast<std::int64_t>(load.size());
        for (;;) {
            std::int64_t busy = 0;
            for (std::int64_t f = std::max<std::int64_t>(p, 0); f < c.start; ++f) busy += freeAt(f) ? 0 : 1;
            if (p - busy < minimum || static_cast<std::int64_t>(load.size()) + busy <= c.start - p) break;
            --p;
        }
        p = std::max(p, minimum);
        load << controlWord(0x14, 0x2f);
        for (const QByteArray& w : load) {
            while (!freeAt(p)) ++p;
            ensure(p);
            words[static_cast<qsizetype>(p)] = w;
            ++p;
        }
        minimum = p;
        // Erased at its end unless the next caption is shown from there.
        const bool next = k + 1 < captions.size() && captions.at(k + 1).start <= c.end;
        if (!next) {
            std::int64_t e = std::max(c.end, minimum);
            while (!freeAt(e)) ++e;
            ensure(e);
            words[static_cast<qsizetype>(e)] = controlWord(0x14, 0x2c);
        }
    }
    QByteArray out;
    const std::int64_t frames = std::max<std::int64_t>(length, words.size());
    for (std::int64_t f = 0; f < frames; ++f) {
        out += f < words.size() && !words.at(static_cast<qsizetype>(f)).isEmpty() ? words.at(static_cast<qsizetype>(f))
                                                                                  : QByteArray(kNullCaptionWord, 2);
    }
    return out;
}

// ---------------------------------------------------------------- Sequences

QByteArray sequenceCaptionData(const Sequence& sequence, const std::function<QByteArray(const Segment&)>& clipData) {
    const Track* d = sequence.track(QStringLiteral("D1"));
    if (!d) return {};
    QByteArray out;
    for (const Segment& s : d->segments) {
        QByteArray part;
        if (!s.isFiller() && clipData) part = clipData(s).mid(static_cast<qsizetype>(s.sourceIn * 2), static_cast<qsizetype>(s.length * 2));
        while (part.size() < s.length * 2) part += kNullCaptionWord;
        out += part;
    }
    return out;
}

QList<std::int64_t> brokenCaptionCuts(const Sequence& sequence, const std::function<QByteArray(const Segment&)>& clipData) {
    QList<std::int64_t> out;
    const Track* d = sequence.track(QStringLiteral("D1"));
    if (!d || !clipData) return out;
    std::int64_t at = 0;
    for (const Segment& s : d->segments) {
        if (!s.isFiller()) {
            const QByteArray data = clipData(s);
            const std::int64_t end = s.sourceIn + s.length;
            if (s.sourceIn > 0 && !isNullWord(data, s.sourceIn - 1) && !isNullWord(data, s.sourceIn)) out << at;
            if (!isNullWord(data, end - 1) && !isNullWord(data, end)) out << at + s.length;
        }
        at += s.length;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ---------------------------------------------------------------- TTML

QString writeTtml(const QList<ShownCaption>& captions, const Rational& rate) {
    auto clock = [&](std::int64_t frame) {
        const auto ms = static_cast<std::int64_t>(std::llround(static_cast<double>(frame) * 1000.0 * static_cast<double>(rate.den()) /
                                                               static_cast<double>(std::max<std::int64_t>(1, rate.num()))));
        return QStringLiteral("%1:%2:%3.%4")
            .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
            .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
            .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
            .arg(ms % 1000, 3, 10, QLatin1Char('0'));
    };
    QString out = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<tt xmlns=\"http://www.w3.org/ns/ttml\" xmlns:tts=\"http://www.w3.org/ns/ttml#styling\" xml:lang=\"en\">\n"
        "  <head>\n"
        "    <styling>\n"
        "      <style xml:id=\"caption\" tts:color=\"white\" tts:backgroundColor=\"black\" tts:fontFamily=\"monospaceSansSerif\"/>\n"
        "    </styling>\n"
        "    <layout>\n"
        "      <region xml:id=\"bottom\" tts:origin=\"10% 10%\" tts:extent=\"80% 80%\" tts:displayAlign=\"after\" tts:textAlign=\"center\"/>\n"
        "    </layout>\n"
        "  </head>\n"
        "  <body style=\"caption\" region=\"bottom\">\n"
        "    <div>\n");
    for (const ShownCaption& c : captions) {
        QStringList rows;
        for (const CaptionRow& r : c.screen.rows) rows << r.text.trimmed().toHtmlEscaped();
        out += QStringLiteral("      <p begin=\"%1\" end=\"%2\">%3</p>\n").arg(clock(c.start), clock(c.end), rows.join(QStringLiteral("<br/>")));
    }
    out += QStringLiteral("    </div>\n  </body>\n</tt>\n");
    return out;
}

} // namespace lumenis::model
