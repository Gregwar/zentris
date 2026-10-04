#pragma once
// Tetris rules: SRS rotation with wall kicks, 7-bag, hold, ghost, lock delay.
// Topping out dissolves the board and ends the game; restart() starts a new one (best score is kept).
#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

#include "mathutil.hpp"

enum PieceType { I = 0, O, T, S, Z, J, L, PIECE_COUNT };

struct Cell {
    int8_t type = -1;   // -1 empty, else PieceType
    float flash = 0.f;  // lock flash, decays to 0
};

struct GameEvent {
    enum Type { Move, Rotate, Lock, HardDrop, Clear, Hold, Spawn, TopOut, BonusReady, BonusStart, BonusEnd } type;
    int count = 0;                 // lines cleared / rows dropped
    int points = 0;                // Clear: points scored; for a Tetris, the Tetris bonus included
    int bonus = 0;                 // Clear: Tetris bonus points (doubled back-to-back)
    bool backToBack = false;       // Clear: a Tetris right after another Tetris
    float x = 0;                   // mean column, for stereo pan
    struct CellInfo { int x, y, type; };
    std::vector<CellInfo> cells;   // cells involved (locked cells, cleared cells, dissolved cells)
};

struct Piece {
    int type = 0, rot = 0, x = 3, y = 0;
};

class Game {
public:
    static constexpr int W = 10, H = 22, HIDDEN = 2;

    Game();
    void reset(uint64_t seed);

    // Controls. Return true when something changed.
    bool move(int dx);
    bool rotate(int dir); // +1 CW, -1 CCW
    bool softDrop();      // one row
    void hardDrop();
    void hold();
    // Gravity tick, driven by the music's beat.
    void gravityStep();
    // Timers (lock delay, spawn delay) and visual smoothing.
    void update(float dt);
    // Game over: the board has been cleared (the renderer dissolves it) and play waits for restart().
    bool over() const { return over_; }
    void restart();
    // Testing (B key): fill the bonus gauge.
    void debugChargeBonus() {
        if (bonusActive_ || over_ || bonusLines_ >= BONUS_LINES) return;
        bonusLines_ = BONUS_LINES;
        events_.push_back({GameEvent::BonusReady});
    }
    // Testing (scene reviews): replaces the board with a mid-game stack that uses every piece color, with a
    // well so no row is full, a held piece and a fresh piece at the top.
    void debugFillBoard(uint64_t seed);
    // Debugging: add the lines needed to reach the next level.
    void skipToNextLevel() {
        if (level() < MAX_LEVEL) lines_ += LINES_PER_LEVEL - lines_ % LINES_PER_LEVEL;
    }

    // Queries
    const Cell& cell(int x, int y) const { return board_[y][x]; }
    bool hasPiece() const { return active_; }
    const Piece& piece() const { return cur_; }
    void pieceCells(const Piece& p, int out[4][2]) const;
    int ghostY() const;
    int holdType() const { return holdType_; }
    bool holdUsed() const { return holdUsed_; }
    int next(int i) const { return next_[i]; }
    float rowOffset(int y) const { return rowOffset_[y]; }
    float pieceVisualY() const { return visY_; }
    float pieceVisualX() const { return visX_; }
    float lockProgress() const { return grounded_ ? lockTimer_ / lockDelay() : 0.f; }
    bool collides(const Piece& p) const;
    int stackHeight() const;

    int score() const { return score_; }
    int lastScore() const { return score_; } // final score while the game is over
    int lines() const { return lines_; }
    // Level: 1 + one per 20 lines, plateau at MAX_LEVEL. difficulty() is 0 at level 1 .. 1 at the plateau.
    static constexpr int MAX_LEVEL = 20, LINES_PER_LEVEL = 20;
    int level() const { return std::min(MAX_LEVEL, 1 + lines_ / LINES_PER_LEVEL); }
    float difficulty() const { return (level() - 1) / float(MAX_LEVEL - 1); }
    int best() const { return best_; }
    int combo() const { return combo_; }

    // Slow-motion bonus: each cleared line charges one step of a gauge (BONUS_LINES steps to fill it; a
    // Tetris charges one extra step, so 5). Once full, activateBonus()
    // starts it: the gauge drains over BONUS_SECONDS (meanwhile the app runs gravity at x0.25; the song
    // keeps its speed). Lines cleared during the bonus don't charge it.
    static constexpr int BONUS_LINES = 12;
    static constexpr float BONUS_SECONDS = 10.f;
    // Gauge level 0..1 (while active: the time left).
    float bonusGauge() const {
        return bonusActive_ ? bonusLeft_ / BONUS_SECONDS : std::min(1.f, bonusLines_ / (float)BONUS_LINES);
    }
    bool bonusReady() const { return !bonusActive_ && !over_ && bonusLines_ >= BONUS_LINES; }
    bool bonusActive() const { return bonusActive_; }
    float bonusLeft() const { return bonusActive_ ? bonusLeft_ : 0.f; }
    bool activateBonus();
    // Tetris bonus on top of the line score, doubled for back-to-back Tetrises.
    static constexpr int TETRIS_BONUS = 400;
    static constexpr int TETRIS_EXTRA_STEPS = 1; // a Tetris charges 4 + 1 gauge steps

    std::vector<GameEvent>& events() { return events_; }

    // Autoplay helper: best (rot, x) for the current piece.
    bool planMove(int& rot, int& x) const;

private:
    // Lock delay shortens with the level (0.55 s at level 1, 0.35 s at the plateau, level 20).
    float lockDelay() const { return 0.55f - 0.2f * difficulty(); }
    static constexpr int MAX_RESETS = 15;

    void spawn(int type);
    void lockPiece();
    int drawFromBag();
    void onMoved();
    bool tryPlace(const Piece& p);

    Cell board_[H][W];
    Piece cur_;
    bool active_ = false;
    int holdType_ = -1;
    bool holdUsed_ = false;
    std::deque<int> next_;
    std::vector<int> bag_;
    Rng rng_{1};
    float lockTimer_ = 0;
    int lockResets_ = 0;
    bool grounded_ = false;
    float spawnDelay_ = 0;
    float rowOffset_[H] = {};
    float fallHold_ = 0;
    bool over_ = false;
    float visX_ = 0, visY_ = 0;
    int score_ = 0, lines_ = 0, best_ = 0, combo_ = -1;
    int bonusLines_ = 0; // gauge steps charged
    bool bonusActive_ = false, lastWasTetris_ = false;
    float bonusLeft_ = 0;
    void endBonus();
    std::vector<GameEvent> events_;
};
