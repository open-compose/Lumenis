// HD Universal Mastering's conversions (universal_mastering.h; Editing Guide p.1297-1298).
#include "lumenis/model/universal_mastering.h"

#include <QObject>

#include <cmath>

namespace lumenis::model {

int masteredSampleRate(const Rational& from, const Rational& to, int original) {
    if (to.num() == 0) return original;
    return static_cast<int>(std::llround(static_cast<double>(original) * from.toDouble() / to.toDouble()));
}

const QList<MasteringConversion>& masteringConversions() {
    static const QList<MasteringConversion> list = [] {
        const Rational ntsc(24000, 1001);
        const Rational film(24);
        const Rational pal(25);
        struct Row {
            Rational from;
            Rational to;
            const char* label;
        };
        // The menu's order (p.1298).
        const Row rows[] = {
            {ntsc, film, QT_TRANSLATE_NOOP("UniversalMastering", "23.97fps -> 24fps for Universal Mastering")},
            {film, pal, QT_TRANSLATE_NOOP("UniversalMastering", "24fps -> 25fps for Universal Mastering")},
            {film, ntsc, QT_TRANSLATE_NOOP("UniversalMastering", "24fps -> 23.97fps for Universal Mastering")},
            {pal, ntsc, QT_TRANSLATE_NOOP("UniversalMastering", "25fps -> 23.97fps for Universal Mastering")},
            {pal, film, QT_TRANSLATE_NOOP("UniversalMastering", "25fps -> 24fps for Universal Mastering")},
            {ntsc, pal, QT_TRANSLATE_NOOP("UniversalMastering", "23.97fps -> 25fps for Universal Mastering")},
        };
        QList<MasteringConversion> out;
        for (const Row& r : rows) {
            out.append({r.from, r.to, masteredSampleRate(r.from, r.to), QObject::tr(r.label)});
        }
        return out;
    }();
    return list;
}

} // namespace lumenis::model
