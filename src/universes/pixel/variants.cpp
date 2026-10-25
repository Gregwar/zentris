// PIXEL's variants: eight places, each with its own sixteen colors.
#include "universes/pixel/variants.hpp"

#include <cmath>

namespace pixu {

// Field order: name, theme, weather, palette, sky[5], sun, glow, cloud, cloudLit, far, farLit, mid, midLit,
// near, nearLit, ground, groundLit, ink, well, wellDot, frame, frameLit, frameDark, panel, panelEdge, text,
// textDim, accent, pieces (I O T S Z J L) base / lit / dark, weather colors, sunX, sunUp, water, hue, mood.
const Variant VARIANTS[] = {
    {"PINE DUSK", TH_FOREST, W_RAIN,
     {0x0e0b1a, 0x221a38, 0x3a2a56, 0x5c3c6e, 0x91507c, 0xd0697a, 0xf3a26a, 0xffdc8c,
      0xfff5e1, 0x1b4646, 0x3b8a5c, 0x93d46c, 0x3a6ec6, 0x6ccbe6, 0xe2465a, 0xa25ad4},
     {2, 3, 4, 5, 6}, 7, 6, 4, 5, 3, 4, 2, 3, 1, 9, 0, 1, 0, 0, 1, 3, 4, 1, 1, 3, 8, 4, 7,
     {13, 7, 15, 10, 14, 12, 6}, {8, 8, 8, 11, 5, 13, 7}, {12, 6, 3, 9, 3, 2, 5}, 4, 8, 0.17f, 20, true, 0.30f, -0.3f},
    {"RED CANYON", TH_CANYON, W_SAND,
     {0x1a0f14, 0x3a1c24, 0x6a2c2c, 0xa4442e, 0xdc7438, 0xf4b45c, 0xffe7a6, 0xfff8ec,
      0x6e4272, 0xb46a9c, 0x2c4f74, 0x46a2b8, 0xa2e2d4, 0x5a8a3e, 0xa8c86a, 0xd0323c},
     {10, 8, 9, 4, 5}, 6, 5, 9, 4, 8, 9, 3, 4, 2, 3, 4, 5, 0, 0, 1, 2, 3, 1, 1, 3, 7, 9, 5,
     {11, 5, 9, 13, 15, 10, 4}, {12, 6, 7, 14, 4, 11, 5}, {10, 4, 8, 1, 2, 1, 3}, 5, 6, 0.80f, 16, false, 0.08f, 0.1f},
    {"SNOW PEAKS", TH_SNOW, W_SNOW,
     {0x101828, 0x24344f, 0x3d5b80, 0x6a8fba, 0xa8c8e8, 0xeef5fc, 0xf4c4c8, 0xffd76a,
      0x1d463a, 0x3c7a52, 0x8cc46c, 0x3a5ed2, 0x5ed2e2, 0xe25a6a, 0xf29a48, 0x9668d6},
     {3, 4, 4, 6, 5}, 7, 6, 4, 5, 4, 5, 3, 5, 8, 9, 5, 5, 0, 1, 2, 2, 4, 0, 5, 3, 1, 3, 13,
     {12, 7, 15, 9, 13, 11, 14}, {5, 5, 5, 10, 6, 12, 7}, {11, 14, 2, 8, 1, 1, 13}, 5, 4, 0.78f, 24, false, 0.62f, 0.4f},
    {"NEON CITY", TH_CITY, W_RAIN,
     {0x07060f, 0x13102a, 0x231b46, 0x3a2a6c, 0x60408e, 0xff4fa2, 0xff9a58, 0xffe26a,
      0xf2eeff, 0x2c3e70, 0x1ed2c0, 0x8affd6, 0x3a7aff, 0x46d066, 0xb04aff, 0xff3a4c},
     {0, 1, 2, 3, 4}, 8, 3, 2, 3, 2, 3, 1, 9, 0, 2, 0, 2, 0, 0, 1, 2, 10, 0, 1, 3, 8, 4, 5,
     {10, 7, 14, 13, 15, 12, 6}, {11, 8, 5, 11, 5, 10, 7}, {9, 6, 3, 9, 1, 2, 15}, 4, 8, 0.74f, 34, true, 0.76f, -0.8f},
    {"SEA CLIFFS", TH_COAST, W_RAIN,
     {0x0c1420, 0x1a2a3c, 0x2e4a5c, 0x487a8c, 0x7ab8c4, 0xc0e2da, 0xffdcae, 0xffbe5c,
      0xfffaf0, 0xc46a48, 0xe85a5a, 0x2a58b0, 0x52c8e8, 0x58a05e, 0xa2d878, 0x8a5ac2},
     {4, 4, 5, 6, 7}, 8, 7, 5, 8, 3, 4, 2, 3, 1, 13, 2, 13, 0, 0, 1, 2, 4, 0, 1, 3, 8, 4, 7,
     {12, 7, 15, 13, 10, 11, 9}, {5, 8, 5, 14, 6, 12, 7}, {11, 9, 1, 2, 9, 1, 2}, 3, 8, 0.22f, 14, true, 0.55f, 0.5f},
    {"CHERRY HILLS", TH_CHERRY, W_PETALS,
     {0x1e1428, 0x3c2842, 0x6c4a6c, 0xb27c9c, 0xf2a2ba, 0xffd6e0, 0xfff6f0, 0xffe08a,
      0xa8daf2, 0x6aa8da, 0x3a68b2, 0x4aa872, 0x9ad882, 0x8a5ac8, 0xe8504a, 0xf2903e},
     {9, 8, 8, 5, 6}, 7, 6, 5, 6, 9, 8, 11, 12, 1, 2, 11, 12, 0, 1, 2, 14, 15, 1, 6, 3, 1, 2, 14,
     {9, 7, 13, 11, 14, 10, 15}, {8, 6, 3, 12, 4, 9, 7}, {10, 15, 1, 1, 1, 1, 14}, 4, 5, 0.70f, 30, true, 0.92f, 0.9f},
    {"EMBER PEAKS", TH_VOLCANO, W_ASH,
     {0x0e0808, 0x261214, 0x3e1c1e, 0x5e2a26, 0x8c3a2a, 0xd24a22, 0xff8c24, 0xffd444,
      0xfff2c6, 0x3a2e42, 0x5c4c5e, 0x4a7ad2, 0x6ad2e2, 0x50a252, 0x9a5ac8, 0xe8304a},
     {1, 9, 3, 4, 4}, 6, 5, 10, 4, 9, 10, 2, 4, 1, 3, 0, 2, 0, 0, 1, 2, 4, 0, 1, 3, 8, 10, 6,
     {12, 7, 14, 13, 15, 11, 6}, {8, 8, 8, 12, 6, 12, 7}, {11, 6, 9, 1, 2, 9, 5}, 10, 6, 0.24f, 12, false, 0.00f, -0.6f},
    {"JADE FALLS", TH_JUNGLE, W_LEAVES,
     {0x0a1410, 0x142a22, 0x224438, 0x3a6a50, 0x6c9c78, 0xb2d2b0, 0xeef2d8, 0xffd062,
      0xa4927a, 0x6a5a4a, 0x3a88d2, 0x6ae2d2, 0x86c442, 0xe8503e, 0xf2902e, 0xa262c2},
     {4, 5, 5, 6, 6}, 7, 6, 5, 6, 4, 5, 3, 4, 2, 3, 1, 2, 0, 0, 1, 9, 8, 0, 1, 3, 6, 4, 7,
     {11, 7, 15, 12, 13, 10, 14}, {6, 6, 6, 6, 6, 11, 7}, {10, 14, 1, 3, 9, 1, 13}, 12, 14, 0.30f, 26, true, 0.42f, 0.2f},
};
const int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

namespace {
vec3 hexToSrgb(uint32_t c) { return {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f}; }
float toLin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float toSrgbF(float c) { c = saturate(c); return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f; }
vec3 lin(uint32_t c) { vec3 s = hexToSrgb(c); return {toLin(s.x), toLin(s.y), toLin(s.z)}; }
// Linear RGB -> OKLab.
vec3 oklab(vec3 c) {
    float l = 0.4122214708f * c.x + 0.5363325363f * c.y + 0.0514459929f * c.z;
    float m = 0.2119034982f * c.x + 0.6806995451f * c.y + 0.1073969566f * c.z;
    float s = 0.0883024619f * c.x + 0.2817188376f * c.y + 0.6299787005f * c.z;
    l = std::cbrt(l); m = std::cbrt(m); s = std::cbrt(s);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s, 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}
float dist2(vec3 a, vec3 b) { vec3 d = a - b; return d.x * d.x * 2.f + (d.y * d.y + d.z * d.z) * 10.f; }
} // namespace

vec3 Palette::linear(int i) const {
    return {toLin(day[i][0] / 255.f), toLin(day[i][1] / 255.f), toLin(day[i][2] / 255.f)};
}

Palette makePalette(const Variant& v) {
    Palette p;
    vec3 lab[16];
    bool pieceIdx[16] = {};
    for (int i = 0; i < 7; i++) pieceIdx[v.piece[i]] = true;
    for (int i = 0; i < 16; i++) {
        const vec3 s = hexToSrgb(v.pal[i]);
        p.day[i][0] = (uint8_t)std::lround(s.x * 255);
        p.day[i][1] = (uint8_t)std::lround(s.y * 255);
        p.day[i][2] = (uint8_t)std::lround(s.z * 255);
        const vec3 c = lin(v.pal[i]);
        lab[i] = oklab(c);
        // Night: moonlight. Colors sink toward a cool blue, keeping a part of their hue; the brightest (the sun)
        // turns to a pale moon; the pieces keep more of their color so they stay apart.
        const float L = luminance(c);
        const float keep = pieceIdx[i] ? 0.55f : 0.22f;
        vec3 moon = vec3(0.30f, 0.42f, 0.85f) * (0.06f + 0.6f * std::pow(L, 1.15f)) * 0.55f;
        vec3 n = c * keep * (pieceIdx[i] ? 1.f : 0.6f) + moon * (1.f - keep * 0.6f);
        if (i == v.sun) n = lerp(n, vec3(0.78f, 0.84f, 1.0f), 0.85f); // the sun turns to a moon
        p.night[i][0] = (uint8_t)std::lround(toSrgbF(n.x) * 255);
        p.night[i][1] = (uint8_t)std::lround(toSrgbF(n.y) * 255);
        p.night[i][2] = (uint8_t)std::lround(toSrgbF(n.z) * 255);
    }
    for (int i = 0; i < 16; i++) {
        // Darker: the closest color to this one at ~65% lightness, among darker ones.
        const vec3 td{lab[i].x * 0.8f - 0.03f, lab[i].y * 0.9f, lab[i].z * 0.9f};
        const vec3 tl{std::min(1.f, lab[i].x * 1.18f + 0.08f), lab[i].y * 0.8f, lab[i].z * 0.8f};
        int bd = -1, bl = -1, bs = -1;
        float dd = 1e9f, dl = 1e9f, ds = 1e9f;
        for (int j = 0; j < 16; j++) {
            if (lab[j].x < lab[i].x - 0.04f) {
                float d = dist2(lab[j], td);
                if (d < dd) { dd = d; bd = j; }
                if (lab[j].x > lab[i].x - 0.22f && d < ds) { ds = d; bs = j; }
            }
            if (lab[j].x > lab[i].x + 0.04f) {
                float d = dist2(lab[j], tl);
                if (d < dl) { dl = d; bl = j; }
            }
        }
        p.darker[i] = (uint8_t)(bd < 0 ? i : bd);
        p.lighter[i] = (uint8_t)(bl < 0 ? i : bl);
        p.shade[i] = (uint8_t)(bs < 0 ? i : bs);
    }
    return p;
}

} // namespace pixu
