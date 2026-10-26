// FLUO: fluorescent paint splashed on a dark wall.
//
// One world: a dark, matte, textured wall (painted black, raw concrete, slate, a night-blue coat), a well marked
// only by a calmer patch of it, and pieces of flashy fluorescent paint. When a line clears, its blocks vanish at
// once and each bursts onto the wall as a splash of its paint: a ragged main blob, streaks and droplets flung
// outward, a few drips running down; and a part of it is flung further, beside the well, where it lands stretched
// along its flight. The paint never goes away: every splash of the song stays on the wall, the newest over the
// oldest, so the wall becomes the song's own painting. Behind the well the paint is held back (the stack stays
// readable) and somewhat under the HUD's columns.
//
// Staging: the room's light is low at calm, fuller at mid, brightest at a chorus. A drop (a real one, not a
// chorus) turns the lights off and a blacklight on: the wall sinks into a deep violet and the stains light up one
// region after another and glow, with a soft halo around them, shimmering slowly; the drop's kicks set the wet
// paint dripping here and there. After the drop the lights come back and the glow fades out. Slow motion drains
// the paint of its color (only the pieces keep theirs). A lock flicks a few specks of paint from the impact; a
// Tetris bursts all four rows and throws two big splashes on the walls on either side; game over bursts the whole
// stack from the top down. A new song in FLUO fades the wall back to clean; a change of universe arrives on a
// clean wall.
//
// Rendering: the splashes are stamped into a persistent paint buffer (premultiplied "over" blending, one quad per
// primitive), so the cost stays constant however many there are; the glow is a quarter-resolution blur of that
// buffer, recomputed only when the paint changed. See shaders.hpp.
//
// Layout contract: a flat orthographic view at the reference camera's scale on the board plane (like RISOGRAPH).
#include "universes/fluo/fluo.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "universes/fluo/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Syne, SIL Open Font License, see OFL-Syne.txt.
const unsigned char FONT_SYNE[] = {
#include "universes/fluo/Syne.ttf.inc"
};

namespace shaders = fluo_shaders;
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

vec2 v2(float x, float y) { return vec2{x, y}; }
vec3 hex(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f}; }

// The variants: a wall (two tones, its texture: blotches of the coat, grain, pores, mottle), the color it takes
// under the blacklight, the HUD's text and accent, and the seven fluorescent paints of the pieces (I O T S Z J L).
struct VariantDef {
    const char* name;
    unsigned wallA, wallB, uv, text, accent;
    float blotch, grain, pores, mottle;
    unsigned piece[7];
};
const VariantDef VARIANTS[] = {
    // a matte black coat, a fine grain: the classic fluo set
    {"BLACKLIGHT", 0x151517, 0x0f0f13, 0x0d0720, 0xe9e7ec, 0xff2d8a, 0.55f, 0.85f, 0.15f, 0.6f,
     {0x00eaff, 0xfff200, 0xb44dff, 0x3dff2e, 0xff2d8a, 0x3a7dff, 0xff8a00}},
    // raw dark concrete: blotchy, pitted; acid paints
    {"CONCRETE", 0x2b2b2c, 0x1e1f21, 0x0c0a1e, 0xe7e5e0, 0xd4ff1f, 0.85f, 0.95f, 0.75f, 0.8f,
     {0x19ffd1, 0xffe81a, 0xff3df0, 0x9cff1f, 0xff2e4d, 0x7375ff, 0xff7a1a}},
    // green-black slate: warm paints
    {"SLATE", 0x171f1c, 0x0f1513, 0x080d1f, 0xe2e9e5, 0xff4fd8, 0.5f, 0.75f, 0.3f, 0.7f,
     {0x29d8ff, 0xffef2e, 0xff4fd8, 0x45ff7a, 0xff4a3d, 0xa070ff, 0xff9a1f}},
    // a night-blue coat: cool paints
    {"NIGHT BLUE", 0x111729, 0x0a0e1b, 0x110726, 0xe4e9f4, 0x00ffe1, 0.55f, 0.75f, 0.12f, 0.6f,
     {0x00ffe1, 0xfff700, 0xc05cff, 0x8aff2b, 0xff2e6e, 0x38b8ff, 0xff8a1a}},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

float easeOut(float t) {
    t = saturate(t);
    return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
}

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool FluoUniverse::init(int width, int height) {
    progWall_ = compile(ugl::FULLSCREEN_VS, shaders::WALL_FS, "fluo wall");
    progPaint_ = compile(shaders::PAINT_VS, shaders::PAINT_FS, "fluo paint");
    progFlat_ = compile(ugl::FULLSCREEN_VS, shaders::FLAT_FS, "fluo flat");
    progComp_ = compile(ugl::FULLSCREEN_VS, shaders::COMPOSITE_FS, "fluo composite");
    progPlay_ = compile(shaders::PLAY_VS, shaders::PLAY_FS, "fluo play");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "fluo text");
    progPanel_ = compile(shaders::PANEL_VS, shaders::PANEL_FS, "fluo panel");

    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glGenVertexArrays(1, &instVao_);
    glGenBuffers(1, &instVbo_);
    glBindVertexArray(instVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(1 + i);
        glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(Inst), (void*)(sizeof(float) * 4 * i));
        glVertexAttribDivisor(1 + i, 1);
    }
    glGenVertexArrays(1, &textVao_);
    glGenBuffers(1, &textVbo_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)(sizeof(float) * 2));
    glGenVertexArrays(1, &panelVao_);
    glGenBuffers(1, &panelVbo_);
    glBindVertexArray(panelVao_);
    glBindBuffer(GL_ARRAY_BUFFER, panelVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    buildFont(font_, FONT_SYNE);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// Glyphs baked as signed distance fields (they scale cleanly).
void FluoUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] fluo font init failed\n");
        delete info;
        return;
    }
    f.info = info;
    f.px = 64.f;
    f.scaleTT = stbtt_ScaleForPixelHeight(info, f.px);
    int x0, y0, x1, y1;
    if (stbtt_GetCodepointBox(info, 'H', &x0, &y0, &x1, &y1)) f.capH = y1 * f.scaleTT;
    const int AW = 1024, AH = 1024, PAD = 8;
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

void FluoUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    const float aspect = (float)width_ / height_;
    const layout::Camera cam = layout::camera(width_, height_);
    const float hh = cam.dist * std::tan(cam.fovDeg * 0.5f * PI / 180.f), hw = hh * aspect;
    viewH_ = v2(hw, hh);
    viewC_ = v2(cam.target.x, cam.target.y);
    vp_ = mat4::ortho(viewC_.x - hw, viewC_.x + hw, viewC_.y - hh, viewC_.y + hh, -1.f, 1.f);
    px_ = 2.f * hh / height_;
    wallT_.create(width_, height_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    wallDirty_ = true;
    // the paint is kept across a resize (scaled into the new buffers)
    auto remake = [&](ugl::Target& t, GLenum internal, GLenum format) {
        ugl::Target old = t;
        t = ugl::Target{};
        t.create(width_, height_, internal, format, GL_HALF_FLOAT);
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (old.fbo) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, old.fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, t.fbo);
            glBlitFramebuffer(0, 0, old.w, old.h, 0, 0, t.w, t.h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            old.destroy();
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };
    remake(paintT_, GL_RGBA16F, GL_RGBA);
    remake(heightT_, GL_R16F, GL_RED);
    wetT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    wetHT_.create(width_, height_, GL_R16F, GL_RED, GL_HALF_FLOAT);
}

FluoUniverse::~FluoUniverse() {
    wallT_.destroy();
    paintT_.destroy();
    heightT_.destroy();
    wetT_.destroy();
    wetHT_.destroy();
    for (GLuint p : {progWall_, progPaint_, progFlat_, progComp_, progPlay_, progText_, progPanel_}) glDeleteProgram(p);
    for (GLuint v : {instVao_, textVao_, panelVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, instVbo_, textVbo_, panelVbo_}) glDeleteBuffers(1, &b);
    if (font_.tex) glDeleteTextures(1, &font_.tex);
    delete (stbtt_fontinfo*)font_.info;
}

// =====================================================================================================
// Song, level
// =====================================================================================================

void FluoUniverse::setSong(const SongLook& t, float seconds) {
    if (t.variant >= 0) variant_ = t.variant % VARIANT_COUNT;
    else {
        // the key's hue picks the wall; night songs (minor, dark) lean to the black or night-blue coats
        float h = std::fmod(t.hue / TAU, 1.f);
        if (h < 0) h += 1.f;
        static const int BY_HUE[6] = {0, 1, 2, 3, 0, 2};
        int v = BY_HUE[std::min(5, (int)(h * 6.f))];
        if (t.hasSong && t.minor && t.brightness < 0.42f && (t.seed % 3) != 0) v = (t.seed & 1) ? 0 : 3;
        if (v == t.avoidVariant) v = (v + 1 + (int)(t.seed % 3)) % VARIANT_COUNT;
        variant_ = v;
    }
    rng_ = Rng(t.seed ^ 0xF1000ull);
    if (seconds <= 0.f) {
        palSet_ = false;
        clearPaint();
    } else {
        // a new song in the same room: the wall fades back to clean
        flies_.clear();
        splashes_.clear();
        drips_.clear();
        recent_.clear();
        wipe_ = 1.4f;
    }
}

void FluoUniverse::clearPaint() {
    flies_.clear();
    splashes_.clear();
    drips_.clear();
    recent_.clear();
    stamps_.clear();
    wipe_ = -1.f; // done on the next frame (GL)
}

void FluoUniverse::setLevel(int level, float, float seconds, float impact) {
    level = std::clamp(level, 0, 2);
    if (level < 2) inDrop_ = false;
    if (!hasLevel_ || seconds < 0.1f) {
        hasLevel_ = true;
        level_ = level;
        dropWait_ = 0.f;
        lv_ = lvFrom_ = lvTo_ = (float)level;
        lvT_ = 1.f;
        return;
    }
    const bool rising = level > level_;
    if (rising && level >= 2) dropWait_ = 1.f; // the peak is announced just before its downbeat: wait for drop()
    level_ = level;
    lvFrom_ = lv_;
    lvTo_ = (float)level;
    lvT_ = 0.f;
    lvDur_ = std::clamp(seconds, rising ? 1.0f + 0.8f * (1.f - impact) : 2.5f, 6.f);
}

// The drop: the lights go off and the blacklight on (fast, a third of a second), the stains light up.
void FluoUniverse::drop(float strength) {
    if (level_ >= 2) inDrop_ = true;
    dropWait_ = 0.f;
    dropT_ = 0.f;
    dropAmp_ = 0.6f + 0.4f * saturate(strength);
}

vec2 FluoUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints FluoUniverse::hints() const {
    const VariantDef& d = VARIANTS[variant_];
    TransitionHints h;
    h.background = ugl::toLinear(hex(d.wallA));
    h.ink = ugl::toLinear(hex(d.accent));
    h.accent = ugl::toLinear(hex(d.piece[0]));
    h.light = false;
    h.styles = TS_INK | TS_RIPPLE | TS_LUMA;
    return h;
}

// =====================================================================================================
// Splashes
// =====================================================================================================

// A splash at c (board units): a ragged main blob with a few lobes, streaks and trails of droplets flung outward
// (most of them along dir), a spray of fine droplets, and sometimes drips. stretch > 1: an oblique hit, the blob
// elongated along dir. Everything appears as the burst's front passes it (a fraction of a second).
void FluoUniverse::splash(vec2 c, vec3 color, float scale, vec2 dir, float delay, float stretch) {
    Splash s;
    s.c = c;
    s.color = color;
    s.delay = delay;
    s.dur = 0.1f + 0.05f * std::min(scale, 2.f);
    Rng& r = rng_;
    const float R0 = 0.66f * scale * r.range(0.85f, 1.15f);
    const float dirA = std::atan2(dir.y, dir.x);
    auto gauss = [&]() { return (r.uniform() + r.uniform() + r.uniform() - 1.5f) * 1.15f; };
    auto angle = [&](float focus) { return r.chance(focus) ? dirA + gauss() * (stretch > 1.f ? 0.55f : 0.95f) : r.range(0.f, TAU); };
    auto add = [&](vec2 a, vec2 b, float ra, float rb, float kind, float rough, float lobes, float st, float d0, float d1,
                   bool capsule) {
        SPrim p;
        p.q = {{a.x, a.y, b.x, b.y}, {ra, rb, kind, r.uniform()}, {color.x, color.y, color.z, 1.f},
               {rough, lobes, dirA, st}};
        p.d0 = d0;
        p.d1 = d1;
        p.capsule = capsule;
        s.prims.push_back(p);
        s.reach = std::max(s.reach, std::max(d0, d1));
    };
    // the main blob and its lobes
    add(c, c, R0, R0, 1.f, 0.35f, 1.f, stretch, 0.f, 0.f, false);
    const int lobes = r.irange(3, 6);
    for (int i = 0; i < lobes; i++) {
        const float a = angle(0.5f), off = R0 * r.range(0.4f, 0.9f) * (stretch > 1.f ? 1.3f : 1.f);
        const vec2 p = v2(c.x + std::cos(a) * off, c.y + std::sin(a) * off);
        add(p, p, R0 * r.range(0.28f, 0.55f), 0.f, 1.f, 0.4f, 0.7f, 1.f, off * 0.5f, 0.f, false);
    }
    // streaks and trails of droplets
    const int rays = r.irange(4, 8);
    for (int i = 0; i < rays; i++) {
        const float a = angle(0.7f);
        const float along = std::max(0.f, std::cos(a - dirA));
        const float u = r.uniform();
        const float L = scale * (0.7f + 1.9f * u * u) * (1.f + 0.6f * along) * (stretch > 1.f ? 0.7f + 0.5f * along : 1.f);
        const vec2 d = v2(std::cos(a), std::sin(a));
        const float w0 = R0 * r.range(0.2f, 0.36f);
        const float start = R0 * 0.55f;
        if (r.chance(0.4f)) {
            const vec2 a0 = v2(c.x + d.x * start, c.y + d.y * start), b0 = v2(c.x + d.x * L, c.y + d.y * L);
            add(a0, b0, w0, w0 * r.range(0.3f, 0.5f), 0.f, 0.6f, 0.f, 1.f, start, L, true);
            const float e = L + w0 * 1.3f;
            const vec2 tip = v2(c.x + d.x * e, c.y + d.y * e);
            add(tip, tip, w0 * r.range(0.6f, 1.1f), w0, 0.f, 0.4f, 0.f, 1.f, L, 0.f, false);
        } else {
            const int k = r.irange(3, 5);
            const vec2 perp = v2(-d.y, d.x);
            for (int j = 0; j < k; j++) {
                const float f = (j + 1.f) / k;
                const float dist = R0 * 1.1f + (L - R0 * 1.1f) * std::pow(f, 0.8f);
                const float jit = r.range(-0.08f, 0.08f) * scale;
                const vec2 p = v2(c.x + d.x * dist + perp.x * jit, c.y + d.y * dist + perp.y * jit);
                const float rr = w0 * lerpf(1.15f, 0.45f, f);
                add(p, p, rr, rr, 0.f, 0.4f, 0.f, 1.f, dist, 0.f, false);
            }
        }
    }
    // the spray
    const int sats = r.irange(6, 12);
    for (int i = 0; i < sats; i++) {
        const float a = angle(0.6f);
        const float dist = R0 * 1.15f + scale * 2.4f * std::pow(r.uniform(), 1.8f);
        const vec2 p = v2(c.x + std::cos(a) * dist, c.y + std::sin(a) * dist);
        const float rr = scale * r.range(0.02f, 0.07f);
        add(p, p, rr, rr, 0.f, 0.3f, 0.f, 1.f, dist, 0.f, false);
    }
    // drips, from the blob's lower edge, once it has landed
    if (r.chance(0.25f)) {
        const int n = 1;
        for (int i = 0; i < n; i++)
            s.drips.push_back({v2(c.x + r.range(-0.6f, 0.6f) * R0, c.y - R0 * r.range(0.4f, 0.7f)), R0 * r.range(0.1f, 0.17f),
                               scale * r.range(0.6f, 2.6f)});
    }
    splashes_.push_back(std::move(s));
}

void FluoUniverse::addRecent(const Recent& rc) {
    if (recent_.size() < 64) recent_.push_back(rc);
    else recent_[recentNext_++ % recent_.size()] = rc;
}

// Where a gob flung from `from` toward `side` (+1 right, -1 left) lands: mostly on the free wall beyond the HUD's
// columns, sometimes nearer, by the well or over a column (held back there); a little higher or lower than the row.
vec2 FluoUniverse::landing(vec2 from, float side) {
    Rng& r = rng_;
    const float edge = std::min(viewC_.x * side + viewH_.x - 1.2f, 21.f);
    const float tx = r.chance(0.68f) ? r.range(11.8f, std::max(12.f, edge)) : r.range(5.8f, 11.4f);
    const float D = std::max(1.f, std::fabs(side * tx - from.x));
    vec2 to = v2(side * tx, from.y + D * r.range(-0.2f, 0.7f) + r.range(-1.f, 1.f));
    to.x = std::clamp(to.x, viewC_.x - viewH_.x + 0.8f, viewC_.x + viewH_.x - 0.8f);
    to.y = std::clamp(to.y, viewC_.y - viewH_.y + 0.8f, viewC_.y + viewH_.y - 0.8f);
    return to;
}

// A gob of paint thrown from `from` to `to` along an arc (arc: how high it rises, relative to the distance); it
// splashes where it lands, the splash stretched along its flight.
void FluoUniverse::fling(vec2 from, vec2 to, vec3 color, float scale, float delay, float arc) {
    Rng& r = rng_;
    Fly f;
    f.a = from;
    f.b = to;
    const vec2 d = v2(to.x - from.x, to.y - from.y);
    const float dist = std::sqrt(d.x * d.x + d.y * d.y);
    f.ctrl = v2(from.x + d.x * 0.5f, from.y + d.y * 0.5f + dist * arc);
    f.delay = delay;
    f.dur = 0.2f + 0.015f * dist;
    f.r0 = 0.3f * std::min(scale, 2.f) * r.range(0.9f, 1.1f);
    f.r1 = f.r0 * 0.6f;
    f.scale = scale;
    f.stretch = 1.2f + 0.5f * r.uniform();
    f.color = color;
    f.recent = std::fabs(to.x) > 5.6f;
    flies_.push_back(f);
}

// Cleared (or dying) cells bursting: each run of up to three cells of one color along a row becomes one gob of its
// paint, thrown out of the well to the side it is on (the middle ones either way) and splashing onto the wall where
// it lands. The cells themselves are gone at once (the board has dropped them); the gobs leave from the row, the
// nearest ones to the completing piece first, the rows from the top down (rowDelay: game over).
void FluoUniverse::rowBurst(const std::vector<GameEvent::CellInfo>& cells, int fromX, float scale, float delay0, float rowDelay) {
    Rng& r = rng_;
    int top = Game::H;
    for (auto& c : cells) top = std::min(top, c.y);
    std::vector<GameEvent::CellInfo> cs;
    for (auto& c : cells)
        if (c.y >= 0 && c.y < Game::H && c.x >= 0 && c.x < Game::W) cs.push_back(c);
    std::sort(cs.begin(), cs.end(), [](const auto& a, const auto& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    for (size_t i = 0; i < cs.size();) {
        size_t j = i + 1;
        while (j < cs.size() && j - i < 3 && cs[j].y == cs[i].y && cs[j].x == cs[j - 1].x + 1 && cs[j].type == cs[i].type) j++;
        const int n = (int)(j - i);
        const float mx = (cs[i].x + cs[j - 1].x) * 0.5f;
        const vec3 p = layout::cellPos(mx, (float)cs[i].y);
        const vec2 from = v2(p.x, p.y + r.range(-0.15f, 0.15f));
        const float side = p.x + r.range(-1.2f, 1.2f) >= 0.f ? 1.f : -1.f;
        const float delay = delay0 + (cs[i].y - top) * rowDelay + std::fabs(mx - fromX) * 0.01f + r.range(0.f, 0.03f);
        const vec3 col = pieceColor(cs[i].type) * r.range(0.92f, 1.04f);
        fling(from, landing(from, side), col, scale * (0.45f + 0.13f * n) * r.range(0.85f, 1.1f), delay, r.range(0.08f, 0.3f));
        i = j;
    }
}

// Splashes spread and drips run (in the wet buffers, see buildWet); the gobs in flight land. Whatever is done is
// stamped once into the persistent buffers.
void FluoUniverse::updatePaint(float dt, float wdt) {
    // the gobs fly and splashes grow at the real speed (a clear never lingers, even in slow motion)
    for (Fly& f : flies_) {
        f.t += dt;
        if (f.t < f.delay + f.dur) continue;
        vec2 dir = v2(f.b.x - f.ctrl.x, f.b.y - f.ctrl.y);
        const float l = std::max(1e-4f, std::sqrt(dir.x * dir.x + dir.y * dir.y));
        dir = v2(dir.x / l, dir.y / l);
        splash(f.b, f.color, f.scale, dir, 0.f, f.stretch);
        if (f.recent) addRecent({f.b, 0.5f * f.scale, f.color});
        f.dur = -1.f; // landed
    }
    flies_.erase(std::remove_if(flies_.begin(), flies_.end(), [](const Fly& f) { return f.dur < 0.f; }), flies_.end());
    for (Splash& s : splashes_) {
        s.t += dt;
        if (s.t < s.delay + s.dur) continue;
        for (const SPrim& p : s.prims) stampPrim(p.q); // spread out: stamped once, whole
        for (const Splash::DripSeed& d : s.drips) {
            Drip dr;
            dr.p = d.p;
            dr.w = d.w;
            dr.maxLen = d.len;
            dr.v = rng_.range(1.0f, 2.2f);
            dr.color = s.color;
            dr.seed = rng_.uniform();
            drips_.push_back(dr);
        }
        s.prims.clear();
    }
    splashes_.erase(std::remove_if(splashes_.begin(), splashes_.end(), [](const Splash& s) { return s.prims.empty(); }),
                    splashes_.end());
    // drips run down the wall, slowing as the paint thins (the world's speed: slow motion slows them)
    for (Drip& d : drips_) {
        if (wdt <= 0.f) break;
        d.v *= std::exp(-wdt / 0.9f);
        d.len = std::min(d.maxLen, d.len + d.v * wdt);
        if (d.len >= d.maxLen - 1e-3f || d.v < 0.06f) d.done = true;
    }
    for (Drip& d : drips_) {
        if (!d.done) continue;
        Inst q[2];
        dripInst(d, q);
        stampPrim(q[0]);
        stampPrim(q[1]);
    }
    drips_.erase(std::remove_if(drips_.begin(), drips_.end(), [](const Drip& d) { return d.done; }), drips_.end());
}

// A drip as paint primitives: the trail, and the bead at its end.
void FluoUniverse::dripInst(const Drip& d, Inst q[2]) const {
    const vec2 tip = v2(d.p.x + 0.04f * std::sin(d.seed * 40.f + d.len * 1.7f) * d.len * 0.2f, d.p.y - d.len);
    q[0] = {{d.p.x, d.p.y, tip.x, tip.y}, {d.w, d.w * 0.8f, 0.f, d.seed}, {d.color.x, d.color.y, d.color.z, 1.f},
            {0.25f, 0.f, 0.f, 1.f}};
    const float bead = d.w * lerpf(0.9f, 1.3f, saturate(d.len / std::max(0.1f, d.maxLen)));
    q[1] = {{tip.x, tip.y - d.w * 0.3f, tip.x, tip.y - d.w * 0.3f}, {bead, bead, 0.f, d.seed},
            {d.color.x, d.color.y, d.color.z, 1.f}, {0.2f, 0.f, 0.f, 1.f}};
}

// The wet paint as it is now: the splashes spreading from where they landed (each primitive appears as the burst's
// front passes it, the streaks growing outward), the drips running.
void FluoUniverse::buildWet() {
    wet_.clear();
    for (const Splash& s : splashes_) {
        if (s.t < s.delay) continue;
        const float k = (s.t - s.delay) / s.dur;
        const float front = s.reach * easeOut(k) + 1e-3f;
        for (const SPrim& p : s.prims) {
            if (p.d0 > front) continue;
            Inst q = p.q;
            if (p.capsule && p.d1 > p.d0) {
                const float f = saturate((front - p.d0) / (p.d1 - p.d0));
                q.a0[2] = q.a0[0] + (q.a0[2] - q.a0[0]) * f;
                q.a0[3] = q.a0[1] + (q.a0[3] - q.a0[1]) * f;
                q.a1[1] = lerpf(q.a1[0], q.a1[1], f);
            } else if (!p.capsule) {
                // a blob or a droplet swells from where the paint hits
                const float g = saturate((front - p.d0) / (0.3f * s.reach + 0.2f));
                q.a1[0] *= 0.5f + 0.5f * g;
                q.a1[1] *= 0.5f + 0.5f * g;
            }
            wet_.push_back(q);
        }
    }
    for (const Drip& d : drips_) {
        Inst q[2];
        dripInst(d, q);
        wet_.push_back(q[0]);
        wet_.push_back(q[1]);
    }
}

// Paint primitives into a pair of buffers: the color ("over") and the coat's thickness (added: paint over paint is
// thicker).
void FluoUniverse::drawPaint(const std::vector<Inst>& q, const ugl::Target& color, const ugl::Target& height) {
    if (q.empty()) return;
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glUseProgram(progPaint_);
    setMat(progPaint_, "uVP", vp_);
    set1f(progPaint_, "uPx", px_);
    glBindVertexArray(instVao_);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(Inst), q.data(), GL_STREAM_DRAW);
    color.bind();
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    set1i(progPaint_, "uOut", 0);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)q.size());
    height.bind();
    glBlendFunc(GL_ONE, GL_ONE);
    set1i(progPaint_, "uOut", 1);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)q.size());
    glDisable(GL_BLEND);
}

void FluoUniverse::flushStamps() {
    drawPaint(stamps_, paintT_, heightT_);
    stamps_.clear();
}

// =====================================================================================================
// Game events
// =====================================================================================================

// Same-piece neighbors of a board cell, as the 8-bit mask PLAY_FS expects (L R D U, LD RD LU RU; y up).
int FluoUniverse::maskAt(const Game& g, int x, int y) const {
    const Cell& c = g.cell(x, y);
    const PCell& ic = cells_[y][x];
    const bool known = ic.type == c.type && ic.id >= 0;
    const float off = g.rowOffset(y);
    auto same = [&](int nx, int ny) {
        if (nx < 0 || nx >= Game::W || ny < Game::HIDDEN || ny >= Game::H) return false;
        const Cell& o = g.cell(nx, ny);
        if (o.type != c.type || std::fabs(g.rowOffset(ny) - off) > 0.01f) return false;
        const PCell& oc = cells_[ny][nx];
        const bool oknown = oc.type == o.type && oc.id >= 0;
        if (known && oknown) return oc.id == ic.id;
        return !known && !oknown;
    };
    const int dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1}, dy[8] = {0, 0, 1, -1, 1, 1, -1, -1}; // game y grows down
    int m = 0;
    for (int i = 0; i < 8; i++)
        if (same(x + dx[i], y + dy[i])) m |= 1 << i;
    return m;
}

// Cells this universe did not see lock (a stack inherited from another universe): grouped into pieces of up to
// four connected cells of the same type.
void FluoUniverse::adoptUnknown(const Game& g) {
    for (int y = Game::H - 1; y >= Game::HIDDEN; y--)
        for (int x = 0; x < Game::W; x++) {
            const int t = g.cell(x, y).type;
            if (t < 0 || (cells_[y][x].type == t && cells_[y][x].id >= 0)) continue;
            const int id = pieceCounter_++;
            int qx[4], qy[4], n = 0, head = 0;
            qx[n] = x; qy[n] = y; n++;
            cells_[y][x] = {id, t, -9.f};
            while (head < n && n < 4) {
                const int cx = qx[head], cy = qy[head];
                head++;
                const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, -1, 0, 1};
                for (int k = 0; k < 4 && n < 4; k++) {
                    const int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || nx >= Game::W || ny < Game::HIDDEN || ny >= Game::H) continue;
                    if (g.cell(nx, ny).type != t || (cells_[ny][nx].type == t && cells_[ny][nx].id >= 0)) continue;
                    if (std::fabs(g.rowOffset(ny) - g.rowOffset(cy)) > 0.01f) continue;
                    cells_[ny][nx] = {id, t, -9.f};
                    qx[n] = nx; qy[n] = ny; n++;
                }
            }
        }
}

void FluoUniverse::onEvent(const GameEvent& ev, const Game&) {
    switch (ev.type) {
    case GameEvent::HardDrop:
        hardK_ = std::min(1.f, 0.5f + ev.count * 0.05f);
        break;
    case GameEvent::Lock: {
        const int id = pieceCounter_++;
        // the impact: a few specks of paint flicked out sideways from under the piece's lowest cells
        float lowY[Game::W];
        int lowT[Game::W];
        for (int x = 0; x < Game::W; x++) { lowY[x] = 1e9f; lowT[x] = 0; }
        for (auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            cells_[c.y][c.x] = {id, c.type, time_};
            const vec3 w = layout::cellPos((float)c.x, (float)c.y);
            if (w.y < lowY[c.x]) { lowY[c.x] = w.y; lowT[c.x] = c.type; }
        }
        float minY = 1e9f;
        for (int x = 0; x < Game::W; x++) minY = std::min(minY, lowY[x]);
        const float k = 1.f + 0.7f * hardK_;
        for (int x = 0; x < Game::W; x++) {
            if (lowY[x] > minY + 0.1f) continue; // only where the piece touched down first
            const vec3 cp = layout::cellPos((float)x, 0.f);
            const int n = rng_.irange(1, 2) + (hardK_ > 0.f ? 1 : 0);
            for (int i = 0; i < n; i++) {
                Speck s;
                const float sd = rng_.chance(0.5f) ? 1.f : -1.f;
                s.p = v2(cp.x + rng_.range(-0.4f, 0.4f), lowY[x] - 0.5f);
                s.v = v2(sd * rng_.range(1.0f, 3.0f) * k, rng_.range(0.6f, 2.2f) * k);
                s.color = pieceColor(lowT[x]);
                s.t = 0.f;
                s.life = rng_.range(0.22f, 0.38f);
                s.r = rng_.range(0.035f, 0.07f);
                specks_.push_back(s);
            }
        }
        hardK_ = 0.f;
        break;
    }
    case GameEvent::Clear: {
        bool cleared[Game::H] = {};
        for (auto& c : ev.cells)
            if (c.y >= 0 && c.y < Game::H) cleared[c.y] = true;
        const bool tetris = ev.count >= 4;
        // the blocks vanish at once (the board has dropped them) and burst into gobs of paint thrown out of the
        // well, the nearest to the completing piece first; each splashes onto the wall where it lands
        rowBurst(ev.cells, (int)std::lround(ev.x), tetris ? 1.3f : 1.f, 0.f, 0.f);
        if (tetris) { // the Tetris: two big gobs thrown far onto the walls on either side
            float my = 0.f;
            int n = 0;
            for (int y = 0; y < Game::H; y++)
                if (cleared[y]) { my += layout::cellPos(0.f, (float)y).y; n++; }
            my = n ? my / n : 0.f;
            for (int s = -1; s <= 1; s += 2) {
                const int type = ev.cells.empty() ? 0 : ev.cells[rng_.irange(0, (int)ev.cells.size() - 1)].type;
                const float x = s * std::min(viewH_.x - 4.f, rng_.range(13.f, 17.f));
                const vec2 to = v2(x, std::clamp(my + rng_.range(-1.5f, 2.5f), -8.f, 8.f));
                fling(v2(s * 2.5f, my), to, pieceColor(type), 2.6f, 0.06f, 0.22f);
            }
        }
        // collapse the bookkeeping like the board
        PCell next[Game::H][Game::W];
        int dst = Game::H - 1;
        for (int y = Game::H - 1; y >= 0; y--) {
            if (cleared[y]) continue;
            for (int x = 0; x < Game::W; x++) next[dst][x] = cells_[y][x];
            dst--;
        }
        for (int y = 0; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) cells_[y][x] = next[y][x];
        break;
    }
    case GameEvent::TopOut: {
        // game over: the whole stack bursts onto the wall, from the top down
        rowBurst(ev.cells, Game::W / 2, 0.9f, 0.15f, 0.06f);
        for (auto& row : cells_)
            for (auto& c : row) c = PCell{};
        break;
    }
    default: break;
    }
}

// =====================================================================================================
// The play: pieces, ghost, previews, gauge, specks
// =====================================================================================================

void FluoUniverse::cellInst(vec2 pos, float scale, int type, int mask, float alpha, float flash, bool ghost) {
    if (alpha <= 0.003f) return;
    const vec3 c = pieceColor(type);
    const float h = 0.5f * scale + 0.25f * scale; // room for the glow
    play_.push_back({{pos.x, pos.y, h, h}, {c.x, c.y, c.z, alpha}, {ghost ? 1.f : 0.f, (float)mask, 0.f, 1.f},
                     {scale, flash, 0.f, 0.f}});
}

void FluoUniverse::buildPlay(const Game& g, float) {
    adoptUnknown(g);
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - g.rowOffset(y));
            const float age = time_ - cells_[y][x].lockT;
            const float flash = age >= 0.f && age < 0.18f ? 0.3f * (1.f - age / 0.18f) : 0.f;
            cellInst(v2(p.x, p.y), 1.f, c.type, maskAt(g, x, y), 1.f, flash, false);
        }
    auto pieceMask = [](int cells[4][2], int i) {
        int m = 0;
        const int dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1}, dy[8] = {0, 0, 1, -1, 1, 1, -1, -1};
        for (int b = 0; b < 8; b++)
            for (int j = 0; j < 4; j++)
                if (j != i && cells[j][0] - cells[i][0] == dx[b] && cells[j][1] - cells[i][1] == dy[b]) m |= 1 << b;
        return m;
    };
    if (g.hasPiece() && !g.over()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            for (int i = 0; i < 4; i++) {
                if (gc[i][1] < Game::HIDDEN) continue;
                const vec3 w = layout::cellPos((float)gc[i][0], (float)gc[i][1]);
                cellInst(v2(w.x, w.y), 1.f, p.type, pieceMask(gc, i), 0.55f, 0.f, true);
            }
        }
        const float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        for (int i = 0; i < 4; i++) {
            if (cells[i][1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(cells[i][0] + dx, cells[i][1] + dy);
            cellInst(v2(w.x, w.y), 1.f, p.type, pieceMask(cells, i), 1.f, 0.f, false);
        }
    }
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
            const vec2 pos = v2(center.x + (cells[i][0] - (minx + maxx) * 0.5f) * sc, center.y - (cells[i][1] - (miny + maxy) * 0.5f) * sc);
            cellInst(pos, sc, type, pieceMask(cells, i), alpha, 0.f, false);
        }
    };
    preview(g.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), g.holdUsed() ? 0.35f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(g.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }
    // the well: two faint hairlines and its floor, the wall's own chalk
    {
        const vec3 c = text_;
        const float a = 0.16f;
        play_.push_back({{-5.12f, 0.f, 0.05f, 10.1f}, {c.x, c.y, c.z, a}, {4.f, 0.f, 0.f, 0.f}, {0.012f, 10.05f, 0.f, 0.f}});
        play_.push_back({{5.12f, 0.f, 0.05f, 10.1f}, {c.x, c.y, c.z, a}, {4.f, 0.f, 0.f, 0.f}, {0.012f, 10.05f, 0.f, 0.f}});
        play_.push_back({{0.f, -10.1f, 5.2f, 0.05f}, {c.x, c.y, c.z, a * 1.4f}, {4.f, 0.f, 0.f, 0.f}, {5.13f, 0.014f, 0.f, 0.f}});
    }
    // the bonus gauge: a faint ring filling in the accent, a disc of it once ready
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        const bool ready = g.bonusReady(), active = g.bonusActive();
        const float fill = active ? saturate(g.bonusLeft() / 10.f) : saturate(gaugeShown_);
        const vec3 c = accent_;
        play_.push_back({{gc.x, gc.y, R + 0.2f, R + 0.2f}, {c.x, c.y, c.z, 1.f}, {2.f, 0.f, 0.f, 0.f},
                         {R, 0.045f, ready ? 1.f : fill, ready ? 1.f : 0.f}});
    }
    // specks of paint flicked by the locks
    for (const Speck& s : specks_) {
        const float a = 1.f - smoothstepf(0.5f, 1.f, s.t / s.life);
        play_.push_back({{s.p.x, s.p.y, s.r + 0.05f, s.r + 0.05f}, {s.color.x, s.color.y, s.color.z, a}, {3.f, 0.f, 0.f, 0.f},
                         {s.r * (1.f - 0.4f * s.t / s.life), 0.f, 0.f, 0.f}});
    }
    // the paint in flight: each gob shrinking a little as it goes to the wall, stretched along its flight, a couple
    // of droplets trailing it
    for (const Fly& fl : flies_) {
        const float u0 = (fl.t - fl.delay) / fl.dur;
        if (u0 < 0.f || u0 >= 1.f) continue;
        for (int k = 0; k < 3; k++) {
            const float u = u0 - k * 0.07f;
            if (u < 0.f) break;
            const float w = 1.f - u;
            const vec2 p = v2(w * w * fl.a.x + 2.f * w * u * fl.ctrl.x + u * u * fl.b.x,
                              w * w * fl.a.y + 2.f * w * u * fl.ctrl.y + u * u * fl.b.y);
            const vec2 v = v2((2.f * w * (fl.ctrl.x - fl.a.x) + 2.f * u * (fl.b.x - fl.ctrl.x)) / fl.dur,
                              (2.f * w * (fl.ctrl.y - fl.a.y) + 2.f * u * (fl.b.y - fl.ctrl.y)) / fl.dur);
            const float sp = std::sqrt(v.x * v.x + v.y * v.y);
            const float R = lerpf(fl.r0, fl.r1, u) * (k == 0 ? 1.f : k == 1 ? 0.45f : 0.3f);
            const float tail = std::min(sp * 0.022f, 1.2f) * (k == 0 ? 1.f : 0.5f);
            const float ext = tail + R + 0.05f;
            play_.push_back({{p.x, p.y, ext, ext}, {fl.color.x, fl.color.y, fl.color.z, 1.f},
                             {5.f, 0.f, std::atan2(v.y, v.x), 0.f}, {R, tail, R * 0.3f, 0.f}});
        }
    }
}

// =====================================================================================================
// Text
// =====================================================================================================

float FluoUniverse::textWidth(const std::string& s, float size, float track) const {
    const Font& f = font_;
    if (!f.ok || s.empty()) return s.size() * size * 0.6f;
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

void FluoUniverse::textQuads(const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const {
    const Font& f = font_;
    if (!f.ok) return;
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
}

void FluoUniverse::drawText(const std::vector<float>& q, const vec3& srgb, float alpha, float bias, float soft) {
    if (q.empty() || !font_.ok || alpha <= 0.003f) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    set4f(progText_, "uInk", srgb.x, srgb.y, srgb.z, saturate(alpha));
    set1f(progText_, "uBias", bias);
    set1f(progText_, "uSoft", soft);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(float), q.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 4));
}

// The HUD in FLUO's typography: Syne, wide and a little wild, in chalk white on the wall; the section, the notices
// and the celebrations in the variant's accent paint. Each line sits on a soft shadow of the wall, so it reads over
// the paint.
void FluoUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels, float mul) {
    const float bridge = saturate(bridge_.amount);
    const vec3 ink = lerp(text_, bridge_.ink, bridge) * mul, acc = lerp(accent_, bridge_.ink, bridge) * mul;
    const vec3 shadow = ugl::toSrgb(lerp(wallB_ * 0.5f, bridge_.color, bridge));
    if (!panels.empty()) {
        glUseProgram(progPanel_);
        set2f(progPanel_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(panelVao_);
        glBindBuffer(GL_ARRAY_BUFFER, panelVbo_);
        for (const HudRect& r : panels) {
            const float q[12] = {r.x, r.y, r.x + r.w, r.y, r.x + r.w, r.y + r.h, r.x, r.y, r.x + r.w, r.y + r.h, r.x, r.y + r.h};
            glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STREAM_DRAW);
            const vec3 c = r.custom ? r.color : r.accent ? ugl::toSrgb(acc) : shadow;
            const float a = saturate(r.accent ? r.alpha * 0.35f : r.alpha * 0.9f);
            glUniform4f(ugl::U(progPanel_, "uColor"), c.x * a, c.y * a, c.z * a, a);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }
    const vec3 inkS = ugl::toSrgb(ink), accS = ugl::toSrgb(acc);
    const float s = height_ / 1080.f;
    std::vector<float> q;
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        const float alpha = saturate(h.alpha);
        auto put = [&](const std::string& text, float size, float track, float dy, const vec3& c, float a, float bias) {
            q.clear();
            const float w = textWidth(text, size, track);
            textQuads(text, ugl::hudLeft(h, w, width_), h.y + dy, size, track, q);
            drawText(q, shadow, a * 0.8f, 0.24f, 0.3f);
            drawText(q, c, a, bias, 0.f);
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            const float sz = base * 1.0f, tr = base * 0.1f, vsz = base * 0.75f, vtr = base * 0.12f;
            const float wName = textWidth(name, sz, tr), gap = base * 0.8f;
            const float wVar = var.empty() ? 0.f : textWidth(var, vsz, vtr);
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            q.clear();
            textQuads(name, x, h.y, sz, tr, q);
            drawText(q, shadow, a * 0.8f, 0.24f, 0.3f);
            drawText(q, accS, a, 0.06f, 0.f);
            if (!var.empty()) {
                q.clear();
                textQuads(var, x + wName + gap, h.y + (sz - vsz) * 0.75f, vsz, vtr, q);
                drawText(q, shadow, a * 0.75f, 0.24f, 0.3f);
                drawText(q, inkS, a * 0.8f, 0.02f, 0.f);
            }
            continue;
        }
        case HUD_TITLE: put(h.text, base * 1.05f, 0.f, 0.f, inkS, saturate(h.alpha * 1.3f), 0.05f); continue;
        case HUD_INFO: put(h.text, base * 0.78f, base * 0.05f, 0.f, inkS, alpha * 0.7f, 0.02f); continue;
        case HUD_SECTION: put(h.text, base * 1.0f, base * 0.2f, 0.f, accS, saturate(0.2f + h.alpha * 1.9f), 0.06f); continue;
        case HUD_LABEL: put(h.text, base * 0.68f, base * 0.24f, 0.f, inkS, alpha * 0.6f, 0.02f); continue;
        case HUD_VALUE: put(h.text, base * 1.05f, base * 0.02f, 0.f, inkS, alpha, 0.05f); continue;
        case HUD_NOTICE: put(h.text, base * 1.0f, base * 0.06f, 0.f, accS, alpha, 0.05f); continue;
        case HUD_TOAST: put(h.text, base * 1.3f, base * 0.12f, -base * 0.1f, accS, alpha, 0.07f); continue;
        case HUD_TOAST_SUB: put(h.text, base * 0.85f, base * 0.1f, 0.f, inkS, alpha, 0.03f); continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) put(h.text, base * 1.1f, base * 0.08f, -base * 0.1f, inkS, alpha, 0.05f);
            else put(h.text, base * 0.85f, base * 0.03f, 0.f, h.accent ? accS : inkS, alpha, 0.02f);
            continue;
        default: put(h.text, base * 0.82f, base * 0.02f, 0.f, h.accent ? accS : inkS, alpha, 0.02f); continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void FluoUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const VariantDef& d = VARIANTS[variant_];
        const float k = palSet_ ? 1.f - std::exp(-dt * 1.2f) : 1.f;
        palSet_ = true;
        wallA_ = lerp(wallA_, ugl::toLinear(hex(d.wallA)), k);
        wallB_ = lerp(wallB_, ugl::toLinear(hex(d.wallB)), k);
        uvTint_ = lerp(uvTint_, ugl::toLinear(hex(d.uv)), k);
        text_ = lerp(text_, ugl::toLinear(hex(d.text)), k);
        accent_ = lerp(accent_, ugl::toLinear(hex(d.accent)), k);
        for (int i = 0; i < 7; i++) piece_[i] = lerp(piece_[i], ugl::toLinear(hex(d.piece[i])), k);
        const float tx[4] = {d.blotch, d.grain, d.pores, d.mottle};
        for (int i = 0; i < 4; i++) tex_[i] = lerpf(tex_[i], tx[i], k);
    }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 1.4f, dt);
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.5f : 0.f), 3.f, dt);
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.35f, slow_);
    const float bridge = saturate(bridge_.amount);

    // a change of universe arrives on a clean wall
    if (bridge_.entering && bridge > 0.9f && !cleanedForEntry_) { clearPaint(); cleanedForEntry_ = true; }
    if (bridge < 0.01f) cleanedForEntry_ = false;

    if (!f.paused) {
        time_ += wdt;
        if (lvT_ < 1.f) {
            lvT_ = std::min(1.f, lvT_ + dt / lvDur_);
            lv_ = lerpf(lvFrom_, lvTo_, smoothstepf(0.f, 1.f, lvT_));
        }
        dropT_ += dt;
        dropWait_ = std::max(0.f, dropWait_ - dt);
        // a real drop (the peak at a drop's pace, not a chorus): the lights go off, the blacklight comes on; a seek
        // straight into a drop goes by the pace alone
        const bool hot = level_ >= 2 && dropWait_ <= 0.f &&
                         (music.hasSong ? music.speed > 1.33f || (inDrop_ && music.speed > 1.26f) : inDrop_);
        uv_ = hot ? std::min(1.f, uv_ + dt / 0.45f) : std::max(0.f, uv_ - dt / 2.8f);
        ignite_ = hot ? std::min(1.f, ignite_ + dt / 1.8f) : std::max(0.f, ignite_ - dt / 3.f);
        // the room's light: low at calm, fuller at mid, brightest at a chorus
        const float lt = 0.78f + 0.17f * smoothstepf(0.f, 1.f, lv_) + 0.17f * smoothstepf(1.f, 2.f, lv_);
        light_ = approach(light_, lt, 0.8f, dt);
        // the drop's kicks set the wet paint dripping, here and there beside the well
        const float kick = std::max(music.kick, kickIn_);
        kickIn_ = approach(kickIn_, 0.f, 6.f, dt);
        dripCool_ -= dt;
        if (kick > 0.5f && prevKick_ <= 0.5f && uv_ > 0.6f && dripCool_ <= 0.f && !recent_.empty()) {
            const int n = rng_.irange(1, 2);
            for (int i = 0; i < n; i++) {
                const Recent& r = recent_[rng_.irange(0, (int)recent_.size() - 1)];
                Drip d;
                d.p = v2(r.p.x + rng_.range(-0.5f, 0.5f) * r.r, r.p.y - r.r * rng_.range(0.3f, 0.6f));
                d.w = r.r * rng_.range(0.1f, 0.16f);
                d.maxLen = rng_.range(1.2f, 3.5f);
                d.v = rng_.range(1.2f, 2.2f);
                d.color = r.color;
                d.seed = rng_.uniform();
                drips_.push_back(d);
            }
            dripCool_ = 0.6f;
        }
        prevKick_ = kick;
        for (Speck& s : specks_) {
            s.t += dt;
            s.v.y -= 14.f * dt;
            s.p.x += s.v.x * dt;
            s.p.y += s.v.y * dt;
        }
        specks_.erase(std::remove_if(specks_.begin(), specks_.end(), [](const Speck& s) { return s.t >= s.life; }), specks_.end());
        gaugeShown_ = approach(gaugeShown_, game.bonusGauge(), 2.5f, dt);
        updatePaint(dt, wdt);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    // ---- The wall's grain (once per size).
    if (wallDirty_) {
        wallT_.bind();
        glUseProgram(progWall_);
        set4f(progWall_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set2f(progWall_, "uRes", (float)width_, (float)height_);
        ugl::drawFullscreen();
        wallDirty_ = false;
    }

    // ---- The paint: cleaned, faded for a new song, the new stamps, the wet paint.
    if (wipe_ < 0.f) {
        glClearColor(0.f, 0.f, 0.f, 0.f);
        for (const ugl::Target* t : {&paintT_, &heightT_}) {
            glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        wipe_ = 0.f;
    } else if (wipe_ > 0.f && !f.paused) {
        wipe_ -= dt;
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_CONSTANT_ALPHA);
        glBlendColor(0.f, 0.f, 0.f, wipe_ <= 0.f ? 0.f : std::exp(-dt * 3.2f));
        glUseProgram(progFlat_);
        set4f(progFlat_, "uColor", 0.f, 0.f, 0.f, 0.f);
        for (const ugl::Target* t : {&paintT_, &heightT_}) {
            t->bind();
            ugl::drawFullscreen();
        }
        glDisable(GL_BLEND);
        wipe_ = std::max(0.f, wipe_);
    }
    flushStamps();
    buildWet();
    if (!wet_.empty()) {
        glClearColor(0.f, 0.f, 0.f, 0.f);
        for (const ugl::Target* t : {&wetT_, &wetHT_}) {
            glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        drawPaint(wet_, wetT_, wetHT_);
    }

    // ---- The fluorescence: on with the blacklight, swelling a little on the drop and settling (no pulse).
    const float dropSwell = dropT_ < 12.f ? smoothstepf(0.f, 0.6f, dropT_) * std::exp(-std::max(0.f, dropT_ - 0.6f) / 2.2f) * dropAmp_ : 0.f;
    const float uvE = smoothstepf(0.f, 1.f, uv_) * (1.f - bridge);
    const float glow = uvE * (1.f + 0.3f * dropSwell * (inDrop_ ? 1.f : 0.f));
    // the light moving slowly over the wall (its reflections slide along the paint's relief)
    if (!f.paused) lampT_ += dt;
    const vec3 lamp(viewC_.x + viewH_.x * 0.8f * std::sin(lampT_ * 0.07f + 1.3f),
                    viewC_.y + viewH_.y * (0.3f + 0.5f * std::sin(lampT_ * 0.047f + 0.4f)), 7.f);

    // ---- The wall and its paint.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progComp_);
    const ugl::Target* texs[5] = {&wallT_, &paintT_, &heightT_, &wetT_, &wetHT_};
    const char* names[5] = {"uWall", "uPaint", "uHeight", "uWet", "uWetH"};
    for (int i = 0; i < 5; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, texs[i]->tex);
        set1i(progComp_, names[i], i);
    }
    glActiveTexture(GL_TEXTURE0);
    set1i(progComp_, "uWetOn", wet_.empty() ? 0 : 1);
    set2f(progComp_, "uTexel", 1.f / width_, 1.f / height_);
    set1f(progComp_, "uPx", px_);
    set3f(progComp_, "uLamp", lamp);
    set4f(progComp_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set3f(progComp_, "uWallA", wallA_);
    set3f(progComp_, "uWallB", wallB_);
    set3f(progComp_, "uUvTint", uvTint_);
    set4f(progComp_, "uTex", tex_[0], tex_[1], tex_[2], tex_[3]);
    set1f(progComp_, "uLight", light_ * (1.f - 0.15f * slow_));
    set1f(progComp_, "uDark", uvE);
    set1f(progComp_, "uGlow", glow);
    set1f(progComp_, "uIgnite", ignite_);
    set1f(progComp_, "uSlow", slow_ * (1.f - uvE));
    set4f(progComp_, "uWell", 0.f, 0.f, 5.f, 10.f);
    set4f(progComp_, "uColL", layout::LEFT_COLUMN[0], layout::LEFT_COLUMN[1], layout::LEFT_COLUMN[2], layout::LEFT_COLUMN[3]);
    set4f(progComp_, "uColR", layout::RIGHT_COLUMN[0], layout::RIGHT_COLUMN[1], layout::RIGHT_COLUMN[2], layout::RIGHT_COLUMN[3]);
    {
        // the HUD's lines, as board rects: the paint is held back under them
        float rects[32 * 4];
        int n = 0;
        for (const HudText& h : f.hud) {
            if (n >= 32) break;
            if (h.alpha <= 0.05f || h.text.empty()) continue;
            const float size = h.scale * 12.f * 1.1f;
            const float w = textWidth(h.text, size, h.scale * 12.f * 0.15f);
            const float x0 = ugl::hudLeft(h, w, width_) - size * 0.5f, x1 = x0 + w + size * 1.0f;
            const float y0 = h.y - size * 0.25f, y1 = h.y + size * 1.05f;
            rects[n * 4 + 0] = viewC_.x + (x0 / width_ * 2.f - 1.f) * viewH_.x;
            rects[n * 4 + 1] = viewC_.y + (1.f - y1 / height_ * 2.f) * viewH_.y;
            rects[n * 4 + 2] = viewC_.x + (x1 / width_ * 2.f - 1.f) * viewH_.x;
            rects[n * 4 + 3] = viewC_.y + (1.f - y0 / height_ * 2.f) * viewH_.y;
            n++;
        }
        if (n > 0) glUniform4fv(ugl::U(progComp_, "uText"), n, rects);
        set1i(progComp_, "uTextN", n);
    }
    set1f(progComp_, "uBridge", smoothstepf(0.f, 1.f, bridge));
    set3f(progComp_, "uBridgeColor", bridge_.color);
    set1f(progComp_, "uDim", dimT_);
    set1f(progComp_, "uFade", saturate(f.fade));
    ugl::drawFullscreen();

    // ---- The play, over the wall.
    const float mul = (1.f - 0.55f * dimT_) * saturate(f.fade);
    play_.clear();
    buildPlay(game, time_);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (!play_.empty()) {
        glUseProgram(progPlay_);
        setMat(progPlay_, "uVP", vp_);
        set1f(progPlay_, "uPx", px_);
        set1f(progPlay_, "uGlowK", (0.35f + 0.9f * uvE) * (1.f - bridge));
        set1f(progPlay_, "uMul", mul);
        set1f(progPlay_, "uBridge", smoothstepf(0.f, 1.f, bridge));
        set3f(progPlay_, "uBridgeInk", bridge_.ink);
        glBindVertexArray(instVao_);
        glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
        glBufferData(GL_ARRAY_BUFFER, play_.size() * sizeof(Inst), play_.data(), GL_STREAM_DRAW);
        for (int pass = 0; pass < 2; pass++) {
            set1i(progPlay_, "uPass", pass);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)play_.size());
        }
    }
    drawHud(f.hud, f.panels, mul);
    glDisable(GL_BLEND);
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_fluo() {
    static const UniverseEntry e{
        "FLUO", "fluorescent paint splashed on a dark wall", [] { return std::unique_ptr<Universe>(new FluoUniverse()); },
        // Energetic, punchy songs with drops suit it best (the blacklight); slow, airy ones less.
        [](const SongLook& l) {
            return 0.55f + 0.3f * smoothstepf(95.f, 130.f, l.bpm) + 0.3f * l.dynamics + 0.2f * l.bassWeight;
        },
        [] {
            std::vector<std::string> v;
            for (const VariantDef& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
