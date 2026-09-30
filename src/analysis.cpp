#include "analysis.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <thread>
#include <cstdio>
#include <cstdlib>

namespace {

using cfloat = std::complex<float>;

void fft(std::vector<cfloat>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * 3.14159265358979f / (float)len;
        cfloat wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cfloat w(1);
            for (size_t k = 0; k < len / 2; k++) {
                cfloat u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

float percentile(std::vector<float> v, float p) {
    if (v.empty()) return 0;
    size_t k = (size_t)std::clamp(p * (float)(v.size() - 1), 0.f, (float)(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

std::vector<float> movingAverage(const std::vector<float>& x, int radius) {
    std::vector<double> pre(x.size() + 1, 0.0);
    for (size_t i = 0; i < x.size(); i++) pre[i + 1] = pre[i] + x[i];
    std::vector<float> out(x.size());
    for (int i = 0; i < (int)x.size(); i++) {
        int a = std::max(0, i - radius), b = std::min((int)x.size(), i + radius + 1);
        out[i] = (float)((pre[b] - pre[a]) / std::max(1, b - a));
    }
    return out;
}

float map01(float v, float lo, float hi) { return std::clamp((v - lo) / (hi - lo), 0.f, 1.f); }

uint64_t fnv(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        h ^= (v >> (i * 8)) & 0xff;
        h *= 0x100000001b3ull;
    }
    return h;
}

} // namespace

const char* segmentName(int kind) {
    static const char* names[SEG_COUNT] = {"INTRO", "VERSE", "BUILD", "CHORUS", "DROP", "BREAK", "OUTRO"};
    return kind >= 0 && kind < SEG_COUNT ? names[kind] : "";
}

std::string keyName(int key, bool minor) {
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(names[((key % 12) + 12) % 12]) + (minor ? " MINOR" : " MAJOR");
}

Analysis analyzeAudio(const std::vector<float>& mono, uint32_t sr) {
    Analysis an;
    const int N = 2048, HOP = 512;
    an.hop = (double)HOP / sr;
    const int nFrames = mono.size() > (size_t)N ? (int)((mono.size() - N) / HOP) + 1 : 0;
    an.fp.duration = (float)mono.size() / sr;
    if (nFrames < 16) return an;

    std::vector<float> window(N);
    for (int i = 0; i < N; i++) window[i] = 0.5f - 0.5f * std::cos(2 * 3.14159265f * i / (N - 1));

    // Log-spaced band edges (in bins) from 30 Hz to 16 kHz.
    int edges[NUM_BANDS + 1];
    for (int b = 0; b <= NUM_BANDS; b++) {
        float f = 30.f * std::pow(16000.f / 30.f, (float)b / NUM_BANDS);
        edges[b] = std::clamp((int)std::lround(f * N / sr), 1, N / 2);
    }
    for (int b = 1; b <= NUM_BANDS; b++) edges[b] = std::max(edges[b], edges[b - 1] + 1);

    std::vector<std::array<float, NUM_BANDS>> rawBands(nFrames);
    std::vector<float> rawLoud(nFrames), rawFlux(nFrames), centroid(nFrames);
    std::array<double, 12> chroma{};
    std::array<double, NUM_BANDS> bandPowerSum{};

    // Short-time spectra, split across threads. Each chunk re-derives the previous frame's spectrum
    // so spectral flux stays continuous across chunk boundaries.
    auto spectrumFrame = [&](int f, std::vector<cfloat>& buf, double& rms) {
        const float* s = &mono[(size_t)f * HOP];
        rms = 0;
        for (int i = 0; i < N; i++) {
            buf[i] = cfloat(s[i] * window[i], 0);
            rms += s[i] * s[i];
        }
        fft(buf);
    };
    auto processRange = [&](int f0, int f1, std::array<double, NUM_BANDS>& powerSum) {
        std::vector<cfloat> buf(N);
        std::vector<float> prevLog(N / 2, 0.f), curLog(N / 2);
        double rms;
        if (f0 > 0) {
            spectrumFrame(f0 - 1, buf, rms);
            for (int k = 1; k < N / 2; k++) prevLog[k] = std::log1p(1000.f * std::abs(buf[k]) * (2.0f / N));
        }
        for (int f = f0; f < f1; f++) {
            spectrumFrame(f, buf, rms);
            rawLoud[f] = 10.f * std::log10((float)(rms / N) + 1e-9f);
            double flux = 0, wsum = 0, msum = 0;
            for (int k = 1; k < N / 2; k++) {
                float mag = std::abs(buf[k]) * (2.0f / N);
                curLog[k] = std::log1p(1000.f * mag);
                float d = curLog[k] - prevLog[k];
                if (d > 0 && k < N / 2 * 0.75) flux += d;
                wsum += (float)k * sr / N * mag;
                msum += mag;
            }
            std::swap(prevLog, curLog);
            rawFlux[f] = (float)flux;
            centroid[f] = msum > 1e-9 ? (float)(wsum / msum) : 0.f;
            for (int b = 0; b < NUM_BANDS; b++) {
                double p = 0;
                for (int k = edges[b]; k < edges[b + 1]; k++) p += std::norm(buf[k]);
                p /= (edges[b + 1] - edges[b]);
                powerSum[b] += p;
                rawBands[f][b] = 10.f * std::log10((float)p + 1e-10f);
            }
        }
    };

    // Chroma from a high-resolution FFT (needed to resolve semitones in the low-mids).
    auto computeChroma = [&]() {
        const int NC = 16384;
        std::vector<cfloat> cb(NC);
        std::vector<float> cw(NC);
        for (int i = 0; i < NC; i++) cw[i] = 0.5f - 0.5f * std::cos(2 * 3.14159265f * i / (NC - 1));
        const size_t stepSamples = (size_t)(sr * 0.5);
        for (size_t start = 0; start + NC < mono.size(); start += stepSamples) {
            for (int i = 0; i < NC; i++) cb[i] = cfloat(mono[start + i] * cw[i], 0);
            fft(cb);
            std::array<double, 12> c{};
            for (int k = 1; k < NC / 2; k++) {
                double freq = (double)k * sr / NC;
                if (freq < 90 || freq > 3000) continue;
                double midi = 12.0 * std::log2(freq / 440.0) + 69.0;
                double nearest = std::round(midi);
                double dev = midi - nearest;
                double w = std::exp(-dev * dev / (2 * 0.2 * 0.2));
                int pc = (((int)nearest) % 12 + 12) % 12;
                c[pc] += std::abs(cb[k]) * w;
            }
            double tot = 0;
            for (double v : c) tot += v;
            if (tot > 1e-9)
                for (int i = 0; i < 12; i++) chroma[i] += c[i] / tot;
        }
    };

    {
        const int T = (int)std::clamp(std::thread::hardware_concurrency() / 2u, 1u, 6u);
        std::vector<std::array<double, NUM_BANDS>> sums(T, std::array<double, NUM_BANDS>{});
        std::vector<std::thread> pool;
        pool.emplace_back(computeChroma);
        for (int t = 0; t < T; t++) {
            int f0 = (int)((int64_t)nFrames * t / T), f1 = (int)((int64_t)nFrames * (t + 1) / T);
            pool.emplace_back(processRange, f0, f1, std::ref(sums[t]));
        }
        for (auto& th : pool) th.join();
        for (auto& ps : sums)
            for (int b = 0; b < NUM_BANDS; b++) bandPowerSum[b] += ps[b];
    }

    // ---- Normalize frame features against the song's own distribution.
    an.frames.resize(nFrames);
    for (int b = 0; b < NUM_BANDS; b++) {
        std::vector<float> col(nFrames);
        for (int f = 0; f < nFrames; f++) col[f] = rawBands[f][b];
        float lo = percentile(col, 0.05f), hi = percentile(col, 0.985f);
        if (hi - lo < 1) hi = lo + 1;
        for (int f = 0; f < nFrames; f++) an.frames[f].bands[b] = map01(rawBands[f][b], lo, hi);
    }
    float loudLo = percentile(rawLoud, 0.05f), loudHi = percentile(rawLoud, 0.99f);
    if (loudHi - loudLo < 1) loudHi = loudLo + 1;

    // Onset envelope: flux minus local mean, rectified.
    std::vector<float> fluxAvg = movingAverage(rawFlux, (int)(0.25 / an.hop));
    std::vector<float> onset(nFrames);
    for (int f = 0; f < nFrames; f++) onset[f] = std::max(0.f, rawFlux[f] - fluxAvg[f]);
    float onsetHi = percentile(onset, 0.985f) + 1e-6f;

    for (int f = 0; f < nFrames; f++) {
        FrameFeatures& ff = an.frames[f];
        auto avg = [&](int a, int b) {
            float s = 0;
            for (int i = a; i <= b; i++) s += ff.bands[i];
            return s / (b - a + 1);
        };
        ff.bass = avg(0, 3);
        ff.mid = avg(4, 9);
        ff.high = avg(10, 15);
        ff.loud = map01(rawLoud[f], loudLo, loudHi);
        ff.onset = std::min(1.f, onset[f] / onsetHi);
    }

    // ---- Tempo and beats: dynamic-programming beat tracker (Ellis 2007).
    {
        std::vector<float> o(nFrames);
        for (int f = 0; f < nFrames; f++) o[f] = onset[f] / onsetHi;
        const float minBpm = 60, maxBpm = 190;
        int lagMin = (int)(60.0 / (maxBpm * an.hop)), lagMax = (int)(60.0 / (minBpm * an.hop)) + 1;
        std::vector<double> ac(lagMax * 2 + 2, 0.0);
        for (int L = lagMin / 2; L <= std::min((int)ac.size() - 1, nFrames - 1); L++) {
            double s = 0;
            for (int i = L; i < nFrames; i++) s += o[i] * o[i - L];
            ac[L] = s / (nFrames - L);
        }
        double best = -1;
        float bestLag = 60.f / (120.f * (float)an.hop);
        for (int L = lagMin; L <= lagMax; L++) {
            double bpm = 60.0 / (L * an.hop);
            double oct = std::log2(bpm / 115.0);
            double w = std::exp(-0.5 * oct * oct / (0.9 * 0.9));
            double s = (ac[L] + 0.5 * ac[std::min((int)ac.size() - 1, 2 * L)] + 0.25 * ac[L / 2]) * w;
            if (s > best) {
                best = s;
                bestLag = (float)L;
                if (L > lagMin && L < lagMax) { // parabolic refinement
                    double a = ac[L - 1], b = ac[L], c = ac[L + 1];
                    double den = a - 2 * b + c;
                    if (std::fabs(den) > 1e-12) bestLag = (float)(L + 0.5 * (a - c) / den);
                }
            }
        }
        const float period = bestLag;
        an.fp.bpm = (float)(60.0 / (period * an.hop));

        std::vector<double> score(nFrames, 0.0);
        std::vector<int> back(nFrames, -1);
        const double tightness = 10.0;
        for (int t = 0; t < nFrames; t++) {
            int a = t - (int)std::lround(2 * period), b = t - (int)std::lround(period / 2);
            double bestPrev = 0;
            int bestIdx = -1;
            for (int p = std::max(0, a); p <= b && p < t; p++) {
                double lr = std::log((t - p) / period);
                double v = score[p] - tightness * lr * lr;
                if (bestIdx < 0 || v > bestPrev) { bestPrev = v; bestIdx = p; }
            }
            score[t] = o[t] + (bestIdx >= 0 ? std::max(0.0, bestPrev) : 0.0);
            back[t] = (bestIdx >= 0 && bestPrev > 0) ? bestIdx : -1;
        }
        // Start from the best score in the last period.
        int t = nFrames - 1;
        for (int i = std::max(0, nFrames - 1 - (int)period); i < nFrames; i++)
            if (score[i] > score[t]) t = i;
        std::vector<int> beats;
        while (t >= 0) {
            beats.push_back(t);
            t = back[t];
        }
        std::reverse(beats.begin(), beats.end());
        // Reference beat: median phase of beats in the middle of the song (robust to intro weirdness).
        if (!beats.empty()) an.fp.beatOffset = beats[beats.size() / 2] * an.hop;
        else an.fp.beatOffset = 0;
        // Store beat times; used for beat-accurate gravity.
        an.beats.reserve(beats.size());
        for (int bIdx : beats) an.beats.push_back(bIdx * an.hop);
    }

    // ---- Key (Krumhansl-Schmuckler).
    {
        const double maj[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
        const double min[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
        auto corr = [&](const double* prof, int root) {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; i++) { mx += chroma[(i + root) % 12]; my += prof[i]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; i++) {
                double x = chroma[(i + root) % 12] - mx, y = prof[i] - my;
                sxy += x * y; sxx += x * x; syy += y * y;
            }
            return sxy / std::sqrt(sxx * syy + 1e-12);
        };
        double best = -2;
        for (int r = 0; r < 12; r++) {
            double a = corr(maj, r), b = corr(min, r);
            if (a > best) { best = a; an.fp.key = r; an.fp.minor = false; }
            if (b > best) { best = b; an.fp.key = r; an.fp.minor = true; }
        }
    }

    // ---- Global character.
    {
        std::vector<float> c = centroid;
        float med = percentile(c, 0.5f);
        an.fp.brightness = map01(std::log2(std::max(med, 1.f)), std::log2(700.f), std::log2(3500.f));

        double tot = 0, low = 0, air = 0;
        for (int b = 0; b < NUM_BANDS; b++) {
            // Tilt-compensate: music spectra fall ~ with frequency; weight by band index.
            double w = bandPowerSum[b] * std::pow(1.9, b);
            tot += w;
            if (b <= 3) low += w;
            if (b >= 11) air += w;
        }
        an.fp.bassWeight = map01((float)std::log10(low / tot + 1e-9), -1.3f, -0.25f);
        an.fp.airWeight = map01((float)std::log10(air / tot + 1e-9), -1.3f, -0.6f);

        std::vector<float> smooth = movingAverage(rawLoud, (int)(1.0 / an.hop));
        float lo = percentile(smooth, 0.1f), hi = percentile(smooth, 0.9f);
        an.fp.dynamics = map01(hi - lo, 3.f, 16.f);

        int peaks = 0;
        for (int f = 1; f + 1 < nFrames; f++)
            if (an.frames[f].onset > 0.3f && an.frames[f].onset >= an.frames[f - 1].onset &&
                an.frames[f].onset > an.frames[f + 1].onset)
                peaks++;
        an.fp.density = map01(peaks / an.fp.duration, 1.0f, 6.0f);
    }

    // ---- Slow intensity curve (drives pace and scene energy).
    {
        std::vector<float> e(nFrames);
        for (int f = 0; f < nFrames; f++) e[f] = 0.7f * an.frames[f].loud + 0.3f * an.frames[f].onset;
        std::vector<float> s = movingAverage(e, (int)(2.5 / an.hop));
        s = movingAverage(s, (int)(1.5 / an.hop));
        float lo = percentile(s, 0.05f), hi = percentile(s, 0.95f);
        if (hi - lo < 1e-3f) hi = lo + 1e-3f;
        an.intensity.resize(nFrames);
        for (int f = 0; f < nFrames; f++) an.intensity[f] = map01(s[f], lo, hi);
    }

    // ---- Song structure: boundaries from timbre novelty and energy steps, snapped to bar lines,
    //      then each segment is labelled (intro, verse, build, chorus, drop, break, outro) and
    //      grouped with the segments that sound alike (so a returning chorus is recognized).
    {
        const int step = 8;
        const double dur = an.fp.duration;
        struct Cand { double t; float score; };
        std::vector<Cand> cands;

        // Timbre / loudness novelty between adjacent 6 s windows.
        const int D = NUM_BANDS + 1;
        std::vector<std::vector<double>> pre(D, std::vector<double>(nFrames + 1, 0.0));
        for (int f = 0; f < nFrames; f++)
            for (int d = 0; d < D; d++)
                pre[d][f + 1] = pre[d][f] + (d < NUM_BANDS ? an.frames[f].bands[d] : an.frames[f].loud * 2.0);
        auto windowMean = [&](int d, int a, int b) { return (pre[d][b] - pre[d][a]) / std::max(1, b - a); };
        const int W = (int)(6.0 / an.hop);
        std::vector<float> nov;
        std::vector<int> novIdx;
        for (int f = W; f + W < nFrames; f += step) {
            double dist = 0;
            for (int d = 0; d < D; d++) {
                double x = windowMean(d, f - W, f) - windowMean(d, f, f + W);
                dist += x * x;
            }
            nov.push_back((float)std::sqrt(dist));
            novIdx.push_back(f);
        }
        auto pickPeaks = [&](const std::vector<float>& v, float thr, float weight) {
            const int nb = (int)(6.0 / (an.hop * step));
            for (int i = 0; i < (int)v.size(); i++) {
                if (v[i] < thr) continue;
                bool isMax = true;
                for (int j = std::max(0, i - nb); j < std::min((int)v.size(), i + nb + 1) && isMax; j++)
                    if (v[j] > v[i]) isMax = false;
                if (isMax) cands.push_back({novIdx[i] * an.hop, v[i] * weight});
            }
        };
        if (!nov.empty()) {
            float mean = std::accumulate(nov.begin(), nov.end(), 0.f) / nov.size(), var = 0;
            for (float v : nov) var += (v - mean) * (v - mean);
            float sd = std::sqrt(var / nov.size()) + 1e-6f;
            std::vector<float> z(nov.size());
            for (size_t i = 0; i < nov.size(); i++) z[i] = (nov[i] - mean) / sd;
            pickPeaks(z, 0.4f, 1.f);
        }
        // Energy steps (drops, breakdowns) on the slow intensity curve, 4 s windows.
        {
            std::vector<double> ip(nFrames + 1, 0.0);
            for (int f = 0; f < nFrames; f++) ip[f + 1] = ip[f] + an.intensity[f];
            const int E = (int)(4.0 / an.hop);
            std::vector<float> steps(novIdx.size(), 0.f);
            for (size_t i = 0; i < novIdx.size(); i++) {
                int f = novIdx[i];
                int a0 = std::max(0, f - E), b1 = std::min(nFrames, f + E);
                double before = (ip[f] - ip[a0]) / std::max(1, f - a0), after = (ip[b1] - ip[f]) / std::max(1, b1 - f);
                steps[i] = (float)std::fabs(after - before);
            }
            pickPeaks(steps, 0.22f, 6.f);
        }

        // Downbeat: which beat of 4 carries the most low-end attack.
        if (an.beats.size() >= 8) {
            float best = -1;
            for (int k = 0; k < 4; k++) {
                float sc = 0;
                for (size_t i = k; i < an.beats.size(); i += 4) {
                    const FrameFeatures& ff = an.frames[std::min((size_t)nFrames - 1, (size_t)(an.beats[i] / an.hop))];
                    sc += ff.bass + ff.onset;
                }
                if (sc > best) { best = sc; an.downbeat = k; }
            }
        }
        auto snap = [&](double t) {
            double bestT = t, bestD = 1e9;
            for (size_t i = an.downbeat; i < an.beats.size(); i += 4) {
                double d = std::fabs(an.beats[i] - t);
                if (d < bestD) { bestD = d; bestT = an.beats[i]; }
            }
            return bestD < 2.5 ? bestT : t;
        };

        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
        std::vector<double> bounds;
        for (const Cand& c : cands) {
            double t = snap(c.t);
            bool ok = t > 6.0 && t < dur - 6.0;
            for (double o : bounds) if (std::fabs(o - t) < 8.0) ok = false;
            if (ok) bounds.push_back(t);
        }
        std::sort(bounds.begin(), bounds.end());

        std::vector<double> eventBounds; // boundaries that start with a salient event
        // ---- Boundary localization (change-point detection at beat resolution).
        // The coarse detectors above say *that* the song changes around a time, but they work on wide,
        // smoothed windows, so they are imprecise (typically late). A listener hears a new section as
        // soon as the old texture stops: a gap, a bass cut, a riser or new elements. For each boundary:
        //   1. model the old section and the new one from their beats (per-dimension mean and spread of
        //      the 16 band levels + loudness), away from the transition zone;
        //   2. mark each beat of the transition zone as "changed" when it no longer fits the old model
        //      (outside the old section's own variation) or fits the new model better;
        //   3. if a salient event (gap, bass cut, hit: far outside the old texture) is followed by changed
        //      beats, the section starts there (a build-up before it still belongs to the old section);
        //   4. otherwise the change is gradual: place the boundary at the split that best separates
        //      unchanged beats (before) from changed beats (after), a robust step fit.
        // Boundaries keep beat accuracy: a change can start on a pickup, not only on a bar line.
        if (an.beats.size() >= 16) {
            const int nb = (int)an.beats.size();
            const double beatLen = 60.0 / std::max(40.f, an.fp.bpm);
            // Beat-synchronous feature vectors.
            std::vector<std::vector<double>> bv(nb, std::vector<double>(D, 0.0));
            for (int i = 0; i + 1 < nb; i++) {
                int a0 = std::clamp((int)(an.beats[i] / an.hop), 0, nFrames - 1);
                int b1 = std::clamp((int)(an.beats[i + 1] / an.hop), a0 + 1, nFrames);
                for (int d = 0; d < D; d++) bv[i][d] = windowMean(d, a0, b1);
            }
            bv[nb - 1] = bv[nb - 2];
            auto beatIndex = [&](double t) {
                int i = (int)(std::upper_bound(an.beats.begin(), an.beats.end(), t) - an.beats.begin()) - 1;
                return std::clamp(i, 0, nb - 1);
            };
            struct Model { std::vector<double> mu, sd; bool ok = false; };
            auto fit = [&](int i0, int i1) {
                Model m;
                i0 = std::clamp(i0, 0, nb);
                i1 = std::clamp(i1, 0, nb);
                if (i1 - i0 < 4) return m;
                m.mu.assign(D, 0.0);
                m.sd.assign(D, 0.0);
                for (int i = i0; i < i1; i++)
                    for (int d = 0; d < D; d++) m.mu[d] += bv[i][d];
                for (double& x : m.mu) x /= (i1 - i0);
                for (int i = i0; i < i1; i++)
                    for (int d = 0; d < D; d++) m.sd[d] += (bv[i][d] - m.mu[d]) * (bv[i][d] - m.mu[d]);
                for (double& x : m.sd) x = std::max(0.06, std::sqrt(x / (i1 - i0)));
                m.ok = true;
                return m;
            };
            auto dev = [&](const Model& m, int i) { // RMS z-score of beat i under model m
                double s2 = 0;
                for (int d = 0; d < D; d++) {
                    double z = (bv[i][d] - m.mu[d]) / m.sd[d];
                    s2 += z * z;
                }
                return std::sqrt(s2 / D);
            };

            std::vector<int> changeBeats;
            eventBounds.clear();
            for (size_t k = 0; k < bounds.size(); k++) {
                const double prevB = k > 0 ? bounds[k - 1] : 0.0, nextB = k + 1 < bounds.size() ? bounds[k + 1] : dur;
                const int ib = beatIndex(bounds[k]);
                const int ip = beatIndex(prevB), in = beatIndex(nextB);
                // Transition zone: 4 bars before to 3 bars after the coarse boundary, inside the neighbours.
                const int z0 = std::max(ip + 4, ib - 16), z1 = std::min(in - 4, ib + 12);
                // Old model: the 4 bars just before the zone (what the ear compares to), else what exists.
                Model mOld = fit(std::max(ip + 2, z0 - 16), z0);
                if (!mOld.ok) mOld = fit(ip + 2, std::max(ip + 6, ib - 8));
                // New model: the new section after the zone.
                Model mNew = fit(z1, std::min(in - 1, z1 + 16));
                if (!mNew.ok) mNew = fit(ib + 4, std::min(in - 1, ib + 20));
                if (!mOld.ok || !mNew.ok || z1 - z0 < 4) { changeBeats.push_back(ib); continue; }
                // The old section's own variation sets what "no longer fits" means.
                std::vector<float> selfDev;
                for (int i = std::max(ip + 2, z0 - 16); i < z0; i++) selfDev.push_back((float)dev(mOld, i));
                double tau = std::max(1.8, selfDev.size() >= 4 ? 1.2 * (double)percentile(selfDev, 0.9f) : 1.8);
                std::vector<int> changed(z1 - z0);
                std::vector<double> dOld(z1 - z0);
                for (int i = z0; i < z1; i++) {
                    double dO = dev(mOld, i), dN = dev(mNew, i);
                    dOld[i - z0] = dO;
                    // Changed: clearly outside the old texture, or clearly closer to the new one.
                    changed[i - z0] = (dO > tau || dN < 0.7 * dO) ? 1 : 0;
                }
                // Case 1: a salient event (gap, cut, hit) far outside the old texture, followed by changed
                // beats, is where the section is heard to start. Extend back through contiguous beats that
                // are already outside the old texture (a fill leading into it).
                const double tauEvent = std::max(3.0, 2.0 * tau);
                int event = -1;
                for (int i = z0; i < z1 && event < 0; i++) {
                    if (dOld[i - z0] < tauEvent) continue;
                    int cnt = 0, tot = 0;
                    for (int j = i; j < std::min(z1, i + 8); j++, tot++) cnt += changed[j - z0];
                    if (cnt * 4 >= tot * 3) event = i;
                }
                if (event >= 0) {
                    while (event > z0 && dOld[event - 1 - z0] > tau) event--;
                    changeBeats.push_back(event);
                    eventBounds.push_back(an.beats[event]);
                    continue;
                }
                // Case 2: a gradual change: step fit on the changed labels.
                // Step fit: cost(c) = unchanged beats at/after c + changed beats before c.
                int after0 = 0;
                for (int v : changed) after0 += 1 - v;
                int best = z0, bestCost = after0, before = 0, after = after0;
                for (int c = z0 + 1; c <= z1; c++) {
                    before += changed[c - 1 - z0];
                    after -= 1 - changed[c - 1 - z0];
                    int cost = before + after;
                    if (cost < bestCost) { bestCost = cost; best = c; }
                }
                changeBeats.push_back(std::min(best, nb - 1));
            }
            for (size_t k = 0; k < bounds.size(); k++) bounds[k] = an.beats[changeBeats[k]];

            // Structural downbeat: section changes land on bar lines, so when most changes agree on a beat
            // phase (mod 4), that phase is the downbeat (the bass/onset guess is only a fallback).
            if (changeBeats.size() >= 3) {
                int votes[4] = {0, 0, 0, 0};
                for (int cb : changeBeats) votes[cb % 4]++;
                int bestPhase = (int)(std::max_element(votes, votes + 4) - votes);
                if (votes[bestPhase] * 2 > (int)changeBeats.size()) an.downbeat = bestPhase;
            }

            std::sort(bounds.begin(), bounds.end());
            std::vector<double> kept;
            for (double t : bounds)
                if (t > 4.0 && t < dur - 4.0 && (kept.empty() || t - kept.back() >= std::max(6.0, 8 * beatLen)))
                    kept.push_back(t);
            bounds = kept;
        }
        bounds.insert(bounds.begin(), 0.0);
        bounds.push_back(dur);

        // Segment features.
        auto meanIntensity = [&](double a, double b) {
            int fa = std::clamp((int)(a / an.hop), 0, nFrames - 1), fb = std::clamp((int)(b / an.hop), fa + 1, nFrames);
            double sum = 0;
            for (int f = fa; f < fb; f++) sum += an.intensity[f];
            return (float)(sum / (fb - fa));
        };
        std::vector<std::array<float, NUM_BANDS>> timbre;
        for (size_t i = 0; i + 1 < bounds.size(); i++) {
            Segment sg;
            sg.start = bounds[i];
            sg.end = bounds[i + 1];
            double len = sg.end - sg.start;
            sg.energy = meanIntensity(sg.start, sg.end);
            for (double e : eventBounds)
                if (std::fabs(e - sg.start) < 0.05) sg.announced = true;
            sg.rise = meanIntensity(sg.start + 0.75 * len, sg.end) - meanIntensity(sg.start, sg.start + 0.25 * len);
            int fa = std::clamp((int)(sg.start / an.hop), 0, nFrames - 1), fb = std::clamp((int)(sg.end / an.hop), fa + 1, nFrames);
            std::array<float, NUM_BANDS> tb{};
            for (int f = fa; f < fb; f++) {
                for (int b2 = 0; b2 < NUM_BANDS; b2++) tb[b2] += an.frames[f].bands[b2];
                sg.brightness += an.frames[f].high;
            }
            for (float& v : tb) v /= (fb - fa);
            sg.brightness /= (fb - fa);
            timbre.push_back(tb);
            an.segments.push_back(sg);
        }

        // Group similar segments (timbre + energy), greedy against each cluster's first member.
        const int n = (int)an.segments.size();
        auto dist = [&](int a, int b) {
            double d = 0;
            for (int k = 0; k < NUM_BANDS; k++) d += (timbre[a][k] - timbre[b][k]) * (timbre[a][k] - timbre[b][k]);
            double de = 2.0 * (an.segments[a].energy - an.segments[b].energy);
            return std::sqrt(d + de * de);
        };
        std::vector<float> all;
        for (int a = 0; a < n; a++)
            for (int b = a + 1; b < n; b++) all.push_back((float)dist(a, b));
        float thr = all.empty() ? 0.f : percentile(all, 0.5f) * 0.6f;
        std::vector<int> reps;
        for (int i = 0; i < n; i++) {
            int c = -1;
            for (int k = 0; k < (int)reps.size() && c < 0; k++)
                if (dist(i, reps[k]) < thr && std::fabs(an.segments[i].energy - an.segments[reps[k]].energy) < 0.2f) c = k;
            if (c < 0) { c = (int)reps.size(); reps.push_back(i); }
            an.segments[i].cluster = c;
        }
        // Labels, from energy and structure. Only the song's peak texture (the group(s) with the highest
        // energy) can be a chorus or a drop: a loud section of another group is still a verse. A peak
        // section entered by a jump (energy rise, a build before it, or a salient event) is a drop.
        {
            int nGroups = 0;
            for (const Segment& sg : an.segments) nGroups = std::max(nGroups, sg.cluster + 1);
            std::vector<double> gE(nGroups, 0.0), gLen(nGroups, 0.0);
            for (const Segment& sg : an.segments) {
                gE[sg.cluster] += sg.energy * (sg.end - sg.start);
                gLen[sg.cluster] += sg.end - sg.start;
            }
            double maxE = 0;
            for (int g = 0; g < nGroups; g++) {
                gE[g] /= std::max(1e-6, gLen[g]);
                maxE = std::max(maxE, gE[g]);
            }
            auto peakGroup = [&](int g) { return gE[g] >= maxE - 0.1 && gE[g] >= 0.65; };
            for (int i = 0; i < n; i++) {
                Segment& sg = an.segments[i];
                float prevE = i > 0 ? an.segments[i - 1].energy : sg.energy;
                int prevKind = i > 0 ? an.segments[i - 1].kind : -1;
                bool prevPeak = i > 0 && peakGroup(an.segments[i - 1].cluster);
                if (i == 0 && n > 1 && sg.energy < 0.55f) sg.kind = SEG_INTRO;
                else if (i == n - 1 && n > 1 && sg.energy < 0.5f) sg.kind = SEG_OUTRO;
                else if (sg.rise > 0.22f && sg.energy < 0.8f) sg.kind = SEG_BUILD;
                else if (peakGroup(sg.cluster) && sg.energy >= 0.65f)
                    sg.kind = (sg.energy - prevE > 0.15f || prevKind == SEG_BUILD || (sg.announced && !prevPeak))
                                  ? SEG_DROP : SEG_CHORUS;
                else if (sg.energy < 0.4f) sg.kind = SEG_BREAK;
                else sg.kind = SEG_VERSE;
            }
        }

        for (const Segment& sg : an.segments) an.fp.sections.push_back(sg.start);
    }

    // ---- Fingerprint hash.
    {
        uint64_t h = 0xcbf29ce484222325ull;
        h = fnv(h, (uint64_t)std::lround(an.fp.bpm));
        h = fnv(h, (uint64_t)(an.fp.key * 2 + an.fp.minor));
        h = fnv(h, (uint64_t)std::lround(an.fp.duration));
        const int SEG = 48;
        for (int i = 0; i < SEG; i++) {
            int f = (int)((int64_t)i * nFrames / SEG);
            h = fnv(h, (uint64_t)std::lround(an.intensity[f] * 7));
            h = fnv(h, (uint64_t)std::lround(an.frames[f].bass * 3) * 16 + std::lround(an.frames[f].high * 3));
        }
        an.fp.hash = h;
    }
    return an;
}

FrameFeatures Analysis::at(double t) const {
    if (frames.empty()) return {};
    double x = t / hop;
    if (x <= 0) return frames.front();
    size_t i = (size_t)x;
    if (i + 1 >= frames.size()) return frames.back();
    float a = (float)(x - i);
    const FrameFeatures &p = frames[i], &q = frames[i + 1];
    FrameFeatures r;
    for (int b = 0; b < NUM_BANDS; b++) r.bands[b] = p.bands[b] + (q.bands[b] - p.bands[b]) * a;
    r.bass = p.bass + (q.bass - p.bass) * a;
    r.mid = p.mid + (q.mid - p.mid) * a;
    r.high = p.high + (q.high - p.high) * a;
    r.loud = p.loud + (q.loud - p.loud) * a;
    r.onset = std::max(p.onset, q.onset);
    return r;
}

float Analysis::intensityAt(double t) const {
    if (intensity.empty()) return 0.3f;
    long i = std::clamp((long)(t / hop), 0L, (long)intensity.size() - 1);
    return intensity[i];
}

int Analysis::sectionAt(double t) const {
    int s = 0;
    for (size_t i = 0; i < fp.sections.size(); i++)
        if (t >= fp.sections[i]) s = (int)i;
    return s;
}

float Analysis::sectionProgress(double t) const {
    int s = sectionAt(t);
    double a = fp.sections.empty() ? 0.0 : fp.sections[s];
    double b = (s + 1 < (int)fp.sections.size()) ? fp.sections[s + 1] : fp.duration;
    return (float)std::clamp((t - a) / std::max(1e-3, b - a), 0.0, 1.0);
}

int Analysis::segmentAt(double t) const {
    int s = 0;
    for (size_t i = 0; i < segments.size(); i++)
        if (t >= segments[i].start) s = (int)i;
    return s;
}

double Analysis::beatPosition(double t) const {
    const double period = 60.0 / std::max(30.f, fp.bpm);
    if (beats.size() < 2) return (t - fp.beatOffset) / period;
    if (t <= beats.front()) return (t - beats.front()) / period;
    if (t >= beats.back()) return (beats.size() - 1) + (t - beats.back()) / period;
    size_t i = std::upper_bound(beats.begin(), beats.end(), t) - beats.begin() - 1;
    double a = beats[i], b = beats[i + 1];
    return i + (t - a) / std::max(1e-3, b - a);
}



