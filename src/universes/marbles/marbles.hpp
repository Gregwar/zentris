#pragma once
// MARBLES: square glass tiles, each holding one glass marble; cleared rows pop and shoot their marbles out across a
// dark table seen from above, where they roll, collide and stain the felt with their colors (see marbles.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class MarblesUniverse : public Universe {
public:
    ~MarblesUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void drop(float strength) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override { slowOn_ = on; }
    void levelUp() override;
    void kick(float amount) override { kickIn_ = std::max(kickIn_, amount); }
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

    // A variant's colors, linear RGB once loaded (floats only: palettes are blended float by float).
    struct Palette {
        vec3 felt, fiber, panel, panelEdge, tile, ink, accent, wave, night, lamp;
        vec3 piece[7];
        float tileTint, tileAlpha, panelAlpha, surface, grain, dye, light, halo;
    };

private:
    // ---- Instances.
    struct MarbleInst { float a0[4], a1[4], a2[4], a3[4], a4[4]; };
    struct FxInst { float a0[4], a1[4], a2[4]; };
    struct Vert { float x, y, r, g, b, a; };
    std::vector<MarbleInst> inst_;
    std::vector<FxInst> fx_;
    // kind: 0 a marble in the landscape, 1 a tile and its marble, 2 the ghost's outline, 3 a tile alone, 4 a pane
    void put(float x, float y, float half, float radius, const vec3& col, float alpha, float rot, float glow, float seed,
             int kind, float mx = 0.f, float my = 0.f, float flare = 0.f, float haze = 0.f, const vec3* lit = nullptr,
             float lightAngle = -9.f);
    void putFx(float x, float y, float size, float rot, const vec3& col, float alpha, int kind);
    void flush();
    void flushFx();

    // ---- The stack's marbles: per board cell, its marble's look and how it jolts on an impact.
    struct Slot {
        float seed = 0.f, rot = 0.f;
        float bounce = 0.f, bounceT = 9.f; // amplitude, time since the impact
        float flare = 0.f;                 // a glint of the window on its glass (a hard drop)
        bool known = false;
        int type = -1;
    };
    Slot slots_[Game::H][Game::W];
    int nextId_ = 1;
    int lastDrop_ = 0;
    void syncSlots(const Game& game);
    float bounceOf(const Slot& s) const;

    // A tile popping (its marble already thrown out): it swells a little and is gone (real time).
    struct Pop { float x, y, t, delay; int type; };
    std::vector<Pop> pops_;

    // ---- The marbles on the table (under the well and the HUD): shot out, rolling, colliding, at rest.
    struct Marble {
        vec2 p, v;
        float r = 0.4f, rot = 0.f, seed = 0.f;
        int type = 0;
        float age = 0.f, delay = 0.f;
        float sleepT = 0.f;
        bool asleep = false;
        bool under = true;     // under the well's pane: gliding out, no collisions
        float fade = 1.f;      // fading out (retired)
        bool retiring = false;
        float light = 0.f, prevLight = 0.f, twinkle = 0.f;
        float spin = 0.f;
        float trail = 0.f;     // distance rolled since its last trace on the felt
        vec3 lit{0.f, 0.f, 0.f};   // the light it receives (traced, smoothed), linear rgb
        vec2 litDir{0.f, -1.f};    // the way that light travels
    };
    std::vector<Marble> marbles_;
    void launch(vec2 from, int type, float seed, float rot, float delay, float vigor);
    void stepPhysics(float h, bool stains);
    void simulateMarbles(float dt);
    void seedMarbles(int n);
    void buildGrid();
    std::vector<int> gridHead_, gridNext_;
    int gridW_ = 1, gridH_ = 1;
    float gridX0_ = -24.f, gridY0_ = -14.f;
    static constexpr float GRID = 0.9f;

    // The HUD (board space): marbles roll under it, dimmed there.
    struct KeepOut { float x0, x1, y0, y1; };
    std::vector<KeepOut> keepOut_;
    float hudShade(vec2 p) const;

    // ---- The dye on the felt: soft stains of a marble's color where it is hit (and faint traces where it rolls),
    // painted into a persistent target; a new song washes it out.
    struct StainInst { float a0[4], a1[4], a2[4]; };
    std::vector<StainInst> stains_;
    void stain(vec2 p, float radius, const vec3& col, float alpha, vec2 stretch = vec2{0.f, 0.f});
    float washT_ = 0.f;    // > 0: the felt being washed (a new song)
    bool washNow_ = true;  // clear at once (the first frame)

    // ---- Light: waves through the heaps at a drop, glints.
    struct Wave {
        float t;      // seconds since it started
        int shape;    // 0 out from the well, 1 in from the edges, 2 a sweep across, 3 rising, 4 rings from a marble, 5 diagonal
        float sign;
        vec2 o;
        float speed, width, gain;
        int bands;
    };
    std::vector<Wave> waves_;
    void startWave(int shape, float gain, int bands);
    float waveCoord(const Wave& w, vec2 p) const;
    struct Glint { vec2 p; float t, life, size; vec3 col; };
    std::vector<Glint> glints_;
    void glint(vec2 p, float size, const vec3& col);
    double lastHalf_ = -1.0, lastBar_ = -1.0;
    int dropCount_ = 0, dropShape_ = 0, waveIndex_ = 0;

    // ---- The traced light: slow beams grazing the table, rays traced in 2D through the marbles (glass balls:
    // refraction in and out, a Fresnel reflection, absorption in their tint, a spectrum on the drop) across the felt.
    struct LightInst { float a0[4], a1[4], a2[4]; };
    struct Ray {
        vec2 o, d;
        vec3 c;
        float w = 0.1f, n = 1.5f;
        int depth = 0, hits = 0;
        float spread = 0.f, gi = 1.f;   // how fast its band widens (out of a marble: a caustic opening), its strength
        bool split = false;
    };
    std::vector<LightInst> light_;
    std::vector<Ray> rays_;
    std::vector<vec3> acc_;
    std::vector<vec2> accDir_;
    int segs_ = 0;
    float beamOn_[2] = {0.f, 0.f};
    float disp_ = 0.f, beamWorld_ = 0.f;
    vec2 keyDir_{0.5f, -0.86f};
    vec3 keyCol_{1.f, 1.f, 1.f};
    float keyAmt_ = 0.f;
    void traceLight(float dt, float world);
    void traceRay(const Ray& first);
    bool hitMarble(vec2 o, vec2 d, float t0, float tMax, float& tHit, int& idx) const;
    void emitShaft(vec2 a, vec2 b, float w0, float w1, const vec3& c, float gain, float fall);

    void simulate(const Game& game, const MusicState& music, float dt);
    void drawWorld(float world);
    void drawBoard(const Game& game, float world);
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
    void drawText(const Font& f, const std::vector<float>& quads, const vec3& ink, float alpha, float bias);

    // ---- GL.
    GLuint progLight_ = 0, lightVao_ = 0, lightVbo_ = 0;
    GLuint progStain_ = 0, progWash_ = 0, stainVao_ = 0, stainVbo_ = 0;
    GLuint progSky_ = 0, progMarble_ = 0, progFx_ = 0, progText_ = 0, progShape_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, marbleVao_ = 0, marbleVbo_ = 0, fxVao_ = 0, fxVbo_ = 0, textVao_ = 0, textVbo_ = 0, shapeVao_ = 0,
           shapeVbo_ = 0;
    // the frame; the table alone (seen through the marbles); the traced light (half resolution, mipmapped); the dye
    ugl::Target sceneT_, bgT_, lightT_, stainT_;
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;

    // ---- Song, variant, palette.
    int variant_ = 0;
    bool palSet_ = false;
    Palette pal_{};
    uint64_t seed_ = 0;
    float seedF_ = 0.f;
    float vivid_ = 0.f;   // 0 the soft colors of the song .. 1 the drop's rich, glowing ones

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;
    float drive_ = 0.f, glow_ = 0.f, swell_ = 0.f;
    float dropPending_ = 0.f;
    bool sectionDrop_ = false, sectionKnown_ = false;

    // ---- Music.
    float time_ = 0.f;
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f;
    bool hasBeat_ = false;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    float overT_ = -1.f;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    Bridge bridge_;
    Rng rng_{4242};
};
