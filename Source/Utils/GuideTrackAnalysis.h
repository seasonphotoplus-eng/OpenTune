#pragma once

// [GuideHarmony] お手本トラックの解析（F0 → ノート）。重い依存を持つので UI ヘッダからは include しない。

#include "GuideHarmony.h"
#include "LegacyNoteGenerator.h"
#include "ModelPathResolver.h"
#include "../Runtime/ProcessF0Runtime.h"
#include "../Inference/F0InferenceService.h"
#include "../Services/ImportedClipF0Extraction.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <string>
#include <vector>

namespace OpenTune {

/** お手本の音声（MELO 等）から F0 → ノートを作る。ブロッキング（バックグラウンドスレッドで呼ぶこと）。
 *  バッファ先頭 = タイムライン 0 秒として扱う。 */
inline bool analyzeGuideTrack(const juce::AudioBuffer<float>& buffer, double sampleRate,
                              std::vector<GuideNote>& out, std::string& errorMessage)
{
    out.clear();
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0 || sampleRate <= 0.0) {
        errorMessage = "empty audio";
        return false;
    }

    // モノラル化
    std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);
    for (int ch = 0; ch < numChannels; ++ch) {
        const float* src = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t>(i)] += src[i] / static_cast<float>(numChannels);
    }

    auto& runtime = ProcessF0Runtime::getInstance();
    if (!runtime.isReady() && !runtime.initialize(ModelPathResolver::getModelsDirectory(), &errorMessage))
        return false;
    auto service = runtime.getF0Service();
    if (service == nullptr) {
        errorMessage = "F0 service unavailable";
        return false;
    }

    auto owner = std::make_shared<F0RunOwnerState>();
    auto f0Result = service->extractF0(mono.data(), mono.size(), static_cast<int>(std::lround(sampleRate)), owner);
    if (!f0Result.ok()) {
        errorMessage = f0Result.error().fullMessage();
        return false;
    }
    const auto& f0 = f0Result.value();
    if (f0.empty()) {
        errorMessage = "F0 inference returned an empty result";
        return false;
    }

    const int hop = service->getF0HopSize();
    const int f0Rate = service->getF0SampleRate();
    const auto energy = computeFrameEnergy(mono.data(), numSamples, static_cast<int>(std::lround(sampleRate)),
                                           f0, f0Rate, hop);
    const auto notes = LegacyNoteGenerator::generate(f0.data(), static_cast<int>(f0.size()), energy.data(),
                                                     0, static_cast<int>(f0.size()), hop,
                                                     static_cast<double>(f0Rate), NoteGeneratorParams{});
    out.reserve(notes.size());
    for (const auto& n : notes) {
        if (!n.isVoiced || n.pitch <= 0.0f || n.endTime <= n.startTime)
            continue;
        out.push_back(GuideNote{ n.startTime, n.endTime, PitchUtils::freqToMidi(n.pitch) });
    }
    if (out.empty()) {
        errorMessage = "no notes found in guide track";
        return false;
    }
    return true;
}

} // namespace OpenTune
