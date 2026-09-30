#pragma once
// Unified keyboard / gamepad input. Both always work; the "active" device (the one used last)
// decides which control hints are shown. Gamepads are detected at startup and hot-plugged.
#include <string>

struct GLFWwindow;

enum Action {
    A_LEFT, A_RIGHT, A_SOFT, A_HARD, A_CW, A_CCW, A_HOLD,
    A_PAUSE, A_NEXT_SONG, A_NEW_SCENE, A_FULLSCREEN, A_QUIT, A_HUD,
    A_SEEK_BACK, A_SEEK_FWD, // Ctrl+Shift+Left/Right: seek in the song (testing)
    A_DEBUG_LEVEL,           // L: jump to the next level (debugging)
    A_COUNT
};

class Input {
public:
    enum Device { Keyboard, Gamepad };

    void init(GLFWwindow* w);
    // Loads SDL_GameControllerDB mappings so many more controllers are recognised as gamepads.
    static void loadMappings();
    void update(float dt);

    bool down(Action a) const { return down_[a]; }
    bool pressed(Action a) const { return down_[a] && !prev_[a]; }
    // Auto-repeat for movement (DAS / ARR): returns number of steps to apply this frame.
    int repeatSteps(Action a) const { return repeat_[a]; }

    Device active() const { return active_; }
    bool gamepadConnected() const { return jid_ >= 0; }
    const std::string& gamepadName() const { return padName_; }
    // Set briefly when the device changes, for a HUD toast.
    float deviceChangedTimer() const { return changedTimer_; }

private:
    void detectGamepad();
    GLFWwindow* win_ = nullptr;
    bool down_[A_COUNT] = {}, prev_[A_COUNT] = {};
    int repeat_[A_COUNT] = {};
    float held_[A_COUNT] = {}, acc_[A_COUNT] = {};
    int jid_ = -1;
    bool mapped_ = true; // false: raw joystick without a known mapping (generic layout)
    std::string padName_;
    Device active_ = Keyboard;
    float changedTimer_ = 0;
    bool prevPadAny_ = false, prevKeyAny_ = false;
    bool shiftWasDown_ = false, shiftChord_ = false; // Shift holds on a clean tap only
    int letter_[26] = {}; // physical key producing each letter on the current keyboard layout
};
