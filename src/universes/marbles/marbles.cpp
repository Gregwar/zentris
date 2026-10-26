// MARBLES: square glass tiles, each holding one glass marble.
//
// One world: a still sky over a landscape drawn as one plain silhouette (hills, hollows and sometimes terraces,
// from the song's seed), a far ridge in the haze behind it, and lots of empty air. The well is a smoked glass pane;
// each cell of a piece is a frosted square tile holding one glossy glass marble of the piece's color (a cat's eye
// or a swirl inside, the light focused through it into a bright crescent on the tile, a window's highlight).
//
// A cleared row pops: its tiles vanish at once and each marble is thrown up out of the well, shrinking into the
// distance as it arcs over to the landscape, where it lands, rolls down into the hollows and stays there, stacking
// with the others into heaps over the song (simple 2D circle physics against the heightfield, the marbles falling
// asleep once they rest). The landscape begins with a few marbles already lying in its hollows.
//
// The song: calm is still; mid sets a marble rolling now and then and lets one catch the light on the bars; a chorus
// makes the heaps glimmer (a few marbles glint on the kicks) under a soft glow from the land; a drop sends waves of
// light flowing through the heaps every two beats (each drop its own shape: out from the well, in from the edges,
// sweeping across, rising from the hollows, rings from a heap, diagonal bands), the marbles lighting up in sequence
// from within with glints along the front, and the kicks make the lit marbles on top of the heaps hop.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane (like GARDEN).
#include "universes/marbles/marbles.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/marbles/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Outfit (Light for titles, Medium for labels and numbers), SIL Open Font License, see OFL-Outfit.txt.
const unsigned char FONT_LIGHT[] = {
#include "universes/marbles/Outfit-Light.ttf.inc"
};
const unsigned char FONT_MEDIUM[] = {
#include "universes/marbles/Outfit-Medium.ttf.inc"
};

namespace shaders = marbles_shaders;
using ugl::set1f;
using ugl::set1i;
using ugl::set2f;
using ugl::set3f;
using ugl::set4f;
using ugl::setMat;

GLuint compile(const char* vs, const char* fs, const char* name) {
    std::string f(fs);
    f.insert(f.find('\n') + 1, shaders::COMMON_GLSL);
    return ugl::program(vs, f.c_str(), name);
}

vec2 v2(float x, float y) { return vec2{x, y}; }
vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
vec2 operator*(vec2 a, float k) { return {a.x * k, a.y * k}; }
float dot2(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
float len2(vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }
float hashf(float a, float b) {
    const uint64_t h = splitmix64((uint64_t)(int64_t)std::lround(a * 1000.f) * 73856093ull ^ (uint64_t)(int64_t)std::lround(b * 1000.f) * 19349663ull);
    return (h >> 40) * (1.0f / 16777216.0f);
}
float smin(float a, float b, float k) {
    const float h = saturate(0.5f + 0.5f * (b - a) / k);
    return lerpf(b, a, h) - k * h * (1.f - h);
}

constexpr float WELL_HALF = 5.25f, WELL_TOP = 10.3f;  // the pane, as the marbles feel it
constexpr float GRAV = 26.f;
constexpr float TILE_H = 0.46f, TILE_R = 0.31f;      // a tile's half size, its marble's radius (board units)
constexpr int MAX_MARBLES = 420;
constexpr float POP = 0.14f;                          // a tile pops in this long (s, real time)

// The variants (sRGB): the sky (top, low), the haze on the far marbles, the far ridge, the land (top, deep), its rim
// light, the well's pane and its edge, the tiles' frost, the HUD ink, the accent, the drop's light, the night of slow
// motion, the moon or sun; the seven marbles; how much the tiles take their marble's tint and how opaque they are,
// the pane's opacity, the moon or sun (x, y, radius, on), stars, a light ground, the halos' strength.
struct VariantDef {
    const char* name;
    unsigned skyTop, skyLow, haze, ridge, land, landDeep, rim, panel, panelEdge, tile, ink, accent, wave, night, orb;
    unsigned piece[7];
    float tileTint, tileAlpha, panelAlpha, orbX, orbY, orbR, orbOn, stars, light, halo;
};
// Pieces in the game's order: I, O, T, S, Z, J, L.
const VariantDef VARIANTS[] = {
    // A clear night: ink-blue hills under a small moon, jewel marbles; the drop's light is warm gold.
    {"MIDNIGHT", 0x070b1c, 0x1a2644, 0x22304e, 0x141e36, 0x0b1124, 0x05080f, 0x6a7cb0, 0x080c18, 0x3a4668, 0x8a9ac8,
     0xdfe6ff, 0xffd27a, 0xffe6b0, 0x040818, 0xe8ecff,
     {0x3fd0e8, 0xffc93c, 0xa66cff, 0x4fd67a, 0xff4d5e, 0x3d7bff, 0xff8a3d},
     0.35f, 0.16f, 0.84f, -15.5f, 7.6f, 0.95f, 1.f, 1.f, 0.f, 1.f},
    // Dusk: a plum sky warming to apricot over aubergine hills, a low sun; the drop's light is rose gold.
    {"DUSK", 0x241a40, 0xd08468, 0x8a5e70, 0x5e4262, 0x26182c, 0x120a16, 0xffb08a, 0x1a1224, 0x5a4060, 0xf0c0b0,
     0xfff0e6, 0xffc88a, 0xffd8b8, 0x120e30, 0xffe2b0,
     {0x5ad8d0, 0xffd25a, 0xc77dff, 0x9be070, 0xff5a6e, 0x5a8cff, 0xff9a4a},
     0.30f, 0.15f, 0.80f, 15.5f, -1.2f, 1.7f, 1.f, 0.f, 0.f, 1.f},
    // Porcelain: a cool white studio sky over pale grey hills, saturated marbles; the drop's light is white.
    {"PORCELAIN", 0xdfe3e8, 0xf4f5f6, 0xe4e7eb, 0xd2d8df, 0xb4bcc8, 0x949eac, 0xffffff, 0xf7f8fa, 0xc4cad2, 0xffffff,
     0x262c34, 0x1f4fd0, 0xffffff, 0x1a2236, 0xffffff,
     {0x1aa0b8, 0xe8a400, 0x7a3cc8, 0x1f9a4e, 0xd8283c, 0x1f4fd0, 0xe8641a},
     0.42f, 0.42f, 0.86f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.55f},
    // Sea glass: a misty teal night, a pale moon in the fog, pastel sea-glass marbles; the drop's light is cyan white.
    {"SEA GLASS", 0x0a1e24, 0x2a5056, 0x355a60, 0x1a363c, 0x0a1a1e, 0x040c0e, 0x7ac0c0, 0x08161a, 0x3a5a5e, 0xbfe8e8,
     0xe0f4f2, 0xa8fff0, 0xd8fff8, 0x061418, 0xd8f0f0,
     {0x8ff0e8, 0xf2d98a, 0xc8a8f0, 0x9fe0a0, 0xf09a9a, 0x6aa8f0, 0xf0b878},
     0.30f, 0.16f, 0.82f, 14.5f, 6.8f, 1.5f, 0.7f, 0.35f, 0.f, 1.f},
    // Obsidian: black on black, the hills told by their rim alone, vivid marbles; the drop's light is white.
    {"OBSIDIAN", 0x040406, 0x0e0e12, 0x16161c, 0x0b0b0f, 0x050507, 0x020203, 0x5a5a66, 0x040405, 0x34343e, 0xffffff,
     0xe8e8ee, 0xffffff, 0xffffff, 0x04060e, 0xffffff,
     {0x00e0ff, 0xffe600, 0xd040ff, 0x40ff7a, 0xff2d55, 0x3a6bff, 0xff7a1a},
     0.25f, 0.08f, 0.88f, 0.f, 0.f, 0.f, 0.f, 0.25f, 0.f, 1.f},
    // Noon: a blue sky over slate hills, a white sun; the drop's light is white.
    {"NOON", 0x86aed6, 0xe2ebf2, 0xc4d4e2, 0xa4b8cc, 0x687c92, 0x48586a, 0xf4f8fc, 0xf0f4f8, 0xb0bccb, 0xffffff,
     0x22303e, 0xe85a2a, 0xffffff, 0x182238, 0xfffaf0,
     {0x16b0c8, 0xf0b400, 0x8a48d8, 0x2aa860, 0xe03a48, 0x2a5ce0, 0xf07a20},
     0.42f, 0.42f, 0.84f, -16.f, 7.4f, 1.0f, 1.f, 0.f, 1.f, 0.6f},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

using Palette = MarblesUniverse::Palette;
static_assert(sizeof(Palette) % sizeof(float) == 0, "a palette is floats only");
constexpr int PAL_FLOATS = (int)(sizeof(Palette) / sizeof(float));

Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    auto L = [](unsigned c) { return ugl::toLinear(hex(c)); };
    Palette p;
    p.skyTop = L(d.skyTop); p.skyLow = L(d.skyLow); p.haze = L(d.haze); p.ridge = L(d.ridge); p.land = L(d.land);
    p.landDeep = L(d.landDeep); p.rim = L(d.rim); p.panel = L(d.panel); p.panelEdge = L(d.panelEdge); p.tile = L(d.tile);
    p.ink = L(d.ink); p.accent = L(d.accent); p.wave = L(d.wave); p.night = L(d.night); p.orb = L(d.orb);
    for (int i = 0; i < 7; i++) p.piece[i] = L(d.piece[i]);
    p.tileTint = d.tileTint; p.tileAlpha = d.tileAlpha; p.panelAlpha = d.panelAlpha;
    p.orbX = d.orbX; p.orbY = d.orbY; p.orbR = d.orbR; p.orbOn = d.orbOn; p.stars = d.stars; p.light = d.light; p.halo = d.halo;
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

// A marble's jolt in its tile after an impact (board units, down first).
float jolt(float amp, float t) { return amp <= 0.f ? 0.f : -amp * std::exp(-t * 9.f) * std::cos(t * 30.f); }

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool MarblesUniverse::init(int width, int height) {
    progSky_ = compile(ugl::FULLSCREEN_VS, shaders::SKY_FS, "marbles sky");
    progMarble_ = compile(shaders::MARBLE_VS, shaders::MARBLE_FS, "marbles marble");
    progFx_ = compile(shaders::FX_VS, shaders::FX_FS, "marbles fx");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "marbles text");
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "marbles shape");
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "marbles post");

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
    instanced(marbleVao_, marbleVbo_, 4, sizeof(MarbleInst));
    instanced(fxVao_, fxVbo_, 3, sizeof(FxInst));

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

    glGenTextures(1, &landTex_);
    glBindTexture(GL_TEXTURE_2D, landTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    buildFont(fontLight_, FONT_LIGHT);
    buildFont(fontMedium_, FONT_MEDIUM);
    pal_ = paletteOf(0);
    resize(width, height);
    buildLand();
    return glGetError() == GL_NO_ERROR;
}

// Glyphs baked as signed distance fields (they scale cleanly).
void MarblesUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] marbles font init failed\n");
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

void MarblesUniverse::resize(int width, int height) {
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
    gridX0_ = -std::max(LAND_X, hw + 2.f);
    gridY0_ = viewC_.y - hh - 3.f;
    gridW_ = (int)std::ceil(-2.f * gridX0_ / GRID) + 1;
    gridH_ = (int)std::ceil((2.f * hh + 6.f) / GRID) + 1;
}

MarblesUniverse::~MarblesUniverse() {
    sceneT_.destroy();
    for (GLuint p : {progSky_, progMarble_, progFx_, progText_, progShape_, progPost_}) glDeleteProgram(p);
    for (GLuint v : {marbleVao_, fxVao_, textVao_, shapeVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, marbleVbo_, fxVbo_, textVbo_, shapeVbo_}) glDeleteBuffers(1, &b);
    if (landTex_) glDeleteTextures(1, &landTex_);
    for (Font* f : {&fontLight_, &fontMedium_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// =====================================================================================================
// The landscape
// =====================================================================================================

// Per song: low and flat under the well, rising toward the edges (one side tall, the other low, never mirrored),
// a few hills with hollows between them, sometimes terraced; kept low under the HUD's columns, and rising toward the
// lower left corner where the HUD's corner lines lie on it (no hollow there to collect marbles under the text).
void MarblesUniverse::buildLand() {
    land_.assign(LAND_N, -10.8f);
    Rng r(seed_ ^ 0x4D41524B4C414E44ull);
    const bool tallRight = r.chance(0.5f);
    struct Bump { float x, a, w; };
    std::vector<Bump> bumps;
    float rise[2];
    for (int si = 0; si < 2; si++) {
        const float side = si ? 1.f : -1.f;
        const bool tall = (si == 1) == tallRight;
        rise[si] = tall ? r.range(5.f, 7.5f) : r.range(2.4f, 3.8f);
        const int n = r.irange(2, 3);
        for (int i = 0; i < n; i++) bumps.push_back({side * r.range(9.f, 21.f), r.range(0.9f, 2.4f) * (tall ? 1.2f : 0.85f), r.range(1.0f, 2.2f)});
        bumps.push_back({side * r.range(10.f, 17.f), -r.range(0.8f, 1.6f), r.range(1.4f, 2.4f)});
    }
    const float bank[2] = {r.range(0.8f, 1.4f), r.range(0.8f, 1.4f)};
    const bool terrace = r.chance(0.45f);
    const float q = r.range(0.9f, 1.3f);
    const float ph1 = r.range(0.f, TAU), ph2 = r.range(0.f, TAU);
    for (int i = 0; i < LAND_N; i++) {
        const float x = -LAND_X + 2.f * LAND_X * i / (LAND_N - 1);
        const float a = std::fabs(x);
        const int si = x > 0.f ? 1 : 0;
        float h = -10.8f + rise[si] * std::pow(smoothstepf(6.5f, 24.f, a), 1.3f);
        for (const Bump& b : bumps) h += b.a * std::exp(-((x - b.x) / b.w) * ((x - b.x) / b.w)) * smoothstepf(6.5f, 9.f, a);
        // a low bank along the well, so that the hollows lie away from it
        h += bank[si] * std::exp(-((a - 6.9f) / 1.1f) * ((a - 6.9f) / 1.1f));
        h += (0.3f * std::sin(x * 0.55f + ph1) + 0.15f * std::sin(x * 1.3f + ph2)) * smoothstepf(7.f, 10.f, a);
        // the lower left corner rises (the HUD's corner lines lie there)
        if (x < -13.f) h = std::max(h, lerpf(-10.8f, -8.6f, smoothstepf(13.f, 16.5f, a)));
        // under the HUD columns the land stays low (the score's column on the left, the gauge on the right)
        const float cap = (x < 0.f ? -9.4f : -8.6f) + 30.f * smoothstepf(12.f, 14.5f, a);
        h = smin(h, cap, 0.6f);
        if (terrace) {
            const float t = (h + 20.f) / q;
            const float fl = std::floor(t);
            h = q * (fl + smoothstepf(0.4f, 1.f, t - fl)) - 20.f;
        }
        // flat under the well
        h = lerpf(-10.9f, h, smoothstepf(5.4f, 6.4f, a));
        land_[i] = h;
    }
    // no hollow in the lower left corner: the land only rises toward the edge
    for (int i = LAND_N - 1; i >= 0; i--) {
        const float x = -LAND_X + 2.f * LAND_X * i / (LAND_N - 1);
        if (x < -13.f && i + 1 < LAND_N) land_[i] = std::max(land_[i], land_[i + 1] + 0.002f);
    }
    if (landTex_) {
        glBindTexture(GL_TEXTURE_2D, landTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, LAND_N, 1, 0, GL_RED, GL_FLOAT, land_.data());
    }
}

float MarblesUniverse::landAt(float x) const {
    if (land_.empty()) return -10.8f;
    const float u = std::clamp((x + LAND_X) / (2.f * LAND_X), 0.f, 1.f) * (LAND_N - 1);
    const int i = std::min((int)u, LAND_N - 2);
    return lerpf(land_[i], land_[i + 1], u - i);
}

float MarblesUniverse::slopeAt(float x) const {
    const float e = 0.08f;
    return (landAt(x + e) - landAt(x - e)) / (2.f * e);
}

bool MarblesUniverse::inKeepOut(vec2 p, float r) const {
    for (const KeepOut& k : keepOut_)
        if (p.x + r > k.x0 && p.x - r < k.x1 && p.y + r > k.y0 && p.y - r < k.y1) return true;
    return false;
}

// =====================================================================================================
// Song, level
// =====================================================================================================

// The place from the song: bright major songs by day (PORCELAIN, NOON), warm or mild ones at dusk or in the sea
// glass mist, minor songs at night (MIDNIGHT, OBSIDIAN, SEA GLASS); the key's hue picks between them.
int MarblesUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    int v;
    if (!t.hasSong) v = (int)(h * VARIANT_COUNT) % VARIANT_COUNT;
    else if (t.minor) v = t.brightness < 0.42f ? (h < 0.5f ? 4 : 0) : (h < 0.5f ? 3 : 0);
    else v = t.brightness > 0.6f ? (h < 0.5f ? 2 : 5) : (h < 0.5f ? 1 : 3);
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void MarblesUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    seedF_ = (float)(look.seed % 1000) * 0.0137f;
    rng_ = Rng(look.seed ^ 0x4D4Bull);
    dropShape_ = (int)(look.seed % 6);
    // a new landscape, a few marbles already lying in its hollows
    buildLand();
    marbles_.clear();
    waves_.clear();
    glints_.clear();
    needSeed_ = true;
    if (seconds <= 0.f) palSet_ = false;
}

void MarblesUniverse::setLevel(int level, float energy, float seconds, float impact) {
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
    lvDur_ = std::clamp(seconds, 1.f, 5.f);
}

// The drop's first downbeat: its hit fires as soon as the section is known to be a drop.
void MarblesUniverse::drop(float strength) {
    (void)strength;
    dropPending_ = 2.5f;
}

// A new section: a few marbles catch the light.
void MarblesUniverse::refresh(float strength) {
    int n = 2 + (int)(strength * 5.f);
    for (int k = 0; k < 24 && n > 0 && !marbles_.empty(); k++) {
        Marble& m = marbles_[rng_.irange(0, (int)marbles_.size() - 1)];
        if (!m.asleep || m.retiring) continue;
        m.twinkle = 1.f;
        glint(v2(m.p.x - 0.36f * m.r, m.p.y + 0.42f * m.r), 0.45f, lerp(pal_.piece[m.type], vec3(1.f), 0.6f));
        n--;
    }
}

void MarblesUniverse::levelUp() { refresh(1.f); }

vec2 MarblesUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints MarblesUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    h.background = lerp(p.skyLow, p.skyTop, 0.5f);
    h.light = ugl::luma(h.background) > 0.28f;
    h.ink = lerp(p.piece[5], p.ink, 0.35f);
    h.accent = p.accent;
    h.styles = TS_INK | TS_LUMA | TS_TILES;
    return h;
}

// =====================================================================================================
// Game events
// =====================================================================================================

void MarblesUniverse::onEvent(const GameEvent& ev, const Game& game) {
    switch (ev.type) {
    case GameEvent::HardDrop:
        lastDrop_ = ev.count;
        break;
    case GameEvent::Lock: {
        if (ev.cells.empty()) break;
        // the marbles keep the look they fell with (the falling piece's seeds)
        const int id = nextId_++;
        const bool hard = lastDrop_ >= 2;
        const float k = std::min(1.f, 0.55f + lastDrop_ / 14.f);
        for (size_t i = 0; i < ev.cells.size(); i++) {
            const auto& c = ev.cells[i];
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            Slot& s = slots_[c.y][c.x];
            s.known = true;
            s.type = std::clamp(c.type, 0, 6);
            s.seed = hashf((float)id, (float)i);
            s.rot = hashf((float)id, i + 10.f) * TAU;
            // the impact: the marbles jolt in their tiles (hard), or settle (soft)
            s.bounce = hard ? 0.11f * k : 0.035f;
            s.bounceT = 0.f;
        }
        if (hard) {
            // the hit carries a little into the stack under it, and the glass of the bottom marbles flashes
            for (const auto& c : ev.cells) {
                const bool bottom = std::none_of(ev.cells.begin(), ev.cells.end(), [&](const GameEvent::CellInfo& o) {
                    return o.x == c.x && o.y == c.y + 1;
                });
                if (!bottom || c.y < 0 || c.y >= Game::H) continue;
                slots_[c.y][c.x].flare = 1.f;
                for (int d = 1; d <= 2 && c.y + d < Game::H; d++) {
                    Slot& u = slots_[c.y + d][c.x];
                    if (game.cell(c.x, c.y + d).type < 0) break;
                    u.bounce = std::max(u.bounce * std::exp(-u.bounceT * 9.f), (d == 1 ? 0.05f : 0.025f) * k);
                    u.bounceT = 0.f;
                }
            }
        }
        lastDrop_ = 0;
        break;
    }
    case GameEvent::Clear: {
        const bool tetris = ev.count >= 4;
        bool cleared[Game::H] = {};
        for (auto& c : ev.cells)
            if (c.y >= 0 && c.y < Game::H) cleared[c.y] = true;
        // the tiles pop and their marbles are thrown out into the landscape
        surf_.clear();
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const Slot& s = slots_[c.y][c.x];
            const int type = std::clamp(c.type, 0, 6);
            const vec3 p = layout::cellPos((float)c.x, c.y - game.rowOffset(c.y));
            pops_.push_back({p.x, p.y, 0.f, 0.f, type});
            launch(v2(p.x, p.y), type, s.known ? s.seed : hashf((float)c.x, (float)c.y), s.known ? s.rot : 0.f, 0.f, tetris ? 1.6f : 1.f);
        }
        // the rows above come down
        Slot next[Game::H][Game::W];
        int dst = Game::H - 1;
        for (int y = Game::H - 1; y >= 0; y--) {
            if (cleared[y]) continue;
            for (int x = 0; x < Game::W; x++) next[dst][x] = slots_[y][x];
            dst--;
        }
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) slots_[y][x] = next[y][x];
        // a Tetris: a wave of light runs out from the well through every heap
        if (tetris) startWave(0, 1.3f, 3);
        break;
    }
    case GameEvent::TopOut: {
        // game over: every marble leaves its tile, row by row from the top, thrown out into the landscape
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        surf_.clear();
        for (auto& c : ev.cells) {
            if (c.y < Game::HIDDEN || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const Slot& s = slots_[c.y][c.x];
            const int type = std::clamp(c.type, 0, 6);
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            const float delay = 0.25f + (c.y - top) * 0.06f + std::fabs(c.x - 4.5f) * 0.012f;
            pops_.push_back({p.x, p.y, 0.f, delay, type});
            launch(v2(p.x, p.y), type, s.known ? s.seed : hashf((float)c.x, (float)c.y), s.known ? s.rot : 0.f, delay, 1.2f);
        }
        for (auto& row : slots_)
            for (auto& c : row) c = Slot{};
        overT_ = 0.f;
        break;
    }
    default: break;
    }
}

// The stack's marbles follow the board: cells the universe has not seen land (a board filled for a review) get a
// look of their own.
void MarblesUniverse::syncSlots(const Game& g) {
    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            Slot& s = slots_[y][x];
            if (c.type < 0) {
                if (s.known) s = Slot{};
                continue;
            }
            if (!s.known || s.type != c.type) {
                s = Slot{};
                s.known = true;
                s.type = c.type;
                s.seed = hashf((float)x * 3.1f + c.type, (float)y * 7.f);
                s.rot = hashf((float)y, (float)x * 5.f + c.type) * TAU;
            }
        }
}

float MarblesUniverse::bounceOf(const Slot& s) const { return jolt(s.bounce, s.bounceT); }

// =====================================================================================================
// The marbles in the world
// =====================================================================================================

// Where a marble thrown from (x0, y0) lands: low in the landscape on its side of the well (the hollows fill first),
// never in the well, never where its heap would reach the HUD's text.
bool MarblesUniverse::pickTarget(float x0, float y0, float r, vec2& out) {
    (void)y0;
    const float bin = 0.35f;
    const float X = viewH_.x - 0.5f;
    const int nb = (int)(2.f * X / bin);
    if ((int)surf_.size() != nb) {
        surf_.assign(nb, 0.f);
        for (int i = 0; i < nb; i++) surf_[i] = landAt(-X + (i + 0.5f) * bin);
        for (const Marble& m : marbles_) {
            if (m.inWell || m.retiring || m.delay > 0.f) continue;
            const int i0 = std::max(0, (int)((m.p.x - m.r + X) / bin)), i1 = std::min(nb - 1, (int)((m.p.x + m.r + X) / bin));
            for (int i = i0; i <= i1; i++) surf_[i] = std::max(surf_[i], m.p.y + m.r * 0.9f);
        }
    }
    const float side = std::fabs(x0) < 0.6f ? (rng_.chance(0.5f) ? -1.f : 1.f) : (x0 < 0.f ? -1.f : 1.f);
    int best = -1;
    float bestS = 1e9f;
    for (int i = 0; i < nb; i++) {
        const float cx = -X + (i + 0.5f) * bin;
        if (cx * side < 6.3f) continue;
        const float y = surf_[i] + r;
        if (y > 2.f) continue;
        if (inKeepOut(v2(cx, y + r), r * 1.5f)) continue;
        const float s = y + rng_.range(0.f, 1.4f);
        if (s < bestS) { bestS = s; best = i; }
    }
    if (best < 0) return false;
    out = v2(-X + (best + 0.5f) * bin + rng_.range(-0.1f, 0.1f), surf_[best] + r);
    // what this marble will add to the heap there, so that the next ones spread
    for (int d = -2; d <= 2; d++)
        if (best + d >= 0 && best + d < nb) surf_[best + d] += (d == 0 ? 0.5f : (std::abs(d) == 1 ? 0.3f : 0.1f)) * r;
    return true;
}

void MarblesUniverse::launch(vec2 from, int type, float seed, float rot, float delay, float vigor) {
    Marble m;
    m.p = from;
    m.type = type;
    m.seed = seed;
    m.rot = rot;
    m.delay = delay;
    m.r = rng_.range(0.235f, 0.295f);
    m.r0 = TILE_R;
    m.inWell = true;
    vec2 tgt;
    if (!pickTarget(from.x, from.y, m.r, tgt)) tgt = v2((from.x < 0.f ? -1.f : 1.f) * (viewH_.x + 4.f), viewC_.y - viewH_.y - 3.f);
    const float apex = std::max(from.y, tgt.y) + rng_.range(1.8f, 3.4f) * vigor;
    const float vy = std::sqrt(2.f * GRAV * (apex - from.y));
    const float T = vy / GRAV + std::sqrt(2.f * std::max(0.f, apex - tgt.y) / GRAV);
    m.v = v2((tgt.x - from.x) / std::max(T, 0.2f), vy);
    m.spin = -m.v.x / m.r * 0.4f;
    marbles_.push_back(m);
}

void MarblesUniverse::wakeAround(vec2 p, float rad) {
    for (Marble& m : marbles_)
        if (m.asleep && len2(m.p - p) < rad) { m.asleep = false; m.sleepT = 0.f; }
}

// One step of the marbles' physics: gravity, the land (a heightfield), the well's pane and the picture's edges as
// walls, and the marbles against each other (a hashed grid). Resting marbles fall asleep and hold still; a hard hit
// wakes them.
void MarblesUniverse::stepPhysics(float h) {
    const float wallX = viewH_.x - 0.05f;
    const int n = (int)marbles_.size();
    for (Marble& m : marbles_) {
        if (m.delay > 0.f) { m.delay -= h; continue; }
        m.age += h;
        if (m.asleep) continue;
        m.v.y -= GRAV * h;
        m.p = m.p + m.v * h;
        m.rot += m.spin * h;
        if (m.inWell && std::fabs(m.p.x) > WELL_HALF + m.r + 0.05f) m.inWell = false;
    }
    gridHead_.assign(gridW_ * gridH_, -1);
    gridNext_.assign(n, -1);
    auto cellOf = [&](vec2 p, int& cx, int& cy) {
        cx = std::clamp((int)((p.x - gridX0_) / GRID), 0, gridW_ - 1);
        cy = std::clamp((int)((p.y - gridY0_) / GRID), 0, gridH_ - 1);
    };
    for (int i = 0; i < n; i++) {
        const Marble& m = marbles_[i];
        if (m.delay > 0.f || m.inWell) continue;
        int cx, cy;
        cellOf(m.p, cx, cy);
        gridNext_[i] = gridHead_[cy * gridW_ + cx];
        gridHead_[cy * gridW_ + cx] = i;
    }
    static std::vector<char> contact;
    contact.assign(n, 0);
    for (int i = 0; i < n; i++) {
        Marble& a = marbles_[i];
        if (a.delay > 0.f || a.inWell || a.asleep) continue;
        // the land
        {
            const float gy = landAt(a.p.x), s = slopeAt(a.p.x);
            const float inv = 1.f / std::sqrt(1.f + s * s);
            const vec2 nrm = v2(-s * inv, inv);
            const float d = (a.p.y - gy) * inv - a.r;
            if (d < 0.f) {
                a.p = a.p + nrm * (-d);
                const float vn = dot2(a.v, nrm);
                if (vn < 0.f) a.v = a.v - nrm * (vn * 1.3f);
                contact[i] = 1;
            }
        }
        // the well's pane and the edges of the picture
        if (std::fabs(a.p.x) < WELL_HALF + a.r && a.p.y < WELL_TOP) {
            const float sx = a.p.x < 0.f ? -1.f : 1.f;
            a.p.x = sx * (WELL_HALF + a.r);
            if (a.v.x * sx < 0.f) a.v.x = -a.v.x * 0.3f;
            contact[i] = 1;
        }
        if (a.p.x < -wallX + a.r) { a.p.x = -wallX + a.r; if (a.v.x < 0.f) a.v.x = -a.v.x * 0.3f; }
        if (a.p.x > wallX - a.r) { a.p.x = wallX - a.r; if (a.v.x > 0.f) a.v.x = -a.v.x * 0.3f; }
        // the others
        int cx, cy;
        cellOf(a.p, cx, cy);
        for (int gy = std::max(0, cy - 1); gy <= std::min(gridH_ - 1, cy + 1); gy++)
            for (int gx = std::max(0, cx - 1); gx <= std::min(gridW_ - 1, cx + 1); gx++)
                for (int j = gridHead_[gy * gridW_ + gx]; j >= 0; j = gridNext_[j]) {
                    if (j == i) continue;
                    Marble& b = marbles_[j];
                    const vec2 dv = a.p - b.p;
                    const float rr = a.r + b.r;
                    const float d2 = dv.x * dv.x + dv.y * dv.y;
                    if (d2 >= rr * rr || d2 < 1e-10f) continue;
                    const float d = std::sqrt(d2);
                    const vec2 nrm = dv * (1.f / d);
                    const float ov = rr - d;
                    contact[i] = 1;
                    if (b.asleep) {
                        const float rel = dot2(a.v, nrm);
                        if (-rel > 3.5f) { b.asleep = false; b.sleepT = 0.f; }
                        a.p = a.p + nrm * ov;
                        if (rel < 0.f) a.v = a.v - nrm * (rel * 1.15f);
                    } else {
                        if (j < i) continue; // the pair is handled once
                        contact[j] = 1;
                        a.p = a.p + nrm * (ov * 0.5f);
                        b.p = b.p - nrm * (ov * 0.5f);
                        const float rel = dot2(a.v - b.v, nrm);
                        if (rel < 0.f) {
                            const float jv = -1.15f * rel * 0.5f;
                            a.v = a.v + nrm * jv;
                            b.v = b.v - nrm * jv;
                        }
                    }
                }
    }
    // rolling: contacts slow the marbles down; a marble that has rested for a moment falls asleep
    for (int i = 0; i < n; i++) {
        Marble& a = marbles_[i];
        if (a.delay > 0.f || a.inWell || a.asleep) continue;
        if (contact[i]) {
            const float k = std::exp(-1.8f * h);
            a.v = a.v * k;
            a.spin = lerpf(a.spin, -a.v.x / a.r, 1.f - std::exp(-12.f * h));
            const float sp = len2(a.v);
            if (sp < 0.25f) {
                a.sleepT += h;
                if (a.sleepT > 0.45f) { a.asleep = true; a.v = v2(0, 0); a.spin = 0.f; }
            } else a.sleepT = 0.f;
        } else a.sleepT = 0.f;
    }
}

void MarblesUniverse::simulateMarbles(float dt) {
    if (dt <= 0.f) return;
    const int steps = std::clamp((int)std::ceil(dt * 120.f), 1, 8);
    for (int s = 0; s < steps; s++) stepPhysics(dt / steps);
    // retire: marbles resting where the HUD's text is, the oldest ones past the cap; gone: out of the picture
    int live = 0;
    for (const Marble& m : marbles_) live += !m.retiring;
    if (live > MAX_MARBLES) {
        int excess = live - MAX_MARBLES;
        std::vector<int> idx;
        for (int i = 0; i < (int)marbles_.size(); i++)
            if (!marbles_[i].retiring && marbles_[i].asleep) idx.push_back(i);
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return marbles_[a].age > marbles_[b].age; });
        for (int i = 0; i < (int)idx.size() && excess > 0; i++, excess--) marbles_[idx[i]].retiring = true;
    }
    std::vector<vec2> gone;
    for (Marble& m : marbles_) {
        if (m.asleep && !m.retiring && inKeepOut(m.p, m.r * 0.6f)) m.retiring = true;
        if (m.retiring) {
            m.fade -= dt / 1.4f;
            if (m.fade <= 0.f) gone.push_back(m.p);
        }
        if (m.p.y < viewC_.y - viewH_.y - 3.f) m.fade = -1.f;
    }
    marbles_.erase(std::remove_if(marbles_.begin(), marbles_.end(), [](const Marble& m) { return m.fade <= 0.f; }), marbles_.end());
    for (const vec2& p : gone) wakeAround(p, 1.4f);
}

// The landscape's first marbles: dropped one after the other over both sides, settled before the first frame.
void MarblesUniverse::seedMarbles(int n) {
    Rng r(seed_ ^ 0x5EEDull);
    const float X = viewH_.x - 0.6f;
    for (int i = 0; i < n; i++) {
        Marble m;
        const float side = r.chance(0.5f) ? -1.f : 1.f;
        const float x = side * r.range(6.4f, X);
        m.p = v2(x, landAt(x) + r.range(1.f, 3.f));
        m.type = r.irange(0, 6);
        m.seed = r.uniform();
        m.rot = r.range(0.f, TAU);
        m.r = r.range(0.235f, 0.295f);
        m.r0 = m.r;
        m.delay = i * 0.012f;
        m.age = 10.f;
        if (inKeepOut(v2(x, landAt(x) + 0.6f), 0.8f)) continue;
        marbles_.push_back(m);
    }
    const float h = 1.f / 120.f;
    const int steps = (int)((n * 0.012f + 6.f) * 120.f);
    for (int s = 0; s < steps; s++) stepPhysics(h);
    for (Marble& m : marbles_) {
        m.asleep = true;
        m.v = v2(0, 0);
        m.spin = 0.f;
        m.age = 10.f + r.range(0.f, 5.f);
    }
    marbles_.erase(std::remove_if(marbles_.begin(), marbles_.end(),
                                  [&](const Marble& m) { return inKeepOut(m.p, m.r * 0.6f) || m.p.y < viewC_.y - viewH_.y - 1.f; }),
                   marbles_.end());
}

// =====================================================================================================
// Light: waves, glints
// =====================================================================================================

void MarblesUniverse::glint(vec2 p, float size, const vec3& col) {
    if (glints_.size() > 140) return;
    glints_.push_back({p, 0.f, rng_.range(0.3f, 0.45f), size * rng_.range(0.8f, 1.2f), col});
}

void MarblesUniverse::startWave(int shape, float gain, int bands) {
    Wave w;
    w.t = 0.f;
    w.shape = shape;
    w.sign = (waveIndex_++ % 2) ? -1.f : 1.f;
    w.gain = gain;
    w.bands = bands;
    w.o = v2(w.sign * 12.f, -9.f);
    switch (shape) {
    case 0: w.speed = 24.f; w.width = 1.3f; break;   // out from the well
    case 1: w.speed = 24.f; w.width = 1.3f; break;   // in from the edges
    case 2: w.speed = 30.f; w.width = 1.6f; break;   // a sweep across
    case 3: w.speed = 8.f; w.width = 0.55f; break;   // rising from the hollows
    case 4: w.speed = 13.f; w.width = 0.9f; break;   // rings from a heap
    default: w.speed = 26.f; w.width = 1.2f; break;  // diagonal bands
    }
    if (shape == 4) {
        // the rings start in a heap: a marble lying on the wave's side
        std::vector<int> c;
        for (int i = 0; i < (int)marbles_.size(); i++)
            if (marbles_[i].asleep && marbles_[i].p.x * w.sign > 0.f) c.push_back(i);
        if (!c.empty()) w.o = marbles_[c[rng_.irange(0, (int)c.size() - 1)]].p;
    }
    waves_.push_back(w);
}

float MarblesUniverse::waveCoord(const Wave& w, vec2 p) const {
    const float X = viewH_.x;
    switch (w.shape) {
    case 0: return std::fabs(p.x) - WELL_HALF;
    case 1: return X - std::fabs(p.x);
    case 2: return w.sign > 0.f ? p.x + X : X - p.x;
    case 3: return p.y + 12.5f;
    case 4: return len2(p - w.o);
    default: return 0.8f * (w.sign * p.x + X) + 0.6f * (6.f - p.y);
    }
}

// =====================================================================================================
// Simulation
// =====================================================================================================

void MarblesUniverse::simulate(const Game& game, const MusicState& music, float dt) {
    time_ += dt;
    hasBeat_ = music.hasSong;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const bool over = overT_ >= 0.f;
    const float world = 1.f - smoothstepf(0.f, 0.6f, bridge_.amount);

    // Is this peak a drop? The HUD's section says it when there is one; else the song's drive.
    const float dropish = sectionKnown_ ? (sectionDrop_ ? 1.f : 0.f) : smoothstepf(1.27f, 1.42f, music.speed);
    const bool peak = level_ == 2;
    const float driveT = over ? 0.f : (peak ? dropish : 0.f);
    if (dropPending_ > 0.f) {
        dropPending_ -= dt;
        if (peak && dropish > 0.5f) {
            // the hit: the drop's shape of wave, a first big wave and one swell of the land's glow
            dropPending_ = 0.f;
            dropShape_ = (dropShape_ + 1 + dropCount_ * 2) % 6;
            dropCount_++;
            drive_ = std::max(drive_, 0.6f);
            swell_ = 1.f;
            startWave(dropShape_, 1.5f, 3);
            lastHalf_ = std::floor(music.beat / 2.0 + 1e-4);
        }
    }
    drive_ = approach(drive_, driveT, driveT > drive_ ? 1.5f : 0.5f, dt);
    glow_ = approach(glow_, 0.03f * wm + 0.07f * wp * (1.f - drive_) + 0.11f * drive_, 1.2f, dt);
    swell_ = approach(swell_, 0.f, 0.5f, dt);

    // The beat grid: waves every two beats through a drop (a stronger, double one on the bars); at mid a marble
    // rolls off a heap now and then and one catches the light, on the bars.
    const double beat = music.hasSong ? music.beat : time_ * 2.0;
    const double half = std::floor(beat / 2.0 + 1e-4);
    if (lastHalf_ < 0.0 || half < lastHalf_ - 2.0) lastHalf_ = half;
    if (half > lastHalf_) {
        lastHalf_ = half;
        if (drive_ > 0.5f && !over && world > 0.5f) {
            const bool bar = ((long long)half % 2) == 0;
            startWave(dropShape_, bar ? 1.f : 0.7f, bar ? 2 : 1);
        }
    }
    const double bar = std::floor(beat / 4.0 + 1e-4);
    if (lastBar_ < 0.0 || bar < lastBar_ - 2.0) lastBar_ = bar;
    if (bar > lastBar_) {
        lastBar_ = bar;
        if (wm > 0.5f && drive_ < 0.3f && !over && !marbles_.empty()) {
            for (int k = 0; k < 12; k++) {
                Marble& m = marbles_[rng_.irange(0, (int)marbles_.size() - 1)];
                if (!m.asleep || m.retiring) continue;
                m.twinkle = 1.f;
                glint(v2(m.p.x - 0.36f * m.r, m.p.y + 0.42f * m.r), 0.5f, lerp(pal_.piece[m.type], vec3(1.f), 0.6f));
                break;
            }
            for (int k = 0; k < 12; k++) {
                Marble& m = marbles_[rng_.irange(0, (int)marbles_.size() - 1)];
                if (!m.asleep || m.retiring || std::fabs(slopeAt(m.p.x)) < 0.05f) continue;
                m.asleep = false;
                m.v = v2((slopeAt(m.p.x) > 0.f ? -1.f : 1.f) * rng_.range(0.8f, 1.5f), 0.6f);
                break;
            }
        }
    }
    // Kicks at the peak: a few marbles glint (a chorus); the lit marbles on top of the heaps hop (a drop).
    sinceKick_ += dt;
    const float kick = std::max(music.kick, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    if (kick > 0.5f && prevKick_ <= 0.5f && sinceKick_ > 0.4f && wp > 0.5f && world > 0.5f && !over && !marbles_.empty()) {
        sinceKick_ = 0.f;
        if (drive_ > 0.5f) {
            // on top of a heap: nothing resting right above it
            int hops = 0;
            for (int k = 0; k < 60 && hops < 9; k++) {
                const int i = rng_.irange(0, (int)marbles_.size() - 1);
                Marble& m = marbles_[i];
                if (!m.asleep || m.retiring || m.light < 0.25f) continue;
                bool covered = false;
                for (const Marble& o : marbles_)
                    if (&o != &m && o.p.y > m.p.y + 0.2f && std::fabs(o.p.x - m.p.x) < m.r + o.r - 0.05f && o.p.y - m.p.y < 0.75f) { covered = true; break; }
                if (covered) continue;
                m.asleep = false;
                m.sleepT = 0.f;
                m.v = v2(rng_.range(-0.4f, 0.4f), rng_.range(2.f, 3.f));
                hops++;
            }
        } else {
            for (int k = 0, n = 0; k < 30 && n < 4; k++) {
                Marble& m = marbles_[rng_.irange(0, (int)marbles_.size() - 1)];
                if (!m.asleep || m.retiring) continue;
                m.twinkle = 1.f;
                glint(v2(m.p.x - 0.36f * m.r, m.p.y + 0.42f * m.r), 0.5f, lerp(pal_.piece[m.type], vec3(1.f), 0.6f));
                n++;
            }
        }
    }
    prevKick_ = kick;

    // The waves move on (real time); each marble takes the light of the fronts passing through it.
    for (Wave& w : waves_) w.t += dt;
    waves_.erase(std::remove_if(waves_.begin(), waves_.end(),
                                [&](const Wave& w) { return w.t * w.speed > (w.shape == 3 ? 30.f : 70.f) + 6.f * w.width; }),
                 waves_.end());
    for (Marble& m : marbles_) {
        m.prevLight = m.light;
        float L = 0.f;
        if (!m.inWell && m.delay <= 0.f)
            for (const Wave& w : waves_) {
                const float d = w.t * w.speed - waveCoord(w, m.p);
                auto band = [&](float e) { return e <= 0.f ? 0.f : std::exp(-e / w.width) * (1.f - std::exp(-e / 0.14f)); };
                float b = band(d);
                if (w.bands >= 2) b += 0.7f * band(d - 1.5f);
                if (w.bands >= 3) b += 0.45f * band(d - 3.f);
                L += w.gain * b;
            }
        m.light = std::min(L, 1.6f);
        if (m.light >= 0.55f && m.prevLight < 0.55f && rng_.chance(0.55f))
            glint(v2(m.p.x - 0.36f * m.r, m.p.y + 0.42f * m.r), 0.42f, lerp(pal_.wave, pal_.piece[m.type], 0.25f));
        m.twinkle = approach(m.twinkle, 0.f, 3.f, dt);
    }
    for (Glint& g : glints_) g.t += dt;
    glints_.erase(std::remove_if(glints_.begin(), glints_.end(), [](const Glint& g) { return g.t > g.life; }), glints_.end());

    simulateMarbles(dt);

    for (Pop& p : pops_) p.t += dt;
    pops_.erase(std::remove_if(pops_.begin(), pops_.end(), [](const Pop& p) { return p.t > p.delay + POP; }), pops_.end());
    for (auto& row : slots_)
        for (Slot& s : row) {
            s.bounceT += dt;
            s.flare = approach(s.flare, 0.f, 7.f, dt);
        }
    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
}

// =====================================================================================================
// Drawing
// =====================================================================================================

void MarblesUniverse::put(float x, float y, float half, float radius, const vec3& col, float alpha, float rot, float glow,
                          float seed, int kind, float mx, float my, float flare, float haze) {
    if (alpha <= 0.003f) return;
    inst_.push_back({{x, y, half, radius}, {col.x, col.y, col.z, alpha}, {rot, glow, seed, (float)kind}, {mx, my, flare, haze}});
}

void MarblesUniverse::putFx(float x, float y, float size, float rot, const vec3& col, float alpha, int kind) {
    if (alpha <= 0.003f) return;
    fx_.push_back({{x, y, size, rot}, {col.x, col.y, col.z, alpha}, {(float)kind, 0.f, 0.f, 0.f}});
}

void MarblesUniverse::flush() {
    if (inst_.empty()) return;
    const float bb = smoothstepf(0.3f, 0.95f, bridge_.amount);
    glUseProgram(progMarble_);
    setMat(progMarble_, "uVP", vp_);
    set1f(progMarble_, "uPx", px_);
    set1f(progMarble_, "uTileTint", pal_.tileTint);
    set1f(progMarble_, "uTileAlpha", pal_.tileAlpha);
    set1f(progMarble_, "uBridge", bb);
    set1f(progMarble_, "uLightGround", pal_.light);
    set3f(progMarble_, "uTileCol", pal_.tile);
    set3f(progMarble_, "uLightCol", lerp(vec3(1.f, 0.98f, 0.95f), pal_.wave, 0.2f) * (1.f - 0.35f * slow_));
    set3f(progMarble_, "uHazeCol", lerp(pal_.haze, pal_.night, 0.7f * slow_));
    set3f(progMarble_, "uBridgeInk", bridge_.ink);
    set3f(progMarble_, "uPanelEdge", pal_.panelEdge);
    glBindVertexArray(marbleVao_);
    glBindBuffer(GL_ARRAY_BUFFER, marbleVbo_);
    glBufferData(GL_ARRAY_BUFFER, inst_.size() * sizeof(MarbleInst), inst_.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)inst_.size());
    inst_.clear();
}

void MarblesUniverse::flushFx() {
    if (fx_.empty()) return;
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(progFx_);
    setMat(progFx_, "uVP", vp_);
    glBindVertexArray(fxVao_);
    glBindBuffer(GL_ARRAY_BUFFER, fxVbo_);
    glBufferData(GL_ARRAY_BUFFER, fx_.size() * sizeof(FxInst), fx_.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)fx_.size());
    fx_.clear();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// The marbles lying in the landscape (and the ones flying out there once they have left the well), their light.
void MarblesUniverse::drawWorld(float world) {
    if (world <= 0.01f) return;
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float base = 0.03f * wm + 0.07f * wp * (1.f - drive_) + 0.1f * drive_ + 0.35f * swell_ * drive_ + 0.32f * slow_;
    const float haze = 0.12f * (1.f - 0.5f * pal_.light);
    for (const Marble& m : marbles_) {
        if (m.delay > 0.f || m.inWell) continue;
        const float rr = lerpf(m.r0, m.r, smoothstepf(0.f, 0.45f, m.age));
        const float lit = m.light + m.twinkle;
        const float a = saturate(m.fade) * world;
        put(m.p.x, m.p.y, 0.f, rr, pal_.piece[m.type], a, m.rot, base + 1.5f * lit, m.seed, 0, 0.f, 0.f, 1.0f * lit,
            haze * (1.f - saturate(lit)));
    }
    flush();
    // their light: a soft halo around each lit marble, glints on the fronts
    for (const Marble& m : marbles_) {
        if (m.delay > 0.f || m.inWell) continue;
        const float lit = m.light + m.twinkle * 0.8f + (slow_ > 0.01f ? 0.25f * slow_ : 0.f);
        if (lit < 0.03f) continue;
        putFx(m.p.x, m.p.y, m.r * 2.4f, 0.f, lerp(pal_.piece[m.type], pal_.wave, 0.3f), 0.3f * lit * pal_.halo * saturate(m.fade) * world, 0);
    }
    for (const Glint& g : glints_) {
        const float k = g.t / g.life;
        const float a = smoothstepf(0.f, 0.15f, k) * (1.f - smoothstepf(0.4f, 1.f, k));
        putFx(g.p.x, g.p.y, g.size * (0.6f + 0.6f * k), 0.25f + 0.3f * k, g.col, 1.4f * a * world * (0.5f + 0.5f * pal_.halo), 1);
    }
    flushFx();
}

void MarblesUniverse::drawBoard(const Game& g, float world) {
    const float bb = smoothstepf(0.3f, 0.95f, bridge_.amount);
    const float glow = 0.3f * slow_;
    // The well's pane: one even tone of smoked (or frosted) glass with a hairline edge.
    put(0.f, 0.f, 5.12f, 10.08f, lerp(pal_.panel, bridge_.color, bb), pal_.panelAlpha * (1.f - 0.5f * bb), 0.f, 0.f, 0.f, 4);
    // The ghost: the tiles' outline.
    if (g.hasPiece() && !g.over()) {
        const Piece& p = g.piece();
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (auto& c : gc) {
                if (c[1] < Game::HIDDEN) continue;
                const vec3 w = layout::cellPos((float)c[0], (float)c[1]);
                put(w.x, w.y, TILE_H, TILE_R, lerp(pal_.piece[std::clamp(p.type, 0, 6)], pal_.ink, 0.25f), 0.5f, 0.f, 0.f, 0.f, 2);
            }
        }
    }
    // The stack.
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const Slot& s = slots_[y][x];
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            put(p.x, p.y, TILE_H, TILE_R, pal_.piece[std::clamp((int)c.type, 0, 6)], 1.f, s.rot, glow, s.seed, 1, 0.f, bounceOf(s), s.flare);
        }
    // Tiles popping (their marble is gone), or waiting for their turn at game over.
    for (const Pop& p : pops_) {
        if (p.t < p.delay) {
            put(p.x, p.y, TILE_H, TILE_R, pal_.piece[p.type], 1.f, 0.f, glow, hashf(p.x, p.y), 1);
            continue;
        }
        const float k = saturate((p.t - p.delay) / POP);
        put(p.x, p.y, TILE_H * (1.f + 0.16f * k), TILE_R, pal_.piece[p.type], (1.f - k) * (1.f - k), 0.f, 0.f, 0.f, 3);
    }
    // The falling piece.
    if (g.hasPiece() && !g.over()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        const int type = std::clamp(p.type, 0, 6);
        for (int i = 0; i < 4; i++) {
            if (cells[i][1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(cells[i][0] + dx, cells[i][1] + dy);
            put(w.x, w.y, TILE_H, TILE_R, pal_.piece[type], 1.f, hashf((float)nextId_, i + 10.f) * TAU, glow, hashf((float)nextId_, (float)i), 1);
        }
    }
    (void)world;
}

// =====================================================================================================
// Text
// =====================================================================================================

float MarblesUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
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

float MarblesUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
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

void MarblesUniverse::drawText(const Font& f, const std::vector<float>& q, const vec3& ink, float alpha, float bias) {
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

// The HUD in MARBLES' typography: Outfit Light for the song, the universe's name, the section and the celebrations;
// widely spaced small capitals in Outfit Medium for the labels, the numbers in Medium. Set straight on the sky or
// the land, pale on a dark ground, dark on a pale one.
void MarblesUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    if (!panels.empty()) {
        std::vector<Vert> v;
        auto quad = [&](float x0, float y0, float x1, float y1, const vec3& c, float a) {
            const Vert q[6] = {{x0, y0, c.x, c.y, c.z, a}, {x1, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a},
                               {x0, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a}, {x0, y1, c.x, c.y, c.z, a}};
            v.insert(v.end(), q, q + 6);
        };
        for (const HudRect& r : panels) {
            const vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? pal_.accent : lerp(pal_.skyLow, pal_.skyTop, 0.5f);
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
    const vec3 darkInk = inkDark ? pal_.ink : ugl::toLinear(hex(0x1e242c));
    const vec3 lightInk = inkDark ? ugl::toLinear(hex(0xf2f4f6)) : pal_.ink;
    // the ground behind a point of the screen: the land or the sky there
    auto inkAt = [&](float px, float py) {
        const float bx = viewC_.x + (px / width_ * 2.f - 1.f) * viewH_.x;
        const float by = viewC_.y - (py / height_ * 2.f - 1.f) * viewH_.y;
        vec3 g;
        if (by < landAt(bx)) g = lerp(pal_.land, pal_.landDeep, smoothstepf(0.f, 7.f, landAt(bx) - by));
        else g = lerp(pal_.skyLow, pal_.skyTop, smoothstepf(0.f, 1.f, std::pow(saturate((by + 12.5f) / 25.f), 0.8f)));
        g = lerp(g, pal_.night, 0.85f * slow_);
        return ugl::luma(g) > 0.25f ? darkInk : lightInk;
    };
    std::vector<float> q;
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        vec3 ink = inkAt(h.x < 0.f ? width_ + h.x : h.x, h.y + base * 0.5f);
        vec3 accent = lerp(pal_.accent, ink, 0.2f);
        const float alpha = saturate(h.alpha);
        q.clear();
        auto place = [&](const Font& f, const std::string& text, float size, float track, float dy = 0.f) {
            const float w = textWidth(f, text, size, track);
            const float x = ugl::hudLeft(h, w, width_);
            ink = inkAt(x + w * 0.5f, h.y + dy + size * 0.6f);
            accent = ugl::luma(ink) > 0.3f ? lerp(pal_.accent, ink, 0.2f) : lerp(pal_.accent, ink, 0.35f);
            textQuads(f, text, x, h.y + dy, size, track, q);
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            // "Marbles" in the light weight, the variant's name in small spaced capitals after it.
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            name = titleCase(name);
            const float sz = base * 1.35f, vsz = base * 0.66f, vtr = vsz * 0.34f;
            const float wName = textWidth(fontLight_, name, sz, sz * 0.02f);
            const float wVar = var.empty() ? 0.f : textWidth(fontMedium_, var, vsz, vtr);
            const float gap = base * 0.8f;
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            textQuads(fontLight_, name, x, h.y - base * 0.1f, sz, sz * 0.02f, q);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            drawText(fontLight_, q, inkAt(x + wName * 0.5f, h.y + sz * 0.4f), a, 0.03f);
            if (!var.empty()) {
                q.clear();
                textQuads(fontMedium_, var, x + wName + gap, h.y + base * 0.2f, vsz, vtr, q);
                drawText(fontMedium_, q, inkAt(x + wName + gap + wVar * 0.5f, h.y + base * 0.2f + vsz * 0.5f), a * 0.85f, 0.02f);
            }
            continue;
        }
        case HUD_SECTION:
            place(fontLight_, titleCase(h.text), base * 1.3f, base * 0.03f);
            drawText(fontLight_, q, ink, saturate(0.2f + h.alpha * 1.9f), 0.03f);
            continue;
        case HUD_TITLE:
            place(fontLight_, h.text, base * 1.2f, base * 0.02f, -base * 0.1f);
            drawText(fontLight_, q, ink, saturate(h.alpha * 1.3f), 0.03f);
            continue;
        case HUD_INFO:
            place(fontMedium_, h.text, base * 0.72f, base * 0.16f);
            drawText(fontMedium_, q, ink, saturate(alpha * 1.2f) * 0.8f, 0.02f);
            continue;
        case HUD_LABEL:
            place(fontMedium_, h.text, base * 0.68f, base * 0.36f);
            drawText(fontMedium_, q, ink, saturate(alpha * 1.2f) * 0.72f, 0.02f);
            continue;
        case HUD_VALUE:
            place(fontLight_, h.text, base * 1.12f, base * 0.03f);
            drawText(fontLight_, q, ink, alpha, 0.04f);
            continue;
        case HUD_NOTICE:
            place(fontLight_, h.text, base * 1.1f, base * 0.06f);
            drawText(fontLight_, q, accent, alpha, 0.04f);
            continue;
        case HUD_KEY:
            place(fontMedium_, h.text, base * 0.85f, base * 0.08f);
            drawText(fontMedium_, q, ink, alpha, 0.02f);
            continue;
        case HUD_TOAST:
            place(fontLight_, titleCase(h.text), base * 1.45f, base * 0.04f, -base * 0.2f);
            drawText(fontLight_, q, ink, alpha, 0.04f);
            continue;
        case HUD_TOAST_SUB:
            place(fontMedium_, h.text, base * 0.8f, base * 0.2f);
            drawText(fontMedium_, q, accent, alpha * 0.9f, 0.02f);
            continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontLight_, titleCase(h.text), base * 1.25f, base * 0.03f, -base * 0.12f);
                drawText(fontLight_, q, ink, alpha, 0.04f);
            } else {
                place(fontMedium_, h.text, base * 0.82f, base * 0.12f);
                drawText(fontMedium_, q, h.accent ? accent : ink, alpha, 0.02f);
            }
            continue;
        default:
            place(fontMedium_, h.text, base * 0.8f, base * 0.1f);
            drawText(fontMedium_, q, h.accent ? accent : ink, alpha, 0.02f);
            continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void MarblesUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const Palette target = paletteOf(variant_);
        if (!palSet_) { pal_ = target; palSet_ = true; }
        else lerpPalette(pal_, target, 1.f - std::exp(-dt * 1.0f));
    }
    // Where the HUD's text lies (board space): marbles never rest there.
    keepOut_.clear();
    keepOut_.push_back({layout::LEFT_COLUMN[0] - 0.3f, layout::LEFT_COLUMN[2] + 0.3f, layout::LEFT_COLUMN[1] - 0.5f, layout::LEFT_COLUMN[3]});
    keepOut_.push_back({layout::RIGHT_COLUMN[0] - 0.3f, layout::RIGHT_COLUMN[2] + 0.3f, layout::RIGHT_COLUMN[1] - 0.5f, layout::RIGHT_COLUMN[3]});
    for (const HudText& h : f.hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        if (h.role == HUD_HELP || h.role == HUD_DIALOG || h.role == HUD_TOAST || h.role == HUD_TOAST_SUB) continue;
        const float base = h.scale * 12.f;
        const float w = (float)h.text.size() * base * (h.role == HUD_UNIVERSE ? 1.05f : 0.75f);
        const float x0 = ugl::hudLeft(h, w, width_), y0 = h.y - base * 0.3f, y1 = h.y + base * 1.6f;
        const float bx0 = viewC_.x + (x0 / width_ * 2.f - 1.f) * viewH_.x, bx1 = viewC_.x + ((x0 + w) / width_ * 2.f - 1.f) * viewH_.x;
        const float byTop = viewC_.y - (y0 / height_ * 2.f - 1.f) * viewH_.y, byBot = viewC_.y - (y1 / height_ * 2.f - 1.f) * viewH_.y;
        keepOut_.push_back({bx0 - 0.3f, bx1 + 0.3f, byBot - 0.2f, byTop + 0.2f});
    }
    // the section, from the HUD (a drop sends its waves, a chorus glimmers)
    sectionKnown_ = false;
    for (const HudText& h : f.hud)
        if (h.role == HUD_SECTION) {
            sectionKnown_ = true;
            sectionDrop_ = h.text == "DROP";
        }
    if (needSeed_) {
        needSeed_ = false;
        seedMarbles(90 + 40 * std::clamp(level_, 0, 2));
    }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 0.9f, dt);
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.5f : 0.f), 3.f, dt);
    if (overT_ >= 0.f) {
        if (!game.over()) overT_ = -1.f;
        else overT_ += dt;
    }
    if (!f.paused) {
        simulate(game, music, dt);
        syncSlots(game);
    }
    const float bridge = saturate(bridge_.amount);
    const float world = 1.f - smoothstepf(0.f, 0.6f, bridge);
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);

    sceneT_.bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    // ---- The sky and the landscape.
    {
        glUseProgram(progSky_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, landTex_);
        set1i(progSky_, "uLand", 0);
        set2f(progSky_, "uLandX", -LAND_X, LAND_X);
        set4f(progSky_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set3f(progSky_, "uSkyTop", pal_.skyTop);
        set3f(progSky_, "uSkyLow", pal_.skyLow);
        set3f(progSky_, "uRidge", pal_.ridge);
        set3f(progSky_, "uLandC", pal_.land);
        set3f(progSky_, "uLandDeep", pal_.landDeep);
        set3f(progSky_, "uRim", pal_.rim);
        set3f(progSky_, "uOrb", pal_.orb);
        set3f(progSky_, "uNight", pal_.night);
        set3f(progSky_, "uBridgeCol", bridge_.color);
        set3f(progSky_, "uGlowCol", lerp(pal_.accent, pal_.wave, drive_ * 0.5f));
        set4f(progSky_, "uOrbP", pal_.orbX, pal_.orbY, pal_.orbR, pal_.orbOn);
        set1f(progSky_, "uGlow", (glow_ + 0.18f * swell_) * world * (pal_.light > 0.5f ? 0.5f : 1.f));
        set1f(progSky_, "uSlow", slow_);
        set1f(progSky_, "uBridge", smoothstepf(0.05f, 0.85f, bridge));
        set1f(progSky_, "uSeed", seedF_);
        set1f(progSky_, "uPx", px_);
        set1f(progSky_, "uStars", pal_.stars);
        set1f(progSky_, "uRimAmt", (0.22f + 0.15f * wm + 0.2f * wp) * world);
        ugl::drawFullscreen();
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // ---- The marbles in the landscape.
    drawWorld(world);
    // ---- The well, the stack, the falling piece.
    drawBoard(game, world);
    flush();
    // ---- Marbles flying out of the well (in front: they have just left their tiles).
    for (const Marble& m : marbles_) {
        if (m.delay > 0.f || !m.inWell) continue;
        const float rr = lerpf(m.r0, m.r, smoothstepf(0.f, 0.45f, m.age));
        put(m.p.x, m.p.y, 0.f, rr, pal_.piece[m.type], 1.f - smoothstepf(0.3f, 0.95f, bridge), m.rot, 0.f, m.seed, 0);
    }
    flush();
    // ---- Previews: small tiles and marbles.
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
            const vec2 pos = v2(center.x + (cells[i][0] - (minx + maxx) * 0.5f) * sc, center.y - (cells[i][1] - (miny + maxy) * 0.5f) * sc);
            put(pos.x, pos.y, TILE_H * sc, TILE_R * sc, pal_.piece[std::clamp(type, 0, 6)], alpha, hashf((float)type, i * 3.f) * TAU, 0.f,
                hashf((float)type + 0.5f, (float)i), 1);
        }
    };
    preview(game.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), game.holdUsed() ? 0.35f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(game.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }
    // ---- The bonus gauge: a ring of twelve marbles, one more for each cleared line; ready, a light runs around it.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float fill = game.bonusActive() ? saturate(game.bonusLeft() / 10.f) : saturate(gaugeShown_);
        const bool ready = game.bonusReady() || game.bonusActive();
        const float n = fill * 12.f;
        const float ga = 1.f - 0.6f * smoothstepf(0.3f, 0.95f, bridge);
        for (int i = 0; i < 12; i++) {
            const float a = PI * 0.5f - TAU * i / 12.f;
            const float x = gc.x + std::cos(a) * 0.82f, y = gc.y + std::sin(a) * 0.82f;
            const float on = saturate(n - i);
            putFx(x, y, 0.07f, 0.f, ugl::luma(pal_.ink) > 0.3f ? pal_.ink * 0.25f : pal_.ink * 0.4f, 0.5f * ga * (1.f - on), 2);
            const float run = ready ? std::pow(0.5f + 0.5f * std::cos(time_ * 3.f - TAU * i / 12.f), 6.f) : 0.f;
            put(x, y, 0.f, 0.15f, pal_.piece[i % 7], on * ga, (float)i, 0.15f * ready + 0.8f * run, hashf((float)i, 3.f), 0, 0.f, 0.f, run);
        }
        flush();
        flushFx();
    }
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

const UniverseEntry& universe_marbles() {
    static const UniverseEntry e{
        "MARBLES", "glass marbles in square tiles, thrown out into a landscape where they pile up",
        [] { return std::unique_ptr<Universe>(new MarblesUniverse()); },
        // Songs with a real drop and some density suit it best (the heaps fill up and the waves have a stage).
        [](const SongLook& l) { return 0.75f + 0.25f * l.density + 0.2f * l.dynamics; },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
