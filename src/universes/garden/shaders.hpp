#pragma once
// GARDEN's shaders (see garden.cpp): the sky and the ground line, the clods of earth, the sprites (blades of grass,
// flowers, petals, seeds, dust), the HUD letters and the final pass.

namespace garden_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x), mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}
vec3 enc(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { s += a * vnoise(p); p = p * 2.03 + 17.1; a *= 0.5; }
    return s;
}
// The earth's texture (the clods and the ground line share it; p in board units): lumps lit from the upper left,
// finer clumps and grains, a few pale pebbles with a little shade under each. tb: how much of it shows.
vec3 earthTexture(vec3 col, vec3 base, vec2 p, float seed, float px, float tb) {
    // relief: the lumps of the clod lit from the upper left (normals from the grain's slope)
    float e = 0.04;
    float h0 = fbm(p * 2.6 + seed);
    float hx = fbm((p + vec2(e, 0.0)) * 2.6 + seed), hy = fbm((p + vec2(0.0, e)) * 2.6 + seed);
    vec3 nrm = normalize(vec3(-(hx - h0) / e * 0.35, -(hy - h0) / e * 0.35, 1.0));
    float lam = dot(nrm, normalize(vec3(-0.4, 0.7, 0.6)));
    col *= mix(1.0, 0.72 + 0.42 * lam, tb);
    // finer clumps and grains
    col *= mix(1.0, 0.9 + 0.2 * fbm(p * 7.0 + 11.0), tb);
    vec2 gc = floor(p * 22.0);
    float h = hash12(gc + seed);
    vec2 gq = fract(p * 22.0) - 0.5;
    float dot_ = smoothstep(0.42, 0.18, length(gq));
    col *= mix(1.0, h > 0.92 ? 1.0 + 0.3 * dot_ : (h < 0.08 ? 1.0 - 0.35 * dot_ : 1.0), tb);
    // pebbles, few and far between: pale stones of all sizes (a rare big one, some medium, small grit), lit from the
    // upper left, a little shade under each
    for (int k = 0; k < 3; k++) {
        float sc = k == 0 ? 1.5 : (k == 1 ? 3.0 : 6.0);
        vec2 pc = floor(p * sc);
        if (hash12(pc + 41.3 + float(k) * 17.0) < (k == 0 ? 0.955 : (k == 1 ? 0.965 : 0.97))) continue;
        vec2 jit = vec2(hash12(pc + 5.7), hash12(pc + 9.1)) - 0.5;
        vec2 pq = (fract(p * sc) - 0.5 - jit * 0.4) / sc;
        float ang = hash12(pc + 2.9) * 6.283;
        pq = mat2(cos(ang), -sin(ang), sin(ang), cos(ang)) * pq;
        float r = (k == 0 ? 0.17 : (k == 1 ? 0.08 : 0.035)) * (0.6 + 0.8 * hash12(pc + 13.3));
        float el = 1.2 + 0.5 * hash12(pc + 3.3);
        float d = length(pq * vec2(1.0, el)) + (vnoise(p * 30.0 + float(k)) - 0.5) * r * 0.25;
        float peb = smoothstep(r + px, r - px, d);
        float shade = smoothstep(r * 1.5, r, length((pq + vec2(-r * 0.15, r * 0.3)) * vec2(1.0, el))) * (1.0 - peb);
        // a pale stone, warm or cool grey, a little of the soil's tint
        vec3 grey = mix(vec3(0.46, 0.42, 0.36), vec3(0.36, 0.38, 0.4), hash12(pc + 21.1));
        vec3 stone = mix(grey, base, 0.2) * (0.8 + 0.45 * hash12(pc + 7.7));
        vec2 lq = pq / r;
        stone *= 0.7 + 0.5 * smoothstep(-0.9, 0.9, lq.y * 0.8 - lq.x * 0.5);   // lit from the upper left
        stone *= 0.92 + 0.16 * vnoise(p * 60.0 + float(k) * 3.0);              // its grain
        col = mix(col, col * 0.62, shade * 0.7 * tb);
        col = mix(col, stone, peb * tb);
    }
    return col;
}
// The earth's color at a height (board units, the well's floor at -10): one gradient for the ground line and the
// clods, dark deep down, its own soil at the floor, a little lighter and drier as it rises.
vec3 earthAt(float y, vec3 ground, vec3 deep, vec3 soil) {
    vec3 floorC = mix(ground, soil, 0.2);
    if (y < -10.0) return mix(floorC, deep, smoothstep(0.0, 3.5, -10.0 - y));
    return mix(floorC, soil * 1.12, smoothstep(-10.0, 8.0, y));
}
)";

// ---------------------------------------------------------------- the sky and the ground line
// One wide line of earth along the bottom of the picture (low and flat under the well, rising gently toward the
// edges), the sky above it: a plain gradient, one sun or moon, the drop's glow rising from the horizon.
inline const char* SKY_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uView;          // center x, y, half extents x, y (board units)
uniform vec3 uSkyTop, uSkyLow, uSunCol, uGlow, uGround, uGroundDeep, uLip, uBridgeCol, uDusk, uSoil;
uniform vec4 uSun;           // x, y, radius, moon (0 / 1)
uniform float uGlowAmt, uSlow, uBridge, uSeed, uPx, uStars, uVeil, uDry, uHaze, uLush;
uniform vec4 uGroundL, uGroundR; // each bank: lip, height, start, end (not mirrored)
float groundY(float x) {
    float a = abs(x);
    vec4 g = x > 0.0 ? uGroundR : uGroundL;
    float side = smoothstep(6.0, 12.0, a);
    return -10.05 + g.x * smoothstep(5.5, 7.6, a) + 0.25 * smoothstep(7.6, 11.0, a) + g.y * pow(smoothstep(g.z, g.w, a), 1.3)
           + (0.22 * sin(x * 0.37 + uSeed) + 0.12 * sin(x * 0.91 + uSeed * 1.7) + 0.3 * sin(x * 0.17 + uSeed * 2.1)) * side;
}
void main() {
    vec2 w = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    // the sky: a plain gradient from the horizon up
    float h = clamp((w.y + 10.0) / 23.0, 0.0, 1.0);
    vec3 sky = mix(uSkyLow, uSkyTop, smoothstep(0.0, 1.0, pow(h, 0.75)));
    // dry spells: a pale, dusty haze; the peak: a deeper, clearer sky
    float grey = dot(sky, vec3(0.3, 0.5, 0.2));
    sky = mix(sky, mix(vec3(grey), uSkyLow, 0.6) * 1.05, uHaze * 0.38);
    sky = mix(sky, sky * mix(vec3(1.0), uSkyTop / max(dot(uSkyTop, vec3(0.33)), 1e-3) * 0.33, 0.25), uLush * smoothstep(-6.0, 12.0, w.y));
    // the drop: a glow of the flowers' color rising from the horizon, steady for the whole drop
    float glow = uGlowAmt * exp(-max(w.y + 9.0, 0.0) / 13.0);
    sky = mix(sky, uGlow, clamp(glow, 0.0, 0.8));
    // slow motion: dusk falls over the garden
    vec3 dusk = mix(uDusk * 0.55, uDusk, smoothstep(-10.0, 6.0, -w.y));
    sky = mix(sky, dusk, uSlow * 0.85);
    // stars (night, or dusk in slow motion): a few faint points, still
    float st = max(uStars, uSlow * 0.8);
    if (st > 0.01) {
        vec2 c = floor(w * 2.2);
        float r = hash12(c + 7.0);
        if (r > 0.985) {
            vec2 q = fract(w * 2.2) - 0.5 - (vec2(hash12(c + 1.3), hash12(c + 2.9)) - 0.5) * 0.6;
            float s = smoothstep(0.09, 0.0, length(q)) * st * smoothstep(-4.0, 4.0, w.y) * (0.4 + 0.6 * hash12(c + 5.1));
            sky += vec3(0.75, 0.8, 0.95) * s * 0.6;
        }
    }
    // the sun (or the moon): a soft disk and a wide halo
    float d = length(w - uSun.xy);
    float sunA = smoothstep(uSun.z + uPx * 1.5, uSun.z - uPx * 1.5, d);
    float halo = exp(-max(d - uSun.z, 0.0) / (uSun.z * (1.6 + 0.8 * (1.0 - uSun.w)))) * (0.22 + 0.1 * (1.0 - uSun.w));
    vec3 sunCol = uSunCol * (1.0 - 0.35 * uSlow);
    if (uSun.w > 0.5) {
        // the moon: a faint mottling
        sunCol *= 0.9 + 0.1 * fbm((w - uSun.xy) * 2.0 + 3.0);
    }
    sky = mix(sky, sunCol, halo * (1.0 - 0.6 * uSlow));
    sky = mix(sky, sunCol, sunA * (1.0 - 0.5 * uSlow));
    // the well: a pale, see-through panel over the sky (rounded at the top, standing on the ground) that marks the
    // area of play, with a slightly brighter hairline edge
    vec2 wd = abs(w) - vec2(5.08, 10.08);
    float rr = 0.3;
    float wsd = length(max(wd + rr, 0.0)) + min(max(wd.x + rr, wd.y + rr), 0.0) - rr;
    float wellM = smoothstep(uPx, -uPx, wsd);
    float wellE = smoothstep(uPx * 1.5, 0.0, abs(wsd + uPx));
    sky = mix(sky, vec3(1.0), wellM * uVeil + wellE * uVeil * 0.8);
    // the ground line: dark earth, a lighter lip where the light catches its edge, fine grain
    float gy = groundY(w.x);
    float below = gy - w.y;
    float edge = smoothstep(-uPx * 1.2, uPx * 1.2, below + (vnoise(vec2(w.x * 9.0, 3.0)) - 0.5) * 0.05);
    float depth = smoothstep(0.0, 3.2, below);
    // the same gradient as the clods (dark deep down, lighter as the banks rise), the deep earth below the surface
    vec3 soil = mix(earthAt(w.y, uGround, uGroundDeep, uSoil), uGroundDeep, depth * 0.5);
    soil = earthTexture(soil, soil, w, 0.0, uPx, 1.0 - uBridge); // the clods' own texture: one earth
    soil = mix(soil, uLip, exp(-max(below, 0.0) / 0.09) * 0.55 * (1.0 - 0.5 * uDry));
    // dry earth is paler and dustier
    soil = mix(soil, soil * 1.3 + uLip * 0.15, uDry * 0.35);
    soil *= 1.0 - 0.45 * uSlow;
    vec3 col = mix(sky, soil, edge);
    col = mix(col, uBridgeCol, uBridge);
    frag = vec4(enc(col), 1.0);
}
)";

// ---------------------------------------------------------------- the clods of earth
// One instance per cell. The cells of one piece merge into one clod: the shape is the distance to the empty
// squares around the cell (its 8 neighbours that are not the same piece), inset, with rounded outer corners and
// a crumbly edge; the grain lives in the piece's own space (it stays with the clod when rows fall).
inline const char* CELL_VS = R"(#version 330 core
layout(location = 0) in vec2 aQ;
layout(location = 1) in vec4 a0;   // x, y, size, neighbour mask (L R D U LD RD LU RU)
layout(location = 2) in vec4 a1;   // linear rgb, alpha
layout(location = 3) in vec4 a2;   // grain origin x, y, crumble 0..1, ghost
layout(location = 4) in vec4 a3;   // seed, moss on the top edge, root (how far it reaches down into the ground), -
layout(location = 5) in vec4 a4;   // the soils joined on the L R D U sides (packed, -1: none)
uniform mat4 uVP;
out vec2 vP;
flat out vec4 v0, v1, v2, v3, v4;
void main() {
    vP = aQ * 0.5;
    if (aQ.y < 0.0) vP.y -= a3.z; // a root reaches down into the ground
    v0 = a0; v1 = a1; v2 = a2; v3 = a3; v4 = a4;
    float squash = a3.w > 0.0 ? a3.w : 1.0; // a crushed row: flattened (its earth keeps its own frame)
    gl_Position = uVP * vec4(a0.xy + vec2(vP.x, vP.y * squash) * a0.z, 0.0, 1.0);
}
)";
inline const char* CELL_FS = R"(#version 330 core
in vec2 vP;
flat in vec4 v0, v1, v2, v3, v4;
out vec4 frag;
vec3 unpackSoil(float f) {
    float v = floor(f + 0.5);
    vec3 c = vec3(floor(v / 65536.0), mod(floor(v / 256.0), 256.0), mod(v, 256.0)) / 255.0;
    return c * c;
}
uniform float uPx, uBridge;
uniform vec3 uBridgeInk, uMoss, uGround, uGroundDeep, uSoil;
float boxD(vec2 p, vec2 o) {
    vec2 q = abs(p - o) - 0.5;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}
void main() {
    int m = int(v0.w + 0.5);
    float size = v0.z;
    vec2 offs[8] = vec2[8](vec2(-1.0, 0.0), vec2(1.0, 0.0), vec2(0.0, -1.0), vec2(0.0, 1.0),
                           vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    float dE = 9.0, dTop = 9.0, dBot = 9.0;
    for (int i = 0; i < 8; i++) {
        if (((m >> i) & 1) != 0) continue;
        float d = boxD(vP, offs[i]);
        dE = min(dE, d);
        if (offs[i].y > 0.5) dTop = min(dTop, d);
        if (offs[i].y < -0.5) dBot = min(dBot, d);
    }
    // rounded outer corners (both sides open)
    const float R = 0.3;
    for (int c = 0; c < 4; c++) {
        vec2 s = vec2(c == 0 || c == 2 ? -1.0 : 1.0, c < 2 ? -1.0 : 1.0);
        int sx = s.x < 0.0 ? 0 : 1, sy = s.y < 0.0 ? 2 : 3;
        if (((m >> sx) & 1) != 0 || ((m >> sy) & 1) != 0) continue;
        vec2 q = vP * s - vec2(0.5 - R);
        if (q.x > 0.0 && q.y > 0.0) dE = min(dE, R - length(q));
    }
    vec2 nc = v2.xy + vP;
    bool ghost = v2.w > 0.5;
    float px = uPx / size;
    float inset = 0.06;
    // a lumpy outline, a few crumbs off it
    float crumb = ghost ? 0.0 : (fbm(nc * 3.3 + v3.x) - 0.5) * 0.13 + (vnoise(nc * 15.0) - 0.5) * 0.05;
    float a = smoothstep(inset - px, inset + px, dE + crumb);
    if (!ghost) {
        vec2 cc = floor(nc * 11.0);
        vec2 cq = fract(nc * 11.0) - 0.5 - (vec2(hash12(cc + 3.1), hash12(cc + 7.7)) - 0.5) * 0.5;
        float cr = hash12(cc + 1.7);
        float band = smoothstep(inset - 0.09, inset - 0.03, dE) * (1.0 - a);
        a = max(a, band * step(0.72, cr) * smoothstep(0.2, 0.12, length(cq)));
    }
    vec3 base = v1.rgb;
    // the earth is one mass: next to a joined clod the two soils mix over a ragged band (half and half right at
    // the join, so both cells meet on the same color)
    if (!ghost) {
        // the distance to each side (L R D U, as the mask)
        vec4 sideD = vec4(0.5 + vP.x, 0.5 - vP.x, 0.5 + vP.y, 0.5 - vP.y);
        vec3 sum = vec3(0.0);
        float wsum = 0.0;
        for (int i = 0; i < 4; i++) {
            float f = v4[i];
            if (f < 0.0) continue;
            float t = sideD[i];
            float w = 0.5 * (1.0 - smoothstep(0.0, 0.32, t + (fbm((v0.xy + vP * size) * 4.0 + 5.0) - 0.5) * 0.22));
            sum += unpackSoil(f) * w;
            wsum += w;
        }
        if (wsum > 0.0) base = mix(base, sum / wsum, min(wsum, 0.5));
        // the earth's gradient by height (the clods share it with the ground line)
        base *= earthAt(v0.y + vP.y * (v3.w > 0.0 ? v3.w : 1.0) * size, uGround, uGroundDeep, uSoil) / max(uSoil, vec3(1e-3));
    }
    vec3 col = base;
    if (!ghost) {
        float tb = 1.0 - uBridge;
        col = earthTexture(col, base, nc, v3.x, px, tb);
        // light from above: the top of the clod catches it, its underside in shade, a darker crumbly rim
        col *= mix(1.0, 1.0 + 0.14 * exp(-max(dTop - inset, 0.0) / 0.2), tb);
        col *= mix(1.0, 1.0 - 0.28 * exp(-max(dBot - inset, 0.0) / 0.3), tb);
        col *= mix(1.0, 1.0 - 0.18 * exp(-max(dE + crumb - inset, 0.0) / 0.04), tb);
        // moss where grass has taken on the top
        col = mix(col, uMoss, v3.y * 0.4 * exp(-max(dTop - inset, 0.0) / 0.07) * tb);
    }
    // a cleared row crumbles: the earth breaks up from its grain
    // a root fades into the ground
    if (v3.z > 0.0) a *= 1.0 - smoothstep(-0.5 - v3.z * 0.45, -0.5 - v3.z, vP.y + (fbm(nc * 6.0) - 0.5) * 0.12);
    float cr = v2.z;
    if (cr > 0.0) {
        float n = vnoise(nc * 9.0) * 0.6 + vnoise(nc * 27.0) * 0.4;
        float k = cr * 1.25 - 0.1;
        a *= smoothstep(k, k + 0.08, n);
        col *= 1.0 - 0.25 * smoothstep(k + 0.25, k, n);
    }
    col = mix(col, uBridgeInk, uBridge);
    frag = vec4(enc(col), a * v1.a);
}
)";

// ---------------------------------------------------------------- sprites
// Kinds: 0 a blade of grass or a stem (root at x, y; half width, length; bent), 1 a soft dot (dust, pollen,
// fireflies), 2 a flower head (opening), 3 a petal, 4 a seed with its pappus, 5 the bonus gauge, 6 a bud.
inline const char* SPRITE_VS = R"(#version 330 core
layout(location = 0) in vec2 aQ;
layout(location = 1) in vec4 a0;   // x, y, half x (width), half y (length)
layout(location = 2) in vec4 a1;   // rotation, kind, p0, p1
layout(location = 3) in vec4 a2;   // linear rgb, alpha
layout(location = 4) in vec4 a3;   // p2, p3, seed, -
uniform mat4 uVP;
out vec2 vQ;
flat out vec4 v1, v2, v3;
void main() {
    int kind = int(a1.y + 0.5);
    vec2 local;
    if (kind == 0) {
        float v = aQ.y * 0.5 + 0.5;
        float w = a0.z * mix(1.0, 0.15, v);
        local = vec2(aQ.x * w + a1.z * v * v * a0.w, v * a0.w);
    } else {
        local = aQ * a0.zw;
    }
    float c = cos(a1.x), s = sin(a1.x);
    vec2 wp = a0.xy + vec2(c * local.x - s * local.y, s * local.x + c * local.y);
    vQ = aQ;
    v1 = a1; v2 = a2; v3 = a3;
    gl_Position = uVP * vec4(wp, 0.0, 1.0);
}
)";
inline const char* SPRITE_FS = R"(#version 330 core
in vec2 vQ;
flat in vec4 v1, v2, v3;
out vec4 frag;
uniform vec3 uCenter;   // the flowers' hearts
uniform vec3 uRing;     // the gauge's ring
void main() {
    int kind = int(v1.y + 0.5);
    vec3 col = v2.rgb;
    float a = 0.0;
    float r = length(vQ);
    if (kind == 0) {
        float v = vQ.y * 0.5 + 0.5;
        a = smoothstep(1.0, 0.45, abs(vQ.x));
        col *= 0.72 + 0.4 * v;
    } else if (kind == 1) {
        a = smoothstep(1.0, v1.z, r);
    } else if (kind == 2) {
        // petals around a heart, opening: p0 openness, p1 petal count
        float ang = atan(vQ.y, vQ.x);
        float n = v1.w;
        float o = v1.z;
        float petal = pow(abs(cos(ang * n * 0.5)), 0.7);
        float R = mix(0.35, 1.0, o) * (0.55 + 0.45 * petal);
        a = smoothstep(R, R - 0.12, r);
        float heart = 0.3 * mix(0.6, 1.0, o);
        col *= 0.8 + 0.25 * smoothstep(0.0, R, r);
        col = mix(col, uCenter, smoothstep(heart, heart - 0.1, r));
        // the petals' veins: a touch darker between them
        col *= 1.0 - 0.12 * (1.0 - petal) * smoothstep(heart, R, r);
    } else if (kind == 3) {
        vec2 q = vQ;
        float e = length(vec2(q.x, q.y * (1.0 + 0.25 * q.y)));
        a = smoothstep(1.0, 0.75, e);
        col *= 0.85 + 0.2 * q.y;
    } else if (kind == 4) {
        float ang = atan(vQ.y, vQ.x);
        float core = smoothstep(0.22, 0.12, r);
        float fil = pow(abs(cos(ang * 5.0)), 12.0) * smoothstep(1.0, 0.85, r) * step(0.15, r) * 0.5;
        float fuzz = smoothstep(1.0, 0.5, r) * 0.18;
        a = max(core, max(fil, fuzz));
    } else if (kind == 5) {
        // the bonus gauge: a fine ring, a meadow rising inside it
        float fill = v1.z, ready = v1.w;
        float ring = smoothstep(0.035, 0.0, abs(r - 0.96)) * (0.55 + 0.45 * ready);
        float inside = smoothstep(0.92, 0.89, r);
        float lvl = -0.9 + 1.8 * fill + 0.03 * sin(vQ.x * 9.0 + v3.z);
        float f = inside * smoothstep(lvl + 0.02, lvl - 0.02, vQ.y) * step(0.001, fill);
        col = mix(uRing, v2.rgb * (0.8 + 0.3 * smoothstep(lvl - 0.6, lvl, vQ.y)), f);
        a = max(ring, f);
    } else if (kind == 6) {
        // a bud: a closed drop of color in a green sheath
        vec2 q = vQ;
        float e = length(vec2(q.x * (1.0 + 0.4 * (q.y + 1.0) * 0.5), q.y));
        a = smoothstep(1.0, 0.8, e);
        col = mix(col, col * 0.35 + vec3(0.04, 0.08, 0.03), smoothstep(0.1, -0.6, q.y));
    }
    frag = vec4(enc(col), a * v2.a);
}
)";

// ---------------------------------------------------------------- a worm in the earth
// One quad over the worm's bounds; the body is the union of capsules along its points (one pass, so a faded worm
// has no darker joints): round, a little thicker behind the head, fine rings, a paler saddle, a damp sheen on top.
// It lies in the soil: a soft dark burrow around it, and the earth's lumps cover it here and there.
inline const char* WORM_VS = R"(#version 330 core
layout(location = 0) in vec2 aQ;
uniform mat4 uVP;
uniform vec4 uBox;   // x0, y0, x1, y1 (board units)
out vec2 vW;
void main() {
    vW = mix(uBox.xy, uBox.zw, aQ * 0.5 + 0.5);
    gl_Position = uVP * vec4(vW, 0.0, 1.0);
}
)";
inline const char* WORM_FS = R"(#version 330 core
in vec2 vW;
out vec4 frag;
uniform vec2 uPts[9];
uniform float uRad, uLen, uVis, uSeed, uPx, uSunk;
uniform vec3 uCol, uDark;
void main() {
    float best = 1e9, bs = 0.0, side = 0.0;
    for (int i = 0; i < 8; i++) {
        vec2 a = uPts[i], b = uPts[i + 1], ab = b - a;
        float t = clamp(dot(vW - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
        vec2 q = vW - (a + ab * t);
        float d = length(q);
        if (d < best) { best = d; bs = (float(i) + t) / 8.0; side = ab.x * q.y - ab.y * q.x; }
    }
    // tapered at both ends, thickest a third of the way back (the saddle)
    float r = uRad * (0.45 + 0.55 * pow(sin(3.14159 * clamp(bs * 0.94 + 0.03, 0.0, 1.0)), 0.6)) * (1.0 + 0.12 * exp(-pow((bs - 0.3) / 0.07, 2.0)));
    float body = smoothstep(r + uPx, r - uPx, best);
    float across = clamp(best / max(r, 1e-4), 0.0, 1.0) * sign(side);
    vec3 col = uCol * (0.62 + 0.5 * sqrt(max(1.0 - across * across, 0.0)));
    float ring = 0.5 + 0.5 * cos(bs * uLen / 0.045 * 6.283);
    col *= 1.0 - 0.16 * ring * ring;
    col = mix(col, col * vec3(1.18, 1.0, 0.95), exp(-pow((bs - 0.3) / 0.06, 2.0)) * 0.6);
    col += vec3(0.05, 0.04, 0.035) * exp(-pow((across - 0.35) / 0.18, 2.0)); // damp sheen
    // the burrow: a soft dark rim in the soil around the body
    float burrow = smoothstep(r * 2.4, r, best) * 0.4;
    // the earth's lumps cover it here and there (fixed in the world: it crawls in and out under them)
    float cover = smoothstep(0.52, 0.66, fbm(vW * 6.0 + 3.7)) * uSunk;
    float bodyA = body * (1.0 - 0.85 * cover);
    float a = bodyA + burrow * (1.0 - bodyA);
    vec3 c = (mix(col, uDark, 0.18) * bodyA + uDark * burrow * (1.0 - bodyA)) / max(a, 1e-4);
    frag = vec4(enc(c), a * uVis);
}
)";

// ---------------------------------------------------------------- text
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
uniform vec3 uInk;
uniform float uAlpha, uBias;
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.75;
    float a = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    frag = vec4(enc(uInk), a * uAlpha);
}
)";

inline const char* SHAPE_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aCol;
uniform vec2 uRes;
out vec4 vCol;
void main() {
    vCol = aCol;
    gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
}
)";
inline const char* SHAPE_FS = R"(#version 330 core
in vec4 vCol;
out vec4 frag;
void main() { frag = vec4(enc(vCol.rgb), vCol.a); }
)";

// ---------------------------------------------------------------- the final pass
inline const char* POST_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene;
uniform float uFade, uDim;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    // a soft vignette, very light
    vec2 q = vUv - 0.5;
    c *= 1.0 - 0.12 * dot(q, q) * 2.0;
    c *= 1.0 - uDim * 0.5;
    c *= uFade;
    frag = vec4(c, 1.0);
}
)";

} // namespace garden_shaders
