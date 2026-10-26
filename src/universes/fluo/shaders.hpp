#pragma once
// FLUO's shaders (see fluo.cpp).
//
// Three layers: the wall (a static texture of the wall's grain, generated once per size, toned per variant in
// COMPOSITE_FS), the paint (a persistent pair of buffers the splashes are stamped into once they are done: the
// color, premultiplied linear + coverage, and the coat's thickness, for its relief; never cleared during a song; a
// "wet" pair, redrawn each frame, holds the splashes still spreading and the drips still running) and the play (the
// pieces, the ghost, the gauge, the paint in flight, specks, drawn last over the composite).

namespace fluo_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(17.1, 9.7); a *= 0.5; }
    return s / 0.96875;
}
)";

// ---------------------------------------------------------------- the wall's grain (generated once per size)
// r: broad blotches of the paint coat (rollered unevenly), g: the fine grain, b: pores / pits (concrete),
// a: a large mottle between the wall's two tones. Board units, so the wall is the same at any resolution.
inline const char* WALL_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uView; // center xy, half extents xy (board units)
uniform vec2 uRes;
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    float blotch = fbm(p * 0.22 + vec2(3.1, 7.7));
    blotch = mix(blotch, fbm(p * 0.9 + vec2(11.0, 2.0)), 0.35);
    // the grain: pixel-fine noise mixed with a slightly coarser one (a few pixels), so it reads as a surface
    vec2 px = vUv * uRes;
    float grain = 0.55 * hash12(px) + 0.45 * vnoise(p * 9.0);
    // pores: sparse small pits
    float n = vnoise(p * 5.5 + vec2(31.0, 5.0));
    float pores = smoothstep(0.78, 0.92, n) * (0.5 + 0.5 * hash12(floor(p * 5.5)));
    pores = max(pores, smoothstep(0.9, 0.98, vnoise(p * 14.0 + 3.0)) * 0.6);
    float mottle = smoothstep(0.25, 0.75, fbm(p * 0.07 + vec2(-4.0, 9.0)));
    frag = vec4(blotch, grain, pores, mottle);
}
)";

// ---------------------------------------------------------------- paint stamps
// One instanced quad per primitive of a splash, drawn into the paint buffer with premultiplied "over" blending
// (the newest paint covers the old). Stamping the same primitive again is harmless (a splash growing is stamped
// every frame of its growth).
//   a0: a xy, b xy (a capsule's ends; a blob's center in a)
//   a1: ra, rb (the capsule's radii at a and b; a blob's radius in ra), kind (0 capsule / droplet, 1 blob), seed
//   a2: color (linear), alpha
//   a3: edge roughness, lobes, stretch direction angle, stretch (1 none: an oblique hit elongates the blob)
inline const char* PAINT_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aA0;
layout(location = 2) in vec4 aA1;
layout(location = 3) in vec4 aA2;
layout(location = 4) in vec4 aA3;
uniform mat4 uVP;
out vec2 vP;
flat out vec4 vA0, vA1, vA2, vA3;
void main() {
    vA0 = aA0; vA1 = aA1; vA2 = aA2; vA3 = aA3;
    vec2 lo, hi;
    if (aA1.z < 0.5) {
        float r = max(aA1.x, aA1.y) * 1.35 + 0.02;
        lo = min(aA0.xy, aA0.zw) - r;
        hi = max(aA0.xy, aA0.zw) + r;
    } else {
        float r = aA1.x * (1.0 + aA3.y * 0.9) * max(aA3.w, 1.0) + 0.02;
        lo = aA0.xy - r;
        hi = aA0.xy + r;
    }
    vP = mix(lo, hi, aCorner * 0.5 + 0.5);
    gl_Position = uVP * vec4(vP, 0.0, 1.0);
}
)";
inline const char* PAINT_FS = R"(#version 330 core
in vec2 vP;
flat in vec4 vA0, vA1, vA2, vA3;
out vec4 frag;
uniform float uPx;  // board units per pixel
uniform int uOut;   // 0: the color (premultiplied, "over"), 1: the coat's thickness (added)
void main() {
    vec2 p = vP;
    float seed = vA1.w;
    float d, r;
    // the ragged edge's noise is never finer than a few pixels (finer, it would only alias)
    float fmax = 0.3 / uPx;
    if (vA1.z < 0.5) {
        vec2 a = vA0.xy, ba = vA0.zw - vA0.xy;
        float l2 = dot(ba, ba);
        float h = l2 > 1e-6 ? clamp(dot(p - a, ba) / l2, 0.0, 1.0) : 0.0;
        r = mix(vA1.x, vA1.y, h);
        d = length(p - a - ba * h) - r;
        // ragged paint edge, scaled to the stroke
        float f = min(2.2 / max(vA1.x, 0.04), fmax);
        d += (vnoise(p * f + seed * 13.7) - 0.5) * vA3.x * r;
    } else {
        vec2 q = p - vA0.xy;
        // an oblique hit: the blob stretched along its flight
        vec2 dir = vec2(cos(vA3.z), sin(vA3.z));
        float st = max(vA3.w, 1.0);
        q -= dir * dot(q, dir) * (1.0 - 1.0 / st);
        float ang = atan(q.y, q.x);
        float s = seed * 6.2831;
        float lob = 0.55 * sin(5.0 * ang + s) + 0.35 * sin(8.0 * ang + 2.3 * s) + 0.25 * sin(13.0 * ang + 4.1 * s)
                  + 0.18 * sin(21.0 * ang + 1.7 * s);
        // sharpen the lobes into short spikes, like a real splat's rim
        lob = sign(lob) * pow(abs(lob), 1.6);
        r = vA1.x;
        float R = vA1.x * (1.0 + vA3.y * 0.45 * lob);
        d = length(q) - R;
        d += (vnoise(p * min(2.4 / max(vA1.x, 0.05), fmax) + seed * 7.3) - 0.5) * vA3.x * vA1.x;
    }
    // the coverage over one pixel of the (distorted) distance: smooth edges at any resolution
    float fw = clamp(fwidth(d), uPx * 0.5, uPx * 4.0);
    float cov = 1.0 - smoothstep(-fw, fw, d);
    if (cov <= 0.0) discard;
    if (uOut == 1) {
        // the coat's thickness: it rises over a short width from the edge (droplets and drips: a dome), a
        // slightly thicker rim where the paint piled up as it spread, then an uneven plateau
        float in_ = max(-d, 0.0);
        // (never steeper than over a few pixels, and a tiny droplet is thin: no glittering specks)
        float wr = clamp(r, 3.0 * uPx, 0.12);
        float u = 1.0 - clamp(in_ / wr, 0.0, 1.0), e = (in_ - wr) / (wr * 0.8);
        float rise = 1.0 - u * u;
        float rim = exp(-e * e);
        float plate = 0.8 + 0.2 * vnoise(p * 3.0 + seed * 5.3);
        float T = 0.032 * clamp(r / 0.08, 0.3, 1.0);
        float h = T * rise * mix(plate, 1.0, rim * 0.5) + T * 0.35 * rim * step(0.06, r);
        frag = vec4(h * cov * vA2.a, 0.0, 0.0, 0.0);
        return;
    }
    // the coat is a little uneven: thicker and thinner patches
    vec3 col = vA2.rgb * (0.9 + 0.2 * vnoise(p * 3.0 + seed * 3.1));
    float a = cov * vA2.a;
    frag = vec4(col * a, a);
}
)";

// A flat color over the whole target (the paint fading out for a new song: blended as dst * constant alpha).
inline const char* FLAT_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uColor;
void main() { frag = uColor; }
)";

// ---------------------------------------------------------------- the wall and its paint
// The paint: the persistent layer, with the "wet" one over it (splashes still spreading, drips still running:
// redrawn each frame into a cleared buffer and stamped into the persistent layer once, when done, so a stamp's
// soft edge is drawn once and never hardens into a jagged one). Its relief comes from its thickness: a light
// moving slowly across the wall makes highlights slide along the rims, the drips and the droplets.
inline const char* COMPOSITE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uWall, uPaint, uHeight, uWet, uWetH;
uniform int uWetOn;
uniform vec2 uTexel;           // one pixel in uv
uniform float uPx;             // board units per pixel
uniform vec3 uLamp;            // the moving light (board units: x, y, height above the wall)
uniform vec4 uView;
uniform vec3 uWallA, uWallB;   // the wall's two tones (linear)
uniform vec3 uUvTint;          // what the wall becomes under the blacklight (linear)
uniform vec4 uTex;             // blotches, grain, pores, mottle amounts
uniform float uLight;          // the room's light (calm .. chorus)
uniform float uDark;           // the blacklight (0 room light .. 1 lights off, UV on)
uniform float uGlow;           // the paint's fluorescence (0..~1.3)
uniform float uIgnite;         // the stains lighting up one region after another (0..1)
uniform vec2 uSweep;           // the drop's sweeping beam: x, strength
uniform float uSlow;          // slow motion: the paint drained of its color, the wall cooler
uniform vec4 uWell;            // center xy, half extents xy
uniform vec4 uColL, uColR;     // the HUD columns (x0 y0 x1 y1)
uniform vec4 uText[32];        // the HUD's lines (x0 y0 x1 y1): the paint is held back under them
uniform int uTextN;
uniform float uBridge;
uniform vec3 uBridgeColor;
uniform float uDim, uFade;
float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }
float inRect(vec2 p, vec4 r, float soft) {
    vec2 c = (r.xy + r.zw) * 0.5, h = abs(r.zw - r.xy) * 0.5;
    return 1.0 - smoothstep(-soft, soft, sdBox(p - c, h));
}
float thick(vec2 uv) {
    float h = texture(uHeight, uv).r;
    if (uWetOn != 0) h += texture(uWetH, uv).r;
    return h;
}
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    vec4 w = texture(uWall, vUv);
    float tone = 1.0 + uTex.x * (w.r - 0.5) * 1.0;
    tone *= 1.0 + uTex.y * (w.g - 0.5) * 0.6;
    tone *= 1.0 - uTex.z * w.b * 0.55;
    vec3 wall = mix(uWallA, uWallB, w.a * uTex.w) * tone;

    // the room's light: a soft wash from above, falling off to the corners
    float vign = 1.0 - 0.35 * smoothstep(0.4, 1.25, length((vUv - 0.5) * vec2(1.6, 1.9)));
    float wash = 0.82 + 0.28 * smoothstep(-uView.w, uView.w * 1.1, p.y);
    // the well's panel: a calmer, slightly deeper patch of the wall
    float well = inRect(p, vec4(uWell.xy - uWell.zw, uWell.xy + uWell.zw), 0.5);
    wall *= 1.0 - 0.22 * well;
    vec3 lit = wall * uLight * wash * vign;
    vec3 uvWall = (wall * 0.35 + uUvTint * (0.6 + 0.4 * tone)) * vign;
    {
        float bx = (p.x - uSweep.x) / 7.5;
        uvWall *= 1.0 + uSweep.y * (0.75 * exp(-bx * bx) - 0.15);
    }
    vec3 col = mix(lit, uvWall, uDark);

    // the paint (the wet layer over the persistent one)
    vec4 pt = texture(uPaint, vUv);
    if (uWetOn != 0) { vec4 wt = texture(uWet, vUv); pt = wt + pt * (1.0 - wt.a); }
    if (pt.a > 0.002) {
        vec3 base = pt.rgb / pt.a;
        // where it is held back: behind the well and the stack (down to the screen's bottom, so the rows stand on
        // a calm floor; it comes back gradually beyond the well's edges), the HUD's columns and its lines
        float near = 1.0 - smoothstep(-0.3, 1.6, sdBox(p - (uWell.xy - vec2(0.0, 2.0)), uWell.zw + vec2(0.0, 2.0)));
        float cols = max(inRect(p, uColL, 1.8), inRect(p, uColR, 1.8));
        float txt = 0.0;
        for (int i = 0; i < uTextN; i++) txt = max(txt, inRect(p, uText[i], 1.0));
        float sub = (1.0 - 0.95 * near) * (1.0 - 0.6 * cols) * (1.0 - 0.95 * txt);

        // the relief: the coat's slope (board units), one pixel either side
        float hx = thick(vUv + vec2(uTexel.x, 0.0)) - thick(vUv - vec2(uTexel.x, 0.0));
        float hy = thick(vUv + vec2(0.0, uTexel.y)) - thick(vUv - vec2(0.0, uTexel.y));
        vec3 N = normalize(vec3(-hx / (2.0 * uPx), -hy / (2.0 * uPx), 1.0) * vec3(1.1, 1.1, 1.0));
        vec3 L = normalize(uLamp - vec3(p, 0.0));
        vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
        float ndl = max(dot(N, L), 0.0);
        float nh = max(dot(N, H), 0.0);
        // a sharp glint on the slopes facing the light, a broad soft sheen of the wet coat around its spot
        float spec = pow(nh, 70.0) * 0.9 + pow(nh, 12.0) * 0.12;
        // the wall's grain shows through the coat a little
        float relief = 0.93 + 0.14 * w.g - 0.2 * uTex.z * w.b;

        float lum = dot(base, vec3(0.2126, 0.7152, 0.0722));
        // under the room's light: a duller, matte-ish paint (a little greyed, lower), shaded by the moving light
        vec3 dull = mix(vec3(lum), base, 0.75) * 0.72 * relief;
        dull = mix(dull, vec3(lum) * 0.6, 0.82 * uSlow);
        vec3 room = dull * uLight * wash * vign * (0.55 + 0.6 * ndl) + vec3(spec) * 0.55 * uLight * vign;
        // under the blacklight: the stains light up one region after another and glow, saturated
        float n = vnoise(p * 0.16 + 4.0) * 0.75 + 0.25 * vnoise(p * 0.6);
        float ign = smoothstep(n * 0.8, n * 0.8 + 0.25, uIgnite);
        // the beams: the stains blaze where they pass, fall back toward plain paint between them
        float bx = (p.x - uSweep.x) / 7.5;
        float beam = exp(-bx * bx);
        float g = clamp(uGlow * ign * mix(1.0, 0.18 + 1.12 * beam, uSweep.y), 0.0, 1.3);
        vec3 fluo = max(mix(vec3(lum), base, 1.18), 0.0) * (1.05 + 0.25 * g) * relief * (0.85 + 0.2 * ndl);
        // the tube's reflection: a faint violet glint, still sliding with the light
        fluo += (uUvTint * 3.0 + vec3(0.06, 0.05, 0.12)) * spec;
        // between the beams the stains are plain paint again, in the beams' dim spill
        room = mix(room, dull * (0.45 + 0.35 * ndl) + vec3(spec) * 0.25, uSweep.y * (1.0 - beam) * 0.8);
        vec3 paint = mix(room, fluo, clamp(g, 0.0, 1.0));
        // held back: the paint sinks into the wall (only a faint, colorless trace of it)
        paint = mix(col * (1.0 - 0.3 * pt.a), paint, sub);
        col = mix(col, paint, pt.a);
    }

    // a soft shoulder for the brightest paint
    // (by the brightest channel, so the paint keeps its hue instead of washing out to white)
    float m = max(col.r, max(col.g, col.b));
    if (m > 0.8) col *= (0.8 + 0.2 * (1.0 - exp(-(m - 0.8) / 0.2))) / m;
    col = mix(col, uBridgeColor, uBridge);
    col *= (1.0 - 0.55 * uDim) * uFade;
    frag = vec4(pow(max(col, 0.0), vec3(1.0 / 2.2)), 1.0);
}
)";

// ---------------------------------------------------------------- the play: pieces, ghost, gauge, specks
// Instanced quads in board space.
//   a0: center xy, quad half extents xy
//   a1: color (linear), alpha
//   a2: kind (0 cell, 1 ghost cell, 2 ring, 3 dot, 4 line, 5 paint in flight), mask (cell: same-piece neighbors),
//       rotation, glow
//   a3: kind's parameters (cell: scale, flash; ring: radius, width, fill, ready; dot: radius; line: half length, half
//       width; paint in flight: the head's radius, the tail's length (along -x), the tail's radius)
inline const char* PLAY_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aA0;
layout(location = 2) in vec4 aA1;
layout(location = 3) in vec4 aA2;
layout(location = 4) in vec4 aA3;
uniform mat4 uVP;
out vec2 vL;
flat out vec4 vA1, vA2, vA3;
void main() {
    vA1 = aA1; vA2 = aA2; vA3 = aA3;
    vec2 l = aCorner * aA0.zw;
    vL = l;
    float c = cos(aA2.z), s = sin(aA2.z);
    vec2 w = aA0.xy + vec2(c * l.x - s * l.y, s * l.x + c * l.y);
    gl_Position = uVP * vec4(w, 0.0, 1.0);
}
)";
inline const char* PLAY_FS = R"(#version 330 core
in vec2 vL;
flat in vec4 vA1, vA2, vA3;
out vec4 frag;
uniform float uPx;
uniform int uPass;      // 0: the glow around the shapes, 1: the shapes
uniform float uGlowK;   // the glow's strength (stronger under the blacklight)
uniform float uMul;     // dim and fade
uniform float uBridge;  // 0 .. 1: the cells drawn plainly in the bridge's ink
uniform vec3 uBridgeInk;
// A cell whose sides toward its own piece's neighbors run to the cell's edge (the piece reads as one shape) and
// whose free corners are rounded. mask bits: L R D U (y up), LD RD LU RU.
// ext: how far a side toward a neighbor runs past the cell's edge (a little for the coverage, far for the shading,
// so the seam between two cells of a piece neither shows a hairline nor a rim).
float cellSdf(vec2 p, int m, float s, float ext) {
    float gap = 0.075 * s, h = 0.5 * s, rr = 0.17 * s;
    float l = (m & 1) != 0 ? h + ext : h - gap;
    float r = (m & 2) != 0 ? h + ext : h - gap;
    float dn = (m & 4) != 0 ? h + ext : h - gap;
    float up = (m & 8) != 0 ? h + ext : h - gap;
    vec2 c = vec2((r - l) * 0.5, (up - dn) * 0.5), b = vec2((r + l) * 0.5, (up + dn) * 0.5);
    vec2 q = p - c;
    bool sx = p.x > 0.0, sy = p.y > 0.0;
    bool freeX = sx ? (m & 2) == 0 : (m & 1) == 0;
    bool freeY = sy ? (m & 8) == 0 : (m & 4) == 0;
    float rad = (freeX && freeY) ? rr : 0.0;
    vec2 d = abs(q) - b + rad;
    float sd = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - rad;
    // a concave corner of the piece (two neighbors, no diagonal: the inside of an L, the joints of a T): the
    // quadrant beyond it is carved out (its two sides run on into the neighbors' edges, with a small fillet), so the
    // shape's edge, its rim and its shading go on around the corner instead of stopping at a flat patch
    const ivec4 SX = ivec4(1, 2, 1, 2), SY = ivec4(4, 4, 8, 8), DG = ivec4(16, 32, 64, 128);
    const vec4 QX = vec4(-1.0, 1.0, -1.0, 1.0), QY = vec4(-1.0, -1.0, 1.0, 1.0);
    float fr = 0.06 * s, c0 = h - gap;
    for (int i = 0; i < 4; i++) {
        if ((m & SX[i]) == 0 || (m & SY[i]) == 0 || (m & DG[i]) != 0) continue;
        vec2 pq = vec2(p.x * QX[i], p.y * QY[i]);
        vec2 dq = vec2(c0 + fr) - pq;
        float sq = length(max(dq, 0.0)) + min(max(dq.x, dq.y), 0.0) - fr;
        sd = max(sd, -sq);
    }
    return sd;
}
void main() {
    int kind = int(vA2.x + 0.5);
    vec3 col = vA1.rgb;
    float alpha = vA1.a;
    vec2 p = vL;
    float aa = uPx * 0.9;
    vec3 outc;
    float a;
    if (kind == 0 || kind == 1) {
        float s = vA3.x;
        int m = int(vA2.y + 0.5);
        float d = cellSdf(p, m, s, 0.06 * s + 2.0 * aa);
        float ds = cellSdf(p, m, s, 10.0);
        if (kind == 1) { // the ghost: a thin outline of the piece's shape and a faint tint
            float line = 1.0 - smoothstep(0.025 * s - aa, 0.025 * s + aa, abs(ds + 0.04 * s)) * 1.0;
            line *= 1.0 - smoothstep(-aa, aa, d);
            float fill = (1.0 - smoothstep(-aa, aa, d)) * 0.07;
            if (uPass == 0) discard;
            a = (line * 0.75 + fill) * alpha;
            outc = col;
        } else if (uPass == 0) { // the glow: fluo paint lit, a soft halo around the shape
            // each cell glows only along its own stretch of the piece's outline (no doubled glow at the seams)
            float hs = 0.5 * s;
            if (((m & 1) != 0 && p.x < -hs) || ((m & 2) != 0 && p.x > hs) || ((m & 4) != 0 && p.y < -hs) ||
                ((m & 8) != 0 && p.y > hs) || ds <= 0.0) discard;
            a = vA2.w * uGlowK * exp(-ds / (0.085 * s)) * (1.0 - smoothstep(-aa, aa, -ds)) * alpha * 0.55;
            outc = col;
        } else {
            // each cell draws only its own square toward its neighbors (they draw theirs: no doubled seams)
            float hs = 0.5 * s + aa; // (overlapping by a pixel: no seam)
            if (((m & 1) != 0 && p.x < -hs) || ((m & 2) != 0 && p.x > hs) || ((m & 4) != 0 && p.y < -hs) ||
                ((m & 8) != 0 && p.y > hs)) discard;
            float inside = 1.0 - smoothstep(-aa, aa, d);
            if (inside <= 0.0) discard;
            // the coat: flat fluo color, its edges a touch hotter, the top edges catching the light (a glossy
            // wet rim), the bottom ones a little deeper
            vec2 gr = vec2(dFdx(ds), dFdy(ds));
            vec2 n = length(gr) > 1e-6 ? normalize(gr) : vec2(0.0);
            float rim = smoothstep(-0.13 * s, 0.0, ds);
            outc = col * (1.0 + 0.22 * rim);
            outc = mix(outc, vec3(1.0), rim * max(n.y, 0.0) * 0.32);
            outc *= 1.0 - rim * max(-n.y, 0.0) * 0.25;
            // compartments: each cell a little bag of paint, puffed (its top slopes catching the light, its lower
            // ones deeper), the bags welded together along pinched seams where the piece's cells meet
            vec2 qq = 0.5 * s - abs(p);
            float e = min(qq.x, qq.y);
            bool shared = qq.x < qq.y ? (p.x < 0.0 ? (m & 1) != 0 : (m & 2) != 0) : (p.y < 0.0 ? (m & 4) != 0 : (m & 8) != 0);
            // a pillow: h = (1 - u^4)(1 - v^4) over the cell, lit by its smooth slopes (no facets)
            vec2 uv = clamp(p / (0.5 * s), -1.0, 1.0);
            vec2 u4 = 1.0 - uv * uv * uv * uv;
            vec2 grad = vec2(-4.0 * uv.x * uv.x * uv.x * u4.y, -4.0 * uv.y * uv.y * uv.y * u4.x);
            float lit = clamp(dot(-grad, vec2(-0.33, 0.94)) * 0.35, -1.0, 1.0);
            float crease = shared ? 1.0 - smoothstep(0.0, 0.07 * s, e) : 0.0;
            outc *= (0.95 + 0.3 * lit) * (1.0 - 0.5 * crease * crease);
            outc = mix(outc, vec3(1.0), 0.3 * smoothstep(0.3, 1.0, lit) * (1.0 - rim));
            // the weld: a fine line of the bag's plastic along the seam
            outc = mix(outc, col * 0.45, shared ? (1.0 - smoothstep(0.004 * s, 0.004 * s + aa, e)) * 0.7 : 0.0);
            outc = mix(outc, vec3(1.0), vA3.y);
            a = inside * alpha;
        }
    } else if (kind == 2) { // the gauge's ring
        if (uPass == 0) discard;
        float R = vA3.x, wd = vA3.y, fill = vA3.z;
        float r = length(p);
        float ring = 1.0 - smoothstep(wd - aa, wd + aa, abs(r - R));
        float ang = atan(p.x, p.y); // 0 at the top, clockwise
        float f = fract(ang / 6.2831853 + 1.0);
        float on = fill >= 0.999 ? 1.0 : smoothstep(0.004, 0.0, f - fill);
        float base = ring * 0.22;
        a = max(base, ring * on) * alpha;
        outc = mix(vec3(0.6), col, on);
        if (vA3.w > 0.5) { // ready: a filled disc glowing softly inside
            float disc = (1.0 - smoothstep(R * 0.55 - aa, R * 0.55 + aa, r)) * 0.9;
            a = max(a, disc * alpha);
            outc = col;
        }
    } else if (kind == 3) { // a speck of paint
        if (uPass == 0) discard;
        float d = length(p) - vA3.x;
        a = (1.0 - smoothstep(-aa, aa, d)) * alpha;
        outc = col;
    } else if (kind == 5) { // a gob of paint in flight: a glossy drop, its tail stretched behind it
        if (uPass == 0) discard;
        float R = vA3.x, Lt = vA3.y;
        float h = Lt > 1e-4 ? clamp(-p.x / Lt, 0.0, 1.0) : 0.0;
        float rr = mix(R, vA3.z, h);
        float d = length(p - vec2(-Lt * h, 0.0)) - rr;
        a = (1.0 - smoothstep(-aa, aa, d)) * alpha;
        if (a <= 0.0) discard;
        // lit from above: a wet highlight toward the light, a deeper underside
        float c = cos(vA2.z), s = sin(vA2.z);
        vec2 up = vec2(s, c); // the world's up, in the drop's frame
        float k = clamp(dot(p, up) / max(R, 1e-3), -1.0, 1.0);
        outc = col * (0.8 + 0.3 * k);
        vec2 hl = up * R * 0.4;
        outc = mix(outc, vec3(1.0), 0.6 * (1.0 - smoothstep(R * 0.12, R * 0.32, length(p - hl))));
    } else { // a line
        if (uPass == 0) discard;
        vec2 q = abs(p) - vec2(vA3.x, vA3.y);
        float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
        a = (1.0 - smoothstep(-aa, aa, d)) * alpha;
        outc = col;
    }
    outc = mix(outc, uBridgeInk, uBridge);
    outc *= uMul;
    vec3 srgb = pow(max(outc, 0.0), vec3(1.0 / 2.2));
    frag = vec4(srgb * a, a);
}
)";

// ---------------------------------------------------------------- text (SDF glyphs) and panels, in pixels
inline const char* TEXT_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;   // pixels, top-left origin
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
uniform vec4 uInk;    // sRGB color, alpha
uniform float uBias;  // > 0: bolder
uniform float uSoft;  // > 0: a soft halo (the width of the edge)
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
uniform vec4 uColor; // premultiplied
void main() { frag = uColor; }
)";

} // namespace fluo_shaders
