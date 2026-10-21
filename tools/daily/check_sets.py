#!/usr/bin/env python3
"""Checks every generated set in the draft pool and the daily opponents' teams.

Fails if an item doesn't fit its set's moves (a Choice item next to Protect, Assault Vest
with a status move, Light Clay without screens...) or an ability isn't one the species can
have. generate.py fixes these when it writes the sets; this guards the committed files.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import setsource  # noqa: E402


def main():
    move_info = setsource.load_move_info(ROOT)
    sound_moves = setsource.load_sound_moves(ROOT)
    abilities = setsource.load_species_abilities(ROOT)
    problems, count = [], 0
    for name in ("draft_pool.party", "daily_trainers.party"):
        text = open(os.path.join(ROOT, "src", "data", name)).read()
        section = None
        for block in text.split("\n\n"):
            header = re.search(r"^=== (\w+) ===", block, re.M)
            if header:
                section = header.group(1)
            mon = re.search(r"^(?:.*\()?SPECIES_(\w+?)\)?(?: @ ITEM_(\w+))?$", block, re.M)
            if not mon:
                continue
            count += 1
            species, item = mon.group(1), mon.group(2)
            moves = re.findall(r"^- MOVE_(\w+)", block, re.M)
            ability = re.search(r"^Ability: ABILITY_(\w+)", block, re.M)
            where = "%s %s in %s" % (name, species, section)
            problem = item and setsource.item_problem(item, moves, move_info, sound_moves)
            if problem:
                problems.append("%s: %s" % (where, problem))
            if ability and species in abilities and ability.group(1) not in abilities[species]:
                problems.append("%s: can't have %s" % (where, ability.group(1)))
    for p in problems:
        print(p)
    print("%d sets checked, %d problems" % (count, len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
