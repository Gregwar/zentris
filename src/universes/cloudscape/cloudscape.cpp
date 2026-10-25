// CLOUDSCAPE: above an endless sea of clouds.
//
// One world: no ground, only sky and cloud. The camera floats above a sea of cumulus tops stretching to a hazy
// horizon; the light is soft, volumetric, with atmospheric perspective carrying the scale. The well is a floating
// pane of frosted glass (only the blur of the sky behind it, no frame), its two HUD columns smaller panes beside it;
// the pieces are flat, soft-colored rounded tiles with a whisper of gradient.
//
// The song drives the sky: calm is a still sea of clouds in haze (barely drifting, pastel, the light low), mid
// drifts the clouds with the tempo (a glide on each beat), flocks of birds cross the distance flapping on the beat
// and gentle beams fall from the sun, peak raises the camera while towering cumulus grow out of the sea on the
// horizon, god rays stream through their gaps and the kicks light them from inside (lightning). A lock breathes a
// soft puff of mist from under the piece; a cleared row dissolves into mist that drifts away on the wind; a Tetris
// parts the high veil around the sun and the light bursts through; slow motion is night above the clouds (the moon,
// stars, silver cloud tops); game over drops the stack through the sea of clouds while the mist rises over it.
// A change of universe closes the clouds in: the camera sinks into the sea and the fog takes the world.
//
// Cost: the sky and the clouds (a 2.5D heightfield of cloud tops ray-marched through a tileable noise texture, the
// towering cumulus as lit impostors on the horizon) are drawn at a reduced resolution (at most 540 lines), the god
// rays at a quarter of it; only the panes, the tiles and the text are drawn at full resolution.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane (like SUMI), so the
// well, the previews and the HUD anchors sit exactly where the layout puts them; the world behind has a camera of
// its own.
#include "universes/cloudscape/cloudscape.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/cloudscape/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// EB Garamond Italic (the titles, a light serif) and Lato (labels and numbers), SIL Open Font License, see
// OFL-*.txt beside this file.
const unsigned char FONT_SERIF[] = {
#include "universes/cloudscape/EBGaramond12-Italic.ttf.inc"
};
const unsigned char FONT_SANS[] = {
#include "universes/cloudscape/Lato-Regular.ttf.inc"
};

namespace shaders = cloudscape_shaders;
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
constexpr float WELL_X = 5.28f, WELL_Y = 10.28f; // the glass pane of the well (half extents, board)

vec2 v2(float x, float y) { return vec2{x, y}; }
vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
vec2 operator*(vec2 a, float k) { return {a.x * k, a.y * k}; }

vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }

// The variants: each a designed sky (sRGB here, linear once loaded). Colors: zenith, horizon, the glow around the
// sun, the sunlight, the far haze, cloud tops in the sun, in the shade, in the valleys, the glass's frost, the
// glass's rim, the HUD's ink and halo, two aurora colors, lightning; the seven glazes (I O T S Z J L). Numbers: the
// sun's azimuth and elevation (radians; azimuth 0 straight ahead behind the well, + to the right), its strength
// and size, the cloud coverage threshold, the high veil, cirrus, towering cumulus, their anvils, lightning, stars,
// aurora, haze density, and moon (1: the light is the moon).
struct VariantDef {
    const char* name;
    unsigned zenith, horizon, glow, sun, haze, lit, shade, deep, frost, rim, ink, halo, aurA, aurB, flash;
    unsigned piece[7];
    float sunAz, sunEl, sunGain, sunSize, cov, veil, cirrus, towers, anvil, lightning, stars, aurora, fog, moon;
};
const VariantDef VARIANTS[] = {
    {"DAWN", 0x8ea3d4, 0xf7cdb8, 0xffcf9a, 0xfff1dc, 0xe9c6c8, 0xfff4ea, 0xb2a3c6, 0x8a83ae, 0xf6eef3, 0xfff6ec,
     0x353a60, 0xfff6f2, 0, 0, 0xfff0e0, {0x41a6d8, 0xf0b63a, 0x9a6dd0, 0x52b986, 0xea6262, 0x4b6bd0, 0xf28c48},
     -0.5f, 0.045f, 1.0f, 0.03f, 0.2f, 0.12f, 0.35f, 0.6f, 0.f, 0.3f, 0.f, 0.f, 0.010f, 0.f},
    {"AZURE NOON", 0x2e6cc9, 0x9fcbef, 0xfff8e4, 0xfffaf0, 0xd3e6f6, 0xffffff, 0xa3bcd9, 0x7891bd, 0xf2f7fc, 0xffffff,
     0x1c3356, 0xf6faff, 0, 0, 0xf0f4ff, {0x1f9bd6, 0xf5b324, 0x8c55d4, 0x2fae74, 0xe5484f, 0x3557c9, 0xf07c2a},
     0.55f, 0.34f, 1.1f, 0.026f, 0.18f, 0.f, 0.5f, 0.8f, 0.f, 0.2f, 0.f, 0.f, 0.008f, 0.f},
    {"GOLDEN HOUR", 0x6f88b8, 0xffd08e, 0xffbf68, 0xffdca6, 0xf0c494, 0xffe2b4, 0xa98f9e, 0x7a6a88, 0xfbf0e2, 0xfff0d6,
     0x46304a, 0xfff6e8, 0, 0, 0xfff0d0, {0x3aa0c8, 0xf2c02c, 0x9c5cc4, 0x52ad78, 0xe2574c, 0x4866c4, 0xef8634},
     0.48f, 0.1f, 1.15f, 0.032f, 0.22f, 0.08f, 0.45f, 0.7f, 0.f, 0.3f, 0.f, 0.f, 0.010f, 0.f},
    {"STORM VIOLET", 0x221c38, 0x7d6e98, 0xcdb3e6, 0xe2d6ff, 0x6c5f88, 0xc8bce0, 0x4f466c, 0x2f2a4a, 0x8a80a8, 0xe8e0ff,
     0xf4f0ff, 0x1e1a30, 0, 0, 0xc8d8ff, {0x5cc8f0, 0xf6d25a, 0xc08cff, 0x6fe0a8, 0xff7a8a, 0x7a90ff, 0xffa860},
     -0.55f, 0.16f, 0.75f, 0.025f, 0.28f, 0.55f, 0.f, 1.0f, 1.f, 1.f, 0.f, 0.f, 0.012f, 0.f},
    {"POLAR NIGHT", 0x040a1c, 0x1a2c4c, 0x8aa0c8, 0xb8c8ea, 0x203454, 0xc6d4ee, 0x2a3a5a, 0x131e36, 0x3a4c70, 0xd8e6ff,
     0xeef4ff, 0x0a1226, 0x3ee6a4, 0x9a6cf0, 0xd0e0ff, {0x5ad8f0, 0xf2dc6a, 0xb48cff, 0x5ef0a8, 0xff7090, 0x7c9cff, 0xffaa66},
     0.45f, 0.22f, 0.55f, 0.022f, 0.2f, 0.f, 0.f, 0.3f, 0.f, 0.f, 0.6f, 0.6f, 0.012f, 1.f},
    {"ROSE SUNSET", 0x4b4a8c, 0xff9e8c, 0xffb07a, 0xffc896, 0xe2909c, 0xffbfa6, 0x8a6a9c, 0x5c4a7c, 0xf6dce0, 0xffe6d8,
     0x3a2446, 0xffeee8, 0, 0, 0xffe0d0, {0x3c9fd0, 0xf3bb3a, 0x8f5ccc, 0x4fb486, 0xe05a6a, 0x4762c4, 0xf28a48},
     -0.45f, 0.015f, 1.1f, 0.036f, 0.22f, 0.15f, 0.5f, 0.6f, 0.f, 0.3f, 0.f, 0.f, 0.011f, 0.f},
    {"MONSOON", 0x4d6460, 0xbccab4, 0xe6eed2, 0xf2f4e4, 0xa8b8a4, 0xe6ecdc, 0x7c8c84, 0x5a6a64, 0xe6ece2, 0xf6faee,
     0x20302b, 0xf4f8f0, 0, 0, 0xe8f0ff, {0x2f93c0, 0xe8b02a, 0x8a58c0, 0x3ea070, 0xd94848, 0x3c5cb8, 0xe07a30},
     0.5f, 0.25f, 0.65f, 0.026f, 0.26f, 0.5f, 0.f, 1.0f, 0.4f, 0.6f, 0.f, 0.f, 0.014f, 0.f},
    {"BLUE HOUR", 0x13285a, 0xe6a88a, 0xf0a888, 0xffc8a0, 0x5a6c9c, 0xa8b8e0, 0x3c4e80, 0x24325c, 0x485a88, 0xffe4d0,
     0xf2f0ff, 0x101a3a, 0, 0, 0xfff0e0, {0x5cc4ec, 0xf4cf5c, 0xb88cf0, 0x66d6a0, 0xf47880, 0x7a94f4, 0xf8a460},
     -0.5f, -0.012f, 0.8f, 0.03f, 0.2f, 0.08f, 0.45f, 0.5f, 0.f, 0.3f, 0.35f, 0.f, 0.010f, 0.f},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));
constexpr int POLAR = 4;

using Palette = CloudscapeUniverse::Palette;
static_assert(sizeof(Palette) % sizeof(float) == 0, "a palette is floats only");
constexpr int PAL_FLOATS = (int)(sizeof(Palette) / sizeof(float));

Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    auto L = [](unsigned c) { return ugl::toLinear(hex(c)); };
    Palette p;
    p.zenith = L(d.zenith); p.horizon = L(d.horizon); p.glow = L(d.glow); p.sun = L(d.sun); p.haze = L(d.haze);
    p.lit = L(d.lit); p.shade = L(d.shade); p.deep = L(d.deep); p.frost = L(d.frost); p.rim = L(d.rim);
    p.ink = L(d.ink); p.halo = L(d.halo); p.aurA = L(d.aurA); p.aurB = L(d.aurB); p.flash = L(d.flash);
    for (int i = 0; i < 7; i++) p.piece[i] = L(d.piece[i]);
    p.sunAz = d.sunAz; p.sunEl = d.sunEl; p.sunGain = d.sunGain; p.sunSize = d.sunSize; p.cov = d.cov; p.veil = d.veil;
    p.cirrus = d.cirrus; p.towers = d.towers; p.anvil = d.anvil; p.lightning = d.lightning; p.stars = d.stars;
    p.aurora = d.aurora; p.fog = d.fog; p.moon = d.moon;
    return p;
}

// Night above the clouds (slow motion): the moon up on the right, stars, silver cloud tops. The glazes and the
// aurora stay the variant's.
Palette nightOf(const Palette& d) {
    auto L = [](unsigned c) { return ugl::toLinear(hex(c)); };
    Palette p = d;
    p.zenith = L(0x040a1e); p.horizon = L(0x1a2a4e); p.glow = L(0x7d92c0); p.sun = L(0xb4c6ea); p.haze = L(0x203256);
    p.lit = L(0xc4d2ee); p.shade = L(0x2c3c62); p.deep = L(0x142040); p.frost = L(0x33456c); p.rim = L(0xdae6ff);
    p.ink = L(0xeef4ff); p.halo = L(0x0a1226); p.flash = L(0xd0e0ff);
    p.sunAz = 0.47f; p.sunEl = 0.22f; p.sunGain = 0.55f; p.sunSize = 0.024f; p.moon = 1.f;
    p.stars = 1.f; p.veil *= 0.3f; p.cirrus *= 0.3f; p.lightning *= 0.5f;
    p.aurora = d.aurora > 0.f ? 1.4f : 0.f;
    return p;
}

void lerpPalette(Palette& a, const Palette& b, float k) {
    float* pa = reinterpret_cast<float*>(&a);
    const float* pb = reinterpret_cast<const float*>(&b);
    for (int i = 0; i < PAL_FLOATS; i++) pa[i] += (pb[i] - pa[i]) * k;
}

vec3 sunDirOf(float az, float el) {
    return {std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el)};
}

std::string titleCase(const std::string& s) {
    std::string o = s;
    bool start = true;
    for (char& c : o) {
        if (std::isalpha((unsigned char)c)) {
            c = start ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c);
            start = false;
        } else start = c == ' ' || c == '-';
    }
    return o;
}

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool CloudscapeUniverse::init(int width, int height) {
    progCloud_ = compile(ugl::FULLSCREEN_VS, shaders::CLOUD_FS, "cloudscape clouds");
    progRays_ = compile(ugl::FULLSCREEN_VS, shaders::RAYS_FS, "cloudscape rays");
    progComp_ = compile(ugl::FULLSCREEN_VS, shaders::COMPOSITE_FS, "cloudscape composite");
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "cloudscape shape");
    progTile_ = compile(shaders::TILE_VS, shaders::TILE_FS, "cloudscape tile");
    progMist_ = compile(shaders::MIST_VS, shaders::MIST_FS, "cloudscape mist");
    progLine_ = compile(shaders::LINE_VS, shaders::LINE_FS, "cloudscape line");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "cloudscape text");
    progDown_ = compile(ugl::FULLSCREEN_VS, shaders::DOWN_FS, "cloudscape down");
    progUp_ = compile(ugl::FULLSCREEN_VS, shaders::UP_FS, "cloudscape up");
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "cloudscape post");

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
    instanced(mistVao_, mistVbo_, 3, sizeof(Mist));
    instanced(lineVao_, lineVbo_, 3, sizeof(Line));

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
    buildFont(fontSerif_, FONT_SERIF);
    buildFont(fontSans_, FONT_SANS);
    pal_ = cur_ = paletteOf(0);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// A tileable 256x256 noise texture: r and b gradient noise (two scales), g and a heightfields of round puffs (domes
// on a jittered grid, two scales): the clouds are made of these.
void CloudscapeUniverse::buildNoise() {
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

// Glyphs baked as signed distance fields: they scale cleanly, and the distance gives the halo of mist.
void CloudscapeUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] cloudscape font init failed\n");
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

void CloudscapeUniverse::resize(int width, int height) {
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
}

void CloudscapeUniverse::createTargets() {
    // The clouds at most 540 lines (half resolution at 1080p), the rays at a quarter of that.
    const int ch = std::max(90, std::min(540, height_ / 2));
    const int cw = std::max(160, (int)std::lround((float)ch * width_ / height_));
    cloudT_.create(cw, ch, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    glBindTexture(GL_TEXTURE_2D, cloudT_.tex);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    raysT_.create(std::max(1, cw / 2), std::max(1, ch / 2), GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    sceneT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    int w = width_, h = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        bloomT_[i].create(w, h, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

CloudscapeUniverse::~CloudscapeUniverse() {
    cloudT_.destroy();
    raysT_.destroy();
    sceneT_.destroy();
    for (auto& t : bloomT_) t.destroy();
    for (GLuint p : {progCloud_, progRays_, progComp_, progShape_, progTile_, progMist_, progLine_, progText_, progDown_, progUp_, progPost_})
        glDeleteProgram(p);
    for (GLuint v : {tileVao_, mistVao_, lineVao_, shapeVao_, textVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, tileVbo_, mistVbo_, lineVbo_, shapeVbo_, textVbo_}) glDeleteBuffers(1, &b);
    if (noiseTex_) glDeleteTextures(1, &noiseTex_);
    for (Font* f : {&fontSerif_, &fontSans_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// =====================================================================================================
// Song, level, bonus
// =====================================================================================================

// The sky from the song: night songs (minor and dark) fly at night or into the storm, bright major ones into the
// day; the key's hue picks among them.
int CloudscapeUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    const bool night = t.hasSong && t.minor && t.brightness < 0.45f;
    const bool bright = t.hasSong && !t.minor && t.brightness > 0.55f;
    int v;
    if (night) v = t.dynamics > 0.6f ? 3 : (h < 0.5f ? 4 : 7);
    else if (bright) v = h < 0.33f ? 1 : (h < 0.66f ? 2 : 0);
    else if (h < 0.14f) v = 5;
    else if (h < 0.28f) v = 2;
    else if (h < 0.42f) v = 6;
    else if (h < 0.56f) v = 1;
    else if (h < 0.7f) v = 7;
    else if (h < 0.84f) v = 0;
    else v = t.minor ? 3 : 5;
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void CloudscapeUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    seedF_ = (float)(look.seed % 1000) * 0.137f;
    rng_ = Rng(look.seed ^ 0xC10Dull);
    drift_ = v2(rng_.range(0.f, 300.f), rng_.range(0.f, 300.f));
    if (seconds <= 0.f) palSet_ = false; // snap on the first frame
    else rayPulse_ = std::max(rayPulse_, 0.6f);
}

void CloudscapeUniverse::setLevel(int level, float energy, float seconds, float impact) {
    level = std::clamp(level, 0, 2);
    energy_ = energy;
    if (!hasLevel_ || seconds < 0.1f) {
        hasLevel_ = true;
        level_ = level;
        lv_ = lvFrom_ = lvTo_ = (float)level;
        lvT_ = 1.f;
        const float wp = smoothstepf(1.1f, 2.f, lv_), wm = smoothstepf(0.2f, 1.f, lv_);
        for (int i = 0; i < 6; i++) grow_[i] = std::min(1.f, wp * (1.f - 0.12f * i) + wm * 0.2f);
        birds_ = wm * (1.f - 0.5f * wp);
        return;
    }
    const bool rising = level > level_;
    level_ = level;
    lvFrom_ = lv_;
    lvTo_ = (float)level;
    lvT_ = 0.f;
    lvDur_ = std::clamp(seconds, rising ? 1.0f : 2.0f, 6.f);
    if (rising) rayPulse_ = std::max(rayPulse_, 0.4f + 0.8f * impact);
}

void CloudscapeUniverse::refresh(float strength) { rayPulse_ = std::max(rayPulse_, 0.5f * strength); }

void CloudscapeUniverse::setBonus(bool on) { slowOn_ = on; }

void CloudscapeUniverse::levelUp() {
    rayPulse_ = std::max(rayPulse_, 0.9f);
    for (int i = 0; i < 3; i++)
        glints_.push_back({v2((i == 1 ? 1.f : -1.f) * WELL_X, WELL_Y - (i == 2 ? 6.f : 0.f)), -0.12f * i, 0.8f, 1.1f});
}

vec2 CloudscapeUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints CloudscapeUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    h.background = lerp(p.horizon, p.zenith, 0.45f);
    h.light = ugl::luma(h.background) > 0.28f;
    h.ink = h.light ? lerp(p.ink, p.deep, 0.3f) : lerp(p.lit, p.ink, 0.5f);
    h.accent = p.sun * p.sunGain;
    h.styles = TS_INK | TS_CURTAIN | TS_LUMA; // mist dissolving, clouds parting, the sun last
    return h;
}

// Where the light is on screen (uv, bottom-left origin), through the world camera.
vec2 CloudscapeUniverse::sunUv() const {
    const vec3 d = sunDirOf(cur_.sunAz, cur_.sunEl);
    const float cp = std::cos(pitch_), sp = std::sin(pitch_);
    const vec3 fwd(0.f, -sp, -cp), up(0.f, cp, -sp);
    const float z = std::max(dot(d, fwd), 0.05f);
    const float aspect = (float)width_ / height_;
    const float nx = d.x / z / (TAN_H * aspect), ny = dot(d, up) / z / TAN_H;
    return v2(nx * 0.5f + 0.5f, ny * 0.5f + 0.5f);
}

void CloudscapeUniverse::puff(vec2 p, vec2 v, float r, float grow, float life, float alpha, float delay, float stretch) {
    Puff q;
    q.p = p; q.v = v; q.r = r; q.grow = grow; q.t = 0.f; q.life = life; q.alpha = alpha; q.delay = delay;
    q.stretch = stretch;
    q.seed = rng_.uniform();
    puffs_.push_back(q);
}

// =====================================================================================================
// Game events
// =====================================================================================================

void CloudscapeUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    // Locks and hard drops: no mist (only the tile's own breath of light, in the tile shader).
    case GameEvent::Clear: {
        const bool tetris = ev.count >= 4;
        const float wind = ev.x < 4.5f ? 1.f : -1.f; // the mist drifts away from where the piece landed
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0.f; d.type = std::clamp(c.type, 0, 6); d.fall = false;
            d.delay = std::fabs(c.x - 4.5f) * 0.022f;
            d.vx = wind * rng_.range(0.6f, 1.2f); d.vy = rng_.range(0.3f, 0.7f); d.rot = 0.f; d.vr = 0.f;
            d.seed = rng_.uniform();
            dying_.push_back(d);
        }
        if (tetris) {
            tetrisT_ = 0.f;
            partT_ = 0.f;
            rayPulse_ = std::max(rayPulse_, 1.6f);
            for (int i = 0; i < 4; i++)
                glints_.push_back({v2(rng_.range(-WELL_X, WELL_X), WELL_Y), -0.1f * i, 0.9f, 1.3f});
        } else {
            rayPulse_ = std::max(rayPulse_, 0.2f * ev.count);
        }
        break;
    }
    case GameEvent::BonusReady:
        glints_.push_back({v2(layout::GAUGE_CENTER.x + 0.6f, layout::GAUGE_CENTER.y + 0.6f), 0.f, 0.9f, 1.1f});
        break;
    case GameEvent::TopOut: {
        // The floor gives way: the stack falls through the sea of clouds.
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0.f; d.type = std::clamp(c.type, 0, 6); d.fall = true;
            d.delay = (Game::H - 1 - c.y) * 0.035f + rng_.range(0.f, 0.08f);
            d.vx = rng_.range(-0.5f, 0.5f); d.vy = rng_.range(0.f, 1.5f);
            d.rot = 0.f; d.vr = rng_.range(-1.2f, 1.2f);
            d.seed = rng_.uniform();
            dying_.push_back(d);
        }
        for (int i = 0; i < 14; i++)
            puff(v2(rng_.range(-5.f, 5.f), -10.2f + rng_.range(-0.3f, 0.6f)), v2(rng_.range(-1.f, 1.f), rng_.range(0.1f, 0.6f)),
                 0.8f, 2.2f, 2.4f, 0.55f, rng_.range(0.f, 0.4f));
        overT_ = 0.f;
        break;
    }
    default: break;
    }
}

// =====================================================================================================
// Simulation
// =====================================================================================================

void CloudscapeUniverse::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    worldTime_ += wdt;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);

    // The flight: the clouds drift with the tempo, a glide on each beat (mid, peak), still when calm.
    {
        const float ph = music.beatPhase;
        if (ph < lastBeatPhase_ - 0.5f) beats_ += 1.f;
        lastBeatPhase_ = ph;
        const float e = 1.f - std::pow(1.f - ph, 2.2f);
        const float eased = beats_ + lerpf(ph, e, wm * 0.8f);
        float d = lastEased_ < 0.f ? 0.f : eased - lastEased_;
        if (d < 0.f || d > 2.f) d = 0.f;
        lastEased_ = eased;
        const float perBeat = 0.12f + 0.38f * wm + 0.55f * wp;  // world units per beat
        const float speed = music.hasSong ? d * perBeat * (wdt / std::max(dt, 1e-4f)) : wdt * 0.25f;
        const float calm = (1.f - wm) * wdt * 0.12f;
        drift_.y += speed + calm;
        drift_.x += (speed + calm) * 0.22f;
        hiDrift_.y += (speed + calm) * 2.5f;
        hiDrift_.x += (speed + calm) * 1.2f + wdt * 0.3f;
    }
    // The camera rises at the peaks.
    {
        const float eyeT = 3.1f + 0.4f * wm + 1.8f * wp + 0.06f * std::sin(worldTime_ * 0.21f);
        eye_ = approach(eye_, eyeT, 0.8f, dt);
        pitch_ = approach(pitch_, 0.07f + 0.04f * wp, 0.8f, dt);
    }
    beatGlow_ = approach(beatGlow_, music.hasSong ? saturate(music.beatPulse) : 0.f, 12.f, dt);

    // Towering cumulus grow out of the sea at the peaks (some skies have them earlier).
    {
        const float count = cur_.towers * 6.f;
        for (int i = 0; i < 6; i++) {
            const float avail = saturate(count - i);
            float target = wp * (1.f - 0.1f * i) + wm * (cur_.towers > 0.9f ? 0.35f : 0.12f) * (i < 3 ? 1.f : 0.f);
            target = std::min(1.f, target) * avail;
            const float rate = target > grow_[i] ? 0.22f : 0.12f;
            grow_[i] = approach(grow_[i], target, rate, wdt);
        }
    }
    // Kicks: lightning inside the tall clouds (peak), the light pulses.
    sinceKick_ += dt;
    sinceFlash_ += dt;
    const float kick = std::max(music.kick, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    if (kick > 0.55f && prevKick_ <= 0.55f && sinceKick_ > 0.18f) {
        sinceKick_ = 0.f;
        rayPulse_ = std::max(rayPulse_, 0.35f * wp + 0.1f * wm);
        if (wp > 0.3f && sinceFlash_ > 0.3f && rng_.chance(std::min(1.f, cur_.lightning * (0.5f + 0.7f * wp)))) {
            int best = -1;
            float bestG = 0.35f;
            for (int k = 0; k < 6; k++) {
                const int i = (int)((rng_.next() >> 20) % 6);
                if (grow_[i] > bestG) { best = i; bestG = grow_[i]; break; }
            }
            if (best >= 0) {
                flashT_[best] = 0.f;
                sinceFlash_ = 0.f;
            }
        }
    }
    prevKick_ = kick;
    for (int i = 0; i < 6; i++) {
        flashT_[i] += dt;
        const float t = flashT_[i];
        // a flicker: a strike, a second one, then a fading glow
        flash_[i] = t > 1.2f ? 0.f : (std::exp(-t * 6.f) + 0.6f * std::exp(-std::fabs(t - 0.16f) * 30.f)) * (0.7f + 0.3f * std::sin(t * 70.f));
    }
    rayPulse_ = approach(rayPulse_, 0.f, 1.4f, dt);

    // Tetris: the veil parts from the sun outward, the light floods, then the sky slowly closes again.
    tetrisT_ += dt;
    partT_ += dt;
    part_ = partT_ < 0.9f ? smoothstepf(0.f, 0.9f, partT_) : 1.f - smoothstepf(3.2f, 6.f, partT_);
    flare_ = tetrisT_ < 3.f ? (tetrisT_ < 0.25f ? tetrisT_ / 0.25f : 1.f) * (1.f - smoothstepf(0.4f, 3.f, tetrisT_)) : 0.f;
    flood_ = flare_ * 0.9f;

    // Birds cross the distance at mid, flapping on the beat.
    birds_ = approach(birds_, wm * (1.f - 0.55f * wp), 0.5f, dt);
    flockTimer_ -= wdt;
    if (flockTimer_ <= 0.f && birds_ > 0.25f && flocks_.size() < 3) {
        Flock f;
        f.dir = rng_.chance(0.5f) ? 1.f : -1.f;
        f.x = -f.dir * (viewH_.x + 2.f);
        const float hor = viewC_.y + std::tan(pitch_) / TAN_H * viewH_.y;
        f.y = hor + rng_.range(1.5f, 7.5f);
        f.speed = rng_.range(1.1f, 1.8f);
        f.t = 0.f;
        const int n = rng_.irange(4, 9);
        const float size = rng_.range(0.16f, 0.26f);
        for (int k = 0; k < n; k++) {
            const float side = (k & 1) ? 1.f : -1.f, rank = (float)((k + 1) / 2);
            f.birds.push_back({-f.dir * rank * 0.55f + rng_.range(-0.12f, 0.12f), side * rank * 0.32f + rng_.range(-0.1f, 0.1f),
                               rng_.range(0.f, 0.35f), size * rng_.range(0.85f, 1.15f)});
        }
        flocks_.push_back(std::move(f));
        flockTimer_ = rng_.range(5.f, 11.f);
    }
    for (Flock& f : flocks_) {
        f.t += wdt;
        f.x += f.dir * f.speed * wdt;
        f.y += 0.08f * std::sin(f.t * 0.7f) * wdt;
    }
    flocks_.erase(std::remove_if(flocks_.begin(), flocks_.end(), [&](const Flock& f) { return std::fabs(f.x) > viewH_.x + 5.f; }), flocks_.end());

    // Effects age (slowed by the slow motion like the world).
    for (auto& p : puffs_) {
        p.t += wdt;
        if (p.t < p.delay) continue;
        p.p = p.p + p.v * wdt;
        p.v = p.v * std::exp(-0.6f * wdt);
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(), [](const Puff& p) { return p.t > p.delay + p.life; }), puffs_.end());
    for (auto& d : dying_) d.t += d.fall ? dt : wdt;
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return d.t > d.delay + (d.fall ? 2.4f : 0.45f); }), dying_.end());
    for (auto& g : glints_) g.t += dt;
    glints_.erase(std::remove_if(glints_.begin(), glints_.end(), [](const Glint& g) { return g.t > g.life; }), glints_.end());

    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
}

// =====================================================================================================
// Geometry
// =====================================================================================================

void CloudscapeUniverse::tile(float x, float y, float hw, float hh, const vec3& c, float alpha, int kind, float age,
                              float angle, float fog, float e0, float e1, float e2, float e3) {
    tiles_.push_back({{x, y, hw, hh}, {c.x, c.y, c.z, alpha}, {(float)kind, age, angle, fog}, {e0, e1, e2, e3}});
}

void CloudscapeUniverse::line(std::vector<Line>& out, vec2 a, vec2 b, const vec3& c, float width, float glow, int kind, float alpha) {
    if (alpha <= 0.003f) return;
    out.push_back({{a.x, a.y, b.x, b.y}, {c.x, c.y, c.z, 0.f}, {width, glow, (float)kind, alpha}});
}

void CloudscapeUniverse::buildBirds() {
    if (flocks_.empty()) return;
    const float beat = beats_ + lastBeatPhase_;
    const vec3 col = lerp(lerp(cur_.deep, cur_.shade, 0.3f), cur_.haze, 0.25f);
    auto inPane = [&](float x, float y) {
        auto in = [&](float x0, float y0, float x1, float y1) {
            return x > x0 - 0.4f && x < x1 + 0.4f && y > y0 - 0.4f && y < y1 + 0.4f;
        };
        return in(-WELL_X, -WELL_Y, WELL_X, WELL_Y) || in(layout::LEFT_COLUMN[0], layout::LEFT_COLUMN[1] - 2.9f, layout::LEFT_COLUMN[2], layout::LEFT_COLUMN[3]) ||
               in(layout::RIGHT_COLUMN[0], layout::RIGHT_COLUMN[1] - 1.3f, layout::RIGHT_COLUMN[2], layout::RIGHT_COLUMN[3]);
    };
    const float a = saturate(birds_ * 1.3f) * (1.f - saturate(bridge_.amount * 2.f));
    for (const Flock& f : flocks_) {
        for (const Bird& b : f.birds) {
            const float x = f.x + b.x, y = f.y + b.y;
            if (inPane(x, y)) continue;
            const float flap = std::sin(TAU * (beat + b.phase));
            const float s = b.size;
            const vec2 c = v2(x, y);
            for (int side = -1; side <= 1; side += 2) {
                const vec2 elbow = c + v2(side * 0.5f * s, 0.18f * s + 0.25f * s * flap);
                const vec2 tip = c + v2(side * 1.0f * s, -0.05f * s + 0.55f * s * flap);
                line(lines_, c, elbow, col, 0.06f * s + 0.012f, 0.f, 2, a * 0.85f);
                line(lines_, elbow, tip, col, 0.04f * s + 0.01f, 0.f, 2, a * 0.85f);
            }
        }
    }
}

void CloudscapeUniverse::buildBoard(const Game& g) {
    // Locked cells.
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            const float age = c.flash > 0.f ? (1.f - c.flash) * Game::LOCK_FX_SECONDS : -1.f;
            tile(p.x, p.y, 0.5f, 0.5f, cur_.piece[c.type], 1.f, 0, age);
        }
    // Cleared cells dissolving into mist; a topped-out stack falling through the clouds.
    for (const Dying& d : dying_) {
        const float t = d.t - d.delay;
        if (d.fall) {
            if (t < 0.f) {
                tile(d.x, d.y, 0.5f, 0.5f, cur_.piece[d.type], 1.f, 0);
                continue;
            }
            const float y = d.y + d.vy * t - 0.5f * 16.f * t * t;
            const float x = d.x + d.vx * t;
            const float s = 1.f - 0.45f * smoothstepf(0.f, 1.8f, t);
            const float fog = smoothstepf(0.3f, 1.6f, t);
            const float a = 1.f - smoothstepf(1.5f, 2.3f, t);
            if (y < viewC_.y - viewH_.y - 2.f) continue;
            tile(x, y, 0.5f * s, 0.5f * s, cur_.piece[d.type], a, 0, -1.f, d.vr * t, fog, s);
            continue;
        }
        if (t < 0.f) {
            tile(d.x, d.y, 0.5f, 0.5f, cur_.piece[d.type], 1.f, 0);
            continue;
        }
        // a cleared tile catches the light and pops: it brightens to the sun's color, swells a little and is gone,
        // from the middle of the row outward
        const float k = saturate(t / 0.4f);
        const float s = 1.f + 0.18f * smoothstepf(0.f, 1.f, k);
        const vec3 c = lerp(cur_.piece[d.type], lerp(cur_.sun, vec3(1.f), 0.5f) * 1.2f, smoothstepf(0.f, 0.35f, k));
        tile(d.x, d.y, 0.5f * s, 0.5f * s, c, 1.f - smoothstepf(0.35f, 1.f, k), 0, -1.f, 0.f, 0.f, s);
    }
    // The falling piece and its ghost.
    if (g.hasPiece()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        const vec3 col = cur_.piece[p.type];
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (auto& c : gc) {
                if (c[1] < Game::HIDDEN) continue;
                const vec3 w = layout::cellPos((float)c[0], (float)c[1]);
                tile(w.x, w.y, 0.5f, 0.5f, col, 0.8f, 1);
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (auto& c : cells) {
            if (c[1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(c[0] + dx, c[1] + dy);
            tile(w.x, w.y, 0.5f, 0.5f, col, 1.f, 3);
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
        for (auto& c : cells) {
            const vec2 pos = v2(center.x + (c[0] - (minx + maxx) * 0.5f) * sc, center.y - (c[1] - (miny + maxy) * 0.5f) * sc);
            tile(pos.x, pos.y, 0.5f * sc, 0.5f * sc, cur_.piece[type], alpha, 3, -1.f, 0.f, 0.f, sc);
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.4f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }
    // The bonus gauge: a halo filling around a small sun.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        tile(gc.x, gc.y, R * 1.3f, R * 1.3f, vec3(1.f), 1.f, 4, -1.f, 0.f, 0.f, saturate(gaugeShown_),
             g.bonusReady() ? 1.f : 0.f, g.bonusActive() ? 1.f : 0.f, R);
    }
}

void CloudscapeUniverse::buildFx() {
    mists_.clear();
    const vec3 mistCol = lerp(cur_.lit * cur_.sun * (0.75f + 0.4f * cur_.sunGain), vec3(1.f), 0.3f);
    for (const Puff& p : puffs_) {
        if (p.t < p.delay) continue;
        const float k = (p.t - p.delay) / p.life;
        const float r = p.r * (1.f + p.grow * (1.f - (1.f - k) * (1.f - k)));
        const float a = p.alpha * smoothstepf(0.f, 0.12f, k) * (1.f - smoothstepf(0.35f, 1.f, k));
        mists_.push_back({{p.p.x, p.p.y, r, p.stretch}, {mistCol.x, mistCol.y, mistCol.z, a}, {p.seed * 11.f, k, 0.f, 0.f}});
    }
    const vec3 glintCol = lerp(cur_.sun, vec3(1.f), 0.5f) * 2.2f;
    for (const Glint& g : glints_) {
        if (g.t < 0.f) continue;
        const float k = g.t / g.life;
        line(fxLines_, g.p, g.p, glintCol, g.size * (0.6f + 0.6f * std::sin(PI * k)), 0.f, 1, std::sin(PI * saturate(k)));
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float CloudscapeUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
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

float CloudscapeUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
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

void CloudscapeUniverse::drawText(const Font& f, const std::vector<float>& q, const vec3& color, float alpha, float halo, float bias) {
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

// The HUD in CLOUDSCAPE's typography: a light italic serif for the song, the sky's name, the section and the
// celebrations; small, widely spaced capitals for labels; light numbers. Ink on a halo of mist.
void CloudscapeUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    if (!panels.empty()) {
        std::vector<Vert> v;
        auto quad = [&](float x0, float y0, float x1, float y1, const vec3& c, float a) {
            const Vert q[6] = {{x0, y0, c.x, c.y, c.z, a}, {x1, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a},
                               {x0, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a}, {x0, y1, c.x, c.y, c.z, a}};
            v.insert(v.end(), q, q + 6);
        };
        for (const HudRect& r : panels) {
            const vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? cur_.piece[0] : cur_.halo;
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
    const vec3 accent = lerp(cur_.ink, cur_.piece[0], 0.55f);
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
            // "Cloudscape" in the italic serif, the sky's name in small spaced capitals after it.
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            name = titleCase(name);
            const float sz = base * 1.45f, vsz = base * 0.66f, vtr = vsz * 0.3f;
            const float wName = textWidth(fontSerif_, name, sz, 0.f);
            const float wVar = var.empty() ? 0.f : textWidth(fontSans_, var, vsz, vtr);
            const float gap = base * 0.8f;
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            textQuads(fontSerif_, name, x, h.y - base * 0.1f, sz, 0.f, q);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            drawText(fontSerif_, q, ink, a, 0.7f, 0.07f);
            if (!var.empty()) {
                q.clear();
                textQuads(fontSans_, var, x + wName + gap, h.y + base * 0.25f, vsz, vtr, q);
                drawText(fontSans_, q, ink, a, 0.7f, 0.05f);
            }
            continue;
        }
        case HUD_SECTION:
            place(fontSerif_, titleCase(h.text), base * 1.35f, 0.f);
            drawText(fontSerif_, q, ink, saturate(0.2f + h.alpha * 1.9f), 0.7f, 0.07f);
            continue;
        case HUD_TITLE:
            place(fontSerif_, h.text, base * 1.2f, 0.f, -base * 0.12f);
            drawText(fontSerif_, q, ink, saturate(h.alpha * 1.3f), 0.7f, 0.07f);
            continue;
        case HUD_INFO:
            place(fontSans_, h.text, base * 0.74f, base * 0.2f);
            drawText(fontSans_, q, ink, saturate(alpha * 1.3f), 0.7f, 0.05f);
            continue;
        case HUD_LABEL:
            place(fontSans_, h.text, base * 0.78f, base * 0.3f);
            drawText(fontSans_, q, ink, saturate(alpha * 1.2f) * 0.85f, 0.5f, 0.04f);
            continue;
        case HUD_VALUE:
            place(fontSans_, h.text, base * 1.06f, base * 0.05f);
            drawText(fontSans_, q, ink, alpha, 0.5f, 0.02f);
            continue;
        case HUD_NOTICE:
            place(fontSerif_, h.text, base * 1.15f, 0.f);
            drawText(fontSerif_, q, accent, alpha, 0.6f, 0.04f);
            continue;
        case HUD_KEY:
            place(fontSans_, h.text, base * 0.9f, base * 0.05f);
            drawText(fontSans_, q, ink, alpha, 0.6f, 0.04f);
            continue;
        case HUD_TOAST:
            place(fontSerif_, titleCase(h.text), base * 1.45f, 0.f, -base * 0.15f);
            drawText(fontSerif_, q, ink, alpha, 0.8f, 0.07f);
            continue;
        case HUD_TOAST_SUB:
            place(fontSans_, h.text, base * 0.85f, base * 0.2f);
            drawText(fontSans_, q, ink, alpha * 0.9f, 0.6f, 0.03f);
            continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontSerif_, titleCase(h.text), base * 1.3f, 0.f, -base * 0.1f);
                drawText(fontSerif_, q, ink, alpha, 0.8f, 0.07f);
            } else {
                place(fontSans_, h.text, base * 0.85f, base * 0.12f);
                drawText(fontSans_, q, h.accent ? accent : ink, alpha, 0.65f, 0.03f);
            }
            continue;
        default:
            place(fontSans_, h.text, base * 0.82f, base * 0.08f);
            drawText(fontSans_, q, h.accent ? accent : ink, alpha, 0.6f, 0.03f);
            continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

// Seeded tower positions: on both sides of the well, the nearer ones bigger and clearer.
void CloudscapeUniverse::drawClouds() {
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float sink = smoothstepf(0.05f, 0.8f, bridge_.amount);
    const float aspect = (float)width_ / height_;
    const vec3 sunDir = sunDirOf(cur_.sunAz, cur_.sunEl);
    const float gain = cur_.sunGain * (0.82f + 0.12f * wm + 0.12f * wp) * (1.f + 0.05f * beatGlow_ * wm);

    glBindFramebuffer(GL_FRAMEBUFFER, cloudT_.fbo);
    glViewport(0, 0, cloudT_.w, cloudT_.h);
    glUseProgram(progCloud_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, noiseTex_);
    set1i(progCloud_, "uNoise", 0);
    set2f(progCloud_, "uRes", (float)cloudT_.w, (float)cloudT_.h);
    set1f(progCloud_, "uTanH", TAN_H);
    set1f(progCloud_, "uAspect", aspect);
    set1f(progCloud_, "uPitch", pitch_);
    set1f(progCloud_, "uEye", eye_);
    set1f(progCloud_, "uTime", worldTime_);
    set1f(progCloud_, "uSeed", seedF_);
    set2f(progCloud_, "uDrift", drift_.x, drift_.y);
    set2f(progCloud_, "uHiDrift", hiDrift_.x, hiDrift_.y);
    set3f(progCloud_, "uSunDir", sunDir);
    set3f(progCloud_, "uSunCol", cur_.sun);
    set1f(progCloud_, "uSunGain", gain);
    set1f(progCloud_, "uSunSize", cur_.sunSize);
    set1f(progCloud_, "uMoon", cur_.moon);
    set3f(progCloud_, "uZenith", cur_.zenith);
    set3f(progCloud_, "uHorizon", cur_.horizon);
    set3f(progCloud_, "uGlow", cur_.glow);
    set3f(progCloud_, "uHaze", cur_.haze);
    set3f(progCloud_, "uLit", cur_.lit);
    set3f(progCloud_, "uShade", cur_.shade);
    set3f(progCloud_, "uDeep", cur_.deep);
    set1f(progCloud_, "uCov", cur_.cov + 0.06f * wp - 0.04f * (1.f - wm));
    set1f(progCloud_, "uAmp", 1.4f + 0.2f * wm + 0.4f * wp);
    set1f(progCloud_, "uFog", cur_.fog * (1.2f - 0.15f * wm - 0.25f * wp));
    {
        float tw[24];
        float growU[6], flashU[6];
        Rng r(seed_ ^ 0x70E5ull);
        for (int i = 0; i < 6; i++) {
            const float side = (i & 1) ? 1.f : -1.f;
            const float near = i < 2 ? 1.f : (i < 4 ? 0.75f : 0.55f);
            tw[i * 4 + 0] = side * r.range(0.3f, 0.75f);
            tw[i * 4 + 1] = r.range(0.1f, 0.16f) * near * (0.7f + 0.3f * cur_.towers);
            tw[i * 4 + 2] = r.range(0.2f, 0.36f) * near;
            tw[i * 4 + 3] = r.range(0.15f, 0.3f) + (1.f - near) * 0.5f;
            growU[i] = grow_[i];
            flashU[i] = flash_[i];
        }
        glUniform4fv(ugl::U(progCloud_, "uTower"), 6, tw);
        glUniform1fv(ugl::U(progCloud_, "uGrow"), 6, growU);
        glUniform1fv(ugl::U(progCloud_, "uFlash"), 6, flashU);
    }
    set3f(progCloud_, "uFlashCol", cur_.flash);
    set1f(progCloud_, "uAnvil", cur_.anvil);
    set1f(progCloud_, "uStars", cur_.stars);
    set1f(progCloud_, "uAurora", cur_.aurora * (0.7f + 0.3f * wm + 0.3f * wp));
    set3f(progCloud_, "uAurA", cur_.aurA);
    set3f(progCloud_, "uAurB", cur_.aurB);
    set1f(progCloud_, "uVeil", cur_.veil * (1.f - 0.4f * wp));
    set1f(progCloud_, "uPart", part_);
    set1f(progCloud_, "uCirrus", cur_.cirrus * (0.6f + 0.4f * wm));
    set1f(progCloud_, "uSink", sink);
    set1f(progCloud_, "uFlood", flood_);
    glDisable(GL_BLEND);
    ugl::drawFullscreen();
    glBindTexture(GL_TEXTURE_2D, cloudT_.tex);
    glGenerateMipmap(GL_TEXTURE_2D);

    // The god rays.
    const vec2 su = sunUv();
    glBindFramebuffer(GL_FRAMEBUFFER, raysT_.fbo);
    glViewport(0, 0, raysT_.w, raysT_.h);
    glUseProgram(progRays_);
    glBindTexture(GL_TEXTURE_2D, cloudT_.tex);
    set1i(progRays_, "uSrc", 0);
    set2f(progRays_, "uSunUv", su.x, su.y);
    set1f(progRays_, "uDecay", 0.968f);
    set1f(progRays_, "uLength", 0.95f);
    ugl::drawFullscreen();
}

void CloudscapeUniverse::render(const Frame& f, GLuint fbo) {
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
    if (slow_ > 0.001f) lerpPalette(cur_, nightOf(pal_), smoothstepf(0.f, 1.f, slow_));
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.55f : 0.f), 3.f, dt);
    if (overT_ >= 0.f) {
        if (!game.over()) overT_ = -1.f;
        else overT_ += dt;
    }
    overFog_ = approach(overFog_, overT_ > 0.5f ? 0.6f : 0.f, overT_ >= 0.f ? 0.9f : 2.f, dt);
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.35f, slow_);
    if (!f.paused) simulate(game, music, dt, wdt);
    const float world = 1.f - saturate(bridge_.amount);
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);

    // ---- The sky and the clouds (reduced resolution), the rays.
    drawClouds();

    // ---- Geometry.
    tiles_.clear();
    lines_.clear();
    fxLines_.clear();
    buildBirds();
    buildBoard(game);
    buildFx();

    // ---- The scene at full resolution.
    glBindFramebuffer(GL_FRAMEBUFFER, sceneT_.fbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    {
        const vec2 su = sunUv();
        const vec2 sunBoard = v2(viewC_.x + (su.x * 2.f - 1.f) * viewH_.x, viewC_.y + (su.y * 2.f - 1.f) * viewH_.y);
        const float sl = std::sqrt(sunBoard.x * sunBoard.x + sunBoard.y * sunBoard.y);
        const vec2 sunScr = sl > 1e-3f ? sunBoard * (1.f / sl) : v2(0.f, 1.f);
        glUseProgram(progComp_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, cloudT_.tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, raysT_.tex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        set1i(progComp_, "uCloud", 0);
        set1i(progComp_, "uRays", 1);
        set1i(progComp_, "uNoise", 2);
        set4f(progComp_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set1f(progComp_, "uPx", px_);
        set4f(progComp_, "uWell", -WELL_X, -WELL_Y, WELL_X, WELL_Y);
        set4f(progComp_, "uColL", layout::LEFT_COLUMN[0], layout::LEFT_COLUMN[1] - 2.9f, layout::LEFT_COLUMN[2], layout::LEFT_COLUMN[3]);
        set4f(progComp_, "uColR", layout::RIGHT_COLUMN[0], layout::RIGHT_COLUMN[1] - 1.3f, layout::RIGHT_COLUMN[2], layout::RIGHT_COLUMN[3]);
        set3f(progComp_, "uFrost", cur_.frost);
        set3f(progComp_, "uRim", cur_.rim * (0.8f + 0.35f * beatGlow_ * wm));
        const float rayGain = (0.12f + 0.25f * wm + 0.75f * wp + 0.12f * beatGlow_ * wm + 0.6f * rayPulse_) * (1.f - 0.4f * cur_.moon);
        set3f(progComp_, "uRayCol", lerp(cur_.sun, cur_.glow, 0.6f) * cur_.sunGain * 0.95f);
        set1f(progComp_, "uRayGain", rayGain);
        set1f(progComp_, "uFrostAmt", 0.16f);
        set1f(progComp_, "uPaneAlpha", 1.f);
        set1f(progComp_, "uTime", worldTime_);
        set2f(progComp_, "uSunScr", sunScr.x, sunScr.y);
        set2f(progComp_, "uSunUv", su.x, su.y);
        set1f(progComp_, "uFlare", flare_ * world);
        set3f(progComp_, "uFlareCol", lerp(cur_.sun, cur_.glow, 0.4f) * 0.9f);
        set1f(progComp_, "uOverFog", overFog_);
        set3f(progComp_, "uMist", lerp(cur_.lit * 0.8f, cur_.haze, 0.5f));
        set1f(progComp_, "uBridge", saturate(bridge_.amount));
        set3f(progComp_, "uBridgeCol", bridge_.color);
        set1f(progComp_, "uCornerShade", 0.35f);
        {
            // light skies get a deep glass behind the stack (a dusk tint of their own sky); dark ones keep theirs
            const float lum = 0.2126f * cur_.frost.x + 0.7152f * cur_.frost.y + 0.0722f * cur_.frost.z;
            set3f(progComp_, "uPaneDeep", lerp(cur_.deep, cur_.zenith, 0.6f) * 0.5f);
            set1f(progComp_, "uPaneDeepAmt", 0.66f * smoothstepf(0.15f, 0.5f, lum));
        }
        set3f(progComp_, "uCornerCol", cur_.halo);
        ugl::drawFullscreen();
        glActiveTexture(GL_TEXTURE0);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    auto drawLines = [&](const std::vector<Line>& v) {
        if (v.empty()) return;
        glUseProgram(progLine_);
        setMat(progLine_, "uVP", vp_);
        set1f(progLine_, "uPx", px_);
        set1f(progLine_, "uBridge", saturate(bridge_.amount));
        glBindVertexArray(lineVao_);
        glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Line), v.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)v.size());
    };
    drawLines(lines_);
    if (!tiles_.empty()) {
        const vec2 su = sunUv();
        const vec2 sb = v2((su.x * 2.f - 1.f) * viewH_.x, (su.y * 2.f - 1.f) * viewH_.y + 6.f);
        const float sl = std::max(1e-3f, std::sqrt(sb.x * sb.x + sb.y * sb.y));
        glUseProgram(progTile_);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        set1i(progTile_, "uNoise", 0);
        setMat(progTile_, "uVP", vp_);
        set1f(progTile_, "uPx", px_);
        set2f(progTile_, "uSunScr", sb.x / sl, sb.y / sl);
        // the tiles take the sky's light, normalized so the glazes stay true
        auto tint = [](const vec3& c, float sat) {
            const float m = std::max(1e-3f, std::max(c.x, std::max(c.y, c.z)));
            return lerp(vec3(1.f), c / m, sat);
        };
        const float bright = lerpf(1.f, 0.72f, cur_.moon);
        set3f(progTile_, "uSkyTint", tint(lerp(cur_.zenith, cur_.horizon, 0.5f), 0.25f) * 1.15f * bright);
        set3f(progTile_, "uSunTint", tint(cur_.sun, 0.45f) * 1.05f * bright);
        set3f(progTile_, "uShadow", cur_.deep * 0.5f);
        set3f(progTile_, "uMist", lerp(cur_.lit * 0.85f, cur_.haze, 0.5f));
        set3f(progTile_, "uRim", cur_.rim);
        set3f(progTile_, "uGaugeSun", lerp(cur_.sun, cur_.glow, 0.4f));
        set3f(progTile_, "uGaugeMoon", vec3(0.85f, 0.9f, 1.f));
        set3f(progTile_, "uGaugeTrack", cur_.ink * 0.7f);
        set1f(progTile_, "uNight", cur_.moon);
        set1f(progTile_, "uBridge", saturate(bridge_.amount));
        set3f(progTile_, "uBridgeInk", bridge_.ink);
        set1f(progTile_, "uTime", worldTime_);
        glBindVertexArray(tileVao_);
        glBindBuffer(GL_ARRAY_BUFFER, tileVbo_);
        glBufferData(GL_ARRAY_BUFFER, tiles_.size() * sizeof(Tile), tiles_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)tiles_.size());
    }
    if (!mists_.empty()) {
        glUseProgram(progMist_);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        set1i(progMist_, "uNoise", 0);
        setMat(progMist_, "uVP", vp_);
        set1f(progMist_, "uBridge", saturate(bridge_.amount));
        glBindVertexArray(mistVao_);
        glBindBuffer(GL_ARRAY_BUFFER, mistVbo_);
        glBufferData(GL_ARRAY_BUFFER, mists_.size() * sizeof(Mist), mists_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)mists_.size());
    }
    drawLines(fxLines_);
    drawHud(f.hud, f.panels);
    glDisable(GL_BLEND);

    // ---- Bloom: the sun, the rims, the bright edges.
    glUseProgram(progDown_);
    set1i(progDown_, "uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    GLuint src = sceneT_.tex;
    int sw = width_, sh = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        bloomT_[i].bind();
        glBindTexture(GL_TEXTURE_2D, src);
        set2f(progDown_, "uTexel", 0.5f / sw, 0.5f / sh);
        set1f(progDown_, "uThreshold", i == 0 ? 0.95f : 0.f);
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
    set1f(progPost_, "uBloomAmt", 0.45f * world);
    set1f(progPost_, "uExposure", 0.94f + 0.08f * flare_);
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_cloudscape() {
    static const UniverseEntry e{
        "CLOUDSCAPE", "above an endless sea of clouds", [] { return std::unique_ptr<Universe>(new CloudscapeUniverse()); },
        // Airy, open songs suit the sky best (slow or mid tempo, light highs, room in the mix); everything still flies.
        [](const SongLook& l) {
            return 0.65f + 0.35f * (1.f - smoothstepf(110.f, 150.f, l.bpm)) + 0.3f * l.airWeight + 0.2f * (1.f - l.density);
        },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
