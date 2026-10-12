@ The Daily Battle Hall: draft six POKéMON, then face today's monotype leader.
@ The C side lives in src/daily_battle.c.

DailyBattleHall_MapScripts::
	map_script MAP_SCRIPT_ON_FRAME_TABLE, DailyBattleHall_OnFrame
	.byte 0

DailyBattleHall_OnFrame:
	map_script_2 VAR_DAILY_INTRO_DONE, 0, DailyBattleHall_EventScript_Intro
	.2byte 0

DailyBattleHall_EventScript_Intro::
	lockall
	setvar VAR_DAILY_INTRO_DONE, 1
	turnobject LOCALID_PLAYER, DIR_NORTH
	msgbox DailyBattleHall_Text_Welcome, MSGBOX_DEFAULT
	call DailyBattleHall_EventScript_Challenge
	releaseall
	end

DailyBattleHall_EventScript_Host::
	lock
	faceplayer
	call DailyBattleHall_EventScript_Challenge
	release
	end

DailyBattleHall_EventScript_Challenge::
	special DailyBattle_Setup
	goto_if_eq VAR_RESULT, TRUE, DailyBattleHall_EventScript_AlreadyWon
	msgbox DailyBattleHall_Text_TodaysOpponent, MSGBOX_DEFAULT
	special DailyDraft_HasTodaysTeam
	goto_if_eq VAR_RESULT, FALSE, DailyBattleHall_EventScript_Draft
	msgbox DailyBattleHall_Text_KeepTeam, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, YES, DailyBattleHall_EventScript_Battle
	goto DailyBattleHall_EventScript_Draft

DailyBattleHall_EventScript_AlreadyWon::
	special DailyBattle_BufferStreak
	msgbox DailyBattleHall_Text_AlreadyWon, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, DailyBattleHall_EventScript_ComeBackTomorrow
	special DailyDraft_HasTodaysTeam
	goto_if_eq VAR_RESULT, TRUE, DailyBattleHall_EventScript_Battle
	goto DailyBattleHall_EventScript_Draft

DailyBattleHall_EventScript_ComeBackTomorrow::
	msgbox DailyBattleHall_Text_ComeBackTomorrow, MSGBOX_DEFAULT
	return

DailyBattleHall_EventScript_Draft::
	msgbox DailyBattleHall_Text_DraftRules, MSGBOX_DEFAULT
	special DailyDraft_Begin
DailyBattleHall_EventScript_DraftRound::
	special DailyDraft_BufferRound
	goto_if_eq VAR_RESULT, TRUE, DailyBattleHall_EventScript_DraftDone
	message DailyBattleHall_Text_Round
	waitmessage
	special DailyDraft_PrepareChoices
	dynmultichoice 0, 0, TRUE, 4, 0, DYN_MULTICHOICE_CB_NONE, DailyBattleHall_Text_Choice1, DailyBattleHall_Text_Choice2, DailyBattleHall_Text_Choice3, DailyBattleHall_Text_CheckStats
	goto_if_eq VAR_RESULT, 3, DailyBattleHall_EventScript_CheckStats
	copyvar VAR_0x8005, VAR_RESULT
	special DailyDraft_BufferChoice
	msgbox DailyBattleHall_Text_ConfirmPick, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, DailyBattleHall_EventScript_DraftRound
	special DailyDraft_Pick
	playse SE_SELECT
	goto DailyBattleHall_EventScript_DraftRound

DailyBattleHall_EventScript_CheckStats::
	closemessage
	setvar VAR_0x8005, 0
	special DailyDraft_ShowSummary
	goto DailyBattleHall_EventScript_DraftRound

DailyBattleHall_EventScript_DraftDone::
	msgbox DailyBattleHall_Text_TeamReady, MSGBOX_DEFAULT
DailyBattleHall_EventScript_Battle::
	msgbox DailyBattleHall_Text_ReadyToBattle, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, DailyBattleHall_EventScript_NotReady
	special HealPlayerParty
	setflag FLAG_DAILY_NO_WHITEOUT
	cleartrainerflag TRAINER_DAILY
	special DailyBattle_Setup
	msgbox DailyBattleHall_Text_HereTheyCome, MSGBOX_DEFAULT
	closemessage
	trainerbattle_no_intro TRAINER_DAILY, DailyBattleHall_Text_OpponentDefeated
	specialvar VAR_RESULT, GetBattleOutcome
	special HealPlayerParty
	goto_if_eq VAR_RESULT, B_OUTCOME_WON, DailyBattleHall_EventScript_Won
	msgbox DailyBattleHall_Text_Lost, MSGBOX_DEFAULT
	return

DailyBattleHall_EventScript_NotReady::
	msgbox DailyBattleHall_Text_NotReady, MSGBOX_DEFAULT
	return

DailyBattleHall_EventScript_Won::
	special DailyBattle_RecordWin
	playfanfare MUS_OBTAIN_BADGE
	msgbox DailyBattleHall_Text_Won, MSGBOX_DEFAULT
	waitfanfare
	return

DailyBattleHall_Text_Welcome:
	.string "Welcome to the DAILY BATTLE HALL!\p"
	.string "Every day a new GYM LEADER, ELITE FOUR\n"
	.string "member or CHAMPION from every region\l"
	.string "waits here with a monotype team.\p"
	.string "Everyone in the world faces the same\n"
	.string "opponent and the same draft each day.$"

DailyBattleHall_Text_TodaysOpponent:
	.string "Today's opponent is {STR_VAR_2}\n"
	.string "{STR_VAR_1}, master of {STR_VAR_3}-type\l"
	.string "POKéMON!$"

DailyBattleHall_Text_KeepTeam:
	.string "You already drafted a team today.\n"
	.string "Battle with it again?$"

DailyBattleHall_Text_DraftRules:
	.string "Time to draft! You'll get six rounds.\n"
	.string "Each round, pick one of three POKéMON.\p"
	.string "Every POKéMON comes with a real\n"
	.string "competitive build. Choose CHECK STATS\l"
	.string "to see moves, ability, item and stats.$"

DailyBattleHall_Text_Round:
	.string "Round {STR_VAR_1} of 6. Who do you pick?$"

DailyBattleHall_Text_Choice1:
	.string "{STR_VAR_1}$"

DailyBattleHall_Text_Choice2:
	.string "{STR_VAR_2}$"

DailyBattleHall_Text_Choice3:
	.string "{STR_VAR_3}$"

DailyBattleHall_Text_CheckStats:
	.string "CHECK STATS$"

DailyBattleHall_Text_ConfirmPick:
	.string "Add {STR_VAR_1} to your team?$"

DailyBattleHall_Text_TeamReady:
	.string "Your team of six is ready!$"

DailyBattleHall_Text_ReadyToBattle:
	.string "Are you ready to battle?$"

DailyBattleHall_Text_NotReady:
	.string "Talk to me whenever you're ready.$"

DailyBattleHall_Text_HereTheyCome:
	.string "{STR_VAR_2} {STR_VAR_1} accepts your\n"
	.string "challenge! Good luck!$"

DailyBattleHall_Text_OpponentDefeated:
	.string "Incredible... You've beaten me fair\n"
	.string "and square!$"

DailyBattleHall_Text_Lost:
	.string "So close! You can try again as many\n"
	.string "times as you like today.$"

DailyBattleHall_Text_Won:
	.string "You won today's DAILY BATTLE!\n"
	.string "Win streak: {STR_VAR_1}\p"
	.string "A new opponent arrives tomorrow.$"

DailyBattleHall_Text_AlreadyWon:
	.string "You already beat today's opponent,\n"
	.string "{STR_VAR_2} {STR_VAR_1}! Win streak: {STR_VAR_3}\p"
	.string "Want a rematch just for fun?$"

DailyBattleHall_Text_ComeBackTomorrow:
	.string "Come back tomorrow for a new\n"
	.string "opponent!$"
