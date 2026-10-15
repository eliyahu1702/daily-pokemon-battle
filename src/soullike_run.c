#include "global.h"
#include "battle.h"
#include "battle_main.h"
#include "data.h"
#include "event_data.h"
#include "item.h"
#include "pokemon_storage_system.h"
#include "list_menu.h"
#include "malloc.h"
#include "pokemon.h"
#include "random.h"
#include "script.h"
#include "script_menu.h"
#include "script_pokemon_util.h"
#include "soullike_coop.h"
#include "soullike_link.h"
#include "soullike_run.h"
#include "string_util.h"
#include "task.h"
#include "constants/battle.h"
#include "constants/items.h"
#include "constants/opponents.h"
#include "constants/species.h"

#define ACT_COUNT 4
#define STOPS_PER_ACT 3
#define DRAFT_POOL_SIZE 4
#define PICK_SLOTS 8
#define OPTION_TEXT_LENGTH 32

enum RunNode
{
    NODE_WILD,
    NODE_TRAINER,
    NODE_CENTER,
    NODE_GYM,
};

enum RunPhase
{
    PHASE_START,
    PHASE_DRAFT,        // players pick from the draft pool
    PHASE_DRAFT_DONE,
    PHASE_MAP,          // the picker chooses the next stop
    PHASE_STOP,         // carry out the chosen stop
    PHASE_BATTLE,       // the script is fighting the battle
    PHASE_STOP_DONE,
    PHASE_OVER,
};

enum PickKind
{
    PICK_DRAFT,
    PICK_MAP,
};

struct ReceivedPick
{
    bool8 valid;
    u8 value;
    u16 step;
};

struct RunState
{
    u8 runNo;               // runs started since power on; both games keep it in step
    u8 phase;
    u8 act;
    u8 stop;                // stops done in this act
    u8 node;                // the stop being played
    u8 pickKind;
    u8 picker;              // 0 = player 1, 1 = player 2
    u8 optionCount;
    u8 options[DRAFT_POOL_SIZE]; // what each menu row stands for (a pool slot or a map choice)
    u16 step;               // decisions made so far
    u32 seed;
    rng_value_t rng;        // only advanced by code both games run identically
    u8 gymTypes[ACT_COUNT];
    u8 map[STOPS_PER_ACT][2];
    // Draft
    u16 pool[DRAFT_POOL_SIZE];
    bool8 poolTaken[DRAFT_POOL_SIZE];
    u8 draftLevel;
    u8 draftPicksDone;
    u8 draftFirst;          // who picks first (and fourth) in the current draft
    bool8 isStarterDraft;
    u8 battlesWon;
    u8 fainted;             // this player's Pokémon lost so far
    // Filled in by the link; StartRun keeps everything from here on.
    // Partner
    bool8 partnerStarted;
    u8 partnerRunNo;
    u8 partnerName[PLAYER_NAME_LENGTH + 1];
    struct ReceivedPick picks[PICK_SLOTS];
};

static EWRAM_DATA struct RunState sRun = {0};

// ---------------------------------------------------------------------------
// Tables

static const u16 sStarters[] = {
    SPECIES_BULBASAUR, SPECIES_CHARMANDER, SPECIES_SQUIRTLE,
    SPECIES_CHIKORITA, SPECIES_CYNDAQUIL, SPECIES_TOTODILE,
    SPECIES_TREECKO, SPECIES_TORCHIC, SPECIES_MUDKIP,
    SPECIES_TURTWIG, SPECIES_CHIMCHAR, SPECIES_PIPLUP,
    SPECIES_SNIVY, SPECIES_TEPIG, SPECIES_OSHAWOTT,
    SPECIES_CHESPIN, SPECIES_FENNEKIN, SPECIES_FROAKIE,
    SPECIES_ROWLET, SPECIES_LITTEN, SPECIES_POPPLIO,
    SPECIES_GROOKEY, SPECIES_SCORBUNNY, SPECIES_SOBBLE,
    SPECIES_SPRIGATITO, SPECIES_FUECOCO, SPECIES_QUAXLY,
};

// Wild drafts draw from the early list in acts 1 and 2 and add the late list after that.
static const u16 sWildEarly[] = {
    SPECIES_PIDGEY, SPECIES_ODDISH, SPECIES_BELLSPROUT, SPECIES_GEODUDE, SPECIES_MACHOP,
    SPECIES_ABRA, SPECIES_GASTLY, SPECIES_ZUBAT, SPECIES_MAREEP, SPECIES_PSYDUCK,
    SPECIES_RALTS, SPECIES_SHROOMISH, SPECIES_ARON, SPECIES_ELECTRIKE, SPECIES_SHINX,
    SPECIES_STARLY, SPECIES_MAGIKARP, SPECIES_LILLIPUP, SPECIES_ROGGENROLA, SPECIES_TIMBURR,
    SPECIES_LITWICK, SPECIES_FLETCHLING, SPECIES_PAWMI, SPECIES_NACLI, SPECIES_POOCHYENA,
    SPECIES_SANDSHREW, SPECIES_NUMEL, SPECIES_SPHEAL, SPECIES_SEEDOT, SPECIES_TYMPOLE,
};

static const u16 sWildLate[] = {
    SPECIES_DRATINI, SPECIES_LARVITAR, SPECIES_BAGON, SPECIES_BELDUM, SPECIES_GIBLE,
    SPECIES_AXEW, SPECIES_DEINO, SPECIES_GOOMY, SPECIES_JANGMO_O, SPECIES_FRIGIBAX,
    SPECIES_HOUNDOUR, SPECIES_TRAPINCH, SPECIES_SNORUNT, SPECIES_KRABBY, SPECIES_DRILBUR,
};

struct GymInfo
{
    u16 leader;
    u16 species[8];
};

// Base forms; they're evolved to fit the level when the team is made.
static const struct GymInfo sGyms[NUMBER_OF_MON_TYPES] = {
    [TYPE_NORMAL]   = {TRAINER_NORMAN_1,   {SPECIES_ZIGZAGOON, SPECIES_LILLIPUP, SPECIES_TEDDIURSA, SPECIES_KANGASKHAN, SPECIES_MILTANK, SPECIES_TAUROS, SPECIES_WHISMUR, SPECIES_STARLY}},
    [TYPE_FIGHTING] = {TRAINER_BRAWLY_1,   {SPECIES_MACHOP, SPECIES_MANKEY, SPECIES_MAKUHITA, SPECIES_MEDITITE, SPECIES_TIMBURR, SPECIES_MIENFOO, SPECIES_CROAGUNK, SPECIES_HAWLUCHA}},
    [TYPE_FLYING]   = {TRAINER_WINONA_1,   {SPECIES_PIDGEY, SPECIES_SPEAROW, SPECIES_DODUO, SPECIES_TAILLOW, SPECIES_STARLY, SPECIES_FLETCHLING, SPECIES_ROOKIDEE, SPECIES_SKARMORY}},
    [TYPE_POISON]   = {TRAINER_WALLACE,    {SPECIES_EKANS, SPECIES_GRIMER, SPECIES_KOFFING, SPECIES_ZUBAT, SPECIES_GULPIN, SPECIES_SKORUPI, SPECIES_TRUBBISH, SPECIES_SALANDIT}},
    [TYPE_GROUND]   = {TRAINER_WALLACE,    {SPECIES_SANDSHREW, SPECIES_DIGLETT, SPECIES_CUBONE, SPECIES_PHANPY, SPECIES_TRAPINCH, SPECIES_HIPPOPOTAS, SPECIES_DRILBUR, SPECIES_SILICOBRA}},
    [TYPE_ROCK]     = {TRAINER_ROXANNE_1,  {SPECIES_GEODUDE, SPECIES_ONIX, SPECIES_NOSEPASS, SPECIES_ROGGENROLA, SPECIES_LARVITAR, SPECIES_ARON, SPECIES_NACLI, SPECIES_DWEBBLE}},
    [TYPE_BUG]      = {TRAINER_WALLACE,    {SPECIES_CATERPIE, SPECIES_WEEDLE, SPECIES_VENONAT, SPECIES_NINCADA, SPECIES_SEWADDLE, SPECIES_DEWPIDER, SPECIES_HERACROSS, SPECIES_SCYTHER}},
    [TYPE_GHOST]    = {TRAINER_PHOEBE,     {SPECIES_GASTLY, SPECIES_SHUPPET, SPECIES_DUSKULL, SPECIES_LITWICK, SPECIES_DRIFLOON, SPECIES_SANDYGAST, SPECIES_GREAVARD, SPECIES_SABLEYE}},
    [TYPE_STEEL]    = {TRAINER_WALLACE,    {SPECIES_MAGNEMITE, SPECIES_ARON, SPECIES_BELDUM, SPECIES_BRONZOR, SPECIES_KLINK, SPECIES_HONEDGE, SPECIES_MAWILE, SPECIES_CUFANT}},
    [TYPE_FIRE]     = {TRAINER_FLANNERY_1, {SPECIES_NUMEL, SPECIES_HOUNDOUR, SPECIES_LITLEO, SPECIES_LITWICK, SPECIES_SIZZLIPEDE, SPECIES_TORKOAL, SPECIES_SLUGMA, SPECIES_PONYTA}},
    [TYPE_WATER]    = {TRAINER_JUAN_1,     {SPECIES_MAGIKARP, SPECIES_PSYDUCK, SPECIES_KRABBY, SPECIES_CARVANHA, SPECIES_WAILMER, SPECIES_BUIZEL, SPECIES_TYMPOLE, SPECIES_MARILL}},
    [TYPE_GRASS]    = {TRAINER_WALLACE,    {SPECIES_ODDISH, SPECIES_BELLSPROUT, SPECIES_SEEDOT, SPECIES_CHERUBI, SPECIES_FOONGUS, SPECIES_SEWADDLE, SPECIES_BOUNSWEET, SPECIES_SKIDDO}},
    [TYPE_ELECTRIC] = {TRAINER_WATTSON_1,  {SPECIES_MAGNEMITE, SPECIES_VOLTORB, SPECIES_ELECTRIKE, SPECIES_MAREEP, SPECIES_SHINX, SPECIES_BLITZLE, SPECIES_TYNAMO, SPECIES_PAWMI}},
    [TYPE_PSYCHIC]  = {TRAINER_TATE_AND_LIZA_1, {SPECIES_ABRA, SPECIES_DROWZEE, SPECIES_RALTS, SPECIES_SPOINK, SPECIES_MEDITITE, SPECIES_ELGYEM, SPECIES_NATU, SPECIES_SLOWPOKE}},
    [TYPE_ICE]      = {TRAINER_GLACIA,     {SPECIES_SWINUB, SPECIES_SNORUNT, SPECIES_SPHEAL, SPECIES_CUBCHOO, SPECIES_BERGMITE, SPECIES_VANILLITE, SPECIES_LAPRAS, SPECIES_FRIGIBAX}},
    [TYPE_DRAGON]   = {TRAINER_DRAKE,      {SPECIES_DRATINI, SPECIES_BAGON, SPECIES_GIBLE, SPECIES_AXEW, SPECIES_DEINO, SPECIES_GOOMY, SPECIES_SWABLU, SPECIES_JANGMO_O}},
    [TYPE_DARK]     = {TRAINER_SIDNEY,     {SPECIES_POOCHYENA, SPECIES_HOUNDOUR, SPECIES_SANDILE, SPECIES_PURRLOIN, SPECIES_ZORUA, SPECIES_NICKIT, SPECIES_IMPIDIMP, SPECIES_ABSOL}},
    [TYPE_FAIRY]    = {TRAINER_WALLACE,    {SPECIES_SNUBBULL, SPECIES_FLABEBE, SPECIES_HATENNA, SPECIES_MAWILE, SPECIES_MIMIKYU, SPECIES_DEDENNE, SPECIES_MARILL, SPECIES_TOGEPI}},
};

static const u16 sTrainers[] = {
    TRAINER_CALVIN_1, TRAINER_JOEY, TRAINER_ALLEN, TRAINER_LAO_1, TRAINER_ALBERT,
};

static const u8 sLevelCaps[ACT_COUNT] = {15, 25, 35, 45};
static const u8 sActStartLevels[ACT_COUNT] = {7, 15, 25, 35};

// The second picker's bonus, better each act.
static const u16 sBonusItems[ACT_COUNT][3] = {
    {ITEM_POTION, ITEM_ORAN_BERRY, ITEM_GREAT_BALL},
    {ITEM_SUPER_POTION, ITEM_RARE_CANDY, ITEM_SITRUS_BERRY},
    {ITEM_HYPER_POTION, ITEM_LEFTOVERS, ITEM_LIFE_ORB},
    {ITEM_FULL_RESTORE, ITEM_FOCUS_SASH, ITEM_CHOICE_SCARF},
};

static const u8 *const sNodeNames[] = {
    [NODE_WILD]    = COMPOUND_STRING("Wild draft"),
    [NODE_TRAINER] = COMPOUND_STRING("Trainer battle"),
    [NODE_CENTER]  = COMPOUND_STRING("Pokémon Center"),
    [NODE_GYM]     = COMPOUND_STRING("Gym Leader"),
};

// ---------------------------------------------------------------------------
// Helpers

static u8 LocalPlayer(void)
{
    return gSoullikeLink.playerId == 0 ? 0 : 1;
}

static const u8 *PlayerName(u8 player)
{
    return player == LocalPlayer() ? gSaveBlock2Ptr->playerName : sRun.partnerName;
}

static u32 SharedRandom(u32 n)
{
    return LocalRandom32(&sRun.rng) % n;
}

// A number only this player needs (like a drafted Pokémon's personality),
// so the shared stream is left alone.
static u32 PrivateRandom(u32 salt)
{
    rng_value_t rng = LocalRandomSeed(sRun.seed ^ (sRun.step * 0x9E3779B9) ^ (salt << 8) ^ LocalPlayer());
    return LocalRandom32(&rng);
}

static u8 StopLevel(void)
{
    u8 start = sActStartLevels[sRun.act], cap = sLevelCaps[sRun.act];
    if (sRun.node == NODE_GYM)
        return cap;
    return start + (cap - start) * (sRun.stop + 1) / (STOPS_PER_ACT + 1);
}

// Only plain level-up evolutions, so both games always agree.
static u16 EvolveForLevel(u16 species, u8 level)
{
    u32 tries, i;

    for (tries = 0; tries < 2; tries++)
    {
        const struct Evolution *evos = GetSpeciesEvolutions(species);
        bool32 evolved = FALSE;

        if (evos == NULL)
            break;
        for (i = 0; evos[i].method != EVOLUTIONS_END; i++)
        {
            if (evos[i].method == EVO_LEVEL && evos[i].params == NULL && evos[i].param <= level)
            {
                species = evos[i].targetSpecies;
                evolved = TRUE;
                break;
            }
        }
        if (!evolved)
            break;
    }
    return species;
}

static void SetMonLevel(struct Pokemon *mon, u8 level)
{
    u16 species = GetMonData(mon, MON_DATA_SPECIES);
    u16 evolved = EvolveForLevel(species, level);
    u32 exp = gExperienceTables[gSpeciesInfo[evolved].growthRate][level];

    if (evolved != species)
    {
        SetMonData(mon, MON_DATA_SPECIES, &evolved);
        SetMonData(mon, MON_DATA_NICKNAME, GetSpeciesName(evolved));
    }
    SetMonData(mon, MON_DATA_EXP, &exp);
    CalculateMonStats(mon);
    GiveMonInitialMoveset(mon);
}

static bool32 GiveRunMon(u16 species, u8 level, u32 salt)
{
    struct Pokemon mon;

    CreateMonWithIVs(&mon, EvolveForLevel(species, level), level, PrivateRandom(salt), OTID_STRUCT_PLAYER_ID, 15);
    return GiveCapturedMonToPlayer(&mon) != MON_CANT_GIVE;
}

static void PickDistinct(u16 *out, u32 count, const u16 *list, u32 listCount)
{
    u32 i, j;

    for (i = 0; i < count; i++)
    {
        bool32 repeat;
        do
        {
            out[i] = list[SharedRandom(listCount)];
            repeat = FALSE;
            for (j = 0; j < i; j++)
                repeat |= (out[j] == out[i]);
        } while (repeat);
    }
}

static void RollActMap(void)
{
    u32 i;

    for (i = 0; i < STOPS_PER_ACT; i++)
    {
        // The first stop of every act always offers a wild draft, so teams can grow.
        u8 a = i == 0 ? NODE_WILD : SharedRandom(3);
        u8 b;
        do
            b = SharedRandom(3);
        while (b == a);
        sRun.map[i][0] = a;
        sRun.map[i][1] = b;
    }
}

static void StartDraft(bool32 starters)
{
    u32 i;

    sRun.isStarterDraft = starters;
    if (starters)
    {
        PickDistinct(sRun.pool, DRAFT_POOL_SIZE, sStarters, ARRAY_COUNT(sStarters));
        sRun.draftLevel = sActStartLevels[0];
        sRun.draftFirst = 0;
    }
    else
    {
        u16 all[ARRAY_COUNT(sWildEarly) + ARRAY_COUNT(sWildLate)];
        u32 count = ARRAY_COUNT(sWildEarly);

        memcpy(all, sWildEarly, sizeof(sWildEarly));
        if (sRun.act >= 2)
        {
            memcpy(&all[count], sWildLate, sizeof(sWildLate));
            count += ARRAY_COUNT(sWildLate);
        }
        PickDistinct(sRun.pool, DRAFT_POOL_SIZE, all, count);
        sRun.draftLevel = StopLevel();
        // Whoever picked second last time picks first now.
        sRun.draftFirst ^= 1;
    }
    for (i = 0; i < DRAFT_POOL_SIZE; i++)
        sRun.poolTaken[i] = FALSE;
    sRun.draftPicksDone = 0;
    sRun.phase = PHASE_DRAFT;
}

static void FillOpponent(struct SoullikeCoopOpponent *opponent, u16 trainerId, u32 count, const u16 *list, u32 listCount, u8 level)
{
    u32 i;

    opponent->trainerId = trainerId;
    opponent->count = count;
    PickDistinct(opponent->species, count, list, listCount);
    for (i = 0; i < count; i++)
    {
        opponent->levels[i] = level;
        opponent->species[i] = EvolveForLevel(opponent->species[i], level);
    }
}

static void SetUpBattle(void)
{
    struct SoullikeCoopOpponent a, b;
    u8 level = StopLevel();

    if (sRun.node == NODE_GYM)
    {
        const struct GymInfo *gym = &sGyms[sRun.gymTypes[sRun.act]];
        u16 species[5];
        u32 i;

        // The leader brings three, the gym trainer two, all of the gym's type.
        PickDistinct(species, 5, gym->species, ARRAY_COUNT(gym->species));
        a.trainerId = gym->leader;
        a.count = 3;
        for (i = 0; i < 3; i++)
        {
            a.levels[i] = level - 2 + i;
            a.species[i] = EvolveForLevel(species[i], a.levels[i]);
        }
        b.trainerId = sTrainers[SharedRandom(ARRAY_COUNT(sTrainers))];
        b.count = 2;
        for (i = 0; i < 2; i++)
        {
            b.levels[i] = level - 3;
            b.species[i] = EvolveForLevel(species[3 + i], b.levels[i]);
        }
    }
    else
    {
        // Two trainers with two each: a little below the stop's level, since
        // there are four of them.
        const u16 *list = sRun.act >= 2 ? sWildLate : sWildEarly;
        u32 count = sRun.act >= 2 ? ARRAY_COUNT(sWildLate) : ARRAY_COUNT(sWildEarly);
        FillOpponent(&a, sTrainers[SharedRandom(ARRAY_COUNT(sTrainers))], 2, list, count, level - 2);
        do
            FillOpponent(&b, sTrainers[SharedRandom(ARRAY_COUNT(sTrainers))], 2, list, count, level - 2);
        while (b.trainerId == a.trainerId);
    }
    SoullikeCoop_SetOpponents(&a, &b);
}

static void UpdateMailbox(void)
{
    gSoullikeLink.runAct = sRun.act;
    gSoullikeLink.runStep = sRun.step;
}

// ---------------------------------------------------------------------------
// Messages from the partner

void SoullikeRun_HandleMessage(u8 type, const u8 *data, u8 len)
{
    if (type == SOULLIKE_MSG_RUN_START && len >= 2 + PLAYER_NAME_LENGTH)
    {
        sRun.partnerRunNo = data[0];
        sRun.partnerStarted = TRUE;
        memcpy(sRun.partnerName, &data[1], PLAYER_NAME_LENGTH);
        sRun.partnerName[PLAYER_NAME_LENGTH] = EOS;
    }
    else if (type == SOULLIKE_MSG_RUN_PICK && len >= 4 && data[0] == sRun.runNo)
    {
        u16 step = data[1] | (data[2] << 8);
        struct ReceivedPick *pick = &sRun.picks[step % PICK_SLOTS];
        pick->step = step;
        pick->value = data[3];
        pick->valid = TRUE;
    }
}

// ---------------------------------------------------------------------------
// Script natives

static void Task_WaitForPartnerStart(u8 taskId)
{
    if (sRun.partnerStarted && sRun.partnerRunNo == sRun.runNo)
    {
        gSpecialVar_Result = TRUE;
        ScriptContext_Enable();
        DestroyTask(taskId);
    }
    else if (!SoullikeLink_IsConnected() && ++gTasks[taskId].data[0] > 600)
    {
        gSoullikeLink.runState = SOULLIKE_RUN_IDLE;
        gSpecialVar_Result = FALSE;
        ScriptContext_Enable();
        DestroyTask(taskId);
    }
}

void SoullikeRun_Begin(struct ScriptContext *ctx)
{
    u8 payload[2 + PLAYER_NAME_LENGTH];
    u8 runNo = sRun.runNo + (gSoullikeLink.runState != SOULLIKE_RUN_IDLE);

    memset(&sRun.picks, 0, sizeof(sRun.picks));
    sRun.runNo = runNo;
    sRun.phase = PHASE_START;
    payload[0] = runNo;
    memcpy(&payload[1], gSaveBlock2Ptr->playerName, PLAYER_NAME_LENGTH);
    payload[1 + PLAYER_NAME_LENGTH] = gSaveBlock2Ptr->playerGender;
    SoullikeCoop_Send(SOULLIKE_MSG_RUN_START, payload, sizeof(payload));
    gSoullikeLink.runState = SOULLIKE_RUN_STARTING;
    CreateTask(Task_WaitForPartnerStart, 80);
}

static void StartRun(void)
{
    u32 i, j;
    u8 types[NUMBER_OF_MON_TYPES];
    u32 typeCount = 0;
    u8 runNo = sRun.runNo;

    // Keep what the link already filled in: the partner may have picked already.
    memset(&sRun, 0, offsetof(struct RunState, partnerStarted));
    sRun.runNo = runNo;

    // The first run of the day is the Daily Run; runs after it go on from there.
    sRun.seed = gSoullikeLink.dailySeed + runNo * 0x2545F491;
    sRun.rng = LocalRandomSeed(sRun.seed);

    for (i = 0; i < NUMBER_OF_MON_TYPES; i++)
    {
        if (sGyms[i].leader != 0)
            types[typeCount++] = i;
    }
    for (i = 0; i < ACT_COUNT; i++)
    {
        j = i + SharedRandom(typeCount - i);
        sRun.gymTypes[i] = types[j];
        types[j] = types[i];
    }
    RollActMap();

    ZeroPlayerPartyMons();
    gSoullikeLink.runState = SOULLIKE_RUN_PLAYING;
    UpdateMailbox();
    StartDraft(TRUE);
}

static void BufferRunOver(bool32 won)
{
    sRun.phase = PHASE_OVER;
    gSoullikeLink.runState = won ? SOULLIKE_RUN_WON : SOULLIKE_RUN_LOST;
    ConvertIntToDecimalStringN(gStringVar1, sRun.act + won, STR_CONV_MODE_LEFT_ALIGN, 2);
    ConvertIntToDecimalStringN(gStringVar2, sRun.battlesWon, STR_CONV_MODE_LEFT_ALIGN, 3);
    ConvertIntToDecimalStringN(gStringVar3, sRun.fainted, STR_CONV_MODE_LEFT_ALIGN, 3);
    if (won)
        StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("You beat all four Gym Leaders!\nThe run is won!\pBattles won: {STR_VAR_2}\nYour Pokémon lost: {STR_VAR_3}"));
    else
        StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("Your team was wiped out.\nThe run is over.\pBadges: {STR_VAR_1}  Battles won: {STR_VAR_2}\nYour Pokémon lost: {STR_VAR_3}"));
}

void SoullikeRun_Next(struct ScriptContext *ctx)
{
    for (;;)
    {
        switch (sRun.phase)
        {
        case PHASE_START:
            StartRun();
            StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("A new run begins!\pFirst, draft your starters.\nTake turns picking from the pool."));
            gSpecialVar_Result = SOULLIKE_RUN_ACTION_MESSAGE;
            return;
        case PHASE_DRAFT:
            sRun.pickKind = PICK_DRAFT;
            gSpecialVar_Result = SOULLIKE_RUN_ACTION_PICK;
            return;
        case PHASE_DRAFT_DONE:
        {
            u8 second = sRun.draftFirst ^ 1;
            u16 item = sBonusItems[sRun.act][SharedRandom(3)];

            sRun.phase = sRun.isStarterDraft ? PHASE_MAP : PHASE_STOP_DONE;
            if (second == LocalPlayer())
                AddBagItem(item, 1);
            StringCopy(gStringVar1, PlayerName(second));
            CopyItemName(item, gStringVar2);
            StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("{STR_VAR_1} picked second, so they get\na bonus {STR_VAR_2}!"));
            gSpecialVar_Result = SOULLIKE_RUN_ACTION_MESSAGE;
            return;
        }
        case PHASE_MAP:
            if (sRun.stop < STOPS_PER_ACT)
            {
                sRun.pickKind = PICK_MAP;
                gSpecialVar_Result = SOULLIKE_RUN_ACTION_PICK;
                return;
            }
            sRun.node = NODE_GYM;
            sRun.phase = PHASE_STOP;
            ConvertIntToDecimalStringN(gStringVar1, sRun.act + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
            StringCopy(gStringVar2, gTypesInfo[sRun.gymTypes[sRun.act]].name);
            StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("The {STR_VAR_2}-type Gym Leader of\nAct {STR_VAR_1} is waiting!"));
            gSpecialVar_Result = SOULLIKE_RUN_ACTION_MESSAGE;
            return;
        case PHASE_STOP:
            switch (sRun.node)
            {
            case NODE_WILD:
                StartDraft(FALSE);
                continue;
            case NODE_CENTER:
                HealPlayerParty();
                sRun.phase = PHASE_STOP_DONE;
                StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("Your Pokémon were healed\nto full health."));
                gSpecialVar_Result = SOULLIKE_RUN_ACTION_MESSAGE;
                return;
            default:
                SetUpBattle();
                sRun.phase = PHASE_BATTLE;
                gSpecialVar_Result = SOULLIKE_RUN_ACTION_BATTLE;
                return;
            }
        case PHASE_STOP_DONE:
            if (sRun.node == NODE_GYM)
            {
                sRun.act++;
                sRun.stop = 0;
                if (sRun.act == ACT_COUNT)
                {
                    BufferRunOver(TRUE);
                    gSpecialVar_Result = SOULLIKE_RUN_ACTION_WON;
                    return;
                }
                RollActMap();
                UpdateMailbox();
                ConvertIntToDecimalStringN(gStringVar1, sRun.act, STR_CONV_MODE_LEFT_ALIGN, 1);
                ConvertIntToDecimalStringN(gStringVar2, sLevelCaps[sRun.act], STR_CONV_MODE_LEFT_ALIGN, 2);
                StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("Badge {STR_VAR_1} earned!\nThe level cap is now {STR_VAR_2}."));
                sRun.phase = PHASE_MAP;
                gSpecialVar_Result = SOULLIKE_RUN_ACTION_MESSAGE;
                return;
            }
            sRun.stop++;
            sRun.phase = PHASE_MAP;
            continue;
        default:
            BufferRunOver(gSoullikeLink.runState == SOULLIKE_RUN_WON);
            gSpecialVar_Result = gSoullikeLink.runState == SOULLIKE_RUN_WON ? SOULLIKE_RUN_ACTION_WON : SOULLIKE_RUN_ACTION_LOST;
            return;
        }
    }
}

static u8 DraftPicker(void)
{
    // Snake order: first, second, second, first.
    static const u8 order[DRAFT_POOL_SIZE] = {0, 1, 1, 0};
    return sRun.draftFirst ^ order[sRun.draftPicksDone];
}

static void PushOption(const u8 *text, u8 id)
{
    struct ListMenuItem item;
    u8 *buffer = Alloc(OPTION_TEXT_LENGTH);

    StringCopyN(buffer, text, OPTION_TEXT_LENGTH - 1);
    buffer[OPTION_TEXT_LENGTH - 1] = EOS;
    item.name = buffer;
    item.id = id;
    MultichoiceDynamic_PushElement(item);
}

// Sets VAR_RESULT to TRUE when this player picks; the options are then
// ready for dynmultistack. gStringVar4 holds the question either way.
void SoullikeRun_PreparePick(struct ScriptContext *ctx)
{
    u32 i;

    sRun.optionCount = 0;
    if (sRun.pickKind == PICK_DRAFT)
    {
        sRun.picker = DraftPicker();
        for (i = 0; i < DRAFT_POOL_SIZE; i++)
        {
            if (!sRun.poolTaken[i])
                sRun.options[sRun.optionCount++] = i;
        }
        ConvertIntToDecimalStringN(gStringVar2, sRun.draftLevel, STR_CONV_MODE_LEFT_ALIGN, 2);
        StringCopy(gStringVar1, PlayerName(sRun.picker));
        StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("{STR_VAR_1}'s pick! Which Pokémon\njoins the team? (Lv. {STR_VAR_2})"));
    }
    else
    {
        sRun.picker = (sRun.act + sRun.stop) & 1;
        sRun.options[0] = 0;
        sRun.options[1] = 1;
        sRun.optionCount = 2;
        ConvertIntToDecimalStringN(gStringVar2, sRun.act + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
        ConvertIntToDecimalStringN(gStringVar3, sRun.stop + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
        StringCopy(gStringVar1, PlayerName(sRun.picker));
        StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("Act {STR_VAR_2}, stop {STR_VAR_3} of 3.\n{STR_VAR_1} chooses where to go next."));
    }

    gSpecialVar_Result = (sRun.picker == LocalPlayer());
    if (!gSpecialVar_Result)
        return;

    for (i = 0; i < sRun.optionCount; i++)
    {
        if (sRun.pickKind == PICK_DRAFT)
            PushOption(GetSpeciesName(sRun.pool[sRun.options[i]]), i);
        else
            PushOption(sNodeNames[sRun.map[sRun.stop][i]], i);
    }
}

void SoullikeRun_SendPick(struct ScriptContext *ctx)
{
    u8 payload[4];
    u8 value = gSpecialVar_Result < sRun.optionCount ? gSpecialVar_Result : 0;

    payload[0] = sRun.runNo;
    payload[1] = sRun.step & 0xFF;
    payload[2] = sRun.step >> 8;
    payload[3] = value;
    SoullikeCoop_Send(SOULLIKE_MSG_RUN_PICK, payload, sizeof(payload));
    gSpecialVar_Result = value;
}

static void Task_WaitForPartnerPick(u8 taskId)
{
    struct ReceivedPick *pick = &sRun.picks[sRun.step % PICK_SLOTS];

    if (pick->valid && pick->step == sRun.step)
    {
        pick->valid = FALSE;
        gSpecialVar_Result = pick->value < sRun.optionCount ? pick->value : 0;
        ScriptContext_Enable();
        DestroyTask(taskId);
    }
}

void SoullikeRun_WaitPick(struct ScriptContext *ctx)
{
    CreateTask(Task_WaitForPartnerPick, 80);
}

// Applies VAR_RESULT as the picker's choice, in both games.
void SoullikeRun_ApplyPick(struct ScriptContext *ctx)
{
    u8 choice = sRun.options[gSpecialVar_Result];

    StringCopy(gStringVar1, PlayerName(sRun.picker));
    if (sRun.pickKind == PICK_DRAFT)
    {
        u16 species = sRun.pool[choice];
        bool32 given = TRUE;

        sRun.poolTaken[choice] = TRUE;
        if (sRun.picker == LocalPlayer())
            given = GiveRunMon(species, sRun.draftLevel, sRun.draftPicksDone);
        StringCopy(gStringVar2, GetSpeciesName(species));
        if (given)
            StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("{STR_VAR_1} picked {STR_VAR_2}!"));
        else
            StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("{STR_VAR_1} picked {STR_VAR_2},\nbut their team is full."));
        if (++sRun.draftPicksDone == DRAFT_POOL_SIZE)
            sRun.phase = PHASE_DRAFT_DONE;
    }
    else
    {
        sRun.node = sRun.map[sRun.stop][choice];
        sRun.phase = PHASE_STOP;
        StringCopy(gStringVar2, sNodeNames[sRun.node]);
        StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("{STR_VAR_1} chose: {STR_VAR_2}."));
    }
    sRun.step++;
    UpdateMailbox();
}

// After a run battle: Nuzlocke, level ups, and whether the run goes on.
void SoullikeRun_AfterBattle(struct ScriptContext *ctx)
{
    struct Pokemon result[MULTI_PARTY_SIZE];
    u32 count, i, j, lost = 0;
    u8 level = StopLevel(), cap = sLevelCaps[sRun.act];

    if (gSpecialVar_Result != B_OUTCOME_WON)
    {
        BufferRunOver(FALSE);
        gSpecialVar_Result = SOULLIKE_RUN_ACTION_LOST;
        return;
    }
    sRun.battlesWon++;

    // Bring the battle's damage back into the party.
    count = SoullikeCoop_GetLocalBattleResult(result);
    for (i = 0; i < count; i++)
    {
        u32 personality = GetMonData(&result[i], MON_DATA_PERSONALITY);
        for (j = 0; j < PARTY_SIZE; j++)
        {
            if (GetMonData(&gParties[B_TRAINER_PLAYER][j], MON_DATA_SPECIES) != SPECIES_NONE
             && GetMonData(&gParties[B_TRAINER_PLAYER][j], MON_DATA_PERSONALITY) == personality)
            {
                gParties[B_TRAINER_PLAYER][j] = result[i];
                break;
            }
        }
    }

    // Nuzlocke: a fainted Pokémon is gone.
    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_SPECIES) != SPECIES_NONE && GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_HP) == 0)
        {
            ZeroMonData(&gParties[B_TRAINER_PLAYER][i]);
            lost++;
        }
    }
    CompactPartySlots();
    CalculatePlayerPartyCount();
    sRun.fainted += lost;

    // Survivors grow toward the level cap.
    for (i = 0; i < gPartiesCount[B_TRAINER_PLAYER]; i++)
    {
        u8 current = GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_LEVEL);
        u8 target = min(cap, max(current + 2, level + 1));
        if (target > current)
            SetMonLevel(&gParties[B_TRAINER_PLAYER][i], target);
    }

    ConvertIntToDecimalStringN(gStringVar1, lost, STR_CONV_MODE_LEFT_ALIGN, 1);
    if (gPartiesCount[B_TRAINER_PLAYER] == 0)
    {
        // Second Wind: a wiped player comes back with one fresh Pokémon.
        u16 species = sWildEarly[PrivateRandom(0xAA) % ARRAY_COUNT(sWildEarly)];
        GiveRunMon(species, level, 0xAB);
        StringCopy(gStringVar2, GetSpeciesName(species));
        StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("Victory! But your whole team fell.\pSecond Wind: {STR_VAR_2} joins you!"));
    }
    else if (lost != 0)
    {
        StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("Victory! {STR_VAR_1} of your Pokémon fainted\nand left the team for good."));
    }
    else
    {
        StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("Victory! Your Pokémon grew stronger."));
    }
    sRun.phase = PHASE_STOP_DONE;
    gSpecialVar_Result = SOULLIKE_RUN_ACTION_MESSAGE;
}
