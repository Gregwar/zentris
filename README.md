# Zen Tetris

A minimalist, relaxing Tetris in C++ / OpenGL 3.3, rendered in 3D and driven by the music you load.

## Build & run

Dependencies (Debian/Ubuntu): `sudo apt install cmake g++ libglfw3-dev libglew-dev`
(miniaudio and stb are vendored in `third_party/`).

```sh
cmake -S . -B build && cmake --build build -j
./build/zentetris                 # plays every .mp3/.wav/.flac in ./audio, shuffled
./build/zentetris song.mp3 ~/Music --fullscreen
```

YouTube playlists (or single videos) work too, through [yt-dlp](https://github.com/yt-dlp/yt-dlp)
(install it with `pipx install "yt-dlp[default]"`; YouTube also needs a JavaScript runtime such as Deno or
Node.js, and Node.js is picked up automatically). Songs are downloaded when queued and cached as MP3 in
`~/.cache/zentetris/youtube/`. Downloading from YouTube is against its terms of service: personal use only.

```sh
./build/zentetris "https://www.youtube.com/playlist?list=..."
./build/zenscope "https://www.youtube.com/watch?v=..."
```

Other options: `--seed N` (repeat a scene), `--autoplay`, `--mute`, `--size WxH`,
`--shots PREFIX N` (renders N screenshots of different scenes and exits), `--phase-shots PREFIX` (one screenshot per scene level of a song).

## zenscope: see what the game hears

`./build/zenscope [songs or folders...]` plays a song and shows the analysis that drives the game:
- a phase timebar with the scene levels (calm / mid / peak) and where the scene changes happen
- the detected structure: intro, verse, build, chorus, drop, break, outro, with similar parts grouped
- pulse zones, a 16-band spectrum, loudness, intensity and onsets, and bar lines
- a live readout of the current section and the game's density/speed/glow profile, with the beat in the bar
- a zoomed detail view with the beat grid

Space plays/pauses, Left/Right seek 5 s, Up/Down zoom the detail view, N/P change song, click to seek.
The game and zenscope share the same analysis and song plan code (`src/songplan.*`), so what you see is what the game uses.

## Controls

Keyboard and gamepad both work at the same time. A gamepad is detected at startup and on hot-plug,
and the on-screen hints follow whichever device you used last.

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | ← → | D-pad / left stick |
| Soft / hard drop | ↓ / Space | Down / Up |
| Rotate | ↑ or X (Z or J to rotate left) | A (B/X to rotate left) |
| Hold | C / Shift (tap) | LB / RB / triggers |
| New scene | T | Y |
| Next song | N / Tab / Enter / PageDown | Back |
| Seek ±10 s in the song (testing) | Ctrl+Shift+Left/Right | |
| Pause | Esc / P (Q quits while paused) | Start |
| Fullscreen | F / F11 | |

## How the music shapes the game

Each song is decoded and analyzed in the background (under 1 s):

- **Tempo and beat grid**: gravity steps land on the beat, at 1 row every 2 beats, every beat, or every half beat, depending on the song's energy at that moment.
- **Key**: the base hue follows the circle of fifths.
- **Brightness, bass/air balance, dynamics, density**: choose the mood (night, dusk or pale), the particle layouts, bloom, how strongly things react, and the camera's motion.
- **Song structure**: the song is split at bar lines into labelled segments (intro, verse, build, chorus, drop, break, outro), and segments that sound alike are grouped. Each segment eases the scene's density, speed, glow and saturation (builds ramp up, breaks thin out).
- **One identity per song**: background, main particles, blocks, frame and mood stay the same for the whole song. At most three intensity levels (calm, mid, peak) shift the hue slightly and add color, glow or an extra particle layer. Changes happen only when the level changes, crossfading over 8 s, and never interrupt each other.
- **Calm by design**: visuals follow slow (~1 s) envelopes of the music and there is no camera shake or flashing. Beat pulses appear only during peak sections (choruses, drops), stronger for faster songs; songs above ~110 BPM also get soft hits on strong transients there.
- **Live bands and loudness**, heavily smoothed, drive particle motion and glow.

The scene seed combines the song's fingerprint with a random seed for each run, so the same song looks
different every time. The combinatorial space covers 8 palette schemes × 3 moods, 8 backgrounds,
12 particle layouts × 8 particle shapes (one or two layers), 8 block materials × 4 meshes (with a varying
block depth), 7 board frames, and a post-processing grade (bloom, vignette, chromatic aberration, grain, split-toning).
The scene name is shown in the bottom-left corner.
