-- midi_latency.lua -- how soon does the firmware (program 893, MAME) take a byte put into MIDI IN 1's ring while Doom
-- runs (renders) on the program task? Run with tests/mame/run_doom.sh OUT this.lua. Once the title screen is up, 40
-- times at random moments: a note-on (ch 4) and its note-off put into the ring (KN7APP notes.c midi_put3 as Lua), then
-- the time until the ring's read pointer (+8) passes them, by a write tap on it. events.txt: each latency in ms.
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
local function E(...) local s = string.format("%8.3f ", now()) .. string.format(...); print("midi_latency: " .. s); ev:write(s, "\n"); ev:flush() end
local function field(tag, mask)
  local p = M.ioport.ports[":cpanel:" .. tag] or M.ioport.ports[":" .. tag]
  for _, f in pairs(p.fields) do if f.mask == mask then return f end end
end
local function wait(s) local t = now() + s; while now() < t do coroutine.yield() end end
local function wait_until(cond, s) local t = now() + s; while now() < t do if cond() then return true end; coroutine.yield() end; return cond() end
local function press(tag, mask) local f = field(tag, mask); f:set_value(1); wait(0.3); f:set_value(0) end
local menuV = 0x502C0000
local ring, target, tPut, lat = nil, nil, 0, {}
local function put(bytes)
  local free = sp:read_u16(ring + 0x10)
  local wr, start, last = sp:read_u32(ring + 0x0C), sp:read_u32(ring), sp:read_u32(ring + 4)
  for _, b in ipairs(bytes) do sp:write_u8(wr, b); wr = (wr == last) and start or wr + 1 end
  sp:write_u16(ring + 0x10, free - #bytes)
  sp:write_u32(ring + 0x0C, wr)
  return wr
end
local co = coroutine.create(function()
  wait_until(function() return now() > 30 end, 60)
  press("CPR_SEG1", 0x04); wait(6)
  press("CPL_SEG0", 0x20); wait(8)
  for i = 1, 8 do
    press("CPR_SEG6", 0x01)
    if wait_until(function() return sp:read_u32(0x502B9E00) == 0x53374E4B end, 20) then break end
  end
  wait_until(function() return sp:read_u32(menuV) == 4 end, 150)
  press("CPL_SEG0", 0x02); wait(1)
  press("CPR_SEG5", 0x01)
  wait_until(function() return sp:read_u32(SYM["_fw"] + 52) ~= 0 end, 60)
  ring = sp:read_u32(SYM["_fw"] + 52)
  wait_until(function() return sp:read_u32(SYM["_gamestate"]) == 3 or sp:read_u32(SYM["_demoplayback"]) == 1 end, 200)
  E("title; ring %08X", ring)
  RD_TAP = sp:install_write_tap(ring + 8, ring + 11, "rd", function(offset, data, mask)
    if target and data == target then lat[#lat + 1] = (now() - tPut) * 1000; target = nil end
  end)
  for i = 1, 40 do
    wait(0.25 + math.random() * 0.5)
    tPut = now()
    target = put({ 0x93, 60, 90, 0x83, 60, 0 })
    wait_until(function() return target == nil end, 2)
    E("note %d: %s ms (frames %d)", i, lat[#lat] and string.format("%.1f", lat[#lat]) or "never", sp:read_u32(SYM["_kn7Frames"]))
  end
  table.sort(lat)
  E("latency ms: min %.1f median %.1f max %.1f (%d of 40)", lat[1] or -1, lat[#lat // 2 + 1] or -1, lat[#lat] or -1, #lat)
  ev:close()
  M:exit()
end)
ML_TICK = emu.add_machine_frame_notifier(function()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then print("midi_latency: ERROR " .. tostring(err)); M:exit() end
  end
end)
