#!/usr/bin/env python3
"""Skybox seam check: are the six faces of each skybox set oriented so the picture is continuous across the cube edges?

For every set (or --sets a/b,c/d) it points the ship at the top/front, bottom/front and two side seams (via saved games, so no
mouse or keyboard is needed), takes a screenshot, and compares the pixels on either side of the seam with the natural pixel-to-pixel
change elsewhere in the picture. ratio 1.0 = seamless; higher = a visible seam. Fails (exit 1) if any ratio > --threshold.

  package/tools/skybox_seams.py                       # all sets
  package/tools/skybox_seams.py --sets dark/set1,red/set2 --threshold 2.5

Needs: the built game (./space_game_v2), python3 with numpy and Pillow, a display. Does not touch assets/: it works on symlinked copies.
Temporarily replaces config/game.json (restored afterwards).
"""
import argparse, json, math, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

S = 1 / math.sqrt(2)
VIEWS = {  # name: (forward, up, seam is vertical?)
    "top":              ((0, S, -S), (0, S, S), False),
    "bottom":           ((0, -S, -S), (0, S, -S), False),
    "front/right":      ((S, 0, -S), (0, 1, 0), True),
    "back/left":        ((-S, 0, S), (0, 1, 0), True),
}
ROOT = "logs/skybox_seams"


def save_game(name, fwd, up):
    d = f"{ROOT}/sv_{name.replace('/', '_')}"
    os.makedirs(d, exist_ok=True)
    flight = {"pos": [5000, 5000, 5000], "vel": [0, 0, 0], "fwd": list(fwd), "up": list(up), "hp": 100, "maxHp": 100, "shield": 0,
              "shieldInstalled": False, "shieldEnabled": False, "warpFuel": 100, "alive": True}
    json.dump({"format": 1, "time": 1789000000, "modules": {"gameplay/flight": flight}}, open(d + "/save_a.json", "w"))
    return d


def ratio(path, vertical):
    a = np.asarray(Image.open(path).convert("RGB")).astype(float)
    if vertical:   # the seam runs top to bottom through the middle: compare columns on either side of it
        d = lambda x: np.abs(a[120:600, x - 4] - a[120:600, x + 4]).mean()
        seam, base = d(640), np.mean([d(x) for x in (300, 420, 860, 980)])
    else:
        d = lambda y: np.abs(a[y - 4, 300:980] - a[y + 4, 300:980]).mean()
        seam, base = d(360), np.mean([d(y) for y in (200, 240, 480, 520)])
    return seam / (base + 1e-6)


def find_sets(root):
    out = []
    for col in sorted(os.listdir(root)):
        for st in sorted(os.listdir(f"{root}/{col}")):
            d = f"{root}/{col}/{st}"
            if os.path.isdir(d) and sum(os.path.exists(f"{d}/{f}.png") for f in ("front", "back", "left", "right", "top", "bot")) >= 6:
                out.append(f"{col}/{st}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sets", default="", help="comma separated color/set names (default: all)")
    ap.add_argument("--threshold", type=float, default=2.5, help="ratios up to ~2.1 occur naturally (content-dependent); a real orientation bug scored 2.7-3.6")
    ap.add_argument("--game", default="./space_game_v2")
    ap.add_argument("--assets", default="assets/skybox/bkg")
    a = ap.parse_args()
    sets = [s for s in a.sets.split(",") if s] or find_sets(a.assets)
    env = dict(os.environ, SDL_AUDIODRIVER="dummy")
    backup = open("config/game.json").read() if os.path.exists("config/game.json") else None
    shutil.rmtree(ROOT, ignore_errors=True)
    os.makedirs(ROOT)
    json.dump({}, open(f"{ROOT}/settings.json", "w"))
    bad = 0
    print(f"{'set':<16}" + "".join(f"{v:>13}" for v in VIEWS) + "   (1.0 = seamless)")
    try:
        for st in sets:
            col, name = st.split("/")
            src = os.path.abspath(f"{a.assets}/{col}/{name}")
            base = f"{ROOT}/bkg/{col}/{name}"
            os.makedirs(base)
            for f in os.listdir(src):
                os.symlink(os.path.join(src, f), os.path.join(base, f))   # includes the set's own skybox.json
            json.dump({"skybox": {"set": st, "dir": f"{ROOT}/bkg"}, "cockpit": {"enabled": False}}, open("config/game.json", "w"))
            row = f"{st:<16}"
            for vname, (fwd, up, vertical) in VIEWS.items():
                d = save_game(vname, fwd, up)
                shot = f"{ROOT}/shot.bmp"
                subprocess.run([a.game, "--frames=45", "--paused", "--ui-click=640,388,10;640,357,20", f"--saves={d}",
                                f"--settings={ROOT}/settings.json", f"--screenshot={shot}", "--screenshot-frame=35"], capture_output=True, env=env)
                r = ratio(shot, vertical)
                bad += r > a.threshold
                row += f"{r:>10.2f}{'  X' if r > a.threshold else '   '}"
            print(row)
    finally:
        if backup is None:
            if os.path.exists("config/game.json"): os.remove("config/game.json")
        else:
            open("config/game.json", "w").write(backup)
        shutil.rmtree(ROOT, ignore_errors=True)
    print(f"\n{'FAIL' if bad else 'OK'}: {bad} seam(s) above {a.threshold}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
