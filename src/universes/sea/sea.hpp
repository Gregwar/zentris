#pragma once
// SEA: glass cubes of water over a sea that rises with the cleared lines (see sea.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class SeaUniverse : public Universe {
public:
    ~SeaUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void drop(float strength) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override { slowOn_ = on; }
    void levelUp() override { refresh(1.f); }
    void kick(float amount) override { kickIn_ = std::max(kickIn_, amount); }
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

    // A variant's colors, linear RGB once loaded (floats only: palettes blend).
    struct Palette {
        vec3 skyTop, skyLow, sun, cloud, stormSky, stormCloud, sea0, sea1, sea2, deep, foam, haze, stormSea, panel;
        vec3 hull, super_, sail, baccent, window, cont0, cont1, cont2, ink, accent, dusk;
        vec3 piece[7];
        float sunX, sunY, sunR, moon, stars, panelA, clouds, lightPanel, night;
    };

private:
    struct CellInst { float a0[4], a1[4], a2[4], a3[4]; };
    struct SpriteInst { float a0[4], a1[4], a2[4], a3[4]; };
    struct Vert { float x, y, r, g, b, a; };
    std::vector<CellInst> cells_;
    std::vector<SpriteInst> sprites_;
    void cell(float x, float y, float size, int mask, const vec3& col, float alpha, float level, float slope, bool ghost, float seed);
    void sprite(float x, float y, float hx, float hy, float rot, int kind, const vec3& col, float alpha, float p0 = 0.f,
                float p1 = 0.f, float seed = 0.f);
    void flushCells();
    void flushSprites();

    // ---- The stack's glass: per board cell, the vessel it belongs to (a piece's cells make one vessel, its water
    // at one level), how full it is, how its water sloshes after a landing.
    struct Glass {
        int id = -1, type = -1;
        float fill = 0.78f, seed = 0.f, slosh = 0.f;
    };
    Glass glass_[Game::H][Game::W];
    float water_[Game::H][Game::W] = {}; // the water's level above each cell's bottom (cells), from its vessel
    float slope_[Game::H][Game::W] = {};
    int nextId_ = 1;
    int lastDrop_ = 0;
    void syncGlass(const Game& game, float dt);
    void levelWater();
    int maskOf(int x, int y) const;
    // the level (board y) of a vessel's water: cells' bottoms, fill fraction
    static float waterLevel(const float* bottoms, int n, float fill);

    // The falling piece's water: its level above the piece's lowest cell (springing when it turns), its tilt.
    float fallH_ = 0.f, fallHV_ = 0.f, fallTilt_ = 0.f, fallTiltV_ = 0.f, prevVisX_ = 0.f;
    int fallType_ = -1, fallRot_ = -1;
    bool fallSnap_ = true;
    void simulatePiece(const Game& game, float dt);

    // A breaking cube (game over: the stack breaks row by row).
    struct Dying { float x, y, delay, t, lvl; int type; bool broken; };
    std::vector<Dying> dying_;
    void breakCell(float x, float y, int type, float power, bool streams);

    // Water pouring off a cleared row on both sides of the well.
    struct Pour { float y, t, rate; vec3 col; };
    std::vector<Pour> pours_;

    // Particles: shards of glass, droplets, the sea's splashes and spray.
    enum { P_SHARD = 0, P_DROP = 1, P_POUR = 2, P_SPLASH = 3, P_SPRAY = 4 };
    struct Particle {
        vec2 p, v;
        float age, life, size, rot, spin, grav, drag, seed;
        int kind;
        vec3 col;
        bool real;   // ages at the real speed, even in slow motion
        bool front;  // drawn over the stack
    };
    std::vector<Particle> parts_;
    void splash(float x, float y, float power, const vec3& col);
    void sprayBurst(float x, int n, float power);
    void drawParticles(bool front, float world);

    // ---- The sea: its level (rising with the cleared lines), its swell, its storm.
    float waterLines_ = 0.f;  // lines of water poured into the sea this song
    float rise_ = 0.f;        // 0..1 shown (approaches the target)
    float riseTarget() const;
    float seaLevel() const;   // the near layer's base (board y)
    float seaSpread() const;
    float layerTop(int k, float x) const; // same as WAVE_GLSL's layerTop
    void setWaves(GLuint prog) const;
    float amp_ = 0.06f, sharp_ = 0.2f, caps_ = 0.f, glint_ = 0.5f, rock_ = 0.03f;
    float waveT_ = 0.f, cloudT_ = 0.f;
    float dir_ = 1.f;
    float stormLin_ = 0.f, storm_ = 0.f;
    float flash_ = 0.f, flashX_ = 12.f;
    float sprayAcc_ = 0.f;
    float dropPending_ = 0.f;
    bool sectionDrop_ = false, sectionKnown_ = false;

    // ---- Boats: they sail slowly across the sea, bigger as it deepens.
    struct Boat { float x, dir, speed, scale, phase, seed, alpha; int type, layer; };
    std::vector<Boat> boats_;
    float boatT_ = 0.f;
    bool spawnBoat(bool onScreen);
    void simulate(const Game& game, const MusicState& music, float dt, float wdt);

    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    int pickVariant(const SongLook& look) const;
    float bgLuma(float bx, float by) const;

    // ---- Text.
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    struct Font {
        bool ok = false;
        GLuint tex = 0;
        float px = 64.f, scaleTT = 1.f, capH = 40.f;
        Glyph g[96];
        void* info = nullptr; // stbtt_fontinfo
    };
    Font fontItalic_, fontRoman_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const Font& f, const std::string& s, float size, float track) const;
    float textQuads(const Font& f, const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const;
    void drawText(const Font& f, const std::vector<float>& quads, const vec3& ink, float alpha, float bias);

    // ---- GL.
    GLuint progSea_ = 0, progBoat_ = 0, progCell_ = 0, progSprite_ = 0, progText_ = 0, progShape_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, cellVao_ = 0, cellVbo_ = 0, spriteVao_ = 0, spriteVbo_ = 0, boatVao_ = 0, boatVbo_ = 0, textVao_ = 0, textVbo_ = 0,
           shapeVao_ = 0, shapeVbo_ = 0;
    ugl::Target sceneT_;
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;

    // ---- Song, variant, palette.
    int variant_ = 0;
    bool palSet_ = false;
    Palette pal_{};
    uint64_t seed_ = 0;

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;

    // ---- Music.
    float time_ = 0.f, worldTime_ = 0.f;
    double lastBar_ = -1.0;
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f;
    float gust_ = 0.f;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    bool over_ = false;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    Bridge bridge_;
    Rng rng_{5150};
};
