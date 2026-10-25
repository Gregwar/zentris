#pragma once
// SYNTHWAVE: a night drive, shot like a film (see synthwave.cpp).
#include <string>
#include <vector>

#include "universe.hpp"

class SynthwaveUniverse : public Universe {
public:
    ~SynthwaveUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override;
    void levelUp() override;
    void kick(float amount) override { kickIn_ = std::max(kickIn_, amount); }
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

    // The palette of a variant (linear RGB once loaded).
    struct Palette {
        vec3 skyTop, haze, sunCore, sunEdge, floor, grid, accent, flare, lamp;
        vec3 piece[7];
        float fog, rain, lamps, skyline, moon, stars;
    };

private:
    // ---- Instanced primitives.
    struct Tile { float a0[4], a1[4], a2[4], a3[4]; };   // see TILE_VS
    struct Line { float a0[4], a1[4], a2[4]; };          // see LINE_VS
    struct Vert { float x, y, r, g, b, a; };              // flat triangles
    std::vector<Tile> tiles_;
    std::vector<Line> lines_, fxLines_;
    std::vector<Vert> shapes_;
    void tile(float x, float y, float hw, float hh, const vec3& c, float alpha, int kind, float age = -1.f, float aux = 0.f,
              float e0 = 1.f, float e1 = 1.f, float e2 = 0.f, float e3 = 0.f);
    void line(std::vector<Line>& out, vec2 a, vec2 b, const vec3& c, float width, float glow, float alpha = 1.f);
    void glint(std::vector<Line>& out, vec2 c, float size, const vec3& col, float alpha = 1.f);
    void light(std::vector<Line>& out, vec2 c, float radius, const vec3& col, float alpha = 1.f);
    void tri(std::vector<Vert>& out, vec2 a, vec2 b, vec2 c, const vec3& col, float alpha);
    void quad(std::vector<Vert>& out, vec2 a, vec2 b, vec2 c, vec2 d, const vec3& col, float alpha);

    // ---- Scene parts.
    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
    void buildLamps(float alpha);
    void buildBoard(const Game& game);
    void buildFx();
    void drawWorld(const MusicState& music);
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    void drawOsd(const Game& game);
    int pickVariant(const SongLook& look) const;
    float horizon() const;
    vec2 sunPos() const;

    // ---- Text.
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    struct Font {
        bool ok = false;
        GLuint tex = 0;
        float px = 56.f, scaleTT = 1.f, ascent = 0.f, capH = 40.f;
        Glyph g[96];
        const unsigned char* data = nullptr;
        void* info = nullptr; // stbtt_fontinfo
    };
    Font font_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const std::string& s, float size, float track) const;
    // Appends the quads of a line of text (top-left at x, y; size: em pixels); returns its width.
    float textQuads(const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const;
    struct TextStyle {
        vec3 color{1.f}, glow{0.f};
        float glowAmt = 0.f, shadow = 0.6f, bias = 0.f, alpha = 1.f;
    };
    void drawText(const std::vector<float>& quads, const TextStyle& st);

    // ---- GL.
    GLuint progWorld_ = 0, progShape_ = 0, progTile_ = 0, progLine_ = 0, progText_ = 0, progDown_ = 0, progUp_ = 0,
           progStreak_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, tileVao_ = 0, tileVbo_ = 0, lineVao_ = 0, lineVbo_ = 0, shapeVao_ = 0, shapeVbo_ = 0,
           textVao_ = 0, textVbo_ = 0;
    GLuint msFbo_ = 0, msColor_ = 0, sceneFbo_ = 0, sceneTex_ = 0;
    int samples_ = 4;
    static constexpr int BLOOM_MIPS = 6;
    GLuint bloomFbo_[BLOOM_MIPS] = {}, bloomTex_[BLOOM_MIPS] = {};
    int bloomW_[BLOOM_MIPS] = {}, bloomH_[BLOOM_MIPS] = {};
    GLuint streakFbo_[2] = {}, streakTex_[2] = {};
    int streakW_ = 1, streakH_ = 1;
    void createTargets();
    void destroyTargets();
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f; // board units per pixel

    // ---- Song, variant, palette.
    int variant_ = 0;
    bool palSet_ = false;
    Palette pal_{};       // current (eased) palette, linear
    uint64_t seed_ = 0;
    float seedF_ = 0.f;
    float sunSide_ = 1.f, sunSideShown_ = 1.f;

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;

    // ---- Music-driven state.
    float worldTime_ = 0.f, scroll_ = 0.f, lastEased_ = -1.f, lastBeatPhase_ = 0.f, beats_ = 0.f;
    float kickAge_[4] = {99, 99, 99, 99}, kickAmp_[4] = {0, 0, 0, 0};
    int kickNext_ = 0;
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f, sinceGlint_ = 9.f;
    float cars_ = 0.f, hazeLit_ = 0.f, flareKick_ = 0.f;
    float flash_ = 0.f, sunFlare_ = 0.f, horizonFlash_ = 0.f;

    // ---- Effects.
    struct Flare { std::vector<vec2> seg; vec3 col; float t; };
    std::vector<Flare> flares_;
    struct Streak { float x, y0, y1, t; vec3 col; };
    std::vector<Streak> streaks_;
    struct RowGo { float y, t, dir; };
    std::vector<RowGo> rows_;
    struct Dying { float x, y, t, delay, dir; int type; bool out; };
    std::vector<Dying> dying_;
    struct Glint { vec2 p; float t, life, size; vec3 col; };
    std::vector<Glint> glints_;
    void glintBurst(int n, float spread);
    float tetrisT_ = 9.f;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f, slowT_ = 0.f;
    float overT_ = -1.f;
    float gaugeShown_ = 0.f, readyT_ = 0.f;
    float dimT_ = 0.f;
    Bridge bridge_;
    Rng rng_{777};
};
