// PRISM: glass cubes in the dark, crossed by slow beams of light.
//
// One world: a near-black room (a breath of depth in the backdrop, a dark polished floor under the well that
// mirrors the light faintly) where light is the subject. The pieces are cubes of optical glass, one cube per cell,
// delicately tinted per piece type: nearly invisible where no light falls (a faint rim, the chamfers catching a
// dim key light, the back edges seen through the glass: every cube stays readable), coming alive where a beam
// crosses them (the body glowing in its tint, the chamfers facing the light blazing, caustic veins inside).
//
// The light: one to three soft beams fall from out of frame through a faint haze (uneven along their length,
// dust glinting in them) and drift very slowly across the well (a minute or more to cross it). Each beam is a
// bundle of rays traced on the CPU through the glass in 2D: refraction at each face (the faces cut slightly off
// square, a hand-made glass), total internal reflection (light piping down through the stack), a Fresnel
// reflection spawning dimmer beams that bounce off the faces, absorption in the glass's tint (light leaving a rose
// cube is rose), pools of light where the rays reach the floor (and a dim reflection back up), glints where they
// cross a face. The light each cube receives lights it (smoothed, so nothing flickers).
//
// Staging: calm is one beam in a light haze; mid brings a second from the other side, a glint on a lit cube every
// bar; a chorus brings a third, low and raking, and the faintest fringes of color. A drop (the HUD's section, or
// the song's drive) is a rainbow: on its downbeat the beams swell (one swell that settles) and turn white, and the
// glass disperses them, each ray splitting into a spectrum that fans out of the cubes into rainbow bands across
// the room and rainbow pools on the floor, for the whole drop; a glint every two beats. Nothing pulses with the
// beat, the beams never move fast, the stack and the HUD never move.
//
// A lock flashes a glint where the piece touched down (a hard drop a brighter one with a few sparks along the
// contact); a cleared row's cubes vanish at once and the row becomes a short burst of light traced through the
// same glass (lighting the cubes around it, pooling on the floor, dispersed in a drop; at the real speed, even in
// slow motion); a Tetris throws the light into a full spectrum for a few seconds; slow motion is
// moonlight (cold beams, the haze and dust slowed); game over turns the stack to light, row by row from the top.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane (like FLUO).
#include "universes/prism/prism.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/prism/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Tenor Sans, SIL Open Font License, see OFL-TenorSans.txt.
const unsigned char FONT_TENOR[] = {
#include "universes/prism/TenorSans-Regular.ttf.inc"
};

namespace shaders = prism_shaders;
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
vec2 add(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 sub(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
vec2 mul(vec2 a, float s) { return {a.x * s, a.y * s}; }
float dot2(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
vec2 norm2(vec2 a) {
    const float l = std::sqrt(dot2(a, a));
    return l > 1e-8f ? mul(a, 1.f / l) : v2(0.f, -1.f);
}
vec2 reflect2(vec2 d, vec2 n) { return sub(d, mul(n, 2.f * dot2(d, n))); }
float lum(const vec3& c) { return 0.3f * c.x + 0.5f * c.y + 0.2f * c.z; }
vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }

// The variants: the room (backdrop, floor), the two lights, the HUD's text and accent, and the seven glasses
// (I O T S Z J L), each a delicate tint that still tells the pieces apart.
struct VariantDef {
    const char* name;
    unsigned back, floor, lightA, lightB, text, accent;
    unsigned tint[7];
};
const VariantDef VARIANTS[] = {
    // clear optical glass, pale tints, a neutral white light
    {"CRYSTAL", 0x07080b, 0x0d0e12, 0xfff3e4, 0xe4eeff, 0xe6e8ee, 0xfff1dc,
     {0x8fe6ff, 0xffe08a, 0xc9a2ff, 0x9ff5b8, 0xff9fb2, 0x8fa8ff, 0xffb878}},
    // a warm tungsten lamp, amber glass and saturated tints, a warm black
    {"TUNGSTEN", 0x0a0705, 0x120d09, 0xffc584, 0xffdcae, 0xf0e2d0, 0xffbe6a,
     {0x5fd6c8, 0xffcc4f, 0xcf86ff, 0x9fde66, 0xff6e5c, 0x7a98ff, 0xff9b40}},
    // moonlight: a cold blue light, jewel glass
    {"MOONLIGHT", 0x04060c, 0x090c16, 0xc4d8ff, 0xa6c0ff, 0xdbe4f4, 0xa9c6ff,
     {0x52d4ff, 0xe8e69a, 0xa47aff, 0x5ae0ae, 0xff6c9a, 0x5c7aff, 0xffae76}},
    // a rose light, pastel glass
    {"ROSE QUARTZ", 0x09060a, 0x100b11, 0xffd2e2, 0xe6d6ff, 0xf2e4ec, 0xffb3cd,
     {0xa8e6f2, 0xffd89c, 0xff9fe0, 0xc4ee98, 0xff7f8c, 0xaf9fff, 0xffae6c}},
    // a pale green light, sea glass
    {"AURORA", 0x030807, 0x08100e, 0xc6ffe6, 0xb8e8ff, 0xdcf0ea, 0x8fffd0,
     {0x66ecff, 0xf2ee7e, 0xb08cff, 0x5cff9a, 0xff7090, 0x6690ff, 0xffa65c}},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

// The beams: each falls from out of frame toward an aim point low in the well that drifts slowly from side to
// side, its angle swaying slowly too (periods of a minute or more).
struct BeamDef { float theta0, thetaAmp, xC, xAmp, yAim, width, period, phase; };
const BeamDef BEAMS[3] = {
    {0.40f, 0.12f, -0.5f, 4.4f, -5.5f, 1.8f, 74.f, 0.3f},  // from the upper left
    {-0.52f, 0.14f, 1.0f, 4.0f, -6.5f, 1.4f, 97.f, 2.1f},  // from the upper right
    {1.10f, 0.10f, -1.5f, 2.0f, -7.0f, 1.2f, 61.f, 4.0f},  // low and raking, from the left (the peaks)
};

constexpr float FLOOR_Y = -10.06f;
constexpr int RAYS = 11;          // rays per beam
constexpr int SPECTRUM = 6;       // rays a dispersed ray splits into
constexpr int MAX_SEGS = 7000;
constexpr float N_GLASS = 1.5f;
// the light buffer's gains: a shaft in the haze is faint, inside the glass brighter, a pool on the floor brighter
constexpr float AIR_GAIN = 0.045f, INSIDE_GAIN = 0.22f, POOL_GAIN = 0.2f;
constexpr float BURST_GAIN = 1.6f;    // a cleared row's rays in the air (brief)
constexpr float SPECTRAL_GAIN = 1.9f; // a dispersed ray in the air (the colors are dimmer than the white they split)
constexpr float FAN_K = 1.0f;         // how widely a spectrum fans out leaving the glass (radians per unit of index)
constexpr float TILT = 0.09f;         // how far off square a face is cut (radians, peak to peak)
// the spectrum, red (bent least) to violet; normalized at init so that the sum is white
vec3 SPEC[SPECTRUM] = {{1.f, 0.06f, 0.03f}, {1.f, 0.42f, 0.f}, {0.85f, 0.95f, 0.f},
                       {0.05f, 1.f, 0.2f},  {0.f, 0.4f, 1.f},  {0.5f, 0.f, 1.f}};
bool specNormalized = false;

float hashf(float x) {
    const float s = std::sin(x * 127.1f + 311.7f) * 43758.5453f;
    return s - std::floor(s);
}

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool PrismUniverse::init(int width, int height) {
    if (!specNormalized) {
        vec3 s(0.f);
        for (auto& c : SPEC) s += c;
        for (auto& c : SPEC) c = vec3(c.x / s.x, c.y / s.y, c.z / s.z);
        specNormalized = true;
    }
    progLight_ = compile(shaders::LIGHT_VS, shaders::LIGHT_FS, "prism light");
    progScene_ = compile(ugl::FULLSCREEN_VS, shaders::SCENE_FS, "prism scene");
    progCube_ = compile(shaders::CUBE_VS, shaders::CUBE_FS, "prism cube");
    progFinal_ = compile(ugl::FULLSCREEN_VS, shaders::FINAL_FS, "prism final");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "prism text");
    progPanel_ = compile(shaders::PANEL_VS, shaders::PANEL_FS, "prism panel");

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

    for (int i = 0; i < SLOTS; i++) {
        acc_[i] = lit_[i] = vec3(0.f);
        accDir_[i] = litDir_[i] = v2(0.f, 0.f);
    }
    for (auto& row : lockT_)
        for (auto& t : row) t = -9.f;
    buildFont(font_, FONT_TENOR);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// Glyphs baked as signed distance fields (they scale cleanly).
void PrismUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] prism font init failed\n");
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

void PrismUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    const float aspect = (float)width_ / height_;
    const layout::Camera cam = layout::camera(width_, height_);
    const float hh = cam.dist * std::tan(cam.fovDeg * 0.5f * PI / 180.f), hw = hh * aspect;
    viewH_ = v2(hw, hh);
    viewC_ = v2(cam.target.x, cam.target.y);
    vp_ = mat4::ortho(viewC_.x - hw, viewC_.x + hw, viewC_.y - hh, viewC_.y + hh, -1.f, 1.f);
    px_ = 2.f * hh / height_;
    lightT_.destroy();
    sceneT_.destroy();
    lightT_.create(std::max(1, width_ / 2), std::max(1, height_ / 2), GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    glBindTexture(GL_TEXTURE_2D, lightT_.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    sceneT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
}

PrismUniverse::~PrismUniverse() {
    lightT_.destroy();
    sceneT_.destroy();
    for (GLuint p : {progLight_, progScene_, progCube_, progFinal_, progText_, progPanel_}) glDeleteProgram(p);
    for (GLuint v : {instVao_, textVao_, panelVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, instVbo_, textVbo_, panelVbo_}) glDeleteBuffers(1, &b);
    if (font_.tex) glDeleteTextures(1, &font_.tex);
    delete (stbtt_fontinfo*)font_.info;
}

// =====================================================================================================
// Song, level
// =====================================================================================================

void PrismUniverse::setSong(const SongLook& t, float seconds) {
    if (t.variant >= 0) variant_ = t.variant % VARIANT_COUNT;
    else {
        // the key's hue picks the light; night songs (minor and dark) lean to moonlight or the aurora
        float h = std::fmod(t.hue / TAU, 1.f);
        if (h < 0) h += 1.f;
        static const int BY_HUE[6] = {0, 3, 1, 4, 2, 0};
        int v = BY_HUE[std::min(5, (int)(h * 6.f))];
        if (t.hasSong && t.minor && t.brightness < 0.42f && (t.seed % 3) != 0) v = (t.seed & 1) ? 2 : 4;
        if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
        variant_ = v;
    }
    rng_ = Rng(t.seed ^ 0x9819ull);
    if (seconds <= 0.f) palSet_ = false;
}

void PrismUniverse::setLevel(int level, float, float seconds, float impact) {
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
    lvDur_ = std::clamp(seconds, rising ? 1.0f + 0.8f * (1.f - impact) : 2.5f, 6.f);
}

void PrismUniverse::drop(float strength) {
    if (level_ >= 2) inDrop_ = true;
    dropWait_ = 0.f;
    dropT_ = 0.f;
    dropAmp_ = 0.6f + 0.4f * saturate(strength);
}

vec2 PrismUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints PrismUniverse::hints() const {
    const VariantDef& d = VARIANTS[variant_];
    TransitionHints h;
    h.background = ugl::toLinear(hex(d.back));
    h.ink = ugl::toLinear(hex(d.text)) * 0.8f;
    h.accent = ugl::toLinear(hex(d.lightA));
    h.light = false;
    h.styles = TS_INK | TS_LUMA | TS_REBUILD;
    return h;
}

// =====================================================================================================
// The glass
// =====================================================================================================

void PrismUniverse::buildGlass(const Game& g) {
    boxes_.clear();
    loose_.clear();
    for (auto& c : byCol_) c.clear();
    gx0_ = gy0_ = 1e9f;
    gx1_ = gy1_ = -1e9f;
    auto put = [&](vec2 c, int slot, int type, bool onGrid, int col) {
        GBox b;
        b.c = c;
        b.slot = slot;
        b.type = type;
        b.seed = hashf((float)slot * 1.618f + 0.37f);
        const int i = (int)boxes_.size();
        boxes_.push_back(b);
        if (onGrid && col >= 0 && col < Game::W) byCol_[col].push_back(i);
        else loose_.push_back(i);
        gx0_ = std::min(gx0_, c.x - 0.5f);
        gx1_ = std::max(gx1_, c.x + 0.5f);
        gy0_ = std::min(gy0_, c.y - 0.5f);
        gy1_ = std::max(gy1_, c.y + 0.5f);
    };
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            put(v2(p.x, p.y), y * Game::W + x, c.type, true, x);
        }
    if (g.hasPiece() && !g.over()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        const bool onGrid = std::fabs(dx) < 1e-3f;
        for (int i = 0; i < 4; i++) {
            if (cells[i][1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(cells[i][0] + dx, cells[i][1] + dy);
            put(v2(w.x, w.y), Game::W * Game::H + i, p.type, onGrid, cells[i][0]);
        }
    }
}

int PrismUniverse::boxAt(vec2 p) const {
    auto inside = [&](int i) {
        const GBox& b = boxes_[i];
        return std::fabs(p.x - b.c.x) < 0.5f && std::fabs(p.y - b.c.y) < 0.5f;
    };
    const int col = (int)std::floor(p.x + 5.f);
    if (col >= 0 && col < Game::W)
        for (int i : byCol_[col])
            if (inside(i)) return i;
    for (int i : loose_)
        if (inside(i)) return i;
    return -1;
}

// The nearest cube a ray in the air enters before tMax: its distance, its index and the face's outward normal.
bool PrismUniverse::hitGlass(vec2 o, vec2 d, float tMax, float& tBest, int& best, vec2& nrm) const {
    if (boxes_.empty()) return false;
    // clip to the glass's bounds first
    float t0 = 0.f, t1 = tMax;
    const float lo[2] = {gx0_, gy0_}, hi[2] = {gx1_, gy1_}, oo[2] = {o.x, o.y}, dd[2] = {d.x, d.y};
    for (int a = 0; a < 2; a++) {
        if (std::fabs(dd[a]) < 1e-7f) {
            if (oo[a] < lo[a] || oo[a] > hi[a]) return false;
            continue;
        }
        float ta = (lo[a] - oo[a]) / dd[a], tb = (hi[a] - oo[a]) / dd[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    best = -1;
    tBest = tMax;
    auto test = [&](int i) {
        const GBox& b = boxes_[i];
        float tn = -1e9f, tf = 1e9f;
        vec2 n{0.f, 0.f};
        const float bc[2] = {b.c.x, b.c.y};
        for (int a = 0; a < 2; a++) {
            if (std::fabs(dd[a]) < 1e-7f) {
                if (std::fabs(oo[a] - bc[a]) >= 0.5f) return;
                continue;
            }
            float ta = (bc[a] - 0.5f - oo[a]) / dd[a], tb = (bc[a] + 0.5f - oo[a]) / dd[a];
            float sgn = -1.f;
            if (ta > tb) { std::swap(ta, tb); sgn = 1.f; }
            if (ta > tn) {
                tn = ta;
                n = a == 0 ? v2(sgn, 0.f) : v2(0.f, sgn);
            }
            tf = std::min(tf, tb);
        }
        if (tn > 1e-4f && tn <= tf && tn < tBest) {
            tBest = tn;
            best = i;
            nrm = n;
        }
    };
    const float xa = o.x + d.x * t0, xb = o.x + d.x * t1;
    const int c0 = std::max(0, (int)std::floor(std::min(xa, xb) + 5.f)), c1 = std::min(Game::W - 1, (int)std::floor(std::max(xa, xb) + 5.f));
    for (int c = c0; c <= c1; c++)
        for (int i : byCol_[c]) test(i);
    for (int i : loose_) test(i);
    return best >= 0;
}

// =====================================================================================================
// The light
// =====================================================================================================

void PrismUniverse::emitShaft(vec2 a, vec2 b, float w0, float w1, vec3 c, float gain, float fade) {
    if (segs_ >= MAX_SEGS) return;
    segs_++;
    const float i1 = std::pow(w0 / std::max(w1, 1e-3f), 0.6f) * fade;
    light_.push_back({{a.x, a.y, b.x, b.y}, {w0, w1, gain, gain * i1}, {c.x, c.y, c.z, 0.f}, {0.f, 0.f, 0.f, 0.f}});
}

void PrismUniverse::emitGlint(vec2 p, vec3 c, float size) {
    if (glow_.size() > 600 || size < 0.02f) return;
    glow_.push_back({{p.x, p.y, 0.f, 0.f}, {size, 0.f, 0.5f, 0.f}, {c.x, c.y, c.z, 2.f}, {0.f, 0.f, 0.f, 0.f}});
}

// One ray through the room: straight in the air, refracted into a cube, piped by total internal reflection,
// refracted out; a part reflected at each face (a dimmer ray of its own), a part absorbed in the glass's tint;
// split into a spectrum at its first cube when the light disperses. Reaching the floor, it pools there and a
// little of it is reflected back up.
void PrismUniverse::trace(Ray first) {
    stack_.clear();
    stack_.push_back(first);
    const float x0 = viewC_.x - viewH_.x - 2.f, x1 = viewC_.x + viewH_.x + 2.f;
    const float y0 = viewC_.y - viewH_.y - 2.f, y1 = viewC_.y + viewH_.y + 2.f;
    while (!stack_.empty() && segs_ < MAX_SEGS) {
        Ray r = stack_.back();
        stack_.pop_back();
        for (int step = 0; step < 28 && segs_ < MAX_SEGS; step++) {
            const float I = lum(r.c);
            if (I < 0.003f) break;
            if (r.box < 0) {
                // ---- in the air: to the view's edge, the floor or the next cube
                float tv0 = -1e9f, tv1 = 1e9f;
                {
                    const float lo[2] = {x0, y0}, hi[2] = {x1, y1}, oo[2] = {r.o.x, r.o.y}, dd[2] = {r.d.x, r.d.y};
                    for (int a = 0; a < 2; a++) {
                        if (std::fabs(dd[a]) < 1e-7f) continue;
                        float ta = (lo[a] - oo[a]) / dd[a], tb = (hi[a] - oo[a]) / dd[a];
                        if (ta > tb) std::swap(ta, tb);
                        tv0 = std::max(tv0, ta);
                        tv1 = std::min(tv1, tb);
                    }
                }
                if (tv1 <= 1e-4f || tv0 > tv1) break;
                float tEnd = tv1;
                bool floorHit = false;
                if (r.d.y < -1e-4f) {
                    const float tf = (FLOOR_Y - r.o.y) / r.d.y;
                    if (tf > 1e-4f && tf < tEnd) { tEnd = tf; floorHit = true; }
                }
                if (r.fall > 0.f) {
                    // a burst's ray reaches only so far
                    const float reach = 3.f / r.fall;
                    if (reach < tEnd) { tEnd = reach; floorHit = false; }
                }
                float tg;
                int b;
                vec2 n;
                const bool hit = hitGlass(r.o, r.d, tEnd, tg, b, n);
                if (hit) { tEnd = tg; floorHit = false; }
                const vec2 p = add(r.o, mul(r.d, tEnd));
                const float w1 = r.w + r.dw * tEnd;
                const float fade = r.fall > 0.f ? std::exp(-r.fall * tEnd) : 1.f;
                emitShaft(r.o, p, r.w, w1, r.c, r.g * (r.split ? AIR_GAIN * SPECTRAL_GAIN : AIR_GAIN * airK_), fade);
                r.c = r.c * (std::pow(r.w / std::max(w1, 1e-3f), 0.6f) * fade);
                r.w = w1;
                if (!hit) {
                    if (!floorHit || lum(r.c) < 0.003f) break;
                    // a pool of light on the polished floor, and a dim reflection back up
                    light_.push_back({{p.x, FLOOR_Y - 0.32f, 0.45f + r.w * 0.8f, 0.24f}, {0.f, 0.f, POOL_GAIN * std::min(r.g, 1.5f), 0.f},
                                      {r.c.x, r.c.y, r.c.z, 1.f}, {0.f, 0.f, 0.f, 0.f}});
                    if (r.depth >= 1) break;
                    r.d.y = -r.d.y;
                    r.c = r.c * 0.07f;
                    r.w *= 1.5f;
                    r.o = p;
                    r.depth++;
                    continue;
                }
                // ---- entering a cube: the face is cut slightly off square (hand-made glass)
                const GBox& B = boxes_[b];
                const float tilt = (hashf(B.seed * 91.3f + n.x * 3.1f + n.y * 7.7f) - 0.5f) * TILT;
                const vec2 nt = v2(n.x * std::cos(tilt) - n.y * std::sin(tilt), n.x * std::sin(tilt) + n.y * std::cos(tilt));
                const float cosi = std::max(0.05f, -dot2(r.d, nt));
                const float R0 = 0.04f, R = std::min(0.35f, 1.8f * (R0 + (1.f - R0) * std::pow(1.f - cosi, 5.f)));
                if (r.depth < 1 && I * R > 0.025f) {
                    Ray s = r;
                    s.w = r.w * 1.6f;
                    s.d = norm2(reflect2(r.d, nt));
                    if (dot2(s.d, n) <= 0.05f) s.d = norm2(reflect2(r.d, n));
                    s.c = r.c * R;
                    s.o = p;
                    s.depth = r.depth + 1;
                    if (!r.split && disp_ > 0.02f) {
                        // dispersing light: the face's reflection diffracts too, a spectrum fanning off the glass
                        const vec2 d0 = s.d;
                        for (int k = 0; k < SPECTRUM; k++) {
                            Ray q = s;
                            q.split = true;
                            q.n = N_GLASS + disp_ * (2.f * k / (SPECTRUM - 1) - 1.f);
                            const float ang = (q.n - N_GLASS) * FAN_K;
                            const float ca = std::cos(ang), sa = std::sin(ang);
                            q.d = v2(d0.x * ca - d0.y * sa, d0.x * sa + d0.y * ca);
                            if (dot2(q.d, n) <= 0.05f) q.d = d0;
                            q.c = s.c * SPEC[k];
                            q.dw = 0.01f + 0.07f * disp_;
                            stack_.push_back(q);
                        }
                    } else {
                        stack_.push_back(s);
                    }
                }
                emitGlint(p, r.c * 0.8f, 0.12f + 0.35f * std::sqrt(I));
                const vec3 cin = r.c * (1.f - R);
                auto refractIn = [&](float nIdx) {
                    const float eta = 1.f / nIdx;
                    const float k = 1.f - eta * eta * (1.f - cosi * cosi);
                    vec2 t = add(mul(r.d, eta), mul(nt, eta * cosi - std::sqrt(std::max(0.f, k))));
                    t = norm2(t);
                    if (dot2(t, n) > -0.05f) t = norm2(sub(t, mul(n, 0.2f)));
                    return t;
                };
                if (!r.split && disp_ > 0.02f) {
                    for (int k = 0; k < SPECTRUM; k++) {
                        Ray s = r;
                        s.split = true;
                        s.n = N_GLASS + disp_ * (2.f * k / (SPECTRUM - 1) - 1.f);
                        s.c = cin * SPEC[k];
                        s.d = refractIn(s.n);
                        s.o = p;
                        s.box = b;
                        stack_.push_back(s);
                    }
                    break;
                }
                r.d = refractIn(r.n);
                r.c = cin;
                r.o = p;
                r.box = b;
                continue;
            }
            // ---- inside a cube: to its far face
            const GBox& B = boxes_[r.box];
            const float tx = r.d.x > 1e-7f ? (B.c.x + 0.5f - r.o.x) / r.d.x : r.d.x < -1e-7f ? (B.c.x - 0.5f - r.o.x) / r.d.x : 1e9f;
            const float ty = r.d.y > 1e-7f ? (B.c.y + 0.5f - r.o.y) / r.d.y : r.d.y < -1e-7f ? (B.c.y - 0.5f - r.o.y) / r.d.y : 1e9f;
            const float t = std::max(0.f, std::min(tx, ty));
            const vec2 n = tx < ty ? v2(r.d.x > 0.f ? 1.f : -1.f, 0.f) : v2(0.f, r.d.y > 0.f ? 1.f : -1.f);
            const vec2 p = add(r.o, mul(r.d, t));
            emitShaft(r.o, p, r.w, r.w, r.c, INSIDE_GAIN * std::min(r.g, 1.5f));
            if (B.slot >= 0 && B.slot < SLOTS) {
                acc_[B.slot] += r.c * t;
                accDir_[B.slot] = add(accDir_[B.slot], mul(r.d, I * t));
            }
            // absorbed in the glass's tint
            {
                const vec3 tn = tintOf(B.type);
                const float m = std::max(tn.x, std::max(tn.y, tn.z));
                const float k = std::min(1.f, t * 0.45f);
                r.c = vec3(r.c.x * (1.f - k * (1.f - tn.x / m)), r.c.y * (1.f - k * (1.f - tn.y / m)), r.c.z * (1.f - k * (1.f - tn.z / m)));
            }
            const int nb = boxAt(add(p, mul(n, 0.02f)));
            if (nb >= 0 && nb != r.box) {
                r.o = p;
                r.box = nb;
                continue;
            }
            const float tilt = (hashf(B.seed * 91.3f + n.x * 3.1f + n.y * 7.7f) - 0.5f) * TILT;
            const vec2 nt = v2(n.x * std::cos(tilt) - n.y * std::sin(tilt), n.x * std::sin(tilt) + n.y * std::cos(tilt));
            const float cosi = std::max(0.f, dot2(r.d, nt));
            const float eta = r.n;
            const float k = 1.f - eta * eta * (1.f - cosi * cosi);
            vec2 out{0.f, 0.f};
            bool tir = k < 0.f;
            if (!tir) {
                out = norm2(add(mul(r.d, eta), mul(nt, std::sqrt(k) - eta * cosi)));
                if (dot2(out, n) < 0.05f) tir = true;
            }
            if (tir) {
                if (++r.bounces > 10) break;
                vec2 rd = norm2(reflect2(r.d, nt));
                if (dot2(rd, n) > -0.05f) rd = norm2(reflect2(r.d, n));
                r.d = rd;
                r.o = p;
                continue;
            }
            // leaving the glass: a part reflected back inside, the rest refracted out (a dispersed ray fans out)
            const float c2 = std::max(0.05f, dot2(out, nt));
            const float R0 = 0.04f, R = std::min(0.5f, 2.f * (R0 + (1.f - R0) * std::pow(1.f - c2, 5.f)));
            if (r.depth < 2 && I * R > 0.03f) {
                Ray s = r;
                s.d = norm2(reflect2(r.d, n));
                s.c = r.c * R;
                s.o = p;
                s.depth = r.depth + 1;
                stack_.push_back(s);
            }
            r.c = r.c * (1.f - R);
            emitGlint(p, r.c, 0.1f + 0.4f * std::sqrt(lum(r.c)));
            if (r.split) {
                // the spectrum fans out: each color bent its own way (the cube's chamfers acting as prisms; much
                // wider than a real cube would, which is the point)
                const float s = (n.x * out.y - n.y * out.x) >= 0.f ? 1.f : -1.f;
                const float ang = (r.n - N_GLASS) * FAN_K * s;
                const float ca = std::cos(ang), sa = std::sin(ang);
                vec2 o2 = v2(out.x * ca - out.y * sa, out.x * sa + out.y * ca);
                if (dot2(o2, n) > 0.08f) out = o2;
            }
            r.d = out;
            r.o = p;
            r.box = -1;
            if (r.split) r.dw = 0.01f + 0.07f * disp_;
        }
    }
}

// =====================================================================================================
// Game events
// =====================================================================================================

// A cleared row as a light source: each vanished cube sends a short burst of rays out from where it was, in every
// direction, traced like the beams (refracted and piped through the neighbouring cubes, which light up from it,
// pooling on the floor, dispersed into spectra in a drop). Nothing is drawn over the row itself: the light is only
// seen where its rays cross the haze, the glass and the floor.
void PrismUniverse::emitBurst(const Burst& b) {
    const float u = b.t / b.life;
    const float e = smoothstepf(0.f, 0.08f, u) * (1.f - u) * (1.f - u);
    if (e < 0.01f) return;
    const int DIRS = 8;
    for (const vec3& cell : b.cells) {
        const vec3 c = lerp(tintOf((int)cell.z), vec3(1.f), 0.5f) * (e * b.power * 0.7f);
        const float j = hashf(cell.x * 7.3f + cell.y * 1.9f) * TAU / DIRS;
        for (int k = 0; k < DIRS; k++) {
            const float a = j + k * TAU / DIRS;
            Ray r;
            r.d = v2(std::cos(a), std::sin(a));
            r.o = add(v2(cell.x, cell.y), mul(r.d, 0.2f));
            r.c = c;
            r.w = 0.2f;
            r.dw = 0.03f;
            r.depth = 1; // no Fresnel offshoots: a burst is brief and dense enough
            r.box = boxAt(r.o);
            r.bounces = 0;
            r.n = N_GLASS;
            r.split = false;
            r.g = BURST_GAIN;
            r.fall = 0.7f;
            trace(r);
        }
    }
}

void PrismUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::HardDrop:
        hardK_ = 1.f;
        break;
    case GameEvent::Lock: {
        // the cells keep the light the falling piece had
        float lowY = 1e9f, sx = 0.f;
        int nLow = 0, type = 0;
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            lockT_[c.y][c.x] = time_;
            const vec3 w = layout::cellPos((float)c.x, (float)c.y);
            for (int i = 0; i < 4; i++) {
                const int ps = Game::W * Game::H + i;
                if (pieceCell_[i][0] == c.x && pieceCell_[i][1] == c.y) {
                    lit_[c.y * Game::W + c.x] = lit_[ps];
                    litDir_[c.y * Game::W + c.x] = litDir_[ps];
                }
            }
            type = c.type;
            if (w.y < lowY - 0.1f) { lowY = w.y; sx = w.x; nLow = 1; }
            else if (std::fabs(w.y - lowY) < 0.1f) { sx += w.x; nLow++; }
        }
        for (int i = 0; i < 4; i++) lit_[Game::W * Game::H + i] = vec3(0.f);
        if (nLow > 0) {
            // one glint where the piece touched down (a hard drop: brighter, a few sparks along the contact)
            const vec2 at = v2(sx / nLow, lowY - 0.5f);
            const vec3 c = lerp(tintOf(type), vec3(1.f), 0.6f);
            flares_.push_back({at, c * (hardK_ > 0.f ? 2.2f : 1.2f), 0.f, hardK_ > 0.f ? 0.32f : 0.22f, hardK_ > 0.f ? 1.1f : 0.6f, 0.f});
            if (hardK_ > 0.f)
                for (int s = -1; s <= 1; s += 2)
                    for (int i = 0; i < 2; i++) {
                        Spark sp;
                        sp.p = v2(at.x + s * (0.4f + 0.5f * nLow * 0.5f), at.y + 0.05f);
                        sp.v = v2(s * rng_.range(3.f, 6.f), rng_.range(0.3f, 1.6f));
                        sp.c = c * 1.6f;
                        sp.t = 0.f;
                        sp.life = rng_.range(0.18f, 0.3f);
                        sp.delay = 0.f;
                        sp.w = 0.03f;
                        sparks_.push_back(sp);
                    }
        }
        hardK_ = 0.f;
        break;
    }
    case GameEvent::Clear: {
        bool cleared[Game::H] = {};
        for (auto& c : ev.cells)
            if (c.y >= 0 && c.y < Game::H) cleared[c.y] = true;
        const bool tetris = ev.count >= 4;
        // the cubes vanish at once (the board has dropped them) and the row becomes a burst of light
        {
            Burst b;
            for (auto& c : ev.cells) {
                const vec3 w = layout::cellPos((float)c.x, (float)c.y);
                b.cells.push_back(vec3(w.x, w.y, (float)c.type));
            }
            b.power = tetris ? 1.5f : 1.f;
            b.life = tetris ? 0.6f : 0.42f;
            if (!b.cells.empty()) bursts_.push_back(b);
        }
        if (tetris) tetrisT_ = 0.f;
        // collapse the per-cell light like the board
        vec3 nl[Game::H][Game::W];
        vec2 nd[Game::H][Game::W];
        float nt[Game::H][Game::W];
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) { nl[y][x] = vec3(0.f); nd[y][x] = v2(0.f, 0.f); nt[y][x] = -9.f; }
        int dst = Game::H - 1;
        for (int y = Game::H - 1; y >= 0; y--) {
            if (cleared[y]) continue;
            for (int x = 0; x < Game::W; x++) {
                nl[dst][x] = lit_[y * Game::W + x];
                nd[dst][x] = litDir_[y * Game::W + x];
                nt[dst][x] = lockT_[y][x];
            }
            dst--;
        }
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) {
                lit_[y * Game::W + x] = nl[y][x];
                litDir_[y * Game::W + x] = nd[y][x];
                lockT_[y][x] = nt[y][x];
            }
        break;
    }
    case GameEvent::TopOut: {
        // game over: the stack goes up in light, row by row from the top down
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (int y = top; y < Game::H; y++) {
            Burst b;
            for (auto& c : ev.cells)
                if (c.y == y) {
                    const vec3 w = layout::cellPos((float)c.x, (float)c.y);
                    b.cells.push_back(vec3(w.x, w.y, (float)c.type));
                }
            b.power = 0.6f;
            b.delay = (y - top) * 0.07f;
            if (!b.cells.empty()) bursts_.push_back(b);
        }
        for (int i = 0; i < SLOTS; i++) lit_[i] = vec3(0.f);
        break;
    }
    default: break;
    }
}

// =====================================================================================================
// The play: cubes, ghost, previews
// =====================================================================================================

void PrismUniverse::cubeInst(vec2 pos, float scale, int type, float alpha, float kind, int slot, float flash) {
    if (alpha <= 0.003f) return;
    const vec3 t = tintOf(type);
    vec3 l(0.f);
    vec2 d{0.f, 0.f};
    float amb = 1.f;
    if (slot >= 0 && slot < SLOTS) {
        l = lit_[slot];
        d = litDir_[slot];
    } else {
        // previews and hold: softly lit from above
        l = light_a_ * 0.22f;
        d = v2(0.4f, -0.9f);
        amb = 1.8f;
    }
    if (kind > 1.5f) amb = 1.5f;
    const float seed = slot >= 0 ? hashf(slot * 0.731f + 2.3f) : hashf(pos.x * 3.1f + pos.y);
    cubes_.push_back({{pos.x, pos.y, scale, alpha}, {t.x, t.y, t.z, kind}, {l.x, l.y, l.z, flash}, {d.x, d.y, seed, amb}});
}

void PrismUniverse::buildCubes(const Game& g) {
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            const float age = time_ - lockT_[y][x];
            const float flash = age >= 0.f && age < 0.2f ? 0.25f * (1.f - age / 0.2f) : 0.f;
            cubeInst(v2(p.x, p.y), 1.f, c.type, 1.f, 0.f, y * Game::W + x, flash);
        }
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
                cubeInst(v2(w.x, w.y), 1.f, p.type, 1.f, 1.f, -1, 0.f);
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (int i = 0; i < 4; i++) {
            if (cells[i][1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(cells[i][0] + dx, cells[i][1] + dy);
            cubeInst(v2(w.x, w.y), 1.f, p.type, 1.f, 2.f, Game::W * Game::H + i, 0.f);
        }
    }
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
            cubeInst(pos, sc, type, alpha, 0.f, -1, 0.f);
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.35f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float PrismUniverse::textWidth(const std::string& s, float size, float track) const {
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

void PrismUniverse::textQuads(const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const {
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

void PrismUniverse::drawText(const std::vector<float>& q, const vec3& srgb, float alpha, float bias, float soft) {
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

// The HUD in PRISM's typography: Tenor Sans, widely tracked capitals, pale on the dark (each line on a soft halo
// of the room's black, so it reads across a beam); the section, the notices and the universe's name in the light's
// own color.
void PrismUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels, float mul) {
    const float bridge = saturate(bridge_.amount);
    const vec3 ink = lerp(text_, bridge_.ink, bridge) * mul, acc = lerp(accent_, bridge_.ink, bridge) * mul;
    const vec3 shadow = ugl::toSrgb(lerp(back_ * 0.5f, bridge_.color, bridge));
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
            drawText(q, shadow, a * 0.85f, 0.26f, 0.32f);
            drawText(q, c, a, bias, 0.f);
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            const float sz = base * 0.95f, tr = base * 0.32f, vsz = base * 0.72f, vtr = base * 0.22f;
            const float wName = textWidth(name, sz, tr), gap = base * 0.9f;
            const float wVar = var.empty() ? 0.f : textWidth(var, vsz, vtr);
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            q.clear();
            textQuads(name, x, h.y, sz, tr, q);
            drawText(q, shadow, a * 0.8f, 0.26f, 0.32f);
            drawText(q, accS, a, 0.04f, 0.f);
            if (!var.empty()) {
                q.clear();
                textQuads(var, x + wName + gap, h.y + (sz - vsz) * 0.75f, vsz, vtr, q);
                drawText(q, shadow, a * 0.75f, 0.26f, 0.32f);
                drawText(q, inkS, a * 0.75f, 0.02f, 0.f);
            }
            continue;
        }
        case HUD_TITLE: put(h.text, base * 1.05f, base * 0.02f, 0.f, inkS, saturate(h.alpha * 1.3f), 0.03f); continue;
        case HUD_INFO: put(h.text, base * 0.78f, base * 0.08f, 0.f, inkS, alpha * 0.65f, 0.01f); continue;
        case HUD_SECTION: put(h.text, base * 0.95f, base * 0.38f, 0.f, accS, saturate(0.2f + h.alpha * 1.9f), 0.04f); continue;
        case HUD_LABEL: put(h.text, base * 0.66f, base * 0.34f, 0.f, inkS, alpha * 0.55f, 0.01f); continue;
        case HUD_VALUE: put(h.text, base * 1.05f, base * 0.04f, 0.f, inkS, alpha, 0.03f); continue;
        case HUD_NOTICE: put(h.text, base * 1.0f, base * 0.12f, 0.f, accS, alpha, 0.03f); continue;
        case HUD_TOAST: put(h.text, base * 1.25f, base * 0.3f, -base * 0.1f, accS, alpha, 0.05f); continue;
        case HUD_TOAST_SUB: put(h.text, base * 0.85f, base * 0.12f, 0.f, inkS, alpha, 0.02f); continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) put(h.text, base * 1.1f, base * 0.16f, -base * 0.1f, inkS, alpha, 0.03f);
            else put(h.text, base * 0.85f, base * 0.05f, 0.f, h.accent ? accS : inkS, alpha, 0.02f);
            continue;
        default: put(h.text, base * 0.82f, base * 0.04f, 0.f, h.accent ? accS : inkS, alpha, 0.02f); continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void PrismUniverse::drawInst(GLuint prog, const std::vector<Inst>& v) {
    if (v.empty()) return;
    (void)prog;
    glBindVertexArray(instVao_);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Inst), v.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)v.size());
}

void PrismUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const VariantDef& d = VARIANTS[variant_];
        const float k = palSet_ ? 1.f - std::exp(-dt * 1.2f) : 1.f;
        palSet_ = true;
        back_ = lerp(back_, ugl::toLinear(hex(d.back)), k);
        floor_ = lerp(floor_, ugl::toLinear(hex(d.floor)), k);
        light_a_ = lerp(light_a_, ugl::toLinear(hex(d.lightA)), k);
        light_b_ = lerp(light_b_, ugl::toLinear(hex(d.lightB)), k);
        text_ = lerp(text_, ugl::toLinear(hex(d.text)), k);
        accent_ = lerp(accent_, ugl::toLinear(hex(d.accent)), k);
        for (int i = 0; i < 7; i++) tint_[i] = lerp(tint_[i], ugl::toLinear(hex(d.tint[i])), k);
    }
    // the section, from the HUD (a drop is a rainbow, a chorus white light)
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

    float swell = 0.f;
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
        rainbow_ = hot ? std::min(1.f, rainbow_ + dt / 0.6f) : std::max(0.f, rainbow_ - dt / 3.f);
        chorus_ = approach(chorus_, level_ >= 2 && !hot ? 1.f : 0.f, 1.2f, dt);
        kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
        // a glint on a lit cube, on the beat grid: every bar at mid, every two beats at a peak
        const float every = lv_ > 1.5f ? 2.f : 4.f;
        if (music.hasSong && lv_ > 0.6f) {
            const double slot = std::floor(music.beat / every);
            if (slot != lastHalfBeat_) {
                if (lastHalfBeat_ >= 0.0 && !boxes_.empty()) {
                    int best = -1;
                    float bl = 0.08f;
                    for (int tries = 0; tries < 12; tries++) {
                        const int i = rng_.irange(0, (int)boxes_.size() - 1);
                        const int s = boxes_[i].slot;
                        const float l = lum(lit_[s]);
                        if (l > bl) { bl = l; best = i; }
                    }
                    if (best >= 0) {
                        const GBox& b = boxes_[best];
                        const float cx = rng_.chance(0.5f) ? 0.44f : -0.44f, cy = rng_.chance(0.5f) ? 0.44f : -0.44f;
                        const vec3 c = rainbow_ > 0.5f ? SPEC[rng_.irange(0, SPECTRUM - 1)] * (float)SPECTRUM * 0.6f
                                                       : lerp(tintOf(b.type), vec3(1.f), 0.6f);
                        flares_.push_back({v2(b.c.x + cx, b.c.y + cy), c * 1.6f, 0.f, 0.6f, 0.75f + 0.35f * rainbow_, 0.f});
                    }
                }
                lastHalfBeat_ = slot;
            }
        }
        // sparks and flares at the real speed (a clear never hangs in slow motion)
        for (Spark& s : sparks_) {
            if (s.delay > 0.f) { s.delay -= dt; continue; }
            s.t += dt;
            s.v.y -= 9.f * dt;
            s.v = mul(s.v, std::exp(-1.5f * dt));
            s.p = add(s.p, mul(s.v, dt));
        }
        sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(), [](const Spark& s) { return s.t >= s.life; }), sparks_.end());
        for (Flare& fl : flares_) {
            if (fl.delay > 0.f) { fl.delay -= dt; continue; }
            fl.t += dt;
        }
        flares_.erase(std::remove_if(flares_.begin(), flares_.end(), [](const Flare& x) { return x.t >= x.life; }), flares_.end());
        for (Burst& b : bursts_) {
            if (b.delay > 0.f) b.delay -= dt;
            else b.t += dt;
        }
        bursts_.erase(std::remove_if(bursts_.begin(), bursts_.end(), [](const Burst& b) { return b.t >= b.life; }), bursts_.end());
        gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 2.5f, dt);
    }
    swell = dropT_ < 12.f ? smoothstepf(0.f, 0.35f, dropT_) * std::exp(-std::max(0.f, dropT_ - 0.35f) / 2.2f) * dropAmp_ * (inDrop_ ? 1.f : 0.f) : 0.f;

    // ---- The light: the beams' strengths and the dispersion, then the rays through the glass.
    const float lvm = smoothstepf(0.2f, 1.f, lv_), lvp = smoothstepf(1.1f, 2.f, lv_);
    const float tet = std::exp(-tetrisT_ / 1.6f) * (tetrisT_ < 8.f ? 1.f : 0.f);
    {
        // a drop is told by color (the dispersion), not by more light
        const float on[3] = {0.75f + 0.15f * lvm + 0.1f * lvp, 0.75f * lvm + 0.1f * lvp, 0.8f * lvp};
        for (int i = 0; i < 3; i++) beams_[i].on = approach(beams_[i].on, on[i], 1.2f, dt);
        const float dispT = 0.035f * chorus_ * lvp + 0.24f * rainbow_ + 0.2f * tet;
        disp_ = approach(disp_, dispT, rainbow_ > 0.5f ? 4.f : 2.f, dt);
        airK_ = 1.f - 0.75f * rainbow_;
    }
    buildGlass(game);
    if (game.hasPiece() && !game.over()) game.pieceCells(game.piece(), pieceCell_);
    else for (auto& c : pieceCell_) c[0] = c[1] = -99;
    for (int i = 0; i < SLOTS; i++) { acc_[i] = vec3(0.f); accDir_[i] = v2(0.f, 0.f); }
    light_.clear();
    glow_.clear();
    segs_ = 0;
    const float worldOn = (1.f - smoothstepf(0.f, 0.7f, bridge)) * (1.f - 0.4f * dimT_);
    for (int bi = 0; bi < 3; bi++) {
        const BeamDef& B = BEAMS[bi];
        const float on = beams_[bi].on * worldOn * (1.f + 0.25f * swell + 0.15f * tet);
        if (on < 0.01f) continue;
        const float w = TAU / B.period;
        const float th = B.theta0 + B.thetaAmp * std::sin(world_ * w * 0.73f + B.phase * 1.7f);
        const float ax = B.xC + B.xAmp * std::sin(world_ * w + B.phase);
        const vec2 d = v2(std::sin(th), -std::cos(th));
        const vec2 aim = v2(ax, B.yAim);
        const vec2 perp = v2(-d.y, d.x);
        vec3 col = bi == 1 ? light_b_ : light_a_;
        col = lerp(col, vec3(1.f, 0.97f, 0.93f), 0.6f * rainbow_);
        col = lerp(col, vec3(0.45f, 0.62f, 1.f), 0.7f * slow_);
        for (int k = 0; k < RAYS; k++) {
            const float u = 2.f * k / (RAYS - 1) - 1.f;
            const float g = std::exp(-u * u * 2.2f);
            Ray r;
            r.o = add(sub(aim, mul(d, 45.f)), mul(perp, u * B.width * 0.5f));
            r.d = d;
            r.c = col * (on * g);
            r.w = B.width / RAYS * 1.5f;
            r.dw = 0.f;
            r.depth = 0;
            r.box = -1;
            r.bounces = 0;
            r.n = N_GLASS;
            r.split = false;
            trace(r);
        }
    }
    // the cleared rows' bursts of light, through the same glass
    for (const Burst& b : bursts_)
        if (b.delay <= 0.f) emitBurst(b);
    // the light the cubes received, smoothed (a cube lights up and dims over a fraction of a second)
    {
        const float k = 1.f - std::exp(-dt * 10.f);
        for (int i = 0; i < SLOTS; i++) {
            const vec3 target = acc_[i] * 0.32f;
            lit_[i] = lerp(lit_[i], target, k);
            litDir_[i] = add(litDir_[i], mul(sub(accDir_[i], litDir_[i]), k));
        }
    }
    // sparks, flares and the gauge, at full resolution
    for (const Spark& s : sparks_) {
        if (s.delay > 0.f) continue;
        const float a = 1.f - smoothstepf(0.4f, 1.f, s.t / s.life);
        const vec2 tail = sub(s.p, mul(s.v, 0.035f));
        glow_.push_back({{s.p.x, s.p.y, tail.x, tail.y}, {s.w, s.w * 0.6f, a, a * 0.5f}, {s.c.x, s.c.y, s.c.z, 3.f}, {0.f, 0.f, 0.f, 0.f}});
    }
    for (const Flare& fl : flares_) {
        if (fl.delay > 0.f) continue;
        const float u = fl.t / fl.life;
        const float a = smoothstepf(0.f, 0.12f, u) * (1.f - smoothstepf(0.25f, 1.f, u));
        glow_.push_back({{fl.p.x, fl.p.y, 0.f, 0.f}, {fl.size * (0.8f + 0.3f * u), 0.f, a, 0.f}, {fl.c.x, fl.c.y, fl.c.z, 2.f}, {0.f, 0.f, 0.f, 0.f}});
    }
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        const bool ready = game.bonusReady(), active = game.bonusActive();
        const float fill = active ? saturate(game.bonusLeft() / 10.f) : saturate(gaugeShown_);
        const vec3 c = lerp(accent_, bridge_.ink, bridge) * 0.9f;
        glow_.push_back({{gc.x, gc.y, R, 0.04f}, {0.f, 0.f, ready ? 1.f : fill, ready ? 1.f : 0.f}, {c.x, c.y, c.z, 4.f}, {0.f, 0.f, 0.f, 0.f}});
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // ---- The light buffer (half resolution, additive), then its mipmaps (the scattered light).
    lightT_.bind();
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(progLight_);
    setMat(progLight_, "uVP", vp_);
    set1f(progLight_, "uPx", px_ * 2.f);
    drawInst(progLight_, light_);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, lightT_.tex);
    glGenerateMipmap(GL_TEXTURE_2D);

    // ---- The scene (HDR): the room lit by the light buffer.
    sceneT_.bind();
    glUseProgram(progScene_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, lightT_.tex);
    set1i(progScene_, "uLight", 0);
    set4f(progScene_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set1f(progScene_, "uPx", px_);
    set1f(progScene_, "uTime", world_);
    set3f(progScene_, "uBack", back_);
    set3f(progScene_, "uFloor", floor_);
    set3f(progScene_, "uHair", lerp(text_, light_a_, 0.5f) * 0.05f);
    set1f(progScene_, "uFloorY", FLOOR_Y);
    set1f(progScene_, "uDust", (0.6f + 0.4f * lvm) * (1.f - bridge));
    set1f(progScene_, "uHaze", 0.8f - 0.15f * rainbow_);
    set1f(progScene_, "uSpill", 0.05f);
    set4f(progScene_, "uColL", layout::LEFT_COLUMN[0], layout::LEFT_COLUMN[1], layout::LEFT_COLUMN[2], layout::LEFT_COLUMN[3]);
    set4f(progScene_, "uColR", layout::RIGHT_COLUMN[0], layout::RIGHT_COLUMN[1], layout::RIGHT_COLUMN[2], layout::RIGHT_COLUMN[3]);
    set1f(progScene_, "uBridge", smoothstepf(0.f, 1.f, bridge));
    set3f(progScene_, "uBridgeColor", bridge_.color);
    ugl::drawFullscreen();

    // ---- The glass cubes over it (premultiplied), then the glints, sparks and gauge (additive).
    const float mul = 1.f;
    cubes_.clear();
    buildCubes(game);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(progCube_);
    setMat(progCube_, "uVP", vp_);
    set1f(progCube_, "uPx", px_);
    set1f(progCube_, "uTime", world_);
    set2f(progCube_, "uKey", -0.6f, 0.8f);
    set1f(progCube_, "uAmbient", 1.f);
    set1f(progCube_, "uMul", mul);
    set1f(progCube_, "uBridge", smoothstepf(0.f, 1.f, bridge));
    set3f(progCube_, "uBridgeInk", bridge_.ink);
    drawInst(progCube_, cubes_);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(progLight_);
    setMat(progLight_, "uVP", vp_);
    set1f(progLight_, "uPx", px_);
    for (Inst& q : glow_)
        if (q.a2[3] < 3.5f) { q.a2[0] *= worldOn; q.a2[1] *= worldOn; q.a2[2] *= worldOn; }
    drawInst(progLight_, glow_);
    glDisable(GL_BLEND);

    // ---- The final pass, then the HUD.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progFinal_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneT_.tex);
    set1i(progFinal_, "uScene", 0);
    const float fade = (1.f - 0.55f * dimT_) * saturate(f.fade);
    set1f(progFinal_, "uMul", fade);
    set1f(progFinal_, "uExposure", 1.f);
    ugl::drawFullscreen();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawHud(f.hud, f.panels, fade);
    glDisable(GL_BLEND);
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_prism() {
    static const UniverseEntry e{
        "PRISM", "glass cubes in the dark, crossed by slow beams of light", [] { return std::unique_ptr<Universe>(new PrismUniverse()); },
        // Songs with air and contrast suit it best (the light in the dark, the rainbow on the drop).
        [](const SongLook& l) { return 0.65f + 0.25f * l.airWeight + 0.2f * (1.f - l.brightness) + 0.2f * l.dynamics; },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
