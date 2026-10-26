#pragma once
// WHITE ROOM: a seamless white studio, matte blocks and soft shadows (see white_room.cpp).
#include <string>
#include <vector>

#include "layout.hpp"
#include "ugl.hpp"
#include "universe.hpp"

class q3Scene;
class q3Body;

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
    // A cube instance: center, half size; x axis, alpha; y axis, plaster; albedo, kind (0 the well's, 1 thrown, 2 a
    // preview).
    struct CubeInst { float a0[4], a1[4], a2[4], a3[4]; };
    std::vector<Tile> tiles_;
    std::vector<Occ> occ_;
    std::vector<CubeInst> board_, boardFade_; // this frame's blocks, piece, previews (opaque, translucent)
    void block(float x, float y, float s, const vec3& c, float alpha, float plaster, int kind);
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
    GLuint progRoom_ = 0, progOcc_ = 0, progTile_ = 0, progShape_ = 0, progText_ = 0, progPost_ = 0, progCube_ = 0,
           progShadow_ = 0;
    GLuint quadVbo_ = 0, tileVao_ = 0, tileVbo_ = 0, occVao_ = 0, occVbo_ = 0, shapeVao_ = 0, shapeVbo_ = 0, textVao_ = 0,
           textVbo_ = 0, cubeVao_ = 0, cubeMeshVbo_ = 0, cubeVbo_ = 0;
    GLuint occFbo_ = 0, occTex_ = 0;
    // the thrown cubes seen from each lamp: depth maps, their view-projections, (on, tan of the half fov, near, far)
    static constexpr int SH_N = 1024;
    GLuint shFbo_[2] = {0, 0}, shTex_[2] = {0, 0}, shCovTex_[2] = {0, 0}, shCmpSampler_ = 0, shRawSampler_ = 0;
    mat4 shVP_[2];
    vec4 shP_[2];
    void renderShadowMaps(int count, const vec3& lo, const vec3& hi);
    void setRoomUniforms(GLuint prog);
    void drawCubes(int first, int count);
    vec4 occRect_{0, 0, 1, 1};
    float shadowK_ = 1.f;
    // the multisampled scene (color, depth), resolved into sceneT_
    GLuint msFbo_ = 0, msColor_ = 0, msDepth_ = 0;
    // the room, shaded once per pixel: its color and its depth (copied into the multisampled scene)
    GLuint roomFbo_ = 0, roomTex_ = 0, roomZTex_ = 0, progCopy_ = 0;
    int samples_ = 0;
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

    // ---- Cleared blocks thrown out of the well: cubes flying out left, right or back, landing on the floor, lying
    // there a while and fading (their physics at the real speed, like the clear).
    // They are rigid bodies (qu3e): they fly, tumble, collide, stack and come to rest on the floor, the cove and each
    // other; a cube at rest freezes (a static body: a pile costs nothing). They lie there 60 to 120 s and fade; a cube
    // gone (or knocked away) wakes the cubes it held, which settle again.
    struct Ejected {
        q3Body* body = nullptr;
        int layers = 0;            // its collision layers (see GHOST in white_room.cpp)
        int ghost = 0;             // thrown out from inside cubes lying there: its own layer, which those leave until it is clear
        float busy = 0.f;          // seconds moving since it last froze
        bool frozen = false;       // long at rest: a static body (wakes when what holds it moves)
        float still = 0.f;         // seconds still
        vec3 p0, p1;               // the last two steps' centers (drawn in between)
        vec4 q0, q1;               // ... and orientations (quaternions)
        vec3 ext;                  // half size of its bounding box
        vec3 rest;                 // where it last lay still (moving off it wakes the frozen ones it held)
        float h = 0.42f;           // half size (drawn)
        int color = 0;
        float age = 0.f, life = 1.f; // seconds; fades over its last FADE seconds
    };
    std::vector<Ejected> ejected_;
    static constexpr int EJECT_MAX = 300;
    q3Scene* world_ = nullptr;     // owned
    float physAcc_ = 0.f;          // real time not stepped yet
    void physicsInit();
    void makeBody(Ejected& e, bool dynamic, const vec3& v, const vec3& w); // (again) at its pose
    int ghostSeq_ = 0;             // the ghosts' layers, in turn
    void unfreezeAround(const Ejected& m, bool hard); // the frozen cubes it touches (above it, or all after a hit)
    void eject(float x, float y, int color, float power);
    void stepEjected(float dt);
    // how high the piles stand over the floor (x, z), from the resting cubes: they darken the floor around them
    static constexpr int HM_NX = 128, HM_NZ = 20;
    static constexpr float HM_X0 = -25.6f, HM_Z0 = -4.4f, HM_CELL = 0.4f;
    std::vector<float> hm_;
    bool hmDirty_ = true;
    GLuint hmTex_ = 0;
    void hmStamp(const vec3& p, const vec3 (&ax)[3], float h);
    std::vector<CubeInst> thrown_, thrownFade_;
    float dropSwing_ = 0.f;  // the drop's gels swinging in from far out (1 on the downbeat .. 0)

    // ---- Effects.
    struct Dying { float x, y, t, delay; int type; }; // game over: the stack turning to plaster
    std::vector<Dying> dying_;
    float press_ = 0.f;             // the last lock's press (stronger after a hard drop)
    int lastDrop_ = 0;

    // ---- Beat.
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
