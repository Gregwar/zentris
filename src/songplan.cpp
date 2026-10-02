#include "songplan.hpp"

#include <algorithm>
#include <cmath>

#include "mathutil.hpp"

SongPlan planSong(const Analysis& an) {
    SongPlan plan;
    const double beat = 60.0 / std::max(40.f, an.fp.bpm);
    const double accentLen = std::clamp(8 * beat, 3.0, 6.0);
    int best = -1;
    for (size_t i = 0; i < an.segments.size(); i++) {
        const Segment& sg = an.segments[i];
        int level = 1;
        if (((sg.kind == SEG_CHORUS || sg.kind == SEG_DROP) && sg.energy >= 0.7f) || sg.energy >= 0.85f) level = 2;
        else if (sg.kind == SEG_INTRO || sg.kind == SEG_BREAK || sg.kind == SEG_OUTRO || sg.energy < 0.45f) level = 0;
        bool shortSeg = sg.end - sg.start < 8.0;
        if (!plan.phases.empty() && (shortSeg || plan.phases.back().level == level))
            plan.phases.back().end = sg.end;
        else
            plan.phases.push_back({sg.start, sg.end, level, (int)i, sg.energy});
        float prevE = i > 0 ? an.segments[i - 1].energy : sg.energy;
        bool lift = sg.kind == SEG_DROP || (sg.kind == SEG_CHORUS && sg.energy - prevE > 0.15f);
        if (level == 2 && sg.end - sg.start >= 8.0)
            plan.pulseZones.push_back({sg.start, sg.end});
        else if (lift && (plan.pulseZones.empty() || sg.start - plan.pulseZones.back().second > 30.0))
            plan.pulseZones.push_back({sg.start, sg.start + accentLen});
        if (i > 0 && (best < 0 || sg.energy > an.segments[best].energy)) best = (int)i;
    }
    if (plan.pulseZones.empty() && best > 0)
        plan.pulseZones.push_back({an.segments[best].start, an.segments[best].start + accentLen});
    if (plan.phases.empty()) plan.phases.push_back({0.0, an.fp.duration, 1, 0, 0.5f});
    return plan;
}

int SongPlan::phaseAt(double t) const {
    int p = 0;
    for (size_t i = 0; i < phases.size(); i++)
        if (t >= phases[i].start) p = (int)i;
    return p;
}

float SongPlan::pulseEnvelope(double t) const {
    float e = 0;
    for (auto& a : pulseZones) {
        if (t < a.first - 1.0 || t > a.second + 1.5) continue;
        float in = smoothstepf((float)(a.first - 1.0), (float)a.first + 0.5f, (float)t);
        float out = 1.f - smoothstepf((float)a.second - 0.5f, (float)a.second + 1.5f, (float)t);
        e = std::max(e, in * out);
    }
    return e;
}

StructureProfile structureProfile(const Analysis& an, double t) {
    StructureProfile p{0.8f, 0.85f, 0.9f, 0.95f};
    if (an.segments.empty()) return p;
    const Segment& sg = an.segments[an.segmentAt(t)];
    float prog = (float)std::clamp((t - sg.start) / std::max(1.0, sg.end - sg.start), 0.0, 1.0);
    switch (sg.kind) {
    case SEG_INTRO: p = {0.3f + 0.3f * prog, 0.45f + 0.2f * prog, 0.65f, 0.8f}; break;
    case SEG_VERSE: p = {0.7f, 0.8f, 0.9f, 0.95f}; break;
    case SEG_BUILD: {
        float k = std::pow(prog, 1.5f);
        p = {0.6f + 0.45f * k, 0.7f + 0.8f * k, 0.85f + 0.5f * k, 0.9f + 0.2f * k};
        break;
    }
    case SEG_CHORUS: p = {1.0f, 1.2f, 1.25f, 1.1f}; break;
    case SEG_DROP: p = {1.1f, 1.45f, 1.45f, 1.15f}; break;
    case SEG_BREAK: p = {0.3f, 0.4f, 0.7f, 0.75f}; break;
    case SEG_OUTRO: p = {0.6f - 0.4f * prog, 0.6f - 0.3f * prog, 0.85f - 0.3f * prog, 0.85f - 0.1f * prog}; break;
    }
    return p;
}

float gravityPace(const Analysis& an, double t) {
    if (an.segments.empty()) return 0.8f;
    switch (an.segments[an.segmentAt(t)].kind) {
    case SEG_BREAK: return 0.4f;
    case SEG_INTRO: case SEG_OUTRO: return 0.5f;
    case SEG_VERSE: return 0.8f;
    case SEG_BUILD: return 1.f;
    case SEG_CHORUS: return 1.2f;
    case SEG_DROP: return 1.3f;
    default: return 0.8f;
    }
}

float pulseTempoAmp(float bpm) { return 0.35f + 0.65f * smoothstepf(80.f, 128.f, bpm); }
float hitTempoAmp(float bpm) { return smoothstepf(100.f, 125.f, bpm); }
