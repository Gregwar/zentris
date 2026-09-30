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
    enum Type { Move, Rotate, Lock, HardDrop, Clear, Hold, Spawn, TopOut } type;
    int count = 0;                 // lines cleared / rows dropped
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
    std::vector<GameEvent> events_;
};
