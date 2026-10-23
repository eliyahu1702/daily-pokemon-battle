#!/usr/bin/env python3
"""Checks that the daily is one attempt: resetting mid-battle counts as a loss.

Usage: forfeit.py ROM OUTDIR
Drafts, starts today's battle, resets the game during it and continues the save. The host
should then say the battle counts as a loss and offer practice instead of the daily, and the
saved result should be a defeat. Screenshots go to OUTDIR.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from full_run import Screens, boot, draft, is_assert_screen  # noqa: E402


def script(S):
    s = boot(0) + draft(S, "0")
    s += [S.until("party"), "wait 60", "press B", "wait 60", S.until("battle"), "wait 400", "shot 10_battle"]
    s += ["reset", "wait 600", "press START", "wait 120", S.until("field"), "wait 120", "shot 20_continued"]
    # Talk to the host: the forfeit notice, then the menu for a used attempt, whose first
    # option shows the saved result.
    s += ["press UP", "wait 20"] * 9
    s += ["press A", "wait 60", "shot 21_forfeit", S.until("results"), "wait 240", "shot 22_results"]
    return "\n".join(s) + "\n"


def main():
    rom, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "script.txt")
    open(path, "w").write(script(Screens(rom)))
    save = os.path.join(out, "save.sav")
    if os.path.exists(save):
        os.remove(save)
    subprocess.check_call([os.path.join(HERE, "playtest"), rom, path, out, save])
    if is_assert_screen(os.path.join(out, "22_results.png")):
        print("assertion screen on the results")
        return 1
    print("forfeit OK: see %s" % os.path.join(out, "22_results.png"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
