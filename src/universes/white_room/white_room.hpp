#pragma once
// WHITE ROOM: a seamless white studio, matte blocks and soft shadows (see white_room.cpp).
#include <string>
#include <vector>

#include "layout.hpp"
#include "ugl.hpp"
#include "universe.hpp"

class WhiteRoomUniverse : public Universe {
public:
    ~WhiteRoomUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void drop(float strength) override;
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

    // A room (linear RGB once loaded; floats only, so two rooms blend).
    struct Palette {
        vec3 wall, panel, ink, inkLight, accent, key, warm, amb, gelA, gelB, plaster;
        vec3 piece[7];
        float exposure; // the dark room is lit harder
    };

private:
    struct Tile { float a0[4], a1[4], a2[4], a3[4]; };
    struct Occ { float a[4]; };
    struct Vert { float x, y, r, g, b, a; };
    std::vector<Tile> tiles_;
    std::vector<Occ> occ_;
    void tile(float x, float y, float hw, float hh, const vec3& c, float alpha, int kind, float e0 = 1.f, float e1 = 0.f,
              float e2 = 0.f, float e3 = 0.f);

    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
    void buildBoard(const Game& game);
    void computeLights(const MusicState& music);
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    void setLightUniforms(GLuint prog);
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
    void drawText(const Font& f, const std::vector<float>& quads, const vec3& color, float alpha, float bias);

    // ---- GL.
    GLuint progRoom_ = 0, progOcc_ = 0, progTile_ = 0, progShape_ = 0, progText_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, tileVao_ = 0, tileVbo_ = 0, occVao_ = 0, occVbo_ = 0, shapeVao_ = 0, shapeVbo_ = 0, textVao_ = 0,
           textVbo_ = 0;
    GLuint occFbo_ = 0, occTex_ = 0;
    static constexpr int OCC_N = 384;      // the occupancy map's texels (square)
    static constexpr float OCC_SIZE = 25.6f; // board units it covers (centered on the board)
    ugl::Target sceneT_;
    void createTargets();
    layout::Camera cam_;
    mat4 vp_;
    float px_ = 0.05f;

    // ---- Song, variant, palette.
    int variant_ = 0;
    bool palSet_ = false;
    Palette pal_{};
    uint64_t seed_ = 0;

    // ---- Scene level: continuous 0 calm .. 2 peak; drive: how much the peak is a drop (vs a chorus).
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;
    float drive_ = 0.f, driveTarget_ = 0.f;
    float side_ = 1.f, sideTo_ = 1.f;   // the key's side (a new section swings it across the room)
    float swell_ = 0.f;                 // the gels flaring in on the drop, settling over seconds
    float sinceDrop_ = 99.f;

    // ---- This frame's light.
    struct Light { vec3 pos, col; float radius, side; }; // a lamp: position (board space), color, radius
    Light l0_{}, l1_{};
    vec3 amb_{0.5f}, ambB_{0.5f};
    vec4 pool_{0, 0, 14, 0.1f};
    float wallLum_ = 0.8f;
    float shadowAmt_ = 0.6f;    // how dark the shadow play is

    // ---- The balls on the floor.
    struct Ball {
        float x, z, h, vx, vy, r;   // h: height of its bottom above the floor
        int color;                  // 0..6 a piece's color, 7 white, 8 the accent
        float sq = 1.f, sqv = 0.f;  // squash (y scale) and its spring
        bool leaving = false;
        float age = 0.f;
    };
    std::vector<Ball> balls_;
    float spawnAcc_ = 0.f;
    float rainT_ = 99.f;            // since the drop's rain started
    void spawnBall(bool fromAbove, int color, float hFrom, float vyFrom);
    vec3 ballColor(const Ball& b) const;
    int wantedBalls() const;

    // ---- Effects.
    struct Dying { float x, y, t, delay; int type; bool over; };
    std::vector<Dying> dying_;
    float press_ = 0.f;             // the last lock's press (stronger after a hard drop)
    int lastDrop_ = 0;

    // ---- Beat.
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f;
    double beat_ = 0.0;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    float overT_ = -1.f, over_ = 0.f;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    float worldTime_ = 0.f;
    Bridge bridge_;
    Rng rng_{1111};
};
