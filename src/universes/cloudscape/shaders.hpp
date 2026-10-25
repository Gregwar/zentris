#pragma once
// CLOUDSCAPE's shaders (see cloudscape.cpp). Everything is drawn in linear HDR (the sun's disc and the rims go above
// 1 and bloom), then tone-mapped in the post.
//
// The sky and the sea of clouds are drawn at a reduced resolution (they are soft) by CLOUD_FS: a 2.5D heightfield of
// cloud tops ray-marched from a camera floating above it, towering cumulus as lit impostors on the horizon, high
// veils and cirrus, stars, a moon and aurora. RAYS_FS blurs the visible light toward the sun (god rays), at a
// quarter resolution. COMPOSITE_FS brings both to full resolution and frosts the floating glass panes.

namespace cloudscape_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }
float sdRound(vec2 p, vec2 b, float r) { vec2 d = abs(p) - b + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }
)";

// ---------------------------------------------------------------- the sky and the sea of clouds (reduced resolution)

inline const char* CLOUD_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uNoise;      // tileable: r perlin, g puffs (inverted worley), b perlin, a small puffs
uniform vec2 uRes;
uniform float uTanH, uAspect, uPitch, uEye, uTime, uSeed;
uniform vec2 uDrift;           // the cloud field's offset (world xz): the flight
uniform vec2 uHiDrift;         // the high clouds' offset
uniform vec3 uSunDir, uSunCol;
uniform float uSunGain, uSunSize, uMoon;
uniform vec3 uZenith, uHorizon, uGlow, uHaze, uLit, uShade, uDeep;
uniform float uCov, uAmp, uFog;
uniform vec4 uTower[6];        // azimuth, half width, height (radians), haze
uniform float uGrow[6], uFlash[6];
uniform vec3 uFlashCol;
uniform float uAnvil;
uniform float uStars, uAurora;
uniform vec3 uAurA, uAurB;
uniform float uVeil, uPart, uCirrus, uSink, uFlood;

const float TOWER_T = 105.0;

vec3 camDir(vec2 uv) {
    vec2 n = uv * 2.0 - 1.0;
    float cp = cos(uPitch), sp = sin(uPitch);
    vec3 fwd = vec3(0.0, -sp, -cp);
    vec3 up = vec3(0.0, cp, -sp);
    return normalize(fwd + vec3(1.0, 0.0, 0.0) * n.x * uTanH * uAspect + up * n.y * uTanH);
}

// The cloud tops: round puffs on puffs, broken by a slow large-scale coverage.
float cloudH(vec2 xz, float lod) {
    vec2 u = (xz + uDrift) * 0.02;
    const mat2 R1 = mat2(0.80, 0.60, -0.60, 0.80), R2 = mat2(0.36, -0.93, 0.93, 0.36);
    vec4 a = textureLod(uNoise, u, lod);
    vec4 b = textureLod(uNoise, R1 * u * 2.3 + vec2(0.37, 0.71), lod + 1.2);
    float heap = textureLod(uNoise, R2 * u * 0.47 + vec2(0.61, 0.17), max(lod - 1.1, 0.0)).g;
    float big = textureLod(uNoise, R1 * u * 0.29 + vec2(0.13, 0.57), max(lod - 1.8, 0.0)).r;
    float h = a.g * 0.42 + heap * 0.38 + b.a * 0.22 + b.r * 0.06;
    h += (big - 0.5) * 0.9;
    h = clamp((h - uCov) / (1.15 - uCov), 0.0, 1.0);
    return h * uAmp;
}

float lodAt(float t, vec3 rd) {
    float pixA = 2.0 * uTanH / uRes.y;
    return log2(max(t * pixA * 5.1 / sqrt(max(-rd.y, 0.02)), 1e-3));
}

bool marchSea(vec3 ro, vec3 rd, out float tHit) {
    tHit = 0.0;
    if (rd.y > -0.002) return false;
    float tA = max((uAmp - ro.y) / rd.y, 0.0);
    float tB = min(-ro.y / rd.y, 260.0);
    if (tA >= tB) return false;
    const int N = 44;
    float dt = (tB - tA) / float(N);
    float t = tA, lastD = 1.0, lastT = tA;
    for (int i = 0; i <= N; i++) {
        vec3 p = ro + rd * t;
        float d = p.y - cloudH(p.xz, lodAt(t, rd));
        if (d < 0.0) {
            tHit = i == 0 ? t : lastT + (t - lastT) * lastD / max(lastD - d, 1e-4);
            return true;
        }
        lastD = d;
        lastT = t;
        t += dt;
    }
    tHit = tB;
    return true; // the cloud floor
}

vec3 skyCol(vec3 d) {
    float y = d.y;
    float t = pow(clamp(y, 0.0, 1.0), 0.55);
    vec3 c = mix(uHorizon, uZenith, smoothstep(0.0, 0.62, t));
    c = mix(c, uHaze, smoothstep(0.0, -0.1, y));
    float mu = max(dot(d, uSunDir), 0.0);
    float g = pow(mu, 8.0) * 0.12 + pow(mu, 60.0) * 0.3 + pow(mu, 700.0) * 0.8;
    c += uGlow * g * uSunGain * (1.0 - 0.6 * uMoon);
    c += uHorizon * 0.1 * exp(-abs(y) * 30.0);
    return c;
}

// The haze at the horizon in a direction (what the far sea fades into).
vec3 hazeAt(vec3 rd) {
    vec3 h = normalize(vec3(rd.x, 0.0, rd.z));
    return mix(skyCol(h), uHaze, 0.35);
}

vec3 shadeSea(vec3 ro, vec3 rd, float t) {
    vec3 p = ro + rd * t;
    float lod = lodAt(t, rd);
    float h = cloudH(p.xz, lod);
    float e = 0.2 + t * 0.012;
    float hx = cloudH(p.xz + vec2(e, 0.0), lod);
    float hz = cloudH(p.xz + vec2(0.0, e), lod);
    vec3 n = normalize(vec3(h - hx, e, h - hz));
    // small puffs on the puffs (shading only)
    vec2 u = (p.xz + uDrift) * 0.02;
    vec4 dn = textureLod(uNoise, u * 6.7 + 0.21, lod + 2.6);
    n = normalize(n + vec3(dn.a - 0.5, 0.0, dn.g - 0.5) * 0.35 * exp(-t * 0.03));
    float hn = clamp(h / max(uAmp, 1e-3), 0.0, 1.0);
    // the sun's shadow: a few steps toward the light
    float sh = 0.0;
    for (int i = 1; i <= 4; i++) {
        float s = float(i * i) * 0.7;
        vec3 q = p + uSunDir * s;
        sh += clamp((cloudH(q.xz, lod + 0.5) - q.y) * 2.5, 0.0, 1.0);
    }
    float shadow = exp(-sh * 0.9);
    float dif = clamp((dot(n, uSunDir) + 0.1) / 1.1, 0.0, 1.0);
    float mu = dot(rd, uSunDir);
    // the valleys in shade, the tops in the sky's light
    vec3 col = mix(uDeep, uShade, pow(hn, 0.8));
    col += uZenith * 0.15 * clamp(n.y, 0.0, 1.0) * (0.4 + 0.6 * hn);
    vec3 sunL = uLit * uSunCol * uSunGain * (1.0 + uFlood);
    col += sunL * dif * shadow * 0.62 * (0.55 + 0.45 * hn);
    // light through the thin tops (the sun behind), and the silver lining on the edges against it
    float back = pow(max(mu, 0.0), 3.0);
    col += sunL * back * hn * hn * 0.35 * (0.5 + 0.5 * shadow);
    float edge = pow(1.0 - clamp(n.y, 0.0, 1.0), 1.5);
    col += uSunCol * uGlow * pow(max(mu, 0.0), 5.0) * (0.15 + edge * 1.4) * (0.3 + 0.7 * hn) * uSunGain;
    col *= 0.72 + 0.28 * hn;
    // mist settles in the valleys
    col = mix(col, uHaze * 0.9 + uShade * 0.1, (1.0 - hn) * (1.0 - hn) * 0.35);
    return col;
}

float stars(vec3 d, float az, float el) {
    vec2 s = vec2(az, el) * 95.0;
    vec2 id = floor(s);
    vec2 f = fract(s) - 0.5;
    float h = hash12(id + uSeed);
    if (h > 0.045) return 0.0;
    vec2 o = (vec2(hash12(id + 3.1), hash12(id + 7.7)) - 0.5) * 0.55;
    float r = length(f - o);
    float b = pow(hash12(id + 1.3), 3.0);
    float tw = 0.6 + 0.4 * sin(uTime * (0.7 + 2.0 * hash12(id + 9.1)) + h * 50.0);
    return exp(-r * r * 28.0) * (0.35 + 1.8 * b) * tw;
}

vec3 aurora(float az, float el) {
    if (uAurora <= 0.001 || el < -0.03) return vec3(0.0);
    vec3 acc = vec3(0.0);
    for (int k = 0; k < 2; k++) {
        float fk = float(k);
        float base = 0.12 + 0.09 * fk + 0.05 * sin(az * 2.1 + uTime * 0.045 + fk * 2.0) + 0.025 * sin(az * 5.3 - uTime * 0.07 + fk);
        float rays = textureLod(uNoise, vec2(az * 1.3 + fk * 0.31 + uTime * 0.004, 0.37 + fk * 0.2), 0.0).r;
        float fine = textureLod(uNoise, vec2(az * 7.0 - uTime * 0.01, 0.61 + fk * 0.13), 0.0).b;
        float curtain = smoothstep(0.42, 0.9, rays * 0.6 + fine * 0.6);
        float h = el - base;
        float prof = smoothstep(-0.05, 0.012, h) * exp(-max(h, 0.0) * (6.0 + 6.0 * (1.0 - curtain)));
        vec3 c = mix(uAurA, uAurB, smoothstep(0.0, 0.2, h));
        acc += c * prof * (0.06 + 1.1 * curtain) * (0.75 - 0.3 * fk);
    }
    return acc * uAurora * 0.8;
}

// High clouds: a veil (a stratus deck that can part around the sun) and cirrus streaks. rgb color, a coverage.
vec4 highClouds(vec3 d, float mu) {
    if (d.y < 0.004) return vec4(0.0);
    float t = 7.0 / d.y;
    vec2 q = d.xz * t + uHiDrift;
    float fade = smoothstep(0.004, 0.08, d.y) * (1.0 - smoothstep(0.55, 1.0, d.y));
    // cirrus: long thin streaks
    float c1 = textureLod(uNoise, q * vec2(0.004, 0.022) + vec2(0.3, 0.1), 0.0).r;
    float c2 = textureLod(uNoise, q * vec2(0.013, 0.07) + vec2(0.7, 0.4), 0.0).b;
    float ci = smoothstep(0.52, 0.8, c1 * 0.75 + c2 * 0.45) * uCirrus * fade;
    // the veil: soft billows
    float v1 = textureLod(uNoise, q * 0.010 + vec2(0.11, 0.83), 0.0).r;
    float v2 = textureLod(uNoise, q * 0.027 + vec2(0.51, 0.23), 0.0).g;
    float vv = smoothstep(0.38, 0.72, v1 * 0.7 + v2 * 0.45) * uVeil * fade;
    // a Tetris parts it from the sun outward
    float ang = acos(clamp(mu, -1.0, 1.0));
    vv *= 1.0 - (1.0 - smoothstep(uPart * 1.4 - 0.12, uPart * 1.4 + 0.1, ang)) * step(0.001, uPart);
    float back = pow(max(mu, 0.0), 5.0);
    vec3 vc = mix(uShade, uLit * uSunCol, 0.35 + 0.25 * d.y) + uGlow * back * 1.4 * uSunGain;
    vc += uSunCol * pow(max(mu, 0.0), 40.0) * 2.0 * (1.0 - vv) * uSunGain; // bright thin edges near the sun
    vec3 cc = mix(uHorizon, vec3(1.0), 0.35) * (0.8 + 0.4 * d.y) + uGlow * back * 1.2 * uSunGain;
    float a = 1.0 - (1.0 - vv * 0.9) * (1.0 - ci * 0.55);
    vec3 col = (vc * vv * 0.9 + cc * ci * 0.55 * (1.0 - vv * 0.9)) / max(a, 1e-4);
    return vec4(col, a);
}

// A towering cumulus: puffy bulbs piled into a tower, lit as spheres (a heightfield of discs), backlit rims,
// lightning inside. rgb color, a coverage.
vec4 tower(int i, float az, float el, float mu) {
    vec4 T = uTower[i];
    float g = uGrow[i];
    if (g < 0.01 || T.y <= 0.0) return vec4(0.0);
    float W = T.y, Ht = T.z * g;
    float baseEl = -0.03;
    if (abs(az - T.x) > W * (1.5 + uAnvil) || el > baseEl + Ht + W) return vec4(0.0);
    float best = -1.0, bestR = 1.0, edgeD = -1.0, wSum = 0.0;
    vec3 nAcc = vec3(0.0);
    vec3 nb = vec3(0.0, 0.0, 1.0);
    float lean = (hash11(float(i) * 3.3 + uSeed) - 0.5) * 0.6;
    for (int j = 0; j < 20; j++) {
        float fj = float(j) / 19.0;
        float h1 = hash11(float(i) * 17.3 + float(j) * 3.1 + uSeed);
        float h2 = hash11(float(i) * 7.9 + float(j) * 11.7 + uSeed);
        // a broad base of bulbs along the horizon, the tower narrowing as it climbs, a cauliflower crown
        float spread = mix(1.0, 0.42, smoothstep(0.0, 0.85, fj));
        float y = baseEl + Ht * (0.02 + 0.86 * pow(fj, 1.15)) + (h2 - 0.5) * W * 0.25;
        float x = T.x + (h1 * 2.0 - 1.0) * W * spread + lean * W * fj * 1.4;
        float r = W * (0.44 - 0.22 * fj) * (0.7 + 0.5 * h2) * mix(0.6, 1.0, g);
        vec2 c = vec2(x, y);
        vec2 sc = vec2(1.0 + uAnvil * smoothstep(0.7, 1.0, fj) * 1.1, 1.0 - uAnvil * smoothstep(0.7, 1.0, fj) * 0.25);
        vec2 q = (vec2(az, el) - c) / sc;
        float z2 = r * r - dot(q, q);
        edgeD = max(edgeD, (r - length(q)) * min(sc.x, sc.y));
        if (z2 > 0.0) {
            float z = sqrt(z2) + r * 0.25 * fj; // higher bulbs stand in front a little: overlapping puffs
            if (z > best) { best = z; bestR = r; }
            // the bulbs blend over a few pixels where they overlap (no hard seams)
            float w = exp(min(z / (W * 0.05), 60.0) - 30.0);
            nAcc += normalize(vec3(q, max(sqrt(z2), r * 0.45))) * w;
            wSum += w;
        }
    }
    if (wSum > 0.0) nb = normalize(nAcc);
    // cauliflower: small bumps on the edge and the faces
    vec4 nz = textureLod(uNoise, (vec2(az, el) - vec2(T.x, 0.0)) * (1.6 / W) * 0.18 + vec2(float(i) * 0.37, 0.0), 0.0);
    float edgeN = (nz.a - 0.5) * W * 0.12 + (nz.g - 0.5) * W * 0.08;
    float aa = 2.0 * uTanH / uRes.y;
    float cov = smoothstep(-aa * 0.7, aa * 0.7, edgeD + edgeN);
    if (cov <= 0.0) return vec4(0.0);
    nb = normalize(nb + vec3(nz.g - 0.5, nz.a - 0.5, 0.0) * 0.6);
    vec3 l = uSunDir;
    float dif = clamp((dot(nb, l) + 0.2) / 1.2, 0.0, 1.0);
    float rim = (1.0 - smoothstep(0.0, W * 0.12, edgeD + edgeN)) * 0.8 + pow(1.0 - clamp(nb.z, 0.0, 1.0), 3.0) * 0.3;
    vec3 amb = mix(uShade * 0.75, uLit * 0.7 + uZenith * 0.2, smoothstep(-0.6, 0.9, nb.y));
    vec3 col = amb + uLit * uSunCol * dif * uSunGain * 0.95;
    col += uSunCol * uGlow * rim * (0.12 + 1.3 * pow(max(mu, 0.0), 3.0)) * uSunGain;
    // lightning: the cloud lit from inside
    float fl = uFlash[i];
    if (fl > 0.001) {
        float inner = clamp(best / bestR, 0.0, 1.0);
        col += uFlashCol * fl * (0.5 + 1.5 * nz.r) * (0.3 + inner) * 1.3;
    }
    // far away: the haze, thicker toward its base
    float hz = clamp(T.w + (1.0 - T.w) * (1.0 - smoothstep(baseEl, baseEl + 0.06, el)) * 0.85, 0.0, 1.0);
    col = mix(col, uHaze * 0.85 + uHorizon * 0.15, hz);
    return vec4(col, cov);
}

void main() {
    vec3 rd = camDir(vUv);
    vec3 ro = vec3(0.0, mix(uEye, uAmp * 0.55, uSink), 0.0);
    float az = atan(rd.x, -rd.z), el = asin(clamp(rd.y, -1.0, 1.0));
    float mu = dot(rd, uSunDir);
    float tHit;
    bool hit = marchSea(ro, rd, tHit);
    vec3 col;
    float src = 0.0;
    if (!hit) {
        col = skyCol(rd);
        if (uStars > 0.001) col += vec3(0.88, 0.92, 1.0) * stars(rd, az, el) * uStars * smoothstep(0.0, 0.08, rd.y);
        col += aurora(az, el);
        // the sun, or the moon
        float a = acos(clamp(mu, -1.0, 1.0));
        float aa = 2.0 * uTanH / uRes.y;
        float disc = 1.0 - smoothstep(uSunSize - aa, uSunSize + aa, a);
        vec3 sunC = uSunCol * uSunGain * 9.0;
        vec2 mq = vec2(az, el) * 60.0;
        float maria = textureLod(uNoise, mq * 0.15 + 0.3, 0.0).r;
        vec3 moonC = vec3(0.95, 0.97, 1.0) * (1.55 - 0.5 * smoothstep(0.45, 0.7, maria));
        col = mix(col, mix(sunC, moonC, uMoon), disc);
        src = disc * 1.2 + pow(max(mu, 0.0), 80.0) * 0.3 * (1.0 - 0.6 * uMoon);
        vec4 hc = highClouds(rd, mu);
        col = mix(col, hc.rgb, hc.a);
        src *= 1.0 - hc.a * 0.92;
    } else {
        col = shadeSea(ro, rd, tHit);
        float f = 1.0 - exp(-tHit * uFog * (1.0 + 6.0 * uSink));
        col = mix(col, hazeAt(rd), f);
        src = pow(max(mu, 0.0), 30.0) * 0.12 * (1.0 - f);
    }
    float towerVis = hit ? smoothstep(TOWER_T - 25.0, TOWER_T + 10.0, tHit) : 1.0;
    if (towerVis > 0.0) {
        for (int i = 0; i < 6; i++) {
            vec4 tw = tower(i, az, el, mu);
            tw.a *= towerVis;
            col = mix(col, tw.rgb, tw.a);
            src *= 1.0 - tw.a;
        }
    }
    frag = vec4(max(col, 0.0), src);
}
)";

// ---------------------------------------------------------------- god rays: the light blurred toward the sun

inline const char* RAYS_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uSrc;   // alpha: the visible light
uniform vec2 uSunUv;
uniform float uDecay, uLength;
void main() {
    vec2 d = (uSunUv - vUv) * (uLength / 48.0);
    vec2 p = vUv + d * hash12(gl_FragCoord.xy); // dithered start: no banding
    float illum = 1.0, acc = 0.0;
    for (int i = 0; i < 48; i++) {
        acc += texture(uSrc, p).a * illum;
        illum *= uDecay;
        p += d;
    }
    frag = vec4(vec3(acc / 48.0), 1.0);
}
)";

// ---------------------------------------------------------------- the world at full resolution: panes, rays, fog

inline const char* COMPOSITE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uCloud, uRays, uNoise;
uniform vec4 uView;          // center xy, half extents (board units)
uniform float uPx;           // board units per pixel
uniform vec4 uWell, uColL, uColR; // panes (board x0 y0 x1 y1)
uniform vec3 uFrost, uRim, uRayCol;
uniform float uFrostAmt, uPaneAlpha, uRayGain, uTime;
uniform vec2 uSunScr;        // 2D direction toward the light (board space)
uniform vec2 uSunUv;
uniform float uFlare;
uniform vec3 uFlareCol;
uniform float uOverFog;
uniform vec3 uMist;
uniform float uBridge;
uniform vec3 uBridgeCol;
uniform float uCornerShade;
uniform vec3 uCornerCol;
uniform vec3 uPaneDeep;      // the well's glass: a deep tint of the sky, so the tiles stand out on light skies
uniform float uPaneDeepAmt;

float paneSd(vec2 p, vec4 r, float rad) {
    vec2 c = (r.xy + r.zw) * 0.5, h = (r.zw - r.xy) * 0.5;
    return sdRound(p - c, h, rad);
}
vec2 paneGrad(vec2 p, vec4 r, float rad) {
    float e = 0.02;
    float s = paneSd(p, r, rad);
    return normalize(vec2(paneSd(p + vec2(e, 0.0), r, rad) - s, paneSd(p + vec2(0.0, e), r, rad) - s) + 1e-6);
}

void main() {
    vec2 uv = vUv;
    vec2 p = uView.xy + (uv * 2.0 - 1.0) * uView.zw;
    vec3 col = texture(uCloud, uv).rgb;
    float rays = texture(uRays, uv).r;
    col += uRayCol * rays * uRayGain;

    // the floating panes of frosted glass: the well and the two HUD columns
    float sW = paneSd(p, uWell, 0.32);
    float sL = paneSd(p, uColL, 0.5), sR = paneSd(p, uColR, 0.5);
    float inW = 1.0 - smoothstep(-1.5 * uPx, 1.5 * uPx, sW);
    float inC = 1.0 - smoothstep(-1.5 * uPx, 1.5 * uPx, min(sL, sR));
    float inside = max(inW, inC);
    if (inside > 0.0) {
        vec3 blur = textureLod(uCloud, uv, 4.2).rgb * 0.6 + textureLod(uCloud, uv, 3.0).rgb * 0.4;
        blur += uRayCol * rays * uRayGain * 0.5;
        float amt = mix(uFrostAmt * 0.75, uFrostAmt, inW);
        // the glass is frosted more toward its top, where the light catches it
        float k = clamp((p.y - uWell.y) / (uWell.w - uWell.y), 0.0, 1.0);
        vec3 frost = mix(blur, uFrost, amt * (0.8 + 0.25 * k));
        float grain = texture(uNoise, p * 0.9).b * 0.5 + texture(uNoise, p * 2.7).r * 0.5;
        frost *= 0.97 + 0.06 * grain;
        frost = mix(frost, uPaneDeep * (0.9 + 0.2 * grain), uPaneDeepAmt * inW);
        col = mix(col, frost, inside * uPaneAlpha);
    }
    // quiet behind the corner where the universe's name is written
    {
        vec2 corner = vec2(uView.x - uView.z, uView.y - uView.w);
        float m = exp(-length((p - corner) * vec2(0.075, 0.28)) * 2.0);
        col = mix(col, uCornerCol, m * uCornerShade);
    }
    // a Tetris: the sun bursts through, a long soft streak and a glow over everything
    if (uFlare > 0.001) {
        vec2 q = (uv - uSunUv) * vec2(uView.z / uView.w, 1.0);
        float r = length(q);
        col += uFlareCol * uFlare * (exp(-r * 5.0) * 0.9 + exp(-abs(q.y) * 70.0) * exp(-abs(q.x) * 1.8) * 0.5);
    }
    // game over: the sea of clouds rises over the screen from below
    if (uOverFog > 0.001) {
        float n = texture(uNoise, p * 0.05 + vec2(uTime * 0.01, -uTime * 0.03)).r * 0.6
                + texture(uNoise, p * 0.13 + vec2(-uTime * 0.02, -uTime * 0.05)).a * 0.4;
        float top = mix(uView.y - uView.w - 2.0, uView.y + 1.0, uOverFog);
        float m = smoothstep(top + 2.5, top - 2.5, p.y + (n - 0.5) * 5.0);
        col = mix(col, uMist * (0.85 + 0.3 * n), m * 0.92);
    }
    // the bridge (a change of universe): the clouds close in
    if (uBridge > 0.0) {
        float n = texture(uNoise, p * 0.035 + vec2(uTime * 0.012, uTime * 0.004)).r * 0.55
                + texture(uNoise, p * 0.09 + vec2(-uTime * 0.02, 0.0)).g * 0.45;
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

// a0: center xy, half size xy (board). a1: rgb, alpha. a2: kind, age, angle, fog. a3: extra.
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
    float c = cos(a2.z), s = sin(a2.z);
    vec2 p = a0.xy + vec2(c * l.x - s * l.y, s * l.x + c * l.y);
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
uniform vec2 uSunScr;
uniform vec3 uSkyTint, uSunTint, uShadow, uMist, uRim, uGaugeSun, uGaugeMoon, uGaugeTrack;
uniform float uNight, uBridge, uTime;
uniform vec3 uBridgeInk;

float tileSd(vec2 p, float s) { return sdRound(p, vec2(0.43 * s), 0.12 * s); }

// A flat tile: a soft-colored rounded square, a whisper of vertical gradient (lighter at the top), no relief.
vec3 flatTile(vec2 p, float s, vec3 pc) {
    float ty = clamp(p.y / (0.43 * s), -1.0, 1.0);
    vec3 col = pc * (0.96 + 0.07 * ty);
    // richer than the sky's pastels, so the tiles read at a glance
    float l = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = max(mix(vec3(l), col, 1.2), 0.0);
    // at night the colors glow faintly (readable under the moon)
    col += pc * uNight * 0.18;
    return col;
}

void main() {
    int kind = int(v2.x + 0.5);
    vec3 pc = v1.rgb;
    float alpha = v1.a;
    float age = v2.y;
    vec2 p = vLocal;
    float px = uPx;
    float bridgeK = smoothstep(0.2, 0.85, uBridge);
    if (kind == 0 || kind == 3) {
        float s = v3.x;
        float sd = tileSd(p, s);
        float cov = 1.0 - smoothstep(-px * 0.7, px * 0.7, sd);
        if (cov <= 0.0) discard;
        vec3 col = flatTile(p, s, pc);
        if (kind == 0 && age >= 0.0) {
            // lock: a cool breath of light through the glaze, fading
            float fl = exp(-age * 4.0);
            col = mix(col, mix(pc, vec3(1.0), 0.6) * 1.15, fl * 0.6);
        }
        // falling through the sea of clouds (game over): into the mist
        col = mix(col, uMist, v2.w);
        col = mix(col, uBridgeInk, bridgeK);
        float a = cov * alpha;
        frag = vec4(col * a, a);
        return;
    }
    if (kind == 5) {
        // the soft shadow a tile casts on the glass
        float s = v3.x;
        float sd = tileSd(p, s);
        float a = (1.0 - smoothstep(-0.12 * s, 0.2 * s, sd)) * alpha * 0.3 * (1.0 - uBridge);
        frag = vec4(uShadow * a, a);
        return;
    }
    if (kind == 1) {
        // ghost: a breath of mist outlined in light
        float sd = tileSd(p, 1.0);
        float line = (1.0 - smoothstep(0.012, 0.012 + px * 1.2, abs(sd + 0.02))) * 0.45;
        float fill = (1.0 - smoothstep(-px, px, sd)) * 0.16;
        vec3 col = pc * (line + fill);
        float a = (line * 0.85 + fill) * alpha * (1.0 - uBridge);
        frag = vec4(col * alpha * (1.0 - uBridge), a);
        return;
    }
    if (kind == 2) {
        // a cleared cell dissolving into mist: v3.y progress 0..1, v3.z seed
        float k = v3.y;
        float sd = tileSd(p, 1.0);
        float cov = 1.0 - smoothstep(-px * 0.7, px * 0.7, sd);
        if (cov <= 0.0) discard;
        float n = texture(uNoise, vWorld * 0.09 + v3.z).r * 0.7 + texture(uNoise, vWorld * 0.23 + v3.z * 1.7).g * 0.3;
        float th = k * 1.25 - 0.1;
        float keep = smoothstep(th, th + 0.08, n);
        float edge = exp(-pow((n - th) / 0.05, 2.0)) * (1.0 - k);
        vec3 col = flatTile(p, 1.0, pc);
        col = mix(col, uMist * 1.15, smoothstep(0.0, 0.6, k));
        col += vec3(1.0) * edge * 0.8;
        float a = cov * max(keep, edge * 0.8) * alpha * (1.0 - uBridge);
        frag = vec4(col * a, a);
        return;
    }
    if (kind == 4) {
        // the bonus gauge: a halo around the sun, filled clockwise; a small sun when ready, the moon when active
        float R = v3.w;
        vec2 q = p / R;
        float r = length(q);
        float aa = px / R * 1.5;
        float a01 = fract(atan(q.x, q.y) / 6.2831853 + 1.0);
        float lvl = v3.x, ready = v3.y, isOn = v3.z;
        float ring = exp(-pow((r - 0.8) / (0.055 + aa), 2.0));
        float fill = 1.0 - smoothstep(lvl - 0.004, lvl + 0.004, a01);
        if (lvl >= 0.999) fill = 1.0;
        float back = (1.0 - smoothstep(0.98 - aa, 0.98 + aa, r));
        vec3 col = uMist * back * 0.18;
        float a = back * 0.22;
        col += uGaugeTrack * ring * 0.5;
        a = max(a, ring * 0.5);
        float pulse = 0.75 + 0.25 * sin(uTime * 4.0);
        vec3 fc = uGaugeSun * 1.3;
        col += fc * ring * fill * (1.0 + ready * pulse);
        a = max(a, ring * fill);
        float disc = 1.0 - smoothstep(0.42 - aa, 0.42 + aa, r);
        if (isOn > 0.5) {
            // the moon: a crescent
            float sh = 1.0 - smoothstep(0.42 - aa, 0.42 + aa, length(q - vec2(0.16, 0.1)));
            float m = disc * (1.0 - sh * 0.85);
            col = mix(col, uGaugeMoon * 1.4, m);
            a = max(a, m);
        } else if (ready > 0.5) {
            float rays = pow(max(cos(atan(q.y, q.x) * 8.0 + uTime * 0.8), 0.0), 6.0) * smoothstep(0.95, 0.42, r) * (1.0 - disc);
            col = mix(col, uGaugeSun * (1.6 + 0.6 * pulse), disc);
            col += uGaugeSun * rays * 0.9 * pulse;
            a = max(a, max(disc, rays * 0.8));
        }
        col = mix(col, uBridgeInk * a, uBridge);
        frag = vec4(col * alpha, a * alpha);
        return;
    }
    discard;
}
)";

// ---------------------------------------------------------------- mist puffs (premultiplied)

// a0: center xy, radius, stretch x. a1: rgb, alpha. a2: seed, swirl, 0, 0.
inline const char* MIST_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
layout(location = 2) in vec4 a1;
layout(location = 3) in vec4 a2;
uniform mat4 uVP;
out vec2 vLocal;
flat out vec4 v1, v2;
void main() {
    vLocal = aCorner;
    v1 = a1; v2 = a2;
    vec2 p = a0.xy + aCorner * vec2(a0.z * a0.w, a0.z);
    gl_Position = uVP * vec4(p, 0.0, 1.0);
}
)";
inline const char* MIST_FS = R"(#version 330 core
in vec2 vLocal;
flat in vec4 v1, v2;
out vec4 frag;
uniform sampler2D uNoise;
uniform float uBridge;
void main() {
    float r = length(vLocal);
    if (r > 1.0) discard;
    vec2 sw = vec2(v2.y * 0.15, -v2.y * 0.1);
    float n = texture(uNoise, vLocal * 0.28 + v2.x + sw).g * 0.55 + texture(uNoise, vLocal * 0.7 + v2.x * 1.7 - sw).r * 0.45;
    float m = smoothstep(1.0, 0.15, r + (n - 0.5) * 1.1);
    float a = m * m * v1.a * (1.0 - uBridge);
    frag = vec4(v1.rgb * a, a);
}
)";

// ---------------------------------------------------------------- lines: birds, glints, streaks

// a0: a.xy, b.xy (board). a1: rgb. a2: width, glow, kind (0 glow line, 1 glint, 2 solid), alpha.
inline const char* LINE_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
layout(location = 2) in vec4 a1;
layout(location = 3) in vec4 a2;
uniform mat4 uVP;
uniform float uPx;
out vec2 vP;
flat out vec4 v0, v1, v2;
void main() {
    vec2 a = a0.xy, b = a0.zw;
    float ext = a2.x * 0.5 + a2.y * 3.0 + uPx * 1.5;
    vec2 p;
    if (int(a2.z + 0.5) == 1) {
        p = a + aCorner * a2.x;
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
uniform float uBridge;
void main() {
    int kind = int(v2.z + 0.5);
    vec3 c = v1.rgb;
    float alpha = v2.w * (1.0 - uBridge);
    if (kind == 1) {
        vec2 q = (vP - v0.xy) / v2.x;
        float r = length(q);
        float rays = exp(-abs(q.y) * 34.0) * (1.0 - smoothstep(0.0, 1.0, abs(q.x)))
                   + exp(-abs(q.x) * 34.0) * (1.0 - smoothstep(0.0, 1.0, abs(q.y)));
        float core = exp(-r * r / 0.006) * 1.5 + exp(-r * 6.0) * 0.35;
        frag = vec4(c * (rays + core) * alpha, 0.0);
        return;
    }
    vec2 a = v0.xy, b = v0.zw;
    vec2 pa = vP - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    float d = length(pa - ba * h);
    float w = max(v2.x * 0.5, uPx * 0.5);
    float core = 1.0 - smoothstep(w - uPx * 0.5, w + uPx * 0.5, d);
    if (kind == 2) {
        float a2 = core * alpha * clamp(v2.x / uPx, 0.35, 1.0);
        frag = vec4(c * a2, a2);
        return;
    }
    float glow = exp(-d / max(v2.y, 1e-4));
    frag = vec4((c * core + c * glow * 0.5) * alpha, 0.0);
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
    // a soft halo of mist behind the letters (readable over any sky)
    float halo = smoothstep(0.12, 0.5, d);
    halo = halo * halo * halo * uHaloAmt * (1.0 - uBridge);
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
    // a soft filmic shoulder: highlights roll into white, the pastels stay
    vec3 over = max(c - 1.0, 0.0);
    c += (over.r + over.g + over.b) * 0.12;
    const float K = 0.66;
    c = mix(c, K + (1.0 - K) * (1.0 - exp(-(c - K) / (1.0 - K))), step(K, c));
    // a light vignette, like the edge of a window
    vec2 q = vUv - 0.5;
    c *= 1.0 - 0.22 * dot(q, q) * 1.6 * (1.0 - uBridge);
    c = mix(c, raw, uBridge);
    float g = hash12(gl_FragCoord.xy + fract(uTime * 7.13) * 431.0) - 0.5;
    c += g * 0.012;
    c *= 1.0 - uDim * 0.55;
    c *= uFade;
    frag = vec4(pow(max(c, 0.0), vec3(1.0 / 2.2)), 1.0);
}
)";

} // namespace cloudscape_shaders
