#ifndef GUARD_CONSTANTS_SOULLIKE_RUN_H
#define GUARD_CONSTANTS_SOULLIKE_RUN_H

// What SoullikeRun_Next asks the run script to do next.
#define SOULLIKE_RUN_ACTION_PICK     0  // one of the players picks from a list
#define SOULLIKE_RUN_ACTION_BATTLE   1  // fight the co-op battle the run set up
#define SOULLIKE_RUN_ACTION_MESSAGE  2  // show gStringVar4, then ask again
#define SOULLIKE_RUN_ACTION_WON      3  // the run is over and won; gStringVar4 says so
#define SOULLIKE_RUN_ACTION_LOST     4  // the run is over and lost; gStringVar4 says so

#endif // GUARD_CONSTANTS_SOULLIKE_RUN_H
