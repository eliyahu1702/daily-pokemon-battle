#!/usr/bin/env python3
"""Plays a fresh game from the title screen through the draft and into today's battle.

Usage: full_run.py ROM OUTDIR [win|lose] [singles|doubles|sunday|DAYS_AHEAD]
The cartridge clock is moved forward to the next day of the given kind, or by DAYS_AHEAD
days (the default is today, whatever it is). Before the battle the run picks new leads: the
third Pokémon, plus the fourth on a doubles day. After the daily it shows the results again,
pages through the records and plays a practice battle with a new draft.
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
    """A symbol's address: from the .map file, or for static symbols the ELF next to it."""
    for line in open(map_path):
        parts = line.split()
        if len(parts) == 2 and parts[1] == name and parts[0].startswith("0x"):
            return int(parts[0], 16)
    elf = os.path.splitext(map_path)[0] + ".elf"
    if os.path.exists(elf):
        for line in subprocess.check_output(["arm-none-eabi-nm", elf], text=True).splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[2] == name:
                return int(parts[0], 16)
    raise SystemExit("%s not found in %s" % (name, map_path))


class Screens:
    """`until` conditions for the game's screens: the main callback each one runs."""

    def __init__(self, rom):
        m = os.path.splitext(rom)[0] + ".map"
        self.callback2 = symbol_address(m, "gMain") + 4
        self.addr = {name: symbol_address(m, cb) | 1 for name, cb in [
            ("field", "CB2_Overworld"), ("draft", "CB2_SelectScreen"), ("party", "CB2_UpdatePartyMenu"),
            ("battle", "BattleMainCB2"), ("results", "CB2_ResultsScreen"), ("records", "CB2_RecordsScreen")]}

    def until(self, screen, key="A"):
        """Presses `key` (or nothing, with "none"; or a comma-separated list in turn) until the
        game is on `screen`."""
        return "until %s 0x%x 0x%x" % (key, self.callback2, self.addr[screen])


def is_doubles_day(day):
    """Mirrors IsDoublesDay in src/daily_battle.c."""
    state = (day * 2246822519 + 0xD0B1E5) & 0xFFFFFFFF
    state = (1103515245 * state + 24691) & 0xFFFFFFFF
    return (state >> 16) % 3 == 0


def is_sunday(day):
    """Mirrors IsSunday in src/daily_battle.c: day 1 was a Saturday."""
    return (day + 5) % 7 == 0


def day_count(days_ahead=0):
    return (datetime.date.today() - datetime.date(2000, 1, 1)).days + 1 + days_ahead  # RtcGetLocalDayCount


def days_until(fmt):
    """Days from today to the next day of the given kind: singles, doubles or sunday."""
    for ahead in range(30):
        day = day_count(ahead)
        if fmt == "sunday" and is_sunday(day) or fmt != "sunday" and is_doubles_day(day) == (fmt == "doubles"):
            return ahead
    raise SystemExit("no %s day in the next 30 days" % fmt)


def boot(days_ahead):
    return ["clockdays %d" % days_ahead, "wait 600", "press START", "wait 120", "press START", "wait 60",
            "press A", "wait 60"]


def draft(S, prefix, browse=False):
    """Drafts six from the picker: the second choice each round. With `browse`, looks around
    the first round's picker and summary first."""
    s = [S.until("draft"), "wait 120", "shot %s1_round1" % prefix]
    if browse:
        s += ["press RIGHT", "wait 30", "shot %s2_round1_right" % prefix]
        s += ["press A", "wait 40", "shot %s3_menu" % prefix, "press A", "wait 120", "shot %s4_summary" % prefix]
        s += ["press RIGHT", "wait 40", "shot %s5_summary_stats" % prefix, "press RIGHT", "wait 40",
              "shot %s6_summary_moves" % prefix]
        s += ["press B", "wait 120", "shot %s7_back" % prefix]   # back on the picker with the menu still open
        s += ["press DOWN", "wait 20", "press A"]
    else:
        s += ["press RIGHT", "wait 30", "press A", "wait 40", "press DOWN", "wait 20", "press A"]
    for r in range(2, 7):
        s += [S.until("field", "none"), S.until("draft", "none"), "wait 120", "shot %s%d_round" % (prefix, r)]
        s += ["press RIGHT", "wait 30", "press A", "wait 40", "press DOWN", "wait 20", "press A"]
    return s + [S.until("field", "none")]


def battle(S, battle_outcome, outcome, doubles, prefix):
    """Picks new leads (the third Pokémon, plus the fourth in doubles), fights a few turns and
    forces the outcome, then waits for the results screen."""
    s = [S.until("party"), "wait 60", "shot %s0_leads" % prefix]
    s += ["press DOWN", "wait 20", "press DOWN", "wait 20", "press A", "wait 30", "press A", "wait 30"]
    if doubles:
        s += ["press DOWN", "wait 20", "press A", "wait 30", "press A", "wait 30"]
    s += ["shot %s1_leads_picked" % prefix, "press A", "wait 60"]  # the cursor is on CONFIRM after the last pick
    s += [S.until("battle"), "wait 400", "shot %s2_battle" % prefix]
    # Mash through a few turns, then force the outcome: the battle ends at the next turn action.
    # UP puts the action menu back on FIGHT; DOWN DOWN walks the party cursor off fainted Pokémon
    # when one has to be replaced, and the B presses back out of any menu that opened by accident.
    pattern = ["UP", "A", "A", "A", "DOWN", "DOWN", "A", "A", "B", "B", "B"]
    for i in range(120):
        s += ["press " + pattern[i % len(pattern)], "wait 20"]
        if i % 10 == 0:
            s.append("shot %s3_turn%03d" % (prefix, i))
    s += ["poke8 0x%x %d" % (battle_outcome, B_OUTCOME[outcome]), S.until("results", ",".join(pattern))]
    s += ["wait 240", "shot %s4_results" % prefix]
    # Out of the results, then through the hall's messages: the result, new badges.
    s += ["wait 60", "press A", "wait 60", S.until("field", "none")]
    s += ["press B", "wait 40"] * 40 + ["shot %s5_after" % prefix]
    return s


def script(S, battle_outcome, outcome, days_ahead, doubles):
    # Birch's intro, the hall's welcome and menu (A picks today's battle), the opponent, the
    # format and the draft rules.
    s = boot(days_ahead)
    s += draft(S, "0", browse=True)
    s += battle(S, battle_outcome, outcome, doubles, "1")
    # Back to the host (the mashing may have walked the player away), who now offers the
    # results again, practice and records.
    s += ["press UP", "wait 20"] * 9
    s += ["press A", "wait 60", S.until("results"), "wait 240", "shot 40_results_again"]
    s += ["press A", "wait 60", S.until("field", "none"), "wait 60", "shot 41_menu_again"]
    s += ["press DOWN", "wait 20", "press DOWN", "wait 20", "shot 42_menu_records", S.until("records")]
    s += ["wait 120", "shot 43_records_stats", "press RIGHT", "wait 30", "shot 44_records_history",
          "press RIGHT", "wait 30", "press DOWN", "wait 20", "shot 45_records_badges", "press B", "wait 60",
          S.until("field", "none"), "wait 60"]
    # A practice battle with a new draft: against today's opponent after a win, and against
    # yesterday's daily (with yesterday's draft) after a loss.
    s += ["press DOWN", "wait 20", "shot 46_menu_practice", "press A", "wait 60"]
    if outcome == "lose":
        s += ["press A", "wait 60", "press A", "wait 60", "press DOWN", "wait 20", "press DOWN", "wait 20",
              "shot 47_practice_who", "press A", "wait 60", "shot 48_practice_day", "press A", "wait 60"]
    s += draft(S, "5")
    s += battle(S, battle_outcome, "win", doubles if outcome == "win" else is_doubles_day(day_count(days_ahead - 1)), "6")
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
    days_ahead = int(fmt) if fmt and fmt.isdigit() else days_until(fmt) if fmt else 0
    doubles = is_doubles_day(day_count(days_ahead))
    print("playing %d days ahead, a %s%s day" % (days_ahead, "Sunday " if is_sunday(day_count(days_ahead)) else "",
                                                 "doubles" if doubles else "singles"))
    battle_outcome = symbol_address(os.path.splitext(rom)[0] + ".map", "gBattleOutcome")
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "script.txt")
    open(path, "w").write(script(Screens(rom), battle_outcome, outcome, days_ahead, doubles))
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
