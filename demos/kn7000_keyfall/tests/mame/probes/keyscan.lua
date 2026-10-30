-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU General Public License v3 or later
--
-- keyscan.lua -- which keys of the keybed can Keyfall read (run_keyfall.sh OUT probes/keyscan.lua)? With the game's
-- song list up, each key from E1 to G7 (MIDI 28-103: the KN7000's 76) is held 0.25 s, and KN7APP's notes[] (the MIDI
-- channels holding the note, from the keybed's MIDI OUT 1 ring) and the count of key events are read meanwhile: first
-- with the instrument as it starts, then after each of $KS_BUTTONS (default: PLAY CHORD OFF/ON) is pressed. Each pass
-- writes one line per key to events.txt and a summary: the keys read, and the lowest of them.
local M = manager.machine
local cpu = M.devices[":maincpu"]
local sp = cpu.spaces["program"]
local OUT = assert(os.getenv("KF_OUT"), "set KF_OUT")
local NAME = os.getenv("KF_NAME") or "Keyfall (piano game)"
local SONG = os.getenv("KF_SONG") or "TWINKLE.MID"
local PARTN = tonumber(os.getenv("KF_PART") or "") or 0
local TEMPO = tonumber(os.getenv("KF_TEMPO") or "") or 100
local ACCOMP = os.getenv("KF_ACCOMP") ~= "0"
local MODE = os.getenv("KF_MODE") or "PLAY"
local LEAD = tonumber(os.getenv("KF_LEAD") or "") or 0
local SKIP = tonumber(os.getenv("KF_SKIP") or "") or 0
local WRONG = tonumber(os.getenv("KF_WRONG") or "") or 0
local NOPRESS = os.getenv("KF_NOPRESS") == "1"
local SNAP = tonumber(os.getenv("KF_SNAP") or "") or 8
local menuV, V_NPROG, ORDER, PR_NAME = 0x502C0000, 0x502C0034, 0x502C3800, 0x40   -- the menu's (kn7menu menu.inc)

local SYM = {}
for line in io.lines((assert(os.getenv("KF_SYMS"), "set KF_SYMS"))) do
  local a, n = line:match("^(%x+) %a (%S+)$")
  if a then SYM[n] = SYM[n] or tonumber(a, 16) end
end

local function now() return M.time:as_double() end
local ev = assert(io.open(OUT .. "/events.txt", "w"))
local function E(...) local s = string.format("%8.3f ", now()) .. string.format(...); print("keyfall_test: " .. s); ev:write(s, "\n"); ev:flush() end
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
local function press(tag, mask) local f = field(tag, mask); f:set_value(1); wait(0.3); f:set_value(0); wait(0.3) end
local function u32(name) return SYM[name] and sp:read_u32(SYM[name]) or -1 end
local function s32(name) return SYM[name] and sp:read_i32(SYM[name]) or -1 end
local fails, checks = 0, 0
local function CHECK(what, ok, got)
  checks = checks + 1
  E("CHECK %s %s%s", ok and "ok  " or "FAIL", what, got and (" (" .. tostring(got) .. ")") or "")
  if not ok then fails = fails + 1 end
end
local function snap(what) M.video:snapshot(); E("snapshot: %s", what) end

-- the soft keys (kn7.h BTN_LCD*) and EXIT
local L = { { "CPL_SEG0", 0x02 }, { "CPL_SEG0", 0x08 }, { "CPL_SEG0", 0x20 }, { "CPL_SEG0", 0x01 }, { "CPL_SEG0", 0x04 } }
local R = { { "CPR_SEG5", 0x10 }, { "CPR_SEG5", 0x20 }, { "CPR_SEG7", 0x01 }, { "CPR_SEG6", 0x01 }, { "CPR_SEG5", 0x01 } }
local function soft(t, i) press(t[i][1], t[i][2]) end

-- the keybed's fields by MIDI note (Doom's doom_test.lua)
local NOTE = { C = 0, D = 2, E = 4, F = 5, G = 7, A = 9, B = 11 }
local KEYN = {}
for _, tag in ipairs({ ":KEYS0", ":KEYS1", ":KEYS2", ":KEYS3", ":KEYS4" }) do
  local p = M.ioport.ports[tag]
  if p then
    for _, f in pairs(p.fields) do
      local l, s1, o, s2 = f.name:gsub("^Key ", ""):match("^(%a)(#?)(%d)(#?)$")
      if l then KEYN[12 * (tonumber(o) + 1) + NOTE[l] + ((s1 .. s2) == "#" and 1 or 0)] = f end
    end
  end
end

local co = coroutine.create(function()
  wait_until(function() return now() > 30 end, 60)
  press("CPR_SEG1", 0x04); wait(6)                      -- DISK MENU
  press("CPL_SEG0", 0x20); wait(8)                      -- STYLE CONVERT
  for _ = 1, 8 do                                       -- OK until the loader runs
    press("CPR_SEG6", 0x01)
    if wait_until(function() return sp:read_u32(0x502B9E00) == 0x53374E4B end, 20) then break end
  end
  local ok = wait_until(function() return sp:read_u32(menuV) == 4 end, 150)
  local n, idx = sp:read_u32(V_NPROG), nil
  for i = 0, n - 1 do if cstr(sp:read_u32(ORDER + 4 * i) + PR_NAME) == NAME then idx = idx or i end end
  E("menu: %s, %d programs, %q at %s", ok and "up" or "NOT UP", n, NAME, tostring(idx))
  if not idx then E("end"); ev:close(); M:exit(); return end
  for _ = 1, idx // 5 do press("CPR_SEG5", 0x20); wait(1) end
  press("CPL_SEG0", ({ 0x02, 0x08, 0x20, 0x01, 0x04 })[idx % 5 + 1]); wait(1)
  press("CPR_SEG5", 0x01)                               -- RUN
  local runAt = now()
  E("RUN")
  CHECK("the songs are up", wait_until(function() return SYM["_menu_screen"] and u32("_menu_screen") == 1 end, 240),
    string.format("%.1f s after RUN, text %q", now() - runAt, cstr(0x5031BC01, 60)))
  local buttons = { { "nothing", nil, nil } }
  for b in (os.getenv("KS_BUTTONS") or "PLAY CHORD OFF/ON=CPL_SEG7:0x08"):gmatch("[^,]+") do
    local name, tag, mask = b:match("^(.-)=(%S+):(%S+)$")
    buttons[#buttons + 1] = { name, tag, tonumber(mask) }
  end
  for _, b in ipairs(buttons) do
    if b[2] then press(b[2], b[3]); wait(2); E("pressed %s", b[1]) end
    local readable, lowest = 0, nil
    for n = 28, 103 do
      local f = KEYN[n]
      if f then
        local ev0 = sp:read_u32(SYM["_kev_wr"])
        f:set_value(1); wait(0.25)
        local mask = sp:read_u16(SYM["_notes"] + 2 * n)
        f:set_value(0); wait(0.2)
        local evs = sp:read_u32(SYM["_kev_wr"]) - ev0
        E("%s: key %d: notes[] %04X while held, %d key events", b[1], n, mask, evs)
        if (mask & 7) ~= 0 then readable = readable + 1; lowest = lowest or n end
      else
        E("%s: key %d: no keybed field", b[1], n)
      end
    end
    E("SUMMARY %s: %d of 76 keys read, the lowest %s", b[1], readable, tostring(lowest))
  end
  E("end")
  ev:close()
  M:exit()
end)
KF_TICK = emu.add_machine_frame_notifier(function()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then print("keyscan: ERROR " .. tostring(err)); E("ERROR %s", tostring(err)); M:exit() end
  end
end)
