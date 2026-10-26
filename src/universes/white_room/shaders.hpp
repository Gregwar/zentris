#pragma once
// WHITE ROOM's shaders (see white_room.cpp). Everything is drawn in linear HDR into a scene target, then
// tone-mapped in the post.
//
// ROOM_FS ray-traces the room through the layout camera: a seamless white cyclorama (floor, a cove, the back wall),
// a low plinth, the opaque matte pane of the well, and a few matte balls on the floor; it lights them with two soft
// lamps in front of the well (the key, and a colored gel) and a soft ambient. The blocks are the shadow casters:
// seen from a lamp, the board's occupancy map (blurred by its mip levels as the shadow travels) throws the stack and
// the falling piece onto the wall and the floor behind, enlarged and soft, a shadow play. The pane casts nothing and
// receives nothing (an evenly lit ground behind the blocks); the plinth and the balls cast their own small shadows.

namespace white_room_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }
float sdRound(vec2 p, vec2 b, float r) { vec2 d = abs(p) - b + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }
// The two lamps: position (board space), color x intensity, radius (the bigger, the softer the shadows), how much
// stronger the light is on its own side of the room (a gel coming in from the side).
uniform vec3 uL0, uC0, uL1, uC1;
uniform float uS0, uS1, uSide0, uSide1;
vec3 lampPos(int i) { return i == 0 ? uL0 : uL1; }
vec3 dirTo(int i, vec3 p) { return normalize(lampPos(i) - p); }
// a gentle falloff with the distance to the lamp (1 at the well)
float lampFall(int i, vec3 p) { vec3 l = lampPos(i); return (dot(l, l) + 1600.0) / (dot(l - p, l - p) + 1600.0); }
float sideK(float side, vec3 lp, float x) { return 1.0 + side * clamp(sign(lp.x) * x / 16.0, -1.0, 1.0); }
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
uniform float uShadowAmt;     // how dark the shadow play is
uniform float uPx;            // a pixel in board units (at the well)

const vec3 PANEL_C = vec3(0.0, 0.1, -0.78);
const vec3 PANEL_H = vec3(5.22, 10.15, 0.17);
const float PANE_Z = -0.62;
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
    float lod = clamp(log2(max(blur * uOccRect.w, 1.0)) - 0.8, 0.0, uOccLod);
    return textureLod(uOcc, uv, lod).r;
}

// The shadow play: the blocks seen from lamp i, thrown onto a point of the room behind them. The ray from the point
// to the lamp crosses the blocks' plane; the penumbra grows with the distance from the blocks to the point, and
// shrinks with the distance from the lamp to the blocks (the lamp's radius seen from the blocks).
float stackShadow(vec3 p, int i, float radius) {
    vec3 l = lampPos(i);
    const float ZB = -0.2;
    if (p.z > ZB - 0.3 || l.z < ZB + 1.0) return 0.0;
    float s = (ZB - p.z) / (l.z - p.z);
    vec3 q = p + (l - p) * s;
    float d = length(l - p);
    float blur = s * d * radius / max((1.0 - s) * d, 1.0) * 0.65 + 0.04; // a fairly crisp penumbra
    return occAt(q.xy, blur);
}

// the plinth, seen from behind it
float plinthShadow(vec3 p, int i, float radius) {
    vec3 l = lampPos(i);
    if (p.z > -PLINTH_HX.y || l.z < 1.0) return 0.0;
    float s = (0.0 - p.z) / (l.z - p.z);
    vec3 q = p + (l - p) * s;
    float d = length(l - p);
    float pen = s * d * radius / max((1.0 - s) * d, 1.0) + 0.03;
    float sd = sdBox(q.xy - vec2(0.0, 0.5 * (uFloor + PLINTH_TOP)), vec2(PLINTH_HX.x, 0.5 * (PLINTH_TOP - uFloor)));
    return 1.0 - smoothstep(-pen, pen, sd);
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

// pane: a point on the pane (never shadowed); else a point of the room, the plinth or a ball
vec3 light(vec3 p, vec3 n, vec3 alb, float ao, int self, bool pane) {
    vec3 col = alb * uAmb * (0.7 + 0.3 * (n.y * 0.5 + 0.5)) * ao;
    for (int i = 0; i < 2; i++) {
        vec3 l = dirTo(i, p);
        vec3 c = i == 0 ? uC0 : uC1;
        float radius = i == 0 ? uS0 : uS1;
        float ndl = max(dot(n, l), 0.0);
        if (ndl <= 0.0 || dot(c, c) < 1e-6) continue;
        // the pane is kept clean: no shadow ever falls on it
        float vis = 1.0;
        if (!pane) {
            float sh = max(stackShadow(p, i, radius) * uShadowAmt, plinthShadow(p, i, radius) * 0.8);
            vis = 1.0 - sh * uShadowK;
            vis *= mix(1.0, ballShadow(p + n * 0.01, l, radius / length(lampPos(i) - p), self), uShadowK);
        }
        col += alb * c * ndl * vis * sideK(i == 0 ? uSide0 : uSide1, lampPos(i), p.x) * lampFall(i, p);
    }
    return col;
}

// the room, the plinth and the balls (everything but the pane)
vec3 trace(vec3 ro, vec3 rd) {
    vec3 n, nb;
    float t = roomHit(ro, rd, n);
    int what = 0; // 0 room, 2 plinth, 3 ball
    int ball = -1;
    float tb = boxHit(ro, rd, vec3(0.0, 0.5 * (uFloor + PLINTH_TOP), 0.0), vec3(PLINTH_HX.x, 0.5 * (PLINTH_TOP - uFloor), PLINTH_HX.y), nb);
    if (tb > 0.0 && tb < t) { t = tb; n = nb; what = 2; }
    for (int i = 0; i < uBalls; i++) {
        float tt = ballHit(ro, rd, i, nb);
        if (tt > 0.0 && tt < t) { t = tt; n = nb; what = 3; ball = i; }
    }
    vec3 p = ro + rd * t;
    vec3 col;
    if (what == 3) {
        vec3 alb = uBallC[ball].rgb;
        float ao = 0.75 + 0.25 * (n.y * 0.5 + 0.5);
        col = light(p, n, alb, ao, ball, false);
        vec3 h = normalize(dirTo(0, p) - rd);
        col += uC0 * pow(max(dot(n, h), 0.0), 24.0) * 0.06; // a soft sheen (matte plastic)
    } else if (what == 2) {
        float ao = 1.0 - 0.28 * occAt(vec2(p.x, -9.6), 0.8) * smoothstep(0.6, -0.6, p.z) * uShadowK;
        col = light(p, n, uWall * 0.98, ao, -1, false);
    } else {
        // the room: a soft pool of light on the wall, the floor darkened around the plinth and under the balls
        float ao = ballAO(p, n, -1);
        if (n.y > 0.9) {
            vec2 d = abs(vec2(p.x, p.z)) - PLINTH_HX;
            float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
            ao *= 1.0 - 0.5 * exp(-max(dist, 0.0) / 0.45);
        }
        ao = mix(1.0, ao, uShadowK);
        col = light(p, n, uWall, ao, -1, false);
        vec2 pd = (p.xy - uPool.xy) / uPool.z;
        col *= 1.0 + uPool.w * (exp(-dot(pd, pd)) - 0.35);
        col *= 1.0 - 0.1 * smoothstep(4.0, 16.0, p.y); // the far top fades a touch (an endless space)
    }
    return col;
}

void main() {
    vec2 ndc = vUv * 2.0 - 1.0;
    vec3 ro = uEye;
    vec3 rd = normalize(vec3(ndc.x * uTan.x, ndc.y * uTan.y, -1.0));
    vec3 col = trace(ro, rd);
    // the well: a pane of frosted glass standing on the plinth; it veils the shadows behind it (the stack reads on a
    // calm ground) and catches the blocks' own short contact shadows; a hairline marks its edge
    float tp = (PANE_Z - ro.z) / rd.z;
    vec3 pp = ro + rd * tp;
    float sd = sdBox(pp.xy - PANEL_C.xy, PANEL_H.xy);
    if (sd < uPx * 1.5) {
        // an even, calm ground behind the blocks: no shadow, no gradient
        // in the panel's own color at one steady level, whatever the lamps and the gels do
        vec3 paneCol = uPanel;
        float inside = 1.0 - smoothstep(-uPx * 0.5, uPx * 0.5, sd);
        col = mix(col, paneCol, 0.8 * inside); // the room shows faintly through it
        float line = 1.0 - smoothstep(0.0, uPx * 1.2, abs(sd));
        col *= 1.0 - 0.22 * line;
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
out vec3 vPos;
flat out vec4 v0, v1, v2, v3;
void main() {
    vec2 l = aCorner * a0.zw;
    vec2 p = a0.xy + l;
    vLocal = l;
    vWorld = p;
    v0 = a0; v1 = a1; v2 = a2; v3 = a3;
    vec3 w = vec3(p, 0.0);
    if (int(a2.x + 0.5) == 5) {
        // a side of a cube: along its edge (u) and back from the front face (v: 0 front .. 1 back)
        int side = int(a2.y + 0.5);
        float h = 0.45 * v3.x, depth = 1.8 * v3.x; // deep enough for the camera's perspective to show its sides
        float u = aCorner.x * h, v = aCorner.y * 0.5 + 0.5;
        vec2 e = side == 0 ? vec2(-h, u) : side == 1 ? vec2(h, u) : side == 2 ? vec2(u, -h) : vec2(u, h);
        w = vec3(a0.xy + e, -depth * v);
        vLocal = vec2(u, v);
    }
    vPos = w;
    gl_Position = uVP * vec4(w, 1.0);
}
)";
inline const char* TILE_FS = R"(#version 330 core
in vec2 vLocal;
in vec2 vWorld;
in vec3 vPos;
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
    float rr = 0.06 * s;
    float sd = sdRound(p, b, rr);
    float e = clamp(1.0 + sd / rr, 0.0, 1.0);
    e = e * e;
    vec2 g = boxGrad(p, b, rr);
    vec3 n = normalize(vec3(g * e, sqrt(max(1.0 - e * e, 0.0))));
    vec3 col = alb * uAmbB * (0.8 + 0.2 * n.z);
    for (int i = 0; i < 2; i++) {
        vec3 l = dirTo(i, wpos);
        vec3 c = i == 0 ? uC0 : uC1;
        float ndl = max(dot(n, l), 0.0);
        float fall = lampFall(i, wpos);
        col += alb * c * ndl * sideK(i == 0 ? uSide0 : uSide1, lampPos(i), wpos.x) * fall;
        vec3 h = normalize(l + vec3(0, 0, 1));
        col += c * pow(max(dot(n, h), 0.0), 18.0) * 0.05 * fall;
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
        float sd = sdRound(p, vec2(0.45 * s), 0.06 * s);
        float cov = 1.0 - smoothstep(-px * 0.7, px * 0.7, sd);
        if (cov <= 0.0) discard;
        vec3 alb = mix(pc, uPlaster, v3.y);
        vec3 col = matte(p, s, alb, vec3(v0.xy, 0.5));
        col = mix(col, uBridgeInk, bridgeK);
        float a = cov * alpha;
        frag = vec4(col * a, a);
        return;
    }
    if (kind == 5) {
        // a side of a cube: matte, lit by the room's lights by its own normal, darker toward the back
        int side = int(v2.y + 0.5);
        vec3 n = side == 0 ? vec3(-1, 0, 0) : side == 1 ? vec3(1, 0, 0) : side == 2 ? vec3(0, -1, 0) : vec3(0, 1, 0);
        vec3 alb = mix(pc, uPlaster, v3.y);
        vec3 col = alb * uAmbB * (side == 3 ? 0.95 : 0.7);
        for (int i = 0; i < 2; i++) {
            vec3 l = dirTo(i, vPos);
            vec3 c = i == 0 ? uC0 : uC1;
            col += alb * c * max(dot(n, l), 0.0) * sideK(i == 0 ? uSide0 : uSide1, lampPos(i), vPos.x) * lampFall(i, vPos);
        }
        col += alb * (side == 3 ? 0.22 : 0.12); // light bounced off the room (never a black face)
        col *= 1.0 - 0.3 * vLocal.y; // toward the back, in the cube's own shade
        col = mix(col, uBridgeInk, bridgeK);
        frag = vec4(col * alpha, alpha);
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
