// RISOGRAPH: a two-ink risograph print.
//
// One world: the whole frame is a print of two spot inks on a paper stock (a third ink joins at the peaks). The
// pieces are flat ink shapes and screened tints; the background is soft: big flat shapes and broad gradients of
// ink density on the paper (a riso gradient, a fine grain, no visible screen), kept off the well and the text's
// side. Calm is a pale gradient disc on the paper; mid deepens it and the second ink's gradient across the sheet;
// the peak brings the third ink's shape and a broad bloom of it from the shapes' edge, which swells in on the drop
// (a third of a second in, slow to settle) and breathes with the kicks, by density only. Only the background's
// drums are out of register, slightly and slowly: the stack, the pieces and the HUD never move with the music.
// Lots of paper, little else; no scenery.
//
// The pieces are flat ink shapes, each type its own ink recipe (a solid ink, the overprint of both, a solid with
// the other ink's dots, a hatched tint with a keyline...), so the seven types read by color and pattern in every
// variant; the cells of one piece print as one clean shape and two pieces always keep a hairline of paper between
// them. A lock presses a soft blot of the second ink out under the piece; a cleared row is torn off as a strip of
// paper (ragged edges, its dots shrinking away); a Tetris stamps the third ink in one huge soft disc over the sheet;
// slow motion is a monotone print (one ink, the drums locked in register); game over runs the drum dry, the stack
// fading in streaks. A change of universe lifts the ink off the paper, leaving the stack plain.
//
// Rendering: the background goes into its own buffer (continuous densities), the stack, the marks and the HUD into
// the ink buffer (one channel per drum, screened where drawn); COMPOSITE_FS prints both on the paper, only the
// background with a registration offset (see shaders.hpp).
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane (like SUMI); the
// stack and the HUD are always printed in register.
#include "universes/risograph/risograph.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/registry.hpp"
#include "universes/risograph/shaders.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Space Mono (Bold and Regular), SIL Open Font License, see OFL-SpaceMono.txt.
const unsigned char FONT_BOLD[] = {
#include "universes/risograph/SpaceMono-Bold.ttf.inc"
};
const unsigned char FONT_REGULAR[] = {
#include "universes/risograph/SpaceMono-Regular.ttf.inc"
};

namespace shaders = risograph_shaders;
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

// The variants: an ink pair (A, the key: the darker or stronger ink, the HUD's; B, the second) and the third ink
// of the peaks (C), on white paper or, for two, on black paper where the inks lie like light. weakB: B alone does
// not stand out from the paper (yellow on white, purple on black), so the pieces always back it with A.
// Inks after the riso ink library (blue, fluorescent pink, yellow, teal, bright red, federal blue, sunflower...).
struct VariantDef {
    const char* name;
    unsigned paper, a, b, c;
    bool dark, weakB;
};
const VariantDef VARIANTS[] = {
    // papers: neutral stocks, never cream (a cool white, light grey, pale blue, pink and mint, two blacks)
    {"FLUO PINK & BLUE", 0xeef0f1, 0x0078bf, 0xff48b0, 0xffe800, false, false},
    {"TEAL & YELLOW", 0xe9eaeb, 0x00838a, 0xffe800, 0xff48b0, false, true},
    {"RED & FEDERAL BLUE", 0xe3eaf0, 0x3d5588, 0xf15060, 0xffb511, false, false},
    {"BLACK & FLUO PINK", 0xf3e5ea, 0x222222, 0xff48b0, 0x0078bf, false, false},
    {"VIOLET & GREEN", 0xe2eee8, 0x765ba7, 0x00a95c, 0xff7477, false, false},
    {"BURGUNDY & MINT", 0xe6e6e9, 0x914e72, 0x82d8d5, 0xffb511, false, true},
    {"YELLOW & PURPLE", 0x15161c, 0xffe800, 0x9d7ad2, 0xff48b0, true, true},
    {"AQUA & ORANGE", 0x1a1a1a, 0x5ec8e5, 0xff6c2f, 0xffe800, true, false},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

// The seven pieces (I O T S Z J L) as ink recipes: density of A, density of B, line screen (0 none, 1 on A, 2 on
// B), keyline (0 none, 1 in A, 2 in B). Two tables: both inks strong, or B backed by A (weakB).
const float RECIPE[2][7][4] = {
    {{0.f, 1.f, 0, 0},      // I: B solid
     {1.f, 0.f, 0, 0},      // O: A solid
     {1.f, 1.f, 0, 0},      // T: the overprint
     {0.42f, 1.f, 0, 0},    // S: B with A's dots
     {0.5f, 0.f, 1, 1},     // Z: A hatched, keyline
     {0.f, 0.55f, 0, 2},    // J: B dotted, keyline
     {1.f, 0.42f, 2, 0}},   // L: A with B's lines
    {{0.f, 1.f, 0, 1},      // I: B solid, an A keyline
     {1.f, 0.f, 0, 0},      // O: A solid
     {1.f, 1.f, 0, 0},      // T: the overprint
     {0.42f, 1.f, 0, 0},    // S: B with A's dots
     {0.5f, 0.f, 1, 1},     // Z: A hatched, keyline
     {0.62f, 0.f, 0, 0},    // J: A tint
     {1.f, 0.5f, 2, 0}},    // L: A with B's lines
};

// Compositions of the world's three shapes (B's big disc, A's screened band, C's shape): side (+1 the right),
// distance of the center from the screen's edge, y, size (radius or half extents),
// rotation, type (0 disc, 1 band, 2 half disc). The peak re-prints them from one composition to the next.
struct CompShape { float s, e, y, sx, sy, rot, type; };
const CompShape COMPS[4][3] = {
    {{1, 5.5f, 4.0f, 6.6f, 0, 0, 0}, {-1, 5.0f, 1.2f, 1.3f, 4.6f, 0.f, 1}, {1, 6.0f, -4.4f, 4.4f, 0, 0, 0}},
    {{1, 6.0f, -2.0f, 6.8f, 0, 0, 0}, {-1, 5.6f, 1.6f, 1.3f, 4.2f, 0.32f, 1}, {1, 5.0f, 5.4f, 4.6f, 0, 0.f, 2}},
    {{1, 5.2f, 2.6f, 6.4f, 0, 0, 0}, {-1, 6.0f, 1.0f, 1.1f, 4.8f, -0.18f, 1}, {-1, 6.4f, 0.4f, 4.0f, 0, 0, 0}},
    {{1, 5.8f, 4.2f, 6.6f, 0, 0, 0}, {-1, 4.8f, 0.8f, 1.4f, 4.4f, 0.14f, 1}, {1, 7.0f, -4.6f, 4.6f, 0, PI, 2}},
};

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool RisographUniverse::init(int width, int height) {
    progWorld_ = compile(ugl::FULLSCREEN_VS, shaders::WORLD_FS, "risograph world");
    progMark_ = compile(shaders::MARK_VS, shaders::MARK_FS, "risograph marks");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "risograph text");
    progPanel_ = compile(shaders::PANEL_VS, shaders::PANEL_FS, "risograph panel");
    progComp_ = compile(ugl::FULLSCREEN_VS, shaders::COMPOSITE_FS, "risograph print");

    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glGenVertexArrays(1, &markVao_);
    glGenBuffers(1, &markVbo_);
    glBindVertexArray(markVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, markVbo_);
    for (int i = 0; i < 5; i++) {
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

    buildFont(fontBold_, FONT_BOLD);
    buildFont(fontReg_, FONT_REGULAR);
    resize(width, height);
    composition(0, shape_);
    for (int i = 0; i < 3; i++) next_[i] = shape_[i];
    return glGetError() == GL_NO_ERROR;
}

// Glyphs baked as signed distance fields (they scale cleanly).
void RisographUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] risograph font init failed\n");
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

void RisographUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    const float aspect = (float)width_ / height_;
    const layout::Camera cam = layout::camera(width_, height_);
    const float hh = cam.dist * std::tan(cam.fovDeg * 0.5f * PI / 180.f), hw = hh * aspect;
    viewH_ = v2(hw, hh);
    viewC_ = v2(cam.target.x, cam.target.y);
    vp_ = mat4::ortho(viewC_.x - hw, viewC_.x + hw, viewC_.y - hh, viewC_.y + hh, -1.f, 1.f);
    px_ = 2.f * hh / height_;
    inkT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    bgT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    // the compositions are placed from the screen's edges
    composition(comp_, next_);
    for (int i = 0; i < 3; i++)
        if (reprintState_[i] == 0) shape_[i] = next_[i];
}

RisographUniverse::~RisographUniverse() {
    inkT_.destroy();
    bgT_.destroy();
    for (GLuint p : {progWorld_, progMark_, progText_, progPanel_, progComp_}) glDeleteProgram(p);
    for (GLuint v : {markVao_, textVao_, panelVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, markVbo_, textVbo_, panelVbo_}) glDeleteBuffers(1, &b);
    for (Font* f : {&fontBold_, &fontReg_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

void RisographUniverse::composition(int comp, Shape out[3]) const {
    for (int i = 0; i < 3; i++) {
        const CompShape& c = COMPS[comp & 3][i];
        const float s = c.s * side_;
        out[i] = {s * (viewH_.x - c.e), c.y + viewC_.y, c.sx, c.sy, c.rot * s, c.type};
        if (c.type == 2) out[i].rot = s > 0 ? c.rot + 0.4f : c.rot - 0.4f; // half discs lean toward the edge
    }
}

// =====================================================================================================
// Song, level
// =====================================================================================================

// The print from the song: night songs (dark, minor) sometimes go to black paper; otherwise the key's hue picks
// the ink pair (warm keys the warm pairs, cool keys the cool ones).
int RisographUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    const bool night = t.hasSong && t.minor && t.brightness < 0.45f;
    int v;
    if (night && (t.seed % 3) != 0) v = h < 0.5f ? 7 : 6;
    else {
        static const int BY_HUE[6] = {0, 2, 1, 4, 3, 5}; // red, orange-yellow, green, cyan-blue, blue, violet
        v = BY_HUE[std::min(5, (int)(h * 6.f))];
        if (t.hasSong && t.dynamics > 0.7f && (t.seed & 1)) v = 3; // punchy songs like black and pink
    }
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 5)) % VARIANT_COUNT;
    return v;
}

void RisographUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    rng_ = Rng(look.seed ^ 0x4150ull);
    // the drums' misregistration: a direction of their own per song, roughly diagonal
    dirB_ = rng_.range(0.35f, 1.05f) + (rng_.chance(0.5f) ? PI : 0.f);
    dirC_ = dirB_ + rng_.range(1.6f, 2.4f);
    if (seconds <= 0.f) {
        palSet_ = false;
        composition(comp_, next_);
        for (int i = 0; i < 3; i++) { shape_[i] = next_[i]; reprintState_[i] = 0; reprint_[i] = 1.f; }
    } else {
        // a new song in the same print: the shapes are re-printed for it
        composition(comp_, next_);
        for (int i = 0; i < 3; i++) reprintState_[i] = 1;
    }
}

void RisographUniverse::setLevel(int level, float, float seconds, float impact) {
    level = std::clamp(level, 0, 2);
    if (!hasLevel_ || seconds < 0.1f) {
        hasLevel_ = true;
        level_ = level;
        lv_ = lvFrom_ = lvTo_ = (float)level;
        lvT_ = 1.f;
        pres_[0] = 1.f;
        pres_[1] = level >= 1 ? 1.f : 0.f;
        pres_[2] = level >= 2 ? 1.f : 0.f;
        if (level < 2 && comp_ != 0) {
            comp_ = 0;
            composition(0, shape_);
            for (int i = 0; i < 3; i++) next_[i] = shape_[i];
        }
        return;
    }
    const bool rising = level > level_;
    level_ = level;
    lvFrom_ = lv_;
    lvTo_ = (float)level;
    lvT_ = 0.f;
    lvDur_ = std::clamp(seconds, rising ? 1.0f + 0.8f * (1.f - impact) : 2.5f, 6.f);
    if (level < 2 && comp_ != 0) { // leaving the peak: back to the calm composition, re-printed
        comp_ = 0;
        composition(0, next_);
        for (int i = 0; i < 3; i++) reprintState_[i] = 1;
    }
}

// The drop: the third ink's gradient blooms in over a third of a second and swells, then settles (nothing moves).
void RisographUniverse::drop(float strength) {
    dropT_ = 0.f;
    dropAmp_ = 0.6f + 0.4f * saturate(strength);
    lv_ = std::max(lv_, 1.6f);
}

void RisographUniverse::refresh(float strength) { kickTgt_ = std::max(kickTgt_, 0.5f * strength); }

void RisographUniverse::levelUp() { kickTgt_ = std::max(kickTgt_, 0.7f); }

vec2 RisographUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints RisographUniverse::hints() const {
    const VariantDef& d = VARIANTS[variant_];
    TransitionHints h;
    h.background = ugl::toLinear(hex(d.paper));
    h.ink = ugl::toLinear(hex(d.a));
    h.accent = ugl::toLinear(hex(d.b));
    h.light = !d.dark;
    h.styles = TS_INK | TS_LUMA | TS_CURTAIN; // ink lifting off, paper
    return h;
}

// =====================================================================================================
// Game events
// =====================================================================================================

// Same-piece neighbors of a board cell, as the 8-bit mask MARK_FS expects (L R D U, LD RD LU RU; y up).
int RisographUniverse::maskAt(const Game& g, int x, int y) const {
    const Cell& c = g.cell(x, y);
    const InkCell& ic = ink_[y][x];
    const bool known = ic.type == c.type && ic.id >= 0;
    const float off = g.rowOffset(y);
    auto same = [&](int nx, int ny) {
        if (nx < 0 || nx >= Game::W || ny < Game::HIDDEN || ny >= Game::H) return false;
        const Cell& o = g.cell(nx, ny);
        if (o.type != c.type || std::fabs(g.rowOffset(ny) - off) > 0.01f) return false;
        const InkCell& oc = ink_[ny][nx];
        const bool oknown = oc.type == o.type && oc.id >= 0;
        if (known && oknown) return oc.id == ic.id;
        return !known && !oknown; // a stack inherited from another universe: same types join
    };
    const int dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1}, dy[8] = {0, 0, 1, -1, 1, 1, -1, -1}; // game y grows down
    int m = 0;
    for (int i = 0; i < 8; i++)
        if (same(x + dx[i], y + dy[i])) m |= 1 << i;
    return m;
}

void RisographUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::HardDrop:
        lastHard_ = std::min(1.f, 0.5f + ev.count * 0.05f);
        break;
    case GameEvent::Lock: {
        const int id = pieceCounter_++;
        float sx = 0.f, by = 1e9f;
        int n = 0;
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            ink_[c.y][c.x] = {id, c.type, lastHard_};
            const vec3 w = layout::cellPos((float)c.x, (float)c.y);
            sx += w.x;
            by = std::min(by, w.y);
            n++;
        }
        if (n > 0) { // a soft blot of ink pressed out under the piece
            blotPos_ = v2(sx / n, by - 0.5f);
            blotT_ = 0.f;
            blotK_ = 0.22f + 0.25f * lastHard_;
        }
        lastHard_ = 0.f;
        break;
    }
    case GameEvent::Clear: {
        bool cleared[Game::H] = {};
        for (auto& c : ev.cells)
            if (c.y >= 0 && c.y < Game::H) cleared[c.y] = true;
        // torn off toward the side away from the piece that completed the rows
        const float dir = ev.x < 4.5f ? 1.f : -1.f;
        int order[Game::H];
        int n = 0;
        for (int y = Game::H - 1; y >= 0; y--)
            if (cleared[y]) order[y] = n++;
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const InkCell& ic = ink_[c.y][c.x];
            const bool known = ic.type == c.type && ic.id >= 0;
            auto same = [&](int x) {
                if (x < 0 || x >= Game::W) return false;
                const InkCell& o = ink_[c.y][x];
                return known ? o.id == ic.id : (o.id < 0 && o.type == c.type);
            };
            int mask = 0;
            if (same(c.x - 1)) mask |= 1;
            if (same(c.x + 1)) mask |= 2;
            Strip s;
            s.x = c.x; s.row = c.y; s.type = std::clamp(c.type, 0, 6); s.mask = mask;
            s.y = layout::cellPos(0.f, (float)c.y).y;
            s.t = 0.f;
            s.delay = order[c.y] * 0.035f;
            s.dir = dir;
            strips_.push_back(s);
        }
        if (ev.count >= 4) { // the Tetris: the third ink stamped over the whole sheet
            stampT_ = 0.f;
        }
        // collapse the bookkeeping like the board
        InkCell next[Game::H][Game::W];
        int dst = Game::H - 1;
        for (int y = Game::H - 1; y >= 0; y--) {
            if (cleared[y]) continue;
            for (int x = 0; x < Game::W; x++) next[dst][x] = ink_[y][x];
            dst--;
        }
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) ink_[y][x] = next[y][x];
        break;
    }
    case GameEvent::BonusStart:
        kickTgt_ = std::max(kickTgt_, 0.7f);
        break;
    case GameEvent::TopOut: {
        // game over: the drum runs dry, the stack fades in streaks from the top
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const InkCell& ic = ink_[c.y][c.x];
            const bool known = ic.type == c.type && ic.id >= 0;
            int mask = 0;
            const int dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1}, dy[8] = {0, 0, 1, -1, 1, 1, -1, -1};
            for (int i = 0; i < 8; i++) {
                const int nx = c.x + dx[i], ny = c.y + dy[i];
                if (nx < 0 || nx >= Game::W || ny < 0 || ny >= Game::H) continue;
                for (auto& o : ev.cells)
                    if (o.x == nx && o.y == ny && o.type == c.type &&
                        (known ? ink_[ny][nx].id == ic.id : ink_[ny][nx].id < 0)) { mask |= 1 << i; break; }
            }
            Dying d;
            d.x = c.x; d.type = std::clamp(c.type, 0, 6); d.mask = mask;
            d.y = layout::cellPos(0.f, (float)c.y).y;
            d.t = 0.f;
            d.delay = 0.25f + (c.y - top) * 0.07f;
            dying_.push_back(d);
        }
        for (auto& row : ink_)
            for (auto& c : row) c = InkCell{};
        break;
    }
    default: break;
    }
}

// =====================================================================================================
// Simulation
// =====================================================================================================

void RisographUniverse::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    worldTime_ += wdt;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 1.9f, lv_);

    // The shapes follow the level: B's disc always, A's band from mid, C's shape at the peak. They come and go by
    // their density, slowly; they never jump or re-compose.
    dropT_ += dt;
    pres_[0] = approach(pres_[0], 1.f, 1.f, dt);
    pres_[1] = approach(pres_[1], wm, 0.8f, dt);
    pres_[2] = approach(pres_[2], level_ >= 2 ? wp : 0.f, dropT_ < 1.f ? 3.f : 0.8f, dt);

    // The third ink's gradient: in with the peak (a fast but smooth bloom on the drop), out slowly with it.
    {
        const float target = level_ >= 2 ? wp : 0.f;
        const float hold = dropT_ < 3.f ? 1.f : 0.f;
        const float tgt = std::max(target, hold);
        bloom_ = approach(bloom_, tgt, tgt > bloom_ ? 6.f : 0.6f, dt);
    }

    for (int i = 0; i < 3; i++) { // a new song: the shapes fade out and back in at their new places
        if (reprintState_[i] == 1) {
            reprint_[i] = std::max(0.f, reprint_[i] - dt * 1.5f);
            if (reprint_[i] <= 0.f) { shape_[i] = next_[i]; reprintState_[i] = 2; }
        } else if (reprintState_[i] == 2) {
            reprint_[i] = std::min(1.f, reprint_[i] + dt * 1.f);
            if (reprint_[i] >= 1.f) reprintState_[i] = 0;
        }
    }
    drift_ += wdt * (0.02f + 0.05f * wm); // the ink's cloud drifts very slowly

    // The kicks breathe the ink's density (from mid, fully at the peak): ~0.2 s in, ~0.5 s out, never a jump.
    const float kick = std::max(music.kick, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    if (kick > 0.5f && prevKick_ <= 0.5f) kickTgt_ = std::max(kickTgt_, kick * (0.35f * wm + 0.65f * wp));
    prevKick_ = kick;
    kickTgt_ *= std::exp(-dt * 6.f);
    kickEnv_ = approach(kickEnv_, kickTgt_, kickTgt_ > kickEnv_ ? 9.f : 2.5f, dt);
    stampT_ += wdt;
    blotT_ += wdt;

    // The background's registration: a little off, drifting slowly (a period of several seconds); the stack and the
    // HUD always print in register.
    const float world = 1.f - saturate(bridge_.amount);
    const float lock = (1.f - slow_) * world;
    const float sway = std::sin(worldTime_ * 0.35f);
    const float mB = (0.03f + 0.015f * sway * wm) * lock;
    const float mC = (0.035f - 0.015f * sway * wm) * lock;
    offB_ = v2(std::cos(dirB_) * mB, std::sin(dirB_) * mB);
    offC_ = v2(std::cos(dirC_) * mC, std::sin(dirC_) * mC);

    // Effects age.
    for (auto& s : strips_) s.t += wdt;
    strips_.erase(std::remove_if(strips_.begin(), strips_.end(), [](const Strip& s) { return s.t > s.delay + 0.45f; }), strips_.end());
    for (auto& d : dying_) d.t += dt;
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return d.t > d.delay + 1.4f; }), dying_.end());
    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 2.5f, dt);
}

// =====================================================================================================
// Marks
// =====================================================================================================

void RisographUniverse::rule(vec2 a, vec2 b, float width, const vec4& ink) {
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    Inst q{{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, len * 0.5f + 2.f * px_, width * 0.5f + 2.f * px_},
           {std::atan2(dy, dx), 1.f, 0.f, 0.f},
           {ink.x, ink.y, ink.z, ink.w},
           {len * 0.5f, width * 0.5f, 0.f, 0.f},
           {0, 0, 0, 0}};
    inst_.push_back(q);
}

void RisographUniverse::ring(vec2 c, float r, float width, const vec4& ink, float discR, float discDens, const vec3& discInk) {
    const float h = r + width + 2.f * px_;
    inst_.push_back({{c.x, c.y, h, h}, {0.f, 2.f, 0.f, 0.f}, {ink.x, ink.y, ink.z, ink.w}, {r, width, discR, discDens},
                     {discInk.x, discInk.y, discInk.z, 0.f}});
}

void RisographUniverse::cell(vec2 pos, float scale, float rot, int type, int mask, float alpha, vec2 offA, vec2 offB,
                             float tear, float starve, bool ghost, float seed) {
    if (alpha <= 0.003f) return;
    const float* r = RECIPE[VARIANTS[variant_].weakB ? 1 : 0][std::clamp(type, 0, 6)];
    const float h = 0.65f * scale;
    inst_.push_back({{pos.x, pos.y, h, h}, {rot, 0.f, (float)mask, alpha}, {r[0], r[1], r[2], r[3]},
                     {tear, starve, ghost ? 1.f : 0.f, seed}, {offA.x, offA.y, offB.x, offB.y}});
}

// Cells this universe did not see lock (a stack inherited from another universe, a test board): grouped into
// pieces of up to four connected cells of the same type, so they print as piece shapes without fusing into blobs.
void RisographUniverse::adoptUnknown(const Game& g) {
    for (int y = Game::H - 1; y >= Game::HIDDEN; y--)
        for (int x = 0; x < Game::W; x++) {
            const int t = g.cell(x, y).type;
            if (t < 0 || (ink_[y][x].type == t && ink_[y][x].id >= 0)) continue;
            const int id = pieceCounter_++;
            int qx[4], qy[4], n = 0, head = 0;
            qx[n] = x; qy[n] = y; n++;
            ink_[y][x] = {id, t, 0.f};
            while (head < n && n < 4) {
                const int cx = qx[head], cy = qy[head];
                head++;
                const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, -1, 0, 1};
                for (int k = 0; k < 4 && n < 4; k++) {
                    const int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || nx >= Game::W || ny < Game::HIDDEN || ny >= Game::H) continue;
                    if (g.cell(nx, ny).type != t || (ink_[ny][nx].type == t && ink_[ny][nx].id >= 0)) continue;
                    if (std::fabs(g.rowOffset(ny) - g.rowOffset(cy)) > 0.01f) continue;
                    ink_[ny][nx] = {id, t, 0.f};
                    qx[n] = nx; qy[n] = ny; n++;
                }
            }
        }
}

void RisographUniverse::buildBoard(const Game& g) {
    adoptUnknown(g);
    const float wp = smoothstepf(1.1f, 1.9f, lv_);
    // Locked cells, always in register (the lock's reaction is a soft blot of ink in the background).
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            cell(v2(p.x, p.y), 1.f, 0.f, c.type, maskAt(g, x, y), 1.f, v2(0, 0), v2(0, 0));
        }
    // Cleared rows torn off: each strip lifts from its trailing end, slides away, its edges ripped and its dots
    // shrinking; gone in under half a second.
    for (const Strip& s : strips_) {
        const float t = s.t - s.delay;
        if (t < 0.f) {
            const vec3 p = layout::cellPos((float)s.x, (float)s.row);
            cell(v2(p.x, p.y), 1.f, 0.f, s.type, s.mask, 1.f, v2(0, 0), v2(0, 0));
            continue;
        }
        const float u = smoothstepf(0.f, 0.42f, t);
        const float ang = s.dir * 0.08f * u;
        const vec2 pivot = v2(-s.dir * 5.f, s.y);
        const vec3 cp = layout::cellPos((float)s.x, (float)s.row);
        const float rx = cp.x - pivot.x, ry = cp.y - pivot.y;
        const float ca = std::cos(ang), sa = std::sin(ang);
        const vec2 pos = v2(pivot.x + ca * rx - sa * ry + s.dir * 0.9f * u, pivot.y + sa * rx + ca * ry + 0.12f * u);
        const float tear = smoothstepf(0.f, 0.1f, t);
        const float fade = 1.f - smoothstepf(0.06f, 0.42f, t);
        cell(pos, 1.f, ang, s.type, s.mask, fade, v2(0, 0), v2(0.f, 0.f), std::max(tear, 0.01f), 0.f, false,
             (float)s.x + s.row * 37.f);
    }
    // Game over: the drum runs dry.
    for (const Dying& d : dying_) {
        const float k = smoothstepf(d.delay, d.delay + 1.1f, d.t);
        const vec3 p = layout::cellPos((float)d.x, 0.f);
        cell(v2(p.x, d.y), 1.f, 0.f, d.type, d.mask, 1.f, v2(0, 0), v2(0, 0), 0.f, 0.02f + k * 1.1f);
    }
    // The falling piece and its ghost (a keyline and a light tint of the piece's main ink).
    auto pieceMask = [](int cells[4][2], int i) {
        int m = 0;
        const int dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1}, dy[8] = {0, 0, 1, -1, 1, 1, -1, -1};
        for (int b = 0; b < 8; b++)
            for (int j = 0; j < 4; j++)
                if (j != i && cells[j][0] - cells[i][0] == dx[b] && cells[j][1] - cells[i][1] == dy[b]) m |= 1 << b;
        return m;
    };
    if (g.hasPiece() && !g.over()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (int i = 0; i < 4; i++) {
                if (gc[i][1] < Game::HIDDEN) continue;
                const vec3 w = layout::cellPos((float)gc[i][0], (float)gc[i][1]);
                cell(v2(w.x, w.y), 1.f, 0.f, p.type, pieceMask(gc, i), 0.85f, v2(0, 0), v2(0, 0), 0.f, 0.f, true);
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (int i = 0; i < 4; i++) {
            if (cells[i][1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(cells[i][0] + dx, cells[i][1] + dy);
            cell(v2(w.x, w.y), 1.f, 0.f, p.type, pieceMask(cells, i), 1.f, v2(0, 0), v2(0, 0));
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
            const vec2 pos = v2(center.x + (cells[i][0] - (minx + maxx) * 0.5f) * sc, center.y - (cells[i][1] - (miny + maxy) * 0.5f) * sc);
            cell(pos, sc, 0.f, type, pieceMask(cells, i), alpha, v2(0, 0), v2(0, 0));
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.35f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }

    // The well: crop marks at its corners and a registration target under it, printed by every drum (so the
    // misregistration shows right around the stack), the third drum only while its ink is on.
    const vec4 all(1.f, 1.f, wp * pres_[2], 1.f);
    const float X = 5.f, Y = 10.f, gap = 0.28f, len = 0.85f, w = 0.04f;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            rule(v2(sx * (X + gap), sy * Y), v2(sx * (X + gap + len), sy * Y), w, all);
            rule(v2(sx * X, sy * (Y + gap)), v2(sx * X, sy * (Y + gap + len * 0.6f)), w, all);
        }
    {
        const vec2 c = v2(0.f, -Y - 0.95f);
        ring(c, 0.26f, 0.035f, all, 0.f, 0.f, vec3(0.f));
        rule(v2(c.x - 0.45f, c.y), v2(c.x + 0.45f, c.y), 0.03f, all);
        rule(v2(c.x, c.y - 0.45f), v2(c.x, c.y + 0.45f), 0.03f, all);
    }
    // the trim lines of the well: hairlines in the key ink
    const vec4 trim(0.55f, 0.f, 0.f, 0.6f);
    rule(v2(-X - 0.06f, -Y - 0.06f), v2(-X - 0.06f, Y), 0.022f, trim);
    rule(v2(X + 0.06f, -Y - 0.06f), v2(X + 0.06f, Y), 0.022f, trim);
    rule(v2(-X - 0.06f, -Y - 0.06f), v2(X + 0.06f, -Y - 0.06f), 0.022f, trim);

    // The bonus gauge: a keyline circle whose disc fills as a deepening tint of B, solid B + C once ready.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        const bool ready = g.bonusReady(), active = g.bonusActive();
        const float fill = active ? saturate(g.bonusLeft() / 10.f) : saturate(gaugeShown_);
        ring(v2(gc.x, gc.y), R, 0.045f, vec4(1.f, 0.f, 0.f, 1.f), R * 0.84f, ready ? 1.f : fill * 0.85f,
             ready ? vec3(0.f, 1.f, 1.f) : vec3(0.f, 1.f, 0.f));
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float RisographUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
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

void RisographUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
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

void RisographUniverse::drawText(const Font& f, const std::vector<float>& q, const vec4& ink, float bias) {
    if (q.empty() || !f.ok) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    set4f(progText_, "uInk", ink.x, ink.y, ink.z, ink.w);
    set1f(progText_, "uBias", bias);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(float), q.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 4));
}

// The HUD in RISOGRAPH's typography: Space Mono, like a riso zine's colophon. Everything in the key ink; the
// section, the notices and the line under a celebration in the second ink; the celebrations' big words printed
// by both drums, so they show the registration.
void RisographUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    if (!panels.empty()) {
        std::vector<float> v;
        glUseProgram(progPanel_);
        set2f(progPanel_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(panelVao_);
        glBindBuffer(GL_ARRAY_BUFFER, panelVbo_);
        for (const HudRect& r : panels) {
            const float q[12] = {r.x, r.y, r.x + r.w, r.y, r.x + r.w, r.y + r.h, r.x, r.y, r.x + r.w, r.y + r.h, r.x, r.y + r.h};
            glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STREAM_DRAW);
            if (r.accent) { // a screened patch of the second ink
                glBlendEquation(GL_MAX);
                glUniform4f(ugl::U(progPanel_, "uColor"), 0.f, 0.3f * r.alpha, 0.f, 0.f);
            } else { // paper: the ink under it lifted
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
                glUniform4f(ugl::U(progPanel_, "uColor"), 0.f, 0.f, 0.f, std::min(1.f, r.alpha * 1.1f));
            }
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glBlendEquation(GL_MAX);
    }
    const float s = height_ / 1080.f;
    const vec4 key(1.f, 0.f, 0.f, 1.f), second(0.f, 1.f, 0.f, 1.f), both(1.f, 1.f, 0.f, 1.f);
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
        auto ink = [&](const vec4& c, float a) { return vec4(c.x * a, c.y * a, c.z * a, c.w * a); };
        switch (h.role) {
        case HUD_UNIVERSE: {
            // "RISOGRAPH" in the bold, the inks after it in the regular
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            const float sz = base * 0.95f, tr = base * 0.12f, vsz = base * 0.8f, vtr = base * 0.06f;
            const float wName = textWidth(fontBold_, name, sz, tr);
            const float gap = base * 0.9f;
            const float wVar = var.empty() ? 0.f : textWidth(fontReg_, var, vsz, vtr);
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            textQuads(fontBold_, name, x, h.y, sz, tr, q);
            drawText(fontBold_, q, ink(key, a), 0.02f);
            if (!var.empty()) {
                q.clear();
                textQuads(fontReg_, var, x + wName + gap, h.y + (sz - vsz) * 0.75f, vsz, vtr, q);
                drawText(fontReg_, q, ink(key, a * 0.85f), 0.02f);
            }
            continue;
        }
        case HUD_TITLE:
            place(fontBold_, h.text, base * 1.05f, 0.f);
            drawText(fontBold_, q, ink(key, saturate(h.alpha * 1.3f)), 0.02f);
            continue;
        case HUD_INFO:
            place(fontReg_, h.text, base * 0.78f, base * 0.04f);
            drawText(fontReg_, q, ink(key, saturate(alpha * 1.2f) * 0.85f), 0.02f);
            continue;
        case HUD_SECTION:
            place(fontBold_, h.text, base * 1.0f, base * 0.18f);
            drawText(fontBold_, q, ink(second, saturate(0.2f + h.alpha * 1.9f)), 0.02f);
            continue;
        case HUD_LABEL:
            place(fontReg_, h.text, base * 0.72f, base * 0.22f);
            drawText(fontReg_, q, ink(key, saturate(alpha * 1.2f) * 0.8f), 0.02f);
            continue;
        case HUD_VALUE:
            place(fontBold_, h.text, base * 1.02f, 0.f);
            drawText(fontBold_, q, ink(key, alpha), 0.02f);
            continue;
        case HUD_NOTICE:
            place(fontBold_, h.text, base * 1.0f, base * 0.05f);
            drawText(fontBold_, q, ink(second, alpha), 0.02f);
            continue;
        case HUD_TOAST:
            place(fontBold_, h.text, base * 1.3f, base * 0.12f, -base * 0.1f);
            drawText(fontBold_, q, ink(both, alpha), 0.02f);
            continue;
        case HUD_TOAST_SUB:
            place(fontReg_, h.text, base * 0.85f, base * 0.1f);
            drawText(fontReg_, q, ink(second, alpha), 0.02f);
            continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontBold_, h.text, base * 1.1f, base * 0.08f, -base * 0.1f);
                drawText(fontBold_, q, ink(key, alpha), 0.02f);
            } else {
                place(fontReg_, h.text, base * 0.85f, base * 0.03f);
                drawText(fontReg_, q, ink(h.accent ? second : key, alpha), 0.02f);
            }
            continue;
        default:
            place(fontReg_, h.text, base * 0.82f, base * 0.02f);
            drawText(fontReg_, q, ink(h.accent ? second : key, alpha), 0.02f);
            continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void RisographUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const VariantDef& d = VARIANTS[variant_];
        const float k = palSet_ ? 1.f - std::exp(-dt * 1.2f) : 1.f;
        palSet_ = true;
        paper_ = lerp(paper_, ugl::toLinear(hex(d.paper)), k);
        inkA_ = lerp(inkA_, ugl::toLinear(hex(d.a)), k);
        inkB_ = lerp(inkB_, ugl::toLinear(hex(d.b)), k);
        inkC_ = lerp(inkC_, ugl::toLinear(hex(d.c)), k);
        dark_ = k >= 1.f ? (d.dark ? 1.f : 0.f) : approach(dark_, d.dark ? 1.f : 0.f, 0.8f, dt);
    }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 1.4f, dt);
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.5f : 0.f), 3.f, dt);
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.35f, slow_);
    if (!f.paused) simulate(game, music, dt, wdt);
    const float bridge = saturate(bridge_.amount);
    const float world = 1.f - bridge;
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 1.9f, lv_);

    // ---- The background: the world's shapes and the sheet's gradients, as soft ink densities, in their own buffer.
    bgT_.bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    {
        float A[16], B[16], C[16];
        const float breath = kickEnv_; // 0..1, smooth
        const float dropSwell = dropT_ < 9.f ? smoothstepf(0.f, 0.35f, dropT_) * std::exp(-std::max(0.f, dropT_ - 0.35f) * 0.45f) * dropAmp_ : 0.f;
        const float tame = 1.f - 0.25f * dark_; // inks laid like light on black paper glow more: keep them lower
        const float dens[3] = {(0.2f + 0.18f * wm + 0.14f * wp + 0.08f * breath + 0.1f * dropSwell) * pres_[0] * tame,
                               0.f, // A's band: retired (it read as a grey bar); the key ink stays for the stack and the HUD
                               (0.55f + 0.12f * breath + 0.15f * dropSwell) * pres_[2] * tame};
        const float drum[3] = {1.f, 0.f, 2.f};
        const float grad[3] = {0.95f, 0.7f, 0.85f};
        for (int i = 0; i < 3; i++) {
            const Shape& s = shape_[i];
            // a slow float of the shapes from mid on (a period of ~30 s)
            const float fy = std::sin(worldTime_ * 0.21f + i * 2.1f) * 0.3f * wm;
            // the shapes swell a little with the kicks' breath (smooth, a few percent)
            const float sz = 1.f + 0.025f * breath * wp;
            A[i * 4] = s.x; A[i * 4 + 1] = s.y + fy; A[i * 4 + 2] = s.sx * sz; A[i * 4 + 3] = (s.type == 1 ? s.sy : s.sx) * sz;
            B[i * 4] = s.rot; B[i * 4 + 1] = s.type; B[i * 4 + 2] = drum[i];
            B[i * 4 + 3] = std::min(1.f, dens[i]) * reprint_[i] * world;
            // full density at the outer side, fading toward the well
            C[i * 4] = std::atan2(-(s.y - viewC_.y) * 0.4f, -s.x); C[i * 4 + 1] = grad[i]; C[i * 4 + 2] = 0.f;
            C[i * 4 + 3] = 0.f;
        }
        // the Tetris stamp: one huge soft disc of the third ink over the sheet, fading in and out
        const float st = stampT_;
        const float stamp = st < 0.25f ? smoothstepf(0.f, 0.25f, st) : 1.f - smoothstepf(0.45f, 2.2f, st);
        A[12] = 0.f; A[13] = 0.6f; A[14] = 12.5f; A[15] = 12.5f;
        B[12] = 0.f; B[13] = 0.f; B[14] = 2.f; B[15] = 0.4f * stamp * world * tame;
        C[12] = -PI * 0.5f; C[13] = 0.8f; C[14] = 0.f; C[15] = 0.f;
        glUseProgram(progWorld_);
        set4f(progWorld_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        glUniform4fv(ugl::U(progWorld_, "uShapeA"), 4, A);
        glUniform4fv(ugl::U(progWorld_, "uShapeB"), 4, B);
        glUniform4fv(ugl::U(progWorld_, "uShapeC"), 4, C);
        set1f(progWorld_, "uPx", px_);
        // the sheet: B's faint gradient from the shapes' side (deeper from mid), and the peak's bloom of C, which
        // swells on the drop and breathes with the kicks (density only)
        const float bloom = (0.20f + 0.10f * breath + 0.22f * dropSwell) * bloom_ * world * (1.f - 0.6f * slow_) * tame;
        const float wash = (0.03f + 0.1f * wm + 0.03f * breath) * world * tame;
        set1f(progWorld_, "uSide", (float)side_);
        set4f(progWorld_, "uWell", 0.f, 0.f, 5.f, 10.f);
        set4f(progWorld_, "uSheet", bloom, wash, std::atan2(-0.35f, 1.f * side_), drift_);
        const float bt = saturate(blotT_ / 0.45f);
        set4f(progWorld_, "uBlot", blotPos_.x, blotPos_.y, bt, bt < 1.f ? blotK_ * world * (1.f - 0.5f * slow_) : 0.f);
        ugl::drawFullscreen();
    }

    inkT_.bind();
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);

    // ---- Marks and pieces, the HUD: each drum's coverage, the most ink wins.
    inst_.clear();
    buildBoard(game);
    glEnable(GL_BLEND);
    glBlendEquation(GL_MAX);
    if (!inst_.empty()) {
        glUseProgram(progMark_);
        setMat(progMark_, "uVP", vp_);
        set1f(progMark_, "uPx", px_);
        glBindVertexArray(markVao_);
        glBindBuffer(GL_ARRAY_BUFFER, markVbo_);
        glBufferData(GL_ARRAY_BUFFER, inst_.size() * sizeof(Inst), inst_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)inst_.size());
    }
    drawHud(f.hud, f.panels);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_BLEND);

    // ---- The print.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progComp_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inkT_.tex);
    set1i(progComp_, "uInk", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bgT_.tex);
    set1i(progComp_, "uBg", 1);
    glActiveTexture(GL_TEXTURE0);
    const float ux = 0.5f / viewH_.x, uy = 0.5f / viewH_.y;
    set2f(progComp_, "uOffA", 0.f, 0.f);
    set2f(progComp_, "uOffB", offB_.x * ux, offB_.y * uy);
    set2f(progComp_, "uOffC", offC_.x * ux, offC_.y * uy);
    // slow motion: a monotone print, everything in the key ink (the second drum a lighter tone of it)
    const vec3 monoB = lerp(paper_, inkA_, 0.45f), monoC = lerp(paper_, inkA_, 0.3f);
    set3f(progComp_, "uPaper", paper_);
    set3f(progComp_, "uInkA", inkA_);
    set3f(progComp_, "uInkB", lerp(inkB_, monoB, slow_));
    set3f(progComp_, "uInkC", lerp(inkC_, monoC, slow_));
    set1f(progComp_, "uDark", dark_);
    set4f(progComp_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set1f(progComp_, "uGrain", 1.f);
    set1f(progComp_, "uBridge", smoothstepf(0.f, 1.f, bridge));
    set3f(progComp_, "uBridgeColor", bridge_.color);
    set3f(progComp_, "uBridgeInk", bridge_.ink);
    set1f(progComp_, "uDim", dimT_);
    set1f(progComp_, "uFade", saturate(f.fade));
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_risograph() {
    static const UniverseEntry e{
        "RISOGRAPH", "a two-ink risograph print", [] { return std::unique_ptr<Universe>(new RisographUniverse()); },
        // Punchy, rhythmic songs suit the drums best (mid to fast tempo, strong dynamics and bass).
        [](const SongLook& l) {
            return 0.65f + 0.35f * smoothstepf(95.f, 135.f, l.bpm) + 0.25f * l.dynamics + 0.15f * l.bassWeight;
        },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
