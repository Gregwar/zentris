#include "theme.hpp"

#include <algorithm>

namespace {

const char* BG_NAMES[BG_COUNT] = {"GRADIENT", "HALO", "HORIZON", "NEBULA", "VOID", "AURORA SKY", "BANDS", "SPOTLIGHT",
                                  "GRID", "HILLS", "CONIC", "STARFIELD"};
const char* PS_NAMES[PS_COUNT] = {"GALAXY", "TUNNEL", "OCEAN", "SPHERE", "DRIFT", "STREAMS",
                                  "WARP", "CURTAINS", "HALOS", "HELIX", "BOKEH", "LATTICE",
                                  "FIREFLIES", "RAIN", "VORTEX", "WAVEFORM", "STARBURST", "ORBITS", "CONFETTI"};
const char* BS_NAMES[BS_COUNT] = {"GLASS", "SOLID", "WIRE", "LANTERN", "INSET", "DOTS", "CRYSTAL", "SPLIT",
                                  "HOLO", "GRADIENT", "DOUBLE"};
const char* MESH_NAMES[MESH_COUNT] = {"CUBE", "ROUNDED", "ORB", "GEM"};
const char* MOOD_NAMES[3] = {"NIGHT", "DUSK", "PALE"};

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


int pickBackground(Rng& r, const Footprint& fp, int mood) {
    float w[BG_COUNT] = {2.f, 1.8f, mood == 2 ? 0.4f : 1.2f, mood == 2 ? 0.f : 1.5f,
                         1.0f, mood == 2 ? 0.f : 1.0f + fp.airWeight, 1.0f, 1.0f,
                         mood == 2 ? 0.3f : 0.8f + 0.8f * fp.bassWeight, 1.0f, 0.9f, mood == 2 ? 0.f : 1.2f};
    return r.weighted(w);
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
    };
    if (avoid >= 0) w[avoid] = 0;
    return r.weighted(w);
}

int pickShape(Rng& r, int style) {
    switch (style) {
    case PS_WARP: return r.chance(0.8f) ? (int)SH_STREAK : (int)SH_DOT;
    case PS_BOKEH: return r.chance(0.6f) ? (int)SH_DISC : (r.chance(0.5f) ? (int)SH_RING : (int)SH_DOT);
    case PS_RAIN: return (int)SH_STREAK;
    case PS_CONFETTI: { float w[SH_COUNT] = {0, 0, 0, 2, 1.5f, 0, 0, 0.5f, 1, 1}; return r.weighted(w); }
    case PS_FIREFLIES: return r.chance(0.7f) ? (int)SH_DOT : (int)SH_SPARKLE;
    case PS_LATTICE: { float w[SH_COUNT] = {2, 0.5f, 0.5f, 1.5f, 1, 0, 0, 1.5f, 0.5f, 1}; return r.weighted(w); }
    case PS_TUNNEL: { float w[SH_COUNT] = {2, 0.5f, 0.5f, 0.8f, 0.8f, 0, 1.5f, 0.5f, 0.3f, 0.5f}; return r.weighted(w); }
    default: { float w[SH_COUNT] = {3, 0.7f, 1.0f, 0.6f, 0.8f, 0.3f, 0.4f, 0.5f, 0.5f, 0.5f}; return r.weighted(w); }
    }
}

void makeLayer(Rng& r, const Footprint& fp, int, ParticleLayer& L, int style, bool secondary) {
    const float bpmN = tempoNorm(fp);
    L.style = style;
    L.shape = pickShape(r, style);
    L.count = secondary ? r.range(0.12f, 0.35f) : r.range(0.45f, 1.0f) * (0.7f + 0.5f * fp.density);
    L.size = r.range(0.6f, 1.5f) * (secondary ? 0.8f : 1.f);
    L.speed = r.range(0.6f, 1.4f) * (0.7f + 0.6f * bpmN);
    L.bright = r.range(0.6f, 1.1f) * (secondary ? 0.8f : 1.f);
    for (float& v : L.p) v = r.uniform();
}

std::string themeName(const Theme& t) {
    const int mood = (int)std::lround(t.pal.mood);
    return std::string(MOOD_NAMES[mood]) + " / " + BS_NAMES[t.blockStyle] + " " + MESH_NAMES[t.blockMesh] + " / " +
           PS_NAMES[t.layers[0].style] + (t.layerCount > 1 ? std::string(" + ") + PS_NAMES[t.layers[1].style] : "") +
           " / " + BG_NAMES[t.bgStyle];
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
    for (int i = 0; i < 7; i++) {
        float f = (float)i / 6.f;
        float jitter = r.range(-0.04f, 0.04f);
        Ls[i] = baseL + r.range(-0.05f, 0.05f);
        Cs[i] = C;
        switch (p.scheme) {
        case 0: hues[i] = h + (i - 3) * p.spread * 0.45f; break;                           // analogous
        case 1: hues[i] = h + (i % 2 ? PI : 0.f) + (i / 2) * 0.12f - 0.18f; break;        // complementary
        case 2: hues[i] = h + (i % 3) * TAU / 3.f + (i / 3) * 0.14f; break;                 // triadic
        case 3:                                                                               // monochrome
            hues[i] = h + jitter;
            Ls[i] = baseL + (f - 0.5f) * (mood == 2 ? 0.3f : 0.24f);
            Cs[i] = C * (0.5f + 0.7f * f);
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
    case MESH_GEM: return 1.f;
    case MESH_SPHERE: return 2.f;
    case MESH_ROUNDED: return roundness;
    default: return 24.f;
    }
}

Theme generateTheme(const Footprint& fp, uint64_t seed) {
    Theme t;
    t.seed = seed;
    Rng r(seed ^ splitmix64(fp.hash));
    const float bpmN = saturate((fp.bpm - 70.f) / 90.f); // 0 slow .. 1 fast

    // ---- Palette: key on the circle of fifths gives the base hue (synesthetic mapping).
    int fifths = (fp.key * 7) % 12;
    t.pal.hue = fifths / 12.f * TAU + r.range(-0.6f, 0.6f);
    float moodW[3] = {0.62f - 0.25f * fp.brightness + (fp.minor ? 0.12f : 0.f),
                      0.2f + 0.2f * fp.brightness, 0.08f + 0.22f * fp.brightness * (1.f - 0.5f * fp.bassWeight)};
    t.pal.mood = (float)r.weighted(moodW);
    float schemeW[8] = {3.f, 2.f, 1.2f, 1.3f, 2.6f, 0.8f + fp.brightness, 1.5f, 0.8f + (t.pal.mood == 0 ? 0.6f : 0.f)};
    t.pal.scheme = r.weighted(schemeW);
    t.pal.chroma = r.range(0.08f, 0.18f) * (fp.minor ? 0.85f : 1.f) * (0.85f + 0.3f * fp.dynamics);
    t.pal.spread = r.range(0.15f, 0.5f);
    t.pal.bgHueOffset = r.chance(0.6f) ? r.range(-0.4f, 0.4f) : r.range(2.2f, 4.0f);
    t.pal.bgChroma = r.range(0.015f, 0.07f);
    t.pal.perm = r.next();
    for (float& v : t.bgP) v = r.uniform();
    resolvePalette(t, 0);
    const int mood = (int)t.pal.mood;

    t.bgStyle = pickBackground(r, fp, mood);
    makeLayer(r, fp, mood, t.layers[0], pickParticleStyle(r, fp, mood, -1), false);
    t.layerCount = 1;
    if (r.chance(0.55f)) {
        makeLayer(r, fp, mood, t.layers[1], pickParticleStyle(r, fp, mood, t.layers[0].style), true);
        t.layerCount = 2;
    }

    // ---- Blocks.
    {
        float w[BS_COUNT] = {mood == 2 ? 0.8f : 3.f, mood == 2 ? 2.5f : 1.2f, 1.2f, mood == 2 ? 0.3f : 1.3f,
                             1.3f, 0.9f, 1.3f, 1.0f, mood == 2 ? 0.4f : 1.1f, 1.1f, 1.0f};
        t.blockStyle = r.weighted(w);
        float mw[MESH_COUNT] = {5.5f, 2.5f, 1.0f, 1.0f};
        if (t.blockStyle == BS_DOTS || t.blockStyle == BS_INSET || t.blockStyle == BS_SPLIT || t.blockStyle == BS_DOUBLE)
            mw[2] = mw[3] = 0;
        t.blockMesh = r.weighted(mw);
        t.roundness = r.range(3.f, 7.f);
        t.blockScale = r.range(0.8f, 0.96f);
        if (t.blockMesh == MESH_SPHERE || t.blockMesh == MESH_GEM) t.blockScale = r.range(0.85f, 1.0f);
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

    // ---- Energy decorations: an audio equalizer and/or light rays behind the board.
    {
        float w[5] = {1.0f, 1.f, 1.f, 0.8f, 0.8f};
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
    }

    // ---- Board frame.
    {
        float w[FR_COUNT] = {2.f, 1.5f, 1.5f, 1.2f, 1.0f, 0.8f, 1.0f, 1.0f, 1.0f};
        t.frameStyle = r.weighted(w);
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
        t.chroma = r.chance(0.5f) ? r.range(0.0005f, 0.0028f) : 0.f;
        t.grain = r.range(0.01f, 0.05f);
        t.exposure = r.range(0.95f, 1.15f);
        t.saturation = r.range(0.88f, 1.15f);
        t.scanlines = r.chance(0.08f) ? r.range(0.03f, 0.07f) : 0.f;
        t.beatPulse = r.range(0.5f, 0.9f) * (0.6f + 0.4f * fp.dynamics);
        t.bassReact = r.range(0.3f, 1.0f);
        t.highReact = r.range(0.3f, 1.0f);
        t.hueDrift = (r.chance(0.5f) ? 1.f : -1.f) * r.range(0.4f, 0.9f);
    }

    t.name = themeName(t);
    return t;
}

// A song keeps one visual identity: background, main particle layout, blocks, frame and mood never change
// inside a song. Levels (0 calm, 1 mid, 2 peak) only shift hue a little and change color intensity, glow and
// an extra particle layer.
// Levels: 0 calm (sparse, muted, cooler), 1 = the song's base scene, 2 peak (fuller, warmer, glowing).
// The song's identity (background, main particles, blocks, frame, mood) never changes.
Theme evolveTheme(const Theme& base, const Footprint& fp, int level, float energy, bool) {
    if (level == 1) return base;
    Theme t = base;
    Rng r(base.seed ^ splitmix64(0xE7011EULL + (uint64_t)level));
    const int mood = (int)std::lround(base.pal.mood);
    const float e = saturate(energy);
    if (level <= 0) {
        // Calm: sparse, muted, dim.
        t.pal.chroma = base.pal.chroma * 0.6f;
        t.pal.bgChroma = base.pal.bgChroma * 0.5f;
        t.layerCount = 1;
        t.layers[0].bright *= 0.65f;
        t.layers[0].count *= 0.55f;
        t.bloom = base.bloom * 0.6f;
        t.saturation = clampf(base.saturation * 0.82f, 0.7f, 1.2f);
        t.emissive = base.emissive * 0.7f;
        t.exposure = base.exposure * 0.9f;
        t.vignette = std::min(0.7f, base.vignette + 0.12f);
        t.camDist = base.camDist * 1.03f;
        resolvePalette(t, -base.hueDrift * 0.8f);
    } else {
        // Peak: always a new particle layer (a style the song has not shown yet), richer color, more glow.
        t.pal.chroma = std::min(0.24f, base.pal.chroma * (1.3f + 0.2f * e));
        t.pal.bgChroma = std::min(0.14f, base.pal.bgChroma * 1.8f + (mood == 2 ? 0.04f : 0.02f));
        int avoid = base.layers[0].style;
        int st = pickParticleStyle(r, fp, mood, avoid);
        if (base.layerCount > 1 && st == base.layers[1].style) st = pickParticleStyle(r, fp, mood, avoid);
        makeLayer(r, fp, mood, t.layers[1], st, true);
        t.layers[1].count = std::min(1.f, t.layers[1].count * 1.6f);
        t.layerCount = 2;
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
