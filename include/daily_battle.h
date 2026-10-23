#ifndef GUARD_DAILY_BATTLE_H
#define GUARD_DAILY_BATTLE_H

struct Trainer;

const struct Trainer *GetDailyTrainer(void);
const struct Trainer *GetDailyTrainerById(u32 id, bool32 doubles);
u32 DailyBattle_GetOpponentForDay(u32 day);
void DailyBattle_InitNewGame(void);

// src/daily_records.c
void DailyRecords_Init(void);
bool32 DailyRecords_FinishForfeit(void);
bool32 DailyRecords_PlayedToday(u32 today);
void DailyRecords_BeginAttempt(u32 day);
bool32 DailyRecords_AddResult(const struct DailyLastResult *result);
void DailyRecords_AwardPracticeWin(void);
u32 DailyRecords_GetStreak(void);
const struct DailyLastResult *DailyRecords_GetLastResult(void);

#endif // GUARD_DAILY_BATTLE_H
