#include "blockshapes.hpp"

#include <cmath>
#include <functional>

#include "mathutil.hpp"
#include "theme.hpp"

// A profile block is an outline (CCW, in the board plane, the cell being -0.5 .. 0.5) and a front height
// z = F(x, y, d), d being the distance to the outline: F gives rims (round or chamfered), puffy faces and
// domes. The back is the front mirrored, and a wall joins them along the outline where F > 0 there.
// The front is meshed as rings: the outline offset inward (rims follow it exactly, sharp corners included),
// then that offset ring scaled toward the center, or circles (domes). Every vertex carries its distance to
// the outline, so the block shader draws edges along the real outline instead of the uv square.

namespace {

struct P2 {
    float x = 0, y = 0;
};
P2 operator+(P2 a, P2 b) { return {a.x + b.x, a.y + b.y}; }
P2 operator-(P2 a, P2 b) { return {a.x - b.x, a.y - b.y}; }
P2 operator*(P2 a, float s) { return {a.x * s, a.y * s}; }
float dot2(P2 a, P2 b) { return a.x * b.x + a.y * b.y; }
float cross2(P2 a, P2 b) { return a.x * b.y - a.y * b.x; }
float len2(P2 a) { return std::sqrt(dot2(a, a)); }
P2 norm2(P2 a) {
    float l = len2(a);
    return l > 1e-9f ? a * (1.f / l) : P2{0, 0};
}
P2 rot2(P2 a, float ang) {
    float c = std::cos(ang), s = std::sin(ang);
    return {a.x * c - a.y * s, a.x * s + a.y * c};
}

// Polygon (CCW corners) with corners rounded by arcs of the given radii (0: sharp corner).
std::vector<P2> roundedPolygon(const std::vector<P2>& c, const std::vector<float>& r, int arcSeg) {
    std::vector<P2> out;
    const int n = (int)c.size();
    for (int i = 0; i < n; i++) {
        P2 v = c[i], p = c[(i + n - 1) % n], q = c[(i + 1) % n];
        if (r[i] <= 0) {
            out.push_back(v);
            continue;
        }
        P2 u1 = norm2(p - v), u2 = norm2(q - v);
        float half = 0.5f * std::acos(clampf(dot2(u1, u2), -1.f, 1.f));
        float t = r[i] / std::tan(half);
        P2 center = v + norm2(u1 + u2) * (r[i] / std::sin(half));
        P2 a1 = v + u1 * t - center, a2 = v + u2 * t - center;
        float sweep = std::atan2(cross2(a1, a2), dot2(a1, a2));
        for (int k = 0; k <= arcSeg; k++) out.push_back(center + rot2(a1, sweep * k / arcSeg));
    }
    return out;
}

std::vector<P2> regularPolygon(int n, float radius, float angle0) {
    std::vector<P2> c;
    for (int i = 0; i < n; i++) {
        float a = angle0 + TAU * i / n;
        c.push_back({radius * std::cos(a), radius * std::sin(a)});
    }
    return c;
}

// Splits long segments so that heightfields and distances are sampled finely enough.
std::vector<P2> subdivide(const std::vector<P2>& pts, float maxLen) {
    std::vector<P2> out;
    const int n = (int)pts.size();
    for (int i = 0; i < n; i++) {
        P2 a = pts[i], b = pts[(i + 1) % n];
        int k = std::max(1, (int)std::ceil(len2(b - a) / maxLen));
        for (int j = 0; j < k; j++) out.push_back(a + (b - a) * ((float)j / k));
    }
    return out;
}

float rimRound(float d, float b) { // quarter circle: vertical at the outline, flat from d = b
    float u = b - std::min(d, b);
    return std::sqrt(std::max(0.f, b * b - u * u));
}
float rimChamfer(float d, float b) { return std::min(d, b); } // 45 degrees

enum RingKind { R_OFFSET, R_SCALE, R_CIRCLE };
struct Ring {
    int kind;
    float v;            // offset distance, scale of the last offset ring, or circle radius
    bool crease = false; // the height profile has a kink there: two vertex copies, normals from each side
};

struct Shape {
    std::vector<P2> outline;
    // After the outline itself. Offsets stay below the corner radii, and below the spacing of the outline
    // points next to a sharp corner (further, the offset ring would fold over itself there).
    std::vector<Ring> rings;
    std::function<float(float x, float y, float d)> z;
    std::function<float(float x, float y, float d)> zBack; // when the back is not the front mirrored
    int backRings = 0; // the back (never in view) uses this many outer rings, then the center (0: all)
    float eNormMin = 0.f; // the distance shown as the face center (gE = 1) is at least this
};

void rimRings(Shape& s, float b, bool round, int steps) {
    if (!round) {
        s.rings.push_back({R_OFFSET, b, true});
        return;
    }
    for (int k = 1; k <= steps; k++) {
        float t = (float)k / steps;
        s.rings.push_back({R_OFFSET, b * t * t});
    }
}

void scaleRings(Shape& s, std::initializer_list<float> list) {
    for (float v : list) s.rings.push_back({R_SCALE, v});
}

// A prism: flat front at H, its rim (width b) rounded or chamfered down to the wall.
void prism(Shape& s, float H, float b, bool round) {
    s.z = [H, b, round](float, float, float d) { return H - b + (round ? rimRound(d, b) : rimChamfer(d, b)); };
    rimRings(s, b, round, 5);
    s.backRings = (int)s.rings.size() + 1;
    scaleRings(s, {0.65f, 0.3f, 0.f});
}

Shape makeShape(int mesh) {
    Shape s;
    const float h = 0.5f;
    switch (mesh) {
    case MESH_CHAMFER: { // a cube whose 12 edges are cut flat at 45 degrees
        const float c = 0.1f;
        s.outline = {{h, -h + c}, {h, h - c}, {h - c, h}, {-h + c, h}, {-h, h - c}, {-h, -h + c}, {-h + c, -h}, {h - c, -h}};
        prism(s, 0.5f, c, false);
        break;
    }
    case MESH_PILLOW: { // a cushion: square outline, puffy faces thinning to the seams, pinched corners
        s.outline = subdivide({{h, -h}, {h, h}, {-h, h}, {-h, -h}}, 0.05f);
        s.z = [](float x, float y, float) {
            auto f = [](float v) { return std::pow(std::max(0.f, 1.f - std::pow(std::fabs(2.f * v), 2.2f)), 0.5f); };
            return 0.32f * f(x) * f(y);
        };
        for (float d : {0.002f, 0.006f, 0.012f, 0.02f, 0.03f}) s.rings.push_back({R_OFFSET, d});
        scaleRings(s, {0.92f, 0.84f, 0.74f, 0.62f, 0.48f, 0.32f, 0.16f, 0.f});
        break;
    }
    case MESH_TILE: { // a thin ceramic tile with rounded corners and a soft rim
        s.outline = subdivide(roundedPolygon({{h, -h}, {h, h}, {-h, h}, {-h, -h}}, {0.1f, 0.1f, 0.1f, 0.1f}, 6), 0.1f);
        prism(s, 0.11f, 0.05f, true);
        break;
    }
    case MESH_COIN: { // a disc facing the camera
        s.outline = regularPolygon(72, h, 0.f);
        prism(s, 0.2f, 0.06f, true);
        break;
    }
    case MESH_OCTAGON: { // flat sides facing the neighbours
        s.outline = regularPolygon(8, h / std::cos(PI / 8), PI / 8);
        prism(s, 0.45f, 0.06f, false);
        break;
    }
    case MESH_HEX: { // pointy top: flat sides joined left and right, so rows read as rows
        s.outline = regularPolygon(6, 0.55f, PI / 6);
        prism(s, 0.45f, 0.06f, false);
        break;
    }
    case MESH_DIAMOND: { // a square turned 45 degrees, tips toward the neighbours
        const float R = 0.6f;
        s.outline = roundedPolygon({{R, 0}, {0, R}, {-R, 0}, {0, -R}}, {0.07f, 0.07f, 0.07f, 0.07f}, 6);
        prism(s, 0.45f, 0.06f, false);
        break;
    }
    case MESH_DOME: { // a half sphere on a flat square base
        const float H = 0.15f, b = 0.04f, Rc = 0.34f, hc = 0.24f;
        s.outline = subdivide(roundedPolygon({{h, -h}, {h, h}, {-h, h}, {-h, -h}}, {0.08f, 0.08f, 0.08f, 0.08f}, 6), 0.04f);
        s.z = [=](float x, float y, float d) {
            float r = std::sqrt(x * x + y * y) / Rc;
            return H - b + rimRound(d, b) + (r < 1.f ? hc * std::sqrt(1.f - r * r) : 0.f);
        };
        s.zBack = [=](float, float, float d) { return H - b + rimRound(d, b); }; // a flat base
        rimRings(s, b, true, 4);
        s.backRings = (int)s.rings.size() + 1;
        scaleRings(s, {0.9f});
        s.rings.push_back({R_CIRCLE, Rc, true});
        for (float u : {0.004f, 0.016f, 0.04f, 0.08f, 0.14f, 0.22f, 0.32f, 0.45f, 0.6f, 0.78f, 1.f})
            s.rings.push_back({R_CIRCLE, Rc * (1.f - u)});
        break;
    }
    case MESH_CROSS: { // a plus sign: the arms of neighbours meet
        const float w = 0.2f;
        s.outline = subdivide(roundedPolygon({{h, -w}, {h, w}, {w, w}, {w, h}, {-w, h}, {-w, w}, {-h, w}, {-h, -w}, {-w, -w},
                                              {-w, -h}, {w, -h}, {w, -w}},
                                             std::vector<float>(12, 0.06f), 6),
                              0.06f);
        prism(s, 0.45f, 0.05f, true);
        s.eNormMin = 0.3f;
        break;
    }
    default: { // MESH_STAR: five rounded points
        std::vector<P2> c;
        for (int i = 0; i < 10; i++) {
            float a = PI / 2 + PI * i / 5, r = i % 2 ? 0.25f : 0.56f;
            c.push_back({r * std::cos(a), r * std::sin(a) - 0.053f});
        }
        std::vector<float> rad;
        for (int i = 0; i < 10; i++) rad.push_back(i % 2 ? 0.05f : 0.07f);
        s.outline = subdivide(roundedPolygon(c, rad, 8), 0.06f);
        prism(s, 0.42f, 0.04f, true);
        s.eNormMin = 0.3f;
        break;
    }
    }
    return s;
}

} // namespace

void buildBlockShape(int mesh, std::vector<float>& v, std::vector<unsigned>& idx) {
    const Shape S = makeShape(mesh);
    const std::vector<P2>& P = S.outline;
    const int n = (int)P.size();

    auto dist = [&](P2 p) {
        float best = 1e9f;
        for (int i = 0; i < n; i++) {
            P2 a = P[i], b = P[(i + 1) % n], ab = b - a;
            float t = clampf(dot2(p - a, ab) / std::max(dot2(ab, ab), 1e-12f), 0.f, 1.f);
            best = std::min(best, len2(p - (a + ab * t)));
        }
        return best;
    };
    auto F = [&](P2 p) { return S.z(p.x, p.y, dist(p)); };
    auto Fb = [&](P2 p) { return S.zBack ? S.zBack(p.x, p.y, dist(p)) : F(p); };
    auto normalOf = [&](P2 p, bool back) { // of the height field, by central differences
        auto H = [&](P2 q) { return back ? Fb(q) : F(q); };
        const float e = 1e-4f;
        float gx = (H(p + P2{e, 0}) - H(p - P2{e, 0})) / (2 * e), gy = (H(p + P2{0, e}) - H(p - P2{0, e})) / (2 * e);
        return normalize(vec3(-gx, -gy, 1.f));
    };

    // Outward normals of the segments; inward miter vectors (offsetting by d moves every edge by d).
    std::vector<P2> segN(n), miter(n);
    for (int i = 0; i < n; i++) {
        P2 e = norm2(P[(i + 1) % n] - P[i]);
        segN[i] = {e.y, -e.x};
    }
    std::vector<bool> sharp(n);
    for (int i = 0; i < n; i++) {
        P2 na = segN[(i + n - 1) % n], nb = segN[i];
        miter[i] = (na + nb) * (-1.f / std::max(1.f + dot2(na, nb), 1e-3f));
        sharp[i] = dot2(na, nb) < std::cos(30.f * PI / 180.f);
    }

    // Ring points.
    std::vector<std::vector<P2>> rings;
    std::vector<bool> crease;
    rings.push_back(P);
    crease.push_back(false);
    std::vector<P2> lastOffset = P;
    for (const Ring& r : S.rings) {
        std::vector<P2> pts(n);
        for (int i = 0; i < n; i++) {
            if (r.kind == R_OFFSET) pts[i] = P[i] + miter[i] * r.v;
            else if (r.kind == R_SCALE) pts[i] = lastOffset[i] * r.v;
            else pts[i] = norm2(P[i]) * r.v;
        }
        if (r.kind == R_OFFSET) lastOffset = pts;
        rings.push_back(pts);
        crease.push_back(r.crease);
    }

    float eNorm = S.eNormMin;
    for (auto& ring : rings)
        for (P2 p : ring) eNorm = std::max(eNorm, dist(p));

    auto push = [&](vec3 p, vec3 nn, float u, float w, float e) {
        float d[10] = {p.x, p.y, p.z, nn.x, nn.y, nn.z, u, w, e, 1.f};
        v.insert(v.end(), d, d + 10);
        return (unsigned)(v.size() / 10 - 1);
    };
    // Triangles face the way their vertex normals do (outward), whatever order they come in.
    auto tri = [&](unsigned a, unsigned b, unsigned c) {
        auto pos = [&](unsigned i) { return vec3(v[i * 10], v[i * 10 + 1], v[i * 10 + 2]); };
        auto nor = [&](unsigned i) { return vec3(v[i * 10 + 3], v[i * 10 + 4], v[i * 10 + 5]); };
        vec3 g = cross(pos(b) - pos(a), pos(c) - pos(a));
        if (dot(g, g) < 1e-14f) return;
        if (dot(g, nor(a) + nor(b) + nor(c)) < 0) std::swap(b, c);
        idx.insert(idx.end(), {a, b, c});
    };

    // Front and back. Columns follow the outline points; a sharp corner gets two columns at the same
    // places, one per side, so that each side of a rim crease is shaded flat.
    std::vector<std::pair<int, int>> cols; // outline point, side of the corner (0: smooth)
    for (int i = 0; i < n; i++) {
        if (sharp[i]) {
            cols.push_back({i, -1});
            cols.push_back({i, 1});
        } else cols.push_back({i, 0});
    }
    const int C = (int)cols.size();
    for (int side = 0; side < 2; side++) {
        const float sz = side ? -1.f : 1.f;
        std::vector<int> ks;
        for (int k = 0; k < (int)rings.size(); k++)
            if (!side || !S.backRings || k < S.backRings || k + 1 == (int)rings.size()) ks.push_back(k);
        const int R = (int)ks.size();
        std::vector<std::vector<unsigned>> outer(R, std::vector<unsigned>(C)), inner(R, std::vector<unsigned>(C));
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++) {
                const int k = ks[r], i = cols[c].first, cs = cols[c].second;
                P2 p = rings[k][i];
                float z = (side ? Fb(p) : F(p)) * sz, e = std::min(1.f, dist(p) / eNorm);
                P2 in = norm2(P2{0, 0} - p) * 4e-4f; // toward the center
                P2 along = cs ? norm2(P[(i + n + cs) % n] - P[i]) * 1e-3f : P2{0, 0};
                auto at = [&](P2 q) {
                    vec3 nn = normalOf(q + along, side);
                    nn.z *= sz;
                    return push(vec3(p.x, p.y, z), nn, p.x / 0.5f, p.y / 0.5f, e);
                };
                if (k == 0) outer[r][c] = inner[r][c] = at(p + in); // the outline: sampled inside
                else if (crease[k]) {
                    outer[r][c] = at(p - in);
                    inner[r][c] = at(p + in);
                } else outer[r][c] = inner[r][c] = at(p);
            }
        for (int k = 0; k + 1 < R; k++)
            for (int c = 0; c < C; c++) {
                int c2 = (c + 1) % C;
                unsigned a = inner[k][c], b = inner[k][c2], cc = outer[k + 1][c2], d = outer[k + 1][c];
                tri(a, b, cc);
                tri(a, cc, d);
            }
    }

    // Wall along the outline: three rows (front rim, middle, back rim), edges at both rims.
    float perim = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float zi = F(P[i]), zj = F(P[j]), seg = len2(P[j] - P[i]);
        if (zi < 1e-4f && zj < 1e-4f) {
            perim += seg;
            continue;
        }
        unsigned col[2][3];
        for (int s = 0; s < 2; s++) {
            int k = s ? j : i;
            P2 nn2 = sharp[k] ? segN[i] : norm2(segN[(k + n - 1) % n] + segN[k]);
            vec3 nn(nn2.x, nn2.y, 0.f);
            float zt = s ? zj : zi, u = (perim + (s ? seg : 0.f)) / 0.5f;
            for (int row = 0; row < 3; row++) {
                float z = zt * (1 - row), e = row == 1 ? std::min(1.f, zt / eNorm) : 0.f;
                col[s][row] = push(vec3(P[k].x, P[k].y, z), nn, u, z / 0.5f, e);
            }
        }
        for (int row = 0; row < 2; row++) {
            tri(col[0][row], col[1][row], col[1][row + 1]);
            tri(col[0][row], col[1][row + 1], col[0][row + 1]);
        }
        perim += seg;
    }
}
