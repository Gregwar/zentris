#pragma once
// Minimal vector / matrix / color / random helpers.
#include <cmath>
#include <cstdint>
#include <algorithm>

constexpr float PI = 3.14159265358979f;
constexpr float TAU = 6.28318530717959f;

struct vec2 { float x = 0, y = 0; };

struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() = default;
    constexpr vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    explicit constexpr vec3(float s) : x(s), y(s), z(s) {}
    vec3 operator+(const vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(const vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator*(const vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    vec4() = default;
    constexpr vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    vec4(const vec3& v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
};

inline float dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3& a, const vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const vec3& v) { return std::sqrt(dot(v, v)); }
inline vec3 normalize(const vec3& v) { float l = length(v); return l > 1e-8f ? v / l : vec3(0, 0, 1); }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float saturate(float v) { return clampf(v, 0.f, 1.f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline vec3 lerp(const vec3& a, const vec3& b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float a, float b, float x) {
    float t = saturate((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}
// Frame-rate independent exponential approach.
inline float approach(float cur, float target, float rate, float dt) {
    return target + (cur - target) * std::exp(-rate * dt);
}

// Column-major 4x4 matrix (OpenGL convention).
struct mat4 {
    float m[16];
    static mat4 identity() {
        mat4 r{};
        for (int i = 0; i < 16; i++) r.m[i] = (i % 5 == 0) ? 1.f : 0.f;
        return r;
    }
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
    mat4 operator*(const mat4& o) const {
        mat4 r{};
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += at(rr, k) * o.at(k, c);
                r.at(rr, c) = s;
            }
        return r;
    }
    vec4 operator*(const vec4& v) const {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3) * v.w,
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3) * v.w,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3) * v.w,
                at(3, 0) * v.x + at(3, 1) * v.y + at(3, 2) * v.z + at(3, 3) * v.w};
    }
    static mat4 perspective(float fovyRad, float aspect, float zn, float zf) {
        mat4 r{};
        float f = 1.f / std::tan(fovyRad * 0.5f);
        r.at(0, 0) = f / aspect;
        r.at(1, 1) = f;
        r.at(2, 2) = (zf + zn) / (zn - zf);
        r.at(2, 3) = 2 * zf * zn / (zn - zf);
        r.at(3, 2) = -1;
        return r;
    }
    static mat4 lookAt(const vec3& eye, const vec3& center, const vec3& up) {
        vec3 f = normalize(center - eye);
        vec3 s = normalize(cross(f, up));
        vec3 u = cross(s, f);
        mat4 r = identity();
        r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z;
        r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z;
        r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
        r.at(0, 3) = -dot(s, eye);
        r.at(1, 3) = -dot(u, eye);
        r.at(2, 3) = dot(f, eye);
        return r;
    }
    static mat4 ortho(float l, float r_, float b, float t, float n, float f) {
        mat4 r = identity();
        r.at(0, 0) = 2 / (r_ - l);
        r.at(1, 1) = 2 / (t - b);
        r.at(2, 2) = -2 / (f - n);
        r.at(0, 3) = -(r_ + l) / (r_ - l);
        r.at(1, 3) = -(t + b) / (t - b);
        r.at(2, 3) = -(f + n) / (f - n);
        return r;
    }
};

// ---------- Random ----------
inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed = 1) : s(splitmix64(seed)) {}
    uint64_t next() { s = splitmix64(s); return s; }
    float uniform() { return (next() >> 40) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * uniform(); }
    int irange(int a, int bInclusive) { return a + (int)(next() % (uint64_t)(bInclusive - a + 1)); }
    bool chance(float p) { return uniform() < p; }
    // Pick an index according to weights.
    template <int N>
    int weighted(const float (&w)[N]) {
        float sum = 0;
        for (float x : w) sum += std::max(0.f, x);
        float r = uniform() * sum;
        for (int i = 0; i < N; i++) {
            r -= std::max(0.f, w[i]);
            if (r <= 0) return i;
        }
        return N - 1;
    }
};

// ---------- Color ----------
inline float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// OKLCH (L 0..1, C ~0..0.37, h radians) -> linear sRGB, clamped to gamut by reducing chroma.
inline vec3 oklchToLinear(float L, float C, float h) {
    for (int iter = 0; iter < 24; iter++) {
        float a = C * std::cos(h), b = C * std::sin(h);
        float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
        float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
        float s_ = L - 0.0894841775f * a - 1.2914855480f * b;
        float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
        vec3 rgb{4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
                 -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
                 -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
        if ((rgb.x >= -1e-4f && rgb.y >= -1e-4f && rgb.z >= -1e-4f && rgb.x <= 1.0001f && rgb.y <= 1.0001f &&
             rgb.z <= 1.0001f) || C < 1e-3f)
            return {saturate(rgb.x), saturate(rgb.y), saturate(rgb.z)};
        C *= 0.88f;
    }
    return vec3(L * L * L);
}

inline float luminance(const vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
