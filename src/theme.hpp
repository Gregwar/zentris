#pragma once
// Procedural scene description. Every run / song produces a different combination, steered by the
// song footprint (key -> hue, brightness -> mood, bass/air -> particle layouts, tempo -> motion...).
#include <string>

#include "analysis.hpp"
#include "mathutil.hpp"

enum BgStyle { BG_GRADIENT, BG_RADIAL, BG_HORIZON, BG_NEBULA, BG_FLAT, BG_AURORA, BG_BANDS, BG_SPOTLIGHT,
               BG_GRID, BG_HILLS, BG_CONIC, BG_STARS,
               BG_DUALGLOW, BG_WAVES, BG_CLOUDS, BG_POLKA, BG_DIAGONAL, BG_SUNBURST, BG_RIPPLES, BG_PLASMA,
               BG_SEA, BG_PEAKS, BG_SHAFTS, BG_HALORING, BG_COUNT };
enum ParticleStyle {
    PS_GALAXY, PS_TUNNEL, PS_WAVES, PS_SPHERE, PS_DRIFT, PS_STREAMS, PS_WARP,
    PS_AURORA, PS_HALOS, PS_HELIX, PS_BOKEH, PS_LATTICE,
    PS_FIREFLIES, PS_RAIN, PS_VORTEX, PS_WAVEFORM, PS_STARBURST, PS_ORBITS, PS_CONFETTI,
    PS_SNOWGLOBE, PS_LADDER, PS_FOUNTAIN, PS_PETALS, PS_CONSTELLATION, PS_TORUS, PS_WALL, PS_COMETS,
    PS_SPARKLERS, PS_BUBBLES, PS_CUBESHELL, PS_LEMNISCATE, PS_BEATRINGS, PS_PLASMA, PS_MOIRE, PS_SWARM,
    PS_SPIRALS, PS_RIBBON, PS_METEORS, PS_COUNT
};
enum ParticleShape { SH_DOT, SH_RING, SH_SPARKLE, SH_SQUARE, SH_DIAMOND, SH_DISC, SH_STREAK, SH_PLUS, SH_STAR, SH_HEX,
                     SH_TRIANGLE, SH_HEART, SH_CRESCENT, SH_XCROSS, SH_DOUBLERING, SH_SQUARE_OUT, SH_DIAMOND_OUT,
                     SH_FLARE, SH_BAR, SH_RINGDOT, SH_COUNT };
enum BlockStyle { BS_GLASS, BS_SOLID, BS_WIRE, BS_LANTERN, BS_INSET, BS_DOTS, BS_FRESNEL, BS_SPLIT,
                  BS_HOLO, BS_GRADIENT, BS_DOUBLE,
                  BS_NEON, BS_CIRCUIT, BS_FROSTED, BS_CHECKER, BS_RINGS, BS_BEVEL, BS_PIXEL, BS_STRIPES,
                  BS_CORE, BS_HATCH, BS_BREATH, BS_COUNT };
enum BlockMesh { MESH_CUBE, MESH_ROUNDED, MESH_SPHERE, MESH_GEM, MESH_COUNT };
// How cleared blocks disappear. Each theme uses a set of 3; each clear picks one.
enum ClearEffect { CE_SHRINK, CE_RISE, CE_SCATTER, CE_SQUASH, CE_SWEEP, CE_FOLD, CE_MELT, CE_SPARKLE,
                   CE_POUR, CE_ZIP, CE_BLOOM,
                   CE_FLIP, CE_DROPOUT, CE_DOMINO, CE_IMPLODE, CE_SPREAD, CE_WAVE, CE_SLICE, CE_PULSE,
                   CE_STRETCH, CE_SINK, CE_CASCADE, CE_COUNT };
enum FrameStyle { FR_OUTLINE, FR_CORNERS, FR_WELL, FR_FLOOR, FR_GRID, FR_NONE, FR_PILLARS, FR_DOUBLE, FR_DOTTED,
                  FR_GLOWBASE, FR_TOPBOTTOM, FR_TICKS, FR_SIDEFADE, FR_UNDERLINE, FR_CORNERDOTS, FR_RAILS, FR_DASHED,
                  FR_DOTPILLARS, FR_COUNT };

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
    // Audio equalizer decoration.
    int eqStyle = 0;        // layout: 0 none, 1 beside the board (bottom), 2 along both sides, 3 ring
    int eqRender = 0;       // 0 bars, 1 bars + falling peak caps, 2 LED segments, 3 line plot, 4 mirrored, 5 needles
    int eqBars = 16;        // 16, 24, 32 or 48 (interpolated between the analysed bands)
    int eqColor = 0;        // 0 gradient along frequencies, 1 by height, 2 single accent
    float eqDecay = 8.f;    // how fast bars fall back (per second)
    float eqPeakFall = 0.5f;// how fast peak caps fall (height units per second)
    float eqAlpha = 0.5f;
    float rays = 0.f;       // light rays behind the board (0 = none)
    // Continuous full-screen layer (not made of points): smoke, silk, lava, caustics, ink, geometry...
    int surfStyle = 0;      // 0 none, see SURF_NAMES
    float surfAmt = 0.4f, surfScale = 1.f;
    float rayCount = 8.f;
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
