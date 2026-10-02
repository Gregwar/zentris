#include "samples.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// Feature statistics of the analysis frames in [t0, t1).
struct WinStats {
    float loud = 0, bass = 0, mid = 0, high = 0, onset = 0, intensity = 0;
    float head = 0;      // strongest onset right at the start (the attack)
    float body = 0;      // mean onset after the attack
    float loudHead = 0, loudTail = 0;
    float stability = 0; // 1 = the spectrum holds still (sustained), 0 = it keeps changing
};

WinStats stats(const Analysis& an, double t0, double t1) {
    WinStats s;
    const int n = (int)an.frames.size();
    int f0 = std::clamp((int)(t0 / an.hop), 0, n - 1), f1 = std::clamp((int)(t1 / an.hop), f0 + 1, n);
    int cnt = f1 - f0, headEnd = std::min(f1, f0 + 4), tail0 = f0 + cnt * 3 / 4;
    float delta = 0;
    for (int f = f0; f < f1; f++) {
        const FrameFeatures& ff = an.frames[f];
        s.loud += ff.loud;
        s.bass += ff.bass;
        s.mid += ff.mid;
        s.high += ff.high;
        s.onset += ff.onset;
        s.intensity += an.intensity[f];
        if (f < headEnd) {
            s.head = std::max(s.head, ff.onset);
            s.loudHead = std::max(s.loudHead, ff.loud);
        } else s.body += ff.onset;
        if (f >= tail0) s.loudTail += ff.loud;
        if (f > f0)
            for (int b = 0; b < NUM_BANDS; b++) delta += std::fabs(ff.bands[b] - an.frames[f - 1].bands[b]);
    }
    for (float* v : {&s.loud, &s.bass, &s.mid, &s.high, &s.onset, &s.intensity}) *v /= cnt;
    s.body /= std::max(1, f1 - headEnd);
    s.loudTail /= std::max(1, f1 - tail0);
    s.stability = std::clamp(1.f - delta / std::max(1, cnt - 1) / NUM_BANDS * 6.f, 0.f, 1.f);
    return s;
}

// The attack near a beat: the steepest rise of the short-time energy (1 ms blocks) shortly after it.
// Analysis frames are 43 ms long and timed at their start, so the real transient comes slightly later.
double refineAttack(const Track& t, double beat) {
    const uint32_t sr = t.sampleRate;
    const int B = std::max(1, (int)(sr / 1000));
    long a = (long)((beat - 0.01) * sr), b = (long)((beat + 0.07) * sr);
    a = std::max(0L, a);
    b = std::min((long)t.frames(), b);
    const int nb = (int)((b - a) / B);
    if (nb < 8) return beat;
    std::vector<float> e(nb);
    for (int k = 0; k < nb; k++) {
        double s = 0;
        for (long i = a + (long)k * B; i < a + (long)(k + 1) * B; i++) {
            float m = 0.5f * (t.pcm[i * 2] + t.pcm[i * 2 + 1]);
            s += m * m;
        }
        e[k] = std::log(1e-7f + (float)(s / B));
    }
    int best = -1;
    float bestRise = 1.0f; // need a clear rise (in log energy), otherwise keep the beat time
    for (int k = 3; k < nb; k++) {
        float rise = e[k] - std::max({e[k - 1], e[k - 2], e[k - 3]}) * 0.5f - std::min({e[k - 1], e[k - 2], e[k - 3]}) * 0.5f;
        if (rise > bestRise) { bestRise = rise; best = k; }
    }
    if (best < 0) return beat;
    return (a + (long)(best - 2) * B) / (double)sr; // a couple of ms before the rise
}

std::shared_ptr<const std::vector<float>> cut(const Track& t, double start, double end, double fadeOut) {
    const uint32_t sr = t.sampleRate;
    uint64_t a = (uint64_t)std::max(0.0, start * sr), b = std::min<uint64_t>(t.frames(), (uint64_t)(end * sr));
    auto out = std::make_shared<std::vector<float>>();
    if (b <= a) return out;
    const uint64_t n = b - a, fin = std::max<uint64_t>(1, sr / 500), fout = std::max<uint64_t>(1, (uint64_t)(fadeOut * sr));
    out->resize(n * 2);
    for (uint64_t i = 0; i < n; i++) {
        float g = 1.f;
        if (i < fin) g = (float)i / fin;
        uint64_t left = n - 1 - i;
        if (left < fout) g *= 0.5f - 0.5f * std::cos(3.14159265f * (float)left / fout);
        (*out)[i * 2] = t.pcm[(a + i) * 2] * g;
        (*out)[i * 2 + 1] = t.pcm[(a + i) * 2 + 1] * g;
    }
    return out;
}

} // namespace

const char* sampleName(int kind) {
    static const char* names[SMP_COUNT] = {"HIT", "LOW", "TONE", "HOOK"};
    return kind >= 0 && kind < SMP_COUNT ? names[kind] : "";
}

std::vector<SongSample> extractSamples(const Track& t) {
    const Analysis& an = t.analysis;
    std::vector<SongSample> out;
    const int nb = (int)an.beats.size();
    if (nb < 16 || an.frames.empty() || an.segments.empty()) return out;

    // Segments per group: a group heard several times is what the song is "about".
    int nGroups = 0;
    for (const Segment& sg : an.segments) nGroups = std::max(nGroups, sg.cluster + 1);
    std::vector<int> groupCount(nGroups, 0);
    for (const Segment& sg : an.segments) groupCount[sg.cluster]++;

    // Picked first: the hook defines the song, the smaller ones avoid reusing its bar.
    const int order[SMP_COUNT] = {SMP_HOOK, SMP_HIT, SMP_LOW, SMP_TONE};
    const int lengthBeats[SMP_COUNT] = {1, 2, 2, 4};
    std::vector<int> usedSegments;

    for (int kind : order) {
        const int k = lengthBeats[kind];
        SongSample best;
        best.score = -1;
        for (int i = 0; i + k < nb; i++) {
            const int inBar = ((i - an.downbeat) % 4 + 4) % 4;
            if (kind == SMP_HOOK && inBar != 0) continue;           // a whole bar, from its first beat
            if ((kind == SMP_LOW || kind == SMP_TONE) && inBar % 2) continue; // half bars
            const double t0 = an.beats[i], t1 = an.beats[i + k];
            if (t0 < 1.0 || t1 > t.duration() - 1.0) continue;
            bool overlaps = false;
            for (const SongSample& o : out) {
                double margin = 2 * (t1 - t0) / k;
                if (t0 < o.end + margin && t1 > o.start - margin) overlaps = true;
            }
            if (overlaps) continue;

            const int segIdx = an.segmentAt(0.5 * (t0 + t1));
            const Segment& sg = an.segments[segIdx];
            const WinStats s = stats(an, t0, t1);
            if (s.loud < 0.25f) continue; // near silence
            float score = 0;
            switch (kind) {
            case SMP_HOOK: {
                // A bar of the most repeated peak texture, starting with a clear downbeat.
                float kindW = sg.kind == SEG_CHORUS || sg.kind == SEG_DROP ? 1.f : sg.kind == SEG_BUILD ? 0.6f
                              : sg.kind == SEG_VERSE ? 0.55f : 0.35f;
                float repeatW = 0.7f + 0.3f * std::min(1.f, (groupCount[sg.cluster] - 1) / 2.f);
                float entryW = t0 - sg.start < 2.5 * (t1 - t0) ? 1.1f : 1.f; // the hook usually opens the section
                score = kindW * repeatW * entryW * (0.5f * s.intensity + 0.5f * s.loud) * (0.6f + 0.4f * s.head);
                break;
            }
            case SMP_HIT:
                // One clean transient that rings out: strong attack, quiet after it, decaying.
                score = s.head * (1.f - 0.8f * s.body) * std::sqrt(s.loud) *
                        std::clamp(0.6f + (s.loudHead - s.loudTail), 0.2f, 1.2f) * (0.6f + 0.4f * s.intensity);
                break;
            case SMP_LOW:
                // Bass-led half bar: lows up, highs down, with a downbeat to start it.
                score = s.bass * std::clamp(1.f - 0.7f * s.high, 0.1f, 1.f) * (0.5f + 0.5f * s.head) * std::sqrt(s.loud);
                break;
            case SMP_TONE:
                // Sustained, tonal half bar: few transients, a steady spectrum, present mids.
                score = s.stability * (1.f - s.onset) * (1.f - s.onset) * s.mid * std::sqrt(s.loud);
                break;
            }
            if (std::find(usedSegments.begin(), usedSegments.end(), segIdx) != usedSegments.end()) score *= 0.8f;
            if (score > best.score) {
                best.score = score;
                best.kind = kind;
                best.start = t0;
                best.end = t1;
                best.beats = k;
                char why[160];
                switch (kind) {
                case SMP_HOOK:
                    std::snprintf(why, sizeof(why), "%s G%d (X%d)  INTENSITY %.2f", segmentName(sg.kind), sg.cluster,
                                  groupCount[sg.cluster], s.intensity);
                    break;
                case SMP_HIT:
                    std::snprintf(why, sizeof(why), "ATTACK %.2f  AFTER %.2f  %s", s.head, s.body, segmentName(sg.kind));
                    break;
                case SMP_LOW:
                    std::snprintf(why, sizeof(why), "BASS %.2f  HIGHS %.2f  %s", s.bass, s.high, segmentName(sg.kind));
                    break;
                default:
                    std::snprintf(why, sizeof(why), "STEADY %.2f  ONSETS %.2f  %s", s.stability, s.onset, segmentName(sg.kind));
                }
                best.why = why;
            }
        }
        if (best.score < 0) continue;
        usedSegments.push_back(an.segmentAt(0.5 * (best.start + best.end)));
        // Cut on the actual attack; keep the beat length so the sample stays in time.
        const double len = best.end - best.start, start = refineAttack(t, best.start);
        best.start = start;
        best.end = start + len;
        best.pcm = cut(t, best.start, best.end, std::min(0.3 * len, 0.35));
        out.push_back(best);
    }
    std::sort(out.begin(), out.end(), [](const SongSample& a, const SongSample& b) { return a.kind < b.kind; });
    return out;
}
