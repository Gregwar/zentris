// LIGHTSOURCE: a dim room where the falling piece is the light.
//
// One world: a dark plaster wall, a floor under the well and two thin rails marking its sides, lit by a low
// ambient (the stack, its holes and the well always read) and, above all, by the pieces themselves. The falling
// piece glows and lights everything around it: an area light (its four cells) with a soft falloff and real shadows
// cast in the board's plane: the stack's blocks shadow the wall and the floor behind them, light pours down an open
// column, the holes under an overhang stay in the dark, the shadows of the stack stretch across the wall as the
// piece moves. The blocks facing the light catch it on their bevels, their far faces stay dark.
//
// When a piece locks it keeps shining for a few seconds (a small flare where it touched down), then dims smoothly
// (its color cooling, nothing blinks) while the next piece already falls and lights the room: two or three lights at
// once for a while. The cells keep a faint ember for a while after.
//
// Staging: a break dims the room and softens the light (wide penumbrae, short reach); mid is the plain light; a
// chorus brings a slow side light sweeping its long shadows across the wall and embers twinkling on the beat; a
// drop hardens the light (crisp shadows), brightens and recolors it, lights the side lamps on both sides (their
// shadows slowly swinging) and relights the whole stack: every piece a light again (a ripple up from the bottom on its
// first downbeat, then each piece shimmering at its own pace and a few swelling in turn every two beats; the five
// brightest cast shadows, the others glow), back to embers when it ends. Nothing pulses the whole room. A cleared row's blocks flare into white light and burn out (their light flooding sideways
// out of the row, at the real speed even in slow motion), a Tetris relights the whole stack from inside, slow motion
// is a cold light with the lamps lingering, game over puts the stack's lights out row by row from the top, each block fading.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane (like PRISM).
#include "universes/lightsource/lightsource.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/lightsource/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Sora, SIL Open Font License, see OFL-Sora.txt.
const unsigned char FONT_SORA[] = {
#include "universes/lightsource/Sora.ttf.inc"
};

namespace shaders = lightsource_shaders;
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
float lum(const vec3& c) { return 0.3f * c.x + 0.5f * c.y + 0.2f * c.z; }
vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }
vec3 mulv(const vec3& a, const vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

// The variants: the room (wall, floor, rails), its ambient fill, the lamps' light (and how much each piece tints
// it), the drop's light, the HUD's text and accent, and the seven pieces (I O T S Z J L).
// Each variant's pieces are a two-color palette: side A (I T Z L) in one hue, side B (O J S) in the other, the
// pieces of a side told apart by their value (SHADE below).
struct VariantDef {
    const char* name;
    unsigned wall, floor, rail, ambient, light, dropLight, text, accent;
    float tintK;
    unsigned piece[7];
};
const VariantDef VARIANTS[] = {
    // warm against cold: amber lamps and moonlit blues in a cold room
    {"TUNGSTEN", 0x6e6052, 0x4a4038, 0x8a7a68, 0x6f85b0, 0xffc890, 0xd2e2ff, 0xeee2d2, 0xffc27a, 0.7f,
     {0xffd08a, 0xb4d8ff, 0xf0a050, 0x4c70c4, 0xcc7632, 0x76a0ec, 0xb4682e}},
    // neon tubes in a concrete room: hot magenta against cyan (complementary)
    {"NEON", 0x48505e, 0x30353f, 0x6a7486, 0x5c6cb4, 0xffffff, 0xff7ae0, 0xdfe6f4, 0x7ae8ff, 0.85f,
     {0xff8ee8, 0xa0f4ff, 0xff40c8, 0x1c8ed0, 0xd01aa0, 0x30d6ff, 0xb01c8c}},
    // candlelight: the flame's orange against a cool sea green, in a brown room
    {"CANDLE", 0x6a5040, 0x45342a, 0x86684e, 0x9a6a48, 0xffa848, 0xff7038, 0xf2dcc4, 0xffb060, 0.6f,
     {0xffc070, 0xa2e2cc, 0xf08a3c, 0x3a907e, 0xd05c2a, 0x56b6a2, 0xb44c28}},
    // silver: pale blush against pale ice, in a grey room under a white light
    {"SILVER", 0x5c5f64, 0x3c3e42, 0x7c8088, 0x8890a4, 0xf4f6ff, 0xa8c4ff, 0xe8eaee, 0xffffff, 0.4f,
     {0xfbe2d8, 0xdeeefa, 0xecb2a4, 0x7a98be, 0xcc8c80, 0xa8c6e6, 0xb07a72}},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

constexpr float FLOOR_Y = -10.06f;
constexpr float EMIT = 0.85f;
constexpr float FADE = 1.8f; // a lamp dimming to its ember
// Which side of the palette each piece type is on (I O T S Z J L): 0 = A, 1 = B.
const int SIDE[7] = {0, 1, 0, 1, 0, 1, 0};
// A drop's two colors: a designed pair of saturated fluorescent hues (linear), a new pair each drop.
// magenta, acid green, cyan, hot orange, UV violet, laser yellow, hot pink, electric blue
const vec3 FLUO[] = {{1.f, 0.f, 0.62f}, {0.42f, 1.f, 0.f}, {0.f, 0.92f, 1.f}, {1.f, 0.28f, 0.f},
                     {0.5f, 0.f, 1.f}, {1.f, 0.9f, 0.f}, {1.f, 0.f, 0.22f}, {0.f, 0.38f, 1.f}};
const int DROP_PAIRS[][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 2}, {7, 3}, {1, 4}, {5, 0}, {3, 7}, {2, 0}, {6, 1}};
constexpr int DROP_PAIR_COUNT = (int)(sizeof(DROP_PAIRS) / sizeof(DROP_PAIRS[0]));
// each piece type's value within its side (I O T S Z J L), and how much it whitens at full light
const float SHADE[7] = {1.f, 1.f, 0.74f, 0.42f, 0.52f, 0.68f, 0.34f};
const float SHADE_WHITE[7] = {0.12f, 0.1f, 0.f, 0.f, 0.f, 0.f, 0.f}; // (HDR, before the shoulder)

float hashf(float x) {
    const float s = std::sin(x * 127.1f + 311.7f) * 43758.5453f;
    return s - std::floor(s);
}

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool LightsourceUniverse::init(int width, int height) {
    progLight_ = compile(ugl::FULLSCREEN_VS, shaders::LIGHT_FS, "lightsource light");
    progScene_ = compile(ugl::FULLSCREEN_VS, shaders::SCENE_FS, "lightsource scene");
    progBlock_ = compile(shaders::BLOCK_VS, shaders::BLOCK_FS, "lightsource block");
    progFx_ = compile(shaders::FX_VS, shaders::FX_FS, "lightsource fx");
    progFinal_ = compile(ugl::FULLSCREEN_VS, shaders::FINAL_FS, "lightsource final");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "lightsource text");
    progPanel_ = compile(shaders::PANEL_VS, shaders::PANEL_FS, "lightsource panel");

    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glGenVertexArrays(1, &instVao_);
    glGenBuffers(1, &instVbo_);
    glBindVertexArray(instVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(1 + i);
        glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(Inst), (void*)(sizeof(float) * 4 * i));
        glVertexAttribDivisor(1 + i, 1);
    }
    glGenVertexArrays(1, &textVao_);
    glGenBuffers(1, &textVbo_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)(sizeof(float) * 2));
    glGenVertexArrays(1, &panelVao_);
    glGenBuffers(1, &panelVbo_);
    glBindVertexArray(panelVao_);
    glBindBuffer(GL_ARRAY_BUFFER, panelVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) { ids_[y][x] = 0; lockAt_[y][x] = -99.f; glint_[y][x] = 0.f; }
    buildFont(font_, FONT_SORA);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

void LightsourceUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] lightsource font init failed\n");
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

void LightsourceUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    const float aspect = (float)width_ / height_;
    const layout::Camera cam = layout::camera(width_, height_);
    const float hh = cam.dist * std::tan(cam.fovDeg * 0.5f * PI / 180.f), hw = hh * aspect;
    viewH_ = v2(hw, hh);
    viewC_ = v2(cam.target.x, cam.target.y);
    vp_ = mat4::ortho(viewC_.x - hw, viewC_.x + hw, viewC_.y - hh, viewC_.y + hh, -1.f, 1.f);
    px_ = 2.f * hh / height_;
    // the light buffer: half resolution, at most 540 lines (the shadows are soft, the cost is per pixel)
    const float ls = std::min(0.5f, 540.f / height_);
    lightW_ = std::max(1, (int)std::lround(width_ * ls));
    lightH_ = std::max(1, (int)std::lround(height_ * ls));
    if (lightFbo_) glDeleteFramebuffers(1, &lightFbo_);
    if (lightTex_) glDeleteTextures(1, &lightTex_);
    if (dirTex_) glDeleteTextures(1, &dirTex_);
    glGenFramebuffers(1, &lightFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, lightFbo_);
    GLuint* texs[2] = {&lightTex_, &dirTex_};
    for (int i = 0; i < 2; i++) {
        glGenTextures(1, texs[i]);
        glBindTexture(GL_TEXTURE_2D, *texs[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, lightW_, lightH_, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, *texs[i], 0);
    }
    const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, bufs);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    sceneT_.destroy();
    sceneT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
}

LightsourceUniverse::~LightsourceUniverse() {
    sceneT_.destroy();
    if (lightFbo_) glDeleteFramebuffers(1, &lightFbo_);
    if (lightTex_) glDeleteTextures(1, &lightTex_);
    if (dirTex_) glDeleteTextures(1, &dirTex_);
    for (GLuint p : {progLight_, progScene_, progBlock_, progFx_, progFinal_, progText_, progPanel_}) glDeleteProgram(p);
    for (GLuint v : {instVao_, textVao_, panelVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, instVbo_, textVbo_, panelVbo_}) glDeleteBuffers(1, &b);
    if (font_.tex) glDeleteTextures(1, &font_.tex);
    delete (stbtt_fontinfo*)font_.info;
}

// =====================================================================================================
// Song, level
// =====================================================================================================

void LightsourceUniverse::setSong(const SongLook& t, float seconds) {
    if (t.variant >= 0) variant_ = t.variant % VARIANT_COUNT;
    else {
        // warm songs (major, bright) get the tungsten or the candle, night songs the neon or the silver
        int v;
        const bool night = t.minor && t.brightness < 0.5f;
        if (night) v = (t.bpm > 118.f || t.airWeight > 0.55f) ? 1 : 3;
        else v = (t.brightness > 0.55f || t.bassWeight < 0.45f) ? 0 : 2;
        if ((t.seed % 5) == 0) v = (v + 1 + (int)((t.seed >> 8) % 3)) % VARIANT_COUNT;
        if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
        variant_ = v;
    }
    rng_ = Rng(t.seed ^ 0x11647ull);
    songSeed_ = t.seed;
    dropIndex_ = 0;
    if (seconds <= 0.f) palSet_ = false;
}

void LightsourceUniverse::setLevel(int level, float, float seconds, float impact) {
    level = std::clamp(level, 0, 2);
    if (level < 2) inDrop_ = false;
    if (!hasLevel_ || seconds < 0.1f) {
        hasLevel_ = true;
        level_ = level;
        dropWait_ = 0.f;
        lv_ = lvFrom_ = lvTo_ = (float)level;
        lvT_ = 1.f;
        return;
    }
    const bool rising = level > level_;
    if (rising && level >= 2) dropWait_ = 1.f; // the peak is announced just before its downbeat: wait for drop()
    level_ = level;
    lvFrom_ = lv_;
    lvTo_ = (float)level;
    lvT_ = 0.f;
    lvDur_ = std::clamp(seconds, rising ? 0.8f + 0.8f * (1.f - impact) : 2.5f, 6.f);
}

void LightsourceUniverse::drop(float strength) {
    if (level_ >= 2) inDrop_ = true;
    dropWait_ = 0.f;
    dropT_ = 0.f;
    dropAmp_ = 0.6f + 0.4f * saturate(strength);
    // the whole stack lights up again, in a ripple from the bottom of the well, turning to the drop's color
    if (level_ >= 2) {
        waveT_ = 0.f;
        if (!huePicked_) pickHue();
    }
}

void LightsourceUniverse::refresh(float strength) {
    // (nothing lights up on its own: a block brightening out of the blue reads as a glitch)
    (void)strength;
}

void LightsourceUniverse::pickHue() {
    Rng r(songSeed_ ^ (0x9E3779B97F4A7C15ull * (uint64_t)(dropIndex_ + 1)));
    int h = r.irange(0, DROP_PAIR_COUNT - 1);
    if (h == lastHue_) h = (h + 1 + r.irange(0, DROP_PAIR_COUNT - 3)) % DROP_PAIR_COUNT;
    lastHue_ = h;
    // (which hue goes to which side: from the song too)
    const bool swap = r.uniform() < 0.5f;
    dropHue_ = FLUO[DROP_PAIRS[h][swap ? 1 : 0]];
    dropHue2_ = FLUO[DROP_PAIRS[h][swap ? 0 : 1]];
    dropIndex_++;
    huePicked_ = true;
}

vec2 LightsourceUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints LightsourceUniverse::hints() const {
    const VariantDef& d = VARIANTS[variant_];
    TransitionHints h;
    h.background = ugl::toLinear(hex(d.wall)) * 0.06f;
    h.ink = ugl::toLinear(hex(d.text)) * 0.7f;
    h.accent = ugl::toLinear(hex(d.light));
    h.light = false;
    h.styles = TS_LUMA | TS_INK | TS_REBUILD;
    return h;
}

// =====================================================================================================
// The board, the lamps
// =====================================================================================================

void LightsourceUniverse::syncBoard(const Game& g) {
    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            if (g.cell(x, y).type < 0) { ids_[y][x] = 0; glint_[y][x] = 0.f; }
        }
    // cells of unknown origin (a board filled for a review, a stack inherited from another universe): grouped into
    // pieces of at most four cells of one color, so that they light like pieces
    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            if (g.cell(x, y).type < 0 || ids_[y][x] != 0) continue;
            const int id = nextSynth_++;
            if (nextSynth_ > SYNTH_ID + 1000000) nextSynth_ = SYNTH_ID;
            const int type = g.cell(x, y).type;
            int todo[8][2], nt = 0, n = 0;
            todo[nt][0] = x; todo[nt][1] = y; nt++;
            while (nt > 0 && n < 4) {
                nt--;
                const int cx = todo[nt][0], cy = todo[nt][1];
                if (ids_[cy][cx] != 0) continue;
                ids_[cy][cx] = id;
                lockAt_[cy][cx] = -99.f;
                n++;
                const int nb[4][2] = {{cx + 1, cy}, {cx - 1, cy}, {cx, cy + 1}, {cx, cy - 1}};
                for (auto& q : nb)
                    if (q[0] >= 0 && q[0] < Game::W && q[1] >= 0 && q[1] < Game::H && nt < 8 && ids_[q[1]][q[0]] == 0 &&
                        g.cell(q[0], q[1]).type == type) { todo[nt][0] = q[0]; todo[nt][1] = q[1]; nt++; }
            }
        }
}

// The stack's pieces and, in a drop, how much each one shines.
void LightsourceUniverse::buildPieces(const Game& g, float dropK) {
    pieces_.clear();
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const int id = ids_[y][x];
            if (id <= 0 || g.cell(x, y).type < 0) continue;
            PieceInfo* pi = nullptr;
            for (PieceInfo& q : pieces_)
                if (q.id == id) { pi = &q; break; }
            if (!pi) {
                PieceInfo q{};
                q.id = id;
                q.type = g.cell(x, y).type;
                pieces_.push_back(q);
                pi = &pieces_.back();
            }
            if (pi->n < 4) {
                pi->cells[pi->n][0] = x;
                pi->cells[pi->n][1] = y;
                pi->n++;
            }
        }
    for (PieceInfo& q : pieces_) {
        vec2 c = v2(0.f, 0.f);
        for (int i = 0; i < q.n; i++) {
            const vec3 w = layout::cellPos((float)q.cells[i][0], q.cells[i][1] - g.rowOffset(q.cells[i][1]));
            c.x += w.x / q.n;
            c.y += w.y / q.n;
        }
        q.c = c;
        q.level = 0.f;
        q.mono = 0.f;
        if (dropK < 0.002f) continue;
        const float seed = hashf((float)(q.id % 100003) * 0.371f + 1.3f);
        // its own slow shimmer (a period of its own: the pieces dim and brighten in turn, the room never pulses)
        const float base = 0.1f + 0.22f * (0.5f + 0.5f * std::sin(world_ * TAU / (2.6f + 2.4f * seed) + seed * TAU));
        // the downbeat's ripple, outward from the bottom of the well
        const float dist = std::sqrt(c.x * c.x + (c.y + 10.5f) * (c.y + 10.5f));
        const float wt = waveT_ - 0.045f * dist;
        const float wave = wt > 0.f ? smoothstepf(0.f, 0.12f, wt) * std::exp(-std::max(0.f, wt - 0.12f) / 0.9f) : 0.f;
        // the drop's color comes with the ripple, and leaves with the drop
        q.mono = smoothstepf(0.f, 0.45f, wt) * smoothstepf(0.f, 0.6f, dropK);
        float flare = 0.f;
        for (const DFlare& d : dflares_)
            if (d.id == q.id) flare += smoothstepf(0.f, 0.5f, d.t) * std::exp(-std::max(0.f, d.t - 0.5f) / 1.1f); // a slow swell, no flash
        q.level = dropK * std::min(0.9f, base + 0.7f * wave + 0.4f * flare);
    }
}

float LightsourceUniverse::pieceLevel(int id) const {
    for (const PieceInfo& q : pieces_)
        if (q.id == id) return q.level;
    return 0.f;
}

void LightsourceUniverse::collapseRows(const bool cleared[Game::H]) {
    int ni[Game::H][Game::W];
    float nl[Game::H][Game::W], ng[Game::H][Game::W];
    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) { ni[y][x] = 0; nl[y][x] = -99.f; ng[y][x] = 0.f; }
    int dst = Game::H - 1;
    for (int y = Game::H - 1; y >= 0; y--) {
        if (cleared[y]) continue;
        for (int x = 0; x < Game::W; x++) { ni[dst][x] = ids_[y][x]; nl[dst][x] = lockAt_[y][x]; ng[dst][x] = glint_[y][x]; }
        dst--;
    }
    std::memcpy(ids_, ni, sizeof(ids_));
    std::memcpy(lockAt_, nl, sizeof(lockAt_));
    std::memcpy(glint_, ng, sizeof(glint_));
}

// A lamp's intensity: the impact's small flare, the steady light (slowly tiring), a smooth fade.
float LightsourceUniverse::lampLevel(const Lamp& l) const {
    const float a = l.age;
    if (l.relight) {
        if (a < 0.3f) return l.power * smoothstepf(0.f, 0.3f, a);
        return l.power * std::exp(-(a - 0.3f) / 0.6f) * (1.f - smoothstepf(1.4f, 2.0f, a));
    }
    // it takes over the falling piece's light exactly (no flash on the lock), slowly tiring
    if (a < l.hold) return l.power * (1.f - 0.14f * a / std::max(0.1f, l.hold));
    // then it dims smoothly (an eased fade, its color cooling) down to its ember
    const float u = (a - l.hold) / FADE;
    return l.power * 0.86f * (1.f - smoothstepf(0.f, 1.f, u));
    // (0.86: where the steady light ends)
}

bool LightsourceUniverse::lampAlive(const Lamp& l) const {
    if (l.relight) return l.age < 2.0f;
    return l.age < l.hold + FADE;
}

void LightsourceUniverse::relight(int id, float power) {
    if (id <= 0) return;
    for (Lamp& l : lamps_)
        if (l.id == id) {
            if (l.relight) { l.age = std::min(l.age, 0.05f); l.power = std::max(l.power, power); }
            return; // still shining from its lock
        }
    int type = 0;
    bool found = false;
    for (int y = 0; y < Game::H && !found; y++)
        for (int x = 0; x < Game::W; x++)
            if (ids_[y][x] == id) { found = true; break; }
    if (!found) return;
    (void)type;
    Lamp l;
    l.id = id;
    l.relight = true;
    l.power = power;
    l.seed = rng_.uniform();
    lamps_.push_back(l);
}

void LightsourceUniverse::onEvent(const GameEvent& ev, const Game& g) {
    switch (ev.type) {
    case GameEvent::HardDrop:
        hardPending_ = true;
        break;
    case GameEvent::Spawn:
        pieceOn_ = 0.f;
        break;
    case GameEvent::Lock: {
        syncBoard(g); // (the cleared rows are already gone from the board: only fills unknown cells)
        const int id = nextId_++;
        if (nextId_ > 1000000) nextId_ = 1;
        int type = 0;
        float lowY = 1e9f, x0 = 1e9f, x1 = -1e9f;
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            ids_[c.y][c.x] = id;
            lockAt_[c.y][c.x] = time_;
            glint_[c.y][c.x] = 0.f;
            type = c.type;
            const vec3 w = layout::cellPos((float)c.x, (float)c.y);
            if (w.y < lowY - 0.1f) { lowY = w.y; x0 = x1 = w.x; }
            else if (std::fabs(w.y - lowY) < 0.1f) { x0 = std::min(x0, w.x); x1 = std::max(x1, w.x); }
        }
        Lamp l;
        l.id = id;
        l.type = type;
        l.seed = rng_.uniform();
        // the steady light lasts a few seconds: longer in a break, shorter in a drop (more lights come and go)
        l.hold = lerpf(lerpf(3.0f, 2.5f, smoothstepf(0.f, 1.f, lv_)), lerpf(2.2f, 1.7f, drop_), smoothstepf(1.f, 2.f, lv_));
        l.power = 1.f;
        lamps_.push_back(l);
        // too many lamps at once: the oldest ones start dying now
        int n = 0;
        for (int i = (int)lamps_.size() - 1; i >= 0; i--)
            if (!lamps_[i].relight && ++n > 4 && lamps_[i].age < lamps_[i].hold) lamps_[i].age = lamps_[i].hold;
        // a small glow where the piece touched down (a hard drop: brighter, longer)
        if (lowY < 1e8f) {
            Contact c;
            c.a = v2(x0 - 0.45f, lowY - 0.5f);
            c.b = v2(x1 + 0.45f, lowY - 0.5f);
            c.c = lightOf(type, monoG_);
            c.power = hardPending_ ? 1.0f : 0.5f;
            c.life = hardPending_ ? 0.4f : 0.28f;
            contacts_.push_back(c);
        }
        hardPending_ = false;
        break;
    }
    case GameEvent::Clear: {
        bool cleared[Game::H] = {};
        for (auto& c : ev.cells)
            if (c.y >= 0 && c.y < Game::H) cleared[c.y] = true;
        const bool tetris = ev.count >= 4;
        for (auto& c : ev.cells) {
            Burn b;
            const vec3 w = layout::cellPos((float)c.x, (float)c.y);
            b.p = v2(w.x, w.y);
            b.type = c.type;
            b.row = c.y;
            b.life = tetris ? 0.55f : 0.42f;
            b.delay = std::fabs(c.x - 4.5f) * 0.008f;
            b.power = tetris ? 1.3f : 1.f;
            b.seed = rng_.uniform();
            burns_.push_back(b);
        }
        {
            // the afterlight: as strong as the falling piece's light it replaces, fading over a second
            float ys = 0.f;
            int ny = 0;
            vec3 col(0.f);
            for (int y = 0; y < Game::H; y++)
                if (cleared[y]) { ys += layout::cellPos(0.f, (float)y).y; ny++; }
            for (auto& c : ev.cells) col += lightOf(c.type, monoG_) * (1.f / std::max<size_t>(1, ev.cells.size()));
            if (ny > 0) {
                for (int k = 0; k < 4; k++) {
                    const vec3 w = layout::cellPos(1.f + k * 2.33f, 0.f);
                    afterPts_[k] = v2(w.x, ys / ny);
                }
                afterCol_ = col;
                afterT_ = 0.f;
            }
        }
        collapseRows(cleared);
        if (tetris) tetrisT_ = 0.f;
        break;
    }
    case GameEvent::TopOut: {
        // game over: the stack's lights go out, row by row from the top
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (auto& c : ev.cells) {
            Burn b;
            const vec3 w = layout::cellPos((float)c.x, (float)c.y);
            b.p = v2(w.x, w.y);
            b.type = c.type;
            b.row = c.y;
            b.over = true;
            b.life = 0.9f;
            b.delay = (c.y - top) * 0.07f + rng_.range(0.f, 0.05f);
            b.power = 0.55f;
            b.seed = rng_.uniform();
            burns_.push_back(b);
        }
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) { ids_[y][x] = 0; glint_[y][x] = 0.f; }
        lamps_.clear();
        break;
    }
    default: break;
    }
}

// =====================================================================================================
// Lights
// =====================================================================================================

// The light a piece's lamp throws: the variant's light, tinted by the piece; the drop's light, slow motion's cold.
vec3 LightsourceUniverse::lightOf(int type, float m) const {
    vec3 p = pieceCol(type);
    p = p * (1.f / std::max(0.05f, std::max(p.x, std::max(p.y, p.z))));
    vec3 c = lerp(light_, p, tintK_);
    c = lerp(c, lerp(dropLight_, p, tintK_ * 0.5f), 0.55f * drop_);
    // a drop's colors: the light takes its side's hue (lightly whitened like the piece's shade)
    if (m > 0.f) c = lerp(c, monoTint(type) * (1.f / std::max(0.05f, std::max(monoTint(type).x, std::max(monoTint(type).y, monoTint(type).z)))), m);
    c = lerp(c, vec3(0.62f, 0.76f, 1.f), 0.55f * slow_);
    return c * (0.6f / std::max(0.05f, lum(c)));
}

// A lamp's face: the piece's own color (the pieces stay apart), a little of the light's.
vec3 LightsourceUniverse::emitOf(int type, float m) const {
    vec3 p = pieceCol(type, m);
    p = p * (1.f / std::max(0.05f, std::max(p.x, std::max(p.y, p.z))));
    vec3 c = lerp(p, lightOf(type, m) * (1.f / 0.6f) * 0.75f, 0.3f * (1.f - m));
    // the pieces of a side read apart by their value (partly in the calm, fully in a drop's colors)
    const int t = type < 0 ? 0 : type > 6 ? 6 : type;
    return c * (0.5f / std::max(0.05f, lum(c)) * lerpf(1.f, SHADE[t], lerpf(0.5f, 1.f, m)));
}

// The pieces in a drop's colors: each side its hue, each piece its value (and a little whitening).
vec3 LightsourceUniverse::monoTint(int type) const {
    const int t = type < 0 ? 0 : type > 6 ? 6 : type;
    return lerp(SIDE[t] ? dropHue2_ : dropHue_, vec3(1.f), SHADE_WHITE[t]) * SHADE[t];
}

vec3 LightsourceUniverse::pieceCol(int type, float m) const {
    const vec3 own = piece_[type < 0 ? 0 : type > 6 ? 6 : type];
    if (m <= 0.f) return own;
    return lerp(own, monoTint(type) * 0.62f, m);
}

float LightsourceUniverse::monoOf(int id) const {
    for (const PieceInfo& q : pieces_)
        if (q.id == id) return q.mono;
    return 0.f;
}

void LightsourceUniverse::buildRows(const Game& g) {
    rowMin_ = Game::H;
    for (int r = 0; r < Game::H; r++) {
        rowY_[r] = layout::cellPos(0.f, r - g.rowOffset(r)).y;
        int m = 0;
        for (int x = 0; x < Game::W; x++)
            if (g.cell(x, r).type >= 0) m |= 1 << x;
        rowMask_[r] = m;
        if (m && rowMin_ == Game::H) rowMin_ = r;
    }
}

void LightsourceUniverse::setRowUniforms(GLuint prog) {
    glUniform1fv(ugl::U(prog, "uRowY"), Game::H, rowY_);
    glUniform1iv(ugl::U(prog, "uRowMask"), Game::H, rowMask_);
    set1i(prog, "uRowMin", rowMin_);
    set1f(prog, "uTopY", rowMin_ < Game::H ? rowY_[rowMin_] + 0.5f : -99.f);
}

// =====================================================================================================
// The play: blocks, ghost, previews
// =====================================================================================================

void LightsourceUniverse::block(vec2 pos, float scale, const vec3& alb, float kind, const vec3& em, int conn, float alpha,
                                float seed, float burnT, float hot) {
    if (alpha <= 0.003f) return;
    blocks_.push_back({{pos.x, pos.y, scale, alpha}, {alb.x, alb.y, alb.z, kind}, {em.x, em.y, em.z, (float)conn}, {seed, burnT, hot, 0.f}});
}

// A cell's neighbors inside its piece: left 1, right 2, up 4, down 8, and the diagonals up-left 16, up-right 32,
// down-left 64, down-right 128 (game rows: up is y - 1).
int LightsourceUniverse::connMask(int cc[4][2], int i) {
    int conn = 0;
    for (int j = 0; j < 4; j++) {
        if (j == i) continue;
        const int dx = cc[j][0] - cc[i][0], dy = cc[j][1] - cc[i][1];
        if (dy == 0 && dx == -1) conn |= 1;
        if (dy == 0 && dx == 1) conn |= 2;
        if (dx == 0 && dy == -1) conn |= 4;
        if (dx == 0 && dy == 1) conn |= 8;
        if (dx == -1 && dy == -1) conn |= 16;
        if (dx == 1 && dy == -1) conn |= 32;
        if (dx == -1 && dy == 1) conn |= 64;
        if (dx == 1 && dy == 1) conn |= 128;
    }
    return conn;
}

void LightsourceUniverse::buildBlocks(const Game& g, float ember) {
    // the lamps' levels by lock id
    auto lampOf = [&](int id) -> const Lamp* {
        if (id <= 0) return nullptr;
        const Lamp* best = nullptr;
        for (const Lamp& l : lamps_)
            if (l.id == id && (!best || l.level > best->level)) best = &l;
        return best;
    };
    const float albK = 0.85f;
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            const int id = ids_[y][x];
            int conn = 0;
            if (id > 0 && id < SYNTH_ID) {
                if (x > 0 && ids_[y][x - 1] == id) conn |= 1;
                if (x < Game::W - 1 && ids_[y][x + 1] == id) conn |= 2;
                if (y > 0 && ids_[y - 1][x] == id && std::fabs(g.rowOffset(y - 1) - g.rowOffset(y)) < 0.02f) conn |= 4;
                if (y < Game::H - 1 && ids_[y + 1][x] == id && std::fabs(g.rowOffset(y + 1) - g.rowOffset(y)) < 0.02f) conn |= 8;
                // the diagonals (a concave corner of the piece is carved where both sides are the piece and the
                // diagonal is not)
                auto same = [&](int xx, int yy) {
                    return xx >= 0 && xx < Game::W && yy >= 0 && yy < Game::H && ids_[yy][xx] == id;
                };
                if (same(x - 1, y - 1)) conn |= 16;
                if (same(x + 1, y - 1)) conn |= 32;
                if (same(x - 1, y + 1)) conn |= 64;
                if (same(x + 1, y + 1)) conn |= 128;
            }
            vec3 em(0.f);
            float hot = 0.f;
            const float mo = monoOf(id);
            const vec3 lc = emitOf(c.type, mo);
            const Lamp* l = lampOf(id);
            float on = 0.f;
            if (l) {
                on = l->level;
                // a dying lamp's filament cools toward a deeper, warmer color
                const float cool = l->relight ? 0.f : smoothstepf(l->hold, l->hold + 1.8f, l->age);
                // fresh from its fall it is still the falling piece's bright face, easing into a lamp's
                const float fr = l->relight ? 0.f : 1.f - smoothstepf(0.f, l->hold, l->age);
                em = mulv(lerp(lc, mulv(lc, vec3(1.f, 0.6f, 0.38f)), cool), vec3(EMIT * on * lerpf(1.f, 1.7f, fr)));
                hot = lerpf(0.25f, 0.6f, fr) * saturate(on) * (1.f - mo);
            }
            // relit by a drop
            {
                const float dl = pieceLevel(id);
                if (dl > 0.002f) {
                    const vec3 dc = lc * (EMIT * 0.55f * dl);
                    if (lum(dc) > lum(em)) { em = dc; hot = 0.2f * dl * (1.f - mo); }
                }
            }
            // the ember: a faint glow kept for a while after the lamp went out
            const float age = time_ - lockAt_[y][x];
            if (id > 0 && age > 0.f) {
                const float e = ember * 0.07f * std::exp(-std::max(0.f, age - 4.f) / 16.f) + 0.5f * glint_[y][x];
                const vec3 ec = mulv(lc, vec3(1.f, 0.7f, 0.5f)) * e;
                if (lum(ec) > lum(em)) em = ec;
            }
            block(v2(p.x, p.y), 1.f, pieceCol(c.type, mo) * albK, 0.f, em, conn, 1.f, hashf(y * 13.f + x), 0.f, hot);
        }
    if (g.hasPiece() && !g.over()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        auto connOf = [&](int cc[4][2], int i) { return connMask(cc, i); };
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (int i = 0; i < 4; i++) {
                if (gc[i][1] < Game::HIDDEN) continue;
                const vec3 w = layout::cellPos((float)gc[i][0], (float)gc[i][1]);
                block(v2(w.x, w.y), 1.f, vec3(0.f), 1.f, vec3(0.f), connOf(gc, i), 0.32f);
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        // in a drop the falling piece takes the drop's color too, at its brightest
        const float pm = monoG_;
        const vec3 lc = emitOf(p.type, pm) * (lerpf(1.f, 1.f / std::max(0.3f, SHADE[p.type < 0 ? 0 : p.type > 6 ? 6 : p.type]), pm) * lerpf(0.35f, 1.f, smoothstepf(0.f, 1.f, pieceOn_)));
        for (int i = 0; i < 4; i++) {
            if (cells[i][1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(cells[i][0] + dx, cells[i][1] + dy);
            block(v2(w.x, w.y), 1.f, pieceCol(p.type, pm) * albK, 0.f, lc * (EMIT * 1.7f), connOf(cells, i), 1.f, 0.f, 0.f, lerpf(0.6f, 0.55f, pm));
        }
    }
    // the burning cells of a cleared row (or of the game over)
    for (const Burn& b : burns_) {
        if (b.delay > 0.f) {
            if (b.over) block(b.p, 1.f, pieceCol(b.type, monoG_) * albK, 0.f, vec3(0.f), 0, 1.f);
            continue;
        }
        const float u = saturate(b.t / b.life);
        const vec3 lc = emitOf(b.type, monoG_);
        if (b.over) {
            // the light goes out: the block fades into the dark
            const float fl = 1.f - smoothstepf(0.f, 1.f, u);
            block(b.p, 1.f, pieceCol(b.type, monoG_) * albK * (1.f - u), 0.f, lc * (EMIT * 0.7f * fl * (1.f - u)), 0, 1.f - smoothstepf(0.6f, 1.f, u));
            continue;
        }
        block(b.p, 1.f, vec3(0.f), 3.f, lc * (EMIT * 1.9f * b.power), 0, 1.f, b.seed, u, 0.35f);
    }
    // the previews: unlit lamps waiting, the next one warming up
    auto preview = [&](int type, vec2 center, float sc, float alpha, float warm) {
        if (type < 0) return;
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        g.pieceCells(p, cells);
        float minx = 9, maxx = -9, miny = 9, maxy = -9;
        for (auto& c : cells) {
            minx = std::min(minx, (float)c[0]); maxx = std::max(maxx, (float)c[0]);
            miny = std::min(miny, (float)c[1]); maxy = std::max(maxy, (float)c[1]);
        }
        // (the palette's deepest values lifted a little: a small piece in the dark must still read)
        vec3 pc = pieceCol(type);
        pc = pc * std::min(1.f / std::max(0.05f, std::max(pc.x, std::max(pc.y, pc.z))), std::max(1.f, 0.2f / std::max(0.01f, lum(pc))));
        for (int i = 0; i < 4; i++) {
            const int conn = connMask(cells, i);
            const vec2 pos = v2(center.x + (cells[i][0] - (minx + maxx) * 0.5f) * sc, center.y - (cells[i][1] - (miny + maxy) * 0.5f) * sc);
            block(pos, sc, pc * albK, 2.f, lightOf(type) * warm, conn, alpha);
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.4f : 1.f, 0.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f, i == 0 ? 0.08f : 0.f);
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float LightsourceUniverse::textWidth(const std::string& s, float size, float track) const {
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

void LightsourceUniverse::textQuads(const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const {
    const Font& f = font_;
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

void LightsourceUniverse::drawText(const std::vector<float>& q, const vec3& srgb, float alpha, float bias, float soft) {
    if (q.empty() || !font_.ok || alpha <= 0.003f) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    set4f(progText_, "uInk", srgb.x, srgb.y, srgb.z, saturate(alpha));
    set1f(progText_, "uBias", bias);
    set1f(progText_, "uSoft", soft);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(float), q.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 4));
}

// The HUD in LIGHTSOURCE's typography: Sora, small widely tracked capitals for the labels, the numbers plain and
// pale, each line on a soft halo of the room's dark (it reads where a lamp lights the wall); the section, the
// notices and the universe's name in the variant's light.
void LightsourceUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels, float mul) {
    const float bridge = saturate(bridge_.amount);
    const vec3 ink = lerp(text_, bridge_.ink, bridge) * mul, acc = lerp(accent_, bridge_.ink, bridge) * mul;
    const vec3 shadow = ugl::toSrgb(lerp(wall_ * 0.004f, bridge_.color, bridge));
    if (!panels.empty()) {
        glUseProgram(progPanel_);
        set2f(progPanel_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(panelVao_);
        glBindBuffer(GL_ARRAY_BUFFER, panelVbo_);
        for (const HudRect& r : panels) {
            const float q[12] = {r.x, r.y, r.x + r.w, r.y, r.x + r.w, r.y + r.h, r.x, r.y, r.x + r.w, r.y + r.h, r.x, r.y + r.h};
            glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STREAM_DRAW);
            const vec3 c = r.custom ? r.color : r.accent ? ugl::toSrgb(acc) : shadow;
            const float a = saturate(r.accent ? r.alpha * 0.35f : r.alpha * 0.9f);
            glUniform4f(ugl::U(progPanel_, "uColor"), c.x * a, c.y * a, c.z * a, a);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }
    const vec3 inkS = ugl::toSrgb(ink), accS = ugl::toSrgb(acc);
    const float s = height_ / 1080.f;
    std::vector<float> q;
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        const float alpha = saturate(h.alpha);
        auto put = [&](const std::string& text, float size, float track, float dy, const vec3& c, float a, float bias) {
            q.clear();
            const float w = textWidth(text, size, track);
            textQuads(text, ugl::hudLeft(h, w, width_), h.y + dy, size, track, q);
            drawText(q, shadow, a * 0.9f, 0.3f, 0.34f);
            drawText(q, c, a, bias, 0.f);
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            const float sz = base * 0.86f, tr = base * 0.36f, vsz = base * 0.66f, vtr = base * 0.28f;
            const float wName = textWidth(name, sz, tr), gap = base * 0.9f;
            const float wVar = var.empty() ? 0.f : textWidth(var, vsz, vtr);
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            q.clear();
            textQuads(name, x, h.y, sz, tr, q);
            drawText(q, shadow, a * 0.85f, 0.3f, 0.34f);
            drawText(q, accS, a, 0.03f, 0.f);
            if (!var.empty()) {
                q.clear();
                textQuads(var, x + wName + gap, h.y + (sz - vsz) * 0.75f, vsz, vtr, q);
                drawText(q, shadow, a * 0.8f, 0.3f, 0.34f);
                drawText(q, inkS, a * 0.7f, 0.02f, 0.f);
            }
            continue;
        }
        case HUD_TITLE: put(h.text, base * 0.98f, base * 0.03f, 0.f, inkS, saturate(h.alpha * 1.3f), 0.03f); continue;
        case HUD_INFO: put(h.text, base * 0.72f, base * 0.12f, 0.f, inkS, alpha * 0.6f, 0.01f); continue;
        case HUD_SECTION: put(h.text, base * 0.86f, base * 0.42f, 0.f, accS, saturate(0.2f + h.alpha * 1.9f), 0.04f); continue;
        case HUD_LABEL: put(h.text, base * 0.6f, base * 0.4f, 0.f, inkS, alpha * 0.55f, 0.01f); continue;
        case HUD_VALUE: put(h.text, base * 1.0f, base * 0.05f, 0.f, inkS, alpha * 0.95f, 0.02f); continue;
        case HUD_NOTICE: put(h.text, base * 0.95f, base * 0.14f, 0.f, accS, alpha, 0.03f); continue;
        case HUD_TOAST: put(h.text, base * 1.15f, base * 0.34f, -base * 0.1f, accS, alpha, 0.04f); continue;
        case HUD_TOAST_SUB: put(h.text, base * 0.8f, base * 0.14f, 0.f, inkS, alpha, 0.02f); continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) put(h.text, base * 1.05f, base * 0.2f, -base * 0.1f, inkS, alpha, 0.03f);
            else put(h.text, base * 0.8f, base * 0.06f, 0.f, h.accent ? accS : inkS, alpha, 0.02f);
            continue;
        default: put(h.text, base * 0.78f, base * 0.05f, 0.f, h.accent ? accS : inkS, alpha, 0.02f); continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void LightsourceUniverse::drawInst(const std::vector<Inst>& v) {
    if (v.empty()) return;
    glBindVertexArray(instVao_);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Inst), v.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)v.size());
}

void LightsourceUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const VariantDef& d = VARIANTS[variant_];
        const float k = palSet_ ? 1.f - std::exp(-dt * 1.2f) : 1.f;
        palSet_ = true;
        wall_ = lerp(wall_, ugl::toLinear(hex(d.wall)), k);
        floor_ = lerp(floor_, ugl::toLinear(hex(d.floor)), k);
        rail_ = lerp(rail_, ugl::toLinear(hex(d.rail)), k);
        ambient_ = lerp(ambient_, ugl::toLinear(hex(d.ambient)), k);
        light_ = lerp(light_, ugl::toLinear(hex(d.light)), k);
        dropLight_ = lerp(dropLight_, ugl::toLinear(hex(d.dropLight)), k);
        text_ = lerp(text_, ugl::toLinear(hex(d.text)), k);
        accent_ = lerp(accent_, ugl::toLinear(hex(d.accent)), k);
        ghost_ = lerp(ghost_, ugl::toLinear(hex(d.text)) * 0.22f, k);
        tintK_ = lerpf(tintK_, d.tintK, k);
        for (int i = 0; i < 7; i++) piece_[i] = lerp(piece_[i], ugl::toLinear(hex(d.piece[i])), k);
    }
    // the section, from the HUD (a drop, or any other peak: a chorus)
    sectionKnown_ = false;
    for (const HudText& h : f.hud)
        if (h.role == HUD_SECTION) {
            sectionKnown_ = true;
            sectionDrop_ = h.text == "DROP";
        }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 1.2f, dt);
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.5f : 0.f), 3.f, dt);
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.3f, slow_);
    const float bridge = saturate(bridge_.amount);
    syncBoard(game);

    if (!f.paused) {
        time_ += dt;
        world_ += wdt;
        if (lvT_ < 1.f) {
            lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
            lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
        }
        dropT_ += dt;
        tetrisT_ += dt;
        dropWait_ = std::max(0.f, dropWait_ - dt);
        const bool dropish = sectionKnown_ ? sectionDrop_
                                           : (music.hasSong ? music.speed > 1.33f || (inDrop_ && music.speed > 1.26f) : inDrop_);
        const bool hot = level_ >= 2 && dropWait_ <= 0.f && dropish;
        drop_ = hot ? std::min(1.f, drop_ + dt / 0.35f) : std::max(0.f, drop_ - dt / 2.5f);
        if (hot && !huePicked_) pickHue();
        if (drop_ <= 0.f && !hot) huePicked_ = false;
        chorus_ = approach(chorus_, level_ >= 2 && !hot ? 1.f : 0.f, 1.0f, dt);
        // the lamps age with the world (slow motion lets them linger)
        for (Lamp& l : lamps_) l.age += l.relight ? dt : wdt;
        lamps_.erase(std::remove_if(lamps_.begin(), lamps_.end(), [&](const Lamp& l) { return !lampAlive(l); }), lamps_.end());
        for (auto& row : glint_)
            for (float& gl : row) gl = std::max(0.f, gl - dt * 1.6f);
        // on the beat grid, every two beats: a drop slowly brightens a few buried pieces
        if (music.hasSong && lv_ > 0.6f) {
            const double slot = std::floor(music.beat / 2.0);
            if (slot != lastBeatSlot_) {
                if (lastBeatSlot_ >= 0.0) {
                    std::vector<int> cells, ids;
                    for (int y = Game::HIDDEN; y < Game::H; y++)
                        for (int x = 0; x < Game::W; x++)
                            if (ids_[y][x] > 0) {
                                cells.push_back(y * Game::W + x);
                                if (std::find(ids.begin(), ids.end(), ids_[y][x]) == ids.end()) ids.push_back(ids_[y][x]);
                            }
                    if (drop_ > 0.5f && !ids.empty()) {
                        // a few pieces flare, in turn
                        const int k = std::min((int)ids.size(), 2 + (int)(ids.size() / 10));
                        for (int i = 0; i < k; i++) dflares_.push_back({ids[rng_.irange(0, (int)ids.size() - 1)], 0.f});
                    }
                }
                lastBeatSlot_ = slot;
            }
        }
        // effects at the real speed (a clear never hangs in slow motion)
        for (Burn& b : burns_) {
            if (b.delay > 0.f) b.delay -= dt;
            else b.t += dt;
        }
        burns_.erase(std::remove_if(burns_.begin(), burns_.end(), [](const Burn& b) { return b.delay <= 0.f && b.t >= b.life; }), burns_.end());
        waveT_ += dt;
        afterT_ += dt;
        pieceOn_ = std::min(1.f, pieceOn_ + dt / 0.35f);
        for (DFlare& d : dflares_) d.t += dt;
        dflares_.erase(std::remove_if(dflares_.begin(), dflares_.end(), [](const DFlare& d) { return d.t > 5.f; }), dflares_.end());
        for (Contact& c : contacts_) c.t += dt;
        contacts_.erase(std::remove_if(contacts_.begin(), contacts_.end(), [](const Contact& c) { return c.t >= c.life; }), contacts_.end());
        gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 2.5f, dt);
        // the side lights: one in a chorus, both in a drop, off otherwise
        side_[0] = approach(side_[0], std::max(0.75f * chorus_, drop_), 0.8f, dt);
        side_[1] = approach(side_[1], drop_, 0.8f, dt);
    }
    for (Lamp& l : lamps_) l.level = lampLevel(l);
    const float swell = inDrop_ && dropT_ < 12.f ? smoothstepf(0.f, 0.25f, dropT_) * std::exp(-std::max(0.f, dropT_ - 0.25f) / 2.2f) * dropAmp_ : 0.f;
    const float tet = tetrisT_ < 6.f ? smoothstepf(0.f, 0.3f, tetrisT_) * std::exp(-std::max(0.f, tetrisT_ - 0.3f) / 0.9f) : 0.f;

    // ---- The staging: the light's power, its softness and reach, the room's fill.
    const float lvm = smoothstepf(0.f, 1.f, lv_), lvp = smoothstepf(1.f, 2.f, lv_);
    const float worldOn = (1.f - smoothstepf(0.f, 0.8f, bridge)) * (1.f - 0.35f * dimT_);
    const float power = (lerpf(0.75f, 1.f, lvm) + 0.1f * chorus_ * lvp + 0.3f * drop_ + 0.45f * swell) * worldOn;
    const float spread = lerpf(lerpf(1.0f, 0.75f, lvm), 0.6f, lvp * chorus_) * (1.f - 0.62f * drop_);
    const float reach = lerpf(3.5f, 5.f, lvm) + 1.f * chorus_ * lvp + 0.5f * drop_;
    const float ambK = lerpf(0.8f, 1.f, lvm) * (1.f - 0.12f * drop_) * (1.f - 0.25f * slow_);
    const float ember = (1.f + 2.f * swell + 4.f * tet) * worldOn;

    // ---- The lights.
    buildRows(game);
    monoG_ = smoothstepf(0.f, 0.6f, drop_) * (waveT_ < 0.45f ? smoothstepf(0.f, 0.45f, waveT_) : 1.f);
    buildPieces(game, drop_ * worldOn);
    lights_.clear();
    glows_.clear();
    auto cellsOf = [&](int id, LightDef& L) {
        int n = 0;
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++)
                if (ids_[y][x] == id && n < 4) {
                    const vec3 w = layout::cellPos((float)x, y - game.rowOffset(y));
                    L.pts.push_back(v2(w.x, w.y));
                    L.own[n][0] = x;
                    L.own[n][1] = y;
                    n++;
                }
        return n;
    };
    if (game.hasPiece() && !game.over()) {
        const Piece& p = game.piece();
        int cells[4][2];
        game.pieceCells(p, cells);
        const float dx = game.pieceVisualX() - p.x, dy = game.pieceVisualY() - p.y;
        LightDef L;
        for (int i = 0; i < 4; i++) {
            const vec3 w = layout::cellPos(cells[i][0] + dx, cells[i][1] + dy);
            L.pts.push_back(v2(w.x, w.y));
        }
        // the room's one real light: stronger and farther-reaching than any lamp, a smaller source (crisper shadows),
        // its energy more in the far term than in the close pool (the cells beside it do not burn out)
        L.color = lightOf(p.type, monoG_) * (1.45f * power * smoothstepf(0.f, 1.f, pieceOn_));
        L.reach = reach * 1.35f;
        L.spread = spread * 0.7f;
        L.nearW = 0.6f + 0.1f * drop_;
        L.sub = 4;
        L.rank = 100.f;
        lights_.push_back(L);
    }
    for (const Lamp& l : lamps_) {
        if (l.level < 0.02f) continue;
        LightDef L;
        if (cellsOf(l.id, L) == 0) continue;
        int type = l.type;
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++)
                if (ids_[y][x] == l.id) type = game.cell(x, y).type >= 0 ? game.cell(x, y).type : type;
        const vec3 lc = lightOf(type, monoOf(l.id));
        const float cool = l.relight ? 0.f : smoothstepf(l.hold, l.hold + 1.8f, l.age);
        // a fresh lamp is exactly the falling piece's light it takes over (the same power, reach and softness: the
        // room does not dim on the lock), then eases into a lamp's
        const float fr = l.relight ? 0.f : 1.f - smoothstepf(0.f, l.hold, l.age);
        L.color = lerp(lc, mulv(lc, vec3(1.f, 0.6f, 0.38f)), cool) * (l.level * power * (l.relight ? 0.8f : lerpf(1.f, 1.45f, fr)));
        L.reach = reach * (l.relight ? 0.8f : lerpf(1.f, 1.35f, fr));
        L.spread = spread * lerpf(1.f, 0.7f, fr);
        L.nearW = lerpf(0.8f, 0.6f, fr) + 0.1f * drop_;
        L.sub = 4; // (switching the sample count would pop the shadows)
        L.rank = l.level * (l.relight ? 0.8f : 1.f);
        lights_.push_back(L);
    }
    {
        // the cleared rows burning: each row a line of light
        for (int r = 0; r < Game::H; r++) {
            LightDef L;
            vec3 c(0.f);
            float I = 0.f;
            for (const Burn& b : burns_) {
                if (b.row != r || (b.over && b.delay > 0.f) || L.pts.size() >= 10) continue;
                // (a cleared row's light is there at once, from the very frame its blocks vanish: no dark frame)
                const float u = b.delay > 0.f ? 0.f : b.t / b.life;
                const float k = b.over ? 0.35f * (1.f - u) : b.power * (1.f - smoothstepf(0.15f, 1.f, u));
                L.pts.push_back(b.p);
                c += lightOf(b.type, monoG_) * k;
                I = std::max(I, k);
            }
            if (L.pts.empty() || I < 0.02f) continue;
            L.color = c * (1.7f / (float)L.pts.size()) * worldOn;
            L.reach = 5.f;
            L.spread = 0.f;
            L.sub = 1;
            L.height = 0.6f;
            L.rank = 50.f * I;
            lights_.push_back(L);
        }
    }
    if (afterT_ < 1.4f && !game.over()) {
        // (it takes over as the burning rows die out: the burns carry the first instants)
        LightDef L;
        for (int k = 0; k < 4; k++) L.pts.push_back(afterPts_[k]);
        L.color = afterCol_ * (1.45f * power * smoothstepf(0.05f, 0.35f, afterT_) * (1.f - smoothstepf(0.35f, 1.4f, afterT_)));
        L.reach = reach * 1.35f;
        L.spread = spread * 0.7f;
        L.nearW = 0.6f + 0.1f * drop_;
        L.height = 1.2f;
        L.sub = 4;
        L.rank = 90.f;
        lights_.push_back(L);
    }
    for (int i = 0; i < 2; i++) {
        if (side_[i] < 0.01f) continue;
        // a stage spot low in each corner, its narrow beam crossing the well, aimed slowly up and down: the stack
        // throws a long crisp shadow inside it across the wall
        const float sgn = i == 0 ? -1.f : 1.f;
        const vec2 src = v2(sgn * 23.5f, -11.5f);
        const float ang = 0.36f + 0.13f * std::sin(world_ * TAU / (i == 0 ? 23.f : 29.f) + i * 2.1f); // above horizontal
        LightDef L;
        L.pts.push_back(src);
        L.aim = v2(-sgn * std::cos(ang), std::sin(ang));
        L.coneOut = std::cos(0.1f);
        L.coneIn = std::cos(0.035f);
        vec3 c = lerp(lerp(light_, dropLight_, drop_), i == 0 ? dropHue_ : dropHue2_, 0.9f * monoG_); // a spot per color
        c = c * (0.6f / std::max(0.05f, lum(c)));
        L.color = c * (side_[i] * (0.35f + 0.25f * drop_) * worldOn);
        L.reach = 30.f;
        L.height = 3.f;
        L.nearW = 0.f;
        L.spread = 0.6f;
        L.sub = 4;
        L.rank = 1.f * side_[i];
        lights_.push_back(L);
    }
    {
        // a drop: the stack's brightest pieces are lights with shadows, the others glow without (cheap). Which ones
        // cast shadows is eased (each piece's share crossfades between its shadowed light and its glow), so that
        // nothing pops when the order changes; at most as many as the free light slots.
        std::vector<const PieceInfo*> lit;
        for (const PieceInfo& q : pieces_)
            if (q.level > 0.01f) lit.push_back(&q);
        std::sort(lit.begin(), lit.end(), [](const PieceInfo* a, const PieceInfo* b) { return a->level > b->level; });
        const int K = std::max(0, std::min(5, MAXL - 1 - (int)lights_.size()));
        std::vector<std::pair<int, float>> nw;
        for (size_t i = 0; i < lit.size(); i++) {
            float w = 0.f;
            for (auto& e : shadowW_)
                if (e.first == lit[i]->id) w = e.second;
            w = approach(w, (int)i < K ? 1.f : 0.f, 4.f, f.paused ? 0.f : dt);
            nw.push_back({lit[i]->id, w});
        }
        shadowW_ = nw;
        for (size_t i = 0; i < lit.size(); i++) {
            const PieceInfo* q = lit[i];
            const float w = nw[i].second;
            const vec3 lc = lightOf(q->type, q->mono) * (q->level * power * 0.3f);
            if (w > 0.01f) {
                LightDef L;
                for (int k = 0; k < q->n; k++) {
                    const vec3 wp = layout::cellPos((float)q->cells[k][0], q->cells[k][1] - game.rowOffset(q->cells[k][1]));
                    L.pts.push_back(v2(wp.x, wp.y));
                    L.own[k][0] = q->cells[k][0];
                    L.own[k][1] = q->cells[k][1];
                }
                L.color = lc * w;
                L.reach = 3.5f;
                L.nearW = 0.85f;
                L.height = 1.1f;
                L.spread = 0.f;
                L.sub = 1;
                L.rank = 0.01f; // last: if the slots run out, these become glows
                lights_.push_back(L);
            }
            if (w < 0.99f && (int)glows_.size() < MAXG) glows_.push_back({q->c, lc * ((float)q->n * 0.85f * (1.f - w)), 1.1f});
        }
    }
    std::stable_sort(lights_.begin(), lights_.end(), [](const LightDef& a, const LightDef& b) { return a.rank > b.rank; });

    // ---- The light pass.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, lightFbo_);
    glViewport(0, 0, lightW_, lightH_);
    glUseProgram(progLight_);
    set4f(progLight_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    setRowUniforms(progLight_);
    {
        float col[MAXL * 4] = {}, par[MAXL * 4] = {}, par2[MAXL * 4] = {}, own[MAXL * 8], pts[MAXP * 2] = {}, aim[MAXL * 4] = {};
        for (float& o : own) o = -1.f;
        int nl = 0, np = 0;
        for (const LightDef& L : lights_) {
            if (lum(L.color) < 0.002f) continue;
            if (nl >= MAXL || np + (int)L.pts.size() > MAXP) {
                // no slot left: it still shines, without shadows (never dropped: that would pop)
                if (L.coneOut <= 0.f && (int)glows_.size() < MAXG && !L.pts.empty()) {
                    vec2 c = v2(0.f, 0.f);
                    for (const vec2& p : L.pts) { c.x += p.x / L.pts.size(); c.y += p.y / L.pts.size(); }
                    glows_.push_back({c, L.color * ((float)L.pts.size() * L.nearW), L.height});
                }
                continue;
            }
            col[nl * 4 + 0] = L.color.x; col[nl * 4 + 1] = L.color.y; col[nl * 4 + 2] = L.color.z; col[nl * 4 + 3] = L.reach;
            par[nl * 4 + 0] = (float)np; par[nl * 4 + 1] = (float)L.pts.size(); par[nl * 4 + 2] = (float)L.sub; par[nl * 4 + 3] = L.spread;
            par2[nl * 4 + 0] = L.height; par2[nl * 4 + 1] = L.nearW;
            aim[nl * 4 + 0] = L.aim.x; aim[nl * 4 + 1] = L.aim.y; aim[nl * 4 + 2] = L.coneOut; aim[nl * 4 + 3] = L.coneIn;
            for (int k = 0; k < 4; k++) { own[nl * 8 + k * 2] = (float)L.own[k][0]; own[nl * 8 + k * 2 + 1] = (float)L.own[k][1]; }
            for (const vec2& p : L.pts) { pts[np * 2] = p.x; pts[np * 2 + 1] = p.y; np++; }
            nl++;
        }
        set1i(progLight_, "uNL", nl);
        glUniform4fv(ugl::U(progLight_, "uLColor"), MAXL, col);
        glUniform4fv(ugl::U(progLight_, "uLParam"), MAXL, par);
        glUniform4fv(ugl::U(progLight_, "uLParam2"), MAXL, par2);
        glUniform4fv(ugl::U(progLight_, "uLOwn"), MAXL * 2, own);
        glUniform2fv(ugl::U(progLight_, "uPts"), MAXP, pts);
        glUniform4fv(ugl::U(progLight_, "uLAim"), MAXL, aim);
        float gp[MAXG * 4] = {}, gc[MAXG * 4] = {};
        const int ng = std::min((int)glows_.size(), (int)MAXG);
        for (int i = 0; i < ng; i++) {
            gp[i * 4] = glows_[i].p.x; gp[i * 4 + 1] = glows_[i].p.y; gp[i * 4 + 2] = glows_[i].r;
            gc[i * 4] = glows_[i].c.x; gc[i * 4 + 1] = glows_[i].c.y; gc[i * 4 + 2] = glows_[i].c.z;
        }
        set1i(progLight_, "uNG", ng);
        glUniform4fv(ugl::U(progLight_, "uGPos"), MAXG, gp);
        glUniform4fv(ugl::U(progLight_, "uGCol"), MAXG, gc);
    }
    ugl::drawFullscreen();

    // ---- The scene (HDR): the room, the blocks, the glows.
    const float ltx = 1.f / lightW_, lty = 1.f / lightH_;
    // (in a drop the room's fill takes the drop's color too)
    const vec3 ambC = lerp(ambient_ * (1.f / std::max(0.02f, lum(ambient_))), (dropHue_ * (1.f / std::max(0.05f, lum(dropHue_))) + dropHue2_ * (1.f / std::max(0.05f, lum(dropHue2_)))) * 0.5f, 0.55f * monoG_);
    const vec3 amb = ambC * (0.035f * ambK);
    sceneT_.bind();
    glUseProgram(progScene_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, lightTex_);
    set1i(progScene_, "uLight", 0);
    set2f(progScene_, "uLightTexel", ltx, lty);
    set4f(progScene_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set1f(progScene_, "uPx", px_);
    set3f(progScene_, "uWall", wall_);
    set3f(progScene_, "uFloor", floor_);
    set3f(progScene_, "uRail", rail_);
    set3f(progScene_, "uAmbient", amb);
    set1f(progScene_, "uFloorY", FLOOR_Y);
    set1f(progScene_, "uBridge", smoothstepf(0.f, 1.f, bridge));
    set3f(progScene_, "uBridgeColor", bridge_.color);
    setRowUniforms(progScene_);
    ugl::drawFullscreen();

    blocks_.clear();
    buildBlocks(game, ember);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(progBlock_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, lightTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, dirTex_);
    glActiveTexture(GL_TEXTURE0);
    set1i(progBlock_, "uLight", 0);
    set1i(progBlock_, "uLightDir", 1);
    set2f(progBlock_, "uLightTexel", ltx, lty);
    set2f(progBlock_, "uRes", (float)width_, (float)height_);
    setMat(progBlock_, "uVP", vp_);
    set1f(progBlock_, "uPx", px_);
    set3f(progBlock_, "uAmbient", amb);
    set1f(progBlock_, "uAmbBlock", 3.f);
    set3f(progBlock_, "uGhost", ghost_);
    set1f(progBlock_, "uBridge", smoothstepf(0.f, 1.f, bridge));
    set3f(progBlock_, "uBridgeInk", bridge_.ink);
    drawInst(blocks_);

    // the glows: a faint halo around the lamps (light in the air), the contacts, the gauge
    fx_.clear();
    if (game.hasPiece() && !game.over()) {
        const Piece& p = game.piece();
        int cells[4][2];
        game.pieceCells(p, cells);
        const float dx = game.pieceVisualX() - p.x, dy = game.pieceVisualY() - p.y;
        // its bloom: a tight bright halo on each cell and a wide faint one around the piece
        const vec3 c = lightOf(p.type, monoG_) * (0.09f * power * pieceOn_);
        vec2 ctr = v2(0.f, 0.f);
        for (int i = 0; i < 4; i++) {
            const vec3 w = layout::cellPos(cells[i][0] + dx, cells[i][1] + dy);
            ctr.x += 0.25f * w.x;
            ctr.y += 0.25f * w.y;
            fx_.push_back({{w.x, w.y, 0.f, 0.f}, {1.25f, 1.f, 0.f, 0.f}, {c.x, c.y, c.z, 0.f}, {0.f, 0.f, 0.f, 0.f}});
        }
        const vec3 c2 = c * 0.45f;
        fx_.push_back({{ctr.x, ctr.y, 0.f, 0.f}, {3.6f, 1.f, 0.f, 0.f}, {c2.x, c2.y, c2.z, 0.f}, {0.f, 0.f, 0.f, 0.f}});
    }
    for (const Contact& ct : contacts_) {
        const float u = ct.t / ct.life;
        const float k = ct.power * smoothstepf(0.f, 0.08f, u) * (1.f - u) * (1.f - u) * worldOn;
        const vec3 c = ct.c * 0.9f;
        fx_.push_back({{ct.a.x, ct.a.y, ct.b.x, ct.b.y}, {0.16f + 0.1f * u, k, 0.f, 0.f}, {c.x, c.y, c.z, 1.f}, {0.f, 0.f, 0.f, 0.f}});
    }
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const bool ready = game.bonusReady(), active = game.bonusActive();
        const float fill = active ? saturate(game.bonusLeft() / Game::BONUS_SECONDS) : saturate(gaugeShown_);
        const vec3 c = lerp(accent_, bridge_.ink, bridge) * 0.55f * (1.f - 0.4f * dimT_);
        fx_.push_back({{gc.x, gc.y, layout::GAUGE_R, 0.035f}, {0.f, 0.f, ready ? 1.f : fill, ready ? 1.f : 0.f}, {c.x, c.y, c.z, 2.f}, {0.f, 0.f, 0.f, 0.f}});
    }
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(progFx_);
    setMat(progFx_, "uVP", vp_);
    set1f(progFx_, "uPx", px_);
    drawInst(fx_);
    glDisable(GL_BLEND);

    // ---- The final pass, then the HUD.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progFinal_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneT_.tex);
    set1i(progFinal_, "uScene", 0);
    const float fade = (1.f - 0.5f * dimT_) * saturate(f.fade);
    set1f(progFinal_, "uMul", fade);
    ugl::drawFullscreen();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawHud(f.hud, f.panels, fade);
    glDisable(GL_BLEND);
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_lightsource() {
    static const UniverseEntry e{
        "LIGHTSOURCE", "a dim room where the falling piece is the light, its shadows cast by the stack",
        [] { return std::unique_ptr<Universe>(new LightsourceUniverse()); },
        // Darker songs with contrast suit it best (a light in the dark), any tempo.
        [](const SongLook& l) { return 0.7f + 0.3f * (1.f - l.brightness) + 0.2f * l.dynamics; },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
