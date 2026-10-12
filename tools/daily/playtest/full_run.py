#!/usr/bin/env python3
"""Plays a fresh game from the title screen through the draft and into today's battle.

Usage: full_run.py ROM OUTDIR
Writes screenshots to OUTDIR and exits non-zero if the game hits an assertion screen.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def script():
    s = ["wait 600", "press START", "wait 120", "press START", "wait 60", "press A", "wait 60"]
    s += ["press A", "wait 20"] * 121          # Birch's intro, the hall intro and draft rules
    s += ["wait 60", "shot 01_round1_menu"]
    s += ["press DOWN"] * 3 + ["press A", "wait 120", "shot 02_summary"]
    s += ["press RIGHT", "wait 40", "shot 03_summary_stats", "press RIGHT", "wait 40", "shot 04_summary_moves"]
    s += ["press B", "wait 120", "press A", "wait 30", "shot 05_confirm", "press A", "wait 30"]
    for r in range(2, 7):
        s += ["wait 20", "shot 1%d_round" % r, "press A", "wait 20", "press A", "wait 30"]
    s += ["press A", "wait 30", "press A", "wait 30", "press A", "wait 30", "press A", "wait 400", "shot 20_battle"]
    for i in range(300):
        s += ["press A", "wait 15"]
        if i % 30 == 0:
            s.append("shot 3%03d_battle" % i)
    return "\n".join(s) + "\n"


def is_assert_screen(png):
    from PIL import Image
    im = Image.open(png).convert("RGB")
    w, h = im.size
    blue = sum(1 for x in range(0, w, 4) for y in range(0, h, 4)
               if (lambda p: p[2] > 200 and p[0] < 40 and p[1] < 40)(im.getpixel((x, y))))
    return blue > (w // 4) * (h // 4) // 2


def main():
    rom, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "script.txt")
    open(path, "w").write(script())
    save = os.path.join(out, "save.sav")
    if os.path.exists(save):
        os.remove(save)
    subprocess.check_call([os.path.join(HERE, "playtest"), rom, path, out, save])
    bad = [f for f in sorted(os.listdir(out)) if f.endswith(".png") and is_assert_screen(os.path.join(out, f))]
    if bad:
        print("assertion screen in: " + ", ".join(bad))
        return 1
    print("full run OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
