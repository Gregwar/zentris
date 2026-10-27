#pragma once
// PRISM: glass cubes in the dark, crossed by slow beams of light (see prism.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class PrismUniverse : public Universe {
public:
    ~PrismUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void drop(float strength) override;
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
    struct Inst { float a0[4], a1[4], a2[4], a3[4]; };

    // ---- The glass: one box per cube (the stack's cells, the falling piece's), looked up by column.
    static constexpr int SLOTS = Game::W * Game::H + 4; // the board's cells, then the falling piece's four
    struct GBox { vec2 c; int slot, type; float seed; };
    std::vector<GBox> boxes_;
    std::vector<int> byCol_[Game::W];
    std::vector<int> loose_;  // boxes off the column grid (the falling piece moving sideways)
    float gx0_ = 0, gy0_ = 0, gx1_ = 0, gy1_ = 0; // the glass's bounds
    void buildGlass(const Game& g);
    int boxAt(vec2 p) const;
    bool hitGlass(vec2 o, vec2 d, float tMax, float& t, int& box, vec2& nrm) const;

    // ---- The light: rays traced through the glass (refraction, total internal reflection, a Fresnel reflection,
    // dispersion into a spectrum), drawn as shafts, pools on the floor and glints where they cross a face.
    struct Ray { vec2 o, d; vec3 c; float w, dw; int depth, box, bounces; float n; bool split; };
    void trace(Ray r);
    void emitShaft(vec2 a, vec2 b, float w0, float w1, vec3 c, float gain);
    void emitGlint(vec2 p, vec3 c, float size);
    std::vector<Inst> light_, glow_;
    std::vector<Ray> stack_;
    int segs_ = 0;
    vec3 acc_[SLOTS];
    vec2 accDir_[SLOTS];
    vec3 lit_[SLOTS];
    vec2 litDir_[SLOTS];
    float airK_ = 1.f;        // how bright a white shaft is in the haze (dimmer on a drop: the colors lead)
    float disp_ = 0.f;        // dispersion (0: white light through the glass .. a full spectrum fanning out)
    vec3 tintOf(int type) const { return tint_[type < 0 ? 0 : type > 6 ? 6 : type]; }

    struct Beam { float on = 0.f; };
    Beam beams_[3];

    // ---- Effects: sparks (a cleared cube shattering into spectral sparks), glints (a lock, the beat's sparkle).
    struct Spark { vec2 p, v; vec3 c; float t, life, delay, w; };
    std::vector<Spark> sparks_;
    struct Flare { vec2 p; vec3 c; float t, life, size, delay; };
    std::vector<Flare> flares_;
    void shatter(vec2 c, int type, float delay, float power);

    // ---- Play.
    std::vector<Inst> cubes_;
    void cubeInst(vec2 pos, float scale, int type, float alpha, float kind, int slot, float flash);
    void buildCubes(const Game& g);
    float lockT_[Game::H][Game::W];
    int pieceCell_[4][2] = {{-99, -99}, {-99, -99}, {-99, -99}, {-99, -99}}; // the falling piece's cells (its light slots)
    float hardK_ = 0.f;

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
    GLuint progLight_ = 0, progScene_ = 0, progCube_ = 0, progFinal_ = 0, progText_ = 0, progPanel_ = 0;
    GLuint quadVbo_ = 0, instVao_ = 0, instVbo_ = 0, textVao_ = 0, textVbo_ = 0, panelVao_ = 0, panelVbo_ = 0;
    ugl::Target lightT_, sceneT_;
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;
    void drawInst(GLuint prog, const std::vector<Inst>& v);

    // ---- Variant (colors eased toward it).
    int variant_ = 0;
    bool palSet_ = false;
    vec3 back_{0.f}, floor_{0.f}, light_a_{1.f}, light_b_{1.f}, text_{1.f}, accent_{1.f};
    vec3 tint_[7];

    // ---- Level, drop, music.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    bool inDrop_ = false, sectionKnown_ = false, sectionDrop_ = false;
    float dropWait_ = 0.f, dropT_ = 99.f, dropAmp_ = 0.f;
    float rainbow_ = 0.f;     // the drop's state (0 .. 1, eased)
    float chorus_ = 0.f;      // a peak that is not a drop
    float tetrisT_ = 99.f;
    float kickIn_ = 0.f;
    double lastHalfBeat_ = -1.0;
    float slow_ = 0.f;
    bool slowOn_ = false;
    float time_ = 0.f, world_ = 0.f;
    float dimT_ = 0.f;
    float gaugeShown_ = 0.f;
    Bridge bridge_;
    Rng rng_{31337};
};
