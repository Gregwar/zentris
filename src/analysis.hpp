#pragma once
// Offline audio analysis: the song "footprint" that drives pace, colors and scene generation.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

constexpr int NUM_BANDS = 16;

// Per analysis frame, every value normalized to ~0..1 relative to the song itself.
struct FrameFeatures {
    std::array<float, NUM_BANDS> bands{};
    float bass = 0, mid = 0, high = 0; // grouped bands
    float loud = 0;                     // overall loudness
    float onset = 0;                    // spectral flux (transients)
};

struct Footprint {
    float bpm = 120;          // estimated tempo
    double beatOffset = 0;    // time of a reference beat (seconds)
    int key = 0;              // pitch class 0=C .. 11=B
    bool minor = false;
    float brightness = 0.5f;  // spectral centroid, 0 dark .. 1 bright
    float bassWeight = 0.5f;  // share of energy in low bands
    float airWeight = 0.5f;   // share of energy in high bands
    float dynamics = 0.5f;    // loudness variability
    float density = 0.5f;     // onset density (busy vs sparse)
    float duration = 0;
    uint64_t hash = 0;        // stable fingerprint of the song
    std::vector<double> sections; // section start times (seconds), first is 0
};

// Musical role of a song segment, inferred from its energy and its context.
enum SegmentKind { SEG_INTRO, SEG_VERSE, SEG_BUILD, SEG_CHORUS, SEG_DROP, SEG_BREAK, SEG_OUTRO, SEG_COUNT };
const char* segmentName(int kind);

struct Segment {
    double start = 0, end = 0;
    int kind = SEG_VERSE;
    int cluster = 0;       // segments that sound alike share a cluster (e.g. every chorus)
    float energy = 0.5f;   // mean intensity 0..1
    float rise = 0;        // intensity at the end minus at the start (builds are > 0)
    float brightness = 0;  // mean high-band level
    bool announced = false; // starts with a salient event (gap, bass cut, hit)
};

struct Analysis {
    double hop = 0.02;                  // seconds per frame
    std::vector<FrameFeatures> frames;
    std::vector<float> intensity;       // slow, section-level energy 0..1
    std::vector<double> beats;          // beat times (seconds)
    int downbeat = 0;                   // index (mod 4) of beats that start a bar
    std::vector<Segment> segments;      // song structure, covers the whole song
    Footprint fp;

    FrameFeatures at(double t) const;   // interpolated
    float intensityAt(double t) const;
    int sectionAt(double t) const;
    // 0..1 progress inside current section
    float sectionProgress(double t) const;
    int segmentAt(double t) const;
    // Continuous beat index at time t (fractional part = phase inside the beat).
    double beatPosition(double t) const;
};

// mono: mono samples at sampleRate. Heavy-ish: run it off the main thread.
Analysis analyzeAudio(const std::vector<float>& mono, uint32_t sampleRate);

std::string keyName(int key, bool minor);
