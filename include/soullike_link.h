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
}; /* size = 0x98 */

extern volatile struct SoullikeLinkMailbox gSoullikeLink;

void SoullikeLink_Init(void);
void SoullikeLink_Update(void);
bool8 SoullikeLink_IsConnected(void);
bool8 SoullikeLink_Send(u8 type, const void *data, u8 len);
bool8 SoullikeLink_Receive(u8 *type, void *data, u8 *len);

#endif // GUARD_SOULLIKE_LINK_H
