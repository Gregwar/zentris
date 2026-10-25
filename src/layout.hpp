#pragma once
// The common layout contract: every universe puts the well, the previews, the gauge and the HUD anchors at the
// same screen places, so a change of universe keeps the stack where it is (only its material and world change).
//
// Board space: the well's cells lie on the plane z = 0, one unit per cell, centered on the origin: x in [-5, 5]
// (10 columns), y in [-10, 10] (20 visible rows), y up. Universes draw the board in board space through the
// reference camera below (or one that maps the z = 0 plane to the same pixels: 3D universes may move their world
// and their light, but the well stays put). A universe may let its camera breathe slightly (VECTOR dollies by up
// to ~4% at peaks): main.cpp places the HUD through the universe's own project().
#include "game.hpp"
#include "mathutil.hpp"

namespace layout {

constexpr float BOARD_W = 10.f, BOARD_H = 20.f;

// Center of a board cell (game coordinates: x 0..9, y 0..21 from the top, the first HIDDEN rows hidden).
inline vec3 cellPos(float x, float y) {
    return {x - (BOARD_W - 1) * 0.5f, (float)(Game::H - 1) - y - (BOARD_H - 1) * 0.5f, 0.f};
}

// Anchors (board space, z = 0).
const vec3 HOLD_CENTER(-8.6f, 7.2f, 0.f);                 // the held piece
inline vec3 nextCenter(int i) { return {8.6f, 7.2f - i * 3.2f, 0.f}; } // the three next pieces
inline float previewScale(int i) { return i == 0 ? 0.62f : 0.5f; }     // hold uses previewScale(0)
const vec3 HOLD_LABEL(-8.6f, 9.4f, 0.f), NEXT_LABEL(8.6f, 9.4f, 0.f);
const vec3 SCORE_ANCHOR(-8.6f, 1.5f, 0.f);                // score / lines / level column (top of it)
const vec3 COMBO_ANCHOR(8.6f, -4.5f, 0.f);
const vec3 GAUGE_CENTER(8.6f, -2.1f, 0.f);                // the bonus gauge (a ring of radius GAUGE_R)
constexpr float GAUGE_R = 1.f;
const vec3 SLOW_TOAST(0.f, 8.5f, 0.f), TETRIS_TOAST(0.f, 6.5f, 0.f), LEVEL_TOAST(0.f, 2.f, 0.f);
// The HUD columns (board space x0, y0, x1, y1): keep the world quiet behind them.
constexpr float LEFT_COLUMN[4] = {-11.2f, -6.8f, -6.0f, 10.4f};
constexpr float RIGHT_COLUMN[4] = {6.0f, -3.6f, 11.2f, 10.4f};

// The reference camera: a straight front view framing the board and both HUD columns.
struct Camera {
    mat4 view, proj, vp;
    vec3 eye, target;
    float fovDeg = 38.f, dist = 0.f;
};
// distScale < 1 moves closer (a dolly), yaw turns around the board (radians).
Camera camera(int width, int height, float distScale = 1.f, float yaw = 0.f, float zNear = 0.5f, float zFar = 800.f);
// Board space -> pixels (top-left origin) through a view-projection.
vec2 project(const mat4& vp, const vec3& p, int width, int height);
// Board space -> pixels through the reference camera.
vec2 project(const vec3& p, int width, int height);

struct Rect { float x0, y0, x1, y1; }; // pixels, top-left origin
// The well (its 10 x 20 cells) on screen, through the reference camera.
Rect boardRect(int width, int height);

} // namespace layout
