#pragma once
// GARDEN: clods of earth that grow grass and flowers, on a wide ground line that follows the song (see garden.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class GardenUniverse : public Universe {
public:
    ~GardenUniverse() override;
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

    // A variant's colors, linear RGB once loaded.
    struct Palette {
        vec3 skyTop, skyLow, sun, glow, ground, groundDeep, lip, grass, lush, dry, moss, ink, accent, center, dusk;
        vec3 flower[3];  // the stack's flowers
        vec3 bloom[4];   // the ground line's flowers at the drop
        vec3 piece[7];
        float sunX, sunY, sunR, moon, stars, veil;
    };

private:
    struct CellInst { float a0[4], a1[4], a2[4], a3[4], a4[4]; };
    struct SpriteInst { float a0[4], a1[4], a2[4], a3[4]; };
    struct Vert { float x, y, r, g, b, a; };
    std::vector<CellInst> cells_;
    std::vector<SpriteInst> sprites_;
    void cell(float x, float y, float size, int mask, const vec3& col, float alpha, vec2 nc, float crumble, bool ghost,
              float seed, float moss, const float* blend = nullptr);
    // a neighbour's soil packed in one float for the cell shader's blend (-1: none)
    float packSoil(const vec3& c) const;
    void sprite(float x, float y, float hx, float hy, float rot, int kind, const vec3& col, float alpha, float p0 = 0.f,
                float p1 = 0.f, float p2 = 0.f, float p3 = 0.f, float seed = 0.f);
    void flushCells();
    void flushSprites();

    // ---- The stack's earth: per board cell, the piece it came from (its cells merge into one clod), its grain
    // origin and how long its top has been open to the air (the grass and the flowers growing on it).
    struct Soil {
        int id = -1, type = -1;
        vec2 nc{0, 0};
        float age = 0.f;    // seconds of growth (0: bare)
        float cover = 0.f;  // 0..1: buried by a block landing on it (the growth folding back into the soil)
        int coverType = -1; // the earth that buried it
        float seed = 0.f;
    };
    Soil soil_[Game::H][Game::W];
    int nextId_ = 1;
    int lastDrop_ = 0;
    void syncSoil(const Game& game, float gdt);
    int maskOf(int x, int y) const;
    float growthAt(const Soil& s) const;

    // Cleared earth crumbling, the stack crumbling at game over.
    struct Dying { float x, y; int type, mask; vec2 nc; float t, delay, seed, age; };
    std::vector<Dying> dying_;

    // Particles: dust, petals, seeds, pollen, fireflies, falling flowers.
    enum { P_DUST = 0, P_PETAL = 1, P_SEED = 2, P_POLLEN = 3, P_FIREFLY = 4, P_FLOWER = 5 };
    struct Particle {
        vec2 p, v;
        float age, life, size, rot, spin, grav, drag, seed;
        int kind;
        vec3 col;
        bool real;   // ages at the real speed, even in slow motion (a line clear)
        bool front;  // drawn over the stack (dust of the stack itself)
    };
    std::vector<Particle> parts_;
    void emitDust(vec2 p, vec2 v, const vec3& col, float size, float life, float grav, bool real);

    // The ground line's plants, fixed per song (positions, sizes, colors), drawn from the song's state.
    struct Blade { float x, h1, h2, h3, h4; };
    struct GFlower { float x, stem, head, u, lean, seed; int color, petals; bool open; };
    std::vector<Blade> blades_;
    std::vector<GFlower> gflowers_;
    struct KeepOut { float x0, x1, y0, y1; };   // HUD text near the ground line (board space): plants stay below it
    std::vector<KeepOut> keepOut_;
    float plantCap(float x, float y0, float len) const;
    void buildGround();
    void drawGround(const MusicState& music, float world);
    void drawStackGrowth(const Game& game, float world);
    void drawBoard(const Game& game, float world);
    void drawParticles(bool front, float world);
    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    int pickVariant(const SongLook& look) const;
    float sway(float x, float seed) const;

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
    GLuint progSky_ = 0, progCell_ = 0, progSprite_ = 0, progText_ = 0, progShape_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, cellVao_ = 0, cellVbo_ = 0, spriteVao_ = 0, spriteVbo_ = 0, textVao_ = 0, textVbo_ = 0,
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
    float seedF_ = 0.f;

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;

    // ---- The ground line's state: dry 0..1, height of the grass, density, the bloom (flowers open) and the drop.
    float dry_ = 0.6f, grassH_ = 0.6f, density_ = 0.7f, bloom_ = 0.f, drive_ = 0.f, glow_ = 0.f, swell_ = 0.f;
    float flurry_ = 0.f, petalAcc_ = 0.f, gust_ = 0.f;
    float dropPending_ = 0.f;   // drop() was called: its hit fires as soon as the section is known to be a drop
    bool sectionDrop_ = false, sectionChorus_ = false, sectionKnown_ = false;

    // ---- Music.
    float worldTime_ = 0.f;
    double lastBar_ = -1.0;
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f;
    double beat_ = 0.0;
    bool hasBeat_ = false;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    float overT_ = -1.f;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    float rush_ = 0.f;   // a Tetris: the stack's flowers open at once
    Bridge bridge_;
    Rng rng_{4242};
};
