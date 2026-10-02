#pragma once
// One-shot samples cut from the song itself, beat-aligned, meant to answer line clears
// (1 line: a hit ... 4 lines: a bar of the song's hook). Shared so the game and zenscope agree.
#include <memory>
#include <string>
#include <vector>

#include "audio.hpp"

enum SampleKind { SMP_HIT, SMP_LOW, SMP_TONE, SMP_HOOK, SMP_COUNT };
const char* sampleName(int kind);

struct SongSample {
    int kind = SMP_HIT;
    double start = 0, end = 0; // seconds in the song
    int beats = 1;
    float score = 0;
    std::string why;           // what the heuristic liked about it (upper-case ASCII)
    std::shared_ptr<const std::vector<float>> pcm; // interleaved stereo with fades, at the track rate
};

// One sample per kind (fewer if the song is too short or has no beat grid).
std::vector<SongSample> extractSamples(const Track& t);
