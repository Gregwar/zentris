#pragma once
// POLAR NIGHT's shaders (see polar.cpp). Everything is drawn in linear HDR (the moon, the aurora's lower edge and the
// ice rims go above 1 and bloom), then tone-mapped in the post.
//
// SKY_FS draws the world at a reduced resolution (it is soft): the night sky (stars, the Milky Way, the moon, a
// twilight glow), the aurora (curtains ray-marched as stacked slices of glowing footprints, one ribbon per group of
// frequency bands), the snowy ranges and a treeline on the horizon, and the snowfield (drifts lit by the moon and
// by the aurora). COMPOSITE_FS brings it to full resolution with the glitter of the snow, the well's sheet of dark
// ice, frost, blowing snow (game over, the bridge).

namespace polar_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float sdRound(vec2 p, vec2 b, float r) { vec2 d = abs(p) - b + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }
// The world camera: pitch > 0 looks up.
vec3 worldDir(vec2 uv, float tanH, float aspect, float pitch) {
    vec2 n = uv * 2.0 - 1.0;
    float cp = cos(pitch), sp = sin(pitch);
    vec3 fwd = vec3(0.0, sp, -cp);
    vec3 up = vec3(0.0, cp, sp);
    return normalize(fwd + vec3(1.0, 0.0, 0.0) * n.x * tanH * aspect + up * n.y * tanH);
}
)";

// ---------------------------------------------------------------- the world (reduced resolution)

inline const char* SKY_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uNoise;      // tileable: r perlin, g puffs, b fine perlin, a small puffs
uniform vec2 uRes;
uniform float uTanH, uAspect, uPitch, uEye, uTime, uSeed;
uniform vec3 uZenith, uHorizon, uGlow, uSnow, uShade, uMount, uMoonCol;
uniform float uGlowAz, uGlowAmt, uMoon, uMoonSize, uHalo22, uStars, uMilky, uTrees, uHaze;
uniform vec3 uMoonDir;
uniform vec3 uAurA, uAurB, uAurC;
uniform vec4 uRib[6];          // per ribbon: z of its footprint, brightness, height (0 short .. 1 tall), phase
uniform float uAurTime, uAurGain, uSurge, uSurgeX, uStorm, uPulse;
uniform vec3 uAurLight;        // the aurora's light falling on the snow
uniform float uDrift, uSpin;   // blowing snow: offset, amount
uniform float uFreeze;         // slow motion: the cold (silver, hushed)

const float H0 = 22.0;         // the aurora's lower edge (world units; the camera stands at uEye)
const float HSPAN = 46.0;      // its tallest extent above that

vec3 skyCol(vec3 d, float az) {
    float y = max(d.y, 0.0);
    vec3 c = mix(uHorizon, uZenith, smoothstep(0.0, 0.55, pow(y, 0.7)));
    // the twilight glow of a sun far below the horizon
    float ga = az - uGlowAz;
    ga = atan(sin(ga), cos(ga));
    c += uGlow * uGlowAmt * exp(-y * 9.0) * exp(-ga * ga * 1.6);
    c += uGlow * uGlowAmt * 0.25 * exp(-y * 3.0) * exp(-ga * ga * 0.5);
    // the moon's glow
    float mu = max(dot(d, uMoonDir), 0.0);
    c += uMoonCol * uMoon * (pow(mu, 12.0) * 0.05 + pow(mu, 200.0) * 0.2);
    return c;
}

float stars(vec3 d, float az, float el) {
    float acc = 0.0;
    for (int k = 0; k < 2; k++) {
        float sc = k == 0 ? 120.0 : 260.0;
        vec2 s = vec2(az * cos(el), el) * sc + float(k) * 17.0;
        vec2 id = floor(s);
        vec2 f = fract(s) - 0.5;
        float h = hash12(id + uSeed + float(k) * 41.0);
        if (h > (k == 0 ? 0.045 : 0.06)) continue;
        vec2 o = (vec2(hash12(id + 3.1), hash12(id + 7.7)) - 0.5) * 0.6;
        float r = length(f - o);
        float b = pow(hash12(id + 1.3), 4.0) * (k == 0 ? 1.0 : 0.35);
        float tw = 0.65 + 0.35 * sin(uTime * (0.8 + 2.5 * hash12(id + 9.1)) + h * 60.0);
        acc += exp(-r * r * (k == 0 ? 70.0 : 45.0)) * (0.1 + 3.0 * b) * tw;
    }
    return acc;
}

// The Milky Way: a faint band of dust and light across the sky.
vec3 milky(vec3 d) {
    vec3 axis = normalize(vec3(0.55, 0.35, 0.76));
    float b = dot(d, axis);
    float band = exp(-b * b / 0.035);
    if (band < 0.01) return vec3(0.0);
    vec2 q = vec2(atan(d.x, -d.z) * 1.3, d.y * 2.0 + b * 3.0);
    float n = textureLod(uNoise, q * 0.35 + 0.17, 0.0).r * 0.6 + textureLod(uNoise, q * 0.9 + 0.53, 0.0).b * 0.4;
    float dust = smoothstep(0.45, 0.7, textureLod(uNoise, q * 0.6 + vec2(0.31, 0.77), 0.0).r);
    float m = band * smoothstep(0.3, 0.8, n) * (1.0 - 0.75 * dust);
    return vec3(0.75, 0.8, 1.0) * m * 0.12;
}

// The aurora: each ribbon is a wavy footprint at the aurora's altitude, its curtain built by stacked slices above
// it (a bright, sharp lower edge, a diffuse top); fine vertical rays along it, folds drifting.
vec3 aurora(vec3 rd, float jitter) {
    if (rd.y < 0.012 || uAurGain < 0.001) return vec3(0.0);
    vec3 acc = vec3(0.0);
    const int N = 34;
    for (int j = 0; j < N; j++) {
        float fj = (float(j) + jitter) / float(N);
        float h = H0 + fj * fj * HSPAN;              // slices denser near the lower edge
        float t = (h - uEye) / rd.y;
        if (t > 1400.0) break;
        vec2 p = rd.xz * t;
        float fade = exp(-t * 0.0012);
        float lod = clamp(log2(t * 0.012), 0.0, 6.0);
        vec3 col = mix(uAurA * 1.3, uAurB, smoothstep(0.02, 0.3, fj));
        col = mix(col, uAurC * 1.3, smoothstep(0.25, 0.75, fj));
        float sum = 0.0;
        for (int k = 0; k < 6; k++) {
            vec4 R = uRib[k];
            if (R.y < 0.004) continue;
            float ph = R.w;
            float fa = 0.6 + abs(R.x) / 140.0;            // farther ribbons fold wider (they stay readable)
            float zc = R.x + fa * (16.0 * sin(p.x * 0.021 / fa + ph + uAurTime * 0.06)
                     + 6.0 * sin(p.x * 0.067 / fa - uAurTime * 0.11 + ph * 2.3))
                     + 2.0 * sin(p.x * 0.19 + uAurTime * 0.23 + ph * 3.1);
            float w = 0.9 + fj * 3.2 + abs(R.x) * 0.008;  // the curtain thickens upward
            float d = (p.y - zc) / w;
            float band = exp(-d * d);
            if (band < 0.01) continue;
            // the curtain's rays: streaks along the footprint
            float u = p.x + zc * 0.3;
            float r1 = textureLod(uNoise, vec2(u * 0.011 + ph, 0.31 + float(k) * 0.13 + uAurTime * 0.0015), lod).r;
            float r2 = textureLod(uNoise, vec2(u * 0.043 - ph, 0.71 + float(k) * 0.07 - uAurTime * 0.004), lod).b;
            float rays = smoothstep(0.38, 0.85, r1 * 0.65 + r2 * 0.55);
            rays = 0.06 + 1.5 * rays;
            // the height of the curtain varies along it, and with its band
            float tall = clamp(R.z * (0.65 + 0.7 * r1), 0.0, 1.0);
            float prof = smoothstep(0.0, 0.035, fj) * exp(-fj * mix(6.0, 1.2, tall));
            prof *= 1.0 + 1.6 * exp(-fj * 40.0);           // the bright lower edge
            float surge = 1.0 + uSurge * 1.5 * exp(-pow((p.x - uSurgeX) / 45.0, 2.0));
            sum += band * rays * prof * R.y * surge;
        }
        acc += col * sum * fade;
    }
    return acc * uAurGain * (1.0 + uPulse * 0.35 + uStorm * 1.5) * (1.5 / float(N));
}

// A ridge line (elevation in radians) from 1D noise along the azimuth.
float ridge(float az, float sc, float off) {
    float a = textureLod(uNoise, vec2(az * 0.42 * sc + off, 0.13 + off), 0.0).r;
    float b = textureLod(uNoise, vec2(az * 1.3 * sc + off * 1.7, 0.41 + off), 0.0).r;
    float c = textureLod(uNoise, vec2(az * 4.1 * sc - off, 0.77 - off), 0.0).b;
    float big = clamp(0.5 + (a - 0.5) * 3.2, 0.0, 1.0);          // massifs
    float rb = clamp(1.0 - abs(b - 0.5) * 4.0, 0.0, 1.0);          // sharp crests on them
    float m = big * (0.55 + 0.45 * rb * rb) + (c - 0.5) * 0.18 + rb * 0.12;
    return clamp(m, 0.0, 1.0);
}

float drifts(vec2 xz, float lod) {
    // long drifts across the wind, sastrugi on them
    vec2 q = xz * vec2(0.03, 0.08);
    float h = textureLod(uNoise, q + vec2(0.13, 0.41), lod).r * 0.7 + textureLod(uNoise, q * 3.1 + vec2(0.7, 0.2), lod + 0.5).b * 0.3;
    return h * 1.6;
}

void main() {
    vec3 rd = worldDir(vUv, uTanH, uAspect, uPitch);
    float az = atan(rd.x, -rd.z), el = asin(clamp(rd.y, -1.0, 1.0));
    float jitter = hash12(floor(gl_FragCoord.xy));
    vec3 col;
    float aurFall = 0.0;
    if (rd.y >= 0.0) {
        col = skyCol(rd, az);
        float starVis = smoothstep(0.0, 0.1, rd.y);
        col += vec3(0.9, 0.93, 1.0) * stars(rd, az, el) * uStars * starVis;
        col += milky(rd) * uMilky * starVis;
        vec3 aur = aurora(rd, jitter);
        col += aur;
        // the moon
        float a = acos(clamp(dot(rd, uMoonDir), -1.0, 1.0));
        float aa = 2.0 * uTanH / uRes.y;
        if (uMoon > 0.001) {
            float disc = 1.0 - smoothstep(uMoonSize - aa, uMoonSize + aa, a);
            vec2 mq = (rd.xy - uMoonDir.xy) / uMoonSize;
            float maria = textureLod(uNoise, mq * 0.18 + 0.3, 0.0).r;
            vec3 mc = uMoonCol * (1.7 - 0.5 * smoothstep(0.45, 0.68, maria));
            col = mix(col, mc, disc * uMoon);
            col += uMoonCol * uMoon * exp(-max(a - uMoonSize, 0.0) * 45.0) * 0.15;
            // ice crystals in the air: the 22 degree halo
            col += uMoonCol * uHalo22 * uMoon * exp(-pow((a - 0.384) / 0.012, 2.0)) * 0.06;
        }
        // the ranges on the horizon: far snowy peaks, then near hills with a treeline
        float off1 = uSeed * 0.013, off2 = 0.5 + uSeed * 0.007;
        float rFar = 0.006 + 0.1 * ridge(az, 1.0, off1);
        float rNear = 0.003 + 0.02 * ridge(az, 2.4, off2);
        // trees on the near hills: small dark spruces, in groves
        float tree = 0.0;
        if (uTrees > 0.0) {
            float cells = 150.0;
            float id = floor(az * cells);
            float cx = (id + 0.5 + (hash11(id + uSeed) - 0.5) * 0.5) / cells;
            float grove = smoothstep(0.4, 0.7, textureLod(uNoise, vec2(cx * 1.9 + 0.37, 0.57), 0.0).r);
            if (hash11(id * 1.37 + uSeed) < uTrees * grove * 1.4) {
                float th = (0.012 + 0.016 * hash11(id * 2.1 + 3.0)) * mix(0.55, 1.0, grove);
                float baseE = 0.003 + 0.02 * ridge(cx, 2.4, off2) - 0.003;
                float top = baseE + th;
                float wdt = (top - el) * 0.22 + 0.0006;
                if (el < top && el > baseE - 0.006 && abs(az - cx) * cos(el) < wdt) tree = 1.0;
            }
        }
        vec3 night = mix(mix(uHorizon, uZenith, 0.4), vec3(dot(mix(uHorizon, uZenith, 0.4), vec3(0.33))), 0.5) * 1.6 + vec3(0.022, 0.028, 0.045);   // starlight and the sky
        vec3 moonDir2 = normalize(vec3(uMoonDir.x, 0.0, uMoonDir.z));
        if (el < rNear || tree > 0.0) {
            float e = 0.004;
            float sl = (ridge(az + e, 2.4, off2) - ridge(az - e, 2.4, off2)) / (2.0 * e) * 0.02;
            float lit = clamp(0.5 + 2.0 * sl * sign(uMoonDir.x + 1e-4), 0.0, 1.0);
            vec3 hill = uSnow * (night * 0.8 + uMoonCol * uMoon * (0.15 + 0.35 * lit) + uAurLight * 0.7);
            float hz = uHaze * 0.35;
            hill = mix(hill, uHorizon * 1.2 + uAurLight * 0.2, hz);
            if (tree > 0.0) hill = uZenith * 0.6 + uShade * 0.12 + uAurLight * 0.04;
            col = hill;
        } else if (el < rFar) {
            // broad faces: lit on the side turned toward the light (the moon, or the aurora ahead), the ridge lines
            // leaning outward from the summits as they come down
            float down = rFar - el;
            float e = 0.016;
            float wob = (textureLod(uNoise, vec2(az * 2.2, el * 3.0) + 0.61, 0.0).r - 0.5) * 0.05;
            float azs = az + wob + down * 0.5;
            float sl = (ridge(azs + e, 1.0, off1) - ridge(azs - e, 1.0, off1)) / (2.0 * e) * 0.1;
            float side = sl * (uMoon > 0.05 ? sign(uMoonDir.x + 1e-4) : 1.0);
            float lit = smoothstep(-0.2, 0.25, side);
            float snowy = textureLod(uNoise, vec2(az * 3.0, el * 2.0) + 0.21, 0.0).r;
            vec3 face = uSnow * (night * (0.5 + 0.5 * lit) + uMoonCol * uMoon * lit * 0.55 + uAurLight * (0.55 + 0.45 * lit));
            face *= (0.85 + 0.3 * snowy) * (0.55 + 0.45 * el / max(rFar, 1e-3));
            float h = clamp(uHaze * 0.8 + 0.3 * (1.0 - el / max(rFar, 1e-3)), 0.0, 0.92);
            col = mix(face, skyCol(vec3(rd.x, 0.015, rd.z), az) * 1.1 + uAurLight * 0.3, h * 0.7);
        }
    } else {
        // the snowfield
        float t = uEye / max(-rd.y, 1e-4);
        vec2 xz = rd.xz * t;
        float lod = clamp(log2(t * 0.25), 0.0, 7.0);
        float e = 0.3 + t * 0.012;
        float h = drifts(xz, lod), hx = drifts(xz + vec2(e, 0.0), lod), hz = drifts(xz + vec2(0.0, e), lod);
        vec3 n = normalize(vec3((h - hx) * 1.8, e, (h - hz) * 1.8));
        vec3 night = mix(mix(uHorizon, uZenith, 0.4), vec3(dot(mix(uHorizon, uZenith, 0.4), vec3(0.33))), 0.5) * 1.6 + vec3(0.022, 0.028, 0.045);
        float moonL = clamp(dot(n, uMoonDir) * 1.1 + 0.3, 0.0, 1.4) * uMoon;
        // the aurora lights the snow from ahead and above, brighter on slopes facing it
        float facing = clamp(dot(n, normalize(vec3(0.0, 0.55, -1.0))) * 2.4 + 0.15, 0.0, 1.8);
        vec3 light = night * (0.8 + 0.4 * n.y) + uMoonCol * moonL * 0.45 + uAurLight * (0.3 + 0.55 * facing);
        col = uSnow * light * (0.85 + 0.3 * h);
        // grazing reflection of the sky near the horizon
        float fres = pow(1.0 - clamp(-rd.y * 5.0, 0.0, 1.0), 3.0);
        col += (uAurLight * 0.6 + uHorizon * 0.4) * fres * 0.5;
        // blowing snow skimming over the drifts
        if (uSpin > 0.001) {
            float s1 = textureLod(uNoise, vec2(xz.x * 0.03 - uDrift * 0.06, xz.y * 0.11), lod).r;
            float s2 = textureLod(uNoise, vec2(xz.x * 0.09 - uDrift * 0.15, xz.y * 0.3 + 0.4), lod).b;
            float sp = smoothstep(0.5, 0.85, s1 * 0.6 + s2 * 0.5) * uSpin * exp(-t * 0.01);
            col += (uSnow * night + uAurLight) * sp * 0.6;
        }
        // the far snow fades into the horizon's haze
        float f = 1.0 - exp(-t * 0.004 * (0.4 + uHaze));
        col = mix(col, uSnow * night * 0.9 + uHorizon * 0.5 + uAurLight * 0.5, f);
        aurFall = 1.0;
    }
    // the hush of slow motion: the colors drain toward silver
    float l = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(col, vec3(l) * vec3(0.85, 0.95, 1.12), uFreeze * 0.55);
    frag = vec4(max(col, 0.0), aurFall);
}
)";

// ---------------------------------------------------------------- the world at full resolution

inline const char* COMPOSITE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uSky, uNoise;
uniform vec4 uView;          // center xy, half extents (board units)
uniform float uPx;           // board units per pixel
uniform vec4 uWell, uColL, uColR; // the well's sheet of ice, the HUD columns (board x0 y0 x1 y1)
uniform vec3 uRim, uIce, uFrostCol, uAurLight, uSnow;
uniform float uTime, uTanH, uAspect, uPitch;
uniform float uGlitter, uFrost, uOver, uBridge, uDrift, uCornerShade, uWellAlpha;
uniform vec3 uBridgeCol, uCornerCol, uMoonCol;

float paneSd(vec2 p, vec4 r, float rad) {
    vec2 c = (r.xy + r.zw) * 0.5, h = (r.zw - r.xy) * 0.5;
    return sdRound(p - c, h, rad);
}

// A feathery frost pattern (fern-like crystals): ridges of layered noise.
float frostPattern(vec2 p) {
    float a = texture(uNoise, p * 0.11).r, b = texture(uNoise, p * 0.31 + 0.3).b, c = texture(uNoise, p * 0.9 + 0.7).r;
    float f = 1.0 - abs(fract((a * 0.6 + b * 0.3 + c * 0.1) * 7.0) - 0.5) * 2.0;
    return pow(f, 4.0) * 0.7 + c * 0.3;
}

void main() {
    vec2 uv = vUv;
    vec2 p = uView.xy + (uv * 2.0 - 1.0) * uView.zw;
    vec4 sky = texture(uSky, uv);
    vec3 col = sky.rgb;

    // the glitter of the snow: still crystals catching the light, twinkling
    if (uGlitter > 0.001) {
        vec3 rd = worldDir(uv, uTanH, uAspect, uPitch);
        if (rd.y < -0.004) {
            vec2 cell = floor(gl_FragCoord.xy / 2.0);
            float h = hash12(cell + 0.37);
            float near = smoothstep(0.004, 0.2, -rd.y);
            if (h > 0.996 - 0.004 * near) {
                float tw = pow(max(sin(uTime * (1.5 + 3.0 * hash12(cell + 5.1)) + h * 90.0), 0.0), 8.0);
                vec2 f = fract(gl_FragCoord.xy / 2.0) - 0.5;
                float spot = exp(-dot(f, f) * 6.0);
                vec3 gc = mix(uMoonCol, vec3(1.0), 0.5) * 0.6 + uAurLight * 2.0;
                col += gc * tw * spot * uGlitter * (0.4 + 0.6 * near) * 1.6;
            }
        }
    }

    // the HUD columns: a soft shade of night behind them (no pane)
    {
        float sL = paneSd(p, uColL, 1.2), sR = paneSd(p, uColR, 1.2);
        float m = 1.0 - smoothstep(-1.5, 1.4, min(sL, sR));
        col = mix(col, uCornerCol, m * 0.28 * uWellAlpha);
    }
    // the well: a sheet of dark ice, the world blurred behind it, frost along its foot, a hairline rim
    {
        float sW = paneSd(p, uWell, 0.18);
        float inW = 1.0 - smoothstep(-1.0 * uPx, 1.0 * uPx, sW);
        if (inW > 0.0) {
            vec3 blur = textureLod(uSky, uv, 4.0).rgb * 0.55 + textureLod(uSky, uv, 2.5).rgb * 0.45;
            float k = clamp((p.y - uWell.y) / (uWell.w - uWell.y), 0.0, 1.0);
            vec3 ice = blur * 0.5 + uIce * (0.65 + 0.35 * k);
            // frost grows up from the foot of the well, faint
            float fd = p.y - uWell.y;
            float fp = texture(uNoise, p * 0.09).r * 0.7 + texture(uNoise, p * 0.3).b * 0.3;
            float fr = (1.0 - smoothstep(0.0, 1.6, fd + (fp - 0.5) * 1.6)) * 0.16;
            ice = mix(ice, uFrostCol, fr);
            col = mix(col, ice, inW * uWellAlpha);
        }
        float rim = exp(-abs(sW) / (1.2 * uPx)) * 0.55 + exp(-abs(sW) / (8.0 * uPx)) * 0.08;
        float k = clamp((p.y - uWell.y) / (uWell.w - uWell.y), 0.0, 1.0);
        col += uRim * rim * (0.35 + 0.65 * k) * uWellAlpha;
    }
    // quiet behind the corner where the universe's name is written
    {
        vec2 corner = vec2(uView.x - uView.z, uView.y - uView.w);
        float m = exp(-length((p - corner) * vec2(0.075, 0.28)) * 2.0);
        col = mix(col, uCornerCol, m * uCornerShade);
    }
    // slow motion: frost creeps in from the edges of the screen
    if (uFrost > 0.001) {
        vec2 q = (uv - 0.5) * vec2(uView.z / uView.w, 1.0);
        float e = min(0.5 * uView.z / uView.w - abs(q.x), 0.5 - abs(q.y));
        float fp = frostPattern(p * 1.3);
        float m = 1.0 - smoothstep(0.0, 0.09 * uFrost, e + (fp - 0.5) * 0.07 * uFrost);
        float fern = smoothstep(0.35, 0.9, fp);
        col = mix(col, uFrostCol * (0.6 + 0.7 * fern), m * (0.15 + 0.3 * fern) * uFrost);
    }
    // game over: blowing snow rises from below and buries the field
    if (uOver > 0.001) {
        float n = texture(uNoise, vec2(p.x * 0.02 - uDrift * 0.05, p.y * 0.12)).r * 0.6
                + texture(uNoise, vec2(p.x * 0.06 - uDrift * 0.12, p.y * 0.3 + 0.5)).b * 0.4;
        float top = mix(uView.y - uView.w - 2.0, uView.y + 2.0, uOver);
        float m = smoothstep(top + 3.0, top - 3.0, p.y + (n - 0.5) * 6.0);
        col = mix(col, uSnow * (0.7 + 0.4 * n), m * 0.85);
    }
    // the bridge (a change of universe): a whiteout of blowing snow
    if (uBridge > 0.0) {
        float n = texture(uNoise, vec2(p.x * 0.012 - uTime * 0.09, p.y * 0.07)).r * 0.55
                + texture(uNoise, vec2(p.x * 0.035 - uTime * 0.22, p.y * 0.21 + 0.3)).b * 0.45;
        float w = smoothstep(0.0, 1.0, uBridge * 2.1 - 0.55 + (n - 0.5) * 0.9);
        w = max(w, smoothstep(0.9, 0.99, uBridge));
        col = mix(col, uBridgeCol, w);
    }
    frag = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- flat triangles (zenscene's panels)

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
void main() { frag = vec4(vCol.rgb * vCol.a, vCol.a); }
)";

// ---------------------------------------------------------------- tiles: the stack, the piece, previews, gauge

// a0: center xy, half size xy (board). a1: rgb, alpha. a2: kind, age, mask, frost. a3: extra.
inline const char* TILE_VS = R"(#version 330 core
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
    vec2 l = aCorner * a0.zw;
    vec2 p = a0.xy + l;
    vLocal = l;
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
uniform sampler2D uNoise;
uniform float uPx;
uniform vec3 uAurLight, uMoonLight, uFrostCol, uGaugeCol, uGaugeTrack, uShadow;
uniform float uBridge, uTime, uBeat;
uniform vec3 uBridgeInk;

// The block's outline: a rounded square, reaching past the cell's edge on the sides where a block of the same kind
// touches it (a piece is one body of ice). top: how far the body's upper edge is (far when joined above).
float iceSd(vec2 p, int mask, float s, out float top) {
    vec2 lo = vec2(-0.45 * s), hi = vec2(0.45 * s);
    if ((mask & 1) != 0) lo.x = -0.9 * s;
    if ((mask & 2) != 0) hi.x = 0.9 * s;
    if ((mask & 4) != 0) hi.y = 1.6 * s;
    if ((mask & 8) != 0) lo.y = -0.9 * s;
    top = (hi.y - p.y) / s;
    vec2 c = (lo + hi) * 0.5, h = (hi - lo) * 0.5;
    return sdRound(p - c, h, 0.11 * s);
}
float iceSd(vec2 p, int mask, float s) { float t; return iceSd(p, mask, s, t); }

// Clear ice tinted by its kind: a dense core, light gathered along its edges, a crisp rim of frost, the aurora's
// light on its upper faces, a faint veil of inner fractures.
vec3 ice(vec2 p, vec2 w, float sd, float s, float topD, vec3 pc, float frost) {
    float d = -sd / s;                                   // depth inside (cell units)
    float n = texture(uNoise, w * 0.11 + 0.37).r;
    float m = texture(uNoise, w * 0.23 + 0.71).b;
    vec3 col = pc * (0.6 + 0.16 * (n - 0.5) + 0.1 * (m - 0.5));
    // light gathered in the ice near its edges
    float eg = exp(-d / 0.12);
    col += mix(pc, vec3(1.0), 0.3) * eg * 0.4;
    // fractures: a faint veil
    float crack = (1.0 - smoothstep(0.0, 0.03, abs(m - 0.5))) * smoothstep(0.55, 0.7, n);
    col += mix(pc, vec3(1.0), 0.5) * crack * 0.12 * smoothstep(0.05, 0.15, d);
    // the sky's light on the upper face: the aurora, the moon
    float top = exp(-topD / 0.35);
    col += uAurLight * (0.12 + 1.0 * top);
    col += uMoonLight * top * 0.15;
    // a crisp rim of frost, brightest along the top
    float rim = exp(-d / (1.3 * uPx / s + 0.012));
    col = mix(col, uFrostCol * (0.9 + 0.5 * top) + uAurLight * 0.6, rim * (0.5 + 0.35 * top));
    // frozen over (game over): snow white
    col = mix(col, uFrostCol * (0.9 + 0.15 * n) + uAurLight * 0.3, frost);
    return col;
}

void main() {
    int kind = int(v2.x + 0.5);
    vec3 pc = v1.rgb;
    float alpha = v1.a;
    float age = v2.y;
    int mask = int(v2.z + 0.5);
    float frost = v2.w;
    vec2 p = vLocal;
    float px = uPx;
    float bridgeK = smoothstep(0.2, 0.85, uBridge);
    if (kind == 0 || kind == 3) {
        float s = v3.x;
        float topD;
        float sd = iceSd(p, mask, s, topD);
        float cov = 1.0 - smoothstep(-px * 0.7, px * 0.7, sd);
        if (cov <= 0.0) discard;
        vec3 col = ice(p, vWorld, sd, s, topD, pc, frost);
        if (kind == 0 && age >= 0.0) {
            // lock: the block freezes, a white breath of frost receding into its edges
            float fl = exp(-age * 3.5);
            float d = -sd / s;
            float reach = 0.5 * fl;
            float f = 1.0 - smoothstep(reach - 0.06, reach + 0.02, d);
            col = mix(col, uFrostCol * 1.3, f * fl * 0.85);
        }
        if (kind == 3) col *= 1.0 + 0.06 * uBeat;
        col = mix(col, uBridgeInk, bridgeK);
        float a = cov * alpha * 0.95;
        frag = vec4(col * a, a);
        return;
    }
    if (kind == 1) {
        // ghost: a thin outline of frost on the dark ice, a breath of the piece's color
        float sd = iceSd(p, mask, 1.0);
        float line = (1.0 - smoothstep(0.01, 0.01 + px * 1.2, abs(sd + 0.03))) * 0.55;
        float fill = (1.0 - smoothstep(-px, px, sd)) * 0.07;
        vec3 col = mix(pc, uFrostCol, 0.45) * line + pc * fill;
        float a = (line * 0.8 + fill) * alpha * (1.0 - uBridge);
        frag = vec4(col * alpha * (1.0 - uBridge), a);
        return;
    }
    if (kind == 2) {
        // a cleared cell: it turns to snow, then crumbles to powder (v3.y progress 0..1, v3.z seed)
        float k = v3.y;
        float topD;
        float sd = iceSd(p, mask, 1.0, topD);
        float cov = 1.0 - smoothstep(-px * 0.7, px * 0.7, sd);
        if (cov <= 0.0) discard;
        float n = texture(uNoise, vWorld * 0.21 + v3.z).g * 0.6 + texture(uNoise, vWorld * 0.53 + v3.z * 1.7).r * 0.4;
        float whiten = smoothstep(0.0, 0.3, k);
        float th = (k - 0.25) * 1.5;
        float keep = smoothstep(th, th + 0.06, n);
        vec3 col = ice(p, vWorld, sd, 1.0, topD, pc, whiten);
        col += uFrostCol * exp(-pow((n - th) / 0.05, 2.0)) * 0.6 * step(0.25, k);
        float a = cov * keep * alpha * (1.0 - uBridge);
        frag = vec4(col * a, a);
        return;
    }
    if (kind == 4) {
        // the bonus gauge: a ring of frost filling clockwise; a snowflake grows at its heart when it is ready
        float R = v3.w;
        vec2 q = p / R;
        float r = length(q);
        float aa = px / R * 1.5;
        float a01 = fract(atan(q.x, q.y) / 6.2831853 + 1.0);
        float lvl = v3.x, ready = v3.y, isOn = v3.z;
        float ring = exp(-pow((r - 0.8) / (0.04 + aa), 2.0));
        float fill = 1.0 - smoothstep(lvl - 0.004, lvl + 0.004, a01);
        if (lvl >= 0.999) fill = 1.0;
        float back = 1.0 - smoothstep(0.98 - aa, 0.98 + aa, r);
        vec3 col = uShadow * back * 0.5;
        float a = back * 0.35;
        col += uGaugeTrack * ring * 0.35;
        a = max(a, ring * 0.4);
        float pulse = 0.75 + 0.25 * sin(uTime * 4.0);
        col += uGaugeCol * ring * fill * (1.1 + ready * pulse);
        a = max(a, ring * fill);
        if (ready > 0.5 || isOn > 0.5) {
            // a six-armed snowflake (slowly turning while the time is frozen)
            float ang = atan(q.y, q.x) + (isOn > 0.5 ? uTime * 0.3 : 0.0);
            float sect = 3.14159265 / 3.0;
            float aa2 = mod(ang + sect * 0.5, sect) - sect * 0.5;
            vec2 s = vec2(cos(aa2), abs(sin(aa2))) * r;
            float arm = abs(s.y) - 0.035;
            arm = max(arm, s.x - 0.6);
            // two branches on each arm
            float br = 1e3;
            for (int i = 0; i < 2; i++) {
                float at = 0.25 + 0.17 * float(i);
                vec2 b = s - vec2(at, 0.0);
                vec2 dir = normalize(vec2(1.0, 1.1));
                float along = clamp(dot(b, dir), 0.0, 0.2 - 0.06 * float(i));
                br = min(br, length(b - dir * along) - 0.028);
            }
            float sdF = min(arm, br);
            float flake = 1.0 - smoothstep(-aa, aa, sdF);
            float glow = exp(-max(sdF, 0.0) / 0.06) * 0.4;
            vec3 fc = uGaugeCol * (isOn > 0.5 ? 1.8 : 1.3 + 0.5 * pulse);
            col = mix(col, fc, flake);
            col += uGaugeCol * glow * (isOn > 0.5 ? 1.0 : pulse);
            a = max(a, max(flake, glow * 0.6));
        }
        col = mix(col, uBridgeInk * a, uBridge);
        frag = vec4(col * alpha, a * alpha);
        return;
    }
    discard;
}
)";

// ---------------------------------------------------------------- snow: flakes, powder, glints (premultiplied)

// a0: center xy, radius, kind (0 flake, 1 powder puff, 2 glint). a1: rgb, alpha. a2: seed, progress, stretch x, 0.
inline const char* DOT_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
layout(location = 2) in vec4 a1;
layout(location = 3) in vec4 a2;
uniform mat4 uVP;
out vec2 vLocal;
flat out vec4 v0, v1, v2;
void main() {
    vLocal = aCorner;
    v0 = a0; v1 = a1; v2 = a2;
    vec2 p = a0.xy + aCorner * vec2(a0.z * max(a2.z, 1.0), a0.z);
    gl_Position = uVP * vec4(p, 0.0, 1.0);
}
)";
inline const char* DOT_FS = R"(#version 330 core
in vec2 vLocal;
flat in vec4 v0, v1, v2;
out vec4 frag;
uniform sampler2D uNoise;
uniform float uBridge;
void main() {
    int kind = int(v0.w + 0.5);
    float r = length(vLocal);
    if (r > 1.0) discard;
    float fade = 1.0 - uBridge;
    if (kind == 0) {
        // a snowflake: a soft dot
        float m = exp(-r * r * 4.5);
        float a = m * v1.a * fade;
        frag = vec4(v1.rgb * a, a);
        return;
    }
    if (kind == 2) {
        // a glint: a tiny four-pointed star of light (additive)
        vec2 q = vLocal;
        float rays = exp(-abs(q.y) * 30.0) * (1.0 - abs(q.x)) + exp(-abs(q.x) * 30.0) * (1.0 - abs(q.y));
        float core = exp(-r * r * 60.0) * 1.5 + exp(-r * 5.0) * 0.25;
        frag = vec4(v1.rgb * (rays * 0.8 + core) * v1.a * fade, 0.0);
        return;
    }
    // powder: a soft billow of snow
    vec2 sw = vec2(v2.y * 0.2, -v2.y * 0.12);
    float n = texture(uNoise, vLocal * 0.3 + v2.x + sw).g * 0.55 + texture(uNoise, vLocal * 0.8 + v2.x * 1.7 - sw).r * 0.45;
    float m = smoothstep(1.0, 0.1, r + (n - 0.5) * 1.0);
    float a = m * m * v1.a * fade;
    frag = vec4(v1.rgb * a, a);
}
)";

// ---------------------------------------------------------------- text (signed distance field glyphs)

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
uniform vec3 uColor, uHalo;
uniform float uAlpha, uHaloAmt, uBias, uBridge;
uniform vec3 uBridgeInk;
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.75;
    float fill = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    // a soft halo of night behind the letters (readable over the aurora)
    float halo = smoothstep(0.1, 0.5, d);
    halo = halo * halo * uHaloAmt * (1.0 - uBridge);
    vec3 fc = mix(uColor, uBridgeInk, uBridge);
    float a = fill + (1.0 - fill) * halo;
    vec3 col = fc * fill + uHalo * halo * (1.0 - fill);
    frag = vec4(col * uAlpha, a * uAlpha);
}
)";

// ---------------------------------------------------------------- bloom and the post

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
uniform float uTime, uFade, uDim, uBloomAmt, uExposure, uBridge;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    c += texture(uBloom, vUv).rgb * uBloomAmt;
    vec3 raw = c;
    c *= uExposure;
    // a soft filmic shoulder; the brightest light goes a little toward white
    vec3 over = max(c - 1.0, 0.0);
    c += (over.r + over.g + over.b) * 0.1;
    const float K = 0.62;
    c = mix(c, K + (1.0 - K) * (1.0 - exp(-(c - K) / (1.0 - K))), step(K, c));
    // a cold night vignette
    vec2 q = vUv - 0.5;
    c *= 1.0 - 0.32 * dot(q, q) * 1.6 * (1.0 - uBridge);
    c = mix(c, raw, uBridge);
    float g = hash12(gl_FragCoord.xy + fract(uTime * 7.13) * 431.0) - 0.5;
    c += g * 0.014;
    c *= 1.0 - uDim * 0.55;
    c *= uFade;
    frag = vec4(pow(max(c, 0.0), vec3(1.0 / 2.2)), 1.0);
}
)";

} // namespace polar_shaders
