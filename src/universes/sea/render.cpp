// SEA's drawing (see sea.cpp): the GL resources, the world's meshes (the sandy bed and its cut face, the sea's surface,
// the boats), the camera, the frame's passes, the stack's glass cubes and the HUD.
//
// The frame: the sun's shadow map (the thrown cubes, the boats); then the world, multisampled, with depth: the sky,
// the sand, the boats, the life in the water; a copy of it (what the thrown cubes refract); the thrown cubes (ray
// traced, writing their depth), the particles under the water, the sea's surface, the water's cut face, the particles
// above it, the rain; resolved, the well's veil over it (and the bridge); a copy of that (what the stack's glass
// refracts); the ghost, the stack, the falling piece, the previews, the gauge, the HUD; the final pass.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "layout.hpp"
#include "universes/sea/sea.hpp"
#include "universes/sea/shaders.hpp"
#include "universes/sea/world.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

using namespace sea_world;

namespace {

// Barlow (Italic for titles, Medium for labels and numbers), SIL Open Font License, see OFL-Barlow.txt.
const unsigned char FONT_ITALIC[] = {
#include "universes/sea/Barlow-Italic.ttf.inc"
};
const unsigned char FONT_ROMAN[] = {
#include "universes/sea/Barlow-Medium.ttf.inc"
};

namespace shaders = sea_shaders;
using ugl::set1f;
using ugl::set1i;
using ugl::set2f;
using ugl::set3f;
using ugl::set4f;
using ugl::setMat;

enum { W_WORLD = 1, W_SHADOW = 2, W_GLASS = 4 };
// Compiles a program, the shared helpers inserted after each #version line.
GLuint compile(const char* vs, const char* fs, const char* name, int with) {
    std::string f(fs), v(vs);
    for (std::string* s : {&f, &v}) {
        const size_t at = s->find('\n') + 1;
        std::string pre = shaders::COMMON_GLSL;
        if (with & W_WORLD) pre += shaders::WORLD_GLSL;
        if (with & W_SHADOW) pre += shaders::SHADOW_GLSL;
        if (with & W_GLASS) pre += shaders::GLASS_GLSL;
        s->insert(at, pre);
    }
    return ugl::program(v.c_str(), f.c_str(), name);
}

mat4 inverse(const mat4& m) {
    const float* a = m.m;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    mat4 r{};
    if (std::fabs(det) < 1e-12f) return mat4::identity();
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] / det;
    return r;
}

// a model matrix: translation, rotation (columns), uniform scale
mat4 model(const vec3& t, const vec3& cx, const vec3& cy, const vec3& cz, float s) {
    mat4 m = mat4::identity();
    m.at(0, 0) = cx.x * s; m.at(1, 0) = cx.y * s; m.at(2, 0) = cx.z * s;
    m.at(0, 1) = cy.x * s; m.at(1, 1) = cy.y * s; m.at(2, 1) = cy.z * s;
    m.at(0, 2) = cz.x * s; m.at(1, 2) = cz.y * s; m.at(2, 2) = cz.z * s;
    m.at(0, 3) = t.x; m.at(1, 3) = t.y; m.at(2, 3) = t.z;
    return m;
}
vec3 qRot(const float q[4], const vec3& v) {
    const vec3 u(q[0], q[1], q[2]);
    const vec3 t = cross(u, v) * 2.f;
    return v + t * q[3] + cross(u, t);
}

// Low-poly meshes: position, normal, material (the boats' paints: 0 hull, 1 superstructure, 2 sail, 3 accent,
// 4 windows, 5..7 containers, 8 dark, 9 deck).
struct MeshBuilder {
    std::vector<float>& v;
    void tri(const vec3& a, const vec3& b, const vec3& c, int m) {
        const vec3 n = normalize(cross(b - a, c - a));
        for (const vec3* p : {&a, &b, &c}) v.insert(v.end(), {p->x, p->y, p->z, n.x, n.y, n.z, (float)m});
    }
    void quad(const vec3& a, const vec3& b, const vec3& c, const vec3& d, int m) {
        tri(a, b, c, m);
        tri(a, c, d, m);
    }
    void box(const vec3& lo, const vec3& hi, int m) {
        const vec3 p[8] = {vec3(lo.x, lo.y, lo.z), vec3(hi.x, lo.y, lo.z), vec3(hi.x, hi.y, lo.z), vec3(lo.x, hi.y, lo.z),
                           vec3(lo.x, lo.y, hi.z), vec3(hi.x, lo.y, hi.z), vec3(hi.x, hi.y, hi.z), vec3(lo.x, hi.y, hi.z)};
        quad(p[0], p[3], p[2], p[1], m); // back (z-)
        quad(p[4], p[5], p[6], p[7], m); // front (z+)
        quad(p[0], p[4], p[7], p[3], m); // left
        quad(p[1], p[2], p[6], p[5], m); // right
        quad(p[3], p[7], p[6], p[2], m); // top
        quad(p[0], p[1], p[5], p[4], m); // bottom
    }
    // a hull along x (bow at +x): length, beam, depth under the water, freeboard; its sides above the water in `side`,
    // below in the dark paint, the deck in `deck`
    void hull(float L, float B, float D, float F, int side, int deck) {
        const int N = 12;
        auto W = [&](float u) { return u < 0.6f ? B * 0.5f * (0.82f + 0.18f * u / 0.6f) : B * 0.5f * std::sqrt(std::max(0.f, 1.f - (u - 0.6f) * (u - 0.6f) / 0.16f)); };
        auto Fh = [&](float u) { return F * (1.f + 0.3f * std::max(0.f, u - 0.7f) / 0.3f); };
        auto Dh = [&](float u) { return -D * (1.f - 0.65f * std::max(0.f, u - 0.65f) / 0.35f); };
        const float wl = std::min(F * 0.35f, 0.25f);
        for (int i = 0; i < N; i++) {
            const float u0 = (float)i / N, u1 = (float)(i + 1) / N;
            const float x0 = -L * 0.5f + L * u0, x1 = -L * 0.5f + L * u1;
            const float w0 = W(u0), w1 = W(u1), f0 = Fh(u0), f1 = Fh(u1), d0 = Dh(u0), d1 = Dh(u1);
            const float b0 = w0 * 0.45f, b1 = w1 * 0.45f;
            // the waterline's width along the side (between the deck's and the bottom's)
            auto at = [&](float w, float b, float f, float d, float y) { return b + (w - b) * (y - d) / std::max(f - d, 1e-3f); };
            const float m0 = at(w0, b0, f0, d0, wl), m1 = at(w1, b1, f1, d1, wl);
            for (float s : {1.f, -1.f}) {
                quad(vec3(x0, f0, s * w0), vec3(x1, f1, s * w1), vec3(x1, wl, s * m1), vec3(x0, wl, s * m0), side);
                quad(vec3(x0, wl, s * m0), vec3(x1, wl, s * m1), vec3(x1, d1, s * b1), vec3(x0, d0, s * b0), 8);
            }
            quad(vec3(x0, d0, b0), vec3(x1, d1, b1), vec3(x1, d1, -b1), vec3(x0, d0, -b0), 8);
            quad(vec3(x0, f0, -w0), vec3(x1, f1, -w1), vec3(x1, f1, w1), vec3(x0, f0, w0), deck);
        }
        const float x0 = -L * 0.5f, w0 = W(0.f), f0 = Fh(0.f), d0 = Dh(0.f);
        quad(vec3(x0, f0, -w0), vec3(x0, f0, w0), vec3(x0, d0, w0 * 0.45f), vec3(x0, d0, -w0 * 0.45f), side);
    }
    // a band of windows around a block (slightly proud of it)
    void windows(float x0, float x1, float y, float h, float zw) {
        box(vec3(x0 + 0.08f, y - h * 0.5f, -zw - 0.02f), vec3(x1 + 0.02f, y + h * 0.5f, zw + 0.02f), 4);
    }
};

} // namespace

// =====================================================================================================
// Setup
// =====================================================================================================

bool SeaUniverse::init(int width, int height) {
    progSky_ = compile(ugl::FULLSCREEN_VS, shaders::SKY_FS, "sea sky", W_WORLD);
    progSand_ = compile(shaders::SAND_VS, shaders::SAND_FS, "sea sand", W_WORLD | W_SHADOW);
    progSurf_ = compile(shaders::SURF_VS, shaders::SURF_FS, "sea surface", W_WORLD);
    progFront_ = compile(shaders::FRONT_VS, shaders::FRONT_FS, "sea front", W_WORLD);
    progBoat_ = compile(shaders::MESH_VS, shaders::BOAT_FS, "sea boat", W_WORLD | W_SHADOW);
    progDepth_ = compile(shaders::MESH_VS, shaders::DEPTH_FS, "sea depth", 0);
    progCube_ = compile(shaders::CUBE_VS, shaders::CUBE_FS, "sea thrown cube", W_WORLD | W_GLASS);
    progLife_ = compile(shaders::LIFE_VS, shaders::LIFE_FS, "sea life", W_WORLD);
    progPart_ = compile(shaders::PART_VS, shaders::PART_FS, "sea particle", W_WORLD);
    progRain_ = compile(ugl::FULLSCREEN_VS, shaders::RAIN_FS, "sea rain", 0);
    progVeil_ = compile(ugl::FULLSCREEN_VS, shaders::VEIL_FS, "sea veil", 0);
    progCell_ = compile(shaders::CELL_VS, shaders::CELL_FS, "sea cube", W_GLASS);
    progSprite_ = compile(shaders::SPRITE_VS, shaders::SPRITE_FS, "sea gauge", 0);
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "sea text", 0);
    progShape_ = compile(shaders::SHAPE_VS, shaders::SHAPE_FS, "sea shape", 0);
    progPost_ = compile(ugl::FULLSCREEN_VS, shaders::POST_FS, "sea post", 0);

    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    auto instanced = [&](GLuint& vao, GLuint& vbo, int vec4s) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        for (int i = 0; i < vec4s; i++) {
            glEnableVertexAttribArray(1 + i);
            glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, (GLsizei)(sizeof(float) * 4 * vec4s), (void*)(sizeof(float) * 4 * i));
            glVertexAttribDivisor(1 + i, 1);
        }
    };
    instanced(cellVao_, cellVbo_, 4);
    instanced(spriteVao_, spriteVbo_, 4);
    instanced(cubeVao_, cubeVbo_, 5);
    instanced(lifeVao_, lifeVbo_, 5);
    instanced(partVao_, partVbo_, 4);

    glGenVertexArrays(1, &textVao_);
    glGenBuffers(1, &textVbo_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (void*)(sizeof(float) * 2));

    glGenVertexArrays(1, &shapeVao_);
    glGenBuffers(1, &shapeVbo_);
    glBindVertexArray(shapeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 6, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)(sizeof(float) * 2));
    glBindVertexArray(0);

    buildMeshes();

    // the sun's shadow map
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_RES, SHADOW_RES, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[gl] sea shadow FBO incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::clamp((int)maxSamples, 1, 4);
    if (std::getenv("ZEN_SEA_GPU")) glGenQueries(2, timer_);

    buildFont(fontItalic_, FONT_ITALIC);
    buildFont(fontRoman_, FONT_ROMAN);
    pal_ = paletteOf(0);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// The world's meshes: the sandy bed (a grid denser near the well, wider far out, its cut face along the front), the
// sea's surface (a grid lifted by the swell in its vertex shader), the water's cut face (a strip along the front), the
// boats and a box for the cubes' shadows.
void SeaUniverse::buildMeshes() {
    auto upload = [](Mesh& m, const std::vector<float>& v, const std::vector<unsigned>& idx, const std::vector<int>& comps) {
        glGenVertexArrays(1, &m.vao);
        glGenBuffers(1, &m.vbo);
        glBindVertexArray(m.vao);
        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
        int stride = 0;
        for (int c : comps) stride += c;
        int off = 0;
        for (size_t i = 0; i < comps.size(); i++) {
            glEnableVertexAttribArray((GLuint)i);
            glVertexAttribPointer((GLuint)i, comps[i], GL_FLOAT, GL_FALSE, stride * (GLsizei)sizeof(float), (void*)(sizeof(float) * off));
            off += comps[i];
        }
        if (!idx.empty()) {
            glGenBuffers(1, &m.ibo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
            m.count = (int)idx.size();
        } else m.count = (int)(v.size() / stride);
        glBindVertexArray(0);
    };
    auto grid = [](int cols, int rows, std::vector<unsigned>& idx, unsigned base) {
        for (int j = 0; j < rows; j++)
            for (int i = 0; i < cols; i++) {
                const unsigned a = base + j * (cols + 1) + i, b = a + 1, c = a + cols + 1, d = c + 1;
                idx.insert(idx.end(), {a, c, b, b, c, d});
            }
    };
    {
        // the sand: z from the front out to 320, x widening with the distance; then the cut face
        std::vector<float> v;
        std::vector<unsigned> idx;
        const int C = 180, R = 150;
        for (int j = 0; j <= R; j++) {
            const float t = (float)j / R;
            const float z = FRONT_Z - 320.f * std::pow(t, 2.1f);
            const float hw = 32.f + 0.95f * (-z);
            for (int i = 0; i <= C; i++) {
                const float x = ((float)i / C * 2.f - 1.f) * hw;
                const vec3 n = sandN(x, z);
                v.insert(v.end(), {x, sandH(x, z), z, n.x, n.y, n.z});
            }
        }
        grid(C, R, idx, 0);
        const unsigned base = (unsigned)(v.size() / 6);
        const int CF = 400;
        for (int i = 0; i <= CF; i++) {
            const float x = ((float)i / CF * 2.f - 1.f) * 70.f;
            v.insert(v.end(), {x, -40.f, FRONT_Z, 0.f, 0.f, 1.f});
            v.insert(v.end(), {x, sandH(x, FRONT_Z), FRONT_Z, 0.f, 0.f, 1.f});
        }
        for (int i = 0; i < CF; i++) {
            const unsigned a = base + i * 2, b = a + 1, c = a + 2, d = a + 3;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
        upload(sand_, v, idx, {3, 3});
    }
    {
        // the sea's surface: out to 800 (the haze takes it far before)
        std::vector<float> v;
        std::vector<unsigned> idx;
        const int C = 220, R = 190;
        for (int j = 0; j <= R; j++) {
            const float t = (float)j / R;
            const float z = FRONT_Z - 800.f * std::pow(t, 2.6f);
            const float hw = 32.f + 0.95f * (-z);
            for (int i = 0; i <= C; i++) v.insert(v.end(), {((float)i / C * 2.f - 1.f) * hw, z});
        }
        grid(C, R, idx, 0);
        upload(surf_, v, idx, {2});
    }
    {
        std::vector<float> v;
        const int CF = 480;
        for (int i = 0; i <= CF; i++) {
            const float x = ((float)i / CF * 2.f - 1.f) * 70.f;
            v.insert(v.end(), {x, 0.f, x, 1.f});
        }
        upload(front_, v, {}, {2});
        front_.count = (CF + 1) * 2;
    }
    {
        // the boats (bow at +x, the waterline at y = 0) and the cubes' box
        std::vector<float> v;
        MeshBuilder b{v};
        auto mark = [&](int t) { boatFirst_[t] = (int)(v.size() / 7); };
        auto done = [&](int t) { boatCount_[t] = (int)(v.size() / 7) - boatFirst_[t]; };
        // a dinghy: a small hull, a mast, one sail
        mark(0);
        b.hull(1.6f, 0.62f, 0.15f, 0.22f, 0, 9);
        b.box(vec3(0.12f, 0.2f, -0.025f), vec3(0.17f, 1.55f, 0.025f), 8);
        b.tri(vec3(0.1f, 0.32f, 0.f), vec3(0.1f, 1.5f, 0.f), vec3(-0.6f, 0.34f, 0.f), 2);
        done(0);
        // a sailboat: a cabin, a tall mast, a mainsail and a jib
        mark(1);
        b.hull(3.2f, 1.0f, 0.3f, 0.38f, 0, 9);
        b.box(vec3(-0.7f, 0.36f, -0.3f), vec3(0.4f, 0.62f, 0.3f), 1);
        b.windows(-0.6f, 0.3f, 0.5f, 0.08f, 0.3f);
        b.box(vec3(0.25f, 0.3f, -0.035f), vec3(0.32f, 4.1f, 0.035f), 8);
        b.tri(vec3(0.22f, 0.72f, 0.f), vec3(0.22f, 3.95f, 0.f), vec3(-1.35f, 0.74f, 0.f), 2);
        b.tri(vec3(0.36f, 0.8f, 0.f), vec3(0.36f, 3.55f, 0.f), vec3(1.55f, 0.45f, 0.f), 2);
        b.box(vec3(-1.4f, 0.38f, -0.5f), vec3(1.5f, 0.42f, 0.5f), 3);
        done(1);
        // a fishing boat: a red hull, a wheelhouse aft, a mast and its boom
        mark(2);
        b.hull(4.6f, 1.6f, 0.5f, 0.6f, 3, 9);
        b.box(vec3(-1.5f, 0.58f, -0.55f), vec3(-0.4f, 1.55f, 0.55f), 1);
        b.windows(-1.45f, -0.4f, 1.3f, 0.22f, 0.55f);
        b.box(vec3(-1.55f, 1.55f, -0.6f), vec3(-0.35f, 1.62f, 0.6f), 8);
        b.box(vec3(0.75f, 0.6f, -0.05f), vec3(0.85f, 3.0f, 0.05f), 8);
        b.box(vec3(0.8f, 1.3f, -0.03f), vec3(2.0f, 1.36f, 0.03f), 8);
        b.box(vec3(-0.2f, 0.6f, -0.5f), vec3(0.5f, 0.85f, 0.5f), 5);
        done(2);
        // a ferry: two decks of windows, a bridge, a funnel
        mark(3);
        b.hull(10.f, 2.6f, 0.8f, 1.0f, 0, 9);
        b.box(vec3(-4.2f, 0.98f, -1.2f), vec3(3.2f, 1.95f, 1.2f), 1);
        b.windows(-4.1f, 3.2f, 1.45f, 0.3f, 1.2f);
        b.box(vec3(-3.4f, 1.95f, -1.0f), vec3(1.8f, 2.75f, 1.0f), 1);
        b.windows(-3.3f, 1.8f, 2.35f, 0.28f, 1.0f);
        b.box(vec3(0.6f, 2.75f, -0.8f), vec3(1.8f, 3.25f, 0.8f), 1);
        b.windows(0.6f, 1.8f, 3.05f, 0.2f, 0.8f);
        b.box(vec3(-1.9f, 2.75f, -0.38f), vec3(-0.9f, 3.9f, 0.38f), 3);
        b.box(vec3(-1.95f, 3.9f, -0.42f), vec3(-0.85f, 4.05f, 0.42f), 8);
        done(3);
        // a container ship: rows of containers, the bridge tower aft
        mark(4);
        b.hull(18.f, 3.0f, 1.0f, 1.2f, 0, 8);
        {
            Rng r(77);
            for (int i = 0; i < 6; i++) {
                const float x0 = -4.6f + i * 2.25f;
                for (int k = 0; k < 2; k++) {
                    const int hgt = r.irange(1, 3);
                    for (int l = 0; l < hgt; l++) {
                        const float z0 = k == 0 ? -1.35f : 0.05f;
                        b.box(vec3(x0, 1.2f + l * 0.72f, z0), vec3(x0 + 2.1f, 1.2f + (l + 1) * 0.72f - 0.04f, z0 + 1.3f), 5 + r.irange(0, 2));
                    }
                }
            }
        }
        b.box(vec3(-8.3f, 1.18f, -1.4f), vec3(-6.2f, 4.6f, 1.4f), 1);
        b.windows(-8.2f, -6.2f, 4.15f, 0.35f, 1.4f);
        b.box(vec3(-8.6f, 4.6f, -1.6f), vec3(-6.0f, 4.75f, 1.6f), 8);
        b.box(vec3(-7.6f, 4.75f, -0.35f), vec3(-6.9f, 5.6f, 0.35f), 3);
        done(4);
        // a liner: three tiers of windows, two funnels
        mark(5);
        b.hull(22.f, 3.2f, 1.0f, 1.6f, 0, 9);
        b.box(vec3(-9.2f, 1.58f, -1.5f), vec3(7.2f, 2.6f, 1.5f), 1);
        b.windows(-9.1f, 7.2f, 2.1f, 0.32f, 1.5f);
        b.box(vec3(-8.2f, 2.6f, -1.35f), vec3(5.6f, 3.5f, 1.35f), 1);
        b.windows(-8.1f, 5.6f, 3.05f, 0.3f, 1.35f);
        b.box(vec3(-6.6f, 3.5f, -1.15f), vec3(4.0f, 4.3f, 1.15f), 1);
        b.windows(-6.5f, 4.0f, 3.9f, 0.26f, 1.15f);
        b.box(vec3(2.6f, 4.3f, -0.9f), vec3(4.0f, 4.8f, 0.9f), 1);
        for (float fx : {-3.8f, -0.8f}) {
            b.box(vec3(fx, 4.3f, -0.55f), vec3(fx + 1.4f, 6.2f, 0.55f), 3);
            b.box(vec3(fx - 0.05f, 6.2f, -0.6f), vec3(fx + 1.45f, 6.45f, 0.6f), 8);
        }
        done(5);
        boxFirst_ = (int)(v.size() / 7);
        b.box(vec3(-0.44f), vec3(0.44f), 0);
        boxCount_ = (int)(v.size() / 7) - boxFirst_;

        glGenVertexArrays(1, &boatVao_);
        glGenBuffers(1, &meshVbo_);
        glGenBuffers(1, &meshInstVbo_);
        glBindVertexArray(boatVao_);
        glBindBuffer(GL_ARRAY_BUFFER, meshVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));
        glBindBuffer(GL_ARRAY_BUFFER, meshInstVbo_);
        for (int i = 0; i < 5; i++) {
            glEnableVertexAttribArray(3 + i);
            glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, 20 * sizeof(float), (void*)(sizeof(float) * 4 * i));
            glVertexAttribDivisor(3 + i, 1);
        }
        glBindVertexArray(0);
        boxVao_ = boatVao_;
    }
}

// Glyphs baked as signed distance fields (they scale cleanly).
void SeaUniverse::buildFont(Font& f, const unsigned char* ttf) {
    stbtt_fontinfo* info = new stbtt_fontinfo;
    if (!stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        std::fprintf(stderr, "[gl] sea font init failed\n");
        delete info;
        return;
    }
    f.info = info;
    f.px = 64.f;
    f.scaleTT = stbtt_ScaleForPixelHeight(info, f.px);
    int x0, y0, x1, y1;
    if (stbtt_GetCodepointBox(info, 'H', &x0, &y0, &x1, &y1)) f.capH = y1 * f.scaleTT;
    const int AW = 1024, AH = 1024, PAD = 8;
    std::vector<unsigned char> atlas(AW * AH, 0);
    int x = 1, y = 1, rowH = 0;
    for (int ch = 32; ch < 128; ch++) {
        Glyph& g = f.g[ch - 32];
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, ch, &adv, &lsb);
        g.adv = adv * f.scaleTT;
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* sdf = stbtt_GetCodepointSDF(info, f.scaleTT, ch, PAD, 128, 128.f / PAD, &w, &h, &xo, &yo);
        if (!sdf) { g.w = g.h = 0; continue; }
        if (x + w + 1 > AW) { x = 1; y += rowH + 1; rowH = 0; }
        if (y + h + 1 > AH) { stbtt_FreeSDF(sdf, nullptr); break; }
        for (int r = 0; r < h; r++) std::memcpy(&atlas[(y + r) * AW + x], sdf + r * w, w);
        g.x0 = x / (float)AW; g.y0 = y / (float)AH; g.x1 = (x + w) / (float)AW; g.y1 = (y + h) / (float)AH;
        g.ox = (float)xo; g.oy = (float)yo; g.w = (float)w; g.h = (float)h;
        x += w + 1;
        rowH = std::max(rowH, h);
        stbtt_FreeSDF(sdf, nullptr);
    }
    glGenTextures(1, &f.tex);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, AW, AH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f.ok = true;
}

void SeaUniverse::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    const float aspect = (float)width_ / height_;
    const layout::Camera cam = layout::camera(width_, height_);
    const float hh = cam.dist * std::tan(cam.fovDeg * 0.5f * PI / 180.f), hw = hh * aspect;
    viewH_ = v2(hw, hh);
    viewC_ = v2(cam.target.x, cam.target.y);
    camDist_ = cam.dist;
    px_ = 2.f * hh / height_;
    sceneT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    bgT_.create(width_, height_, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    // the world's multisampled target
    if (msFbo_) {
        glDeleteFramebuffers(1, &msFbo_);
        glDeleteRenderbuffers(1, &msColor_);
        glDeleteRenderbuffers(1, &msDepth_);
    }
    glGenFramebuffers(1, &msFbo_);
    glGenRenderbuffers(1, &msColor_);
    glGenRenderbuffers(1, &msDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, msColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA16F, width_, height_);
    glBindRenderbuffer(GL_RENDERBUFFER, msDepth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, width_, height_);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[gl] sea world FBO incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    setCamera();
}

SeaUniverse::~SeaUniverse() {
    sceneT_.destroy();
    bgT_.destroy();
    for (GLuint p : {progSky_, progSand_, progSurf_, progFront_, progBoat_, progDepth_, progCube_, progLife_, progPart_, progRain_,
                     progVeil_, progCell_, progSprite_, progText_, progShape_, progPost_})
        glDeleteProgram(p);
    for (Mesh* m : {&sand_, &surf_, &front_}) {
        glDeleteVertexArrays(1, &m->vao);
        glDeleteBuffers(1, &m->vbo);
        if (m->ibo) glDeleteBuffers(1, &m->ibo);
    }
    for (GLuint v : {boatVao_, cellVao_, spriteVao_, cubeVao_, lifeVao_, partVao_, textVao_, shapeVao_}) glDeleteVertexArrays(1, &v);
    for (GLuint b : {meshVbo_, meshInstVbo_, quadVbo_, cellVbo_, spriteVbo_, cubeVbo_, lifeVbo_, partVbo_, textVbo_, shapeVbo_})
        glDeleteBuffers(1, &b);
    glDeleteFramebuffers(1, &msFbo_);
    glDeleteRenderbuffers(1, &msColor_);
    glDeleteRenderbuffers(1, &msDepth_);
    glDeleteFramebuffers(1, &shadowFbo_);
    glDeleteTextures(1, &shadowTex_);
    if (timer_[0]) glDeleteQueries(2, timer_);
    for (Font* f : {&fontItalic_, &fontRoman_}) {
        if (f->tex) glDeleteTextures(1, &f->tex);
        delete (stbtt_fontinfo*)f->info;
    }
}

// =====================================================================================================
// Camera, light
// =====================================================================================================

// The layout's reference camera, raised above the sea (more as it rises, so its surface stays seen from above) and
// looking straight ahead, its image shifted back down: the board plane lands on the reference camera's pixels.
void SeaUniverse::setCamera() {
    const layout::Camera ref = layout::camera(width_, height_);
    const float aspect = (float)width_ / height_;
    const float half = ref.fovDeg * 0.5f * PI / 180.f;
    const float f = 1.f / std::tan(half);
    const float eyeY = lerpf(4.5f, 8.f, rise_);
    eye_ = vec3(ref.target.x, eyeY, ref.target.z + ref.dist);
    horizon_ = f * (eyeY - ref.target.y) / ref.dist;
    view_ = mat4::lookAt(eye_, eye_ + vec3(0.f, 0.f, -1.f), vec3(0.f, 1.f, 0.f));
    mat4 shift = mat4::identity();
    shift.at(1, 3) = horizon_;
    vp_ = shift * mat4::perspective(2.f * half, aspect, 1.f, 900.f) * view_;
    invVP_ = inverse(vp_);
    camR_ = vec3(1.f, 0.f, 0.f);
    camU_ = vec3(0.f, 1.f, 0.f);
}

// Toward the sun (or moon): its azimuth and elevation; a negative elevation is a fraction of the sky shown above the
// horizon (so a low sun or the moon keeps its place in the picture as the sea, and the eye, rise).
vec3 SeaUniverse::sunDir() const {
    const layout::Camera ref = layout::camera(width_, height_);
    const float half = ref.fovDeg * 0.5f * PI / 180.f;
    float el = pal_.sunEl;
    if (el < 0.f) el = std::atan(-el * (1.f - horizon_) * std::tan(half)) * 180.f / PI;
    const float a = pal_.sunAz * PI / 180.f, e = el * PI / 180.f;
    return normalize(vec3(std::sin(a) * std::cos(e), std::sin(e), -std::cos(a) * std::cos(e)));
}

// The world's uniforms (WORLD_GLSL).
void SeaUniverse::worldUniforms(GLuint p) {
    const float bridge = saturate(bridge_.amount);
    const float world = 1.f - smoothstepf(0.f, 0.6f, bridge);
    const vec3 L = sunDir();
    set3f(p, "uEye", eye_);
    set1f(p, "uSea", seaLevel());
    set1f(p, "uFrontZ", FRONT_Z);
    set1f(p, "uWaveT", waveT_);
    set1f(p, "uAmp", amp_);
    set1f(p, "uSharp", sharp_);
    set1f(p, "uDir", dir_);
    set1f(p, "uCaps", caps_ * world);
    set1f(p, "uStorm", storm_ * world);
    set1f(p, "uTime", time_);
    set1f(p, "uSlow", slow_);
    set1f(p, "uNight", pal_.night);
    set1f(p, "uFlash", flash_ * world);
    set1f(p, "uCloudT", cloudT_);
    set1f(p, "uStars", pal_.stars);
    set1f(p, "uClouds", pal_.clouds);
    set3f(p, "uSunDir", L);
    // the light: the sun's, dimmed by the storm's overcast; the sky's, from its colors
    const float overcast = 1.f - 0.75f * storm_;
    set3f(p, "uSunCol", pal_.sun * (pal_.lightK * overcast));
    const vec3 amb = lerp(pal_.skyTop, pal_.skyLow, 0.45f) * pal_.ambK;
    set3f(p, "uAmb", lerp(amb, pal_.stormSky * 1.4f, storm_ * 0.5f));
    set4f(p, "uSunDisc", pal_.sunR * 1.65f * PI / 180.f, 0.f, pal_.moon, 1.f - storm_);
    set3f(p, "uSkyTop", pal_.skyTop);
    set3f(p, "uSkyLow", pal_.skyLow);
    set3f(p, "uHaze", pal_.haze);
    set3f(p, "uWater", pal_.sea2);
    set3f(p, "uDeep", pal_.deep);
    set3f(p, "uFoam", pal_.foam);
    set3f(p, "uDusk", pal_.dusk);
    set3f(p, "uStormSky", pal_.stormSky);
    set3f(p, "uStormSea", pal_.stormSea);
    set3f(p, "uCloud", pal_.cloud);
    set3f(p, "uStormCloud", pal_.stormCloud);
    {
        const float a = flashAz_ * PI / 180.f, e = 6.f * PI / 180.f;
        set3f(p, "uFlashDir", vec3(std::sin(a) * std::cos(e), std::sin(e), -std::cos(a) * std::cos(e)));
    }
    setMat(p, "uVP", vp_);
}

// The boats, one instanced draw per type (depthOnly: into the shadow map).
void SeaUniverse::drawBoats(GLuint prog, bool depthOnly) {
    if (boats_.empty()) return;
    (void)prog;
    glBindVertexArray(boatVao_);
    glBindBuffer(GL_ARRAY_BUFFER, meshInstVbo_);
    std::vector<float> inst;
    for (int t = 0; t < 6; t++) {
        inst.clear();
        for (const Boat& b : boats_) {
            if (b.type != t || b.alpha <= 0.01f) continue;
            if (depthOnly && b.z < -40.f) continue;
            const float L = BOAT_LEN[t] * b.scale, B = L * 0.15f + 0.3f;
            const float bow = b.x + b.dir * L / 3.f, stern = b.x - b.dir * L / 3.f;
            const float pitch = std::atan((waveH(bow, b.z) - waveH(stern, b.z)) / (2.f * L / 3.f)) * 0.8f;
            const float roll = std::atan((waveH(b.x, b.z + B) - waveH(b.x, b.z - B)) / (2.f * B)) * 0.6f +
                               rock_ * std::sin(time_ * 0.85f + b.phase) / (1.f + t * 0.4f);
            // the boat's frame: its length along its course, pitched by the swell, rolled across it
            const vec3 fx0(b.dir, 0.f, 0.f), fz0(0.f, 0.f, b.dir);
            const float cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
            const vec3 fx = fx0 * cp + vec3(0.f, sp, 0.f);
            const vec3 fy1 = vec3(0.f, cp, 0.f) - fx0 * sp;
            const vec3 fy = fy1 * cr + fz0 * sr;
            const vec3 fz = fz0 * cr - fy1 * sr;
            const vec3 pos(b.x, surfY(b.x, b.z) - 0.05f * L * 0.1f, b.z);
            const mat4 m = model(pos, fx, fy, fz, b.scale);
            inst.insert(inst.end(), m.m, m.m + 16);
            inst.insert(inst.end(), {b.alpha * (1.f - smoothstepf(0.f, 0.6f, saturate(bridge_.amount))), b.seed, 0.f, 0.f});
        }
        if (inst.empty()) continue;
        glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(float), inst.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLES, boatFirst_[t], boatCount_[t], (GLsizei)(inst.size() / 20));
    }
}

// The thrown cubes as boxes (their shadows).
void SeaUniverse::drawBoxes(GLuint prog) {
    (void)prog;
    std::vector<float> inst;
    for (const Body& b : bodies_) {
        if (b.age < b.delay || b.fade > 0.6f) continue;
        const mat4 m = model(b.p, qRot(b.q, vec3(1, 0, 0)), qRot(b.q, vec3(0, 1, 0)), qRot(b.q, vec3(0, 0, 1)), 1.f);
        inst.insert(inst.end(), m.m, m.m + 16);
        inst.insert(inst.end(), {1.f, 0.f, 0.f, 0.f});
    }
    if (inst.empty()) return;
    glBindVertexArray(boxVao_);
    glBindBuffer(GL_ARRAY_BUFFER, meshInstVbo_);
    glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(float), inst.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLES, boxFirst_, boxCount_, (GLsizei)(inst.size() / 20));
}

// =====================================================================================================
// The stack's cubes
// =====================================================================================================

void SeaUniverse::cube(float x, float y, float size, const vec3& col, float alpha, bool ghost, float seed, float crack, vec2 crackP) {
    if (alpha <= 0.003f) return;
    cells_.push_back({{x, y, size, 0.f}, {col.x, col.y, col.z, alpha}, {ghost ? 1.f : 0.f, crack, 0.f, 0.f}, {seed, 0.f, crackP.x, crackP.y}});
}

void SeaUniverse::flushCells() {
    if (cells_.empty()) return;
    glUseProgram(progCell_);
    setMat(progCell_, "uVP", vp_);
    set1f(progCell_, "uPx", px_);
    set1f(progCell_, "uTime", time_);
    set1f(progCell_, "uBridge", smoothstepf(0.3f, 0.95f, bridge_.amount));
    set3f(progCell_, "uBridgeInk", bridge_.ink);
    set1f(progCell_, "uLightPanel", pal_.lightPanel);
    set3f(progCell_, "uSea2", pal_.sea2);
    set2f(progCell_, "uRes", (float)width_, (float)height_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bgT_.tex);
    set1i(progCell_, "uBg", 0);
    set2f(progCell_, "uSunP", pal_.sunX, pal_.sunY);
    set3f(progCell_, "uSunCol", pal_.sun);
    set3f(progCell_, "uSkyTop", pal_.skyTop);
    set3f(progCell_, "uSkyLow", pal_.skyLow);
    set1f(progCell_, "uNight", pal_.night);
    // the cubes are seen a little from above and from the well's middle: each one's top and inner side lie behind
    // its neighbours above and toward the middle, so those are drawn after it (bottom row first, outer ones first)
    std::stable_sort(cells_.begin(), cells_.end(), [](const CellInst& a, const CellInst& b) {
        const float ra = std::round(a.a0[1] * 4.f), rb = std::round(b.a0[1] * 4.f);
        if (ra != rb) return ra < rb;
        return std::fabs(a.a0[0]) > std::fabs(b.a0[0]);
    });
    glBindVertexArray(cellVao_);
    glBindBuffer(GL_ARRAY_BUFFER, cellVbo_);
    glBufferData(GL_ARRAY_BUFFER, cells_.size() * sizeof(CellInst), cells_.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)cells_.size());
    cells_.clear();
}

// =====================================================================================================
// Text
// =====================================================================================================

float SeaUniverse::textWidth(const Font& f, const std::string& s, float size, float track) const {
    if (!f.ok || s.empty()) return s.size() * size * 0.5f;
    const stbtt_fontinfo* info = (const stbtt_fontinfo*)f.info;
    const float k = size / f.px;
    float w = 0;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 32 || ch >= 128) ch = '?';
        w += f.g[ch - 32].adv * k + track;
        if (i + 1 < s.size()) {
            unsigned char n = (unsigned char)s[i + 1];
            if (n < 32 || n >= 128) n = '?';
            w += stbtt_GetCodepointKernAdvance(info, ch, n) * f.scaleTT * k;
        }
    }
    return w - track;
}

float SeaUniverse::textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
                             std::vector<float>& out) const {
    if (!f.ok) return 0.f;
    const stbtt_fontinfo* info = (const stbtt_fontinfo*)f.info;
    const float k = size / f.px;
    const float baseline = y + f.capH * k;
    float pen = x;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 32 || ch >= 128) ch = '?';
        const Glyph& g = f.g[ch - 32];
        if (g.w > 0) {
            const float x0 = pen + g.ox * k, y0 = baseline + g.oy * k, x1 = x0 + g.w * k, y1 = y0 + g.h * k;
            const float v[6][4] = {{x0, y0, g.x0, g.y0}, {x1, y0, g.x1, g.y0}, {x1, y1, g.x1, g.y1},
                                   {x0, y0, g.x0, g.y0}, {x1, y1, g.x1, g.y1}, {x0, y1, g.x0, g.y1}};
            for (auto& q : v) out.insert(out.end(), q, q + 4);
        }
        pen += g.adv * k + track;
        if (i + 1 < s.size()) {
            unsigned char n = (unsigned char)s[i + 1];
            if (n < 32 || n >= 128) n = '?';
            pen += stbtt_GetCodepointKernAdvance(info, ch, n) * f.scaleTT * k;
        }
    }
    return pen - track - x;
}

void SeaUniverse::drawText(const Font& f, const std::vector<float>& q, const vec3& ink, float alpha, float bias) {
    if (q.empty() || !f.ok) return;
    glUseProgram(progText_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f.tex);
    set1i(progText_, "uFont", 0);
    set2f(progText_, "uRes", (float)width_, (float)height_);
    const float b = smoothstepf(0.3f, 0.95f, bridge_.amount);
    set3f(progText_, "uInk", lerp(ink, bridge_.ink, b));
    set1f(progText_, "uAlpha", alpha);
    set1f(progText_, "uBias", bias);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glBufferData(GL_ARRAY_BUFFER, q.size() * sizeof(float), q.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(q.size() / 4));
}

// How light the world is behind a point (board space): the sky above the horizon, the water's cut face under the
// sea's level, the sand's cut face, the sea's surface seen from above.
float SeaUniverse::bgLuma(float bx, float by) const {
    const float S = seaLevel();
    const float ndc = (by - viewC_.y) / viewH_.y;
    vec3 c;
    if (by < sandH(bx, FRONT_Z)) c = lerp(vec3(0.62f, 0.47f, 0.29f), pal_.haze, 0.15f) * (1.f - 0.62f * pal_.night) * 0.7f;
    else if (by < S) c = lerp(pal_.sea2, pal_.deep * 0.2f, smoothstepf(0.5f, 10.f, S - by) * 0.92f);
    else if (ndc < horizon_) c = lerp(pal_.sea1, pal_.skyLow, 0.5f);
    else {
        const float h = saturate((ndc - horizon_) * 0.344f / 0.24f);
        c = lerp(pal_.skyLow, pal_.skyTop, smoothstepf(0.f, 1.f, std::pow(h, 0.7f)));
        c = lerp(c, pal_.stormSky, storm_ * 0.85f);
    }
    if (by >= S) c = lerp(c, pal_.stormSea, storm_ * 0.4f);
    c = lerp(c, pal_.dusk, slow_ * 0.6f);
    if (std::fabs(bx) < 5.1f && std::fabs(by) < 10.1f)
        c = lerp(c, pal_.panel, lerpf(pal_.panelA, std::min(0.88f, pal_.panelA * 1.45f), smoothstepf(2.f, 10.f, S - by)));
    return ugl::luma(c);
}

// The HUD in SEA's typography: Barlow, a plain grotesk like the lettering on a harbor's signs; italic for the song, the
// section and the celebrations, spaced capitals for labels. Ink dark over a light sky or panel, pale over the sea.
void SeaUniverse::drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels) {
    const float s = height_ / 1080.f;
    if (!panels.empty()) {
        std::vector<float> v;
        auto quad = [&](float x0, float y0, float x1, float y1, const vec3& c, float a) {
            const float q[6][6] = {{x0, y0, c.x, c.y, c.z, a}, {x1, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a},
                                   {x0, y0, c.x, c.y, c.z, a}, {x1, y1, c.x, c.y, c.z, a}, {x0, y1, c.x, c.y, c.z, a}};
            for (auto& r : q) v.insert(v.end(), r, r + 6);
        };
        for (const HudRect& r : panels) {
            // (the day variants veil the well in a dark glass, but their HUD panels stay light)
            const vec3 panel = pal_.lightPanel > 0.5f ? ugl::toLinear(hex(0xf0f5f6)) : pal_.panel;
            const vec3 c = r.custom ? ugl::toLinear(r.color) : r.accent ? pal_.accent : panel;
            quad(r.x, r.y, r.x + r.w, r.y + r.h, c, std::min(1.f, r.alpha));
        }
        glUseProgram(progShape_);
        set2f(progShape_, "uRes", (float)width_, (float)height_);
        glBindVertexArray(shapeVao_);
        glBindBuffer(GL_ARRAY_BUFFER, shapeVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 6));
    }
    const bool inkDark = ugl::luma(pal_.ink) < 0.2f;
    const vec3 darkInk = inkDark ? pal_.ink : ugl::toLinear(hex(0x1b2a38));
    const vec3 lightInk = inkDark ? ugl::toLinear(hex(0xf2f5f4)) : pal_.ink;
    std::vector<float> q;
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        const float base = h.scale * 12.f;
        auto inkAt = [&](float px, float py) {
            const float bx = viewC_.x + (px / width_ * 2.f - 1.f) * viewH_.x;
            const float by = viewC_.y - (py / height_ * 2.f - 1.f) * viewH_.y;
            // the ink with the most contrast against what is behind it
            const float l = (std::sqrt(bgLuma(bx, by - 0.3f)) + std::sqrt(bgLuma(bx, by)) + std::sqrt(bgLuma(bx, by + 0.3f))) / 3.f;
            return std::fabs(l - std::sqrt(ugl::luma(darkInk))) > std::fabs(l - std::sqrt(ugl::luma(lightInk))) ? darkInk : lightInk;
        };
        vec3 ink = inkAt(h.x < 0.f ? width_ + h.x : h.x, h.y + base * 0.5f);
        vec3 accent = lerp(pal_.accent, ink, 0.25f);
        const float alpha = saturate(h.alpha);
        q.clear();
        auto place = [&](const Font& f, const std::string& text, float size, float track, float dy = 0.f) {
            const float w = textWidth(f, text, size, track);
            const float x = ugl::hudLeft(h, w, width_);
            ink = inkAt(x + w * 0.5f, h.y + dy + size * 0.8f);
            accent = lerp(pal_.accent, ink, 0.25f);
            textQuads(f, text, x, h.y + dy, size, track, q);
        };
        auto titleCase = [](const std::string& str) {
            std::string o = str;
            bool start = true;
            for (char& c : o) {
                if (std::isalpha((unsigned char)c)) {
                    c = start ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c);
                    start = false;
                } else start = c == ' ' || c == '-';
            }
            return o;
        };
        switch (h.role) {
        case HUD_UNIVERSE: {
            // "Sea" in the italic, the variant's name in small spaced capitals after it.
            std::string name = h.text, var;
            const size_t dot = name.find("\xC2\xB7");
            if (dot != std::string::npos) { var = name.substr(dot + 2); name = name.substr(0, dot); }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!var.empty() && var.front() == ' ') var.erase(var.begin());
            name = titleCase(name);
            const float sz = base * 1.4f, vsz = base * 0.66f, vtr = vsz * 0.3f;
            const float wName = textWidth(fontItalic_, name, sz, 0.f);
            const float wVar = var.empty() ? 0.f : textWidth(fontRoman_, var, vsz, vtr);
            const float gap = base * 0.8f;
            const float x = ugl::hudLeft(h, wName + (var.empty() ? 0.f : gap + wVar), width_);
            textQuads(fontItalic_, name, x, h.y - base * 0.12f, sz, 0.f, q);
            const float a = saturate(0.35f + h.alpha * 1.6f);
            drawText(fontItalic_, q, inkAt(x + wName * 0.5f, h.y + sz * 0.4f), a, 0.03f);
            if (!var.empty()) {
                q.clear();
                textQuads(fontRoman_, var, x + wName + gap, h.y + base * 0.22f, vsz, vtr, q);
                drawText(fontRoman_, q, inkAt(x + wName + gap + wVar * 0.5f, h.y + base * 0.22f + vsz * 0.5f), a * 0.85f, 0.02f);
            }
            continue;
        }
        case HUD_SECTION:
            place(fontItalic_, titleCase(h.text), base * 1.35f, 0.f);
            drawText(fontItalic_, q, ink, saturate(0.2f + h.alpha * 1.9f), 0.03f);
            continue;
        case HUD_TITLE:
            place(fontItalic_, h.text, base * 1.25f, 0.f, -base * 0.12f);
            drawText(fontItalic_, q, ink, saturate(h.alpha * 1.3f), 0.03f);
            continue;
        case HUD_INFO:
            place(fontRoman_, h.text, base * 0.78f, base * 0.12f);
            drawText(fontRoman_, q, ink, saturate(alpha * 1.2f) * 0.8f, 0.02f);
            continue;
        case HUD_LABEL:
            place(fontRoman_, h.text, base * 0.74f, base * 0.32f);
            drawText(fontRoman_, q, ink, saturate(alpha * 1.2f) * 0.72f, 0.02f);
            continue;
        case HUD_VALUE:
            place(fontRoman_, h.text, base * 1.08f, base * 0.04f);
            drawText(fontRoman_, q, ink, alpha, 0.02f);
            continue;
        case HUD_NOTICE:
            place(fontItalic_, h.text, base * 1.15f, 0.f);
            drawText(fontItalic_, q, accent, alpha, 0.03f);
            continue;
        case HUD_KEY:
            place(fontRoman_, h.text, base * 0.88f, base * 0.05f);
            drawText(fontRoman_, q, ink, alpha, 0.02f);
            continue;
        case HUD_TOAST:
            place(fontItalic_, titleCase(h.text), base * 1.5f, 0.f, -base * 0.2f);
            drawText(fontItalic_, q, ink, alpha, 0.04f);
            continue;
        case HUD_TOAST_SUB:
            place(fontRoman_, h.text, base * 0.82f, base * 0.2f);
            drawText(fontRoman_, q, accent, alpha * 0.9f, 0.02f);
            continue;
        case HUD_DIALOG:
            if (h.scale >= 3.5f * s) {
                place(fontItalic_, titleCase(h.text), base * 1.3f, 0.f, -base * 0.12f);
                drawText(fontItalic_, q, ink, alpha, 0.04f);
            } else {
                place(fontRoman_, h.text, base * 0.86f, base * 0.1f);
                drawText(fontRoman_, q, h.accent ? accent : ink, alpha, 0.02f);
            }
            continue;
        default:
            place(fontRoman_, h.text, base * 0.84f, base * 0.06f);
            drawText(fontRoman_, q, h.accent ? accent : ink, alpha, 0.02f);
            continue;
        }
    }
}

// =====================================================================================================
// Frame
// =====================================================================================================

void SeaUniverse::render(const Frame& f, GLuint fbo) {
    const Game& game = f.game;
    const MusicState& music = f.music;
    const float dt = f.dt;
    {
        const Palette target = paletteOf(variant_);
        if (!palSet_) { pal_ = target; palSet_ = true; }
        else lerpPalette(pal_, target, 1.f - std::exp(-dt * 1.0f));
    }
    // the section, from the HUD (a drop is a storm, a chorus a breeze)
    sectionKnown_ = false;
    for (const HudText& h : f.hud)
        if (h.role == HUD_SECTION) {
            sectionKnown_ = true;
            sectionDrop_ = h.text == "DROP";
        }
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, 0.9f, dt);
    dimT_ = approach(dimT_, f.paused ? 1.f : (f.dim ? 0.5f : 0.f), 3.f, dt);
    if (over_ && !game.over()) over_ = false;
    const float wdt = f.paused ? 0.f : dt * lerpf(1.f, 0.4f, slow_);
    if (!f.paused) {
        time_ += dt;
        watchBoard(game);
        simulate(game, music, dt, wdt);
    }
    const float bridge = saturate(bridge_.amount);
    const float world = 1.f - smoothstepf(0.f, 0.6f, bridge);
    setCamera();
    const bool timing = timer_[0] != 0;
    if (timing) glBeginQuery(GL_TIME_ELAPSED, timer_[timerFrame_ & 1]);

    // ---- The sun's shadow map: the thrown cubes and the near boats on the sand around the well.
    {
        const vec3 L = sunDir();
        const vec3 c(0.f, -9.f, -14.f);
        const vec3 up = std::fabs(L.y) > 0.95f ? vec3(0, 0, 1) : vec3(0, 1, 0);
        lightVP_ = mat4::ortho(-30.f, 30.f, -30.f, 30.f, 1.f, 300.f) * mat4::lookAt(c + L * 140.f, c, up);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.f);
    glUseProgram(progDepth_);
    setMat(progDepth_, "uVP", lightVP_);
    drawBoxes(progDepth_);
    drawBoats(progDepth_, true);
    glDisable(GL_POLYGON_OFFSET_FILL);

    // ---- The world.
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glViewport(0, 0, width_, height_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE0);
    auto shadowUniforms = [&](GLuint p) {
        set1i(p, "uShadow", 5);
        setMat(p, "uLightVP", lightVP_);
        set1f(p, "uShadowK", pal_.shadowK * (1.f - 0.7f * storm_));
    };
    // the sky (and the water's cut face where nothing is behind it)
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(progSky_);
    worldUniforms(progSky_);
    setMat(progSky_, "uInvVP", invVP_);
    ugl::drawFullscreen();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    // the sandy bed
    glUseProgram(progSand_);
    worldUniforms(progSand_);
    shadowUniforms(progSand_);
    set3f(progSand_, "uSand", lerp(vec3(0.62f, 0.47f, 0.29f), pal_.haze, 0.15f) * (1.f - 0.45f * pal_.night));
    glBindVertexArray(sand_.vao);
    glDrawElements(GL_TRIANGLES, sand_.count, GL_UNSIGNED_INT, nullptr);
    // the boats
    if (!boats_.empty()) {
        glUseProgram(progBoat_);
        worldUniforms(progBoat_);
        shadowUniforms(progBoat_);
        const vec3 mats[10] = {pal_.hull, pal_.super_, pal_.sail, pal_.baccent, pal_.super_ * 0.25f, pal_.cont0, pal_.cont1, pal_.cont2,
                               pal_.hull * 0.45f + vec3(0.02f), lerp(pal_.super_, pal_.hull, 0.5f) * 0.8f};
        float mf[30];
        for (int i = 0; i < 10; i++) { mf[i * 3] = mats[i].x; mf[i * 3 + 1] = mats[i].y; mf[i * 3 + 2] = mats[i].z; }
        glUniform3fv(ugl::U(progBoat_, "uMat"), 10, mf);
        set3f(progBoat_, "uLit", pal_.window);
        drawBoats(progBoat_, false);
    }
    // the life in the water (premultiplied, under the surface)
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    if (!critters_.empty() && world > 0.01f) {
        std::vector<float> inst;
        const float S = seaLevel();
        for (const Critter& c : critters_) {
            const float a = c.alpha * world;
            if (a <= 0.003f) continue;
            const float y = critterY(c);
            const float dark = smoothstepf(0.5f, 10.f, S - y);
            vec3 body = c.col;
            float hx = c.size, hy = c.size, glow = 0.f, anim = time_ + c.phase;
            switch (c.kind) {
            case L_FISH:
                body = lerp(c.col, pal_.deep * 0.25f, dark * 0.7f);
                hx = c.size * 1.25f; hy = c.size * 0.7f;
                anim = time_ * (6.f + 6.f * c.dart) + c.phase;
                break;
            case L_JELLY:
                hx = c.size * 1.2f; hy = c.size * 2.6f;
                glow = 0.35f + 0.35f * dark + 0.6f * glowUp_;
                anim = worldTime_ * 0.55f + c.phase;
                break;
            case L_ANGLER:
                hx = c.size * 1.6f; hy = c.size * 1.25f;
                glow = 0.8f + 0.4f * glowUp_;
                anim = worldTime_ + c.phase;
                break;
            case L_SIPHON:
                hx = c.size + 0.3f; hy = 0.7f;
                glow = 0.55f + 0.5f * glowUp_;
                anim = worldTime_ * 0.35f + c.phase;
                break;
            default: {
                const float tw = 0.55f + 0.45f * std::sin(time_ * (0.3f + 0.02f * c.seed) + c.seed * 6.f);
                glow = (0.8f + 0.5f * glowUp_) * tw * smoothstepf(0.35f, 0.8f, dark);
                hx = hy = c.size;
                break;
            }
            }
            const float bob = c.kind == L_JELLY ? 0.25f * std::sin(anim * 0.5f) : c.kind == L_FISH ? 0.04f * std::sin(time_ * 1.3f + c.phase * 3.f) : 0.f;
            const float v[20] = {c.x, y + bob, hx, hy, c.dir, (float)c.kind, anim, glow, body.x, body.y, body.z, a,
                                 c.glow.x, c.glow.y, c.glow.z, c.seed, c.z, 0.f, 0.f, 0.f};
            inst.insert(inst.end(), v, v + 20);
        }
        if (!inst.empty()) {
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glUseProgram(progLife_);
            worldUniforms(progLife_);
            set1f(progLife_, "uPx", px_);
            set1f(progLife_, "uLifeGlow", 0.5f + 0.5f * glowUp_ + 0.5f);
            glBindVertexArray(lifeVao_);
            glBindBuffer(GL_ARRAY_BUFFER, lifeVbo_);
            glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(float), inst.data(), GL_STREAM_DRAW);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)(inst.size() / 20));
        }
    }
    // a copy of the world so far: what the thrown cubes' glass shows behind it
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bgT_.fbo);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // the thrown cubes, far to near, each writing its depth
    if (!bodies_.empty()) {
        std::vector<const Body*> order;
        for (const Body& b : bodies_)
            if (b.age >= b.delay) order.push_back(&b);
        std::sort(order.begin(), order.end(), [&](const Body* a, const Body* b) {
            const vec3 da = a->p - eye_, db = b->p - eye_;
            return dot(da, da) > dot(db, db);
        });
        std::vector<float> inst;
        for (const Body* b : order) {
            const vec3 col = pal_.glass[b->type];
            const float alpha = (1.f - smoothstepf(0.f, 1.f, b->fade)) * world;
            const float v[20] = {b->p.x, b->p.y, b->p.z, 1.f, b->q[0], b->q[1], b->q[2], b->q[3], col.x, col.y, col.z, alpha,
                                 b->crackL.x, b->crackL.y, b->crackL.z, b->crack, b->fill, b->seed, 0.f, 0.f};
            inst.insert(inst.end(), v, v + 20);
        }
        if (!inst.empty()) {
            glDepthMask(GL_TRUE);
            glUseProgram(progCube_);
            worldUniforms(progCube_);
            set3f(progCube_, "uCamR", camR_);
            set3f(progCube_, "uCamU", camU_);
            set1f(progCube_, "uLightPanel", pal_.lightPanel);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, bgT_.tex);
            set1i(progCube_, "uBg", 0);
            glBindVertexArray(cubeVao_);
            glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
            glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(float), inst.data(), GL_STREAM_DRAW);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)(inst.size() / 20));
            glDepthMask(GL_FALSE);
        }
    }
    // the particles: those under the water before the surface, those above it after
    std::vector<float> under, above;
    for (const Particle& p : parts_) {
        if (p.age < 0.f) continue;
        const float t = p.age / p.life;
        const float a = (1.f - smoothstepf(0.6f, 1.f, t)) * world;
        std::vector<float>& out = p.p.y < surfY(p.p.x, p.p.z) - 0.03f ? under : above;
        auto put = [&](float size, int kind, const vec3& col, float alpha, float stretch, float soft) {
            if (alpha <= 0.003f) return;
            const float v[16] = {p.p.x, p.p.y, p.p.z, size, p.v.x, p.v.y, p.v.z, (float)kind, col.x, col.y, col.z, alpha,
                                 stretch, soft, p.seed, 0.f};
            out.insert(out.end(), v, v + 16);
        };
        switch (p.kind) {
        case P_JET:
            // one soft body for the whole jet (it reads over a pale sky as over the sand), its bright core over it
            put(p.size * 1.4f, 0, lerp(WATER, pal_.sea2, 0.65f), 0.4f * world, 0.18f, 0.7f);
            put(p.size, 0, p.col, 0.82f * world, 0.18f, 0.7f);
            break;
        case P_RUN: put(p.size * 1.1f, 0, p.col, 0.75f * (1.f - t) * world, 0.5f, 0.6f); break;
        case P_SPLASH: put(p.size, 0, p.col, a * 0.85f, 0.f, 0.5f); break;
        case P_SPRAY: put(p.size, 0, p.col, a * 0.7f, 0.12f, 0.7f); break;
        case P_BUBBLE: put(p.size, 3, p.col, a * 0.9f, 0.f, 0.f); break;
        case P_PLUME: put(p.size * (1.f + 1.6f * t), 5, p.col, 0.5f * (1.f - t) * world, 0.f, 0.f); break;
        case P_SAND: put(p.size, 0, p.col, a, 0.f, 0.4f); break;
        default: break;
        }
    }
    auto drawParts = [&](const std::vector<float>& v) {
        if (v.empty()) return;
        glUseProgram(progPart_);
        worldUniforms(progPart_);
        set3f(progPart_, "uCamR", camR_);
        set3f(progPart_, "uCamU", camU_);
        set1f(progPart_, "uPx", px_);
        glBindVertexArray(partVao_);
        glBindBuffer(GL_ARRAY_BUFFER, partVbo_);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)(v.size() / 16));
    };
    drawParts(under);
    // the sea's surface (it writes its depth: what is above it is drawn after)
    glDepthMask(GL_TRUE);
    glUseProgram(progSurf_);
    worldUniforms(progSurf_);
    set1f(progSurf_, "uGlint", glint_ * world);
    glBindVertexArray(surf_.vao);
    glDrawElements(GL_TRIANGLES, surf_.count, GL_UNSIGNED_INT, nullptr);
    glDepthMask(GL_FALSE);
    // the water's cut face: a fine lit line at its top
    glUseProgram(progFront_);
    worldUniforms(progFront_);
    set1f(progFront_, "uPx", px_);
    glBindVertexArray(front_.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, front_.count);
    drawParts(above);
    // rain slanting across in a storm
    if (storm_ * world > 0.01f) {
        glDisable(GL_DEPTH_TEST);
        glUseProgram(progRain_);
        set4f(progRain_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
        set1f(progRain_, "uPx", px_);
        set1f(progRain_, "uRain", storm_ * world);
        set1f(progRain_, "uTimeR", time_);
        set1f(progRain_, "uDirR", dir_);
        set1f(progRain_, "uSeaY", seaLevel());
        set3f(progRain_, "uRainCol", lerp(pal_.foam, pal_.skyLow, 0.5f));
        ugl::drawFullscreen();
    }
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    // resolved into the scene
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneT_.fbo);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    sceneT_.bind();

    // ---- The well's veil over the world (and the bridge: the world fading to its color).
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(progVeil_);
    set4f(progVeil_, "uView", viewC_.x, viewC_.y, viewH_.x, viewH_.y);
    set1f(progVeil_, "uPx", px_);
    set1f(progVeil_, "uPanelA", pal_.panelA * (1.f - bridge));
    set1f(progVeil_, "uSeaY", seaLevel());
    set1f(progVeil_, "uBridge", smoothstepf(0.05f, 0.85f, bridge));
    set3f(progVeil_, "uPanel", pal_.panel);
    set3f(progVeil_, "uHaze", pal_.haze);
    set3f(progVeil_, "uBridgeCol", bridge_.color);
    ugl::drawFullscreen();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // ---- A copy of the scene so far: what the stack's glass shows (bent) behind it.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneT_.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bgT_.fbo);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    sceneT_.bind();
    // ---- The ghost, the stack, the falling piece: every cell its own glass cube of water.
    if (game.hasPiece() && !game.over()) {
        const Piece& p = game.piece();
        Piece gp = p;
        gp.y = game.ghostY();
        if (gp.y > p.y) {
            int gc[4][2];
            game.pieceCells(gp, gc);
            for (auto& c : gc) {
                if (c[1] < Game::HIDDEN) continue;
                const vec3 w = layout::cellPos((float)c[0], (float)c[1]);
                cube(w.x, w.y, 1.f, pal_.glass[std::clamp(p.type, 0, 6)], 1.f, true, 0.f);
            }
        }
    }
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const int t = game.cell(x, y).type;
            if (t < 0) continue;
            const vec3 p = layout::cellPos((float)x, y - game.rowOffset(y));
            cube(p.x, p.y, 1.f, pal_.glass[std::clamp(t, 0, 6)], 1.f, false, std::fmod((float)(x * 7 + y * 13 + t), 50.f));
        }
    for (const Dying& d : dying_)
        cube(d.x, d.y, 1.f, pal_.glass[d.type], 1.f, false, d.x * 3.f + d.y, smoothstepf(d.delay - 0.15f, d.delay, d.t), v2(0.1f, -0.15f));
    if (game.hasPiece() && !game.over()) {
        const Piece& p = game.piece();
        int cells[4][2];
        game.pieceCells(p, cells);
        const float dx = game.pieceVisualX() - p.x, dy = game.pieceVisualY() - p.y;
        const int type = std::clamp(p.type, 0, 6);
        for (int i = 0; i < 4; i++) {
            const auto& c = cells[i];
            if (c[1] < Game::HIDDEN - 1) continue;
            const vec3 w = layout::cellPos(c[0] + dx, c[1] + dy);
            cube(w.x, w.y, 1.f, pal_.glass[type], 1.f, false, (float)(type * 7 + i * 13));
        }
    }
    // ---- Previews: four small cubes each.
    auto preview = [&](int type, vec2 center, float sc, float alpha) {
        if (type < 0) return;
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        game.pieceCells(p, cells);
        float minx = 9, maxx = -9, miny = 9, maxy = -9;
        for (auto& c : cells) {
            minx = std::min(minx, (float)c[0]); maxx = std::max(maxx, (float)c[0]);
            miny = std::min(miny, (float)c[1]); maxy = std::max(maxy, (float)c[1]);
        }
        for (int i = 0; i < 4; i++) {
            const auto& c = cells[i];
            const vec2 pos = v2(center.x + (c[0] - (minx + maxx) * 0.5f) * sc, center.y - (c[1] - (miny + maxy) * 0.5f) * sc);
            cube(pos.x, pos.y, sc, pal_.glass[std::clamp(type, 0, 6)], alpha, false, (float)(type * 3 + i * 11));
        }
    };
    preview(game.holdType(), v2(layout::HOLD_CENTER.x, layout::HOLD_CENTER.y), layout::previewScale(0), game.holdUsed() ? 0.35f : 1.f);
    for (int i = 0; i < 3; i++) {
        const vec3 c = layout::nextCenter(i);
        preview(game.next(i), v2(c.x, c.y), layout::previewScale(i), 1.f);
    }
    flushCells();
    // ---- The bonus gauge: a round glass filling with water.
    {
        const vec3 gc = layout::GAUGE_CENTER;
        const float R = layout::GAUGE_R;
        const float fill = game.bonusActive() ? saturate(game.bonusLeft() / 10.f) : saturate(gaugeShown_);
        const float ready = game.bonusReady() || game.bonusActive() ? 1.f : 0.f;
        const vec3 col = pal_.piece[0];
        const float v[16] = {gc.x, gc.y, R, R, 0.f, 0.f, fill, ready, col.x, col.y, col.z, 1.f - 0.6f * smoothstepf(0.3f, 0.95f, bridge), 0.f, 0.f, 0.f, 0.f};
        glUseProgram(progSprite_);
        setMat(progSprite_, "uVP", vp_);
        set1f(progSprite_, "uTime", time_);
        set1f(progSprite_, "uPx", px_);
        set1f(progSprite_, "uLightPanel", pal_.lightPanel);
        glBindVertexArray(spriteVao_);
        glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);
    }
    drawHud(f.hud, f.panels);
    glDisable(GL_BLEND);

    // ---- Post.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(progPost_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneT_.tex);
    set1i(progPost_, "uScene", 0);
    set1f(progPost_, "uFade", saturate(f.fade));
    set1f(progPost_, "uDim", dimT_);
    ugl::drawFullscreen();

    if (timing) {
        glEndQuery(GL_TIME_ELAPSED);
        // the GPU's time for this universe's frame (the query of the frame before, so nothing stalls)
        if (timerFrame_ > 0) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(timer_[(timerFrame_ + 1) & 1], GL_QUERY_RESULT, &ns);
            timerAcc_ += ns * 1e-6;
            if (++timerN_ == 120) {
                std::printf("[sea] gpu %.2f ms/frame (%zu cubes, %zu particles, %zu boats, %zu critters)\n", timerAcc_ / timerN_,
                            bodies_.size(), parts_.size(), boats_.size(), critters_.size());
                timerAcc_ = 0.0;
                timerN_ = 0;
            }
        }
        timerFrame_++;
    }
}
