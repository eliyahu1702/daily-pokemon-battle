# Daily Pokémon Battle

A Pokémon Emerald romhack built around a daily challenge: draft a team of six, then take on a different gym leader, Elite Four member or champion every day.

## How it plays

- **Start a new game** and you go straight to the Daily Battle Hall.
- **Draft six Pokémon.** Each round shows three options. Pick CHECK STATS to open the summary screen and see each one's moves, ability, held item and stats. Every option comes with a real competitive build taken from Smogon.
- **Fight today's opponent.** It's one of 98 leaders, Elite Four members and champions from Kanto to Paldea, each with a monotype team of competitive sets and the expansion's smartest battle AI.
- **Same challenge for everyone.** The date seeds both the draft and the opponent, so everybody gets the same fight on the same day. Opponents run through a shuffled order, so none repeats until all 98 have appeared.
- Lose and you can try again as often as you like that day. Win and your streak goes up.

Pokémon from every generation are in, with modern moves, abilities and items, thanks to [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion).

## Building

Follow the expansion's [install guide](INSTALL.md) for your OS, then run:

```console
make -j$(nproc)
```

This produces `pokeemerald.gba`. Play it in any GBA emulator with real-time clock support (mGBA is recommended). The daily rotation reads the emulator's clock.

## Where things live

- `src/daily_battle.c`: draft, daily seed and opponent selection.
- `data/maps/DailyBattleHall/`: the hall map and its script.
- `tools/daily/`: the generator that builds the draft pool and opponent teams from Smogon data, and a headless playtest driver. See [tools/daily/README.md](tools/daily/README.md).

The expansion's original README is in [docs/EXPANSION_README.md](docs/EXPANSION_README.md).
