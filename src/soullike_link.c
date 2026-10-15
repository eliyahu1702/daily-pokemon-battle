#include "global.h"
#include "main.h"
#include "script.h"
#include "soullike_link.h"
#include "string_util.h"

#define QUEUE_SIZE 8
#define PING_INTERVAL_FRAMES 120
#define PING_TIMEOUT_FRAMES 600

struct SoullikeLinkMessage
{
    u8 type;
    u8 len;
    u8 data[SOULLIKE_LINK_PAYLOAD_MAX];
};

struct SoullikeLinkQueue
{
    u8 head;
    u8 count;
    struct SoullikeLinkMessage messages[QUEUE_SIZE];
};

struct SoullikeLinkState
{
    struct SoullikeLinkQueue outgoing;
    struct SoullikeLinkQueue incoming;
    u32 pingSentAt;
    bool8 pingPending;
};

EWRAM_DATA volatile struct SoullikeLinkMailbox gSoullikeLink = {0};
static EWRAM_DATA struct SoullikeLinkState sLinkState = {0};

static const u8 sMagic[8] = {'S', 'O', 'U', 'L', 'L', 'I', 'N', 'K'};

static bool8 QueuePush(struct SoullikeLinkQueue *queue, u8 type, const void *data, u8 len)
{
    struct SoullikeLinkMessage *msg;

    if (queue->count >= QUEUE_SIZE || len > SOULLIKE_LINK_PAYLOAD_MAX)
        return FALSE;

    msg = &queue->messages[(queue->head + queue->count) % QUEUE_SIZE];
    msg->type = type;
    msg->len = len;
    if (len != 0)
        memcpy(msg->data, data, len);
    queue->count++;
    return TRUE;
}

static struct SoullikeLinkMessage *QueuePeek(struct SoullikeLinkQueue *queue)
{
    if (queue->count == 0)
        return NULL;
    return &queue->messages[queue->head];
}

static void QueuePop(struct SoullikeLinkQueue *queue)
{
    queue->head = (queue->head + 1) % QUEUE_SIZE;
    queue->count--;
}

void SoullikeLink_Init(void)
{
    u32 i;

    // The mailbox is written byte by byte because it is volatile.
    for (i = 0; i < sizeof(gSoullikeLink); i++)
        ((volatile u8 *)&gSoullikeLink)[i] = 0;
    memset(&sLinkState, 0, sizeof(sLinkState));

    gSoullikeLink.version = SOULLIKE_LINK_VERSION;
    gSoullikeLink.status = SOULLIKE_LINK_OFFLINE;
    // Write the magic last, so the bridge never finds a half-initialized mailbox.
    for (i = 0; i < sizeof(sMagic); i++)
        gSoullikeLink.magic[i] = sMagic[i];
}

bool8 SoullikeLink_IsConnected(void)
{
    return gSoullikeLink.status == SOULLIKE_LINK_CONNECTED;
}

bool8 SoullikeLink_Send(u8 type, const void *data, u8 len)
{
    return QueuePush(&sLinkState.outgoing, type, data, len);
}

bool8 SoullikeLink_Receive(u8 *type, void *data, u8 *len)
{
    struct SoullikeLinkMessage *msg = QueuePeek(&sLinkState.incoming);

    if (msg == NULL)
        return FALSE;
    *type = msg->type;
    *len = msg->len;
    if (msg->len != 0)
        memcpy(data, msg->data, msg->len);
    QueuePop(&sLinkState.incoming);
    return TRUE;
}

static void FlushOutgoing(void)
{
    struct SoullikeLinkMessage *msg;
    u32 i;

    if (gSoullikeLink.outSeq != gSoullikeLink.outAck)
        return; // the bridge hasn't picked up the previous message yet

    msg = QueuePeek(&sLinkState.outgoing);
    if (msg == NULL)
        return;

    gSoullikeLink.outType = msg->type;
    gSoullikeLink.outLen = msg->len;
    for (i = 0; i < msg->len; i++)
        gSoullikeLink.out[i] = msg->data[i];
    gSoullikeLink.outSeq++;
    QueuePop(&sLinkState.outgoing);
}

static void HandleIncoming(u8 type, const u8 *data, u8 len)
{
    u32 sentAt;

    switch (type)
    {
    case SOULLIKE_MSG_PING:
        SoullikeLink_Send(SOULLIKE_MSG_PONG, data, len);
        break;
    case SOULLIKE_MSG_PONG:
        if (len == sizeof(u32))
        {
            memcpy(&sentAt, data, sizeof(u32));
            if (sLinkState.pingPending && sentAt == sLinkState.pingSentAt)
            {
                sLinkState.pingPending = FALSE;
                gSoullikeLink.lastPingFrames = min(gMain.vblankCounter1 - sentAt, 0xFFFF);
                gSoullikeLink.pongsReceived++;
            }
        }
        break;
    default:
        // Everything else is for the game code, read with SoullikeLink_Receive.
        QueuePush(&sLinkState.incoming, type, data, len);
        break;
    }
}

static void ReadIncoming(void)
{
    u8 type, len, i;
    u8 data[SOULLIKE_LINK_PAYLOAD_MAX];

    if (gSoullikeLink.inSeq == gSoullikeLink.inAck)
        return;

    type = gSoullikeLink.inType;
    len = min(gSoullikeLink.inLen, SOULLIKE_LINK_PAYLOAD_MAX);
    for (i = 0; i < len; i++)
        data[i] = gSoullikeLink.in[i];
    gSoullikeLink.inAck = gSoullikeLink.inSeq;

    HandleIncoming(type, data, len);
}

static void SendPing(void)
{
    u32 now = gMain.vblankCounter1;

    if (SoullikeLink_Send(SOULLIKE_MSG_PING, &now, sizeof(now)))
    {
        sLinkState.pingSentAt = now;
        sLinkState.pingPending = TRUE;
    }
}

static void UpdateHeartbeat(void)
{
    u32 sinceLastPing;

    if (!SoullikeLink_IsConnected())
    {
        sLinkState.pingPending = FALSE;
        return;
    }

    sinceLastPing = gMain.vblankCounter1 - sLinkState.pingSentAt;
    if (sLinkState.pingPending ? sinceLastPing >= PING_TIMEOUT_FRAMES : sinceLastPing >= PING_INTERVAL_FRAMES)
        SendPing();
}

// Called once per frame from the main loop.
void SoullikeLink_Update(void)
{
    ReadIncoming();
    UpdateHeartbeat();
    FlushOutgoing();
}

// Debug menu: Soullike Link status
void SoullikeLink_BufferDebugStatus(struct ScriptContext *ctx)
{
    u8 *str;

    switch (gSoullikeLink.status)
    {
    case SOULLIKE_LINK_CONNECTED:
        str = StringCopy(gStringVar1, COMPOUND_STRING("Connected, P"));
        ConvertIntToDecimalStringN(str, gSoullikeLink.playerId + 1, STR_CONV_MODE_LEFT_ALIGN, 1);
        break;
    case SOULLIKE_LINK_WAITING_FOR_PARTNER:
        StringCopy(gStringVar1, COMPOUND_STRING("Waiting for partner"));
        break;
    default:
        StringCopy(gStringVar1, COMPOUND_STRING("Offline"));
        break;
    }

    if (gSoullikeLink.pongsReceived == 0)
        StringCopy(gStringVar2, COMPOUND_STRING("-"));
    else
        ConvertIntToDecimalStringN(gStringVar2, gSoullikeLink.lastPingFrames, STR_CONV_MODE_LEFT_ALIGN, 5);

    if (gSoullikeLink.dailyDate == 0)
        StringCopy(gStringVar3, COMPOUND_STRING("-"));
    else
        ConvertIntToDecimalStringN(gStringVar3, gSoullikeLink.dailyDate, STR_CONV_MODE_LEFT_ALIGN, 8);
}
