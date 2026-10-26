#pragma once
// FLUO: fluorescent paint splashed on a dark wall (see fluo.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class FluoUniverse : public Universe {
public:
    ~FluoUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void drop(float strength) override;
    void refresh(float strength) override { (void)strength; }
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override { slowOn_ = on; }
    void kick(float amount) override { kickIn_ = std::max(kickIn_, amount); }
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

private:
    // One instanced quad (paint stamps and the play share the layout: see shaders.hpp).
    struct Inst { float a0[4], a1[4], a2[4], a3[4]; };

    // ---- Paint: gobs of paint in flight from a cleared row to the wall; splashes growing from where they land
    // for a fraction of a second and drips running down (both redrawn each frame into the wet buffers); once
    // done, they are stamped once into the persistent buffers and live only there.
    struct Fly {
        vec2 a, ctrl, b;            // a quadratic arc from the row to the wall
        float t = 0.f, delay = 0.f, dur = 0.4f;
        float r0, r1;               // the gob's radius leaving the row, landing
        float scale, stretch;       // the splash it makes
        vec3 color;
        bool recent = false;        // a splash beside the well (the drop's kicks start drips there)
    };
    std::vector<Fly> flies_;
    void fling(vec2 from, vec2 to, vec3 color, float scale, float delay, float arc);
    vec2 landing(vec2 from, float side);
    struct SPrim { Inst q; float d0, d1; bool capsule; }; // revealed when the burst's front passes d0 (to d1)
    struct Splash {
        vec2 c;
        float t = 0.f, delay = 0.f, dur = 0.15f, reach = 1.f;
        std::vector<SPrim> prims;
        struct DripSeed { vec2 p; float w, len; };
        std::vector<DripSeed> drips;
        vec3 color;
    };
    struct Drip { vec2 p; float w, len = 0.f, maxLen, v; vec3 color; float seed; bool done = false; };
    std::vector<Splash> splashes_;
    std::vector<Drip> drips_;
    std::vector<Inst> stamps_, wet_;
    struct Recent { vec2 p; float r; vec3 color; };
    std::vector<Recent> recent_; // splashes beside the well (where the drop's kicks start drips)
    size_t recentNext_ = 0;
    void addRecent(const Recent& rc);
    void splash(vec2 c, vec3 color, float scale, vec2 dir, float delay, float stretch = 1.f);
    void rowBurst(const std::vector<GameEvent::CellInfo>& cells, int fromX, float scale, float delay0, float rowDelay);
    void stampPrim(const Inst& q) { stamps_.push_back(q); }
    void updatePaint(float dt, float wdt);
    void buildWet();
    void dripInst(const Drip& d, Inst q[2]) const;
    void drawPaint(const std::vector<Inst>& q, const ugl::Target& color, const ugl::Target& height);
    void flushStamps();
    void clearPaint();

    // ---- Play.
    std::vector<Inst> play_;
    void cellInst(vec2 pos, float scale, int type, int mask, float alpha, float flash, bool ghost);
    void buildPlay(const Game& g, float t);
    struct Speck { vec2 p, v; vec3 color; float t, life, r; };
    std::vector<Speck> specks_;

    // ---- Text.
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    struct Font {
        bool ok = false;
        GLuint tex = 0;
        float px = 64.f, scaleTT = 1.f, capH = 40.f;
        Glyph g[96];
        void* info = nullptr; // stbtt_fontinfo
    };
    Font font_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const std::string& s, float size, float track) const;
    void textQuads(const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const;
    void drawText(const std::vector<float>& quads, const vec3& srgb, float alpha, float bias, float soft);
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels, float mul);

    // ---- GL.
    GLuint progWall_ = 0, progPaint_ = 0, progFlat_ = 0, progComp_ = 0, progPlay_ = 0, progText_ = 0, progPanel_ = 0;
    GLuint quadVbo_ = 0, instVao_ = 0, instVbo_ = 0, textVao_ = 0, textVbo_ = 0, panelVao_ = 0, panelVbo_ = 0;
    ugl::Target wallT_, paintT_, heightT_, wetT_, wetHT_;
    bool wallDirty_ = true;
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;

    // ---- Variant (colors eased toward it).
    int variant_ = 0;
    bool palSet_ = false;
    vec3 wallA_{0.f}, wallB_{0.f}, uvTint_{0.f}, text_{1.f}, accent_{1.f};
    vec3 piece_[7];
    float tex_[4] = {0, 0, 0, 0};
    vec3 pieceColor(int type) const { return piece_[type < 0 ? 0 : type > 6 ? 6 : type]; }

    // ---- Level and drop.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    bool inDrop_ = false;
    float dropWait_ = 0.f;
    float uv_ = 0.f;          // the blacklight: 0 room light .. 1 lights off (eased)
    float ignite_ = 0.f;      // the stains lighting up after the lights went off
    float dropT_ = 99.f, dropAmp_ = 0.f; // time since drop(), its strength
    float lampT_ = 0.f;       // the moving light's clock
    float light_ = 1.f;       // the room's light (eased)
    float kickIn_ = 0.f, prevKick_ = 0.f, dripCool_ = 0.f;
    float slow_ = 0.f;
    bool slowOn_ = false;
    float time_ = 0.f;
    float dimT_ = 0.f;
    float wipe_ = 0.f;        // a new song: the paint fades out, then the wall is clean
    bool cleanedForEntry_ = false;

    // ---- Board bookkeeping: which piece each cell came from (one piece = one shape) and when it locked.
    struct PCell { int id = -1, type = -1; float lockT = -9.f; };
    PCell cells_[Game::H][Game::W];
    int pieceCounter_ = 1;
    float hardK_ = 0.f;
    int maskAt(const Game& g, int x, int y) const;
    void adoptUnknown(const Game& g);

    float gaugeShown_ = 0.f;
    Bridge bridge_;
    Rng rng_{90210};
};
