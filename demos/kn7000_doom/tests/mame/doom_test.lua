-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU General Public License v3 or later
--
-- doom_test.lua -- Doom in MAME, through the program menu (run_doom.sh OUT):
--   * DISK MENU, STYLE CONVERT, OK: the loader, the menu; the "Doom (Chocolate Doom)" row, RUN;
--   * every 2 s to $DOOM_OUT/events.txt: the converter's text, DOOM.BIN's state (gametic, gamestate 0 level,
--     1 intermission, 2 finale, 3 the title screens; demoplayback, episode and map) and the startup log's last line;
--     a snapshot every $DOOM_SNAP seconds (default 5);
--   * after $DOOM_SECONDS (default 120) EXIT held 2 s; then the converter's end (request 0xFE) and KN7APP's check of
--     the borrowed RAM (borrow.c: borrow_bad 0 when its copy in display memory came back as it went).
-- DOOM_TAPS=1: taps on the borrowed RAM log every write and read there from code that is neither KN7APP.BIN's nor
-- DOOM.BIN's (the firmware's), at most 40 of them, with the PC.
local M = manager.machine
local cpu = M.devices[":maincpu"]
local sp = cpu.spaces["program"]
local OUT = assert(os.getenv("DOOM_OUT"), "set DOOM_OUT")
local NAME = os.getenv("DOOM_NAME") or "Doom (Chocolate Doom)"
local SECONDS = tonumber(os.getenv("DOOM_SECONDS") or "") or 120
local SNAP = tonumber(os.getenv("DOOM_SNAP") or "") or 5
local menuV, V_NPROG, ORDER, PR_NAME = 0x502C0000, 0x502C0034, 0x502C3800, 0x40   -- the menu's (kn7menu menu.inc)

local SYM = {}
local SYMS = assert(os.getenv("DOOM_SYMS"), "set DOOM_SYMS")
for line in io.lines(SYMS) do
  local a, n = line:match("^(%x+) %a (%S+)$")
  if a then SYM[n] = tonumber(a, 16) end
end

local function now() return M.time:as_double() end
local ev = assert(io.open(OUT .. "/events.txt", "w"))
local function E(...) local s = string.format("%8.2f ", now()) .. string.format(...); print("doom_test: " .. s); ev:write(s, "\n"); ev:flush() end
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
local function u32(name) return SYM[name] and sp:read_u32(SYM[name]) or -1 end

-- the startup log's last line (main.c logLine[18][81], logRows)
local function logLast()
  local rows = u32("_logRows")
  if rows < 1 or not SYM["_logLine"] then return "" end
  local line = cstr(SYM["_logLine"] + 81 * (rows - 1), 80)
  if line == "" and rows > 1 then line = cstr(SYM["_logLine"] + 81 * (rows - 2), 80) end
  return line
end

local function state()
  return string.format("text %q; gametic %d gamestate %d demo %d E%dM%d; log %q", cstr(0x5031BC01, 60),
    u32("_gametic"), u32("_gamestate"), u32("_demoplayback"), u32("_gameepisode"), u32("_gamemap"), logLast())
end

-- the borrowed RAM (borrow.c) and the code that may touch it
local BORROW = { { 0x50050000, 0x5005EFFF }, { 0x50072000, 0x50080FFF }, { 0x50084000, 0x50090FFF },
                 { 0x500D5000, 0x5011DFFF }, { 0x50153000, 0x5015FFFF }, { 0x50161000, 0x50171FFF } }
local function ours(pc) return (pc >= 0x502BA000 and pc < 0x502C8000) or (pc >= 0x5031C000 and pc < 0x5037A000) end
TAPS, foreign = {}, 0
local function taps_on()
  for _, r in ipairs(BORROW) do
    for _, kind in ipairs({ "w", "r" }) do
      local install = kind == "w" and sp.install_write_tap or sp.install_read_tap
      TAPS[#TAPS + 1] = install(sp, r[1], r[2], "borrow" .. kind .. r[1], function(offset, data, mask)
        local pc = cpu.state["PC"].value
        if not ours(pc) then
          foreign = foreign + 1
          if foreign <= 40 then E("FOREIGN %s %08X by PC %08X", kind == "w" and "write" or "read", offset, pc) end
        end
      end)
    end
  end
end

local co = coroutine.create(function()
  wait_until(function() return now() > 30 end, 60)
  press("CPR_SEG1", 0x04); wait(6)                      -- DISK MENU
  press("CPL_SEG0", 0x20); wait(8)                      -- STYLE CONVERT
  for i = 1, 8 do                                       -- OK until the loader runs
    press("CPR_SEG6", 0x01)
    if wait_until(function() return sp:read_u32(0x502B9E00) == 0x53374E4B end, 20) then break end
  end
  local ok = wait_until(function() return sp:read_u32(menuV) == 4 end, 150)
  local n, idx = sp:read_u32(V_NPROG), nil
  for i = 0, n - 1 do if cstr(sp:read_u32(ORDER + 4 * i) + PR_NAME) == NAME then idx = idx or i end end
  E("menu: %s, %d programs, %q at %s", ok and "up" or "NOT UP", n, NAME, tostring(idx))
  if idx then
    for _ = 1, idx // 5 do press("CPR_SEG5", 0x20); wait(1) end
    press("CPL_SEG0", ({ 0x02, 0x08, 0x20, 0x01, 0x04 })[idx % 5 + 1]); wait(1)
    press("CPR_SEG5", 0x01)                             -- RUN
    E("RUN")
    local t0, nextLog, nextSnap, tapped = now(), now(), now() + SNAP, false
    while now() - t0 < SECONDS do
      if os.getenv("DOOM_TAPS") == "1" and not tapped and u32("_gametic") > 0 then taps_on(); tapped = true; E("taps on") end
      if now() >= nextLog then E("%s", state()); nextLog = nextLog + 2 end
      if now() >= nextSnap then M.video:snapshot(); nextSnap = nextSnap + SNAP end
      coroutine.yield()
    end
    E("EXIT held")
    local f = field("CPC_SEG11", 0x80); f:set_value(1); wait(2.5); f:set_value(0)
    local ended = wait_until(function() return sp:read_u8(0x5031BC00) == 0xFE end, 30)
    E("ended %s; borrow_bad %d; foreign accesses %d; %s", tostring(ended), u32("_borrow_bad"), foreign, state())
    wait(3)
    M.video:snapshot()
  end
  E("end")
  ev:close()
  M:exit()
end)
DOOM_TICK = emu.add_machine_frame_notifier(function()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then print("doom_test: ERROR " .. tostring(err)); M:exit() end
  end
end)
