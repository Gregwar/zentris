// Special animations: the slow-motion bonus (one animation per activation, from a pool, lasting the whole
// bonus) and the Tetris celebrations (short one-shots, from another pool), plus the bonus gauge.
// Everything is drawn with the burst particles (bursts_ for physical ones, glints_ for procedural ones)
// in the theme's colors, kept outside the board or behind its backplate so the stack stays readable.
#include "renderer.hpp"

#include <cmath>
#include <cstdio>

namespace {

constexpr float HALF_W = 5.15f, HALF_H = 10.15f; // board frame half extents (world units)
constexpr float GAUGE_X = -6.15f;

float paleW(const Theme& t) { return smoothstepf(0.4f, 1.f, t.pale); }

// Stable per-particle random numbers.
float hash01(uint64_t seed, int i, int k) {
    return (splitmix64(seed ^ ((uint64_t)i * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)k << 48)) >> 40) * (1.f / 16777216.f);
}

// Rounded rectangle around the board (a superellipse), angle in radians, counterclockwise from +x.
vec2 aroundBoard(float ang, float rx, float ry) {
    float c = std::cos(ang), s = std::sin(ang);
    float r = 1.f / std::pow(std::pow(std::fabs(c), 4.f) + std::pow(std::fabs(s), 4.f), 0.25f);
    return {rx * r * c, ry * r * s};
}

} // namespace

void Renderer::startSlowFx() {
    int k;
    if (forceSlow_ >= 0) k = forceSlow_ % SLOW_ANIMS;
    else do k = rng_.irange(0, SLOW_ANIMS - 1); while (k == lastSlow_);
    slowAnim_ = lastSlow_ = k;
    slowOn_ = true;
    slowT_ = 0;
    slowSeed_ = rng_.next();
    // A soft "time bubble" leaving the board.
    spawnRing(vec3(0, 0, 0.4f), 5.f, lerp(cur_.accent, cur_.partB, 0.5f), 160, 6.f, 1.8f, 0.13f);
    std::printf("[fx] slow motion: animation %d\n", k);
}

void Renderer::startTetrisFx(float rowY, bool backToBack) {
    for (int n = 0; n < (backToBack ? 2 : 1); n++) {
        int k;
        if (forceTetris_ >= 0 && n == 0) k = forceTetris_ % TETRIS_ANIMS;
        else do k = rng_.irange(0, TETRIS_ANIMS - 1); while (k == lastTetris_);
        lastTetris_ = k;
        Anim a;
        a.kind = k;
        a.t = 0;
        static const float DUR[TETRIS_ANIMS] = {1.6f, 1.8f, 1.4f, 1.2f, 2.4f, 1.6f, 1.5f};
        a.dur = DUR[k];
        a.origin = vec3(0, rowY, 0.4f);
        const int p = rng_.irange(0, 6);
        a.colA = lerp(cur_.accent, cur_.piece[p], 0.3f);
        a.colB = lerp(cur_.partB, cur_.piece[(p + 3) % 7], 0.3f);
        a.seed = rng_.next();
        a.emitted = 0;
        tetrisAnims_.push_back(a);
        std::printf("[fx] tetris: animation %d\n", k);
    }
    swell_ = std::max(swell_, backToBack ? 0.55f : 0.4f);
}

void Renderer::updateSpecialFx(const Game& game, const MusicState& music, float dt) {
    slow_ = approach(slow_, slowOn_ ? 1.f : 0.f, slowOn_ ? 2.5f : 1.5f, dt);
    const float wdt = dt * lerpf(1.f, 0.3f, slow_);
    if (slowOn_ || slow_ > 0.01f) {
        slowT_ += dt;
        slowGlints(slowT_, smoothstepf(0.f, 1.f, slow_), game, music);
    }
    readyFlash_ = approach(readyFlash_, 0.f, 0.8f, dt);
    gaugeShown_ = game.bonusActive() ? game.bonusGauge() : approach(gaugeShown_, game.bonusGauge(), 3.f, dt);
    for (size_t i = 0; i < tetrisAnims_.size();) {
        Anim& a = tetrisAnims_[i];
        a.t += wdt;
        tetrisStep(a, wdt);
        if (a.t > a.dur) { tetrisAnims_[i] = tetrisAnims_.back(); tetrisAnims_.pop_back(); continue; }
        i++;
    }
}

// The slow-motion animation for this frame; P fades it in and out.
void Renderer::slowGlints(float t, float P, const Game& game, const MusicState&) {
    const vec3 cA = cur_.accent, cB = cur_.partB, cC = cur_.partA;
    const uint64_t S = slowSeed_;
    auto glint = [&](vec3 p, vec3 c, float size, float a) { glints_.push_back({p, c, size * 3.5f, a * P}); };
    switch (slowAnim_) {
    case 0: { // HALO: dots around the board, a slow light sweeping around them like a second hand.
        const int N = 96;
        const float head = PI * 0.5f - t * 0.8f;
        for (int i = 0; i < N; i++) {
            float ang = TAU * i / N;
            float a = std::pow(0.5f + 0.5f * std::cos(ang - head), 10.f);
            vec2 q = aroundBoard(ang, 11.5f, 11.2f); // beyond the side panels
            glint(vec3(q.x, q.y, -0.8f), lerp(cC, cA, a), 0.07f + 0.09f * a, 0.25f + 0.75f * a);
        }
        break;
    }
    case 1: { // HOURGLASS: thin trickles of sand falling on both sides, beyond the side panels.
        const int STREAMS = 4, N = 90;
        for (int k = 0; k < STREAMS; k++) {
            float x0 = (k % 2 ? 1.f : -1.f) * (k < 2 ? 11.f : 14.5f) + 0.6f * (hash01(S, k, 7) - 0.5f);
            float z = -1.5f - 1.5f * hash01(S, k, 8);
            vec3 c = k < 2 ? cA : cB;
            glint(vec3(x0, 11.6f, z), c, 0.09f, 0.35f); // the neck the sand runs from
            for (int i = 0; i < N; i++) {
                const int id = k * N + i;
                float v = 2.f + 0.6f * hash01(S, id, 1);
                float fall = std::fmod(t * v + hash01(S, id, 2) * 24.f, 24.f);
                float y = 11.5f - fall;
                float spread = 0.05f + 0.025f * fall; // the trickle widens as it falls
                float x = x0 + spread * (hash01(S, id, 0) - 0.5f) * 2.f;
                float edge = smoothstepf(-12.f, -9.5f, y);
                glint(vec3(x, y, z), lerp(c, cC, 0.3f * hash01(S, id, 5)), 0.03f + 0.02f * hash01(S, id, 6), 0.75f * edge);
            }
        }
        break;
    }
    case 2: { // RIPPLES: rounded rings leaving the board outline, slowly, like waves of slowed time.
        const float period = 1.7f, life = 9.f;
        const int N = 140;
        for (int k = 0; k < 6; k++) {
            float age = std::fmod(t, period) + k * period;
            if (age > t || age > life) continue;
            float r = 1.3f * age, a = 0.9f * (1.f - age / life) * (1.f - age / life) * smoothstepf(0.f, 0.6f, age);
            for (int i = 0; i < N; i++) {
                vec2 q = aroundBoard(TAU * i / N, 6.9f + r, 11.3f + r);
                glint(vec3(q.x, q.y, -1.f), lerp(cA, cB, age / life), 0.07f, a);
            }
        }
        break;
    }
    case 3: { // ORBITS: three lights circling the board far behind it, with trails.
        for (int k = 0; k < 3; k++) {
            float w = 0.45f + 0.12f * k, ph = TAU * hash01(S, k, 0);
            float y0 = (k - 1) * 6.5f;
            for (int j = 0; j < 36; j++) {
                float th = (t - j * 0.035f) * w * (k == 1 ? -1.f : 1.f) + ph;
                vec3 p(13.f * std::cos(th), y0 + 1.5f * std::sin(th * 2.f), -5.5f + 4.f * std::sin(th));
                float a = (1.f - j / 36.f);
                glint(p, k == 1 ? cB : lerp(cA, cC, 0.3f * k), j == 0 ? 0.15f : 0.06f * a + 0.015f, (j == 0 ? 0.9f : 0.6f) * a * a);
            }
        }
        break;
    }
    case 4: { // STASIS: motes hanging still around the board, barely drifting, twinkling slowly.
        const int N = 320;
        for (int i = 0; i < N; i++) {
            float side = i % 2 ? 1.f : -1.f;
            vec3 p(side * (6.6f + 11.f * hash01(S, i, 0)), -11.5f + 23.f * hash01(S, i, 1), -1.f - 5.f * hash01(S, i, 2));
            p.x += 0.15f * std::sin(t * 0.3f + i);
            p.y += 0.15f * std::cos(t * 0.25f + i * 1.3f);
            float tw = 0.5f + 0.5f * std::sin(t * 0.8f + TAU * hash01(S, i, 3));
            glint(p, lerp(cC, cB, hash01(S, i, 4)), 0.05f + 0.06f * hash01(S, i, 5), 0.2f + 0.4f * tw);
        }
        break;
    }
    case 5: { // RIBBONS: two slow double helices across the screen, faded behind the board.
        const int N = 170;
        for (int r = 0; r < 2; r++)
            for (int s = 0; s < 2; s++)
                for (int i = 0; i < N; i++) {
                    float x = -19.f + 38.f * i / (N - 1);
                    float ph = x * 0.22f + t * 0.5f * (r ? -1.f : 1.f) + r * 2.f;
                    float y = (r ? 6.8f : -6.8f) + 1.5f * std::sin(ph) + (s ? 0.5f : -0.5f) * std::cos(ph * 1.7f + t * 0.4f);
                    float hide = 1.f - 0.85f * (1.f - smoothstepf(5.f, 6.8f, std::fabs(x)));
                    glint(vec3(x, y, -2.f), lerp(s ? cA : cC, cB, (x + 19.f) / 38.f), 0.06f, 0.5f * hide);
                }
        break;
    }
    default: { // CLOCK: a dial around the board; the lit ticks are the time left.
        const int N = 60;
        const float left = game.bonusActive() ? game.bonusLeft() / Game::BONUS_SECONDS : 0.f;
        for (int i = 0; i < N; i++) {
            float ang = PI * 0.5f - TAU * i / N;
            vec2 q = aroundBoard(ang, 11.5f, 11.2f);
            bool lit = (i + 0.5f) / N < left;
            bool head = lit && (i + 1.5f) / N >= left;
            float big = i % 5 == 0 ? 1.5f : 1.f;
            float a = head ? 0.75f + 0.25f * std::sin(t * 3.f) : lit ? 0.9f : 0.2f;
            glint(vec3(q.x, q.y, -0.8f), lerp(cA, cB, (float)i / N), 0.1f * big * (head ? 1.5f : 1.f), a);
        }
        break;
    }
    }
}

// One step of a Tetris celebration.
void Renderer::tetrisStep(Anim& a, float dt) {
    const vec3 o = a.origin;
    auto emit = [&](float rate, float until) { // number of particles to emit this step
        if (a.t > until) return 0;
        a.emitted += rate * dt;
        int n = (int)a.emitted;
        a.emitted -= n;
        return n;
    };
    auto push = [&](vec3 p, vec3 v, vec3 c, float life, float size) {
        if (bursts_.size() >= 9000) return;
        Burst b;
        b.pos = p;
        b.vel = v;
        b.color = c;
        b.maxLife = b.life = life * rng_.range(0.75f, 1.f);
        b.size = size * rng_.range(0.7f, 1.2f);
        bursts_.push_back(b);
    };
    switch (a.kind) {
    case 0: { // PILLARS: light streaming up both edges of the board from the cleared rows.
        int n = emit(360.f, 1.1f);
        for (int i = 0; i < n; i++) {
            float side = rng_.chance(0.5f) ? 1.f : -1.f;
            push(vec3(side * (HALF_W + 0.3f) + rng_.range(-0.12f, 0.12f), o.y + rng_.range(-1.f, 1.f), 0.3f),
                 vec3(rng_.range(-0.2f, 0.2f), rng_.range(7.f, 11.f), 0), i % 2 ? a.colA : a.colB, 1.4f, 0.18f);
        }
        break;
    }
    case 1: { // BLOOMS: a few soft blooms around the board, one after the other.
        static const float AT[6] = {0.f, 0.2f, 0.4f, 0.65f, 0.9f, 1.15f};
        while (a.emitted < 6 && a.t >= AT[(int)a.emitted]) {
            int k = (int)a.emitted;
            vec3 p = k == 5 ? vec3(0, HALF_H + 1.5f, 0.3f)
                            : vec3((k % 2 ? 1.f : -1.f) * rng_.range(10.f, 14.f), rng_.range(-7.f, 9.f), -0.5f);
            spawnBurst(p, k % 2 ? a.colA : a.colB, 130, 4.5f, 2.f, 0.17f);
            a.emitted += 1;
        }
        break;
    }
    case 2: { // SPIRAL: two arms wound out from the cleared rows.
        int n = emit(420.f, 0.9f);
        for (int i = 0; i < n; i++) {
            float arm = i % 2 ? PI : 0.f, ang = arm + a.t * 7.f + rng_.range(-0.1f, 0.1f);
            vec3 d(std::cos(ang), std::sin(ang) * 0.8f, 0), tg(-d.y, d.x, 0);
            push(o + d * 0.5f, d * rng_.range(6.f, 8.f) + tg * 2.f, i % 2 ? a.colA : a.colB, 1.6f, 0.18f);
        }
        break;
    }
    case 3: { // WAVES: three rings in quick succession.
        while (a.emitted < 3 && a.t >= 0.16f * a.emitted) {
            int k = (int)a.emitted;
            spawnRing(o, 1.f, k == 1 ? a.colB : a.colA, 160, 6.f + 3.f * k, 1.6f, 0.17f);
            a.emitted += 1;
        }
        break;
    }
    case 4: { // STARFALL: gentle colored stars falling across the whole screen, behind the board.
        int n = emit(260.f, 1.6f);
        for (int i = 0; i < n; i++)
            push(vec3(rng_.range(-19.f, 19.f), rng_.range(11.f, 13.f), rng_.range(-3.f, -0.8f)),
                 vec3(rng_.range(-0.3f, 0.3f), rng_.range(-4.5f, -2.5f), 0), cur_.piece[rng_.irange(0, 6)], 3.f, 0.16f);
        break;
    }
    case 5: { // TRACE: two lights run up the frame from the cleared rows and meet at the top in a bloom.
        const float T = 0.9f, len = (HALF_H - o.y) + HALF_W;
        if (a.t < T) {
            float s = smoothstepf(0.f, 1.f, a.t / T) * len;
            for (int side = -1; side <= 1; side += 2) {
                vec3 p = s < HALF_H - o.y ? vec3(side * HALF_W, o.y + s, 0.3f)
                                          : vec3(side * (HALF_W - (s - (HALF_H - o.y))), HALF_H, 0.3f);
                glints_.push_back({p, side < 0 ? a.colA : a.colB, 0.6f, 0.9f});
                for (int i = 0; i < 4; i++)
                    push(p, vec3(rng_.range(-0.4f, 0.4f), rng_.range(-0.4f, 0.4f), 0), side < 0 ? a.colA : a.colB, 0.8f, 0.17f);
            }
        } else if (a.emitted < 1) {
            spawnBurst(vec3(0, HALF_H, 0.4f), lerp(a.colA, a.colB, 0.5f), 180, 5.f, 2.f, 0.17f);
            a.emitted = 1;
        }
        break;
    }
    default: { // SHEET: a sheet of light lifts off the cleared rows and leaves through the top.
        float e = 1.f - (1.f - std::min(1.f, a.t / 1.f)) * (1.f - std::min(1.f, a.t / 1.f));
        float y = o.y + (HALF_H + 3.f - o.y) * e, fade = (1.f - a.t / a.dur);
        for (int i = 0; i < 48; i++) {
            float x = -HALF_W + 2.f * HALF_W * (i + 0.5f) / 48;
            glints_.push_back({vec3(x, y, 0.5f), lerp(a.colA, a.colB, (float)i / 47), 0.35f, 0.7f * fade * fade});
        }
        int n = emit(160.f, 0.9f);
        for (int i = 0; i < n; i++) {
            float side = rng_.chance(0.5f) ? 1.f : -1.f;
            push(vec3(side * HALF_W, y, 0.4f), vec3(side * rng_.range(2.f, 5.f), rng_.range(-0.5f, 1.5f), 0),
                 side < 0 ? a.colA : a.colB, 1.2f, 0.16f);
        }
        break;
    }
    }
}

// The bonus gauge: 12 segments (one per line) left of the board. It breathes slowly once full and drains
// continuously during the bonus.
void Renderer::addGauge(const Game& game, double time, std::vector<BlockInst>& fx) {
    const int N = Game::BONUS_LINES;
    const float gap = 0.14f, segH = (2.f * HALF_H - gap * (N - 1)) / N, w = 0.14f;
    const float pw = paleW(cur_);
    const bool ready = game.bonusReady(), active = game.bonusActive();
    const float breath = 0.5f + 0.5f * std::sin((float)time * TAU * 0.4f);
    const float level = gaugeShown_ * N;
    auto bar = [&](float y0, float h, vec3 c, float a) {
        if (h <= 0.002f || a <= 0.002f) return;
        BlockInst b;
        b.pos = vec3(GAUGE_X, y0 + h * 0.5f, 0);
        b.scale = vec3(w, h, w);
        b.color = vec4(c, a);
        b.params = vec4(2, 0, 0, 0);
        fx.push_back(b);
    };
    for (int i = 0; i < N; i++) {
        float y0 = -HALF_H + i * (segH + gap), f = std::clamp(level - i, 0.f, 1.f);
        vec3 c = lerp(cur_.partA, cur_.accent, (float)i / (N - 1));
        if (active) c = lerp(cur_.partB, cur_.accent, 0.35f);
        c = c * lerpf(1.2f + 0.4f * readyFlash_, 1.f, pw);
        bar(y0, segH, cur_.accent, lerpf(0.1f, 0.2f, pw));                    // track
        float a = ready ? 0.7f + 0.3f * breath : active ? 0.85f : 0.75f;
        bar(y0, segH * f, c, a);
    }
}
