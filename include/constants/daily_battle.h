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
#define VAR_DAILY_BEST_SCORE  VAR_UNUSED_0x40A1
#define VAR_DAILY_BEST_DAY    VAR_UNUSED_0x40A8
#define VAR_DAILY_DOUBLES     VAR_UNUSED_0x40B8 // TRUE when today's battle is a double battle

// Today's best win, so the results screen can be shown again from the hall. Kept for the
// day in VAR_DAILY_WON_DAY. Stats: turns | knockouts << 8 | lost << 12. Each team member:
// species low byte << 8 | HP percent (0 fainted, 0xFF empty slot), in party order.
#define VAR_DAILY_RESULT_SCORE VAR_UNUSED_0x40BB
#define VAR_DAILY_RESULT_STATS VAR_UNUSED_0x40DB
#define VAR_DAILY_RESULT_MON_0 VAR_UNUSED_0x40DC
#define VAR_DAILY_RESULT_MON_1 VAR_UNUSED_0x40E5
#define VAR_DAILY_RESULT_MON_2 VAR_UNUSED_0x40F7
#define VAR_DAILY_RESULT_MON_3 VAR_UNUSED_0x40F8
#define VAR_DAILY_RESULT_MON_4 VAR_UNUSED_0x40F9
#define VAR_DAILY_RESULT_MON_5 VAR_UNUSED_0x40FA

// One day in DOUBLES_DAY_PERIOD is a double battle, picked by the daily seed.
#define DOUBLES_DAY_PERIOD 3

// VAR_FRONTIER_FACILITY value while the party menu picks the daily battle's leads.
#define FACILITY_DAILY_LEADS 0xFD

#define FLAG_DAILY_NO_WHITEOUT FLAG_UNUSED_0x020

#endif // GUARD_CONSTANTS_DAILY_BATTLE_H
