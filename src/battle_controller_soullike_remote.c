// Battle controller for the co-op partner's Pokémon in a Soullike co-op
// battle. It shows the partner's Pokémon like any ally, and when the engine
// asks what that Pokémon should do, it waits for the partner's answer to
// arrive over Soullike Link instead of asking this player. See soullike_coop.h.

#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_controllers.h"
#include "battle_interface.h"
#include "data.h"
#include "pokeball.h"
#include "pokemon.h"
#include "soullike_coop.h"
#include "sound.h"
#include "sprite.h"
#include "trainer.h"
#include "constants/battle_anim.h"

static void RemoteHandleDrawTrainerPic(enum BattlerId battler);
static void RemoteHandleTrainerSlide(enum BattlerId battler);
static void RemoteHandleTrainerSlideBack(enum BattlerId battler);
static void RemoteHandleChoice(enum BattlerId battler);
static void RemoteHandleIntroTrainerBallThrow(enum BattlerId battler);
static void RemoteHandleDrawPartyStatusSummary(enum BattlerId battler);
static void RemoteBufferRunCommand(enum BattlerId battler);
static void RemoteBufferExecCompleted(enum BattlerId battler);

static void (*const sSoullikeRemoteBufferCommands[CONTROLLER_CMDS_COUNT])(enum BattlerId battler) =
{
    [CONTROLLER_GETMONDATA]               = BtlController_HandleGetMonData,
    [CONTROLLER_GETRAWMONDATA]            = BtlController_Empty,
    [CONTROLLER_SETMONDATA]               = BtlController_HandleSetMonData,
    [CONTROLLER_SETRAWMONDATA]            = BtlController_HandleSetRawMonData,
    [CONTROLLER_LOADMONSPRITE]            = BtlController_HandleLoadMonSprite,
    [CONTROLLER_SWITCHINANIM]             = BtlController_HandleSwitchInAnim,
    [CONTROLLER_RETURNMONTOBALL]          = BtlController_HandleReturnMonToBall,
    [CONTROLLER_DRAWTRAINERPIC]           = RemoteHandleDrawTrainerPic,
    [CONTROLLER_TRAINERSLIDE]             = RemoteHandleTrainerSlide,
    [CONTROLLER_TRAINERSLIDEBACK]         = RemoteHandleTrainerSlideBack,
    [CONTROLLER_FAINTANIMATION]           = BtlController_HandleFaintAnimation,
    [CONTROLLER_PALETTEFADE]              = BtlController_Empty,
    [CONTROLLER_BALLTHROWANIM]            = BtlController_Empty,
    [CONTROLLER_PAUSE]                    = BtlController_Empty,
    [CONTROLLER_MOVEANIMATION]            = BtlController_HandleMoveAnimation,
    [CONTROLLER_PRINTSTRING]              = BtlController_HandlePrintString,
    [CONTROLLER_PRINTSTRINGPLAYERONLY]    = BtlController_Empty,
    [CONTROLLER_CHOOSEACTION]             = RemoteHandleChoice,
    [CONTROLLER_YESNOBOX]                 = RemoteHandleChoice,
    [CONTROLLER_CHOOSEMOVE]               = RemoteHandleChoice,
    [CONTROLLER_OPENBAG]                  = RemoteHandleChoice,
    [CONTROLLER_CHOOSEPOKEMON]            = RemoteHandleChoice,
    [CONTROLLER_23]                       = BtlController_Empty,
    [CONTROLLER_HEALTHBARUPDATE]          = BtlController_HandleHealthBarUpdate,
    [CONTROLLER_EXPUPDATE]                = BtlController_Empty, // No experience during co-op battles
    [CONTROLLER_STATUSICONUPDATE]         = BtlController_HandleStatusIconUpdate,
    [CONTROLLER_STATUSANIMATION]          = BtlController_HandleStatusAnimation,
    [CONTROLLER_STATUSXOR]                = BtlController_Empty,
    [CONTROLLER_DATATRANSFER]             = BtlController_Empty,
    [CONTROLLER_DMA3TRANSFER]             = BtlController_Empty,
    [CONTROLLER_PLAYBGM]                  = BtlController_Empty,
    [CONTROLLER_32]                       = BtlController_Empty,
    [CONTROLLER_TWORETURNVALUES]          = BtlController_Empty,
    [CONTROLLER_CHOSENMONRETURNVALUE]     = BtlController_Empty,
    [CONTROLLER_ONERETURNVALUE]           = BtlController_Empty,
    [CONTROLLER_ONERETURNVALUE_DUPLICATE] = BtlController_Empty,
    [CONTROLLER_HITANIMATION]             = BtlController_HandleHitAnimation,
    [CONTROLLER_CANTSWITCH]               = BtlController_Empty,
    [CONTROLLER_PLAYSE]                   = BtlController_HandlePlaySE,
    [CONTROLLER_PLAYFANFAREORBGM]         = BtlController_HandlePlayFanfareOrBGM,
    [CONTROLLER_FAINTINGCRY]              = BtlController_HandleFaintingCry,
    [CONTROLLER_INTROSLIDE]               = BtlController_HandleIntroSlide,
    [CONTROLLER_INTROTRAINERBALLTHROW]    = RemoteHandleIntroTrainerBallThrow,
    [CONTROLLER_DRAWPARTYSTATUSSUMMARY]   = RemoteHandleDrawPartyStatusSummary,
    [CONTROLLER_HIDEPARTYSTATUSSUMMARY]   = BtlController_HandleHidePartyStatusSummary,
    [CONTROLLER_ENDBOUNCE]                = BtlController_Empty,
    [CONTROLLER_SPRITEINVISIBILITY]       = BtlController_HandleSpriteInvisibility,
    [CONTROLLER_BATTLEANIMATION]          = BtlController_HandleBattleAnimation,
    [CONTROLLER_LINKSTANDBYMSG]           = BtlController_Empty,
    [CONTROLLER_RESETACTIONMOVESELECTION] = BtlController_Empty,
    [CONTROLLER_ENDLINKBATTLE]            = BtlController_Empty,
    [CONTROLLER_DEBUGMENU]                = BtlController_Empty,
    [CONTROLLER_TERMINATOR_NOP]           = BtlController_TerminatorNop
};

void SetControllerToSoullikeRemote(enum BattlerId battler)
{
    gBattlerBattleController[battler] = BATTLE_CONTROLLER_SOULLIKE_REMOTE;
    gBattlerControllerEndFuncs[battler] = RemoteBufferExecCompleted;
    gBattlerControllerFuncs[battler] = RemoteBufferRunCommand;
}

static void RemoteBufferRunCommand(enum BattlerId battler)
{
    if (IsBattleControllerActiveOnLocal(battler))
    {
        if (gBattleResources->bufferA[battler][0] < ARRAY_COUNT(sSoullikeRemoteBufferCommands))
            sSoullikeRemoteBufferCommands[gBattleResources->bufferA[battler][0]](battler);
        else
            BtlController_Complete(battler);
    }
}

static void RemoteBufferExecCompleted(enum BattlerId battler)
{
    gBattlerControllerFuncs[battler] = RemoteBufferRunCommand;
    MarkBattleControllerIdleOnLocal(battler);
}

static enum TrainerPicID RemoteGetTrainerBackPicId(void)
{
    // The partner's trainer is drawn as their own character.
    return GetPlayerTrainerPic(SoullikeCoop_GetPartnerGender(), GAME_VERSION);
}

static void Intro_WaitForHealthbox(enum BattlerId battler)
{
    bool32 finished = (gSprites[gHealthboxSpriteIds[battler]].callback == SpriteCallbackDummy);

    if (IsCryPlayingOrClearCrySongs())
        finished = FALSE;

    if (finished)
    {
        gBattleSpritesDataPtr->healthBoxesData[battler].introEndDelay = 3;
        gBattlerControllerFuncs[battler] = BtlController_Intro_DelayAndEnd;
    }
}

static void Intro_ShowHealthbox(enum BattlerId battler)
{
    if (!gBattleSpritesDataPtr->healthBoxesData[battler].ballAnimActive
        && !gBattleSpritesDataPtr->healthBoxesData[GetPartnerBattler(battler)].ballAnimActive
        && gSprites[gBattleControllerData[battler]].callback == SpriteCallbackDummy
        && gSprites[gBattlerSpriteIds[battler]].callback == SpriteCallbackDummy
        && ++gBattleSpritesDataPtr->healthBoxesData[battler].introEndDelay != 1)
    {
        gBattleSpritesDataPtr->healthBoxesData[battler].introEndDelay = 0;
        TryShinyAnimation(battler, GetBattlerMon(battler));

        DestroySprite(&gSprites[gBattleControllerData[battler]]);
        UpdateHealthboxAttribute(gHealthboxSpriteIds[battler], GetBattlerMon(battler), HEALTHBOX_ALL);
        StartHealthboxSlideIn(battler);
        SetHealthboxSpriteVisible(gHealthboxSpriteIds[battler]);

        gBattleSpritesDataPtr->animationData->introAnimActive = FALSE;

        gBattlerControllerFuncs[battler] = Intro_WaitForHealthbox;
    }
}

static void RemoteHandleDrawTrainerPic(enum BattlerId battler)
{
    enum TrainerPicID trainerPicId = RemoteGetTrainerBackPicId();
    s16 xPos = (GetBattlerPosition(battler) & BIT_FLANK) == B_FLANK_LEFT ? 32 : 90;
    s16 yPos = (8 - GetTrainerBackPicCoords(trainerPicId)->size) * 4 + 80;

    BtlController_HandleDrawTrainerPic(battler, trainerPicId, FALSE, xPos, yPos, -1);
}

static void RemoteHandleTrainerSlide(enum BattlerId battler)
{
    BtlController_HandleTrainerSlide(battler, RemoteGetTrainerBackPicId());
}

static void RemoteHandleTrainerSlideBack(enum BattlerId battler)
{
    BtlController_HandleTrainerSlideBack(battler, 35, FALSE);
}

static void WaitForPartnerChoice(enum BattlerId battler)
{
    if (SoullikeCoop_TryTakePartnerChoice(battler))
        BtlController_Complete(battler);
}

static void RemoteHandleChoice(enum BattlerId battler)
{
    gBattlerControllerFuncs[battler] = WaitForPartnerChoice;
    WaitForPartnerChoice(battler);
}

static void RemoteHandleIntroTrainerBallThrow(enum BattlerId battler)
{
    const u16 *trainerPal = GetTrainerBackPicPalette(RemoteGetTrainerBackPicId());
    BtlController_HandleIntroTrainerBallThrow(battler, 0xD6F9, trainerPal, 24, Intro_ShowHealthbox);
}

static void RemoteHandleDrawPartyStatusSummary(enum BattlerId battler)
{
    BtlController_HandleDrawPartyStatusSummary(battler, B_SIDE_PLAYER, TRUE);
}
