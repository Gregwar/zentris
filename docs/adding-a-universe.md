# Adding a universe

A universe is one complete art direction: a world, a palette, materials, a HUD typography and its own way of
staging the music. This guide covers how to design one, how to plug it in, and how to test it. The list of ideas
waiting to be built is the [roadmap](universes.md#roadmap).

## Design

**One world, not a set of options.** Everything on screen belongs to the same place, lit by the same light and
made of the same materials: the background, the well, the pieces, the particles and the HUD text. The song changes
the intensity inside that world (calm, mid, peak), never the world itself. Avoid random combinations of effects.

**Restraint.** Fewer elements, done well. A busy, over-textured background competes with the stack: the player must
read the pieces at a glance at any level. Keep the world quiet behind the well and let the music bring it to life.
Aim for a look that could be a screenshot from an indie game, not a demo of shader effects.

**The pieces.** They are the main material of the universe (brushed ink, phosphor lines, clay, frosted glass,
jelly...). The seven piece types must stay easy to tell apart, by color, value or pattern, in every variant.

**Variants.** Ship about 6 to 8 curated variants: hand-designed sets of background, light, accent **and their own
7 piece colors** (two variants with the same pieces look like the same universe). Pick one per song from the
`SongLook` (key, mode, brightness, dynamics...), never with random hue math.

**Staging the music.** Plan each moment, quietly at first:

| Moment | What to plan |
|---|---|
| Calm | A still, sparse version of the world: little motion, slow envelopes |
| Mid | The world starts moving with the tempo, events on beats and bars |
| Peak | The big picture of the universe; beat pulses and kick hits only here |
| Lock, hard drop | A small, local reaction |
| Line clear | How the row leaves, in the world's own material |
| Tetris | One memorable celebration |
| Slow motion (bonus) | A clearly different mood of the same world (night, negative, time stretch...) |
| Game over | How the stack ends, in the world's terms |
| Exit and entrance | How the world fades to the neutral bridge state (fog, water, clouds closing in...) |

Calm and mid parts follow slow (~1 s) envelopes of the music. **Peaks must be alive and punchy**: `drop()` is the
signature hit on the drop's first downbeat, and for the whole peak the world hits with the kicks, moves with the beats
and changes with the bars, so that even with the sound off it is obvious the song is in a drop. No camera shake and no
strobing (one soft flash on the drop is fine).

**Priorities.** Living, clearly different looks for the song's sections (a DROP, a CHORUS and a BREAK must each be
noticeable at a glance) matter far more than the slow motion bonus or the game over: spend the effort there first.

**Effects that look good** (rules from play testing):
- A line clear makes the blocks themselves disappear (fade, melt, break, fall, pop), quickly (under ~0.5 s). Never
  draw something over the row (a brush stroke, a white dash, a blade of light), and leave nothing behind (no tide
  mark, no lingering trail).
  It runs at the real speed even in slow motion (the slowed world clock would leave the row hiding the stack).
- No scan or swipe crossing the whole screen (for phase changes or anything else): changes come from the world
  itself, dynamically (things rising, bursting, moving with the music).
- Locks and hard drops stay small and come from where the impact is (smoke or dust pushed out from under the
  piece, at the contact), as one cohesive effect for the piece, never a scatter of small puffs per cell.
- Nothing frequent may be big: an effect on every lock (a glow wave around the piece, a ring) quickly feels like
  too much.
- No preview of the falling piece's path beyond the ghost.
- Changes of universe stay easy on the eyes and keep the stack readable from start to end: smooth fades, no
  dither checkerboards, pixelation or scattered flickering cells over the screen.
- The stack must contrast clearly with what is behind it, in every variant (pale pieces on a pale sky do not
  read).
- Background textures stay soft (smooth gradients, a fine grain, a paper tone), never a pattern of visible dots,
  hatches or marks behind the UI: RISOGRAPH's halftone washes across the sheet were "totally ruining the UI".
- Drops never move the stack, the pieces, the previews or the HUD (no registration offset, shake or jolt on them),
  and large background motion stays slow (no big shapes jumping, sliding or re-composing on beats): fast moving
  shapes on a drop cause nausea. Show the drop with color, density and slow envelopes instead.
- Whole-scene pulses (the background brightening, the world surging) stay regular and slow: on the beat grid, never
  on detected onsets (they fire on syncopations and miss beats: irregular pulses are dizzying), and every two beats
  or slower, never on every beat. The host's `kick` and `beatPulse` already follow this (every two beats, their
  strength from the music); a universe keeping its own beat clock pulses at half time (`music.beat / 2`).
- Merging cells into one shape is fine when it is the universe's design (one brush stroke per piece in SUMI),
  never by accident (different pieces of the same color fusing).

**Taste, from play testing the first ten universes.** In one line: minimalist, no bloat, and original, never a
classical Tetris look. What worked and what did not:

| Liked | Disliked |
|---|---|
| WATERCOLOR, SUMI, VECTOR, SMOKE: one strong idea, minimalist, lots of empty space | Bloated screens full of graphics: the first SYNTHWAVE (sun, road, lamps, palms, grain) looked "like an old game of 2010" |
| Pieces made of the world's own material (ink, phosphor, wash, lit smoke) | Generic, "standard" Tetris looks (PIXEL is fine but not original) |
| Limited, deliberate palettes: neutral white or black paper, dark nights with pastel pieces (CLOUDSCAPE's POLAR NIGHT, its blue-hour slow motion) | Tinted "vintage" grounds (SUMI's yellowish paper), pale pieces on pale skies |
| Pieces with depth and life that read at a glance | Flat, bright, dull blocks (POLAR NIGHT), bevels and relief (old CLOUDSCAPE tiles), visible tile junctions inside a shape |
| Text set in the world, restrained HUD | Opaque white cards or framed panels around the HUD (DIORAMA's cards, CLOUDSCAPE's pane borders) |
| Drops that clearly change the energy, with elegance | Too much animation on drops (VECTOR's first punchy peak), shockwaves and glow rings |
| Night moods (players asked for DIORAMA's bonus night as a normal look) | Odd scenery: towering clouds above the horizon, a skyline added for its own sake |

When a universe starts to feel busy, remove elements instead of tuning them. When the right direction is unclear,
render two or three alternatives side by side and let the player pick.

**Typography.** One or two fonts that fit the world, under the SIL Open Font License, placed in
`src/universes/<id>/embed/` with their license text next to the universe (`src/universes/<id>/OFL-*.txt`).

## Implementation

1. Make a folder `src/universes/<id>/` (CMake picks up every `.cpp` in it) with a class deriving from `Universe`
   (`src/universe.hpp`), and a function `const UniverseEntry& universe_<id>()` returning its registry entry:
   its name (upper case), a one-line blurb, a factory, an affinity function (how well it suits a song, from the
   `SongLook`: tempo, brightness, bass/air, dynamics, density; ~1 neutral, 0 never) and its variants.
2. Add one line to `src/universes/registry.def`: `UNIVERSE(<id>, weight)`. Nothing else in the host changes. The weight
   is how often it is picked: 1 for an atmosphere that fits almost any song and stays fresh when it comes back,
   lower for a world with a strong identity that would feel repetitive if it came back often (DIORAMA is 0.3).
3. Implement the interface. The universe owns all its GL resources (created in `init`, freed in its destructor)
   and draws the whole frame into the framebuffer `render` is given (not necessarily the window: during a
   transition it is a texture): world, well, stack, piece, ghost, previews, bonus gauge, effects, and the HUD text
   (`Frame::hud`, `Frame::panels`) in its own typography (`ugl::hudLeft` resolves the alignment; each line's
   `HudRole` says what it is, so a universe can space, weigh or extend each kind its own way: SUMI adds the
   level's name after its own, DIORAMA sets them over the scene on a feathered halo).
   - Assets: every file in `src/universes/<id>/embed/` is compiled in as a byte array, to include as
     `"universes/<id>/<file>.inc"`.
   - `setSong` picks the variant (`look.variant >= 0` forces it; avoid `look.avoidVariant`, the one on screen
     when the universe follows itself); `setLevel` stages calm / mid / peak with its own in-universe change.
   - `refresh`, `onEvent` (locks, hard drops, clears, Tetris, top out), `setBonus` (slow motion), `levelUp` and
     `Frame::music` (beat phase, beat grid, kick, bands) make it react. `Frame::dim` / `paused` / `fade` dim it.
4. **Layout contract** (`src/layout.hpp`): the well's cells lie on the plane z = 0 of board space (one unit per
   cell, x in [-5, 5], y in [-10, 10]); draw them through `layout::camera()` (or a camera mapping that plane to the
   same pixels; a slight breathing is fine, report it through `project()`), with the previews, the gauge and the
   HUD columns at the `layout::` anchors. This is what lets transitions keep the stack in place. A universe with a
   camera of its own (DIORAMA's elevated long lens) glides to the layout's front view while a change runs (from
   `setBridge`) and reports its camera through `project()`, so the HUD follows it.
5. **Transition contract**: there is no code per pair of universes, so any universe pairs with any other.
   - `hints()`: the active variant's background, ink and accent colors, light or dark, and the transition styles
     it likes (`TS_*`). The host's transitions only use this.
   - Optionally, a native exit and entrance: return true from `bridgeable()` and honor `setBridge(Bridge)`, called
     every frame of a change before `render` (amount 0 = itself .. 1 = the bridge): fade the world out toward
     `bridge.color`, keep the camera on the layout, draw the stack plainly in `bridge.ink`. Universes that don't do
     it get it from the host (the stack drawn plainly from the game's cells over their own frame).
6. **Portability**: the Windows build uses MSVC, which rejects `[]` lambdas that use local constants: capture
   explicitly (`[&]`). No VLAs or GCC extensions. Keep expensive passes (ray marching, simulations) at a reduced
   resolution.
7. Document it: a section in [universes.md](universes.md) with a screenshot in `docs/universes/<id>.jpg`, a short
   line in the README gallery, and ✅ on its roadmap row.

## Testing

Always test **off screen** (never on your display) and at a low resolution unless the look's details are being
judged (full 1920x1080 only for final checks):

```sh
xvfb-run -a -s "-screen 0 1280x720x24" ./build/zenscene --size 960x540 --shot out.png --section drop <id>
xvfb-run -a -s "-screen 0 1280x720x24" ./build/zentris --size 960x540 --universe <id> --review-level 2 --review-shot out.png song.mp3
xvfb-run -a -s "-screen 0 1280x720x24" ./build/zentris --size 640x360 --universe <id> --phase-shots out_ song.mp3
xvfb-run -a -s "-screen 0 1280x720x24" ./build/zentris --size 640x360 --universe vector --to <id> --transition ink --transition-shots tr_ 6 song.mp3
tools/universesheet.sh <id> sheet.png 2      # every variant in one contact sheet (level 0, 1 or 2)
```

- Review every variant side by side (`tools/universesheet.sh`) at calm, mid and peak.
- Check a few transitions with a dark and a light universe, both ways (`--universe A --to B` and back).
- `ZEN_TEST_OVER=9` before a shot command hard-drops every piece from 9 s into the shot's settle (the shot then
  shows the game over).
