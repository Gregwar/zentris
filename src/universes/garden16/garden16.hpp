#pragma once
// GARDEN 16: GARDEN as a 16-bit indie game (see garden16.cpp).
#include <string>
#include <vector>

#include "universe.hpp"
#include "universes/garden16/canvas.hpp"

class Garden16Universe : public Universe {
public:
    ~Garden16Universe() override;
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
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

private:
    // ---- The stack's earth: per board cell, the piece it came from (its tiles share one outline) and how long its
    // top has been open to the air (the grass and the flower growing on it).
    struct Soil {
        int id = -1, type = -1;
        float age = 0.f;
        uint32_t seed = 0;
    };
    Soil soil_[Game::H][Game::W];
    int nextId_ = 1;
    int lastDrop_ = 0;
    void syncSoil(const Game& g, float gdt);
    int maskOf(int x, int y) const;

    // Loose pixels: dirt bits of a cleared tile (3x3 chunks), dust puffs, crumbs, petals, seeds, pollen, splashes.
    enum { P_CHUNK, P_PUFF, P_CRUMB, P_PETAL, P_SEED, P_POLLEN, P_SPLASH, P_DUST, P_LEAF };
    struct Part {
        float x, y, vx, vy, age, life, grav, drag, seed;
        uint8_t kind, col, col2;
        bool real;
        uint8_t px[9];
    };
    std::vector<Part> parts_;
    // Butterflies, birds, fireflies.
    enum { C_BUTTERFLY, C_BIRD, C_FIREFLY };
    struct Critter { float x, y, vx, vy, t, seed, life; uint8_t kind, col; bool leaving; };
    std::vector<Critter> critters_;
    float flockT_ = 0.f;
    struct Cloud { float x; int y, w; uint32_t seed; float speed; };
    std::vector<Cloud> clouds_;
    struct Drop { float x, y, v; };
    std::vector<Drop> rain_;
    float rainAmt_ = 0.f;
    // Worms crawling in the dirt (the ground and the landed tiles), pixel by pixel.
    struct Worm {
        float hx, hy, heading, speed, t, wig;
        int tx[12], ty[12];
        int n, len;
        float vis;
        int state; // 0 alive, 1 leaving, 2 dying
    };
    std::vector<Worm> worms_;
    float wormSpawnT_ = 0.f, wormRetireT_ = 0.f;
    float crumbAcc_ = 0.f;

    // Native grid: canvas pixel (0, 0) starts at window pixel (ox_, oy_), each canvas pixel is s_ window pixels.
    float s_ = 1, ox_ = 0, oy_ = 0;
    int W_ = 1, H_ = 1, bx0_ = 0, by0_ = 0, floor_ = 0;
    int cMin_ = 0, cMax_ = 0;
    std::vector<int> tops_;           // the ground's top pixel row per tile column (cMin_..cMax_)
    float raise_[2] = {0.f, 0.f};     // the bank beside the well follows each outer column (rows)
    int lx(float u) const { return bx0_ + g16::iround((u + 5.f) * g16::CELL); }
    int ly(float v) const { return by0_ + g16::iround((10.f - v) * g16::CELL); }
    int topOf(int c) const;
    int baseRows(int c) const;
    void computeTops();
    bool solidAt(int X, int Y) const;   // a crumb or a raindrop stops here
    bool earthAt(int X, int Y) const;   // a worm may crawl here
    int tileAt(int X, int Y, int* gx, int* gy) const;

    g16::Canvas cv_, hud_;
    std::vector<uint8_t> rgba_;
    uint8_t pat_[8][g16::CELL][g16::CELL];
    void buildPatterns();
    void buildPalette();
    float pal_[128][3];   // this frame's palette (sRGB 0..1)

    // ---- Drawing.
    struct HudItem { std::string text; g16::Font font; int x, y, w, h; g16::TextStyle st; HudRole role; };
    std::vector<HudItem> items_;
    struct KeepOut { int x0, x1, y0, y1; };
    std::vector<KeepOut> keepOut_;
    void layoutHud(const std::vector<HudText>& hud);
    void drawSky();
    void drawWellPanel();
    void drawGround();
    void drawGroundPlants();
    void drawAir(bool far);
    void drawStack(const Game& g);
    void drawPiece(const Game& g);
    void drawWorms();
    void drawFront();
    void drawSide(const Game& g);
    void drawText(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    void mergeLayer();
    void compose(const Frame& f);
    uint8_t tilePx(int type, int mask, uint32_t seed, int i, int j) const;
    void drawTile(int X0, int Y0, int type, int mask, uint32_t seed, uint8_t cls);
    void drawGrowth(int X0, int Y0, float age, uint32_t seed, bool openL, bool openR);
    void drawMini(g16::Canvas& cv, int X0, int Y0, int size, int type, int mask, uint8_t cls);
    void tuft(g16::Canvas& cv, int x, int yBase, int h, int sway, uint8_t cls, uint32_t seed);
    void flower(g16::Canvas& cv, int x, int yBase, int stem, int stage, int color, int sway, uint8_t cls);
    int swayAt(int X, float seed) const;
    int plantCap(int X, int yBase, int h) const;
    bool flowerSlot(int c, int k, int& X, int& Y, int& color, float& u, int& stem) const;

    // ---- Simulation.
    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
    void simulateWorms(float dt, float wdt);
    bool spawnWorm();
    void simulateCritters(float dt, float wdt);
    void simulateRain(float dt);
    void shedCrumbs(const Game& g, float dt);
    void chunks(int X0, int Y0, int type, int mask, uint32_t seed, float age, float delay, float power, bool real);
    void puff(int X, int Y, float dir);
    int pickVariant(const SongLook& look) const;

    // ---- GL: the canvas texture and the upscale.
    GLuint prog_ = 0, tex_ = 0, vao_ = 0;

    // ---- Song, variant.
    int variant_ = 0;
    uint64_t seed_ = 0;
    float seedF_ = 0.f;
    uint32_t seedU_ = 1;

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;
    // ---- The ground line's state.
    float dry_ = 0.6f, grassH_ = 0.6f, density_ = 0.7f, bloom_ = 0.f, drive_ = 0.f, glow_ = 0.f, swell_ = 0.f;
    float flurry_ = 0.f, petalAcc_ = 0.f, leafAcc_ = 0.f, gust_ = 0.f, lush_ = 0.f;
    float dropPending_ = 0.f;
    bool sectionDrop_ = false, sectionChorus_ = false, sectionKnown_ = false;

    // ---- Music.
    float worldT_ = 0.f, realT_ = 0.f;
    double lastBar_ = -1.0;
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f;
    double beat_ = 0.0;
    bool hasBeat_ = false;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f, overT_ = -1.f, gaugeShown_ = 0.f, dimT_ = 0.f, rush_ = 0.f;
    Bridge bridge_;
    Rng rng_{4242};
};
