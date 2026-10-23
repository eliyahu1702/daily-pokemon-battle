# Daily Battle tools

## generate.py

Builds the draft pool and the daily opponents from Smogon's competitive sets, in a singles and a doubles version.

```console
python3 tools/daily/generate.py
```

It writes three files, which are committed so the ROM builds without Python:

- `src/data/draft_pool.party`: every set you can be offered in the draft: the singles pool, then the doubles pool, with three or four sets per Pokémon.
- `src/data/daily_trainers.party`: every daily opponent: a monotype team per gym leader and Elite Four member in `leaders.py`, the signature teams (with variations) and competitive archetypes in `opponents.py`, and last the Sunday bosses (`BOSSES` in `opponents.py`), each leading their legendary. Each opponent appears twice, once with singles sets and once (`_DOUBLES`, `Battle Type: Doubles`) with doubles sets.
- `include/constants/daily_battle_generated.h`: their ids and types, and where each Pokémon's sets start in the pool.

`data/report.txt` lists the pool and each opponent's team for a quick read.

Rules it applies:

- Draft pool: fully evolved Pokémon in Smogon's OU through NU tiers (National Dex tier for Pokémon that aren't in Scarlet/Violet). No Ubers, restricted legendaries, mythicals, Megas or Z-crystals. The doubles pool also takes Pokémon with a Smogon doubles or VGC set.
- Sets per Pokémon: Smogon's sets first, then sets built from Showdown's random battle roles ([pkmn/randbats](https://github.com/pkmn/randbats), in `setsource.py`), which come with a movepool, items and abilities; EVs and nature are picked from the Pokémon's stats and moves. Doubles uses Smogon's doubles and VGC sets, then random doubles roles, and only as a last resort a singles set with its singles-only move (hazards, Toxic, Roar) swapped for Protect.
- Opponents: the leader's signature Pokémon first, then the strongest Pokémon of their type to fill six. Sets come from Smogon's Monotype analyses where they exist.
- Every species, move, item and ability is checked against the expansion's constants, so a name that doesn't exist in the game is dropped instead of breaking the build.
- Items must fit the moves: no Choice item next to Protect or another status move (Trick and Healing Wish are fine), no Assault Vest with status moves, no Light Clay without screens. A misfit item becomes Life Orb on attackers and Leftovers (Sitrus Berry in doubles) on the rest. `check_sets.py` checks the committed sets for this in CI.
- Abilities are checked against the species' own ability list in the game. An ability that belongs to another form (Greninja's Battle Bond) switches the Pokémon to that form. Trainer pictures must have a front sprite.

### Refreshing the Smogon data

`data/sets.json` is a trimmed copy of Smogon's sets from [pkmn/smogon](https://github.com/pkmn/smogon). To refresh it:

```console
git clone --depth 1 --filter=blob:none --sparse https://github.com/pkmn/smogon /tmp/smogon
git -C /tmp/smogon sparse-checkout set data/sets
python3 tools/daily/generate.py --refresh /tmp/smogon/data/sets
```

`data/randbats_singles.json` and `data/randbats_doubles.json` are copies of the gen 9 random battle and random doubles data from pkmn/randbats. `data/dex.json` (types, tiers, formes) and `data/items.json` come from the [`@pkmn/dex`](https://www.npmjs.com/package/@pkmn/dex) package.

## playtest/

A headless mGBA driver that presses buttons from a script and saves screenshots.

```console
sudo apt install libmgba-dev libpng-dev python3-pil
make -C tools/daily/playtest
python3 tools/daily/playtest/full_run.py pokeemerald.gba /tmp/run win doubles   # or lose; singles, sunday
```

`full_run.py` moves the clock to the next singles, doubles or Sunday boss day and plays a new game through the six draft rounds, the lead pick, the battle and the results screen, then shows the results again, pages through the records and plays a practice battle (today's opponent after a win, yesterday's daily after a loss). It fails if the game hits an assertion screen or a screen never comes up. After a few turns it forces the outcome by writing `gBattleOutcome`, so both the win and the loss paths get covered. Scripts wait for screens with the `until KEY ADDR VALUE` command, which presses KEY until `gMain.callback2` is that screen's callback (looked up in the `.map` file, or with `arm-none-eabi-nm` for static ones).

`forfeit.py pokeemerald.gba /tmp/forfeit` starts today's battle, resets the game during it and checks that the hall then counts it as a loss.

`check_all.py pokeemerald.gba /tmp/selftest` makes the game build every opponent's team, in singles and doubles, and every draft set, in a few seconds. A set the game would reject on its day (an ability the species can't have, a trainer picture with no front sprite) stops it on an assertion screen and fails the check.

`day_rollover.py pokeemerald.gba /tmp/days 7` starts a new game on each of the next seven days (using the `clockdays` script command to move the cartridge clock) and fails if two days share an opponent or draft, or if the same day comes out differently twice.
