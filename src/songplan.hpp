#pragma once
// How the game stages a song from its analysis: scene levels, pulse zones and the per-section profile.
// Shared by the game and by zenscope (the analysis viewer) so both always agree.
#include <utility>
#include <vector>

#include "analysis.hpp"

struct ScenePhase {
    double start = 0, end = 0;
    int level = 1;      // 0 calm, 1 mid (the song's base scene), 2 peak
    int seg = 0;        // first analysis segment of the phase
    float energy = 0.5f;
};

struct SongPlan {
    std::vector<ScenePhase> phases;                   // a scene change happens at each phase start
    std::vector<std::pair<double, double>> pulseZones; // where beat pulses / hits are allowed
    int phaseAt(double t) const;
    float pulseEnvelope(double t) const;               // 0..1, smooth in/out
};

// Scene levels follow the song structure: calm (intros, breaks, quiet parts), mid, and peak (choruses,
// drops). Consecutive segments at the same level share one scene and very short segments never trigger a
// change. Pulses ride whole peak sections, plus short accents on other lifts.
SongPlan planSong(const Analysis& an);

// Slow multipliers from the current section (intro sparse, build ramping, drop full...). Targets: the
// game eases toward them.
struct StructureProfile {
    float density = 1, speed = 1, glow = 1, saturation = 1;
};
StructureProfile structureProfile(const Analysis& an, double t);
// Gravity pace in rows per beat (before level scaling): one constant per section kind.
float gravityPace(const Analysis& an, double t);

// Pulse strength grows with tempo; transient "hits" only for faster songs.
float pulseTempoAmp(float bpm);
float hitTempoAmp(float bpm);
