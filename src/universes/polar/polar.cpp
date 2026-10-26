// POLAR NIGHT: a snowfield under the stars, aurora ribbons.
//
// One world: a wide snowfield at night, low snowy ranges and a dark treeline on the horizon, a deep sky full of
// stars (the Milky Way faint across it), and the aurora. The aurora is the music: each of its ribbons follows a group
// of frequency bands (the far, low ribbon the bass, the nearer and higher ones the mids and the highs), brightening,
// growing taller and rippling with them, and its light falls on the snow and on the pieces. The well is a sheet of
// dark ice with a hairline rim and frost at its foot; the pieces are tinted ice (the night refracted through it, clear
// edges, a dense core, crisp chamfers, a faceted top, fractures and bubbles), the falling piece still water; the cells of one piece one body of ice (two pieces never
// join, a thin seam stays between them).
//
// Calm is a still night (one faint ribbon low on the horizon, the stars, a few flakes drifting), mid wakes the
// aurora (several curtains, folds drifting with the tempo, a light ripple on each beat) and the snow falls, peak
// fills the sky (curtains overhead, tall rays converging upward, the kicks sending surges of light along them,
// blowing snow skimming over the drifts). A lock freezes the piece (a front of frost crystallising from where it
// touched down, the water turning to ice behind it); a cleared row cracks and shatters into falling shards; a Tetris
// is an auroral substorm (the whole sky flares and dances); slow motion freezes time (the falling snow hangs in the
// air and glints, the aurora barely moves, the colors drain to silver, frost creeps in from the edges of the screen);
// game over freezes the stack white from the bottom up, then it crumbles into powder while the blowing snow buries
// the field. A change of universe goes through a whiteout of blowing snow.
//
// Cost: the world (sky, aurora slices, ranges, snowfield) is drawn at a reduced resolution (at most 540 lines); only
// the ice, the snow, the glitter and the text are drawn at full resolution.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane (like CLOUDSCAPE), so
// the well, the previews and the HUD anchors sit exactly where the layout puts them; the world behind has a camera of
// its own.
#include "universes/polar/polar.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/polar/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Josefin Sans Light and Regular, SIL Open Font License, see OFL-JosefinSans.txt beside this file.
const unsigned char FONT_LIGHT[] = {
#include "universes/polar/JosefinSans-Light.ttf.inc"
};
const unsigned char FONT_REGULAR[] = {
#include "universes/polar/JosefinSans-Regular.ttf.inc"
};

namespace shaders = polar_shaders;
using ugl::set1f;
using ugl::set1i;
using ugl::set2f;
using ugl::set3f;
using ugl::set4f;
using ugl::setMat;

GLuint compile(const char* vs, const char* fs, const char* name) {
    std::string f(fs);
    f.insert(f.find('\n') + 1, shaders::COMMON_GLSL);
    return ugl::program(vs, f.c_str(), name);
}

constexpr float TAN_H = 0.4452f;                 // the world camera's half field of view (vertical, tan of 24 deg)
constexpr float EYE = 1.7f;                      // standing in the snow
constexpr float WELL_X = 5.2f, WELL_Y = 10.2f;   // the well's sheet of ice (half extents, board)
constexpr int FLAKES = 320;
constexpr float FREEZE_SECONDS = 1.8f;           // a locked piece freezing at once, then steaming for a moment

vec2 v2(float x, float y) { return vec2{x, y}; }
vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
vec2 operator*(vec2 a, float k) { return {a.x * k, a.y * k}; }

// A cell's neighbours of the same piece (same(dx, dy), dy in board rows, down): bits 1 left, 2 right, 4 up, 8 down,
// 16 up-left, 32 up-right, 64 down-left, 128 down-right (the tile shader builds one body of ice from them).
template <class F> int neighbourMask(F same) {
    int m = 0;
    if (same(-1, 0)) m |= 1;
    if (same(1, 0)) m |= 2;
    if (same(0, -1)) m |= 4;
    if (same(0, 1)) m |= 8;
    if (same(-1, -1)) m |= 16;
    if (same(1, -1)) m |= 32;
    if (same(-1, 1)) m |= 64;
    if (same(1, 1)) m |= 128;
    return m;
}

// A piece's seed (its veils and cracks), from its id.
float pieceSeed(int id) { return (float)((id * 37) % 101) * 0.73f + 0.5f; }

// A cell's neighbour mask with where its piece landed (relative to the cell, board units, within +-4 by 1/8) packed
// above it, for the cells that leave the stack (shaders.hpp: unpackContact).
float packContact(int mask, float dx, float dy) {
    const int qx = std::clamp((int)std::lround((dx + 4.f) * 8.f), 0, 63), qy = std::clamp((int)std::lround((dy + 4.f) * 8.f), 0, 63);
    return (float)(mask + 256 * (qx + 64 * qy));
}

vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }

// The variants: each a designed night (sRGB here, linear once loaded). Colors: zenith, horizon, the twilight glow,
// the snow in the light, in the shade, the far ranges, the moon, the aurora's lower edge, its curtain, its upper
// fringe, the HUD's ink and halo, frost; the seven ices (I O T S Z J L). Numbers: the glow's azimuth and amount, the
// moon (0 none .. 1), its azimuth, elevation and size, the 22 degree halo, the aurora's strength and height, stars,
// the Milky Way, trees, haze.
struct VariantDef {
    const char* name;
    unsigned zenith, horizon, glow, snow, shade, mount, moonCol, aurA, aurB, aurC, ink, halo, frost;
    unsigned piece[7];
    float glowAz, glowAmt, moon, moonAz, moonEl, moonSize, halo22, aurora, aurHeight, stars, milky, trees, haze, lightMix;
};
const VariantDef VARIANTS[] = {
    {"EMERALD", 0x02060f, 0x0c1a2a, 0x203a50, 0xa8bcd4, 0x1a2a40, 0x34465e, 0xdfe8ff, 0x52ffa8, 0x23d68e, 0x7d4cd8,
     0xeaf6ff, 0x020812, 0xd8ecff, {0x2cc4e8, 0xf0c64e, 0x8e6cf0, 0x3ed68a, 0xf05874, 0x4870f0, 0xf08a3e},
     0.f, 0.f, 0.f, -0.62f, 0.34f, 0.018f, 0.f, 1.0f, 0.75f, 1.f, 0.8f, 0.55f, 0.3f, 0.20f},
    {"VIOLET VEIL", 0x050311, 0x161030, 0x2a1a48, 0xb4b0d8, 0x221a40, 0x3a3460, 0xeee6ff, 0xff7ad2, 0xb052f0, 0x4a56e8,
     0xf4eeff, 0x06041a, 0xe8e0ff, {0x6c8cff, 0xffc860, 0xb060ff, 0x4cdcc6, 0xff4c90, 0x4a56e8, 0xff9070},
     0.f, 0.f, 0.f, 0.6f, 0.3f, 0.016f, 0.f, 0.95f, 0.85f, 1.f, 0.9f, 0.4f, 0.3f, 0.35f},
    {"CRIMSON", 0x07030a, 0x1a0c14, 0x3a1420, 0xc0aab4, 0x2a1824, 0x3e2a36, 0xffe6e0, 0x6cf0a2, 0xff3c50, 0x9a1c56,
     0xfff0f0, 0x0a0408, 0xffe8ec, {0x5cc4e8, 0xffbc56, 0xe0507c, 0x56cc88, 0xff3636, 0x9048d0, 0xff7840},
     0.f, 0.f, 0.f, -0.6f, 0.3f, 0.016f, 0.f, 1.05f, 1.f, 0.9f, 0.6f, 0.6f, 0.3f, 0.80f},
    {"ARCTIC TEAL", 0x01070e, 0x08202c, 0x0e3448, 0x9ec8dc, 0x123040, 0x2a4a5e, 0xe2f4ff, 0x48f2e2, 0x2e8cff, 0x6a4cf0,
     0xe6fbff, 0x010810, 0xd6f6ff, {0x4ee4ff, 0xffe070, 0x8a76ff, 0x2ed8b0, 0xff6a90, 0x2e62f0, 0xffac5c},
     0.f, 0.f, 0.f, 0.58f, 0.33f, 0.017f, 0.f, 1.0f, 0.7f, 1.f, 0.7f, 0.7f, 0.35f, 0.25f},
    {"MOONLIT", 0x07122a, 0x2a3c5e, 0x3c5070, 0xd8e2f4, 0x34466a, 0x56688c, 0xf4f6ff, 0x9effcc, 0x7ad8c0, 0x8ca0ff,
     0xf6f9ff, 0x061026, 0xf2f8ff, {0x5ebce8, 0xeccc78, 0xa68ae8, 0x6ed0a0, 0xec7a8c, 0x6080e8, 0xeca068},
     0.f, 0.f, 1.f, -0.55f, 0.36f, 0.024f, 0.f, 0.45f, 0.5f, 0.5f, 0.2f, 0.65f, 0.45f, 0.20f},
    {"BLUE HOUR", 0x0c1a44, 0x485c94, 0xf09a84, 0xc8cce8, 0x30406e, 0x4a5684, 0xfff0e6, 0x5ef0c8, 0x46b4e0, 0x8a6cf0,
     0xf6f4ff, 0x0a1438, 0xf0f0ff, {0x52c2ff, 0xffd858, 0xff7cb4, 0x5ed6a4, 0xff5a48, 0x6676ff, 0xff9644},
     0.75f, 0.85f, 0.f, -0.5f, 0.3f, 0.016f, 0.f, 0.55f, 0.55f, 0.55f, 0.f, 0.6f, 0.45f, 0.20f},
    {"SOLAR STORM", 0x04030e, 0x140f26, 0x241a3c, 0xb0b4d0, 0x1c1a36, 0x342e52, 0xf0eaff, 0x8cff5a, 0xff4fa6, 0x6a4cff,
     0xf2f0ff, 0x050412, 0xeaf0ff, {0x3edcff, 0xffde48, 0xc05cff, 0x5cff98, 0xff4c78, 0x4868ff, 0xff8a38},
     0.f, 0.f, 0.f, 0.6f, 0.3f, 0.016f, 0.f, 1.25f, 1.f, 1.f, 0.5f, 0.5f, 0.25f, 0.65f},
    {"ICE FOG", 0x111c30, 0x3e4c64, 0x52607a, 0xc4ccd8, 0x3a465a, 0x5a6578, 0xf0f4ff, 0xb4ff9e, 0x7cd2c4, 0x8eb0ff,
     0xf4f7fc, 0x0e1626, 0xf2f6ff, {0x64b4e0, 0xdcc480, 0xa08ad4, 0x6cbc98, 0xdc7c88, 0x6280cc, 0xe09c6c},
     0.f, 0.f, 0.85f, -0.6f, 0.4f, 0.02f, 1.f, 0.6f, 0.45f, 0.35f, 0.f, 0.35f, 0.9f, 0.30f},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

using Palette = PolarUniverse::Palette;
static_assert(sizeof(Palette) % sizeof(float) == 0, "a palette is floats only");
constexpr int PAL_FLOATS = (int)(sizeof(Palette) / sizeof(float));

Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    auto L = [](unsigned c) { return ugl::toLinear(hex(c)); };
    Palette p;
    p.zenith = L(d.zenith); p.horizon = L(d.horizon); p.glow = L(d.glow); p.snow = L(d.snow); p.shade = L(d.shade);
    p.mount = L(d.mount); p.moonCol = L(d.moonCol); p.aurA = L(d.aurA); p.aurB = L(d.aurB); p.aurC = L(d.aurC);
    p.ink = L(d.ink); p.halo = L(d.halo); p.frost = L(d.frost);
    for (int i = 0; i < 7; i++) p.piece[i] = L(d.piece[i]);
    p.glowAz = d.glowAz; p.glowAmt = d.glowAmt; p.moon = d.moon; p.moonAz = d.moonAz; p.moonEl = d.moonEl;
    p.moonSize = d.moonSize; p.halo22 = d.halo22; p.aurora = d.aurora; p.aurHeight = d.aurHeight; p.stars = d.stars;
    p.milky = d.milky; p.trees = d.trees; p.haze = d.haze; p.lightMix = d.lightMix;
    return p;
}

// Slow motion: time frozen in the deep cold. The colors drain to silver blue, the aurora dims and pales, the moon
// (if any) stays. The ices keep their tints.
Palette frozenOf(const Palette& d) {
    auto L = [](unsigned c) { return ugl::toLinear(hex(c)); };
    Palette p = d;
    p.zenith = lerp(d.zenith, L(0x050a18), 0.6f); p.horizon = lerp(d.horizon, L(0x1c2a44), 0.6f);
    p.snow = lerp(d.snow, L(0xc8d6f0), 0.6f); p.shade = lerp(d.shade, L(0x1c2a46), 0.6f);
    p.aurA = lerp(d.aurA, L(0xc8f0ff), 0.6f); p.aurB = lerp(d.aurB, L(0x7ea4e0), 0.6f); p.aurC = lerp(d.aurC, L(0x5a6ac0), 0.6f);
    p.aurora = d.aurora * 0.55f; p.glowAmt *= 0.4f; p.stars = std::max(d.stars, 0.8f);
    return p;
}

void lerpPalette(Palette& a, const Palette& b, float k) {
    float* pa = reinterpret_cast<float*>(&a);
    const float* pb = reinterpret_cast<const float*>(&b);
    for (int i = 0; i < PAL_FLOATS; i++) pa[i] += (pb[i] - pa[i]) * k;
}

vec3 dirOf(float az, float el) { return {std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el)}; }

// The ribbons (far to near): their footprint's distance ahead, and how present each is at calm, mid and peak.
const float RIB_Z[6] = {-300.f, -175.f, -110.f, -70.f, -44.f, -28.f};
const float RIB_LV[6][3] = {{0.6f, 0.85f, 1.f}, {0.5f, 0.9f, 1.f}, {0.f, 0.7f, 1.f}, {0.f, 0.4f, 0.95f}, {0.f, 0.f, 0.85f}, {0.f, 0.f, 0.7f}};
// Which frequency bands each ribbon follows (first band, count): the far one the bass, the nearest the highs.
const int RIB_BANDS[6][2] = {{0, 3}, {3, 3}, {6, 3}, {9, 2}, {11, 2}, {13, 3}};

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool PolarUniverse::init(int width, int height) {
    progSky_ = compile(ugl::FULLSCREEN_VS, shaders::SKY_FS, "polar sky");
    progComp_ = compile(ugl::FULLSCREEN_VS, shaders::COMPOSITE_FS, "polar composite");
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "polar shape");
    progTile_ = compile(shaders::TILE_VS, shaders::TILE_FS, "polar tile");
    progDot_ = compile(shaders::DOT_VS, shaders::DOT_FS, "polar dot");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "polar text");
    progDown_ = compile(ugl::FULLSCREEN_VS, shaders::DOWN_FS, "polar down");
    progUp_ = compile(ugl::FULLSCREEN_VS, shaders::UP_FS, "polar up");
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "polar post");

    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    auto instanced = [&](GLuint& vao, GLuint& vbo, int vec4s, size_t stride) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        for (int i = 0; i < vec4s; i++) {
            glEnableVertexAttribArray(1 + i);
            glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(sizeof(float) * 4 * i));
            glVertexAttribDivisor(1 + i, 1);
        }
    };
    instanced(tileVao_, tileVbo_, 4, sizeof(Tile));
    instanced(dotVao_, dotVbo_, 3, sizeof(Dot));

    glGenVertexArrays(1, &shapeVao_);
    glGenBuffers(1, &shapeVbo_);
    glBindVertexArray(shapeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)(sizeof(float) * 2));

    glGenVertexArrays(1, &textVao_);
    glGenBuffers(1, &textVbo_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)(sizeof(float) * 2));
    glBindVertexArray(0);

    buildNoise();
    buildFont(fontLight_, FONT_LIGHT);
    buildFont(fontRegular_, FONT_REGULAR);
    pal_ = cur_ = paletteOf(0);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// A tileable 256x256 noise texture: r and b gradient noise (two scales), g and a heightfields of round puffs.
void PolarUniverse::buildNoise() {
    const int N = 256;
    std::vector<unsigned char> px(N * N * 4);
    auto hash2 = [](int x, int y, int s) {
        uint64_t h = splitmix64((uint64_t)(uint32_t)x * 73856093ull ^ (uint64_t)(uint32_t)y * 19349663ull ^ (uint64_t)s * 83492791ull);
        return (h >> 40) * (1.0f / 16777216.0f);
    };
    auto perlin = [&](float x, float y, int period, int seed) {
        const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
        const float fx = x - x0, fy = y - y0;
        auto grad = [&](int ix, int iy, float dx, float dy) {
            const float a = hash2(((ix % period) + period) % period, ((iy % period) + period) % period, seed) * TAU;
            return std::cos(a) * dx + std::sin(a) * dy;
        };
        auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
        const float n00 = grad(x0, y0, fx, fy), n10 = grad(x0 + 1, y0, fx - 1, fy);
        const float n01 = grad(x0, y0 + 1, fx, fy - 1), n11 = grad(x0 + 1, y0 + 1, fx - 1, fy - 1);
        const float u = fade(fx), v = fade(fy);
        return lerpf(lerpf(n00, n10, u), lerpf(n01, n11, u), v);
    };
    auto domes = [&](float x, float y, int cells, int seed) {
        const int cx = (int)std::floor(x), cy = (int)std::floor(y);
        float best = 0.f;
        for (int j = -2; j <= 2; j++)
            for (int i = -2; i <= 2; i++) {
                const int ix = cx + i, iy = cy + j;
                const int wx = ((ix % cells) + cells) % cells, wy = ((iy % cells) + cells) % cells;
                const float px_ = ix + 0.15f + 0.7f * hash2(wx, wy, seed), py_ = iy + 0.15f + 0.7f * hash2(wx, wy, seed + 1);
                const float R = 0.7f + 0.55f * hash2(wx, wy, seed + 2);
                const float d2 = ((x - px_) * (x - px_) + (y - py_) * (y - py_)) / (R * R);
                if (d2 < 1.f) best = std::max(best, std::sqrt(1.f - d2) * (0.75f + 0.25f * R));
            }
        return best;
    };
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            const float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            const float r = 0.5f + 0.5f * (perlin(u * 8, v * 8, 8, 11) * 0.7f + perlin(u * 16, v * 16, 16, 12) * 0.35f);
            const float b = 0.5f + 0.5f * (perlin(u * 16, v * 16, 16, 21) * 0.7f + perlin(u * 32, v * 32, 32, 22) * 0.35f);
            const float g = domes(u * 8, v * 8, 8, 31);
            const float a = domes(u * 16, v * 16, 16, 41);
            unsigned char* p = &px[(y * N + x) * 4];
            p[0] = (unsigned char)std::clamp((int)(r * 255.f + 0.5f), 0, 255);
            p[1] = (unsigned char)std::clamp((int)(g * 255.f + 0.5f), 0, 255);
            p[2] = (unsigned char)std::clamp((int)(b * 255.f + 0.5f), 0, 255);
            p[3] = (unsigned char)std::clamp((int)(a * 255.f + 0.5f), 0, 255);
        }
    glGenTextures(1, &noiseTex_);
    glBindTexture(GL_TEXTURE_2D, noiseTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

// Glyphs baked as signed distance fields: they scale cleanly, and the distance gives the halo of night.
void PolarUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] polar font init failed\n");
        delete info;
        return;
    }
    f.info = info;
    f.px = 64.f;
    f.scaleTT = stbtt_ScaleForPixelHeight(info, f.px);
    int x0, y0, x1, y1;
    if (stbtt_GetCodepointBox(info, 'H', &x0, &y0, &x1, &y1)) f.capH = y1 * f.scaleTT;
    const int AW = 1024, AH = 1024, PAD = 10;
    std::vector<unsigned char> atlas(AW * AH, 0);
    int x = 1, y = 1, rowH = 0;
    for (int ch = 32; ch < 128; ch++) {
        Glyph& g = f.g[ch - 32];
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, ch, &adv, &lsb);
        g.adv = adv * f.scaleTT;
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* sdf = stbtt_GetCodepointSDF(info, f.scaleTT, ch, PAD, 128, 128.f / PAD, &w, &h, &xo, &yo);
        if (!sdf) { g.w = g.h = 0; continue; }
        if (x + w + 1 > AW) { x = 1; y += rowH + 1; rowH = 0; }
        if (y + h + 1 > AH) { stbtt_FreeSDF(sdf, nullptr); break; }
        for (int r = 0; r < h; r++) std::memcpy(&atlas[(y + r) * AW + x], sdf + r * w, w);
        g.x0 = x / (float)AW; g.y0 = y / (float)AH; g.x1 = (x + w) / (float)AW; g.y1 = (y + h) / (float)AH;
        g.ox = (float)xo; g.oy = (float)yo; g.w = (float)w; g.h = (float)h;
        x += w + 1;
        rowH = std::max(rowH, h);
        stbtt_FreeSDF(sdf, nullptr);
    }
    glGenTextures(1, &f.tex);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, AW, AH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f.ok = true;
}

void PolarUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    const float aspect = (float)width_ / height_;
    const layout::Camera cam = layout::camera(width_, height_);
    const float hh = cam.dist * std::tan(cam.fovDeg * 0.5f * PI / 180.f), hw = hh * aspect;
    viewH_ = v2(hw, hh);
    viewC_ = v2(cam.target.x, cam.target.y);
    vp_ = mat4::ortho(viewC_.x - hw, viewC_.x + hw, viewC_.y - hh, viewC_.y + hh, -1.f, 1.f);
    px_ = 2.f * hh / height_;
    createTargets();
    if (flakes_.empty()) {
        Rng r(0xF1A4Eull);
        for (int i = 0; i < FLAKES; i++) {
            Flake f;
            f.x = r.range(-1.f, 1.f);
            f.y = r.range(-1.f, 1.f);
            f.depth = std::pow(r.uniform(), 1.6f);
            f.phase = r.range(0.f, TAU);
            f.size = r.range(0.75f, 1.25f);
            flakes_.push_back(f);
        }
    }
}

void PolarUniverse::createTargets() {
    // The world at most 540 lines (half resolution at 1080p).
    const int ch = std::max(90, std::min(540, height_ / 2));
    const int cw = std::max(160, (int)std::lround((float)ch * width_ / height_));
    skyT_.create(cw, ch, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    glBindTexture(GL_TEXTURE_2D, skyT_.tex);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    sceneT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    int w = width_, h = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        bloomT_[i].create(w, h, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

PolarUniverse::~PolarUniverse() {
    skyT_.destroy();
    sceneT_.destroy();
    for (auto& t : bloomT_) t.destroy();
    for (GLuint p : {progSky_, progComp_, progShape_, progTile_, progDot_, progText_, progDown_, progUp_, progPost_})
        glDeleteProgram(p);
    for (GLuint v : {tileVao_, dotVao_, shapeVao_, textVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, tileVbo_, dotVbo_, shapeVbo_, textVbo_}) glDeleteBuffers(1, &b);
    if (noiseTex_) glDeleteTextures(1, &noiseTex_);
    for (Font* f : {&fontLight_, &fontRegular_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// =====================================================================================================
// Song, level, bonus
// =====================================================================================================

// The night from the song: wild, dense songs get the storm, dark minor ones the deep auroras (crimson, violet,
// emerald), bright major ones the moonlit and twilight nights, soft sparse ones the ice fog; otherwise the key's hue.
int PolarUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    int v;
    if (!t.hasSong) v = 0;
    else if (t.dynamics > 0.72f && (t.density > 0.6f || t.bpm > 128.f)) v = 6;
    else if (t.minor && t.brightness < 0.45f) v = h < 0.33f ? 2 : (h < 0.66f ? 1 : 0);
    else if (!t.minor && t.brightness > 0.58f) v = h < 0.5f ? 4 : 5;
    else if (t.density < 0.32f && t.dynamics < 0.4f) v = 7;
    else {
        const int byHue[8] = {2, 5, 0, 3, 4, 1, 7, 6};
        v = byHue[std::min(7, (int)(h * 8.f))];
    }
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void PolarUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    seedF_ = (float)(look.seed % 1000) * 0.137f;
    rng_ = Rng(look.seed ^ 0xA0120ull);
    wind_ = rng_.chance(0.5f) ? 0.35f : -0.35f;
    if (seconds <= 0.f) palSet_ = false; // snap on the first frame
    else { surge_ = std::max(surge_, 0.8f); surgeX_ = rng_.range(-60.f, 60.f); }
}

void PolarUniverse::setLevel(int level, float energy, float seconds, float impact) {
    level = std::clamp(level, 0, 2);
    energy_ = energy;
    if (!hasLevel_ || seconds < 0.1f) {
        hasLevel_ = true;
        level_ = level;
        lv_ = lvFrom_ = lvTo_ = (float)level;
        lvT_ = 1.f;
        return;
    }
    const bool rising = level > level_;
    level_ = level;
    lvFrom_ = lv_;
    lvTo_ = (float)level;
    lvT_ = 0.f;
    lvDur_ = std::clamp(seconds, rising ? 1.0f : 2.0f, 6.f);
    if (rising) { surge_ = std::max(surge_, 0.6f + 0.9f * impact); surgeX_ = rng_.range(-50.f, 50.f); }
}

void PolarUniverse::refresh(float strength) {
    surge_ = std::max(surge_, 0.6f * strength);
    surgeX_ = rng_.range(-70.f, 70.f);
}

void PolarUniverse::setBonus(bool on) { slowOn_ = on; }

void PolarUniverse::levelUp() {
    surge_ = std::max(surge_, 1.f);
    surgeX_ = rng_.range(-40.f, 40.f);
    for (int i = 0; i < 4; i++)
        glints_.push_back({v2((i & 1 ? 1.f : -1.f) * WELL_X, (i < 2 ? 1.f : -1.f) * WELL_Y), -0.1f * i, 0.8f, 0.9f});
}

vec2 PolarUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints PolarUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    h.background = lerp(p.horizon, p.zenith, 0.5f);
    h.light = false;
    h.ink = lerp(p.frost, p.snow, 0.4f);
    h.accent = p.aurA;
    h.styles = TS_INK | TS_CURTAIN | TS_LUMA; // blowing snow, aurora curtains, the darks first
    return h;
}

void PolarUniverse::powder(vec2 p, vec2 v, float size, float life, float alpha, float delay, bool puff) {
    Powder q;
    q.p = p; q.v = v; q.size = size; q.t = 0.f; q.life = life; q.alpha = alpha; q.delay = delay; q.puff = puff;
    q.seed = rng_.uniform();
    powder_.push_back(q);
}

// A block of ice turned to snow, crumbling: fine powder blown off by the wind, gone in a moment.
void PolarUniverse::cellToPowder(float x, float y, float delay, float wind, float strength) {
    const int n = (int)(4 + 3 * strength);
    for (int k = 0; k < n; k++)
        powder(v2(x + rng_.range(-0.45f, 0.45f), y + rng_.range(-0.45f, 0.45f)),
               v2(wind * rng_.range(0.8f, 3.0f) * strength + rng_.range(-0.4f, 0.4f), rng_.range(0.2f, 1.8f) * strength),
               rng_.range(0.05f, 0.1f), rng_.range(0.35f, 0.65f), rng_.range(0.6f, 1.f), delay + rng_.range(0.f, 0.08f));
}

// =====================================================================================================
// Game events
// =====================================================================================================

void PolarUniverse::onEvent(const GameEvent& ev, const Game& game) {
    // the cells of one piece inside an event's cells (cleared rows, a topped-out stack): one body of ice
    auto idAt = [&](int x, int y, int type) {
        if (x < 0 || x >= Game::W || y < 0 || y >= Game::H) return -1;
        const IceCell& ic = ice_[y][x];
        return ic.type == type ? ic.id : -1;
    };
    auto maskOf = [&](const GameEvent::CellInfo& c) {
        const int id = idAt(c.x, c.y, c.type);
        if (id < 0) return 0;
        return neighbourMask([&](int dx, int dy) {
            for (auto& o : ev.cells)
                if (o.x == c.x + dx && o.y == c.y + dy && o.type == c.type && idAt(o.x, o.y, o.type) == id) return true;
            return false;
        });
    };
    // a leaving cell keeps its piece's frame (seed, where it landed), so its ice does not change as it goes
    auto framePiece = [&](Dying& d, int x, int y, int type) {
        const int id = idAt(x, y, type);
        if (id < 0) return;
        d.pseed = pieceSeed(id);
        d.cdx = ice_[y][x].cx - x;
        d.cdy = y - ice_[y][x].cy;
    };
    switch (ev.type) {
    case GameEvent::Lock: {
        // The piece freezes from where it touched down: its resting cells (on the floor or on the stack).
        float cx = 0.f, cy = 0.f, n = 0.f;
        int bottom = -1;
        for (auto& q : ev.cells) bottom = std::max(bottom, q.y);
        auto inPiece = [&](int x, int y) {
            for (auto& o : ev.cells)
                if (o.x == x && o.y == y) return true;
            return false;
        };
        for (auto& q : ev.cells) {
            if (inPiece(q.x, q.y + 1)) continue;
            if (q.y + 1 < Game::H && game.cell(q.x, q.y + 1).type < 0) continue;
            cx += (float)q.x; cy += q.y + 0.5f; n += 1.f;
        }
        if (n <= 0.f)
            for (auto& q : ev.cells)
                if (q.y == bottom) { cx += (float)q.x; cy += q.y + 0.5f; n += 1.f; }
        if (n > 0.f) { cx /= n; cy /= n; }
        // the piece's cells keep its identity (they join into one body of ice, never with another piece)
        const int id = pieceCounter_++;
        for (auto& c : ev.cells)
            if (c.y >= 0 && c.y < Game::H && c.x >= 0 && c.x < Game::W) ice_[c.y][c.x] = {id, c.type, iceClock_, cx, cy};
        // a tiny sparkle where it touched (just inside the ice)
        const vec3 p = layout::cellPos(cx, cy - 0.62f);
        glints_.push_back({v2(p.x, p.y), 0.f, 0.3f, 0.32f});
        break;
    }
    case GameEvent::Clear: {
        const bool tetris = ev.count >= 4;
        // The rows crack from where the piece landed, then shatter (the shards in the tile shader).
        const float ox = layout::cellPos(ev.x, 0.f).x;
        const float seed = rng_.range(0.f, 50.f);
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0.f; d.type = std::clamp(c.type, 0, 6); d.mask = maskOf(c);
            d.over = false; d.crumbled = false; d.delay = 0.f;
            d.seed = seed; d.ox = ox; d.power = tetris ? 1.6f : 1.f;
            framePiece(d, c.x, c.y, c.type);
            dying_.push_back(d);
        }
        {
            bool cleared[Game::H] = {};
            for (auto& c : ev.cells)
                if (c.y >= 0 && c.y < Game::H) cleared[c.y] = true;
            IceCell next[Game::H][Game::W];
            int dst = Game::H - 1;
            for (int y = Game::H - 1; y >= 0; y--) {
                if (cleared[y]) continue;
                for (int x = 0; x < Game::W; x++) {
                    next[dst][x] = ice_[y][x];
                    next[dst][x].cy += (float)(dst - y);
                }
                dst--;
            }
            for (int y = 0; y < Game::H; y++)
                for (int x = 0; x < Game::W; x++) ice_[y][x] = next[y][x];
        }
        if (tetris) {
            stormT_ = 0.f;
            surge_ = std::max(surge_, 1.5f);
            surgeX_ = rng_.range(-30.f, 30.f);
            for (int i = 0; i < 6; i++)
                glints_.push_back({v2(rng_.range(-WELL_X, WELL_X), rng_.range(-WELL_Y, WELL_Y)), -0.08f * i, 0.9f, 1.1f});
        } else {
            surge_ = std::max(surge_, 0.25f * ev.count);
            surgeX_ = rng_.range(-70.f, 70.f);
        }
        break;
    }
    case GameEvent::BonusReady:
        glints_.push_back({v2(layout::GAUGE_CENTER.x + 0.5f, layout::GAUGE_CENTER.y + 0.5f), 0.f, 0.9f, 1.f});
        break;
    case GameEvent::TopOut: {
        // The stack freezes white from the bottom up, then crumbles into powder.
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0.f; d.type = std::clamp(c.type, 0, 6); d.mask = maskOf(c);
            d.over = true; d.crumbled = false;
            d.delay = (Game::H - 1 - c.y) * 0.05f + rng_.range(0.f, 0.05f);
            d.seed = rng_.uniform();
            framePiece(d, c.x, c.y, c.type);
            dying_.push_back(d);
        }
        for (auto& row : ice_)
            for (auto& c : row) c = IceCell{};
        overT_ = 0.f;
        break;
    }
    default: break;
    }
}

// =====================================================================================================
// Simulation
// =====================================================================================================

void PolarUniverse::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    worldTime_ += wdt;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float tempo = std::sqrt(std::clamp(music.bpm, 60.f, 200.f) / 120.f);

    // Beats: the curtains ripple.
    {
        const float ph = music.beatPhase;
        if (ph < lastBeatPhase_ - 0.5f) beats_ += 1.f;
        lastBeatPhase_ = ph;
    }
    beatGlow_ = approach(beatGlow_, music.hasSong ? saturate(music.beatPulse) : 0.f, 12.f, dt);
    pulse_ = beatGlow_ * (0.3f * wm + 0.5f * wp);

    // The aurora's time: slow at calm, livelier with the level and the tempo; nearly frozen in slow motion.
    aurTime_ += wdt * (0.5f + 0.8f * wm + 1.2f * wp) * tempo * (1.f + 1.5f * storm_);

    // The bands drive the ribbons.
    for (int k = 0; k < RIBBONS; k++) {
        float b = 0.f;
        for (int i = 0; i < RIB_BANDS[k][1]; i++) b += music.bandsFast[RIB_BANDS[k][0] + i];
        b /= RIB_BANDS[k][1];
        if (!music.hasSong) b = 0.45f + 0.15f * std::sin(worldTime_ * 0.3f + k);
        band_[k] = approach(band_[k], saturate(b), 7.f, wdt);
        const float* L = RIB_LV[k];
        const float pres = lv_ < 1.f ? lerpf(L[0], L[1], smoothstepf(0.f, 1.f, lv_)) : lerpf(L[1], L[2], smoothstepf(1.f, 2.f, lv_));
        ribbon_[k] = approach(ribbon_[k], pres, 1.2f, dt);
    }

    // Kicks: a surge of light runs along a curtain.
    sinceKick_ += dt;
    const float kick = std::max(music.kick, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    if (kick > 0.55f && prevKick_ <= 0.55f && sinceKick_ > 0.2f) {
        sinceKick_ = 0.f;
        const float s = 0.25f * wm + 0.8f * wp;
        if (s > 0.05f) {
            surge_ = std::max(surge_, s);
            surgeX_ = rng_.range(-70.f, 70.f);
        }
    }
    prevKick_ = kick;
    surge_ = approach(surge_, 0.f, 2.2f, dt);
    surgeX_ += wdt * 25.f * (wind_ > 0.f ? 1.f : -1.f);

    // Tetris: the substorm (a fast flare, the curtains dancing), easing over a few seconds.
    stormT_ += dt;
    storm_ = stormT_ < 4.f ? (stormT_ < 0.3f ? stormT_ / 0.3f : 1.f) * (1.f - smoothstepf(0.5f, 4.f, stormT_)) : 0.f;
    flare_ = storm_;

    // The wind: calm, then gusts at the peaks.
    gust_ = approach(gust_, wp * (0.6f + 0.4f * std::sin(worldTime_ * 0.37f)) + overDrift_ * 1.5f, 0.6f, dt);
    const float windNow = wind_ * (1.f + 2.5f * gust_);
    drift_ += wdt * windNow * 6.f;

    // The snowfall: flakes in layers (depth 0 far .. 1 near); time stops for them in slow motion.
    {
        const float fdt = dt * lerpf(1.f, 0.015f, smoothstepf(0.f, 0.6f, slow_)) * (worldPaused_ ? 0.f : 1.f);
        flakeTime_ += fdt;
        const float fall = 0.55f + 0.35f * wm + 0.5f * wp;
        for (Flake& f : flakes_) {
            const float sp = (0.25f + 1.1f * f.depth) * fall;
            const float sway = 0.25f * std::sin(flakeTime_ * (0.6f + 0.5f * f.depth) + f.phase);
            f.x += (windNow * (0.3f + 0.9f * f.depth) * (0.5f + gust_) + sway) * fdt / viewH_.x;
            f.y -= sp * fdt / viewH_.y;
            if (f.y < -1.05f) { f.y += 2.1f; f.x = rng_.range(-1.f, 1.f); }
            if (f.x < -1.05f) f.x += 2.1f;
            if (f.x > 1.05f) f.x -= 2.1f;
        }
    }

    // Cold vapor: a slight wisp rising off the top faces of the pieces still freezing, thinning as they set.
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const float age = freezeAge(game, x, y);
            if (age < 0.f) continue;
            if (y > Game::HIDDEN && game.cell(x, y - 1).type >= 0) continue;
            const float k = age / FREEZE_SECONDS;
            const float rate = 4.f * (1.f - k) * smoothstepf(0.f, 0.08f, k);
            if (rng_.uniform() > rate * wdt) continue;
            const vec3 p = layout::cellPos((float)x, y - game.rowOffset(y));
            powder(v2(p.x + rng_.range(-0.4f, 0.4f), p.y + 0.42f), v2(rng_.range(-0.06f, 0.06f), rng_.range(0.15f, 0.3f)),
                   rng_.range(0.2f, 0.32f), rng_.range(1.8f, 2.6f), 0.22f + 0.18f * (1.f - k), 0.f, true);
        }

    // Effects age (slowed by the slow motion like the world).
    for (auto& p : powder_) {
        p.t += wdt;
        if (p.t < p.delay) continue;
        p.v.x += windNow * 0.8f * wdt;
        p.v.y -= (p.puff ? -0.15f : 1.6f) * wdt;
        p.v = p.v * std::exp(-(p.puff ? 1.2f : 0.9f) * wdt);
        p.p = p.p + p.v * wdt;
    }
    powder_.erase(std::remove_if(powder_.begin(), powder_.end(), [](const Powder& p) { return p.t > p.delay + p.life; }), powder_.end());
    for (auto& d : dying_) {
        d.t += d.over ? dt : wdt;
        if (d.over && !d.crumbled && d.t - d.delay > 1.3f + d.seed * 0.5f) {
            d.crumbled = true;
            cellToPowder(d.x, d.y, 0.f, wind_ > 0.f ? 1.f : -1.f, 1.1f);
        }
    }
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) {
        return d.over ? d.t > d.delay + 2.6f : d.t > 0.62f * std::sqrt(d.power);
    }), dying_.end());
    iceClock_ += wdt;
    for (auto& g : glints_) g.t += dt;
    glints_.erase(std::remove_if(glints_.begin(), glints_.end(), [](const Glint& g) { return g.t > g.life; }), glints_.end());

    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
}

// =====================================================================================================
// Geometry
// =====================================================================================================

void PolarUniverse::tile(float x, float y, float hw, float hh, const vec3& c, float alpha, int kind, float age,
                         int mask, float fog, float e0, float e1, float e2, float e3) {
    tiles_.push_back({{x, y, hw, hh}, {c.x, c.y, c.z, alpha}, {(float)kind, age, (float)mask, fog}, {e0, e1, e2, e3}});
}

// Seconds since the piece of a locked cell landed while it is still freezing, else -1.
float PolarUniverse::freezeAge(const Game& g, int x, int y) const {
    const IceCell& ic = ice_[y][x];
    if (ic.id < 0 || g.cell(x, y).type != ic.type) return -1.f;
    const float a = iceClock_ - ic.t0;
    return a < FREEZE_SECONDS ? a : -1.f;
}

void PolarUniverse::buildBoard(const Game& g) {
    // Cells whose piece is unknown (a stack inherited from another universe, a prefilled board): grouped into bodies
    // of ice by kind, at most four cells each (a piece's size), so they read as pieces rather than loose tiles.
    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const int t = g.cell(x, y).type;
            if (t < 0 || ice_[y][x].type == t) continue;
            const int id = pieceCounter_++;
            int qx[4], qy[4], n = 0, head = 0;
            ice_[y][x] = {id, t, -99.f, (float)x, y + 0.5f};
            qx[n] = x; qy[n] = y; n++;
            while (head < n && n < 4) {
                const int cx = qx[head], cy = qy[head];
                head++;
                const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
                for (int k = 0; k < 4 && n < 4; k++) {
                    const int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || nx >= Game::W || ny < 0 || ny >= Game::H) continue;
                    if (g.cell(nx, ny).type != t || ice_[ny][nx].type == t) continue;
                    ice_[ny][nx] = {id, t, -99.f, (float)x, y + 0.5f};
                    qx[n] = nx; qy[n] = ny; n++;
                }
            }
        }
    // Which piece a locked cell belongs to (-1: none).
    auto idAt = [&](int x, int y) {
        if (x < 0 || x >= Game::W || y < Game::HIDDEN || y >= Game::H) return -1;
        const int t = g.cell(x, y).type;
        const IceCell& ic = ice_[y][x];
        return t >= 0 && ic.type == t ? ic.id : -1;
    };
    // Locked cells: the cells of one piece are one body of ice.
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            const int id = idAt(x, y);
            // freezing: seconds since it locked (-1: ice long since); where it landed relative to the cell (board
            // units: the piece's own frame, its veils and cracks travel with it)
            const float age = freezeAge(g, x, y);
            float cdx = 0.f, cdy = 0.f;
            if (id >= 0) {
                cdx = ice_[y][x].cx - x;
                cdy = y - ice_[y][x].cy;
            }
            int mask = 0;
            if (id >= 0)
                mask = neighbourMask([&](int dx, int dy) {
                    if (idAt(x + dx, y + dy) != id) return false;
                    return dy == 0 || std::fabs(g.rowOffset(y + dy) - g.rowOffset(y)) < 0.01f;
                });
            tile(p.x, p.y, 0.5f, 0.5f, cur_.piece[c.type], 1.f, 0, age, mask, 0.f, id >= 0 ? pieceSeed(id) : 0.f, cdx, cdy);
        }
    // Cleared cells turning to snow; a topped-out stack freezing white, then crumbling.
    for (const Dying& d : dying_) {
        const float t = d.t - d.delay;
        if (d.over) {
            const float frost = smoothstepf(0.f, 0.7f, t);
            if (!d.crumbled) tile(d.x, d.y, 0.5f, 0.5f, cur_.piece[d.type], 1.f, 0, -1.f, d.mask, frost, d.pseed, d.cdx, d.cdy);
            else {
                const float k = 0.3f + saturate((t - 1.3f - d.seed * 0.5f) / 0.5f) * 0.7f;
                tile(d.x, d.y, 0.5f, 0.5f, cur_.piece[d.type], 1.f, 2, -1.f, packContact(d.mask, d.cdx, d.cdy), 0.f, 1.f, k,
                     d.seed * 7.f, d.pseed);
            }
            continue;
        }
        // shattering: the quad grows to hold the shards flying off (their motion is in the shader)
        const float reach = 0.6f + d.power * (2.4f * t + 7.f * t * t);
        tile(d.x, d.y - reach * 0.35f, 0.5f + reach * 0.8f, 0.5f + reach * 0.75f, cur_.piece[d.type], 1.f, 5, t,
             packContact(d.mask, d.cdx, d.cdy), d.power, d.x, d.y, d.ox, d.pseed);
    }
    // The falling piece and its ghost.
    if (g.hasPiece()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        auto maskIn = [&](int (*cs)[2], int i) {
            return neighbourMask([&](int dx, int dy) {
                for (int j = 0; j < 4; j++)
                    if (cs[j][0] == cs[i][0] + dx && cs[j][1] == cs[i][1] + dy) return true;
                return false;
            });
        };
        const vec3 col = cur_.piece[p.type];
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (int i = 0; i < 4; i++) {
                if (gc[i][1] < Game::HIDDEN) continue;
                const vec3 w = layout::cellPos((float)gc[i][0], (float)gc[i][1]);
                tile(w.x, w.y, 0.5f, 0.5f, col, 0.8f, 1, -1.f, maskIn(gc, i));
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (int i = 0; i < 4; i++) {
            if (cells[i][1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(cells[i][0] + dx, cells[i][1] + dy);
            tile(w.x, w.y, 0.5f, 0.5f, col, 1.f, 3, -1.f, maskIn(cells, i));
        }
    }
    // Previews.
    auto preview = [&](int type, vec2 center, float sc, float alpha) {
        if (type < 0) return;
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        g.pieceCells(p, cells);
        float minx = 9, maxx = -9, miny = 9, maxy = -9;
        for (auto& c : cells) {
            minx = std::min(minx, (float)c[0]); maxx = std::max(maxx, (float)c[0]);
            miny = std::min(miny, (float)c[1]); maxy = std::max(maxy, (float)c[1]);
        }
        for (int i = 0; i < 4; i++) {
            const int m = neighbourMask([&](int dx, int dy) {
                for (int j = 0; j < 4; j++)
                    if (cells[j][0] == cells[i][0] + dx && cells[j][1] == cells[i][1] + dy) return true;
                return false;
            });
            const vec2 pos = v2(center.x + (cells[i][0] - (minx + maxx) * 0.5f) * sc, center.y - (cells[i][1] - (miny + maxy) * 0.5f) * sc);
            tile(pos.x, pos.y, 0.5f * sc, 0.5f * sc, cur_.piece[type], alpha, 3, -1.f, m, 0.f, sc);
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.4f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }
    // The bonus gauge: a ring of frost, a snowflake at its heart.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        tile(gc.x, gc.y, R * 1.3f, R * 1.3f, vec3(1.f), 1.f, 4, -1.f, 0, 0.f, saturate(gaugeShown_),
             g.bonusReady() ? 1.f : 0.f, g.bonusActive() ? 1.f : 0.f, R);
    }
}

void PolarUniverse::buildFx() {
    backDots_.clear();
    dots_.clear();
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float world = 1.f - saturate(bridge_.amount * 1.6f);
    // the snowfall
    const vec3 flakeCol = lerp(cur_.snow, vec3(1.f), 0.4f) * 0.9f + aurLight_ * 1.5f;
    const float active = (0.22f + 0.38f * wm + 0.4f * wp + 0.3f * overDrift_) * FLAKES;
    auto inWell = [&](float x, float y) { return std::fabs(x) < WELL_X && std::fabs(y) < WELL_Y; };
    for (int i = 0; i < (int)flakes_.size(); i++) {
        const Flake& f = flakes_[i];
        const float on = saturate(active - i);
        if (on <= 0.f) break;
        const float x = viewC_.x + f.x * viewH_.x, y = viewC_.y + f.y * viewH_.y;
        const float r = (0.04f + 0.14f * f.depth * f.depth) * f.size;
        float a = (0.1f + 0.5f * f.depth) * on * world;
        const bool front = f.depth > 0.72f;
        if (front && inWell(x, y)) a *= 0.35f;
        std::vector<Dot>& out = front ? dots_ : backDots_;
        out.push_back({{x, y, r, 0.f}, {flakeCol.x, flakeCol.y, flakeCol.z, a}, {f.phase, 0.f, 1.f, 0.f}});
        // frozen in the air: some flakes catch the light
        if (slow_ > 0.3f && std::fmod(f.phase * 7.31f, 1.f) < 0.18f) {
            const float tw = std::pow(0.5f + 0.5f * std::sin(timeGlint_ * (1.2f + f.phase) + f.phase * 13.f), 3.f);
            const vec3 gc = lerp(cur_.frost, cur_.aurA, 0.25f) * 1.8f;
            out.push_back({{x, y, r * 4.f, 2.f}, {gc.x, gc.y, gc.z, a * tw * smoothstepf(0.3f, 0.9f, slow_)}, {0.f, 0.f, 1.f, 0.f}});
        }
    }
    // powder
    const vec3 powCol = lerp(cur_.snow, vec3(1.f), 0.45f) + aurLight_ * 1.2f;
    for (const Powder& p : powder_) {
        if (p.t < p.delay) continue;
        const float k = (p.t - p.delay) / p.life;
        if (p.puff) {
            const float r = p.size * (1.f + 1.4f * (1.f - (1.f - k) * (1.f - k)));
            const float a = p.alpha * smoothstepf(0.f, 0.12f, k) * (1.f - smoothstepf(0.3f, 1.f, k));
            dots_.push_back({{p.p.x, p.p.y, r, 1.f}, {powCol.x, powCol.y, powCol.z, a}, {p.seed * 11.f, k, 1.3f, 0.f}});
        } else {
            const float a = p.alpha * (1.f - smoothstepf(0.5f, 1.f, k));
            dots_.push_back({{p.p.x, p.p.y, p.size, 0.f}, {powCol.x, powCol.y, powCol.z, a}, {p.seed, k, 1.f, 0.f}});
        }
    }
    // glints
    const vec3 glintCol = lerp(cur_.frost, cur_.aurA, 0.3f) * 2.f;
    for (const Glint& g : glints_) {
        if (g.t < 0.f) continue;
        const float k = g.t / g.life;
        dots_.push_back({{g.p.x, g.p.y, g.size * (0.6f + 0.6f * std::sin(PI * k)), 2.f},
                         {glintCol.x, glintCol.y, glintCol.z, std::sin(PI * saturate(k))}, {0.f, 0.f, 1.f, 0.f}});
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float PolarUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
    if (!f.ok || s.empty()) return s.size() * size * 0.5f;
    const stbtt_fontinfo* info = (const stbtt_fontinfo*)f.info;
    const float k = size / f.px;
    float w = 0;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 32 || ch >= 128) ch = '?';
        w += f.g[ch - 32].adv * k + track;
        if (i + 1 < s.size()) {
            unsigned char n = (unsigned char)s[i + 1];
            if (n < 32 || n >= 128) n = '?';
            w += stbtt_GetCodepointKernAdvance(info, ch, n) * f.scaleTT * k;
        }
    }
    return w - track;
}

float PolarUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
                               std::vector<float>& out) const {
    if (!f.ok) return 0.f;
    const stbtt_fontinfo* info = (const stbtt_fontinfo*)f.info;
    const float k = size / f.px;
    const float baseline = y + f.capH * k;
    float pen = x;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 32 || ch >= 128) ch = '?';
        const Glyph& g = f.g[ch - 32];
        if (g.w > 0) {
            const float x0 = pen + g.ox * k, y0 = baseline + g.oy * k, x1 = x0 + g.w * k, y1 = y0 + g.h * k;
            const float v[6][4] = {{x0, y0, g.x0, g.y0}, {x1, y0, g.x1, g.y0}, {x1, y1, g.x1, g.y1},
                                   {x0, y0, g.x0, g.y0}, {x1, y1, g.x1, g.y1}, {x0, y1, g.x0, g.y1}};
            for (auto& q : v) out.insert(out.end(), q, q + 4);
        }
        pen += g.adv * k + track;
        if (i + 1 < s.size()) {
            unsigned char n = (unsigned char)s[i + 1];
            if (n < 32 || n >= 128) n = '?';
            pen += stbtt_GetCodepointKernAdvance(info, ch, n) * f.scaleTT * k;
        }
    }
    return pen - track - x;
}

void PolarUniverse::drawText(const Font& f, const std::vector<float>& q, const vec3& color, float alpha, float halo, float bias) {
    if (q.empty() || !f.ok) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    set3f(progText_, "uColor", color);
    set3f(progText_, "uHalo", cur_.halo);
    set1f(progText_, "uAlpha", alpha);
    set1f(progText_, "uHaloAmt", halo);
    set1f(progText_, "uBias", bias);
    set1f(progText_, "uBridge", smoothstepf(0.3f, 0.95f, bridge_.amount));
    set3f(progText_, "uBridgeInk", bridge_.ink);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(float), q.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 4));
}

namespace {
std::string upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}
} // namespace

// The HUD in POLAR NIGHT's typography: Josefin Sans, light and widely spaced capitals (a cold, airy, Nordic hand);
// icy white ink on a soft halo of night, the variant's name in the aurora's color.
void PolarUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    if (!panels.empty()) {
        std::vector<Vert> v;
        auto quad = [&](float x0, float y0, float x1, float y1, const vec3& c, float a) {
            const Vert q[6] = {{x0, y0, c.x, c.y, c.z, a}, {x1, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a},
                               {x0, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a}, {x0, y1, c.x, c.y, c.z, a}};
            v.insert(v.end(), q, q + 6);
        };
        for (const HudRect& r : panels) {
            const vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? cur_.aurA * 0.8f : cur_.halo;
            quad(r.x, r.y, r.x + r.w, r.y + r.h, c, r.accent || r.custom ? r.alpha : std::min(1.f, r.alpha * 1.1f));
        }
        glUseProgram(progShape_);
        set2f(progShape_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(shapeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vert), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
    }
    const vec3 ink = cur_.ink;
    const vec3 accent = lerp(cur_.ink, cur_.aurA, 0.6f);
    std::vector<float> q;
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        const float alpha = saturate(h.alpha);
        q.clear();
        auto place = [&](const Font& f, const std::string& text, float size, float track, float dy = 0.f) {
            const float w = textWidth(f, text, size, track);
            textQuads(f, text, ugl::hudLeft(h, w, width_), h.y + dy, size, track, q);
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            // "POLAR NIGHT" in light spaced capitals, the night's name after it in the aurora's color.
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            const float sz = base * 0.82f, tr = sz * 0.16f, vsz = base * 0.66f, vtr = vsz * 0.1f;
            const float wName = textWidth(fontLight_, name, sz, tr);
            const float wVar = var.empty() ? 0.f : textWidth(fontRegular_, var, vsz, vtr);
            const float gap = base * 0.6f;
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            textQuads(fontLight_, name, x, h.y, sz, tr, q);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            drawText(fontLight_, q, ink, a, 0.7f, 0.06f);
            if (!var.empty()) {
                q.clear();
                textQuads(fontRegular_, var, x + wName + gap, h.y + (sz - vsz) * 0.5f, vsz, vtr, q);
                drawText(fontRegular_, q, accent, a, 0.7f, 0.04f);
            }
            continue;
        }
        case HUD_SECTION:
            place(fontLight_, upper(h.text), base * 1.1f, base * 0.42f);
            drawText(fontLight_, q, ink, saturate(0.2f + h.alpha * 1.9f), 0.7f, 0.06f);
            continue;
        case HUD_TITLE:
            place(fontLight_, h.text, base * 1.05f, base * 0.05f, -base * 0.2f);
            drawText(fontLight_, q, ink, saturate(h.alpha * 1.3f), 0.7f, 0.07f);
            continue;
        case HUD_INFO:
            place(fontRegular_, upper(h.text), base * 0.68f, base * 0.25f);
            drawText(fontRegular_, q, ink, saturate(alpha * 1.2f) * 0.8f, 0.7f, 0.04f);
            continue;
        case HUD_LABEL:
            place(fontRegular_, upper(h.text), base * 0.7f, base * 0.38f);
            drawText(fontRegular_, q, ink, saturate(alpha * 1.2f) * 0.75f, 0.5f, 0.04f);
            continue;
        case HUD_VALUE:
            place(fontLight_, h.text, base * 1.18f, base * 0.08f);
            drawText(fontLight_, q, ink, alpha, 0.5f, 0.06f);
            continue;
        case HUD_NOTICE:
            place(fontLight_, upper(h.text), base * 1.0f, base * 0.25f);
            drawText(fontLight_, q, accent, alpha, 0.6f, 0.06f);
            continue;
        case HUD_KEY:
            place(fontRegular_, h.text, base * 0.85f, base * 0.1f);
            drawText(fontRegular_, q, ink, alpha, 0.6f, 0.04f);
            continue;
        case HUD_TOAST:
            place(fontLight_, upper(h.text), base * 1.3f, base * 0.5f, -base * 0.1f);
            drawText(fontLight_, q, ink, alpha, 0.8f, 0.07f);
            continue;
        case HUD_TOAST_SUB:
            place(fontRegular_, h.text, base * 0.85f, base * 0.25f);
            drawText(fontRegular_, q, accent, alpha * 0.95f, 0.6f, 0.04f);
            continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontLight_, upper(h.text), base * 1.15f, base * 0.4f, -base * 0.08f);
                drawText(fontLight_, q, ink, alpha, 0.8f, 0.07f);
            } else {
                place(fontRegular_, h.text, base * 0.85f, base * 0.12f);
                drawText(fontRegular_, q, h.accent ? accent : ink, alpha, 0.65f, 0.04f);
            }
            continue;
        default:
            place(fontRegular_, h.text, base * 0.82f, base * 0.1f);
            drawText(fontRegular_, q, h.accent ? accent : ink, alpha, 0.6f, 0.04f);
            continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void PolarUniverse::drawSky() {
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float world = 1.f - smoothstepf(0.1f, 0.85f, bridge_.amount);
    const float aspect = (float)width_ / height_;

    glBindFramebuffer(GL_FRAMEBUFFER, skyT_.fbo);
    glViewport(0, 0, skyT_.w, skyT_.h);
    glUseProgram(progSky_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, noiseTex_);
    set1i(progSky_, "uNoise", 0);
    set2f(progSky_, "uRes", (float)skyT_.w, (float)skyT_.h);
    set1f(progSky_, "uTanH", TAN_H);
    set1f(progSky_, "uAspect", aspect);
    set1f(progSky_, "uPitch", pitch_);
    set1f(progSky_, "uEye", EYE);
    set1f(progSky_, "uTime", worldTime_);
    set1f(progSky_, "uSeed", seedF_);
    set3f(progSky_, "uZenith", cur_.zenith);
    set3f(progSky_, "uHorizon", cur_.horizon);
    set3f(progSky_, "uGlow", cur_.glow);
    set3f(progSky_, "uSnow", cur_.snow);
    set3f(progSky_, "uShade", cur_.shade);
    set3f(progSky_, "uMount", cur_.mount);
    set3f(progSky_, "uMoonCol", cur_.moonCol);
    set1f(progSky_, "uGlowAz", cur_.glowAz);
    set1f(progSky_, "uGlowAmt", cur_.glowAmt);
    set1f(progSky_, "uMoon", cur_.moon);
    set1f(progSky_, "uMoonSize", cur_.moonSize);
    set1f(progSky_, "uHalo22", cur_.halo22);
    set1f(progSky_, "uStars", cur_.stars * (1.f - 0.35f * wp));
    set1f(progSky_, "uMilky", cur_.milky * (1.f - 0.6f * wm));
    set1f(progSky_, "uTrees", cur_.trees);
    set1f(progSky_, "uHaze", cur_.haze);
    set3f(progSky_, "uMoonDir", dirOf(cur_.moonAz, cur_.moonEl));
    set3f(progSky_, "uAurA", cur_.aurA);
    set3f(progSky_, "uAurB", cur_.aurB);
    set3f(progSky_, "uAurC", cur_.aurC);
    {
        float rib[24];
        Rng r(seed_ ^ 0xA0BBull);
        for (int k = 0; k < RIBBONS; k++) {
            const float zj = r.range(0.85f, 1.15f);
            const float ph = r.range(0.f, TAU);
            rib[k * 4 + 0] = RIB_Z[k] * zj;
            rib[k * 4 + 1] = ribbon_[k] * (0.3f + 1.1f * band_[k]) * world;
            rib[k * 4 + 2] = saturate(cur_.aurHeight * (0.3f + 0.35f * wm + 0.45f * wp) + 0.35f * band_[k] - 0.1f);
            rib[k * 4 + 3] = ph;
        }
        glUniform4fv(ugl::U(progSky_, "uRib"), 6, rib);
    }
    set1f(progSky_, "uAurTime", aurTime_);
    set1f(progSky_, "uAurGain", cur_.aurora * (1.f - 0.6f * overFade_));
    set1f(progSky_, "uSurge", surge_);
    set1f(progSky_, "uSurgeX", surgeX_);
    set1f(progSky_, "uStorm", storm_);
    set1f(progSky_, "uPulse", pulse_);
    set3f(progSky_, "uAurLight", aurLight_);
    set1f(progSky_, "uDrift", drift_);
    set1f(progSky_, "uSpin", saturate(0.15f * wm + 0.85f * gust_));
    set1f(progSky_, "uFreeze", smoothstepf(0.f, 1.f, slow_));
    glDisable(GL_BLEND);
    ugl::drawFullscreen();
    glBindTexture(GL_TEXTURE_2D, skyT_.tex);
    glGenerateMipmap(GL_TEXTURE_2D);
}

void PolarUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const Palette target = paletteOf(variant_);
        if (!palSet_) { pal_ = target; palSet_ = true; }
        else lerpPalette(pal_, target, 1.f - std::exp(-dt * 1.2f));
    }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 1.8f, dt);
    cur_ = pal_;
    if (slow_ > 0.001f) lerpPalette(cur_, frozenOf(pal_), smoothstepf(0.f, 1.f, slow_));
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.55f : 0.f), 3.f, dt);
    if (overT_ >= 0.f) {
        if (!game.over()) overT_ = -1.f;
        else overT_ += dt;
    }
    overDrift_ = approach(overDrift_, overT_ > 1.0f ? 1.f : 0.f, overT_ >= 0.f ? 0.5f : 2.f, dt);
    overFade_ = approach(overFade_, overT_ >= 0.f ? 1.f : 0.f, 0.8f, dt);
    worldPaused_ = f.paused;
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.1f, smoothstepf(0.f, 1.f, slow_));
    if (!f.paused) simulate(game, music, dt, wdt);
    timeGlint_ += dt;
    const float world = 1.f - saturate(bridge_.amount);
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    // The camera looks up a little more at the peaks: more sky.
    pitch_ = approach(pitch_, 0.15f + 0.03f * wm + 0.06f * wp, 0.5f, dt);

    // The aurora's light on the snow and the ice: its ribbons, nearer ones weighing more.
    {
        vec3 l(0.f);
        for (int k = 0; k < RIBBONS; k++) {
            const float b = ribbon_[k] * (0.3f + 1.1f * band_[k]) * (0.5f + 0.18f * k);
            const vec3 c = lerp(cur_.aurA, cur_.aurB, cur_.lightMix);
            l = l + lerp(c, vec3(ugl::luma(c)) * vec3(0.8f, 0.95f, 1.2f), 0.15f) * b;
        }
        l = l * (cur_.aurora * 0.035f * (1.f + surge_ * 0.4f + storm_ * 2.0f + pulse_ * 0.3f) * world * (1.f - 0.6f * overFade_));
        aurLight_ = l;
    }

    // ---- The world (reduced resolution).
    drawSky();

    // ---- Geometry.
    tiles_.clear();
    buildBoard(game);
    buildFx();

    // ---- The scene at full resolution.
    glBindFramebuffer(GL_FRAMEBUFFER, sceneT_.fbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    {
        glUseProgram(progComp_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, skyT_.tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        set1i(progComp_, "uSky", 0);
        set1i(progComp_, "uNoise", 1);
        set4f(progComp_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set1f(progComp_, "uPx", px_);
        set4f(progComp_, "uWell", -WELL_X, -WELL_Y, WELL_X, WELL_Y);
        set4f(progComp_, "uColL", layout::LEFT_COLUMN[0], layout::LEFT_COLUMN[1] - 2.9f, layout::LEFT_COLUMN[2], layout::LEFT_COLUMN[3]);
        set4f(progComp_, "uColR", layout::RIGHT_COLUMN[0], layout::RIGHT_COLUMN[1] - 1.3f, layout::RIGHT_COLUMN[2], layout::RIGHT_COLUMN[3]);
        set3f(progComp_, "uRim", lerp(cur_.frost, cur_.aurA, 0.35f) * (0.7f + 0.4f * beatGlow_ * wm) + aurLight_ * 2.f);
        set3f(progComp_, "uIce", lerp(cur_.zenith, cur_.shade, 0.3f) * 0.45f + vec3(ugl::luma(aurLight_)) * 0.15f);
        set3f(progComp_, "uFrostCol", lerp(cur_.frost, cur_.snow, 0.3f) * 0.8f + aurLight_);
        set3f(progComp_, "uAurLight", aurLight_);
        set3f(progComp_, "uSnow", lerp(cur_.snow, cur_.frost, 0.3f) * 0.75f);
        set1f(progComp_, "uTime", worldTime_);
        set1f(progComp_, "uTanH", TAN_H);
        set1f(progComp_, "uAspect", (float)width_ / height_);
        set1f(progComp_, "uPitch", pitch_);
        set1f(progComp_, "uGlitter", (0.5f + 0.5f * cur_.moon + 0.4f * wp) * world);
        set1f(progComp_, "uFrost", smoothstepf(0.f, 1.f, slow_) * world);
        set1f(progComp_, "uOver", overDrift_ * 0.75f);
        set1f(progComp_, "uBridge", saturate(bridge_.amount));
        set1f(progComp_, "uDrift", drift_);
        set1f(progComp_, "uCornerShade", 0.55f);
        set1f(progComp_, "uWellAlpha", 1.f);
        set3f(progComp_, "uBridgeCol", bridge_.color);
        set3f(progComp_, "uCornerCol", cur_.halo);
        set3f(progComp_, "uMoonCol", cur_.moonCol * (0.4f + 0.6f * cur_.moon));
        ugl::drawFullscreen();
        glActiveTexture(GL_TEXTURE0);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    auto drawDots = [&](const std::vector<Dot>& v) {
        if (v.empty()) return;
        glUseProgram(progDot_);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        set1i(progDot_, "uNoise", 0);
        setMat(progDot_, "uVP", vp_);
        set1f(progDot_, "uBridge", saturate(bridge_.amount));
        glBindVertexArray(dotVao_);
        glBindBuffer(GL_ARRAY_BUFFER, dotVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Dot), v.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)v.size());
    };
    drawDots(backDots_);
    if (!tiles_.empty()) {
        glUseProgram(progTile_);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        set1i(progTile_, "uNoise", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, skyT_.tex);
        glActiveTexture(GL_TEXTURE0);
        set1i(progTile_, "uSky", 1);
        set2f(progTile_, "uRes", (float)width_, (float)height_);
        setMat(progTile_, "uVP", vp_);
        set1f(progTile_, "uPx", px_);
        set3f(progTile_, "uAurLight", aurLight_ * 1.4f);
        set3f(progTile_, "uMoonLight", cur_.moonCol * cur_.moon);
        set3f(progTile_, "uFrostCol", lerp(cur_.frost, cur_.snow, 0.15f));
        set3f(progTile_, "uGaugeCol", lerp(cur_.frost, cur_.aurA, 0.5f));
        set3f(progTile_, "uGaugeTrack", cur_.ink * 0.6f);
        set3f(progTile_, "uShadow", cur_.halo);
        set1f(progTile_, "uBridge", saturate(bridge_.amount));
        set3f(progTile_, "uBridgeInk", bridge_.ink);
        set1f(progTile_, "uTime", timeGlint_);
        set1f(progTile_, "uBeat", beatGlow_ * wm);
        set1f(progTile_, "uGlint", beatGlow_ * smoothstepf(1.1f, 2.f, lv_) * (1.f - slow_));
        glBindVertexArray(tileVao_);
        glBindBuffer(GL_ARRAY_BUFFER, tileVbo_);
        glBufferData(GL_ARRAY_BUFFER, tiles_.size() * sizeof(Tile), tiles_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)tiles_.size());
    }
    drawDots(dots_);
    drawHud(f.hud, f.panels);
    glDisable(GL_BLEND);

    // ---- Bloom: the aurora's lower edge, the moon, the rims.
    glUseProgram(progDown_);
    set1i(progDown_, "uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    GLuint src = sceneT_.tex;
    int sw = width_, sh = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        bloomT_[i].bind();
        glBindTexture(GL_TEXTURE_2D, src);
        set2f(progDown_, "uTexel", 0.5f / sw, 0.5f / sh);
        set1f(progDown_, "uThreshold", i == 0 ? 0.6f : 0.f);
        ugl::drawFullscreen();
        src = bloomT_[i].tex;
        sw = bloomT_[i].w;
        sh = bloomT_[i].h;
    }
    glUseProgram(progUp_);
    set1i(progUp_, "uTex", 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = BLOOM_MIPS - 1; i > 0; i--) {
        bloomT_[i - 1].bind();
        glBindTexture(GL_TEXTURE_2D, bloomT_[i].tex);
        set2f(progUp_, "uTexel", 1.f / bloomT_[i].w, 1.f / bloomT_[i].h);
        ugl::drawFullscreen();
    }
    glDisable(GL_BLEND);

    // ---- Post.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progPost_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneT_.tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomT_[0].tex);
    glActiveTexture(GL_TEXTURE0);
    set1i(progPost_, "uScene", 0);
    set1i(progPost_, "uBloom", 1);
    set2f(progPost_, "uRes", (float)width_, (float)height_);
    set1f(progPost_, "uTime", (float)f.time);
    set1f(progPost_, "uFade", saturate(f.fade));
    set1f(progPost_, "uDim", dimT_);
    set1f(progPost_, "uBridge", saturate(bridge_.amount));
    set1f(progPost_, "uBloomAmt", 0.6f * world);
    set1f(progPost_, "uExposure", 1.0f + 0.15f * flare_);
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_polar() {
    static const UniverseEntry e{
        "POLAR NIGHT", "a snowfield under the stars, aurora ribbons", [] { return std::unique_ptr<Universe>(new PolarUniverse()); },
        // Spacious, nocturnal songs suit the polar night best (slow or mid tempo, darker, room for the highs);
        // everything can still be played under the aurora.
        [](const SongLook& l) {
            return 0.6f + 0.3f * (1.f - smoothstepf(115.f, 155.f, l.bpm)) + 0.25f * (1.f - l.brightness) + 0.2f * l.airWeight;
        },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
