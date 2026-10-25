#pragma once
// The universe registry: every universe the game knows, how to make one, and how much it suits a song.
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "universe.hpp"

struct UniverseEntry {
    const char* name;      // upper case, shown in the HUD corner and given to --universe (any case)
    const char* blurb;     // one line: the look
    std::function<std::unique_ptr<Universe>()> make;
    // How well the universe suits a song (relative weight, ~1 neutral; 0 never picked for that song). Null: 1.
    std::function<float(const SongLook&)> affinity;
    std::vector<std::string> variants; // the variants' names (upper case), in index order (at least one)
    bool hidden = false;   // never picked at random (test universes); --universe and the viewer still reach it
};

namespace universes {
const std::vector<const UniverseEntry*>& all();
int count();
const UniverseEntry& entry(int i);
// Index of a universe by name (any case), -1 if unknown.
int find(const std::string& name);
// "NAME" or "NAME:VARIANT" (a variant name, any case, or its index): the index (-1 if unknown) and the variant
// (-1 if none or unknown).
int parse(const std::string& spec, int* variant);
// A universe for a song: weighted by affinity, never `current` again when another one can play (no immediate
// repeat), hidden universes left out.
int pick(const SongLook& look, uint64_t seed, int current);
// "VECTOR · P3 AMBER": a universe and one of its variants, as the HUD shows them (UTF-8 middle dot).
std::string label(int universe, int variant);
// The next universe in the registry order after `current` (hidden ones skipped unless `withHidden`).
int next(int current, bool withHidden = false);
} // namespace universes
