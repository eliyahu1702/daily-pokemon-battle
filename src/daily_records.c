#include "global.h"
#include "bg.h"
#include "daily_battle.h"
#include "data.h"
#include "event_data.h"
#include "field_screen_effect.h"
#include "gpu_regs.h"
#include "graphics.h"
#include "international_string_util.h"
#include "main.h"
#include "menu.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "rtc.h"
#include "script.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "window.h"
#include "constants/daily_battle.h"
#include "constants/daily_battle_generated.h"
#include "constants/rgb.h"
#include "constants/songs.h"
#include "constants/trainers.h"

// The Daily Battle Hall's records: stats, streaks, a history of recent dailies and badges,
// kept in SaveBlock3. Only scored dailies count; practice battles can earn one badge.

#define DAILY_RECORDS_MAGIC 0xDA17

#define sRecords (&gSaveBlock3Ptr->dailyRecords)

struct DailyBadge
{
    const u8 *name;
    const u8 *description;
};

static const struct DailyBadge sDailyBadges[DAILY_BADGE_COUNT] =
{
    [DAILY_BADGE_FIRST_WIN]      = {COMPOUND_STRING("First Win"),      COMPOUND_STRING("Win a daily battle.")},
    [DAILY_BADGE_DOUBLE_TROUBLE] = {COMPOUND_STRING("Double Trouble"), COMPOUND_STRING("Win a daily double battle.")},
    [DAILY_BADGE_FLAWLESS]       = {COMPOUND_STRING("Flawless"),       COMPOUND_STRING("Win without losing a POKéMON.")},
    [DAILY_BADGE_LAST_STAND]     = {COMPOUND_STRING("Last Stand"),     COMPOUND_STRING("Win with one POKéMON left.")},
    [DAILY_BADGE_SPEEDSTER]      = {COMPOUND_STRING("Speedster"),      COMPOUND_STRING("Win in 10 turns or fewer.")},
    [DAILY_BADGE_HIGH_ROLLER]    = {COMPOUND_STRING("High Roller"),    COMPOUND_STRING("Score 1800 or more.")},
    [DAILY_BADGE_ON_A_ROLL]      = {COMPOUND_STRING("On a Roll"),      COMPOUND_STRING("Win 3 days in a row.")},
    [DAILY_BADGE_WEEK_WARRIOR]   = {COMPOUND_STRING("Week Warrior"),   COMPOUND_STRING("Win 7 days in a row.")},
    [DAILY_BADGE_UNSTOPPABLE]    = {COMPOUND_STRING("Unstoppable"),    COMPOUND_STRING("Win 30 days in a row.")},
    [DAILY_BADGE_BOSS_SLAYER]    = {COMPOUND_STRING("Boss Slayer"),    COMPOUND_STRING("Beat a Sunday boss.")},
    [DAILY_BADGE_LEGEND_HUNTER]  = {COMPOUND_STRING("Legend Hunter"),  COMPOUND_STRING("Beat 5 Sunday bosses.")},
    [DAILY_BADGE_TITLE_TAKER]    = {COMPOUND_STRING("Title Taker"),    COMPOUND_STRING("Beat a CHAMPION in a daily.")},
    [DAILY_BADGE_REGULAR]        = {COMPOUND_STRING("Regular"),        COMPOUND_STRING("Play 10 daily battles.")},
    [DAILY_BADGE_VETERAN]        = {COMPOUND_STRING("Veteran"),        COMPOUND_STRING("Play 50 daily battles.")},
    [DAILY_BADGE_CENTURION]      = {COMPOUND_STRING("Centurion"),      COMPOUND_STRING("Play 100 daily battles.")},
    [DAILY_BADGE_PRACTICE_PAYS]  = {COMPOUND_STRING("Practice Pays"),  COMPOUND_STRING("Win a practice battle.")},
};

// Badges earned by the last result, handed to the hall's script one at a time.
static EWRAM_DATA u32 sNewBadges = 0;

static u32 GetToday(void)
{
    RtcCalcLocalTime();
    return RtcGetLocalDayCount();
}

void DailyRecords_Init(void)
{
    memset(sRecords, 0, sizeof(*sRecords));
    sRecords->magic = DAILY_RECORDS_MAGIC;
}

// Saves from before the records came in have something else here. Their streak and today's
// win were kept in vars, so those carry over.
static void TryMigrate(void)
{
    if (sRecords->magic == DAILY_RECORDS_MAGIC)
        return;
    DailyRecords_Init();
    sRecords->lastWinDay = VarGet(VAR_DAILY_WON_DAY);
    sRecords->streak = VarGet(VAR_DAILY_STREAK);
    sRecords->bestStreak = sRecords->streak;
    // The streak's wins are the only ones on record.
    sRecords->played = sRecords->wins = sRecords->streak;
    if (sRecords->lastWinDay == GetToday() + 1)
        sRecords->playedDay = sRecords->lastWinDay;
}

static void Award(u32 badge)
{
    if (!(sRecords->badges & (1u << badge)))
    {
        sRecords->badges |= 1u << badge;
        sNewBadges |= 1u << badge;
    }
}

static void AwardBadges(const struct DailyHistoryEntry *entry)
{
    const struct Trainer *trainer = GetDailyTrainerById(entry->opponent, entry->doubles);
    bool32 boss = entry->opponent >= DAILY_BOSS_FIRST;

    if (entry->won)
    {
        Award(DAILY_BADGE_FIRST_WIN);
        if (entry->doubles)
            Award(DAILY_BADGE_DOUBLE_TROUBLE);
        if (entry->lost == 0)
            Award(DAILY_BADGE_FLAWLESS);
        if (entry->lost == PARTY_SIZE - 1)
            Award(DAILY_BADGE_LAST_STAND);
        if (entry->turns <= 10)
            Award(DAILY_BADGE_SPEEDSTER);
        if (entry->score >= 1800)
            Award(DAILY_BADGE_HIGH_ROLLER);
        if (boss)
            Award(DAILY_BADGE_BOSS_SLAYER);
        if (sRecords->bossWins >= 5)
            Award(DAILY_BADGE_LEGEND_HUNTER);
        if (trainer->trainerClass == TRAINER_CLASS_CHAMPION)
            Award(DAILY_BADGE_TITLE_TAKER);
    }
    if (sRecords->streak >= 3)
        Award(DAILY_BADGE_ON_A_ROLL);
    if (sRecords->streak >= 7)
        Award(DAILY_BADGE_WEEK_WARRIOR);
    if (sRecords->streak >= 30)
        Award(DAILY_BADGE_UNSTOPPABLE);
    if (sRecords->played >= 10)
        Award(DAILY_BADGE_REGULAR);
    if (sRecords->played >= 50)
        Award(DAILY_BADGE_VETERAN);
    if (sRecords->played >= 100)
        Award(DAILY_BADGE_CENTURION);
}

// Records a scored daily. Returns TRUE if it's a new best score.
bool32 DailyRecords_AddResult(const struct DailyLastResult *result)
{
    const struct DailyHistoryEntry *entry = &result->entry;
    bool32 newBest = FALSE;

    TryMigrate();
    sNewBadges = 0;
    sRecords->attemptDay = 0;
    sRecords->playedDay = entry->day;
    sRecords->played++;
    if (entry->won)
    {
        sRecords->wins++;
        // A win yesterday continues the streak.
        sRecords->streak = (sRecords->lastWinDay == entry->day - 1) ? sRecords->streak + 1 : 1;
        sRecords->lastWinDay = entry->day;
        sRecords->bestStreak = max(sRecords->bestStreak, sRecords->streak);
        if (entry->opponent >= DAILY_BOSS_FIRST)
            sRecords->bossWins++;
        newBest = (entry->score > sRecords->bestScore);
        if (newBest)
            sRecords->bestScore = entry->score;
    }
    else
    {
        sRecords->streak = 0;
    }
    sRecords->last = *result;
    sRecords->history[sRecords->historyHead] = *entry;
    sRecords->historyHead = (sRecords->historyHead + 1) % DAILY_HISTORY_COUNT;
    sRecords->historyCount = min(sRecords->historyCount + 1, DAILY_HISTORY_COUNT);
    AwardBadges(entry);
    return newBest;
}

void DailyRecords_AwardPracticeWin(void)
{
    TryMigrate();
    sNewBadges = 0;
    Award(DAILY_BADGE_PRACTICE_PAYS);
}

void DailyRecords_BeginAttempt(u32 day)
{
    TryMigrate();
    sRecords->attemptDay = day + 1;
    sRecords->playedDay = day + 1;
}

// A scored battle that never reached its result (the game was reset mid-battle) is a loss.
// Returns TRUE if there was one. The hall's vars still describe that battle.
bool32 DailyRecords_FinishForfeit(void)
{
    struct DailyLastResult result = {0};
    u32 i;

    TryMigrate();
    if (sRecords->attemptDay == 0)
        return FALSE;
    result.entry.day = sRecords->attemptDay;
    result.entry.opponent = VarGet(VAR_DAILY_OPPONENT);
    result.entry.doubles = VarGet(VAR_DAILY_DOUBLES);
    for (i = 0; i < PARTY_SIZE; i++)
    {
        result.species[i] = GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_SPECIES);
        result.heldItems[i] = GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_HELD_ITEM);
        if (result.species[i] != SPECIES_NONE)
            result.entry.lost++;
    }
    DailyRecords_AddResult(&result);
    return TRUE;
}

bool32 DailyRecords_PlayedToday(u32 today)
{
    TryMigrate();
    return sRecords->playedDay == today + 1;
}

// The win streak, or 0 once a day has gone by without a win.
u32 DailyRecords_GetStreak(void)
{
    TryMigrate();
    if (sRecords->lastWinDay < GetToday())
        return 0;
    return sRecords->streak;
}

// Buffers the win streak into STR_VAR_1.
void DailyBattle_BufferStreak(void)
{
    ConvertIntToDecimalStringN(gStringVar1, DailyRecords_GetStreak(), STR_CONV_MODE_LEFT_ALIGN, 5);
}

// VAR_RESULT is TRUE if the last result earned another badge; its name goes in STR_VAR_1.
void DailyBattle_NextNewBadge(void)
{
    u32 i;

    gSpecialVar_Result = FALSE;
    for (i = 0; i < DAILY_BADGE_COUNT; i++)
    {
        if (sNewBadges & (1u << i))
        {
            sNewBadges &= ~(1u << i);
            StringCopy(gStringVar1, sDailyBadges[i].name);
            gSpecialVar_Result = TRUE;
            return;
        }
    }
}

// VAR_RESULT is TRUE if there's a scored result to show again.
void DailyBattle_HasLastResult(void)
{
    TryMigrate();
    gSpecialVar_Result = (sRecords->last.entry.day != 0);
}

const struct DailyLastResult *DailyRecords_GetLastResult(void)
{
    return &sRecords->last;
}

// The records screen. It reuses the Battle Factory menu background, like the results screen,
// with a light panel for the page.

enum
{
    PAGE_STATS,
    PAGE_HISTORY,
    PAGE_BADGES,
    PAGE_COUNT,
};

enum
{
    RECORDS_WIN_TITLE,
    RECORDS_WIN_PAGE,
    RECORDS_WIN_HINT,
    RECORDS_WIN_BODY,
};

#define PALNUM_PANEL 14
#define PALNUM_TEXT  15

#define HISTORY_ROWS 8
#define BADGE_ROWS   (DAILY_BADGE_COUNT / 2)

static EWRAM_DATA u8 sPage = 0;
static EWRAM_DATA u8 sHistoryScroll = 0;
static EWRAM_DATA u8 sBadgeCursor = 0;

static void Task_RecordsScreen(u8 taskId);

static const u16 sText_Pal[] = INCGFX_U16("graphics/battle_frontier/factory_screen/text.pal", ".gbapal");
static const u16 sPanel_Pal[] =
{
    RGB(0, 0, 0), RGB_WHITE, RGB(7, 7, 7), RGB(22, 22, 22),
    RGB(28, 4, 4), RGB(17, 17, 17), RGB(27, 27, 27), RGB(4, 12, 26),
    RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0),
    RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0), RGB(0, 0, 0),
};

static const u8 sTextColors_Title[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_RED, TEXT_COLOR_TRANSPARENT};
static const u8 sTextColors_Hint[] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_TRANSPARENT};
static const u8 sPanelColors_Normal[] = {1, 2, 3};
static const u8 sPanelColors_Red[] = {1, 4, 3};
static const u8 sPanelColors_Dim[] = {1, 5, 6};
static const u8 sPanelColors_Blue[] = {1, 7, 3};

static const u8 *const sPageNames[PAGE_COUNT] =
{
    [PAGE_STATS]   = COMPOUND_STRING("STATS"),
    [PAGE_HISTORY] = COMPOUND_STRING("HISTORY"),
    [PAGE_BADGES]  = COMPOUND_STRING("BADGES"),
};

static const u8 *const sMonthNames[12] =
{
    COMPOUND_STRING("JAN"), COMPOUND_STRING("FEB"), COMPOUND_STRING("MAR"), COMPOUND_STRING("APR"),
    COMPOUND_STRING("MAY"), COMPOUND_STRING("JUN"), COMPOUND_STRING("JUL"), COMPOUND_STRING("AUG"),
    COMPOUND_STRING("SEP"), COMPOUND_STRING("OCT"), COMPOUND_STRING("NOV"), COMPOUND_STRING("DEC"),
};

static const struct BgTemplate sRecords_BgTemplates[] =
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

static const struct WindowTemplate sRecords_WindowTemplates[] =
{
    [RECORDS_WIN_TITLE] = {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = 2,
        .width = 12,
        .height = 2,
        .paletteNum = PALNUM_TEXT,
        .baseBlock = 1,
    },
    [RECORDS_WIN_PAGE] = {
        .bg = 0,
        .tilemapLeft = 19,
        .tilemapTop = 2,
        .width = 11,
        .height = 2,
        .paletteNum = PALNUM_TEXT,
        .baseBlock = 25,
    },
    [RECORDS_WIN_HINT] = {
        .bg = 0,
        .tilemapLeft = 11,
        .tilemapTop = 0,
        .width = 19,
        .height = 2,
        .paletteNum = PALNUM_TEXT,
        .baseBlock = 47,
    },
    [RECORDS_WIN_BODY] = {
        .bg = 0,
        .tilemapLeft = 1,
        .tilemapTop = 5,
        .width = 28,
        .height = 13,
        .paletteNum = PALNUM_PANEL,
        .baseBlock = 85,
    },
    DUMMY_WIN_TEMPLATE,
};

static void PrintText(u32 windowId, const u8 *colors, u32 font, u32 x, u32 y, const u8 *text)
{
    AddTextPrinterParameterized3(windowId, font, x, y, colors, TEXT_SKIP_DRAW, text);
}

static void PrintRightAligned(u32 windowId, const u8 *colors, u32 font, u32 right, u32 y, const u8 *text)
{
    PrintText(windowId, colors, font, right - GetStringWidth(font, text, 0), y, text);
}

// Writes "OCT 22" for an RTC day count (day 1 is 1 January 2000).
static void FormatDay(u8 *dest, u32 dayCount)
{
    static const u8 sDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    u32 year = 0, month = 0, day = dayCount;

    while (day > (IsLeapYear(year) ? 366u : 365u))
    {
        day -= IsLeapYear(year) ? 366 : 365;
        year++;
    }
    for (month = 0; month < 11; month++)
    {
        u32 length = sDaysInMonth[month] + (month == 1 && IsLeapYear(year));

        if (day <= length)
            break;
        day -= length;
    }
    dest = StringCopy(dest, sMonthNames[month]);
    *dest++ = CHAR_SPACE;
    ConvertIntToDecimalStringN(dest, day, STR_CONV_MODE_LEFT_ALIGN, 2);
}

static u32 CountBadges(void)
{
    u32 i, count = 0;

    for (i = 0; i < DAILY_BADGE_COUNT; i++)
        count += (sRecords->badges >> i) & 1;
    return count;
}

static void PrintStat(u32 column, u32 row, const u8 *label, u32 value, bool32 percent)
{
    u32 x = column * 112;
    u8 *end;

    PrintText(RECORDS_WIN_BODY, sPanelColors_Normal, FONT_NORMAL, x + 4, row * 16 + 2, label);
    end = ConvertIntToDecimalStringN(gStringVar4, value, STR_CONV_MODE_LEFT_ALIGN, 5);
    if (percent)
        StringCopy(end, COMPOUND_STRING("%"));
    PrintRightAligned(RECORDS_WIN_BODY, sPanelColors_Blue, FONT_NORMAL, x + 106, row * 16 + 2, gStringVar4);
}

static void PrintStatsPage(void)
{
    const struct DailyHistoryEntry *last = &sRecords->last.entry;
    u32 today = GetToday();

    PrintStat(0, 0, COMPOUND_STRING("Played"), sRecords->played, FALSE);
    PrintStat(1, 0, COMPOUND_STRING("Wins"), sRecords->wins, FALSE);
    PrintStat(0, 1, COMPOUND_STRING("Win rate"), sRecords->played ? sRecords->wins * 100 / sRecords->played : 0, TRUE);
    PrintStat(1, 1, COMPOUND_STRING("Best score"), sRecords->bestScore, FALSE);
    PrintStat(0, 2, COMPOUND_STRING("Streak"), DailyRecords_GetStreak(), FALSE);
    PrintStat(1, 2, COMPOUND_STRING("Best streak"), sRecords->bestStreak, FALSE);
    PrintStat(0, 3, COMPOUND_STRING("Bosses beaten"), sRecords->bossWins, FALSE);
    PrintStat(1, 3, COMPOUND_STRING("Badges"), CountBadges(), FALSE);

    if (sRecords->playedDay != today + 1)
    {
        StringCopy(gStringVar4, COMPOUND_STRING("Today: not played yet. Good luck!"));
    }
    else if (last->day == today + 1)
    {
        ConvertIntToDecimalStringN(gStringVar1, last->score, STR_CONV_MODE_LEFT_ALIGN, 5);
        StringExpandPlaceholders(gStringVar4, last->won ? COMPOUND_STRING("Today: VICTORY, {STR_VAR_1} points.")
                                                        : COMPOUND_STRING("Today: DEFEAT, {STR_VAR_1} points."));
    }
    else
    {
        StringCopy(gStringVar4, COMPOUND_STRING("Today: played."));
    }
    PrintText(RECORDS_WIN_BODY, sPanelColors_Normal, FONT_NORMAL, 4, 4 * 16 + 18, gStringVar4);
}

static const struct DailyHistoryEntry *GetHistoryEntry(u32 newest)
{
    return &sRecords->history[(sRecords->historyHead + DAILY_HISTORY_COUNT - 1 - newest) % DAILY_HISTORY_COUNT];
}

static void PrintHistoryPage(void)
{
    u32 i;

    if (sRecords->historyCount == 0)
    {
        PrintText(RECORDS_WIN_BODY, sPanelColors_Normal, FONT_NORMAL, 4, 2, COMPOUND_STRING("No daily battles yet."));
        return;
    }
    for (i = 0; i < HISTORY_ROWS && sHistoryScroll + i < sRecords->historyCount; i++)
    {
        const struct DailyHistoryEntry *entry = GetHistoryEntry(sHistoryScroll + i);
        const struct Trainer *trainer = GetDailyTrainerById(entry->opponent, entry->doubles);
        u32 y = 2 + i * 12;
        u8 *end;

        FormatDay(gStringVar4, entry->day - 1);
        PrintText(RECORDS_WIN_BODY, sPanelColors_Normal, FONT_SMALL, 4, y, gStringVar4);
        PrintText(RECORDS_WIN_BODY, entry->won ? sPanelColors_Red : sPanelColors_Dim, FONT_SMALL, 38, y,
                  entry->won ? COMPOUND_STRING("WIN") : COMPOUND_STRING("LOSS"));
        ConvertIntToDecimalStringN(gStringVar4, entry->score, STR_CONV_MODE_LEFT_ALIGN, 5);
        PrintRightAligned(RECORDS_WIN_BODY, sPanelColors_Blue, FONT_SMALL, 86, y, gStringVar4);
        PrintText(RECORDS_WIN_BODY, sPanelColors_Dim, FONT_SMALL, 92, y,
                  entry->doubles ? COMPOUND_STRING("2v2") : COMPOUND_STRING("1v1"));
        end = StringCopy(gStringVar4, gTrainerClasses[trainer->trainerClass].name);
        *end++ = CHAR_SPACE;
        StringCopy(end, trainer->trainerName);
        PrintText(RECORDS_WIN_BODY, sPanelColors_Normal, FONT_SMALL, 110, y, gStringVar4);
    }
    if (sRecords->historyCount > HISTORY_ROWS)
    {
        if (sHistoryScroll > 0)
            PrintText(RECORDS_WIN_BODY, sPanelColors_Dim, FONT_SMALL, 214, 0, COMPOUND_STRING("{UP_ARROW}"));
        if (sHistoryScroll + HISTORY_ROWS < sRecords->historyCount)
            PrintText(RECORDS_WIN_BODY, sPanelColors_Dim, FONT_SMALL, 214, 92, COMPOUND_STRING("{DOWN_ARROW}"));
    }
}

static void PrintBadgesPage(void)
{
    u32 i;

    for (i = 0; i < DAILY_BADGE_COUNT; i++)
    {
        const u8 *colors;

        if (i == sBadgeCursor)
            colors = sPanelColors_Red;
        else if (sRecords->badges & (1u << i))
            colors = sPanelColors_Normal;
        else
            colors = sPanelColors_Dim;
        if (i == sBadgeCursor)
            PrintText(RECORDS_WIN_BODY, colors, FONT_SMALL, (i / BADGE_ROWS) * 112 + 2, 1 + (i % BADGE_ROWS) * 11, COMPOUND_STRING("{RIGHT_ARROW}"));
        PrintText(RECORDS_WIN_BODY, colors, FONT_SMALL, (i / BADGE_ROWS) * 112 + 10, 1 + (i % BADGE_ROWS) * 11, sDailyBadges[i].name);
        if (sRecords->badges & (1u << i))
            PrintRightAligned(RECORDS_WIN_BODY, sPanelColors_Blue, FONT_SMALL, (i / BADGE_ROWS) * 112 + 106, 1 + (i % BADGE_ROWS) * 11, COMPOUND_STRING("GOT"));
    }
    PrintText(RECORDS_WIN_BODY, sPanelColors_Normal, FONT_SMALL, 4, 92, sDailyBadges[sBadgeCursor].description);
}

static void PrintRecords(void)
{
    u32 i;

    for (i = 0; i <= RECORDS_WIN_HINT; i++)
        FillWindowPixelBuffer(i, PIXEL_FILL(0));
    FillWindowPixelBuffer(RECORDS_WIN_BODY, PIXEL_FILL(1));

    PrintText(RECORDS_WIN_TITLE, sTextColors_Title, FONT_NORMAL, 2, 1, COMPOUND_STRING("RECORDS"));
    if (sPage == PAGE_BADGES)
    {
        ConvertIntToDecimalStringN(gStringVar1, CountBadges(), STR_CONV_MODE_LEFT_ALIGN, 2);
        ConvertIntToDecimalStringN(gStringVar2, DAILY_BADGE_COUNT, STR_CONV_MODE_LEFT_ALIGN, 2);
        StringExpandPlaceholders(gStringVar4, COMPOUND_STRING("BADGES {STR_VAR_1}/{STR_VAR_2}"));
    }
    else
    {
        StringCopy(gStringVar4, sPageNames[sPage]);
    }
    PrintRightAligned(RECORDS_WIN_PAGE, sTextColors_Title, FONT_NORMAL, 86, 1, gStringVar4);
    PrintRightAligned(RECORDS_WIN_HINT, sTextColors_Hint, FONT_SMALL, 150, 2,
                      COMPOUND_STRING("{DPAD_LEFTRIGHT} Page   {B_BUTTON} Back"));

    switch (sPage)
    {
    case PAGE_STATS:
        PrintStatsPage();
        break;
    case PAGE_HISTORY:
        PrintHistoryPage();
        break;
    case PAGE_BADGES:
        PrintBadgesPage();
        break;
    }
    for (i = 0; i <= RECORDS_WIN_BODY; i++)
    {
        PutWindowTilemap(i);
        CopyWindowToVram(i, COPYWIN_FULL);
    }
}

static void CB2_RecordsScreen(void)
{
    AnimateSprites();
    BuildOamBuffer();
    RunTextPrinters();
    UpdatePaletteFade();
    RunTasks();
}

static void VBlankCB_RecordsScreen(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

#define tState data[0]

static void CB2_InitRecordsScreen(void)
{
    switch (gMain.state)
    {
    case 0:
        SetVBlankCallback(NULL);
        SetHBlankCallback(NULL);
        CpuFill32(0, (void *)VRAM, VRAM_SIZE);
        ResetBgsAndClearDma3BusyFlags(0);
        InitBgsFromTemplates(0, sRecords_BgTemplates, ARRAY_COUNT(sRecords_BgTemplates));
        InitWindows(sRecords_WindowTemplates);
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
        LoadPalette(sPanel_Pal, BG_PLTT_ID(PALNUM_PANEL), PLTT_SIZE_4BPP);
        LoadPalette(sText_Pal, BG_PLTT_ID(PALNUM_TEXT), PLTT_SIZEOF(4));
        gMain.state++;
        break;
    case 2:
        PrintRecords();
        gMain.state++;
        break;
    default:
        ShowBg(0);
        ShowBg(1);
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_BG0_ON | DISPCNT_BG1_ON | DISPCNT_OBJ_1D_MAP);
        SetVBlankCallback(VBlankCB_RecordsScreen);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        gTasks[CreateTask(Task_RecordsScreen, 0)].tState = 1;
        SetMainCallback2(CB2_RecordsScreen);
        break;
    }
}

// Opens the records screen from the hall.
void DailyBattle_ShowRecords(void)
{
    TryMigrate();
    sPage = PAGE_STATS;
    sHistoryScroll = 0;
    sBadgeCursor = 0;
    LockPlayerFieldControls();
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    gTasks[CreateTask(Task_RecordsScreen, 10)].tState = 0;
}

static bool32 HandleInput(void)
{
    if (JOY_NEW(DPAD_RIGHT | R_BUTTON))
    {
        sPage = (sPage + 1) % PAGE_COUNT;
    }
    else if (JOY_NEW(DPAD_LEFT | L_BUTTON))
    {
        sPage = (sPage + PAGE_COUNT - 1) % PAGE_COUNT;
    }
    else if (JOY_NEW(DPAD_DOWN))
    {
        if (sPage == PAGE_HISTORY && sHistoryScroll + HISTORY_ROWS < sRecords->historyCount)
            sHistoryScroll++;
        else if (sPage == PAGE_BADGES)
            sBadgeCursor = (sBadgeCursor + 1) % DAILY_BADGE_COUNT;
        else
            return FALSE;
    }
    else if (JOY_NEW(DPAD_UP))
    {
        if (sPage == PAGE_HISTORY && sHistoryScroll > 0)
            sHistoryScroll--;
        else if (sPage == PAGE_BADGES)
            sBadgeCursor = (sBadgeCursor + DAILY_BADGE_COUNT - 1) % DAILY_BADGE_COUNT;
        else
            return FALSE;
    }
    else
    {
        return FALSE;
    }
    PlaySE(SE_SELECT);
    return TRUE;
}

static void Task_RecordsScreen(u8 taskId)
{
    switch (gTasks[taskId].tState)
    {
    case 0: // Overworld fading out
        if (!gPaletteFade.active)
        {
            DestroyTask(taskId);
            CleanupOverworldWindowsAndTilemaps();
            gMain.state = 0;
            SetMainCallback2(CB2_InitRecordsScreen);
        }
        break;
    case 1:
        if (gPaletteFade.active)
            break;
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
            gTasks[taskId].tState++;
        }
        else if (HandleInput())
        {
            PrintRecords();
        }
        break;
    case 2:
        if (!gPaletteFade.active)
        {
            FreeAllWindowBuffers();
            SetMainCallback2(CB2_ReturnToFieldContinueScript);
            DestroyTask(taskId);
        }
        break;
    }
}
