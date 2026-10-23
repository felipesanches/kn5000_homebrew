-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU General Public License v3 or later
--
-- rate.lua -- the effects DSP's frame rate in MAME with a program of the menu running: DISK MENU, STYLE CONVERT,
-- OK; the menu's row named $RATE_NAME, RUN; then MSGR7 (DM 0x0F, the heartbeat DSPTONE's and KN7FM's units count)
-- read every second for 10 s, with the keys named in $RATE_KEYS held. Writes $RATE_OUT/rate.txt. (run_rate.sh; -nodrc in $EXTRA to try MAME's
-- interpreter.)
local M = manager.machine
local sp = M.devices[":maincpu"].spaces["program"]
local dm = M.devices[":dsp"].spaces["data"]
local OUT, NAME = assert(os.getenv("RATE_OUT")), assert(os.getenv("RATE_NAME"))
local PANEL = 0x5006BE4C
local f = assert(io.open(OUT .. "/rate.txt", "w"))
local function now() return M.time:as_double() end
local function E(...) local s = string.format("%8.2f ", now()) .. string.format(...); print(s); f:write(s, "\n"); f:flush() end
local function wait(s) local t = now() + s; while now() < t do coroutine.yield() end end
local function wait_until(c, s) local t = now() + s; while now() < t do if c() then return true end; coroutine.yield() end; return c() end
local function field(tag, mask)
  local p = M.ioport.ports[":cpanel:" .. tag] or M.ioport.ports[":" .. tag]
  for _, x in pairs(p.fields) do if x.mask == mask then return x end end
end
local function press(tag, mask)
  local x = field(tag, mask)
  local seg = tonumber(tag:match("SEG(%d+)"))
  local byte = PANEL + seg + (tag:sub(1, 3) == "CPR" and 0 or 0x30)
  for _ = 1, 6 do
    x:set_value(1)
    local seen = wait_until(function() return (sp:read_u8(byte) & mask) ~= 0 end, 1.0)
    wait(0.3); x:set_value(0)
    if seen then return true end
    wait(1.5)
  end
end
local function cstr(a)
  local s = ""
  for i = 0, 63 do local c = sp:read_u8(a + i); if c == 0 then break end; s = s .. string.char(c) end
  return s
end
local co = coroutine.create(function()
  wait_until(function() return now() > 30 end, 60)
  press("CPR_SEG1", 0x04); wait(6); press("CPL_SEG0", 0x20); wait(8)
  for _ = 1, 8 do
    press("CPR_SEG6", 0x01)
    if wait_until(function() return sp:read_u32(0x502B9E00) == 0x53374E4B end, 20) then break end
  end
  wait_until(function() return sp:read_u32(0x502C0000) == 4 end, 150)
  local n, idx = sp:read_u32(0x502C0034), nil
  for i = 0, n - 1 do if cstr(sp:read_u32(0x502C3800 + 4 * i) + 0x40) == NAME then idx = idx or i end end
  E("menu: %d programs, %q at %s", n, NAME, tostring(idx))
  press("CPL_SEG0", ({ 0x02, 0x08, 0x20, 0x01, 0x04 })[idx % 5 + 1]); wait(1)
  press("CPR_SEG5", 0x01)
  wait(20)
  local keys = {}                                   -- $RATE_KEYS: keybed keys held while measuring ("C4 E4 ...")
  for name in (os.getenv("RATE_KEYS") or ""):gmatch("%S+") do
    for _, tag in ipairs({ ":KEYS0", ":KEYS1", ":KEYS2", ":KEYS3" }) do
      local p = M.ioport.ports[tag]
      if p then for _, x in pairs(p.fields) do if x.name == name then keys[#keys + 1] = x end end end
    end
  end
  for _, k in ipairs(keys) do k:set_value(1); wait(0.05) end
  if #keys > 0 then E("%d keys held", #keys) end
  wait(1)
  local h0, t0 = dm:read_u32(0x0F), now()
  for _ = 1, 10 do
    wait(1)
    local h, t = dm:read_u32(0x0F), now()
    E("MSGR7 %d: %.0f frames a second", h, (h - h0) / (t - t0))
    h0, t0 = h, t
  end
  f:close()
  M:exit()
end)
RATE_TICK = emu.add_machine_frame_notifier(function()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then E("ERROR %s", tostring(err)); f:close(); M:exit() end
  end
end)
