#pragma once
// Procedural scene description. Every run / song produces a different combination, steered by the
// song footprint (key -> hue, brightness -> mood, bass/air -> particle layouts, tempo -> motion...).
#include <string>

#include "analysis.hpp"
#include "mathutil.hpp"

enum BgStyle { BG_GRADIENT, BG_RADIAL, BG_HORIZON, BG_NEBULA, BG_FLAT, BG_AURORA, BG_BANDS, BG_SPOTLIGHT,
               BG_GRID, BG_HILLS, BG_CONIC, BG_STARS,
               BG_DUALGLOW, BG_WAVES, BG_CLOUDS, BG_POLKA, BG_DIAGONAL, BG_SUNBURST, BG_RIPPLES, BG_PLASMA,
               BG_SEA, BG_PEAKS, BG_SHAFTS, BG_HALORING,
               // Added later (disabled until reviewed, see scene-options.txt):
               BG_MOON, BG_RIDGELINES, BG_CITY, BG_COLORMESH, BG_ECLIPSE, BG_CIRRUS, BG_CANYON, BG_SWIRL, BG_FOREST,
               BG_ISOMETRIC, BG_PLANET, BG_DESERT, BG_ARCTIC, BG_VOLCANO, BG_ROSEWINDOW, BG_TEMPLE, BG_JUNGLE,
               BG_PAPERCUT, BG_BLUEPRINT, BG_LIGHTHOUSE, BG_COUNT };
enum ParticleStyle {
    PS_GALAXY, PS_TUNNEL, PS_WAVES, PS_SPHERE, PS_DRIFT, PS_STREAMS, PS_WARP,
    PS_AURORA, PS_HALOS, PS_HELIX, PS_BOKEH, PS_LATTICE,
    PS_FIREFLIES, PS_RAIN, PS_VORTEX, PS_WAVEFORM, PS_STARBURST, PS_ORBITS, PS_CONFETTI,
    PS_SNOWGLOBE, PS_LADDER, PS_FOUNTAIN, PS_PETALS, PS_CONSTELLATION, PS_TORUS, PS_WALL, PS_COMETS,
    PS_SPARKLERS, PS_BUBBLES, PS_CUBESHELL, PS_LEMNISCATE, PS_BEATRINGS, PS_PLASMA, PS_MOIRE, PS_SWARM,
    PS_SPIRALS, PS_RIBBON, PS_METEORS, PS_SKYLINE, PS_CORONA, PS_DUNES, PS_WIND, PS_FIRE, PS_SEA,
    // Added later (disabled until reviewed):
    PS_FIREWORKS, PS_JELLYFISH, PS_DANDELION, PS_PENDULUMS, PS_KOI, PS_LANTERNS, PS_SNOWFALL, PS_GEARS, PS_SPIROGRAPH,
    PS_PULSEGRID, PS_BIRDS, PS_BUTTERFLIES, PS_ATOMS, PS_RAINDROPS, PS_EMBERS, PS_SUNFLOWER, PS_TENTACLES,
    PS_MANDALA, PS_SATELLITES, PS_NOTES, PS_COUNT
};
enum ParticleShape { SH_DOT, SH_RING, SH_SPARKLE, SH_SQUARE, SH_DIAMOND, SH_DISC, SH_STREAK, SH_PLUS, SH_STAR, SH_HEX,
                     SH_TRIANGLE, SH_HEART, SH_CRESCENT, SH_XCROSS, SH_DOUBLERING, SH_SQUARE_OUT, SH_DIAMOND_OUT,
                     SH_FLARE, SH_BAR, SH_RINGDOT, SH_COUNT };
enum BlockStyle { BS_GLASS, BS_SOLID, BS_WIRE, BS_LANTERN, BS_INSET, BS_DOTS, BS_FRESNEL, BS_SPLIT,
                  BS_HOLO, BS_GRADIENT, BS_DOUBLE,
                  BS_NEON, BS_CIRCUIT, BS_FROSTED, BS_CHECKER, BS_RINGS, BS_BEVEL, BS_PIXEL, BS_STRIPES,
                  BS_CORE, BS_HATCH, BS_BREATH,
                  // Added later (disabled until reviewed):
                  BS_KINTSUGI, BS_TERRAZZO, BS_CANDY, BS_ENAMEL, BS_PILLOW, BS_SCALES, BS_WAFFLE, BS_OPAL, BS_STAINED,
                  BS_PAPER, BS_GUMMY, BS_BRUSHED, BS_GLAZE, BS_MARBLE, BS_WOOD, BS_STITCH, BS_STUDS, BS_CARBON,
                  BS_VELVET, BS_LED, BS_COUNT };
enum BlockMesh { MESH_CUBE, MESH_ROUNDED, MESH_SPHERE, MESH_GEM,
                 // Added later (disabled until reviewed):
                 MESH_CHAMFER, MESH_PILLOW, MESH_TILE, MESH_COIN, MESH_OCTAGON, MESH_HEX, MESH_DIAMOND, MESH_DOME,
                 MESH_CROSS, MESH_STAR, MESH_HEART, MESH_DROP, MESH_FLOWER, MESH_RING, MESH_PYRAMID, MESH_CAPSULE,
                 MESH_SHIELD, MESH_BLOB, MESH_TRIANGLE, MESH_ZIGGURAT, MESH_COUNT };
// How cleared blocks disappear. Each theme uses a set of 3; each clear picks one.
enum ClearEffect { CE_SHRINK, CE_RISE, CE_SCATTER, CE_SQUASH, CE_SWEEP, CE_FOLD, CE_MELT, CE_SPARKLE,
                   CE_POUR, CE_ZIP, CE_BLOOM,
                   CE_FLIP, CE_DROPOUT, CE_DOMINO, CE_IMPLODE, CE_SPREAD, CE_WAVE, CE_SLICE, CE_PULSE,
                   CE_STRETCH, CE_SINK, CE_CASCADE, CE_COUNT };
// How a piece settles when it locks. Each theme uses one.
enum LockEffect { LE_POP, LE_AFTERGLOW, LE_BOUNCE, LE_SQUASH, LE_GROW, LE_PRESS, LE_TWINKLE, LE_RIPPLE, LE_CASCADE,
                  LE_EMBER, LE_HUESHIFT,
                  // Added later (disabled until reviewed):
                  LE_SHOCKWAVE, LE_SPARKS, LE_MAGNET, LE_JELLY, LE_FLIP, LE_INK, LE_SHIMMER, LE_FROST, LE_DUST,
                  LE_HEARTBEAT, LE_SPLASH, LE_STAMP, LE_PETALS, LE_BLOOM, LE_ROWWAVE, LE_SPIN, LE_CONFETTI, LE_DRIP,
                  LE_ECHO, LE_AURA, LE_COUNT };
const char* lockEffectName(int e);
enum FrameStyle { FR_OUTLINE, FR_CORNERS, FR_WELL, FR_FLOOR, FR_GRID, FR_NONE, FR_PILLARS, FR_DOUBLE, FR_DOTTED,
                  FR_GLOWBASE, FR_TOPBOTTOM, FR_TICKS, FR_SIDEFADE, FR_UNDERLINE, FR_CORNERDOTS, FR_RAILS, FR_DASHED,
                  FR_DOTPILLARS,
                  // Added later (disabled until reviewed):
                  FR_BRACKETS, FR_ARCH, FR_RULER, FR_CHEVRONS, FR_BEADS, FR_NEONTUBE, FR_ZIGZAG, FR_LATTICE, FR_ORBIT,
                  FR_PEDESTAL, FR_FILMSTRIP, FR_PIANO, FR_VINES, FR_CIRCUIT, FR_CRENELS, FR_WAVES, FR_STARS,
                  FR_LANTERNS, FR_BRACES, FR_SCANNER, FR_COUNT };
// Palette schemes and continuous surfaces (see SCHEME_NAMES and SURF_NAMES in theme.cpp).
constexpr int SCHEME_COUNT = 28, SURF_COUNT = 38;

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
    vec3 wellTint; // pale scenes' board backing: a light tint of the scene, not the same white everywhere
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
    int lockEffect = LE_POP;
    // Audio equalizer decoration.
    int eqStyle = 0;        // layout (disabled: always 0): 0 none, 1 beside the board (bottom), 2 along both sides, 3 ring
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
    std::string code; // scene code: themeFromCode(code) gives this scene back, whatever the song
};

// A scene's discrete identity: one value per field. Scene codes are the identity, 2 hex digits per field in this
// order; everything else about the scene comes from a seed made of the identity (see buildTheme).
enum SceneField { SF_MOOD, SF_SCHEME, SF_HUE, SF_BG, SF_PART1, SF_PART2, SF_SURFACE, SF_BLOCK, SF_MESH, SF_FRAME,
                  SF_LOCK, SF_COUNT };
struct SceneId {
    int v[SF_COUNT] = {};
};
const char* sceneFieldName(int field);
int sceneFieldValues(int field);                        // number of values (0 .. n-1)
std::string sceneFieldValueName(int field, int value);
int sceneFieldValue(const Theme& t, int field);         // the scene's current value
SceneId sceneId(const Theme& t);
std::string sceneCode(const SceneId& id);
bool parseSceneCode(const std::string& code, SceneId& id);
// The identity a song and a seed give: the song steers the weights; options switched off are never picked.
SceneId pickIdentity(const Footprint& fp, uint64_t seed);
// The scene of an identity: the same identity always gives the same scene.
Theme buildTheme(const SceneId& id);

// Scene options switched off (src/scene-options.txt, built in; edited with zenscene): scenes never pick them, but a
// scene code that uses one still shows it. New options join the list (disabled) until they are reviewed.
bool sceneOptionDisableable(int field, int value);
bool sceneOptionEnabled(int field, int value);
void setSceneOptionEnabled(int field, int value, bool on);
// The list as text: one "FIELD VALUE" per line, as the menu names them (PARTICLES covers both layers), # comments.
// Loading replaces the current list; unknown lines are reported on stderr.
void loadSceneOptions(const std::string& text);
std::string sceneOptionsText();

// A new scene for a song: pickIdentity then buildTheme.
Theme generateTheme(const Footprint& fp, uint64_t seed);
// The scene of a scene code (as shown in the corner); *ok is false if the code is malformed (a random scene then).
Theme themeFromCode(const std::string& code, bool* ok = nullptr);
float meshExponent(int mesh, float roundness);
// Variant of a song's base theme for an intensity level (0 calm, 1 mid, 2 peak). The song's identity
// (background, main particles, blocks, frame, mood) is preserved; only color, glow and extras change.
Theme evolveTheme(const Theme& base, int level, float energy);
// Re-resolves the palette with a hue shift (used on song section changes).
void resolvePalette(Theme& t, float hueShift);
// Blend continuous parameters; discrete ones come from `b` when t >= 0.5.
Theme blendThemes(const Theme& a, const Theme& b, float t);
