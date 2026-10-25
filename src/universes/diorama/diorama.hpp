#pragma once
// DIORAMA: a sunlit clay miniature (see diorama.cpp).
#include <vector>

#include "universe.hpp"

class DioramaUniverse : public Universe {
public:
    ~DioramaUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override { inverted_ = on; }
    void levelUp() override; // a ring on the water and a flock of birds
    void kick(float amount) override { kick_ = std::max(kick_, amount); }
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return worldTo_; }
    // The world itself is not dissolved natively (the host draws the bridge over it), but the camera glides to
    // the layout's front view while a change runs, so both wells meet at the same pixels.
    void setBridge(const Bridge& b) override { bridge_ = b; }

private:
    // ---- GPU resources.
    struct Mesh { GLuint vao = 0, vbo = 0; int count = 0; };
    // Instances of the block program: position + kind, scale + contact mask, rotation, color + glow.
    struct Inst { vec4 a, b, q, c; };
    struct Sprite { vec4 a, b, c; }; // pos + size, color + alpha, kind / rotation / glow / aspect
    // A rounded card under HUD text (pixels, top-left origin).
    struct Card { float x, y, w, h, alpha; bool accent = false; };
    GLuint compile(const char* vs, const char* fs, const char* name);
    void createTargets();
    void destroyTargets();
    void buildWorld();
    void uploadWorld(Mesh& m, const std::vector<float>& v);
    void uploadInstMesh(Mesh& m, const std::vector<float>& v);
    void drawInstances(const Mesh& m, const std::vector<Inst>& inst, GLuint prog);
    void drawWorld(GLuint prog, bool depthOnly);
    void drawScene(bool depthOnly, bool mirror);
    void setCommonUniforms(GLuint prog);
    void layoutHud(const std::vector<HudText>& in, std::vector<HudText>& out, std::vector<Card>& cards) const;
    void drawHud(const std::vector<HudText>& hud, const std::vector<Card>& cards, const std::vector<HudRect>& panels);
    void buildFont();
    float textWidth(const std::string& text, float scale) const;
    void setCamera(float time);

    // ---- Simulation.
    // Lighting and palette of the moment (time of day x landscape x night).
    struct Env {
        vec3 sunDir, skySunDir, sunCol, skyCol, groundCol, shadowTint, zenith, horizon, fogCol, sunFogCol;
        vec3 pal[16], shallow, deep, piece[7];
        float fogDensity = 0, mist = 0, sunGlow = 0, rim = 0, cloud = 0, stars = 0, lantern = 0, wind = 0, wave = 0,
              exposure = 1, glint = 1;
    } env_;
    void computeEnv(float dt, const MusicState& music);
    void simulate(const Game& game, const MusicState& music, float dt);
    void collect(const Game& game, const MusicState& music, double time);
    vec3 cellPos(float x, float y) const;
    void spawnDust(vec3 pos, vec3 dir, int n, float strength);
    void splash(vec3 pos, float strength);
    void addRipple(float x, float z, float strength);
    void spawnBody(vec3 pos, int type, vec3 vel, float delay, float spin);
    void startLight(float tod, int world, float seconds, float impact);

    int samples_ = 4;
    GLuint progBlock_ = 0, progWorld_ = 0, progBlockDepth_ = 0, progWorldDepth_ = 0, progSky_ = 0, progWater_ = 0,
           progSprite_ = 0, progDown_ = 0, progBlur_ = 0, progComp_ = 0, progPanel_ = 0, progText_ = 0;
    GLuint emptyVao_ = 0, instVbo_ = 0, quadVbo_ = 0, spriteVbo_ = 0, spriteVao_ = 0, hudVao_ = 0, hudVbo_ = 0;
    Mesh cube_, sphere_, island_, tower_, waterMesh_;
    // Trees and rocks: one mesh each, placed per landscape (a landscape change regrows them).
    struct Prop { int mesh; vec3 pos; float scale, yaw; int world; };
    std::vector<Mesh> propMeshes_;
    std::vector<Prop> props_;
    GLuint occTex_ = 0;                       // board occupancy (contact shading on the back wall)
    GLuint shadowFbo_ = 0, shadowTex_ = 0;
    static constexpr int SHADOW_RES = 2048;
    GLuint msFbo_ = 0, msColor_ = 0, msDepth_ = 0;
    GLuint sceneFbo_ = 0, sceneTex_ = 0;
    GLuint reflFbo_ = 0, reflTex_ = 0, reflDepth_ = 0;
    GLuint halfFbo_[2] = {}, halfTex_[2] = {};
    int halfW_ = 1, halfH_ = 1;
    GLuint fontTex_ = 0;
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    Glyph glyphs_[96] = {};
    float fontPx_ = 48.f, fontAscent_ = 0.f, fontScaleTT_ = 1.f;
    bool fontOk_ = false;

    // ---- Scene state.
    bool hasLevel_ = false, hasSong_ = false;
    int level_ = 1;
    float energy_ = 0.5f;
    uint64_t seed_ = 0;
    float todFrom_ = 1, todTo_ = 1, tod_ = 1;           // time of day: 0 dawn, 1 day, 2 golden hour, 2.6 dusk
    int worldFrom_ = 0, worldTo_ = 0;                    // landscape palettes (variants)
    float transT_ = 1, transDur_ = 1;
    float gust_ = 0, flare_ = 0, night_ = 0;
    bool inverted_ = false;
    float pauseFade_ = 0;
    float kick_ = 0, kickEnv_ = 0, lastKickT_ = 0;
    float windTime_ = 0, waterTime_ = 0, cloudTime_ = 0, worldTime_ = 0;
    float birdTimer_ = 6.f;
    static constexpr int MAX_VARIANTS = 16;
    float propGrow_[MAX_VARIANTS] = {1};                 // per variant: how grown its trees are
    Bridge bridge_;
    float camK_ = 0.f; // 0: the diorama's own camera .. 1: the layout's front view (during a change)
    float focus_ = 1.f;

    // Effects.
    struct Body { vec3 pos, vel, axis; float angle, spin, delay, life, scale; int type; bool wet; vec3 color; };
    std::vector<Body> bodies_;
    struct Particle { vec3 pos, vel, color; float life, maxLife, size, rot, rotV; int kind; float grav, drag, alpha; };
    std::vector<Particle> parts_;
    struct Ripple { float x, z, age, strength; };
    std::vector<Ripple> ripples_;
    struct Ring { float age, strength; };
    std::vector<Ring> rings_;
    struct Bird { vec3 pos, vel; float phase; };
    std::vector<Bird> birds_;
    float squashT_[Game::H][Game::W] = {};

    // Per frame.
    std::vector<Inst> solid_, beads_;
    std::vector<Sprite> sprites_;
    mat4 view_, proj_, vp_, lightVP_, mirror_;
    vec3 camPos_, camRight_, camUp_;
    float gaugeShown_ = 0, readyGlow_ = 0;
    Rng rng_{12345};
};
