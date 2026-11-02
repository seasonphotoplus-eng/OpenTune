#pragma once

// [GuideHarmony] お手本（ガイド/MELO）表示・スナップと、ダイアトニックなハモリ生成の共通ヘルパー。
// ヘッダオンリー（CMake のソース一覧を変えずに追加できるように）。

#include "Note.h"
#include "NoteGeneratorTypes.h"
#include "PitchUtils.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace OpenTune {

/** お手本ノート（タイムライン秒、真の MIDI 番号）。 */
struct GuideNote {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    float midi = 0.0f;
};

/** 現在の音（MIDI）を、スケール上で degreeSteps 度ぶん移動した MIDI を返す。
 *  +2 = 3度上, -2 = 3度下, -3 = 4度下, +4 = 5度上, -5 = 6度下。 */
inline float diatonicShiftMidi(float currentMidi, int root, ScaleMode mode, int degreeSteps)
{
    int count = 0;
    const int* tones = ScaleSnapConfig::semitones(mode, count);
    if (tones == nullptr || count <= 0)
        return currentMidi;

    const int m = static_cast<int>(std::lround(currentMidi));
    int bestIdx = 0;
    int bestDist = 1 << 20;
    for (int oct = -2; oct <= 12; ++oct) {
        for (int d = 0; d < count; ++d) {
            const int cand = root + tones[d] + 12 * oct;
            const int dist = std::abs(cand - m);
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = oct * count + d;
            }
        }
    }
    const int idx = bestIdx + degreeSteps;
    const int oct = static_cast<int>(std::floor(static_cast<double>(idx) / static_cast<double>(count)));
    const int deg = idx - oct * count;
    return static_cast<float>(root + tones[deg] + 12 * oct);
}

/** お手本のピッチクラスに合わせた目標 MIDI（オクターブは現在位置に最も近いもの）。 */
inline float snapMidiToGuidePitchClass(float currentMidi, float guideMidi)
{
    const int pc = ((static_cast<int>(std::lround(guideMidi)) % 12) + 12) % 12;
    const float k = std::round((currentMidi - static_cast<float>(pc)) / 12.0f);
    return static_cast<float>(pc) + 12.0f * k;
}

} // namespace OpenTune
