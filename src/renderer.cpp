#include "renderer.hpp"

#include <cstdio>
#include <cstring>

#include "stb_image_write.h"

Renderer::~Renderer() {
    cur_.u.reset();
    next_.u.reset();
    texA_.destroy();
    texB_.destroy();
    if (cellsTex_) glDeleteTextures(1, &cellsTex_);
}

bool Renderer::init(int width, int height) {
    w_ = std::max(1, width);
    h_ = std::max(1, height);
    comp_.init();
    glGenTextures(1, &cellsTex_);
    glBindTexture(GL_TEXTURE_2D, cellsTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, Game::W, Game::H - Game::HIDDEN, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return glGetError() == GL_NO_ERROR;
}

void Renderer::resize(int width, int height) {
    w_ = std::max(1, width);
    h_ = std::max(1, height);
    each([&](Universe& u) { u.resize(w_, h_); });
    if (texA_.fbo) {
        texA_.create(w_, h_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true);
        texB_.create(w_, h_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true);
    }
}

// =====================================================================================================
// Universes
// =====================================================================================================

Renderer::Slot Renderer::make(int index, const SongLook& look, int level, float energy) {
    Slot s;
    s.index = std::clamp(index, 0, universes::count() - 1);
    s.u = universes::entry(s.index).make();
    if (!s.u->init(w_, h_)) std::fprintf(stderr, "[gl] warning: universe %s init reported a GL error\n", universes::entry(s.index).name);
    s.u->setSong(look, 0.f);
    s.u->setLevel(level, energy, 0.f, 0.f);
    if (bonus_) s.u->setBonus(true);
    return s;
}

void Renderer::setUniverse(int index, const SongLook& look, int level, float energy) {
    level_ = level;
    energy_ = energy;
    next_ = Slot{};
    state_ = IDLE;
    cur_ = Slot{}; // the old one's GL resources go first
    cur_ = make(index, look, level, energy);
    std::printf("[universe] %s\n", label().c_str());
}

void Renderer::finishChange() {
    if (state_ == IDLE || !next_.u) return;
    next_.u->setBridge(Bridge{});
    cur_ = std::move(next_);
    next_ = Slot{};
    state_ = IDLE;
}

void Renderer::changeUniverse(int index, const SongLook& look, int level, float energy, const Change& c) {
    if (!cur_.u) {
        setUniverse(index, look, level, energy);
        return;
    }
    if (state_ == RUNNING) finishChange(); // a change asked during another: that one ends at once
    level_ = level;
    energy_ = energy;
    next_ = Slot{};
    next_ = make(index, look, level, energy);
    want_ = c;
    changes_++;
    const TransitionHints a = cur_.u->hints(), b = next_.u->hints();
    kind_ = c.transition >= 0 ? std::min(c.transition, transitions::count() - 1)
                              : transitions::pick(a, b, c.why, look.seed ^ splitmix64(changes_), lastKind_);
    lastKind_ = kind_;
    state_ = PENDING;
    pendingInit_ = true;
    pendingT_ = 0;
    p_ = 0;
    if (!texA_.fbo) {
        texA_.create(w_, h_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true);
        texB_.create(w_, h_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true);
    }
    std::printf("[universe] %s -> %s (%s)\n", universes::label(cur_.index, cur_.u->variant()).c_str(), label().c_str(),
                transitions::name(kind_));
}

void Renderer::setSong(const SongLook& look, float seconds) {
    finishChange();
    if (cur_.u) cur_.u->setSong(look, seconds);
}

int Renderer::universe() const { return next_.u ? next_.index : cur_.index; }
int Renderer::variant() const {
    const Slot& s = next_.u ? next_ : cur_;
    return s.u ? s.u->variant() : 0;
}
std::string Renderer::label() const { return universe() < 0 ? std::string() : universes::label(universe(), variant()); }

// =====================================================================================================
// Forwarding: both universes hear everything during a change (locks and clears look right in both halves)
// =====================================================================================================

void Renderer::setLevel(int level, float energy, float seconds, float impact) {
    level_ = level;
    energy_ = energy;
    each([&](Universe& u) { u.setLevel(level, energy, seconds, impact); });
}
void Renderer::refresh(float strength) { each([&](Universe& u) { u.refresh(strength); }); }
void Renderer::onEvent(const GameEvent& ev, const Game& game) { each([&](Universe& u) { u.onEvent(ev, game); }); }
void Renderer::kick(float amount) { each([&](Universe& u) { u.kick(amount); }); }
void Renderer::levelUp() { each([&](Universe& u) { u.levelUp(); }); }
void Renderer::setBonus(bool on) {
    bonus_ = on;
    each([&](Universe& u) { u.setBonus(on); });
}
void Renderer::forceFx(int slow, int tetris) { each([&](Universe& u) { u.forceFx(slow, tetris); }); }

vec2 Renderer::project(const vec3& p) const {
    if (!cur_.u) return layout::project(p, w_, h_);
    const vec2 a = cur_.u->project(p);
    if (state_ != RUNNING || !next_.u) return a;
    const vec2 b = next_.u->project(p);
    const float k = smoothstepf(0.f, 1.f, p_);
    return {lerpf(a.x, b.x, k), lerpf(a.y, b.y, k)};
}

// =====================================================================================================
// Frame
// =====================================================================================================

void Renderer::render(const Game& game, const MusicState& music, double time, float dt, bool paused,
                      const std::vector<HudText>& hud, float fade) {
    if (!cur_.u) return;
    const Frame f{game, music, time, dt, paused, dim_, fade, hud, panels_};
    const float bpm = music.hasSong ? std::clamp(music.bpm, 40.f, 220.f) : 120.f;

    // ---- A change waits for the next downbeat (or the next beat if the bar is far), then runs for a few bars.
    if (state_ == PENDING) {
        if (pendingInit_) {
            pendingInit_ = false;
            const float barSec = 240.f / bpm;
            const float bars = want_.bars > 0 ? want_.bars : std::clamp(std::round(4.f / barSec), 1.f, 4.f);
            beats_ = bars * 4.f;
            seconds_ = beats_ * 60.f / bpm;
            startBeat_ = music.beat;
            if (music.hasSong && want_.onBeat) {
                const double nextBar = (std::floor(music.beat / 4.0 + 1e-3) + 1.0) * 4.0;
                startBeat_ = (nextBar - music.beat) * 60.0 / bpm <= 1.6 ? nextBar : std::floor(music.beat) + 1.0;
            }
        }
        if (!paused) pendingT_ += dt;
        const bool reached = !music.hasSong || !want_.onBeat || music.beat >= startBeat_ - 1e-3 ||
                             music.beat < startBeat_ - 12.0 || pendingT_ > 3.f;
        if (reached) {
            state_ = RUNNING;
            p_ = 0;
        }
    }
    if (state_ == RUNNING && !paused) p_ += dt / std::max(0.2f, seconds_);

    if (state_ != RUNNING) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w_, h_);
        cur_.u->render(f, 0);
        return;
    }

    // ---- Both universes, each in its own texture, meeting at the bridge in the middle of the change.
    const float p = saturate(p_);
    const TransitionHints ha = cur_.u->hints(), hb = next_.u->hints();
    const float depth = transitions::bridgeDepth(kind_);
    Bridge ba = transitions::bridgeFor(ha, hb, false), bb = transitions::bridgeFor(hb, ha, true);
    ba.amount = depth * smoothstepf(0.f, 0.5f, p);
    bb.amount = depth * (1.f - smoothstepf(0.5f, 1.f, p));
    cur_.u->setBridge(ba);
    next_.u->setBridge(bb);

    texA_.bind();
    cur_.u->render(f, texA_.fbo);
    texB_.bind();
    next_.u->render(f, texB_.fbo);

    // The stack, for the bridge the host draws (rows from the bottom).
    unsigned char cells[(Game::H - Game::HIDDEN) * Game::W];
    for (int y = Game::HIDDEN; y < Game::H; y++)
        for (int x = 0; x < Game::W; x++)
            cells[(Game::H - 1 - y) * Game::W + x] = game.cell(x, y).type >= 0 ? 255 : 0;
    if (game.hasPiece() && !game.over()) {
        int pc[4][2];
        game.pieceCells(game.piece(), pc);
        for (auto& c : pc)
            if (c[1] >= Game::HIDDEN && c[1] < Game::H) cells[(Game::H - 1 - c[1]) * Game::W + c[0]] = 180;
    }
    glBindTexture(GL_TEXTURE_2D, cellsTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Game::W, Game::H - Game::HIDDEN, GL_RED, GL_UNSIGNED_BYTE, cells);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w_, h_);
    transitions::Inputs in;
    in.texA = texA_.tex;
    in.texB = texB_.tex;
    in.cells = cellsTex_;
    in.width = w_;
    in.height = h_;
    in.progress = p;
    in.beats = beats_;
    in.a = ha;
    in.b = hb;
    in.bridgeA = ba;
    in.bridgeB = bb;
    in.hostBridgeA = cur_.u->bridgeable() ? 0.f : ba.amount;
    in.hostBridgeB = next_.u->bridgeable() ? 0.f : bb.amount;
    in.kick = music.kick;
    in.beatPhase = music.beatPhase;
    in.time = time;
    in.seed = changes_ * 7919u;
    comp_.draw(kind_, in);

    if (p_ >= 1.f) finishChange();
}

void Renderer::readFrame(std::vector<unsigned char>& px, int& w, int& h) {
    w = w_;
    h = h_;
    px.resize((size_t)w_ * h_ * 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w_, h_, GL_RGB, GL_UNSIGNED_BYTE, px.data());
}

bool Renderer::screenshot(const std::string& path) {
    std::vector<unsigned char> px, flipped;
    int w, h;
    readFrame(px, w, h);
    flipped.resize(px.size());
    for (int y = 0; y < h; y++) std::memcpy(&flipped[(size_t)y * w * 3], &px[(size_t)(h - 1 - y) * w * 3], w * 3);
    return stbi_write_png(path.c_str(), w, h, 3, flipped.data(), w * 3) != 0;
}
