#pragma once
// The transitions between universes: a reusable library of compositing transitions, universe-agnostic. Each is a
// function of the outgoing and incoming frames (two textures), the progress, the board rectangle (the layout
// contract: both frames have the stack at the same pixels), both universes' TransitionHints and the music. They
// are driven only by that data, so any universe pairs with any other without pair-specific code.
//
// Both universes also travel through the bridge (universe.hpp): the outgoing one dissolves toward it, the incoming
// one emerges from it, so the two halves meet in a shared neutral state (the stack in plain ink over a color
// blended from both backgrounds). Universes that don't draw the bridge natively get it from the host: the stack
// is drawn plainly from the game's cells (uCells).
#include <string>

#include <glad/gl.h>

#include "layout.hpp"
#include "universe.hpp"

namespace transitions {

enum Kind { REBUILD, RIPPLE, INK, CURTAIN, LUMA, TILES, COUNT };

int count();
const char* name(int kind);
unsigned style(int kind);  // its TransitionStyle bit
int find(const std::string& name); // -1 if unknown (any case)
// How far both universes go toward the bridge in the middle of this transition (0..1).
float bridgeDepth(int kind);

// Why the universe changes: picks favour fitting transitions.
enum Reason { SONG_CHANGE, NEW_SCENE, PHASE_PEAK };
// A transition for a change from a universe to another: weighted by both universes' preferred styles, by how
// different their brightness is, and by the reason; never `last` twice in a row.
int pick(const TransitionHints& a, const TransitionHints& b, Reason why, uint64_t seed, int last);

// The bridge a universe goes to (leaving) or comes from (entering) in a change with `other`: its world gone, the
// stack plain in an ink that reads, over its background leaning toward the other's (amount left at 0).
Bridge bridgeFor(const TransitionHints& self, const TransitionHints& other, bool entering);

struct Inputs {
    GLuint texA = 0, texB = 0; // outgoing / incoming frames (display-encoded RGBA)
    GLuint cells = 0;          // 10 x 20 R8 texture: the stack (1), the falling piece (0.7), rows from the bottom
    int width = 1, height = 1;
    float progress = 0;        // 0 .. 1
    float beats = 8;           // the transition's length in beats (fronts step on its beats)
    TransitionHints a, b;
    Bridge bridgeA, bridgeB;   // each side's bridge (color / ink)
    float hostBridgeA = 0, hostBridgeB = 0; // bridge amounts the host draws (for universes not bridgeable)
    float kick = 0, beatPhase = 0;
    double time = 0;
    uint64_t seed = 0;
};

class Compositor {
public:
    ~Compositor();
    bool init();
    // Draws the composite into the bound framebuffer (viewport set by the caller).
    void draw(int kind, const Inputs& in);

private:
    GLuint progs_[COUNT] = {};
};

} // namespace transitions
