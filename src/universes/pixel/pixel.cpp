// PIXEL: a hand-made pixel-art world at a true low resolution.
//
// The whole universe is drawn on the CPU into an indexed canvas where one board cell is 8 x 8 pixels (about
// 340 x 190 pixels on a 16:9 screen), in one variant's sixteen colors; the canvas is then palette-mapped and
// scaled up to the window with nearest filtering, so every pixel is a crisp square on one fixed grid. The grid
// is anchored on the well: the well's edges fall exactly on the shared layout's board rectangle (the canvas
// pixel is the layout cell size / 8, whatever the window), so the stack sits where every universe puts it.
// Gradients (sky, light, fog, fades) are ordered Bayer dithers; motion is in whole pixels, sprite animation is
// stepped (6 to 12 frames per second), and the beat makes things hop by a pixel.
//
// Calm is a still landscape at its golden hour with a few living details; mid slides the near layer in, sways
// the parallax and lets critters loose on the beat; peak brings the weather and the place's big event, and the
// foreground. Locks squash for two frames and puff dust; cleared rows flash and burst into their pixels; a
// Tetris fires a screen-wide pixel firework; the slow motion sweeps the night palette in from the well (stars,
// a moon); a game over crumbles the stack pixel by pixel into a sand pile. Changing universe, the world
// pixelates and dissolves (ordered dither) into the bridge while the stack turns to plain ink.
#include "universes/pixel/pixel.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "layout.hpp"
#include "ugl.hpp"
#include "universes/registry.hpp"

using namespace pixu;

namespace {

const char* UPSCALE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uTex;
uniform vec4 uMap; // canvas origin (window pixels, top-left origin), canvas pixel size, window height
void main() {
    vec2 p = vec2(gl_FragCoord.x, uMap.w - gl_FragCoord.y);
    ivec2 q = ivec2(floor((p - uMap.xy) / uMap.z));
    q = clamp(q, ivec2(0), textureSize(uTex, 0) - ivec2(1));
    frag = vec4(texelFetch(uTex, q, 0).rgb, 1.0);
}
)";

const Variant& VAR(int v) { return VARIANTS[std::clamp(v, 0, VARIANT_COUNT - 1)]; }

// Level names after the universe's name in the HUD corner.
const char* LEVEL_WORDS[3] = {"DUSK", "BREEZE", "STORM"};

float lumaOf(const Palette& p, int i) { return luminance(p.linear(i)); }

} // namespace

// ================================================================== setup

PixelUniverse::~PixelUniverse() {
    if (prog_) glDeleteProgram(prog_);
    if (tex_) glDeleteTextures(1, &tex_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool PixelUniverse::init(int width, int height) {
    prog_ = ugl::program(ugl::FULLSCREEN_VS, UPSCALE_FS, "pixel upscale");
    glGenVertexArrays(1, &vao_);
    pal_ = makePalette(VARIANTS[0]);
    world_.setVariant(0, 1);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

void PixelUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    // The native grid: a board cell is CELL canvas pixels, the well's top-left corner on a canvas pixel corner.
    const layout::Rect r = layout::boardRect(width_, height_);
    s_ = std::max(0.25f, (r.y1 - r.y0) / (layout::BOARD_H * CELL));
    bx0_ = (int)std::ceil(r.x0 / s_);
    by0_ = (int)std::ceil(r.y0 / s_);
    ox_ = r.x0 - bx0_ * s_;
    oy_ = r.y0 - by0_ * s_;
    W_ = (int)std::ceil((width_ - ox_) / s_) + 1;
    H_ = (int)std::ceil((height_ - oy_) / s_) + 1;
    hz_ = by0_ + 108;
    cv_.resize(W_, H_);
    prevCv_.resize(W_, H_);
    rgba_.assign((size_t)W_ * H_ * 4, 255);
    if (tex_) glDeleteTextures(1, &tex_);
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W_, H_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    texW_ = W_;
    texH_ = H_;
}

// ================================================================== song, level

int PixelUniverse::pickVariant(const SongLook& l) const {
    Rng r(l.seed ^ 0x919E1ull);
    if (!l.hasSong) return r.irange(0, VARIANT_COUNT - 1);
    float h = std::fmod(l.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    const float mood = (l.minor ? -0.45f : 0.35f) + (l.brightness - 0.5f) * 1.4f;
    float w[16];
    float sum = 0;
    for (int i = 0; i < VARIANT_COUNT; i++) {
        const Variant& V = VARIANTS[i];
        float d = std::fabs(h - V.hue);
        d = std::min(d, 1.f - d);
        float wi = std::exp(-(d / 0.13f) * (d / 0.13f)) + 0.12f;
        wi *= std::exp(-((mood - V.mood) / 0.95f) * ((mood - V.mood) / 0.95f));
        if (V.theme == TH_SNOW) wi *= 0.7f + 1.2f * smoothstepf(0.45f, 0.75f, l.airWeight);
        if (V.theme == TH_VOLCANO || V.theme == TH_CITY) wi *= 0.7f + 1.0f * smoothstepf(0.45f, 0.75f, l.bassWeight);
        if (V.theme == TH_CITY) wi *= 0.8f + 0.6f * smoothstepf(115.f, 135.f, l.bpm);
        w[i] = wi;
        sum += wi;
    }
    float u = r.uniform() * sum;
    for (int i = 0; i < VARIANT_COUNT; i++) {
        u -= w[i];
        if (u <= 0) return i;
    }
    return VARIANT_COUNT - 1;
}

void PixelUniverse::setSong(const SongLook& look, float seconds) {
    int v = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    if (look.variant < 0 && v == look.avoidVariant) v = (v + 1 + (int)(look.seed % (VARIANT_COUNT - 1))) % VARIANT_COUNT;
    seed_ = look.seed;
    rng_ = Rng(look.seed ^ 0x51CE1ull);
    if (seconds > 0.f && v != variant_) {
        // A new place wipes in from the well, in ordered-dither steps.
        prevVariant_ = variant_;
        prevPal_ = pal_;
        prevWorld_ = world_;
        wipeT_ = 0;
        wipeDur_ = std::clamp(seconds, 1.2f, 3.f);
    }
    variant_ = v;
    pal_ = makePalette(VARIANTS[v]);
    world_.setVariant(v, look.seed);
}

void PixelUniverse::setLevel(int level, float, float seconds, float impact) {
    level = std::clamp(level, 0, 2);
    const bool rising = level > level_;
    level_ = level;
    if (seconds < 0.1f) {
        lv_ = (float)level;
        midP_ = level >= 1 ? 1.f : 0.f;
        peakP_ = level >= 2 ? 1.f : 0.f;
        sway_ = level == 0 ? 0.f : level == 1 ? 4.f : 9.f;
        return;
    }
    levelRate_ = 3.f / std::max(0.6f, seconds);
    if (rising && impact > 0.3f) {
        impactIn_ = 0.45f; // the boundary: the host anticipates rising changes by about this
        impact_ = impact;
    }
}

void PixelUniverse::refresh(float strength) {
    world_.gust(strength);
}

void PixelUniverse::setBonus(bool on) { bonus_ = on; }

void PixelUniverse::levelUp() { levelUpT_ = 0; }

TransitionHints PixelUniverse::hints() const {
    const Variant& V = VAR(variant_);
    TransitionHints h;
    vec3 bg(0.f);
    for (int i = 1; i < 5; i++) bg += pal_.linear(V.sky[i]) * 0.25f;
    h.background = bg;
    h.light = luminance(bg) > 0.3f;
    h.ink = h.light ? pal_.linear(V.ink) : pal_.linear(V.text);
    h.accent = pal_.linear(V.accent);
    h.styles = TS_TILES | TS_REBUILD | TS_LUMA;
    return h;
}

// ================================================================== events

void PixelUniverse::onEvent(const GameEvent& ev, const Game& g) {
    const Variant& V = VAR(variant_);
    switch (ev.type) {
    case GameEvent::Lock: {
        if (ev.cells.size() != 4) break;
        lock_.active = true;
        lock_.age = 0;
        lock_.type = ev.cells[0].type;
        for (int i = 0; i < 4; i++) { lock_.cells[i][0] = ev.cells[i].x; lock_.cells[i][1] = ev.cells[i].y; }
        // dust from under the piece, where it rests on something
        for (auto& c : ev.cells) {
            bool below = false;
            for (auto& o : ev.cells) below |= o.x == c.x && o.y == c.y + 1;
            if (below || c.y < Game::HIDDEN) continue;
            const float px = (float)(bx0_ + c.x * CELL), py = (float)(by0_ + (c.y - Game::HIDDEN + 1) * CELL - 1);
            for (int k = 0; k < 2; k++) {
                const float dir = k ? 1.f : -1.f;
                px_.push_back({px + (k ? CELL - 1 : 0), py, dir * rng_.range(10.f, 24.f), -rng_.range(4.f, 14.f), 0,
                               rng_.range(0.22f, 0.4f), 30.f, V.textDim, PX_DUST});
            }
        }
        break;
    }
    case GameEvent::HardDrop: {
        if (ev.cells.size() != 4) break;
        Trail t;
        for (int i = 0; i < 4; i++) { t.cells[i][0] = ev.cells[i].x; t.cells[i][1] = ev.cells[i].y; }
        t.type = ev.cells[0].type;
        t.rows = ev.count;
        t.age = 0;
        trails_.push_back(t);
        int maxY = 0;
        for (auto& c : ev.cells) maxY = std::max(maxY, c.y);
        for (auto& c : ev.cells) {
            if (c.y != maxY) continue;
            const float py = (float)(by0_ + (c.y - Game::HIDDEN + 1) * CELL - 1);
            for (int k = 0; k < 3; k++)
                px_.push_back({(float)(bx0_ + c.x * CELL + rng_.irange(0, 7)), py, rng_.range(-30.f, 30.f),
                               -rng_.range(10.f, 30.f), 0, rng_.range(0.25f, 0.45f), 60.f, V.textDim, PX_DUST});
        }
        break;
    }
    case GameEvent::Clear: {
        lock_.active = false;
        for (auto& c : ev.cells) dying_.push_back({c.x, c.y, c.type, 0.f, false});
        if (ev.count >= 4 || forcedTetris_ >= 0) {
            forcedTetris_ = -1;
            // A screen-wide pixel firework.
            flash_ = 1.f;
            const int n = ev.backToBack ? 9 : 7;
            for (int i = 0; i < n; i++) {
                Rocket r;
                const float u = (i + 0.5f) / n;
                r.x = 8 + u * (W_ - 16) + rng_.range(-8.f, 8.f);
                r.y = (float)H_ + 2;
                r.vy = -rng_.range(230.f, 290.f);
                r.top = rng_.range(14.f, (float)hz_ - 40.f);
                r.delay = 0.05f + 0.09f * (float)((i * 3) % n) + rng_.range(0.f, 0.04f);
                r.col = V.piece[rng_.irange(0, 6)];
                r.col2 = ev.backToBack ? V.sun : V.piece[rng_.irange(0, 6)];
                r.big = i % 3 == 1;
                rockets_.push_back(r);
            }
        }
        (void)g;
        break;
    }
    case GameEvent::BonusReady: {
        const float cx = (float)lx(layout::GAUGE_CENTER.x), cy = (float)ly(layout::GAUGE_CENTER.y);
        burst(cx, cy, 16, 30.f, V.accent, bright(), false);
        break;
    }
    case GameEvent::TopOut: {
        // The stack crumbles pixel by pixel, from the top, into a sand pile.
        grains_.clear();
        crumbleT_ = 0;
        pileSink_ = 0;
        std::fill(std::begin(pile_), std::end(pile_), 0);
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (auto& c : ev.cells) {
            if (c.y < Game::HIDDEN) continue;
            const float start = 0.25f + (c.y - top) * 0.07f + rng_.range(0.f, 0.15f);
            const int px0 = bx0_ + c.x * CELL, py0 = by0_ + (c.y - Game::HIDDEN) * CELL;
            const Variant& VV = VAR(variant_);
            for (int j = 0; j < CELL; j++)
                for (int i = 0; i < CELL; i++) {
                    uint8_t col = VV.piece[c.type];
                    if (i == CELL - 1 || j == CELL - 1) col = VV.pieceDark[c.type];
                    else if (i == 0 || j == 0) col = VV.pieceLit[c.type];
                    Grain gr{(float)(px0 + i), (float)(py0 + j), 0, 0, start + rng_.range(0.f, 0.8f) + (CELL - j) * 0.02f, col,
                             false, false, rng_.chance(0.09f)};
                    grains_.push_back(gr);
                }
        }
        dying_.clear();
        lock_.active = false;
        break;
    }
    default: break;
    }
}

void PixelUniverse::burst(float x, float y, int n, float speed, uint8_t c1, uint8_t c2, bool big) {
    for (int i = 0; i < n; i++) {
        const float a = i * TAU / n + rng_.range(-0.08f, 0.08f);
        const float sp = speed * rng_.range(0.85f, 1.15f);
        px_.push_back({x, y, std::cos(a) * sp, std::sin(a) * sp, 0, rng_.range(0.9f, 1.4f), 26.f, c1, PX_STAR});
        if (big || (i & 1))
            px_.push_back({x, y, std::cos(a) * sp * 0.55f, std::sin(a) * sp * 0.55f, 0, rng_.range(0.7f, 1.1f), 22.f, c2, PX_STAR});
    }
    if (big)
        for (int i = 0; i < n / 2; i++) {
            const float a = rng_.range(0.f, TAU), sp = speed * rng_.range(0.2f, 0.9f);
            px_.push_back({x, y, std::cos(a) * sp, std::sin(a) * sp, 0, rng_.range(1.2f, 1.8f), 18.f, VAR(variant_).text, PX_SPARK});
        }
}

// ================================================================== simulation

void PixelUniverse::simulate(const Game& g, const MusicState& m, float dt, float wdt) {
    const Variant& V = VAR(variant_);
    // Scene level: presences ease toward the level (the near layer, critters, weather follow them).
    lv_ = approach(lv_, (float)level_, levelRate_, dt);
    midP_ = approach(midP_, level_ >= 1 ? 1.f : 0.f, levelRate_, dt);
    peakP_ = approach(peakP_, level_ >= 2 ? 1.f : 0.f, levelRate_ * (level_ >= 2 ? 1.f : 0.6f), dt);
    sway_ = approach(sway_, level_ == 0 ? 0.f : level_ == 1 ? 4.f : 9.f, 0.5f, dt);
    if (impactIn_ >= 0) {
        impactIn_ -= dt;
        if (impactIn_ < 0) flash_ = std::max(flash_, 0.55f * impact_);
    }
    flash_ = std::max(0.f, flash_ - dt * 5.f);
    kick_ = approach(kick_, 0.f, 4.f, dt);
    const float maxR = std::sqrt((float)(W_ * W_ + H_ * H_));
    nightR_ = bonus_ ? std::min(maxR, nightR_ + dt * maxR / 0.9f) : std::max(0.f, nightR_ - dt * maxR / 1.2f);
    if (wipeT_ >= 0) {
        wipeT_ += dt / wipeDur_;
        if (wipeT_ >= 1.f) { wipeT_ = -1; prevVariant_ = -1; }
    }
    gaugeShown_ = approach(gaugeShown_, g.bonusGauge(), 6.f, dt);
    readyT_ = g.bonusReady() ? readyT_ + dt : 0.f;
    if (levelUpT_ >= 0) { levelUpT_ += dt; if (levelUpT_ > 1.6f) levelUpT_ = -1; }
    (void)m;

    // Lock squash and hard-drop trails.
    if (lock_.active) {
        lock_.age += wdt;
        if (lock_.age > 0.2f) lock_.active = false;
    }
    for (size_t i = 0; i < trails_.size();) {
        trails_[i].age += wdt;
        if (trails_[i].age > 0.16f) { trails_.erase(trails_.begin() + i); continue; }
        i++;
    }
    // Cleared cells: two frames of flash, then they burst into their pixels.
    for (size_t i = 0; i < dying_.size();) {
        Dying& d = dying_[i];
        d.age += wdt;
        if (!d.burst && d.age >= 0.1f) {
            d.burst = true;
            const float cx = (float)(bx0_ + 40), px0 = (float)(bx0_ + d.x * CELL), py0 = (float)(by0_ + (d.y - Game::HIDDEN) * CELL);
            for (int j = 0; j < CELL; j++)
                for (int k = 0; k < CELL; k++) {
                    uint8_t col = V.piece[d.type];
                    if (k == CELL - 1 || j == CELL - 1) col = V.pieceDark[d.type];
                    else if (k == 0 || j == 0) col = V.pieceLit[d.type];
                    if (((k + j) & 1) && rng_.chance(0.35f)) continue;
                    const float x = px0 + k, y = py0 + j;
                    const float out = (x - cx) * rng_.range(1.2f, 2.6f);
                    px_.push_back({x, y, out + rng_.range(-22.f, 22.f), -rng_.range(25.f, 95.f), 0, rng_.range(0.5f, 1.1f), 170.f,
                                   col, PX_DEBRIS});
                }
        }
        if (d.burst && d.age > 0.12f) { dying_[i] = dying_.back(); dying_.pop_back(); continue; }
        i++;
    }
    // Firework rockets.
    for (size_t i = 0; i < rockets_.size();) {
        Rocket& r = rockets_[i];
        if (r.delay > 0) { r.delay -= wdt; i++; continue; }
        r.y += r.vy * wdt;
        if (rng_.chance(0.7f)) px_.push_back({r.x + rng_.range(-0.4f, 0.4f), r.y + 2, 0, 10, 0, 0.25f, 0, V.glow, PX_TRAIL});
        if (r.y <= r.top) {
            burst(r.x, r.y, r.big ? 48 : 34, r.big ? 72.f : 54.f, r.col, r.col2, r.big);
            rockets_[i] = rockets_.back();
            rockets_.pop_back();
            continue;
        }
        i++;
    }
    for (size_t i = 0; i < px_.size();) {
        Px& p = px_[i];
        p.age += wdt;
        if (p.age > p.life || p.y > H_ + 4 || p.x < -4 || p.x > W_ + 4) { px_[i] = px_.back(); px_.pop_back(); continue; }
        p.vy += p.grav * wdt;
        if (p.kind == PX_STAR || p.kind == PX_SPARK) { p.vx *= std::exp(-1.2f * wdt); p.vy *= std::exp(-0.6f * wdt); }
        p.x += p.vx * wdt;
        p.y += p.vy * wdt;
        i++;
    }
    // Game over: grains come loose, fall, a few settle in a pile that sinks away.
    if (crumbleT_ >= 0) {
        crumbleT_ += wdt;
        const int floorY = by0_ + 20 * CELL;
        bool any = false;
        for (Grain& gr : grains_) {
            if (gr.dead) continue;
            any = true;
            if (!gr.loose) {
                if (crumbleT_ >= gr.at) { gr.loose = true; gr.vx = rng_.range(-6.f, 6.f); gr.vy = rng_.range(-12.f, 2.f); }
                continue;
            }
            gr.vy += 150.f * wdt;
            gr.x += gr.vx * wdt;
            gr.y += gr.vy * wdt;
            const int col = std::clamp((int)gr.x - bx0_, 0, 10 * CELL - 1);
            if (gr.y >= floorY - pile_[col]) {
                gr.dead = true;
                if (gr.sand && pile_[col] < 26) pile_[col]++;
            }
        }
        if (!any && crumbleT_ > 2.f) {
            pileSink_ += wdt;
            if (pileSink_ > 0.06f) {
                pileSink_ = 0;
                bool left = false;
                for (int& p : pile_) if (p > 0) { p--; left = true; }
                if (!left) { crumbleT_ = -1; grains_.clear(); }
            }
        }
    }
}

// ================================================================== drawing: the well

void PixelUniverse::drawBlock(int px, int py, int type, int size, int flash, uint8_t kind, int variantFor) {
    const Variant& V = VAR(variantFor >= 0 ? variantFor : variant_);
    const uint8_t base = V.piece[type], lit = V.pieceLit[type], dark = V.pieceDark[type];
    const uint8_t shine = pal_.lighter[lit];
    for (int j = 0; j < size; j++)
        for (int i = 0; i < size; i++) {
            const int X = px + i, Y = py + j;
            if (Y < by0_ - 1 && kind == K_STACK) continue; // above the well: hidden rows
            uint8_t c = base, k = kind;
            const bool edgeDark = i == size - 1 || j == size - 1;
            if (edgeDark) { c = dark; if (kind == K_STACK) k = K_GAP; }
            else if (i == 0 || j == 0) c = lit;
            else if (size >= 8 && ((i == 1 && j == 1) || (i == 2 && j == 1) || (i == 1 && j == 2))) c = shine;
            else if (size >= 8 && ((i == size - 2 && j >= 3) || (j == size - 2 && i >= 3))) c = pal_.darker[base] == dark ? dark : base;
            if (flash == 2) c = edgeDark ? lit : shine;
            else if (flash == 1) c = edgeDark ? base : lit;
            cv_.set(X, Y, c, k);
        }
}

void PixelUniverse::drawWell(const Game& g) {
    const Variant& V = VAR(variant_);
    const int x0 = bx0_, y0 = by0_, x1 = bx0_ + 10 * CELL, y1 = by0_ + 20 * CELL; // exclusive
    // The pedestal under the well, down into the ground / water.
    for (int y = y1 + 3; y < H_; y++) {
        const int d = y - y1 - 3;
        const int a = x0 - 6 - d / 2, b = x1 + 5 + d / 2;
        for (int x = a; x <= b; x++) {
            uint8_t c = V.frame;
            if (x == a || ((d % 5) == 4)) c = V.frameDark;
            else if (((x - a + (d / 5) * 4) % 9) == 0) c = V.frameDark;
            else if ((d % 5) == 0) c = V.frameLit;
            if (x == b) c = V.ink;
            cv_.set(x, y, c, K_FRAME);
        }
    }
    // The inside: dark, with dots at the cell corners and a faint guide under the falling piece.
    int colMask = 0;
    if (g.hasPiece()) {
        int cells[4][2];
        g.pieceCells(g.piece(), cells);
        for (auto& c : cells) colMask |= 1 << std::clamp(c[0], 0, 9);
    }
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const int cx = (x - x0) / CELL;
            uint8_t c = V.well;
            const bool dot = ((x - x0) % CELL == 0) && ((y - y0) % CELL == 0) && x > x0 && y > y0;
            if (dot) c = V.wellDot;
            else if ((colMask >> cx & 1) && dith(0.09f, x, y)) c = V.wellDot;
            cv_.set(x, y, c, K_WELL);
        }
    // The frame: posts (lit outside, shadow inside), a floor, caps with a lantern each.
    for (int y = y0 - 2; y < y1 + 3; y++) {
        cv_.set(x0 - 3, y, V.frameLit, K_FRAME);
        cv_.set(x0 - 2, y, V.frame, K_FRAME);
        cv_.set(x0 - 1, y, V.frameDark, K_EDGE);
        cv_.set(x1, y, V.frameDark, K_EDGE);
        cv_.set(x1 + 1, y, V.frame, K_FRAME);
        cv_.set(x1 + 2, y, V.frameDark, K_FRAME);
        cv_.set(x1 + 3, y, V.ink, K_FRAME);
        cv_.set(x0 - 4, y, V.ink, K_FRAME);
    }
    for (int x = x0 - 4; x <= x1 + 3; x++) {
        cv_.set(x, y1, x < x0 || x >= x1 ? V.frame : V.frameLit, x < x0 || x >= x1 ? K_FRAME : K_EDGE);
        cv_.set(x, y1 + 1, V.frame, K_FRAME);
        cv_.set(x, y1 + 2, V.frameDark, K_FRAME);
    }
    for (int side = 0; side < 2; side++) {
        const int cx = side ? x1 + 1 : x0 - 2;
        cv_.rect(cx - 3, y0 - 5, 7, 3, V.frame, K_FRAME);
        cv_.hline(cx - 3, cx + 3, y0 - 5, V.frameLit, K_FRAME);
        cv_.hline(cx - 3, cx + 3, y0 - 3, V.frameDark, K_FRAME);
        cv_.set(cx - 4, y0 - 4, V.ink, K_FRAME);
        cv_.set(cx + 4, y0 - 4, V.ink, K_FRAME);
        // a lantern: steady in calm, flickering at mid, beating at peak
        const int fl = (int)(worldT_ * 8) + side;
        const bool bright = peakP_ > 0.5f ? (lastBeat_ & 1) == side : (fl % 7) != 0;
        cv_.rect(cx - 1, y0 - 9, 3, 4, V.ink, K_FRAME);
        cv_.set(cx, y0 - 8, bright ? V.sun : V.glow, K_FRAME);
        cv_.set(cx, y0 - 7, V.glow, K_FRAME);
        cv_.set(cx, y0 - 10, V.ink, K_FRAME);
        if (midP_ > 0.3f) cv_.glow(cx, y0 - 7, 1.5f, 5.f + (bright ? 1.f : 0.f), 0.45f * midP_, V.glow, K_WORLD, true, K_SKY);
    }
}

void PixelUniverse::drawStack(const Game& g) {
    const Variant& V = VAR(variant_);
    auto inLock = [&](int x, int y) {
        if (!lock_.active || lock_.age >= 2.f / 12.f) return false;
        for (auto& c : lock_.cells) if (c[0] == x && c[1] == y) return true;
        return false;
    };
    // Locked cells (rows that still have to fall after a clear are drawn higher).
    if (crumbleT_ < 0 || g.hasPiece()) {
        for (int y = Game::HIDDEN; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) {
                const Cell& c = g.cell(x, y);
                if (c.type < 0 || inLock(x, y)) continue;
                const float age = (1.f - c.flash) * Game::LOCK_FX_SECONDS;
                const int fl = c.flash > 0 ? (age < 0.05f ? 2 : age < 0.1f ? 1 : 0) : 0;
                drawBlock(bx0_ + x * CELL, by0_ + iround((y - Game::HIDDEN - g.rowOffset(y)) * CELL), c.type, CELL, fl, K_STACK);
            }
    }
    // The landed piece's two-frame squash: shorter and wider, anchored on its bottom.
    if (lock_.active && lock_.age < 2.f / 12.f) {
        const int sq = lock_.age < 1.f / 12.f ? 2 : 1;
        int minX = 99, maxX = -1, minY = 99, maxY = -1;
        for (auto& c : lock_.cells) {
            minX = std::min(minX, c[0]); maxX = std::max(maxX, c[0]);
            minY = std::min(minY, c[1]); maxY = std::max(maxY, c[1]);
        }
        const int w = (maxX - minX + 1) * CELL, h = (maxY - minY + 1) * CELL;
        Canvas tmp;
        tmp.resize(w, h);
        tmp.fill(0, 255);
        const int saveBx = bx0_, saveBy = by0_;
        // draw the piece into tmp
        std::swap(cv_, tmp);
        bx0_ = -minX * CELL;
        by0_ = -(minY - Game::HIDDEN) * CELL;
        for (auto& c : lock_.cells) drawBlock(bx0_ + c[0] * CELL, by0_ + (c[1] - Game::HIDDEN) * CELL, lock_.type, CELL, 2 - sq, K_STACK);
        std::swap(cv_, tmp);
        bx0_ = saveBx;
        by0_ = saveBy;
        const int dx0 = bx0_ + minX * CELL - sq, dy1 = by0_ + (maxY - Game::HIDDEN + 1) * CELL;
        const int dw = w + 2 * sq, dh = h - sq;
        for (int j = 0; j < dh; j++)
            for (int i = 0; i < dw; i++) {
                const int si = std::clamp(i * w / dw, 0, w - 1), sj = std::clamp(j * h / dh, 0, h - 1);
                const size_t k = (size_t)sj * w + si;
                if (tmp.cls[k] == 255) continue;
                cv_.set(dx0 + i, dy1 - dh + j, tmp.px[k], tmp.cls[k]);
            }
    }
    // Cleared cells: white, then light, then they burst (simulate()).
    for (const Dying& d : dying_) {
        if (d.burst) continue;
        drawBlock(bx0_ + d.x * CELL, by0_ + (d.y - Game::HIDDEN) * CELL, d.type, CELL, d.age < 0.05f ? 2 : 1, K_STACK);
    }
    // Game over grains and the sand pile.
    if (crumbleT_ >= 0) {
        for (const Grain& gr : grains_)
            if (!gr.dead) cv_.set(iround(gr.x), iround(gr.y), gr.col, gr.loose ? K_FX : K_STACK);
        const int floorY = by0_ + 20 * CELL;
        for (int x = 0; x < 10 * CELL; x++)
            for (int k = 0; k < pile_[x]; k++)
                cv_.set(bx0_ + x, floorY - 1 - k, k == pile_[x] - 1 ? V.textDim : (dith(0.5f, x, k) ? V.frame : V.frameDark), K_FX);
    }
    if (!g.hasPiece()) return;
    const Piece& p = g.piece();
    int cells[4][2];
    g.pieceCells(p, cells);
    auto has = [&](int cs[4][2], int x, int y) {
        for (int i = 0; i < 4; i++) if (cs[i][0] == x && cs[i][1] == y) return true;
        return false;
    };
    // Hard-drop trails: streaks above the landed piece for a few frames.
    for (const Trail& t : trails_) {
        int tc[4][2];
        std::memcpy(tc, t.cells, sizeof(tc));
        const float fade = 1.f - t.age / 0.16f;
        for (int i = 0; i < 4; i++) {
            if (has(tc, tc[i][0], tc[i][1] - 1)) continue;
            const int x = bx0_ + tc[i][0] * CELL, yTop = by0_ + (tc[i][1] - Game::HIDDEN) * CELL;
            const int len = std::min(t.rows, 6) * CELL;
            for (int k = 1; k <= len; k++) {
                const float cov = fade * (1.f - (float)k / len) * 0.8f;
                for (int ii = 1; ii < CELL - 1; ii++)
                    if (yTop - k >= by0_ && dith(cov, x + ii, yTop - k) && cv_.kind(x + ii, yTop - k) == K_WELL)
                        cv_.set(x + ii, yTop - k, ii == 1 || ii == CELL - 2 ? V.pieceLit[t.type] : V.wellDot, K_FX);
            }
        }
    }
    // Ghost: the piece's outline, dotted.
    Piece gp = p;
    gp.y = g.ghostY();
    if (gp.y > p.y) {
        int gc[4][2];
        g.pieceCells(gp, gc);
        const uint8_t col = V.pieceLit[p.type];
        for (int i = 0; i < 4; i++) {
            if (gc[i][1] < Game::HIDDEN) continue;
            const int x = bx0_ + gc[i][0] * CELL, y = by0_ + (gc[i][1] - Game::HIDDEN) * CELL;
            for (int k = 0; k < CELL; k++) {
                const bool on = ((k + gc[i][0] + gc[i][1]) & 1) == 0;
                if (!on) continue;
                if (!has(gc, gc[i][0], gc[i][1] - 1)) cv_.set(x + k, y, col, K_FX);
                if (!has(gc, gc[i][0], gc[i][1] + 1)) cv_.set(x + k, y + CELL - 1, col, K_FX);
                if (!has(gc, gc[i][0] - 1, gc[i][1])) cv_.set(x, y + k, col, K_FX);
                if (!has(gc, gc[i][0] + 1, gc[i][1])) cv_.set(x + CELL - 1, y + k, col, K_FX);
            }
        }
    }
    // The falling piece, in whole pixels.
    const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
    for (int i = 0; i < 4; i++) {
        if (cells[i][1] < Game::HIDDEN - 1) continue;
        drawBlock(bx0_ + iround((cells[i][0] + dx) * CELL), by0_ + iround((cells[i][1] - Game::HIDDEN + dy) * CELL), p.type, CELL, 0, K_STACK);
    }
}

// ================================================================== HUD

namespace {
std::string fold(const std::string& in) {
    std::string s;
    for (size_t i = 0; i < in.size(); i++) {
        if ((unsigned char)in[i] == 0xC2 && i + 1 < in.size() && (unsigned char)in[i + 1] == 0xB7) { s += '\x01'; i++; continue; }
        if ((unsigned char)in[i] >= 0x80) continue;
        s += in[i];
    }
    return s;
}
} // namespace

void PixelUniverse::layoutHud(const std::vector<HudText>& hud, std::vector<HudItem>& out) const {
    const Variant& V = VAR(variant_);
    const float hs = std::max(1.f, height_ / 900.f);
    const bool lightText = lumaOf(pal_, V.text) > 0.35f;
    const uint8_t outlineCol = lightText ? V.ink : V.panel;
    const uint8_t bright = this->bright(); // big letters over the dark well: always the light one
    const int leftX0 = lx(-11.6f), leftX1 = lx(-5.6f), rightX0 = lx(5.6f), rightX1 = lx(11.6f);
    for (const HudText& h : hud) {
        if (h.alpha <= 0.02f || h.text.empty()) continue;
        HudItem it;
        it.text = fold(h.text);
        it.role = h.role;
        it.center = h.center;
        it.scale = 1;
        it.font = FONT_SMALL;
        TextStyle st;
        st.color = V.text;
        st.kind = K_HUD;
        const float k = h.scale / hs; // host size, resolution-free
        const float lxr = (h.x - ox_) / s_, lyr = (h.y - oy_) / s_;
        const bool inLeft = lxr >= leftX0 && lxr <= leftX1, inRight = lxr >= rightX0 && lxr <= rightX1;
        const bool onCard = (h.role == HUD_LABEL || h.role == HUD_VALUE) && (inLeft || inRight);
        switch (h.role) {
        case HUD_TITLE:
            it.font = FONT_BIG;
            if (lightText) st.shadow = V.ink;
            else st.outline = outlineCol;
            break;
        case HUD_INFO: st.color = V.accent; st.shadow = V.ink; break;
        case HUD_SECTION: st.color = V.text; st.outline = outlineCol; break;
        case HUD_UNIVERSE: st.color = V.text; st.outline = outlineCol; break;
        case HUD_LABEL: st.color = onCard ? V.textDim : V.text; if (!onCard) st.outline = outlineCol; if (h.accent) st.color = V.accent; break;
        case HUD_VALUE: it.font = k < 2.4f ? FONT_SMALL : FONT_BIG; st.color = V.text; break;
        case HUD_NOTICE: it.font = FONT_BIG; st.color = V.accent; st.outline = V.ink; break;
        case HUD_KEY: it.font = it.text.size() > 2 ? FONT_SMALL : FONT_BIG; st.color = V.accent; break;
        case HUD_TOAST:
            it.font = FONT_BIG;
            it.scale = 2;
            st.color = it.text.find("SLOW") != std::string::npos ? V.accent : bright;
            st.outline = V.ink;
            st.shadow = V.ink;
            st.wave = 1;
            st.wavePhase = std::floor(realT_ * 10.f) * 0.7f;
            break;
        case HUD_TOAST_SUB: it.font = FONT_BIG; st.color = V.accent; st.outline = V.ink; break;
        case HUD_DIALOG:
            it.font = k >= 2.5f ? FONT_BIG : FONT_SMALL;
            it.scale = k >= 4.f ? 2 : 1;
            st.color = bright;
            st.outline = V.ink;
            if (k >= 4.f) st.shadow = V.ink;
            break;
        case HUD_HELP:
        case HUD_HINT: st.color = V.text; st.outline = outlineCol; break;
        default: st.outline = outlineCol; break;
        }
        // Fades: full, then the dim color, then ordered dither.
        if (h.alpha < 0.5f && h.role != HUD_TOAST && h.role != HUD_DIALOG && h.role != HUD_TITLE && h.role != HUD_INFO &&
            h.role != HUD_SECTION && h.role != HUD_UNIVERSE) {
            if (h.alpha > 0.22f) { if (onCard) st.color = V.textDim; } // off the cards, the outline keeps it legible
            else st.alpha = h.alpha / 0.22f;
            if (!onCard && h.role != HUD_TITLE && h.role != HUD_INFO && st.outline < 0) st.outline = outlineCol;
        } else if (h.alpha < 0.7f && (h.role == HUD_TOAST || h.role == HUD_DIALOG || h.role == HUD_KEY)) {
            st.alpha = h.alpha / 0.7f;
        }
        st.scale = it.scale;
        it.st = st;
        it.w = textWidth(it.font, it.text, st.spacing) * it.scale;
        it.h = fontHeight(it.font) * it.scale;
        // Long titles: the small font, then truncated.
        if (h.role == HUD_TITLE) {
            const int maxW = W_ - iround(lxr) - 70;
            if (it.w > maxW) { it.font = FONT_SMALL; it.w = textWidth(it.font, it.text, 1); it.h = 5; it.st.shadow = V.ink; }
            while (it.w > maxW && it.text.size() > 4) {
                it.text = it.text.substr(0, it.text.size() - 4) + "..";
                it.w = textWidth(it.font, it.text, 1);
            }
        }
        if (h.role == HUD_UNIVERSE) {
            std::string word = LEVEL_WORDS[std::clamp(level_, 0, 2)];
            it.text += "  " + word;
            it.w = textWidth(it.font, it.text, 1);
        }
        int x = iround(lxr);
        if (h.center) x = iround(lxr - it.w * 0.5f);
        else if (h.x < 0) x = iround((width_ + h.x - ox_) / s_) - it.w;
        it.x = x;
        it.y = iround(lyr);
        if (h.role == HUD_KEY) it.y = ly(layout::GAUGE_CENTER.y) - it.h / 2;
        if (h.role == HUD_NOTICE && inRight) it.y = std::max(it.y, ly(layout::GAUGE_CENTER.y) + 20);
        out.push_back(it);
    }
    // The host spaces its lines for its own font, which is smaller than ours at high resolutions: the info line
    // goes under the title, the section name above the universe's line.
    auto overlapX = [](const HudItem& a, const HudItem& b) { return a.x < b.x + b.w && b.x < a.x + a.w; };
    for (const HudItem& t : out)
        for (HudItem& o : out) {
            if (t.role == HUD_TITLE && o.role == HUD_INFO && overlapX(t, o) && o.y < t.y + t.h + 2 && o.y >= t.y)
                o.y = t.y + t.h + 2;
            if (t.role == HUD_UNIVERSE && o.role == HUD_SECTION && overlapX(t, o) && o.y + o.h > t.y - 2 && o.y <= t.y)
                o.y = t.y - 2 - o.h;
        }
    // The score column: label / value pairs stacked on the card (the host's spacing is for its own font).
    std::vector<HudItem*> col;
    for (HudItem& it : out)
        if ((it.role == HUD_LABEL || it.role == HUD_VALUE) && it.x + it.w / 2 >= leftX0 && it.x + it.w / 2 <= leftX1 &&
            it.y > ly(3.f))
            col.push_back(&it);
    std::sort(col.begin(), col.end(), [](const HudItem* a, const HudItem* b) { return a->y < b->y; });
    int bottom = -1000;
    HudRole prev = HUD_PLAIN;
    for (HudItem* it : col) {
        const int gap = prev == HUD_LABEL ? 2 : 5;
        if (it->y < bottom + gap) it->y = bottom + gap;
        bottom = it->y + it->h;
        prev = it->role;
    }
}

void PixelUniverse::drawCard(int x0, int y0, int x1, int y1, int tabW) {
    const Variant& V = VAR(variant_);
    // shadow, border with cut corners, fill, an inner highlight line
    for (int y = y0 + 1; y <= y1 + 1; y++) cv_.set(x1 + 1, y, V.ink, K_PANEL);
    cv_.hline(x0 + 1, x1 + 1, y1 + 1, V.ink, K_PANEL);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            const bool corner = (x == x0 || x == x1) && (y == y0 || y == y1);
            if (corner) continue;
            const bool edge = x == x0 || x == x1 || y == y0 || y == y1;
            uint8_t c = edge ? V.panelEdge : V.panel;
            if (!edge && y == y0 + 1) c = pal_.lighter[V.panel] != V.panel && lumaOf(pal_, V.panel) < 0.3f ? V.panel : V.panel;
            cv_.set(x, y, c, K_PANEL);
        }
    cv_.hline(x0 + 1, x1 - 1, y0 + 1, lumaOf(pal_, V.panel) < 0.3f ? V.panelEdge : pal_.darker[V.panel], K_PANEL);
    if (tabW > 0) { // the label's tab across the top border
        const int cx = (x0 + x1) / 2, a = cx - tabW / 2 - 3, b = cx + tabW / 2 + 3;
        for (int y = y0 - 5; y <= y0 + 1; y++)
            for (int x = a; x <= b; x++) {
                const bool corner = (x == a || x == b) && (y == y0 - 5);
                if (corner) continue;
                const bool edge = x == a || x == b || y == y0 - 5;
                cv_.set(x, y, edge ? V.panelEdge : V.panel, K_PANEL);
            }
    }
}

void PixelUniverse::drawSide(const Game& g, const std::vector<HudItem>& items, int hop) {
    const Variant& V = VAR(variant_);
    const int cxL = lx(-8.6f), cxR = lx(8.6f), hw = 19;
    auto find = [&](const char* text) -> const HudItem* {
        for (const HudItem& it : items) if (it.text == text) return &it;
        return nullptr;
    };
    // Hold card.
    {
        const HudItem* lab = find("HOLD");
        const int top = (lab ? lab->y : ly(9.4f)) + 2 - hop;
        drawCard(cxL - hw, top, cxL + hw, ly(layout::HOLD_CENTER.y) + 12 - hop, lab ? lab->w : 0);
    }
    // Score card: around the stacked column.
    {
        int top = 9999, bot = -9999;
        for (const HudItem& it : items)
            if ((it.role == HUD_LABEL || it.role == HUD_VALUE) && std::abs(it.x + it.w / 2 - cxL) < 22 && it.y > ly(3.f)) {
                top = std::min(top, it.y);
                bot = std::max(bot, it.y + it.h);
            }
        if (top < bot) drawCard(cxL - hw, top - 4 - hop, cxL + hw, bot + 4 - hop, 0);
    }
    // Next card.
    {
        const HudItem* lab = find("NEXT");
        const int top = (lab ? lab->y : ly(9.4f)) + 2 - hop;
        drawCard(cxR - hw, top, cxR + hw, ly(layout::nextCenter(2).y) + 10 - hop, lab ? lab->w : 0);
    }
    // Previews in mini blocks.
    auto preview = [&](int type, int cx, int cy, int size, bool used) {
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        g.pieceCells(p, cells);
        int minx = 9, maxx = -9, miny = 9, maxy = -9;
        for (auto& c : cells) {
            minx = std::min(minx, c[0]); maxx = std::max(maxx, c[0]);
            miny = std::min(miny, c[1]); maxy = std::max(maxy, c[1]);
        }
        const int w = (maxx - minx + 1) * size, h = (maxy - miny + 1) * size;
        const int x0 = cx - w / 2, y0 = cy - h / 2;
        for (auto& c : cells) {
            const int px = x0 + (c[0] - minx) * size, py = y0 + (c[1] - miny) * size;
            drawBlock(px, py, type, size, 0, K_HUD);
            if (used)
                for (int j = 0; j < size; j++)
                    for (int i = 0; i < size; i++)
                        if (dith(0.5f, px + i, py + j)) cv_.set(px + i, py + j, V.panel, K_HUD);
        }
    };
    if (g.holdType() >= 0) preview(g.holdType(), cxL, ly(layout::HOLD_CENTER.y) - hop, 5, g.holdUsed());
    for (int i = 0; i < 3; i++) preview(g.next(i), cxR, ly(layout::nextCenter(i).y) - hop, i == 0 ? 6 : 5, false);

    // The bonus gauge: a medallion with twelve segments, lit as lines are cleared.
    {
        const int gx = lx(layout::GAUGE_CENTER.x), gy = ly(layout::GAUGE_CENTER.y) - hop;
        const bool ready = g.bonusReady(), active = g.bonusActive();
        cv_.disc(gx + 1, gy + 1, 10.f, V.ink, K_PANEL);
        cv_.disc(gx, gy, 10.f, V.panelEdge, K_PANEL);
        cv_.disc(gx, gy, 9.f, V.panel, K_PANEL);
        const int lit = (int)std::floor(gaugeShown_ * 12.f + 0.02f);
        const bool blink = ready && ((int)(readyT_ * 4) & 1);
        for (int y = -9; y <= 9; y++)
            for (int x = -9; x <= 9; x++) {
                const float d = std::sqrt((float)(x * x + y * y));
                if (d < 5.2f || d > 8.2f) continue;
                float a = std::atan2((float)x, (float)-y); // 0 at the top, clockwise
                if (a < 0) a += TAU;
                const float seg = a / TAU * 12.f;
                const int si = (int)seg;
                if (seg - si < 0.12f || seg - si > 0.92f) continue; // gaps between segments
                uint8_t c = V.panelEdge;
                if (si < lit) c = (ready || active) ? (blink ? V.text : V.accent) : V.text;
                if (d > 7.4f) c = si < lit ? pal_.darker[c] : V.well;
                cv_.set(gx + x, gy + y, c, K_HUD);
            }
    }
}

void PixelUniverse::drawText(const std::vector<HudItem>& items, const std::vector<HudRect>& panels, int hop) {
    const Variant& V = VAR(variant_);
    for (const HudRect& r : panels) {
        const int x0 = iround((r.x - ox_) / s_), y0 = iround((r.y - oy_) / s_);
        const int x1 = iround((r.x + r.w - ox_) / s_), y1 = iround((r.y + r.h - oy_) / s_);
        const uint8_t c = r.accent ? V.accent : V.panel;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if (r.alpha >= 0.6f || dith(r.alpha / 0.6f, x, y)) cv_.set(x, y, c, K_PANEL);
    }
    const int cxL = lx(-8.6f), cxR = lx(8.6f);
    for (const HudItem& it : items) {
        int y = it.y;
        const int mid = it.x + it.w / 2;
        if (std::abs(mid - cxL) < 24 || std::abs(mid - cxR) < 24) y -= hop;
        if (it.role == HUD_NOTICE && peakP_ > 0.5f && lastBeat_ % 2 == 0) y -= 1;
        pixu::drawText(cv_, it.font, it.text, it.x, y, it.st);
        if (it.role == HUD_UNIVERSE) { // the level as three pips
            for (int i = 0; i < 3; i++) {
                const int px = it.x + it.w + 4 + i * 4;
                const uint8_t c = i <= level_ ? V.accent : V.textDim;
                cv_.rect(px - 1, y - 1, 5, 5, it.st.outline >= 0 ? (uint8_t)it.st.outline : V.ink, K_HUD);
                cv_.rect(px, y, 3, 3, c, K_HUD);
            }
        }
    }
}

// ================================================================== effects

uint8_t PixelUniverse::bright() const {
    const Variant& V = VAR(variant_);
    return lumaOf(pal_, V.text) > 0.35f ? V.text : V.panel;
}

void PixelUniverse::drawFx() {
    const Variant& V = VAR(variant_);
    for (const Px& p : px_) {
        const int x = iround(p.x), y = iround(p.y);
        const float u = p.age / p.life;
        switch (p.kind) {
        case PX_DEBRIS:
            if (u > 0.6f && !dith(1.f - (u - 0.6f) / 0.4f, x, y)) break;
            cv_.set(x, y, p.col, K_FX);
            break;
        case PX_DUST:
            cv_.set(x, y, u < 0.5f ? p.col : V.textDim, K_FX);
            break;
        case PX_TRAIL:
            if (dith(1.f - u, x, y)) cv_.set(x, y, p.col, K_FX);
            break;
        case PX_STAR: {
            // bright head, then the color, crackling (blinking) at the end
            if (u > 0.7f && ((int)(p.age * 20 + p.x) & 1)) break;
            const uint8_t c = u < 0.12f ? bright() : p.col;
            cv_.set(x, y, c, K_FX);
            if (u < 0.35f) cv_.set(x - iround(p.vx * 0.03f), y - iround(p.vy * 0.03f), pal_.darker[p.col], K_FX);
            break;
        }
        case PX_SPARK:
            if (((int)(p.age * 16) + x) % 3 == 0) {
                cv_.set(x, y, bright(), K_FX);
                if (u < 0.5f) { cv_.set(x + 1, y, V.sun, K_FX); cv_.set(x - 1, y, V.sun, K_FX); cv_.set(x, y + 1, V.sun, K_FX); cv_.set(x, y - 1, V.sun, K_FX); }
            }
            break;
        }
    }
    for (const Rocket& r : rockets_) {
        if (r.delay > 0) continue;
        const int x = iround(r.x), y = iround(r.y);
        cv_.set(x, y, bright(), K_FX);
        cv_.set(x, y + 1, V.sun, K_FX);
    }
    // Level up: sparkles climbing the well's posts.
    if (levelUpT_ >= 0) {
        const int y0 = by0_ - 2, y1 = by0_ + 20 * CELL;
        for (int i = 0; i < 10; i++) {
            const float u = levelUpT_ / 1.6f;
            const int y = y1 - iround(u * (y1 - y0) * (0.6f + 0.4f * hash01(i, 1, 9))) + i * 6 % 20;
            const int x = (i & 1) ? bx0_ - 2 : bx0_ + 10 * CELL + 1;
            if (((int)(levelUpT_ * 12) + i) % 3 == 0) continue;
            cv_.set(x, y, bright(), K_FX);
            cv_.set(x - 1, y, V.sun, K_FX);
            cv_.set(x + 1, y, V.sun, K_FX);
            cv_.set(x, y - 1, V.sun, K_FX);
            cv_.set(x, y + 1, V.sun, K_FX);
        }
    }
}

// ================================================================== compose

void PixelUniverse::compose(const Frame& f) {
    const Variant& V = VAR(variant_);
    const float b = saturate(bridge_.amount);
    uint8_t bc[3], bi[3];
    {
        const vec3 c = ugl::toSrgb(bridge_.color), k = ugl::toSrgb(bridge_.ink);
        bc[0] = (uint8_t)(saturate(c.x) * 255); bc[1] = (uint8_t)(saturate(c.y) * 255); bc[2] = (uint8_t)(saturate(c.z) * 255);
        bi[0] = (uint8_t)(saturate(k.x) * 255); bi[1] = (uint8_t)(saturate(k.y) * 255); bi[2] = (uint8_t)(saturate(k.z) * 255);
    }
    // The world pixelates toward the bridge: blocks of 1, 2, 4, 8 pixels.
    const int blk = b < 0.2f ? 1 : b < 0.5f ? 2 : 4;
    const float lightning = world_.lightning();
    const int gcx = bx0_ + 40, gcy = by0_ + 80;
    const float nightStep = std::floor(nightR_ / 6.f) * 6.f;
    const float dim = dimT_;
    const float fade = saturate(f.fade);
    for (int y = 0; y < H_; y++)
        for (int x = 0; x < W_; x++) {
            const size_t i = (size_t)y * W_ + x;
            uint8_t idx = cv_.px[i], cls = cv_.cls[i];
            uint8_t k = cls & 0x3F;
            const bool prev = (cls & K_PREV_PAL) != 0;
            const Palette& P = prev ? prevPal_ : pal_;
            const bool worldish = k == K_SKY || k == K_WORLD || k == K_WELL || k == K_FRAME || k == K_FX || k == K_PANEL;
            if (b > 0.f && worldish && blk > 1) {
                const int sx = x - x % blk, sy = y - y % blk;
                const size_t j = (size_t)sy * W_ + sx;
                const uint8_t sk = cv_.cls[j] & 0x3F;
                if (sk == k || (sk != K_STACK && sk != K_GAP && sk != K_HUD && sk != K_EDGE)) { idx = cv_.px[j]; }
            }
            if (flash_ > 0 && k != K_HUD && dith(flash_, x, y)) idx = P.lighter[idx];
            if (lightning > 0 && k == K_SKY && dith(lightning, x, y)) idx = P.lighter[P.lighter[idx]];
            if (dim > 0 && k != K_HUD && dith(dim, x, y)) idx = P.darker[idx];
            if (dim > 0.5f && k != K_HUD && k != K_STACK && k != K_GAP && dith((dim - 0.5f) * 2.f, x + 1, y)) idx = P.darker[idx];
            // night: a front sweeping from the well, in 6-pixel steps
            bool night = false;
            if (nightStep > 0 && k != K_HUD && k != K_PANEL) {
                const float d = std::sqrt((float)((x - gcx) * (x - gcx) + (y - gcy) * (y - gcy)));
                night = d < nightStep - 4 || (d < nightStep && dith((nightStep - d) / 4.f, x, y));
            }
            const uint8_t* rgb = night ? P.night[idx] : P.day[idx];
            uint8_t out[3] = {rgb[0], rgb[1], rgb[2]};
            if (night && k == K_SKY && hash01(x, y, 991) < 0.012f * (1.f - (float)y / std::max(1, hz_))) {
                const bool tw = (hash2(x, y, (uint32_t)(worldT_ * 3)) & 7) != 0;
                out[0] = tw ? 235 : 150; out[1] = tw ? 238 : 160; out[2] = 255;
            }
            // the bridge: the world dissolves (and pixelates) into its color, the stack and the HUD turn to ink
            if (b > 0.f) {
                const float t = b * 1.12f - 0.06f;
                if (worldish) {
                    if (dith(t, x / blk, y / blk)) { out[0] = bc[0]; out[1] = bc[1]; out[2] = bc[2]; }
                } else if (k == K_GAP) {
                    if (dith(t, x, y)) { out[0] = bc[0]; out[1] = bc[1]; out[2] = bc[2]; }
                } else if (dith(t, x, y)) {
                    out[0] = bi[0]; out[1] = bi[1]; out[2] = bi[2];
                }
            }
            if (fade < 1.f && !dith(fade, x, y)) out[0] = out[1] = out[2] = 0;
            uint8_t* o = &rgba_[i * 4];
            o[0] = out[0]; o[1] = out[1]; o[2] = out[2]; o[3] = 255;
        }
    (void)V;
}

// ================================================================== frame

void PixelUniverse::render(const Frame& f, GLuint fbo) {
    const Game& g = f.game;
    const MusicState& m = f.music;
    const float dt = std::min(f.dt, 0.1f);
    realT_ += dt;
    dimT_ = approach(dimT_, f.paused ? 0.75f : (f.dim ? 0.5f : 0.f), 4.f, dt);
    const float wdt = f.paused ? 0.f : dt * (bonus_ ? 0.4f : 1.f);
    worldT_ += wdt;
    const int beat = (int)std::floor(m.beat);
    lastBeat_ = beat;
    if (!f.paused) simulate(g, m, dt, wdt);

    // The world's state for this frame.
    WorldState ws;
    ws.W = W_; ws.H = H_;
    ws.bx0 = bx0_ - 4; ws.by0 = by0_; ws.bx1 = bx0_ + 10 * CELL + 4; ws.by1 = by0_ + 20 * CELL;
    ws.hz = hz_;
    ws.t = worldT_;
    ws.scroll = sway_ * std::sin(worldT_ * TAU / 26.f);
    ws.lv = lv_;
    const float still = 1.f - saturate(bridge_.amount); // the bridge calms the world
    ws.mid = midP_ * still;
    ws.peak = peakP_ * still;
    ws.energy = m.energy;
    ws.kick = std::max(m.kick, kick_);
    ws.beat = m.beat;
    ws.beatPhase = m.beatPhase;
    ws.bounce = (peakP_ > 0.5f && m.hasSong && m.beatPhase < 0.16f) ? (beat % 4 == 0 ? 2 : 1) : 0;
    ws.night = nightR_ > 1 ? 1.f : 0.f;
    ws.bass = m.audio.bass;
    ws.high = m.audio.high;
    if (!f.paused) {
        world_.update(ws, wdt);
        if (wipeT_ >= 0) prevWorld_.update(ws, wdt);
    }

    cv_.fill(0, K_SKY);
    world_.drawBack(cv_, ws);
    if (wipeT_ >= 0 && prevVariant_ >= 0) {
        // The previous place, outside the wipe's front.
        prevCv_.fill(0, K_SKY);
        prevWorld_.drawBack(prevCv_, ws);
        const float R = wipeT_ * std::sqrt((float)(W_ * W_ + H_ * H_));
        const float Rq = std::floor(R / 5.f) * 5.f;
        const int gcx = bx0_ + 40, gcy = by0_ + 80;
        for (int y = 0; y < H_; y++)
            for (int x = 0; x < W_; x++) {
                const float d = std::sqrt((float)((x - gcx) * (x - gcx) + (y - gcy) * (y - gcy)));
                const bool fresh = d < Rq - 6 || (d < Rq && dith((Rq - d) / 6.f, x, y));
                if (fresh) continue;
                const size_t i = (size_t)y * W_ + x;
                cv_.px[i] = prevCv_.px[i];
                cv_.cls[i] = (uint8_t)(prevCv_.cls[i] | K_PREV_PAL);
            }
    }
    drawWell(g);
    drawStack(g);
    const int hop = ws.bounce > 0 ? 1 : 0;
    std::vector<HudItem> items;
    layoutHud(f.hud, items);
    drawSide(g, items, hop);
    world_.drawFront(cv_, ws);
    drawFx();
    drawText(items, f.panels, hop);
    compose(f);

    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W_, H_, GL_RGBA, GL_UNSIGNED_BYTE, rgba_.data());
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(prog_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    ugl::set1i(prog_, "uTex", 0);
    ugl::set4f(prog_, "uMap", ox_, oy_, s_, (float)height_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ================================================================== registry

const UniverseEntry& universe_pixel() {
    static const UniverseEntry e{
        "PIXEL", "native low-resolution pixel art, sixteen colors, dithered skies, parallax places",
        [] { return std::unique_ptr<Universe>(new PixelUniverse()); },
        // Pixel art loves a beat: livelier songs suit it best; anything can play.
        [](const SongLook& l) { return 0.75f + 0.35f * smoothstepf(95.f, 135.f, l.bpm) + 0.2f * l.density; },
        [] {
            std::vector<std::string> v;
            for (int i = 0; i < VARIANT_COUNT; i++) v.push_back(VARIANTS[i].name);
            return v;
        }()};
    return e;
}
