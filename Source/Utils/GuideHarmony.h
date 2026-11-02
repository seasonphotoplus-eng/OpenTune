#pragma once

// [GuideHarmony] お手本（ガイド/MELO）表示・スナップと、ダイアトニックなハモリ生成の共通ヘルパー。
// ヘッダオンリー（CMake のソース一覧を変えずに追加できるように）。

#include "Note.h"
#include "NoteGeneratorTypes.h"
#include "PitchUtils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
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

/** タイムライン区間 [t0, t1) のノートに対するお手本スナップ先。
 *  重なりが最大のお手本ノートの音名へ（オクターブは現在位置に最も近いもの）。
 *  お手本との重なりがノート長の minOverlapRatio 未満なら nullopt（ハモリ・アドリブ等は触らない）。
 *  guide は startSeconds 昇順であること。 */
inline std::optional<float> guideSnapTarget(const std::vector<GuideNote>& guide,
                                            double t0, double t1, float currentMidi,
                                            double minOverlapRatio = 0.3)
{
    const double len = t1 - t0;
    if (len <= 0.0)
        return std::nullopt;
    const GuideNote* best = nullptr;
    double bestOverlap = 0.0;
    for (const auto& gn : guide) {
        if (gn.startSeconds >= t1)
            break;
        const double ov = std::min(t1, gn.endSeconds) - std::max(t0, gn.startSeconds);
        if (ov > bestOverlap) {
            bestOverlap = ov;
            best = &gn;
        }
    }
    if (best == nullptr || bestOverlap < minOverlapRatio * len)
        return std::nullopt;
    return snapMidiToGuidePitchClass(currentMidi, best->midi);
}

// ============================================================================
// キー（調号）判定：お手本のメロディから、転調も含めて区間ごとに判定する。
// 平行調（D メジャー / B マイナー等）は使う音が同じなので区別せず、
// 「メジャー側の主音」で表す（ダイアトニックなハモリはこれで決まる）。
// ============================================================================

/** ある時刻から有効なキー。root = メジャー側の主音（0=C … 11=B）。 */
struct GuideKeySegment {
    double startSeconds = 0.0;
    int root = 0;
};

inline std::string guideKeyName(int majorRoot)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    const int r = ((majorRoot % 12) + 12) % 12;
    const int minorRoot = (r + 9) % 12;
    return std::string(names[r]) + " / " + names[minorRoot] + "m";
}

inline std::string formatSecondsMMSS(double seconds)
{
    const int total = std::max(0, static_cast<int>(std::floor(seconds)));
    const int sec = total % 60;
    return std::to_string(total / 60) + ":" + (sec < 10 ? "0" : "") + std::to_string(sec);
}

/** 区間リストから時刻 t のキー（メジャー側の主音）。空なら -1。 */
inline int guideKeyAt(const std::vector<GuideKeySegment>& segments, double t)
{
    if (segments.empty())
        return -1;
    int root = segments.front().root;
    for (const auto& seg : segments) {
        if (seg.startSeconds > t)
            break;
        root = seg.root;
    }
    return root;
}

namespace GuideKeyDetail {
/** 12 次元のピッチクラス分布と、調号 root（メジャー主音）のスコア。
 *  Krumhansl-Kessler のメジャー／平行マイナーのプロファイル相関の大きい方。 */
inline double keyScore(const double (&hist)[12], int root)
{
    static constexpr double kMajor[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr double kMinor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto corr = [&hist](const double (&profile)[12], int tonic) {
        double mh = 0.0, mp = 0.0;
        for (int i = 0; i < 12; ++i) { mh += hist[i]; mp += profile[i]; }
        mh /= 12.0; mp /= 12.0;
        double num = 0.0, dh = 0.0, dp = 0.0;
        for (int i = 0; i < 12; ++i) {
            const double h = hist[(i + tonic) % 12] - mh;
            const double p = profile[i] - mp;
            num += h * p; dh += h * h; dp += p * p;
        }
        return (dh <= 0.0 || dp <= 0.0) ? 0.0 : num / std::sqrt(dh * dp);
    };
    return std::max(corr(kMajor, root), corr(kMinor, (root + 9) % 12));
}

inline void histogram(const std::vector<GuideNote>& notes, double w0, double w1, double (&hist)[12], double& total)
{
    for (auto& h : hist) h = 0.0;
    total = 0.0;
    for (const auto& n : notes) {
        const double ov = std::min(w1, n.endSeconds) - std::max(w0, n.startSeconds);
        if (ov <= 0.0)
            continue;
        const int pc = ((static_cast<int>(std::lround(n.midi)) % 12) + 12) % 12;
        hist[pc] += ov;
        total += ov;
    }
}
} // namespace GuideKeyDetail

/** お手本ノートから、転調を含むキー区間を推定する。
 *  windowSeconds 幅の窓を hopSeconds ずつずらしてキーらしさを出し、
 *  キーが切り替わるたびに switchPenalty を払う最適経路（Viterbi）で区間に分ける。 */
inline std::vector<GuideKeySegment> estimateGuideKeySegments(const std::vector<GuideNote>& notes,
                                                             double windowSeconds = 16.0,
                                                             double hopSeconds = 2.0,
                                                             double switchPenalty = 2.5)
{
    std::vector<GuideKeySegment> result;
    if (notes.empty())
        return result;

    double songEnd = 0.0;
    for (const auto& n : notes)
        songEnd = std::max(songEnd, n.endSeconds);

    // 曲全体のキー（窓が作れない短い素材でも使う）
    double hist[12];
    double total = 0.0;
    GuideKeyDetail::histogram(notes, 0.0, songEnd, hist, total);
    if (total <= 0.0)
        return result;
    int globalRoot = 0;
    double globalBest = -1e9;
    for (int r = 0; r < 12; ++r) {
        const double sc = GuideKeyDetail::keyScore(hist, r);
        if (sc > globalBest) { globalBest = sc; globalRoot = r; }
    }

    const int numWindows = songEnd > windowSeconds
        ? static_cast<int>(std::floor((songEnd - windowSeconds) / hopSeconds)) + 1
        : 0;
    if (numWindows < 2) {
        result.push_back({ 0.0, globalRoot });
        return result;
    }

    std::vector<std::array<double, 12>> emission(static_cast<size_t>(numWindows));
    for (int w = 0; w < numWindows; ++w) {
        const double w0 = w * hopSeconds;
        GuideKeyDetail::histogram(notes, w0, w0 + windowSeconds, hist, total);
        for (int r = 0; r < 12; ++r)
            emission[static_cast<size_t>(w)][static_cast<size_t>(r)] =
                total >= 2.0 ? GuideKeyDetail::keyScore(hist, r) : 0.0;   // 音が少ない窓は中立
    }

    // Viterbi（最初の窓は曲全体のキーを少し優遇）
    std::vector<std::array<double, 12>> score(static_cast<size_t>(numWindows));
    std::vector<std::array<int, 12>> back(static_cast<size_t>(numWindows));
    for (int r = 0; r < 12; ++r)
        score[0][static_cast<size_t>(r)] = emission[0][static_cast<size_t>(r)] + (r == globalRoot ? 0.5 : 0.0);
    for (int w = 1; w < numWindows; ++w) {
        for (int r = 0; r < 12; ++r) {
            double best = -1e18;
            int arg = r;
            for (int q = 0; q < 12; ++q) {
                const double v = score[static_cast<size_t>(w - 1)][static_cast<size_t>(q)] - (q == r ? 0.0 : switchPenalty);
                if (v > best) { best = v; arg = q; }
            }
            score[static_cast<size_t>(w)][static_cast<size_t>(r)] = best + emission[static_cast<size_t>(w)][static_cast<size_t>(r)];
            back[static_cast<size_t>(w)][static_cast<size_t>(r)] = arg;
        }
    }
    std::vector<int> path(static_cast<size_t>(numWindows));
    {
        int r = 0;
        double best = -1e18;
        for (int q = 0; q < 12; ++q)
            if (score[static_cast<size_t>(numWindows - 1)][static_cast<size_t>(q)] > best) {
                best = score[static_cast<size_t>(numWindows - 1)][static_cast<size_t>(q)];
                r = q;
            }
        for (int w = numWindows - 1; w >= 0; --w) {
            path[static_cast<size_t>(w)] = r;
            if (w > 0)
                r = back[static_cast<size_t>(w)][static_cast<size_t>(r)];
        }
    }

    // 区間化：切り替わり位置は、窓から出したおおよその位置 ±8 秒のお手本ノートの頭のうち、
    // 「前は旧キー・後ろは新キー」で数えたスケール外の音の長さが最も少ない所にする。
    auto inMajorScale = [](float midi, int root) {
        static constexpr bool kIn[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
        const int pc = ((static_cast<int>(std::lround(midi)) - root) % 12 + 12) % 12;
        return kIn[pc];
    };
    result.push_back({ 0.0, path[0] });
    for (int w = 1; w < numWindows; ++w) {
        const int oldRoot = path[static_cast<size_t>(w - 1)];
        const int newRoot = path[static_cast<size_t>(w)];
        if (newRoot == oldRoot)
            continue;
        const double approx = w * hopSeconds + windowSeconds * 0.5;
        const double lo = std::max(result.back().startSeconds + hopSeconds, approx - 8.0);
        const double hi = approx + 8.0;
        double boundary = approx;
        double bestCost = 1e18;
        for (const auto& cand : notes) {
            if (cand.startSeconds < lo || cand.startSeconds > hi)
                continue;
            double cost = 0.0;
            for (const auto& n : notes) {
                if (n.endSeconds <= lo || n.startSeconds >= hi)
                    continue;
                const int root = n.startSeconds < cand.startSeconds ? oldRoot : newRoot;
                if (!inMajorScale(n.midi, root))
                    cost += n.endSeconds - n.startSeconds;
            }
            // 同点なら approx に近い方
            cost += 1e-3 * std::abs(cand.startSeconds - approx);
            if (cost < bestCost) { bestCost = cost; boundary = cand.startSeconds; }
        }
        if (boundary <= result.back().startSeconds)
            boundary = result.back().startSeconds + hopSeconds;
        result.push_back({ boundary, newRoot });
    }
    return result;
}



// ============================================================================
// お手本の時間ズレ推定：歌（全クリップの F0）とお手本の音名が最も一致する時間ずらし量を探す。
// ============================================================================

/** 歌の 1 フレーム（タイムライン秒、MIDI）。 */
struct GuideVocalPoint {
    double timeSeconds = 0.0;
    float midi = 0.0f;
};

/** お手本を offset 秒ずらしたとき、歌と音名が一致する割合が最大になる offset を返す（-maxShift〜+maxShift）。
 *  outConfidence には「一致率」(0〜1) を入れる。歌の点が少なすぎるときは 0 を返す。 */
inline double estimateGuideOffset(const std::vector<GuideNote>& guide,
                                  const std::vector<GuideVocalPoint>& vocal,
                                  double& outConfidence,
                                  double maxShiftSeconds = 1.0,
                                  double stepSeconds = 0.01)
{
    outConfidence = 0.0;
    if (guide.empty() || vocal.size() < 50)
        return 0.0;

    // お手本を stepSeconds 刻みのピッチクラス列にする（-1 = 無音）
    double end = 0.0;
    for (const auto& n : guide)
        end = std::max(end, n.endSeconds);
    const int gridLen = static_cast<int>(std::ceil(end / stepSeconds)) + 1;
    std::vector<int> grid(static_cast<size_t>(gridLen), -1);
    for (const auto& n : guide) {
        const int pc = ((static_cast<int>(std::lround(n.midi)) % 12) + 12) % 12;
        const int a = std::max(0, static_cast<int>(std::floor(n.startSeconds / stepSeconds)));
        const int b = std::min(gridLen, static_cast<int>(std::ceil(n.endSeconds / stepSeconds)));
        for (int i = a; i < b; ++i)
            grid[static_cast<size_t>(i)] = pc;
    }

    // 歌のピッチクラス（半音の中間付近 = 音程があいまいな点は除く）
    std::vector<std::pair<double, int>> pts;
    pts.reserve(vocal.size());
    for (const auto& v : vocal) {
        const float frac = v.midi - std::round(v.midi);
        if (std::abs(frac) > 0.35f)
            continue;
        pts.emplace_back(v.timeSeconds, ((static_cast<int>(std::lround(v.midi)) % 12) + 12) % 12);
    }
    if (pts.size() < 50)
        return 0.0;

    const int maxSteps = static_cast<int>(std::lround(maxShiftSeconds / stepSeconds));
    std::vector<double> scores;
    std::vector<double> confs;
    scores.reserve(static_cast<size_t>(2 * maxSteps + 1));
    for (int k = -maxSteps; k <= maxSteps; ++k) {
        const double offset = k * stepSeconds;
        int match = 0, active = 0, silent = 0;
        for (const auto& [t, pc] : pts) {
            // お手本を +offset ずらす = 歌の時刻 t では、元のお手本の t - offset を見る
            const int idx = static_cast<int>(std::floor((t - offset) / stepSeconds));
            const int g = (idx >= 0 && idx < gridLen) ? grid[static_cast<size_t>(idx)] : -1;
            if (g < 0) { ++silent; continue; }
            ++active;
            if (g == pc)
                ++match;
        }
        // 音名一致を加点、不一致・お手本の無音に歌が乗ることを減点
        scores.push_back(match - 0.5 * (active - match) - 0.3 * silent);
        confs.push_back(active > 0 ? static_cast<double>(match) / active : 0.0);
    }
    int best = 0;
    for (int i = 1; i < static_cast<int>(scores.size()); ++i)
        if (scores[static_cast<size_t>(i)] > scores[static_cast<size_t>(best)])
            best = i;
    // 同じ最高点が続く平らな所は、その真ん中を採る
    const double tol = std::max(1.0, 0.002 * std::abs(scores[static_cast<size_t>(best)]));
    int lo = best, hi = best;
    while (lo > 0 && scores[static_cast<size_t>(lo - 1)] >= scores[static_cast<size_t>(best)] - tol) --lo;
    while (hi + 1 < static_cast<int>(scores.size()) && scores[static_cast<size_t>(hi + 1)] >= scores[static_cast<size_t>(best)] - tol) ++hi;
    const int centre = (lo + hi) / 2;
    outConfidence = confs[static_cast<size_t>(centre)];
    const double bestOffset = (centre - maxSteps) * stepSeconds;
    return bestOffset;
}


// ============================================================================
// タイミング補正：歌の各ノートの頭を、近くにある同じ音名のお手本ノートの頭へ寄せる。
// ============================================================================

/** タイミング補正用の歌ノート（source 秒 = クリップ内の元の時刻、timeline 秒 = 今の曲上の時刻）。 */
struct GuideTimingNote {
    double sourceStart = 0.0;
    double timelineStart = 0.0;
    float midi = 0.0f;
};

/** (source 秒, 移動先のタイムライン秒) の組。 */
struct GuideTimingAnchor {
    double sourceSeconds = 0.0;
    double targetTimelineSeconds = 0.0;
};

/** 各歌ノートについて、±maxShift 秒以内の同じ音名のお手本ノートの頭のうち一番近いものを選ぶ。
 *  お手本ノート 1 つにつき歌ノート 1 つまで（近い組から確定）。見つからないノートは動かさない。 */
inline std::vector<GuideTimingAnchor> computeGuideTimingAnchors(const std::vector<GuideTimingNote>& notes,
                                                                const std::vector<GuideNote>& guide,
                                                                double maxShiftSeconds = 0.15)
{
    struct Cand { double dist; size_t note; size_t guide; };
    std::vector<Cand> cands;
    for (size_t i = 0; i < notes.size(); ++i) {
        const auto& n = notes[i];
        const int pc = ((static_cast<int>(std::lround(n.midi)) % 12) + 12) % 12;
        for (size_t j = 0; j < guide.size(); ++j) {
            const auto& g = guide[j];
            if (g.startSeconds > n.timelineStart + maxShiftSeconds)
                break;
            const double d = std::abs(g.startSeconds - n.timelineStart);
            if (d > maxShiftSeconds)
                continue;
            const int gpc = ((static_cast<int>(std::lround(g.midi)) % 12) + 12) % 12;
            if (gpc != pc)
                continue;
            cands.push_back({ d, i, j });
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.dist < b.dist; });
    std::vector<bool> noteUsed(notes.size(), false), guideUsed(guide.size(), false);
    std::vector<GuideTimingAnchor> out;
    for (const auto& c : cands) {
        if (noteUsed[c.note] || guideUsed[c.guide])
            continue;
        noteUsed[c.note] = true;
        guideUsed[c.guide] = true;
        out.push_back({ notes[c.note].sourceStart, guide[c.guide].startSeconds });
    }
    std::sort(out.begin(), out.end(), [](const GuideTimingAnchor& a, const GuideTimingAnchor& b) {
        return a.sourceSeconds < b.sourceSeconds;
    });
    return out;
}

/** 調号 root（メジャー主音）で degreeSteps 度移動した MIDI。 */
inline float diatonicShiftMidiInKey(float currentMidi, int majorRoot, int degreeSteps)
{
    return diatonicShiftMidi(currentMidi, majorRoot, ScaleMode::Major, degreeSteps);
}

} // namespace OpenTune
