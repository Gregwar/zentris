#pragma once
// LIGHTSOURCE's shaders (see lightsource.cpp).
//
// Passes, all in board units on a flat orthographic view:
//  - light: a reduced-resolution HDR buffer (two targets): the light every point of the board plane receives from the
//    lamps (the falling piece, the pieces dying out, a cleared row burning, the side lights of a peak), with real 2D
//    shadows: each lamp is an area light (its cells, sampled several times each), and every sample is tested for
//    occlusion against the stack, row by row (one 10-bit mask per row: the rows can be in the middle of falling after
//    a clear, their visual heights are given), a cell's own block ignored. The second target keeps the light's mean
//    direction (for the blocks' shading).
//  - scene: the room (a dark plaster wall, the floor, two rails marking the well) lit by the ambient and that buffer,
//    then the blocks (instanced cells: the stack lit by the buffer through their bevels' normals, the lamps
//    emitting, the ghost's outline, the previews, the burning cells of a cleared row), then the glows (additive).
//  - final: a soft shoulder, sRGB, dim and fade, before the HUD.

namespace lightsource_shaders {

inline const char* COMMON_GLSL = R"(
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
float fbm3(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) { s += a * vnoise(p); p = p * 2.07 + vec2(17.1, 9.7); a *= 0.5; }
    return s / 0.875;
}
// the stack, row by row: the visual height of each game row (they fall after a clear) and its occupied columns
uniform float uRowY[22];
uniform int uRowMask[22];
uniform int uRowMin;     // the first row holding a block (22: none)
uniform float uTopY;     // the top of that row
// the cell (column, row) holding p, or (-1, -1)
ivec2 cellAt(vec2 p) {
    if (p.x < -5.0 || p.x >= 5.0 || uRowMin >= 22 || p.y > uTopY) return ivec2(-1);
    int c = int(floor(p.x + 5.0));
    for (int r = uRowMin; r < 22; r++) {
        float ry = uRowY[r];
        if (p.y > ry + 0.5) break;
        if (p.y >= ry - 0.5) return ((uRowMask[r] >> c) & 1) != 0 ? ivec2(c, r) : ivec2(-1);
    }
    return ivec2(-1);
}
// light buffer, softened (a small rotated-grid blur hides the samples' pattern)
vec4 soft(sampler2D t, vec2 uv, vec2 texel) {
    vec4 s = texture(t, uv) * 0.2;
    s += (texture(t, uv + texel * vec2(1.0, 0.4)) + texture(t, uv + texel * vec2(-0.4, 1.0))
        + texture(t, uv + texel * vec2(-1.0, -0.4)) + texture(t, uv + texel * vec2(0.4, -1.0))) * 0.12;
    s += (texture(t, uv + texel * vec2(2.2, 1.6)) + texture(t, uv + texel * vec2(-1.6, 2.2))
        + texture(t, uv + texel * vec2(-2.2, -1.6)) + texture(t, uv + texel * vec2(1.6, -2.2))) * 0.08;
    return s;
}
)";

// ---------------------------------------------------------------- the light
inline const char* LIGHT_FS = R"(#version 330 core
in vec2 vUv;
layout(location = 0) out vec4 outLight;
layout(location = 1) out vec4 outDir;
#define MAXL 12
#define MAXP 96
uniform vec4 uView;           // center, half extents (board units)
uniform int uNL;
uniform vec4 uLColor[MAXL];   // rgb power, a: reach (the far term's length)
uniform vec4 uLParam[MAXL];   // x: first point, y: points, z: samples per point (1 or 4), w: spread
uniform vec4 uLParam2[MAXL];  // x: height above the plane, y: near weight (the close pool vs the far reach)
uniform vec4 uLOwn[MAXL * 2]; // its own cells (column, row) x 4, ignored as occluders (-1: none)
uniform vec2 uPts[MAXP];
uniform vec4 uLAim[MAXL];
#define MAXG 48
uniform int uNG;
uniform vec4 uGPos[MAXG];     // unshadowed glows: position, radius
uniform vec4 uGCol[MAXG];     // a spot: its aim (xy), the cosines of its cone's outer and inner edges (z = 0: all around)

ivec2 ownC[4];
bool blocked(vec2 a, vec2 b, ivec2 self) {
    float y0 = min(a.y, b.y), y1 = max(a.y, b.y);
    if (y0 > uTopY) return false;
    float dx = b.x - a.x, dy = b.y - a.y;
    for (int r = uRowMin; r < 22; r++) {
        float ry = uRowY[r];
        if (ry - 0.5 > y1) continue;
        if (ry + 0.5 < y0) break;
        int m = uRowMask[r];
        if (self.y == r) m &= ~(1 << self.x);
        for (int k = 0; k < 4; k++) if (ownC[k].y == r) m &= ~(1 << ownC[k].x);
        if (m == 0) continue;
        float ta = 0.0, tb = 1.0;
        if (abs(dy) > 1e-5) {
            ta = (ry - 0.5 - a.y) / dy; tb = (ry + 0.5 - a.y) / dy;
            if (ta > tb) { float t = ta; ta = tb; tb = t; }
            ta = max(ta, 0.0); tb = min(tb, 1.0);
            if (ta > tb) continue;
        }
        float xa = a.x + dx * ta + 5.0, xb = a.x + dx * tb + 5.0;
        float lo = min(xa, xb), hi = max(xa, xb);
        if (hi < 0.0 || lo >= 10.0) continue;
        int c0 = clamp(int(floor(lo)), 0, 9), c1 = clamp(int(floor(hi)), 0, 9);
        int bits = ((2 << c1) - 1) & ~((1 << c0) - 1);
        if ((m & bits) != 0) return true;
    }
    return false;
}

void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    ivec2 self = cellAt(p);
    // a rotated 2x2 pattern per pixel (interleaved gradient noise): the area lights' samples
    float ang = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
    const vec2 OFF[4] = vec2[4](vec2(-0.25, -0.25), vec2(0.25, -0.25), vec2(-0.25, 0.25), vec2(0.25, 0.25));
    vec3 irr = vec3(0.0);
    vec2 dirSum = vec2(0.0);
    float wSum = 0.0;
    for (int li = 0; li < uNL; li++) {
        vec4 P = uLParam[li], P2 = uLParam2[li], C = uLColor[li];
        int p0 = int(P.x), pc = int(P.y), sub = int(P.z);
        float spread = P.w, h = P2.x, nearW = P2.y, reach = C.a;
        vec4 o0 = uLOwn[li * 2], o1 = uLOwn[li * 2 + 1];
        ownC[0] = ivec2(o0.xy); ownC[1] = ivec2(o0.zw); ownC[2] = ivec2(o1.xy); ownC[3] = ivec2(o1.zw);
        float acc = 0.0;
        vec2 dacc = vec2(0.0);
        float h2 = h * h;
        vec4 aim = uLAim[li];
        float cone = 1.0;
        if (aim.z > 0.0) {
            cone = smoothstep(aim.z, aim.w, dot(normalize(p - uPts[p0]), aim.xy));
            if (cone <= 0.001) continue;
        }
        for (int k = 0; k < pc; k++) {
            vec2 c = uPts[p0 + k];
            // the whole cell too faint here: skip its shadow tests
            vec2 dc = c - p;
            float dd0 = dot(dc, dc);
            float est = nearW * h2 * h / pow(dd0 + h2, 1.5) + (1.0 - nearW) * exp(-sqrt(dd0) / reach);
            if (est < 0.0015) continue;
            for (int s = 0; s < sub; s++) {
                vec2 sp = sub == 1 ? c : c + rot * OFF[s] * (spread * 2.0);
                vec2 d = sp - p;
                float dd = dot(d, d);
                float fall = nearW * h2 * h / pow(dd + h2, 1.5) + (1.0 - nearW) * exp(-sqrt(dd) / reach);
                if (blocked(p, sp, self)) continue;
                acc += fall;
                dacc += d * inversesqrt(dd + 1e-4) * fall;
            }
        }
        acc *= cone / float(sub);
        dacc *= cone / float(sub);
        irr += C.rgb * acc;
        float l = dot(C.rgb, vec3(0.3, 0.5, 0.2));
        dirSum += dacc * l;
        wSum += acc * l;
    }
    for (int gi = 0; gi < uNG; gi++) {
        vec2 d = uGPos[gi].xy - p;
        float r = uGPos[gi].z;
        float dd = dot(d, d);
        if (dd > r * r * 16.0) continue;
        float fall = r * r * r / pow(dd + r * r, 1.5);
        vec3 C = uGCol[gi].rgb;
        irr += C * fall;
        float l = dot(C, vec3(0.3, 0.5, 0.2)) * fall;
        dirSum += d * inversesqrt(dd + 1e-4) * l;
        wSum += l;
    }
    outLight = vec4(irr, 1.0);
    outDir = vec4(dirSum, wSum, 1.0);
}
)";

// ---------------------------------------------------------------- the room
inline const char* SCENE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uLight;
uniform vec2 uLightTexel;
uniform vec4 uView;
uniform float uPx;
uniform vec3 uWall, uFloor, uRail, uAmbient;
uniform float uFloorY;
uniform float uBridge;
uniform vec3 uBridgeColor;
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    // the wall: a dark plaster, a soft large variation and a fine grain
    float big = fbm3(p * 0.16 + vec2(3.1, 7.7));
    vec3 alb = uWall * (0.9 + 0.2 * big) * (0.985 + 0.03 * hash12(floor(p / uPx)));
    // the ambient: a little stronger around the well (the room's fill), fading toward the corners
    float fill = 0.72 + 0.4 * exp(-dot(p * vec2(0.055, 0.075), p * vec2(0.055, 0.075)));
    float ambK = fill;
    // the floor under the well: a darker ground, its lip catching the light
    if (p.y < uFloorY) {
        float depth = uFloorY - p.y;
        alb = uFloor * (0.92 + 0.12 * big) * (0.985 + 0.03 * hash12(floor(p / uPx)));
        alb *= mix(1.0, 0.75, smoothstep(0.0, 2.0, depth));
        ambK *= 0.85;
        alb = mix(alb, uRail * 0.9, 1.0 - smoothstep(0.03, 0.07 + uPx, depth));
    }
    // the rails standing at the well's sides
    float rx = abs(abs(p.x) - 5.09);
    if (p.y > uFloorY && p.y < 10.02) alb = mix(alb, uRail, 1.0 - smoothstep(0.035, 0.035 + uPx * 1.2, rx));
    vec3 irr = soft(uLight, vUv, uLightTexel).rgb;
    // a gap between two blocks: their contact shadow
    ivec2 cell = cellAt(p);
    if (cell.x >= 0) { irr *= 0.0; ambK *= 0.35; }
    vec3 c = alb * (uAmbient * ambK + irr);
    c = mix(c, uBridgeColor, uBridge);
    frag = vec4(c, 1.0);
}
)";

// ---------------------------------------------------------------- the blocks (instanced cells)
//   a0: pos xy, scale, alpha      a1: albedo rgb, kind      a2: emission rgb (HDR), connections (L1 R2 U4 D8)
//   a3: seed, burn progress (0..1), hot (emission's white-hot share), unused
// kind 0: a block (the stack, the lamps, the falling piece), lit by the light buffer, plus its emission
// kind 1: the ghost: the piece's outline only, unlit
// kind 2: a preview (hold, next): lit by a soft light of its own
// kind 3: a burning cell (a cleared row): flaring into light, eaten from its edges, gone
inline const char* BLOCK_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aA0;
layout(location = 2) in vec4 aA1;
layout(location = 3) in vec4 aA2;
layout(location = 4) in vec4 aA3;
uniform mat4 uVP;
out vec2 vQ;
flat out vec4 vA0, vA1, vA2, vA3;
void main() {
    vA0 = aA0; vA1 = aA1; vA2 = aA2; vA3 = aA3;
    vQ = aCorner * 0.5;
    vec2 w = aA0.xy + aCorner * 0.5 * aA0.z;
    gl_Position = uVP * vec4(w, 0.0, 1.0);
}
)";
inline const char* BLOCK_FS = R"(#version 330 core
in vec2 vQ;
flat in vec4 vA0, vA1, vA2, vA3;
out vec4 frag;
uniform sampler2D uLight, uLightDir;
uniform vec2 uLightTexel, uRes;
uniform float uPx;
uniform vec3 uAmbient;
uniform float uAmbBlock;
uniform vec3 uGhost;
uniform float uBridge;
uniform vec3 uBridgeInk;
float sdBox(vec2 p, vec2 b, float r) { vec2 d = abs(p) - b + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }
vec2 gradBox(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + r;
    vec2 g = max(d.x, d.y) > 0.0 ? normalize(max(d, 1e-5)) : (d.x > d.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    return g * sign(p + 1e-6);
}
void main() {
    float kind = vA1.w, scale = vA0.z;
    int conn = int(vA2.w + 0.5);
    vec2 q = vQ;
    float g = kind > 0.5 && kind < 1.5 ? 0.065 : 0.035;
    vec2 lo = vec2((conn & 1) != 0 ? -0.62 : -0.5 + g, (conn & 8) != 0 ? -0.62 : -0.5 + g);
    vec2 hi = vec2((conn & 2) != 0 ? 0.62 : 0.5 - g, (conn & 4) != 0 ? 0.62 : 0.5 - g);
    vec2 c = (lo + hi) * 0.5, he = (hi - lo) * 0.5;
    float rad = 0.1;
    vec2 qc = q - c;
    float sd = sdBox(qc, he, rad);
    // a concave corner of the piece (both sides in the piece, the diagonal not): carve the gap's corner square,
    // so that the contour turns cleanly there (no notch, no step)
    {
        float cg = kind > 0.5 && kind < 1.5 ? 0.065 : g;
        if ((conn & 5) == 5 && (conn & 16) == 0) sd = max(sd, -sdBox(q - vec2(-0.5, 0.5), vec2(cg), 0.0));
        if ((conn & 6) == 6 && (conn & 32) == 0) sd = max(sd, -sdBox(q - vec2(0.5, 0.5), vec2(cg), 0.0));
        if ((conn & 9) == 9 && (conn & 64) == 0) sd = max(sd, -sdBox(q - vec2(-0.5, -0.5), vec2(cg), 0.0));
        if ((conn & 10) == 10 && (conn & 128) == 0) sd = max(sd, -sdBox(q - vec2(0.5, -0.5), vec2(cg), 0.0));
    }
    float aa = uPx / max(scale, 1e-3);
    vec3 alb = vA1.rgb, em = vA2.rgb;
    if (kind > 0.5 && kind < 1.5) {
        // the ghost: a thin outline just inside the piece's contour
        // (the ghost's shape is inset by 0.065: the line runs along its edge)
        float line = 1.0 - smoothstep(0.0, aa * 1.2, abs(sd + 0.012) - 0.012);
        float a = line * vA0.w;
        vec3 col = mix(uGhost, uBridgeInk, uBridge);
        frag = vec4(col * a, a);
        return;
    }
    if (kind > 2.5) {
        // a burning cell: white-hot at once, then eaten from its edges by a ragged front while it cools
        float t = vA3.y;
        float n = vnoise(q * 7.0 + vA3.x * 31.0) * 0.6 + vnoise(q * 15.0 + vA3.x * 17.0) * 0.4;
        float eat = smoothstep(0.1, 1.0, t) * 0.5;
        float sdb = sd + eat - (n - 0.5) * 0.07 * smoothstep(0.05, 0.4, t);
        float cov = smoothstep(aa, -aa, sdb);
        float heat = 1.0 - smoothstep(0.0, 0.9, t);
        vec3 hot = mix(em, vec3(1.0, 0.97, 0.92) * max(max(em.r, em.g), em.b), vA3.z * (1.0 - smoothstep(0.0, 0.35, t)));
        vec3 col = hot * (0.25 + 0.75 * heat) * mix(vec3(1.0), vec3(1.0, 0.55, 0.3), smoothstep(0.3, 1.0, t));
        float a = cov * vA0.w;
        col = mix(col, uBridgeInk, uBridge);
        frag = vec4(col * a, a);
        return;
    }
    float cov = smoothstep(aa, -aa, sd) * vA0.w;
    if (cov <= 0.002) discard;
    // the bevel: a narrow rounded edge, its normal tilting outward
    float e = -sd;
    float bev = 1.0 - smoothstep(0.0, 0.085, e);
    vec2 gr = gradBox(qc, he, rad);
    vec3 N = normalize(vec3(gr * bev * 1.3, 1.0));
    vec3 col;
    if (kind > 1.5) {
        // a preview: a soft light of its own from the upper left, its emission (the next piece warming up)
        vec3 L = normalize(vec3(-0.5, 0.7, 0.8));
        float ndl = max(dot(N, L), 0.0);
        col = alb * (uAmbient * uAmbBlock * 1.5 + vec3(0.16) * (0.35 + ndl)) + em;
    } else {
        vec2 uv = gl_FragCoord.xy / uRes;
        vec3 irr = soft(uLight, uv, uLightTexel).rgb;
        vec3 dd = soft(uLightDir, uv, uLightTexel).xyz;
        vec2 L2 = dd.z > 1e-6 ? dd.xy / dd.z : vec2(0.0);
        float spreadL = length(L2);
        vec3 L = normalize(vec3(L2, 0.5));
        float ndl = max(dot(N, L), 0.0) / L.z;
        float lit = mix(1.0, ndl, min(1.0, spreadL * 1.3));
        // the ambient fill from above: the bevels give the block its form even in the dark
        float amb = uAmbBlock * (0.85 + 0.35 * N.y + 0.1 * N.x * -1.0);
        // a faint sheen where the light grazes the bevel
        float sheen = pow(max(dot(N, L), 0.0), 12.0) * bev;
        // a lamp is lit by its own other cells: keep its face its own color (no blown albedo)
        float self = clamp(dot(em, vec3(0.3, 0.5, 0.2)) * 2.5, 0.0, 1.0);
        col = alb * (uAmbient * amb + irr * lit * (1.0 - 0.85 * self)) + irr * sheen * 0.25 * (1.0 - self);
        // the emission: a lamp's whole face, a hot core, the edge a little deeper in color
        float core = smoothstep(0.42, 0.0, length(qc / max(he, vec2(0.2))) * 0.5);
        vec3 emc = em * (0.8 + 0.35 * core) * mix(1.0, 0.8, bev);
        emc = mix(emc, vec3(dot(em, vec3(0.3, 0.5, 0.2))) * 1.15, vA3.z * core * 0.5);
        col += emc;
    }
    col = mix(col, uBridgeInk, uBridge);
    frag = vec4(col * cov, cov);
}
)";

// ---------------------------------------------------------------- glows (instanced quads, additive)
//   a0: a xy, b xy        a1: w, intensity, extra, extra        a2: color rgb, kind
// kind 0: a soft glow (a disc at a, radius w)
// kind 1: a glow along a segment a -> b, width w (a lock's contact)
// kind 2: a ring (the bonus gauge: center a, radius b.x, thickness b.y, fill a1.z, ready a1.w)
inline const char* FX_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aA0;
layout(location = 2) in vec4 aA1;
layout(location = 3) in vec4 aA2;
layout(location = 4) in vec4 aA3;
uniform mat4 uVP;
out vec2 vP;
flat out vec4 vA0, vA1, vA2;
void main() {
    vA0 = aA0; vA1 = aA1; vA2 = aA2;
    float kind = aA2.w;
    vec2 a = aA0.xy, b = aA0.zw, w;
    if (kind < 0.5) w = a + aCorner * aA1.x * 1.6;
    else if (kind < 1.5) {
        vec2 d = b - a;
        float L = length(d);
        vec2 u = L > 1e-5 ? d / L : vec2(1.0, 0.0), v = vec2(-u.y, u.x);
        float e = aA1.x * 2.0;
        w = (a + b) * 0.5 + u * aCorner.x * (L * 0.5 + e) + v * aCorner.y * e;
    } else w = a + aCorner * (b.x + b.y * 4.0);
    vP = w;
    gl_Position = uVP * vec4(w, 0.0, 1.0);
}
)";
inline const char* FX_FS = R"(#version 330 core
in vec2 vP;
flat in vec4 vA0, vA1, vA2;
out vec4 frag;
uniform float uPx;
void main() {
    vec2 a = vA0.xy, b = vA0.zw;
    float kind = vA2.w, g = 0.0;
    if (kind < 0.5) {
        float d = length(vP - a) / vA1.x;
        g = exp(-d * d * 3.0) * vA1.y;
    } else if (kind < 1.5) {
        vec2 ab = b - a;
        float t = clamp(dot(vP - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
        float d = length(vP - (a + ab * t)) / vA1.x;
        g = exp(-d * d * 2.5) * vA1.y;
    } else {
        // the gauge: a hairline circle, its filled arc brighter (clockwise from the top), all bright when ready
        float r = length(vP - a), R = b.x, th = max(b.y, uPx * 0.8);
        float ang = atan(vP.x - a.x, vP.y - a.y);
        float u = fract(ang / 6.2831853 + 1.0);
        float ring = 1.0 - smoothstep(th * 0.5, th * 0.5 + uPx * 1.2, abs(r - R));
        float filled = step(u, vA1.z);
        g = ring * mix(0.18, 1.0, max(filled, vA1.w));
        g += vA1.w * exp(-pow((r - R) / (th * 3.0), 2.0)) * 0.25;
    }
    vec3 c = vA2.rgb * g;
    frag = vec4(c, 0.0);
}
)";

inline const char* FINAL_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene;
uniform float uMul;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    // a soft shoulder: the darks and mids exact, the hot lamps rolling off without burning to white
    // (on the luminance, so a lamp keeps its hue; what still exceeds 1 spills softly toward white)
    const float K = 0.5;
    float L = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float Lm = L < K ? L : K + (1.0 - K) * (1.0 - exp(-(L - K) / (1.0 - K)));
    c *= Lm / max(L, 1e-5);
    float mx = max(c.r, max(c.g, c.b));
    if (mx > 1.0) c = mix(c / mx, vec3(1.0), clamp((mx - 1.0) * 0.5, 0.0, 0.6));
    c *= uMul;
    c = pow(max(c, 0.0), vec3(1.0 / 2.2));
    c += (hash12(gl_FragCoord.xy) - 0.5) / 255.0;
    frag = vec4(c, 1.0);
}
)";

inline const char* TEXT_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
uniform vec2 uRes;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
}
)";
inline const char* TEXT_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uFont;
uniform vec4 uInk;
uniform float uBias;
uniform float uSoft;
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.75 + uSoft;
    float fill = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    frag = vec4(uInk.rgb * uInk.a * fill, uInk.a * fill);
}
)";
inline const char* PANEL_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform vec2 uRes;
void main() { gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0); }
)";
inline const char* PANEL_FS = R"(#version 330 core
out vec4 frag;
uniform vec4 uColor;
void main() { frag = uColor; }
)";

} // namespace lightsource_shaders
