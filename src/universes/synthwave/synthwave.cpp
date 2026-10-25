// SYNTHWAVE: a sunset drive on a neon grid.
//
// One world: a big gradient sun with slit bands over a mirror floor crossed by a perspective neon grid, wire
// mountains and palm silhouettes on the sides, a dark glass well standing in front of the sun like a monolith.
// Surfaces are filled and glossy (airbrushed skies, chrome, glossy neon tiles), finished by a tape: soft bloom,
// film grain and a slight chroma bleed.
//
// The song drives the drive: calm is dusk (the sun half set, stars, a slow dim grid), mid scrolls the grid on the
// beat while the sun's bands breathe and the palms sway, peak is the night drive (car light streaks racing to the
// horizon, lasers, the grid pulsing with the kicks, chrome glints). A lock flashes the piece's neon outline and
// sweeps a chrome glint across it; a cleared row slides away as a streak of light; a Tetris flares the sun under a
// chrome title with a lens flare; slow motion is a paused tape (cyan night, a tracking band); game over rewinds the
// tape, stops it, then static.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane (like SUMI), so the
// well, the previews and the HUD anchors sit exactly where the layout puts them.
#include "universes/synthwave/synthwave.hpp"

#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "stb_easy_font.h"
#include "ugl.hpp"
#include "universes/registry.hpp"
#include "universes/synthwave/shaders.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Audiowide (block display letters) and Mr Dafoe (brush script), SIL Open Font License, see OFL-*.txt beside
// this file.
const unsigned char FONT_BLOCK[] = {
#include "universes/synthwave/Audiowide-Regular.ttf.inc"
};
const unsigned char FONT_SCRIPT[] = {
#include "universes/synthwave/MrDafoe-Regular.ttf.inc"
};

namespace shaders = synthwave_shaders;
using ugl::U;
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

constexpr float HORIZON = -3.6f;        // board y of the horizon
constexpr float SUN_R = 9.2f;
constexpr float WELL_X = 5.1f, WELL_BOT = -10.1f, WELL_TOP = 10.08f;

vec2 v2(float x, float y) { return vec2{x, y}; }
vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
vec2 operator*(vec2 a, float k) { return {a.x * k, a.y * k}; }
float len(vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
vec2 norm(vec2 a) { float l = len(a); return l > 1e-6f ? a * (1.f / l) : v2(1, 0); }
float hashf(float x) { float s = std::sin(x * 12.9898f) * 43758.5453f; return s - std::floor(s); }

vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }

// The variants: each a designed palette (sRGB here, linear once loaded). Fields: sky top / mid / horizon, the
// night sky of the peaks, the sun's crown and base, the grid's neon, the second neon (lasers, labels), mountains,
// floor, horizon haze, palm silhouettes, the seven piece neons (I O T S Z J L), and how cloudy the sky is.
struct VariantDef {
    const char* name;
    unsigned skyTop, skyMid, skyLow, night, sunTop, sunBot, grid, acc2, mount, floor, haze, palm;
    unsigned piece[7];
    float clouds;
};
const VariantDef VARIANTS[] = {
    {"OUTRUN", 0x170a3a, 0x5c1b7c, 0xff6a6a, 0x07051a, 0xffe46b, 0xff2d8d, 0xff3fd2, 0x35e8ff, 0x2a0c40, 0x12062a,
     0xff4fa3, 0x0b0414, {0x36e6ff, 0xffd84a, 0xc14bff, 0x48f59a, 0xff3d6e, 0x4f6bff, 0xff8f2e}, 0.25f},
    {"MIAMI", 0x0a2342, 0x1f7a8c, 0xff9bb8, 0x041226, 0xfff1a8, 0xff5fa2, 0x2ff3e0, 0xff6fb5, 0x0e2f45, 0x061a2a,
     0xff8fc0, 0x03121c, {0x2ff3e0, 0xffe27a, 0xff6fb5, 0x7cff9b, 0xff5470, 0x5aa8ff, 0xffa65a}, 0.35f},
    {"CYBER NIGHT", 0x03040f, 0x141a5c, 0x6b3cff, 0x010208, 0x7af7ff, 0x9b4dff, 0x3d7dff, 0xff3fd8, 0x0b0d2e, 0x04061c,
     0x7a5cff, 0x020310, {0x45f0ff, 0xf5f06a, 0xb65cff, 0x45ffb0, 0xff4f8b, 0x4a72ff, 0xff9a4a}, 0.0f},
    {"BLOOD MOON", 0x070103, 0x3a0612, 0xd1173a, 0x020001, 0xff9a6b, 0xc4001f, 0xff2442, 0xff9a3d, 0x1a0308, 0x0c0105,
     0xff2a4f, 0x050102, {0x4be1ff, 0xffcf4a, 0xff4fd8, 0x6dff7a, 0xff2e3e, 0x7a6bff, 0xff8a2e}, 0.3f},
    {"GOLDEN HOUR", 0x2b1240, 0xa33a4a, 0xffb347, 0x120818, 0xfff7c2, 0xff7a2f, 0xffb347, 0x3fd9ff, 0x3a1530, 0x1a0a16,
     0xffc46b, 0x140611, {0x4fe3ff, 0xffe066, 0xff5fb0, 0x7ff07a, 0xff4a4a, 0x6f7bff, 0xffa13a}, 0.45f},
    {"ICE DAWN", 0x0b1a33, 0x2f6d9a, 0xa6f5e1, 0x050c1c, 0xffffff, 0x8ff7d8, 0x7dffd6, 0xc9a6ff, 0x13324a, 0x081628,
     0xb6fff0, 0x061220, {0x5cf2ff, 0xfff28a, 0xc9a6ff, 0x7dffb0, 0xff6f91, 0x6c8cff, 0xffb36b}, 0.2f},
    {"PURPLE STORM", 0x0d0418, 0x3b1460, 0xb04bd8, 0x05010c, 0xffb3f0, 0x8a2be2, 0xb04bff, 0x5ef0ff, 0x1a0a2c, 0x0a0414,
     0xc66bff, 0x06020c, {0x5ef0ff, 0xffe45c, 0xd65cff, 0x5cff9d, 0xff4f7b, 0x6a6dff, 0xff9a47}, 1.0f},
    {"LILAC HAZE", 0x3b2a6b, 0x9a7fd1, 0xffc6b0, 0x1b1238, 0xfffbe0, 0xff9ec4, 0xff8fd8, 0x7fe9ff, 0x4a3577, 0x22183f,
     0xffd0e0, 0x1c1233, {0x3fdcff, 0xffd447, 0xb44dff, 0x3ee88a, 0xff3f6f, 0x4a5cff, 0xff8a2a}, 0.5f},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));
constexpr int STORM = 6;

SynthwaveUniverse::Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    auto L = [](unsigned c) { return ugl::toLinear(hex(c)); };
    SynthwaveUniverse::Palette p;
    p.skyTop = L(d.skyTop); p.skyMid = L(d.skyMid); p.skyLow = L(d.skyLow); p.night = L(d.night);
    p.sunTop = L(d.sunTop); p.sunBot = L(d.sunBot); p.grid = L(d.grid); p.acc2 = L(d.acc2);
    p.mount = L(d.mount); p.floor = L(d.floor); p.haze = L(d.haze); p.palm = L(d.palm);
    for (int i = 0; i < 7; i++) p.piece[i] = L(d.piece[i]);
    p.clouds = d.clouds;
    return p;
}

void lerpPalette(SynthwaveUniverse::Palette& a, const SynthwaveUniverse::Palette& b, float k) {
    vec3* pa[] = {&a.skyTop, &a.skyMid, &a.skyLow, &a.night, &a.sunTop, &a.sunBot, &a.grid, &a.acc2, &a.mount, &a.floor, &a.haze, &a.palm};
    const vec3* pb[] = {&b.skyTop, &b.skyMid, &b.skyLow, &b.night, &b.sunTop, &b.sunBot, &b.grid, &b.acc2, &b.mount, &b.floor, &b.haze, &b.palm};
    for (int i = 0; i < 12; i++) *pa[i] = lerp(*pa[i], *pb[i], k);
    for (int i = 0; i < 7; i++) a.piece[i] = lerp(a.piece[i], b.piece[i], k);
    a.clouds = lerpf(a.clouds, b.clouds, k);
}

std::string titleCase(const std::string& s) {
    std::string o = s;
    bool start = true;
    for (char& c : o) {
        if (std::isalpha((unsigned char)c)) {
            c = start ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c);
            start = false;
        } else start = true;
    }
    return o;
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

    buildFont(fontBlock_, FONT_BLOCK);
    buildFont(fontScript_, FONT_SCRIPT);
    pal_ = paletteOf(0);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// Glyphs baked as signed distance fields: they scale cleanly, and the distance gives outlines and neon glows.
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
    msFbo_ = msColor_ = sceneFbo_ = sceneTex_ = 0;
    for (int i = 0; i < BLOOM_MIPS; i++) bloomFbo_[i] = bloomTex_[i] = 0;
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
    // Multisampled scene (palms, tiles and lines get clean edges), resolved into sceneTex_.
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
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

SynthwaveUniverse::~SynthwaveUniverse() {
    destroyTargets();
    for (GLuint p : {progWorld_, progShape_, progTile_, progLine_, progText_, progDown_, progUp_, progPost_}) glDeleteProgram(p);
    for (GLuint v : {tileVao_, lineVao_, shapeVao_, textVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, tileVbo_, lineVbo_, shapeVbo_, textVbo_}) glDeleteBuffers(1, &b);
    for (Font* f : {&fontBlock_, &fontScript_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// =====================================================================================================
// Song, level, bonus
// =====================================================================================================

// The variant from the song: the key's hue picks the family, night songs (minor and dark) go to the night
// palettes, bright major ones to the soft dawns.
int SynthwaveUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    const bool night = t.hasSong && t.minor && t.brightness < 0.45f;
    const bool soft = t.hasSong && !t.minor && t.brightness > 0.55f;
    int v;
    if (night) v = (h < 0.2f || h > 0.85f) ? 3 : (h < 0.6f ? 2 : 6);
    else if (soft) v = (h < 0.35f) ? 4 : (h < 0.65f ? 5 : 7);
    else if (h < 0.1f || h >= 0.88f) v = 0;
    else if (h < 0.22f) v = 4;
    else if (h < 0.38f) v = 5;
    else if (h < 0.55f) v = 1;
    else if (h < 0.7f) v = 2;
    else v = (t.seed & 1) ? 6 : 0;
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void SynthwaveUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    seedF_ = (float)(look.seed % 1000) * 0.137f;
    rng_ = Rng(look.seed ^ 0x5717ull);
    if (seconds <= 0.f) palSet_ = false; // snap on the first frame
    else { horizonFlash_ = 1.f; glintBurst(2, 1.f); }
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
    lvDur_ = std::clamp(seconds, rising ? 0.6f : 1.2f, 4.f);
    if (rising) {
        horizonFlash_ = std::max(horizonFlash_, 0.5f + 0.5f * impact);
        if (level >= 2) {
            flash_ = std::max(flash_, 0.25f + 0.35f * impact);
            glintBurst(2 + (int)(3 * impact), 1.f);
        }
    }
}

void SynthwaveUniverse::refresh(float strength) {
    horizonFlash_ = std::max(horizonFlash_, 0.5f * strength);
    if (strength > 0.4f) glintBurst(1, 0.6f);
}

void SynthwaveUniverse::setBonus(bool on) {
    if (on == slowOn_) return;
    slowOn_ = on;
    slowT_ = 0.f;
}

void SynthwaveUniverse::levelUp() {
    sunFlare_ = std::max(sunFlare_, 0.6f);
    horizonFlash_ = 1.f;
    glintBurst(3, 1.f);
}

vec2 SynthwaveUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints SynthwaveUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    h.background = lerp(p.skyTop, p.skyMid, 0.35f) * 0.8f;
    h.ink = p.grid * 0.85f;
    h.accent = p.sunBot;
    h.light = false;
    h.styles = TS_LUMA | TS_RIPPLE | TS_TILES; // the sun's highlights last, rings on the grid, tiles like a TV wall
    return h;
}

// Star glints on the chrome: the well's frame, the sun's rim.
void SynthwaveUniverse::glintBurst(int n, float spread) {
    for (int i = 0; i < n; i++) {
        Glint g;
        const float r = rng_.uniform();
        if (r < 0.5f) {
            const float sx = rng_.chance(0.5f) ? -WELL_X : WELL_X;
            g.p = v2(sx, rng_.chance(0.5f) ? WELL_BOT : rng_.range(-9.f, 9.f));
        } else {
            const float a = rng_.range(0.25f, PI - 0.25f);
            g.p = v2(std::cos(a) * SUN_R * 1.0f, HORIZON + 3.f + std::sin(a) * SUN_R * 0.9f);
            if (std::fabs(g.p.x) < WELL_X + 0.5f) g.p.x = (g.p.x < 0 ? -1.f : 1.f) * (WELL_X + rng_.range(1.f, 4.f));
        }
        g.t = -rng_.range(0.f, 0.25f * spread);
        g.life = rng_.range(0.45f, 0.75f);
        g.size = rng_.range(0.6f, 1.3f);
        g.col = vec3(1.f, 0.95f, 0.9f);
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
        // The piece's outer contour flares neon.
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
                vec2 c = v2(p.x, p.y) + n * 0.49f;
                f.seg.push_back(c - t * 0.49f);
                f.seg.push_back(c + t * 0.49f);
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
        float dir0 = ev.x < 4.5f ? 1.f : -1.f; // away from where the piece landed
        if (std::fabs(ev.x - 4.5f) < 0.6f) dir0 = rng_.chance(0.5f) ? 1.f : -1.f;
        for (size_t i = 0; i < rows.size(); i++) {
            const float dir = tetris ? ((i & 1) ? -dir0 : dir0) : dir0;
            rows_.push_back({layout::cellPos(0, (float)rows[i]).y, -0.03f * i, dir});
        }
        for (auto& c : ev.cells) {
            size_t ri = std::find(rows.begin(), rows.end(), c.y) - rows.begin();
            const float dir = tetris ? ((ri & 1) ? -dir0 : dir0) : dir0;
            vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0; d.dir = dir; d.type = std::clamp(c.type, 0, 6);
            d.delay = (dir > 0 ? (9 - c.x) : c.x) * 0.014f + ri * 0.03f;
            d.rewind = false; d.vx = d.vy = 0;
            dying_.push_back(d);
        }
        if (tetris) {
            sunFlare_ = 1.f;
            tetrisT_ = 0.f;
            flash_ = std::max(flash_, 0.35f);
            glintBurst(4, 1.5f);
        } else {
            horizonFlash_ = std::max(horizonFlash_, 0.25f * ev.count);
        }
        break;
    }
    case GameEvent::BonusReady:
        readyT_ = 0.f;
        glints_.push_back({v2(layout::GAUGE_CENTER.x + 0.7f, layout::GAUGE_CENTER.y + 0.7f), 0.f, 0.8f, 1.1f, vec3(1.f)});
        break;
    case GameEvent::TopOut: {
        // The tape rewinds: the stack flies back up, then stops, then static.
        int topRow = Game::H;
        for (auto& c : ev.cells) topRow = std::min(topRow, c.y);
        for (auto& c : ev.cells) {
            vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0; d.dir = 0; d.type = std::clamp(c.type, 0, 6);
            d.delay = (c.y - topRow) * 0.012f;
            d.rewind = true;
            d.vx = rng_.range(-0.4f, 0.4f);
            d.vy = rng_.range(14.f, 26.f);
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
    // The scene level, eased (rising changes quick, falling ones slow).
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);

    // The grid scrolls with the tempo: a lunge on each beat (mid, peak), steady when calm.
    {
        const float ph = music.beatPhase;
        if (ph < lastBeatPhase_ - 0.5f) beats_ += 1.f;
        lastBeatPhase_ = ph;
        const float surge = wm;
        const float e = 1.f - std::pow(1.f - ph, 2.6f);
        const float eased = beats_ + lerpf(ph, e, surge);
        float d = lastEased_ < 0.f ? 0.f : eased - lastEased_;
        if (d < 0.f || d > 2.f) d = 0.f;
        lastEased_ = eased;
        const float cpb = 0.3f + 0.7f * wm + 1.0f * wp;
        if (music.hasSong) scroll_ += d * cpb * (wdt / std::max(dt, 1e-4f));
        else scroll_ += wdt * 0.6f;
    }

    // Kicks: the grid pulses (peak), and on the beats at mid.
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
        pushKick(0.6f + 0.6f * wp);
        swayKick_ = std::max(swayKick_, 0.6f);
        if (kick > 0.8f && sinceGlint_ > 1.6f) {
            glintBurst(1, 0.3f);
            sinceGlint_ = 0.f;
        }
        if (variant_ == STORM && rng_.chance(0.18f)) flash_ = std::max(flash_, 0.55f);
    } else if (music.hasSong && wm > 0.5f && wp < 0.3f && music.beatPulse > 0.9f && sinceKick_ > 0.3f) {
        pushKick(0.3f);
        swayKick_ = std::max(swayKick_, 0.35f);
    }
    prevKick_ = kick;
    for (int i = 0; i < 4; i++) kickAge_[i] += wdt;

    // The sun's bands drift down and breathe on the beat.
    bandScroll_ += wdt * (0.05f + 0.12f * wm);
    breathe_ = approach(breathe_, saturate(music.beatPulse) * (0.4f * wm + 0.5f * wp) + 0.4f * kick * wp, 10.f, dt);
    // Palms sway (more with the beat at mid).
    swayT_ += wdt * (0.5f + 0.4f * wm);
    swayKick_ = approach(swayKick_, 0.f, 2.5f, dt);

    cars_ = approach(cars_, wp, 1.2f, dt);
    lasers_ = approach(lasers_, wp * (0.5f + 0.5f * saturate(music.energy)), 1.5f, dt);
    stars_ = approach(stars_, 1.f - 0.65f * wm + 0.5f * wp, 1.f, dt);
    flash_ = approach(flash_, 0.f, 5.f, dt);
    sunFlare_ = approach(sunFlare_, 0.f, 1.3f, dt);
    horizonFlash_ = approach(horizonFlash_, 0.f, 2.2f, dt);
    tetrisT_ += dt;

    for (auto& f : flares_) f.t += dt;
    flares_.erase(std::remove_if(flares_.begin(), flares_.end(), [](const Flare& f) { return f.t > 0.6f; }), flares_.end());
    for (auto& s : streaks_) s.t += dt;
    streaks_.erase(std::remove_if(streaks_.begin(), streaks_.end(), [](const Streak& s) { return s.t > 0.35f; }), streaks_.end());
    for (auto& r : rows_) r.t += dt;
    rows_.erase(std::remove_if(rows_.begin(), rows_.end(), [](const RowGo& r) { return r.t > 0.9f; }), rows_.end());
    for (auto& d : dying_) d.t += dt;
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return d.t > d.delay + (d.rewind ? 1.4f : 0.6f); }), dying_.end());
    for (auto& g : glints_) g.t += dt;
    glints_.erase(std::remove_if(glints_.begin(), glints_.end(), [](const Glint& g) { return g.t > g.life; }), glints_.end());

    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
    readyT_ += dt;
    bonusLeft_ = game.bonusLeft();
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

void SynthwaveUniverse::star(std::vector<Line>& out, vec2 c, float radius, const vec3& col, float alpha) {
    if (alpha <= 0.003f) return;
    Line l{{c.x, c.y, c.x, c.y}, {col.x, col.y, col.z, 0.f}, {radius, 0.f, 1.f, alpha}};
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

// A palm silhouette: a ringed, curved trunk and feathered fronds drooping from the crown.
void SynthwaveUniverse::palm(vec2 base, float height, float lean, float scale, float sway, int seed, const vec3& col,
                             float alpha, vec2 off) {
    base = base + off;
    const vec2 p0 = base, p1 = base + v2(lean * 0.12f * height, 0.55f * height);
    const vec2 p2 = base + v2(lean * 0.40f * height + sway * height * 0.10f, height);
    auto bez = [&](float t) { float u = 1 - t; return p0 * (u * u) + p1 * (2 * u * t) + p2 * (t * t); };
    const int N = 22;
    vec2 prevL, prevR;
    for (int i = 0; i <= N; i++) {
        const float t = i / (float)N;
        vec2 p = bez(t), tg = norm(bez(std::min(1.f, t + 0.01f)) - bez(std::max(0.f, t - 0.01f)));
        vec2 n = v2(-tg.y, tg.x);
        float w = scale * (0.36f - 0.17f * t) * ((i & 1) ? 1.f : 1.13f);
        if (i == 0) w *= 1.35f;
        vec2 L = p + n * w, R = p - n * w;
        if (i > 0) quad(shapes_, prevL, prevR, R, L, col, alpha);
        prevL = L;
        prevR = R;
    }
    const vec2 crown = p2;
    // the fronds: long arcs drooping from the crown, thin leaflets hanging from them
    const float angles[11] = {-0.75f, -0.32f, 0.08f, 0.5f, 0.95f, 1.45f, 1.95f, 2.4f, 2.85f, 3.25f, 3.7f};
    for (int k = 0; k < 11; k++) {
        const float h1 = hashf(seed * 13.1f + k * 3.7f), h2 = hashf(seed * 5.3f + k * 9.1f);
        const float a = angles[k] + (h1 - 0.5f) * 0.25f + sway * (0.7f + 0.6f * h2)
                        + 0.03f * std::sin(swayT_ * 2.3f + k * 1.9f + seed);
        const float L = height * 0.34f * (0.8f + 0.4f * h2) * (k == 5 ? 0.6f : 1.f);
        const float up = std::sin(a);
        const float droop = 0.38f + 0.3f * h1 + 0.45f * std::max(0.f, up);
        const vec2 dir = v2(std::cos(a), up);
        auto spine = [&](float t) { return crown + dir * (L * t) + v2(0.f, -droop * L * t * t); };
        const int M = 24;
        for (int i = 0; i < M; i++) {
            const float t0 = i / (float)M, t1 = (i + 1) / (float)M;
            vec2 s0 = spine(t0), s1 = spine(t1);
            vec2 tg = norm(s1 - s0), n = v2(-tg.y, tg.x);
            const float w = 0.045f * scale * (1.f - t0 * 0.85f);
            quad(shapes_, s0 + n * w, s0 - n * w, s1 - n * w * 0.85f, s1 + n * w * 0.85f, col, alpha);
            if (t0 < 0.06f) continue;
            const float env = std::pow(std::sin(PI * (0.1f + 0.9f * t0)), 0.6f) * (1.f - 0.4f * t0);
            const float ll = L * 0.34f * env;
            const vec2 mid = s0 + (s1 - s0) * 0.6f;
            for (int side = -1; side <= 1; side += 2) {
                vec2 ld = norm(tg * 0.55f + n * (0.85f * (float)side) + v2(0.f, -0.45f));
                tri(shapes_, s0, mid, s0 + ld * ll, col, alpha);
            }
        }
    }
}

void SynthwaveUniverse::buildPalms(float alpha) {
    if (alpha <= 0.01f) return;
    const float wm = smoothstepf(0.2f, 1.f, lv_);
    const float amp = 0.025f + 0.05f * wm;
    const float vx = viewH_.x;
    const vec3 rim = lerp(pal_.sunBot, pal_.haze, 0.4f) * 0.9f;
    struct P { float x, baseY, height, lean, scale; int seed; };
    const P palms[4] = {
        {-std::max(14.f, vx - 3.4f), HORIZON - 6.6f, 14.f, 0.5f, 1.1f, 1},
        {-std::max(12.8f, vx - 8.2f), HORIZON - 1.2f, 6.6f, -0.3f, 0.6f, 2},
        {std::max(14.f, vx - 2.8f), HORIZON - 7.4f, 15.f, -0.55f, 1.15f, 3},
        {std::max(13.f, vx - 8.8f), HORIZON - 0.8f, 5.6f, 0.3f, 0.55f, 4},
    };
    for (const P& p : palms) {
        const float sway = amp * std::sin(swayT_ * 1.3f + p.seed * 1.7f) + swayKick_ * 0.06f * (p.seed & 1 ? 1.f : -1.f);
        // the rim the sun lights (toward the center), then the silhouette over it
        const float toC = p.x < 0 ? 1.f : -1.f;
        palm(v2(p.x, p.baseY), p.height, p.lean, p.scale, sway, p.seed, rim, alpha * 0.85f, v2(0.06f * toC, 0.05f) * p.scale);
        palm(v2(p.x, p.baseY), p.height, p.lean, p.scale, sway, p.seed, pal_.palm, alpha, v2(0, 0));
    }
}

void SynthwaveUniverse::buildBoard(const Game& g) {
    const float bridgeK = saturate(bridge_.amount);
    // Locked cells.
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            const float age = c.flash > 0.f ? (1.f - c.flash) * Game::LOCK_FX_SECONDS : -1.f;
            tile(p.x, p.y, 0.5f, 0.5f, pal_.piece[c.type], 1.f, 0, age, 0.f, 1.f);
        }
    // Cleared cells sliding away; a topped-out stack rewinding upward.
    for (const Dying& d : dying_) {
        const float t = d.t - d.delay;
        if (d.rewind) {
            const float k = saturate(t / 1.3f);
            const float y = d.y + (t > 0 ? 0.5f * d.vy * t * t : 0.f);
            const float x = d.x + d.vx * std::max(t, 0.f) + 0.08f * std::sin(t * 37.f + d.y * 3.f);
            if (y > viewC_.y + viewH_.y + 1.f) continue;
            tile(x, y, 0.5f, 0.5f, pal_.piece[d.type], 1.f - k * k, 0, -1.f, 0.f, 1.f);
            continue;
        }
        if (t < 0.f) {
            tile(d.x, d.y, 0.5f, 0.5f, pal_.piece[d.type], 1.f, 0, 0.f, 0.f, 1.f);
            continue;
        }
        const float k = saturate(t / 0.5f);
        const float head = d.x + d.dir * 16.f * k * k, tail = d.x + d.dir * 16.f * k * k * 0.15f;
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
                tile(w.x, w.y, 0.5f, 0.5f, col, 0.55f, 1);
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (auto& c : cells) {
            if (c[1] < Game::HIDDEN - 1) continue;
            vec3 w = layout::cellPos(c[0] + dx, c[1] + dy);
            tile(w.x, w.y, 0.5f, 0.5f, col, 1.f, 3, -1.f, 0.f, 1.f);
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
            tile(pos.x, pos.y, 0.5f * sc, 0.5f * sc, pal_.piece[type], alpha, 3, -1.f, 0.f, sc);
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.35f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), i == 0 ? 1.f : 0.75f);
    }
    // The bonus gauge: a little sun rising as lines are cleared.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        const float ready = g.bonusReady() ? 1.f : 0.f, active = g.bonusActive() ? 1.f : 0.f;
        tile(gc.x, gc.y, R * 1.9f, R * 1.9f, vec3(1.f), 1.f, 4, -1.f, 0.f, saturate(gaugeShown_), ready, active, R);
    }
    // The well's frame: neon rails (under the stack).
    {
        const vec3 c = pal_.grid * (1.1f + 0.6f * horizonFlash_);
        const float a = 1.f - 0.6f * bridgeK;
        line(lines_, v2(-WELL_X, WELL_BOT), v2(-WELL_X, WELL_TOP), c, 0.05f, 0.1f, a);
        line(lines_, v2(WELL_X, WELL_BOT), v2(WELL_X, WELL_TOP), c, 0.05f, 0.1f, a);
        line(lines_, v2(-WELL_X, WELL_BOT), v2(WELL_X, WELL_BOT), c, 0.06f, 0.12f, a);
        // a chrome cap on the rails' tops
        line(lines_, v2(-WELL_X - 0.25f, WELL_TOP), v2(-WELL_X + 0.25f, WELL_TOP), pal_.sunTop * 1.2f, 0.05f, 0.08f, a * 0.8f);
        line(lines_, v2(WELL_X - 0.25f, WELL_TOP), v2(WELL_X + 0.25f, WELL_TOP), pal_.sunTop * 1.2f, 0.05f, 0.08f, a * 0.8f);
    }
}

void SynthwaveUniverse::buildFx() {
    const float world = 1.f - saturate(bridge_.amount);
    for (const Flare& f : flares_) {
        const float k = f.t / 0.6f;
        const float a = (1.f - k) * (1.f - k) * world;
        const vec3 c = lerp(f.col, vec3(1.f), 0.35f) * 2.2f;
        for (size_t i = 0; i + 1 < f.seg.size(); i += 2) line(fxLines_, f.seg[i], f.seg[i + 1], c, 0.06f, 0.16f + 0.2f * k, a);
    }
    for (const Streak& s : streaks_) {
        const float k = s.t / 0.35f;
        line(fxLines_, v2(s.x, s.y0 + (s.y1 - s.y0) * k * 0.6f), v2(s.x, s.y1), s.col * 1.6f, 0.05f, 0.12f, (1.f - k) * 0.8f * world);
    }
    for (const RowGo& r : rows_) {
        if (r.t < 0.f) continue;
        const float k = saturate(r.t / 0.6f);
        const float head = -r.dir * 5.f + r.dir * 34.f * k * k + r.dir * 4.f * k;
        const float tail = head - r.dir * (3.f + 9.f * k);
        const float a = (1.f - smoothstepf(0.5f, 1.f, k)) * world;
        line(fxLines_, v2(tail, r.y), v2(head, r.y), lerp(pal_.grid, vec3(1.f), 0.3f) * 2.4f, 0.09f * (1.f - 0.5f * k), 0.22f, a);
        line(fxLines_, v2(tail - r.dir * 4.f, r.y), v2(tail, r.y), pal_.acc2 * 1.4f, 0.03f, 0.12f, a * 0.6f);
    }
    for (const Glint& g : glints_) {
        if (g.t < 0.f) continue;
        const float k = g.t / g.life;
        const float a = std::sin(PI * saturate(k)) * world;
        star(fxLines_, g.p, g.size * (0.6f + 0.6f * std::sin(PI * k)), g.col * 2.5f, a);
    }
    // The Tetris lens flare: an anamorphic streak through the title, a star on its first letter.
    if (tetrisT_ < 2.4f) {
        const float k = tetrisT_ / 2.4f;
        const float a = (tetrisT_ < 0.15f ? tetrisT_ / 0.15f : 1.f) * (1.f - smoothstepf(0.6f, 1.f, k)) * world;
        const vec2 c = v2(layout::TETRIS_TOAST.x - 3.6f + 7.2f * smoothstepf(0.f, 1.f, k), layout::TETRIS_TOAST.y - 0.2f);
        line(fxLines_, v2(c.x - 18.f, c.y), v2(c.x + 18.f, c.y), pal_.acc2 * 1.2f, 0.035f, 0.1f, a * 0.8f);
        star(fxLines_, c, 2.2f, vec3(1.f, 0.95f, 0.9f) * 2.5f, a);
        // ghost discs along the flare axis (toward the sun)
        for (int i = 1; i <= 3; i++) {
            const vec2 q = c + (v2(0.f, HORIZON + 2.f) - c) * (0.35f * i);
            star(fxLines_, q, 0.35f + 0.15f * i, lerp(pal_.sunBot, pal_.acc2, i / 3.f) * 0.8f, a * 0.5f);
        }
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float SynthwaveUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
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

float SynthwaveUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
                                   float shear, std::vector<float>& out) const {
    if (!f.ok) return 0.f;
    const stbtt_fontinfo* info = (const stbtt_fontinfo*)f.info;
    const float k = size / f.px;
    const float cap = fontBlock_.capH / fontBlock_.px * size; // the block font's cap height sets the line
    const float baseline = y + cap;
    float pen = x;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 32 || ch >= 128) ch = '?';
        const Glyph& g = f.g[ch - 32];
        if (g.w > 0) {
            const float x0 = pen + g.ox * k, y0 = baseline + g.oy * k, x1 = x0 + g.w * k, y1 = y0 + g.h * k;
            auto vert = [&](float vx, float vy, float u, float v) {
                out.push_back(vx + shear * (baseline - vy));
                out.push_back(vy);
                out.push_back(u);
                out.push_back(v);
                out.push_back((vy - (baseline - cap)) / std::max(cap, 1.f));
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

void SynthwaveUniverse::drawText(const Font& f, const std::vector<float>& q, const TextStyle& st) {
    if (q.empty() || !f.ok) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    set1i(progText_, "uMode", st.mode);
    set3f(progText_, "uColor", st.color);
    set3f(progText_, "uGlow", st.glow);
    set3f(progText_, "uOutline", st.outline);
    set3f(progText_, "uSkyTop", pal_.skyTop);
    set3f(progText_, "uSunTop", pal_.sunTop);
    set3f(progText_, "uSunBot", pal_.sunBot);
    set1f(progText_, "uAlpha", st.alpha);
    set1f(progText_, "uGlowAmt", st.glowAmt);
    set1f(progText_, "uOutlineW", st.outlineW);
    set1f(progText_, "uBias", st.bias);
    set1f(progText_, "uBridge", smoothstepf(0.3f, 0.95f, bridge_.amount));
    set3f(progText_, "uBridgeInk", bridge_.ink);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(float), q.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 5));
}

// The HUD in SYNTHWAVE's typography: chrome block italics for titles and numbers, small spaced neon for labels,
// a brush script in neon for the universe's name and the song's section.
void SynthwaveUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    // Panels (zenscene's menu): dark glass, or a neon bar for the accent ones.
    if (!panels.empty()) {
        std::vector<Vert> v;
        for (const HudRect& r : panels) {
            vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? pal_.grid * 0.8f : pal_.night * 0.6f;
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
    const vec3 deep = lerp(pal_.night, vec3(0.02f, 0.f, 0.04f), 0.5f);
    std::vector<float> q;
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        TextStyle st;
        st.alpha = saturate(h.alpha);
        st.outline = deep;
        q.clear();
        auto place = [&](const Font& f, const std::string& text, float size, float track, float shear) {
            const float w = textWidth(f, text, size, track);
            const float x = ugl::hudLeft(h, w, width_);
            textQuads(f, text, x, h.y, size, track, shear, q);
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            // "Synthwave" in neon script, the variant in small chrome after it.
            std::string name = h.text, var;
            size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            name = titleCase(name);
            const float sz = base * 1.75f, vsz = base * 0.9f;
            const float wName = textWidth(fontScript_, name, sz, 0.f);
            const float wVar = var.empty() ? 0.f : textWidth(fontBlock_, var, vsz, vsz * 0.12f);
            const float gap = base * 0.9f;
            HudText hh = h;
            const float x = ugl::hudLeft(hh, wName + (var.empty() ? 0.f : gap + wVar), width_);
            textQuads(fontScript_, name, x, h.y + base * 0.15f, sz, 0.f, 0.f, q);
            st.mode = 1; st.color = lerp(pal_.grid, pal_.sunTop, 0.5f); st.glow = pal_.grid * 0.9f; st.glowAmt = 0.6f; st.outlineW = 0.07f;
            st.alpha = saturate(0.35f + h.alpha * 1.6f);
            drawText(fontScript_, q, st);
            if (!var.empty()) {
                q.clear();
                textQuads(fontBlock_, var, x + wName + gap, h.y + base * 0.15f, vsz, vsz * 0.12f, 0.2f, q);
                TextStyle vs;
                vs.mode = 0; vs.outline = deep; vs.outlineW = 0.1f; vs.glow = pal_.grid * 0.5f; vs.glowAmt = 0.25f;
                vs.alpha = saturate(0.3f + h.alpha * 1.5f);
                drawText(fontBlock_, q, vs);
            }
            continue;
        }
        case HUD_SECTION: {
            place(fontScript_, titleCase(h.text), base * 1.55f, 0.f, 0.f);
            st.mode = 1; st.color = pal_.acc2; st.glow = pal_.acc2 * 0.8f; st.glowAmt = 0.5f; st.outlineW = 0.07f;
            st.alpha = saturate(0.2f + h.alpha * 1.9f);
            drawText(fontScript_, q, st);
            continue;
        }
        case HUD_TITLE:
            place(fontBlock_, h.text, base * 0.88f, base * 0.03f, 0.2f);
            st.mode = 0; st.outlineW = 0.1f; st.glow = pal_.grid * 0.6f; st.glowAmt = 0.3f;
            st.alpha = saturate(h.alpha * 1.2f);
            break;
        case HUD_INFO:
            place(fontBlock_, h.text, base * 0.9f, base * 0.14f, 0.f);
            st.mode = 1; st.color = pal_.acc2 * 0.9f; st.glow = pal_.acc2 * 0.5f; st.glowAmt = 0.3f;
            st.alpha = saturate(h.alpha * 1.3f);
            break;
        case HUD_LABEL:
            place(fontBlock_, h.text, base * 0.82f, base * 0.28f, 0.18f);
            st.mode = 1; st.color = pal_.acc2; st.glow = pal_.acc2 * 0.7f; st.glowAmt = 0.45f;
            st.alpha = saturate(h.alpha * 1.4f);
            break;
        case HUD_VALUE:
            place(fontBlock_, h.text, base * 0.98f, base * 0.04f, 0.2f);
            st.mode = 0; st.outlineW = 0.1f; st.glow = pal_.grid * 0.5f; st.glowAmt = 0.25f;
            break;
        case HUD_NOTICE:
            place(fontBlock_, h.text, base * 0.95f, base * 0.06f, 0.2f);
            st.mode = 1; st.color = pal_.grid; st.glow = pal_.grid * 0.8f; st.glowAmt = 0.5f;
            break;
        case HUD_KEY:
            place(fontBlock_, h.text, base * 0.95f, base * 0.05f, 0.f);
            st.mode = 2; st.color = vec3(1.f); st.outlineW = 0.16f; st.glowAmt = 0.f;
            break;
        case HUD_TOAST:
            place(fontBlock_, h.text, base * 1.05f, base * 0.05f, 0.22f);
            st.mode = 0; st.outlineW = 0.12f; st.glow = pal_.grid; st.glowAmt = 0.8f;
            break;
        case HUD_TOAST_SUB:
            place(fontBlock_, h.text, base * 0.95f, base * 0.08f, 0.2f);
            st.mode = 1; st.color = pal_.sunTop; st.glow = pal_.sunBot * 0.8f; st.glowAmt = 0.5f;
            break;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontBlock_, h.text, base * 1.0f, base * 0.05f, 0.22f);
                st.mode = 0; st.outlineW = 0.12f; st.glow = pal_.grid; st.glowAmt = 0.7f;
            } else {
                place(fontBlock_, h.text, base * 0.9f, base * 0.1f, 0.12f);
                st.mode = 1; st.color = h.accent ? pal_.acc2 : vec3(0.9f, 0.88f, 1.f); st.glow = pal_.acc2 * 0.4f;
                st.glowAmt = 0.3f; st.outlineW = 0.08f;
            }
            break;
        default: // hints, help, plain
            place(fontBlock_, h.text, base * 0.85f, base * 0.08f, 0.f);
            st.mode = 2; st.color = h.accent ? pal_.acc2 : vec3(0.82f, 0.78f, 0.95f); st.outlineW = 0.1f;
            break;
        }
        drawText(fontBlock_, q, st);
    }
}

// The tape's on-screen display (bitmap font, like a VCR's): PLAY/SLOW with a counter, REW, STOP.
void SynthwaveUniverse::pixelText(const std::string& s, float x, float y, float scale, const vec3& col, float alpha) {
    static char buf[60000];
    std::vector<char> txt(s.begin(), s.end());
    txt.push_back(0);
    stb_easy_font_spacing(0.5f);
    const int nq = stb_easy_font_print(0, 0, txt.data(), nullptr, buf, sizeof(buf));
    for (int pass = 0; pass < 2; pass++) {
        const float o = pass == 0 ? scale * 0.6f : 0.f;
        const vec3 c = pass == 0 ? vec3(0.f) : col;
        const float a = pass == 0 ? alpha * 0.6f : alpha;
        for (int i = 0; i < nq; i++) {
            float* v = (float*)(buf + i * 64);
            vec2 p[4];
            for (int k = 0; k < 4; k++) p[k] = v2(std::round(x + v[k * 4] * scale + o), std::round(y + v[k * 4 + 1] * scale + o));
            quad(shapes_, p[0], p[1], p[2], p[3], c, a);
        }
    }
}

void SynthwaveUniverse::drawOsd(const Game& game) {
    shapes_.clear();
    const float s = height_ / 1080.f;
    const float x = 30.f * s, y = 104.f * s, sc = 3.f * s;
    const vec3 white(0.95f, 0.97f, 1.f);
    auto play = [&](float px, float py, float h, float a) {
        tri(shapes_, v2(px, py), v2(px + h * 0.85f, py + h * 0.5f), v2(px, py + h), white, a);
    };
    if (overT_ >= 0.f && overT_ < 2.4f) {
        const float h = 7.f * sc;
        if (overT_ < 1.3f) {
            const float a = (std::fmod(overT_, 0.5f) < 0.33f) ? 1.f : 0.6f;
            tri(shapes_, v2(x + h * 0.85f, y), v2(x, y + h * 0.5f), v2(x + h * 0.85f, y + h), white, a);
            tri(shapes_, v2(x + h * 1.7f, y), v2(x + h * 0.85f, y + h * 0.5f), v2(x + h * 1.7f, y + h), white, a);
            pixelText("REW", x + h * 2.1f, y + 0.5f * sc, sc, white, a);
        } else {
            quad(shapes_, v2(x, y), v2(x + h, y), v2(x + h, y + h), v2(x, y + h), white, 1.f);
            pixelText("STOP", x + h * 1.4f, y + 0.5f * sc, sc, white, 1.f);
        }
    } else if (slow_ > 0.05f && slowOn_) {
        const float a = saturate(slow_ * 1.5f);
        const float h = 7.f * sc;
        play(x, y, h, a);
        play(x + h * 0.9f, y, h, a * 0.0f);
        pixelText("SLOW  x0.25", x + h * 1.2f, y + 0.5f * sc, sc, white, a);
        char buf[32];
        const int left = (int)std::ceil(std::max(0.f, game.bonusLeft()));
        std::snprintf(buf, sizeof(buf), "SP  0:00:%02d", left);
        pixelText(buf, x, y + h * 1.7f, sc * 0.8f, white, a * 0.85f);
    }
    if (shapes_.empty()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(progShape_);
    set1i(progShape_, "uPixels", 1);
    set2f(progShape_, "uRes", (float)width_, (float)height_);
    glBindVertexArray(shapeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
    glBufferData(GL_ARRAY_BUFFER, shapes_.size() * sizeof(Vert), shapes_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shapes_.size());
}

// =====================================================================================================
// Frame
// =====================================================================================================

void SynthwaveUniverse::drawWorld(const MusicState& music) {
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float sunRise = lerpf(lerpf(0.02f, 0.5f, wm), 0.34f, wp);
    const float R = SUN_R * (1.f + 0.05f * sunFlare_);
    const float sunGain = (0.85f + 0.3f * wm + 0.15f * wp) * (1.f + 0.12f * breathe_) * (1.f + 1.4f * sunFlare_);
    glUseProgram(progWorld_);
    set4f(progWorld_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set1f(progWorld_, "uHorizon", HORIZON);
    set4f(progWorld_, "uSun", 0.f, HORIZON + R * sunRise, R, sunGain);
    set2f(progWorld_, "uBands", bandScroll_, breathe_);
    set1f(progWorld_, "uLv", lv_);
    set1f(progWorld_, "uTime", worldTime_);
    set1f(progWorld_, "uScroll", scroll_);
    set1f(progWorld_, "uPx", px_);
    set4f(progWorld_, "uKickAge", kickAge_[0], kickAge_[1], kickAge_[2], kickAge_[3]);
    set4f(progWorld_, "uKickAmp", kickAmp_[0], kickAmp_[1], kickAmp_[2], kickAmp_[3]);
    set1f(progWorld_, "uCars", cars_);
    set1f(progWorld_, "uLasers", lasers_);
    set1f(progWorld_, "uStars", stars_);
    set1f(progWorld_, "uClouds", pal_.clouds * (1.f - 0.3f * wm));
    set1f(progWorld_, "uFlash", flash_);
    set1f(progWorld_, "uGridGain", (0.32f + 0.68f * wm + 0.3f * wp) * (1.f + 0.8f * horizonFlash_) * (0.9f + 0.2f * saturate(music.energy)));
    set1f(progWorld_, "uSlow", slow_);
    set3f(progWorld_, "uSkyTop", pal_.skyTop);
    set3f(progWorld_, "uSkyMid", pal_.skyMid);
    set3f(progWorld_, "uSkyLow", pal_.skyLow);
    set3f(progWorld_, "uNight", pal_.night);
    set3f(progWorld_, "uSunTop", pal_.sunTop);
    set3f(progWorld_, "uSunBot", pal_.sunBot);
    set3f(progWorld_, "uGrid", pal_.grid);
    set3f(progWorld_, "uAcc2", pal_.acc2);
    set3f(progWorld_, "uMount", pal_.mount);
    set3f(progWorld_, "uFloor", pal_.floor);
    set3f(progWorld_, "uHaze", pal_.haze * (1.f + horizonFlash_ * 0.8f));
    set4f(progWorld_, "uWell", -WELL_X, WELL_BOT, WELL_X, WELL_TOP + 2.5f);
    set4f(progWorld_, "uColL", layout::LEFT_COLUMN[0], layout::LEFT_COLUMN[1], layout::LEFT_COLUMN[2], layout::LEFT_COLUMN[3]);
    set4f(progWorld_, "uColR", layout::RIGHT_COLUMN[0], layout::RIGHT_COLUMN[1], layout::RIGHT_COLUMN[2], layout::RIGHT_COLUMN[3]);
    set1f(progWorld_, "uBridge", saturate(bridge_.amount));
    set3f(progWorld_, "uBridgeCol", bridge_.color);
    set1f(progWorld_, "uSeed", seedF_);
    ugl::drawFullscreen();
}

void SynthwaveUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    // The palette (eased toward the song's variant).
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
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.3f, slow_);
    if (!f.paused) simulate(game, music, dt, wdt);
    const float world = 1.f - saturate(bridge_.amount);

    // ---- Geometry.
    tiles_.clear();
    lines_.clear();
    fxLines_.clear();
    shapes_.clear();
    buildPalms(smoothstepf(0.4f, 1.f, world));
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
    if (!shapes_.empty()) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(progShape_);
        set1i(progShape_, "uPixels", 0);
        setMat(progShape_, "uVP", vp_);
        glBindVertexArray(shapeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
        glBufferData(GL_ARRAY_BUFFER, shapes_.size() * sizeof(Vert), shapes_.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shapes_.size());
    }
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
        set3f(progTile_, "uSkyLow", pal_.skyLow);
        set3f(progTile_, "uSunTop", pal_.sunTop);
        set3f(progTile_, "uSunBot", pal_.sunBot);
        set3f(progTile_, "uGrid", pal_.grid);
        set3f(progTile_, "uAcc2", pal_.acc2);
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

    // ---- Bloom: the neon above the threshold, blurred down and back up.
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
        set1f(progDown_, "uThreshold", i == 0 ? 1.0f : 0.f);
        ugl::drawFullscreen();
        src = bloomTex_[i];
        sw = bloomW_[i];
        sh = bloomH_[i];
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

    // ---- The tape.
    float rewind = 0.f, stat = 0.f, squeeze = 0.f;
    if (overT_ >= 0.f) {
        const float t = overT_;
        if (t < 1.3f) rewind = smoothstepf(0.f, 0.15f, t);
        else if (t < 1.65f) squeeze = smoothstepf(1.3f, 1.6f, t);
        else if (t < 1.75f) { squeeze = 1.f; stat = 0.3f; }
        else stat = lerpf(0.9f, 0.14f, smoothstepf(2.0f, 3.2f, t));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progPost_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
    glActiveTexture(GL_TEXTURE0);
    set1i(progPost_, "uScene", 0);
    set1i(progPost_, "uBloom", 1);
    set2f(progPost_, "uRes", (float)width_, (float)height_);
    set1f(progPost_, "uTime", (float)f.time);
    set1f(progPost_, "uFade", saturate(f.fade));
    set1f(progPost_, "uDim", dimT_);
    set1f(progPost_, "uSlow", slow_ * world);
    set1f(progPost_, "uBridge", saturate(bridge_.amount));
    set1f(progPost_, "uBloomAmt", 0.55f * world);
    set1f(progPost_, "uExposure", 1.f);
    set1f(progPost_, "uRewind", rewind);
    set1f(progPost_, "uStatic", stat);
    set1f(progPost_, "uSqueeze", squeeze);
    {
        const layout::Rect r = layout::boardRect(width_, height_);
        set4f(progPost_, "uWellUv", r.x0 / width_, 1.f - r.y1 / height_, r.x1 / width_, 1.f - r.y0 / height_);
    }
    set3f(progPost_, "uSlowTint", lerp(normalize(pal_.acc2 + vec3(0.05f)) * 1.2f, vec3(0.45f, 0.85f, 1.f), 0.6f));
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_synthwave() {
    static const UniverseEntry e{
        "SYNTHWAVE", "a sunset drive on a neon grid", [] { return std::unique_ptr<Universe>(new SynthwaveUniverse()); },
        // Driving, upbeat songs with a pulse suit the drive best; slower ones still cruise at dusk.
        [](const SongLook& l) { return 0.6f + 0.5f * smoothstepf(90.f, 125.f, l.bpm) + 0.3f * l.density + 0.2f * l.bassWeight; },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
