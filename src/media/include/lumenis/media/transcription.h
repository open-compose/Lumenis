#pragma once

#include "lumenis/ext/transcription.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/transcript.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

// Speech to text for transcripts (Editing Guide ch.14; docs/spec/transcripts.md, 8.2a).
//
// The engine is an ext::TranscriptionProvider. Built in is WhisperProvider: whisper.cpp's command-line program
// `whisper-cli` (MIT), found like bmxtranswrap (LUMENIS_WHISPER, beside the application, on the PATH), with a ggml model
// file (LUMENIS_WHISPER_MODEL, else "ggml-base.bin" in the user's application data "Whisper" folder). "PhraseFind AI
// does not require an Internet connection. All processing is done on your local system" (p.436): so does whisper.cpp.
// transcribeClip() reads a clip's sound and makes its model::Transcript through any provider.
namespace lumenis::media {

// whisper-cli and its model, or empty paths when not found.
QString whisperExecutable();
QString whisperModel();

// whisper-cli's JSON output (-oj, one word per segment with -ml 1 -sow) as words; a segment with "speaker_turn_next"
// (its -tdrz option) ends a speaker's turn. Times are its "offsets" in milliseconds. Empty for anything else.
QList<model::EngineWord> parseWhisperJson(const QByteArray& json, QString* language = nullptr);

class WhisperProvider : public ext::TranscriptionProvider {
public:
    ext::ProviderInfo info() const override;
    // Unavailable without whisper-cli or its model, with the reason.
    ext::Availability probe() const override;
    // Runs whisper-cli on the samples (written as a 16 kHz WAV file) and waits for it; progress is asked between its
    // output lines. Speaker turns come back in TranscribedWord::speakerTurnAfter when the model is a tinydiarize one
    // ("tdrz" in its file name). An empty transcript when it fails.
    ext::Transcript transcribe(std::span<const float> samples, int sampleRate, const ext::TranscriptionOptions& options,
                               const ext::TranscriptionProgress& progress) override;
};

// A master clip's transcript: its first audio track, as mono samples at the media's rate, through `provider`. The transcript's mob is
// the clip's MobID. Nothing, with the reason, when the clip has no sound, the engine fails or progress cancels it.
std::optional<model::Transcript> transcribeClip(const model::BinItem& clip, ext::TranscriptionProvider& provider,
                                                const ext::TranscriptionOptions& options,
                                                const ext::TranscriptionProgress& progress = {}, QString* error = nullptr);

} // namespace lumenis::media
