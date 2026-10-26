#pragma once
// RISOGRAPH's shaders (see risograph.cpp).
//
// The frame is a print. The stack, the well's marks and the HUD are drawn as ink coverage into one buffer, one
// channel per drum: r = ink A (the key), g = ink B, b = ink C (the third ink of the peaks), a = the structure (the
// stack and the text, solid, for the bridge); the pieces' tints are screened where they are drawn. The background
// (big flat shapes, the sheet's gradients) goes into a second buffer as continuous ink densities, a soft riso
// gradient with no visible screen. COMPOSITE_FS prints both on the paper: the background's drums slightly out of
// register (only the background: the stack and the HUD never move), a riso grain in the ink, and the inks
// multiplied over white paper (overprints darken) or laid like light over black paper.

namespace risograph_shaders {

// Shared helpers, inserted after each fragment shader's #version line.
inline const char* COMMON_GLSL = R"(
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}
// Halftone screens, p in board units (one cell = one unit). A spot function (0 at a dot's center, 1 between
// dots) thresholded by the density: dots below half, holes above, a line screen when lines is set. The screens'
// lattices repeat every cell, so a piece's cells tile into one seamless tint.
//   drum 0 (A): dots at 45 degrees, drum 1 (B): dots at 0 degrees, drum 2 (C): 26.6 degrees.
float spot(vec2 p, int drum, bool lines) {
    vec2 uv;
    if (drum == 0) uv = vec2(p.x + p.y, p.x - p.y) * 3.0;
    else if (drum == 1) uv = p * 4.0;
    else uv = vec2(2.0 * p.x + p.y, -p.x + 2.0 * p.y) * 1.6;
    if (lines) return 0.5 - 0.5 * cos(6.2831853 * (drum == 1 ? (p.x - p.y) * 3.0 : uv.x));
    return 0.5 - 0.25 * (cos(6.2831853 * uv.x) + cos(6.2831853 * uv.y));
}
float screenAt(vec2 p, float d, int drum, bool lines) {
    if (d <= 0.004) return 0.0;
    if (d >= 0.996) return 1.0;
    float s = spot(p, drum, lines);
    float w = max(fwidth(s) * 0.75, 0.02);
    return clamp((d - s) / w + 0.5, 0.0, 1.0);
}
)";

// ---------------------------------------------------------------- the world: soft gradients of ink
// The background is printed in continuous densities (a riso gradient: no visible screen), into its own buffer, so
// that the registration only ever moves the background, never the stack or the HUD. r = A, g = B, b = C.
inline const char* WORLD_FS = R"(#version 330 core
#define NSHAPES 7
in vec2 vUv;
out vec4 frag;
uniform vec4 uView;            // center xy, half extents xy (board units)
uniform vec4 uShapeA[NSHAPES]; // center xy, size xy (disc / half disc: radius in x)
uniform vec4 uShapeB[NSHAPES]; // rotation, type (0 disc, 1 rectangle, 2 half disc), drum (3: D), density
uniform vec4 uShapeC[NSHAPES]; // gradient angle, gradient amount, -, -
uniform float uPx;             // board units per pixel
uniform vec4 uWell;            // the well: center xy, half extents xy (the sheet's gradients keep clear of it)
uniform float uSide;           // +1: the shapes (and the gradients) are on the right
uniform vec4 uSheet;           // C's bloom (density), B's wash (density), wash angle, slow drift phase
uniform vec4 uBlot;            // a lock's soft blot of B: center xy, age 0..1, strength
float wellDist(vec2 p) { vec2 e = abs(p - uWell.xy) - uWell.zw; return length(max(e, 0.0)); }
void main() {
    vec2 p = uView.xy + (vUv * 2.0 - 1.0) * uView.zw;
    vec4 ink = vec4(0.0); // A, B, C and the peak's hot ink D
    for (int i = 0; i < NSHAPES; i++) {
        float dens = uShapeB[i].w;
        if (dens <= 0.003) continue;
        vec2 q = p - uShapeA[i].xy;
        float c = cos(uShapeB[i].x), s = sin(uShapeB[i].x);
        q = vec2(c * q.x + s * q.y, -s * q.x + c * q.y);
        int type = int(uShapeB[i].y + 0.5);
        float d;
        if (type == 1) { vec2 e = abs(q) - uShapeA[i].zw; d = length(max(e, 0.0)) + min(max(e.x, e.y), 0.0); }
        else {
            d = length(q) - uShapeA[i].z;
            if (type == 2) d = max(d, -q.y);
        }
        float inside = clamp(0.5 - d / uPx, 0.0, 1.0);
        if (inside <= 0.0) continue;
        // a linear gradient across the shape: full density on one side, fading smoothly toward the other
        float ga = uShapeC[i].x;
        vec2 gd = vec2(cos(ga), sin(ga));
        float ext = type == 1 ? dot(abs(gd), uShapeA[i].zw) : uShapeA[i].z;
        float t = clamp(0.5 + 0.5 * dot(p - uShapeA[i].xy, gd) / max(ext, 0.01), 0.0, 1.0);
        float de = dens * mix(1.0, 1.0 - uShapeC[i].y, t * t * (3.0 - 2.0 * t));
        int drum = int(uShapeB[i].z + 0.5);
        float cov = inside * de;
        ink[drum] = 1.0 - (1.0 - ink[drum]) * (1.0 - cov);
    }
    // the sheet: large soft gradients, kept off the well and the text's side
    float wd = wellDist(p);
    float clear = smoothstep(0.6, 5.0, wd);
    vec2 n = (p - uView.xy) / uView.zw; // -1..1
    // a slow cloud in the ink, so the gradient reads as printed (very low frequency, not a pattern)
    float cloud = 0.75 + 0.5 * vnoise(p * 0.11 + vec2(uSheet.w, 0.3 * uSheet.w));
    if (uSheet.y > 0.0) {
        vec2 gd = vec2(cos(uSheet.z), sin(uSheet.z));
        float t = clamp(0.5 + 0.5 * dot(n, gd), 0.0, 1.0);
        float de = uSheet.y * smoothstep(0.3, 1.0, t) * clear * cloud;
        ink.g = 1.0 - (1.0 - ink.g) * (1.0 - de);
    }
    if (uSheet.x > 0.0) {
        // the bloom of the third ink: a broad glow rising from the shapes' edge of the sheet
        float edge = smoothstep(-0.1, 1.05, uSide * n.x) * (1.0 - 0.35 * n.y * n.y);
        float de = uSheet.x * edge * clear * cloud;
        ink.b = 1.0 - (1.0 - ink.b) * (1.0 - de);
    }
    if (uBlot.w > 0.0) {
        // a lock: a small soft blot of the second ink pressed out under the piece
        vec2 q = (p - uBlot.xy) / vec2(1.1 + 0.5 * uBlot.z, 0.45 + 0.15 * uBlot.z);
        float de = uBlot.w * (1.0 - uBlot.z) * (1.0 - uBlot.z) * exp(-2.5 * dot(q, q)) * step(p.y, uBlot.y + 0.2);
        ink.g = 1.0 - (1.0 - ink.g) * (1.0 - de);
    }
    frag = ink;
}
)";

// ---------------------------------------------------------------- instanced marks: cells, rules, rings
inline const char* MARK_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 a0;  // center xy, half extents xy
layout(location = 2) in vec4 a1;  // rotation, kind, p, q
layout(location = 3) in vec4 a2;
layout(location = 4) in vec4 a3;
layout(location = 5) in vec4 a4;
uniform mat4 uVP;
out vec2 vLocal;
out vec2 vWorld;
flat out vec4 v1;
flat out vec4 v2;
flat out vec4 v3;
flat out vec4 v4;
flat out vec2 vHalf;
void main() {
    vec2 l = aPos * a0.zw;
    float c = cos(a1.x), s = sin(a1.x);
    vec2 w = a0.xy + vec2(c * l.x - s * l.y, s * l.x + c * l.y);
    vLocal = l;
    vWorld = w;
    vHalf = a0.zw;
    v1 = a1; v2 = a2; v3 = a3; v4 = a4;
    gl_Position = uVP * vec4(w, 0.0, 1.0);
}
)";

// Kinds:
//   0 a piece's cell. v1.z: 8-bit mask of the same piece's neighbors (L R D U, LD RD LU RU; y up), v1.w: alpha.
//     v2: density of ink A, of ink B, line screen (0 none, 1 on A, 2 on B), keyline (0 none, 1 A, 2 B).
//     v3: torn edges 0..1, ink starvation 0..1, ghost (0 / 1), seed. v4: offsets of drums A and B (board units).
//     The cell's shape is its square inset at the piece's outline, so a piece is one clean shape and two pieces
//     always keep a hairline of paper between them.
//   1 a rule (a filled rectangle). v2: coverage of A, B, C, structure; v3.xy: its half extents.
//   2 a ring and a tinted disc. v2: ring coverage of A, B, C, structure; v3: radius, ring width, disc radius,
//     disc density; v4: disc drums A, B, C (weights), -.
inline const char* MARK_FS = R"(#version 330 core
in vec2 vLocal;
in vec2 vWorld;
flat in vec4 v1;
flat in vec4 v2;
flat in vec4 v3;
flat in vec4 v4;
flat in vec2 vHalf;
out vec4 frag;
uniform float uPx;
const float IN = 0.055;   // inset at a piece's outline
const float KEY = 0.075;  // keyline width
bool has(int m, int b) { return ((m >> b) & 1) != 0; }
// Signed distance to the cell's part of its piece (negative inside), joined sides running on past the cell.
float cellDist(vec2 p, int m) {
    bool L = has(m, 0), R = has(m, 1), D = has(m, 2), U = has(m, 3);
    vec2 lo = vec2(L ? -0.8 : -0.5 + IN, D ? -0.8 : -0.5 + IN);
    vec2 hi = vec2(R ? 0.8 : 0.5 - IN, U ? 0.8 : 0.5 - IN);
    vec2 c = (lo + hi) * 0.5, h = (hi - lo) * 0.5;
    float r = 0.035;
    vec2 q = abs(p - c) - h + r;
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    // concave corners: the diagonal is not the piece, keep its corner of paper
    const float E = 0.5 - IN;
    if (L && D && !has(m, 4)) d = max(d, min(-E - p.x, -E - p.y));
    if (R && D && !has(m, 5)) d = max(d, min(p.x - E, -E - p.y));
    if (L && U && !has(m, 6)) d = max(d, min(-E - p.x, p.y - E));
    if (R && U && !has(m, 7)) d = max(d, min(p.x - E, p.y - E));
    return d;
}
// Joined sides end at the cell's edge (the neighbor draws its own part): > 0 outside.
float joinClip(vec2 p, int m) {
    float e = -9.0;
    if (has(m, 0)) e = max(e, -p.x - 0.505);
    if (has(m, 1)) e = max(e, p.x - 0.505);
    if (has(m, 2)) e = max(e, -p.y - 0.505);
    if (has(m, 3)) e = max(e, p.y - 0.505);
    return e;
}
void main() {
    int kind = int(v1.y + 0.5);
    float px = uPx;
    if (kind == 1) {
        vec2 e = abs(vLocal) - v3.xy;
        float d = max(e.x, e.y);
        float cov = clamp(0.5 - d / px, 0.0, 1.0);
        frag = v2 * cov;
        return;
    }
    if (kind == 2) {
        float r = length(vLocal);
        float ring = clamp(0.5 - (abs(r - v3.x) - v3.y * 0.5) / px, 0.0, 1.0);
        float disc = clamp(0.5 - (r - v3.z) / px, 0.0, 1.0);
        vec4 o = v2 * ring;
        for (int k = 0; k < 3; k++)
            if (v4[k] > 0.0) o[k] = max(o[k], v4[k] * disc * v3.w); // a flat tint (no screen)
        frag = o;
        return;
    }
    // ---- a piece's cell
    int m = int(v1.z + 0.5);
    float alpha = v1.w;
    float tear = v3.x, starve = v3.y, seed = v3.w;
    bool ghost = v3.z > 0.5;
    // the cell's size (previews are smaller): the quad is the cell plus a margin of 0.15 on each side (half 0.65)
    float sc = vHalf.x / 0.65;
    float pxc = px / sc;
    vec4 o = vec4(0.0);
    float lineMode = v2.z, keyMode = v2.w;
    for (int k = 0; k < 2; k++) {
        float dens = k == 0 ? v2.x : v2.y;
        bool key = keyMode > 0.5 && int(keyMode + 0.5) == k + 1;
        if (ghost) { key = (k == 0) == (v2.x >= 0.5); dens = key ? 0.14 : 0.0; }
        if (dens <= 0.0 && !key) continue;
        vec2 off = k == 0 ? v4.xy : v4.zw;
        vec2 p = (vLocal - off) / sc; // cell units
        float d = cellDist(p, m);
        // torn edges: a strip ripped off raggedly at its top and bottom
        if (tear > 0.0) {
            float x = (p.x + seed) * 7.0;
            float r1 = vnoise(vec2(x, 1.0)) * 0.65 + vnoise(vec2(x * 2.9, 5.0)) * 0.35;
            float r2 = vnoise(vec2(x, 9.0)) * 0.65 + vnoise(vec2(x * 3.1, 13.0)) * 0.35;
            float cut = max(p.y - (0.5 - tear * (0.04 + 0.26 * r1)), -p.y - (0.5 - tear * (0.04 + 0.26 * r2)));
            d = max(d, cut);
        }
        // AA on the outline only: the joined edges are cut hard, so two cells of a piece meet without a seam
        float inside = clamp(0.5 - d / pxc, 0.0, 1.0) * step(joinClip(p, m), 0.0);
        if (inside <= 0.0) continue;
        bool lines = (k == 0 && lineMode > 0.5 && lineMode < 1.5) || (k == 1 && lineMode > 1.5);
        // a strip being torn off fades by its dots shrinking
        float shrink = tear > 0.0 ? alpha : 1.0;
        float cov = screenAt(p, dens * shrink, k, lines);
        if (key) {
            float kw = ghost ? 0.05 : KEY;
            cov = max(cov, clamp(0.5 - (-d - kw) / pxc, 0.0, 1.0) * shrink);
        }
        o[k] = inside * cov;
    }
    if (tear > 0.0) alpha = 1.0;
    // the structure: the piece's whole shape, solid
    {
        vec2 p = vLocal / sc;
        o.a = ghost ? 0.0 : clamp(0.5 - cellDist(p, m) / pxc, 0.0, 1.0) * step(joinClip(p, m), 0.0);
    }
    // ink starvation (game over): the drum runs dry in vertical streaks
    if (starve > 0.0) {
        float streak = vnoise(vec2(vWorld.x * 3.1, 0.5)) * 0.7 + vnoise(vec2(vWorld.x * 11.0, vWorld.y * 0.15)) * 0.3;
        float keep = smoothstep(starve - 0.08, starve + 0.08, streak * 0.9 + 0.05);
        o *= keep;
    }
    frag = o * alpha;
}
)";

// ---------------------------------------------------------------- HUD
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
uniform vec4 uInk;   // coverage of A, B, C, structure (times the alpha)
uniform float uBias;
void main() {
    float d = texture(uFont, vUv).r;
    float w = max(fwidth(d), 1e-3) * 0.75;
    float fill = smoothstep(0.5 - uBias - w, 0.5 - uBias + w, d);
    frag = uInk * fill;
}
)";
// A plain rectangle in pixels (zenscene's menu panels): erases the ink under it (alpha) or adds ink.
inline const char* PANEL_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform vec2 uRes;
void main() { gl_Position = vec4(aPos.x / uRes.x * 2.0 - 1.0, 1.0 - aPos.y / uRes.y * 2.0, 0.0, 1.0); }
)";
inline const char* PANEL_FS = R"(#version 330 core
out vec4 frag;
uniform vec4 uColor;
void main() { frag = uColor; }
)";

// ---------------------------------------------------------------- the print
inline const char* COMPOSITE_FS = R"(#version 330 core
in vec2 vUv;
out vec4 frag;
uniform sampler2D uInk;              // the stack, the well's marks and the HUD: always in register
uniform sampler2D uBg;               // the background's gradients
uniform vec2 uOffA, uOffB, uOffC;    // registration of each drum on the background only (uv)
uniform vec3 uPaper, uInkA, uInkB, uInkC, uInkD; // linear (D: the peak's hot ink, background only)
uniform float uDark;                 // black paper: the inks laid like light
uniform vec4 uView;                  // board space of the frame (center, half extents)
uniform float uGrain;
uniform float uBridge;
uniform vec3 uBridgeColor, uBridgeInk;
uniform float uDim, uFade;
void main() {
    vec2 bp = uView.xy + (vUv * 2.0 - 1.0) * uView.zw; // board units
    vec4 fg = texture(uInk, vUv);
    float a = fg.r, b = fg.g, c = fg.b, s = fg.a;
    // the riso grain: the ink laid unevenly (a soft mottle, a fine tooth, the odd missing speck), fixed in the paper
    vec2 g = bp * 34.0;
    float mott = vnoise(bp * 1.3) * 0.5 + vnoise(bp * 3.7 + 7.0) * 0.5;
    float tooth = vnoise(g) * 0.6 + vnoise(g * 2.3 + 3.1) * 0.4;
    float spk = step(0.93, vnoise(g * 0.71 + 11.0));
    float ga = 1.0 - uGrain * (0.10 * mott + 0.16 * smoothstep(0.45, 0.95, tooth) + 0.35 * spk);
    float gb = 1.0 - uGrain * (0.10 * (1.0 - mott) + 0.16 * smoothstep(0.45, 0.95, vnoise(g + 17.0)) + 0.35 * step(0.93, vnoise(g * 0.67 + 29.0)));
    float gc = 1.0 - uGrain * (0.08 * mott + 0.14 * smoothstep(0.45, 0.95, vnoise(g + 41.0)));
    a *= ga; b *= gb; c *= gc;
    // the background's gradients, slightly out of register, with a fine grain (a pixel-scale tooth, nearly
    // invisible: the gradient stays smooth at viewing size)
    float ba = texture(uBg, vUv - uOffA).r;
    float bb = texture(uBg, vUv - uOffB).g;
    float bc = texture(uBg, vUv - uOffC).b;
    float bd = texture(uBg, vUv + uOffB).a;
    vec2 pp = gl_FragCoord.xy;
    float fine = (hash12(pp) - 0.5) * 0.5 + (vnoise(bp * 9.0) - 0.5) * 0.5;
    float bg = 1.0 + 0.16 * fine;
    ba = clamp(ba * bg, 0.0, 1.0); bb = clamp(bb * bg, 0.0, 1.0); bc = clamp(bc * bg, 0.0, 1.0); bd = clamp(bd * bg, 0.0, 1.0);
    a = 1.0 - (1.0 - a) * (1.0 - ba);
    b = 1.0 - (1.0 - b) * (1.0 - bb);
    c = 1.0 - (1.0 - c) * (1.0 - bc);
    // the paper: a stock of its own tone, with a tooth, a slow cloudy formation and fibers (short hairs, a few
    // darker, a few lighter), all fixed in the sheet
    float form = vnoise(bp * 0.35 + 3.0) * 0.6 + vnoise(bp * 1.1 + 9.0) * 0.4;
    float toothP = vnoise(bp * 46.0) * 0.5 + vnoise(bp * 97.0 + 5.0) * 0.5;
    float fibers = 0.0;
    for (int i = 0; i < 3; i++) {
        float an = 0.7 + float(i) * 2.1;
        vec2 r = mat2(cos(an), sin(an), -sin(an), cos(an)) * bp;
        float n = vnoise(vec2(r.x * 1.6, r.y * 26.0) + float(i) * 13.0);
        float sel = vnoise(r * 0.9 + float(i) * 7.0);
        fibers += smoothstep(0.86, 0.95, n) * smoothstep(0.55, 0.8, sel) * (i == 1 ? -0.6 : 1.0);
    }
    float tex = 0.03 * (form - 0.5) + 0.018 * (toothP - 0.5) - 0.015 * fibers;
    vec3 col = uPaper * (1.0 + tex * mix(1.0, 4.0, uDark));
    // white paper: the inks multiply (overprints darken); black paper: the inks lie like light
    vec3 w = col * (vec3(1.0) - a * (vec3(1.0) - uInkA)) * (vec3(1.0) - b * (vec3(1.0) - uInkB)) * (vec3(1.0) - c * (vec3(1.0) - uInkC)) * (vec3(1.0) - bd * (vec3(1.0) - uInkD));
    vec3 k = vec3(1.0) - (vec3(1.0) - col) * (vec3(1.0) - a * uInkA) * (vec3(1.0) - b * uInkB) * (vec3(1.0) - c * uInkC) * (vec3(1.0) - bd * uInkD);
    col = mix(w, k, uDark);
    // the bridge: the world gone, the stack plain
    vec3 plain = mix(uBridgeColor, uBridgeInk, s);
    col = mix(col, plain, uBridge);
    col *= 1.0 - uDim * 0.5;
    col *= uFade;
    frag = vec4(pow(max(col, 0.0), vec3(1.0 / 2.2)), 1.0);
}
)";

} // namespace risograph_shaders
