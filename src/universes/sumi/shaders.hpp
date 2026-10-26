#pragma once
// SUMI: ink on paper. Every shader of the renderer.
//
// Color space: everything is authored and composed in display (sRGB) values, like pigments on paper.
// Ink layers are drawn with a multiplicative blend (dst *= 1 - absorbance): overlapping inks darken
// each other as real ink does, and the paper's fibers always show through a wash.
// NOISE_GLSL is inserted after the #version line of every fragment shader (see SumiUniverse::compile).

namespace sumi_shaders {

inline const char* FULLSCREEN_VS = R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

inline const char* NOISE_GLSL = R"(
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec2 hash22(vec2 p) { return vec2(hash12(p), hash12(p + 17.31)); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
    return s;
}
float fbm3(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
    return s / 0.875;
}
// The song's ink variant: main ink and the second wash (see VARIANTS in sumi.cpp).
uniform vec3 uInk, uInk2;
#define SUMI uInk
#define INDIGO uInk2
)";

// ---------------------------------------------------------------- paper (rendered once per size)
// Stores the paper's relief as a brightness multiplier (x0.5): tone clouds, pulp clumps, long kozo fibers, grain.
inline const char* PAPER_FS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform vec2 uRes;
uniform float uScale; // world units per pixel
float fiberLayer(vec2 p, float cell, float len, float width, float seed, float presence) {
    vec2 id = floor(p / cell);
    float acc = 0.0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            vec2 c = id + vec2(i, j);
            if (hash12(c + seed) > presence) continue;
            vec2 o = (c + hash22(c * 1.37 + seed)) * cell;
            float a = hash12(c + seed * 3.1) * 6.2832;
            vec2 dir = vec2(cos(a), sin(a)), nrm = vec2(-dir.y, dir.x);
            float L = len * (0.5 + hash12(c + seed * 2.3));
            vec2 d = p - o;
            float t = clamp(dot(d, dir), -L, L);
            float bend = (hash12(c + seed * 5.3) - 0.5) * 0.8 / L;
            vec2 q = d - dir * t - nrm * bend * t * t;
            float w = width * (0.6 + 0.8 * hash12(c + 9.1 + seed));
            float taper = 1.0 - (t / L) * (t / L);
            float sgn = hash12(c + seed * 7.7) > 0.35 ? 1.0 : -0.8; // most fibers are lighter, some darker
            acc += smoothstep(w, w * 0.2, length(q)) * taper * sgn;
        }
    return acc;
}
void main() {
    vec2 p = gl_FragCoord.xy * uScale; // world-sized units, resolution independent
    float m = 1.0;
    m += 0.045 * (fbm(p * 0.11 + 3.0) - 0.5) * 2.0;   // large uneven tone
    m += 0.022 * (fbm(p * 0.9 + 11.0) - 0.5) * 2.0;   // cloudy pulp
    m += 0.018 * (vnoise(p * 7.0) - 0.5) * 2.0;       // fine pulp clumps
    float px = uScale;
    m += 0.030 * fiberLayer(p, 0.55, 0.45, max(px * 0.9, 0.010), 1.0, 0.55);
    m += 0.022 * fiberLayer(p, 1.6, 1.3, max(px * 1.1, 0.014), 7.0, 0.5);
    m += 0.016 * fiberLayer(p + 31.0, 0.32, 0.22, max(px * 0.8, 0.008), 13.0, 0.5);
    m += 0.016 * (hash12(gl_FragCoord.xy) - 0.5) * 2.0; // grain
    fragColor = vec4(vec3(m * 0.5), 1.0);
}
)";

// ---------------------------------------------------------------- background (paper + ink-wash landscape)
inline const char* BG_FS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uPaper;
uniform vec4 uView;       // center xy, half extents zw (world)
uniform vec3 uPaperCol;
uniform float uDriftFar, uDriftMid, uDriftNear;
uniform vec4 uLevel;      // level A, level B, wash front radius (world, <0: none), wash progress 0..1
uniform float uImpact, uSeed;
uniform vec2 uWell;       // board half extents
uniform float uWorld;     // 1: the landscape, 0: bare paper (the bridge of a change of universe)
uniform vec3 uSkyCol;     // the peak's sky wash: its pigment, its density, the sun (center xy, radius)
uniform float uSky;
uniform vec3 uSun;
float wellMask(vec2 p, float soft) {
    vec2 d = abs(p) - uWell;
    return 1.0 - smoothstep(0.0, soft, max(d.x, d.y));
}
// A mountain range: ink density at p. base: ridge height, amp: relief, f: frequency.
float range(vec2 p, float base, float amp, float f, float drift, float seed, float soft, float body) {
    // Distinct summits: one per cell (random height, width, lean), the range is their upper envelope.
    float x = p.x * f + drift;
    x += 0.22 * (fbm3(vec2(x * 1.7, seed + 40.0)) - 0.5); // warp: no straight flanks
    float cx = floor(x);
    float h = 0.0;
    for (int i = -2; i <= 2; i++) {
        float c = cx + float(i);
        float hh = hash12(vec2(c, seed));
        float pos = c + 0.5 + (hash12(vec2(c, seed + 1.0)) - 0.5) * 0.7;
        float height = 0.35 + 0.65 * hh * hh;
        float width = 0.9 + 1.0 * hash12(vec2(c, seed + 2.0));
        float dx = x - pos;
        dx *= dx < 0.0 ? 1.0 + 0.5 * (hash12(vec2(c, seed + 3.0)) - 0.5) : 1.0 - 0.5 * (hash12(vec2(c, seed + 3.0)) - 0.5);
        float t = max(0.0, 1.0 - abs(dx) / width);
        // concave flanks, a slightly rounded top
        float k = 1.3 + 1.2 * hash12(vec2(c, seed + 4.0)); // flank curvature varies per summit
        float prof = height * pow(t, k) * (1.0 + 0.25 * t * (1.0 - t));
        h = max(h, prof);
    }
    h += 0.08 * (fbm3(vec2(x * 5.0, seed + 19.0)) - 0.5) + 0.025 * vnoise(vec2(x * 21.0, seed + 23.0));
    h = base + amp * h;
    float d = h - p.y; // > 0 below the ridge
    if (d < -0.2) return 0.0;
    float edge = smoothstep(-soft, soft, d + (vnoise(vec2(p.x * 9.0, p.y * 2.0 + seed)) - 0.5) * 0.05);
    // a darker ridge line, a wash that fades downward into mist
    float wash = 0.35 * exp(-d * 9.0) + 0.5 * exp(-d * 2.0) + 0.15 * exp(-d * body);
    // texture strokes (cun): streaks running down the slopes
    float tex = 0.7 + 0.6 * fbm3(vec2(p.x * 3.0 + seed, p.y * 0.5 + p.x * 0.3));
    return edge * wash * tex;
}
void main() {
    vec2 p = uView.xy + (vUV * 2.0 - 1.0) * uView.zw;
    float relief = texture(uPaper, vUV).r * 2.0;
    float well = wellMask(p, 0.25);
    // The well is a cleaner, slightly brighter sheet laid on the paper.
    float mrel = mix(relief, 1.0 + (relief - 1.0) * 0.45, well);
    vec3 col = uPaperCol * mrel * (1.0 + 0.028 * well);

    // Level at this point: the wash front carries the new level outward from the board.
    float lv = uLevel.y;
    float rr = 0.0;
    if (uLevel.z >= 0.0) {
        rr = length(p * vec2(0.85, 1.0)) + (fbm(p * 0.35 + uSeed) - 0.5) * 3.2;
        lv = mix(uLevel.x, uLevel.y, smoothstep(uLevel.z + 0.8, uLevel.z - 0.8, rr));
    }
    if (well > 0.999 && uLevel.z < 0.0) { fragColor = vec4(col, 1.0); return; } // the board: paper only

    float midW = smoothstep(0.0, 1.0, lv), peakW = smoothstep(1.0, 2.0, lv);
    float mist = smoothstep(0.35, 0.75, fbm3(vec2(p.x * 0.09 - uDriftMid * 0.6, p.y * 0.55 + 4.0)));
    float far = range(p, -1.0, 9.0, 0.13, uDriftFar, 2.0 + uSeed, 0.05, 0.5);
    float mid = range(p, -6.0, 6.5, 0.17, uDriftMid, 17.0 + uSeed, 0.035, 0.7);
    float near = range(p, -12.0, 6.0, 0.10, uDriftNear, 41.0 + uSeed, 0.025, 0.9);
    float aFar = 0.10 + 0.10 * midW + 0.05 * peakW;
    float aMid = 0.24 * midW + 0.12 * peakW;
    float aNear = 0.08 * midW + 0.62 * peakW;
    float dFar = far * aFar * (1.0 - 0.6 * mist * midW);
    float dMid = mid * aMid * (1.0 - 0.75 * mist);
    float dNear = near * aNear * (1.0 - 0.35 * mist);
    float behind = (1.0 - 0.94 * well) * uWorld;
    // The peak's sky: a wash of color soaked into the paper, graded up from the hills and deepest around the
    // sun; the top of the sheet stays white.
    if (uSky > 0.001) {
        float yn = (p.y - uView.y) / uView.w;
        float grad = smoothstep(0.45, -0.75, yn);
        float ds = length(p - uSun.xy) / max(uSun.z, 0.1);
        float halo = exp(-ds * ds * 0.09);
        float mott = 0.7 + 0.6 * fbm3(p * vec2(0.08, 0.16) + vec2(uDriftFar * 2.0, 3.0));
        float d = uSky * peakW * (0.17 * grad + 0.22 * halo) * mott * behind;
        col *= 1.0 - d * (1.0 - uSkyCol);
    }
    vec3 inkFar = mix(SUMI, INDIGO, 0.45), inkMid = mix(SUMI, INDIGO, 0.2);
    col *= 1.0 - behind * dFar * (1.0 - inkFar);
    col *= 1.0 - behind * dMid * (1.0 - inkMid);
    col *= 1.0 - behind * dNear * (1.0 - SUMI);

    // The wash itself: a wet front (with its darker tide line) that dries behind as it spreads.
    if (uLevel.z >= 0.0) {
        float e = uLevel.z - rr;
        float dry = 1.0 - uLevel.w * uLevel.w;
        float tide = exp(-pow((e - 0.12) / 0.16, 2.0)) * 0.12; // a soft tide line, never a ring hitting harder at a drop
        float body = smoothstep(0.0, 0.6, e) * exp(-max(e, 0.0) * 0.35) * (0.045 + 0.06 * uImpact);
        float grain = 0.75 + 0.5 * vnoise(p * 3.0);
        float dens = (tide + body) * grain * dry * (1.0 - 0.8 * well) * uWorld;
        col *= 1.0 - dens * (1.0 - mix(SUMI, INDIGO, 0.3));
    }
    fragColor = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- ink sprites (everything drawn on the paper)
inline const char* SPRITE_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 iA; // center xy, half size zw (world)
layout(location = 2) in vec4 iB; // rotation, kind, seed, alpha
layout(location = 3) in vec4 iC;
layout(location = 4) in vec4 iD;
uniform mat4 uVP;
out vec2 vLocal;
out vec2 vWorld;
flat out vec4 vA, vB, vC, vD;
void main() {
    vec2 l = aCorner * iA.zw;
    float c = cos(iB.x), s = sin(iB.x);
    vec2 w = iA.xy + vec2(c * l.x - s * l.y, s * l.x + c * l.y);
    vLocal = l;
    vWorld = w;
    vA = iA; vB = iB; vC = iC; vD = iD;
    gl_Position = uVP * vec4(w, 0.0, 1.0);
}
)";

inline const char* SPRITE_FS = R"(#version 330 core
in vec2 vLocal;
in vec2 vWorld;
flat in vec4 vA, vB, vC, vD;
out vec4 fragColor;
uniform vec3 uVerm, uVermDark; // the accent (vermilion on the classic paper)
uniform vec3 uPiece[7];          // the seven piece inks
uniform vec2 uWell;
uniform float uPx; // world units per pixel

float sat(float x) { return clamp(x, 0.0, 1.0); }
float aaStep(float d, float soft) { float w = max(uPx * 0.9, soft); return 1.0 - smoothstep(-w, w, d); }

// A brush mark along u (0 start .. 1 end), v across (-1..1 at the nominal width), L: length / width.
// Returns ink density. dry: how much the bristles break up; taper: how much the end thins.
float brush(float u, float v, float L, float seed, float dry, float taper) {
    float w = mix(0.82, 1.0, smoothstep(0.0, 0.06, u)) * (1.0 - taper * smoothstep(0.45, 1.0, u));
    w *= 1.0 + 0.10 * (vnoise(vec2(u * L * 0.35, seed)) - 0.5);
    float vv = v + 0.10 * (vnoise(vec2(u * L * 0.2, seed + 5.0)) - 0.5);
    float edgeN = (vnoise(vec2(u * L * 2.5, seed + 9.0)) - 0.5) * 0.18;
    float side = abs(vv) / max(w, 1e-3) + edgeN;
    float ux = u * L;
    if (ux < 0.35) side = max(side, length(vec2((0.35 - ux) / 0.35, vv / max(w, 1e-3))) + edgeN);
    float body = 1.0 - smoothstep(0.93, 1.0, side);
    float hair = vnoise(vec2(u * L * 0.9, vv * 14.0 + seed));
    float hair2 = vnoise(vec2(u * L * 2.2, vv * 42.0 + seed * 1.7));
    float gaps = dry * (0.05 + 0.95 * smoothstep(0.4, 1.0, u)) + 0.45 * dry * smoothstep(0.7, 1.0, side);
    float breakup = smoothstep(gaps - 0.04, gaps + 0.04, 0.6 * hair + 0.4 * hair2);
    float dens = 0.88 + 0.12 * smoothstep(0.1, 0.0, u) + 0.1 * smoothstep(0.55, 0.95, side);
    dens *= 0.9 + 0.22 * (hair - 0.5);
    return body * mix(1.0, breakup, sat(dry * 1.5)) * dens;
}

// Rounded box distance, with connected sides pushed outward (no edge there).
float blockSD(vec2 q, int mask, float inset, float r) {
    float L = (mask & 1) != 0 ? 0.9 : 0.5 - inset;
    float R = (mask & 2) != 0 ? 0.9 : 0.5 - inset;
    float D = (mask & 4) != 0 ? 0.9 : 0.5 - inset;
    float U = (mask & 8) != 0 ? 0.9 : 0.5 - inset;
    vec2 c = vec2((R - L) * 0.5, (U - D) * 0.5), h = vec2((R + L) * 0.5, (U + D) * 0.5);
    vec2 d = abs(q - c) - (h - r);
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

vec4 inkOut(float dens, vec3 ink) { return vec4(sat(dens) * (1.0 - ink), 1.0); }

void main() {
    int kind = int(vB.y + 0.5);
    bool behind = kind >= 100;
    if (behind) kind -= 100;
    float seed = vB.z, alpha = vB.w;
    vec2 l = vLocal;
    float dens = 0.0;
    vec3 ink = SUMI;

    if (kind == 0 || kind == 1) {
        // ---- blocks (0) and the ghost (1). vC: type, mask, wet, cell scale. vD: bleed age, dissolve, noise origin.
        float sc = vC.w;
        vec2 q = l / sc;
        int mask = int(vC.y + 0.5);
        if ((mask & 1) != 0 && q.x < -0.5) discard;
        if ((mask & 2) != 0 && q.x > 0.5) discard;
        if ((mask & 4) != 0 && q.y < -0.5) discard;
        if ((mask & 8) != 0 && q.y > 0.5) discard;
        int type = int(vC.x + 0.5);
        float wet = vC.z, age = vD.x, dis = vD.y;
        vec2 n = vD.zw + q;
        float px = uPx / sc;
        float d = blockSD(q, mask, 0.045, 0.09);
        d += (fbm3(n * 2.6 + seed) - 0.5) * 0.07 + (vnoise(n * 13.0 + seed) - 0.5) * 0.03;
        if (kind == 1) {
            // ghost: a faint, broken graphite outline
            float line = 1.0 - smoothstep(0.012, 0.03, abs(d + 0.05));
            float brk = smoothstep(0.25, 0.45, vnoise(n * 7.0 + seed));
            dens = line * brk * 0.42 + aaStep(d, px) * 0.035;
            fragColor = inkOut(dens * alpha, mix(uInk, vec3(0.5), 0.45));
            return;
        }
        // dissolving (cleared rows): the ink spreads into water, softens and fades
        d -= dis * 0.22;
        float soft = px + 0.02 * wet + 0.35 * dis;
        float inside = aaStep(d, soft);
        float mott = fbm3(n * 1.25 + seed * 3.0);
        float hair = vnoise(vec2(n.x * 1.3 + seed, n.y * 24.0));
        float pool = exp(-max(-d, 0.0) * 16.0);
        float pool2 = exp(-max(-d, 0.0) * 30.0);
        float dryGap = smoothstep(0.66, 0.82, hair) * smoothstep(-0.16, -0.01, d) * (1.0 - wet);
        vec3 pc = uPiece[type];
        ink = pc;
        if (type == 0) { // I: solid sumi
            dens = 0.88 + 0.10 * (mott - 0.5) + 0.10 * pool - 0.55 * dryGap;
        } else if (type == 1) { // O: dilute wash with a tide line
            dens = 0.26 + 0.20 * (mott - 0.5) + 0.42 * pool2 + 0.06 * hair;
        } else if (type == 2) { // T: vermilion, the seal's red
            dens = 0.86 + 0.10 * (mott - 0.5) + 0.10 * pool - 0.4 * dryGap;
            ink = mix(pc, pc * vec3(0.62, 0.55, 0.6), 0.65 * pool2 + 0.2 * (1.0 - mott));
        } else if (type == 3) { // S: hatched (diagonal dry strokes)
            float h = fract((n.x + n.y) * 2.6 + (vnoise(n * 2.0) - 0.5) * 0.35);
            float lineW = 0.17 + 0.08 * (vnoise(n * 3.0 + 7.0) - 0.5);
            float line = smoothstep(lineW + 0.07, lineW - 0.02, abs(h - 0.5));
            float brk = smoothstep(0.18, 0.35, vnoise(vec2((n.x - n.y) * 4.0, (n.x + n.y) * 1.3) + seed));
            dens = 0.12 + 0.06 * mott + 0.6 * pool2 + 0.72 * line * brk;
        } else if (type == 4) { // Z: indigo wash
            dens = 0.70 + 0.20 * (mott - 0.5) + 0.28 * pool2 + 0.05 * hair;
        } else if (type == 5) { // J: stippled dots in a pale wash
            vec2 g = fract(n * 3.0 + 0.5) - 0.5;
            float dd = length(g) / 3.0 + (vnoise(n * 9.0 + seed) - 0.5) * 0.02;
            float dt = smoothstep(0.075, 0.055, dd);
            dens = 0.10 + 0.05 * mott + 0.55 * pool2 + 0.85 * dt;
        } else { // L: brush outline, nearly blank inside
            float band = smoothstep(-0.19, -0.13, d);
            float brk = smoothstep(0.2, 0.4, vnoise(vec2(n.x * 5.0 + n.y * 5.0, seed)));
            dens = 0.07 + 0.05 * mott + band * (0.86 - 0.3 * (1.0 - brk));
        }
        dens = clamp(dens, 0.0, 1.0) * (1.0 + 0.06 * wet);
        dens *= inside;
        float out_ = max(d, 0.0);
        float halo = wet * 0.10 * exp(-out_ * 40.0) * (1.0 - inside);
        // soaking into the paper after a lock: a bleed halo with capillary fingers and a tide line
        if (age >= 0.0 && age < 4.0) {
            float rad = 0.05 + 0.20 * (1.0 - exp(-age * 3.5));
            float rn = out_ - (fbm3(n * 4.5 + seed * 2.0) - 0.5) * 0.13;
            float fade = exp(-age * 1.0);
            halo += (1.0 - inside) * (smoothstep(rad, rad * 0.2, rn) * 0.16 + exp(-pow((rn - rad) / 0.025, 2.0)) * 0.10) * fade;
        }
        if (type == 2 && halo > 0.0) ink = mix(ink, pc, sat(halo * 3.0) * (1.0 - inside));
        dens += halo * (type == 1 ? 0.7 : 1.0);
        dens *= 1.0 - dis * dis * (0.6 + 0.4 * fbm3(n * 3.0));
        if (dis > 0.0) dens *= 1.0 - smoothstep(0.75, 1.0, dis);
        fragColor = inkOut(dens * alpha, ink);
        return;
    }

    if (kind == 2) { // ---- ink droplet / diffusing speck. vC.rgb color, vC.w rim; vD.x irregularity
        float r = length(l) / vA.z;
        float a = atan(l.y, l.x);
        float rn = r + (vnoise(vec2(cos(a), sin(a)) * 1.6 + seed) - 0.5) * vD.x;
        float body = 1.0 - smoothstep(0.3, 1.0, rn);
        dens = body * (0.65 + vC.w * smoothstep(0.55, 0.95, rn));
        ink = vC.rgb;
    } else if (kind == 3) { // ---- brush stroke. vC.rgb color. vD: reveal, dry, well fade, taper
        float L = vA.z / vA.w;
        float u = l.x / vA.z * 0.5 + 0.5, v = l.y / vA.w;
        float lead = smoothstep(vD.x, vD.x - 0.03, u);
        dens = brush(u, v, L, seed, vD.y, vD.w) * lead;
        ink = vC.rgb;
        if (abs(vWorld.x) < uWell.x && abs(vWorld.y) < uWell.y + 0.4) dens *= vD.z;
    } else if (kind == 4) { // ---- seal (hanko): carved vermilion square. vD.x: ink transfer
        vec2 q = l / vA.z; // -1..1
        float box = max(abs(q.x), abs(q.y)) + (vnoise(q * 6.0 + seed) - 0.5) * 0.05;
        if (box > 1.0) discard;
        float carve = smoothstep(0.035, 0.0, abs(box - 0.84));
        vec4 seg[14] = vec4[14](
            vec4(-0.64, 0.56, -0.08, 0.56), vec4(-0.36, -0.66, -0.36, 0.56), vec4(-0.60, 0.16, -0.12, 0.16),
            vec4(-0.64, -0.26, -0.08, -0.26), vec4(-0.60, -0.26, -0.60, 0.16), vec4(-0.12, -0.66, -0.12, -0.26),
            vec4(0.10, 0.60, 0.62, 0.60), vec4(0.10, 0.20, 0.62, 0.20), vec4(0.10, 0.20, 0.10, 0.60),
            vec4(0.62, 0.20, 0.62, 0.60), vec4(0.36, -0.66, 0.36, 0.60), vec4(0.06, -0.18, 0.66, -0.18),
            vec4(0.14, -0.66, 0.14, -0.18), vec4(0.58, -0.66, 0.58, -0.18));
        for (int i = 0; i < 14; i++) {
            vec2 a = seg[i].xy, b = seg[i].zw, pa = q - a, ba = b - a;
            float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
            float dd = length(pa - ba * h) + (vnoise(q * 11.0 + float(i)) - 0.5) * 0.02;
            carve = max(carve, smoothstep(0.065, 0.045, dd));
        }
        float speck = smoothstep(0.62, 0.8, vnoise(q * 9.0 + seed * 3.0)) * 0.55 +
                      smoothstep(0.75, 0.95, hash12(floor(q * 40.0) + seed)) * 0.35;
        float edge = 1.0 - smoothstep(0.93, 1.0, box);
        dens = edge * (1.0 - carve) * (0.92 - speck * (1.0 - vD.x * 0.5));
        ink = mix(uVerm, uVermDark, 0.15 + 0.2 * vnoise(q * 3.0 + seed));
    } else if (kind == 5) { // ---- enso / brush arc. vC.rgb color, vC.w sweep. vD: radius, width, start angle, reveal
        float r = length(l);
        float ang = atan(l.y, l.x);
        float t = mod(ang - vD.z, 6.2831853) / vC.w; // counterclockwise from the start
        if (t > 1.0) discard;
        float L = vD.x * vC.w / vD.y;
        float r0 = vD.x * (1.0 + 0.018 * sin(t * 5.0 + seed) + 0.025 * t);
        float v = (r - r0) / vD.y;
        float lead = smoothstep(vD.w, vD.w - 0.02, t);
        dens = brush(t, v, L, seed, 0.55, 0.7) * lead;
        ink = vC.rgb;
    } else if (kind == 6) { // ---- sun disc: a vermilion wash
        float r = length(l) / vA.z;
        float a = atan(l.y, l.x);
        r += (vnoise(vec2(cos(a), sin(a)) * 3.0 + seed) - 0.5) * 0.04;
        float body = 1.0 - smoothstep(0.96, 1.0, r);
        float mott = fbm3(l * 0.9 + seed);
        dens = body * (0.62 + 0.18 * (mott - 0.5) + 0.25 * smoothstep(0.8, 0.99, r));
        ink = mix(uVerm, uVermDark, 0.2 * smoothstep(0.85, 1.0, r));
    } else if (kind == 7) { // ---- petal
        vec2 q = l / vA.z;
        float notch = 0.25 * smoothstep(0.15, 0.0, abs(q.x)) * step(0.0, q.y);
        float e = length(vec2(q.x * 1.6, q.y)) + notch * smoothstep(0.4, 1.0, q.y);
        float body = 1.0 - smoothstep(0.85, 1.0, e);
        dens = body * (0.45 + 0.4 * smoothstep(0.6, 0.97, e));
        ink = vC.rgb;
    } else if (kind == 9) { // ---- plum blossom. vC.rgb color, vC.w opening 0..1. vD.x style: 0 wash, 1 outline, 2 bud
        vec2 q = l / (vA.z / 1.15);
        float open = vC.w;
        int style = int(vD.x + 0.5);
        ink = vC.rgb;
        if (style == 2) { // a bud: a dark dab of ink
            float r = length(q * vec2(1.0, 1.3)) + (vnoise(q * 4.0 + seed) - 0.5) * 0.18;
            dens = (1.0 - smoothstep(0.6 * open, 0.75 * open + 0.01, r)) * 0.95;
            ink = SUMI;
        } else {
            float sp = mix(0.4, 1.0, open); // the petals spread as it opens
            float petals = 0.0, rim = 0.0;
            for (int k = 0; k < 5; k++) {
                float a = 6.2831853 * float(k) / 5.0 + seed;
                vec2 c = vec2(cos(a), sin(a)) * 0.45 * sp;
                float d = length(q - c) / (0.42 * sp) + (vnoise(q * 5.0 + float(k) * 3.1 + seed) - 0.5) * 0.14;
                petals = max(petals, 1.0 - smoothstep(0.86, 1.0, d));
                rim = max(rim, exp(-pow((d - 0.9) / 0.08, 2.0)));
            }
            // an accent wash (red plum) or a pale ink wash (white plum), each petal with a darker tide line
            dens = petals * (0.5 + 0.3 * fbm3(q * 2.0 + seed)) + rim * 0.22;
            if (style == 1) dens *= 0.45;
            // the heart: a dark calyx dot and a few stamens around it
            float st = 1.0 - smoothstep(0.06, 0.10, length(q));
            for (int k = 0; k < 6; k++) {
                float a = 6.2831853 * float(k) / 6.0 + seed * 1.7;
                vec2 c = vec2(cos(a), sin(a)) * 0.27 * sp;
                st = max(st, 1.0 - smoothstep(0.035, 0.06, length(q - c)));
            }
            st *= smoothstep(0.3, 0.8, open);
            ink = mix(ink, SUMI, st);
            dens = max(dens, st * 0.9);
        }
    } else if (kind == 8) { // ---- bloom: a drop of ink spreading in wet paper. vD: radius, wash, core, roughness
        float R = max(vD.x, 1e-3);
        float r = length(l) / R;
        float a = atan(l.y, l.x);
        vec2 dir = vec2(cos(a), sin(a));
        // crinkled (cauliflower) edge
        float fing = (fbm3(dir * 2.0 + seed) - 0.5) * vD.w + (vnoise(dir * 11.0 + seed) - 0.5) * vD.w * 0.35;
        float rn = r - fing;
        float body = 1.0 - smoothstep(0.86, 0.97, rn);
        float mott = 0.6 + 0.8 * fbm3(l / R * 2.5 + seed);
        float core = exp(-r * r * 40.0);
        float tide = exp(-pow((rn - 0.93) / 0.03, 2.0));
        dens = body * vD.y * mott + tide * vD.y * 2.2 + core * vD.z;
        ink = vC.rgb;
    }
    if (behind) {
        vec2 dw = abs(vWorld) - uWell;
        dens *= mix(0.06, 1.0, smoothstep(-0.1, 0.25, max(dw.x, dw.y)));
    }
    fragColor = inkOut(dens * alpha, ink);
}
)";

// ---------------------------------------------------------------- final pass: print look, ink negative
inline const char* POST_FS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uScene;
uniform float uAspect, uFade, uDim, uInvert, uInvFront;
uniform vec2 uInvCenter;
uniform vec3 uPaperCol, uLacquer, uIvory;
uniform float uBridge;           // change of universe: 0 itself .. 1 the bridge
uniform vec3 uBridgeColor, uBridgeInk; // display values
uniform float uNeg; // the variant itself is a negative (light ink on lacquer): slow motion then turns it to paper
vec3 negative(vec3 c) {
    // light ink on black lacquer: lightness reversed, the pigments' hue kept
    float Y = dot(c, vec3(0.299, 0.587, 0.114));
    vec3 chroma = c - Y;
    float t = clamp((0.94 - Y) / 0.84, 0.0, 1.0);
    vec3 base = mix(uLacquer, uIvory, pow(t, 1.15));
    return clamp(base + chroma * 0.85, 0.0, 1.0);
}
void main() {
    vec3 col = texture(uScene, vUV).rgb;
    float Y = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(col, mix(vec3(Y), col, 0.5) * vec3(0.78), uDim);
    vec2 d = (vUV - 0.5) * vec2(uAspect, 1.0);
    float v = smoothstep(0.5, 1.25, length(d) + (vnoise(vUV * 4.0) - 0.5) * 0.12);
    col *= mix(vec3(1.0), vec3(0.78), v * 0.6);
    float inv = uInvert;
    if (uInvFront >= 0.0) {
        float r = length((vUV - uInvCenter) * vec2(uAspect, 1.0)) + (fbm3(vUV * 5.0) - 0.5) * 0.12;
        float m = smoothstep(uInvFront, uInvFront - 0.06, r);
        inv = uInvert > 0.5 ? m : 1.0 - m;
        float tide = exp(-pow((r - uInvFront) / 0.012, 2.0)) * 0.35;
        col *= 1.0 - tide * (1.0 - SUMI);
    }
    inv = abs(uNeg - inv);
    col = mix(col, negative(col), inv);
    col *= 1.0 + (hash12(gl_FragCoord.xy + 0.5) - 0.5) * 0.025;
    // The bridge (a change of universe): the paper turns to the bridge's color and the ink to its ink, by how
    // inked each pixel is (the landscape is already gone, so what is left is the well, the stack and the HUD).
    if (uBridge > 0.0) {
        float flip = abs(uNeg - uInvert);
        vec3 bg = mix(uPaperCol, negative(uPaperCol), flip), ink = mix(SUMI, negative(SUMI), flip);
        vec3 dI = ink - bg;
        float k = clamp(dot(col - bg, dI) / max(dot(dI, dI), 1e-4), 0.0, 1.0);
        col = mix(col, mix(uBridgeColor, uBridgeInk, k), uBridge);
    }
    col = mix(mix(uPaperCol, negative(uPaperCol), abs(uNeg - uInvert)), col, uFade);
    fragColor = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- text (absorbance into the scene)
inline const char* TEXT_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform vec2 uRes;
void main() { gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0); }
)";
inline const char* TEXT_FS = R"(#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main() { fragColor = uColor; }
)";

// Text coverage (R ink, G accent, B alpha, A scale / 8) inked onto the paper: a small blur and a noisy threshold
// round the bitmap font's corners into brushed letterforms, with a faint soak around them.
inline const char* TEXT_INK_FS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uText;
uniform vec2 uRes;
uniform vec3 uInkCol, uAccCol;
void main() {
    vec2 px = 1.0 / uRes;
    vec4 m = vec4(0.0);
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) m = max(m, texture(uText, vUV + vec2(i, j) * px * 2.0));
    if (m.b <= 0.0) discard;
    float r = max(0.6, m.a * 8.0 * 0.32);
    vec2 cov = vec2(0.0);
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            float w = (i == 0 ? 2.0 : 1.0) * (j == 0 ? 2.0 : 1.0) / 16.0;
            cov += w * texture(uText, vUV + vec2(i, j) * px * r).rg;
        }
    float n = (vnoise(gl_FragCoord.xy * 0.55) - 0.5) * 0.14 + (hash12(gl_FragCoord.xy) - 0.5) * 0.06;
    vec2 e = smoothstep(vec2(0.30), vec2(0.56), cov + n) + 0.09 * cov;
    vec3 ab = m.b * (min(e.x, 1.0) * (1.0 - uInkCol) + min(e.y, 1.0) * (1.0 - uAccCol));
    fragColor = vec4(clamp(ab, 0.0, 1.0), 1.0);
}
)";

} // namespace sumi_shaders
