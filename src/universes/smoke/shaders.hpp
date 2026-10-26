#pragma once
// SMOKE's shaders (see smoke.cpp). Everything is drawn in linear HDR (the beam's core and the rims go above 1 and
// bloom), then tone-mapped in the post.
//
// SMOKE_FS ray-marches the smoke at a reduced resolution: a slab of curling smoke behind the well (a tileable 3D
// noise, domain-warped and swirled), lit by one hard spotlight (a cone with a crisp edge), with self-shadowing toward
// the lamp (the shafts inside the beam) and the well's own shadow. COMPOSITE_FS brings it to full resolution over
// the dark stage and draws the well, a slab of smoked black glass with a hairline catching the light.

namespace smoke_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }
float sdRound(vec2 p, vec2 b, float r) { vec2 d = abs(p) - b + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }
// The spotlight: a cone from uLPos along uLAxis, tan(half angle) uCone; a crisp edge with a slightly hot ring
// (a Fresnel lens), a gentle falloff with distance.
uniform vec3 uLPos, uLAxis;
uniform float uCone;
float spot(vec3 p, float soft) {
    vec3 v = p - uLPos;
    float a = dot(v, uLAxis);
    if (a <= 0.5) return 0.0;
    float r = length(v - uLAxis * a) / (a * uCone);
    float edge = 1.0 - smoothstep(1.0 - 0.07 - soft, 1.0 + soft, r);
    float prof = mix(1.0, 0.7, r * r) + 0.22 * exp(-pow((r - 0.9) / 0.07, 2.0));
    return edge * prof / (1.0 + a * a * 0.0004);
}
)";

// ---------------------------------------------------------------- the smoke (reduced resolution)

inline const char* SMOKE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler3D uVol;       // tileable: r fbm, g curls (ridged), b a warp field
uniform vec4 uView;           // center xy, half extents (board units)
uniform vec3 uLCol, uFill;
uniform float uGain;
uniform float uHaze, uSmoke, uWarp, uCov, uExt, uShadowK;
uniform vec3 uFlow;
uniform float uSwirl;
uniform vec4 uWell;           // the well's flag (board x0 y0 x1 y1, at z = 0)
uniform float uBlast, uBlastA;
uniform float uThick;
uniform float uSeed;

float dens(vec3 p) {
    vec3 q = p * vec3(0.022, 0.017, 0.022) + uFlow;
    vec3 w = textureLod(uVol, q * 0.5 + vec3(0.31, 0.17, 0.63) + uFlow.zxy * 0.3, 0.0).gba - 0.5;
    float c = cos(uSwirl + p.y * 0.05), s = sin(uSwirl + p.y * 0.05);
    w.xz = mat2(c, s, -s, c) * w.xz;
    q += w * uWarp;
    vec4 n = textureLod(uVol, q, 0.0);
    vec4 m = textureLod(uVol, q * 2.7 + vec3(0.41, 0.07, 0.23), 0.0);
    // billows (the fbm over a threshold), carved by thin curling sheets (the ridges of a finer noise)
    float bill = smoothstep(uCov, uCov + 0.22, n.r + (m.r - 0.5) * 0.25);
    float sheet = 1.0 - abs(m.g * 2.0 - 1.0);
    sheet = sheet * sheet;
    sheet = sheet * sheet;
    float d = bill * (0.1 + 2.2 * sheet);
    d *= mix(1.25, 0.4, smoothstep(-14.0, 13.0, p.y)); // the smoke pools low and thins as it rises
    if (uBlastA > 0.001) {
        float r = length(p.xy * vec2(0.8, 0.6));
        d *= 1.0 - uBlastA * (1.0 - smoothstep(uBlast - 4.0, uBlast, r));
        d += uBlastA * 1.6 * exp(-pow((r - uBlast) / 1.6, 2.0)) * n.r;
    }
    return uHaze + d * uSmoke + uThick * (0.12 + 0.3 * n.r);
}

// Where an orthographic ray (from o, toward -z, length len) is inside the beam's cone: [t0, t1] (empty if t0 >= t1).
vec2 coneSpan(vec3 o, float len, float tanA) {
    vec3 V = vec3(0.0, 0.0, -1.0);
    float cos2 = 1.0 / (1.0 + tanA * tanA);
    vec3 co = o - uLPos;
    float vd = dot(V, uLAxis), cd = dot(co, uLAxis);
    float a = vd * vd - cos2, b = 2.0 * (vd * cd - cos2 * dot(V, co)), c = cd * cd - cos2 * dot(co, co);
    // the lamp's side of the double cone: h(t) = cd + t vd > 0
    float h0 = -1e9, h1 = 1e9;
    if (abs(vd) < 1e-6) { if (cd <= 0.0) return vec2(1.0, 0.0); }
    else if (vd > 0.0) h0 = -cd / vd;
    else h1 = -cd / vd;
    float disc = b * b - 4.0 * a * c;
    float t0, t1;
    if (abs(a) < 1e-7) {
        // one root
        if (abs(b) < 1e-7) return vec2(1.0, 0.0);
        float r = -c / b;
        if (b > 0.0) { t0 = r; t1 = 1e9; } else { t0 = -1e9; t1 = r; }
    } else if (disc < 0.0) {
        if (a < 0.0) return vec2(1.0, 0.0);
        t0 = -1e9; t1 = 1e9;
    } else {
        float sq = sqrt(disc);
        float r0 = (-b - sq) / (2.0 * a), r1 = (-b + sq) / (2.0 * a);
        if (r0 > r1) { float tmp = r0; r0 = r1; r1 = tmp; }
        if (a < 0.0) { t0 = r0; t1 = r1; }
        else {
            // inside outside the roots: keep the side on the lamp's nappe
            float hl = cd + (r0 - 1.0) * vd;
            if (hl > 0.0 && cd + (r1 + 1.0) * vd <= 0.0) { t0 = -1e9; t1 = r0; }
            else if (hl <= 0.0) { t0 = r1; t1 = 1e9; }
            else { t0 = -1e9; t1 = 1e9; }
        }
    }
    t0 = max(max(t0, h0), 0.0);
    t1 = min(min(t1, h1), len);
    return vec2(t0, t1);
}

void main() {
    vec2 xy = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    const float Z0 = 9.0, Z1 = -26.0;
    float j = hash12(gl_FragCoord.xy + uSeed);
    vec2 wc = (uWell.xy + uWell.zw) * 0.5, wh = (uWell.zw - uWell.xy) * 0.5;
    // the whole ray, coarsely: how much smoke stands in it (the far wall shows through the rest), lit by the fill
    const int NC = 8;
    float dzc = (Z0 - Z1) / float(NC);
    float Tall = 1.0;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < NC; i++) {
        float d = dens(vec3(xy, Z0 - (float(i) + j) * dzc));
        float a = 1.0 - exp(-d * dzc * uExt);
        acc += Tall * a * uFill;
        Tall *= 1.0 - a;
    }
    // the beam: march only where the ray crosses the cone (an exact span, so the edge stays crisp)
    vec2 span = coneSpan(vec3(xy, Z0), Z0 - Z1, uCone * 1.04);
    if (span.y > span.x) {
        const int N = 22;
        float dz = (span.y - span.x) / float(N);
        float T = 1.0;
        vec3 L = vec3(0.0);
        for (int i = 0; i < N; i++) {
            vec3 p = vec3(xy, Z0 - span.x - (float(i) + j) * dz);
            float d = dens(p);
            float s = spot(p, 0.02 + uThick * 0.6);
            vec3 toL = normalize(uLPos - p);
            float od = dens(p + toL * 1.3) * 1.3 + dens(p + toL * 3.6) * 2.3 + dens(p + toL * 8.0) * 4.4;
            float sh = exp(-od * uShadowK);
            // the well, a black flag standing in the light, shades the smoke behind it
            if (p.z < 0.0) {
                float t = -p.z / max(uLPos.z - p.z, 1e-3);
                vec2 q = p.xy + (uLPos.xy - p.xy) * t;
                sh *= mix(0.05, 1.0, smoothstep(-0.2, 0.5, sdBox(q - wc, wh)));
            }
            float a = 1.0 - exp(-d * dz * uExt);
            L += T * a * s * sh;
            T *= 1.0 - a;
        }
        acc += uLCol * uGain * L * mix(1.0, Tall, 0.5);
    }
    frag = vec4(acc, Tall);
}
)";

// ---------------------------------------------------------------- the stage at full resolution

inline const char* COMPOSITE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uSmokeT, uNoise;
uniform vec2 uSmokeTexel;
uniform vec4 uView;
uniform float uPx, uTime;
uniform vec4 uWell;
uniform vec3 uPanel, uRim, uFill, uLCol, uWallCol;
uniform float uGain, uWall, uPanelAlpha;
uniform float uOverFog;
uniform vec3 uMist;
uniform float uBridge;
uniform vec3 uBridgeCol;
uniform float uWorld;

void main() {
    vec2 uv = vUv;
    vec2 p = uView.xy + (uv * 2.0 - 1.0) * uView.zw;
    // the smoke, upsampled softly (a small tent over the reduced buffer)
    vec4 sm = texture(uSmokeT, uv) * 0.4;
    sm += texture(uSmokeT, uv + uSmokeTexel * vec2(0.7, 0.4)) * 0.15;
    sm += texture(uSmokeT, uv + uSmokeTexel * vec2(-0.4, 0.7)) * 0.15;
    sm += texture(uSmokeT, uv + uSmokeTexel * vec2(-0.7, -0.4)) * 0.15;
    sm += texture(uSmokeT, uv + uSmokeTexel * vec2(0.4, -0.7)) * 0.15;
    // the far wall of the stage: black, with the pool of the beam where it lands
    vec3 back = uFill * 0.15 + uWallCol * uLCol * uGain * uWall * spot(vec3(p, -24.0), 0.35);
    vec3 col = back * sm.a + sm.rgb;

    // the well: a slab of smoked black glass, the beam grazing it, a hairline catching the light
    vec2 wc = (uWell.xy + uWell.zw) * 0.5, wh = (uWell.zw - uWell.xy) * 0.5;
    float sW = sdRound(p - wc, wh, 0.06);
    float inW = 1.0 - smoothstep(-uPx, uPx, sW);
    if (inW > 0.0) {
        float lit = spot(vec3(p, 0.2), 0.25);
        vec3 glass = uPanel + uLCol * uGain * lit * 0.008 + sm.rgb * 0.035;
        col = mix(col, glass, inW * uPanelAlpha);
    }
    {
        vec2 q = abs(p - wc) - wh;
        vec2 n = q.x > q.y ? vec2(sign(p.x - wc.x), 0.0) : vec2(0.0, sign(p.y - wc.y));
        vec3 toL = uLPos - vec3(p, 0.0);
        float facing = max(dot(n, normalize(toL.xy)), 0.0);
        float lit = spot(vec3(p, 0.3), 0.5);
        float line = exp(-abs(sW) / (uPx * 0.9));
        col += uRim * line * (0.05 + (0.25 + 1.1 * lit) * facing) * uWorld;
    }

    // game over: the smoke rises from below and swallows the stage
    if (uOverFog > 0.001) {
        float n = texture(uNoise, p * 0.04 + vec2(uTime * 0.008, -uTime * 0.025)).r * 0.6
                + texture(uNoise, p * 0.11 + vec2(-uTime * 0.015, -uTime * 0.04)).g * 0.4;
        float top = mix(uView.y - uView.w - 3.0, uView.y + uView.w * 0.9, uOverFog);
        float m = smoothstep(top + 3.0, top - 3.0, p.y + (n - 0.5) * 7.0);
        col = mix(col, uMist * (0.7 + 0.6 * n), m * 0.94);
    }
    // the bridge (a change of universe): the smoke thickens until it fills the frame
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

// a0: center xy, half size xy (board). a1: rgb, alpha. a2: kind, age, angle, unused. a3: extra.
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
uniform float uPx, uTime, uBridge;
uniform vec3 uTint, uKey, uFill, uRim, uBridgeInk, uGaugeLit, uGaugeBlade, uGaugeSlow;
uniform float uKeyGain, uSlow;

vec2 boxGrad(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    vec2 g = (q.x > 0.0 && q.y > 0.0) ? normalize(q) : (q.x > q.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    return g * sign(p + 1e-6);
}

// A satin block caught in the key light: a hard light from the lamp's side models it like a small object on a set
// (the face falls off from the lit corner to the far one, a broad soft sheen near the lit corner), a crisp narrow
// bevel catches the lamp on the edges facing it and the far edges fall into shadow. The colors stay true (the light's
// tint is only a touch).
vec3 satin(vec2 p, float s, vec3 pc, vec2 wpos, float lightK) {
    vec2 b = vec2(0.45 * s);
    float rad = 0.06 * s, bev = 0.075 * s;
    float sd = sdRound(p, b, rad);
    float inset = clamp(-sd / bev, 0.0, 1.0);
    vec2 g = boxGrad(p, b, rad);
    vec3 toL = uLPos - vec3(wpos, 0.5);
    vec2 l2 = normalize(toL.xy);
    float key = clamp(spot(vec3(wpos, 0.4), 0.9), 0.0, 1.0);
    float k = mix(0.7, 1.0, key) * lightK;
    // the face: lit from the lamp's corner
    float across = dot(p / b.x, l2);                 // -1.4 far corner .. 1.4 lit corner
    float face = 0.8 + 0.2 * across;
    float sheen = exp(-dot(p / b.x - l2 * 0.75, p / b.x - l2 * 0.75) * 1.4) * 0.22;
    // the bevel: toward the lamp lit, away from it in shadow
    float facing = dot(g, l2);
    float bevel = 1.0 - inset;
    vec3 base = pc * uTint;
    vec3 col = base * mix(face, 0.75 + 0.8 * facing, bevel * 0.9) * k;
    col += uKey * (sheen * (1.0 - bevel) + bevel * max(facing, 0.0) * 0.28) * k * uKeyGain;
    col += base * uFill * 2.0;
    return col;
}

void main() {
    int kind = int(v2.x + 0.5);
    vec3 pc = v1.rgb;
    float alpha = v1.a;
    vec2 p = vLocal;
    float px = uPx;
    float bridgeK = smoothstep(0.2, 0.85, uBridge);
    if (kind == 0) {
        // a block: v3.x scale, v3.y light (1 lit, 0 dark), v3.z lock flash age (< 0 none)
        float s = v3.x;
        float sd = sdRound(p, vec2(0.45 * s), 0.07 * s);
        float cov = 1.0 - smoothstep(-px * 0.7, px * 0.7, sd);
        if (cov <= 0.0) discard;
        vec3 col = satin(p, s, pc, v0.xy, v3.y);
        float age = v3.z;
        if (age >= 0.0) col += mix(pc, uKey, 0.5) * exp(-age * 5.0) * 0.45;
        col = mix(col, uBridgeInk, bridgeK);
        float a = cov * alpha;
        frag = vec4(col * a, a);
        return;
    }
    if (kind == 3) {
        // the soft shadow a block casts on the glass, away from the lamp
        float s = v3.x;
        float sd = sdRound(p, vec2(0.45 * s), 0.1 * s);
        float a = (1.0 - smoothstep(-0.08 * s, 0.22 * s, sd)) * alpha * 0.75 * (1.0 - uBridge);
        frag = vec4(vec3(0.0), a);
        return;
    }
    if (kind == 1) {
        // the ghost: a hairline outline in the light, an almost-nothing fill
        float sd = sdRound(p, vec2(0.45), 0.07);
        float line = (1.0 - smoothstep(0.0, px * 1.1, abs(sd + 0.03))) * 0.38;
        float fill = (1.0 - smoothstep(-px, px, sd)) * 0.04;
        vec3 col = mix(pc, uKey, 0.25) * (line + fill);
        float a = (line * 0.8 + fill) * alpha * (1.0 - uBridge);
        frag = vec4(col * alpha * (1.0 - uBridge), a);
        return;
    }
    if (kind == 2) {
        // a cleared block turning to smoke: v3.y progress 0..1, v3.z seed. The noise eats it from its edges inward
        // (its top going last, as if it lifted away), and the eroding rim thins into smoke of its own color.
        float k = v3.y;
        float sd = sdRound(p, vec2(0.45), 0.07);
        float cov = 1.0 - smoothstep(-px * 0.7, px * 0.7, sd);
        if (cov <= 0.0) discard;
        float n = texture(uNoise, vWorld * 0.13 + v3.z).r * 0.5 + texture(uNoise, vWorld * 0.09 + v3.z * 1.7).b * 0.5;
        float inner = clamp(-sd / 0.45, 0.0, 1.0);            // 0 at the edge, 1 at the heart
        float field = inner * 0.8 + (n - 0.5) * 0.8 + 0.2 + p.y * 0.12;
        float th = k * 1.25 - 0.08;
        float keep = smoothstep(th, th + 0.07, field);
        float rim = smoothstep(th - 0.12, th, field) * (1.0 - keep); // the frayed rim, already smoke
        vec3 col = satin(p, 1.0, pc, v0.xy, 1.0);
        vec3 smokeC = pc * uTint * (0.55 + 0.35 * uKeyGain);
        col = mix(smokeC, col, keep);
        float a = cov * (keep + rim * 0.45) * alpha * (1.0 - uBridge);
        frag = vec4(col * a, a);
        return;
    }
    if (kind == 4) {
        // the bonus gauge: a camera's iris opening as it fills (six blades); the lamp shines through it when ready,
        // silver while the slow motion runs
        float R = v3.w;
        vec2 q = p / R;
        float r = length(q);
        float aa = px / R * 1.3;
        float lvl = v3.x, ready = v3.y, isOn = v3.z;
        float open = mix(0.1, 0.78, lvl);
        float rot = lvl * 1.2;
        float ang = atan(q.y, q.x) + rot;
        const float SEG = 1.0471976;
        float sa = mod(ang, SEG) - SEG * 0.5;
        float hexR = length(q) * cos(sa) / cos(SEG * 0.5);
        float hole = 1.0 - smoothstep(open - aa, open + aa, hexR);
        float disc = 1.0 - smoothstep(0.92 - aa, 0.92 + aa, r);
        float blades = disc * (1.0 - hole);
        float bi = floor(mod(ang, 6.2831853) / SEG);
        float bladeShade = 0.55 + 0.45 * fract(bi * 0.618 + 0.2);
        float ringR = exp(-pow((r - 0.96) / (0.03 + aa), 2.0));
        float pulse = 0.75 + 0.25 * sin(uTime * 3.5);
        vec3 lightC = isOn > 0.5 ? uGaugeSlow : uGaugeLit;
        vec3 col = uGaugeBlade * bladeShade * blades;
        float glow = hole * disc * (0.25 + 0.75 * lvl) * (ready > 0.5 || isOn > 0.5 ? 0.9 + 0.3 * pulse : 0.5);
        col += lightC * glow;
        col += uRim * ringR * (0.4 + 0.6 * ready);
        float a = max(disc, ringR * 0.8);
        col = mix(col, uBridgeInk * a, uBridge);
        frag = vec4(col * alpha, a * alpha);
        return;
    }
    discard;
}
)";

// ---------------------------------------------------------------- sprites: smoke puffs and wisps, the ring, motes

// a0: center xy, radius, stretch y. a1: rgb, alpha. a2: kind (0 puff, 1 ring, 2 mote, 3 blade, 4 a cleared block's smoke), seed,
// progress, swirl (4: seconds).
inline const char* SPRITE_VS = R"(#version 330 core
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
    vec2 ext = int(a2.x + 0.5) == 3 ? vec2(a0.z, a0.w) : vec2(a0.z, a0.z * a0.w);
    vec2 p = a0.xy + aCorner * ext;
    gl_Position = uVP * vec4(p, 0.0, 1.0);
}
)";
inline const char* SPRITE_FS = R"(#version 330 core
in vec2 vLocal;
flat in vec4 v0, v1, v2;
out vec4 frag;
uniform sampler2D uNoise;
uniform float uBridge;
void main() {
    int kind = int(v2.x + 0.5);
    vec2 q = vLocal;
    float fade = 1.0 - uBridge;
    if (kind == 2) {
        // a mote of dust glinting in the beam
        float r = length(q);
        float a = exp(-r * r * 9.0) * v1.a * fade;
        frag = vec4(v1.rgb * a, 0.0);
        return;
    }
    if (kind == 3) {
        // a blade of light running along a cleared row
        float a = exp(-q.y * q.y * 18.0) * (1.0 - smoothstep(0.75, 1.0, abs(q.x))) * v1.a * fade;
        frag = vec4(v1.rgb * a, 0.0);
        return;
    }
    if (kind == 4) {
        // a cleared block's body streaming up as smoke: a column over the block, v2.y the row's seed, v2.z the rising
        // speed, v2.w seconds. A parcel of smoke at height h left the block h / V seconds ago: the block gives off
        // smoke while it erodes, so the column is a rising band of tendrils whose pattern moves with the smoke.
        float seed = v2.y, V = v2.z, t = v2.w;
        vec2 ext = vec2(v0.z, v0.z * v0.w);
        vec2 wp = v0.xy + q * ext;
        float rowY = v0.y - ext.y + 0.6;
        float h = max(wp.y - rowY, 0.0);
        // some of the smoke climbs faster than the rest: fingers, not a bar
        V *= 0.5 + 1.0 * texture(uNoise, vec2(wp.x * 0.035 + seed * 0.3, 0.37 + seed)).g;
        float te = t - h / V;                       // when this parcel left the block
        const float E = 0.42;                       // the block erodes for that long
        float env = smoothstep(-0.03, 0.08, te) * (1.0 - smoothstep(0.15, E + 0.3, te));
        if (env <= 0.0) discard;
        float age = h / V;
        // it curls sideways as it climbs, more the higher it gets
        float up = smoothstep(0.0, 2.5, h);
        float curl = (texture(uNoise, vec2(wp.x * 0.05 + seed, h * 0.09 + seed * 0.7)).g - 0.5) * 2.4 * up;
        float xo = wp.x + curl;
        float wcol = 0.55 + 0.3 * h;
        float col = 1.0 - smoothstep(wcol * 0.3, wcol, abs(xo - v0.x));
        // tendrils: thin sheets stretched along the rise, carried with the smoke (keyed on te)
        float n1 = texture(uNoise, vec2(xo * 0.06 + seed * 1.3, te * 0.12 + seed)).b;
        float n2 = texture(uNoise, vec2(xo * 0.05 + seed * 0.4, te * 0.2 + h * 0.03)).g;
        float sheet = 1.0 - abs(n1 * 2.0 - 1.0);
        float hk = smoothstep(0.0, 0.8, h);
        float tend = mix(1.0, smoothstep(0.4, 0.95, sheet) * (0.3 + 1.2 * n2), hk);
        // it tears open as it rises: holes that widen with the parcel's age
        float n3 = texture(uNoise, vec2(xo * 0.03 + seed * 0.9, te * 0.09 + h * 0.012 + seed * 0.2)).r;
        tend *= mix(1.0, smoothstep(0.15 + 0.3 * age, 0.5 + 0.3 * age, n3), hk);
        float a = col * env * tend * exp(-age * 1.3) * v1.a * fade;
        frag = vec4(v1.rgb * a, a);
        return;
    }
    float seed = v2.y, k = v2.z, sw = v2.w;
    // the smoke curls: the sprite's own domain warped by the noise, turning as it ages
    vec2 w = vec2(texture(uNoise, q * 0.21 + seed + vec2(0.0, k * 0.2)).r, texture(uNoise, q * 0.21 + seed * 1.3 + 0.5).b) - 0.5;
    float c = cos(sw * k), s = sin(sw * k);
    vec2 r2 = mat2(c, s, -s, c) * q + w * (0.6 + 0.8 * k);
    if (kind == 1) {
        // a smoke ring rolling up: an annulus seen a little from below, its far side thinner
        vec2 rq = q * 1.3 + w * (0.12 + 0.2 * k);
        vec2 e = rq * vec2(1.0, 2.8);
        float d = length(e);
        float n = texture(uNoise, rq * 0.8 + seed).g * 0.6 + texture(uNoise, rq * 2.1 + seed * 1.3).r * 0.4;
        float tube = exp(-pow((d - 0.8 + (n - 0.5) * 0.12) / (0.1 + 0.08 * k), 2.0));
        float back = 0.55 + 0.45 * smoothstep(-0.2, 0.15, -q.y);
        float a = tube * back * (0.55 + 0.75 * n) * v1.a * fade;
        frag = vec4(v1.rgb * a, a);
        return;
    }
    float r = length(r2);
    float n1 = texture(uNoise, r2 * 0.35 + seed).g;
    float n2 = texture(uNoise, r2 * 0.9 + seed * 1.7).r;
    // thin curling sheets rather than a soft ball
    float sheet = 1.0 - abs(n2 * 2.0 - 1.0);
    float m = smoothstep(1.0, 0.1, r + (n1 - 0.5) * 0.9) * (0.35 + 0.9 * sheet * sheet);
    float a = m * v1.a * fade;
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
    // a soft pool of darkness behind the letters (readable when the smoke drifts behind them)
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

// A photographic finish: a filmic curve with deep blacks, a soft bloom, a strong vignette (the edges of the stage
// fall into the dark), a fine grain.
inline const char* POST_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene, uBloom;
uniform vec2 uRes;
uniform float uTime, uFade, uDim, uBloomAmt, uExposure, uBridge, uGrain;
uniform vec3 uToe;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    c += texture(uBloom, vUv).rgb * uBloomAmt;
    vec3 raw = c;
    c *= uExposure;
    vec2 q = vUv - 0.5;
    c *= 1.0 - 0.55 * dot(q * vec2(1.0, 1.25), q * vec2(1.0, 1.25)) * 1.7 * (1.0 - uBridge);
    // a shoulder: the beam's core rolls to white, the colors stay
    vec3 over = max(c - 1.0, 0.0);
    c += (over.r + over.g + over.b) * 0.1;
    const float K = 0.6;
    c = mix(c, K + (1.0 - K) * (1.0 - exp(-(c - K) / (1.0 - K))), step(K, c));
    // the blacks keep a whisper of the stage's own tone
    c += uToe * (1.0 - uBridge);
    c = mix(c, raw, uBridge);
    float g = hash12(gl_FragCoord.xy + fract(uTime * 7.13) * 431.0) - 0.5;
    c = pow(max(c, 0.0), vec3(1.0 / 2.2));
    c += g * uGrain * (1.0 - uBridge);
    c *= 1.0 - uDim * 0.55;
    c *= uFade;
    frag = vec4(max(c, 0.0), 1.0);
}
)";

} // namespace smoke_shaders
