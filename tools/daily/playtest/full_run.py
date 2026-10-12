#!/usr/bin/env python3
"""Plays a fresh game from the title screen through the draft and into today's battle.

Usage: full_run.py ROM OUTDIR [win|lose]
Writes screenshots to OUTDIR and exits non-zero if the game hits an assertion screen.
After a few turns the outcome is forced by writing gBattleOutcome (looked up in the .map
file next to the ROM), so the run always reaches the results screen.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


B_OUTCOME = {"win": 1, "lose": 2}  # include/constants/battle.h


def symbol_address(map_path, name):
    for line in open(map_path):
        parts = line.split()
        if len(parts) == 2 and parts[1] == name and parts[0].startswith("0x"):
            return int(parts[0], 16)
    raise SystemExit("%s not found in %s" % (name, map_path))


def script(battle_outcome, outcome):
    s = ["wait 600", "press START", "wait 120", "press START", "wait 60", "press A", "wait 60"]
    s += ["press A", "wait 20"] * 120          # Birch's intro, the hall intro and draft rules
    s += ["wait 90", "shot 01_round1"]
    s += ["press RIGHT", "wait 30", "shot 02_round1_right"]
    s += ["press A", "wait 40", "shot 03_menu", "press A", "wait 120", "shot 04_summary"]
    s += ["press RIGHT", "wait 40", "shot 05_summary_stats", "press RIGHT", "wait 40", "shot 06_summary_moves"]
    s += ["press B", "wait 120", "shot 07_back"]   # back on the picker with the menu still open
    s += ["press DOWN", "wait 20", "press A", "wait 240"]
    for r in range(2, 7):
        s += ["shot 1%d_round" % r, "press A", "wait 40", "press DOWN", "wait 20", "press A", "wait 240"]
    s += ["press A", "wait 30", "press A", "wait 30", "press A", "wait 30", "press A", "wait 400", "shot 20_battle"]
    # Mash through a few turns, then force the outcome: the battle ends at the next turn action.
    # UP puts the action menu back on FIGHT; DOWN DOWN walks the party cursor off fainted Pokémon
    # when one has to be replaced, and the B presses back out of any menu that opened by accident.
    pattern = ["UP", "A", "A", "A", "DOWN", "DOWN", "A", "A", "B", "B", "B"]
    for i in range(500):
        if i == 120:
            s.append("poke8 0x%x %d" % (battle_outcome, B_OUTCOME[outcome]))
        s += ["press " + pattern[i % len(pattern)], "wait 20"]
        if i % 5 == 0 or 120 <= i <= 160:  # every press around the end of the battle
            s.append("shot 3%04d" % i)
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
    outcome = sys.argv[3] if len(sys.argv) > 3 else "win"
    battle_outcome = symbol_address(os.path.splitext(rom)[0] + ".map", "gBattleOutcome")
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "script.txt")
    open(path, "w").write(script(battle_outcome, outcome))
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
