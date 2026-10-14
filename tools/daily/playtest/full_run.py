#!/usr/bin/env python3
"""Plays a fresh game from the title screen through the draft and into today's battle.

Usage: full_run.py ROM OUTDIR [win|lose] [singles|doubles]
The cartridge clock is moved forward to the next day with the given format (the default is
today, whatever it is). Before the battle the run picks new leads: the third Pokémon, plus the
fourth on a doubles day.
Writes screenshots to OUTDIR and exits non-zero if the game hits an assertion screen.
After a few turns the outcome is forced by writing gBattleOutcome (looked up in the .map
file next to the ROM), so the run always reaches the results screen.
"""
import datetime
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


def is_doubles_day(day):
    """Mirrors IsDoublesDay in src/daily_battle.c."""
    state = (day * 2246822519 + 0xD0B1E5) & 0xFFFFFFFF
    state = (1103515245 * state + 24691) & 0xFFFFFFFF
    return (state >> 16) % 3 == 0


def days_until(fmt):
    """Days from today to the next day of the given format, by the cartridge's day count."""
    today = (datetime.date.today() - datetime.date(2000, 1, 1)).days + 1  # RtcGetLocalDayCount
    for ahead in range(30):
        if is_doubles_day(today + ahead) == (fmt == "doubles"):
            return ahead
    raise SystemExit("no %s day in the next 30 days" % fmt)


def script(battle_outcome, outcome, days_ahead, doubles):
    s = ["clockdays %d" % days_ahead, "wait 600", "press START", "wait 120", "press START", "wait 60", "press A", "wait 60"]
    # Birch's intro, the hall intro, the format and the draft rules. Doubles days have two more
    # pages; on singles days the spare presses open the picker's summary, which B backs out of.
    s += ["press A", "wait 20"] * 121
    s += ["wait 180", "press B", "wait 60", "press B", "wait 60", "press B", "wait 30", "shot 01_round1"]
    s += ["press RIGHT", "wait 30", "shot 02_round1_right"]
    s += ["press A", "wait 40", "shot 03_menu", "press A", "wait 120", "shot 04_summary"]
    s += ["press RIGHT", "wait 40", "shot 05_summary_stats", "press RIGHT", "wait 40", "shot 06_summary_moves"]
    s += ["press B", "wait 120", "shot 07_back"]   # back on the picker with the menu still open
    s += ["press DOWN", "wait 20", "press A", "wait 240"]
    for r in range(2, 7):
        s += ["shot 1%d_round" % r, "press A", "wait 40", "press DOWN", "wait 20", "press A", "wait 240"]
    # "Team ready", YES, and the lead message, then the party menu opens to pick the leads.
    s += ["press A", "wait 40"] * 4 + ["shot 18_leads"]
    s += ["press DOWN", "wait 20", "press DOWN", "wait 20", "press A", "wait 30", "press A", "wait 30"]
    if doubles:
        s += ["press DOWN", "wait 20", "press A", "wait 30", "press A", "wait 30"]
    s += ["shot 19_leads_picked", "press A", "wait 60"]  # the cursor is on CONFIRM after the last pick
    s += ["press A", "wait 60"] * 3 + ["wait 400", "shot 20_battle"]
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
    fmt = sys.argv[4] if len(sys.argv) > 4 else None
    days_ahead = days_until(fmt) if fmt else 0
    doubles = is_doubles_day((datetime.date.today() - datetime.date(2000, 1, 1)).days + 1 + days_ahead)
    print("playing %d days ahead, a %s day" % (days_ahead, "doubles" if doubles else "singles"))
    battle_outcome = symbol_address(os.path.splitext(rom)[0] + ".map", "gBattleOutcome")
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "script.txt")
    open(path, "w").write(script(battle_outcome, outcome, days_ahead, doubles))
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
