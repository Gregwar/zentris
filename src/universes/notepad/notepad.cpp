// NOTEPAD: a page of a squared notebook, drawn by hand in stop motion.
//
// The whole picture is one page of grid paper. The well is a box drawn on it, one cell per printed square, and
// the pieces are drawn in the squares: an outline that wobbles and overshoots its corners, coloured in with
// felt pens, coloured pencils or chalk, never quite inside the lines.
//
// Nothing moves smoothly: the page is redrawn a few times a second, like hand-drawn stop motion. Each drawing
// is a list of things (cells, outlines, doodles, words) compared with the previous one: what stayed is drawn
// exactly as it was (no boil: each thing keeps its own hand-drawn imperfection), what is new is drawn (a falling
// piece is a quick scribble, a landed one is coloured in over a few drawings), what moved or disappeared is rubbed
// out with an eraser:
// it leaves a faint ghost in a residue layer that the eraser keeps going over, fainter on every drawing. A
// falling piece leaves a fading trail of its scribbles; cleared rows are rubbed out, crumbs and all.
//
// The music sets the hand: a break is a slow pencil sketch, faint, almost without colour; the verse draws in
// colour; a chorus draws faster, small doodles appearing in the margins; a drop is frantic: twelve drawings a
// second, bold strokes, the well gone over in marker and doodles everywhere, hopping on the kicks.
#include "universes/notepad/notepad.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>

#include "layout.hpp"
#include "universes/notepad/shaders.hpp"
#include "universes/registry.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {

// Kalam (Indian Type Foundry), SIL Open Font License, see OFL-Kalam.txt (subset to Latin).
const unsigned char FONT_TTF[] = {
#include "universes/notepad/Kalam-Regular.ttf.inc"
};

namespace sh = notepad_shaders;
using ugl::set1f;
using ugl::set1i;
using ugl::set2f;
using ugl::set3f;
using ugl::set4f;
using ugl::setMat;

GLuint compile(const char* vs, const char* fs, const char* name) {
    std::string f(fs);
    f.insert(f.find('\n') + 1, sh::NOISE_GLSL);
    return ugl::program(vs, f.c_str(), name);
}

vec3 hexc(uint32_t h) {
    return ugl::toLinear(vec3(((h >> 16) & 255) / 255.f, ((h >> 8) & 255) / 255.f, (h & 255) / 255.f));
}
vec2 v2(float x, float y) { return vec2{x, y}; }
vec2 add(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 sub(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
vec2 mul(vec2 a, float k) { return {a.x * k, a.y * k}; }
float len(vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
vec2 rot(vec2 p, float a) { const float c = std::cos(a), s = std::sin(a); return {c * p.x - s * p.y, s * p.x + c * p.y}; }
uint64_t mix(uint64_t a, uint64_t b) { return splitmix64(a ^ (b * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull)); }
float hseed(uint64_t k) { return (float)((splitmix64(k) >> 40) % 10007) * 0.0137f; }
uint64_t strHash(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}
uint64_t tag(char t, uint64_t v) { return mix((uint64_t)(unsigned char)t << 48, v); }
// One cell's share of a landed piece's colouring: the cells one after another, overlapping a little.
float landReveal(float progress, int idx) { return saturate((progress * 5.2f - (float)idx) / 2.2f); }

enum Tool { T_PENCIL = 0, T_PEN = 1, T_MARKER = 2, T_CHALK = 3, T_SMUDGE = 4, T_CPENCIL = 5, T_HIGHLIGHT = 6 };
enum Pattern { P_HATCH = 0, P_CROSS = 1, P_SCRIBBLE = 2, P_BANDS = 3, P_STIPPLE = 4, P_RUB = 5 };

// The notebooks. Each is a paper, its printed grid and margin, the pen the outlines are drawn with, the tool and
// the seven colours (and ways of colouring in) of the pieces, the break's pencil, the drop's marker, the doodles'
// inks and the eraser's crumbs. Colors are sRGB; on the blackboard they are chalks.
struct Variant {
    const char* name;
    bool light;
    uint32_t paper, grid;
    float gridA;
    int major;
    float majorK;
    uint32_t margin;
    int marginN;
    float marginA, grain;
    uint32_t outline;
    int outlineTool;
    float outlineW;
    int fillTool;
    float fillLw;
    uint32_t piece[7];
    int pattern[7];
    float angle[7];
    float spacing[7];
    uint32_t graphite;
    int calmTool;
    uint32_t accent;
    int accentTool;
    uint32_t doodle[4];
    int doodleTool;
    uint32_t crumb;
    uint32_t blue; // the slow motion's blue pencil (an animator's rough)
};
const Variant VARIANTS[] = {
    // A school notebook: white paper, blue squares, a red margin, a black fineliner and felt pens.
    {"FELT PENS", true, 0xf8f7f2, 0x93bde6, 0.45f, 0, 1.f, 0xe0625c, 1, 0.85f, 0.05f, 0x1b2030, T_PEN, 0.052f,
     T_MARKER, 1.f,
     {0x14a4dc, 0xffd01c, 0x9a4cc9, 0x38b648, 0xec3a3a, 0x2c5ce0, 0xff891c},
     {P_BANDS, P_BANDS, P_BANDS, P_BANDS, P_BANDS, P_BANDS, P_BANDS},
     {75.f, 8.f, 40.f, -35.f, 100.f, 22.f, -62.f},
     {0.16f, 0.16f, 0.16f, 0.16f, 0.16f, 0.16f, 0.16f},
     0x5a5b62, T_PENCIL, 0xff3d8b, T_MARKER, {0xff3d8b, 0x14a4dc, 0xffc21c, 0x38b648}, T_MARKER, 0xd9a0a0, 0x3d63c9},
    // Engineering graph paper: green squares, a bolder line every five, a graphite pencil and coloured pencils.
    {"PENCIL", true, 0xf4f6f2, 0x80bf98, 0.3f, 5, 1.6f, 0, 0, 0.f, 0.09f, 0x404147, T_PENCIL, 0.06f, T_CPENCIL, 1.7f,
     {0x1f74c4, 0xd69400, 0x7a3cae, 0x2f8c36, 0xd02a1e, 0x22338a, 0xe25e12},
     {P_HATCH, P_CROSS, P_HATCH, P_SCRIBBLE, P_HATCH, P_RUB, P_HATCH},
     {45.f, 30.f, -45.f, 20.f, 0.f, 12.f, 90.f},
     {0.095f, 0.11f, 0.095f, 0.12f, 0.09f, 0.09f, 0.095f},
     0x45464c, T_PENCIL, 0xd63c30, T_CPENCIL, {0xd63c30, 0x2a82cc, 0xd99c18, 0x3f9944}, T_CPENCIL, 0x8f8f96, 0x3d63c9},
    // A squared blackboard: slate green, faint painted squares, white and pastel chalks.
    {"CHALK", false, 0x25322c, 0xdce8e0, 0.075f, 0, 1.f, 0, 0, 0.f, 0.12f, 0xeeefe8, T_CHALK, 0.075f, T_CHALK, 1.1f,
     {0x96d1f2, 0xf6e27a, 0xd2a9f2, 0xa2e39a, 0xf79494, 0x8ca8f8, 0xf8b673},
     {P_HATCH, P_RUB, P_CROSS, P_SCRIBBLE, P_HATCH, P_RUB, P_HATCH},
     {45.f, 0.f, 30.f, 20.f, -45.f, 80.f, 0.f},
     {0.09f, 0.08f, 0.105f, 0.11f, 0.09f, 0.08f, 0.09f},
     0xd5d8d0, T_CHALK, 0xf6e27a, T_CHALK, {0xf6e27a, 0xf79494, 0x96d1f2, 0xa2e39a}, T_CHALK, 0xb5bcb6, 0x86a6f2},
};
constexpr int VARIANT_COUNT = (int)(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

// ---------------------------------------------------------------- the doodles
// Shapes drawn in the margins, as polylines in a unit box (the first one closed when it can be hatched).
using Poly = std::vector<vec2>;
struct Shape { std::vector<Poly> lines; bool fillable; };
enum { D_STAR, D_HEART, D_SPIRAL, D_BOLT, D_NOTE, D_SUN, D_SMILEY, D_CUBE, D_FLOWER, D_ARROW, D_WAVE, D_COUNT };

Poly circle(vec2 c, float rx, float ry, int n, float a0 = 0.f, float a1 = TAU) {
    Poly p;
    for (int i = 0; i <= n; i++) {
        const float a = a0 + (a1 - a0) * i / n;
        p.push_back(v2(c.x + rx * std::cos(a), c.y + ry * std::sin(a)));
    }
    return p;
}
std::vector<Shape> buildShapes() {
    std::vector<Shape> s(D_COUNT);
    {
        Poly p;
        for (int i = 0; i <= 10; i++) {
            const float a = PI * 0.5f + i * TAU / 10.f, r = (i % 2) ? 0.42f : 1.f;
            p.push_back(v2(r * std::cos(a), r * std::sin(a)));
        }
        s[D_STAR] = {{p}, true};
    }
    {
        Poly p;
        for (int i = 0; i <= 32; i++) {
            const float t = TAU * i / 32.f;
            const float x = 16.f * std::pow(std::sin(t), 3.f);
            const float y = 13.f * std::cos(t) - 5.f * std::cos(2 * t) - 2.f * std::cos(3 * t) - std::cos(4 * t);
            p.push_back(v2(x / 17.f, (y + 2.f) / 17.f));
        }
        s[D_HEART] = {{p}, true};
    }
    {
        Poly p;
        for (int i = 0; i <= 44; i++) {
            const float t = i / 44.f, a = t * 2.6f * TAU;
            p.push_back(v2((0.06f + 0.94f * t) * std::cos(a), (0.06f + 0.94f * t) * std::sin(a)));
        }
        s[D_SPIRAL] = {{p}, false};
    }
    s[D_BOLT] = {{{v2(0.15f, 1.f), v2(-0.5f, 0.05f), v2(0.02f, 0.05f), v2(-0.3f, -1.f), v2(0.55f, 0.2f), v2(0.04f, 0.2f),
                   v2(0.45f, 1.f), v2(0.15f, 1.f)}}, true};
    {
        Poly head = circle(v2(-0.32f, -0.62f), 0.3f, 0.21f, 14);
        for (vec2& q : head) q = add(v2(-0.32f, -0.62f), rot(sub(q, v2(-0.32f, -0.62f)), 0.35f));
        s[D_NOTE] = {{head, {v2(-0.04f, -0.55f), v2(-0.04f, 0.92f)}, {v2(-0.04f, 0.92f), v2(0.38f, 0.6f), v2(0.42f, 0.18f)}}, false};
    }
    {
        Shape sun{{circle(v2(0, 0), 0.45f, 0.45f, 18)}, true};
        for (int i = 0; i < 8; i++) {
            const float a = i * TAU / 8.f + 0.2f;
            sun.lines.push_back({v2(0.62f * std::cos(a), 0.62f * std::sin(a)), v2(0.98f * std::cos(a), 0.98f * std::sin(a))});
        }
        s[D_SUN] = sun;
    }
    s[D_SMILEY] = {{circle(v2(0, 0), 1.f, 1.f, 26), {v2(-0.34f, 0.25f), v2(-0.34f, 0.45f)}, {v2(0.34f, 0.25f), v2(0.34f, 0.45f)},
                    circle(v2(0, 0.05f), 0.55f, 0.55f, 10, -PI * 0.85f, -PI * 0.15f)}, true};
    {
        const vec2 a(v2(-0.85f, -0.85f)), o(v2(0.45f, 0.4f));
        const float w = 1.2f;
        Poly front = {a, add(a, v2(w, 0)), add(a, v2(w, w)), add(a, v2(0, w)), a};
        Poly back = {add(a, o), add(a, add(o, v2(w, 0))), add(a, add(o, v2(w, w))), add(a, add(o, v2(0, w))), add(a, o)};
        s[D_CUBE] = {{front, back, {a, add(a, o)}, {add(a, v2(w, 0)), add(a, add(o, v2(w, 0)))},
                      {add(a, v2(w, w)), add(a, add(o, v2(w, w)))}, {add(a, v2(0, w)), add(a, add(o, v2(0, w)))}}, true};
    }
    {
        Shape fl{{circle(v2(0, 0), 0.24f, 0.24f, 10)}, true};
        for (int i = 0; i < 5; i++) {
            const float a = i * TAU / 5.f + 0.3f;
            Poly petal = circle(v2(0.6f, 0), 0.36f, 0.22f, 12);
            for (vec2& q : petal) q = rot(q, a);
            fl.lines.push_back(petal);
        }
        s[D_FLOWER] = fl;
    }
    {
        Poly arc;
        for (int i = 0; i <= 14; i++) {
            const float t = i / 14.f;
            arc.push_back(v2(-0.95f + 1.8f * t, -0.45f + 0.9f * std::sin(t * PI * 0.8f)));
        }
        const vec2 tip = arc.back(), dir = sub(arc.back(), arc[arc.size() - 2]);
        const float ang = std::atan2(dir.y, dir.x);
        s[D_ARROW] = {{arc, {add(tip, rot(v2(-0.4f, 0.2f), ang)), tip, add(tip, rot(v2(-0.4f, -0.2f), ang))}}, false};
    }
    {
        Poly w;
        for (int i = 0; i <= 24; i++) {
            const float t = i / 24.f;
            w.push_back(v2(-1.f + 2.f * t, 0.32f * std::sin(t * 3.f * TAU)));
        }
        s[D_WAVE] = {{w}, false};
    }
    return s;
}
const std::vector<Shape>& shapes() {
    static const std::vector<Shape> s = buildShapes();
    return s;
}

// Hatching inside a closed polygon: lines at an angle, cut by the polygon (even-odd).
std::vector<std::pair<vec2, vec2>> hatchPolygon(const Poly& poly, float ang, float spacing) {
    std::vector<std::pair<vec2, vec2>> out;
    if (poly.size() < 4) return out;
    Poly r;
    for (const vec2& p : poly) r.push_back(rot(p, -ang));
    float y0 = 1e9f, y1 = -1e9f;
    for (const vec2& p : r) y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
    std::vector<float> xs;
    for (float y = y0 + spacing * 0.5f; y < y1; y += spacing) {
        xs.clear();
        for (size_t i = 0; i + 1 < r.size(); i++) {
            const vec2 a = r[i], b = r[i + 1];
            if ((a.y <= y) == (b.y <= y)) continue;
            xs.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2)
            out.push_back({rot(v2(xs[i], y), ang), rot(v2(xs[i + 1], y), ang)});
    }
    return out;
}

// The staging, by stage: 0 break (calm), 1 mid, 2 chorus (a peak without a drop), 3 drop.
const float RATE[4] = {4.5f, 8.f, 10.f, 12.5f};         // drawings per second
const float WEIGHT[4] = {0.95f, 1.f, 1.1f, 1.32f};      // stroke weight
const float WOBBLE[4] = {0.02f, 0.025f, 0.03f, 0.038f}; // the hand's wobble, board units
const float BOIL[4] = {0.f, 0.f, 0.f, 0.f};             // no boil: what stays is not redrawn (stop motion, not jitter)
const float COLOR[4] = {0.92f, 1.f, 1.f, 1.f};          // colour against pencil
const float FILLA[4] = {0.9f, 0.96f, 1.f, 1.f};         // how dense the colouring is
const float HAND[4] = {0.35f, 0.55f, 0.75f, 1.f};       // how fast new things get drawn
const int DOODLES[4] = {0, 2, 5, 11};                   // doodles in the margins
const float RESIDUE_FADE = 0.36f;                       // the eraser going over the ghosts again (per second)

vec3 cellPos(float x, float y) { return layout::cellPos(x, y); }

} // namespace

// =====================================================================================================
// GL setup
// =====================================================================================================

bool NotepadUniverse::init(int width, int height) {
    progPaper_ = compile(sh::FULLSCREEN_VS, sh::PAPER_FS, "notepad paper");
    progMark_ = compile(sh::MARK_VS, sh::MARK_FS, "notepad marks");
    progText_ = compile(sh::TEXT_VS, sh::TEXT_FS, "notepad text");
    progPost_ = compile(sh::FULLSCREEN_VS, sh::POST_FS, "notepad post");
    progFade_ = compile(sh::FULLSCREEN_VS, sh::FADE_FS, "notepad eraser");

    glGenVertexArrays(1, &emptyVao_);
    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glGenBuffers(1, &markVbo_);
    glGenVertexArrays(1, &markVao_);
    glBindVertexArray(markVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, markVbo_);
    for (int i = 0; i < 6; i++) {
        glEnableVertexAttribArray(1 + i);
        glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(Mark), (void*)(sizeof(float) * 4 * i));
        glVertexAttribDivisor(1 + i, 1);
    }
    glGenVertexArrays(1, &textVao_);
    glGenBuffers(1, &textVbo_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 8, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 8, (void*)(sizeof(float) * 4));
    glBindVertexArray(0);
    buildFont();
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

NotepadUniverse::~NotepadUniverse() {
    residueT_.destroy();
    sceneT_.destroy();
    for (GLuint p : {progPaper_, progMark_, progText_, progPost_, progFade_}) glDeleteProgram(p);
    for (GLuint v : {markVao_, textVao_, emptyVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {quadVbo_, markVbo_, textVbo_}) glDeleteBuffers(1, &b);
    if (font_.tex) glDeleteTextures(1, &font_.tex);
    delete (stbtt_fontinfo*)font_.info;
}

void NotepadUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    // A flat view of the page at the reference camera's scale on the board plane (layout contract).
    const float aspect = (float)width_ / height_;
    const layout::Camera cam = layout::camera(width_, height_);
    const float hh = cam.dist * std::tan(cam.fovDeg * 0.5f * PI / 180.f), hw = hh * aspect;
    viewH_ = v2(hw, hh);
    viewC_ = v2(cam.target.x, cam.target.y);
    vp_ = mat4::ortho(viewC_.x - hw, viewC_.x + hw, viewC_.y - hh, viewC_.y + hh, -1.f, 1.f);
    px_ = 2.f * hh / height_;
    // the margin: about a seventh of the page in, always left of the HUD
    marginX_ = std::min(layout::LEFT_COLUMN[0] - 0.8f, std::floor(viewC_.x - hw + 0.28f * hw) + 0.5f);
    createTargets();
    clearPage_ = true;
    dirty_ = true;
}

void NotepadUniverse::createTargets() {
    residueT_.destroy();
    sceneT_.destroy();
    residueT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    sceneT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    residueT_.bind();
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// The handwriting: glyphs baked as signed distance fields (ASCII, plus the middle dot and the multiplication sign).
void NotepadUniverse::buildFont() {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, FONT_TTF, stbtt_GetFontOffsetForIndex(FONT_TTF, 0))) {
        std::fprintf(stderr, "[gl] notepad font init failed\n");
        delete info;
        return;
    }
    Font& f = font_;
    f.info = info;
    f.px = 64.f;
    f.scaleTT = stbtt_ScaleForPixelHeight(info, f.px);
    int x0, y0, x1, y1;
    if (stbtt_GetCodepointBox(info, 'H', &x0, &y0, &x1, &y1)) f.capH = y1 * f.scaleTT;
    const int AW = 1024, AH = 1024, PAD = 8;
    std::vector<unsigned char> atlas(AW * AH, 0);
    int x = 1, y = 1, rowH = 0;
    for (int slot = 0; slot < 97; slot++) {
        const int ch = slot < 95 ? 32 + slot : (slot == 95 ? 0xB7 : 0xD7);
        Glyph& g = f.g[slot];
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, ch, &adv, &lsb);
        g.adv = adv * f.scaleTT;
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* sdf = stbtt_GetCodepointSDF(info, f.scaleTT, ch, PAD, 128, 128.f / PAD, &w, &h, &xo, &yo);
        if (!sdf) continue;
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

namespace {
// UTF-8 to glyph slots (ASCII, the middle dot, the multiplication sign; anything else as '?').
std::vector<int> slots(const std::string& s) {
    std::vector<int> out;
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { out.push_back(c >= 32 && c < 127 ? c - 32 : '?' - 32); continue; }
        int cp = 0, extra = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else { cp = c & 0x07; extra = 3; }
        for (int k = 0; k < extra && i + 1 < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[++i] & 0x3F);
        out.push_back(cp == 0xB7 ? 95 : cp == 0xD7 ? 96 : '?' - 32);
    }
    return out;
}
int slotCode(int slot) { return slot < 95 ? 32 + slot : (slot == 95 ? 0xB7 : 0xD7); }
} // namespace

float NotepadUniverse::textWidth(const std::string& s, float size) const {
    if (!font_.ok) return s.size() * size * 0.5f;
    const stbtt_fontinfo* info = (const stbtt_fontinfo*)font_.info;
    const float k = size / font_.px;
    const std::vector<int> sl = slots(s);
    float w = 0;
    for (size_t i = 0; i < sl.size(); i++) {
        w += font_.g[sl[i]].adv * k;
        if (i + 1 < sl.size()) w += stbtt_GetCodepointKernAdvance(info, slotCode(sl[i]), slotCode(sl[i + 1])) * font_.scaleTT * k;
    }
    return w;
}

// =====================================================================================================
// Song, staging, events
// =====================================================================================================

int NotepadUniverse::pickVariant(const SongLook& t) const {
    if (!t.hasSong) return 0;
    const int r = (int)((t.seed >> 7) % 100);
    const bool night = t.minor && t.brightness < 0.45f;
    if (night ? r < 55 : r < 12) return 2;                  // the blackboard: dark songs, now and then any
    if (t.bpm >= 118.f && t.brightness >= 0.45f) return 0; // bright and fast: felt pens
    if (t.density < 0.45f || t.bpm < 100.f) return 1;      // sparse or slow: pencils
    return r % 2;
}

void NotepadUniverse::setSong(const SongLook& look, float seconds) {
    int v = look.variant >= 0 ? look.variant % VARIANT_COUNT : pickVariant(look);
    if (look.variant < 0 && v == look.avoidVariant) v = (v + 1 + (int)(look.seed % (VARIANT_COUNT - 1))) % VARIANT_COUNT;
    rng_ = Rng(look.seed ^ 0x4E07EBADull);
    if (v != variant_ || seconds <= 0.f) {
        variant_ = v;
        clearPage_ = true; // a new page
        if (seconds > 0.f) { items_.clear(); index_.clear(); doodles_.clear(); }
    }
    dirty_ = true;
}

void NotepadUniverse::setLevel(int level, float, float, float) {
    level_ = std::clamp(level, 0, 2);
    if (level_ < 2) { dropOn_ = false; movers_.clear(); }
    dirty_ = true;
}

// The drop's first downbeat: the hand goes frantic at once: the well is gone over in marker, a burst of
// doodles is drawn in the margins.
void NotepadUniverse::drop(float strength) {
    if (level_ < 2) return;
    dropOn_ = true;
    dropHeld_ = 3.f;
    markerBorn_ = drawing_ + 1;
    const int n = 1 + (int)std::lround(saturate(strength));
    for (int i = 0; i < n; i++) spawnDoodle(true);
    // the travelling drawings come in on the downbeat: the cube in one margin, one or two others
    movers_.clear();
    const int cubeSide = rng_.irange(0, 1);
    spawnMover(MV_CUBE, cubeSide);
    const int other = rng_.irange(1, MV_COUNT - 1);
    spawnMover(other, 1 - cubeSide);
    if (strength > 0.5f) spawnMover(1 + (other % (MV_COUNT - 1)), cubeSide);
    dirty_ = true;
    lastTick_ = -1; // draw now
}

// How far the landed piece is coloured in (0..1): about half a second, a third in a drop.
float NotepadUniverse::landProgress() const {
    if (landId_ == 0) return 1.f;
    const float dur = stage_ >= 3 ? 0.3f : 0.48f;
    return saturate((float)(clock_ - landT0_) / dur);
}

void NotepadUniverse::refresh(float strength) { refreshPending_ = std::max(refreshPending_, strength); }

vec2 NotepadUniverse::project(const vec3& p) const { return layout::project(vp_, p, width_, height_); }

TransitionHints NotepadUniverse::hints() const {
    const Variant& V = VARIANTS[variant_];
    TransitionHints h;
    h.background = hexc(V.paper);
    h.ink = hexc(V.outline);
    h.accent = hexc(V.marginN ? V.margin : V.accent);
    h.light = V.light;
    h.styles = TS_INK | TS_LUMA | TS_REBUILD;
    return h;
}

vec3 NotepadUniverse::inkOf(const vec3& c) const { return VARIANTS[variant_].light ? c : vec3(1.f) - c; }

void NotepadUniverse::onEvent(const GameEvent& ev, const Game& game) {
    (void)game;
    switch (ev.type) {
    case GameEvent::Spawn:
        activeId_ = serial_++;
        break;
    case GameEvent::HardDrop:
        hardPending_ = true;
        break;
    case GameEvent::Lock: {
        // The falling scribble becomes a piece: outlined at once, then coloured in over a few drawings.
        landId_ = activeId_;
        landT0_ = clock_;
        landDrawn_ = 0.f;
        for (size_t i = 0; i < ev.cells.size(); i++) {
            const auto& c = ev.cells[i];
            if (c.y >= 0 && c.y < Game::H && c.x >= 0 && c.x < Game::W) ink_[c.y][c.x] = {activeId_, (int)i, c.type};
        }
        hardPending_ = false;
        dirty_ = true;
        break;
    }
    case GameEvent::Clear: {
        bool cleared[Game::H] = {};
        for (const auto& c : ev.cells) if (c.y >= 0 && c.y < Game::H) cleared[c.y] = true;
        auto same = [&](int x, int y, uint64_t id) {
            return x >= 0 && x < Game::W && y >= 0 && y < Game::H && cleared[y] && ink_[y][x].id == id;
        };
        for (const auto& c : ev.cells) {
            if (c.y < 0 || c.y >= Game::H || c.x < 0 || c.x >= Game::W) continue;
            InkCell ic = ink_[c.y][c.x];
            if (ic.type != c.type || ic.id == 0) ic = {serial_++, 0, c.type};
            int mask = 0;
            if (same(c.x - 1, c.y, ic.id)) mask |= 1;
            if (same(c.x + 1, c.y, ic.id)) mask |= 2;
            if (same(c.x, c.y + 1, ic.id)) mask |= 4;
            if (same(c.x, c.y - 1, ic.id)) mask |= 8;
            const float rv = ic.id == landId_ ? landReveal(landProgress(), ic.idx) : 1.f;
            dying_.push_back({c.x, c.y, c.type, mask, mix(ic.id, 1 + (uint64_t)ic.idx), 0.f, 0.32f, rv});
        }
        // the eraser's crumbs, rolled off the rows
        for (int y = 0; y < Game::H; y++) {
            if (!cleared[y] || y < Game::HIDDEN) continue;
            const float wy = cellPos(0, (float)y).y;
            const int n = ev.count >= 4 ? 6 : 4;
            for (int k = 0; k < n; k++) {
                const vec2 p = v2(rng_.range(-4.6f, 4.6f), wy - 0.42f + rng_.range(-0.1f, 0.1f));
                const float a = rng_.range(0.f, TAU), l = rng_.range(0.07f, 0.13f);
                Transient t{p, add(p, v2(std::cos(a) * l, std::sin(a) * l)), v2(rng_.range(-0.9f, 0.9f), rng_.range(-1.6f, -0.5f)),
                            rng_.range(0.06f, 0.09f), 0.85f, (float)clock_ + 0.12f + 0.025f * k,
                            (float)clock_ + rng_.range(0.5f, 0.75f), hexc(VARIANTS[variant_].crumb),
                            VARIANTS[variant_].light ? T_PENCIL : T_CHALK, serial_++, 0.f, true};
                transients_.push_back(t);
            }
        }
        // A Tetris: two big stars drawn fast in the margins.
        if (ev.count >= 4) {
            for (int i = 0; i < 2; i++) {
                Doodle d;
                d.shape = D_STAR;
                d.size = rng_.range(1.5f, 1.8f);
                if (!freeSpot(d.size, d.pos)) continue;
                d.rot = rng_.range(-0.3f, 0.3f);
                const Variant& V = VARIANTS[variant_];
                d.col = hexc(i ? V.accent : V.doodle[1 + rng_.irange(0, 2)]);
                d.tool = V.doodleTool;
                d.hatch = true;
                d.speed = 0.5f;
                d.until = (float)clock_ + 3.5f;
                d.id = serial_++;
                doodles_.push_back(d);
            }
        }
        // collapse the bookkeeping like the board
        InkCell next[Game::H][Game::W];
        int dst = Game::H - 1;
        for (int y = Game::H - 1; y >= 0; y--) {
            if (cleared[y]) continue;
            for (int x = 0; x < Game::W; x++) next[dst][x] = ink_[y][x];
            dst--;
        }
        std::memcpy(ink_, next, sizeof(ink_));
        dirty_ = true;
        break;
    }
    case GameEvent::TopOut: {
        // Game over: the page is scribbled over, then rubbed out.
        dead_.clear();
        deadTop_ = -10.f;
        for (const Item& it : items_)
            if (it.i2 & 16) {
                dead_.push_back(it);
                if (it.kind == IT_CELL) deadTop_ = std::max(deadTop_, it.y + 0.5f);
            }
        for (Item& it : dead_) it.residue = 0.5f;
        overT_ = 0.f;
        for (auto& row : ink_)
            for (auto& c : row) c = InkCell{};
        dirty_ = true;
        break;
    }
    case GameEvent::BonusReady:
        dirty_ = true;
        break;
    default: break;
    }
}

// Cells of the board the drawing doesn't know yet (a stack inherited from another universe) become pieces of
// their own; emptied cells are forgotten.
// Unknown cells of a colour are grouped four at most, like pieces.
void NotepadUniverse::syncInk(const Game& g) {
    bool unknown[Game::H][Game::W] = {};
    for (int y = 0; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            InkCell& ic = ink_[y][x];
            if (c.type < 0) ic = InkCell{};
            else if (ic.type != c.type || ic.id == 0) unknown[y][x] = true;
        }
    for (int y = Game::H - 1; y >= 0; y--)
        for (int x = 0; x < Game::W; x++) {
            if (!unknown[y][x]) continue;
            const int type = g.cell(x, y).type;
            const uint64_t id = serial_++;
            int qx[4] = {x}, qy[4] = {y}, n = 1;
            unknown[y][x] = false;
            for (int h = 0; h < n && n < 4; h++) {
                const int d[4][2] = {{1, 0}, {0, -1}, {-1, 0}, {0, 1}};
                for (auto& o : d) {
                    const int nx = qx[h] + o[0], ny = qy[h] + o[1];
                    if (n >= 4 || nx < 0 || nx >= Game::W || ny < 0 || ny >= Game::H) continue;
                    if (!unknown[ny][nx] || g.cell(nx, ny).type != type) continue;
                    unknown[ny][nx] = false;
                    qx[n] = nx; qy[n] = ny; n++;
                }
            }
            for (int k = 0; k < n; k++) ink_[qy[k]][qx[k]] = {id, k, type};
        }
}

// =====================================================================================================
// Doodles
// =====================================================================================================

bool NotepadUniverse::freeSpot(float size, vec2& out) {
    struct Box { float x0, y0, x1, y1; };
    const float L = viewC_.x - viewH_.x, R = viewC_.x + viewH_.x, B = viewC_.y - viewH_.y, T = viewC_.y + viewH_.y;
    const Box boxes[3] = {
        {L + 1.3f, -7.9f, layout::LEFT_COLUMN[0] - 1.1f, std::min(8.3f, T - 3.6f)},         // left margin
        {layout::RIGHT_COLUMN[2] + 1.1f, B + 1.2f, R - 1.3f, T - 1.3f},                    // right of the HUD
        {layout::LEFT_COLUMN[0] + 0.6f, B + 1.f, layout::LEFT_COLUMN[2] - 0.4f, -7.6f},    // under the score
    };
    float area[3];
    for (int i = 0; i < 3; i++) {
        const float w = boxes[i].x1 - boxes[i].x0 - 2.f * size, h = boxes[i].y1 - boxes[i].y0 - 2.f * size;
        area[i] = w > 0.f && h > 0.f ? (w + 1.f) * (h + 1.f) : 0.f;
    }
    if (area[0] + area[1] + area[2] <= 0.f) return false;
    for (int tries = 0; tries < 40; tries++) {
        const int b = rng_.weighted(area);
        const Box& bx = boxes[b];
        const vec2 p = v2(rng_.range(bx.x0 + size, bx.x1 - size), rng_.range(bx.y0 + size, bx.y1 - size));
        bool ok = true;
        for (const Doodle& d : doodles_)
            if (len(sub(d.pos, p)) < (d.size + size) * 1.12f + 0.3f) { ok = false; break; }
        if (ok) { out = p; return true; }
    }
    return false;
}

void NotepadUniverse::spawnDoodle(bool burst) {
    const Variant& V = VARIANTS[variant_];
    Doodle d;
    d.size = burst ? rng_.range(1.1f, 1.7f) : rng_.range(0.8f, 1.35f);
    if (!freeSpot(d.size, d.pos)) return;
    const int st = burst ? 3 : stage_;
    d.shape = rng_.irange(0, D_COUNT - 1);
    // not the same shape as the last ones
    for (int k = 0; k < 4; k++) {
        bool seen = false;
        for (size_t i = doodles_.size() > 3 ? doodles_.size() - 3 : 0; i < doodles_.size(); i++) seen |= doodles_[i].shape == d.shape;
        if (!seen) break;
        d.shape = rng_.irange(0, D_COUNT - 1);
    }
    d.rot = rng_.range(-0.35f, 0.35f);
    if (st >= 3) {
        d.col = hexc(V.doodle[rng_.irange(0, 3)]);
        d.tool = V.doodleTool;
        d.hatch = shapes()[d.shape].fillable && rng_.chance(0.65f);
        d.speed = burst ? 0.5f : 0.34f;
    } else if (st == 2) {
        d.col = rng_.chance(0.5f) ? hexc(V.doodle[rng_.irange(0, 3)]) : hexc(V.outline);
        d.tool = rng_.chance(0.5f) ? V.doodleTool : V.outlineTool;
        d.hatch = shapes()[d.shape].fillable && rng_.chance(0.3f);
        d.speed = 0.25f;
    } else {
        d.col = hexc(V.graphite);
        d.tool = V.calmTool;
        d.hatch = false;
        d.speed = 0.18f;
    }
    d.id = serial_++;
    doodles_.push_back(d);
}

// Doodles come with the music (on the beat grid) and are rubbed out, one per drawing, when it calms down.
void NotepadUniverse::manageDoodles() {
    for (size_t i = 0; i < doodles_.size();)
        if (doodles_[i].until > 0.f && clock_ > doodles_[i].until) doodles_.erase(doodles_.begin() + i);
        else i++;
    int cap = DOODLES[stage_];
    if (!movers_.empty()) cap = std::min(cap, 2); // the travelling drawings need the room
    if (slow_ > 0.5f) cap = std::min(cap, 2);
    int songDoodles = 0;
    for (const Doodle& d : doodles_) songDoodles += d.until < 0.f;
    if (songDoodles > cap) {
        for (size_t i = 0; i < doodles_.size(); i++)
            if (doodles_[i].until < 0.f) { doodles_.erase(doodles_.begin() + i); break; }
        return;
    }
    if (refreshPending_ > 0.f && cap > 0) {
        refreshPending_ = 0.f;
        for (size_t i = 0; i < doodles_.size(); i++)
            if (doodles_[i].until < 0.f) { doodles_.erase(doodles_.begin() + i); break; }
        spawnDoodle(false);
        return;
    }
    const int every = stage_ >= 3 ? 2 : stage_ == 2 ? 4 : 16;
    if (cap > 0 && beatsSeen_ - lastDoodleBeat_ >= every) {
        lastDoodleBeat_ = beatsSeen_;
        if (songDoodles >= cap) {
            for (size_t i = 0; i < doodles_.size(); i++)
                if (doodles_[i].until < 0.f) { doodles_.erase(doodles_.begin() + i); break; }
        }
        spawnDoodle(false);
    }
}

// =====================================================================================================
// A drawing
// =====================================================================================================

void NotepadUniverse::style(const MusicState& music) {
    (void)music;
    const Variant& V = VARIANTS[variant_];
    const int st = stage_;
    weight_ = WEIGHT[st];
    wobble_ = WOBBLE[st];
    boil_ = BOIL[st];
    colorAmt_ = COLOR[st];
    fillAlpha_ = FILLA[st];
    handSpeed_ = HAND[st];
    graphite_ = hexc(V.graphite);
    outlineCol_ = hexc(V.outline); // a firm outline in every phase (a break only changes the tool)
    outlineTool_ = st == 0 ? V.calmTool : V.outlineTool;
    if (slow_ > 0.01f) {
        // the slow motion: an animator's blue pencil rough
        const vec3 blue = hexc(V.blue);
        outlineCol_ = lerp(outlineCol_, lerp(blue, hexc(V.outline), 0.25f), slow_);
        if (slow_ > 0.5f) outlineTool_ = V.light ? T_CPENCIL : T_CHALK;
        colorAmt_ *= 1.f - 0.85f * slow_;
    }
}

// A piece drawn in the squares: its cells coloured in, one outline around them all. `cells` are board-like
// cells (x right, row down) at `origin` (cell 0, 0) and `scale`.
void NotepadUniverse::addPiece(std::vector<Item>& out, uint64_t base, const int (*cells)[2], int count, vec2 origin,
                               float scale, int type, float alpha, bool ghost, float residue, float moveResidue,
                               int maskCells) {
    (void)maskCells;
    if (count <= 0) return;
    const Variant& V = VARIANTS[variant_];
    const float pseed = hseed(base);
    auto has = [&](int x, int y) {
        for (int k = 0; k < count; k++) if (cells[k][0] == x && cells[k][1] == y) return true;
        return false;
    };
    if (!ghost) {
        vec3 col = lerp(graphite_, hexc(V.piece[type]), colorAmt_);
        if (slow_ > 0.01f) col = lerp(col, hexc(V.blue), slow_ * 0.85f);
        int tool = colorAmt_ < 0.5f ? (V.light ? T_PENCIL : T_CHALK) : V.fillTool;
        if (slow_ > 0.5f) tool = V.light ? T_CPENCIL : T_CHALK;
        for (int k = 0; k < count; k++) {
            const int x = cells[k][0], y = cells[k][1];
            Item it;
            it.key = mix(base, 1 + (uint64_t)k);
            it.kind = IT_CELL;
            it.x = origin.x + x * scale;
            it.y = origin.y - y * scale;
            it.s = scale;
            it.i0 = type;
            it.i1 = (has(x - 1, y) ? 1 : 0) | (has(x + 1, y) ? 2 : 0) | (has(x, y + 1) ? 4 : 0) | (has(x, y - 1) ? 8 : 0);
            it.col = col;
            it.tool = tool;
            it.alpha = alpha;
            it.c = fillAlpha_;
            it.d = pseed;
            it.residue = residue;
            it.moveResidue = moveResidue;
            out.push_back(it);
        }
    }
    Item o;
    o.key = mix(base, 0x77);
    o.kind = IT_OUTLINE;
    o.x = origin.x + cells[0][0] * scale;
    o.y = origin.y - cells[0][1] * scale;
    o.s = scale;
    o.n = std::min(count, 4);
    for (int k = 0; k < o.n; k++) {
        o.pts[k * 2] = (float)(cells[k][0] - cells[0][0]);
        o.pts[k * 2 + 1] = (float)(cells[k][1] - cells[0][1]);
    }
    o.i0 = type;
    o.i1 = ghost ? 1 : 0;
    // the ghost: a dashed outline in the piece's own colour (a little deeper), lightly hatched inside
    vec3 gcol = lerp(hexc(V.piece[type]), outlineCol_, V.light ? 0.3f : 0.1f);
    gcol = lerp(graphite_, gcol, std::max(0.45f, colorAmt_));
    if (slow_ > 0.01f) gcol = lerp(gcol, hexc(V.blue), slow_ * 0.85f);
    o.col = ghost ? gcol : outlineCol_;
    o.tool = ghost ? (V.light ? T_CPENCIL : T_CHALK) : outlineTool_;
    o.alpha = ghost ? 0.9f * alpha : alpha;
    o.b = (ghost ? 1.05f : 1.f) * weight_;
    o.d = pseed;
    o.residue = ghost ? 0.f : residue * 0.8f;
    o.moveResidue = ghost ? 0.f : moveResidue;
    out.push_back(o);
}

void NotepadUniverse::buildDrawing(const Frame& f, std::vector<Item>& out) {
    const Game& g = f.game;
    const Variant& V = VARIANTS[variant_];
    syncInk(g);

    // ---- the well, drawn twice over in pen (and, from the drop, gone over in marker)
    {
        const float x0 = -5.f, x1 = 5.f, y0 = -10.f, y1 = 10.2f;
        const vec2 pts[4] = {v2(x0, y1), v2(x0, y0), v2(x1, y0), v2(x1, y1)};
        for (int pass = 0; pass < 2; pass++)
            for (int e = 0; e < 3; e++) {
                Item it;
                it.key = tag('W', (uint64_t)(pass * 4 + e));
                it.kind = IT_STROKE;
                const vec2 a = pts[e], b = pts[e + 1];
                const vec2 d = mul(sub(b, a), 1.f / len(sub(b, a)));
                const float o0 = pass ? -0.05f : 0.08f, o1 = pass ? 0.12f : -0.03f;
                it.x = a.x - d.x * o0 + (pass ? 0.025f : 0.f);
                it.y = a.y - d.y * o0 + (pass ? -0.02f : 0.f);
                it.a = b.x + d.x * o1 + (pass ? 0.02f : 0.f);
                it.b = b.y + d.y * o1 + (pass ? 0.025f : 0.f);
                it.c = V.outlineW * 1.25f * weight_;
                it.col = outlineCol_;
                it.tool = outlineTool_;
                it.alpha = pass ? 0.55f : 1.f;
                it.r = (pass ? -1.f : 1.f) * 0.03f;
                it.i1 = 2; // drawn over two drawings when new
                it.residue = 0.3f;
                out.push_back(it);
            }
        if (dropOn_ && stage_ == 3) {
            for (int e = 0; e < 3; e++) {
                Item it;
                it.key = tag('M', (uint64_t)e);
                it.kind = IT_STROKE;
                const vec2 a = pts[e], b = pts[e + 1];
                const vec2 d = mul(sub(b, a), 1.f / len(sub(b, a)));
                it.x = a.x - d.x * 0.15f - 0.04f;
                it.y = a.y - d.y * 0.15f;
                it.a = b.x + d.x * 0.2f + 0.03f;
                it.b = b.y + d.y * 0.2f;
                it.c = V.accentTool == T_HIGHLIGHT ? 0.32f : 0.15f;
                it.col = hexc(V.accent);
                it.tool = V.accentTool;
                it.alpha = V.accentTool == T_HIGHLIGHT ? 0.55f : 0.9f;
                it.r = 0.05f;
                it.i1 = 3;
                it.residue = 0.35f;
                it.world = true;
                out.push_back(it);
            }
        }
    }

    // ---- the stack: one item per cell (its colouring) and one outline per piece
    if (overT_ < 0.f || g.over() == false) {
        struct Group { uint64_t id; int type; int n; int cells[4][2]; int idx[4]; };
        std::vector<Group> groups;
        for (int y = Game::HIDDEN; y < Game::H; y++)
            for (int x = 0; x < Game::W; x++) {
                const Cell& c = g.cell(x, y);
                if (c.type < 0) continue;
                const InkCell& ic = ink_[y][x];
                const int row = y - (int)std::lround(g.rowOffset(y));
                Group* gr = nullptr;
                for (Group& q : groups) if (q.id == ic.id) { gr = &q; break; }
                if (!gr || gr->n >= 4) { groups.push_back({ic.id, c.type, 0, {}, {}}); gr = &groups.back(); }
                gr->cells[gr->n][0] = x;
                gr->cells[gr->n][1] = row;
                gr->idx[gr->n] = ic.idx;
                gr->n++;
            }
        const vec3 o = cellPos(0, 0);
        for (const Group& gr : groups) {
            const size_t first = out.size();
            addPiece(out, mix(gr.id, 0xC311), gr.cells, gr.n, v2(o.x, o.y), 1.f, gr.type, 1.f, false, 0.38f, 0.12f);
            // the cells keep their own keys (the piece's index of each cell), so they survive clears and moves
            const float lp = gr.id == landId_ ? landProgress() : 1.f;
            int k = 0;
            for (size_t i = first; i < out.size(); i++) {
                out[i].i2 |= 16;
                if (out[i].kind != IT_CELL) continue;
                if (lp < 1.f) { out[i].i2 |= 32; out[i].b = landReveal(lp, gr.idx[k]); }
                out[i].key = mix(gr.id, 1 + (uint64_t)gr.idx[k++]);
            }
        }
    }
    // ---- rows being rubbed out
    for (const Dying& d : dying_) {
        if (d.t >= d.dur) continue;
        const vec3 p = cellPos((float)d.x, (float)d.y);
        Item it;
        it.key = d.key;
        it.kind = IT_CELL;
        it.x = p.x;
        it.y = p.y;
        it.i0 = d.type;
        it.i1 = d.mask;
        it.i2 = 8; // its own outline
        if (d.reveal < 1.f) { it.i2 |= 32; it.b = d.reveal; } // caught while being coloured in
        it.a = std::min(1.f, std::floor(d.t / d.dur * 4.f + 1.f) / 4.f) * 0.92f;
        vec3 col = lerp(graphite_, hexc(V.piece[d.type]), colorAmt_);
        it.col = col;
        it.tool = colorAmt_ < 0.5f ? (V.light ? T_PENCIL : T_CHALK) : V.fillTool;
        it.c = fillAlpha_;
        it.d = hseed(d.key);
        it.residue = 0.42f;
        out.push_back(it);
    }
    // ---- game over: the dead stack, scribbled over, then rubbed out
    if (overT_ >= 0.f && overT_ < 2.6f) {
        for (const Item& it : dead_) out.push_back(it);
        const int n = std::min(12, (int)(overT_ / 0.11f));
        for (int k = 0; k < n; k++) {
            Item it;
            it.key = tag('S', (uint64_t)k);
            it.kind = IT_SCRIBBLE;
            Rng r(splitmix64(0x5C1Bull + k * 7919ull + (uint64_t)drawing_ / 1000000ull));
            it.x = r.range(-4.2f, 4.2f);
            it.y = r.range(-9.6f, std::max(-8.f, deadTop_));
            it.s = r.range(1.4f, 2.6f);
            it.r = r.range(-0.6f, 0.6f);
            it.col = outlineCol_;
            it.tool = V.light ? T_PEN : T_CHALK;
            it.alpha = 0.85f;
            it.residue = 0.32f;
            out.push_back(it);
        }
    }

    // ---- the falling piece and its ghost
    if (g.hasPiece() && overT_ < 0.f) {
        if (activeId_ == 0) activeId_ = serial_++;
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        int n = 0;
        for (auto& c : cells) n += c[1] >= Game::HIDDEN;
        const vec3 o = cellPos(0, 0);
        // the ghost first (under), a dashed pencil outline where the piece would land
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y + 1) {
            int gc[4][2];
            g.pieceCells(gp, gc);
            int gv[4][2], gn = 0;
            for (auto& c : gc)
                if (c[1] >= Game::HIDDEN) { gv[gn][0] = c[0]; gv[gn][1] = c[1]; gn++; }
            addPiece(out, mix(activeId_, 0x6057), gv, gn, v2(o.x, o.y), 1.f, p.type, 1.f, true, 0.f, 0.f);
        }
        // the piece: a quick scribble in its colour, a new one wherever it goes (not coloured in until it lands)
        if (n > 0) {
            int all[4][2], m = 0;
            for (int i = 0; i < 4; i++)
                if (cells[i][1] >= Game::HIDDEN) { all[m][0] = cells[i][0]; all[m][1] = cells[i][1]; m++; }
            Item sc;
            sc.key = mix(activeId_, 0x5C21);
            sc.kind = IT_OUTLINE;
            sc.x = o.x + all[0][0];
            sc.y = o.y - all[0][1];
            sc.s = 1.f;
            sc.n = m;
            for (int k = 0; k < m; k++) {
                sc.pts[k * 2] = (float)(all[k][0] - all[0][0]);
                sc.pts[k * 2 + 1] = (float)(all[k][1] - all[0][1]);
            }
            sc.i0 = p.type;
            sc.i1 = 2; // a scribble
            vec3 col = lerp(hexc(V.piece[p.type]), outlineCol_, V.light ? 0.15f : 0.f);
            if (slow_ > 0.01f) col = lerp(col, hexc(V.blue), slow_ * 0.85f);
            sc.col = col;
            sc.tool = slow_ > 0.5f ? (V.light ? T_CPENCIL : T_CHALK) : V.fillTool;
            sc.alpha = 1.f;
            sc.b = weight_;
            sc.d = hseed(sc.key);
            sc.residue = 0.36f;
            sc.moveResidue = 0.26f;
            out.push_back(sc);
        }
    }

    // ---- previews: the held piece on the left, the next three on the right, in the squares
    auto preview = [&](uint64_t base, int type, vec3 anchor, float alpha, float residue) {
        Piece pc{type, 0, 0, 0};
        int cells[4][2];
        g.pieceCells(pc, cells);
        int minx = 99, maxx = -99, miny = 99, maxy = -99;
        for (auto& c : cells) {
            minx = std::min(minx, c[0]); maxx = std::max(maxx, c[0]);
            miny = std::min(miny, c[1]); maxy = std::max(maxy, c[1]);
        }
        // snapped on the grid: cell centers at half units
        float ox = anchor.x - (minx + maxx) * 0.5f, oy = anchor.y + (miny + maxy) * 0.5f;
        ox = std::floor(ox) + 0.5f;
        oy = std::floor(oy) + 0.5f;
        addPiece(out, base, cells, 4, v2(ox, oy), 1.f, type, alpha, false, residue, residue);
    };
    if (g.holdType() >= 0)
        preview(tag('H', (uint64_t)g.holdType()), g.holdType(), layout::HOLD_CENTER, g.holdUsed() ? 0.4f : 1.f, 0.35f);
    for (int i = 0; i < 3; i++)
        preview(tag('N', (uint64_t)(i * 16 + g.next(i))), g.next(i), layout::nextCenter(i), i == 0 ? 1.f : 0.7f, 0.3f);

    // ---- the bonus gauge: a pie coloured in as the lines charge it
    {
        Item it;
        it.key = tag('P', 1);
        it.kind = IT_PIE;
        it.x = layout::GAUGE_CENTER.x;
        it.y = layout::GAUGE_CENTER.y;
        it.s = layout::GAUGE_R * 0.92f;
        it.a = std::floor(g.bonusGauge() * 24.f + 0.001f) / 24.f;
        it.i0 = g.bonusReady() ? 1 : g.bonusActive() ? 2 : 0;
        it.col = it.i0 ? hexc(V.accent) : outlineCol_;
        it.tool = it.i0 ? V.accentTool : (V.light ? T_PENCIL : T_CHALK);
        it.residue = 0.25f;
        out.push_back(it);
    }

    // ---- impact marks and crumbs
    for (const Transient& t : transients_) {
        if (clock_ < t.from || clock_ >= t.until) continue;
        Item it;
        it.key = t.id;
        it.kind = IT_STROKE;
        const float age = (float)(clock_ - t.from);
        const vec2 dp = mul(t.vel, age + 0.6f * age * age);
        it.x = t.a.x + dp.x;
        it.y = t.a.y + dp.y;
        it.a = t.b.x + dp.x;
        it.b = t.b.y + dp.y;
        it.c = t.width * weight_;
        it.col = t.col;
        it.tool = t.tool;
        it.alpha = t.alpha;
        it.r = t.world ? 0.025f : 0.f;
        it.residue = t.residue;
        it.world = t.world;
        out.push_back(it);
    }

    // ---- doodles
    for (const Doodle& d : doodles_) {
        Item it;
        it.key = tag('D', d.id);
        it.kind = IT_DOODLE;
        it.x = d.pos.x;
        it.y = d.pos.y;
        it.s = d.size;
        it.r = d.rot;
        it.i0 = d.shape;
        it.i1 = d.hatch ? 1 : 0;
        it.col = d.col;
        it.tool = d.tool;
        it.c = d.speed;
        it.world = true;
        it.residue = 0.35f;
        // alive in a drop: hearts and stars hop on the kicks, the stars turn with the beats (local, in steps)
        if (stage_ >= 2 && (d.shape == D_HEART || d.shape == D_STAR || d.shape == D_SUN) && kick_ > 0.45f) it.s *= 1.14f;
        if (stage_ >= 3 && (d.shape == D_STAR || d.shape == D_SPIRAL || d.shape == D_FLOWER))
            it.r += (float)((beatsSeen_ / 2) % 8) * 0.22f;
        out.push_back(it);
    }

    // ---- the drop's travelling drawings: a new pose on every drawing (the old one is rubbed out)
    for (const Mover& m : movers_) {
        float x0, y0, x1, y1;
        marginBox(m.side, x0, y0, x1, y1);
        y0 = m.ylo, y1 = m.yhi;
        const float p = (float)(beatNow_ - m.beat0);
        const float pad = m.size * 1.05f;
        const float sg = m.vel.x >= 0.f ? 1.f : -1.f;
        // a coordinate going back and forth between lo and hi (dir: +1 while it goes up)
        auto bounce = [](float v, float lo, float hi, float* dir) {
            const float span = std::max(0.01f, hi - lo);
            float t = std::fmod(v - lo, 2.f * span);
            if (t < 0.f) t += 2.f * span;
            if (dir) *dir = t < span ? 1.f : -1.f;
            return t < span ? lo + t : lo + 2.f * span - t;
        };
        Item it;
        it.key = tag('V', m.id);
        it.kind = IT_MOVER;
        it.i0 = m.kind;
        it.s = m.size;
        it.col = m.col;
        it.tool = m.tool;
        it.a = p;
        it.world = true;
        it.residue = 0.35f;
        it.moveResidue = V.light ? 0.3f : 0.17f; // chalk dust shows more
        float dir = 1.f;
        switch (m.kind) {
        case MV_CUBE:
            it.x = bounce(m.start.x + m.vel.x * p, x0 + pad, x1 - pad, nullptr);
            it.y = bounce(m.start.y + m.vel.y * p, y0 + pad, y1 - pad, nullptr);
            break;
        case MV_BALL:
        case MV_RUNNER:
            it.x = bounce(m.start.x + m.vel.x * p, x0 + pad, x1 - pad, &dir);
            it.y = y0 + 0.4f;
            it.r = dir * sg;
            break;
        case MV_PLANE: {
            const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
            const float R = std::max(0.6f, std::min({0.5f * (x1 - x0) - pad, (0.5f * (y1 - y0) - pad) / 0.75f, 2.8f}));
            const float a = p * 0.6f * sg;
            it.x = cx + R * std::cos(a);
            it.y = cy + R * 0.75f * std::sin(a);
            it.r = std::atan2(sg * 0.75f * std::cos(a), -sg * std::sin(a));
            break;
        }
        case MV_ROCKET: {
            const float lo = y0 - pad * 0.3f, span = std::max(1.f, y1 - y0);
            float t = std::fmod(m.start.y - lo + m.vel.y * p, span);
            if (t < 0.f) t += span;
            it.x = std::clamp(m.start.x + 0.35f * std::sin(p * 0.9f), x0 + pad, x1 - pad);
            it.y = lo + t;
            break;
        }
        default: break;
        }
        out.push_back(it);
    }

    hudItems(f, out);
}

// The HUD, handwritten on the page.
void NotepadUniverse::hudItems(const Frame& f, std::vector<Item>& out) {
    const Variant& V = VARIANTS[variant_];
    const vec3 pen = outlineCol_, accent = hexc(V.marginN ? V.margin : V.accent);
    const vec3 pencil = lerp(graphite_, pen, 0.35f);
    auto titleCase = [](std::string s) {
        bool start = true;
        for (char& c : s) {
            if (std::isalpha((unsigned char)c)) { c = start ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c); start = false; }
            else start = c == ' ' || c == '-';
        }
        return s;
    };
    int counter = 0;
    for (const HudText& h : f.hud) {
        if (h.alpha <= 0.03f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        float size = base;
        vec3 col = pen;
        std::string text = h.text;
        float dy = 0.f;
        bool underline = false;
        switch (h.role) {
        case HUD_TITLE: size = base * 1.7f; dy = -base * 0.3f; break;
        case HUD_INFO: size = base * 1.15f; col = pencil; break;
        case HUD_SECTION: size = base * 1.6f; text = titleCase(text); dy = -base * 0.2f; if (stage_ >= 3) col = accent; break;
        case HUD_UNIVERSE: size = base * 1.2f; text = titleCase(text); col = pencil; break;
        case HUD_LABEL: size = base * 1.2f; text = titleCase(text); col = lerp(pencil, pen, 0.5f); underline = true; break;
        case HUD_VALUE: size = base * 1.5f; dy = -base * 0.1f; break;
        case HUD_NOTICE: size = base * 1.4f; col = accent; break;
        case HUD_KEY: size = base * 1.1f; break;
        case HUD_TOAST: size = base * 2.f; text = titleCase(text) + "!"; col = accent; dy = -base * 0.3f; break;
        case HUD_TOAST_SUB: size = base * 1.3f; break;
        case HUD_DIALOG: size = h.scale >= 3.5f * height_ / 1080.f ? base * 1.7f : base * 1.15f; break;
        default: size = base * 1.08f; if (h.accent) col = accent; break;
        }
        const float w = textWidth(text, size);
        Item it;
        it.kind = IT_TEXT;
        it.text = text;
        it.x = ugl::hudLeft(h, w, width_);
        it.y = h.y + dy;
        it.s = size;
        it.col = col;
        const float boost = h.role == HUD_TITLE || h.role == HUD_INFO || h.role == HUD_UNIVERSE || h.role == HUD_SECTION || h.role == HUD_DIALOG ? 1.8f : 1.2f;
        it.alpha = std::floor(saturate(h.alpha * boost) * 8.f + 0.5f) / 8.f;
        if (it.alpha <= 0.f) continue;
        it.key = mix(strHash(text), (uint64_t)h.role * 977u + (uint64_t)std::lround(it.x / 24.f));
        for (const Item& o : out) if (o.key == it.key) { it.key = mix(it.key, (uint64_t)++counter); break; }
        it.residue = 0.16f;
        it.i0 = h.role;
        out.push_back(it);
        if (underline) {
            Item u;
            u.key = mix(it.key, 0x0D1);
            u.kind = IT_STROKE;
            const float yb = it.y + size * 1.05f;
            u.x = viewC_.x - viewH_.x + (it.x - size * 0.1f) * px_;
            u.y = viewC_.y + viewH_.y - yb * px_;
            u.a = viewC_.x - viewH_.x + (it.x + w + size * 0.15f) * px_;
            u.b = viewC_.y + viewH_.y - (yb + size * 0.05f) * px_;
            u.c = V.outlineW * 0.75f * weight_;
            u.col = pencil;
            u.tool = V.light ? T_PENCIL : T_CHALK;
            u.alpha = it.alpha * 0.7f;
            u.r = -0.04f;
            u.residue = 0.25f;
            out.push_back(u);
        }
    }
}

// =====================================================================================================
// Marks
// =====================================================================================================

namespace {
float eraseNow = 0.f, eraseSeedNow = 0.f; // the cell being emitted is being rubbed out
int boilSeedNow = 0;
}

void NotepadUniverse::stroke(std::vector<Mark>& out, vec2 a, vec2 b, float width, vec3 ink, float alpha, int tool,
                             float seed, float wobble, float reveal, float bow, float dash, bool world, int joints) {
    Mark m{};
    m.head[0] = world ? 10.f : 0.f;
    m.head[1] = (float)tool;
    m.head[2] = seed;
    m.head[3] = alpha;
    m.p[0] = a.x; m.p[1] = a.y; m.p[2] = b.x; m.p[3] = b.y;
    m.q[0] = width; m.q[1] = wobble; m.q[2] = reveal; m.q[3] = bow;
    m.c[0] = ink.x; m.c[1] = ink.y; m.c[2] = ink.z; m.c[3] = dash;
    m.e[0] = (float)boilSeedNow + seed * 0.37f; m.e[1] = boil_; m.e[2] = (float)joints;
    m.f[0] = eraseNow; m.f[1] = eraseSeedNow;
    out.push_back(m);
}

void NotepadUniverse::emitPolyline(const std::vector<vec2>& pts, float width, vec3 ink, float alpha, int tool,
                                   float seed, float wobble, float reveal, bool world, std::vector<Mark>& out) {
    if (pts.size() < 2) return;
    float total = 0.f;
    for (size_t i = 0; i + 1 < pts.size(); i++) total += len(sub(pts[i + 1], pts[i]));
    float drawn = total * saturate(reveal), acc = 0.f;
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        const float l = len(sub(pts[i + 1], pts[i]));
        if (acc >= drawn) break;
        const float r = l > 1e-5f ? saturate((drawn - acc) / l) : 1.f;
        const int joints = (i > 0 ? 1 : 0) | (i + 2 < pts.size() ? 2 : 0);
        stroke(out, pts[i], pts[i + 1], width, ink, alpha, tool, seed + i * 1.618f, wobble, r, 0.f, 0.f, world, joints);
        acc += l;
    }
}

void NotepadUniverse::emit(const Item& it, int age, bool residue, std::vector<Mark>& out, std::vector<float>* text) {
    eraseNow = 0.f;
    switch (it.kind) {
    case IT_CELL: emitCell(it, age, residue, out); break;
    case IT_OUTLINE: emitOutline(it, age, residue, out); break;
    case IT_STROKE: {
        const float reveal = residue || it.i1 <= 0 ? 1.f : saturate((age + 1) * (0.5f + handSpeed_) / (float)it.i1);
        stroke(out, v2(it.x, it.y), v2(it.a, it.b), it.c, inkOf(it.col), it.alpha, it.tool, it.seed, wobble_, reveal,
               it.r, 0.f, it.world);
        break;
    }
    case IT_PIE: {
        const vec2 c = v2(it.x, it.y);
        const float R = it.s;
        const vec3 rimInk = inkOf(it.i0 ? it.col : outlineCol_);
        // the circle, in two strokes that overlap where they meet
        std::vector<vec2> ring;
        const float a0 = it.seed * 0.7f;
        for (int i = 0; i <= 26; i++) {
            const float a = a0 + (TAU + 0.35f) * i / 26.f;
            const float rr = R * (1.f + 0.03f * std::sin(a * 2.f + it.seed));
            ring.push_back(v2(c.x + rr * std::cos(a), c.y + rr * std::sin(a)));
        }
        emitPolyline(ring, VARIANTS[variant_].outlineW * 0.8f * weight_, rimInk, it.i0 ? 0.95f : 0.6f,
                     VARIANTS[variant_].light ? (it.i0 ? VARIANTS[variant_].outlineTool : T_PENCIL) : T_CHALK, it.seed,
                     wobble_ * 0.5f, 1.f, false, out);
        if (it.a > 0.001f) {
            Mark m{};
            m.head[0] = 2.f;
            m.head[1] = (float)it.tool;
            m.head[2] = it.seed;
            m.head[3] = it.i0 ? 0.85f : 0.6f;
            m.p[0] = c.x; m.p[1] = c.y; m.p[2] = R * 0.93f; m.p[3] = R * 0.93f;
            m.q[0] = it.i0 ? (float)P_BANDS : (float)P_HATCH;
            m.q[1] = 0.8f;
            m.q[2] = it.i0 ? 0.2f : 0.11f;
            m.q[3] = 0.05f;
            const vec3 ink = inkOf(it.col);
            m.c[0] = ink.x; m.c[1] = ink.y; m.c[2] = ink.z;
            m.e[0] = 1.f; m.e[1] = it.i0 ? 1.f : 0.8f; m.e[2] = it.a;
            out.push_back(m);
        }
        break;
    }
    case IT_DOODLE: {
        const Shape& S = shapes()[std::clamp(it.i0, 0, D_COUNT - 1)];
        int segs = 0;
        for (const Poly& p : S.lines) segs += (int)p.size() - 1;
        const float drawn = residue ? 1e9f : (age + 1) * std::max(0.12f, it.c) * segs * (0.6f + 0.8f * handSpeed_);
        float acc = 0.f;
        Rng r(splitmix64((uint64_t)(it.seed * 1000.f) + 17));
        const float jit = 0.035f * it.s;
        const float w = (it.tool == T_MARKER ? 0.09f : it.tool == T_CHALK ? 0.085f : 0.055f) * weight_ * std::sqrt(it.s);
        const vec3 ink = inkOf(it.col);
        const float alpha = it.tool == T_HIGHLIGHT ? 0.6f : 0.92f;
        for (size_t li = 0; li < S.lines.size(); li++) {
            const Poly& poly = S.lines[li];
            std::vector<vec2> pts;
            for (const vec2& q : poly) {
                const vec2 jq = v2(q.x + r.range(-1.f, 1.f) * jit / it.s, q.y + r.range(-1.f, 1.f) * jit / it.s);
                pts.push_back(add(v2(it.x, it.y), mul(rot(jq, it.r), it.s)));
            }
            const int n = (int)poly.size() - 1;
            const float reveal = n > 0 ? saturate((drawn - acc) / n) : 1.f;
            if (reveal > 0.f) emitPolyline(pts, w, ink, alpha, it.tool, it.seed + li * 3.1f, wobble_ * 0.6f, reveal, true, out);
            acc += n;
        }
        // coloured in once the outline is done
        if (it.i1 && S.fillable && (residue || drawn >= segs)) {
            const float fillDrawn = 1.f; // coloured in at once when the outline is closed
            const float sp = (it.tool == T_MARKER ? 0.16f : 0.11f) * std::sqrt(it.s);
            Poly poly;
            for (const vec2& q : S.lines[0]) poly.push_back(add(v2(it.x, it.y), mul(rot(q, it.r), it.s * 0.94f)));
            const auto lines = hatchPolygon(poly, 0.6f + it.seed * 0.1f, sp);
            const size_t shown = (size_t)std::ceil(lines.size() * fillDrawn);
            for (size_t k = 0; k < shown && k < lines.size(); k++) {
                // a back-and-forth hand: the hatching overshoots a little
                vec2 a = lines[k].first, b = lines[k].second;
                const vec2 d = sub(b, a);
                const float l = len(d);
                if (l < 1e-4f) continue;
                const vec2 u = mul(d, 1.f / l);
                a = sub(a, mul(u, r.range(-0.04f, 0.08f)));
                b = add(b, mul(u, r.range(-0.04f, 0.08f)));
                stroke(out, a, b, w * (it.tool == T_MARKER ? 1.5f : 0.8f), ink, alpha * 0.75f, it.tool, it.seed + k * 0.77f,
                       wobble_ * 0.4f, 1.f, 0.f, 0.f, true);
            }
        }
        break;
    }
    case IT_SCRIBBLE: {
        // a furious zigzag over the stack
        Rng r(splitmix64((uint64_t)(it.seed * 1000.f) + 5));
        std::vector<vec2> pts;
        const int n = 9;
        for (int i = 0; i <= n; i++) {
            const float t = i / (float)n - 0.5f;
            const vec2 q = v2(t * it.s * 2.f, (i % 2 ? 0.45f : -0.45f) + r.range(-0.15f, 0.15f));
            pts.push_back(add(v2(it.x, it.y), rot(q, it.r)));
        }
        const float reveal = residue ? 1.f : saturate((age + 1) * 0.6f);
        emitPolyline(pts, 0.06f * weight_, inkOf(it.col), it.alpha, it.tool, it.seed, wobble_, reveal, false, out);
        break;
    }
    case IT_MOVER: emitMover(it, out); break;
    case IT_TEXT:
        if (text) emitText(it, residue, *text);
        break;
    default: break;
    }
}

// A coloured-in square (or, while being rubbed out, a square with its own outline).
void NotepadUniverse::emitCell(const Item& it, int age, bool residue, std::vector<Mark>& out) {
    const Variant& V = VARIANTS[variant_];
    const int type = std::clamp(it.i0, 0, 6);
    const float s = it.s, h = 0.5f * s;
    const float inset = 0.075f * s;
    (void)age;
    // a landed piece is coloured in over a few drawings (the hatching sweeps across); everything else at once
    const float reveal = (it.i2 & 32) ? saturate(it.b) : 1.f;
    eraseNow = residue ? 0.f : it.a;
    eraseSeedNow = it.seed * 3.1f;
    const float calm = stage_ == 0 ? 1.06f : stage_ == 3 ? 0.9f : 1.f;
    Mark m{};
    m.head[0] = 1.f;
    m.head[1] = (float)it.tool;
    m.head[2] = it.seed;
    m.head[3] = it.c * it.alpha * (residue ? 0.6f : 1.f);
    m.p[0] = it.x; m.p[1] = it.y; m.p[2] = h; m.p[3] = h;
    m.q[0] = (float)V.pattern[type];
    m.q[1] = V.angle[type] * PI / 180.f + (std::fmod(it.d, 1.f) - 0.5f) * 0.3f;
    m.q[2] = V.spacing[type] * s * calm;
    m.q[3] = 0.075f * s;
    const vec3 ink = inkOf(it.col);
    m.c[0] = ink.x; m.c[1] = ink.y; m.c[2] = ink.z; m.c[3] = (float)it.i1;
    m.e[0] = std::max(reveal, 0.001f); m.e[1] = V.fillLw * (0.85f + 0.15f * weight_); m.e[2] = inset * 0.6f;
    m.e[3] = V.fillTool == T_CPENCIL ? 0.5f : V.light ? 0.f : 0.24f; // the colour rubbed in under the hatching
    m.f[0] = eraseNow; m.f[1] = eraseSeedNow;
    if (reveal > 0.f) out.push_back(m);
    if (it.i2 & 8) {
        // its outline, side by side
        Rng r(splitmix64((uint64_t)(it.seed * 997.f)));
        const vec2 c = v2(it.x, it.y);
        const float e = h - inset;
        const vec2 corners[4] = {v2(c.x - e, c.y - e), v2(c.x + e, c.y - e), v2(c.x + e, c.y + e), v2(c.x - e, c.y + e)};
        const int sideBit[4] = {4, 2, 8, 1}; // bottom, right, top, left
        for (int sd = 0; sd < 4; sd++) {
            if (it.i1 & sideBit[sd]) continue;
            vec2 a = corners[sd], b = corners[(sd + 1) % 4];
            const vec2 d = mul(sub(b, a), 1.f / len(sub(b, a)));
            a = sub(a, mul(d, r.range(-0.01f, 0.07f)));
            b = add(b, mul(d, r.range(-0.01f, 0.07f)));
            stroke(out, a, b, V.outlineW * weight_, inkOf(outlineCol_), it.alpha, outlineTool_, it.seed + sd, wobble_);
        }
    }
    eraseNow = 0.f;
}

// A piece's outline: the edges of its cells that face outside, joined into long lines, drawn inside the squares
// (inset) with the pen overshooting the corners.
void NotepadUniverse::emitOutline(const Item& it, int age, bool residue, std::vector<Mark>& out) {
    const Variant& V = VARIANTS[variant_];
    const int n = std::clamp(it.n, 1, 4);
    int ci[4], cj[4]; // grid cells, j up
    for (int k = 0; k < n; k++) {
        ci[k] = (int)std::lround(it.pts[k * 2]);
        cj[k] = -(int)std::lround(it.pts[k * 2 + 1]);
    }
    auto has = [&](int i, int j) {
        for (int k = 0; k < n; k++) if (ci[k] == i && cj[k] == j) return true;
        return false;
    };
    // unit edges: (start corner, direction, outward normal); corners in grid units (cell (i, j) spans i..i+1)
    struct Edge { int x, y, dx, dy, nx, ny; };
    std::vector<Edge> edges;
    for (int k = 0; k < n; k++) {
        const int i = ci[k], j = cj[k];
        if (!has(i, j + 1)) edges.push_back({i, j + 1, 1, 0, 0, 1});
        if (!has(i, j - 1)) edges.push_back({i, j, 1, 0, 0, -1});
        if (!has(i - 1, j)) edges.push_back({i, j, 0, 1, -1, 0});
        if (!has(i + 1, j)) edges.push_back({i + 1, j, 0, 1, 1, 0});
    }
    // merge collinear neighbours
    struct Run { int x, y, dx, dy, nx, ny, length; };
    std::vector<Run> runs;
    std::vector<bool> used(edges.size(), false);
    for (size_t a = 0; a < edges.size(); a++) {
        if (used[a]) continue;
        used[a] = true;
        Run r{edges[a].x, edges[a].y, edges[a].dx, edges[a].dy, edges[a].nx, edges[a].ny, 1};
        bool grew = true;
        while (grew) {
            grew = false;
            for (size_t b = 0; b < edges.size(); b++) {
                if (used[b]) continue;
                const Edge& e = edges[b];
                if (e.dx != r.dx || e.dy != r.dy || e.nx != r.nx || e.ny != r.ny) continue;
                if (e.x == r.x + r.dx * r.length && e.y == r.y + r.dy * r.length) { r.length++; used[b] = true; grew = true; }
                else if (e.x == r.x - r.dx && e.y == r.y - r.dy) { r.x = e.x; r.y = e.y; r.length++; used[b] = true; grew = true; }
            }
        }
        runs.push_back(r);
    }
    // the drawing order: top first, then around (a hand goes round a shape)
    std::sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) {
        auto rank = [](const Run& r) { return r.ny > 0 ? 0 : r.nx > 0 ? 1 : r.ny < 0 ? 2 : 3; };
        return rank(a) != rank(b) ? rank(a) < rank(b) : (a.x + a.y) < (b.x + b.y);
    });
    const float s = it.s, inset = 0.075f * s;
    const bool ghost = it.i1 == 1, scribble = it.i1 == 2;
    Rng r(splitmix64((uint64_t)(it.seed * 1000.f) + 3));
    (void)age; // the whole outline in one drawing, with its colouring
    const float drawnRuns = (float)runs.size();
    auto toWorld = [&](float gx, float gy) { return v2(it.x + (gx - 0.5f) * s, it.y + (gy - 0.5f) * s); };
    const vec3 ink = inkOf(it.col);
    const float width = V.outlineW * it.b * (ghost ? 0.8f : scribble ? (it.tool == T_MARKER ? 1.7f : 1.9f) : 1.f) * (0.75f + 0.25f * s);
    for (size_t k = 0; k < runs.size(); k++) {
        const float reveal = saturate(drawnRuns - (float)k);
        if (reveal <= 0.f) break;
        const Run& q = runs[k];
        const float D[2] = {(float)q.dx, (float)q.dy}, N[2] = {(float)q.nx, (float)q.ny};
        // concave ends (the outline turns outward) reach further, convex ones stop short, both then overshoot
        auto cellAt = [&](float gx, float gy) { return has((int)std::floor(gx), (int)std::floor(gy)); };
        const float sx = (float)q.x, sy = (float)q.y, ex = sx + D[0] * q.length, ey = sy + D[1] * q.length;
        const bool concave0 = cellAt(sx - 0.5f * D[0] + 0.5f * N[0], sy - 0.5f * D[1] + 0.5f * N[1]);
        const bool concave1 = cellAt(ex + 0.5f * D[0] + 0.5f * N[0], ey + 0.5f * D[1] + 0.5f * N[1]);
        const float i0 = (concave0 ? -inset : inset) / s, i1 = (concave1 ? inset : -inset) / s;
        const float ovMax = scribble ? 0.24f : 0.14f; // a scribble overshoots more
        const float ov0 = (concave0 ? r.range(-0.03f, 0.04f) : r.range(0.f, ovMax)) / s;
        const float ov1 = (concave1 ? r.range(-0.03f, 0.04f) : r.range(0.f, ovMax)) / s;
        const float off = -inset / s;
        vec2 a = toWorld(sx + D[0] * (i0 - ov0) + N[0] * off, sy + D[1] * (i0 - ov0) + N[1] * off);
        vec2 b = toWorld(ex + D[0] * (i1 + ov1) + N[0] * off, ey + D[1] * (i1 + ov1) + N[1] * off);
        const float bow = r.range(-0.025f, 0.025f) * (scribble ? 2.2f : 1.f) * std::min(2.f, (float)q.length);
        stroke(out, a, b, width, ink, it.alpha, it.tool, it.seed + k * 2.3f, wobble_ * (ghost ? 0.6f : scribble ? 1.8f : 1.f), reveal, bow,
               ghost ? 0.17f * s : 0.f, false);
    }
    // the falling scribble: a fast zigzag through each square, never quite inside it
    if (scribble)
        for (int k = 0; k < n; k++) {
            std::vector<vec2> zz;
            // back and forth across the square's diagonal, loosely, a little over its edges
            const int steps = r.irange(4, 6);
            const float sgn = r.chance(0.5f) ? 1.f : -1.f;
            for (int t = 0; t <= steps; t++) {
                const float along = (t / (float)steps - 0.5f) * 0.95f + r.range(-0.07f, 0.07f);
                const float across = ((t % 2) ? 0.36f : -0.36f) + r.range(-0.1f, 0.08f);
                const float gx = 0.5f + (along + across) * 0.7071f, gy = 0.5f + sgn * (along - across) * 0.7071f;
                zz.push_back(toWorld(ci[k] + gx, cj[k] + gy));
            }
            emitPolyline(zz, width * 0.9f, ink, it.alpha * 0.95f, it.tool, it.seed + 41.f + k * 5.3f, wobble_ * 1.4f, 1.f, false, out);
        }
    // the ghost's light hatching: three diagonals a cell, all the same way (not a coloured-in piece)
    if (ghost)
        for (int k = 0; k < n; k++)
            for (int h = -1; h <= 1; h++) {
                const float u = h * 0.3f;
                const vec2 a = toWorld(ci[k] + 0.17f + std::max(0.f, u), cj[k] + 0.17f + std::max(0.f, -u));
                const vec2 b = toWorld(ci[k] + 0.83f - std::max(0.f, -u), cj[k] + 0.83f - std::max(0.f, u));
                stroke(out, a, b, width * 0.8f, ink, it.alpha * (V.light ? 0.7f : 0.55f), it.tool, it.seed + 31.f + k * 3.7f + h,
                       wobble_ * 0.5f, 1.f, 0.f, 0.f, false);
            }
}

void NotepadUniverse::emitText(const Item& it, bool residue, std::vector<float>& out) {
    if (!font_.ok) return;
    const stbtt_fontinfo* info = (const stbtt_fontinfo*)font_.info;
    const float k = it.s / font_.px;
    const float baseline = it.y + font_.capH * k;
    const std::vector<int> sl = slots(it.text);
    Rng r(splitmix64((uint64_t)(it.seed * 1000.f) + 11));
    Rng boil(splitmix64((uint64_t)(it.seed * 1000.f) + 101 + (uint64_t)boilSeedNow * 7));
    const float boilPx = boil_ / px_ * 0.9f;
    vec2 smear = v2(0, 0);
    if (residue) { const float a = r.range(0.f, TAU); smear = v2(std::cos(a) * 1.2f, std::sin(a) * 0.6f); }
    const vec3 ink = inkOf(it.col);
    float pen = it.x;
    for (size_t i = 0; i < sl.size(); i++) {
        const Glyph& g = font_.g[sl[i]];
        // the hand: each letter a little off its line, a little different on every drawing
        const float jx = r.range(-1.f, 1.f) * it.s * 0.012f + boil.range(-1.f, 1.f) * boilPx;
        const float jy = r.range(-1.f, 1.f) * it.s * 0.03f + boil.range(-1.f, 1.f) * boilPx;
        if (g.w > 0) {
            const float x0 = pen + g.ox * k + jx + smear.x, y0 = baseline + g.oy * k + jy + smear.y;
            const float x1 = x0 + g.w * k, y1 = y0 + g.h * k;
            const float v[6][4] = {{x0, y0, g.x0, g.y0}, {x1, y0, g.x1, g.y0}, {x1, y1, g.x1, g.y1},
                                   {x0, y0, g.x0, g.y0}, {x1, y1, g.x1, g.y1}, {x0, y1, g.x0, g.y1}};
            for (auto& q : v) {
                out.insert(out.end(), q, q + 4);
                out.push_back(ink.x); out.push_back(ink.y); out.push_back(ink.z); out.push_back(it.alpha);
            }
        }
        pen += g.adv * k;
        if (i + 1 < sl.size()) pen += stbtt_GetCodepointKernAdvance(info, slotCode(sl[i]), slotCode(sl[i + 1])) * font_.scaleTT * k;
    }
}

// =====================================================================================================
// The drop's travelling drawings
// =====================================================================================================

// The margins they move in: left of the HUD's left column, right of its right column (never the well or the HUD).
void NotepadUniverse::marginBox(int side, float& x0, float& y0, float& x1, float& y1) const {
    const float L = viewC_.x - viewH_.x, R = viewC_.x + viewH_.x, B = viewC_.y - viewH_.y, T = viewC_.y + viewH_.y;
    if (side == 0) { x0 = L + 1.f; x1 = layout::LEFT_COLUMN[0] - 0.9f; y0 = -8.4f; y1 = std::min(8.6f, T - 3.4f); }
    else { x0 = layout::RIGHT_COLUMN[2] + 0.9f; x1 = R - 1.f; y0 = B + 1.f; y1 = T - 1.f; }
}

void NotepadUniverse::spawnMover(int kind, int side) {
    if ((int)movers_.size() >= 3) return;
    const Variant& V = VARIANTS[variant_];
    float x0, y0, x1, y1;
    marginBox(side, x0, y0, x1, y1);
    if (x1 - x0 < 3.f) return;
    Mover m;
    m.kind = kind;
    m.side = side;
    m.beat0 = beatNow_;
    m.ylo = y0, m.yhi = y1;
    // two in a margin: one above the other
    for (Mover& o : movers_)
        if (o.side == side) {
            const float mid = 0.5f * (y0 + y1);
            o.ylo = mid + 0.3f;
            m.yhi = mid - 0.3f;
        }
    m.size = kind == MV_CUBE ? 1.75f : kind == MV_RUNNER ? 1.45f : 1.3f;
    m.size *= std::clamp((x1 - x0) / 8.f, 0.8f, 1.f);
    const float sg = rng_.chance(0.5f) ? 1.f : -1.f;
    switch (kind) {
    case MV_CUBE: m.start = v2(rng_.range(x0, x1), rng_.range(m.ylo, m.yhi)); m.vel = v2(sg * 0.95f, rng_.chance(0.5f) ? 0.7f : -0.7f); break;
    case MV_BALL: m.start = v2(rng_.range(x0, x1), 0.f); m.vel = v2(sg * 0.9f, 0.f); break;
    case MV_RUNNER: m.start = v2(rng_.range(x0, x1), 0.f); m.vel = v2(sg * 0.8f, 0.f); break;
    case MV_PLANE: m.vel = v2(sg, 0.f); break;
    default: m.start = v2(0.5f * (x0 + x1) + rng_.range(-1.f, 1.f), rng_.range(m.ylo, m.yhi)); m.vel = v2(0.f, 1.3f); break;
    }
    m.col = hexc(V.doodle[rng_.irange(0, 3)]);
    for (int k = 0; k < 4; k++) { // not the colour of the other ones
        bool seen = false;
        for (const Mover& o : movers_) seen |= o.col.x == m.col.x && o.col.y == m.col.y && o.col.z == m.col.z;
        if (!seen) break;
        m.col = hexc(V.doodle[(k + 1 + rng_.irange(0, 2)) % 4]);
    }
    m.tool = V.doodleTool;
    m.id = serial_++;
    movers_.push_back(m);
}

// They live through the drop: gone (rubbed out) as soon as it ends.
void NotepadUniverse::manageMovers() {
    if (stage_ < 3) { movers_.clear(); return; }
    // a drop reached without its downbeat (a review shot, a seek): bring them in anyway
    if (movers_.empty()) {
        const int side = rng_.irange(0, 1);
        spawnMover(MV_CUBE, side);
        spawnMover(rng_.irange(1, MV_COUNT - 1), 1 - side);
    }
}

void NotepadUniverse::emitMover(const Item& it, std::vector<Mark>& out) {
    const vec3 ink = inkOf(it.col);
    const float s = it.s;
    const float w = (it.tool == T_MARKER ? 0.085f : it.tool == T_CHALK ? 0.08f : 0.055f) * weight_;
    const float alpha = 0.92f * it.alpha; // (lower for the eraser's ghosts)
    const vec2 c = v2(it.x, it.y);
    Rng r(splitmix64((uint64_t)(it.seed * 1000.f) + 23));
    auto jit = [&](vec2 q) { return v2(q.x + r.range(-1.f, 1.f) * 0.025f * s, q.y + r.range(-1.f, 1.f) * 0.025f * s); };
    float salt = 0.f;
    auto line = [&](std::vector<vec2> pts, float width, float a) {
        for (vec2& q : pts) q = jit(q);
        salt += 1.37f;
        emitPolyline(pts, width, ink, a, it.tool, it.seed + salt, wobble_ * 0.6f, 1.f, true, out);
    };
    auto hatch = [&](const Poly& poly, float ang, float sp, float a) {
        int k = 0;
        for (const auto& l : hatchPolygon(poly, ang, sp)) {
            const vec2 d = sub(l.second, l.first);
            const float L = len(d);
            if (L < 1e-3f) continue;
            const vec2 u = mul(d, 1.f / L);
            stroke(out, sub(l.first, mul(u, r.range(-0.03f, 0.07f))), add(l.second, mul(u, r.range(-0.03f, 0.07f))),
                   w * (it.tool == T_MARKER ? 0.8f : 0.7f), ink, a * 0.6f, it.tool, it.seed + (k++) * 0.77f, wobble_ * 0.3f,
                   1.f, 0.f, 0.f, true);
        }
    };
    const float p = it.a;
    switch (it.i0) {
    case MV_CUBE: {
        // a wireframe cube turning in 3D, its nearest face hatched, the hidden edges lighter
        const float yaw = p * 0.75f, pitch = 0.45f + 0.25f * std::sin(p * 0.33f);
        vec3 v[8];
        vec2 q[8];
        for (int i = 0; i < 8; i++) {
            const vec3 a((i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.f : -1.f);
            const vec3 b(a.x * std::cos(yaw) + a.z * std::sin(yaw), a.y, -a.x * std::sin(yaw) + a.z * std::cos(yaw));
            const vec3 e(b.x, b.y * std::cos(pitch) - b.z * std::sin(pitch), b.y * std::sin(pitch) + b.z * std::cos(pitch));
            v[i] = e;
            const float k = 4.f / (4.f + e.z);
            q[i] = v2(c.x + e.x * k * s * 0.6f, c.y + e.y * k * s * 0.6f);
        }
        const int faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
        int front = 0;
        float best = 1e9f;
        for (int f = 0; f < 6; f++) {
            float z = 0.f;
            for (int k = 0; k < 4; k++) z += v[faces[f][k]].z;
            if (z < best) best = z, front = f;
        }
        Poly face;
        for (int k = 0; k <= 4; k++) face.push_back(q[faces[front][k % 4]]);
        hatch(face, 0.7f, 0.2f * s, alpha);
        for (int a = 0; a < 8; a++)
            for (int b = a + 1; b < 8; b++) {
                const int d = a ^ b;
                if (d != 1 && d != 2 && d != 4) continue;
                const bool back = v[a].z > 0.25f && v[b].z > 0.25f;
                line({q[a], q[b]}, w * (back ? 0.7f : 1.f), back ? alpha * 0.5f : alpha);
            }
        break;
    }
    case MV_BALL: {
        // a ball bouncing on the beat: squashed on the ground, stretched in the air
        const float h = std::fabs(std::sin(p * PI));
        const float squash = h < 0.2f ? 0.7f + 1.5f * h : 1.f + 0.15f * h;
        const float rx = 0.5f * s / std::sqrt(squash), ry = 0.5f * s * squash;
        const vec2 b = v2(c.x, c.y + ry + h * 2.4f * s);
        const float a0 = r.range(0.f, 1.f);
        const Poly ball = circle(b, rx, ry, 18, a0, a0 + TAU + 0.15f);
        hatch(ball, 0.8f, 0.14f * s, alpha);
        line(ball, w, alpha);
        line({v2(c.x - 0.75f * s, c.y), v2(c.x + 0.75f * s, c.y + 0.02f)}, w * 0.7f, alpha * 0.5f); // its ground
        if (h > 0.35f) // speed lines behind it
            for (int k = 0; k < 2; k++) {
                const float yy = b.y + (k ? -0.18f : 0.18f) * s, x0 = b.x - it.r * (rx + 0.15f + 0.12f * k);
                line({v2(x0, yy), v2(x0 - it.r * 0.4f * s, yy)}, w * 0.7f, alpha * 0.6f);
            }
        break;
    }
    case MV_RUNNER: {
        // a stick figure running, a stride per beat
        const float ph = p * PI, dir = it.r >= 0.f ? 1.f : -1.f, sw = std::sin(ph);
        const float bob = 0.1f * s * std::fabs(sw);
        const vec2 hip = v2(c.x, c.y + 0.78f * s + bob), neck = v2(c.x + dir * 0.14f * s, c.y + 1.42f * s + bob);
        const float a0 = r.range(0.f, 1.f);
        line(circle(v2(neck.x + dir * 0.06f * s, neck.y + 0.26f * s), 0.21f * s, 0.23f * s, 12, a0, a0 + TAU + 0.1f), w, alpha);
        line({hip, neck}, w, alpha);
        auto limb = [&](vec2 root, float a1, float a2, float l1, float l2) {
            const vec2 k = add(root, v2(dir * std::sin(a1) * l1, -std::cos(a1) * l1));
            const vec2 e = add(k, v2(dir * std::sin(a2) * l2, -std::cos(a2) * l2));
            line({root, k, e}, w, alpha);
        };
        limb(hip, 0.75f * sw, 0.75f * sw - 0.5f - 0.6f * std::max(0.f, -sw), 0.42f * s, 0.4f * s);
        limb(hip, -0.75f * sw, -0.75f * sw - 0.5f - 0.6f * std::max(0.f, sw), 0.42f * s, 0.4f * s);
        const vec2 sh = add(neck, v2(-dir * 0.03f * s, -0.12f * s));
        limb(sh, -0.9f * sw, -0.9f * sw + 1.3f, 0.3f * s, 0.27f * s);
        limb(sh, 0.9f * sw, 0.9f * sw + 1.3f, 0.3f * s, 0.27f * s);
        line({v2(c.x - 0.7f * s, c.y), v2(c.x + 0.7f * s, c.y - 0.02f)}, w * 0.7f, alpha * 0.45f); // the ground
        break;
    }
    case MV_PLANE: {
        // a paper plane on a loop
        auto P = [&](float x, float y) { return add(c, mul(rot(v2(x, y), it.r), s)); };
        const vec2 nose = P(0.8f, 0.f), tail = P(-0.6f, 0.f), wl = P(-0.7f, 0.62f), wr = P(-0.7f, -0.34f), keel = P(-0.4f, -0.2f);
        hatch({nose, wl, tail, nose}, 0.3f, 0.14f * s, alpha);
        line({nose, wl, tail, nose}, w, alpha);
        line({nose, wr, tail}, w, alpha);
        line({tail, keel, nose}, w * 0.8f, alpha * 0.7f);
        break;
    }
    case MV_ROCKET: {
        // a rocket going up, a scribbled flame under it (a new scribble on every drawing)
        auto P = [&](float x, float y) { return v2(c.x + x * s, c.y + y * s); };
        line({P(0.f, 1.f), P(0.3f, 0.55f), P(0.3f, -0.5f), P(-0.3f, -0.5f), P(-0.3f, 0.55f), P(0.f, 1.f)}, w, alpha);
        line(circle(P(0.f, 0.22f), 0.13f * s, 0.13f * s, 10), w * 0.8f, alpha);
        line({P(0.3f, -0.12f), P(0.56f, -0.66f), P(0.3f, -0.5f)}, w, alpha);
        line({P(-0.3f, -0.12f), P(-0.56f, -0.66f), P(-0.3f, -0.5f)}, w, alpha);
        std::vector<vec2> flame;
        for (int k = 0; k <= 7; k++)
            flame.push_back(P((k % 2 ? 0.22f : -0.22f) * (1.f - k / 9.f), -0.58f - k * 0.11f - r.range(0.f, 0.09f)));
        emitPolyline(flame, w, inkOf(hexc(VARIANTS[variant_].accent)), alpha, it.tool, it.seed + 7.f, wobble_ * 0.6f, 1.f, true, out);
        break;
    }
    default: break;
    }
}


// =====================================================================================================
// The stop-motion clock and the drawings
// =====================================================================================================

namespace {
bool sameItem(const NotepadUniverse::Item& a, const NotepadUniverse::Item& b) {
    if (a.kind != b.kind || a.i0 != b.i0 || a.i1 != b.i1 || a.n != b.n || a.tool != b.tool || a.text != b.text) return false;
    // a cell being coloured in (its `b`, how far) keeps its hand: only the new strokes are added
    const bool colouring = a.kind == 0 && ((a.i2 | b.i2) & 32); // 0: a cell
    const float fa[] = {a.x, a.y, a.s, a.r, colouring ? 0.f : a.b, a.c, a.col.x, a.col.y, a.col.z, a.alpha};
    const float fb[] = {b.x, b.y, b.s, b.r, colouring ? 0.f : b.b, b.c, b.col.x, b.col.y, b.col.z, b.alpha};
    for (size_t i = 0; i < sizeof(fa) / sizeof(fa[0]); i++) if (std::fabs(fa[i] - fb[i]) > 1e-4f) return false;
    // a cell's `a` is how far it is rubbed out: that goes on with the same drawing of it
    if (a.kind != 0 && std::fabs(a.a - b.a) > 1e-4f) return false;
    for (int k = 0; k < 8; k++) if (a.pts[k] != b.pts[k]) return false;
    return a.d == b.d;
}
bool movedItem(const NotepadUniverse::Item& a, const NotepadUniverse::Item& b) {
    return std::fabs(a.x - b.x) + std::fabs(a.y - b.y) > 0.01f;
}
} // namespace

void NotepadUniverse::tick(const Frame& f, float since) {
    drawing_++;
    lastTick_ = clock_;
    dirty_ = false;
    boilSeedNow = 0;
    style(f.music);
    beatNow_ = f.music.hasSong ? f.music.beat : clock_ * 2.0;
    landDrawn_ = landProgress();
    manageDoodles();
    manageMovers();
    if (levelUpPending_) {
        levelUpPending_ = false;
        spawnDoodle(stage_ >= 2);
    }

    std::vector<Item> next;
    next.reserve(items_.size() + 16);
    buildDrawing(f, next);
    std::unordered_map<uint64_t, size_t> nextIndex;
    nextIndex.reserve(next.size() * 2);
    for (size_t i = 0; i < next.size(); i++) nextIndex[next[i].key] = i;

    // ---- the eraser: over the old ghosts again, then over what moved or disappeared
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    residueT_.bind();
    if (clearPage_) {
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        clearPage_ = false;
    } else if (since > 0.f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(progFade_);
        set1f(progFade_, "uK", 1.f - std::exp(-since * RESIDUE_FADE));
        set1f(progFade_, "uSeed", (float)(drawing_ % 997) * 1.37f);
        set4f(progFade_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        glBindVertexArray(emptyVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    {
        std::vector<Mark> ghost;
        std::vector<float> ghostText;
        // squares the new drawing fills: a ghost under them is lighter (the hand erased around them)
        auto covered = [&](const Item& o) {
            for (const Item& n : next)
                if (n.kind == IT_CELL && std::fabs(n.x - o.x) < 0.3f && std::fabs(n.y - o.y) < 0.3f) return true;
            return false;
        };
        for (const Item& o : items_) {
            auto itn = nextIndex.find(o.key);
            float strength = 0.f;
            if (itn == nextIndex.end()) strength = o.residue;
            else if (movedItem(o, next[itn->second])) strength = o.moveResidue * (o.kind == IT_CELL && covered(o) ? 0.3f : 1.f);
            if (!VARIANTS[variant_].light) strength *= 0.65f; // chalk dust shows more than graphite
            if (strength <= 0.001f) continue;
            Item gi = o;
            gi.alpha *= strength;
            gi.c *= 1.f;
            emit(gi, 99, true, ghost, &ghostText);
        }
        if (!ghost.empty() || !ghostText.empty()) {
            const float a = rng_.range(0.f, TAU);
            drawMarks(ghost, true, v2(std::cos(a) * 0.14f, std::sin(a) * 0.05f));
            drawText(ghostText, true);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ---- the new drawing: what stayed keeps its exact hand, what changed is drawn anew
    for (Item& n : next) {
        auto ito = index_.find(n.key);
        if (ito == index_.end()) {
            n.born = drawing_;
            n.seed = rng_.range(0.f, 1000.f);
        } else {
            const Item& o = items_[ito->second];
            n.born = o.born;
            n.seed = sameItem(o, n) ? o.seed : rng_.range(0.f, 1000.f);
        }
    }
    items_.swap(next);
    index_.swap(nextIndex);
    marks_.clear();
    textVerts_.clear();
    for (const Item& it : items_) emit(it, drawing_ - it.born, false, marks_, &textVerts_);
    glBindBuffer(GL_ARRAY_BUFFER, markVbo_);
    glBufferData(GL_ARRAY_BUFFER, marks_.size() * sizeof(Mark), marks_.data(), GL_DYNAMIC_DRAW);
    markCount_ = marks_.size();
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, textVerts_.size() * sizeof(float), textVerts_.data(), GL_DYNAMIC_DRAW);
    textCount_ = textVerts_.size() / 8;
}

void NotepadUniverse::drawMarks(const std::vector<Mark>& m, bool residue, vec2 smear) {
    glUseProgram(progMark_);
    setMat(progMark_, "uVP", vp_);
    set1f(progMark_, "uPx", px_);
    set1f(progMark_, "uResidue", residue ? 1.f : 0.f);
    set2f(progMark_, "uSmear", smear.x, smear.y);
    set1f(progMark_, "uSmearLen", residue ? len(smear) * 1.3f + 0.05f : 0.f);
    set1f(progMark_, "uWorldFade", 1.f - saturate(bridge_.amount * 1.6f));
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ONE, GL_ONE);
    glBindVertexArray(markVao_);
    if (residue) {
        glBindBuffer(GL_ARRAY_BUFFER, markVbo_);
        glBufferData(GL_ARRAY_BUFFER, m.size() * sizeof(Mark), m.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)m.size());
    } else if (markCount_ > 0) {
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)markCount_);
    }
}

void NotepadUniverse::drawText(const std::vector<float>& v, bool residue) {
    if (!font_.ok) return;
    const size_t count = residue ? v.size() / 8 : textCount_;
    if (count == 0) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    set1f(progText_, "uResidue", residue ? 1.f : 0.f);
    set1f(progText_, "uBias", 0.02f + 0.02f * weight_);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ONE, GL_ONE);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    if (residue) glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)count);
}

void NotepadUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    const Variant& V = VARIANTS[variant_];
    pauseFade_ = approach(pauseFade_, f.paused ? 1.f : (f.dim ? 0.6f : 0.f), f.paused ? 4.f : 1.5f, dt);
    if (!f.paused) {
        clock_ += dt;
        kick_ = std::max(approach(kick_, 0.f, 4.f, dt), music.kick);
        slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 3.f, dt);
        dropHeld_ = std::max(0.f, dropHeld_ - dt);
        for (Dying& d : dying_) d.t += dt; // real time, even in slow motion
        dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return d.t > d.dur + 0.2f; }), dying_.end());
        transients_.erase(std::remove_if(transients_.begin(), transients_.end(),
                                         [&](const Transient& t) { return clock_ > t.until + 0.5f; }), transients_.end());
        if (overT_ >= 0.f) {
            overT_ += dt;
            if (overT_ > 2.6f && !game.over()) overT_ = -1.f;
            if (overT_ > 3.f && game.over()) dead_.clear();
        }
        // the stage
        const bool peak = level_ >= 2;
        drive_ = approach(drive_, peak ? smoothstepf(1.27f, 1.42f, music.hasSong ? music.speed : 1.4f) : 0.f, 1.5f, dt);
        if (peak && drive_ > 0.6f) dropOn_ = true;
        int st = level_ == 0 ? 0 : level_ == 1 ? 1 : (dropOn_ || dropHeld_ > 0.f) ? 3 : 2;
        if (st != stage_) { stage_ = st; dirty_ = true; }
        // beats
        if (music.hasSong) {
            const long b = (long)std::floor(music.beat);
            if (b != lastBeatI_) { lastBeatI_ = b; beatsSeen_++; }
        }
        // what changed in the game
        {
            uint64_t s = 1469598103934665603ull;
            auto h = [&](int64_t v) { s = (s ^ (uint64_t)v) * 1099511628211ull; };
            h(game.hasPiece());
            if (game.hasPiece()) { const Piece& p = game.piece(); h(p.type); h(p.x); h(p.y); h(p.rot); h(game.ghostY()); }
            h(game.holdType()); h(game.holdUsed()); h(game.next(0)); h(game.next(1)); h(game.next(2));
            for (int y = 0; y < Game::H; y++) {
                h(std::lround(game.rowOffset(y)));
                for (int x = 0; x < Game::W; x++) h(game.cell(x, y).type);
            }
            h(game.over());
            if (s != sig_) { sig_ = s; dirty_ = true; }
        }
        // when to draw: on the beat grid (on twos at a break, on sixteenths in a drop), at once when the game
        // changed (the piece answers at once), quickly while rows are rubbed out
        const float rate = RATE[stage_] * (1.f - 0.5f * slow_);
        const double since = lastTick_ < 0 ? 1.0 : clock_ - lastTick_;
        bool due = lastTick_ < 0;
        if (music.hasSong && music.bpm > 1.f) {
            const int sub = std::max(1, (int)std::lround(rate * 60.f / music.bpm));
            const long slot = (long)std::floor(music.beat * sub);
            if (slot != beatSlot_) {
                beatSlot_ = slot;
                if (since > 0.55 / rate) due = true;
            }
            if (since > 2.5 / rate) due = true;
        } else if (since >= 1.0 / rate) {
            due = true;
        }
        if (dirty_ && since >= 1.0 / 24.0) due = true;
        if (!dying_.empty() && since >= 1.0 / 16.0) due = true;
        if (landDrawn_ < 1.f && since >= 1.0 / 11.0) due = true; // the colouring, stroke by stroke
        if (bridge_.amount > 0.f && since >= 1.0 / 12.0) due = true;
        if (due) tick(f, (float)std::min(since, 0.5));
    } else if (lastTick_ < 0) {
        tick(f, 0.f);
    }

    // ---- the page: the paper, the residue, the drawing, the words
    sceneT_.bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    const float world = 1.f - saturate(bridge_.amount * 1.4f);
    glUseProgram(progPaper_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, residueT_.tex);
    set1i(progPaper_, "uResidue", 0);
    set4f(progPaper_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set1f(progPaper_, "uPx", px_);
    set3f(progPaper_, "uPaper", hexc(V.paper));
    set3f(progPaper_, "uGrid", hexc(V.grid));
    set3f(progPaper_, "uMargin", hexc(V.margin));
    set4f(progPaper_, "uGridP", V.gridA, (float)V.major, V.majorK, std::max(1.f, 0.028f / px_));
    set4f(progPaper_, "uMarginP", marginX_, (float)V.marginN, V.marginA, 0.f);
    set1f(progPaper_, "uLight", V.light ? 1.f : 0.f);
    set1f(progPaper_, "uWorld", world);
    set1f(progPaper_, "uGrain", V.grain);
    set1f(progPaper_, "uAspect", (float)width_ / height_);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    drawMarks(marks_, false, v2(0, 0));
    drawText(textVerts_, false);
    glDisable(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progPost_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneT_.tex);
    set1i(progPost_, "uScene", 0);
    set1f(progPost_, "uLight", V.light ? 1.f : 0.f);
    set1f(progPost_, "uFade", saturate(f.fade));
    set1f(progPost_, "uDim", pauseFade_);
    set1f(progPost_, "uBridge", smoothstepf(0.f, 1.f, bridge_.amount));
    set3f(progPost_, "uBridgeCol", bridge_.color);
    set3f(progPost_, "uBridgeInk", bridge_.ink);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// =====================================================================================================
// Registry
// =====================================================================================================

const UniverseEntry& universe_notepad() {
    static const UniverseEntry e{
        "NOTEPAD", "a squared notebook drawn by hand in stop motion",
        [] { return std::unique_ptr<Universe>(new NotepadUniverse()); },
        // anything can be doodled; busy, punchy songs suit the frantic hand a little better
        [](const SongLook& l) { return 0.85f + 0.3f * l.density; },
        [] {
            std::vector<std::string> v;
            for (const Variant& x : VARIANTS) v.push_back(x.name);
            return v;
        }()};
    return e;
}
