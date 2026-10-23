#!/usr/bin/env python3
"""Checks that the daily challenge changes with the date and stays the same within a day.

Usage: day_rollover.py ROM OUTDIR [DAYS]
Starts a new game with the cartridge clock set 0..DAYS-1 days ahead, screenshots the first
two draft rounds, and fails if two days look the same or if a rerun of the same day looks
different.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from full_run import Screens, boot  # noqa: E402


def script(S, days_ahead):
    s = boot(days_ahead) + [S.until("draft"), "wait 120", "shot opponent"]
    s += ["press A", "wait 40", "press DOWN", "wait 20", "press A", S.until("field", "none"),
          S.until("draft", "none"), "wait 120", "shot draft"]
    return "\n".join(s) + "\n"


def run(rom, out, days_ahead):
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "script.txt")
    open(path, "w").write(script(Screens(rom), days_ahead))
    save = os.path.join(out, "save.sav")
    if os.path.exists(save):
        os.remove(save)
    subprocess.check_call([os.path.join(HERE, "playtest"), rom, path, out, save])
    return tuple(open(os.path.join(out, name + ".png"), "rb").read() for name in ("opponent", "draft"))


def main():
    rom, out = sys.argv[1], sys.argv[2]
    days = int(sys.argv[3]) if len(sys.argv) > 3 else 5
    seen = {}
    for d in range(days):
        shots = run(rom, os.path.join(out, "day%d" % d), d)
        for part, name in zip(shots, ("opponent", "draft")):
            if (name, part) in seen:
                print("day %d has the same %s as day %d" % (d, name, seen[(name, part)]))
                return 1
            seen[(name, part)] = d
    if run(rom, os.path.join(out, "day1_again"), 1) != run(rom, os.path.join(out, "day1"), 1):
        print("day 1 changed between runs")
        return 1
    print("day rollover OK over %d days" % days)
    return 0


if __name__ == "__main__":
    sys.exit(main())
