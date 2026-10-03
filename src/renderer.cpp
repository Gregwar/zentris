#include "renderer.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "shaders.hpp"
#include "stb_easy_font.h"
#include "stb_image_write.h"

namespace {

// 0 for night/dusk themes, 1 for pale ones; continuous during transitions.
float paleW(const Theme& t) { return smoothstepf(0.4f, 1.f, t.pale); }

// How see-through a block material is (outline-only materials need a calm backdrop to stay readable).
static float seeThrough(const Theme& t) {
    switch (t.blockStyle) {
    case BS_WIRE: case BS_NEON: case BS_DOUBLE: return 1.f;
    case BS_HOLO: return 0.6f;
    case BS_GLASS: return 0.9f * (1.f - t.fillAlpha);
    case BS_FRESNEL: return 1.f - 0.7f * t.fillAlpha;
    case BS_FROSTED: case BS_DOTS: return 0.35f;
    default: return 0.f;
    }
}

GLint U(GLuint p, const char* n) { return glGetUniformLocation(p, n); }
void set1f(GLuint p, const char* n, float v) { glUniform1f(U(p, n), v); }
void set1i(GLuint p, const char* n, int v) { glUniform1i(U(p, n), v); }
void set2f(GLuint p, const char* n, float a, float b) { glUniform2f(U(p, n), a, b); }
void set3f(GLuint p, const char* n, const vec3& v) { glUniform3f(U(p, n), v.x, v.y, v.z); }
void set4f(GLuint p, const char* n, float a, float b, float c, float d) { glUniform4f(U(p, n), a, b, c, d); }
void setMat(GLuint p, const char* n, const mat4& m) { glUniformMatrix4fv(U(p, n), 1, GL_FALSE, m.m); }

GLuint compileStage(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gl] %s %s shader error:\n%s\n", name, type == GL_VERTEX_SHADER ? "vertex" : "fragment",
                     log);
    }
    return s;
}

// Perceptual color blend (OKLab), so a hue change keeps its lightness and saturation instead of dipping
// through duller colors halfway as a linear RGB mix does.
vec3 toOklab(const vec3& c) {
    float l = std::cbrt(0.4122214708f * c.x + 0.5363325363f * c.y + 0.0514459929f * c.z);
    float m = std::cbrt(0.2119034982f * c.x + 0.6806995451f * c.y + 0.1073969566f * c.z);
    float s = std::cbrt(0.0883024619f * c.x + 0.2817188376f * c.y + 0.6299787005f * c.z);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s, 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}
vec3 fromOklab(const vec3& L) {
    float l = L.x + 0.3963377774f * L.y + 0.2158037573f * L.z, m = L.x - 0.1055613458f * L.y - 0.0638541728f * L.z,
          s = L.x - 0.0894841775f * L.y - 1.2914855480f * L.z;
    l = l * l * l; m = m * m * m; s = s * s * s;
    return {std::max(0.f, 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s),
            std::max(0.f, -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s),
            std::max(0.f, -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s)};
}
vec3 mixOklab(const vec3& a, const vec3& b, float t) {
    if (t <= 0.f) return a;
    if (t >= 1.f) return b;
    return fromOklab(lerp(toOklab(a), toOklab(b), t));
}

// Board geometry (world units, one cell = 1).
constexpr float BOARD_W = 10.f, BOARD_H = 20.f;

} // namespace

GLuint Renderer::compile(const char* vs, const char* fs, const char* name) {
    GLuint p = glCreateProgram();
    GLuint a = compileStage(GL_VERTEX_SHADER, vs, name), b = compileStage(GL_FRAGMENT_SHADER, fs, name);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gl] %s link error:\n%s\n", name, log);
    }
    glDeleteShader(a);
    glDeleteShader(b);
    return p;
}

bool Renderer::init(int width, int height) {
    std::string bg = std::string(shaders::BG_FS_HEAD) + shaders::NOISE_GLSL + shaders::BG_FS_BODY;
    progBg_ = compile(shaders::FULLSCREEN_VS, bg.c_str(), "background");
    progPart_ = compile(shaders::PARTICLE_VS, shaders::PARTICLE_FS, "particles");
    progBurst_ = compile(shaders::BURST_VS, shaders::PARTICLE_FS, "bursts");
    progBlock_ = compile(shaders::BLOCK_VS, shaders::BLOCK_FS, "blocks");
    progDown_ = compile(shaders::FULLSCREEN_VS, shaders::DOWN_FS, "bloom-down");
    progUp_ = compile(shaders::FULLSCREEN_VS, shaders::UP_FS, "bloom-up");
    progComp_ = compile(shaders::FULLSCREEN_VS, shaders::COMPOSITE_FS, "composite");
    progText_ = compile(shaders::TEXT_VS, shaders::TEXT_FS, "text");

    glGenVertexArrays(1, &emptyVao_);

    // Particle quad + random seeds.
    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    std::vector<float> seeds(MAX_PARTICLES * 4);
    Rng r(777);
    for (float& s : seeds) s = r.uniform();
    glGenBuffers(1, &seedVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, seedVbo_);
    glBufferData(GL_ARRAY_BUFFER, seeds.size() * sizeof(float), seeds.data(), GL_STATIC_DRAW);
    glGenVertexArrays(1, &partVao_);
    glBindVertexArray(partVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, seedVbo_);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glVertexAttribDivisor(1, 1);

    // Bursts.
    glGenBuffers(1, &burstVbo_);
    glGenVertexArrays(1, &burstVao_);
    glBindVertexArray(burstVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, burstVbo_);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
    glVertexAttribDivisor(2, 1);

    // Block meshes share one instance buffer.
    glGenBuffers(1, &instVbo_);
    buildMesh(cubeMesh_, true, 24.f);
    buildMesh(blockMesh_, false, 24.f);
    builtExp_ = 24.f;

    // Text.
    glGenVertexArrays(1, &textVao_);
    glGenBuffers(1, &textVbo_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::min(4, (int)maxSamples);
    resize(width, height);
    return glGetError() == GL_NO_ERROR;
}

// Blocks are superellipsoids |x|^e + |y|^e + |z|^e = 1 built on a subdivided cube, so every block shape
// is one continuous family (e=1 gem, 2 sphere, 3..7 rounded, ~24 cube) and shapes can morph smoothly.
void Renderer::buildMesh(Mesh& m, bool sharpCube, float e) {
    std::vector<float> v; // pos3 normal3 edge4
    std::vector<unsigned> idx;
    auto push = [&](vec3 p, vec3 n, float a, float b, float c, float w) {
        float d[10] = {p.x, p.y, p.z, n.x, n.y, n.z, a, b, c, w};
        v.insert(v.end(), d, d + 10);
    };
    const vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    {
        const int N = sharpCube ? 1 : 12;
        for (int f = 0; f < 6; f++) {
            vec3 n = axes[f];
            vec3 t1 = std::fabs(n.y) > 0.5f ? vec3(1, 0, 0) : vec3(0, 1, 0);
            vec3 t2 = cross(n, t1);
            unsigned base = (unsigned)(v.size() / 10);
            for (int j = 0; j <= N; j++)
                for (int i = 0; i <= N; i++) {
                    float u = -1 + 2.f * i / N, w = -1 + 2.f * j / N;
                    vec3 cp = n + t1 * u + t2 * w;
                    if (sharpCube) {
                        push(cp * 0.5f, n, u, w, 0, 0);
                    } else {
                        vec3 d = normalize(cp);
                        float sum = std::pow(std::fabs(d.x), e) + std::pow(std::fabs(d.y), e) + std::pow(std::fabs(d.z), e);
                        vec3 p = d / std::pow(sum, 1.f / e);
                        auto g = [&](float x) { return std::copysign(std::pow(std::fabs(x) + 1e-6f, e - 1.f), x); };
                        vec3 nn = normalize(vec3(g(p.x), g(p.y), g(p.z)));
                        // Near the cube limit, blend toward the face normal to keep flat faces crisp.
                        nn = normalize(lerp(nn, n, smoothstepf(8.f, 24.f, e) * 0.8f));
                        push(p * 0.5f, nn, u, w, 0, 0);
                    }
                }
            for (int j = 0; j < N; j++)
                for (int i = 0; i < N; i++) {
                    unsigned a = base + j * (N + 1) + i, b = a + 1, c = a + N + 1, d = c + 1;
                    if (dot(cross(t1, t2), n) > 0) idx.insert(idx.end(), {a, b, d, a, d, c});
                    else idx.insert(idx.end(), {a, d, b, a, c, d});
                }
        }
    }
    if (!m.vao) {
        glGenVertexArrays(1, &m.vao);
        glGenBuffers(1, &m.vbo);
        glGenBuffers(1, &m.ebo);
    }
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 40, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 40, (void*)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 40, (void*)24);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    const int stride = sizeof(BlockInst);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(BlockInst, pos));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(BlockInst, scale));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(BlockInst, color));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(BlockInst, params));
    for (int a = 3; a <= 6; a++) glVertexAttribDivisor(a, 1);
    glBindVertexArray(0);
    m.count = (int)idx.size();
}

void Renderer::resize(int width, int height) {
    w_ = std::max(1, width);
    h_ = std::max(1, height);
    createTargets();
}

void Renderer::createTargets() {
    if (msFbo_) {
        glDeleteFramebuffers(1, &msFbo_);
        glDeleteRenderbuffers(1, &msColor_);
        glDeleteRenderbuffers(1, &msDepth_);
        glDeleteFramebuffers(1, &sceneFbo_);
        glDeleteTextures(1, &sceneTex_);
        glDeleteFramebuffers(BLOOM_MIPS, bloomFbo_);
        glDeleteTextures(BLOOM_MIPS, bloomTex_);
    }
    glGenFramebuffers(1, &msFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glGenRenderbuffers(1, &msColor_);
    glBindRenderbuffer(GL_RENDERBUFFER, msColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA16F, w_, h_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
    glGenRenderbuffers(1, &msDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, msDepth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, w_, h_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "[gl] MSAA framebuffer incomplete\n");

    auto makeTex = [](GLuint& tex, int w, int h) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    glGenFramebuffers(1, &sceneFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    makeTex(sceneTex_, w_, h_);
    // Mip chain on the scene: its small levels give the average brightness for the exposure governor.
    sceneLevels_ = 1 + (int)std::floor(std::log2((float)std::max(w_, h_)));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex_, 0);

    int bw = w_, bh = h_;
    glGenFramebuffers(BLOOM_MIPS, bloomFbo_);
    for (int i = 0; i < BLOOM_MIPS; i++) {
        bw = std::max(1, bw / 2);
        bh = std::max(1, bh / 2);
        bloomW_[i] = bw;
        bloomH_[i] = bh;
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        makeTex(bloomTex_[i], bw, bh);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Transitions never interrupt each other (that would make whatever is half-faded pop): a new target
// waits for the running blend, which is gently shortened so the new one starts soon.
void Renderer::setTheme(const Theme& t, float seconds, bool wipe, float impact, int shape) {
    if (!hasTheme_) {
        from_ = to_ = cur_ = t;
        hasTheme_ = true;
        transT_ = 1.f;
        return;
    }
    if (transT_ < 1.f) {
        pending_ = t;
        pendingDur_ = std::max(0.01f, seconds);
        pendingWipe_ = wipe;
        pendingImpact_ = impact;
        pendingShape_ = shape;
        hasPending_ = true;
        float remaining = (1.f - transT_) * transDur_;
        if (remaining > 1.5f) transDur_ = 1.5f / (1.f - transT_);
        return;
    }
    from_ = to_;
    to_ = t;
    transT_ = 0.f;
    transDur_ = std::max(0.01f, seconds);
    wipe_ = wipe && seconds > 0.05f;
    impact_ = impact;
    wipeShape_ = shape;
    wipeSeed_ = rng_.range(0.f, 100.f);
    swell_ = std::max(swell_, impact);
}

void Renderer::setInverted(bool on) {
    if (on == inverted_) return;
    inverted_ = on;
    invT_ = 0.f;
    swell_ = std::max(swell_, on ? 0.8f : 0.4f);
    spawnRing(vec3(0, 0, 0.6f), 5.f, lerp(cur_.accent, cur_.partB, 0.4f), 180, 9.f, 1.6f, 0.15f);
}

// Where the transition front reaches first (0) to last (1), in screen space. Mirrors wipeCoord() in
// the shaders.
static float wipeCoord(int shape, float x, float y, float aspect, float noise) {
    float radial = std::sqrt(x * x * aspect * aspect + y * y) / std::sqrt(aspect * aspect + 1.f);
    switch (shape) {
    case WIPE_RISE: return y * 0.5f + 0.5f;
    case WIPE_FALL: return 0.5f - y * 0.5f;
    case WIPE_LEFT: return x * 0.5f + 0.5f;
    case WIPE_RIGHT: return 0.5f - x * 0.5f;
    case WIPE_INWARD: return 1.f - radial;
    case WIPE_CURTAINS: return 1.f - std::fabs(x);
    case WIPE_DISSOLVE: return noise;
    case WIPE_DIAGONAL: return (x + y) * 0.25f + 0.5f;
    case WIPE_SPIRAL: {
        float a = std::atan2(y, x * aspect) / TAU + 0.5f;
        return 0.6f * (a - std::floor(a)) + 0.4f * radial;
    }
    case WIPE_DIAMOND: return (std::fabs(x) + std::fabs(y)) * 0.5f;
    case WIPE_RISE_WAVE: return (y * 0.5f + 0.5f) * 0.85f + 0.075f * (1.f + std::sin(x * 9.f));
    case WIPE_FALL_WAVE: return (0.5f - y * 0.5f) * 0.85f + 0.075f * (1.f + std::sin(x * 7.f + 1.f));
    case WIPE_BLINDS: { float f = y * 3.f; return 0.35f * (f - std::floor(f)) + 0.65f * (y * 0.25f + 0.5f); }
    case WIPE_COLUMNS: { float f = x * 4.f; return 0.35f * (f - std::floor(f)) + 0.65f * (x * 0.25f + 0.5f); }
    case WIPE_PETALS: return radial * (0.8f + 0.2f * std::cos(std::atan2(y, x * aspect) * 5.f));
    case WIPE_CROSS: return std::min(std::fabs(x), std::fabs(y));
    case WIPE_SALTIRE: return std::min(std::fabs(x - y), std::fabs(x + y)) * 0.7f;
    case WIPE_CHECKER: return 0.5f * std::fmod(std::fabs(std::floor(x * 4.f) + std::floor(y * 3.f)), 2.f) + 0.45f * radial;
    case WIPE_CORNER: {
        float dx = (x + 1.f) * aspect, dy = y + 1.f;
        return std::sqrt(dx * dx + dy * dy) / (2.f * std::sqrt(aspect * aspect + 1.f));
    }
    case WIPE_SPLIT: return std::fabs(y);
    case WIPE_GRAIN: return noise;
    case WIPE_CLOUD_RISE: return noise * 0.55f + (y * 0.5f + 0.5f) * 0.45f;
    case WIPE_CLOUD_OPEN: return noise * 0.55f + radial * 0.45f;
    case WIPE_SMOKE: return noise;
    default: return radial;
    }
}

// 0 = old scene, 1 = new scene at this position.
float Renderer::wipeMix(const vec3& p) const {
    if (!wipe_ || transT_ >= 1.f) return mix_;
    vec4 c = vp_ * vec4(p, 1.f);
    float w = std::max(c.w, 1e-3f);
    // No per-position noise for blocks: the falling piece and settling rows move every frame, so a hash of
    // their position flickered their color between the two themes while the front passed.
    float d = saturate(wipeCoord(wipeShape_, c.x / w, c.y / w, (float)w_ / h_, 0.5f));
    return 1.f - smoothstepf(wipeFront_ - WIPE_W, wipeFront_, d);
}

vec3 Renderer::cellPos(float x, float y) const {
    return {x - (BOARD_W - 1) * 0.5f, (float)(Game::H - 1) - y - (BOARD_H - 1) * 0.5f, 0.f};
}

vec2 Renderer::project(const vec3& p) const {
    vec4 c = vp_ * vec4(p, 1.f);
    if (c.w <= 1e-4f) return {-1000, -1000};
    return {(c.x / c.w * 0.5f + 0.5f) * w_, (0.5f - c.y / c.w * 0.5f) * h_};
}

void Renderer::spawnBurst(vec3 pos, vec3 color, int n, float speed, float life, float size) {
    for (int i = 0; i < n && bursts_.size() < 8000; i++) {
        vec3 d = normalize(vec3(rng_.range(-1, 1), rng_.range(-1, 1), rng_.range(-1, 1)));
        Burst b;
        b.pos = pos + d * 0.3f;
        b.vel = d * speed * rng_.range(0.3f, 1.f);
        b.color = color;
        b.maxLife = b.life = life * rng_.range(0.6f, 1.f);
        b.size = size * rng_.range(0.5f, 1.2f);
        bursts_.push_back(b);
    }
}

// Particles launched outward in the board plane, as a ring.
void Renderer::spawnRing(vec3 center, float radius, vec3 color, int n, float speed, float life, float size) {
    for (int i = 0; i < n && bursts_.size() < 9000; i++) {
        float a = TAU * (i + rng_.uniform()) / n;
        vec3 d(std::cos(a), std::sin(a) * 0.8f, rng_.range(-0.1f, 0.1f));
        Burst b;
        b.pos = center + d * radius;
        b.vel = d * speed * rng_.range(0.85f, 1.15f);
        b.color = color;
        b.maxLife = b.life = life * rng_.range(0.7f, 1.f);
        b.size = size * rng_.range(0.7f, 1.2f);
        bursts_.push_back(b);
    }
}

void Renderer::levelUp() {
    spawnRing(vec3(0, 0, 0.6f), 5.f, lerp(cur_.accent, cur_.partB, 0.4f), 180, 9.f, 1.6f, 0.14f);
    spawnRing(vec3(0, 0, 0.6f), 3.f, cur_.partA, 120, 6.f, 1.4f, 0.12f);
}

void Renderer::onEvent(const GameEvent& ev, const Game&) {
    const Theme& t = cur_;
    switch (ev.type) {
    case GameEvent::Lock:
        for (auto& c : ev.cells) spawnBurst(cellPos((float)c.x, (float)c.y), t.piece[c.type], 1, 0.4f, 1.6f, 0.08f);
        break;
    case GameEvent::Clear: {
        // Cleared blocks disappear with one of the scene's 3 effects (a Tetris always gets a fuller one).
        int effect = t.clearEffects[rng_.next() % 3];
        if (ev.count >= 4) effect = rng_.chance(0.5f) ? CE_SCATTER : CE_SPARKLE;
        const bool fromRight = rng_.chance(0.5f);
        for (auto& c : ev.cells) {
            vec3 p = cellPos((float)c.x, (float)c.y);
            float x = (float)c.x, delay = 0.f, dur = 0.4f;
            vec3 dir(0, 0, 0);
            switch (effect) {
            case CE_SHRINK: delay = std::fabs(x - 4.5f) * 0.035f; break;
            case CE_RISE: delay = x * 0.02f; dur = 0.5f; break;
            case CE_SCATTER:
                dir = normalize(vec3((x - 4.5f) * 0.35f, rng_.range(-0.4f, 0.6f), rng_.range(0.6f, 1.2f)));
                dur = 0.5f;
                break;
            case CE_SQUASH: delay = std::fabs(x - 4.5f) * 0.015f; dur = 0.35f; break;
            case CE_SWEEP: delay = (fromRight ? 9.f - x : x) * 0.03f; dur = 0.3f; break;
            case CE_FOLD: delay = (4.5f - std::fabs(x - 4.5f)) * 0.035f; dur = 0.35f; break;
            case CE_MELT: delay = rng_.range(0.f, 0.12f); dur = 0.5f; break;
            case CE_SPARKLE: delay = rng_.range(0.f, 0.08f); dur = 0.22f; break;
            case CE_POUR: delay = rng_.range(0.f, 0.15f); dur = 0.5f; break;
            case CE_ZIP: dir = vec3(fromRight ? 1.f : -1.f, 0, 0); delay = (fromRight ? 9.f - x : x) * 0.012f; dur = 0.4f; break;
            case CE_BLOOM: delay = std::fabs(x - 4.5f) * 0.02f; dur = 0.45f; break;
            case CE_FLIP: delay = x * 0.025f; dur = 0.4f; break;
            case CE_DROPOUT: delay = rng_.range(0.f, 0.3f); dur = 0.18f; break;
            case CE_DOMINO: delay = (fromRight ? 9.f - x : x) * 0.04f; dur = 0.45f; break;
            case CE_IMPLODE: dir = vec3(4.5f - x, 0, 0); dur = 0.45f; break;
            case CE_SPREAD: dir = vec3(x - 4.5f, 0, 0); dur = 0.45f; break;
            case CE_WAVE: delay = x * 0.02f; dur = 0.55f; break;
            case CE_SLICE: delay = (fromRight ? 9.f - x : x) * 0.03f; dur = 0.3f; break;
            case CE_PULSE: dur = 0.5f; break;
            case CE_STRETCH: delay = std::fabs(x - 4.5f) * 0.02f; dur = 0.45f; break;
            case CE_SINK: delay = rng_.range(0.f, 0.1f); dur = 0.45f; break;
            case CE_CASCADE: delay = std::fabs(x - 4.5f) * 0.05f; dur = 0.4f; break;
            }
            dying_.push_back({p, c.type, 0.f, delay, dur, effect, dir});
            int parts = effect == CE_SPARKLE ? 10 + 2 * ev.count : 3 + ev.count;
            spawnBurst(p, t.piece[c.type], parts, (effect == CE_SPARKLE ? 1.8f : 1.0f) + 0.2f * ev.count, 2.6f, 0.12f);
        }
        // Rings of light from the cleared rows (a double ring for a Tetris).
        {
            float yc = 0;
            vec3 col(0.f);
            for (auto& c : ev.cells) { yc += cellPos(0, (float)c.y).y; col += t.piece[c.type]; }
            yc /= ev.cells.size();
            col = col / (float)ev.cells.size();
            spawnRing(vec3(0, yc, 0.4f), 1.f, lerp(col, t.accent, 0.3f), 50 + 25 * ev.count, 5.f + 1.5f * ev.count, 1.1f, 0.12f);
            if (ev.count >= 4) spawnRing(vec3(0, yc, 0.4f), 1.f, t.partB, 140, 11.f, 1.5f, 0.14f);
        }
        clearGlow_ = std::min(0.5f, clearGlow_ + 0.1f * ev.count);
        settleGlow_ = std::min(0.3f, settleGlow_ + 0.12f + 0.04f * ev.count);
        if (ev.count >= 4) {
            float yc = 0;
            for (auto& c : ev.cells) yc += cellPos(0, (float)c.y).y;
            startTetrisFx(yc / ev.cells.size(), ev.backToBack);
        }
        break;
    }
    case GameEvent::BonusReady:
        readyFlash_ = 1.f;
        break;
    case GameEvent::BonusStart:
        startSlowFx();
        break;
    case GameEvent::BonusEnd:
        slowOn_ = false;
        break;
    case GameEvent::TopOut:
        // Game over: the stack dissolves slowly, row by row from the top, with one of the scene's effects.
        {
            int topRow = Game::H;
            for (auto& c : ev.cells) topRow = std::min(topRow, c.y);
            int effect = t.clearEffects[rng_.next() % 3];
            if (effect == CE_SWEEP || effect == CE_SPARKLE) effect = CE_RISE;
            for (auto& c : ev.cells) {
                vec3 p = cellPos((float)c.x, (float)c.y);
                float delay = 0.3f + (c.y - topRow) * 0.11f + std::fabs(c.x - 4.5f) * 0.02f;
                vec3 dir = normalize(vec3((c.x - 4.5f) * 0.3f, rng_.range(-0.2f, 0.6f), rng_.range(0.5f, 1.f)));
                dying_.push_back({p, c.type, 0.f, delay, 0.9f, effect, dir});
                spawnBurst(p, t.piece[c.type], 2, 0.6f, 3.5f, 0.12f);
            }
        }
        break;
    default: break;
    }
}

// Equalizer levels at 48 bars (interpolated from the 16 analysed bands): fast attack, theme-specific
// decay, and peak caps that hold briefly then fall at a steady speed.
void Renderer::updateEqualizer(const MusicState& music, float dt) {
    const Theme& th = to_;
    for (int i = 0; i < EQ_MAX; i++) {
        float x = i * (NUM_BANDS - 1) / float(EQ_MAX - 1);
        int b0 = (int)x, b1 = std::min(NUM_BANDS - 1, b0 + 1);
        float v = lerpf(music.bandsFast[b0], music.bandsFast[b1], x - b0) * (0.6f + 0.6f * music.energy);
        v = saturate(v);
        eqBar_[i] = v > eqBar_[i] ? v : approach(eqBar_[i], v, th.eqDecay, dt);
        if (eqBar_[i] >= eqPeak_[i]) {
            eqPeak_[i] = eqBar_[i];
            eqHold_[i] = 0.35f;
        } else if ((eqHold_[i] -= dt) <= 0.f) {
            eqPeak_[i] = std::max(eqBar_[i], eqPeak_[i] - th.eqPeakFall * dt);
        }
    }
}

// Audio equalizer decoration: a layout (where) x a rendering (how) x resolution x coloring.
void Renderer::addEqualizer(const Theme& th, float weight, const MusicState& music, std::vector<BlockInst>& fx) {
    if (th.eqStyle == 0 || weight <= 0.01f) return;
    const float E = music.energy;
    const float halfW = BOARD_W * 0.5f + 0.15f, halfH = BOARD_H * 0.5f + 0.15f;
    const int N = std::clamp(th.eqBars, 8, EQ_MAX);
    float alpha = th.eqAlpha * (0.55f + 0.45f * E) * weight;
    auto level = [&](float u, const float* src) { // u in [0,1] along the spectrum
        float x = u * (EQ_MAX - 1);
        int i0 = (int)x, i1 = std::min(EQ_MAX - 1, i0 + 1);
        return lerpf(src[i0], src[i1], x - i0);
    };
    auto color = [&](float u, float h) {
        vec3 c = th.eqColor == 0 ? lerp(cur_.partA, cur_.partB, u)
               : th.eqColor == 1 ? lerp(cur_.partA, cur_.partB, h)
                                 : cur_.accent;
        return c * lerpf(1.15f, 1.f, paleW(cur_));
    };
    auto box = [&](vec3 c, vec3 sc, vec3 col, float a) {
        if (sc.x <= 0.f || sc.y <= 0.f || a <= 0.002f) return;
        BlockInst b;
        b.pos = c;
        b.scale = sc;
        b.color = vec4(col, a);
        b.params = vec4(2, 0, 0, 0);
        fx.push_back(b);
    };
    struct Bar { vec3 origin, dir, across; float lmax, width; };
    std::vector<Bar> bars;
    std::vector<float> us; // spectrum position of each bar
    // Layouts give, for each bar, where it starts, which way it grows, and its maximum length.
    if (th.eqStyle == 1) { // beside the board, bottom, both sides (lows near the board)
        float span = 12.5f, sp = span / N;
        for (int side = -1; side <= 1; side += 2)
            for (int i = 0; i < N; i++) {
                bars.push_back({vec3(side * (6.4f + (i + 0.5f) * sp), -halfH, -1.5f), vec3(0, 1, 0), vec3(1, 0, 0), 3.2f, sp * 0.6f});
                us.push_back(i / float(N - 1));
            }
    } else if (th.eqStyle == 2) { // along both sides of the board, growing outward
        float span = 2 * halfH - 1.f, sp = span / N;
        for (int side = -1; side <= 1; side += 2)
            for (int i = 0; i < N; i++) {
                bars.push_back({vec3(side * (halfW + 0.45f), -halfH + 0.5f + (i + 0.5f) * sp, -1.f), vec3((float)side, 0, 0),
                                vec3(0, 1, 0), 1.9f, sp * 0.65f});
                us.push_back(i / float(N - 1));
            }
    } else { // ring behind the board (mirrored halves)
        int n = N * 2;
        for (int i = 0; i < n; i++) {
            float ang = TAU * i / n + 0.5f * PI;
            vec3 d(std::cos(ang), std::sin(ang), 0);
            bars.push_back({d * 12.5f + vec3(0, 0, -3.f), d, vec3(-d.y, d.x, 0), 4.f, 0.3f});
            us.push_back(std::fabs(i / float(n) * 2.f - 1.f));
        }
    }
    const bool axisAligned = th.eqStyle != 3; // ring bars are not axis-aligned: draw them with squares
    int render = th.eqRender;
    if (!axisAligned && (render == 0 || render == 4)) render = 2;
    if (!axisAligned && render == 5) render = 3;
    vec3 prevTip;
    bool havePrev = false;
    for (size_t k = 0; k < bars.size(); k++) {
        const Bar& B = bars[k];
        const float u = us[k];
        const float v = level(u, eqBar_), pk = level(u, eqPeak_);
        const float L = 0.12f + B.lmax * v;
        const vec3 col = color(u, v);
        auto axisBox = [&](vec3 from, float len, float width, vec3 c, float a) {
            vec3 center = from + B.dir * (len * 0.5f);
            vec3 sc = vec3(std::fabs(B.dir.x) * len + std::fabs(B.across.x) * width,
                           std::fabs(B.dir.y) * len + std::fabs(B.across.y) * width, 0.15f);
            box(center, sc, c, a);
        };
        switch (render) {
        case 0: axisBox(B.origin, L, B.width, col, alpha); break;                              // bars
        case 1:                                                                                // bars + peak caps
            axisBox(B.origin, L, B.width, col, alpha * 0.8f);
            axisBox(B.origin + B.dir * (0.12f + B.lmax * pk + 0.08f), 0.1f, B.width, color(u, pk) * 1.15f, alpha);
            break;
        case 2: {                                                                              // LED segments
            const float seg = std::max(0.22f, B.lmax / 12.f);
            int n = std::max(1, (int)(L / seg));
            for (int j = 0; j < n; j++) {
                float h = (j + 0.5f) / (B.lmax / seg);
                vec3 c = B.origin + B.dir * ((j + 0.5f) * seg);
                float w = axisAligned ? B.width : 0.26f;
                box(c, vec3(axisAligned ? (std::fabs(B.dir.x) * seg * 0.7f + std::fabs(B.across.x) * w) : w,
                            axisAligned ? (std::fabs(B.dir.y) * seg * 0.7f + std::fabs(B.across.y) * w) : w, 0.12f),
                    th.eqColor == 1 ? color(u, h) : col, alpha);
            }
            break;
        }
        case 3: {                                                                              // line plot
            vec3 tip = B.origin + B.dir * L;
            box(tip, vec3(0.16f), col * 1.1f, alpha);
            // connect consecutive tips on the same side with small dots
            bool sameRow = havePrev && length(tip - prevTip) < 2.5f;
            if (sameRow)
                for (int j = 1; j < 4; j++) box(lerp(prevTip, tip, j / 4.f), vec3(0.09f), col, alpha * 0.8f);
            prevTip = tip;
            havePrev = true;
            break;
        }
        case 4: {                                                                              // mirrored around a center line
            vec3 center = B.origin + B.dir * (B.lmax * 0.5f);
            axisBox(center - B.dir * (L * 0.5f), L, B.width, col, alpha);
            break;
        }
        default:                                                                               // needles + peak dots
            axisBox(B.origin, L, B.width * 0.25f, col, alpha * 0.9f);
            box(B.origin + B.dir * (0.12f + B.lmax * pk + 0.1f), vec3(std::max(0.12f, B.width * 0.5f)), color(u, pk) * 1.15f, alpha);
            break;
        }
    }
}

void Renderer::collectBoard(const Game& g, const MusicState& music, double time, std::vector<BlockInst>& solid, std::vector<BlockInst>& ghost,
                            std::vector<BlockInst>& fx) {
    const Theme& t = cur_;
    const float s = t.blockScale;
    const vec3 base(0, t.boardY * 0.f, 0);
    auto block = [&](vec3 p, int type, float kind, float flash, float glow, float sc) {
        BlockInst b;
        b.pos = p + base;
        b.scale = vec3(s * sc);
        b.color = vec4(mixOklab(from_.piece[type], to_.piece[type], wipeMix(p)), 1);
        b.params = vec4(kind, flash, glow, 0);
        return b;
    };
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++) {
            const Cell& c = g.cell(x, y);
            if (c.type < 0) continue;
            // Beat wave travelling up the stack in energetic parts, and a small pop when a piece locks.
            float rowDelay = (Game::H - 1 - y) * 0.035f;
            float wave = music.beatPhase >= rowDelay ? std::exp(-(music.beatPhase - rowDelay) * 7.f) : 0.f;
            float waveGlow = 0.3f * music.energy * music.intensity * wave;
            solid.push_back(block(cellPos((float)x, y - g.rowOffset(y)), c.type, 0, c.flash, settleGlow_ + waveGlow,
                                  1.f + 0.35f * c.flash));
        }
    for (const Dying& d : dying_) {
        const float k = saturate((d.t - d.delay) / d.dur);   // disappearing phase 0..1
        const float e = k * k * (3.f - 2.f * k);
        const float rise = saturate(d.t / (d.delay + 0.12f)); // glow builds first
        vec3 pos = d.pos, sc(1.f - e);
        float glow = 0.5f * rise * (1.f - 0.5f * k);
        switch (d.effect) {
        case CE_RISE: pos.y += 1.4f * e; sc = vec3(1.f - 0.8f * e); break;
        case CE_SCATTER: pos += d.dir * (2.2f * e); sc = vec3(1.f - 0.9f * e); break;
        case CE_SQUASH: sc = vec3(1.f + 0.25f * e, 1.f - e, 1.f - 0.5f * e); glow *= 1.3f; break;
        case CE_SWEEP: glow = 0.8f * rise * (1.f - k); break;
        case CE_FOLD: sc = vec3(1.f - e, 1.f, 1.f - 0.3f * e); break;
        case CE_MELT: pos.y -= 0.45f * e; sc = vec3(1.f + 0.3f * e, 1.f - e, 1.f); break;
        case CE_SPARKLE: glow = 0.9f * rise; break;
        case CE_POUR: pos.y -= 5.f * k * k; sc = vec3(1.f - 0.6f * e); break;
        case CE_ZIP: pos += d.dir * (11.f * k * k); sc = vec3(1.f, 1.f - 0.5f * e, 1.f); break;
        case CE_BLOOM: {
            float g = k < 0.75f ? 1.f + 0.45f * smoothstepf(0, 0.75f, k) : 1.45f * (1.f - (k - 0.75f) / 0.25f);
            sc = vec3(g);
            glow *= 1.3f;
            break;
        }
        case CE_FLIP: sc = vec3(1.f, std::fabs(std::cos(k * PI * 1.5f)) * (1.f - k), 1.f); break;
        case CE_DROPOUT: sc = vec3(1.f - e); glow = 0.6f * rise; break;
        case CE_DOMINO: pos.y -= 3.f * k * k; pos.x += 0.4f * e * (d.pos.x > 0 ? 1.f : -1.f); sc = vec3(1.f - 0.7f * e); break;
        case CE_IMPLODE: pos.x += d.dir.x * e; sc = vec3(1.f - e); break;
        case CE_SPREAD: pos.x += d.dir.x * 0.5f * e; sc = vec3(1.f - e); break;
        case CE_WAVE: pos.y += 1.2f * e; pos.x += 0.35f * std::sin(k * 9.f + d.pos.x); sc = vec3(1.f - 0.85f * e); break;
        case CE_SLICE: sc = vec3(1.f - e, 1.f, 1.f); pos.x += 0.45f * e; break;
        case CE_PULSE: {
            float p = 0.5f + 0.5f * std::sin(k * TAU * 2.f);
            sc = vec3(k < 0.7f ? 1.f + 0.12f * p : 1.f - (k - 0.7f) / 0.3f);
            glow = 0.6f * p * (1.f - 0.5f * k);
            break;
        }
        case CE_STRETCH: sc = vec3(1.f - e, 1.f + 1.5f * e, 1.f - e * 0.5f); pos.y += 0.9f * e; break;
        case CE_SINK: pos.z -= 3.f * e; sc = vec3(1.f - 0.7f * e); break;
        case CE_CASCADE: pos.y += 0.6f * std::sin(k * PI); sc = vec3(1.f - e); glow *= 1.2f; break;
        default: break;
        }
        if (sc.x <= 0.01f || sc.y <= 0.01f || sc.z <= 0.01f) continue;
        BlockInst b = block(pos, d.type, 0, 0, glow, 1.f);
        b.scale = b.scale * sc;
        solid.push_back(b);
    }
    if (g.hasPiece()) {
        const Piece& p = g.piece();
        int cells[4][2];
        g.pieceCells(p, cells);
        float dx = g.pieceVisualX() - p.x, dy = g.pieceVisualY() - p.y;
        float glow = 0.12f;
        for (auto& c : cells) {
            if (c[1] < Game::HIDDEN - 1) continue;
            solid.push_back(block(cellPos(c[0] + dx, c[1] + dy), p.type, 0, 0, glow, 1));
        }
        Piece gp = p;
        gp.y = g.ghostY();
        if (gp.y > p.y) {
            g.pieceCells(gp, cells);
            for (auto& c : cells) ghost.push_back(block(cellPos((float)c[0], (float)c[1]), p.type, 1, 0, 0, 1));
        }
    }
    // Previews: hold on the left, next queue on the right.
    auto preview = [&](int type, vec3 center, float sc) {
        Piece p{type, 0, 0, 0};
        int cells[4][2];
        g.pieceCells(p, cells);
        float minx = 9, maxx = -9, miny = 9, maxy = -9;
        for (auto& c : cells) {
            minx = std::min(minx, (float)c[0]); maxx = std::max(maxx, (float)c[0]);
            miny = std::min(miny, (float)c[1]); maxy = std::max(maxy, (float)c[1]);
        }
        for (auto& c : cells) {
            vec3 pos = center + vec3((c[0] - (minx + maxx) * 0.5f) * sc, -(c[1] - (miny + maxy) * 0.5f) * sc, 0);
            BlockInst b = block(pos, type, 3, 0, 0, sc);
            if (g.holdUsed() && center.x < 0) b.color = vec4(t.piece[type] * 0.35f, 1);
            solid.push_back(b);
        }
    };
    if (g.holdType() >= 0) preview(g.holdType(), vec3(-8.6f, 7.2f, 0) + base, 0.62f);
    for (int i = 0; i < 3; i++) preview(g.next(i), vec3(8.6f, 7.2f - i * 3.2f, 0) + base, i == 0 ? 0.62f : 0.5f);

    // Frame and board decoration (emissive). Both frame styles are drawn during a transition.
    const float halfW = BOARD_W * 0.5f + 0.15f, halfH = BOARD_H * 0.5f + 0.15f;
    const float th = 0.07f;
    vec3 fc = t.accent * lerpf(1.3f + 0.4f * clearGlow_, 1.f, paleW(t)) * (1.f + 0.6f * music.beatPulse * music.energy);
    auto bar = [&](vec3 c, vec3 sc, vec3 col, float a, float normalBlend = 0.f) {
        if (a <= 0.002f) return;
        BlockInst b;
        b.pos = c + base;
        b.scale = sc;
        b.color = vec4(col, a);
        b.params = vec4(2, 0, 0, normalBlend);
        fx.push_back(b);
    };
    // Backplate (always first): improves legibility over busy backgrounds.
    //            With outline-type blocks over a bright scene it turns darker and nearly opaque.
    vec3 plate = t.bgTop * 0.3f;
    {
        const float lum = luminance(plate), cap = lerpf(0.06f, 0.012f, legible_);
        if (lum > cap) plate = plate * (cap / lum);
    }
    plate = lerp(plate, vec3(1.f), paleW(t));
    fx.clear();
    BlockInst pl;
    pl.pos = vec3(0, 0, -0.62f) + base;
    pl.scale = vec3(BOARD_W + 0.3f, BOARD_H + 0.3f, 0.01f);
    pl.color = vec4(plate, lerpf(lerpf(0.7f, 0.4f, paleW(t)), lerpf(0.94f, 0.82f, paleW(t)), legible_));
    pl.params = vec4(2, 0, 0, 1);
    fx.push_back(pl);
    auto frame = [&](int style, float fa) {
        switch (style) {
        case FR_OUTLINE:
            bar({-halfW, 0, 0}, {th, 2 * halfH + th, th}, fc, fa);
            bar({halfW, 0, 0}, {th, 2 * halfH + th, th}, fc, fa);
            bar({0, -halfH, 0}, {2 * halfW, th, th}, fc, fa);
            bar({0, halfH, 0}, {2 * halfW, th, th}, fc, fa * 0.5f);
            break;
        case FR_CORNERS:
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2) {
                    bar({sx * halfW, sy * (halfH - 0.75f), 0}, {th, 1.5f, th}, fc, fa);
                    bar({sx * (halfW - 0.75f), sy * halfH, 0}, {1.5f, th, th}, fc, fa);
                }
            break;
        case FR_WELL:
            bar({-halfW, -0.5f, 0}, {th, 2 * halfH - 1.f, th}, fc, fa);
            bar({halfW, -0.5f, 0}, {th, 2 * halfH - 1.f, th}, fc, fa);
            bar({0, -halfH, 0}, {2 * halfW + th, th * 1.5f, th}, fc, fa);
            break;
        case FR_FLOOR:
            bar({0, -halfH, 0}, {2 * halfW + 3.f, th * 1.5f, th}, fc, fa);
            bar({-halfW, 0, -0.5f}, {th * 0.6f, 2 * halfH, th}, fc, fa * 0.25f);
            bar({halfW, 0, -0.5f}, {th * 0.6f, 2 * halfH, th}, fc, fa * 0.25f);
            break;
        case FR_GRID:
            for (int y = 0; y <= (int)BOARD_H; y++)
                for (int x = 0; x <= (int)BOARD_W; x++)
                    bar({x - BOARD_W * 0.5f, y - BOARD_H * 0.5f, -0.58f}, {0.08f, 0.08f, 0.01f}, fc, fa * 0.25f); // faint: only a hint of the cells
            break;
        case FR_PILLARS:
            bar({-halfW, 0, 0}, {th * 0.7f, 90.f, th}, fc, fa * 0.8f);
            bar({halfW, 0, 0}, {th * 0.7f, 90.f, th}, fc, fa * 0.8f);
            break;
        case FR_DOUBLE:
            for (int k = 0; k < 2; k++) {
                float o = k * 0.35f, aa = fa * (k ? 0.45f : 1.f);
                bar({-halfW - o, 0, 0}, {th, 2 * (halfH + o) + th, th}, fc, aa);
                bar({halfW + o, 0, 0}, {th, 2 * (halfH + o) + th, th}, fc, aa);
                bar({0, -halfH - o, 0}, {2 * (halfW + o), th, th}, fc, aa);
                bar({0, halfH + o, 0}, {2 * (halfW + o), th, th}, fc, aa * 0.5f);
            }
            break;
        case FR_GLOWBASE:
            bar({0, -halfH, 0}, {2 * halfW + 1.f, th * 3.f, th}, fc, fa * 0.8f);
            bar({0, -halfH + 0.25f, 0}, {2 * halfW + 2.f, th, th}, fc, fa * 0.4f);
            bar({-halfW, 0, -0.3f}, {th * 0.5f, 2 * halfH, th}, fc, fa * 0.2f);
            bar({halfW, 0, -0.3f}, {th * 0.5f, 2 * halfH, th}, fc, fa * 0.2f);
            break;
        case FR_TOPBOTTOM:
            bar({0, -halfH, 0}, {2 * halfW, th, th}, fc, fa);
            bar({0, halfH, 0}, {2 * halfW, th, th}, fc, fa * 0.6f);
            for (int sx = -1; sx <= 1; sx += 2) {
                bar({sx * halfW, -halfH + 0.4f, 0}, {th, 0.8f, th}, fc, fa);
                bar({sx * halfW, halfH - 0.4f, 0}, {th, 0.8f, th}, fc, fa * 0.6f);
            }
            break;
        case FR_TICKS:
            for (int y = 0; y <= (int)BOARD_H; y++) {
                float len = y % 5 == 0 ? 0.6f : 0.3f;
                bar({-halfW - len * 0.5f, y - BOARD_H * 0.5f, 0}, {len, th * 0.8f, th}, fc, fa * 0.8f);
                bar({halfW + len * 0.5f, y - BOARD_H * 0.5f, 0}, {len, th * 0.8f, th}, fc, fa * 0.8f);
            }
            bar({0, -halfH, 0}, {2 * halfW, th, th}, fc, fa * 0.6f);
            break;
        case FR_SIDEFADE:
            for (int k = 0; k < 10; k++) {
                float y0 = -halfH + k * (2 * halfH / 10.f);
                float aa = fa * (1.f - k / 10.f);
                bar({-halfW, y0 + halfH / 10.f, 0}, {th, 2 * halfH / 10.f - 0.05f, th}, fc, aa);
                bar({halfW, y0 + halfH / 10.f, 0}, {th, 2 * halfH / 10.f - 0.05f, th}, fc, aa);
            }
            bar({0, -halfH, 0}, {2 * halfW, th, th}, fc, fa);
            break;
        case FR_UNDERLINE:
            bar({0, -halfH - 0.2f, 0}, {2 * halfW + 8.f, th * 0.8f, th}, fc, fa * 0.7f);
            break;
        case FR_CORNERDOTS:
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2) bar({sx * halfW, sy * halfH, 0}, {0.3f, 0.3f, 0.3f}, fc, fa);
            break;
        case FR_RAILS:
            for (int sx = -1; sx <= 1; sx += 2) {
                bar({sx * halfW, 0, 0}, {th * 0.6f, 2 * halfH, th}, fc, fa);
                bar({sx * (halfW + 0.3f), 0, 0}, {th * 0.6f, 2 * halfH - 1.f, th}, fc, fa * 0.5f);
            }
            break;
        case FR_DASHED:
            for (float y = -halfH + 0.3f; y < halfH; y += 0.8f) {
                bar({-halfW, y, 0}, {th, 0.4f, th}, fc, fa);
                bar({halfW, y, 0}, {th, 0.4f, th}, fc, fa);
            }
            for (float x = -halfW + 0.3f; x < halfW; x += 0.8f) bar({x, -halfH, 0}, {0.4f, th, th}, fc, fa);
            break;
        case FR_DOTPILLARS:
            for (float y = -40.f; y <= 40.f; y += 0.7f) {
                float aa = fa * (1.f - smoothstepf(halfH, 30.f, std::fabs(y)));
                bar({-halfW, y, 0}, {0.1f, 0.1f, 0.1f}, fc, aa);
                bar({halfW, y, 0}, {0.1f, 0.1f, 0.1f}, fc, aa);
            }
            break;
        case FR_DOTTED:
            for (float y = -halfH; y <= halfH + 0.01f; y += 0.5f) {
                bar({-halfW, y, 0}, {0.1f, 0.1f, 0.1f}, fc, fa);
                bar({halfW, y, 0}, {0.1f, 0.1f, 0.1f}, fc, fa);
            }
            for (float x = -halfW; x <= halfW + 0.01f; x += 0.5f) bar({x, -halfH, 0}, {0.1f, 0.1f, 0.1f}, fc, fa);
            break;
        default: break;
        }
    };
    // Equalizer decoration (both themes' layouts during a transition).
    const bool sameEq = from_.eqStyle == to_.eqStyle && from_.eqRender == to_.eqRender && from_.eqBars == to_.eqBars &&
                        from_.eqColor == to_.eqColor;
    if (sameEq) addEqualizer(to_, 1.f, music, fx);
    else {
        addEqualizer(from_, 1.f - mix_, music, fx);
        addEqualizer(to_, mix_, music, fx);
    }
    if (from_.frameStyle == to_.frameStyle) {
        frame(to_.frameStyle, t.frameAlpha);
    } else {
        frame(from_.frameStyle, t.frameAlpha * (1.f - mix_));
        frame(to_.frameStyle, t.frameAlpha * mix_);
    }
}

void Renderer::drawBackground(const MusicState& music, double time) {
    glUseProgram(progBg_);
    const float m = smoothstepf(0, 1, transT_);
    set1i(progBg_, "uStyleA", from_.bgStyle);
    set1i(progBg_, "uStyleB", to_.bgStyle);
    set1f(progBg_, "uMix", m);
    // Without a wipe both color sets are the blended ones (plain crossfade); with a wipe each side
    // keeps its own colors and the shader mixes them by distance from the board.
    const Theme& ca = wipe_ ? from_ : cur_;
    const Theme& cb = wipe_ ? to_ : cur_;
    set3f(progBg_, "uTopA", ca.bgTop);
    set3f(progBg_, "uBottomA", ca.bgBottom);
    set3f(progBg_, "uGlowA", ca.bgGlow);
    set3f(progBg_, "uTopB", cb.bgTop);
    set3f(progBg_, "uBottomB", cb.bgBottom);
    set3f(progBg_, "uGlowB", cb.bgGlow);
    set1i(progBg_, "uWipeShape", wipeShape_);
    set1f(progBg_, "uWipeSeed", wipeSeed_);
    set4f(progBg_, "uWipe", wipe_ && transT_ < 1.f ? 1.f : 0.f, wipeFront_, WIPE_W,
          std::sin(PI * transT_) * (1.f + 2.f * impact_));
    set4f(progBg_, "uP", cur_.bgP[0], cur_.bgP[1], cur_.bgP[2], cur_.bgP[3]);
    set1f(progBg_, "uTime", (float)time);
    set1f(progBg_, "uBass", music.audio.bass * cur_.bassReact);
    set1f(progBg_, "uIntensity", music.intensity * music.glow);
    set1f(progBg_, "uBeat", music.beatPulse * cur_.beatPulse);
    set1f(progBg_, "uAspect", (float)w_ / h_);
    set1f(progBg_, "uPale", paleW(cur_));
    // Light rays: both themes' ray patterns during a transition.
    const float rayGain = (0.4f + 0.8f * music.energy) * (0.6f + 0.6f * music.intensity) * govern_;
    set4f(progBg_, "uRays", from_.rays * (1.f - mix_) * rayGain, from_.rayCount, to_.rays * mix_ * rayGain, to_.rayCount);
    set1f(progBg_, "uRayTime", rayTime_);
    const Theme& sA = wipe_ && transT_ < 1.f ? from_ : (transT_ < 1.f ? from_ : to_);
    set4f(progBg_, "uSurfA", (float)sA.surfStyle, sA.surfAmt * (transT_ < 1.f && !wipe_ ? 1.f - mix_ : 1.f), sA.surfScale, 0.f);
    set4f(progBg_, "uSurfB", (float)to_.surfStyle, to_.surfAmt * (transT_ < 1.f && !wipe_ ? mix_ : 1.f), to_.surfScale, 0.f);
    set1f(progBg_, "uSurfTime", surfTime_);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Renderer::drawParticleLayer(const ParticleLayer& L, const Theme& owner, float weight, bool side,
                                 const MusicState& music) {
    if (weight <= 0.003f && !(wipe_ && transT_ < 1.f)) return;
    float mult;
    switch (L.style) {
    case PS_BOKEH: mult = 0.009f; break;
    case PS_WARP: mult = 0.25f; break;
    case PS_TUNNEL: mult = 0.5f; break;
    case PS_DRIFT: mult = 0.35f; break;
    case PS_HALOS: mult = 0.45f; break;
    case PS_SPHERE: mult = 0.5f; break;
    case PS_HELIX: mult = 0.35f; break;
    case PS_STREAMS: mult = 0.6f; break;
    case PS_FIREFLIES: mult = 0.05f; break;
    case PS_RAIN: mult = 0.2f; break;
    case PS_VORTEX: mult = 0.5f; break;
    case PS_WAVEFORM: mult = 0.25f; break;
    case PS_STARBURST: mult = 0.35f; break;
    case PS_ORBITS: mult = 0.4f; break;
    case PS_CONFETTI: mult = 0.08f; break;
    case PS_SNOWGLOBE: mult = 0.3f; break;
    case PS_LADDER: mult = 0.35f; break;
    case PS_FOUNTAIN: mult = 0.25f; break;
    case PS_PETALS: mult = 0.06f; break;
    case PS_CONSTELLATION: mult = 0.15f; break;
    case PS_TORUS: mult = 0.5f; break;
    case PS_WALL: mult = 0.5f; break;
    case PS_COMETS: mult = 0.03f; break;
    case PS_SPARKLERS: mult = 0.15f; break;
    case PS_BUBBLES: mult = 0.04f; break;
    case PS_CUBESHELL: mult = 0.3f; break;
    case PS_LEMNISCATE: mult = 0.4f; break;
    case PS_BEATRINGS: mult = 0.3f; break;
    case PS_PLASMA: mult = 0.25f; break;
    case PS_MOIRE: mult = 0.4f; break;
    case PS_SWARM: mult = 0.35f; break;
    case PS_SPIRALS: mult = 0.35f; break;
    case PS_RIBBON: mult = 0.3f; break;
    case PS_METEORS: mult = 0.02f; break;
    case PS_SKYLINE: mult = 0.4f; break;
    case PS_CORONA: mult = 0.3f; break;
    case PS_DUNES: mult = 0.4f; break;
    case PS_WIND: mult = 0.12f; break;
    case PS_FIRE: mult = 0.3f; break;
    case PS_SEA: mult = 0.4f; break;
    default: mult = 1.f; break;
    }
    int count = (int)(MAX_PARTICLES * L.count * mult);
    if (L.style == PS_WAVES || L.style == PS_WALL || L.style == PS_PLASMA || L.style == PS_DUNES || L.style == PS_SEA) {
        int n = (int)std::sqrt((float)count);
        n = std::clamp(n, 60, 150);
        count = n * n;
    } else if (L.style == PS_LATTICE) {
        int n = 8 + (int)(L.count * 6);
        count = n * n * n;
    } else if (L.style == PS_SKYLINE) { // 64 columns of dots
        count = 64 * std::clamp(count / 64, 60, 160);
    }
    count = std::clamp(count, 16, MAX_PARTICLES);

    GLuint p = progPart_;
    glUseProgram(p);
    setMat(p, "uVP", vp_);
    setMat(p, "uView", view_);
    set1f(p, "uTime", ptimeFrom_ * L.speed);
    set1f(p, "uSize", L.size * lerpf(1.f, 1.3f, paleW(cur_)));
    set1f(p, "uBright", L.bright * lerpf(1.f, 1.4f, paleW(cur_)) * std::sqrt(music.glow) * std::max(1.f, music.density));
    set1f(p, "uDensity", std::min(1.f, music.density));
    set1f(p, "uCount", (float)count);
    set1f(p, "uAspect", (float)w_ / h_);
    set1f(p, "uP11", proj_.at(1, 1));
    set1f(p, "uPixel", 2.f / h_);
    set1f(p, "uIntensity", music.intensity);
    set1i(p, "uStyle", L.style);
    set1i(p, "uShape", L.shape);
    set4f(p, "uP", L.p[0], L.p[1], L.p[2], L.p[3]);
    set1f(p, "uBass", music.audio.bass);
    set1f(p, "uMid", music.audio.mid);
    set1f(p, "uHigh", music.audio.high);
    set1f(p, "uLoud", music.audio.loud);
    set1f(p, "uBeat", music.beatPulse * cur_.beatPulse);
    set1f(p, "uKick", music.kick * (0.6f + 0.8f * music.energy));
    set2f(p, "uReact", cur_.bassReact, cur_.highReact);
    glUniform1fv(U(p, "uSpec"), 16, music.audio.bands.data());
    const bool wiping = wipe_ && transT_ < 1.f;
    const Theme& ct = wiping ? owner : cur_;
    set3f(p, "uColA", ct.partA);
    set3f(p, "uColB", ct.partB);
    set3f(p, "uColC", ct.partC);
    set1f(p, "uWeight", wiping ? 1.f : weight);
    set1f(p, "uSide", side ? 1.f : 0.f);
    set4f(p, "uWipe", wiping ? 1.f : 0.f, wipeFront_, WIPE_W, impact_ * std::sin(PI * transT_));
    set1i(p, "uWipeShape", wipeShape_);
    set1f(p, "uWipeSeed", wipeSeed_);
    set1f(p, "uPale", paleW(cur_));
    set1f(p, "uBoardDim", lerpf(0.8f, 0.97f, legible_));
    glBindVertexArray(partVao_);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
}

void Renderer::drawBlocks(const std::vector<BlockInst>& inst, bool depthWrite) {
    if (inst.empty()) return;
    glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(BlockInst), inst.data(), GL_STREAM_DRAW);
    const Mesh& m = cur_.blockMesh < 0 ? cubeMesh_ : blockMesh_;
    glBindVertexArray(m.vao);
    glDrawElementsInstanced(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr, (GLsizei)inst.size());
}

void Renderer::drawBursts() {
    if (bursts_.empty() && glints_.empty()) return;
    const float pw = paleW(cur_);
    std::vector<float> burstData, glintData;
    burstData.reserve(bursts_.size() * 8);
    for (auto& b : bursts_) {
        float k = b.life / b.maxLife;
        float a = k * k * lerpf(1.1f, 0.8f, pw);
        vec3 c = lerp(lerp(b.color, vec3(1.f), 0.25f), b.color * 0.9f, pw);
        float d[8] = {b.pos.x, b.pos.y, b.pos.z, b.size * (0.5f + 0.5f * k), c.x, c.y, c.z, a};
        burstData.insert(burstData.end(), d, d + 8);
    }
    for (auto& g : glints_) {
        if (g.alpha <= 0.003f) continue;
        vec3 c = lerp(lerp(g.color, vec3(1.f), 0.15f), g.color * 0.9f, pw);
        float d[8] = {g.pos.x, g.pos.y, g.pos.z, g.size, c.x, c.y, c.z, g.alpha * lerpf(1.f, 0.8f, pw)};
        glintData.insert(glintData.end(), d, d + 8);
    }
    glUseProgram(progBurst_);
    setMat(progBurst_, "uVP", vp_);
    set1f(progBurst_, "uAspect", (float)w_ / h_);
    set1f(progBurst_, "uP11", proj_.at(1, 1));
    set1f(progBurst_, "uWeight", 1.f);
    set1f(progBurst_, "uPale", pw);
    glBindVertexArray(burstVao_);
    glBindBuffer(GL_ARRAY_BUFFER, burstVbo_);
    auto draw = [&](const std::vector<float>& data, int shape) {
        if (data.empty()) return;
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STREAM_DRAW);
        set1i(progBurst_, "uShape", shape);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)(data.size() / 8));
    };
    int shape = cur_.layers[0].shape;
    if (shape == SH_STREAK || shape == SH_DISC || shape == SH_RING) shape = SH_DOT;
    draw(burstData, shape);
    draw(glintData, SH_DOT); // procedural animations: always soft dots, whatever the scene's particle shape
}

void Renderer::drawText(const std::vector<HudText>& hud) {
    glUseProgram(progText_);
    set2f(progText_, "uRes", (float)w_, (float)h_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    static char quadBuf[200000];
    const vec3 tc = cur_.text, ac = lerp(lerp(cur_.accent, vec3(1.f), 0.2f), cur_.accent, paleW(cur_));
    auto toSrgb = [](vec3 c) {
        return vec3(std::pow(saturate(c.x), 1 / 2.2f), std::pow(saturate(c.y), 1 / 2.2f), std::pow(saturate(c.z), 1 / 2.2f));
    };
    for (const HudText& h : hud) {
        if (h.alpha <= 0.01f || h.text.empty()) continue;
        std::vector<char> txt(h.text.begin(), h.text.end());
        txt.push_back(0);
        unsigned char col[4] = {255, 255, 255, 255};
        stb_easy_font_spacing(0.5f);
        int nq = stb_easy_font_print(0, 0, txt.data(), col, quadBuf, sizeof(quadBuf));
        float width = (float)stb_easy_font_width(txt.data()) * h.scale;
        float ox = h.x;
        if (h.center) ox = h.x - width * 0.5f;
        else if (h.x < 0) ox = w_ + h.x - width;
        std::vector<float> tri;
        tri.reserve(nq * 12);
        for (int q = 0; q < nq; q++) {
            float* v = (float*)(quadBuf + q * 64);
            float px[4], py[4];
            for (int k = 0; k < 4; k++) {
                px[k] = std::round(ox + v[k * 4] * h.scale);
                py[k] = std::round(h.y + v[k * 4 + 1] * h.scale);
            }
            const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int o : order) { tri.push_back(px[o]); tri.push_back(py[o]); }
        }
        glBufferData(GL_ARRAY_BUFFER, tri.size() * sizeof(float), tri.data(), GL_STREAM_DRAW);
        vec3 c = toSrgb(h.accent ? ac : tc);
        // Reversed screen (slow-motion bonus): reverse the text too, once the front has covered most of it.
        if (inverted_ == (invT_ > 0.35f)) c = vec3(1.f) - c * 0.92f - vec3(0.04f);
        glUniform4f(U(progText_, "uColor"), c.x, c.y, c.z, h.alpha);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(tri.size() / 2));
    }
}

void Renderer::render(const Game& game, const MusicState& music, double time, float dt, bool paused,
                      const std::vector<HudText>& hud, float fade) {
    // ---- Theme transition.
    transT_ = std::min(1.f, transT_ + dt / transDur_);
    if (transT_ >= 1.f && hasPending_) {
        hasPending_ = false;
        from_ = to_;
        to_ = pending_;
        transT_ = 0.f;
        transDur_ = pendingDur_;
        wipe_ = pendingWipe_;
        impact_ = pendingImpact_;
        wipeShape_ = pendingShape_;
        wipeSeed_ = rng_.range(0.f, 100.f);
        swell_ = std::max(swell_, impact_);
    }
    invT_ += dt;
    const float m = smoothstepf(0, 1, transT_);
    mix_ = m;
    wipeFront_ = m * (1.f + WIPE_W);
    cur_ = blendThemes(from_, to_, m);
    // Block shape morphs continuously (log-space blend of the superellipsoid exponent).
    if (std::fabs(builtExp_ - cur_.meshExp) > 0.002f * cur_.meshExp) {
        buildMesh(blockMesh_, false, cur_.meshExp);
        builtExp_ = cur_.meshExp;
    }
    pauseFade_ = approach(pauseFade_, paused ? 1.f : (dim_ ? 0.6f : 0.f), paused ? 4.f : 1.5f, dt);

    // ---- Simulation of effects.
    if (!paused) {
        glints_.clear(); // kept while paused, so the animation freezes instead of vanishing
        updateSpecialFx(game, music, dt);
        // The slow-motion bonus slows the whole world down, not only the song.
        const float world = lerpf(1.f, 0.3f, slow_), wdt = dt * world;
        ptimeFrom_ += wdt * (0.5f + 0.9f * music.intensity) * music.speed * (0.75f + 0.7f * music.energy);
        rayTime_ += wdt * (0.1f + 0.5f * music.energy);
        surfTime_ += wdt * (0.4f + 0.8f * music.energy) * (0.6f + 0.6f * music.intensity);
        for (size_t i = 0; i < dying_.size();) {
            dying_[i].t += dt;
            if (dying_[i].t > dying_[i].delay + dying_[i].dur + 0.05f) { dying_[i] = dying_.back(); dying_.pop_back(); continue; }
            i++;
        }
        for (size_t i = 0; i < bursts_.size();) {
            Burst& b = bursts_[i];
            b.life -= wdt;
            if (b.life <= 0) { b = bursts_.back(); bursts_.pop_back(); continue; }
            b.vel = b.vel * std::exp(-1.2f * wdt) + vec3(0, 0.25f, 0) * wdt;
            b.pos += b.vel * wdt;
            i++;
        }
    }
    kick_ = approach(kick_, 0.f, 3.5f, dt);
    clearGlow_ = approach(clearGlow_, 0.f, 0.5f, dt);
    settleGlow_ = approach(settleGlow_, 0.f, 1.2f, dt);
    swell_ = approach(swell_, 0.f, 0.9f, dt); // glow swell after a rising transition, ~1.5 s

    // ---- Camera.
    const float aspect = (float)w_ / h_;
    float swayT = (float)time * cur_.swaySpeed * TAU;
    // Front view, never tilted: motion is limited to a slow breathing zoom and beat kicks.
    float yaw = 0.f, pitch = 0.f;
    float dist = cur_.camDist * (1.f + 0.015f * cur_.sway * std::sin(swayT));
    // Keep the board (and side panels) inside narrow windows.
    float needW = 23.f / aspect * 1.05f;
    float visH = 2.f * dist * std::tan(cur_.fov * 0.5f * PI / 180.f);
    if (visH < needW) dist *= needW / visH;
    vec3 target(0, cur_.boardY + 0.4f, 0); // slightly above center: the spawn row sits above the frame
    camPos_ = target + vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)) * dist;
    vec3 up(0, 1, 0);
    view_ = mat4::lookAt(camPos_, target, up);
    proj_ = mat4::perspective(cur_.fov * PI / 180.f, aspect, 0.5f, 600.f);
    vp_ = proj_ * view_;

    // ---- Scene pass (MSAA HDR).
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glViewport(0, 0, w_, h_);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    drawBackground(music, time);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (transT_ < 1.f)
        for (int i = 0; i < from_.layerCount; i++) drawParticleLayer(from_.layers[i], from_, 1.f - m, false, music);
    for (int i = 0; i < to_.layerCount; i++) drawParticleLayer(to_.layers[i], to_, m, true, music);

    std::vector<BlockInst> solid, ghost, fx;
    updateEqualizer(music, paused ? 0.f : dt);
    collectBoard(game, music, time, solid, ghost, fx);
    if (!paused) addGauge(game, time); // glints are kept as they are while paused
    glUseProgram(progBlock_);
    setMat(progBlock_, "uVP", vp_);
    set1f(progBlock_, "uDepth", cur_.blockDepth);
    set1i(progBlock_, "uStyleA", from_.blockStyle);
    set1i(progBlock_, "uStyleB", to_.blockStyle);
    set1f(progBlock_, "uStyleMix", from_.blockStyle == to_.blockStyle ? 1.f : m);
    set1f(progBlock_, "uEdgeW", cur_.edgeWidth);
    set1f(progBlock_, "uEmissive", cur_.emissive * (1.f + 0.25f * music.intensity));
    set1f(progBlock_, "uFill", cur_.fillAlpha);
    set1f(progBlock_, "uGhost", cur_.ghostAlpha);
    set1f(progBlock_, "uPale", paleW(cur_));
    set1f(progBlock_, "uLegible", legible_);
    set1f(progBlock_, "uBeat", music.beatPulse * cur_.beatPulse);
    set1f(progBlock_, "uTime", (float)time);
    set3f(progBlock_, "uCamPos", camPos_);
    set3f(progBlock_, "uLightDir", normalize(vec3(cur_.bgP[3] - 0.5f, 0.9f, 0.7f)));
    // Backplate first (no depth write), then the solid blocks, then translucent helpers.
    // Helpers output premultiplied color: additive on dark themes, normal blending on pale ones.
    std::vector<BlockInst> plate(fx.begin(), fx.begin() + 1), rest(fx.begin() + 1, fx.end());
    const int savedMesh = cur_.blockMesh;
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    cur_.blockMesh = -1;
    drawBlocks(plate, false);
    cur_.blockMesh = savedMesh;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    drawBlocks(solid, true);
    glDisable(GL_CULL_FACE);
    drawBlocks(ghost, false);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    cur_.blockMesh = -1;
    drawBlocks(rest, false);
    cur_.blockMesh = savedMesh;
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawBursts();
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // ---- Resolve MSAA.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo_);
    glBlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // ---- Exposure governor: average scene brightness from a ~8x8 mip; if it exceeds a comfortable level
    //      for the theme's mood, ease exposure and glow down (and back up) over about a second.
    {
        glBindTexture(GL_TEXTURE_2D, sceneTex_);
        glGenerateMipmap(GL_TEXTURE_2D);
        int lvl = std::max(0, sceneLevels_ - 4), lw = 1, lh = 1;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, lvl, GL_TEXTURE_WIDTH, &lw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, lvl, GL_TEXTURE_HEIGHT, &lh);
        std::vector<float> px((size_t)lw * lh * 4);
        glGetTexImage(GL_TEXTURE_2D, lvl, GL_RGBA, GL_FLOAT, px.data());
        double sum = 0, hi = 0;
        for (int i = 0; i < lw * lh; i++) {
            float l = 0.2126f * px[i * 4] + 0.7152f * px[i * 4 + 1] + 0.0722f * px[i * 4 + 2];
            sum += l;
            hi = std::max<double>(hi, l);
        }
        float mean = (float)(sum / std::max(1, lw * lh));
        const float pw = paleW(cur_);
        const float target = lerpf(0.14f, 0.75f, pw) * (1.f + 0.3f * (cur_.pale > 0.2f && pw < 0.5f)); // dusk a bit higher
        float want = std::clamp(target / std::max(mean, 1e-4f), 0.5f, 1.f);
        // Also rein in large areas of very bright light (highlights of the downsampled image).
        if (hi > 0.8f) want = std::min(want, std::max(0.6f, 0.8f / (float)hi));
        govern_ = approach(govern_, want, want < govern_ ? 1.5f : 0.7f, dt);

        // Legibility: brightness of the scene on both sides of the board (outside the middle 46%),
        // combined with how see-through the block material is.
        double side = 0;
        int ns = 0;
        for (int y = 0; y < lh; y++)
            for (int x = 0; x < lw; x++) {
                float u = (x + 0.5f) / lw;
                if (std::abs(u - 0.5f) < 0.23f) continue;
                const float* q = &px[((size_t)y * lw + x) * 4];
                side += 0.2126f * q[0] + 0.7152f * q[1] + 0.0722f * q[2];
                ns++;
            }
        bgLum_ = approach(bgLum_, ns ? (float)(side / ns) : 0.f, 0.5f, dt);
        const float m = from_.blockStyle == to_.blockStyle ? 1.f : smoothstepf(0, 1, transT_);
        const float see = lerpf(seeThrough(from_), seeThrough(to_), m);
        const float bright = smoothstepf(0.05f, 0.22f, bgLum_ * (1.f - 0.6f * pw));
        const float need = std::clamp(std::max(see * (0.25f + 0.75f * bright), 0.5f * bright), 0.f, 1.f);
        legible_ = approach(legible_, need, 0.35f, dt);
    }

    // ---- Bloom mip chain.
    glBindVertexArray(emptyVao_);
    glUseProgram(progDown_);
    set1i(progDown_, "uSrc", 0);
    set1f(progDown_, "uThreshold", cur_.bloomThreshold * lerpf(1.f, 1.1f, paleW(cur_)));
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < BLOOM_MIPS; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        glViewport(0, 0, bloomW_[i], bloomH_[i]);
        GLuint src = i == 0 ? sceneTex_ : bloomTex_[i - 1];
        int sw = i == 0 ? w_ : bloomW_[i - 1], sh = i == 0 ? h_ : bloomH_[i - 1];
        glBindTexture(GL_TEXTURE_2D, src);
        set2f(progDown_, "uTexel", 1.f / sw, 1.f / sh);
        set1i(progDown_, "uFirst", i == 0 ? 1 : 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glUseProgram(progUp_);
    set1i(progUp_, "uSrc", 0);
    set1f(progUp_, "uRadius", 1.f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = BLOOM_MIPS - 1; i > 0; i--) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i - 1]);
        glViewport(0, 0, bloomW_[i - 1], bloomH_[i - 1]);
        glBindTexture(GL_TEXTURE_2D, bloomTex_[i]);
        set2f(progUp_, "uTexel", 1.f / bloomW_[i], 1.f / bloomH_[i]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);

    // ---- Composite to screen.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w_, h_);
    glUseProgram(progComp_);
    set1i(progComp_, "uScene", 0);
    set1i(progComp_, "uBloom", 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
    glActiveTexture(GL_TEXTURE0);
    float bloom = cur_.bloom * (0.8f + 0.4f * music.intensity + 0.3f * clearGlow_ + 0.35f * music.beatPulse) *
                  music.glow * (1.f + 0.45f * swell_) * govern_ * 0.22f;
    set1f(progComp_, "uBloomStrength", bloom);
    set1f(progComp_, "uExposure", cur_.exposure * govern_ * (1.f + 0.05f * swell_) * lerpf(1.f, 0.55f, pauseFade_));
    set1f(progComp_, "uSaturation", cur_.saturation * music.saturation * lerpf(1.f, 0.4f, pauseFade_) * (1.f - 0.15f * slow_));
    set1f(progComp_, "uVignette", cur_.vignette + 0.15f * slow_);
    {
        // Reversed colors (slow-motion bonus): the new state spreads from the board behind a glowing front.
        const float k = saturate(invT_ / 0.7f), front = invT_ < 0.7f ? 1.6f * (1.f - (1.f - k) * (1.f - k)) : -1.f;
        const vec2 c = project(vec3(0, 0, 0));
        set1f(progComp_, "uInvert", inverted_ ? 1.f : 0.f);
        set1f(progComp_, "uInvFront", front);
        set2f(progComp_, "uInvCenter", c.x / w_, 1.f - c.y / h_);
        vec3 rim = lerp(cur_.accent, cur_.partB, 0.3f);
        rim = vec3(std::pow(saturate(rim.x), 1 / 2.2f), std::pow(saturate(rim.y), 1 / 2.2f), std::pow(saturate(rim.z), 1 / 2.2f));
        set3f(progComp_, "uInvRim", rim * (0.6f * (1.f - k)));
    }
    set1f(progComp_, "uChroma", cur_.chroma);
    set1f(progComp_, "uGrain", cur_.grain);
    set1f(progComp_, "uScanlines", cur_.scanlines);
    set1f(progComp_, "uTime", (float)time);
    set1f(progComp_, "uAspect", aspect);
    set1f(progComp_, "uFade", fade);
    set1f(progComp_, "uPale", paleW(cur_));
    set3f(progComp_, "uShadowTint", cur_.shadowTint);
    set3f(progComp_, "uHighlightTint", cur_.highlightTint);
    set2f(progComp_, "uRes", (float)w_, (float)h_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawText(hud);
    glDisable(GL_BLEND);
}

void Renderer::readFrame(std::vector<unsigned char>& px, int& w, int& h) {
    w = w_;
    h = h_;
    px.resize((size_t)w_ * h_ * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w_, h_, GL_RGB, GL_UNSIGNED_BYTE, px.data());
}

bool Renderer::screenshot(const std::string& path) {
    std::vector<unsigned char> px((size_t)w_ * h_ * 3), flipped(px.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w_, h_, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h_; y++) std::memcpy(&flipped[(size_t)y * w_ * 3], &px[(size_t)(h_ - 1 - y) * w_ * 3], w_ * 3);
    return stbi_write_png(path.c_str(), w_, h_, 3, flipped.data(), w_ * 3) != 0;
}
