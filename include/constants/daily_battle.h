#ifndef GUARD_CONSTANTS_DAILY_BATTLE_H
#define GUARD_CONSTANTS_DAILY_BATTLE_H

#define DRAFT_ROUNDS  6
#define DRAFT_CHOICES 3

// Save vars used by the daily challenge. Days are stored as (RTC day count + 1) so 0 means "never".
#define VAR_DAILY_INTRO_DONE  VAR_UNUSED_0x404E
#define VAR_DAILY_OPPONENT    VAR_UNUSED_0x4083
#define VAR_DAILY_DRAFT_ROUND VAR_UNUSED_0x408B
#define VAR_DAILY_DRAFT_DAY   VAR_UNUSED_0x4091
#define VAR_DAILY_WON_DAY     VAR_UNUSED_0x409B
#define VAR_DAILY_STREAK      VAR_UNUSED_0x409D
#define VAR_DAILY_DAY         VAR_UNUSED_0x40A8 // The day being played: today, or a past day in practice
#define VAR_DAILY_DOUBLES     VAR_UNUSED_0x40B8 // TRUE when the current battle is a double battle
#define VAR_DAILY_MODE        VAR_UNUSED_0x40FB // DAILY_MODE_*
#define VAR_DAILY_SEED        VAR_UNUSED_0x40FC // Draft seed: the day for a daily, DAILY_SEED_RANDOM | n in practice

// The scored daily battle, or an unscored practice battle after it.
#define DAILY_MODE_DAILY    0
#define DAILY_MODE_PRACTICE 1

// Practice drafts use seeds no real day reaches.
#define DAILY_SEED_RANDOM 0x8000

// VAR_0x8004 for DailyBattle_SetupPractice: today's opponent, a random one, or the daily from
// 1-7 days ago (PRACTICE_PAST_DAY + days ago).
#define PRACTICE_TODAY     0
#define PRACTICE_RANDOM    1
#define PRACTICE_PAST_DAY  10

// Badges, as bits of the records' badges field.
#define DAILY_BADGE_FIRST_WIN        0
#define DAILY_BADGE_DOUBLE_TROUBLE   1
#define DAILY_BADGE_FLAWLESS         2
#define DAILY_BADGE_LAST_STAND       3
#define DAILY_BADGE_SPEEDSTER        4
#define DAILY_BADGE_HIGH_ROLLER      5
#define DAILY_BADGE_ON_A_ROLL        6
#define DAILY_BADGE_WEEK_WARRIOR     7
#define DAILY_BADGE_UNSTOPPABLE      8
#define DAILY_BADGE_BOSS_SLAYER      9
#define DAILY_BADGE_LEGEND_HUNTER    10
#define DAILY_BADGE_TITLE_TAKER      11
#define DAILY_BADGE_REGULAR          12
#define DAILY_BADGE_VETERAN          13
#define DAILY_BADGE_CENTURION        14
#define DAILY_BADGE_PRACTICE_PAYS    15
#define DAILY_BADGE_COUNT            16

// One day in DOUBLES_DAY_PERIOD is a double battle, picked by the daily seed.
#define DOUBLES_DAY_PERIOD 3

// VAR_FRONTIER_FACILITY value while the party menu picks the daily battle's leads.
#define FACILITY_DAILY_LEADS 0xFD

#define FLAG_DAILY_NO_WHITEOUT FLAG_UNUSED_0x020

#endif // GUARD_CONSTANTS_DAILY_BATTLE_H
