#pragma once
// PIXEL's variants: one place per song, each a hand-designed 16-color palette and a landscape theme. Every
// color of a scene is one of the sixteen (the night palette of the slow motion is a second set of sixteen,
// derived from the first: moonlight). Roles name which index plays which part.
#include <cstdint>

#include "mathutil.hpp"

namespace pixu {

enum Theme { TH_FOREST, TH_CANYON, TH_SNOW, TH_CITY, TH_COAST, TH_CHERRY, TH_VOLCANO, TH_JUNGLE };
enum Weather { W_RAIN, W_SAND, W_SNOW, W_PETALS, W_ASH, W_LEAVES };

struct Variant {
    const char* name;
    Theme theme;
    Weather weather;
    uint32_t pal[16];        // sRGB 0xRRGGBB
    uint8_t sky[5];          // zenith .. horizon
    uint8_t sun, glow;       // sun (or moon) disc, the light around it
    uint8_t cloud, cloudLit;
    uint8_t far, farLit, mid, midLit, nearC, nearLit, ground, groundLit;
    uint8_t ink, well, wellDot;
    uint8_t frame, frameLit, frameDark;
    uint8_t panel, panelEdge, text, textDim, accent;
    uint8_t piece[7], pieceLit[7], pieceDark[7]; // I O T S Z J L
    uint8_t wx1, wx2;        // weather colors
    float sunX;              // sun position: fraction of the width
    int sunUp;               // pixels above the horizon
    bool water;              // a lake / sea below the horizon
    float hue;               // circle-of-fifths hue it suits (0..1, see SongLook::hue)
    float mood;              // -1 night / minor .. +1 bright / major
};

extern const Variant VARIANTS[];
extern const int VARIANT_COUNT;

// A variant's palettes, ready to compose: day and night colors, and shade maps (index -> a darker / lighter
// index of the same palette, for reflections, dimming and flashes).
struct Palette {
    uint8_t day[16][3], night[16][3];
    uint8_t darker[16], lighter[16];
    uint8_t shade[16]; // a gentle darker step (cloud undersides): never more than a little darker
    vec3 linear(int i) const; // linear RGB of a day color
};
Palette makePalette(const Variant& v);

} // namespace pixu
