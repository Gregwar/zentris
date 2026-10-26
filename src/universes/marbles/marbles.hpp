#pragma once
// MARBLES: square glass tiles, each holding one glass marble; cleared rows pop and throw their marbles out into a
// landscape behind the well, where they pile up in the valleys over the song (see marbles.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class MarblesUniverse : public Universe {
public:
    ~MarblesUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void drop(float strength) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override { slowOn_ = on; }
    void levelUp() override;
    void kick(float amount) override { kickIn_ = std::max(kickIn_, amount); }
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

    // A variant's colors, linear RGB once loaded (floats only: palettes are blended float by float).
    struct Palette {
        vec3 skyTop, skyLow, haze, ridge, land, landDeep, rim, panel, panelEdge, tile, ink, accent, wave, night, orb;
        vec3 piece[7];
        float tileTint, tileAlpha, panelAlpha, orbX, orbY, orbR, orbOn, stars, light, halo;
    };

private:
    // ---- Instances.
    struct MarbleInst { float a0[4], a1[4], a2[4], a3[4]; };
    struct FxInst { float a0[4], a1[4], a2[4]; };
    struct Vert { float x, y, r, g, b, a; };
    std::vector<MarbleInst> inst_;
    std::vector<FxInst> fx_;
    // kind: 0 a marble in the landscape, 1 a tile and its marble, 2 the ghost's outline, 3 a tile alone, 4 a pane
    void put(float x, float y, float half, float radius, const vec3& col, float alpha, float rot, float glow, float seed,
             int kind, float mx = 0.f, float my = 0.f, float flare = 0.f, float haze = 0.f);
    void putFx(float x, float y, float size, float rot, const vec3& col, float alpha, int kind);
    void flush();
    void flushFx();

    // ---- The stack's marbles: per board cell, its marble's look and how it jolts on an impact.
    struct Slot {
        float seed = 0.f, rot = 0.f;
        float bounce = 0.f, bounceT = 9.f; // amplitude, time since the impact
        float flare = 0.f;                 // a glint of the window on its glass (a hard drop)
        bool known = false;
        int type = -1;
    };
    Slot slots_[Game::H][Game::W];
    int nextId_ = 1;
    int lastDrop_ = 0;
    void syncSlots(const Game& game);
    float bounceOf(const Slot& s) const;

    // A tile popping (its marble already thrown out): it swells a little and is gone (real time).
    struct Pop { float x, y, t, delay; int type; };
    std::vector<Pop> pops_;

    // ---- The marbles in the world: thrown out of the well, then lying in the landscape.
    struct Marble {
        vec2 p, v;
        float r = 0.27f, rot = 0.f, seed = 0.f;
        int type = 0;
        float age = 0.f, delay = 0.f;
        float sleepT = 0.f;
        bool asleep = false;
        bool inWell = false;   // still crossing the well: no collisions, drawn in front
        float r0 = 0.27f;      // its size when it left its tile (it shrinks into the distance)
        float fade = 1.f;      // fading out (retired)
        bool retiring = false;
        float light = 0.f, prevLight = 0.f, twinkle = 0.f;
        float spin = 0.f;
    };
    std::vector<Marble> marbles_;
    void launch(vec2 from, int type, float seed, float rot, float delay, float vigor);
    void stepPhysics(float h);
    void simulateMarbles(float dt);
    bool pickTarget(float x0, float y0, float r, vec2& out);
    void seedMarbles(int n);
    void wakeAround(vec2 p, float rad);
    std::vector<int> gridHead_, gridNext_;
    int gridW_ = 1, gridH_ = 1;
    float gridX0_ = -24.f, gridY0_ = -14.f;
    static constexpr float GRID = 0.6f;
    std::vector<float> surf_;   // the top of the heaps per bin (for picking where a marble lands)

    // ---- The landscape: heights over x, from the song's seed.
    static constexpr int LAND_N = 1024;
    static constexpr float LAND_X = 26.f;
    std::vector<float> land_;
    GLuint landTex_ = 0;
    void buildLand();
    float landAt(float x) const;
    float slopeAt(float x) const;

    // HUD text in the landscape: marbles never rest there (board space).
    struct KeepOut { float x0, x1, y0, y1; };
    std::vector<KeepOut> keepOut_;
    bool inKeepOut(vec2 p, float r) const;

    // ---- Light: waves through the heaps at a drop, glints.
    struct Wave {
        float t;      // seconds since it started
        int shape;    // 0 out from the well, 1 in from the edges, 2 a sweep across, 3 rising, 4 rings from a heap, 5 diagonal
        float sign;
        vec2 o;
        float speed, width, gain;
        int bands;
    };
    std::vector<Wave> waves_;
    void startWave(int shape, float gain, int bands);
    float waveCoord(const Wave& w, vec2 p) const;
    struct Glint { vec2 p; float t, life, size; vec3 col; };
    std::vector<Glint> glints_;
    void glint(vec2 p, float size, const vec3& col);
    double lastHalf_ = -1.0, lastBar_ = -1.0;
    int dropCount_ = 0, dropShape_ = 0, waveIndex_ = 0;

    void simulate(const Game& game, const MusicState& music, float dt);
    void drawWorld(float world);
    void drawBoard(const Game& game, float world);
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    int pickVariant(const SongLook& look) const;

    // ---- Text.
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    struct Font {
        bool ok = false;
        GLuint tex = 0;
        float px = 64.f, scaleTT = 1.f, capH = 40.f;
        Glyph g[96];
        void* info = nullptr; // stbtt_fontinfo
    };
    Font fontLight_, fontMedium_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const Font& f, const std::string& s, float size, float track) const;
    float textQuads(const Font& f, const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const;
    void drawText(const Font& f, const std::vector<float>& quads, const vec3& ink, float alpha, float bias);

    // ---- GL.
    GLuint progSky_ = 0, progMarble_ = 0, progFx_ = 0, progText_ = 0, progShape_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, marbleVao_ = 0, marbleVbo_ = 0, fxVao_ = 0, fxVbo_ = 0, textVao_ = 0, textVbo_ = 0, shapeVao_ = 0,
           shapeVbo_ = 0;
    ugl::Target sceneT_;
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;

    // ---- Song, variant, palette.
    int variant_ = 0;
    bool palSet_ = false;
    Palette pal_{};
    uint64_t seed_ = 0;
    float seedF_ = 0.f;
    bool needSeed_ = true;

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;
    float drive_ = 0.f, glow_ = 0.f, swell_ = 0.f;
    float dropPending_ = 0.f;
    bool sectionDrop_ = false, sectionKnown_ = false;

    // ---- Music.
    float time_ = 0.f;
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f;
    bool hasBeat_ = false;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    float overT_ = -1.f;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    Bridge bridge_;
    Rng rng_{4242};
};
