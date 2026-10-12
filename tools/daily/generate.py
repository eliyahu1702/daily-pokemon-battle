#!/usr/bin/env python3
"""Generate the draft pool and daily opponent teams from Smogon set data.

Inputs (see tools/daily/README.md for how to refresh them):
  tools/daily/data/dex.json   species info exported from @pkmn/dex
  tools/daily/data/sets.json  trimmed Smogon sets (built by --refresh from a pkmn/smogon checkout)

Outputs (Showdown-format party files consumed by tools/trainerproc):
  src/data/draft_pool.party
  src/data/daily_trainers.party
  include/constants/daily_battle_generated.h
"""
import argparse
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from leaders import LEADERS  # noqa: E402

LEVEL = 100
DRAFT_CHUNK_SIZE = 200

# Smogon formats to take sets from, best first. The draft prefers standard singles
# tiers; opponents prefer monotype sets, which are written for exactly this situation.
DRAFT_FORMATS = ["ou", "uu", "ru", "nu", "pu", "zu", "nationaldex", "nationaldexuu",
                 "nationaldexru", "monotype", "nationaldexmonotype", "battlestadiumsingles", "1v1"]
OPPONENT_FORMATS = ["monotype", "nationaldexmonotype", "ou", "nationaldex", "uu", "ru",
                    "nationaldexuu", "nationaldexru", "nu", "pu", "zu", "battlestadiumsingles", "1v1"]
OLD_GENS = ["gen8", "gen7"]

BANNED_ITEMS = {"Eviolite", "Light Ball", "Thick Club", "Leek", "Stick"}
BANNED_MOVES = {"Hidden Power", "Return", "Frustration", "Baton Pass", "Last Respects",
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


def build_set(R, dex, species, raw):
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
        if it in BANNED_ITEMS or meta.get("mega") or meta.get("z"):
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


def pick_set(R, dex, sets_by_gen, species, formats):
    """The first set with four legal moves, in format preference order."""
    fallback = None
    for gen in ["gen9"] + OLD_GENS:
        entry = sets_by_gen.get(gen, {}).get(species)
        if not entry:
            continue
        fmts = [f for f in formats if f in entry]
        for fmt in fmts:
            for set_name, raw in entry[fmt].items():
                s = build_set(R, dex, species, raw)
                if s:
                    s["source"] = "%s%s: %s" % (gen, fmt, set_name)
                    if len(s["moves"]) == 4:
                        return s
                    fallback = fallback or s
    return fallback


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


def build_draft(R, dex, sets_by_gen):
    pool = []
    for name, info in sorted(dex.items(), key=lambda kv: (kv[1]["num"], kv[0])):
        if not eligible(info):
            continue
        s = pick_set(R, dex, sets_by_gen, name, DRAFT_FORMATS)
        if s:
            s["types"] = info["types"]
            pool.append(s)
    # One entry per species: forms that share a name (Silvally, Rotom stays split since its
    # forms have different names) would look like duplicates in the draft menu.
    seen, out = set(), []
    for s in pool:
        info = dex[s["name"]]
        key = info["base"] if not info["forme"] or info["base"] in ("Silvally", "Arceus", "Genesect", "Oricorio", "Urshifu", "Toxtricity", "Gourgeist", "Lycanroc") else s["species"]
        if key not in seen and s["species"] not in seen:
            seen.add(key)
            seen.add(s["species"])
            out.append(s)
    return out


def build_leader_team(R, dex, sets_by_gen, leader):
    key, name, title, typ, pic, signature = leader
    team, used_base = [], set()

    def try_add(species):
        info = dex.get(species)
        if not info or typ not in info["types"] or info["base"] in used_base:
            return
        if not eligible(info, deep=True):
            return
        s = pick_set(R, dex, sets_by_gen, species, OPPONENT_FORMATS)
        if s:
            team.append(s)
            used_base.add(info["base"])

    for sp in signature:
        if len(team) < 6:
            try_add(sp)
    # Fill with the strongest mons of the type, shuffled per leader so teams differ.
    candidates = [n for n, i in dex.items() if typ in i["types"]]
    candidates.sort(key=lambda n: (TIER_RANK.get(tier_of(dex[n]), 99),
                                   stable_hash(key + n)))
    for sp in candidates:
        if len(team) >= 6:
            break
        try_add(sp)
    return team


CLASS = {"Leader": "Leader", "Elite Four": "Elite Four", "Champion": "Champion"}
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
            keep = set(DRAFT_FORMATS) | set(OPPONENT_FORMATS)
            sets_by_gen[gen] = {sp: {f: v for f, v in fm.items() if f in keep} for sp, fm in full.items()}
            sets_by_gen[gen] = {sp: fm for sp, fm in sets_by_gen[gen].items() if fm}
        json.dump(sets_by_gen, open(sets_path, "w"), separators=(",", ":"), sort_keys=True)
    sets_by_gen = json.load(open(sets_path))

    R = Resolver()
    draft = build_draft(R, dex, sets_by_gen)

    # trainerproc caps a party at 255 mons, so the pool is split into chunks.
    out = ["/* Generated by tools/daily/generate.py from Smogon sets. Do not edit by hand. */", ""]
    chunks = [draft[i:i + DRAFT_CHUNK_SIZE] for i in range(0, len(draft), DRAFT_CHUNK_SIZE)]
    for n, chunk in enumerate(chunks):
        out += ["=== DRAFT_POOL_%d ===" % n, "Name: Draft", "Class: Pkmn Trainer 1",
                "Pic: Cooltrainer M", "Party Size: 6", ""]
        for s in chunk:
            out.append("/* %s (%s) */" % (s["name"], s["source"]))
            out.append(party_text(s))
            out.append("")
    open(os.path.join(ROOT, "src", "data", "draft_pool.party"), "w").write("\n".join(out))

    tout = ["/* Generated by tools/daily/generate.py from Smogon sets. Do not edit by hand. */", ""]
    report = []
    for leader in LEADERS:
        key, name, title, typ, pic, _ = leader
        team = build_leader_team(R, dex, sets_by_gen, leader)
        if len(team) < 6:
            print("warning: %s only has %d %s mons" % (key, len(team), typ), file=sys.stderr)
        tout += ["=== DAILY_TRAINER_%s ===" % key, "Name: %s" % name, "Class: %s" % CLASS[title],
                 "Pic: %s" % pic, "Music: %s" % ("Intense" if title == "Leader" else "Elite Four"),
                 "AI: %s" % AI, ""]
        for s in team:
            tout.append("/* %s (%s) */" % (s["name"], s["source"]))
            tout.append(party_text(s))
            tout.append("")
        report.append("%-10s %-10s %-8s %s" % (key, title, typ, ", ".join(s["name"] for s in team)))
    open(os.path.join(ROOT, "src", "data", "daily_trainers.party"), "w").write("\n".join(tout))

    hdr = ["// Generated by tools/daily/generate.py. Do not edit by hand.",
           "#ifndef GUARD_CONSTANTS_DAILY_BATTLE_GENERATED_H",
           "#define GUARD_CONSTANTS_DAILY_BATTLE_GENERATED_H", "",
           "enum DailyTrainerId", "{"]
    hdr += ["    DAILY_TRAINER_%s," % l[0] for l in LEADERS]
    hdr += ["    DAILY_TRAINERS_COUNT", "};", "",
            "// Each opponent's monotype, indexed by DailyTrainerId.",
            "#define DAILY_TRAINER_TYPES { \\"]
    hdr += ["    TYPE_%s, \\" % l[3].upper() for l in LEADERS]
    hdr += ["}", "",
            "enum DraftPoolChunk", "{"] + ["    DRAFT_POOL_%d," % n for n in range(len(chunks))] + [
            "    DRAFT_POOL_CHUNKS", "};", "",
            "#define DRAFT_POOL_SIZE %d" % len(draft),
            "#define DRAFT_POOL_CHUNK_SIZE %d" % DRAFT_CHUNK_SIZE, "",
            "#endif // GUARD_CONSTANTS_DAILY_BATTLE_GENERATED_H", ""]
    open(os.path.join(ROOT, "include", "constants", "daily_battle_generated.h"), "w").write("\n".join(hdr))

    open(os.path.join(HERE, "data", "report.txt"), "w").write(
        "Draft pool: %d mons\n%s\n\nOpponents:\n%s\n" % (
            len(draft), ", ".join(s["name"] for s in draft), "\n".join(report)))
    for kind, names in sorted(R.misses.items()):
        print("unresolved %s: %s" % (kind, ", ".join(sorted(names))), file=sys.stderr)
    print("draft pool: %d, opponents: %d" % (len(draft), len(LEADERS)))


if __name__ == "__main__":
    main()
