#include "global.h"
#include "battle_factory_screen.h"
#include "battle_main.h"
#include "battle_setup.h"
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
#include "save.h"
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

// Set to 1 by tools/daily/playtest/check_all.py before the hall's first DailyBattle_StartDaily:
// that call then builds every opponent's team and every draft set once, so a set that trips
// an assert shows up in CI rather than on the day it comes up. It ends at 2.
EWRAM_DATA u8 gDailySelfTest = 0;

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

// Day 1 of the RTC count, 1 January 2000, was a Saturday.
static bool32 IsSunday(u32 day)
{
    return (day + 5) % 7 == 0;
}

// The same kind of shuffle for both: walk a fresh order of `count` opponents every `count`
// steps, so nobody repeats before everyone has shown up.
static u32 GetShuffled(u32 step, u32 count, u32 seed)
{
    u8 order[DAILY_TRAINERS_COUNT];
    u32 i, state = (step / count) * 2654435761u + seed;

    for (i = 0; i < count; i++)
        order[i] = i;
    for (i = count - 1; i > 0; i--)
    {
        u32 j = DailyRandom(&state) % (i + 1);
        u8 tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }
    return order[step % count];
}

// Sundays bring the week's boss; every other day draws from the regular opponents.
static u32 GetOpponentForDay(u32 day)
{
    if (IsSunday(day))
        return DAILY_BOSS_FIRST + GetShuffled(day / 7, DAILY_BOSS_COUNT, 0xB055);
    return GetShuffled(day, DAILY_BOSS_FIRST, 0xDA11);
}

static bool32 IsDoublesDay(u32 day)
{
    u32 state = day * 2246822519u + 0xD0B1E5;

    return DailyRandom(&state) % DOUBLES_DAY_PERIOD == 0;
}

// The pool entry for a draft choice: the seed's shuffle of the format's Pokémon decides who
// shows up, and a second roll picks which of that Pokémon's sets it brings. A daily's seed is
// its day, so everyone gets the same draft.
static u32 GetDraftPoolIndex(u32 seed, bool32 doubles, u32 round, u32 choice)
{
    static EWRAM_DATA u16 sShuffled[DRAFT_MAX_SPECIES_COUNT] = {0};
    u32 count = doubles ? DRAFT_DOUBLES_SPECIES_COUNT : DRAFT_SINGLES_SPECIES_COUNT;
    u32 i, species, state = seed * 40503u + 0xD4AF7;

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
    state = seed * 69069u + species * 362437u + 0x5E7;
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

static const u16 sBossLegends[] = DAILY_BOSS_LEGENDS;
STATIC_ASSERT(ARRAY_COUNT(sBossLegends) == DAILY_BOSS_COUNT, DailyBossLegendsCount);

const struct Trainer *GetDailyTrainerById(u32 id, bool32 doubles)
{
    if (id >= DAILY_TRAINERS_COUNT)
        id = 0;
    if (doubles)
        id += DAILY_TRAINERS_COUNT;
    return &sDailyTrainers[DIFFICULTY_NORMAL][id];
}

const struct Trainer *GetDailyTrainer(void)
{
    return GetDailyTrainerById(VarGet(VAR_DAILY_OPPONENT), VarGet(VAR_DAILY_DOUBLES));
}

u32 DailyBattle_GetOpponentForDay(u32 day)
{
    return GetOpponentForDay(day);
}

void DailyBattle_InitNewGame(void)
{
    // The Rain Badge turns off obedience checks for the level 100 drafted team.
    FlagSet(FLAG_BADGE08_GET);
    FlagSet(FLAG_SYS_POKEMON_GET);
    FlagSet(FLAG_DAILY_NO_WHITEOUT);
    DailyRecords_Init();
}

static void RunSelfTest(void)
{
    u32 i;
    u32 draftSets = sDoublesFirstSet[DRAFT_DOUBLES_SPECIES_COUNT - 1] + sDoublesSetCount[DRAFT_DOUBLES_SPECIES_COUNT - 1];

    for (i = 0; i < DAILY_TRAINERS_COUNT * 2; i++)
    {
        const struct Trainer *trainer = &sDailyTrainers[DIFFICULTY_NORMAL][i];

        SanitizeFrontTrainerPic(trainer->trainerPic);
        CreateNPCTrainerPartyFromTrainer(gParties[B_TRAINER_OPPONENT_A], trainer);
    }
    for (i = 0; i < draftSets; i++)
        CreateDraftMon(&sDraftChoices[0], i);
    ZeroEnemyPartyMons();
    gDailySelfTest = 2;
}

// Buffers the current opponent's name, class and theme into STR_VAR_1..3.
void DailyBattle_BufferOpponent(void)
{
    const struct Trainer *trainer = GetDailyTrainer();

    StringCopy(gStringVar1, trainer->trainerName);
    StringCopy(gStringVar2, gTrainerClasses[trainer->trainerClass].name);
    StringCopy(gStringVar3, sDailyTrainerThemes[VarGet(VAR_DAILY_OPPONENT)]);
}

static void SetChallenge(u32 mode, u32 day, u32 opponent, bool32 doubles, u32 seed)
{
    VarSet(VAR_DAILY_MODE, mode);
    VarSet(VAR_DAILY_DAY, day);
    VarSet(VAR_DAILY_OPPONENT, opponent);
    VarSet(VAR_DAILY_DOUBLES, doubles);
    VarSet(VAR_DAILY_SEED, seed);
    DailyBattle_BufferOpponent();
}

// Sets up today's scored daily and buffers the opponent into STR_VAR_1..3. VAR_RESULT is
// TRUE if today's one attempt is already used. VAR_0x8004 is TRUE if a scored battle was cut
// short by a reset, which counts as a loss.
void DailyBattle_StartDaily(void)
{
    u32 today = GetToday();

    if (gDailySelfTest == 1)
        RunSelfTest();
    gSpecialVar_0x8004 = DailyRecords_FinishForfeit();
    SetChallenge(DAILY_MODE_DAILY, today, GetOpponentForDay(today), IsDoublesDay(today), today);
    gSpecialVar_Result = DailyRecords_PlayedToday(today);
}

// Sets up an unscored practice battle with a fresh draft. VAR_0x8004 picks the opponent:
// PRACTICE_TODAY, PRACTICE_RANDOM, or PRACTICE_PAST_DAY + n for the daily from n days ago,
// which keeps that day's draft too.
void DailyBattle_SetupPractice(void)
{
    u32 today = GetToday();
    u32 seed = DAILY_SEED_RANDOM | (Random() & 0x3FFF);

    if (gSpecialVar_0x8004 == PRACTICE_TODAY)
    {
        SetChallenge(DAILY_MODE_PRACTICE, today, GetOpponentForDay(today), IsDoublesDay(today), seed);
    }
    else if (gSpecialVar_0x8004 == PRACTICE_RANDOM)
    {
        SetChallenge(DAILY_MODE_PRACTICE, today, Random() % DAILY_TRAINERS_COUNT,
                     Random() % DOUBLES_DAY_PERIOD == 0, seed);
    }
    else
    {
        u32 day = today - min(gSpecialVar_0x8004 - PRACTICE_PAST_DAY, today - 1);

        SetChallenge(DAILY_MODE_PRACTICE, day, GetOpponentForDay(day), IsDoublesDay(day), day);
    }
    // Practice always drafts from scratch, even when replaying a past day.
    VarSet(VAR_DAILY_DRAFT_DAY, 0);
}

// VAR_RESULT is TRUE if the current battle is a double battle.
void DailyBattle_IsDoubles(void)
{
    gSpecialVar_Result = VarGet(VAR_DAILY_DOUBLES);
}

// VAR_RESULT is TRUE in practice mode.
void DailyBattle_IsPractice(void)
{
    gSpecialVar_Result = (VarGet(VAR_DAILY_MODE) == DAILY_MODE_PRACTICE);
}

// VAR_RESULT is TRUE if the opponent is a Sunday boss; their legendary goes in VAR_0x8004 and
// its name in STR_VAR_1.
void DailyBattle_GetBoss(void)
{
    u32 id = VarGet(VAR_DAILY_OPPONENT);

    gSpecialVar_Result = (id >= DAILY_BOSS_FIRST && id < DAILY_TRAINERS_COUNT);
    if (gSpecialVar_Result)
    {
        gSpecialVar_0x8004 = sBossLegends[id - DAILY_BOSS_FIRST];
        StringCopy(gStringVar1, GetSpeciesName(gSpecialVar_0x8004));
    }
}

// Gets the draft ready. A daily draft is final: leaving halfway picks up at the same round,
// and a finished team goes straight to battle. VAR_RESULT is 0 for a new draft, 1 to resume
// one, 2 when the team is ready.
void DailyDraft_Prepare(void)
{
    u32 key = VarGet(VAR_DAILY_SEED) + 1;
    u32 round = VarGet(VAR_DAILY_DRAFT_ROUND);

    if (VarGet(VAR_DAILY_DRAFT_DAY) == key && round > 0
     && CalculatePlayerPartyCount() == min(round, (u32)DRAFT_ROUNDS))
    {
        gSpecialVar_Result = (round >= DRAFT_ROUNDS) ? 2 : 1;
        return;
    }
    ZeroPlayerPartyMons();
    CalculatePlayerPartyCount();
    VarSet(VAR_DAILY_DRAFT_ROUND, 0);
    VarSet(VAR_DAILY_DRAFT_DAY, key);
    gSpecialVar_Result = 0;
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
    u32 seed = VarGet(VAR_DAILY_SEED);
    bool32 doubles = VarGet(VAR_DAILY_DOUBLES);
    u32 round = VarGet(VAR_DAILY_DRAFT_ROUND);
    u8 *const names[DRAFT_CHOICES] = {gStringVar1, gStringVar2, gStringVar3};
    u32 i;

    for (i = 0; i < DRAFT_CHOICES; i++)
    {
        CreateDraftMon(&sDraftChoices[i], GetDraftPoolIndex(seed, doubles, round, i));
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
    VarSet(VAR_DAILY_DRAFT_ROUND, round + 1);
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
        // Saving the last pick here, behind the black screen, keeps a daily's picks final.
        if (VarGet(VAR_DAILY_MODE) == DAILY_MODE_DAILY && VarGet(VAR_DAILY_DRAFT_ROUND) > 0)
            TrySavingData(SAVE_LINK);
        CleanupOverworldWindowsAndTilemaps();
        DoDailyDraftSelectScreen(sDraftChoices);
        DestroyTask(taskId);
    }
}

// A daily saves as it goes, so a reset can't undo a pick or the battle. The first save is a
// full one when the draft starts; the later ones only write the party, vars and records
// (SaveBlocks 1 and 2, which hold SaveBlock3 too), which is much quicker.
void DailyBattle_SaveIfDaily(void)
{
    if (VarGet(VAR_DAILY_MODE) == DAILY_MODE_DAILY)
        TrySavingData(gSpecialVar_0x8004 ? SAVE_LINK : SAVE_NORMAL);
}

// Call just before the battle. A daily's one attempt is used from here on.
void DailyBattle_BeginAttempt(void)
{
    if (VarGet(VAR_DAILY_MODE) == DAILY_MODE_DAILY)
    {
        DailyRecords_BeginAttempt(VarGet(VAR_DAILY_DAY));
        TrySavingData(SAVE_LINK);
    }
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
