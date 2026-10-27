#pragma once
// GARDEN's drawing surface: an indexed-color canvas at the universe's native low resolution (one board cell is
// CELL x CELL pixels, about 530 x 300 pixels on a 16:9 screen). Everything is drawn here on the CPU in palette
// indices; the screen only gets it at the end, palette-mapped (the palette itself animates, like a 16-bit console's
// CGRAM) and scaled up with nearest filtering. Each pixel also carries its class (sky, dirt, tile, HUD...), so the
// composition can treat them apart (the bridge, dimming) and the worms know where the earth is.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace g16 {

constexpr int CELL = 12; // native pixels per board cell

enum Cls : uint8_t {
    K_SKY = 0,   // the sky, the sun, the clouds, far birds
    K_WELL = 1,  // the well's pale panel
    K_DIRT = 2,  // the ground's dirt
    K_GRASS = 3, // the ground's grass cap
    K_PLANT = 4, // tufts and flowers on the ground
    K_AIR = 5,   // petals, butterflies, rain, seeds
    K_TILE = 6,  // landed tiles of earth (and their grass)
    K_PIECE = 7, // the falling piece, the previews
    K_GHOST = 8, // the ghost's dotted outline
    K_FX = 9,    // dirt bits, dust puffs, crumbs, splashes
    K_HUD = 10,  // text, gauge
};

// 4x4 ordered (Bayer) dither threshold, 0..1 exclusive.
inline float bayer(int x, int y) {
    static const uint8_t B[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    return (B[y & 3][x & 3] + 0.5f) / 16.f;
}
// True when coverage t (0..1) covers pixel (x, y) in the ordered pattern.
inline bool dith(float t, int x, int y) { return t > bayer(x, y); }

inline uint32_t hash2(int x, int y, uint32_t s = 0) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float hash01(int x, int y, uint32_t s = 0) { return (hash2(x, y, s) & 0xFFFFFF) / 16777216.f; }
inline int iround(float v) { return (int)std::floor(v + 0.5f); }
inline int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

struct Canvas {
    int w = 0, h = 0;
    std::vector<uint8_t> px, cls;
    void resize(int W, int H) {
        w = W; h = H;
        px.assign((size_t)w * h, 0);
        cls.assign((size_t)w * h, 0);
    }
    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    void fill(uint8_t c, uint8_t k) {
        std::fill(px.begin(), px.end(), c);
        std::fill(cls.begin(), cls.end(), k);
    }
    void set(int x, int y, uint8_t c, uint8_t k) {
        if (!in(x, y)) return;
        const size_t i = (size_t)y * w + x;
        px[i] = c;
        cls[i] = k;
    }
    uint8_t get(int x, int y) const { return in(x, y) ? px[(size_t)y * w + x] : 0; }
    uint8_t kind(int x, int y) const { return in(x, y) ? cls[(size_t)y * w + x] : 0; }
    void rect(int x, int y, int rw, int rh, uint8_t c, uint8_t k) {
        const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(w, x + rw), y1 = std::min(h, y + rh);
        for (int yy = y0; yy < y1; yy++)
            for (int xx = x0; xx < x1; xx++) {
                const size_t i = (size_t)yy * w + xx;
                px[i] = c;
                cls[i] = k;
            }
    }
    // A filled pixel disc of radius r.
    void disc(int cx, int cy, float r, uint8_t c, uint8_t k) {
        const int R = (int)std::ceil(r);
        const float rr = (r + 0.35f) * (r + 0.35f);
        for (int y = -R; y <= R; y++)
            for (int x = -R; x <= R; x++)
                if (x * x + y * y <= rr) set(cx + x, cy + y, c, k);
    }
};

// ---- Pixel fonts (font.cpp, PIXEL's two bitmap fonts).
enum Font { FONT_SMALL = 0, FONT_BIG = 1 }; // 3x5 capitals / 5x7 with lower case
int textWidth(Font f, const std::string& s, int spacing = 1);
int fontHeight(Font f);
struct TextStyle {
    uint8_t color = 1;
    int shadow = -1;
    int outline = -1;
    int scale = 1;
    int spacing = 1;
    float alpha = 1.f;
    uint8_t kind = K_HUD;
    int wave = 0;
    float wavePhase = 0;
};
int drawText(Canvas& cv, Font f, const std::string& s, int x, int y, const TextStyle& st);

} // namespace g16
