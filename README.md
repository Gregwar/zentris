# Zentris

A Tetris game inspired by **Tetris Effect** that plays **any music you like**: your own folder or a YouTube
playlist. Each song is analysed ahead of time (tempo, key, intro, verse, build, chorus, drop...) and the game stages
it: pieces fall on the beat, the pace follows the song's energy, and each song plays in its own **universe**.

```sh
uvx zentris            # the default playlist
uvx zentris ~/Music    # or any folders, files and YouTube URLs
```

## Universes

Each universe is a hand-directed world with its own palette, materials, typography and way of staging the music:
calm parts stay quiet, drops and choruses bring the whole picture. Each one comes in several variants picked per
song, and songs change universe with a transition on the beat.

<table>
<tr>
<td width="50%"><img src="https://raw.githubusercontent.com/Gregwar/zentris/becac93004878065dbdb2eec1a5f3ea6a91e9ded/docs/universes/vector.jpg"><br><b>VECTOR</b>: phosphor beams on a dark CRT</td>
<td width="50%"><img src="https://raw.githubusercontent.com/Gregwar/zentris/becac93004878065dbdb2eec1a5f3ea6a91e9ded/docs/universes/sumi.jpg"><br><b>SUMI</b>: ink on washi paper</td>
</tr>
<tr>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/becac93004878065dbdb2eec1a5f3ea6a91e9ded/docs/universes/diorama.jpg"><br><b>DIORAMA</b>: a sunlit clay island</td>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/becac93004878065dbdb2eec1a5f3ea6a91e9ded/docs/universes/abyss.jpg"><br><b>ABYSS</b>: the deep sea, lit by living things</td>
</tr>
<tr>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/becac93004878065dbdb2eec1a5f3ea6a91e9ded/docs/universes/smoke.jpg"><br><b>SMOKE</b>: a dark stage, slow smoke and one hard spotlight</td>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/becac93004878065dbdb2eec1a5f3ea6a91e9ded/docs/universes/watercolor.jpg"><br><b>WATERCOLOR</b>: transparent washes on cold-press paper</td>
</tr>
<tr>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/becac93004878065dbdb2eec1a5f3ea6a91e9ded/docs/universes/polar.jpg"><br><b>POLAR NIGHT</b>: a snowfield under the aurora</td>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/becac93004878065dbdb2eec1a5f3ea6a91e9ded/docs/universes/risograph.jpg"><br><b>RISOGRAPH</b>: a two-ink risograph print</td>
</tr>
<tr>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/049bab4ddf4aea7d90c768546e006132082e7ead/docs/universes/white_room.jpg"><br><b>WHITE ROOM</b>: a seamless white studio, matte blocks and soft shadows</td>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/becac93004878065dbdb2eec1a5f3ea6a91e9ded/docs/universes/kintsugi.jpg"><br><b>KINTSUGI</b>: dark polished stone, ceramic mended with gold</td>
</tr>
<tr>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/becac93004878065dbdb2eec1a5f3ea6a91e9ded/docs/universes/laser.jpg"><br><b>LASER SHOW</b>: every piece projected by its own laser</td>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/210013d9eb38de1166e8753be86cea34904be746/docs/universes/garden.jpg"><br><b>GARDEN</b>: tiles of earth growing pixel grass and flowers</td>
</tr>
<tr>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/f5b149393574b12b9a47ec4ab30cb19c21c6283f/docs/universes/sea.jpg"><br><b>SEA</b>: glass cubes of water over a rising 3D sea</td>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/8d4d510ae0b93552a83ed57d0608ba5dadbda54b/docs/universes/fluo.jpg"><br><b>FLUO</b>: fluorescent paint splashed on a dark wall, glowing under a blacklight on drops</td>
</tr>
<tr>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/8d4d510ae0b93552a83ed57d0608ba5dadbda54b/docs/universes/marbles.jpg"><br><b>MARBLES</b>: glass marbles in square tiles, shot out across a dark table where they collide and stain the felt</td>
<td><img src="https://raw.githubusercontent.com/Gregwar/zentris/412230bfad663a226675f239505670847baf2334/docs/universes/prism.jpg"><br><b>PRISM</b>: glass cubes in the dark, slow beams of light, a rainbow on the drop</td>
</tr>
<tr>
<td><img src="docs/universes/lightsource.jpg"><br><b>LIGHTSOURCE</b>: a dim room lit by the falling piece, the stack casting its shadows</td>
<td></td>
</tr>
</table>

Disabled for now (not built; their code stays in the repository):

| Universe | Status |
|---|---|
| PIXEL | Too close to a classical Tetris look |
| SYNTHWAVE | To be redesigned (minimal neon) |
| CLOUDSCAPE | To be reworked (nights only) |
| GARDEN (smooth) | Replaced by its 16-bit version, now GARDEN (`src/universes/garden16/`) |

More in [docs/universes.md](https://github.com/Gregwar/zentris/blob/main/docs/universes.md): every universe and its variants, the transitions, and the
roadmap of 100 universes. To make a new one, see [docs/adding-a-universe.md](https://github.com/Gregwar/zentris/blob/main/docs/adding-a-universe.md).

## Install

[![PyPI](https://img.shields.io/pypi/v/zentris)](https://pypi.org/project/zentris/)

Packages for Linux, macOS and Windows are on [PyPI](https://pypi.org/project/zentris/). Run them with
[uv](https://docs.astral.sh/uv/) as above, or install them:

```sh
pipx install zentris        # or: uv tool install zentris
zentris ~/Music             # with no argument it plays the default YouTube playlist
```

Quote YouTube URLs (`&` is special in the shell). YouTube also needs a JavaScript runtime
([Deno](https://deno.com) or Node.js) on your system.

## Controls

Keyboard and gamepad both work at the same time. A gamepad is detected at startup and on hot-plug,
and the on-screen hints follow whichever device you used last.
Controllers are recognised through the bundled [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)
(`third_party/gamecontrollerdb.txt`; extra mappings can be given in `SDL_GAMECONTROLLERCONFIG`). A controller
missing from it still works with a generic layout.

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | ← → | D-pad / left stick |
| Soft / hard drop | ↓ / Space | Down / Up |
| Rotate | ↑ or X (Z or J to rotate left) | A (B/X to rotate left) |
| Hold | C / Shift (tap) | LB / RB |
| Slow motion (when the gauge is full) | V / E | ZL / ZR (LT / RT) |
| Next universe (with `--universe`: its next variant, a new seed) | T | Y |
| Next song | N / Tab / Enter / PageDown | Back |
| Seek ±10 s in the song (testing) | Ctrl+Shift+Left/Right | |
| Next level (debugging) | L | |
| Fill the bonus gauge (testing) | B | |
| Pause | Esc / P (Q quits while paused) | Start |
| Fullscreen | F / F11 | |

## Tools

- `zenscope [songs...]`: see what the game hears (the song's structure, phases, beats and spectrum), and write
  notes on it.
- `zenscene [UNIVERSE[:VARIANT]]`: browse universes, variants, song phases and transitions while the computer plays.

Both come with the package (`uvx --from zentris zenscope`). Every option, the tools and how the music drives the
game are in [docs/reference.md](https://github.com/Gregwar/zentris/blob/main/docs/reference.md).

## Build from source

```sh
sudo apt install cmake g++ libglfw3-dev libglew-dev    # Debian/Ubuntu
cmake -S . -B build && cmake --build build -j
./build/zentris ~/Music
```

`-DZENTRIS_DISABLED_UNIVERSES="pixel;cloudscape;synthwave;garden"` (the default) lists the universes left out of the build;
`-DZENTRIS_DISABLED_UNIVERSES=""` builds them all.
