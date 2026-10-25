#pragma once
// WATERCOLOR: wet watercolor on cold-press paper (see watercolor.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class WatercolorUniverse : public Universe {
public:
    ~WatercolorUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override;
    void levelUp() override;
    void kick(float amount) override { kickIn_ = std::max(kickIn_, amount); }
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

    // A painting (linear RGB once loaded; the pigments as the color of a full wash on white paper).
    struct Palette {
        vec3 paper, ink, skyTop, skyLow, sun, far, mid, near, tree, accent;
        vec3 piece[7];
        float horizon, reach, sunX, sunY, sunR, sunLift, sunPaint, farH, farRough, midH, midRough, nearType,
            treeType, gran, wet, body;
    };

private:
    struct CellInst { float a0[4], a1[4], a2[4], a3[4]; };
    struct LineInst { float a0[4], a1[4]; };
    struct DropInst { float a0[4], a1[4], a2[4]; };
    struct Vert { float x, y, r, g, b, a; };
    std::vector<CellInst> cells_;
    std::vector<LineInst> pencil_;
    std::vector<DropInst> drops_;   // this frame's blooms, for the wet layer
    void cell(float x, float y, float half, int mask, const vec3& K, float alpha, int kind, float wet, float seed,
              float extra = 0.f, const vec3& body = vec3(0.f), float bodyAmt = 0.f);
    void pencilLine(vec2 a, vec2 b, float width, float alpha, float seed);
    // a bloom of pigment in the sheet: kind 0 a drop, 1 a backrun, 2 spatter
    void drop(vec2 p, float r, float life, const vec3& K, float strength, int kind = 0);

    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
    void buildBoard(const Game& game);
    void drawLand();
    void drawBlooms();
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    int pickVariant(const SongLook& look) const;
    vec2 skySpot(bool big);      // somewhere in the painted sky, away from the well and the columns
    vec3 pieceK(int type) const;
    vec3 dropColor(float wp);

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

    // ---- GL.
    GLuint progPaper_ = 0, progLand_ = 0, progBloom_ = 0, progComp_ = 0, progCell_ = 0, progPencil_ = 0,
           progShape_ = 0, progText_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, cellVao_ = 0, cellVbo_ = 0, lineVao_ = 0, lineVbo_ = 0, bloomVao_ = 0, bloomVbo_ = 0,
           shapeVao_ = 0, shapeVbo_ = 0, textVao_ = 0, textVbo_ = 0, noiseTex_ = 0;
    ugl::Target paperT_, landT_, wetT_, sceneT_;
    void createTargets();
    void buildNoise();
    void bindNoise(GLuint prog, int unit);
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;

    // ---- Song, variant, palette.
    int variant_ = 0;
    bool palSet_ = false;
    Palette pal_{};
    uint64_t seed_ = 0;
    float seedF_ = 0.f;
    vec4 backruns_[3];  // the painting's own backruns in the sky

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;

    // ---- Music.
    float worldTime_ = 0.f;
    double lastBeat_ = -1.0;
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f;
    int beatCount_ = 0;

    // ---- Effects.
    struct Lifted { float x, y, t, delay; int type; };       // a cleared cell being lifted
    std::vector<Lifted> lifted_;
    struct Running { float x, y, t, delay; int type; float seed; }; // game over: the stack running down
    std::vector<Running> running_;
    struct Dot { vec2 p; float r, t, life; vec3 K; };        // spatter on the well's paper
    struct Bloom { vec2 p; float r, t, life; vec3 K; float strength, seed; int kind; };
    std::vector<Bloom> blooms_;
    std::vector<Dot> dots_;
    float flood_ = 0.f, floodT_ = 9.f;
    vec3 floodK_{0.f};

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    float overT_ = -1.f, run_ = 0.f;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    Bridge bridge_;
    Rng rng_{909};
};
