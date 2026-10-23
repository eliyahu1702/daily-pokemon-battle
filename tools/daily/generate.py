#!/usr/bin/env python3
"""Generate the draft pool and daily opponent teams from Smogon set data.

Every draft Pokémon gets several sets for singles days and several for doubles days; the
game picks one per day. Every opponent gets a singles team and a doubles team.

Inputs (see tools/daily/README.md for how to refresh them):
  tools/daily/data/dex.json   species info exported from @pkmn/dex
  tools/daily/data/sets.json  trimmed Smogon sets (built by --refresh from a pkmn/smogon checkout)
  tools/daily/data/randbats_singles.json, randbats_doubles.json
                              Showdown's gen 9 random battle roles, from pkmn/randbats

Outputs (Showdown-format party files consumed by tools/trainerproc):
  src/data/draft_pool.party
  src/data/daily_trainers.party
  include/constants/daily_battle_generated.h
"""
import argparse
import copy
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from leaders import LEADERS  # noqa: E402
from opponents import BOSSES, COMPETITIVE, CUSTOM_SETS, SIGNATURE  # noqa: E402
import setsource  # noqa: E402

LEVEL = 100
DRAFT_CHUNK_SIZE = 200
MIN_DRAFT_SETS = 3  # a Pokémon needs this many sets in a format to be drafted in it
MAX_DRAFT_SETS = 4

# Smogon formats to take sets from, best first. The draft prefers standard singles
# tiers; opponents prefer monotype sets, which are written for exactly this situation.
DRAFT_FORMATS = ["ou", "uu", "ru", "nu", "pu", "zu", "nationaldex", "nationaldexuu",
                 "nationaldexru", "monotype", "nationaldexmonotype", "battlestadiumsingles", "1v1"]
OPPONENT_FORMATS = ["monotype", "nationaldexmonotype", "ou", "nationaldex", "uu", "ru",
                    "nationaldexuu", "nationaldexru", "nu", "pu", "zu", "battlestadiumsingles", "1v1"]
DOUBLES_FORMATS = ["doublesou", "vgc2025", "vgc2024", "vgc2023", "nationaldexdoubles", "battlestadiumdoubles",
                   "vgc2022", "vgc2021", "vgc2020", "2v2doubles"]
OLD_GENS = ["gen8", "gen7"]
MODES = ["singles", "doubles"]

BANNED_ITEMS = {"Eviolite", "Light Ball", "Thick Club", "Leek", "Stick"}
# Opponents may use the species-specific items, since their teams include canon NFE aces.
OPPONENT_BANNED_ITEMS = BANNED_ITEMS - {"Eviolite", "Light Ball"}
BANNED_MOVES = {"Hidden Power", "Return", "Frustration", "Baton Pass", "Last Respects", "Tera Blast",
                "Shed Tail", "Revival Blessing", "Fissure", "Sheer Cold", "Horn Drill", "Guillotine"}
BANNED_ABILITIES = {"Moody", "Shadow Tag", "Arena Trap"}

ITEMS = json.load(open(os.path.join(HERE, "data", "items.json")))

STAT_NAMES = {"hp": "HP", "atk": "Atk", "def": "Def", "spa": "SpA", "spd": "SpD", "spe": "Spe"}

# Showdown name -> expansion constant suffix, where plain normalisation is wrong.
SPECIES_ALIASES = {
    "Lycanroc": "LYCANROC_MIDDAY",
    "Lycanroc-Midnight": "LYCANROC_MIDNIGHT",
    "Lycanroc-Dusk": "LYCANROC_DUSK",
    "Tauros-Paldea-Combat": "TAUROS_PALDEA_COMBAT",
    "Tauros-Paldea-Blaze": "TAUROS_PALDEA_BLAZE",
    "Tauros-Paldea-Aqua": "TAUROS_PALDEA_AQUA",
    "Indeedee": "INDEEDEE_M",
    "Meowstic": "MEOWSTIC_M",
    "Basculegion": "BASCULEGION_M",
    "Oinkologne": "OINKOLOGNE_M",
    "Toxtricity": "TOXTRICITY_AMPED",
    "Urshifu": "URSHIFU_SINGLE_STRIKE",
    "Darmanitan-Galar": "DARMANITAN_GALAR_STANDARD",
    "Darmanitan": "DARMANITAN_STANDARD",
    "Wormadam": "WORMADAM_PLANT",
    "Gourgeist": "GOURGEIST_AVERAGE",
    "Oricorio": "ORICORIO_BAILE",
    "Morpeko": "MORPEKO_FULL_BELLY",
    "Eiscue": "EISCUE_ICE",
    "Mimikyu": "MIMIKYU_DISGUISED",
    "Polteageist": "POLTEAGEIST",
    "Alcremie": "ALCREMIE_STRAWBERRY_VANILLA_CREAM",
    "Vivillon": "VIVILLON_MEADOW",
    "Florges": "FLORGES_RED",
    "Squawkabilly": "SQUAWKABILLY_GREEN",
    "Dudunsparce": "DUDUNSPARCE_TWO_SEGMENT",
    "Palafin": "PALAFIN_ZERO",
    "Maushold": "MAUSHOLD_FAMILY_OF_FOUR",
    "Tatsugiri": "TATSUGIRI_CURLY",
    "Aegislash": "AEGISLASH_SHIELD",
    "Wishiwashi": "WISHIWASHI_SOLO",
    "Minior": "MINIOR_METEOR_RED",
    "Ogerpon": "OGERPON_TEAL",
    "Sinistcha": "SINISTCHA",
    "Pumpkaboo": "PUMPKABOO_AVERAGE",
}
MOVE_ALIASES = {}
ITEM_ALIASES = {"Never-Melt Ice": "NEVER_MELT_ICE"}
ABILITY_ALIASES = {}


def norm(name):
    s = name.replace("é", "e").replace("’", "").replace("'", "").replace(".", "")
    s = s.replace("♀", "_F").replace("♂", "_M").replace(":", "")
    s = re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_").upper()
    return s


def load_constants(prefix, header):
    text = open(os.path.join(ROOT, "include", "constants", header)).read()
    return set(re.findall(r"\b%s_([A-Z0-9_]+)\b" % prefix, text))


class Resolver:
    def __init__(self):
        self.species = load_constants("SPECIES", "species.h")
        self.moves = load_constants("MOVE", "moves.h")
        self.items = load_constants("ITEM", "items.h")
        self.abilities = load_constants("ABILITY", "abilities.h")
        self.misses = {}

    def _get(self, kind, table, aliases, name):
        key = aliases.get(name, norm(name))
        if key in table:
            return key
        self.misses.setdefault(kind, set()).add(name)
        return None

    def species_(self, n): return self._get("species", self.species, SPECIES_ALIASES, n)
    def move(self, n): return self._get("move", self.moves, MOVE_ALIASES, n)
    def item(self, n): return self._get("item", self.items, ITEM_ALIASES, n)
    def ability(self, n): return self._get("ability", self.abilities, ABILITY_ALIASES, n)


def first(v):
    return v[0] if isinstance(v, list) else v


def options(v):
    return v if isinstance(v, list) else [v]


def build_set(R, dex, species, raw, banned_items=BANNED_ITEMS):
    """Turn one Smogon set into a validated, expansion-ready set, or None."""
    info = dex.get(species)
    if not info:
        return None
    sp = R.species_(species)
    if not sp:
        return None
    if any(ITEMS.get(it, {}).get("mega") for it in options(raw.get("item", []))):
        return None  # written for the Mega form, which the draft leaves out
    item = None
    for it in options(raw.get("item", [])):
        meta = ITEMS.get(it, {})
        if it in banned_items or meta.get("mega") or meta.get("z"):
            continue
        c = R.item(it)
        if c:
            item = c
            break
    ability = None
    for ab in options(raw.get("ability", [])) or info["abilities"][:1]:
        if ab in BANNED_ABILITIES or ab not in info["abilities"]:
            continue
        c = R.ability(ab)
        if c:
            ability = c
            break
    if ability is None:
        for ab in info["abilities"]:
            if ab not in BANNED_ABILITIES and R.ability(ab):
                ability = R.ability(ab)
                break
    moves = []
    for slot in raw.get("moves", []):
        for mv in options(slot):
            if mv in BANNED_MOVES or mv.startswith("Hidden Power") or mv.startswith("Max ") or mv.startswith("G-Max"):
                continue
            c = R.move(mv)
            if c and c not in moves:
                moves.append(c)
                break
    if len(moves) < 3 or ability is None:
        return None
    nature = norm(first(raw.get("nature", "Serious")) or "Serious")
    evs = first(raw.get("evs", {})) or {}
    ivs = first(raw.get("ivs", {})) or {}
    return {"species": sp, "name": species, "item": item, "ability": ability,
            "nature": nature, "evs": evs, "ivs": ivs, "moves": moves[:4]}


# Sets that only work under a weather the team doesn't set up go last outside weather teams.
WEATHER_ABILITIES = {"CHLOROPHYLL", "SWIFT_SWIM", "SAND_RUSH", "SLUSH_RUSH", "SOLAR_POWER"}
WEATHER_MOVES = {"SOLAR_BEAM", "WEATHER_BALL"}


def needs_weather(s):
    return s["ability"] in WEATHER_ABILITIES or bool(WEATHER_MOVES & set(s["moves"]))


class SetSource:
    """Every set the generator can use, by species and format (singles or doubles)."""

    def __init__(self, R, dex, sets_by_gen):
        self.R, self.dex, self.sets_by_gen = R, dex, sets_by_gen
        self.randbats = {m: json.load(open(os.path.join(HERE, "data", "randbats_%s.json" % m))) for m in MODES}
        self.base_stats = setsource.load_base_stats(ROOT)
        self.move_info = setsource.load_move_info(ROOT)
        self.cache = {}

    def with_all_stats(self):
        """A copy that also builds random battle sets for species with config-dependent stats
        (Mewtwo, say). Only the bosses' legendaries use it, so the draft pool stays as it was."""
        if not hasattr(self, "_all_stats"):
            self._all_stats = copy.copy(self)
            self._all_stats.base_stats = setsource.load_base_stats(ROOT, conditional=True)
            self._all_stats.cache = {}
        return self._all_stats

    def smogon(self, species, formats, banned_items):
        full, partial = [], []
        for gen in ["gen9"] + OLD_GENS:
            entry = self.sets_by_gen.get(gen, {}).get(species)
            if not entry:
                continue
            for fmt in [f for f in formats if f in entry]:
                for set_name, raw in entry[fmt].items():
                    s = build_set(self.R, self.dex, species, raw, banned_items)
                    if s:
                        s["source"] = "%s%s: %s" % (gen, fmt, set_name)
                        (full if len(s["moves"]) == 4 else partial).append(s)
        if species in CUSTOM_SETS:
            s = build_set(self.R, self.dex, species, CUSTOM_SETS[species], banned_items)
            if s:
                s["source"] = "custom"
                full.append(s)
        return full + partial

    def all(self, species, formats, banned_items=BANNED_ITEMS, weather=False, mode="singles"):
        """Every distinct set, best source first: Smogon analyses, then random battle roles,
        and for doubles, singles sets adapted with Protect as a last resort. Without
        `weather`, sets that rely on a weather setter come after the others."""
        key = (species, tuple(formats), tuple(sorted(banned_items)), weather, mode)
        if key in self.cache:
            return self.cache[key]
        if mode == "doubles":
            found = self.smogon(species, DOUBLES_FORMATS, banned_items)
        else:
            found = self.smogon(species, formats, banned_items)
        found += setsource.randbats_sets(self.R, self.dex, self.randbats[mode], species, self.base_stats,
                                         self.move_info, banned_items, BANNED_MOVES)
        if mode == "doubles":
            found += [setsource.doublesify(x) for x in self.all(species, formats, banned_items, weather)]
        sets, seen = [], set()
        for x in found:
            k = (x["item"], x["ability"], tuple(sorted(x["moves"])))
            if k not in seen:
                seen.add(k)
                sets.append(x)
        full = [x for x in sets if len(x["moves"]) == 4]
        sets = full or sets
        if not weather:
            sets = [x for x in sets if not needs_weather(x)] + [x for x in sets if needs_weather(x)]
        self.cache[key] = sets
        return sets

    def real_doubles(self, species, formats=(), banned_items=BANNED_ITEMS):
        """Doubles sets written for doubles, not adapted from singles."""
        return [x for x in self.all(species, formats, banned_items, mode="doubles")
                if not x["source"].endswith("(doubles)")]


def pick_set(S, species, formats, variant=0, banned_items=BANNED_ITEMS, weather=False, move=None, mode="singles"):
    """The best set, or for variant n the n-th distinct one (wrapping around). With `move`,
    prefer a set that has it, and otherwise put it in the last slot."""
    sets = S.all(species, formats, banned_items, weather, mode)
    if not sets:
        return None
    if move:
        c = S.R.move(move)
        with_move = [x for x in sets if c in x["moves"]]
        if with_move:
            return with_move[0]
        s = dict(sets[0])
        s["moves"] = s["moves"][:3] + [c]
        s["source"] += " + " + move
        return s
    return sets[variant % len(sets)]


STRONG_TIERS = {"OU", "UUBL", "UU", "RUBL", "RU", "NUBL", "NU"}
DEEP_TIERS = STRONG_TIERS | {"PUBL", "PU", "(NU)"}


def tier_of(info):
    """Smogon tier: the current-gen tier, or the National Dex tier for mons not in SV."""
    if info["ns"] == "Past":
        return info["natDexTier"] or "Illegal"
    return info["tier"]


def legal(info):
    if info["nfe"] or info["battleOnly"] or info["ns"] not in (None, "Past"):
        return False
    forme = info["forme"] or ""
    if "Mega" in forme or "Gmax" in forme or "Primal" in forme or "Totem" in forme:
        return False
    return "Restricted Legendary" not in info["tags"] and "Mythical" not in info["tags"]


def eligible(info, deep=False):
    if not legal(info):
        return False
    t = tier_of(info)
    if info["ns"] == "Past" and t == "RU":
        # National Dex lumps everything below UU into RU, so weed out the weak ones.
        return info["bst"] >= (470 if deep else 500)
    return t in (DEEP_TIERS if deep else STRONG_TIERS)


def party_text(s, nickname=None):
    lines = []
    head = "%s (SPECIES_%s)" % (nickname, s["species"]) if nickname else "SPECIES_%s" % s["species"]
    if s["item"]:
        head += " @ ITEM_%s" % s["item"]
    lines.append(head)
    lines.append("Level: %d" % LEVEL)
    lines.append("Ability: ABILITY_%s" % s["ability"])
    if s["evs"]:
        lines.append("EVs: " + " / ".join("%d %s" % (v, STAT_NAMES[k]) for k, v in s["evs"].items() if v))
    iv = {k: 31 for k in STAT_NAMES}
    iv.update(s["ivs"])
    lines.append("IVs: " + " / ".join("%d %s" % (iv[k], STAT_NAMES[k]) for k in STAT_NAMES))
    lines.append("Nature: NATURE_%s" % s["nature"])
    for m in s["moves"]:
        lines.append("- MOVE_%s" % m)
    return "\n".join(lines)


def stable_hash(text):
    return int(hashlib.sha1(text.encode()).hexdigest()[:8], 16)


TIER_RANK = {t: i for i, t in enumerate(["OU", "UUBL", "UU", "RUBL", "RU", "NUBL", "NU", "PUBL", "PU", "(PU)", "ZU"])}


def draft_key(dex, name, species):
    # Forms that share a name (Silvally) would look like duplicates in the draft menu; Rotom
    # stays split since its forms have different names.
    info = dex[name]
    if not info["forme"] or info["base"] in ("Silvally", "Arceus", "Genesect", "Oricorio", "Urshifu",
                                             "Toxtricity", "Gourgeist", "Lycanroc"):
        return info["base"]
    return species


def build_draft(S, dex, mode):
    """The draft pool for one format: a list of (species name, [sets]) with at least
    MIN_DRAFT_SETS sets each."""
    pool, seen = [], set()
    for name, info in sorted(dex.items(), key=lambda kv: (kv[1]["num"], kv[0])):
        # Doubles also takes Pokémon that only shine there (Indeedee, Amoonguss in any tier).
        if not eligible(info) and not (mode == "doubles" and legal(info)
                                       and S.smogon(name, DOUBLES_FORMATS, BANNED_ITEMS)):
            continue
        sets = S.all(name, DRAFT_FORMATS, mode=mode)
        if mode == "doubles":
            # Adapted singles sets only top a Pokémon up to the minimum.
            real = S.real_doubles(name)[:MAX_DRAFT_SETS]
            sets = real + [x for x in sets if x["source"].endswith("(doubles)")][:max(0, MIN_DRAFT_SETS - len(real))]
        if len(sets) < MIN_DRAFT_SETS:
            continue
        key = draft_key(dex, name, sets[0]["species"])
        if key in seen or sets[0]["species"] in seen:
            continue
        seen.update((key, sets[0]["species"]))
        pool.append((name, sets[:MAX_DRAFT_SETS]))
    return pool


def build_leader_team(S, dex, leader, mode):
    key, name, title, typ, pic, signature = leader
    team, used_base = [], set()

    def try_add(species):
        info = dex.get(species)
        if not info or typ not in info["types"] or info["base"] in used_base:
            return
        if not eligible(info, deep=True):
            return
        s = pick_set(S, species, OPPONENT_FORMATS, mode=mode)
        if s:
            team.append(s)
            used_base.add(info["base"])

    for sp in signature:
        if len(team) < 6:
            try_add(sp)
    # Fill with the strongest mons of the type, shuffled per leader so teams differ.
    candidates = [n for n, i in dex.items() if typ in i["types"]]
    # In doubles, Pokémon with real doubles sets go first.
    candidates.sort(key=lambda n: (mode == "doubles" and not S.real_doubles(n),
                                   TIER_RANK.get(tier_of(dex[n]), 99), stable_hash(key + n)))
    for sp in candidates:
        if len(team) >= 6:
            break
        try_add(sp)
    return team


def build_signature_team(S, dex, entry, variant, mode):
    """Variant 0 is the first six of the pool; later variants keep the ace and draw the rest
    from the whole pool, with alternate sets."""
    key, name, cls, pic, theme, variants, pool = entry
    pool = [sp for sp in pool if sp in dex and not banned_species(dex[sp])]
    if variant == 0:
        order = pool
    else:
        rest = sorted(pool[1:], key=lambda sp: stable_hash("%s%d%s" % (key, variant, sp)))
        order = [pool[0]] + rest
    team, used_base = [], set()
    for sp in order:
        if len(team) >= 6:
            break
        if dex[sp]["base"] in used_base:
            continue
        s = pick_set(S, sp, OPPONENT_FORMATS, variant, OPPONENT_BANNED_ITEMS, mode=mode)
        if s:
            team.append(s)
            used_base.add(dex[sp]["base"])
        else:
            print("warning: no set for %s (%s)" % (sp, key), file=sys.stderr)
    return team


def build_fixed_team(S, dex, key, species, mode):
    """`species` entries are names, optionally "Name:Move" to require a move (a Trick Room
    setter, say)."""
    team = []
    for entry in species:
        sp, _, move = entry.partition(":")
        s = sp in dex and pick_set(S, sp, ARCHETYPE_FORMATS, 0, OPPONENT_BANNED_ITEMS,
                                   weather=True, move=move or None, mode=mode)
        if s:
            team.append(s)
        else:
            print("warning: no set for %s (%s)" % (sp, key), file=sys.stderr)
    return team


def banned_species(info):
    # Box legends and mythicals stay out: the player can't draft them either.
    return "Restricted Legendary" in info["tags"] or "Mythical" in info["tags"]


# Archetype teams come from the standard singles tiers, where those sets are written for teams.
ARCHETYPE_FORMATS = ["ou", "nationaldex", "uu", "nationaldexuu", "ru", "nationaldexru", "nu",
                     "monotype", "nationaldexmonotype", "battlestadiumsingles", "pu", "zu", "1v1"]


def all_opponents(S, dex, mode):
    """Every daily opponent in one format: dicts with key, name, class, pic, music, theme and team."""
    opponents = []
    for leader in LEADERS:
        key, name, title, typ, pic, _ = leader
        team = build_leader_team(S, dex, leader, mode)
        if len(team) < 6:
            print("warning: %s only has %d %s mons" % (key, len(team), typ), file=sys.stderr)
        opponents.append({"key": key, "name": name, "class": title, "pic": pic,
                          "music": "Intense" if title == "Leader" else "Elite Four",
                          "theme": "%s %s-type specialist!" % ("An" if typ[0] in "AEIOU" else "A", typ.upper()),
                          "kind": typ, "team": team})
    for entry in SIGNATURE:
        key, name, cls, pic, theme, variants, _ = entry
        for v in range(variants):
            team = build_signature_team(S, dex, entry, v, mode)
            opponents.append({"key": "%s_%d" % (key, v + 1), "name": name, "class": cls, "pic": pic,
                              "music": "Elite Four", "theme": theme,
                              "kind": "signature" if v == 0 else "variant", "team": team})
    for key, name, cls, pic, theme, species in COMPETITIVE:
        opponents.append({"key": key, "name": name, "class": cls, "pic": pic, "music": "Intense",
                          "theme": theme, "kind": "archetype",
                          "team": build_fixed_team(S, dex, key, species, mode)})
    # Sunday bosses come last, so the other days' order doesn't change when bosses are added.
    signature = {e[0]: e for e in SIGNATURE}
    for key, legend, theme in BOSSES:
        entry = signature[key]
        ace = pick_set(S.with_all_stats(), legend, OPPONENT_FORMATS, 0, OPPONENT_BANNED_ITEMS, mode=mode)
        assert ace, legend
        rest = [x for x in build_signature_team(S, dex, entry, 0, mode) if x["name"] != legend]
        team = [ace] + rest[:5]
        opponents.append({"key": "BOSS_" + key, "name": entry[1], "class": entry[2], "pic": entry[3],
                          "music": "Elite Four", "theme": theme, "kind": "boss", "legend": legend,
                          "team": team})
    for o in opponents:
        assert len(o["name"]) <= 10, o["name"]
        if len(o["team"]) < 6:
            print("warning: %s has only %d mons" % (o["key"], len(o["team"])), file=sys.stderr)
    return opponents


AI = "Basic Trainer / Omniscient / Smart Switching / Smart Mon Choices / Predict Switch / Predict Move / Try To 2HKO / HP Aware / Weigh Ability Prediction / PP Stall Prevention"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--refresh", metavar="SMOGON_SETS_DIR",
                    help="rebuild data/sets.json from a pkmn/smogon data/sets checkout")
    args = ap.parse_args()

    dex = json.load(open(os.path.join(HERE, "data", "dex.json")))
    sets_path = os.path.join(HERE, "data", "sets.json")
    if args.refresh:
        sets_by_gen = {}
        for gen in ["gen9"] + OLD_GENS:
            full = json.load(open(os.path.join(args.refresh, gen + ".json")))
            keep = set(DRAFT_FORMATS) | set(OPPONENT_FORMATS) | set(DOUBLES_FORMATS)
            sets_by_gen[gen] = {sp: {f: v for f, v in fm.items() if f in keep} for sp, fm in full.items()}
            sets_by_gen[gen] = {sp: fm for sp, fm in sets_by_gen[gen].items() if fm}
        json.dump(sets_by_gen, open(sets_path, "w"), separators=(",", ":"), sort_keys=True)
    sets_by_gen = json.load(open(sets_path))

    R = Resolver()
    S = SetSource(R, dex, sets_by_gen)
    report = []

    # The draft pool: each format's Pokémon with their sets, back to back. trainerproc caps a
    # party at 255 mons, so the sets are split into chunks.
    pools = {mode: build_draft(S, dex, mode) for mode in MODES}
    flat, tables = [], {}
    for mode in MODES:
        first, counts = [], []
        for name, sets in pools[mode]:
            first.append(len(flat))
            counts.append(len(sets))
            flat += sets
        tables[mode] = (first, counts)
        report.append("Draft pool, %s: %d Pokémon, %d sets\n%s\n" % (
            mode, len(pools[mode]), sum(counts), ", ".join("%s (%d)" % (n, len(x)) for n, x in pools[mode])))
    # Smogon's data allows some abilities that belong to another form in the game, and the
    # game asserts when a trainer Pokémon has an ability its species can't have.
    species_abilities = setsource.load_species_abilities(ROOT)
    opponents = {mode: all_opponents(S, dex, mode) for mode in MODES}
    # Items have to fit the moves too: random battle roles list items for the whole role, so a
    # four-move pick can end up with a Choice item next to Protect.
    sound_moves = setsource.load_sound_moves(ROOT)
    fixes, item_fixes = set(), []
    for mode in MODES:
        for x in [x for _, sets in pools[mode] for x in sets] + [x for o in opponents[mode] for x in o["team"]]:
            note = setsource.legalize_ability(x, species_abilities)
            if note:
                fixes.add(note)
            note = setsource.legalize_item(x, S.move_info, sound_moves, mode == "doubles")
            if note:
                item_fixes.append("%s %s" % (mode, note))
    if fixes:
        report.append("Ability fixes:\n" + "\n".join(sorted(fixes)) + "\n")
    if item_fixes:
        report.append("Item fixes:\n" + "\n".join(item_fixes) + "\n")
    front_pics = setsource.load_front_trainer_pics(ROOT)
    bad_pics = {o["pic"] for mode in MODES for o in opponents[mode] if norm(o["pic"]) not in front_pics}
    if bad_pics:
        raise SystemExit("trainer pics without a front sprite: " + ", ".join(sorted(bad_pics)))

    out = ["/* Generated by tools/daily/generate.py. Do not edit by hand. */", ""]
    chunks = [flat[i:i + DRAFT_CHUNK_SIZE] for i in range(0, len(flat), DRAFT_CHUNK_SIZE)]
    for n, chunk in enumerate(chunks):
        out += ["=== DRAFT_POOL_%d ===" % n, "Name: Draft", "Class: Pkmn Trainer 1",
                "Pic: Cooltrainer M", "Party Size: 6", ""]
        for x in chunk:
            out.append("/* %s (%s) */" % (x["name"], x["source"]))
            out.append(party_text(x))
            out.append("")
    open(os.path.join(ROOT, "src", "data", "draft_pool.party"), "w").write("\n".join(out))

    # Opponents: every singles team, then every doubles team in the same order.
    tout = ["/* Generated by tools/daily/generate.py. Do not edit by hand. */", ""]
    for mode in MODES:
        report.append("Opponents, %s:" % mode)
        for o in opponents[mode]:
            tout += ["=== DAILY_TRAINER_%s%s ===" % (o["key"], "_DOUBLES" if mode == "doubles" else ""),
                     "Name: %s" % o["name"], "Class: %s" % o["class"], "Pic: %s" % o["pic"],
                     "Music: %s" % o["music"], "AI: %s" % AI]
            if mode == "doubles":
                tout.append("Battle Type: Doubles")
            tout.append("")
            for x in o["team"]:
                tout.append("/* %s (%s) */" % (x["name"], x["source"]))
                tout.append(party_text(x))
                tout.append("")
            report.append("%-12s %-10s %-9s %s" % (o["key"], o["class"][:10], o["kind"][:9],
                                                   ", ".join(x["name"] for x in o["team"])))
        report.append("")
    open(os.path.join(ROOT, "src", "data", "daily_trainers.party"), "w").write("\n".join(tout))

    singles = opponents["singles"]
    hdr = ["// Generated by tools/daily/generate.py. Do not edit by hand.",
           "#ifndef GUARD_CONSTANTS_DAILY_BATTLE_GENERATED_H",
           "#define GUARD_CONSTANTS_DAILY_BATTLE_GENERATED_H", "",
           "// Daily opponents. Each has a singles team at its id and a doubles team at",
           "// id + DAILY_TRAINERS_COUNT.",
           "enum DailyTrainerId", "{"]
    hdr += ["    DAILY_TRAINER_%s," % o["key"] for o in singles]
    hdr += ["    DAILY_TRAINERS_COUNT", "};", ""]
    bosses = [o for o in singles if o["kind"] == "boss"]
    hdr += ["// Sunday bosses are the last DAILY_BOSS_COUNT opponents; the other days draw from the rest.",
            "#define DAILY_BOSS_COUNT %d" % len(bosses),
            "#define DAILY_BOSS_FIRST (DAILY_TRAINERS_COUNT - DAILY_BOSS_COUNT)",
            "// Each boss's legendary, shown before the draft.",
            "#define DAILY_BOSS_LEGENDS { %s }" % ", ".join("SPECIES_" + R.species_(o["legend"]) for o in bosses), ""]
    hdr += ["#define DAILY_TRAINER_%s_DOUBLES (DAILY_TRAINER_%s + DAILY_TRAINERS_COUNT)" % (o["key"], o["key"])
            for o in singles]
    hdr += ["",
            "// The line shown under each opponent's name before the battle, indexed by DailyTrainerId.",
            "#define DAILY_TRAINER_THEMES { \\"]
    hdr += ["    COMPOUND_STRING(\"%s\"), \\" % o["theme"] for o in singles]
    hdr += ["}", "",
            "enum DraftPoolChunk", "{"] + ["    DRAFT_POOL_%d," % n for n in range(len(chunks))] + [
            "    DRAFT_POOL_CHUNKS", "};", "",
            "#define DRAFT_POOL_CHUNK_SIZE %d" % DRAFT_CHUNK_SIZE, ""]
    for mode in MODES:
        first, counts = tables[mode]
        up = mode.upper()
        hdr += ["// %s draft: Pokémon n's sets are pool entries FIRST[n] .. FIRST[n] + SETS[n] - 1." % mode.capitalize(),
                "#define DRAFT_%s_SPECIES_COUNT %d" % (up, len(first)),
                "#define DRAFT_%s_FIRST_SET { %s }" % (up, ", ".join(map(str, first))),
                "#define DRAFT_%s_SET_COUNT { %s }" % (up, ", ".join(map(str, counts))), ""]
    hdr += ["#endif // GUARD_CONSTANTS_DAILY_BATTLE_GENERATED_H", ""]
    open(os.path.join(ROOT, "include", "constants", "daily_battle_generated.h"), "w").write("\n".join(hdr))

    open(os.path.join(HERE, "data", "report.txt"), "w").write("\n".join(report))
    for kind, names in sorted(R.misses.items()):
        print("unresolved %s: %s" % (kind, ", ".join(sorted(names))), file=sys.stderr)
    print("draft: %s; opponents: %d per format" % (
        ", ".join("%s %d Pokémon / %d sets" % (m, len(pools[m]), sum(tables[m][1])) for m in MODES), len(singles)))


if __name__ == "__main__":
    main()
