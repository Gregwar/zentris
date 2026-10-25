#pragma once
// SYNTHWAVE's shaders (see synthwave.cpp). Everything is drawn in linear HDR (neon cores go above 1 and bloom),
// then tone-mapped, grained and lightly bled in the tape post.

namespace synthwave_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise1(float x) { float i = floor(x), f = fract(x); float u = f * f * (3.0 - 2.0 * f); return mix(hash11(i), hash11(i + 1.0), u); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
    return s;
}
// Ridged 1D noise: sharp peaks for the mountain ranges.
float ridge1(float x) {
    float s = 0.0, a = 0.55, f = 1.0;
    for (int i = 0; i < 5; i++) { s += a * (1.0 - abs(vnoise1(x * f) * 2.0 - 1.0)); f *= 2.07; a *= 0.47; }
    return s;
}
float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }
float sdRound(vec2 p, vec2 b, float r) { vec2 d = abs(p) - b + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }
)";

// ---------------------------------------------------------------- the world: sky, sun, mountains, floor

inline const char* WORLD_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uView;          // center xy, half extents (board units)
uniform float uHorizon;      // board y of the horizon
uniform vec4 uSun;           // center xy, radius, gain
uniform vec2 uBands;         // band scroll, breathe 0..1
uniform float uLv;           // 0 calm .. 2 peak (continuous)
uniform float uTime, uScroll, uPx;
uniform vec4 uKickAge, uKickAmp;
uniform float uCars, uLasers, uStars, uClouds, uFlash, uGridGain, uSlow;
uniform vec3 uSkyTop, uSkyMid, uSkyLow, uNight, uSunTop, uSunBot, uGrid, uAcc2, uMount, uFloor, uHaze;
uniform vec4 uWell;          // x0 y0 x1 y1 (board)
uniform vec4 uColL, uColR;   // HUD columns (board)
uniform float uBridge;
uniform vec3 uBridgeCol;
uniform float uSeed;

const float GA = 2.3, GB = 30.0; // floor grid: lateral and depth scales

float wMid() { return smoothstep(0.2, 1.0, uLv); }
float wPeak() { return smoothstep(1.1, 2.0, uLv); }

vec3 skyGrad(float t) {
    vec3 c = mix(uSkyLow, uSkyMid, smoothstep(0.0, 0.42, t));
    c = mix(c, uSkyTop, smoothstep(0.3, 1.0, t));
    // night drive: the upper sky deepens
    c = mix(c, uNight, wPeak() * 0.75 * smoothstep(0.15, 0.85, t));
    // dusk: a little dimmer overall
    c *= mix(0.78, 1.0, wMid());
    return c;
}

float starField(vec2 p, float scale, float density) {
    vec2 g = p * scale;
    vec2 id = floor(g);
    vec2 f = fract(g) - 0.5;
    float h = hash12(id + uSeed);
    if (h > density) return 0.0;
    vec2 o = (vec2(hash12(id + 3.1), hash12(id + 7.7)) - 0.5) * 0.7;
    float d = length(f - o) / scale;
    float size = mix(0.012, 0.05, pow(hash12(id + 1.3), 4.0));
    float tw = 0.55 + 0.45 * sin(uTime * (0.8 + 2.5 * hash12(id + 9.1)) + h * 60.0);
    float core = 1.0 - smoothstep(size, size + 1.5 * uPx, d);
    float glow = exp(-d / (size * 2.5 + 0.02)) * 0.25;
    return (core + glow) * tw * (0.4 + 1.6 * hash12(id + 5.5));
}

// The sun: emission and coverage (0 where the slit bands let the sky through).
vec3 sunAt(vec2 p, out float cover) {
    vec2 q = (p - uSun.xy) / uSun.z;
    float r = length(q);
    float aa = 1.5 * uPx / uSun.z;
    float disc = 1.0 - smoothstep(1.0 - aa, 1.0, r);
    float v = q.y;
    float vh = clamp((uHorizon - uSun.y) / uSun.z, -1.0, 0.9);
    float t = (v - vh) / (1.0 - vh);  // 0 at the horizon .. 1 at the crown
    float u = 0.56 - t;               // below the first band
    float gap = 0.0;
    if (u > 0.0) {
        float k = u * 9.0 * (1.0 - vh) * 0.5 + uBands.x;
        float f = fract(k);
        float fw = max(fwidth(k), 1e-4);
        float frac = clamp(0.08 + u * 0.95, 0.0, 0.62) * (0.8 + 0.5 * uBands.y);
        gap = (1.0 - smoothstep(frac - fw, frac + fw, f)) * smoothstep(0.0, fw * 1.5, f + fw * 0.5);
    }
    cover = disc * (1.0 - gap);
    float g = smoothstep(0.0, 1.0, t);
    vec3 c = mix(uSunBot, uSunTop, g * g * (0.6 + 0.4 * g));
    c *= 1.0 + 0.35 * smoothstep(0.6, 1.0, t); // a hotter crown
    return c * uSun.w;
}

float farRidge(float x) { return (0.9 + 2.6 * ridge1(x * 0.16 + 11.0 + uSeed * 0.37)) * smoothstep(3.0, 13.0, abs(x)); }
float nearRidge(float x) { return (0.25 + 1.5 * ridge1(x * 0.26 + 47.0 + uSeed * 0.21)) * smoothstep(7.0, 17.0, abs(x)); }

// Everything above the horizon (also seen mirrored in the floor).
vec3 skyCol(vec2 p, bool refl) {
    float H = uHorizon;
    float h = p.y - H;
    float top = uView.y + uView.w;
    float t = clamp(h / max(top - H, 1.0), 0.0, 1.0);
    vec3 col = skyGrad(t);
    float sunVis = 1.0;
    // stars: dusk and night
    if (!refl) {
        float st = starField(p, 1.5, 0.16) + 0.6 * starField(p + 31.7, 3.1, 0.12);
        col += vec3(0.85, 0.85, 1.0) * st * uStars * smoothstep(1.0, 6.0, h);
    }
    // lasers (peak): a fan from the vanishing point
    if (uLasers > 0.001) {
        vec2 d = p - vec2(0.0, H);
        float len = length(d);
        float ang = atan(d.y, d.x);
        float l = 0.0;
        for (int i = 0; i < 6; i++) {
            float fi = float(i);
            float a = 1.5708 + (fi - 2.5) * 0.36 + 0.22 * sin(uTime * (0.31 + 0.07 * fi) + fi * 1.7);
            float dist = abs(sin(ang - a)) * len;
            l += exp(-dist / (0.035 + len * 0.004)) * smoothstep(0.0, 4.0, len) * exp(-len * 0.05);
        }
        col += mix(uAcc2, uGrid, 0.35) * l * uLasers * 0.9;
    }
    // the sun and its halo
    float cover;
    vec3 sc = sunAt(p, cover);
    float r = length(p - uSun.xy) / uSun.z;
    col += uSunBot * exp(-max(r - 1.0, 0.0) * 3.0) * 0.35 * uSun.w * (1.0 - cover);
    col += uSunBot * exp(-max(r - 1.0, 0.0) * 0.9) * 0.10 * uSun.w;
    col = mix(col, sc, cover);
    // cloud streaks drifting across (heavier in some variants)
    if (uClouds > 0.001) {
        float n = fbm(vec2(p.x * 0.06 + uTime * 0.012, p.y * 0.42 + 3.0));
        float n2 = fbm(vec2(p.x * 0.11 - uTime * 0.02, p.y * 0.9 + 9.0));
        float band = smoothstep(1.2, 3.0, h) * (1.0 - smoothstep(7.5, 12.0, h));
        float m = smoothstep(0.5, 0.64, n * 0.7 + n2 * 0.45) * band * uClouds;
        float lit = smoothstep(0.6, 0.52, n * 0.7 + n2 * 0.45 - 0.05);
        vec3 cc = uMount * 0.7 + uSkyMid * 0.25 + uSunBot * 0.3 * lit * uSun.w;
        col = mix(col, cc, m * 0.92);
    }
    // the mountain ranges: far (hazy, wireframed) and near (dark)
    float hf = farRidge(p.x);
    if (h < hf) {
        float depth = hf - h;
        vec3 mc = mix(uMount, uSkyLow, 0.32) * 0.9;
        mc = mix(mc, uMount, smoothstep(0.0, 2.5, depth) * 0.6);
        // a wire mesh following the slopes
        float k1 = depth * 1.7, k2 = p.x * 0.9 + depth * 0.35;
        float w1 = abs(fract(k1) - 0.5) / max(fwidth(k1), 1e-4);
        float w2 = abs(fract(k2) - 0.5) / max(fwidth(k2), 1e-4);
        float wire = max(1.0 - smoothstep(0.5, 1.5, w1), 1.0 - smoothstep(0.5, 1.5, w2));
        mc += uGrid * wire * (0.10 + 0.35 * wMid()) * smoothstep(0.0, 0.6, depth) * exp(-depth * 0.35);
        mc += uSunTop * exp(-depth * 9.0) * 0.55; // the rim the sun lights
        col = mc;
    }
    float hn = nearRidge(p.x);
    if (h < hn) {
        float depth = hn - h;
        vec3 mc = uMount * 0.55;
        mc += uHaze * exp(-depth * 10.0) * 0.4;
        col = mc;
    }
    return col;
}

float kickPulse(float d) {
    float s = 0.0;
    for (int i = 0; i < 4; i++) {
        float a = uKickAge[i];
        if (a > 3.0) continue;
        float front = a * 16.0;
        float w = 0.25 + 0.12 * front;
        s += exp(-pow((d - front) / w, 2.0)) * exp(-a * 1.6) * uKickAmp[i];
    }
    return s;
}

// Light streaks of cars on the floor's lanes (peak): taillights racing to the horizon, headlights coming.
vec3 cars(float gx, float gz, float d) {
    vec3 acc = vec3(0.0);
    float fz = fwidth(gz);
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        float side = i < 2 ? -1.0 : 1.0;
        float lane = side * (i == 0 || i == 3 ? 6.2 : 4.4);
        bool away = (i == 1 || i == 3);        // inner lanes recede, outer ones come
        float S = 7.0 + 3.0 * fi;
        float v = away ? 9.0 + 2.0 * fi : 6.0;
        float head = (away ? uTime * v : -uTime * v) + fi * 13.0;
        float back = away ? (head - gz) : (gz - head);
        float cell = floor(back / S);
        back = (back / S - cell) * S;
        if (hash11(cell * 7.13 + fi * 3.1) > 0.75) continue;
        float lat = min(abs(gx - lane - 0.16), abs(gx - lane + 0.16)) * d / GA;
        float size = 0.04 + 0.09 * d / 9.0;
        float lw = exp(-pow(lat / size, 2.0));
        float trail = exp(-back * (away ? 0.45 : 1.2)) * 1.1 + exp(-back * 7.0) * 3.0;
        vec3 c = away ? vec3(1.0, 0.12, 0.10) : vec3(1.0, 0.82, 0.62);
        acc += c * lw * trail * (1.0 - smoothstep(S * 0.08, S * 0.3, fz));
    }
    return acc;
}

vec3 floorCol(vec2 p) {
    float H = uHorizon;
    float d = max(H - p.y, 1e-3);
    float gx = p.x * GA / d;
    float gz = GB / d + uScroll;
    float wm = wMid(), wp = wPeak();
    // the mirror: the sky and the sun reflected, broken by ripples
    float rip = sin(GB / d * 2.6 + uTime * 0.8) * 0.5 + sin(GB / d * 6.1 - uTime * 1.4) * 0.3;
    vec2 rp = vec2(p.x + rip * 0.05 * d, H + d * 0.85);
    vec3 refl = skyCol(rp, true);
    float fres = mix(0.06, 0.42, exp(-d * 0.55));
    float bars = 0.45 + 0.55 * smoothstep(0.08, 0.2, abs(fract(GB / d * 0.5 + uTime * 0.05) - 0.5));
    vec3 col = uFloor * (0.7 + 0.5 * exp(-d * 0.5)) + refl * fres * bars;
    // the grid
    vec2 g = vec2(gx, gz);
    vec2 fw = max(fwidth(g), vec2(1e-4));
    vec2 dist = abs(fract(g + 0.5) - 0.5) / fw;
    float lw = mix(0.6, 1.6, smoothstep(0.0, 9.0, d)) * max(1.0, 1.0 / (uPx * 45.0));
    float fadeX = 1.0 - smoothstep(0.12, 0.45, fw.x), fadeZ = 1.0 - smoothstep(0.12, 0.45, fw.y);
    float lx = (1.0 - smoothstep(lw * 0.5, lw * 0.5 + 1.0, dist.x)) * fadeX;
    float lz = (1.0 - smoothstep(lw * 0.5, lw * 0.5 + 1.0, dist.y)) * fadeZ;
    float bx = abs(fract(gx + 0.5) - 0.5) * d / GA;          // board units to the line
    float bz = abs(fract(gz + 0.5) - 0.5) * d * d / GB;
    float glow = exp(-bx * 7.0) * fadeX + exp(-bz * 7.0) * fadeZ;
    float pulse = kickPulse(d);
    float gain = uGridGain * (1.0 + 2.2 * pulse);
    float near = smoothstep(0.0, 0.9, d);
    col += uGrid * (max(lx, lz) * 1.5 + glow * 0.16) * gain * near;
    // the lit band of a kick runs across the floor
    col += uGrid * pulse * 0.10 * near;
    if (uCars > 0.001) col += cars(gx, gz, d) * uCars * 4.0;
    return col;
}

void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    float H = uHorizon;
    vec3 col = p.y >= H ? skyCol(p, false) : floorCol(p);
    float h = p.y - H;
    // the horizon's glow
    col += uHaze * (exp(-abs(h) * 1.1) * 0.35 + exp(-abs(h) * 7.0) * 0.8) * (0.7 + 0.5 * wMid());
    // a flash (a strong rise, lightning): the sky lights up
    col += (uSunTop * 0.35 + uHaze * 0.25) * uFlash * smoothstep(-3.0, 8.0, h);

    // quiet behind the HUD columns
    float cl = sdBox(p - (uColL.xy + uColL.zw) * 0.5, (uColL.zw - uColL.xy) * 0.5);
    float cr = sdBox(p - (uColR.xy + uColR.zw) * 0.5, (uColR.zw - uColR.xy) * 0.5);
    float cm = 1.0 - smoothstep(-0.6, 1.4, min(cl, cr));
    col *= 1.0 - 0.42 * cm;
    // and behind the corner where the universe's name is written
    vec2 corner = vec2(uView.x - uView.z, uView.y - uView.w);
    col *= 1.0 - 0.5 * exp(-length((p - corner) * vec2(0.09, 0.32)) * 2.2);
    // the dark glass well: a shadow around it, the world dimly seen through it
    vec2 wc = (uWell.xy + uWell.zw) * 0.5, wh = (uWell.zw - uWell.xy) * 0.5;
    float sw = sdBox(p - wc, wh);
    col *= 1.0 - 0.45 * exp(-max(sw, 0.0) * 0.9) * step(0.0, sw);
    float inside = 1.0 - smoothstep(-uPx, uPx, sw);
    inside *= 1.0 - smoothstep(uWell.w - 2.6, uWell.w, p.y) * 0.85;
    vec3 glass = col * 0.07 + uSkyTop * 0.10 + uNight * 0.08;
    // a faint cell grid and a soft sheen on the glass
    vec2 cg = abs(fract(p - wc + 0.5) - 0.5) / max(fwidth(p), vec2(1e-4));
    float cell = 1.0 - smoothstep(0.4, 1.4, min(cg.x, cg.y));
    glass += uGrid * cell * 0.018;
    glass += vec3(0.9, 0.85, 1.0) * 0.008 * smoothstep(0.6, 0.0, abs((p.x - wc.x) * 0.5 + (p.y - wc.y) * 0.18 - 1.2));
    col = mix(col, glass, inside);

    // the bridge (a change of universe): a VHS tracking dissolve, band by band, to the plain backdrop
    if (uBridge > 0.0) {
        float band = floor(gl_FragCoord.y / 7.0);
        float n = hash11(band * 1.37 + floor(uTime * 9.0) * 0.17);
        float n2 = hash11(floor(gl_FragCoord.y / 37.0) * 3.1);
        float w = smoothstep(0.0, 1.0, uBridge * 1.6 - 0.3 + (n * 0.3 + n2 * 0.3 - 0.3));
        col = mix(col, uBridgeCol, w);
    }
    frag = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- flat colored triangles (palms, OSD, panels)

inline const char* SHAPE_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aCol;
uniform mat4 uVP;
uniform int uPixels;   // 1: aPos in pixels (top-left origin)
uniform vec2 uRes;
out vec4 vCol;
void main() {
    vCol = aCol;
    if (uPixels == 1) gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
    else gl_Position = uVP * vec4(aPos, 0.0, 1.0);
}
)";
inline const char* SHAPE_FS = R"(#version 330 core
in vec4 vCol;
out vec4 frag;
void main() { frag = vCol; }
)";

// ---------------------------------------------------------------- tiles: the stack, the piece, previews, gauge

// a0: center xy, half size xy (board). a1: rgb, alpha. a2: kind, age, aux, seed. a3: extra.
inline const char* TILE_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
layout(location = 2) in vec4 a1;
layout(location = 3) in vec4 a2;
layout(location = 4) in vec4 a3;
uniform mat4 uVP;
out vec2 vLocal;   // board units from the center
out vec2 vWorld;
flat out vec4 v0, v1, v2, v3;
void main() {
    vec2 p = a0.xy + aCorner * a0.zw;
    vLocal = aCorner * a0.zw;
    vWorld = p;
    v0 = a0; v1 = a1; v2 = a2; v3 = a3;
    gl_Position = uVP * vec4(p, 0.0, 1.0);
}
)";
inline const char* TILE_FS = R"(#version 330 core
in vec2 vLocal;
in vec2 vWorld;
flat in vec4 v0, v1, v2, v3;
out vec4 frag;
uniform float uPx;           // board units per pixel
uniform vec3 uSkyTop, uSkyLow, uSunTop, uSunBot, uGrid, uAcc2;
uniform float uBridge;
uniform vec3 uBridgeInk;
uniform float uTime;

// The chrome ramp: a pale sky above a dark horizon line, warm ground below.
vec3 chrome(float t) {
    vec3 c = mix(vec3(0.95, 0.97, 1.0), mix(uSkyTop, vec3(0.25, 0.35, 0.9), 0.5) * 0.9, smoothstep(0.0, 0.5, t));
    c = mix(c, vec3(0.06, 0.03, 0.10), smoothstep(0.47, 0.52, t));
    c = mix(c, mix(uSunBot, uSunTop, smoothstep(0.55, 1.0, t)) * 1.1, smoothstep(0.52, 0.6, t));
    return c;
}

void main() {
    int kind = int(v2.x + 0.5);
    vec3 pc = v1.rgb;
    float alpha = v1.a;
    float age = v2.y;
    vec2 p = vLocal;
    float px = uPx;
    if (kind == 0 || kind == 3) {
        // A glossy neon tile with a chrome bevel.
        float s = v3.x;                       // cell scale (previews are smaller)
        vec2 b = vec2(0.46 * s);
        float sd = sdRound(p, b, 0.12 * s);
        float cov = 1.0 - smoothstep(-px * 0.7, px * 0.7, sd);
        if (cov <= 0.0) discard;
        float e = 0.10 * s;
        vec2 q = p / b;                       // -1..1
        float ty = 0.5 - 0.5 * q.y;           // 0 top .. 1 bottom
        // face: a deep-to-bright neon gradient, the sky reflected in its upper half, a crisp horizon line
        vec3 deep = pc * 0.22, bright = pc * 0.95 + 0.02;
        vec3 face = mix(bright, deep, smoothstep(0.0, 1.0, ty));
        float hz = 0.42 + 0.05 * q.x;
        vec3 skyRef = mix(mix(pc, vec3(1.0), 0.3), pc * 0.7, smoothstep(0.0, hz, ty));
        face = mix(face, skyRef, (1.0 - smoothstep(hz - 0.02, hz + 0.01, ty)) * 0.55);
        face *= 1.0 - 0.35 * (1.0 - smoothstep(0.0, 0.04, abs(ty - hz - 0.02)));
        // gloss: a soft diagonal streak, top left
        float gl = exp(-pow((q.x * 0.8 + q.y * 0.6 - 0.75) / 0.14, 2.0)) * smoothstep(0.2, 0.9, -q.x * 0.4 + q.y * 0.9 + 0.3);
        face += vec3(1.0) * gl * 0.22;
        // inner neon line
        float inner = exp(-pow((sd + e + 0.035 * s) / (0.018 * s + px), 2.0));
        face += pc * inner * 1.1;
        // chrome bevel, lit from the top left
        vec2 n = normalize(vec2(abs(p.x) > abs(p.y) ? sign(p.x) : 0.0, abs(p.y) >= abs(p.x) ? sign(p.y) : 0.0) + q * 0.5);
        float lit = dot(n, normalize(vec2(-0.45, 0.9))) * 0.5 + 0.5;
        vec3 bev = chrome(1.0 - lit) * mix(vec3(1.0), pc * 1.3 + 0.2, 0.35);
        float inBev = smoothstep(-e - px, -e + px, sd);
        vec3 col = mix(face, bev, inBev);
        col += vec3(1.0) * exp(-pow((sd + 0.012 * s) / (0.012 * s + px), 2.0)) * 0.35 * lit;
        if (kind == 0 && age >= 0.0) {
            // lock: the outline flares neon, a chrome glint sweeps across the piece
            float fl = exp(-age * 5.0);
            col += mix(pc, vec3(1.0), 0.5) * fl * (inBev * 1.6 + 0.35);
            float g = (vWorld.x - vWorld.y * 0.6) * 0.5 - (age * 9.0 - 3.0) + v2.z;
            col += vec3(1.0, 0.97, 0.92) * exp(-g * g / 0.05) * smoothstep(0.8, 0.0, age) * 1.6;
        }
        // the bridge: a plain block in the bridge's ink
        col = mix(col, uBridgeInk, smoothstep(0.2, 0.85, uBridge));
        frag = vec4(col * cov * alpha, cov * alpha);
        return;
    }
    if (kind == 1) {
        // Ghost: a thin neon outline, a breath of fill.
        vec2 b = vec2(0.44);
        float sd = sdRound(p, b, 0.1);
        float line = 1.0 - smoothstep(0.02, 0.02 + px * 1.2, abs(sd));
        float fill = (1.0 - smoothstep(-px, px, sd)) * 0.07;
        vec3 col = pc * (line * 1.1 + fill);
        float a = (line * 0.9 + fill) * alpha * (1.0 - uBridge);
        frag = vec4(col * alpha * (1.0 - uBridge), a);
        return;
    }
    if (kind == 2) {
        // A cleared cell sliding away as a streak of light: v3.x = direction, v3.y = progress 0..1.
        float dir = v3.x, k = v3.y;
        float hw = v0.z;
        // tile body at the leading end, a tail behind it
        float lead = dir * p.x;                 // along the motion, -hw..hw
        float body = 0.46;
        float tailLen = 2.0 * hw - 2.0 * body;
        float x = lead - (hw - 2.0 * body) + body; // >0 inside the body region
        float vy = abs(p.y);
        float cy = 1.0 - smoothstep(0.42 * (1.0 - 0.6 * k), 0.42 * (1.0 - 0.6 * k) + px, vy);
        float along = clamp((lead + hw) / max(2.0 * hw, 1e-3), 0.0, 1.0); // 0 tail end .. 1 head
        float core = exp(-vy * vy / (0.012 + 0.02 * (1.0 - k)));
        vec3 hot = mix(pc, vec3(1.0), 0.55);
        vec3 col = hot * (pow(along, 2.5) * 2.4 * cy + core * pow(along, 1.2) * 1.5);
        float a = (pow(along, 2.5) * cy + core * along * 0.5) * alpha * (1.0 - k * k);
        frag = vec4(col * alpha * (1.0 - k * k) * (1.0 - uBridge), clamp(a, 0.0, 1.0) * (1.0 - uBridge));
        return;
    }
    if (kind == 4) {
        // The bonus gauge: a little sun rising as lines are cleared (v3.x level, v3.y ready, v3.z active).
        float R = v3.w;
        vec2 q = p / R;
        float r = length(q);
        float aa = px / R * 1.5;
        float disc = 1.0 - smoothstep(1.0 - aa, 1.0, r);
        float lvl = v3.x;
        float fillY = -1.0 + 2.0 * lvl;
        float filled = 1.0 - smoothstep(fillY - aa, fillY + aa, q.y);
        float u = 0.3 - q.y;
        float gap = 0.0;
        if (u > 0.0) {
            float f = fract(u * 3.2 - uTime * 0.15 * (0.5 + v3.y));
            float fr = clamp(u * 0.3, 0.0, 0.5);
            gap = 1.0 - smoothstep(fr - 0.05, fr + 0.05, f);
        }
        vec3 sunCol = mix(uSunBot, uSunTop, smoothstep(-0.9, 0.9, q.y));
        if (v3.z > 0.5) sunCol = mix(uAcc2, vec3(1.0), smoothstep(-0.9, 0.9, q.y) * 0.6);
        float pulse = v3.y * (0.75 + 0.25 * sin(uTime * 4.0));
        filled *= disc;
        vec3 col = sunCol * (1.4 + 1.2 * pulse) * filled * (1.0 - gap);
        float ring = exp(-pow((r - 1.0) / (0.035 + aa), 2.0));
        vec3 rc = mix(uGrid, uSunTop, v3.y);
        col += rc * ring * (0.9 + 1.2 * pulse);
        col += mix(uGrid, uSunBot, v3.y) * exp(-max(r - 1.0, 0.0) * 6.0) * (0.15 + 0.5 * pulse) * (1.0 - disc);
        float a = max(disc * mix(0.55, 1.0, filled * (1.0 - gap)), ring);
        vec3 glassC = uSkyTop * 0.15;
        col = mix(glassC, col, max(filled * (1.0 - gap), ring)) * disc + col * (1.0 - disc);
        col = mix(col, uBridgeInk * disc, uBridge);
        frag = vec4(col * alpha, a * alpha);
        return;
    }
    discard;
}
)";

// ---------------------------------------------------------------- neon lines and star glints (additive)

// a0: a.xy, b.xy (board). a1: rgb intensity. a2: width (board), glow (board), kind, alpha.
inline const char* LINE_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
layout(location = 2) in vec4 a1;
layout(location = 3) in vec4 a2;
uniform mat4 uVP;
out vec2 vP;
flat out vec4 v0, v1, v2;
void main() {
    vec2 a = a0.xy, b = a0.zw;
    float ext = a2.x * 0.5 + a2.y * 3.0;
    vec2 p;
    if (int(a2.z + 0.5) == 1) {
        p = a + aCorner * a2.x;             // a star: center a, radius = width
    } else {
        vec2 d = b - a;
        float l = length(d);
        vec2 t = l > 1e-5 ? d / l : vec2(1.0, 0.0);
        vec2 n = vec2(-t.y, t.x);
        float u = aCorner.x * 0.5 + 0.5;
        p = mix(a - t * ext, b + t * ext, u) + n * aCorner.y * ext;
    }
    vP = p;
    v0 = a0; v1 = a1; v2 = a2;
    gl_Position = uVP * vec4(p, 0.0, 1.0);
}
)";
inline const char* LINE_FS = R"(#version 330 core
in vec2 vP;
flat in vec4 v0, v1, v2;
out vec4 frag;
uniform float uPx;
void main() {
    int kind = int(v2.z + 0.5);
    vec3 c = v1.rgb;
    float alpha = v2.w;
    if (kind == 1) {
        // a four-pointed glint: two thin rays and a hot core
        vec2 q = (vP - v0.xy) / v2.x;
        float r = length(q);
        float rays = exp(-abs(q.y) * 40.0) * (1.0 - smoothstep(0.0, 1.0, abs(q.x)))
                   + exp(-abs(q.x) * 40.0) * (1.0 - smoothstep(0.0, 1.0, abs(q.y)));
        float diag = (exp(-abs(q.x - q.y) * 50.0) + exp(-abs(q.x + q.y) * 50.0)) * (1.0 - smoothstep(0.0, 0.45, r)) * 0.4;
        float core = exp(-r * r / 0.004) * 2.0 + exp(-r * 7.0) * 0.4;
        frag = vec4(c * (rays + diag + core) * alpha, 0.0);
        return;
    }
    vec2 a = v0.xy, b = v0.zw;
    vec2 pa = vP - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    float d = length(pa - ba * h);
    float w = v2.x * 0.5;
    float core = 1.0 - smoothstep(w, w + uPx, d);
    float glow = exp(-d / max(v2.y, 1e-4));
    // white-hot core, colored glow
    vec3 col = mix(c, vec3(length(c) * 0.6), 0.45) * core + c * glow * 0.55;
    frag = vec4(col * alpha, 0.0);
}
)";

// ---------------------------------------------------------------- text (signed distance field glyphs)

inline const char* TEXT_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;   // pixels, top-left origin
layout(location = 1) in vec2 aUv;
layout(location = 2) in float aLy;   // 0 at the cap line .. 1 at the baseline
uniform vec2 uRes;
out vec2 vUv;
out float vLy;
void main() {
    vUv = aUv;
    vLy = aLy;
    gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
}
)";
inline const char* TEXT_FS = R"(#version 330 core
in vec2 vUv;
in float vLy;
out vec4 frag;
uniform sampler2D uFont;
uniform int uMode;          // 0 chrome, 1 neon, 2 plain
uniform vec3 uColor, uGlow, uOutline, uSkyTop, uSunTop, uSunBot;
uniform float uAlpha, uGlowAmt, uOutlineW, uBias, uBridge;
uniform vec3 uBridgeInk;
vec3 chrome(float t) {
    vec3 c = mix(vec3(1.0, 1.0, 1.0), mix(uSkyTop, vec3(0.30, 0.45, 1.0), 0.6) * 1.1, smoothstep(0.05, 0.52, t));
    c = mix(c, vec3(0.07, 0.03, 0.12), smoothstep(0.50, 0.54, t));
    c = mix(c, mix(uSunBot, uSunTop, smoothstep(0.58, 1.05, t)) * 1.25, smoothstep(0.54, 0.64, t));
    return c;
}
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.75;
    float fill = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    float outl = smoothstep(0.5 - uBias - uOutlineW - w, 0.5 - uBias - uOutlineW + w, d);
    float glow = smoothstep(0.0, 0.5, d);
    glow = glow * glow * uGlowAmt;
    vec3 fc;
    if (uMode == 0) fc = chrome(vLy);
    else if (uMode == 1) fc = mix(uColor, vec3(1.0), 0.55 * smoothstep(0.5, 0.75, d)) * 1.25;
    else fc = uColor;
    fc = mix(fc, uBridgeInk, uBridge);
    vec3 col = mix(uOutline, fc, fill);
    float a = max(fill, outl);
    // premultiplied: the solid letters, then the additive glow
    vec3 outc = col * a + uGlow * glow * (1.0 - a) * (1.0 - uBridge);
    frag = vec4(outc * uAlpha, a * uAlpha);
}
)";

// ---------------------------------------------------------------- bloom and the tape post

inline const char* DOWN_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uTex;
uniform vec2 uTexel;
uniform float uThreshold;   // > 0 on the first step only
void main() {
    vec3 c = texture(uTex, vUv + vec2(-uTexel.x, -uTexel.y)).rgb + texture(uTex, vUv + vec2(uTexel.x, -uTexel.y)).rgb
           + texture(uTex, vUv + vec2(-uTexel.x, uTexel.y)).rgb + texture(uTex, vUv + vec2(uTexel.x, uTexel.y)).rgb;
    c *= 0.25;
    if (uThreshold > 0.0) {
        float m = max(c.r, max(c.g, c.b));
        c *= max(m - uThreshold, 0.0) / max(m, 1e-4);
    }
    frag = vec4(c, 1.0);
}
)";
inline const char* UP_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uTex;
uniform vec2 uTexel;
void main() {
    vec3 c = texture(uTex, vUv).rgb * 4.0;
    c += texture(uTex, vUv + vec2(uTexel.x, 0)).rgb * 2.0 + texture(uTex, vUv - vec2(uTexel.x, 0)).rgb * 2.0;
    c += texture(uTex, vUv + vec2(0, uTexel.y)).rgb * 2.0 + texture(uTex, vUv - vec2(0, uTexel.y)).rgb * 2.0;
    c += texture(uTex, vUv + uTexel).rgb + texture(uTex, vUv - uTexel).rgb;
    c += texture(uTex, vUv + vec2(uTexel.x, -uTexel.y)).rgb + texture(uTex, vUv + vec2(-uTexel.x, uTexel.y)).rgb;
    frag = vec4(c / 16.0, 1.0);
}
)";

inline const char* POST_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene, uBloom;
uniform vec2 uRes;
uniform float uTime, uFade, uDim, uSlow, uBridge, uBloomAmt, uExposure;
uniform float uRewind, uStatic, uSqueeze;   // game over: tape rewinding, static, the picture collapsing
uniform vec4 uWellUv;                       // the well (uv): the tape's tracking leaves it alone
uniform vec3 uSlowTint;
float bandNoise(float y, float t) { return hash11(floor(y) * 1.13 + floor(t) * 7.7); }
void main() {
    vec2 uv = vUv;
    float t = uTime;
    // the picture collapsing to a line (tape stopped)
    if (uSqueeze > 0.0) {
        uv.y = 0.5 + (uv.y - 0.5) / max(1.0 - uSqueeze * 0.985, 0.015);
        if (uv.y < 0.0 || uv.y > 1.0) { frag = vec4(vec3(0.0), 1.0); return; }
    }
    float yPix = uv.y * uRes.y;
    // tracking: a band rolling up the picture (slow motion), many while rewinding, a few on the bridge
    float shift = 0.0;
    float trackBand = 0.0;
    {
        float pos = fract(t * 0.07) * 1.4 - 0.2;
        float b = exp(-pow((uv.y - pos) / 0.035, 2.0));
        trackBand = b * uSlow;
        float jag = (vnoise(vec2(yPix * 0.35, t * 30.0)) - 0.5);
        shift += b * uSlow * jag * 0.012;
        shift += (bandNoise(yPix / 3.0, t * 24.0) - 0.5) * 0.04 * uRewind * step(0.55, vnoise(vec2(yPix * 0.02, t * 6.0)));
        shift += uRewind * 0.012 * sin(uv.y * 30.0 + t * 40.0);
    }
    float inWell = step(uWellUv.x, uv.x) * step(uv.x, uWellUv.z) * step(uWellUv.y, uv.y) * step(uv.y, uWellUv.w);
    shift *= mix(1.0, 0.15, inWell);
    uv.x += shift;
    // chroma bleed: the tape smears red and blue sideways a little
    float bleed = (0.9 + 2.5 * uSlow + 5.0 * uRewind) / uRes.x * (1.0 - uBridge);
    vec3 c;
    c.r = texture(uScene, uv + vec2(bleed, 0.0)).r;
    c.g = texture(uScene, uv).g;
    c.b = texture(uScene, uv - vec2(bleed, 0.0)).b;
    c += texture(uBloom, uv).rgb * uBloomAmt * mix(1.0, 0.45, inWell);
    vec3 raw = c; // the bridge is shown as is (both universes meet on the same color)
    // slow motion: a desaturated cyan night, the tape paused
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(c, l * uSlowTint * 1.25 + c * 0.12, uSlow * 0.72);
    c += vec3(0.6, 0.8, 0.9) * trackBand * 0.25 * (0.5 + 0.5 * vnoise(vec2(uv.x * 200.0, t * 40.0)));
    // tone: soft shoulder per channel, neon cores roll into white
    c *= uExposure;
    vec3 over = max(c - 1.0, 0.0);
    c += (over.r + over.g + over.b) * 0.16;
    const float K = 0.62;
    c = mix(c, K + (1.0 - K) * (1.0 - exp(-(c - K) / (1.0 - K))), step(K, c));
    // vignette
    vec2 q = vUv - 0.5;
    c *= 1.0 - 0.35 * dot(q, q) * 1.6 * (1.0 - uBridge);
    c = mix(c, raw, uBridge);
    // static (game over)
    if (uStatic > 0.0) {
        float n = hash12(floor(gl_FragCoord.xy / 2.0) + floor(t * 50.0) * 13.1);
        float lines = 0.85 + 0.15 * sin(yPix * 1.3 + t * 80.0);
        c = mix(c, vec3(n * 0.9 + 0.05) * vec3(0.85, 0.9, 1.0) * lines, uStatic);
    }
    // film grain, restrained
    float g = hash12(gl_FragCoord.xy + fract(t * 7.13) * 431.0) - 0.5;
    c += g * 0.022 * (1.0 - uBridge * 0.8);
    c *= 1.0 - uDim * 0.55;
    c *= uFade;
    frag = vec4(pow(max(c, 0.0), vec3(1.0 / 2.2)), 1.0);
}
)";

} // namespace synthwave_shaders
