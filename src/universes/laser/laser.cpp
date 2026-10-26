// LASER SHOW: a few laser beams in a dark, hazy room.
//
// One world: a black room filled with a light haze, three projectors out of sight (one low on each side below the
// frame, one hidden behind the well). Their beams are thin, pure lines of colored light, made visible only by the haze
// they cross: uneven along their length, a soft glow around them where the haze is thick. The well is a slab of black
// glass standing in front of the beams (they pass behind it; its rim catches each beam where it crosses); the pieces
// are dark acrylic, edge-lit: a smoked, tinted body whose silhouette glows in the piece's color (one shape per piece,
// no seam inside it).
//
// The song drives the projectors, slowly: calm is one or two thin beams through faint haze, pivoting slowly; mid
// splits them into a few beams per side that pivot with the bands (the bass leans the left fan, the highs the right
// one) and breathe every two beats; a build gathers them toward one point above the well; a peak without a drop (a
// chorus) opens five beams per side, swaying on the bars. A drop opens full laser fans from all three projectors in
// the drop's own colors (each laser set has its own), bursting open from a bundle then sweeping slowly for the
// whole drop, the haze glowing in the new color (one swell on the downbeat, settling over seconds). The light of the
// room never pulses with the beat.
//
// A lock leaves a small point of light where the piece touched down; a cleared row burns out (the blocks' edges
// flare, then a burning front eats each one from its edges in, gone in under half a second, at the real speed even in
// slow motion); a Tetris is a salute (every projector opens one white fan, which sweeps once and closes); slow motion
// freezes the beams in a thick, cold haze (the lasers turn ice white, like rods of light); game over switches the
// lasers off one by one while the stack goes dark row by row. A change of universe fills the room with haze.
//
// Cost: the haze glow is drawn at a reduced resolution (at most 360 lines); the beams' cores, the well, the blocks
// and the text at full resolution.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane, so the well, the
// previews and the HUD anchors sit exactly where the layout puts them.
#include "universes/laser/laser.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/laser/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Jura Light (most of the HUD) and Jura Medium (numbers, the section), SIL Open Font License, see OFL-Jura.txt.
const unsigned char FONT_LIGHT[] = {
#include "universes/laser/Jura-Light.ttf.inc"
};
const unsigned char FONT_MEDIUM[] = {
#include "universes/laser/Jura-Medium.ttf.inc"
};

namespace shaders = laser_shaders;
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
constexpr float DEG = PI / 180.f;

vec2 v2(float x, float y) { return vec2{x, y}; }

vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }

// The laser sets (sRGB here, linear once loaded): the song's lasers (up to three, the fans' slots take them in
// turn), the drop's lasers, the room's haze, the well's rim, the HUD's ink and accent, and the seven acrylics
// (I O T S Z J L).
struct VariantDef {
    const char* name;
    unsigned laser[3], drop[3];
    unsigned haze, rim, ink, accent;
    unsigned piece[7];
};
const VariantDef VARIANTS[] = {
    // the classic 532 nm green; the drop turns cyan
    {"GREEN", {0x2cff5c, 0x2cff5c, 0x2cff5c}, {0x30d8ff, 0x2aa8ff, 0x30d8ff}, 0x030806, 0x6dffa0, 0xdcefe2, 0x59ff8a,
     {0x6fe6ff, 0xfff08a, 0xc79cff, 0x9cff8a, 0xff7f8c, 0x7e9bff, 0xffb070}},
    // white lasers (red, green and blue mixed); the drop splits them into pure red, green and blue
    {"RGB WHITE", {0xf4f2ff, 0xf4f2ff, 0xf4f2ff}, {0xff2a3a, 0x2cff6a, 0x3a6cff}, 0x050506, 0xe8ecff, 0xeeeef2, 0xdfe4ff,
     {0x8fd6f2, 0xf2e2a0, 0xc6aef2, 0xa6e6b0, 0xf0a0ac, 0x9aaef2, 0xf2c296}},
    // magenta and cyan in turn; the drop turns gold
    {"MAGENTA CYAN", {0xff2ad8, 0x22eaff, 0xff2ad8}, {0xffc23a, 0xffa82a, 0xffc23a}, 0x070409, 0xff8cf0, 0xf2e2f6, 0xff5ae6,
     {0x4ff0ff, 0xfff27a, 0xff6ae8, 0x6affb4, 0xff5a6e, 0x8a7cff, 0xffa65a}},
    // one deep red; the drop turns amber
    {"RED", {0xff1e14, 0xff1e14, 0xff1e14}, {0xffa018, 0xffc040, 0xffa018}, 0x090303, 0xff7060, 0xf2dcd6, 0xff4a38,
     {0x9fe6f5, 0xffe2a6, 0xe2b0ff, 0xb8f2a6, 0xff8a6a, 0x9cb4ff, 0xffbe8a}},
    // 445 nm blue and 405 nm violet; the drop turns aqua
    {"BLUE VIOLET", {0x2a4cff, 0x9a3cff, 0x2a4cff}, {0x22ffd0, 0x22e0ff, 0x22ffd0}, 0x040410, 0x8aa0ff, 0xe0e4f8, 0x6c7cff,
     {0x72f4ff, 0xffe680, 0xe490ff, 0x86f0a0, 0xff7aa8, 0x6e8cff, 0xffa47a}},
    // sodium amber; the drop turns green
    {"AMBER", {0xffa20e, 0xffa20e, 0xffa20e}, {0x46ff6a, 0x9cff3a, 0x46ff6a}, 0x080603, 0xffc070, 0xf4ead8, 0xffb43a,
     {0x7fdcff, 0xfff0b0, 0xd2a0ff, 0xb2f07a, 0xff7462, 0x8ea2ff, 0xffc88a}},
    // aqua; the drop turns rose
    {"AQUA", {0x14f0d2, 0x14f0d2, 0x14f0d2}, {0xff3c96, 0xff5ad0, 0xff3c96}, 0x030807, 0x7affea, 0xdcf2ee, 0x3cf5dc,
     {0xa0f4ff, 0xffd870, 0xbf8cff, 0x8affc0, 0xff6f8a, 0x7896ff, 0xffa060}},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

using Palette = LaserUniverse::Palette;
static_assert(sizeof(Palette) % sizeof(float) == 0, "a palette is floats only");
constexpr int PAL_FLOATS = (int)(sizeof(Palette) / sizeof(float));

vec3 lin(unsigned c) { return ugl::toLinear(hex(c)); }

Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    Palette p;
    for (int i = 0; i < 3; i++) {
        p.laser[i] = lin(d.laser[i]);
        p.dropLaser[i] = lin(d.drop[i]);
    }
    p.haze = lin(d.haze);
    p.rim = lin(d.rim);
    p.ink = lin(d.ink);
    p.accent = lin(d.accent);
    for (int i = 0; i < 7; i++) p.piece[i] = lin(d.piece[i]);
    return p;
}

void lerpPalette(Palette& a, const Palette& b, float k) {
    float* pa = reinterpret_cast<float*>(&a);
    const float* pb = reinterpret_cast<const float*>(&b);
    for (int i = 0; i < PAL_FLOATS; i++) pa[i] += (pb[i] - pa[i]) * k;
}

std::string upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)std::toupper((unsigned char)c);
    return o;
}

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

// A bell over [0, 1]: rises over `in`, holds, falls over `out`.
float bell(float t, float in, float hold, float out) {
    if (t < 0.f) return 0.f;
    return smoothstepf(0.f, in, t) * (1.f - smoothstepf(in + hold, in + hold + out, t));
}

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool LaserUniverse::init(int width, int height) {
    progHaze_ = compile(ugl::FULLSCREEN_VS, shaders::HAZE_FS, "laser haze");
    progComp_ = compile(ugl::FULLSCREEN_VS, shaders::COMPOSITE_FS, "laser composite");
    progBeam_ = compile(shaders::BEAM_VS, shaders::BEAM_FS, "laser beam");
    progWell_ = compile(ugl::FULLSCREEN_VS, shaders::WELL_FS, "laser well");
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "laser shape");
    progTile_ = compile(shaders::TILE_VS, shaders::TILE_FS, "laser tile");
    progSpark_ = compile(shaders::SPARK_VS, shaders::SPARK_FS, "laser spark");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "laser text");
    progDown_ = compile(ugl::FULLSCREEN_VS, shaders::DOWN_FS, "laser down");
    progUp_ = compile(ugl::FULLSCREEN_VS, shaders::UP_FS, "laser up");
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "laser post");

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
    instanced(beamVao_, beamVbo_, 3, sizeof(BeamGpu));
    instanced(sparkVao_, sparkVbo_, 3, sizeof(Spark));

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
    buildFont(fontMedium_, FONT_MEDIUM);
    pal_ = paletteOf(0);
    // the projectors: low on each side below the frame, and one hidden behind the well
    em_[0].origin = v2(-20.5f, -14.5f);
    em_[1].origin = v2(20.5f, -14.5f);
    em_[2].origin = v2(0.f, -3.f);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// The noise: a tileable 64^3 volume for the haze (r an fbm, g a finer one) and a tileable 256^2 texture (the burn,
// the bridge's haze front).
void LaserUniverse::buildNoise() {
    auto hash3 = [](int x, int y, int z, int s) {
        return splitmix64((uint64_t)(uint32_t)x * 73856093ull ^ (uint64_t)(uint32_t)y * 19349663ull ^
                          (uint64_t)(uint32_t)z * 83492791ull ^ (uint64_t)s * 2654435761ull);
    };
    auto perlin3 = [&](float x, float y, float z, int period, int seed) {
        const int x0 = (int)std::floor(x), y0 = (int)std::floor(y), z0 = (int)std::floor(z);
        const float fx = x - x0, fy = y - y0, fz = z - z0;
        auto grad = [&](int ix, int iy, int iz, float dx, float dy, float dz) {
            const uint64_t h = hash3(((ix % period) + period) % period, ((iy % period) + period) % period,
                                     ((iz % period) + period) % period, seed);
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
    auto q8 = [](float v) { return (unsigned char)std::clamp((int)(v * 255.f + 0.5f), 0, 255); };
    const int N = 64;
    std::vector<unsigned char> vol((size_t)N * N * N * 4);
    for (int z = 0; z < N; z++)
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                const float u = (x + 0.5f) / N, v = (y + 0.5f) / N, w = (z + 0.5f) / N;
                const float r = 0.5f + 0.7f * (perlin3(u * 4, v * 4, w * 4, 4, 1) * 0.62f + perlin3(u * 8, v * 8, w * 8, 8, 2) * 0.28f +
                                               perlin3(u * 16, v * 16, w * 16, 16, 3) * 0.1f);
                const float g = 0.5f + 0.75f * (perlin3(u * 8, v * 8, w * 8, 8, 4) * 0.7f + perlin3(u * 16, v * 16, w * 16, 16, 5) * 0.3f);
                unsigned char* p = &vol[(((size_t)z * N + y) * N + x) * 4];
                p[0] = q8(r); p[1] = q8(g); p[2] = 128; p[3] = 255;
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
void LaserUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] laser font init failed\n");
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

void LaserUniverse::resize(int width, int height) {
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

void LaserUniverse::createTargets() {
    // The haze glow at most 360 lines (it is soft; the beams' cores are drawn at full resolution).
    const int sh = std::max(90, std::min(360, height_ / 2));
    const int sw = std::max(160, (int)std::lround((float)sh * width_ / height_));
    hazeT_.create(sw, sh, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    sceneT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    int w = width_, h = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        bloomT_[i].create(w, h, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

LaserUniverse::~LaserUniverse() {
    hazeT_.destroy();
    sceneT_.destroy();
    for (auto& t : bloomT_) t.destroy();
    for (GLuint p : {progHaze_, progComp_, progBeam_, progWell_, progShape_, progTile_, progSpark_, progText_, progDown_, progUp_, progPost_})
        glDeleteProgram(p);
    for (GLuint v : {tileVao_, beamVao_, sparkVao_, shapeVao_, textVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, tileVbo_, beamVbo_, sparkVbo_, shapeVbo_, textVbo_}) glDeleteBuffers(1, &b);
    if (volTex_) glDeleteTextures(1, &volTex_);
    if (noiseTex_) glDeleteTextures(1, &noiseTex_);
    for (Font* f : {&fontLight_, &fontMedium_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// =====================================================================================================
// Song, level, drop, bonus
// =====================================================================================================

// The lasers from the song: dark minor songs get the red, the violet or the magenta, bright major ones the green,
// the white or the amber; the key's hue picks among them.
int LaserUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    const bool dark = t.hasSong && t.minor && t.brightness < 0.45f;
    const bool bright = t.hasSong && !t.minor && t.brightness > 0.55f;
    int v;
    if (dark) v = h < 0.33f ? 3 : (h < 0.66f ? 4 : 2);
    else if (bright) v = h < 0.33f ? 0 : (h < 0.66f ? 1 : 5);
    else v = std::min(VARIANT_COUNT - 1, (int)(h * VARIANT_COUNT));
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void LaserUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    seedF_ = (float)(look.seed % 1000) * 0.137f;
    rng_ = Rng(look.seed ^ 0x1A5Eull);
    calmSide_ = (int)((look.seed >> 7) & 1);
    flow_ = vec3(rng_.range(0.f, 1.f), rng_.range(0.f, 1.f), rng_.range(0.f, 1.f));
    if (seconds <= 0.f) palSet_ = false; // snap on the first frame
}

void LaserUniverse::setLevel(int level, float energy, float seconds, float impact) {
    level = std::clamp(level, 0, 2);
    energy_ = energy;
    const bool rising = level > level_;
    if (!hasLevel_ || seconds < 0.1f) {
        // instant (first frame, viewer): the moment's shape at once
        hasLevel_ = true;
        level_ = level;
        wCalm_ = level == 0 ? 1.f : 0.f;
        wMid_ = level == 1 ? 1.f : 0.f;
        wChorus_ = level == 2 ? 1.f : 0.f;
        wDrop_ = 0.f;
        inDrop_ = false;
        dropWait_ = -1.f;
        return;
    }
    level_ = level;
    if (level < 2) {
        inDrop_ = false;
        dropWait_ = -1.f;
    } else if (rising && impact >= 0.75f) {
        dropWait_ = 0.f; // into a peak: the fans open on the drop's downbeat (drop())
    }
}

// The drop's downbeat: the fans burst open (a drop: full fans from every projector in the drop's colors; a chorus:
// five beams a side). One swell of the haze, settling over seconds.
void LaserUniverse::drop(float strength) {
    if (level_ < 2) return;
    dropWait_ = -1.f;
    const float s = saturate(strength);
    if (sig_ > 1.33f) {
        inDrop_ = true;
        dropT_ = 0.f;
        swell_ = std::max(swell_, 0.8f * s);
    } else {
        chorusT_ = 0.f;
        swell_ = std::max(swell_, 0.35f * s);
    }
}

void LaserUniverse::refresh(float strength) { swell_ = std::max(swell_, 0.25f * strength); }

void LaserUniverse::setBonus(bool on) { slowOn_ = on; }

void LaserUniverse::levelUp() { swell_ = std::max(swell_, 0.4f); }

vec2 LaserUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints LaserUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    h.background = p.haze * 0.5f + p.laser[0] * 0.01f;
    h.light = false;
    h.ink = lerp(p.ink, p.laser[0], 0.2f) * 0.8f;
    h.accent = p.laser[0];
    h.styles = TS_INK | TS_LUMA; // the haze filling the room, darks first
    return h;
}

// =====================================================================================================
// Game events
// =====================================================================================================

void LaserUniverse::onEvent(const GameEvent& ev, const Game& game) {
    switch (ev.type) {
    case GameEvent::Lock: {
        if (ev.cells.empty()) break;
        // the piece's cells keep its identity (one acrylic shape, never joined with another piece)
        const int id = pieceCounter_++;
        for (auto& c : ev.cells)
            if (c.y >= 0 && c.y < Game::H && c.x >= 0 && c.x < Game::W) own_[c.y][c.x] = {id, c.type};
        // a point of light where it touched down: its resting cells' bottom edge
        auto inPiece = [&](int x, int y) {
            for (auto& o : ev.cells)
                if (o.x == x && o.y == y) return true;
            return false;
        };
        float x0 = 1e9f, x1 = -1e9f, yb = -1e9f;
        int n = 0;
        for (auto& q : ev.cells) {
            if (inPiece(q.x, q.y + 1)) continue;
            if (q.y + 1 < Game::H && game.cell(q.x, q.y + 1).type < 0) continue;
            const vec3 p = layout::cellPos((float)q.x, (float)q.y);
            x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); yb = std::max(yb, -p.y);
            n++;
        }
        if (n == 0)
            for (auto& q : ev.cells) {
                const vec3 p = layout::cellPos((float)q.x, (float)q.y);
                x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); yb = std::max(yb, -p.y);
            }
        const float hard = lastDrop_ >= 2 ? 1.f : 0.f;
        const vec3 pc = pal_.piece[std::clamp(ev.cells[0].type, 0, 6)];
        sparkFx_.push_back({v2(0.5f * (x0 + x1), -yb - 0.5f), 0.f, 0.22f + 0.08f * hard, 0.5f + 0.15f * hard,
                            0.5f * (x1 - x0) + 0.5f + 0.4f * hard, lerp(pc, vec3(1.f), 0.45f) * (1.6f + 0.8f * hard)});
        lastDrop_ = 0;
        break;
    }
    case GameEvent::HardDrop:
        lastDrop_ = ev.count;
        break;
    case GameEvent::Clear: {
        // the blocks burn out: each keeps its piece's shape within the row while it burns
        auto inClear = [&](int x, int y) {
            for (auto& o : ev.cells)
                if (o.x == x && o.y == y) return true;
            return false;
        };
        const float seed = rng_.range(0.f, 50.f);
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0.f; d.type = std::clamp(c.type, 0, 6); d.over = false; d.seed = seed;
            d.delay = std::fabs(c.x - 4.5f) * 0.012f;
            const int id = (c.y >= 0 && c.y < Game::H) ? own_[c.y][c.x].id : -1;
            d.mask = id < 0 ? 0 : neighbourMask([&](int dx, int dy) {
                const int nx = c.x + dx, ny = c.y + dy;
                if (nx < 0 || nx >= Game::W || ny < 0 || ny >= Game::H || !inClear(nx, ny)) return false;
                return own_[ny][nx].id == id;
            });
            dying_.push_back(d);
        }
        // the owners follow the rows down
        {
            bool cleared[Game::H] = {};
            for (auto& c : ev.cells)
                if (c.y >= 0 && c.y < Game::H) cleared[c.y] = true;
            Owner next[Game::H][Game::W];
            int dst = Game::H - 1;
            for (int y = Game::H - 1; y >= 0; y--) {
                if (cleared[y]) continue;
                for (int x = 0; x < Game::W; x++) next[dst][x] = own_[y][x];
                dst--;
            }
            for (int y = 0; y < Game::H; y++)
                for (int x = 0; x < Game::W; x++) own_[y][x] = next[y][x];
        }
        if (ev.count >= 4) {
            saluteT_ = 0.f;
            swell_ = std::max(swell_, 0.6f);
        }
        break;
    }
    case GameEvent::TopOut: {
        // the stack goes dark row by row from the top while the lasers switch off
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            Dying d;
            d.x = p.x; d.y = p.y; d.t = 0.f; d.type = std::clamp(c.type, 0, 6); d.over = true;
            d.delay = (c.y - top) * 0.08f;
            d.seed = 0.f;
            const int id = (c.y >= 0 && c.y < Game::H) ? own_[c.y][c.x].id : -1;
            d.mask = id < 0 ? 0 : neighbourMask([&](int dx, int dy) {
                const int nx = c.x + dx, ny = c.y + dy;
                if (nx < 0 || nx >= Game::W || ny < 0 || ny >= Game::H) return false;
                return own_[ny][nx].id == id;
            });
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

// The projectors follow the moment: which beams of each fan are on, how wide the fan is and where it points.
void LaserUniverse::updateEmitters(const MusicState& music, float dt, float wdt) {
    const float freeze = 1.f - 0.96f * slow_; // slow motion: the beams stand still
    // the beat clock (slowed with the slow motion), and a breath every two beats
    {
        double d = music.beat - lastBeat_;
        if (lastBeat_ < 0.0 || d < 0.0 || d > 1.0) d = 0.0;
        lastBeat_ = music.beat;
        beatClock_ += (float)d * freeze;
        const float half = (float)std::floor(music.beat / 2.0);
        if (half != lastHalf_ && slow_ < 0.5f) {
            stepPhase_ = stepPhase_ >= 0.f ? -1.f : 1.f;
            lastHalf_ = half;
        }
        stepShown_ = approach(stepShown_, stepPhase_, 4.f, dt);
    }
    const float wActive = wMid_ + wChorus_ + wDrop_;
    // the bands: the bass leans the left fan, the highs the right one (slowly)
    {
        float lo = 0.f, hi = 0.f;
        for (int b = 0; b < 4; b++) lo += music.bandsFast[b];
        for (int b = NUM_BANDS - 6; b < NUM_BANDS; b++) hi += music.bandsFast[b];
        lo = saturate(std::max(lo / 4.f, music.audio.bass));
        hi = saturate(hi / 6.f * 1.6f);
        bass_ = approach(bass_, lo, 0.8f * freeze, dt);
        high_ = approach(high_, hi, 0.8f * freeze, dt);
    }
    const float bc = beatClock_;
    const float sway8 = std::sin(bc * TAU / 32.f), sway8b = std::sin(bc * TAU / 32.f + 1.9f);
    const float sway4 = std::sin(bc * TAU / 16.f + 0.7f);
    const float calmDrift = std::sin(worldTime_ * 0.09f + seedF_);

    // the drop's fans burst open from a bundle (fast, then slower and slower)
    const float open = 0.08f + 0.92f * (1.f - std::exp(-dropT_ / 0.45f));
    const float chorusOpen = 0.3f + 0.7f * (1.f - std::exp(-chorusT_ / 0.6f));
    const float sal = bell(saluteT_, 0.35f, 0.6f, 1.4f);
    const float salOpen = 1.f - std::exp(-saluteT_ / 0.4f);

    for (int e = 0; e < EMITTERS; e++) {
        Emitter& E = em_[e];
        const bool side = e < 2;
        const float mir = e == 1 ? -1.f : 1.f;
        float target[SLOTS] = {};
        float center = 0.f, spread = 0.f;
        if (side) {
            // calm: one beam (the other side a faint second one); mid: two a side; chorus: five; drop: all
            const float lone = e == calmSide_ ? 1.f : 0.45f;
            for (int i = 0; i < SLOTS; i++) {
                const int k = e == 1 ? SLOTS - 1 - i : i;
                float v = 0.f;
                if (k == 7) v += wCalm_ * lone;
                if (k == 4 || k == 10) v += wMid_;
                if (k == 1 || k == 4 || k == 7 || k == 10 || k == 13) v += wChorus_;
                if (k % 2 == 0) v += wDrop_ * 0.9f;
                target[i] = std::min(1.f, v);
            }
            const float bandLean = (e == 0 ? bass_ : high_) - 0.35f;
            const float cCalm = (6.f + 5.f * calmDrift * mir) * DEG;
            const float cMid = (16.f + 4.f * sway8 + 12.f * bandLean) * DEG;
            const float cChorus = (22.f + 9.f * (e == 0 ? sway8 : sway8b) + 8.f * bandLean) * DEG;
            const float cDrop = (7.f + 7.f * (e == 0 ? sway8 : sway8b)) * DEG;
            center = wCalm_ * cCalm + wMid_ * cMid + wChorus_ * cChorus + wDrop_ * cDrop;
            const float sMid = (30.f + 2.f * stepShown_) * DEG;
            const float sChorus = (58.f + 4.f * stepShown_) * DEG * chorusOpen;
            const float sDrop = (56.f + 3.f * stepShown_ + 4.f * sway4) * DEG * open;
            spread = wMid_ * sMid + wChorus_ * sChorus + wDrop_ * sDrop;
            // a build gathers the beams toward one point above the well
            if (tension_ > 0.001f) {
                const float conv = std::atan2(std::fabs(E.origin.x), 11.5f - E.origin.y);
                center = lerpf(center, conv, tension_ * 0.85f);
                spread = lerpf(spread, 5.f * DEG, tension_ * 0.8f);
            }
            // a Tetris: every projector salutes with a full fan
            if (sal > 0.001f) {
                for (int i = 0; i < SLOTS; i++) target[i] = std::max(target[i], sal);
                center = lerpf(center, (24.f + 30.f * (salOpen - 0.5f)) * DEG, sal);
                spread = lerpf(spread, 100.f * DEG * salOpen, sal);
            }
        } else {
            // the projector behind the well: only in a drop (and a salute), a fan rising out of the well
            for (int i = 0; i < SLOTS; i++) target[i] = (i % 4 == 1 || i == 7) ? wDrop_ * 0.85f : 0.f;
            center = 8.f * DEG * sway4 * wDrop_;
            spread = (70.f + 4.f * stepShown_) * DEG * open * wDrop_;
            if (sal > 0.001f) {
                for (int i = 0; i < SLOTS; i++) target[i] = std::max(target[i], (i % 2 == 1) ? sal : 0.f);
                center = lerpf(center, (40.f * (salOpen - 0.5f)) * DEG, sal);
                spread = lerpf(spread, 110.f * DEG * salOpen, sal);
            }
        }
        // the beams fade in and out over a second or so (a drop's open in a beat)
        for (int i = 0; i < SLOTS; i++) {
            const float rate = target[i] > E.vis[i] ? (dropT_ < 0.6f || saluteT_ < 0.5f ? 9.f : 1.6f) : 0.9f;
            E.vis[i] = approach(E.vis[i], target[i], rate, dt);
        }
        // the fan's shape: eased (except the burst of a drop, which the weights carry)
        const float k = 1.f - std::exp(-dt * 3.f * freeze);
        E.center += (center - E.center) * k;
        E.spread += (spread - E.spread) * (dropT_ < 1.5f || saluteT_ < 1.f ? 1.f - std::exp(-dt * 14.f * freeze) : k);
        (void)wActive;
        (void)wdt;
    }
}

void LaserUniverse::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    worldTime_ += wdt;
    // the section's pace and glow: a drop runs faster and brighter than a chorus
    sig_ = 0.5f * (music.speed + music.glow);
    if (level_ >= 2) {
        if (dropWait_ >= 0.f) {
            dropWait_ += dt;
            if (dropWait_ > 1.2f) drop(0.6f); // no downbeat came (a seek, no beat grid): open anyway
        } else if (!inDrop_ && sig_ > 1.36f) {
            inDrop_ = true;
            dropT_ = 0.f;
            swell_ = std::max(swell_, 0.6f);
        } else if (inDrop_ && sig_ < 1.24f && dropT_ > 4.f) {
            inDrop_ = false; // the drop gave way to a chorus inside the same peak
            chorusT_ = 0.f;
        }
    }
    // the moment's shape: weights eased (rising quickly, falling slowly)
    {
        const bool waiting = dropWait_ >= 0.f;
        const float tCalm = level_ == 0 ? 1.f : 0.f;
        const float tMid = level_ == 1 || (level_ == 2 && waiting) ? 1.f : 0.f;
        const float tChorus = level_ == 2 && !inDrop_ && !waiting ? 1.f : 0.f;
        const float tDrop = level_ == 2 && inDrop_ ? 1.f : 0.f;
        auto ease = [&](float& w, float t, float up, float down) { w = approach(w, t, t > w ? up : down, dt); };
        ease(wCalm_, tCalm, 1.2f, 0.7f);
        ease(wMid_, tMid, 1.2f, 0.8f);
        ease(wChorus_, tChorus, 2.0f, 0.7f);
        ease(wDrop_, tDrop, dropT_ < 1.f ? 7.f : 1.5f, 0.5f);
        dropColor_ = approach(dropColor_, tDrop, tDrop > dropColor_ ? 6.f : 0.6f, dt);
        // a build: the pace rising toward the peak gathers the beams
        const float tens = level_ == 1 ? smoothstepf(1.0f, 1.42f, music.speed) : 0.f;
        tension_ = approach(tension_, tens, tens > tension_ ? 0.8f : 3.f, dt);
    }
    dropT_ += dt;
    chorusT_ += dt;
    saluteT_ += dt;
    swell_ = approach(swell_, 0.f, 0.45f, dt);
    flow_.x += wdt * 0.0035f;
    flow_.y += wdt * 0.0022f;
    flow_.z += wdt * 0.004f;
    updateEmitters(music, dt, wdt);

    // effects at the real speed (a line clear must not linger in slow motion)
    for (auto& s : sparkFx_) s.t += dt;
    sparkFx_.erase(std::remove_if(sparkFx_.begin(), sparkFx_.end(), [](const SparkFx& s) { return s.t > s.life; }), sparkFx_.end());
    for (auto& d : dying_) d.t += dt;
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return d.t > d.delay + (d.over ? 9.f : 0.45f); }),
                 dying_.end());
    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
}

// The beams of this frame, from the projectors' fans.
void LaserUniverse::buildBeams() {
    beams_.clear();
    const float cold = smoothstepf(0.f, 1.f, slow_);
    const float sal = bell(saluteT_, 0.35f, 0.6f, 1.4f);
    const float off = 1.f - smoothstepf(0.1f, 0.7f, bridge_.amount);
    // the beams' strength by moment (many beams in a drop: each a little less)
    const float gain = (wCalm_ * 0.7f + wMid_ * 0.9f + wChorus_ * 1.0f + wDrop_ * 0.8f + 0.0001f) /
                       (wCalm_ + wMid_ + wChorus_ + wDrop_ + 0.0001f);
    const vec3 ice = lin(0xd6e6ff);
    for (int e = 0; e < EMITTERS; e++) {
        const Emitter& E = em_[e];
        const float mir = e == 1 ? -1.f : 1.f;
        for (int i = 0; i < SLOTS; i++) {
            float v = E.vis[i];
            if (v < 0.004f) continue;
            // game over: the lasers switch off one by one
            if (overT_ >= 0.f) {
                const float at = 0.3f + 2.4f * std::fmod((float)(i * 7 + e * 5) * 0.618f, 1.f);
                v *= 1.f - smoothstepf(at, at + 0.2f, overT_);
                if (v < 0.004f) continue;
            }
            const float u = (float)i / (SLOTS - 1) - 0.5f;
            const float a = E.center + E.spread * u;
            const vec2 d = v2(std::sin(a) * mir, std::cos(a));
            const int ci = (i + e) % 3;
            vec3 c = lerp(pal_.laser[ci], pal_.dropLaser[ci], dropColor_);
            c = lerp(c, vec3(1.f), sal * 0.55f);
            c = lerp(c, ice * 0.8f, cold * 0.75f);
            const float I = 1.5f * gain * v * off * (1.f + 0.35f * cold);
            const float depth = std::fmod(i * 0.137f + e * 0.311f + 0.05f, 1.f);
            const float width = 0.02f * (1.f + 0.6f * cold);
            BeamGpu b;
            b.a0[0] = E.origin.x; b.a0[1] = E.origin.y; b.a0[2] = d.x; b.a0[3] = d.y;
            b.a1[0] = c.x * I; b.a1[1] = c.y * I; b.a1[2] = c.z * I; b.a1[3] = depth;
            b.a2[0] = width; b.a2[1] = 90.f; b.a2[2] = 0.f; b.a2[3] = 0.f;
            if ((int)beams_.size() < MAX_BEAMS) beams_.push_back(b);
        }
    }
}

// =====================================================================================================
// Geometry
// =====================================================================================================

void LaserUniverse::tile(float x, float y, float h, const vec3& c, float alpha, int kind, int mask, float flash, float scale,
                         float e0, float e1, float e2, float e3) {
    tiles_.push_back({{x, y, h, h}, {c.x, c.y, c.z, alpha}, {(float)kind, (float)mask, flash, scale}, {e0, e1, e2, e3}});
}

void LaserUniverse::buildBoard(const Game& g) {
    // Cells whose piece is unknown (a stack inherited from another universe, a prefilled board): grouped into shapes
    // by kind, at most four cells each, so they read as pieces rather than loose tiles.
    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const int t = g.cell(x, y).type;
            if (t < 0 || own_[y][x].type == t) continue;
            const int id = pieceCounter_++;
            int qx[4], qy[4], n = 0, head = 0;
            own_[y][x] = {id, t};
            qx[n] = x; qy[n] = y; n++;
            while (head < n && n < 4) {
                const int cx = qx[head], cy = qy[head];
                head++;
                const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
                for (int k = 0; k < 4 && n < 4; k++) {
                    const int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || nx >= Game::W || ny < 0 || ny >= Game::H) continue;
                    if (g.cell(nx, ny).type != t || own_[ny][nx].type == t) continue;
                    own_[ny][nx] = {id, t};
                    qx[n] = nx; qy[n] = ny; n++;
                }
            }
        }
    auto idAt = [&](int x, int y) {
        if (x < 0 || x >= Game::W || y < Game::HIDDEN || y >= Game::H) return -1;
        const int t = g.cell(x, y).type;
        return t >= 0 && own_[y][x].type == t ? own_[y][x].id : -1;
    };
    const float glowOn = 1.f;
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            const int id = idAt(x, y);
            const int mask = id < 0 ? 0 : neighbourMask([&](int dx, int dy) {
                if (idAt(x + dx, y + dy) != id) return false;
                return std::fabs(g.rowOffset(y + dy) - g.rowOffset(y)) < 0.01f;
            });
            const float age = c.flash > 0.f ? (1.f - c.flash) * Game::LOCK_FX_SECONDS : -1.f;
            tile(p.x, p.y, 0.6f, pal_.piece[c.type], 1.f, 0, mask, age, 1.f, glowOn);
        }
    // cleared blocks burning out; a topped-out stack going dark
    for (const Dying& d : dying_) {
        const float t = d.t - d.delay;
        if (d.over) {
            const float k = smoothstepf(0.f, 0.6f, t);
            const float a = 1.f - smoothstepf(5.f, 8.f, d.t);
            tile(d.x, d.y, 0.6f, pal_.piece[d.type], a, 0, d.mask, -1.f, 1.f, lerpf(1.f, 0.08f, k));
            continue;
        }
        const float k = saturate(t / 0.4f);
        if (k >= 1.f) continue;
        tile(d.x, d.y, 0.6f, pal_.piece[d.type], 1.f, 2, d.mask, -1.f, 1.f, k, d.seed + d.x * 0.37f);
    }
    // the falling piece and its ghost: one shape each
    if (g.hasPiece()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        auto maskIn = [&](int (*cs)[2], int x, int y) {
            return neighbourMask([&](int dx, int dy) {
                for (int k = 0; k < 4; k++)
                    if (cs[k][0] == x + dx && cs[k][1] == y + dy) return true;
                return false;
            });
        };
        const vec3 col = pal_.piece[p.type];
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (auto& c : gc) {
                if (c[1] < Game::HIDDEN) continue;
                const vec3 w = layout::cellPos((float)c[0], (float)c[1]);
                tile(w.x, w.y, 0.6f, col, 1.f, 1, maskIn(gc, c[0], c[1]), -1.f, 1.f);
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (auto& c : cells) {
            if (c[1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(c[0] + dx, c[1] + dy);
            tile(w.x, w.y, 0.6f, col, 1.f, 0, maskIn(cells, c[0], c[1]), -1.f, 1.f, 1.f);
        }
    }
    // previews
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
            const int mask = neighbourMask([&](int ddx, int ddy) {
                for (auto& o : cells)
                    if (o[0] == c[0] + ddx && o[1] == c[1] + ddy) return true;
                return false;
            });
            tile(pos.x, pos.y, 0.6f * sc, pal_.piece[type], alpha, 0, mask, -1.f, sc, 1.f);
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.4f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }
    // the bonus gauge: a ring of laser light
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        tile(gc.x, gc.y, R * 1.25f, vec3(1.f), 1.f, 4, 0, -1.f, 1.f, saturate(gaugeShown_), g.bonusReady() ? 1.f : 0.f,
             g.bonusActive() ? 1.f : 0.f, R);
    }
    // sparks
    sparks_.clear();
    for (const SparkFx& s : sparkFx_) {
        const float k = s.t / s.life;
        const float a = (1.f - k) * (1.f - k) * smoothstepf(0.f, 0.03f, s.t);
        sparks_.push_back({{s.p.x, s.p.y, s.size, s.len * (0.6f + 0.6f * k)}, {s.c.x, s.c.y, s.c.z, a}, {0.f, k, 0.f, 0.f}});
    }
}

// The HUD's quiet zones: where the text, the previews and the gauge are, the beams fade (board rectangles).
void LaserUniverse::buildMasks(const std::vector<HudText>& hud) {
    maskRects_.clear();
    auto rect = [&](float x0, float y0, float x1, float y1) {
        if (maskRects_.size() >= 24 * 4) return;
        maskRects_.insert(maskRects_.end(), {x0, y0, x1, y1});
    };
    auto toBoard = [&](float px, float py) {
        return v2(viewC_.x - viewH_.x + px / width_ * 2.f * viewH_.x, viewC_.y + viewH_.y - py / height_ * 2.f * viewH_.y);
    };
    for (const HudText& h : hud) {
        if (h.alpha <= 0.05f || h.text.empty()) continue;
        if (h.role == HUD_HELP || h.role == HUD_DIALOG || h.role == HUD_TOAST || h.role == HUD_TOAST_SUB) continue;
        const float base = h.scale * 12.f;
        const float size = base * (h.role == HUD_VALUE ? 1.15f : h.role == HUD_TITLE ? 1.25f : 0.9f);
        const float w = textWidth(fontLight_, h.text, size, base * 0.2f) * 1.1f;
        const float x = ugl::hudLeft(h, w, width_);
        const vec2 a = toBoard(x, h.y - size * 0.1f), b = toBoard(x + w, h.y + size * 1.0f);
        rect(a.x, b.y, b.x, a.y);
    }
    rect(layout::HOLD_CENTER.x - 2.f, layout::HOLD_CENTER.y - 1.3f, layout::HOLD_CENTER.x + 2.f, layout::HOLD_CENTER.y + 1.3f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        rect(c.x - 1.8f, c.y - 1.1f, c.x + 1.8f, c.y + 1.1f);
    }
    rect(layout::GAUGE_CENTER.x - 1.2f, layout::GAUGE_CENTER.y - 1.2f, layout::GAUGE_CENTER.x + 1.2f, layout::GAUGE_CENTER.y + 1.2f);
}

// =====================================================================================================
// Text
// =====================================================================================================

float LaserUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
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

float LaserUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
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

void LaserUniverse::drawText(const Font& f, const std::vector<float>& q, const vec3& color, float alpha, float halo, float bias) {
    if (q.empty() || !f.ok) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    set3f(progText_, "uColor", color);
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

// The HUD in LASER SHOW's typography, like the labels of a lighting console: a thin technical sans, small widely
// spaced capitals for the labels, light numbers, the section and the universe's own word in the laser's color.
void LaserUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    if (!panels.empty()) {
        std::vector<Vert> v;
        auto quad = [&](float x0, float y0, float x1, float y1, const vec3& c, float a) {
            const Vert q[6] = {{x0, y0, c.x, c.y, c.z, a}, {x1, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a},
                               {x0, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a}, {x0, y1, c.x, c.y, c.z, a}};
            v.insert(v.end(), q, q + 6);
        };
        for (const HudRect& r : panels) {
            const vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? pal_.accent : vec3(0.006f, 0.006f, 0.008f);
            quad(r.x, r.y, r.x + r.w, r.y + r.h, c, r.accent || r.custom ? r.alpha : std::min(1.f, r.alpha * 1.1f));
        }
        glUseProgram(progShape_);
        set2f(progShape_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(shapeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vert), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
    }
    const vec3 ink = pal_.ink * 0.9f;
    const vec3 dim = pal_.ink * 0.45f;
    // the accent follows the lasers: the drop's color while it lasts
    const vec3 accent = lerp(pal_.accent, pal_.dropLaser[0], dropColor_) * 1.05f;
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
            // "LASER SHOW" in spaced light capitals, the laser set's name after it in the laser's color
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            name = upper(name);
            const float sz = base * 0.95f, tr = sz * 0.32f, vsz = base * 0.72f, vtr = vsz * 0.36f;
            const float wName = textWidth(fontLight_, name, sz, tr);
            const float wVar = var.empty() ? 0.f : textWidth(fontLight_, var, vsz, vtr);
            const float gap = base * 1.1f;
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            textQuads(fontLight_, name, x, h.y, sz, tr, q);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            drawText(fontLight_, q, ink, a, 0.55f, 0.06f);
            if (!var.empty()) {
                q.clear();
                textQuads(fontLight_, var, x + wName + gap, h.y + (sz - vsz) * 0.5f, vsz, vtr, q);
                drawText(fontLight_, q, accent, a, 0.55f, 0.07f);
            }
            continue;
        }
        case HUD_SECTION:
            place(fontMedium_, upper(h.text), base * 0.85f, base * 0.42f);
            drawText(fontMedium_, q, accent, saturate(0.25f + h.alpha * 1.8f), 0.55f, 0.05f);
            continue;
        case HUD_TITLE:
            place(fontLight_, h.text, base * 1.22f, base * 0.04f, -base * 0.08f);
            drawText(fontLight_, q, ink, saturate(h.alpha * 1.3f), 0.55f, 0.07f);
            continue;
        case HUD_INFO:
            place(fontLight_, upper(h.text), base * 0.68f, base * 0.3f);
            drawText(fontLight_, q, dim, saturate(alpha * 1.4f), 0.55f, 0.07f);
            continue;
        case HUD_LABEL:
            place(fontLight_, h.text, base * 0.72f, base * 0.4f);
            drawText(fontLight_, q, dim, saturate(alpha * 1.3f), 0.55f, 0.07f);
            continue;
        case HUD_VALUE:
            place(fontLight_, h.text, base * 1.12f, base * 0.08f);
            drawText(fontLight_, q, ink, alpha, 0.55f, 0.06f);
            continue;
        case HUD_NOTICE:
            place(fontLight_, upper(h.text), base * 0.95f, base * 0.25f);
            drawText(fontLight_, q, accent, alpha, 0.55f, 0.06f);
            continue;
        case HUD_KEY:
            place(fontMedium_, h.text, base * 0.82f, base * 0.1f);
            drawText(fontMedium_, q, ink, alpha, 0.55f, 0.05f);
            continue;
        case HUD_TOAST:
            place(fontLight_, upper(h.text), base * 1.25f, base * 0.38f, -base * 0.1f);
            drawText(fontLight_, q, ink, alpha, 0.65f, 0.07f);
            continue;
        case HUD_TOAST_SUB:
            place(fontLight_, h.text, base * 0.8f, base * 0.3f);
            drawText(fontLight_, q, accent, alpha * 0.9f, 0.55f, 0.06f);
            continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontLight_, upper(h.text), base * 1.1f, base * 0.3f, -base * 0.1f);
                drawText(fontLight_, q, ink, alpha, 0.65f, 0.07f);
            } else {
                place(fontLight_, h.text, base * 0.82f, base * 0.14f);
                drawText(fontLight_, q, h.accent ? accent : ink, alpha, 0.55f, 0.06f);
            }
            continue;
        default:
            place(fontLight_, h.text, base * 0.8f, base * 0.12f);
            drawText(fontLight_, q, h.accent ? accent : ink, alpha, 0.55f, 0.06f);
            continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void LaserUniverse::setHazeUniforms(GLuint prog) {
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, volTex_);
    set1i(prog, "uVol", 2);
    set3f(prog, "uFlow", flow_);
    set1f(prog, "uHazeVar", lerpf(0.8f, 1.f, slow_));
    set1f(prog, "uHazeScale", 0.032f);
    const int nm = (int)(maskRects_.size() / 4);
    glUniform1i(ugl::U(prog, "uNMask"), nm);
    if (nm > 0) glUniform4fv(ugl::U(prog, "uMask"), nm, maskRects_.data());
    glActiveTexture(GL_TEXTURE0);
}

void LaserUniverse::setBeamUniforms(GLuint prog) {
    float a[MAX_BEAMS * 4], b[MAX_BEAMS * 4];
    const int n = (int)beams_.size();
    for (int i = 0; i < n; i++) {
        std::memcpy(&a[i * 4], beams_[i].a0, sizeof(float) * 4);
        std::memcpy(&b[i * 4], beams_[i].a1, sizeof(float) * 4);
    }
    glUniform1i(ugl::U(prog, "uNB"), n);
    if (n > 0) {
        glUniform4fv(ugl::U(prog, "uBA"), n, a);
        glUniform4fv(ugl::U(prog, "uBB"), n, b);
    }
}

void LaserUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const Palette target = paletteOf(variant_);
        if (!palSet_) { pal_ = target; palSet_ = true; }
        else lerpPalette(pal_, target, 1.f - std::exp(-dt * 1.2f));
    }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 1.2f, dt);
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.55f : 0.f), 3.f, dt);
    if (overT_ >= 0.f) {
        if (!game.over()) overT_ = -1.f;
        else overT_ += dt;
    }
    over_ = overT_ >= 0.f ? smoothstepf(0.3f, 3.f, overT_) : approach(over_, 0.f, 1.5f, dt);
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.2f, slow_);
    if (!f.paused) simulate(game, music, dt, wdt);
    const float world = 1.f - saturate(bridge_.amount);
    buildMasks(f.hud);
    buildBeams();

    // ---- The haze (reduced resolution).
    {
        const float cold = smoothstepf(0.f, 1.f, slow_);
        hazeT_.bind();
        glDisable(GL_BLEND);
        glUseProgram(progHaze_);
        setHazeUniforms(progHaze_);
        setBeamUniforms(progHaze_);
        set4f(progHaze_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set1f(progHaze_, "uGlowW", 0.03f * (1.f + 2.5f * cold));
        set1f(progHaze_, "uHaloW", 1.6f + 1.5f * cold);
        set1f(progHaze_, "uHalo", 0.006f + 0.02f * cold + 0.008f * swell_);
        // the room's own haze, faintly lit by the lasers (their color, the drop's while it lasts)
        vec3 room = (lerp(pal_.laser[0], pal_.dropLaser[0], dropColor_) + lerp(pal_.laser[1], pal_.dropLaser[1], dropColor_)) * 0.5f;
        room = lerp(room, lin(0xd6e6ff), cold * 0.7f);
        const float amb = (0.0015f + 0.002f * wMid_ + 0.003f * wChorus_ + 0.009f * wDrop_ + 0.012f * swell_ + 0.01f * cold) *
                          (1.f - over_ * 0.8f) * (1.f - smoothstepf(0.1f, 0.7f, bridge_.amount));
        set3f(progHaze_, "uAmbientCol", room);
        set1f(progHaze_, "uAmbient", amb);
        set1f(progHaze_, "uThick", cold);
        ugl::drawFullscreen();
    }

    // ---- The room at full resolution.
    glBindFramebuffer(GL_FRAMEBUFFER, sceneT_.fbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    {
        glUseProgram(progComp_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hazeT_.tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        set1i(progComp_, "uHazeT", 0);
        set1i(progComp_, "uNoise", 1);
        set2f(progComp_, "uHazeTexel", 1.f / hazeT_.w, 1.f / hazeT_.h);
        set4f(progComp_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set1f(progComp_, "uTime", worldTime_);
        set3f(progComp_, "uBack", pal_.haze * 0.35f);
        set1f(progComp_, "uOverFog", over_ * 0.5f);
        set3f(progComp_, "uMist", pal_.haze * 2.f);
        set1f(progComp_, "uBridge", saturate(bridge_.amount));
        set3f(progComp_, "uBridgeCol", bridge_.color);
        ugl::drawFullscreen();
        glActiveTexture(GL_TEXTURE0);
    }
    glEnable(GL_BLEND);
    // the beams' cores
    if (!beams_.empty()) {
        glBlendFunc(GL_ONE, GL_ONE);
        glUseProgram(progBeam_);
        setHazeUniforms(progBeam_);
        setMat(progBeam_, "uVP", vp_);
        set1f(progBeam_, "uPx", px_);
        set1f(progBeam_, "uBridge", 0.f);
        set2f(progBeam_, "uWellH", WELL_X, WELL_Y);
        glBindVertexArray(beamVao_);
        glBindBuffer(GL_ARRAY_BUFFER, beamVbo_);
        glBufferData(GL_ARRAY_BUFFER, beams_.size() * sizeof(BeamGpu), beams_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)beams_.size());
    }
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // the well: black glass in front of the beams, its rim catching them
    {
        glUseProgram(progWell_);
        setBeamUniforms(progWell_);
        set4f(progWell_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set1f(progWell_, "uPx", px_);
        set4f(progWell_, "uWell", -WELL_X, -WELL_Y, WELL_X, WELL_Y);
        set3f(progWell_, "uGlass", pal_.haze * 0.25f + vec3(0.004f, 0.004f, 0.005f));
        set3f(progWell_, "uRim", lerp(pal_.rim, pal_.dropLaser[0], dropColor_ * 0.6f));
        set1f(progWell_, "uGlassAlpha", lerpf(0.975f, 0.0f, saturate(bridge_.amount)));
        set1f(progWell_, "uRimBase", 0.10f * (1.f - over_ * 0.7f));
        set1f(progWell_, "uBridge", saturate(bridge_.amount));
        ugl::drawFullscreen();
    }

    // ---- The stack, the pieces, the previews, the gauge.
    tiles_.clear();
    buildBoard(game);
    if (!tiles_.empty()) {
        glUseProgram(progTile_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        set1i(progTile_, "uNoise", 0);
        setMat(progTile_, "uVP", vp_);
        set1f(progTile_, "uPx", px_);
        set1f(progTile_, "uTime", worldTime_);
        set1f(progTile_, "uLight", 1.f);
        set1f(progTile_, "uBridge", saturate(bridge_.amount));
        set3f(progTile_, "uBridgeInk", bridge_.ink);
        set3f(progTile_, "uGaugeCol", lerp(pal_.accent, pal_.dropLaser[0], dropColor_));
        set3f(progTile_, "uGaugeSlow", lin(0xd6e6ff));
        set3f(progTile_, "uTrack", pal_.ink * 0.3f);
        glBindVertexArray(tileVao_);
        glBindBuffer(GL_ARRAY_BUFFER, tileVbo_);
        glBufferData(GL_ARRAY_BUFFER, tiles_.size() * sizeof(Tile), tiles_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)tiles_.size());
    }
    if (!sparks_.empty()) {
        glUseProgram(progSpark_);
        setMat(progSpark_, "uVP", vp_);
        set1f(progSpark_, "uBridge", saturate(bridge_.amount));
        glBindVertexArray(sparkVao_);
        glBindBuffer(GL_ARRAY_BUFFER, sparkVbo_);
        glBufferData(GL_ARRAY_BUFFER, sparks_.size() * sizeof(Spark), sparks_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)sparks_.size());
    }
    drawHud(f.hud, f.panels);
    glDisable(GL_BLEND);

    // ---- Bloom: the beams' cores, the lit edges.
    glUseProgram(progDown_);
    set1i(progDown_, "uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    GLuint src = sceneT_.tex;
    int sw = width_, sh = height_;
    for (int i = 0; i < BLOOM_MIPS; i++) {
        bloomT_[i].bind();
        glBindTexture(GL_TEXTURE_2D, src);
        set2f(progDown_, "uTexel", 0.5f / sw, 0.5f / sh);
        set1f(progDown_, "uThreshold", i == 0 ? 0.8f : 0.f);
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
    set1f(progPost_, "uBloomAmt", 0.55f * world);
    set1f(progPost_, "uGrain", 0.014f);
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_laser() {
    static const UniverseEntry e{
        "LASER SHOW", "a few laser beams in a dark haze, edge-lit acrylic pieces",
        [] { return std::unique_ptr<Universe>(new LaserUniverse()); },
        // Electronic, driving songs suit it best (a steady tempo, bass, dynamics: drops to open the fans); quiet
        // acoustic songs still play, with a beam or two.
        [](const SongLook& l) {
            return 0.55f + 0.3f * l.bassWeight + 0.3f * l.dynamics + 0.2f * smoothstepf(100.f, 130.f, l.bpm);
        },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
