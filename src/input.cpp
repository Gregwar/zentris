#include "input.hpp"

#include "platform.hpp"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
Input* g_input = nullptr;
bool g_joystickEvent = false;
void joystickCallback(int, int) { g_joystickEvent = true; }

// Auto-repeat: delay before repeating, then the repeat interval (close to modern guideline handling).
constexpr float DAS = 0.13f, ARR = 0.033f, SOFT_RATE = 0.033f;
} // namespace

void Input::loadMappings() {
    namespace fs = std::filesystem;
    fs::path exe = platform::executableDir();
    std::vector<fs::path> candidates = {exe / "gamecontrollerdb.txt"};
#ifdef ZEN_SOURCE_DIR
    candidates.push_back(fs::path(ZEN_SOURCE_DIR) / "third_party" / "gamecontrollerdb.txt");
#endif
    for (const auto& p : candidates) {
        std::ifstream f(p);
        if (!f) continue;
        std::stringstream ss;
        ss << f.rdbuf();
        if (glfwUpdateGamepadMappings(ss.str().c_str())) {
            std::printf("[input] gamepad mappings loaded from %s\n", p.string().c_str());
            break;
        }
    }
    // User additions, SDL style.
    if (const char* extra = std::getenv("SDL_GAMECONTROLLERCONFIG")) glfwUpdateGamepadMappings(extra);
}

void Input::init(GLFWwindow* w) {
    win_ = w;
    // Letter shortcuts follow the keyboard layout (e.g. Z is Z on AZERTY): find which physical key
    // produces each letter; default to the US position.
    for (int c = 0; c < 26; c++) letter_[c] = GLFW_KEY_A + c;
    for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_GRAVE_ACCENT; k++) {
        const char* name = glfwGetKeyName(k, 0);
        if (name && name[0] && !name[1] && name[0] >= 'a' && name[0] <= 'z') letter_[name[0] - 'a'] = k;
    }
    g_input = this;
    glfwSetJoystickCallback(joystickCallback);
    detectGamepad();
    if (jid_ >= 0) {
        active_ = Gamepad;
        changedTimer_ = 3.f;
    }
}

void Input::detectGamepad() {
    int old = jid_;
    jid_ = -1;
    padName_.clear();
    // Prefer a controller with a known mapping; otherwise use any joystick with a generic layout.
    for (int pass = 0; pass < 2 && jid_ < 0; pass++)
        for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; j++) {
            if (!glfwJoystickPresent(j)) continue;
            bool isPad = glfwJoystickIsGamepad(j);
            if (pass == 0 && !isPad) continue;
            jid_ = j;
            mapped_ = isPad;
            const char* n = isPad ? glfwGetGamepadName(j) : glfwGetJoystickName(j);
            padName_ = n ? n : "GAMEPAD";
            if (!isPad) std::printf("[input] %s has no known mapping: using a generic layout\n", padName_.c_str());
            break;
        }
    if (jid_ != old) {
        if (jid_ >= 0) {
            std::printf("[input] gamepad connected: %s\n", padName_.c_str());
            active_ = Gamepad;
        } else {
            std::printf("[input] gamepad disconnected, using keyboard\n");
            active_ = Keyboard;
        }
        changedTimer_ = 3.f;
    }
}

void Input::update(float dt) {
    if (g_joystickEvent) {
        g_joystickEvent = false;
        detectGamepad();
    }
    changedTimer_ = std::max(0.f, changedTimer_ - dt);
    for (int i = 0; i < A_COUNT; i++) prev_[i] = down_[i];

    auto key = [&](int k) { return glfwGetKey(win_, k) == GLFW_PRESS; };
    auto letter = [&](char c) { return key(letter_[c - 'a']); };
    bool kb[A_COUNT] = {};
    const bool ctrl = key(GLFW_KEY_LEFT_CONTROL) || key(GLFW_KEY_RIGHT_CONTROL);
    const bool shift = key(GLFW_KEY_LEFT_SHIFT) || key(GLFW_KEY_RIGHT_SHIFT);
    const bool seekChord = ctrl && shift;
    // Ctrl+Shift+Left/Right seeks in the song; the arrows then don't move the piece.
    kb[A_SEEK_BACK] = seekChord && key(GLFW_KEY_LEFT);
    kb[A_SEEK_FWD] = seekChord && key(GLFW_KEY_RIGHT);
    kb[A_LEFT] = (!seekChord && key(GLFW_KEY_LEFT)) || letter('a');
    kb[A_RIGHT] = (!seekChord && key(GLFW_KEY_RIGHT)) || letter('d');
    kb[A_SOFT] = key(GLFW_KEY_DOWN) || letter('s');
    kb[A_HARD] = key(GLFW_KEY_SPACE) || letter('w');
    kb[A_CW] = key(GLFW_KEY_UP) || letter('x') || letter('k');
    kb[A_CCW] = letter('z') || letter('j');
    // Shift holds only when tapped alone (released without Ctrl or an arrow), so it can be part of the
    // Ctrl+Shift seek chord.
    bool shiftTap = false;
    if (shift) {
        if (ctrl || key(GLFW_KEY_LEFT) || key(GLFW_KEY_RIGHT)) shiftChord_ = true;
    } else if (shiftWasDown_) {
        shiftTap = !shiftChord_;
        shiftChord_ = false;
    }
    shiftWasDown_ = shift;
    kb[A_HOLD] = letter('c') || shiftTap;
    kb[A_BONUS] = letter('v') || letter('e');
    kb[A_PAUSE] = key(GLFW_KEY_ESCAPE) || letter('p');
    kb[A_NEXT_SONG] = letter('n') || key(GLFW_KEY_TAB) || key(GLFW_KEY_PAGE_DOWN) || key(GLFW_KEY_ENTER);
    kb[A_NEW_SCENE] = letter('t');
    kb[A_FULLSCREEN] = key(GLFW_KEY_F11) || letter('f');
    kb[A_QUIT] = letter('q');
    kb[A_HUD] = letter('h');
    kb[A_DEBUG_LEVEL] = letter('l');
    kb[A_DEBUG_BONUS] = letter('b');

    bool pad[A_COUNT] = {};
    if (jid_ >= 0) {
        GLFWgamepadstate st;
        bool ok = mapped_ && glfwGetGamepadState(jid_, &st);
        if (!mapped_) {
            // Generic layout for unknown controllers: axes 0/1 = left stick, hat 0 = D-pad, face buttons 0-3,
            // shoulders 4-5, select/start 8-9 (the most common HID ordering).
            int na = 0, nb = 0, nh = 0;
            const float* ax = glfwGetJoystickAxes(jid_, &na);
            const unsigned char* bt = glfwGetJoystickButtons(jid_, &nb);
            const unsigned char* ht = glfwGetJoystickHats(jid_, &nh);
            if (ax && bt) {
                std::memset(&st, 0, sizeof(st));
                for (int i = 0; i < 6 && i < na; i++) st.axes[i] = ax[i];
                // Triggers are usually buttons 6/7 on such controllers.
                st.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = 6 < nb && bt[6] ? 1.f : -1.f;
                st.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = 7 < nb && bt[7] ? 1.f : -1.f;
                auto B = [&](int i) { return (unsigned char)(i < nb ? bt[i] : 0); };
                st.buttons[GLFW_GAMEPAD_BUTTON_A] = B(1);
                st.buttons[GLFW_GAMEPAD_BUTTON_B] = B(2);
                st.buttons[GLFW_GAMEPAD_BUTTON_X] = B(0);
                st.buttons[GLFW_GAMEPAD_BUTTON_Y] = B(3);
                st.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] = B(4);
                st.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] = B(5);
                st.buttons[GLFW_GAMEPAD_BUTTON_BACK] = B(8);
                st.buttons[GLFW_GAMEPAD_BUTTON_START] = B(9);
                unsigned char h = nh > 0 && ht ? ht[0] : 0;
                st.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP] = (h & GLFW_HAT_UP) ? 1 : 0;
                st.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] = (h & GLFW_HAT_DOWN) ? 1 : 0;
                st.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] = (h & GLFW_HAT_LEFT) ? 1 : 0;
                st.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] = (h & GLFW_HAT_RIGHT) ? 1 : 0;
                ok = true;
            }
        }
        if (ok) {
            auto b = [&](int i) { return st.buttons[i] == GLFW_PRESS; };
            float ax = st.axes[GLFW_GAMEPAD_AXIS_LEFT_X], ay = st.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
            float lt = st.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER], rt = st.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER];
            // Stick: dominant axis only, to avoid accidental diagonals.
            bool sx = std::fabs(ax) > 0.5f && std::fabs(ax) > std::fabs(ay);
            bool sy = std::fabs(ay) > 0.5f && std::fabs(ay) >= std::fabs(ax);
            pad[A_LEFT] = b(GLFW_GAMEPAD_BUTTON_DPAD_LEFT) || (sx && ax < 0);
            pad[A_RIGHT] = b(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT) || (sx && ax > 0);
            pad[A_SOFT] = b(GLFW_GAMEPAD_BUTTON_DPAD_DOWN) || (sy && ay > 0);
            pad[A_HARD] = b(GLFW_GAMEPAD_BUTTON_DPAD_UP) || (sy && ay < -0.8f);
            pad[A_CW] = b(GLFW_GAMEPAD_BUTTON_A);
            pad[A_CCW] = b(GLFW_GAMEPAD_BUTTON_B) || b(GLFW_GAMEPAD_BUTTON_X);
            pad[A_HOLD] = b(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER) || b(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
            pad[A_BONUS] = lt > 0.5f || rt > 0.5f;
            pad[A_NEW_SCENE] = b(GLFW_GAMEPAD_BUTTON_Y);
            pad[A_PAUSE] = b(GLFW_GAMEPAD_BUTTON_START);
            pad[A_NEXT_SONG] = b(GLFW_GAMEPAD_BUTTON_BACK);
            pad[A_HUD] = b(GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
        }
    }

    bool padAny = false, keyAny = false;
    for (int i = 0; i < A_COUNT; i++) {
        padAny |= pad[i];
        keyAny |= kb[i];
        down_[i] = kb[i] || pad[i];
    }
    // Switch the active device on a fresh press from the other one.
    if (padAny && !prevPadAny_ && active_ != Gamepad) { active_ = Gamepad; changedTimer_ = 2.f; }
    if (keyAny && !prevKeyAny_ && active_ != Keyboard) { active_ = Keyboard; changedTimer_ = 2.f; }
    prevPadAny_ = padAny;
    prevKeyAny_ = keyAny;

    // Auto-repeat.
    for (int i = 0; i < A_COUNT; i++) {
        repeat_[i] = 0;
        if (!down_[i]) { held_[i] = 0; acc_[i] = 0; continue; }
        if (!prev_[i]) { repeat_[i] = 1; held_[i] = 0; acc_[i] = 0; continue; }
        held_[i] += dt;
        const bool seek = i == A_SEEK_BACK || i == A_SEEK_FWD;
        float delay = (i == A_SOFT) ? 0.f : seek ? 0.35f : DAS;
        float rate = (i == A_SOFT) ? SOFT_RATE : seek ? 0.2f : ARR;
        if (held_[i] >= delay) {
            acc_[i] += dt;
            while (acc_[i] >= rate) { acc_[i] -= rate; repeat_[i]++; }
        }
    }
    // Opposite directions: latest wins.
    if (down_[A_LEFT] && down_[A_RIGHT]) {
        if (held_[A_LEFT] < held_[A_RIGHT]) repeat_[A_RIGHT] = 0; else repeat_[A_LEFT] = 0;
    }
}
