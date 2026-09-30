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
    else d = radial;
    return clamp(d, 0.0, 1.0);
}

uniform vec4 uP;
uniform float uTime, uBass, uIntensity, uBeat, uAspect, uPale;
uniform vec4 uRays; // strength A, count A, strength B, count B
uniform float uRayTime;
)";

inline const char* BG_FS_BODY = R"(
vec3 bgStyle(int s, vec2 uv) {
    vec2 c = vec2((uv.x - 0.5) * uAspect, uv.y - 0.5);
    vec3 grad = mix(uBottom, uTop, smoothstep(0.0, 1.0, uv.y));
    float breathe = 0.85 + 0.25 * uIntensity + 0.1 * uBass;
    if (s == 0) { // gradient
        return grad;
    } else if (s == 1) { // radial halo
        float d = length(c - vec2(0.0, (uP.x - 0.5) * 0.3));
        return mix(grad, uGlow, exp(-d * d * (5.0 + 6.0 * uP.y)) * 0.55 * breathe);
    } else if (s == 2) { // horizon + sun
        float hy = 0.35 + 0.2 * uP.x;
        vec3 col = mix(uBottom, uTop, smoothstep(hy - 0.05, 1.0, uv.y));
        col = mix(col, uGlow * 0.8, exp(-abs(uv.y - hy) * 18.0) * 0.5 * breathe);
        vec2 sc = c - vec2((uP.y - 0.5) * 0.6, hy + 0.12 - 0.5);
        float sun = 1.0 - smoothstep(0.125, 0.13, length(sc));
        float stripes = step(0.5, fract((uv.y - hy) * 60.0)) + step(hy + 0.1, uv.y);
        col = mix(col, uGlow * 1.2, sun * clamp(stripes, 0.0, 1.0) * 0.8);
        col += uGlow * exp(-length(sc) * 5.0) * 0.25 * breathe;
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
// Phase changes ripple outward from the board: k = how much of the new scene is shown here.
float wipeMix(float d) { return uWipe.x > 0.5 ? 1.0 - smoothstep(uWipe.y - uWipe.z, uWipe.y, d) : uMix; }
void main() {
    float nz = clamp((fbm(vUV * vec2(uAspect, 1.0) * 2.5 + uWipeSeed) - 0.25) / 0.5, 0.0, 1.0);
    float d = wipeCoord(vUV * 2.0 - 1.0, uAspect, nz);
    float k = wipeMix(d);
    uTop = uTopA; uBottom = uBottomA; uGlow = uGlowA;
    vec3 a = bgStyle(uStyleA, vUV);
    uTop = uTopB; uBottom = uBottomB; uGlow = uGlowB;
    vec3 b = bgStyle(uStyleB, vUV);
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
uniform float uTime, uSize, uBright, uCount, uAspect, uP11, uPixel, uIntensity, uDensity, uSide, uKick;
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
    } else { // confetti tumbling down
        float x = (s.x - 0.5) * 130.0 + sin(t * 0.4 + s.w * 20.0) * 4.0;
        float y = mod(s.y * 90.0 - t * (2.0 + 2.5 * s.w), 90.0) - 45.0;
        bright = (1.0 - smoothstep(32.0, 45.0, abs(y))) * 0.8;
        sz = 0.7 + 0.7 * abs(sin(t * 0.9 + s.w * 20.0)); // tumbling
        cm = s.w;
        return vec3(x, y, -6.0 - s.z * 70.0);
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
    a *= 1.0 - 0.8 * inBoard * smoothstep(-30.0, -8.0, p.z);

    // Enforce a minimum on-screen size: fade instead of shrinking below ~1.5px (no shimmer).
    float ndcSize = size * uP11 / max(clip.w, 1e-3);
    float minNdc = uPixel * 1.6;
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
out vec4 vEdge;
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
in vec4 vEdge;
in vec4 vCol;
in vec4 vPar;
out vec4 fragColor;
uniform int uStyleA, uStyleB;
uniform float uStyleMix;
uniform float uEdgeW, uEmissive, uFill, uGhost, uPale, uBeat, uTime;
uniform vec3 uCamPos, uLightDir;

float gLam, gSpec, gFres, gE, gEdge, gEdgeGlow, gAA;
vec2 gUV;

vec4 shade(int style, vec3 col) {
    vec3 rgb;
    float a = 1.0;
    float em = uEmissive, lam = gLam, spec = gSpec, fres = gFres, e = gE, edge = gEdge, edgeGlow = gEdgeGlow, aa = gAA;
    vec2 uv = gUV;
    if (style == 0) { // glass
        rgb = col * (0.1 + 0.25 * lam) + col * edgeGlow * em * 1.1 + mix(col, vec3(1.0), 0.4) * spec * 0.3;
        a = mix(uFill, 1.0, edge);
    } else if (style == 1) { // solid
        rgb = col * (0.3 + 0.7 * lam) * (1.0 - 0.3 * edge) + vec3(spec) * 0.3 + col * 0.12 * em;
    } else if (style == 2) { // wire
        rgb = col * edgeGlow * em * 1.5 + col * 0.03;
        a = max(edge, 0.1 + 0.1 * uPale);
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
    } else if (style == 8) { // holographic scanlines
        float stripes = 0.5 + 0.5 * sin(vWorld.y * 16.0 - uTime * 2.0);
        rgb = col * (0.1 + 0.5 * stripes * em) + col * edgeGlow * em * 0.8;
        a = mix(0.45, 1.0, max(edge, stripes * 0.5));
    } else if (style == 9) { // lit gradient (bright top, deep bottom)
        float g = clamp(0.5 + 0.5 * (vWorld.y - floor(vWorld.y + 0.5)) * 2.0, 0.0, 1.0);
        rgb = col * mix(0.3, 1.05, g) * (0.5 + 0.5 * lam) + col * edge * 0.25 * em;
    } else { // double outline
        float ring = (1.0 - smoothstep(uEdgeW * 2.3 - aa, uEdgeW * 2.3 + aa, e)) * smoothstep(uEdgeW * 1.6 - aa, uEdgeW * 1.6 + aa, e);
        rgb = col * (edge + ring * 0.8) * em * 1.2 + col * 0.06;
        a = max(max(edge, ring), 0.12 + 0.1 * uPale);
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
    gE = 1.0 - max(abs(gUV.x), abs(gUV.y)); // 1 at face center, 0 on the edge
    gAA = max(fwidth(gE) * 1.2, 1e-3);
    gEdge = 1.0 - smoothstep(uEdgeW - gAA, uEdgeW + gAA, gE);
    gEdgeGlow = gEdge + exp(-gE / max(uEdgeW, 0.01) * 2.5) * 0.35;

    if (kind == 1) { // ghost
        vec3 rgb = col * (gEdgeGlow * 0.9 * max(uEmissive, 0.5) + 0.06);
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
