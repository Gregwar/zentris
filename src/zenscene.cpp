// zenscene: a universe viewer. The computer plays (blocks keep falling) while the menu on the left picks the
// universe and its variant, the song phase (section, level) and the transition used for the next change of
// universe, so universes and the transitions between them can be looked at without a song. "Send to dashboard"
// makes a scene review thread (tools/scenereview.py) out of the universe shown and a comment.
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "game.hpp"
#include "library.hpp"
#include "platform.hpp"
#include "renderer.hpp"
#include "songplan.hpp"

namespace {

enum { ROW_UNIVERSE, ROW_VARIANT, ROW_SECTION, ROW_LEVEL, ROW_TRANSITION, ROWS };
const char* ROW_NAMES[ROWS] = {"UNIVERSE", "VARIANT", "SECTION", "LEVEL", "TRANSITION"};
const char* LEVEL_NAMES[3] = {"CALM", "MID", "PEAK"};

struct Options {
    std::string universe; // NAME[:VARIANT]
    int width = 1600, height = 900;
    std::string shot; // --shot OUT.png: render a few seconds, save a screenshot, exit
    float shotAfter = 8.f;
    std::string reviewShot; // --review-shot OUT.png: the same without the menu, for the dashboard (prints [review])
    int select = 0;
    int section = -1, level = -1;
};

class SceneViewer {
public:
    explicit SceneViewer(Options o) : opt_(std::move(o)) {}
    int run();

private:
    void show(bool transition);
    void change(int row, int dir);
    std::string rowValue(int row) const;
    std::string spec() const;
    int rowAt(double mx, double my) const;
    void autoplay(float dt);
    void updateMusic(float dt);
    std::vector<HudText> buildHud();
    void onKey(int key, int action, int mods);
    void onMouse(int button, int action);
    void onScroll(double dy);
    void onChar(unsigned cp);
    void sendToDashboard();
    bool overButton(double mx, double my) const;
    void tetris();

    Options opt_;
    GLFWwindow* win_ = nullptr;
    Renderer R_;
    Game game_;
    MusicState music_;
    std::mt19937_64 rng_{std::random_device{}() ^ (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count()};

    int universe_ = 0, variant_ = 0;
    int transition_ = -1;        // -1: picked from the universes' hints
    uint64_t seed_ = 1;
    static constexpr float BPM = 120.f; // the steady beat standing in for a song
    int section_ = SEG_VERSE, level_ = 1;
    int sel_ = 0, hover_ = -1;
    float s_ = 1.f;             // HUD scale
    double time_ = 0;
    // Autoplay and gravity.
    bool planned_ = false;
    int planRot_ = 0, planX_ = 0;
    float botTimer_ = 0, gravAcc_ = 0, overTime_ = 0, kickEnv_ = 0;
    double lastBeat_ = 0;
    // Dashboard thread: the comment being typed, and the state of the last send (0 idle, 1 sending, 2 sent, 3 failed).
    bool typing_ = false, skipChar_ = false, overButton_ = false, menu_ = true;
    double mx_ = -1, my_ = -1; // cursor (framebuffer pixels)
    std::string comment_;
    std::shared_ptr<std::atomic<int>> send_ = std::make_shared<std::atomic<int>>(0);
    int sendSeen_ = 0;
    float toast_ = 0;
    std::string toastText_;
};

float levelEnergy(int level) { return level == 2 ? 0.9f : (level == 1 ? 0.55f : 0.25f); }

std::string SceneViewer::spec() const {
    const UniverseEntry& e = universes::entry(universe_);
    return std::string(e.name) + (e.variants.empty() ? "" : ":" + e.variants[std::clamp(variant_, 0, (int)e.variants.size() - 1)]);
}

// Shows the universe and variant of the menu: at once, or with the menu's transition (on the next downbeat).
void SceneViewer::show(bool transition) {
    SongLook look = makeSongLook(nullptr, seed_);
    look.variant = variant_;
    if (transition) {
        Renderer::Change c;
        c.transition = transition_;
        R_.changeUniverse(universe_, look, level_, levelEnergy(level_), c);
    } else {
        R_.setUniverse(universe_, look, level_, levelEnergy(level_));
    }
    std::printf("[zenscene] %s  |  %s %s\n  zentris --universe '%s'\n", R_.label().c_str(), segmentName(section_),
                LEVEL_NAMES[level_], spec().c_str());
    std::fflush(stdout);
}

std::string SceneViewer::rowValue(int row) const {
    const UniverseEntry& e = universes::entry(universe_);
    switch (row) {
    case ROW_UNIVERSE: return std::string(e.name) + (e.hidden ? " (TEST)" : "");
    case ROW_VARIANT: return e.variants.empty() ? "-" : e.variants[std::clamp(variant_, 0, (int)e.variants.size() - 1)];
    case ROW_SECTION: return segmentName(section_);
    case ROW_LEVEL: return LEVEL_NAMES[level_];
    case ROW_TRANSITION: return transition_ < 0 ? "AUTO" : transitions::name(transition_);
    default: return "";
    }
}

void SceneViewer::change(int row, int dir) {
    switch (row) {
    case ROW_UNIVERSE:
        universe_ = (universe_ + dir + universes::count()) % universes::count();
        variant_ = 0;
        show(true);
        break;
    case ROW_VARIANT: {
        const int n = std::max<int>(1, (int)universes::entry(universe_).variants.size());
        variant_ = (variant_ + dir + n) % n;
        show(true);
        break;
    }
    case ROW_SECTION: {
        section_ = (section_ + dir + SEG_COUNT) % SEG_COUNT;
        // Each section starts at its usual level (the level row still overrides it).
        const int lv[SEG_COUNT] = {0, 1, 1, 2, 2, 0, 0};
        const bool rising = lv[section_] > level_;
        level_ = lv[section_];
        R_.setLevel(level_, levelEnergy(level_), rising ? 1.8f : 4.5f, rising && level_ == 2 ? 1.f : 0.5f);
        break;
    }
    case ROW_LEVEL: {
        const int old = level_;
        level_ = (level_ + dir + 3) % 3;
        R_.setLevel(level_, levelEnergy(level_), level_ > old ? 1.8f : 4.5f, level_ == 2 ? 1.f : 0.5f);
        break;
    }
    case ROW_TRANSITION:
        transition_ = (transition_ + 1 + dir + transitions::count() + 1) % (transitions::count() + 1) - 1;
        break;
    }
}

// A Tetris on the bottom rows (to look at the celebration).
void SceneViewer::tetris() {
    GameEvent ev{GameEvent::Clear};
    ev.count = 4;
    for (int y = 18; y < 22; y++)
        for (int x = 0; x < Game::W; x++) ev.cells.push_back({x, y, (x + y) % 7});
    ev.points = 1200;
    R_.onEvent(ev, game_);
}

// Menu geometry (pixels).
constexpr float MENU_X = 16, MENU_TOP = 20, MENU_FIRST = 64, ROW_H = 24, MENU_W = 344, VALUE_X = 128;
constexpr int KEYS = 9;
constexpr float BUTTON_Y = MENU_FIRST + ROWS * ROW_H + 10 + KEYS * 17 + 12, BUTTON_H = 26, COMMENT_COLS = 40;

bool SceneViewer::overButton(double mx, double my) const {
    return mx >= (MENU_X + 12) * s_ && mx <= (MENU_X + MENU_W - 12) * s_ && my >= BUTTON_Y * s_ &&
           my <= (BUTTON_Y + BUTTON_H) * s_;
}

// Splits text into lines of at most n characters, at spaces when possible.
std::vector<std::string> wrap(const std::string& text, size_t n) {
    std::vector<std::string> lines;
    std::string rest = text;
    while (rest.size() > n) {
        size_t cut = rest.rfind(' ', n);
        if (cut == std::string::npos || cut == 0) cut = n;
        lines.push_back(rest.substr(0, cut));
        rest = rest.substr(cut == n ? cut : cut + 1);
    }
    lines.push_back(rest);
    return lines;
}

int SceneViewer::rowAt(double mx, double my) const {
    if (mx < MENU_X * s_ || mx > (MENU_X + MENU_W) * s_) return -1;
    const int r = (int)std::floor((my - MENU_FIRST * s_) / (ROW_H * s_));
    return r >= 0 && r < ROWS ? r : -1;
}

std::vector<HudText> SceneViewer::buildHud() {
    int w, h;
    glfwGetFramebufferSize(win_, &w, &h);
    s_ = std::max(1.f, h / 900.f);
    const float s = s_;
    std::vector<HudText> hud;
    std::vector<HudRect> panels;
    // Bottom left: the universe and its variant (the game shows them there too).
    hud.push_back({R_.label(), 28 * s, h - 50.f * s, 1.3f * s, 0.6f, false, false, HUD_UNIVERSE});
    if (R_.changing()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s %3.0f%%", R_.transitionName(), 100.f * R_.changeProgress());
        hud.push_back({buf, 28 * s, h - 30.f * s, 1.2f * s, 0.6f, true});
    }
    if (!menu_) { // dashboard snapshots: the universe only
        R_.setPanels(std::move(panels));
        return hud;
    }
    const std::vector<std::string> lines = wrap(asciiFold(comment_) + "_", (size_t)COMMENT_COLS);
    const float commentY = BUTTON_Y + BUTTON_H + 12;
    float panelH = BUTTON_Y + BUTTON_H + 12 - MENU_TOP + 8;
    if (typing_) panelH += lines.size() * 16 + 24;
    if (toast_ > 0) panelH += 20;
    panels.push_back({MENU_X * s, MENU_TOP * s - 8 * s, MENU_W * s, panelH * s, 0.62f});
    hud.push_back({"ZENSCENE", (MENU_X + 12) * s, MENU_TOP * s + 4 * s, 2.2f * s, 0.95f, true});
    for (int r = 0; r < ROWS; r++) {
        const float y = (MENU_FIRST + r * ROW_H) * s;
        if (r == ROW_SECTION) panels.push_back({(MENU_X + 12) * s, y - 4 * s, (MENU_W - 24) * s, 1.f * s, 0.25f, true});
        if (r == sel_) panels.push_back({MENU_X * s, y - 2 * s, 4 * s, (ROW_H - 4) * s, 0.9f, true});
        if (r == sel_ || r == hover_) panels.push_back({(MENU_X + 6) * s, y - 2 * s, (MENU_W - 12) * s, (ROW_H - 4) * s, r == sel_ ? 0.16f : 0.08f, true});
        hud.push_back({ROW_NAMES[r], (MENU_X + 12) * s, y + 3 * s, 1.3f * s, r == sel_ ? 0.85f : 0.5f});
        hud.push_back({rowValue(r), (MENU_X + VALUE_X) * s, y + 3 * s, 1.3f * s, r == sel_ ? 1.f : 0.8f, r < ROW_SECTION});
    }
    const float hy = (MENU_FIRST + ROWS * ROW_H + 10) * s;
    // Shortcuts, one per line: the key in bold (drawn twice, a pixel apart), what it does next to it.
    const char* keys[KEYS][2] = {{"ARROWS", "CHOOSE / CHANGE"}, {"T", "NEXT UNIVERSE"}, {"V", "NEXT VARIANT"},
                                 {"I", "INSTANT SWITCH"},       {"F", "FILL BOARD"},    {"B", "SLOW MOTION"},
                                 {"X", "TETRIS"},               {"L", "LEVEL UP"},      {"ESC", "QUIT"}};
    for (int i = 0; i < KEYS; i++) {
        const float y = hy + i * 17 * s;
        for (float dx : {0.f, std::max(1.f, std::round(s))}) hud.push_back({keys[i][0], (MENU_X + 12) * s + dx, y, 1.2f * s, 0.8f});
        hud.push_back({keys[i][1], (MENU_X + 104) * s, y, 1.1f * s, 0.5f});
    }
    // Send to dashboard: a button (or D), then a comment box; Enter sends, Esc cancels.
    {
        const bool hot = overButton_ || typing_;
        panels.push_back({(MENU_X + 12) * s, BUTTON_Y * s, (MENU_W - 24) * s, BUTTON_H * s, hot ? 0.35f : 0.18f, true});
        const char* label = typing_ ? "SEND  (ENTER)" : "SEND TO DASHBOARD  (D)";
        for (float dx : {0.f, std::max(1.f, std::round(s))})
            hud.push_back({label, (MENU_X + MENU_W * 0.5f) * s + dx, (BUTTON_Y + 8) * s, 1.3f * s, 0.95f, false, true});
        float y = commentY;
        if (typing_) {
            for (const std::string& l : lines) {
                hud.push_back({l, (MENU_X + 12) * s, y * s, 1.1f * s, 0.9f});
                y += 16;
            }
            hud.push_back({comment_.empty() ? "TYPE A COMMENT    ESC  CANCEL" : "ENTER  SEND    ESC  CANCEL",
                           (MENU_X + 12) * s, (y + 6) * s, 1.1f * s, 0.45f});
            y += 24;
        }
        if (toast_ > 0) hud.push_back({toastText_, (MENU_X + 12) * s, (y + 2) * s, 1.1f * s, std::min(1.f, toast_), true});
    }
    R_.setPanels(std::move(panels));
    return hud;
}

// The computer plays at a calm, human pace: a moment to think, unhurried moves, soft drops.
void SceneViewer::autoplay(float dt) {
    if (!game_.hasPiece()) { planned_ = false; return; }
    if (!planned_) {
        planned_ = game_.planMove(planRot_, planX_);
        botTimer_ = 0.45f;
        if (!planned_) return;
    }
    botTimer_ -= dt;
    if (botTimer_ > 0) return;
    botTimer_ = 0.13f;
    const Piece& p = game_.piece();
    if (p.rot != planRot_) { if (!game_.rotate(1)) planned_ = false; return; }
    if (p.x < planX_) { if (!game_.move(1)) planned_ = false; return; }
    if (p.x > planX_) { if (!game_.move(-1)) planned_ = false; return; }
    if (game_.softDrop()) { botTimer_ = 0.035f; return; }
    game_.hardDrop();
    planned_ = false;
}

// No song: a steady beat, and section / level envelopes like the game's.
void SceneViewer::updateMusic(float dt) {
    const float bpm = BPM;
    const double beat = time_ * bpm / 60.0;
    music_.bpm = bpm;
    music_.beat = beat;
    music_.hasSong = true;
    // Section feel (density, speed, glow, saturation), as structureProfile gives it halfway through a section.
    const float prof[SEG_COUNT][4] = {{0.45f, 0.55f, 0.65f, 0.8f}, {0.7f, 0.8f, 0.9f, 0.95f}, {0.73f, 0.98f, 1.f, 0.97f},
                                      {1.0f, 1.2f, 1.25f, 1.1f},   {1.1f, 1.45f, 1.45f, 1.15f}, {0.3f, 0.4f, 0.7f, 0.75f},
                                      {0.4f, 0.45f, 0.7f, 0.8f}};
    const float* p = prof[section_];
    music_.density = approach(music_.density, p[0], 1.f, dt);
    music_.speed = approach(music_.speed, p[1], 1.f, dt);
    music_.glow = approach(music_.glow, p[2], 1.f, dt);
    music_.saturation = approach(music_.saturation, p[3], 1.f, dt);
    music_.intensity = approach(music_.intensity, levelEnergy(level_), 0.5f, dt);
    music_.energy = approach(music_.energy, saturate((0.3f + 0.7f * smoothstepf(80.f, 135.f, bpm)) * (0.45f + 0.55f * music_.intensity)), 1.f, dt);
    const float E = music_.energy;
    // A kick on every beat from the mid level on.
    if (std::floor(beat) != std::floor(lastBeat_) && level_ >= 1) kickEnv_ = 1.f;
    lastBeat_ = beat;
    kickEnv_ = approach(kickEnv_, 0.f, 4.f, dt);
    music_.kick = E * kickEnv_ * (level_ == 2 ? 0.8f : 0.4f);
    music_.beatPhase = (float)(beat - std::floor(beat));
    const float breath = 0.5f - 0.5f * std::cos(TAU * (float)std::fmod(beat / 8.0, 1.0));
    const float swell = 0.5f + 0.5f * std::cos(TAU * music_.beatPhase);
    const bool zone = level_ == 2 && (section_ == SEG_CHORUS || section_ == SEG_DROP);
    const float accent = (zone ? 1.f : 0.f) * pulseTempoAmp(bpm) * swell * swell * (0.4f + 0.6f * kickEnv_);
    const float groove = (level_ >= 1 ? 0.5f : 0.15f) * E * smoothstepf(95.f, 130.f, bpm) * swell * swell * swell;
    music_.beatPulse = 0.25f * breath + accent + groove;
    // Bands: slow wandering levels, the low ones lifted by the kick.
    const float t = (float)time_;
    for (int b = 0; b < NUM_BANDS; b++) {
        float v = (0.2f + 0.4f * music_.intensity) * (1.f - 0.3f * b / NUM_BANDS) + 0.12f * std::sin(t * (0.5f + 0.13f * b) + b * 1.7f);
        if (b < 4) v += 0.3f * music_.kick;
        v = saturate(v);
        music_.audio.bands[b] = approach(music_.audio.bands[b], v, 1.2f, dt);
        music_.bandsFast[b] = approach(music_.bandsFast[b], v, 8.f, dt);
    }
    auto avg = [&](int a, int z) { float x = 0; for (int b = a; b < z; b++) x += music_.audio.bands[b]; return x / (z - a); };
    music_.audio.bass = avg(0, 4);
    music_.audio.mid = avg(4, 10);
    music_.audio.high = avg(10, NUM_BANDS);
    music_.audio.loud = avg(0, NUM_BANDS);
    music_.audio.onset = 0;
}

void SceneViewer::onKey(int key, int action, int mods) {
    if (action == GLFW_RELEASE) return;
    if (typing_) { // the comment box takes the keyboard
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) sendToDashboard();
        else if (key == GLFW_KEY_ESCAPE) typing_ = false;
        else if (key == GLFW_KEY_BACKSPACE) {
            while (!comment_.empty() && ((unsigned char)comment_.back() & 0xC0) == 0x80) comment_.pop_back(); // UTF-8
            if (!comment_.empty()) comment_.pop_back();
        } else if (key == GLFW_KEY_V && (mods & GLFW_MOD_CONTROL)) {
            if (const char* clip = glfwGetClipboardString(win_)) comment_ += clip;
            for (char& c : comment_) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        }
        return;
    }
    const bool press = action == GLFW_PRESS;
    switch (key) {
    case GLFW_KEY_ESCAPE: case GLFW_KEY_Q: glfwSetWindowShouldClose(win_, 1); break;
    case GLFW_KEY_UP: sel_ = (sel_ + ROWS - 1) % ROWS; break;
    case GLFW_KEY_DOWN: sel_ = (sel_ + 1) % ROWS; break;
    case GLFW_KEY_LEFT: change(sel_, -1); break;
    case GLFW_KEY_RIGHT: change(sel_, 1); break;
    case GLFW_KEY_T: if (press) change(ROW_UNIVERSE, (mods & GLFW_MOD_SHIFT) ? -1 : 1); break;
    case GLFW_KEY_V: if (press) change(ROW_VARIANT, (mods & GLFW_MOD_SHIFT) ? -1 : 1); break;
    case GLFW_KEY_I: if (press) show(false); break;
    case GLFW_KEY_F: if (press) game_.debugFillBoard(rng_()); break;
    case GLFW_KEY_B: if (press) { game_.debugChargeBonus(); game_.activateBonus(); } break;
    case GLFW_KEY_X: if (press) tetris(); break;
    case GLFW_KEY_L: if (press) R_.levelUp(); break;
    case GLFW_KEY_D:
        if (press) { typing_ = true; skipChar_ = true; } // the 'd' itself is not typed
        break;
    default: break;
    }
}

void SceneViewer::onChar(unsigned cp) {
    if (!typing_) return;
    if (skipChar_) { skipChar_ = false; return; }
    if (cp < 0x80) comment_ += (char)cp; // UTF-8
    else if (cp < 0x800) { comment_ += (char)(0xC0 | (cp >> 6)); comment_ += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        comment_ += (char)(0xE0 | (cp >> 12));
        comment_ += (char)(0x80 | ((cp >> 6) & 0x3F));
        comment_ += (char)(0x80 | (cp & 0x3F));
    } else {
        comment_ += (char)(0xF0 | (cp >> 18));
        comment_ += (char)(0x80 | ((cp >> 12) & 0x3F));
        comment_ += (char)(0x80 | ((cp >> 6) & 0x3F));
        comment_ += (char)(0x80 | (cp & 0x3F));
    }
}

// Runs tools/scenereview.py add in the background: it renders the universe for the dashboard (same universe,
// variant, section and level) and opens a thread "to process" with the comment.
void SceneViewer::sendToDashboard() {
    if (comment_.find_first_not_of(' ') == std::string::npos) return;
    const std::string script = std::string(ZEN_SOURCE_DIR) + "/tools/scenereview.py";
    if (!std::filesystem::exists(script)) {
        toastText_ = "NO DASHBOARD TOOL (NEEDS THE SOURCES)";
        toast_ = 4.f;
        return;
    }
#ifdef _WIN32
    const std::string python = "python";
#else
    const std::string python = "python3";
#endif
    const std::string cmd = python + " " + platform::quoteArg(script) + " add --code " + platform::quoteArg(spec()) +
                            " --section " + segmentName(section_) + " --level " + std::to_string(level_) +
                            " --message " + platform::quoteArg(comment_);
    std::printf("[zenscene] sending to the dashboard: %s\n", comment_.c_str());
    std::fflush(stdout);
    auto state = send_;
    state->store(1);
    std::thread([cmd, state] { state->store(std::system(cmd.c_str()) == 0 ? 2 : 3); }).detach();
    typing_ = false;
    comment_.clear();
}

void SceneViewer::onMouse(int button, int action) {
    if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT && overButton_) {
        if (typing_) sendToDashboard();
        else typing_ = true;
        return;
    }
    if (typing_ || action != GLFW_PRESS || hover_ < 0) return;
    sel_ = hover_;
    if (button == GLFW_MOUSE_BUTTON_LEFT) change(hover_, 1);
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) change(hover_, -1);
}

void SceneViewer::onScroll(double dy) {
    if (typing_ || hover_ < 0 || dy == 0) return;
    sel_ = hover_;
    change(hover_, dy < 0 ? 1 : -1);
}

int SceneViewer::run() {
    const bool review = !opt_.reviewShot.empty();
    const bool shotMode = !opt_.shot.empty() || review;
    if (review) opt_.shot = opt_.reviewShot;
    menu_ = !review;
    if (shotMode) rng_.seed(1); // reproducible snapshots: before / after pairs show the same game
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    if (shotMode) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    }
    win_ = glfwCreateWindow(opt_.width, opt_.height, "Zenscene", nullptr, nullptr);
    if (!win_) { std::fprintf(stderr, "could not create an OpenGL 3.3 window\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win_);
    glfwSwapInterval(shotMode ? 0 : 1);
    if (!gladLoadGL(glfwGetProcAddress)) { std::fprintf(stderr, "could not load OpenGL functions\n"); return 1; }
    glGetError();
    glfwSetWindowUserPointer(win_, this);
    glfwSetKeyCallback(win_, [](GLFWwindow* w, int key, int, int action, int mods) {
        static_cast<SceneViewer*>(glfwGetWindowUserPointer(w))->onKey(key, action, mods);
    });
    glfwSetMouseButtonCallback(win_, [](GLFWwindow* w, int button, int action, int) {
        static_cast<SceneViewer*>(glfwGetWindowUserPointer(w))->onMouse(button, action);
    });
    glfwSetCharCallback(win_, [](GLFWwindow* w, unsigned cp) {
        static_cast<SceneViewer*>(glfwGetWindowUserPointer(w))->onChar(cp);
    });
    glfwSetScrollCallback(win_, [](GLFWwindow* w, double, double dy) {
        static_cast<SceneViewer*>(glfwGetWindowUserPointer(w))->onScroll(dy);
    });

    int fbw, fbh;
    glfwGetFramebufferSize(win_, &fbw, &fbh);
    if (!R_.init(fbw, fbh)) std::fprintf(stderr, "[gl] warning: renderer init reported a GL error\n");
    s_ = std::max(1.f, fbh / 900.f);
    sel_ = std::clamp(opt_.select, 0, ROWS - 1);

    game_.reset(rng_());
    game_.debugFillBoard(rng_()); // blocks right away
    seed_ = rng_();
    if (opt_.section >= 0) {
        section_ = opt_.section;
        const int lv[SEG_COUNT] = {0, 1, 1, 2, 2, 0, 0};
        level_ = lv[section_];
    }
    if (opt_.level >= 0) level_ = opt_.level;
    if (!opt_.universe.empty()) {
        int v = -1;
        const int u = universes::parse(opt_.universe, &v);
        if (u < 0) std::fprintf(stderr, "[zenscene] unknown universe '%s'\n", opt_.universe.c_str());
        universe_ = std::max(0, u);
        variant_ = std::max(0, v);
    } else {
        universe_ = universes::pick(SongLook{}, seed_, -1);
        variant_ = (int)(seed_ % std::max<size_t>(1, universes::entry(universe_).variants.size()));
    }
    show(false);

    double last = glfwGetTime();
    float shotT = 0;
    int lastW = 0, lastH = 0;
    while (!glfwWindowShouldClose(win_)) {
        const double now = glfwGetTime();
        const float dt = shotMode ? 1.f / 60.f : (float)std::min(0.1, now - last);
        last = now;
        time_ += dt;
        glfwPollEvents();
        {
            double mx, my;
            int ww, wh;
            glfwGetCursorPos(win_, &mx, &my);
            glfwGetWindowSize(win_, &ww, &wh);
            glfwGetFramebufferSize(win_, &fbw, &fbh);
            hover_ = shotMode || ww <= 0 || typing_ ? -1 : rowAt(mx * fbw / ww, my * fbh / wh);
            overButton_ = !shotMode && ww > 0 && overButton(mx * fbw / ww, my * fbh / wh);
            if (ww > 0) { mx_ = mx * fbw / ww; my_ = my * fbh / wh; }
        }
        // Dashboard sends finish in the background.
        toast_ = std::max(0.f, toast_ - dt);
        if (const int st = send_->load(); st != sendSeen_) {
            sendSeen_ = st;
            toastText_ = st == 1 ? "SENDING TO THE DASHBOARD..." : (st == 2 ? "SENT: IN TO PROCESS" : "COULD NOT SEND (SEE THE TERMINAL)");
            toast_ = st == 1 ? 1e9f : 4.f;
        }

        // The computer plays; gravity follows the section's pace. Dashboard snapshots get a filled board shortly
        // before the shot, then left as it is.
        const float fillAt = opt_.shotAfter - 1.5f;
        const bool filled = review && shotT >= fillAt;
        if (review && !filled && shotT + dt >= fillAt) game_.debugFillBoard(rng_());
        if (filled) {
            // the board stays as filled
        } else if (game_.over()) {
            overTime_ += dt;
            if (overTime_ > 3.f) { game_.restart(); overTime_ = 0; }
        } else {
            autoplay(dt);
            const float pace[SEG_COUNT] = {0.5f, 0.8f, 1.f, 1.2f, 1.3f, 0.4f, 0.5f};
            gravAcc_ += dt * BPM / 60.f * pace[section_] * (game_.bonusActive() ? 0.25f : 1.f);
            for (; gravAcc_ >= 1.f; gravAcc_ -= 1.f) game_.gravityStep();
        }
        game_.update(dt);
        for (const GameEvent& ev : game_.events()) {
            R_.onEvent(ev, game_);
            if (ev.type == GameEvent::BonusStart) R_.setBonus(true);
            if (ev.type == GameEvent::BonusEnd) R_.setBonus(false);
        }
        game_.events().clear();
        updateMusic(dt);

        if ((fbw != lastW || fbh != lastH) && fbw > 0 && fbh > 0) {
            R_.resize(fbw, fbh);
            lastW = fbw;
            lastH = fbh;
        }
        if (fbw > 0 && fbh > 0) R_.render(game_, music_, time_, dt, false, buildHud(), 1.f);
        if (shotMode && (shotT += dt) >= opt_.shotAfter) {
            R_.screenshot(opt_.shot);
            std::printf("[shot] %s\n", opt_.shot.c_str());
            if (review)
                std::printf("[review] {\"code\": \"%s\", \"scene\": \"%s\", \"level\": %d, \"section\": \"%s\", \"bpm\": %.0f}\n",
                            spec().c_str(), R_.label().c_str(), level_, segmentName(section_), BPM);
            break;
        }
        glfwSwapBuffers(win_);
    }
    // A send still running finishes first.
    for (int i = 0; i < 600 && send_->load() == 1; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    glfwDestroyWindow(win_);
    glfwTerminate();
    return 0;
}

void usage() {
    std::printf(
        "usage: zenscene [options] [UNIVERSE[:VARIANT]]\n"
        "  Shows a universe (a random one, or the one given, as zentris shows it in its corner) while the computer\n"
        "  plays. The menu on the left picks the universe, its variant, the song phase and the transition used for\n"
        "  the next change of universe (T: next universe, V: next variant, both with that transition).\n"
        "  --size WxH        window size (default 1600x900)\n"
        "  --shot OUT.png    render for a few seconds (--shot-after SEC, default 8), save a screenshot, exit\n"
        "  --section NAME    song section shown (intro, verse, build, chorus, drop, break, outro)\n"
        "  --level N         scene level shown: 0 calm, 1 mid, 2 peak (default: the section's usual one)\n"
        "  --review-shot OUT.png  a screenshot without the menu and with a filled board, prints a [review] line\n"
        "  --select N        menu row selected at start\n"
        "  --list            print the universes and their variants, then exit\n");
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--size") { std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height); }
        else if (a == "--shot") o.shot = next();
        else if (a == "--shot-after") o.shotAfter = (float)std::atof(next().c_str());
        else if (a == "--select") o.select = std::atoi(next().c_str());
        else if (a == "--review-shot") o.reviewShot = next();
        else if (a == "--level") o.level = std::clamp(std::atoi(next().c_str()), 0, 2);
        else if (a == "--section") {
            const std::string v = next();
            std::string up = v;
            for (char& c : up) c = (char)std::toupper((unsigned char)c);
            for (int k = 0; k < SEG_COUNT; k++)
                if (up == segmentName(k)) o.section = k;
            if (o.section < 0) { std::fprintf(stderr, "unknown section '%s'\n", v.c_str()); return 1; }
        }
        else if (a == "--universe" || a == "--scene") o.universe = next();
        else if (a == "--list") {
            for (int k = 0; k < universes::count(); k++) {
                const UniverseEntry& e = universes::entry(k);
                std::printf("%-10s %s%s\n", e.name, e.blurb, e.hidden ? " (test, never picked)" : "");
                for (size_t v = 0; v < e.variants.size(); v++) std::printf("  %s:%s\n", e.name, e.variants[v].c_str());
            }
            return 0;
        }
        else if (!a.empty() && a[0] == '-') { usage(); return 1; }
        else o.universe = a;
    }
    return SceneViewer(std::move(o)).run();
}
