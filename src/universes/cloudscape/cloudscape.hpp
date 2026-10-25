#pragma once
// CLOUDSCAPE: above an endless sea of clouds (see cloudscape.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class CloudscapeUniverse : public Universe {
public:
    ~CloudscapeUniverse() override;
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

    // A sky (linear RGB once loaded).
    struct Palette {
        vec3 zenith, horizon, glow, sun, haze, lit, shade, deep, frost, rim, ink, halo, aurA, aurB, flash;
        vec3 piece[7];
        float sunAz, sunEl, sunGain, sunSize, cov, veil, cirrus, towers, anvil, lightning, stars, aurora, fog, moon;
    };

private:
    struct Tile { float a0[4], a1[4], a2[4], a3[4]; };
    struct Mist { float a0[4], a1[4], a2[4]; };
    struct Line { float a0[4], a1[4], a2[4]; };
    struct Vert { float x, y, r, g, b, a; };
    std::vector<Tile> tiles_;
    std::vector<Mist> mists_;
    std::vector<Line> lines_, fxLines_;
    void tile(float x, float y, float hw, float hh, const vec3& c, float alpha, int kind, float age = -1.f,
              float angle = 0.f, float fog = 0.f, float e0 = 1.f, float e1 = 0.f, float e2 = 0.f, float e3 = 0.f);
    void line(std::vector<Line>& out, vec2 a, vec2 b, const vec3& c, float width, float glow, int kind, float alpha);

    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
    void buildBirds();
    void buildBoard(const Game& game);
    void buildFx();
    void drawClouds();
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    int pickVariant(const SongLook& look) const;
    void puff(vec2 p, vec2 v, float r, float grow, float life, float alpha, float delay = 0.f, float stretch = 1.35f);
    vec2 sunUv() const;   // where the light is on screen (uv, may be off screen)

    // ---- Text.
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    struct Font {
        bool ok = false;
        GLuint tex = 0;
        float px = 64.f, scaleTT = 1.f, capH = 40.f;
        Glyph g[96];
        void* info = nullptr; // stbtt_fontinfo
    };
    Font fontSerif_, fontSans_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const Font& f, const std::string& s, float size, float track) const;
    float textQuads(const Font& f, const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const;
    void drawText(const Font& f, const std::vector<float>& quads, const vec3& color, float alpha, float halo, float bias);

    // ---- GL.
    GLuint progCloud_ = 0, progRays_ = 0, progComp_ = 0, progShape_ = 0, progTile_ = 0, progMist_ = 0, progLine_ = 0,
           progText_ = 0, progDown_ = 0, progUp_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, tileVao_ = 0, tileVbo_ = 0, mistVao_ = 0, mistVbo_ = 0, lineVao_ = 0, lineVbo_ = 0,
           shapeVao_ = 0, shapeVbo_ = 0, textVao_ = 0, textVbo_ = 0, noiseTex_ = 0;
    ugl::Target cloudT_, raysT_, sceneT_;
    static constexpr int BLOOM_MIPS = 5;
    ugl::Target bloomT_[BLOOM_MIPS];
    void createTargets();
    void buildNoise();
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;

    // ---- Song, variant, palette.
    int variant_ = 0;
    bool palSet_ = false;
    Palette pal_{};   // eased toward the variant
    Palette cur_{};   // this frame's (the variant, its night in slow motion)
    uint64_t seed_ = 0;
    float seedF_ = 0.f;

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;

    // ---- The flight.
    float worldTime_ = 0.f, lastEased_ = -1.f, lastBeatPhase_ = 0.f, beats_ = 0.f;
    vec2 drift_{0.f, 0.f}, hiDrift_{0.f, 0.f};
    float eye_ = 3.2f, pitch_ = 0.07f, beatGlow_ = 0.f;
    float grow_[6] = {}, flash_[6] = {}, flashT_[6] = {};
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f, sinceFlash_ = 9.f;
    float rayPulse_ = 0.f, part_ = 0.f, partT_ = 9.f, flare_ = 0.f, flood_ = 0.f;
    struct Bird { float x, y, phase, size; };
    struct Flock { float x, y, dir, speed, t; std::vector<Bird> birds; };
    std::vector<Flock> flocks_;
    float flockTimer_ = 2.f, birds_ = 0.f;

    // ---- Effects.
    struct Puff { vec2 p, v; float r, grow, t, life, alpha, seed, delay, stretch; }; // stretch: width / height
    std::vector<Puff> puffs_;
    struct Dying { float x, y, t, delay; int type; bool fall; float vx, vy, rot, vr; float seed; };
    std::vector<Dying> dying_;
    struct Glint { vec2 p; float t, life, size; };
    std::vector<Glint> glints_;
    float tetrisT_ = 9.f;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    float overT_ = -1.f, overFog_ = 0.f;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    Bridge bridge_;
    Rng rng_{606};
};
