#pragma once
// GLSL sources of the diorama renderer: one sun + sky ambient, soft warm shadows, clay materials, water with
// reflections, a painted sky, soft sprites, tilt-shift depth of field, and the HUD (SDF text, rounded cards).
// Every scene pass writes the view distance in alpha (used by the depth of field).

namespace diorama_shaders {

// Shared by every lit pass.
inline const char* COMMON = R"(
uniform vec3 uSunDir;      // toward the light (sun, or moon at night)
uniform vec3 uSkySunDir;   // where the sun disk / glow is painted in the sky
uniform vec3 uSunCol, uSkyCol, uGroundCol, uShadowTint, uFogCol, uSunFogCol, uCamPos, uZenith, uHorizon;
uniform float uFogDensity, uMist, uWaterY, uTime, uNight, uRim, uSunGlow, uStars, uCloud, uCloudTime;
uniform sampler2DShadow uShadow;
uniform mat4 uLightVP;

float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { s += a * vnoise(p); p = mat2(1.6, 1.2, -1.2, 1.6) * p; a *= 0.5; }
    return s;
}

const vec2 POISSON[12] = vec2[](vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
    vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893), vec2(0.507, 0.064),
    vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));

float shadowAt(vec3 wp, vec3 n) {
    vec4 lp = uLightVP * vec4(wp + n * 0.06, 1.0);
    vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
    if (c.x < 0.003 || c.y < 0.003 || c.x > 0.997 || c.y > 0.997 || c.z > 0.999) return 1.0;
    float a = hash12(gl_FragCoord.xy) * 6.2832;
    mat2 R = mat2(cos(a), sin(a), -sin(a), cos(a));
    vec2 ts = 2.2 / vec2(textureSize(uShadow, 0));
    float s = 0.0;
    for (int i = 0; i < 12; i++) s += texture(uShadow, vec3(c.xy + R * POISSON[i] * ts, c.z - 0.0009));
    return s / 12.0;
}

vec3 skyColor(vec3 d) {
    float y = max(d.y, 0.0);
    vec3 c = mix(uHorizon, uZenith, pow(y, 0.5));
    float sd = max(dot(d, uSkySunDir), 0.0);
    c += uSunFogCol * (0.45 * pow(sd, 5.0) + 0.5 * pow(sd, 40.0)) * uSunGlow;
    if (d.y < 0.0) c = mix(uHorizon, uHorizon * 0.9, min(1.0, -d.y * 3.0));
    return c;
}

vec3 fogColor(vec3 d) {
    return uFogCol + uSunFogCol * pow(max(dot(d, uSkySunDir), 0.0), 4.0) * 0.45 * uSunGlow;
}

vec3 applyFog(vec3 col, vec3 wp) {
    vec3 v = wp - uCamPos;
    float dist = length(v);
    vec3 d = v / dist;
    float f = 1.0 - exp(-max(dist - 75.0, 0.0) * uFogDensity);
    float mist = uMist * exp(-max(wp.y - uWaterY, 0.0) * 0.35) * (1.0 - exp(-max(dist - 70.0, 0.0) * 0.015));
    return mix(col, fogColor(d), clamp(max(f, mist), 0.0, 1.0));
}

// Clay: wrapped diffuse sun, hemispheric sky ambient, warm-tinted soft shadows (never black), a little
// subsurface warmth on the terminator, a broad faint specular and a rim of sunlight.
vec3 shade(vec3 albedo, vec3 n, vec3 wp, float ao, float sh) {
    vec3 v = normalize(uCamPos - wp);
    float ndl = dot(n, uSunDir);
    float diff = clamp((ndl + 0.2) / 1.2, 0.0, 1.0);
    // Gentle shadows: a little sun leaks into the cast shadows, contact shading is softened and the sky fills a
    // touch more (the clay keeps its depth without dark pools).
    sh = mix(0.27, 1.0, sh);
    ao = mix(1.0, ao, 0.72);
    float lit = diff * sh;
    vec3 hemi = mix(uGroundCol, uSkyCol, n.y * 0.5 + 0.5) * 1.07;
    vec3 amb = hemi * ao * mix(uShadowTint, vec3(1.0), sh * 0.7 + 0.3 * diff);
    vec3 col = albedo * (uSunCol * lit + amb);
    float term = smoothstep(-0.3, 0.1, ndl) * (1.0 - smoothstep(0.1, 0.55, ndl));
    col += albedo * albedo * uSunCol * term * 0.22;
    vec3 h = normalize(uSunDir + v);
    col += uSunCol * pow(max(dot(n, h), 0.0), 24.0) * 0.045 * sh;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * uRim * clamp(dot(n, uSunDir) + 0.4, 0.0, 1.0);
    col += uSunCol * albedo * rim * 0.6;
    return col;
}
)";

// ---------- Blocks (instanced: falling pieces, the stack, previews, tumbling bodies, gauge beads).
inline const char* BLOCK_VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec4 iPosKind;   // world position, kind (0 block, 1 ghost tile, 2 bead)
layout(location = 3) in vec4 iScaleMask; // scale, neighbour mask (1 left, 2 right, 4 up, 8 down)
layout(location = 4) in vec4 iQuat;
layout(location = 5) in vec4 iColGlow;   // albedo (linear), glow
uniform mat4 uVP, uMirror;
uniform float uClipY;
out vec3 vWP, vN, vLocal, vCol;
flat out float vKind, vMask, vGlow;
vec3 qrot(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }
void main() {
    vec3 wp = iPosKind.xyz + qrot(iQuat, aPos * iScaleMask.xyz);
    vWP = wp;
    vN = qrot(iQuat, aNrm / iScaleMask.xyz);
    vLocal = aPos;
    vCol = iColGlow.rgb;
    vKind = iPosKind.w;
    vMask = iScaleMask.w;
    vGlow = iColGlow.a;
    gl_Position = uVP * uMirror * vec4(wp, 1.0);
    gl_ClipDistance[0] = wp.y - uClipY;
}
)";

inline const char* BLOCK_FS = R"(
in vec3 vWP, vN, vLocal, vCol;
flat in float vKind, vMask, vGlow;
out vec4 frag;
uniform float uLanternGlow;
void main() {
#ifdef DEPTH_ONLY
    frag = vec4(1.0);
#else
    vec3 n = normalize(vN);
    vec3 alb = vCol;
    int m = int(vMask + 0.5);
    float ao = 1.0;
    // Crevices between touching blocks: the bevels facing a neighbour see less sky.
    if ((m & 1) != 0) ao -= 0.45 * smoothstep(0.0, 0.9, -n.x);
    if ((m & 2) != 0) ao -= 0.45 * smoothstep(0.0, 0.9, n.x);
    if ((m & 4) != 0) ao -= 0.45 * smoothstep(0.0, 0.9, n.y);
    if ((m & 8) != 0) ao -= 0.45 * smoothstep(0.0, 0.9, -n.y);
    ao = max(ao, 0.45);
    // Painted clay: a faint mottling, slightly lighter top faces.
    float g = vnoise(vLocal.xy * 7.0 + vLocal.z * 5.0 + vCol.rg * 31.0);
    alb *= 0.95 + 0.08 * g;
    alb *= 1.0 + 0.05 * max(n.y, 0.0);
    // Local face coordinates (for the lantern glow and the ghost tile's rim).
    vec3 an = abs(vLocal);
    vec2 f = an.x > an.y && an.x > an.z ? vLocal.yz : (an.y > an.z ? vLocal.xz : vLocal.xy);
    float edge = max(abs(f.x), abs(f.y)) * 2.0; // 0 center .. 1 edge
    if (vKind > 0.5 && vKind < 1.5) {
        // Ghost: a tile pressed on the back wall, with a lighter raised rim.
        float rim = smoothstep(0.62, 0.8, max(abs(vLocal.x), abs(vLocal.y)) * 2.0 * 0.9);
        alb = mix(alb, vec3(1.0, 0.97, 0.92) * 0.95, rim * 0.55);
    }
    float sh = shadowAt(vWP, n);
    vec3 col = shade(alb, n, vWP, ao, sh);
    // Lanterns: at night (and for the lit gauge beads) the blocks glow from inside, through their walls.
    float glow = vGlow + uLanternGlow * (vKind < 0.5 ? 1.0 : 0.0);
    if (glow > 0.0) {
        float inner = 1.0 - smoothstep(0.15, 1.0, edge);
        vec3 lc = mix(vCol, vec3(1.0, 0.82, 0.55), 0.25) * (0.35 + 1.1 * inner);
        col += lc * glow;
    }
    col = applyFog(col, vWP);
    frag = vec4(col, length(vWP - uCamPos));
#endif
}
)";

// ---------- Static world (island, tower, steles, trees, rocks, distant hills).
inline const char* WORLD_VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec3 aMat; // palette slot, shade, sway weight
uniform mat4 uVP, uMirror, uModel;
uniform float uClipY, uWind, uWindTime;
out vec3 vWP, vN;
flat out float vSlot;
out float vShade;
void main() {
    vec3 wp = (uModel * vec4(aPos, 1.0)).xyz;
    vec3 nrm = mat3(uModel) * aNrm;
    if (aMat.z > 0.0) {
        // Wind: trees lean and sway, more with the music's energy and on the beat.
        float ph = uWindTime + wp.x * 0.23 + wp.z * 0.17;
        vec2 o = vec2(sin(ph * 1.3) + 0.4 * sin(ph * 3.1 + 1.7), 0.5 * cos(ph * 1.1));
        wp.xz += o * aMat.z * uWind * 0.35;
        wp.x += aMat.z * uWind * 0.25;
    }
    vWP = wp;
    vN = nrm;
    vSlot = aMat.x;
    vShade = aMat.y;
    gl_Position = uVP * uMirror * vec4(wp, 1.0);
    gl_ClipDistance[0] = wp.y - uClipY;
}
)";

inline const char* WORLD_FS = R"(
in vec3 vWP, vN;
flat in float vSlot;
in float vShade;
out vec4 frag;
uniform vec3 uPal[16];
uniform sampler2D uOcc;
uniform float uLanternGlow, uEmissive;
void main() {
#ifdef DEPTH_ONLY
    frag = vec4(1.0);
#else
    vec3 n = normalize(vN);
    int slot = int(vSlot + 0.5);
    vec3 alb = uPal[slot] * vShade;
    float ao = 1.0;
    if (slot == 11 && n.z > 0.5 && abs(vWP.x) < 5.0 && abs(vWP.y) < 10.0) {
        // The back wall of the well: contact shading from the stack and faint engraved cell lines.
        float occ = texture(uOcc, vec2((vWP.x + 6.0) / 12.0, (vWP.y + 11.0) / 22.0)).r;
        ao *= 1.0 - 0.38 * occ;
        vec2 cl = abs(fract(vec2(vWP.x, vWP.y)) - 0.5);
        float line = smoothstep(0.47, 0.495, max(cl.x, cl.y));
        alb *= 1.0 - 0.05 * line;
    }
    if (slot <= 4) alb *= 0.94 + 0.12 * vnoise(vWP.xz * 0.9); // mottled ground
    float sh = shadowAt(vWP, n);
    vec3 col = shade(alb, n, vWP, ao, sh);
    if (slot == 15) col += uPal[15] * (0.15 + 2.2 * uLanternGlow); // lanterns
    col += alb * uEmissive;
    col = applyFog(col, vWP);
    frag = vec4(col, length(vWP - uCamPos));
#endif
}
)";

// ---------- Sky: painted gradient, sun (or moon), soft clouds, stars at night.
inline const char* SKY_VS = R"(
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 0.9999, 1.0);
}
)";
inline const char* SKY_FS = R"(
in vec2 vNdc;
out vec4 frag;
uniform mat4 uInvVP;
uniform float uMirrored;
void main() {
    vec4 a = uInvVP * vec4(vNdc, -1.0, 1.0), b = uInvVP * vec4(vNdc, 1.0, 1.0);
    vec3 d = normalize(b.xyz / b.w - a.xyz / a.w);
    if (uMirrored > 0.5) d.y = -d.y;
    vec3 col = skyColor(d);
    // Sun / moon disk.
    float sd = dot(d, uSkySunDir);
    float disk = smoothstep(0.99935, 0.9996, sd);
    vec3 diskCol = mix(uSunFogCol * 3.0 + vec3(0.6), vec3(0.85, 0.9, 1.0) * 1.4, uNight);
    col = mix(col, diskCol, disk * smoothstep(-0.02, 0.01, d.y));
    // Stars.
    if (uStars > 0.0 && d.y > 0.0) {
        vec2 sp = d.xz / (d.y + 0.35) * 160.0;
        vec2 cell = floor(sp);
        float h = hash12(cell);
        vec2 off = vec2(hash12(cell + 7.1), hash12(cell + 3.3)) - 0.5;
        float st = smoothstep(0.24, 0.0, length(fract(sp) - 0.5 - off * 0.6)) * step(0.93, h);
        st *= 0.6 + 0.4 * sin(uTime * (1.0 + 3.0 * h) + h * 40.0);
        col += vec3(0.9, 0.92, 1.0) * st * uStars * smoothstep(0.0, 0.25, d.y);
    }
    // Clouds: soft painted puffs on a high plane, lit from the sun side.
    if (d.y > 0.0 && uCloud > 0.0) {
        vec2 uv = d.xz / (d.y + 0.12) * 1.6 + vec2(uCloudTime * 0.02, 0.0);
        float n = fbm(uv * 1.1);
        float cov = mix(0.75, 0.42, uCloud);
        float c = smoothstep(cov, cov + 0.22, n);
        float lit = clamp(0.55 + 0.9 * (fbm(uv * 1.1 + uSkySunDir.xz * 0.12) - n) * 4.0, 0.0, 1.0);
        vec3 cc = mix(mix(uHorizon, uZenith, 0.35) * 0.95 + uShadowTint * 0.05, uSunFogCol * 0.6 + uHorizon * 0.9, lit);
        cc = mix(cc, vec3(1.0), 0.15 * (1.0 - uNight));
        c *= smoothstep(0.0, 0.18, d.y) * 0.85;
        col = mix(col, cc, c);
    }
    frag = vec4(col, 2000.0);
}
)";

// ---------- Water: gentle waves, shore foam, ripple rings, mirrored reflections, the sun's glint path.
inline const char* WATER_VS = R"(
layout(location = 0) in vec3 aPos;
uniform mat4 uVP;
out vec3 vWP;
void main() { vWP = aPos; gl_Position = uVP * vec4(aPos, 1.0); }
)";
inline const char* WATER_FS = R"(
in vec3 vWP;
out vec4 frag;
uniform sampler2D uRefl;
uniform vec2 uRes;
uniform vec4 uRipples[16]; // x, z, age, strength
uniform vec4 uRings[6];    // age, strength
uniform vec3 uShallow, uDeep;
uniform vec4 uIsland;      // center x, z, radius x, z
uniform float uWave, uWaterTime, uGlint;
float islandDist(vec2 p) {
    vec2 q = (p - uIsland.xy) / uIsland.zw;
    return (length(q) - 1.0) * 0.5 * (uIsland.z + uIsland.w);
}
float height(vec2 p) {
    float t = uWaterTime;
    float h = uWave * (0.10 * sin(dot(p, vec2(0.31, 0.12)) + t * 1.1) + 0.07 * sin(dot(p, vec2(-0.17, 0.41)) + t * 1.4)
                       + 0.05 * sin(dot(p, vec2(0.63, -0.52)) + t * 2.1) + 0.06 * (vnoise(p * 0.9 + t * 0.3) - 0.5));
    float di = islandDist(p);
    for (int i = 0; i < 6; i++) {
        if (uRings[i].y <= 0.0) continue;
        float r = di - uRings[i].x * 5.5;
        h += uRings[i].y * 0.12 * exp(-r * r * 0.5) * sin(r * 2.6) * exp(-uRings[i].x * 0.55);
    }
    for (int i = 0; i < 16; i++) {
        if (uRipples[i].w <= 0.0) continue;
        float r = length(p - uRipples[i].xy) - uRipples[i].z * 3.2;
        h += uRipples[i].w * 0.1 * exp(-r * r * 1.5) * sin(r * 5.0) * exp(-uRipples[i].z * 1.1);
    }
    return h;
}
void main() {
    vec2 p = vWP.xz;
    float e = 0.08;
    float h0 = height(p);
    vec3 n = normalize(vec3(-(height(p + vec2(e, 0)) - h0) / e, 1.0, -(height(p + vec2(0, e)) - h0) / e));
    vec3 v = normalize(uCamPos - vWP);
    float dist = length(uCamPos - vWP);
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    vec2 suv = gl_FragCoord.xy / uRes + n.xz * vec2(0.25, 0.6) * 0.12 / (1.0 + dist * 0.02);
    vec3 refl = texture(uRefl, clamp(suv, 0.001, 0.999)).rgb;
    float di = islandDist(p);
    vec3 body = mix(uShallow, uDeep, smoothstep(-0.5, 9.0, di));
    body *= uSkyCol * 0.75 + uSunCol * 0.35 * max(uSunDir.y, 0.1);
    vec3 col = mix(body, refl, clamp(fres * 1.15, 0.22, 0.88));
    // Foam on the shore and on ring crests.
    float foam = smoothstep(0.55, 0.0, di + (vnoise(p * 2.0 + uWaterTime * 0.4) - 0.5) * 0.5 + 0.1 * sin(uWaterTime * 1.3));
    foam += 0.6 * smoothstep(0.02, 0.08, h0 - 0.05 * uWave) * smoothstep(0.0, 4.0, di);
    vec3 foamCol = (uSkyCol * 0.8 + uSunCol * 0.6) * vec3(1.0, 0.98, 0.95);
    col = mix(col, foamCol, clamp(foam, 0.0, 1.0) * 0.75);
    // Sun glint path toward the painted sun.
    vec3 r = reflect(-v, n);
    float g = pow(max(dot(r, uSkySunDir), 0.0), 180.0) * 3.0 + pow(max(dot(r, uSkySunDir), 0.0), 18.0) * 0.12;
    col += uSunFogCol * g * uGlint;
    col = applyFog(col, vWP);
    frag = vec4(col, dist);
}
)";

// ---------- Soft sprites: dust, petals, confetti, droplets, fireflies, birds.
inline const char* SPRITE_VS = R"(
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 iPosSize;
layout(location = 2) in vec4 iCol;
layout(location = 3) in vec4 iParams; // kind, rotation, glow, aspect
uniform mat4 uVP;
uniform vec3 uRight, uUp;
out vec2 vUV;
out vec4 vCol;
flat out vec4 vParams;
out vec3 vWP;
void main() {
    float c = cos(iParams.y), s = sin(iParams.y);
    vec2 q = vec2(aCorner.x * c - aCorner.y * s, aCorner.x * s + aCorner.y * c);
    vec3 wp = iPosSize.xyz + (uRight * q.x + uUp * q.y) * iPosSize.w;
    vUV = aCorner;
    vCol = iCol;
    vParams = iParams;
    vWP = iPosSize.xyz;
    gl_Position = uVP * vec4(wp, 1.0);
}
)";
inline const char* SPRITE_FS = R"(
in vec2 vUV;
in vec4 vCol;
flat in vec4 vParams;
in vec3 vWP;
out vec4 frag;
void main() {
    int kind = int(vParams.x + 0.5);
    vec2 p = vUV;
    float r = length(p);
    float a = 0.0;
    vec3 c = vCol.rgb;
    vec3 lightC = uSunCol * 0.55 + uSkyCol * 0.9;
    if (kind == 0) {        // dust puff
        a = smoothstep(1.0, 0.1, r);
        a *= a;
        c *= lightC;
    } else if (kind == 1) { // petal: a leaf-like ellipse, two-tone
        vec2 q = vec2(p.x, p.y / max(vParams.w, 0.2));
        a = smoothstep(1.0, 0.8, length(q)) * smoothstep(-1.0, -0.6, p.y);
        c *= lightC * (0.85 + 0.25 * p.x);
    } else if (kind == 2) { // confetti: a small rectangle that flickers as it tumbles
        a = step(abs(p.x), 0.9) * step(abs(p.y), 0.9 * abs(vParams.w));
        c *= lightC * (0.8 + 0.3 * abs(vParams.w));
    } else if (kind == 3) { // glow (fireflies, lanterns' halos)
        float g = exp(-r * r * 5.0);
        frag = vec4(c * g * vCol.a, 0.0);
        return;
    } else if (kind == 4) { // bird: a soft V that flaps
        float w = vParams.w; // wing lift
        vec2 q = vec2(abs(p.x), p.y);
        float y = w * q.x * 0.9 - 0.15 * q.x * q.x;
        float dline = abs(q.y - y);
        a = smoothstep(0.16, 0.06, dline) * smoothstep(1.0, 0.8, q.x);
        c = applyFog(c * (uSkyCol * 0.5 + uSunCol * 0.2), vWP);
    } else {                // droplet
        a = smoothstep(1.0, 0.6, r);
        c = c * lightC + vec3(0.4) * smoothstep(0.5, 0.0, length(p - vec2(-0.3, 0.3)));
    }
    a *= vCol.a;
    if (a < 0.003) discard;
    frag = vec4(c * a, a);
}
)";

// ---------- Post: downsample, blur, composite.
inline const char* POST_VS = R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
inline const char* DOWN_FS = R"(#version 330 core
in vec2 vUV;
out vec4 frag;
uniform sampler2D uSrc;
uniform vec2 uTexel;
void main() {
    vec4 s = texture(uSrc, vUV + uTexel * vec2(-0.5, -0.5)) + texture(uSrc, vUV + uTexel * vec2(0.5, -0.5)) +
             texture(uSrc, vUV + uTexel * vec2(-0.5, 0.5)) + texture(uSrc, vUV + uTexel * vec2(0.5, 0.5));
    frag = s * 0.25;
}
)";
inline const char* BLUR_FS = R"(#version 330 core
in vec2 vUV;
out vec4 frag;
uniform sampler2D uSrc;
uniform vec2 uDir;
void main() {
    const float w[5] = float[](0.227, 0.1945, 0.1216, 0.054, 0.0162);
    vec4 s = texture(uSrc, vUV) * w[0];
    for (int i = 1; i < 5; i++) s += (texture(uSrc, vUV + uDir * float(i)) + texture(uSrc, vUV - uDir * float(i))) * w[i];
    frag = s;
}
)";
inline const char* COMP_FS = R"(#version 330 core
in vec2 vUV;
out vec4 frag;
uniform sampler2D uScene, uBlur;
uniform float uFocus, uDof, uExposure, uFade, uDim, uSat, uTime, uAspect, uVignette, uGlowAmt;
uniform vec2 uSunUV;
uniform vec3 uGlowCol, uLift, uGain;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
vec3 tonemap(vec3 x) {
    // A soft filmic curve: gentle shoulder, no crushed shadows (keeps the pastels airy).
    x = max(x, 0.0);
    vec3 a = x * (2.51 * x + 0.03), b = x * (2.43 * x + 0.59) + 0.14;
    return mix(clamp(a / b, 0.0, 1.0), x / (1.0 + x), 0.15);
}
void main() {
    vec4 s = texture(uScene, vUV);
    vec4 b = texture(uBlur, vUV);
    float d = s.a;
    // Tilt-shift: the board's plane is sharp; the far landscape and the near water soften, and so do the
    // very top and bottom of the frame.
    float far = smoothstep(uFocus * 1.25, uFocus * 3.2, d);
    float near = smoothstep(uFocus * 0.86, uFocus * 0.6, d);
    float tilt = smoothstep(0.30, 0.5, abs(vUV.y - 0.47));
    float coc = clamp(max(max(far, near), tilt * 0.8) * uDof, 0.0, 1.0);
    vec3 col = mix(s.rgb, b.rgb, coc);
    // Soft glow around the painted sun (golden hour, Tetris flares).
    vec2 sd = (vUV - uSunUV) * vec2(uAspect, 1.0);
    float r2 = dot(sd, sd);
    col += uGlowCol * uGlowAmt * (exp(-r2 * 14.0) * 0.7 + exp(-r2 * 2.5) * 0.3);
    col *= uExposure;
    col = tonemap(col);
    // Grade: lift the shadows toward the scene's tint (airy, never black).
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.3); // a gentle S-curve: depth without losing the pastels
    col = col * uGain + uLift * (1.0 - col);
    float l = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(l), col, uSat);
    vec2 q = vUV - 0.5;
    col *= 1.0 - uVignette * smoothstep(0.35, 0.95, length(q * vec2(uAspect * 0.75, 1.0)));
    col = mix(col, col * 0.55 + vec3(l) * 0.05, uDim);
    col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));
    col += (hash(vUV * 1000.0 + fract(uTime)) - 0.5) * 0.012;
    frag = vec4(col * uFade, 1.0);
}
)";

// ---------- HUD: small wooden plaques with a soft shadow, SDF text.
inline const char* HUD_VS = R"(#version 330 core
layout(location = 0) in vec4 aPosUV; // pixel position, uv / local
uniform vec2 uRes;
out vec2 vUV;
void main() {
    vUV = aPosUV.zw;
    gl_Position = vec4(aPosUV.x / uRes.x * 2.0 - 1.0, 1.0 - aPosUV.y / uRes.y * 2.0, 0.0, 1.0);
}
)";
inline const char* PANEL_FS = R"(#version 330 core
in vec2 vUV; // pixels relative to the card's center
out vec4 frag;
uniform vec2 uHalf;
uniform float uRadius, uShadow, uAlpha, uWood;
uniform vec3 uColor, uShadowCol;
float box(vec2 p, vec2 b, float r) { vec2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }
void main() {
    float d = box(vUV, uHalf, uRadius);
    float a = clamp(0.5 - d, 0.0, 1.0);
    float ds = box(vUV - vec2(0.0, uShadow * 0.45), uHalf, uRadius);
    float sa = uShadow > 0.0 ? (1.0 - smoothstep(-uShadow * 0.6, uShadow, ds)) * 0.35 : 0.0;
    // A whisper of paint: the top of the card is a touch lighter.
    vec3 c = uColor * (1.0 + 0.04 * clamp(-vUV.y / max(uHalf.y, 1.0), -1.0, 1.0));
    if (uWood > 0.0) {
        // A small plaque of dark stained wood: lengthwise grain, a lighter bevel on top, a darker lip below.
        float g = sin(vUV.y * 0.9 + sin(vUV.x * 0.021 + vUV.y * 0.05) * 3.0) * 0.5 + 0.5;
        float g2 = sin(vUV.y * 2.3 + vUV.x * 0.013) * 0.5 + 0.5;
        c *= 0.9 + 0.12 * g + 0.06 * g2;
        float edge = smoothstep(-uWood * 2.2, -0.5, d);
        // the rim's outward direction: lit along the top, shaded along the bottom, neutral on the sides
        vec2 q = abs(vUV) - uHalf + uRadius, s = sign(vUV);
        vec2 nn = (q.x > 0.0 && q.y > 0.0) ? normalize(q) * s : (q.x > q.y ? vec2(s.x, 0.0) : vec2(0.0, s.y));
        float k = nn.y < 0.0 ? mix(1.0, 1.55, -nn.y) : mix(1.0, 0.7, nn.y);
        c = mix(c, c * k, edge * 0.8);
    }
    float outA = a + sa * (1.0 - a);
    vec3 outC = (c * a + uShadowCol * sa * (1.0 - a)) / max(outA, 1e-4);
    frag = vec4(outC, outA * uAlpha);
}
)";
inline const char* TEXT_FS = R"(#version 330 core
in vec2 vUV;
out vec4 frag;
uniform sampler2D uFont;
uniform vec3 uColor;
uniform float uAlpha, uSoft, uBias;
void main() {
    float d = texture(uFont, vUV).r;
    float w = max(fwidth(d), 1e-3) * 0.75 + uSoft;
    float a = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    frag = vec4(uColor, a * uAlpha);
}
)";

} // namespace diorama_shaders
