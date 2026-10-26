#pragma once
// KINTSUGI: dark polished stone, ceramic pieces mended with gold, gold veins spreading through the marble (see
// kintsugi.cpp).
#include <string>
#include <vector>

#include "ugl.hpp"
#include "universe.hpp"

class KintsugiUniverse : public Universe {
public:
    ~KintsugiUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void drop(float strength) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override { slowOn_ = on; }
    void levelUp() override;
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

    // A stone and its metal (linear RGB once loaded; floats only, so two palettes blend).
    struct Palette {
        vec3 stone, cloud, calcite, light, metal, molten, hot, warm, ink, dim;
        vec3 piece[7];
        float veinAmt, sheenAngle;
    };

private:
    // ---- The gold veins: polylines with an arrival time per point (when the gold reaches it).
    struct VPoint { float x, y, t, dist, w; };
    struct Vein { std::vector<VPoint> pts; float alpha = 1.f, seed = 0.f; };
    struct Net {
        std::vector<Vein> veins;
        float born = 0.f;        // world time of its first point
        float fadeAt = 1e9f;     // world time it starts to fade
        float fadeDur = 3.f;
        float heat = 0.f, heatTo = 0.f; // molten 0..1 (eased)
        int kind = 0;            // 0 mid, 1 peak (drop / chorus), 2 Tetris burst, 3 lock crack
    };
    std::vector<Net> nets_;
    void growNet(Net& n, int kind, float speed, uint64_t seed);
    void walk(Net& n, Rng& r, float x, float y, float ang, float len, float w, float t0, float speed, float dist0,
              int depth, float branchRate, float branchDelay, float branchSpeed);
    void fadeNets(int kind, float dur);
    bool hasNet(int kind) const;

    // ---- Instances.
    struct Block { float a0[4], a1[4], a2[4], a3[4]; };
    struct Sprite { float a0[4], a1[4], a2[4]; };
    std::vector<Block> blocks_;
    std::vector<Sprite> sprites_;
    std::vector<float> veinVerts_, shardVerts_;
    void buildBoard(const Game& g);
    void buildVeins(bool locks);
    void buildFx();
    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    int pickVariant(const SongLook& look) const;
    void bakeMarble();
    void setLight(GLuint prog);

    // ---- The pieces the stack is made of (which cells belong together), kept in step with the board.
    struct Owner { int type = -1, id = -1; float sx = 0, sy = 0; int base = 0; }; // base: the piece's lowest row when it locked
    Owner own_[22][10];
    int nextId_ = 1;

    // ---- Text.
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    struct Font {
        bool ok = false;
        GLuint tex = 0;
        float px = 64.f, scaleTT = 1.f, capH = 40.f;
        Glyph g[96];
        void* info = nullptr; // stbtt_fontinfo
    };
    Font fontSerif_, fontItalic_, fontCaps_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const Font& f, const std::string& s, float size, float track) const;
    void textQuads(const Font& f, const std::string& s, float x, float y, float size, float track, std::vector<float>& out) const;
    void drawText(const Font& f, const std::vector<float>& quads, const vec3& color, float alpha, float halo, float bias);

    // ---- GL.
    GLuint progMarble_ = 0, progStone_ = 0, progVein_ = 0, progBlock_ = 0, progShard_ = 0, progSprite_ = 0,
           progText_ = 0, progDown_ = 0, progUp_ = 0, progPost_ = 0, progShape_ = 0;
    GLuint quadVbo_ = 0, blockVao_ = 0, blockVbo_ = 0, spriteVao_ = 0, spriteVbo_ = 0, veinVao_ = 0, veinVbo_ = 0,
           shardVao_ = 0, shardVbo_ = 0, textVao_ = 0, textVbo_ = 0, shapeVao_ = 0, shapeVbo_ = 0;
    ugl::Target marbleT_, sceneT_;
    static constexpr int BLOOM_MIPS = 5;
    ugl::Target bloomT_[BLOOM_MIPS];
    bool marbleDirty_ = true;
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f;

    // ---- Song, variant, palette.
    int variant_ = 0;
    bool palSet_ = false;
    Palette pal_{}, cur_{};
    uint64_t seed_ = 1;
    float seedF_ = 0.f;
    int netSerial_ = 0;

    // ---- Scene level: continuous 0 calm .. 2 peak, and the drop.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;
    bool dropPending_ = false; // risen into a peak, waiting for its first downbeat
    float dropWait_ = 0.f;
    float dropness_ = 0.f;     // the whole drop: molten gold, a warm stone (eased, steady)
    float dropTarget_ = 0.f;
    float swell_ = 0.f;        // one swell on the drop, settling over seconds
    float sinceDrop_ = 99.f;

    // ---- World clock and the beat grid.
    float now_ = 0.f;
    float lastHalf_ = -1.f;
    float surgeT_ = -1.f, surgeAmp_ = 0.f;

    // ---- Effects.
    // A shard of a cleared row: one fragment of a piece between its gold seams (it may span several cells of the
    // piece), as triangles around its pivot: per vertex its offset from the pivot (board units), its point in piece
    // space, and 0 on a broken edge (a seam: gold), 1 elsewhere.
    struct Shard { std::vector<float> verts; vec2 pivot, v; float rot = 0, spin = 0, t = 0, delay = 0, life = 0; int type = 0; };
    std::vector<Shard> shards_;
    void breakRows(const GameEvent& ev, bool tetris);
    struct Leaf { vec2 p, v; float rot, spin, turn, turnRate, size, t, life, seed; };
    std::vector<Leaf> leaves_;
    struct Glint { vec2 p; float t, life, size; };
    std::vector<Glint> glints_;
    struct Dying { int x, y, type; float sx, sy, t, delay; int mask; };
    std::vector<Dying> dying_; // game over: the stack cracking and going dark
    int lastDrop_ = 0;
    void spawnLeaves(int n, vec2 at, float speed, float spread);

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f;
    float overT_ = -1.f, over_ = 0.f;
    float gaugeShown_ = 0.f;
    float dimT_ = 0.f;
    float levelGlint_ = 0.f;
    Bridge bridge_;
    Rng rng_{1313};
};
