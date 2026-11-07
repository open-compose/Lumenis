#pragma once

#include "lumenis/ext/provider.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace lumenis::ext {

// Extension point for speech-to-text, which drives the ScriptSync and
// PhraseFind equivalents. The bundled implementation will be whisper.cpp
// (MIT); a licensed engine or a cloud service can be added as another
// provider without changing callers.

// How to transcribe.
struct TranscriptionOptions {
    std::string language;       // BCP 47 tag, empty = detect
    bool wordTimestamps = true;
};

// One recognised word, with its times in seconds from the start of the samples and the engine's
// confidence (0 to 1).
struct TranscribedWord {
    std::string text;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    float confidence = 0.0f;
    // The speaker changes after this word (an engine that marks speaker turns, such as whisper.cpp's tinydiarize).
    bool speakerTurnAfter = false;
};

// The language detected or used, and the words in time order.
struct Transcript {
    std::string language;
    std::vector<TranscribedWord> words;
};

// Mono float samples at the given rate. Returns false to cancel.
using TranscriptionProgress = std::function<bool(double fraction)>;

// A speech-to-text engine (Category::Transcription). transcribe() runs synchronously and calls
// progress from time to time; when progress returns false it stops early.
class TranscriptionProvider : public Provider {
public:
    virtual Transcript transcribe(std::span<const float> samples, int sampleRate,
                                  const TranscriptionOptions& options,
                                  const TranscriptionProgress& progress) = 0;
};

} // namespace lumenis::ext
