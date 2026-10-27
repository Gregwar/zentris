#pragma once
// MARBLES' shaders (see marbles.cpp): the sky and the landscape, the tiles and their glass marbles (and the marbles
// lying in the landscape), the light of the marbles (halos, glints), the HUD letters and the final pass.
// Everything is drawn in linear light into a float target; the final pass encodes it.

namespace marbles_shaders {

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
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { s += a * vnoise(p); p = p * 2.03 + 17.1; a *= 0.5; }
    return s;
}
)";

// ---------------------------------------------------------------- the sky and the landscape
// A plain gradient sky (a moon or a sun on some variants, faint stars at night), one far ridge in the haze, and the
// landscape: a silhouette from the song's heightfield (uLand), its top edge caught by a hairline of light.
inline const char* SKY_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec4 uView;          // center x, y, half width, half height (board units)
uniform sampler2D uLand;     // the landscape's heights (R32F, one row)
uniform vec2 uLandX;         // x of its first and last sample
uniform vec3 uSkyTop, uSkyLow, uRidge, uLandC, uLandDeep, uRim, uOrb, uNight, uBridgeCol, uGlowCol;
uniform vec4 uOrbP;          // x, y, radius, on
uniform float uGlow, uSlow, uBridge, uSeed, uPx, uStars, uRimAmt;
float landAt(float x) { return texture(uLand, vec2((x - uLandX.x) / (uLandX.y - uLandX.x), 0.5)).r; }
float ridgeAt(float x) {
    float a = abs(x);
    return -7.4 + 3.2 * fbm(vec2(x * 0.06 + uSeed, uSeed * 0.3)) + 2.6 * smoothstep(5.0, 24.0, a) - 1.2 * smoothstep(9.0, 0.0, a);
}
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    float hy = clamp((p.y + 12.5) / 25.0, 0.0, 1.0);
    vec3 sky = mix(uSkyLow, uSkyTop, smoothstep(0.0, 1.0, pow(hy, 0.8)));
    // the night of slow motion, deeper at the top
    sky = mix(sky, uNight * (0.7 + 0.6 * (1.0 - hy)), uSlow * 0.85);
    // stars (night variants, and slow motion)
    float st = uStars + uSlow * 0.8;
    if (st > 0.01) {
        vec2 g = floor(p * 3.0);
        float h = hash12(g + 7.1);
        if (h > 0.985) {
            vec2 o = fract(p * 3.0) - 0.5 - (vec2(hash12(g + 1.3), hash12(g + 2.9)) - 0.5) * 0.6;
            float s = smoothstep(0.09, 0.0, length(o)) * (h - 0.985) / 0.015;
            sky += vec3(0.9, 0.92, 1.0) * s * 0.55 * st * smoothstep(-6.0, 4.0, p.y);
        }
    }
    // the moon or the sun: a soft disc and its halo
    if (uOrbP.w > 0.0) {
        float d = length(p - uOrbP.xy);
        float disc = smoothstep(uOrbP.z + uPx, uOrbP.z - uPx, d);
        sky += uOrb * (0.2 * exp(-max(d - uOrbP.z, 0.0) / (uOrbP.z * 2.2))) * uOrbP.w * (1.0 - 0.6 * uSlow);
        sky = mix(sky, uOrb, disc * uOrbP.w * (1.0 - 0.5 * uSlow));
    }
    float land = landAt(p.x);
    // the glow of a peak, rising from the land's line (a steady level, one swell on a drop)
    sky += uGlowCol * uGlow * exp(-max(p.y - land, 0.0) / 3.0) * smoothstep(5.0, 8.0, abs(p.x));
    vec3 col = sky;
    // the far ridge, half lost in the haze
    float rh = ridgeAt(p.x);
    float rA = smoothstep(rh + uPx, rh - uPx, p.y);
    col = mix(col, mix(sky, uRidge, 0.75), rA);
    // the landscape: a plain silhouette, deeper down, a hairline of light along its top
    float lA = smoothstep(land + uPx, land - uPx, p.y);
    vec3 lc = mix(uLandC, uLandDeep, smoothstep(0.0, 7.0, land - p.y));
    lc = mix(lc, uNight * 0.5, uSlow * 0.5);
    float rim = smoothstep(uPx * 2.6, uPx * 0.6, land - p.y) * lA;
    col = mix(col, lc, lA);
    col += uRim * rim * uRimAmt;
    col = mix(col, uBridgeCol, uBridge);
    frag = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- tiles and marbles
// One instance per square: a frosted tile holding a glass marble (kind 1), a marble alone in the landscape (0),
// the ghost's outline (2), a tile popping (3), a flat panel (4: the well's pane).
inline const char* MARBLE_VS = R"(#version 330 core
layout(location = 0) in vec2 aQ;
layout(location = 1) in vec4 a0;   // x, y, half size of the tile (kind 4: half width), marble radius (kind 4: half height)
layout(location = 2) in vec4 a1;   // linear rgb, alpha
layout(location = 3) in vec4 a2;   // rotation, glow, seed, kind
layout(location = 4) in vec4 a3;   // marble offset x, y (in the tile), flare, haze
layout(location = 5) in vec4 a4;   // the light it receives: linear rgb, the angle it travels (< -5: the key light)
uniform mat4 uVP;
out vec2 vP;
flat out vec4 v0, v1, v2, v3, v4;
void main() {
    int kind = int(a2.w + 0.5);
    vec2 ext;
    if (kind == 0) ext = vec2(a0.w * 1.08);
    else if (kind == 4) ext = vec2(a0.z, a0.w) + 0.05;
    else ext = vec2(a0.z + 0.12);
    vP = aQ * ext;
    v0 = a0; v1 = a1; v2 = a2; v3 = a3; v4 = a4;
    gl_Position = uVP * vec4(a0.xy + vP, 0.0, 1.0);
}
)";
inline const char* MARBLE_FS = R"(#version 330 core
in vec2 vP;
flat in vec4 v0, v1, v2, v3, v4;
out vec4 frag;
uniform vec4 uKey;             // the key light (the beams' source): the direction it travels xy, its strength, -
uniform vec3 uKeyCol;
uniform float uPx, uTileTint, uTileAlpha, uBridge, uLightGround, uSat, uCore;
uniform vec3 uTileCol, uLightCol, uHazeCol, uBridgeInk, uPanelEdge;
uniform vec4 uPanel;           // the well's pane: rgb, opacity (what a marble in a tile sees behind it)
uniform vec4 uView;            // board center x, y, half width, half height (to sample the background)
uniform sampler2D uBg;         // the sky and the land, as drawn this frame
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
vec3 saturateCol(vec3 c, float s) {
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return max(mix(vec3(l), c, s), vec3(0.0));
}
vec3 bgAt(vec2 w) { return texture(uBg, clamp((w - uView.xy) / uView.zw * 0.5 + 0.5, 0.001, 0.999)).rgb; }
// One layer of a marble's core at the inner point qi (style: 0 ribbon, 1 cat's eye, 2 swirl): coverage, and the
// shade of the face seen there.
vec2 coreLayer(vec2 qi, int style, float sd) {
    float core = 0.0, face = 1.0;
    float rr = length(qi);
    if (style == 0) {
        // a twisted ribbon through the middle: its width follows the twist, its two faces lit differently
        float ang = qi.x * 2.6 + sd * 6.283;
        float w = 0.36 * abs(cos(ang)) + 0.03;
        float dy = abs(qi.y - 0.08 * sin(qi.x * 2.3 + sd * 9.0));
        core = smoothstep(w, w * 0.6, dy) * smoothstep(0.95, 0.72, abs(qi.x));
        face = sin(ang) > 0.0 ? 1.0 : 0.55;
        face *= 0.75 + 0.4 * smoothstep(w, 0.0, dy);
    } else if (style == 1) {
        // a cat's eye: three curved vanes, thick in the middle, tapering out
        for (int k = 0; k < 3; k++) {
            float a = float(k) * 1.0472 + sd * 3.0;
            vec2 dir = vec2(cos(a), sin(a));
            float along = dot(qi, dir);
            float across = dot(qi, vec2(-dir.y, dir.x)) + 0.22 * along * along * (sd - 0.5) * 2.0;
            float w = 0.17 * (1.0 - 0.85 * abs(along));
            float v = smoothstep(w, w * 0.35, abs(across)) * smoothstep(0.9, 0.45, abs(along));
            if (v > core) { core = v; face = (across > 0.0 ? 1.0 : 0.6) * (0.8 + 0.35 * smoothstep(w, 0.0, abs(across))); }
        }
    } else {
        // a swirl of strands twisting out from the middle
        float a = atan(qi.y, qi.x);
        float s = sin(3.0 * a + rr * 5.5 + sd * 6.283);
        core = smoothstep(0.35, 0.8, s) * smoothstep(0.92, 0.3, rr);
        face = 0.6 + 0.55 * smoothstep(0.6, 1.0, s);
    }
    return vec2(core, face);
}
// A glass marble (q: -1..1 over its disc, center and R in board units). The glass is clear: through it the world
// behind is seen upside down and shrunk (the ball is a lens), slightly tinted, darker toward the rim where the light
// is bent away; inside, deeper than the surface and magnified by it, its colored core in two layers (the far one dim
// and shifted: depth): a twisted ribbon (style 0), a cat's eye of three curved vanes (1) or a swirl of strands (2).
// A sharp highlight of the window up left, a soft secondary reflection low right, the sky caught in the rim, the light
// focused through the glass on the far side, a few tiny bubbles. glow: the glass lit from within (the drop's waves).
vec3 glassMarble(vec2 q, vec2 center, float R, vec3 col, float rot, float seed, float glow, float flare, bool inTile, vec3 tileEnv,
                 vec3 Lc, vec2 ld) {
    float r2 = dot(q, q);
    float rl = sqrt(r2);
    float z = sqrt(max(1.0 - r2, 0.0));
    float fres = pow(1.0 - z, 2.5);
    vec3 c0 = saturateCol(col, uSat);
    // what is behind, refracted: inverted and shrunk through the lens (behind a tile: its back, lighter low where the
    // light from above comes out)
    vec3 env = inTile ? tileEnv * (1.0 - 0.35 * q.y) : bgAt(center - q * R * 2.4);
    vec3 tint = mix(vec3(1.0), c0, 0.4);
    vec3 m = env * tint * (0.35 + 0.6 * z);
    // the core, inside: magnified by the front of the glass; the far layer deeper (shifted toward the light, dimmer)
    float cr = cos(rot), sr = sin(rot);
    mat2 rm = mat2(cr, sr, -sr, cr);
    int style = int(floor(seed));
    float sd = fract(seed);
    vec2 qi = rm * (q * (1.12 + 0.25 * (1.0 - z)));
    vec2 far = coreLayer(rm * (q * 1.25 + vec2(0.1, -0.08)) * vec2(-1.0, 1.0), style, fract(sd + 0.37));
    vec2 near = coreLayer(qi, style, sd);
    float fade = smoothstep(1.0, 0.8, rl);   // the core never reaches the surface
    m = mix(m, c0 * 0.4 * uCore * (0.5 + 0.5 * z), far.x * fade * 0.75);
    vec3 coreCol = c0 * near.y * uCore * (0.5 + 0.55 * z);
    m = mix(m, coreCol, near.x * fade * 0.97);
    float core = max(near.x, far.x * 0.5) * fade;
    // the glass lit from within: the core glows first
    m += c0 * core * glow * 1.7 + c0 * glow * 0.3 * z + vec3(0.05) * glow * near.x * fade;
    // the rim: dark where the light is bent away, then the sky caught at the very edge (darker low: the ground)
    m *= 1.0 - mix(0.32, 0.2, uLightGround) * smoothstep(0.6, 1.0, rl);
    m = mix(m, uLightCol * mix(0.1, 0.45, smoothstep(-0.6, 0.8, q.y)), fres * 0.55);
    // The light it receives (traced through the heaps: Lc, travelling along ld). Glass is a ball lens: the light
    // crossing it is tinted by the glass and the core and focused into a bright spot inside, on the far side; the
    // side facing the light catches it along the rim (Fresnel); the window of the source is a sharp highlight on
    // the near side, where the surface faces it.
    float Ll = dot(Lc, vec3(0.3, 0.5, 0.2));
    vec2 qn = q / max(rl, 1e-3);
    float facing = max(0.0, -dot(qn, ld));
    m += c0 * Lc * (0.3 * z + 0.7 * core);
    vec2 cp = (q - ld * 0.56) * mat2(ld.x, ld.y, -ld.y, ld.x) * vec2(1.7, 1.0);
    float spot = exp(-dot(cp, cp) * 20.0);
    m += (c0 * 0.75 + 0.18) * Lc * spot * 1.6 + (c0 * 0.5 + 0.08) * spot * 0.35;
    m += mix(Lc, c0 * Ll, 0.3) * pow(fres, 0.7) * facing * 0.9;
    vec2 hp = -ld * vec2(0.42, 0.46);
    vec2 dh = (q - hp) * vec2(1.0, 1.4);
    float hs = 0.75 + 2.0 * min(Ll, 1.2);
    m += uLightCol * exp(-dot(dh, dh) * 110.0) * (hs + 3.0 * flare);
    m += uLightCol * exp(-dot(dh, dh) * 7.0) * (0.04 + 0.1 * min(Ll, 1.0) + 0.25 * flare);

    // tiny bubbles trapped in the glass
    for (int k = 0; k < 3; k++) {
        float hk = fract(sd * (13.7 + float(k) * 7.31));
        if (hk < 0.35) continue;
        vec2 bp = vec2(fract(sd * 31.1 + float(k) * 0.37), fract(sd * 17.3 + float(k) * 0.61)) * 1.1 - 0.55;
        float br = 0.03 + 0.03 * fract(hk * 5.3);
        float d = length(q - bp);
        float ring = smoothstep(br, br * 0.7, d) * smoothstep(br * 0.35, br * 0.75, d);
        m += uLightCol * ring * 0.25;
        m += uLightCol * smoothstep(br * 0.4, 0.0, length(q - bp + vec2(br * 0.35, -br * 0.35))) * 0.35;
    }
    return m;
}
void main() {
    int kind = int(v2.w + 0.5);
    vec3 col = v1.rgb;
    float alpha = v1.a;
    vec2 p = vP;
    float px = uPx;
    if (kind == 4) {
        // the well's pane: one even tone, a hairline at its edge
        float d = sdRoundBox(p, v0.zw, 0.06);
        float a = smoothstep(px, -px, d);
        float edge = smoothstep(px * 1.6, 0.0, abs(d + px * 0.8));
        vec3 c = mix(col, uPanelEdge, edge * 0.8);
        frag = vec4(c, a * mix(alpha, 1.0, edge * 0.5));
        return;
    }
    float h = v0.z, R = v0.w;
    vec4 outc = vec4(0.0);
    vec2 mo = v3.xy;
    float bb = uBridge;
    vec3 c0 = saturateCol(col, uSat);
    vec3 tileEnv = vec3(0.0);
    vec3 Lc = v4.rgb;
    vec2 ld = vec2(cos(v4.w), sin(v4.w));
    if (v4.w < -5.0) { Lc = uKeyCol * uKey.z; ld = uKey.xy; }
    if (kind >= 1) {
        float d = sdRoundBox(p, vec2(h), 0.075 * h / 0.46);
        float a = smoothstep(px, -px, d);
        if (kind == 2) {
            // the ghost: the tile's outline only
            float line = smoothstep(px * 1.5, px * 0.2, abs(d + px * 1.2));
            frag = vec4(mix(c0, uBridgeInk, bb), line * alpha);
            return;
        }
        // a frosted tile tinted by its marble
        vec3 base = mix(uTileCol, c0, uTileTint);
        base *= 1.0 + 0.12 * (p.y / h);
        float ta = uTileAlpha * 0.3;
        // what the marble sees behind it: the tile over the pane over the world
        vec3 behind = mix(bgAt(v0.xy), uPanel.rgb, uPanel.a);
        tileEnv = mix(behind, base, clamp(ta * 2.0, 0.0, 1.0)) * 0.95 + 0.015;
        // the marble's shadow on the tile, and the light it focuses there
        vec2 sp = (p - mo - ld * 0.11) / R;
        float sh = exp(-dot(sp, sp) * 1.6);
        base *= 1.0 - 0.5 * sh * (1.0 - uLightGround * 0.3);
        ta = mix(ta, max(ta, 0.3), sh * 0.5);
        vec2 cq = (p - mo - ld * 0.35) / R;
        float caus = exp(-dot(cq, cq) * 6.0);
        base += (c0 * 0.8 + 0.1) * caus * (0.35 + 0.9 * dot(Lc, vec3(0.3, 0.5, 0.2)));
        ta = max(ta, caus * 0.3);
        // a hairline lit along the top left of the tile's edge
        float rim = smoothstep(px * 1.8, px * 0.2, abs(d + px * 1.1));
        float lit = clamp(dot(normalize(p + 1e-4), vec2(-0.7071, 0.7071)) * 0.5 + 0.5, 0.0, 1.0);
        base = mix(base, mix(mix(c0, vec3(1.0), 0.6), c0 * 0.75, uLightGround), rim * (0.25 + 0.5 * mix(lit, 1.0 - lit * 0.5, uLightGround)));
        ta = max(ta, rim * (0.25 + 0.3 * lit));
        // the bridge: plain squares of ink
        base = mix(base, uBridgeInk, bb);
        ta = mix(ta, 0.95, bb);
        outc = vec4(base, a * ta);
        if (kind == 3) { frag = vec4(outc.rgb, outc.a * alpha); return; }
    }
    // the marble
    vec2 q = (p - mo) / R;
    float rl = length(q);
    float ma = smoothstep(1.0, 1.0 - px / R * 1.6, rl) * (1.0 - bb);
    if (ma > 0.0) {
        vec3 m = glassMarble(q, v0.xy + mo, R, col, v2.x, v2.z, v2.y, v3.z, kind >= 1, tileEnv, Lc, ld);
        m = mix(m, uHazeCol, v3.w);
        float a = ma + outc.a * (1.0 - ma);
        vec3 c = (m * ma + outc.rgb * outc.a * (1.0 - ma)) / max(a, 1e-4);
        outc = vec4(c, a);
    }
    frag = vec4(outc.rgb, outc.a * alpha);
}
)";

// ---------------------------------------------------------------- the traced light (instanced, additive, half res)
//   a0: a xy, b xy        a1: w0, w1, i0, i1        a2: color rgb, kind
// kind 0: a shaft from a to b (a soft band, its width w0 -> w1, its strength i0 -> i1 along it)
// kind 1: a pool of light where rays reach the land (an ellipse at a, radii b)
inline const char* LIGHT_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aA0;
layout(location = 2) in vec4 aA1;
layout(location = 3) in vec4 aA2;
uniform mat4 uVP;
out vec2 vP;
flat out vec4 vA0, vA1, vA2;
void main() {
    vA0 = aA0; vA1 = aA1; vA2 = aA2;
    vec2 a = aA0.xy, b = aA0.zw, w;
    if (aA2.w < 0.5) {
        vec2 d = b - a;
        float L = length(d);
        vec2 u = L > 1e-5 ? d / L : vec2(1.0, 0.0), v = vec2(-u.y, u.x);
        float e = max(aA1.x, aA1.y) * 2.0;
        w = (a + b) * 0.5 + u * aCorner.x * (L * 0.5 + e) + v * aCorner.y * e;
    } else {
        w = a + aCorner * b * 1.8;
    }
    vP = w;
    gl_Position = uVP * vec4(w, 0.0, 1.0);
}
)";
inline const char* LIGHT_FS = R"(#version 330 core
in vec2 vP;
flat in vec4 vA0, vA1, vA2;
out vec4 frag;
uniform float uPx;
void main() {
    vec2 a = vA0.xy, b = vA0.zw;
    float g;
    if (vA2.w < 0.5) {
        vec2 ab = b - a;
        float t = clamp(dot(vP - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
        float w = max(mix(vA1.x, vA1.y, t), uPx * 0.7);
        float d = length(vP - (a + ab * t));
        g = exp(-d * d / (w * w) * 2.0) * mix(vA1.z, vA1.w, t);
    } else {
        vec2 q = (vP - a) / b;
        g = exp(-dot(q, q) * 2.2) * vA1.z;
    }
    frag = vec4(vA2.rgb * g, 0.0);
}
)";
// The light buffer over the sky and the land (additive): the shafts broken up by a slow haze, a little of the
// light scattered around (its blurred mipmaps), quieter behind the HUD's columns.
inline const char* LIGHTCOMP_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uLight;
uniform vec4 uView;
uniform float uTime, uHaze, uSpill, uGain;
uniform vec4 uColL, uColR;
float h12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vn(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(h12(i), h12(i + vec2(1, 0)), f.x), mix(h12(i + vec2(0, 1)), h12(i + vec2(1, 1)), f.x), f.y);
}
vec3 light(vec2 uv, float lod) { return textureLod(uLight, clamp(uv, vec2(0.001), vec2(0.999)), lod).rgb; }
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    float h = 0.5 * vn(p * vec2(0.16, 0.22) + vec2(uTime * 0.045, -uTime * 0.018)) + 0.5 * vn(p * 0.5 + vec2(-uTime * 0.06, uTime * 0.03));
    float haze = mix(1.0, 0.35 + 1.3 * h, uHaze);
    float inL = smoothstep(uColL.x - 1.0, uColL.x + 1.0, p.x) * (1.0 - smoothstep(uColL.z - 1.0, uColL.z + 1.0, p.x))
              * smoothstep(uColL.y - 1.0, uColL.y + 1.0, p.y) * (1.0 - smoothstep(uColL.w - 1.0, uColL.w + 1.0, p.y));
    float inR = smoothstep(uColR.x - 1.0, uColR.x + 1.0, p.x) * (1.0 - smoothstep(uColR.z - 1.0, uColR.z + 1.0, p.x))
              * smoothstep(uColR.y - 1.0, uColR.y + 1.0, p.y) * (1.0 - smoothstep(uColR.w - 1.0, uColR.w + 1.0, p.y));
    float hud = 1.0 - 0.6 * max(inL, inR);
    vec3 L = light(vUv, 0.0);
    vec3 spill = light(vUv, 4.0) + 0.6 * light(vUv, 5.5);
    vec3 c = (L * haze + spill * uSpill) * hud * uGain;
    // a soft shoulder: the light never burns out
    c = c / (1.0 + max(max(c.r, c.g), c.b) * 0.8);
    frag = vec4(c, 0.0);
}
)";

// ---------------------------------------------------------------- light: halos and glints (added)
inline const char* FX_VS = R"(#version 330 core
layout(location = 0) in vec2 aQ;
layout(location = 1) in vec4 a0;   // x, y, size, rotation
layout(location = 2) in vec4 a1;   // linear rgb, alpha
layout(location = 3) in vec4 a2;   // kind (0 halo, 1 glint, 2 soft dot), -, -, -
uniform mat4 uVP;
out vec2 vQ;
flat out vec4 v1, v2;
void main() {
    float c = cos(a0.w), s = sin(a0.w);
    vec2 l = aQ * a0.z;
    vQ = aQ;
    v1 = a1; v2 = a2;
    gl_Position = uVP * vec4(a0.xy + vec2(c * l.x - s * l.y, s * l.x + c * l.y), 0.0, 1.0);
}
)";
inline const char* FX_FS = R"(#version 330 core
in vec2 vQ;
flat in vec4 v1, v2;
out vec4 frag;
void main() {
    int kind = int(v2.x + 0.5);
    float r = length(vQ);
    float a;
    if (kind == 0) a = exp(-r * r * 5.0) * smoothstep(1.0, 0.6, r);
    else if (kind == 1) {
        // a four-pointed glint: two thin rays and a hot point
        float rays = exp(-abs(vQ.y) * 40.0) * pow(max(1.0 - abs(vQ.x), 0.0), 2.0) + exp(-abs(vQ.x) * 40.0) * pow(max(1.0 - abs(vQ.y), 0.0), 2.0);
        a = rays * 0.8 + exp(-r * r * 60.0);
    } else a = smoothstep(1.0, 0.0, r);
    frag = vec4(v1.rgb * a * v1.a, 1.0);
}
)";

// ---------------------------------------------------------------- text
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
    frag = vec4(uInk, a * uAlpha);
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
void main() { frag = vCol; }
)";

// ---------------------------------------------------------------- the final pass
inline const char* POST_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene;
uniform float uFade, uDim;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    // a soft roll-off of the brightest light (the glowing marbles), a light vignette
    float mx = max(max(c.r, c.g), c.b);
    if (mx > 0.85) c *= (0.85 + 0.4 * (1.0 - exp(-(mx - 0.85) * 2.5))) / mx;
    vec2 q = vUv - 0.5;
    c *= 1.0 - 0.16 * dot(q, q) * 2.0;
    c *= 1.0 - uDim * 0.5;
    c *= uFade;
    c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
    // a whisper of dither against banding in the smooth sky
    c += (hash12(gl_FragCoord.xy) - 0.5) / 255.0;
    frag = vec4(c, 1.0);
}
)";

} // namespace marbles_shaders
