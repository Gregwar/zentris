// GARDEN 16: GARDEN as a 16-bit indie game.
//
// The same world as GARDEN (clods of earth that grow grass and flowers over a line of earth, banks rising toward the
// edges and beside the well with its outer columns, worms in the dirt, a plain sky with one sun or moon), drawn as
// pixel art at a true low resolution: the whole universe is drawn on the CPU into an indexed canvas where one board
// cell is a 12 x 12 tile (about 530 x 300 pixels on a 16:9 screen), then palette-mapped and scaled up with nearest
// filtering. Gradients are flat bands with thin ordered-dither seams, every shape has its pixel outline, and motion
// is in whole pixels with stepped sprite frames.
//
// Everything sits on the board's grid: each landed cell is a tile of soil (its piece's own light earth, a hand-made
// dirt texture with lumps and pebbles, a dark outline around each piece, a soft seam between the tiles of one piece);
// the ground is made of the same tiles, its banks stepping up tile by tile. An open tile top grows a grass cap in a
// few frames, then tufts, then (now and then) a flower that blooms frame by frame.
//
// The palette animates like a console's: the grass dries (calm, breaks), turns green (mid) and lush (chorus); a drop
// shifts the sky toward the variant's glow from the horizon, flowers burst open across the banks frame by frame,
// petals fly, butterflies swarm and the worms hurry. APRIL RAIN rains lightly through its drops (pixel streaks, tiny
// splashes on the tops). A cleared row pops: its tiles burst into 3x3 dirt bits that fall and shrink away; a hard
// landing pushes two small dust puffs out from the contact; a Tetris sends dandelion seeds up from the ground line.
#include "universes/garden16/garden16.hpp"

#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "ugl.hpp"
#include "universes/registry.hpp"

using namespace g16;

namespace {

const char* UPSCALE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uTex;
uniform vec4 uMap; // canvas origin (window pixels, top-left origin), canvas pixel size, window height
void main() {
    vec2 p = vec2(gl_FragCoord.x, uMap.w - gl_FragCoord.y);
    ivec2 q = ivec2(floor((p - uMap.xy) / uMap.z));
    q = clamp(q, ivec2(0), textureSize(uTex, 0) - ivec2(1));
    frag = vec4(texelFetch(uTex, q, 0).rgb, 1.0);
}
)";

// ---- The palette's slots.
enum : uint8_t {
    SKY0 = 0, // 6 bands, top to horizon
    SUN = 6, SUNRING, CLOUD_HI, CLOUD, CLOUD_SH,
    WELL = 11, WELL_EDGE, WELL_RAIN,
    G_OUT = 14, G_DEEP, G_SH, G_BASE, G_HI,
    PEB_HI = 19, PEB, PEB_SH,
    GR_OUT = 22, GR_SH, GR_BASE, GR_HI,
    FL0 = 26, // 4 flowers: petal, shade
    FL_C = 34, WORM, WORM_SH, INK, PALE, ACCENT, HALO, HALO2,
    RAIN = 42, GHOST, FIREFLY, FIREFLY_DIM, STAR, SEED, BIRD, DUST, ROOT, EARTH_INK,
    PIECE0 = 56, // 7 soils x 5 shades (outline, shadow, base, light, highlight)
    PAL_N = 96,
};
inline uint8_t PC(int t, int k) { return (uint8_t)(PIECE0 + t * 5 + k); }
inline uint8_t FLP(int c) { return (uint8_t)(FL0 + 2 * (c & 3)); }
inline uint8_t FLS(int c) { return (uint8_t)(FL0 + 2 * (c & 3) + 1); }

// ---- The variants (sRGB).
struct VariantDef {
    const char* name;
    unsigned sky[6];
    unsigned sun, sunRing, cloudHi, cloud, cloudSh;
    unsigned well, wellEdge, wellRain;
    unsigned ground, dry, green, lush;
    unsigned flower[4];
    unsigned center, worm, ink, pale, accent, halo, glow, rain, ghost, firefly, bird, shade, dusk;
    unsigned soil[7];
    float sunX, sunY;
    int sunR, moon;
    float stars;
    int clouds, rains, fireflies, butterflies;
};
const VariantDef VARIANTS[] = {
    // A spring day: a clear blue sky, daisies, buttercups, pink clover and violets; the drop glows blossom pink.
    {"SPRING DAY",
     {0x5b93d6, 0x6ea3de, 0x86b4e4, 0xa0c5e9, 0xbad5ec, 0xd3e4ee},
     0xfff8dc, 0xe8f1ea, 0xffffff, 0xeef4f8, 0xc2d5e6,
     0xd0e1ef, 0xebf3fa, 0xbfd2e2,
     0x7a4b33, 0xcdb46e, 0x6db349, 0x41a040,
     {0xfbf8f0, 0xf6cf3a, 0xf28aaa, 0xa98ae8},
     0xf2b52c, 0xe9a09a, 0x2a2433, 0xfbf6e8, 0xe0607e, 0xf6eedc, 0xf6aecb, 0xdceaf6, 0x8fa6ba, 0xfff0a0, 0x3d4a6a,
     0x3a2440, 0x3c3a6a,
     {0xb55a3c, 0xc99a48, 0x9a6a46, 0x7d8650, 0x5f4536, 0x7a8796, 0xd0ae78},
     -15.5f, 6.5f, 9, 0, 0.f, 3, 0, 0, 1},
    // April rain: a soft overcast sky, fresh greens, forget-me-nots; the drop darkens the sky and rains lightly.
    {"APRIL RAIN",
     {0x8ea2b2, 0x9bafbd, 0xa9bcc5, 0xb8c8cc, 0xc6d2d0, 0xd5ddd6},
     0xeef0e4, 0xc9d4d2, 0xf2f4f0, 0xdfe5e4, 0xb3c0c6,
     0xc7d3d6, 0xe0e8ea, 0xb5c3c8,
     0x6a4a38, 0xbfb57c, 0x5fb055, 0x3a9a52,
     {0xf6f6fa, 0x86bff0, 0xf0a2c4, 0xf6d862},
     0xf0c040, 0xe8a49c, 0x283038, 0xf6f8f2, 0x4f8fd0, 0xf2f2ea, 0x75889c, 0xe6f0f8, 0x8098a8, 0xfff0a0, 0x45505e,
     0x2c2c48, 0x34406a,
     {0xa85a44, 0xbf9a58, 0x8c6a52, 0x6e8a5c, 0x584438, 0x6f8496, 0xc8b088},
     12.5f, 7.5f, 8, 0, 0.f, 5, 1, 0, 1},
    // A golden dusk: a purple sky over an orange horizon, a low sun, cream and coral flowers; the drop glows magenta.
    {"GOLDEN DUSK",
     {0x3d3f78, 0x5a4886, 0x86578c, 0xbf6a7c, 0xe8906c, 0xf7bd7c},
     0xffe6aa, 0xfbb978, 0xffd2a8, 0xeea08e, 0xb07088,
     0xf0c8b4, 0xfde4d4, 0xe2b4a4,
     0x5a3430, 0xcf9e60, 0x7c9e48, 0x5a8e3c,
     {0xfff0dc, 0xff8a5c, 0xffc84a, 0xd890e0},
     0xffc23a, 0xe8988c, 0x2c1e30, 0xfff2e2, 0xff7a6a, 0xfbe8d6, 0xd8589c, 0xf0e0e8, 0xb08c96, 0xfff0a0, 0x3a2840,
     0x3a1c3a, 0x2a2056,
     {0xc0603e, 0xd6a24c, 0xa06a44, 0x8a8a4e, 0x6a4234, 0x8a7f98, 0xdcb47c},
     -14.f, -6.5f, 13, 0, 0.f, 3, 0, 0, 1},
    // A firefly night: a deep blue night with a moon and stars, pastel earths, glowing flowers; the drop glows violet.
    {"FIREFLY NIGHT",
     {0x0f1530, 0x141c40, 0x19244e, 0x1f2c5a, 0x263565, 0x2f3f70},
     0xe8ecf8, 0x56649a, 0x4a5888, 0x343e6c, 0x252c54,
     0x212a52, 0x3a4880, 0x2a3460,
     0x3a2a36, 0x6a6c58, 0x2e7058, 0x2a8466,
     {0xf0f4ff, 0xa8e0ff, 0xffb8e0, 0xd8f0a0},
     0xfff0a0, 0xc48a90, 0x1a1c2e, 0xeef0ff, 0xffd870, 0x2a3058, 0x5a46a8, 0x9aa8d0, 0x7a88c0, 0xfff2a0, 0x0a0e20,
     0x101028, 0x0c1030,
     {0xc88466, 0xdab276, 0xa88468, 0x9aa074, 0x8a6e60, 0x98a6b8, 0xe2caa0},
     14.5f, 6.f, 8, 1, 1.f, 2, 0, 1, 0},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));
const VariantDef& VAR(int v) { return VARIANTS[((v % VARIANT_COUNT) + VARIANT_COUNT) % VARIANT_COUNT]; }

struct C3 { float r, g, b; };
C3 H(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }
C3 mix(C3 a, C3 b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
C3 mul(C3 a, float k) { return {a.r * k, a.g * k, a.b * k}; }
float lum(C3 c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
vec3 lin(unsigned v) { return ugl::toLinear(vec3(((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f)); }

// A 16-bit ramp from one color: shadows lean to the variant's shade (cool), lights to a warm white.
void ramp5(C3 b, C3 shade, C3* o) {
    o[0] = mix(mul(b, 0.34f), shade, 0.55f);
    o[1] = mix(mul(b, 0.8f), shade, 0.2f);
    o[2] = b;
    o[3] = mix(b, C3{1.f, 0.97f, 0.88f}, 0.3f);
    o[4] = mix(b, C3{1.f, 1.f, 0.95f}, 0.58f);
}
void ramp4(C3 b, C3 shade, C3* o) {
    o[0] = mix(mul(b, 0.42f), shade, 0.45f);
    o[1] = mix(mul(b, 0.78f), shade, 0.15f);
    o[2] = b;
    o[3] = mix(b, C3{1.f, 1.f, 0.82f}, 0.3f);
}

std::string fold(const std::string& in) {
    std::string s;
    for (size_t i = 0; i < in.size(); i++) {
        if ((unsigned char)in[i] == 0xC2 && i + 1 < in.size() && (unsigned char)in[i + 1] == 0xB7) { s += '\x01'; i++; continue; }
        if ((unsigned char)in[i] >= 0x80) continue;
        s += in[i];
    }
    return s;
}

float hashf(float a, float b) {
    const uint64_t h = splitmix64((uint64_t)(int64_t)std::lround(a * 1000.f) * 73856093ull ^ (uint64_t)(int64_t)std::lround(b * 1000.f) * 19349663ull);
    return (h >> 40) * (1.0f / 16777216.0f);
}

// GARDEN's banks, not mirrored: per song one side rises tall and far, the other stays low.
vec4 groundSide(float seed, bool right) {
    const bool tall = (hashf(seed, 5.f) < 0.5f) == right;
    const float k = right ? 1.f : 0.f;
    const float h1 = hashf(seed + k * 7.3f, 1.f), h2 = hashf(seed + k * 7.3f, 2.f), h3 = hashf(seed + k * 7.3f, 3.f),
                h4 = hashf(seed + k * 7.3f, 4.f);
    const float lip = tall ? 0.75f + 0.4f * h1 : 0.3f + 0.35f * h1;
    const float bank = tall ? 3.2f + 1.8f * h2 : 0.7f + 1.1f * h2;
    const float s = tall ? 9.5f + 3.f * h3 : 11.f + 5.f * h3;
    return vec4{lip, bank, s, s + (tall ? 9.f : 7.f) + 8.f * h4};
}
float groundYBase(float x, float seed) {
    const float a = std::fabs(x);
    const vec4 g = groundSide(seed, x > 0.f);
    const float side = smoothstepf(6.f, 12.f, a);
    return -10.05f + g.x * smoothstepf(5.5f, 7.6f, a) + 0.25f * smoothstepf(7.6f, 11.f, a) + g.y * std::pow(smoothstepf(g.z, g.w, a), 1.3f) +
           (0.22f * std::sin(x * 0.37f + seed) + 0.12f * std::sin(x * 0.91f + seed * 1.7f) + 0.3f * std::sin(x * 0.17f + seed * 2.1f)) * side;
}

const int NDX[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
const int NDY[8] = {0, 0, 1, -1, 1, 1, -1, -1};
enum { M_L = 1, M_R = 2, M_D = 4, M_U = 8, M_LD = 16, M_RD = 32, M_LU = 64, M_RU = 128 };

const char* LEVEL_WORDS[3] = {"DRY", "GREEN", "BLOOM"};

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

Garden16Universe::~Garden16Universe() {
    if (prog_) glDeleteProgram(prog_);
    if (tex_) glDeleteTextures(1, &tex_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool Garden16Universe::init(int width, int height) {
    prog_ = ugl::program(ugl::FULLSCREEN_VS, UPSCALE_FS, "garden16 upscale");
    glGenVertexArrays(1, &vao_);
    buildPatterns();
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

void Garden16Universe::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    // The native grid: a board cell is CELL canvas pixels, the well's top-left corner on a canvas pixel corner.
    const layout::Rect r = layout::boardRect(width_, height_);
    s_ = std::max(0.25f, (r.y1 - r.y0) / (layout::BOARD_H * CELL));
    bx0_ = (int)std::ceil(r.x0 / s_);
    by0_ = (int)std::ceil(r.y0 / s_);
    ox_ = r.x0 - bx0_ * s_;
    oy_ = r.y0 - by0_ * s_;
    W_ = (int)std::ceil((width_ - ox_) / s_) + 1;
    H_ = (int)std::ceil((height_ - oy_) / s_) + 1;
    floor_ = by0_ + 20 * CELL;
    cMin_ = floorDiv(-bx0_, CELL) - 1;
    cMax_ = floorDiv(W_ - bx0_, CELL) + 1;
    tops_.assign(cMax_ - cMin_ + 1, floor_);
    cv_.resize(W_, H_);
    hud_.resize(W_, H_);
    rgba_.assign((size_t)W_ * H_ * 4, 255);
    if (tex_) glDeleteTextures(1, &tex_);
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W_, H_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    clouds_.clear();
    rain_.clear();
    worms_.clear();
}

// Eight hand-made dirt tiles (0 base, 1 shadow lump, 2 light grain, 3..5 a pebble), shared by the ground and the
// clods: a few lumps with a lit grain above each, a pebble on every other tile.
void Garden16Universe::buildPatterns() {
    Rng r(0x16B17ull);
    for (int p = 0; p < 8; p++) {
        auto& t = pat_[p];
        std::memset(t, 0, sizeof(t));
        const int lumps = 3 + (p % 3 == 0 ? 1 : 0);
        for (int l = 0; l < lumps; l++) {
            const int x = r.irange(1, 8), y = r.irange(2, 10), L = r.irange(2, 3);
            for (int k = 0; k < L && x + k < CELL; k++) t[y][x + k] = 1;
            if (t[y - 1][x + 1] == 0) t[y - 1][x + 1] = 2;
        }
        for (int s = 0; s < 2; s++) {
            const int x = r.irange(1, 10), y = r.irange(1, 10);
            if (t[y][x] == 0) t[y][x] = 2;
        }
        if (p % 2 == 0) {
            const int x = r.irange(2, 8), y = r.irange(2, 7);
            t[y][x] = 3; t[y][x + 1] = 4; t[y + 1][x] = 4; t[y + 1][x + 1] = 5;
            if (p % 4 == 0) { t[y][x + 2] = 4; t[y + 1][x + 2] = 5; }
            t[y + 2][x] = 1; t[y + 2][x + 1] = 1;
        }
    }
}

// =====================================================================================================
// Song, level
// =====================================================================================================

// Bright major songs in the spring day or at the golden dusk; minor songs in the April rain or the firefly night.
int Garden16Universe::pickVariant(const SongLook& t) const {
    float h = std::fmod(t.hue / TAU, 1.f);
    if (h < 0) h += 1.f;
    int v;
    if (t.hasSong && !t.minor) v = h < 0.5f ? 0 : 2;
    else if (t.hasSong) v = h < 0.5f ? 1 : 3;
    else v = (int)(h * VARIANT_COUNT) % VARIANT_COUNT;
    if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
    return v;
}

void Garden16Universe::setSong(const SongLook& look, float seconds) {
    (void)seconds;
    variant_ = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    seed_ = look.seed;
    seedF_ = (float)(look.seed % 1000) * 0.0137f;
    seedU_ = (uint32_t)(look.seed * 2654435761ull) | 1u;
    rng_ = Rng(look.seed ^ 0x6A16ull);
    clouds_.clear();
}

void Garden16Universe::setLevel(int level, float energy, float seconds, float impact) {
    (void)impact;
    level = std::clamp(level, 0, 2);
    energy_ = energy;
    if (!hasLevel_ || seconds < 0.1f) {
        hasLevel_ = true;
        level_ = level;
        lv_ = lvFrom_ = lvTo_ = (float)level;
        lvT_ = 1.f;
        const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
        dry_ = 1.f - 0.65f * wm - 0.35f * wp;
        grassH_ = 0.45f + 0.35f * wm + 0.6f * wp;
        density_ = 0.3f + 0.45f * wm + 0.25f * wp;
        lush_ = wp;
        return;
    }
    level_ = level;
    lvFrom_ = lv_;
    lvTo_ = (float)level;
    lvT_ = 0.f;
    lvDur_ = std::clamp(seconds, 1.5f, 6.f);
}

void Garden16Universe::drop(float strength) {
    (void)strength;
    dropPending_ = 2.5f;
}

void Garden16Universe::refresh(float strength) {
    gust_ = std::max(gust_, 0.35f * strength);
    const int n = 2 + (int)(strength * 4.f);
    for (int i = 0; i < n; i++) {
        const int c = rng_.chance(0.5f) ? rng_.irange(cMin_, -2) : rng_.irange(11, cMax_);
        Part p{};
        p.x = (float)(bx0_ + c * CELL + rng_.irange(0, 11));
        p.y = (float)(topOf(c) - rng_.irange(3, 8));
        p.vx = (c < 0 ? -1.f : 1.f) * rng_.range(2.f, 6.f);
        p.vy = -rng_.range(4.f, 9.f);
        p.life = rng_.range(6.f, 9.f); p.grav = 0.2f; p.drag = 0.15f; p.kind = P_SEED; p.seed = rng_.uniform() * 50.f;
        parts_.push_back(p);
    }
}

void Garden16Universe::levelUp() { refresh(1.f); }

TransitionHints Garden16Universe::hints() const {
    const VariantDef& V = VAR(variant_);
    TransitionHints h;
    h.background = lerp(lin(V.sky[2]), lin(V.sky[4]), 0.5f);
    h.light = ugl::luma(h.background) > 0.28f;
    h.ink = lin(V.soil[1]);
    h.accent = lin(V.accent);
    h.styles = TS_INK | TS_LUMA | TS_REBUILD;
    return h;
}

// =====================================================================================================
// The ground's columns
// =====================================================================================================

// Whole tiles above the well's floor for a column of the ground (0 under the well).
int Garden16Universe::baseRows(int c) const {
    if (c >= 0 && c < Game::W) return 0;
    const float u = (float)c - 5.f + 0.5f;
    const float gy = groundYBase(u, seedF_) + 10.f;
    int h = std::max(0, (int)std::floor(gy + 0.5f));
    // beside the well, the bank follows the earth in the well's outer column (GARDEN's shelf), tile by tile
    const float r = raise_[c < 0 ? 0 : 1];
    const float a = std::fabs(u);
    const float shelf = r * std::exp(-std::max(0.f, a - 5.f) / (2.5f + 0.35f * r));
    h = std::max(h, (int)std::floor(shelf + 0.35f));
    return h;
}

void Garden16Universe::computeTops() {
    for (int c = cMin_; c <= cMax_; c++) tops_[c - cMin_] = floor_ - baseRows(c) * CELL;
}

int Garden16Universe::topOf(int c) const {
    c = std::clamp(c, cMin_, cMax_);
    return tops_[c - cMin_];
}

int Garden16Universe::tileAt(int X, int Y, int* gx, int* gy) const {
    const int x = floorDiv(X - bx0_, CELL), row = floorDiv(Y - by0_, CELL);
    const int y = row + Game::HIDDEN;
    if (x < 0 || x >= Game::W || y < 0 || y >= Game::H) return 0;
    if (gx) *gx = x;
    if (gy) *gy = y;
    return soil_[y][x].id >= 0 ? 1 : 0;
}

bool Garden16Universe::solidAt(int X, int Y) const {
    if (Y >= floor_ && X >= bx0_ && X < bx0_ + Game::W * CELL) return true;
    const int c = floorDiv(X - bx0_, CELL);
    if (Y >= topOf(c)) return true;
    return tileAt(X, Y, nullptr, nullptr) != 0;
}

bool Garden16Universe::earthAt(int X, int Y) const {
    auto in = [&](int x, int y) {
        if (x < 1 || x >= W_ - 1 || y >= H_ - 1) return false;
        const int c = floorDiv(x - bx0_, CELL);
        if (y >= topOf(c) + 5) return true;
        return y >= by0_ && tileAt(x, y, nullptr, nullptr) != 0;
    };
    return in(X, Y) && in(X + 3, Y) && in(X - 3, Y) && in(X, Y + 3) && in(X, Y - 3);
}

// =====================================================================================================
// Game events
// =====================================================================================================

int Garden16Universe::maskOf(int x, int y) const {
    const Soil& s = soil_[y][x];
    int m = 0;
    for (int i = 0; i < 8; i++) {
        const int nx = x + NDX[i], ny = y + NDY[i];
        if (nx < 0 || nx >= Game::W || ny < 0 || ny >= Game::H) continue;
        if (s.id >= 0 && soil_[ny][nx].id == s.id) m |= 1 << i;
    }
    return m;
}

void Garden16Universe::puff(int X, int Y, float dir) {
    Part p{};
    p.x = (float)X; p.y = (float)Y;
    p.vx = dir * rng_.range(38.f, 52.f); p.vy = -rng_.range(4.f, 9.f);
    p.life = 0.34f; p.drag = 7.f; p.kind = P_PUFF; p.col = DUST; p.real = false;
    parts_.push_back(p);
    for (int i = 0; i < 3; i++) {
        Part q{};
        q.x = (float)X; q.y = (float)Y - 1.f;
        q.vx = dir * rng_.range(30.f, 70.f); q.vy = -rng_.range(20.f, 45.f);
        q.life = rng_.range(0.25f, 0.4f); q.grav = 260.f; q.drag = 1.f; q.kind = P_DUST; q.col = DUST;
        parts_.push_back(q);
    }
}

// A tile bursts into sixteen 3x3 bits of its own pixels, thrown out and falling, shrinking away.
void Garden16Universe::chunks(int X0, int Y0, int type, int mask, uint32_t seed, float age, float delay, float power, bool real) {
    uint8_t t[CELL][CELL];
    for (int j = 0; j < CELL; j++)
        for (int i = 0; i < CELL; i++) t[j][i] = tilePx(type, mask, seed, i, j);
    if (age > 1.f && !(mask & M_U))
        for (int i = 1; i < CELL - 1; i++) { t[0][i] = GR_OUT; t[1][i] = GR_HI; t[2][i] = GR_BASE; }
    for (int cj = 0; cj < 4; cj++)
        for (int ci = 0; ci < 4; ci++) {
            Part p{};
            bool any = false;
            for (int k = 0; k < 9; k++) {
                p.px[k] = t[cj * 3 + k / 3][ci * 3 + k % 3];
                any |= p.px[k] != 255;
            }
            if (!any) continue;
            const float ox = (ci * 3 + 1.5f - 6.f) / 6.f, oy = (cj * 3 + 1.5f - 6.f) / 6.f;
            p.x = (float)(X0 + ci * 3); p.y = (float)(Y0 + cj * 3);
            p.vx = ox * rng_.range(30.f, 75.f) * power + rng_.range(-12.f, 12.f);
            p.vy = -rng_.range(35.f, 105.f) * power + oy * 25.f;
            p.grav = 560.f; p.drag = 0.4f;
            p.life = rng_.range(0.36f, 0.5f);
            p.age = -delay;
            p.kind = P_CHUNK; p.real = real;
            parts_.push_back(p);
        }
}

void Garden16Universe::onEvent(const GameEvent& ev, const Game& game) {
    (void)game;
    switch (ev.type) {
    case GameEvent::HardDrop: lastDrop_ = ev.count; break;
    case GameEvent::Lock: {
        if (ev.cells.empty()) break;
        const int id = nextId_++;
        const uint32_t pseed = hash2(id, 77, seedU_);
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            Soil& s = soil_[c.y][c.x];
            s.id = id; s.type = std::clamp((int)c.type, 0, 6);
            s.age = 0.f;
            s.seed = hash2(c.x, c.y, pseed);
        }
        // a hard landing pushes two small puffs of dust out from the contact, one each side of the piece's base
        if (lastDrop_ >= 2 && bridge_.amount < 0.5f) {
            int maxY = -1;
            for (auto& c : ev.cells) maxY = std::max(maxY, c.y);
            int lo = 99, hi = -1;
            for (auto& c : ev.cells)
                if (c.y == maxY) { lo = std::min(lo, c.x); hi = std::max(hi, c.x); }
            const int Y = by0_ + (maxY - Game::HIDDEN + 1) * CELL - 2;
            puff(bx0_ + lo * CELL - 1, Y, -1.f);
            puff(bx0_ + (hi + 1) * CELL, Y, 1.f);
        }
        lastDrop_ = 0;
        break;
    }
    case GameEvent::Clear: {
        bool cleared[Game::H] = {};
        int lowest = -1;
        for (auto& c : ev.cells)
            if (c.y >= 0 && c.y < Game::H) { cleared[c.y] = true; lowest = std::max(lowest, c.y); }
        const float power = 1.f + 0.15f * (float)(std::min(ev.count, 4) - 1);
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const Soil& s = soil_[c.y][c.x];
            const int type = std::clamp((int)c.type, 0, 6);
            const bool exposed = c.y == 0 || soil_[c.y - 1][c.x].id < 0 || cleared[c.y - 1];
            chunks(bx0_ + c.x * CELL, by0_ + (c.y - Game::HIDDEN) * CELL, type, s.id >= 0 ? maskOf(c.x, c.y) : 0, s.seed,
                   exposed ? s.age : 0.f, 0.f, power, true);
        }
        // a worm in a cleared row (or above it) fades where it is
        for (Worm& w : worms_) {
            int gx, gy;
            if (tileAt(w.tx[0], w.ty[0], &gx, &gy) && gy <= lowest) w.state = 2;
        }
        // the rows collapse like the board's
        Soil next[Game::H][Game::W];
        int dst = Game::H - 1;
        for (int y = Game::H - 1; y >= 0; y--) {
            if (cleared[y]) continue;
            for (int x = 0; x < Game::W; x++) next[dst][x] = soil_[y][x];
            dst--;
        }
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) soil_[y][x] = next[y][x];
        if (ev.count >= 4) {
            // a Tetris: dandelion seeds rise from the whole ground line, the stack's flowers open at once
            rush_ = 1.f;
            for (int i = 0; i < 70; i++) {
                const int c = rng_.chance(0.5f) ? rng_.irange(cMin_, -1) : rng_.irange(10, cMax_);
                Part p{};
                p.x = (float)(bx0_ + c * CELL + rng_.irange(0, 11));
                p.y = (float)(topOf(c) - rng_.irange(1, 6));
                p.vx = (c < 0 ? -1.f : 1.f) * rng_.range(1.f, 6.f);
                p.vy = -rng_.range(6.f, 16.f);
                p.age = -rng_.range(0.f, 1.2f);
                p.life = rng_.range(6.f, 9.f); p.grav = 0.25f; p.drag = 0.12f; p.kind = P_SEED; p.seed = rng_.uniform() * 50.f;
                parts_.push_back(p);
            }
        }
        break;
    }
    case GameEvent::TopOut: {
        // game over: the stack crumbles into dirt bits, row by row from the top
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (auto& c : ev.cells) {
            if (c.y < Game::HIDDEN || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            const Soil& s = soil_[c.y][c.x];
            const int type = std::clamp((int)c.type, 0, 6);
            chunks(bx0_ + c.x * CELL, by0_ + (c.y - Game::HIDDEN) * CELL, type, s.id >= 0 ? maskOf(c.x, c.y) : 0, s.seed, 0.f,
                   0.2f + (c.y - top) * 0.07f + std::fabs(c.x - 4.5f) * 0.015f, 0.6f, true);
        }
        for (Worm& w : worms_) w.state = 2;
        for (auto& row : soil_)
            for (auto& c : row) c = Soil{};
        overT_ = 0.f;
        break;
    }
    default: break;
    }
}

// The stack's earth follows the board: cells never seen landing (a board filled for a review) are taken as they
// are, fully grown, the cells of one type next to each other as one piece.
void Garden16Universe::syncSoil(const Game& g, float gdt) {
    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            Soil& s = soil_[y][x];
            if (c.type < 0) { s = Soil{}; continue; }
            if (s.id < 0 || s.type != c.type) {
                s.id = 1000000 + c.type;
                s.type = std::clamp((int)c.type, 0, 6);
                s.seed = hash2(x, y * 7 + c.type, seedU_);
                s.age = 12.f + 8.f * hash01(x, y, 3);
            }
            const bool exposed = y == 0 || g.cell(x, y - 1).type < 0;
            if (exposed) s.age += gdt * (1.f + 5.f * rush_) * (1.f + 0.6f * bloom_);
            else s.age = 0.f;
        }
}

// =====================================================================================================
// Simulation
// =====================================================================================================

void Garden16Universe::simulate(const Game& game, const MusicState& music, float dt, float wdt) {
    worldT_ += wdt;
    hasBeat_ = music.hasSong;
    beat_ = music.beat;
    if (lvT_ < 1.f) {
        lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
        lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
    }
    float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const bool over = overT_ >= 0.f;
    const VariantDef& V = VAR(variant_);

    // the bank beside the well follows the solid run of earth in each outer column
    for (int side = 0; side < 2; side++) {
        const int col = side == 0 ? 0 : Game::W - 1;
        int n = 0;
        while (n < Game::H - Game::HIDDEN && game.cell(col, Game::H - 1 - n).type >= 0) n++;
        const float target = over ? 0.f : (float)n;
        raise_[side] += (target - raise_[side]) * (1.f - std::exp(-wdt * 3.f));
    }

    // Is this peak a drop? The HUD's section says it when there is one; else the song's drive.
    const float dropish = sectionKnown_ ? (sectionDrop_ ? 1.f : 0.f) : smoothstepf(1.27f, 1.42f, music.speed);
    const bool peak = level_ == 2;
    const float bloomTarget = over ? 0.f : (peak ? dropish : 0.f) * smoothstepf(0.5f, 1.f, wp + (peak ? 0.5f : 0.f));
    if (dropPending_ > 0.f) {
        dropPending_ -= dt;
        if (peak && dropish > 0.5f) {
            // the hit: flowers burst open across the banks, a flurry of petals, butterflies, the sky's glow swells
            dropPending_ = 0.f;
            lvT_ = 1.f; lv_ = lvTo_;
            wm = smoothstepf(0.2f, 1.f, lv_); wp = smoothstepf(1.1f, 2.f, lv_);
            bloom_ = std::max(bloom_, 0.14f);
            flurry_ = 1.f;
            gust_ = std::max(gust_, 1.f);
            swell_ = 0.3f;
            if (V.butterflies)
                for (int i = 0; i < 5; i++) {
                    Critter c{};
                    const bool left = rng_.chance(0.5f);
                    const int col = left ? rng_.irange(cMin_ + 1, -2) : rng_.irange(11, cMax_ - 1);
                    c.x = (float)(bx0_ + col * CELL + 6); c.y = (float)(topOf(col) - 4);
                    c.vx = (left ? -1.f : 1.f) * rng_.range(8.f, 20.f); c.vy = -rng_.range(20.f, 40.f);
                    c.kind = C_BUTTERFLY; c.col = (uint8_t)rng_.irange(0, 3); c.seed = rng_.uniform() * 50.f; c.life = 1e9f;
                    critters_.push_back(c);
                }
        }
    }
    drive_ = approach(drive_, bloomTarget, bloomTarget > drive_ ? 1.5f : 0.4f, dt);
    bloom_ = approach(bloom_, bloomTarget * 1.12f, bloomTarget * 1.12f > bloom_ ? 0.75f : 0.3f, dt);
    glow_ = approach(glow_, 0.62f * drive_, drive_ > glow_ ? 1.2f : 0.35f, dt);
    swell_ = approach(swell_, 0.f, 0.45f, dt);
    flurry_ = approach(flurry_, 0.f, 0.65f, dt);
    gust_ = approach(gust_, 0.f, 0.45f, dt);
    rush_ = approach(rush_, 0.f, 0.6f, dt);
    const float dryT = over ? 1.f : 1.f - 0.65f * wm - 0.35f * wp;
    dry_ = approach(dry_, dryT, dryT > dry_ ? 0.35f : 0.7f, dt);
    grassH_ = approach(grassH_, over ? 0.4f : 0.45f + 0.35f * wm + 0.6f * wp, 0.5f, dt);
    density_ = approach(density_, over ? 0.3f : 0.3f + 0.45f * wm + 0.25f * wp, 0.5f, dt);
    lush_ = approach(lush_, over ? 0.f : wp, 0.6f, dt);
    rainAmt_ = approach(rainAmt_, V.rains ? smoothstepf(0.3f, 0.8f, drive_) : 0.f, 0.7f, dt);

    const float world = 1.f - smoothstepf(0.f, 0.6f, bridge_.amount);
    // Petals off the open flowers: a flurry on the drop's downbeat, then a steady few.
    if (bloom_ > 0.2f && world > 0.3f) {
        petalAcc_ += (110.f * flurry_ + 7.f * smoothstepf(0.4f, 1.f, bloom_)) * wdt;
        int guard = 0;
        while (petalAcc_ >= 1.f && guard++ < 40) {
            petalAcc_ -= 1.f;
            const int c = rng_.chance(0.5f) ? rng_.irange(cMin_, -2) : rng_.irange(11, cMax_);
            int X, Y, color, stem;
            float u;
            if (!flowerSlot(c, rng_.irange(0, 2), X, Y, color, u, stem)) continue;
            if (bloom_ < u + 0.18f) continue;
            Part p{};
            p.x = (float)X; p.y = (float)(Y - stem - 3);
            const float side = c < 0 ? -1.f : 1.f;
            p.vx = side * rng_.range(3.f, 14.f) * (1.f + gust_) + rng_.range(-4.f, 4.f);
            p.vy = -(rng_.range(5.f, 12.f) + rng_.range(15.f, 45.f) * gust_);
            p.life = rng_.range(5.f, 8.f); p.grav = 1.5f; p.drag = 0.55f; p.kind = P_PETAL; p.col = FLP(color);
            p.col2 = FLS(color); p.seed = rng_.uniform() * 50.f;
            parts_.push_back(p);
        }
    } else petalAcc_ = 0.f;

    // A chorus (a peak that is not a drop): leaves blow off the lush grass on the wind, in gusts every two bars.
    {
        const float chorus = saturate(lush_ - drive_ * 1.5f) * world * (over ? 0.f : 1.f);
        const float gustBar = hasBeat_ ? 0.5f + 0.5f * std::cos((float)(beat_ / 8.0 * TAU)) : 0.5f;
        leafAcc_ += chorus * (10.f + 22.f * gustBar) * wdt;
        const float dir = (seedU_ & 2) ? 1.f : -1.f;
        int guard = 0;
        while (leafAcc_ >= 1.f && guard++ < 10) {
            leafAcc_ -= 1.f;
            const int c = rng_.chance(0.5f) ? rng_.irange(cMin_, -2) : rng_.irange(11, cMax_);
            Part p{};
            p.x = (float)(bx0_ + c * CELL + rng_.irange(0, 11));
            p.y = (float)(topOf(c) - rng_.irange(2, 7));
            p.vx = dir * rng_.range(18.f, 42.f); p.vy = -rng_.range(6.f, 22.f);
            p.life = rng_.range(3.5f, 6.f); p.grav = 4.f; p.drag = 0.25f; p.kind = P_LEAF;
            p.col = rng_.chance(0.5f) ? GR_HI : GR_BASE; p.col2 = GR_SH; p.seed = rng_.uniform() * 50.f;
            parts_.push_back(p);
        }
    }

    // Bars at mid and peak: a seed or two drifts up from the grass.
    if (music.hasSong) {
        const double bar = std::floor(music.beat / 4.0 + 1e-4);
        if (lastBar_ < 0.0 || bar < lastBar_ - 4.0) lastBar_ = bar;
        if (bar > lastBar_) {
            lastBar_ = bar;
            if (wm > 0.5f && world > 0.5f && !over) refresh(0.f);
        }
    }
    // Kicks at the peak: pollen puffs from a few open flowers (a drop), seeds shaken from the grass (a chorus).
    sinceKick_ += dt;
    const float kick = std::max(music.kick, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    if (kick > 0.5f && prevKick_ <= 0.5f && sinceKick_ > 0.4f && wp > 0.5f && world > 0.5f && !over) {
        sinceKick_ = 0.f;
        for (int k = 0; k < 3; k++) {
            const int c = rng_.chance(0.5f) ? rng_.irange(cMin_, -2) : rng_.irange(11, cMax_);
            int X, Y, color, stem;
            float u;
            const bool fl = bloom_ > 0.4f && flowerSlot(c, rng_.irange(0, 2), X, Y, color, u, stem) && bloom_ > u + 0.2f;
            if (!fl) { X = bx0_ + c * CELL + rng_.irange(0, 11); Y = topOf(c) - 3; stem = 0; }
            for (int i = 0; i < 4; i++) {
                Part p{};
                p.x = (float)X + rng_.range(-1.5f, 1.5f); p.y = (float)(Y - stem - 3);
                p.vx = rng_.range(-5.f, 5.f); p.vy = -rng_.range(4.f, 10.f);
                p.life = rng_.range(1.f, 1.8f); p.grav = 0.5f; p.drag = 1.f;
                p.kind = P_POLLEN; p.col = fl ? FL_C : SEED; p.seed = rng_.uniform();
                parts_.push_back(p);
            }
        }
    }
    prevKick_ = kick;

    if (!over && world > 0.5f) shedCrumbs(game, dt);
    else crumbAcc_ = 0.f;
    simulateWorms(dt, wdt);
    simulateCritters(dt, wdt);
    simulateRain(dt);

    // Clouds drift slowly (more of them, darker, under the rain).
    {
        const int want = V.clouds + (rainAmt_ > 0.3f ? 3 : 0);
        while ((int)clouds_.size() < want) {
            Cloud c{};
            const bool init = clouds_.size() < (size_t)V.clouds && worldT_ < 1.f;
            c.w = rng_.irange(26, 54);
            c.x = init ? rng_.range(-20.f, (float)W_) : (rng_.chance(0.5f) ? -(float)c.w - 4.f : (float)W_ + 4.f);
            c.y = rng_.irange(16, std::max(20, (int)(floor_ * 0.42f)));
            c.seed = (uint32_t)rng_.next();
            c.speed = rng_.range(0.8f, 2.2f) * (c.x > W_ * 0.5f ? -1.f : 1.f);
            clouds_.push_back(c);
        }
        for (Cloud& c : clouds_) c.x += c.speed * (1.f + 0.8f * wp) * wdt;
        clouds_.erase(std::remove_if(clouds_.begin(), clouds_.end(),
                                     [&](const Cloud& c) { return c.x < -c.w - 30.f || c.x > W_ + 30.f; }),
                      clouds_.end());
        if ((int)clouds_.size() > want + 1) clouds_.pop_back();
    }

    // The particles.
    std::vector<std::pair<int, int>> hits;
    const float windX = 3.f * std::sin(worldT_ * 0.23f);
    for (Part& p : parts_) {
        const float pdt = p.real ? dt : wdt;
        p.age += pdt;
        if (p.age < 0.f) continue;
        if (p.kind == P_PETAL || p.kind == P_SEED) p.vx += windX * pdt;
        if (p.kind == P_LEAF) p.vy += std::sin(p.age * 3.f + p.seed) * 30.f * pdt;
        p.vy += p.grav * pdt;
        const float k = std::exp(-p.drag * pdt);
        p.vx *= k; p.vy *= k;
        p.x += p.vx * pdt; p.y += p.vy * pdt;
        if (p.kind == P_CRUMB && p.vy > 0.f && solidAt(iround(p.x), iround(p.y))) {
            hits.push_back({iround(p.x), iround(p.y) - 1});
            p.age = p.life + 1.f;
        }
    }
    parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Part& p) { return p.age > p.life; }), parts_.end());
    for (auto& h : hits) {
        Part q{};
        q.x = (float)h.first; q.y = (float)h.second;
        q.vx = rng_.range(-15.f, 15.f); q.vy = -rng_.range(10.f, 25.f);
        q.life = 0.15f; q.grav = 300.f; q.kind = P_DUST; q.col = DUST; q.real = true;
        parts_.push_back(q);
    }
    if (parts_.size() > 3000) parts_.erase(parts_.begin(), parts_.begin() + (parts_.size() - 3000));
    gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
}

// The falling clod sheds a crumb of soil now and then from its underside.
void Garden16Universe::shedCrumbs(const Game& g, float dt) {
    if (!g.hasPiece() || dt <= 0.f) { crumbAcc_ = 0.f; return; }
    const Piece& p = g.piece();
    if (g.ghostY() <= p.y) { crumbAcc_ = 0.f; return; }
    crumbAcc_ += 3.5f * dt;
    int cells[4][2];
    g.pieceCells(p, cells);
    const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
    int guard = 0;
    while (crumbAcc_ >= 1.f && guard++ < 4) {
        crumbAcc_ -= 1.f;
        int bottom[4], nb = 0;
        for (int i = 0; i < 4; i++) {
            bool under = false;
            for (int j = 0; j < 4; j++) under |= cells[j][0] == cells[i][0] && cells[j][1] == cells[i][1] + 1;
            if (!under) bottom[nb++] = i;
        }
        if (nb == 0) break;
        const int i = bottom[rng_.irange(0, nb - 1)];
        const int X0 = bx0_ + iround((cells[i][0] + dx) * CELL), Y0 = by0_ + iround((cells[i][1] - Game::HIDDEN + dy) * CELL);
        if (Y0 < by0_) continue;
        Part q{};
        q.x = (float)(X0 + rng_.irange(2, 9)); q.y = (float)(Y0 + CELL);
        q.vy = 30.f; q.grav = 320.f; q.life = 4.f; q.kind = P_CRUMB; q.real = true;
        q.col = PC(std::clamp(p.type, 0, 6), rng_.chance(0.5f) ? 1 : 2);
        parts_.push_back(q);
    }
}

bool Garden16Universe::spawnWorm() {
    std::vector<std::pair<int, int>> stack;
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++)
            if (soil_[y][x].id >= 0) stack.push_back({bx0_ + x * CELL + 6, by0_ + (y - Game::HIDDEN) * CELL + 6});
    for (int tries = 0; tries < 20; tries++) {
        int X, Y;
        if (stack.size() >= 3 && rng_.chance(0.35f)) {
            const auto& s = stack[rng_.irange(0, (int)stack.size() - 1)];
            X = s.first; Y = s.second;
        } else {
            X = rng_.irange(4, W_ - 5);
            const int top = topOf(floorDiv(X - bx0_, CELL));
            if (top + 9 > H_ - 4) continue;
            Y = rng_.irange(top + 9, H_ - 4);
        }
        if (!earthAt(X, Y)) continue;
        bool near = false;
        for (const Worm& o : worms_) near |= std::abs(o.tx[0] - X) + std::abs(o.ty[0] - Y) < 14;
        if (near) continue;
        Worm w{};
        w.hx = (float)X; w.hy = (float)Y;
        w.heading = rng_.chance(0.5f) ? 0.f : PI;
        w.heading += rng_.range(-0.4f, 0.4f);
        w.speed = rng_.range(4.f, 6.5f);
        w.wig = rng_.range(0.f, TAU);
        w.t = rng_.range(0.f, 50.f);
        w.len = rng_.irange(6, 9);
        w.n = 1;
        for (int i = 0; i < 12; i++) { w.tx[i] = X; w.ty[i] = Y; }
        w.vis = 0.f;
        worms_.push_back(w);
        return true;
    }
    return false;
}

// A few worms crawl slowly through the dirt; a drop brings many more, and they hurry.
void Garden16Universe::simulateWorms(float dt, float wdt) {
    const float wm = smoothstepf(0.2f, 1.f, lv_);
    const bool over = overT_ >= 0.f;
    const float target = over ? 0.f : 2.f + 2.f * wm + 8.f * drive_;
    int alive = 0;
    for (const Worm& w : worms_) alive += w.state == 0;
    wormSpawnT_ -= dt;
    if (alive + 0.5f < target && wormSpawnT_ <= 0.f && spawnWorm()) wormSpawnT_ = drive_ > 0.3f ? 0.25f : 0.8f;
    wormRetireT_ -= dt;
    if (alive > target + 0.6f && wormRetireT_ <= 0.f) {
        for (Worm& w : worms_)
            if (w.state == 0) { w.state = 1; break; }
        wormRetireT_ = 0.6f;
    }
    const float mult = 1.f + 1.6f * drive_;
    for (Worm& w : worms_) {
        if (w.state == 2) { w.vis -= dt / 0.3f; continue; }
        w.vis = w.state == 1 ? w.vis - dt / 1.5f : std::min(1.f, w.vis + dt / 1.0f);
        if (!earthAt(w.tx[0], w.ty[0]) && !earthAt(w.tx[w.n / 2], w.ty[w.n / 2])) { w.state = 2; continue; }
        w.t += wdt;
        w.wig += wdt * (5.f + 4.f * drive_);
        w.heading += (0.9f * std::sin(w.t * 0.55f) + 0.6f * std::sin(w.t * 1.37f + w.wig * 0.1f)) * wdt * mult;
        // keep mostly level, like a worm in a burrow
        w.heading += -std::sin(w.heading) * std::cos(w.heading) * 0.4f * wdt;
        auto ahead = [&](float h) { return earthAt(iround(w.hx + std::cos(h) * 4.f), iround(w.hy + std::sin(h) * 4.f)); };
        if (!ahead(w.heading)) {
            bool found = false;
            for (int k = 1; k <= 6 && !found; k++)
                for (float sg : {1.f, -1.f}) {
                    const float h = w.heading + sg * k * 0.45f;
                    if (ahead(h)) { w.heading = h; found = true; break; }
                }
            if (!found) {
                // a dead end: it turns back, its tail leading
                std::reverse(w.tx, w.tx + w.n);
                std::reverse(w.ty, w.ty + w.n);
                w.hx = (float)w.tx[0]; w.hy = (float)w.ty[0];
                w.heading += PI;
            }
        }
        const float nx = w.hx + std::cos(w.heading) * w.speed * mult * wdt, ny = w.hy + std::sin(w.heading) * w.speed * mult * wdt;
        if (earthAt(iround(nx), iround(ny))) { w.hx = nx; w.hy = ny; }
        const int X = iround(w.hx), Y = iround(w.hy);
        if (X != w.tx[0] || Y != w.ty[0]) {
            for (int i = std::min(w.n, 11); i > 0; i--) { w.tx[i] = w.tx[i - 1]; w.ty[i] = w.ty[i - 1]; }
            w.tx[0] = X; w.ty[0] = Y;
            w.n = std::min(w.n + 1, w.len);
        }
    }
    worms_.erase(std::remove_if(worms_.begin(), worms_.end(), [](const Worm& w) { return w.state != 0 && w.vis <= 0.f; }), worms_.end());
}

// Butterflies over the banks (more at mid, a swarm at a drop), birds crossing the sky in a chorus, fireflies at night
// and in slow motion.
void Garden16Universe::simulateCritters(float dt, float wdt) {
    const VariantDef& V = VAR(variant_);
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const bool over = overT_ >= 0.f;
    const float world = 1.f - smoothstepf(0.f, 0.6f, bridge_.amount);
    int nb = 0, nf = 0, nbird = 0;
    for (const Critter& c : critters_) {
        if (c.leaving) continue;
        nb += c.kind == C_BUTTERFLY;
        nf += c.kind == C_FIREFLY;
        nbird += c.kind == C_BIRD;
    }
    const int wantB = over || world < 0.5f || !V.butterflies ? 0 : (int)(1.f * wm + 1.5f * wp + 4.f * drive_ - 2.f * rainAmt_ + 0.2f);
    const int wantF = world < 0.5f ? 0 : (int)(V.fireflies * (6.f + 6.f * wm + 16.f * drive_) + 30.f * slow_);
    auto bankCol = [&]() { return rng_.chance(0.5f) ? rng_.irange(cMin_ + 1, -2) : rng_.irange(11, cMax_ - 1); };
    if (nb < wantB && rng_.chance(dt * 2.f)) {
        Critter c{};
        const bool left = rng_.chance(0.5f);
        c.x = left ? -4.f : (float)W_ + 4.f;
        c.y = (float)(topOf(left ? cMin_ + 1 : cMax_ - 1) - rng_.irange(10, 40));
        c.vx = (left ? 1.f : -1.f) * rng_.range(10.f, 18.f);
        c.kind = C_BUTTERFLY; c.col = (uint8_t)rng_.irange(0, 3); c.seed = rng_.uniform() * 50.f; c.life = 1e9f;
        critters_.push_back(c);
    }
    if (nf < wantF && rng_.chance(dt * 6.f)) {
        Critter c{};
        const int col = rng_.chance(0.2f) ? rng_.irange(0, 9) : bankCol();
        c.x = (float)(bx0_ + col * CELL + rng_.irange(0, 11));
        c.y = (float)(topOf(col) - rng_.irange(4, 50));
        c.kind = C_FIREFLY; c.seed = rng_.uniform() * 50.f; c.life = rng_.range(7.f, 12.f);
        critters_.push_back(c);
    }
    // a chorus (a peak that is not a drop) sends a small flock across the sky every few bars; a drop now and then
    flockT_ -= dt;
    if (!over && world > 0.5f && wp > 0.6f && flockT_ <= 0.f && nbird == 0 && !V.fireflies) {
        flockT_ = drive_ > 0.5f ? 14.f : 7.f;
        const bool left = rng_.chance(0.5f);
        const int n = rng_.irange(3, 5);
        const float y = rng_.range(18.f, floor_ * 0.35f);
        for (int i = 0; i < n; i++) {
            Critter c{};
            const int rank = (i + 1) / 2, side = (i % 2) ? 1 : -1;
            c.x = left ? -8.f - rank * 9.f : (float)W_ + 8.f + rank * 9.f;
            c.y = y + rank * 5.f * (float)side * 0.6f + rank * 3.f;
            c.vx = (left ? 1.f : -1.f) * 22.f;
            c.kind = C_BIRD; c.seed = rng_.uniform() * 50.f; c.life = 1e9f; c.t = rng_.range(0.f, 1.f);
            critters_.push_back(c);
        }
    }
    int keepB = 0;
    for (Critter& c : critters_) {
        c.t += wdt;
        if (c.kind == C_BUTTERFLY) {
            if (!c.leaving && keepB++ >= wantB) c.leaving = true;
            // flutter: a wandering course along the banks, away from the well and the HUD columns
            const float wob = std::sin(c.t * 2.3f + c.seed) * 14.f + std::sin(c.t * 5.1f + c.seed * 2.f) * 6.f;
            const int col = floorDiv(iround(c.x) - bx0_, CELL);
            const float groundY = (float)topOf(col);
            const float targetY = groundY - 18.f - 10.f * std::sin(c.t * 0.7f + c.seed);
            c.vy += ((targetY - c.y) * 0.8f + wob) * wdt;
            c.vy *= std::exp(-1.5f * wdt);
            if (!c.leaving) {
                const float wl = (float)(bx0_ - 10), wr = (float)(bx0_ + Game::W * CELL + 10);
                if (c.x > wl && c.x < wr) c.vx += (c.x < (wl + wr) * 0.5f ? -1.f : 1.f) * 60.f * wdt;
                if (c.x < 6.f) c.vx += 30.f * wdt;
                if (c.x > W_ - 6.f) c.vx -= 30.f * wdt;
                c.vx += std::sin(c.t * 0.9f + c.seed * 3.f) * 10.f * wdt;
                const float sp = std::fabs(c.vx);
                if (sp > 22.f) c.vx *= 22.f / sp;
            }
            c.x += c.vx * wdt;
            c.y += c.vy * wdt;
            if (c.y > groundY - 4.f) c.y = groundY - 4.f;
            if (c.leaving && (c.x < -10.f || c.x > W_ + 10.f)) c.life = -1.f;
        } else if (c.kind == C_BIRD) {
            c.x += c.vx * wdt;
            c.y += std::sin(c.t * 1.3f + c.seed) * 2.f * wdt;
            if (c.x < -60.f || c.x > W_ + 60.f) c.life = -1.f;
        } else {
            c.vx += std::sin(c.t * 0.7f + c.seed * 3.f) * 4.f * wdt;
            c.vy += std::cos(c.t * 0.6f + c.seed * 5.f) * 3.f * wdt;
            c.vx *= std::exp(-0.3f * wdt); c.vy *= std::exp(-0.3f * wdt);
            c.x += c.vx * wdt; c.y += c.vy * wdt;
            c.life -= wdt;
            if (nf > wantF + 2 && c.life > 2.f) c.life = 2.f;
        }
    }
    critters_.erase(std::remove_if(critters_.begin(), critters_.end(), [](const Critter& c) { return c.life <= 0.f; }), critters_.end());
}

// Light rain through the drops (APRIL RAIN): thin streaks, a tiny splash where each lands.
void Garden16Universe::simulateRain(float dt) {
    const int want = (int)(70.f * rainAmt_ * (float)W_ / 530.f);
    while ((int)rain_.size() < want) {
        Drop d{rng_.range(0.f, (float)W_ + 40.f), rng_.range(-40.f, rain_.empty() ? (float)H_ : 0.f), rng_.range(190.f, 240.f)};
        rain_.push_back(d);
    }
    for (size_t i = 0; i < rain_.size();) {
        Drop& d = rain_[i];
        d.y += d.v * dt;
        d.x -= d.v * 0.25f * dt;
        const int X = iround(d.x), Y = iround(d.y);
        bool hit = Y >= H_;
        if (!hit && Y >= 0 && X >= 0 && X < W_ && solidAt(X, Y)) {
            hit = true;
            Part q{};
            q.x = (float)X; q.y = (float)(Y - 1);
            q.life = 0.16f; q.kind = P_SPLASH; q.col = RAIN; q.real = true;
            parts_.push_back(q);
        }
        if (hit) {
            if ((int)rain_.size() > want) { rain_[i] = rain_.back(); rain_.pop_back(); continue; }
            d.x = rng_.range(0.f, (float)W_ + 40.f);
            d.y = rng_.range(-20.f, 0.f);
        }
        i++;
    }
}

// =====================================================================================================
// The palette of this frame
// =====================================================================================================

void Garden16Universe::buildPalette() {
    const VariantDef& V = VAR(variant_);
    C3 p[128];
    for (C3& c : p) c = C3{1.f, 0.f, 1.f};
    const C3 shade = H(V.shade);
    const float glowAmt = saturate(glow_ + swell_);
    const float haze = smoothstepf(0.4f, 1.f, dry_) * (1.f - V.stars * 0.6f);
    const C3 glow = H(V.glow);
    for (int i = 0; i < 6; i++) {
        C3 c = H(V.sky[i]);
        const float g = lum(c);
        c = mix(c, mix(C3{g, g, g}, H(V.sky[5]), 0.6f), haze * 0.3f);
        const float w = V.rains ? (0.45f + 0.55f * i / 5.f) * 0.8f : std::pow(i / 5.f, 1.2f) * 0.75f;
        c = mix(c, glow, w * glowAmt);
        // a chorus: a deeper, more saturated sky
        const float lu = saturate(lush_ - glowAmt) * 0.22f;
        const float m = lum(c);
        c = C3{saturate(c.r + (c.r - m) * lu * 2.f - lu * 0.25f), saturate(c.g + (c.g - m) * lu * 2.f - lu * 0.2f), saturate(c.b + (c.b - m) * lu * 2.f)};
        p[SKY0 + i] = c;
    }
    p[SUN] = mix(H(V.sun), glow, 0.15f * glowAmt);
    p[SUNRING] = mix(H(V.sunRing), glow, 0.35f * glowAmt);
    for (int i = 0; i < 3; i++) {
        C3 c = H(i == 0 ? V.cloudHi : i == 1 ? V.cloud : V.cloudSh);
        c = mix(c, glow, (V.rains ? 0.45f : 0.25f) * glowAmt);
        c = mix(c, mul(H(V.cloudSh), 0.85f), 0.35f * rainAmt_ * (i == 0 ? 0.6f : 1.f));
        p[CLOUD_HI + i] = c;
    }
    p[WELL] = H(V.well); p[WELL_EDGE] = H(V.wellEdge); p[WELL_RAIN] = H(V.wellRain);
    {
        C3 g[5];
        ramp5(H(V.ground), shade, g);
        p[G_OUT] = g[0]; p[G_DEEP] = mix(g[0], g[1], 0.55f); p[G_SH] = g[1]; p[G_BASE] = g[2]; p[G_HI] = g[3];
        const C3 stone = mix(C3{0.78f, 0.76f, 0.72f}, H(V.ground), 0.25f);
        const float k = V.stars > 0.5f ? 0.85f : 1.f;
        p[PEB_HI] = mul(mix(stone, C3{1, 1, 1}, 0.4f), k);
        p[PEB] = mul(stone, k);
        p[PEB_SH] = mul(mix(stone, shade, 0.4f), k * 0.8f);
    }
    {
        C3 d[4], gr[4], lu[4];
        ramp4(H(V.dry), shade, d);
        ramp4(H(V.green), shade, gr);
        ramp4(H(V.lush), shade, lu);
        for (int i = 0; i < 4; i++) p[GR_OUT + i] = mix(mix(d[i], gr[i], 1.f - saturate(dry_)), lu[i], saturate(lush_));
    }
    for (int i = 0; i < 4; i++) {
        p[FL0 + 2 * i] = H(V.flower[i]);
        p[FL0 + 2 * i + 1] = mix(mul(H(V.flower[i]), 0.82f), shade, 0.18f);
    }
    p[FL_C] = H(V.center);
    p[WORM] = H(V.worm);
    p[WORM_SH] = mix(mul(H(V.worm), 0.7f), shade, 0.2f);
    p[INK] = H(V.ink); p[PALE] = H(V.pale); p[ACCENT] = H(V.accent);
    p[HALO] = mix(H(V.halo), p[G_HI], 0.3f); p[HALO2] = mix(H(V.halo), p[G_HI], 0.6f);
    p[RAIN] = H(V.rain); p[GHOST] = H(V.ghost);
    p[FIREFLY] = H(V.firefly); p[FIREFLY_DIM] = mix(H(V.firefly), p[SKY0 + 3], 0.55f);
    p[STAR] = mix(H(V.pale), p[SKY0], 0.25f);
    p[SEED] = mix(C3{0.97f, 0.96f, 0.92f}, p[SKY0 + 4], 0.15f);
    p[BIRD] = H(V.bird);
    p[DUST] = mix(p[G_HI], C3{1, 1, 1}, 0.3f);
    p[ROOT] = mix(H(V.dry), C3{1.f, 0.95f, 0.85f}, 0.25f);
    p[EARTH_INK] = C3{0.96f, 0.91f, 0.78f}; // straw letters set in the earth, in every light
    for (int t = 0; t < 7; t++) ramp5(mix(H(V.ground), H(V.soil[t]), 0.4f), shade, &p[PIECE0 + t * 5]);
    // slow motion: dusk falls over the garden (the stack keeps more of its color, the HUD all of it)
    if (slow_ > 0.001f) {
        const C3 dusk = H(V.dusk);
        for (int i = 0; i < PAL_N; i++) {
            if (i >= INK && i <= HALO2) continue;
            if (i == FIREFLY || i == FIREFLY_DIM) continue;
            const float k = slow_ * (i >= PIECE0 ? 0.35f : 0.75f);
            const C3 n = {p[i].r * 0.42f + dusk.r * 0.35f, p[i].g * 0.42f + dusk.g * 0.35f, p[i].b * 0.5f + dusk.b * 0.45f};
            p[i] = mix(p[i], n, k);
        }
    }
    for (int i = 0; i < 128; i++) {
        pal_[i][0] = saturate(p[i].r);
        pal_[i][1] = saturate(p[i].g);
        pal_[i][2] = saturate(p[i].b);
    }
}

// =====================================================================================================
// Drawing: the world
// =====================================================================================================

void Garden16Universe::drawSky() {
    const VariantDef& V = VAR(variant_);
    const int hz = std::max(1, floor_);
    for (int Y = 0; Y < H_; Y++) {
        const float t = saturate((float)Y / hz);
        const float v = std::pow(t, 0.9f) * 5.999f;
        const int i = (int)v;
        const float f = v - i;
        for (int X = 0; X < W_; X++) {
            int b = i;
            if (f > 0.8f && b < 5 && dith((f - 0.8f) / 0.2f, X, Y)) b++;
            const size_t k = (size_t)Y * W_ + X;
            cv_.px[k] = (uint8_t)(SKY0 + b);
            cv_.cls[k] = K_SKY;
        }
    }
    // stars (night, and dusk in slow motion): a few still points, a rare one twinkling
    const float st = std::max(V.stars, slow_ * 0.8f);
    if (st > 0.05f)
        for (int Y = 0; Y < hz * 2 / 3; Y++)
            for (int X = 0; X < W_; X++) {
                const float h = hash01(X, Y, 991);
                if (h > 0.002f * st) continue;
                if ((hash2(X, Y, (uint32_t)(realT_ * 1.5f)) & 15) == 0) continue;
                cv_.set(X, Y, STAR, K_SKY);
                if (h < 0.00015f) { cv_.set(X + 1, Y, STAR, K_SKY); cv_.set(X - 1, Y, STAR, K_SKY); cv_.set(X, Y + 1, STAR, K_SKY); cv_.set(X, Y - 1, STAR, K_SKY); }
            }
    // the sun or the moon: a disc, a solid ring and a dithered halo
    {
        const int cx = lx(V.sunX), cy = ly(V.sunY), R = V.sunR;
        for (int y = -R - 5; y <= R + 5; y++)
            for (int x = -R - 5; x <= R + 5; x++) {
                const float d = std::sqrt((float)(x * x + y * y));
                const int X = cx + x, Y = cy + y;
                if (d <= R + 0.4f) cv_.set(X, Y, SUN, K_SKY);
                else if (d <= R + 2.4f) cv_.set(X, Y, SUNRING, K_SKY);
                else if (d <= R + 4.6f && dith(0.5f, X, Y)) cv_.set(X, Y, SUNRING, K_SKY);
            }
        if (V.moon) {
            // a few craters
            const int cr[4][3] = {{-3, -2, 2}, {3, 2, 1}, {-1, 4, 1}, {4, -4, 1}};
            for (auto& c : cr)
                for (int y = -c[2]; y <= c[2]; y++)
                    for (int x = -c[2]; x <= c[2]; x++)
                        if (x * x + y * y <= c[2] * c[2]) cv_.set(cx + c[0] + x, cy + c[1] + y, SUNRING, K_SKY);
        }
    }
    // the clouds: a few puffs with a flat base, a lit top row and a shaded base
    for (const Cloud& c : clouds_) {
        const int n = 3 + (int)(c.seed % 2);
        float bx[4], br[4];
        for (int k = 0; k < n; k++) {
            bx[k] = c.x + c.w * (k + 0.5f) / n + (hash01(k, 1, c.seed) - 0.5f) * 4.f;
            br[k] = c.w / (float)n * (0.65f + 0.45f * hash01(k, 2, c.seed)) * (k == 0 || k == n - 1 ? 0.8f : 1.1f);
        }
        const int base = c.y;
        auto inside = [&](int X, int Y) {
            if (Y > base) return false;
            for (int k = 0; k < n; k++) {
                const float dx = (X - bx[k]) / br[k], dy = (Y - base) / (br[k] * 0.9f);
                if (dx * dx + dy * dy <= 1.f) return true;
            }
            return false;
        };
        const int x0 = (int)std::floor(c.x) - 4, x1 = (int)std::ceil(c.x + c.w) + 4;
        for (int Y = base - c.w / 2 - 2; Y <= base; Y++)
            for (int X = x0; X <= x1; X++) {
                if (!inside(X, Y)) continue;
                uint8_t col = CLOUD;
                if (!inside(X, Y - 1)) col = CLOUD_HI;
                else if (Y >= base - 1) col = CLOUD_SH;
                cv_.set(X, Y, col, K_SKY);
            }
    }
}

void Garden16Universe::drawWellPanel() {
    const int x0 = bx0_ - 2, x1 = bx0_ + Game::W * CELL + 1, y0 = by0_ - 3, y1 = floor_ - 1;
    for (int Y = y0; Y <= y1; Y++)
        for (int X = x0; X <= x1; X++) {
            const int top = Y - y0;
            if (top == 0 && (X <= x0 + 1 || X >= x1 - 1)) continue;
            if (top == 1 && (X == x0 || X == x1)) continue;
            const bool edge = X == x0 || X == x1 || top == 0 || (top == 1 && (X == x0 + 1 || X == x1 - 1));
            cv_.set(X, Y, edge ? WELL_EDGE : WELL, K_WELL);
        }
}

void Garden16Universe::drawGround() {
    for (int c = cMin_; c <= cMax_; c++) {
        const int X0 = bx0_ + c * CELL, top = topOf(c), tl = topOf(c - 1), tr = topOf(c + 1);
        for (int Y = std::max(0, top); Y < H_; Y++) {
            const int d = Y - top;
            const int tileRow = floorDiv(Y - floor_, CELL), jj = (Y - floor_) - tileRow * CELL;
            const uint32_t th = hash2(c, tileRow, seedU_);
            const auto& pt = pat_[th & 7];
            const bool mir = (th >> 3) & 1;
            for (int i = 0; i < CELL; i++) {
                const int X = X0 + i;
                if (X < 0 || X >= W_) continue;
                const bool eL = Y < tl, eR = Y < tr;
                // the corners of an exposed step are cut by a pixel
                if (d == 0 && ((i == 0 && eL) || (i == CELL - 1 && eR))) continue;
                const int drip = 3 + (hash01(X, 1, seedU_) < 0.45f ? 1 : 0) + (hash01(X, 2, seedU_) < 0.15f ? 1 : 0);
                uint8_t col;
                uint8_t cls = K_DIRT;
                if ((i == 0 && eL) || (i == CELL - 1 && eR)) {
                    col = d < 3 ? GR_OUT : G_OUT;
                    if (d < 3) cls = K_GRASS;
                } else if (d == 0) { col = GR_OUT; cls = K_GRASS; }
                else if (d == 1) { col = hash01(X, 3, seedU_) < 0.25f ? GR_BASE : GR_HI; cls = K_GRASS; }
                else if (d == 2) { col = GR_BASE; cls = K_GRASS; }
                else if (d < drip) { col = GR_SH; cls = K_GRASS; }
                else {
                    // dirt: lighter near the top, darker deeper down (ordered dither between the layers)
                    int layer = 0;
                    if (d >= 18 + 4 || (d >= 18 && dith((d - 18) / 4.f, X, Y))) layer = 1;
                    if (d >= 40 + 4 || (d >= 40 && dith((d - 40) / 4.f, X, Y))) layer = 2;
                    static const uint8_t L[3][3] = {{G_BASE, G_SH, G_HI}, {G_SH, G_DEEP, G_BASE}, {G_DEEP, G_OUT, G_SH}};
                    const int v = pt[jj][mir ? CELL - 1 - i : i];
                    if (v == 3) col = PEB_HI;
                    else if (v == 4) col = PEB;
                    else if (v == 5) col = PEB_SH;
                    else col = L[layer][v == 0 ? 0 : v == 1 ? 1 : 2];
                    for (const KeepOut& k : keepOut_) // calm earth under the HUD's letters
                        if (X >= k.x0 && X <= k.x1 && Y >= k.y0 && Y <= k.y1) { col = L[layer][0]; break; }
                    if (d == drip && layer == 0 && v == 0) col = G_SH;     // the grass's shadow on the dirt
                    if (eL && i == 1) col = layer == 0 ? G_HI : G_BASE;     // a lit edge on a step facing left
                    if (eR && i == CELL - 2) col = G_SH;                     // a shaded edge facing right
                }
                cv_.set(X, Y, col, cls);
            }
        }
    }
}

int Garden16Universe::swayAt(int X, float seed) const {
    const float wm = smoothstepf(0.2f, 1.f, lv_), wp = smoothstepf(1.1f, 2.f, lv_);
    const float phase = hasBeat_ ? (float)(beat_ * PI * 0.5) : worldT_ * 1.2f;
    const float amp = 0.3f + 0.45f * wm + 0.55f * wp + 1.2f * gust_;
    const float v = amp * std::sin(phase + X * 0.045f + seed * 2.f);
    return v > 0.55f ? 1 : (v < -0.55f ? -1 : 0);
}

int Garden16Universe::plantCap(int X, int yBase, int h) const {
    for (const KeepOut& k : keepOut_)
        if (X >= k.x0 && X <= k.x1 && yBase - h <= k.y1 && yBase >= k.y0) h = std::min(h, std::max(0, yBase - k.y1 - 2));
    return h;
}

// A tuft of three blades rooted at yBase (they grow up from the row above it), swaying a pixel.
void Garden16Universe::tuft(Canvas& cv, int x, int yBase, int h, int sway, uint8_t cls, uint32_t seed) {
    if (h <= 0) return;
    if (h == 1) { cv.set(x, yBase - 1, GR_BASE, cls); return; }
    const int hs[3] = {h - 1 - (int)(seed & 1), h, h - 1 - (int)((seed >> 1) & 1)};
    for (int b = 0; b < 3; b++) {
        const int dx = b - 1, hb = hs[b];
        for (int k = 0; k < hb; k++) {
            int xx = x + dx;
            if (dx != 0 && hb >= 3 && k >= hb - 1) xx += dx;
            if ((hb >= 3 && k >= hb / 2) || (hb == 2 && k == 1)) xx += sway;
            const uint8_t col = k == hb - 1 ? GR_HI : (k == 0 && hb >= 3 ? GR_SH : GR_BASE);
            cv.set(xx, yBase - 1 - k, col, cls);
        }
    }
}

// A flower on its stem, frame by frame: 0 the stem growing (stem pixels), 1 a bud, 2 half open, 3 open.
void Garden16Universe::flower(Canvas& cv, int x, int yBase, int stem, int stage, int color, int sway, uint8_t cls) {
    if (stem <= 0) return;
    for (int k = 0; k < stem; k++) cv.set(x + (k >= stem * 3 / 5 && stem >= 3 ? sway : 0), yBase - 1 - k, GR_SH, cls);
    if (stem >= 4) cv.set(x + ((color & 1) ? 1 : -1), yBase - 2, GR_BASE, cls);
    if (stage <= 0) return;
    const int hx = x + (stem >= 3 ? sway : 0), hy = yBase - 1 - stem;
    const uint8_t P = FLP(color), S = FLS(color);
    if (stage == 1) {
        cv.set(hx, hy, S, cls);
        cv.set(hx, hy - 1, P, cls);
        return;
    }
    if (stage == 2) {
        const char* rows[3] = {".P.", "PCP", ".S."};
        for (int r = 0; r < 3; r++)
            for (int i = 0; i < 3; i++) {
                const char ch = rows[r][i];
                if (ch == '.') continue;
                cv.set(hx - 1 + i, hy - 2 + r, ch == 'P' ? P : ch == 'S' ? S : FL_C, cls);
            }
        return;
    }
    if (stage >= 4) {
        // a big aster (the far banks, at a drop)
        const char* rows[7] = {"...P...", ".P.P.P.", "..PPP..", "PPPCPPP", "..SSS..", ".S.S.S.", "...S..."};
        for (int r = 0; r < 7; r++)
            for (int i = 0; i < 7; i++) {
                const char ch = rows[r][i];
                if (ch == '.') continue;
                cv.set(hx - 3 + i, hy - 6 + r, ch == 'P' ? P : ch == 'S' ? S : FL_C, cls);
            }
        return;
    }
    const char* rows[5] = {".P.P.", "PPPPP", ".PCP.", "SSSSS", ".S.S."};
    for (int r = 0; r < 5; r++)
        for (int i = 0; i < 5; i++) {
            const char ch = rows[r][i];
            if (ch == '.') continue;
            cv.set(hx - 2 + i, hy - 4 + r, ch == 'P' ? P : ch == 'S' ? S : FL_C, cls);
        }
}

// A flower slot of the banks (column c, slot k): where it stands, its color, when it opens (bloom), its stem.
bool Garden16Universe::flowerSlot(int c, int k, int& X, int& Y, int& color, float& u, int& stem) const {
    if (c >= -1 && c <= Game::W) return false;
    const float h3 = hash01(c * 3 + k, 13, seedU_);
    if (h3 > 0.7f) return false;
    X = bx0_ + c * CELL + 2 + k * 4;
    Y = topOf(c);
    color = (int)(hash01(c * 3 + k, 14, seedU_) * 4.f) & 3;
    u = hash01(c * 3 + k, 15, seedU_) * 0.9f;
    const float far = smoothstepf(10.f, 16.f, std::fabs(c - 4.5f));
    stem = 3 + (int)(hash01(c * 3 + k, 12, seedU_) * 5.f) + (int)(far * 4.f);
    return true;
}

void Garden16Universe::drawGroundPlants() {
    for (int c = cMin_; c <= cMax_; c++) {
        const int top = topOf(c), X0 = bx0_ + c * CELL;
        const bool inWell = c >= 0 && c < Game::W;
        if (inWell && soil_[Game::H - 1][c].id >= 0) continue;
        const float far = smoothstepf(10.f, 16.f, std::fabs(c - 4.5f));
        for (int k = 0; k < 3; k++) {
            const int slot = c * 3 + k;
            const float h1 = hash01(slot, 11, seedU_), h2 = hash01(slot, 12, seedU_), h4 = hash01(slot, 16, seedU_);
            const int X = X0 + 2 + k * 4 + (int)(h4 * 2.f);
            if (X < -3 || X > W_ + 3) continue;
            const int sway = swayAt(X, h2);
            if (inWell) {
                if (h1 < density_ * 0.8f) tuft(cv_, X, top, 1 + (h2 > 0.6f ? 1 : 0), 0, K_PLANT, (uint32_t)slot);
                continue;
            }
            // dry stems with seed heads stand when the line is dry
            if (h1 < 0.12f && dry_ > 0.35f) {
                const int hs = plantCap(X, top, 5 + (int)(h2 * 4.f) + (int)(far * 2.f));
                for (int y = 0; y < hs; y++) cv_.set(X + (y >= hs - 2 ? sway : 0), top - 1 - y, GR_SH, K_PLANT);
                if (dry_ > 0.5f && hs >= 3) {
                    const int sx = X + sway, sy = top - hs;
                    cv_.set(sx, sy, SEED, K_PLANT); cv_.set(sx, sy - 1, SEED, K_PLANT);
                    cv_.set(sx - 1, sy - 1, DUST, K_PLANT); cv_.set(sx + 1, sy, DUST, K_PLANT);
                }
                continue;
            }
            if (h1 < density_) {
                const int ht = plantCap(X, top, 2 + iround(grassH_ * 2.5f) + (int)(h2 * (2.f + 3.f * lush_)) + (int)(far * 1.5f));
                tuft(cv_, X, top, ht, sway, K_PLANT, (uint32_t)slot);
            }
        }
        // the drop's flowers
        if (bloom_ > 0.01f && !inWell)
            for (int k = 0; k < 3; k++) {
                int X, Y, color, stem;
                float u;
                if (!flowerSlot(c, k, X, Y, color, u, stem)) continue;
                const float s = (bloom_ - u) / 0.05f;
                if (s < 0.f) continue;
                const bool big = hash01(c * 3 + k, 17, seedU_) < 0.25f + 0.3f * far;
                const int stage = std::min(big ? 4 : 3, (int)s);
                const int head = stage >= 4 ? 7 : 5;
                const int st = plantCap(X, Y, stage == 0 ? std::max(1, (int)(stem * s)) : stem + head) - (stage == 0 ? 0 : head);
                if (st <= 0) continue;
                flower(cv_, X, Y, st, stage, color, swayAt(X, (float)k), K_PLANT);
            }
    }
}

// The air: petals, seeds, pollen, butterflies, fireflies, the rain (far: the birds, behind the well's panel).
void Garden16Universe::drawAir(bool far) {
    if (far) {
        for (const Critter& c : critters_) {
            if (c.kind != C_BIRD) continue;
            static const char* F[3][2] = {{"#...#", ".#.#."}, {".....", "##.##"}, {".....", ".###."}};
            const int f = ((int)(c.t * 6.f + c.seed)) % 4;
            const int fi = f == 3 ? 1 : f;
            const int X = iround(c.x) - 2, Y = iround(c.y);
            for (int r = 0; r < 2; r++)
                for (int i = 0; i < 5; i++)
                    if (F[fi][r][i] == '#') cv_.set(X + i, Y + r, BIRD, K_SKY);
            cv_.set(X + 2, Y + 2, BIRD, K_SKY);
        }
        return;
    }
    // rain behind the stack (faint behind the well's pane)
    for (const Drop& d : rain_)
        for (int k = 0; k < 4; k++) {
            const int X = iround(d.x + 0.25f * k), Y = iround(d.y) - k;
            if (!cv_.in(X, Y)) continue;
            const uint8_t kd = cv_.kind(X, Y);
            if (kd == K_WELL) cv_.set(X, Y, WELL_RAIN, K_WELL);
            else if (kd == K_SKY || kd == K_AIR || kd == K_PLANT) cv_.set(X, Y, RAIN, K_AIR);
        }
    for (const Part& p : parts_) {
        if (p.age < 0.f) continue;
        const int X = iround(p.x), Y = iround(p.y);
        const float t = p.age / p.life;
        switch (p.kind) {
        case P_PETAL: {
            if (t > 0.92f && ((int)(p.age * 20.f) & 1)) break;
            const bool wide = ((int)(p.age * 5.f + p.seed) & 1) != 0;
            cv_.set(X, Y, p.col, K_AIR);
            if (wide) cv_.set(X + 1, Y, p.col2, K_AIR);
            break;
        }
        case P_SEED:
            if (t > 0.9f && ((int)(p.age * 16.f) & 1)) break;
            cv_.set(X, Y, SEED, K_AIR);
            cv_.set(X - 1, Y - 1, SEED, K_AIR); cv_.set(X + 1, Y - 1, SEED, K_AIR); cv_.set(X, Y - 2, SEED, K_AIR);
            cv_.set(X, Y + 1, DUST, K_AIR);
            break;
        case P_LEAF: {
            if (t > 0.9f && ((int)(p.age * 16.f) & 1)) break;
            const int f = (int)(p.age * 6.f + p.seed) & 3;
            // a little leaf tumbling: flat, edge on, flat, turned
            cv_.set(X, Y, p.col, K_AIR);
            if (f == 0) { cv_.set(X + 1, Y, p.col, K_AIR); cv_.set(X + 1, Y + 1, p.col2, K_AIR); cv_.set(X - 1, Y - 1, p.col2, K_AIR); }
            else if (f == 1) cv_.set(X, Y + 1, p.col2, K_AIR);
            else if (f == 2) { cv_.set(X - 1, Y, p.col, K_AIR); cv_.set(X - 1, Y + 1, p.col2, K_AIR); cv_.set(X + 1, Y - 1, p.col2, K_AIR); }
            else cv_.set(X + 1, Y, p.col2, K_AIR);
            break;
        }
        case P_POLLEN:
            if (t > 0.7f && ((int)(p.age * 18.f) & 1)) break;
            cv_.set(X, Y, p.col, K_AIR);
            break;
        default: break;
        }
    }
    for (const Critter& c : critters_) {
        const int X = iround(c.x), Y = iround(c.y);
        if (c.kind == C_BUTTERFLY) {
            const bool open = ((int)(c.t * 9.f + c.seed) & 1) != 0;
            const uint8_t P = FLP(c.col), S = FLS(c.col);
            if (open) {
                const char* rows[3] = {"PP.PP", "PSBSP", ".S.S."};
                for (int r = 0; r < 3; r++)
                    for (int i = 0; i < 5; i++) {
                        const char ch = rows[r][i];
                        if (ch != '.') cv_.set(X - 2 + i, Y - 1 + r, ch == 'P' ? P : ch == 'S' ? S : GR_OUT, K_AIR);
                    }
            } else {
                cv_.set(X - 1, Y - 2, P, K_AIR); cv_.set(X + 1, Y - 2, P, K_AIR);
                cv_.set(X - 1, Y - 1, S, K_AIR); cv_.set(X + 1, Y - 1, S, K_AIR);
                cv_.set(X, Y - 1, GR_OUT, K_AIR); cv_.set(X, Y, GR_OUT, K_AIR);
            }
        } else if (c.kind == C_FIREFLY) {
            const float blink = 0.5f + 0.5f * std::sin(c.t * 1.6f + c.seed * 3.f);
            const float fadeIn = std::min(1.f, c.t / 1.f) * std::min(1.f, c.life / 1.f);
            const float b = blink * fadeIn;
            if (b < 0.25f) continue;
            cv_.set(X, Y, b > 0.5f ? FIREFLY : FIREFLY_DIM, K_AIR);
            if (b > 0.75f) {
                cv_.set(X + 1, Y, FIREFLY_DIM, K_AIR); cv_.set(X - 1, Y, FIREFLY_DIM, K_AIR);
                cv_.set(X, Y + 1, FIREFLY_DIM, K_AIR); cv_.set(X, Y - 1, FIREFLY_DIM, K_AIR);
            }
        }
    }
}

// =====================================================================================================
// Drawing: tiles
// =====================================================================================================

// One pixel of a 12 x 12 tile of earth: the very tile the ground is made of (the same lumps, specks and pebbles
// from the same patterns, the same lit left edge and shaded right edge on a step), in the ground's ramp tinted a
// little toward the piece's own soil; a clean dark outline around the piece, no seam inside it. 255: transparent.
uint8_t Garden16Universe::tilePx(int t, int m, uint32_t seed, int i, int j) const {
    const bool oL = !(m & M_L), oR = !(m & M_R), oD = !(m & M_D), oU = !(m & M_U);
    const int E = CELL - 1;
    if ((oL && i == 0) || (oR && i == E) || (oU && j == 0) || (oD && j == E)) return PC(t, 0);
    // concave corners (two sides joined, the diagonal not): a pixel of outline turns the corner
    if ((i == 0 && j == 0 && !(m & M_LU)) || (i == E && j == 0 && !(m & M_RU)) || (i == 0 && j == E && !(m & M_LD)) ||
        (i == E && j == E && !(m & M_RD)))
        return PC(t, 0);
    const auto& pt = pat_[seed & 7];
    const bool mir = (seed >> 3) & 1;
    const int v = pt[j][mir ? E - i : i];
    uint8_t col;
    if (v == 3) col = PEB_HI;
    else if (v == 4) col = PEB;
    else if (v == 5) col = PEB_SH;
    else col = v == 0 ? PC(t, 2) : v == 1 ? PC(t, 1) : PC(t, 3);
    if (oL && i == 1) col = PC(t, 3);       // a lit edge facing left, as on the ground's steps
    if (oR && i == E - 1) col = PC(t, 1);   // a shaded edge facing right
    return col;
}

void Garden16Universe::drawTile(int X0, int Y0, int type, int mask, uint32_t seed, uint8_t cls) {
    for (int j = 0; j < CELL; j++)
        for (int i = 0; i < CELL; i++) {
            const uint8_t v = tilePx(type, mask, seed, i, j);
            if (v != 255) cv_.set(X0 + i, Y0 + j, v, cls);
        }
}

// The growth on an open tile top: a grass cap in three frames, then tufts, then (one tile in two) a flower that
// blooms frame by frame.
void Garden16Universe::drawGrowth(int X0, int Y0, float age, uint32_t seed, bool openL, bool openR) {
    const int st = age < 0.2f ? 0 : age < 0.45f ? 1 : age < 0.8f ? 2 : 3;
    if (st == 0) return;
    for (int i = 0; i < CELL; i++) {
        const int X = X0 + i;
        if ((i == 0 && openL) || (i == CELL - 1 && openR)) {
            if (st >= 2) { cv_.set(X, Y0 + 1, GR_OUT, K_TILE); cv_.set(X, Y0 + 2, GR_OUT, K_TILE); }
            continue;
        }
        if (st == 1) {
            if (hash01(i, 21, seed) < 0.55f) cv_.set(X, Y0, GR_OUT, K_TILE);
            continue;
        }
        cv_.set(X, Y0, GR_OUT, K_TILE);
        cv_.set(X, Y0 + 1, (i == 1 && openL) ? GR_BASE : GR_HI, K_TILE);
        if (st >= 3) {
            cv_.set(X, Y0 + 2, GR_BASE, K_TILE);
            if (hash01(i, 22, seed) < 0.5f) cv_.set(X, Y0 + 3, GR_SH, K_TILE);
            if (hash01(i, 23, seed) < 0.15f) cv_.set(X, Y0 + 4, GR_SH, K_TILE);
        }
    }
    if (st < 3) return;
    const int n = 2 + (int)((seed >> 5) & 1);
    for (int t = 0; t < n; t++) {
        const int x = X0 + 2 + (int)(hash01(t, 24, seed) * 8.f);
        const int ht = std::clamp((int)((age - 0.8f) * 2.5f), 1, 3) + (hash01(t, 25, seed) > 0.6f ? 1 : 0);
        tuft(cv_, x, Y0, ht, swayAt(x, (float)t), K_TILE, seed >> t);
    }
    if (hash01(0, 26, seed) < 0.45f) {
        const float start = 1.5f + 6.5f * hash01(0, 27, seed);
        const float s = (age - start) / 0.9f;
        if (s >= 0.f) {
            const int stage = std::min(3, (int)s);
            const int stemFull = 2 + (int)(hash01(0, 28, seed) * 3.f);
            const int stem = stage == 0 ? std::max(1, (int)(stemFull * s + 0.5f)) : stemFull;
            const int x = X0 + 3 + (int)(hash01(0, 29, seed) * 6.f);
            flower(cv_, x, Y0, stem, stage, (int)(hash01(0, 30, seed) * 3.f), swayAt(x, 0.5f), K_TILE);
        }
    }
}

void Garden16Universe::drawStack(const Game& g) {
    // the ghost: the landing place as a dotted outline
    if (g.hasPiece() && !g.over()) {
        const Piece& p = g.piece();
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            auto has = [&](int x, int y) {
                for (auto& c : gc)
                    if (c[0] == x && c[1] == y) return true;
                return false;
            };
            for (auto& c : gc) {
                if (c[1] < Game::HIDDEN) continue;
                const int X0 = bx0_ + c[0] * CELL, Y0 = by0_ + (c[1] - Game::HIDDEN) * CELL;
                for (int k = 0; k < CELL; k++) {
                    if (((X0 + k + Y0) & 1) != 0) continue;
                    if (!has(c[0], c[1] - 1)) cv_.set(X0 + k, Y0, GHOST, K_GHOST);
                    if (!has(c[0], c[1] + 1)) cv_.set(X0 + k, Y0 + CELL - 1, GHOST, K_GHOST);
                    if (!has(c[0] - 1, c[1])) cv_.set(X0, Y0 + k, GHOST, K_GHOST);
                    if (!has(c[0] + 1, c[1])) cv_.set(X0 + CELL - 1, Y0 + k, GHOST, K_GHOST);
                }
            }
        }
    }
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const Soil& s = soil_[y][x];
            const int X0 = bx0_ + x * CELL, Y0 = by0_ + iround((y - Game::HIDDEN - g.rowOffset(y)) * CELL);
            drawTile(X0, Y0, std::clamp((int)c.type, 0, 6), maskOf(x, y), s.seed, K_TILE);
        }
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const Soil& s = soil_[y][x];
            const bool exposed = y == 0 || g.cell(x, y - 1).type < 0;
            if (!exposed || s.age <= 0.f) continue;
            const int m = maskOf(x, y);
            const int X0 = bx0_ + x * CELL, Y0 = by0_ + iround((y - Game::HIDDEN - g.rowOffset(y)) * CELL);
            drawGrowth(X0, Y0, s.age, s.seed, !(m & M_L), !(m & M_R));
        }
}

void Garden16Universe::drawPiece(const Game& g) {
    if (!g.hasPiece() || g.over()) return;
    const Piece& p = g.piece();
    int cells[4][2];
    g.pieceCells(p, cells);
    auto in = [&](int x, int y) {
        for (auto& c : cells)
            if (c[0] == x && c[1] == y) return true;
        return false;
    };
    const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
    const uint32_t seed = hash2(nextId_, 77, seedU_);
    for (auto& c : cells) {
        if (c[1] < Game::HIDDEN - 1) continue;
        int m = 0;
        for (int i = 0; i < 8; i++)
            if (in(c[0] + NDX[i], c[1] + NDY[i])) m |= 1 << i;
        const int X0 = bx0_ + iround((c[0] + dx) * CELL), Y0 = by0_ + iround((c[1] - Game::HIDDEN + dy) * CELL);
        drawTile(X0, Y0, std::clamp(p.type, 0, 6), m, hash2(c[0] - p.x, c[1] - p.y, seed), K_PIECE);
    }
}

void Garden16Universe::drawWorms() {
    for (const Worm& w : worms_) {
        const int shown = std::min(w.n, (int)std::ceil(saturate(w.vis) * w.len));
        for (int i = shown - 1; i >= 0; i--) {
            const int a = std::max(0, i - 1), b = std::min(w.n - 1, i + 1);
            const bool vertical = std::abs(w.ty[b] - w.ty[a]) > std::abs(w.tx[b] - w.tx[a]);
            const int off = iround(std::sin(w.wig - i * 0.95f) * 0.75f * std::sin(PI * (i + 0.5f) / w.len));
            const int X = w.tx[i] + (vertical ? off : 0), Y = w.ty[i] + (vertical ? 0 : off);
            const bool tail = i == w.len - 1;
            for (int k = 0; k < (tail ? 1 : 2); k++) {
                const int x = vertical ? X + k : X, y = vertical ? Y : Y + k;
                const uint8_t kd = cv_.kind(x, y);
                if (kd != K_DIRT && kd != K_TILE) continue;
                bool hidden = false; // worms pass beneath the HUD's letters
                for (const KeepOut& ko : keepOut_) hidden |= x >= ko.x0 && x <= ko.x1 && y >= ko.y0 && y <= ko.y1;
                if (hidden) continue;
                cv_.set(x, y, k == 0 ? (i == 0 ? WORM_SH : WORM) : WORM_SH, kd);
            }
        }
    }
}

void Garden16Universe::drawFront() {
    for (const Part& p : parts_) {
        if (p.kind == P_PETAL || p.kind == P_SEED || p.kind == P_POLLEN || p.kind == P_LEAF) continue;
        const int X = iround(p.x), Y = iround(p.y);
        switch (p.kind) {
        case P_CHUNK: {
            const float a = std::max(0.f, p.age);
            // the tile holds for a frame or two, then its bits fly and shrink: 3x3, 2x2, a pixel
            const int sz = a < 0.04f ? 3 : (a < p.life * 0.45f ? 3 : (a < p.life * 0.75f ? 2 : 1));
            const int bx = a < 0.04f ? iround(p.x - p.vx * a) : X, by = a < 0.04f ? iround(p.y - p.vy * a) : Y;
            for (int j = 0; j < sz; j++)
                for (int i = 0; i < sz; i++) {
                    const int k = sz == 1 ? 4 : (sz == 2 ? (j * 3 + i + (j > 0 ? 1 : 0)) : j * 3 + i);
                    const uint8_t v = p.px[std::min(k, 8)];
                    if (v != 255) cv_.set(bx + i + (3 - sz) / 2, by + j + (3 - sz) / 2, v, K_FX);
                }
            break;
        }
        case P_PUFF: {
            // a little cloud of dust in four frames: a tuft, a puff, breaking apart, two last grains
            static const char* F[4][3] = {{"....", ".##.", "####"}, {".##..", "####.", "#####"}, {"#.#..", ".#.##", "#...#"}, {".....", "#...#", "....."}};
            const int f = std::min(3, (int)(p.age / p.life * 4.f));
            const int w = (int)std::strlen(F[f][0]);
            for (int r = 0; r < 3; r++)
                for (int i = 0; i < w; i++)
                    if (F[f][r][i] == '#') cv_.set(X - w / 2 + i, Y - 2 + r, p.col, K_FX);
            break;
        }
        case P_CRUMB: cv_.set(X, Y, p.col, K_FX); break;
        case P_DUST: cv_.set(X, Y, p.col, K_FX); break;
        case P_SPLASH:
            if (p.age < p.life * 0.5f) { cv_.set(X - 1, Y, RAIN, K_FX); cv_.set(X + 1, Y, RAIN, K_FX); }
            else { cv_.set(X - 2, Y - 1, RAIN, K_FX); cv_.set(X + 2, Y - 1, RAIN, K_FX); }
            break;
        default: break;
        }
    }
}

// =====================================================================================================
// Drawing: previews, gauge, HUD (into the HUD layer, merged with a pale halo where the earth is behind)
// =====================================================================================================

void Garden16Universe::drawMini(Canvas& cv, int X0, int Y0, int size, int t, int m, uint8_t cls) {
    const bool oL = !(m & M_L), oR = !(m & M_R), oD = !(m & M_D), oU = !(m & M_U);
    const int E = size - 1;
    for (int j = 0; j < size; j++)
        for (int i = 0; i < size; i++) {
            if ((i == 0 && j == 0 && oL && oU) || (i == E && j == 0 && oR && oU) || (i == 0 && j == E && oL && oD) ||
                (i == E && j == E && oR && oD))
                continue;
            uint8_t v;
            if ((i == 0 && oL) || (i == E && oR) || (j == 0 && oU) || (j == E && oD)) v = PC(t, 0);
            else if (i == 1 && oL) v = PC(t, 3);
            else if (i == E - 1 && oR) v = PC(t, 1);
            else {
                const float h = hash01(X0 + i, Y0 + j, 911);
                v = h < 0.18f ? PC(t, 1) : (h > 0.88f ? PC(t, 3) : PC(t, 2));
            }
            cv.set(X0 + i, Y0 + j, v, cls);
        }
}

void Garden16Universe::drawSide(const Game& g) {
    auto preview = [&](int type, int cx, int cy, int size, bool used) {
        if (type < 0) return;
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        g.pieceCells(p, cells);
        int minx = 9, maxx = -9, miny = 9, maxy = -9;
        for (auto& c : cells) {
            minx = std::min(minx, c[0]); maxx = std::max(maxx, c[0]);
            miny = std::min(miny, c[1]); maxy = std::max(maxy, c[1]);
        }
        auto in = [&](int x, int y) {
            for (auto& c : cells)
                if (c[0] == x && c[1] == y) return true;
            return false;
        };
        const int w = (maxx - minx + 1) * size, h = (maxy - miny + 1) * size;
        const int x0 = cx - w / 2, y0 = cy - h / 2;
        for (auto& c : cells) {
            int m = 0;
            for (int i = 0; i < 8; i++)
                if (in(c[0] + NDX[i], c[1] + NDY[i])) m |= 1 << i;
            const int t = std::clamp(type, 0, 6);
            drawMini(hud_, x0 + (c[0] - minx) * size, y0 + (c[1] - miny) * size, size, t, m, K_PIECE);
            if (used) // a held piece already used: its tiles a step darker
                for (int j = 0; j < size; j++)
                    for (int i = 0; i < size; i++) {
                        const int X = x0 + (c[0] - minx) * size + i, Y = y0 + (c[1] - miny) * size + j;
                        const uint8_t v = hud_.get(X, Y);
                        if (v >= PIECE0 && v < PIECE0 + 35 && (v - PIECE0) % 5 > 0) hud_.set(X, Y, (uint8_t)(v - 1), K_PIECE);
                    }
        }
    };
    preview(g.holdType(), lx(layout::HOLD_CENTER.x), ly(layout::HOLD_CENTER.y), 7, g.holdUsed());
    for (int i = 0; i < 3; i++) preview(g.next(i), lx(layout::nextCenter(i).x), ly(layout::nextCenter(i).y), i == 0 ? 7 : 6, false);

    // The bonus gauge: a ring with a meadow rising inside, a flower opening in it when it is ready.
    {
        const int gx = lx(layout::GAUGE_CENTER.x), gy = ly(layout::GAUGE_CENTER.y), R = CELL;
        const float fill = g.bonusActive() ? saturate(g.bonusLeft() / 10.f) : saturate(gaugeShown_);
        const bool ready = g.bonusReady() || g.bonusActive();
        const float level = gy + R - fill * 2.f * R;
        for (int y = -R; y <= R; y++)
            for (int x = -R; x <= R; x++) {
                const float d = std::sqrt((float)(x * x + y * y));
                if (d > R + 0.45f) continue;
                const int X = gx + x, Y = gy + y;
                if (d > R - 0.55f) { hud_.set(X, Y, GR_OUT, K_HUD); continue; }
                const int wave = ((int)(realT_ * 3.f) + x / 3) & 1;
                const int lv = iround(level) + (fill > 0.02f && fill < 0.98f ? wave : 0);
                if (Y < lv) continue;
                uint8_t c = Y == lv ? GR_HI : (Y > gy + R - 4 ? G_BASE : GR_BASE);
                if (Y == gy + R - 4) c = GR_SH;
                hud_.set(X, Y, c, K_HUD);
            }
        if (ready) {
            const int stage = std::min(3, (int)(realT_ * 4.f) % 8);
            flower(hud_, gx, gy + 4, 4, std::max(1, stage), 0, 0, K_HUD);
        }
    }
}

void Garden16Universe::layoutHud(const std::vector<HudText>& hud) {
    items_.clear();
    const float hs = std::max(1.f, height_ / 900.f);
    const int leftX0 = lx(-11.6f), leftX1 = lx(-5.6f), rightX0 = lx(5.6f), rightX1 = lx(11.6f);
    for (const HudText& h : hud) {
        if (h.alpha <= 0.02f || h.text.empty()) continue;
        HudItem it;
        it.text = fold(h.text);
        it.role = h.role;
        it.font = FONT_SMALL;
        TextStyle st;
        const float k = h.scale / hs;
        const float lxr = (h.x - ox_) / s_, lyr = (h.y - oy_) / s_;
        const bool inRight = lxr >= rightX0 && lxr <= rightX1;
        int scale = 1;
        switch (h.role) {
        case HUD_TITLE: it.font = FONT_BIG; break;
        case HUD_SECTION: it.font = FONT_BIG; break;
        case HUD_VALUE: it.font = k < 2.4f ? FONT_SMALL : FONT_BIG; break;
        case HUD_NOTICE: it.font = FONT_BIG; st.color = ACCENT; break;
        case HUD_KEY: it.font = it.text.size() > 2 ? FONT_SMALL : FONT_BIG; st.color = ACCENT; break;
        case HUD_TOAST:
            it.font = FONT_BIG;
            scale = 2;
            st.wave = 1;
            st.wavePhase = std::floor(realT_ * 10.f) * 0.7f;
            break;
        case HUD_TOAST_SUB: it.font = FONT_BIG; st.color = ACCENT; break;
        case HUD_DIALOG:
            it.font = k >= 2.5f ? FONT_BIG : FONT_SMALL;
            scale = k >= 4.f ? 2 : 1;
            break;
        default: break;
        }
        if (h.accent && h.role != HUD_INFO && h.role != HUD_SECTION) st.color = ACCENT;
        // fades: no dither on letters (they would break up); a faint line is shown or not
        if (h.alpha < 0.18f) continue;
        st.scale = scale;
        it.st = st;
        it.w = textWidth(it.font, it.text, st.spacing) * scale;
        it.h = fontHeight(it.font) * scale;
        if (h.role == HUD_TITLE) {
            const int maxW = W_ - iround(lxr) - 70;
            if (it.w > maxW) { it.font = FONT_SMALL; it.w = textWidth(it.font, it.text, 1); it.h = 5; }
            while (it.w > maxW && it.text.size() > 4) {
                it.text = it.text.substr(0, it.text.size() - 4) + "..";
                it.w = textWidth(it.font, it.text, 1);
            }
        }
        if (h.role == HUD_UNIVERSE) {
            it.text += "  " + std::string(LEVEL_WORDS[std::clamp(level_, 0, 2)]);
            it.w = textWidth(it.font, it.text, 1);
        }
        int x = iround(lxr);
        if (h.center) x = iround(lxr - it.w * 0.5f);
        else if (h.x < 0) x = iround((width_ + h.x - ox_) / s_) - it.w;
        it.x = x;
        it.y = iround(lyr);
        if (h.role == HUD_KEY) it.y = ly(layout::GAUGE_CENTER.y) - it.h / 2;
        if (h.role == HUD_NOTICE && inRight) it.y = std::max(it.y, ly(layout::GAUGE_CENTER.y) + 18);
        items_.push_back(it);
    }
    // the host spaces its lines for its own font: the info line goes under the title, the section above the
    // universe's line, the score column's labels and values stacked
    auto overlapX = [](const HudItem& a, const HudItem& b) { return a.x < b.x + b.w && b.x < a.x + a.w; };
    for (const HudItem& t : items_)
        for (HudItem& o : items_) {
            if (t.role == HUD_TITLE && o.role == HUD_INFO && overlapX(t, o) && o.y < t.y + t.h + 3 && o.y >= t.y) o.y = t.y + t.h + 3;
            if (t.role == HUD_UNIVERSE && o.role == HUD_SECTION && overlapX(t, o) && o.y + o.h > t.y - 3 && o.y <= t.y) o.y = t.y - 3 - o.h;
        }
    std::vector<HudItem*> col;
    for (HudItem& it : items_)
        if ((it.role == HUD_LABEL || it.role == HUD_VALUE) && it.x + it.w / 2 >= leftX0 && it.x + it.w / 2 <= leftX1 && it.y > ly(3.f))
            col.push_back(&it);
    std::sort(col.begin(), col.end(), [](const HudItem* a, const HudItem* b) { return a->y < b->y; });
    int bottom = -1000;
    HudRole prev = HUD_PLAIN;
    for (HudItem* it : col) {
        const int gap = prev == HUD_LABEL ? 2 : 5;
        if (it->y < bottom + gap) it->y = bottom + gap;
        bottom = it->y + it->h;
        prev = it->role;
    }
    // the plants keep below the letters in the lower part of the picture
    keepOut_.clear();
    for (const HudItem& it : items_)
        if (it.y + it.h > ly(-4.f)) keepOut_.push_back({it.x - 3, it.x + it.w + 3, it.y - 3, it.y + it.h + 2});
}

void Garden16Universe::drawText(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    (void)hud;
    for (const HudRect& r : panels) {
        const int x0 = iround((r.x - ox_) / s_), y0 = iround((r.y - oy_) / s_);
        const int x1 = iround((r.x + r.w - ox_) / s_), y1 = iround((r.y + r.h - oy_) / s_);
        const uint8_t c = r.accent ? ACCENT : WELL;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if (r.alpha >= 0.6f || dith(r.alpha / 0.6f, x, y)) cv_.set(x, y, c, K_HUD);
    }
    for (const HudItem& it : items_) {
        // the sky's ink at that height: dark on a pale sky, pale on a dark one
        const int band = std::clamp((int)(std::pow(saturate((float)(it.y + it.h / 2) / std::max(1, floor_)), 0.9f) * 5.999f), 0, 5);
        const float* sc = pal_[SKY0 + band];
        const bool darkInk = 0.2126f * sc[0] + 0.7152f * sc[1] + 0.0722f * sc[2] > 0.3f;
        TextStyle st = it.st;
        // over the earth: pale straw letters with a dark shadow, set in the earth (no halo plate)
        int earth = 0, total = 0;
        for (int y = it.y; y < it.y + it.h; y++)
            for (int x = it.x; x < it.x + it.w; x += 2) {
                total++;
                const uint8_t k = cv_.kind(x, y);
                earth += k == K_DIRT || k == K_GRASS || k == K_PLANT;
            }
        const bool overEarth = total > 0 && earth * 2 > total;
        if (overEarth) {
            if (st.color == 1) st.color = EARTH_INK;
            st.shadow = G_OUT;
            st.kind = (uint8_t)(K_HUD + 2);
        } else {
            if (st.color == 1) st.color = darkInk ? INK : PALE;
            st.kind = darkInk ? K_HUD : (uint8_t)(K_HUD + 1); // the halo: pale under dark letters, dark under pale ones
        }
        g16::drawText(hud_, it.font, it.text, it.x, it.y, st);
        if (it.role == HUD_UNIVERSE) {
            // the level as three small sprouts
            for (int i = 0; i < 3; i++) {
                const int px = it.x + it.w + 5 + i * 5, py = it.y + 4;
                const bool on = i <= level_;
                hud_.set(px, py, on ? GR_BASE : GR_OUT, st.kind);
                hud_.set(px, py - 1, on ? GR_HI : GR_OUT, st.kind);
                if (on) { hud_.set(px - 1, py - 2, GR_HI, st.kind); hud_.set(px + 1, py - 2, GR_HI, st.kind); }
            }
        }
    }
}

// The HUD layer over the scene: where the earth is behind it, a soft pale halo of two pixel rings around it.
void Garden16Universe::mergeLayer() {
    auto earthy = [](uint8_t k) { return k == K_DIRT || k == K_GRASS || k == K_PLANT; };
    for (int Y = 0; Y < H_; Y++)
        for (int X = 0; X < W_; X++) {
            const size_t i = (size_t)Y * W_ + X;
            if (hud_.px[i] != 255) continue;
            if (!earthy(cv_.cls[i])) continue;
            int best = 9, kind = 0;
            for (int dy = -1; dy <= 1 && best > 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const int x = X + dx, y = Y + dy;
                    if (x < 0 || y < 0 || x >= W_ || y >= H_) continue;
                    const size_t j = (size_t)y * W_ + x;
                    if (hud_.px[j] == 255) continue;
                    const int d = std::max(std::abs(dx), std::abs(dy));
                    if (d < best) { best = d; kind = hud_.cls[j]; }
                }
            if (best > 1 || kind == K_HUD + 2) continue;
            const bool pale = kind != K_HUD + 1;
            cv_.px[i] = pale ? HALO : G_OUT;
            cv_.cls[i] = K_HUD;
        }
    for (size_t i = 0; i < hud_.px.size(); i++)
        if (hud_.px[i] != 255) {
            cv_.px[i] = hud_.px[i];
            cv_.cls[i] = hud_.cls[i] == K_PIECE ? K_PIECE : K_HUD;
        }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void Garden16Universe::compose(const Frame& f) {
    const float bt = smoothstepf(0.f, 1.f, saturate(bridge_.amount));
    const vec3 bc = ugl::toSrgb(bridge_.color), bi = ugl::toSrgb(bridge_.ink);
    const float dim = dimT_, fade = saturate(f.fade);
    uint8_t tabW[128][3], tabS[128][3], tabH[128][3];
    for (int i = 0; i < 128; i++)
        for (int c = 0; c < 3; c++) {
            const float v = pal_[i][c];
            const float b = c == 0 ? bc.x : c == 1 ? bc.y : bc.z, k = c == 0 ? bi.x : c == 1 ? bi.y : bi.z;
            const float w = (v + (b - v) * bt) * (1.f - 0.55f * dim) * fade;
            const float s = (v + (k - v) * bt) * (1.f - 0.45f * dim) * fade;
            const float h = (v + (k - v) * bt) * fade;
            tabW[i][c] = (uint8_t)(saturate(w) * 255.f + 0.5f);
            tabS[i][c] = (uint8_t)(saturate(s) * 255.f + 0.5f);
            tabH[i][c] = (uint8_t)(saturate(h) * 255.f + 0.5f);
        }
    for (size_t i = 0; i < cv_.px.size(); i++) {
        const uint8_t idx = cv_.px[i] & 127, k = cv_.cls[i];
        const uint8_t* rgb = (k == K_TILE || k == K_PIECE) ? tabS[idx] : (k == K_HUD ? tabH[idx] : tabW[idx]);
        uint8_t* o = &rgba_[i * 4];
        o[0] = rgb[0]; o[1] = rgb[1]; o[2] = rgb[2]; o[3] = 255;
    }
}

void Garden16Universe::render(const Frame& f, GLuint fbo) {
    const Game& g = f.game;
    const MusicState& m = f.music;
    const float dt = std::min(f.dt, 0.1f);
    realT_ += dt;
    // the section, from the HUD (a drop blooms, a chorus stays green)
    sectionKnown_ = false;
    for (const HudText& h : f.hud)
        if (h.role == HUD_SECTION) {
            sectionKnown_ = true;
            sectionDrop_ = h.text == "DROP";
            sectionChorus_ = h.text == "CHORUS";
        }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 0.9f, dt);
    dimT_ = approach(dimT_, f.paused ? 0.75f : (f.dim ? 0.5f : 0.f), 3.f, dt);
    if (overT_ >= 0.f) {
        if (!g.over()) overT_ = -1.f;
        else overT_ += dt;
    }
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.4f, slow_);
    computeTops();
    if (!f.paused) {
        simulate(g, m, dt, wdt);
        syncSoil(g, wdt);
    }
    computeTops();
    buildPalette();
    layoutHud(f.hud);

    drawSky();
    drawAir(true);
    drawWellPanel();
    drawGround();
    drawGroundPlants();
    drawAir(false);
    drawStack(g);
    drawWorms();
    drawPiece(g);
    drawFront();
    hud_.fill(255, 0);
    drawSide(g);
    drawText(f.hud, f.panels);
    mergeLayer();
    compose(f);

    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W_, H_, GL_RGBA, GL_UNSIGNED_BYTE, rgba_.data());
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(prog_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    ugl::set1i(prog_, "uTex", 0);
    ugl::set4f(prog_, "uMap", ox_, oy_, s_, (float)height_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_garden16() {
    static const UniverseEntry e{
        "GARDEN 16", "GARDEN as a 16-bit indie game: tiles of soil growing pixel grass and flowers",
        [] { return std::unique_ptr<Universe>(new Garden16Universe()); },
        [](const SongLook& l) {
            return 0.7f + 0.25f * l.airWeight + 0.2f * l.dynamics + 0.15f * (1.f - smoothstepf(130.f, 170.f, l.bpm));
        },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
