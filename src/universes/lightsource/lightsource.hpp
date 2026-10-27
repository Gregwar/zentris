#pragma once
// LIGHTSOURCE: a dim room where the falling piece is the light (see lightsource.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class LightsourceUniverse : public Universe {
public:
    ~LightsourceUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void drop(float strength) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override { slowOn_ = on; }
    void kick(float amount) override { (void)amount; }
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

private:
    struct Inst { float a0[4], a1[4], a2[4], a3[4]; };

    // ---- The board as LIGHTSOURCE knows it: which piece (lock) each cell came from, when it locked.
    int ids_[Game::H][Game::W];
    float lockAt_[Game::H][Game::W];
    float glint_[Game::H][Game::W]; // a small flare of an ember (the beat's twinkle in a chorus)
    int nextId_ = 1;
    void syncBoard(const Game& g);
    void collapseRows(const bool cleared[Game::H]);

    // ---- The lamps: a locked piece keeps shining, then dims smoothly to its ember (or a piece relit for a
    // moment by a new section).
    struct Lamp {
        int id = 0, type = 0;
        float age = 0.f, hold = 2.4f, seed = 0.f;
        bool relight = false;
        float power = 1.f;
        float level = 0.f; // its intensity this frame
    };
    std::vector<Lamp> lamps_;
    float lampLevel(const Lamp& l) const;
    bool lampAlive(const Lamp& l) const;
    void relight(int id, float power);

    // ---- Effects: the burning cells of a cleared row (and of a game over), the contact glows of the locks.
    struct Burn { vec2 p; int type; int row; float t = 0.f, life = 0.45f, delay = 0.f, power = 1.f, seed = 0.f; bool over = false; };
    std::vector<Burn> burns_;
    // a clear's afterlight: the light of the piece that vanished with the rows, lingering where they were
    float afterT_ = 99.f;
    vec2 afterPts_[4];
    vec3 afterCol_{0.f};
    struct Contact { vec2 a, b; vec3 c; float t = 0.f, life = 0.3f, power = 1.f; };
    std::vector<Contact> contacts_;
    bool hardPending_ = false;

    // ---- Lights for the light pass.
    static constexpr int MAXL = 12, MAXP = 96, MAXG = 48;
    static constexpr int SYNTH_ID = 10000000; // ids given to pieces of unknown origin (grouped by color, drawn apart)
    int nextSynth_ = SYNTH_ID;
    struct LightDef {
        vec3 color{0.f};
        float reach = 6.f, height = 1.1f, nearW = 0.8f, spread = 0.3f;
        int sub = 4;
        vec2 aim{0.f, 0.f};
        float coneOut = 0.f, coneIn = 0.f;
        std::vector<vec2> pts;
        int own[4][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
        float rank = 0.f;
    };
    std::vector<LightDef> lights_;
    struct Glow { vec2 p; vec3 c; float r; };
    std::vector<Glow> glows_; // unshadowed lights (the stack's dimmer pieces in a drop)

    // ---- A drop relights the whole stack: every piece a light again (a ripple from the bottom on the downbeat,
    // then each piece shimmering at its own pace and flaring in turn on the beat), back to embers when it ends.
    struct PieceInfo { int id, type, n; int cells[4][2]; vec2 c; float level, mono; };
    std::vector<PieceInfo> pieces_;
    struct DFlare { int id; float t; };
    std::vector<DFlare> dflares_;
    float waveT_ = 99.f;
    void buildPieces(const Game& g, float dropK);
    float pieceLevel(int id) const;
    float rowY_[Game::H];
    int rowMask_[Game::H];
    int rowMin_ = Game::H;
    void buildRows(const Game& g);
    void setRowUniforms(GLuint prog);
    // m: how far the piece has turned to the drop's single color (0: its own)
    vec3 lightOf(int type, float m = 0.f) const;
    vec3 emitOf(int type, float m = 0.f) const;
    vec3 pieceCol(int type, float m = 0.f) const;
    vec3 monoTint(int type) const; // the piece's shade of its side's drop color (max channel 1)

    // ---- A drop's colors: a pair per drop (from the song's seed and the drop's index, never the last pair).
    vec3 dropHue_{1.f}, dropHue2_{1.f}; // side A's and side B's
    int dropIndex_ = 0, lastHue_ = -1;
    uint64_t songSeed_ = 0;
    float monoOf(int id) const;
    bool huePicked_ = false;
    void pickHue();
    float pieceOn_ = 1.f; // the falling piece's light coming on after it spawns
    std::vector<std::pair<int, float>> shadowW_; // per stack piece in a drop: its share as a shadowed light (eased)
    float monoG_ = 0.f; // the drop's color over the whole scene (the falling piece, the burning rows, the spots)

    // ---- Play.
    std::vector<Inst> blocks_, fx_;
    void block(vec2 pos, float scale, const vec3& alb, float kind, const vec3& em, int conn, float alpha,
               float seed = 0.f, float burnT = 0.f, float hot = 0.f);
    void buildBlocks(const Game& g, float ember);
    static int connMask(int cc[4][2], int i);

    // ---- Text.
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    struct Font {
        bool ok = false;
        GLuint tex = 0;
        float px = 64.f, scaleTT = 1.f, capH = 40.f;
        Glyph g[96];
        void* info = nullptr;
    };
    Font font_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const std::string& s, float size, float track) const;
    void textQuads(const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const;
    void drawText(const std::vector<float>& quads, const vec3& srgb, float alpha, float bias, float soft);
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels, float mul);

    // ---- GL.
    GLuint progLight_ = 0, progScene_ = 0, progBlock_ = 0, progFx_ = 0, progFinal_ = 0, progText_ = 0, progPanel_ = 0;
    GLuint quadVbo_ = 0, instVao_ = 0, instVbo_ = 0, textVao_ = 0, textVbo_ = 0, panelVao_ = 0, panelVbo_ = 0;
    GLuint lightFbo_ = 0, lightTex_ = 0, dirTex_ = 0;
    int lightW_ = 1, lightH_ = 1;
    ugl::Target sceneT_;
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;
    void drawInst(const std::vector<Inst>& v);

    // ---- Variant (colors eased toward it).
    int variant_ = 0;
    bool palSet_ = false;
    vec3 wall_{0.f}, floor_{0.f}, rail_{0.f}, ambient_{0.f}, light_{1.f}, dropLight_{1.f}, text_{1.f}, accent_{1.f}, ghost_{1.f};
    float tintK_ = 0.5f;
    vec3 piece_[7];

    // ---- Level, drop, music.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    bool inDrop_ = false, sectionKnown_ = false, sectionDrop_ = false;
    float dropWait_ = 0.f, dropT_ = 99.f, dropAmp_ = 0.f;
    float drop_ = 0.f;    // the drop's state (0..1, eased)
    float chorus_ = 0.f;  // a peak that is not a drop
    float tetrisT_ = 99.f;
    double lastBeatSlot_ = -1.0;
    float slow_ = 0.f;
    bool slowOn_ = false;
    float time_ = 0.f, world_ = 0.f;
    float dimT_ = 0.f;
    float gaugeShown_ = 0.f;
    float side_[2] = {0.f, 0.f}; // the side lights of a peak (eased on)
    Bridge bridge_;
    Rng rng_{4242};
};
