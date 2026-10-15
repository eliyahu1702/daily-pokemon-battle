-- Pokémon Soullike link bridge for mGBA 0.10 or newer.
--
-- Load it in mGBA with Tools > Scripting > File > Load script, after
-- loading the ROM. It connects to the relay server and copies messages
-- between the relay and the game's mailbox in memory.
--
-- Both players must use the same RELAY_HOST, RELAY_PORT and ROOM.

RELAY_HOST = RELAY_HOST or "127.0.0.1"
RELAY_PORT = RELAY_PORT or 7777
ROOM = ROOM or "soullike"

-- Mailbox layout. Must match include/soullike_link.h.
local LINK_VERSION = 1
local PAYLOAD_MAX = 60
local MAGIC = "SOULLINK"
local OFF = {
	version = 0x08,
	status = 0x09,
	playerId = 0x0A,
	outSeq = 0x0C,
	outAck = 0x0D,
	inSeq = 0x0E,
	inAck = 0x0F,
	outType = 0x10,
	outLen = 0x11,
	out = 0x12,
	inType = 0x4E,
	inLen = 0x4F,
	inData = 0x50,
	dailySeed = 0x8C,
	dailyDate = 0x90,
}
local STATUS_OFFLINE, STATUS_WAITING, STATUS_CONNECTED = 0, 1, 2

-- Relay wire format: u16 big-endian length, then that many bytes.
-- The first byte of the body is the frame kind.
local KIND_HELLO, KIND_WELCOME, KIND_DATA, KIND_PEER, KIND_ERROR = 1, 2, 3, 4, 5

local EWRAM_START, EWRAM_SIZE = 0x02000000, 0x40000
local RECONNECT_FRAMES = 180
local RESCAN_FRAMES = 60

-- Loading the script again in mGBA keeps the old copy running, which
-- would join the room a second time and fill it. Shut the old copy down.
if SoullikeBridge and SoullikeBridge.shutdown then
	SoullikeBridge.shutdown()
end

local bridge = {
	sock = nil,
	buffer = "",
	mailbox = nil,      -- EWRAM address of the mailbox, once found
	inbox = {},         -- messages from the partner waiting for the ROM
	status = STATUS_OFFLINE,
	playerId = 0,
	seed = 0,
	date = 0,
	retryIn = 0,
	rescanIn = 0,
	stats = { sent = 0, received = 0 },
}
SoullikeBridge = bridge

local function log(msg)
	console:log("[Soullike] " .. msg)
end

local function frame(kind, body)
	local payload = string.char(kind) .. (body or "")
	return string.pack(">I2", #payload) .. payload
end

-- Mailbox -----------------------------------------------------------------

local function mailboxValid()
	return bridge.mailbox and emu:readRange(bridge.mailbox, #MAGIC) == MAGIC
end

local function findMailbox()
	local ram = emu:readRange(EWRAM_START, EWRAM_SIZE)
	local pos = ram:find(MAGIC, 1, true)
	if not pos then
		return nil
	end
	local addr = EWRAM_START + pos - 1
	local version = emu:read8(addr + OFF.version)
	if version ~= LINK_VERSION then
		log(string.format("ROM link version %d does not match bridge version %d", version, LINK_VERSION))
		return nil
	end
	return addr
end

local function writeSessionInfo()
	local mb = bridge.mailbox
	if not mb then
		return
	end
	emu:write8(mb + OFF.status, bridge.status)
	emu:write8(mb + OFF.playerId, bridge.playerId)
	emu:write32(mb + OFF.dailySeed, bridge.seed)
	emu:write32(mb + OFF.dailyDate, bridge.date)
end

local function setStatus(status)
	if bridge.status ~= status then
		bridge.status = status
		local names = { [0] = "offline", "waiting for partner", "connected to partner" }
		log("Status: " .. names[status])
	end
	writeSessionInfo()
end

-- Network -----------------------------------------------------------------

local function disconnect(reason)
	if bridge.sock then
		bridge.sock:close()
		bridge.sock = nil
		log("Disconnected: " .. reason)
	end
	bridge.buffer = ""
	bridge.inbox = {}
	bridge.retryIn = RECONNECT_FRAMES
	setStatus(STATUS_OFFLINE)
end

local function handleFrame(body)
	local kind = body:byte(1)
	if kind == KIND_WELCOME then
		bridge.playerId, bridge.seed, bridge.date = string.unpack(">I1I4I4", body, 2)
		log(string.format("Joined room '%s' as player %d (Daily Run %d)", ROOM, bridge.playerId + 1, bridge.date))
		setStatus(STATUS_WAITING)
	elseif kind == KIND_PEER then
		if body:byte(2) == 1 then
			setStatus(STATUS_CONNECTED)
		else
			bridge.inbox = {}
			setStatus(STATUS_WAITING)
		end
	elseif kind == KIND_DATA then
		bridge.inbox[#bridge.inbox + 1] = body:sub(2)
		bridge.stats.received = bridge.stats.received + 1
	elseif kind == KIND_ERROR then
		log("Relay error: " .. body:sub(2))
		disconnect("relay refused the connection")
	end
end

local function onReceived()
	while bridge.sock do
		local data, err = bridge.sock:receive(1024)
		if not data then
			if err ~= socket.ERRORS.AGAIN then
				disconnect(tostring(err))
			end
			return
		end
		bridge.buffer = bridge.buffer .. data
		while #bridge.buffer >= 2 do
			local len = string.unpack(">I2", bridge.buffer)
			if #bridge.buffer < 2 + len then
				break
			end
			local body = bridge.buffer:sub(3, 2 + len)
			bridge.buffer = bridge.buffer:sub(3 + len)
			if len > 0 then
				handleFrame(body)
			end
		end
		-- Only read what select() reported; the next frame polls again.
		if not bridge.sock or not bridge.sock:hasdata() then
			return
		end
	end
end

local function connect()
	log(string.format("Connecting to %s:%d...", RELAY_HOST, RELAY_PORT))
	local sock = socket.tcp()
	local ok, err = sock:connect(RELAY_HOST, RELAY_PORT)
	if not ok then
		log("Could not reach the relay: " .. tostring(err))
		sock:close()
		bridge.retryIn = RECONNECT_FRAMES
		return
	end
	bridge.sock = sock
	sock:add("received", onReceived)
	sock:add("error", function(_, e) disconnect(tostring(e)) end)
	sock:send(frame(KIND_HELLO, string.char(LINK_VERSION) .. ROOM))
end

-- Per-frame pump ----------------------------------------------------------

local function pumpOutgoing()
	local mb = bridge.mailbox
	local outSeq = emu:read8(mb + OFF.outSeq)
	if outSeq == emu:read8(mb + OFF.outAck) then
		return
	end
	local msgType = emu:read8(mb + OFF.outType)
	local len = math.min(emu:read8(mb + OFF.outLen), PAYLOAD_MAX)
	local payload = len > 0 and emu:readRange(mb + OFF.out, len) or ""
	-- Messages sent while the partner is away are dropped; the game
	-- only relies on the link while the status says connected.
	if bridge.sock and bridge.status == STATUS_CONNECTED then
		bridge.sock:send(frame(KIND_DATA, string.char(msgType) .. payload))
		bridge.stats.sent = bridge.stats.sent + 1
	end
	emu:write8(mb + OFF.outAck, outSeq)
end

local function pumpIncoming()
	local mb = bridge.mailbox
	local inSeq = emu:read8(mb + OFF.inSeq)
	if #bridge.inbox == 0 or inSeq ~= emu:read8(mb + OFF.inAck) then
		return
	end
	local msg = table.remove(bridge.inbox, 1)
	local msgType = msg:byte(1)
	local payload = msg:sub(2, 1 + PAYLOAD_MAX)
	emu:write8(mb + OFF.inType, msgType)
	emu:write8(mb + OFF.inLen, #payload)
	for i = 1, #payload do
		emu:write8(mb + OFF.inData + i - 1, payload:byte(i))
	end
	-- Bump the sequence number last so the ROM never reads a half-written message.
	emu:write8(mb + OFF.inSeq, (inSeq + 1) & 0xFF)
end

local function onFrame()
	if not bridge.sock then
		bridge.retryIn = bridge.retryIn - 1
		if bridge.retryIn <= 0 then
			connect()
		end
	end

	if not mailboxValid() then
		-- Not booted yet, or the game was reset.
		bridge.mailbox = nil
		bridge.rescanIn = bridge.rescanIn - 1
		if bridge.rescanIn > 0 then
			return
		end
		bridge.rescanIn = RESCAN_FRAMES
		bridge.mailbox = findMailbox()
		if not bridge.mailbox then
			return
		end
		log(string.format("Found the game's mailbox at 0x%08X", bridge.mailbox))
		writeSessionInfo()
	end

	pumpOutgoing()
	pumpIncoming()
end

local callbackIds = {
	callbacks:add("frame", onFrame),
	callbacks:add("stop", function() disconnect("emulation stopped") end),
}

function bridge.shutdown()
	for _, id in ipairs(callbackIds) do
		callbacks:remove(id)
	end
	disconnect("script reloaded")
	bridge.retryIn = math.huge
end

log("Bridge loaded. Room: " .. ROOM)
