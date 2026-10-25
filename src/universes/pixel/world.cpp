// PIXEL's landscapes. Each variant is one place, drawn back to front on the native canvas: a dithered sky, its
// sun (or moon) and clouds, parallax ranges (far, mid, near) with their lit faces toward the sun, water that
// mirrors them, then critters. The scene level stages the place: calm is the still landscape with a few living
// details; mid brings the near layer in, parallax sway, and critters animated on the beat; peak brings weather
// (rain, sand, snow, petals, ash, leaves), the place's big event (lightning, aurora, eruption, searchlights,
// lanterns, a rainbow) and foreground framing, everything hopping on the beat by whole pixels.
#include "universes/pixel/world.hpp"

#include <algorithm>
#include <cmath>

namespace pixu {

namespace {

struct Ctx {
    Canvas& cv;
    const Variant& V;
    const WorldState& s;
    uint32_t seed;
    int W, H, hz;
    int sunSide; // -1: the sun is on the left, +1: on the right
    int off(float depth) const { return iround(s.scroll * depth); }
};

bool skyOrWorld(const Canvas& cv, int x, int y) {
    const uint8_t k = cv.kind(x, y);
    return k == K_SKY || k == K_WORLD || k == K_FX;
}

// ------------------------------------------------------------------ sky

void drawSky(Ctx& c, const uint8_t st[5], int bottom) {
    static const float pos[5] = {0.f, 0.36f, 0.62f, 0.83f, 1.f};
    for (int y = 0; y < bottom && y < c.H; y++) {
        const float t = (float)y / std::max(1, bottom);
        int k = 0;
        while (k < 3 && t >= pos[k + 1]) k++;
        const float f = saturate((t - pos[k]) / (pos[k + 1] - pos[k]));
        const float fq = std::floor(f * 4.f) / 4.f; // four dither steps per band: crafted, not noisy
        for (int x = 0; x < c.W; x++) c.cv.set(x, y, dith(fq, x, y) ? st[k + 1] : st[k], K_SKY);
    }
}

void drawStars(Ctx& c, int bottom, float amount, uint8_t col, uint8_t dim) {
    if (amount <= 0.01f) return;
    const int tw = c.s.step(4.f);
    for (int y = 0; y < bottom; y++)
        for (int x = 0; x < c.W; x++) {
            const float h = hash01(x, y, c.seed + 77);
            if (h > 0.006f * amount * (1.f - (float)y / bottom)) continue;
            if (c.cv.kind(x, y) != K_SKY) continue;
            const bool on = (hash2(x, y, (uint32_t)tw) & 7) != 0;
            c.cv.set(x, y, on ? col : dim, K_SKY);
        }
}

void drawSun(Ctx& c, int x, int y, int r, uint8_t sun, uint8_t glow, float glowAmt) {
    c.cv.glow(x, y, (float)r + 1.5f, (float)r + 9.f + 3.f * glowAmt, 0.55f * glowAmt + 0.25f, glow, K_SKY, true, K_SKY);
    c.cv.disc(x, y, (float)r, sun, K_SKY);
}

// A pixel cloud: puffs on a flat base, lit on top, shaded at the bottom.
void drawCloud(Ctx& c, int cx, int cy, int w, uint32_t id, uint8_t base, uint8_t lit, uint8_t shade) {
    const int n = 3 + (int)(hash01((int)id, 1, c.seed) * 3);
    float px[6], py[6], pr[6];
    for (int i = 0; i < n; i++) {
        const float u = (i + 0.5f) / n;
        px[i] = cx - w * 0.5f + u * w;
        pr[i] = (2.5f + w * 0.13f) * (0.7f + 0.6f * hash01((int)id, i + 2, c.seed)) * (1.f - 0.5f * std::fabs(u - 0.5f));
        py[i] = (float)cy - pr[i] * 0.35f;
    }
    auto inside = [&](int x, int y) {
        if (y > cy) return false;
        for (int i = 0; i < n; i++) {
            const float dx = x - px[i], dy = (y - py[i]) * 1.25f;
            if (dx * dx + dy * dy <= pr[i] * pr[i]) return true;
        }
        return false;
    };
    const int x0 = cx - w / 2 - 8, x1 = cx + w / 2 + 8, y0 = cy - 14;
    for (int y = y0; y <= cy; y++)
        for (int x = x0; x <= x1; x++) {
            if (!inside(x, y)) continue;
            uint8_t col = base;
            if (!inside(x, y - 1) || !inside(x - c.sunSide, y - 2)) col = lit;
            else if (y == cy || (y == cy - 1 && dith(0.5f, x, y))) col = shade;
            if (c.cv.kind(x, y) == K_SKY) c.cv.set(x, y, col, K_SKY);
        }
}

void drawClouds(Ctx& c, int count, float speed, int yTop, int yBot, uint8_t base, uint8_t lit, uint8_t shade) {
    for (int i = 0; i < count; i++) {
        const uint32_t id = (uint32_t)i * 7 + 3;
        const int w = 20 + (int)(hash01((int)id, 9, c.seed) * 26);
        const float sp = speed * (0.6f + 0.8f * hash01((int)id, 10, c.seed));
        const float span = (float)c.W + w + 40;
        float x = hash01((int)id, 11, c.seed) * span + c.s.t * sp;
        x = std::fmod(x, span);
        if (x < 0) x += span;
        const int cy = yTop + (int)(hash01((int)id, 12, c.seed) * (yBot - yTop));
        drawCloud(c, (int)std::floor(x) - w / 2 - 20, cy, w, id, base, lit, shade);
    }
}

// ------------------------------------------------------------------ ranges

// Triangular peaks placed per cell (S pixels), roughened: per column, the highest one.
struct Peak { float h, tx, top; };
Peak peaks(int wx, uint32_t seed, int S, float hmin, float hmax, float smin, float smax, float rough) {
    const int cell = (int)std::floor(wx / (float)S);
    Peak best{-1e9f, 0, 0};
    for (int k = cell - 3; k <= cell + 3; k++) {
        const float tx = k * (float)S + hash01(k, 1, seed) * S;
        const float h = hmin + (hmax - hmin) * hash01(k, 2, seed);
        const float sr = smin + (smax - smin) * hash01(k, 3, seed);
        const float sl = sr * (0.75f + 0.5f * hash01(k, 4, seed));
        const float d = wx - tx;
        const float v = h - (d < 0 ? -d * sl : d * sr);
        if (v > best.h) best = {v, tx, h};
    }
    best.h += (fbm1(wx * 0.21f, seed + 9, 3) - 0.5f) * rough;
    return best;
}

// A range of peaks: two-tone faces (lit toward the sun, the shadow side with a diagonal ridge line), optional
// snow above a depth from each summit, optional haze dithered toward `haze` near the bottom.
void drawPeaks(Ctx& c, float depth, int base, int S, float hmin, float hmax, float smin, float smax, float rough,
               uint8_t fill, uint8_t lit, int snowDepth, uint8_t snow, uint8_t snowShade, int hazeRows, uint8_t haze,
               int bottom, uint32_t seed, int lift = 0) {
    const int o = c.off(depth);
    for (int x = 0; x < c.W; x++) {
        const int wx = x + o;
        const Peak p = peaks(wx, seed, S, hmin, hmax, smin, smax, rough);
        const int top = base - lift - iround(p.h);
        const float ptop = base - lift - p.h + (p.h - p.top);
        const float summit = (float)(base - lift) - p.top;
        (void)ptop;
        for (int y = std::max(0, top); y < bottom && y < c.H; y++) {
            const float d = y - summit;
            const float rel = (wx - p.tx) * (float)c.sunSide; // > 0: away from the sun
            const float jag = (hash01(y, (int)p.tx, seed) - 0.5f) * 2.f;
            const bool litFace = rel < d * 0.42f - 0.5f + jag;
            uint8_t col = litFace ? lit : fill;
            if (snowDepth > 0) {
                const float sd = snowDepth * (0.75f + 0.5f * hash01((int)p.tx, 5, seed)) + (hash01(wx, 3, seed) - 0.5f) * 4.f;
                if (d < sd) col = litFace ? snow : snowShade;
            }
            if (hazeRows > 0 && y > bottom - hazeRows) {
                const float t = (float)(y - (bottom - hazeRows)) / hazeRows;
                if (dith(std::floor(t * 4.f) / 4.f * 0.75f, x, y)) col = haze;
            }
            c.cv.set(x, y, col, K_WORLD);
        }
    }
}

// A generic silhouette band: top(wx) gives the top row; the band is filled down to `bottom`, with a rim of
// `lit` pixels on top (`rim` rows on slopes facing the sun, 1 on flats).
template <class F>
void drawBand(Ctx& c, float depth, F top, int bottom, uint8_t fill, uint8_t lit, int rim, int dy = 0) {
    const int o = c.off(depth);
    for (int x = 0; x < c.W; x++) {
        const int wx = x + o;
        const int t0 = top(wx) + dy, tl = top(wx - 1) + dy, tr = top(wx + 1) + dy;
        const int slope = tr - tl;
        const bool faces = c.sunSide < 0 ? slope < 0 : slope > 0;
        for (int y = std::max(0, t0); y < bottom && y < c.H; y++) {
            const int d = y - t0;
            uint8_t col = fill;
            if (d == 0 && rim > 0) col = lit;
            else if (faces && d < rim) col = lit;
            c.cv.set(x, y, col, K_WORLD);
        }
    }
}

// Pine silhouettes: the top of a forest band (max of jagged triangles).
int pineTop(int wx, uint32_t seed, int base, int spacing, int hmin, int hmax) {
    const int cell = (int)std::floor(wx / (float)spacing);
    float best = 0;
    for (int k = cell - 3; k <= cell + 3; k++) {
        const float tx = k * (float)spacing + hash01(k, 1, seed) * spacing * 0.8f;
        const float h = hmin + (hmax - hmin) * hash01(k, 2, seed);
        const float d = std::fabs(wx - tx);
        float v = h - d * 2.4f;
        if (d > 0.5f && ((int)(h - d * 2.4f + 64) & 3) == 0) v -= 1.f; // branch tiers
        best = std::max(best, v);
    }
    return base - (int)best;
}

// One pine tree: tiers of branches, a lit edge toward the sun, optional snow on each tier.
void drawPine(Ctx& c, int x, int yBase, int h, uint8_t fill, uint8_t lit, int snow, uint8_t k = K_WORLD, int sway = 0) {
    const int top = yBase - h;
    for (int y = top; y < yBase - 1; y++) {
        const int r = y - top;
        const int tier = r % 5;
        const int hw = std::max(0, r / 3 + tier / 2 - 1);
        const int sx = (r < h / 3) ? sway : 0;
        for (int dx = -hw; dx <= hw; dx++) {
            uint8_t col = fill;
            if (dx * c.sunSide < 0 && std::abs(dx) >= hw - 1 && hw > 0) col = lit;
            if (snow >= 0 && tier == 0 && std::abs(dx) < hw) col = (uint8_t)snow;
            if (snow >= 0 && tier == 1 && std::abs(dx) < hw - 1 && dith(0.5f, x + dx, y)) col = (uint8_t)snow;
            c.cv.set(x + dx + sx, y, col, k);
        }
    }
    c.cv.vline(x, yBase - 2, yBase, c.V.ink, k);
    c.cv.set(x, top - 1, fill, k);
}

// A clump of foliage / blossom: a ragged ellipse, lit on top (toward the sun), shaded underneath.
void drawClump(Ctx& c, int cx, int cy, int rx, int ry, uint8_t base, uint8_t lit, uint8_t shade, uint32_t id, uint8_t k,
               bool onlyBack) {
    for (int yy = -ry; yy <= ry; yy++)
        for (int xx = -rx; xx <= rx; xx++) {
            const float e = (xx * xx) / (float)(rx * rx) + (yy * yy) / (float)(ry * ry);
            if (e > 1.05f) continue;
            if (e > 0.7f && hash01(cx + xx, cy + yy, id) < 0.35f) continue; // ragged edge
            const int X = cx + xx, Y = cy + yy;
            if (onlyBack && !skyOrWorld(c.cv, X, Y)) continue;
            uint8_t col = base;
            const float lx = xx * (float)-c.sunSide / rx - yy / (float)ry; // toward the light
            if (lx > 0.75f || (lx > 0.45f && dith(0.5f, X, Y))) col = lit;
            else if (yy > ry / 2 || (lx < -0.6f && dith(0.5f, X, Y))) col = shade;
            c.cv.set(X, Y, col, k);
        }
}

// A round-crowned tree (cherry): a trunk forking into branches, clumps of blossom on them, gaps of sky between.
void drawRoundTree(Ctx& c, int x, int yBase, int h, uint8_t trunk, uint8_t base, uint8_t lit, uint8_t shade, uint32_t id,
                   int sway = 0, uint8_t k = K_WORLD) {
    const int crownY = yBase - h * 6 / 10;
    const int cw = std::max(5, h * 5 / 10);
    for (int y = crownY; y <= yBase; y++) {
        const int w = 1 + (h > 30 ? 1 : 0) + (y > yBase - 3 ? 1 : 0);
        c.cv.rect(x - w / 2, y, w, 1, trunk, k);
    }
    const int n = 3 + h / 10;
    for (int i = 0; i < n; i++) {
        const float u = n == 1 ? 0.5f : (float)i / (n - 1);
        const int ex = x + iround((u - 0.5f) * 2.f * cw * 0.85f) + sway;
        const int ey = yBase - h + (int)(std::fabs(u - 0.5f) * h * 0.5f) + (int)(hash01((int)id, i, c.seed) * h * 0.12f);
        c.cv.line(x, crownY + 2, ex, ey + 2, trunk, k);
    }
    // back clumps (shaded), then front clumps
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n + 2; i++) {
            const float u = (float)i / (n + 1);
            const int ex = x + iround((u - 0.5f) * 2.f * cw * (pass ? 0.8f : 0.95f)) + sway;
            const int ey = yBase - h + (int)(std::fabs(u - 0.5f) * h * 0.4f) + (pass ? 2 : -1) +
                           (int)(hash01((int)id, i + 20 * pass, c.seed) * h * 0.15f);
            const int rx = std::max(3, h / 7 + (int)(hash01((int)id, i + 40, c.seed) * 3));
            const int ry = std::max(2, rx * 2 / 3);
            if (pass == 0) drawClump(c, ex, ey, rx, ry, shade, base, shade, id + i, k, false);
            else drawClump(c, ex, ey, rx - 1, ry, base, lit, shade, id + 7 * i, k, false);
        }
}

// ------------------------------------------------------------------ water

// The water mirrors what is above the horizon (darker), with a shimmer that steps at 8 fps and the sun's
// glitter, livelier on the beat at mid and peak.
void drawWater(Ctx& c, const Palette& P, int top, int bottom, int sunX, uint8_t ripple, uint8_t glitter) {
    const int st = c.s.step(8.f);
    const float live = 0.4f + 0.6f * c.s.mid + 0.6f * c.s.peak;
    for (int y = top; y < bottom && y < c.H; y++) {
        const int d = y - top;
        const int srcY = top - 1 - d * 2 / 3 - d / 4;
        const float amp = (0.4f + d * 0.05f) * live;
        const int sh = iround(std::sin(y * 1.7f + st * 0.9f) * amp);
        for (int x = 0; x < c.W; x++) {
            const int sx = std::clamp(x + sh, 0, c.W - 1), sy = std::clamp(srcY, 0, c.H - 1);
            uint8_t col = P.darker[c.cv.get(sx, sy)];
            if ((d % 5) == 2 && dith(0.18f * live, x + st, y)) col = ripple;
            // sun glitter: short streaks in a column under the sun, blinking with the beat
            const int gw = 3 + d / 3;
            if (std::abs(x - sunX) < gw && (hash2(x / 3, y, (uint32_t)(c.s.beatIndex() + st / 4)) & 7) == 0 &&
                (d % 2 == 0))
                col = glitter;
            c.cv.set(x, y, col, K_WORLD);
        }
    }
}

// ------------------------------------------------------------------ sprites

// Critter frames (5 x 3): wings up, level, down, level.
const char* BIRD[4][3] = {{"#...#", ".#.#.", "..#.."}, {".....", "##.##", "..#.."}, {".....", ".###.", "#.#.#"},
                          {".....", "##.##", "..#.."}};

void drawSprite(Canvas& cv, const char* const* rows, int nrows, int x, int y, uint8_t col, uint8_t k, bool onlyBack) {
    for (int r = 0; r < nrows; r++)
        for (int i = 0; rows[r][i]; i++)
            if (rows[r][i] == '#' && (!onlyBack || skyOrWorld(cv, x + i, y + r))) cv.set(x + i, y + r, col, k);
}

// Grass / reed blades from the bottom, swaying a pixel on the beat.
void drawBlades(Ctx& c, int x0, int x1, int base, int hmax, uint8_t col, uint8_t lit, int slide, int sway, uint32_t sd) {
    for (int x = x0; x <= x1; x++) {
        const float edge = 1.f - std::fabs((x - x0) / (float)std::max(1, x1 - x0) - (x0 < c.W / 2 ? 0.f : 1.f));
        const int h = (int)((3 + hash01(x, 1, sd) * hmax) * (0.35f + 0.65f * edge));
        if (hash01(x, 2, sd) < 0.25f) continue;
        const int lean = hash01(x, 3, sd) < 0.5f ? -1 : 1;
        for (int i = 0; i < h; i++) {
            const int bx = x + (i > h * 2 / 3 ? lean + sway : 0);
            const int y = base - i + slide;
            if (!skyOrWorld(c.cv, bx, y)) continue;
            c.cv.set(bx, y, (i == h - 1 && hash01(x, 4, sd) < 0.5f) ? lit : col, K_WORLD);
        }
    }
}

// A branch from a top corner: a tapering bough with twigs, clumps of leaves / blossoms / needles hanging off it.
void drawBranch(Ctx& c, bool left, int slide, int sway, uint8_t wood, uint8_t leaf, uint8_t leafLit, uint8_t leafDark,
                int len, bool needles, uint32_t sd) {
    const int dir = left ? 1 : -1;
    const int sx = left ? -2 : c.W + 1, sy = 3 - slide;
    std::vector<std::pair<int, int>> tips;
    int px = sx, py = sy;
    for (int i = 0; i <= len; i++) {
        const float t = (float)i / len;
        const int x = sx + dir * i;
        const int y = sy + iround(len * 0.32f * t * t + std::sin(t * 5.f + sd) * 2.f) + (t > 0.5f ? sway : 0);
        const int th = t < 0.3f ? 3 : t < 0.65f ? 2 : 1;
        for (int k = 0; k < th; k++)
            if (skyOrWorld(c.cv, x, y + k)) c.cv.set(x, y + k, wood, K_WORLD);
        if (i > 6 && i % 11 == 4) { // a twig, down and outward
            const int l = 4 + (int)(hash01(i, 3, sd) * 5);
            int tx = x, ty = y + th;
            for (int k = 0; k < l; k++) {
                tx += (k % 2) * dir;
                ty += 1;
                if (skyOrWorld(c.cv, tx, ty)) c.cv.set(tx, ty, wood, K_WORLD);
            }
            tips.push_back({tx, ty});
        }
        if (i % 8 == 2) tips.push_back({x, y + 2});
        px = x;
        py = y;
    }
    tips.push_back({px, py + 1});
    for (size_t n = 0; n < tips.size(); n++) {
        const int cx = tips[n].first, cy = tips[n].second;
        const int rx = 3 + (int)(hash01((int)n, 2, sd) * 4), ry = 2 + (int)(hash01((int)n, 5, sd) * 2);
        if (needles) {
            // a fan of needles: short strokes in rows
            for (int r = -ry; r <= ry; r++)
                for (int xx = -rx - 1; xx <= rx + 1; xx++) {
                    if (((xx + r * 2) & 1) || hash01(cx + xx, cy + r, sd) < 0.2f) continue;
                    if (std::abs(xx) > rx + 1 - std::abs(r)) continue;
                    const int X = cx + xx, Y = cy + r + (std::abs(xx) > rx / 2 ? 1 : 0);
                    if (skyOrWorld(c.cv, X, Y)) c.cv.set(X, Y, r < 0 ? leafLit : (r > 0 ? leafDark : leaf), K_WORLD);
                }
        } else {
            drawClump(c, cx, cy + 1, rx, ry, leaf, leafLit, leafDark, sd + (uint32_t)n * 13, K_WORLD, true);
        }
    }
}

int pmod(int a, int m) { return ((a % m) + m) % m; }

int slideOf(float presence, int dist) { return iround((1.f - smoothstepf(0.f, 1.f, presence)) * dist); }

} // namespace

// ================================================================== World

void World::setVariant(int v, uint64_t seed) {
    v_ = std::clamp(v, 0, VARIANT_COUNT - 1);
    seed_ = seed;
    pal_ = makePalette(VARIANTS[v_]);
    rng_ = Rng(seed ^ 0x9E77ull);
    birds_.clear();
    flies_.clear();
    drops_.clear();
    splashes_.clear();
    bombs_.clear();
    boltOn_ = false;
}

void World::gust(float strength) { gust_ = std::max(gust_, strength); }

void World::update(const WorldState& s, float wdt) {
    const Variant& V = VARIANTS[v_];
    const float e = saturate(s.energy);
    const int beat = s.beatIndex();
    const bool onBeat = beat != lastBeat_;
    lastBeat_ = beat;
    gust_ = std::max(0.f, gust_ - wdt * 0.5f);

    // ---- Birds (mid and peak): small flocks crossing the sky; a refresh sends one at once.
    {
        const bool gulls = V.theme == TH_COAST, hawk = V.theme == TH_CANYON;
        const int want = (int)(s.mid * (hawk ? 1 : 5) + s.peak * (hawk ? 1 : 2));
        birdCool_ -= wdt;
        if (((int)birds_.size() < want && birdCool_ <= 0) || gust_ > 0.3f) {
            if (hawk) {
                birds_.push_back({rng_.range(30.f, s.W - 30.f), rng_.range(18.f, s.hz - 50.f), 0, 0, 1, rng_.range(0.f, TAU)});
            } else {
                const bool fromLeft = rng_.chance(0.5f);
                const int n = rng_.irange(2, 5);
                const float y = rng_.range(12.f, s.hz - 40.f), sp = rng_.range(9.f, 16.f) * (fromLeft ? 1 : -1);
                for (int i = 0; i < n; i++)
                    birds_.push_back({fromLeft ? -8.f - i * 7 : s.W + 8.f + i * 7, y + (i % 2) * 4 + i, sp,
                                      rng_.range(-0.6f, 0.6f), gulls ? 2 : 0, rng_.range(0.f, 4.f)});
            }
            birdCool_ = rng_.range(4.f, 9.f) / (0.5f + s.mid);
            gust_ = 0;
        }
        for (size_t i = 0; i < birds_.size();) {
            Bird& b = birds_[i];
            if (b.kind == 1) {
                b.phase += wdt * 0.5f;
                if (s.mid < 0.2f) b.y -= wdt * 10;
            } else {
                b.x += b.vx * wdt;
                b.y += (b.vy + std::sin(b.phase + s.t * 1.3f) * 1.5f) * wdt;
            }
            if (b.x < -30 || b.x > s.W + 30 || b.y < -20) { birds_[i] = birds_.back(); birds_.pop_back(); continue; }
            i++;
        }
    }

    // ---- Fireflies (forest, jungle, cherry; any place at night).
    {
        const bool fl = V.theme == TH_FOREST || V.theme == TH_JUNGLE || V.theme == TH_CHERRY;
        const int want = (int)((fl ? s.mid * 9 + s.peak * 5 : 0) + s.night * 12);
        while ((int)flies_.size() < want) {
            const bool left = rng_.chance(0.5f);
            const float x = left ? rng_.range(4.f, s.bx0 - 56.f) : rng_.range(s.bx1 + 56.f, s.W - 4.f);
            flies_.push_back({x, rng_.range(s.hz - 10.f, s.H - 8.f), 0, 0, rng_.range(0.f, 100.f)});
        }
        while ((int)flies_.size() > want) flies_.pop_back();
        for (Fly& f : flies_) {
            f.vx += rng_.range(-1.f, 1.f) * wdt * 20;
            f.vy += rng_.range(-1.f, 1.f) * wdt * 20;
            f.vx *= std::exp(-wdt);
            f.vy *= std::exp(-wdt);
            f.x += f.vx * wdt;
            f.y += f.vy * wdt;
            f.y = std::clamp(f.y, (float)s.hz - 30.f, (float)s.H - 4.f);
            f.x = std::clamp(f.x, 2.f, (float)s.W - 3.f);
        }
    }

    // ---- Weather (peak; a little at mid for the gentle kinds).
    {
        float amt = s.peak * (0.55f + 0.45f * e);
        if (V.weather == W_SNOW || V.weather == W_PETALS || V.weather == W_LEAVES) amt += s.mid * 0.12f;
        if (V.weather == W_ASH) amt += s.mid * 0.2f;
        amt += gust_ * 0.3f;
        float rate = 0;
        switch (V.weather) {
        case W_RAIN: rate = 300; break;
        case W_SAND: rate = 160; break;
        case W_SNOW: rate = 120; break;
        case W_PETALS: rate = 45; break;
        case W_ASH: rate = 90; break;
        case W_LEAVES: rate = 32; break;
        }
        float n = rate * amt * wdt;
        while (n > 0) {
            if (n < 1 && !rng_.chance(n)) break;
            n -= 1;
            Drop d{};
            d.kind = V.weather;
            d.seed = rng_.range(0.f, 100.f);
            d.age = 0;
            switch (V.weather) {
            case W_RAIN:
                d.x = rng_.range(-20.f, (float)s.W);
                d.y = rng_.range(-10.f, 0.f);
                d.vx = 45;
                d.vy = rng_.range(210.f, 260.f);
                d.life = rng_.range((float)s.hz - 30, (float)s.H + 5);
                break;
            case W_SAND:
                d.x = -10;
                d.y = rng_.range(s.hz - 60.f, (float)s.H);
                d.vx = rng_.range(140.f, 220.f);
                d.vy = rng_.range(-6.f, 6.f);
                d.life = 9;
                break;
            case W_SNOW:
                d.x = rng_.range(-10.f, (float)s.W + 10);
                d.y = -2;
                d.vx = rng_.range(-4.f, 8.f);
                d.vy = rng_.range(14.f, 30.f) * (1.f + s.peak * 0.6f);
                d.life = rng_.range((float)s.hz, (float)s.H);
                d.kind = rng_.chance(0.25f) ? 100 + W_SNOW : W_SNOW;
                break;
            case W_PETALS:
            case W_LEAVES:
                d.x = rng_.range(-30.f, (float)s.W);
                d.y = -3;
                d.vx = rng_.range(10.f, 28.f) * (1 + s.peak);
                d.vy = rng_.range(12.f, 24.f);
                d.life = 99;
                break;
            case W_ASH:
                if (rng_.chance(0.3f)) { // embers rise
                    d.kind = 200;
                    d.x = rng_.range(0.f, (float)s.W);
                    d.y = (float)s.H + 2;
                    d.vx = rng_.range(-6.f, 6.f);
                    d.vy = -rng_.range(14.f, 30.f);
                } else {
                    d.x = rng_.range(-10.f, (float)s.W + 10);
                    d.y = -2;
                    d.vx = rng_.range(-5.f, 3.f);
                    d.vy = rng_.range(8.f, 18.f);
                }
                d.life = 99;
                break;
            }
            drops_.push_back(d);
        }
        for (size_t i = 0; i < drops_.size();) {
            Drop& d = drops_[i];
            d.age += wdt;
            float sway = 0;
            if (d.kind == W_SNOW || d.kind == 100 + W_SNOW) sway = std::sin(d.age * 2.f + d.seed) * 6.f;
            if (d.kind == W_PETALS || d.kind == W_LEAVES) sway = std::sin(d.age * 3.f + d.seed) * 14.f;
            d.x += (d.vx + sway + gust_ * 40.f) * wdt;
            d.y += d.vy * wdt;
            bool dead = d.x > s.W + 30 || d.y > s.H + 6 || d.y < -12 || d.x < -40;
            if ((d.kind == W_RAIN || d.kind == W_SNOW || d.kind == 100 + W_SNOW) && d.y >= d.life) {
                if (d.kind == W_RAIN && splashes_.size() < 300) splashes_.push_back({d.x, d.y, 0, 0, 0.12f, 0, 0, 0});
                dead = true;
            }
            if (dead) { drops_[i] = drops_.back(); drops_.pop_back(); continue; }
            i++;
        }
        for (size_t i = 0; i < splashes_.size();) {
            splashes_[i].age += wdt;
            if (splashes_[i].age > splashes_[i].life) { splashes_[i] = splashes_.back(); splashes_.pop_back(); continue; }
            i++;
        }
    }

    // ---- Lightning (rain places, peak): on a strong hit, with a few seconds between bolts.
    boltCool_ -= wdt;
    if (boltOn_) {
        bolt_.age += wdt;
        if (bolt_.age > 0.32f) boltOn_ = false;
    }
    flash_ = boltOn_ ? (bolt_.age < 0.07f ? 1.f : (bolt_.age > 0.14f && bolt_.age < 0.2f ? 0.6f : 0.f)) : 0.f;
    if (V.weather == W_RAIN && s.peak > 0.6f && boltCool_ <= 0 && (s.kick > 0.45f || (onBeat && beat % 8 == 0 && rng_.chance(0.3f)))) {
        boltOn_ = true;
        bolt_.age = 0;
        const bool left = rng_.chance(0.5f);
        bolt_.x0 = left ? rng_.irange(10, std::max(11, s.bx0 - 60)) : rng_.irange(std::min(s.W - 11, s.bx1 + 60), s.W - 10);
        bolt_.y0 = 0;
        bolt_.y1 = s.hz - rng_.irange(4, 20);
        bolt_.xs.clear();
        int x = bolt_.x0;
        for (int y = 0; y <= bolt_.y1; y++) {
            if (y % 3 == 0) x += rng_.irange(-2, 2);
            bolt_.xs.push_back(x);
        }
        boltCool_ = rng_.range(3.5f, 7.f);
    }

    // ---- Eruption (volcano, peak): lava bombs from the crater on beats.
    if (V.theme == TH_VOLCANO) {
        if (onBeat && s.peak > 0.5f) {
            const int n = 1 + (int)(e * 3) + (beat % 4 == 0 ? 2 : 0);
            const float cx = s.W - 56.f + iround(s.scroll * 0.5f), cy = (float)s.hz - 66;
            for (int i = 0; i < n; i++)
                bombs_.push_back({cx + rng_.range(-3.f, 3.f), cy, rng_.range(-28.f, 22.f), -rng_.range(45.f, 80.f), 0});
        }
        for (size_t i = 0; i < bombs_.size();) {
            Bomb& b = bombs_[i];
            b.age += wdt;
            b.vy += 70.f * wdt;
            b.x += b.vx * wdt;
            b.y += b.vy * wdt;
            if (b.y > s.hz + 4 || b.age > 4) { bombs_[i] = bombs_.back(); bombs_.pop_back(); continue; }
            i++;
        }
    }
    // ---- A fish jumps in the lake on a downbeat (forest, jungle).
    if ((V.theme == TH_FOREST || V.theme == TH_JUNGLE || V.theme == TH_CHERRY) && s.mid > 0.5f) {
        fishCool_ -= wdt;
        if (fishT_ < 0 && fishCool_ <= 0 && onBeat && beat % 4 == 0) {
            fishT_ = 0;
            fishX_ = rng_.chance(0.5f) ? rng_.range(20.f, s.bx0 - 60.f) : rng_.range(s.bx1 + 60.f, s.W - 20.f);
            fishCool_ = rng_.range(6.f, 12.f);
        }
    }
    if (fishT_ >= 0) {
        fishT_ += wdt;
        if (fishT_ > 1.2f) fishT_ = -1;
    }
    prevPhase_ = s.beatPhase;
}

// ------------------------------------------------------------------ back

void World::drawBack(Canvas& cv, const WorldState& s) {
    const Variant& V = VARIANTS[v_];
    const Palette& P = pal_;
    Ctx c{cv, V, s, (uint32_t)(seed_ & 0xFFFFFFFF), s.W, s.H, s.hz, V.sunX < 0.5f ? -1 : 1};
    const int b1 = s.bounce, b2 = s.bounce > 0 ? s.bounce : 0;
    const int sunX = iround(V.sunX * s.W), sunY = s.hz - V.sunUp;
    const int sunR = (V.theme == TH_CITY ? 6 : 8) + (s.peak > 0.5f ? s.bounce : 0);
    const int sway = (s.mid > 0.5f && ((s.beatIndex() & 1) == 0)) ? 1 : 0;
    const float cloudSpeed = 0.3f + 1.5f * s.mid + 2.5f * s.peak;
    const int clouds = 2 + (int)(s.mid * 1.5f + s.peak * 2.5f);
    const uint32_t sd = c.seed;
    const int nearSlide = slideOf(s.mid, 46);

    // Sky.
    uint8_t sky[5];
    for (int i = 0; i < 5; i++) sky[i] = V.sky[i];
    drawSky(c, sky, s.hz);
    if (V.theme == TH_SNOW && s.peak > 0.01f) {
        // Snow peaks: dusk falls at the peak (a darker sky for the aurora), dissolving in.
        const uint8_t dusk[5] = {1, 2, 2, 3, 6};
        Canvas tmp;
        for (int y = 0; y < s.hz; y++) {
            const float t = (float)y / s.hz;
            static const float pos[5] = {0.f, 0.36f, 0.62f, 0.83f, 1.f};
            int k = 0;
            while (k < 3 && t >= pos[k + 1]) k++;
            const float f = std::floor(saturate((t - pos[k]) / (pos[k + 1] - pos[k])) * 4.f) / 4.f;
            for (int x = 0; x < s.W; x++)
                if (dith(s.peak, x, y + 1)) cv.set(x, y, dith(f, x, y) ? dusk[k + 1] : dusk[k], K_SKY);
        }
    }
    if (V.theme == TH_CITY || V.theme == TH_VOLCANO) drawStars(c, s.hz - 20, V.theme == TH_CITY ? 1.f : 0.4f, V.text, V.textDim);
    if (V.theme == TH_SNOW) drawStars(c, s.hz - 30, s.peak, 5, 4);

    // Aurora (snow peak): curtains of light rippling with the bands, a new fold on each beat.
    if (V.theme == TH_SNOW && s.peak > 0.05f) {
        const uint8_t cols[3] = {12, 10, 15};
        const int st = s.step(10.f);
        for (int x = 0; x < s.W; x++) {
            const float fx = x * 0.035f;
            const float base = 22 + 10 * std::sin(fx + st * 0.05f) + 5 * std::sin(fx * 2.3f - s.beatIndex() * 0.7f);
            const float hgt = (14 + 18 * (0.5f + 0.5f * std::sin(fx * 1.7f + 1.3f))) * (0.6f + 0.6f * s.high);
            for (int y = (int)(base - hgt); y <= (int)base; y++) {
                if (y < 0 || cv.kind(x, y) != K_SKY) continue;
                const float u = (base - y) / hgt; // 0 bottom .. 1 top
                const int ci = u < 0.18f ? 0 : u < 0.6f ? 1 : 2;
                const float cov = (u < 0.18f ? 0.9f : (1.f - u) * 0.8f) * s.peak;
                if (dith(cov, x, y)) cv.set(x, y, cols[ci], K_SKY);
            }
        }
    }
    // Searchlights (city peak): two beams sweeping the clouds, behind the skyline.
    if (V.theme == TH_CITY && s.peak > 0.05f) {
        for (int i = 0; i < 2; i++) {
            const float ox = i == 0 ? s.W * 0.12f : s.W * 0.88f, oy = (float)s.hz;
            const float a = -PI * 0.5f + std::sin(s.t * 0.6f + i * 2.1f) * 0.55f;
            const float dx = std::cos(a), dy = std::sin(a);
            for (int y = 0; y < s.hz; y++)
                for (int x = 0; x < s.W; x++) {
                    const float px = x - ox, py = y - oy;
                    const float along = px * dx + py * dy;
                    if (along < 0) continue;
                    const float across = std::fabs(px * -dy + py * dx);
                    const float wdt = 1.5f + along * 0.07f;
                    if (across > wdt || cv.kind(x, y) != K_SKY) continue;
                    if (dith((across < wdt * 0.5f ? 0.5f : 0.25f) * s.peak, x, y)) cv.set(x, y, 4, K_SKY);
                }
        }
    }

    drawSun(c, sunX, sunY - (V.theme == TH_SNOW ? iround(s.peak * 14) : 0), sunR, V.sun, V.glow, 0.5f + 0.5f * s.mid);
    if (V.theme == TH_CITY) { // moon craters
        cv.set(sunX - 2, sunY - 1, 4, K_SKY);
        cv.set(sunX + 2, sunY + 2, 4, K_SKY);
        cv.set(sunX + 1, sunY - 3, 4, K_SKY);
    }
    {
        const bool storm = V.weather == W_RAIN && s.peak > 0.5f;
        const uint8_t cb = storm ? V.sky[0] : V.cloud, cl = storm ? V.cloud : V.cloudLit;
        drawClouds(c, clouds + (storm ? 3 : 0), cloudSpeed, 10, s.hz - 46, cb, cl, storm ? P.darker[cb] : P.shade[cb]);
    }
    // Lightning bolt (behind the ranges: it strikes the far hills).
    if (boltOn_ && bolt_.age < 0.2f) {
        for (int y = bolt_.y0; y <= bolt_.y1 && y < (int)bolt_.xs.size(); y++) {
            cv.set(bolt_.xs[y], y, 8, K_SKY);
            if (bolt_.age < 0.07f) cv.set(bolt_.xs[y] + 1, y, V.glow, K_SKY);
        }
    }

    // ---- The place.
    const int bot = s.H;
    switch (V.theme) {
    case TH_FOREST: {
        drawPeaks(c, 0.15f, s.hz, 52, 26, 50, 0.7f, 1.2f, 3.f, V.far, V.farLit, 0, 0, 0, 7, V.sky[4], s.hz, sd + 1);
        // far shore: a forest band, a cabin with a lit window and smoke
        drawBand(c, 0.35f, [&](int wx) { return pineTop(wx, sd + 2, s.hz - 2 - iround(3 * std::sin(wx * 0.03f)), 5, 3, 12); },
                 s.hz, V.mid, V.midLit, 1);
        {
            const int cx = iround(s.W * 0.86f) + c.off(0.35f), cy = s.hz - 6;
            cv.rect(cx - 4, cy - 3, 9, 4, V.ink, K_WORLD);
            cv.line(cx - 5, cy - 3, cx, cy - 7, V.ink, K_WORLD);
            cv.line(cx + 5, cy - 3, cx, cy - 7, V.ink, K_WORLD);
            cv.set(cx + 2, cy - 2, V.sun, K_WORLD);
            cv.rect(cx + 2, cy - 9, 1, 3, V.ink, K_WORLD);
            const int st = s.step(6.f);
            for (int i = 0; i < 5; i++) {
                const int sy = cy - 11 - i * 3 - (st % 3);
                const int sx = cx + 2 + iround(std::sin((sy + st) * 0.5f) * 1.f) - i / 2;
                if (cv.kind(sx, sy) == K_SKY && i < 4) cv.set(sx, sy, V.cloudLit, K_SKY);
            }
        }
        drawWater(c, P, s.hz, bot, sunX, V.sky[3], V.sun);
        // shores at the bottom corners, with pines (small in calm, the big near ones slide in at mid)
        auto shore = [&](int wx, bool left) {
            const float u = left ? wx / (float)(s.bx0 - 20) : (s.W - wx) / (float)(s.W - s.bx1 - 20);
            return s.hz + 20 + iround(std::max(0.f, u) * 34 + 2 * std::sin(wx * 0.2f));
        };
        drawBand(c, 0.6f, [&](int wx) { return wx < s.W / 2 ? shore(wx, true) : shore(wx, false); }, bot, V.ground, V.groundLit, 1);
        for (int i = 0; i < 3; i++) {
            const int x = 10 + i * 17 + c.off(0.6f), x2 = s.W - 12 - i * 19 + c.off(0.6f);
            drawPine(c, x, shore(x, true) + 1, 12 + i * 3, V.nearC, V.nearLit, -1);
            drawPine(c, x2, shore(x2, false) + 1, 14 + i * 2, V.nearC, V.nearLit, -1);
        }
        if (s.mid > 0.01f) {
            const int big[4] = {6, 34, s.W - 8, s.W - 36};
            const int bh[4] = {58, 44, 62, 40};
            for (int i = 0; i < 4; i++) {
                const int x = big[i] + c.off(1.f);
                drawPine(c, x, bot + 2 + nearSlide - b1 * (i & 1), bh[i], V.ink, V.nearC, -1, K_WORLD, sway);
            }
        }
        break;
    }
    case TH_CANYON: {
        // far mesas
        drawBand(c, 0.15f, [&](int wx) {
            const float v = fbm1(wx * 0.018f, sd + 1, 3);
            const float k = smoothstepf(0.48f, 0.53f, v);
            return s.hz - 6 - iround((6 + 26 * k) / 2) * 2 - (int)(hash01(wx / 3, 1, sd) * 1.5f * k);
        }, s.hz + 1, V.far, V.farLit, 2);
        // mid buttes with strata
        {
            const int o = c.off(0.4f);
            auto butte = [&](int wx) {
                const float cx1 = s.W * 0.13f, cx2 = s.W * 0.85f, cx3 = s.W * 0.33f;
                float h = 0;
                auto one = [&](float cx, float hw, float ht) {
                    const float d = std::fabs(wx - cx);
                    if (d < hw) return ht;
                    if (d < hw + 10) return ht * (1.f - (d - hw) / 10.f) * 0.55f;
                    return 0.f;
                };
                h = std::max({one(cx1, 22, 56), one(cx2, 26, 48), one(cx3, 9, 30)});
                h += (hash01(wx / 2, 2, sd) - 0.5f) * 2.f * (h > 0);
                return s.hz + 4 - iround(h);
            };
            for (int x = 0; x < s.W; x++) {
                const int wx = x + o;
                const int t0 = butte(wx), tl = butte(wx - 1), tr = butte(wx + 1);
                const int slope = tr - tl;
                for (int y = std::max(0, t0); y < s.hz + 6; y++) {
                    uint8_t col = V.mid;
                    const int d = y - t0;
                    if (d == 0 || (c.sunSide > 0 ? slope > 0 : slope < 0) || (std::abs(slope) > 4 && d < 3)) col = V.midLit;
                    if ((y % 6) == 3 && d > 1 && hash01(wx / 4, y, sd) < 0.7f) col = V.nearC; // strata
                    cv.set(x, y, col, K_WORLD);
                }
            }
        }
        // desert floor: dunes in dithered bands, small rocks
        for (int y = s.hz; y < bot; y++)
            for (int x = 0; x < s.W; x++) {
                if (y < s.hz + 4 && cv.kind(x, y) != K_SKY) continue; // the mesas' feet stay
                const int d = y - s.hz;
                // dune ridges: lit crests, shaded troughs, spaced wider toward the viewer
                (void)d;
                uint8_t col = V.ground;
                int prevR = -1000, nextR = 1000;
                for (int k = 0; k < 12; k++) {
                    const int r = s.hz + 6 + k * (5 + k * 2) + iround(2.5f * std::sin((x + c.off(0.5f + k * 0.05f)) * 0.035f + k * 1.7f));
                    if (r <= y) prevR = r;
                    else { nextR = r; break; }
                }
                if (y - prevR == 0 || (y - prevR == 1 && dith(0.5f, x, y))) col = V.groundLit;
                else if (nextR - y <= 1 && dith(0.5f, x, y)) col = V.mid;
                if (hash01(x + c.off(0.7f), y, sd) < 0.006f) col = V.nearC;
                cv.set(x, y, col, K_WORLD);
            }
        // heat shimmer on the horizon at peak (rows slide by a pixel)
        if (s.peak > 0.3f) {
            const int st = s.step(10.f);
            for (int y = s.hz - 8; y < s.hz + 2; y++) {
                const int sh = ((y + st) % 4 == 0) ? 1 : 0;
                if (!sh) continue;
                for (int x = s.W - 1; x > 0; x--) cv.set(x, y, cv.get(x - 1, y), cv.kind(x - 1, y));
            }
        }
        // near rocks and saguaros (slide in at mid)
        auto rock = [&](int cx, int base, int w, int h) {
            for (int y = 0; y < h; y++) {
                const int hw = iround(w * std::sqrt(1.f - (float)y / h));
                for (int dx = -hw; dx <= hw; dx++) {
                    uint8_t col = V.nearC;
                    if (dx * c.sunSide < 0 && dx * -c.sunSide > hw / 3) col = V.nearLit;
                    if (y == h - 1) col = V.nearLit;
                    cv.set(cx + dx, base - y, col, K_WORLD);
                }
            }
        };
        auto cactus = [&](int x, int base, int h, int arm) {
            cv.rect(x - 1, base - h, 3, h, 13, K_WORLD);
            cv.vline(x - 1 - (c.sunSide > 0), base - h + 1, base, 14, K_WORLD);
            cv.vline(x + 1 + (c.sunSide < 0) * 0, base - h + 1, base, V.ink, K_WORLD);
            cv.set(x, base - h - 1, 13, K_WORLD);
            const int ay = base - h / 2 - arm;
            cv.rect(x - 4, ay, 3, 2, 13, K_WORLD);
            cv.rect(x - 4, ay - 5, 2, 6, 13, K_WORLD);
            cv.rect(x + 2, ay + 3, 3, 2, 13, K_WORLD);
            cv.rect(x + 3, ay - 1, 2, 5, 13, K_WORLD);
        };
        cactus(40 + c.off(0.7f), s.hz + 22, 12, 1);
        cactus(s.W - 30 + c.off(0.7f), s.hz + 26, 10, 0);
        if (s.mid > 0.01f) {
            rock(10 + c.off(1.f), bot + 1 + nearSlide - b1, 22, 18);
            rock(s.W - 14 + c.off(1.f), bot + 1 + nearSlide - b1, 26, 22);
            cactus(58 + c.off(1.f), bot - 2 + nearSlide - b1, 26, 3);
            cactus(s.W - 54 + c.off(1.f), bot - 2 + nearSlide, 22, 1);
        }
        break;
    }
    case TH_SNOW: {
        drawPeaks(c, 0.12f, s.hz, 64, 46, 86, 0.8f, 1.35f, 4.f, V.far, V.farLit, 60, 5, 4, 0, 0, s.hz, sd + 1);
        drawPeaks(c, 0.3f, s.hz + 4, 40, 18, 40, 0.8f, 1.2f, 3.f, V.mid, 4, 9, 5, 4, 6, 4, s.hz + 4, sd + 2);
        // snowfield, softly shaded
        for (int y = s.hz; y < bot; y++)
            for (int x = 0; x < s.W; x++) {
                if (y < s.hz + 4 && cv.kind(x, y) == K_WORLD) continue; // under the mid range's foot
                const float t = std::sin((x + c.off(0.6f)) * 0.04f + y * 0.25f) * 0.5f + 0.5f;
                uint8_t col = dith(t * 0.45f, x, y) ? 4 : 5;
                if (hash01(x, y, sd) < 0.004f && ((s.step(3.f) + x) & 3) == 0) col = 6; // sparkles
                cv.set(x, y, col, K_WORLD);
            }
        // a far line of small pines, then the near snowy pines (slide in at mid)
        for (int i = 0; i < 9; i++) {
            const int x = (int)(hash01(i, 1, sd) * s.W) + c.off(0.5f);
            if (x > s.bx0 - 50 && x < s.bx1 + 50) continue;
            drawPine(c, x, s.hz + 10 + (int)(hash01(i, 2, sd) * 6), 9 + (int)(hash01(i, 3, sd) * 6), 8, 9, 5);
        }
        if (s.mid > 0.01f) {
            const int xs[5] = {8, 30, 52, s.W - 10, s.W - 34};
            const int hs[5] = {54, 40, 28, 58, 42};
            for (int i = 0; i < 5; i++)
                drawPine(c, xs[i] + c.off(1.f), bot + 2 + nearSlide - b1 * (i & 1), hs[i], 8, 9, 5, K_WORLD, sway);
        }
        break;
    }
    case TH_CITY: {
        // far skyline: tall towers, a few dim windows, red beacons blinking on the beat
        auto tower = [&](int wx, uint32_t sdd, int period, int hmin, int hmax) {
            const int b = (int)std::floor(wx / (float)period);
            const int h = hmin + (int)(hash01(b, 1, sdd) * (hmax - hmin));
            return std::make_pair(b, h);
        };
        {
            const int o = c.off(0.2f);
            for (int x = 0; x < s.W; x++) {
                const int wx = x + o;
                auto a = tower(wx, sd + 1, 9, 10, 46), b = tower(wx + 4, sd + 2, 13, 6, 30);
                const int h = std::max(a.second, b.second);
                const bool spire = (wx % 9 == 4) && a.second > 38;
                for (int y = s.hz - h - (spire ? 6 : 0); y < s.hz; y++) {
                    uint8_t col = V.far;
                    if (y >= s.hz - h && ((wx % 3) == 1) && ((y % 4) == 2) && hash01(wx / 3, y / 4, sd) < 0.12f) col = V.farLit + 1;
                    cv.set(x, y, col, K_WORLD);
                }
                if (spire && (s.beatIndex() & 1)) cv.set(x, s.hz - h - 7, 15, K_WORLD);
            }
        }
        // mid skyline: windows toggling on the beat (mid and peak), neon signs
        {
            const int o = c.off(0.45f);
            const int bt = s.beatIndex();
            for (int x = 0; x < s.W; x++) {
                const int wx = x + o;
                auto a = tower(wx, sd + 3, 11, 8, 34), b = tower(wx + 5, sd + 4, 7, 4, 20);
                const bool useA = a.second >= b.second;
                const int h = std::max(a.second, b.second);
                const int bid = useA ? a.first : 1000 + b.first;
                const int lx = useA ? ((wx % 11) + 11) % 11 : (((wx + 5) % 7) + 7) % 7;
                const int bw = useA ? 11 : 7;
                for (int y = s.hz + 2 - h; y < s.hz + 2; y++) {
                    uint8_t col = V.mid;
                    if (y == s.hz + 2 - h || (lx == (c.sunSide > 0 ? bw - 1 : 0))) col = V.midLit;
                    if (lx > 0 && lx < bw - 1 && (lx % 2) == 1 && ((y - (s.hz + 2 - h)) % 3) == 2 && y < s.hz - 1) {
                        const uint32_t hw = hash2(bid * 37 + lx, y, sd + (uint32_t)(s.mid > 0.5f ? bt / 2 : 0));
                        const float on = 0.25f + 0.25f * s.mid;
                        if ((hw & 255) < on * 255) col = (hw >> 8) % 5 == 0 ? 10 : (hw >> 8) % 3 == 0 ? 6 : 7;
                    }
                    cv.set(x, y, col, K_WORLD);
                }
            }
            // neon signs on a few buildings, blinking on beats (mid+)
            for (int i = 0; i < 4; i++) {
                const int x = (int)(hash01(i, 5, sd) * s.W) + c.off(0.45f);
                if (x > s.bx0 - 48 && x < s.bx1 + 48) continue;
                const int y = s.hz - 18 - (int)(hash01(i, 6, sd) * 10);
                const uint8_t nc = i % 3 == 0 ? 5 : i % 3 == 1 ? 10 : 14;
                const bool on = s.mid < 0.5f || ((bt + i) % 4) != 0;
                cv.rect(x, y, 3, 10, V.ink, K_WORLD);
                for (int k = 0; k < 4; k++) cv.set(x + 1, y + 1 + k * 2, on ? nc : V.mid, K_WORLD);
            }
        }
        drawWater(c, P, s.hz + 2, bot, sunX, V.sky[2], 7);
        // the elevated railway (mid): pillars, deck, a train every two bars
        if (s.mid > 0.01f) {
            const int dy = s.hz + 20 + nearSlide;
            for (int x = 0; x < s.W; x++) {
                cv.set(x, dy, V.nearLit, K_WORLD);
                cv.set(x, dy + 1, V.nearC, K_WORLD);
                cv.set(x, dy + 2, V.nearC, K_WORLD);
                if (((x + c.off(0.8f)) % 24) < 3) cv.vline(x, dy + 3, bot, V.nearC, K_WORLD);
            }
            const float bars = (float)(s.beat / 8.0);
            const float u = bars - std::floor(bars);
            const int tl = 64;
            const int tx = iround(-tl + u * (s.W + tl * 2) * 1.0f);
            for (int x = tx; x < tx + tl; x++) {
                for (int y = dy - 6; y < dy; y++) {
                    uint8_t col = 9;
                    if (y == dy - 6) col = 3;
                    if (y == dy - 4 && ((x - tx) % 4) > 0 && (x - tx) % 16 != 0) col = 7;
                    if ((x - tx) % 16 == 0) col = V.ink;
                    cv.set(x, y, col, K_WORLD);
                }
            }
        }
        // quay at the bottom
        cv.rect(0, bot - 4, s.W, 4, V.ink, K_WORLD);
        for (int x = 0; x < s.W; x += 12) cv.rect(x + c.off(1.f) % 12, bot - 6, 1, 2, V.ink, K_WORLD);
        break;
    }
    case TH_COAST: {
        // far headland on the right
        drawPeaks(c, 0.12f, s.hz, 40, 4, 16, 0.4f, 0.8f, 2.f, V.far, V.farLit, 0, 0, 0, 3, V.sky[4], s.hz, sd + 1);
        // the sea, with crests
        drawWater(c, P, s.hz, bot, sunX, 5, 8);
        {
            const int st = s.step(6.f);
            const float waves = 0.25f + 0.4f * s.mid + 0.8f * s.peak;
            for (int y = s.hz + 3; y < bot; y += 3 + (y - s.hz) / 9) {
                for (int x = 0; x < s.W; x++) {
                    const float ph = (x + st * (1 + (y - s.hz) / 12)) * 0.12f + y;
                    const float v = std::sin(ph) * 0.5f + 0.5f;
                    if (v > 1.f - 0.18f * waves && hash01(x / 2, y, sd) < 0.8f) cv.set(x, y, 8, K_WORLD);
                }
            }
        }
        // the cliff on the left with grass and the lighthouse
        const int o = c.off(0.45f);
        auto cliff = [&](int wx) {
            const float edge = s.W * 0.22f;
            const float top = s.hz - 34 + 3 * std::sin(wx * 0.15f) + 2 * fbm1(wx * 0.2f, sd + 3, 2);
            if (wx < edge) return iround(top);
            return iround(top + (wx - edge) * 2.4f + 3 * std::sin(wx * 0.8f));
        };
        for (int x = 0; x < s.W; x++) {
            const int wx = x + o;
            const int t0 = cliff(wx);
            if (t0 >= bot) continue;
            const int tr = cliff(wx + 1);
            for (int y = std::max(0, t0); y < bot; y++) {
                const int d = y - t0;
                uint8_t col = V.mid;
                if (d < 2 && tr - t0 < 3) col = d == 0 ? 14 : 13;                // grass on top
                else if (tr - t0 >= 2 && d < 3) col = V.midLit;                    // the face toward the sea
                else if (hash01(wx / 4, y, sd) < 0.03f) col = V.midLit;
                else if (pmod(y + (int)(hash01(wx / 6, 1, sd) * 3), 6) == 0 && hash01(wx / 3, y, sd) < 0.8f) col = V.nearC;
                cv.set(x, y, col, K_WORLD);
            }
            // foam at the cliff foot
            const int fy = s.hz + 8 + (int)(hash01(wx, 9, sd) * 4);
            if (t0 < fy && tr >= fy - 6 && ((s.step(8.f) + wx) % 3) != 0) cv.set(x + 1, fy, 8, K_WORLD);
        }
        {
            const int lx = iround(s.W * 0.08f) - o * 0 + c.off(0.45f) * 0;
            const int lxx = lx - (o - c.off(0.45f));
            const int base = cliff(lxx + o) + 1;
            const int th = 26;
            for (int y = 0; y < th; y++) {
                const int hw = 3 - (y > th - 8 ? 0 : 0) - (y < 6 ? 0 : 0);
                const int w = y < th / 2 ? hw : hw + 1;
                for (int dx = -w + 1; dx < w; dx++) {
                    uint8_t col = 8;
                    if (((y / 4) & 1) == 1) col = 10;
                    if (dx * c.sunSide > w / 2) col = ((y / 4) & 1) ? 9 : 5;
                    cv.set(lxx + dx, base - y - 1, col, K_WORLD);
                }
            }
            const int ly = base - th - 1;
            cv.rect(lxx - 3, ly - 1, 7, 1, V.ink, K_WORLD);
            const bool lampOn = s.mid < 0.5f ? ((s.step(1.f) & 3) != 0) : true;
            cv.rect(lxx - 2, ly - 5, 5, 4, V.ink, K_WORLD);
            cv.rect(lxx - 1, ly - 4, 3, 3, lampOn ? 7 : 6, K_WORLD);
            cv.rect(lxx - 2, ly - 7, 5, 2, 10, K_WORLD);
            cv.set(lxx, ly - 8, V.ink, K_WORLD);
            // the beam sweeps at mid and peak, a turn every two bars
            if (s.mid > 0.3f) {
                const float a = (float)(s.beat / 8.0) * TAU;
                const float dx = std::cos(a);
                const float reach = std::fabs(dx);
                if (reach > 0.2f) {
                    const int dir = dx > 0 ? 1 : -1;
                    const int len = iround(120 * reach);
                    for (int i = 4; i < len; i++) {
                        const int bx = lxx + dir * i;
                        const int hw = 1 + i / 14;
                        for (int yy = -hw; yy <= hw; yy++) {
                            const int by = ly - 3 + yy;
                            if (!skyOrWorld(cv, bx, by) || cv.kind(bx, by) == K_WORLD) continue;
                            if (dith((std::abs(yy) < hw / 2 + 1 ? 0.5f : 0.25f) * s.mid * (1.f - (float)i / len), bx, by))
                                cv.set(bx, by, 6, K_SKY);
                        }
                    }
                }
            }
        }
        // sea stack on the right
        {
            const int cx = iround(s.W * 0.9f) + c.off(0.55f);
            for (int y = 0; y < 30; y++) {
                const int hw = 6 - y / 10 + (y < 3 ? -1 : 0);
                for (int dx = -hw; dx <= hw; dx++)
                    cv.set(cx + dx, s.hz + 6 - y, dx * c.sunSide < -hw / 3 ? V.midLit : V.mid, K_WORLD);
            }
            for (int dx = -9; dx <= 9; dx++)
                if ((dx + s.step(8.f)) % 3) cv.set(cx + dx, s.hz + 7, 8, K_WORLD);
        }
        // near rocks with grass (mid)
        if (s.mid > 0.01f) {
            auto nr = [&](int wx) {
                const bool left = wx < s.W / 2;
                const float u = left ? 1.f - wx / 70.f : 1.f - (s.W - wx) / 76.f;
                return bot - iround(std::max(0.f, u) * 26 + 2 * std::sin(wx * 0.3f)) + nearSlide - b1;
            };
            for (int x = 0; x < s.W; x++) {
                const int t0 = nr(x + c.off(1.f));
                for (int y = std::max(0, t0); y < bot; y++) {
                    const int d = y - t0;
                    cv.set(x, y, d == 0 ? 14 : d < 2 ? 13 : V.nearC, K_WORLD);
                }
            }
        }
        break;
    }
    case TH_CHERRY: {
        // the sacred mountain on the left: a single cone with a snow cap
        {
            const int px = iround(s.W * 0.13f) + c.off(0.1f), pt = s.hz - 64;
            for (int x = 0; x < s.W; x++) {
                const int d = std::abs(x - px);
                int top = pt + iround(d * 0.62f + d * d * 0.004f);
                if (d < 4) top = pt + (d > 1 ? 1 : 0);
                for (int y = std::max(0, top); y < s.hz; y++) {
                    const int depth = y - pt;
                    const bool lit = (x - px) * c.sunSide < -(y - top) * 0.3f + 1;
                    uint8_t col = lit ? V.farLit : V.far;
                    const int cap = 16 + (int)(hash01(x / 2, 1, sd) * 6) - (d % 5 == 0 ? 3 : 0);
                    if (depth < cap) col = lit ? 6 : 8;
                    if (y > s.hz - 5 && dith((y - (s.hz - 5)) / 5.f * 0.5f, x, y)) col = V.sky[4];
                    cv.set(x, y, col, K_WORLD);
                }
            }
        }
        // rolling green hills, a pagoda on the right
        drawBand(c, 0.35f, [&](int wx) { return s.hz - 8 - iround(5 * std::sin(wx * 0.025f + 1) + 3 * std::sin(wx * 0.07f)); },
                 s.hz, V.mid, V.midLit, 2);
        {
            const int px = iround(s.W * 0.86f) + c.off(0.35f), pb = s.hz - 10 - iround(5 * std::sin((px) * 0.025f + 1));
            for (int tier = 0; tier < 3; tier++) {
                const int ty = pb - tier * 7;
                const int w = 7 - tier * 2;
                cv.rect(px - w + 2, ty - 4, (w - 2) * 2 + 1, 4, 14, K_WORLD);
                cv.rect(px - w - 2, ty - 5, (w + 2) * 2 + 1, 1, V.ink, K_WORLD);
                cv.set(px - w - 3, ty - 6, V.ink, K_WORLD);
                cv.set(px + w + 3, ty - 6, V.ink, K_WORLD);
                cv.rect(px - w + 2, ty - 3, 1, 3, V.ink, K_WORLD);
            }
            cv.vline(px, pb - 26, pb - 20, V.ink, K_WORLD);
        }
        drawWater(c, P, s.hz, bot, sunX, V.sky[1], 6);
        // grassy banks
        auto bank = [&](int wx) {
            const bool left = wx < s.W / 2;
            const float u = left ? 1.f - wx / 92.f : 1.f - (s.W - wx) / 96.f;
            return bot - iround(std::max(0.f, u) * 30 + 2 * std::sin(wx * 0.15f));
        };
        drawBand(c, 0.6f, bank, bot, V.ground, V.groundLit, 2);
        // cherry trees: small far ones; big near ones slide in at mid
        for (int i = 0; i < 3; i++) {
            const int x = 18 + i * 22 + c.off(0.6f), x2 = s.W - 20 - i * 24 + c.off(0.6f);
            drawRoundTree(c, x, bank(x) + 1, 14 + i * 2, 1, 4, 5, 3, 10 + i);
            drawRoundTree(c, x2, bank(x2) + 1, 13 + i * 3, 1, 4, 5, 3, 20 + i);
        }
        if (s.mid > 0.01f) {
            drawRoundTree(c, 14 + c.off(1.f), bot + 4 + nearSlide - b1, 52, 1, 4, 5, 3, 31, sway);
            drawRoundTree(c, s.W - 16 + c.off(1.f), bot + 4 + nearSlide, 56, 1, 4, 5, 3, 32, sway);
        }
        break;
    }
    case TH_VOLCANO: {
        drawPeaks(c, 0.12f, s.hz, 46, 14, 34, 0.6f, 1.1f, 3.f, V.far, V.farLit, 0, 0, 0, 5, V.sky[4], s.hz, sd + 1);
        // the volcano on the right
        const int vx = s.W - 56 + c.off(0.3f), vt = s.hz - 66;
        const int st = s.step(10.f);
        {
            for (int x = 0; x < s.W; x++) {
                const int d = std::abs(x - vx);
                int top = vt + iround(std::max(0, d - 6) * 1.05f + std::max(0, d - 30) * 0.4f) + (d < 4 ? 2 : 0);
                top += (int)((hash01(x / 2, 4, sd) - 0.5f) * 2);
                for (int y = std::max(0, top); y < s.hz + 2; y++) {
                    const bool lit = (x - vx) * c.sunSide > -1 ? false : true;
                    uint8_t col = lit ? V.midLit : V.mid;
                    if (y - top < 2 && d < 8) col = 5; // crater rim glow
                    cv.set(x, y, col, K_WORLD);
                }
            }
            // lava channels flowing down (stepped flow, brighter on kicks)
            for (int ch = 0; ch < 3; ch++) {
                float x = (float)vx + (ch - 1) * 2;
                const float drift = (ch - 1) * 0.55f + (hash01(ch, 7, sd) - 0.5f) * 0.3f;
                for (int y = vt + 3; y < s.hz + 2; y++) {
                    x += drift + std::sin(y * 0.3f + ch) * 0.35f;
                    const int phase = pmod(y - st, 6);
                    uint8_t col = phase < 2 ? 7 : phase < 4 ? 6 : 5;
                    if (s.kick > 0.6f && phase == 3) col = 7;
                    cv.set((int)x, y, col, K_WORLD);
                    if ((y & 3) == 0) cv.set((int)x + 1, y, 5, K_WORLD);
                }
            }
            // the glow over the crater, and smoke drifting away
            cv.glow(vx, vt - 2, 2.f, 14.f + 4.f * s.kick, 0.5f + 0.3f * s.mid, 5, K_SKY, true, K_SKY);
            for (int i = 0; i < 6 + (int)(s.mid * 4); i++) {
                const float age = std::fmod(s.t * 0.25f + i * 0.17f, 1.f);
                const int px = vx - iround(age * 60) + iround(std::sin(i * 1.7f + s.t * 0.3f) * 3);
                const int py = vt - 6 - iround(age * 34);
                const int r = 3 + iround(age * 6);
                for (int yy = -r; yy <= r; yy++)
                    for (int xx = -r; xx <= r; xx++)
                        if (xx * xx + yy * yy <= r * r && cv.kind(px + xx, py + yy) == K_SKY &&
                            dith(0.75f - age * 0.6f, px + xx, py + yy))
                            cv.set(px + xx, py + yy, yy < -r / 2 ? V.cloudLit : V.cloud, K_SKY);
            }
            for (const Bomb& b : bombs_) {
                const int x = iround(b.x), y = iround(b.y);
                cv.set(x, y, 8, K_WORLD);
                cv.set(x - iround(b.vx * 0.04f), y - iround(b.vy * 0.04f), 7, K_WORLD);
                cv.set(x - iround(b.vx * 0.08f), y - iround(b.vy * 0.08f), 6, K_WORLD);
            }
            if (s.peak > 0.3f) { // the fountain
                for (int i = 0; i < 12; i++) {
                    const int fx = vx + iround((hash01(i, st, sd) - 0.5f) * 8);
                    const int fy = vt - (int)(hash01(i, st + 1, sd) * 14 * s.peak);
                    cv.set(fx, fy, i & 1 ? 7 : 6, K_SKY);
                }
            }
        }
        // basalt plain with glowing cracks that pulse on kicks
        for (int y = s.hz; y < bot; y++)
            for (int x = 0; x < s.W; x++) {
                if (y < s.hz + 2 && cv.kind(x, y) != K_SKY) continue; // the cone's foot stays
                uint8_t col = V.ground;
                if (pmod(y - s.hz + (int)(hash01(x / 7, 3, sd) * 2), 5) == 0 && hash01(x / 2, y, sd) < 0.5f) col = V.groundLit;
                const float cr = fbm1((x + c.off(0.7f)) * 0.08f + y * 0.9f, sd + 5, 2);
                if (std::fabs(cr - 0.5f) < 0.012f) col = s.kick > 0.5f ? 6 : 5;
                cv.set(x, y, col, K_WORLD);
            }
        if (s.mid > 0.01f) {
            for (int side = 0; side < 2; side++) {
                for (int x = 0; x < 80; x++) {
                    const int px = side ? s.W - 1 - x : x;
                    const int h = iround((1.f - x / 80.f) * 34 + std::sin(x * 0.7f) * 3 + (hash01(x / 3, side, sd) - 0.5f) * 6);
                    for (int y = bot - h + nearSlide - (side ? 0 : b1); y < bot; y++) {
                        const int d = y - (bot - h + nearSlide);
                        cv.set(px, y, d < 2 && ((x / 3) & 1) ? V.nearLit : V.nearC, K_WORLD);
                    }
                }
            }
        }
        break;
    }
    case TH_JUNGLE: {
        // karst towers in the mist
        for (int layer = 0; layer < 2; layer++) {
            const float depth = layer ? 0.25f : 0.1f;
            const int o = c.off(depth);
            const uint32_t ls = sd + 11 + layer;
            for (int x = 0; x < s.W; x++) {
                const int wx = x + o;
                const int cell = (int)std::floor(wx / 22.f);
                int top = s.hz;
                bool litX = false;
                for (int k = cell - 1; k <= cell + 1; k++) {
                    const float cx = k * 22.f + hash01(k, 1, ls) * 12;
                    const float hw = 5 + hash01(k, 2, ls) * 6;
                    const float h = (layer ? 22 : 36) + hash01(k, 3, ls) * (layer ? 26 : 44);
                    const float d = std::fabs(wx - cx);
                    if (d > hw) continue;
                    const float round = std::sqrt(1.f - (d / hw) * (d / hw));
                    const int t = s.hz - iround(h * (0.75f + 0.25f * round));
                    if (t < top) { top = t; litX = (wx - cx) * c.sunSide < 0; }
                }
                for (int y = std::max(0, top); y < s.hz; y++) {
                    uint8_t col = layer ? (litX ? V.midLit : V.mid) : (litX ? V.farLit : V.far);
                    const float mist = (float)(y - (s.hz - 22)) / 22.f;
                    if (mist > 0 && dith(std::floor(mist * 4) / 4 * (layer ? 0.5f : 0.8f), x, y)) col = layer ? V.far : V.sky[3];
                    cv.set(x, y, col, K_WORLD);
                }
            }
        }
        // jungle canopy and the temple ruins on the right
        drawBand(c, 0.4f, [&](int wx) {
            const int cell = (int)std::floor(wx / 9.f);
            int best = 0;
            for (int k = cell - 2; k <= cell + 2; k++) {
                const float cx = k * 9.f + hash01(k, 1, sd + 4) * 6, r = 5 + hash01(k, 2, sd + 4) * 4;
                const float d = std::fabs(wx - cx);
                if (d < r) best = std::max(best, iround(std::sqrt(r * r - d * d) + hash01(k, 3, sd + 4) * 6));
            }
            return s.hz + 4 - best;
        }, s.hz + 6, V.nearC, V.mid, 2);
        {
            const int tx = iround(s.W * 0.86f) + c.off(0.4f), tb = s.hz - 4;
            for (int step = 0; step < 5; step++) {
                const int w = 16 - step * 3, y = tb - step * 5;
                cv.rect(tx - w, y - 5, w * 2 + 1, 5, 8, K_WORLD);
                cv.hline(tx - w, tx + w, y - 5, 6, K_WORLD);
                cv.rect(c.sunSide > 0 ? tx - w : tx + w - 1, y - 4, 2, 4, 9, K_WORLD);
            }
            cv.rect(tx - 1, tb - 23, 3, 4, V.ink, K_WORLD);
            for (int i = 0; i < 6; i++) { // vines
                const int vx = tx - 12 + (int)(hash01(i, 8, sd) * 24);
                const int len = 3 + (int)(hash01(i, 9, sd) * 6);
                for (int k = 0; k < len; k++) cv.set(vx + (k & 1), tb - 20 + (int)(hash01(i, 10, sd) * 12) + k, 12, K_WORLD);
            }
        }
        drawWater(c, P, s.hz + 6, bot, sunX, V.sky[2], 6);
        // the waterfall on the left: a cliff, falling water in stepped stripes, mist at its foot
        {
            const int o = c.off(0.55f);
            const int cx0 = -10 + o, cx1 = 58 + o, top = s.hz - 52;
            for (int x = 0; x < s.W; x++) {
                const int wx = x;
                if (wx < cx0 || wx > cx1) continue;
                const int t0 = top + iround(4 * std::sin(wx * 0.3f)) + (wx > cx1 - 8 ? (wx - (cx1 - 8)) * 3 : 0);
                for (int y = std::max(0, t0); y < s.hz + 10; y++) {
                    uint8_t col = (pmod(y + (int)(hash01(wx / 5, 1, sd) * 3), 7) == 0 && hash01(wx / 3, y, sd) < 0.75f) ? V.nearC : V.mid;
                    if (y - t0 < 2) col = 12;
                    else if (y - t0 < 4 && dith(0.5f, x, y)) col = V.midLit;
                    cv.set(x, y, col, K_WORLD);
                }
            }
            const int fx = 34 + o, st = s.step(12.f);
            for (int y = top + 2; y < s.hz + 8; y++)
                for (int dx = 0; dx < 7; dx++) {
                    const int ph = pmod(y - st * 2 + dx * 3, 7);
                    cv.set(fx + dx, y, ph < 2 ? 6 : ph < 5 ? 11 : 10, K_WORLD);
                }
            for (int i = 0; i < 40; i++) {
                const int mx = fx + 3 + iround((hash01(i, st / 2, sd) - 0.5f) * 26);
                const int my = s.hz + 6 + iround((hash01(i, st / 2 + 1, sd) - 0.7f) * 10);
                if (skyOrWorld(cv, mx, my)) cv.set(mx, my, 6, K_WORLD);
            }
            // a rainbow in the mist (peak)
            if (s.peak > 0.05f) {
                const uint8_t rc[6] = {13, 14, 7, 12, 10, 15};
                const int rx = fx + 3, ry = s.hz + 10;
                for (int b = 0; b < 6; b++) {
                    const float R = 46.f - b;
                    for (int a = 0; a < 200; a++) {
                        const float ang = PI + a / 199.f * PI;
                        const int x = rx + iround(std::cos(ang) * R * 1.2f), y = ry + iround(std::sin(ang) * R);
                        if (cv.kind(x, y) == K_SKY && dith(0.5f * s.peak, x, y)) cv.set(x, y, rc[b], K_SKY);
                    }
                }
            }
        }
        // fog band drifting at mid
        if (s.mid > 0.05f) {
            const int fy = s.hz - 12;
            for (int y = fy - 4; y < fy + 5; y++)
                for (int x = 0; x < s.W; x++) {
                    const float v = (0.5f + 0.5f * std::sin((x + s.t * 4) * 0.04f + y * 0.3f)) * (1.f - std::fabs(y - fy) / 5.f);
                    if (skyOrWorld(cv, x, y) && dith(std::floor(v * 3) / 3 * 0.5f * s.mid, x, y)) cv.set(x, y, V.sky[3], cv.kind(x, y));
                }
        }
        if (s.mid > 0.01f) { // big near leaves at the bottom corners
            for (int side = 0; side < 2; side++)
                for (int i = 0; i < 5; i++) {
                    const int bx = side ? s.W - 6 - i * 13 : 6 + i * 13;
                    const int by = bot + nearSlide - (i % 2) * 6 - b1 * (i & 1);
                    const int len = 18 + i * 2;
                    const float ang = (side ? -1.f : 1.f) * (0.5f + i * 0.18f);
                    for (int k = 0; k < len; k++) {
                        const float t = (float)k / len;
                        const int x = bx + iround(std::sin(ang) * k), y = by - iround(std::cos(ang) * k * 0.9f);
                        const int hw = iround(std::sin(t * PI) * 4);
                        for (int w = -hw; w <= hw; w++) {
                            const int px = x + iround(std::cos(ang) * w), py = y + iround(std::sin(ang) * w * 0.6f);
                            cv.set(px, py, w == 0 ? V.nearLit : (w * (side ? 1 : -1) > 0 ? 12 : V.nearC), K_WORLD);
                        }
                    }
                }
        }
        break;
    }
    }

    // ---- Critters behind the well.
    {
        const int fr = s.beatIndex() * 2 + (s.beatPhase > 0.5f ? 1 : 0);
        for (const Bird& b : birds_) {
            const uint8_t col = b.kind == 2 ? 8 : V.ink;
            if (b.kind == 1) { // hawk circling
                const int x = iround(b.x + std::cos(b.phase) * 26), y = iround(b.y + std::sin(b.phase) * 8);
                const int f = (fr / 2) & 3;
                drawSprite(cv, BIRD[f], 3, x - 2, y, col, K_WORLD, true);
                cv.set(x - 3, y + (f == 0 ? 0 : 1), col, K_WORLD);
                cv.set(x + 3, y + (f == 0 ? 0 : 1), col, K_WORLD);
                continue;
            }
            const int f = (fr + (int)b.phase) & 3;
            drawSprite(cv, BIRD[f], 3, iround(b.x) - 2, iround(b.y), col, K_WORLD, true);
        }
        for (const Fly& f : flies_) {
            const int x = iround(f.x), y = iround(f.y);
            if (!skyOrWorld(cv, x, y)) continue;
            const bool on = ((s.beatIndex() + (int)f.seed) % 3 == 0) ? s.beatPhase < 0.7f : ((int)(f.seed * 7 + s.t * 3) % 5 == 0);
            if (on) {
                cv.set(x, y, V.sun, K_WORLD);
                if (s.peak > 0.5f || s.night > 0.5f)
                    for (int k = 0; k < 4; k++) {
                        const int ox = k == 0 ? 1 : k == 1 ? -1 : 0, oy = k == 2 ? 1 : k == 3 ? -1 : 0;
                        if (skyOrWorld(cv, x + ox, y + oy)) cv.set(x + ox, y + oy, V.glow, K_WORLD);
                    }
            } else if (((int)f.seed & 1) == 0) {
                cv.set(x, y, V.nearLit, K_WORLD);
            }
        }
        if (fishT_ >= 0) {
            const float u = fishT_ / 0.7f;
            const int wy = (V.theme == TH_JUNGLE ? s.hz + 6 : s.hz) + 10;
            if (u < 1) {
                const int x = iround(fishX_ + u * 10), y = wy - iround(std::sin(u * PI) * 9);
                cv.set(x, y, V.text, K_WORLD);
                cv.set(x - 1, y + (u < 0.5f ? 1 : -1), V.text, K_WORLD);
                cv.set(x - 2, y + (u < 0.5f ? 1 : -1), V.textDim, K_WORLD);
            }
            const float sp = fishT_ < 0.25f ? fishT_ / 0.25f : (fishT_ > 0.7f ? (fishT_ - 0.7f) / 0.5f : -1);
            if (sp >= 0 && sp <= 1) {
                const int sx = iround(fishT_ < 0.25f ? fishX_ : fishX_ + 10);
                const int r = 1 + iround(sp * 3);
                cv.set(sx - r, wy, V.text, K_WORLD);
                cv.set(sx + r, wy, V.text, K_WORLD);
                if (sp < 0.5f) cv.set(sx, wy - 1 - iround(sp * 4), V.text, K_WORLD);
            }
        }
        // lanterns rising from the pond (cherry, peak)
        if (V.theme == TH_CHERRY && s.peak > 0.05f) {
            for (int i = 0; i < 9; i++) {
                const float sp = 3.f + hash01(i, 1, c.seed) * 3.f;
                const float age = std::fmod(s.t * sp / 120.f + hash01(i, 2, c.seed), 1.f);
                const int x = iround((hash01(i, 3, c.seed) < 0.5f ? hash01(i, 4, c.seed) * (s.bx0 - 56) : s.bx1 + 56 + hash01(i, 4, c.seed) * (s.W - s.bx1 - 60)) +
                                     std::sin(s.t * 0.7f + i) * 3);
                const int y = iround(s.H - 6 - age * (s.H + 10));
                if (!dith(s.peak * (age < 0.9f ? 1.f : (1 - age) * 10), x, y)) continue;
                const bool fl = ((s.step(8.f) + i) % 5) == 0;
                for (int yy = 0; yy < 3; yy++)
                    for (int xx = 0; xx < 2; xx++)
                        if (skyOrWorld(cv, x + xx, y + yy)) cv.set(x + xx, y + yy, yy == 0 ? 15 : (fl ? 6 : 7), K_WORLD);
            }
        }
    }
    (void)b2;
}

// ------------------------------------------------------------------ front

void World::drawFront(Canvas& cv, const WorldState& s) {
    const Variant& V = VARIANTS[v_];
    Ctx c{cv, V, s, (uint32_t)(seed_ & 0xFFFFFFFF), s.W, s.H, s.hz, V.sunX < 0.5f ? -1 : 1};
    const int slide = slideOf(s.peak, 40);
    const int sway = (s.peak > 0.5f && (s.beatIndex() & 1)) ? 1 : 0;
    const int hop = s.bounce;
    const uint32_t sd = c.seed + 500;

    // ---- Foreground framing (peak): silhouettes sliding in at the corners.
    if (s.peak > 0.01f) {
        switch (V.theme) {
        case TH_FOREST:
            drawBlades(c, 0, 70, s.H + 1 - hop, 16, V.ink, V.nearC, slide, sway, sd);
            drawBlades(c, s.W - 72, s.W, s.H + 1 - hop, 16, V.ink, V.nearC, slide, sway, sd + 1);
            drawBranch(c, true, slide, sway, V.ink, V.nearC, V.nearLit, V.ink, 70, true, sd);
            break;
        case TH_CANYON:
            drawBlades(c, 0, 50, s.H + 1 - hop, 8, V.nearC, V.ground, slide, sway, sd);
            drawBlades(c, s.W - 56, s.W, s.H + 1 - hop, 9, V.nearC, V.ground, slide, sway, sd + 1);
            break;
        case TH_SNOW:
            drawBranch(c, false, slide, sway, V.ink, 8, 5, V.ink, 80, true, sd);
            drawBlades(c, 0, 48, s.H + 1 - hop, 7, 4, 5, slide, sway, sd);
            break;
        case TH_CITY: {
            // power lines across the top corners, with birds sitting on them
            for (int w = 0; w < 2; w++) {
                for (int x = 0; x < s.W; x++) {
                    if (x > s.bx0 - 54 && x < s.bx1 + 54) continue;
                    const float u = x < s.W / 2 ? x / (float)(s.bx0 - 54) : (x - (s.bx1 + 54)) / (float)(s.W - s.bx1 - 54);
                    const int y = 4 + w * 5 + iround(std::sin(u * PI) * 7) - slide + (w ? sway : 0);
                    if (skyOrWorld(cv, x, y)) cv.set(x, y, V.ink, K_WORLD);
                }
            }
            break;
        }
        case TH_COAST:
            drawBlades(c, 0, 66, s.H + 1 - hop, 14, V.ink, 13, slide, sway, sd);
            drawBlades(c, s.W - 66, s.W, s.H + 1 - hop, 12, V.ink, 13, slide, sway, sd + 1);
            break;
        case TH_CHERRY:
            drawBranch(c, true, slide, sway, 1, 4, 5, 3, 90, false, sd);
            drawBranch(c, false, slide + 6, sway, 1, 4, 5, 3, 60, false, sd + 3);
            break;
        case TH_VOLCANO:
            drawBranch(c, false, slide, sway, V.ink, V.ink, 2, V.ink, 70, true, sd);
            break;
        case TH_JUNGLE:
            drawBranch(c, true, slide, sway, V.ink, V.nearC, 12, V.ink, 80, false, sd);
            drawBranch(c, false, slide, sway, V.ink, V.nearC, 12, V.ink, 64, false, sd + 2);
            break;
        }
    }
    // Birds perched on the city's power lines.
    if (V.theme == TH_CITY && s.peak > 0.5f) {
        for (int i = 0; i < 4; i++) {
            const int x = i < 2 ? 14 + i * 21 : s.W - 20 - (i - 2) * 23;
            const float u = x < s.W / 2 ? x / (float)(s.bx0 - 54) : (x - (s.bx1 + 54)) / (float)(s.W - s.bx1 - 54);
            const int y = 4 + iround(std::sin(u * PI) * 7) - slide - 2 - ((s.beatIndex() + i) % 4 == 0 ? 1 : 0);
            cv.rect(x, y, 2, 2, V.ink, K_WORLD);
            cv.set(x + (i & 1 ? 2 : -1), y, V.ink, K_WORLD);
        }
    }

    // ---- Weather.
    for (const Drop& d : drops_) {
        const int x = iround(d.x), y = iround(d.y);
        auto put = [&](int px, int py, uint8_t col) {
            if (skyOrWorld(cv, px, py)) cv.set(px, py, col, K_WORLD);
        };
        switch (d.kind) {
        case W_RAIN: // a slanted streak, lighter at its head
            put(x, y, V.wx2);
            put(x, y - 1, V.wx1);
            put(x - 1, y - 2, V.wx1);
            put(x - 1, y - 3, V.wx1);
            if (((int)d.seed & 3) == 0) put(x - 1, y - 4, V.wx1);
            break;
        case W_SAND:
            for (int k = 0; k < 5; k++) put(x - k, y, k < 2 ? V.wx2 : V.wx1);
            break;
        case W_SNOW: put(x, y, V.wx1); break;
        case 100 + W_SNOW:
            put(x, y, V.wx1);
            put(x + 1, y, V.wx1);
            put(x, y + 1, V.wx1);
            put(x + 1, y + 1, V.wx2);
            break;
        case W_PETALS: {
            const bool flip = ((int)(d.age * 8 + d.seed) & 1) != 0;
            put(x, y, V.wx1);
            put(x + (flip ? 1 : 0), y + (flip ? 0 : 1), V.wx2);
            break;
        }
        case W_LEAVES: {
            const int f = (int)(d.age * 8 + d.seed) & 3;
            const uint8_t col = ((int)d.seed & 1) ? V.wx1 : V.wx2;
            put(x, y, col);
            put(x + (f == 0 ? 1 : f == 2 ? -1 : 0), y + (f == 1 ? 1 : f == 3 ? -1 : 0), col);
            break;
        }
        case W_ASH: put(x, y, V.wx1); break;
        case 200: // ember
            put(x, y, ((int)(d.age * 10 + d.seed) & 3) ? V.wx2 : 7);
            break;
        }
    }
    for (const Drop& sp : splashes_) {
        const int x = iround(sp.x), y = iround(sp.y);
        auto put = [&](int px, int py) {
            if (skyOrWorld(cv, px, py)) cv.set(px, py, V.wx2, K_WORLD);
        };
        if (sp.age < 0.06f) { put(x, y - 1); }
        else { put(x - 1, y - 1); put(x + 1, y - 1); }
    }
    // Sandstorm haze (canyon peak): a rolling dithered band.
    if (V.weather == W_SAND && s.peak > 0.05f) {
        for (int y = s.hz - 30; y < s.H; y++)
            for (int x = 0; x < s.W; x++) {
                const float v = 0.5f + 0.5f * std::sin((x - s.t * 70) * 0.03f + y * 0.11f);
                if (skyOrWorld(cv, x, y) && v > 0.8f && dith(0.25f * s.peak, x, y)) cv.set(x, y, V.wx1, K_WORLD);
            }
    }
}

} // namespace pixu
