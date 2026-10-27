// MARBLES: square glass tiles, each holding one glass marble.
//
// One world, seen from above: a dark table (felt, slate or brushed metal, one per variant, a fine grain, never busy)
// with the well floating over it as a dark smoked glass pane: everything on the table (the marbles, the dye, the light)
// lies under the well and the HUD, never over the game (under the pane a marble is at most a dim shape). Each cell of a piece is a big clear glass marble held in a
// faint glass box: the table under it seen through it upside down, a core in two layers (ribbon, cat's eye or swirl,
// one per piece type), tiny bubbles. Each variant's palette is a few related hues and an accent, soft through the song
// and rich in a drop. Real light: two slow beams grazing the table from its sides, their rays traced in 2D through the
// marbles (glass balls refracting, reflecting, tinting and focusing the light: caustics opening behind them, colored
// shadows, a marble lighting its neighbors), each marble shaded by the light that actually crosses it.
//
// A cleared row pops: its tiles vanish at once and each marble drops onto the table under the well and is shot out in a
// random direction (mostly out to its side), rolling out from under the pane across the table, slowing on the felt,
// knocking into the marbles already lying there (elastic collisions, billiard chains; a marble at rest sleeps until
// it is hit). Pushed off the edge of the picture, a marble is gone. Every hit spreads the marbles' colors into the
// felt (soft stains, the harder the hit the wider, faint traces where they roll): a song starts on a dark, empty
// table and ends on a colorful one; a new song washes the dye out.
//
// The song: calm is still; mid sets a marble rolling now and then and lets one catch the light on the bars; a chorus
// makes the marbles glimmer (a few glint on the kicks) under a soft glow around the well; a drop splits the beams into
// spectra and sends waves of light flowing through the marbles every two beats (each drop its own shape: out from the
// well, in from the edges, sweeping across, rising, rings from a marble, diagonal bands), the marbles lighting up in
// sequence from within, and the kicks nudge the lit marbles into their neighbors (more color).
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
vec2 nrm2(vec2 a) { const float l = len2(a); return l > 1e-8f ? a * (1.f / l) : v2(0.f, -1.f); }
float lum3(const vec3& c) { return 0.3f * c.x + 0.5f * c.y + 0.2f * c.z; }
vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }
float hashf(float a, float b) {
    const uint64_t h = splitmix64((uint64_t)(int64_t)std::lround(a * 1000.f) * 73856093ull ^ (uint64_t)(int64_t)std::lround(b * 1000.f) * 19349663ull);
    return (h >> 40) * (1.0f / 16777216.0f);
}
float smin(float a, float b, float k) {
    const float h = saturate(0.5f + 0.5f * (b - a) / k);
    return lerpf(b, a, h) - k * h * (1.f - h);
}

constexpr float WELL_HALF = 5.2f, WELL_TOP = 10.15f;   // the pane's extent (the drop's waves start there)
constexpr float TILE_H = 0.46f, TILE_R = 0.43f;      // a tile's half size, its marble's radius (board units)
constexpr int MAX_MARBLES = 250;
constexpr float R_MIN = 0.38f, R_MAX = 0.43f;          // the marbles on the table
constexpr float ROLL = 2.2f, DRAG = 0.2f;              // the felt's rolling resistance: units/s^2, and 1/s
constexpr float E_MARBLE = 0.86f;                      // restitution, marble on marble
constexpr float STAIN_V = 0.6f;                        // a hit at least this hard (relative speed) stains the felt
// each piece type has its own core: a twisted ribbon (0), a cat's eye (1) or a swirl of strands (2), packed with
// the marble's own seed in one float for the shader
const int CORE_STYLE[7] = {0, 1, 2, 1, 0, 2, 1};
float mseed(int type, float seed) { return (float)CORE_STYLE[std::clamp(type, 0, 6)] + std::fmod(std::fabs(seed), 1.f) * 0.999f; }
constexpr float POP = 0.14f;                          // a tile pops in this long (s, real time)

// The variants (sRGB): the table (its ground and the color of its grain's ridges), the well's pane and its edge, the
// tiles' frost, the HUD ink, the accent, the drop's light, the night of slow motion, the lamps; the seven marbles; how
// much the tiles take their marble's tint and how opaque they are, the pane's opacity, the table's surface (0 felt,
// 1 slate, 2 brushed metal), its grain's contrast, how strongly the dye shows, a light ground, the halos' strength.
struct VariantDef {
    const char* name;
    unsigned felt, fiber, panel, panelEdge, tile, ink, accent, wave, night, lamp;
    unsigned piece[7];
    float tileTint, tileAlpha, panelAlpha, surface, grain, dye, light, halo;
};
// Pieces in the game's order: I, O, T, S, Z, J, L.
const VariantDef VARIANTS[] = {
    // Midnight: ink-blue felt, jewel marbles, a cool white lamp; the drop's light is warm gold.
    {"MIDNIGHT", 0x060914, 0x18203c, 0x070a14, 0x3a4668, 0x8a9ac8, 0xdfe6ff, 0xffd27a, 0xffe6b0, 0x040818, 0xe0e6ff,
     {0x62c8e8, 0xf0b54a, 0x7a86f0, 0x3fa8c0, 0x2f55c8, 0xcfdcf5, 0x8fb0f0},
     0.35f, 0.16f, 0.93f, 0.f, 1.f, 0.42f, 0.f, 1.f},
    // Sea glass: dark teal slate, pastel sea-glass marbles, a pale sea light; the drop's light is cyan white.
    {"SEA GLASS", 0x081214, 0x1e3234, 0x061012, 0x3a5a5e, 0xbfe8e8, 0xe0f4f2, 0xa8fff0, 0xd8fff8, 0x061418, 0xd8f4f0,
     {0x8fe8e0, 0xb8e0c0, 0xa8d0f0, 0x7ad0a0, 0xf0907a, 0x4a9ac8, 0xd8f4e4},
     0.30f, 0.16f, 0.93f, 1.f, 1.f, 0.36f, 0.f, 1.f},
    // Obsidian: black brushed metal, vivid marbles; the drop's light is white.
    {"OBSIDIAN", 0x060607, 0x1a1a20, 0x040405, 0x34343e, 0xffffff, 0xe8e8ee, 0xffffff, 0xffffff, 0x04060e, 0xfff8f0,
     {0xd8e4f0, 0xff8a2a, 0x9a9aa8, 0x6a7a88, 0xff3a2a, 0x8a9ab0, 0xffc84a},
     0.25f, 0.08f, 0.94f, 2.f, 0.8f, 0.34f, 0.f, 1.f},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

using Palette = MarblesUniverse::Palette;
static_assert(sizeof(Palette) % sizeof(float) == 0, "a palette is floats only");
constexpr int PAL_FLOATS = (int)(sizeof(Palette) / sizeof(float));

Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    auto L = [](unsigned c) { return ugl::toLinear(hex(c)); };
    Palette p;
    p.felt = L(d.felt); p.fiber = L(d.fiber); p.panel = L(d.panel); p.panelEdge = L(d.panelEdge); p.tile = L(d.tile);
    p.ink = L(d.ink); p.accent = L(d.accent); p.wave = L(d.wave); p.night = L(d.night); p.lamp = L(d.lamp);
    for (int i = 0; i < 7; i++) p.piece[i] = L(d.piece[i]);
    p.tileTint = d.tileTint; p.tileAlpha = d.tileAlpha; p.panelAlpha = d.panelAlpha;
    p.surface = d.surface; p.grain = d.grain; p.dye = d.dye; p.light = d.light; p.halo = d.halo;
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
    progLight_ = compile(shaders::LIGHT_VS, shaders::LIGHT_FS, "marbles light");
    progStain_ = compile(shaders::STAIN_VS, shaders::STAIN_FS, "marbles stain");
    progWash_ = compile(ugl::FULLSCREEN_VS, shaders::WASH_FS, "marbles wash");

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
    instanced(marbleVao_, marbleVbo_, 5, sizeof(MarbleInst));
    instanced(lightVao_, lightVbo_, 3, sizeof(LightInst));
    instanced(fxVao_, fxVbo_, 3, sizeof(FxInst));
    instanced(stainVao_, stainVbo_, 3, sizeof(StainInst));

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

    buildFont(fontLight_, FONT_LIGHT);
    buildFont(fontMedium_, FONT_MEDIUM);
    pal_ = paletteOf(0);
    resize(width, height);
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
    bgT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    lightT_.create(std::max(1, width_ / 2), std::max(1, height_ / 2), GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    glBindTexture(GL_TEXTURE_2D, lightT_.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    stainT_.create(std::max(1, width_ / 2), std::max(1, height_ / 2), GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    washNow_ = true;
    gridX0_ = viewC_.x - hw - 2.f;
    gridY0_ = viewC_.y - hh - 2.f;
    gridW_ = (int)std::ceil((2.f * hw + 4.f) / GRID) + 1;
    gridH_ = (int)std::ceil((2.f * hh + 4.f) / GRID) + 1;
}

MarblesUniverse::~MarblesUniverse() {
    sceneT_.destroy();
    bgT_.destroy();
    lightT_.destroy();
    stainT_.destroy();
    for (GLuint p : {progSky_, progMarble_, progFx_, progText_, progShape_, progPost_, progLight_, progStain_, progWash_}) glDeleteProgram(p);
    for (GLuint v : {marbleVao_, fxVao_, textVao_, shapeVao_, lightVao_, stainVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, marbleVbo_, fxVbo_, textVbo_, shapeVbo_, lightVbo_, stainVbo_}) glDeleteBuffers(1, &b);
    for (Font* f : {&fontLight_, &fontMedium_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// Under the HUD's columns or a line of its text (the marbles roll under them, dimmed there: the HUD stays readable).
float MarblesUniverse::hudShade(vec2 p) const {
    float k = 0.f;
    for (const KeepOut& o : keepOut_) {
        const float dx = std::max(o.x0 - p.x, p.x - o.x1), dy = std::max(o.y0 - p.y, p.y - o.y1);
        k = std::max(k, 1.f - smoothstepf(-0.3f, 0.4f, std::max(dx, dy)));
    }
    return k;
}

// =====================================================================================================
// Song, level
// =====================================================================================================

// The table from the song (only dark ones: the light is what the marbles live by): dark minor songs on OBSIDIAN or
// MIDNIGHT, brighter ones on SEA GLASS or MIDNIGHT, and the same for major songs the other way round; the key's hue
// picks between them.
int MarblesUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    int v;
    // 0 MIDNIGHT, 1 SEA GLASS, 2 OBSIDIAN
    if (!t.hasSong) v = (int)(h * VARIANT_COUNT) % VARIANT_COUNT;
    else if (t.minor) v = t.brightness < 0.42f ? (h < 0.5f ? 2 : 0) : (h < 0.5f ? 1 : 0);
    else v = t.brightness > 0.6f ? (h < 0.5f ? 1 : 0) : (h < 0.5f ? 0 : 2);
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 2)) % VARIANT_COUNT;
    return v;
}

void MarblesUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    seedF_ = (float)(look.seed % 1000) * 0.0137f;
    rng_ = Rng(look.seed ^ 0x4D4Bull);
    dropShape_ = (int)(look.seed % 6);
    // an empty table: the marbles of the last song fade away, its dye is washed out
    waves_.clear();
    glints_.clear();
    if (seconds <= 0.f) {
        palSet_ = false;
        marbles_.clear();
        washNow_ = true;
    } else {
        for (Marble& m : marbles_) m.retiring = true;
        washT_ = 2.5f;
    }
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
    h.background = lerp(p.felt, p.fiber, 0.4f);
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
        // the tiles pop and their marbles are shot out across the table
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const Slot& s = slots_[c.y][c.x];
            const int type = std::clamp(c.type, 0, 6);
            const vec3 p = layout::cellPos((float)c.x, c.y - game.rowOffset(c.y));
            pops_.push_back({p.x, p.y, 0.f, 0.f, type});
            launch(v2(p.x, p.y), type, s.known ? s.seed : hashf((float)c.x, (float)c.y), s.known ? s.rot : 0.f, 0.f, tetris ? 1.25f : 1.f);
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
        // a Tetris: a wave of light runs out from the well through every marble
        if (tetris) startWave(0, 1.3f, 3);
        break;
    }
    case GameEvent::TopOut: {
        // game over: every marble leaves its tile, row by row from the top, shot out across the table
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (auto& c : ev.cells) {
            if (c.y < Game::HIDDEN || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const Slot& s = slots_[c.y][c.x];
            const int type = std::clamp(c.type, 0, 6);
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            const float delay = 0.25f + (c.y - top) * 0.06f + std::fabs(c.x - 4.5f) * 0.012f;
            pops_.push_back({p.x, p.y, 0.f, delay, type});
            launch(v2(p.x, p.y), type, s.known ? s.seed : hashf((float)c.x, (float)c.y), s.known ? s.rot : 0.f, delay, 1.f);
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
    int unseen = 0;
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
                unseen++;
            }
        }
    // a whole board filled at once some time into an empty table: a review's board (a song never starts with marbles
    // on the table; a change of universe brings its stack on its first frames)
    if (unseen >= 24 && (int)marbles_.size() < 60 && time_ > 5.f) seedMarbles(90 + 50 * std::clamp(level_, 0, 2));
}

float MarblesUniverse::bounceOf(const Slot& s) const { return jolt(s.bounce, s.bounceT); }

// =====================================================================================================
// The marbles on the table
// =====================================================================================================

// A marble leaves its tile: dropped onto the table under it and shot out in a random direction, mostly out to its
// side, at a real speed (it rolls out from under the well).
void MarblesUniverse::launch(vec2 from, int type, float seed, float rot, float delay, float vigor) {
    Marble m;
    m.p = from;
    m.type = type;
    m.seed = seed;
    m.rot = rot;
    m.delay = delay;
    m.r = rng_.range(R_MIN, R_MAX);
    const float side = std::fabs(from.x) < 0.6f ? (rng_.chance(0.5f) ? -1.f : 1.f) : (from.x < 0.f ? -1.f : 1.f);
    const float ang = rng_.chance(0.85f) ? (side > 0.f ? 0.f : PI) + rng_.range(-1.15f, 1.15f) : rng_.range(0.f, TAU);
    const float sp = rng_.range(6.5f, 10.5f) * vigor;
    m.v = v2(std::cos(ang), std::sin(ang)) * sp;
    m.spin = rng_.range(-1.f, 1.f);
    marbles_.push_back(m);
}

void MarblesUniverse::buildGrid() {
    const int n = (int)marbles_.size();
    gridHead_.assign(gridW_ * gridH_, -1);
    gridNext_.assign(n, -1);
    for (int i = 0; i < n; i++) {
        const Marble& m = marbles_[i];
        if (m.delay > 0.f || m.fade <= 0.f || m.under) continue;
        const int cx = std::clamp((int)((m.p.x - gridX0_) / GRID), 0, gridW_ - 1);
        const int cy = std::clamp((int)((m.p.y - gridY0_) / GRID), 0, gridH_ - 1);
        gridNext_[i] = gridHead_[cy * gridW_ + cx];
        gridHead_[cy * gridW_ + cx] = i;
    }
}

// A stain of dye on the felt (queued: painted into the dye target with the frame), in a richer shade of the glass's
// color (pale glass would leave a gray haze).
void MarblesUniverse::stain(vec2 p, float radius, const vec3& col, float alpha, vec2 stretch) {
    if (stains_.size() > 6000 || alpha <= 0.002f) return;
    const float l = lum3(col);
    const vec3 c = vec3(std::max(0.f, l + (col.x - l) * 1.7f), std::max(0.f, l + (col.y - l) * 1.7f), std::max(0.f, l + (col.z - l) * 1.7f));
    stains_.push_back({{p.x, p.y, radius, rng_.uniform()}, {c.x, c.y, c.z, alpha}, {stretch.x, stretch.y, 0.f, 0.f}});
}

// One step of the table, seen from above: the marbles roll on (under the well and the HUD too), slowed by the felt; a
// few passes of collisions between marbles (a hashed grid; elastic, equal density: billiard
// chains). A marble at rest sleeps until it is hit hard enough (a gentle touch leaves it be: no jitter, no creeping).
// Each hit stains the felt with both marbles' colors around them, the harder the wider; rolling leaves faint traces.
// A marble pushed off the edge of the picture is gone.
void MarblesUniverse::stepPhysics(float h, bool stains) {
    const float xL = viewC_.x - viewH_.x, xR = viewC_.x + viewH_.x, yB = viewC_.y - viewH_.y, yT = viewC_.y + viewH_.y;
    const int n = (int)marbles_.size();
    for (Marble& m : marbles_) {
        if (m.delay > 0.f) { m.delay -= h; continue; }
        m.age += h;
        if (m.asleep || m.fade <= 0.f) continue;
        const float sp = len2(m.v);
        m.under = std::fabs(m.p.x) < WELL_HALF && std::fabs(m.p.y) < WELL_TOP;
        // under the pane it glides on out, touching nothing (nothing ever comes to rest there, hidden)
        if (m.under) {
            if (sp < 4.5f) m.v = sp > 1e-3f ? m.v * (4.5f / sp) : v2(m.p.x < 0.f ? -4.5f : 4.5f, 0.f);
        } else if (sp > 1e-5f) {
            const float ns = std::max(0.f, sp - (ROLL + DRAG * sp) * h);
            m.v = m.v * (ns / sp);
        }
        m.p = m.p + m.v * h;
        m.rot += m.spin * sp / m.r * 0.35f * h;
        if (stains && sp > 1.f) {
            m.trail += sp * h;
            if (m.trail > 0.28f) {
                m.trail = 0.f;
                stain(m.p, m.r * 0.7f, pal_.piece[m.type], 0.014f * std::min(1.f, sp / 6.f), m.v * (0.5f / sp));
            }
        }
        if (m.p.x < xL - m.r || m.p.x > xR + m.r || m.p.y < yB - m.r || m.p.y > yT + m.r) m.fade = -1.f;
    }
    buildGrid();
    auto hitStain = [&](const Marble& m, vec2 toward, float v, float k) {
        const float s = saturate((v - STAIN_V) / 9.f);
        stain(m.p + toward * (0.3f * m.r), m.r * (1.f + 1.9f * s), pal_.piece[m.type], k * (0.1f + 0.4f * s), toward * (0.35f * s));
    };
    for (int iter = 0; iter < 2; iter++)
        for (int i = 0; i < n; i++) {
            Marble& a = marbles_[i];
            if (a.delay > 0.f || a.asleep || a.fade <= 0.f || a.under) continue;
            const int cx = std::clamp((int)((a.p.x - gridX0_) / GRID), 0, gridW_ - 1);
            const int cy = std::clamp((int)((a.p.y - gridY0_) / GRID), 0, gridH_ - 1);
            for (int gy = std::max(0, cy - 1); gy <= std::min(gridH_ - 1, cy + 1); gy++)
                for (int gx = std::max(0, cx - 1); gx <= std::min(gridW_ - 1, cx + 1); gx++)
                    for (int j = gridHead_[gy * gridW_ + gx]; j >= 0; j = gridNext_[j]) {
                        if (j == i) continue;
                        Marble& b = marbles_[j];
                        if (j < i && !b.asleep) continue; // a pair of moving marbles is handled once
                        if (b.under) continue;
                        const vec2 dv = a.p - b.p;
                        const float rr = a.r + b.r;
                        const float d2 = dot2(dv, dv);
                        if (d2 >= rr * rr || d2 < 1e-10f) continue;
                        const float d = std::sqrt(d2);
                        const vec2 nn = dv * (1.f / d);
                        const float ov = rr - d;
                        const float vn = dot2(a.v - b.v, nn);
                        if (b.asleep) {
                            if (vn > -0.15f) {
                                // a gentle touch: the resting one stays put
                                a.p = a.p + nn * ov;
                                if (vn < 0.f) a.v = a.v - nn * vn;
                                continue;
                            }
                            b.asleep = false;
                            b.sleepT = 0.f;
                        }
                        const float ma = a.r * a.r, mb = b.r * b.r;
                        const float wa = mb / (ma + mb), wb = ma / (ma + mb);
                        a.p = a.p + nn * (ov * wa);
                        b.p = b.p - nn * (ov * wb);
                        if (vn < 0.f) {
                            const float J = -(1.f + E_MARBLE) * vn;
                            a.v = a.v + nn * (J * wa);
                            b.v = b.v - nn * (J * wb);
                            if (std::fabs(a.spin) < 0.2f) a.spin = (i & 1) ? 1.f : -1.f;
                            b.spin = -a.spin;
                            if (stains && -vn > STAIN_V) {
                                hitStain(a, nn * -1.f, -vn, 1.f);
                                hitStain(b, nn, -vn, 1.f);
                                if (-vn > 4.f) glint(b.p + nn * b.r, 0.3f + 0.04f * std::min(-vn, 10.f), lerp(lerp(pal_.piece[a.type], pal_.piece[b.type], 0.5f), vec3(1.f), 0.6f));
                            }
                        }
                    }
        }
    // at rest: asleep (and ground for gentle touches) until hit
    for (int i = 0; i < n; i++) {
        Marble& a = marbles_[i];
        if (a.delay > 0.f || a.asleep || a.under) continue;
        if (len2(a.v) < 0.08f) {
            a.sleepT += h;
            if (a.sleepT > 0.2f) { a.asleep = true; a.v = v2(0.f, 0.f); }
        } else a.sleepT = 0.f;
    }
}

void MarblesUniverse::simulateMarbles(float dt) {
    if (dt <= 0.f) return;
    const int steps = std::clamp((int)std::ceil(dt * 120.f), 1, 8);
    for (int s = 0; s < steps; s++) stepPhysics(dt / steps, true);
    // past the cap, the oldest marbles at rest fade away
    int live = 0;
    for (const Marble& m : marbles_) live += !m.retiring && m.fade > 0.f;
    if (live > MAX_MARBLES) {
        int excess = live - MAX_MARBLES;
        std::vector<int> idx;
        for (int i = 0; i < (int)marbles_.size(); i++)
            if (!marbles_[i].retiring && marbles_[i].fade > 0.f) idx.push_back(i);
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            if (marbles_[a].asleep != marbles_[b].asleep) return marbles_[a].asleep;
            return marbles_[a].age > marbles_[b].age;
        });
        for (int i = 0; i < (int)idx.size() && excess > 0; i++, excess--) marbles_[idx[i]].retiring = true;
    }
    for (Marble& m : marbles_)
        if (m.retiring) m.fade -= dt / 1.6f;
    marbles_.erase(std::remove_if(marbles_.begin(), marbles_.end(), [](const Marble& m) { return m.fade <= 0.f; }), marbles_.end());
}

// A board filled for a review shot: the table as it would be late in a song, marbles lying about on dyed felt.
void MarblesUniverse::seedMarbles(int n) {
    Rng r(seed_ ^ 0x5EEDull);
    const float X = viewH_.x - 0.7f, Y = viewH_.y - 0.7f;
    for (int k = 0, placed = 0; k < n * 30 && placed < n; k++) {
        Marble m;
        m.type = r.irange(0, 6);
        m.seed = r.uniform();
        m.rot = r.range(0.f, TAU);
        m.r = r.range(R_MIN, R_MAX);
        // more of them near the well (where they come from), some far out
        const float side = r.chance(0.5f) ? -1.f : 1.f;
        m.p = v2(side * (WELL_HALF + m.r + 0.1f + std::pow(r.uniform(), 1.4f) * (X - WELL_HALF - m.r)), viewC_.y + r.range(-Y, Y));
        bool ok = true;
        for (const Marble& o : marbles_)
            if (ok && len2(o.p - m.p) < o.r + m.r + 0.04f) ok = false;
        if (!ok) continue;
        m.asleep = true;
        m.age = 10.f + r.range(0.f, 5.f);
        marbles_.push_back(m);
        placed++;
        // the dye of its hits and its way there
        const vec3 c = pal_.piece[m.type];
        for (int h = r.irange(1, 3); h > 0; h--) {
            const float a = r.range(0.f, TAU), v = r.range(1.f, 9.f), sk = saturate((v - STAIN_V) / 9.f);
            const vec2 dir = v2(std::cos(a), std::sin(a));
            stain(m.p + dir * (r.range(0.f, 1.6f)), m.r * (1.f + 1.9f * sk), c, 0.1f + 0.4f * sk, dir * (0.35f * sk));
        }
        const float a = r.range(0.f, TAU), len = r.range(1.f, 6.f);
        const vec2 dir = v2(std::cos(a), std::sin(a));
        for (float t = 0.f; t < len; t += 0.28f) stain(m.p + dir * t, m.r * 0.7f, c, 0.014f, dir * 0.5f);
    }
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
    case 4: w.speed = 13.f; w.width = 0.9f; break;   // rings from a marble
    default: w.speed = 26.f; w.width = 1.2f; break;  // diagonal bands
    }
    if (shape == 4) {
        // the rings start at a marble lying on the wave's side
        std::vector<int> c;
        for (int i = 0; i < (int)marbles_.size(); i++)
            if (marbles_[i].p.x * w.sign > 0.f) c.push_back(i);
        if (!c.empty()) w.o = marbles_[c[rng_.irange(0, (int)c.size() - 1)]].p;
    }
    waves_.push_back(w);
}

float MarblesUniverse::waveCoord(const Wave& w, vec2 p) const {
    const float X = viewH_.x;
    switch (w.shape) {
    case 0: return std::max(std::fabs(p.x) - WELL_HALF, std::fabs(p.y) - WELL_TOP);
    case 1: return X - std::fabs(p.x);
    case 2: return w.sign > 0.f ? p.x + X : X - p.x;
    case 3: return p.y + 13.f;
    case 4: return len2(p - w.o);
    default: return 0.8f * (w.sign * p.x + X) + 0.6f * (6.f - p.y);
    }
}

// =====================================================================================================
// Simulation
// =====================================================================================================

void MarblesUniverse::simulate(const Game& game, const MusicState& music, float dt) {
    time_ += dt;
    buildGrid();
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
    // the colors: soft through the song, a little richer in a chorus, rich and glowing for a drop (in quickly at its
    // start, settling back over a few seconds after it)
    {
        const float vt = std::max(drive_, 0.3f * wp * (1.f - drive_));
        vivid_ = approach(vivid_, vt, vt > vivid_ ? 2.5f : 0.6f, dt);
    }
    swell_ = approach(swell_, 0.f, 0.5f, dt);

    // The beat grid: waves every two beats through a drop (a stronger, double one on the bars); at mid a marble is
    // set rolling now and then and one catches the light, on the bars.
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
            if (rng_.chance(0.5f))
                for (int k = 0; k < 12; k++) {
                    Marble& m = marbles_[rng_.irange(0, (int)marbles_.size() - 1)];
                    if (!m.asleep || m.retiring) continue;
                    const float a = rng_.range(0.f, TAU);
                    m.asleep = false;
                    m.sleepT = 0.f;
                    m.v = v2(std::cos(a), std::sin(a)) * rng_.range(1.5f, 2.6f);
                    break;
                }
        }
    }
    // Kicks at the peak: a few marbles glint (a chorus); a few lit marbles are nudged into their neighbors (a drop).
    sinceKick_ += dt;
    const float kick = std::max(music.kick, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    if (kick > 0.5f && prevKick_ <= 0.5f && sinceKick_ > 0.4f && wp > 0.5f && world > 0.5f && !over && !marbles_.empty()) {
        sinceKick_ = 0.f;
        if (drive_ > 0.5f) {
            int nudges = 0;
            for (int k = 0; k < 60 && nudges < 5; k++) {
                Marble& m = marbles_[rng_.irange(0, (int)marbles_.size() - 1)];
                if (!m.asleep || m.retiring || m.light < 0.25f) continue;
                const float a = rng_.range(0.f, TAU);
                m.asleep = false;
                m.sleepT = 0.f;
                m.v = v2(std::cos(a), std::sin(a)) * rng_.range(2.f, 3.5f);
                nudges++;
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
        if (m.delay <= 0.f)
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
// The traced light
// =====================================================================================================

namespace {
constexpr int RAYS = 44;              // rays per beam
constexpr int MAX_SEGS = 3600;        // light segments per frame, all beams together (bounded cost)
constexpr int MAX_HITS = 14;          // marbles one ray crosses at most
constexpr float N_GLASS = 1.5f;
constexpr float SHAFT_GAIN = 0.3f;    // a beam on the table
constexpr float REACH = 13.f;         // a lamp's light fades over this distance across the table
constexpr float LIT_K = 0.4f;         // the light a marble receives, from what crossed it
constexpr float FAN_K = 1.1f;         // how widely a spectrum fans out of a marble (radians per unit of index)
constexpr float OPEN = 0.12f;         // how fast a ray's band widens out of a marble (the caustic opening)
const vec3 SPEC3[3] = {{1.15f, 0.22f, 0.12f}, {0.25f, 1.05f, 0.35f}, {0.12f, 0.3f, 1.2f}};
} // namespace

void MarblesUniverse::emitShaft(vec2 a, vec2 b, float w0, float w1, const vec3& c, float gain, float fall) {
    if (segs_ >= MAX_SEGS || lum3(c) * gain < 0.0004f) return;
    segs_++;
    // a widening band spreads the same light: dimmer as it opens (and as it goes)
    light_.push_back({{a.x, a.y, b.x, b.y}, {w0, w1, gain, gain * fall * std::sqrt(w0 / std::max(w1, 1e-4f))}, {c.x, c.y, c.z, 0.f}});
}

// The nearest marble along a ray, between t0 and tMax: marching the hashed grid, the circles of each new cell's
// neighborhood tested exactly (a circle reached at t has its center in a neighbor of the cell holding the point at
// t, so once the march passes the best hit nothing nearer is left).
bool MarblesUniverse::hitMarble(vec2 o, vec2 d, float t0, float tMax, float& tHit, int& idx) const {
    float best = tMax;
    idx = -1;
    int lastCx = -9999, lastCy = -9999;
    const float step = GRID * 0.5f;
    for (float t = t0; t <= best + step && t <= tMax + step; t += step) {
        const vec2 p = o + d * t;
        const int cx = std::clamp((int)((p.x - gridX0_) / GRID), 0, gridW_ - 1);
        const int cy = std::clamp((int)((p.y - gridY0_) / GRID), 0, gridH_ - 1);
        if (cx == lastCx && cy == lastCy) continue;
        lastCx = cx;
        lastCy = cy;
        for (int gy = std::max(0, cy - 1); gy <= std::min(gridH_ - 1, cy + 1); gy++)
            for (int gx = std::max(0, cx - 1); gx <= std::min(gridW_ - 1, cx + 1); gx++)
                for (int j = gridHead_[gy * gridW_ + gx]; j >= 0; j = gridNext_[j]) {
                    const Marble& m = marbles_[j];
                    const vec2 oc = o - m.p;
                    const float b = dot2(oc, d), cc = dot2(oc, oc) - m.r * m.r;
                    const float disc = b * b - cc;
                    if (disc <= 0.f) continue;
                    const float th = -b - std::sqrt(disc);
                    if (th > std::max(t0, 1e-3f) && th < best) { best = th; idx = j; }
                }
    }
    tHit = best;
    return idx >= 0;
}

// One ray grazing the table: straight on to the next marble or out of the picture; into a marble, refracted (a part
// reflected off its surface, a dimmer ray of its own), across it (the light it carries lights the marble, tinted by
// its glass on the way), out of it refracted again: a ball lens, the rays crossing in a bright caustic just behind it
// and opening out (its band widening), the shadow around them tinted by the glass; on the drop the first marble
// splits it into a spectrum.
void MarblesUniverse::traceRay(const Ray& first) {
    rays_.clear();
    rays_.push_back(first);
    const float xL = viewC_.x - viewH_.x - 1.f, xR = viewC_.x + viewH_.x + 1.f;
    const float yB = viewC_.y - viewH_.y - 1.f, yT = viewC_.y + viewH_.y + 1.f;
    while (!rays_.empty() && segs_ < MAX_SEGS) {
        Ray r = rays_.back();
        rays_.pop_back();
        for (int guard = 0; guard < 40 && segs_ < MAX_SEGS; guard++) {
            if (lum3(r.c) * r.gi < 0.004f) break;
            float tEdge = 1e9f;
            if (r.d.x > 1e-6f) tEdge = std::min(tEdge, (xR - r.o.x) / r.d.x);
            if (r.d.x < -1e-6f) tEdge = std::min(tEdge, (xL - r.o.x) / r.d.x);
            if (r.d.y > 1e-6f) tEdge = std::min(tEdge, (yT - r.o.y) / r.d.y);
            if (r.d.y < -1e-6f) tEdge = std::min(tEdge, (yB - r.o.y) / r.d.y);
            if (tEdge <= 1e-4f) break;
            float tm;
            int mi = -1;
            const bool hitM = r.hits < MAX_HITS && hitMarble(r.o, r.d, 0.f, tEdge, tm, mi);
            const float tEnd = hitM ? tm : tEdge;
            const vec2 p = r.o + r.d * tEnd;
            const float w1 = r.w + r.spread * tEnd;
            const float fall = std::exp(-tEnd / REACH);
            emitShaft(r.o, p, r.w, w1, r.c, SHAFT_GAIN * r.gi * (r.split ? 1.4f : 1.f), fall);
            r.gi *= std::sqrt(r.w / std::max(w1, 1e-4f)) * fall;
            r.w = w1;
            if (!hitM) break;
            // ---- into a marble
            const Marble& m = marbles_[mi];
            const vec2 n = nrm2(p - m.p);
            const float cosi = std::max(0.05f, -dot2(r.d, n));
            const float R = std::min(0.25f, 0.04f + 0.96f * std::pow(1.f - cosi, 5.f));
            if (r.depth < 1 && lum3(r.c) * r.gi * R > 0.02f) {
                Ray s = r;
                s.d = nrm2(r.d - n * (2.f * dot2(r.d, n)));
                s.c = r.c * R;
                s.o = p + n * 0.002f;
                s.depth = r.depth + 1;
                s.spread = OPEN;
                rays_.push_back(s);
            }
            r.c = r.c * (1.f - R);
            const float eta = 1.f / r.n;
            const float k = 1.f - eta * eta * (1.f - cosi * cosi);
            const vec2 t = nrm2(r.d * eta + n * (eta * cosi - std::sqrt(std::max(0.f, k))));
            const float chord = std::max(0.f, -2.f * dot2(p - m.p, t));
            const vec2 q = p + t * chord;
            acc_[mi] += r.c * (chord * r.gi);
            accDir_[mi] = accDir_[mi] + t * (lum3(r.c) * r.gi * chord);
            {
                const vec3 tn = pal_.piece[m.type];
                const float mx = std::max(0.02f, std::max(tn.x, std::max(tn.y, tn.z)));
                const float a = std::min(1.f, 0.35f + 0.5f * chord);
                r.c = vec3(r.c.x * (1.f - a * (1.f - tn.x / mx)), r.c.y * (1.f - a * (1.f - tn.y / mx)), r.c.z * (1.f - a * (1.f - tn.z / mx))) * 0.93f;
            }
            // ---- out of it: a narrow band (the rays converge), opening out from there
            const vec2 n2 = nrm2(q - m.p);
            const float c2 = std::max(0.f, dot2(t, n2));
            auto refractOut = [&](float nIdx) {
                const float k2 = 1.f - nIdx * nIdx * (1.f - c2 * c2);
                if (k2 < 0.f) return nrm2(t - n2 * (2.f * c2));
                return nrm2(t * nIdx + n2 * (std::sqrt(k2) - nIdx * c2));
            };
            r.hits++;
            const float wOut = std::max(0.035f, r.w * 0.35f);
            r.gi *= std::min(1.6f, std::sqrt(r.w / wOut));
            r.w = wOut;
            r.spread = OPEN;
            if (!r.split && disp_ > 0.02f) {
                // the drop: the marble splits the light into a spectrum fanning out
                for (int sk = 0; sk < 3; sk++) {
                    Ray s = r;
                    s.split = true;
                    s.n = N_GLASS + disp_ * (sk - 1.f);
                    vec2 od = refractOut(s.n);
                    const float ang = (s.n - N_GLASS) * FAN_K;
                    const float ca = std::cos(ang), sa = std::sin(ang);
                    const vec2 o2 = v2(od.x * ca - od.y * sa, od.x * sa + od.y * ca);
                    if (dot2(o2, n2) > 0.05f) od = o2;
                    s.d = od;
                    s.c = r.c * SPEC3[sk] * 0.4f;
                    s.o = q + n2 * 0.002f;
                    rays_.push_back(s);
                }
                break;
            }
            r.d = refractOut(r.n);
            r.o = q + n2 * 0.002f;
        }
    }
}

// The beams: two lamps beyond the table's sides, their light grazing it in wide soft bands, one on each side of the
// well, turning and drifting very slowly (a minute or more); the second one comes up from mid on; stronger and whiter
// at a drop, where the marbles split them into spectra. Each marble is lit by the light that actually crosses it
// (smoothed, nothing flickers); the drop's waves are light too, travelling through the marbles.
void MarblesUniverse::traceLight(float dt, float world) {
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    beamWorld_ += dt * lerpf(1.f, 0.35f, slow_);
    const float on0 = 0.75f + 0.1f * wm + 0.05f * wp + 0.15f * drive_, on1 = 0.35f + 0.35f * wm + 0.15f * wp + 0.2f * drive_;
    beamOn_[0] = approach(beamOn_[0], on0, 1.f, dt);
    beamOn_[1] = approach(beamOn_[1], on1, 1.f, dt);
    disp_ = approach(disp_, 0.16f * drive_, drive_ > disp_ / 0.16f ? 2.5f : 0.8f, dt);
    light_.clear();
    segs_ = 0;
    const int n = (int)marbles_.size();
    acc_.assign(n, vec3(0.f));
    accDir_.assign(n, v2(0.f, 0.f));
    const float side = seedF_ > 6.85f ? -1.f : 1.f;   // the first lamp's side (from the song)
    vec3 col = lerp(pal_.lamp, pal_.wave, 0.25f);
    col = lerp(col, vec3(1.f, 0.98f, 0.95f), 0.5f * drive_);
    col = lerp(col, vec3(0.5f, 0.62f, 1.f), 0.6f * slow_);
    const float gain = world * (1.f - 0.3f * slow_);
    keyCol_ = col;
    const float xL = viewC_.x - viewH_.x - 1.f, xR = viewC_.x + viewH_.x + 1.f;
    const float yB = viewC_.y - viewH_.y - 1.f, yT = viewC_.y + viewH_.y + 1.f;
    for (int b = 0; b < 2; b++) {
        const float ph = seedF_ * 3.7f + b * 2.1f;
        // beam 0 comes down across the first side, beam 1 up across the other
        const float sx = b == 0 ? -side : side;
        const float th = 0.5f + 0.28f * std::sin(beamWorld_ * TAU / (97.f + 31.f * b) + ph);
        vec2 d = v2(-sx * std::cos(th), (b == 0 ? -1.f : 1.f) * std::sin(th));
        if (b == 0) {
            keyDir_ = d;
            keyAmt_ = 0.32f * beamOn_[0] * gain;
        }
        const float on = beamOn_[b] * gain;
        if (on < 0.02f) continue;
        const vec2 aim = v2(sx * (12.f + 3.f * std::sin(beamWorld_ * TAU / (71.f + 23.f * b) + ph)),
                            (b == 0 ? 2.f : -2.f) + 3.5f * std::sin(beamWorld_ * TAU / (83.f + 17.f * b) + ph * 0.7f));
        const float width = 7.5f + 1.2f * std::sin(beamWorld_ * TAU / 53.f + ph * 1.3f);
        const vec2 perp = v2(-d.y, d.x);
        for (int k = 0; k < RAYS; k++) {
            const float u = 2.f * (k + 0.5f) / RAYS - 1.f;
            Ray r;
            vec2 o = aim + perp * (u * width * 0.5f);
            // back along the beam to the edge of the view
            float back = 1e9f;
            if (d.x > 1e-6f) back = std::min(back, (o.x - xL) / d.x);
            if (d.x < -1e-6f) back = std::min(back, (o.x - xR) / d.x);
            if (d.y > 1e-6f) back = std::min(back, (o.y - yB) / d.y);
            if (d.y < -1e-6f) back = std::min(back, (o.y - yT) / d.y);
            if (back < 0.f || back > 1e8f) continue;
            r.o = o - d * back;
            r.d = d;
            r.c = col * (on * std::exp(-u * u * 2.2f));
            r.w = width / RAYS * 1.5f;
            traceRay(r);
        }
    }
    // the light the marbles received, smoothed; the drop's waves as light travelling through the marbles
    const float ks = 1.f - std::exp(-dt * 8.f);
    for (int i = 0; i < n; i++) {
        Marble& m = marbles_[i];
        vec3 target = acc_[i] * LIT_K;
        vec2 dir = accDir_[i];
        if (m.light > 0.02f && !waves_.empty()) {
            // the waves travel toward a growing coordinate: its gradient here
            const Wave& w = waves_.back();
            const float e = 0.3f;
            vec2 g = v2(waveCoord(w, m.p + v2(e, 0.f)) - waveCoord(w, m.p - v2(e, 0.f)), waveCoord(w, m.p + v2(0.f, e)) - waveCoord(w, m.p - v2(0.f, e)));
            target += pal_.wave * (0.4f * std::min(m.light, 1.2f));
            dir = dir + nrm2(g) * (1.5f * m.light);
        }
        // a soft shoulder: a marble never burns out
        target = target * (1.f / (1.f + 0.9f * lum3(target)));
        m.lit = lerp(m.lit, target, ks);
        // in the shadow it still shows the lamp's side
        dir = dir + keyDir_ * 0.05f;
        m.litDir = nrm2(m.litDir + (nrm2(dir) - m.litDir) * ks);
    }
}

// =====================================================================================================
// Drawing
// =====================================================================================================

void MarblesUniverse::put(float x, float y, float half, float radius, const vec3& col, float alpha, float rot, float glow,
                          float seed, int kind, float mx, float my, float flare, float haze, const vec3* lit, float lightAngle) {
    if (alpha <= 0.003f) return;
    const vec3 L = lit ? *lit : vec3{0.f, 0.f, 0.f};
    inst_.push_back({{x, y, half, radius}, {col.x, col.y, col.z, alpha}, {rot, glow, seed, (float)kind}, {mx, my, flare, haze},
                     {L.x, L.y, L.z, lit ? lightAngle : -9.f}});
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
    set3f(progMarble_, "uBridgeInk", bridge_.ink);
    set3f(progMarble_, "uPanelEdge", pal_.panelEdge);
    // the marbles' colors: soft through the song, rich at a drop
    set1f(progMarble_, "uSat", lerpf(0.78f, 1.25f, vivid_));
    set1f(progMarble_, "uCore", lerpf(0.85f, 1.15f, vivid_));
    set4f(progMarble_, "uPanel", pal_.panel.x, pal_.panel.y, pal_.panel.z, pal_.panelAlpha);
    set4f(progMarble_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set4f(progMarble_, "uKey", keyDir_.x, keyDir_.y, keyAmt_, 0.f);
    set3f(progMarble_, "uKeyCol", keyCol_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bgT_.tex);
    set1i(progMarble_, "uBg", 1);
    glActiveTexture(GL_TEXTURE0);
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

// The marbles on the table (once they have left the well), their shadows, their light.
void MarblesUniverse::drawWorld(float world) {
    if (world <= 0.01f) return;
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float base = 0.03f * wm + 0.07f * wp * (1.f - drive_) + 0.1f * drive_ + 0.35f * swell_ * drive_ + 0.32f * slow_;
    for (const Marble& m : marbles_) {
        if (m.delay > 0.f) continue;
        const float lit = m.light + m.twinkle;
        const float a = saturate(m.fade) * world * (1.f - 0.7f * hudShade(m.p));
        const float ang = std::atan2(m.litDir.y, m.litDir.x);
        put(m.p.x, m.p.y, 0.f, m.r, pal_.piece[m.type], a, m.rot, base + 0.55f * lit + 0.12f * vivid_, mseed(m.type, m.seed), 0, 0.f, 0.f,
            0.6f * lit, 0.f, &m.lit, ang);
    }
    flush();
    // their light: a soft halo around each lit marble, glints on the fronts and the hard hits
    for (const Marble& m : marbles_) {
        if (m.delay > 0.f) continue;
        const float lit = m.light + m.twinkle * 0.8f + (slow_ > 0.01f ? 0.25f * slow_ : 0.f);
        if (lit < 0.03f) continue;
        putFx(m.p.x, m.p.y, m.r * 2.4f, 0.f, lerp(pal_.piece[m.type], pal_.wave, 0.3f), 0.18f * lit * pal_.halo * saturate(m.fade) * world * (1.f - hudShade(m.p)), 0);
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
            put(p.x, p.y, TILE_H, TILE_R, pal_.piece[std::clamp((int)c.type, 0, 6)], 1.f, s.rot, glow, mseed(c.type, s.seed), 1, 0.f, bounceOf(s), s.flare);
        }
    // Tiles popping (their marble is gone), or waiting for their turn at game over.
    for (const Pop& p : pops_) {
        if (p.t < p.delay) {
            put(p.x, p.y, TILE_H, TILE_R, pal_.piece[p.type], 1.f, 0.f, glow, mseed(p.type, hashf(p.x, p.y)), 1);
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
            put(w.x, w.y, TILE_H, TILE_R, pal_.piece[type], 1.f, hashf((float)nextId_, i + 10.f) * TAU, glow, mseed(type, hashf((float)nextId_, (float)i)), 1);
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
// widely spaced small capitals in Outfit Medium for the labels, the numbers in Medium. Set straight on the table,
// pale on its dark ground.
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
            const vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? pal_.accent : lerp(pal_.felt, pal_.fiber, 0.4f);
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
    // the table is dark everywhere: pale ink
    auto inkAt = [&](float, float) {
        const vec3 g = lerp(lerp(pal_.felt, pal_.fiber, 0.4f), pal_.night, 0.85f * slow_);
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
    // Where the HUD lies (board space): its columns and its other lines of text, where the marbles rolling under it
    // are dimmed.
    keepOut_.clear();
    keepOut_.push_back({layout::LEFT_COLUMN[0], layout::LEFT_COLUMN[2], layout::LEFT_COLUMN[1], layout::LEFT_COLUMN[3]});
    keepOut_.push_back({layout::RIGHT_COLUMN[0] + 0.6f, layout::RIGHT_COLUMN[2], layout::RIGHT_COLUMN[1], layout::RIGHT_COLUMN[3]});
    for (const HudText& h : f.hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        if (h.role == HUD_HELP || h.role == HUD_DIALOG || h.role == HUD_TOAST || h.role == HUD_TOAST_SUB) continue;
        const float base = h.scale * 12.f;
        const float w = (float)h.text.size() * base * (h.role == HUD_UNIVERSE ? 1.05f : 0.75f);
        const float x0 = ugl::hudLeft(h, w, width_), y0 = h.y - base * 0.3f, y1 = h.y + base * 1.6f;
        const float bx0 = viewC_.x + (x0 / width_ * 2.f - 1.f) * viewH_.x, bx1 = viewC_.x + ((x0 + w) / width_ * 2.f - 1.f) * viewH_.x;
        const float byTop = viewC_.y - (y0 / height_ * 2.f - 1.f) * viewH_.y, byBot = viewC_.y - (y1 / height_ * 2.f - 1.f) * viewH_.y;
        keepOut_.push_back({bx0, bx1, byBot, byTop});
    }
    // the section, from the HUD (a drop sends its waves, a chorus glimmers)
    sectionKnown_ = false;
    for (const HudText& h : f.hud)
        if (h.role == HUD_SECTION) {
            sectionKnown_ = true;
            sectionDrop_ = h.text == "DROP";
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

    traceLight(f.paused ? 0.f : dt, world);
    // ---- The traced light: shafts, additive, at half resolution, then its mipmaps (the scattered light).
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    lightT_.bind();
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!light_.empty()) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE);
        glUseProgram(progLight_);
        setMat(progLight_, "uVP", vp_);
        set1f(progLight_, "uPx", px_ * 2.f);
        glBindVertexArray(lightVao_);
        glBindBuffer(GL_ARRAY_BUFFER, lightVbo_);
        glBufferData(GL_ARRAY_BUFFER, light_.size() * sizeof(LightInst), light_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)light_.size());
        glDisable(GL_BLEND);
    }
    glBindTexture(GL_TEXTURE_2D, lightT_.tex);
    glGenerateMipmap(GL_TEXTURE_2D);

    // ---- The dye on the felt: washed out for a new song, the new stains painted over the old ones.
    stainT_.bind();
    if (washNow_) {
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
        washNow_ = false;
        washT_ = 0.f;
    } else if (washT_ > 0.f && !f.paused) {
        washT_ -= dt;
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(progWash_);
        set1f(progWash_, "uK", washT_ <= 0.f ? 1.f : 1.f - std::exp(-dt * 2.2f));
        ugl::drawFullscreen();
        glDisable(GL_BLEND);
    }
    if (!stains_.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(progStain_);
        setMat(progStain_, "uVP", vp_);
        glBindVertexArray(stainVao_);
        glBindBuffer(GL_ARRAY_BUFFER, stainVbo_);
        glBufferData(GL_ARRAY_BUFFER, stains_.size() * sizeof(StainInst), stains_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)stains_.size());
        glDisable(GL_BLEND);
        stains_.clear();
    }

    bgT_.bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    // ---- The table: its surface, the dye, the light on it.
    {
        glUseProgram(progSky_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, lightT_.tex);
        set1i(progSky_, "uLight", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, stainT_.tex);
        set1i(progSky_, "uStain", 1);
        glActiveTexture(GL_TEXTURE0);
        set4f(progSky_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set3f(progSky_, "uFelt", pal_.felt);
        set3f(progSky_, "uFiber", pal_.fiber);
        set3f(progSky_, "uNight", pal_.night);
        set3f(progSky_, "uBridgeCol", bridge_.color);
        set3f(progSky_, "uGlowCol", lerp(pal_.accent, pal_.wave, drive_ * 0.5f));
        set1f(progSky_, "uKind", pal_.surface);
        set1f(progSky_, "uGrain", pal_.grain);
        set1f(progSky_, "uDye", pal_.dye * lerpf(0.85f, 1.15f, vivid_));
        set1f(progSky_, "uGlow", (glow_ + 0.18f * swell_) * world);
        set1f(progSky_, "uSlow", slow_);
        set1f(progSky_, "uBridge", smoothstepf(0.05f, 0.85f, bridge));
        set1f(progSky_, "uSeed", seedF_);
        set1f(progSky_, "uTime", time_);
        set1f(progSky_, "uLightK", 9.f);
        set1f(progSky_, "uAmb", 0.6f);
        set1f(progSky_, "uSheen", pal_.surface > 1.5f ? 0.08f : (pal_.surface > 0.5f ? 0.06f : 0.04f));
        set4f(progSky_, "uColL", layout::LEFT_COLUMN[0], layout::LEFT_COLUMN[1], layout::LEFT_COLUMN[2], layout::LEFT_COLUMN[3]);
        set4f(progSky_, "uColR", layout::RIGHT_COLUMN[0], layout::RIGHT_COLUMN[1], layout::RIGHT_COLUMN[2], layout::RIGHT_COLUMN[3]);
        ugl::drawFullscreen();
    }
    // the frame starts from the table (kept apart, for the marbles to refract)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, bgT_.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneT_.fbo);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    sceneT_.bind();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // ---- The marbles on the table.
    drawWorld(world);
    // ---- The well, the stack, the falling piece.
    drawBoard(game, world);
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
                mseed(type, hashf((float)type + 0.5f, (float)i)), 1);
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
            put(x, y, 0.f, 0.15f, pal_.piece[i % 7], on * ga, (float)i, 0.15f * ready + 0.8f * run, mseed(i % 7, hashf((float)i, 3.f)), 0, 0.f, 0.f, run, 1.f);
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
        "MARBLES", "glass marbles in square tiles, shot out across a dark table where they collide and stain the felt",
        [] { return std::unique_ptr<Universe>(new MarblesUniverse()); },
        // Songs with a real drop and some density suit it best (the table fills up and the waves have a stage).
        [](const SongLook& l) { return 0.75f + 0.25f * l.density + 0.2f * l.dynamics; },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
