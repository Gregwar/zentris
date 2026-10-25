#pragma once
// The host renderer: owns the active universe (universe.hpp) and, during a change of universe, the incoming one;
// renders each into its own texture and composites them with a transition (transitions.hpp). Outside changes,
// only the active universe renders, straight to the window.
#include <memory>
#include <string>
#include <vector>

#include <glad/gl.h>

#include "layout.hpp"
#include "transitions.hpp"
#include "ugl.hpp"
#include "universe.hpp"
#include "universes/registry.hpp"

class Renderer {
public:
    ~Renderer();
    bool init(int width, int height);
    void resize(int width, int height);

    // ---- Universes.
    // Switches to universe `index` (registry) at once: no transition.
    void setUniverse(int index, const SongLook& look, int level, float energy);
    // Changes to universe `index` with a transition, started on the next downbeat and lasting a few bars. A change
    // asked while another runs finishes that one first. Universe `index` may be the active one: a fresh instance
    // (e.g. with another variant) replaces it.
    struct Change {
        int transition = -1;   // transitions::Kind, -1: picked from both universes' hints
        transitions::Reason why = transitions::NEW_SCENE;
        float bars = 0;        // length in bars, 0: about 4 s at the song's tempo (1 to 4 bars)
        bool onBeat = true;    // wait for the next downbeat (or beat) to start
    };
    void changeUniverse(int index, const SongLook& look, int level, float energy, const Change& c);
    void changeUniverse(int index, const SongLook& look, int level, float energy) {
        changeUniverse(index, look, level, energy, Change{});
    }
    // A new song in the same universe: the universe performs its own change.
    void setSong(const SongLook& look, float seconds);
    int universe() const;              // the universe shown (the incoming one during a change)
    int variant() const;               // its variant
    std::string label() const;         // "VECTOR · P3 AMBER"
    bool changing() const { return state_ != IDLE; }
    float changeProgress() const { return state_ == RUNNING ? p_ : (state_ == PENDING ? 0.f : -1.f); }
    const char* transitionName() const { return transitions::name(kind_); }

    // ---- Song phase and game, forwarded to the universe(s).
    void setLevel(int level, float energy, float seconds, float impact);
    void refresh(float strength);
    void onEvent(const GameEvent& ev, const Game& game);
    void kick(float amount);
    void levelUp();
    void setBonus(bool on);
    void setDim(bool d) { dim_ = d; }
    void setPanels(std::vector<HudRect> p) { panels_ = std::move(p); }
    void forceFx(int slow, int tetris);

    void render(const Game& game, const MusicState& music, double time, float dt, bool paused,
                const std::vector<HudText>& hud, float fade);
    // Board space -> window pixels, through the universe shown (blended during a change).
    vec2 project(const vec3& p) const;
    bool screenshot(const std::string& path);
    // The frame just rendered, RGB, rows bottom to top.
    void readFrame(std::vector<unsigned char>& px, int& w, int& h);

    static vec3 gaugeCenter() { return layout::GAUGE_CENTER; }
    static constexpr float GAUGE_R = layout::GAUGE_R;

private:
    struct Slot {
        std::unique_ptr<Universe> u;
        int index = -1;
    };
    Slot make(int index, const SongLook& look, int level, float energy);
    void finishChange();
    template <class F> void each(F f) {
        if (cur_.u) f(*cur_.u);
        if (next_.u) f(*next_.u);
    }

    Slot cur_, next_;
    enum State { IDLE, PENDING, RUNNING } state_ = IDLE;
    int kind_ = 0, lastKind_ = -1;
    float p_ = 0, seconds_ = 3.f, beats_ = 8.f, pendingT_ = 0;
    double startBeat_ = 0;
    bool pendingInit_ = false;
    Change want_;
    uint64_t changes_ = 0;
    ugl::Target texA_, texB_;
    transitions::Compositor comp_;
    GLuint cellsTex_ = 0;
    int level_ = 1;
    float energy_ = 0.5f;
    bool bonus_ = false, dim_ = false;
    std::vector<HudRect> panels_;
    int w_ = 1, h_ = 1;
};
