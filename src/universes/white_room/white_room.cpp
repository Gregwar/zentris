// WHITE ROOM: a seamless white studio, matte blocks and soft shadows.
//
// One world: an endless white cyclorama (the floor curving up into the back wall, no corner, no horizon). The well
// is a frosted matte pane standing on a low white plinth; the blocks are matte cubes (soft plastic, unglazed
// ceramic) standing just in front of it. A lamp in front of the well throws the stack's shadow onto the wall behind,
// enlarged and soft, a shadow play: the tetrominoes themselves, in shade, beside the well (the pane casts nothing and
// no shadow falls on it: it hides what lies behind it, so the real stack always reads on a calm, even ground).
// Nothing else but the cleared blocks lying on the floor and the lettering set on the wall like a gallery's. Each
// room is one color story (a family of related hues in spaced values, one accent, two gels that belong to it).
//
// The song moves the lamp: calm is a soft light high in front, the shadows hidden behind the pane; mid brings the
// lamp lower and to the side and the stack's shadow starts to climb the wall beside the well; a chorus is a warm lamp
// low on the side, the shadow large and crisp. A drop changes the room for its whole length: on its downbeat two
// colored gels swing in low from far out on either side, so two enlarged colored copies of the stack's shadow sweep
// in across the wall and settle, one on each side, each lit by the other gel; then the gels slowly circle, the
// shadow copies sweeping. A new section swings the lamp across to the other side. The light never pulses. Slow
// motion is evening: the room goes blue-grey and a single warm lamp hangs over the well. A lock presses the piece
// into the panel; a cleared row vacates at once and its blocks are thrown out from behind the pane as cubes, to the
// left, the right or back toward the wall: they fly, tumble, bounce and land on the floor, the plinth or the cubes
// already there, piling up into messy mounds (tilted, turned, leaning, tipping off an edge), lie there a minute or
// two and fade away (the ones above then drop); game over turns the stack to white plaster row by row as the light
// sets. Every cube (the stack, the piece, the previews, the thrown ones) is the same real cube, rasterized in the
// layout's camera and lit by the room.
//
// Layout contract: the layout's own camera (the well's plane z = 0 is exactly where the layout puts it).
#include "universes/white_room/white_room.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "universes/registry.hpp"
#include "universes/white_room/shaders.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Manrope Light and Medium (SIL Open Font License, see OFL-Manrope.txt beside this file).
const unsigned char FONT_LIGHT[] = {
#include "universes/white_room/Manrope-Light.ttf.inc"
};
const unsigned char FONT_MEDIUM[] = {
#include "universes/white_room/Manrope-Medium.ttf.inc"
};

namespace shaders = white_room_shaders;
using ugl::set1f;
using ugl::set1i;
using ugl::set2f;
using ugl::set3f;
using ugl::set4f;
using ugl::setMat;

GLuint compile(const char* vs, const char* fs, const char* name, bool lit = false) {
    std::string f(fs);
    f.insert(f.find('\n') + 1, std::string(shaders::COMMON_GLSL) + (lit ? shaders::LIGHT_GLSL : ""));
    return ugl::program(vs, f.c_str(), name);
}

constexpr float FLOOR = -10.6f, WALL_Z = -9.f, COVE = 5.f;
constexpr float PLINTH_TOP = -10.05f, PLINTH_HX = 5.5f, PLINTH_HZ = 1.3f;
constexpr float PANE_Z = -0.62f;

vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }
vec3 lin(unsigned c) { return ugl::toLinear(hex(c)); }

// The rooms (sRGB): the wall and the panel, the lettering (on a lit wall, on a dark one), the accent, the key light
// and its warm (chorus) tone, the ambient, the two gels of the drop, the plaster of a cleared block; the seven
// blocks (I O T S Z J L); the room's exposure. Each room is one color story: a family of related hues in spaced
// values (the seven blocks read by value and tone) with at most one accent, and two gels that belong to it.
struct VariantDef {
    const char* name;
    unsigned wall, panel, ink, inkLight, accent, key, warm, amb, gelA, gelB, plaster;
    unsigned piece[7];
    float exposure;
};
const VariantDef VARIANTS[] = {
    // ink blues from navy to cornflower, a warm stone, one terracotta; gels of warm amber and cool blue
    {"GALLERY", 0xedeeec, 0xf4f4f2, 0x24262a, 0xf2f2f0, 0xc8643f, 0xffffff, 0xffe8cc, 0xf0f3f8, 0xffb060, 0x6f9cff, 0xf4f4f2,
     {0x7f9cc8, 0xc8643f, 0x1f2c48, 0x5d6470, 0x8f857a, 0x34507e, 0x4f74a8}, 1.f},
    // earth: espresso, walnut, camel, ochre, olive, sage, one clay; gels of amber and dusk violet
    {"LINEN", 0xf0e9dd, 0xf6f1e8, 0x3a2c22, 0xf6efe4, 0xb0623e, 0xfff6ea, 0xffdcae, 0xfaf0e2, 0xffa850, 0xa48cff, 0xf7f1e8,
     {0xa88866, 0xc4943e, 0x3a2a22, 0x6f6b3a, 0xb0623e, 0x6e4a34, 0x8e9476}, 1.f},
    // the cold sea: midnight, petrol, teal, sea glass, slate, fog, one coral; gels of ice cyan and coral
    {"NORDIC", 0xebf0f4, 0xf3f6f9, 0x1e2a3a, 0xeef3f8, 0xe0705a, 0xf2f7ff, 0xfff0dc, 0xecf3ff, 0x6ad8ff, 0xff8a6e, 0xf3f6f9,
     {0x75aeb0, 0xe0705a, 0x1c2a3c, 0x3f8a8c, 0x8c9aa6, 0x2b5f6b, 0x4a5a6c}, 1.f},
    // celadon glazes and tenmoku browns, ash, one kaki; gels of gold and jade
    {"CERAMIC", 0xefebe2, 0xf5f1e9, 0x2c2520, 0xf5f0e6, 0xb0602e, 0xfffaf0, 0xffe4c0, 0xf6f0e6, 0xffc45a, 0x5ad6a8, 0xf5f1e9,
     {0x8fb3a0, 0x9a7a5a, 0x2a4038, 0x4f8070, 0xb0602e, 0x3e2a20, 0x8c8a82}, 1.f},
    // a dark room: a black studio, a black pane, flashy blocks (electric cyan, lemon, hot pink, acid lime,
    // tangerine, ultraviolet, white) and hard light, the shadow play clear on the lit black wall; gels of hot pink
    // and cyan
    {"DARK ROOM", 0x404046, 0x0c0c0e, 0xf0f0f2, 0xf0f0f2, 0xff3ea5, 0xffffff, 0xfff0e0, 0x161a24, 0xff3e9a, 0x2ad8ff, 0x2a2a2e,
     {0x22e4ff, 0xfff04a, 0xff3ea5, 0x7dff5a, 0xff6a2a, 0x8a5cff, 0xf4f4f4}, 1.35f},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

using Palette = WhiteRoomUniverse::Palette;
static_assert(sizeof(Palette) % sizeof(float) == 0, "a palette is floats only");
constexpr int PAL_FLOATS = (int)(sizeof(Palette) / sizeof(float));

Palette paletteOf(int v) {
    const VariantDef& d = VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT];
    Palette p;
    p.wall = lin(d.wall); p.panel = lin(d.panel); p.ink = lin(d.ink); p.inkLight = lin(d.inkLight); p.accent = lin(d.accent);
    p.key = lin(d.key); p.warm = lin(d.warm); p.amb = lin(d.amb); p.gelA = lin(d.gelA); p.gelB = lin(d.gelB);
    p.plaster = lin(d.plaster);
    for (int i = 0; i < 7; i++) p.piece[i] = lin(d.piece[i]);
    p.exposure = d.exposure;
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

// The key lamp (its position in front of the well, board space: the lower and the more to the side, the longer and
// the further the stack's shadow travels across the wall), its radius (softness), its strength and warmth, the
// ambient, the pool of light on the wall and how dark the shadow play is, for calm, mid and a chorus.
struct LightLook { float x, y, z, radius, key, warm, amb, pool, shadow; };
const LightLook LOOKS[3] = {
    {-4.f, 13.f, 26.f, 5.0f, 0.46f, 0.0f, 0.84f, 0.05f, 0.45f},
    {-12.f, -6.f, 22.f, 2.6f, 0.70f, 0.15f, 0.60f, 0.10f, 0.7f},
    {-18.f, -9.f, 17.f, 1.3f, 0.95f, 0.6f, 0.44f, 0.14f, 0.85f},
};
// The drop's two gels, low on either side (the stack's shadows climb the wall, enlarged).
const vec3 GEL_A(-19.f, -10.f, 13.f), GEL_B(19.f, -10.2f, 13.f);

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool WhiteRoomUniverse::init(int width, int height) {
    progRoom_ = compile(ugl::FULLSCREEN_VS, shaders::ROOM_FS, "white room", true);
    progCube_ = compile(shaders::CUBE_VS, shaders::CUBE_FS, "white room cube", true);
    progShadow_ = compile(shaders::SHADOW_VS, shaders::SHADOW_FS, "white room shadow");
    progCopy_ = compile(ugl::FULLSCREEN_VS, shaders::COPY_FS, "white room copy");
    progOcc_ = compile(shaders::OCC_VS, shaders::OCC_FS, "white room occupancy");
    progTile_ = compile(shaders::TILE_VS, shaders::TILE_FS, "white room tile");
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "white room shape");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "white room text");
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "white room post");

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
    instanced(occVao_, occVbo_, 1, sizeof(Occ));

    // The cube: 36 corners at +-1 with their faces' normals (counter-clockwise seen from outside), then the
    // instances.
    {
        std::vector<float> mesh;
        const vec3 N[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const vec3 U[6] = {{0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        const vec3 V[6] = {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}};
        const float ab[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
        for (int f = 0; f < 6; f++)
            for (auto& c : ab) {
                const vec3 q = N[f] + U[f] * c[0] + V[f] * c[1];
                const float v[6] = {q.x, q.y, q.z, N[f].x, N[f].y, N[f].z};
                mesh.insert(mesh.end(), v, v + 6);
            }
        glGenVertexArrays(1, &cubeVao_);
        glGenBuffers(1, &cubeMeshVbo_);
        glGenBuffers(1, &cubeVbo_);
        glBindVertexArray(cubeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, cubeMeshVbo_);
        glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(float), mesh.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)(sizeof(float) * 3));
        glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
        for (int i = 0; i < 4; i++) {
            glEnableVertexAttribArray(2 + i);
            glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, sizeof(CubeInst), (void*)(sizeof(float) * 4 * i));
            glVertexAttribDivisor(2 + i, 1);
        }
    }

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

    // The occupancy map: the board's blocks seen from the front, blurred by its mip levels for the soft shadows.
    glGenTextures(1, &occTex_);
    glBindTexture(GL_TEXTURE_2D, occTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, OCC_N, OCC_N, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glGenFramebuffers(1, &occFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, occFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, occTex_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // The shadow maps (the thrown cubes seen from each lamp): read compared (soft taps) and raw (the casters' depth).
    for (int i = 0; i < 2; i++) {
        glGenTextures(1, &shTex_[i]);
        glBindTexture(GL_TEXTURE_2D, shTex_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SH_N, SH_N, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &shFbo_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, shFbo_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shTex_[i], 0);
        // where the casters are (mipmapped: the shaders skip the points far from any)
        glGenTextures(1, &shCovTex_[i]);
        glBindTexture(GL_TEXTURE_2D, shCovTex_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, SH_N, SH_N, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_2D);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, shCovTex_[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "[gl] white room shadow FBO incomplete\n");
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        shP_[i] = vec4(0, 0, 0, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGenSamplers(1, &shCmpSampler_);
    glSamplerParameteri(shCmpSampler_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(shCmpSampler_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(shCmpSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(shCmpSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(shCmpSampler_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glSamplerParameteri(shCmpSampler_, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenSamplers(1, &shRawSampler_);
    glSamplerParameteri(shRawSampler_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(shRawSampler_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(shRawSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(shRawSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(shRawSampler_, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    // The piles' heightmap: the ground (the floor, the plinth's top) and the resting cubes' tops, cell by cell.
    hmBase_.assign(HM_NX * HM_NZ, 0.f);
    for (int iz = 0; iz < HM_NZ; iz++)
        for (int ix = 0; ix < HM_NX; ix++) {
            const float x = HM_X0 + (ix + 0.5f) * HM_CELL, z = HM_Z0 + (iz + 0.5f) * HM_CELL;
            hmBase_[iz * HM_NX + ix] = std::fabs(x) < PLINTH_HX && std::fabs(z) < PLINTH_HZ ? PLINTH_TOP : FLOOR;
        }
    hm_ = hmBase_;
    hmUp_.assign(HM_NX * HM_NZ, 0.f);
    glGenTextures(1, &hmTex_);
    glBindTexture(GL_TEXTURE_2D, hmTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, HM_NX, HM_NZ, 0, GL_RED, GL_FLOAT, hmUp_.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    hmDirty_ = true;

    buildFont(fontLight_, FONT_LIGHT);
    buildFont(fontMedium_, FONT_MEDIUM);
    pal_ = paletteOf(0);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// Glyphs baked as signed distance fields (they scale cleanly, and soften into the lettering's shadow).
void WhiteRoomUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] white room font init failed\n");
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

void WhiteRoomUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    cam_ = layout::camera(width_, height_);
    vp_ = cam_.vp;
    const float hh = cam_.dist * std::tan(cam_.fovDeg * 0.5f * PI / 180.f);
    px_ = 2.f * hh / height_;
    createTargets();
}

void WhiteRoomUniverse::createTargets() {
    sceneT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    // the scene is drawn multisampled (the cubes' edges), with a depth (the room's, then the cubes'), then resolved
    if (msFbo_) glDeleteFramebuffers(1, &msFbo_);
    for (GLuint* r : {&msColor_, &msDepth_})
        if (*r) glDeleteRenderbuffers(1, r);
    GLint maxS = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxS);
    samples_ = std::min(4, (int)maxS);
    glGenFramebuffers(1, &msFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glGenRenderbuffers(1, &msColor_);
    glBindRenderbuffer(GL_RENDERBUFFER, msColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA16F, width_, height_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
    glGenRenderbuffers(1, &msDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, msDepth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, width_, height_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "[gl] white room MSAA FBO incomplete\n");
    // the room's own target (its color, its depth)
    if (roomFbo_) glDeleteFramebuffers(1, &roomFbo_);
    for (GLuint* t : {&roomTex_, &roomZTex_})
        if (*t) glDeleteTextures(1, t);
    auto tex = [&](GLuint& t, GLenum internal, GLenum format, GLenum type) {
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, width_, height_, 0, format, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    tex(roomTex_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    tex(roomZTex_, GL_R32F, GL_RED, GL_FLOAT);
    glGenFramebuffers(1, &roomFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, roomFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, roomTex_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, roomZTex_, 0);
    const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, bufs);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "[gl] white room room FBO incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

WhiteRoomUniverse::~WhiteRoomUniverse() {
    sceneT_.destroy();
    for (GLuint p : {progRoom_, progOcc_, progTile_, progShape_, progText_, progPost_, progCube_, progShadow_, progCopy_})
        glDeleteProgram(p);
    for (GLuint v : {tileVao_, occVao_, shapeVao_, textVao_, cubeVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, tileVbo_, occVbo_, shapeVbo_, textVbo_, cubeMeshVbo_, cubeVbo_}) glDeleteBuffers(1, &b);
    for (GLuint f : {occFbo_, shFbo_[0], shFbo_[1], msFbo_, roomFbo_})
        if (f) glDeleteFramebuffers(1, &f);
    for (GLuint t : {occTex_, shTex_[0], shTex_[1], shCovTex_[0], shCovTex_[1], hmTex_, roomTex_, roomZTex_})
        if (t) glDeleteTextures(1, &t);
    for (GLuint r : {msColor_, msDepth_})
        if (r) glDeleteRenderbuffers(1, &r);
    for (GLuint sm : {shCmpSampler_, shRawSampler_})
        if (sm) glDeleteSamplers(1, &sm);
    for (Font* f : {&fontLight_, &fontMedium_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// =====================================================================================================
// Song, level, bonus
// =====================================================================================================

// The room from the song: bright major songs get the clean whites (GALLERY, NORDIC), warm ones the linen and the
// ceramics, dark minor ones the dark room or the cold sea; the key's hue picks among them.
int WhiteRoomUniverse::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    const bool dark = t.hasSong && t.minor && t.brightness < 0.42f;
    const bool bright = t.hasSong && !t.minor && t.brightness > 0.55f;
    int v;
    if (dark) v = h < 0.6f ? 4 : 2;
    else if (bright) v = h < 0.5f ? 2 : 0;
    else if (h < 0.17f) v = 1;
    else if (h < 0.33f) v = 3;
    else if (h < 0.5f) v = 2;
    else if (h < 0.67f) v = 0;
    else if (h < 0.83f) v = t.minor ? 2 : 1;
    else v = t.minor ? 4 : 3;
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void WhiteRoomUniverse::setSong(const SongLook& look, float seconds) {
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    rng_ = Rng(look.seed ^ 0x3417ull);
    if (seconds <= 0.f) palSet_ = false; // snap on the first frame
}

void WhiteRoomUniverse::setLevel(int level, float energy, float seconds, float impact) {
    (void)impact;
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
    // the light swings slowly: a rise in a couple of seconds, a fall over several
    lvDur_ = std::clamp(seconds * 1.3f, rising ? 2.0f : 4.0f, 7.f);
}

// The drop's downbeat: the gels flare in (one swell, settling over seconds) and swing in from far out on either side
// (the stack's two colored shadows sweep in across the wall and settle).
void WhiteRoomUniverse::drop(float strength) {
    swell_ = std::max(swell_, saturate(strength));
    sinceDrop_ = 0.f;
    dropSwing_ = 1.f;
    if (level_ >= 2) {
        // the light goes straight to the peak, without waiting for the end of the swing
        lvFrom_ = lv_;
        lvTo_ = 2.f;
        lvT_ = std::max(lvT_, 0.f);
        lvDur_ = std::min(lvDur_, 1.6f);
    }
}

// A new section: the light swings across to the other side of the room (the shadows turn around the stack).
void WhiteRoomUniverse::refresh(float strength) {
    if (strength >= 0.25f) sideTo_ = -sideTo_;
}

void WhiteRoomUniverse::setBonus(bool on) { slowOn_ = on; }

vec2 WhiteRoomUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints WhiteRoomUniverse::hints() const {
    const Palette p = paletteOf(variant_);
    TransitionHints h;
    const float e = p.exposure * (variant_ == 4 ? 0.62f : 1.f);
    h.background = p.wall * e;
    h.light = ugl::luma(h.background) > 0.3f;
    h.ink = h.light ? p.piece[2] * 0.7f : p.piece[0];
    h.accent = p.accent;
    h.styles = TS_LUMA | TS_CURTAIN | TS_REBUILD; // the light going, the room opening, the blocks set again
    return h;
}

// =====================================================================================================
// Ejected cubes
// =====================================================================================================

namespace {
constexpr float EJECT_Z = -1.25f; // thrown out from behind the pane (z = -0.62): nothing is drawn over the well
constexpr float FADE = 1.5f;
constexpr float ZMIN = WALL_Z + COVE + 0.55f, ZMAX = 2.2f, XMAX = 17.f; // the floor they keep to
enum { AIR, SETTLE, REST };

float extentY(const vec3 (&ax)[3], float h) { return h * (std::fabs(ax[0].y) + std::fabs(ax[1].y) + std::fabs(ax[2].y)); }
void orthonormalize(vec3 (&ax)[3]) {
    ax[0] = normalize(ax[0]);
    ax[1] = normalize(ax[1] - ax[0] * dot(ax[0], ax[1]));
    ax[2] = cross(ax[0], ax[1]);
}
vec3 rotateAbout(const vec3& v, const vec3& k, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1.f - c));
}
int upAxis(const vec3 (&ax)[3]) {
    int u = 0;
    for (int k = 1; k < 3; k++)
        if (std::fabs(ax[k].y) > std::fabs(ax[u].y)) u = k;
    return u;
}
} // namespace

float WhiteRoomUniverse::hmAt(float x, float z) const {
    const int ix = (int)std::floor((x - HM_X0) / HM_CELL), iz = (int)std::floor((z - HM_Z0) / HM_CELL);
    if (ix < 0 || iz < 0 || ix >= HM_NX || iz >= HM_NZ) return FLOOR;
    return hm_[iz * HM_NX + ix];
}

// A resting cube raises the cells under it to its top (a little under it: the next one nests in).
void WhiteRoomUniverse::hmStamp(const vec3& p, const vec3 (&ax)[3], float h) {
    const int u = upAxis(ax);
    const vec3 a = ax[(u + 1) % 3], b = ax[(u + 2) % 3];
    const vec3 n = ax[u] * (ax[u].y < 0.f ? -1.f : 1.f); // its top face's normal
    const float ext = extentY(ax, h);
    // the top face over a cell (a tilted cube is lower on one side)
    auto top = [&](float dx, float dz) {
        return p.y + std::min(ext, (h - n.x * dx - n.z * dz) / std::max(n.y, 0.3f)) * 0.92f;
    };
    const float R = 1.42f * h;
    const int x0 = std::max(0, (int)std::floor((p.x - R - HM_X0) / HM_CELL)),
              x1 = std::min(HM_NX - 1, (int)std::floor((p.x + R - HM_X0) / HM_CELL));
    const int z0 = std::max(0, (int)std::floor((p.z - R - HM_Z0) / HM_CELL)),
              z1 = std::min(HM_NZ - 1, (int)std::floor((p.z + R - HM_Z0) / HM_CELL));
    for (int iz = z0; iz <= z1; iz++)
        for (int ix = x0; ix <= x1; ix++) {
            const float dx = HM_X0 + (ix + 0.5f) * HM_CELL - p.x, dz = HM_Z0 + (iz + 0.5f) * HM_CELL - p.z;
            if (std::fabs(dx * a.x + dz * a.z) > h * 0.95f || std::fabs(dx * b.x + dz * b.z) > h * 0.95f) continue;
            float& c = hm_[iz * HM_NX + ix];
            c = std::max(c, top(dx, dz));
        }
    const int ix = (int)std::floor((p.x - HM_X0) / HM_CELL), iz = (int)std::floor((p.z - HM_Z0) / HM_CELL);
    if (ix >= 0 && iz >= 0 && ix < HM_NX && iz < HM_NZ) hm_[iz * HM_NX + ix] = std::max(hm_[iz * HM_NX + ix], top(0.f, 0.f));
    hmDirty_ = true;
}

// A cube is gone: the piles again from the ground up (the resting cubes from the lowest); a cube left hanging over
// the gap wakes and drops.
void WhiteRoomUniverse::hmRebuild() {
    hm_ = hmBase_;
    std::vector<std::pair<float, int>> order;
    for (int i = 0; i < (int)ejected_.size(); i++) {
        const Ejected& e = ejected_[i];
        if (e.state != AIR) order.push_back({e.toP.y - extentY(e.toAx, e.h), i});
    }
    std::sort(order.begin(), order.end());
    for (auto& o : order) {
        Ejected& e = ejected_[o.second];
        const float r = 0.7f * e.h, x = e.toP.x, z = e.toP.z;
        const float S = std::max({hmAt(x, z), hmAt(x + r, z), hmAt(x - r, z), hmAt(x, z + r), hmAt(x, z - r)});
        if (o.first > S + 0.3f * e.h) {
            if (e.state == REST) {
                e.p = e.toP;
                for (int k = 0; k < 3; k++) e.ax[k] = e.toAx[k];
            }
            e.state = AIR;
            e.v = vec3(0.f, 0.f, 0.f);
            e.w = vec3(rng_.range(-1.5f, 1.5f), rng_.range(-1.5f, 1.5f), rng_.range(-1.5f, 1.5f));
            e.tips = 0;
        } else hmStamp(e.toP, e.toAx, e.h);
    }
    hmDirty_ = true;
}

// A cleared block thrown out: left, right (mostly toward its own side of the well) or back toward the wall, with an
// upward kick and a tumble; power grows with the lines cleared.
void WhiteRoomUniverse::eject(float x, float y, int color, float power) {
    Ejected e;
    e.h = 0.42f;
    e.p = vec3(x, y, EJECT_Z);
    const float side = x + rng_.range(-3.5f, 3.5f) < 0.f ? -1.f : 1.f;
    if (rng_.chance(0.3f)) {
        // back toward the wall, spreading sideways (piles behind and beside the well)
        e.v = vec3(side * rng_.range(3.f, 10.f), rng_.range(3.f, 7.f), -rng_.range(1.f, 3.5f));
    } else {
        e.v = vec3(side * rng_.range(5.f, 17.f), rng_.range(3.f, 8.5f), rng_.range(-2.5f, 0.6f));
    }
    e.v = e.v * power;
    const float yaw = rng_.range(0.f, TAU);
    e.ax[0] = vec3(std::cos(yaw), 0.f, -std::sin(yaw));
    e.ax[1] = vec3(0.f, 1.f, 0.f);
    e.ax[2] = cross(e.ax[0], e.ax[1]);
    // tumbling about an axis across its flight
    const vec3 dir = normalize(vec3(e.v.x, 0.f, e.v.z));
    const vec3 axis = normalize(cross(vec3(0.f, 1.f, 0.f), dir) +
                                vec3(rng_.range(-0.4f, 0.4f), rng_.range(-0.4f, 0.4f), rng_.range(-0.4f, 0.4f)));
    e.w = axis * (rng_.range(4.f, 11.f) * (rng_.chance(0.8f) ? 1.f : -1.f));
    e.color = color;
    e.life = rng_.range(60.f, 120.f);
    e.state = AIR;
    ejected_.push_back(e);
    // over the cap: the oldest start fading now
    int alive = 0;
    for (const Ejected& o : ejected_) alive += o.life - o.age > FADE ? 1 : 0;
    for (Ejected& o : ejected_) {
        if (alive <= EJECT_MAX) break;
        if (o.life - o.age > FADE) { o.life = o.age + FADE; alive--; }
    }
}

// Landing: on the floor, the plinth or a pile (the heightmap under it). A cube whose middle is not held (it came
// down on an edge) tips off toward the lower side; otherwise it settles, its lowest face down, turned a little,
// tilted along the pile under it (leaning when it tipped already).
void WhiteRoomUniverse::land(Ejected& e) {
    const float h = e.h, r = 0.7f * h, x = e.p.x, z = e.p.z;
    const float Hc = hmAt(x, z), Hxp = hmAt(x + r, z), Hxm = hmAt(x - r, z), Hzp = hmAt(x, z + r), Hzm = hmAt(x, z - r);
    const float S = std::max({Hc, Hxp, Hxm, Hzp, Hzm}), lo = std::min({Hc, Hxp, Hxm, Hzp, Hzm});
    // the pile's slope around (a cube on a peak rolls down its steepest side: the piles spread into mounds)
    // (never toward where it cannot go: in front of the well, out of the room)
    const float R2 = 2.2f * h;
    auto around = [&](float xx, float zz) {
        const bool shut = (std::fabs(xx) < PLINTH_HX + h && zz > PANE_Z - h) || zz < ZMIN || zz > ZMAX || std::fabs(xx) > XMAX;
        return shut ? 1e3f : hmAt(xx, zz);
    };
    float low = 1e9f;
    vec3 downhill(1.f, 0.f, 0.f);
    const float a0 = rng_.range(0.f, TAU);
    for (int q = 0; q < 8; q++) {
        const vec3 dd(std::cos(a0 + q * TAU / 8.f), 0.f, std::sin(a0 + q * TAU / 8.f));
        const float v = around(x + dd.x * R2, z + dd.z * R2);
        if (v < low) { low = v; downhill = dd; }
    }
    const bool edge = Hc < S - 0.45f * h, steep = S - low > 1.25f * h;
    if ((edge && e.tips < 2) || (steep && e.tips < 10)) {
        vec3 d(Hxm - Hxp, 0.f, Hzm - Hzp);
        if (!edge) d = downhill + vec3(rng_.range(-0.3f, 0.3f), 0.f, rng_.range(-0.3f, 0.3f));
        if (length(d) < 1e-3f) d = vec3(rng_.range(-1.f, 1.f), 0.f, rng_.range(-1.f, 1.f));
        d = normalize(d);
        const float sp = rng_.range(2.2f, 3.6f);
        e.v = vec3(d.x * sp, 1.2f, d.z * sp);
        e.w = cross(vec3(0.f, 1.f, 0.f), d) * rng_.range(4.f, 7.f);
        e.p.y = S + extentY(e.ax, h) + 0.02f;
        e.tips++;
        return;
    }
    const int ui = upAxis(e.ax), j = (ui + 1) % 3, k = (ui + 2) % 3;
    const float sgn = e.ax[ui].y < 0.f ? -1.f : 1.f;
    float gx = (Hxp - Hxm) / (2.f * r), gz = (Hzp - Hzm) / (2.f * r);
    const float maxT = (e.tips > 0 || Hc < S - 0.2f * h) ? 0.8f : 0.2f;
    const float gl = std::sqrt(gx * gx + gz * gz);
    if (gl > maxT) { gx *= maxT / gl; gz *= maxT / gl; }
    gx += rng_.range(-0.04f, 0.04f);
    gz += rng_.range(-0.04f, 0.04f);
    const vec3 n = normalize(vec3(-gx, 1.f, -gz));
    vec3 T[3];
    T[ui] = n * sgn;
    vec3 A = e.ax[j] - n * dot(e.ax[j], n);
    if (length(A) < 1e-3f) A = e.ax[k] - n * dot(e.ax[k], n);
    T[j] = rotateAbout(normalize(A), n, rng_.range(-0.3f, 0.3f));
    T[k] = cross(T[ui], T[j]);
    const float P = std::max(Hc, 0.5f * (S + lo));
    e.fromP = e.p;
    e.toP = vec3(x, P + h / n.y, z);
    for (int i = 0; i < 3; i++) {
        e.fromAx[i] = e.ax[i];
        e.toAx[i] = T[i];
    }
    e.settle = 0.f;
    e.state = SETTLE;
    e.v = vec3(0.f, 0.f, 0.f);
    e.w = vec3(0.f, 0.f, 0.f);
    hmStamp(e.toP, e.toAx, h);
}

// Flight: gravity and the tumble, the room's bounds, the pane (they stay behind it, or beside the well), the piles
// in the way, the ground (a bounce, then landing). Settling cubes ease into their pose; resting ones cost nothing.
void WhiteRoomUniverse::stepEjected(float dt) {
    const float G = 30.f;
    const int steps = 2;
    const float hdt = dt / steps;
    for (Ejected& e : ejected_) {
        if (e.state == REST) continue;
        if (e.state == SETTLE) {
            e.settle = std::min(1.f, e.settle + dt / 0.14f);
            const float k = smoothstepf(0.f, 1.f, e.settle);
            e.p = lerp(e.fromP, e.toP, k);
            for (int i = 0; i < 3; i++) e.ax[i] = lerp(e.fromAx[i], e.toAx[i], k);
            orthonormalize(e.ax);
            if (e.settle >= 1.f) {
                e.state = REST;
                e.p = e.toP;
                for (int i = 0; i < 3; i++) e.ax[i] = e.toAx[i];
            }
            continue;
        }
        for (int s = 0; s < steps && e.state == AIR; s++) {
            e.v.y -= G * hdt;
            const vec3 prev = e.p;
            e.p += e.v * hdt;
            const float wl = length(e.w);
            if (wl > 1e-4f) {
                const vec3 kax = e.w / wl;
                for (int i = 0; i < 3; i++) e.ax[i] = rotateAbout(e.ax[i], kax, wl * hdt);
                orthonormalize(e.ax);
            }
            if (e.p.z < ZMIN) { e.p.z = ZMIN; e.v.z = std::fabs(e.v.z) * 0.3f; }
            if (e.p.z > ZMAX) { e.p.z = ZMAX; e.v.z = -std::fabs(e.v.z) * 0.3f; }
            if (std::fabs(e.p.x) > XMAX) { e.p.x = std::copysign(XMAX, e.p.x); e.v.x = -e.v.x * 0.3f; }
            // the pane (and the plinth's front): behind it, or beside the well
            const float px = PLINTH_HX + e.h;
            const float ez = e.h * (std::fabs(e.ax[0].z) + std::fabs(e.ax[1].z) + std::fabs(e.ax[2].z));
            if (std::fabs(e.p.x) < px && e.p.z > PANE_Z - ez && e.p.y < 10.6f) {
                if (std::fabs(prev.x) < px) { e.p.z = PANE_Z - ez; e.v.z = -std::fabs(e.v.z) * 0.3f; }
                else { e.p.x = std::copysign(px, prev.x); e.v.x = -e.v.x * 0.3f; }
            }
            const float ext = extentY(e.ax, e.h);
            // a pile in the way
            const float hc = hmAt(e.p.x, e.p.z);
            if (e.p.y - ext < hc - 0.35f * e.h && hmAt(prev.x, prev.z) < hc - 0.05f) {
                e.p.x = prev.x;
                e.p.z = prev.z;
                e.v.x *= -0.3f;
                e.v.z *= -0.3f;
            }
            const float r = 0.7f * e.h;
            const float S = std::max({hmAt(e.p.x, e.p.z), hmAt(e.p.x + r, e.p.z), hmAt(e.p.x - r, e.p.z),
                                      hmAt(e.p.x, e.p.z + r), hmAt(e.p.x, e.p.z - r)});
            if (e.p.y - ext <= S) {
                if (e.v.y > 0.f) e.p.y = S + ext;
                else if (e.v.y < -4.f) {
                    e.p.y = S + ext;
                    e.v.y = -e.v.y * 0.28f;
                    e.v.x *= 0.6f;
                    e.v.z *= 0.6f;
                    e.w = e.w * 0.5f + vec3(rng_.range(-3.f, 3.f), rng_.range(-3.f, 3.f), rng_.range(-3.f, 3.f));
                } else land(e);
            }
        }
    }
    const size_t before = ejected_.size();
    for (Ejected& e : ejected_) e.age += dt;
    ejected_.erase(std::remove_if(ejected_.begin(), ejected_.end(), [](const Ejected& e) { return e.age >= e.life; }),
                   ejected_.end());
    if (ejected_.size() != before) hmRebuild();
}

// =====================================================================================================
// Game events
// =====================================================================================================

void WhiteRoomUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::Lock:
        // the piece presses into the panel (more after a hard drop)
        press_ = lastDrop_ >= 2 ? 1.f : 0.55f;
        lastDrop_ = 0;
        break;
    case GameEvent::HardDrop:
        lastDrop_ = ev.count;
        break;
    case GameEvent::Clear: {
        // the row vacates at once: its blocks are thrown out from behind the pane as cubes (harder for more lines)
        const float power = 1.f + 0.08f * std::max(0, ev.count - 1);
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            eject(p.x, p.y, std::clamp(c.type, 0, 6), power * rng_.range(0.9f, 1.1f));
        }
        break;
    }
    case GameEvent::TopOut: {
        // the stack turns to plaster row by row from the top as the light sets
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            dying_.push_back({p.x, p.y, 0.f, (c.y - top) * 0.11f + rng_.range(0.f, 0.06f), std::clamp(c.type, 0, 6)});
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

void WhiteRoomUniverse::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    worldTime_ += wdt;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    const float wp = smoothstepf(1.3f, 2.f, lv_);
    // a drop rather than a chorus: the song's drive (its sections' speed) in a peak
    driveTarget_ = (level_ >= 2 ? 1.f : 0.f) * smoothstepf(1.27f, 1.42f, music.hasSong ? music.speed : 1.45f);
    drive_ = approach(drive_, driveTarget_ * wp, driveTarget_ > drive_ ? 1.1f : 0.5f, dt);
    side_ = approach(side_, sideTo_, 0.45f, dt);
    swell_ = approach(swell_, 0.f, 0.6f, dt);
    sinceDrop_ += dt;
    beat_ = music.beat;

    dropSwing_ = approach(dropSwing_, 0.f, 0.55f, dt);

    press_ = approach(press_, 0.f, 9.f, dt);
    // a line clear runs at the real speed, even in slow motion
    for (auto& d : dying_) d.t += dt;
    stepEjected(dt);
    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
}

// The light of this frame: the section's look (calm, mid, chorus) and the drop's two gels, the side, the slow
// circling at a peak, slow motion's lamp and the game over's sunset.
void WhiteRoomUniverse::computeLights(const MusicState& music) {
    const float l = std::clamp(lv_, 0.f, 2.f);
    const int i0 = std::min(1, (int)l);
    const float k = l - i0;
    const LightLook& a = LOOKS[i0];
    const LightLook& b = LOOKS[i0 + 1];
    auto mixL = [&](float LightLook::*m) { return lerpf(a.*m, b.*m, k); };
    vec3 p0(mixL(&LightLook::x), mixL(&LightLook::y), mixL(&LightLook::z));
    float radius = mixL(&LightLook::radius), key = mixL(&LightLook::key), warm = mixL(&LightLook::warm);
    float amb = mixL(&LightLook::amb), pool = mixL(&LightLook::pool), shadow = mixL(&LightLook::shadow);
    const float wp = smoothstepf(1.3f, 2.f, lv_);
    const float d = smoothstepf(0.f, 1.f, drive_);
    // at a peak the lamps circle slowly (eight bars a turn): the shadows sweep across the wall
    const float orbit = (float)std::sin(TAU * std::fmod(music.beat / 32.0, 1.0));
    const float orbit2 = (float)std::cos(TAU * std::fmod(music.beat / 32.0, 1.0));
    p0 = p0 + vec3(3.f * orbit, 1.5f * orbit2, 0.f) * wp;
    vec3 keyCol = lerp(pal_.key, pal_.warm, warm) * key;
    // the drop: the key turns into the first gel and goes low to the side, the second gel comes in from the other
    // side: two colored copies of the stack's shadow climb the wall, each lit by the other gel
    // on the drop's downbeat they swing in from far out (the shadow copies sweep in across the wall and settle)
    const float sw = dropSwing_ * dropSwing_ * (3.f - 2.f * dropSwing_);
    const vec3 ga = GEL_A + vec3(2.5f * orbit - 30.f * sw, 1.2f * orbit2 + 4.f * sw, 0.f),
               gb = GEL_B + vec3(2.5f * orbit + 30.f * sw, -1.2f * orbit2 + 4.f * sw, 0.f);
    p0 = lerp(p0, ga, d);
    vec3 p1 = gb;
    radius = lerpf(radius, 1.0f, d);
    shadow = lerpf(shadow, 1.0f, d);
    // a dark room (no ambient to speak of): the key is a spotlight, a hard pool of light on the black wall with the
    // shadow play inside it; its gels stay narrower beams on the black
    const float dark = 1.f - smoothstepf(0.05f, 0.3f, ugl::luma(pal_.amb));
    const float flare = (1.f + 0.18f * swell_ * d) * lerpf(1.f, 0.85f, dark);
    vec3 c0 = lerp(keyCol, pal_.gelA * 1.3f * flare, d);
    vec3 c1 = pal_.gelB * 1.3f * flare * d;
    vec3 ambC = pal_.amb * lerpf(amb, 0.14f, d);
    ambC = lerp(ambC, (pal_.gelA + pal_.gelB) * 0.12f, d * 0.3f * (1.f - dark));
    p0.x *= side_;
    p1.x *= side_;
    // slow motion: evening, one warm lamp hanging over the well
    const float s = smoothstepf(0.f, 1.f, slow_);
    p0 = lerp(p0, vec3(-2.5f * side_, 17.f, 8.f), s);
    c0 = lerp(c0, lin(0xffb46a) * 1.05f, s);
    c1 = c1 * (1.f - s);
    ambC = lerp(ambC, lin(0x2a3550) * 0.9f, s);
    radius = lerpf(radius, 0.8f, s);
    // game over: the lamp sinks to the floor (the shadows grow up the wall) and fades
    const float o = over_;
    p0 = lerp(p0, vec3(-14.f * side_, -10.f, 14.f), o);
    c0 = c0 * (1.f - 0.7f * o);
    c1 = c1 * (1.f - o);
    ambC = ambC * (1.f - 0.45f * o);
    const float e = pal_.exposure;
    l0_ = {p0, c0 * e, radius, 0.f};
    l1_ = {p1, c1 * e, 1.0f, 0.f};
    amb_ = ambC * e;
    ambB_ = amb_ * 0.95f;
    shadowAmt_ = shadow;
    pool_ = vec4(p0.x * 0.4f, 2.f + p0.y * 0.2f, lerpf(15.f, 10.f, dark), lerpf(pool, 2.2f, dark) * (1.f - d) * (1.f - s));
    // how lit the wall is (for the lettering's ink)
    const vec3 d0 = normalize(p0), d1 = normalize(p1);
    wallLum_ = ugl::luma(pal_.wall * (amb_ + l0_.col * std::max(0.f, d0.z) + l1_.col * std::max(0.f, d1.z)));
}

// =====================================================================================================
// Geometry
// =====================================================================================================

void WhiteRoomUniverse::tile(float x, float y, float hw, float hh, const vec3& c, float alpha, int kind, float e0, float e1,
                             float e2, float e3) {
    tiles_.push_back({{x, y, hw, hh}, {c.x, c.y, c.z, alpha}, {(float)kind, 0.f, 0.f, 0.f}, {e0, e1, e2, e3}});
}

// A block is a cube: its front face on the board's plane, the rest of it running back from it (the camera's
// perspective shows the sides turned toward it, the pane veils its back). The well's blocks also throw the shadow
// play (the occupancy map); the previews do not (far from the wall).
void WhiteRoomUniverse::block(float x, float y, float s, const vec3& c, float alpha, float plaster, int kind) {
    const float h = 0.45f * s;
    const CubeInst ci{{x, y, -h, h}, {1.f, 0.f, 0.f, alpha}, {0.f, 1.f, 0.f, plaster}, {c.x, c.y, c.z, (float)kind}};
    (alpha >= 0.999f ? board_ : boardFade_).push_back(ci);
    if (kind == 0) occ_.push_back({{x, y, 0.46f * s, alpha}});
}

void WhiteRoomUniverse::buildBoard(const Game& g) {
    occ_.clear();
    board_.clear();
    boardFade_.clear();
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            // the locked piece presses into the panel for an instant
            const float age = c.flash > 0.f ? (1.f - c.flash) * Game::LOCK_FX_SECONDS : 9.f;
            const float pr = age < 0.4f ? press_ * 0.07f * std::exp(-age * 6.f) : 0.f;
            block(p.x, p.y, 1.f - pr, pal_.piece[c.type], 1.f, 0.f, 0);
        }
    // A topped-out stack turning to plaster.
    for (const Dying& d : dying_)
        block(d.x, d.y, 1.f, pal_.piece[d.type], 1.f, 0.85f * smoothstepf(0.f, 0.8f, d.t - d.delay), 0);
    // The falling piece and its ghost.
    if (g.hasPiece()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        const vec3 col = pal_.piece[p.type];
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (auto& c : gc) {
                if (c[1] < Game::HIDDEN) continue;
                const vec3 w = layout::cellPos((float)c[0], (float)c[1]);
                tile(w.x, w.y, 0.5f, 0.5f, col, 0.9f, 1);
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (auto& c : cells) {
            if (c[1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(c[0] + dx, c[1] + dy);
            block(w.x, w.y, 1.f, col, 1.f, 0.f, 0);
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
            const float x = center.x + (c[0] - (minx + maxx) * 0.5f) * sc, y = center.y - (c[1] - (miny + maxy) * 0.5f) * sc;
            block(x, y, sc, pal_.piece[type], alpha, 0.f, 2);
        }
    };
    preview(g.holdType(), vec2{layout::HOLD_CENTER.x, layout::HOLD_CENTER.y}, layout::previewScale(0), g.holdUsed() ? 0.4f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), vec2{c.x, c.y}, layout::previewScale(i), 1.f);
    }
    // The bonus gauge: a groove in the wall.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        tile(gc.x, gc.y, R * 1.05f, R * 1.05f, vec3(1.f), 1.f, 4, saturate(gaugeShown_), g.bonusReady() ? 1.f : 0.f,
             g.bonusActive() ? 1.f : 0.f, R);
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float WhiteRoomUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
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

float WhiteRoomUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
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

// Lettering set on the wall (vinyl letters): a faint soft shadow away from the key light, then the letters.
void WhiteRoomUniverse::drawText(const Font& f, const std::vector<float>& q, const vec3& color, float alpha, float bias) {
    if (q.empty() || !f.ok) return;
    const float bridgeK = smoothstepf(0.3f, 0.95f, bridge_.amount);
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(float), q.data(), GL_STREAM_DRAW);
    const float s = height_ / 1080.f;
    const vec3 L = normalize(l0_.pos);
    const float sh = std::min(4.f, 1.6f / std::max(0.2f, L.z)) * s;
    const float shadowA = 0.16f * (1.f - bridgeK) * saturate(wallLum_ * 1.5f);
    if (shadowA > 0.01f) {
        set2f(progText_, "uShift", -L.x * sh, L.y * sh);
        set3f(progText_, "uColor", vec3(0.f));
        set1f(progText_, "uAlpha", alpha * shadowA);
        set1f(progText_, "uBias", bias);
        set1f(progText_, "uSoft", 0.06f);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 4));
    }
    set2f(progText_, "uShift", 0.f, 0.f);
    set3f(progText_, "uColor", lerp(color, bridge_.ink, bridgeK));
    set1f(progText_, "uAlpha", alpha);
    set1f(progText_, "uBias", bias);
    set1f(progText_, "uSoft", 0.f);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 4));
}

// The HUD as a gallery's wall text: the song in a medium weight, small widely spaced capitals for the labels,
// light numbers; the section in a large light weight. Dark ink on the lit wall, pale ink when the room is dark.
void WhiteRoomUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    const float lit = smoothstepf(0.04f, 0.13f, wallLum_);
    const vec3 ink = lerp(pal_.inkLight, pal_.ink, lit);
    const vec3 dim = lerp(ink, lerp(pal_.inkLight * 0.6f, pal_.wall, lit), 0.28f);
    const vec3 accent = lerp(pal_.inkLight, pal_.accent, 0.5f + 0.5f * lit);
    if (!panels.empty()) {
        std::vector<Vert> v;
        auto quad = [&](float x0, float y0, float x1, float y1, const vec3& c, float a) {
            const Vert q[6] = {{x0, y0, c.x, c.y, c.z, a}, {x1, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a},
                               {x0, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a}, {x0, y1, c.x, c.y, c.z, a}};
            v.insert(v.end(), q, q + 6);
        };
        for (const HudRect& r : panels) {
            const vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? pal_.accent : pal_.wall * (0.4f + 0.6f * lit);
            quad(r.x, r.y, r.x + r.w, r.y + r.h, c, r.alpha);
        }
        glUseProgram(progShape_);
        set2f(progShape_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(shapeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vert), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
    }
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
            // "White Room" in the medium weight, the room's name in small spaced capitals after it
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            name = titleCase(name);
            const float sz = base * 1.15f, vsz = base * 0.6f, vtr = vsz * 0.45f;
            const float wName = textWidth(fontMedium_, name, sz, 0.f);
            const float wVar = var.empty() ? 0.f : textWidth(fontLight_, var, vsz, vtr);
            const float gap = base * 0.9f;
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            textQuads(fontMedium_, name, x, h.y, sz, 0.f, q);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            drawText(fontMedium_, q, ink, a, 0.f);
            if (!var.empty()) {
                q.clear();
                textQuads(fontMedium_, var, x + wName + gap, h.y + base * 0.2f, vsz, vtr, q);
                drawText(fontMedium_, q, ink, a * 0.8f, 0.f);
            }
            continue;
        }
        case HUD_SECTION:
            place(fontLight_, titleCase(h.text), base * 1.5f, 0.f);
            drawText(fontLight_, q, accent, saturate(0.2f + h.alpha * 1.9f), 0.02f);
            continue;
        case HUD_TITLE:
            place(fontMedium_, h.text, base * 1.12f, 0.f, -base * 0.05f);
            drawText(fontMedium_, q, ink, saturate(h.alpha * 1.3f), 0.f);
            continue;
        case HUD_INFO:
            place(fontLight_, upper(h.text), base * 0.62f, base * 0.34f);
            drawText(fontLight_, q, dim, saturate(alpha * 1.4f), 0.05f);
            continue;
        case HUD_LABEL:
            place(fontMedium_, h.text, base * 0.6f, base * 0.42f);
            drawText(fontMedium_, q, dim, saturate(alpha * 1.3f), 0.02f);
            continue;
        case HUD_VALUE:
            place(fontLight_, h.text, base * 1.12f, base * 0.04f);
            drawText(fontLight_, q, ink, alpha, 0.03f);
            continue;
        case HUD_NOTICE:
            place(fontLight_, h.text, base * 1.15f, 0.f);
            drawText(fontLight_, q, accent, alpha, 0.03f);
            continue;
        case HUD_KEY:
            place(fontMedium_, h.text, base * 0.8f, base * 0.1f);
            drawText(fontMedium_, q, ink, alpha, 0.f);
            continue;
        case HUD_TOAST:
            place(fontLight_, titleCase(h.text), base * 1.6f, 0.f, -base * 0.15f);
            drawText(fontLight_, q, ink, alpha, 0.03f);
            continue;
        case HUD_TOAST_SUB:
            place(fontMedium_, h.text, base * 0.75f, base * 0.3f);
            drawText(fontMedium_, q, accent, alpha * 0.9f, 0.f);
            continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontLight_, titleCase(h.text), base * 1.35f, 0.f, -base * 0.1f);
                drawText(fontLight_, q, ink, alpha, 0.03f);
            } else {
                place(fontMedium_, h.text, base * 0.8f, base * 0.16f);
                drawText(fontMedium_, q, h.accent ? accent : ink, alpha, 0.f);
            }
            continue;
        default:
            place(fontMedium_, h.text, base * 0.8f, base * 0.12f);
            drawText(fontMedium_, q, h.accent ? accent : ink, alpha, 0.f);
            continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void WhiteRoomUniverse::setLightUniforms(GLuint p) {
    set3f(p, "uL0", l0_.pos);
    set3f(p, "uC0", l0_.col);
    set1f(p, "uS0", l0_.radius);
    set1f(p, "uSide0", l0_.side);
    set3f(p, "uL1", l1_.pos);
    set3f(p, "uC1", l1_.col);
    set1f(p, "uS1", l1_.radius);
    set1f(p, "uSide1", l1_.side);
}

// The room's light and shadows (the room and the cubes share them): the lamps, the ambient, the occupancy map, the
// piles' heightmap, the shadow maps.
void WhiteRoomUniverse::setRoomUniforms(GLuint p) {
    setLightUniforms(p);
    set3f(p, "uAmb", amb_);
    set1f(p, "uFloor", FLOOR);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, occTex_);
    set1i(p, "uOcc", 0);
    set4f(p, "uOccRect", occRect_.x, occRect_.y, occRect_.z, occRect_.w);
    set1f(p, "uOccLod", std::log2((float)OCC_N));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, hmTex_);
    set1i(p, "uHm", 1);
    set4f(p, "uHmRect", HM_X0, HM_Z0, HM_NX * HM_CELL, HM_NZ * HM_CELL);
    for (int i = 0; i < 2; i++) {
        glActiveTexture(GL_TEXTURE2 + i);
        glBindTexture(GL_TEXTURE_2D, shTex_[i]);
        glBindSampler(2 + i, shCmpSampler_);
        glActiveTexture(GL_TEXTURE4 + i);
        glBindTexture(GL_TEXTURE_2D, shTex_[i]);
        glBindSampler(4 + i, shRawSampler_);
        glActiveTexture(GL_TEXTURE6 + i);
        glBindTexture(GL_TEXTURE_2D, shCovTex_[i]);
    }
    glActiveTexture(GL_TEXTURE0);
    set1i(p, "uSh0", 2);
    set1i(p, "uSh1", 3);
    set1i(p, "uShD0", 4);
    set1i(p, "uShD1", 5);
    set1i(p, "uShC0", 6);
    set1i(p, "uShC1", 7);
    setMat(p, "uShVP0", shVP_[0]);
    setMat(p, "uShVP1", shVP_[1]);
    set4f(p, "uShP0", shP_[0].x, shP_[0].y, shP_[0].z, shP_[0].w);
    set4f(p, "uShP1", shP_[1].x, shP_[1].y, shP_[1].z, shP_[1].w);
    set1f(p, "uShadowK", shadowK_);
    set1f(p, "uShadowAmt", shadowAmt_);
}

// Instances [first, first + count) of the cube buffer.
void WhiteRoomUniverse::drawCubes(int first, int count) {
    if (count <= 0) return;
    glBindVertexArray(cubeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    for (int i = 0; i < 4; i++)
        glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, sizeof(CubeInst),
                              (void*)(sizeof(CubeInst) * (size_t)first + sizeof(float) * 4 * i));
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count);
}

// The thrown cubes (the first `count` instances) seen from each lamp: a perspective from the lamp framing their
// bounds (lo, hi). A lamp that is off (or no cube) has no map.
void WhiteRoomUniverse::renderShadowMaps(int count, const vec3& lo, const vec3& hi) {
    shP_[0] = shP_[1] = vec4(0.f, 0.f, 0.f, 0.f);
    if (count <= 0) return;
    const vec3 c = (lo + hi) * 0.5f;
    for (int i = 0; i < 2; i++) {
        const Light& L = i == 0 ? l0_ : l1_;
        if (ugl::luma(L.col) < 1e-3f) continue;
        vec3 f = c - L.pos;
        if (length(f) < 1e-3f) f = vec3(0.f, -1.f, 0.f);
        f = normalize(f);
        const vec3 up = std::fabs(f.y) > 0.9f ? vec3(0.f, 0.f, 1.f) : vec3(0.f, 1.f, 0.f);
        const mat4 view = mat4::lookAt(L.pos, L.pos + f, up);
        float m = 0.f, dmin = 1e9f, dmax = 0.f;
        bool wide = false;
        for (int k = 0; k < 8; k++) {
            const vec3 q((k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z);
            const vec4 v = view * vec4(q, 1.f);
            const float d = -v.z;
            if (d < 0.3f) { wide = true; continue; }
            m = std::max(m, std::max(std::fabs(v.x), std::fabs(v.y)) / d);
            dmin = std::min(dmin, d);
            dmax = std::max(dmax, d);
        }
        const float tMax = std::tan(80.f * PI / 180.f);
        const float tanHalf = wide ? tMax : std::min(m * 1.04f + 0.01f, tMax);
        const float zn = wide ? 0.3f : std::max(0.3f, dmin - 1.f), zf = std::max(dmax, zn + 1.f) + 2.f;
        shVP_[i] = mat4::perspective(2.f * std::atan(tanHalf), 1.f, zn, zf) * view;
        shP_[i] = vec4(1.f, tanHalf, zn, zf);
        glBindFramebuffer(GL_FRAMEBUFFER, shFbo_[i]);
        glViewport(0, 0, SH_N, SH_N);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 3.f);
        glUseProgram(progShadow_);
        setMat(progShadow_, "uVP", shVP_[i]);
        drawCubes(0, count);
        glBindTexture(GL_TEXTURE_2D, shCovTex_[i]);
        glGenerateMipmap(GL_TEXTURE_2D);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    glDisable(GL_DEPTH_TEST);
}

void WhiteRoomUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const Palette target = paletteOf(variant_);
        if (!palSet_) { pal_ = target; palSet_ = true; }
        else lerpPalette(pal_, target, 1.f - std::exp(-dt * 1.2f));
    }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 1.0f, dt);
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.5f : 0.f), 3.f, dt);
    if (overT_ >= 0.f) {
        if (!game.over()) {
            overT_ = -1.f;
            dying_.clear();
        } else overT_ += dt;
    }
    over_ = overT_ >= 0.f ? smoothstepf(0.3f, 4.f, overT_) : approach(over_, 0.f, 1.5f, dt);
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.25f, slow_);
    if (!f.paused) simulate(game, music, dt, wdt);
    computeLights(music);
    const float bridge = saturate(bridge_.amount);
    const float shadowK = 1.f - smoothstepf(0.f, 0.7f, bridge);

    tiles_.clear();
    buildBoard(game);

    // ---- The cubes this frame: [thrown | thrown, fading | the board's | the board's, translucent].
    thrown_.clear();
    thrownFade_.clear();
    vec3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (const Ejected& e : ejected_) {
        const float a = saturate((e.life - e.age) / FADE);
        if (a <= 0.f) continue;
        const vec3 c = pal_.piece[e.color];
        const CubeInst ci{{e.p.x, e.p.y, e.p.z, e.h}, {e.ax[0].x, e.ax[0].y, e.ax[0].z, a}, {e.ax[1].x, e.ax[1].y, e.ax[1].z, 0.f},
                          {c.x, c.y, c.z, 1.f}};
        (a >= 1.f ? thrown_ : thrownFade_).push_back(ci);
        const float r = e.h * 1.74f;
        lo = vec3(std::min(lo.x, e.p.x - r), std::min(lo.y, e.p.y - r), std::min(lo.z, e.p.z - r));
        hi = vec3(std::max(hi.x, e.p.x + r), std::max(hi.y, e.p.y + r), std::max(hi.z, e.p.z + r));
    }
    // nearest first (the piles' hidden faces fail the depth test before they are shaded)
    {
        const vec3 eye = cam_.eye;
        auto nearer = [&](const CubeInst& a, const CubeInst& b) {
            const vec3 da(a.a0[0] - eye.x, a.a0[1] - eye.y, a.a0[2] - eye.z), db(b.a0[0] - eye.x, b.a0[1] - eye.y, b.a0[2] - eye.z);
            return dot(da, da) < dot(db, db);
        };
        std::sort(thrown_.begin(), thrown_.end(), nearer);
    }
    const int n0 = (int)thrown_.size(), n1 = (int)thrownFade_.size(), n2 = (int)board_.size(), n3 = (int)boardFade_.size();
    {
        std::vector<CubeInst>& all = thrown_;
        all.insert(all.end(), thrownFade_.begin(), thrownFade_.end());
        all.insert(all.end(), board_.begin(), board_.end());
        all.insert(all.end(), boardFade_.begin(), boardFade_.end());
        glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
        glBufferData(GL_ARRAY_BUFFER, all.size() * sizeof(CubeInst), all.empty() ? nullptr : all.data(), GL_STREAM_DRAW);
    }

    // ---- The piles' heightmap (what lies on the floor darkens it).
    if (hmDirty_) {
        for (int i = 0; i < HM_NX * HM_NZ; i++) hmUp_[i] = hm_[i] - hmBase_[i];
        glBindTexture(GL_TEXTURE_2D, hmTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, HM_NX, HM_NZ, GL_RED, GL_FLOAT, hmUp_.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        hmDirty_ = false;
    }


    // ---- The occupancy map.
    glBindFramebuffer(GL_FRAMEBUFFER, occFbo_);
    glViewport(0, 0, OCC_N, OCC_N);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    const float occX0 = -OCC_SIZE * 0.5f, occY0 = -OCC_SIZE * 0.5f, occTexel = OCC_N / OCC_SIZE;
    occRect_ = vec4(occX0, occY0, OCC_SIZE, occTexel);
    if (!occ_.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glUseProgram(progOcc_);
        set4f(progOcc_, "uOccRect", occX0, occY0, OCC_SIZE, occTexel);
        set1f(progOcc_, "uPx", 1.f / occTexel);
        glBindVertexArray(occVao_);
        glBindBuffer(GL_ARRAY_BUFFER, occVbo_);
        glBufferData(GL_ARRAY_BUFFER, occ_.size() * sizeof(Occ), occ_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)occ_.size());
        glDisable(GL_BLEND);
    }
    glBindTexture(GL_TEXTURE_2D, occTex_);
    glGenerateMipmap(GL_TEXTURE_2D);

    // ---- The thrown cubes seen from the lamps.
    renderShadowMaps(n0 + n1, lo, hi);

    // ---- The room (its color and its depth), shaded once per pixel.
    glBindFramebuffer(GL_FRAMEBUFFER, roomFbo_);
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    shadowK_ = shadowK;
    {
        glUseProgram(progRoom_);
        setRoomUniforms(progRoom_);
        const float th = std::tan(cam_.fovDeg * 0.5f * PI / 180.f);
        set3f(progRoom_, "uEye", cam_.eye);
        set2f(progRoom_, "uTan", th * width_ / (float)height_, th);
        setMat(progRoom_, "uVP", vp_);
        set3f(progRoom_, "uWall", pal_.wall);
        set3f(progRoom_, "uPanel", pal_.panel);
        set1f(progRoom_, "uWallZ", WALL_Z);
        set1f(progRoom_, "uCove", COVE);
        set4f(progRoom_, "uPool", pool_.x, pool_.y, pool_.z, pool_.w);
        set1f(progRoom_, "uBridge", smoothstepf(0.05f, 0.9f, bridge));
        set3f(progRoom_, "uBridgeCol", bridge_.color);
        set1f(progRoom_, "uPx", px_);
        ugl::drawFullscreen();
    }
    // ... copied into the multisampled scene
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_ALWAYS);
    glUseProgram(progCopy_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, roomTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, roomZTex_);
    glActiveTexture(GL_TEXTURE0);
    set1i(progCopy_, "uRoom", 0);
    set1i(progCopy_, "uRoomZ", 1);
    ugl::drawFullscreen();

    // ---- The cubes: the thrown ones, the blocks, the piece, the previews (opaque, then the fading ones).
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    if (n0 + n1 + n2 + n3 > 0) {
        glUseProgram(progCube_);
        setRoomUniforms(progCube_);
        setMat(progCube_, "uVP", vp_);
        set3f(progCube_, "uEye", cam_.eye);
        set3f(progCube_, "uPanel", pal_.panel);
        set3f(progCube_, "uPlaster", pal_.plaster);
        set3f(progCube_, "uBridgeInk", bridge_.ink);
        set3f(progCube_, "uBridgeCol", bridge_.color);
        set1f(progCube_, "uBridgeK", smoothstepf(0.2f, 0.85f, bridge));
        set1f(progCube_, "uBridgeRoom", smoothstepf(0.05f, 0.9f, bridge));
        drawCubes(0, n0);
        drawCubes(n0 + n1, n2);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        drawCubes(n0, n1);
        drawCubes(n0 + n1 + n2, n3);
    }
    glDisable(GL_CULL_FACE);

    // ---- The ghost, the gauge.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    if (!tiles_.empty()) {
        glUseProgram(progTile_);
        setMat(progTile_, "uVP", vp_);
        set1f(progTile_, "uPx", px_);
        set1f(progTile_, "uTime", worldTime_);
        const float lit = smoothstepf(0.04f, 0.13f, wallLum_);
        const vec3 ink = lerp(pal_.inkLight, pal_.ink, lit);
        set3f(progTile_, "uInk", ink);
        set3f(progTile_, "uGroove", lerp(ink, pal_.wall, 0.55f));
        set3f(progTile_, "uAccent", pal_.accent);
        set1f(progTile_, "uBridge", bridge);
        set3f(progTile_, "uBridgeInk", bridge_.ink);
        glBindVertexArray(tileVao_);
        glBindBuffer(GL_ARRAY_BUFFER, tileVbo_);
        glBufferData(GL_ARRAY_BUFFER, tiles_.size() * sizeof(Tile), tiles_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)tiles_.size());
    }
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    drawHud(f.hud, f.panels);
    glDisable(GL_BLEND);

    for (int i = 2; i < 6; i++) glBindSampler(i, 0); // (the samplers are global state)

    // ---- Resolve.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneT_.fbo);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // ---- Post.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progPost_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneT_.tex);
    set1i(progPost_, "uScene", 0);
    set2f(progPost_, "uRes", (float)width_, (float)height_);
    set1f(progPost_, "uTime", (float)f.time);
    set1f(progPost_, "uFade", saturate(f.fade));
    set1f(progPost_, "uDim", dimT_);
    set1f(progPost_, "uBridge", bridge);
    set1f(progPost_, "uExposure", 1.f);
    set1f(progPost_, "uGrain", 0.012f);
    ugl::drawFullscreen();
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_white_room() {
    static const UniverseEntry e{
        "WHITE ROOM", "a seamless white studio, matte blocks and soft shadows",
        [] { return std::unique_ptr<Universe>(new WhiteRoomUniverse()); },
        // A quiet room for almost any song; it leans a little toward clean, bright, airy mixes.
        [](const SongLook& l) { return 0.8f + 0.25f * l.brightness + 0.15f * l.airWeight; },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
