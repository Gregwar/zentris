#include "theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

const char* BG_NAMES[BG_COUNT] = {"GRADIENT", "HALO", "HORIZON", "NEBULA", "VOID", "AURORA SKY", "BANDS", "SPOTLIGHT",
                                  "GRID", "HILLS", "CONIC", "STARFIELD", "DUAL GLOW", "WAVES", "CLOUDS", "POLKA",
                                  "DIAGONAL", "SUNBURST", "RIPPLES", "PLASMA", "SEA", "PEAKS", "SHAFTS", "HALO RING",
                                  "MOON", "RIDGELINES", "CITY", "COLOR MESH", "ECLIPSE", "CIRRUS", "CANYON",
                                  "SWIRL", "FOREST", "ISOMETRIC", "PLANET", "DESERT", "ARCTIC", "VOLCANO",
                                  "ROSE WINDOW", "TEMPLE", "JUNGLE", "PAPER CUT", "BLUEPRINT", "LIGHTHOUSE"};
const char* PS_NAMES[PS_COUNT] = {"GALAXY", "TUNNEL", "OCEAN", "SPHERE", "DRIFT", "STREAMS",
                                  "WARP", "CURTAINS", "HALOS", "HELIX", "BOKEH", "LATTICE",
                                  "FIREFLIES", "RAIN", "VORTEX", "WAVEFORM", "STARBURST", "ORBITS", "CONFETTI",
                                  "SNOWGLOBE", "LADDER", "FOUNTAIN", "PETALS", "CONSTELLATION", "TORUS", "WALL",
                                  "COMETS", "SPARKLERS", "BUBBLES", "CUBE", "INFINITY", "BEAT RINGS", "PLASMA",
                                  "MOIRE", "SWARM", "SPIRALS", "RIBBON", "METEORS", "SKYLINE", "CORONA", "DUNES", "WIND", "FIRE", "SEA",
                                  "FIREWORKS", "JELLYFISH", "DANDELION", "PENDULUMS", "KOI", "LANTERNS", "SNOWFALL",
                                  "GEARS", "SPIROGRAPH", "PULSE GRID", "BIRDS", "BUTTERFLIES", "ATOMS", "RAINDROPS",
                                  "EMBERS", "SUNFLOWER", "TENTACLES", "MANDALA", "SATELLITES", "NOTES"};
const char* BS_NAMES[BS_COUNT] = {"GLASS", "SOLID", "WIRE", "LANTERN", "INSET", "DOTS", "CRYSTAL", "SPLIT",
                                  "HOLO", "GRADIENT", "DOUBLE", "NEON", "CIRCUIT", "FROSTED", "CHECKER", "RINGS",
                                  "BEVEL", "PIXEL", "STRIPES", "CORE", "HATCH", "BREATH",
                                  "KINTSUGI", "TERRAZZO", "CANDY", "ENAMEL", "PILLOW", "SCALES", "WAFFLE", "OPAL",
                                  "STAINED", "PAPER", "GUMMY", "BRUSHED", "GLAZE", "MARBLE", "WOOD", "STITCH",
                                  "STUDS", "CARBON", "VELVET", "LED"};
const char* LE_NAMES[LE_COUNT] = {"POP", "AFTERGLOW", "BOUNCE", "SQUASH", "GROW", "PRESS", "TWINKLE", "RIPPLE",
                                  "CASCADE", "EMBER", "HUE SHIFT",
                                  "SHOCKWAVE", "SPARKS", "MAGNET", "JELLY", "FLIP", "INK", "SHIMMER", "FROST", "DUST",
                                  "HEARTBEAT", "SPLASH", "STAMP", "PETALS", "BLOOM", "ROW WAVE", "SPIN", "CONFETTI",
                                  "DRIP", "ECHO", "AURA"};
const char* MESH_NAMES[MESH_COUNT] = {"CUBE", "ROUNDED", "ORB", "GEM", "CHAMFER", "PILLOW", "TILE", "COIN", "OCTAGON",
                                      "HEX", "DIAMOND", "DOME", "CROSS", "STAR", "HEART", "DROP", "FLOWER", "RING",
                                      "PYRAMID", "CAPSULE", "SHIELD", "BLOB", "TRIANGLE", "ZIGGURAT"};
const char* MOOD_NAMES[4] = {"NIGHT", "DUSK", "PALE", "COLORFUL"};
const char* SURF_NAMES[SURF_COUNT] = {"", "SMOKE", "SILK", "LAVA", "CAUSTICS", "INK", "GEOMETRY", "AURORA", "FOG",
                              "BEAMS", "FLOW RINGS", "LIQUID", "SHADES", "VORONOI", "WATER", "FACETS", "HEXES",
                              "SHARDS", "MARBLE", "TOPOGRAPHY", "KALEIDOSCOPE", "RAIN RINGS", "TRUCHET", "WEAVE",
                              "HALFTONE", "SAND", "BRUSH", "PRISM", "OIL SLICK", "LACE", "MOSAIC", "ZEBRA",
                              "FROST", "CRACKLE", "LEAF SHADOWS", "DRIPS", "CIRCUITRY", "DAPPLE"};

vec3 ok(float L, float C, float h) { return oklchToLinear(L, C, h); }

// Mid/dark yellows and olives (OKLCH hue ~55..120 deg) read as muddy brown in large areas:
// push background hues out of that band.
float avoidMud(float h, float L) {
    if (L > 0.7f) return h;
    float x = std::fmod(h, TAU);
    if (x < 0) x += TAU;
    const float lo = 0.95f, hi = 2.1f;
    if (x > lo && x < hi) return (x - lo < hi - x) ? lo - 0.25f : hi + 0.25f;
    return h;
}


// A weighted pick that never lands on an option switched off (unless all of them are).
template <int N>
int pickEnabled(Rng& r, const float (&w)[N], int field, int offset = 0) {
    float v[N];
    bool any = false;
    for (int i = 0; i < N; i++) {
        v[i] = sceneOptionEnabled(field, i + offset) ? w[i] : 0.f;
        any |= v[i] > 0;
    }
    return any ? r.weighted(v) : r.weighted(w);
}

int pickBackground(Rng& r, const Footprint& fp, int mood) {
    float w[BG_COUNT] = {2.f, 1.8f, mood == 2 ? 0.4f : 1.2f, mood == 2 ? 0.f : 1.5f,
                         1.0f, mood == 2 ? 0.f : 1.0f + fp.airWeight, 1.0f, 1.0f,
                         mood == 2 ? 0.3f : 0.8f + 0.8f * fp.bassWeight, 1.0f, 0.9f, mood == 2 ? 0.f : 1.2f,
                         1.2f, 1.0f, 1.0f, 0.7f, 1.0f, 0.9f, 0.9f, mood == 2 ? 0.4f : 1.0f,
                         mood == 2 ? 0.5f : 1.0f, 0.9f, 1.0f, 1.0f,
                         1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                         1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    return pickEnabled(r, w, SF_BG);
}

float tempoNorm(const Footprint& fp) { return saturate((fp.bpm - 70.f) / 90.f); }

int pickParticleStyle(Rng& r, const Footprint& fp, int mood, int avoid) {
    const float bass = fp.bassWeight, air = fp.airWeight, bpmN = tempoNorm(fp);
    float w[PS_COUNT] = {
        1.2f + 0.5f * (1 - bpmN),         // galaxy
        0.6f + 1.4f * bass * bpmN + 0.3f, // tunnel
        0.8f + 1.4f * bass,               // ocean
        0.8f + 0.8f * bass,               // sphere
        0.8f + 1.4f * air * (1 - bpmN),   // drift
        1.0f + 0.6f * (1 - fp.density),   // streams
        0.3f + 1.4f * bpmN * air,         // warp
        mood == 2 ? 0.3f : 0.7f + air,    // curtains
        0.8f + 1.0f * bass,               // halos
        0.8f + 0.5f * fp.density,         // helix
        0.8f + 0.8f * (1 - fp.density),   // bokeh
        0.7f + 0.6f * fp.density,         // lattice
        0.9f + 0.8f * (1 - bpmN),         // fireflies
        0.5f + 0.8f * air,                // rain
        0.7f + 0.8f * bpmN,               // vortex
        0.8f + 0.8f * fp.density,         // waveform
        0.6f + 1.0f * bpmN,               // starburst
        0.8f + 0.4f * (1 - fp.density),   // orbits
        mood == 0 ? 0.5f : 0.9f,          // confetti
        0.8f,                             // snowglobe
        0.7f + 0.4f * fp.density,         // ladder
        0.6f + 0.8f * bpmN,               // fountain
        mood == 0 ? 0.5f : 1.0f,          // petals
        0.8f + 0.5f * (1 - fp.density),   // constellation
        0.8f + 0.4f * bass,               // torus
        0.7f + 0.7f * bass,               // wall
        0.5f + 0.8f * bpmN,               // comets
        0.5f + 0.8f * bpmN * fp.density,  // sparklers
        0.7f + 0.6f * air,                // bubbles
        0.8f,                             // cube shell
        0.8f,                             // infinity
        0.6f + 1.0f * bpmN,               // beat rings
        0.7f + 0.6f * bass,               // plasma
        0.6f,                             // moire
        0.8f + 0.5f * fp.density,         // swarm
        0.8f,                             // spirals
        mood == 2 ? 0.4f : 0.8f + 0.4f * air, // ribbon
        0.4f + 0.8f * bpmN,               // meteors
        0.8f + 0.5f * fp.density,         // skyline
        0.8f + 0.5f * bass,               // corona
        0.7f + 0.6f * bass,               // dunes
        0.6f + 0.8f * air,                // wind
        0.6f + 0.6f * bpmN,               // fire
        0.7f + 0.5f * (1 - bpmN),         // sea
        0.6f + 0.8f * bpmN,               // fireworks
        0.8f + 0.5f * (1 - bpmN),         // jellyfish
        0.8f + 0.6f * air,                // dandelion
        0.8f,                             // pendulums
        0.8f + 0.4f * (1 - bpmN),         // koi
        0.8f + 0.4f * (1 - fp.density),   // lanterns
        0.8f + 0.5f * air,                // snowfall
        0.8f + 0.4f * fp.density,         // gears
        0.8f,                             // spirograph
        0.7f + 0.6f * bass,               // pulse grid
        0.8f, 0.8f, 0.8f, 0.8f, 0.7f + 0.5f * bpmN, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, // birds .. notes
    };
    if (avoid >= 0) w[avoid] = 0;
    return pickEnabled(r, w, SF_PART1, 1);
}

int pickShape(Rng& r, int style) {
    switch (style) {
    case PS_WARP: return r.chance(0.8f) ? (int)SH_STREAK : (int)SH_DOT;
    case PS_BOKEH: return r.chance(0.6f) ? (int)SH_DISC : (r.chance(0.5f) ? (int)SH_RING : (int)SH_DOT);
    case PS_RAIN:
    case PS_METEORS:
    case PS_COMETS: return (int)SH_STREAK;
    case PS_BUBBLES: return r.chance(0.6f) ? (int)SH_RING : (int)SH_DOUBLERING;
    case PS_PETALS: { float w[SH_COUNT] = {0.5f, 0, 0, 0, 0.5f, 0, 0, 0, 0.5f, 0, 0.5f, 1.2f, 1.2f, 0, 0, 0, 0.5f, 0, 0, 0}; return r.weighted(w); }
    case PS_CONFETTI: { float w[SH_COUNT] = {0, 0, 0, 2, 1.5f, 0, 0, 0.5f, 1, 1, 1, 0.5f, 0, 0.5f, 0, 0.5f, 0.5f, 0, 0.5f, 0}; return r.weighted(w); }
    case PS_SKYLINE: return r.chance(0.5f) ? (int)SH_SQUARE : (int)SH_DOT;
    case PS_WIND: return (int)SH_STREAK;
    case PS_FIRE: return r.chance(0.75f) ? (int)SH_DOT : (int)SH_SPARKLE;
    case PS_CORONA:
    case PS_SEA:
    case PS_DUNES: return (int)SH_DOT;
    case PS_FIREFLIES: return r.chance(0.7f) ? (int)SH_DOT : (int)SH_SPARKLE;
    case PS_FIREWORKS: return r.chance(0.75f) ? (int)SH_DOT : (int)SH_SPARKLE;
    case PS_JELLYFISH:
    case PS_DANDELION:
    case PS_KOI:
    case PS_LANTERNS:
    case PS_SNOWFALL:
    case PS_PENDULUMS:
    case PS_SPIROGRAPH:
    case PS_BIRDS:
    case PS_BUTTERFLIES:
    case PS_ATOMS:
    case PS_RAINDROPS:
    case PS_SUNFLOWER:
    case PS_TENTACLES:
    case PS_MANDALA:
    case PS_SATELLITES:
    case PS_NOTES: return (int)SH_DOT;
    case PS_EMBERS: return r.chance(0.8f) ? (int)SH_DOT : (int)SH_SPARKLE;
    case PS_GEARS: return r.chance(0.7f) ? (int)SH_DOT : (int)SH_SQUARE;
    case PS_PULSEGRID: { float w[SH_COUNT] = {3, 0.6f, 0, 0.8f, 0.6f, 0, 0, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.4f}; return r.weighted(w); }
    case PS_LATTICE: { float w[SH_COUNT] = {2, 0.5f, 0.5f, 1.5f, 1, 0, 0, 1.5f, 0.5f, 1, 0.5f, 0, 0, 1, 0.3f, 1, 1, 0.3f, 0.3f, 0.5f}; return r.weighted(w); }
    case PS_TUNNEL: { float w[SH_COUNT] = {2, 0.5f, 0.5f, 0.8f, 0.8f, 0, 1.5f, 0.5f, 0.3f, 0.5f, 0.3f, 0, 0, 0.3f, 0.3f, 0.5f, 0.5f, 0.5f, 0.8f, 0.3f}; return r.weighted(w); }
    default: { float w[SH_COUNT] = {3, 0.7f, 1.0f, 0.6f, 0.8f, 0.3f, 0.4f, 0.5f, 0.5f, 0.5f, 0.4f, 0.2f, 0.3f, 0.4f, 0.4f, 0.4f, 0.4f, 0.6f, 0.3f, 0.5f}; return r.weighted(w); }
    }
}

void makeLayer(Rng& r, const Footprint& fp, int, ParticleLayer& L, int style, bool secondary) {
    const float bpmN = tempoNorm(fp);
    L.style = style;
    L.shape = pickShape(r, style);
    L.count = secondary ? r.range(0.12f, 0.35f) : r.range(0.45f, 1.0f) * (0.7f + 0.5f * fp.density);
    // Detailed shapes (crescents, stars, outlines...) get busy in numbers: fewer of them.
    const bool detailed = !(L.shape == SH_DOT || L.shape == SH_RING || L.shape == SH_DISC || L.shape == SH_STREAK);
    if (detailed) L.count *= 0.65f;
    L.size = r.range(0.6f, 1.5f) * (secondary ? 0.8f : 1.f);
    L.speed = r.range(0.6f, 1.4f) * (0.7f + 0.6f * bpmN);
    L.bright = r.range(0.6f, 1.1f) * (secondary ? 0.8f : 1.f);
    for (float& v : L.p) v = r.uniform();
}

std::string themeName(const Theme& t) {
    const int mood = (int)std::lround(t.pal.mood);
    std::string parts = t.layerCount > 0 ? std::string(PS_NAMES[t.layers[0].style]) : std::string("NO PARTICLES");
    if (t.layerCount > 1) parts += std::string(" + ") + PS_NAMES[t.layers[1].style];
    if (t.surfStyle > 0) parts += std::string(" + ") + SURF_NAMES[t.surfStyle];
    return std::string(MOOD_NAMES[mood]) + " / " + BS_NAMES[t.blockStyle] + " " + MESH_NAMES[t.blockMesh] + " / " +
           parts + " / " + BG_NAMES[t.bgStyle];
}

} // namespace

void resolvePalette(Theme& t, float hueShift) {
    t.hueShift = hueShift;
    const PaletteParams& p = t.pal;
    const float h = p.hue + hueShift;
    const int mood = (int)std::lround(p.mood);
    Rng r(p.perm);

    float hues[7], Ls[7], Cs[7];
    // Darker colors can be more saturated (light ones leave the screen's gamut): pieces stay mid-light.
    float baseL = mood == 0 ? 0.68f : (mood == 1 ? 0.74f : (mood == 3 ? 0.72f : 0.60f)); // glow adds light: keep colors deep enough
    float C = p.chroma;
    // On a pale background low-chroma colors read as gray: keep pale themes clearly colored (calm sections too).
    if (mood == 2) C = std::min(0.24f, std::max(0.13f, C * 1.3f));
    if (mood == 3) C = 0.32f; // pieces as saturated as the screen allows (clipped to the gamut's edge)
    for (int i = 0; i < 7; i++) {
        float f = (float)i / 6.f;
        float jitter = r.range(-0.04f, 0.04f);
        // Pieces spread in lightness, so they stand apart even when their hues are close.
        Ls[i] = baseL + (f - 0.5f) * 0.14f + r.range(-0.03f, 0.03f);
        Cs[i] = C;
        switch (p.scheme) {
        // Analogous: neighbouring hues over 90 .. 110 degrees (any narrower and the seven pieces read as one color).
        case 0: hues[i] = h + (i - 3) * (0.26f + 0.15f * (p.spread - 0.15f)); break;
        case 1: hues[i] = h + (i % 2 ? PI : 0.f) + (i / 2) * 0.12f - 0.18f; break;        // complementary
        case 2: hues[i] = h + (i % 3) * TAU / 3.f + (i / 3) * 0.14f; break;                 // triadic
        case 3:                                                                               // monochrome
            hues[i] = h + jitter;
            Ls[i] = baseL + (f - 0.5f) * (mood == 2 ? 0.3f : 0.24f);
            Cs[i] = C * (mood == 2 ? 0.85f + 0.4f * f : 0.5f + 0.7f * f); // low chroma reads as gray on pale
            break;
        case 4: hues[i] = h + f * p.spread * 5.f; break;                                     // duotone ramp
        case 5: hues[i] = h + i * TAU / 7.f; Cs[i] = C * 0.62f; break;                      // pastel rainbow
        case 6: hues[i] = h + ((i % 3) - 1) * 2.6f + (i / 3) * 0.1f; break;                 // split complement
        // Added later (disabled until reviewed). OKLCH hues: ~0 pink/red, 0.7 orange, 1.7 yellow, 2.6 green,
        // 3.4 cyan, 4.4 blue, 5.3 violet.
        case 8:                                                                               // sunset ramp
            hues[i] = h + 0.3f - f * 1.7f;                                                    // warm, light -> cool, deep
            Ls[i] = baseL + (0.5f - f) * 0.18f;
            Cs[i] = C * 1.1f;
            break;
        case 9:                                                                               // jewel tones
            hues[i] = h + i * TAU / 7.f;
            Ls[i] = baseL - (mood == 2 ? 0.06f : 0.1f) + (f - 0.5f) * 0.08f;
            Cs[i] = std::min(0.25f, C * 1.4f);
            break;
        case 10:                                                                              // neon pair
            hues[i] = h + (i % 2 ? 2.4f : 0.f) + (i / 2) * 0.08f;
            Cs[i] = std::min(0.27f, C * 1.5f);
            break;
        case 11: {                                                                            // earth
            const float earth[7] = {0.45f, 0.75f, 1.15f, 2.35f, 0.25f, 1.6f, 2.9f};          // terracotta .. sage
            hues[i] = earth[i] + 0.15f * std::sin(h);
            Ls[i] = baseL + (f - 0.5f) * 0.26f;
            Cs[i] = C * 0.7f;
            break;
        }
        case 12:                                                                              // ice
            hues[i] = 3.3f + f * 1.9f + 0.15f * std::sin(h);                                  // cyan .. lavender
            Ls[i] = baseL + 0.05f + (f - 0.5f) * 0.14f;
            Cs[i] = C * 0.8f;
            break;
        case 13: {                                                                            // candy
            const float candy[7] = {0.1f, 2.8f, 1.75f, 4.1f, 5.3f, 0.8f, 3.5f};              // pink, mint, lemon...
            hues[i] = candy[i] + 0.1f * std::sin(h);
            Ls[i] = (mood == 2 ? 0.72f : 0.86f) + (f - 0.5f) * 0.06f;
            Cs[i] = 0.11f;
            break;
        }
        case 14: hues[i] = h + (i % 4) * TAU / 4.f + (i / 4) * 0.15f; break;                // tetradic
        case 15: hues[i] = h + i * 2.39996f; break;                                          // golden angle
        case 16:                                                                              // twilight
            hues[i] = 4.2f + f * 1.9f + 0.1f * std::sin(h);                                   // blue .. magenta
            Ls[i] = baseL + (f - 0.5f) * 0.18f;
            Cs[i] = C * 1.1f;
            break;
        case 17:                                                                              // forest
            hues[i] = i == 6 ? h : 2.4f + f * 1.0f + 0.1f * std::sin(h);                     // greens, teals, one accent
            Ls[i] = baseL + (f - 0.5f) * 0.22f;
            Cs[i] = C * (i == 6 ? 1.2f : 0.9f);
            break;
        // Fixed-hue palettes (the scene's hue only nudges them): OKLCH hues ~0 pink/red, 0.7 orange, 1.7 yellow,
        // 2.6 green, 3.4 cyan, 4.4 blue, 5.3 violet.
        case 18: {                                                                            // aurora
            const float hs[7] = {2.6f, 2.9f, 3.3f, 3.7f, 4.6f, 5.1f, 5.5f};                  // green, teal, violet
            hues[i] = hs[i] + 0.12f * std::sin(h);
            Ls[i] = baseL + (f - 0.5f) * 0.16f;
            break;
        }
        case 19: {                                                                            // coral reef
            const float hs[7] = {0.35f, 0.55f, 3.25f, 3.45f, 1.25f, 0.15f, 3.7f};            // coral, turquoise, sand
            hues[i] = hs[i] + 0.1f * std::sin(h);
            if (i == 4) Cs[i] = C * 0.7f;
            break;
        }
        case 20:                                                                              // sunrise
            hues[i] = 6.0f + f * 1.6f + 0.1f * std::sin(h);                                    // pink, peach, gold
            Ls[i] = baseL + f * 0.12f - 0.04f;
            break;
        case 21:                                                                              // deep sea
            hues[i] = 4.6f - f * 1.4f + 0.1f * std::sin(h);                                    // navy .. aqua
            Ls[i] = baseL - 0.08f + f * 0.2f;
            break;
        case 22: {                                                                            // retro
            const float hs[7] = {1.45f, 3.3f, 0.6f, 1.2f, 3.6f, 0.4f, 1.7f};                  // mustard, teal, burnt orange
            hues[i] = hs[i] + 0.08f * std::sin(h);
            Cs[i] = C * 0.85f;
            Ls[i] = baseL + ((i % 3) - 1) * 0.06f;
            break;
        }
        case 23:                                                                              // citrus
            hues[i] = 0.75f + f * 1.6f + 0.08f * std::sin(h);                                  // orange, lemon, lime
            Ls[i] = baseL + 0.04f + (f - 0.5f) * 0.1f;
            break;
        case 24: {                                                                            // berry
            const float hs[7] = {6.1f, 5.6f, 4.6f, 0.15f, 5.2f, 4.3f, 5.9f};                  // raspberry, plum, blueberry
            hues[i] = hs[i] + 0.1f * std::sin(h);
            Ls[i] = baseL - 0.04f + (f - 0.5f) * 0.14f;
            break;
        }
        case 25: {                                                                            // vaporwave
            const float hs[7] = {5.9f, 3.4f, 5.2f, 3.7f, 6.2f, 4.9f, 3.2f};                   // pink, cyan, purple
            hues[i] = hs[i] + 0.1f * std::sin(h);
            break;
        }
        case 26: {                                                                            // autumn
            const float hs[7] = {0.5f, 0.85f, 0.15f, 1.3f, 0.65f, 6.1f, 1.05f};               // rust, amber, maroon, gold
            hues[i] = hs[i] + 0.08f * std::sin(h);
            Ls[i] = baseL - 0.06f + (f - 0.5f) * 0.16f;
            break;
        }
        case 27: {                                                                            // primary
            const float hs[7] = {0.5f, 1.75f, 4.5f, 0.45f, 1.8f, 4.4f, 0.1f};                 // red, yellow, blue
            hues[i] = hs[i];
            Cs[i] = std::min(0.26f, C * 1.4f);
            if (i % 3 == 1) Ls[i] = std::max(Ls[i], 0.86f);                                   // yellows need light
            break;
        }
        default:                                                                              // ink + accent
            hues[i] = h + jitter;
            // Ink on pale backgrounds; on dark ones a soft single tint (near-white glowing blocks
            // wash out to white).
            Cs[i] = mood == 2 ? 0.025f + 0.02f * (i % 2) : 0.07f + 0.02f * (i % 2);
            Ls[i] = (mood == 2 ? 0.35f : 0.68f) - 0.05f * (i % 3);
            break;
        }
        hues[i] += jitter;
        if (mood == 2) hues[i] = avoidMud(hues[i], Ls[i]); // mid-lightness olives read as mud on pale themes
        // Yellows are light, vivid colors: at the pieces' usual mid lightness they turn mustard, khaki and olive on
        // dark backgrounds. Around yellow (orange and green keep theirs), lift lightness and chroma: lemon and lime.
        else if (mood <= 1) {
            const float w = std::max(0.f, 1.f - std::fabs(std::remainder(hues[i] - 1.85f, TAU)) / 0.7f);
            Ls[i] = std::max(Ls[i], 0.68f + 0.24f * w);
            Cs[i] *= 1.f + 0.4f * w;
        }
    }
    // Permute which piece gets which color.
    int idx[7] = {0, 1, 2, 3, 4, 5, 6};
    for (int i = 6; i > 0; i--) std::swap(idx[i], idx[r.next() % (i + 1)]);
    for (int i = 0; i < 7; i++) t.piece[i] = ok(Ls[idx[i]], Cs[idx[i]], hues[idx[i]]);

    const float bh = avoidMud(h + p.bgHueOffset, mood == 2 ? 0.9f : 0.5f);
    float accentH = p.scheme == 7 ? h : h + r.range(-0.6f, 0.6f);
    if (mood == 0) {
        float lt = 0.07f + t.bgP[0] * 0.07f, lb = 0.13f + t.bgP[1] * 0.1f;
        t.bgTop = ok(lt, p.bgChroma, bh);
        t.bgBottom = ok(lb, p.bgChroma * 1.2f, avoidMud(bh + 0.35f * (t.bgP[2] - 0.5f), lb));
        t.bgGlow = ok(0.42f + 0.1f * t.bgP[3], std::min(0.16f, p.bgChroma * 2.5f + 0.03f), avoidMud(accentH, 0.45f));
        t.accent = ok(0.82f, 0.13f, accentH);
        t.text = ok(0.93f, 0.02f, h);
        t.partA = ok(0.78f, C * 1.2f, h);
        t.partB = ok(0.75f, 0.14f, accentH + 0.5f);
        t.partC = ok(0.84f, 0.09f, bh + 0.4f); // tinted: additive near-white piles up to white
        t.pale = 0;
    } else if (mood == 3) {
        // Colorful (added later, disabled until reviewed): a vivid mid-lightness background whose hue is opposite
        // the pieces', so they stand out; light pieces and particles.
        // Deep, saturated and two-hued (top and bottom hues 0.5 .. 1.6 rad apart, either way), for contrast with the
        // light pieces and variety between scenes.
        // Pop: bright, as saturated as the screen allows (chroma beyond the gamut is clipped to its edge), two hues
        // far apart.
        // The background is a neighbouring hue family of the pieces' (not their opposite: a vivid complement,
        // red pieces on blue, clashes at this saturation), its two hues turning away from the pieces' one.
        // Centered on the pieces' mean hue (weighted by chroma: multi-hue palettes spread far from h).
        float mx = 0, my = 0;
        for (int i = 0; i < 7; i++) mx += Cs[i] * std::cos(hues[i]), my += Cs[i] * std::sin(hues[i]);
        const float mean = std::atan2(my, mx);
        const float side = p.perm & 1 ? 1.f : -1.f;
        const float ch = avoidMud(mean + side * (0.2f + 0.3f * t.bgP[2]), 0.55f);
        const float bc = 0.3f + p.bgChroma;
        const float lt = 0.52f + t.bgP[0] * 0.12f, lb = 0.4f + t.bgP[1] * 0.12f;
        const float turn = side * (0.2f + 0.3f * t.bgP[3]);
        t.bgTop = ok(lt, bc, ch);
        t.bgBottom = ok(lb, bc * 1.1f, avoidMud(ch + turn, lb));
        // Glows, clouds and particles are colored, not light: a light glow would haze the whole scene.
        t.bgGlow = ok(0.72f, 0.3f, avoidMud(ch - 0.5f, 0.72f));
        t.accent = ok(0.92f, 0.1f, accentH);
        t.text = ok(0.98f, 0.02f, h);
        t.partA = ok(0.88f, 0.16f, ch + 0.3f);
        t.partB = ok(0.84f, 0.2f, ch - 0.9f);
        t.partC = ok(0.93f, 0.1f, ch + 1.6f);
        t.pale = 0.25f; // dusk's blending (a dark board well); a lower emissive keeps blocks from washing out
    } else if (mood == 1) {
        float lt = 0.26f + t.bgP[0] * 0.1f, lb = 0.42f + t.bgP[1] * 0.14f;
        t.bgTop = ok(lt, p.bgChroma * 1.5f + 0.02f, bh);
        t.bgBottom = ok(lb, p.bgChroma * 1.8f + 0.03f, avoidMud(bh + 0.8f * (t.bgP[2] - 0.3f), lb));
        t.bgGlow = ok(0.8f, 0.09f, accentH);
        t.accent = ok(0.9f, 0.08f, accentH);
        t.text = ok(0.96f, 0.02f, h);
        t.partA = ok(0.88f, C * 0.8f, h);
        t.partB = ok(0.85f, 0.1f, accentH + 0.5f);
        t.partC = ok(0.9f, 0.07f, bh + 0.4f);
        t.pale = 0.25f;
    } else {
        // Faint backdrop and particles: pale pieces are soft, a vivid backdrop took the attention from the game.
        t.bgTop = ok(0.93f + t.bgP[0] * 0.04f, p.bgChroma * 0.35f, bh);
        t.bgBottom = ok(0.83f + t.bgP[1] * 0.06f, p.bgChroma * 0.5f + 0.006f, bh + 0.5f * (t.bgP[2] - 0.5f));
        t.bgGlow = ok(0.9f, 0.07f, avoidMud(accentH, 0.6f)); // tinted, so energy glow shows on pale
        t.accent = ok(0.5f, 0.12f, accentH);
        t.text = ok(0.3f, 0.03f, h);
        t.partA = ok(0.66f, std::min(0.06f, C * 0.45f), avoidMud(h, 0.66f));
        t.partB = ok(0.72f, 0.05f, avoidMud(accentH + 0.5f, 0.66f));
        t.partC = ok(0.68f, 0.05f, avoidMud(bh + 0.4f, 0.68f)); // tinted: near-white vanishes into the light background
        t.pale = 1;
    }
    {
        // The board backing of pale scenes: a light tint of the accent's hue (the frame's), so the board reads as a
        // panel of the scene's colors rather than the same white everywhere. Light yellows and yellow-greens turn
        // khaki there (the scene's exposure dims the backing): they lean to the nearest of salmon and mint.
        float wh = std::fmod(accentH, TAU);
        if (wh < 0) wh += TAU;
        if (wh > 0.9f && wh < 2.6f) wh = wh < 1.75f ? 0.6f : 2.9f;
        t.wellTint = ok(0.97f, 0.045f, wh);
    }
    t.shadowTint = ok(0.5f, 0.12f, h + PI + r.range(-0.8f, 0.8f));
    t.highlightTint = ok(0.8f, 0.1f, h + r.range(-0.5f, 0.5f));
}

float meshExponent(int mesh, float roundness) {
    switch (mesh) {
    case MESH_GEM: return 1.35f; // a full, rounded diamond (a sharp octahedron leaves cells touching at tips)
    case MESH_SPHERE: return 2.f;
    case MESH_ROUNDED: return roundness;
    // Profile meshes are not superellipsoids: the nearest one, which a superellipsoid morphs toward in a
    // transition before the shape switches.
    case MESH_PILLOW: return 4.f;
    case MESH_TILE: return 7.f;
    case MESH_COIN: case MESH_DOME: return 2.5f;
    case MESH_OCTAGON: case MESH_HEX: return 5.f;
    case MESH_DIAMOND: case MESH_STAR: return 1.35f;
    case MESH_HEART: case MESH_DROP: case MESH_FLOWER: case MESH_BLOB: return 2.5f;
    case MESH_CAPSULE: case MESH_TRIANGLE: return 2.f;
    case MESH_RING: case MESH_SHIELD: return 4.f;
    case MESH_PYRAMID: return 7.f;
    case MESH_ZIGGURAT: return 10.f;
    default: return 24.f;
    }
}

// The range of block scales of a mesh, when it is not the usual 0.8 .. 0.96.
static bool meshScaleRange(int mesh, float& a, float& b) {
    switch (mesh) {
    case MESH_SPHERE: a = 0.85f, b = 1.f; return true;
    case MESH_GEM: a = 0.95f, b = 1.05f; return true; // diamonds leave gaps: draw them larger
    case MESH_COIN: a = 0.86f, b = 1.f; return true;
    case MESH_HEX: a = 0.84f, b = 0.9f; return true; // tips stay clear of the rows above and below
    case MESH_DIAMOND: a = 0.8f, b = 0.87f; return true; // tips nearly meet
    case MESH_CROSS: a = 0.86f, b = 0.98f; return true; // arms nearly meet
    case MESH_STAR: a = 0.86f, b = 0.94f; return true; // side points stay clear of the neighbours'
    case MESH_HEART: case MESH_FLOWER: a = 0.86f, b = 0.96f; return true;
    case MESH_DROP: case MESH_BLOB: a = 0.86f, b = 0.96f; return true;
    case MESH_CAPSULE: a = 0.86f, b = 0.94f; return true; // ends stay clear of the cells above and below
    case MESH_TRIANGLE: a = 0.84f, b = 0.92f; return true; // bottom corners stay clear of the neighbours'
    case MESH_RING: case MESH_SHIELD: a = 0.8f, b = 0.92f; return true;
    case MESH_PYRAMID: case MESH_ZIGGURAT: a = 0.8f, b = 0.88f; return true; // square bases: a clear gap
    default: return false;
    }
}

// ---- Scene codes. A scene is its discrete identity, one byte per field in SceneField order (mood, palette, hue,
// background, particles, particles 2, surface, blocks, shape, frame, lock effect), 2 hex digits each. Everything
// else (exact colors, particle counts and speeds, glow, camera...) comes from a seed made of the identity itself,
// so a code always gives the same scene, whatever the song.

std::string sceneCode(const SceneId& id) {
    std::string c;
    char b[4];
    for (int f = 0; f < SF_COUNT; f++) {
        std::snprintf(b, sizeof(b), "%02x", id.v[f] & 0xff);
        c += b;
    }
    return c;
}

bool parseSceneCode(const std::string& code, SceneId& id) {
    if (code.size() != 2 * SF_COUNT) return false;
    for (int f = 0; f < SF_COUNT; f++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            const char ch = (char)std::tolower((unsigned char)code[2 * f + k]);
            if (ch >= '0' && ch <= '9') v = v * 16 + (ch - '0');
            else if (ch >= 'a' && ch <= 'f') v = v * 16 + (ch - 'a' + 10);
            else return false;
        }
        if (v >= sceneFieldValues(f)) return false;
        id.v[f] = v;
    }
    if (id.v[SF_PART1] == 0) id.v[SF_PART2] = 0; // a second layout only with a first one
    return true;
}

SceneId sceneId(const Theme& t) {
    SceneId id;
    for (int f = 0; f < SF_COUNT; f++) id.v[f] = sceneFieldValue(t, f);
    return id;
}

// The identity picks. The song steers the weights (key -> hue, brightness -> mood, bass / air / tempo -> layouts...);
// options switched off are never picked.
SceneId pickIdentity(const Footprint& fp, uint64_t seed) {
    Rng r(seed);
    SceneId id;
    // Key on the circle of fifths gives the base hue (synesthetic mapping), within about +-30 degrees; without a
    // song, any hue.
    const int fifths = (fp.key * 7) % 12;
    id.v[SF_HUE] = fp.hash ? ((fifths * 2 + r.irange(-2, 2)) % 24 + 24) % 24 : r.irange(0, 23);
    float moodW[4] = {0.62f - 0.25f * fp.brightness + (fp.minor ? 0.12f : 0.f), 0.2f + 0.2f * fp.brightness,
                      0.08f + 0.22f * fp.brightness * (1.f - 0.5f * fp.bassWeight), 0.12f + 0.2f * fp.brightness};
    const int mood = pickEnabled(r, moodW, SF_MOOD);
    id.v[SF_MOOD] = mood;
    // Muted schemes (monochrome, pastel rainbow, ink + accent) are rarer: scenes read as colorful first.
    float schemeW[SCHEME_COUNT] = {3.f, 2.f, 1.2f, 0.8f, 2.6f, 0.4f + 0.5f * fp.brightness, 1.5f, 0.35f + (mood == 0 ? 0.25f : 0.f),
                                   1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f,
                                   1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f};
    id.v[SF_SCHEME] = pickEnabled(r, schemeW, SF_SCHEME);
    const int bg = pickBackground(r, fp, mood);
    id.v[SF_BG] = bg;
    int p1 = pickParticleStyle(r, fp, mood, -1), p2 = -1;
    if (r.chance(0.55f)) p2 = pickParticleStyle(r, fp, mood, p1);
    // Continuous surface layer in about two thirds of the scenes; some of those drop particles entirely.
    int surf = 0;
    if (r.chance(0.65f)) {
        float sw[SURF_COUNT] = {0, 1.3f, 1.1f, 0.9f, mood == 2 ? 0.5f : 1.f, 1.f, 1.f, mood == 2 ? 0.3f : 1.f, 1.1f,
                                mood == 2 ? 0.3f : 0.9f, 0.9f, 1.f, 1.1f, 1.1f, 1.f, 1.f, 0.9f, 1.f,
                                1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                                1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
        // Flat tilings (voronoi, facets, hexes...) and a second waterline clash with a landscape's floor or horizon.
        auto floorStyle = [](int st) { return st == PS_WAVES || st == PS_DUNES || st == PS_SEA; };
        const bool landscape = bg == BG_HORIZON || bg == BG_GRID || bg == BG_HILLS || bg == BG_SEA || bg == BG_PEAKS ||
                               floorStyle(p1) || floorStyle(p2);
        if (landscape) sw[13] = sw[14] = sw[15] = sw[16] = 0.f;
        if (landscape) sw[22] = sw[23] = sw[24] = sw[25] = 0.f; // truchet, weave, halftone, sand: flat textures too
        if (landscape) sw[29] = sw[30] = sw[33] = sw[36] = 0.f; // lace, mosaic, crackle, circuitry
        surf = pickEnabled(r, sw, SF_SURFACE);
        if (r.chance(0.3f)) p1 = p2 = -1;
        else if (p2 >= 0 && r.chance(0.5f)) p2 = -1;
    }
    id.v[SF_PART1] = p1 + 1;
    id.v[SF_PART2] = p1 >= 0 ? p2 + 1 : 0;
    id.v[SF_SURFACE] = surf;
    float w[BS_COUNT] = {mood == 2 ? 0.8f : 3.f, mood == 2 ? 2.5f : 1.2f, 1.2f, mood == 2 ? 0.3f : 1.3f,
                         1.3f, 0.9f, 1.3f, 1.0f, mood == 2 ? 0.4f : 1.1f, 1.1f, 1.0f,
                         mood == 2 ? 0.4f : 1.1f, 0.9f, 1.0f, 0.9f, 0.9f, 1.1f, 0.9f, 0.9f,
                         mood == 2 ? 0.5f : 1.0f, 0.8f, 0.9f,
                         1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                         1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    const int block = pickEnabled(r, w, SF_BLOCK);
    id.v[SF_BLOCK] = block;
    float mw[MESH_COUNT] = {5.5f, 2.5f, 1.0f, 0.5f, // gems read less clearly as pieces: rarer
                            1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                            1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    const int faceStyles[] = {BS_DOTS, BS_INSET, BS_SPLIT, BS_DOUBLE, BS_CIRCUIT, BS_CHECKER, BS_RINGS, BS_PIXEL, BS_HATCH,
                              BS_STRIPES, BS_KINTSUGI, BS_TERRAZZO, BS_ENAMEL, BS_SCALES, BS_WAFFLE, BS_STAINED,
                              BS_PAPER, BS_BRUSHED, BS_MARBLE, BS_WOOD, BS_STITCH, BS_STUDS, BS_CARBON, BS_LED};
    for (int fsIdx : faceStyles)
        if (block == fsIdx) { // face patterns need flat faces (and wide ones: a star's points cut them)
            mw[MESH_SPHERE] = mw[MESH_GEM] = mw[MESH_PILLOW] = mw[MESH_DOME] = mw[MESH_STAR] = 0;
            mw[MESH_FLOWER] = mw[MESH_RING] = mw[MESH_PYRAMID] = mw[MESH_CAPSULE] = mw[MESH_BLOB] = mw[MESH_TRIANGLE] =
                mw[MESH_ZIGGURAT] = 0;
        }
    id.v[SF_MESH] = pickEnabled(r, mw, SF_MESH);
    float fw[FR_COUNT] = {2.f, 1.5f, 1.5f, 1.2f, 1.0f, 0.8f, 1.0f, 1.0f, 1.0f,
                          1.0f, 1.0f, 1.0f, 1.0f, 0.8f, 0.8f, 1.0f, 1.0f, 0.8f,
                          1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                          1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    id.v[SF_FRAME] = pickEnabled(r, fw, SF_FRAME);
    float lw[LE_COUNT];
    for (float& v : lw) v = 1.f;
    id.v[SF_LOCK] = pickEnabled(r, lw, SF_LOCK);
    return id;
}

// The identity's seed: the scene's details depend on its choices only.
static uint64_t identitySeed(const SceneId& id) {
    uint64_t h = 0x5CE7E1DE7ull;
    for (int f = 0; f < SF_COUNT; f++) h = splitmix64(h ^ ((uint64_t)id.v[f] << 8 | (uint64_t)f));
    return h;
}

Theme buildTheme(const SceneId& id) {
    Theme t;
    t.seed = identitySeed(id);
    t.code = sceneCode(id);
    Rng r(t.seed);
    const Footprint fp; // neutral: details don't depend on the song (the music drives them at run time)
    const int mood = id.v[SF_MOOD];

    // ---- Palette.
    t.pal.mood = (float)mood;
    t.pal.scheme = id.v[SF_SCHEME];
    t.pal.hue = id.v[SF_HUE] * TAU / 24.f;
    t.pal.chroma = r.range(0.14f, 0.22f);
    t.pal.spread = r.range(0.15f, 0.5f);
    // The background takes the pieces' hue or a contrasting one. When the pieces already share one hue (analogous,
    // monochrome, ink + accent), a background of that hue too makes a one-tint scene: still possible (it can be
    // beautiful), but contrast is more common then.
    const int sc = id.v[SF_SCHEME];
    const bool oneHue = sc == 0 || sc == 3 || sc == 7;
    t.pal.bgHueOffset = r.chance(oneHue ? 0.4f : 0.6f) ? r.range(-0.4f, 0.4f) : r.range(2.2f, 4.0f);
    t.pal.bgChroma = r.range(0.025f, 0.08f);
    t.pal.perm = r.next();
    for (float& v : t.bgP) v = r.uniform();
    resolvePalette(t, 0);

    // ---- Background, particle layouts, surface.
    t.bgStyle = id.v[SF_BG];
    t.layerCount = 0;
    if (id.v[SF_PART1] > 0) {
        makeLayer(r, fp, mood, t.layers[0], id.v[SF_PART1] - 1, false);
        t.layerCount = 1;
        if (id.v[SF_PART2] > 0) {
            makeLayer(r, fp, mood, t.layers[1], id.v[SF_PART2] - 1, true);
            t.layerCount = 2;
        }
    }
    t.surfStyle = id.v[SF_SURFACE];
    t.surfAmt = mood == 2 ? r.range(0.25f, 0.45f) : r.range(0.3f, 0.6f);
    // Flat tilings (voronoi, facets, hexes) cover the whole screen evenly with cells about a piece's size: at the
    // amount of a soft layer (smoke, fog...) they read as a second grid competing with the board. Keep them faint.
    if (t.surfStyle == 13 || t.surfStyle == 15 || t.surfStyle == 16) t.surfAmt *= 0.4f;
    // Crisp line art (ink, geometry, rings, contours, arcs, weave, lace, cracks, traces) reads as a second
    // background over the real one: a faint pattern only.
    switch (t.surfStyle) {
    case 5: case 6: case 10: case 19: case 21: case 22: case 23: case 29: case 30: case 33: case 36: t.surfAmt *= 0.25f;
    }
    t.surfScale = r.range(0.7f, 1.5f);

    // ---- Blocks.
    t.blockStyle = id.v[SF_BLOCK];
    t.blockMesh = id.v[SF_MESH];
    t.roundness = r.range(3.f, 7.f);
    t.blockScale = r.range(0.8f, 0.96f);
    float sa, sb;
    if (meshScaleRange(t.blockMesh, sa, sb)) t.blockScale = r.range(sa, sb);
    t.blockDepth = (t.blockMesh == MESH_CUBE && r.chance(0.3f)) ? r.range(0.2f, 0.6f) : 1.f;
    t.edgeWidth = r.range(0.04f, 0.14f);
    t.emissive = mood == 2 ? r.range(0.15f, 0.4f) : (mood == 3 ? r.range(0.35f, 0.8f) : r.range(0.7f, 1.6f));
    t.fillAlpha = r.range(0.25f, 0.65f);
    t.ghostAlpha = r.range(0.15f, 0.35f);
    t.meshExp = meshExponent(t.blockMesh, t.roundness);

    // ---- Line-clear effects: a set of 3 distinct ones.
    {
        int pool[CE_COUNT];
        for (int i = 0; i < CE_COUNT; i++) pool[i] = i;
        for (int i = CE_COUNT - 1; i > 0; i--) std::swap(pool[i], pool[r.next() % (i + 1)]);
        // ZIP (the whole row sliding off to one side) is off.
        for (int i = 0, n = 0; n < 3; i++)
            if (pool[i] != CE_ZIP) t.clearEffects[n++] = pool[i];
    }

    // ---- Light rays behind the board in half the scenes (the equalizer decoration stays off).
    t.rays = r.chance(0.5f) ? r.range(0.12f, 0.3f) : 0.f;
    t.rayCount = (float)r.irange(5, 14);
    t.eqStyle = 0;

    // ---- Board frame, lock effect.
    t.frameStyle = id.v[SF_FRAME];
    t.frameAlpha = r.range(0.35f, 0.9f);
    t.lockEffect = id.v[SF_LOCK];

    // ---- Camera framing.
    t.fov = r.range(30.f, 55.f);
    // Tight framing: board (20 rows + spawn row) fills ~90% of the height, with margin for zoom pulses.
    const float visibleH = r.range(23.0f, 23.8f);
    t.camDist = (visibleH * 0.5f) / std::tan(t.fov * 0.5f * PI / 180.f);
    // Always a straight, front-facing view of the board: no yaw, pitch or roll.
    t.camPitch = t.camYaw = t.roll = 0.f;
    t.sway = r.range(0.2f, 1.2f) * 0.9f; // gentle zoom breathing only
    t.swaySpeed = r.range(0.05f, 0.15f);
    t.boardY = 0.f;

    // ---- Post / grade.
    t.bloom = mood == 0 ? r.range(0.6f, 1.3f) + 0.15f : (mood == 1 ? r.range(0.35f, 0.8f) : r.range(0.12f, 0.3f));
    t.bloomThreshold = r.range(0.65f, 1.0f);
    t.vignette = mood == 2 ? r.range(0.05f, 0.25f) : r.range(0.15f, 0.55f);
    // Kept subtle and rare: RGB fringes on many small particles strain the eyes.
    t.chroma = r.chance(0.35f) ? r.range(0.0003f, 0.0011f) : 0.f;
    t.grain = r.range(0.01f, 0.05f);
    t.exposure = r.range(0.95f, 1.15f);
    t.saturation = mood == 3 ? r.range(1.1f, 1.25f) : r.range(0.95f, 1.15f);
    t.scanlines = r.chance(0.08f) ? r.range(0.03f, 0.07f) : 0.f;
    t.beatPulse = r.range(0.5f, 0.9f) * 0.8f;
    t.bassReact = r.range(0.3f, 1.0f);
    t.highReact = r.range(0.3f, 1.0f);
    t.hueDrift = (r.chance(0.5f) ? 1.f : -1.f) * r.range(0.4f, 0.9f);

    t.name = themeName(t);
    return t;
}

// The song's hash is folded into the seed, so each song gets its own scenes from the same run seed.
Theme generateTheme(const Footprint& song, uint64_t seed) {
    return buildTheme(pickIdentity(song, seed ^ splitmix64(song.hash)));
}

Theme themeFromCode(const std::string& code, bool* ok) {
    SceneId id;
    const bool valid = parseSceneCode(code, id);
    if (ok) *ok = valid;
    return valid ? buildTheme(id) : generateTheme(Footprint{}, 0);
}

const char* lockEffectName(int e) { return e >= 0 && e < LE_COUNT ? LE_NAMES[e] : "?"; }

// A song keeps one visual identity: background, main particle layout, blocks, frame and mood never change
// inside a song. Levels (0 calm, 1 mid, 2 peak) only shift hue a little and change color intensity, glow and
// an extra particle layer.
// Levels: 0 calm (sparse, muted, cooler), 1 = the song's base scene, 2 peak (fuller, warmer, glowing).
// The song's identity (background, main particles, blocks, frame, mood) never changes.
Theme evolveTheme(const Theme& base, int level, float energy) {
    const Footprint fp; // neutral, as in buildTheme
    if (level == 1) return base;
    Theme t = base;
    Rng r(base.seed ^ splitmix64(0xE7011EULL + (uint64_t)level));
    const int mood = (int)std::lround(base.pal.mood);
    const float e = saturate(energy);
    if (level <= 0) {
        // Calm: sparse and quiet rather than gray. The blocks keep most of their color; the calm comes from
        // fewer, dimmer particles, a thinner surface, less glow, a darker edge and a step back.
        t.pal.chroma = base.pal.chroma * 0.88f;
        t.pal.bgChroma = base.pal.bgChroma * 0.75f;
        t.layerCount = std::min(1, base.layerCount);
        t.surfAmt = base.surfAmt * 0.65f;
        t.layers[0].bright *= 0.6f;
        t.layers[0].count *= 0.45f;
        t.bloom = base.bloom * 0.55f;
        t.saturation = clampf(base.saturation * 0.96f, 0.85f, 1.2f);
        t.emissive = base.emissive * 0.85f;
        t.exposure = base.exposure * 0.95f;
        t.vignette = std::min(0.7f, base.vignette + 0.15f);
        t.camDist = base.camDist * 1.05f;
        resolvePalette(t, -base.hueDrift * 0.8f);
    } else {
        // Peak: always a new particle layer (a style the song has not shown yet), richer color, more glow.
        t.pal.chroma = std::min(0.24f, base.pal.chroma * (1.3f + 0.2f * e));
        // Pale backgrounds stay soft at peaks: a vivid light background outshines the blocks.
        t.pal.bgChroma = mood == 2 ? std::min(0.06f, base.pal.bgChroma * 1.3f + 0.01f)
                                   : std::min(0.14f, base.pal.bgChroma * 1.8f + 0.02f);
        int avoid = base.layers[0].style;
        int st = pickParticleStyle(r, fp, mood, avoid);
        if (base.layerCount > 1 && st == base.layers[1].style) st = pickParticleStyle(r, fp, mood, avoid);
        // No particle floor under a flat tiling surface (voronoi, water, facets, hexes).
        auto floorStyle = [](int s) { return s == PS_WAVES || s == PS_DUNES || s == PS_SEA; };
        for (int k = 0; k < 8 && base.surfStyle >= 13 && base.surfStyle <= 16 && floorStyle(st); k++)
            st = pickParticleStyle(r, fp, mood, avoid);
        const int slot = base.layerCount == 0 ? 0 : 1;
        // Fuller, not cluttered: the new layer keeps a secondary density (sparser still over a moving surface,
        // so a peak never stacks three dense moving layers) and the existing layers get only a small boost.
        makeLayer(r, fp, mood, t.layers[slot], st, true);
        if (base.surfStyle > 0 && slot > 0) t.layers[slot].count *= 0.7f;
        t.layerCount = slot + 1;
        t.surfAmt = std::min(0.8f, base.surfAmt * 1.1f);
        t.layers[0].count = std::min(1.f, t.layers[0].count * 1.15f);
        for (int i = 0; i < 2; i++) t.layers[i].bright *= 1.1f;
        t.bloom = base.bloom * 1.3f;
        t.saturation = clampf(base.saturation * 1.12f, 0.8f, 1.3f);
        t.emissive = base.emissive * 1.25f;
        t.exposure = base.exposure * 1.0f;
        t.vignette = std::max(0.05f, base.vignette - 0.08f);
        t.edgeWidth = std::min(0.18f, base.edgeWidth * 1.2f);
        t.camDist = base.camDist * 0.97f;
        resolvePalette(t, base.hueDrift * 1.4f);
    }
    t.name = themeName(t);
    return t;
}

Theme blendThemes(const Theme& a, const Theme& b, float t) {
    Theme r = t < 0.5f ? a : b;
    auto L = [&](float x, float y) { return lerpf(x, y, t); };
    auto C = [&](const vec3& x, const vec3& y) { return lerp(x, y, t); };
    r.bgTop = C(a.bgTop, b.bgTop);
    r.bgBottom = C(a.bgBottom, b.bgBottom);
    r.bgGlow = C(a.bgGlow, b.bgGlow);
    for (int i = 0; i < 7; i++) r.piece[i] = C(a.piece[i], b.piece[i]);
    r.accent = C(a.accent, b.accent);
    r.text = C(a.text, b.text);
    r.partA = C(a.partA, b.partA);
    r.partB = C(a.partB, b.partB);
    r.partC = C(a.partC, b.partC);
    r.wellTint = C(a.wellTint, b.wellTint);
    r.shadowTint = C(a.shadowTint, b.shadowTint);
    r.highlightTint = C(a.highlightTint, b.highlightTint);
    r.pale = L(a.pale, b.pale);
    r.hueShift = L(a.hueShift, b.hueShift);
    for (int i = 0; i < 4; i++) r.bgP[i] = L(a.bgP[i], b.bgP[i]);
    r.edgeWidth = L(a.edgeWidth, b.edgeWidth);
    r.eqAlpha = L(a.eqAlpha, b.eqAlpha);
    r.surfAmt = L(a.surfAmt, b.surfAmt);
    r.blockScale = L(a.blockScale, b.blockScale);
    r.blockDepth = L(a.blockDepth, b.blockDepth);
    r.meshExp = std::exp(L(std::log(a.meshExp), std::log(b.meshExp)));
    r.emissive = L(a.emissive, b.emissive);
    r.fillAlpha = L(a.fillAlpha, b.fillAlpha);
    r.ghostAlpha = L(a.ghostAlpha, b.ghostAlpha);
    r.frameAlpha = L(a.frameAlpha, b.frameAlpha);
    r.camDist = L(a.camDist, b.camDist);
    r.camPitch = L(a.camPitch, b.camPitch);
    r.camYaw = L(a.camYaw, b.camYaw);
    r.fov = L(a.fov, b.fov);
    r.sway = L(a.sway, b.sway);
    r.swaySpeed = L(a.swaySpeed, b.swaySpeed);
    r.roll = L(a.roll, b.roll);
    r.boardY = L(a.boardY, b.boardY);
    r.bloom = L(a.bloom, b.bloom);
    r.bloomThreshold = L(a.bloomThreshold, b.bloomThreshold);
    r.vignette = L(a.vignette, b.vignette);
    r.chroma = L(a.chroma, b.chroma);
    r.grain = L(a.grain, b.grain);
    r.exposure = L(a.exposure, b.exposure);
    r.saturation = L(a.saturation, b.saturation);
    r.scanlines = L(a.scanlines, b.scanlines);
    r.beatPulse = L(a.beatPulse, b.beatPulse);
    r.bassReact = L(a.bassReact, b.bassReact);
    r.highReact = L(a.highReact, b.highReact);
    return r;
}

// ---- Scene fields (the identity in scene codes, zenscene's menu rows).
namespace {

const char* SF_NAMES[SF_COUNT] = {"MOOD", "PALETTE", "HUE", "BACKGROUND", "PARTICLES", "PARTICLES 2", "SURFACE",
                                  "BLOCKS", "SHAPE", "FRAME", "LOCK EFFECT"};
const char* SCHEME_NAMES[SCHEME_COUNT] = {"ANALOGOUS", "COMPLEMENTARY", "TRIADIC", "MONOCHROME", "DUOTONE",
                                          "PASTEL RAINBOW", "SPLIT COMPLEMENT", "INK + ACCENT", "SUNSET RAMP",
                                          "JEWEL TONES", "NEON PAIR", "EARTH", "ICE", "CANDY", "TETRADIC",
                                          "GOLDEN ANGLE", "TWILIGHT", "FOREST", "AURORA", "CORAL REEF",
                                          "SUNRISE", "DEEP SEA", "RETRO", "CITRUS", "BERRY", "VAPORWAVE",
                                          "AUTUMN", "PRIMARY"};
const char* FR_NAMES[FR_COUNT] = {"OUTLINE", "CORNERS", "WELL", "FLOOR", "GRID", "NONE", "PILLARS", "DOUBLE", "DOTTED",
                                  "GLOW BASE", "TOP + BOTTOM", "TICKS", "SIDE FADE", "UNDERLINE", "CORNER DOTS",
                                  "RAILS", "DASHED", "DOT PILLARS", "BRACKETS", "ARCH", "RULER", "CHEVRONS", "BEADS",
                                  "NEON TUBE", "ZIGZAG", "LATTICE", "ORBIT", "PEDESTAL", "FILMSTRIP", "PIANO",
                                  "VINES", "CIRCUIT", "CRENELS", "WAVES", "STARS", "LANTERNS", "BRACES", "SCANNER"};
constexpr int HUE_STEPS = 24;

} // namespace

const char* sceneFieldName(int f) { return f >= 0 && f < SF_COUNT ? SF_NAMES[f] : "?"; }

int sceneFieldValues(int f) {
    switch (f) {
    case SF_MOOD: return 4;
    case SF_SCHEME: return SCHEME_COUNT;
    case SF_HUE: return HUE_STEPS;
    case SF_BG: return BG_COUNT;
    case SF_PART1: case SF_PART2: return PS_COUNT + 1; // 0 = none
    case SF_SURFACE: return SURF_COUNT;
    case SF_BLOCK: return BS_COUNT;
    case SF_MESH: return MESH_COUNT;
    case SF_FRAME: return FR_COUNT;
    case SF_LOCK: return LE_COUNT;
    default: return 0;
    }
}

std::string sceneFieldValueName(int f, int v) {
    if (v < 0 || v >= sceneFieldValues(f)) return "?";
    switch (f) {
    case SF_MOOD: return MOOD_NAMES[v];
    case SF_SCHEME: return SCHEME_NAMES[v];
    case SF_HUE: return std::to_string(v * 360 / HUE_STEPS);
    case SF_BG: return BG_NAMES[v];
    case SF_PART1: case SF_PART2: return v == 0 ? "NONE" : PS_NAMES[v - 1];
    case SF_SURFACE: return v == 0 ? "NONE" : SURF_NAMES[v];
    case SF_BLOCK: return BS_NAMES[v];
    case SF_MESH: return MESH_NAMES[v];
    case SF_FRAME: return FR_NAMES[v];
    case SF_LOCK: return LE_NAMES[v];
    default: return "?";
    }
}

int sceneFieldValue(const Theme& t, int f) {
    switch (f) {
    case SF_MOOD: return (int)std::lround(t.pal.mood);
    case SF_SCHEME: return t.pal.scheme;
    case SF_HUE: {
        float h = std::fmod(t.pal.hue, TAU);
        if (h < 0) h += TAU;
        return (int)std::lround(h / TAU * HUE_STEPS) % HUE_STEPS;
    }
    case SF_BG: return t.bgStyle;
    case SF_PART1: return t.layerCount > 0 ? t.layers[0].style + 1 : 0;
    case SF_PART2: return t.layerCount > 1 ? t.layers[1].style + 1 : 0;
    case SF_SURFACE: return t.surfStyle;
    case SF_BLOCK: return t.blockStyle;
    case SF_MESH: return t.blockMesh;
    case SF_FRAME: return t.frameStyle;
    case SF_LOCK: return t.lockEffect;
    default: return -1;
    }
}

// ---- Scene options switched off. The list built into the binary comes from src/scene-options.txt.
namespace {

const char* const BUILT_IN_OPTIONS =
#include "scene_options.inc"
    ;

struct OptionList {
    std::vector<std::vector<bool>> off; // [field][value]
    OptionList() {
        off.resize(SF_COUNT);
        for (int f = 0; f < SF_COUNT; f++) off[f].assign(sceneFieldValues(f), false);
    }
};

OptionList& options() {
    static OptionList list;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        loadSceneOptions(BUILT_IN_OPTIONS);
    }
    return list;
}

int optionField(int f) { return f == SF_PART2 ? SF_PART1 : f; } // the two particle layers share one pool

} // namespace

bool sceneOptionDisableable(int f, int v) {
    if (f < 0 || f >= SF_COUNT || f == SF_HUE || v < 0 || v >= sceneFieldValues(f)) return false;
    return !((f == SF_PART1 || f == SF_PART2 || f == SF_SURFACE) && v == 0); // "none" is not an option
}

bool sceneOptionEnabled(int f, int v) {
    return !sceneOptionDisableable(f, v) || !options().off[optionField(f)][v];
}

void setSceneOptionEnabled(int f, int v, bool on) {
    if (sceneOptionDisableable(f, v)) options().off[optionField(f)][v] = !on;
}

void loadSceneOptions(const std::string& text) {
    OptionList& list = options();
    list = OptionList();
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = text.substr(i, j - i);
        i = j + 1;
        if (const size_t h = line.find('#'); h != std::string::npos) line.resize(h);
        while (!line.empty() && std::isspace((unsigned char)line.back())) line.pop_back();
        size_t b = 0;
        while (b < line.size() && std::isspace((unsigned char)line[b])) b++;
        line = line.substr(b);
        if (line.empty()) continue;
        for (char& c : line) c = (char)std::toupper((unsigned char)c);
        // The longest field name that starts the line ("LOCK EFFECT", "BACKGROUND"...), then the value's name.
        bool found = false;
        for (int f = 0; f < SF_COUNT && !found; f++) {
            if (f == SF_PART2 || f == SF_HUE) continue;
            const std::string name = std::string(sceneFieldName(f)) + " ";
            if (line.compare(0, name.size(), name) != 0) continue;
            const std::string value = line.substr(name.size());
            for (int v = 0; v < sceneFieldValues(f); v++)
                if (sceneOptionDisableable(f, v) && sceneFieldValueName(f, v) == value) {
                    list.off[f][v] = true;
                    found = true;
                }
        }
        if (!found) std::fprintf(stderr, "[scene options] unknown option '%s'\n", line.c_str());
    }
}

std::string sceneOptionsText() {
    std::string s =
        "# Scene options switched off: scenes never pick them (a scene code that pins one still shows it).\n"
        "# One \"FIELD VALUE\" per line, as zenscene's menu names them (PARTICLES covers both layers).\n"
        "# Edited with zenscene (X, or the ON/OFF boxes); built into the game: rebuild after a change.\n";
    for (int f = 0; f < SF_COUNT; f++)
        for (int v = 0; v < sceneFieldValues(f); v++)
            if (sceneOptionDisableable(f, v) && f != SF_PART2 && !sceneOptionEnabled(f, v))
                s += std::string(sceneFieldName(f)) + " " + sceneFieldValueName(f, v) + "\n";
    return s;
}
