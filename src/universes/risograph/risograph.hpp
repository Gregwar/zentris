#pragma once
// RISOGRAPH: a two-ink risograph print (see risograph.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class RisographUniverse : public Universe {
public:
    ~RisographUniverse() override;
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

private:
    // One instanced quad (see MARK_FS for the meaning of each field by kind).
    struct Inst { float a0[4], a1[4], a2[4], a3[4], a4[4]; };
    std::vector<Inst> inst_;
    std::vector<Inst> cellInst_; // the pieces' cells: their own ink buffer (the drop prints them in other inks)
    void rule(vec2 a, vec2 b, float width, const vec4& ink);
    void ring(vec2 c, float r, float width, const vec4& ink, float discR, float discDens, const vec3& discInk);
    void cell(vec2 pos, float scale, float rot, int type, int mask, float alpha, vec2 offA, vec2 offB, float tear = 0.f,
              float starve = 0.f, bool ghost = false, float seed = 0.f);
    void buildBoard(const Game& g);
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    void drawPanels(const std::vector<HudRect>& panels, bool eraseOnly);
    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
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
    Font fontBold_, fontReg_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const Font& f, const std::string& s, float size, float track) const;
    void textQuads(const Font& f, const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const;
    void drawText(const Font& f, const std::vector<float>& quads, const vec4& ink, float bias);

    // ---- GL.
    GLuint progWorld_ = 0, progMark_ = 0, progText_ = 0, progPanel_ = 0, progComp_ = 0;
    GLuint quadVbo_ = 0, markVao_ = 0, markVbo_ = 0, textVao_ = 0, textVbo_ = 0, panelVao_ = 0, panelVbo_ = 0;
    ugl::Target inkT_, bgT_, pieceT_;
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;

    // ---- Song, variant, inks (linear, easing toward the variant's).
    int variant_ = 0;
    bool palSet_ = false;
    vec3 paper_{0.9f}, inkA_{0.f}, inkB_{0.f}, inkC_{0.f}, inkD_{0.f};
    float dark_ = 0.f;
    // The drop's inks: for the whole drop (not a chorus), the pieces print from another pair of drums.
    vec3 dropA_{0.f}, dropB_{0.f};
    bool inDrop_ = false;
    float dropInk_ = 0.f; // 0 the variant's pair .. 1 the drop's pair (linear; eased when used)
    float dropWait_ = 0.f; // a peak announced, its downbeat (drop()) not hit yet
    // the main shapes print on the right, where the sheet is free (the title, the section and the universe's name
    // are on the left)
    int side_ = 1;
    float dirB_ = 0.6f, dirC_ = 2.4f; // the drums' misregistration directions (radians)

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;

    // ---- The world: three big flat shapes (B, A, C) and the Tetris stamp.
    struct Shape { float x, y, sx, sy, rot, type; };
    Shape shape_[3]{}, next_[3]{};
    float reprint_[3] = {1.f, 1.f, 1.f}; // a shape re-printed elsewhere (a new song): fades out, then in at the new place
    int reprintState_[3] = {0, 0, 0};    // 0 idle, 1 shrinking, 2 growing
    float pres_[3] = {1.f, 0.f, 0.f};    // presence of each shape (follows the level)
    int comp_ = 0;
    float stampT_ = 9.f;
    float drift_ = 0.f;
    void composition(int comp, Shape out[3]) const;

    // ---- Music.
    float worldTime_ = 0.f;
    double lastBeat_ = -1.0;
    int beatCount_ = 0;
    float kickIn_ = 0.f, prevKick_ = 0.f;
    float kickTgt_ = 0.f, kickEnv_ = 0.f; // the kicks, smoothed into a slow breath of ink density
    float layer_ = 0.f, layerT_ = 9.f; // the drop's print: presence, time since it started inking in
    float bloom_ = 0.f, dropT_ = 9.f, dropAmp_ = 0.f; // the third ink's gradient (the peak), the drop's swell of it
    // The background's current: a clock (flow_) running at a speed that follows the section, eased twice so the
    // forms never change speed abruptly; glide_ is the gliders' clock, restarted while they are not printed.
    float flow_ = 0.f, speed_ = 0.f, speedMid_ = 0.f, glide_ = 0.f, glideIn_ = 0.f, breathS_ = 0.f;
    vec2 offB_{0.f, 0.f}, offC_{0.f, 0.f};       // the background's registration (never the stack's)
    vec2 blotPos_{0.f, 0.f};
    float blotT_ = 9.f, blotK_ = 0.f;             // the last lock's soft blot

    // ---- Board bookkeeping: which lock each cell came from (one piece = one shape) and how hard it landed.
    struct InkCell { int id = -1, type = -1; float hard = 0.f; };
    InkCell ink_[Game::H][Game::W];
    int pieceCounter_ = 1;
    float lastHard_ = 0.f;
    int maskAt(const Game& g, int x, int y) const;
    void adoptUnknown(const Game& g);

    // ---- Effects.
    struct Strip { int x, row, type, mask; float y, t, delay, dir; };   // a cleared row torn off
    std::vector<Strip> strips_;
    struct Dying { int x, type, mask; float y, t, delay; };            // game over: the drum running dry
    std::vector<Dying> dying_;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    Bridge bridge_;
    Rng rng_{4711};
};
