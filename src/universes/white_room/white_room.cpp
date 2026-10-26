// WHITE ROOM: a seamless white studio, matte blocks and soft shadows.
//
// One world: an endless white cyclorama (the floor curving up into the back wall, no corner, no horizon). The well
// is a pane of frosted glass standing on a low white plinth; the blocks are matte, soft-cornered tiles (soft
// plastic, unglazed ceramic) standing just in front of it. A lamp in front of the well throws the stack's shadow
// onto the wall behind, enlarged and soft, a shadow play: the tetrominoes themselves, in shade, beside the well (the
// pane casts nothing; it veils what lies behind it, so the real stack always reads on a calm ground). Nothing else
// but a few small matte balls on the floor and the lettering set on the wall like a gallery's. Each room is one
// color story (a family of related hues in spaced values, one accent, two gels that belong to it).
//
// The song moves the lamp: calm is a soft light high in front, the shadows hidden behind the pane; mid brings the
// lamp lower and to the side and the stack's shadow starts to climb the wall beside the well; a chorus is a warm lamp
// low on the side, the shadow large and crisp, a few white balls hopping on the kicks. A drop changes the room for
// its whole length: the key turns into a colored gel low on one side and a second gel comes in low from the other,
// so the stack throws two enlarged colored copies of itself up the wall, one on each side, each lit by the other
// gel; a rain of white balls falls on the floor on the drop's downbeat, bounces and settles, then hops with the
// kicks while the two gels slowly circle, the shadow copies sweeping. A new section swings the lamp across to the
// other side. Beats only press the balls (never the stack, never the light). Slow motion is evening: the room goes blue-grey and a single warm lamp hangs over the well. A lock
// presses the piece into the panel; a cleared block sinks into the white (it shrinks and turns to plaster in a
// third of a second); a Tetris rolls balls of the cleared colors out onto the floor; game over turns the stack to
// white plaster row by row as the light sets.
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

GLuint compile(const char* vs, const char* fs, const char* name) {
    std::string f(fs);
    f.insert(f.find('\n') + 1, shaders::COMMON_GLSL);
    return ugl::program(vs, f.c_str(), name);
}

constexpr float FLOOR = -10.6f, WALL_Z = -9.f, COVE = 5.f;

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
    // a dim evening room: light warm blocks (ivory, sand, apricot, rose clay), sage and sea grey, one saffron;
    // gels of amber and teal
    {"GREY ROOM", 0x8e8f8f, 0x9a9b9a, 0xf2ede2, 0xf2ede2, 0xe0a33a, 0xffe2c4, 0xffd4a8, 0xc4d0e8, 0xffa040, 0x46c8c0, 0xd8d6d0,
     {0xefe6d2, 0xe0a33a, 0xb87a70, 0xb5c4a0, 0xd98a5e, 0x7fa8a4, 0xcdb48c}, 0.9f},
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
    progRoom_ = compile(ugl::FULLSCREEN_VS, shaders::ROOM_FS, "white room");
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
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

WhiteRoomUniverse::~WhiteRoomUniverse() {
    sceneT_.destroy();
    for (GLuint p : {progRoom_, progOcc_, progTile_, progShape_, progText_, progPost_}) glDeleteProgram(p);
    for (GLuint v : {tileVao_, occVao_, shapeVao_, textVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, tileVbo_, occVbo_, shapeVbo_, textVbo_}) glDeleteBuffers(1, &b);
    if (occFbo_) glDeleteFramebuffers(1, &occFbo_);
    if (occTex_) glDeleteTextures(1, &occTex_);
    for (Font* f : {&fontLight_, &fontMedium_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// =====================================================================================================
// Song, level, bonus
// =====================================================================================================

// The room from the song: bright major songs get the clean whites (GALLERY, NORDIC), warm ones the linen and the
// ceramics, dark minor ones the grey room or the cold sea; the key's hue picks among them.
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

// The drop's downbeat: the gels flare in (one swell, settling over seconds) and the rain of balls starts.
void WhiteRoomUniverse::drop(float strength) {
    swell_ = std::max(swell_, saturate(strength));
    sinceDrop_ = 0.f;
    rainT_ = 0.f;
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

void WhiteRoomUniverse::levelUp() {
    for (int i = 0; i < 3; i++) spawnBall(true, 8, 0.f, 0.f);
}

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
// Balls
// =====================================================================================================

int WhiteRoomUniverse::wantedBalls() const {
    const float wm = smoothstepf(0.4f, 1.f, lv_), wp = smoothstepf(1.3f, 2.f, lv_);
    if (overT_ >= 0.f) return (int)balls_.size();
    int n = (int)std::lround(3.f * wm + 3.f * wp + 10.f * wp * drive_);
    if (level_ < 1 && lv_ < 0.6f) n = 0;
    return n;
}

void WhiteRoomUniverse::spawnBall(bool fromAbove, int color, float hFrom, float vyFrom) {
    if (balls_.size() >= 32) return;
    Ball b{};
    b.r = rng_.range(0.26f, 0.5f);
    // a free place on the floor, in the front corners beside the plinth
    float bestX = 0, bestZ = 0, bestD = -1;
    for (int k = 0; k < 10; k++) {
        const float s = rng_.chance(0.5f) ? -1.f : 1.f;
        const float x = s < 0.f ? -rng_.range(6.6f, 11.f) : rng_.range(6.6f, 19.f);
        const float z = rng_.range(-4.2f, -1.7f);
        float d = 99.f;
        for (const Ball& o : balls_) d = std::min(d, std::hypot(o.x - x, o.z - z) - o.r);
        if (d > bestD) { bestD = d; bestX = x; bestZ = z; }
    }
    b.x = bestX;
    b.z = bestZ;
    b.h = fromAbove ? hFrom : 0.f;
    b.vy = vyFrom;
    b.vx = 0.f;
    b.color = color;
    balls_.push_back(b);
}

vec3 WhiteRoomUniverse::ballColor(const Ball& b) const {
    if (b.color >= 0 && b.color < 7) return pal_.piece[b.color];
    if (b.color == 8) return pal_.accent;
    return pal_.plaster * 0.95f;
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
        // the blocks sink into the white: they shrink and turn to plaster, from the middle of the row outward
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            dying_.push_back({p.x, p.y, 0.f, std::fabs(c.x - 4.5f) * 0.012f, std::clamp(c.type, 0, 6), false});
        }
        if (ev.count >= 4) {
            // a Tetris rolls balls of the cleared colors out from behind the plinth onto the floor
            for (int i = 0; i < 10 && balls_.size() < 32; i++) {
                const int type = std::clamp(ev.cells[(size_t)rng_.irange(0, (int)ev.cells.size() - 1)].type, 0, 6);
                spawnBall(false, type, 0.f, 0.f);
                Ball& b = balls_.back();
                const float s = b.x < 0.f ? -1.f : 1.f;
                const float land = b.x;
                b.x = s * rng_.range(5.6f, 6.2f);
                b.h = rng_.range(0.3f, 1.2f);
                b.vy = rng_.range(5.f, 9.f);
                b.vx = (land - b.x) * rng_.range(0.6f, 0.9f);
            }
        }
        break;
    }
    case GameEvent::TopOut: {
        // the stack turns to plaster row by row from the top as the light sets
        int top = Game::H;
        for (auto& c : ev.cells) top = std::min(top, c.y);
        for (auto& c : ev.cells) {
            const vec3 p = layout::cellPos((float)c.x, (float)c.y);
            dying_.push_back({p.x, p.y, 0.f, (c.y - top) * 0.11f + rng_.range(0.f, 0.06f), std::clamp(c.type, 0, 6), true});
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

    // Kicks (every two beats from the host): the balls hop or press, never the stack, never the light.
    sinceKick_ += dt;
    const float kick = std::max(music.kick, kickIn_);
    kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
    const bool kicked = kick > 0.5f && prevKick_ <= 0.5f && sinceKick_ > 0.3f;
    prevKick_ = kick;
    if (kicked) {
        sinceKick_ = 0.f;
        const float wm = smoothstepf(0.4f, 1.f, lv_);
        for (Ball& b : balls_) {
            if (b.leaving || b.h > 0.02f || std::fabs(b.vy) > 0.1f) continue;
            if (wp > 0.5f && rng_.chance(0.35f + 0.35f * drive_)) b.vy = rng_.range(2.4f, 3.4f + 2.0f * drive_);
            else b.sqv -= (1.2f + 1.5f * wp) * wm; // a gentle press
        }
    }

    // The balls: the drop's rain, the others coming and going with the sections.
    rainT_ += dt;
    const int want = wantedBalls();
    int present = 0;
    for (const Ball& b : balls_) present += b.leaving ? 0 : 1;
    if (rainT_ < 3.f && level_ >= 2) {
        // the rain: fast at first, the balls bounce lower and lower and settle
        spawnAcc_ += dt * 14.f;
        while (spawnAcc_ >= 1.f && present < std::max(want, 6 + (int)(10.f * driveTarget_))) {
            spawnAcc_ -= 1.f;
            spawnBall(true, 7, rng_.range(3.f, 8.f), -rng_.range(2.f, 8.f));
            present++;
        }
        if (spawnAcc_ >= 1.f) spawnAcc_ = 0.f;
    } else if (present < want) {
        spawnAcc_ += dt * 2.f;
        if (spawnAcc_ >= 1.f) {
            spawnAcc_ = 0.f;
            spawnBall(true, 7, rng_.range(4.f, 7.f), 0.f);
        }
    } else if (present > want) {
        // the oldest ones roll out of the room
        for (Ball& b : balls_)
            if (!b.leaving && b.h < 0.05f) {
                b.leaving = true;
                b.vx = (b.x < 0.f ? -1.f : 1.f) * rng_.range(2.5f, 4.5f);
                break;
            }
    }
    const float g = 30.f;
    for (Ball& b : balls_) {
        b.age += wdt;
        b.vy -= g * wdt;
        b.h += b.vy * wdt;
        b.x += b.vx * wdt;
        if (!b.leaving) b.vx *= std::exp(-1.5f * wdt);
        if (b.h <= 0.f) {
            b.h = 0.f;
            if (b.vy < -1.2f) {
                b.sqv -= std::min(-b.vy * 0.9f, 6.f); // the impact squashes it a little
                b.vy = -b.vy * 0.5f;
            } else b.vy = 0.f;
        }
        // squash spring
        const float acc = -320.f * (b.sq - 1.f) - 16.f * b.sqv;
        b.sqv += acc * wdt;
        b.sq += b.sqv * wdt;
        b.sq = std::clamp(b.sq, 0.75f, 1.12f);
    }
    balls_.erase(std::remove_if(balls_.begin(), balls_.end(), [](const Ball& b) { return std::fabs(b.x) > 26.f; }), balls_.end());

    press_ = approach(press_, 0.f, 9.f, dt);
    // a line clear runs at the real speed, even in slow motion
    for (auto& d : dying_) d.t += dt;
    dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return !d.over && d.t > d.delay + 0.5f; }),
                 dying_.end());
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
    const vec3 ga = GEL_A + vec3(2.5f * orbit, 1.2f * orbit2, 0.f), gb = GEL_B + vec3(2.5f * orbit, -1.2f * orbit2, 0.f);
    p0 = lerp(p0, ga, d);
    vec3 p1 = gb;
    radius = lerpf(radius, 1.0f, d);
    shadow = lerpf(shadow, 1.0f, d);
    const float flare = 1.f + 0.18f * swell_ * d;
    vec3 c0 = lerp(keyCol, pal_.gelA * 1.3f * flare, d);
    vec3 c1 = pal_.gelB * 1.3f * flare * d;
    vec3 ambC = pal_.amb * lerpf(amb, 0.14f, d);
    ambC = lerp(ambC, (pal_.gelA + pal_.gelB) * 0.12f, d * 0.3f);
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
    pool_ = vec4(p0.x * 0.4f, 2.f + p0.y * 0.2f, 15.f, pool * (1.f - d) * (1.f - s));
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

void WhiteRoomUniverse::buildBoard(const Game& g) {
    occ_.clear();
    auto block = [&](float x, float y, float s, const vec3& c, float alpha, float plaster) {
        tile(x, y, 0.5f * s, 0.5f * s, c, alpha, 0, s, plaster);
        occ_.push_back({{x, y, 0.46f * s, alpha}});
    };
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            // the locked piece presses into the panel for an instant
            const float age = c.flash > 0.f ? (1.f - c.flash) * Game::LOCK_FX_SECONDS : 9.f;
            const float pr = age < 0.4f ? press_ * 0.07f * std::exp(-age * 6.f) : 0.f;
            block(p.x, p.y, 1.f - pr, pal_.piece[c.type], 1.f, 0.f);
        }
    // Cleared blocks sinking into the white; a topped-out stack turning to plaster.
    for (const Dying& d : dying_) {
        const float t = d.t - d.delay;
        if (d.over) {
            block(d.x, d.y, 1.f, pal_.piece[d.type], 1.f, 0.85f * smoothstepf(0.f, 0.8f, t));
            continue;
        }
        if (t < 0.f) {
            block(d.x, d.y, 1.f, pal_.piece[d.type], 1.f, 0.f);
            continue;
        }
        const float k = saturate(t / 0.32f);
        if (k >= 1.f) continue;
        const float s = 1.f - k * k * (3.f - 2.f * k) * 0.85f;
        block(d.x, d.y, s, pal_.piece[d.type], 1.f - smoothstepf(0.6f, 1.f, k), smoothstepf(0.f, 0.7f, k));
    }
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
            block(w.x, w.y, 1.f, col, 1.f, 0.f);
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
            tile(x, y, 0.5f * sc, 0.5f * sc, pal_.piece[type], alpha, 0, sc, 0.f); // (no shadow: far from the wall)
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
            dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return d.over; }), dying_.end());
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

    // ---- The occupancy map.
    glBindFramebuffer(GL_FRAMEBUFFER, occFbo_);
    glViewport(0, 0, OCC_N, OCC_N);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    const float occX0 = -OCC_SIZE * 0.5f, occY0 = -OCC_SIZE * 0.5f, occTexel = OCC_N / OCC_SIZE;
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

    // ---- The room.
    glBindFramebuffer(GL_FRAMEBUFFER, sceneT_.fbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    {
        glUseProgram(progRoom_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, occTex_);
        set1i(progRoom_, "uOcc", 0);
        set4f(progRoom_, "uOccRect", occX0, occY0, OCC_SIZE, occTexel);
        set1f(progRoom_, "uOccLod", std::log2((float)OCC_N));
        const float th = std::tan(cam_.fovDeg * 0.5f * PI / 180.f);
        set3f(progRoom_, "uEye", cam_.eye);
        set2f(progRoom_, "uTan", th * width_ / (float)height_, th);
        setLightUniforms(progRoom_);
        set3f(progRoom_, "uAmb", amb_);
        set3f(progRoom_, "uWall", pal_.wall);
        set3f(progRoom_, "uPanel", pal_.panel);
        set1f(progRoom_, "uFloor", FLOOR);
        set1f(progRoom_, "uWallZ", WALL_Z);
        set1f(progRoom_, "uCove", COVE);
        set4f(progRoom_, "uPool", pool_.x, pool_.y, pool_.z, pool_.w);
        float bp[32 * 4], bc[32 * 4];
        const int nb = std::min(32, (int)balls_.size());
        for (int i = 0; i < nb; i++) {
            const Ball& b = balls_[i];
            const vec3 c = ballColor(b);
            bp[i * 4 + 0] = b.x; bp[i * 4 + 1] = FLOOR + b.h + b.r * b.sq; bp[i * 4 + 2] = b.z; bp[i * 4 + 3] = b.r;
            bc[i * 4 + 0] = c.x; bc[i * 4 + 1] = c.y; bc[i * 4 + 2] = c.z; bc[i * 4 + 3] = b.sq;
        }
        set1i(progRoom_, "uBalls", nb);
        if (nb > 0) {
            glUniform4fv(ugl::U(progRoom_, "uBallP"), nb, bp);
            glUniform4fv(ugl::U(progRoom_, "uBallC"), nb, bc);
        }
        set1f(progRoom_, "uBridge", smoothstepf(0.05f, 0.9f, bridge));
        set3f(progRoom_, "uBridgeCol", bridge_.color);
        set1f(progRoom_, "uShadowK", shadowK);
        set1f(progRoom_, "uShadowAmt", shadowAmt_);
        set1f(progRoom_, "uFrost", 0.72f);
        set1f(progRoom_, "uPx", px_);
        ugl::drawFullscreen();
    }

    // ---- The blocks, the ghost, the previews, the gauge.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (!tiles_.empty()) {
        glUseProgram(progTile_);
        setMat(progTile_, "uVP", vp_);
        set1f(progTile_, "uPx", px_);
        set1f(progTile_, "uTime", worldTime_);
        setLightUniforms(progTile_);
        set3f(progTile_, "uAmbB", ambB_);
        const float lit = smoothstepf(0.04f, 0.13f, wallLum_);
        const vec3 ink = lerp(pal_.inkLight, pal_.ink, lit);
        set3f(progTile_, "uInk", ink);
        set3f(progTile_, "uGroove", lerp(ink, pal_.wall, 0.55f));
        set3f(progTile_, "uAccent", pal_.accent);
        set3f(progTile_, "uPlaster", pal_.plaster);
        set1f(progTile_, "uBridge", bridge);
        set3f(progTile_, "uBridgeInk", bridge_.ink);
        glBindVertexArray(tileVao_);
        glBindBuffer(GL_ARRAY_BUFFER, tileVbo_);
        glBufferData(GL_ARRAY_BUFFER, tiles_.size() * sizeof(Tile), tiles_.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)tiles_.size());
    }
    drawHud(f.hud, f.panels);
    glDisable(GL_BLEND);

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
