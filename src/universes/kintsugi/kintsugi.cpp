// KINTSUGI: dark polished stone, ceramic pieces mended with gold.
//
// One world: a slab of dark polished stone (nero marquina, serpentine, slate, porphyry...) under one soft raking
// light, the broad reflection of a soft box lying across it. The well is a honed, darker panel set into the slab,
// framed by a hairline of metal. The pieces are glazed ceramic, each one a vessel broken and mended: its shards
// joined by fine seams of gold (the seams cross the cells, continuous over the piece and different for each one,
// never on the grid). Gold lives in the stone too: veins that grow through the marble with the song.
//
// The song: calm is the bare stone in a low, cool light, nothing moving; mid warms the light and lets a few thin
// gold veins creep into the open stone, a glint of light running over them every two beats; a chorus lights the slab
// fully and the veins spread in polished gold; a DROP melts the metal: on its first downbeat molten gold races out
// through the stone along new cracks in from the edges of the slab, flakes of gold leaf burst out of the
// cracks and settle slowly, and for the whole drop the veins stay molten (glowing, lighting the stone around them),
// the stone takes a warm grade, the seams of the pieces glow, new branches keep spreading slowly and a surge of
// light runs out along the cracks every two beats. Never a pulse of the whole scene's light: the drop is one swell
// that settles to a steady, brighter level.
//
// A lock leaves a hairline crack of molten gold at the contact, cooling in a breath; a cleared row cracks along its
// seams and its shards fall away into the dark (well under half a second, at the real speed even in slow motion);
// a Tetris sends a burst of molten veins out of the rows into the stone. Slow motion is moonlight: the light swings
// to the other side, cold and silver, the metal turns to platinum, the gold leaf hangs in the air. Game over opens
// every seam of the stack wide with gold, row by row, before it sinks into the dark. A change of universe dims the
// slab to the bridge, the stack turning to plain ink.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane, like SMOKE.
#include "universes/kintsugi/kintsugi.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/kintsugi/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Cormorant Garamond (Medium, Italic: the song, the numbers, the celebrations) and Marcellus (engraved capitals:
// the labels), SIL Open Font License, see OFL-*.txt beside this file.
const unsigned char FONT_SERIF[] = {
#include "universes/kintsugi/CormorantGaramond-Medium.ttf.inc"
};
const unsigned char FONT_ITALIC[] = {
#include "universes/kintsugi/CormorantGaramond-Italic.ttf.inc"
};
const unsigned char FONT_CAPS[] = {
#include "universes/kintsugi/Marcellus-Regular.ttf.inc"
};

namespace shaders = kintsugi_shaders;
using ugl::set1f;
using ugl::set1i;
using ugl::set2f;
using ugl::set3f;
using ugl::set4f;
using ugl::setMat;

GLuint compile(const char* vs, const char* fs, const char* name, bool ceramic = false) {
    std::string f(fs);
    std::string common = shaders::COMMON_GLSL;
    if (ceramic) common += shaders::CERAMIC_GLSL;
    f.insert(f.find('\n') + 1, common);
    return ugl::program(vs, f.c_str(), name);
}

constexpr float WELL_X = 5.25f, WELL_Y = 10.25f; // the honed panel of the well (half extents, board)

vec2 v2(float x, float y) { return vec2{x, y}; }
vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
vec2 operator*(vec2 a, float k) { return {a.x * k, a.y * k}; }
float len2(vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }
vec3 lin(unsigned c) { return ugl::toLinear(hex(c)); }

// The variants: a stone and its metal (sRGB here). The stone, its clouding and its natural veins; the raking
// light; the metal, molten metal and its white-hot front; the drop's warm grade; the HUD's ink and its dim tone;
// the seven glazes (I O T S Z J L); how much the natural veins show; the angle of the soft box's reflection.
struct VariantDef {
    const char* name;
    unsigned stone, cloud, calcite, light, metal, molten, hot, warm, ink, dim;
    unsigned piece[7];
    float veinAmt, sheenAngle;
};
const VariantDef VARIANTS[] = {
    {"NERO & GOLD", 0x0d0d0f, 0x1d1c20, 0x9a968e, 0xfff1dc, 0xdcab48, 0xffa236, 0xfff0c4, 0xffb27a, 0xece4d4, 0x8e877a,
     {0x86c5b6, 0xe6b552, 0xb07ac0, 0x9cc46a, 0xe06a4c, 0x5f86d8, 0xeea06a}, 0.10f, -0.55f},
    {"SERPENTINE & GOLD", 0x09120f, 0x15251e, 0x6f8f7c, 0xf4f2e0, 0xdfb24f, 0xff9030, 0xfff2c8, 0xffb46a, 0xe6ecdf, 0x7d8c80,
     {0xa8dccb, 0xf0c050, 0xd889a6, 0xc9dc86, 0xe5583e, 0x7393ea, 0xe08d5c}, 0.10f, -0.7f},
    {"SLATE & SILVER", 0x0f1217, 0x1e232c, 0x6e7686, 0xe8eeff, 0xc9d2de, 0x8fb8ff, 0xf2f8ff, 0x9ab8ff, 0xe4e8f0, 0x7e8594,
     {0x7fd0e0, 0xeedc78, 0xb79ae8, 0x86dcae, 0xf07a7a, 0x6a8cf0, 0xf4ac7c}, 0.08f, -0.45f},
    {"PORPHYRY & GOLD", 0x180b0d, 0x2b1417, 0x8f625c, 0xffeedd, 0xe2b252, 0xff8a2a, 0xfff0c0, 0xffa060, 0xf0e2d8, 0x957f78,
     {0x6fcfc0, 0xf2cf62, 0xb4a0ea, 0x9ed58a, 0xf59a8a, 0x78a6f0, 0xf0a050}, 0.08f, -0.6f},
    {"BASALT & COPPER", 0x110f0f, 0x221d1b, 0x5f544b, 0xffe6cc, 0xd88a5c, 0xff6a30, 0xffd8b0, 0xff9060, 0xeee0d2, 0x8f8278,
     {0x5ec8c8, 0xeadbb0, 0xa486d6, 0x8ccf7a, 0xe06058, 0x5c84d8, 0xe8b050}, 0.09f, -0.5f},
    {"SODALITE & GOLD", 0x0a0e1b, 0x141c35, 0x5d6c98, 0xf4f0ff, 0xe2b650, 0xff9a38, 0xfff4d0, 0xffb870, 0xe8e8f4, 0x7d8299,
     {0x7ad6e6, 0xf0c860, 0xd08ad8, 0x7fd6a0, 0xf07462, 0x8ea2f6, 0xf49a5c}, 0.09f, -0.65f},
    {"OBSIDIAN & PLATINUM", 0x08080a, 0x141418, 0x74747c, 0xf2f4ff, 0xe4e2dc, 0xffc890, 0xffffff, 0xffd0a0, 0xeeeeee, 0x86868c,
     {0x9fd2dc, 0xead6a0, 0xc2a8dc, 0xaed6a8, 0xe6a09a, 0x98aee0, 0xe8bc94}, 0.07f, -0.5f},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

using Palette = KintsugiUniverse::Palette;
static_assert(sizeof(Palette) % sizeof(float) == 0, "a palette is floats only");
constexpr int PAL_FLOATS = (int)(sizeof(Palette) / sizeof(float));

Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    Palette p;
    p.stone = lin(d.stone); p.cloud = lin(d.cloud); p.calcite = lin(d.calcite); p.light = lin(d.light);
    p.metal = lin(d.metal); p.molten = lin(d.molten); p.hot = lin(d.hot); p.warm = lin(d.warm);
    p.ink = lin(d.ink); p.dim = lin(d.dim);
    for (int i = 0; i < 7; i++) p.piece[i] = lin(d.piece[i]);
    p.veinAmt = d.veinAmt;
    p.sheenAngle = d.sheenAngle;
    return p;
}

// Slow motion: moonlight. A cold silver light from the other side, the metal turned to platinum, the stone bluer.
Palette moonOf(const Palette& d) {
    Palette p = d;
    auto cool = [](const vec3& c) {
        const float l = ugl::luma(c);
        return lerp(c, vec3(l * 0.8f, l * 0.95f, l * 1.35f), 0.75f);
    };
    p.stone = cool(d.stone);
    p.cloud = cool(d.cloud);
    p.calcite = cool(d.calcite);
    p.light = lin(0xaebfff);
    p.metal = lin(0xdde5f0);
    p.molten = lin(0x9fc0ff);
    p.hot = lin(0xeef4ff);
    p.warm = lin(0x8fb0ff);
    p.ink = lin(0xdfe6f4);
    p.dim = lin(0x7c8698);
    for (int i = 0; i < 7; i++) p.piece[i] = lerp(d.piece[i], cool(d.piece[i]) * 1.1f, 0.45f);
    p.sheenAngle = d.sheenAngle + 1.2f;
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

// Where no vein may run: the well and the HUD columns, from the top of the slab to its foot (the veins live in the
// open stone on both sides). Board x0 y0 x1 y1.
constexpr float KEEP_L[4] = {-12.0f, -14.f, -5.4f, 14.f}, KEEP_R[4] = {5.4f, -14.f, 12.0f, 14.f};
// The corners of the HUD on the left too: the song's title (top) and the universe and section (bottom).
constexpr float KEEP_TL[4] = {-40.f, 9.f, -5.4f, 14.f}, KEEP_BL[4] = {-40.f, -14.f, -5.4f, -8.f};
bool blocked(float x, float y) {
    auto in = [&](const float r[4]) { return x > r[0] && x < r[2] && y > r[1] && y < r[3]; };
    return in(KEEP_L) || in(KEEP_R) || in(KEEP_TL) || in(KEEP_BL) || (std::fabs(x) < WELL_X + 0.25f && std::fabs(y) < 14.f);
}

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool KintsugiUniverse::init(int width, int height) {
    progMarble_ = compile(ugl::FULLSCREEN_VS, shaders::MARBLE_FS, "kintsugi marble");
    progStone_ = compile(ugl::FULLSCREEN_VS, shaders::STONE_FS, "kintsugi stone");
    progVein_ = compile(shaders::VEIN_VS, shaders::VEIN_FS, "kintsugi vein");
    progBlock_ = compile(shaders::BLOCK_VS, shaders::BLOCK_FS, "kintsugi block", true);
    progShard_ = compile(shaders::SHARD_VS, shaders::SHARD_FS, "kintsugi shard", true);
    progSprite_ = compile(shaders::SPRITE_VS, shaders::SPRITE_FS, "kintsugi sprite");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "kintsugi text");
    progDown_ = compile(ugl::FULLSCREEN_VS, shaders::DOWN_FS, "kintsugi down");
    progUp_ = compile(ugl::FULLSCREEN_VS, shaders::UP_FS, "kintsugi up");
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "kintsugi post");
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "kintsugi shape");

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
    instanced(blockVao_, blockVbo_, 4, sizeof(Block));
    instanced(spriteVao_, spriteVbo_, 3, sizeof(Sprite));
    auto plain = [&](GLuint& vao, GLuint& vbo, int vec4s) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        for (int i = 0; i < vec4s; i++) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 4 * vec4s, (void*)(sizeof(float) * 4 * i));
        }
    };
    plain(veinVao_, veinVbo_, 3);
    plain(shardVao_, shardVbo_, 3);

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

    buildFont(fontSerif_, FONT_SERIF);
    buildFont(fontItalic_, FONT_ITALIC);
    buildFont(fontCaps_, FONT_CAPS);
    pal_ = cur_ = paletteOf(0);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// Glyphs baked as signed distance fields: they scale cleanly, and the distance gives the halo.
void KintsugiUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] kintsugi font init failed\n");
        delete info;
        return;
    }
    f.info = info;
    f.px = 64.f;
    f.scaleTT = stbtt_ScaleForPixelHeight(info, f.px);
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

void KintsugiUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    const float aspect = (float)width_ / height_;
    const layout::Camera cam = layout::camera(width_, height_);
    const float hh = cam.dist * std::tan(cam.fovDeg * 0.5f * PI / 180.f), hw = hh * aspect;
    viewH_ = v2(hw, hh);
    viewC_ = v2(cam.target.x, cam.target.y);
    vp_ = mat4::ortho(viewC_.x - hw, viewC_.x + hw, viewC_.y - hh, viewC_.y + hh, -1.f, 1.f);
    px_ = 2.f * hh / height_;
    marbleT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    sceneT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    int w = width_, h = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        bloomT_[i].create(w, h, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    marbleDirty_ = true;
}

KintsugiUniverse::~KintsugiUniverse() {
    marbleT_.destroy();
    sceneT_.destroy();
    for (auto& t : bloomT_) t.destroy();
    for (GLuint p : {progMarble_, progStone_, progVein_, progBlock_, progShard_, progSprite_, progText_, progDown_, progUp_,
                     progPost_, progShape_})
        glDeleteProgram(p);
    for (GLuint v : {blockVao_, spriteVao_, veinVao_, shardVao_, textVao_, shapeVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, blockVbo_, spriteVbo_, veinVbo_, shardVbo_, textVbo_, shapeVbo_}) glDeleteBuffers(1, &b);
    for (Font* f : {&fontSerif_, &fontItalic_, &fontCaps_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

void KintsugiUniverse::bakeMarble() {
    marbleT_.bind();
    glDisable(GL_BLEND);
    glUseProgram(progMarble_);
    set4f(progMarble_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set1f(progMarble_, "uSeed", seedF_);
    ugl::drawFullscreen();
    marbleDirty_ = false;
}

// =====================================================================================================
// Song, level, drop
// =====================================================================================================

// The stone from the song: dark minor songs get the black stones, warm bright songs the porphyry and the copper,
// cool ones the slate and the sodalite; the key's hue picks among them.
int KintsugiUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    int v;
    if (t.hasSong && t.minor && t.brightness < 0.42f) v = h < 0.5f ? 0 : 6;
    else v = std::min(VARIANT_COUNT - 1, (int)(h * VARIANT_COUNT));
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void KintsugiUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed ? look.seed : 1;
    seedF_ = (float)(seed_ % 1000) * 0.173f;
    rng_ = Rng(seed_ ^ 0x6b17ull);
    marbleDirty_ = true;
    if (seconds <= 0.f) palSet_ = false;
    // the new song's veins grow again
    fadeNets(0, 2.f);
    fadeNets(1, 2.f);
    if (level_ >= 1) {
        nets_.emplace_back();
        growNet(nets_.back(), 0, 1.4f, seed_ ^ (uint64_t)(++netSerial_ * 977));
    }
}

void KintsugiUniverse::fadeNets(int kind, float dur) {
    for (Net& n : nets_)
        if (n.kind == kind && n.fadeAt > now_) {
            n.fadeAt = now_;
            n.fadeDur = dur;
            n.heatTo = 0.f;
        }
}

bool KintsugiUniverse::hasNet(int kind) const {
    for (const Net& n : nets_)
        if (n.kind == kind && n.fadeAt > now_) return true;
    return false;
}

void KintsugiUniverse::setLevel(int level, float energy, float seconds, float impact) {
    level = std::clamp(level, 0, 2);
    energy_ = energy;
    const int old = level_;
    level_ = level;
    if (!hasLevel_ || seconds < 0.1f) {
        hasLevel_ = true;
        lv_ = lvFrom_ = lvTo_ = (float)level;
        lvT_ = 1.f;
        // a still frame (the viewer, the first frame): the veins of the level already grown
        nets_.clear();
        if (level >= 1) {
            nets_.emplace_back();
            growNet(nets_.back(), 0, 1.4f, seed_ ^ 0x11ull);
            nets_.back().born = now_ - 30.f;
        }
        if (level >= 2) {
            nets_.emplace_back();
            growNet(nets_.back(), 1, 30.f, seed_ ^ 0x22ull);
            nets_.back().born = now_ - 14.f;
        }
        dropPending_ = false;
        return;
    }
    lvFrom_ = lv_;
    lvTo_ = (float)level;
    lvT_ = 0.f;
    const bool rising = level > old;
    lvDur_ = std::clamp(seconds, rising ? 1.0f : 2.5f, 6.f);
    if (level >= 1 && !hasNet(0)) {
        nets_.emplace_back();
        growNet(nets_.back(), 0, 1.4f, seed_ ^ (uint64_t)(++netSerial_ * 977));
    }
    if (level < 2) {
        fadeNets(1, level == 1 ? 3.5f : 5.f);
        dropPending_ = false;
    }
    if (level == 0) fadeNets(0, 6.f);
    if (rising && level == 2) {
        // into a peak: the gold waits for the drop's first downbeat (drop()); without one (a chorus reached
        // without a downbeat, a seek), it grows anyway after a moment
        dropPending_ = true;
        dropWait_ = 0.f;
        (void)impact;
    }
}

// The drop's first downbeat: molten gold races out through the stone along new cracks, gold leaf bursts out of
// them; the drop look (molten veins, a warm stone, glowing seams) holds for the whole drop.
void KintsugiUniverse::drop(float strength) {
    if (level_ < 2) return;
    const float s = saturate(strength);
    dropPending_ = false;
    sinceDrop_ = 0.f;
    swell_ = std::max(swell_, 0.6f * s + 0.2f);
    fadeNets(1, 0.8f);
    nets_.emplace_back();
    Net& n = nets_.back();
    growNet(n, 1, 34.f, seed_ ^ (uint64_t)(++netSerial_ * 7919));
    n.heat = 1.f;
    n.heatTo = 1.f;
    // the gold leaf bursts out of the cracks as the gold races through them
    int count = 0;
    for (const Vein& v : n.veins) {
        if (v.pts.size() < 4 || v.pts[0].w < 0.05f) continue; // the trunks only
        for (size_t i = 2; i < v.pts.size() && count < 90; i += 3) {
            const VPoint& p = v.pts[i];
            if (p.t > 1.2f) break;
            Leaf l;
            l.p = v2(p.x, p.y);
            const float a = rng_.range(0.f, TAU);
            const float sp = rng_.range(3.f, 11.f) * s;
            l.v = v2(std::cos(a) * sp, std::sin(a) * sp + 2.f);
            l.rot = rng_.range(0.f, TAU);
            l.spin = rng_.range(-6.f, 6.f);
            l.turn = rng_.range(0.f, TAU);
            l.turnRate = rng_.range(2.f, 7.f);
            l.size = rng_.range(0.06f, 0.15f);
            l.t = -p.t;
            l.life = rng_.range(5.f, 11.f);
            l.seed = rng_.uniform();
            leaves_.push_back(l);
            count++;
        }
    }
}

void KintsugiUniverse::refresh(float strength) {
    // a new section in the same level: a glint runs over the veins
    surgeT_ = now_;
    surgeAmp_ = std::max(surgeAmp_, 0.5f * strength);
}

void KintsugiUniverse::levelUp() { levelGlint_ = 1.f; }

vec2 KintsugiUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints KintsugiUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    h.background = lerp(p.stone, p.cloud, 0.4f);
    h.light = false;
    h.ink = lerp(p.ink, p.metal, 0.35f) * 0.8f;
    h.accent = p.metal;
    h.styles = TS_INK | TS_LUMA | TS_REBUILD; // gold spreading, darks first, the vessel rebuilt
    return h;
}

// =====================================================================================================
// The veins
// =====================================================================================================

// A crack running through the stone from (x, y): angular, jittered, braking around the HUD columns and the well;
// branches leave it later and spread slowly.
void KintsugiUniverse::walk(Net& n, Rng& r, float x, float y, float ang, float len, float w, float t0, float speed,
                            float dist0, int depth, float branchRate, float branchDelay, float branchSpeed) {
    Vein v;
    v.seed = r.uniform();
    const float step = 0.3f;
    float s = 0.f, a = ang;
    v.pts.push_back({x, y, t0, dist0, w});
    struct Pending { float x, y, ang, t, dist, w; };
    std::vector<Pending> kids;
    const float limX = viewH_.x + 1.f, limY = viewH_.y + 1.f;
    while (s < len) {
        // an angular crack: small jitter, now and then a kink, drawn back toward its course
        a += (r.uniform() - 0.5f) * 0.22f + (a - ang) * -0.05f;
        if (r.chance(0.06f)) a += (r.chance(0.5f) ? 1.f : -1.f) * r.range(0.3f, 0.6f);
        float nx = x + std::cos(a) * step, ny = y + std::sin(a) * step;
        if (blocked(nx, ny)) {
            // the crack meets the HUD's quiet zone: it turns away once if it can, else it ends there
            const float b = a + (r.chance(0.5f) ? 1.f : -1.f) * r.range(0.6f, 1.0f);
            const float tx = x + std::cos(b) * step, ty = y + std::sin(b) * step;
            if (blocked(tx, ty) || s > len * 0.7f) break;
            a = b; ang = b; nx = tx; ny = ty;
        }
        x = nx; y = ny; s += step;
        if (std::fabs(x - viewC_.x) > limX || std::fabs(y - viewC_.y) > limY) break;
        const float k = s / len;
        v.pts.push_back({x, y, t0 + s / speed, dist0 + s, w * (1.f - 0.75f * k * k)});
        if (depth < 1 && r.chance(branchRate) && s > 0.8f) {
            const float side = r.chance(0.5f) ? 1.f : -1.f;
            kids.push_back({x, y, a + side * r.range(0.45f, 1.0f), t0 + s / speed + branchDelay * r.range(0.15f, 1.f),
                            dist0 + s, w * r.range(0.45f, 0.65f)});
        }
    }
    if (v.pts.size() >= 2) n.veins.push_back(std::move(v));
    for (const Pending& p : kids)
        walk(n, r, p.x, p.y, p.ang, len * r.range(0.2f, 0.45f), p.w, p.t, branchSpeed * r.range(0.6f, 1.3f), p.dist,
             depth + 1, branchRate * 0.6f, branchDelay, branchSpeed);
}

// kind 0: a few thin veins creeping into the stone (mid); kind 1: the peak's network, trunks racing in from the edges
// to the edges of the slab (speed: how fast), branches spreading for many seconds after; kind 2: a Tetris burst.
void KintsugiUniverse::growNet(Net& n, int kind, float speed, uint64_t seed) {
    Rng r(seed);
    n.kind = kind;
    n.born = now_;
    n.veins.clear();
    const float W = viewH_.x, H = viewH_.y;
    const float inner = KEEP_R[2] + 0.4f;           // the open stone: from here to the edge of the slab
    // a crack entering the slab from its outer edge (y: where), from its foot or its top (x: how far out)
    auto fromEdge = [&](float sgn, float y, float len, float w, float t0, int depth, float br, float bd, float bs) {
        // diagonally inward, downward from high up, upward from low down
        const float mag = r.range(0.25f, 0.7f);
        const float t = y > H * 0.2f ? -mag : (y < -H * 0.2f ? mag : (r.chance(0.5f) ? mag : -mag));
        walk(n, r, sgn * (W + 0.5f), y, sgn > 0 ? PI - t : t, len, w, t0, speed, 0.f, depth, br, bd, bs);
    };
    auto fromFoot = [&](float sgn, bool top, float len, float w, float t0, int depth, float br, float bd, float bs) {
        const float x = sgn * r.range(inner + 0.5f, std::max(inner + 1.f, W - 1.f));
        // diagonally, like the veins of marble (never straight up: that would read as lightning)
        const float e = r.range(0.8f, 1.2f);
        const float a = top ? (sgn > 0 ? -e : -(PI - e)) : (sgn > 0 ? e : PI - e);
        walk(n, r, x, top ? H + 0.5f : -H - 0.5f, a, len, w, t0, speed, 0.f, depth, br, bd, bs);
    };
    if (kind == 0) {
        // mid: two thin veins creeping into the stone, one on each side
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            if (r.chance(0.5f)) fromEdge((float)sgn, r.range(-H * 0.7f, H * 0.7f), r.range(6.f, 10.f), r.range(0.04f, 0.055f),
                                         r.range(0.f, 3.f), 1, 0.02f, 3.f, speed * 0.8f);
            else fromFoot((float)sgn, r.chance(0.3f), r.range(6.f, 10.f), r.range(0.04f, 0.055f), r.range(0.f, 3.f), 1, 0.02f, 3.f,
                          speed * 0.8f);
        }
        return;
    }
    if (kind == 1) {
        // the peak: on each side a crack runs in from the edge of the slab and one rises from its foot (or falls from its
        // top); branches leave them for many seconds after (up to 7 s after the gold passed, crawling at ~1.6 units/s)
        const float bd = 7.f, bs = 1.6f;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            fromEdge((float)sgn, r.range(-H * 0.6f, H * 0.6f), r.range(14.f, 22.f), r.range(0.065f, 0.085f), r.range(0.f, 0.1f), 0,
                     0.035f, bd, bs);
            fromFoot((float)sgn, r.chance(0.35f), r.range(12.f, 20.f), r.range(0.055f, 0.075f), r.range(0.1f, 0.3f), 0, 0.035f, bd, bs);
        }
        return;
    }
    if (kind == 2) {
        // a Tetris: four cracks burst in from the edges of the slab, molten, and cool off
        for (int i = 0; i < 4; i++) {
            const float sgn = i % 2 == 0 ? -1.f : 1.f;
            fromEdge(sgn, r.range(-H * 0.8f, H * 0.5f), r.range(10.f, 16.f), r.range(0.06f, 0.08f), r.range(0.f, 0.15f), 1, 0.08f,
                     0.6f, 6.f);
        }
    }
}

// The veins as line strips (two triangles per segment), with what VEIN_FS needs per vertex.
void KintsugiUniverse::buildVeins(bool locks) {
    veinVerts_.clear();
    for (const Net& n : nets_) {
        if ((n.kind == 3) != locks) continue;
        const float fade = n.fadeAt >= 1e8f ? 0.f : smoothstepf(n.fadeAt, n.fadeAt + n.fadeDur, now_);
        if (fade >= 1.f) continue;
        for (const Vein& v : n.veins) {
            const size_t N = v.pts.size();
            std::vector<vec2> nrm(N);
            for (size_t i = 0; i < N; i++) {
                const VPoint& a = v.pts[i > 0 ? i - 1 : i];
                const VPoint& b = v.pts[i + 1 < N ? i + 1 : i];
                vec2 d = v2(b.x - a.x, b.y - a.y);
                const float l = std::max(1e-4f, len2(d));
                nrm[i] = v2(-d.y / l, d.x / l);
            }
            auto vert = [&](size_t i, float side) {
                const VPoint& p = v.pts[i];
                const float H = p.w * 0.5f + 0.32f + 2.f * px_;
                const float f[12] = {p.x + nrm[i].x * H * side, p.y + nrm[i].y * H * side, side, n.born + p.t,
                                     H, p.w, p.dist, v.alpha,
                                     n.heat, v.seed, n.born, fade};
                veinVerts_.insert(veinVerts_.end(), f, f + 12);
            };
            for (size_t i = 0; i + 1 < N; i++) {
                vert(i, -1.f); vert(i, 1.f); vert(i + 1, 1.f);
                vert(i, -1.f); vert(i + 1, 1.f); vert(i + 1, -1.f);
            }
        }
    }
}

// =====================================================================================================
// Game events
// =====================================================================================================

void KintsugiUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::Lock: {
        if (ev.cells.empty()) break;
        // the piece's cells belong together (one vessel), with its own pattern of seams
        const int id = nextId_++;
        const float sx = rng_.range(0.f, 50.f), sy = rng_.range(0.f, 50.f);
        int bottom = -1;
        for (auto& c : ev.cells) bottom = std::max(bottom, c.y);
        for (auto& c : ev.cells)
            if (c.y >= 0 && c.y < Game::H && c.x >= 0 && c.x < Game::W) own_[c.y][c.x] = {c.type, id, sx, sy, bottom};
        // a hairline crack of molten gold at the contact, under the piece
        float x0 = 1e9f, x1 = -1e9f;
        for (auto& c : ev.cells)
            if (c.y == bottom) { x0 = std::min(x0, (float)c.x); x1 = std::max(x1, (float)c.x); }
        const vec3 a = layout::cellPos(x0, (float)bottom), b = layout::cellPos(x1, (float)bottom);
        const float yb = a.y - 0.5f;
        const float hard = lastDrop_ >= 2 ? 1.f : 0.f;
        Net n;
        n.kind = 3;
        n.born = now_;
        n.heat = n.heatTo = 1.f;
        n.fadeAt = now_ + 0.25f + 0.2f * hard;
        n.fadeDur = 0.5f;
        Rng r(rng_.next());
        const float cx = 0.5f * (a.x + b.x), half = 0.5f * (b.x - a.x) + 0.35f + 0.25f * hard;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            Vein v;
            v.seed = r.uniform();
            float x = cx, y = yb, d = 0.f;
            v.pts.push_back({x, y, 0.f, 0.f, 0.03f});
            while (std::fabs(x - cx) < half) {
                x += sgn * 0.12f;
                y = yb + r.range(-0.03f, 0.03f);
                d += 0.12f;
                v.pts.push_back({x, y, d / 7.f, d, 0.03f * (1.f - 0.6f * std::fabs(x - cx) / half)});
            }
            n.veins.push_back(std::move(v));
        }
        nets_.push_back(std::move(n));
        lastDrop_ = 0;
        break;
    }
    case GameEvent::HardDrop:
        lastDrop_ = ev.count;
        break;
    case GameEvent::Clear: {
        const bool tetris = ev.count >= 4;
        bool cleared[Game::H] = {};
        // the row cracks along its seams; its shards fall away into the dark
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H) continue;
            cleared[c.y] = true;
            const Owner& o = own_[c.y][c.x];
            const bool known = o.type == c.type && o.id >= 0;
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            const float sx = known ? o.sx : 3.7f, sy = known ? o.sy : 1.3f;
            const float G = 0.035f;
            const vec2 cn[4] = {v2(-0.5f + G, -0.5f + G), v2(0.5f - G, -0.5f + G), v2(0.5f - G, 0.5f - G), v2(-0.5f + G, 0.5f - G)};
            const vec2 ctr = v2(rng_.range(-0.22f, 0.22f), rng_.range(-0.22f, 0.22f));
            for (int i = 0; i < 4; i++) {
                const vec2 A = cn[i], B = cn[(i + 1) % 4];
                Shard s;
                s.ax = ctr.x; s.ay = ctr.y; s.bx = A.x; s.by = A.y; s.cx = B.x; s.cy = B.y;
                s.ox = p.x; s.oy = p.y;
                s.sx = c.x + sx; s.sy = -(float)c.y + sy;
                const vec2 g = v2((ctr.x + A.x + B.x) / 3.f, (ctr.y + A.y + B.y) / 3.f);
                const vec2 out = g - ctr;
                const float ol = std::max(1e-3f, len2(out));
                const float sp = rng_.range(0.8f, 2.f) * (tetris ? 1.5f : 1.f);
                s.v = v2(out.x / ol * sp * 0.45f, out.y / ol * sp * 0.4f - rng_.range(0.8f, 2.2f));
                s.rot = 0.f;
                s.spin = rng_.range(-2.5f, 2.5f);
                s.t = 0.f;
                s.delay = std::fabs(c.x - 4.5f) * 0.012f;
                s.life = 0.4f;
                s.type = std::clamp(c.type, 0, 6);
                s.edgeA = 0.f; s.edgeB = 0.f;
                shards_.push_back(s);
            }
        }
        // collapse the bookkeeping like the board
        Owner next[Game::H][Game::W];
        int dst = Game::H - 1;
        for (int y = Game::H - 1; y >= 0; y--) {
            if (cleared[y]) continue;
            for (int x = 0; x < Game::W; x++) {
                next[dst][x] = own_[y][x];
                next[dst][x].base += dst - y;
            }
            dst--;
        }
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) own_[y][x] = next[y][x];
        if (tetris) {
            nets_.emplace_back();
            Net& n = nets_.back();
            growNet(n, 2, 26.f, rng_.next());
            n.heat = n.heatTo = 1.f;
            n.fadeAt = now_ + 2.5f;
            n.fadeDur = 2.5f;
            for (int i = 0; i < 30; i++) {
                Leaf l;
                const float sgn = i % 2 == 0 ? -1.f : 1.f;
                l.p = v2(sgn * (WELL_X + 0.4f), rng_.range(-9.5f, -6.f));
                const float a = sgn > 0 ? rng_.range(-0.9f, 0.9f) : PI + rng_.range(-0.9f, 0.9f);
                const float sp = rng_.range(4.f, 10.f);
                l.v = v2(std::cos(a) * sp, std::sin(a) * sp + 2.f);
                l.rot = rng_.range(0.f, TAU); l.spin = rng_.range(-6.f, 6.f);
                l.turn = rng_.range(0.f, TAU); l.turnRate = rng_.range(2.f, 7.f);
                l.size = rng_.range(0.06f, 0.14f);
                l.t = -rng_.range(0.f, 0.3f);
                l.life = rng_.range(3.f, 6.f);
                l.seed = rng_.uniform();
                leaves_.push_back(l);
            }
            swell_ = std::max(swell_, 0.4f);
        }
        break;
    }
    case GameEvent::TopOut: {
        // game over: every seam of the stack opens wide with gold, row by row from the top, then it sinks into the dark
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        auto ownerAt = [&](int x, int y) -> const Owner* {
            if (x < 0 || x >= Game::W || y < 0 || y >= Game::H) return nullptr;
            return &own_[y][x];
        };
        for (auto& c : ev.cells) {
            if (c.y < Game::HIDDEN || c.y >= Game::H) continue; // (the hidden rows above the well stay hidden)
            const Owner& o = own_[c.y][c.x];
            const bool known = o.type == c.type && o.id >= 0;
            int mask = 0;
            if (known) {
                for (auto& q : ev.cells) {
                    const Owner* oo = ownerAt(q.x, q.y);
                    if (!oo || oo->id != o.id) continue;
                    if (q.y == c.y && q.x == c.x - 1) mask |= 1;
                    if (q.y == c.y && q.x == c.x + 1) mask |= 2;
                    if (q.x == c.x && q.y == c.y + 1) mask |= 4;
                    if (q.x == c.x && q.y == c.y - 1) mask |= 8;
                }
            }
            Dying d;
            d.x = c.x; d.y = c.y; d.type = std::clamp(c.type, 0, 6);
            d.sx = known ? o.sx : 3.7f; d.sy = known ? o.sy : 1.3f;
            d.t = 0.f;
            d.delay = 0.2f + (c.y - top) * 0.09f + std::fabs(c.x - 4.5f) * 0.02f;
            d.mask = mask;
            dying_.push_back(d);
        }
        for (auto& row : own_)
            for (auto& c : row) c = Owner{};
        overT_ = 0.f;
        break;
    }
    default: break;
    }
}

// =====================================================================================================
// Simulation
// =====================================================================================================

void KintsugiUniverse::spawnLeaves(int n, vec2 at, float speed, float spread) {
    for (int i = 0; i < n; i++) {
        Leaf l;
        l.p = at + v2(rng_.range(-spread, spread), rng_.range(-spread, spread));
        const float a = rng_.range(0.f, TAU);
        l.v = v2(std::cos(a) * speed, std::sin(a) * speed);
        l.rot = rng_.range(0.f, TAU); l.spin = rng_.range(-1.5f, 1.5f);
        l.turn = rng_.range(0.f, TAU); l.turnRate = rng_.range(1.f, 3.f);
        l.size = rng_.range(0.05f, 0.12f);
        l.t = 0.f;
        l.life = rng_.range(7.f, 12.f);
        l.seed = rng_.uniform();
        leaves_.push_back(l);
    }
}

void KintsugiUniverse::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    now_ += wdt;
    sinceDrop_ += wdt;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    // a peak without its downbeat (a chorus reached by a seek, no beat grid): the gold grows anyway
    if (dropPending_) {
        dropWait_ += dt;
        if (dropWait_ > 1.5f) {
            dropPending_ = false;
            nets_.emplace_back();
            growNet(nets_.back(), 1, 9.f, seed_ ^ (uint64_t)(++netSerial_ * 31));
        }
    }
    // The drop look: a peak whose section runs hot (a drop, not a chorus: the song's speed profile tells them apart),
    // held for the whole drop, eased in and out over a second or two.
    const bool peak = level_ >= 2;
    const bool hot = peak && (music.speed > 1.33f || !music.hasSong) && sinceDrop_ < 1e4f;
    dropTarget_ = peak && (hot || sinceDrop_ < 3.f) ? 1.f : 0.f;
    if (peak && music.hasSong && music.speed < 1.3f && sinceDrop_ > 3.f) dropTarget_ = 0.f; // a chorus: polished gold
    dropness_ = approach(dropness_, dropTarget_, dropTarget_ > dropness_ ? 2.5f : 0.6f, dt);
    swell_ = approach(swell_, 0.f, 0.35f, dt);
    for (Net& n : nets_) {
        if (n.kind == 1) n.heatTo = n.fadeAt > now_ ? dropness_ : 0.f;
        if (n.kind == 0) n.heatTo = 0.25f * dropness_;
        n.heat = approach(n.heat, n.heatTo, n.kind >= 2 ? 0.8f : 1.2f, wdt);
    }
    nets_.erase(std::remove_if(nets_.begin(), nets_.end(), [&](const Net& n) { return now_ > n.fadeAt + n.fadeDur; }), nets_.end());

    // The beat grid, at half time: glints on the veins (mid, chorus), a surge of light along them (the drop).
    if (music.hasSong) {
        const float half = (float)std::floor(music.beat / 2.0);
        if (lastHalf_ >= 0.f && half != lastHalf_ && level_ >= 1 && !slowOn_) {
            if (dropness_ > 0.5f) {
                surgeT_ = now_;
                surgeAmp_ = 0.9f * dropness_;
            }
            // a few glints where the gold is
            const int want = level_ >= 2 ? 3 : 2;
            for (int k = 0; k < want; k++) {
                const Net* best = nullptr;
                int tries = 0;
                while (!best && tries++ < 6 && !nets_.empty()) {
                    const Net& n = nets_[rng_.irange(0, (int)nets_.size() - 1)];
                    if (n.kind <= 1 && n.fadeAt > now_ && !n.veins.empty()) best = &n;
                }
                if (!best) break;
                const Vein& v = best->veins[rng_.irange(0, (int)best->veins.size() - 1)];
                const VPoint& p = v.pts[rng_.irange(0, (int)v.pts.size() - 1)];
                if (best->born + p.t > now_) continue;
                glints_.push_back({v2(p.x, p.y), -rng_.range(0.f, 0.25f), 0.9f, rng_.range(0.25f, 0.45f)});
            }
        }
        lastHalf_ = half;
    }
    surgeAmp_ = approach(surgeAmp_, 0.f, 0.6f, wdt);

    // the gold leaf of the drop: a few flakes keep coming down, slowly, for the whole drop
    if (dropness_ > 0.6f && !slowOn_ && leaves_.size() < 60 && rng_.chance(wdt * 3.f)) {
        Leaf l;
        l.p = v2(rng_.range(KEEP_R[2] + 0.3f, std::max(KEEP_R[2] + 0.5f, viewH_.x)), viewH_.y + 0.5f);
        if (rng_.chance(0.5f)) l.p.y = rng_.range(KEEP_BL[3] + 1.f, KEEP_TL[1] - 1.f), l.p.x = -l.p.x; // left: below the title
        l.v = v2(rng_.range(-0.3f, 0.3f), -rng_.range(0.4f, 0.9f));
        l.rot = rng_.range(0.f, TAU); l.spin = rng_.range(-1.2f, 1.2f);
        l.turn = rng_.range(0.f, TAU); l.turnRate = rng_.range(1.f, 3.f);
        l.size = rng_.range(0.05f, 0.11f);
        l.t = 0.f; l.life = 30.f; l.seed = rng_.uniform();
        leaves_.push_back(l);
    }
    for (Leaf& l : leaves_) {
        l.t += wdt;
        if (l.t < 0.f) continue;
        // fast out of the cracks, braked by the air, then drifting down, swaying
        l.v = l.v * std::exp(-2.2f * wdt);
        const vec2 settle = v2(0.25f * std::sin(l.t * 0.9f + l.seed * 20.f), -0.55f);
        l.v = l.v + (settle - l.v) * (1.f - std::exp(-0.6f * wdt));
        l.p = l.p + l.v * wdt;
        l.rot += l.spin * wdt;
        l.turn += l.turnRate * wdt;
        l.spin *= std::exp(-0.5f * wdt);
        l.turnRate = std::max(1.f, l.turnRate * std::exp(-0.4f * wdt));
    }
    const float bottom = viewC_.y - viewH_.y - 1.f;
    leaves_.erase(std::remove_if(leaves_.begin(), leaves_.end(), [&](const Leaf& l) { return l.t > l.life || l.p.y < bottom; }), leaves_.end());
    for (Glint& g : glints_) g.t += wdt;
    glints_.erase(std::remove_if(glints_.begin(), glints_.end(), [](const Glint& g) { return g.t > g.life; }), glints_.end());
    // a cleared row runs at the real speed, even in slow motion
    for (Shard& s : shards_) {
        s.t += dt;
        if (s.t < s.delay) continue;
        s.v.y -= 16.f * dt;
        s.ox += s.v.x * dt;
        s.oy += s.v.y * dt;
        s.rot += s.spin * dt;
    }
    shards_.erase(std::remove_if(shards_.begin(), shards_.end(), [](const Shard& s) { return s.t > s.delay + s.life; }), shards_.end());
    for (Dying& d : dying_) d.t += dt;
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return d.t > d.delay + 6.f; }), dying_.end());
    levelGlint_ = approach(levelGlint_, 0.f, 0.8f, dt);
    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
}

// =====================================================================================================
// Geometry
// =====================================================================================================

void KintsugiUniverse::buildBoard(const Game& g) {
    blocks_.clear();
    shardVerts_.clear();
    const float seamHeat = 0.85f * dropness_;
    auto block = [&](float x, float y, float s, const vec3& c, float alpha, int kind, int mask, float px, float py, float age,
                     float extra, float heat) {
        blocks_.push_back({{x, y, 0.5f * s, s}, {c.x, c.y, c.z, alpha}, {(float)kind, (float)mask, 0.f, heat}, {px, py, age, extra}});
    };
    auto valid = [](int x, int y) { return x >= 0 && x < Game::W && y >= Game::HIDDEN && y < Game::H; };
    // the stack: the cells of one piece join into one vessel
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const Owner& o = own_[y][x];
            const bool known = o.type == c.type && o.id >= 0;
            auto joined = [&](int nx, int ny) {
                if (!known || !valid(nx, ny)) return false;
                const Cell& oc = g.cell(nx, ny);
                const Owner& oo = own_[ny][nx];
                if (oc.type != c.type || oo.type != oc.type || oo.id != o.id) return false;
                return std::fabs(g.rowOffset(ny) - g.rowOffset(y)) < 0.01f;
            };
            int mask = 0;
            if (joined(x - 1, y)) mask |= 1;
            if (joined(x + 1, y)) mask |= 2;
            if (joined(x, y + 1)) mask |= 4;
            if (joined(x, y - 1)) mask |= 8;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            const float age = c.flash > 0.f ? (1.f - c.flash) * Game::LOCK_FX_SECONDS : -1.f;
            const float sx = known ? o.sx : 3.7f + x * 0.31f, sy = known ? o.sy : 1.3f + y * 0.17f;
            block(p.x, p.y, 1.f, cur_.piece[c.type], 1.f, 0, mask, x + sx, -(float)y + sy, age, known ? (float)(o.base - y) : 0.f,
                  seamHeat);
        }
    // game over: the seams open with gold, row by row, then the stack sinks into the dark
    for (const Dying& d : dying_) {
        const float t = d.t - d.delay;
        const float open = smoothstepf(0.f, 0.9f, t);
        const float dark = 1.f - 0.85f * smoothstepf(1.6f, 4.5f, t);
        const vec3 p = layout::cellPos((float)d.x, (float)d.y);
        block(p.x, p.y, 1.f, cur_.piece[d.type] * dark, 1.f - smoothstepf(4.f, 5.8f, t), 2, d.mask, d.x + d.sx, -(float)d.y + d.sy,
              -1.f, open, 0.6f * open * (1.f - smoothstepf(1.f, 3.5f, t)));
    }
    // the falling piece and its ghost
    auto pieceMask = [](int cells[4][2], int i) {
        int m = 0;
        for (int j = 0; j < 4; j++) {
            if (j == i) continue;
            const int dx = cells[j][0] - cells[i][0], dy = cells[j][1] - cells[i][1];
            if (dx == -1 && dy == 0) m |= 1;
            if (dx == 1 && dy == 0) m |= 2;
            if (dx == 0 && dy == 1) m |= 4;
            if (dx == 0 && dy == -1) m |= 8;
        }
        return m;
    };
    if (g.hasPiece()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        const vec3 col = cur_.piece[p.type];
        const float sx = (float)(nextId_ % 37) * 1.37f, sy = (float)(nextId_ % 23) * 2.11f; // the seams it will keep
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (int i = 0; i < 4; i++) {
                if (gc[i][1] < Game::HIDDEN) continue;
                const vec3 w = layout::cellPos((float)gc[i][0], (float)gc[i][1]);
                block(w.x, w.y, 1.f, col, 0.5f, 1, pieceMask(gc, i), 0.f, 0.f, -1.f, 0.f, 0.f);
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (int i = 0; i < 4; i++) {
            if (cells[i][1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(cells[i][0] + dx, cells[i][1] + dy);
            block(w.x, w.y, 1.f, col, 1.f, 0, pieceMask(cells, i), cells[i][0] - p.x + sx, -(float)(cells[i][1] - p.y) + sy, -1.f,
                  0.f, seamHeat);
        }
    }
    // previews
    auto preview = [&](int type, vec2 center, float sc, float alpha, int salt) {
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
            const vec2 pos = v2(center.x + (cells[i][0] - (minx + maxx) * 0.5f) * sc, center.y - (cells[i][1] - (miny + maxy) * 0.5f) * sc);
            block(pos.x, pos.y, sc, cur_.piece[type], alpha, 0, pieceMask(cells, i), (float)cells[i][0] + salt * 5.3f,
                  -(float)cells[i][1] + salt * 3.1f, -1.f, 0.f, seamHeat);
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.4f : 1.f, 1);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f, 2 + i);
    }

    // the shards of the cleared rows: falling away into the dark (shrinking, darkening), their broken edges gold
    for (const Shard& s : shards_) {
        const float t = s.t - s.delay;
        const float k = saturate(t / s.life);
        const float scale = 1.f - 0.45f * k;
        const float alpha = t < 0.f ? 1.f : 1.f - smoothstepf(0.45f, 1.f, k);
        const float shade = 1.f - 0.45f * k;
        const vec3 c = cur_.piece[s.type] * shade;
        const float cs = std::cos(s.rot), sn = std::sin(s.rot);
        const vec2 ctr = v2((s.ax + s.bx + s.cx) / 3.f, (s.ay + s.by + s.cy) / 3.f);
        auto emit = [&](float lx, float ly, float ba, float bb) {
            const float rx = (lx - ctr.x) * scale, ry = (ly - ctr.y) * scale;
            const float wx = s.ox + ctr.x + cs * rx - sn * ry, wy = s.oy + ctr.y + sn * rx + cs * ry;
            const float f[12] = {wx, wy, s.sx + lx, s.sy + ly, c.x, c.y, c.z, alpha, wx, wy, ba, bb};
            shardVerts_.insert(shardVerts_.end(), f, f + 12);
        };
        // (crack edges: center-A (bb = 0) and center-B (ba = 0); A-B is the cell's own edge)
        emit(s.ax, s.ay, 1.f, 1.f);
        emit(s.bx, s.by, 1.f, 0.f);
        emit(s.cx, s.cy, 0.f, 1.f);
    }
}

void KintsugiUniverse::buildFx() {
    sprites_.clear();
    const float moon = smoothstepf(0.f, 1.f, slow_);
    // glints on the veins
    for (const Glint& g : glints_) {
        if (g.t < 0.f) continue;
        const float k = g.t / g.life;
        const float a = std::sin(PI * saturate(k)) * (1.f - moon * 0.6f);
        const vec3 c = lerp(cur_.metal, cur_.hot, 0.6f) * 2.2f;
        sprites_.push_back({{g.p.x, g.p.y, g.size, 0.f}, {c.x, c.y, c.z, a}, {0.f, 0.f, 0.f, 0.f}});
    }
    // gold leaf: bright when it turns to face the light
    for (const Leaf& l : leaves_) {
        if (l.t < 0.f) continue;
        const float face = std::cos(l.turn);
        const float flash = std::pow(std::fabs(face), 6.f);
        const float a = smoothstepf(0.f, 0.15f, l.t) * (1.f - smoothstepf(l.life - 1.5f, l.life, l.t));
        if (blocked(l.p.x, l.p.y) && std::fabs(l.p.x) > WELL_X + 0.5f) continue; // (never over the HUD)
        const float hot = std::exp(-l.t * 1.5f);
        const vec3 c = lerp(cur_.metal * (0.35f + 1.6f * flash), cur_.hot * 2.5f, hot * 0.7f);
        sprites_.push_back({{l.p.x, l.p.y, l.size, l.rot}, {c.x, c.y, c.z, a}, {1.f, std::max(0.12f, std::fabs(face)), l.seed, 0.f}});
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float KintsugiUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
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

void KintsugiUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
                                 std::vector<float>& out) const {
    if (!f.ok) return;
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
}

void KintsugiUniverse::drawText(const Font& f, const std::vector<float>& q, const vec3& color, float alpha, float halo, float bias) {
    if (q.empty() || !f.ok) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    set3f(progText_, "uColor", color);
    set1f(progText_, "uAlpha", alpha);
    set1f(progText_, "uHaloAmt", halo);
    set1f(progText_, "uBias", bias);
    set1f(progText_, "uBridge", smoothstepf(0.3f, 0.95f, bridge_.amount));
    set3f(progText_, "uBridgeInk", bridge_.ink);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(float), q.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 4));
}

// The HUD as if engraved in the slab: Marcellus capitals (an inscription in stone) for the labels, Cormorant for
// the numbers, its italic for the song, the section and the celebrations, the accents in the metal.
void KintsugiUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    if (!panels.empty()) {
        std::vector<float> v;
        auto quad = [&](float x0, float y0, float x1, float y1, const vec3& c, float a) {
            const float q[6][6] = {{x0, y0, c.x, c.y, c.z, a}, {x1, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a},
                                   {x0, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a}, {x0, y1, c.x, c.y, c.z, a}};
            for (auto& r : q) v.insert(v.end(), r, r + 6);
        };
        for (const HudRect& r : panels) {
            const vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? cur_.metal : cur_.stone * 0.6f;
            quad(r.x, r.y, r.x + r.w, r.y + r.h, c, r.accent || r.custom ? r.alpha : std::min(1.f, r.alpha * 1.1f));
        }
        glUseProgram(progShape_);
        set2f(progShape_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(shapeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 6));
    }
    const vec3 ink = cur_.ink * 0.9f;
    const vec3 dim = cur_.dim * 0.9f;
    const vec3 metal = cur_.metal * 1.05f;
    std::vector<float> q;
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        const float alpha = saturate(h.alpha);
        q.clear();
        auto place = [&](const Font& f, const std::string& text, float size, float track, float dy = 0.f) {
            const float w = textWidth(f, text, size, track);
            textQuads(f, text, ugl::hudLeft(h, w, width_), h.y + dy, size, track, q);
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            // "Kintsugi" in the italic, the stone and its metal in small engraved capitals after it
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            name = titleCase(name);
            const float sz = base * 1.5f, vsz = base * 0.56f, vtr = vsz * 0.26f;
            const float wName = textWidth(fontItalic_, name, sz, 0.f);
            const float wVar = var.empty() ? 0.f : textWidth(fontCaps_, var, vsz, vtr);
            const float gap = base * 0.8f;
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            textQuads(fontItalic_, name, x, h.y - base * 0.15f, sz, 0.f, q);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            drawText(fontItalic_, q, ink, a, 0.45f, 0.03f);
            if (!var.empty()) {
                q.clear();
                textQuads(fontCaps_, var, x + wName + gap, h.y + base * 0.3f, vsz, vtr, q);
                drawText(fontCaps_, q, metal, a, 0.45f, 0.04f);
            }
            continue;
        }
        case HUD_SECTION:
            place(fontItalic_, titleCase(h.text), base * 1.45f, 0.f, -base * 0.1f);
            drawText(fontItalic_, q, metal, saturate(0.2f + h.alpha * 1.9f), 0.45f, 0.03f);
            continue;
        case HUD_TITLE:
            place(fontItalic_, h.text, base * 1.4f, 0.f, -base * 0.16f);
            drawText(fontItalic_, q, ink, saturate(h.alpha * 1.3f), 0.45f, 0.03f);
            continue;
        case HUD_INFO:
            place(fontCaps_, h.text, base * 0.62f, base * 0.25f);
            drawText(fontCaps_, q, dim, saturate(alpha * 1.4f), 0.45f, 0.05f);
            continue;
        case HUD_LABEL:
            place(fontCaps_, h.text, base * 0.68f, base * 0.34f);
            drawText(fontCaps_, q, dim, saturate(alpha * 1.3f), 0.45f, 0.05f);
            continue;
        case HUD_VALUE:
            place(fontSerif_, h.text, base * 1.3f, base * 0.04f, -base * 0.12f);
            drawText(fontSerif_, q, ink, alpha, 0.45f, 0.03f);
            continue;
        case HUD_NOTICE:
            place(fontItalic_, h.text, base * 1.3f, 0.f);
            drawText(fontItalic_, q, metal, alpha, 0.45f, 0.03f);
            continue;
        case HUD_KEY:
            place(fontCaps_, h.text, base * 0.8f, base * 0.1f);
            drawText(fontCaps_, q, ink, alpha, 0.45f, 0.05f);
            continue;
        case HUD_TOAST:
            place(fontItalic_, titleCase(h.text), base * 1.6f, 0.f, -base * 0.15f);
            drawText(fontItalic_, q, metal, alpha, 0.55f, 0.03f);
            continue;
        case HUD_TOAST_SUB:
            place(fontCaps_, h.text, base * 0.75f, base * 0.25f);
            drawText(fontCaps_, q, ink, alpha * 0.9f, 0.45f, 0.05f);
            continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontItalic_, titleCase(h.text), base * 1.4f, 0.f, -base * 0.1f);
                drawText(fontItalic_, q, ink, alpha, 0.55f, 0.03f);
            } else {
                place(fontCaps_, h.text, base * 0.78f, base * 0.14f);
                drawText(fontCaps_, q, h.accent ? metal : ink, alpha, 0.45f, 0.05f);
            }
            continue;
        default:
            place(fontCaps_, h.text, base * 0.78f, base * 0.1f);
            drawText(fontCaps_, q, h.accent ? metal : ink, alpha, 0.45f, 0.05f);
            continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void KintsugiUniverse::setLight(GLuint prog) {
    // the raking light comes from the upper left (from the right in moonlight); the soft box's reflection drifts
    // slowly across the slab over the song
    const float moon = smoothstepf(0.f, 1.f, slow_);
    const float lx = lerpf(-0.75f, 0.8f, moon), ly = lerpf(0.65f, 0.6f, moon);
    set3f(prog, "uLight", vec3(lx, ly, 16.f));
    const float drift = std::sin(now_ * 0.013f + seedF_) * 4.f;
    set4f(prog, "uSheen", cur_.sheenAngle, -3.f + drift, 5.5f, 1.f);
}

void KintsugiUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const Palette target = paletteOf(variant_);
        if (!palSet_) { pal_ = target; palSet_ = true; }
        else lerpPalette(pal_, target, 1.f - std::exp(-dt * 1.2f));
    }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 1.0f, dt);
    cur_ = pal_;
    if (slow_ > 0.001f) lerpPalette(cur_, moonOf(pal_), smoothstepf(0.f, 1.f, slow_));
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.55f : 0.f), 3.f, dt);
    if (overT_ >= 0.f) {
        if (!game.over()) { overT_ = -1.f; dying_.clear(); }
        else overT_ += dt;
    }
    over_ = overT_ >= 0.f ? smoothstepf(1.5f, 5.f, overT_) * 0.6f : approach(over_, 0.f, 1.5f, dt);
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.25f, slow_);
    if (!f.paused) simulate(game, music, dt, wdt);
    if (marbleDirty_) bakeMarble();

    const float world = 1.f - saturate(bridge_.amount);
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float moon = smoothstepf(0.f, 1.f, slow_);
    // The light of the section: steady (calm dim and cool, mid warmer, a chorus full, a drop fuller with one swell
    // that settles); never pulsing with the beat.
    const float gain = (0.62f + 0.22f * wm + 0.16f * wp + 0.1f * dropness_ + 0.25f * swell_) * (1.f - 0.55f * over_) *
                       lerpf(1.f, 0.8f, moon);
    const vec3 lightCol = lerp(lerp(cur_.light, vec3(0.75f, 0.82f, 1.f), 0.35f * (1.f - wm)), cur_.light * cur_.warm * 1.3f,
                               0.35f * dropness_ * (1.f - moon));

    glBindFramebuffer(GL_FRAMEBUFFER, sceneT_.fbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    // ---- The slab and the well.
    glUseProgram(progStone_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, marbleT_.tex);
    set1i(progStone_, "uMarble", 0);
    setLight(progStone_);
    set4f(progStone_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set3f(progStone_, "uStone", cur_.stone);
    set3f(progStone_, "uCloud", cur_.cloud);
    set3f(progStone_, "uCalcite", lerp(cur_.cloud, cur_.calcite, cur_.veinAmt));
    set3f(progStone_, "uLightCol", lightCol);
    set3f(progStone_, "uWarm", cur_.warm);
    set1f(progStone_, "uGain", gain);
    set1f(progStone_, "uWarmAmt", 0.32f * dropness_ * (1.f - moon));
    set1f(progStone_, "uWellDark", 0.62f);
    set3f(progStone_, "uRimCol", cur_.metal);
    set1f(progStone_, "uRim", (0.07f + 0.04f * wm + 0.5f * levelGlint_) * (1.f - over_));
    set1f(progStone_, "uBridge", saturate(bridge_.amount));
    set3f(progStone_, "uBridgeCol", bridge_.color);
    set4f(progStone_, "uWell", -WELL_X, -WELL_Y, WELL_X, WELL_Y);
    set1f(progStone_, "uPx", px_);
    ugl::drawFullscreen();

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // ---- The veins in the stone (the lock cracks come later, over the stack).
    auto drawVeins = [&](bool locks) {
        buildVeins(locks);
        if (veinVerts_.empty()) return;
        glUseProgram(progVein_);
        setMat(progVein_, "uVP", vp_);
        setLight(progVein_);
        set1f(progVein_, "uNow", now_);
        set1f(progVein_, "uPx", px_);
        set1f(progVein_, "uBridge", saturate(bridge_.amount));
        set3f(progVein_, "uMetal", cur_.metal);
        set3f(progVein_, "uMolten", cur_.molten);
        set3f(progVein_, "uHot", cur_.hot);
        set1f(progVein_, "uGain", gain);
        set1f(progVein_, "uSurgeT", surgeT_);
        set1f(progVein_, "uSurgeAmp", surgeAmp_);
        set4f(progVein_, "uColL", KEEP_L[0], KEEP_L[1], KEEP_L[2], KEEP_L[3]);
        set4f(progVein_, "uColTL", KEEP_TL[0], KEEP_TL[1], KEEP_TL[2], KEEP_TL[3]);
        set4f(progVein_, "uColBL", KEEP_BL[0], KEEP_BL[1], KEEP_BL[2], KEEP_BL[3]);
        set4f(progVein_, "uColR", KEEP_R[0], KEEP_R[1], KEEP_R[2], KEEP_R[3]);
        glBindVertexArray(veinVao_);
        glBindBuffer(GL_ARRAY_BUFFER, veinVbo_);
        glBufferData(GL_ARRAY_BUFFER, veinVerts_.size() * sizeof(float), veinVerts_.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(veinVerts_.size() / 12));
    };
    drawVeins(false);

    // ---- Sprites: glints and gold leaf, behind the pieces.
    buildFx();
    auto drawSprites = [&](const std::vector<Sprite>& v) {
        if (v.empty()) return;
        glUseProgram(progSprite_);
        setMat(progSprite_, "uVP", vp_);
        set1f(progSprite_, "uBridge", saturate(bridge_.amount));
        set3f(progSprite_, "uMetal", cur_.metal);
        set3f(progSprite_, "uBridgeInk", bridge_.ink);
        glBindVertexArray(spriteVao_);
        glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Sprite), v.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)v.size());
    };
    drawSprites(sprites_);

    // ---- The ceramic: the stack, the piece, the ghost, the previews; then the shards of cleared rows.
    buildBoard(game);
    auto ceramicUniforms = [&](GLuint p) {
        glUseProgram(p);
        setMat(p, "uVP", vp_);
        setLight(p);
        set1f(p, "uPx", px_);
        set3f(p, "uMetal", cur_.metal);
        set3f(p, "uMolten", cur_.molten);
        set3f(p, "uHot", cur_.hot);
        set3f(p, "uLightCol", lerp(lightCol, vec3(1.f), 0.5f));
        set1f(p, "uGain", 0.75f + 0.25f * gain);
        set1f(p, "uBridge", saturate(bridge_.amount));
        set3f(p, "uBridgeInk", bridge_.ink);
    };
    if (!blocks_.empty()) {
        ceramicUniforms(progBlock_);
        glBindVertexArray(blockVao_);
        glBindBuffer(GL_ARRAY_BUFFER, blockVbo_);
        glBufferData(GL_ARRAY_BUFFER, blocks_.size() * sizeof(Block), blocks_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)blocks_.size());
    }
    if (!shardVerts_.empty()) {
        ceramicUniforms(progShard_);
        glBindVertexArray(shardVao_);
        glBindBuffer(GL_ARRAY_BUFFER, shardVbo_);
        glBufferData(GL_ARRAY_BUFFER, shardVerts_.size() * sizeof(float), shardVerts_.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(shardVerts_.size() / 12));
    }
    drawVeins(true); // the hairline cracks of the locks, at the contact

    // ---- The gauge: a thin ring of metal, mended as it fills.
    {
        std::vector<Sprite> g;
        const vec3 c = layout::GAUGE_CENTER;
        const vec3 m = cur_.metal;
        g.push_back({{c.x, c.y, layout::GAUGE_R * 1.1f, 0.f}, {m.x, m.y, m.z, 1.f},
                     {2.f, saturate(gaugeShown_), game.bonusReady() ? 1.f : 0.f, game.bonusActive() ? 1.f : 0.f}});
        drawSprites(g);
    }

    drawHud(f.hud, f.panels);
    glDisable(GL_BLEND);

    // ---- Bloom: the molten gold.
    glUseProgram(progDown_);
    set1i(progDown_, "uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    GLuint src = sceneT_.tex;
    int sw = width_, sh = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        bloomT_[i].bind();
        glBindTexture(GL_TEXTURE_2D, src);
        set2f(progDown_, "uTexel", 0.5f / sw, 0.5f / sh);
        set1f(progDown_, "uThreshold", i == 0 ? 0.8f : 0.f);
        ugl::drawFullscreen();
        src = bloomT_[i].tex;
        sw = bloomT_[i].w;
        sh = bloomT_[i].h;
    }
    glUseProgram(progUp_);
    set1i(progUp_, "uTex", 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = BLOOM_MIPS - 1; i > 0; i--) {
        bloomT_[i - 1].bind();
        glBindTexture(GL_TEXTURE_2D, bloomT_[i].tex);
        set2f(progUp_, "uTexel", 1.f / bloomT_[i].w, 1.f / bloomT_[i].h);
        ugl::drawFullscreen();
    }
    glDisable(GL_BLEND);

    // ---- Post.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progPost_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneT_.tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomT_[0].tex);
    glActiveTexture(GL_TEXTURE0);
    set1i(progPost_, "uScene", 0);
    set1i(progPost_, "uBloom", 1);
    set1f(progPost_, "uTime", (float)f.time);
    set1f(progPost_, "uFade", saturate(f.fade));
    set1f(progPost_, "uDim", dimT_);
    set1f(progPost_, "uBridge", saturate(bridge_.amount));
    set1f(progPost_, "uBloomAmt", 0.6f * world);
    set1f(progPost_, "uGrain", 0.014f);
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_kintsugi() {
    static const UniverseEntry e{
        "KINTSUGI", "dark polished stone, ceramic pieces mended with gold, gold veins in the marble",
        [] { return std::unique_ptr<Universe>(new KintsugiUniverse()); },
        // Songs that build and drop suit it best (the gold needs a peak to melt); everything still plays.
        [](const SongLook& l) { return 0.7f + 0.4f * l.dynamics + 0.15f * l.bassWeight; },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
