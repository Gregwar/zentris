// SEA: glass cubes of water over a sea that rises with the cleared lines.
//
// One world, seen from the side: a plain sky over a sea in three layers like cut paper (far, middle, near), each with
// its own slow swell and a few glints of light. The pieces are glass vessels filled with water: a clear shell with a
// fine wall and a light line along its upper left inside, and water in it up to about four fifths, one level for the
// whole piece (it levels out again when the piece turns, it tilts when the piece moves, it sloshes when the piece
// lands hard). Each piece type has its own water (sea blues, teals and greens per variant).
//
// A cleared row breaks: a few small sharp shards of glass fly off and its water spills out, pouring off both sides of
// the well into the sea below. Every line poured in raises the sea a little, smoothly, for the whole song (up to about
// a third of the picture, the well and the HUD stay readable; a new song starts again from a low sea). Boats sail
// slowly across it at various distances, and the deeper it gets the bigger the boats that come: dinghies and
// sailboats on a shallow sea, then fishing boats, then ferries, container ships and liners.
//
// Calm parts leave the sea glassy, mid brings a gentle swell, a chorus a livelier one with a few whitecaps. A drop
// raises a storm, eased in over about a second: big, faster waves with whitecaps and spray, foam streaks, slanting
// rain, dark clouds, boats rocking hard, a lightning glow in the clouds on its first downbeat and every four bars,
// spray torn off the crests on the kicks. It calms back over a few seconds after the drop. Behind the well the sea
// stays flat under a calm, subdued panel, so the glass reads at a glance.
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
// the sea's level: the near layer's base, low at the start of a song, at most about a third of the picture up
// the sea starts empty, under the sand (its far layer below the lowest dune), and rises as water falls in
constexpr float SEA_LOW = -12.6f, SEA_HIGH = -3.0f;
// deep enough for boats: the near layer above the highest dune
constexpr float BOAT_RISE = 0.45f;
// how many poured lines raise the sea most of the way
constexpr float SEA_LINES = 8.f;
// the layers (WAVE_GLSL's L_OFF, L_AMP, L_FRQ, L_SPD)
const float L_OFF[3] = {1.15f, 0.55f, 0.f}, L_AMP[3] = {0.35f, 0.65f, 1.f}, L_FRQ[3] = {1.7f, 1.25f, 0.85f}, L_SPD[3] = {0.6f, 0.8f, 1.f};
// a boat's half length at scale 1 (BOAT_VS's BOAT_HALF), its layer's scale
const float BOAT_LEN[6] = {0.5f, 0.9f, 1.1f, 1.9f, 3.3f, 3.7f};
const float LAYER_SCALE[3] = {0.5f, 0.75f, 1.0f};

// The variants (sRGB): the sky (top, low), the sun or moon, the clouds, the storm's sky and clouds; the sea's three
// layers (far, middle, near), its depths, foam, haze and storm water; the well's panel; the boats (hull,
// superstructure, sails, accent, windows, three container colors); the HUD ink, the accent, the dusk of slow motion;
// the seven waters; the sun (x, y, radius), moon 0/1, stars, the panel's opacity, cloudiness, a light panel (dark
// glass edges), night (lit windows).
struct VariantDef {
    const char* name;
    unsigned skyTop, skyLow, sun, cloud, stormSky, stormCloud;
    unsigned sea0, sea1, sea2, deep, foam, haze, stormSea, panel;
    unsigned hull, super_, sail, baccent, window, cont0, cont1, cont2;
    unsigned ink, accent, dusk;
    unsigned piece[7];
    float sunX, sunY, sunR, moon, stars, panelA, clouds, lightPanel, night;
};
const VariantDef VARIANTS[] = {
    // A calm noon: a clear blue sky, a bright sea, white sails; cyan, cobalt, indigo, sea green, teal, azure, seafoam.
    {"CALM NOON", 0x6aa6dc, 0xe2eef2, 0xfffbea, 0xffffff, 0x5a6472, 0x3e4652,
     0xa2ccd8, 0x4f9fc0, 0x23709c, 0x0f3d62, 0xf6fbfb, 0xd6e8ee, 0x3a5462, 0xf0f5f6,
     0x22344a, 0xf3f1ea, 0xfbfaf4, 0xd0583c, 0x2c4458, 0xd0583c, 0x3d7fa6, 0xe0b040,
     0x1b3348, 0x1f88b8, 0x2a2f5a,
     {0x14c0cc, 0x1e56b0, 0x7a4cc8, 0x1fa676, 0x0a7a84, 0x6aa8f4, 0x8ad6a8},
     -14.f, 8.5f, 1.0f, 0.f, 0.f, 0.6f, 0.3f, 1.f, 0.f},
    // A golden hour: a low sun in an apricot haze, a sea of copper and slate, a dusky panel; bright clear waters.
    {"GOLDEN HOUR", 0x34446e, 0xf4b47c, 0xffe4b4, 0xf2a888, 0x3a3248, 0x2a2534,
     0xe0aa88, 0x8a7c94, 0x3a4870, 0x1a2242, 0xffe8d4, 0xf2c6a0, 0x2c3048, 0x1a2140,
     0x1d1f33, 0xf6e6d2, 0xfff1e0, 0xe0603a, 0xffd08a, 0xe0603a, 0x3d6f96, 0xe8b24a,
     0xfbeee0, 0xffb36a, 0x241a3c,
     {0x4ae0ea, 0x3c8af2, 0x8e7cf6, 0x48d69a, 0x20b2b0, 0x90caff, 0xb8f2d0},
     15.5f, -1.0f, 1.6f, 0.f, 0.f, 0.62f, 0.35f, 0.f, 0.35f},
    // A grey North Sea: an overcast sky, grey-green water, red fishing hulls; deep teals and greens in the glass.
    {"GREY NORTH SEA", 0x98a4ac, 0xd6dbd8, 0xe4e6e2, 0xc6cccd, 0x4a5254, 0x343b3e,
     0x9aaaa6, 0x5f7571, 0x34494a, 0x18282c, 0xeef2ee, 0xc8d0ce, 0x26363a, 0xe9ecea,
     0x2a3236, 0xe8e6de, 0xd8d4c8, 0xc8452e, 0x34404a, 0xb8503a, 0x4e6e80, 0xd09a3c,
     0x232c30, 0x3c8a86, 0x2a3040,
     {0x16a2b6, 0x2a5a90, 0x5a54a6, 0x2e9a64, 0x0e6a6c, 0x5c98d0, 0x7cc496},
     -12.f, 7.f, 0.95f, 0.f, 0.f, 0.62f, 0.75f, 1.f, 0.f},
    // A night harbor: a deep blue night, a moon, boats with lit windows; pale, luminous waters in the glass.
    {"NIGHT HARBOR", 0x070d22, 0x1c2a4c, 0xe8ecf4, 0x2a3658, 0x0a0f1c, 0x1a2236,
     0x26385c, 0x17284c, 0x0d1a38, 0x060c1c, 0xa4b8d4, 0x34466c, 0x0c1426, 0x0c1428,
     0x0a1020, 0x2e3c5a, 0x5c6c8c, 0x6a3a40, 0xffc870, 0x4a3040, 0x2a4058, 0x5a5030,
     0xdce6f4, 0x8ad8ff, 0x101030,
     {0x6ee6f2, 0x5c9cff, 0xac9cff, 0x6eeab0, 0x3ccaca, 0xa4ccff, 0xc4f6da},
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

// The 8 neighbours, in the shader's bit order (L R D U LD RD LU RU), as game offsets (game y grows down).
const int NDX[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
const int NDY[8] = {0, 0, 1, -1, 1, 1, -1, -1};

float crest(float s, float sharp) {
    const float c = s * 0.5f + 0.5f;
    return 2.f * std::pow(std::max(c, 0.f), 1.f + 2.5f * sharp) - 1.f;
}
float flatAt(float x) { return lerpf(1.f, 0.12f, smoothstepf(5.9f, 4.9f, std::fabs(x))); }

// how full a vessel is (its id)
float fillOf(int id) { (void)id; return 1.f; }

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool SeaUniverse::init(int width, int height) {
    progSea_ = compile(ugl::FULLSCREEN_VS, shaders::SEA_FS, "sea sky", true);
    progBoat_ = compile(shaders::BOAT_VS, shaders::BOAT_FS, "sea boat", true);
    progCell_ = compile(shaders::CELL_VS, shaders::CELL_FS, "sea cell");
    progSprite_ = compile(shaders::SPRITE_VS, shaders::SPRITE_FS, "sea sprite");
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
}

SeaUniverse::~SeaUniverse() {
    sceneT_.destroy();
    for (GLuint p : {progSea_, progBoat_, progCell_, progSprite_, progText_, progShape_, progPost_}) glDeleteProgram(p);
    for (GLuint v : {cellVao_, spriteVao_, boatVao_, textVao_, shapeVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, cellVbo_, spriteVbo_, boatVbo_, textVbo_, shapeVbo_}) glDeleteBuffers(1, &b);
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
    const int maxT = std::clamp((int)(1.f + rise_ * 5.5f), 1, 5);
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
// Game events
// =====================================================================================================

int SeaUniverse::maskOf(int x, int y) const {
    const Glass& s = glass_[y][x];
    int m = 0;
    if (s.id < 0) return 0;
    for (int i = 0; i < 8; i++) {
        const int nx = x + NDX[i], ny = y + NDY[i];
        if (nx < 0 || nx >= Game::W || ny < 0 || ny >= Game::H) continue;
        if (glass_[ny][nx].id == s.id) m |= 1 << i;
    }
    return m;
}

// The water's level (board y) in a vessel made of cells with these bottoms, filled to `fill` of its volume (the
// cells are joined, so one level fills them all from the bottom up).
float SeaUniverse::waterLevel(const float* bottoms, int n, float fill) {
    if (n <= 0) return 0.f;
    float lo = bottoms[0], hi = bottoms[0] + 1.f;
    for (int i = 1; i < n; i++) { lo = std::min(lo, bottoms[i]); hi = std::max(hi, bottoms[i] + 1.f); }
    const float want = fill * n;
    for (int it = 0; it < 24; it++) {
        const float mid = 0.5f * (lo + hi);
        float v = 0.f;
        for (int i = 0; i < n; i++) v += std::clamp(mid - bottoms[i], 0.f, 1.f);
        (v < want ? lo : hi) = mid;
    }
    return 0.5f * (lo + hi);
}

// A cube breaks: its glass shatters into sharp shards and its water bursts out of it, up and outward, then falls
// (behind the stack, inside the well) to the sea or the sand; the sea rises as that water reaches it (`water`:
// the lines of sea the cube holds).
void SeaUniverse::breakCell(float x, float y, int type, float power, float water) {
    const vec3 tint = pal_.piece[std::clamp(type, 0, 6)];
    const float side = x < 0.f ? -1.f : 1.f;
    for (int i = 0; i < 6; i++) {
        Particle q{};
        q.p = v2(x + rng_.range(-0.45f, 0.45f), y + rng_.range(-0.45f, 0.45f));
        q.v = v2((q.p.x - x) * rng_.range(4.f, 9.f) * power + side * rng_.range(0.f, 1.5f), rng_.range(1.5f, 6.f) * power);
        q.life = rng_.range(0.5f, 0.8f);
        q.size = rng_.range(0.08f, 0.17f);
        q.rot = rng_.range(0.f, TAU); q.spin = rng_.range(-12.f, 12.f);
        q.grav = 20.f; q.drag = 0.4f;
        q.kind = P_SHARD; q.real = true; q.front = true; q.seed = rng_.uniform();
        q.col = pal_.lightPanel > 0.5f ? lerp(tint, vec3(1.f), 0.35f) : lerp(tint, vec3(1.f), 0.6f);
        parts_.push_back(q);
    }
    constexpr int N = 26;
    for (int i = 0; i < N && parts_.size() < 4000; i++) {
        Particle q{};
        const vec2 o = v2(rng_.range(-0.42f, 0.42f), rng_.range(-0.42f, 0.42f));
        q.p = v2(x + o.x, y + o.y);
        // bursting out of the cube in every direction, more upward and toward the nearer side of the well
        const float a = rng_.range(0.f, TAU);
        const float sp = rng_.range(1.5f, 5.f) * power;
        q.v = v2(std::cos(a) * sp + o.x * 4.f + side * rng_.range(0.f, 2.5f), std::sin(a) * sp * 0.6f + rng_.range(1.f, 4.f) * power);
        q.life = 4.f;
        q.size = rng_.range(0.07f, 0.15f);
        q.grav = 16.f; q.drag = 0.35f;
        q.kind = P_POUR; q.real = true; q.front = true; q.seed = rng_.uniform();
        q.col = lerp(tint, vec3(1.f), 0.2f * rng_.uniform());
        q.water = water / N;
        q.y0 = y;
        parts_.push_back(q);
    }
}

void SeaUniverse::splash(float x, float y, float power, const vec3& col) {
    if (parts_.size() > 3000) return;
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
    case GameEvent::HardDrop: lastDrop_ = ev.count; break;
    case GameEvent::Spawn: fallSnap_ = true; break;
    case GameEvent::Lock: {
        if (ev.cells.empty()) break;
        // the piece's cells make one vessel; a hard landing sloshes its water
        const int id = nextId_++;
        const float slosh = lastDrop_ >= 2 ? std::min(1.f, 0.55f + lastDrop_ / 20.f) : 0.25f;
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            Glass& s = glass_[c.y][c.x];
            s.id = id; s.type = std::clamp((int)c.type, 0, 6);
            s.fill = fillOf(id);
            s.seed = hashf((float)id, 7.f) * 50.f;
            s.slosh = slosh;
        }
        lastDrop_ = 0;
        break;
    }
    case GameEvent::Clear: {
        bool cleared[Game::H] = {};
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            cleared[c.y] = true;
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            const int type = std::clamp((int)c.type, 0, 6);
            // each cube's water: a tenth of a line of sea (more for a Tetris)
            breakCell(p.x, p.y, type, 1.f + 0.12f * (float)(std::min(ev.count, 4) - 1), (ev.count >= 4 ? 1.5f : 1.f) / Game::W);
        }
        // the glass bookkeeping collapses like the board
        Glass next[Game::H][Game::W];
        int dst = Game::H - 1;
        for (int y = Game::H - 1; y >= 0; y--) {
            if (cleared[y]) continue;
            for (int x = 0; x < Game::W; x++) next[dst][x] = glass_[y][x];
            dst--;
        }
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) glass_[y][x] = next[y][x];
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
            d.lvl = glass_[c.y][c.x].id >= 0 ? water_[c.y][c.x] : 0.78f;
            d.delay = 0.15f + (c.y - top) * 0.07f + std::fabs(c.x - 4.5f) * 0.015f;
            d.t = 0.f;
            d.broken = false;
            dying_.push_back(d);
        }
        for (auto& row : glass_)
            for (auto& c : row) c = Glass{};
        over_ = true;
        break;
    }
    default: break;
    }
}

// The glass follows the board: cells the universe has not seen land (a board filled for a review, a game joined in
// progress) are taken as they are, joined with their neighbours of the same type.
void SeaUniverse::syncGlass(const Game& g, float dt) {
    int unseen = 0;
    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            Glass& s = glass_[y][x];
            if (c.type < 0) { s = Glass{}; continue; }
            if (s.id < 0 || s.type != c.type) {
                s.id = 1000000 + c.type;
                s.type = std::clamp((int)c.type, 0, 6);
                s.fill = fillOf(s.id + x * 3 + y);
                s.seed = hashf((float)x, (float)y * 7.f + c.type) * 50.f;
                s.slosh = 0.f;
                unseen++;
            }
            s.slosh *= std::exp(-dt * 2.f);
        }
    // a whole stack appearing at once: a game in progress, its sea already risen
    if (unseen >= 12 && waterLines_ < 10.f) {
        waterLines_ = 10.f;
        rise_ = riseTarget();
        boats_.clear();
        for (int i = 0; i < 4; i++) spawnBoat(true);
    }
}

// Each vessel's water: one level for the joined cells of one piece (the parts of a piece split by a clear each find
// their own), sloshing after a hard landing.
void SeaUniverse::levelWater() {
    bool seen[Game::H][Game::W] = {};
    int qx[Game::H * Game::W], qy[Game::H * Game::W];
    float bottoms[Game::H * Game::W];
    for (int y0 = 0; y0 < Game::H; y0++)
        for (int x0 = 0; x0 < Game::W; x0++) {
            if (seen[y0][x0] || glass_[y0][x0].id < 0) continue;
            const int id = glass_[y0][x0].id;
            int n = 0, head = 0;
            qx[n] = x0; qy[n] = y0; n++;
            seen[y0][x0] = true;
            while (head < n) {
                const int x = qx[head], y = qy[head];
                head++;
                for (int i = 0; i < 4; i++) {
                    const int nx = x + NDX[i], ny = y + NDY[i];
                    if (nx < 0 || nx >= Game::W || ny < 0 || ny >= Game::H || seen[ny][nx] || glass_[ny][nx].id != id) continue;
                    seen[ny][nx] = true;
                    qx[n] = nx; qy[n] = ny; n++;
                }
            }
            for (int i = 0; i < n; i++) bottoms[i] = layout::cellPos(0.f, (float)qy[i]).y - 0.5f;
            const Glass& g0 = glass_[y0][x0];
            const float h = waterLevel(bottoms, n, g0.fill);
            const float slope = g0.slosh * 0.32f * std::sin(time_ * 8.5f + g0.seed) * std::min(1.f, 2.f / (float)n * 2.f);
            for (int i = 0; i < n; i++) {
                water_[qy[i]][qx[i]] = h - bottoms[i];
                slope_[qy[i]][qx[i]] = slope;
            }
        }
}

// The falling piece's water levels out again when it turns (a spring, a little overshoot) and tilts against its
// sideways motion.
void SeaUniverse::simulatePiece(const Game& g, float dt) {
    if (!g.hasPiece() || g.over()) { fallSnap_ = true; return; }
    const Piece& p = g.piece();
    int cells[4][2];
    g.pieceCells(p, cells);
    float bottoms[4], lo = 1e9f;
    for (int i = 0; i < 4; i++) {
        bottoms[i] = layout::cellPos(0.f, (float)cells[i][1]).y - 0.5f;
        lo = std::min(lo, bottoms[i]);
    }
    const float target = waterLevel(bottoms, 4, fillOf(nextId_)) - lo;
    const float vx = dt > 1e-4f ? (g.pieceVisualX() - prevVisX_) / dt : 0.f;
    prevVisX_ = g.pieceVisualX();
    if (fallSnap_ || p.type != fallType_) {
        fallH_ = target; fallHV_ = 0.f; fallTilt_ = 0.f; fallTiltV_ = 0.f;
        fallSnap_ = false;
        fallType_ = p.type;
        fallRot_ = p.rot;
        return;
    }
    fallRot_ = p.rot;
    const float h = std::min(dt, 1.f / 30.f);
    fallHV_ += (90.f * (target - fallH_) - 9.f * fallHV_) * h;
    fallH_ += fallHV_ * h;
    const float tiltT = std::clamp(-vx * 0.025f, -0.3f, 0.3f);
    fallTiltV_ += (110.f * (tiltT - fallTilt_) - 7.f * fallTiltV_) * h;
    fallTilt_ += fallTiltV_ * h;
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
        const int want = rise_ < BOAT_RISE ? 0 : (int)(3.f + 1.f * wp + 2.f * rise_);
        boatT_ -= wdt;
        if ((int)boats_.size() < want && boatT_ <= 0.f && world > 0.5f) {
            if (spawnBoat(false)) boatT_ = rng_.range(4.f, 10.f);
            else boatT_ = 1.f;
        }
        for (Boat& b : boats_) {
            b.x += b.dir * b.speed * wdt * (1.f + 0.4f * storm_);
            // the sea draining (a new song): they fade before they would sit on the sand
            b.alpha = approach(b.alpha, rise_ < BOAT_RISE ? 0.f : 1.f, 1.5f, dt);
        }
        boats_.erase(std::remove_if(boats_.begin(), boats_.end(),
                                    [&](const Boat& b) {
                                        return b.dir * b.x > viewH_.x + BOAT_LEN[b.type] * b.scale + 1.f || (rise_ < BOAT_RISE && b.alpha <= 0.f);
                                    }),
                     boats_.end());
    }

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
        p.v.y -= p.grav * pdt;
        const float k = std::exp(-p.drag * pdt);
        p.v.x *= k; p.v.y *= k;
        p.p.x += p.v.x * pdt; p.p.y += p.v.y * pdt;
        p.rot += p.spin * pdt;
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

void SeaUniverse::cell(float x, float y, float size, int mask, const vec3& col, float alpha, float level, float slope, bool ghost,
                       float seed) {
    if (alpha <= 0.003f) return;
    cells_.push_back({{x, y, size, (float)mask}, {col.x, col.y, col.z, alpha}, {level, slope, ghost ? 1.f : 0.f, 0.f}, {seed, 0.f, 0.f, 0.f}});
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
    set1f(progCell_, "uPx", px_);
    set1f(progCell_, "uTime", time_);
    set1f(progCell_, "uBridge", smoothstepf(0.3f, 0.95f, bridge_.amount));
    set3f(progCell_, "uBridgeInk", bridge_.ink);
    set1f(progCell_, "uLightPanel", pal_.lightPanel);
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
    if (by < layerTop(2, bx)) c = pal_.sea2;
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
    if (std::fabs(bx) < 5.1f && std::fabs(by) < 10.1f) c = lerp(c, pal_.panel, pal_.panelA);
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
        syncGlass(game, dt);
        simulate(game, music, dt, wdt);
        simulatePiece(game, dt);
    }
    levelWater();
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
        set4f(progSea_, "uSun", pal_.sunX, pal_.sunY, pal_.sunR, pal_.moon);
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
    // ---- Splashes and spray (behind the stack).
    drawParticles(false, world);
    flushSprites();
    // ---- The ghost, the stack, the falling piece.
    if (game.hasPiece() && !game.over()) {
        const Piece& p = game.piece();
        Piece gp = p;
        gp.y = game.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            game.pieceCells(gp, gc);
            auto inGhost = [&](int x, int y) {
                for (auto& c : gc)
                    if (c[0] == x && c[1] == y) return true;
                return false;
            };
            for (auto& c : gc) {
                if (c[1] < Game::HIDDEN) continue;
                int m = 0;
                for (int i = 0; i < 8; i++)
                    if (inGhost(c[0] + NDX[i], c[1] + NDY[i])) m |= 1 << i;
                const vec3 w = layout::cellPos((float)c[0], (float)c[1]);
                cell(w.x, w.y, 1.f, m, pal_.piece[std::clamp(p.type, 0, 6)], 1.f, 0.f, 0.f, true, 0.f);
            }
        }
    }
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Glass& s = glass_[y][x];
            if (s.id < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - game.rowOffset(y));
            cell(p.x, p.y, 1.f, maskOf(x, y), pal_.piece[s.type], 1.f, water_[y][x], slope_[y][x], false, s.seed);
        }
    for (const Dying& d : dying_) cell(d.x, d.y, 1.f, 0, pal_.piece[d.type], 1.f, d.lvl, 0.f, false, 0.f);
    if (game.hasPiece() && !game.over()) {
        const Piece& p = game.piece();
        int cells[4][2];
        game.pieceCells(p, cells);
        auto inPiece = [&](int x, int y) {
            for (auto& c : cells)
                if (c[0] == x && c[1] == y) return true;
            return false;
        };
        float lo = 1e9f, cx = 0.f;
        for (auto& c : cells) {
            lo = std::min(lo, layout::cellPos(0.f, (float)c[1]).y - 0.5f);
            cx += layout::cellPos((float)c[0], 0.f).x * 0.25f;
        }
        const float dx = game.pieceVisualX() - p.x, dy = game.pieceVisualY() - p.y;
        const int type = std::clamp(p.type, 0, 6);
        for (auto& c : cells) {
            if (c[1] < Game::HIDDEN - 1) continue;
            int m = 0;
            for (int i = 0; i < 8; i++)
                if (inPiece(c[0] + NDX[i], c[1] + NDY[i])) m |= 1 << i;
            const vec3 rest = layout::cellPos((float)c[0], (float)c[1]);
            const vec3 w = layout::cellPos(c[0] + dx, c[1] + dy);
            // the level at this cell's center, tilted about the piece's middle
            const float lvl = lo + fallH_ - (rest.y - 0.5f) + fallTilt_ * (rest.x - cx);
            cell(w.x, w.y, 1.f, m, pal_.piece[type], 1.f, lvl, fallTilt_, false, (float)(nextId_ % 97));
        }
    }
    // ---- Previews: small vessels, their water at rest.
    auto preview = [&](int type, vec2 center, float sc, float alpha) {
        if (type < 0) return;
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        game.pieceCells(p, cells);
        float minx = 9, maxx = -9, miny = 9, maxy = -9;
        float bottoms[4];
        for (int i = 0; i < 4; i++) {
            const auto& c = cells[i];
            minx = std::min(minx, (float)c[0]); maxx = std::max(maxx, (float)c[0]);
            miny = std::min(miny, (float)c[1]); maxy = std::max(maxy, (float)c[1]);
            bottoms[i] = -(float)c[1] - 0.5f;
        }
        const float h = waterLevel(bottoms, 4, 1.f);
        auto inPiece = [&](int x, int y) {
            for (auto& c : cells)
                if (c[0] == x && c[1] == y) return true;
            return false;
        };
        for (int i = 0; i < 4; i++) {
            const auto& c = cells[i];
            int m = 0;
            for (int k = 0; k < 8; k++)
                if (inPiece(c[0] + NDX[k], c[1] + NDY[k])) m |= 1 << k;
            const vec2 pos = v2(center.x + (c[0] - (minx + maxx) * 0.5f) * sc, center.y - (c[1] - (miny + maxy) * 0.5f) * sc);
            cell(pos.x, pos.y, sc, m, pal_.piece[std::clamp(type, 0, 6)], alpha, h - bottoms[i], 0.f, false, (float)type * 3.f);
        }
    };
    preview(game.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), game.holdUsed() ? 0.35f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(game.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }
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
        "SEA", "glass cubes of water over a rising sea", [] { return std::unique_ptr<Universe>(new SeaUniverse()); },
        // Songs with a swell and a drop to storm on suit it best; any of them a little.
        [](const SongLook& l) { return 0.75f + 0.2f * l.dynamics + 0.15f * l.airWeight + 0.1f * l.bassWeight; },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
