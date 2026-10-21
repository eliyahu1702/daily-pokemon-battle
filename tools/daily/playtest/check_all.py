#!/usr/bin/env python3
"""Builds every daily opponent's team (singles and doubles) and every draft set once.

Usage: check_all.py ROM OUTDIR
Sets gDailySelfTest before the hall picks today's opponent, which makes the game create
every team and draft set in one go. A set the game rejects stops it on an assertion screen,
saved as OUTDIR/selftest.png, and the check fails. Much faster than playing every day.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from full_run import symbol_address  # noqa: E402


def main():
    rom, out = sys.argv[1], sys.argv[2]
    flag = symbol_address(os.path.splitext(rom)[0] + ".map", "gDailySelfTest")
    os.makedirs(out, exist_ok=True)
    s = ["wait 600", "poke8 0x%x 1" % flag, "press START", "wait 120", "press START", "wait 60",
         "press A", "wait 60"]
    s += ["press A", "wait 20"] * 110   # up to the hall's opponent announcement
    s += ["wait 1800", "shot selftest", "expect8 0x%x 2" % flag]
    path = os.path.join(out, "script.txt")
    open(path, "w").write("\n".join(s) + "\n")
    save = os.path.join(out, "save.sav")
    if os.path.exists(save):
        os.remove(save)
    if subprocess.call([os.path.join(HERE, "playtest"), rom, path, out, save]) != 0:
        print("self-test did not finish: see %s" % os.path.join(out, "selftest.png"))
        return 1
    print("every opponent team and draft set builds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
