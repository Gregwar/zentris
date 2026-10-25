// SUMI: ink on washi paper.
//
// One world, one visual language. A flat sheet of paper; everything else is ink laid on it (multiplicative
// blend, so overlapping washes darken like real ink). The song's scene level is the intensity of the
// painting, not a different painting: calm is an almost empty sheet with a far mountain, mid brings mist
// and drifting ranges and a few plum petals, peak adds bold near mountains, the enso drawn around the board
// and the red sun. Beats are drops of ink blooming in wet paper. A scene change is a wash spreading from
// the board. The slow-motion bonus turns the print to its negative: white ink on black lacquer.
//
// The view is flat: an orthographic projection with the same scale as the layout's reference camera on the
// board plane, so the well, the previews and the HUD anchors sit where the layout contract puts them.
#include "universes/sumi/sumi.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "layout.hpp"
#include "stb_easy_font.h"
#include "ugl.hpp"
#include "universes/registry.hpp"
#include "universes/sumi/shaders.hpp"

namespace shaders = sumi_shaders;

namespace {

using ugl::U;
using ugl::set1f;
using ugl::set1i;
using ugl::set2f;
using ugl::set3f;
using ugl::set4f;
using ugl::setMat;

GLuint compileStage(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gl] %s %s shader error:\n%s\n", name, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
    }
    return s;
}



constexpr float WELL_HX = 5.f, WELL_HY = 10.f;

float hashf(float x) { float s = std::sin(x * 12.9898f) * 43758.5453f; return s - std::floor(s); }
vec2 v2(float x, float y) { return vec2{x, y}; }
vec2 add(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 mul(vec2 a, float k) { return {a.x * k, a.y * k}; }

// The ink variants: one per song, hand-designed. Each is a paper, a main ink and a second wash, an accent
// (seal, sun, petals, HUD highlights) and the seven piece inks (I O T S Z J L). The piece treatments never
// change (I solid, O dilute wash, T the accent, S hatched, Z the second ink, J stippled, L outlined), so the
// pieces stay readable in every variant. Negative variants are drawn as light ink on lacquer.
// To add one: append a line here and its name to the registry entry (end of file), and let pickVariant() choose it.
struct Variant {
    const char* name;
    vec3 paper, ink, ink2, accent;
    vec3 piece[7];
    bool negative;
    vec3 lacquer, ivory;
};
const Variant VARIANTS[] = {
    {"SUMI & SHU", {0.930f, 0.930f, 0.925f}, {0.105f, 0.094f, 0.088f}, {0.22f, 0.29f, 0.42f}, {0.784f, 0.251f, 0.165f},
     {{0.105f, 0.094f, 0.088f}, {0.16f, 0.15f, 0.15f}, {0.784f, 0.251f, 0.165f}, {0.105f, 0.094f, 0.088f},
      {0.22f, 0.29f, 0.42f}, {0.105f, 0.094f, 0.088f}, {0.105f, 0.094f, 0.088f}}, false, {}, {}},
    {"AIZOME & OCHRE", {0.930f, 0.930f, 0.925f}, {0.085f, 0.115f, 0.215f}, {0.30f, 0.40f, 0.52f}, {0.80f, 0.56f, 0.20f},
     {{0.085f, 0.115f, 0.215f}, {0.26f, 0.33f, 0.46f}, {0.80f, 0.56f, 0.20f}, {0.085f, 0.115f, 0.215f},
      {0.20f, 0.42f, 0.47f}, {0.085f, 0.115f, 0.215f}, {0.085f, 0.115f, 0.215f}}, false, {}, {}},
    {"SEPIA & CELADON", {0.930f, 0.930f, 0.925f}, {0.215f, 0.135f, 0.080f}, {0.40f, 0.52f, 0.46f}, {0.33f, 0.58f, 0.48f},
     {{0.215f, 0.135f, 0.080f}, {0.36f, 0.27f, 0.19f}, {0.33f, 0.58f, 0.48f}, {0.215f, 0.135f, 0.080f},
      {0.16f, 0.36f, 0.32f}, {0.215f, 0.135f, 0.080f}, {0.215f, 0.135f, 0.080f}}, false, {}, {}},
    {"MURASAKI & GOLD", {0.930f, 0.930f, 0.925f}, {0.185f, 0.085f, 0.165f}, {0.45f, 0.36f, 0.52f}, {0.80f, 0.63f, 0.24f},
     {{0.185f, 0.085f, 0.165f}, {0.40f, 0.31f, 0.44f}, {0.80f, 0.63f, 0.24f}, {0.185f, 0.085f, 0.165f},
      {0.42f, 0.20f, 0.46f}, {0.185f, 0.085f, 0.165f}, {0.185f, 0.085f, 0.165f}}, false, {}, {}},
    {"MATCHA & RUST", {0.930f, 0.930f, 0.925f}, {0.115f, 0.150f, 0.085f}, {0.42f, 0.48f, 0.32f}, {0.68f, 0.30f, 0.14f},
     {{0.115f, 0.150f, 0.085f}, {0.30f, 0.34f, 0.24f}, {0.68f, 0.30f, 0.14f}, {0.115f, 0.150f, 0.085f},
      {0.34f, 0.46f, 0.20f}, {0.115f, 0.150f, 0.085f}, {0.115f, 0.150f, 0.085f}}, false, {}, {}},
    {"KAKI & CHARCOAL", {0.930f, 0.930f, 0.925f}, {0.125f, 0.115f, 0.110f}, {0.33f, 0.35f, 0.37f}, {0.90f, 0.44f, 0.16f},
     {{0.125f, 0.115f, 0.110f}, {0.30f, 0.28f, 0.27f}, {0.90f, 0.44f, 0.16f}, {0.125f, 0.115f, 0.110f},
      {0.28f, 0.36f, 0.42f}, {0.125f, 0.115f, 0.110f}, {0.125f, 0.115f, 0.110f}}, false, {}, {}},
    // Negatives: designed as a print, shown reversed (ink -> silver, paper -> lacquer; hues kept).
    {"URUSHI & SILVER", {0.930f, 0.930f, 0.925f}, {0.090f, 0.090f, 0.095f}, {0.30f, 0.30f, 0.32f}, {0.80f, 0.26f, 0.17f},
     {{0.090f, 0.090f, 0.095f}, {0.25f, 0.25f, 0.26f}, {0.80f, 0.26f, 0.17f}, {0.090f, 0.090f, 0.095f},
      {0.30f, 0.36f, 0.50f}, {0.090f, 0.090f, 0.095f}, {0.090f, 0.090f, 0.095f}}, true,
     {0.040f, 0.040f, 0.042f}, {0.900f, 0.900f, 0.895f}},
    {"YORU & GOLD", {0.930f, 0.930f, 0.925f}, {0.095f, 0.090f, 0.085f}, {0.35f, 0.33f, 0.30f}, {0.82f, 0.64f, 0.26f},
     {{0.095f, 0.090f, 0.085f}, {0.26f, 0.25f, 0.24f}, {0.82f, 0.64f, 0.26f}, {0.095f, 0.090f, 0.085f},
      {0.45f, 0.30f, 0.26f}, {0.095f, 0.090f, 0.085f}, {0.095f, 0.090f, 0.085f}}, true,
     {0.035f, 0.035f, 0.038f}, {0.900f, 0.900f, 0.895f}},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

vec3 cellPos(float x, float y) { return layout::cellPos(x, y); }

// The level's name, after the universe's name in the HUD.
const char* LEVEL_WORDS[3] = {"STILL WATER", "DRIFTING MIST", "BOLD BRUSH"};

} // namespace

GLuint SumiUniverse::compile(const char* vs, const char* fs, const char* name) {
    // Insert the shared noise functions after the fragment shader's #version line.
    std::string f(fs);
    size_t nl = f.find('\n');
    f.insert(nl + 1, shaders::NOISE_GLSL);
    GLuint p = glCreateProgram();
    GLuint a = compileStage(GL_VERTEX_SHADER, vs, name), b = compileStage(GL_FRAGMENT_SHADER, f.c_str(), name);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gl] %s link error:\n%s\n", name, log);
    }
    glDeleteShader(a);
    glDeleteShader(b);
    return p;
}

bool SumiUniverse::init(int width, int height) {
    progPaper_ = compile(shaders::FULLSCREEN_VS, shaders::PAPER_FS, "paper");
    progBg_ = compile(shaders::FULLSCREEN_VS, shaders::BG_FS, "background");
    progSprite_ = compile(shaders::SPRITE_VS, shaders::SPRITE_FS, "ink");
    progPost_ = compile(shaders::FULLSCREEN_VS, shaders::POST_FS, "post");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "text");
    progTextInk_ = compile(shaders::FULLSCREEN_VS, shaders::TEXT_INK_FS, "text ink");

    glGenVertexArrays(1, &emptyVao_);
    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    glGenBuffers(1, &spriteVbo_);
    glGenVertexArrays(1, &spriteVao_);
    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(1 + i);
        glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(Sprite), (void*)(sizeof(float) * 4 * i));
        glVertexAttribDivisor(1 + i, 1);
    }

    glGenVertexArrays(1, &textVao_);
    glGenBuffers(1, &textVbo_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    for (int i = 0; i < 7; i++) piece_[i] = VARIANTS[0].piece[i];
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

void SumiUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    // Flat orthographic view of the desk, at the reference camera's scale on the board plane (layout contract).
    const float aspect = (float)width_ / height_;
    const layout::Camera cam = layout::camera(width_, height_);
    const float hh = cam.dist * std::tan(cam.fovDeg * 0.5f * PI / 180.f), hw = hh * aspect;
    viewH_ = v2(hw, hh);
    viewC_ = v2(cam.target.x, cam.target.y);
    vp_ = mat4::ortho(viewC_.x - hw, viewC_.x + hw, viewC_.y - hh, viewC_.y + hh, -1.f, 1.f);
    createTargets();
}

void SumiUniverse::destroyTargets() {
    if (!sceneFbo_) return;
    glDeleteFramebuffers(1, &sceneFbo_);
    glDeleteTextures(1, &sceneTex_);
    glDeleteFramebuffers(1, &paperFbo_);
    glDeleteTextures(1, &paperTex_);
    glDeleteFramebuffers(1, &textFbo_);
    glDeleteTextures(1, &textTex_);
    sceneFbo_ = 0;
}

SumiUniverse::~SumiUniverse() {
    destroyTargets();
    for (GLuint p : {progPaper_, progBg_, progSprite_, progPost_, progText_, progTextInk_}) glDeleteProgram(p);
    glDeleteVertexArrays(1, &emptyVao_);
    glDeleteVertexArrays(1, &spriteVao_);
    glDeleteVertexArrays(1, &textVao_);
    glDeleteBuffers(1, &quadVbo_);
    glDeleteBuffers(1, &spriteVbo_);
    glDeleteBuffers(1, &textVbo_);
}

void SumiUniverse::createTargets() {
    destroyTargets();
    auto makeTex = [](GLuint& tex, int w, int h) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    glGenFramebuffers(1, &sceneFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    makeTex(sceneTex_, width_, height_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex_, 0);
    glGenFramebuffers(1, &textFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, textFbo_);
    makeTex(textTex_, width_, height_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textTex_, 0);
    glGenFramebuffers(1, &paperFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, paperFbo_);
    makeTex(paperTex_, width_, height_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, paperTex_, 0);
    // The paper's relief only depends on the size: rendered once here.
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    glUseProgram(progPaper_);
    set2f(progPaper_, "uRes", (float)width_, (float)height_);
    set1f(progPaper_, "uScale", 2.f * viewH_.y / height_);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------- song, level, bridge

void SumiUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    if (look.variant < 0 && variant_ == look.avoidVariant) {
        // Following itself: the neighbouring variant of the same kind (print / negative).
        variant_ = VARIANTS[variant_].negative ? (variant_ == 6 ? 7 : 6) : (variant_ + 1 + (int)(look.seed % 5)) % 6;
    }
    rng_ = Rng(look.seed ^ 0x5131ull);
    if (seconds <= 0.f) variantSet_ = false; // snap the colors on the first frame
    else if (hasLevel_) startWash(level_, seconds, 0.3f); // a new song arrives with its wash, the colors ease in
}

void SumiUniverse::setLevel(int level, float, float seconds, float impact) {
    level = std::clamp(level, 0, 2);
    if (!hasLevel_ || seconds < 0.1f) {
        level_ = level;
        hasLevel_ = true;
        hasPending_ = false;
        lvA_ = lvB_ = lvShown_ = (float)level;
        washT_ = 1.f;
        washFront_ = -1.f;
        ensoT_ = level >= 2 ? 5.f : 0.f;
        ensoPresence_ = sunPresence_ = level >= 2 ? 1.f : 0.f;
        petalPresence_ = level >= 2 ? 1.f : level == 1 ? 0.6f : 0.f;
        return;
    }
    if (washT_ < 1.f) { // never interrupt a wash: queue, and hurry the running one a little
        pendingLevel_ = level;
        pendingDur_ = seconds;
        pendingImpact_ = impact;
        hasPending_ = true;
        float remaining = (1.f - washT_) * washDur_;
        if (remaining > 1.2f) washDur_ = 1.2f / (1.f - washT_);
        return;
    }
    startWash(level, seconds, impact);
}

// A new section at the same level, or a level up: a soft wash.
void SumiUniverse::refresh(float strength) {
    if (!hasLevel_) return;
    setLevel(hasPending_ ? pendingLevel_ : level_, 0.f, 1.6f, strength);
}

void SumiUniverse::startWash(int level, float seconds, float impact) {
    level_ = level;
    lvA_ = lvB_;
    lvB_ = (float)level;
    washT_ = 0.f;
    washDur_ = std::clamp(seconds, 1.4f, 4.5f);
    washImpact_ = saturate(impact);
    washSeed_ = rng_.range(0.f, 50.f);
    washFront_ = 0.f;
}

void SumiUniverse::setBonus(bool on) {
    if (on == inverted_) return;
    inverted_ = on;
    invT_ = 0.f;
}

void SumiUniverse::levelUp() {
    spawnBloom(v2(0, 0), 12.5f, 0.6f, inkC_);
}

vec2 SumiUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

// From afar: the paper (or the lacquer of a negative), the ink, the vermilion. Linear RGB.
TransitionHints SumiUniverse::hints() const {
    const Variant& V = VARIANTS[variant_];
    TransitionHints h;
    if (V.negative) {
        h.background = ugl::toLinear(V.lacquer);
        h.ink = ugl::toLinear(V.ivory);
        h.light = false;
    } else {
        h.background = ugl::toLinear(V.paper);
        h.ink = ugl::toLinear(V.ink);
        h.light = true;
    }
    h.accent = ugl::toLinear(V.accent);
    h.styles = TS_INK | TS_LUMA | TS_CURTAIN;
    return h;
}

// A point on the paper beside the board (and beside the side panels), for blooms.
vec2 SumiUniverse::randomSidePoint() {
    float side = rng_.chance(0.5f) ? 1.f : -1.f;
    float x = rng_.range(11.f, std::max(12.f, viewH_.x - 0.5f));
    float y = rng_.range(-viewH_.y + 1.f, viewH_.y - 1.f) + viewC_.y;
    return v2(side * x, y);
}

void SumiUniverse::spawnBloom(vec2 pos, float size, float strength, vec3 col) {
    if (blooms_.size() >= 18) blooms_.erase(blooms_.begin());
    Bloom b;
    b.pos = pos;
    b.age = 0;
    b.life = 2.6f + 1.4f * strength;
    b.size = size;
    b.inner = 0.035f + 0.045f * strength;
    b.tide = 0.25f + 0.35f * strength; // the dense core of the drop
    b.rough = rng_.range(0.2f, 0.35f);
    b.seed = rng_.range(0.f, 100.f);
    b.col = col;
    blooms_.push_back(b);
    sinceBloom_ = 0;
}

// ---------------------------------------------------------------- game events

void SumiUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::Lock: {
        if (ev.cells.empty()) break;
        int minX = 99, minY = 99;
        for (auto& c : ev.cells) minX = std::min(minX, c.x), minY = std::min(minY, c.y);
        const int id = pieceCounter_++;
        const vec2 shift = v2(hashf(id * 1.31f) * 40.f, hashf(id * 2.17f) * 40.f);
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            ink_[c.y][c.x] = {id, c.type, add(shift, v2((float)(c.x - minX), (float)-(c.y - minY)))};
        }
        break;
    }
    case GameEvent::Clear: {
        bool cleared[Game::H] = {};
        for (auto& c : ev.cells) if (c.y >= 0 && c.y < Game::H) cleared[c.y] = true;
        const bool fromRight = rng_.chance(0.5f);
        const bool tetris = ev.count >= 4;
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const InkCell& ic = ink_[c.y][c.x];
            const bool known = ic.type == c.type && ic.id >= 0;
            int mask = 0;
            auto same = [&](int x) {
                if (x < 0 || x >= Game::W) return false;
                const InkCell& o = ink_[c.y][x];
                return known ? o.id == ic.id : (o.id < 0 && o.type == c.type);
            };
            if (same(c.x - 1)) mask |= 1;
            if (same(c.x + 1)) mask |= 2;
            const float order = fromRight ? 9.f - c.x : (float)c.x;
            Dying d;
            d.x = c.x; d.y = c.y; d.type = c.type; d.mask = mask;
            d.t = 0; d.delay = order * 0.018f; d.dur = 0.42f;
            d.nc = known ? ic.nc : v2(c.x + 13.7f, -(float)c.y + 5.1f);
            d.seed = known ? (float)(ic.id % 97) : 3.f;
            dying_.push_back(d);
            // ink lifting off into the water: a few specks that drift and diffuse
            vec3 p = cellPos((float)c.x, (float)c.y);
            vec3 col = piece_[c.type];
            for (int i = 0; i < (tetris ? 3 : 2) && drops_.size() < 500; i++) {
                Drop dr;
                dr.pos = v2(p.x + rng_.range(-0.35f, 0.35f), p.y + rng_.range(-0.3f, 0.3f));
                float a = rng_.range(0.f, TAU);
                float sp = rng_.range(0.3f, 1.1f) * (tetris ? 1.6f : 1.f);
                dr.vel = v2(std::cos(a) * sp + (fromRight ? -0.6f : 0.6f), std::sin(a) * sp * 0.6f + 0.25f);
                dr.size = rng_.range(0.07f, 0.16f);
                dr.grow = rng_.range(1.5f, 3.2f);
                dr.age = -d.delay;
                dr.life = rng_.range(0.5f, 0.8f);
                dr.alpha = rng_.range(0.2f, 0.35f);
                dr.seed = rng_.range(0.f, 100.f);
                dr.col = col;
                drops_.push_back(dr);
            }
        }
        // Nothing is painted over the rows: their ink melts back into the paper. A Tetris stamps the red seal.
        if (tetris) {
            Stamp st;
            st.pos = v2(layout::HOLD_CENTER.x, 4.0f);
            st.size = 1.05f;
            st.rot = rng_.range(-0.07f, 0.07f);
            st.age = -0.3f;
            st.life = 3.6f;
            st.seed = rng_.range(0.f, 100.f);
            stamps_.push_back(st);
            if (ev.backToBack) {
                st.pos = v2(layout::HOLD_CENTER.x + 0.6f, 2.9f);
                st.size = 0.8f;
                st.rot = rng_.range(0.05f, 0.12f);
                st.age = -0.6f;
                st.seed = rng_.range(0.f, 100.f);
                stamps_.push_back(st);
            }
        }
        // Collapse the ink bookkeeping like the board.
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
    case GameEvent::BonusReady:
        readyFlash_ = 1.f;
        spawnBloom(v2(layout::GAUGE_CENTER.x, layout::GAUGE_CENTER.y), 2.4f, 0.8f, verm_);
        break;
    case GameEvent::BonusStart:
        spawnBloom(v2(0, 0), 14.f, 1.f, inkC_);
        break;
    case GameEvent::TopOut: {
        // Game over: the painting dissolves, row by row from the top.
        int topRow = Game::H;
        for (auto& c : ev.cells) topRow = std::min(topRow, c.y);
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const InkCell& ic = ink_[c.y][c.x];
            const bool known = ic.type == c.type && ic.id >= 0;
            Dying d;
            d.x = c.x; d.y = c.y; d.type = c.type; d.mask = 0;
            d.t = 0; d.delay = 0.3f + (c.y - topRow) * 0.12f + std::fabs(c.x - 4.5f) * 0.03f; d.dur = 1.2f;
            d.nc = known ? ic.nc : v2(c.x + 13.7f, -(float)c.y + 5.1f);
            d.seed = known ? (float)(ic.id % 97) : 3.f;
            // pieces stay joined while they fade
            for (auto& o : ev.cells) {
                if (o.y < 0 || o.y >= Game::H || o.x < 0 || o.x >= Game::W) continue;
                const InkCell& oc = ink_[o.y][o.x];
                if (!known || oc.id != ic.id) continue;
                if (o.y == c.y && o.x == c.x - 1) d.mask |= 1;
                if (o.y == c.y && o.x == c.x + 1) d.mask |= 2;
                if (o.x == c.x && o.y == c.y + 1) d.mask |= 4;
                if (o.x == c.x && o.y == c.y - 1) d.mask |= 8;
            }
            dying_.push_back(d);
        }
        for (auto& row : ink_)
            for (auto& c : row) c = InkCell{};
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------- simulation

void SumiUniverse::simulate(const Game&, const MusicState& music, float dt, float wdt) {
    const float lv = lvShown_;
    const float e = saturate(music.energy);
    // Mountains drift (parallax), slower when calm.
    const float pace = (0.35f + 0.65f * smoothstepf(0.f, 2.f, lv)) * (0.6f + 0.8f * e);
    driftFar_ += wdt * 0.010f * pace;
    driftMid_ += wdt * 0.026f * pace;
    driftNear_ += wdt * 0.050f * pace;

    // Beats: drops of ink blooming in the wet paper beside the board.
    sinceBloom_ += wdt;
    const bool beat = music.beatPhase < prevBeatPhase_ - 0.5f;
    prevBeatPhase_ = music.beatPhase;
    if (beat) {
        beatCount_++;
        const int every = lv >= 1.5f ? 4 : lv >= 0.5f ? 8 : 16;
        if (beatCount_ % every == 0) {
            float strength = (0.45f + 0.55f * e) * (lv >= 1.5f ? 1.f : lv >= 0.5f ? 0.75f : 0.5f) + 0.3f * music.kick;
            strength = saturate(strength);
            vec3 col = inkC_;
            if (lv >= 1.5f) {
                float r = rng_.uniform();
                if (r < 0.22f) col = ink2C_;
                else if (r < 0.30f && music.kick > 0.4f) col = verm_;
            } else if (lv >= 0.5f && rng_.chance(0.15f)) col = ink2C_;
            spawnBloom(randomSidePoint(), (0.9f + 1.6f * strength) * rng_.range(0.75f, 1.2f), strength, col);
        }
    }
    if (music.kick > 0.85f && sinceBloom_ > 1.2f && lv >= 1.5f)
        spawnBloom(randomSidePoint(), 1.6f + 1.5f * music.kick, 0.5f * music.kick, inkC_);
    for (size_t i = 0; i < blooms_.size();) {
        blooms_[i].age += wdt;
        if (blooms_[i].age > blooms_[i].life) { blooms_.erase(blooms_.begin() + i); continue; }
        i++;
    }

    // Plum petals, mid and peak only.
    const float want = petalPresence_ * (5.f + 6.f * e);
    if ((float)petals_.size() < want && rng_.chance(std::min(1.f, wdt * 1.6f))) {
        Petal p;
        float side = rng_.chance(0.5f) ? 1.f : -1.f;
        p.pos = v2(side * rng_.range(6.5f, viewH_.x + 1.f), viewC_.y + viewH_.y + 0.6f);
        p.vel = v2(rng_.range(-0.35f, 0.05f), -rng_.range(0.45f, 0.85f));
        p.rot = rng_.range(0.f, TAU);
        p.spin = rng_.range(-1.2f, 1.2f);
        p.size = rng_.range(0.2f, 0.3f);
        p.age = 0;
        p.life = 40.f;
        p.seed = rng_.range(0.f, 100.f);
        petals_.push_back(p);
    }
    for (size_t i = 0; i < petals_.size();) {
        Petal& p = petals_[i];
        p.age += wdt;
        float sway = std::sin(p.age * 1.3f + p.seed) * 0.35f;
        p.pos = add(p.pos, mul(add(p.vel, v2(sway, 0.f)), wdt));
        p.rot += p.spin * wdt;
        if (p.pos.y < viewC_.y - viewH_.y - 1.f || p.age > p.life) { petals_[i] = petals_.back(); petals_.pop_back(); continue; }
        i++;
    }

    for (size_t i = 0; i < drops_.size();) {
        Drop& d = drops_[i];
        d.age += wdt;
        if (d.age > d.life) { drops_[i] = drops_.back(); drops_.pop_back(); continue; }
        if (d.age > 0) {
            d.vel = mul(d.vel, std::exp(-1.8f * wdt));
            d.pos = add(d.pos, mul(d.vel, wdt));
        }
        i++;
    }
    for (size_t i = 0; i < strokes_.size();) {
        Stroke& s = strokes_[i];
        s.age += dt;
        if (s.age > s.draw + s.hold + s.fade) { strokes_.erase(strokes_.begin() + i); continue; }
        i++;
    }
    for (size_t i = 0; i < stamps_.size();) {
        stamps_[i].age += dt;
        if (stamps_[i].age > stamps_[i].life) { stamps_.erase(stamps_.begin() + i); continue; }
        i++;
    }
    for (size_t i = 0; i < dying_.size();) {
        dying_[i].t += dt;
        if (dying_[i].t > dying_[i].delay + dying_[i].dur) { dying_[i] = dying_.back(); dying_.pop_back(); continue; }
        i++;
    }
}

// ---------------------------------------------------------------- sprites

void SumiUniverse::addBlock(std::vector<Sprite>& out, vec2 pos, int type, int mask, float scale, vec2 nc, float seed,
                        float wet, float age, float dissolve, float alpha, int kind) {
    Sprite s;
    s.x = pos.x;
    s.y = pos.y;
    s.hw = s.hh = (0.5f + 0.4f) * scale;
    s.rot = 0;
    s.kind = (float)kind;
    s.seed = seed;
    s.alpha = alpha;
    s.c[0] = (float)type; s.c[1] = (float)mask; s.c[2] = wet; s.c[3] = scale;
    s.d[0] = age; s.d[1] = dissolve; s.d[2] = nc.x; s.d[3] = nc.y;
    out.push_back(s);
}

void SumiUniverse::collectScenery(const MusicState& music, std::vector<Sprite>& out) {
    auto put = [&](float x, float y, float hw, float hh, float rot, int kind, float seed, float alpha, vec3 c, float cw,
                   float d0, float d1, float d2, float d3) {
        Sprite s{x, y, hw, hh, rot, (float)kind, seed, alpha, {c.x, c.y, c.z, cw}, {d0, d1, d2, d3}};
        out.push_back(s);
    };
    // Peak: the enso, drawn in one breath, holding the red sun, in the free paper right of the board.
    {
        const float x0 = layout::RIGHT_COLUMN[2] - 0.2f, x1 = viewH_.x - 0.7f;
        const float R = std::clamp((x1 - x0) * 0.5f - 0.5f, 2.f, 4.4f), cx = (x0 + x1) * 0.5f, cy = 4.4f;
        if (sunPresence_ > 0.01f) {
            float r = R * 0.46f * (1.f + 0.015f * music.beatPulse);
            put(cx + R * 0.16f, cy + R * 0.14f, r, r, 0, K_SUN + K_BEHIND, 4.f, 0.88f * sunPresence_, verm_, 0, 0, 0, 0, 0);
        }
        if (ensoPresence_ > 0.01f) {
            const float W = 0.36f * R / 4.4f + 0.12f, sweep = TAU - 0.6f, start = 2.0f;
            float reveal = saturate(ensoT_ / 1.3f);
            reveal = 1.f - (1.f - reveal) * (1.f - reveal) * (1.f - reveal);
            put(cx, cy, R + W * 2.f, R + W * 2.f, 0, K_ENSO + K_BEHIND, 11.f, 0.9f * ensoPresence_, inkC_, sweep, R, W,
                start, reveal);
        }
    }
    for (const Bloom& b : blooms_) {
        float grow = 1.f - std::exp(-b.age * 1.8f);
        float R = b.size * (0.15f + 0.85f * grow);
        float a = smoothstepf(0.f, 0.12f, b.age) * std::pow(1.f - b.age / b.life, 1.3f);
        put(b.pos.x, b.pos.y, R * 1.45f, R * 1.45f, 0, K_BLOOM + K_BEHIND, b.seed, a, b.col, 0, R, b.inner, b.tide * std::exp(-b.age * 1.1f), b.rough);
    }
    for (const Petal& p : petals_) {
        float a = smoothstepf(0.f, 1.f, p.age) * petalPresence_;
        put(p.pos.x, p.pos.y, p.size, p.size, p.rot, K_PETAL + K_BEHIND, p.seed, 0.42f * a, verm_, 0, 0, 0, 0, 0);
    }
    // The well: three brush lines (left, right, bottom), open at the top, and the signature seal.
    auto stroke = [&](vec2 a, vec2 b, float width, float seed, float dry, float taper, float dens, vec3 col, float reveal,
                      float wellFade, float alpha) {
        vec2 c = mul(add(a, b), 0.5f);
        float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
        put(c.x, c.y, len * 0.5f, width * 1.3f, std::atan2(dy, dx), K_STROKE, seed, alpha * dens, col, 0, reveal, dry,
            wellFade, taper);
    };
    stroke(v2(-5.32f, 10.15f), v2(-5.28f, -10.45f), 0.11f, 3.f, 0.5f, 0.45f, 0.9f, inkC_, 1.f, 1.f, 1.f);
    stroke(v2(5.30f, 10.05f), v2(5.33f, -10.4f), 0.10f, 7.f, 0.5f, 0.5f, 0.9f, inkC_, 1.f, 1.f, 1.f);
    stroke(v2(-5.75f, -10.36f), v2(5.85f, -10.30f), 0.15f, 13.f, 0.5f, 0.5f, 0.92f, inkC_, 1.f, 1.f, 1.f);
    put(6.12f, -9.45f, 0.42f, 0.42f, 0.04f, K_SEAL, 21.f, 0.92f, verm_, 0, 1.f, 0, 0, 0);

    // Effects: brush strokes washing rows away, specks of ink lifting into the water, stamped seals.
    for (const Stroke& s : strokes_) {
        float reveal = saturate(s.age / s.draw);
        reveal = 1.f - (1.f - reveal) * (1.f - reveal);
        float fade = 1.f - smoothstepf(s.draw + s.hold, s.draw + s.hold + s.fade, s.age);
        float wellFade = 1.f - smoothstepf(s.draw + 0.05f, s.draw + 0.55f, s.age);
        stroke(s.a, s.b, s.width, s.seed, s.dry, s.taper, s.dens, s.col, reveal, wellFade, fade);
    }
    for (const Drop& d : drops_) {
        if (d.age <= 0) continue;
        float k = d.age / d.life;
        float r = d.size * (1.f + d.grow * std::sqrt(k));
        float a = d.alpha * (1.f - k) * (1.f - k) * smoothstepf(0.f, 0.08f, d.age);
        put(d.pos.x, d.pos.y, r, r, 0, K_DROP, d.seed, a * 0.8f, d.col, 0.f, 0.6f, 0, 0, 0);
    }
    for (const Stamp& s : stamps_) {
        if (s.age < 0) continue;
        // pressed down: a little larger for an instant, then settled; fades at the end
        float press = smoothstepf(0.f, 0.12f, s.age);
        float sz = s.size * (1.f + 0.18f * (1.f - press));
        float a = press * (1.f - smoothstepf(s.life - 0.9f, s.life, s.age));
        put(s.pos.x, s.pos.y, sz, sz, s.rot, K_SEAL, s.seed, 0.95f * a, verm_, 0, press, 0, 0, 0);
    }
}

void SumiUniverse::collectBoard(const Game& g, const MusicState&, std::vector<Sprite>& out) {
    auto valid = [](int x, int y) { return x >= 0 && x < Game::W && y >= 0 && y < Game::H; };
    // Locked cells: the cells of one piece join into one brushed shape.
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const InkCell& ic = ink_[y][x];
            const bool known = ic.type == c.type && ic.id >= 0;
            auto joined = [&](int nx, int ny) {
                if (!valid(nx, ny) || ny < Game::HIDDEN) return false;
                const Cell& o = g.cell(nx, ny);
                if (o.type != c.type) return false;
                if (ny != y && std::fabs(g.rowOffset(ny) - g.rowOffset(y)) > 0.01f) return false;
                const InkCell& oc = ink_[ny][nx];
                const bool oknown = oc.type == o.type && oc.id >= 0;
                return known && oknown && oc.id == ic.id; // cells of unknown pieces (a stack inherited from another universe) stay apart
            };
            int mask = 0;
            if (joined(x - 1, y)) mask |= 1;
            if (joined(x + 1, y)) mask |= 2;
            if (joined(x, y + 1)) mask |= 4; // board y grows downward
            if (joined(x, y - 1)) mask |= 8;
            vec3 p = cellPos((float)x, y - g.rowOffset(y));
            vec2 nc = known ? ic.nc : v2(x + 13.7f, -(float)y + 5.1f);
            float seed = known ? (float)(ic.id % 97) : 3.f;
            float age = c.flash > 0.f ? (1.f - c.flash) * Game::LOCK_FX_SECONDS : -1.f;
            float wet = age >= 0.f ? 0.6f * std::exp(-age * 2.5f) : 0.f;
            addBlock(out, v2(p.x, p.y), c.type, mask, 1.f, nc, seed, wet, age, 0.f, 1.f);
        }
    for (const Dying& d : dying_) {
        float k = saturate((d.t - d.delay) / d.dur);
        vec3 p = cellPos((float)d.x, (float)d.y);
        addBlock(out, v2(p.x, p.y), d.type, d.mask, 1.f, d.nc, d.seed, 0.f, -1.f, k, 1.f - k * k);
    }
    // Mask of a piece's own cells (for the falling piece, its ghost and the previews).
    auto pieceMask = [](int cells[4][2], int i) {
        int m = 0;
        for (int j = 0; j < 4; j++) {
            if (j == i) continue;
            int dx = cells[j][0] - cells[i][0], dy = cells[j][1] - cells[i][1];
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
        int minX = 99, minY = 99;
        for (auto& c : cells) minX = std::min(minX, c[0]), minY = std::min(minY, c[1]);
        const int id = pieceCounter_;
        const vec2 shift = v2(hashf(id * 1.31f) * 40.f, hashf(id * 2.17f) * 40.f);
        const float seed = (float)(id % 97);
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (int i = 0; i < 4; i++) {
                vec3 w = cellPos((float)gc[i][0], (float)gc[i][1]);
                vec2 nc = add(shift, v2((float)(cells[i][0] - minX), (float)-(cells[i][1] - minY)));
                addBlock(out, v2(w.x, w.y), p.type, pieceMask(gc, i), 1.f, nc, seed, 0.f, -1.f, 0.f, 1.f, K_GHOST);
            }
        }
        float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (int i = 0; i < 4; i++) {
            if (cells[i][1] < Game::HIDDEN - 1) continue;
            vec3 w = cellPos(cells[i][0] + dx, cells[i][1] + dy);
            vec2 nc = add(shift, v2((float)(cells[i][0] - minX), (float)-(cells[i][1] - minY)));
            addBlock(out, v2(w.x, w.y), p.type, pieceMask(cells, i), 1.f, nc, seed, 1.f, -1.f, 0.f, 1.f);
        }
    }
    // Previews: hold on the left, the next queue on the right.
    auto preview = [&](int type, vec2 center, float sc, float alpha) {
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        g.pieceCells(p, cells);
        float minx = 9, maxx = -9, miny = 9, maxy = -9;
        for (auto& c : cells) {
            minx = std::min(minx, (float)c[0]); maxx = std::max(maxx, (float)c[0]);
            miny = std::min(miny, (float)c[1]); maxy = std::max(maxy, (float)c[1]);
        }
        for (int i = 0; i < 4; i++) {
            vec2 pos = v2(center.x + (cells[i][0] - (minx + maxx) * 0.5f) * sc, center.y - (cells[i][1] - (miny + maxy) * 0.5f) * sc);
            vec2 nc = v2(cells[i][0] + type * 7.3f, -(float)cells[i][1] + type * 3.1f);
            addBlock(out, pos, type, pieceMask(cells, i), sc, nc, (float)type * 5.f, 0.f, -1.f, 0.f, alpha);
        }
    };
    if (g.holdType() >= 0)
        preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.32f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), i == 0 ? 1.f : 0.8f);
    }

    // The bonus gauge: a small enso that the cleared lines draw, vermilion once complete.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float GAUGE_R = layout::GAUGE_R;
        const float sweep = TAU * 0.93f, start = 1.75f;
        const bool ready = g.bonusReady(), active = g.bonusActive();
        Sprite track{gc.x, gc.y, 1.3f, 1.3f, 0, (float)K_ENSO, 2.f, 0.16f, {inkC_.x, inkC_.y, inkC_.z, sweep},
                     {GAUGE_R, 0.07f, start, 1.f}};
        out.push_back(track);
        float lvl = gaugeShown_;
        if (lvl > 0.005f) {
            vec3 col = ready || active ? verm_ : inkC_;
            float a = 0.85f;
            if (ready) a = 0.72f + 0.2f * (0.5f + 0.5f * std::sin(readyT_ * TAU * 0.45f));
            Sprite arc{gc.x, gc.y, 1.4f, 1.4f, 0, (float)K_ENSO, 5.f, a, {col.x, col.y, col.z, sweep},
                       {GAUGE_R, 0.15f, start, std::min(1.f, lvl * 1.005f)}};
            out.push_back(arc);
        }
    }
}

// ---------------------------------------------------------------- text

// SUMI's typography for each kind of HUD line: airy letter spacing, quieter labels, the level's name after the
// universe's (the bitmap font has no middle dot: a dash).
namespace {
struct Styled { HudText t; float spacing; };
Styled style(const HudText& in, int level) {
    Styled s{in, 0.5f};
    HudText& t = s.t;
    for (size_t i; (i = t.text.find("\xC2\xB7")) != std::string::npos;) t.text.replace(i, 2, "-");
    switch (in.role) {
    case HUD_TITLE: s.spacing = 1.f; t.scale *= 0.91f; t.alpha = std::min(0.9f, t.alpha * 1.45f); break;
    case HUD_INFO: s.spacing = 1.2f; t.scale *= 0.93f; t.alpha = std::min(1.f, t.alpha * 1.1f); break;
    case HUD_SECTION: s.spacing = 1.5f; t.scale *= 1.077f; t.alpha = std::min(1.f, t.alpha * 1.43f); break;
    case HUD_UNIVERSE:
        s.spacing = 1.5f;
        t.alpha = std::min(1.f, t.alpha * 1.2f);
        if (level >= 0) t.text += std::string("   ") + LEVEL_WORDS[std::min(level, 2)];
        break;
    case HUD_LABEL: s.spacing = 2.f; t.scale *= 0.875f; t.alpha *= 0.8f; break;
    case HUD_VALUE: s.spacing = 1.f; t.alpha *= 0.98f; break;
    default: break;
    }
    return s;
}
} // namespace

void SumiUniverse::drawText(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    glUseProgram(progText_);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    static char quadBuf[200000];
    // Panels (zenscene's menu): plain paint, normal blending.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (const HudRect& r : panels) {
        const float q[12] = {r.x, r.y, r.x + r.w, r.y, r.x + r.w, r.y + r.h, r.x, r.y, r.x + r.w, r.y + r.h, r.x, r.y + r.h};
        glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STREAM_DRAW);
        vec3 c = r.custom ? r.color : r.accent ? verm_ : paperCol_ * 1.04f;
        glUniform4f(U(progText_, "uColor"), c.x, c.y, c.z, r.alpha);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    // Text is ink too: its coverage goes to its own buffer, then it is inked onto the paper (multiplicative).
    glBindFramebuffer(GL_FRAMEBUFFER, textFbo_);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendEquation(GL_MAX);
    glBlendFunc(GL_ONE, GL_ONE);
    bool any = false;
    for (const HudText& raw : hud) {
        if (raw.alpha <= 0.01f || raw.text.empty()) continue;
        Styled st = style(raw, level_);
        if (raw.role == HUD_UNIVERSE) { // the level's name only where it clears the well
            stb_easy_font_spacing(st.spacing);
            const float right = raw.x + stb_easy_font_width((char*)st.t.text.c_str()) * raw.scale;
            if (right > layout::boardRect(width_, height_).x0 - 24.f * raw.scale) st = style(raw, -1);
        }
        const HudText& h = st.t;
        std::vector<char> txt(h.text.begin(), h.text.end());
        txt.push_back(0);
        unsigned char col[4] = {255, 255, 255, 255};
        stb_easy_font_spacing(st.spacing);
        int nq = stb_easy_font_print(0, 0, txt.data(), col, quadBuf, sizeof(quadBuf));
        float width = (float)stb_easy_font_width(txt.data()) * h.scale;
        float ox = h.x;
        if (h.center) ox = h.x - width * 0.5f;
        else if (h.x < 0) ox = width_ + h.x - width;
        std::vector<float> tri;
        tri.reserve(nq * 12);
        for (int q = 0; q < nq; q++) {
            float* v = (float*)(quadBuf + q * 64);
            float px[4], py[4];
            for (int k = 0; k < 4; k++) {
                px[k] = std::round(ox + v[k * 4] * h.scale);
                py[k] = std::round(h.y + v[k * 4 + 1] * h.scale);
            }
            const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int o : order) { tri.push_back(px[o]); tri.push_back(py[o]); }
        }
        glBufferData(GL_ARRAY_BUFFER, tri.size() * sizeof(float), tri.data(), GL_STREAM_DRAW);
        glUniform4f(U(progText_, "uColor"), h.accent ? 0.f : 1.f, h.accent ? 1.f : 0.f, saturate(h.alpha), h.scale / 8.f);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(tri.size() / 2));
        any = true;
    }
    glBlendEquation(GL_FUNC_ADD);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    if (!any) return;
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
    glUseProgram(progTextInk_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textTex_);
    set1i(progTextInk_, "uText", 0);
    set2f(progTextInk_, "uRes", (float)width_, (float)height_);
    set3f(progTextInk_, "uInkCol", lerp(inkC_, paperCol_, 0.06f));
    set3f(progTextInk_, "uAccCol", verm_ * 0.85f);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ---------------------------------------------------------------- frame

void SumiUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    const bool paused = f.paused;
    // The bridge (a change of universe): the landscape and the paper's events dry out, the stack stays.
    const float world = 1.f - saturate(bridge_.amount);
    // ---- Wash (scene transition).
    if (washT_ < 1.f) {
        washT_ = std::min(1.f, washT_ + dt / washDur_);
        float k = 1.f - (1.f - washT_) * (1.f - washT_);
        washFront_ = 34.f * k;
        if (washT_ >= 1.f) {
            washFront_ = -1.f;
            lvA_ = lvB_;
            if (hasPending_) {
                hasPending_ = false;
                startWash(pendingLevel_, pendingDur_, pendingImpact_);
            }
        }
    }
    // The sprites (enso, sun, petals, blooms) follow the level once the wash has passed them.
    const float target = (washFront_ < 0.f || washFront_ > 12.f) ? lvB_ : lvA_;
    lvShown_ = approach(lvShown_, target, 2.f, dt);
    const float peak = lvShown_ >= 1.5f ? 1.f : 0.f;
    ensoPresence_ = approach(ensoPresence_, peak, peak > 0.f ? 3.f : 0.6f, dt);
    if (peak > 0.f) ensoT_ += dt;
    else if (ensoPresence_ < 0.01f) ensoT_ = 0.f;
    sunPresence_ = approach(sunPresence_, peak, 0.5f, dt);
    petalPresence_ = approach(petalPresence_, lvShown_ >= 1.5f ? 1.f : lvShown_ >= 0.5f ? 0.6f : 0.f, 0.3f, dt);
    // The song's ink variant; its colors ease in (a new song arrives with its wash).
    {
        const Variant& V = VARIANTS[variant_];
        const float k = variantSet_ ? 1.f - std::exp(-dt * 1.2f) : 1.f;
        variantSet_ = true;
        paperCol_ = lerp(paperCol_, V.paper, k);
        verm_ = lerp(verm_, V.accent, k);
        inkC_ = lerp(inkC_, V.ink, k);
        ink2C_ = lerp(ink2C_, V.ink2, k);
        for (int i = 0; i < 7; i++) piece_[i] = lerp(piece_[i], V.piece[i], k);
        if (V.negative) { lacquer_ = lerp(lacquer_, V.lacquer, k); ivory_ = lerp(ivory_, V.ivory, k); }
        else {
            lacquer_ = lerp(lacquer_, vec3(0.06f, 0.04f, 0.036f), k);
            ivory_ = lerp(ivory_, vec3(0.925f, 0.9f, 0.845f), k);
        }
        neg_ = k >= 1.f ? (V.negative ? 1.f : 0.f) : approach(neg_, V.negative ? 1.f : 0.f, 0.7f, dt);
    }

    invT_ += dt;
    slow_ = approach(slow_, inverted_ ? 1.f : 0.f, 1.5f, dt);
    pauseFade_ = approach(pauseFade_, paused ? 1.f : (f.dim ? 0.6f : 0.f), paused ? 4.f : 1.5f, dt);
    kick_ = approach(kick_, 0.f, 3.5f, dt);
    if (!paused) {
        const float wdt = dt * lerpf(1.f, 0.35f, slow_);
        simulate(game, music, dt, wdt);
        gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 1.5f, dt);
        readyT_ = game.bonusReady() ? readyT_ + dt : 0.f;
        readyFlash_ = approach(readyFlash_, 0.f, 1.f, dt);
    }

    // ---- Paper and landscape.
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(progBg_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, paperTex_);
    set1i(progBg_, "uPaper", 0);
    set4f(progBg_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set3f(progBg_, "uPaperCol", paperCol_);
    set3f(progBg_, "uInk", inkC_);
    set3f(progBg_, "uInk2", ink2C_);
    set1f(progBg_, "uDriftFar", driftFar_);
    set1f(progBg_, "uDriftMid", driftMid_);
    set1f(progBg_, "uDriftNear", driftNear_);
    set4f(progBg_, "uLevel", lvA_, lvB_, washFront_, washT_);
    set1f(progBg_, "uImpact", washImpact_);
    set1f(progBg_, "uSeed", washSeed_ * 0.f);
    set2f(progBg_, "uWell", WELL_HX + 0.05f, WELL_HY + 0.05f);
    set1f(progBg_, "uWorld", world);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ---- Ink.
    std::vector<Sprite> sprites;
    sprites.reserve(512);
    collectScenery(music, sprites);
    for (Sprite& s : sprites)
        if (s.kind >= K_BEHIND || s.kind == K_DROP) s.alpha *= world; // blooms, petals, enso, sun, specks
    collectBoard(game, music, sprites);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
    glUseProgram(progSprite_);
    setMat(progSprite_, "uVP", vp_);
    set3f(progSprite_, "uVerm", verm_);
    set3f(progSprite_, "uInk", inkC_);
    set3f(progSprite_, "uInk2", ink2C_);
    {
        float pc[21];
        for (int i = 0; i < 7; i++) { pc[i * 3] = piece_[i].x; pc[i * 3 + 1] = piece_[i].y; pc[i * 3 + 2] = piece_[i].z; }
        glUniform3fv(U(progSprite_, "uPiece"), 7, pc);
    }
    set3f(progSprite_, "uVermDark", verm_ * vec3(0.62f, 0.55f, 0.6f));
    set2f(progSprite_, "uWell", WELL_HX, WELL_HY);
    set1f(progSprite_, "uPx", 2.f * viewH_.y / height_);
    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glBufferData(GL_ARRAY_BUFFER, sprites.size() * sizeof(Sprite), sprites.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)sprites.size());

    drawText(f.hud, f.panels);
    glDisable(GL_BLEND);

    // ---- Print: vignette, grain, dusk when paused, ink negative in slow motion.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progPost_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex_);
    set1i(progPost_, "uScene", 0);
    set1f(progPost_, "uAspect", (float)width_ / height_);
    set1f(progPost_, "uFade", saturate(f.fade));
    set1f(progPost_, "uBridge", saturate(bridge_.amount));
    set3f(progPost_, "uBridgeColor", ugl::toSrgb(bridge_.color));
    set3f(progPost_, "uBridgeInk", ugl::toSrgb(bridge_.ink));
    set1f(progPost_, "uDim", pauseFade_);
    set1f(progPost_, "uInvert", inverted_ ? 1.f : 0.f);
    {
        const float k = saturate(invT_ / 0.9f);
        set1f(progPost_, "uInvFront", invT_ < 0.9f ? 1.9f * (1.f - (1.f - k) * (1.f - k)) : -1.f);
        vec2 c = project(vec3(0, 0, 0));
        set2f(progPost_, "uInvCenter", c.x / width_, 1.f - c.y / height_);
    }
    set3f(progPost_, "uPaperCol", paperCol_);
    set3f(progPost_, "uInk", inkC_);
    set3f(progPost_, "uLacquer", lacquer_);
    set3f(progPost_, "uIvory", ivory_);
    set1f(progPost_, "uNeg", neg_);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// The variant of a scene: its key hue picks the family (warm reds -> shu, oranges -> persimmon, greens -> matcha,
// cyans -> celadon, blues -> aizome, violets -> murasaki); some dark (night mood) scenes take a lacquer negative.
int SumiUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    const bool warm = h < 0.15f || h > 0.80f;
    const bool night = t.hasSong && t.minor && t.brightness < 0.45f;
    if (night && (t.seed & 3) == 0) return warm ? 6 : 7;
    if (h < 0.06f || h >= 0.88f) return 0;
    if (h < 0.15f) return 5;
    if (h < 0.22f) return (t.seed & 1) ? 3 : 0;
    if (h < 0.40f) return 4;
    if (h < 0.53f) return 2;
    if (h < 0.70f) return 1;
    return 3;
}

// ---------------------------------------------------------------- registry

const UniverseEntry& universe_sumi() {
    static const UniverseEntry e{
        "SUMI", "ink on washi paper", [] { return std::unique_ptr<Universe>(new SumiUniverse()); },
        // Unhurried songs suit the brush best; anything can be painted.
        [](const SongLook& l) { return 0.7f + 0.6f * (1.f - smoothstepf(100.f, 140.f, l.bpm)) + 0.2f * l.dynamics; },
        [] {
            std::vector<std::string> v;
            for (const Variant& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
