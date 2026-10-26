#pragma once
// WHITE ROOM's shaders (see white_room.cpp). Everything is drawn in linear HDR into a scene target, then
// tone-mapped in the post.
//
// ROOM_FS ray-traces the room through the layout camera: a seamless white cyclorama (floor, a cove, the back wall),
// the plinth and the white panel of the well, and a few matte balls on the floor; it lights them with two soft
// directional area lights (the key, and a colored gel) and a soft ambient, with soft shadows: the panel's and the
// plinth's (analytic, the penumbra growing with the distance), the blocks' (an occupancy map of the board,
// blurred by its mip levels as the shadow travels) and the balls' (analytic).

namespace white_room_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }
float sdRound(vec2 p, vec2 b, float r) { vec2 d = abs(p) - b + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }
// The two lights: direction toward the light (unit), color x intensity, softness (tan of the angular radius),
// how much stronger the light is on its own side of the room (a gel coming in from the side).
uniform vec3 uL0, uC0, uL1, uC1;
uniform float uS0, uS1, uSide0, uSide1;
uniform vec4 uLamp;        // slow motion: a point lamp (xyz), w: how much light 0 is that lamp
vec3 dirTo0(vec3 p) { return normalize(mix(uL0, normalize(uLamp.xyz - p), uLamp.w)); }
float sideK(float side, vec3 l, float x) { return 1.0 + side * clamp(sign(l.x) * x / 16.0, -1.0, 1.0); }
)";

inline const char* ROOM_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform vec3 uEye;
uniform vec2 uTan;            // tan of the half fov (x, y)
uniform vec3 uAmb;            // ambient (sky of the room), color x intensity
uniform vec3 uWall, uPanel;   // albedos
uniform float uFloor, uWallZ, uCove;
uniform vec4 uPool;           // the light pool on the wall: center xy, radius, strength
uniform sampler2D uOcc;       // the board's occupancy (blocks, piece, previews): r
uniform vec4 uOccRect;        // x0, y0, size (board units), texels per unit
uniform float uOccLod;        // the map's last mip level
uniform int uBalls;
uniform vec4 uBallP[32];      // center, radius
uniform vec4 uBallC[32];      // albedo, squash (y scale)
uniform float uBridge;
uniform vec3 uBridgeCol;
uniform float uShadowK;       // 1: shadows, 0: none (bridge)

const vec3 PANEL_C = vec3(0.0, 0.1, -0.78);
const vec3 PANEL_H = vec3(5.22, 10.15, 0.17);
const float PLINTH_TOP = -10.05;
const vec2 PLINTH_HX = vec2(5.5, 1.3);

// ray / box: t of entry (or -1) and the normal
float boxHit(vec3 ro, vec3 rd, vec3 c, vec3 h, out vec3 n) {
    vec3 inv = 1.0 / rd;
    vec3 t0 = (c - h - ro) * inv, t1 = (c + h - ro) * inv;
    vec3 tmin = min(t0, t1), tmax = max(t0, t1);
    float tn = max(max(tmin.x, tmin.y), tmin.z), tf = min(min(tmax.x, tmax.y), tmax.z);
    if (tf < max(tn, 0.0)) return -1.0;
    n = tn == tmin.x ? vec3(-sign(rd.x), 0, 0) : (tn == tmin.y ? vec3(0, -sign(rd.y), 0) : vec3(0, 0, -sign(rd.z)));
    return tn;
}

// the cyclorama: floor y = uFloor, back wall z = uWallZ, joined by a quarter cylinder of radius uCove
float roomHit(vec3 ro, vec3 rd, out vec3 n) {
    float zc = uWallZ + uCove, yc = uFloor + uCove;
    if (rd.y < 0.0) {
        float t = (uFloor - ro.y) / rd.y;
        vec3 p = ro + rd * t;
        if (p.z >= zc) { n = vec3(0, 1, 0); return t; }
    }
    if (rd.z < 0.0) {
        float t = (uWallZ - ro.z) / rd.z;
        vec3 p = ro + rd * t;
        if (p.y >= yc) { n = vec3(0, 0, 1); return t; }
    }
    // the cove (the far side of the cylinder seen from inside the room)
    vec2 o = ro.yz - vec2(yc, zc);
    vec2 d = rd.yz;
    float a = dot(d, d), b = dot(o, d), c = dot(o, o) - uCove * uCove;
    float disc = b * b - a * c;
    if (disc < 0.0 || a < 1e-6) { n = vec3(0, 0, 1); return 1e4; }
    float t = (-b + sqrt(disc)) / a;
    vec3 p = ro + rd * t;
    n = normalize(vec3(0.0, yc - p.y, zc - p.z));
    return t;
}

float ballHit(vec3 ro, vec3 rd, int i, out vec3 n) {
    vec4 b = uBallP[i];
    float sq = uBallC[i].w;
    // an ellipsoid squashed toward the floor: scale y around its bottom
    vec3 base = vec3(b.x, b.y - b.w * sq, b.z);
    vec3 s = vec3(1.0, 1.0 / sq, 1.0);
    vec3 o = (ro - base) * s - vec3(0, b.w, 0);
    vec3 d = rd * s;
    float A = dot(d, d), B = dot(o, d), C = dot(o, o) - b.w * b.w;
    float disc = B * B - A * C;
    if (disc < 0.0) return -1.0;
    float t = (-B - sqrt(disc)) / A;
    if (t < 0.0) return -1.0;
    vec3 q = o + d * t;
    n = normalize(q * s);
    return t;
}

float occAt(vec2 p, float blur) {
    vec2 uv = (p - uOccRect.xy) / uOccRect.z;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return 0.0;
    float lod = clamp(log2(max(blur * uOccRect.w, 1.0)), 0.0, uOccLod);
    return textureLod(uOcc, uv, lod).r;
}

// the blocks' shadow: where the ray toward the light crosses the slab of the blocks (z -0.5 .. 0.5)
float blockShadow(vec3 p, vec3 l, float soft) {
    if (l.z <= 0.02) return 0.0;
    float t0 = max(0.0, (-0.5 - p.z) / l.z), t1 = (0.5 - p.z) / l.z;
    if (t1 <= t0) return 0.0;
    // a few samples through the depth of the blocks, each blurred by how far the shadow has traveled; a jitter per
    // pixel hides the steps between them
    float occ = 0.0;
    float j = hash12(gl_FragCoord.xy) - 0.5;
    float step_ = (t1 - t0) / 8.0 * length(l.xy);
    for (int k = 0; k < 8; k++) {
        float t = mix(t0, t1, (float(k) + 0.5 + j * 0.6) / 8.0);
        vec3 q = p + l * t;
        occ = max(occ, occAt(q.xy, t * soft + 0.08 + 0.5 * step_));
    }
    return occ;
}

// the panel and the plinth, seen from behind them
float panelShadow(vec3 p, vec3 l, float soft) {
    float occ = 0.0;
    if (p.z < PANEL_C.z - PANEL_H.z && l.z > 0.02) {
        float t = (PANEL_C.z - p.z) / l.z;
        vec3 q = p + l * t;
        float pen = t * soft + 0.03;
        float d = sdBox(q.xy - PANEL_C.xy, PANEL_H.xy);
        occ = 1.0 - smoothstep(-pen, pen, d);
    }
    if (p.z < -PLINTH_HX.y && l.z > 0.02) {
        float t = (0.0 - p.z) / l.z;
        vec3 q = p + l * t;
        float pen = t * soft + 0.03;
        float d = sdBox(q.xy - vec2(0.0, 0.5 * (uFloor + PLINTH_TOP)), vec2(PLINTH_HX.x, 0.5 * (PLINTH_TOP - uFloor)));
        occ = max(occ, 1.0 - smoothstep(-pen, pen, d));
    }
    return occ;
}

float ballShadow(vec3 p, vec3 l, float soft, int self) {
    float vis = 1.0;
    for (int i = 0; i < uBalls; i++) {
        if (i == self) continue;
        vec4 b = uBallP[i];
        vec3 oc = b.xyz - p;
        float tca = dot(oc, l);
        if (tca <= 0.0) continue;
        float d = length(oc - l * tca);
        float r = b.w * mix(1.0, uBallC[i].w, 0.5);
        float pen = tca * soft + 0.04;
        vis *= smoothstep(r - pen, r + pen, d);
    }
    return vis;
}

float ballAO(vec3 p, vec3 n, int self) {
    float ao = 1.0;
    for (int i = 0; i < uBalls; i++) {
        if (i == self) continue;
        vec4 b = uBallP[i];
        vec3 oc = b.xyz - p;
        float d2 = dot(oc, oc);
        float d = sqrt(d2);
        ao *= 1.0 - clamp(dot(n, oc / d) * b.w * b.w / d2, 0.0, 1.0) * 0.9;
    }
    return ao;
}

vec3 light(vec3 p, vec3 n, vec3 alb, float ao, int self, bool panelFace, bool behind) {
    vec3 col = alb * uAmb * (0.7 + 0.3 * (n.y * 0.5 + 0.5)) * ao;
    for (int i = 0; i < 2; i++) {
        vec3 l = i == 0 ? dirTo0(p) : uL1;
        vec3 c = i == 0 ? uC0 : uC1;
        float soft = i == 0 ? uS0 : uS1;
        float ndl = max(dot(n, l), 0.0);
        if (ndl <= 0.0 || dot(c, c) < 1e-6) continue;
        float vis = 1.0;
        float sh = blockShadow(p + n * 0.01, l, soft) * 0.92;
        if (behind) sh = max(sh, panelShadow(p, l, soft));
        vis *= 1.0 - sh * uShadowK;
        vis *= mix(1.0, ballShadow(p + n * 0.01, l, soft, self), uShadowK);
        float lampFall = i == 0 ? mix(1.0, 260.0 / (40.0 + dot(uLamp.xyz - p, uLamp.xyz - p)), uLamp.w) : 1.0;
        col += alb * c * ndl * vis * sideK(i == 0 ? uSide0 : uSide1, l, p.x) * lampFall;
    }
    return col;
}

void main() {
    vec2 ndc = vUv * 2.0 - 1.0;
    vec3 ro = uEye;
    vec3 rd = normalize(vec3(ndc.x * uTan.x, ndc.y * uTan.y, -1.0));
    vec3 n, nb;
    float t = roomHit(ro, rd, n);
    int what = 0; // 0 room, 1 panel, 2 plinth, 3 ball
    int ball = -1;
    float tb = boxHit(ro, rd, PANEL_C, PANEL_H, nb);
    if (tb > 0.0 && tb < t) { t = tb; n = nb; what = 1; }
    tb = boxHit(ro, rd, vec3(0.0, 0.5 * (uFloor + PLINTH_TOP), 0.0), vec3(PLINTH_HX.x, 0.5 * (PLINTH_TOP - uFloor), PLINTH_HX.y), nb);
    if (tb > 0.0 && tb < t) { t = tb; n = nb; what = 2; }
    for (int i = 0; i < uBalls; i++) {
        float tt = ballHit(ro, rd, i, nb);
        if (tt > 0.0 && tt < t) { t = tt; n = nb; what = 3; ball = i; }
    }
    vec3 p = ro + rd * t;
    vec3 col;
    if (what == 3) {
        vec3 alb = uBallC[ball].rgb;
        // the floor bounces a little light up under the ball
        float ao = 0.75 + 0.25 * (n.y * 0.5 + 0.5);
        col = light(p, n, alb, ao, ball, false, p.z < -0.96);
        // a soft sheen (matte plastic)
        vec3 h = normalize(dirTo0(p) - rd);
        col += uC0 * pow(max(dot(n, h), 0.0), 24.0) * 0.06;
    } else if (what == 1) {
        // the panel: its face catches the blocks' shadows; its rounded edge catches the light
        float ao = 1.0 - 0.32 * occAt(p.xy, 0.9) * uShadowK;
        vec2 q = p.xy - PANEL_C.xy;
        float e = clamp(1.0 + sdBox(q, PANEL_H.xy) / 0.09, 0.0, 1.0);
        vec2 g = abs(q.x) - PANEL_H.x > abs(q.y) - PANEL_H.y ? vec2(sign(q.x), 0.0) : vec2(0.0, sign(q.y));
        vec3 nn = n.z > 0.5 ? normalize(vec3(g * e, sqrt(max(1.0 - e * e, 0.0)))) : n;
        col = light(p, nn, uPanel, ao, -1, true, false);
    } else if (what == 2) {
        float ao = 1.0 - 0.28 * occAt(vec2(p.x, -9.6), 0.8) * smoothstep(0.6, -0.6, p.z) * uShadowK;
        col = light(p, n, uWall * 0.98, ao, -1, false, p.z < -0.96);
    } else {
        // the room: a soft pool of light on the wall, the floor darkened around the plinth and under the balls
        float ao = ballAO(p, n, -1);
        if (n.y > 0.9) {
            vec2 d = abs(vec2(p.x, p.z)) - PLINTH_HX;
            float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
            ao *= 1.0 - 0.5 * exp(-max(dist, 0.0) / 0.45);
        }
        ao = mix(1.0, ao, uShadowK);
        col = light(p, n, uWall, ao, -1, false, p.z < -0.96);
        vec2 pd = (p.xy - uPool.xy) / uPool.z;
        col *= 1.0 + uPool.w * (exp(-dot(pd, pd)) - 0.35);
        // the room's far top fades a touch (an endless space, not a ceiling)
        col *= 1.0 - 0.1 * smoothstep(4.0, 16.0, p.y);
    }
    col = mix(col, uBridgeCol, uBridge);
    frag = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- the occupancy map (board -> shadows)

// a0: center xy, half size, alpha (board units)
inline const char* OCC_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
uniform vec4 uOccRect;
out vec2 vLocal;
flat out vec4 v0;
void main() {
    vec2 p = a0.xy + aCorner * (a0.z + 0.1);
    vLocal = aCorner * (a0.z + 0.1);
    v0 = a0;
    vec2 uv = (p - uOccRect.xy) / uOccRect.z;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)";
inline const char* OCC_FS = R"(#version 330 core
in vec2 vLocal;
flat in vec4 v0;
out vec4 frag;
uniform float uPx;
void main() {
    float d = sdRound(vLocal, vec2(v0.z), 0.12 * v0.z);
    float a = (1.0 - smoothstep(-uPx, uPx, d)) * v0.w;
    frag = vec4(a, 0.0, 0.0, a);
}
)";

// ---------------------------------------------------------------- tiles: the stack, the piece, previews, gauge

// a0: center xy, half size xy (board). a1: rgb, alpha. a2: kind, unused x3. a3: extra.
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
uniform float uPx, uBridge, uTime;
uniform vec3 uAmbB, uBridgeInk, uInk, uGroove, uAccent, uPlaster;

vec2 boxGrad(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    vec2 g = (q.x > 0.0 && q.y > 0.0) ? normalize(q) : (q.x > q.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    return g * sign(p + 1e-6);
}

// A matte block (soft plastic, unglazed ceramic): a flat face and a soft rounded edge that turns toward the lights,
// lit by the room's two lights and its ambient; a faint broad sheen near the key's edge.
vec3 matte(vec2 p, float s, vec3 alb, vec3 wpos) {
    vec2 b = vec2(0.45 * s);
    float rr = 0.15 * s;
    float sd = sdRound(p, b, rr);
    float e = clamp(1.0 + sd / rr, 0.0, 1.0);
    e = e * e;
    vec2 g = boxGrad(p, b, rr);
    vec3 n = normalize(vec3(g * e, sqrt(max(1.0 - e * e, 0.0))));
    vec3 col = alb * uAmbB * (0.8 + 0.2 * n.z);
    for (int i = 0; i < 2; i++) {
        vec3 l = i == 0 ? dirTo0(wpos) : uL1;
        vec3 c = i == 0 ? uC0 : uC1;
        float ndl = max(dot(n, l), 0.0);
        float lampFall = i == 0 ? mix(1.0, 260.0 / (40.0 + dot(uLamp.xyz - wpos, uLamp.xyz - wpos)), uLamp.w) : 1.0;
        col += alb * c * ndl * sideK(i == 0 ? uSide0 : uSide1, l, wpos.x) * lampFall;
        vec3 h = normalize(l + vec3(0, 0, 1));
        col += c * pow(max(dot(n, h), 0.0), 18.0) * 0.05 * lampFall;
    }
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
        // a block: v3.x scale, v3.y plaster (0 its color .. 1 white plaster: a cleared block sinking into the
        // wall, the stack at game over)
        float s = v3.x;
        float sd = sdRound(p, vec2(0.45 * s), 0.15 * s);
        float cov = 1.0 - smoothstep(-px * 0.7, px * 0.7, sd);
        if (cov <= 0.0) discard;
        vec3 alb = mix(pc, uPlaster, v3.y);
        vec3 col = matte(p, s, alb, vec3(v0.xy, 0.5));
        col = mix(col, uBridgeInk, bridgeK);
        float a = cov * alpha;
        frag = vec4(col * a, a);
        return;
    }
    if (kind == 1) {
        // the ghost: a hairline outline in the piece's color, an almost-nothing fill
        float sd = sdRound(p, vec2(0.43), 0.14);
        float line = (1.0 - smoothstep(0.0, px * 1.2, abs(sd + 0.02))) * 0.75;
        float fill = (1.0 - smoothstep(-px, px, sd)) * 0.07;
        float a = (line + fill) * alpha * (1.0 - uBridge);
        frag = vec4(pc * a, a);
        return;
    }
    if (kind == 4) {
        // the bonus gauge: a fine groove in the wall, the filled arc in ink, a dot of the accent once it is ready
        float R = v3.w;
        vec2 q = p / R;
        float r = length(q);
        float aa = px / R * 1.2;
        float lvl = v3.x, ready = v3.y, isOn = v3.z;
        float ang = atan(q.x, q.y);                  // 0 at the top, clockwise
        float frac = (ang < 0.0 ? ang + 6.2831853 : ang) / 6.2831853;
        float ring = 1.0 - smoothstep(0.012, 0.012 + aa, abs(r - 0.82));
        float arc = (1.0 - smoothstep(0.035, 0.035 + aa, abs(r - 0.82))) * step(frac, lvl);
        float dot_ = 1.0 - smoothstep(0.13, 0.13 + aa, length(q - vec2(0.0, 0.0)));
        float breathe = 0.75 + 0.25 * sin(uTime * 2.5);
        vec3 col = uGroove * ring;
        float a = ring * 0.6;
        col = mix(col, uInk, arc);
        a = max(a, arc);
        float dk = dot_ * max(ready, isOn) * (isOn > 0.5 ? 1.0 : breathe);
        col = mix(col, uAccent, dk);
        a = max(a, dk);
        col = mix(col, uBridgeInk * a, uBridge);
        frag = vec4(col * alpha * a, a * alpha);
        return;
    }
    discard;
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
void main() { frag = vec4(vCol.rgb * vCol.a, vCol.a); }
)";

// ---------------------------------------------------------------- text

inline const char* TEXT_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;   // pixels, top-left origin
layout(location = 1) in vec2 aUv;
uniform vec2 uRes;
uniform vec2 uShift;                 // pixels: the lettering's shadow
out vec2 vUv;
void main() {
    vUv = aUv;
    vec2 p = aPos + uShift;
    gl_Position = vec4(p.x / uRes.x * 2.0 - 1.0, 1.0 - p.y / uRes.y * 2.0, 0.0, 1.0);
}
)";
// Lettering set on the wall: the fill, or (uSoft > 0) its soft shadow on the wall.
inline const char* TEXT_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uFont;
uniform vec3 uColor;
uniform float uAlpha, uBias, uSoft;
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.75 + uSoft;
    float fill = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    frag = vec4(uColor * fill * uAlpha, fill * uAlpha);
}
)";

// ---------------------------------------------------------------- the post

// A clean photographic finish: a soft shoulder (the white room stays white without clipping its colors), a very
// light vignette, a fine grain.
inline const char* POST_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene;
uniform vec2 uRes;
uniform float uTime, uFade, uDim, uExposure, uBridge, uGrain;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    vec3 raw = c;
    c *= uExposure;
    vec2 q = vUv - 0.5;
    c *= 1.0 - 0.22 * dot(q * vec2(1.0, 1.3), q * vec2(1.0, 1.3)) * (1.0 - uBridge);
    const float K = 0.7;
    c = mix(c, K + (1.0 - K) * (1.0 - exp(-(c - K) / (1.0 - K))), step(K, c));
    c = mix(c, raw, uBridge);
    float g = hash12(gl_FragCoord.xy + fract(uTime * 7.13) * 431.0) - 0.5;
    c = pow(max(c, 0.0), vec3(1.0 / 2.2));
    c += g * uGrain * (1.0 - uBridge);
    c *= 1.0 - uDim * 0.5;
    c *= uFade;
    frag = vec4(max(c, 0.0), 1.0);
}
)";

} // namespace white_room_shaders
