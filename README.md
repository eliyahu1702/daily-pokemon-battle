# Pokémon Soullike

A two-player co-op Pokémon roguelike, built as a romhack you and a friend play online on two emulators.

## The idea

- **Co-op with a little rivalry.** Every battle is a double battle where each of you controls one Pokémon.
- **About 40 minutes per run:** 4 gyms of random, never-repeating types, then the Champion.
- **Shared draft.** Wild Pokémon come from a shared pool you take turns picking from, and you take turns choosing the route.
- **Nuzlocke rules.** Fainted Pokémon are gone. The only Revive in the run comes from a shrine event, and it's shared.
- **Daily Run.** The same seed for everyone each day, plus random runs, with your own score history.

The full design is in the project's design doc.

## Status

Early. What works so far:

- The base game: [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion) 1.17.1, which has Pokémon from every generation and modern battle mechanics.
- **Soullike Link**, the connection between two emulators: two mGBAs join a room on a small relay server, get the same Daily Run seed, and pass messages between the two games. See [tools/soullike_link/README.md](tools/soullike_link/README.md).

Next up: co-op double battles where the ally slot is your friend, then the run map, draft and events.

## Building

See [INSTALL.md](INSTALL.md) for the toolchain. On Ubuntu:

```sh
sudo apt-get install gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi libpng-dev python3
make -j$(nproc)
```

This produces `pokeemerald.gba`. The original pokeemerald-expansion README is in [docs/soullike/EXPANSION_README.md](docs/soullike/EXPANSION_README.md).
