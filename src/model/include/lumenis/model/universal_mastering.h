#pragma once

#include "lumenis/core/rational.h"

#include <QList>
#include <QString>

// HD Universal Mastering (Editing Guide p.1297-1298; 8.7c2, docs/spec/sequence-format.md): "1080p high-definition
// sequences edited at one frame rate ... played back in real time at a different frame rate and with audio conversion that
// matches the video conversion ... by remastering audio at a different sample rate to generate replacement audio tracks
// from previously mixed-down audio clips." The six conversions, with the sample rate the audio is remastered at.
namespace lumenis::model {

struct MasteringConversion {
    Rational from;  // the sequence's frame rate
    Rational to;    // the rate it is output at
    int sampleRate; // the remastered audio's rate, from 48 kHz (the guide's table, p.1297)
    QString label;  // Change Sample Rate's menu item (p.1298)
};

// "t 23.97fps -> 24fps for Universal Mastering ... t 23.97fps -> 25fps for Universal Mastering" (p.1298), in the guide's
// order.
const QList<MasteringConversion>& masteringConversions();

// The rate `original` audio at `from` frames per second is remastered at to play at `to`: original x from / to, to the
// nearest sample (47952, 46034, 48048, 46080, 50000 and 50050 from 48 kHz, as the table on p.1297 gives them).
int masteredSampleRate(const Rational& from, const Rational& to, int original = 48000);

} // namespace lumenis::model
