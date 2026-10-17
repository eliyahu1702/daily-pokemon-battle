#include "global.h"
#include "battle.h"
#include "battle_controllers.h"
#include "battle_setup.h"
#include "event_data.h"
#include "load_save.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "pokemon.h"
#include "random.h"
#include "script.h"
#include "soullike_coop.h"
#include "soullike_link.h"
#include "soullike_run.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "title_screen.h"
#include "constants/battle.h"
#include "constants/battle_partner.h"
#include "constants/opponents.h"
#include "constants/species.h"

// Each player brings up to three Pokémon, like Emerald's tag battles.
#define COOP_TEAM_SIZE MULTI_PARTY_SIZE
#define COOP_CHOICE_QUEUE_SIZE 8
#define COOP_OUTBOX_SIZE 8
#define COOP_HASH_SLOTS 4

// Payload layouts (byte 0 of every co-op message is the battle number, so a
// message left over from an earlier battle is never mistaken for a new one).
#define TEAM_CHUNK_HEADER 2   // battle number, chunk index
#define TEAM_CHUNK_DATA (SOULLIKE_LINK_PAYLOAD_MAX - TEAM_CHUNK_HEADER)
#define CHOICE_HEADER 7       // battle number, command, size, item party index, item move index, selected mon, switch-in mon
#define CHOICE_DATA_MAX 16

struct CoopTeam
{
    u8 name[PLAYER_NAME_LENGTH + 1];
    u8 gender;
    u8 count;
    u8 padding[2];
    struct Pokemon mons[COOP_TEAM_SIZE];
};

#define TEAM_CHUNKS ((sizeof(struct CoopTeam) + TEAM_CHUNK_DATA - 1) / TEAM_CHUNK_DATA)

// The partner's team for one battle. There are two slots because the
// partner can start the handshake for the next battle before this game has
// finished showing the end of the current one.
struct CoopPartnerSlot
{
    struct CoopTeam team;
    u32 chunksReceived;
    u32 seed;
    bool8 ready;
};

struct CoopChoice
{
    u8 cmd;
    u8 size;
    u8 itemPartyIndex;
    u8 itemMoveIndex;
    u8 selectedMonPartyId;
    u8 monToSwitchIntoId;
    u8 data[CHOICE_DATA_MAX];
};

struct CoopOutMessage
{
    u8 type;
    u8 len;
    u8 data[SOULLIKE_LINK_PAYLOAD_MAX];
};

struct CoopState
{
    u8 battleNo;          // number of the current (or next) co-op battle; both games keep it in step
    bool8 active;
    bool8 handshaking;
    u8 nextChunkToSend;
    bool8 readySent;
    u32 seed;
    struct CoopTeam localTeam;
    struct CoopPartnerSlot partner[2];
    struct CoopTeam *p1Team;
    struct CoopTeam *p2Team;
    rng_value_t engineRng;
    bool8 engineRngInUse;
    struct CoopChoice choices[COOP_CHOICE_QUEUE_SIZE];
    u8 choiceHead;
    u8 choiceCount;
    struct CoopOutMessage outbox[COOP_OUTBOX_SIZE];
    u8 outHead;
    u8 outCount;
    u16 localTurn[COOP_HASH_SLOTS];
    u32 localHash[COOP_HASH_SLOTS];
    bool8 localHashValid[COOP_HASH_SLOTS];
    u16 remoteTurn[COOP_HASH_SLOTS];
    u32 remoteHash[COOP_HASH_SLOTS];
    bool8 remoteHashValid[COOP_HASH_SLOTS];
    u16 turn;
    bool8 customOpponents;
    struct SoullikeCoopOpponent opponents[2];
    u8 resultCount;
    struct Pokemon result[COOP_TEAM_SIZE];
};

static EWRAM_DATA struct CoopState sCoop = {0};

extern const u8 Debug_SoullikeCoopBattle[];
extern const u8 Debug_SoullikeRun[];

static const u8 sTestNames[2][PLAYER_NAME_LENGTH + 1] = {
    _("ONE"),
    _("TWO"),
};

// Used when a player has no Pokémon yet, so a fresh save can still try a co-op battle.
static const u16 sDemoTeams[2][COOP_TEAM_SIZE] = {
    {SPECIES_COMBUSKEN, SPECIES_LOMBRE, SPECIES_ELECTRIKE},
    {SPECIES_MARSHTOMP, SPECIES_GROVYLE, SPECIES_KIRLIA},
};
#define DEMO_LEVEL 45

static u8 LocalPlayerId(void)
{
    return gSoullikeLink.playerId == 0 ? 0 : 1;
}

static enum BattlerId LocalBattler(void)
{
    return LocalPlayerId() == 0 ? B_BATTLER_0 : B_BATTLER_2;
}

bool32 SoullikeCoop_IsActive(void)
{
    return sCoop.active;
}

bool32 SoullikeCoop_IsRemoteBattler(enum BattlerId battler)
{
    return sCoop.active && (battler == B_BATTLER_0 || battler == B_BATTLER_2) && battler != LocalBattler();
}

const u8 *SoullikeCoop_GetPlayerName(enum BattlerId battler)
{
    return battler == B_BATTLER_0 ? sCoop.p1Team->name : sCoop.p2Team->name;
}

u8 SoullikeCoop_GetPartnerGender(void)
{
    return LocalPlayerId() == 0 ? sCoop.p2Team->gender : sCoop.p1Team->gender;
}

static void QueueSend(u8 type, const void *data, u8 len)
{
    struct CoopOutMessage *msg;

    if (sCoop.outCount >= COOP_OUTBOX_SIZE)
    {
        gSoullikeLink.coopDesyncs++;
        return;
    }
    msg = &sCoop.outbox[(sCoop.outHead + sCoop.outCount) % COOP_OUTBOX_SIZE];
    msg->type = type;
    msg->len = len;
    memcpy(msg->data, data, len);
    sCoop.outCount++;
}

void SoullikeCoop_Send(u8 type, const void *data, u8 len)
{
    QueueSend(type, data, len);
}

static void FlushOutbox(void)
{
    while (sCoop.outCount != 0)
    {
        struct CoopOutMessage *msg = &sCoop.outbox[sCoop.outHead];
        if (!SoullikeLink_Send(msg->type, msg->data, msg->len))
            return;
        sCoop.outHead = (sCoop.outHead + 1) % COOP_OUTBOX_SIZE;
        sCoop.outCount--;
    }
}

// ---------------------------------------------------------------------------
// Handshake: swap teams and agree on a seed

static void BuildLocalTeam(void)
{
    struct CoopTeam *team = &sCoop.localTeam;
    u32 i;

    memset(team, 0, sizeof(*team));
    StringCopyN(team->name, gSaveBlock2Ptr->playerName, PLAYER_NAME_LENGTH + 1);
    team->gender = gSaveBlock2Ptr->playerGender;

    for (i = 0; i < PARTY_SIZE && team->count < COOP_TEAM_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        if (IsValidForBattle(mon))
            team->mons[team->count++] = *mon;
    }

    if (team->count == 0)
    {
        for (i = 0; i < COOP_TEAM_SIZE; i++)
        {
            CreateMonWithIVs(&team->mons[i], sDemoTeams[LocalPlayerId()][i], DEMO_LEVEL, 0x1000 * (i + 1) + LocalPlayerId(), OTID_STRUCT_PLAYER_ID, 20);
            GiveMonInitialMoveset(&team->mons[i]);
        }
        team->count = COOP_TEAM_SIZE;
    }
}

static struct CoopPartnerSlot *PartnerSlot(u8 battleNo)
{
    return &sCoop.partner[battleNo & 1];
}

static bool32 SendHandshake(void)
{
    u8 payload[SOULLIKE_LINK_PAYLOAD_MAX];

    while (sCoop.nextChunkToSend < TEAM_CHUNKS && sCoop.outCount < COOP_OUTBOX_SIZE)
    {
        u32 offset = sCoop.nextChunkToSend * TEAM_CHUNK_DATA;
        u32 len = min(TEAM_CHUNK_DATA, sizeof(struct CoopTeam) - offset);
        payload[0] = sCoop.battleNo;
        payload[1] = sCoop.nextChunkToSend;
        memcpy(&payload[TEAM_CHUNK_HEADER], (u8 *)&sCoop.localTeam + offset, len);
        QueueSend(SOULLIKE_MSG_COOP_TEAM, payload, TEAM_CHUNK_HEADER + len);
        sCoop.nextChunkToSend++;
    }
    if (sCoop.nextChunkToSend < TEAM_CHUNKS)
        return FALSE;

    if (!sCoop.readySent && sCoop.outCount < COOP_OUTBOX_SIZE)
    {
        payload[0] = sCoop.battleNo;
        memcpy(&payload[1], &sCoop.seed, sizeof(u32));
        QueueSend(SOULLIKE_MSG_COOP_READY, payload, 1 + sizeof(u32));
        sCoop.readySent = TRUE;
    }
    return sCoop.readySent;
}

static void Task_CoopHandshake(u8 taskId)
{
    struct CoopPartnerSlot *partner = PartnerSlot(sCoop.battleNo);

    if (!SoullikeLink_IsConnected())
    {
        // Lost the partner. Give up; the script tells the player.
        sCoop.handshaking = FALSE;
        gSoullikeLink.coopState = SOULLIKE_COOP_IDLE;
        gSpecialVar_Result = FALSE;
        ScriptContext_Enable();
        DestroyTask(taskId);
        return;
    }

    if (SendHandshake() && partner->ready)
    {
        sCoop.handshaking = FALSE;
        gSpecialVar_Result = TRUE;
        ScriptContext_Enable();
        DestroyTask(taskId);
    }
}

void SoullikeCoop_BeginHandshake(struct ScriptContext *ctx)
{
    if (!SoullikeLink_IsConnected())
    {
        gSpecialVar_Result = FALSE;
        ScriptContext_Enable();
        return;
    }

    BuildLocalTeam();
    sCoop.handshaking = TRUE;
    sCoop.nextChunkToSend = 0;
    sCoop.readySent = FALSE;
    // Player 1 picks the seed; player 2's READY carries a seed that is ignored.
    sCoop.seed = Random32() ^ gMain.vblankCounter1;
    gSoullikeLink.coopState = SOULLIKE_COOP_HANDSHAKE;
    CreateTask(Task_CoopHandshake, 80);
}

static void HandleTeamChunk(const u8 *data, u8 len)
{
    struct CoopPartnerSlot *partner;
    u32 offset;
    u8 battleNo = data[0], chunk = data[1];

    if (len < TEAM_CHUNK_HEADER || (u8)(battleNo - sCoop.battleNo) > 1 || chunk >= TEAM_CHUNKS)
        return;

    partner = PartnerSlot(battleNo);
    if (chunk == 0)
    {
        // First chunk of a new handshake: forget whatever this slot held before.
        partner->chunksReceived = 0;
        partner->ready = FALSE;
    }
    offset = chunk * TEAM_CHUNK_DATA;
    memcpy((u8 *)&partner->team + offset, &data[TEAM_CHUNK_HEADER], min(len - TEAM_CHUNK_HEADER, sizeof(struct CoopTeam) - offset));
    partner->chunksReceived |= 1u << chunk;
}

static void HandleReady(const u8 *data, u8 len)
{
    struct CoopPartnerSlot *partner;
    u8 battleNo = data[0];

    if (len < 1 + sizeof(u32) || (u8)(battleNo - sCoop.battleNo) > 1)
        return;

    partner = PartnerSlot(battleNo);
    if (partner->chunksReceived != (1u << TEAM_CHUNKS) - 1)
        return; // a chunk went missing; never start with half a team
    memcpy(&partner->seed, &data[1], sizeof(u32));
    partner->ready = TRUE;
}

// ---------------------------------------------------------------------------
// Starting and ending the battle

static void CB2_EndCoopBattle(void)
{
    struct CoopPartnerSlot *partner = PartnerSlot(sCoop.battleNo);

    sCoop.active = FALSE;
    gSoullikeLink.coopState = SOULLIKE_COOP_FINISHED;
    gSoullikeLink.coopOutcome = gBattleOutcome;

    // Keep this player's side as it ended the battle, then put the party
    // back. A run applies the result to the party itself.
    sCoop.resultCount = LocalPlayerId() == 0 ? sCoop.p1Team->count : sCoop.p2Team->count;
    memcpy(sCoop.result, gParties[LocalPlayerId() == 0 ? B_TRAINER_PLAYER : B_TRAINER_PARTNER], sizeof(sCoop.result));
    LoadPlayerParty();
    sCoop.customOpponents = FALSE;

    partner->ready = FALSE;
    partner->chunksReceived = 0;
    sCoop.battleNo++;
    sCoop.choiceCount = 0;
    gSpecialVar_Result = gBattleOutcome;
    SetMainCallback2(CB2_ReturnToFieldContinueScriptPlayMapMusic);
}

void SoullikeCoop_SetOpponents(const struct SoullikeCoopOpponent *a, const struct SoullikeCoopOpponent *b)
{
    sCoop.opponents[0] = *a;
    sCoop.opponents[1] = *b;
    sCoop.customOpponents = TRUE;
}

u32 SoullikeCoop_GetLocalBattleResult(struct Pokemon *mons)
{
    memcpy(mons, sCoop.result, sizeof(struct Pokemon) * sCoop.resultCount);
    return sCoop.resultCount;
}

// Runs right after the battle seed is set, so both games build the same Pokémon.
static void CreateCustomOpponents(void)
{
    u32 side, i;

    for (side = 0; side < 2; side++)
    {
        struct Pokemon *party = gParties[side == 0 ? B_TRAINER_OPPONENT_A : B_TRAINER_OPPONENT_B];
        const struct SoullikeCoopOpponent *opponent = &sCoop.opponents[side];

        for (i = 0; i < PARTY_SIZE; i++)
            ZeroMonData(&party[i]);
        for (i = 0; i < opponent->count && i < ARRAY_COUNT(opponent->species); i++)
        {
            CreateMonWithIVs(&party[i], opponent->species[i], opponent->levels[i], Random32(), OTID_STRUCT_RANDOM_NO_SHINY, 10 + 5 * i);
            GiveMonInitialMoveset(&party[i]);
        }
    }
}

void SoullikeCoop_StartBattle(struct ScriptContext *ctx)
{
    struct CoopPartnerSlot *partner = PartnerSlot(sCoop.battleNo);
    u32 i;

    sCoop.p1Team = LocalPlayerId() == 0 ? &sCoop.localTeam : &partner->team;
    sCoop.p2Team = LocalPlayerId() == 0 ? &partner->team : &sCoop.localTeam;
    if (LocalPlayerId() == 1)
        sCoop.seed = partner->seed;

    // Both games build exactly the same parties: player 1's team on the
    // left (battler 0), player 2's team on the right (battler 2).
    SavePlayerParty();
    for (i = 0; i < PARTY_SIZE; i++)
    {
        ZeroMonData(&gParties[B_TRAINER_PLAYER][i]);
        ZeroMonData(&gParties[B_TRAINER_PARTNER][i]);
    }
    for (i = 0; i < sCoop.p1Team->count; i++)
        gParties[B_TRAINER_PLAYER][i] = sCoop.p1Team->mons[i];
    for (i = 0; i < sCoop.p2Team->count; i++)
        gParties[B_TRAINER_PARTNER][i] = sCoop.p2Team->mons[i];
    gPartiesCount[B_TRAINER_PLAYER] = sCoop.p1Team->count;
    gPartiesCount[B_TRAINER_PARTNER] = sCoop.p2Team->count;

    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_DOUBLE | BATTLE_TYPE_MULTI
                     | BATTLE_TYPE_INGAME_PARTNER | BATTLE_TYPE_TWO_OPPONENTS;
    gPartnerTrainerId = TRAINER_PARTNER(PARTNER_STEVEN);
    for (i = 0; i < MAX_FRONTIER_PARTY_SIZE; i++)
    {
        gSelectedOrderFromParty[i] = i + 1;
        gSaveBlock2Ptr->frontier.selectedPartyMons[i] = i + 1;
    }

    memset(&gTrainerBattleParameter, 0, sizeof(gTrainerBattleParameter));
    TRAINER_BATTLE_PARAM.opponentA = sCoop.customOpponents ? sCoop.opponents[0].trainerId : TRAINER_GRUNT_WEATHER_INST_1;
    TRAINER_BATTLE_PARAM.opponentB = sCoop.customOpponents ? sCoop.opponents[1].trainerId : TRAINER_GRUNT_WEATHER_INST_3;

    // Same seed, same opponents, same engine random stream in both games.
    SeedRng(sCoop.seed);
    CreateNPCTrainerPartyFromTrainer(gParties[B_TRAINER_OPPONENT_A], GetTrainerStructFromId(TRAINER_BATTLE_PARAM.opponentA));
    CreateNPCTrainerPartyFromTrainer(gParties[B_TRAINER_OPPONENT_B], GetTrainerStructFromId(TRAINER_BATTLE_PARAM.opponentB));
    if (sCoop.customOpponents)
        CreateCustomOpponents();
    sCoop.engineRng = LocalRandomSeed(sCoop.seed ^ 0x434F4F50);
    sCoop.engineRngInUse = FALSE;
    gBattleEnvironment = BATTLE_ENVIRONMENT_PLAIN;
    CalculateEnemyPartyCount();

    sCoop.active = TRUE;
    sCoop.choiceHead = 0;
    sCoop.choiceCount = 0;
    sCoop.turn = 0;
    memset(sCoop.localHashValid, 0, sizeof(sCoop.localHashValid));
    memset(sCoop.remoteHashValid, 0, sizeof(sCoop.remoteHashValid));
    gSoullikeLink.coopState = SOULLIKE_COOP_IN_BATTLE;
    gSoullikeLink.coopTurn = 0;
    gSoullikeLink.coopBattles++;

    BattleSetup_StartTrainerBattle_Debug();
    gMain.savedCallback = CB2_EndCoopBattle;
}

void SoullikeCoop_BufferResult(struct ScriptContext *ctx)
{
    if (gSoullikeLink.coopOutcome == B_OUTCOME_WON)
        StringCopy(gStringVar1, COMPOUND_STRING("You won together!"));
    else if (gSoullikeLink.coopOutcome == B_OUTCOME_LOST)
        StringCopy(gStringVar1, COMPOUND_STRING("You both blacked out."));
    else
        StringCopy(gStringVar1, COMPOUND_STRING("The battle ended."));

    if (gSoullikeLink.coopDesyncs == 0)
        StringCopy(gStringVar2, COMPOUND_STRING("in sync"));
    else
        ConvertIntToDecimalStringN(gStringVar2, gSoullikeLink.coopDesyncs, STR_CONV_MODE_LEFT_ALIGN, 5);
}

// ---------------------------------------------------------------------------
// In battle: random numbers, choices and checksums

void SoullikeCoop_SwapEngineRng(void)
{
    rng_value_t tmp = gRngValue;
    gRngValue = sCoop.engineRng;
    sCoop.engineRng = tmp;
    sCoop.engineRngInUse = !sCoop.engineRngInUse;
    // The stream's counter goes up by one per number drawn.
    if (!sCoop.engineRngInUse)
        gSoullikeLink.coopRngCalls = sCoop.engineRng.ctr;
}

static u32 HashBytes(u32 hash, const void *data, u32 size)
{
    const u8 *bytes = data;
    u32 i;

    for (i = 0; i < size; i++)
        hash = (hash ^ bytes[i]) * 16777619;
    return hash;
}

#define HASH_FIELD(hash, field) HashBytes(hash, &(field), sizeof(field))

static u32 HashBattleState(void)
{
    u32 hash = 2166136261;
    u32 i;

    for (i = 0; i < gBattlersCount; i++)
    {
        struct BattlePokemon *mon = &gBattleMons[i];
        hash = HASH_FIELD(hash, mon->species);
        hash = HASH_FIELD(hash, mon->hp);
        hash = HASH_FIELD(hash, mon->maxHP);
        hash = HASH_FIELD(hash, mon->status1);
        hash = HASH_FIELD(hash, mon->statStages);
        hash = HASH_FIELD(hash, mon->moves);
        hash = HASH_FIELD(hash, mon->pp);
        hash = HASH_FIELD(hash, mon->ability);
        hash = HASH_FIELD(hash, mon->item);
        hash = HASH_FIELD(hash, mon->level);
        hash = HASH_FIELD(hash, gBattlerPartyIndexes[i]);
    }
    hash = HASH_FIELD(hash, gBattleWeather);
    hash = HASH_FIELD(hash, gAbsentBattlerFlags);
    // Called with the engine stream swapped in, so this is the engine's state.
    hash = HASH_FIELD(hash, gRngValue);
    return hash;
}

static void CompareHashes(void)
{
    u32 i, j;

    for (i = 0; i < COOP_HASH_SLOTS; i++)
    {
        if (!sCoop.localHashValid[i])
            continue;
        for (j = 0; j < COOP_HASH_SLOTS; j++)
        {
            if (sCoop.remoteHashValid[j] && sCoop.remoteTurn[j] == sCoop.localTurn[i])
            {
                if (sCoop.remoteHash[j] == sCoop.localHash[i])
                    gSoullikeLink.coopHashesMatched++;
                else
                    gSoullikeLink.coopDesyncs++;
                sCoop.localHashValid[i] = FALSE;
                sCoop.remoteHashValid[j] = FALSE;
                break;
            }
        }
    }
}

// Called by the battle engine each time a turn's move selection begins.
void SoullikeCoop_OnTurnStart(void)
{
    u8 payload[1 + sizeof(u16) + sizeof(u32)];
    u32 hash = HashBattleState();
    u32 slot = sCoop.turn % COOP_HASH_SLOTS;

    sCoop.localTurn[slot] = sCoop.turn;
    sCoop.localHash[slot] = hash;
    sCoop.localHashValid[slot] = TRUE;
    gSoullikeLink.coopTurn = sCoop.turn;
    gSoullikeLink.coopHash = hash;

    payload[0] = sCoop.battleNo;
    memcpy(&payload[1], &sCoop.turn, sizeof(u16));
    memcpy(&payload[3], &hash, sizeof(u32));
    QueueSend(SOULLIKE_MSG_COOP_HASH, payload, sizeof(payload));
    sCoop.turn++;
    CompareHashes();
}

static void HandleHash(const u8 *data, u8 len)
{
    u16 turn;
    u32 hash, slot;

    if (len < 1 + sizeof(u16) + sizeof(u32) || !sCoop.active || data[0] != sCoop.battleNo)
        return;
    memcpy(&turn, &data[1], sizeof(u16));
    memcpy(&hash, &data[3], sizeof(u32));
    slot = turn % COOP_HASH_SLOTS;
    sCoop.remoteTurn[slot] = turn;
    sCoop.remoteHash[slot] = hash;
    sCoop.remoteHashValid[slot] = TRUE;
    CompareHashes();
}

static bool32 IsChoiceCommand(u8 cmd)
{
    switch (cmd)
    {
    case CONTROLLER_CHOOSEACTION:
    case CONTROLLER_CHOOSEMOVE:
    case CONTROLLER_OPENBAG:
    case CONTROLLER_CHOOSEPOKEMON:
    case CONTROLLER_YESNOBOX:
        return TRUE;
    default:
        return FALSE;
    }
}

// The opponents' controllers run their AI when asked for a choice, and that
// AI rolls random numbers, so those calls use the engine's stream as well.
// Both games ask the opponents the same questions in the same order.
bool32 SoullikeCoop_ControllerUsesEngineRng(enum BattlerId battler)
{
    return sCoop.active && !IsOnPlayerSide(battler) && IsChoiceCommand(gBattleResources->bufferA[battler][0]);
}

// Called for every reply a controller sends to the engine. When it's this
// player's answer to a "what will you do?" question, send it to the partner.
void SoullikeCoop_OnEngineResponse(enum BattlerId battler, const u8 *data, u16 size)
{
    u8 payload[CHOICE_HEADER + CHOICE_DATA_MAX];
    u8 cmd;

    if (!sCoop.active || battler != LocalBattler())
        return;
    cmd = gBattleResources->bufferA[battler][0];
    if (!IsChoiceCommand(cmd))
        return;

    size = min(size, CHOICE_DATA_MAX);
    payload[0] = sCoop.battleNo;
    payload[1] = cmd;
    payload[2] = size;
    // Menus also leave a few answers outside the reply itself.
    payload[3] = gBattleStruct->itemPartyIndex[battler];
    payload[4] = gBattleStruct->itemMoveIndex[battler];
    payload[5] = gSelectedMonPartyId;
    payload[6] = gBattleStruct->monToSwitchIntoId[battler];
    memcpy(&payload[CHOICE_HEADER], data, size);
    QueueSend(SOULLIKE_MSG_COOP_CHOICE, payload, CHOICE_HEADER + size);
}

static void HandleChoice(const u8 *data, u8 len)
{
    struct CoopChoice *choice;

    if (len < CHOICE_HEADER || !sCoop.active || data[0] != sCoop.battleNo)
        return;
    if (sCoop.choiceCount >= COOP_CHOICE_QUEUE_SIZE)
    {
        gSoullikeLink.coopDesyncs++;
        return;
    }
    choice = &sCoop.choices[(sCoop.choiceHead + sCoop.choiceCount) % COOP_CHOICE_QUEUE_SIZE];
    choice->cmd = data[1];
    choice->size = min(data[2], min(CHOICE_DATA_MAX, len - CHOICE_HEADER));
    choice->itemPartyIndex = data[3];
    choice->itemMoveIndex = data[4];
    choice->selectedMonPartyId = data[5];
    choice->monToSwitchIntoId = data[6];
    memcpy(choice->data, &data[CHOICE_HEADER], choice->size);
    sCoop.choiceCount++;
}

// The remote controller calls this while it waits for the partner's answer.
// Returns TRUE once the answer is in the battler's reply buffer.
bool32 SoullikeCoop_TryTakePartnerChoice(enum BattlerId battler)
{
    struct CoopChoice *choice;
    u8 cmd = gBattleResources->bufferA[battler][0];
    u32 i;

    if (sCoop.choiceCount == 0)
        return FALSE;

    choice = &sCoop.choices[sCoop.choiceHead];
    sCoop.choiceHead = (sCoop.choiceHead + 1) % COOP_CHOICE_QUEUE_SIZE;
    sCoop.choiceCount--;

    if (choice->cmd != cmd)
        gSoullikeLink.coopDesyncs++;

    for (i = 0; i < choice->size; i++)
        gBattleResources->bufferB[battler][i] = choice->data[i];

    if (cmd == CONTROLLER_OPENBAG)
    {
        gBattleStruct->itemPartyIndex[battler] = choice->itemPartyIndex;
        gBattleStruct->itemMoveIndex[battler] = choice->itemMoveIndex;
    }
    else if (cmd == CONTROLLER_CHOOSEPOKEMON)
    {
        gBattleStruct->monToSwitchIntoId[battler] = choice->monToSwitchIntoId;
        gSelectedMonPartyId = choice->selectedMonPartyId;
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// Per-frame work

static void StartNewGameForTest(void)
{
    ResetTasks();
    ResetSpriteData();
    FreeAllSpritePalettes();
    ResetPaletteFade();
    SetVBlankCallback(NULL);
    SetHBlankCallback(NULL);
    gSaveBlock2Ptr->playerGender = LocalPlayerId() == 0 ? MALE : FEMALE;
    StringCopy(gSaveBlock2Ptr->playerName, sTestNames[LocalPlayerId()]);
    gSaveBlock2Ptr->optionsTextSpeed = OPTIONS_TEXT_SPEED_INSTANT;
    SetMainCallback1(NULL);
    SetMainCallback2(CB2_SoullikeTestNewGame);
}

// Lets the end-to-end test skip the intro and start a co-op battle by itself.
static void UpdateTestCommand(void)
{
    if (!SoullikeLink_IsConnected())
        return;

    switch (gSoullikeLink.testCommand)
    {
    case SOULLIKE_TEST_COOP_BATTLE:
    case SOULLIKE_TEST_RUN:
        // Wait for the title screen, so the intro isn't cut off halfway through loading.
        if (IsTitleScreenRunning())
        {
            gSoullikeLink.testCommand++; // to the matching LOADING state
            StartNewGameForTest();
        }
        break;
    case SOULLIKE_TEST_COOP_LOADING:
    case SOULLIKE_TEST_RUN_LOADING:
        if (gMain.callback2 == CB2_Overworld && !ArePlayerFieldControlsLocked() && !ScriptContext_IsEnabled())
        {
            ScriptContext_SetupScript(gSoullikeLink.testCommand == SOULLIKE_TEST_RUN_LOADING ? Debug_SoullikeRun : Debug_SoullikeCoopBattle);
            gSoullikeLink.testCommand = SOULLIKE_TEST_NONE;
        }
        break;
    }
}

void SoullikeCoop_Update(void)
{
    u8 type, len;
    u8 data[SOULLIKE_LINK_PAYLOAD_MAX];

    while (SoullikeLink_Receive(&type, data, &len))
    {
        if (len == 0)
            continue;
        switch (type)
        {
        case SOULLIKE_MSG_COOP_TEAM:
            HandleTeamChunk(data, len);
            break;
        case SOULLIKE_MSG_COOP_READY:
            HandleReady(data, len);
            break;
        case SOULLIKE_MSG_COOP_CHOICE:
            HandleChoice(data, len);
            break;
        case SOULLIKE_MSG_COOP_HASH:
            HandleHash(data, len);
            break;
        case SOULLIKE_MSG_RUN_START:
        case SOULLIKE_MSG_RUN_PICK:
            SoullikeRun_HandleMessage(type, data, len);
            break;
        }
    }
    FlushOutbox();
    UpdateTestCommand();
}
