#include "layout.hpp"

#include "universe.hpp"

namespace layout {

Camera camera(int width, int height, float distScale, float yaw, float zNear, float zFar) {
    Camera c;
    const float aspect = (float)std::max(1, width) / std::max(1, height);
    const float half = c.fovDeg * 0.5f * PI / 180.f;
    // The board (20 rows + margins) fills the height; narrow windows back off to keep both HUD columns.
    float dist = 11.9f / std::tan(half);
    const float needW = 23.5f / aspect * 1.05f;
    const float visH = 2.f * dist * std::tan(half);
    if (visH < needW) dist *= needW / visH;
    dist *= distScale;
    c.dist = dist;
    c.target = vec3(0, 0.4f, 0);
    c.eye = c.target + vec3(std::sin(yaw) * dist, 0.f, std::cos(yaw) * dist);
    c.view = mat4::lookAt(c.eye, c.target, vec3(0, 1, 0));
    c.proj = mat4::perspective(c.fovDeg * PI / 180.f, aspect, zNear, zFar);
    c.vp = c.proj * c.view;
    return c;
}

vec2 project(const mat4& vp, const vec3& p, int width, int height) {
    vec4 c = vp * vec4(p, 1.f);
    if (c.w <= 1e-4f) return {-1000, -1000};
    return {(c.x / c.w * 0.5f + 0.5f) * width, (0.5f - c.y / c.w * 0.5f) * height};
}

vec2 project(const vec3& p, int width, int height) { return project(camera(width, height).vp, p, width, height); }

Rect boardRect(int width, int height) {
    const mat4 vp = camera(width, height).vp;
    vec2 a = project(vp, vec3(-BOARD_W * 0.5f, -BOARD_H * 0.5f, 0), width, height);
    vec2 b = project(vp, vec3(BOARD_W * 0.5f, BOARD_H * 0.5f, 0), width, height);
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

} // namespace layout

vec2 Universe::project(const vec3& p) const { return layout::project(p, width_, height_); }

// The key's hue walks the circle of fifths (neighbouring keys get neighbouring hues), +-30 degrees per run.
SongLook makeSongLook(const Footprint* fp, uint64_t seed) {
    SongLook l;
    Rng r(seed ^ 0x500C1001ull);
    l.seed = splitmix64(seed ^ (fp ? fp->hash : 0x5EEDull));
    if (!fp || !fp->hash) {
        l.hue = r.irange(0, 23) * TAU / 24.f;
        return l;
    }
    l.hasSong = true;
    const int fifths = (fp->key * 7) % 12;
    l.hue = (((fifths * 2 + r.irange(-2, 2)) % 24 + 24) % 24) * TAU / 24.f;
    l.key = fp->key;
    l.minor = fp->minor;
    l.bpm = fp->bpm;
    l.brightness = fp->brightness;
    l.bassWeight = fp->bassWeight;
    l.airWeight = fp->airWeight;
    l.dynamics = fp->dynamics;
    l.density = fp->density;
    return l;
}
