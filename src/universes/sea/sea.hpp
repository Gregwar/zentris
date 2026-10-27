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
        vec3 glass[7];
        float sunX, sunY, sunR, moon, stars, panelA, clouds, lightPanel, night;
    };

private:
    struct CellInst { float a0[4], a1[4], a2[4], a3[4]; };
    struct SpriteInst { float a0[4], a1[4], a2[4], a3[4]; };
    struct Vert { float x, y, r, g, b, a; };
    std::vector<CellInst> cells_;
    std::vector<SpriteInst> sprites_;
    std::vector<SpriteInst> lifeInst_;
    // A glass cube of water (every cell is one): its center, size, glass color, rotation, how full (0..1), out in the
    // world (cut by the sand, tinted under the sea), crack (0 whole .. 1 about to burst).
    void cube(float x, float y, float size, const vec3& col, float alpha, float rot, float fill, bool ghost, bool world, float seed,
              float crack = 0.f);
    void sprite(float x, float y, float hx, float hy, float rot, int kind, const vec3& col, float alpha, float p0 = 0.f,
                float p1 = 0.f, float seed = 0.f);
    void flushCells();
    void flushSprites();

    // Cubes thrown out of a cleared row, up and outward: cracks spread over each, water spurting through them, then it
    // bursts in the air (or on the sand), its water raising the sea.
    struct Flyer { vec2 p, v; float rot, spin, t, crackAt, breakAt, water, spilt, spurtAcc, seed; int type; };
    std::vector<Flyer> flyers_;
    void burst(const Flyer& f);
    void simulateFlyers(float dt);
    int boardCount_ = 0; // the board's cubes last frame (a whole stack appearing at once: a review, a game joined)
    void watchBoard(const Game& game);

    // A breaking cube (game over: the stack breaks row by row).
    struct Dying { float x, y, delay, t; int type; bool broken; };
    std::vector<Dying> dying_;
    void breakCell(float x, float y, int type, float power, float water, vec2 carry = vec2{0.f, 0.f});

    // Particles: shards of glass, droplets, the sea's splashes and spray.
    enum { P_SHARD = 0, P_DROP = 1, P_POUR = 2, P_SPLASH = 3, P_SPRAY = 4, P_CLOUD = 5, P_BUBBLE = 6, P_STREAM = 7, P_RUN = 8, P_WET = 9,
           P_SAND = 10 };
    struct Particle {
        vec2 p, v;
        float age, life, size, rot, spin, grav, drag, seed;
        int kind;
        vec3 col;
        bool real;   // ages at the real speed, even in slow motion
        bool front;  // drawn over the stack
        float water = 0.f;  // lines of sea it carries (added when it reaches the sea)
        float y0 = 0.f;     // where it burst from
        float dir = 0.f;    // a stream's water running on the sand: which way
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

    // ---- Life in the sea, in layers: schools of small fish near the surface, jellyfish drifting in the middle
    // water, and in the dark depths an anglerfish and its lure, siphonophores. Only where there is water.
    enum { L_FISH = 0, L_JELLY = 1, L_ANGLER = 2, L_SIPHON = 3 };
    struct Critter {
        int kind, school;
        float x, u, du, dir, speed, size, phase, seed, alpha, dart, dartY, dyv;
        vec3 col, glow;
    };
    std::vector<Critter> critters_;
    int nextSchool_ = 0;
    float lifeT_ = 0.f, scatter_ = 0.f, glowUp_ = 0.f;
    float waterDepth() const;                // the near layer's base above the sand (board units)
    float critterY(const Critter& c) const;  // where a critter is (board y)
    float darkAt(float depth) const;         // how dark the water is this far under the surface (0..1)
    void spawnLife(bool onScreen);
    void simulateLife(float dt, float wdt, float world);
    void drawLife(float world);

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
    GLuint progSea_ = 0, progBoat_ = 0, progCell_ = 0, progSprite_ = 0, progLife_ = 0, progText_ = 0, progShape_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, cellVao_ = 0, cellVbo_ = 0, spriteVao_ = 0, spriteVbo_ = 0, lifeVao_ = 0, lifeVbo_ = 0, boatVao_ = 0, boatVbo_ = 0, textVao_ = 0, textVbo_ = 0,
           shapeVao_ = 0, shapeVbo_ = 0;
    ugl::Target sceneT_, bgT_; // the scene, and a copy of it behind the glass
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
