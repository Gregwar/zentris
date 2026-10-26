#pragma once
// ABYSS's shaders (see abyss.cpp). Everything is drawn in linear HDR: the water is near black, the living lights go
// above 1 and bloom; the post tone-maps, vignettes and dithers (dark gradients band without it).

namespace abyss_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
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
float fbm3(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) { s += a * vnoise(p); p = p * 2.07 + vec2(5.1, 1.3); a *= 0.5; }
    return s / 0.875;
}
float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }
float sdRound(vec2 p, vec2 b, float r) { vec2 d = abs(p) - b + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }
float sdSeg(vec2 p, vec2 a, vec2 b) { vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0); return length(pa - ba * h); }
// The seabed's height at x (board units), shared with the C++ side (bedY in abyss.cpp).
float bedY(float x, float seed) {
    float ax = abs(x);
    return -10.38 - 0.55 * smoothstep(5.4, 8.0, ax) + 3.4 * smoothstep(10.0, 23.0, ax)
           + 0.32 * sin(x * 0.8 + seed) + 0.16 * sin(x * 2.7 + seed * 1.7) + 0.07 * sin(x * 7.3);
}
)";

// ---------------------------------------------------------------- the water: depth, light, distant life, seabed

inline const char* WORLD_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uView;          // center xy, half extents (board units)
uniform float uPx, uTime, uLv, uSeed;
uniform vec3 uDeep, uWater, uSurf, uGlow, uGlow2;
uniform float uRays;         // light from the far surface (calm)
uniform float uLights;       // how many distant lights
uniform float uPlankton;     // plankton clouds (mid, peak)
uniform float uBloom;        // their flare on a kick
uniform float uBio;          // peak: the bioluminescence blooms through the water
uniform float uSurge;        // the drop's first seconds: it blooms brighter still
uniform float uKick;         // the kick's envelope, at the peak (a brightness pulse)
uniform float uDrive;        // 0 a chorus' peak .. 1 a drop's: denser, warmer, hit harder by the kicks
uniform float uBeat;         // creatures' light pulse
uniform float uFlash;
uniform float uTetris;       // seconds since a Tetris (the plankton bloom)
uniform vec3 uTetrisCol;
uniform float uSlow;         // the abyss goes dark
uniform vec4 uLure;          // anglerfish lure: x, y, intensity, facing
uniform vec3 uLureCol;
uniform float uOut;          // lights going out (game over, bridge)
uniform float uOver;
uniform float uBridge;
uniform vec3 uBridgeCol;
uniform vec4 uWell, uColL, uColR;

// Distant living lights: a sparse field, each blinking at its own pace, going out one by one with uOut.
vec3 lightField(vec2 p, float scale, float dens, float salt, float bright) {
    vec2 g = p * scale + vec2(uTime * 0.03, uTime * 0.012) * scale;
    vec2 id = floor(g);
    vec2 f = fract(g) - 0.5;
    float h = hash12(id + salt + uSeed);
    if (h > dens) return vec3(0.0);
    float h2 = hash12(id + salt + 7.7), h3 = hash12(id + salt + 3.1);
    if (h2 < uOut) return vec3(0.0);
    vec2 o = (vec2(h3, hash12(id + 9.4 + salt)) - 0.5) * 0.6;
    o += 0.08 * vec2(sin(uTime * 0.5 + h * 40.0), cos(uTime * 0.37 + h2 * 30.0));
    float d = length(f - o) / scale;
    float size = mix(0.025, 0.07, h3 * h3);
    // a slow blink: mostly a faint ember, now and then a bright flash
    float blink = 0.25 + 0.75 * pow(0.5 + 0.5 * sin(uTime * (0.3 + 0.9 * h2) + h * 60.0), 8.0);
    blink += uBeat * 0.6 * step(0.55, h3);
    float core = exp(-d * d / (size * size));
    float edge = 0.5 - max(abs(f.x), abs(f.y));   // stays inside its cell (no clipped squares)
    float halo = exp(-d * d / (size * size * 9.0)) * 0.25 * smoothstep(0.0, 0.2, edge);
    vec3 c = mix(uGlow, uGlow2, step(0.62, h2));
    return c * (core * 1.4 + halo) * blink * bright;
}

void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    float top = uView.y + uView.w, bot = uView.y - uView.w;
    float t = clamp((p.y - bot) / (2.0 * uView.w), 0.0, 1.0); // 0 bottom .. 1 top
    float wm = smoothstep(0.2, 1.0, uLv), wp = smoothstep(1.1, 2.0, uLv);

    // The water column: black below, the faintest blue of the far surface above, a soft cone of it overhead.
    vec3 col = mix(uDeep, uWater, pow(t, 1.7) * 0.9);
    float cone = exp(-pow(p.x / (11.0 + 10.0 * t), 2.0)) * smoothstep(0.25, 1.0, t);
    col += uWater * cone * (0.25 + 0.75 * uRays);
    // Turbid water: slow drifting murk, the depth's texture.
    float murk = fbm(p * 0.075 + vec2(uTime * 0.012, -uTime * 0.005));
    col *= 0.7 + 0.6 * murk;
    // God rays far above (the calm), slanting and swaying, dying with depth.
    if (uRays > 0.001) {
        float x = p.x + (top - p.y) * 0.22;
        float r = pow(vnoise(vec2(x * 0.32 + uTime * 0.025, 0.5)), 3.0)
                + 0.7 * pow(vnoise(vec2(x * 0.85 - uTime * 0.04, 3.7)), 4.0);
        float fall = exp(-(top - p.y) * 0.13) * (0.6 + 0.4 * murk);
        col += uSurf * r * fall * uRays * 0.17;
    }
    // Plankton clouds (mid, peak): faint luminous drifts that flare with the kicks.
    if (uPlankton > 0.001) {
        float pn = fbm(p * 0.11 + vec2(-uTime * 0.018, uTime * 0.011) + 13.0);
        float cloud = smoothstep(0.5, 0.82, pn);
        vec2 g = p * 2.6 + vec2(uTime * 0.05, 0.0);
        vec2 id = floor(g);
        float hs = hash12(id + 31.0);
        vec2 o = vec2(hash12(id + 3.0), hash12(id + 5.0)) - 0.5;
        float dd = length(fract(g) - 0.5 - o * 0.7) / 2.6;
        float tw = 0.5 + 0.5 * sin(uTime * (1.0 + 3.0 * hs) + hs * 50.0);
        float spark = exp(-dd * dd / 0.0016) * step(0.6, hs) * tw;
        vec3 pc = mix(uGlow, uGlow2, smoothstep(0.55, 0.8, fbm3(p * 0.05 + 2.0)));
        col += pc * cloud * uPlankton * (0.012 + 0.16 * uBloom + 0.02 * uBeat);
        col += pc * spark * cloud * uPlankton * (0.15 + 1.6 * uBloom);
    }
    // Peak: the bioluminescence blooms. The water itself lights up in its accent color (a milky sea, in slow living
    // patches, brighter higher up), and a dense field of plankton lights up all through it. The kicks and beats
    // pulse its brightness, never its motion: everything still drifts as slowly as in the calm.
    if (uBio > 0.001) {
        float sea = fbm(p * vec2(0.055, 0.085) + vec2(uTime * 0.011, uTime * 0.005) + 71.0);
        float patchy = smoothstep(0.3, 0.8, sea);
        vec3 warm = mix(uGlow2, uGlow, smoothstep(0.35, 0.75, fbm3(p * 0.035 + 5.0 + uTime * 0.004)) * (1.0 - 0.6 * uDrive));
        // the same strength of light whatever the palette (pale glows would wash the deep out)
        float lum = max(dot(warm, vec3(0.2126, 0.7152, 0.0722)), 0.05);
        vec3 haze = warm * (0.3 / lum);
        warm *= min(1.0, 0.55 / lum);
        float pulse = 1.0 + (0.5 + 0.9 * uDrive) * uBloom + 0.45 * uBeat + (0.8 + 1.2 * uDrive) * uKick + 1.2 * uSurge;
        col += haze * uBio * (0.006 + 0.012 * uDrive + (0.05 + 0.03 * uDrive) * patchy * patchy) * pulse * (0.5 + 0.5 * t);
        float dens = (0.1 + 0.16 * uDrive + 0.25 * uSurge) * uBio;
        float sp = 0.0;
        for (int i = 0; i < 2; i++) {
            float sc = i == 0 ? 3.1 : 1.7;
            vec2 g = p * sc + vec2(uTime * 0.04, uTime * 0.025 * (i == 0 ? 1.0 : -0.6)) * sc + float(i) * 17.3;
            vec2 id = floor(g);
            float hs = hash12(id + 91.0 + float(i) * 7.0);
            vec2 o = vec2(hash12(id + 13.0), hash12(id + 29.0)) - 0.5;
            float dd = length(fract(g) - 0.5 - o * 0.6) / sc;
            float sz = i == 0 ? 0.035 : 0.05;
            float tw = 0.45 + 0.55 * pow(0.5 + 0.5 * sin(uTime * (0.7 + 1.6 * hs) + hs * 80.0), 3.0);
            sp += exp(-dd * dd / (sz * sz)) * step(hs, dens * (0.15 + 1.6 * patchy * patchy)) * tw * (i == 0 ? 0.7 : 1.0);
        }
        // the plankton gathers in the patches, and thins out behind the stack so the pieces keep the eye
        float behindWell = 1.0 - 0.8 * (1.0 - smoothstep(5.0, 6.5, abs(p.x))) * (1.0 - smoothstep(9.5, 11.5, p.y));
        col += warm * sp * (0.25 + 0.55 * patchy) * pulse * behindWell;
    }
    // Distant lights: far and dim, a nearer sparse layer.
    col += lightField(p, 0.95, 0.05 + 0.06 * uLights, 0.0, 0.5 + 0.3 * uLights);
    col += lightField(p, 0.42, 0.02 + 0.04 * uLights, 17.0, 0.7 + 0.5 * uLights);
    // The Tetris: the plankton blooms all through the water at once, in living patches, then dims.
    if (uTetris < 3.0) {
        float n = fbm3(p * vec2(0.12, 0.3) + vec2(0.0, -uTime * 0.4));
        float env = smoothstep(0.0, 0.15, uTetris) * (1.0 - smoothstep(0.3, 2.6, uTetris));
        col += uTetrisCol * env * 0.16 * (0.3 + 1.4 * n * n);
    }
    col += uGlow * uFlash * 0.05 * (0.5 + t);

    // The seabed: dark rock, its top edge caught by a little light.
    {
        float by = bedY(p.x, uSeed);
        float rough = (fbm3(vec2(p.x * 0.9, p.y * 0.9) + 40.0) - 0.5) * 0.35;
        float bed = by + rough;
        float inBed = 1.0 - smoothstep(bed - uPx, bed + uPx, p.y);
        float depthIn = max(bed - p.y, 0.0);
        vec3 rock = uDeep * 0.5 + uWater * 0.05 * (0.6 + 0.8 * fbm3(p * 1.4));
        rock += uSurf * 0.012 * exp(-depthIn * 5.0) * (0.3 + uRays);
        rock += uGlow * 0.025 * exp(-depthIn * 3.0) * (0.3 + 0.7 * uLights);
        col = mix(col, rock, inBed);
        // a haze of glow over the bed where the worms live
        col += uGlow * 0.012 * exp(-abs(p.y - bed) * 1.5) * (0.4 + uLights);
    }

    // The abyss goes dark (slow motion) and the lights go out (game over).
    col *= 1.0 - 0.94 * uSlow;
    col *= 1.0 - 0.55 * uOver;

    // The anglerfish (slow motion): its lure lights the water, its body only a silhouette in that halo.
    if (uLure.z > 0.001) {
        vec2 L = uLure.xy;
        float face = uLure.w;
        float d = length(p - L);
        vec3 halo = uLureCol * (exp(-d * 0.35) * 0.16 + exp(-d * 1.4) * 0.35 + exp(-d * d * 30.0) * 5.0);
        float flick = 0.85 + 0.15 * sin(uTime * 2.3) * sin(uTime * 3.7);
        vec2 q = p - L;
        q.x *= face;                               // the fish faces +x in q
        vec2 bc = vec2(-3.1, -1.5);                // body center behind and below the lure
        vec2 bq = q - bc;
        float body = length(bq / vec2(2.7, 2.1)) - 1.0;
        float tail = sdSeg(q, bc + vec2(-1.8, 0.1), bc + vec2(-4.4, 0.5)) - mix(0.9, 0.2, clamp((-(q.x - bc.x) - 1.8) / 2.6, 0.0, 1.0));
        float fish = min(body * 2.0, tail);
        // the open mouth: a dark gape at the front, teeth caught by the lure
        vec2 mq = q - (bc + vec2(1.9, -0.25));
        float mouth = length(mq / vec2(1.05, 0.8)) - 1.0;
        float ang = atan(mq.y, mq.x);
        float teethRow = abs(length(mq / vec2(1.05, 0.8)) - 1.0);
        float teeth = (1.0 - smoothstep(0.0, 0.12, teethRow)) * smoothstep(0.35, 0.0, abs(fract(ang * 3.2) - 0.5) - 0.12)
                      * step(-1.6, ang) * step(ang, 1.6) * (1.0 - step(0.0, -mq.x - 0.4));
        // the illicium: a thin rod arching from the head to the lure
        vec2 h0 = bc + vec2(1.2, 1.9), h1 = vec2(-0.9, 1.4);
        float rod = min(sdSeg(q, h0, h1), sdSeg(q, h1, vec2(0.0)));
        float lit = exp(-d * 0.55);
        float fishMask = 1.0 - smoothstep(-0.05, 0.05, fish);
        vec3 bodyCol = uDeep * 0.2 + uLureCol * (0.05 * lit + 0.35 * lit * lit * smoothstep(-0.35, 0.0, fish));
        vec3 c = col + halo * flick * uLure.z;
        c = mix(c, bodyCol, fishMask * uLure.z);
        c += vec3(1.0, 0.95, 0.85) * teeth * lit * 0.35 * uLure.z * (1.0 - step(0.0, mouth - 0.15));
        c = mix(c, uDeep * 0.1, (1.0 - smoothstep(0.02, 0.05, rod)) * uLure.z * 0.8);
        col = c;
    }

    // Quiet behind the HUD columns and the corner where the universe's name is written.
    float cl = sdBox(p - (uColL.xy + uColL.zw) * 0.5, (uColL.zw - uColL.xy) * 0.5);
    float cr = sdBox(p - (uColR.xy + uColR.zw) * 0.5, (uColR.zw - uColR.xy) * 0.5);
    float cm = 1.0 - smoothstep(-0.6, 1.6, min(cl, cr));
    col *= 1.0 - 0.45 * cm;
    vec2 corner = vec2(uView.x - uView.z, uView.y - uView.w);
    col *= 1.0 - 0.5 * exp(-length((p - corner) * vec2(0.09, 0.3)) * 2.2);

    // The well: a faint glass column, darker inside so the jelly reads; its walls catch a little light.
    vec2 wc = (uWell.xy + uWell.zw) * 0.5, wh = (uWell.zw - uWell.xy) * 0.5;
    float sw = sdBox(p - wc, wh);
    float inside = (1.0 - smoothstep(-uPx, uPx, sw)) * (1.0 - 0.75 * smoothstep(10.0, 13.0, p.y));
    vec3 glass = col * 0.3 + uDeep * 0.4;
    glass += uWater * 0.05 * (0.6 + 0.4 * sin((p.x - wc.x) * 0.9 + uTime * 0.2)) * smoothstep(bot, top, p.y);
    glass += uGlow * 0.006 * (1.0 - smoothstep(0.0, 1.4, -sw));
    col = mix(col, glass, inside);
    col += uGlow * 0.025 * exp(-max(sw, 0.0) * 2.5) * step(0.0, sw) * (0.5 + 0.5 * uLights) * (1.0 - uSlow);

    // The bridge (a change of universe): the water closes over everything.
    if (uBridge > 0.0) col = mix(col, uBridgeCol, smoothstep(0.0, 0.85, uBridge));
    frag = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- sprites: snow, jellies, sparks, threads

// a0: a.xy, b.xy (board). a1: rgb, alpha. a2: kind, size, p0, p1.
inline const char* SPRITE_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
layout(location = 2) in vec4 a1;
layout(location = 3) in vec4 a2;
uniform mat4 uVP;
out vec2 vP;      // board units
out vec2 vL;      // local (jellyfish: bell radii, dots: size units)
flat out vec4 v0, v1, v2;
void main() {
    int kind = int(a2.x + 0.5);
    float s = a2.y;
    vec2 p;
    vec2 l = aCorner;
    if (kind == 3) {
        vec2 a = a0.xy, b = a0.zw;
        vec2 d = b - a;
        float len = length(d);
        vec2 t = len > 1e-5 ? d / len : vec2(1.0, 0.0);
        vec2 n = vec2(-t.y, t.x);
        float ext = s * 0.5 + s * 6.0 + 0.04;
        p = mix(a - t * ext, b + t * ext, aCorner.x * 0.5 + 0.5) + n * aCorner.y * ext;
    } else if (kind == 1) {
        // the jellyfish: bell on top, tentacles below; tilted by b.x
        l = vec2(aCorner.x * 1.5, mix(-4.2, 1.5, aCorner.y * 0.5 + 0.5));
        float c = cos(a0.z), sn = sin(a0.z);
        vec2 r = vec2(l.x * c - l.y * sn, l.x * sn + l.y * c);
        p = a0.xy + r * s;
    } else {
        float ext = kind == 4 ? 6.0 : 4.0;
        l = aCorner * ext;
        p = a0.xy + l * s;
    }
    vP = p;
    vL = l;
    v0 = a0; v1 = a1; v2 = a2;
    gl_Position = uVP * vec4(p, 0.0, 1.0);
}
)";
inline const char* SPRITE_FS = R"(#version 330 core
in vec2 vP;
in vec2 vL;
flat in vec4 v0, v1, v2;
out vec4 frag;
uniform float uPx, uTime;
void main() {
    int kind = int(v2.x + 0.5);
    vec3 c = v1.rgb;
    float alpha = v1.a;
    if (kind == 0) {
        // a soft dot of light (marine snow, plankton): a gaussian core and a faint halo
        float d = length(vL);
        float v = exp(-d * d * 1.6) + exp(-d * 1.4) * 0.12;
        frag = vec4(c * v * alpha, 0.0);
        return;
    }
    if (kind == 4) {
        // a photophore: a crisp hot core in a wide soft halo
        float d = length(vL);
        float v = (1.0 - smoothstep(0.55, 1.0, d)) * 1.6 + exp(-d * 0.8) * 0.3;
        vec3 hot = mix(c, vec3(length(c) * 0.7), 0.4);
        frag = vec4((hot * (1.0 - smoothstep(0.0, 0.8, d)) * 1.2 + c * v) * alpha, 0.0);
        return;
    }
    if (kind == 3) {
        // a glowing thread from a to b: a thin core and a soft glow
        vec2 a = v0.xy, b = v0.zw;
        float d = sdSeg(vP, a, b);
        float w = v2.y * 0.5;
        float core = 1.0 - smoothstep(w, w + uPx, d);
        float glow = exp(-d / max(v2.y * 2.5 + 0.015, 1e-3));
        frag = vec4(c * (core * 0.9 + glow * 0.45) * alpha, 0.0);
        return;
    }
    if (kind == 1) {
        // A jellyfish, in bell radii. p0: contraction (0..1, the beat), p1: light. b.y: seed.
        vec2 q = vL;
        float k = v2.z, light = v2.w, seed = v0.w;
        float rx = 1.0 - 0.16 * k, ry = 0.78 + 0.12 * k;
        float px = uPx / max(v2.y, 1e-3);
        float e = length(vec2(q.x / rx, max(q.y, 0.0) / ry));
        float xn = clamp(q.x / rx, -1.0, 1.0);
        float rim = -0.16 * (1.0 - xn * xn) - 0.04 - 0.1 * k * (1.0 - xn * xn); // the margin curls under
        float inBell = (1.0 - smoothstep(1.0 - px * 2.0, 1.0, e)) * smoothstep(rim - px * 2.0, rim + px, q.y);
        vec3 col = vec3(0.0);
        // the bell: a thin translucent fill, a bright membrane, radial canals, four gonad rings
        float edge = exp(-(1.0 - e) * 9.0) * inBell;
        float ang = atan(q.x, q.y + 0.25);
        float canals = pow(abs(cos(ang * 4.0)), 26.0) * smoothstep(0.95, 0.2, e) * inBell;
        float gon = 0.0;
        for (int i = 0; i < 4; i++) {
            float a = 0.785 + 1.571 * float(i);
            vec2 gc = vec2(sin(a) * 0.36 * rx, 0.22 + cos(a) * 0.14);
            gon += exp(-dot((q - gc) * vec2(1.0, 1.6), (q - gc) * vec2(1.0, 1.6)) / 0.02) * 0.6;
        }
        gon *= inBell;
        vec3 hot = mix(c, vec3(1.0), 0.35);
        col += c * inBell * (0.10 + 0.22 * smoothstep(1.0, 0.0, e));
        col += hot * edge * 0.8;
        col += c * canals * 0.45;
        col += hot * gon * 0.35;
        // marginal lights along the rim, flashing on the beat
        float mx = q.x / rx;
        float onRim = exp(-pow((q.y - rim) / 0.05, 2.0)) * step(abs(mx), 1.0);
        float dots = pow(0.5 + 0.5 * cos(mx * 22.0 + seed), 10.0);
        col += hot * onRim * dots * (0.6 + 1.6 * k);
        // tentacles: thin, trailing and waving; oral arms: shorter frilled ribbons at the center
        float below = rim - q.y;
        if (below > -0.05) {
            float tent = 0.0;
            for (int i = 0; i < 7; i++) {
                float fi = float(i);
                float x0 = (fi / 6.0 * 2.0 - 1.0) * 0.85 * rx;
                float len = 2.2 + 1.8 * hash11(fi * 3.7 + seed);
                float w = sin(below * 2.6 - uTime * 1.7 + fi * 1.3 + seed) * 0.14 * below
                        + sin(below * 6.0 - uTime * 2.9 + fi) * 0.03 * below;
                float d = abs(q.x - x0 - w);
                float fade = (1.0 - smoothstep(len * 0.5, len, below)) * smoothstep(-0.05, 0.1, below);
                tent += exp(-d * d / (0.0009 + px * px)) * fade;
            }
            float arms = 0.0;
            for (int i = 0; i < 3; i++) {
                float fi = float(i);
                float x0 = (fi - 1.0) * 0.16;
                float w = sin(below * 3.5 - uTime * 1.3 + fi * 2.1 + seed) * 0.12 * below;
                float frill = 0.06 + 0.05 * sin(below * 14.0 + fi * 3.0 - uTime * 2.0);
                float d = abs(q.x - x0 - w);
                float fade = 1.0 - smoothstep(0.7, 1.5, below);
                arms += smoothstep(frill, frill * 0.3, d) * fade * smoothstep(-0.02, 0.15, below);
            }
            col += c * tent * 0.55 + c * arms * 0.28;
        }
        col *= light * (0.55 + 0.75 * k);
        // a soft glow around the whole bell
        float win = smoothstep(1.5, 1.0, abs(q.x)) * smoothstep(1.5, 1.0, q.y) * smoothstep(-1.6, -0.4, q.y);
        float halo = exp(-max(e - 0.6, 0.0) * 2.6) * 0.06 * light * (0.6 + k) * win;
        col += c * halo;
        frag = vec4(col * alpha, 0.0);
        return;
    }
    discard;
}
)";

// ---------------------------------------------------------------- the whale: a dark living silhouette with lights

inline const char* WHALE_VS = R"(#version 330 core
layout(location = 0) in vec4 aPosUv;   // board xy, u (0 head .. 1 tail; >= 2 fins), v (-1 belly .. 1 back)
uniform mat4 uVP;
out vec2 vUv;
out vec2 vP;
void main() {
    vUv = aPosUv.zw;
    vP = aPosUv.xy;
    gl_Position = uVP * vec4(aPosUv.xy, 0.0, 1.0);
}
)";
inline const char* WHALE_FS = R"(#version 330 core
in vec2 vUv;
in vec2 vP;
out vec4 frag;
uniform vec3 uBody, uSpot, uVeil;
uniform float uAlpha, uTime, uBeat, uLen, uHalf, uLights;
uniform vec4 uColL, uColR, uWell;
void main() {
    float u = vUv.x, v = vUv.y;
    bool fin = u >= 2.0;
    float fw = max(fwidth(v), 1e-4);
    float cov = 1.0 - smoothstep(1.0 - fw * 1.6, 1.0, abs(v));
    if (cov <= 0.0) discard;
    // the body: darker than the water, its back faintly lit from far above
    float back = smoothstep(-0.6, 1.0, v);
    vec3 col = uBody * (0.3 + 1.2 * back);
    col += uVeil * 1.4 * pow(smoothstep(0.55, 1.0, v), 2.0);      // a rim of the surface's light on the back
    col += uSpot * 0.035 * smoothstep(-0.2, -1.0, v);              // its own lights glowing faintly on the belly
    vec3 lights = vec3(0.0);
    if (!fin) {
        // throat pleats under the head
        float pleat = smoothstep(-0.15, -0.9, v) * (1.0 - smoothstep(0.25, 0.5, u));
        col += uVeil * 0.18 * pleat * pow(0.5 + 0.5 * sin(v * 60.0), 6.0);
        // bioluminescent spots: a lateral row and scattered ones, in board units along the body
        vec2 s = vec2(u * uLen, v * uHalf);
        float rx = (fract(s.x / 1.1) - 0.5) * 1.1;
        float ry = (v - 0.12) * uHalf;
        float rowDots = exp(-(rx * rx + ry * ry) / 0.008) * smoothstep(0.03, 0.12, u) * (1.0 - smoothstep(0.8, 0.95, u));
        vec2 g = s / 0.7;
        vec2 id = floor(g);
        float h = hash12(id + 11.0);
        vec2 o = vec2(hash12(id + 2.0), hash12(id + 4.0)) - 0.5;
        float dd = length(fract(g) - 0.5 - o * 0.6) * 0.7;
        float r = mix(0.05, 0.11, hash12(id + 6.0));
        float scat = exp(-dd * dd / (r * r)) * step(0.72, h) * (1.0 - smoothstep(0.85, 0.98, u)) * smoothstep(0.85, 0.3, abs(v));
        float tw = 0.6 + 0.4 * sin(uTime * (0.8 + 1.7 * h) + h * 30.0);
        // a slow wave of light travelling from the head to the tail on the beat
        float wave = exp(-pow(fract(u * 1.4 - uTime * 0.25) - 0.5, 2.0) / 0.01);
        lights = uSpot * (rowDots * (0.7 + 0.8 * uBeat + 0.8 * wave) + scat * tw * (0.7 + 0.6 * uBeat)) * 1.3;
        // the eye, a pale glint
        vec2 eq = vec2((u - 0.075) * uLen, (v + 0.05) * uHalf);
        lights += uSpot * exp(-dot(eq, eq) / 0.01) * 1.2;
    } else {
        // the long flipper's leading edge, beaded with light
        float e = smoothstep(0.55, 0.95, v) * (1.0 - smoothstep(2.92, 3.0, u));
        float bead = pow(0.5 + 0.5 * sin(u * 40.0), 8.0);
        lights = uSpot * e * bead * (0.6 + 0.6 * uBeat) * uLights;
    }
    col = mix(col, uVeil, 0.25);                               // the water between it and us
    // quieter behind the HUD columns
    float cl = sdBox(vP - (uColL.xy + uColL.zw) * 0.5, (uColL.zw - uColL.xy) * 0.5);
    float cr = sdBox(vP - (uColR.xy + uColR.zw) * 0.5, (uColR.zw - uColR.xy) * 0.5);
    float cm = 1.0 - smoothstep(-0.6, 1.6, min(cl, cr));
    // seen through the well's dark glass: fainter, so the stack stays readable
    float sw = sdBox(vP - (uWell.xy + uWell.zw) * 0.5, (uWell.zw - uWell.xy) * 0.5);
    cm = max(cm, 1.25 * (1.0 - smoothstep(-0.2, 0.2, sw)));
    float a = cov * uAlpha * (1.0 - 0.45 * cm);
    frag = vec4((col + lights * uLights * (1.0 - 0.4 * cm)) * a, a);
}
)";

// ---------------------------------------------------------------- tiles: jelly blocks, ghost, previews, gauge

// a0: center xy, half size xy (board). a1: rgb, alpha. a2: kind, age, type, neighbor mask. a3: scale, light, e2, e3.
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
uniform float uPx, uTime, uBeat, uSlow, uBridge, uWave;
uniform vec3 uBridgeInk, uGlow, uGlow2, uDeep;

// Each piece carries its own pattern inside the jelly, so pieces read apart by pattern as well as by hue.
float motif(int type, vec2 q) {
    if (type == 0) {                       // I: three filaments
        float f = fract(q.y * 3.0 + 0.5) - 0.5;
        return exp(-f * f / 0.006) * 0.55;
    }
    if (type == 1) {                       // O: a ring
        float d = length(q) - 0.2;
        return exp(-d * d / 0.0011);
    }
    if (type == 2) {                       // T: radial canals, like a medusa's
        float a = atan(q.y, q.x);
        float r = length(q);
        return pow(abs(cos(a * 2.0)), 24.0) * smoothstep(0.34, 0.06, r) * 0.9 + exp(-r * r / 0.004) * 0.6;
    }
    if (type == 3) {                       // S: four photophores
        vec2 g = abs(q) - vec2(0.19);
        return exp(-dot(g, g) / 0.0018) * 1.1;
    }
    if (type == 4) {                       // Z: a cellular net
        vec2 g = q * 3.2 + 7.0;
        vec2 id = floor(g), f = fract(g);
        float d1 = 9.0, d2 = 9.0;
        for (int j = -1; j <= 1; j++)
            for (int i = -1; i <= 1; i++) {
                vec2 o = vec2(float(i), float(j));
                vec2 pt = o + 0.5 + 0.35 * sin(vec2(hash12(id + o), hash12(id + o + 3.3)) * 6.283 + uTime * 0.4);
                float d = length(f - pt);
                if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
            }
        return exp(-pow(d2 - d1, 2.0) / 0.006) * 0.6;
    }
    if (type == 5) {                       // J: a bright nucleus
        float r = length(q);
        return exp(-r * r / 0.005) * 1.3 + exp(-pow(r - 0.13, 2.0) / 0.0008) * 0.35;
    }
    // L: chevron ridges
    float f = fract((abs(q.x) * 1.0 + q.y) * 2.6) - 0.5;
    return exp(-f * f / 0.008) * 0.5;
}

float caustic(vec2 p, float t) {
    float n1 = vnoise(p + vec2(t * 0.30, -t * 0.21));
    float n2 = vnoise(p * 1.3 - vec2(t * 0.22, t * 0.17) + 4.0);
    return 1.0 - smoothstep(0.0, 0.07, abs(n1 - n2));
}

void main() {
    int kind = int(v2.x + 0.5);
    vec3 pc = v1.rgb;
    float alpha = v1.a;
    float age = v2.y;
    vec2 p = vLocal;
    float px = uPx;
    if (kind == 0 || kind == 3) {
        // A translucent gelatinous block lit from within.
        float s = v3.x;
        float light = v3.y;
        float fl = age >= 0.0 ? exp(-age * 5.0) * 0.45 : 0.0;    // lock: the jelly quivers (no flare, no glow)
        float jig = age >= 0.0 ? sin(age * 17.0) * exp(-age * 5.0) : 0.0;  // the jelly settling
        float wob = 0.010 * s * (1.0 + 1.5 * fl + (kind == 3 ? 0.6 : 0.0));
        p += wob * vec2(sin(uTime * 2.3 + vWorld.y * 2.1 + vWorld.x), cos(uTime * 1.9 + vWorld.x * 2.4 - vWorld.y));
        p.y *= 1.0 + 0.07 * jig;
        p.x *= 1.0 - 0.05 * jig;
        int mask = int(v2.w + 0.5);
        float L = (mask & 1) != 0 ? 0.64 : 0.455, R = (mask & 2) != 0 ? 0.64 : 0.455;
        float U = (mask & 4) != 0 ? 0.64 : 0.455, D = (mask & 8) != 0 ? 0.64 : 0.455;
        vec2 lo = vec2(-L, -D) * s, hi = vec2(R, U) * s;
        float sd = sdRound(p - (lo + hi) * 0.5, (hi - lo) * 0.5, 0.17 * s);
        float cov = 1.0 - smoothstep(-px * 0.8, px * 0.8, sd);
        if (cov <= 0.0) discard;
        float din = max(-sd, 0.0) / s;
        vec2 q = vLocal / s;
        // the light: slow breathing in slow motion, the beat, the Tetris bloom
        light *= 1.0 + 0.14 * uBeat + uSlow * 0.4 * sin(uTime * 0.8 + vWorld.y * 0.25 + vWorld.x * 0.1);
        light += uWave * 0.6;
        float rim = exp(-din / 0.028);
        float rim2 = exp(-pow((din - 0.085) / 0.022, 2.0)) * 0.3;
        float core = exp(-dot(q, q) / 0.10);
        float vein = caustic(vWorld * 2.1 + v2.z * 3.0, uTime);
        float mot = motif(int(v2.z + 0.5), q);
        float spec = exp(-dot(q - vec2(-0.2, 0.24), q - vec2(-0.2, 0.24)) / 0.006) * step(0.5, 1.0 - float((mask & 4) != 0) * 0.0);
        vec3 hot = mix(pc, vec3(1.0), 0.35);
        vec3 col = pc * (0.06 + 0.5 * core) * light;
        col += pc * vein * 0.30 * light * (0.4 + 0.6 * core);
        col += hot * rim * (0.75 * light + 0.18);
        col += pc * rim2 * light;
        col += mix(pc, vec3(1.0), 0.2) * mot * 0.85 * light;
        col += vec3(0.85, 1.0, 1.0) * spec * 0.16;
        float a = clamp(0.6 + 0.4 * rim + 0.25 * core, 0.0, 1.0);
        // a dark membrane once its light has gone out (game over)
        col += uDeep * 0.0;
        // the bridge: a plain block in the bridge's ink
        float b = smoothstep(0.2, 0.85, uBridge);
        col = mix(col, uBridgeInk * (0.75 + 0.35 * rim), b);
        a = mix(a, 1.0, b);
        a *= cov * alpha;
        frag = vec4(col * cov * alpha, a);
        return;
    }
    if (kind == 1) {
        // The ghost: a membrane of plankton dots.
        float sd = sdRound(p, vec2(0.43), 0.16);
        float line = exp(-sd * sd / (0.0007 + px * px));
        float dots = 0.35 + 0.65 * pow(0.5 + 0.5 * sin((vWorld.x * 1.3 + vWorld.y) * 18.0 + uTime * 1.5), 3.0);
        float fill = (1.0 - smoothstep(-px, px, sd)) * 0.05;
        vec3 col = pc * (line * dots * 0.9 + fill);
        float a = (line * 0.3 + fill) * alpha * (1.0 - uBridge);
        frag = vec4(col * alpha * (1.0 - uBridge), a);
        return;
    }
    if (kind == 2) {
        // A cleared cell bursting: a flash, then the jelly breaks into points of light (v3.x: progress 0..1).
        float k = v3.x;
        float sd = sdRound(p, vec2(0.455), 0.17);
        float cov = 1.0 - smoothstep(-px, px, sd);
        float n = vnoise(vWorld * 7.0 + v2.z) * 0.8 + vnoise(vWorld * 17.0) * 0.2;
        float keep = smoothstep(k * 1.15 - 0.05, k * 1.15, n);
        float edge = exp(-pow((n - k * 1.15) / 0.05, 2.0));
        vec3 hot = mix(pc, vec3(1.0), 0.25);   // the jelly's own light, not a white flare spilling around
        vec3 col = hot * (0.9 * (1.0 - k) * keep + edge * 1.3 * (1.0 - k));
        float a = cov * keep * (1.0 - k) * alpha;
        frag = vec4(col * cov * alpha * (1.0 - uBridge), a * (1.0 - uBridge));
        return;
    }
    if (kind == 4) {
        // The bonus gauge: a glowing orb filling with living light (v3: level, ready, active, radius).
        float R = v3.w;
        vec2 q = p / R;
        float r = length(q);
        float aa = px / R * 1.5;
        float disc = 1.0 - smoothstep(1.0 - aa, 1.0, r);
        float lvl = v3.x;
        float fy = -1.0 + 2.0 * lvl + 0.05 * sin(q.x * 5.0 + uTime * 1.8) * step(0.01, lvl) * step(lvl, 0.99);
        float filled = (1.0 - smoothstep(fy - aa, fy + aa, q.y)) * disc;
        vec3 fc = mix(uGlow, uGlow2, v3.z);
        float pulse = v3.y * (0.6 + 0.4 * sin(uTime * 3.0));
        vec2 g = q * 6.0 + vec2(0.0, -uTime * 0.6);
        float sp = exp(-pow(length(fract(g) - 0.5), 2.0) / 0.01) * step(0.65, hash12(floor(g)));
        vec3 col = fc * (0.22 + 0.55 * smoothstep(fy - 0.9, fy, q.y) + 0.8 * sp) * filled * (1.0 + 1.2 * pulse);
        col += mix(fc, vec3(1.0), 0.4) * exp(-pow((q.y - fy) / 0.05, 2.0)) * disc * step(0.01, lvl) * 0.8;
        float ring = exp(-pow((r - 1.0) / (0.045 + aa), 2.0));
        col += mix(uGlow, vec3(1.0), 0.25) * ring * (0.55 + 1.2 * pulse);
        col += fc * exp(-max(r - 1.0, 0.0) * 4.0) * (0.06 + 0.4 * pulse) * (1.0 - disc);
        col += uDeep * 0.0;
        float a = max(disc * 0.85, ring);
        col = mix(col, uBridgeInk * max(disc * 0.6, ring), uBridge);
        frag = vec4(col * alpha, a * alpha);
        return;
    }
    discard;
}
)";

// ---------------------------------------------------------------- HUD panels (zenscene's menu)

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
uniform vec3 uColor, uGlow, uBridgeInk;
uniform float uAlpha, uGlowAmt, uWeight, uShadow, uBridge;
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.75;
    float fill = smoothstep(0.5 - uWeight - w, 0.5 - uWeight + w, d);
    float glow = smoothstep(0.05, 0.5, d);
    glow *= glow;
    float shadow = smoothstep(0.18, 0.5, d) * uShadow;
    vec3 fc = mix(uColor, uBridgeInk, uBridge);
    vec3 col = fc * fill + uGlow * glow * uGlowAmt * (1.0 - fill) * (1.0 - uBridge);
    float a = max(fill, shadow * 0.7);
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
        float k = clamp((m - uThreshold * 0.5) / uThreshold, 0.0, 1.0);   // a soft knee
        c *= k * k * max(m - uThreshold * 0.5, 0.0) / max(m, 1e-4);
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
uniform float uTime, uFade, uDim, uBridge, uBloomAmt, uSlow, uOver;
uniform vec3 uTint;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    c += texture(uBloom, vUv).rgb * uBloomAmt;
    vec3 raw = c;
    // tone: a soft shoulder per channel, hot cores roll toward white
    vec3 over = max(c - 1.0, 0.0);
    c += (over.r + over.g + over.b) * 0.12;
    const float K = 0.55;
    c = mix(c, K + (1.0 - K) * (1.0 - exp(-(c - K) / (1.0 - K))), step(K, c));
    // the deep closes in at the edges (darker still in slow motion and once the game is over)
    vec2 q = vUv - 0.5;
    q.x *= uRes.x / uRes.y * 0.75;
    float vig = 1.0 - (0.55 + 0.3 * uSlow + 0.2 * uOver) * smoothstep(0.15, 0.85, length(q));
    c *= mix(vig, 1.0, uBridge);
    c = mix(c, raw, uBridge);
    c *= 1.0 - uDim * 0.55;
    c *= uFade;
    c = pow(max(c, 0.0), vec3(1.0 / 2.2));
    // dither only (the dark gradients would band); no grain: the water stays clean
    float g = hash12(gl_FragCoord.xy + fract(uTime * 7.13) * 431.0) - 0.5;
    c += g * (1.5 / 255.0);
    frag = vec4(c, 1.0);
}
)";

} // namespace abyss_shaders
