#pragma once
// LASER SHOW: a dark, hazy room where each piece is projected by its own laser; edge-lit acrylic pieces (see laser.cpp).
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class LaserUniverse : public Universe {
public:
    ~LaserUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void drop(float strength) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override;
    void levelUp() override;
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }
    void forceFx(int slow, int tetris) override { (void)slow; if (tetris >= 0) saluteT_ = 0.f; }

    // A laser set (linear RGB once loaded; floats only, so two sets blend).
    struct Palette {
        vec3 laser[3];     // the song's lasers (the slots of a fan take them in turn)
        vec3 dropLaser[3]; // the drop's
        vec3 haze;         // the room's own faint haze
        vec3 rim, ink, accent;
        vec3 piece[7];
    };

private:
    // ---- The projectors: one per piece (the falling one and each piece of the stack), at a random spot of the dark
    // room, projecting its piece: a soft cone of light and thin rays to the corners of its outline.
    struct Proj {
        vec2 home;            // where it stands (board space, outside the well and the HUD)
        float seed = 0.f;
        float power = 0.f;    // on 0..1, eased
        int type = 0;
    };
    std::map<int, Proj> projs_; // by piece id (own_)
    Proj fall_;                 // the falling piece's
    bool hasFall_ = false;
    // A cleared part: its projector gets weak, blinks and switches off (a whole piece: its lens too).
    struct DyingCone {
        vec2 src;
        std::vector<vec2> corners;
        int type;
        float t, delay, seed, power;
        bool lens;
    };
    std::vector<DyingCone> dyingCones_;
    struct BeamGpu { float a0[4], a1[4], a2[4]; };
    struct ConeGpu { float a0[4], a1[4], a2[4]; };
    std::vector<BeamGpu> beams_;
    std::vector<ConeGpu> cones_;
    vec2 randomSpot();
    vec2 srcPos(const Proj& p) const;
    vec3 laserColor(int type) const;
    void buildProjection(const Game& game);
    void addProjection(vec2 src, const std::vector<vec2>& corners, const vec3& col, float power, float depth, bool lens);
    void updateProjectors(const Game& game, const MusicState& music, float dt);
    // The outline corners of a set of cells (game grid), at their drawn positions.
    std::vector<vec2> outline(const std::vector<std::pair<int, int>>& cells, const Game* game, float dx, float dy) const;

    struct Tile { float a0[4], a1[4], a2[4], a3[4]; };
    struct Spark { float a0[4], a1[4], a2[4]; };
    struct Vert { float x, y, r, g, b, a; };
    std::vector<Tile> tiles_;
    std::vector<Spark> sparks_, lenses_;
    void tile(float x, float y, float h, const vec3& c, float alpha, int kind, int mask, float flash, float scale,
              float e0 = 1.f, float e1 = 0.f, float e2 = 0.f, float e3 = 0.f);

    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
    void buildBoard(const Game& game);
    void buildMasks(const std::vector<HudText>& hud);
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    int pickVariant(const SongLook& look) const;

    // ---- Text.
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    struct Font {
        bool ok = false;
        GLuint tex = 0;
        float px = 64.f, scaleTT = 1.f, capH = 40.f;
        Glyph g[96];
        void* info = nullptr; // stbtt_fontinfo
    };
    Font fontLight_, fontMedium_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const Font& f, const std::string& s, float size, float track) const;
    float textQuads(const Font& f, const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const;
    void drawText(const Font& f, const std::vector<float>& quads, const vec3& color, float alpha, float halo, float bias);

    // ---- GL.
    GLuint progCone_ = 0, progHaze_ = 0, progComp_ = 0, progBeam_ = 0, progWell_ = 0, progShape_ = 0, progTile_ = 0, progSpark_ = 0,
           progText_ = 0, progDown_ = 0, progUp_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, coneVao_ = 0, coneVbo_ = 0, tileVao_ = 0, tileVbo_ = 0, beamVao_ = 0, beamVbo_ = 0, sparkVao_ = 0, sparkVbo_ = 0,
           shapeVao_ = 0, shapeVbo_ = 0, textVao_ = 0, textVbo_ = 0, volTex_ = 0, noiseTex_ = 0;
    ugl::Target hazeT_, sceneT_;
    static constexpr int BLOOM_MIPS = 5;
    ugl::Target bloomT_[BLOOM_MIPS];
    void createTargets();
    void buildNoise();
    void setHazeUniforms(GLuint prog);
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;
    std::vector<float> maskRects_; // the HUD's quiet zones (board x0 y0 x1 y1)

    // ---- Song, variant, palette.
    int variant_ = 0;
    bool palSet_ = false;
    Palette pal_{};
    uint64_t seed_ = 0;
    float seedF_ = 0.f;

    // ---- The music's staging: continuous weights of each moment, eased.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvTarget_ = 1.f;
    float energy_ = 0.5f;
    float wCalm_ = 0.f, wMid_ = 1.f, wChorus_ = 0.f, wDrop_ = 0.f; // the moment, eased
    float dropColor_ = 0.f;   // the drop's lasers (0 the song's)
    bool inDrop_ = false;     // the drop look holds (until the peak ends)
    float dropWait_ = -1.f;   // rising into a peak: seconds waiting for the drop's downbeat
    float dropT_ = 99.f;      // seconds since the drop opened
    float tension_ = 0.f;     // a build: more projectors switch on as the pace rises
    float swell_ = 0.f;       // the haze lit up once (a drop, a level up), settling over seconds
    float sig_ = 1.f;         // the section's pace and glow (a drop runs faster and brighter than a chorus)
    float bass_ = 0.f, high_ = 0.f;
    float beatClock_ = 0.f;   // beats, slowed with the slow motion
    double lastBeat_ = -1.0;
    float stepPhase_ = 0.f, stepShown_ = 0.f, lastHalf_ = -1.f;
    vec3 flow_{0.f};
    float worldTime_ = 0.f;
    float saluteT_ = 99.f;    // a Tetris: every projector flares up white once

    // ---- Effects.
    struct SparkFx { vec2 p; float t, life, size, len; vec3 c; };
    std::vector<SparkFx> sparkFx_;
    struct Dying { float x, y, t, delay; int type, mask; bool over; float seed; };
    std::vector<Dying> dying_;
    int lastDrop_ = 0;
    // The piece each locked cell came from: a piece's cells join (no seam inside it), two pieces never do.
    struct Owner { int id = -1, type = -1; };
    Owner own_[Game::H][Game::W];
    int pieceCounter_ = 0;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    float overT_ = -1.f, over_ = 0.f;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    Bridge bridge_;
    Rng rng_{1414};
};
