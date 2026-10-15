-- Drives a co-op battle for run_coop_e2e.py: asks the ROM to skip the intro
-- and start a co-op battle, then keeps pressing A (attack with the first
-- move, default target) until the battle ends. Logs the co-op status.
-- Set SCREENSHOT_DIR in a config script to save a screenshot every 300 frames.
local OFF = {
	coopState = 0x98, coopOutcome = 0x99, coopTurn = 0x9A, coopHash = 0x9C,
	coopHashesMatched = 0xA0, coopDesyncs = 0xA2, testCommand = 0xA4, coopBattles = 0xA5,
}
local frames = 0
local requestedAt = nil

callbacks:add("frame", function()
	frames = frames + 1
	local mb = SoullikeBridge.mailbox
	if not mb then return end

	if not requestedAt and emu:read8(mb + 0x09) == 2 then
		emu:write8(mb + OFF.testCommand, 1)
		requestedAt = frames
	end

	-- Mash A to get through the intro to the title screen, and then through
	-- the battle. Not in between: in the overworld, A could start talking to
	-- someone while the battle is about to begin.
	local state = emu:read8(mb + OFF.coopState)
	local test = emu:read8(mb + OFF.testCommand)
	if test == 1 or state == 2 then
		if frames % 8 < 2 then emu:addKey(C.GBA_KEY.A) else emu:clearKey(C.GBA_KEY.A) end
	else
		emu:clearKey(C.GBA_KEY.A)
	end

	if SCREENSHOT_DIR and frames % 300 == 0 then
		emu:screenshot(string.format("%s/p%d_%05d.png", SCREENSHOT_DIR, emu:read8(mb + 0x0A) + 1, frames))
	end

	if frames % 120 == 0 then
		console:log(string.format("COOP frame=%d test=%d state=%d outcome=%d turn=%d hash=%d matched=%d desyncs=%d battles=%d",
			frames, emu:read8(mb + OFF.testCommand), state, emu:read8(mb + OFF.coopOutcome), emu:read16(mb + OFF.coopTurn),
			emu:read32(mb + OFF.coopHash), emu:read16(mb + OFF.coopHashesMatched),
			emu:read16(mb + OFF.coopDesyncs), emu:read8(mb + OFF.coopBattles)))
	end
end)
