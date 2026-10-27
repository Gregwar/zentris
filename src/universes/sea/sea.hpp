#pragma once
// SEA: glass cubes of water over a rising sea, a 3D diorama (see sea.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class SeaUniverse : public Universe {
public:
    ~SeaUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void drop(float strength) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override { slowOn_ = on; }
    void levelUp() override { refresh(1.f); }
    void kick(float amount) override { kickIn_ = std::max(kickIn_, amount); }
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

    // A variant's colors, linear RGB once loaded (floats only: palettes blend).
    struct Palette {
        vec3 skyTop, skyLow, sun, cloud, stormSky, stormCloud, sea0, sea1, sea2, deep, foam, haze, stormSea, panel;
        vec3 hull, super_, sail, baccent, window, cont0, cont1, cont2, ink, accent, dusk;
        vec3 piece[7];
        vec3 glass[7];
        float sunX, sunY, sunR, moon, stars, panelA, clouds, lightPanel, night;
        // the 3D light: the sun's (or moon's) azimuth (degrees, 0 straight into the scene, 90 to the right, 180 behind
        // the eye) and elevation (degrees; negative: a fraction of the sky shown above the horizon), its strength, the
        // sky's light, how dark its shadows
        float sunAz, sunEl, lightK, ambK, shadowK;
    };

private:
    // ---- The stack, ghost, falling piece and previews: glass cubes on the board plane, drawn over the world.
    struct CellInst { float a0[4], a1[4], a2[4], a3[4]; };
    std::vector<CellInst> cells_;
    void cube(float x, float y, float size, const vec3& col, float alpha, bool ghost, float seed, float crack = 0.f,
              vec2 crackP = vec2{0.f, 0.f});
    void flushCells();

    // ---- Cubes thrown out of a cleared row: rigid bodies tumbling on all axes into the world behind the well; each
    // cracks where it first hits (the sand, the sea, another cube) and its water jets out of the crack under pressure,
    // raising the sea; empty, it sinks into the sand.
    struct Body {
        vec3 p, v, w;           // position, velocity, angular velocity (world)
        float q[4];             // orientation (x, y, z, w)
        vec3 crackL, crackN;    // the crack: where on the glass and its outward normal (the cube's frame)
        int type;
        float seed, age, delay, hitT, crack, fill, water, pend, acc, fade, still;
        bool hit, wet, asleep, passed;
    };
    std::vector<Body> bodies_;
    void throwCube(const vec3& p, int type, const vec3& v, const vec3& w, float delay, float water);
    void simulateBodies(float dt);
    void crackAt(Body& b, const vec3& cornerL);
    void leak(Body& b, float dt);
    // A game over: the stack cracks row by row from the top, each cube then thrown out like a cleared one.
    struct Dying { float x, y, delay, t; int type; bool gone; };
    std::vector<Dying> dying_;
    int boardCount_ = 0; // the board's cubes last frame (a whole stack appearing at once: a review, a game joined)
    void watchBoard(const Game& game);

    // ---- Particles: the cubes' water (jets, runs on the sand), splashes, spray, bubbles, plumes, sand.
    enum { P_JET = 0, P_RUN = 1, P_SPLASH = 2, P_SPRAY = 3, P_BUBBLE = 4, P_PLUME = 5, P_SAND = 6 };
    struct Particle {
        vec3 p, v;
        float age, life, size, grav, drag, seed;
        int kind;
        vec3 col;
        bool real;          // ages at the real speed, even in slow motion
        float water = 0.f;  // lines of sea it carries (added when it reaches the sea)
    };
    std::vector<Particle> parts_;
    void splash(const vec3& p, float power, const vec3& col);
    void sprayBurst(float x, float z, int n, float power);
    void simulateParticles(float dt, float wdt);

    // ---- The sea: its level (rising with the cleared lines), its swell, its storm.
    float waterLines_ = 0.f;  // lines of water poured into the sea this song
    float rise_ = 0.f;        // 0..1 shown (approaches the target)
    float riseTarget() const;
    float seaLevel() const;
    float waveH(float x, float z) const;       // the swell above the sea's level (WORLD_GLSL's waveH)
    float surfY(float x, float z) const { return seaLevel() + waveH(x, z); }
    float amp_ = 0.06f, sharp_ = 0.2f, caps_ = 0.f, glint_ = 0.5f, rock_ = 0.03f;
    float waveT_ = 0.f, cloudT_ = 0.f;
    float dir_ = 1.f;
    float stormLin_ = 0.f, storm_ = 0.f;
    float flash_ = 0.f, flashAz_ = 20.f;
    float sprayAcc_ = 0.f;
    float dropPending_ = 0.f;
    bool sectionDrop_ = false, sectionKnown_ = false;

    // ---- Boats: low-poly ships sailing slowly across the sea, bigger as it deepens (the big ones far out).
    struct Boat { float x, z, dir, speed, scale, phase, seed, alpha; int type; };
    std::vector<Boat> boats_;
    float boatT_ = 0.f;
    bool spawnBoat(bool onScreen);

    // ---- Life in the sea, in layers: schools of small fish near the surface, jellyfish drifting in the middle
    // water, and in the dark depths an anglerfish and its lure, siphonophores, points of light. Only where there is
    // water, behind the diorama's cut face.
    enum { L_FISH = 0, L_JELLY = 1, L_ANGLER = 2, L_SIPHON = 3, L_MOTE = 4 };
    struct Critter {
        int kind, school;
        float x, z, u, du, dir, speed, size, phase, seed, alpha, dart, dartY, dyv;
        vec3 col, glow;
    };
    std::vector<Critter> critters_;
    int nextSchool_ = 0;
    float lifeT_ = 0.f, glowUp_ = 0.f;
    float hwAt(float z) const;               // the half width of the view at depth z (board units)
    float waterDepth() const;                // the sea's depth at the cut face (board units)
    float critterY(const Critter& c) const;
    void spawnLife(bool onScreen);
    void simulateLife(float dt, float wdt, float world);

    void simulate(const Game& game, const MusicState& music, float dt, float wdt);

    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    int pickVariant(const SongLook& look) const;
    float bgLuma(float bx, float by) const;

    // ---- Text.
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    struct Font {
        bool ok = false;
        GLuint tex = 0;
        float px = 64.f, scaleTT = 1.f, capH = 40.f;
        Glyph g[96];
        void* info = nullptr; // stbtt_fontinfo
    };
    Font fontItalic_, fontRoman_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const Font& f, const std::string& s, float size, float track) const;
    float textQuads(const Font& f, const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const;
    void drawText(const Font& f, const std::vector<float>& quads, const vec3& ink, float alpha, float bias);

    // ---- The camera: the layout's reference lens raised above the sea, its image shifted back down (a shift lens), so
    // the board plane falls on the very same pixels while the world behind it is seen from above.
    void setCamera();
    vec3 sunDir() const;
    mat4 view_, vp_, invVP_, lightVP_;
    vec3 eye_{0.f, 4.5f, 34.6f}, camR_{1.f, 0.f, 0.f}, camU_{0.f, 1.f, 0.f};
    float horizon_ = 0.3f;     // the horizon's height on screen (NDC)
    float camDist_ = 34.6f;

    // ---- GL.
    struct Mesh { GLuint vao = 0, vbo = 0, ibo = 0; int count = 0; };
    void buildMeshes();
    void worldUniforms(GLuint prog);
    void drawBoats(GLuint prog, bool depthOnly);
    void drawBoxes(GLuint prog);
    GLuint progSky_ = 0, progSand_ = 0, progSurf_ = 0, progFront_ = 0, progBoat_ = 0, progDepth_ = 0, progCube_ = 0,
           progLife_ = 0, progPart_ = 0, progRain_ = 0, progVeil_ = 0, progCell_ = 0, progSprite_ = 0, progText_ = 0,
           progShape_ = 0, progPost_ = 0;
    Mesh sand_, surf_, front_;
    GLuint meshVbo_ = 0, meshInstVbo_ = 0, boatVao_ = 0, boxVao_ = 0;
    int boatFirst_[6] = {}, boatCount_[6] = {}, boxFirst_ = 0, boxCount_ = 0;
    GLuint quadVbo_ = 0, cellVao_ = 0, cellVbo_ = 0, spriteVao_ = 0, spriteVbo_ = 0, cubeVao_ = 0, cubeVbo_ = 0, lifeVao_ = 0,
           lifeVbo_ = 0, partVao_ = 0, partVbo_ = 0, textVao_ = 0, textVbo_ = 0, shapeVao_ = 0, shapeVbo_ = 0;
    GLuint msFbo_ = 0, msColor_ = 0, msDepth_ = 0, shadowFbo_ = 0, shadowTex_ = 0;
    static constexpr int SHADOW_RES = 1536;
    int samples_ = 4;
    ugl::Target sceneT_, bgT_; // the scene, and a copy of it behind the glass
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;
    GLuint timer_[2] = {0, 0};
    int timerFrame_ = 0;
    double timerAcc_ = 0.0;
    int timerN_ = 0;

    // ---- Song, variant, palette.
    int variant_ = 0;
    bool palSet_ = false;
    Palette pal_{};
    uint64_t seed_ = 0;

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;

    // ---- Music.
    float time_ = 0.f, worldTime_ = 0.f;
    double lastBar_ = -1.0;
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f;
    float gust_ = 0.f;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    bool over_ = false;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    Bridge bridge_;
    Rng rng_{5150};
};
