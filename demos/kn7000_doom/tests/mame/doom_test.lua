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
-- DOOM_PLAY=1, once the title screen is up: MENU (Esc), C5 three times (New Game, the episode, the skill), and once
-- E1M1 runs: G#4 held (forward), F4 held (turn left), C5 three times (fire), C6 (the fist), E5 twice (the automap),
-- each CHECKed against DOOM.BIN's memory (the player's position, angle, bullets and weapon: tests/mame/offsets.txt),
-- a snapshot after each.
-- DOOM_TAPS=1: once the title screen is up, taps on the borrowed RAM log every write there from code that is neither
-- KN7APP.BIN's nor DOOM.BIN's (the firmware's), at most 40 of them, with the PC (reads too with DOOM_TAPS_READ=1: slow,
-- as every read of the game's there runs Lua).
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

-- the keybed's fields by MIDI note ("Key C#3" is 49)
local NOTE = { C = 0, D = 2, E = 4, F = 5, G = 7, A = 9, B = 11 }
local KEYN = {}
for _, tag in ipairs({ ":KEYS0", ":KEYS1", ":KEYS2", ":KEYS3", ":KEYS4" }) do
  local p = M.ioport.ports[tag]
  if p then
    for _, f in pairs(p.fields) do
      local l, s1, o, s2 = f.name:gsub("^Key ", ""):match("^(%a)(#?)(%d)(#?)$")  -- "Key C#3", "C#3" or "C3#"
      if l then KEYN[12 * (tonumber(o) + 1) + NOTE[l] + ((s1 .. s2) == "#" and 1 or 0)] = f end
    end
  end
end
local function key(n, s) assert(KEYN[n], "no keybed field for note " .. n); KEYN[n]:set_value(1); wait(s or 0.3); KEYN[n]:set_value(0); wait(0.3) end

-- the player's state (tests/mame/offsets.txt: offsets.c as mn10300-elf-gcc lays the structures out)
local OFF = {}
for line in io.lines(os.getenv("DOOM_OFFSETS")) do local n, v = line:match("^(%S+) (%d+)$"); if n then OFF[n] = tonumber(v) end end
local function player()
  local p = SYM["_players"]
  local mo = sp:read_u32(p + OFF.off_player_mo)
  local x, y, a = 0, 0, 0
  if mo ~= 0 then
    x, y, a = sp:read_i32(mo + OFF.off_mobj_x) / 65536, sp:read_i32(mo + OFF.off_mobj_y) / 65536, sp:read_u32(mo + OFF.off_mobj_angle)
  end
  return { x = x, y = y, angle = a, health = sp:read_i32(p + OFF.off_player_health),
           bullets = sp:read_i32(p + OFF.off_player_ammo), weapon = sp:read_i32(p + OFF.off_player_readyweapon) }
end
local fails, checks = 0, 0
local function CHECK(what, ok, got)
  checks = checks + 1
  E("CHECK %s %s%s", ok and "ok  " or "FAIL", what, got and (" (" .. tostring(got) .. ")") or "")
  if not ok then fails = fails + 1 end
end

-- the startup log's last line (main.c logLine[18][81], logRows)
local function logLast()
  local rows = u32("_logRows")
  if rows < 1 or not SYM["_logLine"] then return "" end
  local line = cstr(SYM["_logLine"] + 81 * (rows - 1), 80)
  if line == "" and rows > 1 then line = cstr(SYM["_logLine"] + 81 * (rows - 2), 80) end
  return line
end

local lastFrames, lastT = 0, 0
local function state()
  local f, t = u32("_kn7Frames"), now()
  local fps = t > lastT and (f - lastFrames) / (t - lastT) or 0
  lastFrames, lastT = f, t
  return string.format("text %q; gametic %d gamestate %d demo %d E%dM%d; frames %d (%.1f/s); log %q", cstr(0x5031BC01, 60),
    u32("_gametic"), u32("_gamestate"), u32("_demoplayback"), u32("_gameepisode"), u32("_gamemap"), f, fps, logLast())
end

-- the borrowed RAM (borrow.c) and the code that may touch it
local BORROW = { { 0x50050000, 0x5005EFFF }, { 0x50072000, 0x50080FFF }, { 0x50084000, 0x50090FFF },
                 { 0x500D5000, 0x5011DFFF }, { 0x50153000, 0x5015FFFF }, { 0x50161000, 0x50171FFF } }
local function ours(pc) return (pc >= 0x502BA000 and pc < 0x502C8000) or (pc >= 0x5031C000 and pc < 0x5037A000) end
TAPS, foreign = {}, 0
local function taps_on()
  for _, r in ipairs(BORROW) do
    for _, kind in ipairs(os.getenv("DOOM_TAPS_READ") == "1" and { "w", "r" } or { "w" }) do
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
    if os.getenv("DOOM_KEYSCAN") == "1" then           -- which keys reach KN7APP's notes[] (held 1 s each)
      wait_until(function() return u32("_gamestate") == 3 or u32("_demoplayback") == 1 end, 300)
      for _, n in ipairs({ 48, 55, 56, 59, 60, 62, 64, 65, 67, 68, 69, 71, 72, 74, 76, 79, 84, 88, 95 }) do
        KEYN[n]:set_value(1); wait(0.6)
        local mid = sp:read_u16(SYM["_notes"] + 2 * n)
        KEYN[n]:set_value(0); wait(0.6)
        E("key %d: notes[] while held %04X, after %04X", n, mid, sp:read_u16(SYM["_notes"] + 2 * n))
      end
    end
    if os.getenv("DOOM_PLAY") == "1" then
      CHECK("the title screen", wait_until(function() return u32("_gamestate") == 3 or u32("_demoplayback") == 1 end, 300), state())
      wait(2)
      press("CPL_SEG0", 0x02)                           -- LCDL 1: MENU
      CHECK("MENU: the menu is up", wait_until(function() return u32("_menuactive") == 1 end, 10))
      M.video:snapshot()
      for _, what in ipairs({ "New Game", "the episode", "the skill" }) do key(72); wait(1); E("C5: %s", what) end
      CHECK("E1M1 starts", wait_until(function() return u32("_gamestate") == 0 and u32("_demoplayback") == 0 and
        u32("_gamemap") == 1 and u32("_menuactive") == 0 end, 60), state())
      local loading, tic0 = now(), u32("_gametic")       -- (gamestate is GS_LEVEL from before P_SetupLevel reads the map)
      CHECK("E1M1 runs", wait_until(function() return u32("_gametic") >= tic0 + 35 end, 120), state())
      E("E1M1 loaded and running after %.1f s", now() - loading)
      wait(1)
      M.video:snapshot()
      local p0 = player()
      E("player at %.1f, %.1f angle %08X health %d bullets %d weapon %d", p0.x, p0.y, p0.angle, p0.health, p0.bullets, p0.weapon)
      key(68, 2)                                        -- G#4: forward
      local p1 = player()
      CHECK("G#4: forward", math.abs(p1.x - p0.x) + math.abs(p1.y - p0.y) > 32, string.format("%.1f, %.1f", p1.x, p1.y))
      M.video:snapshot()
      key(65, 1)                                        -- F4: turn left
      local p2 = player()
      CHECK("F4: turned", p2.angle ~= p1.angle, string.format("%08X", p2.angle))
      M.video:snapshot()
      for _ = 1, 3 do key(72, 0.2); wait(0.5) end       -- C5: fire
      local p3 = player()
      CHECK("C5: fired", p3.bullets < p2.bullets, p2.bullets .. " -> " .. p3.bullets)
      M.video:snapshot()
      key(84); wait(1)                                  -- C6: the fist
      CHECK("C6: the fist", player().weapon == 0, player().weapon)
      key(76); wait(1); M.video:snapshot()              -- E5: the automap
      CHECK("E5: the automap", u32("_automapactive") == 1, u32("_automapactive"))
      key(76); wait(1)
      E("play: %d checks, %d failed", checks, fails)
    end
    local t0, nextLog, nextSnap, tapped = now(), now(), now() + SNAP, false
    while now() - t0 < SECONDS do
      if os.getenv("DOOM_TAPS") == "1" and not tapped and u32("_gamestate") == 3 then taps_on(); tapped = true; E("taps on") end
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
