#pragma once
// SUMI: ink on washi paper (see sumi.cpp).
#include <vector>

#include "universe.hpp"

class SumiUniverse : public Universe {
public:
    ~SumiUniverse() override;
    bool init(int width, int height) override;
    void resize(int width, int height) override;
    void setSong(const SongLook& look, float seconds) override;
    void setLevel(int level, float energy, float seconds, float impact) override;
    void drop(float strength) override;
    void refresh(float strength) override;
    void onEvent(const GameEvent& ev, const Game& game) override;
    void setBonus(bool on) override;
    void levelUp() override;
    void kick(float amount) override { kick_ = std::max(kick_, amount); }
    void render(const Frame& f, GLuint fbo) override;
    vec2 project(const vec3& p) const override;
    TransitionHints hints() const override;
    int variant() const override { return variant_; }
    bool bridgeable() const override { return true; }
    void setBridge(const Bridge& b) override { bridge_ = b; }

private:
    // One instanced quad per ink mark (see SPRITE_FS for the meaning of each field by kind).
    struct Sprite { float x, y, hw, hh, rot, kind, seed, alpha, c[4], d[4]; };
    enum { K_BLOCK = 0, K_GHOST = 1, K_DROP = 2, K_STROKE = 3, K_SEAL = 4, K_ENSO = 5, K_SUN = 6, K_PETAL = 7,
           K_BLOOM = 8, K_BLOSSOM = 9, K_BEHIND = 100 };
    struct Bloom { vec2 pos; float age, life, size, inner, tide, rough, seed; vec3 col; };
    struct Petal { vec2 pos, vel; float rot, spin, size, age, life, seed, flip, flipRate; bool red; float gustVar = 1.f, speed = 1.f; };
    // The peak's plum branch: brushed limbs and shoots (painted one after another, from the paper's edge) and
    // the blossoms opening along them once the brush has passed.
    struct BranchSeg { vec2 a, b; float width, t0, dur, dry, taper, seed; };
    struct Blossom { vec2 pos; float size, rot, t0, seed; int style; }; // style 0 accent wash, 1 ink outline, 2 bud
    struct Drop { vec2 pos, vel; float size, grow, age, life, alpha, seed; vec3 col; };
    struct Stroke { vec2 a, b; float width, age, draw, hold, fade, dry, taper, dens, seed, wellFade; vec3 col; };
    struct Stamp { vec2 pos; float size, rot, age, life, seed; };
    struct Dying { int x, y, type, mask; float t, delay, dur; vec2 nc; float seed; };
    // Per board cell: which lock it came from (cells of one piece join into one brushed shape) and the origin
    // of its ink texture, so a piece keeps its brushwork when rows collapse.
    struct InkCell { int id = -1, type = -1; vec2 nc; };

    GLuint compile(const char* vs, const char* fs, const char* name);
    void createTargets();
    void destroyTargets();
    void addBlock(std::vector<Sprite>& out, vec2 pos, int type, int mask, float scale, vec2 nc, float seed,
                  float wet, float age, float dissolve, float alpha, int kind = K_BLOCK);
    void collectBoard(const Game& game, const MusicState& music, std::vector<Sprite>& out);
    void collectScenery(const MusicState& music, std::vector<Sprite>& out);
    void simulate(const Game& game, const MusicState& music, float dt, float wdt);
    void spawnBloom(vec2 pos, float size, float strength, vec3 col);
    void drawText(const std::vector<HudText>& hud, const std::vector<HudRect>& panels);
    void startWash(int level, float seconds, float impact);
    void buildBranch();
    void spawnPetal(vec2 pos, bool fromBranch);
    vec2 randomSidePoint();
    int pickVariant(const SongLook& look) const;

    GLuint progPaper_ = 0, progBg_ = 0, progSprite_ = 0, progPost_ = 0, progText_ = 0, progTextInk_ = 0;
    GLuint emptyVao_ = 0, quadVbo_ = 0, spriteVao_ = 0, spriteVbo_ = 0, textVao_ = 0, textVbo_ = 0;
    GLuint paperFbo_ = 0, paperTex_ = 0, sceneFbo_ = 0, sceneTex_ = 0, textFbo_ = 0, textTex_ = 0;

    // Scene level (0 calm, 1 mid, 2 peak): the one shown, a queued one while a wash runs.
    bool hasLevel_ = false, hasPending_ = false;
    int level_ = 1, pendingLevel_ = 1;
    float pendingDur_ = 1.f, pendingImpact_ = 0.f;
    // The wash (in-universe scene change): levels before / after, progress, its front radius in world units.
    float lvA_ = 1.f, lvB_ = 1.f, washT_ = 1.f, washDur_ = 1.f, washImpact_ = 0.f, washFront_ = -1.f, washSeed_ = 0.f;
    float lvShown_ = 1.f;            // level for the sprites (eased as the wash passes them)
    // The song's ink variant; the current colors ease toward it.
    int variant_ = 0;
    bool variantSet_ = false;
    vec3 paperCol_{0.925f, 0.890f, 0.815f};
    vec3 verm_{0.784f, 0.251f, 0.165f};
    vec3 inkC_{0.105f, 0.094f, 0.088f}, ink2C_{0.22f, 0.29f, 0.42f}, lacquer_{0.06f, 0.04f, 0.036f}, ivory_{0.925f, 0.9f, 0.845f};
    vec3 piece_[7];
    float neg_ = 0.f;
    float driftFar_ = 0, driftMid_ = 0, driftNear_ = 0;
    float ensoT_ = 0, ensoPresence_ = 0, sunPresence_ = 0, petalPresence_ = 0;
    std::vector<BranchSeg> branch_;
    std::vector<Blossom> blossoms_;
    float branchT_ = 0, branchPresence_ = 0, branchSeed_ = 0;
    float dropHeld_ = 0;   // just after the drop: the sprites show the peak even before the wash reaches them
    float dropWait_ = 0;   // rising into a drop: the enso and the branch wait for its first downbeat
    float petalBurst_ = 0; // the drop's flurry of petals, settling to the peak's steady fall
    float petalAcc_ = 0;
    float gust_ = 0;       // the drop's gust: the petals hurled down on the drop, slowing over a few seconds
    float drive_ = 0;      // a drop's peak (the song driving harder) against a chorus' one
    float skyWash_ = 0, skySwell_ = 0; // the drop's colored wash in the sky: steady, one swell on the drop
    vec3 skyCol_{0.784f, 0.251f, 0.165f};

    std::vector<Bloom> blooms_;
    std::vector<Petal> petals_;
    std::vector<Drop> drops_;
    std::vector<Stroke> strokes_;
    std::vector<Stamp> stamps_;
    std::vector<Dying> dying_;
    InkCell ink_[Game::H][Game::W];
    int pieceCounter_ = 1;
    float prevBeatPhase_ = 0;
    int beatCount_ = 0;
    float sinceBloom_ = 0;

    bool inverted_ = false;
    float invT_ = 10.f;
    float slow_ = 0;
    float gaugeShown_ = 0, readyFlash_ = 0, readyT_ = 0;
    float pauseFade_ = 0;
    float kick_ = 0;
    Bridge bridge_;

    mat4 vp_;
    vec2 viewC_{0, 0.4f}, viewH_{22, 12.4f};
    Rng rng_{12345};
};
