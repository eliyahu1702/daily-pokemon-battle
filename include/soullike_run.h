#ifndef GUARD_SOULLIKE_RUN_H
#define GUARD_SOULLIKE_RUN_H

// Soullike runs: the roguelike loop two players play together.
//
// A run is 4 acts. Each act is a small branching map of 3 stops followed by
// a Gym Leader of a random type (no type twice in a run). The players take
// turns choosing the next stop: a wild draft, a trainer battle or a
// Pokémon Center. Wild drafts show a shared pool of Pokémon that the
// players pick from in snake order (A, B, B, A); the second picker gets a
// bonus item. Battles are co-op double battles (see soullike_coop.h).
// Fainted Pokémon are gone for good (Nuzlocke), survivors grow up to the
// act's level cap, and the run is lost when a battle is lost.
//
// Both games run the same run in lockstep. Everything both players see is
// rolled from the run seed (the relay's Daily Run seed), so the only thing
// sent over the link is each decision: whoever's turn it is picks, and the
// pick is applied in both games.

#include "constants/soullike_run.h"

struct ScriptContext;

void SoullikeRun_HandleMessage(u8 type, const u8 *data, u8 len);

// Script natives used by Debug_SoullikeRun.
void SoullikeRun_Begin(struct ScriptContext *ctx);
void SoullikeRun_Next(struct ScriptContext *ctx);
void SoullikeRun_PreparePick(struct ScriptContext *ctx);
void SoullikeRun_SendPick(struct ScriptContext *ctx);
void SoullikeRun_WaitPick(struct ScriptContext *ctx);
void SoullikeRun_ApplyPick(struct ScriptContext *ctx);
void SoullikeRun_AfterBattle(struct ScriptContext *ctx);

#endif // GUARD_SOULLIKE_RUN_H
