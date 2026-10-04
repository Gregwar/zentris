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
                                  "DIAGONAL", "SUNBURST", "RIPPLES", "PLASMA", "SEA", "PEAKS", "SHAFTS", "HALO RING"};
const char* PS_NAMES[PS_COUNT] = {"GALAXY", "TUNNEL", "OCEAN", "SPHERE", "DRIFT", "STREAMS",
                                  "WARP", "CURTAINS", "HALOS", "HELIX", "BOKEH", "LATTICE",
                                  "FIREFLIES", "RAIN", "VORTEX", "WAVEFORM", "STARBURST", "ORBITS", "CONFETTI",
                                  "SNOWGLOBE", "LADDER", "FOUNTAIN", "PETALS", "CONSTELLATION", "TORUS", "WALL",
                                  "COMETS", "SPARKLERS", "BUBBLES", "CUBE", "INFINITY", "BEAT RINGS", "PLASMA",
                                  "MOIRE", "SWARM", "SPIRALS", "RIBBON", "METEORS", "SKYLINE", "CORONA", "DUNES", "WIND", "FIRE", "SEA"};
const char* BS_NAMES[BS_COUNT] = {"GLASS", "SOLID", "WIRE", "LANTERN", "INSET", "DOTS", "CRYSTAL", "SPLIT",
                                  "HOLO", "GRADIENT", "DOUBLE", "NEON", "CIRCUIT", "FROSTED", "CHECKER", "RINGS",
                                  "BEVEL", "PIXEL", "STRIPES", "CORE", "HATCH", "BREATH"};
const char* LE_NAMES[LE_COUNT] = {"POP", "AFTERGLOW", "BOUNCE", "SQUASH", "GROW", "PRESS", "TWINKLE", "RIPPLE",
                                  "CASCADE", "EMBER", "HUE SHIFT"};
const char* MESH_NAMES[MESH_COUNT] = {"CUBE", "ROUNDED", "ORB", "GEM"};
const char* MOOD_NAMES[3] = {"NIGHT", "DUSK", "PALE"};
const char* SURF_NAMES[18] = {"", "SMOKE", "SILK", "LAVA", "CAUSTICS", "INK", "GEOMETRY", "AURORA", "FOG",
                              "BEAMS", "FLOW RINGS", "LIQUID", "SHADES", "VORONOI", "WATER", "FACETS", "HEXES",
                              "SHARDS"};

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


// ---- Pools. Each pool's first items (LEGACY) are drawn from the scene's stream exactly as they always were, so scene
// codes keep their scenes; items added later come from a separate stream `x` (always the same number of draws), only
// when enabled. A pick that lands on a disabled item moves on to the next enabled one, without drawing again.
constexpr int MOOD_LEGACY = 3, SCHEME_LEGACY = 8, BG_LEGACY = 24, PS_LEGACY = 44, SURF_LEGACY = 18, BS_LEGACY = 22,
              MESH_LEGACY = 4, FR_LEGACY = 18, LE_LEGACY = 11;

// Rng::weighted over the first n weights (the same draw as weighted() on an array of n).
int weightedPrefix(Rng& r, const float* w, int n) {
    float sum = 0;
    for (int i = 0; i < n; i++) sum += std::max(0.f, w[i]);
    float u = r.uniform() * sum;
    for (int i = 0; i < n; i++) {
        u -= std::max(0.f, w[i]);
        if (u <= 0) return i;
    }
    return n - 1;
}

// v: the legacy pick. offset: menu value of item 0 (1 for particles, whose menu value 0 is "none").
int poolChoice(int v, Rng& x, const float* w, int n, int legacy, int field, int offset = 0) {
    auto on = [&](int i) { return w[i] > 0 && sceneOptionEnabled(field, i + offset); };
    float wo = 0, wn = 0;
    for (int i = 0; i < n; i++) (i < legacy ? wo : wn) += on(i) ? w[i] : 0.f;
    const float q = x.uniform(), q2 = x.uniform();
    if (wn > 0 && q * (wo + wn) < wn) { // one of the later items
        float u = q2 * wn;
        for (int i = legacy; i < n; i++)
            if (on(i) && (u -= w[i]) <= 0) return i;
    }
    if (sceneOptionEnabled(field, v + offset)) return v;
    for (int k = 1; k < n; k++) {
        const int c = (v + k) % n;
        if (on(c)) return c;
    }
    return v; // everything disabled: keep the pick
}

template <int N>
int pickPool(Rng& r, Rng& x, const float (&w)[N], int legacy, int field, int offset = 0) {
    return poolChoice(weightedPrefix(r, w, legacy), x, w, N, legacy, field, offset);
}

int pickBackground(Rng& r, Rng& x, const Footprint& fp, int mood) {
    float w[BG_COUNT] = {2.f, 1.8f, mood == 2 ? 0.4f : 1.2f, mood == 2 ? 0.f : 1.5f,
                         1.0f, mood == 2 ? 0.f : 1.0f + fp.airWeight, 1.0f, 1.0f,
                         mood == 2 ? 0.3f : 0.8f + 0.8f * fp.bassWeight, 1.0f, 0.9f, mood == 2 ? 0.f : 1.2f,
                         1.2f, 1.0f, 1.0f, 0.7f, 1.0f, 0.9f, 0.9f, mood == 2 ? 0.4f : 1.0f,
                         mood == 2 ? 0.5f : 1.0f, 0.9f, 1.0f, 1.0f};
    return pickPool(r, x, w, BG_LEGACY, SF_BG);
}

float tempoNorm(const Footprint& fp) { return saturate((fp.bpm - 70.f) / 90.f); }

int pickParticleStyle(Rng& r, Rng& x, const Footprint& fp, int mood, int avoid) {
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
    };
    if (avoid >= 0) w[avoid] = 0;
    return pickPool(r, x, w, PS_LEGACY, SF_PART1, 1);
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
    float baseL = mood == 0 ? 0.72f : (mood == 1 ? 0.8f : 0.60f); // glow adds light: keep colors deep enough
    float C = p.chroma * (mood == 1 ? 0.9f : 1.f);
    // On a pale background low-chroma colors read as gray: keep pale themes clearly colored (calm sections too).
    if (mood == 2) C = std::min(0.24f, std::max(0.13f, C * 1.3f));
    for (int i = 0; i < 7; i++) {
        float f = (float)i / 6.f;
        float jitter = r.range(-0.04f, 0.04f);
        // Pieces spread in lightness, so they stand apart even when their hues are close.
        Ls[i] = baseL + (f - 0.5f) * 0.14f + r.range(-0.03f, 0.03f);
        Cs[i] = C;
        switch (p.scheme) {
        case 0: hues[i] = h + (i - 3) * p.spread * 0.45f; break;                           // analogous
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
        t.bgTop = ok(0.93f + t.bgP[0] * 0.04f, p.bgChroma * 0.6f, bh);
        t.bgBottom = ok(0.83f + t.bgP[1] * 0.06f, p.bgChroma * 0.9f + 0.01f, bh + 0.5f * (t.bgP[2] - 0.5f));
        t.bgGlow = ok(0.9f, 0.07f, avoidMud(accentH, 0.6f)); // tinted, so energy glow shows on pale
        t.accent = ok(0.5f, 0.12f, accentH);
        t.text = ok(0.3f, 0.03f, h);
        t.partA = ok(0.6f, C * 0.9f, avoidMud(h, 0.6f));
        t.partB = ok(0.68f, 0.1f, avoidMud(accentH + 0.5f, 0.6f));
        t.partC = ok(0.99f, 0.01f, bh);
        t.pale = 1;
    }
    t.shadowTint = ok(0.5f, 0.12f, h + PI + r.range(-0.8f, 0.8f));
    t.highlightTint = ok(0.8f, 0.1f, h + r.range(-0.5f, 0.5f));
}

float meshExponent(int mesh, float roundness) {
    switch (mesh) {
    case MESH_GEM: return 1.35f; // a full, rounded diamond (a sharp octahedron leaves cells touching at tips)
    case MESH_SPHERE: return 2.f;
    case MESH_ROUNDED: return roundness;
    default: return 24.f;
    }
}

// The footprint fields a scene uses, quantized so that its scene code can carry them (an empty footprint,
// the loading scene's, is kept as is: its codes end with 'g').
static Footprint sceneFootprint(const Footprint& f) {
    if (!f.hash) return Footprint{};
    Footprint q;
    q.bpm = (float)std::clamp((int)std::lround(f.bpm), 40, 295);
    q.key = std::clamp(f.key, 0, 11);
    q.minor = f.minor;
    auto n = [](float x) { return (float)std::lround(saturate(x) * 15.f) / 15.f; };
    q.brightness = n(f.brightness);
    q.bassWeight = n(f.bassWeight);
    q.airWeight = n(f.airWeight);
    q.dynamics = n(f.dynamics);
    q.density = n(f.density);
    return q;
}

static Theme generateFrom(const Footprint& fp, uint64_t seed, bool generic);

// The song's hash is folded into the seed (unchanged for an empty footprint), so the code needs only the seed
// and the quantized footprint to give back the same scene, with any song.
Theme generateTheme(const Footprint& song, uint64_t seed) {
    return generateFrom(sceneFootprint(song), seed ^ splitmix64(song.hash) ^ splitmix64(0), !song.hash);
}

static bool parseOverrides(const std::string& s, SceneOverrides& o);

Theme themeFromCode(const std::string& code, bool* ok, Footprint* fpOut, SceneOverrides* ovOut) {
    const size_t us = code.find('_');
    std::string c = code.substr(0, us);
    SceneOverrides ov;
    const bool ovValid = us == std::string::npos || parseOverrides(code.substr(us + 1), ov);
    const bool generic = !c.empty() && (c.back() == 'g' || c.back() == 'G');
    if (generic) c.pop_back();
    const size_t dash = c.find('-');
    char* end = nullptr;
    const uint64_t seed = std::strtoull(c.substr(0, dash).c_str(), &end, 16);
    bool valid = end && *end == 0 && dash != 0;
    Footprint fp;
    if (!generic) {
        // seed-BBKMbadyd: bpm-40, key, minor, then brightness, bass, air, dynamics, density in 15ths.
        auto hex = [&](size_t i) {
            const char ch = (char)std::tolower(i < c.size() ? c[i] : 'x');
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            valid = false;
            return 0;
        };
        const size_t p = dash + 1;
        valid = valid && dash != std::string::npos && c.size() == p + 9;
        fp.bpm = (float)(40 + hex(p) * 16 + hex(p + 1));
        fp.key = std::min(hex(p + 2), 11);
        fp.minor = hex(p + 3) != 0;
        float* f[5] = {&fp.brightness, &fp.bassWeight, &fp.airWeight, &fp.dynamics, &fp.density};
        for (int k = 0; k < 5; k++) *f[k] = (float)hex(p + 4 + k) / 15.f;
    } else {
        valid = valid && dash == std::string::npos;
    }
    if (ok) *ok = valid && ovValid;
    if (fpOut) *fpOut = fp;
    if (ovOut) *ovOut = ov;
    Theme t = generateFrom(fp, seed, generic);
    if (ov.any()) applyOverrides(t, fp, ov);
    return t;
}

static Theme generateFrom(const Footprint& fp, uint64_t seed, bool generic) {
    Theme t;
    t.seed = seed;
    {
        char b[48];
        if (generic)
            std::snprintf(b, sizeof(b), "%llxg", (unsigned long long)seed);
        else
            std::snprintf(b, sizeof(b), "%llx-%02x%x%x%x%x%x%x%x", (unsigned long long)seed, (int)fp.bpm - 40, fp.key,
                          fp.minor ? 1 : 0, (int)std::lround(fp.brightness * 15), (int)std::lround(fp.bassWeight * 15),
                          (int)std::lround(fp.airWeight * 15), (int)std::lround(fp.dynamics * 15),
                          (int)std::lround(fp.density * 15));
        t.code = b;
    }
    Rng r(seed ^ splitmix64(0));
    Rng x(seed ^ 0xADDED00F5ull); // later pool items (see pickPool)
    const float bpmN = saturate((fp.bpm - 70.f) / 90.f); // 0 slow .. 1 fast

    // ---- Palette: key on the circle of fifths gives the base hue (synesthetic mapping).
    int fifths = (fp.key * 7) % 12;
    t.pal.hue = fifths / 12.f * TAU + r.range(-0.6f, 0.6f);
    float moodW[3] = {0.62f - 0.25f * fp.brightness + (fp.minor ? 0.12f : 0.f),
                      0.2f + 0.2f * fp.brightness, 0.08f + 0.22f * fp.brightness * (1.f - 0.5f * fp.bassWeight)};
    t.pal.mood = (float)pickPool(r, x, moodW, MOOD_LEGACY, SF_MOOD);
    float schemeW[8] = {3.f, 2.f, 1.2f, 1.3f, 2.6f, 0.8f + fp.brightness, 1.5f, 0.8f + (t.pal.mood == 0 ? 0.6f : 0.f)};
    t.pal.scheme = pickPool(r, x, schemeW, SCHEME_LEGACY, SF_SCHEME);
    t.pal.chroma = r.range(0.11f, 0.2f) * (fp.minor ? 0.9f : 1.f) * (0.9f + 0.2f * fp.dynamics);
    t.pal.spread = r.range(0.15f, 0.5f);
    t.pal.bgHueOffset = r.chance(0.6f) ? r.range(-0.4f, 0.4f) : r.range(2.2f, 4.0f);
    t.pal.bgChroma = r.range(0.015f, 0.07f);
    t.pal.perm = r.next();
    for (float& v : t.bgP) v = r.uniform();
    resolvePalette(t, 0);
    const int mood = (int)t.pal.mood;

    t.bgStyle = pickBackground(r, x, fp, mood);
    makeLayer(r, fp, mood, t.layers[0], pickParticleStyle(r, x, fp, mood, -1), false);
    t.layerCount = 1;
    if (r.chance(0.55f)) {
        makeLayer(r, fp, mood, t.layers[1], pickParticleStyle(r, x, fp, mood, t.layers[0].style), true);
        t.layerCount = 2;
    }
    // Continuous surface layer in about two thirds of the scenes; some of those drop particles entirely.
    if (r.chance(0.65f)) {
        float sw[18] = {0, 1.3f, 1.1f, 0.9f, mood == 2 ? 0.5f : 1.f, 1.f, 1.f, mood == 2 ? 0.3f : 1.f, 1.1f,
                        mood == 2 ? 0.3f : 0.9f, 0.9f, 1.f, 1.1f, 1.1f, 1.f, 1.f, 0.9f, 1.f};
        // Flat tilings (voronoi, facets, hexes) and a second waterline clash with a landscape's floor or horizon.
        bool landscape = t.bgStyle == BG_HORIZON || t.bgStyle == BG_GRID || t.bgStyle == BG_HILLS ||
                         t.bgStyle == BG_SEA || t.bgStyle == BG_PEAKS;
        for (int i = 0; i < t.layerCount; i++) // particle floors too
            landscape |= t.layers[i].style == PS_WAVES || t.layers[i].style == PS_DUNES || t.layers[i].style == PS_SEA;
        if (landscape) sw[13] = sw[14] = sw[15] = sw[16] = 0.f;
        t.surfStyle = pickPool(r, x, sw, SURF_LEGACY, SF_SURFACE);
        t.surfAmt = mood == 2 ? r.range(0.25f, 0.45f) : r.range(0.3f, 0.6f);
        t.surfScale = r.range(0.7f, 1.5f);
        if (r.chance(0.3f)) t.layerCount = 0;
        else if (t.layerCount == 2 && r.chance(0.5f)) t.layerCount = 1;
    }

    // ---- Blocks.
    {
        float w[BS_COUNT] = {mood == 2 ? 0.8f : 3.f, mood == 2 ? 2.5f : 1.2f, 1.2f, mood == 2 ? 0.3f : 1.3f,
                             1.3f, 0.9f, 1.3f, 1.0f, mood == 2 ? 0.4f : 1.1f, 1.1f, 1.0f,
                             mood == 2 ? 0.4f : 1.1f, 0.9f, 1.0f, 0.9f, 0.9f, 1.1f, 0.9f, 0.9f,
                             mood == 2 ? 0.5f : 1.0f, 0.8f, 0.9f};
        t.blockStyle = pickPool(r, x, w, BS_LEGACY, SF_BLOCK);
        float mw[MESH_COUNT] = {5.5f, 2.5f, 1.0f, 0.5f}; // gems read less clearly as pieces: rarer
        const int faceStyles[] = {BS_DOTS, BS_INSET, BS_SPLIT, BS_DOUBLE, BS_CIRCUIT, BS_CHECKER, BS_RINGS, BS_PIXEL, BS_HATCH,
                                  BS_STRIPES};
        for (int fsIdx : faceStyles)
            if (t.blockStyle == fsIdx) mw[2] = mw[3] = 0; // face patterns need flat faces
        t.blockMesh = pickPool(r, x, mw, MESH_LEGACY, SF_MESH);
        t.roundness = r.range(3.f, 7.f);
        t.blockScale = r.range(0.8f, 0.96f);
        if (t.blockMesh == MESH_SPHERE) t.blockScale = r.range(0.85f, 1.0f);
        if (t.blockMesh == MESH_GEM) t.blockScale = r.range(0.95f, 1.05f); // diamonds leave gaps: draw them larger
        t.blockDepth = (t.blockMesh == MESH_CUBE && r.chance(0.3f)) ? r.range(0.2f, 0.6f) : 1.f;
        t.edgeWidth = r.range(0.04f, 0.14f);
        t.emissive = mood == 2 ? r.range(0.15f, 0.4f) : r.range(0.7f, 1.6f);
        t.fillAlpha = r.range(0.25f, 0.65f);
        t.ghostAlpha = r.range(0.15f, 0.35f);
        t.meshExp = meshExponent(t.blockMesh, t.roundness);
    }

    // ---- Line-clear effects: a set of 3 distinct ones for this scene.
    {
        int pool[CE_COUNT];
        for (int i = 0; i < CE_COUNT; i++) pool[i] = i;
        for (int i = CE_COUNT - 1; i > 0; i--) std::swap(pool[i], pool[r.next() % (i + 1)]);
        for (int i = 0; i < 3; i++) t.clearEffects[i] = pool[i];
    }

    // ---- Energy decorations: light rays behind the board. The equalizer bars beside the board are disabled
    // (they looked pasted on; spectrum-driven particle layouts replace them); their draws are kept so the
    // rest of each song's scene stays the same.
    {
        float w[4] = {16.f, 1.f, 1.f, 0.8f}; // rare: about 1 scene in 7
        t.eqStyle = r.weighted(w);
        float rw[6] = {1.f, 1.3f, 1.1f, 1.1f, 0.9f, 1.f};
        t.eqRender = r.weighted(rw);
        const int barsChoice[4] = {16, 24, 32, 48};
        t.eqBars = barsChoice[r.irange(0, 3)];
        float cw[3] = {1.4f, 1.f, 0.7f};
        t.eqColor = r.weighted(cw);
        t.eqDecay = r.chance(0.5f) ? r.range(3.f, 6.f) : r.range(9.f, 16.f);
        t.eqPeakFall = r.range(0.25f, 0.9f);
        t.eqAlpha = r.range(0.35f, 0.6f);
        t.rays = r.chance(0.5f) ? r.range(0.12f, 0.3f) : 0.f;
        t.rayCount = (float)r.irange(5, 14);
        t.eqStyle = 0;
    }

    // ---- Board frame.
    {
        float w[FR_COUNT] = {2.f, 1.5f, 1.5f, 1.2f, 1.0f, 0.8f, 1.0f, 1.0f, 1.0f,
                             1.0f, 1.0f, 1.0f, 1.0f, 0.8f, 0.8f, 1.0f, 1.0f, 0.8f};
        t.frameStyle = pickPool(r, x, w, FR_LEGACY, SF_FRAME);
        t.frameAlpha = r.range(0.35f, 0.9f);
    }

    // ---- Camera framing.
    {
        t.fov = r.range(30.f, 55.f);
        // Tight framing: board (20 rows + spawn row) fills ~90% of the height, with margin for zoom pulses.
        float visibleH = r.range(23.0f, 23.8f);
        t.camDist = (visibleH * 0.5f) / std::tan(t.fov * 0.5f * PI / 180.f);
        // Always a straight, front-facing view of the board: no yaw, pitch or roll.
        t.camPitch = 0.f;
        t.camYaw = 0.f;
        t.roll = 0.f;
        t.sway = r.range(0.2f, 1.2f) * (0.6f + 0.6f * fp.dynamics); // gentle zoom breathing only
        t.swaySpeed = r.range(0.05f, 0.15f) * (0.7f + 0.6f * bpmN);
        t.boardY = 0.f;
    }

    // ---- Post / grade.
    {
        t.bloom = mood == 0 ? r.range(0.6f, 1.3f) + 0.3f * fp.dynamics : (mood == 1 ? r.range(0.35f, 0.8f) : r.range(0.12f, 0.3f));
        t.bloomThreshold = r.range(0.65f, 1.0f);
        t.vignette = mood == 2 ? r.range(0.05f, 0.25f) : r.range(0.15f, 0.55f);
        // Kept subtle and rare: RGB fringes on many small particles strain the eyes.
        t.chroma = r.chance(0.35f) ? r.range(0.0003f, 0.0011f) : 0.f;
        t.grain = r.range(0.01f, 0.05f);
        t.exposure = r.range(0.95f, 1.15f);
        t.saturation = r.range(0.95f, 1.15f);
        t.scanlines = r.chance(0.08f) ? r.range(0.03f, 0.07f) : 0.f;
        t.beatPulse = r.range(0.5f, 0.9f) * (0.6f + 0.4f * fp.dynamics);
        t.bassReact = r.range(0.3f, 1.0f);
        t.highReact = r.range(0.3f, 1.0f);
        t.hueDrift = (r.chance(0.5f) ? 1.f : -1.f) * r.range(0.4f, 0.9f);
    }

    // ---- Lock effect: from its own random stream, so adding it changed no other choice of existing scenes.
    {
        float lw[LE_COUNT];
        for (float& v : lw) v = 1.f;
        t.lockEffect = poolChoice((int)(Rng(seed ^ 0x10CCEFFEC7ull).next() % LE_LEGACY), x, lw, LE_COUNT, LE_LEGACY, SF_LOCK);
    }

    t.name = themeName(t);
    return t;
}

const char* lockEffectName(int e) { return e >= 0 && e < LE_COUNT ? LE_NAMES[e] : "?"; }

// A song keeps one visual identity: background, main particle layout, blocks, frame and mood never change
// inside a song. Levels (0 calm, 1 mid, 2 peak) only shift hue a little and change color intensity, glow and
// an extra particle layer.
// Levels: 0 calm (sparse, muted, cooler), 1 = the song's base scene, 2 peak (fuller, warmer, glowing).
// The song's identity (background, main particles, blocks, frame, mood) never changes.
Theme evolveTheme(const Theme& base, const Footprint& fp, int level, float energy, bool) {
    if (level == 1) return base;
    Theme t = base;
    Rng r(base.seed ^ splitmix64(0xE7011EULL + (uint64_t)level));
    Rng x(base.seed ^ splitmix64(0xADDE7011EULL + (uint64_t)level));
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
        int st = pickParticleStyle(r, x, fp, mood, avoid);
        if (base.layerCount > 1 && st == base.layers[1].style) st = pickParticleStyle(r, x, fp, mood, avoid);
        // No particle floor under a flat tiling surface (voronoi, water, facets, hexes).
        auto floorStyle = [](int s) { return s == PS_WAVES || s == PS_DUNES || s == PS_SEA; };
        for (int k = 0; k < 8 && base.surfStyle >= 13 && base.surfStyle <= 16 && floorStyle(st); k++)
            st = pickParticleStyle(r, x, fp, mood, avoid);
        const int slot = base.layerCount == 0 ? 0 : 1;
        makeLayer(r, fp, mood, t.layers[slot], st, true);
        t.layers[slot].count = std::min(1.f, t.layers[slot].count * 1.6f);
        t.layerCount = slot + 1;
        t.surfAmt = std::min(0.8f, base.surfAmt * 1.25f);
        t.layers[0].count = std::min(1.f, t.layers[0].count * 1.3f);
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

// ---- Scene adjustments (zenscene). They are applied after generation, so pinning one choice never changes the
// others: the random draws of the base scene stay the same.
namespace {

const char SF_KEYS[SF_COUNT] = {'m', 's', 'h', 'g', 'p', 'q', 'f', 'b', 'k', 'r', 'l'};
const char* SF_NAMES[SF_COUNT] = {"MOOD", "PALETTE", "HUE", "BACKGROUND", "PARTICLES", "PARTICLES 2", "SURFACE",
                                  "BLOCKS", "SHAPE", "FRAME", "LOCK EFFECT"};
const char* SCHEME_NAMES[8] = {"ANALOGOUS", "COMPLEMENTARY", "TRIADIC", "MONOCHROME", "DUOTONE", "PASTEL RAINBOW",
                               "SPLIT COMPLEMENT", "INK + ACCENT"};
const char* FR_NAMES[FR_COUNT] = {"OUTLINE", "CORNERS", "WELL", "FLOOR", "GRID", "NONE", "PILLARS", "DOUBLE", "DOTTED",
                                  "GLOW BASE", "TOP + BOTTOM", "TICKS", "SIDE FADE", "UNDERLINE", "CORNER DOTS",
                                  "RAILS", "DASHED", "DOT PILLARS"};
constexpr int HUE_STEPS = 24;

// v from the range [a0, a1] to the same position in [b0, b1].
float remap(float v, float a0, float a1, float b0, float b1) { return b0 + saturate((v - a0) / (a1 - a0)) * (b1 - b0); }

} // namespace

const char* sceneFieldName(int f) { return f >= 0 && f < SF_COUNT ? SF_NAMES[f] : "?"; }

int sceneFieldValues(int f) {
    switch (f) {
    case SF_MOOD: return 3;
    case SF_SCHEME: return 8;
    case SF_HUE: return HUE_STEPS;
    case SF_BG: return BG_COUNT;
    case SF_PART1: case SF_PART2: return PS_COUNT + 1; // 0 = none
    case SF_SURFACE: return 18;
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

static bool parseOverrides(const std::string& s, SceneOverrides& o) {
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find('_', i);
        if (j == std::string::npos) j = s.size();
        const std::string tok = s.substr(i, j - i);
        i = j + 1;
        if (tok.size() < 2) return false;
        int f = 0;
        while (f < SF_COUNT && SF_KEYS[f] != std::tolower((unsigned char)tok[0])) f++;
        if (f == SF_COUNT) return false;
        char* end = nullptr;
        const long v = std::strtol(tok.c_str() + 1, &end, 10);
        if (*end || v < 0 || v >= sceneFieldValues(f)) return false;
        o.v[f] = (int)v;
    }
    return true;
}

void applyOverrides(Theme& t, const Footprint& fp, const SceneOverrides& o) {
    const int oldMood = (int)std::lround(t.pal.mood);
    const int mood = o.v[SF_MOOD] >= 0 ? o.v[SF_MOOD] : oldMood;
    if (mood != oldMood) {
        // The mood-dependent settings keep their place in the new mood's range.
        auto emi = [](int m, float& a, float& b) { a = m == 2 ? 0.15f : 0.7f; b = m == 2 ? 0.4f : 1.6f; };
        auto blo = [](int m, float& a, float& b) { a = m == 0 ? 0.6f : (m == 1 ? 0.35f : 0.12f); b = m == 0 ? 1.6f : (m == 1 ? 0.8f : 0.3f); };
        auto vig = [](int m, float& a, float& b) { a = m == 2 ? 0.05f : 0.15f; b = m == 2 ? 0.25f : 0.55f; };
        auto srf = [](int m, float& a, float& b) { a = m == 2 ? 0.25f : 0.3f; b = m == 2 ? 0.45f : 0.6f; };
        auto move = [&](float& v, auto range) {
            float a0, a1, b0, b1;
            range(oldMood, a0, a1);
            range(mood, b0, b1);
            v = remap(v, a0, a1, b0, b1);
        };
        move(t.emissive, emi);
        move(t.bloom, blo);
        move(t.vignette, vig);
        move(t.surfAmt, srf);
        t.pal.mood = (float)mood;
    }
    if (o.v[SF_SCHEME] >= 0) t.pal.scheme = o.v[SF_SCHEME];
    if (o.v[SF_HUE] >= 0) t.pal.hue = o.v[SF_HUE] * TAU / HUE_STEPS;
    resolvePalette(t, t.hueShift);

    if (o.v[SF_BG] >= 0) t.bgStyle = o.v[SF_BG];
    // A pinned particle layout gets its own settings, from its own random stream.
    auto layer = [&](int slot, int style) {
        Rng r(t.seed ^ splitmix64(0x9A57ull + (uint64_t)slot * 64 + (uint64_t)style));
        makeLayer(r, fp, mood, t.layers[slot], style, slot == 1);
    };
    if (o.v[SF_PART1] == 0) t.layerCount = 0;
    else if (o.v[SF_PART1] > 0) {
        layer(0, o.v[SF_PART1] - 1);
        t.layerCount = std::max(t.layerCount, 1);
    }
    if (t.layerCount > 0) { // a second layout only with a first one
        if (o.v[SF_PART2] == 0) t.layerCount = 1;
        else if (o.v[SF_PART2] > 0) {
            layer(1, o.v[SF_PART2] - 1);
            t.layerCount = 2;
        }
    }
    if (o.v[SF_SURFACE] >= 0) {
        if (t.surfStyle == 0 && o.v[SF_SURFACE] > 0) {
            t.surfAmt = mood == 2 ? 0.35f : 0.45f;
            t.surfScale = 1.f;
        }
        t.surfStyle = o.v[SF_SURFACE];
    }
    if (o.v[SF_BLOCK] >= 0) t.blockStyle = o.v[SF_BLOCK];
    if (o.v[SF_MESH] >= 0 && o.v[SF_MESH] != t.blockMesh) {
        auto scale = [](int m, float& a, float& b) {
            a = m == MESH_SPHERE ? 0.85f : (m == MESH_GEM ? 0.95f : 0.8f);
            b = m == MESH_SPHERE ? 1.f : (m == MESH_GEM ? 1.05f : 0.96f);
        };
        float a0, a1, b0, b1;
        scale(t.blockMesh, a0, a1);
        scale(o.v[SF_MESH], b0, b1);
        t.blockScale = remap(t.blockScale, a0, a1, b0, b1);
        t.blockMesh = o.v[SF_MESH];
        if (t.blockMesh != MESH_CUBE) t.blockDepth = 1.f;
        t.meshExp = meshExponent(t.blockMesh, t.roundness);
    }
    if (o.v[SF_FRAME] >= 0) t.frameStyle = o.v[SF_FRAME];
    if (o.v[SF_LOCK] >= 0) t.lockEffect = o.v[SF_LOCK];

    t.name = themeName(t);
    std::string code = t.code.substr(0, t.code.find('_'));
    for (int f = 0; f < SF_COUNT; f++)
        if (o.v[f] >= 0) code += std::string("_") + SF_KEYS[f] + std::to_string(o.v[f]);
    t.code = code;
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
