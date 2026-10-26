#pragma once
// LASER SHOW's shaders (see laser.cpp). Everything is drawn in linear HDR (the beams' cores and the pieces' lit edges
// go above 1 and bloom), then tone-mapped in the post.
//
// The beams are rays in the board plane from projectors out of frame, seen through a haze: HAZE_FS draws the light
// the haze scatters around them (a soft glow and a wide, faint halo, both following the haze's density) at a reduced
// resolution; BEAM_VS / BEAM_FS draw each beam's thin core at full resolution, its brightness following the same haze.
// WELL_FS lays the well over them, a slab of black glass whose rim catches the beams passing behind it.

namespace laser_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }
float sdRound(vec2 p, vec2 b, float r) { vec2 d = abs(p) - b + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }
// The haze a beam crosses: a slow 3D noise (the beam's depth picks its slice), so each beam is uneven along its length
// and no two beams are lit the same.
uniform sampler3D uVol;
uniform vec3 uFlow;
uniform float uHazeVar, uHazeScale;
float hazeAt(vec2 p, float depth) {
    vec3 q = vec3(p * uHazeScale, depth) + uFlow;
    float a = textureLod(uVol, q, 0.0).r;
    float b = textureLod(uVol, q * 2.7 + vec3(0.37, 0.11, 0.53) + uFlow.yzx * 0.5, 0.0).g;
    float c = textureLod(uVol, q * 6.1 + vec3(0.71, 0.29, 0.13) - uFlow.zxy * 0.8, 0.0).r;
    float h = a * 0.6 + b * 0.32 + c * 0.16;
    // patches of thick haze where the beam glows, thin gaps where it nearly vanishes
    float v = smoothstep(0.36, 0.78, h);
    return mix(1.0, 0.04 + 2.6 * v * v * v, uHazeVar);
}
// The HUD's quiet zones: the beams fade behind the text, the previews and the gauge.
uniform vec4 uMask[24];
uniform int uNMask;
float hudQuiet(vec2 p) {
    float m = 0.0;
    for (int i = 0; i < uNMask; i++) {
        vec4 r = uMask[i];
        float d = sdBox(p - (r.xy + r.zw) * 0.5, (r.zw - r.xy) * 0.5);
        m = max(m, 1.0 - smoothstep(-0.4, 1.4, d));
    }
    return 1.0 - 0.72 * m;
}
)";

// ---------------------------------------------------------------- the haze (reduced resolution)

inline const char* HAZE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uView;          // center xy, half extents (board units)
uniform vec4 uBA[48];        // beams: origin xy, direction xy
uniform vec4 uBB[48];        // beams: color (intensity in it), depth in the haze
uniform int uNB;
uniform float uGlowW, uHaloW, uHalo, uAmbient;
uniform vec3 uAmbientCol;
uniform float uThick;        // slow motion: a thick haze, the beams like rods
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    vec3 acc = vec3(0.0);
    float quiet = hudQuiet(p);
    for (int i = 0; i < uNB; i++) {
        vec2 o = uBA[i].xy, d = uBA[i].zw;
        vec2 v = p - o;
        float s = dot(v, d);
        if (s < 0.0) continue;
        float perp = abs(v.x * d.y - v.y * d.x);
        float h = hazeAt(p, uBB[i].w);
        float g = exp(-perp * perp / uGlowW) * 0.3 + exp(-perp / uHaloW) * uHalo;
        acc += uBB[i].rgb * g * h / (1.0 + s * 0.035);
    }
    acc *= quiet;
    // the haze itself, faintly lit by the light scattered around the room
    float hz = hazeAt(p * 0.6, 0.31);
    acc += uAmbientCol * uAmbient * (0.55 + 0.45 * hz);
    acc *= 1.0 + uThick * 0.8 * (hz - 0.4);
    frag = vec4(max(acc, 0.0), 1.0);
}
)";

// ---------------------------------------------------------------- the room at full resolution

inline const char* COMPOSITE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uHazeT, uNoise;
uniform vec2 uHazeTexel;
uniform vec4 uView;
uniform float uTime;
uniform vec3 uBack;
uniform float uBridge;
uniform vec3 uBridgeCol;
uniform float uOverFog;
uniform vec3 uMist;
void main() {
    vec2 uv = vUv;
    vec2 p = uView.xy + (uv * 2.0 - 1.0) * uView.zw;
    vec3 hz = texture(uHazeT, uv).rgb * 0.4;
    hz += texture(uHazeT, uv + uHazeTexel * vec2(0.7, 0.4)).rgb * 0.15;
    hz += texture(uHazeT, uv + uHazeTexel * vec2(-0.4, 0.7)).rgb * 0.15;
    hz += texture(uHazeT, uv + uHazeTexel * vec2(-0.7, -0.4)).rgb * 0.15;
    hz += texture(uHazeT, uv + uHazeTexel * vec2(0.4, -0.7)).rgb * 0.15;
    vec3 col = uBack + hz;
    if (uOverFog > 0.001) {
        float n = texture(uNoise, p * 0.04 + vec2(uTime * 0.006, -uTime * 0.02)).r * 0.6
                + texture(uNoise, p * 0.11 + vec2(-uTime * 0.012, -uTime * 0.03)).g * 0.4;
        col = mix(col, uMist * (0.6 + 0.8 * n), uOverFog * 0.6);
    }
    // the bridge (a change of universe): the haze thickens until it fills the room
    if (uBridge > 0.0) {
        float n = texture(uNoise, p * 0.03 + vec2(uTime * 0.01, uTime * 0.02)).r * 0.55
                + texture(uNoise, p * 0.08 + vec2(-uTime * 0.02, uTime * 0.01)).g * 0.45;
        float w = smoothstep(0.0, 1.0, uBridge * 2.0 - 0.5 + (n - 0.5) * 1.0);
        w = max(w, smoothstep(0.88, 0.99, uBridge));
        col = mix(col, uBridgeCol, w);
    }
    frag = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- the beams' cores (full resolution, additive)

// One instance per beam: a long thin quad from the projector. a0: origin xy, direction xy; a1: color (intensity in
// it), depth; a2: core half width (board), length, unused, unused.
inline const char* BEAM_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
layout(location = 2) in vec4 a1;
layout(location = 3) in vec4 a2;
uniform mat4 uVP;
uniform float uPx;
out vec2 vWorld;
out float vAcross;
flat out vec4 v0, v1, v2;
void main() {
    vec2 d = a0.zw;
    vec2 n = vec2(-d.y, d.x);
    float hw = max(a2.x * 3.5, uPx * 3.0);
    float along = (aCorner.x * 0.5 + 0.5) * a2.y;
    vec2 p = a0.xy + d * along + n * aCorner.y * hw;
    vWorld = p;
    vAcross = aCorner.y * hw;
    v0 = a0; v1 = a1; v2 = a2;
    gl_Position = uVP * vec4(p, 0.0, 1.0);
}
)";
inline const char* BEAM_FS = R"(#version 330 core
in vec2 vWorld;
in float vAcross;
flat in vec4 v0, v1, v2;
out vec4 frag;
uniform float uPx, uBridge;
uniform vec2 uWellH;
void main() {
    float w = max(v2.x, uPx * 0.6);
    float x = vAcross / w;
    float core = exp(-x * x);
    float s = dot(vWorld - v0.xy, v0.zw);
    float h = hazeAt(vWorld, v1.w);
    // a narrow core, thinner than a pixel at most: keep its energy (a sub-pixel beam is dimmer, not wider)
    float k = min(1.0, v2.x / (uPx * 0.6));
    float behind = 1.0 - 0.985 * (1.0 - smoothstep(-0.05, 0.05, sdBox(vWorld, uWellH)));
    vec3 c = v1.rgb * core * h * k * hudQuiet(vWorld) * behind / (1.0 + s * 0.035);
    frag = vec4(c * (1.0 - uBridge), 0.0);
}
)";

// ---------------------------------------------------------------- the well: black glass, its rim catching the beams

inline const char* WELL_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uView;
uniform float uPx;
uniform vec4 uWell;
uniform vec3 uGlass, uRim;
uniform float uGlassAlpha, uRimBase;
uniform vec4 uBA[48];
uniform vec4 uBB[48];
uniform int uNB;
uniform float uBridge;
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    vec2 wc = (uWell.xy + uWell.zw) * 0.5, wh = (uWell.zw - uWell.xy) * 0.5;
    float sW = sdRound(p - wc, wh, 0.06);
    if (sW > 0.6) discard;
    float inW = 1.0 - smoothstep(-uPx, uPx, sW);
    // the beams passing behind the rim light it where they cross
    float line = exp(-abs(sW) / (uPx * 0.9));
    vec3 lit = vec3(0.0);
    if (abs(sW) < 0.5) {
        for (int i = 0; i < uNB; i++) {
            vec2 v = p - uBA[i].xy;
            float s = dot(v, uBA[i].zw);
            if (s < 0.0) continue;
            float perp = abs(v.x * uBA[i].w - v.y * uBA[i].z);
            lit += uBB[i].rgb * exp(-perp * perp / 0.05);
        }
    }
    vec3 glass = uGlass;
    float a = inW * uGlassAlpha;
    vec3 rim = (uRim * uRimBase + lit * 1.3) * line * (1.0 - uBridge);
    frag = vec4(glass * a + rim, a);
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

// a0: center xy, half size xy (board). a1: rgb, alpha. a2: kind, neighbour mask, lock flash age (< 0 none), scale.
// a3: extra (burn: progress, seed; gauge: level, ready, active, radius).
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
uniform float uPx, uTime, uBridge, uLight;
uniform vec3 uBridgeInk, uGaugeCol, uGaugeSlow, uTrack;

// The silhouette of a piece seen from one of its cells (cell units, the cell's center at 0): the sides shared with a
// cell of the same piece open (no seam inside a piece), the others inset by a hairline gap; the inner corners notched.
float pieceSd(vec2 p, int m) {
    const float IN = 0.45, OUT = 4.0;
    float l = (m & 1) != 0 ? OUT : IN, r = (m & 2) != 0 ? OUT : IN;
    float u = (m & 4) != 0 ? OUT : IN, d = (m & 8) != 0 ? OUT : IN;
    vec2 c = vec2((r - l) * 0.5, (u - d) * 0.5), h = vec2((l + r) * 0.5, (u + d) * 0.5);
    float sd = sdRound(p - c, h, 0.07);
    // inner corners: both sides shared, the diagonal not
    if ((m & 5) == 5 && (m & 16) == 0) sd = max(sd, min(-IN - p.x, p.y - IN));
    if ((m & 6) == 6 && (m & 32) == 0) sd = max(sd, min(p.x - IN, p.y - IN));
    if ((m & 9) == 9 && (m & 64) == 0) sd = max(sd, min(-IN - p.x, -IN - p.y));
    if ((m & 10) == 10 && (m & 128) == 0) sd = max(sd, min(p.x - IN, -IN - p.y));
    return sd;
}

// Dark acrylic, edge-lit: a smoked, tinted body; the light piped through it glows along the silhouette's edge (a
// crisp bright line just inside it, a softer glow fading inward).
vec3 acrylic(float e, vec3 pc, float px, float glow) {
    float line = exp(-pow((e - 0.04) / (0.022 + px * 0.6), 2.0));
    float inner = exp(-e / 0.075);
    float deep = exp(-e / 0.3);
    vec3 col = pc * (0.07 + 0.08 * deep);
    col += pc * inner * 0.42 * glow;
    col += (pc * 1.5 + vec3(0.18)) * line * glow;
    return col;
}

void main() {
    int kind = int(v2.x + 0.5);
    vec3 pc = v1.rgb;
    float alpha = v1.a;
    float s = v2.w;
    float px = uPx / s;
    vec2 p = vLocal / s;
    int m = int(v2.y + 0.5);
    float bridgeK = smoothstep(0.2, 0.85, uBridge);
    if (kind == 0) {
        // a block: v3.x its light (1 on, 0 off at game over)
        float sd = pieceSd(p, m);
        float cov = 1.0 - smoothstep(-px * 0.7, px * 0.7, sd);
        if (cov <= 0.0) discard;
        float e = max(-sd, 0.0);
        float glow = v3.x * uLight;
        float age = v2.z;
        if (age >= 0.0) glow *= 1.0 + 1.1 * exp(-age * 7.0);
        vec3 col = acrylic(e, pc, px, glow);
        col = mix(col, uBridgeInk, bridgeK);
        float a = cov * alpha;
        frag = vec4(col * a, a);
        return;
    }
    if (kind == 1) {
        // the ghost: the silhouette's edge only, faint
        float sd = pieceSd(p, m);
        float line = exp(-pow((sd + 0.035) / (0.018 + px * 0.7), 2.0));
        float a = line * 0.4 * alpha * (1.0 - uBridge);
        frag = vec4(pc * 0.9 * a, a * 0.5);
        return;
    }
    if (kind == 2) {
        // a cleared block burning out: its edge flares, then a burning front eats it from the edges in (a hot rim
        // running ahead of the char), gone at v3.x = 1. v3.y seed.
        float k = v3.x;
        float sd = pieceSd(p, m);
        float cov = 1.0 - smoothstep(-px * 0.7, px * 0.7, sd);
        if (cov <= 0.0) discard;
        float e = max(-sd, 0.0);
        float n = texture(uNoise, vWorld * 0.31 + v3.y).r * 0.6 + texture(uNoise, vWorld * 0.13 + v3.y * 1.7).b * 0.4;
        float field = e * 1.9 + (n - 0.5) * 0.55;
        float th = smoothstep(0.08, 1.0, k) * 1.35 - 0.05;
        float keep = smoothstep(th, th + 0.05, field);
        float front = exp(-pow((field - th) / 0.045, 2.0)) * step(0.08, k);
        float flare = 1.0 + 1.4 * smoothstep(0.0, 0.08, k);
        vec3 col = acrylic(e, pc, px, flare * uLight) * keep;
        col *= mix(1.0, 0.5, smoothstep(0.1, 0.6, k)); // the body chars ahead of the front
        vec3 hot = mix(pc, vec3(1.0), 0.55) * 2.6;
        col += hot * front;
        float a = cov * max(keep, front * 0.9) * alpha * (1.0 - uBridge);
        frag = vec4(col * cov * alpha * (1.0 - uBridge), a);
        return;
    }
    if (kind == 4) {
        // the bonus gauge: a ring of laser light drawn clockwise from the top as it fills; whole and brighter when
        // ready, cold white while the slow motion drains it
        float R = v3.w;
        vec2 q = vLocal / R;
        float r = length(q);
        float aa = uPx / R;
        float lvl = v3.x, ready = v3.y, isOn = v3.z;
        float ang = atan(q.x, q.y);                 // 0 at the top, clockwise
        float t = fract(ang / 6.2831853 + 1.0);
        float ring = exp(-pow((r - 0.86) / (0.018 + aa), 2.0));
        float fill = 1.0 - smoothstep(lvl - 0.004, lvl + 0.004, t);
        vec3 lc = isOn > 0.5 ? uGaugeSlow : uGaugeCol;
        vec3 col = uTrack * ring * 0.35;
        col += lc * ring * fill * (1.4 + 0.8 * ready);
        // a soft halo around the ring once it is whole
        col += lc * exp(-pow((r - 0.86) / 0.12, 2.0)) * 0.18 * max(ready, isOn);
        float a = clamp(ring, 0.0, 1.0) * 0.6;
        col = mix(col, uBridgeInk * ring, uBridge);
        frag = vec4(col * alpha, a * alpha * 0.0);
        return;
    }
    discard;
}
)";

// ---------------------------------------------------------------- sparks (additive): a lock's touch-down

// a0: center xy, radius, streak length. a1: rgb, alpha. a2: kind, progress, unused, unused.
inline const char* SPARK_VS = R"(#version 330 core
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
    vec2 ext = vec2(max(a0.z, a0.w), a0.z);
    gl_Position = uVP * vec4(a0.xy + aCorner * ext, 0.0, 1.0);
}
)";
inline const char* SPARK_FS = R"(#version 330 core
in vec2 vLocal;
flat in vec4 v0, v1, v2;
out vec4 frag;
uniform float uBridge;
void main() {
    vec2 ext = vec2(max(v0.z, v0.w), v0.z);
    vec2 q = vLocal * ext;
    // a point of light where the piece touched down, a short streak along the contact
    float dot0 = exp(-dot(q, q) / (v0.z * v0.z * 0.08));
    float streak = exp(-q.y * q.y / (v0.z * v0.z * 0.012)) * exp(-abs(q.x) / (v0.w * 0.35));
    float a = (dot0 + streak * 0.6) * v1.a * (1.0 - uBridge);
    frag = vec4(v1.rgb * a, 0.0);
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
uniform vec3 uColor;
uniform float uAlpha, uHaloAmt, uBias, uBridge;
uniform vec3 uBridgeInk;
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.75;
    float fill = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    // a soft pool of darkness behind the letters (readable when a beam passes behind them)
    float halo = smoothstep(0.1, 0.5, d);
    halo = halo * halo * uHaloAmt * (1.0 - uBridge);
    vec3 fc = mix(uColor, uBridgeInk, uBridge);
    float a = fill + (1.0 - fill) * halo;
    frag = vec4(fc * fill * uAlpha, a * uAlpha);
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

// A clean finish: a soft shoulder (the beams' cores roll to white, their color kept around them), a soft bloom, a
// light vignette, a fine grain.
inline const char* POST_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene, uBloom;
uniform vec2 uRes;
uniform float uTime, uFade, uDim, uBloomAmt, uBridge, uGrain;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    c += texture(uBloom, vUv).rgb * uBloomAmt;
    vec3 raw = c;
    vec2 q = vUv - 0.5;
    c *= 1.0 - 0.4 * dot(q * vec2(1.0, 1.2), q * vec2(1.0, 1.2)) * 1.6 * (1.0 - uBridge);
    vec3 over = max(c - 1.0, 0.0);
    c += (over.r + over.g + over.b) * 0.12;
    const float K = 0.62;
    c = mix(c, K + (1.0 - K) * (1.0 - exp(-(c - K) / (1.0 - K))), step(K, c));
    c = mix(c, raw, uBridge);
    float g = hash12(gl_FragCoord.xy + fract(uTime * 7.13) * 431.0) - 0.5;
    c = pow(max(c, 0.0), vec3(1.0 / 2.2));
    c += g * uGrain * (1.0 - uBridge);
    c *= 1.0 - uDim * 0.55;
    c *= uFade;
    frag = vec4(max(c, 0.0), 1.0);
}
)";

} // namespace laser_shaders
