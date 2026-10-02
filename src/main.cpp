// Zentris: a minimalist, music-driven Tetris.
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "audio.hpp"
#include "game.hpp"
#include "input.hpp"
#include "library.hpp"
#include "platform.hpp"
#include "renderer.hpp"
#include "songplan.hpp"
#include "theme.hpp"

namespace fs = std::filesystem;

struct Options {
    std::vector<std::string> paths;
    bool autoplay = false, mute = false, fullscreen = false, hidden = false, shuffle = true;
    std::string shotPrefix;
    int shotCount = 0;
    bool phaseShots = false;
    float shotTime = 25.f;
    uint64_t seed = 0;
    int width = 1600, height = 900;
};

static const char* DEFAULT_PLAYLIST = "https://www.youtube.com/playlist?list=PLtvLBa3c9VV4NmuGGbpansfMjs5XFvKgd";

static void usage() {
    std::printf(
        "usage: zentris [options] [songs or folders...]\n"
        "  (default: a built-in YouTube playlist, via yt-dlp; local files, folders and other YouTube\n"
        "   playlist/video URLs work too)\n"
        "  --fullscreen         start fullscreen\n"
        "  --size WxH           window size (default 1600x900)\n"
        "  --seed N             fixed scene seed (default: random each run)\n"
        "  --in-order           play songs in order (default: random order)\n"
        "  --autoplay           let the computer play\n"
        "  --mute               no sound\n"
        "  --shots PREFIX N     render N screenshots of different scenes then exit (testing)\n"
        "  --shot-time SEC      song position used for screenshots (default 25)\n"
        "  --phase-shots PREFIX one screenshot per phase of the first song, then exit (testing)\n");
}

class App {
public:
    explicit App(Options o) : opt_(std::move(o)) {}
    int run();

private:
    void startTrack(std::shared_ptr<Track> t);
    void newScene(float seconds);
    void computePhases();
    Theme phaseTheme(int phase) const;
    void handleEvents();
    void updateGravity(double songTime, float dt);
    void autoplay(float dt);
    void toggleFullscreen();
    std::vector<HudText> buildHud(float dt);

    Options opt_;
    GLFWwindow* win_ = nullptr;
    AudioEngine audio_;
    Library lib_;
    Game game_;
    Input input_;
    Renderer R_;
    uint64_t runSeed_ = 0;
    uint64_t sceneCounter_ = 0;
    std::shared_ptr<Track> track_, nextTrack_;
    Theme baseTheme_;
    // Song phases: detected sections, with long sections split into sub-phases so the scene keeps evolving.
    SongPlan plan_;
    int lastPhase_ = 0;
    float skipToast_ = 0;
    float hit_ = 0;
    int lastSeg_ = 0;          // for soft refreshes between same-level sections
    Rng wipeRng_{1};
    int pickWipe(int kind);    // 0 rising, 1 falling, 2 refresh, 3 song/scene change
    float seekToast_ = 0;
    std::string failedTitle_;
    float failedToast_ = 0;
    float overTime_ = 0; // seconds since game over
    int lastLevel_ = 1;
    float levelToast_ = 0;
    // Slow-motion bonus (gravity at x0.25 while it lasts) and Tetris rewards.
    bool slowWas_ = false;
    float slowToast_ = 0, tetrisToast_ = 0;
    int tetrisPoints_ = 0;
    bool tetrisB2B_ = false;
    bool paused_ = false, wantSkip_ = false, showHelp_ = true, quit_ = false;
    double songTime_ = 0, simSongTime_ = 0;
    float trackAge_ = 0, helpTimer_ = 14.f, sceneNameTimer_ = 0, runTime_ = 0;
    // Gravity bookkeeping.
    // Slow-motion bonus: gravity runs at BONUS_GAME_SPEED (real time) while the music keeps its speed (only a
    // short tape bend marks the start and the end).
    static constexpr float BONUS_GAME_SPEED = 0.25f;
    double rowAcc_ = 0, lastSongBeat_ = -1; // gravity rows accumulated, song beat at the last update
    float gameRate_ = 1.f; // gravity speed in real time (eased)
    float pace_ = -1, fallbackTimer_ = 0; // section pace (rows per beat before level scaling), eased
    // Autoplay.
    bool planned_ = false;
    int planRot_ = 0, planX_ = 0;
    float botTimer_ = 0;
    // Windowed geometry for fullscreen toggling.
    int winX_ = 100, winY_ = 100, winW_ = 1600, winH_ = 900;
    MusicState music_;
};

void App::startTrack(std::shared_ptr<Track> t) {
    bool first = !track_;
    track_ = std::move(t);
    audio_.play(track_);
    if (first && showHelp_) helpTimer_ = 14.f; // show controls once the music starts
    trackAge_ = 0;
    lastPhase_ = 0;
    lastSeg_ = 0;
    pace_ = -1;
    lastSongBeat_ = -1;
    simSongTime_ = 0;
    std::printf("[app] now playing: %s\n", track_->title.c_str());
    sceneCounter_++;
    computePhases();
    if (first) {
        // The scene shown (paused) while the first song loaded becomes its scene: it just starts.
        lastPhase_ = plan_.phaseAt(0.0);
    } else {
        baseTheme_ = generateTheme(track_->analysis.fp, runSeed_ ^ splitmix64(sceneCounter_));
        R_.setTheme(phaseTheme(0), 3.5f, true, 0.f, pickWipe(3));
    }
    sceneNameTimer_ = 8.f;
    std::printf("[app] scene: %s\n", baseTheme_.name.c_str());
    lib_.prefetch(runSeed_ + sceneCounter_ * 7919);
}

void App::newScene(float seconds) {
    sceneCounter_++;
    Footprint fp = track_ ? track_->analysis.fp : Footprint{};
    baseTheme_ = generateTheme(fp, runSeed_ ^ splitmix64(sceneCounter_ * 0x51ed27ull));
    R_.setTheme(phaseTheme(lastPhase_), seconds, true, 0.f, pickWipe(3));
    sceneNameTimer_ = 6.f;
    std::printf("[app] scene: %s\n", baseTheme_.name.c_str());
}

void App::computePhases() {
    plan_ = planSong(track_->analysis);
    const Analysis& an = track_->analysis;
    std::printf("[app] structure:");
    for (const Segment& sg : an.segments) std::printf(" %s@%.0f", segmentName(sg.kind), sg.start);
    std::printf("\n[app] scene levels:");
    for (const ScenePhase& p : plan_.phases) std::printf(" L%d@%.0f", p.level, p.start);
    std::printf("\n[app] %zu scene changes, %zu pulse zones\n", plan_.phases.size() - 1, plan_.pulseZones.size());
}

Theme App::phaseTheme(int phase) const {
    if (!track_ || plan_.phases.empty()) return baseTheme_;
    const ScenePhase& ph = plan_.phases[std::clamp(phase, 0, (int)plan_.phases.size() - 1)];
    return evolveTheme(baseTheme_, track_->analysis.fp, ph.level, ph.energy, true);
}

// Transition shapes vary; rising changes favour expanding shapes, falling ones dissolves and falling
// fronts, refreshes use line-like sweeps.
int App::pickWipe(int kind) {
    float w[WIPE_COUNT];
    switch (kind) {
    // radial rise fall left right inward curtains dissolve diagonal spiral diamond | risewave fallwave blinds
    // columns petals cross saltire checker corner split grain
    // ... | cloudrise cloudopen smoke  (organic cloud-like shapes are favoured everywhere)
    case 0: { float v[WIPE_COUNT] = {3, 2.5f, 0.3f, 1, 1, 1.5f, 1, 1.5f, 1, 1.2f, 1.5f, 2, 0.3f, 0.8f, 0.8f, 1.5f, 1.2f, 1, 0.6f, 0.8f, 1.2f, 1, 3, 3, 2.5f}; std::copy(v, v + WIPE_COUNT, w); break; }
    case 1: { float v[WIPE_COUNT] = {0.5f, 0.3f, 2.5f, 1, 1, 0.7f, 1.5f, 3.5f, 1, 0.8f, 0.5f, 0.3f, 2, 1.2f, 1, 0.5f, 0.6f, 0.6f, 0.8f, 0.8f, 0.8f, 2.5f, 2, 2.5f, 3.5f}; std::copy(v, v + WIPE_COUNT, w); break; }
    case 2: { float v[WIPE_COUNT] = {1, 1.5f, 0.8f, 1.2f, 1.2f, 0.6f, 1.2f, 0, 1.2f, 0.8f, 0.8f, 1.2f, 1, 1, 1, 0.6f, 0.6f, 0.6f, 0.4f, 0.8f, 1, 0, 2, 1.5f, 0}; std::copy(v, v + WIPE_COUNT, w); break; }
    default: { float v[WIPE_COUNT] = {1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.5f, 2.5f, 2.5f, 2.5f}; std::copy(v, v + WIPE_COUNT, w); break; }
    }
    return wipeRng_.weighted(w);
}

void App::toggleFullscreen() {
    GLFWmonitor* mon = glfwGetWindowMonitor(win_);
    if (mon) {
        glfwSetWindowMonitor(win_, nullptr, winX_, winY_, winW_, winH_, 0);
    } else {
        glfwGetWindowPos(win_, &winX_, &winY_);
        glfwGetWindowSize(win_, &winW_, &winH_);
        GLFWmonitor* m = glfwGetPrimaryMonitor();
        const GLFWvidmode* vm = glfwGetVideoMode(m);
        glfwSetWindowMonitor(win_, m, 0, 0, vm->width, vm->height, vm->refreshRate);
    }
}

// Game events only drive visuals: the song is the only thing you hear.
void App::handleEvents() {
    for (const GameEvent& ev : game_.events()) {
        R_.onEvent(ev, game_);
        if (ev.type == GameEvent::Clear && ev.count >= 4) {
            tetrisToast_ = 2.4f;
            tetrisPoints_ = ev.points;
            tetrisB2B_ = ev.backToBack;
        }
        if (ev.type == GameEvent::BonusStart) slowToast_ = 2.f;
    }
    game_.events().clear();
}

// Gravity rides the beat. The song section sets the pace in rows per beat (x0.4 in a break .. x1.3 in a
// drop), and the level scales it up gradually to x10 at level 20 (capped at 28 rows/s).
void App::updateGravity(double songTime, float dt) {
    const float diff = game_.difficulty();
    gameRate_ = approach(gameRate_, game_.bonusActive() ? BONUS_GAME_SPEED : 1.f, 3.f, dt);
    if (gameRate_ > 0.999f) gameRate_ = 1.f;
    if (!track_ || audio_.finished()) {
        fallbackTimer_ += dt * gameRate_;
        if (fallbackTimer_ > 0.9f * (1.f - 0.45f * diff)) { fallbackTimer_ = 0; game_.gravityStep(); }
        return;
    }
    const Analysis& an = track_->analysis;
    // Normalize the tempo into a comfortable 65..130 BPM band.
    float tempoMul = 1.f, bpm = an.fp.bpm;
    while (bpm * tempoMul > 130.f) tempoMul *= 0.5f;
    while (bpm * tempoMul < 65.f) tempoMul *= 2.f;
    // Section pace, in rows per beat (gravityPace: one constant per section, x0.4 in a break .. x1.3 in a
    // drop). Eased over ~0.25 s; at most x1 in the first 20 s of a song.
    float want = gravityPace(an, songTime + 0.25);
    if (trackAge_ < 20.f) want = std::min(want, 1.f);
    pace_ = pace_ < 0 ? want : approach(pace_, want, 4.f, dt);
    // Level scaling: x10 at the plateau (level 20), capped at 28 rows per second.
    const float MAX_ROWS_PER_SEC = 28.f;
    const float beatsPerSec = bpm * tempoMul / 60.f;
    const float rowsPerBeat = std::min(pace_ * (1.f + 9.f * diff), MAX_ROWS_PER_SEC / beatsPerSec);
    // Rows accumulate with the song's beats (scaled during the bonus so gravity runs at gameRate_ in real time).
    const double songBeat = an.beatPosition(songTime);
    double dBeat = 0;
    if (lastSongBeat_ < 0 || songBeat < lastSongBeat_ || songBeat - lastSongBeat_ > 8.0) rowAcc_ = 0; // start, seek, new song
    else dBeat = (songBeat - lastSongBeat_) * std::min(1.f, gameRate_ / std::max(0.05f, audio_.currentSpeed()));
    lastSongBeat_ = songBeat;
    rowAcc_ += dBeat * tempoMul * rowsPerBeat;
    const int steps = (int)std::min(4.0, std::floor(rowAcc_));
    for (int i = 0; i < steps; i++) game_.gravityStep();
    rowAcc_ = std::min(rowAcc_ - steps, 1.0); // a long frame doesn't pile up steps
}

void App::autoplay(float dt) {
    if (game_.bonusReady()) game_.activateBonus();
    if (!game_.hasPiece()) { planned_ = false; return; }
    if (!planned_) {
        planned_ = game_.planMove(planRot_, planX_);
        botTimer_ = 0.25f;
        if (!planned_) return;
    }
    botTimer_ -= dt;
    if (botTimer_ > 0) return;
    botTimer_ = 0.07f;
    const Piece& p = game_.piece();
    if (p.rot != planRot_) { if (!game_.rotate(1)) planned_ = false; return; }
    if (p.x < planX_) { if (!game_.move(1)) planned_ = false; return; }
    if (p.x > planX_) { if (!game_.move(-1)) planned_ = false; return; }
    game_.hardDrop();
    planned_ = false;
}

std::vector<HudText> App::buildHud(float dt) {
    std::vector<HudText> hud;
    int w, h;
    glfwGetFramebufferSize(win_, &w, &h);
    const float s = std::max(1.f, h / 900.f);
    auto upper = [](std::string x) { for (auto& c : x) c = (char)toupper((unsigned char)c); return x; };

    if (track_) {
        float a = trackAge_ < 7.f ? smoothstepf(0, 1, trackAge_) : 0.35f;
        const Footprint& fp = track_->analysis.fp;
        hud.push_back({asciiFold(track_->title), 28 * s, 24 * s, 2.2f * s, a});
        char info[128];
        std::snprintf(info, sizeof(info), "%.0f BPM   %s", fp.bpm, keyName(fp.key, fp.minor).c_str());
        hud.push_back({info, 28 * s, 50 * s, 1.5f * s, a * 0.7f, true});
    } else {
        hud.push_back({lib_.empty() ? "NO SONGS FOUND" : "LOADING THE FIRST SONG...", w * 0.5f, h * 0.5f - 60 * s,
                       3.f * s, 0.8f, false, true});
    }
    sceneNameTimer_ = std::max(0.f, sceneNameTimer_ - dt);
    if (track_ && !track_->analysis.segments.empty()) {
        const Segment& sg = track_->analysis.segments[track_->analysis.segmentAt(songTime_)];
        hud.push_back({segmentName(sg.kind), 28 * s, h - 60.f * s, 1.3f * s, 0.3f, true});
    }
    hud.push_back({R_.targetTheme().name, 28 * s, h - 40.f * s, 1.3f * s,
                   0.12f + 0.4f * smoothstepf(0, 2, sceneNameTimer_)});

    // Score panel under the hold slot, labels above previews.
    vec2 hold = R_.project(vec3(-8.6f, 9.4f, 0)), next = R_.project(vec3(8.6f, 9.4f, 0));
    vec2 sc = R_.project(vec3(-8.6f, 1.5f, 0));
    hud.push_back({"HOLD", hold.x, hold.y, 1.5f * s, 0.45f, false, true});
    hud.push_back({"NEXT", next.x, next.y, 1.5f * s, 0.45f, false, true});
    hud.push_back({"SCORE", sc.x, sc.y, 1.4f * s, 0.45f, false, true});
    hud.push_back({std::to_string(game_.score()), sc.x, sc.y + 18 * s, 2.8f * s, 0.9f, false, true});
    hud.push_back({"LINES", sc.x, sc.y + 62 * s, 1.4f * s, 0.45f, false, true});
    hud.push_back({std::to_string(game_.lines()), sc.x, sc.y + 80 * s, 2.8f * s, 0.9f, false, true});
    hud.push_back({"LEVEL", sc.x, sc.y + 124 * s, 1.4f * s, 0.45f, false, true});
    hud.push_back({std::to_string(game_.level()) + (game_.level() >= Game::MAX_LEVEL ? " MAX" : ""), sc.x, sc.y + 142 * s,
                   2.8f * s, 0.9f, false, true});
    if (game_.best() > 0) {
        hud.push_back({"BEST", sc.x, sc.y + 186 * s, 1.4f * s, 0.35f, false, true});
        hud.push_back({std::to_string(game_.best()), sc.x, sc.y + 204 * s, 2.f * s, 0.6f, false, true});
    }
    if (game_.combo() > 0) {
        vec2 c = R_.project(vec3(8.6f, -4.5f, 0));
        hud.push_back({"COMBO " + std::to_string(game_.combo()), c.x, c.y, 2.f * s, 0.8f, true, true});
    }

    // Persistent, discreet skip hint (top right) and feedback when skipping.
    if (lib_.size() > 1) {
        const char* key = input_.active() == Input::Gamepad ? "BACK  NEXT SONG" : "N  NEXT SONG";
        hud.push_back({key, -28 * s, 24 * s, 1.4f * s, 0.3f});
    }
    skipToast_ = std::max(0.f, skipToast_ - dt);
    if (wantSkip_ || skipToast_ > 0)
        hud.push_back({wantSkip_ ? "NEXT SONG LOADING: " + asciiFold(lib_.loadingTitle()) : "NOW: " + (track_ ? asciiFold(track_->title) : std::string()), -28 * s, 44 * s, 1.4f * s,
                       wantSkip_ ? 0.8f : std::min(0.8f, skipToast_), true});

    // Seek feedback: song time and the section the song is in.
    seekToast_ = std::max(0.f, seekToast_ - dt);
    if (seekToast_ > 0 && track_) {
        const Analysis& an = track_->analysis;
        int sec = (int)songTime_;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%d:%02d  %s", sec / 60, sec % 60,
                      an.segments.empty() ? "" : segmentName(an.segments[an.segmentAt(songTime_)].kind));
        hud.push_back({buf, w * 0.5f, 24 * s, 2.f * s, std::min(1.f, seekToast_ * 2.f), true, true});
    }

    failedToast_ = std::max(0.f, failedToast_ - dt);
    if (failedToast_ > 0)
        hud.push_back({"COULD NOT LOAD " + asciiFold(failedTitle_) + ", TRYING ANOTHER SONG", -28 * s, 64 * s, 1.3f * s,
                       std::min(1.f, failedToast_), true});

    // Bonus gauge (a ring below the "next" previews): the key to press in its center once it's full.
    {
        const vec3 gc = Renderer::gaugeCenter();
        vec2 g = R_.project(gc), lbl = R_.project(gc - vec3(0, Renderer::GAUGE_R + 0.55f, 0));
        const bool pad = input_.active() == Input::Gamepad;
        if (game_.bonusReady()) {
            float br = 0.55f + 0.3f * (0.5f + 0.5f * std::sin(runTime_ * TAU * 0.4f));
            hud.push_back({pad ? "ZL ZR" : "V", g.x, g.y - 7 * s, (pad ? 1.3f : 1.8f) * s, br, true, true});
        }
        hud.push_back({game_.bonusActive() || game_.bonusReady() ? "SLOW" : "BONUS", lbl.x, lbl.y, 1.2f * s,
                       game_.bonusActive() ? 0.6f : game_.bonusReady() ? 0.5f : 0.3f, game_.bonusActive(), true});
    }
    slowToast_ = std::max(0.f, slowToast_ - dt);
    if (slowToast_ > 0) {
        vec2 c = R_.project(vec3(0, 8.5f, 0));
        float a = std::min(1.f, slowToast_) * std::min(1.f, (2.f - slowToast_) * 4.f);
        hud.push_back({"SLOW MOTION", c.x, c.y, 3.f * s, 0.75f * a, true, true});
    }
    tetrisToast_ = std::max(0.f, tetrisToast_ - dt);
    if (tetrisToast_ > 0) {
        vec2 c = R_.project(vec3(0, 6.5f, 0)); // between the slow-motion and level toasts
        float a = std::min(1.f, tetrisToast_ * 1.5f) * std::min(1.f, (2.4f - tetrisToast_) * 5.f);
        float grow = 1.f + 0.08f * smoothstepf(2.4f, 1.6f, tetrisToast_);
        hud.push_back({tetrisB2B_ ? "BACK TO BACK TETRIS" : "TETRIS", c.x, c.y, (tetrisB2B_ ? 3.4f : 4.2f) * grow * s, 0.9f * a,
                       true, true});
        hud.push_back({"+" + std::to_string(tetrisPoints_), c.x, c.y + 44 * s, 2.2f * s, 0.75f * a, false, true});
    }

    levelToast_ = std::max(0.f, levelToast_ - dt);
    if (levelToast_ > 0) {
        vec2 c = R_.project(vec3(0, 2.f, 0));
        float a = std::min(1.f, levelToast_) * std::min(1.f, (2.5f - levelToast_) * 4.f);
        hud.push_back({"LEVEL " + std::to_string(game_.level()), c.x, c.y, 3.2f * s, 0.8f * a, true, true});
    }

        // Input device toast.
    if (input_.deviceChangedTimer() > 0) {
        std::string d = input_.active() == Input::Gamepad ? "GAMEPAD  " + upper(input_.gamepadName()) : "KEYBOARD";
        hud.push_back({d, -28 * s, 70.f * s, 1.5f * s, std::min(1.f, input_.deviceChangedTimer()), true});
    }

    helpTimer_ = std::max(0.f, helpTimer_ - dt);
    float helpA = paused_ ? 0.9f : (showHelp_ ? std::min(0.6f, helpTimer_ * 0.3f) : 0.f);
    if (helpA > 0.01f) {
        // One control per line, right-aligned in the bottom-right corner, clear of the board.
        std::vector<std::string> lines;
        if (input_.active() == Input::Gamepad)
            lines = {"MOVE  DPAD / STICK", "ROTATE  A / B", "HARD DROP  UP", "HOLD  LB / RB", "SLOW MOTION  ZL / ZR", "NEW SCENE  Y",
                     "NEXT SONG  BACK", "PAUSE  START"};
        else
            lines = {"MOVE  ARROWS", "ROTATE  UP / Z", "HARD DROP  SPACE", "HOLD  C", "SLOW MOTION  V", "NEW SCENE  T",
                     "NEXT SONG  N", "SEEK  CTRL+SHIFT+ARROWS", "FULLSCREEN  F", "PAUSE  ESC"};
        const float lineH = 20.f * s, bottom = h - 40.f * s;
        for (size_t i = 0; i < lines.size(); i++)
            hud.push_back({lines[i], -28 * s, bottom - (lines.size() - 1 - i) * lineH, 1.3f * s, helpA});
    }
    if (game_.over() && overTime_ > 1.0f) {
        float a = std::min(1.f, (overTime_ - 1.0f) / 1.2f);
        hud.push_back({"GAME OVER", w * 0.5f, h * 0.5f - 70 * s, 5.f * s, 0.95f * a, true, true});
        hud.push_back({"SCORE  " + std::to_string(game_.lastScore()), w * 0.5f, h * 0.5f + 0 * s, 2.f * s, 0.85f * a, false, true});
        hud.push_back({"BEST  " + std::to_string(game_.best()), w * 0.5f, h * 0.5f + 30 * s, 1.6f * s, 0.6f * a, false, true});
        if (overTime_ > 2.5f) {
            float b = std::min(1.f, (overTime_ - 2.5f) / 0.8f);
            hud.push_back({input_.active() == Input::Gamepad ? "PRESS A BUTTON TO PLAY AGAIN" : "PRESS A KEY TO PLAY AGAIN",
                           w * 0.5f, h * 0.5f + 70 * s, 1.8f * s, 0.75f * b, false, true});
        }
    }
    if (paused_) {
        hud.push_back({"PAUSED", w * 0.5f, h * 0.5f - 40 * s, 5.f * s, 0.95f, true, true});
        hud.push_back({input_.active() == Input::Gamepad ? "START TO RESUME" : "ESC TO RESUME    Q TO QUIT",
                       w * 0.5f, h * 0.5f + 20 * s, 1.8f * s, 0.7f, false, true});
    }
    return hud;
}

int App::run() {
    runSeed_ = opt_.seed ? opt_.seed
                         : (std::random_device{}() ^ (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
    const bool shotMode = !opt_.shotPrefix.empty();

    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    Input::loadMappings();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    if (opt_.hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    if (shotMode) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWmonitor* mon = opt_.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    int ww = opt_.width, wh = opt_.height;
    if (mon) { const GLFWvidmode* vm = glfwGetVideoMode(mon); ww = vm->width; wh = vm->height; }
    win_ = glfwCreateWindow(ww, wh, "Zentris", mon, nullptr);
    if (!win_) { std::fprintf(stderr, "could not create an OpenGL 3.3 window\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win_);
    glfwSwapInterval(shotMode || opt_.hidden ? 0 : 1);
    if (!gladLoadGL(glfwGetProcAddress)) { std::fprintf(stderr, "could not load OpenGL functions\n"); return 1; }
    glGetError();

    int fbw, fbh;
    glfwGetFramebufferSize(win_, &fbw, &fbh);
    if (!R_.init(fbw, fbh)) std::fprintf(stderr, "[gl] warning: renderer init reported a GL error\n");
    input_.init(win_);

    if (!audio_.init()) std::fprintf(stderr, "[audio] continuing without sound\n");
    audio_.setMuted(opt_.mute || shotMode);
    lib_.setSampleRate(audio_.sampleRate());
    lib_.setShuffle(opt_.shuffle);
    lib_.scan(opt_.paths);
    std::printf("[app] %zu songs found\n", lib_.size());
    lib_.prefetch(runSeed_);

    game_.reset(runSeed_);
    wipeRng_ = Rng(runSeed_ ^ 0x77195EEDull);
    baseTheme_ = generateTheme(Footprint{}, runSeed_);
    R_.setTheme(baseTheme_, 0.01f);

    double last = glfwGetTime();
    float fade = 0.f;
    int shotsTaken = 0;
    float shotSettle = 0;
    while (!glfwWindowShouldClose(win_) && !quit_) {
        double now = glfwGetTime();
        float dt = shotMode ? 1.f / 60.f : (float)std::min(0.1, now - last);
        last = now;
        runTime_ += dt;
        glfwPollEvents();
        input_.update(dt);

        // ---- Songs.
        if (!nextTrack_) {
            bool wasLoading = lib_.loading();
            std::string tried = lib_.loadingTitle();
            nextTrack_ = lib_.takeReady();
            if (nextTrack_) std::printf("[app] next song ready: %s\n", nextTrack_->title.c_str());
            // A failed load (e.g. an unavailable video) just moves on to another song.
            if (!nextTrack_ && wasLoading && !lib_.loading()) {
                std::printf("[app] could not load %s, trying another song\n", tried.c_str());
                failedTitle_ = tried;
                failedToast_ = 4.f;
            }
            if (!nextTrack_ && !lib_.loading() && !lib_.empty()) lib_.prefetch(runSeed_ + (uint64_t)(runTime_ * 1000));
        }
        if (!track_ && nextTrack_) startTrack(std::move(nextTrack_));
        if (track_ && nextTrack_ && (audio_.finished() || wantSkip_)) {
            wantSkip_ = false;
            skipToast_ = 0;
            startTrack(std::move(nextTrack_));
        }
        if (shotMode && track_) {
            if (opt_.phaseShots) {
                // Settle for 12 s ending 2 s before the next phase change (or the song end).
                int k = std::min(shotsTaken, (int)plan_.phases.size() - 1);
                double end = k + 1 < (int)plan_.phases.size() ? plan_.phases[k + 1].start : track_->duration();
                if (simSongTime_ < end - 14.0) simSongTime_ = std::max(plan_.phases[k].start, end - 14.0);
            } else if (simSongTime_ < opt_.shotTime - 12.0) simSongTime_ = opt_.shotTime - 12.0;
            simSongTime_ += dt * audio_.currentSpeed();
            songTime_ = simSongTime_;
        } else {
            songTime_ = audio_.position();
        }

        // ---- Global controls.
        // Until the first song is ready the game waits, paused (no play without music).
        const bool waiting = !track_;
        if (input_.pressed(A_PAUSE) && !game_.over() && !waiting) {
            paused_ = !paused_;
            audio_.setPaused(paused_);
        }
        if (input_.pressed(A_QUIT) && paused_) quit_ = true;
        if (input_.pressed(A_FULLSCREEN)) toggleFullscreen();
        // Test hook: ZEN_TEST_SKIP=<seconds> presses "next song" periodically.
        static const double testSkip = std::getenv("ZEN_TEST_SKIP") ? std::atof(std::getenv("ZEN_TEST_SKIP")) : 0.0;
        const bool testSkipNow = testSkip > 0 && std::fmod(runTime_, testSkip) < dt && runTime_ > dt;
        if (testSkipNow) std::printf("[test] skip pressed (next ready: %s)\n", nextTrack_ ? "yes" : "no");
        if (input_.pressed(A_NEXT_SONG) && !testSkipNow)
            std::printf("[input] next song pressed (%zu songs, next %s)\n", lib_.size(),
                        nextTrack_ ? ("ready: " + nextTrack_->title).c_str() : ("loading: " + lib_.loadingTitle()).c_str());
        if ((input_.pressed(A_NEXT_SONG) || testSkipNow) && lib_.size() > 1) {
            wantSkip_ = true;
            skipToast_ = 2.5f;
        }
        if (input_.pressed(A_NEW_SCENE)) newScene(2.5f);
        // Ctrl+Shift+Left/Right: jump 10 s in the song (repeats while held) to test sections quickly.
        {
            int dir = input_.repeatSteps(A_SEEK_FWD) - input_.repeatSteps(A_SEEK_BACK);
            if (dir != 0 && track_ && !audio_.finished()) {
                double target = std::clamp(audio_.position() + 10.0 * dir, 0.0, track_->duration() - 1.0);
                audio_.seek(target);
                songTime_ = target;
                simSongTime_ = target;
                pace_ = -1; // re-sync the beat-driven gravity
                lastSongBeat_ = -1;
                lastSeg_ = track_->analysis.segmentAt(target);
                seekToast_ = 1.5f;
            }
        }
        if (input_.pressed(A_DEBUG_LEVEL) && !game_.over()) game_.skipToNextLevel();
        if (input_.pressed(A_DEBUG_BONUS)) game_.debugChargeBonus();
        if (input_.pressed(A_HUD)) { showHelp_ = !showHelp_; helpTimer_ = showHelp_ ? 1e9f : 0; }

        // ---- Game.
        if (!paused_ && !waiting) {
            trackAge_ += dt;
            if (opt_.autoplay || shotMode) {
                autoplay(dt);
            } else {
                for (int i = 0; i < input_.repeatSteps(A_LEFT); i++) game_.move(-1);
                for (int i = 0; i < input_.repeatSteps(A_RIGHT); i++) game_.move(1);
                for (int i = 0; i < input_.repeatSteps(A_SOFT); i++) game_.softDrop();
                if (input_.pressed(A_CW)) game_.rotate(1);
                if (input_.pressed(A_CCW)) game_.rotate(-1);
                if (input_.pressed(A_HOLD)) game_.hold();
                if (input_.pressed(A_HARD)) game_.hardDrop();
                if (input_.pressed(A_BONUS)) game_.activateBonus();
            }
            if (game_.over()) {
                // Game over: the board dissolves, then any gameplay key/button starts a new game.
                overTime_ += dt;
                bool any = false;
                for (Action a : {A_LEFT, A_RIGHT, A_SOFT, A_HARD, A_CW, A_CCW, A_HOLD, A_PAUSE}) any |= input_.pressed(a);
                if ((any && overTime_ > 2.5f) || ((opt_.autoplay || shotMode) && overTime_ > 4.f)) {
                    game_.restart();
                    overTime_ = 0;
                }
            } else {
                updateGravity(songTime_, dt);
            }
            game_.update(dt);
            handleEvents();
            if (game_.level() > lastLevel_ && !game_.over()) {
                levelToast_ = 2.5f;
                R_.setTheme(R_.latestTheme(), 1.6f, true, 0.35f, pickWipe(2)); // soft refresh wave
                R_.levelUp();
                std::printf("[app] level %d\n", game_.level());
            }
            lastLevel_ = game_.level();
            R_.setDim(game_.over() && overTime_ > 1.2f);
            // Slow motion: a short tape bend of the song marks it (gravity's own slow-down is in updateGravity).
            if (game_.bonusActive() != slowWas_) {
                slowWas_ = game_.bonusActive();
                audio_.bend(-0.15f, 0.9f); // a very slight slow-down and back, at the start and at the end
                // The screen's colors are reversed for the whole bonus (a front sweeps it in and out).
                R_.setInverted(slowWas_);
            }
        }

        // ---- Music-driven state.
        if (track_) {
            const Analysis& an = track_->analysis;
            double t = songTime_ - 0.03;
            // Visuals only follow slow envelopes (~1 s): no per-beat hits, no transient flicker.
            FrameFeatures f = an.at(t);
            const float rate = 1.2f;
            for (int b = 0; b < NUM_BANDS; b++) music_.audio.bands[b] = approach(music_.audio.bands[b], f.bands[b], rate, dt);
            music_.audio.bass = approach(music_.audio.bass, f.bass, rate, dt);
            music_.audio.mid = approach(music_.audio.mid, f.mid, rate, dt);
            music_.audio.high = approach(music_.audio.high, f.high, rate, dt);
            music_.audio.loud = approach(music_.audio.loud, f.loud, rate, dt);
            music_.audio.onset = 0;
            music_.intensity = approach(music_.intensity, an.intensityAt(t), 0.5f, dt);
            // Slow breathing over two bars (8 beats) instead of a pulse on every beat.
            double bp = an.beatPosition(t);
            music_.beatPhase = (float)(bp - std::floor(bp));
            float breath = 0.5f - 0.5f * std::cos(TAU * (float)std::fmod(bp / 8.0, 1.0));
            // Beat pulses only inside rare accent windows, as a soft swell (no hard hits).
            float swell = 0.5f + 0.5f * std::cos(TAU * music_.beatPhase);
            // Pulses only inside pulse zones (peak sections, drops); stronger for faster songs.
            const float tempoAmp = pulseTempoAmp(an.fp.bpm);
            const float zone = plan_.pulseEnvelope(t);
            float accent = zone * tempoAmp * swell * swell * (0.4f + 0.6f * std::max(f.bass, f.onset));
            // Soft hits on strong transients, for faster songs, still only inside pulse zones.
            // Overall energy: faster songs, intense sections and higher levels all push it up.
            const int lvl = plan_.phases[plan_.phaseAt(t)].level;
            music_.energy = approach(music_.energy,
                                     saturate((0.3f + 0.7f * smoothstepf(80.f, 135.f, an.fp.bpm)) *
                                              (0.45f + 0.55f * music_.intensity) * (1.f + 0.25f * game_.difficulty())),
                                     1.f, dt);
            const float E = music_.energy;
            // Outside pulse zones, energetic songs still pulse on the beat in mid/peak parts.
            float base = (lvl >= 1 ? 0.5f : 0.15f) * E * smoothstepf(95.f, 130.f, an.fp.bpm);
            float groove = base * swell * swell * swell * (0.5f + 0.5f * f.bass);
            float hitTarget = std::max(zone, lvl >= 1 ? 0.5f * E : 0.f) * smoothstepf(92.f, 120.f, an.fp.bpm) *
                              smoothstepf(0.45f, 0.9f, f.onset);
            hit_ = hitTarget > hit_ ? approach(hit_, hitTarget, 30.f, dt) : approach(hit_, hitTarget, 3.f, dt);
            music_.beatPulse = 0.25f * breath + accent + groove + 0.7f * hit_;
            // Bass kick envelope (pushes the particle field) and fast band levels (equalizer).
            float kickTarget = E * smoothstepf(0.45f, 0.85f, f.onset) * (0.4f + 0.6f * f.bass);
            music_.kick = kickTarget > music_.kick ? approach(music_.kick, kickTarget, 30.f, dt)
                                                   : approach(music_.kick, kickTarget, 4.f, dt);
            for (int b = 0; b < NUM_BANDS; b++)
                music_.bandsFast[b] = f.bands[b] > music_.bandsFast[b] ? approach(music_.bandsFast[b], f.bands[b], 25.f, dt)
                                                                       : approach(music_.bandsFast[b], f.bands[b], 6.f, dt);

            // Structure-driven profile (density, speed, glow, saturation), eased over a few seconds.
            {
                const StructureProfile prof = structureProfile(an, t + 1.2); // look ahead: reach the new section's feel on time
                const float ease = 1.0f; // ~1 s, smooth but on time
                music_.density = approach(music_.density, prof.density, ease, dt);
                music_.speed = approach(music_.speed, prof.speed, ease, dt);
                music_.glow = approach(music_.glow, prof.glow, ease, dt);
                music_.saturation = approach(music_.saturation, prof.saturation, ease, dt);
            }
            if (audio_.finished()) {
                music_.beatPulse = 0;
                music_.intensity = approach(music_.intensity, 0.2f, 0.5f, dt);
            }
            // The song is known in advance: start the ripple early so it crosses the board on the boundary.
            // Soft refresh: a new section inside the same scene (chorus -> chorus, verse -> verse) is marked
            // by a light wave and a small glow lift; colors and layout stay the same.
            if (!an.segments.empty()) {
                int seg = an.segmentAt(songTime_ + 0.45);
                if (seg > lastSeg_) {
                    const Segment& sg = an.segments[seg];
                    const ScenePhase& ph = plan_.phases[plan_.phaseAt(sg.start + 0.01)];
                    bool phaseStart = std::fabs(ph.start - sg.start) < 0.05;
                    if (!phaseStart && sg.end - sg.start >= 8.0) {
                        float strength = (sg.kind == SEG_CHORUS || sg.kind == SEG_DROP) ? 0.5f : 0.3f;
                        R_.setTheme(R_.latestTheme(), 1.6f, true, strength, pickWipe(2));
                        std::printf("[app] refresh (%s)\n", segmentName(sg.kind));
                    }
                    lastSeg_ = seg;
                } else if (seg < lastSeg_) {
                    lastSeg_ = seg; // seeked back
                }
            }
            // Scene changes are anticipated so the wave crosses the board on the boundary. Rising changes
            // (into mid, and above all into a peak/drop) are quick and marked by a ring and a glow swell;
            // falling ones (into a break or calm part) are slow and soft.
            {
                int cur = std::clamp(lastPhase_, 0, (int)plan_.phases.size() - 1);
                int nxt = plan_.phaseAt(songTime_ + 1.5);
                if (nxt != cur) {
                    const ScenePhase& to = plan_.phases[nxt];
                    bool rising = to.level > plan_.phases[cur].level;
                    double lead = rising ? 0.45 : 1.2;
                    if (songTime_ + lead >= to.start || nxt < cur) {
                        lastPhase_ = nxt;
                        float impact = rising ? (to.level >= 2 ? 1.f : 0.5f) : 0.f;
                        R_.setTheme(phaseTheme(nxt), rising ? 1.8f : 4.5f, true, impact, pickWipe(rising ? 0 : 1));
                        lastSeg_ = to.seg;
                        const Segment& sg = an.segments[to.seg];
                        std::printf("[app] scene level %d (%s, energy %.2f)%s\n", to.level, segmentName(sg.kind),
                                    sg.energy, rising ? " rising" : "");
                    }
                }
            }
        } else {
            music_.intensity = 0.2f;
            music_.beatPhase = (float)std::fmod(runTime_, 1.0);
            music_.beatPulse = 0;
        }

        glfwGetFramebufferSize(win_, &fbw, &fbh);
        static int lastW = 0, lastH = 0;
        if ((fbw != lastW || fbh != lastH) && fbw > 0 && fbh > 0) {
            R_.resize(fbw, fbh);
            lastW = fbw;
            lastH = fbh;
        }
        fade = std::min(1.f, fade + dt * 0.8f);
        if (fbw > 0 && fbh > 0) R_.render(game_, music_, runTime_, dt, paused_ || waiting, buildHud(dt), shotMode ? 1.f : fade);

        if (shotMode && track_) {
            // Test hook: ZEN_FX=slow:N or tetris:N plays that special animation ZEN_FX_AT seconds before each shot.
            static const char* fxEnv = std::getenv("ZEN_FX");
            static const float fxAt = std::getenv("ZEN_FX_AT") ? (float)std::atof(std::getenv("ZEN_FX_AT")) : 6.f;
            if (fxEnv && shotSettle < 12.f - fxAt && shotSettle + dt >= 12.f - fxAt) {
                int idx = std::atoi(std::strchr(fxEnv, ':') ? std::strchr(fxEnv, ':') + 1 : "0") + shotsTaken;
                if (std::strncmp(fxEnv, "slow", 4) == 0) {
                    R_.forceSpecialFx(idx, -1);
                    game_.debugChargeBonus();
                } else {
                    R_.forceSpecialFx(-1, idx);
                    GameEvent ev{GameEvent::Clear};
                    ev.count = 4;
                    for (int y = 18; y < 22; y++)
                        for (int x = 0; x < Game::W; x++) ev.cells.push_back({x, y, (x + y) % 7});
                    ev.points = 1200;
                    tetrisToast_ = 2.4f;
                    tetrisPoints_ = 1200;
                    R_.onEvent(ev, game_);
                }
            }
            shotSettle += dt;
            if (shotSettle > 12.f) {
                char name[512];
                std::snprintf(name, sizeof(name), "%s%02d.png", opt_.shotPrefix.c_str(), shotsTaken);
                R_.screenshot(name);
                std::printf("[shot] %s  <- %s\n", name, R_.targetTheme().name.c_str());
                shotsTaken++;
                shotSettle = 0;
                if (shotsTaken >= opt_.shotCount) break;
                if (opt_.phaseShots) {
                    if (shotsTaken >= (int)plan_.phases.size()) break;
                    glfwSwapBuffers(win_);
                    continue;
                }
                newScene(0.01f);
                if (shotsTaken % 2 == 0 && nextTrack_) startTrack(std::move(nextTrack_));
                simSongTime_ = 0;
            }
        }
        glfwSwapBuffers(win_);
        // Debug: ZEN_FPS=1 prints the average frame time every 2 s.
        static const bool showFps = std::getenv("ZEN_FPS") != nullptr;
        if (showFps) {
            static double acc = 0, t0 = glfwGetTime();
            static int frames = 0;
            frames++;
            double tn = glfwGetTime();
            if (tn - t0 > 2.0) {
                std::printf("[fps] %.1f fps (%.2f ms/frame)\n", frames / (tn - t0), 1000.0 * (tn - t0) / frames);
                frames = 0;
                t0 = tn;
            }
            (void)acc;
        }
    }
    audio_.shutdown();
    glfwDestroyWindow(win_);
    glfwTerminate();
    return 0;
}

int main(int argc, char** argv) {
    platform::lineBufferStdout();
    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--autoplay") o.autoplay = true;
        else if (a == "--mute") o.mute = true;
        else if (a == "--shuffle") o.shuffle = true; // the default; kept for older command lines
        else if (a == "--in-order") o.shuffle = false;
        else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--hidden") o.hidden = true;
        else if (a == "--seed" && i + 1 < argc) o.seed = std::stoull(argv[++i]);
        else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &o.width, &o.height);
        else if (a == "--shots" && i + 2 < argc) { o.shotPrefix = argv[++i]; o.shotCount = std::atoi(argv[++i]); }
        else if (a == "--phase-shots" && i + 1 < argc) { o.shotPrefix = argv[++i]; o.shotCount = 99; o.phaseShots = true; }
        else if (a == "--shot-time" && i + 1 < argc) o.shotTime = std::stof(argv[++i]);
        else o.paths.push_back(a);
    }
    // No songs given: the default playlist.
    if (o.paths.empty()) o.paths.push_back(DEFAULT_PLAYLIST);
    App app(o);
    int rc = app.run();
    // Exit now: don't wait for background downloads/analysis (an unfinished download resumes next time).
    std::fflush(stdout);
    std::_Exit(rc);
}
