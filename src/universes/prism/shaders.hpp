#pragma once
// PRISM's shaders (see prism.cpp).
//
// Passes, all in board units on a flat orthographic view: the light (the beams traced on the CPU, drawn as soft
// additive shafts into a half-resolution HDR buffer with mipmaps), the scene (an HDR buffer: the dark backdrop and
// floor lit by that buffer, haze and dust, then the glass cubes over it, then the glints and sparks), and the final
// pass (a soft shoulder, sRGB, dim and fade) before the HUD.

namespace prism_shaders {

inline const char* COMMON_GLSL = R"(
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec2 hash22(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973)); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.xx + p3.yz) * p3.zy); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
float fbm3(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) { s += a * vnoise(p); p = p * 2.07 + vec2(17.1, 9.7); a *= 0.5; }
    return s / 0.875;
}
)";

// ---------------------------------------------------------------- light primitives (instanced quads, additive)
//   a0: a xy, b xy        a1: w0, w1, i0, i1        a2: color rgb, kind        a3: extra
// kind 0: a shaft from a to b (a soft band, its width w0 -> w1 and its strength i0 -> i1 along it)
// kind 1: a pool of light (an ellipse at a, radii b)
// kind 2: a glint (a star at a, size w0: a hot core, a soft halo and four thin rays, the horizontal ones longer)
// kind 3: a spark (a short crisp streak from a to b)
// kind 4: a ring (the bonus gauge: center a, radius b.x, thickness b.y, fill i0, ready i1)
inline const char* LIGHT_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aA0;
layout(location = 2) in vec4 aA1;
layout(location = 3) in vec4 aA2;
layout(location = 4) in vec4 aA3;
uniform mat4 uVP;
out vec2 vP;
flat out vec4 vA0, vA1, vA2, vA3;
void main() {
    vA0 = aA0; vA1 = aA1; vA2 = aA2; vA3 = aA3;
    vec2 a = aA0.xy, b = aA0.zw, w;
    float kind = aA2.w;
    if (kind < 0.5 || (kind > 2.5 && kind < 3.5)) {
        vec2 d = b - a;
        float L = length(d);
        vec2 u = L > 1e-5 ? d / L : vec2(1.0, 0.0), v = vec2(-u.y, u.x);
        float e = max(aA1.x, aA1.y) * (kind < 0.5 ? 2.0 : 1.6);
        w = (a + b) * 0.5 + u * aCorner.x * (L * 0.5 + e) + v * aCorner.y * e;
    } else if (kind < 1.5) {
        w = a + aCorner * b * 1.8;
    } else if (kind < 2.5) {
        w = a + aCorner * aA1.x;
    } else {
        w = a + aCorner * (b.x + b.y * 3.0);
    }
    vP = w;
    gl_Position = uVP * vec4(w, 0.0, 1.0);
}
)";
inline const char* LIGHT_FS = R"(#version 330 core
in vec2 vP;
flat in vec4 vA0, vA1, vA2, vA3;
out vec4 frag;
uniform float uPx;
void main() {
    vec2 a = vA0.xy, b = vA0.zw;
    float kind = vA2.w;
    float g = 0.0;
    if (kind < 0.5 || (kind > 2.5 && kind < 3.5)) {
        vec2 ab = b - a;
        float t = clamp(dot(vP - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
        float w = max(mix(vA1.x, vA1.y, t), uPx * 0.7);
        float d = length(vP - (a + ab * t));
        float I = mix(vA1.z, vA1.w, t);
        if (kind < 0.5) g = exp(-d * d / (w * w) * 2.0) * I;
        else g = (exp(-d * d / (w * w) * 5.0) + 0.25 * exp(-d * d / (w * w) * 0.8)) * I;
    } else if (kind < 1.5) {
        vec2 q = (vP - a) / b;
        g = exp(-dot(q, q) * 2.2) * vA1.z;
    } else if (kind < 2.5) {
        float s = vA1.x;
        vec2 q = (vP - a) / s;
        float r2 = dot(q, q);
        float th = max(uPx / s, 0.004);
        float rays = exp(-abs(q.y) / (th * 1.4)) * pow(max(0.0, 1.0 - abs(q.x)), 3.0)
                   + 0.55 * exp(-abs(q.x) / (th * 1.4)) * pow(max(0.0, 1.0 - abs(q.y) * 1.6), 3.0);
        g = (exp(-r2 * 90.0) * 1.6 + exp(-r2 * 14.0) * 0.35 + rays * 0.7) * vA1.z;
    } else {
        // the gauge: a thin ring, filled clockwise from the top
        vec2 q = vP - a;
        float R = b.x, th = b.y;
        float d = abs(length(q) - R);
        float ring = 1.0 - smoothstep(th * 0.5 - uPx, th * 0.5 + uPx, d);
        float ang = atan(q.x, q.y);
        float f = ang < 0.0 ? ang + 6.2831853 : ang;
        float filled = step(f, vA1.z * 6.2831853);
        g = ring * mix(0.12, 1.0, max(filled, vA1.w)) + vA1.w * exp(-d * d / (th * th) * 0.4) * 0.25;
    }
    frag = vec4(vA2.rgb * g, 0.0);
}
)";

// ---------------------------------------------------------------- the scene: backdrop, floor, haze, dust, light
inline const char* SCENE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uLight;
uniform vec4 uView;        // center xy, half extents xy (board units)
uniform float uPx;
uniform float uTime;       // the world's clock (slowed in slow motion)
uniform vec3 uBack, uFloor, uHair;
uniform float uFloorY;
uniform float uDust;       // dust motes in the beams
uniform float uHaze;       // how much the haze breaks the beams up
uniform float uSpill;      // light scattered on the backdrop
uniform vec4 uColL, uColR; // the HUD's columns: the beams quieter there
uniform float uBridge;
uniform vec3 uBridgeColor;
vec3 light(vec2 uv, float lod) { return textureLod(uLight, clamp(uv, vec2(0.001), vec2(0.999)), lod).rgb; }
void main() {
    vec2 uv = vUv;
    vec2 p = uView.xy + (uv * 2.0 - 1.0) * uView.zw;
    // the backdrop: near black, a breath of depth (a little lighter low behind the well, darker to the corners)
    float r = length((p - vec2(0.0, -6.0)) / vec2(26.0, 18.0));
    vec3 col = uBack * (1.25 - 0.55 * smoothstep(0.0, 1.0, r));
    // the haze the beams cross: slow drifting banks, so a shaft is uneven along its length
    float h = fbm3(p * vec2(0.16, 0.22) + vec2(uTime * 0.045, -uTime * 0.018));
    float h2 = vnoise(p * 0.6 + vec2(-uTime * 0.07, uTime * 0.03));
    float haze = mix(1.0, 0.25 + 1.35 * h * (0.75 + 0.5 * h2), uHaze);
    // softly, without an edge
    float inL = smoothstep(uColL.x - 1.0, uColL.x + 1.0, p.x) * (1.0 - smoothstep(uColL.z - 1.0, uColL.z + 1.0, p.x))
              * smoothstep(uColL.y - 1.0, uColL.y + 1.0, p.y) * (1.0 - smoothstep(uColL.w - 1.0, uColL.w + 1.0, p.y));
    float inR = smoothstep(uColR.x - 1.0, uColR.x + 1.0, p.x) * (1.0 - smoothstep(uColR.z - 1.0, uColR.z + 1.0, p.x))
              * smoothstep(uColR.y - 1.0, uColR.y + 1.0, p.y) * (1.0 - smoothstep(uColR.w - 1.0, uColR.w + 1.0, p.y));
    float hud = 1.0 - 0.5 * max(inL, inR);
    vec3 L = light(uv, 0.0);
    vec3 spill = light(uv, 4.5) + 0.6 * light(uv, 6.0);
    float floorY = uFloorY;
    if (p.y < floorY) {
        // a dark, polished floor: its own tone, the beams' pools (in the light buffer) and a dim reflection of the
        // light above, fading with the distance from its edge
        float k = floorY - p.y;
        col = uFloor * (0.8 + 0.4 * exp(-k * 0.8));
        vec2 mp = vec2(p.x, 2.0 * floorY - p.y);
        vec2 muv = (mp - uView.xy) / uView.zw * 0.5 + 0.5;
        vec3 refl = light(muv, 1.5 + k * 1.2);
        col += refl * 0.22 * exp(-k * 0.7) + L * 0.9 + spill * 0.18;
        col += uHair * 0.6 * exp(-k * k * 260.0); // the floor's front edge catches a hair of light
    } else {
        col += L * haze * hud + spill * uSpill;
    }
    // dust motes, glinting only where the light is
    float lum = dot(L + spill * 0.3, vec3(0.3, 0.5, 0.2));
    if (uDust > 0.0 && lum > 0.002) {
        float dsum = 0.0;
        for (int k = 0; k < 2; k++) {
            float sc = k == 0 ? 0.55 : 0.9;
            vec2 q = p / sc + vec2(uTime * (0.05 + 0.04 * float(k)), uTime * (0.03 - 0.05 * float(k)));
            vec2 c = floor(q);
            vec2 o = hash22(c + float(k) * 19.0);
            vec2 m = c + 0.2 + 0.6 * o + 0.15 * vec2(sin(uTime * 0.3 + o.x * 6.0), cos(uTime * 0.23 + o.y * 6.0));
            float d = length(q - m) * sc;
            float tw = 0.5 + 0.5 * sin(uTime * (0.8 + o.y) + o.x * 30.0);
            float sz = uPx * (0.9 + 0.8 * o.y);
            dsum += exp(-d * d / (sz * sz)) * (0.35 + 0.65 * tw) * step(0.35, o.x);
        }
        col += dsum * uDust * min(lum, 1.5) * 1.1 * vec3(1.0, 0.98, 0.95);
    }
    // the well: three hairlines of glass edge, almost invisible, catching the light that crosses them
    float hx = min(abs(p.x - 5.08), abs(p.x + 5.08));
    float wall = exp(-hx * hx / (uPx * uPx * 0.8)) * step(floorY, p.y) * (1.0 - smoothstep(9.6, 10.4, p.y));
    col += uHair * wall * (0.25 + lum * 2.5);
    col = mix(col, uBridgeColor, uBridge);
    frag = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- the glass cubes (instanced, premultiplied over)
//   a0: center xy, size, alpha       a1: tint rgb, kind (0 cube, 1 ghost, 2 the falling piece)
//   a2: light received rgb, flash    a3: light direction xy, seed, ambient
inline const char* CUBE_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aA0;
layout(location = 2) in vec4 aA1;
layout(location = 3) in vec4 aA2;
layout(location = 4) in vec4 aA3;
uniform mat4 uVP;
out vec2 vQ;
flat out vec4 vA0, vA1, vA2, vA3;
void main() {
    vA0 = aA0; vA1 = aA1; vA2 = aA2; vA3 = aA3;
    vQ = aCorner * 0.56;
    gl_Position = uVP * vec4(aA0.xy + vQ * aA0.z, 0.0, 1.0);
}
)";
inline const char* CUBE_FS = R"(#version 330 core
in vec2 vQ;
flat in vec4 vA0, vA1, vA2, vA3;
out vec4 frag;
uniform float uPx;
uniform float uTime;
uniform vec2 uKey;         // the faint ambient key light's direction (where it comes from)
uniform float uAmbient;    // the stack's ambient (readability in the dark)
uniform float uMul;
uniform float uBridge;
uniform vec3 uBridgeInk;
float sdBox(vec2 p, vec2 b, float r) { vec2 d = abs(p) - b + r; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r; }
void main() {
    float S = vA0.z;
    vec2 q = vQ;                 // cell units (-0.56 .. 0.56)
    float px = uPx / S;          // one pixel in cell units
    float hs = 0.47;
    float d = sdBox(q, vec2(hs), 0.03);
    float cov = 1.0 - smoothstep(-px * 0.5, px * 0.5, d);
    float alpha = vA0.w;
    vec3 tint = vA1.rgb;
    float kind = vA1.w;
    vec3 lit = vA2.rgb;
    float litL = dot(lit, vec3(0.3, 0.5, 0.2));
    // a soft knee: a cube in a strong beam glows, it never burns to white
    lit /= 1.0 + 0.6 * litL;
    litL /= 1.0 + 0.6 * litL;
    vec2 ld = vA3.xy;            // the light's direction through the cube (zero: none)
    float seed = vA3.z;
    float amb = vA3.w * uAmbient;
    if (kind > 0.5 && kind < 1.5) {
        // the ghost: a hairline outline of the cube, nothing inside
        float line = exp(-d * d / (px * px * 1.2));
        vec3 c = tint * line * 0.22 * alpha;
        frag = vec4(mix(c, uBridgeInk * line * 0.3 * alpha, uBridge) * uMul, 0.0);
        return;
    }
    if (cov <= 0.0) discard;
    // the bevel: a narrow chamfer all around the face, its normal turning outward
    float bw = 0.075;
    float bev = smoothstep(-bw, -bw + px * 1.5, d) ; // 1 on the chamfer
    vec2 e = abs(q) - vec2(hs - bw);
    // the chamfer's normal: toward its side, blended across the diagonal of a corner (no seam)
    float wx = smoothstep(-0.02, 0.02, e.x - e.y);
    vec2 n2 = normalize(mix(vec2(0.0, q.y >= 0.0 ? 1.0 : -1.0), vec2(q.x >= 0.0 ? 1.0 : -1.0, 0.0), wx));
    // ambient: a faint body of tinted glass (light gathering low in it), the chamfers catching a dim key light,
    // a hairline at the silhouette: every cube readable in the dark, none of them shouting
    float keyGlint = pow(max(dot(n2, uKey), 0.0), 2.0);
    float bevT = smoothstep(-bw, 0.0, d); // across the chamfer: 0 at the face, 1 at the silhouette
    vec3 col = tint * amb * (0.03 + 0.045 * smoothstep(0.45, -0.45, q.y)) * (1.0 - bev);
    col += tint * amb * bev * (0.1 + 0.4 * keyGlint) * (0.6 + 0.4 * bevT);
    float rim = exp(-d * d / (px * px * 1.0));
    col += tint * amb * rim * 0.32;
    float ridge = exp(-(d + bw) * (d + bw) / (px * px * 1.2));
    col += mix(tint, vec3(1.0), 0.5) * amb * ridge * 0.1 * (0.3 + keyGlint);
    // a broad, faint reflection across the face (the glass reads as glass)
    float sheen = smoothstep(0.55, -0.1, q.x + q.y * 0.9) * smoothstep(-0.9, -0.1, q.x + q.y * 0.9);
    col += vec3(0.85, 0.9, 1.0) * sheen * amb * 0.018 * (1.0 - bev);
    // the back edges seen through the glass (a smaller square, shifted): the cube's depth
    vec2 bo = vec2(0.07, 0.07);
    float db = abs(sdBox(q - bo, vec2(hs - 0.16), 0.02));
    float back = exp(-db * db / (px * px * 1.0)) * step(d, -bw);
    col += tint * back * amb * 0.07;
    // ---- where the light passes: the glass comes alive
    if (litL > 0.001) {
        vec2 ldn = length(ld) > 1e-4 ? normalize(ld) : vec2(0.0, -1.0);
        // inside: the light scatters in the body, brighter toward the side it came from
        float side = dot(q, -ldn);
        float body = 0.35 + 0.65 * smoothstep(-0.5, 0.5, side);
        col += lit * tint * body * 0.28 * (1.0 - bev * 0.5);
        // the chamfers facing the light blaze, those it leaves by glow in the glass's color
        float inF = pow(max(dot(n2, -ldn), 0.0), 3.0), outF = pow(max(dot(n2, ldn), 0.0), 2.0);
        col += bev * (lit * inF * 1.4 + lit * tint * outF * 0.9);
        col += rim * lit * (0.25 + 0.6 * inF);
        // the back edges catch it too
        col += back * lit * tint * 0.7;
        // caustics: a slow net of bright veins inside the body (two noise fields crossing)
        vec2 cq = q * 2.6 + vec2(seed * 17.0, seed * 5.0);
        float n1 = vnoise(cq + vec2(uTime * 0.11, -uTime * 0.07)), n2b = vnoise(cq * 1.37 + vec2(-uTime * 0.09, uTime * 0.05) + 3.7);
        float vein = pow(clamp(1.0 - abs(n1 - n2b) * 3.0, 0.0, 1.0), 3.0);
        col += lit * mix(tint, vec3(1.0), 0.5) * vein * 0.4 * (1.0 - bev);
    }
    col += vec3(1.0) * vA2.w * (0.3 + 0.7 * bev); // a lock's brief glint
    // premultiplied: the glass dims what is behind it a little (a tinted absorption), and adds its own light
    float a = (0.16 + 0.18 * bev) * cov * alpha;
    col *= cov * alpha;
    vec3 plain = uBridgeInk * cov * alpha * (0.55 + 0.45 * rim);
    col = mix(col, plain, uBridge);
    a = mix(a, cov * alpha * 0.9, uBridge);
    frag = vec4(col * uMul, a);
}
)";

// ---------------------------------------------------------------- the final pass
inline const char* FINAL_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uScene;
uniform float uMul;       // dim and fade
uniform float uExposure;
void main() {
    vec3 c = texture(uScene, vUv).rgb * uExposure;
    // a soft shoulder: linear in the darks and mids (the backdrop and the bridge's colors stay exact), the hot
    // cores of the light rolling off to white
    const float K = 0.55;
    vec3 over = max(c - K, 0.0);
    c = min(c, vec3(K)) + (1.0 - K) * (1.0 - exp(-over / (1.0 - K)));
    c *= uMul;
    c = pow(max(c, 0.0), vec3(1.0 / 2.2));
    c += (hash12(gl_FragCoord.xy) - 0.5) / 255.0; // dither (the dark gradients would band)
    frag = vec4(c, 1.0);
}
)";

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
uniform vec4 uInk;    // sRGB color, alpha
uniform float uBias;  // > 0: bolder
uniform float uSoft;  // > 0: a soft halo (the width of the edge)
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.75 + uSoft;
    float fill = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    frag = vec4(uInk.rgb * uInk.a * fill, uInk.a * fill);
}
)";
inline const char* PANEL_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform vec2 uRes;
void main() { gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0); }
)";
inline const char* PANEL_FS = R"(#version 330 core
out vec4 frag;
uniform vec4 uColor; // premultiplied
void main() { frag = uColor; }
)";

} // namespace prism_shaders
