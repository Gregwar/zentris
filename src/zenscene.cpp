// zenscene: a scene viewer. The computer plays (blocks keep falling) while the menu on the left adjusts the
// scene: every choice of the generated scene can be pinned, and the song phase (section, level) is picked by
// hand. The scene code printed after each change gives the adjusted scene back: zentris --scene CODE.
// Options can be switched off (src/scene-options.txt): scenes, new random ones included, never pick them; the menu
// hides them too unless --disabled is given. "Send to dashboard" makes a scene review thread (tools/scenereview.py) out of the scene and a comment.
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "game.hpp"
#include "library.hpp"
#include "platform.hpp"
#include "renderer.hpp"
#include "songplan.hpp"
#include "stb_easy_font.h"
#include "theme.hpp"

namespace {

// Menu rows: the song phase first, then the scene's choices.
enum { ROW_SECTION, ROW_LEVEL, ROW_FIELDS };
constexpr int ROWS = ROW_FIELDS + SF_COUNT;
const char* LEVEL_NAMES[3] = {"CALM", "MID", "PEAK"};

struct Options {
    std::string code;
    int width = 1600, height = 900;
    std::string shot; // --shot OUT.png: render a few seconds, save a screenshot, exit
    float shotAfter = 8.f;
    std::string reviewShot; // --review-shot OUT.png: the same without the menu, for the dashboard (prints [review])
    int select = 0;
    int section = -1, level = -1;
    bool showDisabled = false; // --disabled: the menu also offers the options switched off
};

class SceneViewer {
public:
    explicit SceneViewer(Options o) : opt_(std::move(o)) {}
    int run();

private:
    void setCode(const std::string& code);
    void randomScene();
    void rebuild(float seconds);
    void change(int row, int dir);
    void togglePin(int row);
    void toggleEnabled(int row);
    void saveOptions();
    std::string rowValue(int row, bool* pinned) const;
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

    Options opt_;
    GLFWwindow* win_ = nullptr;
    Renderer R_;
    Game game_;
    MusicState music_;
    std::mt19937_64 rng_{std::random_device{}() ^ (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count()};

    SceneId id_;                // the scene (its code is the identity)
    bool pin_[SF_COUNT] = {};   // choices kept through new random scenes
    Theme base_;
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

void SceneViewer::setCode(const std::string& code) {
    SceneId id;
    if (!parseSceneCode(code, id)) {
        std::fprintf(stderr, "[zenscene] bad scene code '%s', using a random scene\n", code.c_str());
        randomScene();
        return;
    }
    id_ = id;
    rebuild(0.01f);
}

// R: a new random identity (only options switched on), keeping the pinned choices.
void SceneViewer::randomScene() {
    SceneId id = pickIdentity(Footprint{}, rng_());
    for (int f = 0; f < SF_COUNT; f++)
        if (pin_[f]) id.v[f] = id_.v[f];
    if (id.v[SF_PART1] == 0) id.v[SF_PART2] = 0;
    id_ = id;
    rebuild(1.2f);
}

// Rebuilds the scene from its identity, shows it at the chosen song phase, prints its code.
void SceneViewer::rebuild(float seconds) {
    base_ = buildTheme(id_);
    const float energy = level_ == 2 ? 0.9f : (level_ == 1 ? 0.55f : 0.25f);
    R_.setTheme(evolveTheme(base_, level_, energy), seconds, seconds > 0.1f);
    std::printf("[zenscene] %s  |  %s  |  lock %s  |  %s %s\n  zentris --scene %s\n", base_.code.c_str(),
                base_.name.c_str(), lockEffectName(base_.lockEffect), segmentName(section_), LEVEL_NAMES[level_],
                base_.code.c_str());
    std::fflush(stdout);
}

std::string SceneViewer::rowValue(int row, bool* pinned) const {
    *pinned = false;
    if (row == ROW_SECTION) return segmentName(section_);
    if (row == ROW_LEVEL) return LEVEL_NAMES[level_];
    const int f = row - ROW_FIELDS;
    *pinned = pin_[f];
    if (f == SF_PART2 && id_.v[SF_PART1] == 0) return "-";
    return sceneFieldValueName(f, id_.v[f]);
}

void SceneViewer::change(int row, int dir) {
    if (row == ROW_SECTION) {
        section_ = (section_ + dir + SEG_COUNT) % SEG_COUNT;
        // Each section starts at its usual level (the level row still overrides it).
        const int lv[SEG_COUNT] = {0, 1, 1, 2, 2, 0, 0};
        level_ = lv[section_];
        rebuild(1.2f);
        return;
    }
    if (row == ROW_LEVEL) {
        level_ = (level_ + dir + 3) % 3;
        rebuild(1.2f);
        return;
    }
    const int f = row - ROW_FIELDS;
    if (f == SF_PART2 && id_.v[SF_PART1] == 0) return; // a second layout needs a first one
    const int n = sceneFieldValues(f);
    int v = id_.v[f];
    for (int k = 0; k < n; k++) { // the options switched off are skipped (unless --disabled)
        v = (v + dir + n) % n;
        if (opt_.showDisabled || sceneOptionEnabled(f, v)) break;
    }
    id_.v[f] = v;
    if (f == SF_PART1 && v == 0) id_.v[SF_PART2] = 0;
    pin_[f] = true; // a changed choice is pinned
    rebuild(0.6f);
}

// Space: pins the selected choice as it is (it then stays through new scenes), or unpins it.
void SceneViewer::togglePin(int row) {
    if (row < ROW_FIELDS) return;
    const int f = row - ROW_FIELDS;
    pin_[f] = !pin_[f];
}

// X: switches the option shown on the row off (or back on), in src/scene-options.txt. A switched off option stays
// on screen, pinned, until the row changes.
void SceneViewer::toggleEnabled(int row) {
    if (row < ROW_FIELDS) return;
    const int f = row - ROW_FIELDS, v = id_.v[f];
    if (!sceneOptionDisableable(f, v)) return;
    const bool on = !sceneOptionEnabled(f, v);
    setSceneOptionEnabled(f, v, on);
    if (!on) pin_[f] = true;
    std::printf("[zenscene] %s %s %s\n", on ? "enabled" : "disabled", sceneFieldName(f == SF_PART2 ? SF_PART1 : f),
                sceneFieldValueName(f, v).c_str());
    saveOptions();
    rebuild(0.6f);
}

void SceneViewer::saveOptions() {
    const std::string path = std::string(ZEN_SOURCE_DIR) + "/src/scene-options.txt";
    FILE* out = std::filesystem::exists(std::filesystem::path(path).parent_path()) ? std::fopen(path.c_str(), "w") : nullptr;
    if (!out) {
        toastText_ = "OPTIONS NOT SAVED (NO SOURCES)";
        toast_ = 4.f;
        return;
    }
    std::fputs(sceneOptionsText().c_str(), out);
    std::fclose(out);
    std::printf("  saved in %s (rebuild zentris to use it in the game)\n", path.c_str());
    toastText_ = "SAVED: REBUILD THE GAME TO USE IT";
    toast_ = 3.f;
}

// Menu geometry (pixels): title, then one line per row.
constexpr float MENU_X = 16, MENU_TOP = 20, MENU_FIRST = 64, ROW_H = 24, MENU_W = 344, VALUE_X = 128;
// Per row, at the right: the pin box and the ON / OFF box.
constexpr float PIN_X = 282, PIN_W = 16, ONOFF_X = 304, ONOFF_W = 28;
enum { HIT_NONE, HIT_PIN, HIT_ONOFF };
int boxAt(double mx, double my, float s, int row) {
    const float y = (MENU_FIRST + row * ROW_H) * s;
    if (my < y - 2 * s || my > y + (ROW_H - 6) * s) return HIT_NONE;
    if (mx >= (MENU_X + PIN_X - 3) * s && mx <= (MENU_X + PIN_X + PIN_W + 3) * s) return HIT_PIN;
    if (mx >= (MENU_X + ONOFF_X - 3) * s && mx <= (MENU_X + ONOFF_X + ONOFF_W + 3) * s) return HIT_ONOFF;
    return HIT_NONE;
}
const vec3 OFF_RED(0.92f, 0.26f, 0.26f);

// Below the shortcuts: the "send to dashboard" button, then the comment box while typing.
constexpr float BUTTON_Y = MENU_FIRST + ROWS * ROW_H + 117, BUTTON_H = 26, COMMENT_COLS = 40;

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
    if (!menu_) { // dashboard snapshots: the scene and its code only
        hud.push_back({base_.name, 28 * s, h - 60.f * s, 1.3f * s, 0.45f});
        hud.push_back({"#" + base_.code, 28 * s, h - 40.f * s, 1.5f * s, 0.85f, true});
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
    if (opt_.showDisabled) {
        char tag[] = "ALL OPTIONS";
        stb_easy_font_spacing(0.5f);
        hud.push_back({tag, (MENU_X + MENU_W - 14) * s - stb_easy_font_width(tag) * 1.1f * s, MENU_TOP * s + 10 * s, 1.1f * s, 0.5f});
    }
    for (int r = 0; r < ROWS; r++) {
        const float y = (MENU_FIRST + r * ROW_H) * s;
        if (r == ROW_FIELDS) panels.push_back({(MENU_X + 12) * s, y - 4 * s, (MENU_W - 24) * s, 1.f * s, 0.25f, true});
        if (r == sel_) panels.push_back({MENU_X * s, y - 2 * s, 4 * s, (ROW_H - 4) * s, 0.9f, true});
        if (r == sel_ || r == hover_) panels.push_back({(MENU_X + 6) * s, y - 2 * s, (MENU_W - 12) * s, (ROW_H - 4) * s, r == sel_ ? 0.16f : 0.08f, true});
        bool pinned = false;
        const std::string label = r == ROW_SECTION ? "SECTION" : (r == ROW_LEVEL ? "LEVEL" : sceneFieldName(r - ROW_FIELDS));
        const std::string value = rowValue(r, &pinned);
        hud.push_back({label, (MENU_X + 12) * s, y + 3 * s, 1.3f * s, r == sel_ ? 0.85f : 0.5f});
        // Pinned choices are in the accent color, generated ones in the text color; switched off ones are dimmed
        // and struck through in red.
        const int f = r - ROW_FIELDS;
        const int v = r >= ROW_FIELDS ? id_.v[f] : -1;
        const bool off = r >= ROW_FIELDS && !sceneOptionEnabled(f, v);
        hud.push_back({value, (MENU_X + VALUE_X) * s, y + 3 * s, 1.3f * s, off ? 0.45f : (r == sel_ || pinned ? 1.f : 0.8f), pinned});
        if (off) {
            std::vector<char> txt(value.begin(), value.end());
            txt.push_back(0);
            stb_easy_font_spacing(0.5f);
            const float tw = stb_easy_font_width(txt.data()) * 1.3f;
            panels.push_back({(MENU_X + VALUE_X - 2) * s, y + 7 * s, tw * s + 4 * s, std::max(1.f, 1.5f * s), 0.95f, false, true, OFF_RED});
        }
        if (r < ROW_FIELDS || (f == SF_PART2 && base_.layerCount == 0)) continue;
        const int hit = r == hover_ ? boxAt(mx_, my_, s, r) : HIT_NONE;
        // Pin: a small push-pin (head, body, collar, needle), in the accent color when pinned, faint gray when not.
        const float py = y + 1 * s, pw = PIN_W * s;
        {
            const float cx = (MENU_X + PIN_X) * s + pw * 0.5f, a = pinned ? 0.95f : (hit == HIT_PIN ? 0.75f : 0.3f);
            const float parts[4][4] = {{-4, 0, 8, 5}, {-2.5f, 5, 5, 4}, {-5, 9, 10, 2}, {-0.75f, 11, 1.5f, 5}};
            for (const auto& q : parts)
                panels.push_back({cx + q[0] * s, py + q[1] * s, std::max(1.f, q[2] * s), std::max(1.f, q[3] * s), a,
                                  pinned, !pinned, vec3(0.6f)});
        }
        // ON / OFF box: "ON" in plain dim text, "OFF" on a red tag.
        if (!sceneOptionDisableable(f, v)) continue;
        const float ox = (MENU_X + ONOFF_X) * s;
        if (off) panels.push_back({ox, py - 1 * s, ONOFF_W * s, (PIN_W + 2) * s, 0.9f, false, true, OFF_RED});
        else if (hit == HIT_ONOFF) panels.push_back({ox, py - 1 * s, ONOFF_W * s, (PIN_W + 2) * s, 0.15f, true});
        hud.push_back({off ? "OFF" : "ON", ox + ONOFF_W * 0.5f * s, y + 4 * s, 1.1f * s, off ? 1.f : (hit == HIT_ONOFF ? 0.9f : 0.4f), false, true});
    }
    const float hy = (MENU_FIRST + ROWS * ROW_H + 10) * s;
    // Shortcuts, one per line: the key in bold (drawn twice, a pixel apart), what it does next to it.
    const char* keys[6][2] = {{"SPACE", "PIN / UNPIN"}, {"X", "ENABLE / DISABLE"}, {"C", "UNPIN ALL"}, {"R", "NEW SCENE"},
                              {"F", "FILL BOARD"}, {"ESC", "QUIT"}};
    for (int i = 0; i < 6; i++) {
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
    // The scene code, bottom left (also printed in the terminal after each change).
    hud.push_back({base_.name, 28 * s, h - 60.f * s, 1.3f * s, 0.45f});
    hud.push_back({"#" + base_.code, 28 * s, h - 40.f * s, 1.5f * s, 0.85f, true});
    R_.setPanels(std::move(panels));
    return hud;
}

// The computer plays at a calm, human pace: a moment to think, unhurried moves, soft drops.
void SceneViewer::autoplay(float dt) {
    if (game_.bonusReady()) game_.activateBonus();
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

// No song: a steady beat at the scene's tempo, and section / level envelopes like the game's.
void SceneViewer::updateMusic(float dt) {
    const float bpm = BPM;
    const double beat = time_ * bpm / 60.0;
    // Section feel (density, speed, glow, saturation), as structureProfile gives it halfway through a section.
    const float prof[SEG_COUNT][4] = {{0.45f, 0.55f, 0.65f, 0.8f}, {0.7f, 0.8f, 0.9f, 0.95f}, {0.73f, 0.98f, 1.f, 0.97f},
                                      {1.0f, 1.2f, 1.25f, 1.1f},   {1.1f, 1.45f, 1.45f, 1.15f}, {0.3f, 0.4f, 0.7f, 0.75f},
                                      {0.4f, 0.45f, 0.7f, 0.8f}};
    const float* p = prof[section_];
    music_.density = approach(music_.density, p[0], 1.f, dt);
    music_.speed = approach(music_.speed, p[1], 1.f, dt);
    music_.glow = approach(music_.glow, p[2], 1.f, dt);
    music_.saturation = approach(music_.saturation, p[3], 1.f, dt);
    const float intensity = level_ == 2 ? 0.9f : (level_ == 1 ? 0.55f : 0.25f);
    music_.intensity = approach(music_.intensity, intensity, 0.5f, dt);
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
    switch (key) {
    case GLFW_KEY_ESCAPE: case GLFW_KEY_Q: glfwSetWindowShouldClose(win_, 1); break;
    case GLFW_KEY_UP: sel_ = (sel_ + ROWS - 1) % ROWS; break;
    case GLFW_KEY_DOWN: sel_ = (sel_ + 1) % ROWS; break;
    case GLFW_KEY_LEFT: change(sel_, -1); break;
    case GLFW_KEY_RIGHT: change(sel_, 1); break;
    case GLFW_KEY_SPACE: if (action == GLFW_PRESS) togglePin(sel_); break;
    case GLFW_KEY_X: if (action == GLFW_PRESS) toggleEnabled(sel_); break;
    case GLFW_KEY_C:
        if (action == GLFW_PRESS) for (bool& p : pin_) p = false;
        break;
    case GLFW_KEY_R: if (action == GLFW_PRESS) randomScene(); break;
    case GLFW_KEY_F: if (action == GLFW_PRESS) game_.debugFillBoard(rng_()); break;
    case GLFW_KEY_D:
        if (action == GLFW_PRESS) { typing_ = true; skipChar_ = true; } // the 'd' itself is not typed
        break;
    default: break;
    }
    (void)mods;
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

// Runs tools/scenereview.py add in the background: it renders the scene for the dashboard (same code, section and
// level) and opens a thread "to process" with the comment.
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
    const std::string cmd = python + " " + platform::quoteArg(script) + " add --code " + platform::quoteArg(base_.code) +
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
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (const int hit = boxAt(mx_, my_, s_, hover_); hit != HIT_NONE) {
            if (hit == HIT_PIN) togglePin(hover_);
            else toggleEnabled(hover_);
            return;
        }
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT) change(hover_, 1);
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) change(hover_, -1);
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE) togglePin(hover_);
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

    // The options list of the sources (it may be newer than the one built in).
    if (FILE* in = std::fopen((std::string(ZEN_SOURCE_DIR) + "/src/scene-options.txt").c_str(), "r")) {
        std::string text;
        char buf[4096];
        for (size_t n; (n = std::fread(buf, 1, sizeof(buf), in)) > 0;) text.append(buf, n);
        std::fclose(in);
        loadSceneOptions(text);
    }
    game_.reset(rng_());
    game_.debugFillBoard(rng_()); // blocks right away
    if (opt_.section >= 0) {
        section_ = opt_.section;
        const int lv[SEG_COUNT] = {0, 1, 1, 2, 2, 0, 0};
        level_ = lv[section_];
    }
    if (opt_.level >= 0) level_ = opt_.level;
    if (opt_.code.empty()) randomScene();
    else setCode(opt_.code);

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

        // The computer plays; gravity follows the section's pace at the scene's tempo. Dashboard snapshots get a
        // filled board shortly before the shot, then left as it is.
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
            float bpm = BPM;
            while (bpm > 130.f) bpm *= 0.5f;
            while (bpm < 65.f) bpm *= 2.f;
            gravAcc_ += dt * bpm / 60.f * pace[section_];
            for (; gravAcc_ >= 1.f; gravAcc_ -= 1.f) game_.gravityStep();
        }
        game_.update(dt);
        for (const GameEvent& ev : game_.events()) R_.onEvent(ev, game_);
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
                            base_.code.c_str(), base_.name.c_str(), level_, segmentName(section_), BPM);
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
        "usage: zenscene [options] [CODE]\n"
        "  Shows a scene (a random one, or the one of a scene code as zentris shows and prints it) while the\n"
        "  computer plays. The menu on the left pins any choice of the scene and picks the song phase; the\n"
        "  scene code is printed after each change (zentris --scene CODE shows the same scene).\n"
        "  --size WxH        window size (default 1600x900)\n"
        "  --shot OUT.png    render for a few seconds (--shot-after SEC, default 8), save a screenshot, exit\n"
        "  --section NAME    song section shown (intro, verse, build, chorus, drop, break, outro)\n"
        "  --level N         scene level shown: 0 calm, 1 mid, 2 peak (default: the section's usual one)\n"
        "  --review-shot OUT.png  a screenshot without the menu and with a filled board, prints a [review] line\n"
        "  --select N        menu row selected at start\n"
        "  --disabled        the menu also offers the options switched off (X switches the shown one on / off)\n");
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
        else if (a == "--disabled") o.showDisabled = true;
        else if (a == "--level") o.level = std::clamp(std::atoi(next().c_str()), 0, 2);
        else if (a == "--section") {
            const std::string v = next();
            std::string up = v;
            for (char& c : up) c = (char)std::toupper((unsigned char)c);
            for (int k = 0; k < SEG_COUNT; k++)
                if (up == segmentName(k)) o.section = k;
            if (o.section < 0) { std::fprintf(stderr, "unknown section '%s'\n", v.c_str()); return 1; }
        }
        else if (a == "--scene") o.code = next();
        else if (!a.empty() && a[0] == '-') { usage(); return 1; }
        else o.code = a;
    }
    return SceneViewer(std::move(o)).run();
}
