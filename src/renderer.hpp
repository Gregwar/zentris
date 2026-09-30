#pragma once
#include <array>
#include <string>
#include <vector>

#include <GL/glew.h>

#include "analysis.hpp"
#include "game.hpp"
#include "theme.hpp"

// Per-frame audio / music state the visuals react to.
struct MusicState {
    FrameFeatures audio;
    float intensity = 0.3f;  // slow section energy
    float beatPhase = 0;     // 0..1 inside current beat
    float beatPulse = 0;     // decaying pulse on each beat
    // Slow multipliers from the song structure (intro sparse, chorus full, builds ramping...).
    float density = 1, speed = 1, glow = 1, saturation = 1;
    float energy = 0.5f;     // overall energy: tempo x section intensity x level (0..1)
    float kick = 0;          // envelope of strong bass hits (fast attack, ~0.3 s decay)
    std::array<float, NUM_BANDS> bandsFast{}; // quickly smoothed band levels (equalizer)
};

// How a scene transition spreads over the screen (every shape has a soft glowing front).
enum WipeShape {
    WIPE_RADIAL,   // from the board outward
    WIPE_RISE,     // bottom to top
    WIPE_FALL,     // top to bottom
    WIPE_LEFT,     // left to right
    WIPE_RIGHT,    // right to left
    WIPE_INWARD,   // edges to center
    WIPE_CURTAINS, // both sides to the middle
    WIPE_DISSOLVE, // organic noise blotches
    WIPE_DIAGONAL, // corner to corner
    WIPE_SPIRAL,   // sweeping around the board
    WIPE_DIAMOND,  // diamond from the center
    WIPE_COUNT
};

struct HudText {
    std::string text;
    float x, y;       // pixels (top-left origin); negative x = right aligned from the right edge
    float scale;
    float alpha;
    bool accent = false;
    bool center = false;
};

class Renderer {
public:
    bool init(int width, int height);
    void resize(int width, int height);
    // Start a transition to a new theme.
    // wipe: the new theme ripples outward from the board (used for song phases and song changes).
    // impact 0..1: a rising transition (e.g. into a drop) sweeps a bright ring out and swells the glow.
    void setTheme(const Theme& t, float seconds, bool wipe = false, float impact = 0.f, int shape = WIPE_RADIAL);
    // Latest requested theme (a queued one if a transition is running).
    const Theme& latestTheme() const { return hasPending_ ? pending_ : to_; }
    const Theme& targetTheme() const { return to_; }
    const Theme& current() const { return cur_; }

    void onEvent(const GameEvent& ev, const Game& game);
    void kick(float amount) { kick_ = std::max(kick_, amount); }
    void levelUp(); // ring of light around the board
    // Dim the scene (like pause) without freezing it, e.g. behind the game-over screen.
    void setDim(bool d) { dim_ = d; }

    void render(const Game& game, const MusicState& music, double time, float dt, bool paused,
                const std::vector<HudText>& hud, float fade);
    // World position (board space) to window pixels.
    vec2 project(const vec3& p) const;
    bool screenshot(const std::string& path);

private:
    struct Mesh { GLuint vao = 0, vbo = 0, ebo = 0; int count = 0; };
    struct BlockInst { vec3 pos, scale; vec4 color, params; };
    struct Burst { vec3 pos, vel; vec3 color; float life, maxLife, size; };
    struct Dying { vec3 pos; int type; float t, delay, dur; int effect; vec3 dir; };

    GLuint compile(const char* vs, const char* fs, const char* name);
    void buildMesh(Mesh& m, bool sharpCube, float exponent);
    void createTargets();
    void drawBackground(const MusicState& music, double time);
    void drawParticleLayer(const ParticleLayer& L, const Theme& owner, float weight, bool side, const MusicState& music);
    float wipeMix(const vec3& worldPos) const;
    void drawBlocks(const std::vector<BlockInst>& inst, bool depthWrite);
    void drawBursts();
    void drawText(const std::vector<HudText>& hud);
    void spawnRing(vec3 center, float radius, vec3 color, int n, float speed, float life, float size);
    void addEqualizer(const Theme& th, float weight, const MusicState& music, std::vector<BlockInst>& fx);
    void collectBoard(const Game& game, const MusicState& music, double time, std::vector<BlockInst>& solid,
                      std::vector<BlockInst>& ghost, std::vector<BlockInst>& fx);
    void spawnBurst(vec3 pos, vec3 color, int n, float speed, float life, float size);
    vec3 cellPos(float x, float y) const; // board cell -> world

    int w_ = 1, h_ = 1;
    GLuint progBg_ = 0, progPart_ = 0, progBurst_ = 0, progBlock_ = 0, progDown_ = 0, progUp_ = 0, progComp_ = 0,
           progText_ = 0;
    GLuint emptyVao_ = 0;
    GLuint quadVbo_ = 0, seedVbo_ = 0, partVao_ = 0;
    GLuint burstVao_ = 0, burstVbo_ = 0;
    GLuint instVbo_ = 0;
    GLuint textVao_ = 0, textVbo_ = 0;
    Mesh cubeMesh_, blockMesh_;  // sharp cube for helpers; morphable superellipsoid for blocks
    float builtExp_ = -1;

    GLuint msFbo_ = 0, msColor_ = 0, msDepth_ = 0;
    GLuint sceneFbo_ = 0, sceneTex_ = 0;
    static constexpr int BLOOM_MIPS = 6;
    GLuint bloomFbo_[BLOOM_MIPS] = {}, bloomTex_[BLOOM_MIPS] = {};
    int bloomW_[BLOOM_MIPS] = {}, bloomH_[BLOOM_MIPS] = {};
    int samples_ = 4;

    Theme from_, to_, cur_, pending_;
    float transT_ = 1.f, transDur_ = 1.f, pendingDur_ = 1.f, mix_ = 1.f;
    bool hasTheme_ = false, hasPending_ = false, wipe_ = false, pendingWipe_ = false;
    float wipeFront_ = 0, impact_ = 0, pendingImpact_ = 0, swell_ = 0, wipeSeed_ = 0;
    int wipeShape_ = WIPE_RADIAL, pendingShape_ = WIPE_RADIAL;
    static constexpr float WIPE_W = 0.35f;
    float pauseFade_ = 0, settleGlow_ = 0;
    bool dim_ = false;
    float govern_ = 1.f;      // automatic exposure reduction when the scene gets too bright
    int sceneLevels_ = 1;

    mat4 view_, proj_, vp_;
    vec3 camPos_;
    float ptimeFrom_ = 0, ptimeTo_ = 0; // particle clocks (speed follows music intensity)
    float kick_ = 0;
    float clearGlow_ = 0;
    std::vector<Burst> bursts_;
    std::vector<Dying> dying_;
    float rayTime_ = 0;
    Rng rng_{12345};
    static constexpr int MAX_PARTICLES = 26000;
};
