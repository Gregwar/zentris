// DEBUG: a plain, flat, light universe for developing the framework and judging transitions between a dark and a
// light universe. Flat colored cells with ink outlines on tinted paper, stb_easy_font HUD. Hidden from the random
// choice (--universe debug, or the viewer, reach it). It doesn't draw the bridge itself: the host does it.
#include <cstdio>
#include <vector>

#include "layout.hpp"
#include "stb_easy_font.h"
#include "ugl.hpp"
#include "universes/registry.hpp"

namespace {

const char* BG_FS = R"(#version 330 core
in vec2 vUv;
uniform vec3 uPaper, uLine;
uniform vec2 uRes;
uniform float uTime, uBeat, uGrid;
out vec4 frag;
float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
void main() {
    vec2 px = gl_FragCoord.xy;
    vec3 c = uPaper * (1.0 - 0.06 * length(vUv - 0.5));
    // A faint grid that thickens with the level and breathes on the beat.
    vec2 g = abs(fract(px / (uRes.y / 24.0)) - 0.5);
    float line = smoothstep(0.47, 0.5, max(g.x, g.y));
    c = mix(c, uLine, line * uGrid * (0.5 + 0.5 * uBeat));
    c *= 1.0 - 0.03 * (hash(floor(px)) - 0.5);
    frag = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
)";

const char* QUAD_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;   // pixels, top-left origin
layout(location = 1) in vec4 aCol;   // linear RGB, alpha
uniform vec2 uRes;
out vec4 vCol;
void main() {
    vCol = aCol;
    gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
}
)";
const char* QUAD_FS = R"(#version 330 core
in vec4 vCol;
uniform float uFade;
out vec4 frag;
void main() { frag = vec4(pow(max(vCol.rgb, 0.0), vec3(1.0 / 2.2)) * uFade, vCol.a); }
)";

struct Variant { const char* name; vec3 paper, line, ink, accent; vec3 pieces[7]; };
const Variant VARIANTS[3] = {
    {"PAPER", {0.86f, 0.82f, 0.74f}, {0.55f, 0.5f, 0.42f}, {0.05f, 0.045f, 0.04f}, {0.75f, 0.12f, 0.06f},
     {{0.1f, 0.45f, 0.6f}, {0.8f, 0.6f, 0.1f}, {0.45f, 0.2f, 0.55f}, {0.2f, 0.55f, 0.2f}, {0.75f, 0.15f, 0.1f},
      {0.12f, 0.2f, 0.6f}, {0.85f, 0.38f, 0.08f}}},
    {"MINT", {0.68f, 0.84f, 0.76f}, {0.3f, 0.5f, 0.42f}, {0.03f, 0.08f, 0.06f}, {0.1f, 0.35f, 0.6f},
     {{0.05f, 0.4f, 0.45f}, {0.75f, 0.65f, 0.15f}, {0.35f, 0.25f, 0.55f}, {0.15f, 0.5f, 0.2f}, {0.7f, 0.2f, 0.2f},
      {0.1f, 0.25f, 0.55f}, {0.8f, 0.45f, 0.15f}}},
    {"BLUSH", {0.9f, 0.74f, 0.74f}, {0.6f, 0.4f, 0.42f}, {0.1f, 0.03f, 0.05f}, {0.55f, 0.1f, 0.3f},
     {{0.2f, 0.4f, 0.6f}, {0.8f, 0.55f, 0.15f}, {0.5f, 0.2f, 0.5f}, {0.25f, 0.5f, 0.3f}, {0.7f, 0.1f, 0.2f},
      {0.2f, 0.2f, 0.55f}, {0.85f, 0.4f, 0.2f}}},
};

class DebugUniverse : public Universe {
public:
    ~DebugUniverse() override {
        glDeleteProgram(bg_);
        glDeleteProgram(quad_);
        glDeleteBuffers(1, &vbo_);
        glDeleteVertexArrays(1, &vao_);
    }
    bool init(int width, int height) override {
        bg_ = ugl::program(ugl::FULLSCREEN_VS, BG_FS, "debug paper");
        quad_ = ugl::program(QUAD_VS, QUAD_FS, "debug quads");
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
        resize(width, height);
        return glGetError() == GL_NO_ERROR;
    }
    void resize(int width, int height) override {
        width_ = std::max(1, width);
        height_ = std::max(1, height);
    }
    void setSong(const SongLook& look, float) override {
        var_ = look.variant >= 0 ? look.variant % 3 : (int)(splitmix64(look.seed) % 3);
        if (look.variant < 0 && var_ == look.avoidVariant) var_ = (var_ + 1) % 3;
    }
    void setLevel(int level, float, float seconds, float) override {
        level_ = level;
        if (seconds < 0.1f) lv_ = (float)level;
        rate_ = seconds < 0.1f ? 100.f : 3.f / seconds;
    }
    void onEvent(const GameEvent& ev, const Game&) override {
        if (ev.type == GameEvent::Clear)
            for (auto& c : ev.cells) flashes_.push_back({c.x, c.y, 0.f});
    }
    void setBonus(bool on) override { bonus_ = on; }
    TransitionHints hints() const override {
        const Variant& v = VARIANTS[var_];
        TransitionHints h;
        h.background = v.paper;
        h.ink = v.ink;
        h.accent = v.accent;
        h.light = true;
        h.styles = TS_INK | TS_CURTAIN | TS_LUMA;
        return h;
    }
    int variant() const override { return var_; }

    void render(const Frame& f, GLuint fbo) override {
        const Variant& V = VARIANTS[var_];
        lv_ = approach(lv_, (float)level_, rate_, f.dt);
        dimT_ = approach(dimT_, f.dim || f.paused ? 1.f : 0.f, 3.f, f.dt);
        slow_ = approach(slow_, bonus_ ? 1.f : 0.f, 3.f, f.dt);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width_, height_);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        // ---- Paper: warmer and gridded as the level rises; the bonus cools it.
        glUseProgram(bg_);
        vec3 paper = lerp(V.paper, V.paper * vec3(1.04f, 0.98f, 0.9f), saturate(lv_ - 1.f));
        paper = lerp(paper, paper * vec3(0.85f, 0.92f, 1.08f), slow_);
        paper = paper * (1.f - 0.45f * dimT_) * f.fade;
        ugl::set3f(bg_, "uPaper", paper);
        ugl::set3f(bg_, "uLine", V.line * (1.f - 0.45f * dimT_) * f.fade);
        ugl::set2f(bg_, "uRes", (float)width_, (float)height_);
        ugl::set1f(bg_, "uTime", (float)f.time);
        ugl::set1f(bg_, "uBeat", 1.f - f.music.beatPhase);
        ugl::set1f(bg_, "uGrid", 0.12f + 0.3f * lv_);
        ugl::drawFullscreen();

        // ---- Board, pieces, previews, gauge, HUD: flat quads.
        q_.clear();
        const Game& g = f.game;
        const float gain = 1.f - 0.4f * dimT_;
        const vec3 ink = V.ink;
        auto P = [&](const vec3& p) { return project(p); };
        auto cell = [&](const vec3& c, float half, const vec3& col, float a) {
            vec2 p0 = P(c + vec3(-half, half, 0)), p1 = P(c + vec3(half, -half, 0));
            rect(p0.x, p0.y, p1.x - p0.x, p1.y - p0.y, ink, a);
            const float b = std::max(1.f, (p1.x - p0.x) * 0.08f);
            rect(p0.x + b, p0.y + b, p1.x - p0.x - 2 * b, p1.y - p0.y - 2 * b, col * gain, a);
        };
        // The well.
        {
            vec2 a = P(vec3(-5.1f, 10.f, 0)), b = P(vec3(5.1f, -10.1f, 0));
            const float t = std::max(2.f, height_ / 400.f);
            rect(a.x - t, a.y, t, b.y - a.y, ink, 0.9f);
            rect(b.x, a.y, t, b.y - a.y, ink, 0.9f);
            rect(a.x - t, b.y, b.x - a.x + 2 * t, t, ink, 0.9f);
            rect(a.x, a.y, b.x - a.x, b.y - a.y, V.paper * 1.05f, 0.35f);
        }
        for (int y = Game::HIDDEN; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) {
                const Cell& c = g.cell(x, y);
                if (c.type < 0) continue;
                cell(layout::cellPos((float)x, y - g.rowOffset(y)), 0.47f, lerp(V.pieces[c.type], vec3(1.f), 0.6f * c.flash), 1.f);
            }
        for (auto& fl : flashes_) {
            fl.t += f.dt;
            const float k = 1.f - saturate(fl.t / 0.4f);
            cell(layout::cellPos((float)fl.x, (float)fl.y), 0.47f * (0.4f + 0.6f * k), V.accent, k);
        }
        flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(), [](const Flash& x) { return x.t > 0.4f; }), flashes_.end());
        if (g.hasPiece() && !g.over()) {
            int cs[4][2];
            g.pieceCells(g.piece(), cs);
            const int gy = g.ghostY() - g.piece().y;
            for (auto& c : cs) cell(layout::cellPos((float)c[0], (float)(c[1] + gy)), 0.47f, V.paper, 0.35f);
            const float dy = g.pieceVisualY() - g.piece().y;
            for (auto& c : cs)
                if (c[1] + dy > 1.6f) cell(layout::cellPos((float)c[0], c[1] + dy), 0.47f, V.pieces[g.piece().type], 1.f);
        }
        auto preview = [&](int type, vec3 center, float sc, float a) {
            Piece p{type, 0, 0, 0};
            int cs[4][2];
            g.pieceCells(p, cs);
            float mx = 0, my = 0;
            for (auto& c : cs) { mx += c[0] * 0.25f; my += c[1] * 0.25f; }
            for (auto& c : cs) cell(center + vec3((c[0] - mx) * sc, -(c[1] - my) * sc, 0), 0.47f * sc, V.pieces[type], a);
        };
        if (g.holdType() >= 0) preview(g.holdType(), layout::HOLD_CENTER, layout::previewScale(0), g.holdUsed() ? 0.4f : 1.f);
        for (int i = 0; i < 3; i++) preview(g.next(i), layout::nextCenter(i), layout::previewScale(i), i ? 0.7f : 1.f);
        // Gauge: a ring of dots.
        for (int j = 0; j < 24; j++) {
            const float a = PI * 0.5f - TAU * (j + 0.5f) / 24;
            const vec3 c = layout::GAUGE_CENTER + vec3(std::cos(a), std::sin(a), 0) * (layout::GAUGE_R * 0.85f);
            const bool lit = (j + 0.5f) / 24 <= g.bonusGauge();
            cell(c, 0.09f, lit ? V.accent : V.paper, lit ? 1.f : 0.5f);
        }
        for (const HudRect& r : f.panels)
            rect(r.x, r.y, r.w, r.h, r.custom ? ugl::toLinear(r.color) : (r.accent ? V.accent : V.paper * 0.8f), r.alpha);
        for (const HudText& h : f.hud) text(h, h.accent ? V.accent : ink);

        glUseProgram(quad_);
        ugl::set2f(quad_, "uRes", (float)width_, (float)height_);
        ugl::set1f(quad_, "uFade", f.fade);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, q_.size() * sizeof(float), q_.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q_.size() / 6));
        glDisable(GL_BLEND);
    }

private:
    void rect(float x, float y, float w, float h, const vec3& c, float a) {
        const float v[6][2] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y}, {x + w, y + h}, {x, y + h}};
        for (auto& p : v) q_.insert(q_.end(), {p[0], p[1], c.x, c.y, c.z, a});
    }
    void text(const HudText& h, const vec3& col) {
        if (h.alpha <= 0.01f || h.text.empty()) return;
        std::string s;
        for (size_t i = 0; i < h.text.size(); i++) { // "·" -> "-"
            if ((unsigned char)h.text[i] == 0xC2 && i + 1 < h.text.size() && (unsigned char)h.text[i + 1] == 0xB7) { s += '-'; i++; }
            else s += h.text[i];
        }
        static char buf[60000];
        stb_easy_font_spacing(0.5f);
        const int n = stb_easy_font_print(0, 0, (char*)s.c_str(), nullptr, buf, sizeof(buf));
        const float sc = h.scale, w = stb_easy_font_width((char*)s.c_str()) * sc;
        const float x0 = ugl::hudLeft(h, w, width_), y0 = h.y;
        const float* v = (const float*)buf; // 4 vertices per quad: x, y, z, color (16 bytes each)
        for (int k = 0; k < n; k++) {
            const float* q = v + k * 16;
            const float ax = x0 + q[0] * sc, ay = y0 + q[1] * sc, bx = x0 + q[8] * sc, by = y0 + q[9] * sc;
            rect(ax, ay, bx - ax, by - ay, col, std::min(1.f, h.alpha));
        }
    }

    struct Flash { int x, y; float t; };
    std::vector<Flash> flashes_;
    std::vector<float> q_;
    GLuint bg_ = 0, quad_ = 0, vao_ = 0, vbo_ = 0;
    int var_ = 0, level_ = 1;
    float lv_ = 1, rate_ = 1, dimT_ = 0, slow_ = 0;
    bool bonus_ = false;
};

} // namespace

const UniverseEntry& universe_debug() {
    static const UniverseEntry e{"DEBUG", "flat test universe: colored cells on paper",
                                 [] { return std::unique_ptr<Universe>(new DebugUniverse()); }, nullptr,
                                 {VARIANTS[0].name, VARIANTS[1].name, VARIANTS[2].name}, true};
    return e;
}
