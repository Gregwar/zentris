#!/usr/bin/env python3
"""Promo video: N random songs of the default playlist, each recorded around its first drop (a random
scene per song, the game played at a calm pace), stitched together with crossfades.

usage: tools/promo.py [OUT.mp4] [--songs N] [--before SEC] [--after SEC] [--fade SEC] [--size WxH]
Needs a built zentris (build/zentris), yt-dlp and ffmpeg.
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZENTRIS = os.path.join(ROOT, "build", "zentris")


def default_playlist_id():
    with open(os.path.join(ROOT, "src", "youtube.hpp")) as f:
        return re.search(r'DEFAULT_PLAYLIST = "[^"]*list=([\w-]+)"', f.read()).group(1)


def playlist_ids(list_id):
    """Video ids of the playlist: the listing zentris cached, else asked to yt-dlp."""
    cache = os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache"))
    tsv = os.path.join(cache, "zentris", "youtube", "lists", f"list-{list_id}.tsv")
    if os.path.exists(tsv):
        with open(tsv) as f:
            ids = [line.split("\t")[0] for line in f if line.strip()]
        if ids:
            return ids
    out = subprocess.run(["yt-dlp", "--flat-playlist", "--print", "%(id)s",
                          f"https://www.youtube.com/playlist?list={list_id}"],
                         capture_output=True, text=True, check=True).stdout
    return [line.strip() for line in out.splitlines() if line.strip()]


def duration(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path],
                         capture_output=True, text=True, check=True).stdout
    return float(out.strip())


def record(video_id, out, args):
    """One clip around the song's first drop; False if the song could not be loaded."""
    cmd = [ZENTRIS, "--hidden", "--size", args.size, "--clip", out, "--clip-before", str(args.before),
           "--clip-after", str(args.after), f"ytdl:{video_id}"]
    try:
        log = subprocess.run(cmd, capture_output=True, text=True, timeout=600).stdout
    except subprocess.TimeoutExpired:
        return False
    for line in log.splitlines():
        if line.startswith("[clip]") or line.startswith("[app] now playing"):
            print("   ", line)
    return os.path.exists(out) and "[clip] wrote" in log


def stitch(clips, out, fade):
    """Crossfades consecutive clips (picture and sound), fades in at the start and out at the end."""
    durs = [duration(c) for c in clips]
    inputs = sum((["-i", c] for c in clips), [])
    vf, af = [], []
    v, a, t = "[0:v]", "[0:a]", durs[0]
    for k in range(1, len(clips)):
        vf.append(f"{v}[{k}:v]xfade=transition=fade:duration={fade}:offset={t - fade:.3f}[v{k}]")
        af.append(f"{a}[{k}:a]acrossfade=d={fade}[a{k}]")
        v, a, t = f"[v{k}]", f"[a{k}]", t + durs[k] - fade
    vf.append(f"{v}fade=t=in:d=0.5,fade=t=out:st={t - 1.0:.3f}:d=1[vout]")
    af.append(f"{a}afade=t=in:d=0.5,afade=t=out:st={t - 1.0:.3f}:d=1[aout]")
    subprocess.run(["ffmpeg", "-y", "-v", "error", *inputs, "-filter_complex", ";".join(vf + af),
                    "-map", "[vout]", "-map", "[aout]", "-c:v", "libx264", "-preset", "medium", "-crf", "18",
                    "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", out], check=True)
    return t


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?", default="promo.mp4")
    ap.add_argument("--songs", type=int, default=5)
    ap.add_argument("--before", type=float, default=4.0, help="seconds before the drop")
    ap.add_argument("--after", type=float, default=6.0, help="seconds after the drop")
    ap.add_argument("--fade", type=float, default=1.0, help="crossfade length")
    ap.add_argument("--size", default="1920x1080")
    args = ap.parse_args()
    if not os.path.exists(ZENTRIS):
        sys.exit(f"build zentris first ({ZENTRIS} is missing)")

    ids = playlist_ids(default_playlist_id())
    random.shuffle(ids)
    print(f"{len(ids)} songs in the playlist, recording {args.songs}")
    with tempfile.TemporaryDirectory() as tmp:
        clips = []
        for vid in ids:
            if len(clips) == args.songs:
                break
            clip = os.path.join(tmp, f"clip{len(clips)}.mp4")
            print(f"[{len(clips) + 1}/{args.songs}] {vid}")
            if record(vid, clip, args):
                clips.append(clip)
            else:
                print("    could not record it, trying another song")
        if len(clips) < 2:
            sys.exit("not enough clips")
        total = stitch(clips, args.out, args.fade)
    print(f"wrote {args.out} ({total:.1f} s)")


if __name__ == "__main__":
    main()
