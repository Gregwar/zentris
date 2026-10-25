// SYNTHWAVE: a night drive, shot like a film.
//
// One world, one light: a soft sun (or moon) low on the horizon to one side, deep haze, a wet road surface that
// mirrors it in a long broken column, a few fine grid lines running into the fog, street lamps passing. The well
// is a slab of smoked frosted glass with hairline rails lit from below; the pieces are dark frosted glass lit from
// inside (a soft inner glow and a thin bright rim). Finished like film: bloom with a little halation, anamorphic
// streaks on the brightest lights, a filmic curve with rich blacks, a fine grain, a vignette.
//
// The song drives the drive: calm is a quiet empty road at night (the sun nearly set, thick fog, the floor still),
// mid moves forward with the tempo (the lines scroll, the lamps pass, light pulses run into the fog on the beat),
// peak is the fast drive (head- and tail-light trails, the haze lit up, flares blooming on the kicks). A lock
// flares the piece's rim, a cleared row leaves as a light trail, a Tetris flares the sun and the lens, slow motion
// is a time stretch (letterbox, the night drained of color, longer light trails), game over turns the lights off
// one by one while the camera tilts down into the fog. It leaves and enters through the fog.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane (like SUMI), so the
// well, the previews and the HUD anchors sit exactly where the layout puts them.
#include "universes/synthwave/synthwave.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "layout.hpp"
#include "ugl.hpp"
#include "universes/registry.hpp"
#include "universes/synthwave/shaders.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Sora (a geometric sans), SIL Open Font License, see OFL-Sora.txt beside this file.
const unsigned char FONT_TTF[] = {
#include "universes/synthwave/Sora.ttf.inc"
};

namespace shaders = synthwave_shaders;
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

constexpr float HORIZON = -4.3f;         // board y of the horizon
constexpr float GA = 1.15f, GB = 18.f;   // the floor's perspective (as in WORLD_FS)
constexpr float WELL_X = 5.1f, WELL_BOT = -10.1f, WELL_TOP = 10.08f;

vec2 v2(float x, float y) { return vec2{x, y}; }
vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
vec2 operator*(vec2 a, float k) { return {a.x * k, a.y * k}; }

vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }

// The variants: each a night, graded like a film (sRGB here, linear once loaded). Fields: the sky above, the haze
// on the horizon, the sun's core and limb, the road, the grid's lines, the accent (HUD, gauge), the lens streak
// tint, the lamps, the seven piece lights (I O T S Z J L), then fog density, rain, street lamps, a city skyline,
// a moon instead of the sun, stars.
struct VariantDef {
    const char* name;
    unsigned skyTop, haze, sunCore, sunEdge, floor, grid, accent, flare, lamp;
    unsigned piece[7];
    float fog, rain, lamps, skyline, moon, stars;
};
const VariantDef VARIANTS[] = {
    {"MIDNIGHT", 0x07050d, 0x3c1442, 0xffd9ea, 0xff2f86, 0x08060c, 0x2fc9c6, 0xff5fa8, 0x6aa8ff, 0xffd2ea,
     {0x3fe3dd, 0xffc75e, 0xb27bff, 0x63f0a6, 0xff5079, 0x5b7dff, 0xff8f4f}, 1.0f, 0.f, 0.6f, 0.f, 0.f, 0.4f},
    {"SODIUM", 0x070504, 0x3d200b, 0xfff1cc, 0xff8618, 0x090604, 0xffa646, 0xffb54c, 0xffa860, 0xffa040,
     {0x4fd2ff, 0xffd668, 0xe36fc6, 0xa6e86a, 0xff5139, 0x6688ff, 0xff9338}, 1.1f, 0.f, 1.0f, 0.f, 0.f, 0.f},
    {"MOONLIGHT", 0x03050b, 0x16253d, 0xf1f6ff, 0x7aa6ff, 0x04060b, 0x5f8fff, 0x9cc6ff, 0x7aaeff, 0xcfe0ff,
     {0x66e0ff, 0xf0e391, 0xa592ff, 0x76efc2, 0xff6d8c, 0x5f80ff, 0xffa676}, 1.0f, 0.f, 0.35f, 0.f, 1.f, 1.0f},
    {"TAIL LIGHTS", 0x060203, 0x320b12, 0xffbca8, 0xe01830, 0x080304, 0xff3549, 0xff4152, 0xff5c66, 0xffb0a0,
     {0x55d6ff, 0xffc456, 0xd468ff, 0x66ef95, 0xff3d50, 0x6d74ff, 0xff8d3e}, 1.25f, 1.f, 0.5f, 0.f, 0.f, 0.f},
    {"VIOLET FOG", 0x07040f, 0x2e1b4e, 0xf7dcff, 0x9a4cff, 0x07050e, 0xa477ff, 0xc896ff, 0x9a90ff, 0xe6d0ff,
     {0x52e2ff, 0xffd36b, 0xd27bff, 0x6af0b0, 0xff5a8a, 0x6c7bff, 0xff9a5c}, 1.7f, 0.f, 0.4f, 0.f, 0.f, 0.2f},
    {"MINT DAWN", 0x0a1519, 0x2c5552, 0xfffbe8, 0xffa585, 0x071113, 0x6cecc6, 0x84f4d4, 0xffcfb0, 0xfff0d8,
     {0x5ae3ff, 0xffd977, 0xc58cff, 0x74f0b6, 0xff6a7c, 0x6b8cff, 0xffa25e}, 1.3f, 0.f, 0.3f, 0.f, 0.f, 0.f},
    {"CITY LIGHTS", 0x03060c, 0x112a3c, 0xd9fbff, 0x28b6e6, 0x04070c, 0x31bfff, 0x3fdcff, 0x5ac6ff, 0xffd9a8,
     {0x45e6ff, 0xffd05c, 0xc36dff, 0x5ff0a0, 0xff4f7a, 0x5c7aff, 0xff9447}, 0.95f, 0.f, 0.5f, 1.f, 0.f, 0.15f},
    {"ROSE DUSK", 0x0d0a1e, 0x52223e, 0xffe9c4, 0xff6a4e, 0x0a0710, 0xff8aa0, 0xffa07a, 0xffa890, 0xffd6b8,
     {0x4fdcff, 0xffcf5a, 0xc86cff, 0x6bed9a, 0xff4b62, 0x6476ff, 0xff9248}, 1.05f, 0.f, 0.45f, 0.f, 0.f, 0.3f},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

SynthwaveUniverse::Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    auto L = [](unsigned c) { return ugl::toLinear(hex(c)); };
    SynthwaveUniverse::Palette p;
    p.skyTop = L(d.skyTop); p.haze = L(d.haze); p.sunCore = L(d.sunCore); p.sunEdge = L(d.sunEdge);
    p.floor = L(d.floor); p.grid = L(d.grid); p.accent = L(d.accent); p.flare = L(d.flare); p.lamp = L(d.lamp);
    for (int i = 0; i < 7; i++) p.piece[i] = L(d.piece[i]);
    p.fog = d.fog; p.rain = d.rain; p.lamps = d.lamps; p.skyline = d.skyline; p.moon = d.moon; p.stars = d.stars;
    return p;
}

void lerpPalette(SynthwaveUniverse::Palette& a, const SynthwaveUniverse::Palette& b, float k) {
    vec3* pa[] = {&a.skyTop, &a.haze, &a.sunCore, &a.sunEdge, &a.floor, &a.grid, &a.accent, &a.flare, &a.lamp};
    const vec3* pb[] = {&b.skyTop, &b.haze, &b.sunCore, &b.sunEdge, &b.floor, &b.grid, &b.accent, &b.flare, &b.lamp};
    for (int i = 0; i < 9; i++) *pa[i] = lerp(*pa[i], *pb[i], k);
    for (int i = 0; i < 7; i++) a.piece[i] = lerp(a.piece[i], b.piece[i], k);
    float* fa[] = {&a.fog, &a.rain, &a.lamps, &a.skyline, &a.moon, &a.stars};
    const float* fb[] = {&b.fog, &b.rain, &b.lamps, &b.skyline, &b.moon, &b.stars};
    for (int i = 0; i < 6; i++) *fa[i] = lerpf(*fa[i], *fb[i], k);
}

std::string upperCase(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool SynthwaveUniverse::init(int width, int height) {
    progWorld_ = compile(ugl::FULLSCREEN_VS, shaders::WORLD_FS, "synthwave world");
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "synthwave shape");
    progTile_ = compile(shaders::TILE_VS, shaders::TILE_FS, "synthwave tile");
    progLine_ = compile(shaders::LINE_VS, shaders::LINE_FS, "synthwave line");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "synthwave text");
    progDown_ = compile(ugl::FULLSCREEN_VS, shaders::DOWN_FS, "synthwave down");
    progUp_ = compile(ugl::FULLSCREEN_VS, shaders::UP_FS, "synthwave up");
    progStreak_ = compile(ugl::FULLSCREEN_VS, shaders::STREAK_FS, "synthwave streak");
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "synthwave post");

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
    instanced(tileVao_, tileVbo_, 4, sizeof(Tile));
    instanced(lineVao_, lineVbo_, 3, sizeof(Line));

    glGenVertexArrays(1, &shapeVao_);
    glGenBuffers(1, &shapeVbo_);
    glBindVertexArray(shapeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)(sizeof(float) * 2));

    glGenVertexArrays(1, &textVao_);
    glGenBuffers(1, &textVbo_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 5, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 5, (void*)(sizeof(float) * 2));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float) * 5, (void*)(sizeof(float) * 4));
    glBindVertexArray(0);

    buildFont(font_, FONT_TTF);
    pal_ = paletteOf(0);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// Glyphs baked as signed distance fields: they scale cleanly, and the distance gives soft shadows and glows.
void SynthwaveUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] synthwave font init failed\n");
        delete info;
        return;
    }
    f.info = info;
    f.data = ttf;
    f.px = 64.f;
    f.scaleTT = stbtt_ScaleForPixelHeight(info, f.px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    f.ascent = asc * f.scaleTT;
    int x0, y0, x1, y1;
    if (stbtt_GetCodepointBox(info, 'H', &x0, &y0, &x1, &y1)) f.capH = y1 * f.scaleTT;
    const int AW = 1024, AH = 1024, PAD = 10;
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

void SynthwaveUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    const float aspect = (float)width_ / height_;
    const layout::Camera cam = layout::camera(width_, height_);
    const float hh = cam.dist * std::tan(cam.fovDeg * 0.5f * PI / 180.f), hw = hh * aspect;
    viewH_ = v2(hw, hh);
    viewC_ = v2(cam.target.x, cam.target.y);
    vp_ = mat4::ortho(viewC_.x - hw, viewC_.x + hw, viewC_.y - hh, viewC_.y + hh, -1.f, 1.f);
    px_ = 2.f * hh / height_;
    createTargets();
}

void SynthwaveUniverse::destroyTargets() {
    if (msFbo_) { glDeleteFramebuffers(1, &msFbo_); glDeleteRenderbuffers(1, &msColor_); }
    if (sceneFbo_) { glDeleteFramebuffers(1, &sceneFbo_); glDeleteTextures(1, &sceneTex_); }
    for (int i = 0; i < BLOOM_MIPS; i++)
        if (bloomFbo_[i]) { glDeleteFramebuffers(1, &bloomFbo_[i]); glDeleteTextures(1, &bloomTex_[i]); }
    for (int i = 0; i < 2; i++)
        if (streakFbo_[i]) { glDeleteFramebuffers(1, &streakFbo_[i]); glDeleteTextures(1, &streakTex_[i]); }
    msFbo_ = msColor_ = sceneFbo_ = sceneTex_ = 0;
    for (int i = 0; i < BLOOM_MIPS; i++) bloomFbo_[i] = bloomTex_[i] = 0;
    for (int i = 0; i < 2; i++) streakFbo_[i] = streakTex_[i] = 0;
}

void SynthwaveUniverse::createTargets() {
    destroyTargets();
    auto makeTex = [](GLuint& fbo, GLuint& tex, int w, int h) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    };
    makeTex(sceneFbo_, sceneTex_, width_, height_);
    // Multisampled scene (tiles and lines get clean edges), resolved into sceneTex_.
    GLint maxS = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxS);
    samples_ = std::min(4, (int)maxS);
    if (samples_ > 1) {
        glGenRenderbuffers(1, &msColor_);
        glBindRenderbuffer(GL_RENDERBUFFER, msColor_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA16F, width_, height_);
        glGenFramebuffers(1, &msFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &msFbo_);
            glDeleteRenderbuffers(1, &msColor_);
            msFbo_ = msColor_ = 0;
            samples_ = 0;
        }
    }
    int w = width_, h = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        bloomW_[i] = w;
        bloomH_[i] = h;
        makeTex(bloomFbo_[i], bloomTex_[i], w, h);
    }
    // The anamorphic streaks: a quarter of the width, an eighth of the height.
    streakW_ = std::max(1, width_ / 4);
    streakH_ = std::max(1, height_ / 8);
    for (int i = 0; i < 2; i++) makeTex(streakFbo_[i], streakTex_[i], streakW_, streakH_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

SynthwaveUniverse::~SynthwaveUniverse() {
    destroyTargets();
    for (GLuint p : {progWorld_, progShape_, progTile_, progLine_, progText_, progDown_, progUp_, progStreak_, progPost_})
        glDeleteProgram(p);
    for (GLuint v : {tileVao_, lineVao_, shapeVao_, textVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, tileVbo_, lineVbo_, shapeVbo_, textVbo_}) glDeleteBuffers(1, &b);
    if (font_.tex) glDeleteTextures(1, &font_.tex);
    delete (stbtt_fontinfo*)font_.info;
}

// =====================================================================================================
// Song, level, bonus
// =====================================================================================================

// The variant from the song: the key's hue picks the family, dark minor songs go to the deepest nights (moon,
// rain, violet fog), bright major ones to the dawns and dusks.
int SynthwaveUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    const bool night = t.hasSong && t.minor && t.brightness < 0.45f;
    const bool soft = t.hasSong && !t.minor && t.brightness > 0.55f;
    int v;
    if (night) v = (h < 0.2f || h > 0.85f) ? 3 : (h < 0.6f ? 2 : 4);
    else if (soft) v = (h < 0.35f) ? 7 : (h < 0.65f ? 5 : 0);
    else if (h < 0.1f || h >= 0.88f) v = 0;
    else if (h < 0.22f) v = 1;
    else if (h < 0.38f) v = 7;
    else if (h < 0.55f) v = 6;
    else if (h < 0.7f) v = 2;
    else v = (t.seed & 1) ? 4 : 0;
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void SynthwaveUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    seedF_ = (float)(look.seed % 1000) * 0.137f;
    rng_ = Rng(look.seed ^ 0x5717ull);
    sunSide_ = ((look.seed >> 3) & 1) ? 1.f : -1.f;
    if (seconds <= 0.f) {
        palSet_ = false; // snap on the first frame
        sunSideShown_ = sunSide_;
    } else {
        horizonFlash_ = 1.f;
        glintBurst(2, 1.f);
    }
}

void SynthwaveUniverse::setLevel(int level, float energy, float seconds, float impact) {
    level = std::clamp(level, 0, 2);
    energy_ = energy;
    if (!hasLevel_ || seconds < 0.1f) {
        hasLevel_ = true;
        level_ = level;
        lv_ = lvFrom_ = lvTo_ = (float)level;
        lvT_ = 1.f;
        return;
    }
    const bool rising = level > level_;
    level_ = level;
    lvFrom_ = lv_;
    lvTo_ = (float)level;
    lvT_ = 0.f;
    lvDur_ = std::clamp(seconds, rising ? 0.6f : 1.4f, 4.f);
    if (rising) {
        horizonFlash_ = std::max(horizonFlash_, 0.4f + 0.5f * impact);
        if (level >= 2) {
            flash_ = std::max(flash_, 0.3f + 0.5f * impact);
            flareKick_ = std::max(flareKick_, 0.6f + 0.6f * impact);
            glintBurst(1 + (int)(2 * impact), 1.f);
        }
    }
}

void SynthwaveUniverse::refresh(float strength) {
    horizonFlash_ = std::max(horizonFlash_, 0.4f * strength);
    if (strength > 0.4f) flareKick_ = std::max(flareKick_, 0.4f * strength);
}

void SynthwaveUniverse::setBonus(bool on) {
    if (on == slowOn_) return;
    slowOn_ = on;
    slowT_ = 0.f;
}

void SynthwaveUniverse::levelUp() {
    sunFlare_ = std::max(sunFlare_, 0.5f);
    horizonFlash_ = 1.f;
    glintBurst(2, 1.f);
}

vec2 SynthwaveUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints SynthwaveUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    h.background = p.skyTop + p.haze * 0.25f;
    h.ink = lerp(p.grid, vec3(1.f), 0.2f) * 0.8f;
    h.accent = p.sunEdge;
    h.light = false;
    h.styles = TS_LUMA | TS_INK | TS_RIPPLE; // the lights last, a glowing fog front, a ring in the haze
    return h;
}

float SynthwaveUniverse::horizon() const {
    // game over: the camera tilts down into the fog (the horizon climbs the screen)
    const float tilt = overT_ >= 0.f ? smoothstepf(0.2f, 2.2f, overT_) : 0.f;
    return HORIZON + 7.f * tilt;
}

vec2 SynthwaveUniverse::sunPos() const {
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float R = lerpf(4.3f, 1.7f, pal_.moon);
    const float x = sunSideShown_ * std::clamp(viewH_.x * 0.68f, 13.5f, 17.f);
    const float rise = lerpf(lerpf(-0.42f, 0.02f, wm), -0.08f, wp);
    const float sunY = horizon() + R * rise;
    const float moonY = horizon() + lerpf(5.4f, 6.4f, wm);
    return v2(x, lerpf(sunY, moonY, pal_.moon));
}

// Anamorphic glints: on the well's rail tops and the sun's rim.
void SynthwaveUniverse::glintBurst(int n, float spread) {
    const vec2 sp = sunPos();
    const float R = lerpf(4.3f, 1.7f, pal_.moon);
    for (int i = 0; i < n; i++) {
        Glint g;
        if (rng_.chance(0.5f)) {
            g.p = v2(rng_.chance(0.5f) ? -WELL_X : WELL_X, WELL_BOT);
        } else {
            const float a = rng_.range(0.3f, PI - 0.3f);
            g.p = v2(sp.x + std::cos(a) * R, std::max(sp.y + std::sin(a) * R, HORIZON + 0.2f));
        }
        g.t = -rng_.range(0.f, 0.25f * spread);
        g.life = rng_.range(0.6f, 1.0f);
        g.size = rng_.range(0.5f, 1.0f);
        g.col = lerp(pal_.flare, vec3(1.f), 0.5f);
        glints_.push_back(g);
    }
}

// =====================================================================================================
// Game events
// =====================================================================================================

void SynthwaveUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::Lock: {
        if (ev.cells.empty()) break;
        // The piece's outer contour flares.
        auto inPiece = [&](int x, int y) {
            for (auto& o : ev.cells)
                if (o.x == x && o.y == y) return true;
            return false;
        };
        Flare f;
        f.t = 0;
        f.col = pal_.piece[std::clamp(ev.cells[0].type, 0, 6)];
        const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (auto& q : ev.cells) {
            vec3 p = layout::cellPos((float)q.x, (float)q.y);
            for (int d = 0; d < 4; d++) {
                if (inPiece(q.x + dx[d], q.y + dy[d])) continue;
                vec2 n = v2((float)dx[d], (float)-dy[d]), t = v2(-n.y, n.x);
                vec2 c = v2(p.x, p.y) + n * 0.47f;
                f.seg.push_back(c - t * 0.47f);
                f.seg.push_back(c + t * 0.47f);
            }
        }
        flares_.push_back(std::move(f));
        break;
    }
    case GameEvent::HardDrop: {
        if (ev.count < 2) break;
        for (auto& q : ev.cells) {
            bool top = true;
            for (auto& o : ev.cells)
                if (o.x == q.x && o.y < q.y) top = false;
            if (!top) continue;
            Streak s;
            s.x = layout::cellPos((float)q.x, 0).x;
            s.y0 = layout::cellPos(0, (float)(q.y - ev.count)).y + 0.5f;
            s.y1 = layout::cellPos(0, (float)q.y).y + 0.5f;
            s.t = 0;
            s.col = pal_.piece[std::clamp(q.type, 0, 6)];
            streaks_.push_back(s);
        }
        break;
    }
    case GameEvent::Clear: {
        const bool tetris = ev.count >= 4;
        std::vector<int> rows;
        for (auto& c : ev.cells)
            if (std::find(rows.begin(), rows.end(), c.y) == rows.end()) rows.push_back(c.y);
        std::sort(rows.begin(), rows.end());
        // the rows leave toward the sun, like traffic toward the light
        const float dir0 = sunSide_;
        for (size_t i = 0; i < rows.size(); i++) rows_.push_back({layout::cellPos(0, (float)rows[i]).y, -0.04f * i, dir0});
        for (auto& c : ev.cells) {
            size_t ri = std::find(rows.begin(), rows.end(), c.y) - rows.begin();
            vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0; d.dir = dir0; d.type = std::clamp(c.type, 0, 6);
            d.delay = (dir0 > 0 ? (9 - c.x) : c.x) * 0.012f + ri * 0.04f;
            d.out = false;
            dying_.push_back(d);
        }
        if (tetris) {
            sunFlare_ = 1.f;
            tetrisT_ = 0.f;
            flash_ = std::max(flash_, 0.5f);
            flareKick_ = std::max(flareKick_, 0.8f);
            glintBurst(3, 1.5f);
        } else {
            horizonFlash_ = std::max(horizonFlash_, 0.2f * ev.count);
        }
        break;
    }
    case GameEvent::BonusReady:
        readyT_ = 0.f;
        glints_.push_back({v2(layout::GAUGE_CENTER.x, layout::GAUGE_CENTER.y + layout::GAUGE_R), 0.f, 0.9f, 1.0f,
                           lerp(pal_.accent, vec3(1.f), 0.4f)});
        break;
    case GameEvent::TopOut: {
        // The lights go out, row by row from the top, while the camera tilts down into the fog.
        int topRow = Game::H;
        for (auto& c : ev.cells) topRow = std::min(topRow, c.y);
        for (auto& c : ev.cells) {
            vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0; d.dir = 0; d.type = std::clamp(c.type, 0, 6);
            d.delay = (c.y - topRow) * 0.05f + rng_.range(0.f, 0.1f);
            d.out = true;
            dying_.push_back(d);
        }
        overT_ = 0.f;
        break;
    }
    default: break;
    }
}

// =====================================================================================================
// Simulation
// =====================================================================================================

void SynthwaveUniverse::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    worldTime_ += wdt;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    sunSideShown_ = approach(sunSideShown_, sunSide_, 0.6f, dt);

    // The road moves with the tempo: nearly still when calm, a slow drive at mid, fast at peak, with a gentle
    // surge on each beat.
    {
        const float ph = music.beatPhase;
        if (ph < lastBeatPhase_ - 0.5f) beats_ += 1.f;
        lastBeatPhase_ = ph;
        const float e = 1.f - std::pow(1.f - ph, 2.2f);
        const float eased = beats_ + lerpf(ph, e, 0.5f * wm);
        float d = lastEased_ < 0.f ? 0.f : eased - lastEased_;
        if (d < 0.f || d > 2.f) d = 0.f;
        lastEased_ = eased;
        const float cpb = 0.04f + 0.4f * wm + 0.55f * wp;
        if (music.hasSong) scroll_ += d * cpb * (wdt / std::max(dt, 1e-4f));
        else scroll_ += wdt * (0.1f + 0.6f * wm);
    }

    // Kicks: flares bloom and the haze lights up (peak); light pulses run into the fog on the beats (mid).
    sinceKick_ += dt;
    sinceGlint_ += dt;
    const float kick = std::max(music.kick, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    auto pushKick = [&](float amp) {
        kickAge_[kickNext_] = 0.f;
        kickAmp_[kickNext_] = amp;
        kickNext_ = (kickNext_ + 1) % 4;
        sinceKick_ = 0.f;
    };
    if (kick > 0.55f && prevKick_ <= 0.55f && sinceKick_ > 0.18f && wp > 0.3f) {
        pushKick(0.5f + 0.5f * wp);
        flareKick_ = std::max(flareKick_, (0.35f + 0.5f * kick) * wp);
        hazeLit_ = std::max(hazeLit_, 0.5f + 0.4f * kick * wp);
        if (kick > 0.85f && sinceGlint_ > 2.f) {
            glintBurst(1, 0.3f);
            sinceGlint_ = 0.f;
        }
    } else if (music.hasSong && wm > 0.5f && wp < 0.3f && music.beatPulse > 0.9f && sinceKick_ > 0.3f) {
        pushKick(0.35f);
    }
    prevKick_ = kick;
    for (int i = 0; i < 4; i++) kickAge_[i] += wdt;

    cars_ = approach(cars_, wp, 1.0f, dt);
    hazeLit_ = approach(hazeLit_, 0.12f * wm + 0.3f * wp + 0.5f * horizonFlash_, 2.0f, dt);
    flareKick_ = approach(flareKick_, 0.f, 2.6f, dt);
    flash_ = approach(flash_, 0.f, 3.f, dt);
    sunFlare_ = approach(sunFlare_, 0.f, 1.1f, dt);
    horizonFlash_ = approach(horizonFlash_, 0.f, 1.8f, dt);
    tetrisT_ += dt;

    for (auto& f : flares_) f.t += dt;
    flares_.erase(std::remove_if(flares_.begin(), flares_.end(), [](const Flare& f) { return f.t > 0.7f; }), flares_.end());
    for (auto& s : streaks_) s.t += dt;
    streaks_.erase(std::remove_if(streaks_.begin(), streaks_.end(), [](const Streak& s) { return s.t > 0.4f; }), streaks_.end());
    for (auto& r : rows_) r.t += dt;
    rows_.erase(std::remove_if(rows_.begin(), rows_.end(), [](const RowGo& r) { return r.t > 1.0f; }), rows_.end());
    for (auto& d : dying_) d.t += dt;
    const bool over = overT_ >= 0.f;
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(),
                                [over](const Dying& d) { return d.out ? !over : d.t > d.delay + 0.6f; }),
                 dying_.end());
    for (auto& g : glints_) g.t += dt;
    glints_.erase(std::remove_if(glints_.begin(), glints_.end(), [](const Glint& g) { return g.t > g.life; }), glints_.end());

    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
    readyT_ += dt;
}

// =====================================================================================================
// Geometry builders
// =====================================================================================================

void SynthwaveUniverse::tile(float x, float y, float hw, float hh, const vec3& c, float alpha, int kind, float age,
                             float aux, float e0, float e1, float e2, float e3) {
    Tile t{{x, y, hw, hh}, {c.x, c.y, c.z, alpha}, {(float)kind, age, aux, 0.f}, {e0, e1, e2, e3}};
    tiles_.push_back(t);
}

void SynthwaveUniverse::line(std::vector<Line>& out, vec2 a, vec2 b, const vec3& c, float width, float glow, float alpha) {
    if (alpha <= 0.003f) return;
    Line l{{a.x, a.y, b.x, b.y}, {c.x, c.y, c.z, 0.f}, {width, glow, 0.f, alpha}};
    out.push_back(l);
}

void SynthwaveUniverse::glint(std::vector<Line>& out, vec2 c, float size, const vec3& col, float alpha) {
    if (alpha <= 0.003f) return;
    Line l{{c.x, c.y, c.x, c.y}, {col.x, col.y, col.z, 0.f}, {size, 0.f, 1.f, alpha}};
    out.push_back(l);
}

void SynthwaveUniverse::light(std::vector<Line>& out, vec2 c, float radius, const vec3& col, float alpha) {
    if (alpha <= 0.003f) return;
    Line l{{c.x, c.y, c.x, c.y}, {col.x, col.y, col.z, 0.f}, {radius, 0.f, 2.f, alpha}};
    out.push_back(l);
}

void SynthwaveUniverse::tri(std::vector<Vert>& out, vec2 a, vec2 b, vec2 c, const vec3& col, float alpha) {
    out.push_back({a.x, a.y, col.x, col.y, col.z, alpha});
    out.push_back({b.x, b.y, col.x, col.y, col.z, alpha});
    out.push_back({c.x, c.y, col.x, col.y, col.z, alpha});
}

void SynthwaveUniverse::quad(std::vector<Vert>& out, vec2 a, vec2 b, vec2 c, vec2 d, const vec3& col, float alpha) {
    tri(out, a, b, c, col, alpha);
    tri(out, a, c, d, col, alpha);
}

// Street lamps along the road, passing with the drive: a light on a pole, its long reflection on the wet floor,
// fading into the fog.
void SynthwaveUniverse::buildLamps(float alpha) {
    alpha *= pal_.lamps;
    if (alpha <= 0.01f) return;
    const float H = horizon();
    const float spacing = 3.f, X = 3.3f, pole = 1.9f;
    const float off = std::fmod(scroll_, spacing);
    const float wp = smoothstepf(1.1f, 2.f, lv_);
    for (int i = 1; i < 16; i++) {
        const float zr = i * spacing - off; // depth from the viewer
        if (zr < 1.2f) continue;
        const float d = GB / zr;
        const float vis = std::exp(-zr * pal_.fog * 0.085f) * smoothstepf(1.2f, 2.6f, zr);
        for (int side = -1; side <= 1; side += 2) {
            const float x = side * X * d / GA;
            const float a = alpha * vis * smoothstepf(WELL_X + 0.4f, WELL_X + 1.6f, std::fabs(x));
            if (a <= 0.01f) continue;
            const vec2 top = v2(x, H - d + pole * d / GA);
            const float r = 0.25f + 0.07f * d;
            light(lines_, top, r, pal_.lamp * (2.2f + 0.8f * wp), a);
            // its reflection on the wet road
            line(lines_, v2(x, H - d), top, pal_.skyTop + pal_.haze * 0.1f, 0.012f * d, 0.f, a * 0.5f);
            line(lines_, v2(x, H - d - 0.1f * d), v2(x, H - d - 0.8f * d), pal_.lamp * 0.05f, 0.002f, 0.06f * d, a);
        }
    }
}

void SynthwaveUniverse::buildBoard(const Game& g) {
    const float bridgeK = saturate(bridge_.amount);
    (void)bridgeK;
    // Locked cells.
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            const float age = c.flash > 0.f ? (1.f - c.flash) * Game::LOCK_FX_SECONDS : -1.f;
            tile(p.x, p.y, 0.5f, 0.5f, pal_.piece[c.type], 1.f, 0, age, 0.f, 1.f, 1.f);
        }
    // Cleared cells leaving as light trails; a topped-out stack whose lights go out.
    for (const Dying& d : dying_) {
        const float t = d.t - d.delay;
        if (d.out) {
            const float on = 1.f - smoothstepf(0.f, 0.45f, t);
            const float flick = t > 0.f && t < 0.2f ? (std::sin(t * 90.f) > 0.f ? 1.f : 0.45f) : 1.f;
            const float sink = 0.35f * smoothstepf(0.f, 2.5f, t);
            tile(d.x, d.y - sink, 0.5f, 0.5f, pal_.piece[d.type], 1.f - 0.5f * smoothstepf(0.5f, 3.f, t), 0, -1.f, 0.f,
                 1.f, on * flick);
            continue;
        }
        if (t < 0.f) {
            tile(d.x, d.y, 0.5f, 0.5f, pal_.piece[d.type], 1.f, 0, 0.f, 0.f, 1.f, 1.f);
            continue;
        }
        const float k = saturate(t / 0.55f);
        const float head = d.x + d.dir * 18.f * k * k, tail = d.x + d.dir * 18.f * k * k * 0.1f;
        const float hw = std::fabs(head - tail) * 0.5f + 0.5f;
        tile((head + tail) * 0.5f, d.y, hw, 0.5f, pal_.piece[d.type], 1.f, 2, -1.f, 0.f, d.dir, k);
    }
    // The falling piece and its ghost.
    if (g.hasPiece()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        const vec3 col = pal_.piece[p.type];
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (auto& c : gc) {
                if (c[1] < Game::HIDDEN) continue;
                vec3 w = layout::cellPos((float)c[0], (float)c[1]);
                tile(w.x, w.y, 0.5f, 0.5f, col, 1.f, 1);
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (auto& c : cells) {
            if (c[1] < Game::HIDDEN - 1) continue;
            vec3 w = layout::cellPos(c[0] + dx, c[1] + dy);
            tile(w.x, w.y, 0.5f, 0.5f, col, 1.f, 3, -1.f, 0.f, 1.f, 1.f);
        }
    }
    // Previews.
    auto preview = [&](int type, vec2 center, float sc, float alpha) {
        if (type < 0) return;
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        g.pieceCells(p, cells);
        float minx = 9, maxx = -9, miny = 9, maxy = -9;
        for (auto& c : cells) {
            minx = std::min(minx, (float)c[0]); maxx = std::max(maxx, (float)c[0]);
            miny = std::min(miny, (float)c[1]); maxy = std::max(maxy, (float)c[1]);
        }
        for (auto& c : cells) {
            vec2 pos = v2(center.x + (c[0] - (minx + maxx) * 0.5f) * sc, center.y - (c[1] - (miny + maxy) * 0.5f) * sc);
            tile(pos.x, pos.y, 0.5f * sc, 0.5f * sc, pal_.piece[type], alpha, 3, -1.f, 0.f, sc, 1.f);
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.35f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), i == 0 ? 1.f : 0.7f);
    }
    // The bonus gauge: a ring with a light rising in it.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        const float ready = g.bonusReady() ? 1.f : 0.f, active = g.bonusActive() ? 1.f : 0.f;
        tile(gc.x, gc.y, R * 1.6f, R * 1.6f, vec3(1.f), 1.f, 4, -1.f, 0.f, saturate(gaugeShown_), ready, active, R);
    }
}

void SynthwaveUniverse::buildFx() {
    const float world = 1.f - saturate(bridge_.amount);
    for (const Flare& f : flares_) {
        const float k = f.t / 0.7f;
        const float a = (1.f - k) * (1.f - k) * world * 0.7f;
        const vec3 c = lerp(f.col, vec3(1.f), 0.3f) * 1.6f;
        for (size_t i = 0; i + 1 < f.seg.size(); i += 2) line(fxLines_, f.seg[i], f.seg[i + 1], c, 0.035f, 0.12f + 0.2f * k, a);
    }
    for (const Streak& s : streaks_) {
        const float k = s.t / 0.4f;
        line(fxLines_, v2(s.x, s.y0 + (s.y1 - s.y0) * k * 0.7f), v2(s.x, s.y1), s.col * 1.3f, 0.03f, 0.1f, (1.f - k) * 0.7f * world);
    }
    // a cleared row: a long light trail running off toward the sun's side
    for (const RowGo& r : rows_) {
        if (r.t < 0.f) continue;
        const float k = saturate(r.t / 0.7f);
        const float head = -r.dir * 5.f + r.dir * 40.f * k * k + r.dir * 4.f * k;
        const float tail = head - r.dir * (4.f + 14.f * k);
        const float a = (1.f - smoothstepf(0.45f, 1.f, k)) * world;
        line(fxLines_, v2(tail, r.y), v2(head, r.y), lerp(pal_.flare, vec3(1.f), 0.5f) * 1.5f, 0.03f * (1.f - 0.5f * k), 0.14f, a);
    }
    for (const Glint& g : glints_) {
        if (g.t < 0.f) continue;
        const float k = g.t / g.life;
        const float a = std::sin(PI * saturate(k)) * world;
        glint(fxLines_, g.p, g.size * (0.7f + 0.5f * std::sin(PI * k)), g.col * 2.f, a);
    }
    // The Tetris: an anamorphic flare crossing the screen through the toast, a few lens ghosts toward the sun.
    if (tetrisT_ < 2.6f) {
        const float k = tetrisT_ / 2.6f;
        const float a = (tetrisT_ < 0.12f ? tetrisT_ / 0.12f : 1.f) * (1.f - smoothstepf(0.5f, 1.f, k)) * world;
        const vec2 c = v2(layout::TETRIS_TOAST.x - 3.f + 6.f * smoothstepf(0.f, 1.f, k), layout::TETRIS_TOAST.y - 0.3f);
        line(fxLines_, v2(c.x - 30.f, c.y), v2(c.x + 30.f, c.y), pal_.flare * 0.9f, 0.02f, 0.08f, a * 0.8f);
        glint(fxLines_, c, 2.0f, lerp(pal_.flare, vec3(1.f), 0.6f) * 2.f, a);
        const vec2 sp = sunPos();
        for (int i = 1; i <= 3; i++) {
            const vec2 q = c + (sp - c) * (-0.3f * i);
            light(fxLines_, q, 0.5f + 0.35f * i, lerp(pal_.flare, pal_.sunEdge, i / 3.f) * 0.12f, a * 0.6f);
        }
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float SynthwaveUniverse::textWidth(const std::string& s, float size, float track) const {
    const Font& f = font_;
    if (!f.ok || s.empty()) return s.size() * size * 0.6f;
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

float SynthwaveUniverse::textQuads(const std::string& s, float x, float y, float size, float track,
                                   std::vector<float>& out) const {
    const Font& f = font_;
    if (!f.ok) return 0.f;
    const stbtt_fontinfo* info = (const stbtt_fontinfo*)f.info;
    const float k = size / f.px;
    const float cap = f.capH / f.px * size;
    const float baseline = y + cap;
    float pen = x;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 32 || ch >= 128) ch = '?';
        const Glyph& g = f.g[ch - 32];
        if (g.w > 0) {
            const float x0 = pen + g.ox * k, y0 = baseline + g.oy * k, x1 = x0 + g.w * k, y1 = y0 + g.h * k;
            auto vert = [&](float vx, float vy, float u, float v) {
                out.push_back(vx);
                out.push_back(vy);
                out.push_back(u);
                out.push_back(v);
                out.push_back(0.f);
            };
            vert(x0, y0, g.x0, g.y0); vert(x1, y0, g.x1, g.y0); vert(x1, y1, g.x1, g.y1);
            vert(x0, y0, g.x0, g.y0); vert(x1, y1, g.x1, g.y1); vert(x0, y1, g.x0, g.y1);
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

void SynthwaveUniverse::drawText(const std::vector<float>& q, const TextStyle& st) {
    if (q.empty() || !font_.ok) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    set3f(progText_, "uColor", st.color);
    set3f(progText_, "uGlow", st.glow);
    set1f(progText_, "uAlpha", st.alpha);
    set1f(progText_, "uGlowAmt", st.glowAmt);
    set1f(progText_, "uShadow", st.shadow);
    set1f(progText_, "uBias", st.bias);
    set1f(progText_, "uBridge", smoothstepf(0.3f, 0.95f, bridge_.amount));
    set3f(progText_, "uBridgeInk", bridge_.ink);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(float), q.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 5));
}

// The HUD in SYNTHWAVE's typography: one clean geometric sans, small quiet labels widely tracked, crisp values,
// the variant's accent as the only color.
void SynthwaveUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    if (!panels.empty()) {
        std::vector<Vert> v;
        for (const HudRect& r : panels) {
            vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? pal_.accent * 0.6f : pal_.skyTop * 0.7f;
            quad(v, v2(r.x, r.y), v2(r.x + r.w, r.y), v2(r.x + r.w, r.y + r.h), v2(r.x, r.y + r.h), c,
                 r.accent || r.custom ? r.alpha : std::min(1.f, r.alpha * 1.2f));
        }
        glUseProgram(progShape_);
        set1i(progShape_, "uPixels", 1);
        set2f(progShape_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(shapeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vert), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
    }
    const vec3 white(1.05f, 1.03f, 1.08f);
    const vec3 quiet = lerp(vec3(0.62f, 0.62f, 0.7f), pal_.accent, 0.15f);
    const vec3 acc = lerp(pal_.accent, vec3(1.f), 0.15f) * 1.15f;
    std::vector<float> q;
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        TextStyle st;
        st.alpha = saturate(h.alpha);
        q.clear();
        auto place = [&](const std::string& text, float size, float track) {
            const float w = textWidth(text, size, track);
            const float x = ugl::hudLeft(h, w, width_);
            textQuads(text, x, h.y, size, track, q);
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            // "SYNTHWAVE" quiet and widely spaced, a hairline, the variant in the accent.
            std::string name = h.text, var;
            size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            const float sz = base * 0.72f, tr = sz * 0.3f;
            const float wName = textWidth(name, sz, tr);
            const float wVar = var.empty() ? 0.f : textWidth(var, sz, tr);
            const float gap = base * 1.9f;
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            const float y = h.y + base * 0.3f;
            textQuads(name, x, y, sz, tr, q);
            st.color = quiet; st.alpha = saturate(0.3f + h.alpha * 1.4f);
            drawText(q, st);
            if (!var.empty()) {
                q.clear();
                textQuads(var, x + wName + gap, y, sz, tr, q);
                TextStyle vs = st;
                vs.color = acc; vs.glow = pal_.accent * 0.5f; vs.glowAmt = 0.25f;
                drawText(q, vs);
                // the hairline between them
                std::vector<Vert> v;
                const float lx = x + wName + gap * 0.28f, ly = y + sz * 0.36f, lw = gap * 0.44f, th = std::max(1.f, 1.2f * s);
                quad(v, v2(lx, ly), v2(lx + lw, ly), v2(lx + lw, ly + th), v2(lx, ly + th), quiet, st.alpha * 0.7f);
                glUseProgram(progShape_);
                set1i(progShape_, "uPixels", 1);
                set2f(progShape_, "uRes", (float)width_, (float)height_);
                glBindVertexArray(shapeVao_);
                glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
                glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vert), v.data(), GL_STREAM_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
            }
            continue;
        }
        case HUD_SECTION:
            place(upperCase(h.text), base * 0.72f, base * 0.3f);
            st.color = acc; st.glow = pal_.accent * 0.5f; st.glowAmt = 0.25f;
            st.alpha = saturate(0.2f + h.alpha * 1.6f);
            break;
        case HUD_TITLE:
            place(h.text, base * 0.95f, base * 0.04f);
            st.color = white;
            break;
        case HUD_INFO:
            place(upperCase(h.text), base * 0.66f, base * 0.26f);
            st.color = quiet;
            break;
        case HUD_LABEL:
            place(upperCase(h.text), base * 0.62f, base * 0.36f);
            st.color = quiet; st.alpha = saturate(h.alpha * 0.9f);
            break;
        case HUD_VALUE:
            place(h.text, base * 1.05f, base * 0.05f);
            st.color = white; st.bias = 0.02f;
            break;
        case HUD_NOTICE:
            place(upperCase(h.text), base * 0.8f, base * 0.22f);
            st.color = acc; st.glow = pal_.accent * 0.6f; st.glowAmt = 0.35f;
            break;
        case HUD_KEY:
            place(h.text, base * 0.8f, base * 0.1f);
            st.color = white; st.shadow = 0.9f;
            break;
        case HUD_TOAST:
            place(upperCase(h.text), base * 0.92f, base * 0.42f);
            st.color = white * 1.2f; st.glow = pal_.flare * 0.7f; st.glowAmt = 0.45f;
            break;
        case HUD_TOAST_SUB:
            place(h.text, base * 0.8f, base * 0.2f);
            st.color = acc; st.glow = pal_.accent * 0.5f; st.glowAmt = 0.3f;
            break;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(upperCase(h.text), base * 0.85f, base * 0.3f);
                st.color = white; st.glow = pal_.flare * 0.5f; st.glowAmt = 0.3f;
            } else {
                place(h.text, base * 0.82f, base * 0.12f);
                st.color = h.accent ? acc : vec3(0.85f, 0.85f, 0.9f);
            }
            break;
        default: // hints, help, plain
            place(h.text, base * 0.78f, base * 0.08f);
            st.color = h.accent ? acc : vec3(0.75f, 0.75f, 0.82f);
            break;
        }
        drawText(q, st);
    }
}

// Slow motion's caption, in the lower letterbox bar: the speed and the time left, like a film's timecode.
void SynthwaveUniverse::drawOsd(const Game& game) {
    if (slow_ < 0.05f || !slowOn_ || bridge_.amount > 0.5f) return;
    const float s = height_ / 1080.f;
    char buf[64];
    const int left = (int)std::ceil(std::max(0.f, game.bonusLeft()));
    std::snprintf(buf, sizeof(buf), "0.25X   \xC2\xB7   00:%02d", left);
    std::string txt(buf);
    // the middle dot is not in the font's ASCII range: draw it as a spaced period
    size_t dot = txt.find("\xC2\xB7");
    if (dot != std::string::npos) txt.replace(dot, 2, "/");
    const float size = 15.f * s, tr = size * 0.45f;
    const float w = textWidth(txt, size, tr);
    const float bar = 0.075f * height_ * slow_;
    std::vector<float> q;
    textQuads(txt, width_ * 0.5f - w * 0.5f, height_ - bar * 0.5f - size * 0.36f, size, tr, q);
    TextStyle st;
    st.color = vec3(0.8f, 0.82f, 0.9f);
    st.alpha = saturate(slow_ * 1.5f - 0.3f);
    st.shadow = 0.f;
    drawText(q, st);
}

// =====================================================================================================
// Frame
// =====================================================================================================

void SynthwaveUniverse::drawWorld(const MusicState& music) {
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float over = overT_ >= 0.f ? smoothstepf(0.3f, 2.2f, overT_) : 0.f;
    const vec2 sp = sunPos();
    const float R = lerpf(4.3f, 1.7f, pal_.moon) * (1.f + 0.04f * sunFlare_);
    const float sunGain = (0.8f + 0.15f * wm + 0.15f * wp) * (1.f + 0.6f * sunFlare_ + 0.2f * flash_) * (1.f - 0.6f * over);
    glUseProgram(progWorld_);
    set4f(progWorld_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set1f(progWorld_, "uHorizon", horizon());
    set4f(progWorld_, "uSun", sp.x, sp.y, R, sunGain);
    set1f(progWorld_, "uMoon", pal_.moon);
    set1f(progWorld_, "uLv", lv_);
    set1f(progWorld_, "uTime", worldTime_);
    set1f(progWorld_, "uScroll", scroll_);
    set1f(progWorld_, "uPx", px_);
    set4f(progWorld_, "uKickAge", kickAge_[0], kickAge_[1], kickAge_[2], kickAge_[3]);
    set4f(progWorld_, "uKickAmp", kickAmp_[0], kickAmp_[1], kickAmp_[2], kickAmp_[3]);
    set1f(progWorld_, "uCars", cars_ * (1.f - over));
    set1f(progWorld_, "uRain", pal_.rain * (0.35f + 0.65f * wm));
    set1f(progWorld_, "uSkyline", pal_.skyline);
    set1f(progWorld_, "uStars", pal_.stars * (1.f - 0.5f * wp));
    set1f(progWorld_, "uFog", pal_.fog * (1.15f - 0.2f * wm) * (1.f + 1.5f * over));
    set1f(progWorld_, "uHazeLit", hazeLit_ + 0.4f * flash_ + 0.6f * sunFlare_);
    set1f(progWorld_, "uGridGain", (0.1f + 0.26f * wm + 0.06f * wp) * (1.f + 0.6f * horizonFlash_) * (0.9f + 0.2f * saturate(music.energy)) * (1.f - over));
    set1f(progWorld_, "uBars", slow_ * (1.f - saturate(bridge_.amount)));
    set1f(progWorld_, "uTrail", slow_);
    set3f(progWorld_, "uSkyTop", pal_.skyTop);
    set3f(progWorld_, "uHaze", pal_.haze * (1.f + 0.4f * horizonFlash_));
    set3f(progWorld_, "uSunCore", pal_.sunCore);
    set3f(progWorld_, "uSunEdge", pal_.sunEdge);
    set3f(progWorld_, "uFloor", pal_.floor);
    set3f(progWorld_, "uGrid", pal_.grid);
    set3f(progWorld_, "uAcc", pal_.accent);
    set4f(progWorld_, "uWell", -WELL_X, WELL_BOT, WELL_X, WELL_TOP + 2.5f);
    set4f(progWorld_, "uColL", layout::LEFT_COLUMN[0], layout::LEFT_COLUMN[1], layout::LEFT_COLUMN[2], layout::LEFT_COLUMN[3]);
    set4f(progWorld_, "uColR", layout::RIGHT_COLUMN[0], layout::RIGHT_COLUMN[1], layout::RIGHT_COLUMN[2], layout::RIGHT_COLUMN[3]);
    set1f(progWorld_, "uRail", (0.55f + 0.25f * wm + 0.6f * horizonFlash_) * (1.f - 0.6f * saturate(bridge_.amount)) * (1.f - 0.7f * over));
    set1f(progWorld_, "uBridge", saturate(bridge_.amount));
    set3f(progWorld_, "uBridgeCol", bridge_.color);
    set1f(progWorld_, "uSeed", seedF_);
    ugl::drawFullscreen();
}

void SynthwaveUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const Palette target = paletteOf(variant_);
        if (!palSet_) { pal_ = target; palSet_ = true; }
        else lerpPalette(pal_, target, 1.f - std::exp(-dt * 1.6f));
    }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 3.f, dt);
    slowT_ += dt;
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.55f : 0.f), 3.f, dt);
    if (overT_ >= 0.f) {
        if (!game.over()) overT_ = -1.f;
        else overT_ += dt;
    }
    // slow motion stretches time
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.25f, slow_);
    if (!f.paused) simulate(game, music, dt, wdt);
    const float world = 1.f - saturate(bridge_.amount);
    const float over = overT_ >= 0.f ? smoothstepf(0.3f, 2.2f, overT_) : 0.f;

    // ---- Geometry.
    tiles_.clear();
    lines_.clear();
    fxLines_.clear();
    shapes_.clear();
    buildLamps(smoothstepf(0.3f, 1.f, world) * (1.f - over));
    buildBoard(game);
    buildFx();

    // ---- The scene (multisampled when possible).
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_ ? msFbo_ : sceneFbo_);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    drawWorld(music);

    glEnable(GL_BLEND);
    auto drawLines = [&](const std::vector<Line>& v) {
        if (v.empty()) return;
        glBlendFunc(GL_ONE, GL_ONE);
        glUseProgram(progLine_);
        setMat(progLine_, "uVP", vp_);
        set1f(progLine_, "uPx", px_);
        glBindVertexArray(lineVao_);
        glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Line), v.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)v.size());
    };
    drawLines(lines_);
    if (!tiles_.empty()) {
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(progTile_);
        setMat(progTile_, "uVP", vp_);
        set1f(progTile_, "uPx", px_);
        set3f(progTile_, "uSkyTop", pal_.skyTop);
        set3f(progTile_, "uSunCore", pal_.sunCore);
        set3f(progTile_, "uSunEdge", pal_.sunEdge);
        set3f(progTile_, "uGrid", pal_.grid);
        set3f(progTile_, "uAcc", pal_.accent);
        set1f(progTile_, "uBridge", saturate(bridge_.amount));
        set3f(progTile_, "uBridgeInk", bridge_.ink);
        set1f(progTile_, "uTime", worldTime_);
        glBindVertexArray(tileVao_);
        glBindBuffer(GL_ARRAY_BUFFER, tileVbo_);
        glBufferData(GL_ARRAY_BUFFER, tiles_.size() * sizeof(Tile), tiles_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)tiles_.size());
    }
    drawLines(fxLines_);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawHud(f.hud, f.panels);
    drawOsd(game);
    glDisable(GL_BLEND);

    // ---- Resolve.
    if (msFbo_) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo_);
        glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    // ---- Bloom: the lights above the threshold, blurred down and back up.
    glUseProgram(progDown_);
    set1i(progDown_, "uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    GLuint src = sceneTex_;
    int sw = width_, sh = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        glViewport(0, 0, bloomW_[i], bloomH_[i]);
        glBindTexture(GL_TEXTURE_2D, src);
        set2f(progDown_, "uTexel", 0.5f / sw, 0.5f / sh);
        set1f(progDown_, "uThreshold", i == 0 ? 0.85f : 0.f);
        ugl::drawFullscreen();
        src = bloomTex_[i];
        sw = bloomW_[i];
        sh = bloomH_[i];
    }
    // ---- Anamorphic streaks from the quarter-resolution highlights (before the upsampling adds into them).
    const float streakAmt = (2.5f + 2.f * smoothstepf(1.1f, 2.f, lv_) + 5.f * flareKick_ + 3.5f * sunFlare_ + 3.f * slow_)
                            * world * (1.f - 0.8f * over);
    glUseProgram(progStreak_);
    set1i(progStreak_, "uTex", 0);
    {
        GLuint from = bloomTex_[0];
        const float steps[3] = {1.f, 7.f, 40.f};
        for (int i = 0; i < 3; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, streakFbo_[i & 1]);
            glViewport(0, 0, streakW_, streakH_);
            glBindTexture(GL_TEXTURE_2D, from);
            set1f(progStreak_, "uStep", steps[i] / streakW_);
            set1f(progStreak_, "uThreshold", i == 0 ? 1.1f : 0.f);
            ugl::drawFullscreen();
            from = streakTex_[i & 1];
        }
    }
    glUseProgram(progUp_);
    set1i(progUp_, "uTex", 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = BLOOM_MIPS - 1; i > 0; i--) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i - 1]);
        glViewport(0, 0, bloomW_[i - 1], bloomH_[i - 1]);
        glBindTexture(GL_TEXTURE_2D, bloomTex_[i]);
        set2f(progUp_, "uTexel", 1.f / bloomW_[i], 1.f / bloomH_[i]);
        ugl::drawFullscreen();
    }
    glDisable(GL_BLEND);

    // ---- The film.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progPost_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, streakTex_[0]);
    glActiveTexture(GL_TEXTURE0);
    set1i(progPost_, "uScene", 0);
    set1i(progPost_, "uBloom", 1);
    set1i(progPost_, "uStreak", 2);
    set2f(progPost_, "uRes", (float)width_, (float)height_);
    set1f(progPost_, "uTime", (float)f.time);
    set1f(progPost_, "uFade", saturate(f.fade));
    set1f(progPost_, "uDim", dimT_);
    set1f(progPost_, "uSlow", slow_ * world);
    set1f(progPost_, "uBridge", saturate(bridge_.amount));
    set1f(progPost_, "uBloomAmt", 0.5f * world);
    set1f(progPost_, "uStreakAmt", streakAmt);
    set1f(progPost_, "uExposure", 1.f);
    set1f(progPost_, "uOver", over * 0.6f);
    set3f(progPost_, "uStreakTint", pal_.flare * 1.4f);
    {
        const layout::Rect r = layout::boardRect(width_, height_);
        set4f(progPost_, "uWellUv", r.x0 / width_, 1.f - r.y1 / height_, r.x1 / width_, 1.f - r.y0 / height_);
    }
    set3f(progPost_, "uShadowTint", normalize(pal_.haze + vec3(0.01f)));
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_synthwave() {
    static const UniverseEntry e{
        "SYNTHWAVE", "a night drive, shot like a film", [] { return std::unique_ptr<Universe>(new SynthwaveUniverse()); },
        // Driving songs with a pulse suit the drive best; slower ones still cruise through the fog.
        [](const SongLook& l) { return 0.6f + 0.5f * smoothstepf(90.f, 125.f, l.bpm) + 0.3f * l.density + 0.2f * l.bassWeight; },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
