#ifndef GUARD_SOULLIKE_COOP_H
#define GUARD_SOULLIKE_COOP_H

// Soullike co-op battles: two players on two emulators fight one double
// battle together, each controlling one Pokémon on the player's side.
//
// Both games run the same battle in lockstep. Player 1's Pokémon is always
// battler 0 (left) and player 2's is always battler 2 (right), in both games,
// so the battle engine sees exactly the same battle on both sides. Each game
// drives its own battler with the normal player controls and drives the
// partner's battler with a "remote" controller that waits for the partner's
// choices to arrive over Soullike Link.
//
// The engine gets its own random number stream, seeded the same in both
// games and only advanced by the engine itself, so damage rolls, crits and
// AI choices come out identical. Animations and menus keep using the normal
// stream, so local differences there (like the battle scene option) don't
// matter. At the start of every turn both games swap a checksum of the
// battle state, and any mismatch is counted in the link mailbox.

struct ScriptContext;

// One opposing trainer in a co-op battle, for battles the run sets up.
struct SoullikeCoopOpponent
{
    u16 trainerId;  // picture, name and AI come from this trainer
    u8 count;
    u8 levels[3];
    u16 species[3];
};

bool32 SoullikeCoop_IsActive(void);
bool32 SoullikeCoop_IsRemoteBattler(enum BattlerId battler);
const u8 *SoullikeCoop_GetPlayerName(enum BattlerId battler);
u8 SoullikeCoop_GetPartnerGender(void);

// Queues a message for the partner. Messages go out in order, a few per frame.
void SoullikeCoop_Send(u8 type, const void *data, u8 len);

// Called once per frame from the main loop, after SoullikeLink_Update.
void SoullikeCoop_Update(void);

// Battle engine hooks.
void SoullikeCoop_SwapEngineRng(void);
bool32 SoullikeCoop_ControllerUsesEngineRng(enum BattlerId battler);
void SoullikeCoop_OnTurnStart(void);
void SoullikeCoop_OnEngineResponse(enum BattlerId battler, const u8 *data, u16 size);
bool32 SoullikeCoop_TryTakePartnerChoice(enum BattlerId battler);

// Remote controller for the partner's battler.
void SetControllerToSoullikeRemote(enum BattlerId battler);

// Replaces the opponents of the next co-op battle. Both games must pass the same teams.
void SoullikeCoop_SetOpponents(const struct SoullikeCoopOpponent *a, const struct SoullikeCoopOpponent *b);
// After a co-op battle: copies this player's Pokémon as they ended the battle and returns how many.
u32 SoullikeCoop_GetLocalBattleResult(struct Pokemon *mons);

// Script natives used by Debug_SoullikeCoopBattle.
void SoullikeCoop_BeginHandshake(struct ScriptContext *ctx);
void SoullikeCoop_StartBattle(struct ScriptContext *ctx);
void SoullikeCoop_BufferResult(struct ScriptContext *ctx);

#endif // GUARD_SOULLIKE_COOP_H
