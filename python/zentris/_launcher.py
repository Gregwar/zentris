"""Console entry points: run the bundled native binaries (zentris, zenscope, zenscene)."""

import os
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path

BIN_DIR = Path(__file__).resolve().parent / "bin"


def _tool_env() -> dict:
    env = dict(os.environ)
    # yt-dlp is a dependency of this package: point the game to the one installed alongside.
    scripts = Path(sysconfig.get_path("scripts"))
    for name in ("yt-dlp", "yt-dlp.exe"):
        if (scripts / name).is_file():
            env.setdefault("ZENTRIS_YTDLP", str(scripts / name))
            break
    # yt-dlp needs ffmpeg to convert audio; use the bundled one when the system has none.
    if not shutil.which("ffmpeg"):
        try:
            import imageio_ffmpeg

            env.setdefault("ZENTRIS_FFMPEG", imageio_ffmpeg.get_ffmpeg_exe())
        except Exception:
            pass
    return env


def _run(name: str) -> None:
    exe = BIN_DIR / (name + (".exe" if os.name == "nt" else ""))
    if not exe.is_file():
        sys.exit(f"{name}: bundled binary not found at {exe}")
    try:
        code = subprocess.call([str(exe), *sys.argv[1:]], env=_tool_env())
    except KeyboardInterrupt:
        code = 130
    sys.exit(code)


def main() -> None:
    _run("zentris")


def scope() -> None:
    _run("zenscope")


def scene() -> None:
    _run("zenscene")
