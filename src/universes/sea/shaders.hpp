#pragma once
// SEA's shaders (see sea.cpp): the sky and the layered sea with its boats, the glass cubes of water, the sprites
// (shards, droplets, spray, the bonus gauge), the HUD letters and the final pass.

namespace sea_shaders {

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
)";

// The sea's swell, shared by the sea and the boats (sea.cpp's layerTop is the same function).
inline const char* WAVE_GLSL = R"(
uniform float uLevel, uSpread, uAmp, uSharp, uDir, uWaveT;
// The sea's layers: offsets above the level, amplitude, frequency and speed of their swell (SEA_WAVES in sea.cpp).
const vec3 L_OFF = vec3(1.15, 0.55, 0.0);
const vec3 L_AMP = vec3(0.35, 0.65, 1.0);
const vec3 L_FRQ = vec3(1.7, 1.25, 0.85);
const vec3 L_SPD = vec3(0.6, 0.8, 1.0);

float crest(float s) {
    float c = s * 0.5 + 0.5;
    return 2.0 * pow(c, 1.0 + 2.5 * uSharp) - 1.0;
}
// behind the well the sea lies flat and calm
float flatAt(float x) { return mix(1.0, 0.12, smoothstep(5.9, 4.9, abs(x))); }
float layerTop(int k, float x) {
    float A = uAmp * L_AMP[k] * flatAt(x);
    float p = uWaveT * L_SPD[k];
    float f = L_FRQ[k];
    float fk = float(k);
    float s1 = crest(sin(x * 0.55 * f - p * uDir + fk * 1.7));
    float s2 = crest(sin(x * 1.13 * f - p * 1.37 * uDir + fk * 3.1 + 1.3));
    float s3 = sin(x * 2.9 * f - p * 0.6 * uDir + fk * 0.9);
    return uLevel + L_OFF[k] * uSpread + A * (0.55 * s1 + 0.32 * s2 + 0.13 * s3);
}
float layerBase(int k) { return uLevel + L_OFF[k] * uSpread; }
// the sandy bed under the sea: low dunes (sea.cpp's sandTop is the same function)
float sandTop(float x) { return -9.35 + 0.55 * sin(x * 0.21 + 1.3) + 0.3 * sin(x * 0.53 + 0.4) + 0.1 * sin(x * 1.37 + 2.0); }
// how dark the water is this far under the surface (sea.cpp's darkAt)
float darkAt(float depth) { return smoothstep(0.5, 10.0, depth); }
// the well's panel: more opaque over the dark depths, so the glass stays easy to read there
float panelK(float pa, vec2 w) { return mix(pa, 0.88, smoothstep(2.0, 10.0, layerTop(2, w.x) - w.y)); }

)";

// ---------------------------------------------------------------- the sky and the sea
// A side view: a plain sky with one sun (or moon) and a few long soft clouds, over a sea in three layers like cut
// paper (far, middle, near), each with its own swell. The sea's level rises with the cleared lines. Boats sail on
// the layers' crests, the far ones smaller and hazier. A drop turns it into a storm. The well is a calm, subdued
// panel over it.
inline const char* SEA_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uView;          // center x, y, half extents x, y (board units)
uniform float uPx, uTime, uCloudT;
uniform vec3 uSkyTop, uSkyLow, uSunCol, uCloud, uStormSky, uStormCloud, uDusk;
uniform vec4 uSun;           // x, y, radius, moon (0 / 1)
uniform vec3 uSea0, uSea1, uSea2, uDeep, uFoam, uHaze, uStormSea;
uniform vec3 uPanel, uSand;
uniform float uPanelA;
uniform float uStorm, uCaps, uGlint, uRain, uFlash, uFlashX, uSlow, uBridge, uStars, uClouds, uLifeGlow;
uniform vec3 uBridgeCol;


void main() {
    vec2 w = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    float wellD;
    {
        vec2 wd = abs(w) - vec2(5.08, 10.08);
        float rr = 0.25;
        wellD = length(max(wd + rr, 0.0)) + min(max(wd.x + rr, wd.y + rr), 0.0) - rr;
    }
    float inWell = smoothstep(uPx, -uPx, wellD);
    // ---- the sea's layers' tops; a pixel deep inside one of them shows nothing behind it
    float tops[3] = float[3](layerTop(0, w.x), layerTop(1, w.x), layerTop(2, w.x));
    int kStart = -1;
    for (int k = 2; k >= 0; k--)
        if (tops[k] - w.y > uPx * 1.5) { kStart = k; break; }
    float horizon = layerBase(0);
    vec3 col = vec3(0.0);
    if (kStart < 0) {
    // ---- the sky: a plain gradient down to the far sea
    float h = clamp((w.y - horizon) / max(uView.y + uView.w - horizon, 1.0), 0.0, 1.0);
    vec3 sky = mix(uSkyLow, uSkyTop, smoothstep(0.0, 1.0, pow(h, 0.7)));
    vec3 stormSky = mix(uStormSky * 1.1, uStormSky * 0.6, h);
    sky = mix(sky, stormSky, uStorm * 0.85);
    sky = mix(sky, mix(uDusk * 1.3, uDusk * 0.7, h), uSlow * 0.8);
    // stars (the night, slow motion): a few faint points, still
    float st = max(uStars, uSlow * 0.7) * (1.0 - uStorm);
    if (st > 0.01) {
        vec2 c = floor(w * 2.2);
        if (hash12(c + 7.0) > 0.985) {
            vec2 q = fract(w * 2.2) - 0.5 - (vec2(hash12(c + 1.3), hash12(c + 2.9)) - 0.5) * 0.6;
            sky += vec3(0.75, 0.8, 0.95) * smoothstep(0.09, 0.0, length(q)) * st * smoothstep(-2.0, 6.0, w.y - horizon) * 0.5 *
                   (0.4 + 0.6 * hash12(c + 5.1));
        }
    }
    // the sun or the moon, veiled by the storm
    float sd = length(w - uSun.xy);
    float sunA = smoothstep(uSun.z + uPx * 1.5, uSun.z - uPx * 1.5, sd);
    float halo = exp(-max(sd - uSun.z, 0.0) / (uSun.z * (1.8 + 0.6 * (1.0 - uSun.w)))) * (0.24 + 0.1 * (1.0 - uSun.w));
    float veil = 1.0 - uStorm;
    sky = mix(sky, uSunCol, halo * veil * (1.0 - 0.5 * uSlow));
    sky = mix(sky, uSunCol, sunA * veil * (1.0 - 0.4 * uSlow));
    // long soft clouds, drifting with the wind; heavier and darker in a storm
    float band = smoothstep(horizon + 1.0, horizon + 5.0, w.y) * (1.0 - smoothstep(9.0, 14.0, w.y - horizon * 0.2));
    if (band > 0.001 || uStorm > 0.001) {
        float cov = uClouds + 0.55 * uStorm;
        vec2 cp = vec2(w.x * 0.07 - uCloudT, w.y * 0.32);
        float n = fbm(cp + vec2(0.0, 3.0)) + 0.25 * fbm(cp * 2.3 + 9.0);
        float cl = smoothstep(1.05 - cov * 0.55, 1.25 - cov * 0.4, n) * mix(band, 1.0, uStorm * 0.8);
        vec3 cc = mix(uCloud, uStormCloud * (0.8 + 0.4 * smoothstep(0.8, 1.4, n)), uStorm);
        cc = mix(cc, uDusk * 1.2, uSlow * 0.6);
        // lightning: a glow inside the clouds, far from the well
        cc += vec3(0.85, 0.9, 1.0) * uFlash * exp(-length((w - vec2(uFlashX, horizon + 7.0)) * vec2(0.22, 0.35))) * 1.3;
        sky = mix(sky, cc, cl * mix(0.55, 0.85, uStorm));
        sky += vec3(0.6, 0.65, 0.8) * uFlash * 0.1 * exp(-abs(w.x - uFlashX) * 0.12) * smoothstep(horizon, horizon + 8.0, w.y);
    }
    col = sky;
    }
    // ---- the sea's layers, far to near (the boats are drawn after, hidden by the water in front of them)
    vec3 seaC[3] = vec3[3](uSea0, uSea1, uSea2);
    for (int k = max(kStart, 0); k < 3; k++) {
        float top = tops[k];
        float depth = top - w.y;
        if (depth > -uPx * 1.5) {
            float A = max(uAmp * L_AMP[k], 1e-3);
            vec3 c = seaC[k];
            // a lighter skin at the crest, deeper water further down
            c = mix(c, uHaze, 0.22 * exp(-max(depth, 0.0) / 0.3) * (1.0 - 0.3 * float(k)));
            c = mix(c, uDeep, smoothstep(0.0, 8.0, depth) * 0.75 * float(k) * 0.5);
            // long soft streaks on the water
            c *= 0.96 + 0.08 * vnoise(vec2(w.x * 0.5 - uWaveT * 0.3 * uDir, w.y * 9.0 + float(k) * 5.0)) * exp(-max(depth, 0.0) / 4.0);
            // the storm: darker, greyer water (near the surface: the depths are dark already)
            float dk = k == 2 ? darkAt(depth) : 0.0;
            c = mix(c, uStormSea * (1.35 - 0.3 * float(k)), uStorm * 0.6 * (1.0 - dk));
            c = mix(c, uDusk * (0.55 - 0.1 * float(k)), uSlow * 0.7 * (1.0 - dk));
            if (k == 2) {
                // light from the surface: long soft shafts slanting down, fading with depth
                float ray = vnoise(vec2((w.x + (w.y - top) * 0.35) * 0.8 + uWaveT * 0.04, 0.5));
                c += uHaze * 0.08 * smoothstep(0.55, 0.9, ray) * exp(-max(depth, 0.0) / 3.0) * (1.0 - uStorm) * (1.0 - uSlow);
                // the deep: darker and darker, to a near-black blue
                c = mix(c, uDeep * 0.2, dk * 0.92);
                // a few points of light living in the dark, each drifting and glowing at its own slow pace
                if (dk > 0.4) {
                    vec2 g = w * 1.1 + vec2(uTime * 0.02, 0.0);
                    vec2 gc = floor(g);
                    float r = hash12(gc + 31.0);
                    if (r > 0.9) {
                        vec2 o = vec2(hash12(gc + 4.1), hash12(gc + 8.3)) * 0.6 + 0.2 +
                                 0.1 * vec2(sin(uTime * 0.3 + r * 30.0), cos(uTime * 0.23 + r * 17.0));
                        float d = length(fract(g) - o);
                        float tw = 0.55 + 0.45 * sin(uTime * (0.3 + r * 0.5) + r * 60.0);
                        vec3 bc = mix(vec3(0.35, 0.9, 1.0), vec3(0.6, 1.0, 0.75), hash12(gc + 2.2));
                        c += bc * (smoothstep(0.06, 0.02, d) * 0.8 + exp(-d * 16.0) * 0.18) * tw * smoothstep(0.4, 0.9, dk) * uLifeGlow;
                    }
                }
            }
            // glints: short sparks of light on the water, most under the sun
            if (uGlint > 0.01) {
                vec2 g = vec2(w.x * 2.4 + float(k) * 7.0, w.y * 10.0);
                vec2 gc = floor(g);
                float r = hash12(gc + float(k) * 13.0);
                float under = exp(-abs(w.x - uSun.x) / 5.0) * (1.0 - uSun.w * 0.4);
                float thr = 0.965 - 0.05 * under;
                if (r > thr) {
                    vec2 gq = fract(g) - 0.5;
                    float tw = 0.5 + 0.5 * sin(uTime * (0.9 + r * 1.3) + r * 40.0);
                    float dash = smoothstep(0.42, 0.1, abs(gq.x)) * smoothstep(0.22, 0.0, abs(gq.y));
                    float fade = exp(-max(depth, 0.0) / (1.2 + 2.0 * float(k))) * (1.0 - dk);
                    c = mix(c, mix(uSunCol, vec3(1.0), 0.3), dash * tw * tw * uGlint * fade * (0.5 + 0.5 * under));
                }
            }
            // whitecaps on the crests, foam blown down the face of the waves
            if (uCaps > 0.01) {
                float hgt = (top - layerBase(k)) / A;
                float fn = vnoise(vec2(w.x * 3.0 - uWaveT * uDir * 1.5, w.y * 6.0 + float(k) * 3.0));
                float cap = smoothstep(0.3, 0.85, hgt) * smoothstep(0.08 + 0.25 * A, 0.0, depth) * smoothstep(0.35, 0.6, fn);
                float streak = smoothstep(0.66, 0.85, vnoise(vec2(w.x * 0.8 - uWaveT * uDir * 0.8, w.y * 5.0 + float(k) * 9.0))) * uStorm * 0.35 *
                               exp(-max(depth, 0.0) / 0.8);
                c = mix(c, uFoam, clamp((cap + streak) * uCaps, 0.0, 0.9) * flatAt(w.x));
            }
            // a fine bright line along the crest
            c = mix(c, mix(uHaze, uFoam, 0.4), smoothstep(uPx * 2.0, 0.0, abs(depth)) * 0.35);
            float a = smoothstep(-uPx * 1.2, uPx * 1.2, depth);
            col = mix(col, c, a);
        }
    }
    // the sand in front: dry and pale above the water, dark and wet under it, a darker line where the water meets it
    {
        float sT = sandTop(w.x);
        float sa = smoothstep(uPx * 1.2, -uPx * 1.2, w.y - sT);
        if (sa > 0.0) {
            float d = sT - w.y;
            vec3 sc = uSand * (1.0 - 0.28 * smoothstep(0.0, 2.5, d));
            // fine grain and long ripples
            sc *= 0.93 + 0.1 * hash12(floor(w / (uPx * 1.5))) + 0.05 * sin(w.x * 3.1 + 2.0 * sin(w.y * 2.3));
            sc = mix(sc, uSand * 1.15, smoothstep(uPx * 2.5, 0.0, d) * 0.6);
            float wTop = tops[2];
            float wet = smoothstep(-uPx, uPx, wTop - w.y);
            sc = mix(sc, mix(uSea2, uDeep, 0.4) * 0.9 + uSand * 0.12, wet * 0.7);
            sc = mix(sc, uDeep * 0.22, wet * darkAt(wTop - w.y) * 0.9);
            sc = mix(sc, uSand * 0.6, smoothstep(0.12, 0.0, abs(w.y - (wTop - 0.06))) * smoothstep(-0.05, 0.1, sT - wTop) * 0.5);
            sc = mix(sc, uDusk * 0.5, uSlow * 0.5);
            sc = mix(sc, uStormSea * 0.7, uStorm * 0.3);
            col = mix(col, sc, sa);
        }
    }
    // rain slanting across in a storm (never behind the blocks)
    if (uRain > 0.01) {
        float an = 0.35 * uDir;
        vec2 r = vec2(cos(an) * w.x - sin(an) * w.y, sin(an) * w.x + cos(an) * w.y);
        float colI = floor(r.x * 4.0);
        float off = hash12(vec2(colI, 3.0));
        float v = fract(r.y * 0.12 + uTime * (1.4 + off) + off * 7.0);
        float line = smoothstep(uPx * 1.2, 0.0, abs(fract(r.x * 4.0) - 0.5) / 4.0) * smoothstep(0.0, 0.02, v) * smoothstep(0.09, 0.03, v);
        line *= step(0.55, hash12(vec2(colI, floor(r.y * 0.12 + uTime * (1.4 + off) + off * 7.0))));
        float hudCol = abs(w.x) > 5.8 && abs(w.x) < 11.5 && w.y > -7.0 ? 0.45 : 1.0;
        col = mix(col, mix(uFoam, uSkyLow, 0.5), line * uRain * 0.16 * hudCol * (1.0 - inWell) * smoothstep(-0.05, 0.1, w.y - tops[2]));
    }
    // the well: a calm, subdued panel over the sea and the sky, with a fine edge
    float wellE = smoothstep(uPx * 1.5, 0.0, abs(wellD + uPx));
    col = mix(col, uPanel, inWell * panelK(uPanelA, w));
    col = mix(col, mix(uPanel, uHaze, 0.5), wellE * 0.35);
    col = mix(col, uBridgeCol, uBridge);
    frag = vec4(enc(col), 1.0);
}
)";

// ---------------------------------------------------------------- the boats
// One quad per boat, around it in its own frame: it sits on its layer's crest, rocks with the swell, and the water of
// its own layer and of the nearer ones hides what is below their surface.
inline const char* BOAT_VS = R"(#version 330 core
layout(location = 0) in vec2 aQ;
layout(location = 1) in vec4 a0;   // x, layer, scale, type
layout(location = 2) in vec4 a1;   // dir, phase, alpha, seed
uniform mat4 uVP;
uniform float uTime, uRock;
out vec2 vQ;
out vec2 vW;
flat out vec4 v0, v1;
const float BOAT_HALF[6] = float[6](0.5, 0.9, 1.1, 1.9, 3.3, 3.7);
void main() {
    int k = int(a0.y + 0.5), type = int(a0.w + 0.5);
    float s = a0.z;
    float len = BOAT_HALF[type] * s;
    float y0 = layerTop(k, a0.x);
    float span = max(0.25, len * 0.5);
    float slope = (layerTop(k, a0.x + span) - layerTop(k, a0.x - span)) / (2.0 * span);
    float rock = atan(slope) * 0.75 + uRock * sin(uTime * 0.85 + a1.y) / (1.0 + float(type) * 0.4);
    vec2 q = vec2(aQ.x * BOAT_HALF[type] * 1.25, mix(-0.5, 2.5, aQ.y * 0.5 + 0.5));
    vQ = vec2(q.x * a1.x, q.y);
    vec2 d = q * s;
    float c = cos(rock), sn = sin(rock);
    vW = vec2(a0.x, y0 - 0.04 * s) + vec2(c * d.x - sn * d.y, sn * d.x + c * d.y);
    v0 = a0; v1 = a1;
    gl_Position = uVP * vec4(vW, 0.0, 1.0);
}
)";
inline const char* BOAT_FS = R"(#version 330 core
in vec2 vQ;
in vec2 vW;
flat in vec4 v0, v1;
out vec4 frag;
uniform float uPx, uStorm, uSlow, uPanelA, uNight;
uniform vec3 uHaze, uDusk, uPanel, uHull, uSuper, uSail, uBAccent, uWindow, uCont0, uCont1, uCont2;
// ---- boats: flat silhouettes, painted back to front (premultiplied)
float sdBox(vec2 p, vec2 c, vec2 h) {
    vec2 d = abs(p - c) - h;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}
float sdSeg(vec2 p, vec2 a, vec2 b, float r) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}
float sdTri(vec2 p, vec2 p0, vec2 p1, vec2 p2) {
    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)), vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}
// a hull: the deck from xs (stern) to xb (bow) at heights ys, yb; the keel from ks to kb at y = kd
float sdHull(vec2 p, float xs, float ys, float xb, float yb, float ks, float kb, float kd) {
    vec2 a = vec2(xs, ys), b = vec2(xb, yb), c = vec2(kb, kd), d = vec2(ks, kd);
    return min(sdTri(p, a, b, c), sdTri(p, a, c, d));
}
float bpx;
void paint(inout vec4 acc, float d, vec3 col) {
    float a = clamp(0.5 - d / bpx, 0.0, 1.0);
    acc = acc * (1.0 - a) + vec4(col * a, a);
}
// windows: a row of small lit (or dark) panes along y, inside the box (c, h)
void windows(inout vec4 acc, vec2 q, vec2 c, vec2 h, float pitch, vec3 col) {
    if (abs(q.y - c.y) > h.y || abs(q.x - c.x) > h.x) return;
    float u = fract((q.x - c.x) / pitch);
    float d = max(abs(u - 0.5) * pitch - pitch * 0.22, abs(q.y - c.y) - h.y * 0.6);
    paint(acc, d, col);
}
vec4 boat(vec2 q, int type, float seed) {
    vec4 acc = vec4(0.0);
    vec3 win = mix(uWindow, uWindow * 1.4, uNight);
    if (type == 0) {
        // a dinghy: one small sail
        paint(acc, sdHull(q, -0.42, 0.1, 0.45, 0.12, -0.32, 0.3, -0.12), uHull);
        paint(acc, sdSeg(q, vec2(0.0, 0.1), vec2(0.0, 0.95), 0.018), uHull);
        paint(acc, sdTri(q, vec2(0.03, 0.17), vec2(0.03, 0.9), vec2(0.42, 0.18)), uSail);
    } else if (type == 1) {
        // a sailboat: a main sail and a jib, a low cabin
        paint(acc, sdHull(q, -0.78, 0.14, 0.88, 0.17, -0.6, 0.55, -0.13), uHull);
        paint(acc, sdBox(q, vec2(-0.12, 0.22), vec2(0.27, 0.07)), uSuper);
        paint(acc, sdSeg(q, vec2(0.05, 0.14), vec2(0.05, 1.75), 0.022), uHull);
        paint(acc, sdTri(q, vec2(0.0, 0.26), vec2(0.0, 1.68), vec2(-0.62, 0.28)), uSail);
        paint(acc, sdTri(q, vec2(0.1, 0.24), vec2(0.1, 1.55), vec2(0.78, 0.21)), uSail * 0.93);
    } else if (type == 2) {
        // a fishing boat: a high bow, a wheelhouse, a mast and its booms
        paint(acc, sdSeg(q, vec2(0.38, 0.4), vec2(0.38, 1.4), 0.025), uHull);
        paint(acc, sdSeg(q, vec2(0.38, 1.25), vec2(1.0, 0.52), 0.012), uHull);
        paint(acc, sdSeg(q, vec2(0.38, 1.05), vec2(-0.95, 0.5), 0.011), uHull);
        paint(acc, sdHull(q, -0.98, 0.3, 1.08, 0.46, -0.86, 0.78, -0.16), uBAccent);
        paint(acc, sdBox(q, vec2(0.05, 0.2), vec2(1.0, 0.035)), uHull);
        paint(acc, sdBox(q, vec2(-0.42, 0.52), vec2(0.27, 0.22)), uSuper);
        windows(acc, q, vec2(-0.42, 0.6), vec2(0.22, 0.06), 0.15, win);
    } else if (type == 3) {
        // a ferry: two white decks, a row of windows, one funnel
        paint(acc, sdHull(q, -1.72, 0.46, 1.88, 0.52, -1.6, 1.45, -0.16), uHull);
        paint(acc, sdBox(q, vec2(0.08, 0.32), vec2(1.7, 0.035)), uBAccent);
        paint(acc, sdBox(q, vec2(-0.05, 0.66), vec2(1.48, 0.18)), uSuper);
        paint(acc, sdBox(q, vec2(-0.25, 0.94), vec2(1.02, 0.12)), uSuper);
        paint(acc, sdBox(q, vec2(0.62, 1.13), vec2(0.36, 0.09)), uSuper);
        paint(acc, sdBox(q, vec2(-0.8, 1.2), vec2(0.13, 0.22)), uBAccent);
        paint(acc, sdBox(q, vec2(-0.8, 1.39), vec2(0.13, 0.04)), uHull);
        windows(acc, q, vec2(-0.05, 0.68), vec2(1.38, 0.06), 0.16, win);
        windows(acc, q, vec2(-0.25, 0.95), vec2(0.95, 0.05), 0.16, win);
        windows(acc, q, vec2(0.66, 1.13), vec2(0.28, 0.04), 0.1, win);
    } else if (type == 4) {
        // a container ship: stacks of boxes in a few colors, the bridge at the stern
        paint(acc, sdHull(q, -3.02, 0.56, 3.22, 0.62, -2.85, 2.82, -0.18), uHull);
        paint(acc, sdBox(q, vec2(0.0, 0.04), vec2(2.95, 0.07)), uBAccent);
        if (q.x > -1.95 && q.x < 2.65 && q.y > 0.5 && q.y < 1.4) {
            float ci = floor((q.x + 1.95) / 0.38);
            float rows = 1.0 + floor(hash12(vec2(ci, seed)) * 2.99);
            float u = q.x + 1.95 - ci * 0.38;
            float r = floor((q.y - 0.56) / 0.25);
            if (r >= 0.0 && r < rows) {
                float h = hash12(vec2(ci * 3.1 + r, seed + 7.0));
                vec3 cc = h < 0.4 ? uCont0 : (h < 0.75 ? uCont1 : uCont2);
                float v = q.y - 0.56 - r * 0.25;
                float d = max(abs(u - 0.19) - 0.17, abs(v - 0.125) - 0.11);
                paint(acc, d, cc * (0.9 + 0.2 * hash12(vec2(ci, r + 3.0))));
            }
        }
        paint(acc, sdBox(q, vec2(-2.42, 1.05), vec2(0.3, 0.5)), uSuper);
        paint(acc, sdBox(q, vec2(-2.42, 1.58), vec2(0.42, 0.05)), uSuper);
        paint(acc, sdBox(q, vec2(-2.78, 1.35), vec2(0.09, 0.22)), uBAccent);
        windows(acc, q, vec2(-2.42, 1.4), vec2(0.25, 0.05), 0.12, win);
    } else {
        // a liner: a dark hull, white decks stepped up, rows of windows, two funnels
        paint(acc, sdHull(q, -3.4, 0.72, 3.62, 0.82, -3.2, 3.1, -0.2), uHull);
        paint(acc, sdBox(q, vec2(0.05, 0.62), vec2(3.32, 0.1)), uSuper);
        paint(acc, sdBox(q, vec2(-0.05, 0.96), vec2(2.9, 0.18)), uSuper);
        paint(acc, sdBox(q, vec2(-0.15, 1.27), vec2(2.4, 0.14)), uSuper);
        paint(acc, sdBox(q, vec2(-0.25, 1.52), vec2(1.6, 0.11)), uSuper);
        paint(acc, sdBox(q, vec2(-0.65, 1.9), vec2(0.19, 0.3)), uBAccent);
        paint(acc, sdBox(q, vec2(0.5, 1.9), vec2(0.19, 0.3)), uBAccent);
        paint(acc, sdBox(q, vec2(-0.65, 2.16), vec2(0.19, 0.05)), uHull);
        paint(acc, sdBox(q, vec2(0.5, 2.16), vec2(0.19, 0.05)), uHull);
        windows(acc, q, vec2(0.05, 0.62), vec2(3.2, 0.05), 0.14, win);
        windows(acc, q, vec2(-0.05, 0.97), vec2(2.8, 0.07), 0.14, win);
        windows(acc, q, vec2(-0.15, 1.27), vec2(2.3, 0.05), 0.14, win);
    }
    return acc;
}

void main() {
    int k = int(v0.y + 0.5), type = int(v0.w + 0.5);
    bpx = uPx / v0.z;
    vec4 b = boat(vQ, type, v1.w);
    if (b.a <= 0.001) discard;
    // hidden by the water of its own layer and the nearer ones
    float a = b.a * v1.z;
    for (int j = 0; j < 3; j++)
        if (j >= k) a *= smoothstep(-uPx, uPx, vW.y - layerTop(j, vW.x));
    vec3 bc = b.rgb / b.a;
    // the far ones fade into the haze
    bc = mix(bc, uHaze, (2.0 - float(k)) * 0.2 + 0.15 * uStorm);
    bc = mix(bc, uDusk, uSlow * 0.5);
    // keep the HUD columns quiet; behind the well, under its panel
    if (abs(vW.x) > 6.0 && vW.y > -6.8) a *= 0.55;
    vec2 wd = abs(vW) - vec2(5.08, 10.08);
    float wellD = length(max(wd + 0.25, 0.0)) + min(max(wd.x + 0.25, wd.y + 0.25), 0.0) - 0.25;
    bc = mix(bc, uPanel, smoothstep(uPx, -uPx, wellD) * panelK(uPanelA, vW));
    frag = vec4(enc(bc), a);
}
)";

// ---------------------------------------------------------------- the glass cubes of water
// One instance per cell: every cell is a small solid glass cube full of water, seen in a slight three-quarter view (its
// front face on the cell, its top and the side turned to the well's middle showing: one camera above the well's
// middle, orthographic, so the stack stays a clean grid; sea.cpp draws them bottom row first, the outer ones first).
// Each pixel follows its ray through the cube: into the glass (a rounded box with thick walls, bulging a hair under
// the water's pressure), into the water, out of the water, out of the glass, each time refracted; what it then meets
// is the scene behind (a copy of the scene drawn so far), shifted by the whole path. The glass is thick and clear,
// a colorless band around the water on every face with crisp bright lines on its edges and around the water; the water carries the color, deeper the longer the path
// through it. The faces reflect the sky by Fresnel (the top face most), with a sharp highlight of the sun (or the
// moon); a ray caught by total internal reflection at the edges goes dark or mirrors the sky. Light focused through the
// water plays on the cube's bottom. A cube full is full to the brim; once cracked and past its pressure, its level
// drops. A cracked cube shows one crack (a few jagged lines from a point) where its water leaks out.
inline const char* CELL_VS = R"(#version 330 core
layout(location = 0) in vec2 aQ;
layout(location = 1) in vec4 a0;   // x, y, size, rotation
layout(location = 2) in vec4 a1;   // linear rgb (the water's color), alpha
layout(location = 3) in vec4 a2;   // ghost, crack, -, fill
layout(location = 4) in vec4 a3;   // seed, in the world (sand, sea), the crack's point (the front face's frame)
uniform mat4 uVP;
out vec2 vP;
out vec2 vW;
flat out vec4 v0, v1, v2, v3;
flat out vec3 vD;
void main() {
    vP = aQ * 0.75;                       // the quad covers the front face and the top and side behind it
    v0 = a0; v1 = a1; v2 = a2; v3 = a3;
    float c = cos(a0.w), s = sin(a0.w);
    vec2 d = vP * a0.z;
    vW = a0.xy + vec2(c * d.x - s * d.y, s * d.x + c * d.y);
    // the view: from above the well's middle, a little from above, more from the side the farther out
    vD = normalize(vec3(clamp(a0.x * 0.045, -0.3, 0.3), -0.27, -1.0));
    gl_Position = uVP * vec4(vW, 0.0, 1.0);
}
)";
inline const char* CELL_FS = R"(#version 330 core
in vec2 vP;
in vec2 vW;
flat in vec4 v0, v1, v2, v3;
flat in vec3 vD;
out vec4 frag;
uniform sampler2D uBg;          // the scene behind (encoded)
uniform vec2 uRes, uSunP;
uniform float uPx, uTime, uBridge, uLightPanel, uSlow, uNight;
uniform vec3 uBridgeInk, uSea2, uDeep, uDusk, uSkyTop, uSkyLow, uSunCol;
const float H = 0.44;    // the cube's half size (a gap of 0.12 between neighbours)
const float RO = 0.06;   // its rounded edges
const float WT = 0.11;   // the glass's thickness: a clear band of glass around the water on every face
const float RI = 0.02;   // the water's rounded corners
float bul;               // the bulge under pressure
float sdBox(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}
float bulge(vec3 p, float h) {
    vec3 a = clamp(abs(p) / h, 0.0, 1.0);
    vec3 c = 1.0 - a * a;
    return c.x * c.y + c.y * c.z + c.x * c.z;
}
float sdOut(vec3 p) { return sdBox(p, vec3(H), RO) - bul * bulge(p, H); }
float sdIn(vec3 p) { return sdBox(p, vec3(H - WT), RI) - bul * bulge(p, H - WT); }
vec3 nOut(vec3 p) {
    const vec2 k = vec2(1.0, -1.0) * 0.0015;
    return normalize(k.xyy * sdOut(p + k.xyy) + k.yyx * sdOut(p + k.yyx) + k.yxy * sdOut(p + k.yxy) + k.xxx * sdOut(p + k.xxx));
}
vec3 nIn(vec3 p) {
    const vec2 k = vec2(1.0, -1.0) * 0.0015;
    return normalize(k.xyy * sdIn(p + k.xyy) + k.yyx * sdIn(p + k.yyx) + k.yxy * sdIn(p + k.yxy) + k.xxx * sdIn(p + k.xxx));
}
// from inside a box, how far along d to its surface
float exitOut(vec3 p, vec3 d) {
    float t = 0.002;
    for (int i = 0; i < 40; i++) {
        float s = -sdOut(p + d * t);
        if (s < 0.0005) break;
        t += max(s, 0.001);
    }
    return t;
}
float exitIn(vec3 p, vec3 d) {
    float t = 0.002;
    for (int i = 0; i < 40; i++) {
        float s = -sdIn(p + d * t);
        if (s < 0.0005) break;
        t += max(s, 0.001);
    }
    return t;
}
// from outside the water (in the glass), how far along d to the water (or a miss: beyond tMax)
float minIn;   // how close a ray staying in the glass came to the water (the water's crisp outline)
float enterIn(vec3 p, vec3 d, float tMax) {
    float t = 0.002;
    minIn = 1e9;
    for (int i = 0; i < 40; i++) {
        float s = sdIn(p + d * t);
        minIn = min(minIn, s);
        if (s < 0.0005) return t;
        t += s;
        if (t > tMax) break;
    }
    return 1e9;
}
vec3 bgAt(vec2 offBoard) {
    vec2 uv = (gl_FragCoord.xy + offBoard / uPx) / uRes;
    return pow(texture(uBg, clamp(uv, vec2(0.001), vec2(0.999))).rgb, vec3(2.2));
}
float rotS, rotC;
vec2 toWorld(vec2 v) { return vec2(rotC * v.x - rotS * v.y, rotS * v.x + rotC * v.y); }
vec3 sunDir;   // local
// the sky (and the sea below the horizon) seen in a direction (local)
vec3 env(vec3 r) {
    vec3 w = vec3(toWorld(r.xy), r.z);
    vec3 sky = mix(uSkyLow, uSkyTop, smoothstep(0.0, 0.7, w.y));
    vec3 sea = mix(uSkyLow * 0.7, uSea2 * 0.5, smoothstep(0.0, -0.4, w.y));
    vec3 c = w.y > 0.0 ? sky : sea;
    return c + uSunCol * pow(max(dot(r, sunDir), 0.0), 900.0) * 6.0;
}
// one crack: a few jagged lines from its point, growing with `crk` (0..1), a small star of crushed glass at the point
float crackAt(vec2 p, vec2 o, float crk, float seed, float px) {
    vec2 q = p - o;
    float r = length(q);
    float ang = atan(q.y, q.x);
    const float n = 5.0;
    float k = floor(ang / 6.2832 * n + 0.5);
    float aj = (k + 0.4 * (hash12(vec2(k, seed)) - 0.5)) / n * 6.2832;
    float wob = 0.03 * (vnoise(vec2(r * 10.0, k * 3.7 + seed)) - 0.5) * 2.0;
    vec2 dir = vec2(cos(aj), sin(aj));
    float along = dot(q, dir);
    float off = abs(q.x * dir.y - q.y * dir.x - wob * smoothstep(0.0, 0.08, r));
    float len = crk * (0.12 + 0.22 * hash12(vec2(k, seed + 3.0)));
    float line = smoothstep(px * 1.4, 0.0, off) * step(0.0, along) * smoothstep(len, len - 0.03, along);
    float star = smoothstep(0.035 * crk + px, 0.0, r) * 0.8;
    return clamp(max(line, star), 0.0, 1.0);
}
vec3 sort3(vec3 a) {
    float lo = min(a.x, min(a.y, a.z)), hi = max(a.x, max(a.y, a.z));
    return vec3(lo, a.x + a.y + a.z - lo - hi, hi);
}
// one sample of the cube at sP (the cube's frame), wP its point in the world: linear color, coverage
vec4 shade(vec2 sP, vec2 wP) {
    float size = v0.z;
    float px = uPx / size;
    bool lightP = uLightPanel > 0.5;
    bool world = v3.y > 0.5;
    vec3 tint = v1.rgb;
    float seed = v3.x;
    float crk = v2.y;
    float fill = v2.w;
    rotC = cos(v0.w); rotS = sin(v0.w);
    // the view and the sun in the cube's frame
    vec3 D = normalize(vec3(rotC * vD.x + rotS * vD.y, -rotS * vD.x + rotC * vD.y, vD.z));
    vec2 sl = uSunP - v0.xy;
    vec3 Lw = normalize(vec3(sl, 9.0));
    sunDir = normalize(vec3(rotC * Lw.x + rotS * Lw.y, -rotS * Lw.x + rotC * Lw.y, Lw.z));
    // under pressure the walls bulge a hair; it goes once the crack has let enough out
    float pressure = crk > 0.001 ? clamp((fill - 0.5) / 0.5, 0.0, 1.0) : 1.0;
    bul = 0.012 * pressure;
    // ---- the ray: the pixel is where it crosses the front face's plane
    vec3 o = vec3(sP, H) - D * 0.8;
    float t = 0.0, md = 1e9;
    bool hit = false;
    for (int i = 0; i < 40; i++) {
        float s = sdOut(o + D * t);
        md = min(md, s);
        if (s < 0.0004) { hit = true; break; }
        t += s;
        if (t > 2.6) break;
    }
    float cover = hit ? 1.0 : smoothstep(px, 0.0, md);
    if (cover <= 0.0) return vec4(0.0);
    vec3 p1 = o + D * t;
    vec3 n1 = nOut(p1);
    if (v2.x > 0.5) {
        // the ghost: the cube's edges only, faint
        vec3 a = sort3(abs(p1));
        float edge = smoothstep(px * 2.5 + RO * 0.5, RO * 0.3, H - a.y);
        vec3 ec = lightP ? tint * 0.45 : mix(tint, vec3(1.0), 0.6);
        return vec4(mix(ec, uBridgeInk, uBridge), (edge * 0.6 + 0.05) * cover * v1.a);
    }
    // ---- in through the glass
    float cosI = clamp(dot(-D, n1), 0.0, 1.0);
    float F = 0.04 + 0.96 * pow(1.0 - cosI, 5.0);
    vec3 refl = reflect(D, n1);
    vec3 d = refract(D, n1, 1.0 / 1.5);
    float glassLen = 0.0, waterLen = 0.0;
    bool tir = false;
    vec3 p = p1;
    float tOut = exitOut(p, d);
    float tW = enterIn(p, d, tOut);
    vec3 nBottom = vec3(0.0);
    vec3 pW = vec3(0.0);
    if (tW < tOut) {
        glassLen += tW;
        p += d * tW;
        vec3 n2 = nIn(p);
        vec3 d2 = refract(d, n2, 1.5 / 1.33);
        if (dot(d2, d2) < 0.5) d2 = reflect(d, n2);
        // through the water (to its level, if it has one)
        float tE = exitIn(p, d2);
        vec3 p3 = p + d2 * tE;
        float wl = min(1.0, fill / 0.5);
        if (wl >= 0.999) waterLen = tE;
        else {
            vec3 up = vec3(rotS, rotC, 0.0);
            float hi = H - WT;
            float ey = (abs(up.x) + abs(up.y)) * hi;
            float lvl = -ey + 2.0 * ey * wl;
            float s2 = dot(p, up) - lvl, s3 = dot(p3, up) - lvl;
            float k = (s2 < 0.0 && s3 < 0.0) ? 1.0 : (s2 >= 0.0 && s3 >= 0.0) ? 0.0
                    : clamp((s2 < 0.0 ? -s2 : -s3) / max(abs(s2 - s3), 1e-4), 0.0, 1.0);
            waterLen = tE * (wl > 0.001 ? k : 0.0);
        }
        vec3 n3 = nIn(p3);
        nBottom = n3; pW = p3;
        d = refract(d2, -n3, 1.33 / 1.5);
        if (dot(d, d) < 0.5) d = reflect(d2, -n3);
        p = p3;
        tOut = exitOut(p, d);
    }
    glassLen += tOut;
    p += d * tOut;
    vec3 n4 = nOut(p);
    vec3 d4 = refract(d, -n4, 1.5);
    if (dot(d4, d4) < 0.5) { tir = true; d4 = reflect(d, -n4); }
    // ---- what it meets behind: the scene, shifted by the whole path
    const float ZB = 2.2;
    vec3 dd = d4;
    if (tir) dd.z = -abs(dd.z);
    dd.z = min(dd.z, -0.25);
    dd = normalize(dd);
    vec3 q = p + dd * ((-ZB - p.z) / dd.z);
    vec3 q0 = o + D * ((-ZB - o.z) / D.z);
    vec2 off = q.xy - q0.xy;
    off *= min(1.0, 1.1 / max(length(off), 1e-4));
    vec3 behind = bgAt(toWorld(off) * size);
    // caught inside at an edge: it mirrors the cube's own lit water and the sky (bright edges, not dark)
    if (tir) behind = behind * 0.3 + env(d4) * 0.3 + tint * (lightP ? 0.12 : 0.55);
    // ---- the colors along the way: green-blue glass where it is thick, the water's own color the deeper through it
    vec3 tN = clamp(tint / 0.75, vec3(0.02), vec3(1.0));   // a darker water absorbs more
    vec3 Tg = exp(-glassLen * vec3(0.14, 0.05, 0.08));   // clear, colorless glass (barely green at its longest)
    vec3 Tw = pow(tN, vec3(waterLen * 0.85));
    vec3 col = behind * Tg * Tw;
    // the water glows a little (light scattered in it; more on a dark scene, where it carries the color)
    // the water glows a little (light scattered in it); on a dark scene it is luminous, well above the night behind it
    if (lightP) col += tint * waterLen * 0.02;
    else col = col * 0.7 + tint * (0.22 + 0.3 * waterLen) * (0.8 + 0.4 * uNight) * smoothstep(0.0, 0.04, waterLen);
    // light focused through the water onto the cube's bottom: a soft moving caustic
    if (nBottom.y < -0.7 && waterLen > 0.0) {
        vec2 cq = pW.xz * 3.2;
        float caus = vnoise(cq + vec2(uTime * 0.3, seed)) * vnoise(cq * 1.3 - vec2(uTime * 0.25, seed * 0.7) + 5.0);
        col += mix(tint, vec3(1.0), 0.6) * smoothstep(0.15, 0.45, caus) * (lightP ? 0.3 : 0.3) * Tw * Tg;
    }
    // ---- the surface: the sky reflected (Fresnel), the sun's sharp highlight
    vec3 rc = env(refl);
    // on a dark scene the glass still catches some light at its edges (the harbor's lights, the moon's sky)
    if (!lightP) rc += mix(uSkyLow, vec3(1.0), 0.6) * 0.6;
    col = mix(col, rc, F);
    float spec = pow(max(dot(refl, sunDir), 0.0), 220.0);
    col += uSunCol * spec * (lightP ? 1.6 : 1.1);
    // ---- crisp bright lines: the glass's outer edges, and the water's outline seen through the clear glass
    {
        vec3 a = sort3(abs(p1));
        float edge = smoothstep(max(RO * 0.8, RO * 0.35 + px * 1.6), RO * 0.35, H - a.y);
        float inner = waterLen > 0.0 ? 0.0 : smoothstep(px * 1.6, 0.0, minIn);
        vec3 lc = lightP ? vec3(1.0) : mix(uSkyLow, vec3(1.0), 0.7);
        col = mix(col, lc, clamp(edge * (lightP ? 0.55 : 0.6) + inner * (lightP ? 0.6 : 0.7), 0.0, 1.0));
    }
    // ---- the crack, where the water leaks out (on the front face)
    if (crk > 0.001 && n1.z > 0.5) {
        float c = crackAt(p1.xy, v3.zw, crk, seed, px);
        float cd = crackAt(p1.xy + vec2(px * 1.4, -px * 1.4), v3.zw, crk, seed, px);
        col = mix(col, col * 0.4, cd * 0.55 * (1.0 - c));
        col = mix(col, vec3(1.0), c * 0.95);
    }
    // ---- out in the world: under the sea, tinted and darkened by its water; the sand hides what is under it
    float cut = 1.0;
    if (world) {
        float depth = layerTop(2, wP.x) - wP.y;
        if (depth > 0.0) {
            col = mix(col, uSea2 * 0.7, 0.3 * smoothstep(0.0, 0.4, depth));
            col = mix(col, uDeep * 0.5, darkAt(depth) * 0.45);   // capped: deep cubes never go near-black
        }
        col = mix(col, uDusk * 0.6, uSlow * 0.3);
        cut = smoothstep(-uPx, uPx, wP.y - sandTop(wP.x) + 0.05);
    }
    col = mix(col, uBridgeInk, uBridge);
    return vec4(col, cover * v1.a * cut);
}
// four samples on a rotated grid: smooth silhouettes, face boundaries, the water's outline and the edge lines
void main() {
    float pxl = uPx / v0.z;
    float c = cos(v0.w), s = sin(v0.w);
    const vec2 G[4] = vec2[4](vec2(-0.125, -0.375), vec2(0.375, -0.125), vec2(0.125, 0.375), vec2(-0.375, 0.125));
    vec3 acc = vec3(0.0);
    float a = 0.0;
    for (int i = 0; i < 4; i++) {
        vec2 o = G[i] * pxl;
        vec2 d = o * v0.z;
        vec4 r = shade(vP + o, vW + vec2(c * d.x - s * d.y, s * d.x + c * d.y));
        acc += r.rgb * r.a;
        a += r.a;
    }
    if (a <= 0.0) discard;
    frag = vec4(enc(acc / a), a * 0.25);
}
)";

// ---------------------------------------------------------------- life in the sea
// One quad per critter (sea.cpp's drawLife), cut at the sea's surface and veiled by the well's panel like the sea
// behind it. Flat cut-paper shapes: a small fish (its tail beating), a jellyfish (a pulsing translucent bell, trailing
// tentacles, a soft glow), an anglerfish (a near-black shape, a lit lure), a siphonophore (a chain of lit beads).
inline const char* LIFE_VS = R"(#version 330 core
layout(location = 0) in vec2 aQ;
layout(location = 1) in vec4 a0;   // x, y, half extents
layout(location = 2) in vec4 a1;   // direction, kind, animation, glow
layout(location = 3) in vec4 a2;   // linear rgb, alpha
layout(location = 4) in vec4 a3;   // glow rgb, seed
uniform mat4 uVP;
out vec2 vQ;
out vec2 vW;
flat out vec4 v0, v1, v2, v3;
void main() {
    vQ = aQ;
    vW = a0.xy + aQ * a0.zw;
    v0 = a0; v1 = a1; v2 = a2; v3 = a3;
    gl_Position = uVP * vec4(vW, 0.0, 1.0);
}
)";
inline const char* LIFE_FS = R"(#version 330 core
in vec2 vQ;
in vec2 vW;
flat in vec4 v0, v1, v2, v3;
out vec4 frag;
uniform float uPx, uPanelA, uSlow;
uniform vec3 uPanel, uDusk;
float sdEll(vec2 p, vec2 r) {
    float k = length(p / r);
    return (k - 1.0) * min(r.x, r.y);
}
float sdSeg(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0));
}
void main() {
    int kind = int(v1.y + 0.5);
    float t = v1.z, glowK = v1.w;
    vec2 ext = v0.zw;
    // the critter's own frame, in board units, facing +x
    vec2 q = vQ * ext;
    q.x *= v1.x;
    float px = uPx;
    vec3 col = v2.rgb;
    float a = 0.0;
    vec3 glow = v3.rgb;
    float g = 0.0;   // additive glow
    if (kind == 0) {
        // a small fish: an almond body, a forked tail beating
        float L = ext.x / 1.25;
        vec2 b = q / L;
        float wag = sin(t) * 0.18 * smoothstep(0.1, -0.9, b.x);
        b.y -= wag;
        float body = sdEll(b - vec2(0.15, 0.0), vec2(0.62, 0.27));
        vec2 tq = b - vec2(-0.55, 0.0);
        float tail = max(max(-tq.x - 0.42, tq.x - 0.08 + abs(tq.y) * 0.3), abs(tq.y) - (0.05 - tq.x * 0.7));
        float fork = -(abs(tq.y) - (-tq.x - 0.24) * 0.5);
        tail = max(tail, -max(fork, -tq.x - 0.42));
        float dd = min(body, tail) * L;
        a = smoothstep(px, -px, dd);
        // an eye, and a slightly darker back
        col *= 1.0 - 0.12 * smoothstep(-0.05, 0.15, b.y);
        float eye = smoothstep(px * 0.9, -px * 0.9, length(b - vec2(0.52, 0.05)) * L - 0.025);
        col = mix(col, col * 0.35, eye);
    } else if (kind == 1) {
        // a jellyfish: a bell contracting and opening slowly, tentacles trailing under it
        float R = ext.x / 1.2;
        vec2 b = (q - vec2(0.0, ext.y - R * 1.0)) / R;   // the bell's center near the top of the quad
        float c = pow(0.5 + 0.5 * sin(t * 3.0), 3.0);    // the contraction
        vec2 br = vec2(0.85 - 0.18 * c, 0.62 + 0.12 * c);
        float bell = max(sdEll(b, br), -b.y - 0.05 * (1.0 - c));
        float bellA = smoothstep(px / R, -px / R, bell);
        float rimL = smoothstep(2.5 * px / R, 0.0, abs(bell));
        float inside = smoothstep(0.0, -0.4, bell);
        // tentacles: a few wavy threads and two frilled oral arms
        float ten = 1.0;
        float tl = 0.0;
        for (int i = 0; i < 5; i++) {
            float fi = float(i);
            float x0 = (fi - 2.0) / 2.0 * br.x * 0.8;
            float yy = -b.y;
            float len = 2.4 + 0.6 * fract(fi * 0.37 + v3.w);
            if (yy > 0.0 && yy < len) {
                float wx = x0 * (1.0 - 0.15 * yy) + 0.12 * sin(yy * 2.6 - t * 2.0 + fi * 1.7) * yy * 0.6;
                float w = (i == 1 || i == 3) ? 0.06 : 0.025;
                float dd = abs(b.x - wx) - w * (1.0 - yy / len);
                ten = min(ten, dd);
                tl = max(tl, smoothstep(px * 1.2 / R, 0.0, dd) * (1.0 - yy / len));
            }
        }
        a = max(bellA * (0.45 + 0.35 * (1.0 - inside)), rimL * 0.85);
        a = max(a, tl * 0.6);
        col = mix(col, vec3(1.0), rimL * 0.4);
        // the glow around it
        float gd = length(b * vec2(1.0, 1.2));
        g = exp(-gd * 2.2) * 0.35 + bellA * 0.25;
        g *= glowK;
    } else if (kind == 2) {
        // an anglerfish: a round dark body, a wide jaw with pale teeth, a stalk bent forward, a lit lure
        float S = ext.y / 1.25;
        vec2 b = q / S;
        float body = sdEll(b - vec2(-0.05, -0.05), vec2(0.62, 0.5));
        // the tail
        vec2 tq = b - vec2(-0.75, 0.0);
        float tail = max(-tq.x - 0.35, max(tq.x - 0.1, abs(tq.y) - (0.08 - tq.x * 0.8)));
        // the open mouth: a wedge cut from the front
        vec2 mq = b - vec2(0.62, -0.12);
        float mouth = max(-mq.x - 0.45, abs(mq.y + mq.x * 0.1) - (0.02 + (mq.x + 0.45) * 0.35));
        float dd = max(min(body, tail), -mouth) * S;
        a = smoothstep(px, -px, dd);
        // teeth along the jaws
        float teeth = 0.0;
        if (mouth < 0.06 && mq.x > -0.4 && mq.x < 0.05) {
            float u = fract((mq.x + 0.4) / 0.09);
            teeth = smoothstep(0.03, 0.0, abs(abs(mq.y + mq.x * 0.1) - (0.02 + (mq.x + 0.45) * 0.35) + (0.5 - abs(u - 0.5)) * 0.1 - 0.02));
        }
        col = mix(col, mix(glow, vec3(1.0), 0.5) * 0.35, teeth * a);
        // the stalk, bending forward, the lure bobbing at its end
        vec2 lure = vec2(0.82 + 0.05 * sin(t * 0.9), 0.62 + 0.06 * sin(t * 1.3));
        vec2 mid = vec2(0.45, 0.85);
        float stalk = min(sdSeg(b, vec2(0.15, 0.42), mid), sdSeg(b, mid, lure)) * S - 0.008;
        float sa = smoothstep(px, -px, stalk);
        a = max(a, sa);
        // the body catches a little of the lure's light
        float lit = exp(-length(b - lure) * 2.5) * 0.6;
        col += glow * lit * 0.12 * a;
        float ld = length(b - lure) * S;
        float lo = smoothstep(0.05, 0.02, ld);
        col = mix(col, mix(glow, vec3(1.0), 0.5), lo);
        a = max(a, lo);
        g = (exp(-ld * 9.0) * 0.6 + exp(-ld * 2.5) * 0.15) * glowK * (0.85 + 0.15 * sin(t * 0.7));
    } else {
        // a siphonophore: a long chain of small beads on a thread, curving slowly
        float L = ext.x - 0.3;
        float x = clamp(q.x, -L, L);
        float yc = 0.22 * sin(x * 1.3 + t) + 0.1 * sin(x * 2.9 - t * 0.7);
        float thread = abs(q.y - yc);
        float on = step(abs(q.x), L);
        float pitch = 0.16;
        float k = floor(q.x / pitch + 0.5);
        float bx = k * pitch;
        float by = 0.22 * sin(bx * 1.3 + t) + 0.1 * sin(bx * 2.9 - t * 0.7);
        float taper = 1.0 - 0.6 * abs(bx) / L;
        float bead = length(q - vec2(bx, by)) - 0.045 * taper * (0.8 + 0.4 * hash12(vec2(k, v3.w)));
        float beadA = smoothstep(px, -px, bead) * step(abs(bx), L);
        float th = smoothstep(px, 0.0, thread - 0.004) * on;
        a = max(beadA * 0.85, th * 0.35);
        col = mix(col, vec3(1.0), beadA * 0.3);
        g = (exp(-max(bead, 0.0) * 30.0) * 0.4 * step(abs(bx), L) + exp(-thread * 9.0) * 0.08 * on) * glowK;
    }
    // under the water only; behind the well, veiled by its panel as the sea is
    float cut = smoothstep(px, -px, vW.y - layerTop(2, vW.x));
    vec2 wd = abs(vW) - vec2(5.08, 10.08);
    float wellD = length(max(wd + 0.25, 0.0)) + min(max(wd.x + 0.25, wd.y + 0.25), 0.0) - 0.25;
    float veil = smoothstep(uPx, -uPx, wellD) * panelK(uPanelA, vW);
    col = mix(col, uDusk * 0.6, uSlow * 0.4);
    // the glow fades out before the quad's edges; behind the well, the life is quieter still
    g *= smoothstep(1.0, 0.6, max(abs(vQ.x), abs(vQ.y)));
    veil = mix(veil, 1.0, smoothstep(uPx, -uPx, wellD) * 0.4);
    cut *= 1.0 - 0.7 * smoothstep(uPx, -uPx, wellD);
    // premultiplied (blended ONE, ONE_MINUS_SRC_ALPHA): the shape over the water, its glow added on top
    float k = cut * v2.a;
    vec3 outC = enc(mix(col, uPanel, veil)) * a + enc(glow) * g * (1.0 - veil * 0.8);
    frag = vec4(outC * k, a * k);
}
)";

// ---------------------------------------------------------------- sprites
// Kinds: 0 a soft droplet (stretched along its rotation), 1 a shard of glass, 2 the bonus gauge (a round glass
// filling with water), 3 a bubble.
inline const char* SPRITE_VS = R"(#version 330 core
layout(location = 0) in vec2 aQ;
layout(location = 1) in vec4 a0;   // x, y, half x, half y
layout(location = 2) in vec4 a1;   // rotation, kind, p0, p1
layout(location = 3) in vec4 a2;   // linear rgb, alpha
layout(location = 4) in vec4 a3;   // p2, p3, seed, -
uniform mat4 uVP;
out vec2 vQ;
out vec2 vW;
flat out vec4 v1, v2, v3, v0;
void main() {
    vec2 local = aQ * a0.zw;
    float c = cos(a1.x), s = sin(a1.x);
    vec2 wp = a0.xy + vec2(c * local.x - s * local.y, s * local.x + c * local.y);
    vQ = aQ;
    vW = wp;
    v0 = a0; v1 = a1; v2 = a2; v3 = a3;
    gl_Position = uVP * vec4(wp, 0.0, 1.0);
}
)";
inline const char* SPRITE_FS = R"(#version 330 core
in vec2 vQ;
in vec2 vW;
flat in vec4 v1, v2, v3, v0;
out vec4 frag;
uniform float uTime, uPx, uLightPanel;
uniform vec4 uWell;   // the well behind its pane: half width, bottom, top, how much the pane hides (0: not masked)
float sdTri(vec2 p, vec2 p0, vec2 p1, vec2 p2) {
    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)), vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}
void main() {
    int kind = int(v1.y + 0.5);
    vec3 col = v2.rgb;
    float a = 0.0;
    if (kind == 0) {
        a = smoothstep(1.0, 1.0 - max(v1.z, 0.05), length(vQ));
    } else if (kind == 1) {
        // a sharp sliver: an irregular triangle, a bright edge
        float s = v3.z;
        vec2 p0 = vec2(-1.0, -0.6 + 0.5 * fract(s * 7.3)), p1 = vec2(1.0, -0.2 + 0.4 * fract(s * 3.1)), p2 = vec2(-0.3 + 0.8 * fract(s * 5.7), 1.0);
        float d = sdTri(vQ, p0, p1, p2);
        float pxl = uPx / max(min(v0.z, v0.w), 1e-3);
        a = smoothstep(pxl, -pxl, d);
        col = mix(col, vec3(1.0), smoothstep(-pxl * 3.0, 0.0, d) * 0.6);
    } else if (kind == 3) {
        // a bubble: a thin bright ring, a speck of light at its upper left
        float pxl = uPx / max(v0.z, 1e-3);
        float r = length(vQ);
        a = smoothstep(pxl * 1.5, 0.0, abs(r - 0.82)) * 0.9 + smoothstep(1.0, 0.0, r) * 0.15;
        a = max(a, smoothstep(0.3, 0.0, length(vQ - vec2(-0.35, 0.35))));
    } else {
        // the bonus gauge: a round glass, water rising inside it; ready: it brims over with light
        float R = 0.92;
        float pxl = uPx / v0.z;
        float r = length(vQ);
        float fill = v1.z, ready = v1.w;
        float rim = smoothstep(pxl * 1.5, 0.0, abs(r - R) - 0.025);
        float lvl = -R + 2.0 * R * fill + 0.03 * sin(uTime * 1.7 + vQ.x * 4.0) + 0.015 * sin(uTime * 2.9 - vQ.x * 7.0);
        float inside = smoothstep(R - 0.05 + pxl, R - 0.05 - pxl, r);
        float water = smoothstep(pxl, -pxl, vQ.y - lvl) * inside;
        vec3 wc = col * (1.15 - 0.35 * smoothstep(lvl, -R, vQ.y));
        wc = mix(wc, mix(col, vec3(1.0), 0.6), smoothstep(-0.12, 0.0, vQ.y - lvl) * 0.6 + ready * 0.25 * (0.5 + 0.5 * sin(uTime * 2.0)));
        vec3 rc = uLightPanel > 0.5 ? col * 0.55 : mix(col, vec3(1.0), 0.6);
        vec3 c = mix(mix(col, vec3(1.0), 0.6), wc, water);
        float aa = inside * 0.12;
        aa = mix(aa, 0.9, water);
        float men = smoothstep(pxl * 2.0, 0.0, abs(vQ.y - lvl)) * inside;
        c = mix(c, vec3(1.0), men * 0.7);
        aa = max(aa, men * 0.9);
        c = mix(c, rc, rim);
        aa = max(aa, rim * 0.85);
        col = c;
        a = aa;
    }
    // behind the well: its pane veils what passes behind it (the stack is drawn over it later)
    float inW = smoothstep(0.06, -0.06, abs(vW.x) - uWell.x) * smoothstep(-0.06, 0.06, vW.y - uWell.y) * smoothstep(0.06, -0.06, vW.y - uWell.z);
    frag = vec4(enc(col), a * v2.a * (1.0 - uWell.w * inW));
}
)";

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
    vec2 q = vUv - 0.5;
    c *= 1.0 - 0.1 * dot(q, q) * 2.0;
    c *= 1.0 - uDim * 0.5;
    c *= uFade;
    frag = vec4(c, 1.0);
}
)";

} // namespace sea_shaders
