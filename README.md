# Daily Pokémon Battle

A Pokémon Emerald romhack built around a daily challenge: draft a team of six, then take on a different gym leader, Elite Four member or champion every day, with one shot to win.

## How it plays

- **Start a new game** and you go straight to the Daily Battle Hall.
- **Singles or doubles.** About one day in three is a double battle. The hall tells you which before the draft starts.
- **Draft six Pokémon** on a Battle Factory style screen. Each round shows three Pokémon side by side with their held item and ability; move the cursor to compare them, open SUMMARY for moves and stats, and PICK one. Every option comes with a real competitive build (moves, item, ability, nature, EVs). Each Pokémon has three or four builds and the day decides which one shows up. On doubles days the builds are doubles builds: Protect, spread moves, Fake Out, redirection and doubles items.
- **Choose your leads.** Before the battle you pick the Pokémon that lead off (two on doubles days); the rest follow in draft order.
- **Fight today's opponent.** There are 190 of them, all running competitive sets with the expansion's smartest battle AI:
  - 92 gym leaders and Elite Four members from Kanto to Paldea, each with a monotype team.
  - Champions (Red, Blue, Lance, Steven, Wallace, Cynthia, Alder, Iris, Diantha, Kukui, Leon, Geeta, Nemona, Kieran), villains (Giovanni, Archie, Maxie, Cyrus, Ghetsis, N, Lysandre, Guzma, Lusamine, Rose, Volo), rivals (Silver, Wally, Hop, Arven, Penny) and the Frontier Brains, each with their signature team plus a variation.
  - Battle Tower aces running real competitive archetypes: rain, sun, sand, snow, hyper offense, stall, balance, Trick Room, VoltTurn, setup, and classic DPP and BW sand teams.
- **Sunday boss.** Every Sunday a villain or champion brings their legendary: Maxie with Groudon, Archie with Kyogre, Cyrus with Dialga, Ghetsis with Black Kyurem, N with Reshiram, Lysandre with Yveltal, Lusamine with Dusk Mane Necrozma, Rose with Eternatus, Volo with Giratina, Giovanni with Mewtwo, Steven with Rayquaza and Red with Ho-Oh. The hall shows the legendary before the draft.
- **Same challenge for everyone.** The date seeds both the draft and the opponent, so everybody gets the same fight on the same day. Opponents run through a shuffled order, so none repeats until all of them have appeared.
- **One shot a day.** The daily is a single scored attempt. Draft picks are final, and the game saves before the battle, so resetting mid-battle counts as a loss.
- **See your score.** After the battle a results screen shows your team with their held items, how each one finished, and a score. A win is worth 1000, plus 150 for every Pokémon still standing and 1 per percent of HP left across the team, minus 15 per turn. A loss scores 75 per opposing Pokémon knocked out. The host can show the results screen again for the rest of the day, so it's easy to screenshot and share.
- **Records and badges.** The host's RECORDS screen shows games played, wins, win rate, best score, current and best win streak and bosses beaten, a history of your last 30 dailies, and 16 badges to earn (Flawless, Speedster, Boss Slayer, a 30-day streak and more).
- **Practice.** Once today's attempt is used, practice battles are open and don't count for your records: today's opponent again with a fresh draft, a random opponent, or any of the last seven days' dailies with that day's draft.

Pokémon from every generation are in, with modern moves, abilities and items, thanks to [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion).

## Playing

- **On an emulator:** download `daily-pokemon-battle.bps` from https://eliyahu1702.github.io/daily-pokemon-battle/ or the [latest release](https://github.com/eliyahu1702/daily-pokemon-battle/releases/latest) and apply it to your own Emerald file with [Rom Patcher JS](https://www.marcrobledo.com/RomPatcher.js/) or [Flips](https://github.com/Alcaro/Flips). Use an emulator with real-time clock support: mGBA on PC, Pizza Boy GBA or My Boy! on Android, Delta on iPhone.
- **No emulator?** The same page can patch your Emerald file and run it in your browser, on PC or phone. The file stays on your device.

The base file must be Pokémon Emerald (USA/Europe), SHA-1 `f3ae088181bf583e55daf962a92bb46f4f1d07b7`. No game files are distributed here.

## Releasing

1. Build the ROM, then make and check the patch: `FLIPS=path/to/flips tools/release/make_patch.sh emerald.gba daily-pokemon-battle.bps`.
2. Build the site: `tools/release/build_site.sh daily-pokemon-battle.bps site-out`, then push `site-out` to the `gh-pages` branch.
3. Attach the patch to a new GitHub release.

## Building

Follow the expansion's [install guide](INSTALL.md) for your OS, then run:

```console
make -j$(nproc)
```

This produces `pokeemerald.gba`. Play it in any GBA emulator with real-time clock support (mGBA is recommended). The daily rotation reads the emulator's clock.

## Where things live

- `src/daily_battle.c`: draft, daily seed and opponent selection.
- `src/battle_factory_screen.c`: the draft picker (a daily mode of the Battle Factory rental screen).
- `src/daily_results_screen.c`: the results screen and score.
- `src/daily_records.c`: the records (stats, streaks, history and badges, saved in SaveBlock3) and the records screen.
- `data/maps/DailyBattleHall/`: the hall map and its script.
- `site/`: the play-in-browser page (patching in `bps.js`, EmulatorJS setup in `app.js`); `tools/release/` builds it.
- `tools/daily/`: the generator that builds the draft pool and opponent teams from Smogon and Showdown data, and a headless playtest driver. See [tools/daily/README.md](tools/daily/README.md).

The expansion's original README is in [docs/EXPANSION_README.md](docs/EXPANSION_README.md).
