#ifndef GUARD_DAILY_BATTLE_H
#define GUARD_DAILY_BATTLE_H

struct Trainer;

const struct Trainer *GetDailyTrainer(void);
void DailyBattle_InitNewGame(void);

#endif // GUARD_DAILY_BATTLE_H
