#pragma once
// KINTSUGI's shaders (see kintsugi.cpp). Everything is drawn in linear HDR (molten gold goes above 1 and blooms),
// then tone-mapped in the post.
//
// MARBLE_FS bakes the stone once per song and size (its clouding, its soft figure, the crystal grain);
// STONE_FS lights it every frame (a soft raking light, the polished slab's broad reflection) and draws the well,
// a honed, darker panel with a hairline of metal. VEIN_FS draws the gold veins (line strips that grow along their
// arrival times, molten at their fronts); BLOCK_FS the ceramic shards; SHARD_FS the pieces of a cleared row;
// SPRITE_FS the gold leaf, the glints and the gauge.

namespace kintsugi_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { s += a * vnoise(p); p = p * 2.03 + vec2(17.1, 9.7); a *= 0.5; }
    return s;
}
// The raking light: a soft key from one side of the slab (uLight.xy its direction in the board plane, uLight.z its
// spread), and the broad reflection of a soft box across the polished stone (uSheen: angle, offset, width, gain).
uniform vec3 uLight;
uniform vec4 uSheen;
float rake(vec2 p) {
    float t = dot(p, normalize(uLight.xy)) / uLight.z;
    return 0.35 + 0.65 * smoothstep(-1.2, 1.0, t);
}
float sheen(vec2 p) {
    vec2 d = vec2(cos(uSheen.x), sin(uSheen.x));
    float u = dot(p, d) - uSheen.y;
    return exp(-u * u / (uSheen.z * uSheen.z)) * uSheen.w;
}
)";

// ---------------------------------------------------------------- the stone, baked once (song, size)

inline const char* MARBLE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uView;   // center xy, half extents (board units)
uniform float uSeed;
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    vec2 q = p + vec2(uSeed * 3.1, uSeed * 1.7);
    // clouding: large soft drifts in the stone
    float cloud = fbm(q * 0.07 + vec2(fbm(q * 0.05), fbm(q * 0.05 + 4.2)) * 1.6);
    // the crystal grain: a fine speckle that only shows in the reflection
    float grain = hash12(floor(p * 34.0 + uSeed));
    // a second, finer drift: the stone's figure, very soft (no veins: the gold is the only line in the stone)
    float figure = fbm(q * 0.22 + cloud * 2.0);
    frag = vec4(cloud, figure, grain, 1.0);
}
)";

// ---------------------------------------------------------------- the stone, lit every frame, and the well

inline const char* STONE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uMarble;
uniform vec4 uView;
uniform vec3 uStone, uCloud, uCalcite, uLightCol, uWarm, uRimCol;
uniform float uGain, uWarmAmt, uWellDark, uRim, uBridge;
uniform vec3 uBridgeCol;
uniform vec4 uWell;   // x0 y0 x1 y1 (board)
uniform float uPx;
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    vec4 m = texture(uMarble, vUv);
    float lit = rake(p) * uGain;
    vec3 base = mix(uStone, uCloud, 0.75 * smoothstep(0.3, 0.85, m.r));
    base = mix(base, uCalcite, 0.5 * smoothstep(0.45, 0.75, m.g) * smoothstep(0.3, 0.7, m.r));
    vec3 c = base * lit * uLightCol;
    // the polished slab mirrors the soft box: a broad, faint band, where the crystal grain sparkles a little
    float sh = sheen(p);
    c += uLightCol * sh * (0.016 + 0.006 * m.g + 0.008 * step(0.985, m.b)) * uGain;
    // the drop warms the stone (a steady grade, never a pulse)
    c = mix(c, c * uWarm * 1.6 + uWarm * 0.004, uWarmAmt);
    // the well: a honed, darker panel set into the slab, a hairline of metal around it
    vec2 wc = 0.5 * (uWell.xy + uWell.zw), wh = 0.5 * (uWell.zw - uWell.xy);
    vec2 d2 = abs(p - wc) - wh;
    float sd = length(max(d2, 0.0)) + min(max(d2.x, d2.y), 0.0);
    float inside = 1.0 - smoothstep(-uPx, uPx, sd);
    vec3 honed = base * uWellDark * (0.85 + 0.15 * rake(p)) * uLightCol * uGain;
    // a soft inner shadow under the slab's lip (top and the side away from the light)
    float lip = smoothstep(0.0, 1.4, -sd);
    honed *= mix(0.55, 1.0, lip);
    c = mix(c, honed, inside);
    float line = exp(-pow(sd / (uPx * 0.9), 2.0));
    c += uRimCol * line * uRim * (0.5 + 0.5 * rake(p));
    c = mix(c, uBridgeCol, uBridge);
    frag = vec4(c, 1.0);
}
)";

// ---------------------------------------------------------------- gold veins (line strips)

inline const char* VEIN_VS = R"(#version 330 core
layout(location = 0) in vec4 aA;  // x, y, across (-1..1), arrival (world seconds)
layout(location = 1) in vec4 aB;  // half width of the strip (board), core width (board), path distance, alpha
layout(location = 2) in vec4 aC;  // heat (molten 0..1), seed, born (world seconds), fade (0 shown .. 1 gone)
uniform mat4 uVP;
out vec4 vA;
out vec4 vB;
out vec4 vC;
out vec2 vP;
void main() {
    vA = aA; vB = aB; vC = aC; vP = aA.xy;
    gl_Position = uVP * vec4(aA.xy, 0.0, 1.0);
}
)";
inline const char* VEIN_FS = R"(#version 330 core
in vec4 vA;
in vec4 vB;
in vec4 vC;
in vec2 vP;
out vec4 frag;
uniform float uNow, uPx, uBridge;
uniform vec3 uMetal, uMolten, uHot;
uniform float uGain, uSurgeT, uSurgeAmp;
uniform vec4 uColL, uColR, uColTL, uColBL;  // the HUD columns and corners (board x0 y0 x1 y1): the veins fade behind them
float colMask(vec2 p, vec4 r) {
    vec2 c = 0.5 * (r.xy + r.zw), h = 0.5 * (r.zw - r.xy);
    vec2 d = abs(p - c) - h;
    float sd = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
    return smoothstep(-0.3, 0.3, sd);
}
void main() {
    float age = uNow - vA.w;
    if (age < 0.0) discard;
    float dist = abs(vA.z) * vB.x;               // board distance from the vein's center line
    float w = max(vB.y, uPx * 0.7);
    float core = 1.0 - smoothstep(w * 0.5 - uPx * 0.5, w * 0.5 + uPx * 0.6, dist);
    // the growing tip: a hot bead of metal
    float front = exp(-age * 2.6);
    float heat = clamp(vC.x, 0.0, 1.0);
    // a surge of light runs out along the veins every two beats (the drop)
    float sg = uNow - uSurgeT - vB.z / 26.0;
    float surge = uSurgeAmp * exp(-sg * sg / 0.012) * step(0.0, uSurgeT);
    float show = vB.w * (1.0 - vC.w) * (1.0 - uBridge);
    show *= mix(0.12, 1.0, colMask(vP, uColL) * colMask(vP, uColR) * colMask(vP, uColTL) * colMask(vP, uColBL));
    // the metal: a polished inlay catching the raking light (bright where the light rakes along it)
    float glint = 0.55 + 0.45 * sin(vB.z * 1.7 + vC.y * 6.0) * sin(vB.z * 0.53 + 1.3);
    float lit = rake(vP) * uGain;
    vec3 metal = uMetal * (0.35 + 0.9 * glint * glint) * lit + uMetal * sheen(vP) * 2.5;
    vec3 molten = mix(uMolten, uHot, front);
    float hot = max(heat, front);
    vec3 col = mix(metal, molten * (0.85 + 2.2 * front * front), hot) + uHot * surge * 1.2;
    // the halo: molten metal lights the stone around it
    float halo = exp(-dist * dist / (0.004 + 0.012 * heat)) * (0.16 * heat + 0.6 * front + 0.3 * surge);
    vec3 rgb = col * core + molten * halo * (1.0 - core);
    frag = vec4(rgb * show, core * show);
}
)";

// ---------------------------------------------------------------- the ceramic shards (blocks, ghost, previews)

// Shared by BLOCK_FS and SHARD_FS: the glazed ceramic and its gold seams, in piece space (continuous across the
// cells of one piece, different for each piece).
inline const char* CERAMIC_GLSL = R"(
uniform vec3 uMetal, uMolten, uHot, uLightCol;
uniform float uGain, uSeamHeat, uBridge;
uniform vec3 uBridgeInk;
// distance to the nearest seam (Voronoi edges) and the shard's id
vec2 seams(vec2 p) {
    vec2 n = floor(p), f = fract(p);
    float md = 8.0;
    vec2 mr = vec2(0.0), mg = vec2(0.0);
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            vec2 g = vec2(x, y);
            vec2 r = g + 0.15 + 0.7 * hash22(n + g) - f;
            float d = dot(r, r);
            if (d < md) { md = d; mr = r; mg = g; }
        }
    // exact distance to the border with the nearest other cell
    md = 8.0;
    for (int y = -2; y <= 2; y++)
        for (int x = -2; x <= 2; x++) {
            vec2 g = mg + vec2(x, y);
            vec2 r = g + 0.15 + 0.7 * hash22(n + g) - f;
            if (dot(mr - r, mr - r) > 1e-5) md = min(md, dot(0.5 * (mr + r), normalize(r - mr)));
        }
    return vec2(md, hash12(n + mg));
}
// p: piece space (cells), wp: board space (the light), edge: distance to the shape's outer edge (cells),
// seamW: seam width, heat: molten seams 0..1
vec4 ceramic(vec3 C, vec2 p, vec2 wp, float edge, float scale, float seamW, float heat, float px) {
    vec2 s = seams(p * 0.55);
    float tone = 0.9 + 0.16 * s.y;
    float lit = rake(wp) * uGain;
    vec3 col = C * tone * (0.55 + 0.6 * lit) * uLightCol;
    // a glazed body: deeper toward its edges, its lip catching the light
    col *= mix(0.78, 1.0, smoothstep(0.0, 0.22, edge));
    col += uLightCol * sheen(wp) * 0.10 * lit;
    // the gold seams that join the shards
    float sd = s.x / 0.55;
    float w = seamW * 0.5;
    float seam = 1.0 - smoothstep(w - px, w + px, sd);
    seam *= smoothstep(0.02, 0.08, edge); // not on the outer edge itself
    float glint = 0.6 + 0.4 * sin(p.x * 3.1 + p.y * 5.3 + s.y * 9.0);
    vec3 metal = uMetal * (0.4 + 0.6 * glint) * lit + uMetal * sheen(wp) * 1.5;
    vec3 molten = mix(uMolten * 1.6, uHot * 2.4, clamp(heat - 1.0, 0.0, 1.0));
    metal = mix(metal, molten, clamp(heat, 0.0, 1.0));
    col = mix(col, metal, seam);
    // molten seams warm the glaze beside them
    col += uMolten * 0.22 * clamp(heat, 0.0, 1.0) * exp(-sd * sd / 0.006) * (1.0 - seam);
    return vec4(col, seam);
}
)";

inline const char* BLOCK_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;           // -1..1
layout(location = 1) in vec4 aPos;               // center xy (board), half size, scale
layout(location = 2) in vec4 aCol;               // color, alpha
layout(location = 3) in vec4 aInfo;              // kind (0 block, 1 ghost, 2 cracking at game over), mask, seed, heat
layout(location = 4) in vec4 aCell;              // piece space center xy, lock age, extra
uniform mat4 uVP;
out vec2 vQ;
out vec2 vW;
flat out vec4 vCol;
flat out vec4 vInfo;
flat out vec4 vCell;
flat out float vScale;
void main() {
    float h = aPos.z * 1.06;
    vec2 q = aCorner * h;            // board units from the center
    vQ = q / aPos.w;                 // cell units
    vW = aPos.xy + q;
    vCol = aCol; vInfo = aInfo; vCell = aCell; vScale = aPos.w;
    gl_Position = uVP * vec4(vW, 0.0, 1.0);
}
)";
inline const char* BLOCK_FS = R"(#version 330 core
in vec2 vQ;
in vec2 vW;
flat in vec4 vCol;
flat in vec4 vInfo;
flat in vec4 vCell;
flat in float vScale;
out vec4 frag;
uniform float uPx;
void main() {
    int mask = int(vInfo.y + 0.5);
    const float G = 0.035;
    // the shape: the cell's box extended through the sides joined to the same piece (no junction inside a piece)
    vec2 lo = vec2((mask & 1) != 0 ? -1.0 : -0.5 + G, (mask & 4) != 0 ? -1.0 : -0.5 + G);
    vec2 hi = vec2((mask & 2) != 0 ? 1.0 : 0.5 - G, (mask & 8) != 0 ? 1.0 : 0.5 - G);
    vec2 c = 0.5 * (lo + hi), hb = 0.5 * (hi - lo);
    const float R = 0.07;
    vec2 d = abs(vQ - c) - hb + R;
    float sd = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - R;
    float px = uPx / vScale;
    float a = 1.0 - smoothstep(-px, px, sd);
    int kind = int(vInfo.x + 0.5);
    if (kind == 1) {
        // the ghost: a hairline of metal where the piece will land
        float line = 1.0 - smoothstep(0.0, px * 1.4 + 0.012, abs(sd + 0.02));
        vec3 m = mix(uMetal * (0.6 + 0.6 * rake(vW)) * uGain, uBridgeInk, uBridge);
        frag = vec4(m * line * vCol.a, line * vCol.a);
        return;
    }
    if (a <= 0.0) discard;
    float heat = vInfo.w;
    // a fresh lock: the seams near the contact glow and cool
    float age = vCell.z;
    if (age >= 0.0) heat = max(heat, 1.6 * exp(-age * 3.0) * smoothstep(0.6, -0.5, vQ.y + vCell.w));
    float seamW = 0.045;
    if (kind == 2) seamW = mix(0.045, 0.16, vCell.w); // game over: the cracks open wide with gold
    vec4 cer = ceramic(vCol.rgb, vCell.xy + vQ, vW, -sd, vScale, seamW, heat, px * 1.2);
    vec3 col = mix(cer.rgb, uBridgeInk * (0.85 + 0.15 * rake(vW)), smoothstep(0.3, 0.95, uBridge));
    frag = vec4(col * a * vCol.a, a * vCol.a);
}
)";

// ---------------------------------------------------------------- the pieces of a cleared row

inline const char* SHARD_VS = R"(#version 330 core
layout(location = 0) in vec4 aA;  // board xy (where it is now), piece space xy (where it was)
layout(location = 1) in vec4 aB;  // color, alpha
layout(location = 2) in vec4 aC;  // board xy (the light), seam edge distance, outline distance (barycentric)
layout(location = 3) in vec4 aD;  // kind (0 face, 1 gold wall, 2 glaze wall), light, lip (face) / across (wall)
uniform mat4 uVP;
out vec4 vA;
out vec4 vB;
out vec4 vC;
out vec4 vD;
void main() {
    vA = aA; vB = aB; vC = aC; vD = aD;
    gl_Position = uVP * vec4(aA.xy, 0.0, 1.0);
}
)";
// A shard of a cleared row, a slab tumbling in 3D: its face is the glazed ceramic (lit by its own tilt), its walls the
// broken edge (gold where it broke along a seam, the glaze's darker side along the piece's own edge).
inline const char* SHARD_FS = R"(#version 330 core
in vec4 vA;
in vec4 vB;
in vec4 vC;
in vec4 vD;
out vec4 frag;
uniform float uPx;
void main() {
    float kind = vD.x, lit = vD.y;
    vec3 col;
    if (kind < 0.5) {
        vec4 cer = ceramic(vB.rgb, vA.zw, vC.xy, 0.05 + 0.3 * vD.z, 1.0, 0.045, 0.0, uPx);
        // the broken edge: a thin line of gold where it cracked
        float e = vC.z;
        float edge = 1.0 - smoothstep(0.0, fwidth(e) * 1.5 + 0.008, e);
        col = mix(cer.rgb, uMetal * 1.4 * uGain, edge) * lit;
    } else if (kind < 1.5) {
        // the gold lacquer the shard broke along: metal, brighter where it turns to the light
        float g = 0.6 + 0.4 * sin(vA.z * 7.0 + vA.w * 5.0);
        col = uMetal * uGain * (0.25 + 1.1 * lit) * g + uMetal * 0.6 * pow(clamp(lit - 0.6, 0.0, 1.0), 2.0);
    } else {
        // the glaze running down the piece's own edge, a little deeper
        col = vB.rgb * uLightCol * (0.4 + 0.45 * lit) * mix(1.0, 0.75, vD.z);
    }
    col = mix(col, uBridgeInk, smoothstep(0.3, 0.95, uBridge));
    // the silhouette, smoothed over a pixel (vC.w: 0 on it)
    float a = vB.a * smoothstep(0.0, fwidth(vC.w) * 1.2 + 1e-4, vC.w);
    frag = vec4(col * a, a);
}
)";

// ---------------------------------------------------------------- sprites: gold leaf, glints, the gauge

inline const char* SPRITE_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aA;   // x, y, size, rotation
layout(location = 2) in vec4 aB;   // color (linear HDR), alpha
layout(location = 3) in vec4 aC;   // kind, e0, e1, e2
uniform mat4 uVP;
out vec2 vQ;
flat out vec4 vB;
flat out vec4 vC;
void main() {
    float c = cos(aA.w), s = sin(aA.w);
    vec2 q = aCorner;
    vQ = q;
    vec2 r = vec2(c * q.x - s * q.y, s * q.x + c * q.y) * aA.z;
    if (aC.x > 0.5 && aC.x < 1.5) r = vec2(c * q.x * aC.y - s * q.y, s * q.x * aC.y + c * q.y) * aA.z; // leaf: turned edge-on
    vB = aB; vC = aC;
    gl_Position = uVP * vec4(aA.xy + r, 0.0, 1.0);
}
)";
inline const char* SPRITE_FS = R"(#version 330 core
in vec2 vQ;
flat in vec4 vB;
flat in vec4 vC;
out vec4 frag;
uniform float uBridge;
uniform vec3 uMetal, uBridgeInk;
void main() {
    int kind = int(vC.x + 0.5);
    float r = length(vQ);
    if (kind == 0) {
        // a glint: a soft point of light
        float a = exp(-r * r * 9.0) + 0.6 * exp(-r * r * 60.0);
        frag = vec4(vB.rgb * a * vB.a * (1.0 - uBridge), 0.0);
    } else if (kind == 1) {
        // a flake of gold leaf: a ragged little square turning in the air (bright when it faces the light)
        float n = hash12(floor(vQ * 3.0 + vC.z * 13.0));
        float a = 1.0 - smoothstep(0.62, 0.8, max(abs(vQ.x), abs(vQ.y)) + 0.18 * n);
        frag = vec4(vB.rgb * a * vB.a * (1.0 - uBridge), a * vB.a * (1.0 - uBridge));
    } else {
        // the gauge: a thin ring of metal that fills like a mended rim (e0 fill, e1 ready, e2 active)
        float ang = atan(vQ.x, vQ.y) / 6.2831853 + 0.5; // 0 at the bottom, clockwise
        float ring = 1.0 - smoothstep(0.0, 0.035, abs(r - 0.82));
        float filled = step(ang, vC.y);
        float a = ring * mix(0.18, 1.0, filled);
        vec3 c = mix(vB.rgb * 0.5, vB.rgb * (1.2 + 1.2 * vC.z + 0.8 * vC.w), filled);
        c = mix(c, uBridgeInk, uBridge);
        frag = vec4(c * a * vB.a, a * vB.a);
    }
}
)";

// ---------------------------------------------------------------- flat rectangles (the viewer's menu)

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
uniform vec3 uColor;
uniform float uAlpha, uHaloAmt, uBias, uBridge;
uniform vec3 uBridgeInk;
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.75;
    float fill = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    // a soft pool of shadow behind the letters (they stay readable over a vein)
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
uniform float uThreshold;
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

// A photographic finish: a soft filmic shoulder, the bloom of the molten gold, a vignette, a whisper of grain.
inline const char* POST_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene, uBloom;
uniform float uTime, uFade, uDim, uBloomAmt, uBridge, uGrain;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    c += texture(uBloom, vUv).rgb * uBloomAmt;
    vec3 raw = c;
    vec2 q = vUv - 0.5;
    c *= 1.0 - 0.5 * dot(q * vec2(1.0, 1.2), q * vec2(1.0, 1.2)) * 1.6 * (1.0 - uBridge);
    const float K = 0.6;
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

} // namespace kintsugi_shaders
