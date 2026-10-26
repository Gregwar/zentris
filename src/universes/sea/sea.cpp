// SEA: glass bottles of water over a sea that rises with the cleared lines.
//
// One world, seen from the side: a plain sky over a sea in three layers like cut paper (far, middle, near), each with
// its own slow swell and a few glints of light, over a sandy bed. Every cell of a piece is its own small square bottle
// of colored glass with a wooden stopper, with a gap to its neighbours: four bottles per piece. The glass shows the
// scene behind it, bent (a copy of the scene drawn so far), filtered by its color, darker where it is thick, with bright
// rims and a crisp specular; the water in it is clear, its surface level in the world (a bright meniscus).
//
// The song starts with no sea at all, only the sand. A cleared row's bottles are tossed out of the well to their side,
// each to its own place on the sand; it lands, tumbles and settles on its side, its neck outward; its stopper shoots
// off like a champagne cork and the water erupts from the neck under pressure, a blast then a thick jet tapering off
// over a second or two, arcing onto the sand (where it runs on) and into the sea (under the sea already: a pale plume
// and bubbles). The empty bottles stay on the sand for the whole song, in piles that settle into the sand by a layer
// once two deep (the oldest buried). The sea rises as that water lands, smoothly, for
// the whole song, up to about three quarters of the well's height. The deeper it gets, the darker its depths (down to
// a near-black blue, a few points of light living there) and the more life it holds, in layers: small schools of fish
// near the surface, jellyfish drifting and pulsing in the middle water, an anglerfish and siphonophores in the dark.
// Boats sail on it once it is deep enough, bigger as it deepens: dinghies and sailboats, then fishing boats, then
// ferries, container ships and liners.
//
// Calm parts leave the sea glassy, mid brings a gentle swell, a chorus a livelier one with a few whitecaps. A drop
// raises a storm, eased in over about a second: big, faster waves with whitecaps and spray, foam streaks, slanting
// rain, dark clouds, boats rocking hard, a lightning glow in the clouds on its first downbeat and every four bars,
// spray torn off the crests on the kicks. It calms back over a few seconds after the drop. Behind the well the sea
// stays flat under a calm, subdued panel (more opaque over the dark depths), so the glass reads at a glance. In the
// storm the fish dart away and the jellies and the deep glow a little brighter.
//
// Slow motion brings dusk and a glassy sea, game over breaks the whole stack row by row, and a change of universe
// fades the world to the bridge.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane (like GARDEN).
#include "universes/sea/sea.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/registry.hpp"
#include "universes/sea/shaders.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Barlow (Italic for titles, Medium for labels and numbers), SIL Open Font License, see OFL-Barlow.txt.
const unsigned char FONT_ITALIC[] = {
#include "universes/sea/Barlow-Italic.ttf.inc"
};
const unsigned char FONT_ROMAN[] = {
#include "universes/sea/Barlow-Medium.ttf.inc"
};

namespace shaders = sea_shaders;
using ugl::set1f;
using ugl::set1i;
using ugl::set2f;
using ugl::set3f;
using ugl::set4f;
using ugl::setMat;

// Compiles a program, the shared helpers inserted after each #version line (the swell too, when `waves`).
GLuint compile(const char* vs, const char* fs, const char* name, bool waves = false) {
    std::string f(fs), v(vs);
    if (waves) f.insert(f.find('\n') + 1, shaders::WAVE_GLSL);
    f.insert(f.find('\n') + 1, shaders::COMMON_GLSL);
    if (waves) {
        v.insert(v.find('\n') + 1, shaders::WAVE_GLSL);
        v.insert(v.find('\n') + 1, shaders::COMMON_GLSL);
    }
    return ugl::program(v.c_str(), f.c_str(), name);
}

vec2 v2(float x, float y) { return vec2{x, y}; }
vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }
float hashf(float a, float b) {
    const uint64_t h = splitmix64((uint64_t)(int64_t)std::lround(a * 1000.f) * 73856093ull ^ (uint64_t)(int64_t)std::lround(b * 1000.f) * 19349663ull);
    return (h >> 40) * (1.0f / 16777216.0f);
}

constexpr float WELL_X = 5.1f;
// the sea's level: the near layer's base. The sea starts empty, under the sand (its far layer below the lowest dune),
// and rises as water falls in, up to about three quarters of the well's height
constexpr float SEA_LOW = -12.6f, SEA_HIGH = 5.0f;
// deep enough for boats: the near layer above the highest dune
constexpr float BOAT_LEVEL = -8.3f;
// how many poured lines raise the sea about two thirds of the way (most of a song's lines to fill it)
constexpr float SEA_LINES = 14.f;
// the sandy bed's mean height (sandTop)
constexpr float SAND_Y = -9.35f;
// the bottles: gravity in flight; a bottle lying on its side (its thickness, its center over what it lies on); the
// piles' columns; the stopper's center and the neck's mouth (CELL_FS's CAP_C, NECK_C + NECK_H); how full they are
constexpr float B_GRAV = 22.f, B_LAYER = 0.84f, B_LIFT = 0.41f, B_COL = 1.02f, CAP_Y = 0.4f, MOUTH_Y = 0.35f, B_FULL = 0.7f;
// the empty bottles sink slowly into the sand (a layer in this many seconds) and are gone once under it
constexpr float B_SINK_T = 12.f;
// how many bottles stay on the sand at most (the oldest go first, buried)
constexpr int B_MAX = 420;
// clear water, almost colorless (linear)
const vec3 WATER{0.74f, 0.86f, 0.95f};
// the layers (WAVE_GLSL's L_OFF, L_AMP, L_FRQ, L_SPD)
const float L_OFF[3] = {1.15f, 0.55f, 0.f}, L_AMP[3] = {0.35f, 0.65f, 1.f}, L_FRQ[3] = {1.7f, 1.25f, 0.85f}, L_SPD[3] = {0.6f, 0.8f, 1.f};
// a boat's half length at scale 1 (BOAT_VS's BOAT_HALF), its layer's scale
const float BOAT_LEN[6] = {0.5f, 0.9f, 1.1f, 1.9f, 3.3f, 3.7f};
const float LAYER_SCALE[3] = {0.5f, 0.75f, 1.0f};

// The variants (sRGB): the sky (top, low), the sun or moon, the clouds, the storm's sky and clouds; the sea's three
// layers (far, middle, near), its depths, foam, haze and storm water; the well's panel; the boats (hull,
// superstructure, sails, accent, windows, three container colors); the HUD ink, the accent, the dusk of slow motion;
// the seven waters (the sea's life), the seven glasses of the bottles; the sun (x, y, radius), moon 0/1, stars, the panel's opacity, cloudiness, a light panel (dark
// glass edges), night (lit windows).
struct VariantDef {
    const char* name;
    unsigned skyTop, skyLow, sun, cloud, stormSky, stormCloud;
    unsigned sea0, sea1, sea2, deep, foam, haze, stormSea, panel;
    unsigned hull, super_, sail, baccent, window, cont0, cont1, cont2;
    unsigned ink, accent, dusk;
    unsigned piece[7];
    unsigned glass[7];
    float sunX, sunY, sunR, moon, stars, panelA, clouds, lightPanel, night;
};
const VariantDef VARIANTS[] = {
    // A calm noon: a clear blue sky, a bright sea, white sails; cyan, cobalt, indigo, sea green, teal, azure, seafoam.
    {"CALM NOON", 0x6aa6dc, 0xe2eef2, 0xfffbea, 0xffffff, 0x5a6472, 0x3e4652,
     0xa2ccd8, 0x4f9fc0, 0x23709c, 0x0f3d62, 0xf6fbfb, 0xd6e8ee, 0x3a5462, 0xf0f5f6,
     0x22344a, 0xf3f1ea, 0xfbfaf4, 0xd0583c, 0x2c4458, 0xd0583c, 0x3d7fa6, 0xe0b040,
     0x1b3348, 0x1f88b8, 0x2a2f5a,
     {0x14c0cc, 0x1e56b0, 0x7a4cc8, 0x1fa676, 0x0a7a84, 0x6aa8f4, 0x8ad6a8},
     {0x0f8f9a, 0x2556b8, 0x6a46b8, 0x1f8a52, 0xc07a14, 0x3f8fd0, 0xb84a72},
     -14.f, 8.5f, 1.0f, 0.f, 0.f, 0.6f, 0.3f, 1.f, 0.f},
    // A golden hour: a low sun in an apricot haze, a sea of copper and slate, a dusky panel; bright clear waters.
    {"GOLDEN HOUR", 0x34446e, 0xf4b47c, 0xffe4b4, 0xf2a888, 0x3a3248, 0x2a2534,
     0xe0aa88, 0x8a7c94, 0x3a4870, 0x1a2242, 0xffe8d4, 0xf2c6a0, 0x2c3048, 0x1a2140,
     0x1d1f33, 0xf6e6d2, 0xfff1e0, 0xe0603a, 0xffd08a, 0xe0603a, 0x3d6f96, 0xe8b24a,
     0xfbeee0, 0xffb36a, 0x241a3c,
     {0x4ae0ea, 0x3c8af2, 0x8e7cf6, 0x48d69a, 0x20b2b0, 0x90caff, 0xb8f2d0},
     {0x40d0e0, 0x4a88f0, 0xa080f0, 0x40c880, 0xf0a030, 0x90c8ff, 0xf080a8},
     15.5f, -1.0f, 1.6f, 0.f, 0.f, 0.62f, 0.35f, 0.f, 0.35f},
    // A grey North Sea: an overcast sky, grey-green water, red fishing hulls; deep teals and greens in the glass.
    {"GREY NORTH SEA", 0x98a4ac, 0xd6dbd8, 0xe4e6e2, 0xc6cccd, 0x4a5254, 0x343b3e,
     0x9aaaa6, 0x5f7571, 0x34494a, 0x18282c, 0xeef2ee, 0xc8d0ce, 0x26363a, 0xe9ecea,
     0x2a3236, 0xe8e6de, 0xd8d4c8, 0xc8452e, 0x34404a, 0xb8503a, 0x4e6e80, 0xd09a3c,
     0x232c30, 0x3c8a86, 0x2a3040,
     {0x16a2b6, 0x2a5a90, 0x5a54a6, 0x2e9a64, 0x0e6a6c, 0x5c98d0, 0x7cc496},
     {0x168a96, 0x2a5a96, 0x5e4c9e, 0x2a8a52, 0xb07420, 0x4a84bc, 0xa84a62},
     -12.f, 7.f, 0.95f, 0.f, 0.f, 0.62f, 0.75f, 1.f, 0.f},
    // A night harbor: a deep blue night, a moon, boats with lit windows; pale, luminous waters in the glass.
    {"NIGHT HARBOR", 0x070d22, 0x1c2a4c, 0xe8ecf4, 0x2a3658, 0x0a0f1c, 0x1a2236,
     0x26385c, 0x17284c, 0x0d1a38, 0x060c1c, 0xa4b8d4, 0x34466c, 0x0c1426, 0x0c1428,
     0x0a1020, 0x2e3c5a, 0x5c6c8c, 0x6a3a40, 0xffc870, 0x4a3040, 0x2a4058, 0x5a5030,
     0xdce6f4, 0x8ad8ff, 0x101030,
     {0x6ee6f2, 0x5c9cff, 0xac9cff, 0x6eeab0, 0x3ccaca, 0xa4ccff, 0xc4f6da},
     {0x60e0f0, 0x6a9cff, 0xb09cff, 0x60e0a0, 0xffb848, 0xa8ccff, 0xff90b8},
     13.f, 8.f, 0.8f, 1.f, 1.f, 0.55f, 0.25f, 0.f, 1.f},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

using Palette = SeaUniverse::Palette;
static_assert(sizeof(Palette) % sizeof(float) == 0, "a palette is floats only");
constexpr int PAL_FLOATS = (int)(sizeof(Palette) / sizeof(float));

Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    auto L = [](unsigned c) { return ugl::toLinear(hex(c)); };
    Palette p;
    p.skyTop = L(d.skyTop); p.skyLow = L(d.skyLow); p.sun = L(d.sun); p.cloud = L(d.cloud); p.stormSky = L(d.stormSky);
    p.stormCloud = L(d.stormCloud); p.sea0 = L(d.sea0); p.sea1 = L(d.sea1); p.sea2 = L(d.sea2); p.deep = L(d.deep);
    p.foam = L(d.foam); p.haze = L(d.haze); p.stormSea = L(d.stormSea); p.panel = L(d.panel);
    p.hull = L(d.hull); p.super_ = L(d.super_); p.sail = L(d.sail); p.baccent = L(d.baccent); p.window = L(d.window);
    p.cont0 = L(d.cont0); p.cont1 = L(d.cont1); p.cont2 = L(d.cont2);
    p.ink = L(d.ink); p.accent = L(d.accent); p.dusk = L(d.dusk);
    for (int i = 0; i < 7; i++) p.piece[i] = L(d.piece[i]);
    for (int i = 0; i < 7; i++) p.glass[i] = L(d.glass[i]);
    p.sunX = d.sunX; p.sunY = d.sunY; p.sunR = d.sunR; p.moon = d.moon; p.stars = d.stars; p.panelA = d.panelA;
    p.clouds = d.clouds; p.lightPanel = d.lightPanel; p.night = d.night;
    return p;
}

void lerpPalette(Palette& a, const Palette& b, float k) {
    float* pa = reinterpret_cast<float*>(&a);
    const float* pb = reinterpret_cast<const float*>(&b);
    for (int i = 0; i < PAL_FLOATS; i++) pa[i] += (pb[i] - pa[i]) * k;
}

std::string titleCase(const std::string& s) {
    std::string o = s;
    bool start = true;
    for (char& c : o) {
        if (std::isalpha((unsigned char)c)) {
            c = start ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c);
            start = false;
        } else start = c == ' ' || c == '-';
    }
    return o;
}

float crest(float s, float sharp) {
    const float c = s * 0.5f + 0.5f;
    return 2.f * std::pow(std::max(c, 0.f), 1.f + 2.5f * sharp) - 1.f;
}
float flatAt(float x) { return lerpf(1.f, 0.12f, smoothstepf(5.9f, 4.9f, std::fabs(x))); }

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool SeaUniverse::init(int width, int height) {
    progSea_ = compile(ugl::FULLSCREEN_VS, shaders::SEA_FS, "sea sky", true);
    progBoat_ = compile(shaders::BOAT_VS, shaders::BOAT_FS, "sea boat", true);
    progCell_ = compile(shaders::CELL_VS, shaders::CELL_FS, "sea bottle", true);
    progSprite_ = compile(shaders::SPRITE_VS, shaders::SPRITE_FS, "sea sprite");
    progLife_ = compile(shaders::LIFE_VS, shaders::LIFE_FS, "sea life", true);
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "sea text");
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "sea shape");
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "sea post");

    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    auto instanced = [&](GLuint& vao, GLuint& vbo, int vec4s, size_t stride) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        for (int i = 0; i < vec4s; i++) {
            glEnableVertexAttribArray(1 + i);
            glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(sizeof(float) * 4 * i));
            glVertexAttribDivisor(1 + i, 1);
        }
    };
    instanced(cellVao_, cellVbo_, 4, sizeof(CellInst));
    instanced(spriteVao_, spriteVbo_, 4, sizeof(SpriteInst));
    instanced(lifeVao_, lifeVbo_, 4, sizeof(SpriteInst));
    instanced(boatVao_, boatVbo_, 2, sizeof(float) * 8);

    glGenVertexArrays(1, &textVao_);
    glGenBuffers(1, &textVbo_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)(sizeof(float) * 2));

    glGenVertexArrays(1, &shapeVao_);
    glGenBuffers(1, &shapeVbo_);
    glBindVertexArray(shapeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)(sizeof(float) * 2));
    glBindVertexArray(0);

    buildFont(fontItalic_, FONT_ITALIC);
    buildFont(fontRoman_, FONT_ROMAN);
    pal_ = paletteOf(0);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// Glyphs baked as signed distance fields (they scale cleanly).
void SeaUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] sea font init failed\n");
        delete info;
        return;
    }
    f.info = info;
    f.px = 64.f;
    f.scaleTT = stbtt_ScaleForPixelHeight(info, f.px);
    int x0, y0, x1, y1;
    if (stbtt_GetCodepointBox(info, 'H', &x0, &y0, &x1, &y1)) f.capH = y1 * f.scaleTT;
    const int AW = 1024, AH = 1024, PAD = 8;
    std::vector<unsigned char> atlas(AW * AH, 0);
    int x = 1, y = 1, rowH = 0;
    for (int ch = 32; ch < 128; ch++) {
        Glyph& g = f.g[ch - 32];
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, ch, &adv, &lsb);
        g.adv = adv * f.scaleTT;
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* sdf = stbtt_GetCodepointSDF(info, f.scaleTT, ch, PAD, 128, 128.f / PAD, &w, &h, &xo, &yo);
        if (!sdf) { g.w = g.h = 0; continue; }
        if (x + w + 1 > AW) { x = 1; y += rowH + 1; rowH = 0; }
        if (y + h + 1 > AH) { stbtt_FreeSDF(sdf, nullptr); break; }
        for (int r = 0; r < h; r++) std::memcpy(&atlas[(y + r) * AW + x], sdf + r * w, w);
        g.x0 = x / (float)AW; g.y0 = y / (float)AH; g.x1 = (x + w) / (float)AW; g.y1 = (y + h) / (float)AH;
        g.ox = (float)xo; g.oy = (float)yo; g.w = (float)w; g.h = (float)h;
        x += w + 1;
        rowH = std::max(rowH, h);
        stbtt_FreeSDF(sdf, nullptr);
    }
    glGenTextures(1, &f.tex);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, AW, AH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f.ok = true;
}

void SeaUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    const float aspect = (float)width_ / height_;
    const layout::Camera cam = layout::camera(width_, height_);
    const float hh = cam.dist * std::tan(cam.fovDeg * 0.5f * PI / 180.f), hw = hh * aspect;
    viewH_ = v2(hw, hh);
    viewC_ = v2(cam.target.x, cam.target.y);
    vp_ = mat4::ortho(viewC_.x - hw, viewC_.x + hw, viewC_.y - hh, viewC_.y + hh, -1.f, 1.f);
    px_ = 2.f * hh / height_;
    sceneT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    bgT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    initPiles();
}

SeaUniverse::~SeaUniverse() {
    sceneT_.destroy();
    bgT_.destroy();
    for (GLuint p : {progSea_, progBoat_, progCell_, progSprite_, progLife_, progText_, progShape_, progPost_}) glDeleteProgram(p);
    for (GLuint v : {cellVao_, spriteVao_, lifeVao_, boatVao_, textVao_, shapeVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, cellVbo_, spriteVbo_, lifeVbo_, boatVbo_, textVbo_, shapeVbo_}) glDeleteBuffers(1, &b);
    for (Font* f : {&fontItalic_, &fontRoman_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// =====================================================================================================
// Song, level
// =====================================================================================================

// The sea from the song: bright major songs at noon or at the golden hour; minor songs on the grey North Sea or in a
// night harbor; the key's hue picks among them.
int SeaUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    int v;
    if (t.hasSong && !t.minor) v = h < 0.5f ? 0 : 1;
    else if (t.hasSong && t.minor) v = h < 0.5f ? 2 : 3;
    else v = (int)(h * VARIANT_COUNT) % VARIANT_COUNT;
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void SeaUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    rng_ = Rng(look.seed ^ 0x5EAull);
    dir_ = (look.seed >> 7) & 1 ? 1.f : -1.f;
    // a new song: the sea goes back down (at once before the first frame, else it drains over a few seconds)
    waterLines_ = 0.f;
    if (seconds <= 0.f) {
        palSet_ = false;
        rise_ = 0.f;
        boats_.clear();
        critters_.clear();
        bottles_.clear();
        initPiles();
    } else {
        // the last song's bottles sink into the sand
        for (Pile& p : pile_) {
            p.target += 3.f * B_LAYER;
            std::fill(p.h.begin(), p.h.end(), 0.f);
        }
    }
}

void SeaUniverse::setLevel(int level, float energy, float seconds, float impact) {
    (void)impact;
    level = std::clamp(level, 0, 2);
    energy_ = energy;
    if (!hasLevel_ || seconds < 0.1f) {
        hasLevel_ = true;
        level_ = level;
        lv_ = lvFrom_ = lvTo_ = (float)level;
        lvT_ = 1.f;
        return;
    }
    level_ = level;
    lvFrom_ = lv_;
    lvTo_ = (float)level;
    lvT_ = 0.f;
    lvDur_ = std::clamp(seconds, 1.5f, 6.f);
}

// The drop's first downbeat: its hit fires as soon as the section is known to be a drop (a chorus stays a breeze).
void SeaUniverse::drop(float strength) {
    (void)strength;
    dropPending_ = 2.5f;
}

void SeaUniverse::refresh(float strength) { gust_ = std::max(gust_, 0.4f * strength); }

vec2 SeaUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints SeaUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    h.background = lerp(p.skyLow, p.sea1, 0.4f);
    h.light = ugl::luma(h.background) > 0.28f;
    h.ink = p.piece[0];
    h.accent = p.accent;
    h.styles = TS_RIPPLE | TS_LUMA | TS_INK;
    return h;
}

// =====================================================================================================
// The sea
// =====================================================================================================

// the sandy bed (WAVE_GLSL's sandTop)
static float sandTop(float x) {
    return -9.35f + 0.55f * std::sin(x * 0.21f + 1.3f) + 0.3f * std::sin(x * 0.53f + 0.4f) + 0.1f * std::sin(x * 1.37f + 2.f);
}

float SeaUniverse::riseTarget() const { return 1.f - std::exp(-waterLines_ / SEA_LINES); }
float SeaUniverse::seaLevel() const { return lerpf(SEA_LOW, SEA_HIGH, rise_); }
float SeaUniverse::seaSpread() const { return 1.f + 0.3f * rise_; }

// The top of a layer of the sea at x (the same function as WAVE_GLSL's layerTop).
float SeaUniverse::layerTop(int k, float x) const {
    const float A = amp_ * L_AMP[k] * flatAt(x);
    const float p = waveT_ * L_SPD[k];
    const float f = L_FRQ[k];
    const float fk = (float)k;
    const float s1 = crest(std::sin(x * 0.55f * f - p * dir_ + fk * 1.7f), sharp_);
    const float s2 = crest(std::sin(x * 1.13f * f - p * 1.37f * dir_ + fk * 3.1f + 1.3f), sharp_);
    const float s3 = std::sin(x * 2.9f * f - p * 0.6f * dir_ + fk * 0.9f);
    return seaLevel() + L_OFF[k] * seaSpread() + A * (0.55f * s1 + 0.32f * s2 + 0.13f * s3);
}

// The swell's uniforms (WAVE_GLSL).
void SeaUniverse::setWaves(GLuint prog) const {
    set1f(prog, "uLevel", seaLevel());
    set1f(prog, "uSpread", seaSpread());
    set1f(prog, "uAmp", amp_);
    set1f(prog, "uSharp", sharp_);
    set1f(prog, "uDir", dir_);
    set1f(prog, "uWaveT", waveT_);
}

// A boat comes in from one side (or is already out there when the song starts): the deeper the sea, the bigger.
bool SeaUniverse::spawnBoat(bool onScreen) {
    if (boats_.size() >= 8) return false;
    const int maxT = std::clamp((int)(1.f + (seaLevel() - BOAT_LEVEL) / 2.2f), 1, 5);
    const int minT = std::max(0, maxT - 2);
    int type = rng_.chance(0.45f) ? maxT : rng_.irange(minT, maxT);
    int layer;
    if (type >= 3) layer = rng_.chance(0.6f) ? 0 : 1;      // big ships stay out at sea
    else if (type == 2) layer = rng_.irange(0, 2);
    else layer = rng_.chance(0.3f) ? 0 : rng_.irange(1, 2);
    Boat b;
    b.type = type;
    b.layer = layer;
    b.scale = LAYER_SCALE[layer] * rng_.range(0.9f, 1.1f);
    b.dir = rng_.chance(0.5f) ? 1.f : -1.f;
    b.speed = rng_.range(0.16f, 0.36f) * (0.6f + 0.6f * LAYER_SCALE[layer]) * (type >= 3 ? 1.3f : 1.f);
    b.phase = rng_.range(0.f, TAU);
    b.seed = rng_.range(0.f, 50.f);
    const float len = BOAT_LEN[type] * b.scale;
    b.x = onScreen ? rng_.range(-viewH_.x + 1.f, viewH_.x - 1.f) : -b.dir * (viewH_.x + len + 0.5f);
    // not on top of another boat of the same layer
    for (const Boat& o : boats_)
        if (o.layer == layer && std::fabs(o.x - b.x) < len + BOAT_LEN[o.type] * o.scale + 1.f && o.dir == b.dir) return false;
    b.alpha = onScreen ? 1.f : 0.f;
    boats_.push_back(b);
    return true;
}

// =====================================================================================================
// Life in the sea
// =====================================================================================================

float SeaUniverse::waterDepth() const { return seaLevel() - SAND_Y; }
// how dark the water is this far under the surface (WAVE_GLSL's darkAt)
float SeaUniverse::darkAt(float depth) const { return smoothstepf(0.5f, 10.f, depth); }
float SeaUniverse::critterY(const Critter& c) const {
    const float bed = sandTop(c.x) + 0.45f, surf = seaLevel() - 0.45f;
    return lerpf(bed, surf, c.u + c.du) + c.dartY;
}

// Something new comes into the water (or is already there): what the sea's depth asks for and is missing.
void SeaUniverse::spawnLife(bool onScreen) {
    const float D = waterDepth();
    int have[4] = {0, 0, 0, 0};
    for (const Critter& c : critters_)
        if (c.kind == L_FISH) have[0] += c.du == 0.f; // one leader per school
        else have[c.kind]++;
    const int want[4] = {D > 2.f ? std::min(4, 1 + (int)((D - 2.f) / 3.5f)) : 0, D > 6.f ? std::min(5, 1 + (int)((D - 6.f) / 2.2f)) : 0,
                         D > 11.f ? 1 : 0, D > 10.f ? (D > 13.f ? 2 : 1) : 0};
    int kind = -1;
    const int start = rng_.irange(0, 3);
    for (int i = 0; i < 4 && kind < 0; i++) {
        const int k = (start + i) % 4;
        if (have[k] < want[k]) kind = k;
    }
    if (kind < 0) return;
    Critter c{};
    c.kind = kind;
    c.dir = rng_.chance(0.5f) ? 1.f : -1.f;
    c.phase = rng_.range(0.f, TAU);
    c.seed = rng_.range(0.f, 50.f);
    c.alpha = onScreen ? 1.f : 0.f;
    // a depth under the surface, as a place in the water column (so that it rises with the sea)
    auto under = [&](float lo, float hi) { return saturate(1.f - std::clamp(rng_.range(lo, hi), 0.f, D) / std::max(D, 0.5f)); };
    const float margin = 2.5f;
    const float x0 = onScreen ? rng_.range(-viewH_.x + 1.f, viewH_.x - 1.f) : -c.dir * (viewH_.x + margin);
    c.x = x0;
    if (kind == L_FISH) {
        // a small school near the surface or in the middle water, now and then one in the accent color
        const int n = rng_.irange(5, 9);
        c.speed = rng_.range(0.7f, 1.3f);
        c.size = rng_.range(0.24f, 0.32f);
        c.u = under(0.9f, std::min(5.f, D * 0.6f));
        c.school = nextSchool_++;
        const bool accent = rng_.chance(0.25f);
        c.col = accent ? lerp(pal_.baccent, pal_.foam, 0.2f) : lerp(pal_.sea2, pal_.foam, 0.62f);
        for (int i = 0; i < n; i++) {
            Critter f = c;
            f.x = x0 - c.dir * (i == 0 ? 0.f : rng_.range(0.3f, 2.2f));
            f.du = i == 0 ? 0.f : rng_.range(-0.6f, 0.6f) / std::max(D, 1.f);
            if (i > 0 && f.du == 0.f) f.du = 1e-4f;
            f.phase = rng_.range(0.f, TAU);
            f.size = c.size * rng_.range(0.85f, 1.1f);
            critters_.push_back(f);
        }
        return;
    }
    if (kind == L_JELLY) {
        // jellyfish: slowly pulsing bells, translucent, a soft glow of their own
        c.speed = rng_.range(0.05f, 0.14f);
        c.size = rng_.range(0.32f, 0.5f);
        c.u = under(2.5f, std::min(D * 0.75f, 8.f));
        const vec3 hues[3] = {pal_.piece[2], pal_.piece[0], lerp(pal_.baccent, pal_.piece[2], 0.4f)};
        c.col = lerp(hues[rng_.irange(0, 2)], vec3(1.f), 0.45f);
        c.glow = c.col;
    } else if (kind == L_ANGLER) {
        // an anglerfish in the dark: a near-black shape, only its lure lit
        c.speed = rng_.range(0.1f, 0.18f);
        c.size = 0.6f;
        c.u = rng_.range(0.06f, 0.2f);
        c.col = pal_.deep * 0.35f + vec3(0.012f, 0.016f, 0.024f);
        c.glow = vec3(1.f, 0.88f, 0.55f);
    } else {
        // a siphonophore: a long chain of small lit beads drifting slowly
        c.speed = rng_.range(0.04f, 0.09f);
        c.size = rng_.range(1.6f, 2.4f);
        c.u = rng_.range(0.12f, 0.3f);
        c.col = lerp(pal_.piece[0], vec3(1.f), 0.35f);
        c.glow = c.col;
    }
    if (onScreen && kind != L_FISH) c.x = rng_.range(-viewH_.x + 2.f, viewH_.x - 2.f);
    critters_.push_back(c);
}

void SeaUniverse::simulateLife(float dt, float wdt, float world) {
    lifeT_ -= wdt;
    if (lifeT_ <= 0.f && world > 0.5f && !over_) {
        spawnLife(false);
        lifeT_ = rng_.range(1.5f, 4.f);
    }
    glowUp_ = approach(glowUp_, storm_, 1.f, dt);
    const float surf = seaLevel();
    for (Critter& c : critters_) {
        c.dart = approach(c.dart, 0.f, 0.6f, dt);
        c.x += c.dir * c.speed * wdt * (1.f + 2.5f * c.dart);
        c.dartY += c.dyv * wdt;
        c.dyv *= std::exp(-2.5f * wdt);
        c.dartY = approach(c.dartY, 0.f, 0.15f, wdt);
        // out of the water (the sea draining for a new song): it fades away
        const float y = critterY(c);
        const bool wet = y < surf - c.size * 0.6f && y > sandTop(c.x) + 0.1f;
        c.alpha = approach(c.alpha, wet ? 1.f : 0.f, 0.8f, dt);
    }
    critters_.erase(std::remove_if(critters_.begin(), critters_.end(),
                                   [&](const Critter& c) {
                                       const float y = critterY(c);
                                       const bool wet = y < surf - c.size * 0.6f;
                                       return c.dir * c.x > viewH_.x + c.size * 2.f + 3.f || (!wet && c.alpha <= 0.f);
                                   }),
                     critters_.end());
}

// Each critter on a quad of its own (LIFE_FS draws it): a0 x, y, half extents; a1 direction, kind, animation, glow;
// a2 its color, alpha; a3 its glow's color, seed.
void SeaUniverse::drawLife(float world) {
    if (critters_.empty() || world <= 0.01f) return;
    const float surf = seaLevel();
    for (const Critter& c : critters_) {
        const float a = c.alpha * world;
        if (a <= 0.003f) continue;
        const float y = critterY(c);
        const float dark = darkAt(surf - y);
        vec3 body = c.col;
        float hx = c.size, hy = c.size, glow = 0.f, anim = time_ + c.phase;
        switch (c.kind) {
        case L_FISH:
            body = lerp(c.col, pal_.deep * 0.25f, dark * 0.7f);
            hx = c.size * 1.25f; hy = c.size * 0.7f;
            anim = time_ * (6.f + 6.f * c.dart) + c.phase;
            break;
        case L_JELLY:
            hx = c.size * 1.2f; hy = c.size * 2.6f;
            glow = 0.35f + 0.35f * dark + 0.6f * glowUp_;
            anim = worldTime_ * 0.55f + c.phase;
            break;
        case L_ANGLER:
            hx = c.size * 1.6f; hy = c.size * 1.25f;
            glow = 0.8f + 0.4f * glowUp_;
            anim = worldTime_ + c.phase;
            break;
        default:
            hx = c.size + 0.3f; hy = 0.7f;
            glow = 0.55f + 0.5f * glowUp_;
            anim = worldTime_ * 0.35f + c.phase;
            break;
        }
        // jellies rise a little on each pulse, sink back slowly
        const float bob = c.kind == L_JELLY ? 0.25f * std::sin(anim * 0.5f) : c.kind == L_FISH ? 0.04f * std::sin(time_ * 1.3f + c.phase * 3.f) : 0.f;
        lifeInst_.push_back({{c.x, y + bob, hx, hy}, {c.dir, (float)c.kind, anim, glow}, {body.x, body.y, body.z, a}, {c.glow.x, c.glow.y, c.glow.z, c.seed}});
    }
    if (lifeInst_.empty()) return;
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(progLife_);
    setMat(progLife_, "uVP", vp_);
    setWaves(progLife_);
    set1f(progLife_, "uPx", px_);
    set1f(progLife_, "uPanelA", pal_.panelA * (1.f - saturate(bridge_.amount)));
    set3f(progLife_, "uPanel", pal_.panel);
    set3f(progLife_, "uDusk", pal_.dusk);
    set1f(progLife_, "uSlow", slow_);
    glBindVertexArray(lifeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lifeVbo_);
    glBufferData(GL_ARRAY_BUFFER, lifeInst_.size() * sizeof(SpriteInst), lifeInst_.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)lifeInst_.size());
    lifeInst_.clear();
}

// =====================================================================================================
// Game events
// =====================================================================================================

// A cube breaks: its glass shatters into sharp shards and its water bursts out of it, up and outward, then falls
// (behind the stack, inside the well) to the sea or the sand; the sea rises as that water reaches it (`water`:
// the lines of sea the cube holds).
void SeaUniverse::breakCell(float x, float y, int type, float power, float water, vec2 carry) {
    const vec3 tint = pal_.piece[std::clamp(type, 0, 6)];
    const float side = x < 0.f ? -1.f : 1.f;
    for (int i = 0; i < 9; i++) {
        Particle q{};
        q.p = v2(x + rng_.range(-0.45f, 0.45f), y + rng_.range(-0.45f, 0.45f));
        q.v = v2((q.p.x - x) * rng_.range(4.f, 9.f) * power + side * rng_.range(0.f, 1.5f) + carry.x * 0.5f,
                 rng_.range(1.5f, 6.f) * power + carry.y * 0.4f);
        q.life = rng_.range(0.5f, 0.8f);
        q.size = rng_.range(0.08f, 0.17f);
        q.rot = rng_.range(0.f, TAU); q.spin = rng_.range(-12.f, 12.f);
        q.grav = 20.f; q.drag = 0.4f;
        q.kind = P_SHARD; q.real = true; q.front = true; q.seed = rng_.uniform();
        q.col = pal_.lightPanel > 0.5f ? lerp(tint, vec3(1.f), 0.35f) : lerp(tint, vec3(1.f), 0.6f);
        parts_.push_back(q);
    }
    // a cube thrown in the air lets its water go as one mass that opens up as it falls (it carries the cube's
    // momentum); a cube breaking in place bursts out in every direction
    const bool thrown = carry.x != 0.f || carry.y != 0.f;
    if (y < layerTop(2, x) - 0.2f) {
        // under the sea already: its water clouds out into the sea, and air bubbles up
        for (int i = 0; i < 7; i++) {
            Particle q{};
            q.p = v2(x + rng_.range(-0.3f, 0.3f), y + rng_.range(-0.3f, 0.3f));
            const float a = rng_.range(0.f, TAU);
            q.v = v2(carry.x * 0.25f + std::cos(a) * rng_.range(0.3f, 1.2f), carry.y * 0.15f + std::sin(a) * rng_.range(0.3f, 1.2f));
            q.life = rng_.range(1.6f, 2.6f);
            q.size = rng_.range(0.2f, 0.34f);
            q.grav = -0.3f; q.drag = 2.2f;
            q.kind = P_CLOUD; q.real = true; q.front = true; q.seed = rng_.uniform();
            q.col = lerp(WATER, vec3(1.f), 0.15f);
            parts_.push_back(q);
        }
        for (int i = 0; i < 9; i++) {
            Particle q{};
            q.p = v2(x + rng_.range(-0.35f, 0.35f), y + rng_.range(-0.35f, 0.35f));
            q.v = v2(carry.x * 0.15f + rng_.range(-0.6f, 0.6f), rng_.range(0.5f, 1.8f));
            q.life = rng_.range(2.5f, 4.f);
            q.size = rng_.range(0.035f, 0.085f);
            q.grav = -3.5f; q.drag = 1.6f;
            q.kind = P_BUBBLE; q.real = true; q.front = true; q.seed = rng_.uniform();
            q.col = lerp(tint, vec3(1.f), 0.7f);
            parts_.push_back(q);
        }
        waterLines_ += water;
        return;
    }
    const int N = thrown ? 15 : 20;
    for (int i = 0; i < N && parts_.size() < 4000; i++) {
        Particle q{};
        const vec2 o = v2(rng_.range(-0.38f, 0.38f), rng_.range(-0.38f, 0.38f));
        q.p = v2(x + o.x, y + o.y);
        const float a = rng_.range(0.f, TAU);
        if (thrown) {
            const float sp = rng_.range(0.6f, 2.4f) * power;
            q.v = v2(carry.x * 0.55f + std::cos(a) * sp + o.x * 2.5f, carry.y * 0.35f + std::sin(a) * sp * 0.7f + o.y * 2.5f + 1.f);
        } else {
            const float sp = rng_.range(1.5f, 5.f) * power;
            q.v = v2(std::cos(a) * sp + o.x * 4.f + side * rng_.range(0.f, 2.5f), std::sin(a) * sp * 0.6f + rng_.range(1.f, 4.f) * power);
        }
        q.life = 4.f;
        q.size = thrown ? rng_.range(0.09f, 0.19f) : rng_.range(0.07f, 0.15f);
        q.grav = 16.f; q.drag = 0.35f;
        q.kind = P_POUR; q.real = true; q.front = true; q.seed = rng_.uniform();
        q.col = lerp(WATER, vec3(1.f), 0.2f * rng_.uniform());
        q.water = water / N;
        q.y0 = y;
        parts_.push_back(q);
    }
}

void SeaUniverse::splash(float x, float y, float power, const vec3& col) {
    if (parts_.size() > 5500) return;
    const int n = 2 + (int)(power * 4.f);
    for (int i = 0; i < n; i++) {
        Particle q{};
        q.p = v2(x + rng_.range(-0.1f, 0.1f), y);
        q.v = v2(rng_.range(-1.2f, 1.2f), rng_.range(1.5f, 4.f) * power);
        q.life = rng_.range(0.35f, 0.6f);
        q.size = rng_.range(0.025f, 0.045f);
        q.grav = 14.f; q.drag = 0.5f;
        q.kind = P_SPLASH; q.real = true; q.front = false; q.seed = rng_.uniform();
        q.col = lerp(col, pal_.foam, 0.6f);
        parts_.push_back(q);
    }
}

// Spray torn off a crest by the wind (outside the well).
void SeaUniverse::sprayBurst(float x, int n, float power) {
    for (int i = 0; i < n && parts_.size() < 3000; i++) {
        Particle q{};
        const float px = x + rng_.range(-0.6f, 0.6f);
        q.p = v2(px, layerTop(2, px) + rng_.range(-0.05f, 0.1f));
        q.v = v2(dir_ * rng_.range(2.f, 6.f) * power, rng_.range(1.f, 3.5f) * power);
        q.life = rng_.range(0.8f, 1.4f);
        q.size = rng_.range(0.025f, 0.055f);
        q.grav = 5.f; q.drag = 0.7f;
        q.kind = P_SPRAY; q.real = false; q.front = false; q.seed = rng_.uniform();
        q.col = pal_.foam;
        parts_.push_back(q);
    }
}

void SeaUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::Clear: {
        // The row's bottles are tossed out of the well to their side (the row itself is gone from the board at once):
        // each flies to its own place on the sand, lands, tumbles and settles, its stopper pops and its water pours out.
        const int n = std::min(ev.count, 4);
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            const float dx = c.x - (Game::W - 1) * 0.5f;
            Bottle b{};
            b.p = v2(p.x, p.y);
            b.side = dx < 0.f ? -1 : 1;
            b.type = std::clamp((int)c.type, 0, 6);
            b.seed = rng_.range(0.f, 50.f);
            b.fill = B_FULL;
            // each bottle's water: a tenth of a line of sea (more for a Tetris)
            b.water = (n >= 4 ? 1.5f : 1.f) / Game::W;
            placeBottle(b);
            const float side = (float)b.side;
            // a ballistic toss that lands just short of its place (it slides into it as it settles)
            b.T = 0.7f + 0.035f * std::fabs(b.rest.x - b.p.x) + rng_.range(0.f, 0.12f);
            const vec2 aim = v2(b.rest.x - side * 0.2f, b.rest.y + 0.08f);
            b.v = v2((aim.x - b.p.x) / b.T, (aim.y - b.p.y) / b.T + 0.5f * B_GRAV * b.T);
            // it turns as it flies, to come down about on its side (a full turn more now and then)
            b.landRot = b.restRot - side * rng_.range(0.25f, 0.5f);
            const float turns = rng_.chance(0.4f) ? 1.f : 0.f;
            b.rot = 0.f;
            b.spin = (b.landRot - side * TAU * turns) / b.T;
            b.state = BS_FLY;
            b.capState = CS_ON;
            bottles_.push_back(b);
        }
        if (ev.count >= 4) {
            // a Tetris: the sea answers with spray up from both sides
            gust_ = std::max(gust_, 0.8f);
            for (int i = 0; i < 4; i++) {
                const float side = i % 2 ? 1.f : -1.f;
                const float x = side * rng_.range(7.f, viewH_.x - 2.f);
                for (int k = 0; k < 26; k++) {
                    Particle q{};
                    q.p = v2(x + rng_.range(-0.5f, 0.5f), layerTop(2, x));
                    q.v = v2(rng_.range(-1.f, 1.f), rng_.range(4.f, 9.f));
                    q.age = -rng_.range(0.f, 0.3f) - i * 0.12f;
                    q.life = rng_.range(1.f, 1.5f);
                    q.size = rng_.range(0.03f, 0.07f);
                    q.grav = 9.f; q.drag = 0.4f;
                    q.kind = P_SPLASH; q.real = true; q.front = false; q.seed = rng_.uniform();
                    q.col = pal_.foam;
                    parts_.push_back(q);
                }
            }
        }
        break;
    }
    case GameEvent::TopOut: {
        // game over: the stack breaks, row by row from the top
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (auto& c : ev.cells) {
            if (c.y < Game::HIDDEN || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.type = std::clamp((int)c.type, 0, 6);
            d.delay = 0.15f + (c.y - top) * 0.07f + std::fabs(c.x - 4.5f) * 0.015f;
            d.t = 0.f;
            d.broken = false;
            dying_.push_back(d);
        }
        over_ = true;
        break;
    }
    default: break;
    }
}

// The places for the bottles on the sand: columns from the well's side out to the edge of the screen (on the left,
// short of the corner's text).
void SeaUniverse::initPiles() {
    for (int k = 0; k < 2; k++) {
        Pile& p = pile_[k];
        p.x0 = WELL_X + 0.85f;
        p.x1 = k ? viewH_.x - 0.7f : std::min(16.2f, viewH_.x - 0.7f);
        const int n = std::max(1, (int)((p.x1 - p.x0) / B_COL));
        p.h.assign(n, 0.f);
    }
}

// Where a bottle comes to rest: on the lowest column (a little at random), lying on its side, its neck outward.
void SeaUniverse::placeBottle(Bottle& b) {
    Pile& P = pile_[b.side > 0];
    if (P.h.empty()) initPiles();
    int best = 0;
    float bv = 1e9f;
    for (int c = 0; c < (int)P.h.size(); c++) {
        const float v = P.h[c] + rng_.range(0.f, 0.55f);
        if (v < bv) { bv = v; best = c; }
    }
    const float side = (float)b.side;
    const float x = side * (P.x0 + (best + 0.5f) * B_COL + rng_.range(-0.1f, 0.1f));
    const float h = P.h[best];
    // on the sand it follows the slope; on other bottles it lies about flat
    const float slope = h < 0.1f ? std::atan((sandTop(x + 0.4f) - sandTop(x - 0.4f)) / 0.8f) : 0.f;
    b.rest = v2(x, sandTop(x) + h + B_LIFT - (h < 0.1f ? 0.03f : 0.06f));
    b.restRot = -side * PI * 0.5f + slope + rng_.range(-0.14f, 0.14f);
    b.bury0 = P.target;
    P.h[best] += B_LAYER * rng_.range(0.92f, 1.f);
    // every column two deep: the pile settles into the sand by a layer
    if (*std::min_element(P.h.begin(), P.h.end()) >= 2.f * B_LAYER - 0.1f) {
        P.target += B_LAYER;
        for (float& v : P.h) v = std::max(0.f, v - B_LAYER);
    }
}

// A filled board for a review: the empty bottles of the lines cleared so far lie on the sand, their stoppers by them.
void SeaUniverse::restBottles(int n) {
    for (int i = 0; i < n; i++) {
        Bottle b{};
        b.side = i % 2 ? 1 : -1;
        b.type = rng_.irange(0, 6);
        b.seed = rng_.range(0.f, 50.f);
        placeBottle(b);
        b.p = b.rest;
        b.rot = b.restRot;
        b.state = BS_REST;
        b.landT = 10.f;
        b.fill = 0.f;
        b.under = b.rest.y < layerTop(2, b.rest.x);
        b.wetDone = true;
        if (b.under || rng_.chance(0.3f)) b.capState = CS_GONE;
        else {
            b.capState = CS_REST;
            const float cx = b.rest.x + (float)b.side * rng_.range(0.6f, 1.6f);
            b.cp = v2(cx, sandTop(cx) + 0.09f);
            b.crot = (rng_.chance(0.5f) ? 1.f : -1.f) * PI * 0.5f + rng_.range(-0.2f, 0.2f);
            b.cbury0 = pile_[b.side > 0].target;
        }
        bottles_.push_back(b);
    }
    for (Pile& p : pile_) p.bury = p.target;
}

// The bottles thrown out of a cleared row (at the real speed): flight, landing, the stopper popping, the pour.
void SeaUniverse::simulateBottles(float dt) {
    // the sand slowly swallows the empty bottles (all of a pile together, so none is left hanging)
    for (int k = 0; k < 2; k++) {
        bool any = false;
        for (const Bottle& b : bottles_) any = any || ((b.side > 0) == (k == 1) && b.state == BS_REST);
        if (!any) continue;
        Pile& P = pile_[k];
        const float d = dt * B_LAYER / B_SINK_T;
        P.target += d;
        for (float& v : P.h) v = std::max(0.f, v - d);
    }
    for (Pile& p : pile_) p.bury = approach(p.bury, p.target, 0.22f, dt);
    for (Bottle& b : bottles_) {
        const float side = (float)b.side;
        const vec2 rest = v2(b.rest.x, b.rest.y - buryOf(b.side, b.bury0));
        switch (b.state) {
        case BS_FLY: {
            b.t += dt;
            b.v.y -= B_GRAV * dt;
            b.p.x += b.v.x * dt; b.p.y += b.v.y * dt;
            b.rot += b.spin * dt;
            if (std::fabs(b.p.x) > WELL_X + 0.3f && b.v.y < 0.f && b.p.y < layerTop(2, b.p.x) - 0.15f) {
                // into the sea: a splash, then it sinks to its place
                splash(b.p.x, layerTop(2, b.p.x), 1.2f, WATER);
                splash(b.p.x, layerTop(2, b.p.x), 1.f, WATER);
                b.state = BS_SINK;
                b.under = true;
                b.landT = 0.f;
            } else if (b.t >= b.T) {
                b.state = BS_SETTLE;
                b.land = b.p;
                b.rot = b.landRot;
                b.landT = 0.f;
                // a puff of sand where it lands
                for (int i = 0; i < 7 && parts_.size() < 4000; i++) {
                    Particle q{};
                    q.p = v2(b.p.x + rng_.range(-0.3f, 0.3f), b.p.y - 0.3f);
                    q.v = v2(rng_.range(-1.5f, 1.5f) + side * 0.6f, rng_.range(1.f, 2.8f));
                    q.life = rng_.range(0.35f, 0.6f);
                    q.size = rng_.range(0.03f, 0.055f);
                    q.grav = 12.f; q.drag = 1.f;
                    q.kind = P_SAND; q.real = true; q.front = true; q.seed = rng_.uniform();
                    q.col = lerp(vec3(0.62f, 0.47f, 0.29f), pal_.haze, 0.15f) * (1.f - 0.55f * pal_.night);
                    parts_.push_back(q);
                }
            }
            break;
        }
        case BS_SETTLE: {
            // a small bounce, a slide into its place, the last of the turn rocking out
            b.landT += dt;
            const float u = std::min(1.f, b.landT / 0.5f);
            const float e = 1.f - (1.f - u) * (1.f - u);
            b.p = v2(lerpf(b.land.x, rest.x, e), lerpf(b.land.y, rest.y, e) + 0.2f * std::sin(PI * std::min(u * 1.7f, 1.f)) * (1.f - u));
            b.rot = b.restRot + (b.landRot - b.restRot) * std::cos(u * PI * 2.f) * (1.f - u) * (1.f - u);
            if (u >= 1.f) b.state = BS_REST;
            break;
        }
        case BS_SINK: {
            // through the water, slowly, to its place (its turn unwinding)
            b.landT += dt;
            const float k = 1.f - std::exp(-2.2f * dt);
            b.p.x += (rest.x - b.p.x) * k;
            b.p.y += (rest.y - b.p.y) * k;
            b.rot += (b.restRot - b.rot) * k;
            if (std::fabs(rest.y - b.p.y) < 0.03f) b.state = BS_REST;
            break;
        }
        default: {
            b.landT += dt;
            // the jet's kick: pushed back from its neck, rocking, settling as the pressure drops
            const float since = b.capState == CS_ON ? 0.f : b.landT - b.popT;
            const float kick = b.capState == CS_ON ? 0.f : 0.22f * smoothstepf(0.f, 0.06f, since) * std::exp(-since * 2.2f);
            const vec2 upR = v2(-std::sin(b.restRot), std::cos(b.restRot));
            b.p = v2(rest.x - upR.x * kick, rest.y - std::max(0.f, upR.y) * kick * 0.5f);
            b.rot = b.restRot + (float)b.side * 0.1f * std::sin(since * 20.f) * std::exp(-since * 3.f) * (b.capState == CS_ON ? 0.f : 1.f);
            break;
        }
        }
        if (b.state == BS_FLY) continue;
        const float c = std::cos(b.rot), s = std::sin(b.rot);
        auto at = [&](float ly) { return v2(b.p.x - s * ly, b.p.y + c * ly); }; // a point on the bottle's axis
        const vec2 up = v2(-s, c);
        // the stopper shoots off like a champagne cork, spinning, high in an arc
        if (b.capState == CS_ON && b.landT >= (b.under ? 0.6f : 0.32f)) {
            b.capState = CS_FLY;
            b.popT = b.landT;
            b.cp = at(CAP_Y);
            b.crot = b.rot;
            b.cv = v2(up.x * 7.f + side * rng_.range(0.5f, 1.5f), up.y * 7.f + rng_.range(5.5f, 8.f));
            b.cspin = side * rng_.range(-26.f, -14.f);
            b.pourAcc = 0.f;
        }
        // the water erupts out of the neck under pressure: a blast, then a thick jet tapering off over a second or two
        if (b.capState != CS_ON && b.fill > 0.f) {
            const float before = b.fill;
            b.fill = std::max(0.f, b.fill - dt * (0.1f + 0.5f * b.fill));
            if (b.fill < 0.02f) b.fill = 0.f;
            const float out = b.water * (before - b.fill) / B_FULL;
            const vec2 m = at(MOUTH_Y);
            if (b.under || m.y < layerTop(2, m.x)) {
                // under the sea: the jet bursts out as a pale churning plume, a stream of bubbles rising from it
                waterLines_ += out;
                const float pr = std::pow(before / B_FULL, 1.3f);
                const float blast = std::exp(-(b.landT - b.popT) * 9.f);
                b.pourAcc += dt * (18.f + 40.f * pr + 60.f * blast);
                while (b.pourAcc >= 1.f && parts_.size() < 6000) {
                    b.pourAcc -= 1.f;
                    Particle q{};
                    const bool plume = rng_.chance(0.55f);
                    const float sp = plume ? (1.5f + 5.f * pr + 3.f * blast) * rng_.range(0.7f, 1.1f) : rng_.range(0.5f, 1.5f);
                    q.p = v2(m.x + rng_.range(-0.05f, 0.05f), m.y);
                    q.v = v2(up.x * sp + rng_.range(-0.4f, 0.4f), up.y * sp + rng_.range(0.2f, 1.f));
                    if (plume) {
                        q.life = rng_.range(0.9f, 1.6f);
                        q.size = rng_.range(0.12f, 0.22f) * (0.7f + 0.6f * pr);
                        q.grav = -0.6f; q.drag = 2.4f;
                        q.kind = P_CLOUD;
                        q.col = lerp(WATER, vec3(1.f), 0.55f);
                    } else {
                        q.life = rng_.range(2.f, 3.5f);
                        q.size = rng_.range(0.03f, 0.075f);
                        q.grav = -3.5f; q.drag = 1.6f;
                        q.kind = P_BUBBLE;
                        q.col = vec3(0.85f, 0.93f, 1.f);
                    }
                    q.real = true; q.front = true; q.seed = rng_.uniform();
                    parts_.push_back(q);
                }
            } else {
                b.pend += out;
                // the pressure: highest at the pop, falling as it empties
                const float pr = std::pow(before / B_FULL, 1.3f);
                const float since = b.landT - b.popT;
                const float blast = std::exp(-since * 9.f);
                b.pourAcc += dt * (70.f + 230.f * pr + 300.f * blast);
                const int k = (int)b.pourAcc;
                b.pourAcc -= (float)k;
                // the jet's direction: out of the neck, a little upward
                vec2 jd = v2(up.x, up.y + 0.75f);
                const float jl = std::sqrt(jd.x * jd.x + jd.y * jd.y);
                jd = v2(jd.x / jl, jd.y / jl);
                for (int i = 0; i < k && parts_.size() < 6000; i++) {
                    Particle q{};
                    // most of it the jet's core; some spray around it, wider and finer
                    const bool spray = rng_.chance(0.18f + 0.3f * blast);
                    const float spread = spray ? rng_.range(-0.45f, 0.45f) * (1.f + blast) : rng_.range(-0.022f, 0.022f);
                    const float cs = std::cos(spread), sn = std::sin(spread);
                    const vec2 d = v2(jd.x * cs - jd.y * sn, jd.x * sn + jd.y * cs);
                    const float sp = (2.2f + 8.5f * pr + 4.f * blast) * (spray ? rng_.range(0.55f, 1.05f) : rng_.range(0.98f, 1.02f));
                    q.p = v2(m.x + d.x * 0.03f, m.y + d.y * 0.03f);
                    q.v = v2(d.x * sp, d.y * sp);
                    q.age = -(float)i / (float)k * dt;
                    q.life = 3.f;
                    q.size = spray ? rng_.range(0.035f, 0.06f) : rng_.range(0.085f, 0.12f) * (0.65f + 0.55f * pr + 0.3f * blast);
                    q.grav = 15.f; q.drag = spray ? 0.6f : 0.15f;
                    q.kind = P_STREAM; q.real = true; q.front = true; q.seed = rng_.uniform();
                    q.col = lerp(WATER, vec3(1.f), (spray ? 0.4f : 0.1f) + 0.2f * rng_.uniform());
                    q.water = b.pend / (float)k;
                    q.dir = side;
                    parts_.push_back(q);
                }
                if (k > 0) b.pend = 0.f;
                if (!b.wetDone && since > 0.4f) {
                    // a dark wet patch on the sand where it pours
                    b.wetDone = true;
                    Particle q{};
                    const float wx = m.x + side * 2.2f;
                    q.p = v2(wx, sandTop(wx) - 0.07f);
                    q.life = 7.f;
                    q.size = 1.6f;
                    q.rot = std::atan((sandTop(wx + 0.5f) - sandTop(wx - 0.5f)) / 1.f);
                    q.kind = P_WET; q.real = true; q.front = false; q.seed = rng_.uniform();
                    q.col = lerp(vec3(0.62f, 0.47f, 0.29f), pal_.haze, 0.15f) * (1.f - 0.62f * pal_.night) * 0.55f;
                    parts_.push_back(q);
                }
            }
            if (b.fill <= 0.f) { waterLines_ += b.pend; b.pend = 0.f; }
        }
    }
    // the stoppers: they fly, land on the sand and lie there (under the sea they float up and drift away)
    for (Bottle& b : bottles_) {
        if (b.capState == CS_FLY) {
            b.cv.y -= B_GRAV * dt;
            b.cp.x += b.cv.x * dt; b.cp.y += b.cv.y * dt;
            b.crot += b.cspin * dt;
            const float sea = layerTop(2, b.cp.x), sand = sandTop(b.cp.x);
            if (b.cp.y < sea - 0.05f && sea > sand) b.capState = CS_RISE;
            else if (b.cv.y < 0.f && b.cp.y < sand + 0.1f) {
                b.capState = CS_REST;
                b.cp.y = sand + 0.09f;
                b.crot = (rng_.chance(0.5f) ? 1.f : -1.f) * PI * 0.5f + rng_.range(-0.15f, 0.15f);
                b.cbury0 = pile_[b.side > 0].bury;
            }
        } else if (b.capState == CS_RISE) {
            b.cv.x *= std::exp(-3.f * dt);
            b.cv.y = approach(b.cv.y, 1.2f, 4.f, dt);
            b.cp.x += b.cv.x * dt; b.cp.y += b.cv.y * dt;
            b.crot += b.cspin * dt * 0.2f;
            b.cspin *= std::exp(-2.f * dt);
            if (b.cp.y > layerTop(2, b.cp.x) - 0.05f) b.capState = CS_FLOAT;
        } else if (b.capState == CS_FLOAT) {
            b.cp.x += (b.cp.x < 0.f ? -1.f : 1.f) * (0.5f + 0.6f * storm_) * dt; // out to sea, away from the well
            b.cp.y = layerTop(2, b.cp.x) - 0.02f;
            b.crot = approach(b.crot, PI * 0.5f, 1.f, dt);
            if (std::fabs(b.cp.x) > viewH_.x + 1.f || b.cp.y < sandTop(b.cp.x)) b.capState = CS_GONE;
        }
    }
    // the buried ones go; at most so many (the oldest first)
    bottles_.erase(std::remove_if(bottles_.begin(), bottles_.end(),
                                  [&](const Bottle& b) {
                                      return b.state == BS_REST && b.capState != CS_FLY && b.capState != CS_RISE &&
                                             b.rest.y - buryOf(b.side, b.bury0) + 0.42f < sandTop(b.rest.x) - 0.02f;
                                  }),
                   bottles_.end());
    if ((int)bottles_.size() > B_MAX) bottles_.erase(bottles_.begin(), bottles_.begin() + ((int)bottles_.size() - B_MAX));
}

// A whole stack appearing at once (a board filled for a review, a game joined in progress): its sea has risen
// already, deep, with its boats and its life.
void SeaUniverse::watchBoard(const Game& g) {
    int n = 0;
    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) n += g.cell(x, y).type >= 0;
    if (n - boardCount_ >= 12 && waterLines_ < 24.f) {
        waterLines_ = 24.f;
        rise_ = riseTarget();
        boats_.clear();
        for (int i = 0; i < 4; i++) spawnBoat(true);
        critters_.clear();
        for (int i = 0; i < 16; i++) spawnLife(true);
        bottles_.clear();
        initPiles();
        restBottles(44);
    }
    boardCount_ = n;
}

// =====================================================================================================
// Simulation
// =====================================================================================================

void SeaUniverse::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    worldTime_ += wdt;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float world = 1.f - smoothstepf(0.f, 0.6f, bridge_.amount);

    // Is this peak a drop? The HUD's section says it when there is one; else the song's drive.
    const float dropish = sectionKnown_ ? (sectionDrop_ ? 1.f : 0.f) : smoothstepf(1.27f, 1.42f, music.speed);
    const bool stormOn = level_ == 2 && dropish > 0.5f && !over_;
    // the storm comes in over about a second, and calms over a few
    stormLin_ = stormOn ? std::min(1.f, stormLin_ + dt / 1.0f) : std::max(0.f, stormLin_ - dt / 4.f);
    storm_ = smoothstepf(0.f, 1.f, stormLin_);
    if (dropPending_ > 0.f) {
        dropPending_ -= dt;
        if (stormOn) {
            // the hit: lightning in the clouds far out, a gust tearing spray off the crests
            dropPending_ = 0.f;
            flash_ = 1.f;
            flashX_ = (rng_.chance(0.5f) ? -1.f : 1.f) * rng_.range(10.f, viewH_.x - 3.f);
            gust_ = 1.f;
            for (int i = 0; i < 4; i++) sprayBurst((i % 2 ? 1.f : -1.f) * rng_.range(7.f, viewH_.x - 1.f), 18, 1.3f);
            // the life under it startles: the fish dart away
            for (Critter& c : critters_)
                if (c.kind == L_FISH) {
                    c.dart = 1.f;
                    c.dyv = rng_.range(-1.6f, 1.6f);
                }
        }
    }
    gust_ = approach(gust_, 0.f, 0.5f, dt);
    flash_ = approach(flash_, 0.f, 4.f, dt);
    const float chorus = wp * (1.f - storm_);
    const float calmSlow = 1.f - 0.75f * slow_;
    amp_ = lerpf(0.045f + 0.05f * wm + 0.09f * wp, 1.15f, storm_) * calmSlow * (1.f - 0.6f * smoothstepf(0.f, 0.6f, bridge_.amount));
    sharp_ = 0.15f + 0.2f * wp + 0.65f * storm_;
    caps_ = std::min(1.f, 0.4f * chorus + storm_ + 0.3f * gust_ * wp) * calmSlow;
    glint_ = (0.45f + 0.3f * wm + 0.25f * wp) * (1.f - storm_) * (1.f - 0.7f * slow_);
    rock_ = (0.025f + 0.02f * wm + 0.035f * wp + 0.16f * storm_) * calmSlow;
    waveT_ += wdt * (0.3f + 0.08f * wm + 0.14f * wp + 0.7f * storm_ + 0.25f * gust_);
    cloudT_ += wdt * (0.008f + 0.05f * storm_);

    // the sea rises with the water poured in (or drains back for a new song)
    rise_ = approach(rise_, riseTarget(), 2.f, dt);

    // Spray off the crests through the storm, bursts on its kicks; a lightning glow every four bars.
    if (storm_ > 0.05f && world > 0.5f && !over_) {
        sprayAcc_ += 26.f * storm_ * wdt;
        int guard = 0;
        while (sprayAcc_ >= 1.f && guard++ < 20) {
            sprayAcc_ -= 1.f;
            const float side = rng_.chance(0.5f) ? -1.f : 1.f;
            sprayBurst(side * rng_.range(6.f, viewH_.x), 1, 0.7f + 0.6f * storm_);
        }
    } else sprayAcc_ = 0.f;
    sinceKick_ += dt;
    const float kick = std::max(music.kick, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    if (kick > 0.5f && prevKick_ <= 0.5f && sinceKick_ > 0.4f && world > 0.5f && !over_ && wp > 0.5f) {
        sinceKick_ = 0.f;
        if (storm_ > 0.5f) {
            for (int i = 0; i < 2; i++) sprayBurst((rng_.chance(0.5f) ? -1.f : 1.f) * rng_.range(7.f, viewH_.x - 1.f), 12, 1.2f);
        } else {
            sprayBurst((rng_.chance(0.5f) ? -1.f : 1.f) * rng_.range(7.f, viewH_.x - 1.f), 5, 0.6f);
        }
    }
    prevKick_ = kick;
    if (music.hasSong) {
        const double bar = std::floor(music.beat / 4.0 + 1e-4);
        if (lastBar_ < 0.0 || bar < lastBar_ - 4.0) lastBar_ = bar;
        if (bar > lastBar_) {
            lastBar_ = bar;
            if (storm_ > 0.6f && ((long long)bar % 4) == 0 && world > 0.5f) {
                flash_ = std::max(flash_, 0.65f);
                flashX_ = (rng_.chance(0.5f) ? -1.f : 1.f) * rng_.range(10.f, viewH_.x - 3.f);
            }
        }
    }

    // Boats: a few out at sea, more in a chorus, sailing slowly; they keep coming as long as the world is there.
    {
        const bool afloat = seaLevel() >= BOAT_LEVEL;
        const int want = !afloat ? 0 : (int)(3.f + 1.f * wp + 2.f * rise_);
        boatT_ -= wdt;
        if ((int)boats_.size() < want && boatT_ <= 0.f && world > 0.5f) {
            if (spawnBoat(false)) boatT_ = rng_.range(4.f, 10.f);
            else boatT_ = 1.f;
        }
        for (Boat& b : boats_) {
            b.x += b.dir * b.speed * wdt * (1.f + 0.4f * storm_);
            // the sea draining (a new song): they fade before they would sit on the sand
            b.alpha = approach(b.alpha, !afloat ? 0.f : 1.f, 1.5f, dt);
        }
        boats_.erase(std::remove_if(boats_.begin(), boats_.end(),
                                    [&](const Boat& b) {
                                        return b.dir * b.x > viewH_.x + BOAT_LEN[b.type] * b.scale + 1.f || (!afloat && b.alpha <= 0.f);
                                    }),
                     boats_.end());
    }

    // The bottles thrown out of a cleared row (at the real speed).
    simulateBottles(dt);

    simulateLife(dt, wdt, world);

    // Game over: the cubes break one by one.
    for (Dying& d : dying_) {
        d.t += dt;
        if (!d.broken && d.t >= d.delay) {
            d.broken = true;
            breakCell(d.x, d.y, d.type, 0.7f, 0.5f / Game::W);
        }
    }
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return d.broken; }), dying_.end());

    // The particles (a clear's shards and water at the real speed).
    struct Hit { float x, y; vec3 col; float power; };
    std::vector<Hit> hits;
    for (Particle& p : parts_) {
        const float pdt = p.real ? dt : wdt;
        p.age += pdt;
        if (p.age < 0.f) continue;
        if (p.kind == P_SPRAY) p.v.x += dir_ * 2.f * (storm_ + gust_) * pdt;
        // bubbles wobble up and pop at the surface
        if (p.kind == P_BUBBLE) {
            p.v.x += std::sin(time_ * 5.f + p.seed * 40.f) * 1.5f * pdt;
            if (p.p.y > layerTop(2, p.p.x) - p.size) p.age = p.life + 1.f;
        }
        p.v.y -= p.grav * pdt;
        const float k = std::exp(-p.drag * pdt);
        p.v.x *= k; p.v.y *= k;
        p.p.x += p.v.x * pdt; p.p.y += p.v.y * pdt;
        p.rot += p.spin * pdt;
        // a bottle's stream: on the sand it runs on outward and soaks in; into the sea it splashes
        if (p.kind == P_STREAM && p.v.y < 0.f) {
            const float sea = layerTop(2, p.p.x), sand = sandTop(p.p.x);
            if (p.p.y < sea) {
                if (rng_.chance(0.35f)) hits.push_back({p.p.x, sea, p.col, 0.8f});
                waterLines_ += p.water;
                p.water = 0.f;
                p.age = p.life + 1.f;
            } else if (p.p.y < sand + 0.02f) {
                p.kind = P_RUN;
                p.age = 0.f;
                p.life = rng_.range(0.35f, 0.7f);
                p.p.y = sand + 0.02f;
                p.v = v2(p.dir * rng_.range(1.2f, 2.8f) + p.v.x * 0.3f, 0.f);
                p.grav = 0.f; p.drag = 2.f;
                if (rng_.chance(0.3f)) hits.push_back({p.p.x, sand, p.col, 0.55f});
            }
            continue;
        }
        if (p.kind == P_RUN) {
            p.p.y = sandTop(p.p.x) + 0.02f;
            if (p.p.y < layerTop(2, p.p.x)) {
                waterLines_ += p.water;
                p.water = 0.f;
                p.age = p.life + 1.f;
            }
            continue;
        }
        if (p.kind == P_WET || p.kind == P_SAND) continue;
        // water reaching the sea splashes into it (behind the well, it just joins it)
        if ((p.kind == P_DROP || p.kind == P_POUR) && p.v.y < 0.f) {
            // once it falls below its cube, inside the well, the water passes behind the stack
            if (p.front && std::fabs(p.p.x) < WELL_X && p.p.y < p.y0 - 0.7f) p.front = false;
            const float sea = layerTop(2, p.p.x), sand = sandTop(p.p.x), top = std::max(sea, sand);
            bool gone = false;
            if (p.p.y < top) {
                if (std::fabs(p.p.x) > WELL_X + 0.1f && rng_.chance(0.5f))
                    hits.push_back({p.p.x, top, p.col, sea >= sand ? 0.9f : 0.35f});
                gone = true;
            } else if (std::fabs(p.p.x) < WELL_X && p.p.y < -10.4f) gone = true;
            if (gone) {
                waterLines_ += p.water;
                p.water = 0.f;
                p.age = p.life + 1.f;
            }
        }
    }
    for (const Particle& p : parts_)
        if (p.age > p.life) waterLines_ += p.water;
    parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Particle& p) { return p.age > p.life; }), parts_.end());
    for (const Hit& h : hits) splash(h.x, h.y, h.power, h.col);

    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
}

// =====================================================================================================
// Drawing
// =====================================================================================================

void SeaUniverse::bottle(float x, float y, float size, const vec3& col, float alpha, float rot, int mode, float fill, bool ghost, bool world,
                         float seed, float crack) {
    if (alpha <= 0.003f) return;
    cells_.push_back({{x, y, size, rot}, {col.x, col.y, col.z, alpha}, {ghost ? 1.f : 0.f, crack, (float)mode, fill},
                      {seed, world ? 1.f : 0.f, 0.f, 0.f}});
}

void SeaUniverse::sprite(float x, float y, float hx, float hy, float rot, int kind, const vec3& col, float alpha, float p0, float p1,
                         float seed) {
    if (alpha <= 0.003f) return;
    sprites_.push_back({{x, y, hx, hy}, {rot, (float)kind, p0, p1}, {col.x, col.y, col.z, alpha}, {0.f, 0.f, seed, 0.f}});
}

void SeaUniverse::flushCells() {
    if (cells_.empty()) return;
    glUseProgram(progCell_);
    setMat(progCell_, "uVP", vp_);
    setWaves(progCell_);
    set1f(progCell_, "uPx", px_);
    set1f(progCell_, "uTime", time_);
    set1f(progCell_, "uBridge", smoothstepf(0.3f, 0.95f, bridge_.amount));
    set3f(progCell_, "uBridgeInk", bridge_.ink);
    set1f(progCell_, "uLightPanel", pal_.lightPanel);
    set1f(progCell_, "uSlow", slow_);
    set3f(progCell_, "uSea2", pal_.sea2);
    set3f(progCell_, "uDeep", pal_.deep);
    set3f(progCell_, "uDusk", pal_.dusk);
    // a warm, honey-brown wood (darker at night)
    set3f(progCell_, "uWood", ugl::toLinear(hex(0xb07a48)) * (1.f - 0.45f * pal_.night));
    set2f(progCell_, "uRes", (float)width_, (float)height_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bgT_.tex);
    set1i(progCell_, "uBg", 0);
    glBindVertexArray(cellVao_);
    glBindBuffer(GL_ARRAY_BUFFER, cellVbo_);
    glBufferData(GL_ARRAY_BUFFER, cells_.size() * sizeof(CellInst), cells_.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)cells_.size());
    cells_.clear();
}

void SeaUniverse::flushSprites() {
    if (sprites_.empty()) return;
    glUseProgram(progSprite_);
    setMat(progSprite_, "uVP", vp_);
    set1f(progSprite_, "uTime", time_);
    set1f(progSprite_, "uPx", px_);
    set1f(progSprite_, "uLightPanel", pal_.lightPanel);
    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glBufferData(GL_ARRAY_BUFFER, sprites_.size() * sizeof(SpriteInst), sprites_.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)sprites_.size());
    sprites_.clear();
}

void SeaUniverse::drawParticles(bool front, float world) {
    // the bottles' jets: first one soft blue body for all of them (so that it reads over a pale sky as over the sand),
    // then their bright cores over it: one cohesive flow
    for (const Particle& p : parts_) {
        if (p.kind != P_STREAM || p.front != front || p.age < 0.f) continue;
        const float sp = std::sqrt(p.v.x * p.v.x + p.v.y * p.v.y);
        const float len = 1.f + std::min(sp * 0.18f, 2.2f);
        sprite(p.p.x, p.p.y, p.size * 1.3f * len, p.size * 1.3f, std::atan2(p.v.y, p.v.x), 0, lerp(WATER, pal_.sea2, 0.6f), 0.3f, 0.7f);
    }
    for (const Particle& p : parts_) {
        if (p.front != front || p.age < 0.f) continue;
        const float t = p.age / p.life;
        float a = 1.f - smoothstepf(0.6f, 1.f, t);
        if (!front) a *= world;
        switch (p.kind) {
        case P_SHARD: sprite(p.p.x, p.p.y, p.size, p.size * 0.7f, p.rot, 1, p.col, a * 0.95f, 0.f, 0.f, p.seed); break;
        case P_DROP:
        case P_POUR: {
            const float sp = std::sqrt(p.v.x * p.v.x + p.v.y * p.v.y);
            const float s = std::max(p.size, px_ * 0.9f);
            // over the well (the stack below a cleared row), the water stays light
            const float over = std::fabs(p.p.x) < WELL_X ? 0.8f : 0.95f;
            sprite(p.p.x, p.p.y, s * (1.f + std::min(sp * 0.07f, 1.6f)), s, std::atan2(p.v.y, p.v.x), 0, p.col, a * over, 0.5f);
            break;
        }
        case P_CLOUD: {
            const float s = p.size * (1.f + 1.6f * t);
            sprite(p.p.x, p.p.y, s, s, 0.f, 0, p.col, 0.5f * (1.f - t) * (front ? 1.f : world), 1.f);
            break;
        }
        case P_BUBBLE: sprite(p.p.x, p.p.y, p.size, p.size, 0.f, 3, p.col, a * 0.9f); break;
        case P_STREAM: {
            // a clear stream: drops stretched along their way, overlapping into one flow
            const float sp = std::sqrt(p.v.x * p.v.x + p.v.y * p.v.y);
            sprite(p.p.x, p.p.y, p.size * (1.f + std::min(sp * 0.18f, 2.2f)), p.size, std::atan2(p.v.y, p.v.x), 0, p.col, 0.8f, 0.7f);
            break;
        }
        case P_RUN: {
            // water running on the sand, thinning as it soaks in
            const float sl = std::atan((sandTop(p.p.x + 0.2f) - sandTop(p.p.x - 0.2f)) / 0.4f);
            sprite(p.p.x, p.p.y, p.size * 2.4f * (1.f - 0.5f * t), p.size * 0.55f, sl, 0, p.col, 0.75f * (1.f - t), 0.6f);
            break;
        }
        case P_WET: {
            const float g = smoothstepf(0.f, 0.15f, t) * (1.f - smoothstepf(0.4f, 1.f, t));
            sprite(p.p.x, p.p.y, p.size * (0.6f + 0.5f * smoothstepf(0.f, 0.3f, t)), 0.075f, p.rot, 0, p.col, 0.5f * g * world, 0.8f);
            break;
        }
        case P_SAND: sprite(p.p.x, p.p.y, p.size, p.size, 0.f, 0, p.col, a, 0.4f); break;
        case P_SPLASH: sprite(p.p.x, p.p.y, std::max(p.size, px_), std::max(p.size, px_), 0.f, 0, p.col, a * 0.85f, 0.5f); break;
        case P_SPRAY: {
            const float sp = std::sqrt(p.v.x * p.v.x + p.v.y * p.v.y);
            const float s = std::max(p.size, px_ * 0.8f);
            const float behind = std::fabs(p.p.x) < WELL_X + 0.2f && p.p.y < 10.f ? 0.25f : 1.f;
            sprite(p.p.x, p.p.y, s * (1.f + std::min(sp * 0.12f, 2.f)), s, std::atan2(p.v.y, p.v.x), 0, p.col, a * 0.7f * behind, 0.7f);
            break;
        }
        default: break;
        }
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float SeaUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
    if (!f.ok || s.empty()) return s.size() * size * 0.5f;
    const stbtt_fontinfo* info = (const stbtt_fontinfo*)f.info;
    const float k = size / f.px;
    float w = 0;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 32 || ch >= 128) ch = '?';
        w += f.g[ch - 32].adv * k + track;
        if (i + 1 < s.size()) {
            unsigned char n = (unsigned char)s[i + 1];
            if (n < 32 || n >= 128) n = '?';
            w += stbtt_GetCodepointKernAdvance(info, ch, n) * f.scaleTT * k;
        }
    }
    return w - track;
}

float SeaUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
                             std::vector<float>& out) const {
    if (!f.ok) return 0.f;
    const stbtt_fontinfo* info = (const stbtt_fontinfo*)f.info;
    const float k = size / f.px;
    const float baseline = y + f.capH * k;
    float pen = x;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 32 || ch >= 128) ch = '?';
        const Glyph& g = f.g[ch - 32];
        if (g.w > 0) {
            const float x0 = pen + g.ox * k, y0 = baseline + g.oy * k, x1 = x0 + g.w * k, y1 = y0 + g.h * k;
            const float v[6][4] = {{x0, y0, g.x0, g.y0}, {x1, y0, g.x1, g.y0}, {x1, y1, g.x1, g.y1},
                                   {x0, y0, g.x0, g.y0}, {x1, y1, g.x1, g.y1}, {x0, y1, g.x0, g.y1}};
            for (auto& q : v) out.insert(out.end(), q, q + 4);
        }
        pen += g.adv * k + track;
        if (i + 1 < s.size()) {
            unsigned char n = (unsigned char)s[i + 1];
            if (n < 32 || n >= 128) n = '?';
            pen += stbtt_GetCodepointKernAdvance(info, ch, n) * f.scaleTT * k;
        }
    }
    return pen - track - x;
}

void SeaUniverse::drawText(const Font& f, const std::vector<float>& q, const vec3& ink, float alpha, float bias) {
    if (q.empty() || !f.ok) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    const float b = smoothstepf(0.3f, 0.95f, bridge_.amount);
    set3f(progText_, "uInk", lerp(ink, bridge_.ink, b));
    set1f(progText_, "uAlpha", alpha);
    set1f(progText_, "uBias", bias);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(float), q.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 4));
}

// How light the world is behind a point (board space): the panel, the sea, the sky.
float SeaUniverse::bgLuma(float bx, float by) const {
    vec3 c;
    const float top2 = layerTop(2, bx);
    if (by < top2) c = lerp(pal_.sea2, pal_.deep * 0.2f, darkAt(top2 - by) * 0.92f);
    else if (by < layerTop(1, bx)) c = pal_.sea1;
    else if (by < layerTop(0, bx)) c = pal_.sea0;
    else {
        const float hor = seaLevel() + L_OFF[0] * seaSpread();
        const float h = saturate((by - hor) / std::max(viewC_.y + viewH_.y - hor, 1.f));
        c = lerp(pal_.skyLow, pal_.skyTop, smoothstepf(0.f, 1.f, std::pow(h, 0.7f)));
        c = lerp(c, pal_.stormSky, storm_ * 0.85f);
    }
    if (by >= -10.f) c = lerp(c, pal_.stormSea, by < layerTop(0, bx) ? storm_ * 0.65f : 0.f);
    c = lerp(c, pal_.dusk, slow_ * 0.75f);
    if (std::fabs(bx) < 5.1f && std::fabs(by) < 10.1f)
        c = lerp(c, pal_.panel, lerpf(pal_.panelA, 0.88f, by < top2 ? smoothstepf(2.f, 10.f, top2 - by) : 0.f));
    return ugl::luma(c);
}

// The HUD in SEA's typography: Barlow, a plain grotesk like the lettering on a harbor's signs; italic for the song, the
// section and the celebrations, spaced capitals for labels. Ink dark over a light sky or panel, pale over the sea.
void SeaUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    if (!panels.empty()) {
        std::vector<Vert> v;
        auto quad = [&](float x0, float y0, float x1, float y1, const vec3& c, float a) {
            const Vert q[6] = {{x0, y0, c.x, c.y, c.z, a}, {x1, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a},
                               {x0, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a}, {x0, y1, c.x, c.y, c.z, a}};
            v.insert(v.end(), q, q + 6);
        };
        for (const HudRect& r : panels) {
            const vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? pal_.accent : pal_.panel;
            quad(r.x, r.y, r.x + r.w, r.y + r.h, c, std::min(1.f, r.alpha));
        }
        glUseProgram(progShape_);
        set2f(progShape_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(shapeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vert), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
    }
    const bool inkDark = ugl::luma(pal_.ink) < 0.2f;
    const vec3 darkInk = inkDark ? pal_.ink : ugl::toLinear(hex(0x1b2a38));
    const vec3 lightInk = inkDark ? ugl::toLinear(hex(0xf2f5f4)) : pal_.ink;
    std::vector<float> q;
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        auto inkAt = [&](float px, float py) {
            const float bx = viewC_.x + (px / width_ * 2.f - 1.f) * viewH_.x;
            const float by = viewC_.y - (py / height_ * 2.f - 1.f) * viewH_.y;
            // the ink with the most contrast against what is behind it
            const float l = (std::sqrt(bgLuma(bx, by - 0.3f)) + std::sqrt(bgLuma(bx, by)) + std::sqrt(bgLuma(bx, by + 0.3f))) / 3.f;
            return std::fabs(l - std::sqrt(ugl::luma(darkInk))) > std::fabs(l - std::sqrt(ugl::luma(lightInk))) ? darkInk : lightInk;
        };
        vec3 ink = inkAt(h.x < 0.f ? width_ + h.x : h.x, h.y + base * 0.5f);
        vec3 accent = lerp(pal_.accent, ink, 0.25f);
        const float alpha = saturate(h.alpha);
        q.clear();
        auto place = [&](const Font& f, const std::string& text, float size, float track, float dy = 0.f) {
            const float w = textWidth(f, text, size, track);
            const float x = ugl::hudLeft(h, w, width_);
            ink = inkAt(x + w * 0.5f, h.y + dy + size * 0.8f);
            accent = lerp(pal_.accent, ink, 0.25f);
            textQuads(f, text, x, h.y + dy, size, track, q);
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            // "Sea" in the italic, the variant's name in small spaced capitals after it.
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            name = titleCase(name);
            const float sz = base * 1.4f, vsz = base * 0.66f, vtr = vsz * 0.3f;
            const float wName = textWidth(fontItalic_, name, sz, 0.f);
            const float wVar = var.empty() ? 0.f : textWidth(fontRoman_, var, vsz, vtr);
            const float gap = base * 0.8f;
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            textQuads(fontItalic_, name, x, h.y - base * 0.12f, sz, 0.f, q);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            drawText(fontItalic_, q, inkAt(x + wName * 0.5f, h.y + sz * 0.4f), a, 0.03f);
            if (!var.empty()) {
                q.clear();
                textQuads(fontRoman_, var, x + wName + gap, h.y + base * 0.22f, vsz, vtr, q);
                drawText(fontRoman_, q, inkAt(x + wName + gap + wVar * 0.5f, h.y + base * 0.22f + vsz * 0.5f), a * 0.85f, 0.02f);
            }
            continue;
        }
        case HUD_SECTION:
            place(fontItalic_, titleCase(h.text), base * 1.35f, 0.f);
            drawText(fontItalic_, q, ink, saturate(0.2f + h.alpha * 1.9f), 0.03f);
            continue;
        case HUD_TITLE:
            place(fontItalic_, h.text, base * 1.25f, 0.f, -base * 0.12f);
            drawText(fontItalic_, q, ink, saturate(h.alpha * 1.3f), 0.03f);
            continue;
        case HUD_INFO:
            place(fontRoman_, h.text, base * 0.78f, base * 0.12f);
            drawText(fontRoman_, q, ink, saturate(alpha * 1.2f) * 0.8f, 0.02f);
            continue;
        case HUD_LABEL:
            place(fontRoman_, h.text, base * 0.74f, base * 0.32f);
            drawText(fontRoman_, q, ink, saturate(alpha * 1.2f) * 0.72f, 0.02f);
            continue;
        case HUD_VALUE:
            place(fontRoman_, h.text, base * 1.08f, base * 0.04f);
            drawText(fontRoman_, q, ink, alpha, 0.02f);
            continue;
        case HUD_NOTICE:
            place(fontItalic_, h.text, base * 1.15f, 0.f);
            drawText(fontItalic_, q, accent, alpha, 0.03f);
            continue;
        case HUD_KEY:
            place(fontRoman_, h.text, base * 0.88f, base * 0.05f);
            drawText(fontRoman_, q, ink, alpha, 0.02f);
            continue;
        case HUD_TOAST:
            place(fontItalic_, titleCase(h.text), base * 1.5f, 0.f, -base * 0.2f);
            drawText(fontItalic_, q, ink, alpha, 0.04f);
            continue;
        case HUD_TOAST_SUB:
            place(fontRoman_, h.text, base * 0.82f, base * 0.2f);
            drawText(fontRoman_, q, accent, alpha * 0.9f, 0.02f);
            continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontItalic_, titleCase(h.text), base * 1.3f, 0.f, -base * 0.12f);
                drawText(fontItalic_, q, ink, alpha, 0.04f);
            } else {
                place(fontRoman_, h.text, base * 0.86f, base * 0.1f);
                drawText(fontRoman_, q, h.accent ? accent : ink, alpha, 0.02f);
            }
            continue;
        default:
            place(fontRoman_, h.text, base * 0.84f, base * 0.06f);
            drawText(fontRoman_, q, h.accent ? accent : ink, alpha, 0.02f);
            continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void SeaUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const Palette target = paletteOf(variant_);
        if (!palSet_) { pal_ = target; palSet_ = true; }
        else lerpPalette(pal_, target, 1.f - std::exp(-dt * 1.0f));
    }
    // the section, from the HUD (a drop is a storm, a chorus a breeze)
    sectionKnown_ = false;
    for (const HudText& h : f.hud)
        if (h.role == HUD_SECTION) {
            sectionKnown_ = true;
            sectionDrop_ = h.text == "DROP";
        }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 0.9f, dt);
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.5f : 0.f), 3.f, dt);
    if (over_ && !game.over()) over_ = false;
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.4f, slow_);
    if (!f.paused) {
        time_ += dt;
        watchBoard(game);
        simulate(game, music, dt, wdt);
    }
    const float bridge = saturate(bridge_.amount);
    const float world = 1.f - smoothstepf(0.f, 0.6f, bridge);

    sceneT_.bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    // ---- The sky, the sea, the boats, the well's panel.
    {
        glUseProgram(progSea_);
        set4f(progSea_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set1f(progSea_, "uPx", px_);
        set1f(progSea_, "uTime", time_);
        set1f(progSea_, "uCloudT", cloudT_);
        set3f(progSea_, "uSkyTop", pal_.skyTop);
        set3f(progSea_, "uSkyLow", pal_.skyLow);
        set3f(progSea_, "uSunCol", pal_.sun);
        set3f(progSea_, "uCloud", pal_.cloud);
        set3f(progSea_, "uStormSky", pal_.stormSky);
        set3f(progSea_, "uStormCloud", pal_.stormCloud);
        set3f(progSea_, "uDusk", pal_.dusk);
        {
            // the sun keeps its place in the sky above the sea as it rises (the same fraction of the sky)
            const float h0 = -1.5f, top = 11.5f, h = std::max(seaLevel() + L_OFF[0] * seaSpread(), h0);
            const float sy = h + (pal_.sunY - h0) / (top - h0) * (top - h);
            set4f(progSea_, "uSun", pal_.sunX, sy, pal_.sunR, pal_.moon);
        }
        set1f(progSea_, "uLifeGlow", 0.5f + 0.5f * glowUp_);
        set3f(progSea_, "uSea0", pal_.sea0);
        set3f(progSea_, "uSea1", pal_.sea1);
        set3f(progSea_, "uSea2", pal_.sea2);
        set3f(progSea_, "uDeep", pal_.deep);
        set3f(progSea_, "uFoam", pal_.foam);
        set3f(progSea_, "uHaze", pal_.haze);
        set3f(progSea_, "uStormSea", pal_.stormSea);
        set3f(progSea_, "uPanel", pal_.panel);
        {
            const vec3 sand = lerp(vec3(0.62f, 0.47f, 0.29f), pal_.haze, 0.15f) * (1.f - 0.62f * pal_.night);
            set3f(progSea_, "uSand", sand);
        }
        set1f(progSea_, "uPanelA", pal_.panelA * (1.f - bridge));
        setWaves(progSea_);
        set1f(progSea_, "uStorm", storm_ * world);
        set1f(progSea_, "uCaps", caps_ * world);
        set1f(progSea_, "uGlint", glint_ * world);
        set1f(progSea_, "uRain", storm_ * world);
        set1f(progSea_, "uFlash", flash_ * world);
        set1f(progSea_, "uFlashX", flashX_);
        set1f(progSea_, "uSlow", slow_);
        set1f(progSea_, "uBridge", smoothstepf(0.05f, 0.85f, bridge));
        set3f(progSea_, "uBridgeCol", bridge_.color);
        set1f(progSea_, "uStars", pal_.stars);
        set1f(progSea_, "uClouds", pal_.clouds);
        ugl::drawFullscreen();
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // ---- The boats, each on its quad, behind the water in front of them.
    if (!boats_.empty() && world > 0.01f) {
        std::vector<float> inst;
        for (const Boat& bt : boats_) {
            const float v[8] = {bt.x, (float)bt.layer, bt.scale, (float)bt.type, bt.dir, bt.phase, bt.alpha * world, bt.seed};
            inst.insert(inst.end(), v, v + 8);
        }
        glUseProgram(progBoat_);
        setMat(progBoat_, "uVP", vp_);
        setWaves(progBoat_);
        set1f(progBoat_, "uTime", time_);
        set1f(progBoat_, "uRock", rock_);
        set1f(progBoat_, "uPx", px_);
        set1f(progBoat_, "uStorm", storm_ * world);
        set1f(progBoat_, "uSlow", slow_);
        set1f(progBoat_, "uPanelA", pal_.panelA * (1.f - bridge));
        set1f(progBoat_, "uNight", pal_.night);
        set3f(progBoat_, "uHaze", pal_.haze);
        set3f(progBoat_, "uDusk", pal_.dusk);
        set3f(progBoat_, "uPanel", pal_.panel);
        set3f(progBoat_, "uHull", pal_.hull);
        set3f(progBoat_, "uSuper", pal_.super_);
        set3f(progBoat_, "uSail", pal_.sail);
        set3f(progBoat_, "uBAccent", pal_.baccent);
        set3f(progBoat_, "uWindow", pal_.window);
        set3f(progBoat_, "uCont0", pal_.cont0);
        set3f(progBoat_, "uCont1", pal_.cont1);
        set3f(progBoat_, "uCont2", pal_.cont2);
        glBindVertexArray(boatVao_);
        glBindBuffer(GL_ARRAY_BUFFER, boatVbo_);
        glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(float), inst.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)boats_.size());
    }
    // ---- The life in the water.
    drawLife(world);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // ---- Splashes and spray (behind the stack).
    drawParticles(false, world);
    flushSprites();
    // ---- A copy of the scene so far: what the glass shows (bent) behind it.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneT_.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bgT_.fbo);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    sceneT_.bind();
    // ---- The empty bottles on the sand, their stoppers by them.
    for (const Bottle& b : bottles_) {
        if (b.state == BS_FLY) continue;
        bottle(b.p.x, b.p.y, 1.f, pal_.glass[b.type], 1.f, b.rot, b.capState == CS_ON ? B_CLOSED : B_OPEN, b.fill, false, true, b.seed);
    }
    for (const Bottle& b : bottles_) {
        if (b.capState == CS_ON || b.capState == CS_GONE) continue;
        const float y = b.capState == CS_REST ? b.cp.y - buryOf(b.side, b.cbury0) : b.cp.y;
        bottle(b.cp.x, y, 1.f, pal_.glass[b.type], 1.f, b.crot, B_CAP, 0.f, false, true, b.seed);
    }
    // ---- The ghost, the stack, the falling piece: every cell its own glass bottle of water.
    if (game.hasPiece() && !game.over()) {
        const Piece& p = game.piece();
        Piece gp = p;
        gp.y = game.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            game.pieceCells(gp, gc);
            for (auto& c : gc) {
                if (c[1] < Game::HIDDEN) continue;
                const vec3 w = layout::cellPos((float)c[0], (float)c[1]);
                bottle(w.x, w.y, 1.f, pal_.glass[std::clamp(p.type, 0, 6)], 1.f, 0.f, B_CLOSED, B_FULL, true, false, 0.f);
            }
        }
    }
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const int t = game.cell(x, y).type;
            if (t < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - game.rowOffset(y));
            bottle(p.x, p.y, 1.f, pal_.glass[std::clamp(t, 0, 6)], 1.f, 0.f, B_CLOSED, B_FULL, false, false, hashf((float)x, (float)y * 7.f + t) * 50.f);
        }
    for (const Dying& d : dying_)
        bottle(d.x, d.y, 1.f, pal_.glass[d.type], 1.f, 0.f, B_CLOSED, B_FULL, false, false, d.x * 3.f + d.y, smoothstepf(d.delay - 0.15f, d.delay, d.t));
    if (game.hasPiece() && !game.over()) {
        const Piece& p = game.piece();
        int cells[4][2];
        game.pieceCells(p, cells);
        const float dx = game.pieceVisualX() - p.x, dy = game.pieceVisualY() - p.y;
        const int type = std::clamp(p.type, 0, 6);
        for (int i = 0; i < 4; i++) {
            const auto& c = cells[i];
            if (c[1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(c[0] + dx, c[1] + dy);
            bottle(w.x, w.y, 1.f, pal_.glass[type], 1.f, 0.f, B_CLOSED, B_FULL, false, false, (float)(type * 7 + i * 13));
        }
    }
    // ---- Previews: four small bottles each.
    auto preview = [&](int type, vec2 center, float sc, float alpha) {
        if (type < 0) return;
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        game.pieceCells(p, cells);
        float minx = 9, maxx = -9, miny = 9, maxy = -9;
        for (auto& c : cells) {
            minx = std::min(minx, (float)c[0]); maxx = std::max(maxx, (float)c[0]);
            miny = std::min(miny, (float)c[1]); maxy = std::max(maxy, (float)c[1]);
        }
        for (int i = 0; i < 4; i++) {
            const auto& c = cells[i];
            const vec2 pos = v2(center.x + (c[0] - (minx + maxx) * 0.5f) * sc, center.y - (c[1] - (miny + maxy) * 0.5f) * sc);
            bottle(pos.x, pos.y, sc, pal_.glass[std::clamp(type, 0, 6)], alpha, 0.f, B_CLOSED, B_FULL, false, false, (float)(type * 3 + i * 11));
        }
    };
    preview(game.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), game.holdUsed() ? 0.35f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(game.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }
    // ---- The bottles flying out of a cleared row.
    for (const Bottle& b : bottles_)
        if (b.state == BS_FLY) bottle(b.p.x, b.p.y, 1.f, pal_.glass[b.type], 1.f, b.rot, B_CLOSED, b.fill, false, true, b.seed);
    flushCells();
    // ---- The bonus gauge: a round glass filling with water.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        const float fill = game.bonusActive() ? saturate(game.bonusLeft() / 10.f) : saturate(gaugeShown_);
        const float ready = game.bonusReady() || game.bonusActive() ? 1.f : 0.f;
        sprite(gc.x, gc.y, R, R, 0.f, 2, pal_.piece[0], 1.f - 0.6f * smoothstepf(0.3f, 0.95f, bridge), fill, ready);
    }
    // ---- Shards and water of the stack itself.
    drawParticles(true, world);
    flushSprites();
    drawHud(f.hud, f.panels);
    glDisable(GL_BLEND);

    // ---- Post.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progPost_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneT_.tex);
    set1i(progPost_, "uScene", 0);
    set1f(progPost_, "uFade", saturate(f.fade));
    set1f(progPost_, "uDim", dimT_);
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_sea() {
    static const UniverseEntry e{
        "SEA", "glass bottles of water over a rising sea", [] { return std::unique_ptr<Universe>(new SeaUniverse()); },
        // Songs with a swell and a drop to storm on suit it best; any of them a little.
        [](const SongLook& l) { return 0.75f + 0.2f * l.dynamics + 0.15f * l.airWeight + 0.1f * l.bassWeight; },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
