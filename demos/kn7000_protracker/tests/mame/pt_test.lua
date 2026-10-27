-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU General Public License v3 or later
--
-- pt_test.lua -- ProTracker in MAME, through the program menu (run_pt.sh OUT):
--   * DISK MENU, STYLE CONVERT, OK: the loader, the menu; the "ProTracker 2.3D" row, RUN;
--   * PT.BIN loads the first module of MODS.TXT, sends its samples to the DSP and plays it;
--   * each second to $PT_OUT/events.txt: the converter's text, the DSP's state (KN7APP.BIN's dsp: state, fail,
--     ready, the voice commands sent), and the replayer's (PT.BIN's song: position, pattern, row, speed, BPM;
--     editor.songPlaying; the status line); a snapshot every 4 s;
--   * then LCDR 2 (the next module) every $PT_EACH seconds (default 20) until $PT_MODULES modules (default 2) have
--     played; then STOP (LCDL 3), and the end.
-- The sound is in $PT_OUT/sound.wav (-wavwrite): tests/mame/sound_report.py says when it is silent.
local M = manager.machine
local sp = M.devices[":maincpu"].spaces["program"]
local OUT = assert(os.getenv("PT_OUT"), "set PT_OUT")
local NAME = os.getenv("PT_NAME") or "ProTracker 2.3D"
local EACH = tonumber(os.getenv("PT_EACH") or "") or 20
local MODULES = tonumber(os.getenv("PT_MODULES") or "") or 2
local menuV, V_NPROG, ORDER, PR_NAME = 0x502C0000, 0x502C0034, 0x502C3800, 0x40   -- the menu's (kn7menu menu.inc)

-- the builds' symbols (run_pt.sh: nm of kn7app.elf and pt.elf)
local SYM = {}
local SYMS = assert(os.getenv("PT_SYMS"), "set PT_SYMS")
for line in io.lines(SYMS) do
  local a, n = line:match("^(%x+) %a (%S+)$")
  if a then SYM[n] = tonumber(a, 16) end
end
local DSP, SONG, EDITOR, UI = SYM["_dsp"], SYM["_song"], SYM["_editor"], SYM["_ui"]

local function now() return M.time:as_double() end
local ev = assert(io.open(OUT .. "/events.txt", "w"))
local function E(...) local s = string.format("%8.2f ", now()) .. string.format(...); print("pt_test: " .. s); ev:write(s, "\n"); ev:flush() end
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

-- module_t (pt2_structs.h, PT2_KN7) as mn10300-elf-gcc lays it out (tests/mame/offsets.c): speed 0x10, row 0x14,
-- header.name 0x16, currRow 0xAE4, currBPM 0xAEC, currPos 0xAF0, currPattern 0xAF2; editor_t: songPlaying 0x2E,
-- currMode 0x2170
local function state()
  local d = DSP and string.format("dsp state %d fail %d ready %d flushes %d busy %d bank %d ms",
    sp:read_u32(DSP + 36), sp:read_u32(DSP + 40), sp:read_u32(DSP + 44), sp:read_u32(DSP + 164),
    sp:read_u32(DSP + 160), sp:read_u32(DSP + 152)) or "dsp ?"
  local s = "song ?"
  if SONG and sp:read_u32(SONG) ~= 0 then
    local m = sp:read_u32(SONG)
    s = string.format("song %q pos %d patt %d row %d speed %d bpm %d playing %d", cstr(m + 0x16, 20),
      sp:read_u16(m + 0xAF0), sp:read_u16(m + 0xAF2), sp:read_u8(m + 0xAE4), sp:read_u32(m + 0x10),
      sp:read_u32(m + 0xAEC), EDITOR and sp:read_u8(EDITOR + 0x2E) or -1)
  end
  if UI then s = s .. string.format(", status %q", cstr(UI, 18)) end  -- ui_t's statusMessage, its first field
  local L = SYM["_ptLate"]
  if L then  -- main.c: the ticks run under 1, 2, 5, 10, 20, 50 ms late and later; the latest (ms)
    local t = {}
    for i = 0, 6 do t[#t + 1] = tostring(sp:read_u32(L + 4 * i)) end
    s = s .. string.format(", late %s max %.1f ms", table.concat(t, "/"), sp:read_u32(SYM["_ptLateMax"]) / 2001)
  end
  return d .. "; " .. s
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
    local t0 = now()
    local played = 1
    local nextSnap, nextMod = t0 + 4, t0 + EACH
    while true do
      wait(1)
      E("request %02X, text %q; %s", sp:read_u8(0x5031BC00), cstr(0x5031BC01, 60), state())
      if now() >= nextSnap then M.video:snapshot(); nextSnap = nextSnap + 4 end
      if now() >= nextMod then
        if played >= MODULES then break end
        press("CPR_SEG5", 0x20)                         -- LCDR 2: the next module
        E("LCDR 2: the next module")
        played = played + 1
        nextMod = now() + EACH
      end
    end
    press("CPL_SEG0", 0x20)                             -- LCDL 3: STOP
    E("LCDL 3: STOP")
    wait(2)
    M.video:snapshot()
    E("request %02X, text %q; %s", sp:read_u8(0x5031BC00), cstr(0x5031BC01, 60), state())
  end
  E("end")
  ev:close()
  M:exit()
end)
PT_TICK = emu.add_machine_frame_notifier(function()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then print("pt_test: ERROR " .. tostring(err)); M:exit() end
  end
end)
