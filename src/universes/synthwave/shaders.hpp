#pragma once
// SYNTHWAVE's shaders (see synthwave.cpp). Everything is drawn in linear HDR (the sun, the lights and the neon
// rims go above 1), then graded like film in the post: bloom with a little halation, anamorphic streaks, a filmic
// curve with rich blacks and a soft highlight rolloff, a fine grain.

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
float fbm3(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
    return s / 0.875;
}
float ridge1(float x) {
    float s = 0.0, a = 0.55, f = 1.0;
    for (int i = 0; i < 4; i++) { s += a * (1.0 - abs(vnoise1(x * f) * 2.0 - 1.0)); f *= 2.07; a *= 0.47; }
    return s;
}
float sq(float x) { return x * x; }
float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }
float sdRound(vec2 p, vec2 b, float r) { vec2 d = abs(p) - b + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }
)";

// ---------------------------------------------------------------- the world: sky, sun, haze, the wet floor

inline const char* WORLD_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uView;          // center xy, half extents (board units)
uniform float uHorizon;      // board y of the horizon
uniform vec4 uSun;           // center xy, radius, gain
uniform float uMoon;         // 1: a cold moon (smaller, crisper, higher)
uniform float uLv;           // 0 calm .. 2 peak (continuous)
uniform float uTime, uScroll, uPx;
uniform vec4 uKickAge, uKickAmp;
uniform float uCars, uRain, uSkyline, uStars, uFog, uHazeLit, uGridGain, uBars, uTrail;
uniform vec3 uSkyTop, uHaze, uSunCore, uSunEdge, uFloor, uGrid, uAcc;
uniform vec4 uWell;          // x0 y0 x1 y1 (board)
uniform vec4 uColL, uColR;   // HUD columns (board)
uniform float uRail;         // the well's rails: brightness
uniform float uBridge;
uniform vec3 uBridgeCol;
uniform float uSeed;
uniform vec3 uKickCol[4];    // each pulse's color (the peak changes it bar to bar)
uniform float uPulseSpeed;   // how fast the pulses run into the fog
uniform float uPulseGlow;    // the light band of a pulse on the wet road
uniform float uStreaks;      // the peak's speed dashes on the road (highs make more of them)
uniform float uHighs;
uniform float uQuiet;        // how much the world dims behind the HUD columns (more at the peak)

const float GA = 1.15, GB = 18.0; // floor: lateral and depth scales (few, wide lines)

float wMid() { return smoothstep(0.2, 1.0, uLv); }
float wPeak() { return smoothstep(1.1, 2.0, uLv); }

// The light of the sun: its soft disc (cover) and its glow, sharpened or softened (the floor sees it blurred).
vec3 sunLight(vec2 p, float soft, out float cover) {
    vec2 q = (p - uSun.xy) / uSun.z;
    float r = length(q);
    float edge = mix(0.09, 0.015, uMoon) + soft;
    cover = 1.0 - smoothstep(1.0 - edge, 1.0 + edge, r);
    // the disc: a hot core rolling to the limb, the lower half reddened by the air
    vec3 c = mix(uSunEdge, uSunCore, pow(1.0 - smoothstep(0.0, 1.05, r), 0.6));
    c = mix(c, uSunEdge * 1.1, smoothstep(0.1, -1.0, q.y) * 0.55 * (1.0 - uMoon));
    if (uMoon > 0.5) c *= 0.88 + 0.12 * smoothstep(0.35, 0.65, fbm3(q * 2.3 + 4.0)); // faint maria
    vec3 disc = c * mix(0.7 + 0.8 * pow(max(1.0 - r * r, 0.0), 1.5) + 1.6 * exp(-r * r * 9.0), 1.6, uMoon) * cover;
    float rr = max(r - 1.0, 0.0);
    vec3 glow = uSunEdge * (exp(-rr * 3.0) * 0.45 + exp(-rr * 0.8) * 0.12 + exp(-rr * 0.2) * 0.025) * (1.0 - cover);
    return (disc + glow) * uSun.w;
}

// Two thin strata crossing the low sky (the only bands near the sun): darker against the glow, a lit lining.
float strata(vec2 p) {
    float h = p.y - uHorizon;
    float s = 0.0;
    for (int i = 0; i < 2; i++) {
        float fi = float(i);
        float y0 = mix(1.1, 2.6, fi) + 0.35 * (vnoise1(p.x * 0.05 + fi * 7.0 + uSeed) - 0.5) + uSun.y - uHorizon - 0.2;
        float th = (0.07 + 0.09 * vnoise1(p.x * 0.11 + fi * 3.0 + uTime * 0.01)) * mix(1.0, 1.8, fi);
        float m = exp(-sq((h - y0) / th)) * smoothstep(0.25, 0.7, vnoise1(p.x * 0.07 - uTime * 0.008 + fi * 11.0));
        s += m * (0.55 - 0.25 * fi);
    }
    return s;
}

float farHills(float x) { return (0.25 + 0.9 * ridge1(x * 0.11 + 11.0 + uSeed * 0.37)) * smoothstep(2.0, 16.0, abs(x)); }

vec3 skyCol(vec2 p, bool refl) {
    float H = uHorizon;
    float h = p.y - H;
    float top = uView.y + uView.w;
    float t = clamp(h / max(top - H, 1.0), 0.0, 1.0);
    // the night: near black above, the haze's color only low on the horizon
    vec3 col = uSkyTop * (1.0 - 0.5 * t) + uHaze * (exp(-h * 0.38) * 0.75 + exp(-h * 0.1) * 0.05);
    if (!refl && uStars > 0.001) {
        vec2 g = p * 1.1;
        vec2 id = floor(g), f = fract(g) - 0.5;
        float hs = hash12(id + uSeed);
        if (hs < 0.035) {
            vec2 o = (vec2(hash12(id + 3.1), hash12(id + 7.7)) - 0.5) * 0.7;
            float d = length(f - o) / 1.1;
            float tw = 0.6 + 0.4 * sin(uTime * (0.5 + 1.5 * hash12(id + 9.1)) + hs * 90.0);
            col += vec3(0.8, 0.85, 1.0) * exp(-d * d / (uPx * uPx * 1.2)) * tw * 0.5 * uStars * smoothstep(3.0, 9.0, h);
        }
    }
    float cover;
    vec3 sun = sunLight(p, refl ? 0.25 : 0.0, cover);
    float st = refl ? 0.0 : strata(p);
    col = col * (1.0 - cover) + sun * (1.0 - 0.6 * st * cover);
    // the strata: dim bands, their lower edge lit by the sun behind them
    float sd = length(p - uSun.xy) / uSun.z;
    col = mix(col, col * 0.55 + uSunEdge * 0.06 * uSun.w * exp(-max(sd - 1.0, 0.0) * 0.6), st * (1.0 - cover) * 0.7);
    // distant hills, nearly lost in the haze
    float hf = farHills(p.x) * (1.0 - uSkyline);
    if (h < hf) col = mix(col, uSkyTop * 0.5 + uHaze * 0.35, 0.8 * smoothstep(0.0, 0.05, hf - h));
    // a city on the horizon: blocks and a few lit windows
    if (uSkyline > 0.001) {
        float bx = floor(p.x * 1.6);
        float bh = (0.25 + 1.5 * pow(hash11(bx * 1.37 + uSeed), 2.2)) * smoothstep(1.5, 9.0, abs(p.x)) * (1.0 - smoothstep(16.0, 24.0, abs(p.x)));
        if (h < bh) {
            vec3 b = uSkyTop * 0.4 + uHaze * 0.22;
            vec2 w = vec2(p.x * 9.0, h * 7.0);
            float lit = step(0.78, hash12(floor(w) + bx)) * step(0.25, fract(w.x)) * step(0.3, fract(w.y));
            b += mix(uAcc, vec3(1.0, 0.8, 0.55), hash12(floor(w) * 1.3)) * lit * 0.35 * (refl ? 0.6 : 1.0);
            col = mix(col, b, uSkyline);
        }
    }
    return col;
}

float kickPulse(float z, out vec3 pc) {
    float s = 0.0;
    pc = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        float a = uKickAge[i];
        if (a > 3.0) continue;
        float front = a * uPulseSpeed;
        float w = exp(-sq((z - front) / (0.4 + 0.5 * front))) * exp(-a * 1.3) * uKickAmp[i];
        s += w;
        pc += uKickCol[i] * w;
    }
    return s;
}

// Light trails of cars (peak): tail lights receding, headlights coming, long exposures on the lanes.
vec3 cars(float gx, float zr, float d) {
    vec3 acc = vec3(0.0);
    float fz = fwidth(zr);
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        float side = i < 2 ? -1.0 : 1.0;
        bool away = (i == 1 || i == 2);
        float lane = side * (away ? 1.7 : 2.35);
        float S = 9.0 + 4.0 * fi;
        float v = away ? 4.0 + 1.5 * fi : 7.0;
        float head = (away ? uTime * v : -uTime * v) + fi * 13.0;
        float back = away ? (head - zr) : (zr - head);
        float cell = floor(back / S);
        back = (back / S - cell) * S;
        if (hash11(cell * 7.13 + fi * 3.1) > 0.7) continue;
        float lat = min(abs(gx - lane - 0.06), abs(gx - lane + 0.06)) * d / GA;
        float size = 0.02 + 0.012 * d;
        float lw = exp(-sq(lat / size));
        float len = (away ? 0.35 : 0.9) / mix(1.0, 3.5, uTrail);
        float trail = exp(-back * len) * 0.8 + exp(-back * 6.0) * 0.6;
        vec3 c = away ? vec3(1.0, 0.08, 0.06) : vec3(1.0, 0.86, 0.7);
        acc += c * lw * trail * (1.0 - smoothstep(S * 0.1, S * 0.35, fz));
    }
    return acc;
}

vec3 hazeCol(vec2 p) {
    float sx = abs(p.x - uSun.x);
    return uHaze * 0.55 + uSunEdge * uSun.w * (exp(-sx * 0.16) * 0.28 + 0.03);
}

vec3 floorCol(vec2 p) {
    float H = uHorizon;
    float d = max(H - p.y, 1e-3);
    float zr = GB / d;                 // depth (not scrolled)
    float gx = p.x * GA / d;
    float gz = zr + uScroll;
    float vis = exp(-zr * uFog * 0.085); // how much of the floor the fog lets through
    // wet asphalt: dark, with long streaky reflections
    float wet = vnoise(vec2(p.x * 2.4, zr * 0.35 + uScroll * 0.35));
    vec3 col = uFloor * (0.55 + 0.45 * wet);
    float rip = (vnoise(vec2(p.x * 0.8, gz * 2.0)) - 0.5);
    vec2 rp = vec2(p.x + rip * 0.25 * d * 0.3, H + d * 0.8);
    float fres = 0.18 + 0.6 * exp(-d * 0.3);
    col += skyCol(rp, true) * fres * (0.65 + 0.5 * wet);
    // the sun's long reflection: a column broken into bright streaks
    float sx = abs(p.x - uSun.x) / (uSun.z * (0.45 + 0.07 * d));
    float brk = smoothstep(0.25, 0.85, vnoise(vec2(p.x * 1.5, gz * 3.0 + uTime * 0.3)));
    col += uSunEdge * mix(1.0, 1.6, brk) * exp(-sx * sx * 2.0) * exp(-d * 0.22) * 0.9 * uSun.w * (0.5 + 0.8 * brk);
    col += uSunCore * exp(-sx * sx * 9.0) * exp(-d * 0.5) * brk * 0.8 * uSun.w;
    // the grid: few, fine lines, gone in the fog
    vec2 g = vec2(gx * 0.5, gz);   // a line every other lane
    vec2 fw = max(fwidth(g), vec2(1e-4));
    vec2 dist = abs(fract(g + 0.5) - 0.5) / fw;
    float fadeX = 1.0 - smoothstep(0.08, 0.3, fw.x), fadeZ = 1.0 - smoothstep(0.08, 0.3, fw.y);
    float lw = max(0.8, 1.0 / (uPx * 60.0));
    float lx = (1.0 - smoothstep(lw * 0.5, lw * 0.5 + 1.0, dist.x)) * fadeX;
    float lz = (1.0 - smoothstep(lw * 0.5, lw * 0.5 + 1.0, dist.y)) * fadeZ;
    float bx = abs(fract(gx * 0.5 + 0.5) - 0.5) * 2.0 * d / GA;
    float glow = exp(-bx * 14.0) * fadeX * 0.35;
    vec3 pcol;
    float pulse = kickPulse(zr, pcol);
    float near = smoothstep(0.0, 1.2, d) * mix(0.18, 1.0, smoothstep(8.0, 2.5, d));
    float gain = uGridGain * vis;
    col += (uGrid + 2.5 * pcol) * (lx * 0.75 + lz * 0.22 + glow) * gain * near;
    col += pcol * uPulseGlow * vis * near;
    // the peak: speed dashes on the road (reflections streaking past), more of them with the highs
    if (uStreaks > 0.001) {
        float lg = gx * 2.0;
        float lane = floor(lg);
        float hl = hash11(lane * 3.71 + 1.3);
        float seg = fract(gz * 0.09 + hl * 7.0);
        float cell = floor(gz * 0.09 + hl * 7.0);
        float on = step(hash12(vec2(lane, cell)), 0.25 + 0.5 * uHighs) * step(0.4, hl);
        float dash = smoothstep(0.0, 0.03, seg) * (1.0 - smoothstep(0.03, 0.45, seg));
        float lat = abs(fract(lg) - 0.5) / max(fwidth(lg), 1e-4);
        float lw2 = 1.0 - smoothstep(0.6, 1.8, lat);
        vec3 sc = mix(uGrid, vec3(1.0), 0.35 + 0.3 * hash11(lane));
        col += sc * lw2 * dash * on * uStreaks * 0.9 * vis * smoothstep(0.3, 1.5, d) * (1.0 - smoothstep(0.1, 0.3, fwidth(lg)));
    }
    if (uCars > 0.001) col += cars(gx, zr, d) * uCars * 5.0 * mix(1.0, vis, 0.5);
    // fog over the far floor
    col = mix(col, hazeCol(p) * (0.6 + 0.6 * uHazeLit), (1.0 - vis) * 0.85);
    return col;
}

void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    float H = uHorizon;
    vec3 col = p.y >= H ? skyCol(p, false) : floorCol(p);
    float h = p.y - H;
    // the haze on the horizon: a lit veil drifting, thicker with uFog, lit up by the peaks
    float drift = fbm3(vec2(p.x * 0.07 + uTime * 0.015, h * 0.45 + 2.0));
    float veil = exp(-abs(h + 0.3) * mix(0.75, 0.45, uFog * 0.5)) * (0.55 + 0.7 * drift);
    col += hazeCol(p) * veil * (0.35 + 0.25 * uFog) * (0.75 + 0.9 * uHazeLit);
    // rain (some variants): fine slanted streaks, lit where the light is
    if (uRain > 0.001) {
        vec2 rp = vec2(p.x + p.y * 0.12, p.y);
        float col0 = floor(rp.x * 5.0);
        float sp = 20.0 + 8.0 * hash11(col0);
        float y = rp.y * 0.9 + uTime * sp * 0.12 + hash11(col0 * 3.7) * 40.0;
        float seg = fract(y * 0.35);
        float dx = abs(fract(rp.x * 5.0) - 0.5) / 5.0;
        float r = exp(-dx * dx / (uPx * uPx * 0.35)) * smoothstep(0.0, 0.05, seg) * (1.0 - smoothstep(0.05, 0.4, seg));
        r *= step(0.7, hash11(col0 * 1.9 + floor(y * 0.35)));
        float lit = exp(-length(p - uSun.xy) * 0.09) + 0.15;
        col += hazeCol(p) * r * uRain * 0.35 * lit;
    }

    // quiet behind the HUD columns and the corner where the universe's name is written
    float cl = sdBox(p - (uColL.xy + uColL.zw) * 0.5, (uColL.zw - uColL.xy) * 0.5);
    float cr = sdBox(p - (uColR.xy + uColR.zw) * 0.5, (uColR.zw - uColR.xy) * 0.5);
    col *= 1.0 - uQuiet * (1.0 - smoothstep(-0.6, 1.6, min(cl, cr)));
    vec2 corner = vec2(uView.x - uView.z, uView.y - uView.w);
    col *= 1.0 - (0.45 + 0.7 * (uQuiet - 0.35)) * exp(-length((p - corner) * vec2(0.09, 0.32)) * 2.2);

    // the well: smoked frosted glass, the world dim through it, the sun's light diffused in it
    vec2 wc = (uWell.xy + uWell.zw) * 0.5, wh = (uWell.zw - uWell.xy) * 0.5;
    float sw = sdBox(p - wc, wh);
    float inside = 1.0 - smoothstep(-uPx, uPx, sw);
    float topFade = smoothstep(uWell.w - 2.2, uWell.w, p.y);
    inside *= 1.0 - topFade * 0.9;
    vec3 glass = col * 0.18 + uSkyTop * 0.25 + uHaze * 0.04;
    glass += uSunEdge * exp(-abs(p.x - uSun.x) * 0.12 - abs(p.y - uSun.y) * 0.15) * 0.05 * uSun.w;
    vec2 cg = abs(fract(p - wc + 0.5) - 0.5) / max(fwidth(p), vec2(1e-4));
    glass += uGrid * (1.0 - smoothstep(0.3, 1.2, min(cg.x, cg.y))) * 0.006;
    col = mix(col, glass, inside);
    // its rails: hairlines lit from below, fading upward
    float rx = abs(abs(p.x - wc.x) - wh.x) / uPx;
    float ry = (p.y - uWell.y) / (uWell.w - uWell.y);
    float rail = (1.0 - smoothstep(0.5, 1.6, rx)) * step(uWell.y - uPx, p.y) * step(p.y, uWell.w - 1.0)
               * mix(1.0, 0.12, smoothstep(0.0, 0.9, ry));
    float base = (1.0 - smoothstep(0.5, 1.6, abs(p.y - uWell.y) / uPx)) * step(abs(p.x - wc.x), wh.x);
    col += uGrid * (rail + base * 1.2) * uRail;
    col += uGrid * exp(-abs(p.y - uWell.y) * 3.0) * step(abs(p.x - wc.x), wh.x + 0.3) * step(p.y, uWell.y) * 0.05 * uRail;

    // the bridge (a change of universe): the fog takes the world, in drifting folds
    if (uBridge > 0.0) {
        float n = fbm3(p * 0.16 + vec2(uTime * 0.05, 0.0));
        float w = smoothstep(n - 0.3, n + 0.3, uBridge * 1.6 - 0.3);
        col = mix(col, uBridgeCol, w);
    }
    // letterbox (slow motion)
    float bh = 0.075 * uBars;
    if (vUv.y < bh || vUv.y > 1.0 - bh) col = vec3(0.0);
    frag = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- flat colored triangles (panels)

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
void main() { frag = vec4(vCol.rgb * vCol.a, vCol.a); }
)";

// ---------------------------------------------------------------- tiles: the stack, the piece, previews, gauge

// a0: center xy, half size xy (board). a1: rgb, alpha. a2: kind, age, aux, 0. a3: extra.
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
uniform vec3 uSkyTop, uSunCore, uSunEdge, uGrid, uAcc;
uniform float uBridge;
uniform vec3 uBridgeInk;
uniform float uTime;

void main() {
    int kind = int(v2.x + 0.5);
    vec3 pc = v1.rgb;
    float alpha = v1.a;
    float age = v2.y;
    vec2 p = vLocal;
    float px = uPx;
    if (kind == 0 || kind == 3) {
        // Smoked frosted glass lit from inside: a dark tinted face, a soft inner glow, a thin bright rim.
        // v3.x: cell scale (previews are smaller), v3.y: light (1 on, 0 off: the lights going out).
        float s = v3.x;
        float on = v3.y;
        vec2 b = vec2(0.455 * s);
        float sd = sdRound(p, b, 0.07 * s);
        float cov = 1.0 - smoothstep(-px * 0.6, px * 0.6, sd);
        if (cov <= 0.0) discard;
        vec2 q = p / b;
        float live = kind == 3 ? 1.0 : 0.82;   // the falling piece burns a little brighter
        // face: smoked glass, lighter toward the top, the sun's side faintly warmer
        vec3 face = uSkyTop * 0.6 + pc * (0.1 + 0.07 * (q.y * 0.5 + 0.5)) * live;
        // the inner glow, from the rim inward
        vec2 aq = abs(q);
        float m = pow(pow(aq.x, 6.0) + pow(aq.y, 6.0), 1.0 / 6.0);   // a soft squircle: no corner seams
        float inner = exp(-(1.0 - min(m, 1.0)) * 2.6);
        face += pc * inner * 0.4 * live * on;
        // the rim: a hairline of light just inside the edge, hotter on top
        float rim = exp(-sq((sd + 0.035 * s) / (0.012 * s + px * 0.7)));
        float topLit = 0.75 + 0.45 * smoothstep(-0.4, 1.0, q.y);
        vec3 col = face + mix(pc, vec3(1.0), 0.25) * rim * 1.35 * topLit * live * on;
        // a faint sheen on the glass, top left
        col += vec3(1.0) * 0.025 * smoothstep(-0.2, 1.2, q.y - q.x * 0.3) * cov;
        // the light off: only the smoked glass, its rim a dim memory
        col = mix(uSkyTop * 0.5 + pc * 0.03 + pc * rim * 0.12, col, on);
        if (kind == 0 && age >= 0.0) {
            // lock: the rim flares and settles, a breath of light inside
            float fl = exp(-age * 6.0);
            col += mix(pc, vec3(1.0), 0.4) * fl * (rim * 2.2 + inner * 0.6 + 0.08);
        }
        col = mix(col, uBridgeInk, smoothstep(0.2, 0.85, uBridge));
        float a = cov * alpha * mix(0.9, 1.0, inner);
        frag = vec4(col * a, a);
        return;
    }
    if (kind == 1) {
        // Ghost: a faint hairline, nothing else.
        vec2 b = vec2(0.445);
        float sd = sdRound(p, b, 0.07);
        float line = 1.0 - smoothstep(0.0, px * 1.2, abs(sd + 0.03));
        vec3 col = pc * line * 0.45;
        frag = vec4(col * alpha * (1.0 - uBridge), line * 0.4 * alpha * (1.0 - uBridge));
        return;
    }
    if (kind == 2) {
        // A cleared cell leaving as a light trail: v3.x = direction, v3.y = progress 0..1.
        float dir = v3.x, k = v3.y;
        float hw = v0.z;
        float lead = dir * p.x;
        float vy = abs(p.y);
        float along = clamp((lead + hw) / max(2.0 * hw, 1e-3), 0.0, 1.0);
        float core = exp(-vy * vy / (0.002 + 0.012 * (1.0 - k)));
        vec3 hot = mix(pc, vec3(1.0), 0.45);
        float fade = 1.0 - k * k;
        vec3 col = hot * core * (along * along * along * 3.0 + along * 0.4) * fade;
        frag = vec4(col * alpha * (1.0 - uBridge), 0.0);
        return;
    }
    if (kind == 4) {
        // The bonus gauge: a hairline ring, a soft light rising inside as lines are cleared
        // (v3.x level, v3.y ready, v3.z active, v3.w radius).
        float R = v3.w;
        vec2 q = p / R;
        float r = length(q);
        float aa = px / R * 1.5;
        float disc = 1.0 - smoothstep(0.92 - aa, 0.92, r);
        float lvl = v3.x;
        float fillY = -0.92 + 1.84 * lvl;
        float filled = (1.0 - smoothstep(fillY - aa, fillY + aa, q.y)) * disc;
        float pulse = v3.y * (0.7 + 0.3 * sin(uTime * 3.0));
        vec3 lc = v3.z > 0.5 ? uAcc : mix(uSunEdge, uSunCore, smoothstep(-0.9, 0.9, q.y) * 0.5);
        vec3 col = lc * filled * (0.35 + 0.25 * smoothstep(-1.0, 1.0, q.y) + 0.8 * pulse);
        float surface = exp(-sq((q.y - fillY) / (0.02 + aa))) * disc * step(0.01, lvl) * step(lvl, 0.99);
        col += mix(lc, vec3(1.0), 0.3) * surface * 1.2;
        float ring = exp(-sq((r - 1.0) / (0.012 + aa)));
        col += mix(uGrid * 0.6, mix(lc, vec3(1.0), 0.3) * 1.8, v3.y) * ring;
        col += lc * exp(-max(r - 1.0, 0.0) * 5.0) * 0.3 * pulse * (1.0 - disc);
        float a = max(disc * 0.75, ring);
        col = mix(col + uSkyTop * 0.4 * disc * (1.0 - filled), uBridgeInk * a, uBridge);
        frag = vec4(col * alpha, a * alpha);
        return;
    }
    discard;
}
)";

// ---------------------------------------------------------------- light lines and anamorphic glints (additive)

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
        p = a + aCorner * vec2(a2.x * 4.0, a2.x);   // a glint: center a, size = width, stretched sideways
    } else if (int(a2.z + 0.5) == 2) {
        p = a + aCorner * a2.x;                     // a round light: center a, radius = width
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
        // an anamorphic glint: a hot core and a long thin horizontal streak
        vec2 q = (vP - v0.xy) / v2.x;
        float core = exp(-dot(q, q) / 0.004) * 2.0 + exp(-length(q) * 9.0) * 0.35;
        float streak = exp(-abs(q.y) / max(uPx / v2.x * 0.8, 0.01)) * exp(-abs(q.x) * 1.1) * 0.9;
        frag = vec4(c * (core + streak) * alpha, 0.0);
        return;
    }
    if (kind == 2) {
        // a round light in the fog (lamps): core and halo
        float r = length(vP - v0.xy) / v2.x;
        float l = exp(-r * r * 60.0) * 2.0 + exp(-r * 5.0) * 0.25;
        l *= 1.0 - smoothstep(0.8, 1.0, r);
        frag = vec4(c * l * alpha, 0.0);
        return;
    }
    vec2 a = v0.xy, b = v0.zw;
    vec2 pa = vP - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    float d = length(pa - ba * h);
    float w = v2.x * 0.5;
    float core = 1.0 - smoothstep(w, w + uPx, d);
    float glow = exp(-d / max(v2.y, 1e-4));
    vec3 col = mix(c, vec3(length(c) * 0.6), 0.35) * core + c * glow * 0.45;
    frag = vec4(col * alpha, 0.0);
}
)";

// ---------------------------------------------------------------- text (signed distance field glyphs)

inline const char* TEXT_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;   // pixels, top-left origin
layout(location = 1) in vec2 aUv;
layout(location = 2) in float aLy;
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
uniform vec3 uColor, uGlow;
uniform float uAlpha, uGlowAmt, uShadow, uBias, uBridge;
uniform vec3 uBridgeInk;
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.7;
    float fill = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    // a soft dark shadow under the letters (legible over the light), and an optional glow
    float sh = smoothstep(0.18, 0.5, d) * uShadow;
    float glow = smoothstep(0.1, 0.5, d);
    glow = glow * glow * uGlowAmt * (1.0 - uBridge);
    vec3 fc = mix(uColor, uBridgeInk, uBridge);
    float a = max(fill, sh * 0.55);
    vec3 col = fc * fill + uGlow * glow * (1.0 - fill);
    frag = vec4(col * uAlpha, a * uAlpha);
}
)";

// ---------------------------------------------------------------- bloom, streaks and the film post

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
// The anamorphic streak: a horizontal blur at a quarter resolution, in passes of growing reach.
inline const char* STREAK_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uTex;
uniform float uStep;        // texels between taps (uv)
uniform float uThreshold;   // > 0 on the first pass
void main() {
    vec3 c = vec3(0.0);
    float wsum = 0.0;
    for (int i = -6; i <= 6; i++) {
        float w = exp(-abs(float(i)) * 0.42);
        vec3 s = texture(uTex, vUv + vec2(float(i) * uStep, 0.0)).rgb;
        if (uThreshold > 0.0) {
            float m = max(s.r, max(s.g, s.b));
            s *= max(m - uThreshold, 0.0) / max(m, 1e-4);
        }
        c += s * w;
        wsum += w;
    }
    frag = vec4(c / wsum, 1.0);
}
)";

inline const char* POST_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene, uBloom, uStreak;
uniform vec2 uRes;
uniform float uTime, uFade, uDim, uSlow, uBridge, uBloomAmt, uStreakAmt, uExposure, uOver;
uniform vec3 uStreakTint, uShadowTint;
uniform vec4 uWellUv;
void main() {
    vec2 uv = vUv;
    vec3 c = texture(uScene, uv).rgb;
    vec3 bloom = texture(uBloom, uv).rgb;
    // bloom with a touch of halation: the glow around highlights leans red
    c += bloom * uBloomAmt * vec3(1.08, 0.92, 0.88);
    float inWell = step(uWellUv.x, uv.x) * step(uv.x, uWellUv.z) * step(uWellUv.y, uv.y) * step(uv.y, uWellUv.w);
    // the streaks soften over the HUD columns too (the labels stay legible at the peak)
    float wcx = (uWellUv.x + uWellUv.z) * 0.5, whw = (uWellUv.z - uWellUv.x) * 0.5;
    float inCols = 1.0 - smoothstep(whw * 2.2, whw * 2.6, abs(uv.x - wcx));
    c += texture(uStreak, uv).rgb * uStreakTint * uStreakAmt * (1.0 - 0.35 * inCols - 0.25 * inWell);
    vec3 raw = c; // the bridge is shown as is (both universes meet on the same color)
    // slow motion: the night drained of color, cooler, a little darker
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(c, l * vec3(0.78, 0.9, 1.1) * 1.05 + c * 0.2, uSlow * 0.6);
    // the lights going out (game over)
    c *= mix(uExposure, 1.0 + (uExposure - 1.0) * 0.35, inWell) * (1.0 - 0.82 * uOver);
    // film: a gentle toe (rich blacks) and a long shoulder (highlights roll into white)
    vec3 x = max(c, 0.0) * 1.1;
    vec3 tm = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
    c = tm;
    // a whisper of the palette in the shadows
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c += uShadowTint * 0.0025 * (1.0 - smoothstep(0.0, 0.2, lum));
    // vignette
    vec2 q = vUv - 0.5;
    c *= 1.0 - 0.55 * pow(max(dot(q * vec2(1.0, 1.2), q * vec2(1.0, 1.2)) * 2.2, 0.0), 1.4) * (1.0 - uBridge);
    c = mix(c, raw, uBridge);
    // fine grain
    float g = hash12(gl_FragCoord.xy + fract(uTime * 7.13) * 431.0) - 0.5;
    c += g * 0.02 * (0.08 + lum) * (1.0 - uBridge * 0.8);
    c *= 1.0 - uDim * 0.55;
    c *= uFade;
    // dithered to 8 bits: no banding in the long dark gradients of the haze
    float dh = hash12(gl_FragCoord.xy * 1.37 + fract(uTime * 3.1) * 97.0) - 0.5;
    frag = vec4(pow(max(c, 0.0), vec3(1.0 / 2.2)) + dh / 255.0, 1.0);
}
)";

} // namespace synthwave_shaders
