#pragma once
// VECTOR: every visible thing is a glowing beam segment on black glass.
// The accumulation buffer stores beam energy, not color: R = phosphor intensity, G = "hot" (overexposed, white)
// intensity. The previous frame decays into it (phosphor persistence) and the composite turns energy into the
// song's phosphor color, through a slightly curved CRT face.

namespace vector_shaders {

// One beam segment per instance. Endpoints are projected, then a screen-space quad is laid around the segment,
// wide enough for the halo. layer 0: world (masked behind the well and panels, depth tested against the terrain),
// 1: board (on top), 2: screen pixels (HUD, top-left origin).
inline const char* LINE_VS = R"(#version 330 core
layout(location = 0) in vec2 aCorner;   // x: 0 start .. 1 end, y: -1 .. 1 across
layout(location = 1) in vec4 iA;        // xyz, width (px)
layout(location = 2) in vec4 iB;        // xyz, layer
layout(location = 3) in vec4 iC;        // intensity at A, at B, hot at A, at B
uniform mat4 uVP;
uniform vec2 uRes;
uniform float uTime, uJitter;
out vec2 vP;
flat out vec2 vA, vB;
flat out float vW, vLayer;
flat out vec4 vC;
float hash(float n) { return fract(sin(n) * 43758.5453); }
void main() {
    float layer = iB.w;
    vec4 ca, cb;
    if (layer > 1.5) {
        ca = vec4(iA.x / uRes.x * 2.0 - 1.0, 1.0 - iA.y / uRes.y * 2.0, 0.0, 1.0);
        cb = vec4(iB.x / uRes.x * 2.0 - 1.0, 1.0 - iB.y / uRes.y * 2.0, 0.0, 1.0);
    } else {
        ca = uVP * vec4(iA.xyz, 1.0);
        cb = uVP * vec4(iB.xyz, 1.0);
    }
    const float NEAR_W = 0.2;
    if (ca.w < NEAR_W && cb.w < NEAR_W) { gl_Position = vec4(3.0, 3.0, 3.0, 1.0); return; }
    if (ca.w < NEAR_W) ca = mix(ca, cb, (NEAR_W - ca.w) / (cb.w - ca.w));
    if (cb.w < NEAR_W) cb = mix(cb, ca, (NEAR_W - cb.w) / (ca.w - cb.w));
    vec2 sa = (ca.xy / ca.w * 0.5 + 0.5) * uRes, sb = (cb.xy / cb.w * 0.5 + 0.5) * uRes;
    if (layer < 1.5) {
        // Beam jitter: the deflection never lands exactly twice in the same place.
        float k = floor(uTime * 40.0) + float(gl_InstanceID) * 7.31;
        vec2 j = (vec2(hash(k), hash(k + 3.7)) - 0.5) * uJitter;
        sa += j; sb += j;
    }
    vec2 d = sb - sa;
    float len = length(d);
    vec2 dir = len > 1e-3 ? d / len : vec2(1.0, 0.0);
    vec2 nrm = vec2(-dir.y, dir.x);
    float w = iA.w;
    float R = w * 10.0 + 2.0;
    vec2 p = mix(sa, sb, aCorner.x) + dir * (aCorner.x * 2.0 - 1.0) * R + nrm * aCorner.y * R;
    float z = layer > 1.5 ? 0.0 : clamp(mix(ca.z / ca.w, cb.z / cb.w, aCorner.x), -1.0, 1.0);
    gl_Position = vec4(p / uRes * 2.0 - 1.0, z, 1.0);
    vP = p; vA = sa; vB = sb; vW = w; vC = iC; vLayer = layer;
}
)";

inline const char* LINE_FS = R"(#version 330 core
in vec2 vP;
flat in vec2 vA, vB;
flat in float vW, vLayer;
flat in vec4 vC;
uniform vec2 uQuad[4];     // the well, in pixels (bottom-left origin), counter-clockwise
uniform vec4 uRects[2];    // HUD panels: x0, y0, x1, y1 (pixels, bottom-left origin)
uniform float uQuadDim, uRectDim, uHalo;
out vec4 frag;
float edge(vec2 a, vec2 b, vec2 p) { vec2 e = normalize(b - a); return (p.x - a.x) * e.y - (p.y - a.y) * e.x; }
void main() {
    vec2 ab = vB - vA;
    float l2 = dot(ab, ab);
    float t = l2 > 1e-4 ? clamp(dot(vP - vA, ab) / l2, 0.0, 1.0) : 0.0;
    float d = length(vP - (vA + ab * t));
    float core = exp(-0.5 * d * d / (vW * vW));
    float halo = exp(-d / (vW * 2.2));
    float I = mix(vC.x, vC.y, t) * (core + uHalo * halo);
    float H = mix(vC.z, vC.w, t) * (core * core + 0.6 * uHalo * halo);
    if (vLayer < 0.5) {
        vec2 p = gl_FragCoord.xy;
        float m = 1e9;
        for (int i = 0; i < 4; i++) m = min(m, -edge(uQuad[i], uQuad[(i + 1) % 4], p));
        float k = 1.0 - (1.0 - uQuadDim) * smoothstep(-6.0, 6.0, m);
        for (int i = 0; i < 2; i++) {
            vec4 r = uRects[i];
            float mr = min(min(p.x - r.x, r.z - p.x), min(p.y - r.y, r.w - p.y));
            k *= 1.0 - (1.0 - uRectDim) * smoothstep(-30.0, 30.0, mr);
        }
        I *= k; H *= k;
    }
    frag = vec4(I, H, 0.0, 1.0);
}
)";

// Depth-only terrain surface (hidden-line removal for the mountains), and flat HUD panels in pixels.
inline const char* FILL_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uVP;
uniform vec2 uRes;
uniform int uScreen;
void main() {
    if (uScreen == 1) gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0);
    else gl_Position = uVP * vec4(aPos, 1.0);
}
)";
inline const char* FILL_FS = R"(#version 330 core
uniform vec4 uColor;
out vec4 frag;
void main() { frag = uColor; }
)";

// Phosphor persistence: last frame's energy, decayed (the hot white core fades faster than the phosphor).
inline const char* DECAY_FS = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uPrev;
uniform vec2 uDecay;
out vec4 frag;
void main() {
    vec2 e = texture(uPrev, vUv).xy;
    frag = vec4(max(e * uDecay - 0.0015, 0.0), 0.0, 1.0);
}
)";

inline const char* DOWN_FS = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uTexel;
out vec4 frag;
void main() {
    vec4 s = texture(uTex, vUv + uTexel * vec2(-1.0, -1.0)) + texture(uTex, vUv + uTexel * vec2(1.0, -1.0)) +
             texture(uTex, vUv + uTexel * vec2(-1.0, 1.0)) + texture(uTex, vUv + uTexel * vec2(1.0, 1.0));
    frag = s * 0.25;
}
)";
inline const char* UP_FS = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform vec2 uTexel;
out vec4 frag;
void main() {
    vec4 s = texture(uTex, vUv) * 4.0;
    s += texture(uTex, vUv + vec2(uTexel.x, 0.0)) * 2.0 + texture(uTex, vUv - vec2(uTexel.x, 0.0)) * 2.0;
    s += texture(uTex, vUv + vec2(0.0, uTexel.y)) * 2.0 + texture(uTex, vUv - vec2(0.0, uTexel.y)) * 2.0;
    s += texture(uTex, vUv + uTexel) + texture(uTex, vUv - uTexel);
    s += texture(uTex, vUv + vec2(uTexel.x, -uTexel.y)) + texture(uTex, vUv + vec2(-uTexel.x, uTexel.y));
    frag = s / 16.0;
}
)";

// The tube: energy -> phosphor color, curved glass, faint scanlines, retrace bar, degauss wobble, power-off.
inline const char* COMPOSITE_FS = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uAccum, uBloom;
uniform vec2 uRes;
uniform vec3 uPhos0, uPhos1;   // phosphor below / above the split
uniform float uSplit;          // 0..1 from the top: above it the screen shows uPhos1
uniform float uBar, uBarAmt;   // retrace bar position from the top (0..1) and strength
uniform float uGain, uBloomAmt, uCurv, uScan, uTime, uDegauss, uFade;
uniform vec2 uCollapse;        // CRT power-off: horizontal / vertical extent of the picture (1 = full)
uniform float uDot;            // the last bright line / dot while the tube switches off
uniform float uBridge;         // change of universe: 0 the tube .. 1 the neutral bridge (flat, ink on uBridgeColor)
uniform vec3 uBridgeColor, uBridgeInk;
out vec4 frag;
void main() {
    vec2 c = vUv - 0.5;
    float aspect = uRes.x / uRes.y;
    vec2 ca = c * vec2(aspect, 1.0);
    float r2 = dot(ca, ca) / (aspect * aspect * 0.25 + 0.25);
    c *= (1.0 + uCurv * r2) / (1.0 + uCurv * 0.5);
    if (uDegauss > 0.0) {
        float a = uDegauss;
        c += a * vec2(0.010 * sin(c.y * 23.0 + uTime * 31.0) + 0.004 * sin(c.y * 71.0 - uTime * 57.0),
                      0.006 * sin(c.x * 17.0 - uTime * 27.0));
        float ang = a * 0.02 * sin(uTime * 19.0);
        c = mat2(cos(ang), -sin(ang), sin(ang), cos(ang)) * c;
    }
    // Glass edge: rounded corners.
    vec2 q = abs(c) * 2.0;
    vec2 dq = max(q - vec2(1.0 - 0.03 / aspect, 0.97), 0.0) / vec2(0.03 / aspect, 0.03);
    float glass = 1.0 - smoothstep(0.75, 1.0, length(dq));
    if (q.x > 1.0 || q.y > 1.0) glass = 0.0;
    vec2 cc = c / max(uCollapse, vec2(1e-3));
    vec2 uv = cc + 0.5;
    float inside = step(abs(cc.x), 0.5) * step(abs(cc.y), 0.5);
    vec2 e = texture(uAccum, uv).xy * inside;
    vec2 b = texture(uBloom, uv).xy * inside;
    float I = e.x + uBloomAmt * b.x;
    float H = e.y + uBloomAmt * 0.7 * b.y;
    float fromTop = 0.5 - c.y;
    vec3 phos = mix(uPhos0, uPhos1, smoothstep(uSplit + 0.004, uSplit - 0.004, fromTop));
    // Power-off: the picture is squeezed to a line then a dot; what's left of it brightens as it shrinks.
    float squeeze = 1.0 / max(0.04, sqrt(uCollapse.x * uCollapse.y));
    vec3 lin = (phos * I + vec3(1.0, 0.97, 0.92) * H) * uGain * min(sqrt(squeeze), 2.5);
    if (uDot > 0.0) {
        float hy = uCollapse.y * 0.5, hx = uCollapse.x * 0.5 * aspect;
        float dy = max(abs(c.y) - hy, 0.0) * uRes.y, dx = max(abs(c.x * aspect) - hx, 0.0) * uRes.y;
        float r = sqrt(dx * dx + dy * dy);
        float beam = exp(-0.5 * r * r / 3.0) + 0.3 * exp(-r / 14.0);
        lin += (phos * 1.2 + vec3(0.7)) * beam * uDot;
    }
    // Retrace bar: a bright horizontal beam sweeping down; what it has passed is freshly drawn.
    if (uBarAmt > 0.0) {
        float d = (fromTop - uBar) * uRes.y;
        float bar = exp(-0.5 * d * d / 3.0) + 0.3 * exp(-abs(d) / 16.0);
        float wake = d < 0.0 ? exp(d / (uRes.y * 0.06)) : 0.0;
        lin += (phos * 1.2 + vec3(0.45)) * uBarAmt * bar;
        lin *= 1.0 + 0.6 * uBarAmt * wake;
    }
    lin = lin * lin / (lin + 0.035); // toe: faint halos and bloom tails sink back into black
    vec3 col = 1.0 - exp(-lin);
    float scan = 0.5 + 0.5 * cos(gl_FragCoord.y * 3.14159265 * 2.0 / 3.0);
    col *= 1.0 - uScan * scan * (1.0 - uBridge);
    col *= 1.0 - 0.25 * r2 * (1.0 - uBridge);
    col *= mix(glass, 1.0, uBridge);
    if (uBridge > 0.0) {
        // The bridge: the beams that are left (the well, the stack, the HUD) become ink on the bridge color.
        float k = clamp((e.x + 0.5 * b.x + e.y) * 1.4, 0.0, 1.0);
        col = mix(col, mix(uBridgeColor, uBridgeInk, k), uBridge);
    }
    col *= uFade;
    frag = vec4(pow(max(col, 0.0), vec3(1.0 / 2.2)), 1.0);
}
)";

} // namespace vector_shaders
