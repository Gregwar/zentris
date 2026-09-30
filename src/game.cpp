#include "game.hpp"

#include <algorithm>
#include <cstring>

namespace {

// Spawn orientation, y-down, inside an n x n box.
struct Shape { int n; int cells[4][2]; };
const Shape SHAPES[PIECE_COUNT] = {
    {4, {{0, 1}, {1, 1}, {2, 1}, {3, 1}}}, // I
    {2, {{0, 0}, {1, 0}, {0, 1}, {1, 1}}}, // O
    {3, {{1, 0}, {0, 1}, {1, 1}, {2, 1}}}, // T
    {3, {{1, 0}, {2, 0}, {0, 1}, {1, 1}}}, // S
    {3, {{0, 0}, {1, 0}, {1, 1}, {2, 1}}}, // Z
    {3, {{0, 0}, {0, 1}, {1, 1}, {2, 1}}}, // J
    {3, {{2, 0}, {0, 1}, {1, 1}, {2, 1}}}, // L
};

// SRS kicks in y-up convention (standard tables); index [fromState][dir], dir 0 = CW, 1 = CCW.
const int KICKS_JLSTZ[4][2][5][2] = {
    {{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}, {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}},     // 0->R, 0->L
    {{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}, {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}},         // R->2, R->0
    {{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}, {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}},     // 2->L, 2->R
    {{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}, {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}},   // L->0, L->2
};
const int KICKS_I[4][2][5][2] = {
    {{{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}, {{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}},     // 0->R, 0->L
    {{{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}, {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}},     // R->2, R->0
    {{{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}, {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}},     // 2->L, 2->R
    {{{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}, {{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}},     // L->0, L->2
};

} // namespace

Game::Game() { reset(1); }

void Game::reset(uint64_t seed) {
    for (auto& row : board_)
        for (auto& c : row) c = Cell{};
    rng_ = Rng(seed);
    bag_.clear();
    next_.clear();
    for (int i = 0; i < 5; i++) next_.push_back(drawFromBag());
    holdType_ = -1;
    holdUsed_ = false;
    score_ = lines_ = 0;
    combo_ = -1;
    active_ = false;
    spawnDelay_ = 0.4f;
    std::fill(std::begin(rowOffset_), std::end(rowOffset_), 0.f);
}

int Game::drawFromBag() {
    if (bag_.empty()) {
        for (int i = 0; i < PIECE_COUNT; i++) bag_.push_back(i);
        for (size_t i = bag_.size(); i > 1; i--) std::swap(bag_[i - 1], bag_[rng_.next() % i]);
    }
    int t = bag_.back();
    bag_.pop_back();
    return t;
}

void Game::pieceCells(const Piece& p, int out[4][2]) const {
    const Shape& s = SHAPES[p.type];
    for (int i = 0; i < 4; i++) {
        int x = s.cells[i][0], y = s.cells[i][1];
        for (int r = 0; r < ((p.rot % 4) + 4) % 4; r++) { // rotate CW in y-down space
            int nx = s.n - 1 - y, ny = x;
            x = nx;
            y = ny;
        }
        out[i][0] = p.x + x;
        out[i][1] = p.y + y;
    }
}

bool Game::collides(const Piece& p) const {
    int c[4][2];
    pieceCells(p, c);
    for (auto& q : c) {
        if (q[0] < 0 || q[0] >= W || q[1] >= H) return true;
        if (q[1] >= 0 && board_[q[1]][q[0]].type >= 0) return true;
    }
    return false;
}

int Game::ghostY() const {
    Piece p = cur_;
    while (!collides(p)) p.y++;
    return p.y - 1;
}

int Game::stackHeight() const {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (board_[y][x].type >= 0) return H - y;
    return 0;
}

void Game::spawn(int type) {
    cur_ = Piece{type, 0, type == O ? 4 : 3, 0};
    if (collides(cur_)) {
        // Top out: the board dissolves (renderer) and the game ends until restart().
        GameEvent ev{GameEvent::TopOut};
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (board_[y][x].type >= 0) ev.cells.push_back({x, y, board_[y][x].type});
        events_.push_back(ev);
        for (auto& row : board_)
            for (auto& c : row) c = Cell{};
        best_ = std::max(best_, score_);
        over_ = true;
        active_ = false;
        return;
    }
    // Drop into view immediately if possible.
    Piece p = cur_;
    p.y++;
    if (!collides(p)) cur_ = p;
    active_ = true;
    grounded_ = false;
    lockTimer_ = 0;
    lockResets_ = 0;
    visX_ = (float)cur_.x;
    visY_ = (float)cur_.y;
    events_.push_back({GameEvent::Spawn});
}

void Game::onMoved() {
    Piece below = cur_;
    below.y++;
    bool g = collides(below);
    if (g && lockResets_ < MAX_RESETS) {
        lockTimer_ = 0;
        if (grounded_) lockResets_++;
    }
    grounded_ = g;
}

bool Game::tryPlace(const Piece& p) {
    if (collides(p)) return false;
    cur_ = p;
    return true;
}

bool Game::move(int dx) {
    if (!active_) return false;
    Piece p = cur_;
    p.x += dx;
    if (!tryPlace(p)) return false;
    onMoved();
    GameEvent ev{GameEvent::Move};
    ev.x = (float)cur_.x;
    events_.push_back(ev);
    return true;
}

bool Game::rotate(int dir) {
    if (!active_ || cur_.type == O) return false;
    int from = cur_.rot;
    int d = dir > 0 ? 0 : 1;
    const auto& kicks = cur_.type == I ? KICKS_I : KICKS_JLSTZ;
    for (int k = 0; k < 5; k++) {
        Piece p = cur_;
        p.rot = (from + (dir > 0 ? 1 : 3)) % 4;
        p.x += kicks[from][d][k][0];
        p.y -= kicks[from][d][k][1]; // table is y-up
        if (tryPlace(p)) {
            visX_ = (float)cur_.x;
            visY_ = (float)cur_.y;
            onMoved();
            GameEvent ev{GameEvent::Rotate};
            ev.x = (float)cur_.x;
            ev.count = dir;
            events_.push_back(ev);
            return true;
        }
    }
    return false;
}

bool Game::softDrop() {
    if (!active_) return false;
    Piece p = cur_;
    p.y++;
    if (!tryPlace(p)) return false;
    score_ += 1;
    onMoved();
    return true;
}

void Game::hardDrop() {
    if (!active_) return;
    int gy = ghostY();
    int dist = gy - cur_.y;
    cur_.y = gy;
    score_ += 2 * dist;
    GameEvent ev{GameEvent::HardDrop};
    ev.count = dist;
    ev.x = (float)cur_.x;
    int c[4][2];
    pieceCells(cur_, c);
    for (auto& q : c) ev.cells.push_back({q[0], q[1], cur_.type});
    events_.push_back(ev);
    visY_ = (float)cur_.y;
    lockPiece();
}

void Game::hold() {
    if (!active_ || holdUsed_) return;
    int t = cur_.type;
    if (holdType_ < 0) {
        holdType_ = t;
        int n = next_.front();
        next_.pop_front();
        next_.push_back(drawFromBag());
        spawn(n);
    } else {
        int h = holdType_;
        holdType_ = t;
        spawn(h);
    }
    holdUsed_ = true;
    events_.push_back({GameEvent::Hold});
}

void Game::gravityStep() {
    if (!active_) return;
    Piece p = cur_;
    p.y++;
    if (tryPlace(p)) onMoved();
}

void Game::lockPiece() {
    int c[4][2];
    pieceCells(cur_, c);
    GameEvent lockEv{GameEvent::Lock};
    float mx = 0;
    for (auto& q : c) {
        if (q[1] >= 0) board_[q[1]][q[0]] = Cell{(int8_t)cur_.type, 0.35f};
        lockEv.cells.push_back({q[0], q[1], cur_.type});
        mx += q[0];
    }
    lockEv.x = mx / 4;
    events_.push_back(lockEv);
    active_ = false;
    holdUsed_ = false;

    // Line clears.
    std::vector<int> full;
    for (int y = 0; y < H; y++) {
        bool f = true;
        for (int x = 0; x < W && f; x++) f = board_[y][x].type >= 0;
        if (f) full.push_back(y);
    }
    if (!full.empty()) {
        GameEvent ev{GameEvent::Clear};
        ev.count = (int)full.size();
        ev.x = lockEv.x;
        for (int y : full)
            for (int x = 0; x < W; x++) ev.cells.push_back({x, y, board_[y][x].type});
        // Compact rows, tracking how far each surviving row falls (for smooth animation).
        Cell nb[H][W];
        float off[H] = {};
        int dst = H - 1;
        for (int y = H - 1; y >= 0; y--) {
            if (std::find(full.begin(), full.end(), y) != full.end()) continue;
            std::memcpy(nb[dst], board_[y], sizeof(board_[y]));
            off[dst] = rowOffset_[y] + (float)(dst - y);
            dst--;
        }
        for (; dst >= 0; dst--) {
            for (int x = 0; x < W; x++) nb[dst][x] = Cell{};
            off[dst] = 0;
        }
        std::memcpy(board_, nb, sizeof(board_));
        std::memcpy(rowOffset_, off, sizeof(off));
        static const int pts[5] = {0, 100, 300, 500, 800};
        combo_++;
        score_ += pts[std::min(4, (int)full.size())] + 50 * combo_;
        lines_ += (int)full.size();
        best_ = std::max(best_, score_);
        events_.push_back(ev);
        spawnDelay_ = 0.5f;
        fallHold_ = 0.3f; // let the cleared blocks dissolve before the stack settles
    } else {
        combo_ = -1;
        spawnDelay_ = 0.06f;
    }
}

void Game::restart() {
    for (auto& row : board_)
        for (auto& c : row) c = Cell{};
    std::fill(std::begin(rowOffset_), std::end(rowOffset_), 0.f);
    score_ = lines_ = 0;
    combo_ = -1;
    holdType_ = -1;
    holdUsed_ = false;
    active_ = false;
    over_ = false;
    spawnDelay_ = 0.6f;
}

void Game::update(float dt) {
    if (over_) return;
    fallHold_ = std::max(0.f, fallHold_ - dt);
    for (int y = 0; y < H; y++) {
        if (fallHold_ <= 0) rowOffset_[y] = approach(rowOffset_[y], 0.f, 7.f, dt);
        if (rowOffset_[y] < 0.001f) rowOffset_[y] = 0;
        for (int x = 0; x < W; x++) board_[y][x].flash = std::max(0.f, board_[y][x].flash - dt * 0.9f);
    }
    if (!active_) {
        spawnDelay_ -= dt;
        if (spawnDelay_ <= 0) {
            int n = next_.front();
            next_.pop_front();
            next_.push_back(drawFromBag());
            spawn(n);
        }
        return;
    }
    visX_ = (float)cur_.x; // sideways moves are instant (smoothing them reads as input lag)
    visY_ = approach(visY_, (float)cur_.y, 22.f, dt);

    Piece below = cur_;
    below.y++;
    grounded_ = collides(below);
    if (grounded_) {
        lockTimer_ += dt;
        if (lockTimer_ >= lockDelay()) lockPiece();
    } else {
        lockTimer_ = 0;
    }
}

// ---- Autoplay: classic weighted heuristic (Pierre Dellacherie-lite).
bool Game::planMove(int& bestRot, int& bestX) const {
    if (!active_) return false;
    double bestScore = -1e18;
    bool found = false;
    for (int rot = 0; rot < 4; rot++) {
        for (int x = -2; x < W; x++) {
            Piece p = cur_;
            p.rot = rot;
            p.x = x;
            p.y = 0;
            if (collides(p)) continue;
            while (true) {
                Piece q = p;
                q.y++;
                if (collides(q)) break;
                p = q;
            }
            int8_t b[H][W];
            for (int yy = 0; yy < H; yy++)
                for (int xx = 0; xx < W; xx++) b[yy][xx] = board_[yy][xx].type >= 0;
            int c[4][2];
            pieceCells(p, c);
            double landing = 0;
            for (auto& q : c) {
                if (q[1] >= 0) b[q[1]][q[0]] = 1;
                landing += H - q[1];
            }
            int cleared = 0;
            for (int yy = 0; yy < H; yy++) {
                bool f = true;
                for (int xx = 0; xx < W; xx++) f = f && b[yy][xx];
                if (f) {
                    cleared++;
                    for (int k = yy; k > 0; k--) std::memcpy(b[k], b[k - 1], W);
                    std::memset(b[0], 0, W);
                }
            }
            int heights[W], holes = 0, agg = 0, bump = 0, wells = 0;
            for (int xx = 0; xx < W; xx++) {
                heights[xx] = 0;
                for (int yy = 0; yy < H; yy++)
                    if (b[yy][xx]) { heights[xx] = H - yy; break; }
                for (int yy = H - heights[xx] + 1; yy < H; yy++)
                    if (!b[yy][xx]) holes++;
                agg += heights[xx];
            }
            for (int xx = 0; xx + 1 < W; xx++) bump += std::abs(heights[xx] - heights[xx + 1]);
            for (int xx = 0; xx < W; xx++) {
                int l = xx > 0 ? heights[xx - 1] : 99, r = xx < W - 1 ? heights[xx + 1] : 99;
                int d = std::min(l, r) - heights[xx];
                if (d > 2) wells += d;
            }
            double s = -0.51 * agg + 0.76 * cleared * cleared - 0.36 * holes * 3 - 0.18 * bump - 0.1 * wells -
                       0.05 * landing;
            if (s > bestScore) {
                bestScore = s;
                bestRot = rot;
                bestX = x;
                found = true;
            }
        }
    }
    return found;
}
