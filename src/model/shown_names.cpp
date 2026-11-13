// Shown names for effects and parameter categories (shown_names.h; docs/spec/trademarks.md).
#include "lumenis/model/shown_names.h"

#include <QCoreApplication>

#include <utility>

namespace lumenis::model {

namespace {

// Stored words and ours, the longer first where one contains another. scripts/check-trademarks.py reads these pairs
// (one per line, as {"stored", "shown"}), so keep that form.
constexpr std::pair<const char*, const char*> kShown[] = {
    {" (Media Composer | Symphony Option Only)", ""},
    {"PlasmaWipe Avid ", "Gradient Wipe "},
    {"PlasmaWipe", "Gradient Wipe"},
    {"Avid Pan & Zoom", "Pan & Zoom"},
    {"Avid Titler+", "Title Designer"},
    {"Avid High Qual", "High Quality"},
    {"Avid Ultra Qual", "Ultra Quality"},
    {"Titler+", "Title Designer"},
    {"FluidMotion", "Flow Motion"},
    {"FluidBlur", "Flow Blur"},
    {"FluidColorMap", "Flow Color Map"},
    {"FluidMorph", "Flow Morph"},
    {"FluidFilm", "Flow Film"},
    {"AniMatte", "Animated Matte"},
    {"SpectraMatte Key", "Spectral Key"},
    {"SpectraMatte", "Spectral Key"},
    {"SpectraGraph", "Key Graph"},
    {"Intraframe", "Frame Paint"},
    {"Magic Mask", "Color Mask"},
    {"Use NaturalMatch", "Keep Luminance"},
    {"NaturalMatch", "Keep Luminance"},
    {"Illusion FX", "Image FX"},
    {"SubCap", "Subtitles"},
    {"FrameFlex", "Framing"},
    {"AudioSuite", "Audio Processing"},
};

} // namespace

QString shownName(const QString& stored) {
    QString out = stored;
    for (const auto& [from, to] : kShown) out.replace(QLatin1String(from), QLatin1String(to));
    // Then in the language installed (10.4): Qt Linguist's "effects" context holds these names as renamed here
    // (scripts/update-translations.py applies the same table), so a translation never brings a mark back.
    if (out.isEmpty()) return out;
    return QCoreApplication::translate("effects", out.toUtf8().constData());
}

} // namespace lumenis::model
