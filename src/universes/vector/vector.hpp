#pragma once
// VECTOR: phosphor beams on a dark CRT (see vector.cpp).
#include <vector>

#include "universe.hpp"

class VectorUniverse : public Universe {
public:
    ~VectorUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void refresh(float strength) override;
    void drop(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override;
    void levelUp() override;
    void kick(float amount) override { kick_ = std::max(kick_, amount); }
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return basePhos_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

private:
    // ---- Beams.
    enum { L_WORLD = 0, L_BOARD = 1, L_SCREEN = 2 };
    struct Beam { vec4 a, b, c; }; // a: xyz + width px, b: xyz + layer, c: intensity A/B, hot A/B
    std::vector<Beam> beams_;
    void beam(const vec3& a, const vec3& b, float ia, float ib, float ha, float hb, float width, int layer);
    void beam(const vec3& a, const vec3& b, float i, float hot = 0.f, float width = 1.f, int layer = L_BOARD) {
        beam(a, b, i, i, hot, hot, width, layer);
    }
    float px_ = 1.f; // pixels per reference pixel (1080p)

    // ---- Scene parts.
    void buildWorld(const MusicState& music);
    void buildBoard(const Game& game, float dt);
    void buildFx(float dt);
    void buildHud(const std::vector<HudText>& hud);
    void glyph(const vec3& c, float half, int type, float inten, float hot, int layer, bool outline = true);
    void strokeText(const std::string& s, float x, float y, float scale, float inten, float hot);
    static float textWidth(const std::string& text, float scale);

    // Debris: a free beam segment (line clears, game over), spinning in its plane and drifting apart.
    struct Debris { vec3 p, v, half; float spin, life, maxLife, delay, inten, hot; };
    std::vector<Debris> debris_;
    void shatter(const vec3& c, float half, int type, const vec3& vel, float spread, float life, float delay, float hot);
    // Rings: expanding vector shockwaves (Tetris: circles; level up: the well's outline).
    struct Ring { vec3 c; float r0, speed, t, life, inten; int sides; float aspect; };
    std::vector<Ring> rings_;
    // A locked piece's outline flaring hot; a cleared row's beam collapsing; hard drop streaks.
    struct Flare { std::vector<vec3> seg; vec3 c; float t; };
    std::vector<Flare> flares_;
    struct RowFlash { float y, t; };
    std::vector<RowFlash> rowFlashes_;
    struct Streak { float x, y0, y1, t; };
    std::vector<Streak> streaks_;

    // ---- Targets.
    GLuint progLine_ = 0, progFill_ = 0, progDecay_ = 0, progDown_ = 0, progUp_ = 0, progComp_ = 0;
    GLuint lineVao_ = 0, cornerVbo_ = 0, beamVbo_ = 0, fillVao_ = 0, fillVbo_ = 0;
    GLuint accFbo_[2] = {}, accTex_[2] = {}, depthRb_ = 0;
    int accCur_ = 0;
    static constexpr int BLOOM_MIPS = 4;
    GLuint bloomFbo_[BLOOM_MIPS] = {}, bloomTex_[BLOOM_MIPS] = {};
    int bloomW_[BLOOM_MIPS] = {}, bloomH_[BLOOM_MIPS] = {};
    void createTargets();
    void destroyTargets();
    std::vector<float> fill_; // terrain surface triangles (depth only)
    static constexpr int NC = 41, NR = 40;
    float H_[NR][NC] = {}, IN_[NR] = {}, ZK_[NR] = {};

    mat4 view_, proj_, vp_;
    vec3 camPos_;

    // ---- Song and phase: the phosphor (from the key), the scene level.
    bool hasSong_ = false;
    uint64_t seed_ = 0;
    int level_ = 1;
    int phos_ = 0, phosPrev_ = 0;     // phosphor shown, and the one the retrace sweep is replacing
    int basePhos_ = 0;                // the song's phosphor (the bonus shows its complement)
    bool palSweep_ = false;           // the running sweep changes the phosphor
    float slowAmt_ = 0;               // slow-motion bonus presence (eased)
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f; // scene level, continuous 0 calm .. 2 peak
    float sweepT_ = 9.f, sweepDur_ = 1.f, sweepAmt_ = 0.f; // retrace bar
    float degauss_ = 0.f;
    vec3 phosColor(int p) const;
    void sweep(int want, float seconds, float amount);

    // ---- Music-driven state.
    float beats_ = 0, lastBeatPhase_ = 0; // beats counted from the start (tempo-locked scrolling)
    float scroll_ = 0;                    // terrain scroll (rows)
    float worldTime_ = 0;
    float overdrive_ = 0, kick_ = 0, flashWell_ = 0;
    float throb_ = 0;
    float camDolly_ = 0;

    // ---- Peak: the drop hit and the living peak (see drop() and render()).
    float dropT_ = 99.f, dropAmt_ = 0;     // seconds since the drop hit, and its strength
    float slam_ = 0;                       // the mountains slammed up by the drop (decays over ~1 s)
    float peakWait_ = -1.f;                // rising into a peak: seconds waiting for the drop (-1: none)
    float drive_ = 0;                      // how hard the peak reacts (peak presence x energy x tempo)
    float punch_ = 0;                      // per-beat / per-kick punch envelope (fast attack, fast decay)
    float scrollShown_ = 0;                // scroll_ with the per-beat surge
    long long lastBeat_ = -1;              // beat index seen last frame
    int bar_ = 0;                          // bars counted at peak (the Lissajous figure, the ridges change on each)
    float barT_ = 9.f;                     // seconds since the last downbeat
    float beatT_ = 9.f;                    // seconds since the last beat
    bool downLast_ = false;                // the last beat was a downbeat (harder punch)

    // ---- Game state seen by the universe.
    bool inverted_ = false;
    float dimT_ = 0;
    float overT_ = -1.f; // seconds since the top out (-1: playing)
    float gaugeShown_ = 0, readyFlash_ = 0;
    Bridge bridge_;
    Rng rng_{12345};
};
