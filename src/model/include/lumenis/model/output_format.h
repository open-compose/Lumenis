#pragma once

#include "lumenis/core/project_format.h"
#include "lumenis/core/rational.h"

#include <QList>
#include <QString>

#include <cstdint>

// The Digital Cut tool's Output Format menu (8.7c2b; docs/spec/digital-cut.md "Output formats"): the play rates of
// 23.976p, 24p and 25p projects (Editing Guide p.932-937) and HD Universal Mastering's output formats of 1080 projects
// (p.1298-1299). Each format says how the sequence's frames are laid onto the tape: the rate they play at, the tape's
// rate and fields, and the 2:3 pulldown that fills the fields between ("When Media Composer records the digital cut, it
// adds the pulldown frames and recreates a telecine transfer to an NTSC videotape", p.933).
namespace lumenis::model {

struct OutputFormat {
    QString label;       // the menu's text: "23.976 (NTSC)", "1080p25/PAL", "NTSC 30i" ...
    QString description; // "A brief description of each output format is displayed in the Digital Cut tool" (p.933)
    Rational playRate;   // the sequence's frames play at this rate
    Rational tapeRate;   // frames a second on the tape
    bool interlaced = false; // two fields a tape frame
    int width = 0;       // the tape's raster; 0 is the project's (a downconvert gives its SD raster)
    int height = 0;
    bool ntsc = false;   // NTSC timecode: the RS-422 Output menu offers Drop or Non-Drop (p.935)
    bool destinationRate = false; // 24 (NTSC): the Dest. TC Rate menu, 29.97 or 30.00 fps (p.936)
    bool cadence = false;         // 2:3 pulldown of 23.976 frames: the Video Pulldown Cadence menu (p.937)
};

// "Select the video pulldown cadence ... Standard 2:3:2:3, Advanced 2:3:3:2" (p.937).
enum class PulldownCadence { Standard, Advanced };

// The Output Format menu for `project`, its own format first.
// * SD 23.976p and 24p NTSC projects: 23.976 (NTSC), 24 (NTSC), 29.97 (NTSC); SD 24p and 25p PAL projects: 24 (PAL),
//   25 (PAL) (the table on p.933-935). "(Media Composer) Depending on the type of project you are working with, NTSC or
//   PAL, the system will only display output options for one format" (p.933).
// * 1080 projects (the "HD 1080p" and "HD 1080i" groups, thin rasters too): the table on p.1299 (1080p23.976, 1080p24
//   and 1080p25 to each other and to 1080i59.94; 1080i50 to PAL 25i; 1080i59.94 to NTSC 30i).
// * Other projects: their own format alone.
QList<OutputFormat> outputFormats(const ProjectFormat& project);

// How much faster the sound plays on the tape than in the sequence: the play rate over the sequence's rate (p.935: 0.999
// for 23.976 NTSC from 24p, "4.1% speedup" for 25 PAL, "25% speedup" for 29.97 NTSC ...).
double outputSpeed(const OutputFormat& format, const Rational& sequenceRate);

// The sequence frame tape field `field` shows (a progressive tape's frames count as fields), counted from the first
// field of the digital cut. The frames play at the play rate onto the tape's fields at their middles, so 23.976 frames
// onto 59.94 fields give AA BBB CC DDD (Standard 2:3:2:3); Advanced 2:3:3:2 gives AA BBB CCC DD. A digital cut that
// starts on a B frame (`pullinB`, the sequence's Pullin column, p.938) starts the cycle two fields on.
std::int64_t outputSourceFrame(const OutputFormat& format, std::int64_t field, PulldownCadence cadence = PulldownCadence::Standard,
                               bool pullinB = false);

// The tape frames `frames` sequence frames fill: until each of them has been shown.
std::int64_t outputFrameCount(const OutputFormat& format, std::int64_t frames, PulldownCadence cadence = PulldownCadence::Standard,
                              bool pullinB = false);

// A timecode position at `from` as the position with the same label at `to` (01:00:00:00 at 24 fps is 01:00:00:00 at
// 30): frames x to's nominal rate / from's, to the nearest frame.
std::int64_t sameTimecodeFrames(std::int64_t frames, const Rational& from, const Rational& to);

} // namespace lumenis::model
