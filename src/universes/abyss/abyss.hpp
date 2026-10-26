#pragma once
// ABYSS: the deep sea, lit only by living things (see abyss.cpp).
#include <string>
#include <vector>

#include "universe.hpp"

class AbyssUniverse : public Universe {
public:
    ~AbyssUniverse() override;
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

    // The palette of a variant (linear RGB once loaded).
    struct Palette {
        vec3 deep, water, surf, glow, glow2, snow, whale;
        vec3 piece[7];
    };

private:
    // ---- Instanced primitives.
    struct Tile { float a0[4], a1[4], a2[4], a3[4]; };   // see TILE_VS
    struct Sprite { float a0[4], a1[4], a2[4]; };        // see SPRITE_VS
    struct WVert { float x, y, u, v; };                  // the whale's mesh (see WHALE_VS)
    std::vector<Tile> tiles_;
    std::vector<Sprite> back_, mid_, front_, fx_;        // sprite layers (behind the well, in front, effects)
    std::vector<WVert> whaleMesh_;
    void tile(float x, float y, float hw, float hh, const vec3& c, float alpha, int kind, float age, int type,
              int mask, float scale = 1.f, float light = 1.f, float e2 = 0.f, float e3 = 0.f);
    // kind 0 soft dot, 1 jellyfish, 3 glowing thread (a..b), 4 photophore (a dot with a sharp core)
    void sprite(std::vector<Sprite>& out, int kind, vec2 a, vec2 b, float size, const vec3& c, float alpha,
                float p0 = 0.f, float p1 = 0.f);

    // ---- Scene parts.
    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
    void buildLife(const MusicState& music);
    void buildWhale();
    void buildBoard(const Game& game);
    void buildFx();
    void drawWorld();
    void drawSprites(const std::vector<Sprite>& v);
    void drawHud(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    int pickVariant(const SongLook& look) const;
    void spawnJelly(bool anywhere);
    void spawnSchool();
    void spawnSiphon(bool anywhere);

    // ---- Text (signed distance field glyphs).
    struct Glyph { float x0, y0, x1, y1, ox, oy, w, h, adv; };
    struct Font {
        bool ok = false;
        GLuint tex = 0;
        float px = 64.f, scaleTT = 1.f, ascent = 0.f, capH = 40.f;
        Glyph g[96];
        const unsigned char* data = nullptr;
        void* info = nullptr; // stbtt_fontinfo
    };
    Font fontSans_, fontSerif_;
    void buildFont(Font& f, const unsigned char* ttf);
    float textWidth(const Font& f, const std::string& s, float size, float track) const;
    float textQuads(const Font& f, const std::string& s, float x, float y, float size, float track,
                    std::vector<float>& out) const;
    struct TextStyle {
        vec3 color{1.f}, glow{0.f};
        float glowAmt = 0.f, alpha = 1.f, weight = 0.f, shadow = 0.6f;
    };
    void drawText(const Font& f, const std::vector<float>& quads, const TextStyle& st);

    // ---- GL.
    GLuint progWorld_ = 0, progTile_ = 0, progSprite_ = 0, progWhale_ = 0, progText_ = 0, progShape_ = 0,
           progDown_ = 0, progUp_ = 0, progPost_ = 0;
    GLuint quadVbo_ = 0, tileVao_ = 0, tileVbo_ = 0, spriteVao_ = 0, spriteVbo_ = 0, whaleVao_ = 0, whaleVbo_ = 0,
           textVao_ = 0, textVbo_ = 0, shapeVao_ = 0, shapeVbo_ = 0;
    GLuint sceneFbo_ = 0, sceneTex_ = 0;
    static constexpr int BLOOM_MIPS = 6;
    GLuint bloomFbo_[BLOOM_MIPS] = {}, bloomTex_[BLOOM_MIPS] = {};
    int bloomW_[BLOOM_MIPS] = {}, bloomH_[BLOOM_MIPS] = {};
    void createTargets();
    void destroyTargets();
    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    float px_ = 0.05f; // board units per pixel

    // ---- Song, variant, palette.
    int variant_ = 0;
    bool palSet_ = false;
    Palette pal_{};
    uint64_t seed_ = 0;
    float seedF_ = 0.f;

    // ---- Scene level: continuous 0 calm .. 2 peak.
    int level_ = 1;
    bool hasLevel_ = false;
    float lv_ = 1.f, lvFrom_ = 1.f, lvTo_ = 1.f, lvT_ = 1.f, lvDur_ = 1.f;
    float energy_ = 0.5f;

    // ---- Music-driven state.
    float worldTime_ = 0.f, beatGlow_ = 0.f, beatPh_ = 0.f, lastPh_ = 0.f;
    float prevKick_ = 0.f, kickIn_ = 0.f, sinceKick_ = 9.f, bloom_ = 0.f, flash_ = 0.f;
    float life_ = 0.f, peak_ = 0.f, rays_ = 1.f;
    float surge_ = 0.f, drive_ = 0.f, kickEnv_ = 0.f; // the drop's surge of light (decays over a few seconds)

    // ---- The deep's inhabitants.
    struct Flake { float x, y, z, phase, size; };       // marine snow (z: 0 far .. 1 near)
    std::vector<Flake> snow_;
    struct Jelly { float x, y, vx, vy, size, depth, phase, tilt, age, life; int hue; float seed; };
    std::vector<Jelly> jellies_;
    struct School { float x, y, dir, speed, age, life, turn; int n; float seed; };
    std::vector<School> schools_;
    struct Siphon { float x, y, vx, len, age, life, seed, depth; int hue; };
    std::vector<Siphon> siphons_;
    struct Whale { float t = -1.f, dur = 48.f, y = 3.f, dir = -1.f, size = 1.f, swim = 0.f; } whale_;
    float whaleCool_ = 0.f, jellyTimer_ = 0.f, schoolTimer_ = 0.f, siphonTimer_ = 0.f;

    // ---- Effects.
    struct Spark { float x, y, vx, vy, t, life, delay, size; vec3 col; float twinkle; int kind; };
    std::vector<Spark> sparks_;
    struct Dying { float x, y, t, delay, out, vx, vy; int type, mask; bool sink; };
    std::vector<Dying> dying_;
    float tetrisT_ = 99.f;

    // ---- Game state seen by the universe.
    bool slowOn_ = false;
    float slow_ = 0.f, slowT_ = 0.f;
    float lureX_ = -15.f, lureY_ = -2.f;
    float overT_ = -1.f, over_ = 0.f;
    float gaugeShown_ = 0.f, readyT_ = 9.f;
    float dimT_ = 0.f;
    Bridge bridge_;
    Rng rng_{4242};
};
