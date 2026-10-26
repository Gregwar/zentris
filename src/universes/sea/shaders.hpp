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
uniform vec3 uPanel;
uniform float uPanelA;
uniform float uStorm, uCaps, uGlint, uRain, uFlash, uFlashX, uSlow, uBridge, uStars, uClouds;
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
            c *= 0.96 + 0.08 * vnoise(vec2(w.x * 0.5 - uWaveT * 0.3 * uDir, w.y * 9.0 + float(k) * 5.0));
            // the storm: darker, greyer water
            c = mix(c, uStormSea * (1.35 - 0.3 * float(k)), uStorm * 0.6);
            c = mix(c, uDusk * (0.55 - 0.1 * float(k)), uSlow * 0.7);
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
                    float fade = exp(-max(depth, 0.0) / (1.2 + 2.0 * float(k)));
                    c = mix(c, mix(uSunCol, vec3(1.0), 0.3), dash * tw * tw * uGlint * fade * (0.5 + 0.5 * under));
                }
            }
            // whitecaps on the crests, foam blown down the face of the waves
            if (uCaps > 0.01) {
                float hgt = (top - layerBase(k)) / A;
                float fn = vnoise(vec2(w.x * 3.0 - uWaveT * uDir * 1.5, w.y * 6.0 + float(k) * 3.0));
                float cap = smoothstep(0.3, 0.85, hgt) * smoothstep(0.08 + 0.25 * A, 0.0, depth) * smoothstep(0.35, 0.6, fn);
                float streak = smoothstep(0.66, 0.85, vnoise(vec2(w.x * 0.8 - uWaveT * uDir * 0.8, w.y * 5.0 + float(k) * 9.0))) * uStorm * 0.35 *
                               exp(-max(depth, 0.0) / 1.5);
                c = mix(c, uFoam, clamp((cap + streak) * uCaps, 0.0, 0.9) * flatAt(w.x));
            }
            // a fine bright line along the crest
            c = mix(c, mix(uHaze, uFoam, 0.4), smoothstep(uPx * 2.0, 0.0, abs(depth)) * 0.35);
            float a = smoothstep(-uPx * 1.2, uPx * 1.2, depth);
            col = mix(col, c, a);
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
        col = mix(col, mix(uFoam, uSkyLow, 0.5), line * uRain * 0.16 * hudCol * (1.0 - inWell));
    }
    // the well: a calm, subdued panel over the sea and the sky, with a fine edge
    float wellE = smoothstep(uPx * 1.5, 0.0, abs(wellD + uPx));
    col = mix(col, uPanel, inWell * uPanelA);
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
    bc = mix(bc, uPanel, smoothstep(uPx, -uPx, wellD) * uPanelA);
    frag = vec4(enc(bc), a);
}
)";

// ---------------------------------------------------------------- the glass cubes of water
// One instance per cell. The cells of one piece make one glass vessel (the shape is the distance to the empty squares
// around the cell, those of its 8 neighbours that are not the same piece): a clear shell with a fine wall, a light
// line along its upper left inside, and water in it up to one level for the whole vessel (it levels out when the
// piece turns, tilts when it moves).
inline const char* CELL_VS = R"(#version 330 core
layout(location = 0) in vec2 aQ;
layout(location = 1) in vec4 a0;   // x, y, size, neighbour mask (L R D U LD RD LU RU)
layout(location = 2) in vec4 a1;   // linear rgb, alpha
layout(location = 3) in vec4 a2;   // water level above the cell's bottom (cells), slope, ghost, -
layout(location = 4) in vec4 a3;   // seed, -, -, -
uniform mat4 uVP;
out vec2 vP;
flat out vec4 v0, v1, v2, v3;
void main() {
    vP = aQ * 0.5;
    v0 = a0; v1 = a1; v2 = a2; v3 = a3;
    gl_Position = uVP * vec4(a0.xy + vP * a0.z, 0.0, 1.0);
}
)";
inline const char* CELL_FS = R"(#version 330 core
in vec2 vP;
flat in vec4 v0, v1, v2, v3;
out vec4 frag;
uniform float uPx, uTime, uBridge, uLightPanel;
uniform vec3 uBridgeInk;
float boxD(vec2 p, vec2 o) {
    vec2 q = abs(p - o) - 0.5;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}
void main() {
    int m = int(v0.w + 0.5);
    float size = v0.z;
    vec2 offs[8] = vec2[8](vec2(-1.0, 0.0), vec2(1.0, 0.0), vec2(0.0, -1.0), vec2(0.0, 1.0),
                           vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    float dE = 9.0, dTL = 9.0, dBR = 9.0;
    for (int i = 0; i < 8; i++) {
        if (((m >> i) & 1) != 0) continue;
        float d = boxD(vP, offs[i]);
        dE = min(dE, d);
        // the upper left sides (L, U, LU) catch the light, the lower right ones (R, D, RD) are in shade
        if ((offs[i].x < -0.5 && offs[i].y > -0.5) || (offs[i].y > 0.5 && offs[i].x < 0.5)) dTL = min(dTL, d);
        if ((offs[i].x > 0.5 && offs[i].y < 0.5) || (offs[i].y < -0.5 && offs[i].x > -0.5)) dBR = min(dBR, d);
    }
    // rounded outer corners (both sides open)
    const float R = 0.16;
    for (int c = 0; c < 4; c++) {
        vec2 s = vec2(c == 0 || c == 2 ? -1.0 : 1.0, c < 2 ? -1.0 : 1.0);
        int sx = s.x < 0.0 ? 0 : 1, sy = s.y < 0.0 ? 2 : 3;
        if (((m >> sx) & 1) != 0 || ((m >> sy) & 1) != 0) continue;
        vec2 q = vP * s - vec2(0.5 - R);
        if (q.x > 0.0 && q.y > 0.0) {
            float d = R - length(q);
            dE = min(dE, d);
            if (s.x < 0.0 && s.y > 0.0) dTL = min(dTL, d);
            if (s.x > 0.0 && s.y < 0.0) dBR = min(dBR, d);
        }
    }
    float px = uPx / size;
    const float inset = 0.045;
    float shape = smoothstep(inset - px, inset + px, dE);
    float e = dE - inset;   // depth inside the glass
    bool ghost = v2.z > 0.5;
    vec3 tint = v1.rgb;
    bool lightP = uLightPanel > 0.5;
    // the wall: a fine band of glass all around, darker along the lower right
    float wall = smoothstep(0.055 + px, 0.055 - px, e);
    vec3 wallC = lightP ? tint * 0.5 : mix(tint, vec3(1.0), 0.55);
    wallC *= dBR < dTL ? (lightP ? 0.8 : 0.78) : 1.0;
    if (ghost) {
        frag = vec4(enc(mix(wallC, uBridgeInk, uBridge)), shape * (wall * 0.4 + 0.05) * v1.a);
        return;
    }
    // the water: one level for the vessel, a gentle ripple on its surface
    vec2 wp = v0.xy + vP * size;
    float s = v2.x - 0.5 + v2.y * vP.x + (0.012 * sin(uTime * 1.6 + wp.x * 4.0 + v3.x) + 0.006 * sin(uTime * 2.7 - wp.x * 7.0)) / size;
    float depth = s - vP.y;
    float inner = smoothstep(0.05, 0.07, e);
    float wm = smoothstep(-px, px, depth) * inner;
    // the air in the glass: a pale tint of the water's color
    vec3 col = mix(tint, vec3(1.0), lightP ? 0.55 : 0.4);
    float a = lightP ? 0.22 : 0.16;
    // the water: lit under its surface, deeper down darker; soft caustics moving slowly
    vec3 wc = tint * (1.2 - 0.42 * smoothstep(0.0, 2.6, depth));
    wc = mix(wc, mix(tint, vec3(1.0), 0.5), exp(-max(depth, 0.0) / 0.08) * 0.55);
    float cs = vnoise(wp * 2.2 + vec2(uTime * 0.25, -uTime * 0.18)) + vnoise(wp * 3.1 - vec2(uTime * 0.2, uTime * 0.12));
    wc *= 0.92 + 0.16 * smoothstep(0.75, 1.25, cs) * (1.0 - smoothstep(0.0, 3.0, depth) * 0.5);
    col = mix(col, wc, wm);
    a = mix(a, 0.94, wm);
    // the meniscus: a bright line where the water meets the glass's air
    float men = smoothstep(px * 2.0, 0.0, abs(depth)) * inner;
    col = mix(col, mix(tint, vec3(1.0), 0.75), men * 0.9);
    a = max(a, men * 0.95);
    // the glass wall, then a light line along the upper left inside
    col = mix(col, wallC, wall);
    a = max(a, wall * 0.9);
    float hl = smoothstep(px * 1.8, 0.0, abs(dTL - inset - 0.095)) * step(dTL, dBR + 0.2);
    col = mix(col, vec3(1.0), hl * 0.55);
    a = max(a, hl * 0.6);
    col = mix(col, uBridgeInk, uBridge);
    a = mix(a, 1.0, uBridge);
    frag = vec4(enc(col), a * shape * v1.a);
}
)";

// ---------------------------------------------------------------- sprites
// Kinds: 0 a soft droplet (stretched along its rotation), 1 a shard of glass, 2 the bonus gauge (a round glass
// filling with water).
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
