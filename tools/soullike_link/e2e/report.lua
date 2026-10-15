-- Prints the game's link state so run_e2e.py can check it.
local frames = 0
callbacks:add("frame", function()
	frames = frames + 1
	if frames % 300 ~= 0 then return end
	local mb = SoullikeBridge.mailbox
	if not mb then
		console:log("E2E mailbox=none")
		return
	end
	console:log(string.format("E2E frame=%d status=%d player=%d date=%d seed=%d pongs=%d rtt=%d",
		frames, emu:read8(mb + 0x09), emu:read8(mb + 0x0A), emu:read32(mb + 0x90),
		emu:read32(mb + 0x8C), emu:read16(mb + 0x94), emu:read16(mb + 0x96)))
end)
