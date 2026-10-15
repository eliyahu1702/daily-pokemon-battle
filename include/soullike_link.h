#ifndef GUARD_SOULLIKE_LINK_H
#define GUARD_SOULLIKE_LINK_H

// Soullike Link: lets two emulators talk to each other.
//
// The ROM never touches the network. It reads and writes a small mailbox
// in EWRAM, and the mGBA Lua script tools/soullike_link/bridge.lua copies
// messages between that mailbox and the relay server.
//
// The mailbox layout is shared with bridge.lua. If you change it, bump
// SOULLIKE_LINK_VERSION and update the offsets at the top of bridge.lua.

#define SOULLIKE_LINK_VERSION       1
#define SOULLIKE_LINK_PAYLOAD_MAX   60

enum SoullikeLinkStatus
{
    SOULLIKE_LINK_OFFLINE,              // no bridge script or no relay connection
    SOULLIKE_LINK_WAITING_FOR_PARTNER,  // connected to the relay, partner not in the room yet
    SOULLIKE_LINK_CONNECTED,            // both players are in the room
};

enum SoullikeLinkMsgType
{
    SOULLIKE_MSG_NONE,
    SOULLIKE_MSG_PING,      // payload: u32 sender frame counter
    SOULLIKE_MSG_PONG,      // payload: echo of the PING payload
    SOULLIKE_MSG_COOP_TEAM, // co-op battle handshake: one chunk of the sender's team
    SOULLIKE_MSG_COOP_READY,// co-op battle handshake: team sent, plus the battle seed
    SOULLIKE_MSG_COOP_CHOICE, // co-op battle: what the sender picked for their Pokémon
    SOULLIKE_MSG_COOP_HASH, // co-op battle: checksum of the battle state at the start of a turn
    SOULLIKE_MSG_RUN_START, // run: the sender is ready to start run number N
    SOULLIKE_MSG_RUN_PICK,  // run: what the sender picked at one decision of the run
};

enum SoullikeRunState
{
    SOULLIKE_RUN_IDLE,
    SOULLIKE_RUN_STARTING,  // waiting for the partner to start the run too
    SOULLIKE_RUN_PLAYING,
    SOULLIKE_RUN_WON,
    SOULLIKE_RUN_LOST,
};

enum SoullikeCoopState
{
    SOULLIKE_COOP_IDLE,
    SOULLIKE_COOP_HANDSHAKE,  // swapping teams with the partner
    SOULLIKE_COOP_IN_BATTLE,
    SOULLIKE_COOP_FINISHED,   // last battle is over, coopOutcome holds the result
};

enum SoullikeTestCommand
{
    SOULLIKE_TEST_NONE,
    SOULLIKE_TEST_COOP_BATTLE,  // test script: skip the intro, start a new game and begin a co-op battle
    SOULLIKE_TEST_COOP_LOADING, // ROM: new game started, the battle begins once the map is up
    SOULLIKE_TEST_RUN,          // test script: like SOULLIKE_TEST_COOP_BATTLE, but starts a whole run
    SOULLIKE_TEST_RUN_LOADING,  // ROM: new game started, the run begins once the map is up
};

struct SoullikeLinkMailbox
{
    /*0x00*/ u8 magic[8];           // "SOULLINK", written by the ROM so the bridge can find the mailbox
    /*0x08*/ u8 version;            // ROM: SOULLIKE_LINK_VERSION
    /*0x09*/ u8 status;             // bridge: enum SoullikeLinkStatus
    /*0x0A*/ u8 playerId;           // bridge: 0 or 1, assigned by the relay
    /*0x0B*/ u8 unused;
    /*0x0C*/ u8 outSeq;             // ROM: bumped after filling out*
    /*0x0D*/ u8 outAck;             // bridge: set to outSeq once the message is sent
    /*0x0E*/ u8 inSeq;              // bridge: bumped after filling in*
    /*0x0F*/ u8 inAck;              // ROM: set to inSeq once the message is read
    /*0x10*/ u8 outType;
    /*0x11*/ u8 outLen;
    /*0x12*/ u8 out[SOULLIKE_LINK_PAYLOAD_MAX];
    /*0x4E*/ u8 inType;
    /*0x4F*/ u8 inLen;
    /*0x50*/ u8 in[SOULLIKE_LINK_PAYLOAD_MAX];
    /*0x8C*/ u32 dailySeed;         // bridge: today's Daily Run seed, from the relay
    /*0x90*/ u32 dailyDate;         // bridge: today's date as YYYYMMDD (UTC), from the relay
    /*0x94*/ u16 pongsReceived;     // ROM: link health, for the debug menu and tests
    /*0x96*/ u16 lastPingFrames;    // ROM: round trip of the last ping, in frames
    // Co-op battle status, for tests and debugging. The ROM writes these,
    // except testCommand, which a test script sets.
    /*0x98*/ u8 coopState;          // enum SoullikeCoopState
    /*0x99*/ u8 coopOutcome;        // gBattleOutcome of the last co-op battle
    /*0x9A*/ u16 coopTurn;          // turns played in the current co-op battle
    /*0x9C*/ u32 coopHash;          // checksum of the battle state at the start of the current turn
    /*0xA0*/ u16 coopHashesMatched; // turns where both games had the same checksum
    /*0xA2*/ u16 coopDesyncs;       // turns where they did not, or choices arrived out of order
    /*0xA4*/ u8 testCommand;        // test script: enum SoullikeTestCommand, cleared by the ROM
    /*0xA5*/ u8 coopBattles;        // co-op battles started since power on
    /*0xA6*/ u16 coopRngCalls;      // random numbers the co-op battle engine has drawn (low 16 bits)
    /*0xA8*/ u8 runState;           // enum SoullikeRunState
    /*0xA9*/ u8 runAct;             // act of the current run, from 0
    /*0xAA*/ u16 runStep;           // decisions made so far in the current run
}; /* size = 0xAC */

extern volatile struct SoullikeLinkMailbox gSoullikeLink;

void SoullikeLink_Init(void);
void SoullikeLink_Update(void);
bool8 SoullikeLink_IsConnected(void);
bool8 SoullikeLink_Send(u8 type, const void *data, u8 len);
bool8 SoullikeLink_Receive(u8 *type, void *data, u8 *len);

#endif // GUARD_SOULLIKE_LINK_H
