"""Extra set sources for generate.py.

Smogon's analyses only cover a few sets per Pokémon, and few Pokémon have doubles sets at
all. Showdown's random battle data (pkmn/randbats) fills the gap: it lists roles for almost
every Pokémon in both singles and doubles, each with a movepool, items and abilities. This
module turns those roles into concrete sets, giving them EVs and a nature from the
Pokémon's base stats and the kind of moves it runs.
"""
import itertools
import os
import re

STAT_KEYS = ["hp", "atk", "def", "spa", "spd", "spe"]

# Moves that only make sense in singles; doubles fallbacks swap one of them for Protect.
SINGLES_ONLY_MOVES = {"STEALTH_ROCK", "SPIKES", "TOXIC_SPIKES", "STICKY_WEB", "RAPID_SPIN", "DEFOG",
                      "ROAR", "WHIRLWIND", "DRAGON_TAIL", "CIRCLE_THROW", "HAZE", "TOXIC",
                      "PAIN_SPLIT", "SUBSTITUTE", "SLEEP_TALK", "REST"}
BULKY_ROLE_WORDS = ("Support", "Wall", "Bulky", "Pivot", "Tera Blast user")


def load_base_stats(root):
    """SPECIES_* suffix -> {hp, atk, def, spa, spd, spe}, read from the expansion's species info."""
    stats = {}
    folder = os.path.join(root, "src", "data", "pokemon", "species_info")
    fields = {"hp": "baseHP", "atk": "baseAttack", "def": "baseDefense",
              "spa": "baseSpAttack", "spd": "baseSpDefense", "spe": "baseSpeed"}
    for name in sorted(os.listdir(folder)):
        text = open(os.path.join(folder, name)).read()
        parts = re.split(r"\[SPECIES_([A-Z0-9_]+)\]\s*=\s*\{", text)
        for i in range(1, len(parts), 2):
            block = parts[i + 1]
            found = {}
            for key, field in fields.items():
                m = re.search(r"\.%s\s*=\s*(\d+)" % field, block)
                if m:
                    found[key] = int(m.group(1))
            if len(found) == 6:
                stats[parts[i]] = found
    return stats


def load_species_abilities(root):
    """SPECIES_* suffix -> the abilities the expansion allows it, in slot order."""
    out = {}
    folder = os.path.join(root, "src", "data", "pokemon", "species_info")
    for name in sorted(os.listdir(folder)):
        text = open(os.path.join(folder, name)).read()
        parts = re.split(r"\[SPECIES_([A-Z0-9_]+)\]\s*=\s*\{", text)
        for i in range(1, len(parts), 2):
            m = re.search(r"\.abilities\s*=\s*\{([^}]*)\}", parts[i + 1])
            if m:
                abilities = re.findall(r"ABILITY_([A-Z0-9_]+)", m.group(1))
                out[parts[i]] = [a for a in abilities if a != "NONE"]
    return out


def legalize_ability(s, species_abilities):
    """Makes a set's ability one the game allows. An ability that belongs to another form
    (Greninja's Battle Bond) switches to that form; otherwise the first legal ability is used.
    Returns a note when something changed."""
    legal = species_abilities.get(s["species"])
    if not legal or s["ability"] in legal:
        return None
    # Prefer the form named after the ability (GRENINJA_BATTLE_BOND over GRENINJA_ASH).
    forms = sorted(species_abilities.items(), key=lambda f: (f[0] != s["species"] + "_" + s["ability"], f[0]))
    for form, abilities in forms:
        if form.startswith(s["species"] + "_") and s["ability"] in abilities:
            old, s["species"] = s["species"], form
            return "%s -> %s for %s" % (old, form, s["ability"])
    old, s["ability"] = s["ability"], legal[0]
    return "%s: %s -> %s" % (s["species"], old, legal[0])


def load_sound_moves(root):
    text = open(os.path.join(root, "src", "data", "moves_info.h")).read()
    parts = re.split(r"\[MOVE_([A-Z0-9_]+)\]\s*=\s*\{", text)
    return {parts[i] for i in range(1, len(parts), 2) if re.search(r"\.soundMove\s*=\s*TRUE", parts[i + 1])}


CHOICE_ITEMS = {"CHOICE_BAND", "CHOICE_SPECS", "CHOICE_SCARF"}
# Status moves a Choice holder still runs on purpose: passing the item, or fainting on use.
CHOICE_OK_STATUS = {"TRICK", "SWITCHEROO", "HEALING_WISH", "LUNAR_DANCE", "MEMENTO"}
SCREENS = {"REFLECT", "LIGHT_SCREEN", "AURORA_VEIL"}
CHARGE_MOVES = {"METEOR_BEAM", "ELECTRO_SHOT", "SKY_ATTACK", "SOLAR_BEAM", "SOLAR_BLADE", "GEOMANCY",
                "SKULL_BASH", "RAZOR_WIND", "FREEZE_SHOCK", "ICE_BURN", "BOUNCE", "DIG", "DIVE", "FLY",
                "PHANTOM_FORCE", "SHADOW_FORCE"}


def is_status(move, move_info):
    # A few damaging moves pick their category in code (Water Shuriken), so power decides too.
    category, power = move_info.get(move, ("STATUS", 0))
    return category == "STATUS" and power == 0


def item_problem(item, moves, move_info, sound_moves):
    """Why an item doesn't fit a set's moves, or None."""
    status = [m for m in moves if is_status(m, move_info)]
    if item in CHOICE_ITEMS and set(status) - CHOICE_OK_STATUS:
        return "choice item with " + ", ".join(sorted(set(status) - CHOICE_OK_STATUS))
    if item == "ASSAULT_VEST" and status:
        return "Assault Vest with " + ", ".join(status)
    if item == "LIGHT_CLAY" and not SCREENS & set(moves):
        return "Light Clay without screens"
    if item == "THROAT_SPRAY" and not sound_moves & set(moves):
        return "Throat Spray without a sound move"
    if item == "POWER_HERB" and not CHARGE_MOVES & set(moves):
        return "Power Herb without a charge move"
    return None


def legalize_item(s, move_info, sound_moves, doubles):
    """Swaps an item that doesn't fit the set's moves (Choice Scarf with Protect) for Life Orb
    on attackers, or Leftovers (Sitrus Berry in doubles) on the rest. Returns a note."""
    problem = s["item"] and item_problem(s["item"], s["moves"], move_info, sound_moves)
    if not problem:
        return None
    attacks = sum(1 for m in s["moves"] if not is_status(m, move_info))
    old = s["item"]
    s["item"] = "LIFE_ORB" if attacks >= 3 else "SITRUS_BERRY" if doubles else "LEFTOVERS"
    return "%s (%s): %s -> %s" % (s["species"], problem, old, s["item"])


def load_front_trainer_pics(root):
    """TRAINER_PIC_* suffixes that have a front sprite (back-only pics crash as opponents)."""
    text = open(os.path.join(root, "src", "data", "graphics", "trainers.h")).read()
    parts = re.split(r"\[TRAINER_PIC_([A-Z0-9_]+)\]\s*=", text)
    return {parts[i] for i in range(1, len(parts), 2) if ".frontPic" in parts[i + 1]}


def load_move_info(root):
    """MOVE_* suffix -> (category, power)."""
    text = open(os.path.join(root, "src", "data", "moves_info.h")).read()
    info = {}
    parts = re.split(r"\[MOVE_([A-Z0-9_]+)\]\s*=\s*\{", text)
    for i in range(1, len(parts), 2):
        block = parts[i + 1]
        cat = re.search(r"\.category\s*=\s*DAMAGE_CATEGORY_(\w+)", block)
        # Power can depend on the generation setting ("B_UPDATED_MOVE_DATA >= GEN_6 ? 80 : 90").
        power = re.search(r"\.power\s*=\s*([^,\n]+)", block)
        values = [int(v) for v in re.findall(r"(?<!GEN_)\b(\d+)\b", power.group(1))] if power else []
        info[parts[i]] = (cat.group(1) if cat else "STATUS", max(values, default=0))
    return info


def spread_for(moves, base, move_info, role, trick_room=False):
    """EVs and nature for a set that comes without them."""
    phys = sum(1 for m in moves if move_info.get(m, ("STATUS", 0))[0] == "PHYSICAL" and move_info[m][1] > 0)
    spec = sum(1 for m in moves if move_info.get(m, ("STATUS", 0))[0] == "SPECIAL" and move_info[m][1] > 0)
    attack = "atk" if phys >= spec else "spa"
    # Whole words only: a "Wallbreaker" is an attacker.
    bulky_role = any(re.search(r"\b%s\b" % w, role) for w in BULKY_ROLE_WORDS)
    # Bulky roles with three or more attacks invest in attack, not defense.
    bulky = (bulky_role and phys + spec <= 2) or phys + spec <= 1
    slow = trick_room or base["spe"] < 55
    if bulky_role and not bulky:
        defense = "def" if base["def"] <= base["spd"] else "spd"
        evs = {"hp": 252, attack: 252, defense: 4}
        nature = ("Brave" if attack == "atk" else "Quiet") if slow else ("Adamant" if attack == "atk" else "Modest")
    elif bulky:
        defense = "def" if base["def"] <= base["spd"] else "spd"
        evs = {"hp": 252, defense: 252, attack if phys + spec else "spe": 4}
        nature = {"def": "Impish" if attack == "atk" else "Bold",
                  "spd": "Careful" if attack == "atk" else "Calm"}[defense]
        if slow and defense == "def":
            nature = "Relaxed"
        elif slow:
            nature = "Sassy"
    elif slow:
        evs = {"hp": 252, attack: 252, "def": 4}
        nature = "Brave" if attack == "atk" else "Quiet"
    else:
        evs = {attack: 252, "spe": 252, "hp": 4}
        nature = "Jolly" if attack == "atk" else "Timid"
    ivs = {"spe": 0} if slow else {}
    if attack == "spa" and phys == 0:
        ivs["atk"] = 0
    return evs, nature.upper(), ivs


def randbats_sets(R, dex, data, species, base_stats, move_info, banned_items, banned_moves, max_sets=4):
    """Concrete sets built from a species' random battle roles."""
    entry = data.get(species)
    sp = R.species_(species)
    if not entry or not sp or sp not in base_stats:
        return []
    out = []
    for role_name, role in entry.get("roles", {}).items():
        moves = []
        for mv in role.get("moves", []):
            if mv in banned_moves or mv.startswith("Hidden Power"):
                continue
            c = R.move(mv)
            if c and c not in moves:
                moves.append(c)
        if len(moves) < 4:
            continue
        items = [R.item(i) for i in role.get("items", entry.get("items", [])) if i not in banned_items]
        items = [i for i in items if i] or [None]
        abilities = [a for a in role.get("abilities", entry.get("abilities", [])) if a in dex[species]["abilities"]]
        abilities = [R.ability(a) for a in abilities] or []
        abilities = [a for a in abilities if a]
        if not abilities:
            continue
        # Movepools can be longer than four: take a few different four-move picks, keeping the
        # role's setup or signature move (listed first only by chance) in at least one of them.
        combos = list(itertools.combinations(moves, 4))
        combos.sort(key=lambda c: sum(moves.index(m) for m in c))
        for n, combo in enumerate(combos[:3]):
            combo = list(combo)
            evs, nature, ivs = spread_for(combo, base_stats[sp], move_info, role_name,
                                          trick_room="TRICK_ROOM" in moves)
            out.append({"species": sp, "name": species, "item": items[n % len(items)],
                        "ability": abilities[n % len(abilities)], "nature": nature,
                        "evs": evs, "ivs": ivs, "moves": combo,
                        "source": "randbats: %s" % role_name})
    # Spread the picks across roles rather than taking every combo of the first role.
    by_role = {}
    for s in out:
        by_role.setdefault(s["source"], []).append(s)
    interleaved = [s for group in itertools.zip_longest(*by_role.values()) for s in group if s]
    return interleaved[:max_sets]


def doublesify(s):
    """A singles set adapted for doubles: the first singles-only move becomes Protect."""
    moves = list(s["moves"])
    if "PROTECT" not in moves:
        for i, m in enumerate(moves):
            if m in SINGLES_ONLY_MOVES:
                moves[i] = "PROTECT"
                break
    out = dict(s)
    out["moves"] = moves
    out["source"] = s["source"] + " (doubles)"
    return out
