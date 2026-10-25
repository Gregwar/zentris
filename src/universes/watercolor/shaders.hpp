#pragma once
// WATERCOLOR's shaders (see watercolor.cpp).
//
// Everything is pigment on paper: colors are absorbances (K, per channel) and the paper shows through as
// paper * exp(-K) (Beer-Lambert), so washes glaze over each other like transparent watercolor. The background is
// built in three layers: the paper (cold-press relief, rendered once per size), the painted landscape (LAND_FS,
// reduced resolution, its absorbance and how much it granulates) and the wet layer (BLOOM_FS, the blooms of the pigment
// the beats drop, at a reduced resolution). COMPOSITE_FS lays them on the paper at full
// resolution (granulation in the paper's valleys, the relief, the slow motion's night glaze, the game over's run,
// the bridge). Pieces, pencil lines and the HUD are then glazed over it with dual-source blending (dst * mul + add):
// transparent pigments multiply, the toned paper's gouache adds.

namespace watercolor_shaders {

// Shared helpers, inserted after each fragment shader's #version line (uNoise: tileable, r g b perlin at periods 8,
// 16 and 32 across the texture, a cold-press domes).
inline const char* COMMON_GLSL = R"(
uniform sampler2D uNoise;
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
// p in lattice cells (one texture = 8 cells)
float nz(vec2 p) { return texture(uNoise, p * 0.125).r; }
float fbm(vec2 p) {
    const mat2 R = mat2(0.8, 0.6, -0.6, 0.8);
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { s += a * texture(uNoise, p * 0.125).r; p = R * p * 2.03 + 0.37; a *= 0.5; }
    return s / 0.9375;
}
float fbm2(vec2 p) {
    const mat2 R = mat2(0.8, 0.6, -0.6, 0.8);
    return (texture(uNoise, p * 0.125).r * 0.65 + texture(uNoise, (R * p) * 0.26 + 0.31).r * 0.35);
}
float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }
)";

// ---------------------------------------------------------------- the paper (rendered once per size)
// r: the cold-press relief (0..1), g: a fine grain, b: the sheet's large uneven tone.
inline const char* PAPER_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform float uScale;   // board units per pixel
void main() {
    vec2 p = gl_FragCoord.xy * uScale;   // board units, resolution independent
    const mat2 R = mat2(0.8, 0.6, -0.6, 0.8);
    // cold-press: a mottled tooth of rounded hills and pits, no weave and no network of ridges
    float h = 0.5 + 0.62 * (texture(uNoise, p * 0.26).b - 0.5) + 0.42 * (texture(uNoise, R * p * 0.17 + 0.31).g - 0.5)
            + 0.3 * (texture(uNoise, R * R * p * 0.5 + 0.7).b - 0.5) + 0.12 * (texture(uNoise, p * 0.035).r - 0.5);
    h = 0.5 + (h - 0.5) * 1.15;
    float g = hash12(gl_FragCoord.xy);
    float tone = fbm(p * 0.06 + 3.0) * 0.7 + fbm(p * 0.6 + 7.0) * 0.3;
    frag = vec4(clamp(h, 0.0, 1.0), g, tone, 1.0);
}
)";

// ---------------------------------------------------------------- the painted landscape (reduced resolution)
// Output: rgb the absorbance, a how much it granulates (0 smooth .. 1 dry brush skipping the paper's hills).
inline const char* LAND_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uView;          // center xy, half extents (board)
uniform float uSeed;
uniform float uHorizon, uReach, uSpread, uSkyDens, uGlaze, uWet, uGran;
uniform vec3 uSkyTopK, uSkyLowK, uSunK, uFarK, uMidK, uNearK, uTreeK, uAccentK;
uniform vec4 uSun;           // x, y, radius, lifted (1: the disc lifted out of the sky)
uniform float uSunPaint;     // a painted disc (0..1)
uniform vec4 uFar;           // height, roughness, density, presence
uniform vec4 uMid;           // height, roughness, density, presence
uniform vec4 uNear;          // type (0 meadow, 1 water, 2 rows), density, presence, -
uniform vec4 uTrees;         // type (0 none, 1 conifers, 2 round, 3 blossom), density, presence, -
uniform vec4 uBloom[3];      // backruns in the sky: x, y, radius, strength
uniform float uLpx;          // board units per pixel of this pass
uniform float uNight;        // slow motion: the night glaze (0..1)
uniform vec3 uNightK;
uniform vec3 uMoon;          // x, y, radius (board)
uniform float uFlood;        // Tetris: a fresh glaze of the accent
uniform vec3 uFloodK;

vec3 K; float G, W;
void lay(vec3 k, float d, float gran) {
    float w = d * dot(k, vec3(0.333));
    K += k * d; G += gran * w; W += w;
}
// a wash coming in with the level: a blotchy front rather than a fade
float presence(vec2 p, float pres, float s) {
    float n = fbm(p * 0.22 + s) * 0.8 + 0.1;
    return smoothstep(n - 0.12, n + 0.12, pres);
}
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    float sd = uSeed;
    K = vec3(0.0); G = 0.0; W = 1e-4;

    // ---- the painting's footprint on the sheet: an organic vignette with ragged edges, white paper around it
    float big = (fbm(p * 0.06 + sd) - 0.5) * 2.0;
    float topY = uHorizon + uReach + 2.4 * big + 0.7 * (fbm(vec2(p.x * 0.3, sd + 4.0)) - 0.5) * 2.0;
    float tUp = clamp((p.y - uHorizon) / max(uReach, 1.0), 0.0, 1.5);
    float botY = uView.y - uView.w + 0.4 + 1.2 * (fbm(vec2(p.x * 0.16, sd + 9.0)) - 0.5) * 2.0;
    float sideX = uView.z * (0.8 + 0.17 * uSpread) + 2.2 * (fbm(vec2(p.y * 0.08, sd + 7.0 + sign(p.x) * 3.0)) - 0.5) * 2.0
                - 3.5 * tUp * tUp - 2.5 * smoothstep(botY + 3.5, botY, p.y);
    float dTop = topY - p.y, dSide = sideX - abs(p.x), dBot = p.y - botY;
    // one ragged edge around the painting: wet and feathery, a little pigment pooled where it dried
    float disp = 0.55 * (fbm(p * 0.45 + sd + 2.0) - 0.5) * 2.0 + 0.2 * (fbm(p * 1.6 + 5.0) - 0.5) * 2.0 + 0.06 * (nz(p * 6.0) - 0.5);
    float E = min(min(dTop, dSide), dBot * 1.2) + disp;
    float paint = smoothstep(0.0, 0.1 + 0.35 * uWet, E);
    float edgeRim = exp(-max(E, 0.0) / (0.18 + 0.2 * uWet)) * paint * (1.0 - 0.5 * uWet);
    float lpx = uLpx;
    float shore = uHorizon - 0.75 + 0.1 * (fbm(vec2(p.x * 0.5, 40.0)) - 0.5);

    // ---- the sky: a graded, variegated wet-in-wet wash down to the shore
    float t = clamp((p.y - uHorizon) / max(uReach, 1.0), 0.0, 1.0);
    float v1 = fbm(p * 0.11 + sd);
    vec3 skyK = mix(uSkyLowK, uSkyTopK, smoothstep(0.0, 1.0, t * 1.15 + (v1 - 0.5) * 1.1));
    float skyM = paint * smoothstep(shore - 0.1, shore + 0.15, p.y);
    float skyD = uSkyDens * (0.5 + 1.0 * fbm(p * 0.17 + sd * 1.7)) * (1.0 - 0.3 * t);
    skyD += uSkyDens * 0.9 * edgeRim;
    // the sun: lifted out of the wash (a soft clean disc, a halo), or painted
    float sunN = (fbm(p * 1.3 + sd) - 0.5) * 0.12;
    float ds = length(p - uSun.xy) / max(uSun.z, 0.01) - 1.0 + sunN;
    float lift = mix(1.0, smoothstep(-0.05, 1.4, ds), uSun.w) * mix(1.0, smoothstep(0.0, 0.35, ds) * 0.5 + 0.5, uSun.w);
    skyD *= lift;
    // backruns (cauliflower blooms): a little lighter inside, a soft ragged fringe
    float sdBloom = 1.0;
    for (int i = 0; i < 3; i++) {
        if (uBloom[i].w <= 0.0) continue;
        vec2 q = p - uBloom[i].xy;
        float e = length(q) / uBloom[i].z - 1.0 + (fbm(p * 0.9 + float(i) * 7.0) - 0.5) * 1.1 + (fbm(p * 3.0 + float(i)) - 0.5) * 0.35;
        float inside = smoothstep(0.1, -0.3, e);
        sdBloom *= 1.0 - 0.32 * inside * uBloom[i].w;
        skyD += uSkyDens * 0.55 * exp(-abs(e - 0.03) / 0.07) * uBloom[i].w * (0.4 + 0.8 * fbm(p * 2.0 + 3.0));
    }
    skyD *= sdBloom;
    lay(skyK, skyD * skyM, uGran * 0.6);
    // a painted sun: a wet disc, darker rim
    float disc = smoothstep(0.08, -0.06, ds) * paint * uSunPaint * smoothstep(shore, shore + 0.3, p.y);
    lay(uSunK, disc * (0.5 + 0.35 * fbm(p * 0.8 + 2.0) + 0.6 * exp(-abs(ds) / 0.06)), 0.1);

    // ---- the far range: a wet edge into the sky, graded down into mist
    float edgeLift = smoothstep(4.0, 17.0, abs(p.x));
    float r1 = fbm(vec2(p.x * 0.07, sd + 20.0));
    float r2 = 1.0 - abs(fbm(vec2(p.x * 0.17, sd + 21.0)) - 0.5) * 2.0;
    float farTop = uHorizon + uFar.x * (0.3 + 0.7 * edgeLift) * (0.25 + mix(r1, r2 * r2, uFar.y) * 1.2);
    float f = farTop - p.y + 0.18 * (fbm(p * 1.4) - 0.5);
    float farA = smoothstep(-0.1, 0.12 + 0.35 * uWet, f) * paint * presence(p, uFar.w, 3.0);
    float farD = uFar.z * (0.45 + 0.75 * exp(-max(f, 0.0) / 1.6)) * (0.75 + 0.5 * fbm(p * 0.35 + 5.0));
    farD *= (1.0 - 0.75 * smoothstep(-0.2, -1.2, p.y - uHorizon)) * smoothstep(shore - 0.05, shore + 0.2, p.y);
    lay(uFarK, farD * farA, uGran);

    // ---- the middle range: a crisp edge with a dried rim, lighter toward its foot
    float m1 = fbm(vec2(p.x * 0.1, sd + 30.0));
    float m2 = 1.0 - abs(fbm(vec2(p.x * 0.24, sd + 31.0)) - 0.5) * 2.0;
    float baseY = uHorizon - 0.7;
    float midTop = baseY + uMid.x * (0.35 + 0.65 * edgeLift) * (0.2 + mix(m1, m2, uMid.y) * 1.1);
    float f2 = midTop - p.y + 0.07 * (fbm(p * 2.6) - 0.5) + 0.025 * (nz(p * 6.0) - 0.5);
    float midA = smoothstep(-0.6 * lpx, 0.6 * lpx + 0.01, f2) * paint * presence(p, uMid.w, 11.0);
    float midD = uMid.z * (0.5 + 0.55 * fbm(p * 0.45 + 9.0)) * (1.0 - 0.55 * smoothstep(0.2, 2.5, f2));
    midD += uMid.z * 0.5 * exp(-max(f2, 0.0) / max(0.09, lpx * 1.2));
    lay(uMidK, midD * midA, uGran * 0.5);

    // ---- the foreground
    float nearP = presence(p, uNear.z, 17.0) * paint;
    if (uNear.x > 0.5 && uNear.x < 1.5) {
        // water: the sky and the hills reflected, broken by lifted horizontal streaks
        float yw = shore;
        if (p.y < yw) {
            float depth = yw - p.y;
            float rip = (nz(vec2(p.x * 0.6, p.y * 9.0)) - 0.5) * 0.6;
            vec2 m = vec2(p.x + rip, 2.0 * yw - p.y);
            float mf2 = baseY + uMid.x * (0.35 + 0.65 * smoothstep(4.0, 17.0, abs(m.x))) * (0.2 + mix(fbm(vec2(m.x * 0.1, sd + 30.0)), 1.0 - abs(fbm(vec2(m.x * 0.24, sd + 31.0)) - 0.5) * 2.0, uMid.y) * 1.1) - m.y;
            float refl = smoothstep(-0.05, 0.1, mf2) * uMid.w;
            float streak = smoothstep(0.6, 0.72, nz(vec2(p.x * 0.18 + sd, p.y * 6.0)));
            // the sun's glitter: a broken column of lifted light under it
            float col = (1.0 - smoothstep(0.4, 1.1, abs(p.x - uSun.x + rip * 2.0) / max(uSun.z, 0.01))) * step(0.3, uSun.z)
                      * smoothstep(0.35, 0.6, nz(vec2(p.x * 0.3, p.y * 5.0 + sd)));
            float wd = uNear.y * (0.4 + 0.5 * smoothstep(0.0, 4.5, depth)) * (1.0 - 0.75 * streak) * (1.0 - 0.8 * col * (uSun.w + uSunPaint));
            vec3 wk = mix(mix(uSkyLowK, uSkyTopK, 0.3), uNearK, smoothstep(0.0, 5.0, depth) * 0.8 + 0.1);
            lay(wk, wd * nearP, uGran * 0.3);
            lay(uMidK, refl * uMid.z * 0.55 * (1.0 - 0.85 * streak) * (1.0 - smoothstep(1.0, 5.0, depth)) * nearP, 0.2);
        }
    } else {
        float gy = shore;
        float g = gy - p.y + 0.1 * (fbm(p * 1.8 + 3.0) - 0.5);
        float ga = smoothstep(-0.02, 0.05, g) * nearP;
        float gd = uNear.y * (0.55 + 0.6 * fbm(p * vec2(0.12, 0.9) + 13.0));
        if (uNear.x > 1.5) {
            // rows converging to the horizon (lavender): stripes of color between pale green lanes
            float a = atan(p.x, max(g + 0.6, 0.05) * 3.0);
            float s = 0.5 + 0.5 * sin(a * 26.0 + (fbm(p * 0.8) - 0.5) * 2.0);
            float row = smoothstep(0.35, 0.75, s) * smoothstep(0.0, 0.6, g);
            lay(uMidK * 0.6 + uNearK * 0.2, gd * 0.6 * ga, 0.4);
            lay(uNearK, gd * 1.3 * row * ga, 0.55);
        } else {
            lay(uNearK, gd * ga, 0.35);
        }
        // dry brush toward the bottom of the painting
        float dry = smoothstep(2.0, 0.0, dBot);
        G += dry * 1.2 * W;
    }

    // ---- trees standing on the shore, outside the well
    if (uTrees.x > 0.5) {
        float pres = presence(p, uTrees.z, 23.0) * paint;
        float best = 0.0;
        float blossom = 0.0;
        for (int i = 0; i < 9; i++) {
            float fi = float(i);
            float side = mod(fi, 2.0) < 0.5 ? -1.0 : 1.0;
            float x0 = side * (11.5 + fi * 1.15 + hash11(fi + sd) * 1.4);
            float hgt = (1.2 + 1.6 * hash11(fi * 3.1 + sd)) * (0.8 + 0.4 * smoothstep(11.0, 20.0, abs(x0)));
            vec2 q = p - vec2(x0, shore - 0.1);
            if (uTrees.x < 1.5) {
                // a conifer: a jagged narrow triangle
                float w = (hgt - q.y) / hgt * 0.42 * hgt * 0.45;
                float e = w - abs(q.x) + 0.12 * (nz(vec2(q.y * 6.0, fi)) - 0.5) * (q.y / hgt);
                float in1 = smoothstep(-0.02, 0.03, e) * step(0.0, q.y) * step(q.y, hgt);
                best = max(best, in1 * (1.0 + 0.6 * exp(-max(e, 0.0) / 0.06)));
            } else {
                // a round tree: a blotted crown on a thin trunk
                vec2 c = q - vec2(0.0, hgt * 0.62);
                float rr = hgt * 0.42;
                float e = rr * (1.0 + 0.28 * (fbm(p * 1.3 + fi) - 0.5) * 2.0) - length(c * vec2(1.0, 1.25));
                float crown = smoothstep(-0.02, 0.04, e);
                float trunk = smoothstep(0.06, 0.02, abs(q.x + 0.04 * sin(q.y * 3.0))) * step(0.0, q.y) * step(q.y, hgt * 0.5);
                best = max(best, max(crown * (0.85 + 0.5 * exp(-max(e, 0.0) / 0.08)), trunk * 1.2));
                blossom = max(blossom, crown * smoothstep(0.55, 0.75, nz(p * 3.5 + fi)));
            }
        }
        vec3 tk = uTrees.x > 2.5 ? uAccentK : uTreeK;
        lay(tk, best * uTrees.y * pres, 0.3);
        if (uTrees.x > 2.5) lay(uAccentK, blossom * uTrees.y * 0.8 * pres, 0.2);
    }

    // ---- the peak's glaze: deeper, more saturated
    float avgK = dot(K, vec3(0.3333));
    K = max(mix(vec3(avgK), K, 1.0 + 0.22 * uGlaze), 0.0) * (1.0 + 0.12 * uGlaze);
    // ---- Tetris: a fresh glaze of the accent washed over the painting
    lay(uFloodK, uFlood * smoothstep(0.25, 0.7, fbm(p * 0.25 + sd)) * paint * 0.3, 0.3);
    // ---- slow motion: a night glaze of indigo spreading over the painting, the moon lifted out of it
    float nightFront = smoothstep(0.0, 0.2, uNight * 1.25 - (fbm(p * 0.18 + 4.0) * 0.8 + (1.0 - vUv.y) * 0.25));
    float dm = length(p - uMoon.xy) / uMoon.z - 1.0 + (fbm(p * 1.4) - 0.5) * 0.1;
    float moon = smoothstep(0.05, -0.08, dm);
    float nightD = (0.7 + 0.5 * fbm(p * 0.2 + 9.0)) * (1.0 - moon) + exp(-max(dm, 0.0) / 0.12) * 0.35 * (1.0 - moon);
    K *= 1.0 - 0.6 * moon * nightFront;
    lay(uNightK, nightD * nightFront * paint * 0.62, 0.35);
    frag = vec4(K, clamp(G / W, 0.0, 1.5));
}
)";

// ---------------------------------------------------------------- the wet layer (blooms, reduced resolution)
// Every drop of pigment the music lets fall is a bloom: it spreads (fast, then slower), its pigment diluting as it
// goes, its edge feathery while wet and ragged like a cauliflower, then it dries with a darker rim and slowly fades
// from the sheet. A backrun (water dropped into a drying wash) pushes the pigment out: a lighter heart (negative
// absorbance: it lifts the washes under it) and a dark ragged fringe. Accumulated additively: rgb absorbance, a the
// wetness.
// a0: center xy, radius, age (seconds). a1: absorbance rgb, strength. a2: seed, kind (0 bloom, 1 backrun, 2 spatter), life, -.
inline const char* BLOOM_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
layout(location = 2) in vec4 a1;
layout(location = 3) in vec4 a2;
uniform vec4 uView;
out vec2 vLocal;
out vec2 vWorld;
flat out vec4 v0, v1, v2;
void main() {
    vec2 l = aCorner * a0.z * 1.7;
    vec2 p = a0.xy + l;
    vLocal = l;
    vWorld = p;
    v0 = a0; v1 = a1; v2 = a2;
    gl_Position = vec4((p - uView.xy) / uView.zw, 0.0, 1.0);
}
)";
inline const char* BLOOM_FS = R"(#version 330 core
in vec2 vLocal;
in vec2 vWorld;
flat in vec4 v0, v1, v2;
out vec4 frag;
// noise around a circle: periodic in the angle
float ring(vec2 dir, float k, float s) { return textureLod(uNoise, dir * k * 0.125 + vec2(s, s * 1.7), 0.0).r * 2.0 - 1.0; }
void main() {
    float R = v0.z, t = v0.w, seed = v2.x, kind = v2.y, life = v2.z;
    float grow = 1.0 - exp(-t / (kind > 1.5 ? 0.08 : (kind > 0.5 ? 0.7 : 0.45)));
    float wet = exp(-t / (kind > 1.5 ? 0.6 : 2.2));
    float r = R * (0.3 + 0.7 * grow);
    float len = length(vLocal);
    vec2 dir = vLocal / max(len, 1e-4);
    // a cauliflower edge: lobes of a few sizes, finer while it spreads
    float edge = 1.0 + 0.28 * ring(dir, 1.1, seed) + 0.12 * ring(dir, 2.9, seed + 3.0) + 0.06 * ring(dir, 6.5, seed + 7.0);
    float fib = (fbm(vWorld * 2.5 + seed * 11.0) - 0.5) * (0.25 + 0.35 * wet) + (nz(vWorld * 7.0) - 0.5) * 0.08;
    float d = len / (r * edge) - 1.0 + fib;
    float soft = kind > 1.5 ? 0.06 : 0.12 + 0.3 * wet;
    float cov = smoothstep(soft, -soft, d);
    float fade = 1.0 - smoothstep(life * 0.55, life, t);
    float mott = 0.55 + 0.9 * fbm(vWorld * 0.9 + seed * 5.0);
    float dil = 1.0 / (0.45 + 0.55 * grow);                  // the pigment dilutes as it spreads
    float rim = exp(-abs(d + 0.04) / (0.05 + 0.1 * wet)) * (1.0 - wet * 0.85) * (0.3 + 0.9 * fbm(vWorld * 1.7 + seed));
    vec3 K;
    if (kind > 0.5 && kind < 1.5) {
        // a backrun: the heart lifted, the pigment piled in a dark fringe
        float heart = smoothstep(-0.05, -0.35, d);
        K = v1.rgb * v1.a * (rim * 1.4 * (0.5 + 0.8 * fbm(vWorld * 2.0 + seed)) - 0.45 * heart * mott);
        K *= fade;
        frag = vec4(K, cov * wet * fade);
        return;
    }
    float dens = v1.a * mott * dil * (kind > 1.5 ? 1.0 : 0.75) + v1.a * rim * (kind > 1.5 ? 0.9 : 0.35);
    K = v1.rgb * dens * cov * fade;
    frag = vec4(K, cov * wet * fade);
}
)";

// ---------------------------------------------------------------- the composite (full resolution)
inline const char* COMPOSITE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uPaper, uLand, uWet;
uniform vec4 uView;
uniform vec2 uRes;
uniform vec3 uPaperCol;
uniform float uPx;
uniform vec4 uWell;          // the masking: half extents x, y; inside strength; rim
uniform float uWetGain;
uniform float uRun;          // game over: the painting runs down and is washed out (0..1)
uniform float uBridge;
uniform vec3 uBridgeCol;
uniform float uLift;         // the whole painting's strength (0..1)
uniform float uSeed;
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    vec4 paper = texture(uPaper, vUv);
    float h = paper.r;
    // ---- the painting (with the game over's run: streaks pulled down the sheet)
    vec2 uv = vUv;
    float runN = nz(vec2(p.x * 1.4 + uSeed, 3.0));
    uv.y += uRun * (0.02 + 0.12 * runN * runN) * smoothstep(0.0, 0.3, uRun);
    vec4 land = texture(uLand, uv);
    vec4 wet = texture(uWet, uv);
    vec3 K = land.rgb * uLift;
    float gran = land.a;
    // the well is masked: only a faint ghost of the washes, a crisp edge with pigment pooled against it
    float e = sdBox(p, uWell.xy) + (fbm(p * 2.2) - 0.5) * 0.06 + (nz(p * 8.0) - 0.5) * 0.02;
    float inside = smoothstep(0.015, -0.015, e);
    float pool = exp(-max(e, 0.0) / 0.07) * (1.0 - inside) * uWell.w;
    K *= mix(1.0 + pool, uWell.z, inside);
    vec3 wk = wet.rgb * uWetGain * (1.0 - inside);
    float wetness = clamp(wet.a, 0.0, 1.0);
    K = max(K + wk, 0.0);
    gran = mix(gran, 0.45, clamp(dot(wk, vec3(0.33)) / max(dot(K, vec3(0.33)), 1e-3), 0.0, 1.0));
    // granulation: the pigment settles in the paper's valleys and skips the hills
    float gf = clamp(1.0 + gran * 2.0 * (0.52 - h), 0.0, 2.2);
    K *= gf;
    // the game over's wash: water floods the sheet from the top and lifts the pigment back to paper
    K *= 1.0 - 0.85 * smoothstep(0.35, 1.0, uRun) * smoothstep(-0.25, 0.25, uRun * 1.7 - (1.0 - vUv.y) - (fbm(p * 0.35 + 8.0) - 0.5) * 0.9 - runN * 0.2);
    // ---- the paper: its tone, its relief lit from the top left, wet areas a little darker and glossier
    vec3 paperC = uPaperCol * (0.97 + 0.06 * paper.b);
    float hl = texture(uPaper, vUv + vec2(-1.5, 1.5) / uRes).r;
    paperC *= 1.0 + 0.07 * (h - hl) - 0.025 * (0.5 - h);
    vec3 col = paperC * exp(-K);
    col *= 1.0 - 0.06 * wetness * (1.0 - inside);
    // ---- the bridge: the sheet washed back to plain paper of the bridge's color, from the well outward
    float bd = length(p / vec2(uView.z, uView.w)) * 1.0 + (fbm(p * 0.3 + 2.0) - 0.5) * 0.5;
    float washed = smoothstep(bd - 0.08, bd + 0.08, uBridge * 1.6 - 0.1);
    float tide = exp(-abs(uBridge * 1.6 - 0.1 - bd) / 0.03) * (1.0 - smoothstep(0.85, 1.0, uBridge)) * step(0.01, uBridge);
    vec3 plain = mix(paperC, uBridgeCol, smoothstep(0.0, 1.0, uBridge));
    col = mix(col, plain, washed);
    col *= 1.0 - 0.12 * tide;
    frag = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- cells: pieces, stack, previews, the gauge, spatter

// a0: center xy, half size (board), neighbor mask (8 bits). a1: absorbance rgb, alpha.
// a2: kind, wetness, seed, extra. a3: body color rgb (gouache, linear), body amount.
inline const char* CELL_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
layout(location = 2) in vec4 a1;
layout(location = 3) in vec4 a2;
layout(location = 4) in vec4 a3;
uniform mat4 uVP;
out vec2 vLocal;
out vec2 vWorld;
flat out vec4 v0, v1, v2, v3;
void main() {
    float kind = a2.x;
    vec2 ext = vec2(0.72);
    if (kind > 1.5 && kind < 2.5) ext = vec2(0.72, 0.72 + a2.w * 2.4);   // running down (game over)
    if (kind > 3.5 && kind < 4.5) ext = vec2(1.25);                       // the gauge
    if (kind > 4.5) ext = vec2(0.5 * a2.w + 0.4, 0.7);                    // a lift stroke (a row)
    vec2 l = aCorner * ext;
    if (kind > 1.5 && kind < 2.5) l.y = l.y < 0.0 ? -(0.72 + a2.w * 2.4) : 0.72;
    vec2 p = a0.xy + l * a0.z * 2.0;
    vLocal = l;
    vWorld = p;
    v0 = a0; v1 = a1; v2 = a2; v3 = a3;
    gl_Position = uVP * vec4(p, 0.0, 1.0);
}
)";
inline const char* CELL_FS = R"(#version 330 core
in vec2 vLocal;
in vec2 vWorld;
flat in vec4 v0, v1, v2, v3;
layout(location = 0, index = 0) out vec4 fragAdd;
layout(location = 0, index = 1) out vec4 fragMul;
uniform sampler2D uPaper;
uniform vec2 uRes;
uniform float uPx;           // board units per pixel
uniform float uGran;
uniform float uBridge;
uniform vec3 uBridgeK;
uniform float uTime;

bool bit(float m, float b) { return mod(floor(m / b), 2.0) > 0.5; }

void outPigment(vec3 K, float cov, vec3 body, float bodyAmt) {
    vec3 T = exp(-K);
    float b = bodyAmt * cov;
    fragMul = vec4(mix(vec3(1.0), T, cov) * (1.0 - b), 1.0);
    fragAdd = vec4(body * b, 0.0);
}

void main() {
    float kind = v2.x, wet = v2.y, seed = v2.z;
    float m = v0.w;
    vec2 q = vLocal;                     // cell units: the cell is [-0.5, 0.5]
    float h = texture(uPaper, gl_FragCoord.xy / uRes).r;
    float aa = uPx / (v0.z * 2.0) * 1.2;
    vec3 K = v1.rgb;
    K = mix(K, uBridgeK, uBridge);
    vec2 wp = vWorld;

    if (kind > 3.5 && kind < 4.5) {
        // ---- the gauge: a round pan filling with a wash from the bottom (v2.w: fill, v3.x ready, v3.y active)
        float r = length(q);
        float R = 0.5;
        float fill = v2.w;
        float lvl = -R + 2.0 * R * fill + 0.025 * sin(q.x * 9.0 + uTime * 2.0) * (1.0 - v3.y);
        float disc = smoothstep(R + aa, R - aa, r + (nz(wp * 6.0) - 0.5) * 0.03);
        float below = smoothstep(lvl + 0.02, lvl - 0.02, q.y);
        float d = (0.55 + 0.35 * fbm(wp * 1.4 + seed)) + 0.7 * exp(-abs(q.y - lvl) / 0.035) * (1.0 - step(0.999, fill));
        d += 0.5 * exp(-(R - r) / 0.03);
        float ready = v3.x;
        d *= 1.0 + ready * (0.25 + 0.15 * sin(uTime * 3.0));
        float cov = disc * below * v1.a;
        float gf = clamp(1.0 + uGran * 2.0 * (0.52 - h), 0.0, 2.0);
        outPigment(K * d * gf, cov, vec3(0.0), 0.0);
        return;
    }

    if (kind > 4.5) {
        // ---- the tide mark a wet brush leaves when it lifts a row: two faint ragged lines, a bloom at the end
        float half_ = 0.5 * v2.w;
        float y = q.y;
        float top = 0.5 + 0.06 * (fbm(vec2(wp.x * 1.3, seed)) - 0.5) * 2.0;
        float bot = -0.5 + 0.06 * (fbm(vec2(wp.x * 1.3, seed + 5.0)) - 0.5) * 2.0;
        float lines = (exp(-abs(y - top) / 0.02) + exp(-abs(y - bot) / 0.02)) * 0.5;
        float span = smoothstep(half_ + 0.3, half_ - 0.2, abs(q.x));
        float inside = smoothstep(top + 0.02, top - 0.02, y) * smoothstep(bot - 0.02, bot + 0.02, y);
        float d = (lines * 0.8 + inside * 0.18 * fbm(vec2(wp.x * 0.5, wp.y * 8.0))) * span;
        outPigment(K * d, v1.a, vec3(0.0), 0.0);
        return;
    }

    if (kind > 2.5) {
        // ---- a spatter dot: a round drop dried with a dark rim
        float r = length(q) * 2.0;
        float e = 1.0 + (fbm(wp * 9.0 + seed) - 0.5) * 0.35 - r;
        float cov = smoothstep(-0.04, 0.04, e) * v1.a;
        float d = 0.6 + 0.8 * exp(-max(e, 0.0) / 0.15);
        outPigment(K * d, cov, v3.rgb, v3.w);
        return;
    }

    // ---- a cell of a wash: the region of same-color neighbors is one wash (mask bits: 1 L, 2 R, 4 D, 8 U,
    // 16 LD, 32 RD, 64 LU, 128 RU)
    bool L = bit(m, 1.0), Rr = bit(m, 2.0), D = bit(m, 4.0), U = bit(m, 8.0);
    // beyond an open side belongs to the neighbor's own cell
    if ((L && q.x < -0.5) || (Rr && q.x > 0.5) || (D && q.y < -0.5) || (U && q.y > 0.5)) discard;
    float d = 9.0;
    if (!L) d = min(d, q.x + 0.5);
    if (!Rr) d = min(d, 0.5 - q.x);
    if (!D) d = min(d, q.y + 0.5);
    if (!U) d = min(d, 0.5 - q.y);
    if (L && D && !bit(m, 16.0)) d = min(d, q.x < -0.5 && q.y < -0.5 ? -min(-0.5 - q.x, -0.5 - q.y) : length(max(q + 0.5, 0.0)));
    if (Rr && D && !bit(m, 32.0)) d = min(d, q.x > 0.5 && q.y < -0.5 ? -min(q.x - 0.5, -0.5 - q.y) : length(max(vec2(0.5 - q.x, q.y + 0.5), 0.0)));
    if (L && U && !bit(m, 64.0)) d = min(d, q.x < -0.5 && q.y > 0.5 ? -min(-0.5 - q.x, q.y - 0.5) : length(max(vec2(q.x + 0.5, 0.5 - q.y), 0.0)));
    if (Rr && U && !bit(m, 128.0)) d = min(d, q.x > 0.5 && q.y > 0.5 ? -min(q.x - 0.5, q.y - 0.5) : length(max(0.5 - q, 0.0)));
    float sc = v0.z * 2.0;               // board units per cell
    vec2 nw = wp / sc;                   // noise in cell units (continuous across the region)
    float run = kind > 1.5 && kind < 2.5 ? v2.w : 0.0;
    if (run > 0.0 && q.y < -0.5 && q.y > -3.5) {
        // running down: one or two drips under the cell, thin runs ending in a bead
        float best = -9.0;
        for (int i = 0; i < 2; i++) {
            float hx = fract(sin(seed * 12.9 + float(i) * 78.2) * 43758.5);
            float xd = -0.32 + 0.64 * hx;
            float len = run * (0.4 + 1.8 * fract(hx * 7.13));
            float w = 0.05 + 0.04 * fract(hx * 3.7);
            float yy = clamp(q.y, -0.5 - len, -0.5);
            float cap = w - length(vec2(q.x - xd, q.y - yy));
            float bead = w * 1.7 - length(vec2(q.x - xd, q.y + 0.5 + len));
            best = max(best, max(cap, bead));
        }
        d = best;
    }
    // the edge: a little irregular, pushed out while wet; a hairline of paper between different colors
    float bleed = -0.045 + 0.07 * wet;
    float edgeN = (fbm(nw * 2.4) - 0.5) * (0.14 + 0.06 * wet) + (nz(nw * 11.0) - 0.5) * 0.025;
    float de = d + bleed + edgeN;
    float soft = aa + 0.05 * wet;
    float cov = smoothstep(-soft, soft, de) * v1.a;
    if (cov <= 0.002) discard;
    // the wash: uneven, pooled toward the bottom, a dark rim where it dried (wet: no rim, darker, flatter)
    float dens = 0.6 + 0.5 * (fbm(nw * 0.7 + 3.7 + seed) - 0.5) * 2.0 * (1.0 - 0.5 * wet);
    dens += 0.1 * (fbm(nw * 2.3 + 1.1) - 0.5) * 2.0;
    // pigment settles toward the bottom of the wash
    dens += 0.12 * clamp(-q.y, -0.5, 0.5);
    float rimW = 0.05 + 0.04 * (1.0 - wet);
    float rim = exp(-max(de, 0.0) / rimW) * (0.6 + 0.5 * fbm(nw * 4.0 + 1.0));
    dens += rim * (1.0 - wet) * 0.85;
    dens *= 1.0 + 0.18 * wet;
    // lifted (a cleared row): the brush takes the pigment back in streaks
    float lifted = kind > 0.5 && kind < 1.5 ? v2.w : 0.0;
    if (lifted > 0.0) {
        float streak = nz(vec2(wp.x * 0.4 + seed, wp.y * 12.0));
        dens *= mix(1.0, 0.08 + 0.35 * streak * streak, lifted);
    }
    // inside one piece the cells stay countable: a faint lighter hairline where two strokes meet, a touch of
    // pigment pooled along it
    float dOpen = 9.0;
    if (L) dOpen = min(dOpen, abs(q.x + 0.5));
    if (Rr) dOpen = min(dOpen, abs(0.5 - q.x));
    if (D) dOpen = min(dOpen, abs(q.y + 0.5));
    if (U) dOpen = min(dOpen, abs(0.5 - q.y));
    dOpen += (nz(nw * 9.0) - 0.5) * 0.02;
    dens *= 1.0 - 0.38 * exp(-max(dOpen, 0.0) / 0.018) * (1.0 - 0.6 * wet);
    dens += 0.16 * exp(-abs(dOpen - 0.05) / 0.03) * (1.0 - wet);
    float gf = clamp(1.0 + uGran * 2.0 * (0.52 - h), 0.0, 2.0);
    dens *= mix(1.0, gf, 1.0 - 0.6 * wet);
    // a mixed pigment separating a little in the wash
    vec3 sep = 1.0 + 0.22 * vec3(fbm(nw * 0.8 + 9.0) - 0.5, fbm(nw * 0.8 + 19.0) - 0.5, fbm(nw * 0.8 + 29.0) - 0.5) * 2.0;
    outPigment(K * sep * dens, cov, v3.rgb * (0.9 + 0.2 * h) * (1.0 - 0.35 * rim) * (0.92 + 0.16 * fbm(nw * 1.2 + 4.0)), v3.w * (1.0 - lifted));
}
)";

// ---------------------------------------------------------------- graphite pencil lines
// a0: a, b (board). a1: width (board), alpha, seed, -.
inline const char* PENCIL_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
layout(location = 2) in vec4 a1;
uniform mat4 uVP;
out vec2 vSeg;      // along (0..len), across (board)
out vec2 vWorld;
flat out vec4 v0, v1;
void main() {
    vec2 a = a0.xy, b = a0.zw;
    vec2 d = b - a;
    float len = max(length(d), 1e-4);
    vec2 t = d / len, n = vec2(-t.y, t.x);
    float w = a1.x * 2.5;
    float u = aCorner.x * 0.5 + 0.5;
    vec2 p = a + t * (u * (len + 2.0 * w) - w) + n * aCorner.y * w;
    vSeg = vec2(u * (len + 2.0 * w) - w, aCorner.y * w);
    vWorld = p;
    v0 = vec4(len, 0.0, 0.0, 0.0);
    v1 = a1;
    gl_Position = uVP * vec4(p, 0.0, 1.0);
}
)";
inline const char* PENCIL_FS = R"(#version 330 core
in vec2 vSeg;
in vec2 vWorld;
flat in vec4 v0, v1;
layout(location = 0, index = 0) out vec4 fragAdd;
layout(location = 0, index = 1) out vec4 fragMul;
uniform sampler2D uPaper;
uniform vec2 uRes;
uniform float uPx;
uniform vec3 uGraphite;      // linear transmittance of a full stroke
uniform float uBridge;
void main() {
    float len = v0.x, w = v1.x;
    float wob = (nz(vec2(vSeg.x * 0.9 + v1.z * 7.0, v1.z)) - 0.5) * w * 1.2;
    float across = abs(vSeg.y - wob);
    float ww = max(w, uPx * 0.7);
    float line = smoothstep(ww + uPx, ww * 0.3, across);
    // pressure: lighter at the ends of the stroke
    float ends = smoothstep(-ww, len * 0.08 + ww, vSeg.x) * smoothstep(len + ww, len - len * 0.08 - ww, vSeg.x);
    float h = texture(uPaper, gl_FragCoord.xy / uRes).r;
    float tooth = smoothstep(0.25, 0.7, h + (texture(uPaper, gl_FragCoord.xy / uRes).g - 0.5) * 0.3);
    float a = line * (0.35 + 0.65 * ends) * (0.3 + 0.7 * tooth) * v1.y * (1.0 - uBridge * 0.5);
    fragMul = vec4(mix(vec3(1.0), uGraphite, a), 1.0);
    fragAdd = vec4(0.0);
}
)";

// ---------------------------------------------------------------- flat shapes (HUD panels), text

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
layout(location = 0, index = 0) out vec4 fragAdd;
layout(location = 0, index = 1) out vec4 fragMul;
void main() { fragAdd = vec4(vCol.rgb * vCol.a, 0.0); fragMul = vec4(vec3(1.0 - vCol.a), 1.0); }
)";

inline const char* TEXT_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;   // pixels, top-left origin
layout(location = 1) in vec2 aUv;
uniform vec2 uRes;
out vec2 vUv;
out vec2 vPix;
void main() {
    vUv = aUv;
    vPix = aPos;
    gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
}
)";
// Ink letters: a transparent ink (multiplied), a touch darker at the edges where it dried.
inline const char* TEXT_FS = R"(#version 330 core
in vec2 vUv;
in vec2 vPix;
layout(location = 0, index = 0) out vec4 fragAdd;
layout(location = 0, index = 1) out vec4 fragMul;
uniform sampler2D uFont;
uniform vec3 uInk;           // linear color of the ink at full strength
uniform float uAlpha, uBias, uGouache;
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.75;
    float fill = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    float rim = smoothstep(0.5 - uBias + w * 4.0, 0.5 - uBias, d) * fill;
    float a = fill * uAlpha * (0.86 + 0.14 * rim);
    vec3 K = -log(max(uInk, vec3(0.003)));
    // on toned paper the letters are white gouache
    fragMul = vec4(mix(exp(-K * a), vec3(1.0 - a), uGouache), 1.0);
    fragAdd = vec4(uInk * a * uGouache, 0.0);
}
)";

// ---------------------------------------------------------------- the post
inline const char* POST_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene;
uniform float uFade, uDim;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    c *= 1.0 - uDim * 0.5;
    c *= uFade;
    frag = vec4(pow(max(c, 0.0), vec3(1.0 / 2.2)), 1.0);
}
)";

} // namespace watercolor_shaders
