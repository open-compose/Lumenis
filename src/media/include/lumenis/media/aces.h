#pragma once

#include <QString>
#include <QStringList>

#include <array>

// ACES (Editing Guide p.557-560; 8.5e4b1, docs/spec/color-adapters.md "ACES projects"): "Those input transforms convert
// the camera source to ACES 2065 color space ... Then, internally we convert to the ACEScct color space. This is done so
// the current controls still feel the same" (p.557). The published encodings are built in here: ACES2065-1 (linear,
// AP0 primaries, SMPTE ST 2065-1), ACEScg (linear, AP1), ACEScct and ACEScc (logarithmic, AP1; Academy S-2016-001 and
// S-2014-003) and ACESproxy (S-2013-001), with the camera log curves and gamuts their makers publish (ARRI LogC3 and
// ALEXA Wide Gamut, Sony S-Log3 with S-Gamut3 and S-Gamut3.Cine, Panasonic V-Log and V-Gamut). Primaries are converted
// through XYZ with a Bradford adaptation between the D65 and ACES whites (ours: the Academy's own transforms use CAT02).
namespace lumenis::media::aces {

using Matrix = std::array<double, 9>;
// Chromaticities, xy of red, green, blue and white.
using Primaries = std::array<double, 8>;

Primaries ap0();
Primaries ap1();
Primaries rec709();
Primaries rec2020();
Primaries p3d65();
Primaries alexaWideGamut();
Primaries sGamut3();
Primaries sGamut3Cine();
Primaries vGamut();

// The matrix taking linear RGB in `from`'s primaries to `to`'s, adapting the white (Bradford) when they differ.
Matrix conversion(const Primaries& from, const Primaries& to);
std::array<double, 3> apply(const Matrix& m, const std::array<double, 3>& rgb);

// ACEScct (S-2016-001): linear AP1 to the log encoding and back; values above 1 in, up to about 1.468 out.
double linearToAcescct(double linear);
double acescctToLinear(double cct);
// ACEScc (S-2014-003).
double linearToAcescc(double linear);
double acesccToLinear(double cc);
// ACESproxy 10-bit (S-2013-001): linear AP1 to the code value over 1023, from 64 to 940.
double linearToAcesproxy(double linear);
// The camera curves to scene linear: ARRI LogC3 at EI 800, Sony S-Log3, Panasonic V-Log.
double logC3ToLinear(double v);
double sLog3ToLinear(double v);
double vLogToLinear(double v);

// ACES output (ours, an approximation of the Reference Rendering and Output Transforms, which are not built in): scene
// linear AP1 to display light 0-1 with a filmic tone curve fitted to the ACES SDR output (K. Narkowicz's published fit,
// applied per channel after 0.6 exposure), for showing the ACEScct working space on a monitor.
std::array<double, 3> outputToneMap(const std::array<double, 3>& linearAp1);

} // namespace lumenis::media::aces
