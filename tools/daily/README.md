# Daily Battle tools

## generate.py

Builds the draft pool and the daily opponents from Smogon's competitive sets.

```console
python3 tools/daily/generate.py
```

It writes three files, which are committed so the ROM builds without Python:

- `src/data/draft_pool.party`: every Pokémon you can be offered in the draft, one competitive set each.
- `src/data/daily_trainers.party`: one monotype team per gym leader, Elite Four member and champion in `leaders.py`.
- `include/constants/daily_battle_generated.h`: their ids and types.

`data/report.txt` lists the pool and each opponent's team for a quick read.

Rules it applies:

- Draft pool: fully evolved Pokémon in Smogon's OU through NU tiers (National Dex tier for Pokémon that aren't in Scarlet/Violet). No Ubers, restricted legendaries, mythicals, Megas or Z-crystals. One entry per species.
- Opponents: the leader's signature Pokémon first, then the strongest Pokémon of their type to fill six. Sets come from Smogon's Monotype analyses where they exist.
- Every species, move, item and ability is checked against the expansion's constants, so a name that doesn't exist in the game is dropped instead of breaking the build.

### Refreshing the Smogon data

`data/sets.json` is a trimmed copy of Smogon's sets from [pkmn/smogon](https://github.com/pkmn/smogon). To refresh it:

```console
git clone --depth 1 --filter=blob:none --sparse https://github.com/pkmn/smogon /tmp/smogon
git -C /tmp/smogon sparse-checkout set data/sets
python3 tools/daily/generate.py --refresh /tmp/smogon/data/sets
```

`data/dex.json` (types, tiers, formes) and `data/items.json` come from the [`@pkmn/dex`](https://www.npmjs.com/package/@pkmn/dex) package.

## playtest/

A headless mGBA driver that presses buttons from a script and saves screenshots.

```console
sudo apt install libmgba-dev libpng-dev python3-pil
make -C tools/daily/playtest
python3 tools/daily/playtest/full_run.py pokeemerald.gba /tmp/run win   # or lose
```

`full_run.py` plays a new game through the six draft rounds, today's battle and the results screen, and fails if the game hits an assertion screen. After a few turns it forces the outcome by writing `gBattleOutcome`, so both the win and the loss paths get covered.
