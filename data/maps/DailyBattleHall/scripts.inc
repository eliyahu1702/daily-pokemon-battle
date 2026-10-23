@ The Daily Battle Hall: draft six POKéMON, then take on today's opponent. The daily is one
@ scored attempt; after it, practice battles are open. The C side lives in src/daily_battle.c,
@ src/daily_results_screen.c and src/daily_records.c.

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
	call DailyBattleHall_EventScript_Menu
	releaseall
	end

DailyBattleHall_EventScript_Host::
	lock
	faceplayer
	call DailyBattleHall_EventScript_Menu
	release
	end

DailyBattleHall_EventScript_Menu::
	special DailyBattle_StartDaily
	call_if_eq VAR_0x8004, TRUE, DailyBattleHall_EventScript_Forfeited
	goto_if_eq VAR_RESULT, TRUE, DailyBattleHall_EventScript_PlayedMenu
	message DailyBattleHall_Text_WhatToDo
	waitmessage
	dynmultipush DailyBattleHall_Text_OptionTodaysBattle, 0
	dynmultipush DailyBattleHall_Text_OptionRecords, 1
	dynmultipush DailyBattleHall_Text_OptionBye, 2
	dynmultistack 0, 0, FALSE, 3, FALSE, 0, DYN_MULTICHOICE_CB_NONE
	closemessage
	goto_if_eq VAR_RESULT, 0, DailyBattleHall_EventScript_Challenge
	goto_if_eq VAR_RESULT, 1, DailyBattleHall_EventScript_RecordsThenMenu
	return

DailyBattleHall_EventScript_Forfeited::
	msgbox DailyBattleHall_Text_Forfeited, MSGBOX_DEFAULT
	return

DailyBattleHall_EventScript_RecordsThenMenu::
	special DailyBattle_ShowRecords
	goto DailyBattleHall_EventScript_Menu

@ Today's one attempt is used: results, practice and records.
DailyBattleHall_EventScript_PlayedMenu::
	special DailyBattle_BufferStreak
	msgbox DailyBattleHall_Text_AlreadyPlayed, MSGBOX_DEFAULT
DailyBattleHall_EventScript_PlayedMenuNoIntro::
	message DailyBattleHall_Text_WhatToDo
	waitmessage
	special DailyBattle_HasLastResult
	goto_if_eq VAR_RESULT, FALSE, DailyBattleHall_EventScript_PlayedMenuNoResults
	dynmultipush DailyBattleHall_Text_OptionResults, 0
DailyBattleHall_EventScript_PlayedMenuNoResults::
	dynmultipush DailyBattleHall_Text_OptionPractice, 1
	dynmultipush DailyBattleHall_Text_OptionRecords, 2
	dynmultipush DailyBattleHall_Text_OptionBye, 3
	dynmultistack 0, 0, FALSE, 4, FALSE, 0, DYN_MULTICHOICE_CB_NONE
	closemessage
	goto_if_eq VAR_RESULT, 0, DailyBattleHall_EventScript_ShowLastResult
	goto_if_eq VAR_RESULT, 1, DailyBattleHall_EventScript_Practice
	goto_if_eq VAR_RESULT, 2, DailyBattleHall_EventScript_PlayedRecords
	msgbox DailyBattleHall_Text_ComeBackTomorrow, MSGBOX_DEFAULT
	return

DailyBattleHall_EventScript_ShowLastResult::
	special DailyBattle_ShowLastResult
	goto DailyBattleHall_EventScript_PlayedMenuNoIntro

DailyBattleHall_EventScript_PlayedRecords::
	special DailyBattle_ShowRecords
	goto DailyBattleHall_EventScript_PlayedMenuNoIntro

DailyBattleHall_EventScript_Practice::
	msgbox DailyBattleHall_Text_PracticeRules, MSGBOX_DEFAULT
	message DailyBattleHall_Text_PracticeWho
	waitmessage
	dynmultipush DailyBattleHall_Text_OptionTodaysOpponent, PRACTICE_TODAY
	dynmultipush DailyBattleHall_Text_OptionRandomOpponent, PRACTICE_RANDOM
	dynmultipush DailyBattleHall_Text_OptionPastDaily, 2
	dynmultipush DailyBattleHall_Text_OptionCancel, 3
	dynmultistack 0, 0, FALSE, 4, FALSE, 0, DYN_MULTICHOICE_CB_NONE
	closemessage
	goto_if_eq VAR_RESULT, 2, DailyBattleHall_EventScript_PracticePastDay
	goto_if_gt VAR_RESULT, 2, DailyBattleHall_EventScript_PlayedMenuNoIntro
	copyvar VAR_0x8004, VAR_RESULT
	goto DailyBattleHall_EventScript_StartPractice

DailyBattleHall_EventScript_PracticePastDay::
	message DailyBattleHall_Text_PracticeWhichDay
	waitmessage
	dynmultipush DailyBattleHall_Text_OptionDaysAgo1, PRACTICE_PAST_DAY + 1
	dynmultipush DailyBattleHall_Text_OptionDaysAgo2, PRACTICE_PAST_DAY + 2
	dynmultipush DailyBattleHall_Text_OptionDaysAgo3, PRACTICE_PAST_DAY + 3
	dynmultipush DailyBattleHall_Text_OptionDaysAgo4, PRACTICE_PAST_DAY + 4
	dynmultipush DailyBattleHall_Text_OptionDaysAgo5, PRACTICE_PAST_DAY + 5
	dynmultipush DailyBattleHall_Text_OptionDaysAgo6, PRACTICE_PAST_DAY + 6
	dynmultipush DailyBattleHall_Text_OptionDaysAgo7, PRACTICE_PAST_DAY + 7
	dynmultipush DailyBattleHall_Text_OptionCancel, 0
	dynmultistack 0, 0, FALSE, 5, FALSE, PRACTICE_PAST_DAY + 1, DYN_MULTICHOICE_CB_NONE
	closemessage
	goto_if_lt VAR_RESULT, PRACTICE_PAST_DAY + 1, DailyBattleHall_EventScript_PlayedMenuNoIntro
	goto_if_gt VAR_RESULT, PRACTICE_PAST_DAY + 7, DailyBattleHall_EventScript_PlayedMenuNoIntro
	copyvar VAR_0x8004, VAR_RESULT
DailyBattleHall_EventScript_StartPractice::
	special DailyBattle_SetupPractice
	msgbox DailyBattleHall_Text_PracticeOpponent, MSGBOX_DEFAULT
	call DailyBattleHall_EventScript_AnnounceBoss
	call DailyBattleHall_EventScript_AnnounceFormat
	goto DailyBattleHall_EventScript_Draft

@ Today's scored daily.
DailyBattleHall_EventScript_Challenge::
	msgbox DailyBattleHall_Text_TodaysOpponent, MSGBOX_DEFAULT
	call DailyBattleHall_EventScript_AnnounceBoss
	call DailyBattleHall_EventScript_AnnounceFormat
	goto DailyBattleHall_EventScript_Draft

@ A Sunday boss shows off their legendary before the draft.
DailyBattleHall_EventScript_AnnounceBoss::
	special DailyBattle_GetBoss
	goto_if_eq VAR_RESULT, FALSE, DailyBattleHall_EventScript_Return
	playse SE_PIN
	showmonpic VAR_0x8004, 10, 3
	playmoncry VAR_0x8004, CRY_MODE_NORMAL
	msgbox DailyBattleHall_Text_BossLegend, MSGBOX_DEFAULT
	waitmoncry
	hidemonpic
	special DailyBattle_BufferOpponent
DailyBattleHall_EventScript_Return::
	return

DailyBattleHall_EventScript_AnnounceFormat::
	special DailyBattle_IsDoubles
	goto_if_eq VAR_RESULT, TRUE, DailyBattleHall_EventScript_AnnounceDoubles
	msgbox DailyBattleHall_Text_SinglesDay, MSGBOX_DEFAULT
	return

DailyBattleHall_EventScript_AnnounceDoubles::
	msgbox DailyBattleHall_Text_DoublesDay, MSGBOX_DEFAULT
	return

DailyBattleHall_EventScript_ComeBackTomorrow::
	msgbox DailyBattleHall_Text_ComeBackTomorrow, MSGBOX_DEFAULT
	return

DailyBattleHall_EventScript_Draft::
	special DailyDraft_Prepare
	goto_if_eq VAR_RESULT, 2, DailyBattleHall_EventScript_Battle
	goto_if_eq VAR_RESULT, 1, DailyBattleHall_EventScript_ResumeDraft
	special DailyBattle_IsPractice
	goto_if_eq VAR_RESULT, TRUE, DailyBattleHall_EventScript_PracticeDraftRules
	msgbox DailyBattleHall_Text_DraftRules, MSGBOX_DEFAULT
	message DailyBattleHall_Text_SavingDraft
	waitmessage
	setvar VAR_0x8004, FALSE
	special DailyBattle_SaveIfDaily
	closemessage
	goto DailyBattleHall_EventScript_DraftRound
DailyBattleHall_EventScript_PracticeDraftRules::
	msgbox DailyBattleHall_Text_PracticeDraftRules, MSGBOX_DEFAULT
	goto DailyBattleHall_EventScript_DraftRound
DailyBattleHall_EventScript_ResumeDraft::
	msgbox DailyBattleHall_Text_ResumeDraft, MSGBOX_DEFAULT
DailyBattleHall_EventScript_DraftRound::
	special DailyDraft_BufferRound
	goto_if_eq VAR_RESULT, TRUE, DailyBattleHall_EventScript_DraftDone
	special DailyDraft_PrepareChoices
	special DailyDraft_OpenPicker
	special DailyDraft_Pick
	goto DailyBattleHall_EventScript_DraftRound

DailyBattleHall_EventScript_DraftDone::
	setvar VAR_0x8004, TRUE
	special DailyBattle_SaveIfDaily
	msgbox DailyBattleHall_Text_TeamReady, MSGBOX_DEFAULT
DailyBattleHall_EventScript_Battle::
	special DailyBattle_IsPractice
	goto_if_eq VAR_RESULT, TRUE, DailyBattleHall_EventScript_AskPracticeReady
	msgbox DailyBattleHall_Text_ReadyToBattle, MSGBOX_YESNO
	goto DailyBattleHall_EventScript_CheckReady
DailyBattleHall_EventScript_AskPracticeReady::
	msgbox DailyBattleHall_Text_ReadyToPractice, MSGBOX_YESNO
DailyBattleHall_EventScript_CheckReady::
	goto_if_eq VAR_RESULT, NO, DailyBattleHall_EventScript_NotReady
	special HealPlayerParty
	setflag FLAG_DAILY_NO_WHITEOUT
	cleartrainerflag TRAINER_DAILY
	special DailyBattle_IsDoubles
	goto_if_eq VAR_RESULT, TRUE, DailyBattleHall_EventScript_ChooseTwoLeads
	msgbox DailyBattleHall_Text_ChooseLead, MSGBOX_DEFAULT
	goto DailyBattleHall_EventScript_ChooseLeads
DailyBattleHall_EventScript_ChooseTwoLeads::
	msgbox DailyBattleHall_Text_ChooseTwoLeads, MSGBOX_DEFAULT
DailyBattleHall_EventScript_ChooseLeads::
	closemessage
	special DailyBattle_ChooseLeads
	special DailyBattle_BeginAttempt
	special DailyBattle_BufferOpponent
	msgbox DailyBattleHall_Text_HereTheyCome, MSGBOX_DEFAULT
	closemessage
	trainerbattle_no_intro TRAINER_DAILY, DailyBattleHall_Text_OpponentDefeated
	special DailyBattle_RecordResult
	copyvar VAR_0x8006, VAR_RESULT
	special DailyBattle_ShowResults
	special HealPlayerParty
	special DailyBattle_IsPractice
	goto_if_eq VAR_RESULT, TRUE, DailyBattleHall_EventScript_PracticeOver
	goto_if_eq VAR_0x8006, TRUE, DailyBattleHall_EventScript_Won
	msgbox DailyBattleHall_Text_Lost, MSGBOX_DEFAULT
	goto DailyBattleHall_EventScript_NewBadges

DailyBattleHall_EventScript_PracticeOver::
	msgbox DailyBattleHall_Text_PracticeOver, MSGBOX_DEFAULT
	goto DailyBattleHall_EventScript_NewBadges

DailyBattleHall_EventScript_NotReady::
	msgbox DailyBattleHall_Text_NotReady, MSGBOX_DEFAULT
	return

DailyBattleHall_EventScript_Won::
	special DailyBattle_BufferStreak
	playfanfare MUS_OBTAIN_BADGE
	msgbox DailyBattleHall_Text_Won, MSGBOX_DEFAULT
	waitfanfare
DailyBattleHall_EventScript_NewBadges::
	special DailyBattle_NextNewBadge
	goto_if_eq VAR_RESULT, FALSE, DailyBattleHall_EventScript_Return
	playfanfare MUS_OBTAIN_ITEM
	msgbox DailyBattleHall_Text_NewBadge, MSGBOX_DEFAULT
	waitfanfare
	goto DailyBattleHall_EventScript_NewBadges

DailyBattleHall_Text_Welcome:
	.string "Welcome to the DAILY BATTLE HALL!\p"
	.string "Every day brings a new challenger:\n"
	.string "GYM LEADERS, ELITE FOUR, CHAMPIONS,\l"
	.string "villains and Battle Tower aces!\p"
	.string "Everyone in the world faces the same\n"
	.string "opponent and the same draft each day.\p"
	.string "You get one shot a day, so make\n"
	.string "it count!$"

DailyBattleHall_Text_TodaysOpponent:
	.string "Today's opponent is {STR_VAR_2}\n"
	.string "{STR_VAR_1}!\p"
	.string "{STR_VAR_3}$"

DailyBattleHall_Text_SinglesDay:
	.string "Today is a SINGLE BATTLE, six on six.$"

DailyBattleHall_Text_DoublesDay:
	.string "Today is a DOUBLE BATTLE!\n"
	.string "Two POKéMON on each side at once.\p"
	.string "The draft brings sets built for\n"
	.string "doubles: Protect, Fake Out, spread\l"
	.string "moves and support.$"

DailyBattleHall_Text_ChooseLead:
	.string "Pick the POKéMON that leads off.\n"
	.string "The rest follow in party order.$"

DailyBattleHall_Text_ChooseTwoLeads:
	.string "Pick the two POKéMON that lead off.\n"
	.string "The rest follow in party order.$"

DailyBattleHall_Text_DraftRules:
	.string "Time to draft! You'll get six rounds.\n"
	.string "Each round, pick one of three POKéMON.\p"
	.string "Every POKéMON comes with a real\n"
	.string "competitive build. Choose SUMMARY\l"
	.string "to see its moves and stats.\p"
	.string "Choose carefully: your picks are\n"
	.string "final!$"

DailyBattleHall_Text_TeamReady:
	.string "Your team of six is ready!$"

DailyBattleHall_Text_ReadyToBattle:
	.string "You only get one try at today's\n"
	.string "battle. Are you ready?$"

DailyBattleHall_Text_ReadyToPractice:
	.string "Ready for your practice battle?$"

DailyBattleHall_Text_NotReady:
	.string "Talk to me whenever you're ready.$"

DailyBattleHall_Text_HereTheyCome:
	.string "{STR_VAR_2} {STR_VAR_1} accepts your\n"
	.string "challenge! Good luck!$"

DailyBattleHall_Text_OpponentDefeated:
	.string "Incredible... You've beaten me fair\n"
	.string "and square!$"

DailyBattleHall_Text_Lost:
	.string "So close! That was your shot for\n"
	.string "today. A new opponent arrives\l"
	.string "tomorrow.\p"
	.string "Until then, practice battles are\n"
	.string "open. Talk to me any time!$"

DailyBattleHall_Text_Won:
	.string "You won today's DAILY BATTLE!\n"
	.string "Win streak: {STR_VAR_1}\p"
	.string "A new opponent arrives tomorrow.$"

DailyBattleHall_Text_AlreadyPlayed:
	.string "You've had your shot at today's\n"
	.string "battle. Win streak: {STR_VAR_1}$"

DailyBattleHall_Text_Forfeited:
	.string "Your last daily battle didn't finish,\n"
	.string "so it counts as a loss.$"

DailyBattleHall_Text_WhatToDo:
	.string "What would you like to do?$"

DailyBattleHall_Text_OptionTodaysBattle:
	.string "TODAY'S BATTLE$"

DailyBattleHall_Text_OptionResults:
	.string "MY RESULTS$"

DailyBattleHall_Text_OptionPractice:
	.string "PRACTICE$"

DailyBattleHall_Text_OptionRecords:
	.string "RECORDS$"

DailyBattleHall_Text_OptionBye:
	.string "BYE$"

DailyBattleHall_Text_OptionCancel:
	.string "CANCEL$"

DailyBattleHall_Text_OptionTodaysOpponent:
	.string "TODAY'S OPPONENT$"

DailyBattleHall_Text_OptionRandomOpponent:
	.string "RANDOM OPPONENT$"

DailyBattleHall_Text_OptionPastDaily:
	.string "A PAST DAILY$"

DailyBattleHall_Text_OptionDaysAgo1:
	.string "YESTERDAY$"

DailyBattleHall_Text_OptionDaysAgo2:
	.string "2 DAYS AGO$"

DailyBattleHall_Text_OptionDaysAgo3:
	.string "3 DAYS AGO$"

DailyBattleHall_Text_OptionDaysAgo4:
	.string "4 DAYS AGO$"

DailyBattleHall_Text_OptionDaysAgo5:
	.string "5 DAYS AGO$"

DailyBattleHall_Text_OptionDaysAgo6:
	.string "6 DAYS AGO$"

DailyBattleHall_Text_OptionDaysAgo7:
	.string "7 DAYS AGO$"

DailyBattleHall_Text_PracticeRules:
	.string "Practice battles are just for fun.\n"
	.string "They don't count for your records.\p"
	.string "You draft a new team every time.$"

DailyBattleHall_Text_PracticeWho:
	.string "Who do you want to face?$"

DailyBattleHall_Text_PracticeWhichDay:
	.string "Which day? You'll get that day's\n"
	.string "draft too.$"

DailyBattleHall_Text_PracticeOpponent:
	.string "Your practice opponent is {STR_VAR_2}\n"
	.string "{STR_VAR_1}!\p"
	.string "{STR_VAR_3}$"

DailyBattleHall_Text_PracticeDraftRules:
	.string "Time to draft! Six rounds, one of\n"
	.string "three POKéMON each round.$"

DailyBattleHall_Text_SavingDraft:
	.string "Saving the game before the draft...$"

DailyBattleHall_Text_ResumeDraft:
	.string "Let's pick up your draft where you\n"
	.string "left off.$"

DailyBattleHall_Text_BossLegend:
	.string "This is a SUNDAY BOSS BATTLE!\p"
	.string "The boss brings the legendary\n"
	.string "{STR_VAR_1}!$"

DailyBattleHall_Text_PracticeOver:
	.string "Good practice! Talk to me if you\n"
	.string "want another round.$"

DailyBattleHall_Text_NewBadge:
	.string "You earned a badge:\n"
	.string "{STR_VAR_1}!$"

DailyBattleHall_Text_ComeBackTomorrow:
	.string "Come back tomorrow for a new\n"
	.string "opponent!$"
