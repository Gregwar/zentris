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
uniform mat4 uVP;
out vec2 vP;
flat out vec4 v0, v1, v2, v3;
void main() {
    int kind = int(a2.w + 0.5);
    vec2 ext;
    if (kind == 0) ext = vec2(a0.w * 1.08);
    else if (kind == 4) ext = vec2(a0.z, a0.w) + 0.05;
    else ext = vec2(a0.z + 0.12);
    vP = aQ * ext;
    v0 = a0; v1 = a1; v2 = a2; v3 = a3;
    gl_Position = uVP * vec4(a0.xy + vP, 0.0, 1.0);
}
)";
inline const char* MARBLE_FS = R"(#version 330 core
in vec2 vP;
flat in vec4 v0, v1, v2, v3;
out vec4 frag;
uniform float uPx, uTileTint, uTileAlpha, uBridge, uLightGround;
uniform vec3 uTileCol, uLightCol, uHazeCol, uBridgeInk, uPanelEdge;
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
// A glass marble seen through its own surface (q: -1..1 over the marble): a tinted body, clear in the middle and
// deep at the rim, a cat's eye of three vanes or a swirled ribbon inside, the light focused through it into a
// crescent on the far side, a window's reflection, and its own light when the waves pass (glow).
vec3 marble(vec2 q, vec3 col, float rot, float seed, float glow, float flare) {
    float r2 = dot(q, q);
    float z = sqrt(max(1.0 - r2, 0.0));
    float fres = pow(1.0 - z, 2.2);
    // the inside, magnified a little by the glass, turning with the marble
    vec2 qi = q * (0.78 + 0.22 * z);
    float c = cos(rot), s = sin(rot);
    qi = vec2(c * qi.x - s * qi.y, s * qi.x + c * qi.y);
    float core = 0.0;
    bool eye = fract(seed * 7.13) < 0.55;
    if (eye) {
        // a cat's eye: three thin curved vanes through the center
        for (int k = 0; k < 3; k++) {
            float a = float(k) * 1.0472 + seed * 3.0;
            vec2 dir = vec2(cos(a), sin(a));
            float along = dot(qi, dir);
            float across = dot(qi, vec2(-dir.y, dir.x)) + 0.12 * along * along * (fract(seed * 3.7) - 0.5) * 4.0;
            float w = 0.13 * (1.0 - 0.75 * abs(along));
            core = max(core, smoothstep(w, w * 0.25, abs(across)) * smoothstep(0.82, 0.25, abs(along)));
        }
    } else {
        // a swirl: two ribbons twisting out from the center
        float a = atan(qi.y, qi.x);
        float rr = length(qi);
        float sw = sin(2.0 * a + rr * 5.5 + seed * 6.283);
        core = smoothstep(0.45, 0.92, sw) * smoothstep(0.86, 0.35, rr);
    }
    vec3 coreCol = eye ? mix(col * 1.25, vec3(1.0), 0.45) : mix(col, vec3(1.0), 0.72);
    // the glass body: clear in the middle, deep and saturated toward the rim
    vec3 body = col * (0.30 + 0.55 * z);
    body = mix(body, col * col * 0.55, fres * 0.6);
    vec3 m = mix(body, coreCol * (0.55 + 0.45 * z), core * 0.9);
    // the light focused through the glass, a bright crescent opposite the light
    vec2 cp = q - vec2(0.36, -0.44);
    float ca = exp(-dot(cp, cp) * 6.5) * (1.0 - fres * 0.7);
    m += (col * 0.9 + 0.08) * ca;
    // the sky caught in the rim, the window's sharp highlight up left and its soft bloom
    m += uLightCol * fres * 0.28 * smoothstep(-0.4, 0.9, q.y);
    vec2 dh = (q - vec2(-0.36, 0.42)) * vec2(1.0, 1.35);
    float spec = exp(-dot(dh, dh) * 60.0);
    m += uLightCol * (spec * (1.1 + 3.0 * flare) + exp(-dot(dh, dh) * 7.0) * (0.12 + 0.3 * flare));
    // a little shade where it rests
    m *= 1.0 - 0.3 * smoothstep(0.1, 1.0, -q.y) * fres;
    // its own light: the glass glowing from within, the core first
    m += (col * 1.3 + 0.06) * glow * (0.25 + 0.75 * z) + coreCol * core * glow * 0.9;
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
    if (kind >= 1) {
        float d = sdRoundBox(p, vec2(h), 0.075 * h / 0.46);
        float a = smoothstep(px, -px, d);
        if (kind == 2) {
            // the ghost: the tile's outline only
            float line = smoothstep(px * 1.5, px * 0.2, abs(d + px * 1.2));
            frag = vec4(mix(col, uBridgeInk, bb), line * alpha);
            return;
        }
        // a frosted tile tinted by its marble
        vec3 base = mix(uTileCol, col, uTileTint);
        base *= 1.0 + 0.12 * (p.y / h);
        float ta = uTileAlpha;
        // the marble's shadow on the tile, and the light it focuses there
        vec2 sp = (p - mo - vec2(0.06, -0.09)) / R;
        float sh = exp(-dot(sp, sp) * 1.6);
        base *= 1.0 - 0.5 * sh * (1.0 - uLightGround * 0.3);
        ta = mix(ta, max(ta, 0.55), sh * 0.6);
        vec2 cq = (p - mo - vec2(0.2, -0.24)) / R;
        float caus = exp(-dot(cq, cq) * 5.0);
        base += col * caus * 0.55;
        ta = max(ta, caus * 0.45);
        // a hairline lit along the top left of the tile's edge
        float rim = smoothstep(px * 1.8, px * 0.2, abs(d + px * 1.1));
        float lit = clamp(dot(normalize(p + 1e-4), vec2(-0.7071, 0.7071)) * 0.5 + 0.5, 0.0, 1.0);
        base = mix(base, mix(mix(col, vec3(1.0), 0.6), col * 0.75, uLightGround), rim * (0.25 + 0.5 * mix(lit, 1.0 - lit * 0.5, uLightGround)));
        ta = max(ta, rim * (0.35 + 0.4 * lit));
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
        vec3 m = marble(q, col, v2.x, v2.z, v2.y, v3.z);
        m = mix(m, uHazeCol, v3.w);
        float a = ma + outc.a * (1.0 - ma);
        vec3 c = (m * ma + outc.rgb * outc.a * (1.0 - ma)) / max(a, 1e-4);
        outc = vec4(c, a);
    }
    frag = vec4(outc.rgb, outc.a * alpha);
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
