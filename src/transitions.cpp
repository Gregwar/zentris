#include "transitions.hpp"

#include <cctype>
#include <cstdio>
#include <string>

#include "ugl.hpp"

namespace transitions {

namespace {

struct Info {
    const char* name;
    float weight;       // base weight
    float depth;        // bridge depth in the middle
    const char* glsl;   // vec3 compose(vec2 px): the composite (linear RGB) at pixel px
};

// Shared by every transition: inputs, sampling with the host bridge, board geometry, noise, the
// luminance-aware mix and the front (seam) styles.
const char* PRELUDE = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uA, uB, uCells;
uniform vec2 uRes;
uniform vec4 uBoard;            // the well: x0, y0, x1, y1 (pixels, bottom-left origin)
uniform float uP, uBeats, uKick, uBeatPhase, uTime, uSeed;
uniform vec3 uBgA, uBgB, uInkA, uInkB, uAccA, uAccB;
uniform float uLightA, uLightB;
uniform vec3 uBridgeColorA, uBridgeInkA, uBridgeColorB, uBridgeInkB;
uniform float uHostBridgeA, uHostBridgeB;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { s += a * vnoise(p); p = p * 2.03 + 17.1; a *= 0.5; }
    return s;
}
vec2 boardSize() { return uBoard.zw - uBoard.xy; }
vec2 boardCenter() { return (uBoard.xy + uBoard.zw) * 0.5; }
vec2 cellSize() { return boardSize() / vec2(10.0, 20.0); }
// 0..1 inside the well (can go beyond outside it).
vec2 boardUv(vec2 px) { return (px - uBoard.xy) / boardSize(); }
// Distance from the well's rectangle, in screen heights (0 inside).
float boardDist(vec2 px) {
    vec2 d = max(max(uBoard.xy - px, px - uBoard.zw), 0.0);
    return length(d) / uRes.y;
}
// The farthest screen corner from the well, in screen heights.
float maxBoardDist() { return max(boardDist(vec2(0.0)), boardDist(vec2(uRes.x, 0.0))) + 0.001; }

// The bridge drawn by the host: the well and the stack, plain ink on the bridge color.
vec3 bridgeImage(vec2 px, vec3 color, vec3 ink) {
    vec2 c = boardUv(px) * vec2(10.0, 20.0);
    vec3 col = color;
    float px1 = 1.5 / cellSize().x;
    if (c.x >= 0.0 && c.x < 10.0 && c.y >= 0.0 && c.y < 20.0) {
        float v = texelFetch(uCells, ivec2(c), 0).r;
        vec2 f = abs(fract(c) - 0.5);
        float inCell = step(max(f.x, f.y), 0.44);
        float rim = step(0.36, max(f.x, f.y));
        col = mix(col, ink, v * inCell * mix(0.55, 1.0, rim));
    }
    // The well: an open box (sides and floor).
    float side = min(abs(c.x), abs(c.x - 10.0));
    float well = (step(side, px1 * 1.2) * step(0.0, c.y) * step(c.y, 20.0)) +
                 step(abs(c.y), px1 * 0.6 * cellSize().x / cellSize().y) * step(0.0, c.x) * step(c.x, 10.0);
    return mix(col, ink, clamp(well, 0.0, 1.0) * 0.8);
}
vec3 lin(vec3 c) { return pow(max(c, 0.0), vec3(2.2)); }
vec3 sampleA(vec2 px) {
    vec3 c = lin(texture(uA, px / uRes).rgb);
    return uHostBridgeA > 0.0 ? mix(c, bridgeImage(px, uBridgeColorA, uBridgeInkA), uHostBridgeA) : c;
}
vec3 sampleB(vec2 px) {
    vec3 c = lin(texture(uB, px / uRes).rgb);
    return uHostBridgeB > 0.0 ? mix(c, bridgeImage(px, uBridgeColorB, uBridgeInkB), uHostBridgeB) : c;
}
// A -> B by m, luminance-aware: between a dark and a light universe the blend never passes through gray mush:
// toward a lighter one, light blooms in first (max), toward a darker one, ink spreads first (min).
vec3 bridgeMix(vec3 a, vec3 b, float m) {
    float gap = clamp(abs(luma(uBgB) - luma(uBgA)) * 2.5, 0.0, 1.0);
    vec3 mid = mix(mix(a, b, 0.5), luma(uBgB) > luma(uBgA) ? max(a, b) : min(a, b), gap);
    return m < 0.5 ? mix(a, mid, m * 2.0) : mix(mid, b, m * 2.0 - 1.0);
}
// The front: a glow in the incoming universe's accent when it's dark, an ink line when it's light.
vec3 front(vec3 col, float s) {
    s = clamp(s, 0.0, 1.5);
    vec3 glow = mix(uAccB, uAccA, 0.3) * (0.55 + 0.45 * uKick) + vec3(0.12) * s;
    vec3 lit = col + glow * s;
    vec3 inked = mix(col, uInkB, clamp(s, 0.0, 1.0) * 0.45);
    return mix(lit, inked, uLightB);
}
// Progress 0..1 remapped so a soft switch of half width `soft` around T starts all A and ends all B.
float switchAt(float T, float soft) {
    float p = uP * (1.0 + 2.0 * soft) - soft;
    return smoothstep(T - soft, T + soft, p);
}
float frontAt(float T, float width) {
    float d = (uP - T) / width;
    return exp(-d * d);
}
vec3 compose(vec2 px);
void main() {
    vec3 c = compose(gl_FragCoord.xy);
    frag = vec4(pow(max(c, 0.0), vec3(1.0 / 2.2)), 1.0);
}
)";

const Info INFOS[COUNT] = {
    // REBUILD: the stack re-materializes row by row from the bottom (a scan beam on the row being rebuilt), then
    // the world follows, outward from the well.
    {"REBUILD", 1.2f, 0.85f, R"(
vec3 compose(vec2 px) {
    vec2 b = boardUv(px);
    bool inBoard = b.x >= -0.02 && b.x <= 1.02 && b.y >= -0.01 && b.y <= 1.0;
    float rowsEnd = 0.5;
    float scan = uP / rowsEnd * 20.0;             // rows rebuilt so far (from the bottom)
    vec3 a = sampleA(px), bb = sampleB(px);
    if (inBoard) {
        float row = floor(clamp(b.y, 0.0, 0.999) * 20.0);
        float m = clamp(scan - row, 0.0, 1.0);
        m = smoothstep(0.0, 1.0, m);
        vec3 c = bridgeMix(a, bb, m);
        // The beam on the row being rebuilt, overshooting the well a little.
        float y = b.y * 20.0;
        float d = (y - min(scan, 20.0)) * cellSize().y;
        float beam = exp(-d * d / 18.0) * step(scan, 20.5) * step(0.0, uP);
        return front(c, beam * 1.2);
    }
    float T = rowsEnd - 0.05 + (1.0 - rowsEnd + 0.03) * clamp(boardDist(px) / maxBoardDist(), 0.0, 1.0);
    T += 0.03 * (fbm(px / uRes.y * 4.0 + uSeed) - 0.5);
    float m = switchAt(T, 0.015);
    return front(bridgeMix(a, bb, m), frontAt(T, 0.012) * 0.8);
}
)"},
    // RIPPLE: a refracting ring from the well; an echo ring on each beat. Never picked (weight 0): a glowing ring
    // sweeping the screen reads as a shockwave on the drop; still available with --transition ripple.
    {"RIPPLE", 0.0f, 0.5f, R"(
vec3 compose(vec2 px) {
    vec2 c = boardCenter();
    vec2 d = (px - c) / uRes.y;
    // An ellipse fitted to the well: the ring leaves the board's outline first.
    vec2 e = d / vec2(boardSize().x / boardSize().y, 1.0);
    float r = length(e);
    float maxR = length(vec2(uRes.x, uRes.y) * 0.5 / uRes.y / vec2(boardSize().x / boardSize().y, 1.0)) + 0.05;
    float R = mix(-0.08, maxR + 0.08, uP);
    float w = 0.045;
    float ring = exp(-pow((r - R) / w, 2.0));
    vec2 dir = length(d) > 1e-4 ? normalize(d) : vec2(0.0);
    float amp = (0.018 + 0.012 * uKick) * uRes.y * (1.0 - uP * 0.5);
    vec2 off = dir * amp * ring * sign(r - R) * -1.0;
    // Echo: a fainter ring one beat behind.
    float echoR = R - 1.0 / max(uBeats, 1.0) * (maxR + 0.16);
    float echo = exp(-pow((r - echoR) / (w * 0.7), 2.0)) * 0.35;
    vec2 off2 = dir * amp * 0.4 * echo;
    vec3 a = sampleA(px + off + off2), b = sampleB(px + off + off2);
    float m = smoothstep(R + w * 0.6, R - w * 0.6, r);
    vec3 col = bridgeMix(a, b, m);
    return front(col, ring * 0.7 + echo * 0.4);
}
)"},
    // INK: an organic dissolve spreading from the well, with fibrous edges (inked toward a light universe,
    // glowing toward a dark one).
    {"INK", 1.0f, 0.7f, R"(
vec3 compose(vec2 px) {
    vec2 q = px / uRes.y;
    float dist = clamp(boardDist(px) / maxBoardDist(), 0.0, 1.0);
    float n = fbm(q * 3.2 + uSeed * 7.0 + vec2(0.0, uTime * 0.05));
    float T = 0.08 + 0.55 * dist + 0.4 * (n - 0.3);
    T += 0.05 * (fbm(q * 26.0 + uSeed) - 0.5); // fibers
    float m = switchAt(T, 0.02);
    vec3 a = sampleA(px), b = sampleB(px);
    vec3 col = bridgeMix(a, b, m);
    float edge = frontAt(T + 0.015, 0.03);
    // Wet halo: the incoming side darkens (or glows) a little just behind the edge.
    float halo = smoothstep(0.0, 0.08, uP - T) * (1.0 - smoothstep(0.08, 0.25, uP - T));
    return front(col, edge * 0.8 + halo * 0.12);
}
)"},
    // CURTAIN: the old world opens like curtains from the well's axis, gathered in folds at the edges.
    {"CURTAIN", 0.8f, 0.4f, R"(
vec3 compose(vec2 px) {
    float cx = boardCenter().x;
    float hw = max(cx, uRes.x - cx);
    float e = uP * uP * (3.0 - 2.0 * uP);
    float o = e * (hw + 40.0);                     // half width of the opening (pixels)
    float dx = abs(px.x - cx);
    vec3 b = sampleB(px);
    if (dx < o) {
        // The revealed universe, shaded by the curtains' edges.
        float sh = exp(-(o - dx) / (uRes.y * 0.05));
        return mix(b, b * 0.35, sh * (1.0 - 0.5 * uLightB) * (1.0 - e));
    }
    float side = sign(px.x - cx);
    float u = (dx - o) / max(hw - o, 1.0);         // 0 at the edge .. 1 at the screen side
    float src = cx + side * u * hw;                // gathered: the curtain's content squeezed outward
    float fold = 0.5 + 0.5 * cos(u * (10.0 + 30.0 * e) + side * 1.3);
    vec3 a = sampleA(vec2(src, px.y));
    a *= 1.0 - (0.35 * e) * fold;
    // The leading edge catches the light.
    float lip = exp(-(dx - o) / (uRes.y * 0.006));
    return front(a, lip * 0.6);
}
)"},
    // LUMA: darks switch first, highlights last; a small drift from the well.
    {"LUMA", 0.8f, 0.8f, R"(
vec3 compose(vec2 px) {
    vec3 a = sampleA(px), b = sampleB(px);
    // Shades from both frames, slightly blurred by a coarse noise so the order reads as light, not as grain.
    float l = sqrt(clamp(0.5 * luma(a) + 0.5 * luma(b), 0.0, 1.0));
    float T = 0.08 + 0.45 * l + 0.3 * clamp(boardDist(px) / maxBoardDist(), 0.0, 1.0);
    T += 0.14 * (fbm(px / uRes.y * 2.5 + uSeed) - 0.5);
    float m = switchAt(T, 0.12);
    return front(bridgeMix(a, b, m), frontAt(T, 0.02) * 0.25);
}
)"},
    // TILES: the screen in cells of the well's size, aligned with it; the well flips first (from the bottom),
    // then the cells around it ring after ring, each ring on an eighth note.
    {"TILES", 1.0f, 0.6f, R"(
vec2 center0(vec2 id) { return uBoard.xy + (id + 0.5) * cellSize(); }
vec3 compose(vec2 px) {
    vec2 cs = cellSize();
    vec2 g = (px - uBoard.xy) / cs;                  // board cell coordinates (fractional)
    vec2 id = floor(g);
    vec2 lc = g - id - 0.5;                          // -0.5..0.5 inside the cell
    float steps = max(2.0, uBeats * 2.0);
    float T;
    bool inBoard = id.x >= 0.0 && id.x < 10.0 && id.y >= 0.0 && id.y < 20.0;
    float jit = hash(id + uSeed);
    if (inBoard) {
        T = 0.04 + 0.3 * (id.y / 20.0) + 0.03 * jit;
    } else {
        // Outward from the well, cards scattered (not whole columns at once).
        float d = clamp(boardDist(center0(id)) / maxBoardDist(), 0.0, 1.0);
        T = 0.34 + 0.46 * d + 0.16 * jit;
        T = ceil(T * steps) / steps;                 // rings flip on the eighth notes
    }
    float dur = max(1.6 / steps, 0.05);
    float f = clamp((uP - T) / dur + 0.5, 0.0, 1.0);
    float ang = f * 3.14159265;
    float sx = abs(cos(ang));                         // the card seen edge-on in the middle of its flip
    vec2 center = uBoard.xy + (id + 0.5) * cs;
    vec3 gap = mix(uBgA, uBgB, f) * 0.5;
    if (abs(lc.x) > sx * 0.5) return gap;
    vec2 sp = center + vec2(lc.x / max(sx, 1e-3), lc.y) * cs;
    vec3 col = f < 0.5 ? sampleA(sp) : sampleB(sp);
    col *= 0.55 + 0.45 * sx;                         // turning away from the light
    float flipping = sin(ang);
    float border = step(0.47, max(abs(lc.x) / max(sx, 1e-3), abs(lc.y)));
    return front(col, border * flipping * 0.7);
}
)"},
};

std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

} // namespace

int count() { return COUNT; }
const char* name(int kind) { return INFOS[std::clamp(kind, 0, COUNT - 1)].name; }
unsigned style(int kind) { return 1u << std::clamp(kind, 0, COUNT - 1); }
float bridgeDepth(int kind) { return INFOS[std::clamp(kind, 0, COUNT - 1)].depth; }

int find(const std::string& n) {
    const std::string u = upper(n);
    for (int k = 0; k < COUNT; k++)
        if (u == INFOS[k].name) return k;
    return -1;
}

int pick(const TransitionHints& a, const TransitionHints& b, Reason why, uint64_t seed, int last) {
    float w[COUNT];
    const float gap = std::fabs(ugl::luma(a.background) - ugl::luma(b.background));
    for (int k = 0; k < COUNT; k++) {
        w[k] = INFOS[k].weight;
        if (a.styles & style(k)) w[k] *= 1.7f;
        if (b.styles & style(k)) w[k] *= 1.7f;
    }
    // Between a dark and a light universe: transitions that bridge brightness gracefully.
    w[LUMA] *= 1.f + 1.5f * gap;
    w[INK] *= 1.f + 1.f * gap;
    w[CURTAIN] *= 1.f - 0.4f * gap;
    if (why == PHASE_PEAK) { w[TILES] *= 1.5f; w[CURTAIN] *= 0.3f; w[LUMA] *= 0.5f; }
    if (why == SONG_CHANGE) { w[REBUILD] *= 1.4f; w[CURTAIN] *= 1.3f; }
    if (last >= 0 && last < COUNT) w[last] = 0.f;
    Rng r(seed ^ 0x7A2517105ull);
    return r.weighted(w);
}

Bridge bridgeFor(const TransitionHints& self, const TransitionHints& other, bool entering) {
    Bridge br;
    br.entering = entering;
    // Neutral in form (no world, a plain stack), in the universe's own tones leaning a little toward the other's:
    // the front between the two halves then only changes the material, never passes through a gray screen.
    br.color = ugl::toLinear(lerp(ugl::toSrgb(self.background), ugl::toSrgb(other.background), 0.15f)); // perceptual
    // The ink: the universe's own (its lines on a light ground, its glow on a dark one), kept readable.
    vec3 ink = self.light ? self.ink : self.accent;
    const float lc = ugl::luma(br.color);
    const float li = ugl::luma(ink);
    if (std::fabs(li - lc) < 0.25f) ink = lc > 0.3f ? ink * 0.25f : ink + vec3(0.45f);
    br.ink = ink;
    return br;
}

Compositor::~Compositor() {
    for (GLuint p : progs_)
        if (p) glDeleteProgram(p);
}

bool Compositor::init() {
    for (int k = 0; k < COUNT; k++) {
        const std::string fs = std::string(PRELUDE) + INFOS[k].glsl;
        progs_[k] = ugl::program(ugl::FULLSCREEN_VS, fs.c_str(), (std::string("transition ") + INFOS[k].name).c_str());
    }
    return glGetError() == GL_NO_ERROR;
}

void Compositor::draw(int kind, const Inputs& in) {
    using namespace ugl;
    const GLuint p = progs_[std::clamp(kind, 0, COUNT - 1)];
    glUseProgram(p);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    set1i(p, "uA", 0);
    set1i(p, "uB", 1);
    set1i(p, "uCells", 2);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, in.texA);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, in.texB);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, in.cells);
    glActiveTexture(GL_TEXTURE0);
    const layout::Rect r = layout::boardRect(in.width, in.height);
    set2f(p, "uRes", (float)in.width, (float)in.height);
    set4f(p, "uBoard", r.x0, in.height - r.y1, r.x1, in.height - r.y0);
    set1f(p, "uP", saturate(in.progress));
    set1f(p, "uBeats", in.beats);
    set1f(p, "uKick", in.kick);
    set1f(p, "uBeatPhase", in.beatPhase);
    set1f(p, "uTime", (float)std::fmod(in.time, 1000.0));
    set1f(p, "uSeed", (float)(in.seed % 1000) * 0.013f);
    set3f(p, "uBgA", in.a.background);
    set3f(p, "uBgB", in.b.background);
    set3f(p, "uInkA", in.a.ink);
    set3f(p, "uInkB", in.b.ink);
    set3f(p, "uAccA", in.a.accent);
    set3f(p, "uAccB", in.b.accent);
    set1f(p, "uLightA", in.a.light ? 1.f : 0.f);
    set1f(p, "uLightB", in.b.light ? 1.f : 0.f);
    set3f(p, "uBridgeColorA", in.bridgeA.color);
    set3f(p, "uBridgeInkA", in.bridgeA.ink);
    set3f(p, "uBridgeColorB", in.bridgeB.color);
    set3f(p, "uBridgeInkB", in.bridgeB.ink);
    set1f(p, "uHostBridgeA", in.hostBridgeA);
    set1f(p, "uHostBridgeB", in.hostBridgeB);
    drawFullscreen();
}

} // namespace transitions
