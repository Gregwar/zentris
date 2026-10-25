// SMOKE: a dark stage, slow smoke and one hard spotlight.
//
// One world: a black studio. A single hard spotlight hangs out of frame and cuts one crisp cone of light through
// slow, curling smoke; everything outside the beam falls into deep black. The well is a slab of smoked black glass
// standing in the light (a hairline catches the lamp along its edge, it casts its own shadow into the smoke behind),
// the pieces satin blocks caught in the key light: their edges toward the lamp lit, a broad sheen across the face,
// a soft shadow on the glass. Dust motes glint where the beam is.
//
// The song drives the smoke and the light: calm is a thin haze and a few wisps hanging still, the beam narrow and
// dim; mid sets the smoke rolling with the tempo (a slow glide on each beat), the bass swirling it; peak fills the
// stage with smoke, the iris opens and more light cuts through (the beam wider and brighter, sharp shafts inside it
// where the smoke shades itself), the kicks surging the smoke. A lock breathes a little smoke out from under the
// piece; a cleared row is crossed by a blade of light and burns off into wisps of smoke curling up through the beam;
// a Tetris blows a smoke ring up from the well, clears the smoke outward and opens the light; slow motion is a pin
// spot (the lamp swings straight overhead, cold and narrow, the smoke hangs almost still); game over turns the lamp
// off (its filament cooling to orange), the stack going dark row by row while the smoke rises over it. A change of
// universe thickens the smoke until it fills the frame.
//
// Cost: the smoke (a slab of noise ray-marched through the beam, with a few steps toward the lamp for the shafts)
// is drawn at a reduced resolution (at most 400 lines); only the well, the blocks, the sprites and the text are
// drawn at full resolution.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane, so the well, the
// previews and the HUD anchors sit exactly where the layout puts them.
#include "universes/smoke/smoke.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/registry.hpp"
#include "universes/smoke/shaders.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Yrsa Light Italic (the song, the section, the celebrations) and Lato Light (labels and numbers), SIL Open Font
// License, see OFL-*.txt beside this file.
const unsigned char FONT_SERIF[] = {
#include "universes/smoke/Yrsa-LightItalic.ttf.inc"
};
const unsigned char FONT_SANS[] = {
#include "universes/smoke/Lato-Light.ttf.inc"
};

namespace shaders = smoke_shaders;
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

constexpr float WELL_X = 5.2f, WELL_Y = 10.2f; // the glass slab of the well (half extents, board)

vec2 v2(float x, float y) { return vec2{x, y}; }
vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 operator*(vec2 a, float k) { return {a.x * k, a.y * k}; }

vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }

// The variants: each a lighting setup (sRGB here, linear once loaded). Colors: the lamp (its gel), the smoke's own
// tint, the fill in the shadows, the well's glass, its hairline, the HUD's ink and accent; the seven satin blocks
// (I O T S Z J L). Then the lamp's position and where it points (board space, z toward the viewer), its strength, the
// cone (tan of its half angle), the haze and the smoke.
struct VariantDef {
    const char* name;
    unsigned light, smoke, fill, panel, rim, ink, accent;
    unsigned piece[7];
    float lx, ly, lz, ax, ay, az, gain, cone, haze, smokeAmt;
};
const VariantDef VARIANTS[] = {
    {"TUNGSTEN", 0xffb36a, 0xfff2e2, 0x0d1220, 0x0d0d10, 0xffcf9c, 0xf1e5d3, 0xffb36a,
     {0x4fb5c6, 0xe8b84a, 0xa268c6, 0x76b06a, 0xd85a48, 0x4c70d2, 0xee8d46},
     -13.f, 19.f, 8.f, 4.f, -14.f, -7.f, 1.0f, 0.2f, 1.f, 1.f},
    {"DAYLIGHT", 0xdfe9ff, 0xf4f8ff, 0x0c0e12, 0x0c0d10, 0xeef4ff, 0xe9eef6, 0xbcd4ff,
     {0x5cc8f0, 0xf3d55b, 0xb98cf0, 0x57d39a, 0xf2637a, 0x6b8af5, 0xff9f5a},
     13.f, 19.f, 8.f, -4.f, -14.f, -7.f, 0.85f, 0.18f, 1.f, 0.9f},
    {"RED GEL", 0xff3b2a, 0xffe6e0, 0x061614, 0x0e0b0b, 0xff8e7a, 0xf3dcd4, 0xff6a52,
     {0xf3e7d3, 0xffb347, 0xc94b7d, 0x74c7b5, 0xff5a43, 0x7465c8, 0xf2937a},
     -31.f, 9.f, 8.f, 10.f, -6.f, -7.f, 1.0f, 0.14f, 1.f, 1.1f},
    {"TEAL GEL", 0x33cfc6, 0xe6fffc, 0x1a0e0a, 0x0b0e0e, 0x8ff0e8, 0xdff3f0, 0x5fe0d6,
     {0xc8f2ec, 0xf4c65e, 0x8a72e6, 0x3cc493, 0xf0706a, 0x3d8ee0, 0xf5a35e},
     10.f, 20.f, 8.f, -6.f, -14.f, -7.f, 1.05f, 0.18f, 1.f, 1.f},
    {"VIOLET", 0x8e6bff, 0xf0eaff, 0x0c0814, 0x0d0b12, 0xc8b4ff, 0xebe4ff, 0xb39cff,
     {0x6fe3ff, 0xffd56e, 0xd98bff, 0x7af2b4, 0xff6fa6, 0x7487ff, 0xffa877},
     0.f, 21.f, 7.f, 1.f, -14.f, -7.f, 1.2f, 0.19f, 1.f, 1.f},
    {"SODIUM", 0xd2dc58, 0xfcffe8, 0x120a1a, 0x0d0e0b, 0xe8eea0, 0xeef0d8, 0xd8e070,
     {0x6fd3c6, 0xf0e27a, 0xc17fd8, 0x96d468, 0xe8644e, 0x5b7ed0, 0xf2a446},
     -9.f, 20.f, 8.f, 9.f, -14.f, -7.f, 0.95f, 0.18f, 1.f, 1.f},
    {"WHITE NOIR", 0xffffff, 0xffffff, 0x060606, 0x0b0b0b, 0xffffff, 0xeeeeee, 0xd8d8d8,
     {0xa4cdd6, 0xe6d49c, 0xbba3d0, 0xa9cfa2, 0xe0a49c, 0x919fcf, 0xdcb58a},
     31.f, 8.f, 8.f, -10.f, -8.f, -7.f, 0.55f, 0.12f, 1.f, 1.1f},
    {"ROSE", 0xff7aa2, 0xfff0f4, 0x0e0a14, 0x0e0c0f, 0xffc0d2, 0xf5e4ea, 0xff9ab8,
     {0x78d9e8, 0xffdc96, 0xb871dc, 0x8edaa9, 0xff5778, 0x6c84e6, 0xffab7f},
     -16.f, 18.f, 8.f, 6.f, -14.f, -7.f, 1.05f, 0.17f, 1.f, 1.f},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

using Palette = SmokeUniverse::Palette;
static_assert(sizeof(Palette) % sizeof(float) == 0, "a palette is floats only");
constexpr int PAL_FLOATS = (int)(sizeof(Palette) / sizeof(float));

vec3 lin(unsigned c) { return ugl::toLinear(hex(c)); }

Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    Palette p;
    p.light = lin(d.light); p.smoke = lin(d.smoke); p.fill = lin(d.fill); p.panel = lin(d.panel); p.rim = lin(d.rim);
    p.ink = lin(d.ink); p.accent = lin(d.accent);
    for (int i = 0; i < 7; i++) p.piece[i] = lin(d.piece[i]);
    p.lpos = vec3(d.lx, d.ly, d.lz);
    p.aim = vec3(d.ax, d.ay, d.az);
    p.gain = d.gain; p.cone = d.cone; p.haze = d.haze; p.smokeAmt = d.smokeAmt;
    return p;
}

// Slow motion: a pin spot. The lamp swings straight overhead, cold and narrow; the blocks keep their colors.
Palette pinOf(const Palette& d) {
    Palette p = d;
    p.light = lin(0xc9d6ff);
    p.smoke = lin(0xf0f4ff);
    p.fill = d.fill * 0.5f;
    p.rim = lin(0xdde6ff);
    p.lpos = vec3(0.f, 22.f, 6.f);
    p.aim = vec3(0.f, -14.f, -10.f);
    p.cone = 0.11f;
    p.gain = 1.5f;
    p.haze = 0.8f;
    p.smokeAmt = 0.8f;
    return p;
}

void lerpPalette(Palette& a, const Palette& b, float k) {
    float* pa = reinterpret_cast<float*>(&a);
    const float* pb = reinterpret_cast<const float*>(&b);
    for (int i = 0; i < PAL_FLOATS; i++) pa[i] += (pb[i] - pa[i]) * k;
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

std::string upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool SmokeUniverse::init(int width, int height) {
    progSmoke_ = compile(ugl::FULLSCREEN_VS, shaders::SMOKE_FS, "smoke smoke");
    progComp_ = compile(ugl::FULLSCREEN_VS, shaders::COMPOSITE_FS, "smoke composite");
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "smoke shape");
    progTile_ = compile(shaders::TILE_VS, shaders::TILE_FS, "smoke tile");
    progSprite_ = compile(shaders::SPRITE_VS, shaders::SPRITE_FS, "smoke sprite");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "smoke text");
    progDown_ = compile(ugl::FULLSCREEN_VS, shaders::DOWN_FS, "smoke down");
    progUp_ = compile(ugl::FULLSCREEN_VS, shaders::UP_FS, "smoke up");
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "smoke post");

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
    instanced(spriteVao_, spriteVbo_, 3, sizeof(Sprite));

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
    // the dust in the air: motes scattered through the front of the smoke
    Rng r(0x5E0Cull);
    for (int i = 0; i < 90; i++)
        moteList_.push_back({vec3(r.range(-22.f, 22.f), r.range(-13.f, 13.f), r.range(-8.f, 2.f)), r.range(0.f, TAU), r.range(0.5f, 1.f)});
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// The noise: a tileable 64^3 volume for the smoke (r an fbm, g a finer noise whose ridges make the curling sheets,
// b and a two warp fields) and a tileable 256^2 texture for the sprites and the fog fronts.
void SmokeUniverse::buildNoise() {
    auto hash3 = [](int x, int y, int z, int s) {
        uint64_t h = splitmix64((uint64_t)(uint32_t)x * 73856093ull ^ (uint64_t)(uint32_t)y * 19349663ull ^
                                (uint64_t)(uint32_t)z * 83492791ull ^ (uint64_t)s * 2654435761ull);
        return h;
    };
    // gradient noise with a period (tileable)
    auto perlin3 = [&](float x, float y, float z, int period, int seed) {
        const int x0 = (int)std::floor(x), y0 = (int)std::floor(y), z0 = (int)std::floor(z);
        const float fx = x - x0, fy = y - y0, fz = z - z0;
        auto grad = [&](int ix, int iy, int iz, float dx, float dy, float dz) {
            const uint64_t h = hash3(((ix % period) + period) % period, ((iy % period) + period) % period,
                                     ((iz % period) + period) % period, seed);
            // a random unit vector
            const float u = (h >> 40) * (1.0f / 16777216.0f) * 2.f - 1.f;
            const float a = ((h >> 8) & 0xFFFF) * (TAU / 65536.f);
            const float s = std::sqrt(std::max(0.f, 1.f - u * u));
            return s * std::cos(a) * dx + s * std::sin(a) * dy + u * dz;
        };
        auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
        const float u = fade(fx), v = fade(fy), w = fade(fz);
        const float x00 = lerpf(grad(x0, y0, z0, fx, fy, fz), grad(x0 + 1, y0, z0, fx - 1, fy, fz), u);
        const float x10 = lerpf(grad(x0, y0 + 1, z0, fx, fy - 1, fz), grad(x0 + 1, y0 + 1, z0, fx - 1, fy - 1, fz), u);
        const float x01 = lerpf(grad(x0, y0, z0 + 1, fx, fy, fz - 1), grad(x0 + 1, y0, z0 + 1, fx - 1, fy, fz - 1), u);
        const float x11 = lerpf(grad(x0, y0 + 1, z0 + 1, fx, fy - 1, fz - 1), grad(x0 + 1, y0 + 1, z0 + 1, fx - 1, fy - 1, fz - 1), u);
        return lerpf(lerpf(x00, x10, v), lerpf(x01, x11, v), w);
    };
    const int N = 64;
    std::vector<unsigned char> vol((size_t)N * N * N * 4);
    auto q8 = [](float v) { return (unsigned char)std::clamp((int)(v * 255.f + 0.5f), 0, 255); };
    for (int z = 0; z < N; z++)
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                const float u = (x + 0.5f) / N, v = (y + 0.5f) / N, w = (z + 0.5f) / N;
                const float r = 0.5f + 0.62f * (perlin3(u * 4, v * 4, w * 4, 4, 1) * 0.6f + perlin3(u * 8, v * 8, w * 8, 8, 2) * 0.28f +
                                                perlin3(u * 16, v * 16, w * 16, 16, 3) * 0.12f);
                const float g = 0.5f + 0.75f * (perlin3(u * 8, v * 8, w * 8, 8, 4) * 0.75f + perlin3(u * 16, v * 16, w * 16, 16, 5) * 0.25f);
                const float b = 0.5f + 0.7f * perlin3(u * 4, v * 4, w * 4, 4, 6);
                const float a = 0.5f + 0.7f * perlin3(u * 4, v * 4, w * 4, 4, 7);
                unsigned char* p = &vol[(((size_t)z * N + y) * N + x) * 4];
                p[0] = q8(r); p[1] = q8(g); p[2] = q8(b); p[3] = q8(a);
            }
    glGenTextures(1, &volTex_);
    glBindTexture(GL_TEXTURE_3D, volTex_);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, N, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, vol.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glBindTexture(GL_TEXTURE_3D, 0);

    const int M = 256;
    std::vector<unsigned char> px((size_t)M * M * 4);
    for (int y = 0; y < M; y++)
        for (int x = 0; x < M; x++) {
            const float u = (x + 0.5f) / M, v = (y + 0.5f) / M;
            const float r = 0.5f + 0.6f * (perlin3(u * 8, v * 8, 0.3f, 8, 11) * 0.7f + perlin3(u * 16, v * 16, 0.3f, 16, 12) * 0.35f);
            const float g = 0.5f + 0.6f * (perlin3(u * 4, v * 4, 0.6f, 4, 13) * 0.65f + perlin3(u * 8, v * 8, 0.6f, 8, 14) * 0.35f);
            const float b = 0.5f + 0.6f * (perlin3(u * 16, v * 16, 0.9f, 16, 15) * 0.7f + perlin3(u * 32, v * 32, 0.9f, 32, 16) * 0.35f);
            unsigned char* p = &px[((size_t)y * M + x) * 4];
            p[0] = q8(r); p[1] = q8(g); p[2] = q8(b); p[3] = 255;
        }
    glGenTextures(1, &noiseTex_);
    glBindTexture(GL_TEXTURE_2D, noiseTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, M, M, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

// Glyphs baked as signed distance fields: they scale cleanly, and the distance gives the halo of darkness.
void SmokeUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] smoke font init failed\n");
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

void SmokeUniverse::resize(int width, int height) {
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

void SmokeUniverse::createTargets() {
    // The smoke at most 400 lines (soft; the beam's edge stays crisp enough once upsampled).
    const int sh = std::max(90, std::min(400, height_ / 2));
    const int sw = std::max(160, (int)std::lround((float)sh * width_ / height_));
    smokeT_.create(sw, sh, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    sceneT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    int w = width_, h = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        bloomT_[i].create(w, h, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

SmokeUniverse::~SmokeUniverse() {
    smokeT_.destroy();
    sceneT_.destroy();
    for (auto& t : bloomT_) t.destroy();
    for (GLuint p : {progSmoke_, progComp_, progShape_, progTile_, progSprite_, progText_, progDown_, progUp_, progPost_})
        glDeleteProgram(p);
    for (GLuint v : {tileVao_, spriteVao_, shapeVao_, textVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, tileVbo_, spriteVbo_, shapeVbo_, textVbo_}) glDeleteBuffers(1, &b);
    if (volTex_) glDeleteTextures(1, &volTex_);
    if (noiseTex_) glDeleteTextures(1, &noiseTex_);
    for (Font* f : {&fontSerif_, &fontSans_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// =====================================================================================================
// Song, level, bonus
// =====================================================================================================

// The light from the song: dark minor songs get the colored gels and the noir, bright major ones the white lights;
// the key's hue picks among them.
int SmokeUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    const bool dark = t.hasSong && t.minor && t.brightness < 0.45f;
    const bool bright = t.hasSong && !t.minor && t.brightness > 0.55f;
    int v;
    if (dark) v = h < 0.25f ? 2 : (h < 0.5f ? 4 : (h < 0.75f ? 6 : 3));
    else if (bright) v = h < 0.33f ? 0 : (h < 0.66f ? 1 : 7);
    else if (h < 0.125f) v = 2;
    else if (h < 0.25f) v = 0;
    else if (h < 0.375f) v = 5;
    else if (h < 0.5f) v = 3;
    else if (h < 0.625f) v = 1;
    else if (h < 0.75f) v = 4;
    else if (h < 0.875f) v = 7;
    else v = t.minor ? 6 : 0;
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void SmokeUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    seedF_ = (float)(look.seed % 1000) * 0.137f;
    rng_ = Rng(look.seed ^ 0x5300ull);
    flow_ = vec3(rng_.range(0.f, 1.f), rng_.range(0.f, 1.f), rng_.range(0.f, 1.f));
    if (seconds <= 0.f) palSet_ = false; // snap on the first frame
    else swell_ = std::max(swell_, 0.4f);
}

void SmokeUniverse::setLevel(int level, float energy, float seconds, float impact) {
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
    if (rising) swell_ = std::max(swell_, 0.25f + 0.5f * impact);
}

void SmokeUniverse::refresh(float strength) { swell_ = std::max(swell_, 0.3f * strength); }

void SmokeUniverse::setBonus(bool on) { slowOn_ = on; }

void SmokeUniverse::levelUp() { swell_ = std::max(swell_, 0.5f); }

vec2 SmokeUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints SmokeUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    h.background = p.fill * 0.6f + p.light * 0.025f;
    h.light = false;
    h.ink = lerp(p.ink, p.light, 0.25f) * 0.85f;
    h.accent = p.light;
    h.styles = TS_INK | TS_LUMA | TS_CURTAIN; // smoke dissolving, darks first, the stage curtain
    return h;
}

vec3 SmokeUniverse::lampPos() const { return cur_.lpos; }
vec3 SmokeUniverse::lampAxis() const { return normalize(cur_.aim - cur_.lpos); }

// The same cone as the shaders' spot() (soft edge), for what the CPU lights: the motes, the puffs.
float SmokeUniverse::spotAt(const vec3& p) const {
    const vec3 lp = lampPos(), ax = lampAxis();
    const vec3 v = p - lp;
    const float a = dot(v, ax);
    if (a <= 0.5f) return 0.f;
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float cone = cur_.cone * (1.f + 0.08f * wm + 0.3f * wp + iris_);
    const float r = length(v - ax * a) / (a * cone);
    const float edge = 1.f - smoothstepf(0.85f, 1.05f, r);
    return edge * lerpf(1.f, 0.7f, r * r) / (1.f + a * a * 0.0004f);
}

void SmokeUniverse::puff(vec2 p, vec2 v, float r, float grow, float life, float alpha, float delay, float stretch, int kind) {
    Puff q;
    q.p = p; q.v = v; q.r = r; q.grow = grow; q.t = 0.f; q.life = life; q.alpha = alpha; q.delay = delay;
    q.stretch = stretch; q.kind = kind;
    q.seed = rng_.uniform();
    puffs_.push_back(q);
}

// =====================================================================================================
// Game events
// =====================================================================================================

void SmokeUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::Lock: {
        // The impact pushes smoke out from the contact under the piece: a thin layer that starts at the piece's
        // base and spreads out sideways along the stack, rising a little as it thins (harder after a hard drop).
        if (ev.cells.empty()) break;
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f;
        for (auto& q : ev.cells) {
            const vec3 p = layout::cellPos((float)q.x, (float)q.y);
            x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y);
        }
        const float hw = 0.5f * (x1 - x0) + 0.4f;
        const float hard = lastDrop_ >= 2 ? 1.f : 0.f;
        puff(v2(0.5f * (x0 + x1), y0 - 0.5f), v2(0.f, 0.22f), hw, 1.1f + 0.5f * hard, 1.1f, 0.4f + 0.15f * hard, 0.f, 0.18f / hw);
        puffs_.back().minGlow = 0.45f; // caught in the stray light, even outside the beam
        lastDrop_ = 0;
        break;
    }
    case GameEvent::HardDrop:
        lastDrop_ = ev.count; // the lock that follows pushes more smoke
        break;
    case GameEvent::Clear: {
        const bool tetris = ev.count >= 4;
        // a blade of light runs along each row, then the blocks burn off into smoke curling up through the beam
        float rows[4] = {0, 0, 0, 0};
        int nr = 0;
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            bool seen = false;
            for (int i = 0; i < nr; i++)
                if (rows[i] == p.y) seen = true;
            if (!seen && nr < 4) rows[nr++] = p.y;
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0.f; d.type = std::clamp(c.type, 0, 6); d.over = false;
            d.delay = std::fabs(c.x - 4.5f) * 0.025f; // from the middle of the row outward
            d.seed = rng_.uniform();
            dying_.push_back(d);
        }
        if (tetris) {
            // a smoke ring rolls up out of the well, the smoke is blown outward, the light opens
            puff(v2(0.f, rows[0] + 2.f), v2(0.f, 5.5f), 2.2f, 1.3f, 4.5f, 0.85f, 0.2f, 1.f, 1);
            blastT_ = 0.f;
            swell_ = std::max(swell_, 1.3f);
        } else {
            swell_ = std::max(swell_, 0.12f * ev.count);
        }
        break;
    }
    case GameEvent::TopOut: {
        // The lamp goes out: the stack goes dark row by row from the top, the smoke rises over it.
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0.f; d.type = std::clamp(c.type, 0, 6); d.over = true;
            d.delay = (c.y - top) * 0.09f + rng_.range(0.f, 0.05f);
            d.seed = rng_.uniform();
            dying_.push_back(d);
        }
        overT_ = 0.f;
        break;
    }
    default: break;
    }
}

// =====================================================================================================
// Simulation
// =====================================================================================================

void SmokeUniverse::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    worldTime_ += wdt;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);

    // The smoke rises and rolls: a slow glide on each beat (mid, peak), barely drifting when calm.
    {
        const float ph = music.beatPhase;
        if (ph < lastBeatPhase_ - 0.5f) beats_ += 1.f;
        lastBeatPhase_ = ph;
        const float e = 1.f - std::pow(1.f - ph, 2.4f);
        const float eased = beats_ + lerpf(ph, e, wm * 0.85f);
        float d = lastEased_ < 0.f ? 0.f : eased - lastEased_;
        if (d < 0.f || d > 2.f) d = 0.f;
        lastEased_ = eased;
        const float perBeat = 0.004f + 0.007f * wm + 0.007f * wp; // noise units per beat
        const float speed = music.hasSong ? d * perBeat * (wdt / std::max(dt, 1e-4f)) : wdt * 0.008f;
        const float calm = wdt * 0.003f;
        flow_.y -= speed + calm;
        flow_.x += (speed + calm) * 0.35f;
        flow_.z += (speed + calm) * 0.25f;
    }
    // The bass swirls it (mid, peak); the kicks surge it (peak).
    {
        float b = music.hasSong ? (music.bandsFast[0] + music.bandsFast[1] + music.bandsFast[2]) / 3.f : 0.f;
        b = std::max(b, music.audio.bass);
        bass_ = approach(bass_, saturate(b) * (0.3f + 0.7f * wm), 3.f, dt);
        swirl_ += wdt * (0.03f + 0.25f * bass_ * (0.4f + 0.6f * wp));
    }
    beatGlow_ = approach(beatGlow_, music.hasSong ? saturate(music.beatPulse) : 0.f, 10.f, dt);
    sinceKick_ += dt;
    const float kick = std::max(music.kick, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    if (kick > 0.55f && prevKick_ <= 0.55f && sinceKick_ > 0.18f) {
        sinceKick_ = 0.f;
        surge_ = std::max(surge_, 0.15f * wm + 0.5f * wp);
    }
    prevKick_ = kick;
    surge_ = approach(surge_, 0.f, 1.3f, dt);
    swell_ = approach(swell_, 0.f, 0.55f, dt);
    blastT_ += dt;
    const float blast = blastT_ < 4.f ? (1.f - smoothstepf(0.6f, 4.f, blastT_)) : 0.f;
    iris_ = approach(iris_, 0.25f * blast, 2.f, dt);

    // The dust drifts slowly in the air.
    for (Mote& m : moteList_) {
        m.phase += wdt * 0.3f;
        m.p.x += wdt * (0.12f * std::sin(m.phase * 0.7f + m.p.y * 0.2f) + 0.05f);
        m.p.y += wdt * (0.1f * std::cos(m.phase * 0.5f + m.p.x * 0.15f) + 0.06f);
        if (m.p.y > 13.5f) m.p.y -= 27.f;
        if (m.p.x > 22.5f) m.p.x -= 45.f;
    }

    // Effects age (slowed by the slow motion like the world).
    for (auto& p : puffs_) {
        p.t += wdt;
        if (p.t < p.delay) continue;
        p.p = p.p + p.v * wdt;
        p.v = p.v * std::exp(-(p.kind == 1 ? 0.35f : 0.7f) * wdt);
        if (p.kind == 0) p.v.x += std::sin(p.t * 1.7f + p.seed * 9.f) * 0.35f * wdt; // a curl as it rises
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(), [](const Puff& p) { return p.t > p.delay + p.life; }), puffs_.end());
    for (auto& d : dying_) d.t += d.over ? dt : wdt;
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return d.t > d.delay + (d.over ? 9.f : 0.45f); }), dying_.end());
    for (auto& s : sweeps_) s.t += wdt;
    sweeps_.erase(std::remove_if(sweeps_.begin(), sweeps_.end(), [](const Sweep& s) { return s.t > 0.6f; }), sweeps_.end());

    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
}

// =====================================================================================================
// Geometry
// =====================================================================================================

void SmokeUniverse::tile(float x, float y, float hw, float hh, const vec3& c, float alpha, int kind, float e0, float e1,
                         float e2, float e3, float angle, float age) {
    tiles_.push_back({{x, y, hw, hh}, {c.x, c.y, c.z, alpha}, {(float)kind, age, angle, 0.f}, {e0, e1, e2, e3}});
}

void SmokeUniverse::buildBoard(const Game& g) {
    // The shadows the blocks cast on the glass, away from the lamp (drawn first, under every block).
    std::vector<Tile> blocks;
    const vec3 lp = lampPos();
    auto shadowOff = [&](float x, float y) {
        vec2 d = v2(x - lp.x, y - lp.y);
        const float l = std::max(1e-3f, std::sqrt(d.x * d.x + d.y * d.y));
        return d * (0.2f / l);
    };
    auto block = [&](float x, float y, float s, const vec3& c, float alpha, float light, float age, bool shadow) {
        if (shadow) {
            const vec2 o = shadowOff(x, y);
            tile(x + o.x, y + o.y, 0.62f * s, 0.62f * s, vec3(0.f), alpha, 3, s);
        }
        blocks.push_back({{x, y, 0.5f * s, 0.5f * s}, {c.x, c.y, c.z, alpha}, {0.f, -1.f, 0.f, 0.f}, {s, light, age, 0.f}});
    };
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            const float age = c.flash > 0.f ? (1.f - c.flash) * Game::LOCK_FX_SECONDS : -1.f;
            block(p.x, p.y, 1.f, cur_.piece[c.type], 1.f, 1.f, age, true);
        }
    // Cleared blocks burning off into smoke; a topped-out stack going dark.
    for (const Dying& d : dying_) {
        const float t = d.t - d.delay;
        if (d.over) {
            const float k = smoothstepf(0.f, 0.7f, t);
            const float a = 1.f - smoothstepf(5.f, 8.f, d.t);
            block(d.x, d.y, 1.f, cur_.piece[d.type], a, lerpf(1.f, 0.1f, k), -1.f, true);
            continue;
        }
        if (t < 0.f) {
            block(d.x, d.y, 1.f, cur_.piece[d.type], 1.f, 1.f, -1.f, true);
            continue;
        }
        // the light goes off on the cleared blocks: they dim and sink into the dark, from the middle outward
        const float k = saturate(t / 0.42f);
        block(d.x, d.y - 0.25f * k * k, 1.f - 0.15f * k, cur_.piece[d.type], 1.f - smoothstepf(0.2f, 1.f, k),
              lerpf(1.f, 0.05f, smoothstepf(0.f, 0.6f, k)), -1.f, false);
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
                blocks.push_back({{w.x, w.y, 0.5f, 0.5f}, {col.x, col.y, col.z, 0.9f}, {1.f, -1.f, 0.f, 0.f}, {1.f, 0.f, 0.f, 0.f}});
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (auto& c : cells) {
            if (c[1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(c[0] + dx, c[1] + dy);
            block(w.x, w.y, 1.f, col, 1.f, 1.f, -1.f, true);
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
            block(pos.x, pos.y, sc, cur_.piece[type], alpha, 1.f, -1.f, false);
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.4f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }
    tiles_.insert(tiles_.end(), blocks.begin(), blocks.end());
    // The bonus gauge: an iris.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        tile(gc.x, gc.y, R * 1.1f, R * 1.1f, vec3(1.f), 1.f, 4, saturate(gaugeShown_), g.bonusReady() ? 1.f : 0.f,
             g.bonusActive() ? 1.f : 0.f, R);
    }
}

void SmokeUniverse::buildFx() {
    sprites_.clear();
    blades_.clear();
    motes_.clear();
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float lightOn = 1.f - over_;
    const float gain = cur_.gain * lightOn;
    // the smoke puffs, lit by the beam where they drift into it
    for (const Puff& p : puffs_) {
        if (p.t < p.delay) continue;
        const float k = (p.t - p.delay) / p.life;
        const float r = p.r * (1.f + p.grow * (1.f - (1.f - k) * (1.f - k)));
        const float lit = spotAt(vec3(p.p.x, p.p.y, 0.6f));
        const float glow = std::max(p.kind == 1 ? std::max(lit, 0.55f) : lit, p.minGlow);
        const vec3 c = cur_.light * cur_.smoke * (0.12f + 1.1f * glow) * gain + cur_.fill * 2.f;
        float a = p.alpha * smoothstepf(0.f, 0.1f, k) * (1.f - smoothstepf(0.3f, 1.f, k));
        if (p.kind == 1) a = p.alpha * smoothstepf(0.f, 0.08f, k) * (1.f - smoothstepf(0.45f, 1.f, k));
        (p.kind == 1 ? blades_ : sprites_).push_back({{p.p.x, p.p.y, r, p.stretch}, {c.x, c.y, c.z, a},
                            {(float)p.kind, p.seed * 11.f, k, (p.seed - 0.5f) * 3.f}});
    }
    // a blade of light along each cleared row
    for (const Sweep& s : sweeps_) {
        if (s.t < 0.f) continue;
        const float k = s.t / 0.6f;
        const float a = std::sin(PI * saturate(k)) * 1.6f;
        const vec3 c = lerp(cur_.light, vec3(1.f), 0.5f) * (1.2f + gain);
        blades_.push_back({{s.dir * (-5.f + 10.f * smoothstepf(0.f, 0.7f, k)) * 0.3f, s.y, 5.6f, 0.5f}, {c.x, c.y, c.z, a}, {3.f, 0.f, k, 0.f}});
    }
    // dust motes, seen only where the beam is
    const float vis = (0.25f + 0.45f * wm + 0.5f * wp) * lightOn * (1.f - 0.5f * slow_);
    const float beat = beats_ + lastBeatPhase_;
    for (const Mote& m : moteList_) {
        if (std::fabs(m.p.x) < WELL_X + 0.3f && std::fabs(m.p.y) < WELL_Y + 0.3f) continue;
        const float lit = spotAt(m.p);
        if (lit < 0.02f) continue;
        const float tw = 0.55f + 0.45f * std::sin(m.phase * 3.1f + beat * 0.5f);
        const vec3 c = cur_.light * cur_.gain * lit * tw * vis * 2.2f;
        const float s = 0.05f + 0.06f * m.size;
        motes_.push_back({{m.p.x, m.p.y, s, 1.f}, {c.x, c.y, c.z, 1.f}, {2.f, 0.f, 0.f, 0.f}});
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float SmokeUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
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

float SmokeUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
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

void SmokeUniverse::drawText(const Font& f, const std::vector<float>& q, const vec3& color, float alpha, float halo, float bias) {
    if (q.empty() || !f.ok) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    set3f(progText_, "uColor", color);
    set3f(progText_, "uHalo", vec3(0.f));
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

// The HUD in SMOKE's typography, like a theatre programme: a light italic serif for the song, the light's name, the
// section and the celebrations; small, widely spaced light capitals for the labels; light numbers.
void SmokeUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    if (!panels.empty()) {
        std::vector<Vert> v;
        auto quad = [&](float x0, float y0, float x1, float y1, const vec3& c, float a) {
            const Vert q[6] = {{x0, y0, c.x, c.y, c.z, a}, {x1, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a},
                               {x0, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a}, {x0, y1, c.x, c.y, c.z, a}};
            v.insert(v.end(), q, q + 6);
        };
        for (const HudRect& r : panels) {
            const vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? cur_.accent : cur_.panel * 0.6f;
            quad(r.x, r.y, r.x + r.w, r.y + r.h, c, r.accent || r.custom ? r.alpha : std::min(1.f, r.alpha * 1.1f));
        }
        glUseProgram(progShape_);
        set2f(progShape_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(shapeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vert), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
    }
    const vec3 ink = cur_.ink * 0.92f;
    const vec3 dim = cur_.ink * 0.55f;
    const vec3 accent = cur_.accent * 1.1f;
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
            // "Smoke" in the italic serif, the light's name in small spaced capitals after it.
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            name = titleCase(name);
            const float sz = base * 1.5f, vsz = base * 0.62f, vtr = vsz * 0.42f;
            const float wName = textWidth(fontSerif_, name, sz, 0.f);
            const float wVar = var.empty() ? 0.f : textWidth(fontSans_, var, vsz, vtr);
            const float gap = base * 0.8f;
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            textQuads(fontSerif_, name, x, h.y - base * 0.12f, sz, 0.f, q);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            drawText(fontSerif_, q, ink, a, 0.5f, 0.05f);
            if (!var.empty()) {
                q.clear();
                textQuads(fontSans_, var, x + wName + gap, h.y + base * 0.28f, vsz, vtr, q);
                drawText(fontSans_, q, accent, a, 0.5f, 0.06f);
            }
            continue;
        }
        case HUD_SECTION:
            place(fontSerif_, titleCase(h.text), base * 1.4f, 0.f);
            drawText(fontSerif_, q, accent, saturate(0.2f + h.alpha * 1.9f), 0.5f, 0.05f);
            continue;
        case HUD_TITLE:
            place(fontSerif_, h.text, base * 1.3f, 0.f, -base * 0.14f);
            drawText(fontSerif_, q, ink, saturate(h.alpha * 1.3f), 0.5f, 0.05f);
            continue;
        case HUD_INFO:
            place(fontSans_, upper(h.text), base * 0.66f, base * 0.32f);
            drawText(fontSans_, q, dim, saturate(alpha * 1.4f), 0.5f, 0.06f);
            continue;
        case HUD_LABEL:
            place(fontSans_, h.text, base * 0.7f, base * 0.42f);
            drawText(fontSans_, q, dim, saturate(alpha * 1.3f), 0.5f, 0.06f);
            continue;
        case HUD_VALUE:
            place(fontSans_, h.text, base * 1.12f, base * 0.06f);
            drawText(fontSans_, q, ink, alpha, 0.5f, 0.04f);
            continue;
        case HUD_NOTICE:
            place(fontSerif_, h.text, base * 1.2f, 0.f);
            drawText(fontSerif_, q, accent, alpha, 0.5f, 0.04f);
            continue;
        case HUD_KEY:
            place(fontSans_, h.text, base * 0.85f, base * 0.1f);
            drawText(fontSans_, q, ink, alpha, 0.5f, 0.05f);
            continue;
        case HUD_TOAST:
            place(fontSerif_, titleCase(h.text), base * 1.55f, 0.f, -base * 0.15f);
            drawText(fontSerif_, q, ink, alpha, 0.6f, 0.05f);
            continue;
        case HUD_TOAST_SUB:
            place(fontSans_, h.text, base * 0.8f, base * 0.3f);
            drawText(fontSans_, q, accent, alpha * 0.9f, 0.5f, 0.05f);
            continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontSerif_, titleCase(h.text), base * 1.35f, 0.f, -base * 0.1f);
                drawText(fontSerif_, q, ink, alpha, 0.6f, 0.05f);
            } else {
                place(fontSans_, h.text, base * 0.82f, base * 0.16f);
                drawText(fontSans_, q, h.accent ? accent : ink, alpha, 0.5f, 0.05f);
            }
            continue;
        default:
            place(fontSans_, h.text, base * 0.8f, base * 0.12f);
            drawText(fontSans_, q, h.accent ? accent : ink, alpha, 0.5f, 0.05f);
            continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void SmokeUniverse::drawSmoke() {
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float thick = smoothstepf(0.0f, 0.85f, bridge_.amount);
    const float blast = blastT_ < 4.f ? smoothstepf(0.f, 0.25f, blastT_) * (1.f - smoothstepf(1.f, 4.f, blastT_)) : 0.f;
    const vec3 ax = lampAxis();
    const float lightOn = 1.f - over_;
    const float gain = cur_.gain * (0.95f + 0.15f * wm + 0.5f * wp + 0.6f * swell_ + 0.12f * surge_ + 0.05f * beatGlow_ * wm) *
                       lightOn * (1.f - 0.6f * thick);

    glBindFramebuffer(GL_FRAMEBUFFER, smokeT_.fbo);
    glViewport(0, 0, smokeT_.w, smokeT_.h);
    glUseProgram(progSmoke_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, volTex_);
    set1i(progSmoke_, "uVol", 0);
    set4f(progSmoke_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set3f(progSmoke_, "uLPos", lampPos());
    set3f(progSmoke_, "uLAxis", ax);
    set1f(progSmoke_, "uCone", cur_.cone * (1.f + 0.08f * wm + 0.3f * wp + iris_ + 0.4f * thick));
    set3f(progSmoke_, "uLCol", cur_.light * cur_.smoke);
    set3f(progSmoke_, "uFill", cur_.fill * (0.3f + 1.2f * thick));
    set1f(progSmoke_, "uGain", gain);
    set1f(progSmoke_, "uHaze", 0.009f * cur_.haze * (1.f + 0.2f * wm + 0.4f * wp));
    set1f(progSmoke_, "uSmoke", 0.06f * cur_.smokeAmt * (1.f + 1.6f * wm + 3.0f * wp + 1.8f * surge_));
    set1f(progSmoke_, "uWarp", 1.1f + 0.6f * bass_ + 0.4f * surge_);
    set1f(progSmoke_, "uCov", 0.56f - 0.04f * wm - 0.06f * wp - 0.05f * surge_);
    set1f(progSmoke_, "uExt", 1.0f);
    set1f(progSmoke_, "uShadowK", 1.6f);
    set3f(progSmoke_, "uFlow", flow_);
    set1f(progSmoke_, "uSwirl", swirl_);
    set4f(progSmoke_, "uWell", -WELL_X, -WELL_Y, WELL_X, WELL_Y);
    set1f(progSmoke_, "uBlast", 2.f + 22.f * smoothstepf(0.f, 3.f, blastT_));
    set1f(progSmoke_, "uBlastA", blast * 0.9f);
    set1f(progSmoke_, "uThick", thick * 1.0f + overFog_ * 0.3f);
    set1f(progSmoke_, "uSeed", seedF_);
    glDisable(GL_BLEND);
    ugl::drawFullscreen();
}

void SmokeUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const Palette target = paletteOf(variant_);
        if (!palSet_) { pal_ = target; palSet_ = true; }
        else lerpPalette(pal_, target, 1.f - std::exp(-dt * 1.2f));
    }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 1.2f, dt);
    cur_ = pal_;
    if (slow_ > 0.001f) lerpPalette(cur_, pinOf(pal_), smoothstepf(0.f, 1.f, slow_));
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.55f : 0.f), 3.f, dt);
    if (overT_ >= 0.f) {
        if (!game.over()) overT_ = -1.f;
        else overT_ += dt;
    }
    // game over: the lamp dies (its filament cooling to orange), then the smoke rises
    over_ = overT_ >= 0.f ? smoothstepf(0.6f, 3.2f, overT_) * 0.85f : approach(over_, 0.f, 1.5f, dt);
    if (over_ > 0.001f) cur_.light = lerp(cur_.light, lin(0xff6a24), smoothstepf(0.f, 0.6f, over_));
    overFog_ = approach(overFog_, overT_ > 1.5f ? 0.55f : 0.f, overT_ >= 0.f ? 0.12f : 1.5f, dt);
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.2f, slow_);
    if (!f.paused) simulate(game, music, dt, wdt);
    const float world = 1.f - saturate(bridge_.amount);
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    (void)wm;
    (void)wp;

    // ---- The smoke (reduced resolution).
    drawSmoke();

    // ---- Geometry.
    tiles_.clear();
    buildBoard(game);
    buildFx();

    const vec3 lp = lampPos(), ax = lampAxis();
    const float cone = cur_.cone * (1.f + 0.08f * wm + 0.3f * wp + iris_);
    const float lightOn = 1.f - over_;

    // ---- The stage at full resolution.
    glBindFramebuffer(GL_FRAMEBUFFER, sceneT_.fbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    {
        glUseProgram(progComp_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, smokeT_.tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        set1i(progComp_, "uSmokeT", 0);
        set1i(progComp_, "uNoise", 1);
        set2f(progComp_, "uSmokeTexel", 1.f / smokeT_.w, 1.f / smokeT_.h);
        set4f(progComp_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set1f(progComp_, "uPx", px_);
        set1f(progComp_, "uTime", worldTime_);
        set4f(progComp_, "uWell", -WELL_X, -WELL_Y, WELL_X, WELL_Y);
        set3f(progComp_, "uLPos", lp);
        set3f(progComp_, "uLAxis", ax);
        set1f(progComp_, "uCone", cone);
        set3f(progComp_, "uPanel", cur_.panel);
        set3f(progComp_, "uRim", cur_.rim * lightOn);
        set3f(progComp_, "uFill", cur_.fill);
        set3f(progComp_, "uLCol", cur_.light);
        set3f(progComp_, "uWallCol", cur_.smoke);
        set1f(progComp_, "uGain", cur_.gain * lightOn * (0.8f + 0.4f * wp + 0.5f * swell_));
        set1f(progComp_, "uWall", 0.05f);
        set1f(progComp_, "uPanelAlpha", 0.96f);
        set1f(progComp_, "uOverFog", overFog_);
        set3f(progComp_, "uMist", cur_.fill * 1.5f + cur_.light * 0.01f);
        set1f(progComp_, "uBridge", saturate(bridge_.amount));
        set3f(progComp_, "uBridgeCol", bridge_.color);
        set1f(progComp_, "uWorld", world);
        ugl::drawFullscreen();
        glActiveTexture(GL_TEXTURE0);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    auto drawSprites = [&](const std::vector<Sprite>& v) {
        if (v.empty()) return;
        glUseProgram(progSprite_);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        set1i(progSprite_, "uNoise", 0);
        setMat(progSprite_, "uVP", vp_);
        set1f(progSprite_, "uBridge", saturate(bridge_.amount));
        glBindVertexArray(spriteVao_);
        glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Sprite), v.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)v.size());
    };
    drawSprites(motes_);
    drawSprites(sprites_); // the smoke behind the blocks
    if (!tiles_.empty()) {
        glUseProgram(progTile_);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        set1i(progTile_, "uNoise", 0);
        setMat(progTile_, "uVP", vp_);
        set1f(progTile_, "uPx", px_);
        set1f(progTile_, "uTime", worldTime_);
        set3f(progTile_, "uLPos", lp);
        set3f(progTile_, "uLAxis", ax);
        set1f(progTile_, "uCone", cone * 1.15f);
        // the blocks take a touch of the lamp's color, normalized so the colors stay true
        auto tint = [](const vec3& c, float sat) {
            const float m = std::max(1e-3f, std::max(c.x, std::max(c.y, c.z)));
            return lerp(vec3(1.f), c / m, sat);
        };
        const float keyOn = lerpf(0.25f, 1.f, lightOn);
        set3f(progTile_, "uTint", tint(cur_.light, 0.18f) * 1.05f);
        set3f(progTile_, "uKey", lerp(cur_.light, vec3(1.f), 0.45f));
        set1f(progTile_, "uKeyGain", keyOn);
        set3f(progTile_, "uFill", cur_.fill);
        set3f(progTile_, "uRim", cur_.rim);
        set3f(progTile_, "uGaugeLit", lerp(cur_.light, vec3(1.f), 0.3f) * 1.2f);
        set3f(progTile_, "uGaugeBlade", lerp(cur_.panel, cur_.ink, 0.12f));
        set3f(progTile_, "uGaugeSlow", lin(0xc9d6ff) * 0.6f);
        set1f(progTile_, "uSlow", slow_);
        set1f(progTile_, "uBridge", saturate(bridge_.amount));
        set3f(progTile_, "uBridgeInk", bridge_.ink);
        glBindVertexArray(tileVao_);
        glBindBuffer(GL_ARRAY_BUFFER, tileVbo_);
        glBufferData(GL_ARRAY_BUFFER, tiles_.size() * sizeof(Tile), tiles_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)tiles_.size());
    }
    drawSprites(blades_);
    drawHud(f.hud, f.panels);
    glDisable(GL_BLEND);

    // ---- Bloom: the beam's core, the rims, the blades of light.
    glUseProgram(progDown_);
    set1i(progDown_, "uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    GLuint src = sceneT_.tex;
    int sw = width_, sh = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        bloomT_[i].bind();
        glBindTexture(GL_TEXTURE_2D, src);
        set2f(progDown_, "uTexel", 0.5f / sw, 0.5f / sh);
        set1f(progDown_, "uThreshold", i == 0 ? 0.7f : 0.f);
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
    set1f(progPost_, "uBloomAmt", 0.5f * world);
    set1f(progPost_, "uExposure", 1.f);
    set1f(progPost_, "uGrain", 0.018f);
    set3f(progPost_, "uToe", cur_.fill * 0.0f);
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_smoke() {
    static const UniverseEntry e{
        "SMOKE", "a dark stage, slow smoke and one hard spotlight", [] { return std::unique_ptr<Universe>(new SmokeUniverse()); },
        // Dark, heavy songs suit the stage best (bass in the mix, a dark timbre, slow or mid tempo); everything still plays.
        [](const SongLook& l) {
            return 0.6f + 0.35f * l.bassWeight + 0.3f * (1.f - l.brightness) + 0.15f * (1.f - smoothstepf(110.f, 160.f, l.bpm));
        },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
