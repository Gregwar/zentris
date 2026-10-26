#pragma once
// POLAR NIGHT: a snowfield under the stars, aurora ribbons (see polar.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class PolarUniverse : public Universe {
public:
    ~PolarUniverse() override;
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

    // A night (linear RGB once loaded).
    struct Palette {
        vec3 zenith, horizon, glow, snow, shade, mount, moonCol, aurA, aurB, aurC, ink, halo, frost;
        vec3 piece[7];
        float glowAz, glowAmt, moon, moonAz, moonEl, moonSize, halo22, aurora, aurHeight, stars, milky, trees, haze, lightMix;
    };

private:
    struct Tile { float a0[4], a1[4], a2[4], a3[4]; };
    struct Dot { float a0[4], a1[4], a2[4]; };
    struct Vert { float x, y, r, g, b, a; };
    std::vector<Tile> tiles_;
    std::vector<Dot> backDots_, dots_;
    void tile(float x, float y, float hw, float hh, const vec3& c, float alpha, int kind, float age = -1.f,
              int mask = 0, float fog = 0.f, float e0 = 1.f, float e1 = 0.f, float e2 = 0.f, float e3 = 0.f);

    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
    void buildBoard(const Game& game);
    void buildFx();
    void drawSky();
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    int pickVariant(const SongLook& look) const;
    void powder(vec2 p, vec2 v, float size, float life, float alpha, float delay = 0.f, bool puff = false);
    void cellToPowder(float x, float y, float delay, float wind, float strength);

    // ---- Text.
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    struct Font {
        bool ok = false;
        GLuint tex = 0;
        float px = 64.f, scaleTT = 1.f, capH = 40.f;
        Glyph g[96];
        void* info = nullptr; // stbtt_fontinfo
    };
    Font fontLight_, fontRegular_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const Font& f, const std::string& s, float size, float track) const;
    float textQuads(const Font& f, const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const;
    void drawText(const Font& f, const std::vector<float>& quads, const vec3& color, float alpha, float halo, float bias);

    // ---- GL.
    GLuint progSky_ = 0, progComp_ = 0, progShape_ = 0, progTile_ = 0, progDot_ = 0, progText_ = 0, progDown_ = 0,
           progUp_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, tileVao_ = 0, tileVbo_ = 0, dotVao_ = 0, dotVbo_ = 0, shapeVao_ = 0, shapeVbo_ = 0,
           textVao_ = 0, textVbo_ = 0, noiseTex_ = 0;
    ugl::Target skyT_, sceneT_;
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
    Palette cur_{};   // this frame's (the variant, its frozen night in slow motion)
    uint64_t seed_ = 0;
    float seedF_ = 0.f;

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;

    // ---- The sky.
    static constexpr int RIBBONS = 6;
    float worldTime_ = 0.f, aurTime_ = 0.f, lastBeatPhase_ = 0.f, beats_ = 0.f;
    float pitch_ = 0.17f, beatGlow_ = 0.f;
    float band_[RIBBONS] = {}, ribbon_[RIBBONS] = {};
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f;
    float surge_ = 0.f, surgeX_ = 0.f, storm_ = 0.f, stormT_ = 9.f, flare_ = 0.f, pulse_ = 0.f;
    float wind_ = 0.3f, gust_ = 0.f, drift_ = 0.f;
    vec3 aurLight_{0.f};

    // ---- Snow and effects.
    struct Flake { float x, y, depth, phase, size; };
    std::vector<Flake> flakes_;
    float flakeTime_ = 0.f;
    struct Powder { vec2 p, v; float size, t, life, alpha, seed, delay; bool puff; };
    std::vector<Powder> powder_;
    // A cleared cell shattering (or, game over, a cell freezing white then crumbling): ox the blast's origin (board
    // x), power 1 (a Tetris more), seed shared by one clear's cells so its shards run across them.
    struct Dying { float x, y, t, delay; int type, mask; bool over, crumbled; float seed, ox = 0.f, power = 1.f; };
    std::vector<Dying> dying_;
    struct Glint { vec2 p; float t, life, size; };
    // The piece each locked cell came from (from the Lock events): a piece's cells join, two pieces never do. t0:
    // when it locked (iceClock_), cx cy: where it touched down (board grid), the frost spreading from there.
    struct IceCell { int id = -1, type = -1; float t0 = -99.f, cx = 0.f, cy = 0.f; };
    float iceClock_ = 0.f;
    IceCell ice_[Game::H][Game::W];
    int pieceCounter_ = 0;
    std::vector<Glint> glints_;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    float overT_ = -1.f, overDrift_ = 0.f, overFade_ = 0.f;
    float timeGlint_ = 0.f;
    bool worldPaused_ = false;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    Bridge bridge_;
    Rng rng_{808};
};
