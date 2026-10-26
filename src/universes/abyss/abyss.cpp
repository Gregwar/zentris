// ABYSS: the deep sea, lit only by living things.
//
// One world: near-black blue water with real depth (the far surface a faint glow overhead, murk, a dark seabed),
// drifting marine snow, and everything that shines is alive: the pieces are translucent gelatinous blocks lit from
// within (a soft inner glow, a bright membrane, caustic veins and a pattern of their own), jellyfish, siphonophores,
// lanternfish schools, worms on the seabed, distant blinking lights.
//
// The song sets how much lives here: calm is deep silence (snow, a few distant lights, god rays far above), mid
// brings jellyfish and siphonophores drifting and pulsing their light on the beat and schools of fish as specks of
// light, peak brings a giant whale with bioluminescent spots passing slowly behind the well and the bioluminescence
// blooming through the water (the water lit in its accent, dense plankton, brightness pulsing with the kicks; a
// drop's peak floods it more than a chorus', and its first downbeat surges the light). A lock makes the jelly quiver and stirs a few motes from under
// it; a cleared row's jelly breaks into points of light (a brief burst of sparks); a Tetris makes the plankton bloom
// all through the water at once; slow motion puts every light out but the pieces' (long slow glows) and an anglerfish's lure; game over
// sinks the stack into the dark, its lights going out one by one. It leaves and enters by the water closing over it,
// the distant lights going out one by one.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane, so the well, the
// previews and the HUD anchors sit exactly where the layout puts them.
#include "universes/abyss/abyss.hpp"

#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "ugl.hpp"
#include "universes/abyss/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Cantarell Light (the HUD's humanist sans, converted from its CFF outlines to TrueType ones for stb_truetype) and EB Garamond 12 Italic (the universe's name, sections, toasts), SIL
// Open Font License, see OFL-*.txt beside this file.
const unsigned char FONT_SANS[] = {
#include "universes/abyss/Cantarell-Light.ttf.inc"
};
const unsigned char FONT_SERIF[] = {
#include "universes/abyss/EBGaramond12-Italic.ttf.inc"
};

namespace shaders = abyss_shaders;
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

constexpr float WELL_X = 5.1f, WELL_BOT = -10.1f, WELL_TOP = 10.1f;

vec2 v2(float x, float y) { return vec2{x, y}; }
vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
vec2 operator*(vec2 a, float k) { return {a.x * k, a.y * k}; }
float len(vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
vec2 norm(vec2 a) { float l = len(a); return l > 1e-6f ? a * (1.f / l) : v2(1, 0); }
float hashf(float x) { float s = std::sin(x * 12.9898f) * 43758.5453f; return s - std::floor(s); }

// The seabed's height (the same as bedY in the shaders).
float bedY(float x, float seed) {
    const float ax = std::fabs(x);
    return -10.38f - 0.55f * smoothstepf(5.4f, 8.0f, ax) + 3.4f * smoothstepf(10.f, 23.f, ax) +
           0.32f * std::sin(x * 0.8f + seed) + 0.16f * std::sin(x * 2.7f + seed * 1.7f) + 0.07f * std::sin(x * 7.3f);
}

vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }

// The variants: each a designed bioluminescent palette (sRGB here, linear once loaded). Fields: the deep (bottom),
// the water higher up, the far surface's light (god rays), the two living glows, marine snow, the whale's spots,
// and the seven jelly hues (I O T S Z J L).
struct VariantDef {
    const char* name;
    unsigned deep, water, surf, glow, glow2, snow, whale;
    unsigned piece[7];
};
const VariantDef VARIANTS[] = {
    {"CYAN LANTERN", 0x010409, 0x06283a, 0x7fd8ff, 0x3ff0ff, 0x2f7bff, 0xb8e6ff, 0x5ffcff,
     {0x45f3ff, 0xe8f57a, 0xa68bff, 0x4dffb0, 0xff6fb6, 0x3d82ff, 0xffb35c}},
    {"VIOLET VEIL", 0x03020b, 0x1a0d38, 0xb59bff, 0xc65cff, 0xff4fb8, 0xe0ccff, 0xd78cff,
     {0x6fe6ff, 0xffd36b, 0xc35bff, 0x6dffb9, 0xff4f9e, 0x6c74ff, 0xff8f5c}},
    {"EMERALD TRENCH", 0x010705, 0x042e24, 0x9cffd8, 0x3dff9e, 0xb4ff4a, 0xc8ffe6, 0x7dffc0,
     {0x3ff5e6, 0xf1ff6a, 0xb98aff, 0x45ff7f, 0xff7a9a, 0x47a6ff, 0xffc04d}},
    {"EMBER VENT", 0x070202, 0x260a08, 0xffb08a, 0xff5a2e, 0xffb43a, 0xffd8c0, 0xff7a45,
     {0x5fe8ff, 0xffd84a, 0xe05cff, 0x9dff5c, 0xff3b3b, 0x8a7bff, 0xff8a2a}},
    {"ICE LIGHT", 0x02060c, 0x15304a, 0xe6f6ff, 0xdff6ff, 0x8fd3ff, 0xffffff, 0xe8fbff,
     {0x9ff4ff, 0xfff4b8, 0xcdb8ff, 0xa8ffd4, 0xffb0cc, 0x8fb4ff, 0xffd2a0}},
    {"ULTRAMARINE", 0x01030d, 0x0a1a5a, 0x6f8cff, 0x47ff8a, 0x3c5cff, 0xb5c4ff, 0x5effa0,
     {0x48e8ff, 0xd9ff5a, 0x9a6bff, 0x40ff8a, 0xff5f8f, 0x4466ff, 0xffae45}},
    {"CORAL GHOST", 0x060309, 0x2a1224, 0xffc2c8, 0xff7a8e, 0xffc06b, 0xffe0e0, 0xff9aa8,
     {0x66ecff, 0xffe07a, 0xc690ff, 0x7cffc0, 0xff5c78, 0x7a8cff, 0xffa060}},
    {"MOON JELLY", 0x03030f, 0x202048, 0xd8d0ff, 0xc9b8ff, 0xffd9a8, 0xeeeaff, 0xd0c8ff,
     {0x8fe9ff, 0xffe6a0, 0xc7a5ff, 0x9df5c8, 0xff9cc0, 0x8ea2ff, 0xffbe8a}},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

AbyssUniverse::Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    auto L = [](unsigned c) { return ugl::toLinear(hex(c)); };
    AbyssUniverse::Palette p;
    p.deep = L(d.deep); p.water = L(d.water); p.surf = L(d.surf); p.glow = L(d.glow); p.glow2 = L(d.glow2);
    p.snow = L(d.snow); p.whale = L(d.whale);
    for (int i = 0; i < 7; i++) p.piece[i] = L(d.piece[i]);
    return p;
}

void lerpPalette(AbyssUniverse::Palette& a, const AbyssUniverse::Palette& b, float k) {
    vec3* pa[] = {&a.deep, &a.water, &a.surf, &a.glow, &a.glow2, &a.snow, &a.whale};
    const vec3* pb[] = {&b.deep, &b.water, &b.surf, &b.glow, &b.glow2, &b.snow, &b.whale};
    for (int i = 0; i < 7; i++) *pa[i] = lerp(*pa[i], *pb[i], k);
    for (int i = 0; i < 7; i++) a.piece[i] = lerp(a.piece[i], b.piece[i], k);
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

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool AbyssUniverse::init(int width, int height) {
    progWorld_ = compile(ugl::FULLSCREEN_VS, shaders::WORLD_FS, "abyss world");
    progTile_ = compile(shaders::TILE_VS, shaders::TILE_FS, "abyss tile");
    progSprite_ = compile(shaders::SPRITE_VS, shaders::SPRITE_FS, "abyss sprite");
    progWhale_ = compile(shaders::WHALE_VS, shaders::WHALE_FS, "abyss whale");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "abyss text");
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "abyss shape");
    progDown_ = compile(ugl::FULLSCREEN_VS, shaders::DOWN_FS, "abyss down");
    progUp_ = compile(ugl::FULLSCREEN_VS, shaders::UP_FS, "abyss up");
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "abyss post");

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
    instanced(spriteVao_, spriteVbo_, 3, sizeof(Sprite));

    glGenVertexArrays(1, &whaleVao_);
    glGenBuffers(1, &whaleVbo_);
    glBindVertexArray(whaleVao_);
    glBindBuffer(GL_ARRAY_BUFFER, whaleVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(WVert), nullptr);

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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 6, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)(sizeof(float) * 2));
    glBindVertexArray(0);

    buildFont(fontSans_, FONT_SANS);
    buildFont(fontSerif_, FONT_SERIF);
    pal_ = paletteOf(0);
    resize(width, height);

    // Marine snow: sparse, three depths (far tiny and dim, a few near, larger and soft).
    snow_.clear();
    for (int i = 0; i < 150; i++) {
        Flake f;
        f.z = rng_.uniform();
        f.z = f.z * f.z;
        if (i < 10) f.z = rng_.range(0.88f, 1.f);
        f.x = rng_.range(-viewH_.x, viewH_.x);
        f.y = rng_.range(-viewH_.y, viewH_.y) + viewC_.y;
        f.phase = rng_.range(0.f, TAU);
        f.size = rng_.range(0.6f, 1.4f);
        snow_.push_back(f);
    }
    return glGetError() == GL_NO_ERROR;
}

// Glyphs baked as signed distance fields: they scale cleanly, and the distance gives the soft living glow.
void AbyssUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] abyss font init failed\n");
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

void AbyssUniverse::resize(int width, int height) {
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

void AbyssUniverse::destroyTargets() {
    if (sceneFbo_) { glDeleteFramebuffers(1, &sceneFbo_); glDeleteTextures(1, &sceneTex_); }
    for (int i = 0; i < BLOOM_MIPS; i++)
        if (bloomFbo_[i]) { glDeleteFramebuffers(1, &bloomFbo_[i]); glDeleteTextures(1, &bloomTex_[i]); }
    sceneFbo_ = sceneTex_ = 0;
    for (int i = 0; i < BLOOM_MIPS; i++) bloomFbo_[i] = bloomTex_[i] = 0;
}

void AbyssUniverse::createTargets() {
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

AbyssUniverse::~AbyssUniverse() {
    destroyTargets();
    for (GLuint p : {progWorld_, progTile_, progSprite_, progWhale_, progText_, progShape_, progDown_, progUp_, progPost_})
        glDeleteProgram(p);
    for (GLuint v : {tileVao_, spriteVao_, whaleVao_, textVao_, shapeVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, tileVbo_, spriteVbo_, whaleVbo_, textVbo_, shapeVbo_}) glDeleteBuffers(1, &b);
    for (Font* f : {&fontSans_, &fontSerif_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// =====================================================================================================
// Song, level, bonus
// =====================================================================================================

// The variant from the song: the key's hue picks the palette's family; dark minor songs go to the deepest
// palettes, bright major ones to the pale lights.
int AbyssUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    const bool dark = t.hasSong && t.minor && t.brightness < 0.45f;
    const bool pale = t.hasSong && !t.minor && t.brightness > 0.6f;
    int v;
    if (dark) v = (h < 0.12f || h > 0.9f) ? 3 : (h < 0.45f ? 2 : (h < 0.7f ? 5 : 1));
    else if (pale) v = (h < 0.3f) ? 6 : (h < 0.62f ? 4 : 7);
    else if (h < 0.07f) v = 3;
    else if (h < 0.16f) v = 6;
    else if (h < 0.26f) v = 7;
    else if (h < 0.42f) v = 2;
    else if (h < 0.56f) v = 0;
    else if (h < 0.64f) v = 4;
    else if (h < 0.74f) v = 5;
    else if (h < 0.92f) v = 1;
    else v = 0;
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void AbyssUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    seedF_ = (float)(look.seed % 1000) * 0.0137f;
    rng_ = Rng(look.seed ^ 0xab755ull);
    if (seconds <= 0.f) palSet_ = false; // snap on the first frame
    else flash_ = std::max(flash_, 0.6f);
}

void AbyssUniverse::setLevel(int level, float energy, float seconds, float impact) {
    level = std::clamp(level, 0, 2);
    energy_ = energy;
    if (!hasLevel_ || seconds < 0.1f) {
        hasLevel_ = true;
        level_ = level;
        lv_ = lvFrom_ = lvTo_ = (float)level;
        lvT_ = 1.f;
        const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
        life_ = wm;
        peak_ = wp;
        rays_ = 1.f - 0.8f * wm;
        // a viewer's instant level: the creatures already there
        jellies_.clear();
        schools_.clear();
        siphons_.clear();
        const int nj = (int)std::round(wm * 3.f + wp * 2.f);
        for (int i = 0; i < nj; i++) spawnJelly(true);
        if (wm > 0.5f) { spawnSchool(); spawnSiphon(true); }
        if (wp > 0.5f) {
            spawnSchool();
            whale_.t = whale_.dur * 0.5f;
            whale_.dir = -1.f;
            whale_.y = 1.5f;
        }
        return;
    }
    const bool rising = level > level_;
    level_ = level;
    lvFrom_ = lv_;
    lvTo_ = (float)level;
    lvT_ = 0.f;
    lvDur_ = std::clamp(seconds, rising ? 0.6f : 1.5f, 5.f);
    if (rising) {
        flash_ = std::max(flash_, 0.4f + 0.6f * impact);
        if (level >= 2) bloom_ = std::max(bloom_, 0.5f + 0.5f * impact);
    }
}

// The drop's first downbeat: the bioluminescence surges through the water, then settles to the peak's bloom.
void AbyssUniverse::drop(float strength) {
    surge_ = std::max(surge_, saturate(0.6f + 0.4f * strength));
    bloom_ = std::max(bloom_, 0.8f * strength);
    flash_ = std::max(flash_, 0.6f * strength);
}

void AbyssUniverse::refresh(float strength) {
    flash_ = std::max(flash_, 0.5f * strength);
    if (strength > 0.4f) bloom_ = std::max(bloom_, 0.4f * strength);
}

void AbyssUniverse::setBonus(bool on) {
    if (on == slowOn_) return;
    slowOn_ = on;
    slowT_ = 0.f;
}

void AbyssUniverse::levelUp() {
    flash_ = std::max(flash_, 0.8f);
    bloom_ = std::max(bloom_, 0.5f);
}

vec2 AbyssUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints AbyssUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    h.background = lerp(p.deep, p.water, 0.35f);
    h.ink = p.glow * 0.8f;
    h.accent = p.glow2;
    h.light = false;
    h.styles = TS_INK | TS_LUMA; // an organic glowing dissolve, darks first (no rings: nothing expanding)
    return h;
}

// =====================================================================================================
// The inhabitants
// =====================================================================================================

// Jellyfish live in the open water on both sides of the well (behind the HUD columns they are dimmed).
void AbyssUniverse::spawnJelly(bool anywhere) {
    Jelly j;
    const float side = rng_.chance(0.5f) ? -1.f : 1.f;
    const float x0 = 12.f, x1 = std::max(13.f, viewH_.x - 1.5f);
    j.x = side * rng_.range(x0, x1);
    j.depth = rng_.range(0.f, 1.f);
    j.size = lerpf(0.55f, 1.35f, j.depth) * rng_.range(0.85f, 1.15f);
    j.y = anywhere ? rng_.range(-6.f, 8.f) : viewC_.y - viewH_.y - 4.f * j.size;
    j.vx = rng_.range(-0.12f, 0.12f);
    j.vy = 0.f;
    j.phase = rng_.range(0.f, 1.f);
    j.tilt = rng_.range(-0.25f, 0.25f);
    j.age = anywhere ? rng_.range(4.f, 10.f) : 0.f;
    j.life = rng_.range(40.f, 70.f);
    j.hue = rng_.chance(0.6f) ? -1 : (int)(rng_.uniform() * 7.f) % 7;
    j.seed = rng_.range(0.f, 100.f);
    jellies_.push_back(j);
}

void AbyssUniverse::spawnSchool() {
    School s;
    s.dir = rng_.chance(0.5f) ? 1.f : -1.f;
    s.x = -s.dir * (viewH_.x + 6.f);
    s.y = rng_.chance(0.5f) ? rng_.range(-8.5f, -6.5f) : rng_.range(6.f, 10.f);
    s.speed = rng_.range(1.6f, 2.6f);
    s.age = 0.f;
    s.life = (2.f * viewH_.x + 12.f) / s.speed;
    s.turn = 0.f;
    s.n = 26 + (int)(rng_.uniform() * 18.f);
    s.seed = rng_.range(0.f, 100.f);
    schools_.push_back(s);
}

void AbyssUniverse::spawnSiphon(bool anywhere) {
    Siphon s;
    const float side = rng_.chance(0.5f) ? -1.f : 1.f;
    s.x = side * rng_.range(13.5f, std::max(14.f, viewH_.x - 2.f));
    s.y = rng_.range(2.f, 9.f);
    s.vx = rng_.range(0.05f, 0.15f) * -side;
    s.len = rng_.range(9.f, 15.f);
    s.age = anywhere ? 6.f : 0.f;
    s.life = rng_.range(45.f, 70.f);
    s.seed = rng_.range(0.f, 100.f);
    s.depth = rng_.range(0.3f, 1.f);
    s.hue = rng_.chance(0.5f) ? 0 : 1;
    siphons_.push_back(s);
}

// =====================================================================================================
// Game events
// =====================================================================================================

void AbyssUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::Lock: {
        if (ev.cells.empty()) break;
        // The jelly quivers (in the shader, no glow); at the contact, a few motes of plankton stirred up from under it.
        {
            int bottom = -1;
            for (auto& q : ev.cells) bottom = std::max(bottom, q.y);
            float x0 = 1e9f, x1 = -1e9f;
            for (auto& q : ev.cells)
                if (q.y == bottom) { x0 = std::min(x0, (float)q.x); x1 = std::max(x1, (float)q.x); }
            const vec3 a = layout::cellPos(x0, (float)bottom), b = layout::cellPos(x1, (float)bottom);
            const vec3 col = pal_.piece[std::clamp(ev.cells[0].type, 0, 6)];
            const int n = 3 + (int)(x1 - x0) * 2;
            for (int i = 0; i < n; i++) {
                Spark s;
                s.x = rng_.range(a.x - 0.4f, b.x + 0.4f);
                s.y = a.y - 0.48f;
                s.vx = (s.x - (a.x + b.x) * 0.5f) * 0.9f + rng_.range(-0.2f, 0.2f);
                s.vy = rng_.range(0.05f, 0.35f);
                s.t = 0.f; s.life = rng_.range(0.4f, 0.7f); s.delay = 0.f; s.size = rng_.range(0.035f, 0.055f);
                s.col = lerp(col, vec3(1.f), 0.2f); s.twinkle = rng_.range(0.f, 10.f); s.kind = 1;
                sparks_.push_back(s);
            }
        }
        break;
    }
    case GameEvent::Clear: {
        const bool tetris = ev.count >= 4;
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            const int type = std::clamp(c.type, 0, 6);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0.f; d.delay = std::fabs(c.x - 4.5f) * 0.025f; d.out = 0.f;
            d.vx = d.vy = 0.f; d.type = type; d.mask = 0; d.sink = false;
            dying_.push_back(d);
            // the row bursts into plankton sparks drifting up
            const int n = tetris ? 4 : 3;
            for (int i = 0; i < n; i++) {
                Spark s;
                s.x = p.x + rng_.range(-0.45f, 0.45f);
                s.y = p.y + rng_.range(-0.45f, 0.45f);
                const float a = rng_.range(0.f, TAU);
                const float sp = rng_.range(0.4f, 1.8f);
                s.vx = std::cos(a) * sp;
                s.vy = std::sin(a) * sp + rng_.range(0.3f, 0.9f);
                s.t = 0.f; s.life = rng_.range(0.5f, 0.9f); s.delay = d.delay; s.size = rng_.range(0.04f, 0.1f);
                s.col = lerp(pal_.piece[type], pal_.glow, rng_.range(0.f, 0.5f));
                s.twinkle = rng_.range(0.f, 10.f); s.kind = 0;
                sparks_.push_back(s);
            }
        }
        if (tetris) {
            // the plankton blooms all through the water at once (no front sweeping the screen): bursts of light
            tetrisT_ = 0.f;
            const float bot = viewC_.y - viewH_.y;
            for (int i = 0; i < 220; i++) {
                Spark s;
                s.x = rng_.range(-viewH_.x, viewH_.x);
                s.y = rng_.range(bot, viewC_.y + viewH_.y);
                s.vx = rng_.range(-0.3f, 0.3f);
                s.vy = rng_.range(0.2f, 0.9f);
                s.t = 0.f; s.life = rng_.range(1.0f, 2.0f); s.delay = rng_.range(0.f, 0.35f); s.size = rng_.range(0.04f, 0.11f);
                s.col = lerp(pal_.glow, pal_.glow2, rng_.uniform()); s.twinkle = rng_.range(0.f, 10.f); s.kind = 0;
                sparks_.push_back(s);
            }
            bloom_ = 1.f;
        } else {
            bloom_ = std::max(bloom_, 0.2f * ev.count);
        }
        break;
    }
    case GameEvent::BonusReady:
        readyT_ = 0.f;
        break;
    case GameEvent::TopOut: {
        // The stack sinks into the dark, its lights going out one by one.
        auto typeAt = [&](int x, int y) {
            for (auto& o : ev.cells)
                if (o.x == x && o.y == y) return o.type;
            return -1;
        };
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0.f; d.delay = 0.f;
            d.out = rng_.range(0.3f, 3.2f);
            d.vx = rng_.range(-0.15f, 0.15f);
            d.vy = 0.f;
            d.type = std::clamp(c.type, 0, 6);
            d.mask = 0;
            if (typeAt(c.x - 1, c.y) == c.type) d.mask |= 1;
            if (typeAt(c.x + 1, c.y) == c.type) d.mask |= 2;
            if (typeAt(c.x, c.y - 1) == c.type) d.mask |= 4;
            if (typeAt(c.x, c.y + 1) == c.type) d.mask |= 8;
            d.sink = true;
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

void AbyssUniverse::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    worldTime_ += wdt;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    life_ = approach(life_, wm, 0.4f, dt);
    peak_ = approach(peak_, wp, 0.5f, dt);
    rays_ = approach(rays_, 1.f - 0.8f * wm, 0.3f, dt);

    // The beat: creatures contract and flash on each beat (mid, peak); a slow breath without a song.
    // half time: the scene's pulses come every two beats (on every beat they are tiring to watch)
    const double b2 = music.beat / 2.0;
    float ph = music.hasSong ? (float)(b2 - std::floor(b2)) : std::fmod(worldTime_ * 0.25f, 1.f);
    beatPh_ = ph;
    const float beatW = music.hasSong ? (0.35f + 0.65f * wm) : 0.4f;
    beatGlow_ = std::exp(-ph * 4.5f) * beatW * (0.6f + 0.4f * wp) + 0.3f * saturate(music.beatPulse) * wp;
    const bool beatHit = music.hasSong && ph < lastPh_;
    lastPh_ = ph;
    // the bar's downbeat a little stronger
    const bool downbeat = music.hasSong && ((long long)std::floor(music.beat + 0.5) % 4 + 4) % 4 == 0;
    const float accent = downbeat ? 1.f : 0.7f;

    // The peak pulses on the beat grid (steady, every two beats, the downbeat stronger; the detected kicks are
    // irregular):
    // the plankton flares (no ring of light: nothing expanding).
    sinceKick_ += dt;
    const float kick = std::max(beatHit ? accent : 0.f, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    if ((beatHit || (kickIn_ > 0.55f && prevKick_ <= 0.55f)) && peak_ > 0.3f) {
        sinceKick_ = 0.f;
        const float side = rng_.chance(0.5f) ? -1.f : 1.f;
        const float cx = rng_.chance(0.3f) ? rng_.range(-3.f, 3.f) : side * rng_.range(9.f, viewH_.x - 2.f);
        const float cy = rng_.range(-6.f, 8.f);
        bloom_ = std::max(bloom_, 0.45f + 0.35f * kick);
        // a little cloud of plankton flaring where it hits
        for (int i = 0; i < 10; i++) {
            Spark s;
            s.x = cx + rng_.range(-1.5f, 1.5f);
            s.y = cy + rng_.range(-1.5f, 1.5f);
            s.vx = rng_.range(-0.4f, 0.4f); s.vy = rng_.range(-0.1f, 0.5f);
            s.t = 0.f; s.life = rng_.range(1.2f, 2.4f); s.delay = rng_.range(0.f, 0.2f); s.size = rng_.range(0.04f, 0.08f);
            s.col = lerp(pal_.glow, pal_.glow2, rng_.uniform()); s.twinkle = rng_.range(0.f, 10.f); s.kind = 0;
            sparks_.push_back(s);
        }
    }
    prevKick_ = kick;
    // the pulse's envelope, from the beat phase itself (the same shape on every beat)
    kickEnv_ = std::max(music.hasSong ? std::exp(-ph * 4.f) * accent : 0.f, kickIn_);
    bloom_ = approach(bloom_, 0.f, 1.2f, dt);
    surge_ = approach(surge_, 0.f, 0.45f, dt); // a few seconds
    // A drop's peak against a chorus' one (both the top level): the drop's sections drive harder (the song
    // structure's pace), and the bloom goes further.
    drive_ = approach(drive_, peak_ * smoothstepf(1.27f, 1.42f, music.hasSong ? music.speed : 1.f), 0.8f, dt);
    flash_ = approach(flash_, 0.f, 1.5f, dt);
    tetrisT_ += dt;

    // Marine snow sinks slowly, wandering.
    const float bot = viewC_.y - viewH_.y, top = viewC_.y + viewH_.y;
    for (Flake& f : snow_) {
        f.y -= (0.10f + 0.35f * f.z) * wdt;
        f.x += 0.06f * std::sin(worldTime_ * 0.3f + f.phase) * wdt;
        if (f.y < bot - 0.5f) { f.y = top + 0.5f; f.x = rng_.range(-viewH_.x, viewH_.x); }
    }

    // Jellyfish (mid, peak): rise on each contraction, drift, fade away.
    {
        const int target = (int)std::round(life_ * 3.f + peak_ * 2.f);
        jellyTimer_ -= wdt;
        if ((int)jellies_.size() < target && jellyTimer_ <= 0.f) { spawnJelly(false); jellyTimer_ = rng_.range(3.f, 7.f); }
        for (Jelly& j : jellies_) {
            j.age += wdt;
            const float k = std::exp(-ph * 4.f);
            j.vy = approach(j.vy, 0.08f + 0.55f * k * j.size, 4.f, wdt);
            j.y += j.vy * wdt * (0.6f + 0.4f * j.size);
            j.x += j.vx * wdt + 0.05f * std::sin(worldTime_ * 0.2f + j.seed) * wdt;
            j.tilt = 0.22f * std::sin(worldTime_ * 0.17f + j.seed) + j.vx * 0.8f;
            // too many: the oldest leave sooner
            if ((int)jellies_.size() > target && j.life > j.age + 6.f && rng_.chance(wdt * 0.1f)) j.life = j.age + 6.f;
        }
        jellies_.erase(std::remove_if(jellies_.begin(), jellies_.end(), [&](const Jelly& j) {
            return j.age > j.life || j.y > top + 6.f;
        }), jellies_.end());
    }
    // Schools of lanternfish (mid, peak).
    {
        const int target = life_ > 0.5f ? (peak_ > 0.5f ? 2 : 1) : 0;
        schoolTimer_ -= wdt;
        if ((int)schools_.size() < target && schoolTimer_ <= 0.f) { spawnSchool(); schoolTimer_ = rng_.range(6.f, 14.f); }
        for (School& s : schools_) {
            s.age += wdt;
            s.x += s.dir * s.speed * wdt * (1.f + 0.6f * s.turn);
            s.y += 0.3f * std::sin(s.age * 0.4f + s.seed) * wdt;
            s.turn = approach(s.turn, 0.f, 1.5f, dt);
        }
        if (kick > 0.7f && sinceKick_ < 0.02f)
            for (School& s : schools_) s.turn = 1.f; // they dart
        schools_.erase(std::remove_if(schools_.begin(), schools_.end(), [&](const School& s) {
            return s.age > s.life + 4.f;
        }), schools_.end());
    }
    // Siphonophores (mid, peak).
    {
        const int target = life_ > 0.4f ? 1 : 0;
        siphonTimer_ -= wdt;
        if ((int)siphons_.size() < target && siphonTimer_ <= 0.f) { spawnSiphon(false); siphonTimer_ = rng_.range(10.f, 20.f); }
        for (Siphon& s : siphons_) {
            s.age += wdt;
            s.x += s.vx * wdt;
            if (target == 0 && s.life > s.age + 4.f) s.life = s.age + 4.f;
        }
        siphons_.erase(std::remove_if(siphons_.begin(), siphons_.end(), [](const Siphon& s) { return s.age > s.life; }),
                       siphons_.end());
    }
    // The whale (peak): one slow pass behind the well, then a long rest.
    whaleCool_ -= wdt;
    if (whale_.t < 0.f && peak_ > 0.6f && whaleCool_ <= 0.f) {
        whale_.t = 0.f;
        whale_.dir = rng_.chance(0.5f) ? -1.f : 1.f;
        whale_.y = rng_.range(0.5f, 3.f);
        whale_.size = rng_.range(0.95f, 1.1f);
    }
    if (whale_.t >= 0.f) {
        whale_.t += wdt;
        whale_.swim += wdt * 0.9f;
        if (whale_.t > whale_.dur) { whale_.t = -1.f; whaleCool_ = 25.f; }
    }

    // Effects.
    for (Spark& s : sparks_) {
        s.t += dt;
        const float t = s.t - s.delay;
        if (t < 0.f) continue;
        const float drag = s.kind == 1 ? 2.8f : 1.1f;
        s.vx *= std::exp(-drag * dt);
        s.vy *= std::exp(-drag * dt);
        if (s.kind == 0) {
            s.vy += 0.35f * dt; // buoyant: plankton drifts up
            s.vx += 0.25f * std::sin(s.t * 2.f + s.twinkle) * dt;
        }
        s.x += s.vx * dt;
        s.y += s.vy * dt;
    }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(), [](const Spark& s) { return s.t > s.delay + s.life; }),
                  sparks_.end());
    if (sparks_.size() > 1800) sparks_.erase(sparks_.begin(), sparks_.begin() + (sparks_.size() - 1800));
    for (Dying& d : dying_) {
        d.t += dt;
        if (d.sink) {
            d.vy -= 0.55f * dt;
            d.vy *= std::exp(-0.4f * dt);
            d.y += d.vy * dt;
            d.x += d.vx * dt;
        }
    }
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) {
        return d.sink ? d.t > 7.f : d.t > d.delay + 0.5f;
    }), dying_.end());

    // The anglerfish's lure wanders slowly beside the well while the bonus lasts.
    lureX_ = -13.8f + 1.3f * std::sin(slowT_ * 0.23f) ;
    lureY_ = -3.4f + 1.f * std::sin(slowT_ * 0.37f + 1.f);

    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
    readyT_ += dt;
}

// =====================================================================================================
// Geometry builders
// =====================================================================================================

void AbyssUniverse::tile(float x, float y, float hw, float hh, const vec3& c, float alpha, int kind, float age, int type,
                         int mask, float scale, float light, float e2, float e3) {
    Tile t{{x, y, hw, hh}, {c.x, c.y, c.z, alpha}, {(float)kind, age, (float)type, (float)mask}, {scale, light, e2, e3}};
    tiles_.push_back(t);
}

void AbyssUniverse::sprite(std::vector<Sprite>& out, int kind, vec2 a, vec2 b, float size, const vec3& c, float alpha,
                           float p0, float p1) {
    if (alpha <= 0.003f) return;
    Sprite s{{a.x, a.y, b.x, b.y}, {c.x, c.y, c.z, alpha}, {(float)kind, size, p0, p1}};
    out.push_back(s);
}

void AbyssUniverse::buildLife(const MusicState& music) {
    (void)music;
    const float dark = (1.f - 0.95f * slow_) * (1.f - 0.6f * over_);
    const float out = std::max(saturate(bridge_.amount * 1.25f), over_ * 0.85f); // lights going out one by one
    auto alive = [&](float h) { return smoothstepf(h, h + 0.12f, 1.f - out); };
    const float wm = life_;

    // Marine snow: faint, sparse, lit a little more in the calm.
    {
        const float lit = (0.55f + 0.45f * rays_) * dark * (1.f - saturate(bridge_.amount));
        for (const Flake& f : snow_) {
            const float s = lerpf(0.018f, 0.06f, f.z) * f.size;
            const float a = lerpf(0.10f, 0.42f, f.z) * lit * (0.8f + 0.2f * std::sin(worldTime_ * 0.7f + f.phase));
            sprite(f.z > 0.85f ? front_ : back_, 0, v2(f.x, f.y), v2(0, 0), s, pal_.snow * (f.z > 0.85f ? 0.35f : 0.5f), a);
        }
    }
    // Worms and anemones on the seabed: glowing tips, pulsing with the beat.
    for (int i = 0; i < 16; i++) {
        const float h = hashf(i * 7.31f + seedF_);
        float x = (i / 15.f * 2.f - 1.f) * (viewH_.x - 1.f) + (h - 0.5f) * 2.2f;
        if (std::fabs(x) < 6.2f) x = (x < 0 ? -1.f : 1.f) * (6.2f + h * 1.5f);
        const float by = bedY(x, seedF_) - 0.05f;
        const float tall = 0.25f + 0.6f * hashf(i * 3.1f + 2.f);
        const float sway = 0.08f * std::sin(worldTime_ * 0.9f + i);
        const vec3 c = (i % 3 == 0) ? pal_.glow2 : pal_.glow;
        const float a = (0.35f + 0.4f * wm + 0.5f * beatGlow_ * ((i & 1) ? 1.f : 0.6f)) * dark * alive(hashf(i * 1.7f));
        sprite(back_, 3, v2(x, by), v2(x + sway, by + tall), 0.02f, c * 0.25f, a);
        sprite(back_, 4, v2(x + sway, by + tall), v2(0, 0), 0.05f, c * 0.8f, a);
    }
    // Siphonophores: a long chain of glowing beads, a light running along it on each beat.
    for (const Siphon& s : siphons_) {
        const float fade = smoothstepf(0.f, 5.f, s.age) * (1.f - smoothstepf(s.life - 5.f, s.life, s.age));
        const float a0 = fade * dark * alive(hashf(s.seed)) * lerpf(0.45f, 1.f, s.depth);
        if (a0 <= 0.01f) continue;
        const vec3 c = s.hue ? pal_.glow2 : pal_.glow;
        const int N = 46;
        vec2 prev{};
        for (int i = 0; i < N; i++) {
            const float u = i / (float)(N - 1);
            const float y = s.y - u * s.len;
            const float x = s.x + 0.9f * std::sin(u * 3.2f + worldTime_ * 0.25f + s.seed) * u + 0.3f * std::sin(u * 9.f + worldTime_ * 0.6f);
            const vec2 p = v2(x, y);
            if (i > 0) sprite(back_, 3, prev, p, 0.015f, c * 0.18f, a0);
            prev = p;
            const float run = std::exp(-std::pow((u - beatPh_ * 1.2f) / 0.08f, 2.f)) * (0.4f + 0.6f * wm);
            const float bead = (i % 3 == 0) ? 1.f : 0.45f;
            sprite(back_, 0, p, v2(0, 0), 0.07f * bead * (1.f - 0.4f * u) + 0.02f, c * (0.5f + 1.6f * run),
                   a0 * (0.5f + 0.5f * bead));
        }
    }
    // Jellyfish.
    for (const Jelly& j : jellies_) {
        const float fade = smoothstepf(0.f, 4.f, j.age) * (1.f - smoothstepf(j.life - 6.f, j.life, j.age));
        const float behindHud = (std::fabs(j.x) < 12.f && std::fabs(j.x) > 5.5f && j.y > -7.f) ? 0.35f : 1.f;
        const float a = fade * dark * alive(hashf(j.seed)) * lerpf(0.5f, 1.f, j.depth) * behindHud;
        if (a <= 0.01f) continue;
        const vec3 c = j.hue < 0 ? lerp(pal_.glow, pal_.glow2, hashf(j.seed) * 0.6f) : pal_.piece[j.hue];
        const float k = std::exp(-beatPh_ * 4.f) * (0.4f + 0.6f * std::max(wm, 0.3f));
        Sprite s{{j.x, j.y, j.tilt, j.seed}, {c.x, c.y, c.z, a}, {1.f, j.size, k, 0.8f + 0.5f * beatGlow_}};
        back_.push_back(s);
    }
    // Lanternfish schools: specks of light swimming together.
    for (const School& s : schools_) {
        const float a0 = dark * alive(hashf(s.seed)) * smoothstepf(0.f, 2.f, s.age) * 0.9f;
        for (int i = 0; i < s.n; i++) {
            const float h1 = hashf(s.seed + i * 1.37f), h2 = hashf(s.seed + i * 2.71f), h3 = hashf(s.seed * 2.f + i);
            const float ox = (h1 - 0.5f) * 5.f * (1.f - 0.4f * s.turn) + 0.4f * std::sin(worldTime_ * 1.3f + i);
            const float oy = (h2 - 0.5f) * 2.2f * (1.f - 0.4f * s.turn) + 0.25f * std::sin(worldTime_ * 1.7f + i * 0.7f);
            const vec2 p = v2(s.x + ox, s.y + oy + 0.3f * std::sin(s.x * 0.4f + h3 * 6.f));
            if (std::fabs(p.x) < WELL_X + 0.3f && p.y < WELL_TOP) continue; // never in front of the stack
            const float tw = 0.6f + 0.4f * std::sin(worldTime_ * (2.f + 3.f * h3) + i);
            const vec3 c = h3 > 0.8f ? pal_.glow2 : pal_.glow;
            sprite(back_, 4, p, v2(0, 0), 0.035f + 0.02f * h2, c * 0.9f, a0 * tw * (0.7f + 0.5f * beatGlow_));
        }
    }
}

// The whale: a long dark body with a lateral row of lights, a long flipper, flukes beating slowly.
void AbyssUniverse::buildWhale() {
    whaleMesh_.clear();
    if (whale_.t < 0.f) return;
    const float Lw = 30.f * whale_.size, H = 0.1f * Lw;
    const float travel = 2.f * (viewH_.x + Lw * 0.5f + 4.f);
    const float k = whale_.t / whale_.dur;
    const float dir = whale_.dir;
    const float cx = -dir * travel * 0.5f + dir * travel * k;
    const float cy = whale_.y + 0.6f * std::sin(whale_.t * 0.12f);
    const vec2 head = v2(cx + dir * Lw * 0.5f, cy);
    auto spine = [&](float s) {
        const float y = cy + 0.9f * std::sin(whale_.swim - s * 3.2f) * std::pow(s, 2.2f) - 0.6f * s * s;
        return v2(head.x - dir * s * Lw, y + 0.15f * std::sin(whale_.swim * 0.5f));
    };
    auto radius = [&](float s, float& top, float& bottom) {
        float r = H * (0.12f + 0.88f * std::sqrt(smoothstepf(0.f, 0.24f, s))) * (1.f - 0.9f * std::pow(saturate((s - 0.25f) / 0.72f), 0.95f));
        top = r * 0.85f;
        bottom = r * (1.15f - 0.2f * smoothstepf(0.4f, 0.9f, s));
        if (s > 0.62f && s < 0.72f) top += H * 0.22f * std::sin((s - 0.62f) / 0.1f * PI) * (s < 0.69f ? 1.f : 0.6f); // dorsal fin
    };
    auto strip = [&](auto&& point, auto&& halfw, int N, float u0, float u1) {
        for (int i = 0; i < N; i++) {
            const float a = i / (float)N, b = (i + 1) / (float)N;
            vec2 pa = point(a), pb = point(b);
            vec2 ta = norm(point(std::min(1.f, a + 0.01f)) - point(std::max(0.f, a - 0.01f)));
            vec2 tb = norm(point(std::min(1.f, b + 0.01f)) - point(std::max(0.f, b - 0.01f)));
            vec2 na = v2(-ta.y, ta.x) * dir * -1.f, nb = v2(-tb.y, tb.x) * dir * -1.f; // "up" side of the body
            if (na.y < 0) na = na * -1.f;
            if (nb.y < 0) nb = nb * -1.f;
            float ta0, ba0, tb0, bb0;
            halfw(a, ta0, ba0);
            halfw(b, tb0, bb0);
            const float ua = lerpf(u0, u1, a), ub = lerpf(u0, u1, b);
            const WVert A{pa.x + na.x * ta0, pa.y + na.y * ta0, ua, 1.f}, B{pa.x - na.x * ba0, pa.y - na.y * ba0, ua, -1.f};
            const WVert C{pb.x + nb.x * tb0, pb.y + nb.y * tb0, ub, 1.f}, D{pb.x - nb.x * bb0, pb.y - nb.y * bb0, ub, -1.f};
            whaleMesh_.insert(whaleMesh_.end(), {A, B, C, B, D, C});
        }
    };
    // the flipper (behind the body: drawn first)
    {
        const vec2 base = spine(0.27f);
        const float sw = 0.25f * std::sin(whale_.swim * 0.7f);
        const vec2 tip = base + v2(-dir * Lw * 0.15f, -Lw * 0.15f) + v2(0.f, sw * 2.f);
        auto pt = [&](float s) { return base + (tip - base) * s + v2(0.f, -std::sin(s * PI) * 0.6f); };
        auto hw = [&](float s, float& t, float& b) { t = b = H * 0.32f * (1.f - 0.8f * s) * smoothstepf(0.f, 0.08f, s + 0.04f); };
        strip(pt, hw, 14, 2.f, 3.f);
    }
    strip(spine, radius, 64, 0.f, 1.f);
    // the flukes: seen nearly edge on, two lobes sweeping back from the tail stock
    {
        const vec2 t0 = spine(0.985f), t1 = spine(0.95f);
        const vec2 tg = norm(t0 - t1);
        const float beat = 0.35f * std::sin(whale_.swim - 3.2f);
        for (int lobe = -1; lobe <= 1; lobe += 2) {
            const float ang = std::atan2(tg.y, tg.x) + lobe * 0.6f + beat;
            const vec2 d = v2(std::cos(ang), std::sin(ang));
            auto pt = [&](float s) { return t0 + d * (s * Lw * 0.14f); };
            auto hw = [&](float s, float& t, float& b) { t = b = H * 0.13f * std::sin(PI * std::min(1.f, s * 0.85f + 0.15f)); };
            strip(pt, hw, 10, 2.f, 3.f);
        }
    }
}

void AbyssUniverse::buildBoard(const Game& g) {
    // Locked cells: same-type neighbors merge into one jelly mass (a faint seam stays at the cells' edges).
    auto sameAt = [&](int x, int y, int type, float off) {
        if (x < 0 || x >= Game::W || y < Game::HIDDEN || y >= Game::H) return false;
        const Cell& c = g.cell(x, y);
        return c.type == type && std::fabs(g.rowOffset(y) - off) < 0.01f;
    };
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const float off = g.rowOffset(y);
            vec3 p = layout::cellPos((float)x, y - off);
            int mask = 0;
            if (sameAt(x - 1, y, c.type, off)) mask |= 1;
            if (sameAt(x + 1, y, c.type, off)) mask |= 2;
            if (sameAt(x, y - 1, c.type, off)) mask |= 4;
            if (sameAt(x, y + 1, c.type, off)) mask |= 8;
            const float age = c.flash > 0.f ? (1.f - c.flash) * Game::LOCK_FX_SECONDS : -1.f;
            tile(p.x, p.y, 0.5f, 0.5f, pal_.piece[c.type], 1.f, 0, age, c.type, mask, 1.f, 1.f);
        }
    // Cleared cells bursting; a topped-out stack sinking, its lights going out.
    for (const Dying& d : dying_) {
        if (d.sink) {
            const float on = 1.f - smoothstepf(d.out - 0.05f, d.out + 0.05f, d.t);
            // a last flicker before the light goes out
            const float flick = (d.t > d.out - 0.35f && d.t < d.out) ? (std::sin(d.t * 60.f + d.x * 3.f) > 0.f ? 1.f : 0.4f) : 1.f;
            const float a = 1.f - smoothstepf(3.f, 7.f, d.t);
            tile(d.x, d.y, 0.5f, 0.5f, pal_.piece[d.type], a, 0, -1.f, d.type, d.mask, 1.f, 0.08f + 0.92f * on * flick);
            continue;
        }
        const float t = d.t - d.delay;
        const float k = saturate(t / 0.5f);
        tile(d.x, d.y, 0.5f, 0.5f, pal_.piece[d.type], 1.f, 2, -1.f, d.type, 0, k);
    }
    // The falling piece and its ghost.
    if (g.hasPiece()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        const vec3 col = pal_.piece[p.type];
        auto pieceMask = [&](int (&cs)[4][2], int i) {
            int m = 0;
            for (int j = 0; j < 4; j++) {
                if (j == i) continue;
                const int dx = cs[j][0] - cs[i][0], dy = cs[j][1] - cs[i][1];
                if (dx == -1 && dy == 0) m |= 1;
                if (dx == 1 && dy == 0) m |= 2;
                if (dx == 0 && dy == -1) m |= 4;
                if (dx == 0 && dy == 1) m |= 8;
            }
            return m;
        };
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (auto& c : gc) {
                if (c[1] < Game::HIDDEN) continue;
                vec3 w = layout::cellPos((float)c[0], (float)c[1]);
                tile(w.x, w.y, 0.5f, 0.5f, col, 0.8f, 1, -1.f, p.type, 0);
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (int i = 0; i < 4; i++) {
            if (cells[i][1] < Game::HIDDEN - 1) continue;
            vec3 w = layout::cellPos(cells[i][0] + dx, cells[i][1] + dy);
            tile(w.x, w.y, 0.5f, 0.5f, col, 1.f, 3, -1.f, p.type, pieceMask(cells, i), 1.f, 1.2f);
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
        for (int i = 0; i < 4; i++) {
            int m = 0;
            for (int j = 0; j < 4; j++) {
                const int ddx = cells[j][0] - cells[i][0], ddy = cells[j][1] - cells[i][1];
                if (ddx == -1 && ddy == 0) m |= 1;
                if (ddx == 1 && ddy == 0) m |= 2;
                if (ddx == 0 && ddy == -1) m |= 4;
                if (ddx == 0 && ddy == 1) m |= 8;
            }
            vec2 pos = v2(center.x + (cells[i][0] - (minx + maxx) * 0.5f) * sc, center.y - (cells[i][1] - (miny + maxy) * 0.5f) * sc);
            tile(pos.x, pos.y, 0.5f * sc, 0.5f * sc, pal_.piece[type], alpha, 3, -1.f, type, m, sc, 1.f);
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.35f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), i == 0 ? 1.f : 0.7f);
    }
    // The bonus gauge: an orb filling with living light.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        tile(gc.x, gc.y, R * 1.8f, R * 1.8f, vec3(1.f), 1.f, 4, -1.f, 0, 0, saturate(gaugeShown_),
             g.bonusReady() ? 1.f : 0.f, g.bonusActive() ? 1.f : 0.f, R);
    }
}

void AbyssUniverse::buildFx() {
    const float world = 1.f - saturate(bridge_.amount);
    // The well's glass walls: two faint threads of light, a ledge at the bottom.
    {
        const vec3 c = pal_.glow * (0.22f + 0.12f * beatGlow_) * (1.f - 0.6f * slow_);
        const float a = 0.8f * (1.f - 0.5f * saturate(bridge_.amount));
        sprite(back_, 3, v2(-WELL_X, WELL_BOT), v2(-WELL_X, WELL_TOP), 0.02f, c, a);
        sprite(back_, 3, v2(WELL_X, WELL_BOT), v2(WELL_X, WELL_TOP), 0.02f, c, a);
        sprite(back_, 3, v2(-WELL_X, WELL_BOT), v2(WELL_X, WELL_BOT), 0.025f, c * 1.3f, a);
    }
    for (const Spark& s : sparks_) {
        const float t = s.t - s.delay;
        if (t < 0.f) continue;
        const float k = t / s.life;
        const float a = smoothstepf(0.f, 0.08f, t) * (1.f - k) * (1.f - k) * (0.65f + 0.35f * std::sin(s.t * 7.f + s.twinkle));
        sprite(fx_, s.kind == 1 ? 4 : 0, v2(s.x, s.y), v2(0, 0), s.size, s.col * (1.6f - 0.6f * k), a * world);
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float AbyssUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
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

float AbyssUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
                               std::vector<float>& out) const {
    if (!f.ok) return 0.f;
    const stbtt_fontinfo* info = (const stbtt_fontinfo*)f.info;
    const float k = size / f.px;
    const float cap = fontSans_.capH / fontSans_.px * size; // the sans' cap height sets the line
    const float baseline = y + cap;
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

void AbyssUniverse::drawText(const Font& f, const std::vector<float>& q, const TextStyle& st) {
    if (q.empty() || !f.ok) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    set3f(progText_, "uColor", st.color);
    set3f(progText_, "uGlow", st.glow);
    set1f(progText_, "uAlpha", st.alpha);
    set1f(progText_, "uGlowAmt", st.glowAmt);
    set1f(progText_, "uWeight", st.weight);
    set1f(progText_, "uShadow", st.shadow);
    set1f(progText_, "uBridge", smoothstepf(0.3f, 0.95f, bridge_.amount));
    set3f(progText_, "uBridgeInk", bridge_.ink);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(float), q.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 4));
}

// The HUD in ABYSS's typography, like a nature film's titles: a light humanist sans, widely spaced, for labels and
// numbers, a Garamond italic for the universe's name, the song's section and the celebrations; everything with a
// faint living glow.
void AbyssUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    if (!panels.empty()) {
        std::vector<float> v;
        for (const HudRect& r : panels) {
            const vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? pal_.glow * 0.35f : pal_.deep * 0.6f;
            const float a = r.accent || r.custom ? r.alpha : std::min(1.f, r.alpha * 1.2f);
            const float pts[6][2] = {{r.x, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, {r.x, r.y}, {r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}};
            for (auto& p : pts) v.insert(v.end(), {p[0], p[1], c.x, c.y, c.z, a});
        }
        glUseProgram(progShape_);
        set2f(progShape_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(shapeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 6));
    }
    const vec3 pale = lerp(vec3(0.85f, 0.95f, 1.f), pal_.snow, 0.5f);
    const vec3 glowC = pal_.glow;
    std::vector<float> q;
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        TextStyle st;
        st.alpha = saturate(h.alpha);
        q.clear();
        const Font* font = &fontSans_;
        auto place = [&](const Font& f, const std::string& text, float size, float track) {
            const float w = textWidth(f, text, size, track);
            const float x = ugl::hudLeft(h, w, width_);
            textQuads(f, text, x, h.y, size, track, q);
            font = &f;
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            // "Abyss" in italic, the variant in small spaced capitals after it.
            std::string name = h.text, var;
            size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            name = titleCase(name);
            const float sz = base * 1.6f, vsz = base * 0.72f;
            const float wName = textWidth(fontSerif_, name, sz, 0.f);
            const float wVar = var.empty() ? 0.f : textWidth(fontSans_, var, vsz, vsz * 0.3f);
            const float gap = base * 0.8f;
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            textQuads(fontSerif_, name, x, h.y - base * 0.1f, sz, 0.f, q);
            st.color = lerp(pale, glowC, 0.35f); st.glow = glowC * 0.8f; st.glowAmt = 0.55f;
            st.alpha = saturate(0.35f + h.alpha * 1.6f);
            drawText(fontSerif_, q, st);
            if (!var.empty()) {
                q.clear();
                textQuads(fontSans_, var, x + wName + gap, h.y + base * 0.25f, vsz, vsz * 0.3f, q);
                TextStyle vs;
                vs.color = glowC * 0.9f; vs.glow = glowC * 0.5f; vs.glowAmt = 0.3f; vs.weight = 0.03f;
                vs.alpha = saturate(0.3f + h.alpha * 1.5f);
                drawText(fontSans_, q, vs);
            }
            continue;
        }
        case HUD_SECTION:
            place(fontSerif_, titleCase(h.text), base * 1.35f, base * 0.02f);
            st.color = lerp(glowC, pale, 0.3f); st.glow = glowC * 0.7f; st.glowAmt = 0.45f;
            st.alpha = saturate(0.2f + h.alpha * 1.9f);
            break;
        case HUD_TITLE:
            place(fontSans_, h.text, base * 0.95f, base * 0.06f);
            st.color = pale; st.glow = glowC * 0.5f; st.glowAmt = 0.3f; st.weight = 0.03f;
            st.alpha = saturate(h.alpha * 1.2f);
            break;
        case HUD_INFO:
            place(fontSans_, h.text, base * 0.8f, base * 0.22f);
            st.color = lerp(pal_.glow2, pale, 0.4f) * 0.85f; st.glow = pal_.glow2 * 0.4f; st.glowAmt = 0.2f; st.weight = 0.03f;
            st.alpha = saturate(h.alpha * 1.3f);
            break;
        case HUD_LABEL:
            place(fontSans_, h.text, base * 0.78f, base * 0.34f);
            st.color = glowC * 0.95f; st.glow = glowC * 0.6f; st.glowAmt = 0.4f; st.weight = 0.04f;
            st.alpha = saturate(h.alpha * 1.4f);
            break;
        case HUD_VALUE:
            place(fontSans_, h.text, base * 1.1f, base * 0.06f);
            st.color = pale; st.glow = glowC * 0.45f; st.glowAmt = 0.25f; st.weight = 0.02f;
            break;
        case HUD_NOTICE:
            place(fontSerif_, titleCase(h.text), base * 1.05f, base * 0.02f);
            for (size_t i = 1; i < q.size(); i += 4) q[i] += base * 0.2f;
            st.color = lerp(glowC, pale, 0.4f); st.glow = glowC * 0.8f; st.glowAmt = 0.5f;
            break;
        case HUD_KEY:
            place(fontSans_, h.text, base * 0.95f, base * 0.05f);
            st.color = vec3(1.f); st.weight = 0.06f; st.shadow = 1.f;
            break;
        case HUD_TOAST:
            place(fontSerif_, titleCase(h.text), base * 1.3f, base * 0.03f);
            for (size_t i = 1; i < q.size(); i += 4) q[i] -= base * 0.3f; // the italic sits higher, clear of the line under it
            st.color = lerp(pale, glowC, 0.25f) * 1.1f; st.glow = glowC; st.glowAmt = 0.8f;
            break;
        case HUD_TOAST_SUB:
            place(fontSans_, h.text, base * 0.9f, base * 0.2f);
            st.color = pal_.glow2; st.glow = pal_.glow2 * 0.7f; st.glowAmt = 0.45f; st.weight = 0.03f;
            break;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontSerif_, titleCase(h.text), base * 1.3f, base * 0.02f);
                st.color = lerp(pale, glowC, 0.25f); st.glow = glowC; st.glowAmt = 0.7f;
            } else {
                place(fontSans_, h.text, base * 0.9f, base * 0.12f);
                st.color = h.accent ? glowC : pale; st.glow = glowC * 0.4f; st.glowAmt = 0.25f; st.weight = 0.03f;
            }
            break;
        default: // hints, help, plain
            place(fontSans_, h.text, base * 0.85f, base * 0.1f);
            st.color = h.accent ? glowC : pale * 0.85f; st.weight = 0.04f; st.shadow = 0.9f;
            break;
        }
        drawText(*font, q, st);
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void AbyssUniverse::drawWorld() {
    const float out = std::max(saturate(bridge_.amount * 1.25f), over_ * 0.85f);
    glUseProgram(progWorld_);
    set4f(progWorld_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set1f(progWorld_, "uPx", px_);
    set1f(progWorld_, "uTime", worldTime_);
    set1f(progWorld_, "uLv", lv_);
    set1f(progWorld_, "uSeed", seedF_);
    set3f(progWorld_, "uDeep", pal_.deep);
    set3f(progWorld_, "uWater", pal_.water);
    set3f(progWorld_, "uSurf", pal_.surf);
    set3f(progWorld_, "uGlow", pal_.glow);
    set3f(progWorld_, "uGlow2", pal_.glow2);
    set1f(progWorld_, "uRays", rays_);
    set1f(progWorld_, "uLights", 0.3f + 0.7f * life_);
    set1f(progWorld_, "uPlankton", smoothstepf(0.3f, 1.f, life_) * (0.5f + 0.5f * peak_));
    set1f(progWorld_, "uBloom", bloom_);
    set1f(progWorld_, "uBio", std::max(peak_ * (0.4f + 0.6f * drive_), 0.9f * surge_) * (1.f - slow_)); // the surge lights it at once
    set1f(progWorld_, "uDrive", drive_);
    set1f(progWorld_, "uKick", kickEnv_ * peak_);
    set1f(progWorld_, "uSurge", surge_);
    set1f(progWorld_, "uBeat", beatGlow_);
    set1f(progWorld_, "uFlash", flash_);
    set1f(progWorld_, "uTetris", tetrisT_);
    set3f(progWorld_, "uTetrisCol", lerp(pal_.glow, pal_.glow2, 0.3f));
    set1f(progWorld_, "uSlow", slow_);
    const float lure = smoothstepf(0.3f, 1.f, slow_) * (1.f - saturate(bridge_.amount)) * (1.f - over_);
    set4f(progWorld_, "uLure", lureX_, lureY_, lure, 1.f);
    set3f(progWorld_, "uLureCol", lerp(pal_.glow2, vec3(1.f, 0.9f, 0.7f), 0.35f));
    set1f(progWorld_, "uOut", out);
    set1f(progWorld_, "uOver", over_);
    set1f(progWorld_, "uBridge", saturate(bridge_.amount));
    set3f(progWorld_, "uBridgeCol", bridge_.color);
    set4f(progWorld_, "uWell", -WELL_X, WELL_BOT, WELL_X, WELL_TOP + 8.f);
    set4f(progWorld_, "uColL", layout::LEFT_COLUMN[0], layout::LEFT_COLUMN[1], layout::LEFT_COLUMN[2], layout::LEFT_COLUMN[3]);
    set4f(progWorld_, "uColR", layout::RIGHT_COLUMN[0], layout::RIGHT_COLUMN[1], layout::RIGHT_COLUMN[2], layout::RIGHT_COLUMN[3]);
    ugl::drawFullscreen();
}

void AbyssUniverse::drawSprites(const std::vector<Sprite>& v) {
    if (v.empty()) return;
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(progSprite_);
    setMat(progSprite_, "uVP", vp_);
    set1f(progSprite_, "uPx", px_);
    set1f(progSprite_, "uTime", worldTime_);
    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Sprite), v.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)v.size());
}

void AbyssUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const Palette target = paletteOf(variant_);
        if (!palSet_) { pal_ = target; palSet_ = true; }
        else lerpPalette(pal_, target, 1.f - std::exp(-dt * 1.4f));
    }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, slowOn_ ? 1.2f : 0.8f, dt);
    slowT_ += dt;
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.55f : 0.f), 3.f, dt);
    if (overT_ >= 0.f) {
        if (!game.over()) overT_ = -1.f;
        else overT_ += dt;
    }
    over_ = approach(over_, overT_ >= 0.f ? smoothstepf(0.3f, 3.5f, overT_) : 0.f, overT_ >= 0.f ? 2.f : 0.6f, dt);
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.35f, slow_);
    if (!f.paused) simulate(game, music, dt, wdt);
    const float world = 1.f - saturate(bridge_.amount);

    tiles_.clear();
    back_.clear();
    mid_.clear();
    front_.clear();
    fx_.clear();
    buildLife(music);
    buildWhale();
    buildBoard(game);
    buildFx();

    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    drawWorld();

    glEnable(GL_BLEND);
    // the whale, far behind
    if (!whaleMesh_.empty()) {
        const float k = whale_.t / whale_.dur;
        const float a = (0.88f * smoothstepf(0.f, 0.06f, k) * (1.f - smoothstepf(0.94f, 1.f, k))) * world * (1.f - 0.95f * slow_) * (1.f - over_);
        if (a > 0.005f) {
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glUseProgram(progWhale_);
            setMat(progWhale_, "uVP", vp_);
            set3f(progWhale_, "uBody", lerp(pal_.deep, pal_.water, 0.25f) * 0.5f);
            set3f(progWhale_, "uSpot", pal_.whale);
            set3f(progWhale_, "uVeil", pal_.water * 0.8f + pal_.surf * 0.02f);
            set1f(progWhale_, "uAlpha", a);
            set4f(progWhale_, "uColL", layout::LEFT_COLUMN[0], layout::LEFT_COLUMN[1], layout::LEFT_COLUMN[2], layout::LEFT_COLUMN[3]);
            set4f(progWhale_, "uWell", -WELL_X, WELL_BOT, WELL_X, WELL_TOP + 8.f);
            set4f(progWhale_, "uColR", layout::RIGHT_COLUMN[0], layout::RIGHT_COLUMN[1], layout::RIGHT_COLUMN[2], layout::RIGHT_COLUMN[3]);
            set1f(progWhale_, "uTime", worldTime_);
            set1f(progWhale_, "uBeat", beatGlow_);
            set1f(progWhale_, "uLen", 30.f * whale_.size);
            set1f(progWhale_, "uHalf", 30.f * whale_.size * 0.1f);
            set1f(progWhale_, "uLights", 1.f - std::max(saturate(bridge_.amount * 1.4f), 0.f));
            glBindVertexArray(whaleVao_);
            glBindBuffer(GL_ARRAY_BUFFER, whaleVbo_);
            glBufferData(GL_ARRAY_BUFFER, whaleMesh_.size() * sizeof(WVert), whaleMesh_.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)whaleMesh_.size());
        }
    }
    drawSprites(back_);
    if (!tiles_.empty()) {
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(progTile_);
        setMat(progTile_, "uVP", vp_);
        set1f(progTile_, "uPx", px_);
        set1f(progTile_, "uTime", worldTime_);
        set1f(progTile_, "uBeat", beatGlow_ * 0.6f);
        set1f(progTile_, "uSlow", slow_);
        set1f(progTile_, "uBridge", saturate(bridge_.amount));
        set3f(progTile_, "uBridgeInk", bridge_.ink);
        set3f(progTile_, "uGlow", pal_.glow);
        set3f(progTile_, "uGlow2", pal_.glow2);
        set3f(progTile_, "uDeep", pal_.deep);
        set1f(progTile_, "uWave", smoothstepf(0.f, 0.15f, tetrisT_) * (1.f - smoothstepf(0.3f, 2.f, tetrisT_)));
        glBindVertexArray(tileVao_);
        glBindBuffer(GL_ARRAY_BUFFER, tileVbo_);
        glBufferData(GL_ARRAY_BUFFER, tiles_.size() * sizeof(Tile), tiles_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)tiles_.size());
    }
    drawSprites(fx_);
    drawSprites(front_);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawHud(f.hud, f.panels);
    glDisable(GL_BLEND);

    // ---- Bloom: the living lights, blurred down and back up.
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
        set1f(progDown_, "uThreshold", i == 0 ? 0.7f : 0.f);
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

    // ---- Post.
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
    set1f(progPost_, "uBridge", saturate(bridge_.amount));
    set1f(progPost_, "uBloomAmt", 0.75f * (0.4f + 0.6f * world));
    set1f(progPost_, "uSlow", slow_ * world);
    set1f(progPost_, "uOver", over_);
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_abyss() {
    static const UniverseEntry e{
        "ABYSS", "the deep sea, lit only by living things", [] { return std::unique_ptr<Universe>(new AbyssUniverse()); },
        // Slow, dark, spacious songs suit the deep best; anything still sinks in, faster songs less often.
        [](const SongLook& l) {
            return 0.55f + 0.45f * (1.f - smoothstepf(95.f, 140.f, l.bpm)) + 0.3f * (1.f - l.brightness) +
                   (l.minor ? 0.15f : 0.f) + 0.15f * (1.f - l.density);
        },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
