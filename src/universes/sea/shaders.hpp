#pragma once
// SEA's shaders (see sea.cpp): the sky and the layered sea with its boats, the glass bottles of water, the sprites
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

// ---------------------------------------------------------------- the glass bottles of water
// One instance per cell: every cell is a small square bottle of colored glass (a cube-shaped body, a short neck, a
// wooden stopper), with a gap to its neighbours. The glass is transparent: it shows what is behind it (a copy of the
// scene drawn so far, bent a little: more at the thick edges, more through the water), filtered by the glass's color,
// darker where it is thicker (its edges, its heavy bottom), with bright rims and a crisp specular. The water inside is
// clear, almost colorless, its surface level in the world (a bright meniscus), whatever the bottle's angle. Modes: 0 a
// stoppered bottle, 1 an open one, 2 the stopper alone. A bottle about to burst (game over) cracks.
inline const char* CELL_VS = R"(#version 330 core
layout(location = 0) in vec2 aQ;
layout(location = 1) in vec4 a0;   // x, y, size, rotation
layout(location = 2) in vec4 a1;   // linear rgb (the glass), alpha
layout(location = 3) in vec4 a2;   // ghost, crack, mode, fill
layout(location = 4) in vec4 a3;   // seed, in the world (sand, sea), -, -
uniform mat4 uVP;
out vec2 vP;
out vec2 vW;
flat out vec4 v0, v1, v2, v3;
void main() {
    vP = aQ * 0.5;
    v0 = a0; v1 = a1; v2 = a2; v3 = a3;
    float c = cos(a0.w), s = sin(a0.w);
    vec2 d = vP * a0.z;
    vW = a0.xy + vec2(c * d.x - s * d.y, s * d.x + c * d.y);
    gl_Position = uVP * vec4(vW, 0.0, 1.0);
}
)";
inline const char* CELL_FS = R"(#version 330 core
in vec2 vP;
in vec2 vW;
flat in vec4 v0, v1, v2, v3;
out vec4 frag;
uniform sampler2D uBg;          // the scene behind (encoded)
uniform vec2 uRes;
uniform float uPx, uTime, uBridge, uLightPanel, uSlow;
uniform vec3 uBridgeInk, uSea2, uDeep, uDusk, uWood;
// a cube of glass with a cork in a short mouth on top
const vec2 BODY_C = vec2(0.0, -0.07), BODY_H = vec2(0.43, 0.36);
const float BODY_R = 0.06;
const vec2 NECK_C = vec2(0.0, 0.315), NECK_H = vec2(0.08, 0.035);
const vec2 CAP_C = vec2(0.0, 0.4), CAP_H = vec2(0.105, 0.065);
const float WT = 0.05;   // the glass's thickness
float rbox(vec2 p, vec2 h, float r) {
    vec2 q = abs(p) - h + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}
float glassSd(vec2 p) {
    float b = rbox(p - BODY_C, BODY_H, BODY_R);
    float n = rbox(p - NECK_C, NECK_H, 0.02);
    // a small lip at the top of the neck
    float lip = rbox(p - vec2(0.0, NECK_C.y + NECK_H.y - 0.018), vec2(NECK_H.x + 0.016, 0.02), 0.012);
    return min(smin(b, n, 0.04), lip);
}
// the stopper: a wooden plug, a little narrower where it goes into the neck
float capSd(vec2 p) {
    vec2 q = p - CAP_C;
    float taper = 1.0 - 0.18 * smoothstep(-0.01, -0.07, q.y);
    return rbox(vec2(q.x / taper, q.y), CAP_H, 0.03);
}
vec3 wood(vec2 p, float seed, float px) {
    vec2 q = p - CAP_C;
    // grain running along the stopper, wavy, with a few darker lines
    float g = q.x * 34.0 + 2.2 * vnoise(vec2(q.x * 9.0 + seed, q.y * 7.0)) + seed;
    float grain = 0.5 + 0.5 * sin(g * 3.14159);
    vec3 c = uWood * (0.82 + 0.22 * grain);
    c *= 1.0 - 0.3 * smoothstep(0.82, 0.98, grain);
    // lit from the upper left: lighter left and top (its end grain), darker right and underneath
    c *= 1.0 + 0.25 * smoothstep(0.05, -0.1, q.x) - 0.3 * smoothstep(0.0, 0.12, q.x);
    c = mix(c, uWood * 1.45, smoothstep(CAP_H.y - 0.035, CAP_H.y - 0.005, q.y) * 0.7);
    c *= 1.0 - 0.35 * smoothstep(-0.03, -0.08, q.y);
    // a dark outline
    float d = capSd(p);
    c = mix(c, uWood * 0.3, smoothstep(px * 2.2, 0.0, abs(d)) * 0.8);
    return c;
}
vec3 bgAt(vec2 offBoard) {
    vec2 uv = (gl_FragCoord.xy + offBoard / uPx) / uRes;
    return pow(texture(uBg, clamp(uv, vec2(0.001), vec2(0.999))).rgb, vec3(2.2));
}
void main() {
    float size = v0.z;
    float px = uPx / size;
    vec2 p = vP;
    int mode = int(v2.z + 0.5);
    if (mode == 2) p += CAP_C;   // the stopper alone, centered on its instance
    float dg = mode == 2 ? 1e3 : glassSd(p);
    float dc = mode == 1 ? 1e3 : capSd(p);
    // the edge's normal on screen (for the bending of the light)
    vec2 nS = vec2(dFdx(dg), dFdy(dg));
    nS = nS / max(length(nS), 1e-6);
    float dAll = min(dg, dc);
    float cover = smoothstep(px, -px, dAll);
    if (cover <= 0.0) discard;
    bool lightP = uLightPanel > 0.5;
    bool world = v3.y > 0.5;
    vec3 tint = v1.rgb;
    float seed = v3.x;
    if (v2.x > 0.5) {
        // the ghost: the bottle's outline only, faint
        float o = smoothstep(px * 1.6, 0.0, abs(dAll));
        vec3 ec = lightP ? tint * 0.4 : mix(tint, vec3(1.0), 0.6);
        frag = vec4(enc(mix(ec, uBridgeInk, uBridge)), (o * 0.6 + 0.04) * cover * v1.a);
        return;
    }
    float rot = v0.w;
    float cr = cos(rot), sr = sin(rot);
    vec3 col = vec3(0.0);
    float glassA = smoothstep(px, -px, dg);
    if (glassA > 0.0) {
        float e = -dg;                                  // depth inside the glass
        // the cavity: the glass's inside, with a heavy bottom
        float bottomY = BODY_C.y - BODY_H.y;
        float di = max(dg + WT, (bottomY + WT + 0.055) - p.y);
        float cav = smoothstep(px, -px, di);
        float wallK = 1.0 - cav;
        // ---- the water: its surface level in the world, whatever the bottle's angle
        vec2 rp = p - BODY_C;
        vec2 wp = vec2(cr * rp.x - sr * rp.y, sr * rp.x + cr * rp.y);
        vec2 hi = BODY_H - WT;
        float ey = abs(cr) * hi.y + abs(sr) * hi.x;
        float fill = v2.w;
        float level = -ey + 2.0 * ey * fill + 0.012 * sin(uTime * 2.1 + seed) * fill * (1.0 - fill) * 4.0;
        // the meniscus climbs the walls a little
        float lvl = level + 0.03 * exp(max(di, -0.2) / 0.02 * 1.0);
        float wet = fill > 0.001 ? smoothstep(px, -px, wp.y - lvl) * cav : 0.0;
        // ---- what is seen through it, bent: at the thick edges and, more, through the water (a lens)
        vec2 wpB = wp * size;
        vec2 offWall = -nS * 0.09 * size * smoothstep(WT + 0.05, 0.0, e);
        vec2 offWater = -vec2(wpB.x * 0.55, wpB.y * 0.2);
        vec2 offAir = -wpB * 0.08;
        vec3 bgW = bgAt(offWall + offWater);
        vec3 bgA = bgAt(offWall + offAir);
        // the glass's color filters the light; thicker glass, more color and less light
        // (the faces, crossed straight through, are lightly colored; the color gathers at the edges, see below)
        vec3 T = mix(vec3(1.0), tint, lightP ? 0.68 : 0.3);
        vec3 glow = tint * (lightP ? 0.02 : 0.05);
        vec3 inAir = bgA * T * (lightP ? 1.02 : 0.95) + glow + (lightP ? vec3(0.0) : mix(tint, vec3(1.0), 0.5) * 0.05);
        vec3 inWater = bgW * T * vec3(0.84, 0.91, 0.95) * (0.97 - 0.14 * smoothstep(level, -ey, wp.y)) + glow * 1.1 + (lightP ? vec3(0.0) : vec3(0.02, 0.035, 0.05));
        // light gathered by the water, low in it
        inWater += mix(tint, vec3(1.0), 0.6) * 0.08 * smoothstep(-ey + 0.25, -ey + 0.02, wp.y) * (lightP ? 0.6 : 1.0);
        col = mix(inAir, inWater, wet);
        // darker toward the walls (more glass to cross at a slant)
        col *= 0.8 + 0.2 * smoothstep(0.0, 0.1, -di);
        // the meniscus: a bright line on the water, a slightly darker band just under it
        if (fill > 0.001 && fill < 0.999) {
            float dl = wp.y - lvl;
            float line = smoothstep(px * 1.6, 0.0, abs(dl)) * cav;
            float band = smoothstep(0.0, -0.012, dl) * smoothstep(-0.05, -0.02, dl) * cav;
            col *= 1.0 - 0.3 * band;
            col = mix(col, mix(vec3(1.0), tint, 0.15), line * 0.85);
        }
        // ---- the walls: thick glass, darker and deeper in color, with bright rims
        vec3 thick = bgAt(offWall * 1.5) * tint * tint * (lightP ? 0.75 : 0.6) + tint * (lightP ? 0.06 : 0.38);
        // the glass's color gathering toward its edges, seen at a slant through the side walls
        col = mix(col, thick, 0.55 * smoothstep(0.11, 0.0, -di) * cav);
        float bottomK = smoothstep(bottomY + WT + 0.06, bottomY + 0.01, p.y);
        thick *= 1.0 - 0.25 * bottomK;
        col = mix(col, thick, wallK);
        // the light from the upper left: rims bright on that side, dimmer on the other
        vec2 L = normalize(vec2(-0.6, 0.8));
        vec2 nL = vec2(cr * nS.x + sr * nS.y, -sr * nS.x + cr * nS.y); // screen normal back to the bottle's frame
        float facing = 0.35 + 0.65 * smoothstep(-0.4, 0.8, dot(nL, L));
        float outer = smoothstep(px * 1.8, 0.0, abs(e - px * 0.6));
        float innerL = smoothstep(px * 1.4, 0.0, abs(di)) * step(p.y, CAP_C.y - 0.1);
        vec3 rimC = mix(tint, vec3(1.0), lightP ? 0.55 : 0.7);
        col = mix(col, rimC, outer * facing * 0.9);
        col = mix(col, mix(tint, vec3(1.0), 0.75), innerL * 0.2 * facing);
        // the thick bottom's top edge catches the light
        col = mix(col, mix(tint, vec3(1.0), 0.7),
                  smoothstep(px * 1.6, 0.0, abs(p.y - (bottomY + WT + 0.055))) * step(abs(p.x), BODY_H.x - WT - 0.02) * 0.45);
        // ---- the specular: a long crisp highlight down the left of the body, a spot at its top, one on the
        // shoulder, a faint one at the right
        float sxx = (p.x + 0.245) / 0.022;
        float sx = (exp(-sxx * sxx) * 0.85 + exp(-sxx * sxx * 0.08) * 0.12) * smoothstep(bottomY + 0.07, bottomY + 0.2, p.y) * smoothstep(0.13, 0.03, p.y);
        float spot = smoothstep(0.04, 0.015, length((p - vec2(-0.245, 0.06)) * vec2(1.0, 0.75)));
        float sh = smoothstep(0.03, 0.012, length((p - vec2(-0.045, 0.245)) * vec2(1.6, 0.6)));
        float sr2 = smoothstep(0.016, 0.006, abs(p.x - 0.29)) * smoothstep(bottomY + 0.1, bottomY + 0.2, p.y) * smoothstep(0.12, 0.02, p.y) * 0.35;
        col = mix(col, vec3(1.0), clamp(sx * 0.75 + spot + sh * 0.8 + sr2, 0.0, 1.0));
        // ---- cracks, spreading from a point as the bottle is about to burst
        float crk = v2.y;
        if (crk > 0.001) {
            vec2 o = vec2(hash12(vec2(seed, 1.0)), hash12(vec2(seed, 2.0))) * 0.3 - 0.15 + BODY_C;
            vec2 q = p - o;
            float ang = atan(q.y, q.x);
            float n = 7.0;
            float k = floor(ang / 6.2832 * n + 0.5);
            float aj = (k + 0.3 * (hash12(vec2(k, seed)) - 0.5)) / n * 6.2832;
            vec2 dir = vec2(cos(aj), sin(aj));
            float along = dot(q, dir);
            float off = abs(q.x * dir.y - q.y * dir.x) + 0.02 * sin(along * 30.0 + k);
            float len = crk * (0.5 + 0.4 * hash12(vec2(k, seed + 3.0)));
            float line = smoothstep(px * 1.6, 0.0, off) * step(0.0, along) * smoothstep(len, len - 0.05, along);
            col = mix(col, vec3(1.0), line * 0.9);
        }
    }
    // ---- the wooden stopper over the neck
    float capA = smoothstep(px, -px, dc);
    if (capA > 0.0) col = mix(col, wood(p, seed * 3.7, px), capA);
    // ---- out in the world: under the sea, tinted and darkened by its water; half buried, the sand hides it
    float cut = 1.0;
    if (world) {
        float depth = layerTop(2, vW.x) - vW.y;
        if (depth > 0.0) {
            col = mix(col, uSea2 * 0.7, 0.3 * smoothstep(0.0, 0.4, depth));
            col = mix(col, uDeep * 0.2, darkAt(depth) * 0.85);
        }
        col = mix(col, uDusk * 0.6, uSlow * 0.3);
        cut = smoothstep(-uPx, uPx, vW.y - sandTop(vW.x) + 0.05);
    }
    col = mix(col, uBridgeInk, uBridge);
    frag = vec4(enc(col), cover * v1.a * cut);
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
flat out vec4 v1, v2, v3, v0;
void main() {
    vec2 local = aQ * a0.zw;
    float c = cos(a1.x), s = sin(a1.x);
    vec2 wp = a0.xy + vec2(c * local.x - s * local.y, s * local.x + c * local.y);
    vQ = aQ;
    v0 = a0; v1 = a1; v2 = a2; v3 = a3;
    gl_Position = uVP * vec4(wp, 0.0, 1.0);
}
)";
inline const char* SPRITE_FS = R"(#version 330 core
in vec2 vQ;
flat in vec4 v1, v2, v3, v0;
out vec4 frag;
uniform float uTime, uPx, uLightPanel;
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
    frag = vec4(enc(col), a * v2.a);
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
