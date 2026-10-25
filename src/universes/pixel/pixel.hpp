#pragma once
// PIXEL: native low-resolution pixel art (see pixel.cpp).
#include <string>
#include <vector>

#include "universe.hpp"
#include "universes/pixel/canvas.hpp"
#include "universes/pixel/variants.hpp"
#include "universes/pixel/world.hpp"

class PixelUniverse : public Universe {
public:
    ~PixelUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override;
    void levelUp() override;
    void kick(float amount) override { kick_ = std::max(kick_, amount); }
    void render(const Frame& f, GLuint fbo) override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }
    void forceFx(int slow, int tetris) override { (void)slow; forcedTetris_ = tetris; }

private:
    // A loose pixel: explosions, dust, sparks, firework stars.
    struct Px { float x, y, vx, vy, age, life, grav; uint8_t col, kind; };
    enum { PX_DEBRIS, PX_DUST, PX_SPARK, PX_STAR, PX_TRAIL };
    struct Rocket { float x, y, vy, top, delay; uint8_t col, col2; bool big; };
    struct Dying { int x, y, type; float age; bool burst; };
    struct Grain { float x, y, vx, vy, at; uint8_t col; bool loose, dead, sand; };
    struct LockFx { int cells[4][2]; int type; float age; bool active; };
    struct Trail { int cells[4][2]; int type, rows; float age; };
    struct HudItem { std::string text; pixu::Font font; int x, y, scale, w, h; pixu::TextStyle st; HudRole role; bool center; };

    int pickVariant(const SongLook& look) const;
    uint8_t bright() const; // the variant's light text color (its panel color when the text is dark)
    void layoutHud(const std::vector<HudText>& hud, std::vector<HudItem>& out) const;
    void drawWell(const Game& g);
    void drawStack(const Game& g);
    void drawBlock(int px, int py, int type, int size, int flash, uint8_t kind, int variantFor = -1);
    void drawCard(int x0, int y0, int x1, int y1, int tabW);
    void drawSide(const Game& g, const std::vector<HudItem>& items, int hop);
    void drawFx();
    void drawText(const std::vector<HudItem>& items, const std::vector<HudRect>& panels, int hop);
    void simulate(const Game& g, const MusicState& m, float dt, float wdt);
    void compose(const Frame& f);
    void burst(float x, float y, int n, float speed, uint8_t c1, uint8_t c2, bool big);
    void contactDust(const GameEvent& ev, int perSide, float strength);
    int lx(float u) const { return bx0_ + 40 + pixu::iround(u * pixu::CELL); }  // board units -> canvas
    int ly(float v) const { return by0_ + pixu::iround((10.f - v) * pixu::CELL); }

    // GL: the canvas texture and the upscale.
    GLuint prog_ = 0, tex_ = 0, vao_ = 0;
    int texW_ = 0, texH_ = 0;
    // Native grid: canvas pixel (0, 0) starts at window pixel (ox_, oy_), each canvas pixel is s_ window pixels.
    float s_ = 1, ox_ = 0, oy_ = 0;
    int W_ = 1, H_ = 1, bx0_ = 0, by0_ = 0, hz_ = 0;
    pixu::Canvas cv_, prevCv_;
    std::vector<uint8_t> rgba_;

    // Variant (and the previous one while a new song dissolves in).
    int variant_ = 0, prevVariant_ = -1;
    pixu::Palette pal_{}, prevPal_{};
    pixu::World world_, prevWorld_;
    float wipeT_ = -1, wipeDur_ = 2.f;
    uint64_t seed_ = 1;

    // Scene level.
    int level_ = 1;
    float lv_ = 1, midP_ = 1, peakP_ = 0, levelRate_ = 1.f, sway_ = 0;
    float impactIn_ = -1, impact_ = 0;
    // Slow motion: the night palette dissolves in (0..1).
    bool bonus_ = false;
    float nightK_ = 0;
    float flash_ = 0;      // a palette flash (lighter): Tetris, drops into a peak
    float kick_ = 0;
    float dimT_ = 0;
    float worldT_ = 0, realT_ = 0;
    int lastBeat_ = -1;
    float gaugeShown_ = 0, readyT_ = 0;
    int forcedTetris_ = -1;
    Bridge bridge_;

    // Effects.
    std::vector<Px> px_;
    std::vector<Rocket> rockets_;
    std::vector<Dying> dying_;
    std::vector<Grain> grains_;
    float crumbleT_ = -1;
    int pile_[10 * pixu::CELL] = {};
    float pileSink_ = 0;
    LockFx lock_{};
    std::vector<Trail> trails_;
    float levelUpT_ = -1;
    Rng rng_{4242};
};
