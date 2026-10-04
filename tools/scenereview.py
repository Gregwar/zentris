#!/usr/bin/env python3
"""Scene review: render batches of random scenes, review them in the browser (phone or computer), and discuss
changes with Claude in threads.

Dashboard tabs:
  scenes      new scenes, one at a time: skip one (-> skipped) or comment on it (-> to process)
  skipped     scenes skipped without a comment (they can still be commented)
  to process  commented scenes, waiting for Claude
  to review   Claude's changes, with before/after snapshots: accept (-> done) or comment again (-> to process)
  suggestions Claude's own proposals, each on its own local branch (suggest/...), not in main: before (main) / after
              (the branch) snapshots; accept (-> merged by Claude), dismiss (-> branch dropped) or comment (-> to
              process; Claude's answer comes back here)
  done        accepted threads, accepted or dismissed suggestions

Commands:
  generate [--count 100] [--jobs 3] [--size 1280x720]
      Renders COUNT snapshots, each a random scene made for a random downloaded song of the default playlist, in a
      random part of it (calm, mid and peak levels in turn), with a filled board to show the blocks.
  serve [--port 8765] [--lan] [--no-browser]
      Runs the dashboard (--lan: reachable from the local network, e.g. a phone).
  pending
      Prints the threads to process (JSON: scene, snapshots, messages).
  watch [--interval 5] [--all]
      Waits until a thread to process appears that was not handed out yet, prints the new ones (JSON), marks them
      handed out and exits: run it in the background to be woken up. --all also returns the ones handed out.
  reply KEY --message TEXT [--no-render] [--image PNG]
      Answers a thread: renders the scene again with the current build (same song, seed, time and level) as the
      "after" snapshot, the previous one being "before", and moves the thread to "to review".
  add --code CODE --message TEXT [--section VERSE] [--level 1] [--size 1280x720]
      Opens a thread "to process" on a scene code (zenscene's "send to dashboard"): renders it with zenscene, at
      that song section and level, into the "zenscene" batch.
  suggest --code CODE --branch BRANCH --after-bin ZENSCENE --title TEXT --message TEXT [--section --level --size]
      Opens a suggestion: the scene rendered with main's build/zenscene (before) and with the branch's zenscene
      (after). Replies to it re-render "after" with the branch's binary and bring it back to "suggestions".
      --before-image / --after-image PNG replace the renders (for what zenscene can't show, like the game's HUD).
  suggestions
      Prints the open suggestions (key, status, branch, title), to avoid proposing the same thing twice.
  resolve KEY
      Closes an accepted (merged) or dismissed (branch dropped) suggestion: -> done.

Everything lives in the user data folder (~/.local/share/zentris/scene-review/): one folder per batch (snapshots,
scenes.jsonl with what reproduces each scene), threads.json (the threads and their state) and feedback.jsonl (every
message, appended, for later processing).
Needs a built zentris (build/zentris), and zenscene (build/zenscene) for the scenes it sends.
"""
import argparse
import concurrent.futures
import contextlib
import datetime
import fcntl
import glob
import json
import os
import random
import re
import shlex
import socket
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZENTRIS = os.path.join(ROOT, "build", "zentris")
ZENSCENE = os.path.join(ROOT, "build", "zenscene")
DATA = os.path.join(os.environ.get("XDG_DATA_HOME", os.path.expanduser("~/.local/share")), "zentris", "scene-review")
CACHE = os.path.join(os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache")), "zentris", "youtube")
THREADS = os.path.join(DATA, "threads.json")
FEEDBACK = os.path.join(DATA, "feedback.jsonl")
LEVELS = {0: "calm", 1: "mid", 2: "peak"}


def now():
    return datetime.datetime.now().isoformat(timespec="seconds")


# ---------------------------------------------------------------- songs and rendering

def default_playlist_id():
    with open(os.path.join(ROOT, "src", "youtube.hpp")) as f:
        return re.search(r'DEFAULT_PLAYLIST = "[^"]*list=([\w-]+)"', f.read()).group(1)


def cached_songs():
    """(video id, title, mp3 path) of the downloaded songs, those of the default playlist first."""
    titles, order = {}, []
    lists = sorted(glob.glob(os.path.join(CACHE, "lists", "*.tsv")))
    first = os.path.join(CACHE, "lists", f"list-{default_playlist_id()}.tsv")
    if first in lists:
        lists.remove(first)
        lists.insert(0, first)
    for tsv in lists:
        with open(tsv, encoding="utf-8", errors="replace") as f:
            for line in f:
                parts = line.rstrip("\n").split("\t")
                if parts and parts[0] and parts[0] not in titles:
                    titles[parts[0]] = parts[1] if len(parts) > 1 else parts[0]
                    order.append(parts[0])
    songs = [(v, titles[v], os.path.join(CACHE, v + ".mp3")) for v in order]
    songs = [s for s in songs if os.path.exists(s[2])]
    if not songs:  # no listing: any downloaded song
        songs = [(os.path.basename(p)[:-4], os.path.basename(p)[:-4], p) for p in glob.glob(os.path.join(CACHE, "*.mp3"))]
    return songs


def run_snapshot(cmd):
    """Runs a --review-shot command; its [review] JSON line, or None."""
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=180).stdout
    except subprocess.TimeoutExpired:
        return None
    for line in out.splitlines():
        if line.startswith("[review] "):
            return json.loads(line[len("[review] "):])
    return None


def render(i, song, level, folder, size):
    vid, title, path = song
    seed = random.getrandbits(63) | 1
    shot = os.path.join(folder, f"scene{i:03d}.png")
    cmd = [ZENTRIS, "--hidden", "--mute", "--size", size, "--seed", str(seed), "--review-shot", shot,
           "--review-level", str(level), path]
    info = run_snapshot(cmd)
    if info:
        info.update(id=i, image=os.path.basename(shot), video=vid, title=title, seed=seed, wanted_level=level,
                    level_name=LEVELS.get(info["level"], "?"), snapshot_cmd=shlex.join(cmd))
    return info


def generate(args):
    if not os.path.exists(ZENTRIS):
        sys.exit(f"build zentris first ({ZENTRIS} is missing)")
    songs = cached_songs()
    if not songs:
        sys.exit(f"no downloaded song in {CACHE}: play the default playlist a little first")
    random.shuffle(songs)
    name = datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")
    folder = os.path.join(DATA, name)
    os.makedirs(folder, exist_ok=True)
    print(f"{len(songs)} downloaded songs; rendering {args.count} scenes into {folder}")
    jobs = [(i, songs[i % len(songs)], i % 3) for i in range(args.count)]
    scenes = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(render, i, s, lv, folder, args.size): i for i, s, lv in jobs}
        for n, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            info = fut.result()
            if info:
                scenes.append(info)
                print(f"[{n}/{args.count}] {info['level_name']:4s} {info['scene']}  ({info['title'][:50]})")
            else:
                print(f"[{n}/{args.count}] failed (scene {futures[fut]})")
    scenes.sort(key=lambda s: s["id"])
    with open(os.path.join(folder, "scenes.jsonl"), "w") as f:
        for s in scenes:
            f.write(json.dumps(s, ensure_ascii=False) + "\n")
    print(f"done: {len(scenes)} scenes. Review them with: tools/scenereview.py serve")


# ---------------------------------------------------------------- scenes and threads

def batches():
    if not os.path.isdir(DATA):
        return []
    return sorted(d for d in os.listdir(DATA) if os.path.exists(os.path.join(DATA, d, "scenes.jsonl")))


def all_scenes():
    """Every scene of every batch, keyed 'batch:id'."""
    out = {}
    for b in batches():
        with open(os.path.join(DATA, b, "scenes.jsonl")) as f:
            for line in f:
                if line.strip():
                    s = json.loads(line)
                    s["batch"], s["key"] = b, f"{b}:{s['id']}"
                    out[s["key"]] = s
    return out


@contextlib.contextmanager
def threads_locked():
    """threads.json, read and written back under a lock (the dashboard and the commands share it)."""
    os.makedirs(DATA, exist_ok=True)
    with open(THREADS + ".lock", "w") as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        state = {}
        if os.path.exists(THREADS):
            with open(THREADS) as f:
                state = json.load(f)
        yield state
        tmp = THREADS + ".tmp"
        with open(tmp, "w") as f:
            json.dump(state, f, ensure_ascii=False, indent=1)
        os.replace(tmp, THREADS)


def read_threads():
    if not os.path.exists(THREADS):
        return {}
    with open(THREADS) as f:
        return json.load(f)


def log_feedback(scene, msg, status):
    entry = {"key": scene["key"], "status": status, **msg, "code": scene["code"], "scene": scene["scene"],
             "level": scene["level_name"], "section": scene["section"], "energy": scene["energy"],
             "time": scene["time"], "video": scene["video"], "title": scene["title"], "song": scene["song"],
             "seed": scene["seed"], "snapshot_cmd": scene["snapshot_cmd"]}
    with open(FEEDBACK, "a") as f:
        f.write(json.dumps(entry, ensure_ascii=False) + "\n")


def user_action(key, action, text=""):
    scene = all_scenes().get(key)
    if scene is None:
        raise KeyError(key)
    with threads_locked() as threads:
        t = threads.setdefault(key, {"key": key, "status": "scenes", "messages": [], "handed": False})
        if action == "skip":
            msg = {"from": "user", "at": now(), "kind": "skip", "text": text}
            t["status"] = "skipped"
        elif action == "accept":
            msg = {"from": "user", "at": now(), "kind": "accept", "text": text}
            # An accepted suggestion still has to be merged by Claude.
            t["status"] = "accepted" if scene.get("branch") else "done"
            t["handed"] = False
        elif action == "dismiss":
            msg = {"from": "user", "at": now(), "kind": "dismiss", "text": text}
            t["status"] = "dismissed"
            t["handed"] = False
        else:
            if not text.strip():
                raise ValueError("empty comment")
            msg = {"from": "user", "at": now(), "kind": "comment", "text": text.strip()}
            t["status"] = "process"
            t["handed"] = False
        t["messages"].append(msg)
        status = t["status"]
    log_feedback(scene, msg, status)
    return t


def thread_view(t, scenes):
    """A thread with its scene and the snapshot files, for the commands' JSON."""
    s = scenes[t["key"]]
    folder = os.path.join(DATA, s["batch"])
    msgs = []
    for m in t["messages"]:
        m = dict(m)
        for k in ("before", "after"):
            if m.get(k):
                m[k] = os.path.join(folder, m[k])
        msgs.append(m)
    return {"key": t["key"], "status": t["status"], "scene": s["scene"], "code": s["code"], "level": s["level_name"],
            "section": s["section"], "energy": s["energy"], "time": s["time"], "title": s["title"],
            "song": s["song"], "snapshot_cmd": s["snapshot_cmd"], "original": os.path.join(folder, s["image"]),
            "latest": latest_image(t, s, folder), "messages": msgs}


def latest_image(t, s, folder):
    for m in reversed(t["messages"]):
        if m.get("after"):
            return os.path.join(folder, m["after"])
    return os.path.join(folder, s["image"])


def cmd_pending(args):
    scenes = all_scenes()
    out = [thread_view(t, scenes) for t in read_threads().values() if t["status"] == "process"]
    print(json.dumps(out, ensure_ascii=False, indent=1))


def cmd_watch(args):
    scenes = None
    while True:
        with threads_locked() as threads:
            new = [t for t in threads.values()
                   if t["status"] in ("process", "accepted", "dismissed") and (args.all or not t.get("handed"))]
            for t in new:
                t["handed"] = True
        if new:
            scenes = all_scenes()
            print(json.dumps([thread_view(t, scenes) for t in new], ensure_ascii=False, indent=1))
            return
        time.sleep(args.interval)


def cmd_reply(args):
    scenes = all_scenes()
    threads = read_threads()
    if args.key not in threads:
        sys.exit(f"no thread {args.key}")
    s = scenes[args.key]
    folder = os.path.join(DATA, s["batch"])
    t = threads[args.key]
    suggestion = bool(s.get("branch"))
    # A suggestion always compares main (the original snapshot) with its branch.
    before = s["image"] if suggestion else next((m["after"] for m in reversed(t["messages"]) if m.get("after")), s["image"])
    after = None
    if args.image:
        after = f"scene{s['id']:03d}-r{len(t['messages'])}.png"
        os.makedirs(os.path.join(folder, "after"), exist_ok=True)
        with open(args.image, "rb") as src, open(os.path.join(folder, "after", after), "wb") as dst:
            dst.write(src.read())
        after = "after/" + after
    elif not args.no_render:
        after = f"after/scene{s['id']:03d}-r{len(t['messages'])}.png"
        os.makedirs(os.path.join(folder, "after"), exist_ok=True)
        cmd = shlex.split(s["snapshot_cmd"])
        cmd[cmd.index("--review-shot") + 1] = os.path.join(folder, after)
        if suggestion:
            cmd[0] = s["after_bin"]
        info = run_snapshot(cmd)
        if not info:
            sys.exit("could not render the scene again")
        print(f"rendered {info['scene']} ({info['code']})")
    msg = {"from": "claude", "at": now(), "kind": "change", "text": args.message, "before": before, "after": after}
    if after is None:
        msg.pop("before")
    status = "suggest" if suggestion else "review"
    with threads_locked() as threads:
        th = threads[args.key]
        th["messages"].append(msg)
        th["status"] = status
    log_feedback(s, msg, status)
    print(f"{args.key} -> {'suggestions' if suggestion else 'to review'}")


def cmd_suggest(args):
    for b in (ZENSCENE, args.after_bin):
        if not os.path.exists(b):
            sys.exit(f"missing {b}")
    folder = os.path.join(DATA, "suggestions")
    os.makedirs(os.path.join(folder, "after"), exist_ok=True)
    tag = f"{os.getpid()}-{int(time.time() * 1000)}"
    tmp_before, tmp_after = os.path.join(folder, f"in-{tag}-a.png"), os.path.join(folder, f"in-{tag}-b.png")
    cmd = [ZENSCENE, "--size", args.size, "--review-shot", tmp_before, "--section", args.section, "--level",
           str(args.level), args.code]
    info = run_snapshot(cmd)
    after_cmd = [args.after_bin] + cmd[1:]
    after_cmd[after_cmd.index("--review-shot") + 1] = tmp_after
    info_after = run_snapshot(after_cmd)
    if not info or not info_after:
        sys.exit("could not render the scene")
    # Shots zenscene can't make (e.g. the game's HUD): given images replace the renders.
    for src, dst in ((args.before_image, tmp_before), (args.after_image, tmp_after)):
        if src:
            with open(src, "rb") as a, open(dst, "wb") as b:
                b.write(a.read())
    with threads_locked() as threads:
        jsonl = os.path.join(folder, "scenes.jsonl")
        sid = sum(1 for line in open(jsonl) if line.strip()) if os.path.exists(jsonl) else 0
        image, after = f"scene{sid:03d}.png", f"after/scene{sid:03d}-r0.png"
        os.replace(tmp_before, os.path.join(folder, image))
        os.replace(tmp_after, os.path.join(folder, after))
        cmd[cmd.index("--review-shot") + 1] = os.path.join(folder, image)
        level = info["level"]
        scene = {"id": sid, "image": image, "code": info["code"], "scene": info["scene"], "level": level,
                 "level_name": LEVELS.get(level, "?"), "section": info["section"],
                 "energy": [0.25, 0.55, 0.9][level], "bpm": info["bpm"], "time": 0, "duration": 0, "phase": 0,
                 "phases": 1, "levels": "", "title": args.title, "song": "", "video": "", "seed": 0,
                 "source": "suggestion", "branch": args.branch, "after_bin": os.path.abspath(args.after_bin),
                 "snapshot_cmd": shlex.join(cmd)}
        with open(jsonl, "a") as f:
            f.write(json.dumps(scene, ensure_ascii=False) + "\n")
        key = f"suggestions:{sid}"
        msg = {"from": "claude", "at": now(), "kind": "suggest", "text": args.message, "before": image, "after": after}
        threads[key] = {"key": key, "status": "suggest", "messages": [msg], "handed": True}
    scene["key"], scene["batch"] = key, "suggestions"
    log_feedback(scene, msg, "suggest")
    print(f"{key} -> suggestions ({args.title}, branch {args.branch})")


def cmd_suggestions(args):
    scenes, threads = all_scenes(), read_threads()
    for k, t in threads.items():
        s = scenes.get(k)
        if s and s.get("branch"):
            print(f"{k}\t{t['status']}\t{s['branch']}\t{s.get('title', '')}")


def cmd_resolve(args):
    with threads_locked() as threads:
        if args.key not in threads:
            sys.exit(f"no thread {args.key}")
        threads[args.key]["status"] = "done"
    print(f"{args.key} -> done")


def cmd_add(args):
    if not os.path.exists(ZENSCENE):
        sys.exit(f"build zenscene first ({ZENSCENE} is missing)")
    if not args.message.strip():
        sys.exit("empty comment")
    folder = os.path.join(DATA, "zenscene")
    os.makedirs(folder, exist_ok=True)
    tmp = os.path.join(folder, f"incoming-{os.getpid()}.png")
    cmd = [ZENSCENE, "--size", args.size, "--review-shot", tmp, "--section", args.section, "--level", str(args.level),
           args.code]
    info = run_snapshot(cmd)
    if not info:
        sys.exit("could not render the scene")
    # Numbered under the threads lock (zenscene may send several at once).
    with threads_locked():
        jsonl = os.path.join(folder, "scenes.jsonl")
        sid = sum(1 for line in open(jsonl) if line.strip()) if os.path.exists(jsonl) else 0
        image = f"scene{sid:03d}.png"
        os.replace(tmp, os.path.join(folder, image))
        cmd[cmd.index("--review-shot") + 1] = os.path.join(folder, image)
        level = info["level"]
        scene = {"id": sid, "image": image, "code": info["code"], "scene": info["scene"], "level": level,
                 "level_name": LEVELS.get(level, "?"), "section": info["section"],
                 "energy": [0.25, 0.55, 0.9][level], "bpm": info["bpm"], "time": 0, "duration": 0, "phase": 0,
                 "phases": 1, "levels": "", "title": "", "song": "", "video": "", "seed": 0,
                 "source": "zenscene", "snapshot_cmd": shlex.join(cmd)}
        with open(jsonl, "a") as f:
            f.write(json.dumps(scene, ensure_ascii=False) + "\n")
    user_action(f"zenscene:{sid}", "comment", args.message)
    print(f"zenscene:{sid} -> to process ({info['scene']}, {info['code']})")


# ---------------------------------------------------------------- dashboard

PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Scene review</title>
<style>
:root { --bg:#111317; --panel:#1b1e24; --line:#2c313a; --text:#e6e8ec; --dim:#8b93a1; --acc:#7cc4ff;
        --good:#5fd38d; --bad:#ff8a7a; --claude:#d9a46c; }
* { box-sizing:border-box; }
html, body { margin:0; background:var(--bg); color:var(--text); font:15px/1.45 system-ui, sans-serif; }
header { position:sticky; top:0; z-index:5; background:var(--bg); border-bottom:1px solid var(--line);
         display:flex; gap:4px; padding:8px 12px; overflow-x:auto; scrollbar-width:none; }
header button { background:none; color:var(--dim); border:0; padding:8px 12px; border-radius:6px; font:inherit;
                white-space:nowrap; cursor:pointer; }
header button.on { background:var(--panel); color:var(--text); font-weight:600; }
header .n { display:inline-block; min-width:20px; padding:0 6px; margin-left:6px; border-radius:99px;
            background:var(--line); font-size:12px; color:var(--text); }
header .n.hot { background:var(--acc); color:#111; }
main { padding:12px; max-width:1500px; margin:0 auto; }
.view { display:grid; grid-template-columns:minmax(0,1fr); gap:14px; align-items:start; max-width:1600px; margin:0 auto; }
.shots img { width:100%; display:block; border-radius:8px; background:#000; }
.shots.pair { display:grid; grid-template-columns:1fr 1fr; gap:8px; }
.shots figure { margin:0; } .shots figcaption { color:var(--dim); font-size:12px; margin:2px 0 6px; }
.panel { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:14px; }
h1 { font-size:17px; margin:0 0 6px; } .dim { color:var(--dim); } code { font-size:12px; word-break:break-all; }
.level { display:inline-block; padding:2px 10px; border-radius:99px; font-weight:600; text-transform:uppercase;
         font-size:12px; letter-spacing:.05em; }
.l0 { background:#24415a; color:#9fd4ff; } .l1 { background:#4a3d1f; color:#ffd88a; } .l2 { background:#5a2430; color:#ff9fb0; }
.timeline { position:relative; height:20px; border-radius:4px; overflow:hidden; margin:10px 0 2px; background:#000; }
.timeline div { position:absolute; top:0; bottom:0; } .timeline .mark { width:3px; background:#fff; box-shadow:0 0 4px #fff; }
dl { display:grid; grid-template-columns:auto 1fr; gap:3px 10px; margin:10px 0; } dt { color:var(--dim); } dd { margin:0; }
textarea { width:100%; height:100px; background:#0e1014; color:var(--text); border:1px solid var(--line);
           border-radius:6px; padding:8px; font:inherit; resize:vertical; }
.buttons { display:flex; flex-wrap:wrap; gap:8px; margin-top:8px; } .buttons button { flex:1 1 40%; }
button.b { border:0; border-radius:6px; padding:11px 9px; font:inherit; font-weight:600; cursor:pointer; color:#111; }
.b-go { background:var(--acc); } .b-good { background:var(--good); } .b-nav { background:#39404c; color:var(--text) !important; }
.msgs { margin-top:12px; display:flex; flex-direction:column; gap:8px; }
.msg { border-left:3px solid var(--acc); padding:2px 0 2px 9px; white-space:pre-wrap; }
.msg.claude { border-color:var(--claude); } .msg.meta { border-color:var(--line); color:var(--dim); font-size:13px; }
.msg .who { font-size:12px; color:var(--dim); }
.grid { display:grid; grid-template-columns:repeat(auto-fill, minmax(220px, 1fr)); gap:10px; }
.card { background:var(--panel); border:1px solid var(--line); border-radius:8px; overflow:hidden; cursor:pointer; }
.card img { width:100%; display:block; aspect-ratio:16/9; object-fit:cover; background:#000; }
.card div { padding:6px 8px; font-size:13px; } .card .last { color:var(--dim); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.empty { color:var(--dim); padding:40px 0; text-align:center; }
.keys { font-size:12px; color:var(--dim); margin-top:10px; } kbd { background:#2a2f38; border-radius:3px; padding:0 4px; }
.back { margin-bottom:10px; }
@media (max-width:900px) {
  .view { grid-template-columns:1fr; } .keys { display:none; } main { padding:8px; }
  .shots.pair { grid-template-columns:1fr; } .grid { grid-template-columns:repeat(auto-fill, minmax(150px, 1fr)); }
}
</style></head><body>
<header id="tabs"></header>
<main id="main"></main>
<script>
const TABS = [["scenes", "Scenes"], ["skipped", "Skipped"], ["process", "To process"], ["review", "To review"],
              ["suggest", "Suggestions"], ["done", "Done"]];
const LV = ["calm", "mid", "peak"], LVCOL = ["#24415a", "#4a3d1f", "#5a2430"];
let scenes = [], threads = {}, tab = "scenes", open = null, pos = 0;
const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const fmt = t => Math.floor(t / 60) + ":" + String(Math.floor(t % 60)).padStart(2, "0");
const status = s => (threads[s.key] || {}).status || (s.branch ? "suggest" : "scenes");
const tabOf = st => ["accepted", "dismissed"].includes(st) ? "done" : st;
const img = (s, f) => `img/${encodeURIComponent(s.batch)}/${f.split("/").map(encodeURIComponent).join("/")}`;
const inTab = t => scenes.filter(s => tabOf(status(s)) === t);

async function load() {
  const r = await (await fetch("api/state")).json();
  scenes = r.scenes; threads = r.threads; render();
}
function render() {
  $("tabs").innerHTML = TABS.map(([k, l]) => {
    const n = inTab(k).length, hot = (k === "review" || k === "scenes" || k === "suggest") && n;
    return `<button class="${k === tab ? "on" : ""}" data-t="${k}">${l}<span class="n ${hot ? "hot" : ""}">${n}</span></button>`;
  }).join("");
  document.querySelectorAll("#tabs button").forEach(b => b.onclick = () => { tab = b.dataset.t; open = null; pos = 0; render(); });
  const list = inTab(tab);
  if (tab === "scenes" && !open) { pos = Math.min(pos, Math.max(0, list.length - 1)); return showScene(list[pos], list); }
  if (open) return showScene(scenes.find(s => s.key === open), null);
  if (!list.length) return $("main").innerHTML = `<div class="empty">Nothing here.</div>`;
  $("main").innerHTML = `<div class="grid">${list.map(s => {
    const t = threads[s.key], last = t && [...t.messages].reverse().find(m => m.text);
    return `<div class="card" data-k="${s.key}"><img loading="lazy" src="${img(s, latest(s))}">
      <div><span class="level l${s.level}">${LV[s.level]}</span> ${esc(s.title || s.scene)}</div>
      <div class="last">${last ? esc((last.from === "claude" ? "Claude: " : "") + last.text) : "&nbsp;"}</div></div>`;
  }).join("")}</div>`;
  document.querySelectorAll(".card").forEach(c => c.onclick = () => { open = c.dataset.k; render(); scrollTo(0, 0); });
}
function latest(s) {
  const t = threads[s.key];
  for (const m of t ? [...t.messages].reverse() : []) if (m.after) return m.after;
  return s.image;
}
function showScene(s, list) {
  if (!s) return $("main").innerHTML = `<div class="empty">All scenes seen. New ones: <code>tools/scenereview.py generate</code></div>`;
  const t = threads[s.key] || { messages: [] }, st = status(s);
  const change = [...t.messages].reverse().find(m => m.after);
  const shots = change && st !== "scenes"
    ? `<div class="shots pair"><figure><figcaption>before</figcaption><img src="${img(s, change.before || s.image)}"></figure>
       <figure><figcaption>after</figcaption><img src="${img(s, change.after)}"></figure></div>`
    : `<div class="shots"><img src="${img(s, s.image)}"></div>`;
  const ph = (s.levels || "").split(" ").filter(Boolean).map(p => { const m = p.match(/L(\d)@(\d+)/); return [+m[1], +m[2]]; });
  const strip = ph.map(([l, t0], k) => {
    const end = k + 1 < ph.length ? ph[k + 1][1] : s.duration;
    return `<div style="left:${100 * t0 / s.duration}%;width:${100 * (end - t0) / s.duration}%;background:${LVCOL[l]};opacity:${k === s.phase ? 1 : .5}" title="${LV[l]} from ${fmt(t0)}"></div>`;
  }).join("") + `<div class="mark" style="left:${100 * s.time / s.duration}%"></div>`;
  const msgs = t.messages.map(m => m.kind === "skip" ? `<div class="msg meta">skipped</div>`
    : m.kind === "accept" ? `<div class="msg meta">accepted${m.text ? ": " + esc(m.text) : ""}</div>`
    : m.kind === "dismiss" ? `<div class="msg meta">dismissed</div>`
    : `<div class="msg ${m.from}"><div class="who">${m.from === "claude" ? "Claude" : "you"} · ${m.at.replace("T", " ")}</div>${esc(m.text)}</div>`).join("");
  let actions;
  if (st === "scenes") actions = `<button class="b b-go" id="send">Comment</button><button class="b b-nav" id="skip">Skip</button>`;
  else if (st === "review") actions = `<button class="b b-good" id="accept">Accept</button><button class="b b-go" id="send">Comment again</button>`;
  else if (st === "suggest") actions = `<button class="b b-good" id="accept">Accept</button><button class="b b-go" id="send">Comment</button><button class="b b-nav" id="dismiss">Dismiss</button>`;
  else if (st === "accepted" || st === "dismissed") actions = "";
  else actions = `<button class="b b-go" id="send">${st === "process" ? "Add to the comment" : "Comment"}</button>`;
  const hint = { scenes: "Skip moves it to Skipped; a comment sends it to Claude (To process).",
                 skipped: "A comment sends it to Claude.", process: "Waiting for Claude.",
                 review: "Accept, or comment to send it back to Claude.", done: "Accepted. A comment reopens it.",
                 suggest: "Claude's proposal (branch " + esc(s.branch || "") + ", not in main): accept to merge it, dismiss to drop it, or comment.",
                 accepted: "Accepted: Claude merges the branch.", dismissed: "Dismissed: Claude drops the branch." }[st];
  $("main").innerHTML = `${list ? "" : `<button class="b b-nav back" id="back">← ${TABS.find(x => x[0] === tab)[1]}</button>`}
  <div class="view"><div>${shots}</div><div class="panel">
    ${list ? `<div class="dim">${pos + 1} of ${list.length} new scenes</div>` : ""}
    <h1>${esc(s.title || s.scene)}</h1>${s.title ? `<div class="dim">${esc(s.scene)}</div>` : ""}
    ${s.song ? `<div><span class="level l${s.level}">${LV[s.level]}</span> ${esc(s.section)} · phase ${s.phase + 1} of ${s.phases}</div>
    <div class="timeline">${strip}</div>
    <div class="dim" style="font-size:12px">song phases (blue calm, amber mid, red peak); white: the snapshot</div>
    <dl><dt>song</dt><dd>${esc(s.title)}</dd><dt>at</dt><dd>${fmt(s.time)} of ${fmt(s.duration)} · ${s.bpm} BPM</dd>
      <dt>energy</dt><dd>${s.energy.toFixed(2)}</dd><dt>code</dt><dd><code>${esc(s.code)}</code></dd></dl>
    <div class="dim" style="font-size:12px">same scene: <code>zentris --scene ${esc(s.code)} 'ytdl:${esc(s.video)}'</code></div>`
    : `<div><span class="level l${s.level}">${LV[s.level]}</span> ${esc(s.section)} · ${s.branch ? "suggestion, branch <code>" + esc(s.branch) + "</code>" : "sent from zenscene"}</div>
    <dl><dt>code</dt><dd><code>${esc(s.code)}</code></dd></dl>
    <div class="dim" style="font-size:12px">same scene: <code>zenscene --section ${esc(s.section.toLowerCase())} --level ${s.level} ${esc(s.code)}</code></div>`}
    <div class="msgs">${msgs}</div>
    <textarea id="comment" placeholder="What is good, what should be reworked..."></textarea>
    <div class="buttons">${actions}</div>
    <div class="dim" style="font-size:12px;margin-top:6px">${hint}</div>
    ${list ? `<div class="buttons"><button class="b b-nav" id="prev">Previous</button><button class="b b-nav" id="next">Next</button></div>` : ""}
    <div class="keys"><kbd>Ctrl+Enter</kbd> comment &nbsp; <kbd>Ctrl+→</kbd> ${st === "review" || st === "suggest" ? "accept" : "skip"} &nbsp; <kbd>Ctrl+←</kbd> previous</div>
  </div></div>`;
  const on = (id, f) => $(id) && ($(id).onclick = f);
  on("send", () => act(s, "comment")); on("skip", () => act(s, "skip")); on("accept", () => act(s, "accept"));
  on("dismiss", () => act(s, "dismiss"));
  on("back", () => { open = null; render(); });
  on("prev", () => { pos = Math.max(0, pos - 1); render(); }); on("next", () => { pos = Math.min(list.length - 1, pos + 1); render(); });
  window.onkeydown = e => {
    if (!e.ctrlKey) return;
    if (e.key === "Enter") { e.preventDefault(); act(s, "comment"); }
    else if (e.key === "ArrowRight") { e.preventDefault(); act(s, st === "review" || st === "suggest" ? "accept" : "skip"); }
    else if (e.key === "ArrowLeft" && list) { e.preventDefault(); pos = Math.max(0, pos - 1); render(); }
  };
}
async function act(s, action) {
  const text = $("comment").value.trim();
  if (action === "comment" && !text) return $("comment").focus();
  const r = await fetch("api/action", { method: "POST", headers: { "Content-Type": "application/json" },
                                        body: JSON.stringify({ key: s.key, action, text }) });
  if (!r.ok) return alert(await r.text());
  threads[s.key] = await r.json();
  if (open) open = null;
  render(); scrollTo(0, 0);
}
{ // deep links: #review or #review/<scene key>
  const [t, k] = decodeURIComponent(location.hash.slice(1)).split("/");
  if (TABS.some(x => x[0] === t)) { tab = t; open = k || null; }
}
load();
setInterval(async () => {  // picks up Claude's replies, unless the user is typing
  if (document.activeElement && document.activeElement.id === "comment" && $("comment").value) return;
  const r = await (await fetch("api/state")).json();
  if (JSON.stringify(r.threads) !== JSON.stringify(threads)) { scenes = r.scenes; threads = r.threads; render(); }
}, 10000);
</script></body></html>"""


def lan_ip():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("192.0.2.1", 9))  # no packet is sent: this only picks the outgoing interface
            return s.getsockname()[0]
        except OSError:
            return "127.0.0.1"


def serve(args):

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def reply(self, body, ctype="application/json", code=200):
            data = body if isinstance(body, bytes) else body.encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = urlparse(self.path).path
            if path == "/":
                self.reply(PAGE, "text/html; charset=utf-8")
            elif path == "/api/state":
                self.reply(json.dumps({"scenes": list(all_scenes().values()), "threads": read_threads()}))
            elif path.startswith("/img/"):
                parts = [unquote(p) for p in path[5:].split("/")]
                if len(parts) < 2 or parts[0] not in batches() or any(p in ("", ".", "..") for p in parts):
                    return self.reply("not found", "text/plain", 404)
                file = os.path.join(DATA, *parts)
                if not file.endswith(".png") or not os.path.exists(file):
                    return self.reply("not found", "text/plain", 404)
                with open(file, "rb") as f:
                    self.reply(f.read(), "image/png")
            else:
                self.reply("not found", "text/plain", 404)

        def do_POST(self):
            if urlparse(self.path).path != "/api/action":
                return self.reply("not found", "text/plain", 404)
            req = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
            try:
                t = user_action(req.get("key"), req.get("action"), req.get("text", ""))
            except (KeyError, ValueError) as e:
                return self.reply(str(e), "text/plain", 400)
            self.reply(json.dumps(t))

    host = "0.0.0.0" if args.lan else "127.0.0.1"
    server = ThreadingHTTPServer((host, args.port), Handler)
    url = f"http://{lan_ip() if args.lan else '127.0.0.1'}:{args.port}/"
    print(f"scene review at {url}\nthreads: {THREADS}\nCtrl+C to stop", flush=True)
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate", help="render a batch of random scenes")
    g.add_argument("--count", type=int, default=100)
    g.add_argument("--jobs", type=int, default=3, help="renders at the same time")
    g.add_argument("--size", default="1280x720")
    s = sub.add_parser("serve", help="run the dashboard")
    s.add_argument("--port", type=int, default=8765)
    s.add_argument("--lan", action="store_true", help="listen on the local network (phone)")
    s.add_argument("--no-browser", action="store_true")
    sub.add_parser("pending", help="print the threads to process")
    w = sub.add_parser("watch", help="wait for new threads to process")
    w.add_argument("--interval", type=float, default=5)
    w.add_argument("--all", action="store_true")
    r = sub.add_parser("reply", help="answer a thread (renders the after snapshot)")
    r.add_argument("key")
    r.add_argument("--message", required=True)
    r.add_argument("--no-render", action="store_true", help="answer without a new snapshot")
    r.add_argument("--image", help="use this PNG as the after snapshot")
    sg = sub.add_parser("suggest", help="open a suggestion (a branch's change, before/after)")
    sg.add_argument("--code", required=True)
    sg.add_argument("--branch", required=True)
    sg.add_argument("--after-bin", required=True, help="the branch's zenscene binary")
    sg.add_argument("--title", required=True)
    sg.add_argument("--message", required=True)
    sg.add_argument("--section", default="VERSE")
    sg.add_argument("--level", type=int, default=1, choices=[0, 1, 2])
    sg.add_argument("--size", default="1280x720")
    sg.add_argument("--before-image", help="use this PNG as the before snapshot (e.g. a zentris shot with the HUD)")
    sg.add_argument("--after-image", help="use this PNG as the after snapshot")
    sub.add_parser("suggestions", help="list the suggestions")
    rs = sub.add_parser("resolve", help="close a merged or dropped suggestion")
    rs.add_argument("key")
    a = sub.add_parser("add", help="open a thread on a scene code (zenscene)")
    a.add_argument("--code", required=True)
    a.add_argument("--message", required=True)
    a.add_argument("--section", default="VERSE")
    a.add_argument("--level", type=int, default=1, choices=[0, 1, 2])
    a.add_argument("--size", default="1280x720")
    args = ap.parse_args()
    {"generate": generate, "serve": serve, "pending": cmd_pending, "watch": cmd_watch, "reply": cmd_reply,
     "add": cmd_add, "suggest": cmd_suggest, "suggestions": cmd_suggestions, "resolve": cmd_resolve}[args.cmd](args)


if __name__ == "__main__":
    main()
