#include "global.h"
#include "battle_factory_screen.h"
#include "battle_main.h"
#include "daily_battle.h"
#include "data.h"
#include "event_data.h"
#include "field_screen_effect.h"
#include "field_weather.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "pokemon.h"
#include "random.h"
#include "rtc.h"
#include "script.h"
#include "string_util.h"
#include "task.h"
#include "trainer_util.h"
#include "constants/battle_ai.h"
#include "constants/daily_battle.h"
#include "constants/daily_battle_generated.h"

// The draft pool is stored as trainer parties, so it can be written in Showdown format and
// checked by trainerproc like any other team.
static const struct Trainer sDraftPool[DIFFICULTY_COUNT][DRAFT_POOL_CHUNKS] =
{
#include "data/draft_pool.h"
};

// Singles teams at DailyTrainerId, doubles teams at DailyTrainerId + DAILY_TRAINERS_COUNT.
static const struct Trainer sDailyTrainers[DIFFICULTY_COUNT][DAILY_TRAINERS_COUNT * 2] =
{
#include "data/daily_trainers.h"
};

// Each draft Pokémon's sets sit side by side in the pool; see daily_battle_generated.h.
static const u16 sSinglesFirstSet[] = DRAFT_SINGLES_FIRST_SET;
static const u8 sSinglesSetCount[] = DRAFT_SINGLES_SET_COUNT;
static const u16 sDoublesFirstSet[] = DRAFT_DOUBLES_FIRST_SET;
static const u8 sDoublesSetCount[] = DRAFT_DOUBLES_SET_COUNT;
STATIC_ASSERT(ARRAY_COUNT(sSinglesFirstSet) == DRAFT_SINGLES_SPECIES_COUNT, DraftSinglesCount);
STATIC_ASSERT(ARRAY_COUNT(sDoublesFirstSet) == DRAFT_DOUBLES_SPECIES_COUNT, DraftDoublesCount);

#define DRAFT_MAX_SPECIES_COUNT max(DRAFT_SINGLES_SPECIES_COUNT, DRAFT_DOUBLES_SPECIES_COUNT)

static const u8 *const sDailyTrainerThemes[] = DAILY_TRAINER_THEMES;
STATIC_ASSERT(ARRAY_COUNT(sDailyTrainerThemes) == DAILY_TRAINERS_COUNT, DailyTrainerThemesCount);

static EWRAM_DATA struct Pokemon sDraftChoices[DRAFT_CHOICES] = {0};

static void Task_OpenDraftPicker(u8 taskId);
static void CB2_ReturnFromChooseLeads(void);

// A fixed generator, separate from the game's RNG, so every player gets the same
// draft and opponent on the same date.
static u32 DailyRandom(u32 *state)
{
    *state = ISO_RANDOMIZE1(*state);
    return *state >> 16;
}

static u32 GetToday(void)
{
    RtcCalcLocalTime();
    return RtcGetLocalDayCount();
}

static u32 GetOpponentForDay(u32 day)
{
    u8 order[DAILY_TRAINERS_COUNT];
    u32 i, state = (day / DAILY_TRAINERS_COUNT) * 2654435761u + 0xDA11;

    // Walk a fresh shuffle of every opponent before anyone repeats.
    for (i = 0; i < DAILY_TRAINERS_COUNT; i++)
        order[i] = i;
    for (i = DAILY_TRAINERS_COUNT - 1; i > 0; i--)
    {
        u32 j = DailyRandom(&state) % (i + 1);
        u8 tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }
    return order[day % DAILY_TRAINERS_COUNT];
}

static bool32 IsDoublesDay(u32 day)
{
    u32 state = day * 2246822519u + 0xD0B1E5;

    return DailyRandom(&state) % DOUBLES_DAY_PERIOD == 0;
}

// The pool entry for a draft choice: today's shuffle of the format's Pokémon decides who
// shows up, and a second roll picks which of that Pokémon's sets it brings.
static u32 GetDraftPoolIndex(u32 day, bool32 doubles, u32 round, u32 choice)
{
    static EWRAM_DATA u16 sShuffled[DRAFT_MAX_SPECIES_COUNT] = {0};
    u32 count = doubles ? DRAFT_DOUBLES_SPECIES_COUNT : DRAFT_SINGLES_SPECIES_COUNT;
    u32 i, species, state = day * 40503u + 0xD4AF7;

    for (i = 0; i < count; i++)
        sShuffled[i] = i;
    for (i = count - 1; i > 0; i--)
    {
        u32 j = DailyRandom(&state) % (i + 1);
        u16 tmp = sShuffled[i];
        sShuffled[i] = sShuffled[j];
        sShuffled[j] = tmp;
    }
    species = sShuffled[round * DRAFT_CHOICES + choice];
    state = day * 69069u + species * 362437u + 0x5E7;
    if (doubles)
        return sDoublesFirstSet[species] + DailyRandom(&state) % sDoublesSetCount[species];
    return sSinglesFirstSet[species] + DailyRandom(&state) % sSinglesSetCount[species];
}

static void CreateDraftMon(struct Pokemon *mon, u32 poolIndex)
{
    const struct Trainer *pool = &sDraftPool[DIFFICULTY_NORMAL][poolIndex / DRAFT_POOL_CHUNK_SIZE];
    struct TrainerGenerator generator;

    // Drafted mons belong to the player, so they obey and show the player as OT. The OT ID
    // is part of the encryption key, so it has to be set before the mon is created.
    MakeTrainerGenerator(&generator, pool);
    generator.otID = OTID_STRUCT_PLAYER_ID;
    StringCopyN(generator.name, gSaveBlock2Ptr->playerName, TRAINER_NAME_LENGTH + 1);
    GenerateMonFromTrainerMon(mon, &pool->party[poolIndex % DRAFT_POOL_CHUNK_SIZE], &generator);
    SetMonData(mon, MON_DATA_OT_GENDER, &gSaveBlock2Ptr->playerGender);
    CalculateMonStats(mon);
}

const struct Trainer *GetDailyTrainer(void)
{
    u32 id = VarGet(VAR_DAILY_OPPONENT);

    if (id >= DAILY_TRAINERS_COUNT)
        id = 0;
    if (VarGet(VAR_DAILY_DOUBLES))
        id += DAILY_TRAINERS_COUNT;
    return &sDailyTrainers[DIFFICULTY_NORMAL][id];
}

void DailyBattle_InitNewGame(void)
{
    // The Rain Badge turns off obedience checks for the level 100 drafted team.
    FlagSet(FLAG_BADGE08_GET);
    FlagSet(FLAG_SYS_POKEMON_GET);
    FlagSet(FLAG_DAILY_NO_WHITEOUT);
}

// Picks today's opponent and format. Buffers name, class and theme into STR_VAR_1..3.
// VAR_RESULT is TRUE if the player already beat today's opponent.
void DailyBattle_Setup(void)
{
    u32 today = GetToday();
    u32 id = GetOpponentForDay(today);
    const struct Trainer *trainer;

    VarSet(VAR_DAILY_OPPONENT, id);
    VarSet(VAR_DAILY_DOUBLES, IsDoublesDay(today));
    trainer = GetDailyTrainer();
    StringCopy(gStringVar1, trainer->trainerName);
    StringCopy(gStringVar2, gTrainerClasses[trainer->trainerClass].name);
    StringCopy(gStringVar3, sDailyTrainerThemes[id]);
    gSpecialVar_Result = (VarGet(VAR_DAILY_WON_DAY) == today + 1);
}

// VAR_RESULT is TRUE if today's battle is a double battle.
void DailyBattle_IsDoubles(void)
{
    gSpecialVar_Result = IsDoublesDay(GetToday());
}

// VAR_RESULT is TRUE if the player has a finished team drafted today.
void DailyDraft_HasTodaysTeam(void)
{
    gSpecialVar_Result = VarGet(VAR_DAILY_DRAFT_DAY) == GetToday() + 1
                      && VarGet(VAR_DAILY_DRAFT_ROUND) >= DRAFT_ROUNDS
                      && CalculatePlayerPartyCount() == PARTY_SIZE;
}

void DailyDraft_Begin(void)
{
    ZeroPlayerPartyMons();
    VarSet(VAR_DAILY_DRAFT_ROUND, 0);
    VarSet(VAR_DAILY_DRAFT_DAY, 0);
}

// Buffers the round number (1-6) into STR_VAR_1. VAR_RESULT is TRUE when the draft is finished.
void DailyDraft_BufferRound(void)
{
    u32 round = VarGet(VAR_DAILY_DRAFT_ROUND);

    gSpecialVar_Result = (round >= DRAFT_ROUNDS);
    ConvertIntToDecimalStringN(gStringVar1, round + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
}

// Builds this round's three choices and buffers their names into STR_VAR_1..3.
void DailyDraft_PrepareChoices(void)
{
    u32 today = GetToday();
    bool32 doubles = IsDoublesDay(today);
    u32 round = VarGet(VAR_DAILY_DRAFT_ROUND);
    u8 *const names[DRAFT_CHOICES] = {gStringVar1, gStringVar2, gStringVar3};
    u32 i;

    for (i = 0; i < DRAFT_CHOICES; i++)
    {
        CreateDraftMon(&sDraftChoices[i], GetDraftPoolIndex(today, doubles, round, i));
        StringCopy(names[i], GetSpeciesName(GetMonData(&sDraftChoices[i], MON_DATA_SPECIES)));
    }
}

// Adds choice VAR_0x8005 to the player's party and moves to the next round.
void DailyDraft_Pick(void)
{
    u32 choice = min(gSpecialVar_0x8005, DRAFT_CHOICES - 1);
    u32 round = VarGet(VAR_DAILY_DRAFT_ROUND);

    if (round >= DRAFT_ROUNDS)
        return;
    gParties[B_TRAINER_PLAYER][round] = sDraftChoices[choice];
    CalculatePlayerPartyCount();
    VarSet(VAR_DAILY_DRAFT_ROUND, ++round);
    if (round == DRAFT_ROUNDS)
        VarSet(VAR_DAILY_DRAFT_DAY, GetToday() + 1);
}

// Opens the Battle Factory style picker on this round's choices. The pick comes back in VAR_0x8005.
void DailyDraft_OpenPicker(void)
{
    LockPlayerFieldControls();
    FadeScreen(FADE_TO_BLACK, 0);
    CreateTask(Task_OpenDraftPicker, 10);
}

static void Task_OpenDraftPicker(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        CleanupOverworldWindowsAndTilemaps();
        DoDailyDraftSelectScreen(sDraftChoices);
        DestroyTask(taskId);
    }
}

// Records today's win and buffers the win streak into STR_VAR_1.
void DailyBattle_RecordWin(void)
{
    u32 today = GetToday();
    u32 lastWin = VarGet(VAR_DAILY_WON_DAY);
    u32 streak = VarGet(VAR_DAILY_STREAK);

    if (lastWin != today + 1)
    {
        // A win yesterday (stored as today) continues the streak.
        streak = (lastWin == today) ? streak + 1 : 1;
        VarSet(VAR_DAILY_STREAK, streak);
        VarSet(VAR_DAILY_WON_DAY, today + 1);
    }
    ConvertIntToDecimalStringN(gStringVar1, streak, STR_CONV_MODE_LEFT_ALIGN, 5);
}

// Buffers the win streak into STR_VAR_3, keeping the opponent's name in STR_VAR_1..2.
void DailyBattle_BufferStreak(void)
{
    ConvertIntToDecimalStringN(gStringVar3, VarGet(VAR_DAILY_STREAK), STR_CONV_MODE_LEFT_ALIGN, 5);
}

// Opens the party menu to pick the lead (two leads on a doubles day). The picks move to the
// front of the party in the order chosen; backing out keeps the current order.
void DailyBattle_ChooseLeads(void)
{
    gSpecialVar_0x8005 = VarGet(VAR_DAILY_DOUBLES) ? 2 : 1;
    VarSet(VAR_FRONTIER_FACILITY, FACILITY_DAILY_LEADS);
    gMain.savedCallback = CB2_ReturnFromChooseLeads;
    InitChooseHalfPartyForBattle(0);
}

static void CB2_ReturnFromChooseLeads(void)
{
    struct Pokemon party[PARTY_SIZE];
    u32 i, count = 0;

    if (gSelectedOrderFromParty[0] != 0)
    {
        for (i = 0; i < MAX_FRONTIER_PARTY_SIZE && gSelectedOrderFromParty[i] != 0; i++)
            party[count++] = gParties[B_TRAINER_PLAYER][gSelectedOrderFromParty[i] - 1];
        for (i = 0; i < PARTY_SIZE; i++)
        {
            u32 j;
            bool32 chosen = FALSE;

            for (j = 0; j < MAX_FRONTIER_PARTY_SIZE && gSelectedOrderFromParty[j] != 0; j++)
                chosen |= (gSelectedOrderFromParty[j] - 1 == i);
            if (!chosen && count < PARTY_SIZE)
                party[count++] = gParties[B_TRAINER_PLAYER][i];
        }
        memcpy(gParties[B_TRAINER_PLAYER], party, sizeof(party));
    }
    VarSet(VAR_FRONTIER_FACILITY, 0);
    SetMainCallback2(CB2_ReturnToFieldContinueScriptPlayMapMusic);
}
