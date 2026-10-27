#pragma once
// SEA's world, shared by its simulation (sea.cpp) and its drawing (render.cpp): the sandy bed, the sea's levels, the
// palettes. sandH and crest are the same functions as in WORLD_GLSL (shaders.hpp).
#include <cmath>

#include "universes/sea/sea.hpp"

namespace sea_world {

using Palette = SeaUniverse::Palette;

constexpr float WELL_X = 5.1f;
// the diorama's cut face, just behind the board plane: the world lies behind it
constexpr float FRONT_Z = -1.0f;
// the sea's level: it starts under the lowest sand (all dry, dunes out to the horizon), and rises as water falls in:
// first a lake in the hollow behind the well, then it spills over the far plain out to the horizon and creeps up the
// beach, up to about three quarters of the well's height
constexpr float SEA_LOW = -14.6f, SEA_HIGH = 5.0f;
// how many poured lines fill the sea: it rises linearly, every line by the same amount (a Tetris pours 1.5 per line)
constexpr float SEA_FULL_LINES = 60.f;
// the first boats sail out once the sea is this high
constexpr float BOAT_LEVEL = -10.4f;
// gravity for the thrown cubes and the water (snappy, as before)
constexpr float GRAV = 22.f;
// clear water, almost colorless (linear)
const vec3 WATER{0.74f, 0.86f, 0.95f};
// the boats' lengths (their meshes, scale 1) and how deep they sit
constexpr float BOAT_LEN[6] = {1.6f, 3.2f, 4.6f, 10.f, 18.f, 22.f};

inline vec2 v2(float x, float y) { return vec2{x, y}; }
inline vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }

// the sandy bed: a beach under the well sloping gently down far out, a lake's hollow just behind the well (the lowest
// sand: the water gathers there first), low dunes over it (z <= FRONT_Z)
inline float sandH(float x, float z) {
    const float lake = std::exp(-(x * x / 576.f + (z + 15.f) * (z + 15.f) / 100.f));
    const float base = -9.2f - 1.8f * smoothstepf(3.f, 48.f, -z) - 5.2f * lake;
    const float k = (1.f - 0.4f * smoothstepf(10.f, 60.f, -z)) * (1.f - 0.8f * lake);
    const float d = 0.55f * std::sin(x * 0.21f + 1.3f + z * 0.11f) + 0.3f * std::sin(x * 0.53f + 0.4f - z * 0.27f) +
                    0.1f * std::sin(x * 1.37f + 2.f + z * 0.61f) + 0.35f * std::sin(x * 0.07f - z * 0.19f + 0.5f);
    return base + d * k;
}
inline vec3 sandN(float x, float z) {
    const float e = 0.05f;
    return normalize(vec3(sandH(x - e, z) - sandH(x + e, z), 2.f * e, sandH(x, z - e) - sandH(x, z + e)));
}
inline float crest(float s, float sharp) {
    const float c = s * 0.5f + 0.5f;
    return 2.f * std::pow(std::max(c, 0.f), 1.f + 2.5f * sharp) - 1.f;
}
inline float calmAt(float x, float z) {
    return 1.f - 0.75f * (1.f - smoothstepf(4.5f, 6.5f, std::fabs(x))) * (1.f - smoothstepf(4.f, 10.f, -z));
}

Palette paletteOf(int variant);
void lerpPalette(Palette& a, const Palette& b, float k);
int variantCount();
const char* variantName(int i);

} // namespace sea_world
