#pragma once
// GLSL sources. All rendering happens in linear HDR; the composite pass grades and outputs sRGB.

namespace shaders {

// ---------------------------------------------------------------- fullscreen triangle
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
)";

// ---------------------------------------------------------------- background
inline const char* BG_FS_HEAD = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform int uStyleA, uStyleB;
uniform float uMix;
uniform vec3 uTopA, uBottomA, uGlowA, uTopB, uBottomB, uGlowB;
uniform vec4 uWipe; // x: on, y: front, z: width, w: front glow strength
vec3 uTop, uBottom, uGlow; // colors of the style being evaluated
uniform int uWipeShape;
uniform float uWipeSeed;
// Where the transition front reaches first (0) to last (1). Mirrors wipeCoord() in renderer.cpp.
float wipeCoord(vec2 ndc, float aspect, float noise) {
    float radial = length(ndc * vec2(aspect, 1.0)) / length(vec2(aspect, 1.0));
    float d;
    if (uWipeShape == 1) d = ndc.y * 0.5 + 0.5;
    else if (uWipeShape == 2) d = 0.5 - ndc.y * 0.5;
    else if (uWipeShape == 3) d = ndc.x * 0.5 + 0.5;
    else if (uWipeShape == 4) d = 0.5 - ndc.x * 0.5;
    else if (uWipeShape == 5) d = 1.0 - radial;
    else if (uWipeShape == 6) d = 1.0 - abs(ndc.x);
    else if (uWipeShape == 7) d = noise;
    else if (uWipeShape == 8) d = (ndc.x + ndc.y) * 0.25 + 0.5;
    else if (uWipeShape == 9) d = 0.6 * fract(atan(ndc.y, ndc.x * aspect) / 6.2831853 + 0.5) + 0.4 * radial;
    else if (uWipeShape == 10) d = (abs(ndc.x) + abs(ndc.y)) * 0.5;
    else if (uWipeShape == 11) d = (ndc.y * 0.5 + 0.5) * 0.85 + 0.075 * (1.0 + sin(ndc.x * 9.0));
    else if (uWipeShape == 12) d = (0.5 - ndc.y * 0.5) * 0.85 + 0.075 * (1.0 + sin(ndc.x * 7.0 + 1.0));
    else if (uWipeShape == 13) d = 0.35 * fract(ndc.y * 3.0) + 0.65 * (ndc.y * 0.25 + 0.5);
    else if (uWipeShape == 14) d = 0.35 * fract(ndc.x * 4.0) + 0.65 * (ndc.x * 0.25 + 0.5);
    else if (uWipeShape == 15) d = radial * (0.8 + 0.2 * cos(atan(ndc.y, ndc.x * aspect) * 5.0));
    else if (uWipeShape == 16) d = min(abs(ndc.x), abs(ndc.y));
    else if (uWipeShape == 17) d = min(abs(ndc.x - ndc.y), abs(ndc.x + ndc.y)) * 0.7;
    else if (uWipeShape == 18) d = 0.5 * mod(floor(ndc.x * 4.0) + floor(ndc.y * 3.0), 2.0) + 0.45 * radial;
    else if (uWipeShape == 19) d = length((ndc + 1.0) * vec2(aspect, 1.0)) / length(vec2(aspect, 1.0) * 2.0);
    else if (uWipeShape == 20) d = abs(ndc.y);
    else if (uWipeShape == 21) d = noise * 0.7 + 0.3 * fract(sin(dot(floor(ndc * 40.0), vec2(12.9898, 78.233))) * 43758.5453);
    else if (uWipeShape == 22) d = noise * 0.55 + (ndc.y * 0.5 + 0.5) * 0.45;
    else if (uWipeShape == 23) d = noise * 0.55 + radial * 0.45;
    else if (uWipeShape == 24) d = noise;
    else d = radial;
    return clamp(d, 0.0, 1.0);
}

uniform vec4 uP;
uniform float uTime, uBass, uIntensity, uBeat, uAspect, uPale;
uniform vec4 uRays; // strength A, count A, strength B, count B
uniform float uRayTime;
uniform vec4 uSurfA, uSurfB; // style, amount, scale, (unused) for each side of a transition
uniform float uSurfTime;
)";

inline const char* BG_FS_BODY = R"(
// The same hue with its chroma scaled by k (k > 1: a deeper tint, readable over pale backgrounds).
vec3 tint(vec3 col, float k) { return max(mix(vec3(dot(col, vec3(0.2126, 0.7152, 0.0722))), col, k), 0.0); }
vec3 bgStyle(int s, vec2 uv) {
    vec2 c = vec2((uv.x - 0.5) * uAspect, uv.y - 0.5);
    vec3 grad = mix(uBottom, uTop, smoothstep(0.0, 1.0, uv.y));
    float breathe = 0.85 + 0.25 * uIntensity + 0.1 * uBass;
    if (s == 0) { // gradient
        return grad;
    } else if (s == 1) { // radial halo
        float d = length(c - vec2(0.0, (uP.x - 0.5) * 0.3));
        return mix(grad, uGlow, exp(-d * d * (5.0 + 6.0 * uP.y)) * 0.55 * breathe);
    } else if (s == 2) { // horizon: glowing horizon line and a soft diffuse glow above it (no sun disc)
        float hy = 0.35 + 0.2 * uP.x;
        vec3 col = mix(uBottom, uTop, smoothstep(hy - 0.05, 1.0, uv.y));
        col = mix(col, uGlow * 0.8, exp(-abs(uv.y - hy) * 18.0) * 0.5 * breathe);
        vec2 gc = c - vec2((uP.y - 0.5) * 0.9, hy - 0.5);
        col += uGlow * exp(-dot(gc, gc) * (3.0 + 4.0 * uP.z)) * 0.18 * breathe * smoothstep(hy - 0.02, hy + 0.05, uv.y);
        return col;
    } else if (s == 3) { // nebula
        vec2 p = c * (1.6 + uP.x * 2.0) + vec2(uTime * 0.01, uTime * 0.004);
        float n = fbm(p + fbm(p * 1.3 + uTime * 0.02));
        float n2 = fbm(p * 2.2 - uTime * 0.015);
        vec3 col = grad;
        col = mix(col, uGlow, smoothstep(0.35, 0.9, n) * 0.55 * breathe);
        col = mix(col, uTop * 0.6, smoothstep(0.5, 0.9, n2) * 0.5);
        return col;
    } else if (s == 4) { // void: flat, with gentle vignette
        return mix(uTop, uBottom, 0.5) * (1.0 - 0.35 * dot(c, c));
    } else if (s == 5) { // aurora sky
        vec3 col = grad;
        for (int i = 0; i < 3; i++) {
            float fi = float(i);
            float y = 0.6 + 0.12 * fi + 0.06 * sin(c.x * (2.0 + fi) + uTime * (0.1 + 0.05 * fi) + uP.x * 6.0);
            float band = exp(-pow((uv.y - y) * (9.0 + 4.0 * fi), 2.0));
            float flick = 0.6 + 0.4 * vnoise(vec2(c.x * 6.0 + uTime * 0.2, fi * 7.0));
            col += uGlow * band * flick * (0.35 - 0.08 * fi) * breathe;
        }
        return col;
    } else if (s == 6) { // soft horizontal bands
        float n = 5.0 + floor(uP.x * 6.0);
        float b = floor(uv.y * n) / (n - 1.0);
        float edge = smoothstep(0.0, 0.02, fract(uv.y * n)) * (1.0 - smoothstep(0.98, 1.0, fract(uv.y * n)));
        vec3 col = mix(uBottom, uTop, b);
        return mix(col * 0.97, col, edge);
    } else if (s == 7) { // spotlight from above
        float x = c.x - (uP.x - 0.5) * 0.4;
        float cone = (1.0 - smoothstep(0.0, 0.35 + 0.4 * (1.0 - uv.y) * uP.y, abs(x))) * smoothstep(-0.2, 1.0, uv.y);
        return mix(grad, uGlow, cone * 0.45 * breathe);
    } else if (s == 8) { // synthwave grid floor under a horizon
        float hy = 0.3 + 0.12 * uP.x;
        vec3 col = mix(uBottom, uTop, smoothstep(hy - 0.02, 1.0, uv.y));
        col = mix(col, uGlow * 0.7, exp(-abs(uv.y - hy) * 25.0) * 0.5 * breathe);
        if (uv.y < hy) {
            float depth = 0.12 / max(hy - uv.y, 0.004);
            float gx = abs(fract(c.x * depth * 3.0) - 0.5);
            float gz = abs(fract(depth * 2.0 + uTime * (0.3 + 0.4 * uIntensity)) - 0.5);
            float w = 0.02 + 0.01 * depth;
            float line = max(smoothstep(0.5 - w, 0.5, gx), smoothstep(0.5 - w * 1.5, 0.5, gz));
            col = mix(col, uGlow, line * 0.45 * exp(-depth * 0.35) * breathe);
        }
        return col;
    } else if (s == 9) { // layered hills
        vec3 col = grad;
        for (int k = 0; k < 3; k++) {
            float fk = float(k);
            float hh = 0.34 - 0.08 * fk + 0.05 * sin(c.x * (1.6 + fk) + uP.x * 6.0 + fk * 2.0 + uTime * 0.01 * (fk + 1.0))
                       + 0.025 * sin(c.x * (5.0 + 2.0 * fk) + fk * 3.0);
            vec3 layer = mix(uBottom, uTop, 0.55 - 0.18 * fk) * (0.75 + 0.12 * fk);
            layer = mix(layer, uGlow, 0.08 * (2.0 - fk));
            col = mix(col, layer, 1.0 - smoothstep(hh - 0.004, hh + 0.004, uv.y));
        }
        return col;
    } else if (s == 10) { // slowly turning conic gradient
        float a = atan(c.y, c.x) / 6.2831853 + uTime * 0.008;
        float k = 0.5 + 0.5 * sin(a * 6.2831853 * (1.0 + floor(uP.x * 3.0)));
        vec3 col = mix(uBottom, uTop, k);
        return mix(col, uGlow, exp(-dot(c, c) * 4.0) * 0.3 * breathe);
    } else if (s == 12) { // two soft glows, left and right
        vec3 col = grad;
        col = mix(col, uGlow, exp(-dot(c - vec2(-0.55, 0.1), c - vec2(-0.55, 0.1)) * 5.0) * 0.4 * breathe);
        col = mix(col, uTop * 1.6, exp(-dot(c - vec2(0.55, -0.15), c - vec2(0.55, -0.15)) * 5.0) * 0.35 * breathe);
        return col;
    } else if (s == 13) { // soft layered waves
        vec3 col = grad;
        for (int k = 0; k < 4; k++) {
            float fk = float(k);
            float y = 0.2 + 0.17 * fk + 0.04 * sin(c.x * (2.0 + fk) + uTime * 0.05 * (1.0 + fk) + uP.x * 5.0);
            col = mix(col, mix(uBottom, uGlow, 0.25 + 0.1 * fk), smoothstep(0.03, 0.0, abs(uv.y - y)) * 0.4);
        }
        return col;
    } else if (s == 14) { // big soft clouds
        float n = fbm(c * 1.2 + vec2(uTime * 0.006, 0.0));
        return mix(grad, mix(uTop, uGlow, 0.4) * 1.1, smoothstep(0.45, 0.8, n) * 0.5 * breathe);
    } else if (s == 15) { // very soft dot pattern
        vec2 g = fract(uv * vec2(26.0 * uAspect, 26.0)) - 0.5;
        float dotm = exp(-dot(g, g) * 40.0);
        return mix(grad, uGlow, dotm * 0.12 * breathe);
    } else if (s == 16) { // diagonal gradient with soft bands
        float d = (uv.x * uAspect * 0.5 + uv.y) * 0.8;
        vec3 col = mix(uBottom, uTop, smoothstep(0.0, 1.2, d));
        return mix(col, uGlow, (0.5 + 0.5 * sin(d * 12.0 + uTime * 0.05)) * 0.08);
    } else if (s == 17) { // static sunburst from below
        vec2 p = c - vec2(0.0, -0.7);
        float a = atan(p.y, p.x);
        float ray = pow(0.5 + 0.5 * sin(a * (10.0 + floor(uP.x * 10.0))), 3.0);
        return mix(grad, uGlow, ray * exp(-length(p) * 1.2) * 0.35 * breathe);
    } else if (s == 18) { // slow concentric ripples
        float r = length(c);
        float w = 0.5 + 0.5 * sin(r * 30.0 - uTime * 0.4);
        return mix(grad, uGlow, w * exp(-r * 2.0) * 0.18 * breathe);
    } else if (s == 19) { // slow plasma
        float v = sin(c.x * 3.0 + uTime * 0.05) + sin(c.y * 4.0 - uTime * 0.04) + sin((c.x + c.y) * 3.0 + uTime * 0.03);
        return mix(grad, mix(uGlow, uTop * 1.5, 0.5 + 0.5 * sin(v)), 0.25 * (0.5 + 0.5 * cos(v * 1.3)) * breathe);
    } else if (s == 20) { // sea horizon with a shimmering reflection
        float hy = 0.38 + 0.1 * uP.x;
        vec3 sky = mix(uBottom, uTop, smoothstep(hy, 1.0, uv.y));
        vec3 sea = mix(uBottom * 0.6, uTop * 0.4, smoothstep(0.0, hy, uv.y));
        float refl = exp(-abs(c.x) * 4.0) * (0.5 + 0.5 * sin(uv.y * 160.0 + uTime * 0.8 + sin(c.x * 20.0)));
        sea = mix(sea, uGlow, refl * 0.3 * smoothstep(0.0, hy, uv.y));
        vec3 col = uv.y > hy ? sky : sea;
        return mix(col, uGlow, exp(-abs(uv.y - hy) * 40.0) * 0.4 * breathe);
    } else if (s == 21) { // sharp mountain peaks
        vec3 col = grad;
        for (int k = 0; k < 2; k++) {
            float fk = float(k);
            float x = c.x * (2.0 + fk) + uP.x * 7.0 + fk * 3.0;
            float hh = 0.3 - 0.08 * fk + 0.12 * abs(fract(x * 0.5) - 0.5) * 2.0 * (0.6 + 0.4 * sin(x * 1.7));
            col = mix(col, mix(uBottom, uTop, 0.35 - 0.15 * fk) * (0.7 + 0.2 * fk), 1.0 - smoothstep(hh - 0.003, hh + 0.003, uv.y));
        }
        return col;
    } else if (s == 22) { // vertical light shafts
        float x = c.x * 6.0 + uP.x * 10.0;
        float shaft = pow(0.5 + 0.5 * sin(x + sin(x * 0.37 + uTime * 0.05) * 2.0), 6.0);
        return mix(grad, uGlow, shaft * smoothstep(-0.2, 1.0, uv.y) * 0.3 * breathe);
    } else if (s == 23) { // soft halo ring around the board
        float r = length(c * vec2(1.0, 1.3));
        float ring = exp(-pow((r - 0.42 - 0.05 * uP.x) * 9.0, 2.0));
        return mix(grad, uGlow, ring * 0.4 * breathe);
    } else if (s == 24) { // moon: a large soft disc low in the sky, faint halo, subtle maria
        vec2 mc = vec2((uP.y < 0.5 ? -1.0 : 1.0) * (0.5 + 0.18 * uP.x) * uAspect * 0.62, -0.08 + 0.12 * uP.z);
        float R = 0.13 + 0.04 * uP.w, d = length(c - mc);
        vec3 col = mix(grad, uGlow, exp(-max(d - R, 0.0) * 7.0) * 0.2 * (1.0 - 0.4 * uPale));
        float tex = fbm((c - mc) / R * 1.6 + uP.x * 9.0);
        vec3 moon = mix(tint(uGlow, 0.6) * 1.15, tint(uGlow, 1.6), uPale);
        moon = mix(moon, mix(moon, uTop, 0.4), smoothstep(0.45, 0.75, tex) * 0.4);
        moon *= 1.0 - 0.15 * smoothstep(0.0, 1.0, dot(c - mc, vec2(0.7, -0.7)) / R * 0.5 + 0.5); // soft shading
        return mix(col, moon, smoothstep(R, R - 0.01, d) * mix(0.6, 0.65, uPale));
    } else if (s == 25) { // ridgelines: stacked undulating profiles, front ones hiding those behind
        vec3 col = grad, ink = mix(uGlow, tint(uBottom, 2.0) * 0.6, uPale);
        float n = 22.0 + floor(uP.x * 10.0), top = 0.62 + 0.1 * uP.y;
        for (int i = 0; i < 32; i++) {
            float fi = float(i);
            if (fi >= n) break;
            float y0 = top * (1.0 - fi / n);
            float x = c.x * 2.6 + fi * 3.1 + uP.z * 20.0;
            float bump = vnoise(vec2(x, fi * 1.7 + uTime * 0.04)) * 0.6 + vnoise(vec2(x * 2.3, fi + uTime * 0.03)) * 0.4;
            float h = y0 + 0.08 * pow(bump, 2.5) * (0.6 + 0.8 * exp(-c.x * c.x * 1.5));
            col = mix(col, grad, step(uv.y, h)); // the ridge fill hides the lines behind
            col = mix(col, ink, exp(-pow((uv.y - h) * 450.0, 2.0)) * (0.3 + 0.12 * fi / n));
        }
        return col;
    } else if (s == 26) { // city: a distant skyline with a few lit windows and a glow above it
        vec3 col = mix(grad, uGlow, exp(-max(uv.y - 0.2, 0.0) * 6.0) * 0.28 * (1.0 - 0.3 * uPale));
        vec3 sil = mix(mix(uBottom, uTop, 0.6) * 0.75, tint(uBottom, 1.5) * 0.8, uPale);
        for (int k = 0; k < 2; k++) {
            float fk = float(k), w = 0.05 - 0.015 * fk;
            float x = c.x / w + uP.x * 50.0 + fk * 17.0, id = floor(x);
            float hb = 0.1 + 0.02 * fk + (0.05 + 0.03 * fk) * hash12(vec2(id, fk)) + 0.1 * pow(hash12(vec2(id, fk + 7.0)), 6.0);
            float gap = smoothstep(0.0, 0.03, fract(x)) * smoothstep(1.0, 0.97, fract(x)) * step(0.12, hash12(vec2(id, fk + 3.0)));
            hb = mix(0.08 + 0.02 * fk, hb, gap) - 0.03 * fk;
            vec3 lc = mix(mix(sil, grad, 0.45), sil, fk);
            float inside = smoothstep(hb + 0.0015, hb - 0.0015, uv.y);
            col = mix(col, lc, inside);
            if (k == 1) {
                vec2 wg = vec2(fract(x) * 5.0, uv.y * 140.0);
                vec2 wf = fract(wg) - 0.5;
                float lit = step(0.9, hash12(floor(wg) + id * 13.0)) * (0.6 + 0.4 * sin(uTime * 0.05 + hash12(floor(wg)) * 40.0));
                float win = smoothstep(0.3, 0.2, abs(wf.x)) * smoothstep(0.32, 0.22, abs(wf.y)) * step(0.01, hb - uv.y - 0.01);
                col = mix(col, uGlow * mix(1.2, 1.0, uPale), inside * win * lit * 0.6);
            }
        }
        return col;
    } else if (s == 27) { // color mesh: a smooth four-corner gradient whose control colors drift slowly
        float t = uTime * 0.02 + uP.x * 6.2831853;
        vec3 deep = mix(uGlow, tint(uGlow, 2.5) * 0.9, uPale);
        vec3 k0 = mix(uBottom, deep, 0.35 + 0.2 * sin(t)), k1 = mix(uTop, uBottom, 0.5 + 0.5 * sin(t * 0.7 + 2.0));
        vec3 k2 = mix(uTop, deep, 0.2 + 0.15 * sin(t * 0.8 + 4.0)), k3 = mix(tint(uBottom, 1.6), uTop, 0.5 + 0.5 * sin(t * 0.6 + 1.0));
        vec2 q = uv + 0.08 * vec2(sin(uv.y * 3.0 + t), sin(uv.x * 2.5 - t * 0.8));
        q = smoothstep(0.0, 1.0, clamp(q, 0.0, 1.0));
        return mix(mix(k0, k1, q.x), mix(k2, k3, q.x), q.y);
    } else if (s == 28) { // eclipse: a dark disc with a glowing corona and soft rays
        vec2 ec = vec2((uP.y < 0.5 ? -1.0 : 1.0) * (0.42 + 0.14 * uP.x) * uAspect * 0.62, 0.17 + 0.08 * uP.z);
        float R = 0.09 + 0.03 * uP.w;
        vec2 p = c - ec;
        float d = length(p), a = atan(p.y, p.x);
        float rays = 0.55 + 0.45 * vnoise(vec2(a * 5.0 + 3.0, uTime * 0.02)) * vnoise(vec2(a * 11.0, uTime * 0.03 + 5.0));
        float cor = exp(-max(d - R, 0.0) * mix(14.0, 9.0, rays)) * (0.4 + 0.6 * rays);
        vec3 cc = mix(uGlow * 1.2, tint(uGlow, 2.5) * 0.85, uPale);
        vec3 col = mix(grad, cc, cor * 0.55 * step(R, d));
        col = mix(col, cc * mix(1.1, 0.9, uPale), exp(-pow((d - R) * 160.0, 2.0)) * 0.5);
        return mix(col, mix(mix(uTop, uBottom, 0.3) * 0.5, tint(uBottom, 1.3) * 0.8, uPale), smoothstep(R, R - 0.004, d));
    } else if (s == 29) { // cirrus: high streaky wisps drifting across the sky
        vec2 q = vec2(c.x + uTime * 0.005, c.y + (c.x + uTime * 0.005) * (0.25 * uP.z - 0.12));
        q.y += 0.07 * fbm(q * vec2(1.5, 3.0) + uP.x * 10.0) + 0.03 * sin(q.x * 2.5 + uP.y * 6.0);
        float fib = vnoise(vec2(q.x * 3.0, q.y * 45.0)) * 0.6 + vnoise(vec2(q.x * 7.0, q.y * 90.0) + 4.0) * 0.4; // fine fibers
        float clump = smoothstep(0.45, 0.75, fbm(vec2(q.x * 1.3, q.y * 5.0) + uP.y * 7.0)); // long thin patches
        float wisp = clump * mix(0.35, 1.0, smoothstep(0.3, 0.8, fib)) * smoothstep(0.15, 0.55, uv.y);
        vec3 cl = mix(tint(mix(uTop, uGlow, 0.6), 0.7) * 1.5, tint(uGlow, 2.2) * 0.92, uPale);
        return mix(grad, cl, wisp * mix(0.55, 0.45, uPale));
    } else if (s == 30) { // canyon: layered mesas with rock strata, warm near and cool far
        vec3 col = grad;
        for (int k = 0; k < 3; k++) {
            float fk = float(k);
            float x = c.x * (1.2 + 0.6 * fk) + uP.x * 9.0 + fk * 4.0;
            float pl = smoothstep(0.35, 0.6, vnoise(vec2(x, fk * 5.0))); // flat tops with steep sides
            float hh = 0.3 - 0.08 * fk + 0.11 * pl + 0.008 * vnoise(vec2(x * 12.0, fk));
            float y = uv.y + 0.006 * sin(c.x * 9.0 + fk) + 0.004 * vnoise(vec2(c.x * 30.0, fk));
            float band = hash12(vec2(floor(y * (50.0 + 20.0 * fk)), fk));
            vec3 warm = mix(uBottom, uGlow, 0.5), cool = mix(uTop, uBottom, 0.6);
            vec3 rock = mix(cool, warm, 0.15 + 0.25 * fk + 0.35 * band);
            rock = mix(rock, tint(mix(uBottom, uGlow, 0.2 + 0.3 * band), 1.4) * (0.8 + 0.04 * fk), uPale);
            rock = mix(rock, grad, 0.4 - 0.15 * fk); // haze on the far layers
            col = mix(col, rock, smoothstep(hh + 0.002, hh - 0.002, uv.y));
        }
        return col;
    } else if (s == 31) { // swirl: a slow painterly flow of the sky, Van Gogh like
        vec2 p = c * 1.4;
        for (int i = 0; i < 3; i++) {
            float fi = float(i);
            vec2 o = vec2(sin(fi * 2.3 + uP.x * 6.0) * 0.9, cos(fi * 1.7 + uP.y * 6.0) * 0.35) + 0.05 * vec2(sin(uTime * 0.03 + fi), cos(uTime * 0.025 + fi));
            vec2 q = p - o;
            float ang = 2.2 * exp(-dot(q, q) * 3.0) * (mod(fi, 2.0) * 2.0 - 1.0);
            p = o + mat2(cos(ang), -sin(ang), sin(ang), cos(ang)) * q;
        }
        float n = fbm(p * 1.5 + uTime * 0.01);
        float strokes = 0.5 + 0.5 * sin(p.y * 30.0 + n * 6.0 + vnoise(p * 9.0) * 2.0);
        vec3 paint = mix(mix(uTop, uGlow, 0.5) * 1.3, tint(uGlow, 2.2) * 0.9, uPale);
        vec3 col = mix(grad, paint, smoothstep(0.4, 0.75, n) * mix(0.28, 0.3, uPale));
        return mix(col, mix(uGlow, tint(uBottom, 1.8) * 0.8, uPale), strokes * smoothstep(0.3, 0.7, n) * 0.1);
    } else if (s == 32) { // forest: rows of pine silhouettes in depth layers, fog between rows
        vec3 col = grad, fog = mix(grad, uGlow, 0.25 * (1.0 - 0.5 * uPale));
        vec3 dark = mix(mix(uBottom, uTop, 0.5) * 0.55, tint(uBottom, 1.8) * 0.7, uPale);
        for (int k = 0; k < 4; k++) {
            float fk = float(k), w = 0.035 + 0.017 * fk; // back rows first: smaller trees, more fog
            float base = 0.3 - 0.07 * fk + 0.025 * sin(c.x * (1.3 + fk) + fk * 2.0 + uP.x * 6.0);
            float x = c.x / w + fk * 11.3 + uP.y * 30.0, id = floor(x), fx = fract(x) - 0.5;
            float off = (hash12(vec2(id, fk)) - 0.5) * 0.3, th = w * (1.6 + 1.2 * hash12(vec2(id, fk + 3.0)));
            float hgt = (uv.y - base) / th; // 0 at the foot of the tree, 1 at its tip
            float tier = 0.7 + 0.3 * (1.0 - fract(hgt * 4.0 + 0.3)); // branch tiers
            float hw = (1.0 - hgt) * 0.5 * tier;
            float tree = smoothstep(hw + 0.015, hw - 0.015, abs(fx - off)) * step(0.0, hgt) * step(hgt, 1.0);
            float ground = step(uv.y, base + 0.005);
            col = mix(col, fog, smoothstep(base + 0.12, base, uv.y) * 0.5); // fog lying on the row behind
            col = mix(col, mix(dark, fog, 0.6 - 0.18 * fk), max(tree, ground));
        }
        return col;
    } else if (s == 33) { // isometric: a faint tumbling-cubes pattern
        vec2 p = c * (7.0 + 4.0 * uP.x) + vec2(uTime * 0.01, 0.0);
        vec2 r = vec2(1.0, 1.7320508), h = r * 0.5;
        vec2 a = mod(p, r) - h, b = mod(p - h, r) - h;
        vec2 g = dot(a, a) < dot(b, b) ? a : b;
        float ang = atan(g.y, g.x);
        float face = ang > 0.5236 && ang < 2.618 ? 1.0 : (ang >= 2.618 || ang < -1.5708 ? 0.5 : 0.0); // top, left, right
        vec3 col = mix(mix(grad, uGlow, 0.07 * face), grad * (0.95 + 0.06 * face), uPale);
        vec2 ag = abs(g);
        float border = 0.5 - max(dot(ag, vec2(0.5, 0.8660254)), ag.x);
        float edge = 1.0 - smoothstep(0.0, 0.025, border);
        return mix(col, mix(uGlow, tint(uBottom, 1.5) * 0.8, uPale), edge * 0.1);
    } else { // starfield
        vec3 col = grad;
        vec2 cell = floor(uv * vec2(140.0 * uAspect, 140.0));
        float h = hash12(cell);
        if (h > 0.975) {
            vec2 f = fract(uv * vec2(140.0 * uAspect, 140.0)) - 0.5;
            float tw = 0.6 + 0.4 * sin(uTime * (0.3 + h * 0.6) + h * 50.0); // slow shimmer
            col += uGlow * exp(-dot(f, f) * 30.0) * tw * (h - 0.975) * 30.0 * 0.5;
        }
        return col;
    }
}
// Continuous surface layers (not points). Returns color and coverage to mix over the background.
float polyDist(vec2 q, float sides) {
    float a = atan(q.y, q.x), seg = 6.2831853 / sides;
    return cos(floor(0.5 + a / seg) * seg - a) * length(q);
}
vec3 hueTurn(vec3 col, float a) { // rotate a color around the gray axis
    const vec3 k = vec3(0.57735);
    return col * cos(a) + cross(k, col) * sin(a) + k * dot(k, col) * (1.0 - cos(a));
}
float smoothNoise(vec2 p) { // three octaves only: rounder shapes than fbm
    return 0.55 * vnoise(p) + 0.3 * vnoise(p * 2.1 + vec2(3.7, 1.9)) + 0.15 * vnoise(p * 4.3 + vec2(8.1, 5.3));
}
vec4 paleInk(vec3 col, float a) { // on pale scenes a light pattern vanishes into the light background: give it more coverage
    return vec4(col, a * mix(1.0, 2.2, uPale));
}
vec4 surface(vec4 S, vec2 uv) {
    int s = int(S.x + 0.5);
    if (s == 0 || S.y <= 0.001) return vec4(0.0);
    float amt = S.y * (0.7 + 0.5 * uIntensity) * (1.0 + 0.15 * uBeat) * mix(1.0, 0.7, uPale);
    vec2 c = vec2((uv.x - 0.5) * uAspect, uv.y - 0.5) * S.z;
    float T = uSurfTime;
    vec3 colA = uGlow, colB = mix(uTop, uGlow, 0.5) * 1.4;
    if (s == 1) { // drifting smoke (domain-warped noise)
        vec2 q = c * 1.5;
        vec2 w = vec2(fbm(q + T * 0.05), fbm(q + vec2(5.2, 1.3) - T * 0.04));
        float n = fbm(q + 2.0 * w + T * 0.03);
        return vec4(mix(colA, colB, w.x), smoothstep(0.35, 0.85, n) * amt);
    } else if (s == 2) { // silk ribbons
        float v = c.y * 3.0 + sin(c.x * 2.0 + T * 0.2) * 0.6 + fbm(c * 1.2 + T * 0.03) * 1.5;
        float band = pow(0.5 + 0.5 * sin(v * 3.0), 4.0);
        return vec4(mix(colA, colB, 0.5 + 0.5 * sin(v)), band * amt * 0.8);
    } else if (s == 3) { // lava lamp blobs
        float f = 0.0;
        for (int i = 0; i < 6; i++) {
            float fi = float(i);
            vec2 p = vec2(sin(T * 0.07 * (1.0 + fi * 0.3) + fi * 2.1) * 0.7, sin(T * 0.05 * (1.0 + fi * 0.2) + fi * 1.3) * 0.45);
            f += 0.018 / (dot(c - p, c - p) + 0.002);
        }
        return vec4(mix(colA, colB, smoothstep(1.0, 3.0, f)), smoothstep(0.9, 1.5, f) * amt);
    } else if (s == 4) { // water caustics
        vec2 p = c * 6.0;
        for (int i = 0; i < 4; i++) {
            float fi = float(i);
            p += vec2(cos(p.y * 1.3 + T * 0.4 + fi), sin(p.x * 1.1 - T * 0.35 + fi * 1.7)) * 0.6;
        }
        float v = 0.5 + 0.5 * sin(p.x + p.y);
        return vec4(colA * 1.1, pow(v, 12.0) * amt * 0.7); // thin, soft light lines
    } else if (s == 5) { // ink swirls: contour lines of a warped field
        float n = fbm(c * 2.0 + fbm(c * 3.0 + T * 0.03) * 2.0);
        float l = smoothstep(0.44, 0.5, abs(fract(n * 5.0) - 0.5));
        return vec4(mix(colA, colB, n), l * amt * 0.7);
    } else if (s == 6) { // large slowly rotating geometric outlines
        float a = 0.0;
        for (int k = 0; k < 3; k++) {
            float fk = float(k), dir = mod(fk, 2.0) * 2.0 - 1.0;
            float ang = T * 0.04 * (fk + 1.0) * dir;
            vec2 q = mat2(cos(ang), -sin(ang), sin(ang), cos(ang)) * c;
            float d = polyDist(q, 3.0 + fk + floor(S.z * 2.0));
            a += exp(-abs(d - (0.25 + 0.18 * fk)) * 90.0) * (1.0 - 0.2 * fk);
        }
        return vec4(mix(colA, colB, 0.4), min(a, 1.0) * amt);
    } else if (s == 7) { // continuous aurora curtains
        float a = 0.0;
        for (int k = 0; k < 3; k++) {
            float fk = float(k);
            float x = c.x * (1.5 + fk * 0.5) + T * 0.05 * (fk + 1.0);
            float y0 = 0.62 + 0.1 * fk + 0.07 * sin(x * 2.0) + 0.05 * fbm(vec2(x, fk));
            float curtain = exp(-pow((uv.y - y0) * 7.0, 2.0)) * smoothstep(y0 - 0.25, y0, uv.y);
            float rays = 0.6 + 0.4 * sin(x * 25.0 + fbm(vec2(x * 3.0, T * 0.1)) * 4.0);
            a += curtain * rays * (0.7 - 0.15 * fk);
        }
        return vec4(mix(colA, colB, 0.3), min(a, 1.0) * amt);
    } else if (s == 8) { // drifting fog banks
        float a = 0.0;
        for (int k = 0; k < 4; k++) {
            float fk = float(k);
            float y0 = 0.15 + 0.2 * fk + 0.05 * fbm(vec2(c.x * 2.0 + T * 0.02 * (fk + 1.0), fk));
            a += exp(-pow((uv.y - y0) * 8.0, 2.0)) * fbm(vec2(c.x * 3.0 + T * 0.03 * (fk + 1.0), fk * 3.0));
        }
        return vec4(mix(uTop * 1.5, colA, 0.4), min(a, 1.0) * amt);
    } else if (s == 9) { // sweeping light beams from below
        float a = 0.0;
        for (int k = 0; k < 3; k++) {
            float fk = float(k);
            vec2 o = vec2((fk - 1.0) * 0.6 * uAspect, -0.62);
            float ang = 1.5708 + sin(T * 0.12 + fk * 2.1) * 0.45;
            vec2 dir = vec2(cos(ang), sin(ang));
            vec2 p = c / S.z - o;
            float along = dot(p, dir), across = abs(dot(p, vec2(-dir.y, dir.x)));
            a += (along > 0.0 ? 1.0 : 0.0) * exp(-across * across / (0.002 + 0.01 * along)) * exp(-along * 0.9);
        }
        return vec4(colA * 1.1, min(a, 1.0) * amt);
    } else if (s == 10) { // glowing rings flowing inward
        float r = length(c);
        float v = fract(r * 4.0 + T * 0.12);
        return vec4(mix(colA, colB, r), exp(-pow((v - 0.5) * 10.0, 2.0)) * exp(-r * 1.2) * amt);
    } else if (s == 11) { // liquid gradient blobs
        float v = sin(c.x * 2.0 + T * 0.1 + sin(c.y * 3.0 - T * 0.07));
        float n = fbm(c * 0.8 + T * 0.02);
        return vec4(mix(colA, colB, 0.5 + 0.5 * v), (0.4 + 0.6 * n) * amt * 0.55);
    } else if (s == 12) { // passing cloud shadows and light
        float n = fbm(c * 1.0 + vec2(T * 0.04, T * 0.01));
        vec3 col = n > 0.5 ? colA * 1.1 : uBottom * 0.35;
        return vec4(col, abs(n - 0.5) * 2.0 * amt);
    } else if (s == 13) { // voronoi map: drifting seeds, so borders slide and cells reshape
        vec2 p = c * 4.0, ip = floor(p), fp = fract(p);
        float d1 = 8.0, d2 = 8.0;
        vec2 id = vec2(0.0);
        for (int j = -1; j <= 1; j++)
            for (int i = -1; i <= 1; i++) {
                vec2 g = vec2(float(i), float(j));
                vec2 h = hash22(ip + g);
                float d = length(g + 0.5 + 0.42 * sin(T * 0.15 + h * 6.2831853) - fp);
                if (d < d1) { d2 = d1; d1 = d; id = ip + g; } else if (d < d2) d2 = d;
            }
        float edge = 1.0 - smoothstep(0.02, 0.07, d2 - d1);
        float shade = hash12(id);
        float pulse = 0.5 + 0.5 * sin(T * 0.3 + shade * 6.2831853);
        return vec4(mix(colA, colB, shade), (edge * 0.75 + 0.22 * pulse * shade) * amt);
    } else if (s == 14) { // water seen through a window: a waving surface, its level slowly rising and falling
        float x = c.x / S.z;
        float level = 0.38 + 0.08 * sin(T * 0.05 + uP.z * 6.0);
        float surf = level + 0.025 * sin(x * 3.0 + T * 0.5) + 0.012 * sin(x * 7.0 - T * 0.37) + 0.006 * sin(x * 13.0 + T * 0.8);
        float below = smoothstep(surf + 0.003, surf - 0.003, uv.y);
        float depth = clamp((surf - uv.y) / max(surf, 0.05), 0.0, 1.0);
        vec2 p = vec2(x * 5.0, uv.y * 9.0);
        p += vec2(sin(p.y + T * 0.4), cos(p.x - T * 0.3)) * 0.5;
        float rip = pow(0.5 + 0.5 * sin(p.x + p.y), 6.0) * (1.0 - depth); // soft light ripples near the top
        float crest = exp(-pow((uv.y - surf) * 220.0, 2.0));
        vec3 col = mix(colA * 0.9, uBottom * 0.35, depth) + colA * rip * 0.6;
        return vec4(mix(col, colA * 1.4, crest), min(1.0, max(below * (0.55 + 0.3 * depth), crest) * amt * 1.5));
    } else if (s == 15) { // low-poly facets: a triangle mesh whose vertices rise and fall, each face flat-lit
        vec2 p = c * 4.5;
        vec2 q = vec2(p.x - p.y * 0.57735, p.y * 1.1547);
        vec2 iq = floor(q), f = fract(q);
        // Split each lattice cell along its short diagonal: equilateral triangles.
        bool up = f.x + f.y > 1.0;
        vec2 b = up ? vec2(1.0) : vec2(0.0);
        vec2 v0 = iq + b, v1 = iq + vec2(1.0, 0.0), v2 = iq + vec2(0.0, 1.0);
        vec3 A = vec3(v0.x + v0.y * 0.5, v0.y * 0.866, 0.35 * sin(T * 0.25 + hash12(v0) * 6.2831853));
        vec3 B = vec3(v1.x + v1.y * 0.5, v1.y * 0.866, 0.35 * sin(T * 0.25 + hash12(v1) * 6.2831853));
        vec3 C = vec3(v2.x + v2.y * 0.5, v2.y * 0.866, 0.35 * sin(T * 0.25 + hash12(v2) * 6.2831853));
        vec3 n = normalize(cross(B - A, C - A));
        n *= sign(n.z);
        float light = clamp(0.5 + 1.2 * dot(n.xy, normalize(vec2(0.6, 0.8))), 0.0, 1.0); // side light: strong face contrast
        float tint = hash12(iq * 2.0 + b);
        // Thin edges: distance to the nearest triangle side in lattice space.
        float de = up ? min(min(1.0 - f.x, 1.0 - f.y), (f.x + f.y - 1.0) * 0.7071) : min(min(f.x, f.y), (1.0 - f.x - f.y) * 0.7071);
        float edge = 1.0 - smoothstep(0.0, 0.03, de);
        vec3 col = mix(colB * 0.7, colA * 1.2, light) * (0.85 + 0.3 * tint);
        return vec4(mix(col, colA * 1.3, edge * 0.6), (0.08 + 0.36 * light + 0.2 * edge) * amt);
    } else if (s == 16) { // hexagon tiles lighting up as slow waves cross them
        vec2 p = c * 6.0;
        vec2 r = vec2(1.0, 1.7320508), h = r * 0.5;
        vec2 a = mod(p, r) - h, b = mod(p - h, r) - h;
        vec2 g = dot(a, a) < dot(b, b) ? a : b;
        vec2 id = p - g;
        vec2 ag = abs(g);
        float border = 0.5 - max(dot(ag, vec2(0.5, 0.8660254)), ag.x);
        float w = 0.5 + 0.5 * sin(length(id) * 0.6 - T * 0.4 + hash12(floor(id * 2.0)) * 1.5);
        float line = 1.0 - smoothstep(0.0, 0.04, border);
        float lit = pow(w, 3.0) * smoothstep(0.02, 0.08, border);
        return vec4(mix(colA, colB * 1.3 + 0.1, lit), (line * 0.16 + lit * 0.55) * amt);
    } else if (s == 18) { // marble: soft clouded stone with thin veins slowly shifting
        vec2 p = c * 1.2;
        vec2 wq = vec2(fbm(p * 0.9 + T * 0.01), fbm(p * 0.9 + vec2(5.2, 1.3) - T * 0.008));
        float r1 = 1.0 - abs(smoothNoise(p * 1.3 + wq * 2.2) - 0.5) * 2.0; // ridges of warped noise: a vein network
        float r2 = 1.0 - abs(smoothNoise(p * 2.8 + wq * 2.8 + 7.0) - 0.5) * 2.0;
        float vein = pow(r1, 24.0) * 0.8 + 0.35 * pow(r2, 30.0) + 0.25 * pow(r1, 5.0);
        float cloud = smoothstep(0.35, 0.75, fbm(p * 0.7 + wq + 2.0));
        return paleInk(mix(mix(colB * 0.7, colA, cloud * 0.5), colA * 1.15, min(vein, 1.0)), (0.08 + 0.16 * cloud + 0.4 * vein) * amt);
    } else if (s == 19) { // topography: contour lines of a slowly evolving height map
        vec2 p = c * 1.3;
        float h = smoothNoise(p + vec2(T * 0.01, 0.0)) + 0.4 * smoothNoise(p * 2.0 + vec2(4.0, 1.0) - vec2(0.0, T * 0.013));
        float v = h * 12.0, fw = max(fwidth(v), 1e-4);
        float line = 1.0 - smoothstep(fw * 0.4, fw * 1.6, 0.5 - abs(fract(v) - 0.5));
        float major = mod(floor(v + 0.5), 5.0) < 0.5 ? 1.0 : 0.55;
        return paleInk(mix(colA, colB, fract(h * 2.0)), line * major * amt * 0.6);
    } else if (s == 20) { // kaleidoscope: a soft mirrored pattern slowly turning
        // Repeated mirror folds, each followed by a slowly turning rotation: symmetry all over the screen.
        vec2 q = c * 1.4;
        float ang = 0.6 + T * 0.015;
        mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
        for (int i = 0; i < 5; i++) q = rot * (abs(q) - vec2(0.32, 0.22));
        float n = smoothNoise(q * 2.0 + 1.3);
        float w = 0.5 + 0.5 * sin(length(q) * 9.0 + n * 4.0 - T * 0.04), petals = smoothstep(0.4, 0.9, w);
        float rim = exp(-pow((w - 0.62) * 9.0, 2.0)); // soft outline around each mirrored shape
        return paleInk(mix(colB, colA * 1.1, max(petals, rim)), (0.06 + 0.14 * petals + 0.22 * rim) * amt);
    } else if (s == 21) { // rain rings: sparse drops landing on still water, rings spreading and fading
        vec2 p = c * 4.0, ip = floor(p);
        float a = 0.0;
        for (int j = -1; j <= 1; j++)
            for (int i = -1; i <= 1; i++) {
                vec2 g = ip + vec2(float(i), float(j));
                float h = hash12(g);
                float tt = T * (0.09 + 0.04 * h) + h * 7.0;
                float cyc = floor(tt), t = fract(tt);
                if (hash12(g + cyc * 3.7) > 0.45) continue; // this cell stays dry this time
                vec2 o = g + 0.2 + 0.6 * hash22(g + cyc * 1.3);
                float d = length(p - o), R = t * 0.85;
                float ring = exp(-pow((d - R) * 34.0, 2.0)) + 0.5 * exp(-pow((d - R * 0.65) * 40.0, 2.0));
                a += ring * pow(1.0 - t, 2.0) * smoothstep(0.0, 0.05, t);
            }
        return paleInk(colA * 1.1, min(a, 1.0) * amt * 0.55);
    } else if (s == 22) { // truchet: quarter-circle arcs joining into flowing paths, a faint light travelling along them
        vec2 p = c * 5.0, ip = floor(p), f = fract(p);
        bool flip = hash12(ip) > 0.5;
        if (flip) f.x = 1.0 - f.x;
        bool first = abs(length(f) - 0.5) < abs(length(f - 1.0) - 0.5);
        vec2 q = first ? f : 1.0 - f;
        float d = abs(length(q) - 0.5);
        float s0 = atan(q.y, q.x) / 1.5707963; // 0..1 along the arc
        if ((mod(ip.x + ip.y, 2.0) > 0.5) != flip) s0 = 1.0 - s0;
        if (!first) s0 = 1.0 - s0;
        float fw = fwidth(p.x);
        float line = 1.0 - smoothstep(0.03, 0.03 + fw * 1.5, d);
        float glow = exp(-d * d * 150.0);
        float shim = pow(0.5 + 0.5 * sin(6.2831853 * (s0 - T * 0.08)), 6.0);
        return paleInk(mix(colB, colA * 1.2, shim), (line * 0.32 + glow * shim * 0.45) * amt);
    } else if (s == 23) { // weave: strands going over and under, a faint sheen drifting across
        vec2 p = c * 9.0, ip = floor(p), f = fract(p);
        bool hOver = mod(ip.x + ip.y, 2.0) < 0.5;
        float wh = abs(f.y - 0.5), wv = abs(f.x - 0.5);
        float inH = 1.0 - smoothstep(0.36, 0.42, wh), inV = 1.0 - smoothstep(0.36, 0.42, wv);
        // The strand on top is rounded across its width and dips at both ends where it goes under.
        float topH = inH * (hOver ? 1.0 : 1.0 - inV), topV = inV * (hOver ? 1.0 - inH : 1.0);
        float shH = sqrt(max(1.0 - pow(wh / 0.42, 2.0), 0.0)) * (hOver ? 0.75 + 0.25 * sin(3.14159 * f.x) : 0.6);
        float shV = sqrt(max(1.0 - pow(wv / 0.42, 2.0), 0.0)) * (hOver ? 0.6 : 0.75 + 0.25 * sin(3.14159 * f.y));
        float sh = topH * shH + topV * shV;
        float cov = max(topH, topV);
        float sheen = 0.5 + 0.5 * sin(dot(c, vec2(1.0, 0.6)) * 1.3 - T * 0.08);
        vec3 col = mix(colB * 0.75, colA, topH / max(cov, 1e-3) * 0.5 + 0.25) * (0.55 + 0.45 * sh);
        return paleInk(col, cov * (0.1 + 0.16 * sh + 0.12 * sheen * sh) * amt);
    } else if (s == 24) { // halftone: a dot screen whose dots swell and shrink with a slow noise field
        vec2 r = mat2(0.7071, -0.7071, 0.7071, 0.7071) * c * 16.0;
        vec2 ip = floor(r), f = fract(r) - 0.5;
        vec2 cc = mat2(0.7071, 0.7071, -0.7071, 0.7071) * (ip + 0.5) / 16.0;
        float n = smoothstep(0.25, 0.8, fbm(cc * 1.4 + vec2(T * 0.012, T * 0.007)));
        float rad = 0.44 * sqrt(n);
        float d = length(f), fw = fwidth(r.x);
        float dot1 = 1.0 - smoothstep(rad - fw, rad + fw, d);
        return paleInk(mix(colB, colA, n), dot1 * amt * 0.36);
    } else if (s == 25) { // sand: fine wind ripples, gently wavy, slowly creeping
        vec2 p = c;
        float v = p.y * 18.0 + fbm(p * 1.2) * 4.0 + sin(p.x * 1.8 + fbm(p * 0.6) * 3.0) * 1.0 - T * 0.05;
        float s0 = fract(v), aa = clamp(fwidth(v) * 1.5, 0.0, 1.0);
        float lit = smoothstep(0.0, 0.72, s0) * (1.0 - smoothstep(0.72, 1.0, s0)); // long gentle face rising into the light, short steep face
        lit = mix(lit, 0.4, aa);
        float big = smoothstep(0.2, 0.75, fbm(p * 0.9 + vec2(T * 0.004, 0.0)));
        vec3 col = mix(uBottom * 0.6 + colB * 0.15, colA, lit);
        return paleInk(col, (0.12 + 0.1 * lit) * (0.6 + 0.4 * big) * amt);
    } else if (s == 26) { // brush: broad soft painterly strokes slowly appearing and fading away
        vec3 col = vec3(0.0);
        float a = 0.0;
        vec2 cp = c / S.z;
        for (int k = 0; k < 10; k++) {
            float fk = float(k);
            float tt = T * 0.02 + fk * 0.237, cyc = floor(tt), ph = fract(tt);
            vec2 hh = hash22(vec2(fk * 7.1, cyc));
            vec2 o = (hh - 0.5) * vec2(0.95 * uAspect, 0.9);
            float ang = (hash12(vec2(cyc, fk * 3.3)) - 0.5) * 1.6;
            vec2 q = mat2(cos(ang), sin(ang), -sin(ang), cos(ang)) * (cp - o);
            float L = 0.3 + 0.3 * hh.y, W = 0.08 + 0.06 * hh.x;
            float across = q.y + 0.12 * sin(q.x * 2.2 + fk) * L;
            float wid = W * (1.0 - 0.4 * pow(min(abs(q.x) / L, 1.0), 2.0));
            float ragged = L * 0.3 * (fbm(vec2(across * 30.0, fk)) - 0.5); // dry, uneven stroke ends
            float m = (1.0 - smoothstep(wid * 0.7, wid, abs(across))) * (1.0 - smoothstep(L * 0.6, L, abs(q.x) + ragged));
            float bristle = 0.6 + 0.4 * smoothstep(0.3, 0.7, fbm(vec2(q.x * 1.2 + fk * 5.0, across * 60.0)));
            float w = m * bristle * pow(sin(3.14159 * ph), 2.0) * 0.45;
            col += mix(colA, colB, fract(fk * 0.41 + cyc * 0.29)) * w;
            a += w;
        }
        return paleInk(col / max(a, 1e-3), min(a, 1.0) * amt);
    } else if (s == 27) { // prism: soft dispersion bands, the theme hue fanned out across each band, drifting
        float v = dot(c, normalize(vec2(1.0, 0.45))) * 1.3 + smoothNoise(c * 0.8 + vec2(T * 0.008, 0.0)) * 1.0 - T * 0.015;
        float x = fract(v) - 0.5; // position across a band
        float b = exp(-x * x * 40.0);
        float env = smoothstep(0.2, 0.7, smoothNoise(c * 0.6 - vec2(0.0, T * 0.006) + 3.0));
        return paleInk(hueTurn(colA * 1.2, x * 2.4), b * env * amt * 0.42);
    } else { // glass shards: large translucent polygons drifting and turning, their overlaps adding up
        float a = 0.0;
        vec3 col = vec3(0.0);
        for (int k = 0; k < 6; k++) {
            float fk = float(k);
            vec2 o = vec2(sin(T * 0.03 * (1.0 + fk * 0.21) + fk * 2.4) * 0.8 * uAspect, sin(T * 0.025 * (1.0 + fk * 0.17) + fk * 1.7) * 0.4);
            float ang = T * 0.05 * (mod(fk, 2.0) * 2.0 - 1.0) + fk;
            vec2 q = mat2(cos(ang), -sin(ang), sin(ang), cos(ang)) * (c / S.z - o);
            float d = polyDist(q, 3.0 + mod(fk, 3.0)) - (0.12 + 0.05 * mod(fk * 1.7, 3.0));
            float w = (1.0 - smoothstep(-0.004, 0.004, d)) * 0.32 + exp(-abs(d) * 200.0) * 0.5;
            a += w;
            col += mix(colA, colB, fract(fk * 0.37)) * w;
        }
        return vec4(col / max(a, 1e-3), min(a, 1.0) * amt);
    }
}

// Phase changes ripple outward from the board: k = how much of the new scene is shown here.
float wipeMix(float d) { return uWipe.x > 0.5 ? 1.0 - smoothstep(uWipe.y - uWipe.z, uWipe.y, d) : uMix; }
void main() {
    vec2 wq = vUV * vec2(uAspect, 1.0);
    float nz = uWipeShape == 24 ? fbm(wq * 1.8 + fbm(wq * 2.5 + uWipeSeed) * 1.6 + uWipeSeed)
                                : fbm(wq * (uWipeShape >= 22 ? 1.6 : 2.5) + uWipeSeed);
    nz = clamp((nz - 0.25) / 0.5, 0.0, 1.0);
    float d = wipeCoord(vUV * 2.0 - 1.0, uAspect, nz);
    float k = wipeMix(d);
    uTop = uTopA; uBottom = uBottomA; uGlow = uGlowA;
    vec3 a = bgStyle(uStyleA, vUV);
    vec4 sa = surface(uSurfA, vUV);
    a = mix(a, sa.rgb, clamp(sa.a, 0.0, 1.0));
    uTop = uTopB; uBottom = uBottomB; uGlow = uGlowB;
    vec3 b = bgStyle(uStyleB, vUV);
    vec4 sb = surface(uSurfB, vUV);
    b = mix(b, sb.rgb, clamp(sb.a, 0.0, 1.0));
    vec3 col = mix(a, b, k);
    vec3 glow = mix(uGlowA, uGlowB, k);
    // Energy glow rising from below the board: follows the song structure (calm = none, peak = strong).
    vec2 c = vec2((vUV.x - 0.5) * uAspect, vUV.y + 0.15);
    float rise = exp(-dot(c, c) * 2.2) * smoothstep(0.25, 1.1, uIntensity);
    col = mix(col, glow, rise * mix(0.45, 0.3, uPale));
    // Soft luminous band at the wave front.
    if (uWipe.x > 0.5) {
        float x = (d - (uWipe.y - uWipe.z * 0.5)) / (uWipe.z * 0.4);
        col = mix(col, uGlowB * 1.3, exp(-x * x) * uWipe.w * mix(0.22, 0.35, uPale));
    }
    // Slowly rotating light rays from behind the board.
    if (uRays.x + uRays.z > 0.001) {
        vec2 rc = vec2((vUV.x - 0.5) * uAspect, vUV.y - 0.52);
        float ang = atan(rc.y, rc.x), r = length(rc);
        float fall = smoothstep(0.06, 0.35, r) * exp(-r * 1.4);
        float ra = pow(max(0.0, sin(ang * uRays.y + uRayTime)), 6.0);
        float rb = pow(max(0.0, sin(ang * uRays.w - uRayTime * 0.8)), 6.0);
        col = mix(col, glow * 1.2, clamp((ra * uRays.x + rb * uRays.z) * fall, 0.0, 0.6));
    }
    col *= 1.0 + 0.08 * uBeat * (1.0 - uPale);
    fragColor = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- procedural particles
inline const char* PARTICLE_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aSeed;
uniform mat4 uVP, uView;
uniform float uTime, uSize, uBright, uCount, uAspect, uP11, uPixel, uIntensity, uDensity, uSide, uKick, uBoardDim;
uniform float uPale; // also read by the fragment shader
uniform vec4 uWipe;
uniform int uWipeShape;
uniform float uWipeSeed;
// Where the transition front reaches first (0) to last (1). Mirrors wipeCoord() in renderer.cpp.
float wipeCoord(vec2 ndc, float aspect, float noise) {
    float radial = length(ndc * vec2(aspect, 1.0)) / length(vec2(aspect, 1.0));
    float d;
    if (uWipeShape == 1) d = ndc.y * 0.5 + 0.5;
    else if (uWipeShape == 2) d = 0.5 - ndc.y * 0.5;
    else if (uWipeShape == 3) d = ndc.x * 0.5 + 0.5;
    else if (uWipeShape == 4) d = 0.5 - ndc.x * 0.5;
    else if (uWipeShape == 5) d = 1.0 - radial;
    else if (uWipeShape == 6) d = 1.0 - abs(ndc.x);
    else if (uWipeShape == 7) d = noise;
    else if (uWipeShape == 8) d = (ndc.x + ndc.y) * 0.25 + 0.5;
    else if (uWipeShape == 9) d = 0.6 * fract(atan(ndc.y, ndc.x * aspect) / 6.2831853 + 0.5) + 0.4 * radial;
    else if (uWipeShape == 10) d = (abs(ndc.x) + abs(ndc.y)) * 0.5;
    else if (uWipeShape == 11) d = (ndc.y * 0.5 + 0.5) * 0.85 + 0.075 * (1.0 + sin(ndc.x * 9.0));
    else if (uWipeShape == 12) d = (0.5 - ndc.y * 0.5) * 0.85 + 0.075 * (1.0 + sin(ndc.x * 7.0 + 1.0));
    else if (uWipeShape == 13) d = 0.35 * fract(ndc.y * 3.0) + 0.65 * (ndc.y * 0.25 + 0.5);
    else if (uWipeShape == 14) d = 0.35 * fract(ndc.x * 4.0) + 0.65 * (ndc.x * 0.25 + 0.5);
    else if (uWipeShape == 15) d = radial * (0.8 + 0.2 * cos(atan(ndc.y, ndc.x * aspect) * 5.0));
    else if (uWipeShape == 16) d = min(abs(ndc.x), abs(ndc.y));
    else if (uWipeShape == 17) d = min(abs(ndc.x - ndc.y), abs(ndc.x + ndc.y)) * 0.7;
    else if (uWipeShape == 18) d = 0.5 * mod(floor(ndc.x * 4.0) + floor(ndc.y * 3.0), 2.0) + 0.45 * radial;
    else if (uWipeShape == 19) d = length((ndc + 1.0) * vec2(aspect, 1.0)) / length(vec2(aspect, 1.0) * 2.0);
    else if (uWipeShape == 20) d = abs(ndc.y);
    else if (uWipeShape == 21) d = noise * 0.7 + 0.3 * fract(sin(dot(floor(ndc * 40.0), vec2(12.9898, 78.233))) * 43758.5453);
    else if (uWipeShape == 22) d = noise * 0.55 + (ndc.y * 0.5 + 0.5) * 0.45;
    else if (uWipeShape == 23) d = noise * 0.55 + radial * 0.45;
    else if (uWipeShape == 24) d = noise;
    else d = radial;
    return clamp(d, 0.0, 1.0);
}

uniform int uStyle, uShape;
uniform vec4 uP;
uniform float uBass, uMid, uHigh, uLoud, uBeat;
uniform vec2 uReact;
uniform float uSpec[16];
uniform vec3 uColA, uColB, uColC;
out vec2 vUV;
out vec3 vCol;
out float vAlpha;

const float PI = 3.14159265, TAU = 6.2831853;
vec3 rotX(vec3 p, float a) { float c = cos(a), s = sin(a); return vec3(p.x, c * p.y - s * p.z, s * p.y + c * p.z); }
vec3 rotY(vec3 p, float a) { float c = cos(a), s = sin(a); return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z); }
vec3 rotZ(vec3 p, float a) { float c = cos(a), s = sin(a); return vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z); }
float spec(float x) { int i = int(clamp(x, 0.0, 0.999) * 16.0); return uSpec[i]; }
float phash(float n) { return fract(sin(n * 12.9898 + 4.1414) * 43758.5453); }
// Half height of the view at a depth behind the board (the board fills ~23 units of height at z = 0).
float viewH(float depth) { return 11.7 + depth / uP11; }
float gWarm = 0.0; // lanterns: how much of a warm glow tints the dot
// Koi: a lazy, slowly wandering loop on one side of the board (never stopping: the body follows it).
vec2 koiPath(float th, float tau, float ph, float side, float hw, float hh) {
    vec2 c = vec2(side * (0.7 + 0.04 * sin(0.07 * tau + ph)) * hw, 0.22 * hh * sin(0.05 * tau + ph * 1.3));
    vec2 r = vec2((0.3 + 0.04 * sin(0.11 * tau + ph)) * hh, (0.32 + 0.06 * sin(0.09 * tau + ph * 0.7)) * hh);
    return c + vec2(cos(th), sin(th) + 0.2 * sin(2.0 * th + ph)) * r;
}

vec3 stylePos(vec4 s, float t, out float bright, out float cm, out float sz) {
    float bass = uBass * uReact.x;
    bright = 1.0; cm = s.w; sz = 1.0;
    if (uStyle == 0) { // galaxy
        float arms = 2.0 + floor(uP.x * 4.0);
        float r = 5.0 + 70.0 * pow(s.x, 0.85);
        float arm = floor(s.y * arms);
        float ang = arm / arms * TAU + r * (0.03 + 0.07 * uP.y) - t * 0.06 * (1.0 + 18.0 / (r + 4.0))
                    + (s.z - 0.5) * (0.3 + 0.012 * r);
        float h = (s.w - 0.5) * (1.5 + 7.0 * exp(-r / 14.0));
        r *= 1.0 + 0.04 * bass;
        vec3 p = vec3(cos(ang) * r, h, sin(ang) * r);
        p = rotX(p, mix(0.2, 1.3, uP.z));
        p = rotZ(p, (uP.w - 0.5) * 0.8);
        bright = 0.35 + 0.8 * exp(-r / 25.0);
        cm = s.x;
        return p + vec3(0.0, 0.0, -40.0);
    } else if (uStyle == 1) { // tunnel
        float ring = floor(s.y * 30.0);
        float depth = uP.w > 0.5 ? fract(ring / 30.0 + t * 0.05) : fract(s.y + t * 0.05);
        float ang = s.x * TAU + t * 0.05 * (uP.y - 0.5) + depth * (uP.y - 0.5) * 3.0;
        float R = 17.0 + 10.0 * uP.z + 3.0 * bass;
        if (uP.x > 0.35) {
            float n = 3.0 + floor((uP.x - 0.35) / 0.65 * 5.0);
            R *= cos(PI / n) / cos(mod(ang, TAU / n) - PI / n);
        }
        bright = smoothstep(0.0, 0.35, depth) * (1.0 - smoothstep(0.8, 1.0, depth));
        cm = fract(ring * 0.137);
        return vec3(cos(ang) * R, sin(ang) * R, mix(-170.0, 25.0, depth));
    } else if (uStyle == 2) { // ocean grid
        float N = floor(sqrt(uCount));
        float i = float(gl_InstanceID);
        float u = mod(i, N) / (N - 1.0), v = floor(i / N) / (N - 1.0);
        float x = (u - 0.5) * 190.0, z = 18.0 - v * 170.0;
        float amp = 1.0 + 2.0 * uP.y;
        float y = -17.0 - 6.0 * uP.x + (sin(x * 0.05 + t * 0.6) + sin(z * 0.07 - t * 0.8 + x * 0.02)) * amp
                  + spec(abs(u - 0.5) * 2.0) * 5.0 * uReact.x * (1.0 - 0.6 * v);
        bright = (1.0 - smoothstep(0.3, 1.0, v)) * (0.5 + 0.5 * spec(abs(u - 0.5) * 2.0));
        cm = v;
        sz = 0.8;
        return vec3(x, y, z);
    } else if (uStyle == 3) { // sphere shell
        float z = s.x * 2.0 - 1.0;
        if (uP.z > 0.5) z = (floor(s.x * 26.0) + 0.5) / 26.0 * 2.0 - 1.0; // latitude rings
        float a = s.y * TAU;
        vec3 d = vec3(sqrt(1.0 - z * z) * cos(a), z, sqrt(1.0 - z * z) * sin(a));
        float R = 26.0 + 14.0 * uP.x;
        R *= 1.0 + 0.06 * bass + 0.015 * sin(t + s.z * 6.0);
        vec3 p = rotY(rotX(d, uP.y * 1.5), t * 0.05) * R;
        bright = 0.35 + 0.65 * (0.5 + 0.5 * p.z / R);
        cm = s.z;
        return p + vec3(0.0, 0.0, -48.0);
    } else if (uStyle == 4) { // drift (snow / embers)
        float dir = uP.x < 0.5 ? -1.0 : 1.0;
        float x = (s.x - 0.5) * 130.0 + sin(t * 0.3 + s.w * 20.0) * 3.0 * (uP.y + 0.2);
        float y = mod(s.y * 90.0 + dir * t * (1.5 + 3.5 * s.w), 90.0) - 45.0;
        float z = -6.0 - s.z * 90.0;
        bright = (1.0 - smoothstep(32.0, 45.0, abs(y))) * (0.4 + 0.6 * s.w) * (0.8 + 0.2 * uHigh * uReact.y);
        sz = 0.5 + s.w;
        return vec3(x, y, z);
    } else if (uStyle == 5) { // flowing streams
        float K = 3.0 + floor(uP.x * 6.0);
        float k = floor(s.x * K);
        float u = fract(s.y + t * 0.025 * (0.6 + fract(k * 0.37)));
        float ph = k * 1.7 + uP.y * 6.0;
        float a = u * TAU;
        float m = 1.0 + floor(uP.z * 3.0);
        vec3 p = vec3(sin(a * m + ph) * 45.0, sin(a * 2.0 + ph * 1.3) * 18.0 + cos(a * 3.0 + ph) * 6.0,
                      cos(a + ph * 0.7) * 30.0 - 45.0);
        p += (vec3(s.z, s.w, fract(s.z * 7.0)) - 0.5) * (1.5 + 3.0 * bass);
        bright = 0.5 + 0.5 * sin(u * TAU * 3.0 + t);
        cm = k / K;
        return p;
    } else if (uStyle == 6) { // warp
        float ang = s.x * TAU;
        float rad = 7.0 + 65.0 * pow(s.z, 0.6);
        float depth = fract(s.y + t * 0.2 * (0.5 + s.w));
        bright = smoothstep(0.0, 0.5, depth) * (1.0 - smoothstep(0.9, 1.0, depth));
        cm = s.z;
        return vec3(cos(ang) * rad, sin(ang) * rad, mix(-220.0, 25.0, depth));
    } else if (uStyle == 7) { // curtains
        float ci = floor(s.x * 3.0);
        float x = (s.y - 0.5) * 220.0;
        float z = -55.0 - ci * 25.0 + sin(x * 0.03 + t * 0.2 + ci) * 15.0;
        float h = s.z;
        float y = 2.0 + uP.x * 10.0 + h * 32.0 * (0.6 + 0.4 * sin(x * 0.045 + t * 0.3 + ci * 2.0));
        bright = pow(1.0 - h, 1.5) * (0.4 + 0.6 * (0.5 + 0.5 * sin(x * 0.08 + t * 0.7 + ci)));
        cm = h;
        sz = 0.8;
        return vec3(x, y, z);
    } else if (uStyle == 8) { // halos (spectrum rings)
        float R = 3.0 + floor(uP.x * 8.0);
        float ring = floor(s.x * R);
        float dir = mod(ring, 2.0) * 2.0 - 1.0;
        float ang = s.y * TAU + t * 0.08 * dir * (0.5 + ring * 0.1);
        float sp = spec(ring / R);
        float rad = 13.0 + ring * (4.0 + 4.0 * uP.y) + sp * 3.0 * uReact.x;
        vec3 p = vec3(cos(ang) * rad, sin(ang) * rad, 0.0);
        p = rotX(p, (uP.z - 0.5) * 2.2);
        bright = 0.35 + 0.8 * sp;
        cm = ring / R;
        return p + vec3(0.0, 0.0, -22.0 - ring * 1.5);
    } else if (uStyle == 9) { // helix
        float strands = 2.0 + floor(uP.x * 3.0);
        float st = floor(s.x * strands);
        float y = (fract(s.y + t * 0.02) - 0.5) * 130.0;
        float ang = y * (0.06 + 0.1 * uP.y) + st * TAU / strands + t * 0.2;
        float rad = 17.0 + 9.0 * uP.z + 2.0 * bass;
        vec3 p = vec3(cos(ang) * rad, y, sin(ang) * rad) + (vec3(s.z, s.w, fract(s.w * 9.0)) - 0.5) * 1.2;
        if (uP.w > 0.5) p = vec3(p.y, p.x * 0.6, p.z);
        bright = (1.0 - smoothstep(40.0, 65.0, abs(y)));
        cm = st / strands;
        return p + vec3(0.0, 0.0, -28.0);
    } else if (uStyle == 10) { // bokeh
        vec3 p = vec3((s.x - 0.5) * 150.0, (s.y - 0.5) * 95.0, -12.0 - s.z * 85.0);
        p += vec3(sin(t * 0.1 + s.w * 20.0) * 4.0, cos(t * 0.13 + s.w * 10.0) * 3.0, 0.0);
        sz = 2.5 + 5.0 * s.w;
        bright = 0.12 + 0.1 * uMid * uReact.y;
        return p;
    } else if (uStyle == 11) { // lattice
        float n = floor(pow(uCount, 1.0 / 3.0));
        float i = float(gl_InstanceID);
        vec3 g = vec3(mod(i, n), mod(floor(i / n), n), floor(i / (n * n))) - (n - 1.0) * 0.5;
        float spacing = 9.0 + 5.0 * uP.x;
        vec3 p = g * spacing;
        p = rotY(rotX(p, uP.y * 0.8), t * 0.03);
        float d = length(p);
        bright = 0.3 + 0.6 * pow(0.5 + 0.5 * sin(d * 0.08 - t * 0.5), 3.0) * (0.5 + uLoud);
        cm = fract(d * 0.01);
        sz = 0.8;
        return p + vec3(0.0, 0.0, -50.0);
    } else if (uStyle == 12) { // fireflies: slow wandering glows
        vec3 base = vec3((s.x - 0.5) * 100.0, (s.y - 0.5) * 60.0, -8.0 - s.z * 55.0);
        float tt = t * 0.15 + s.w * 20.0;
        bright = 0.35 + 0.65 * (0.5 + 0.5 * sin(t * 0.6 + s.w * 30.0));
        sz = 0.9 + 0.6 * s.w;
        return base + vec3(sin(tt * 1.3 + s.y * 7.0) * 5.0, cos(tt * 1.1 + s.x * 5.0) * 3.5, sin(tt * 0.7) * 3.0);
    } else if (uStyle == 13) { // slanted rain
        float slant = (uP.x - 0.5) * 0.8;
        float y = mod(s.y * 110.0 - t * (18.0 + 14.0 * s.w), 110.0) - 55.0;
        bright = 0.3 + 0.3 * s.w;
        cm = s.w;
        return vec3((s.x - 0.5) * 150.0 + y * slant, y, -6.0 - s.z * 80.0);
    } else if (uStyle == 14) { // vortex pulling inward
        float k = fract(s.x + t * 0.02 * (0.5 + s.w));
        float r = 3.0 + 65.0 * (1.0 - k);
        float ang = s.y * TAU + t * 0.1 + 25.0 / r * (uP.x > 0.5 ? 1.0 : -1.0);
        bright = smoothstep(3.0, 12.0, r) * (1.0 - smoothstep(45.0, 68.0, r));
        cm = k;
        vec3 p = vec3(cos(ang) * r, sin(ang) * r, 0.0);
        p = rotX(p, 0.3 + uP.y * 0.9);
        return p + vec3(0.0, 0.0, -35.0);
    } else if (uStyle == 15) { // waveform lines following the spectrum
        float u = float(gl_InstanceID) / max(uCount, 1.0);
        float layer = floor(s.y * 3.0);
        float x = (fract(u * 3.0) - 0.5) * 170.0;
        float amp = spec(abs(fract(u * 3.0) - 0.5) * 2.0) * (5.0 + 5.0 * uP.x);
        float y = -6.0 + (layer - 1.0) * 9.0 * (0.6 + uP.y) + amp * sin(x * 0.12 + t * 1.5 + layer * 2.0);
        bright = 0.4 + 0.6 * amp / 10.0;
        cm = layer / 2.0;
        sz = 0.7;
        return vec3(x, y, -28.0 - layer * 12.0);
    } else if (uStyle == 16) { // starburst rays flowing out
        float rays = 10.0 + floor(uP.x * 18.0);
        float ray = floor(s.x * rays);
        float k = fract(s.y + t * 0.08 * (0.6 + s.w));
        float d = 5.0 + k * 75.0;
        float ang = ray / rays * TAU + t * 0.015 + (s.z - 0.5) * 0.04;
        bright = smoothstep(5.0, 12.0, d) * (1.0 - k);
        cm = k;
        return vec3(cos(ang) * d, sin(ang) * d * 0.9, -32.0);
    } else if (uStyle == 17) { // orbits with bodies
        float n = 3.0 + floor(uP.x * 4.0);
        float o = floor(s.x * n);
        float R = 12.0 + o * (6.0 + 5.0 * uP.y);
        bool body = s.w > 0.985;
        float ang = s.y * TAU + t * (body ? 0.5 : 0.1) / (o + 1.0);
        vec3 p = vec3(cos(ang) * R, 0.0, sin(ang) * R);
        p = rotX(p, 1.1 + uP.z * 0.4 + o * 0.08);
        bright = body ? 1.0 : 0.3;
        sz = body ? 3.0 : 0.6;
        cm = o / n;
        return p + vec3(0.0, 0.0, -30.0);
    } else if (uStyle == 18) { // confetti tumbling down
        float x = (s.x - 0.5) * 130.0 + sin(t * 0.4 + s.w * 20.0) * 4.0;
        float y = mod(s.y * 90.0 - t * (2.0 + 2.5 * s.w), 90.0) - 45.0;
        bright = (1.0 - smoothstep(32.0, 45.0, abs(y))) * 0.8;
        sz = 0.7 + 0.7 * abs(sin(t * 0.9 + s.w * 20.0)); // tumbling
        cm = s.w;
        return vec3(x, y, -6.0 - s.z * 70.0);
    } else if (uStyle == 19) { // snowglobe: slow swirl inside a sphere
        float z = s.x * 2.0 - 1.0, a = s.y * TAU + t * (0.15 + 0.2 * s.w) * (1.0 - abs(z));
        float R = 24.0 * pow(s.z, 0.33);
        vec3 p = vec3(sqrt(1.0 - z * z) * cos(a), z, sqrt(1.0 - z * z) * sin(a)) * R;
        bright = 0.5 + 0.5 * s.w;
        cm = s.z;
        return rotX(p, uP.x * 0.6) + vec3(0.0, 0.0, -45.0);
    } else if (uStyle == 20) { // ladder: two strands with rungs
        float y = (fract(s.y + t * 0.015) - 0.5) * 130.0;
        float ang = y * (0.05 + 0.06 * uP.y) + t * 0.25;
        float rung = s.x < 0.3 ? 1.0 : 0.0;
        float side = rung > 0.5 ? (s.z * 2.0 - 1.0) : (s.x < 0.65 ? -1.0 : 1.0);
        if (rung > 0.5) y = floor(y / 4.0) * 4.0;
        vec3 p = vec3(cos(ang) * 16.0 * side, y, sin(ang) * 16.0 * side);
        if (uP.w > 0.5) p = vec3(p.y, p.x * 0.6, p.z);
        bright = (rung > 0.5 ? 0.45 : 0.9) * (1.0 - smoothstep(40.0, 65.0, abs(y)));
        cm = rung > 0.5 ? 0.5 : (side > 0.0 ? 0.0 : 1.0);
        return p + vec3(0.0, 0.0, -30.0);
    } else if (uStyle == 21) { // fountains arcing up from both bottom sides
        float k = fract(s.y + t * 0.25 * (0.6 + 0.4 * s.w));
        float side = s.x < 0.5 ? -1.0 : 1.0;
        float vx = (0.3 + 0.9 * s.z) * 14.0 * -side, vy = 34.0 + 12.0 * s.w;
        vec3 p = vec3(side * (26.0 + 8.0 * uP.x) + vx * k, -30.0 + vy * k - 30.0 * k * k, -18.0 - 10.0 * s.w);
        bright = (1.0 - k) * smoothstep(0.0, 0.08, k);
        cm = k;
        sz = 0.8;
        return p;
    } else if (uStyle == 22) { // petals drifting sideways
        float x = mod(s.x * 170.0 + t * (4.0 + 3.0 * s.w), 170.0) - 85.0;
        float y = (s.y - 0.5) * 80.0 + sin(t * 0.5 + s.w * 20.0 + x * 0.05) * 6.0 - t * 1.2 * s.z;
        y = mod(y + 40.0, 80.0) - 40.0;
        sz = 0.9 + 0.5 * abs(sin(t * 0.7 + s.w * 30.0));
        bright = 0.7;
        cm = s.w;
        return vec3(x, y, -8.0 - s.z * 60.0);
    } else if (uStyle == 23) { // constellation: clusters of stars, gently breathing
        float c = floor(s.x * 14.0);
        vec3 center = vec3(sin(c * 12.9) * 60.0, cos(c * 7.3) * 32.0, -30.0 - fract(c * 0.37) * 50.0);
        vec3 p = center + (vec3(s.y, s.z, s.w) - 0.5) * vec3(12.0, 8.0, 6.0);
        bright = 0.4 + 0.6 * pow(s.w, 3.0) * (0.7 + 0.3 * sin(t * 0.4 + c));
        sz = 0.6 + 1.4 * pow(s.w, 4.0);
        cm = fract(c * 0.618);
        return p;
    } else if (uStyle == 24) { // torus shell, slowly rolling
        float u = s.x * TAU + t * 0.05, v = s.y * TAU;
        float R = 22.0 + 6.0 * uP.x, r = 6.0 + 3.0 * uP.y;
        vec3 p = vec3((R + r * cos(v)) * cos(u), r * sin(v), (R + r * cos(v)) * sin(u));
        p = rotX(p, 0.9 + uP.z * 0.5 + 0.1 * sin(t * 0.1));
        bright = 0.35 + 0.65 * (0.5 + 0.5 * cos(v));
        cm = s.y;
        return p + vec3(0.0, 0.0, -40.0);
    } else if (uStyle == 25) { // wall of dots behind the board, waving with the spectrum
        float N = floor(sqrt(uCount));
        float i = float(gl_InstanceID);
        float u = mod(i, N) / (N - 1.0), v = floor(i / N) / (N - 1.0);
        float z = -45.0 + sin(u * 8.0 + t * 0.6) * 3.0 + spec(abs(u - 0.5) * 2.0) * 6.0 * uReact.x;
        bright = 0.12 + 0.28 * spec(v); // faint: a regular grid catches the eye, it must stay in the background
        cm = v;
        sz = 0.8;
        return vec3((u - 0.5) * 150.0, (v - 0.5) * 90.0, z);
    } else if (uStyle == 26) { // comets: few bright heads crossing slowly
        float k = fract(s.y + t * 0.03 * (0.5 + s.w));
        float ang = s.x * TAU;
        vec3 dir = vec3(cos(ang), sin(ang) * 0.5, 0.0);
        vec3 p = dir * (k - 0.5) * 180.0 + vec3(0.0, (s.z - 0.5) * 60.0, -30.0 - s.w * 40.0);
        bright = sin(k * PI) * (s.w > 0.7 ? 1.0 : 0.3);
        sz = s.w > 0.7 ? 1.6 : 0.6;
        cm = s.w;
        return p;
    } else if (uStyle == 27) { // sparklers at both sides of the board
        float k = fract(s.y + t * 0.6 * (0.5 + s.w));
        float side = s.x < 0.5 ? -1.0 : 1.0;
        float ang = s.z * TAU;
        vec3 p = vec3(side * 12.0, -2.0 + (uP.x - 0.5) * 10.0, -4.0) + vec3(cos(ang), sin(ang), 0.3 * sin(ang * 3.0)) * k * (5.0 + 5.0 * s.w);
        p.y -= 6.0 * k * k;
        bright = (1.0 - k) * (0.5 + 0.5 * uHigh);
        sz = 0.5;
        cm = k;
        return p;
    } else if (uStyle == 28) { // bubbles rising
        float y = mod(s.y * 100.0 + t * (3.0 + 4.0 * s.w), 100.0) - 50.0;
        float x = (s.x - 0.5) * 140.0 + sin(t * 0.6 + s.w * 20.0 + y * 0.1) * 2.0;
        sz = 1.2 + 2.2 * s.z;
        bright = 0.35 * (1.0 - smoothstep(35.0, 50.0, abs(y)));
        cm = s.z;
        return vec3(x, y, -8.0 - s.w * 70.0);
    } else if (uStyle == 29) { // points along the edges of a slowly turning cube
        float e = floor(s.x * 12.0);
        float axis = floor(e / 4.0), corner = mod(e, 4.0);
        vec2 cc = vec2(mod(corner, 2.0), floor(corner / 2.0)) * 2.0 - 1.0;
        vec3 p = axis < 0.5 ? vec3(s.y * 2.0 - 1.0, cc.x, cc.y) : (axis < 1.5 ? vec3(cc.x, s.y * 2.0 - 1.0, cc.y) : vec3(cc.x, cc.y, s.y * 2.0 - 1.0));
        p *= 18.0 + 6.0 * uP.x;
        p = rotY(rotX(p, t * 0.04 + uP.y), t * 0.06);
        bright = 0.5 + 0.5 * uLoud;
        cm = e / 12.0;
        return p + vec3(0.0, 0.0, -45.0);
    } else if (uStyle == 30) { // infinity loop stream
        float u = fract(s.x + t * 0.03) * TAU;
        float dnm = 1.0 + sin(u) * sin(u);
        vec3 p = vec3(cos(u) / dnm, sin(u) * cos(u) / dnm, 0.0) * (40.0 + 10.0 * uP.x);
        p += (vec3(s.y, s.z, s.w) - 0.5) * (2.0 + 3.0 * uBass);
        bright = 0.6 + 0.4 * sin(u * 3.0 + t);
        cm = fract(s.x + t * 0.03);
        return rotX(p, (uP.y - 0.5) * 1.2) + vec3(0.0, 0.0, -35.0);
    } else if (uStyle == 31) { // rings expanding outward on a steady pulse
        float k = fract(s.y + t * 0.15);
        float ring = floor(s.y * 6.0);
        float r = 6.0 + fract(ring / 6.0 + t * 0.15) * 60.0;
        float a = s.x * TAU;
        float ph = ring * 2.17;
        // Organic rings: a slowly turning wobble, a fuzzy width and a brightness that comes and goes along them.
        float wob = 1.0 + 0.07 * sin(3.0 * a + ph + t * 0.35) + 0.04 * sin(5.0 * a - ph * 1.3 - t * 0.5);
        float rr = r * wob + (s.z - 0.5) * (1.0 + r * 0.05) + (s.w - 0.5) * (s.w - 0.5) * 6.0;
        float along = 0.35 + 0.65 * smoothstep(-0.4, 0.6, sin(2.0 * a + ph * 1.7 + t * 0.25) + 0.5 * sin(7.0 * a - ph));
        bright = (1.0 - smoothstep(20.0, 66.0, r)) * (0.5 + 0.5 * uBeat) * along * (0.6 + 0.4 * s.z);
        cm = r / 66.0 + 0.1 * sin(a * 2.0 + ph);
        vec2 c = vec2(sin(ph), cos(ph * 1.3)) * r * 0.04;
        return vec3(c.x + cos(a) * rr, c.y + sin(a) * rr * 0.85, -25.0);
    } else if (uStyle == 32) { // plasma field: grid colored by interfering waves
        float N = floor(sqrt(uCount));
        float i = float(gl_InstanceID);
        vec2 g = vec2(mod(i, N), floor(i / N)) / (N - 1.0) - 0.5;
        float v = sin(g.x * 10.0 + t * 0.5) + sin(g.y * 8.0 - t * 0.4) + sin((g.x + g.y) * 7.0 + t * 0.3);
        bright = 0.2 + 0.6 * (0.5 + 0.5 * sin(v * 1.5));
        cm = 0.5 + 0.5 * sin(v);
        sz = 1.4;
        return vec3(g.x * 150.0, g.y * 90.0, -50.0);
    } else if (uStyle == 33) { // moire: two offset rotating ring families
        float fam = s.x < 0.5 ? -1.0 : 1.0;
        float ring = floor(s.y * 16.0);
        float a = s.z * TAU + t * 0.04 * fam;
        vec3 c = vec3(fam * (4.0 + 3.0 * sin(t * 0.1)), 0.0, 0.0);
        float r = 4.0 + ring * 4.0;
        bright = 0.35;
        cm = ring / 16.0;
        return c + vec3(cos(a) * r, sin(a) * r, -30.0 - fam * 2.0);
    } else if (uStyle == 34) { // swarm flowing through a slowly changing field
        vec3 p = (vec3(s.x, s.y, s.z) - 0.5) * vec3(110.0, 60.0, 40.0);
        for (int k = 0; k < 3; k++) {
            p += vec3(sin(p.y * 0.05 + t * 0.2 + s.w), cos(p.x * 0.04 - t * 0.15), sin(p.x * 0.03 + p.y * 0.03)) * 6.0;
        }
        bright = 0.5 + 0.3 * uMid;
        cm = s.w;
        return p + vec3(0.0, 0.0, -40.0);
    } else if (uStyle == 35) { // logarithmic spirals, turning
        float arms = 3.0 + floor(uP.x * 4.0);
        float arm = floor(s.x * arms);
        float th = s.y * 4.0 * PI;
        float r = 3.0 * exp(0.22 * th);
        float a = th + arm / arms * TAU + t * 0.08;
        bright = smoothstep(3.0, 8.0, r) * (1.0 - smoothstep(50.0, 75.0, r));
        cm = s.y;
        return vec3(cos(a) * r, sin(a) * r, -35.0);
    } else if (uStyle == 36) { // one wide flowing ribbon
        float x = (s.x - 0.5) * 180.0;
        float w = (s.y - 0.5) * 8.0;
        float y = sin(x * 0.035 + t * 0.4) * 14.0 + cos(x * 0.02 - t * 0.3) * 6.0 + w;
        bright = (1.0 - abs(s.y - 0.5) * 2.0) * 0.8;
        cm = s.x;
        return vec3(x, y, -30.0 + sin(x * 0.02 + t * 0.2) * 8.0);
    } else if (uStyle == 38) { // skyline: dotted equalizer columns across the far back (lows at the edges)
        float R = max(floor(uCount / 64.0), 1.0);
        uint id = (uint(gl_InstanceID) * 7919u) % uint(64.0 * R); // scrambled: sparse scenes thin out evenly
        float col = float(id % 64u), row = float(id / 64u);
        float k = mod(col, 32.0);
        float side = col < 32.0 ? -1.0 : 1.0;
        float h = row / R * 60.0;
        float level = 3.0 + 42.0 * spec(1.0 - k / 31.0) + 1.5 * sin(t * 0.5 + k * 0.7);
        float lit = 1.0 - smoothstep(level - 2.0, level, h);
        float cap = exp(-pow((h - level) / 1.2, 2.0));
        bright = 0.32 * lit * (0.5 + 0.5 * h / max(level, 1.0)) + 0.3 * cap;
        cm = h / 60.0;
        sz = 1.1;
        return vec3(side * (3.0 + (k + 0.5) * 1.8), -32.0 + h, -55.0);
    } else if (uStyle == 39) { // corona: rays of dots around the board, their length following the spectrum
        float rays = 72.0 + floor(uP.x * 3.0) * 24.0;
        float ray = floor(s.x * rays);
        float ph = (ray + 0.5) / rays;
        float a = ph * TAU - 0.5 * PI + t * 0.02;
        float L = 3.0 + 24.0 * spec(1.0 - abs(ph * 2.0 - 1.0)); // lows at the bottom, highs at the top
        float d = s.y * 30.0;
        float r = 19.0 + 4.0 * uP.y + d;
        bright = (1.0 - smoothstep(L - 2.0, L, d)) * (0.12 + 0.3 * d / L);
        cm = d / 30.0;
        sz = 0.8;
        return vec3(cos(a) * r, sin(a) * r, -40.0);
    } else if (uStyle == 40) { // dunes: a dotted ground whose ridges follow the spectrum, rolling toward the viewer
        float N = floor(sqrt(uCount));
        float i = float(gl_InstanceID);
        float u = mod(i, N) / (N - 1.0), v = floor(i / N) / (N - 1.0);
        float x = (u - 0.5) * 220.0, z = 5.0 - v * 140.0;
        float sp = spec(min(abs(u - 0.5) * 2.2, 1.0)); // lows in the middle, highs toward the sides
        float roll = 0.5 + 0.5 * sin(z * 0.09 + t * 0.9 + sin(x * 0.03) * 1.5);
        float y = -15.0 - 4.0 * uP.x + 14.0 * sp * roll;
        bright = (0.1 + 0.4 * roll * sp) * smoothstep(-135.0, -85.0, z);
        cm = sp;
        sz = 0.9;
        return vec3(x, y, z);
    } else if (uStyle == 41) { // wind: streaks blowing sideways in slow gusts, along gently curving paths
        float dir = uP.x < 0.5 ? -1.0 : 1.0;
        float x = mod(s.x * 200.0 + dir * (t * (14.0 + 16.0 * s.w) + 8.0 * sin(t * 0.23 + s.z * 2.0)), 200.0) - 100.0;
        float y = (s.y - 0.5) * 80.0 + sin(x * 0.035 + t * 0.4 + s.w * 6.0) * (3.0 + 4.0 * uP.y);
        bright = (1.0 - smoothstep(65.0, 100.0, abs(x))) * (0.2 + 0.35 * s.w);
        cm = s.w;
        sz = 0.6;
        return vec3(x, y, -8.0 - s.z * 80.0);
    } else if (uStyle == 42) { // fire: embers rising from the bottom, flickering, swaying, fading as they climb
        float k = fract(s.y + t * (0.08 + 0.1 * s.w));
        float x = (s.x - 0.5) * 150.0 + sin(k * 7.0 + t * 1.3 + s.w * 20.0) * k * (2.0 + 3.0 * uP.x);
        float depth = 8.0 + s.z * 70.0;
        float base = -0.4 * (28.0 + depth);              // just above the bottom of the view at that depth
        float y = base + k * k * (30.0 + 25.0 * s.w);    // dense at the base, sparse sparks higher up
        bright = pow(1.0 - k, 1.3) * (0.65 + 0.3 * sin(t * 6.0 + s.z * 40.0)) * (0.7 + 0.3 * s.w);
        cm = k;
        sz = 2.2 - 1.7 * k;
        return vec3(x, y, -depth);
    } else if (uStyle == 43) { // sea seen through a window: water filling the bottom, its surface waving, its level slowly rising and falling
        float N = floor(sqrt(uCount));
        float i = float(gl_InstanceID);
        // v = 1 at the surface for the first instances: sparse scenes drop deep rows, never the surface.
        float u = mod(i, N) / (N - 1.0), v = 1.0 - floor(i / N) / (N - 1.0);
        float x = (u - 0.5) * 170.0 + (s.x - 0.5) * 1.5; // jittered: no visible columns
        float level = -15.0 + 5.0 * sin(t * 0.06 + uP.x * 6.0) + 3.0 * uP.y;
        float surf = level + 2.5 * sin(x * 0.05 + t * 0.5) + 1.2 * sin(x * 0.11 - t * 0.37) + 0.6 * sin(x * 0.23 + t * 0.8);
        float y = mix(-48.0, surf, pow(v, 0.7)); // rows gather toward the surface
        float top = exp(-(1.0 - v) * 14.0);      // bright crest line
        bright = 0.12 + 0.2 * v + 0.5 * top;
        cm = v;
        sz = 1.1 + 0.5 * top;
        return vec3(x + sin(y * 0.3 + t * 0.7) * 0.4, y, -35.0);
    } else if (uStyle == 44) { // fireworks: slow soft bursts blooming and fading here and there, sparks sinking
        float b = floor(s.x * 5.0);
        float ct = t / 10.0 + fract(b * 0.618 + uP.x);
        float cyc = floor(ct), k = fract(ct);
        float hs = cyc * 7.31 + b * 3.17;
        float side = mod(b + cyc, 2.0) * 2.0 - 1.0;
        float depth = 35.0 + 35.0 * phash(hs + 1.0);
        float hh = viewH(depth), hw = hh * uAspect;
        vec3 c = vec3(side * mix(0.5, 0.78, phash(hs + 2.0)) * hw, mix(-0.1, 0.55, phash(hs + 3.0)) * hh, -depth);
        // Sparks on rays spread evenly over a sphere; each ray is a head followed by a fading trail of dots.
        float NR = 44.0;
        float ray = floor(s.y * NR);
        float z = 1.0 - 2.0 * (ray + 0.5) / NR, a = ray * 2.39996 + hs;
        vec3 dir = vec3(sqrt(1.0 - z * z) * cos(a), z, sqrt(1.0 - z * z) * sin(a));
        float lag = s.z * s.z;
        float kk = max(k - lag * 0.12, 0.0);
        float R = (0.22 + 0.12 * phash(hs + 4.0)) * hh * (0.92 + 0.08 * fract(ray * 0.618));
        vec3 p = c + dir * R * (1.0 - exp(-kk * 4.5));
        p.y -= 0.3 * hh * kk * kk;
        bright = smoothstep(0.0, 0.15, k) * pow(1.0 - k, 1.4) * pow(1.0 - s.z, 2.0) * (0.85 + 0.25 * uHigh * uReact.y);
        sz = (1.0 - 0.4 * k) * (1.0 - 0.5 * s.z);
        cm = 0.7 * fract(phash(hs + 5.0) + 0.25 * k + 0.15 * s.z);
        return p;
    } else if (uStyle == 45) { // jellyfish: pulsing bells drifting upward, trailing tentacles of dots
        float j = floor(s.x * 4.0);
        float depth = 30.0 + 24.0 * fract(j * 0.37 + uP.x);
        float hh = viewH(depth), hw = hh * uAspect;
        float rise = fract(t * 0.011 * (0.8 + 0.4 * fract(j * 0.61)) + j * 0.27 + uP.y);
        float side = mod(j, 2.0) * 2.0 - 1.0;
        vec3 c = vec3(side * (0.38 + 0.38 * fract(j * 0.43 + uP.z)) * hw + 3.0 * sin(t * 0.13 + j * 2.0),
                      mix(-1.6 * hh, 1.3 * hh, rise), -depth);
        float pulse = 0.5 + 0.5 * sin(t * 0.85 + j * 1.9);
        pulse = pulse * pulse * (3.0 - 2.0 * pulse); // 1 open, 0 contracted
        float R = (0.17 + 0.05 * fract(j * 0.71)) * hh;
        float polMax = mix(1.05, 1.4, pulse);
        float wide = mix(0.82, 1.08, pulse);
        vec3 lp;
        if (s.y < 0.55) { // the bell, its rim brighter
            float v = sqrt(s.z);
            float pol = v * polMax, az = s.w * TAU;
            lp = vec3(sin(pol) * cos(az) * R * wide, cos(pol) * R * mix(1.0, 0.7, pulse), sin(pol) * sin(az) * R * wide);
            bright = 0.3 + 0.55 * smoothstep(0.75, 1.0, v);
            cm = 0.15 + 0.35 * v;
        } else { // tentacles hanging from the rim, waving behind the pulse
            float ti = floor(s.z * 9.0);
            float u = fract(s.z * 9.0) * (0.75 + 0.25 * fract(ti * 0.37)), az = ti / 9.0 * TAU + 0.3;
            float rr = sin(polMax) * R * wide * (1.0 - 0.35 * u);
            float L = R * (2.4 + 0.8 * fract(ti * 0.53)) * (0.95 + 0.1 * (1.0 - pulse));
            float rimY = cos(polMax) * R * mix(1.0, 0.7, pulse);
            lp = vec3(cos(az) * rr, rimY - u * L, sin(az) * rr);
            lp.x += sin(u * 5.0 - t * 1.1 + ti) * u * 0.32 * R;
            lp.z += cos(u * 4.0 - t * 0.9 + ti * 1.7) * u * 0.24 * R;
            bright = 0.42 * pow(1.0 - u, 1.2) + 0.05;
            sz = 0.7;
            cm = 0.35 + 0.3 * u;
        }
        lp = rotZ(rotX(lp, -0.35), 0.15 * sin(t * 0.2 + j));
        bright *= smoothstep(0.0, 0.1, rise) * (1.0 - smoothstep(0.9, 1.0, rise));
        return c + lp;
    } else if (uStyle == 46) { // dandelion: seeds drifting up and sideways, each a small radial tuft on a stalk
        float i = floor(s.x * 22.0);
        float hi = phash(i * 1.37 + 0.5);
        float depth = 14.0 + 60.0 * phash(i * 2.11 + 0.2);
        float hh = viewH(depth), hw = hh * uAspect;
        float k = fract(t * (0.016 + 0.01 * hi) + phash(i * 0.73));
        float wind = uP.x < 0.5 ? -1.0 : 1.0;
        vec3 c = vec3((phash(i * 3.3) * 2.0 - 1.0) * hw + wind * (k - 0.4) * 0.5 * hw + 4.0 * sin(t * 0.21 + i),
                      mix(-hh - 4.0, hh + 4.0, k) + 2.0 * sin(t * 0.37 + i * 1.3), -depth);
        float scale = 0.9 + 0.4 * hi;
        vec3 lp;
        if (s.y < 0.18) { // stalk and seed
            float u = s.z;
            lp = vec3(0.0, -u * 3.0, 0.0);
            bright = u > 0.8 ? 0.65 : 0.35;
            sz = u > 0.8 ? 1.0 : 0.6;
            cm = 0.15;
        } else { // pappus: fine filaments radiating, a tiny tuft at each tip
            float f = floor(s.z * 16.0);
            float u = pow(s.w, 0.6);
            float pol = 0.25 + 1.25 * fract(f * 0.618), az = f * 2.4;
            vec3 d = vec3(sin(pol) * cos(az), cos(pol), sin(pol) * sin(az));
            lp = d * u * 2.6;
            bright = 0.3 + 0.6 * pow(u, 4.0);
            sz = 0.6 + 0.5 * pow(u, 4.0);
            cm = 0.3 + 0.4 * u;
        }
        lp = rotZ(rotY(lp, t * 0.15 + i), 0.35 * sin(t * 0.4 + i * 2.1) - wind * 0.25);
        bright *= smoothstep(0.0, 0.1, k) * (1.0 - smoothstep(0.85, 1.0, k));
        return c + lp * scale;
    } else if (uStyle == 47) { // pendulums: two mirrored rows of increasing length, the classic pendulum wave
        float f = s.x * 20.0;
        float i = floor(mod(f, 10.0));                 // 0 = innermost, shortest
        float side = f < 10.0 ? -1.0 : 1.0;
        float depth = 45.0;
        float hh = viewH(depth);
        float m = 20.0 + (9.0 - i);                   // oscillations per cycle: all realign every 75 s
        float w = TAU * m / 75.0;
        float L = 1.72 * hh * (20.0 / m) * (20.0 / m);
        float th = (0.16 + 0.025 * uLoud * uReact.y) * cos(w * t + uP.x * 0.5);
        vec3 piv = vec3(side * (0.92 + i * 0.085) * hh, 0.86 * hh, -depth);
        vec3 dir = vec3(side * sin(th), -cos(th), 0.0);
        if (s.y < 0.3) { // thread
            bright = 0.1;
            sz = 0.45;
            cm = 0.7 * i / 9.0;
            return piv + dir * L * s.z;
        }
        float z = s.z * 2.0 - 1.0, a = s.w * TAU;
        vec3 o = vec3(sqrt(1.0 - z * z) * cos(a), z, sqrt(1.0 - z * z) * sin(a)) * 0.035 * hh * pow(fract(s.y * 7.0), 0.33);
        bright = 0.55 + 0.25 * (0.5 + 0.5 * o.z);
        sz = 0.8;
        cm = 0.7 * i / 9.0;
        return piv + dir * L + o;
    } else if (uStyle == 48) { // koi: a few fish, tapering chains of dots, swimming lazy curves
        float fi = floor(s.x * 4.0);
        // Each fish is 40 beads: 26 along the spine, two lines of 5 for the tail fin, two of 2 for the side fins.
        float slot = floor(fract(s.x * 4.0) * 40.0);
        float side = mod(fi, 2.0) * 2.0 - 1.0;
        float depth = 34.0 + 5.0 * fi;
        float hh = viewH(depth), hw = hh * uAspect;
        float sc = hh / 26.0;
        float ph = fi * 1.7 + uP.x * 6.0;
        float tau = t * 0.45;
        float bodyLen = (7.0 + 1.5 * fract(fi * 0.57)) * sc;
        float u, off, rad;
        if (slot < 26.0) { u = slot / 25.0 * 0.82; off = 0.0; rad = 1.2 * pow(sin(PI * min(u / 0.82 * 0.85 + 0.15, 1.0)), 0.7); }
        else if (slot < 36.0) { float e = mod(slot - 26.0, 5.0) / 4.0; u = 0.82 + 0.16 * e; off = (slot < 31.0 ? -1.0 : 1.0) * (0.15 + 0.8 * e); rad = 0.45 - 0.15 * e; }
        else { float e = mod(slot - 36.0, 2.0); u = 0.24 + 0.05 * e; off = (slot < 38.0 ? -1.0 : 1.0) * (1.2 + 0.6 * e); rad = 0.4; }
        // Swims around its loop, the body sampled backward along it.
        float dir = fi < 1.5 ? -1.0 : 1.0;
        float th = dir * tau * 0.18 + ph;
        float lag = dir * bodyLen / (0.33 * hh);
        float tb = th - u * lag;
        vec2 bp = koiPath(tb, tau, ph, side, hw, hh);
        vec2 tg = normalize(bp - koiPath(tb - 0.02 * dir, tau, ph, side, hw, hh) + vec2(1e-5));
        vec2 nr = vec2(-tg.y, tg.x);
        float sway = sin(u * 5.0 - t * 1.8 + fi) * 0.5 * u;
        vec2 jit = (vec2(s.y, s.z) - 0.5) * vec2(0.5, 1.7) * rad;
        vec2 q = bp + (nr * (sway + off) + tg * jit.x + nr * jit.y) * sc;
        bright = 0.24 * (slot < 26.0 ? 1.0 : 0.6);
        sz = (0.6 + 0.9 * rad) * (0.8 + 0.4 * s.w);
        cm = 0.5 * smoothstep(-0.3, 0.3, sin(u * 11.0 + fi * 2.3) + 0.6 * sin(u * 27.0 + fi));
        return vec3(q, -depth);
    } else if (uStyle == 49) { // sky lanterns: slowly rising and swaying, a warm flame glowing in each
        float i = floor(s.x * 22.0);
        float hi = phash(i * 1.91 + 0.3);
        float depth = 15.0 + 70.0 * pow(phash(i * 2.7 + 0.1), 0.8);
        float hh = viewH(depth), hw = hh * uAspect;
        float k = fract(t * (0.01 + 0.006 * hi) + phash(i * 0.53));
        vec3 c = vec3((phash(i * 4.1) * 2.0 - 1.0) * hw * 0.95 + 3.0 * sin(t * 0.15 + i * 1.7) + k * 8.0 * (uP.x - 0.5),
                      mix(-hh - 5.0, hh + 5.0, k), -depth);
        vec3 lp;
        if (s.y < 0.75) { // paper shell, wider at the top, lit from below
            float v = s.z;
            float r = 1.0 + 0.6 * v;
            float a = s.w * TAU;
            if (s.y < 0.22) a = floor(s.w * 5.0) / 5.0 * TAU; // ribs of the frame
            else if (s.y < 0.3) { v = 1.0; r *= sqrt(fract(s.y * 37.0)); } // closed top
            lp = vec3(cos(a) * r, v * 3.4, sin(a) * r);
            bright = (s.y < 0.22 ? 0.3 : 0.14) + 0.55 * pow(1.0 - v, 2.0);
            sz = s.y < 0.22 ? 0.7 : 1.8;
            cm = 0.3 + 0.4 * v;
            gWarm = 0.3;
        } else { // flame
            lp = (vec3(s.z, s.w, fract(s.z * 7.0 + s.w * 3.0)) - 0.5) * 0.7 + vec3(0.0, 0.4, 0.0);
            bright = 0.6 * (0.85 + 0.15 * sin(t * 1.3 + i * 3.0));
            sz = 1.25;
            cm = 0.1;
            gWarm = 0.6;
        }
        lp = rotZ(rotY(lp, t * 0.1 + i), 0.12 * sin(t * 0.35 + i * 2.3)) * (0.9 + 0.4 * hi);
        bright *= smoothstep(0.0, 0.08, k) * (1.0 - smoothstep(0.85, 1.0, k));
        return c + lp;
    } else if (uStyle == 50) { // snowfall: big soft flakes falling gently, swaying, in depth layers
        float layer = s.x < 0.12 ? 0.0 : (s.x < 0.4 ? 1.0 : 2.0);
        float depth = layer < 0.5 ? 8.0 + 10.0 * s.z : (layer < 1.5 ? 28.0 + 14.0 * s.z : 58.0 + 30.0 * s.z);
        float hh = viewH(depth), hw = hh * uAspect;
        float span = 2.0 * hh + 8.0;
        float y = hh + 4.0 - mod(s.y * span + t * 0.14 * hh * (0.75 + 0.5 * s.w), span);
        float x = (fract(s.w * 7.13 + s.x * 3.1) * 2.0 - 1.0) * hw * 1.05
                  + sin(t * 0.45 * (0.7 + 0.6 * s.w) + s.w * 30.0) * 0.075 * hh
                  + (uP.x - 0.5) * 0.25 * (y - hh);
        sz = (layer < 0.5 ? 3.4 : (layer < 1.5 ? 1.7 : 0.95)) * (0.75 + 0.5 * s.w);
        bright = (layer < 0.5 ? 0.16 : (layer < 1.5 ? 0.4 : 0.5)) * (0.7 + 0.3 * s.w);
        cm = layer * 0.3 + 0.15 * s.w;
        return vec3(x, y, -depth);
    } else if (uStyle == 51) { // gears: two mirrored chains of interlocking toothed rings, turning together
        float side = mod(float(gl_InstanceID), 2.0) * 2.0 - 1.0;
        float gi = s.x < 0.28 ? 0.0 : (s.x < 0.48 ? 1.0 : (s.x < 0.84 ? 2.0 : 3.0));
        // Chain: pitch radius, teeth (2 per unit of radius) and the direction to the next gear.
        vec4 G0 = vec4(-31.0, 13.0, 7.0, -1.9), G1 = vec4(0.0, 0.0, 5.0, -1.2), G2 = vec4(0.0, 0.0, 9.0, -2.6),
             G3 = vec4(0.0, 0.0, 4.0, 0.0);
        G1.xy = G0.xy + (G0.z + G1.z) * vec2(cos(G0.w), sin(G0.w));
        G2.xy = G1.xy + (G1.z + G2.z) * vec2(cos(G1.w), sin(G1.w));
        G3.xy = G2.xy + (G2.z + G3.z) * vec2(cos(G2.w), sin(G2.w));
        float al = t * 0.12 + uP.x * 3.0;
        float gs = viewH(40.0) / 27.0;
        // Each next angle keeps a tooth of one gear in a gap of the other at their contact point.
        vec4 g = G0;
        if (gi > 0.5) { al = G0.w + PI + (G0.z * 2.0 * (G0.w - al) + PI) / (G1.z * 2.0); g = G1; }
        if (gi > 1.5) { al = G1.w + PI + (G1.z * 2.0 * (G1.w - al) + PI) / (G2.z * 2.0); g = G2; }
        if (gi > 2.5) { al = G2.w + PI + (G2.z * 2.0 * (G2.w - al) + PI) / (G3.z * 2.0); g = G3; }
        float R = g.z, N = R * 2.0;
        float ang, r;
        if (s.z < 0.62) { // toothed outline: tip, flank, root, flank
            float w = s.y * N, ti = floor(w), q = fract(w);
            float ro = R + 0.8, rin = R - 0.95;
            float pa;
            if (q < 0.3) { pa = mix(-0.17, 0.17, q / 0.3); r = ro; }
            else if (q < 0.45) { float e = (q - 0.3) / 0.15; pa = mix(0.17, 0.3, e); r = mix(ro, rin, e); }
            else if (q < 0.85) { pa = mix(0.3, 0.7, (q - 0.45) / 0.4); r = rin; }
            else { float e = (q - 0.85) / 0.15; pa = mix(0.7, 0.83, e); r = mix(rin, ro, e); }
            ang = al + (ti + pa) / N * TAU;
            bright = 0.5;
            cm = 0.2 + 0.1 * gi;
        } else if (s.z < 0.8) { // inner rim
            ang = s.y * TAU; r = R - 2.2;
            bright = 0.3; cm = 0.5;
        } else if (s.z < 0.9) { // hub
            ang = s.y * TAU; r = 0.28 * R;
            bright = 0.45; cm = 0.7;
        } else { // spokes
            float sp = floor(s.w * 5.0);
            ang = al + sp / 5.0 * TAU; r = mix(0.28 * R, R - 2.2, fract(s.w * 5.0));
            bright = 0.25; cm = 0.6;
        }
        vec2 q2 = (g.xy + vec2(cos(ang), sin(ang)) * r) * gs;
        sz = 0.75;
        return vec3(side * -q2.x * (0.95 + 0.1 * uP.y), q2.y, -40.0 - gi * 0.7);
    } else if (uStyle == 52) { // spirograph: hypotrochoids slowly traced, then fading
        float ci = floor(s.x * 4.0);
        float u = s.y;
        float ct = t / 38.0 + ci * 0.25 + uP.x;
        float cyc = floor(ct), k = fract(ct);
        float hs = cyc * 5.7 + ci * 13.1;
        float pk = floor(phash(hs) * 7.0);
        float Rg = pk < 1.0 ? 5.0 : (pk < 2.0 ? 7.0 : (pk < 3.0 ? 8.0 : (pk < 4.0 ? 7.0 : (pk < 5.0 ? 9.0 : (pk < 6.0 ? 6.0 : 10.0)))));
        float rg = pk < 1.0 ? 3.0 : (pk < 2.0 ? 4.0 : (pk < 3.0 ? 3.0 : (pk < 4.0 ? 2.0 : (pk < 5.0 ? 4.0 : (pk < 6.0 ? 5.0 : 3.0)))));
        float d = rg * (0.5 + 0.7 * phash(hs + 1.0));
        float side = mod(ci, 2.0) * 2.0 - 1.0;
        float depth = 34.0 + 10.0 * floor(ci / 2.0);
        float hh = viewH(depth), hw = hh * uAspect;
        vec3 c = vec3(side * (0.6 + 0.12 * phash(hs + 2.0)) * hw, (floor(ci / 2.0) > 0.5 ? -0.42 : 0.4) * hh * side, -depth);
        float size = (0.33 + 0.08 * phash(hs + 3.0)) * hh;
        float th = u * TAU * rg;
        float q = (Rg - rg) / rg;
        vec2 xy = vec2((Rg - rg) * cos(th) + d * cos(q * th), (Rg - rg) * sin(th) - d * sin(q * th)) * size / (Rg - rg + d);
        float rot = t * 0.02 * side + phash(hs + 4.0) * TAU;
        xy = vec2(cos(rot) * xy.x - sin(rot) * xy.y, sin(rot) * xy.x + cos(rot) * xy.y);
        float drawn = smoothstep(0.0, 0.7, k);
        float age = drawn - u;
        bright = smoothstep(0.0, 0.004, age) * (0.4 + 0.6 * exp(-age * 3.0)) * (1.0 - smoothstep(0.7, 1.0, k)) * 0.6
                 * (1.0 + 0.8 * exp(-age * age * 4000.0));
        sz = 0.7;
        cm = 0.7 * fract(u * 0.8 + phash(hs + 5.0));
        return c + vec3(xy, 0.0);
    } else if (uStyle == 53) { // pulse grid: a 3D grid of dots, a slow wave rolling through it
        float NZ = 4.0;
        float N = max(floor(sqrt(uCount / (2.0 * NZ))), 2.0);
        float nx = 2.0 * N;
        float i = float(gl_InstanceID);
        vec3 g = vec3(mod(i, nx) / (nx - 1.0) - 0.5, mod(floor(i / nx), N) / (N - 1.0) - 0.5, floor(i / (nx * N)) / (NZ - 1.0));
        float depth = 32.0 + g.z * 54.0;
        float hh = viewH(depth), hw = hh * uAspect;
        vec3 p = vec3(g.x * 2.1 * hw, g.y * 2.1 * hh, -depth);
        float dd = length(p.xy / vec2(hw, hh));
        float ph = dd * 5.0 - t * 0.6 + g.z * 1.2 + (uP.x - 0.5) * g.x * 4.0;
        float wv = sin(ph);
        float amp = 1.5 + 3.5 * bass;
        p.z += amp * wv * 1.4;
        p.xy += p.xy / max(length(p.xy), 1.0) * amp * 0.5 * wv;
        bright = (0.07 + 0.32 * pow(0.5 + 0.5 * wv, 3.0)) * (1.0 - 0.35 * g.z);
        sz = 0.75 + 0.35 * (0.5 + 0.5 * wv);
        cm = clamp(0.4 + 0.3 * wv + 0.15 * (g.z - 0.5), 0.0, 1.0);
        return p;
    } else { // meteors: fast diagonal streaks
        float k = fract(s.y + t * 0.3 * (0.5 + s.w));
        vec3 start = vec3((s.x - 0.2) * 160.0, 50.0, -20.0 - s.z * 50.0);
        vec3 p = start + vec3(-60.0, -110.0, 0.0) * k;
        bright = sin(k * PI) * 0.8;
        cm = s.w;
        return p;
    }
}

void main() {
    float bright, cm, sz;
    vec3 p = stylePos(aSeed, uTime, bright, cm, sz);
    // Bass kicks push the whole field softly outward from behind the board, then it settles back.
    vec3 fromCenter = p - vec3(0.0, 0.0, -20.0);
    p += normalize(fromCenter + vec3(1e-3)) * uKick * (1.2 + 0.03 * length(fromCenter)) * (0.6 + 0.8 * aSeed.z);
    vec4 clip = uVP * vec4(p, 1.0);
    float viewZ = -(uView * vec4(p, 1.0)).z;
    float size = 0.22 * uSize * sz * (1.0 + 0.4 * uBeat * uReact.x);
    float a = bright * uBright * (0.75 + 0.5 * uLoud * uReact.y);
    a *= smoothstep(3.0, 14.0, viewZ); // fade near the camera
    if (uStyle >= 44 && uStyle <= 53) a *= 1.0 + 0.6 * uPale; // fine layouts: a little more ink on pale backgrounds
    // Phase ripple: layers of the old scene recede as the wave front passes, the new ones appear.
    if (uWipe.x > 0.5) {
        vec2 ndc = clip.xy / max(clip.w, 1e-3);
        float d = wipeCoord(ndc, uAspect, fract(sin(dot(aSeed.xy, vec2(12.9898, 78.233)) + uWipeSeed) * 43758.5453));
        float k = 1.0 - smoothstep(uWipe.y - uWipe.z, uWipe.y, d);
        a *= uSide > 0.5 ? k : 1.0 - k;
        // Bright ring riding the wave front on rising transitions.
        float x = (d - (uWipe.y - uWipe.z * 0.5)) / (uWipe.z * 0.3);
        a *= 1.0 + 3.0 * uWipe.w * exp(-x * x);
    }
    // Structural density: sparser scenes fade out part of the instances (smoothly, never popping).
    float frac = float(gl_InstanceID) / max(uCount, 1.0);
    a *= 1.0 - smoothstep(uDensity - 0.15, uDensity + 0.001, frac);
    // Keep the playfield legible: dim particles in the board's column of space.
    float inBoard = (1.0 - smoothstep(4.5, 7.0, abs(p.x))) * (1.0 - smoothstep(9.5, 13.0, abs(p.y)));
    a *= 1.0 - uBoardDim * inBoard * smoothstep(-30.0, -8.0, p.z);

    // Enforce a minimum on-screen size: fade instead of shrinking below ~1.5px (no shimmer). Detailed shapes
    // (crescents, stars, outlines...) shimmer and turn into noise when tiny: they fade below ~5px.
    float ndcSize = size * uP11 / max(clip.w, 1e-3);
    bool simple = uShape == 0 || uShape == 1 || uShape == 5 || uShape == 6;
    float minNdc = uPixel * (simple ? 1.6 : 5.0);
    if (ndcSize < minNdc) { a *= (ndcSize / minNdc) * (ndcSize / minNdc); size *= minNdc / ndcSize; }

    vec2 offs;
    if (uShape == 6) { // streak along screen-space motion
        float b2, c2, s2;
        vec3 pp = stylePos(aSeed, uTime - 0.08, b2, c2, s2);
        vec4 cp = uVP * vec4(pp, 1.0);
        vec2 d = (clip.xy / clip.w - cp.xy / max(cp.w, 1e-3)) * vec2(uAspect, 1.0);
        float len = length(d);
        vec2 dir = len > 1e-5 ? d / len : vec2(0.0, 1.0);
        vec2 perp = vec2(-dir.y, dir.x);
        float w = size * uP11 / clip.w;
        vec2 o = dir * aCorner.x * (min(len, 0.3) * 0.5 + w) + perp * aCorner.y * w;
        offs = o / vec2(uAspect, 1.0) * clip.w;
        vUV = aCorner;
    } else {
        offs = aCorner * size * vec2(uP11 / uAspect, uP11);
        vUV = aCorner;
    }
    clip.xy += offs;
    gl_Position = clip;
    vCol = cm < 0.5 ? mix(uColA, uColB, cm * 2.0) : mix(uColB, uColC, cm * 2.0 - 1.0);
    if (uStyle == 49) vCol = mix(vCol, vec3(1.0, 0.62, 0.3) * max(max(vCol.r, vCol.g), vCol.b), gWarm);
    vAlpha = a;
}
)";

// Burst particles (CPU simulated): position & size per instance.
inline const char* BURST_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aPosSize;
layout(location = 2) in vec4 aColor;
uniform mat4 uVP;
uniform float uAspect, uP11;
out vec2 vUV;
out vec3 vCol;
out float vAlpha;
void main() {
    vec4 clip = uVP * vec4(aPosSize.xyz, 1.0);
    clip.xy += aCorner * aPosSize.w * vec2(uP11 / uAspect, uP11);
    gl_Position = clip;
    vUV = aCorner;
    vCol = aColor.rgb;
    vAlpha = aColor.a;
}
)";

inline const char* PARTICLE_FS = R"(#version 330 core
in vec2 vUV;
in vec3 vCol;
in float vAlpha;
out vec4 fragColor;
uniform int uShape;
uniform float uWeight, uPale;
void main() {
    vec2 q = vUV;
    float r = length(q);
    float m;
    if (uShape == 0) m = exp(-r * r * 5.0) * (1.0 - smoothstep(0.7, 1.0, r));
    else if (uShape == 1) m = (1.0 - smoothstep(0.0, 0.2, abs(r - 0.7))) * 0.9 + exp(-r * r * 30.0) * 0.3;
    else if (uShape == 2) {
        float cr = exp(-abs(q.x) * 14.0) * exp(-abs(q.y) * 2.5) + exp(-abs(q.y) * 14.0) * exp(-abs(q.x) * 2.5);
        m = cr * 0.9 + exp(-r * r * 12.0);
    }
    else if (uShape == 3) m = 1.0 - smoothstep(0.45, 0.6, max(abs(q.x), abs(q.y)));
    else if (uShape == 4) m = 1.0 - smoothstep(0.5, 0.7, abs(q.x) + abs(q.y));
    else if (uShape == 5) m = (1.0 - smoothstep(0.9, 1.0, r)) * (0.55 + 0.45 * smoothstep(0.7, 0.95, r));
    else if (uShape == 6) m = exp(-q.y * q.y * 6.0) * (1.0 - smoothstep(0.3, 1.0, abs(q.x)));
    else if (uShape == 8) { // five-point star
        float a = atan(q.y, q.x);
        float rs = 0.45 + 0.35 * pow(abs(cos(a * 2.5)), 3.0);
        m = 1.0 - smoothstep(rs - 0.08, rs + 0.02, r);
    } else if (uShape == 9) { // hexagon
        vec2 h = abs(q);
        float d = max(h.x * 0.866 + h.y * 0.5, h.y);
        m = 1.0 - smoothstep(0.6, 0.7, d);
    } else if (uShape == 10) { // triangle
        float d = max(abs(q.x) * 0.866 + q.y * 0.5, -q.y);
        m = 1.0 - smoothstep(0.42, 0.52, d);
    } else if (uShape == 11) { // heart
        vec2 h = vec2(abs(q.x), -q.y + 0.25);
        float d = length(h - vec2(0.28, 0.12)) - 0.3;
        d = min(d, max(h.x * 0.9 + h.y * 0.7 - 0.45, -h.y + 0.1));
        m = 1.0 - smoothstep(-0.02, 0.06, d);
    } else if (uShape == 12) { // crescent
        m = (1.0 - smoothstep(0.62, 0.72, r)) * smoothstep(0.52, 0.62, length(q - vec2(0.28, 0.12)));
    } else if (uShape == 13) { // x cross
        float d = min(abs(q.x - q.y), abs(q.x + q.y));
        m = (1.0 - smoothstep(0.1, 0.2, d)) * (1.0 - smoothstep(0.6, 0.8, r));
    } else if (uShape == 14) { // double ring
        m = max(1.0 - smoothstep(0.0, 0.1, abs(r - 0.75)), 1.0 - smoothstep(0.0, 0.1, abs(r - 0.45)));
    } else if (uShape == 15) { // square outline
        float d = max(abs(q.x), abs(q.y));
        m = 1.0 - smoothstep(0.0, 0.1, abs(d - 0.55));
    } else if (uShape == 16) { // diamond outline
        float d = abs(q.x) + abs(q.y);
        m = 1.0 - smoothstep(0.0, 0.12, abs(d - 0.6));
    } else if (uShape == 17) { // long four-point flare
        m = exp(-abs(q.x) * 18.0) * exp(-abs(q.y) * 1.8) + exp(-abs(q.y) * 18.0) * exp(-abs(q.x) * 1.8) * 0.6
            + exp(-r * r * 20.0) * 0.8;
    } else if (uShape == 18) { // short bar
        m = (1.0 - smoothstep(0.12, 0.2, abs(q.y))) * (1.0 - smoothstep(0.6, 0.8, abs(q.x)));
    } else if (uShape == 19) { // ring with a dot
        m = max(1.0 - smoothstep(0.0, 0.12, abs(r - 0.7)), exp(-r * r * 25.0));
    }
    else {
        float bar = min(abs(q.x), abs(q.y));
        m = (1.0 - smoothstep(0.1, 0.2, bar)) * (1.0 - smoothstep(0.6, 0.8, max(abs(q.x), abs(q.y))));
    }
    float a = clamp(m * vAlpha * uWeight, 0.0, 1.5);
    if (a < 0.002) discard;
    // Premultiplied output: alpha = 0 means purely additive (dark themes), pale themes blend normally.
    fragColor = vec4(vCol * a, min(a, 1.0) * uPale);
}
)";

// ---------------------------------------------------------------- blocks
inline const char* BLOCK_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aEdge;
layout(location = 3) in vec3 iPos;
layout(location = 4) in vec3 iScale;
layout(location = 5) in vec4 iColor;
layout(location = 6) in vec4 iParams; // x: kind (0 block, 1 ghost, 2 emissive, 3 preview), y: flash, z: glow, w: rand
uniform mat4 uVP;
uniform float uDepth;
out vec3 vN;
out vec3 vWorld;
centroid out vec4 vEdge; // centroid: never extrapolated outside the face with MSAA
out vec4 vCol;
out vec4 vPar;
void main() {
    vec3 sc = iScale;
    if (iParams.x < 1.5 || iParams.x > 2.5) sc.z *= uDepth;
    vec3 wp = iPos + aPos * sc;
    vN = normalize(aNormal / sc);
    vWorld = wp;
    vEdge = aEdge;
    vCol = iColor;
    vPar = iParams;
    gl_Position = uVP * vec4(wp, 1.0);
}
)";

inline const char* BLOCK_FS = R"(#version 330 core
in vec3 vN;
in vec3 vWorld;
centroid in vec4 vEdge;
in vec4 vCol;
in vec4 vPar;
out vec4 fragColor;
uniform int uStyleA, uStyleB;
uniform float uStyleMix;
uniform float uEdgeW, uEmissive, uFill, uGhost, uPale, uBeat, uTime, uLegible;
uniform vec3 uCamPos, uLightDir;
uniform vec3 uAccent, uHighlight;
uniform vec3 uPiece[7];

float gLam, gSpec, gFres, gE, gEdge, gEdgeGlow, gAA;
vec2 gUV;
// Face frame (styles 22+): normal, view, light, the face's tangents along uv, the face position with x to the
// right and y up (front face), and a seed per piece type and face (stable while the piece moves).
vec3 gN, gV, gL, gT, gB;
vec2 gFP;
float gSeed;

float h11(float n) { return fract(sin(n * 12.9898 + 4.1414) * 43758.5453); }
float h21(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(h21(i), h21(i + vec2(1, 0)), f.x), mix(h21(i + vec2(0, 1)), h21(i + vec2(1, 1)), f.x), f.y);
}
// Normal tilted by a height field's gradient along the face's uv.
vec3 bumpN(vec2 g) { return normalize(gN - gT * g.x - gB * g.y); }
float lamOf(vec3 n) { return max(dot(n, gL), 0.0) * 0.65 + 0.35; }
float specOf(vec3 n, float p) { return pow(max(dot(reflect(-gL, n), gV), 0.0), p); }
vec3 hueRot(vec3 c, float a) {
    const vec3 k = vec3(0.57735);
    float ca = cos(a);
    return max(c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca), 0.0);
}
float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0));
}
vec3 goldOf() { return mix(vec3(1.0, 0.74, 0.3), uAccent, 0.2); }

vec4 shade(int style, vec3 col) {
    vec3 rgb;
    float a = 1.0;
    float em = uEmissive, lam = gLam, spec = gSpec, fres = gFres, e = gE, edge = gEdge, edgeGlow = gEdgeGlow, aa = gAA;
    vec2 uv = gUV;
    if (style == 0) { // glass
        // Dark tinted glass glows on dark themes; on pale ones a dark fill over the light backplate turns
        // gray, so the glass there is a light, saturated tint.
        rgb = col * mix(0.1 + 0.25 * lam, 0.75 + 0.25 * lam, uPale) + col * edgeGlow * em * 1.1 + mix(col, vec3(1.0), 0.4) * spec * 0.3;
        a = mix(mix(uFill, max(uFill, 0.82), uPale), 1.0, edge);
    } else if (style == 1) { // solid
        rgb = col * (0.3 + 0.7 * lam) * (1.0 - 0.3 * edge) + vec3(spec) * 0.3 + col * 0.12 * em;
    } else if (style == 2) { // wire
        // On pale themes a dark see-through fill over the light backplate turns gray: tint it instead.
        rgb = col * edgeGlow * em * 1.5 + col * mix(0.03, 0.8, uPale);
        a = max(edge, mix(mix(0.1, 0.45, uLegible), 0.6, uPale));
        rgb *= 1.0 + 0.5 * uLegible * (1.0 - uPale); // brighter lines over the darkened backplate
    } else if (style == 3) { // lantern
        float inner = pow(clamp(e, 0.0, 1.0), 1.4);
        rgb = col * (0.15 + inner * em * 1.4) + col * edge * 0.4 + vec3(spec) * 0.2;
        a = 0.92;
    } else if (style == 4) { // inset
        float core = 1.0 - smoothstep(0.42 - aa, 0.42 + aa, 1.0 - e);
        rgb = mix(col * 0.25 * lam, col * (0.55 + em * 0.8), core) + vec3(spec) * 0.15;
    } else if (style == 5) { // dots
        vec2 g = fract((uv * 0.5 + 0.5) * 3.0) - 0.5;
        float dotm = 1.0 - smoothstep(0.26 - aa, 0.3 + aa, length(g));
        rgb = col * (0.1 * lam + dotm * (0.45 + em * 0.9));
        a = mix(0.55, 1.0, dotm);
    } else if (style == 6) { // crystal (fresnel)
        rgb = col * (0.08 + fres * em * 2.0) + mix(col, vec3(1.0), 0.4) * spec * 0.4 + col * lam * 0.12 + col * edge * 0.3 * em;
        a = mix(uFill * 0.7, 1.0, max(fres, edge));
    } else if (style == 7) { // split two-tone
        float tone = smoothstep(-aa, aa, uv.x + uv.y);
        rgb = col * mix(0.4, 0.95, tone) * lam + col * edge * 0.3 * em + vec3(spec) * 0.2;
    } else if (style == 8) { // holographic foil: two close tones split by soft-edged diagonals drifting across the stack
        float d = (vWorld.x + vWorld.y) * 0.5 - uTime * 0.1;
        float f = fract(d), w = fwidth(d) * 1.2;
        float b = smoothstep(0.5 - w, 0.5 + w, f) * (1.0 - smoothstep(1.0 - w, 1.0, f));
        rgb = col * mix(0.62, 0.8, b) * (0.7 + 0.3 * lam) * (0.75 + 0.35 * em);
        rgb += col * edgeGlow * em * 0.5 + vec3(spec) * 0.15;
        a = mix(0.9, 1.0, edge);
    } else if (style == 9) { // lit gradient (bright top, deep bottom)
        float g = clamp(0.5 + 0.5 * (vWorld.y - floor(vWorld.y + 0.5)) * 2.0, 0.0, 1.0);
        rgb = col * mix(0.3, 1.05, g) * (0.5 + 0.5 * lam) + col * edge * 0.25 * em;
    } else if (style == 10) { // double outline
        float ring = (1.0 - smoothstep(uEdgeW * 2.3 - aa, uEdgeW * 2.3 + aa, e)) * smoothstep(uEdgeW * 1.6 - aa, uEdgeW * 1.6 + aa, e);
        rgb = col * (edge + ring * 0.8) * em * 1.2 + col * mix(0.06, 0.8, uPale);
        a = max(max(edge, ring), mix(mix(0.12, 0.45, uLegible), 0.6, uPale));
        rgb *= 1.0 + 0.5 * uLegible * (1.0 - uPale); // brighter lines over the darkened backplate
    } else if (style == 11) { // neon tube: wide soft glow along the edges, dark core
        float tube = exp(-e / max(uEdgeW, 0.02) * 1.2);
        rgb = col * (mix(0.04, 0.8, uPale) + tube * em * 1.4);
        a = max(tube, mix(mix(0.15, 0.45, uLegible), 0.6, uPale));
        rgb *= 1.0 + 0.5 * uLegible * (1.0 - uPale); // brighter lines over the darkened backplate
    } else if (style == 12) { // circuit traces
        vec2 g = abs(fract((uv * 0.5 + 0.5) * 3.0) - 0.5);
        float trace = 1.0 - smoothstep(0.04 - aa, 0.06 + aa, min(g.x, g.y));
        float node = 1.0 - smoothstep(0.1 - aa, 0.13 + aa, length(g));
        rgb = col * (0.12 * lam + max(trace * 0.6, node) * (0.4 + em * 0.8)) + col * edge * 0.3 * em;
    } else if (style == 13) { // frosted glass
        vec2 q = floor((uv * 0.5 + 0.5) * 24.0);
        float n = fract(sin(dot(q, vec2(12.9898, 78.233))) * 43758.5453);
        // On pale themes a thin dark frost over the light backplate looks washed out: a denser, colored frost.
        rgb = col * mix(0.2 + 0.25 * lam + 0.15 * n, 0.7 + 0.25 * lam + 0.12 * n, uPale) + col * edgeGlow * em * 0.7
            + mix(col, vec3(1.0), 0.3) * spec * 0.2;
        a = mix(mix(0.55, 0.88, uPale), 1.0, edge);
    } else if (style == 14) { // checker
        vec2 c = floor((uv * 0.5 + 0.5) * 2.0);
        float t = mod(c.x + c.y, 2.0);
        rgb = col * mix(0.35, 0.9, t) * lam + col * edge * 0.3 * em;
    } else if (style == 15) { // concentric square rings
        float rr = 0.5 + 0.5 * sin((1.0 - e) * 22.0);
        rgb = col * (0.15 + 0.55 * rr * em) * (0.6 + 0.4 * lam) + col * edge * 0.3 * em;
    } else if (style == 16) { // classic bevel: lit top-left, shaded bottom-right
        float bev = 1.0 - smoothstep(0.18 - aa, 0.18 + aa, e);
        float side = clamp(0.5 + 0.5 * (uv.y - uv.x) * 0.7, 0.0, 1.0);
        rgb = col * (0.55 + 0.35 * lam) * mix(1.0, mix(0.55, 1.35, side), bev);
    } else if (style == 17) { // pixel mosaic
        vec2 q = floor((uv * 0.5 + 0.5) * 4.0);
        float n = fract(sin(dot(q + floor(vWorld.xy), vec2(12.9898, 78.233))) * 43758.5453);
        rgb = col * (0.35 + 0.55 * n) * (0.6 + 0.4 * lam) + col * edge * 0.2 * em;
    } else if (style == 18) { // wide, soft diagonal stripes, barely drifting
        float st = 0.5 + 0.5 * sin((uv.x + uv.y) * 4.5 - uTime * 0.15);
        rgb = col * (0.4 + 0.22 * st * em) * (0.6 + 0.4 * lam) + col * edge * 0.3 * em;
    } else if (style == 19) { // glowing round core
        float core = 1.0 - smoothstep(0.45 - aa, 0.55 + aa, length(uv));
        rgb = col * (0.12 * lam + core * (0.5 + em * 0.9)) + col * edge * 0.25 * em;
    } else if (style == 20) { // cross-hatching
        float h1 = abs(fract((uv.x + uv.y) * 4.0) - 0.5), h2 = abs(fract((uv.x - uv.y) * 4.0) - 0.5);
        float hatch = 1.0 - smoothstep(0.1 - aa, 0.14 + aa, min(h1, h2));
        rgb = col * (0.18 * lam + hatch * (0.35 + 0.6 * em)) + col * edge * 0.35 * em;
    } else if (style == 22) { // kintsugi: matte ceramic mended with thin gold seams
        float ang = (gSeed - 0.5) * 1.4 + (h11(gSeed * 3.1) > 0.5 ? 1.5708 : 0.0);
        vec2 p = mat2(cos(ang), sin(ang), -sin(ang), cos(ang)) * gFP;
        float s2 = h11(gSeed * 5.3), s3 = h11(gSeed * 9.7);
        float yc = 0.4 * (s2 - 0.5) + 0.16 * sin(p.x * 2.2 + gSeed * 6.28) + 0.06 * sin(p.x * 5.3 + s3 * 6.28);
        float d = abs(p.y - yc);
        float x0 = (s3 - 0.5) * 0.9, y0 = 0.4 * (s2 - 0.5) + 0.16 * sin(x0 * 2.2 + gSeed * 6.28) + 0.06 * sin(x0 * 5.3 + s3 * 6.28);
        float ba = (0.6 + 0.5 * s2) * (s3 > 0.5 ? 1.0 : -1.0);
        vec2 b0 = vec2(x0, y0), b1 = b0 + vec2(cos(ba), sin(ba)) * (0.55 + 0.3 * gSeed);
        float t = clamp(dot(p - b0, b1 - b0) / dot(b1 - b0, b1 - b0), 0.0, 1.0);
        float db = segDist(p, b0, b1) + t * 0.025; // the branch thins out
        float w = 0.045, dm = min(d, db), fw = max(fwidth(dm), 1e-3);
        float seam = 1.0 - smoothstep(w - fw, w + fw, dm);
        vec3 ceramic = col * (0.3 + 0.62 * lam) * (1.0 - 0.18 * edge) + col * 0.1 * em + vec3(0.05) * specOf(gN, 8.0);
        vec3 gold = goldOf() * mix(0.85 + 0.35 * em + 0.6 * spec, 0.75, uPale);
        rgb = mix(ceramic, gold, seam * (1.0 - 0.5 * edge));
    } else if (style == 23) { // terrazzo: flat color with scattered chips of lighter tones and one other piece color
        rgb = col * (0.34 + 0.56 * lam) + col * 0.1 * em + vec3(spec) * 0.12;
        vec2 q = (gFP * 0.5 + 0.5) * 3.0;
        vec2 cell = floor(q), f = fract(q) - 0.5;
        float hc = h21(cell + gSeed * 17.0), fq = length(fwidth(q)); // derivatives out of the branch below
        if (hc > 0.3) {
            float h2 = h21(cell * 1.7 + gSeed * 31.0), h3 = h21(cell * 2.3 + gSeed * 11.0);
            vec2 o = (vec2(h2, h3) - 0.5) * 0.2;
            float ca = h2 * 6.28;
            vec2 r = mat2(cos(ca), sin(ca), -sin(ca), cos(ca)) * (f - o);
            float rad = 0.15 + 0.1 * h3;
            vec2 rn = r / vec2(rad, rad * (0.6 + 0.35 * hc));
            float d = length(rn) * (1.0 + 0.12 * sin(atan(rn.y, rn.x) * 3.0 + hc * 9.0)); // irregular, bounded
            float fw = max(fq / rad, 1e-3);
            float chip = (1.0 - smoothstep(1.0 - fw, 1.0 + fw, d)) * (1.0 - smoothstep(0.15, 0.4, fq)); // none on grazing faces
            int other = int(mod(vPar.w + 3.0, 7.0));
            vec3 cc = hc < 0.62 ? mix(col, vec3(1.0), 0.5) : (hc < 0.82 ? col * 0.4 : mix(uPiece[other], col, 0.3));
            rgb = mix(rgb, cc * (0.45 + 0.55 * lam) + cc * 0.1 * em, chip);
        }
    } else if (style == 24) { // hard candy: saturated, domed, glossy, a little translucent
        vec2 g = -4.0 * uv * uv * uv * 0.32; // height 1 - u^4 - v^4
        vec3 nb = bumpN(g);
        float lb = lamOf(nb);
        vec3 cs = max(mix(vec3(dot(col, vec3(0.3, 0.55, 0.15))), col, 1.3), 0.0);
        vec3 body = mix(cs * (0.22 + 0.5 * lb) + cs * em * 0.4 * pow(e, 0.7), cs * (0.5 + 0.45 * lb), uPale);
        float hl = specOf(nb, 14.0) * 0.45 + specOf(nb, 90.0) * 0.9;
        rgb = body * (1.0 - 0.2 * (1.0 - e) * (1.0 - e)) + mix(cs, vec3(1.0), 0.7) * hl;
        a = mix(mix(0.86, 0.94, uPale), 1.0, max(hl, edge * 0.5));
    } else if (style == 25) { // enamel pin: thin polished metal rim, flat glossy color inside
        float rw = 0.16;
        float rim = 1.0 - smoothstep(rw - aa, rw + aa, e);
        float groove = exp(-pow((e - rw - 0.035) / 0.035, 2.0));
        float band = 0.5 + 0.5 * cos((gFP.x * 0.6 + gFP.y) * 2.4 + 0.8);
        float bev = clamp(0.5 + 0.5 * (gFP.y - gFP.x) * 0.8, 0.0, 1.0) * (1.0 - smoothstep(0.0, rw, e));
        vec3 metal = mix(vec3(0.92, 0.84, 0.62), uHighlight, 0.3) * (0.35 + 0.5 * band + 0.3 * bev) * mix(1.0, 0.7, uPale)
            + vec3(spec) * 0.5;
        vec3 inner = col * (0.48 + 0.42 * lam) * (1.0 - 0.4 * groove) + col * 0.12 * em;
        inner += mix(col, vec3(1.0), 0.6) * specOf(gN, 6.0) * 0.12 * (1.0 - groove);
        rgb = mix(inner, metal, rim);
    } else if (style == 26) { // pillow: softly inflated, bright center, edges falling into gentle shade
        vec2 g = -4.0 * uv * uv * uv * 0.55;
        vec3 nb = bumpN(g);
        float r = pow(pow(abs(uv.x), 4.0) + pow(abs(uv.y), 4.0), 0.25);
        float puff = 1.0 - r * r * r;
        rgb = col * (0.22 + 0.62 * lamOf(nb)) * (0.62 + 0.45 * puff) + col * 0.12 * em * puff + mix(col, vec3(1.0), 0.5) * specOf(nb, 10.0) * 0.1;
    } else if (style == 27) { // scales: overlapping arcs hanging down, subtle
        vec2 q = (gFP * 0.5 + 0.5) * 3.0;
        float rr = ceil(q.y * 2.0);
        float R = 0.58, tone = 1.0, line = 0.0, dd = 1.0;
        for (int k = 1; k >= 0; k--) {
            float row = rr + float(k);
            float off = mod(row, 2.0) * 0.5;
            vec2 c = vec2(floor(q.x - off) + 0.5 + off, row * 0.5);
            float d = length(q - c);
            if (d < R && dd > 0.99) { dd = d / R; }
        }
        float fw = max(fwidth(dd), 1e-3);
        tone = 1.0 - 0.16 * smoothstep(0.35, 1.0, dd);
        line = smoothstep(0.88 - fw, 0.95, dd) * (1.0 - smoothstep(1.0, 1.0 + fw, dd));
        rgb = col * (0.34 + 0.58 * lam) * tone * (1.0 - 0.3 * line) + col * 0.1 * em + vec3(spec) * 0.15;
    } else if (style == 28) { // waffle: embossed grid of square pockets
        vec2 ld = normalize(vec2(gL.x, gL.y) + vec2(0.0, 1e-3));
        vec2 q = (gFP * 0.5 + 0.5) * 3.0;
        float fw = max(fwidth(q.x) + fwidth(q.y), 1e-3);
        float sw = max(0.07, fw);
        #define WAFFLE_H(P) smoothstep(0.3, 0.3 + sw, max(abs(fract(P).x - 0.5), abs(fract(P).y - 0.5)))
        float h = WAFFLE_H(q);
        float slope = (WAFFLE_H(q + ld * 0.05) - WAFFLE_H(q - ld * 0.05));
        #undef WAFFLE_H
        float edgeRidge = 1.0 - smoothstep(0.1, 0.1 + aa, e);
        h = max(h, edgeRidge);
        rgb = col * (0.36 + 0.55 * lam) * (0.76 + 0.24 * h) * (1.0 - 0.45 * slope * (1.0 - edgeRidge)) + col * 0.1 * em
            + vec3(spec) * 0.12 * h;
    } else if (style == 29) { // opal: milky body, hue shimmer drifting slowly with view and position
        float ph = dot(gFP, vec2(0.35, 0.25)) + dot(vWorld.xy, vec2(0.11, 0.07)) + dot(gV.xy, vec2(2.0, 1.4)) + uTime * 0.03;
        float wob = vnoise(gFP * 0.8 + vec2(gSeed * 9.0, uTime * 0.02));
        float ang = mix(0.12, 0.08, uPale) * sin((ph + wob * 0.35) * 6.28);
        vec3 c1 = hueRot(col, ang);
        float milk = dot(col, vec3(0.3, 0.55, 0.15));
        vec3 body = mix(c1, vec3(milk) * 1.15, 0.12);
        vec3 sheen = mix(hueRot(col, ang * 4.0), vec3(milk * 1.3), 0.4);
        rgb = body * (0.36 + 0.54 * lam) + sheen * 0.07 * (0.5 + 0.5 * sin((ph - wob) * 9.0)) * (0.5 + 0.5 * em)
            + body * 0.1 * em + mix(col, vec3(1.0), 0.6) * specOf(gN, 12.0) * 0.12 + col * edge * 0.12 * em;
    } else if (style == 30) { // stained glass: leaded panes of glowing translucent color
        float a1 = gSeed * 3.1416, a2 = a1 + 1.0 + h11(gSeed * 4.7) * 1.2;
        vec2 o1 = (vec2(h11(gSeed * 2.3), h11(gSeed * 6.1)) - 0.5) * 0.5;
        vec2 n1 = vec2(cos(a1), sin(a1)), n2 = vec2(cos(a2), sin(a2));
        float d1 = dot(gFP - o1, n1);
        float d2 = dot(gFP - o1, n2);
        if (d1 < 0.0) d2 = 9.0; // the second lead line stops at the first: three panes
        float dl = min(abs(d1), abs(d2));
        float fw = max(fwidth(dl), 1e-3);
        float lead = max(1.0 - smoothstep(0.045 - fw, 0.045 + fw, dl), 1.0 - smoothstep(0.11 - aa, 0.11 + aa, e));
        float pane = d1 < 0.0 ? 0.0 : (d2 < 0.0 ? 1.0 : 2.0);
        float tone = 0.84 + 0.28 * h11(pane + gSeed * 13.0);
        vec3 glow = mix(col * (0.25 + em * 0.75 * (0.55 + 0.45 * e)), col * (0.62 + 0.25 * lam), uPale) * tone;
        vec3 leadC = mix(vec3(0.03) + col * 0.04, vec3(0.05) + col * 0.1, uPale) + vec3(spec) * 0.25;
        rgb = mix(glow, leadC, lead);
        a = mix(mix(0.82, 0.97, uPale), 1.0, lead);
    } else if (style == 31) { // paper: matte, folded once along a diagonal, faint fibers
        float dir = h11(gSeed * 2.9) > 0.5 ? 1.0 : -1.0;
        float df = (gFP.x * dir - gFP.y) * 0.7071;
        float fw = max(fwidth(df), 1e-3);
        float side = smoothstep(-fw, fw, df);
        float facet = mix(1.0, 0.8, side);
        float crease = 1.0 - smoothstep(0.02 - fw, 0.03 + fw, abs(df));
        vec2 fr = mat2(0.8, 0.6, -0.6, 0.8) * gFP;
        float fib = vnoise(fr * vec2(18.0, 3.0) + gSeed * 40.0) * 0.6 + vnoise(gFP * 22.0 + 7.0) * 0.4;
        float fade = 1.0 - smoothstep(0.15, 0.5, length(fwidth(gFP)) * 18.0);
        float lum = 1.0 + (fib - 0.5) * 0.16 * fade;
        rgb = col * (0.38 + 0.54 * lam) * facet * lum * (1.0 + 0.15 * crease) * (1.0 - 0.12 * edge) + col * 0.08 * em;
    } else { // breathing fill (follows the music's pulse)
        rgb = col * (0.25 + 0.35 * lam + (0.3 + 0.6 * uBeat) * em * 0.6) + col * edge * 0.3 * em;
        a = mix(0.7, 1.0, edge);
    }
    return vec4(rgb, a);
}

void main() {
    int kind = int(vPar.x + 0.5);
    vec3 col = vCol.rgb;
    if (kind == 2) { // emissive helpers, premultiplied: additive unless pale theme or flagged normal-blend
        float a = vCol.a;
        float mx = max(col.r, max(col.g, col.b));
        if (mx > 1e-4) col *= 1.2 * (1.0 - exp(-mx / 1.2)) / mx;
        fragColor = vec4(col * a, a * max(uPale, vPar.w));
        return;
    }
    vec3 N = normalize(vN);
    vec3 V = normalize(uCamPos - vWorld);
    if (dot(N, V) < 0.0) N = -N;
    vec3 L = normalize(uLightDir);
    gLam = max(dot(N, L), 0.0) * 0.65 + 0.35;
    gSpec = pow(max(dot(reflect(-L, N), V), 0.0), 40.0);
    gFres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    gUV = vEdge.xy;
    // 1 at face center, 0 on the edge. Clamped: a negative value would make the edge glow explode
    // into single-pixel sparkles. Profile meshes (coin, star...) give their distance to the outline.
    gE = vEdge.w > 0.5 ? clamp(vEdge.z, 0.0, 1.0) : clamp(1.0 - max(abs(gUV.x), abs(gUV.y)), 0.0, 1.0);
    gAA = max(fwidth(gE) * 1.2, 1e-3);
    gEdge = 1.0 - smoothstep(uEdgeW - gAA, uEdgeW + gAA, gE);
    gEdgeGlow = gEdge + exp(-gE / max(uEdgeW, 0.01) * 2.5) * 0.35;
    {
        vec3 an = abs(vN);
        vec3 n = an.x > an.y && an.x > an.z ? vec3(sign(vN.x), 0, 0) : (an.y > an.z ? vec3(0, sign(vN.y), 0) : vec3(0, 0, sign(vN.z)));
        gT = abs(n.y) > 0.5 ? vec3(1, 0, 0) : vec3(0, 1, 0);
        gB = cross(n, gT);
        vec3 fp = gT * gUV.x + gB * gUV.y;
        gFP = vec2(dot(fp, vec3(1, 0, 1)), dot(fp, vec3(0, 1, -1)));
        gN = N; gV = V; gL = L;
        gSeed = h11(vPar.w * 7.31 + dot(n, vec3(1.3, 2.9, 4.7)) + 0.5);
    }

    if (kind == 1) { // ghost
        vec3 rgb = col * (gEdgeGlow * 0.9 * max(uEmissive, 0.5) + 0.06);
        float gmx = max(rgb.r, max(rgb.g, rgb.b));
        if (gmx > 1e-4) rgb *= 1.1 * (1.0 - exp(-gmx / 1.1)) / gmx; // same soft ceiling as blocks
        float a = uGhost * (gEdge * 0.8 + 0.25);
        fragColor = vec4(clamp(rgb, 0.0, 64.0), clamp(a, 0.0, 1.0));
        return;
    }
    // Material crossfade between the two themes' block styles.
    vec4 sa = shade(uStyleA, col);
    vec4 r = uStyleMix < 0.999 ? mix(sa, shade(uStyleB, col), uStyleMix) : shade(uStyleB, col);
    vec3 rgb = r.rgb;
    float a = r.a;
    rgb *= 1.0 + uBeat * 0.25;
    rgb += col * vPar.z * 0.8;                                           // glow (active piece, clears)
    rgb += col * vPar.y * 0.8 * (1.0 - 0.6 * uPale);                     // lock flash (tinted, never white)
    if (kind == 3) a *= 0.9;
    // Soft ceiling that keeps the hue: blocks glow, but never bleach to white.
    float mx = max(rgb.r, max(rgb.g, rgb.b));
    float cap = mix(1.3, 0.95, uPale);
    if (mx > 1e-4) rgb *= cap * (1.0 - exp(-mx / cap)) / mx;
    if (any(isnan(rgb)) || isnan(a)) { rgb = vec3(0.0); a = 0.0; }
    fragColor = vec4(clamp(rgb, 0.0, 64.0), clamp(a, 0.0, 1.0));
}
)";

// ---------------------------------------------------------------- bloom (mip chain, CoD-style)
inline const char* DOWN_FS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uSrc;
uniform vec2 uTexel;
uniform int uFirst;
uniform float uThreshold;
vec3 s(vec2 o) { return texture(uSrc, vUV + o * uTexel).rgb; }
void main() {
    vec3 a = s(vec2(-2, 2)), b = s(vec2(0, 2)), c = s(vec2(2, 2));
    vec3 d = s(vec2(-2, 0)), e = s(vec2(0, 0)), f = s(vec2(2, 0));
    vec3 g = s(vec2(-2, -2)), h = s(vec2(0, -2)), i = s(vec2(2, -2));
    vec3 j = s(vec2(-1, 1)), k = s(vec2(1, 1)), l = s(vec2(-1, -1)), m = s(vec2(1, -1));
    vec3 col = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    if (uFirst == 1) {
        float br = max(col.r, max(col.g, col.b));
        float knee = uThreshold * 0.5;
        float soft = clamp(br - uThreshold + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee + 1e-4);
        float contrib = max(soft, br - uThreshold) / max(br, 1e-4);
        col *= contrib;
        col = min(col, vec3(40.0));
    }
    fragColor = vec4(col, 1.0);
}
)";

inline const char* UP_FS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uSrc;
uniform vec2 uTexel;
uniform float uRadius;
vec3 s(vec2 o) { return texture(uSrc, vUV + o * uTexel * uRadius).rgb; }
void main() {
    vec3 col = s(vec2(0, 0)) * 4.0;
    col += (s(vec2(-1, 0)) + s(vec2(1, 0)) + s(vec2(0, -1)) + s(vec2(0, 1))) * 2.0;
    col += s(vec2(-1, -1)) + s(vec2(1, -1)) + s(vec2(-1, 1)) + s(vec2(1, 1));
    fragColor = vec4(col / 16.0, 1.0);
}
)";

inline const char* COMPOSITE_FS = R"(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uScene, uBloom;
uniform float uBloomStrength, uExposure, uSaturation, uVignette, uChroma, uGrain, uScanlines, uTime, uAspect;
uniform float uFade, uPale;
uniform vec3 uShadowTint, uHighlightTint;
uniform vec2 uRes;
uniform float uInvert, uInvFront; // reversed colors (0/1), and the front still spreading that state (-1: done)
uniform vec2 uInvCenter;
uniform vec3 uInvRim;
float hash(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec3 tonemap(vec3 c) {
    // Hue-preserving soft shoulder: linear up to k, then compresses; very bright light desaturates to white.
    const float k = 0.72;
    float m = max(c.r, max(c.g, c.b));
    if (m <= k) return c;
    float mm = k + (1.0 - k) * (1.0 - exp(-(m - k) / (1.0 - k)));
    vec3 r = c * (mm / m);
    // Only extreme light drifts slightly toward white; strong colors keep their hue.
    float white = clamp((m - 2.0) / 10.0, 0.0, 1.0);
    return mix(r, vec3(mm), white * 0.15);
}
void main() {
    vec2 c = vUV - 0.5;
    vec3 col;
    if (uChroma > 0.0) {
        vec2 off = c * uChroma * 2.0;
        col = vec3(texture(uScene, vUV + off).r, texture(uScene, vUV).g, texture(uScene, vUV - off).b);
    } else {
        col = texture(uScene, vUV).rgb;
    }
    col += texture(uBloom, vUV).rgb * uBloomStrength;
    col *= uExposure;
    col = tonemap(col);
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(lum), col, uSaturation);
    // Split-tone grade.
    col += uShadowTint * (1.0 - smoothstep(0.0, 0.5, lum)) * 0.035 * (1.0 - uPale);
    col = mix(col, col * (0.85 + 0.3 * uHighlightTint / max(0.01, dot(uHighlightTint, vec3(0.333)))),
              smoothstep(0.4, 1.0, lum) * 0.25);
    vec2 vc = c * vec2(uAspect, 1.0);
    col *= 1.0 - uVignette * smoothstep(0.25, 1.1, dot(vc, vc) * 1.6);
    if (uScanlines > 0.0) col *= 1.0 - uScanlines * (0.5 + 0.5 * sin(gl_FragCoord.y * 3.14159));
    col = clamp(col, 0.0, 1.0);
    col = pow(col, vec3(1.0 / 2.2));
    {
        // Inside the front: the new state; outside: the previous one.
        float inv = uInvert;
        float d = length((vUV - uInvCenter) * vec2(uAspect, 1.0));
        if (uInvFront >= 0.0) inv = mix(1.0 - uInvert, uInvert, 1.0 - smoothstep(uInvFront - 0.03, uInvFront + 0.03, d));
        col = mix(col, 1.0 - col * 0.92 - 0.04, inv); // negative, softened a little at both ends
        if (uInvFront >= 0.0) col = clamp(col + uInvRim * exp(-pow((d - uInvFront) / 0.035, 2.0)), 0.0, 1.0);
    }
    col += (hash(gl_FragCoord.xy) - 0.5) * (uGrain * 0.6 + 1.0 / 255.0);
    fragColor = vec4(col * uFade, 1.0);
}
)";

// ---------------------------------------------------------------- text
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

} // namespace shaders
