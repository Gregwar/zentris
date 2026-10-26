// GARDEN: clods of earth that grow grass and flowers.
//
// One world: a wide line of earth along the bottom of the picture (flat under the well, rising gently toward the
// edges) under a plain sky with one sun or moon, and lots of empty air. The pieces are clods of the same earth, with
// a soft crumbly edge, a fine grain and a few pebbles; landed, they merge into one mass of earth that grows out of
// the ground line. Wherever the top of a clod is open to the air, life takes hold: a fringe of
// grass sprouts within a second or two, then a few small flowers open slowly over about twelve seconds. A block
// landing on top buries them: the grass and the flowers fold back into the soil in one quick, smooth motion.
//
// The ground line has its own vegetation and follows the song: calm parts and breaks leave it dry (pale, sparse
// stems), mid brings the green back, a chorus makes it lush, and a drop makes it bloom: on the drop's first downbeat
// flowers open across the whole line, quickly at first and then slowing, petals rise from them on the breeze and the
// sky takes the flowers' glow from the horizon; it stays in bloom for the whole drop, swaying slowly on the beat
// grid, and when the drop ends it fades back and dries out over seconds.
//
// A cleared row crumbles into soil dust that falls and settles (its flowers fall with it), a hard landing pushes a
// puff of dust out from the contact, a Tetris sends dandelion seeds up from the whole ground line and opens the
// stack's flowers at once, slow motion brings dusk and fireflies, game over crumbles the stack back into the earth,
// and a change of universe lets the plants sink and the sky fade to the bridge.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane (like WATERCOLOR).
#include "universes/garden/garden.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/garden/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Lora (Italic for titles, Medium for labels and numbers), SIL Open Font License, see OFL-Lora.txt.
const unsigned char FONT_ITALIC[] = {
#include "universes/garden/Lora-Italic.ttf.inc"
};
const unsigned char FONT_ROMAN[] = {
#include "universes/garden/Lora-Medium.ttf.inc"
};

namespace shaders = garden_shaders;
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
vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }
float hashf(float a, float b) {
    const uint64_t h = splitmix64((uint64_t)(int64_t)std::lround(a * 1000.f) * 73856093ull ^ (uint64_t)(int64_t)std::lround(b * 1000.f) * 19349663ull);
    return (h >> 40) * (1.0f / 16777216.0f);
}

constexpr float WELL_X = 5.15f, WELL_Y = 10.1f;

// The ground line's top edge (board y) at board x; the same function as SKY_FS's groundY.
float groundY(float x, float seed) {
    const float a = std::fabs(x);
    const float side = smoothstepf(6.f, 12.f, a);
    return -10.05f + 0.9f * smoothstepf(5.5f, 7.6f, a) + 0.25f * smoothstepf(7.6f, 11.f, a) + 3.f * std::pow(smoothstepf(11.f, 24.f, a), 1.3f) +
           (0.22f * std::sin(x * 0.37f + seed) + 0.12f * std::sin(x * 0.91f + seed * 1.7f)) * side;
}

// The variants (sRGB): the sky (top, at the horizon), the sun or moon, the drop's glow, the ground (top, deep, its
// lit lip), the grass (green, lush, dry), the moss on the clods, the HUD ink, the accent, the flowers' heart, the
// dusk of slow motion; the stack's three flowers, the ground line's four bloom colors; the soil of every clod;
// the sun (x, y, radius), moon 0/1, stars, the opacity of the well's pale panel.
struct VariantDef {
    const char* name;
    unsigned skyTop, skyLow, sun, glow, ground, groundDeep, lip, grass, lush, dry, moss, ink, accent, center, dusk;
    unsigned flower[3];
    unsigned bloom[4];
    unsigned soil;
    float sunX, sunY, sunR, moon, stars, veil;
};
const VariantDef VARIANTS[] = {
    // A spring morning: a clear pale sky, daisies, buttercups and pink clover; the drop glows blossom pink.
    {"SPRING MORNING", 0x8fbbe0, 0xf2ecdc, 0xfff6e0, 0xf4b0c4, 0x4a3626, 0x271b14, 0x7a5c40, 0x7ea446, 0x4f8a34, 0xcdb98a,
     0x5a7a30, 0x3b3226, 0xd9607e, 0xf2c230, 0x3c3a6a,
     {0xfbfaf4, 0xf6d23e, 0xf0a0b8}, {0xfbfaf4, 0xf6d23e, 0xec7f9e, 0xa88ae0},
     0x8c6242,
     -16.f, 6.5f, 1.2f, 0.f, 0.f, 0.16f},
    // A summer dusk: a low sun in an apricot haze under a dusky blue, cream and coral flowers; the drop glows magenta.
    {"SUMMER DUSK", 0x34406c, 0xf2a676, 0xffd6a6, 0xd8589c, 0x2c1f1c, 0x150e0e, 0x6a4030, 0x6a8a3a, 0x4a7a32, 0xc8a060,
     0x4a6a2c, 0xf6e6d4, 0xff9a6a, 0xffc23a, 0x1e1e48,
     {0xfff0dc, 0xf3895e, 0xe0b8f0}, {0xfff0dc, 0xff8a5c, 0xffc84a, 0xd890e0},
     0x7a5238,
     16.5f, -5.f, 2.f, 0.f, 0.f, 0.13f},
    // An autumn field: an overcast pale sky, rust, gold and aster violet; the drop glows amber.
    {"AUTUMN FIELD", 0xa9b8c8, 0xebe0c8, 0xfff2dc, 0xf0a048, 0x3e2c1e, 0x21170f, 0x7a5a36, 0x8a9244, 0x6a8a3a, 0xbf9658,
     0x6a6a2a, 0x3c2c20, 0xc0602a, 0x6a3a1a, 0x2e2c50,
     {0xc8642a, 0xe6b440, 0x8c6ab4}, {0xd06a2a, 0xe8b23c, 0x9a70c0, 0xf4ead8},
     0x96643c,
     -15.5f, 7.f, 1.4f, 0.f, 0.f, 0.16f},
    // A night garden: a deep blue sky, the moon, white moonflowers and pale blue; the drop glows violet.
    {"NIGHT GARDEN", 0x0a1024, 0x26304c, 0xe6ebf4, 0x6a4ab8, 0x15131a, 0x08070b, 0x3a3440, 0x3a5a4a, 0x2e6a52, 0x6a6a64,
     0x2e4a3a, 0xdde3ee, 0xb8c8ff, 0xf4e0a0, 0x0a0a20,
     {0xf2f5ff, 0xa4c0ff, 0xcaa8f4}, {0xf2f5ff, 0xa4c0ff, 0xd0a8f8, 0xf8e0a0},
     0x8e7462,
     15.5f, 6.f, 1.f, 1.f, 1.f, 0.07f},
    // A misty dawn: lavender grey over a pink horizon, sage grass, white and lilac flowers; the drop glows gold.
    {"MISTY DAWN", 0xb4b6d4, 0xf4d8cc, 0xfff2e6, 0xffc25a, 0x3e3232, 0x1e1818, 0x7a6464, 0x84a07e, 0x5e8c68, 0xbcae9c,
     0x5a7a5e, 0x3a3044, 0xb070c0, 0xf4d070, 0x2a2850,
     {0xffffff, 0xc4a0e8, 0xf4b0b8}, {0xffffff, 0xc89cf0, 0xf2a0b4, 0xf4d070},
     0x80604e,
     -15.f, -3.5f, 1.5f, 0.f, 0.f, 0.16f},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));
constexpr int NIGHT = 3;

using Palette = GardenUniverse::Palette;
static_assert(sizeof(Palette) % sizeof(float) == 0, "a palette is floats only");
constexpr int PAL_FLOATS = (int)(sizeof(Palette) / sizeof(float));

Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    auto L = [](unsigned c) { return ugl::toLinear(hex(c)); };
    Palette p;
    p.skyTop = L(d.skyTop); p.skyLow = L(d.skyLow); p.sun = L(d.sun); p.glow = L(d.glow); p.ground = L(d.ground);
    p.groundDeep = L(d.groundDeep); p.lip = L(d.lip); p.grass = L(d.grass); p.lush = L(d.lush); p.dry = L(d.dry);
    p.moss = L(d.moss); p.ink = L(d.ink); p.accent = L(d.accent); p.center = L(d.center); p.dusk = L(d.dusk);
    for (int i = 0; i < 3; i++) p.flower[i] = L(d.flower[i]);
    for (int i = 0; i < 4; i++) p.bloom[i] = L(d.bloom[i]);
    for (int i = 0; i < 7; i++) p.piece[i] = L(d.soil); // one earth for every clod
    p.sunX = d.sunX; p.sunY = d.sunY; p.sunR = d.sunR; p.moon = d.moon; p.stars = d.stars; p.veil = d.veil;
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
// how far the bottom row reaches down into the ground line (cells)
constexpr float ROOT = 0.35f;
const int NDX[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
const int NDY[8] = {0, 0, 1, -1, 1, 1, -1, -1};

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool GardenUniverse::init(int width, int height) {
    progSky_ = compile(ugl::FULLSCREEN_VS, shaders::SKY_FS, "garden sky");
    progCell_ = compile(shaders::CELL_VS, shaders::CELL_FS, "garden cell");
    progSprite_ = compile(shaders::SPRITE_VS, shaders::SPRITE_FS, "garden sprite");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "garden text");
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "garden shape");
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "garden post");

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
    instanced(cellVao_, cellVbo_, 5, sizeof(CellInst));
    instanced(spriteVao_, spriteVbo_, 4, sizeof(SpriteInst));

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
void GardenUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] garden font init failed\n");
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

void GardenUniverse::resize(int width, int height) {
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
    buildGround();
}

GardenUniverse::~GardenUniverse() {
    sceneT_.destroy();
    for (GLuint p : {progSky_, progCell_, progSprite_, progText_, progShape_, progPost_}) glDeleteProgram(p);
    for (GLuint v : {cellVao_, spriteVao_, textVao_, shapeVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, cellVbo_, spriteVbo_, textVbo_, shapeVbo_}) glDeleteBuffers(1, &b);
    for (Font* f : {&fontItalic_, &fontRoman_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// The ground line's plants: blades every few hundredths of a unit across the whole width, and the flowers that
// open there at a drop (outside the well, a few to each stretch).
void GardenUniverse::buildGround() {
    blades_.clear();
    gflowers_.clear();
    Rng r(seed_ ^ 0x6A4DE1ull);
    const float X = viewH_.x + 1.f;
    for (float x = -X; x < X; x += 0.05f) blades_.push_back({x + r.range(-0.02f, 0.02f), r.uniform(), r.uniform(), r.uniform(), r.uniform()});
    for (float x = -X; x < X; x += r.range(0.13f, 0.3f)) {
        if (std::fabs(x) < 6.2f) continue;
        GFlower f;
        f.x = x;
        f.stem = r.range(0.5f, 1.6f);
        f.head = r.range(0.15f, 0.27f);
        f.u = r.range(0.f, 0.92f);
        f.lean = r.range(-0.18f, 0.18f);
        f.seed = r.range(0.f, 100.f);
        f.color = r.irange(0, 3);
        f.petals = r.irange(5, 8);
        f.open = false;
        gflowers_.push_back(f);
    }
}

// =====================================================================================================
// Song, level
// =====================================================================================================

// The garden from the song: dark minor songs walk in the night garden; bright major ones in spring or at a summer
// dusk; other minor songs in autumn or at dawn; the key's hue picks among them.
int GardenUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    int v;
    if (t.hasSong && t.minor && t.brightness < 0.42f) v = NIGHT;
    else if (t.hasSong && !t.minor && t.brightness > 0.55f) v = h < 0.5f ? 0 : 1;
    else if (t.hasSong && t.minor) v = h < 0.5f ? 2 : 4;
    else v = (int)(h * VARIANT_COUNT) % VARIANT_COUNT;
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void GardenUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    seedF_ = (float)(look.seed % 1000) * 0.0137f;
    rng_ = Rng(look.seed ^ 0x6A4Dull);
    buildGround();
    if (seconds <= 0.f) palSet_ = false;
}

void GardenUniverse::setLevel(int level, float energy, float seconds, float impact) {
    (void)impact;
    level = std::clamp(level, 0, 2);
    energy_ = energy;
    if (!hasLevel_ || seconds < 0.1f) {
        const bool first = !hasLevel_;
        hasLevel_ = true;
        level_ = level;
        lv_ = lvFrom_ = lvTo_ = (float)level;
        lvT_ = 1.f;
        if (first || seconds < 0.1f) {
            // an instant level (first frame, viewer): the ground line is already in that state
            const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
            dry_ = 1.f - 0.65f * wm - 0.35f * wp;
            grassH_ = 0.45f + 0.35f * wm + 0.6f * wp;
            density_ = 0.22f + 0.5f * wm + 0.28f * wp;
        }
        return;
    }
    level_ = level;
    lvFrom_ = lv_;
    lvTo_ = (float)level;
    lvT_ = 0.f;
    lvDur_ = std::clamp(seconds, 1.5f, 6.f);
}

// The drop's first downbeat: its hit fires as soon as the section is known to be a drop (a chorus rising to the
// peak stays lush and green).
void GardenUniverse::drop(float strength) {
    (void)strength;
    dropPending_ = 2.5f;
}

void GardenUniverse::refresh(float strength) {
    gust_ = std::max(gust_, 0.35f * strength);
    const int n = 2 + (int)(strength * 4.f);
    for (int i = 0; i < n; i++) {
        const float side = rng_.chance(0.5f) ? -1.f : 1.f;
        const float x = side * rng_.range(6.5f, viewH_.x - 1.f);
        Particle p{};
        p.p = v2(x, groundY(x, seedF_) + rng_.range(0.2f, 0.7f));
        p.v = v2(side * rng_.range(0.15f, 0.45f), rng_.range(0.35f, 0.7f));
        p.life = rng_.range(6.f, 9.f); p.size = rng_.range(0.06f, 0.09f); p.grav = -0.02f; p.drag = 0.15f;
        p.kind = P_SEED; p.col = vec3(0.95f, 0.94f, 0.9f); p.seed = rng_.uniform() * 50.f;
        parts_.push_back(p);
    }
}

void GardenUniverse::levelUp() { refresh(1.f); }

vec2 GardenUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints GardenUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    h.background = lerp(p.skyLow, p.skyTop, 0.45f);
    h.light = ugl::luma(h.background) > 0.28f;
    h.ink = p.piece[1];
    h.accent = p.accent;
    h.styles = TS_INK | TS_LUMA | TS_REBUILD;
    return h;
}

// =====================================================================================================
// Game events
// =====================================================================================================

void GardenUniverse::emitDust(vec2 p, vec2 v, const vec3& col, float size, float life, float grav, bool real) {
    if (parts_.size() > 2500) return;
    Particle q{};
    q.p = p; q.v = v; q.life = life; q.size = size; q.grav = grav; q.drag = 1.2f; q.kind = P_DUST; q.col = col;
    q.real = real; q.front = true; q.seed = rng_.uniform();
    parts_.push_back(q);
}

int GardenUniverse::maskOf(int x, int y) const {
    const Soil& s = soil_[y][x];
    int m = 0;
    for (int i = 0; i < 8; i++) {
        const int nx = x + NDX[i], ny = y + NDY[i];
        if (nx < 0 || nx >= Game::W || ny < 0 || ny >= Game::H) continue;
        if (soil_[ny][nx].id >= 0 && s.id >= 0) m |= 1 << i; // the earth is one mass: every clod joins its neighbours
    }
    return m;
}

float GardenUniverse::growthAt(const Soil& s) const { return smoothstepf(0.1f, 1.6f, s.age); }

void GardenUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::HardDrop:
        lastDrop_ = ev.count;
        break;
    case GameEvent::Lock: {
        if (ev.cells.empty()) break;
        // the piece's cells belong together (one clod); its grain origin is its bounding box's corner, the same
        // origin the falling piece used, so the clod keeps its grain as it lands
        const int id = nextId_++;
        int minX = 99, minY = 99, maxY = -1;
        float cx = 0.f;
        for (auto& c : ev.cells) { minX = std::min(minX, c.x); minY = std::min(minY, c.y); maxY = std::max(maxY, c.y); cx += c.x; }
        cx /= (float)ev.cells.size();
        const int type = std::clamp(ev.cells[0].type, 0, 6);
        const float sx = type * 5.1f + (float)(id % 17) * 3.7f, sy = type * 2.3f + (float)(id % 13) * 1.9f;
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            Soil& s = soil_[c.y][c.x];
            s.id = id; s.type = std::clamp((int)c.type, 0, 6);
            s.nc = v2((float)(c.x - minX) + sx, -(float)(c.y - minY) + sy);
            s.age = 0.f; s.cover = 0.f; s.coverType = -1;
            s.seed = rng_.range(0.f, 100.f);
        }
        // a hard landing pushes one puff of soil dust out from under the piece, at the contact
        if (lastDrop_ >= 2 && bridge_.amount < 0.5f) {
            const vec3 col = lerp(pal_.piece[type], pal_.ground, 0.3f) * 1.1f;
            const float k = std::min(1.f, 0.5f + lastDrop_ / 16.f);
            for (auto& c : ev.cells) {
                const bool bottom = c.y + 1 >= Game::H || std::none_of(ev.cells.begin(), ev.cells.end(), [&](const GameEvent::CellInfo& o) {
                                        return o.x == c.x && o.y == c.y + 1;
                                    });
                if (!bottom) continue;
                const vec3 p = layout::cellPos((float)c.x, (float)c.y);
                const float side = c.x < cx - 0.1f ? -1.f : (c.x > cx + 0.1f ? 1.f : (rng_.chance(0.5f) ? -1.f : 1.f));
                for (int i = 0; i < 3; i++) {
                    const float ox = rng_.range(-0.45f, 0.45f);
                    emitDust(v2(p.x + ox, p.y - 0.5f + rng_.range(0.f, 0.08f)),
                             v2(side * rng_.range(0.6f, 2.0f) * k + ox * 0.8f, rng_.range(0.15f, 0.6f) * k), col * rng_.range(0.85f, 1.15f),
                             rng_.range(0.05f, 0.1f), rng_.range(0.45f, 0.7f), 1.2f, false);
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
        // the row crumbles into soil dust that falls and settles; its flowers fall with it
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const Soil& s = soil_[c.y][c.x];
            const int type = std::clamp((int)c.type, 0, 6);
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.type = type;
            d.mask = s.id >= 0 && s.type == type ? maskOf(c.x, c.y) : 0;
            // a crumbling clod does not stay joined to the cells that remain
            int keep = 0;
            for (int i = 0; i < 8; i++) {
                const int ny = c.y + NDY[i];
                if (ny >= 0 && ny < Game::H && cleared[ny]) keep |= d.mask & (1 << i);
            }
            d.mask = keep;
            d.nc = s.id >= 0 ? s.nc : v2((float)c.x, -(float)c.y);
            d.t = 0.f;
            d.delay = std::fabs(c.x - 4.5f) * 0.012f;
            d.seed = s.seed;
            const bool exposed = c.y == 0 || soil_[c.y - 1][c.x].id < 0 || cleared[c.y - 1];
            d.age = exposed && s.cover <= 0.f ? s.age : 0.f;
            dying_.push_back(d);
            const vec3 col = pal_.piece[type];
            for (int i = 0; i < 9; i++)
                emitDust(v2(p.x + rng_.range(-0.45f, 0.45f), p.y + rng_.range(-0.45f, 0.45f)),
                         v2(rng_.range(-0.7f, 0.7f), rng_.range(-0.6f, 0.4f)), col * rng_.range(0.7f, 1.2f),
                         rng_.range(0.035f, 0.075f), rng_.range(0.32f, 0.48f), 14.f, true);
            // the flower on its top falls with it
            if (d.age > 0.f && hashf(s.seed, 99.f) < 0.45f) {
                const float start = 1.5f + 6.5f * hashf(s.seed, 97.f);
                const float op = smoothstepf(start, start + 3.5f, d.age);
                if (op > 0.2f) {
                    Particle q{};
                    q.p = v2(p.x - 0.3f + 0.6f * hashf(s.seed, 98.f), p.y + 0.7f);
                    q.v = v2(rng_.range(-0.4f, 0.4f), rng_.range(-0.5f, 0.f));
                    q.life = 0.5f; q.size = 0.085f; q.grav = 12.f; q.drag = 0.5f; q.kind = P_FLOWER;
                    q.col = pal_.flower[(int)(hashf(s.seed, 96.f) * 2.999f)]; q.rot = rng_.range(-0.3f, 0.3f);
                    q.spin = rng_.range(-4.f, 4.f); q.real = true; q.front = true; q.seed = op;
                    parts_.push_back(q);
                }
            }
        }
        // collapse the bookkeeping like the board
        Soil next[Game::H][Game::W];
        int dst = Game::H - 1;
        for (int y = Game::H - 1; y >= 0; y--) {
            if (cleared[y]) continue;
            for (int x = 0; x < Game::W; x++) next[dst][x] = soil_[y][x];
            dst--;
        }
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) soil_[y][x] = next[y][x];
        if (tetris) {
            // dandelion seeds rise from the whole ground line, and the stack's flowers open at once
            rush_ = 1.f;
            for (int i = 0; i < 70; i++) {
                const float side = rng_.chance(0.5f) ? -1.f : 1.f;
                const float x = side * rng_.range(5.8f, viewH_.x);
                Particle p{};
                p.p = v2(x, groundY(x, seedF_) + rng_.range(0.1f, 0.6f));
                p.v = v2(side * rng_.range(0.1f, 0.5f), rng_.range(0.5f, 1.3f));
                p.age = -rng_.range(0.f, 1.2f);
                p.life = rng_.range(6.f, 9.f); p.size = rng_.range(0.07f, 0.11f); p.grav = -0.02f; p.drag = 0.12f;
                p.kind = P_SEED; p.col = vec3(0.95f, 0.94f, 0.9f); p.seed = rng_.uniform() * 50.f;
                parts_.push_back(p);
            }
        }
        break;
    }
    case GameEvent::TopOut: {
        // game over: the stack crumbles back into the earth, row by row from the top
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (auto& c : ev.cells) {
            if (c.y < Game::HIDDEN || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const Soil& s = soil_[c.y][c.x];
            const int type = std::clamp((int)c.type, 0, 6);
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.type = type;
            d.mask = s.id >= 0 && s.type == type ? maskOf(c.x, c.y) : 0;
            d.nc = s.id >= 0 ? s.nc : v2((float)c.x, -(float)c.y);
            d.t = 0.f;
            d.delay = 0.2f + (c.y - top) * 0.07f + std::fabs(c.x - 4.5f) * 0.015f;
            d.seed = s.seed;
            d.age = 0.f;
            dying_.push_back(d);
            for (int i = 0; i < 4; i++) {
                Particle q{};
                q.p = v2(p.x + rng_.range(-0.45f, 0.45f), p.y + rng_.range(-0.45f, 0.45f));
                q.v = v2(rng_.range(-0.5f, 0.5f), rng_.range(-0.4f, 0.2f));
                q.age = -d.delay; q.life = 0.5f; q.size = rng_.range(0.04f, 0.08f); q.grav = 10.f; q.drag = 1.2f;
                q.kind = P_DUST; q.col = pal_.piece[type] * rng_.range(0.7f, 1.2f); q.real = true; q.front = true;
                parts_.push_back(q);
            }
        }
        for (auto& row : soil_)
            for (auto& c : row) c = Soil{};
        overT_ = 0.f;
        break;
    }
    default: break;
    }
}

// The stack's earth follows the board: cells the universe has not seen land (a board filled for a review) are
// taken as they are, fully grown; open tops grow, covered ones fold their growth back into the soil.
void GardenUniverse::syncSoil(const Game& g, float gdt) {
    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            Soil& s = soil_[y][x];
            if (c.type < 0) { s = Soil{}; continue; }
            if (s.id < 0 || s.type != c.type) {
                s.id = 1000000 + c.type;
                s.type = std::clamp((int)c.type, 0, 6);
                s.nc = v2((float)x + c.type * 5.1f, -(float)y + c.type * 2.3f);
                s.seed = hashf((float)x, (float)y * 7.f + c.type) * 100.f;
                s.age = 12.f + 8.f * hashf(s.seed, 3.f);
                s.cover = 0.f;
            }
            const bool exposed = y == 0 || g.cell(x, y - 1).type < 0;
            if (exposed) {
                if (s.cover > 0.f) { s.cover = 0.f; s.age = 0.f; }
                s.age += gdt * (1.f + 5.f * rush_) * (1.f + 0.6f * bloom_);
            } else if (s.age > 0.f) {
                if (s.cover <= 0.f) s.coverType = std::clamp((int)g.cell(x, y - 1).type, 0, 6);
                s.cover = std::min(1.f, s.cover + gdt / 0.35f);
                if (s.cover >= 1.f) { s.age = 0.f; }
            }
        }
}

// =====================================================================================================
// Simulation
// =====================================================================================================

float GardenUniverse::sway(float x, float seed) const {
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    // on the beat grid, one sway every four beats (never faster than every two)
    const float phase = hasBeat_ ? (float)(beat_ * PI * 0.5) : worldTime_ * 1.2f;
    const float amp = 0.025f + 0.05f * wm + 0.06f * wp + 0.12f * gust_;
    return amp * std::sin(phase + seed * 2.1f + x * 0.04f);
}

void GardenUniverse::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    worldTime_ += wdt;
    hasBeat_ = music.hasSong;
    beat_ = music.beat;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const bool over = overT_ >= 0.f;

    // Is this peak a drop? The HUD's section says it when there is one; else the song's drive (a drop's sections
    // drive harder than a chorus').
    const float dropish = sectionKnown_ ? (sectionDrop_ ? 1.f : 0.f) : smoothstepf(1.27f, 1.42f, music.speed);
    const bool peak = level_ == 2;
    const float bloomTarget = over ? 0.f : (peak ? dropish : 0.f) * smoothstepf(0.5f, 1.f, wp + (peak ? 0.5f : 0.f));
    if (dropPending_ > 0.f) {
        dropPending_ -= dt;
        if (peak && dropish > 0.5f) {
            // the hit: flowers burst open across the line, a flurry of petals, the sky's glow swells
            dropPending_ = 0.f;
            lvT_ = 1.f; lv_ = lvTo_;
            wm = smoothstepf(0.2f, 1.f, lv_); wp = smoothstepf(1.1f, 2.f, lv_);
            bloom_ = std::max(bloom_, 0.14f);
            flurry_ = 1.f;
            gust_ = std::max(gust_, 1.f);
            swell_ = 0.3f;
        }
    }
    drive_ = approach(drive_, bloomTarget, bloomTarget > drive_ ? 1.5f : 0.4f, dt);
    // the bloom: quickly at first, then slowing; closing slowly after the drop
    bloom_ = approach(bloom_, bloomTarget * 1.12f, bloomTarget * 1.12f > bloom_ ? 0.75f : 0.3f, dt);
    glow_ = approach(glow_, 0.62f * drive_, drive_ > glow_ ? 1.2f : 0.35f, dt);
    swell_ = approach(swell_, 0.f, 0.45f, dt);
    flurry_ = approach(flurry_, 0.f, 0.65f, dt);
    gust_ = approach(gust_, 0.f, 0.45f, dt);
    rush_ = approach(rush_, 0.f, 0.6f, dt);
    // the grass: dry and sparse when calm (and in breaks), green at mid, lush at the peak; dried out at game over
    const float dryT = over ? 1.f : 1.f - 0.65f * wm - 0.35f * wp;
    dry_ = approach(dry_, dryT, dryT > dry_ ? 0.35f : 0.7f, dt);
    grassH_ = approach(grassH_, over ? 0.4f : 0.45f + 0.35f * wm + 0.6f * wp, 0.5f, dt);
    density_ = approach(density_, over ? 0.22f : 0.22f + 0.5f * wm + 0.28f * wp, 0.5f, dt);

    const float world = 1.f - smoothstepf(0.f, 0.6f, bridge_.amount);
    // Petals on the breeze: a flurry off the opening flowers on the drop's downbeat, then a steady few.
    if (bloom_ > 0.2f && world > 0.3f && !gflowers_.empty()) {
        petalAcc_ += (130.f * flurry_ + 9.f * smoothstepf(0.4f, 1.f, bloom_)) * wdt;
        int guard = 0;
        while (petalAcc_ >= 1.f && guard++ < 40) {
            petalAcc_ -= 1.f;
            const GFlower& f = gflowers_[rng_.irange(0, (int)gflowers_.size() - 1)];
            const float o = smoothstepf(f.u, f.u + 0.1f, bloom_);
            if (o < 0.5f) continue;
            const float a = std::fabs(f.x);
            const float side = f.x < 0.f ? -1.f : 1.f;
            const float sc = 1.f + 0.8f * smoothstepf(10.f, 22.f, a);
            const float y = groundY(f.x, seedF_) + f.stem * sc * (0.5f + 0.5f * grassH_);
            Particle p{};
            p.p = v2(f.x + rng_.range(-0.1f, 0.1f), y);
            p.v = v2(side * rng_.range(0.2f, 1.1f) * (1.f + gust_) + rng_.range(-0.3f, 0.3f), rng_.range(0.5f, 1.2f) + rng_.range(1.5f, 4.5f) * gust_);
            p.life = rng_.range(6.f, 9.f); p.size = rng_.range(0.09f, 0.15f); p.grav = 0.1f; p.drag = 0.55f;
            p.rot = rng_.range(0.f, TAU); p.spin = rng_.range(-2.5f, 2.5f); p.kind = P_PETAL;
            p.col = pal_.bloom[f.color]; p.seed = rng_.uniform() * 50.f;
            parts_.push_back(p);
        }
    } else petalAcc_ = 0.f;

    // Bars at mid and peak: a seed or two drifts up from the grass.
    if (music.hasSong) {
        const double bar = std::floor(music.beat / 4.0 + 1e-4);
        if (lastBar_ < 0.0 || bar < lastBar_ - 4.0) lastBar_ = bar;
        if (bar > lastBar_) {
            lastBar_ = bar;
            if (wm > 0.5f && world > 0.5f && !over) {
                const int n = 1 + (wp > 0.5f ? 1 : 0);
                for (int i = 0; i < n; i++) {
                    const float side = rng_.chance(0.5f) ? -1.f : 1.f;
                    const float x = side * rng_.range(6.5f, viewH_.x - 1.f);
                    Particle p{};
                    p.p = v2(x, groundY(x, seedF_) + rng_.range(0.3f, 0.8f));
                    p.v = v2(side * rng_.range(0.15f, 0.4f), rng_.range(0.3f, 0.6f));
                    p.life = rng_.range(6.f, 9.f); p.size = rng_.range(0.06f, 0.09f); p.grav = -0.02f; p.drag = 0.15f;
                    p.kind = P_SEED; p.col = vec3(0.95f, 0.94f, 0.9f); p.seed = rng_.uniform() * 50.f;
                    parts_.push_back(p);
                }
            }
        }
    }
    // Kicks at the peak: pollen puffs from a few open flowers (a drop), seeds shaken from the grass (a chorus).
    sinceKick_ += dt;
    const float kick = std::max(music.kick, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    if (kick > 0.5f && prevKick_ <= 0.5f && sinceKick_ > 0.4f && wp > 0.5f && world > 0.5f && !over) {
        sinceKick_ = 0.f;
        for (int k = 0; k < 2; k++) {
            float x, y;
            vec3 col;
            if (bloom_ > 0.4f && !gflowers_.empty()) {
                const GFlower& f = gflowers_[rng_.irange(0, (int)gflowers_.size() - 1)];
                const float sc = 1.f + 0.8f * smoothstepf(10.f, 22.f, std::fabs(f.x));
                x = f.x; y = groundY(f.x, seedF_) + f.stem * sc * (0.5f + 0.5f * grassH_);
                col = lerp(pal_.center, vec3(1.f), 0.3f);
            } else {
                const float side = rng_.chance(0.5f) ? -1.f : 1.f;
                x = side * rng_.range(6.5f, viewH_.x - 1.f);
                y = groundY(x, seedF_) + 0.5f * grassH_;
                col = lerp(pal_.lush, vec3(1.f), 0.5f);
            }
            for (int i = 0; i < 4; i++) {
                Particle p{};
                p.p = v2(x + rng_.range(-0.12f, 0.12f), y + rng_.range(-0.05f, 0.1f));
                p.v = v2(rng_.range(-0.35f, 0.35f), rng_.range(0.25f, 0.7f));
                p.life = rng_.range(1.2f, 2.f); p.size = rng_.range(0.025f, 0.045f); p.grav = -0.05f; p.drag = 1.f;
                p.kind = P_POLLEN; p.col = col; p.seed = rng_.uniform();
                parts_.push_back(p);
            }
        }
    }
    prevKick_ = kick;

    // Slow motion: fireflies over the dusk garden.
    {
        int ff = 0;
        for (const Particle& p : parts_) ff += p.kind == P_FIREFLY;
        const int want = (int)(45.f * smoothstepf(0.3f, 1.f, slow_) * world);
        for (int i = ff; i < want && i < ff + 2; i++) {
            Particle p{};
            const float side = rng_.chance(0.5f) ? -1.f : 1.f;
            const float x = rng_.chance(0.25f) ? rng_.range(-5.f, 5.f) : side * rng_.range(5.5f, viewH_.x - 0.5f);
            p.p = v2(x, rng_.range(groundY(x, seedF_) + 0.3f, 2.f));
            p.v = v2(rng_.range(-0.2f, 0.2f), rng_.range(-0.1f, 0.1f));
            p.life = rng_.range(7.f, 12.f); p.size = rng_.range(0.05f, 0.07f); p.drag = 0.2f; p.kind = P_FIREFLY;
            p.col = vec3(1.f, 0.85f, 0.4f); p.seed = rng_.range(0.f, 50.f);
            parts_.push_back(p);
        }
    }

    // The particles age (a line clear's dust at the real speed).
    const float windX = 0.12f * std::sin(worldTime_ * 0.23f);
    for (Particle& p : parts_) {
        const float pdt = p.real ? dt : wdt;
        p.age += pdt;
        if (p.age < 0.f) continue;
        if (p.kind == P_FIREFLY) {
            p.v.x += std::sin(worldTime_ * 0.7f + p.seed * 3.f) * 0.15f * pdt;
            p.v.y += std::cos(worldTime_ * 0.6f + p.seed * 5.f) * 0.12f * pdt;
        }
        if (p.kind == P_PETAL || p.kind == P_SEED) p.v.x += windX * pdt;
        p.v.y -= p.grav * pdt;
        const float k = std::exp(-p.drag * pdt);
        p.v.x *= k; p.v.y *= k;
        p.p.x += p.v.x * pdt; p.p.y += p.v.y * pdt;
        p.rot += p.spin * pdt;
        // dust settles on the ground line
        if (p.kind == P_DUST) {
            const float gy = groundY(p.p.x, seedF_);
            if (p.p.y < gy) { p.p.y = gy; p.v = v2(p.v.x * 0.3f, 0.f); }
        }
    }
    parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Particle& p) { return p.age > p.life; }), parts_.end());
    for (Dying& d : dying_) d.t += dt;
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return d.t > d.delay + 0.42f; }), dying_.end());

    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
}

// =====================================================================================================
// Geometry
// =====================================================================================================

void GardenUniverse::cell(float x, float y, float size, int mask, const vec3& col, float alpha, vec2 nc, float crumble,
                          bool ghost, float seed, float moss, const float* blend, float root) {
    if (alpha <= 0.003f) return;
    cells_.push_back({{x, y, size, (float)mask}, {col.x, col.y, col.z, alpha}, {nc.x, nc.y, crumble, ghost ? 1.f : 0.f},
                      {seed, moss, root, 0.f},
                      {blend ? blend[0] : -1.f, blend ? blend[1] : -1.f, blend ? blend[2] : -1.f, blend ? blend[3] : -1.f}});
}

float GardenUniverse::packSoil(const vec3& c) const {
    // sqrt-encoded 8 bits per channel (the dark earths keep their steps fine), exact in a float
    auto q = [](float v) { return (int)std::lround(std::sqrt(std::clamp(v, 0.f, 1.f)) * 255.f); };
    return (float)(q(c.x) * 65536 + q(c.y) * 256 + q(c.z));
}

void GardenUniverse::sprite(float x, float y, float hx, float hy, float rot, int kind, const vec3& col, float alpha,
                            float p0, float p1, float p2, float p3, float seed) {
    if (alpha <= 0.003f) return;
    sprites_.push_back({{x, y, hx, hy}, {rot, (float)kind, p0, p1}, {col.x, col.y, col.z, alpha}, {p2, p3, seed, 0.f}});
}

void GardenUniverse::flushCells() {
    if (cells_.empty()) return;
    glUseProgram(progCell_);
    setMat(progCell_, "uVP", vp_);
    set1f(progCell_, "uPx", px_);
    const float bb = smoothstepf(0.3f, 0.95f, bridge_.amount);
    set1f(progCell_, "uBridge", bb);
    set3f(progCell_, "uBridgeInk", bridge_.ink);
    set3f(progCell_, "uMoss", pal_.moss);
    glBindVertexArray(cellVao_);
    glBindBuffer(GL_ARRAY_BUFFER, cellVbo_);
    glBufferData(GL_ARRAY_BUFFER, cells_.size() * sizeof(CellInst), cells_.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)cells_.size());
    cells_.clear();
}

void GardenUniverse::flushSprites() {
    if (sprites_.empty()) return;
    glUseProgram(progSprite_);
    setMat(progSprite_, "uVP", vp_);
    set3f(progSprite_, "uCenter", pal_.center);
    set3f(progSprite_, "uRing", pal_.ink);
    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glBufferData(GL_ARRAY_BUFFER, sprites_.size() * sizeof(SpriteInst), sprites_.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)sprites_.size());
    sprites_.clear();
}

// The ground line's plants: blades of grass (dry stems when calm), the flowers of a drop.
void GardenUniverse::drawGround(const MusicState& music, float world) {
    (void)music;
    if (world <= 0.01f) return;
    const float wp = smoothstepf(1.1f, 2.f, lv_);
    const float dusk = 1.f - 0.5f * slow_;
    const float minW = px_ * 0.7f;
    for (const Blade& b : blades_) {
        const float a = std::fabs(b.x);
        // short and tidy under the well, taller toward the edges of the picture
        const float hs = lerpf(0.2f, 1.f, smoothstepf(5.2f, 6.6f, a));
        const float side = 1.f + 0.8f * smoothstepf(10.f, 22.f, a);
        const bool stem = b.h1 < 0.14f;   // the stems that stay standing when everything is dry
        const float vis = stem ? 1.f : smoothstepf(density_, density_ - 0.06f, b.h1);
        if (vis <= 0.f) continue;
        float len = (0.25f + 0.75f * b.h2) * grassH_ * hs * side;
        if (stem) len *= 1.f + 1.4f * dry_;
        const float y0 = groundY(b.x, seedF_) - 0.05f;
        // under the HUD columns, the plants stay below the text
        if (b.x > layout::LEFT_COLUMN[0] - 0.5f && b.x < layout::LEFT_COLUMN[2] + 0.5f) len = std::min(len, layout::LEFT_COLUMN[1] - 0.6f - y0);
        if (b.x > layout::RIGHT_COLUMN[0] - 0.5f && b.x < layout::RIGHT_COLUMN[2] + 0.5f) len = std::min(len, layout::RIGHT_COLUMN[1] - 0.6f - y0);
        len = plantCap(b.x, y0, len);
        if (len <= 0.02f) continue;
        vec3 col = lerp(pal_.grass, pal_.lush, saturate(b.h3 * 0.6f + 0.6f * wp));
        col = lerp(col, pal_.dry * (0.85f + 0.3f * b.h4) * (stem ? 0.55f : 1.f), saturate(dry_ * (0.65f + 0.35f * b.h3) + (stem ? 0.35f * dry_ : 0.f)));
        col = col * ((0.78f + 0.4f * b.h4) * dusk);
        float w = (stem ? 0.03f : 0.034f) * side;
        float alpha = world * vis;
        if (w < minW) { alpha *= w / minW; w = minW; }
        const float rot = (b.h4 - 0.5f) * 0.4f + sway(b.x, b.h1) * (0.6f + 0.6f * b.h2);
        const float bend = (b.h3 - 0.5f) * 0.35f;
        sprite(b.x, y0, w, len, rot, 0, col, alpha, bend);
        // dry stems carry a seed head
        if (stem && dry_ > 0.4f) {
            const float c = std::cos(rot), s = std::sin(rot);
            const float tx = bend * len, ty = len;
            sprite(b.x + c * tx - s * ty, y0 + s * tx + c * ty, 0.035f * side, 0.06f * side, rot, 6, pal_.dry * 0.8f * dusk,
                   alpha * smoothstepf(0.4f, 0.8f, dry_));
        }
    }
    // The drop's flowers.
    if (bloom_ > 0.01f) {
        for (GFlower& f : gflowers_) {
            const float stemG = smoothstepf(f.u - 0.22f, f.u, bloom_);
            if (stemG <= 0.f) continue;
            const float o = smoothstepf(f.u, f.u + 0.1f, bloom_);
            const float a = std::fabs(f.x);
            const float side = 1.f + 0.8f * smoothstepf(10.f, 22.f, a);
            const float y0 = groundY(f.x, seedF_) - 0.05f;
            float len = f.stem * side * (0.5f + 0.5f * grassH_) * stemG;
            if (f.x > layout::LEFT_COLUMN[0] - 0.5f && f.x < layout::LEFT_COLUMN[2] + 0.5f) len = std::min(len, layout::LEFT_COLUMN[1] - 0.8f - y0);
            len = plantCap(f.x, y0, len + f.head * side * 1.2f) - f.head * side * 1.2f;
            if (len <= 0.02f) continue;
            const float rot = f.lean + sway(f.x, f.seed) * 1.3f;
            const vec3 stemCol = pal_.lush * 0.8f * dusk;
            sprite(f.x, y0, std::max(0.018f * side, px_ * 0.6f), len, rot, 0, stemCol, world, f.lean * 0.4f);
            const float c = std::cos(rot), s = std::sin(rot);
            const float tx = f.lean * 0.4f * len, ty = len;
            const float hx = f.x + c * tx - s * ty, hy = y0 + s * tx + c * ty;
            const vec3 col = pal_.bloom[f.color] * dusk;
            if (o < 0.2f) sprite(hx, hy, f.head * 0.35f * side, f.head * 0.55f * side, rot, 6, col, world * stemG);
            else sprite(hx, hy, f.head * side * (0.6f + 0.4f * o), f.head * side * (0.6f + 0.4f * o), rot + f.seed, 2, col, world,
                        o, (float)f.petals);
            // a flower closing as the drop ends lets its petals go
            if (!f.open && o > 0.9f) f.open = true;
            else if (f.open && o < 0.5f) {
                f.open = false;
                for (int i = 0; i < 2 && world > 0.5f; i++) {
                    Particle p{};
                    p.p = v2(hx, hy);
                    p.v = v2(rng_.range(-0.4f, 0.4f), rng_.range(0.f, 0.3f));
                    p.life = rng_.range(2.f, 3.5f); p.size = f.head * 0.5f * side; p.grav = 0.35f; p.drag = 0.8f;
                    p.rot = rng_.range(0.f, TAU); p.spin = rng_.range(-2.f, 2.f); p.kind = P_PETAL; p.col = pal_.bloom[f.color];
                    p.seed = rng_.uniform() * 50.f;
                    parts_.push_back(p);
                }
            }
        }
    }
}

// A plant at x rooted at y0 stays below the HUD letters near the ground line.
float GardenUniverse::plantCap(float x, float y0, float len) const {
    for (const KeepOut& k : keepOut_)
        if (x > k.x0 && x < k.x1 && k.y1 > y0) len = std::min(len, std::max(0.f, k.y0 - 0.15f - y0));
    return len;
}

// Grass and flowers on the open tops of the stack.
void GardenUniverse::drawStackGrowth(const Game& g, float world) {
    if (world <= 0.01f) return;
    const float dusk = 1.f - 0.4f * slow_;
    const float minW = px_ * 0.7f;
    auto growth = [&](float cx, float top, float age, float seed, float fold, const vec3& foldCol, bool lOpen, bool rOpen,
                      float alpha) {
        const float gr = smoothstepf(0.1f, 1.6f, age);
        if (gr <= 0.f || alpha <= 0.f) return;
        const float lo = lOpen ? -0.33f : -0.5f, hi = rOpen ? 0.33f : 0.5f;
        const float fk = smoothstepf(0.f, 1.f, fold);
        const float keep = 1.f - fk;
        const int N = 9;
        for (int i = 0; i < N; i++) {
            const float h1 = hashf(seed, (float)i), h2 = hashf(seed, i + 20.f), h3 = hashf(seed, i + 40.f);
            const float bx = cx + lo + (hi - lo) * (i + 0.5f + (h1 - 0.5f) * 0.7f) / N;
            const float len = (0.11f + 0.18f * h2) * gr * (0.25f + 0.75f * keep);
            vec3 col = lerp(pal_.grass, pal_.lush, h3);
            col = lerp(col, pal_.dry, 0.3f * dry_);
            col = lerp(col * ((0.8f + 0.35f * h1) * dusk), foldCol, fk);
            // folding back: the blades lie down and sink into the soil
            const float dir = h3 < 0.5f ? -1.f : 1.f;
            const float rot = (h3 - 0.5f) * 0.6f + sway(bx, h1) * 0.5f + dir * 1.2f * fk;
            float w = 0.026f;
            float a = alpha * (1.f - fk * fk * fk);
            if (w < minW) { a *= w / minW; w = minW; }
            sprite(bx, top - 0.03f, w, len, rot, 0, col, a, (h1 - 0.5f) * 0.3f);
        }
        // a few flowers per open run, opening slowly over about twelve seconds
        if (hashf(seed, 99.f) < 0.45f) {
            const float start = 1.5f + 6.5f * hashf(seed, 97.f);
            const float op = smoothstepf(start, start + 3.5f, age);
            const float stemG = smoothstepf(start - 1.f, start + 1.2f, age);
            if (stemG > 0.f) {
                const float fx = cx + lo + 0.1f + (hi - lo - 0.2f) * hashf(seed, 98.f);
                const float len = (0.24f + 0.1f * hashf(seed, 95.f)) * stemG * keep;
                const float rot = (hashf(seed, 94.f) - 0.5f) * 0.3f + sway(fx, seed) * 0.6f;
                const float c = std::cos(rot), s = std::sin(rot);
                const vec3 stemCol = lerp(pal_.lush * 0.85f * dusk, foldCol, fk);
                const float a = alpha * (1.f - fk * fk);
                sprite(fx, top - 0.03f, std::max(0.016f, minW), len + 0.03f, rot, 0, stemCol, a * (0.016f < minW ? 0.016f / minW : 1.f));
                const float hx = fx - s * (len + 0.03f), hy = top - 0.03f + c * (len + 0.03f);
                const vec3 col = lerp(pal_.flower[(int)(hashf(seed, 96.f) * 2.999f)] * dusk, foldCol, fk);
                const float hs = keep;
                if (op < 0.22f) sprite(hx, hy, 0.04f * stemG * hs, 0.06f * stemG * hs, rot, 6, col, a);
                else {
                    const float r = (0.11f + 0.04f * hashf(seed, 93.f)) * (0.6f + 0.4f * op) * hs;
                    sprite(hx, hy, r, r, rot + seed, 2, col, a, op, (float)(5 + (int)(hashf(seed, 92.f) * 3.f)));
                }
            }
        }
    };
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Soil& s = soil_[y][x];
            if (s.id < 0 || s.age <= 0.f || s.cover >= 1.f) continue;
            const float off = g.rowOffset(y);
            const vec3 p = layout::cellPos((float)x, y - off);
            const bool lOpen = x == 0 || soil_[y][x - 1].id < 0;
            const bool rOpen = x == Game::W - 1 || soil_[y][x + 1].id < 0;
            const vec3 foldCol = s.coverType >= 0 ? pal_.piece[s.coverType] : pal_.piece[s.type];
            growth(p.x, p.y + 0.5f - 0.05f, s.age, s.seed, s.cover, foldCol, lOpen, rOpen, world);
        }
    // a crumbling row's grass goes with it
    for (const Dying& d : dying_) {
        if (d.age <= 0.f) continue;
        const float k = smoothstepf(0.f, 0.25f, d.t - d.delay);
        growth(d.x, d.y + 0.5f - 0.05f, d.age, d.seed, 0.f, pal_.piece[d.type], !(d.mask & 1), !(d.mask & 2), world * (1.f - k));
    }
}

void GardenUniverse::drawBoard(const Game& g, float world) {
    (void)world;
    // The ghost: a faint shadow of the clod on the ground where it will land.
    if (g.hasPiece() && !g.over()) {
        const Piece& p = g.piece();
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
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
                cell(w.x, w.y, 1.f, m, lerp(pal_.piece[std::clamp(p.type, 0, 6)], pal_.groundDeep, 0.5f), 0.16f, v2(0, 0), 0.f, true, 0.f, 0.f);
            }
        }
    }
    // The stack.
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Soil& s = soil_[y][x];
            if (s.id < 0) continue;
            const float off = g.rowOffset(y);
            int m = maskOf(x, y);
            // rows still falling into place do not join the rows around them
            for (int i = 0; i < 8; i++) {
                const int ny = y + NDY[i];
                if ((m & (1 << i)) && ny >= 0 && ny < Game::H && std::fabs(g.rowOffset(ny) - off) > 0.02f) m &= ~(1 << i);
            }
            const vec3 p = layout::cellPos((float)x, y - off);
            const float moss = s.cover < 1.f ? growthAt(s) * (1.f - s.cover) : 0.f;
            // the soils of the clods around (L R D U) blend into this one across the joins
            float blend[4];
            for (int i = 0; i < 4; i++)
                blend[i] = (m & (1 << i)) && y + NDY[i] < Game::H ? packSoil(pal_.piece[soil_[y + NDY[i]][x + NDX[i]].type]) : -1.f;
            // the bottom row grows out of the ground: joined below (no rim, no rounded corners), its earth mixing
            // into the ground's
            const bool root = y == Game::H - 1 && std::fabs(off) < 0.02f;
            if (root) {
                m |= (1 << 2) | (1 << 4) | (1 << 5);
                blend[2] = packSoil(pal_.ground);
            }
            cell(p.x, p.y, 1.f, m, pal_.piece[s.type], 1.f, s.nc, 0.f, false, (float)(s.id % 97), moss, blend,
                 root ? ROOT : 0.f);
        }
    // Cleared earth crumbling (at the real speed, even in slow motion).
    for (const Dying& d : dying_) {
        const float k = smoothstepf(0.f, 0.36f, d.t - d.delay);
        cell(d.x, d.y - 0.12f * k * k, 1.f, d.mask, pal_.piece[d.type], 1.f, d.nc, std::max(0.001f, k), false, d.seed, 0.f);
    }
}

void GardenUniverse::drawParticles(bool front, float world) {
    const float dusk = 1.f - 0.4f * slow_;
    for (const Particle& p : parts_) {
        if (p.front != front || p.age < 0.f) continue;
        const float t = p.age / p.life;
        float a = (p.kind == P_DUST || p.kind == P_FLOWER) ? 1.f - smoothstepf(0.55f, 1.f, t)
                                                           : smoothstepf(0.f, 0.12f, t) * (1.f - smoothstepf(0.7f, 1.f, t));
        if (!front) {
            a *= world;
            // behind the well, the air's things stay faint
            if (std::fabs(p.p.x) < WELL_X + 0.3f && p.p.y < WELL_Y && p.p.y > -10.f) a *= 0.35f;
        }
        switch (p.kind) {
        case P_DUST: sprite(p.p.x, p.p.y, p.size, p.size, 0.f, 1, p.col, a * 0.9f, 0.35f); break;
        case P_POLLEN: sprite(p.p.x, p.p.y, p.size, p.size, 0.f, 1, p.col * dusk, a * 0.85f, 0.1f); break;
        case P_PETAL: {
            const float flip = 0.45f + 0.55f * std::fabs(std::cos(p.age * 2.3f + p.seed));
            sprite(p.p.x, p.p.y, p.size * 0.6f * flip, p.size, p.rot, 3, p.col * dusk, a * 0.95f);
            break;
        }
        case P_SEED: sprite(p.p.x, p.p.y, p.size, p.size, p.rot + std::sin(p.age * 0.8f + p.seed) * 0.3f, 4, p.col * dusk, a * 0.8f); break;
        case P_FIREFLY: {
            const float blink = 0.5f + 0.5f * std::sin(p.age * 1.1f + p.seed * 3.f);
            const float b = a * blink * blink;
            sprite(p.p.x, p.p.y, p.size * 6.f, p.size * 6.f, 0.f, 1, p.col, b * 0.2f, 0.f);
            sprite(p.p.x, p.p.y, p.size * 1.3f, p.size * 1.3f, 0.f, 1, vec3(1.f, 0.93f, 0.6f), b, 0.35f);
            break;
        }
        case P_FLOWER: sprite(p.p.x, p.p.y, p.size, p.size, p.rot, 2, p.col, a, p.seed, 5.f); break;
        default: break;
        }
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float GardenUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
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

float GardenUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
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

void GardenUniverse::drawText(const Font& f, const std::vector<float>& q, const vec3& ink, float alpha, float bias) {
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

// The HUD in GARDEN's typography: Lora italic for the song, the garden's name, the section and the celebrations,
// like a label on a seed packet; spaced small capitals for labels; the numbers in the medium roman. All in the
// garden's own ink, straight on the sky.
void GardenUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
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
    // the sky's ink follows the sky behind the letters: dark on a pale sky, pale on a dark one
    const bool inkDark = ugl::luma(pal_.ink) < 0.2f;
    const vec3 darkInk = inkDark ? pal_.ink : ugl::toLinear(hex(0x2c2430));
    const vec3 lightInk = inkDark ? ugl::toLinear(hex(0xf4efe4)) : pal_.ink;
    auto skyLumaAt = [&](float by) {
        const float hh = saturate((by + 10.f) / 23.f);
        const vec3 sky = lerp(pal_.skyLow, pal_.skyTop, smoothstepf(0.f, 1.f, std::pow(hh, 0.75f)));
        return ugl::luma(lerp(sky, pal_.dusk, 0.85f * slow_));
    };
    const vec3 earthInk = lerp(pal_.dry, vec3(1.f), 0.45f) * (1.f - 0.3f * slow_);
    std::vector<float> q;
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        // letters over the ground line are set in the earth, pale like dry grass; the others in the sky's ink
        auto inkAt = [&](float px, float py) {
            const float bx = viewC_.x + (px / width_ * 2.f - 1.f) * viewH_.x;
            const float by = viewC_.y - (py / height_ * 2.f - 1.f) * viewH_.y;
            if (by < groundY(bx, seedF_) - 0.1f) return earthInk;
            return skyLumaAt(by) > 0.3f ? darkInk : lightInk;
        };
        vec3 ink = inkAt(h.x < 0.f ? width_ + h.x : h.x, h.y + base * 0.5f);
        vec3 accent = lerp(pal_.accent, ink, 0.2f);
        const float alpha = saturate(h.alpha);
        q.clear();
        auto place = [&](const Font& f, const std::string& text, float size, float track, float dy = 0.f) {
            const float w = textWidth(f, text, size, track);
            const float x = ugl::hudLeft(h, w, width_);
            ink = inkAt(x + w * 0.5f, h.y + dy + size * 0.8f);
            accent = lerp(pal_.accent, ink, 0.2f);
            textQuads(f, text, x, h.y + dy, size, track, q);
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            // "Garden" in the italic, the variant's name in small spaced capitals after it.
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

void GardenUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const Palette target = paletteOf(variant_);
        if (!palSet_) { pal_ = target; palSet_ = true; }
        else lerpPalette(pal_, target, 1.f - std::exp(-dt * 1.0f));
    }
    // the HUD letters in the lower part of the picture: the plants keep below them
    keepOut_.clear();
    for (const HudText& h : f.hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        const float w = (float)h.text.size() * base * 0.75f;
        const float x0 = ugl::hudLeft(h, w, width_), y0 = h.y - base * 0.3f, y1 = h.y + base * 1.6f;
        const float bx0 = viewC_.x + (x0 / width_ * 2.f - 1.f) * viewH_.x, bx1 = viewC_.x + ((x0 + w) / width_ * 2.f - 1.f) * viewH_.x;
        const float byTop = viewC_.y - (y0 / height_ * 2.f - 1.f) * viewH_.y, byBot = viewC_.y - (y1 / height_ * 2.f - 1.f) * viewH_.y;
        if (byBot > -4.f) continue;
        keepOut_.push_back({bx0 - 0.3f, bx1 + 0.3f, byBot, byTop});
    }
    // the section, from the HUD (a drop blooms, a chorus stays green)
    sectionKnown_ = false;
    for (const HudText& h : f.hud)
        if (h.role == HUD_SECTION) {
            sectionKnown_ = true;
            sectionDrop_ = h.text == "DROP";
            sectionChorus_ = h.text == "CHORUS";
        }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 0.9f, dt);
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.5f : 0.f), 3.f, dt);
    if (overT_ >= 0.f) {
        if (!game.over()) overT_ = -1.f;
        else overT_ += dt;
    }
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.4f, slow_);
    if (!f.paused) {
        simulate(game, music, dt, wdt);
        syncSoil(game, wdt);
    }
    const float bridge = saturate(bridge_.amount);
    const float world = 1.f - smoothstepf(0.f, 0.6f, bridge);

    sceneT_.bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    // ---- The sky and the ground line.
    {
        glUseProgram(progSky_);
        set4f(progSky_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set3f(progSky_, "uSkyTop", pal_.skyTop);
        set3f(progSky_, "uSkyLow", lerp(pal_.skyLow, pal_.skyLow * vec3(0.95f, 0.93f, 0.9f), 0.5f * dry_ * (1.f - pal_.stars)));
        set3f(progSky_, "uSunCol", pal_.sun);
        set3f(progSky_, "uGlow", pal_.glow);
        set3f(progSky_, "uGround", pal_.ground);
        set3f(progSky_, "uGroundDeep", pal_.groundDeep);
        set3f(progSky_, "uLip", pal_.lip);
        set3f(progSky_, "uDusk", pal_.dusk);
        set3f(progSky_, "uBridgeCol", bridge_.color);
        set4f(progSky_, "uSun", pal_.sunX, pal_.sunY, pal_.sunR, pal_.moon);
        set1f(progSky_, "uGlowAmt", (glow_ + swell_) * world);
        set1f(progSky_, "uSlow", slow_);
        set1f(progSky_, "uBridge", smoothstepf(0.05f, 0.85f, bridge));
        set1f(progSky_, "uSeed", seedF_);
        set1f(progSky_, "uPx", px_);
        set1f(progSky_, "uStars", pal_.stars);
        set1f(progSky_, "uVeil", pal_.veil * (1.f - bridge_.amount));
        set1f(progSky_, "uDry", dry_);
        set1f(progSky_, "uHaze", smoothstepf(0.4f, 1.f, dry_) * (1.f - pal_.stars * 0.6f));
        set1f(progSky_, "uLush", smoothstepf(1.1f, 2.f, lv_) * (1.f - drive_));
        ugl::drawFullscreen();
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // ---- The ground line's plants and the air's things (behind the stack).
    drawGround(music, world);
    drawParticles(false, world);
    flushSprites();
    // ---- The stack, its growth, the falling piece.
    drawBoard(game, world);
    flushCells();
    drawStackGrowth(game, world);
    flushSprites();
    if (game.hasPiece() && !game.over()) {
        const Piece& p = game.piece();
        int cells[4][2];
        game.pieceCells(p, cells);
        auto inPiece = [&](int x, int y) {
            for (auto& c : cells)
                if (c[0] == x && c[1] == y) return true;
            return false;
        };
        int minX = 99, minY = 99;
        for (auto& c : cells) { minX = std::min(minX, c[0]); minY = std::min(minY, c[1]); }
        const float dx = game.pieceVisualX() - p.x, dy = game.pieceVisualY() - p.y;
        const int type = std::clamp(p.type, 0, 6);
        const float sx = type * 5.1f + (float)(nextId_ % 17) * 3.7f, sy = type * 2.3f + (float)(nextId_ % 13) * 1.9f;
        for (auto& c : cells) {
            if (c[1] < Game::HIDDEN - 1) continue;
            int m = 0;
            for (int i = 0; i < 8; i++)
                if (inPiece(c[0] + NDX[i], c[1] + NDY[i])) m |= 1 << i;
            const vec3 w = layout::cellPos(c[0] + dx, c[1] + dy);
            cell(w.x, w.y, 1.f, m, pal_.piece[type], 1.f, v2((float)(c[0] - minX) + sx, -(float)(c[1] - minY) + sy), 0.f, false,
                 (float)(nextId_ % 97), 0.f);
        }
    }
    // ---- Previews: small clods.
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
        auto inPiece = [&](int x, int y) {
            for (auto& c : cells)
                if (c[0] == x && c[1] == y) return true;
            return false;
        };
        for (auto& c : cells) {
            int m = 0;
            for (int i = 0; i < 8; i++)
                if (inPiece(c[0] + NDX[i], c[1] + NDY[i])) m |= 1 << i;
            const vec2 pos = v2(center.x + (c[0] - (minx + maxx) * 0.5f) * sc, center.y - (c[1] - (miny + maxy) * 0.5f) * sc);
            cell(pos.x, pos.y, sc, m, pal_.piece[std::clamp(type, 0, 6)], alpha, v2(c[0] + type * 5.1f, -(float)c[1] + type * 2.3f),
                 0.f, false, (float)type, 0.f);
        }
    };
    preview(game.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), game.holdUsed() ? 0.35f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(game.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }
    flushCells();
    // ---- The bonus gauge: a fine ring, a meadow rising inside, a flower opening in it when it is ready.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        const float fill = game.bonusActive() ? saturate(game.bonusLeft() / 10.f) : saturate(gaugeShown_);
        const float ready = game.bonusReady() || game.bonusActive() ? 1.f : 0.f;
        sprite(gc.x, gc.y, R, R, 0.f, 5, lerp(pal_.lush, pal_.grass, 0.3f), 0.85f * (1.f - 0.6f * smoothstepf(0.3f, 0.95f, bridge)),
               fill, ready, 0.f, 0.f, worldTime_ * 0.5f);
        if (ready > 0.f) sprite(gc.x, gc.y + 0.1f, 0.34f, 0.34f, worldTime_ * 0.05f, 2, pal_.flower[0], world, 1.f, 6.f);
    }
    // ---- Dust of the stack itself (crumbling rows, landings, falling flowers).
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

const UniverseEntry& universe_garden() {
    static const UniverseEntry e{
        "GARDEN", "clods of earth that grow grass and flowers", [] { return std::unique_ptr<Universe>(new GardenUniverse()); },
        // Warm, organic songs suit the garden best (mid tempo, acoustic air, a drop to bloom on), any of them a little.
        [](const SongLook& l) {
            return 0.7f + 0.25f * l.airWeight + 0.2f * l.dynamics + 0.15f * (1.f - smoothstepf(130.f, 170.f, l.bpm));
        },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
