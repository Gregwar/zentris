#pragma once
// Procedural scene description. Every run / song produces a different combination, steered by the
// song footprint (key -> hue, brightness -> mood, bass/air -> particle layouts, tempo -> motion...).
#include <string>

#include "analysis.hpp"
#include "mathutil.hpp"

enum BgStyle { BG_GRADIENT, BG_RADIAL, BG_HORIZON, BG_NEBULA, BG_FLAT, BG_AURORA, BG_BANDS, BG_SPOTLIGHT, BG_COUNT };
enum ParticleStyle {
    PS_GALAXY, PS_TUNNEL, PS_WAVES, PS_SPHERE, PS_DRIFT, PS_STREAMS, PS_WARP,
    PS_AURORA, PS_HALOS, PS_HELIX, PS_BOKEH, PS_LATTICE, PS_COUNT
};
enum ParticleShape { SH_DOT, SH_RING, SH_SPARKLE, SH_SQUARE, SH_DIAMOND, SH_DISC, SH_STREAK, SH_PLUS, SH_COUNT };
enum BlockStyle { BS_GLASS, BS_SOLID, BS_WIRE, BS_LANTERN, BS_INSET, BS_DOTS, BS_FRESNEL, BS_SPLIT, BS_COUNT };
enum BlockMesh { MESH_CUBE, MESH_ROUNDED, MESH_SPHERE, MESH_GEM, MESH_COUNT };
// How cleared blocks disappear. Each theme uses a set of 3; each clear picks one.
enum ClearEffect { CE_SHRINK, CE_RISE, CE_SCATTER, CE_SQUASH, CE_SWEEP, CE_FOLD, CE_MELT, CE_SPARKLE, CE_COUNT };
enum FrameStyle { FR_OUTLINE, FR_CORNERS, FR_WELL, FR_FLOOR, FR_GRID, FR_NONE, FR_PILLARS, FR_COUNT };

struct PaletteParams {
    float hue = 0;        // radians
    int scheme = 0;
    float chroma = 0.12f;
    float mood = 0;       // 0 dark, 1 dusk, 2 pale
    float bgHueOffset = 0;
    float bgChroma = 0.04f;
    uint64_t perm = 0;    // piece color permutation seed
    float spread = 0.3f;
};

struct ParticleLayer {
    int style = PS_GALAXY;
    int shape = SH_DOT;
    float count = 0.5f;   // fraction of the particle budget
    float size = 1.f;
    float speed = 1.f;
    float bright = 1.f;
    float p[4] = {0.5f, 0.5f, 0.5f, 0.5f};
};

struct Theme {
    uint64_t seed = 0;
    PaletteParams pal;

    // Resolved colors (linear RGB).
    vec3 bgTop, bgBottom, bgGlow;
    vec3 piece[7];
    vec3 accent, text;
    vec3 partA, partB, partC;
    vec3 shadowTint, highlightTint;
    float pale = 0;       // 0 = additive glow on dark, 1 = soft pale theme

    int bgStyle = BG_GRADIENT;
    float bgP[4] = {0.5f, 0.5f, 0.5f, 0.5f};

    ParticleLayer layers[2];
    int layerCount = 1;

    int blockStyle = BS_GLASS;
    int blockMesh = MESH_CUBE;
    float roundness = 4.f;  // superellipsoid exponent for MESH_ROUNDED
    float meshExp = 24.f;   // resolved block shape exponent (1 gem .. 2 sphere .. ~24 cube)
    float blockScale = 0.9f;
    float blockDepth = 1.f;
    float edgeWidth = 0.08f;
    float emissive = 1.f;
    float fillAlpha = 0.5f;
    float ghostAlpha = 0.25f;

    int clearEffects[3] = {CE_SHRINK, CE_RISE, CE_SWEEP};
    int frameStyle = FR_OUTLINE;
    float frameAlpha = 0.6f;

    float camDist = 30, camPitch = 0, camYaw = 0, fov = 45, sway = 1, swaySpeed = 0.1f, roll = 0;
    float boardY = 0;

    float bloom = 0.8f, bloomThreshold = 0.8f, vignette = 0.3f, chroma = 0.001f, grain = 0.02f, exposure = 1.f,
          saturation = 1.f, scanlines = 0.f;

    float beatPulse = 0.5f, bassReact = 0.5f, highReact = 0.5f;
    float hueDrift = 0.3f;
    float hueShift = 0.f;   // hue offset currently applied by resolvePalette

    std::string name;
};

Theme generateTheme(const Footprint& fp, uint64_t seed);
float meshExponent(int mesh, float roundness);
// Variant of a song's base theme for an intensity level (0 calm, 1 mid, 2 peak). The song's identity
// (background, main particles, blocks, frame, mood) is preserved; only color, glow and extras change.
Theme evolveTheme(const Theme& base, const Footprint& fp, int level, float energy, bool unused = true);
// Re-resolves the palette with a hue shift (used on song section changes).
void resolvePalette(Theme& t, float hueShift);
// Blend continuous parameters; discrete ones come from `b` when t >= 0.5.
Theme blendThemes(const Theme& a, const Theme& b, float t);
