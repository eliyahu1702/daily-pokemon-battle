#include "global.h"
#include "battle.h"
#include "bg.h"
#include "daily_battle.h"
#include "data.h"
#include "event_data.h"
#include "gpu_regs.h"
#include "graphics.h"
#include "main.h"
#include "malloc.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "rtc.h"
#include "menu.h"
#include "international_string_util.h"
#include "script.h"
#include "field_screen_effect.h"
#include "battle_main.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "window.h"
#include "constants/daily_battle.h"
#include "constants/rgb.h"
#include "constants/songs.h"

// The screen shown after the daily battle: the drafted team, the battle stats and a score.
// It reuses the Battle Factory menu background so it matches the draft picker.

// Score: a win is worth 1000, plus 150 for each Pokémon still standing and 1 per percent of
// HP left across the team, minus 15 per turn. A loss scores 75 per opposing Pokémon knocked out.
#define SCORE_WIN          1000
#define SCORE_PER_SURVIVOR 150
#define SCORE_PER_TURN     15
#define SCORE_PER_KO_LOSS  75
#define SCORE_WIN_MIN      100

struct DailyResult
{
    u16 score;
    u16 hpPercent; // Sum of every team member's remaining HP percentage, 0-600
    u8 turns;
    u8 lost;
    u8 knockouts;
    bool8 won:1;
    bool8 newBest:1;
    u8 slot[PARTY_SIZE]; // Party index shown in each spot, or PARTY_SIZE for none
    u8 hp[PARTY_SIZE];   // HP percent left, 0 if fainted
};

#define HP_EMPTY 0xFF

static const u16 sResultMonVars[PARTY_SIZE] =
{
    VAR_DAILY_RESULT_MON_0, VAR_DAILY_RESULT_MON_1, VAR_DAILY_RESULT_MON_2,
    VAR_DAILY_RESULT_MON_3, VAR_DAILY_RESULT_MON_4, VAR_DAILY_RESULT_MON_5,
};

enum
{
    RESULTS_WIN_TITLE,
    RESULTS_WIN_SCORE,
    RESULTS_WIN_OPPONENT,
    RESULTS_WIN_TEAM,
    RESULTS_WIN_STATS,
    RESULTS_WIN_PROMPT,
};

#define PALNUM_TEXT 15

static EWRAM_DATA struct DailyResult sResult = {0};
static EWRAM_DATA u8 sIconSpriteIds[PARTY_SIZE] = {0};

void DailyBattle_ShowResults(void);
static void CB2_InitResultsScreen(void);
static void Task_ResultsScreen(u8 taskId);

static const u16 sText_Pal[] = INCGFX_U16("graphics/battle_frontier/factory_screen/text.pal", ".gbapal");
static const u8 sTextColors_Normal[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_TRANSPARENT};
// Color 1 of the factory text palette is black, for text on the light picture band.
static const u8 sTextColors_Dark[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_WHITE, TEXT_COLOR_TRANSPARENT};
static const u8 sTextColors_Red[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_RED, TEXT_COLOR_TRANSPARENT};

static const u8 sText_Victory[] = _("VICTORY!");
static const u8 sText_Defeat[] = _("DEFEAT");
static const u8 sText_Score[] = _("SCORE {STR_VAR_1}");
static const u8 sText_Opponent[] = _("vs {STR_VAR_2} {STR_VAR_1}");
static const u8 sText_Fainted[] = _("FNT");
static const u8 sText_Percent[] = _("{STR_VAR_1}%");
static const u8 sText_Stats1[] = _("Turns: {STR_VAR_1}   Lost: {STR_VAR_2}   KOs: {STR_VAR_3}/6");
static const u8 sText_Stats2[] = _("HP left: {STR_VAR_1}%   Best today: {STR_VAR_2}");
static const u8 sText_NewBest[] = _("NEW BEST!");
static const u8 sText_Ok[] = _("OK");

static const struct BgTemplate sResults_BgTemplates[] =
{
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 24,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 0,
        .baseTile = 0
    },
    {
        .bg = 1,
        .charBaseIndex = 1,
        .mapBaseIndex = 25,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 3,
        .baseTile = 0
    },
};

static const struct WindowTemplate sResults_WindowTemplates[] =
{
    [RESULTS_WIN_TITLE] = {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 2,
        .width = 12,
        .height = 2,
        .paletteNum = PALNUM_TEXT,
        .baseBlock = 1,
    },
    [RESULTS_WIN_SCORE] = {
        .bg = 0,
        .tilemapLeft = 19,
        .tilemapTop = 2,
        .width = 11,
        .height = 2,
        .paletteNum = PALNUM_TEXT,
        .baseBlock = 25,
    },
    [RESULTS_WIN_OPPONENT] = {
        .bg = 0,
        .tilemapLeft = 11,
        .tilemapTop = 0,
        .width = 19,
        .height = 2,
        .paletteNum = PALNUM_TEXT,
        .baseBlock = 47,
    },
    [RESULTS_WIN_TEAM] = {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 9,
        .width = 30,
        .height = 2,
        .paletteNum = PALNUM_TEXT,
        .baseBlock = 85,
    },
    [RESULTS_WIN_STATS] = {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 15,
        .width = 20,
        .height = 3,
        .paletteNum = PALNUM_TEXT,
        .baseBlock = 145,
    },
    [RESULTS_WIN_PROMPT] = {
        .bg = 0,
        .tilemapLeft = 22,
        .tilemapTop = 14,
        .width = 8,
        .height = 6,
        .paletteNum = PALNUM_TEXT,
        .baseBlock = 205,
    },
    DUMMY_WIN_TEMPLATE,
};

// Fills in each team member's HP percent. Returns the sum.
static u32 GetTeamHpPercent(u32 *lost)
{
    u32 i, total = 0;

    *lost = 0;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        u32 hp, maxHp;

        sResult.slot[i] = PARTY_SIZE;
        if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE)
            continue;
        sResult.slot[i] = i;
        hp = GetMonData(mon, MON_DATA_HP);
        maxHp = GetMonData(mon, MON_DATA_MAX_HP);
        sResult.hp[i] = (hp == 0 || maxHp == 0) ? 0 : max(1u, hp * 100 / maxHp);
        if (hp == 0)
            (*lost)++;
        total += sResult.hp[i];
    }
    return total;
}

static void SaveResult(void)
{
    u32 i;

    VarSet(VAR_DAILY_RESULT_SCORE, sResult.score);
    VarSet(VAR_DAILY_RESULT_STATS, sResult.turns | (sResult.knockouts << 8) | (sResult.lost << 12));
    for (i = 0; i < PARTY_SIZE; i++)
    {
        u32 species = SPECIES_NONE;

        if (sResult.slot[i] < PARTY_SIZE)
            species = GetMonData(&gParties[B_TRAINER_PLAYER][sResult.slot[i]], MON_DATA_SPECIES);
        VarSet(sResultMonVars[i], ((species & 0xFF) << 8) | (species == SPECIES_NONE ? HP_EMPTY : sResult.hp[i]));
    }
}

// The team is still today's draft, but a rematch may have changed the lead order, so each
// saved spot is matched back to the party by species.
static void LoadSavedResult(void)
{
    u32 i, j, stats = VarGet(VAR_DAILY_RESULT_STATS);
    bool8 used[PARTY_SIZE] = {0};

    sResult.won = TRUE;
    sResult.newBest = FALSE;
    sResult.score = VarGet(VAR_DAILY_RESULT_SCORE);
    sResult.turns = stats & 0xFF;
    sResult.knockouts = (stats >> 8) & 0xF;
    sResult.lost = stats >> 12;
    sResult.hpPercent = 0;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        u32 saved = VarGet(sResultMonVars[i]);

        sResult.slot[i] = PARTY_SIZE;
        sResult.hp[i] = saved & 0xFF;
        if (sResult.hp[i] == HP_EMPTY)
            continue;
        sResult.hpPercent += sResult.hp[i];
        for (j = 0; j < PARTY_SIZE; j++)
        {
            u32 species = GetMonData(&gParties[B_TRAINER_PLAYER][j], MON_DATA_SPECIES);

            if (!used[j] && species != SPECIES_NONE && (species & 0xFF) == (saved >> 8))
                break;
        }
        if (j < PARTY_SIZE)
        {
            used[j] = TRUE;
            sResult.slot[i] = j;
        }
    }
}

// Call right after the daily battle, before the party is healed. Sets VAR_RESULT to TRUE on a win.
void DailyBattle_RecordResult(void)
{
    u32 lost, today, score;

    sResult.won = (gBattleOutcome == B_OUTCOME_WON);
    sResult.turns = gBattleResults.battleTurnCounter;
    sResult.hpPercent = GetTeamHpPercent(&lost);
    sResult.lost = lost;
    sResult.knockouts = gBattleResults.opponentFaintCounter;

    if (sResult.won)
    {
        s32 total = SCORE_WIN + SCORE_PER_SURVIVOR * (PARTY_SIZE - lost) + sResult.hpPercent - SCORE_PER_TURN * sResult.turns;
        score = max(total, SCORE_WIN_MIN);
    }
    else
    {
        score = SCORE_PER_KO_LOSS * sResult.knockouts;
    }
    sResult.score = score;

    RtcCalcLocalTime();
    today = RtcGetLocalDayCount() + 1;
    if (VarGet(VAR_DAILY_BEST_DAY) != today)
    {
        VarSet(VAR_DAILY_BEST_DAY, today);
        VarSet(VAR_DAILY_BEST_SCORE, 0);
    }
    sResult.newBest = (score > VarGet(VAR_DAILY_BEST_SCORE));
    if (sResult.newBest)
        VarSet(VAR_DAILY_BEST_SCORE, score);
    // Keep today's best win for the hall's "see your results" option.
    if (sResult.won && (VarGet(VAR_DAILY_WON_DAY) != today || score > VarGet(VAR_DAILY_RESULT_SCORE)))
        SaveResult();

    gSpecialVar_Result = sResult.won;
}

// VAR_RESULT is TRUE if there's a win from today to show again.
void DailyBattle_HasSavedResult(void)
{
    RtcCalcLocalTime();
    gSpecialVar_Result = VarGet(VAR_DAILY_WON_DAY) == RtcGetLocalDayCount() + 1
                      && VarGet(VAR_DAILY_RESULT_SCORE) != 0;
}

void DailyBattle_ShowSavedResults(void)
{
    LoadSavedResult();
    DailyBattle_ShowResults();
}

void DailyBattle_ShowResults(void)
{
    LockPlayerFieldControls();
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    CreateTask(Task_ResultsScreen, 10);
    gTasks[FindTaskIdByFunc(Task_ResultsScreen)].data[0] = 0;
}

static void CB2_ResultsScreen(void)
{
    AnimateSprites();
    BuildOamBuffer();
    RunTextPrinters();
    UpdatePaletteFade();
    RunTasks();
}

static void VBlankCB_ResultsScreen(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void PrintText(u32 windowId, const u8 *colors, u32 font, u32 x, u32 y, const u8 *text)
{
    AddTextPrinterParameterized3(windowId, font, x, y, colors, TEXT_SKIP_DRAW, text);
}

static void PrintResults(void)
{
    u32 i;
    const struct Trainer *trainer = GetDailyTrainer();

    for (i = 0; i < RESULTS_WIN_PROMPT + 1; i++)
        FillWindowPixelBuffer(i, PIXEL_FILL(0));

    PrintText(RESULTS_WIN_TITLE, sResult.won ? sTextColors_Red : sTextColors_Normal, FONT_NORMAL, 2, 1,
              sResult.won ? sText_Victory : sText_Defeat);

    ConvertIntToDecimalStringN(gStringVar1, sResult.score, STR_CONV_MODE_LEFT_ALIGN, 5);
    StringExpandPlaceholders(gStringVar4, sText_Score);
    PrintText(RESULTS_WIN_SCORE, sTextColors_Red, FONT_NORMAL, GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 86), 1, gStringVar4);

    StringCopy(gStringVar1, trainer->trainerName);
    StringCopy(gStringVar2, gTrainerClasses[trainer->trainerClass].name);
    StringExpandPlaceholders(gStringVar4, sText_Opponent);
    PrintText(RESULTS_WIN_OPPONENT, sTextColors_Normal, FONT_NORMAL, GetStringRightAlignXOffset(FONT_NORMAL, gStringVar4, 150), 1, gStringVar4);

    // HP left under each team member's icon.
    for (i = 0; i < PARTY_SIZE; i++)
    {
        const u8 *text;

        if (sResult.slot[i] >= PARTY_SIZE)
            continue;
        if (sResult.hp[i] == 0)
        {
            text = sText_Fainted;
        }
        else
        {
            ConvertIntToDecimalStringN(gStringVar1, sResult.hp[i], STR_CONV_MODE_LEFT_ALIGN, 3);
            StringExpandPlaceholders(gStringVar4, sText_Percent);
            text = gStringVar4;
        }
        PrintText(RESULTS_WIN_TEAM, sResult.hp[i] == 0 ? sTextColors_Red : sTextColors_Dark, FONT_SMALL,
                  20 + 40 * i - GetStringWidth(FONT_SMALL, text, 0) / 2, 2, text);
    }

    ConvertIntToDecimalStringN(gStringVar1, sResult.turns, STR_CONV_MODE_LEFT_ALIGN, 3);
    ConvertIntToDecimalStringN(gStringVar2, sResult.lost, STR_CONV_MODE_LEFT_ALIGN, 1);
    ConvertIntToDecimalStringN(gStringVar3, sResult.knockouts, STR_CONV_MODE_LEFT_ALIGN, 1);
    StringExpandPlaceholders(gStringVar4, sText_Stats1);
    PrintText(RESULTS_WIN_STATS, sTextColors_Normal, FONT_SMALL, 2, 0, gStringVar4);
    ConvertIntToDecimalStringN(gStringVar1, sResult.hpPercent / PARTY_SIZE, STR_CONV_MODE_LEFT_ALIGN, 3);
    ConvertIntToDecimalStringN(gStringVar2, VarGet(VAR_DAILY_BEST_SCORE), STR_CONV_MODE_LEFT_ALIGN, 5);
    StringExpandPlaceholders(gStringVar4, sText_Stats2);
    PrintText(RESULTS_WIN_STATS, sTextColors_Normal, FONT_SMALL, 2, 12, gStringVar4);

    if (sResult.newBest)
        PrintText(RESULTS_WIN_PROMPT, sTextColors_Red, FONT_NORMAL, 4, 1, sText_NewBest);
    PrintText(RESULTS_WIN_PROMPT, sTextColors_Normal, FONT_NORMAL, 4, 33, sText_Ok);

    for (i = 0; i < RESULTS_WIN_PROMPT + 1; i++)
    {
        PutWindowTilemap(i);
        CopyWindowToVram(i, COPYWIN_FULL);
    }
}

static void CreateTeamIcons(void)
{
    u32 i;

    LoadMonIconPalettes();
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon;

        sIconSpriteIds[i] = MAX_SPRITES;
        if (sResult.slot[i] >= PARTY_SIZE)
            continue;
        mon = &gParties[B_TRAINER_PLAYER][sResult.slot[i]];
        sIconSpriteIds[i] = CreateMonIcon(GetMonData(mon, MON_DATA_SPECIES), SpriteCB_MonIcon, 20 + 40 * i, 56, 0,
                                          GetMonData(mon, MON_DATA_PERSONALITY));
        // Fainted team members stand still.
        if (sResult.hp[i] == 0)
            gSprites[sIconSpriteIds[i]].callback = SpriteCallbackDummy;
    }
}

static void DestroyTeamIcons(void)
{
    u32 i;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (sIconSpriteIds[i] < MAX_SPRITES)
            FreeAndDestroyMonIconSprite(&gSprites[sIconSpriteIds[i]]);
    }
    FreeMonIconPalettes();
}

static void CB2_InitResultsScreen(void)
{
    switch (gMain.state)
    {
    case 0:
        SetVBlankCallback(NULL);
        SetHBlankCallback(NULL);
        CpuFill32(0, (void *)VRAM, VRAM_SIZE);
        ResetBgsAndClearDma3BusyFlags(0);
        InitBgsFromTemplates(0, sResults_BgTemplates, ARRAY_COUNT(sResults_BgTemplates));
        InitWindows(sResults_WindowTemplates);
        DeactivateAllTextPrinters();
        SetGpuReg(REG_OFFSET_BLDCNT, 0);
        SetGpuReg(REG_OFFSET_BLDALPHA, 0);
        SetGpuReg(REG_OFFSET_BLDY, 0);
        SetGpuReg(REG_OFFSET_WININ, 0);
        SetGpuReg(REG_OFFSET_WINOUT, 0);
        ChangeBgX(0, 0, BG_COORD_SET);
        ChangeBgY(0, 0, BG_COORD_SET);
        ChangeBgX(1, 0, BG_COORD_SET);
        ChangeBgY(1, 0, BG_COORD_SET);
        gMain.state++;
        break;
    case 1:
        ResetPaletteFade();
        ResetSpriteData();
        ResetTasks();
        FreeAllSpritePalettes();
        LoadBgTiles(1, gFrontierFactoryMenu_Gfx, sizeof(gFrontierFactoryMenu_Gfx), 0);
        LoadBgTilemap(1, gFrontierFactoryMenu_Tilemap, BG_SCREEN_SIZE, 0);
        LoadPalette(gFrontierFactoryMenu_Pal, 0, 2 * PLTT_SIZE_4BPP);
        LoadPalette(sText_Pal, BG_PLTT_ID(PALNUM_TEXT), PLTT_SIZEOF(4));
        gMain.state++;
        break;
    case 2:
        CreateTeamIcons();
        PrintResults();
        gMain.state++;
        break;
    default:
        ShowBg(0);
        ShowBg(1);
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_BG0_ON | DISPCNT_BG1_ON | DISPCNT_OBJ_1D_MAP);
        SetVBlankCallback(VBlankCB_ResultsScreen);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        CreateTask(Task_ResultsScreen, 0);
        gTasks[FindTaskIdByFunc(Task_ResultsScreen)].data[0] = 2;
        if (sResult.won)
            PlayFanfare(MUS_OBTAIN_B_POINTS);
        SetMainCallback2(CB2_ResultsScreen);
        break;
    }
}

#define tState data[0]
#define tTimer data[1]

#define RESULTS_INPUT_DELAY 90 // frames

static void Task_ResultsScreen(u8 taskId)
{
    switch (gTasks[taskId].tState)
    {
    case 0: // Overworld fading out
        if (!gPaletteFade.active)
        {
            DestroyTask(taskId);
            CleanupOverworldWindowsAndTilemaps();
            gMain.state = 0;
            SetMainCallback2(CB2_InitResultsScreen);
        }
        break;
    case 2: // Results fading in. Input waits a moment so mashing A out of the battle doesn't skip the score.
        if (!gPaletteFade.active && ++gTasks[taskId].tTimer >= RESULTS_INPUT_DELAY)
            gTasks[taskId].tState++;
        break;
    case 3:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
            gTasks[taskId].tState++;
        }
        break;
    case 4:
        if (!gPaletteFade.active)
        {
            DestroyTeamIcons();
            FreeAllWindowBuffers();
            SetMainCallback2(CB2_ReturnToFieldContinueScript);
            DestroyTask(taskId);
        }
        break;
    }
}
