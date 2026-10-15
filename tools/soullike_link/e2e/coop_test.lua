-- Drives a co-op battle or a whole run for run_coop_e2e.py: asks the ROM to
-- skip the intro and start a co-op battle (or a run, with TEST_MODE = "run"
-- in a config script), then keeps pressing A (first option in every menu,
-- first move, default target) until it's over. Logs the co-op and run status.
-- Set SCREENSHOT_DIR in a config script to save a screenshot every 300 frames.
local OFF = {
	coopState = 0x98, coopOutcome = 0x99, coopTurn = 0x9A, coopHash = 0x9C,
	coopHashesMatched = 0xA0, coopDesyncs = 0xA2, testCommand = 0xA4, coopBattles = 0xA5,
	runState = 0xA8, runAct = 0xA9, runStep = 0xAA,
}
local RUN_MODE = TEST_MODE == "run"
local START_COMMAND = RUN_MODE and 3 or 1
local frames = 0
local requestedAt = nil

callbacks:add("frame", function()
	frames = frames + 1
	local mb = SoullikeBridge.mailbox
	if not mb then return end

	if not requestedAt and emu:read8(mb + 0x09) == 2 then
		emu:write8(mb + OFF.testCommand, START_COMMAND)
		requestedAt = frames
	end

	-- Mash A to get through the intro to the title screen, and then through
	-- the battle. Not in between: in the overworld, A could start talking to
	-- someone while the battle is about to begin.
	local state = emu:read8(mb + OFF.coopState)
	local test = emu:read8(mb + OFF.testCommand)
	local run = emu:read8(mb + OFF.runState)
	if test == START_COMMAND or state == 2 or (RUN_MODE and run >= 1 and run <= 2) then
		-- In a run, now and then move the cursor down, so the game doesn't get
		-- stuck trying to send out a fainted Pokémon and picks vary a little,
		-- and now and then press B for a while instead of A, to back out of
		-- menus A can't leave (like trying to switch to a Pokémon that's
		-- already out).
		local backing = RUN_MODE and frames % 256 >= 192
		local press = frames % 8 < 2
		if press and not backing then emu:addKey(C.GBA_KEY.A) else emu:clearKey(C.GBA_KEY.A) end
		if press and backing then emu:addKey(C.GBA_KEY.B) else emu:clearKey(C.GBA_KEY.B) end
		if RUN_MODE and frames % 64 == 4 then emu:addKey(C.GBA_KEY.DOWN) else emu:clearKey(C.GBA_KEY.DOWN) end
		-- After backing out, move the battle menu's cursor back up to FIGHT.
		if RUN_MODE and frames % 256 == 250 then emu:addKey(C.GBA_KEY.UP) else emu:clearKey(C.GBA_KEY.UP) end
	else
		emu:clearKey(C.GBA_KEY.A)
		emu:clearKey(C.GBA_KEY.DOWN)
		emu:clearKey(C.GBA_KEY.B)
		emu:clearKey(C.GBA_KEY.UP)
	end

	if SCREENSHOT_DIR and frames % 300 == 0 then
		emu:screenshot(string.format("%s/p%d_%05d.png", SCREENSHOT_DIR, emu:read8(mb + 0x0A) + 1, frames))
	end

	if frames % 120 == 0 then
		console:log(string.format("COOP frame=%d test=%d state=%d outcome=%d turn=%d hash=%d matched=%d desyncs=%d battles=%d",
			frames, emu:read8(mb + OFF.testCommand), state, emu:read8(mb + OFF.coopOutcome), emu:read16(mb + OFF.coopTurn),
			emu:read32(mb + OFF.coopHash), emu:read16(mb + OFF.coopHashesMatched),
			emu:read16(mb + OFF.coopDesyncs), emu:read8(mb + OFF.coopBattles)))
		if RUN_MODE then
			console:log(string.format("RUN frame=%d state=%d act=%d step=%d", frames, run,
				emu:read8(mb + OFF.runAct), emu:read16(mb + OFF.runStep)))
		end
	end
end)
