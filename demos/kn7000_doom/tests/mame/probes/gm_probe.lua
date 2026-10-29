-- gm_probe.lua -- does the KN7000 (program 893, MAME) take GM System On / Off through the MIDI IN 1 ring, and come back?
-- Run with tests/mame/run_doom.sh OUT this.lua (it starts Doom only to read KN7APP's fw.midiIn1, then quits it).
-- Snapshots: main screen before; after GM On; after GM Off. Sound: notes on ch 4 after program change 30 (GM:
-- Distortion Guitar) in GM mode, then the same notes after GM Off. events.txt has the times.
local M = manager.machine
local cpu = M.devices[":maincpu"]
local sp = cpu.spaces["program"]
local OUT = os.getenv("DOOM_OUT")
local SYM = {}
for line in io.lines(os.getenv("DOOM_SYMS")) do
  local a, n = line:match("^(%x+) %a (%S+)$")
  if a then SYM[n] = tonumber(a, 16) end
end
local function now() return M.time:as_double() end
local ev = io.open(OUT .. "/events.txt", "w")
local function E(...) local s = string.format("%8.2f ", now()) .. string.format(...); print("gm_probe: " .. s); ev:write(s, "\n"); ev:flush() end
local function cstr(a, n)
  local s = ""
  for i = 0, (n or 64) - 1 do local c = sp:read_u8(a + i); if c == 0 then break end; s = s .. string.char(c) end
  return s
end
local function field(tag, mask)
  local p = M.ioport.ports[":cpanel:" .. tag] or M.ioport.ports[":" .. tag]
  for _, f in pairs(p.fields) do if f.mask == mask then return f end end
end
local function wait(s) local t = now() + s; while now() < t do coroutine.yield() end end
local function wait_until(cond, s) local t = now() + s; while now() < t do if cond() then return true end; coroutine.yield() end; return cond() end
local function press(tag, mask) local f = field(tag, mask); f:set_value(1); wait(0.3); f:set_value(0) end
local menuV, V_NPROG, ORDER, PR_NAME = 0x502C0000, 0x502C0034, 0x502C3800, 0x40

local ring
local function put(bytes)  -- into MIDI IN 1's ring as if received (KN7APP notes.c midi_put3, any length)
  local free = sp:read_u16(ring + 0x10)
  if free < #bytes then E("ring full"); return end
  local wr, start, last = sp:read_u32(ring + 0x0C), sp:read_u32(ring), sp:read_u32(ring + 4)
  for _, b in ipairs(bytes) do sp:write_u8(wr, b); wr = (wr == last) and start or wr + 1 end
  sp:write_u16(ring + 0x10, free - #bytes)
  sp:write_u32(ring + 0x0C, wr)
end
local function notes(ch, mark)
  E("%s start", mark)
  for _, n in ipairs({ 48, 52, 55, 60 }) do put({ 0x90 + ch - 1, n, 100 }); wait(0.4); put({ 0x80 + ch - 1, n, 0 }); wait(0.1) end
  E("%s end", mark)
end

local co = coroutine.create(function()
  wait_until(function() return now() > 30 end, 60)
  M.video:snapshot()
  press("CPR_SEG1", 0x04); wait(6)
  press("CPL_SEG0", 0x20); wait(8)
  for i = 1, 8 do
    press("CPR_SEG6", 0x01)
    if wait_until(function() return sp:read_u32(0x502B9E00) == 0x53374E4B end, 20) then break end
  end
  wait_until(function() return sp:read_u32(menuV) == 4 end, 150)
  press("CPL_SEG0", 0x02); wait(1)
  press("CPR_SEG5", 0x01)
  E("RUN")
  wait_until(function() return sp:read_u32(SYM["_fw"] + 52) ~= 0 end, 60)
  ring = sp:read_u32(SYM["_fw"] + 52)
  E("MIDI IN 1 ring header %08X", ring)
  wait_until(function() return sp:read_u32(SYM["_gamestate"]) == 3 end, 200)  -- the title screen: Doom is up
  wait(2)
  local f = field("CPC_SEG11", 0x80); f:set_value(1); wait(2.5); f:set_value(0)
  wait_until(function() return sp:read_u8(0x5031BC00) == 0xFE end, 60)
  wait(2); press("CPC_SEG11", 0x80); wait(4)          -- EXIT on the message: "Continue Next?"
  press("CPR_SEG6", 0x01); wait(6)                    -- NO (right 4): the main screen (Pinball's pf_test.lua)
  M.video:snapshot(); E("main screen")
  local function drums(mark)
    E("%s start", mark)
    for _, n in ipairs({ 36, 38, 42, 46, 36, 38 }) do put({ 0x99, n, 110 }); wait(0.3); put({ 0x89, n, 0 }); wait(0.05) end
    E("%s end", mark)
  end
  put({ 0xC3, 30 }); wait(0.5); notes(4, "NX pc30")
  put({ 0xB3, 0, 121 }); put({ 0xB3, 32, 0 }); put({ 0xC3, 30 }); wait(0.5); notes(4, "GM2 bank pc30 distortion guitar")
  put({ 0xB3, 0, 121 }); put({ 0xB3, 32, 0 }); put({ 0xC3, 73 }); wait(0.5); notes(4, "GM2 bank pc73 flute")
  put({ 0xB3, 0, 121 }); put({ 0xB3, 32, 0 }); put({ 0xC3, 0 }); wait(0.5); notes(4, "GM2 bank pc0 piano")
  put({ 0xB3, 0, 121 }); put({ 0xB3, 32, 0 }); put({ 0xC3, 56 }); wait(0.5); notes(4, "GM2 bank pc56 trumpet")
  drums("NX ch10 drums")
  put({ 0xB9, 0, 120 }); put({ 0xB9, 32, 0 }); put({ 0xC9, 0 }); wait(0.5); drums("GM2 drum bank kit 0")
  wait(2); M.video:snapshot()
  E("end")
  ev:close()
  M:exit()
end)
GM_TICK = emu.add_machine_frame_notifier(function()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then print("gm_probe: ERROR " .. tostring(err)); M:exit() end
  end
end)
