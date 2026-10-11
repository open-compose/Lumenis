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

struct TranscriptionOptions {
    std::string language;       // BCP 47 tag, empty = detect
    bool wordTimestamps = true;
};

struct TranscribedWord {
    std::string text;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    float confidence = 0.0f;
};

struct Transcript {
    std::string language;
    std::vector<TranscribedWord> words;
};

// Mono float samples at the given rate. Returns false to cancel.
using TranscriptionProgress = std::function<bool(double fraction)>;

class TranscriptionProvider : public Provider {
public:
    virtual Transcript transcribe(std::span<const float> samples, int sampleRate,
                                  const TranscriptionOptions& options,
                                  const TranscriptionProgress& progress) = 0;
};

} // namespace lumenis::ext
