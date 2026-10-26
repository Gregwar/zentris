#pragma once
// WHITE ROOM's shaders (see white_room.cpp). Everything is drawn in linear HDR into a multisampled scene target,
// then tone-mapped in the post.
//
// ROOM_FS ray-traces the room through the layout camera: a seamless white cyclorama (floor, a cove, the back wall),
// a low plinth and the frosted pane of the well; it writes the depth of what it hits (the room, the plinth) so the
// cubes drawn after it are hidden where they should be. Every cube (the well's blocks, the falling piece, the
// previews, the cleared blocks thrown out) is one instanced mesh, rasterized in the same camera and lit the same way
// (CUBE_FS): matte, softly bevelled edges catching the light, a crease between neighbours, a faint sheen. Light: two
// soft lamps in front of the well (the key, and a colored gel) and a soft ambient. Shadows: the stack throws a shadow
// play on the wall and the floor behind, from the board's occupancy map seen from each lamp (blurred by its mip levels
// as the shadow travels); the thrown cubes throw theirs from a shadow map per lamp (soft: the penumbra grows with the
// distance to the caster), and darken the floor where they pile up (from the piles' heightmap). The pane casts
// nothing and receives nothing (an evenly lit ground behind the blocks); the plinth casts its own shadow.

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

// The room's light, shared by the room and the cubes (inserted after COMMON_GLSL in ROOM_FS and CUBE_FS).
inline const char* LIGHT_GLSL = R"(
uniform vec3 uAmb;            // ambient (sky of the room), color x intensity
uniform float uFloor;
uniform sampler2D uOcc;       // the board's occupancy (blocks, piece): r
uniform vec4 uOccRect;        // x0, y0, size (board units), texels per unit
uniform float uOccLod;        // the map's last mip level
uniform sampler2D uHm;        // the thrown cubes' piles: height above their ground (r), over x, z
uniform vec4 uHmRect;         // x0, z0, width, depth
uniform sampler2DShadow uSh0, uSh1; // the thrown cubes seen from each lamp (compared)
uniform sampler2D uShD0, uShD1;     // the same depths, raw (for the penumbra)
uniform sampler2D uShC0, uShC1;     // where the casters are (1), mipmapped: a single tap tells a point far from any
uniform mat4 uShVP0, uShVP1;
uniform vec4 uShP0, uShP1;    // on, tan of the half fov, near, far
uniform float uShadowK;       // 1: shadows, 0: none (bridge)
uniform float uShadowAmt;     // how dark the shadow play is

const vec3 PANEL_C = vec3(0.0, 0.1, -0.78);
const vec3 PANEL_H = vec3(5.22, 10.15, 0.17);
const float PANE_Z = -0.62;
const float PLINTH_TOP = -10.05;
const vec2 PLINTH_HX = vec2(5.5, 1.3);
const float SH_N = 1024.0;

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

float shCover(int i, vec2 uv, float lod) { return i == 0 ? textureLod(uShC0, uv, lod).r : textureLod(uShC1, uv, lod).r; }
float shRaw(int i, vec2 uv) { return i == 0 ? textureLod(uShD0, uv, 0.0).r : textureLod(uShD1, uv, 0.0).r; }
float shCmp(int i, vec3 c) { return i == 0 ? texture(uSh0, c) : texture(uSh1, c); }

// The thrown cubes' soft shadows from lamp i's shadow map: a few raw taps find the casters in the way and how far
// they are (the penumbra grows with the distance from the caster to the point, like a lamp of that radius), then a
// handful of compared taps (each one bilinear) over that penumbra. Most points find no caster and stop there.
float cubeShadow(int i, vec3 p, vec3 n, float radius) {
    vec4 P = i == 0 ? uShP0 : uShP1;
    if (P.x < 0.5) return 1.0;
    mat4 M = i == 0 ? uShVP0 : uShVP1;
    float texel = 2.0 * P.y / SH_N; // a texel's size per unit of distance
    float dist = length(lampPos(i) - p);
    vec4 c = M * vec4(p + n * (texel * dist * 1.5 + 0.015), 1.0);
    if (c.w <= P.z) return 1.0;
    vec3 s = c.xyz / c.w * 0.5 + 0.5;
    if (s.x < 0.0 || s.y < 0.0 || s.x > 1.0 || s.y > 1.0) return 1.0;
    float ref = min(s.z, 1.0) - 0.0004;
    float zr = c.w;
    float span = 2.0 * zr * P.y; // world units across the map at this distance
    float rot = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) * 6.2831853;
    mat2 R = mat2(cos(rot), sin(rot), -sin(rot), cos(rot));
    // the casters: their depth, linearized
    float search = clamp(radius * 0.5 / span, 2.0 / SH_N, 48.0 / SH_N);
    // no caster anywhere near (most of the room): lit, at the cost of one tap
    if (shCover(i, s.xy, clamp(log2(search * SH_N) + 1.5, 0.0, 10.0)) < 1e-4) return 1.0;
    float zb = 0.0, nb = 0.0;
    for (int k = 0; k < 7; k++) {
        float a = float(k) * 1.0472;
        vec2 o = k == 6 ? vec2(0.0) : R * vec2(cos(a), sin(a)) * (k % 2 == 0 ? 1.0 : 0.45);
        float d = shRaw(i, s.xy + o * search);
        if (d < ref) {
            float zn = d * 2.0 - 1.0;
            zb += 2.0 * P.z * P.w / (P.w + P.z - zn * (P.w - P.z));
            nb += 1.0;
        }
    }
    if (nb < 0.5) return 1.0;
    zb /= nb;
    float pen = radius * max(zr - zb, 0.0) / max(zb, 0.5) * 0.6 + texel * zr * 1.5;
    float r = clamp(pen / span, 1.0 / SH_N, 32.0 / SH_N);
    float lit = 0.0;
    for (int k = 0; k < 8; k++) {
        float a = float(k) * 0.7854 + 0.39;
        vec2 o = R * vec2(cos(a), sin(a)) * (k % 2 == 0 ? 0.95 : 0.5);
        lit += shCmp(i, vec3(s.xy + o * r, ref));
    }
    return lit / 8.0;
}

// how much the piles of thrown cubes darken a point (low in a crevice, on the floor beside a pile)
float pileAO(vec3 p) {
    vec2 uv = (p.xz - uHmRect.xy) / uHmRect.zw;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return 1.0;
    float H = 0.5 * (textureLod(uHm, uv, 1.0).r + textureLod(uHm, uv, 2.0).r);
    if (H < 0.01) return 1.0;
    float ground = (abs(p.x) < PLINTH_HX.x && abs(p.z) < PLINTH_HX.y) ? PLINTH_TOP : uFloor;
    float above = p.y - ground;
    return 1.0 - 0.55 * smoothstep(0.0, 0.7, H - above * 0.8) * uShadowK;
}

// a point of the room, the plinth or a cube; stack: whether the stack's shadow play reaches it
vec3 light(vec3 p, vec3 n, vec3 alb, float ao, bool stack) {
    vec3 col = alb * uAmb * (0.7 + 0.3 * (n.y * 0.5 + 0.5)) * ao;
    for (int i = 0; i < 2; i++) {
        vec3 l = dirTo(i, p);
        vec3 c = i == 0 ? uC0 : uC1;
        float radius = i == 0 ? uS0 : uS1;
        float ndl = max(dot(n, l), 0.0);
        if (ndl <= 0.0 || dot(c, c) < 1e-6) continue;
        float vis = 1.0;
        if (stack) {
            float sh = max(stackShadow(p, i, radius) * uShadowAmt, plinthShadow(p, i, radius) * 0.8);
            vis = 1.0 - sh * uShadowK;
        }
        vis *= mix(1.0, cubeShadow(i, p, n, radius), uShadowK);
        col += alb * c * ndl * vis * sideK(i == 0 ? uSide0 : uSide1, lampPos(i), p.x) * lampFall(i, p);
    }
    return col;
}
)";

inline const char* ROOM_FS = R"(#version 330 core
in vec2 vUv;
layout(location = 0) out vec4 frag;
layout(location = 1) out vec4 fragZ; // the depth of what it hit (copied into the scene's depth)
uniform vec3 uEye;
uniform vec2 uTan;            // tan of the half fov (x, y)
uniform mat4 uVP;             // the camera (the depth written)
uniform vec3 uWall, uPanel;   // albedos
uniform float uWallZ, uCove;
uniform vec4 uPool;           // the light pool on the wall: center xy, radius, strength
uniform float uBridge;
uniform vec3 uBridgeCol;
uniform float uPx;            // a pixel in board units (at the well)

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

// the room and the plinth; t: the distance of the hit
vec3 trace(vec3 ro, vec3 rd, out float t) {
    vec3 n, nb;
    t = roomHit(ro, rd, n);
    bool plinth = false;
    float tb = boxHit(ro, rd, vec3(0.0, 0.5 * (uFloor + PLINTH_TOP), 0.0), vec3(PLINTH_HX.x, 0.5 * (PLINTH_TOP - uFloor), PLINTH_HX.y), nb);
    if (tb > 0.0 && tb < t) { t = tb; n = nb; plinth = true; }
    vec3 p = ro + rd * t;
    vec3 col;
    float ao = pileAO(p);
    if (plinth) {
        ao *= 1.0 - 0.28 * occAt(vec2(p.x, -9.6), 0.8) * smoothstep(0.6, -0.6, p.z) * uShadowK;
        col = light(p, n, uWall * 0.98, ao, true);
    } else {
        // the room: a soft pool of light on the wall, the floor darkened around the plinth
        if (n.y > 0.9) {
            vec2 d = abs(vec2(p.x, p.z)) - PLINTH_HX;
            float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
            ao *= 1.0 - 0.5 * exp(-max(dist, 0.0) / 0.45) * uShadowK;
        }
        col = light(p, n, uWall, ao, true);
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
    float t;
    vec3 col = trace(ro, rd, t);
    // the depth of the room (the cubes drawn after it are hidden behind the plinth)
    vec4 cp = uVP * vec4(ro + rd * min(t, 700.0), 1.0);
    fragZ = vec4(clamp(cp.z / cp.w * 0.5 + 0.5, 0.0, 1.0));
    // the well: a pane of frosted glass standing on the plinth; it veils what lies behind it (the stack reads on a
    // calm ground); a hairline marks its edge
    float tp = (PANE_Z - ro.z) / rd.z;
    vec3 pp = ro + rd * tp;
    float sd = sdBox(pp.xy - PANEL_C.xy, PANEL_H.xy);
    if (sd < uPx * 1.5) {
        // an even, calm ground behind the blocks: no shadow, no gradient
        // in the panel's own color at one steady level, whatever the lamps and the gels do
        float inside = 1.0 - smoothstep(-uPx * 0.5, uPx * 0.5, sd);
        col = mix(col, uPanel, 0.8 * inside); // the room shows faintly through it
        float line = 1.0 - smoothstep(0.0, uPx * 1.2, abs(sd));
        col *= 1.0 - 0.22 * line;
    }
    col = mix(col, uBridgeCol, uBridge);
    frag = vec4(col, 1.0);
}
)";

// The room (shaded once per pixel) into the multisampled scene, with its depth.
inline const char* COPY_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uRoom, uRoomZ;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    frag = texelFetch(uRoom, p, 0);
    gl_FragDepth = texelFetch(uRoomZ, p, 0).r;
}
)";

// ---------------------------------------------------------------- the cubes

// One cube mesh (corners at +-1, flat normals), instanced. a0: center, half size. a1: x axis, alpha. a2: y axis,
// plaster. a3: albedo, kind (0 a block of the well, 1 thrown out, 2 a preview).
inline const char* CUBE_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec4 a0;
layout(location = 3) in vec4 a1;
layout(location = 4) in vec4 a2;
layout(location = 5) in vec4 a3;
uniform mat4 uVP;
out vec3 vWorld, vLocal, vN;
flat out vec3 vX, vY, vZ;
flat out vec4 vC, vF;
void main() {
    vec3 X = a1.xyz, Y = a2.xyz, Z = cross(X, Y);
    vec3 w = a0.xyz + (X * aPos.x + Y * aPos.y + Z * aPos.z) * a0.w;
    vWorld = w;
    vLocal = aPos;
    vN = X * aNrm.x + Y * aNrm.y + Z * aNrm.z;
    vX = X; vY = Y; vZ = Z;
    vC = vec4(a3.rgb, a1.w);
    vF = vec4(a2.w, a3.w, a0.w, 0.0);
    gl_Position = uVP * vec4(w, 1.0);
}
)";

inline const char* CUBE_FS = R"(#version 330 core
in vec3 vWorld, vLocal, vN;
flat in vec3 vX, vY, vZ;
flat in vec4 vC, vF;
out vec4 frag;
uniform vec3 uEye, uPanel, uPlaster, uBridgeInk, uBridgeCol;
uniform float uBridgeK, uBridgeRoom;

// A matte cube (soft plastic, unglazed ceramic): flat faces, edges softly rounded that turn toward the lights, a
// fine crease where it meets its neighbours, a faint broad sheen; lit like the room, light bounced off the white
// room in its shade (never a black face).
void main() {
    int kind = int(vF.y + 0.5);
    // the pane hides the thrown cubes behind it (nothing is drawn over the well) and veils the blocks' backs
    float tp = (PANE_Z - uEye.z) / (vWorld.z - uEye.z);
    vec2 pp = uEye.xy + (vWorld.xy - uEye.xy) * tp;
    float overPane = sdBox(pp - PANEL_C.xy, PANEL_H.xy) < 0.0 ? 1.0 : 0.0;
    float behind = vWorld.z < PANE_Z ? overPane : 0.0;
    if (kind == 1 && overPane > 0.5) discard;
    vec3 q = clamp(vLocal, -1.0, 1.0);
    vec3 aq = abs(q);
    // the face: its own axis is 1, the others ramp up to it across the bevel
    float B = kind == 1 ? 0.2 : 0.24;
    vec3 t = smoothstep(1.0 - B, 1.0, aq);
    vec3 fn = abs(vLocal);
    float face = fn.x > fn.y && fn.x > fn.z ? 0.0 : (fn.y > fn.z ? 1.0 : 2.0);
    vec3 tf = t;
    if (face == 0.0) tf.x = 1.0; else if (face == 1.0) tf.y = 1.0; else tf.z = 1.0;
    vec3 nl = sign(q) * tf * vec3(face == 0.0 ? 1.0 : 0.75, face == 1.0 ? 1.0 : 0.75, face == 2.0 ? 1.0 : 0.75);
    vec3 n = normalize(vX * nl.x + vY * nl.y + vZ * nl.z);
    vec3 fN = normalize(vN);
    // the crease: the outermost rim of the bevel, a little darker (neighbours read apart)
    vec3 te = smoothstep(1.0 - B * 0.4, 1.0, aq);
    if (face == 0.0) te.x = 0.0; else if (face == 1.0) te.y = 0.0; else te.z = 0.0;
    float crease = max(te.x, max(te.y, te.z));
    vec3 alb = mix(vC.rgb, uPlaster, vF.x);
    vec3 col = light(vWorld + fN * 0.004, n, alb, pileAO(vWorld) * (0.85 + 0.15 * (n.y * 0.5 + 0.5)), kind == 1);
    vec3 rd = normalize(vWorld - uEye);
    vec3 h = normalize(dirTo(0, vWorld) - rd);
    col += uC0 * pow(max(dot(n, h), 0.0), 18.0) * 0.06 * lampFall(0, vWorld);
    col += alb * (0.1 + 0.06 * n.y); // light bounced off the room
    col *= 1.0 - 0.4 * crease;
    if (kind != 1) {
        // a block of the well (or a preview): its sides in its own shade toward the back
        float back = clamp(-vWorld.z / (2.0 * vF.z), 0.0, 1.0);
        col *= 1.0 - 0.3 * back * (1.0 - abs(fN.z));
        col = mix(col, uPanel, 0.8 * behind); // seen through the pane
        col = mix(col, uBridgeInk, uBridgeK);
    } else {
        col = mix(col, uBridgeCol, uBridgeRoom);
    }
    float a = vC.a;
    frag = vec4(col * a, a);
}
)";

// the thrown cubes seen from a lamp (their depth); a fading cube casts a dithered (thinning) shadow
inline const char* SHADOW_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec4 a0;
layout(location = 3) in vec4 a1;
layout(location = 4) in vec4 a2;
uniform mat4 uVP;
flat out float vA;
void main() {
    vec3 X = a1.xyz, Y = a2.xyz, Z = cross(X, Y);
    vA = a1.w;
    gl_Position = uVP * vec4(a0.xyz + (X * aPos.x + Y * aPos.y + Z * aPos.z) * a0.w, 1.0);
}
)";
inline const char* SHADOW_FS = R"(#version 330 core
flat in float vA;
out vec4 frag;
void main() {
    if (vA < 0.999 && hash12(gl_FragCoord.xy) > vA) discard;
    frag = vec4(1.0);
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

// ---------------------------------------------------------------- tiles: the ghost, the gauge

// a0: center xy, half size xy (board). a1: rgb, alpha. a2: kind, unused x3. a3: extra.
inline const char* TILE_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 a0;
layout(location = 2) in vec4 a1;
layout(location = 3) in vec4 a2;
layout(location = 4) in vec4 a3;
uniform mat4 uVP;
out vec2 vLocal;
flat out vec4 v0, v1, v2, v3;
void main() {
    vec2 l = aCorner * a0.zw;
    vec2 p = a0.xy + l;
    vLocal = l;
    v0 = a0; v1 = a1; v2 = a2; v3 = a3;
    // the ghost sits a hair behind the blocks' front faces (the falling piece hides it where they meet)
    float z = int(a2.x + 0.5) == 1 ? -0.03 : 0.0;
    gl_Position = uVP * vec4(p, z, 1.0);
}
)";
inline const char* TILE_FS = R"(#version 330 core
in vec2 vLocal;
flat in vec4 v0, v1, v2, v3;
out vec4 frag;
uniform float uPx, uBridge, uTime;
uniform vec3 uInk, uGroove, uAccent, uBridgeInk;

void main() {
    int kind = int(v2.x + 0.5);
    vec3 pc = v1.rgb;
    float alpha = v1.a;
    vec2 p = vLocal;
    float px = uPx;
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
