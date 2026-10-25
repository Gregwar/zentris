// VECTOR universe: phosphor beams on a dark CRT.
//
// One world, one visual language: everything on screen is a glowing beam segment (a gaussian core with a short
// halo), drawn in one phosphor color chosen from the song's key. The previous frame decays under the new one
// (phosphor persistence), so motion leaves short trails. Calm parts of a song show only the well and a lone
// oscilloscope trace on the horizon; mid parts lay a floor grid that scrolls one row per beat; peaks raise that
// floor into mountains driven by the spectrum, with a Lissajous figure in the sky and the camera breathing.
// Pieces differ by their glyph (the line pattern inside each cell), not by hue.
#include "universes/vector/vector.hpp"

#include "layout.hpp"
#include "ugl.hpp"
#include "universes/registry.hpp"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "universes/vector/shaders.hpp"

namespace {

GLint U(GLuint p, const char* n) { return glGetUniformLocation(p, n); }
void set1f(GLuint p, const char* n, float v) { glUniform1f(U(p, n), v); }
void set1i(GLuint p, const char* n, int v) { glUniform1i(U(p, n), v); }
void set2f(GLuint p, const char* n, float a, float b) { glUniform2f(U(p, n), a, b); }
void set3f(GLuint p, const char* n, const vec3& v) { glUniform3f(U(p, n), v.x, v.y, v.z); }
void set4f(GLuint p, const char* n, float a, float b, float c, float d) { glUniform4f(U(p, n), a, b, c, d); }
void setMat(GLuint p, const char* n, const mat4& m) { glUniformMatrix4fv(U(p, n), 1, GL_FALSE, m.m); }

constexpr float WELL_X = 5.12f, WELL_BOT = -10.12f, WELL_TOP = 10.05f, WELL_DEPTH = 1.6f;
constexpr float FLOOR_Y = -12.5f;

// ---- Phosphors: the whole palette. One per song (from its key); brightness is the only other variable.
struct Phosphor { const char* name; vec3 color; };
const Phosphor PHOSPHORS[4] = {
    {"P1 GREEN", {0.20f, 1.00f, 0.30f}},
    {"P3 AMBER", {1.00f, 0.50f, 0.08f}},
    {"P4 WHITE", {0.55f, 0.82f, 1.00f}},
    {"P22 ROSE", {1.00f, 0.25f, 0.62f}},
};
// The slow-motion bonus shows the "other" tube: a complement that stays inside the set.
const int ALT_PHOSPHOR[4] = {3, 2, 1, 0};

// ---- Stroke font: polylines on a 4 x 6 grid (y up), in the spirit of the 1979 arcade vector fonts.
// Points are 2 digits (x, y); spaces separate points; '|' starts a new stroke.
const char* glyphStrokes(char ch) {
    switch (ch) {
    case 'A': return "00 04 26 44 40|02 42";
    case 'B': return "00 06 36 45 44 33 03|33 42 41 30 00";
    case 'C': return "46 06 00 40";
    case 'D': return "00 06 26 44 42 20 00";
    case 'E': return "46 06 00 40|03 33";
    case 'F': return "46 06 00|03 33";
    case 'G': return "46 06 00 40 42 22";
    case 'H': return "00 06|40 46|03 43";
    case 'I': return "06 46|26 20|00 40";
    case 'J': return "46 41 30 10 01 02";
    case 'K': return "00 06|46 03 40";
    case 'L': return "06 00 40";
    case 'M': return "00 06 24 46 40";
    case 'N': return "00 06 40 46";
    case 'O': return "00 06 46 40 00";
    case 'P': return "00 06 46 43 03";
    case 'Q': return "00 06 46 42 20 00|22 40";
    case 'R': return "00 06 46 43 03|13 40";
    case 'S': return "46 06 03 43 40 00";
    case 'T': return "06 46|26 20";
    case 'U': return "06 00 40 46";
    case 'V': return "06 20 46";
    case 'W': return "06 00 22 40 46";
    case 'X': return "00 46|06 40";
    case 'Y': return "06 23 46|23 20";
    case 'Z': return "06 46 00 40";
    case '0': return "00 06 46 40 00|04 42";
    case '1': return "15 26 20|10 30";
    case '2': return "06 46 43 03 00 40";
    case '3': return "06 46 40 00|13 43";
    case '4': return "06 03 43|46 40";
    case '5': return "46 06 03 43 40 00";
    case '6': return "06 00 40 43 03";
    case '7': return "06 46 41 20";
    case '8': return "00 06 46 40 00|03 43";
    case '9': return "43 03 06 46 40";
    case '-': return "13 33";
    case '+': return "13 33|22 24";
    case '=': return "12 32|14 34";
    case ':': return "22 22|24 24";
    case '.': return "20 20";
    case ',': return "21 10";
    case '\'': return "26 24";
    case '"': return "16 14|36 34";
    case '!': return "26 22|20 20";
    case '?': return "05 16 36 45 44 23 22|20 20";
    case '/': return "00 46";
    case '\\': return "06 40";
    case '(': return "36 24 22 30";
    case ')': return "16 24 22 10";
    case '[': return "36 16 10 30";
    case ']': return "16 36 30 10";
    case '<': return "35 13 31";
    case '>': return "15 33 11";
    case '#': return "16 10|36 30|04 44|02 42";
    case '%': return "00 46|05 15|31 41";
    case '&': return "40 05 16 25 03 00 20 42";
    case '*': return "13 33|22 24|12 34|14 32";
    case '_': return "00 40";
    case '@': return "42 22 24 44 40 00 06 46";
    default: return nullptr;
    }
}
constexpr float FONT_UNIT = 1.2f; // pixels per font grid unit at HUD scale 1

float hash1(float n) { float s = std::sin(n) * 43758.5453f; return s - std::floor(s); }
float hash2(int x, int y) { return hash1((float)x * 127.1f + (float)y * 311.7f); }
// Smooth 2D value noise, 0..1.
float vnoise(float x, float y) {
    int ix = (int)std::floor(x), iy = (int)std::floor(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hash2(ix, iy), b = hash2(ix + 1, iy), c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool VectorUniverse::init(int width, int height) {
    progLine_ = ugl::program(vector_shaders::LINE_VS, vector_shaders::LINE_FS, "beams");
    progFill_ = ugl::program(vector_shaders::FILL_VS, vector_shaders::FILL_FS, "fill");
    progDecay_ = ugl::program(ugl::FULLSCREEN_VS, vector_shaders::DECAY_FS, "persistence");
    progDown_ = ugl::program(ugl::FULLSCREEN_VS, vector_shaders::DOWN_FS, "bloom-down");
    progUp_ = ugl::program(ugl::FULLSCREEN_VS, vector_shaders::UP_FS, "bloom-up");
    progComp_ = ugl::program(ugl::FULLSCREEN_VS, vector_shaders::COMPOSITE_FS, "tube");

    // Beam quads: one instanced quad per segment.
    const float corners[8] = {0, -1, 1, -1, 0, 1, 1, 1};
    glGenBuffers(1, &cornerVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, cornerVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    glGenBuffers(1, &beamVbo_);
    glGenVertexArrays(1, &lineVao_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, cornerVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    for (int i = 0; i < 3; i++) {
        glEnableVertexAttribArray(1 + i);
        glVertexAttribDivisor(1 + i, 1);
    }

    glGenBuffers(1, &fillVbo_);
    glGenVertexArrays(1, &fillVao_);
    glBindVertexArray(fillVao_);
    glBindBuffer(GL_ARRAY_BUFFER, fillVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

void VectorUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    px_ = height_ / 1080.f;
    vp_ = layout::camera(width_, height_).vp;
    createTargets();
}

VectorUniverse::~VectorUniverse() {
    destroyTargets();
    for (GLuint p : {progLine_, progFill_, progDecay_, progDown_, progUp_, progComp_})
        if (p) glDeleteProgram(p);
    GLuint bufs[3] = {cornerVbo_, beamVbo_, fillVbo_};
    glDeleteBuffers(3, bufs);
    GLuint vaos[2] = {lineVao_, fillVao_};
    glDeleteVertexArrays(2, vaos);
}

void VectorUniverse::destroyTargets() {
    if (!accFbo_[0]) return;
    glDeleteFramebuffers(2, accFbo_);
    glDeleteTextures(2, accTex_);
    glDeleteRenderbuffers(1, &depthRb_);
    glDeleteFramebuffers(BLOOM_MIPS, bloomFbo_);
    glDeleteTextures(BLOOM_MIPS, bloomTex_);
    accFbo_[0] = 0;
}

void VectorUniverse::createTargets() {
    destroyTargets();
    auto makeTex = [](GLuint& tex, int w, int h) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, w, h, 0, GL_RG, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    glGenRenderbuffers(1, &depthRb_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);
    glGenFramebuffers(2, accFbo_);
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, accFbo_[i]);
        makeTex(accTex_[i], width_, height_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accTex_[i], 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "[gl] accumulation framebuffer incomplete\n");
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    int bw = width_, bh = height_;
    glGenFramebuffers(BLOOM_MIPS, bloomFbo_);
    for (int i = 0; i < BLOOM_MIPS; i++) {
        bw = std::max(1, bw / 2);
        bh = std::max(1, bh / 2);
        bloomW_[i] = bw;
        bloomH_[i] = bh;
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        makeTex(bloomTex_[i], bw, bh);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// =====================================================================================================
// Song and phase: the phosphor and the scene level
// =====================================================================================================

namespace {
// The key-derived hue walks the circle of fifths: neighbouring keys share a tube.
int phosphorFor(const SongLook& l) {
    if (l.variant >= 0) return l.variant % 4;
    int idx = ((int)std::lround(l.hue / TAU * 24.f) % 24 + 24) % 24;
    const int p = ((idx + 3) / 6) % 4;
    return p == l.avoidVariant ? (p + 1 + (int)(l.seed % 3)) % 4 : p;
}
} // namespace

vec3 VectorUniverse::phosColor(int p) const { return PHOSPHORS[((p % 4) + 4) % 4].color; }

TransitionHints VectorUniverse::hints() const {
    TransitionHints h;
    const vec3 c = phosColor(basePhos_);
    h.background = c * 0.004f;
    h.ink = c * 0.6f;
    h.accent = c;
    h.light = false;
    h.styles = TS_REBUILD | TS_RIPPLE | TS_TILES; // beams: fronts and scanned rows suit a tube
    return h;
}

// The retrace bar sweeps down the tube; with a new phosphor, what it has passed shows the new one.
void VectorUniverse::sweep(int want, float seconds, float amount) {
    phosPrev_ = phos_; // a new sweep finishes the previous one at once
    palSweep_ = want != phos_;
    phos_ = want;
    lvFrom_ = lv_;
    sweepT_ = 0.f;
    sweepDur_ = seconds;
    sweepAmt_ = amount;
}

void VectorUniverse::setSong(const SongLook& look, float seconds) {
    seed_ = look.seed;
    basePhos_ = phosphorFor(look);
    const int want = inverted_ ? ALT_PHOSPHOR[basePhos_] : basePhos_;
    if (!hasSong_ || seconds < 0.1f) {
        hasSong_ = true;
        phos_ = phosPrev_ = want;
        palSweep_ = false;
        sweepT_ = 9.f;
        return;
    }
    sweep(want, 1.6f, 1.f);
    lvTo_ = (float)level_;
}

void VectorUniverse::setLevel(int level, float, float seconds, float impact) {
    const int oldLevel = level_;
    level_ = level;
    if (seconds < 0.1f) {
        // Instant (scene viewer, first frame): no sweep.
        lv_ = lvFrom_ = lvTo_ = (float)level;
        phosPrev_ = phos_;
        palSweep_ = false;
        sweepT_ = 9.f;
        return;
    }
    const bool rising = level > oldLevel, levelChange = level != oldLevel;
    sweep(phos_, rising ? 0.7f : (levelChange ? 1.5f : 1.1f), levelChange ? 1.f : 0.25f + 0.5f * impact);
    lvTo_ = (float)level;
    if (rising && level >= 2) degauss_ = 1.f;
}

void VectorUniverse::refresh(float strength) {
    sweep(phos_, 1.1f, 0.25f + 0.5f * strength);
    lvTo_ = (float)level_;
}

void VectorUniverse::setBonus(bool on) {
    if (on == inverted_) return;
    inverted_ = on;
    // The other tube is swept in by the retrace beam (and swept back out when the bonus ends).
    sweep(on ? ALT_PHOSPHOR[basePhos_] : basePhos_, 0.55f, 1.f);
    lvTo_ = (float)level_;
}

void VectorUniverse::levelUp() {
    rings_.push_back({vec3(0, 0, 0), 0.2f, 9.f, 0.f, 1.1f, 1.f, 0, 1.f});
    flashWell_ = 1.f;
}

// =====================================================================================================
// Geometry helpers
// =====================================================================================================

vec2 VectorUniverse::project(const vec3& p) const {
    vec4 c = vp_ * vec4(p, 1.f);
    if (c.w <= 1e-4f) return {-1000, -1000};
    return {(c.x / c.w * 0.5f + 0.5f) * width_, (0.5f - c.y / c.w * 0.5f) * height_};
}

void VectorUniverse::beam(const vec3& a, const vec3& b, float ia, float ib, float ha, float hb, float width, int layer) {
    if (ia <= 0.004f && ib <= 0.004f && ha <= 0.004f && hb <= 0.004f) return;
    if (beams_.size() >= 80000) return;
    const float w = layer == L_SCREEN ? width : width * px_;
    beams_.push_back({vec4(a, std::max(0.55f, w)), vec4(b, (float)layer), vec4(ia, ib, ha, hb)});
}

// The 7 glyphs: every cell is an outlined square; the line pattern inside tells the pieces apart.
//   I diamond   O inner square   T saltire   S hatch   Z double bar   J dot   L cross
void VectorUniverse::glyph(const vec3& c, float h, int type, float in, float hot, int layer, bool outline) {
    auto P = [&](float x, float y) { return c + vec3(x * h, y * h, 0); };
    const float wid = 1.2f;
    if (outline) {
        beam(P(-1, -1), P(1, -1), in, hot, wid, layer);
        beam(P(1, -1), P(1, 1), in, hot, wid, layer);
        beam(P(1, 1), P(-1, 1), in, hot, wid, layer);
        beam(P(-1, 1), P(-1, -1), in, hot, wid, layer);
    }
    const float g = in * 0.8f, gh = hot * 0.8f;
    switch (type) {
    case I: {
        const float r = 0.6f;
        beam(P(0, r), P(r, 0), g, gh, wid, layer);
        beam(P(r, 0), P(0, -r), g, gh, wid, layer);
        beam(P(0, -r), P(-r, 0), g, gh, wid, layer);
        beam(P(-r, 0), P(0, r), g, gh, wid, layer);
        break;
    }
    case O: {
        const float r = 0.45f;
        beam(P(-r, -r), P(r, -r), g, gh, wid, layer);
        beam(P(r, -r), P(r, r), g, gh, wid, layer);
        beam(P(r, r), P(-r, r), g, gh, wid, layer);
        beam(P(-r, r), P(-r, -r), g, gh, wid, layer);
        break;
    }
    case T: {
        const float r = 0.55f;
        beam(P(-r, -r), P(r, r), g, gh, wid, layer);
        beam(P(-r, r), P(r, -r), g, gh, wid, layer);
        break;
    }
    case S: {
        const float r = 0.62f;
        beam(P(-r, -r), P(r, r), g, gh, wid, layer);
        beam(P(-r, 0.f), P(0.f, r), g, gh, wid, layer);
        beam(P(0.f, -r), P(r, 0.f), g, gh, wid, layer);
        break;
    }
    case Z: {
        const float r = 0.58f;
        beam(P(-r, 0.28f), P(r, 0.28f), g, gh, wid, layer);
        beam(P(-r, -0.28f), P(r, -0.28f), g, gh, wid, layer);
        break;
    }
    case J:
        beam(P(0, 0), P(0, 0), g * 1.3f, gh, wid * 2.4f, layer);
        break;
    case L: {
        const float r = 0.6f;
        beam(P(-r, 0), P(r, 0), g, gh, wid, layer);
        beam(P(0, -r), P(0, r), g, gh, wid, layer);
        break;
    }
    default: break;
    }
}

// A cell's glyph turned into free segments (the same lines the cell was drawn with).
void VectorUniverse::shatter(const vec3& c, float h, int type, const vec3& vel, float spread, float life, float delay,
                       float hot) {
    size_t first = beams_.size();
    glyph(c, h, type, 1.f, 0.f, L_BOARD);
    for (size_t i = first; i < beams_.size() && debris_.size() < 6000; i++) {
        const Beam& b = beams_[i];
        vec3 a(b.a.x, b.a.y, b.a.z), e(b.b.x, b.b.y, b.b.z);
        vec3 mid = (a + e) * 0.5f;
        Debris d;
        d.p = mid;
        d.half = (e - a) * 0.5f;
        if (length(d.half) < 1e-3f) d.half = vec3(0.05f, 0, 0); // the dot becomes a short dash
        vec3 out = mid - c;
        d.v = vel + out * spread * 3.f + vec3(rng_.range(-1, 1), rng_.range(-1, 1), rng_.range(-0.5f, 1.f)) * spread;
        d.spin = rng_.range(-1.f, 1.f) * (3.f + 6.f * spread);
        d.maxLife = d.life = life * rng_.range(0.7f, 1.15f);
        d.delay = delay;
        d.inten = 1.f;
        d.hot = hot;
        debris_.push_back(d);
    }
    beams_.resize(first);
}

// =====================================================================================================
// Game events
// =====================================================================================================

void VectorUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::Lock: {
        if (ev.cells.empty()) break;
        // The piece's outer contour flares hot and decays through the phosphor.
        auto inPiece = [&](int x, int y) {
            for (auto& o : ev.cells)
                if (o.x == x && o.y == y) return true;
            return false;
        };
        Flare f;
        f.t = 0;
        vec3 c(0.f);
        for (auto& q : ev.cells) c += layout::cellPos((float)q.x, (float)q.y);
        f.c = c / (float)ev.cells.size();
        const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (auto& q : ev.cells) {
            vec3 p = layout::cellPos((float)q.x, (float)q.y);
            for (int d = 0; d < 4; d++) {
                if (inPiece(q.x + dx[d], q.y + dy[d])) continue;
                vec3 n((float)dx[d], (float)-dy[d], 0);
                vec3 t(-n.y, n.x, 0);
                f.seg.push_back(p + n * 0.5f - t * 0.5f);
                f.seg.push_back(p + n * 0.5f + t * 0.5f);
            }
        }
        flares_.push_back(std::move(f));
        break;
    }
    case GameEvent::HardDrop: {
        // Speed lines: the piece's columns, from where it was to where it landed.
        if (ev.count < 2) break;
        for (auto& q : ev.cells) {
            bool top = true;
            for (auto& o : ev.cells)
                if (o.x == q.x && o.y < q.y) top = false;
            if (!top) continue;
            streaks_.push_back({layout::cellPos((float)q.x, 0).x, layout::cellPos(0, (float)(q.y - ev.count)).y + 0.5f,
                                layout::cellPos(0, (float)q.y).y + 0.5f, 0.f});
        }
        break;
    }
    case GameEvent::Clear: {
        const bool tetris = ev.count >= 4;
        float yc = 0;
        for (auto& c : ev.cells) yc += layout::cellPos(0, (float)c.y).y;
        yc /= std::max<size_t>(1, ev.cells.size());
        // Each cleared cell's lines fly apart (vector debris); the row's beam collapses to the middle.
        for (auto& c : ev.cells) {
            vec3 p = layout::cellPos((float)c.x, (float)c.y);
            vec3 vel((c.x - 4.5f) * (tetris ? 0.9f : 0.5f), rng_.range(-0.5f, 1.5f), rng_.range(0.5f, 3.f));
            shatter(p, 0.42f, c.type, vel, tetris ? 1.5f : 0.8f, tetris ? 1.3f : 1.1f, std::fabs(c.x - 4.5f) * 0.012f,
                    tetris ? 0.45f : 0.8f);
        }
        std::vector<int> rows;
        for (auto& c : ev.cells)
            if (std::find(rows.begin(), rows.end(), c.y) == rows.end()) rows.push_back(c.y);
        for (int y : rows) rowFlashes_.push_back({layout::cellPos(0, (float)y).y, 0.f});
        if (tetris) {
            // A vector shockwave and beam overdrive.
            rings_.push_back({vec3(0, yc, 0), 0.5f, 34.f, 0.f, 1.1f, 0.9f, 128, 1.f});
            if (ev.backToBack) rings_.push_back({vec3(0, yc, 0), 0.5f, 22.f, -0.15f, 1.1f, 0.7f, 4, 1.f});
            overdrive_ = 1.f;
        } else {
            overdrive_ = std::max(overdrive_, 0.12f * ev.count);
        }
        break;
    }
    case GameEvent::BonusReady:
        readyFlash_ = 1.f;
        break;
    case GameEvent::TopOut: {
        // The stack goes to pieces from the top, then the tube switches off.
        int topRow = Game::H;
        for (auto& c : ev.cells) topRow = std::min(topRow, c.y);
        for (auto& c : ev.cells) {
            vec3 p = layout::cellPos((float)c.x, (float)c.y);
            float delay = (c.y - topRow) * 0.03f;
            vec3 vel((c.x - 4.5f) * 0.3f, rng_.range(-1.f, 1.5f), rng_.range(0.f, 2.f));
            shatter(p, 0.42f, c.type, vel, 0.7f, 1.3f, delay, 0.6f);
        }
        overT_ = 0.f;
        break;
    }
    default: break;
    }
}

// =====================================================================================================
// The world: the horizon trace, the floor grid that becomes mountains, the Lissajous sky
// =====================================================================================================

void VectorUniverse::buildWorld(const MusicState& music) {
    const float wGrid = smoothstepf(0.15f, 1.f, lv_), wPeak = smoothstepf(1.1f, 2.f, lv_);
    const float E = music.energy;
    const float gain = (1.f - 0.7f * dimT_) * (1.f - bridge_.amount); // the bridge: the world fades out
    auto band = [&](int b) { return music.bandsFast[std::clamp(b, 0, NUM_BANDS - 1)]; };
    const float bass = (band(0) + band(1) + band(2)) / 3.f;
    const float mid = (band(5) + band(6) + band(7) + band(8)) / 4.f;
    const float high = (band(11) + band(12) + band(13) + band(14)) / 4.f;
    const float t = worldTime_;

    // ---- The horizon: one oscilloscope trace of the music, all the way across.
    {
        const int N = 260;
        const float z = -95.f, y0 = camPos_.y;
        const float A = (3.5f + 2.5f * wPeak) * (0.35f + 0.9f * music.audio.loud) * (1.f + 0.3f * throb_);
        const float in = (0.95f - 0.3f * wGrid + 0.2f * wPeak) * gain;
        vec3 prev;
        for (int i = 0; i <= N; i++) {
            const float x = -120.f + 240.f * i / N;
            float y = A * ((0.2f + bass) * 0.8f * std::sin(x * TAU / 75.f + t * 0.9f) +
                           (0.12f + mid) * 0.45f * std::sin(x * TAU / 24.f - t * 1.7f) +
                           (0.05f + high) * 0.2f * std::sin(x * TAU / 7.f + t * 4.3f));
            vec3 p(x, y0 + y, z);
            if (i) beam(prev, p, in, in, 0.04f * in, 0.04f * in, 1.25f, L_WORLD);
            prev = p;
        }
    }

    // ---- The floor: a grid scrolling one row per beat; at peaks the spectrum lifts it into mountains.
    if (wGrid > 0.01f) {
        constexpr float DX = 3.1f, DZ = 4.f, Z0 = 4.f;
        const float frac = scroll_ - std::floor(scroll_);
        const int base = (int)std::floor(scroll_);
        auto& H = H_;
        auto& IN = IN_;
        auto& ZK = ZK_;
        // A beam of light runs along the grid from the horizon to the viewer once per beat.
        const float zw = -150.f + 158.f * music.beatPhase;
        const float pulseAmp = wGrid * (0.2f + 0.6f * E) * (0.5f + 0.5f * std::min(1.f, music.beatPulse * 1.5f));
        for (int k = 0; k < NR; k++) {
            const float z = Z0 - (k - frac) * DZ;
            ZK[k] = z;
            const float far = 1.f - smoothstepf(-70.f, -150.f, z);
            const float near = smoothstepf(14.f, 2.f, z);
            const float pulse = std::exp(-((z - zw) * (z - zw)) / 60.f);
            IN[k] = (0.26f * far * near + pulse * pulseAmp * far) * wGrid * gain;
            for (int i = 0; i < NC; i++) {
                const float x = (i - NC / 2) * DX;
                float h = 0.f;
                if (wPeak > 0.001f) {
                    const float u = saturate((std::fabs(x) - 12.f) / 50.f);
                    const float env = smoothstepf(0.f, 0.35f, u) * smoothstepf(-14.f, -60.f, z);
                    const float prof = vnoise(i * 0.3f + (x < 0 ? 31.f : 0.f), (k + base) * 0.3f);
                    const float ridge = smoothstepf(0.25f, 0.85f, prof);
                    h = wPeak * env * (0.6f + 11.f * ridge * ridge) * (0.25f + 1.f * band((int)(u * 15.99f))) *
                        (1.f + 0.35f * kick_);
                }
                H[k][i] = FLOOR_Y + h;
            }
        }
        auto P = [&](int k, int i) { return vec3((i - NC / 2) * DX, H[k][i], ZK[k]); };
        const float longI = 1.f - 0.72f * wPeak, transI = 1.f + 0.25f * wPeak;
        for (int k = 0; k < NR; k++)
            for (int i = 0; i < NC; i++) {
                const float hk = (H[k][i] - FLOOR_Y) / 18.f;
                const float hot = wPeak * kick_ * 0.35f * hk;
                if (i + 1 < NC) {
                    const float h2 = wPeak * kick_ * 0.35f * (H[k][i + 1] - FLOOR_Y) / 18.f;
                    beam(P(k, i), P(k, i + 1), IN[k] * transI * (1.f + 0.6f * hk), IN[k] * transI *
                         (1.f + 0.6f * (H[k][i + 1] - FLOOR_Y) / 18.f), hot * IN[k], h2 * IN[k], 1.05f, L_WORLD);
                }
                if (k + 1 < NR) beam(P(k, i), P(k + 1, i), IN[k] * longI, IN[k + 1] * longI, 0, 0, 1.05f, L_WORLD);
            }
        // Hidden lines: the mountains hide what's behind them (a depth-only surface, never seen).
        if (wPeak > 0.01f) {
            fill_.reserve(NR * NC * 18);
            for (int k = 0; k + 1 < NR; k++)
                for (int i = 0; i + 1 < NC; i++) {
                    const vec3 a = P(k, i), b = P(k, i + 1), c = P(k + 1, i + 1), d = P(k + 1, i);
                    for (const vec3& v : {a, b, c, a, c, d}) {
                        fill_.push_back(v.x);
                        fill_.push_back(v.y);
                        fill_.push_back(v.z);
                    }
                }
        }
    }

    // ---- Peak: two Lissajous scopes in the outer sky (bass against mids on the left, mids against highs on the
    // right), slowly turning, set behind the mountains.
    if (wPeak > 0.01f) {
        static const int RATIOS[4][2] = {{3, 2}, {2, 3}, {3, 4}, {4, 3}};
        const int* r = RATIOS[seed_ % 4];
        const int N = 240;
        for (int side = -1; side <= 1; side += 2) {
            const float ax = side < 0 ? bass : mid, ay = side < 0 ? mid : high;
            const vec3 C(side * 44.f, 13.f, -60.f);
            const float rad = 4.6f * (0.75f + 0.35f * ax) * (1.f + 0.08f * kick_);
            const float ry = 4.6f * (0.75f + 0.35f * ay);
            const float delta = t * 0.31f * side;
            const float in = 0.42f * wPeak * gain;
            vec3 prev;
            for (int j = 0; j <= N; j++) {
                const float th = TAU * j / N;
                const float u = r[0] * th + delta;
                vec3 p = C + vec3(rad * std::sin(u), ry * std::sin(r[1] * th), rad * std::cos(u));
                if (j) beam(prev, p, in, in, 0.05f * in, 0.05f * in, 1.1f, L_WORLD);
                prev = p;
            }
        }
    }
}

// =====================================================================================================
// The board: the well, the stack, the piece, ghost, previews, gauge
// =====================================================================================================

void VectorUniverse::buildBoard(const Game& g, float dt) {
    const float gain = 1.f - 0.75f * dimT_;
    const float th = throb_;

    // ---- The well: a crisp open box in perspective, with graduations like an instrument.
    {
        const float in = (0.62f + 0.12f * th) * gain, hot = 0.6f * flashWell_ * gain;
        const vec3 bl(-WELL_X, WELL_BOT, 0), br(WELL_X, WELL_BOT, 0), tl(-WELL_X, WELL_TOP, 0), tr(WELL_X, WELL_TOP, 0);
        const vec3 back(0, 0, -WELL_DEPTH);
        beam(tl, bl, in, hot, 1.5f, L_BOARD);
        beam(bl, br, in, hot, 1.5f, L_BOARD);
        beam(br, tr, in, hot, 1.5f, L_BOARD);
        beam(tl, tl + vec3(-0.45f, 0, 0), in, hot, 1.5f, L_BOARD);
        beam(tr, tr + vec3(0.45f, 0, 0), in, hot, 1.5f, L_BOARD);
        const float bi = 0.2f * gain * (1.f + 0.3f * th);
        beam(tl + back, bl + back, bi, 0, 1.1f, L_BOARD);
        beam(bl + back, br + back, bi, 0, 1.1f, L_BOARD);
        beam(br + back, tr + back, bi, 0, 1.1f, L_BOARD);
        for (const vec3& c : {bl, br, tl, tr}) beam(c, c + back, bi * 1.3f, 0, 1.1f, L_BOARD);
        // Graduations on the back wall: a tick per row, longer every five.
        for (int r = 1; r < 20; r++) {
            const float y = WELL_BOT + 0.12f + r;
            const float len = r % 5 == 0 ? 0.42f : 0.18f;
            const float ti = (r % 5 == 0 ? 0.24f : 0.14f) * gain;
            beam(vec3(-WELL_X, y, -WELL_DEPTH), vec3(-WELL_X + len, y, -WELL_DEPTH), ti, 0, 1.f, L_BOARD);
            beam(vec3(WELL_X, y, -WELL_DEPTH), vec3(WELL_X - len, y, -WELL_DEPTH), ti, 0, 1.f, L_BOARD);
        }
        for (int c = 1; c < 10; c++) {
            const float x = -WELL_X + 0.12f + c;
            beam(vec3(x, WELL_BOT, -WELL_DEPTH), vec3(x, WELL_BOT + 0.18f, -WELL_DEPTH), 0.14f * gain, 0, 1.f, L_BOARD);
        }
    }

    // ---- The stack.
    const float CELL = 0.4f; // half size of a cell's square (a small gap between neighbours)
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const float age = (1.f - c.flash) * Game::LOCK_FX_SECONDS;
            const float hot = std::exp(-age / 0.16f) * (c.dropped ? 1.1f : 0.8f);
            const float in = (0.6f + 0.06f * th + 0.5f * std::exp(-age / 0.5f)) * gain;
            glyph(layout::cellPos((float)x, y - g.rowOffset(y)), CELL, c.type, in, hot * gain, L_BOARD);
        }

    // ---- The piece in play, and its ghost (a dashed contour where it will land).
    if (g.hasPiece() && !g.over()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        const int gy = g.ghostY();
        if (gy > p.y) {
            auto inPiece = [&](int x, int y) {
                for (auto& q : cells)
                    if (q[0] == x && q[1] == y) return true;
                return false;
            };
            const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            const float gi = 0.3f * gain;
            for (auto& q : cells) {
                const vec3 c = layout::cellPos((float)q[0], (float)(q[1] + gy - p.y));
                for (int d = 0; d < 4; d++) {
                    if (inPiece(q[0] + dx[d], q[1] + dy[d])) continue;
                    vec3 n((float)dx[d], (float)-dy[d], 0), t(-n.y, n.x, 0);
                    const vec3 a = c + n * 0.46f - t * 0.46f;
                    for (int s = 0; s < 3; s++) {
                        const float f0 = (s + 0.18f) / 3.f, f1 = (s + 0.82f) / 3.f;
                        beam(a + t * (0.92f * f0), a + t * (0.92f * f1), gi, 0, 1.1f, L_BOARD);
                    }
                }
            }
        }
        const float dyv = g.pieceVisualY() - p.y;
        const float hot = 0.06f + 0.35f * g.lockProgress() * g.lockProgress();
        for (auto& q : cells) {
            if (q[1] + dyv < 0.6f) continue;
            glyph(layout::cellPos((float)q[0], q[1] + dyv), CELL, p.type, (1.05f + 0.1f * th) * gain, hot * gain, L_BOARD);
        }
    }

    // ---- Previews: hold on the left, the next queue on the right.
    auto preview = [&](int type, vec3 center, float sc, float in) {
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        g.pieceCells(p, cells);
        float minx = 9, maxx = -9, miny = 9, maxy = -9;
        for (auto& c : cells) {
            minx = std::min(minx, (float)c[0]); maxx = std::max(maxx, (float)c[0]);
            miny = std::min(miny, (float)c[1]); maxy = std::max(maxy, (float)c[1]);
        }
        for (auto& c : cells) {
            vec3 pos = center + vec3((c[0] - (minx + maxx) * 0.5f) * sc, -(c[1] - (miny + maxy) * 0.5f) * sc, 0);
            glyph(pos, CELL * sc, type, in * gain, 0, L_BOARD);
        }
    };
    if (g.holdType() >= 0) preview(g.holdType(), vec3(-8.6f, 7.2f, 0), 0.62f, g.holdUsed() ? 0.3f : 0.85f);
    for (int i = 0; i < 3; i++) preview(g.next(i), vec3(8.6f, 7.2f - i * 3.2f, 0), i == 0 ? 0.62f : 0.5f, i == 0 ? 0.9f : 0.55f);
    // Thin rules under the HOLD / NEXT labels.
    for (float sx : {-1.f, 1.f}) {
        const float in = 0.3f * gain;
        const vec3 a(sx * 6.6f, 8.85f, 0), b(sx * 10.6f, 8.85f, 0);
        beam(a, b, in, 0, 1.f, L_BOARD);
        beam(a, a + vec3(0, -0.2f, 0), in, 0, 1.f, L_BOARD);
        beam(b, b + vec3(0, -0.2f, 0), in, 0, 1.f, L_BOARD);
    }

    // ---- Bonus gauge: a dial of ticks; full, it breathes hot; in use, it drains.
    {
        const float target = g.bonusGauge();
        gaugeShown_ = approach(gaugeShown_, target, 6.f, dt);
        readyFlash_ = approach(readyFlash_, 0.f, 2.f, dt);
        const vec3 gc = layout::GAUGE_CENTER;
        const int N = 36;
        const bool ready = g.bonusReady(), active = g.bonusActive();
        const float breathe = ready ? 0.5f + 0.5f * std::sin(worldTime_ * 4.f) : 0.f;
        for (int j = 0; j < N; j++) {
            const float a = PI * 0.5f - TAU * (j + 0.5f) / N;
            const vec3 d(std::cos(a), std::sin(a), 0);
            const bool lit = (j + 0.5f) / N <= gaugeShown_;
            float in = lit ? 0.8f : 0.13f, hot = 0.f;
            if (lit && ready) { in = 0.9f; hot = 0.15f + 0.35f * breathe + readyFlash_; }
            if (lit && active) hot = 0.2f;
            const float r0 = (j % 3 == 0) ? 0.68f : 0.76f;
            beam(gc + d * (layout::GAUGE_R * r0), gc + d * layout::GAUGE_R, in * gain, hot * gain, 1.1f, L_BOARD);
        }
    }
}

// =====================================================================================================
// Effects: debris, shockwaves, flares
// =====================================================================================================

void VectorUniverse::buildFx(float dt) {
    const float gain = 1.f;
    // Vector debris: free segments spinning apart, fading through the phosphor.
    for (auto& d : debris_) {
        if (d.delay > 0) {
            d.delay -= dt;
            beam(d.p - d.half, d.p + d.half, 0.8f * gain, 0.3f * d.hot, 1.2f, L_BOARD);
            continue;
        }
        d.life -= dt;
        d.p += d.v * dt;
        d.v = d.v * std::exp(-1.1f * dt) + vec3(0, -2.5f * dt, 0);
        const float a = d.spin * dt, cs = std::cos(a), sn = std::sin(a);
        d.half = vec3(d.half.x * cs - d.half.y * sn, d.half.x * sn + d.half.y * cs, d.half.z);
        const float k = saturate(d.life / d.maxLife);
        beam(d.p - d.half, d.p + d.half, (0.2f + 0.7f * k) * k * gain, d.hot * k * k * k * gain, 1.25f, L_BOARD);
    }
    debris_.erase(std::remove_if(debris_.begin(), debris_.end(), [](const Debris& d) { return d.life <= 0; }),
                  debris_.end());

    // Shockwave rings (circles in the board plane; sides == 0: the well's outline growing outward).
    for (auto& r : rings_) {
        r.t += dt;
        if (r.t < 0) continue;
        const float u = saturate(r.t / r.life), k = 1.f - u;
        const float rad = r.r0 + r.speed * r.life * (1.f - (1.f - u) * (1.f - u)) * 0.6f;
        const float in = r.inten * k * k * k, hot = r.inten * k * k * k * k;
        if (r.sides == 0) {
            const float hx = WELL_X + rad, hy = (WELL_TOP - WELL_BOT) * 0.5f + rad, cy = (WELL_TOP + WELL_BOT) * 0.5f;
            const vec3 a(-hx, cy - hy, 0), b(hx, cy - hy, 0), c(hx, cy + hy, 0), d(-hx, cy + hy, 0);
            beam(a, b, in, hot, 1.6f, L_BOARD);
            beam(b, c, in, hot, 1.6f, L_BOARD);
            beam(c, d, in, hot, 1.6f, L_BOARD);
            beam(d, a, in, hot, 1.6f, L_BOARD);
            continue;
        }
        vec3 prev;
        for (int j = 0; j <= r.sides; j++) {
            const float a = TAU * j / r.sides + (r.sides == 4 ? PI * 0.25f : 0.f);
            vec3 p = r.c + vec3(std::cos(a) * rad, std::sin(a) * rad * r.aspect, 0);
            if (j) beam(prev, p, in, hot, 1.7f, L_BOARD);
            prev = p;
        }
    }
    rings_.erase(std::remove_if(rings_.begin(), rings_.end(), [](const Ring& r) { return r.t >= r.life; }), rings_.end());

    // Lock flares: the piece's contour, hot, opening slightly as it fades.
    for (auto& f : flares_) {
        f.t += dt;
        const float u = saturate(f.t / 0.42f), k = 1.f - u;
        const float grow = 1.f + 0.1f * std::sqrt(u);
        for (size_t i = 0; i + 1 < f.seg.size(); i += 2)
            beam(f.c + (f.seg[i] - f.c) * grow, f.c + (f.seg[i + 1] - f.c) * grow, 0.9f * k * k, 1.1f * k * k * k, 1.5f,
                 L_BOARD);
    }
    flares_.erase(std::remove_if(flares_.begin(), flares_.end(), [](const Flare& f) { return f.t >= 0.42f; }),
                  flares_.end());

    // Cleared rows: a hot beam across the row, collapsing to its middle.
    for (auto& r : rowFlashes_) {
        r.t += dt;
        const float u = saturate(r.t / 0.32f), k = 1.f - u;
        const float hl = (WELL_X + 0.3f) * std::pow(k, 0.6f);
        beam(vec3(-hl, r.y, 0.1f), vec3(hl, r.y, 0.1f), 1.2f * k, 1.6f * k * k, 1.8f, L_BOARD);
    }
    rowFlashes_.erase(std::remove_if(rowFlashes_.begin(), rowFlashes_.end(), [](const RowFlash& r) { return r.t >= 0.32f; }),
                      rowFlashes_.end());

    // Hard drop streaks.
    for (auto& s : streaks_) {
        s.t += dt;
        const float k = 1.f - saturate(s.t / 0.22f);
        beam(vec3(s.x, s.y0, 0), vec3(s.x, s.y1, 0), 0.f, 0.7f * k * k, 0.f, 0.4f * k * k, 1.3f, L_BOARD);
    }
    streaks_.erase(std::remove_if(streaks_.begin(), streaks_.end(), [](const Streak& s) { return s.t >= 0.22f; }),
                   streaks_.end());
}

// =====================================================================================================
// HUD: stroke text in screen pixels
// =====================================================================================================

float VectorUniverse::textWidth(const std::string& text, float scale) {
    if (text.empty()) return 0.f;
    const float u = scale * FONT_UNIT;
    size_t n = 0;
    for (unsigned char c : text) n += (c & 0xC0) != 0x80; // UTF-8: one glyph per character
    return n * 6.f * u - 2.f * u;
}

void VectorUniverse::strokeText(const std::string& s, float x, float y, float scale, float in, float hot) {
    const float u = scale * FONT_UNIT;
    const float wid = std::max(0.85f, 0.42f * scale);
    float cx = x;
    for (size_t i = 0; i < s.size(); i++) {
        char ch = s[i];
        if ((unsigned char)ch == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0xB7) { ch = '\x01'; i++; } // "·"
        const char* st = ch == '\x01' ? "23 23" : glyphStrokes((char)std::toupper((unsigned char)ch));
        if (st) {
            bool have = false;
            vec3 prev;
            for (const char* q = st; *q;) {
                if (*q == '|') { have = false; q++; continue; }
                if (*q == ' ') { q++; continue; }
                const float gx = (float)(q[0] - '0'), gy = (float)(q[1] - '0');
                q += 2;
                vec3 p(cx + gx * u, y + (6.f - gy) * u, 0);
                if (have) beam(prev, p, in, hot, wid, L_SCREEN); // a repeated point draws a dot
                prev = p;
                have = true;
            }
        }
        cx += 6.f * u;
    }
}

void VectorUniverse::buildHud(const std::vector<HudText>& hud) {
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        float x = h.x;
        const float wdt = textWidth(h.text, h.scale);
        if (h.center) x -= wdt * 0.5f;
        else if (x < 0) x = width_ + x - wdt;
        const float a = std::min(1.f, h.alpha);
        strokeText(h.text, x, h.y, h.scale, (h.accent ? 1.05f : 0.95f) * a, h.accent ? 0.22f * a : 0.f);
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void VectorUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const double time = f.time;
    const float dt = f.dt;
    const bool paused = f.paused;
    // ---- Clocks.
    slowAmt_ = approach(slowAmt_, inverted_ ? 1.f : 0.f, 3.f, dt);
    const float worldRate = lerpf(1.f, 0.35f, slowAmt_);
    const float wdt = paused ? 0.f : dt * worldRate;
    worldTime_ += wdt;
    if (!paused) {
        float d = music.beatPhase - lastBeatPhase_;
        if (d < -0.5f) { d += 1.f; beats_ += 1.f; }
        if (d < 0.f || d > 0.5f) d = 0.f;
        scroll_ += d * worldRate;
    }
    lastBeatPhase_ = music.beatPhase;

    sweepT_ += dt;
    const float sp = saturate(sweepT_ / sweepDur_);
    if (sp >= 1.f) { phosPrev_ = phos_; palSweep_ = false; }
    lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.2f, 0.95f, sp));
    degauss_ = std::max(0.f, degauss_ - dt * 1.4f);
    overdrive_ = approach(overdrive_, 0.f, 3.f, dt);
    flashWell_ = approach(flashWell_, 0.f, 4.f, dt);
    kick_ = std::max(approach(kick_, 0.f, 6.f, dt), music.kick);
    throb_ = approach(throb_, saturate(music.beatPulse), 12.f, dt);
    dimT_ = approach(dimT_, f.dim || paused ? 1.f : 0.f, 3.f, dt);
    if (overT_ >= 0.f) {
        if (!game.over()) overT_ = -1.f;
        else overT_ += dt;
    }
    const float wPeak = smoothstepf(1.1f, 2.f, lv_), wGrid = smoothstepf(0.15f, 1.f, lv_);

    // ---- Camera: the layout's front view; peaks breathe (a slow dolly) and lean in on kicks. On the bridge the
    // camera settles back on the layout.
    camDolly_ = approach(camDolly_, wPeak * (0.5f + 0.5f * std::sin((beats_ + music.beatPhase) * TAU / 32.f)), 1.5f, dt);
    const float still = 1.f - bridge_.amount;
    const float yaw = (0.008f + 0.018f * wPeak) * std::sin(worldTime_ * 0.17f) * still;
    const layout::Camera cam =
        layout::camera(width_, height_, 1.f - (0.035f * camDolly_ + 0.01f * kick_ * wPeak) * still, yaw);
    camPos_ = cam.eye;
    view_ = cam.view;
    proj_ = cam.proj;
    vp_ = cam.vp;

    // ---- Beams.
    beams_.clear();
    fill_.clear();
    buildWorld(music);
    const size_t worldEnd = beams_.size();
    buildBoard(game, dt);
    buildFx(paused ? 0.f : dt);
    const size_t boardEnd = beams_.size();
    buildHud(f.hud);

    // ---- Persistence: last frame decays into this one.
    const int prev = accCur_;
    accCur_ ^= 1;
    glBindFramebuffer(GL_FRAMEBUFFER, accFbo_[accCur_]);
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(progDecay_);
    set1i(progDecay_, "uPrev", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, accTex_[prev]);
    float tau = lerpf(0.035f, 0.025f, wGrid);           // calm parts linger a little longer
    tau = lerpf(tau, 0.16f, slowAmt_);                  // slow motion: long, beautiful trails
    if (overT_ >= 0.f) tau = std::max(tau, 0.1f);
    set2f(progDecay_, "uDecay", std::exp(-dt / tau), std::exp(-dt / (tau * 0.5f)));
    ugl::drawFullscreen();
    glClear(GL_DEPTH_BUFFER_BIT);

    // ---- Terrain surface, depth only (hidden-line removal).
    if (!fill_.empty()) {
        glUseProgram(progFill_);
        setMat(progFill_, "uVP", vp_);
        set1i(progFill_, "uScreen", 0);
        glBindVertexArray(fillVao_);
        glBindBuffer(GL_ARRAY_BUFFER, fillVbo_);
        glBufferData(GL_ARRAY_BUFFER, fill_.size() * sizeof(float), fill_.data(), GL_STREAM_DRAW);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.f, 4.f);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(fill_.size() / 3));
        glDisable(GL_POLYGON_OFFSET_FILL);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_DEPTH_TEST);
    }

    // ---- Beams, max-blended (crossings and joints never bead up).
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, beamVbo_);
    glBufferData(GL_ARRAY_BUFFER, beams_.size() * sizeof(Beam), beams_.data(), GL_STREAM_DRAW);
    glUseProgram(progLine_);
    setMat(progLine_, "uVP", vp_);
    set2f(progLine_, "uRes", (float)width_, (float)height_);
    set1f(progLine_, "uTime", (float)time);
    set1f(progLine_, "uJitter", 0.35f * px_ + 0.6f * degauss_ * px_);
    set1f(progLine_, "uHalo", 0.07f);
    {
        // World beams fade behind the well's glass and the HUD columns.
        const vec3 q[4] = {{-WELL_X - 0.05f, WELL_BOT - 0.05f, 0}, {WELL_X + 0.05f, WELL_BOT - 0.05f, 0},
                           {WELL_X + 0.05f, WELL_TOP + 1.6f, 0}, {-WELL_X - 0.05f, WELL_TOP + 1.6f, 0}};
        float qv[8];
        for (int i = 0; i < 4; i++) {
            vec2 p = project(q[i]);
            qv[i * 2] = p.x;
            qv[i * 2 + 1] = height_ - p.y;
        }
        glUniform2fv(U(progLine_, "uQuad"), 4, qv);
        auto rect = [&](float x0, float y0, float x1, float y1, float* o) {
            vec2 a = project(vec3(x0, y0, 0)), b = project(vec3(x1, y1, 0));
            o[0] = std::min(a.x, b.x); o[2] = std::max(a.x, b.x);
            o[1] = height_ - std::max(a.y, b.y); o[3] = height_ - std::min(a.y, b.y);
        };
        float rv[8];
        rect(-11.2f, -6.8f, -6.0f, 10.4f, rv);
        rect(6.0f, -3.6f, 11.2f, 10.4f, rv + 4);
        glUniform4fv(U(progLine_, "uRects"), 2, rv);
        set1f(progLine_, "uQuadDim", 0.035f);
        set1f(progLine_, "uRectDim", 0.3f);
    }
    auto drawBeams = [&](size_t first, size_t count) {
        if (!count) return;
        for (int i = 0; i < 3; i++)
            glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(Beam),
                                  (void*)(first * sizeof(Beam) + i * 4 * sizeof(float)));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)count);
    };
    glEnable(GL_BLEND);
    glBlendEquation(GL_MAX);
    if (!fill_.empty()) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_FALSE);
    }
    drawBeams(0, worldEnd);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    drawBeams(worldEnd, boardEnd - worldEnd);
    // zenscene's menu panels: dark glass under the text (or a lit bar for the accent ones).
    if (!f.panels.empty()) {
        glUseProgram(progFill_);
        set1i(progFill_, "uScreen", 1);
        set2f(progFill_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(fillVao_);
        glBindBuffer(GL_ARRAY_BUFFER, fillVbo_);
        for (const HudRect& r : f.panels) {
            const float q[18] = {r.x, r.y, 0, r.x + r.w, r.y, 0, r.x + r.w, r.y + r.h, 0,
                                 r.x, r.y, 0, r.x + r.w, r.y + r.h, 0, r.x, r.y + r.h, 0};
            glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STREAM_DRAW);
            if (r.accent || r.custom) {
                glBlendEquation(GL_MAX);
                const float v = r.custom ? luminance(r.color) : 0.35f;
                set4f(progFill_, "uColor", v * r.alpha, 0.f, 0.f, 1.f);
            } else {
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                set4f(progFill_, "uColor", 0.f, 0.f, 0.f, std::min(1.f, r.alpha * 1.2f));
            }
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glBlendEquation(GL_MAX);
        glUseProgram(progLine_);
        glBindVertexArray(lineVao_);
        glBindBuffer(GL_ARRAY_BUFFER, beamVbo_);
    }
    drawBeams(boardEnd, beams_.size() - boardEnd);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_BLEND);

    // ---- Tight bloom on the beams.
    glUseProgram(progDown_);
    set1i(progDown_, "uTex", 0);
    GLuint src = accTex_[accCur_];
    int sw = width_, sh = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        glViewport(0, 0, bloomW_[i], bloomH_[i]);
        glBindTexture(GL_TEXTURE_2D, src);
        set2f(progDown_, "uTexel", 0.5f / sw, 0.5f / sh);
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

    // ---- The tube.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progComp_);
    set1i(progComp_, "uAccum", 0);
    set1i(progComp_, "uBloom", 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, accTex_[accCur_]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
    glActiveTexture(GL_TEXTURE0);
    set2f(progComp_, "uRes", (float)width_, (float)height_);
    set3f(progComp_, "uPhos0", phosColor(palSweep_ ? phosPrev_ : phos_));
    set3f(progComp_, "uPhos1", phosColor(phos_));
    const float bar = -0.04f + 1.08f * sp;
    set1f(progComp_, "uSplit", palSweep_ ? bar : -1.f);
    set1f(progComp_, "uBar", bar);
    set1f(progComp_, "uBarAmt", sp < 1.f ? sweepAmt_ * std::min(1.f, (1.f - sp) * 6.f) * std::min(1.f, sp * 20.f) : 0.f);
    set1f(progComp_, "uGain", 1.f + 0.35f * overdrive_ + 0.12f * kick_ * wPeak);
    set1f(progComp_, "uBloomAmt", 0.25f + 0.3f * overdrive_ + 0.15f * kick_ * wPeak);
    set1f(progComp_, "uCurv", 0.07f * (1.f - bridge_.amount)); // the glass flattens onto the bridge
    set1f(progComp_, "uScan", 0.14f);
    set1f(progComp_, "uTime", (float)time);
    set1f(progComp_, "uDegauss", degauss_ * degauss_);
    set1f(progComp_, "uFade", f.fade);
    set1f(progComp_, "uBridge", bridge_.amount);
    set3f(progComp_, "uBridgeColor", bridge_.color);
    set3f(progComp_, "uBridgeInk", bridge_.ink);
    // Game over: the tube switches off (picture to a line, line to a dot), then comes back on, dim.
    float sx = 1.f, sy = 1.f, dot = 0.f;
    if (overT_ >= 0.f) {
        const float t = overT_;
        const float LINE = 0.004f, DOT = 0.004f;
        if (t < 0.9f) {
        } else if (t < 1.15f) {
            const float k = smoothstepf(0.9f, 1.15f, t);
            sy = lerpf(1.f, LINE, k * k);
            dot = 0.7f * k;
        } else if (t < 1.4f) {
            const float k = smoothstepf(1.15f, 1.4f, t);
            sy = LINE;
            sx = lerpf(1.f, DOT, k * k);
            dot = 0.8f;
        } else if (t < 2.2f) {
            sy = LINE;
            sx = DOT;
            dot = 0.9f * (1.f - smoothstepf(1.4f, 2.2f, t));
        } else if (t < 2.5f) {
            const float k = smoothstepf(2.2f, 2.5f, t);
            sy = lerpf(LINE, 1.f, k);
            dot = 0.4f * (1.f - k);
        }
    }
    set2f(progComp_, "uCollapse", sx, sy);
    set1f(progComp_, "uDot", dot);
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_vector() {
    static const UniverseEntry e{
        "VECTOR", "phosphor beams on a dark CRT", [] { return std::unique_ptr<Universe>(new VectorUniverse()); },
        // Driving, electronic songs suit the tube best; anything can play on it.
        [](const SongLook& l) { return 0.7f + 0.6f * smoothstepf(95.f, 135.f, l.bpm) + 0.3f * l.density; },
        {PHOSPHORS[0].name, PHOSPHORS[1].name, PHOSPHORS[2].name, PHOSPHORS[3].name}};
    return e;
}
