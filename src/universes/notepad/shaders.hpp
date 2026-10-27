#pragma once
// NOTEPAD's shaders. Everything is drawn in a multiplicative "ink space": the page starts as the paper (or, on a
// blackboard, its complement) and every mark multiplies it by its ink; the post pass turns it back (a blackboard
// is shown as 1 - S, so chalk lightens the slate like a screen blend). The alpha channel counts how much of the
// board (stack, piece, previews, HUD) covers a pixel, for the bridge.

namespace notepad_shaders {

inline const char* NOISE_GLSL = R"(
float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash21(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec2 hash22(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973)); p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), u.x), mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), u.x), u.y);
}
float vnoise1(float x) { float i = floor(x), f = fract(x); float u = f * f * (3.0 - 2.0 * f); return mix(hash11(i), hash11(i + 1.0), u); }
float fbm(vec2 p) { return (0.5 * vnoise(p) + 0.25 * vnoise(p * 2.03 + 7.1) + 0.125 * vnoise(p * 4.01 + 3.3)) / 0.875; }
)";

inline const char* FULLSCREEN_VS = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// ---------------------------------------------------------------- the page
// The paper: tone, grain, the printed grid (and its major lines), the margin rules, a soft light and the corner
// lifting a little; times the residue of everything erased.
inline const char* PAPER_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uResidue;
uniform vec4 uView;     // center, half extents (board units)
uniform float uPx;      // board units per pixel
uniform vec3 uPaper, uGrid, uMargin;
uniform vec4 uGridP;    // alpha, major every N (0: none), major alpha factor, line width (px)
uniform vec4 uMarginP;  // x, count, alpha, -
uniform float uLight, uWorld, uGrain, uAspect;
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    vec3 c = uPaper;
    // the printed grid, a little uneven
    float lw = uGridP.w * uPx * 0.5;
    vec2 g = abs(fract(p + 0.5) - 0.5);
    float line = 1.0 - smoothstep(lw, lw + uPx, min(g.x, g.y));
    float ga = uGridP.x * (0.8 + 0.4 * vnoise(p * 0.6 + 3.0));
    if (uGridP.y > 0.5) {
        vec2 gm = abs(fract(p / uGridP.y + 0.5) - 0.5) * uGridP.y;
        float major = 1.0 - smoothstep(lw * 1.3, lw * 1.3 + uPx, min(gm.x, gm.y));
        ga = mix(ga, min(1.0, ga * uGridP.z), major);
        line = max(line, major);
    }
    c = mix(c, uGrid, line * ga * uWorld);
    // the margin rules
    for (int i = 0; i < 2; i++) {
        if (float(i) >= uMarginP.y) break;
        float d = abs(p.x - (uMarginP.x - float(i) * 0.16));
        float m = 1.0 - smoothstep(0.7 * uPx, 1.6 * uPx, d);
        c = mix(c, uMargin, m * uMarginP.z * (0.85 + 0.15 * vnoise(vec2(p.y * 2.0, 5.0))) * uWorld);
    }
    // paper tooth
    float gr = 0.6 * vnoise(p * 23.0) + 0.4 * vnoise(p * 61.0 + 9.0);
    c *= 1.0 - uGrain * (gr - 0.5);
    // a soft light from the top left, the bottom right corner lifting off the desk
    vec2 q = vec2(vUv.x * uAspect, vUv.y);
    c *= 1.0 - 0.06 * smoothstep(0.2, 1.6, length(q - vec2(0.3 * uAspect, 0.85)));
    vec2 k = vec2((1.0 - vUv.x) * uAspect, vUv.y);
    float corner = (k.x + k.y) / 0.32;
    c *= 1.0 - 0.07 * (1.0 - smoothstep(0.0, 1.0, corner)) * uWorld;
    c *= 1.0 + 0.03 * smoothstep(0.5, 1.0, corner) * (1.0 - smoothstep(1.0, 1.4, corner)) * uWorld;
    vec3 S = uLight > 0.5 ? c : 1.0 - c;
    vec3 R = mix(vec3(1.0), texture(uResidue, vUv).rgb, uWorld);
    frag = vec4(S * R, 0.0);
}
)";

// ---------------------------------------------------------------- marks
inline const char* MARK_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aHead;
layout(location = 2) in vec4 aP;
layout(location = 3) in vec4 aQ;
layout(location = 4) in vec4 aC;
layout(location = 5) in vec4 aE;
layout(location = 6) in vec4 aF;
uniform mat4 uVP;
uniform float uPx, uSmearLen;
out vec2 vW;
flat out vec4 vHead, vP, vQ, vC, vE, vF;
void main() {
    float kind = mod(aHead.x, 10.0);
    vec2 w;
    float extra = uSmearLen + 3.0 * uPx;
    if (kind < 0.5) {
        vec2 a = aP.xy, b = aP.zw;
        vec2 d = b - a;
        float L = length(d);
        vec2 t = L > 1e-5 ? d / L : vec2(1.0, 0.0);
        vec2 n = vec2(-t.y, t.x);
        float m = aQ.x * 0.75 + abs(aQ.y) * 1.5 + abs(aE.y) * 1.5 + abs(aQ.w) + extra;
        w = (a + b) * 0.5 + t * aCorner.x * (L * 0.5 + m) + n * aCorner.y * m;
    } else {
        float m = aQ.w * 1.6 + extra + (kind > 1.5 ? 0.0 : 0.02);
        w = aP.xy + aCorner * (aP.zw + m);
    }
    vW = w;
    vHead = aHead; vP = aP; vQ = aQ; vC = aC; vE = aE; vF = aF;
    gl_Position = uVP * vec4(w, 0.0, 1.0);
}
)";

inline const char* MARK_FS = R"(#version 330 core
in vec2 vW;
flat in vec4 vHead, vP, vQ, vC, vE, vF;
out vec4 frag;
uniform float uPx;
uniform float uResidue;   // 1: a ghost written into the residue
uniform vec2 uSmear;      // the eraser's drag (board units)
uniform float uWorldFade; // the world's marks fade during a bridge

float tooth(vec2 p) { return 0.55 * vnoise(p * 29.0) + 0.3 * vnoise(p * 71.0 + 11.0) + 0.15 * hash21(floor(p / uPx)); }

// What a tool does to a line: graphite catching the paper's tooth, a ballpoint's uneven flow, a felt pen
// pooling at its edges, chalk broken by the grain, a soft smudge.
float toolCov(float tool, float cov, vec2 p, float along, float seed, float edge) {
    if (tool > 4.5 && tool < 5.5) { // coloured pencil: waxier, covers more
        float g = tooth(p);
        return cov * (0.85 + 0.15 * vnoise(vec2(along * 1.7, seed))) * smoothstep(0.0, 0.45, g + 0.55 - 0.3 * edge);
    }
    if (tool < 0.5) {
        float g = tooth(p);
        float flow = 0.8 + 0.2 * vnoise(vec2(along * 1.7, seed));
        return cov * flow * smoothstep(0.12, 0.7, g + 0.28 - 0.3 * edge);
    } else if (tool < 1.5) {
        return cov * (0.84 + 0.16 * vnoise(vec2(along * 5.0, seed * 3.0)));
    } else if (tool < 2.5) {
        return cov * (0.8 + 0.2 * edge) * (0.9 + 0.1 * vnoise(vec2(along * 13.0, seed)));
    } else if (tool < 3.5) {
        float g = tooth(p * 0.75);
        return cov * smoothstep(0.3, 0.62, g + 0.3 - 0.3 * edge) * (0.85 + 0.15 * vnoise(vec2(along * 3.0, seed)));
    } else if (tool < 4.5) {
        return cov;
    }
    return cov * (0.85 + 0.15 * edge); // highlighter
}

void main() {
    float kind = mod(vHead.x, 10.0);
    bool world = vHead.x > 9.5;
    float tool = vHead.y, seed = vHead.z;
    vec2 p = vW;
    if (uResidue > 0.5) p -= uSmear * (vnoise(vW * 1.3 + seed * 3.1) * 1.2 - 0.2);
    float cov = 0.0;
    float along = 0.0;
    float edge = 0.4;
    float aa = uPx * (0.8 + uResidue * 0.7);
    if (kind < 0.5) {
        // ---- a stroke
        vec2 a = vP.xy, b = vP.zw;
        vec2 d = b - a;
        float L = length(d);
        vec2 t = L > 1e-5 ? d / L : vec2(1.0, 0.0);
        vec2 n = vec2(-t.y, t.x);
        vec2 rel = p - a;
        float u = dot(rel, t), v = dot(rel, n);
        float s = L > 1e-5 ? u / L : 0.5;
        if (s > vQ.z + 0.002) discard; // drawn up to here so far
        // a polyline's inner joints: flat cuts, no taper, the wobble pinned to the shared point
        bool js = mod(vE.z, 2.0) > 0.5, je = vE.z > 1.5;
        if ((js && u < 0.0) || (je && u > L)) discard;
        float env = (js ? smoothstep(0.0, 0.35, s) : 1.0) * (je ? smoothstep(1.0, 0.65, s) : 1.0);
        float wob = env * (vQ.y * (vnoise1(u * 1.6 + seed * 13.1) * 2.0 - 1.0)
                  + vQ.y * 0.35 * (vnoise1(u * 6.1 + seed * 7.7) * 2.0 - 1.0)
                  + vE.y * (vnoise1(u * 2.4 + vE.x * 31.7) * 2.0 - 1.0))
                  + vQ.w * sin(3.14159 * clamp(s, 0.0, 1.0));
        float dv = v - wob;
        float du = max(-u, 0.0) + max(u - L, 0.0);
        bool tapered = (tool > 0.5 && tool < 1.5) || tool < 0.5 || (tool > 4.5 && tool < 5.5);
        float taper = tapered ? mix(0.6, 1.0, (js ? 1.0 : smoothstep(0.0, 0.1, s)) * (je ? 1.0 : smoothstep(1.0, 0.9, s))) : 1.0;
        float hw = max(vQ.x * 0.5 * taper, 0.55 * uPx) * (1.0 + 0.15 * uResidue);
        float dist = length(vec2(du, dv));
        if (tool > 3.5 && tool < 4.5) {
            cov = exp(-dist * dist / (hw * hw)) * (0.6 + 0.4 * vnoise(vec2(u * 3.0, v * 9.0) + seed));
        } else {
            cov = 1.0 - smoothstep(hw - aa, hw + aa, dist);
        }
        if (cov <= 0.002) discard;
        if (vC.w > 0.0) cov *= step(fract(u / vC.w + seed), 0.58);
        along = u;
        edge = clamp(dist / hw, 0.0, 1.0);
    } else {
        // ---- a coloured-in area: a cell (its open sides wobble over and short of the outline) or a pie
        vec2 c = vP.xy, h = vP.zw;
        vec2 r = p - c;
        float over = vQ.w;
        float ang = vQ.y, sp = max(vQ.z, 0.01);
        float ca = cos(ang), sa = sin(ang);
        vec2 q = vec2(ca * p.x + sa * p.y, -sa * p.x + ca * p.y); // pattern space
        float pat = vQ.x;
        float k = floor(q.y / sp + 0.5);
        float region;
        if (kind < 1.5) {
            int m = int(vC.w + 0.5);
            float lj = pat < 2.5 ? (hash11(k * 1.37 + seed * 7.0) - 0.5) * 2.2 * over : 0.0;
            float inset = vE.z;
            float oL = (m & 1) != 0 ? 0.0 : -inset + over * (vnoise1(p.y * 2.7 + seed * 3.3) * 2.0 - 0.9) + lj;
            float oR = (m & 2) != 0 ? 0.0 : -inset + over * (vnoise1(p.y * 2.9 + seed * 5.1) * 2.0 - 0.9) + lj;
            float oD = (m & 4) != 0 ? 0.0 : -inset + over * (vnoise1(p.x * 2.8 + seed * 2.3) * 2.0 - 0.9) + lj;
            float oU = (m & 8) != 0 ? 0.0 : -inset + over * (vnoise1(p.x * 2.6 + seed * 4.7) * 2.0 - 0.9) + lj;
            float dx0 = r.x + h.x + oL, dx1 = h.x + oR - r.x, dy0 = r.y + h.y + oD, dy1 = h.y + oU - r.y;
            float rx0 = (m & 1) != 0 ? step(0.0, dx0) : smoothstep(-aa, aa, dx0);
            float rx1 = (m & 2) != 0 ? step(0.0, dx1) : smoothstep(-aa, aa, dx1);
            float ry0 = (m & 4) != 0 ? step(0.0, dy0) : smoothstep(-aa, aa, dy0);
            float ry1 = (m & 8) != 0 ? step(0.0, dy1) : smoothstep(-aa, aa, dy1);
            region = rx0 * rx1 * ry0 * ry1;
        } else {
            float R = h.x + over * (vnoise1(atan(r.y, r.x) * 3.0 + seed) * 2.0 - 0.8);
            float dist = length(r);
            region = 1.0 - smoothstep(R - aa, R + aa, dist);
            float a = atan(r.x, r.y);
            if (a < 0.0) a += 6.28318;
            region *= step(a, vE.z * 6.28318);
        }
        if (region <= 0.0) discard;
        // drawn so far: the strokes go across from one side
        if (vE.x < 0.999) {
            float span = (abs(ca) + abs(sa)) * max(h.x, h.y) * 1.05;
            float rq = -sa * r.x + ca * r.y;
            if ((rq / span) * 0.5 + 0.5 > vE.x) discard;
        }
        float lw = sp * 0.42 * vE.y;
        float pc;
        if (pat < 0.5) { // hatching
            float local = q.y - k * sp + sp * 0.2 * (vnoise1(q.x * 1.2 + k * 3.7 + seed) * 2.0 - 1.0);
            pc = 1.0 - smoothstep(lw * 0.5 - aa, lw * 0.5 + aa, abs(local));
            along = q.x;
        } else if (pat < 1.5) { // cross-hatching
            float local = q.y - k * sp + sp * 0.2 * (vnoise1(q.x * 1.2 + k * 3.7 + seed) * 2.0 - 1.0);
            float h1 = 1.0 - smoothstep(lw * 0.5 - aa, lw * 0.5 + aa, abs(local));
            vec2 q2 = vec2(q.y, -q.x);
            float k2 = floor(q2.y / sp + 0.5);
            float local2 = q2.y - k2 * sp + sp * 0.2 * (vnoise1(q2.x * 1.2 + k2 * 5.3 + seed) * 2.0 - 1.0);
            float h2 = 1.0 - smoothstep(lw * 0.45 - aa, lw * 0.45 + aa, abs(local2));
            pc = 1.0 - (1.0 - h1) * (1.0 - 0.85 * h2);
            along = q.x + q.y;
        } else if (pat < 2.5) { // scribble: loops going back and forth
            float w = q.y / sp + 0.55 * sin(q.x / sp * 1.9 + seed * 3.0 + 1.7 * vnoise1(q.x * 0.8 + seed));
            float local = (w - floor(w + 0.5)) * sp;
            pc = 1.0 - smoothstep(lw * 0.45 - aa, lw * 0.45 + aa, abs(local));
            along = q.x;
        } else if (pat < 3.5) { // felt pen: wide overlapping bands, darker where they overlap
            float bw = sp * 1.35;
            float k0 = floor(q.y / sp);
            float acc = 1.0;
            for (int i = 0; i < 2; i++) {
                float kk = k0 + float(i);
                float centre = (kk + 0.5) * sp + sp * 0.12 * (vnoise1(q.x * 1.5 + kk * 2.1 + seed) * 2.0 - 1.0);
                float cb = 1.0 - smoothstep(bw * 0.5 - aa, bw * 0.5 + aa, abs(q.y - centre));
                cb *= 0.88 + 0.12 * vnoise(vec2(q.x * 18.0, kk * 3.0 + seed));
                acc *= 1.0 - 0.72 * cb;
            }
            pc = (1.0 - acc) / 0.72;
            along = q.x;
        } else if (pat < 4.5) { // stipple
            vec2 gq = q / (sp * 0.85);
            vec2 gi = floor(gq);
            vec2 f = fract(gq) - 0.5 - (hash22(gi + seed) - 0.5) * 0.5;
            float rr = 0.2 + 0.08 * hash21(gi + 3.0);
            float dd = length(f) * sp * 0.85;
            pc = 1.0 - smoothstep(rr * sp * 0.85 - aa, rr * sp * 0.85 + aa, dd);
            along = q.x;
        } else { // shading with the side of the lead or the chalk
            pc = 0.5 + 0.38 * vnoise(vec2(q.x * 1.6, q.y * 16.0) + seed) + 0.12 * vnoise(q * 5.0);
            along = q.x;
        }
        // an even layer of colour under the lines (a pencil's side, chalk rubbed in): the cell reads as coloured in
        if (kind < 1.5 && pat < 2.5 && vE.w > 0.0)
            pc = 1.0 - (1.0 - pc) * (1.0 - vE.w * (0.8 + 0.2 * vnoise(q * vec2(1.3, 9.0) + seed)));
        cov = region * pc;
        edge = 0.3;
    }
    cov = toolCov(tool, cov, p, along, seed, edge);
    // being rubbed out: the eraser goes back and forth, the marks thin out in streaks
    if (vF.x > 0.0) {
        float e = vF.x;
        float n = 0.65 * vnoise(vec2(p.x * 2.5, p.y * 11.0) + vF.y) + 0.35 * vnoise(p * 7.0 + vF.y);
        cov *= smoothstep(e - 0.15, e + 0.05, n * 0.95 + 0.05) * (1.0 - 0.6 * e);
    }
    if (uResidue > 0.5) {
        // what an eraser leaves: broken, streaky remains, thicker where it pressed less
        vec2 sd = normalize(uSmear + vec2(1e-4, 0.0));
        vec2 sp = vec2(dot(vW, sd), dot(vW, vec2(-sd.y, sd.x)));
        float streak = vnoise(vec2(sp.x * 1.5, sp.y * 26.0) + seed);
        float lo = kind < 0.5 ? 0.3 : 0.42;
        cov *= smoothstep(lo, 0.8, fbm(vW * 2.3 + seed * 1.3)) * (0.3 + 0.7 * streak);
    }
    float A = clamp(cov * vHead.w, 0.0, 1.0);
    if (world) A *= uWorldFade;
    if (A <= 0.002) discard;
    frag = vec4(mix(vec3(1.0), vC.rgb, A), world ? 0.0 : A);
}
)";

// ---------------------------------------------------------------- the eraser going over the residue again
inline const char* FADE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform float uK, uSeed;
uniform vec4 uView;
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    float n = 0.7 * vnoise(p * 0.8 + uSeed) + 0.3 * vnoise(vec2(p.x * 3.0, p.y * 9.0) + uSeed * 1.7);
    frag = vec4(1.0, 1.0, 1.0, clamp(uK * mix(0.3, 1.7, n), 0.0, 1.0));
}
)";

// ---------------------------------------------------------------- handwriting
inline const char* TEXT_VS = R"(#version 330 core
layout(location = 0) in vec4 aPos; // pixels (top-left origin), uv
layout(location = 1) in vec4 aCol; // ink, alpha
uniform vec2 uRes;
out vec2 vUv;
out vec4 vCol;
void main() {
    vUv = aPos.zw;
    vCol = aCol;
    gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
}
)";
inline const char* TEXT_FS = R"(#version 330 core
in vec2 vUv;
in vec4 vCol;
out vec4 frag;
uniform sampler2D uFont;
uniform float uResidue, uBias;
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * (0.75 + uResidue * 0.6);
    float cov = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    cov *= 0.86 + 0.14 * vnoise(gl_FragCoord.xy * 0.35);
    if (uResidue > 0.5) cov *= smoothstep(0.15, 0.85, vnoise(gl_FragCoord.xy * 0.05));
    float A = clamp(cov * vCol.a, 0.0, 1.0);
    if (A <= 0.002) discard;
    frag = vec4(mix(vec3(1.0), vCol.rgb, A), A);
}
)";

// ---------------------------------------------------------------- the print
inline const char* POST_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene;
uniform float uLight, uFade, uDim, uBridge;
uniform vec3 uBridgeCol, uBridgeInk;
void main() {
    vec4 s = texture(uScene, vUv);
    vec3 c = uLight > 0.5 ? s.rgb : 1.0 - s.rgb;
    float cov = clamp(s.a, 0.0, 1.0);
    c = mix(c, mix(uBridgeCol, uBridgeInk, cov), uBridge);
    c *= 1.0 - 0.45 * uDim;
    c *= uFade;
    frag = vec4(pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
)";

} // namespace notepad_shaders
