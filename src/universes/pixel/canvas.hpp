#pragma once
// PIXEL's drawing surface: an indexed-color canvas at the universe's native low resolution (one board cell is
// CELL x CELL pixels, about 340 x 190 on a 16:9 screen). Everything in the universe is drawn here on the CPU,
// pixel by pixel, in palette indices (0..15); the host's screen only gets it at the end, palette-mapped and
// scaled up with nearest filtering. Each pixel also carries its class (sky, world, well, stack, HUD...), so
// the composition can treat them apart (the night palette's stars, the bridge, dimming).
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace pixu {

constexpr int CELL = 8; // native pixels per board cell

// Pixel classes (low 6 bits of Canvas::cls); bit 7 selects the previous variant's palette (a variant wipe).
enum Cls : uint8_t {
    K_SKY = 0,     // the sky (stars at night)
    K_WORLD = 1,   // landscape, critters, weather
    K_WELL = 2,    // the well's inside
    K_FRAME = 3,   // the well's frame
    K_EDGE = 4,    // the frame's inner line (stays as ink at the bridge)
    K_STACK = 5,   // blocks: faces
    K_GAP = 6,     // blocks: their dark edge (a gap at the bridge)
    K_FX = 7,      // effects (explosions, dust, fireworks)
    K_PANEL = 8,   // HUD cards
    K_HUD = 9,     // HUD text, gauge
    K_PREV_PAL = 0x80,
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
// 1D value noise (smooth), 0..1.
inline float vnoise1(float x, uint32_t s) {
    const float i = std::floor(x), f = x - i;
    const float a = hash01((int)i, 7, s), b = hash01((int)i + 1, 7, s);
    const float u = f * f * (3 - 2 * f);
    return a + (b - a) * u;
}
inline float fbm1(float x, uint32_t s, int oct = 4) {
    float v = 0, a = 0.5f, n = 0;
    for (int o = 0; o < oct; o++) { v += a * vnoise1(x, s + o * 31u); n += a; x *= 2.07f; a *= 0.5f; }
    return v / n;
}
inline int iround(float v) { return (int)std::floor(v + 0.5f); }

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
    uint8_t kind(int x, int y) const { return in(x, y) ? (uint8_t)(cls[(size_t)y * w + x] & 0x3F) : 0; }
    void rect(int x, int y, int rw, int rh, uint8_t c, uint8_t k) {
        const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(w, x + rw), y1 = std::min(h, y + rh);
        for (int yy = y0; yy < y1; yy++)
            for (int xx = x0; xx < x1; xx++) {
                const size_t i = (size_t)yy * w + xx;
                px[i] = c;
                cls[i] = k;
            }
    }
    void hline(int x0, int x1, int y, uint8_t c, uint8_t k) { rect(std::min(x0, x1), y, std::abs(x1 - x0) + 1, 1, c, k); }
    void vline(int x, int y0, int y1, uint8_t c, uint8_t k) { rect(x, std::min(y0, y1), 1, std::abs(y1 - y0) + 1, c, k); }
    // Bresenham line.
    void line(int x0, int y0, int x1, int y1, uint8_t c, uint8_t k) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            set(x0, y0, c, k);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    // A filled pixel disc of radius r (pixel centers within r + 0.5 of the center pixel).
    void disc(int cx, int cy, float r, uint8_t c, uint8_t k) {
        const int R = (int)std::ceil(r);
        const float rr = (r + 0.35f) * (r + 0.35f);
        for (int y = -R; y <= R; y++)
            for (int x = -R; x <= R; x++)
                if (x * x + y * y <= rr) set(cx + x, cy + y, c, k);
    }
    // Ordered-dither disc: coverage falls from `inner` (full) to `outer` (none).
    void glow(int cx, int cy, float inner, float outer, float amount, uint8_t c, uint8_t k, bool onlyKind = false,
              uint8_t onKind = 0) {
        const int R = (int)std::ceil(outer);
        for (int y = -R; y <= R; y++)
            for (int x = -R; x <= R; x++) {
                const float d = std::sqrt((float)(x * x + y * y));
                if (d > outer) continue;
                float t = d <= inner ? 1.f : 1.f - (d - inner) / std::max(0.01f, outer - inner);
                t *= amount;
                const int X = cx + x, Y = cy + y;
                if (!in(X, Y) || (onlyKind && kind(X, Y) != onKind)) continue;
                if (dith(t, X, Y)) set(X, Y, c, k);
            }
    }
};

// ---- Pixel fonts (font.cpp).
enum Font { FONT_SMALL = 0, FONT_BIG = 1 }; // 3x5 capitals / 5x7 with lower case
int textWidth(Font f, const std::string& s, int spacing = 1);
int fontHeight(Font f); // cap height (FONT_BIG's descenders go 2 rows below)
// Draws text with its top-left at (x, y), each glyph pixel a scale x scale square. Returns the width drawn.
// `mask` (optional) decides per pixel whether to draw (dither fades): mask(x, y) -> bool.
struct TextStyle {
    uint8_t color = 15;
    int shadow = -1;   // palette index of a 1-pixel drop shadow (down), -1: none
    int outline = -1;  // palette index of a 1-pixel outline all around, -1: none
    int scale = 1;
    int spacing = 1;   // extra columns between glyphs (in font pixels)
    float alpha = 1.f; // < 1: ordered-dither fade
    uint8_t kind = K_HUD;
    int wave = 0;      // pixels of a per-letter wave (toasts)
    float wavePhase = 0;
};
int drawText(Canvas& cv, Font f, const std::string& s, int x, int y, const TextStyle& st);

} // namespace pixu
