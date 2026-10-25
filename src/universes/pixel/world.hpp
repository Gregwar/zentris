#pragma once
// PIXEL's landscapes: the sky, the parallax ranges, water, critters and weather of each variant's place,
// drawn on the native canvas around the well (see world.cpp).
#include <vector>

#include "mathutil.hpp"
#include "universes/pixel/canvas.hpp"
#include "universes/pixel/variants.hpp"

namespace pixu {

// Everything the landscape reacts to, for one frame (canvas pixels, world seconds).
struct WorldState {
    int W = 0, H = 0;             // canvas
    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0; // the well (its cells), canvas pixels
    int hz = 0;                   // horizon row
    float t = 0;                  // world time (slower in slow motion, frozen when paused)
    float scroll = 0;             // parallax sway, pixels at depth 1
    float lv = 0;                 // eased scene level 0..2
    float mid = 0, peak = 0;      // presences of the mid and peak elements, 0..1
    float energy = 0.5f, kick = 0;
    double beat = 0;              // beats since the grid origin
    float beatPhase = 0;
    int bounce = 0;               // 0 / 1 / 2 pixels: the beat's hop (peak)
    float night = 0;              // slow motion
    float bass = 0, high = 0;     // smoothed bands
    int beatIndex() const { return (int)std::floor(beat); }
    int step(float fps) const { return (int)std::floor(t * fps); } // stepped animation frames
};

class World {
public:
    void setVariant(int v, uint64_t seed);
    void update(const WorldState& s, float wdt);
    void drawBack(Canvas& cv, const WorldState& s);  // sky to ground, critters: behind the well
    void drawFront(Canvas& cv, const WorldState& s); // foreground framing and weather (skips the board and HUD)
    float lightning() const { return flash_; }       // 0..1: a lightning flash lights the sky
    void gust(float strength);                        // a refresh: birds take off, a gust of weather
    int variant() const { return v_; }

private:
    struct Bird { float x, y, vx, vy; int kind; float phase; };
    struct Fly { float x, y, vx, vy, seed; };
    struct Drop { float x, y, vx, vy, life, age; int kind; float seed; };
    struct Bolt { std::vector<int> xs; int x0, y0, y1; float age; };
    struct Bomb { float x, y, vx, vy, age; };

    int v_ = 0;
    uint64_t seed_ = 1;
    Palette pal_{};
    Rng rng_{1};
    std::vector<Bird> birds_;
    std::vector<Fly> flies_;
    std::vector<Drop> drops_, splashes_;
    std::vector<Bomb> bombs_;
    Bolt bolt_{};
    bool boltOn_ = false;
    float flash_ = 0, boltCool_ = 2.f, birdCool_ = 0, fishCool_ = 3.f;
    float prevPhase_ = 0;
    int lastBeat_ = -1;
    float fishT_ = -1, fishX_ = 0;
    float gust_ = 0;
};

} // namespace pixu
