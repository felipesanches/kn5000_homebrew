-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU General Public License v3 or later
--
-- pt_test.lua -- ProTracker in MAME, through the program menu (run_pt.sh OUT):
--   * DISK MENU, STYLE CONVERT, OK: the loader, the menu; the "ProTracker 2.3D" row, RUN;
--   * PT.BIN loads the first module of its folder, sends its samples to the DSP and plays it;
--   * each second to $PT_OUT/events.txt: the converter's text, the DSP's state (KN7APP.BIN's dsp: state, fail,
--     ready, the voice commands sent), and the replayer's (PT.BIN's song: position, pattern, row, speed, BPM;
--     editor.songPlaying; the status line); a snapshot every 4 s;
--   * then LCDR 2 (the next module) every $PT_EACH seconds (default 20) until $PT_MODULES modules (default 2) have
--     played; then STOP (LCDL 3), and the end.
-- PT_EDIT=1 instead, once the first module has played 10 s: LCDL 5 (the spectrum analyzer), MUTE UP 1 (channel 1
-- muted), STOP, EDIT (LCDL 4), the keybed's C3, E3 and G3 (into the pattern on channel 1 from the row shown, read back from
-- PT.BIN's memory and logged), LCDL 2 (the pattern plays), a snapshot after each; then LCDR 4, 4, 3 (the sample),
-- LCDR 5 twice (all muted, none), LCDL 1 and PAGE DOWN (the position), LCDR 1 (the module before), each logged.
-- The sound is in $PT_OUT/sound.wav (-wavwrite): tests/mame/sound_report.py says when it is silent.
local M = manager.machine
local sp = M.devices[":maincpu"].spaces["program"]
local OUT = assert(os.getenv("PT_OUT"), "set PT_OUT")
local NAME = os.getenv("PT_NAME") or "ProTracker 2.3D"
local EACH = tonumber(os.getenv("PT_EACH") or "") or 20
local MODULES = tonumber(os.getenv("PT_MODULES") or "") or 2
local EDIT = os.getenv("PT_EDIT") == "1"
local EDIT2 = os.getenv("PT_EDIT") == "2"  -- the editor (PLAN-EDITING.md): every step checked in PT.BIN's memory
local RISK = os.getenv("PT_RISK") == "1"  -- does the firmware still act on TRANSPOSE, OCTAVE ... (PLAN-EDITING.md, "Risks")
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
local KEY = {}
for _, tag in ipairs({ ":KEYS0", ":KEYS1", ":KEYS2", ":KEYS3" }) do
  local p = M.ioport.ports[tag]
  if p then for _, f in pairs(p.fields) do KEY[f.name] = f end end
end

-- note_t (6 bytes: param, sample, command, -, period) of row r, channel c of the current pattern (module_t.patterns at
-- 0x954, currPattern at 0xAF2: tests/mame/offsets.c)
local function note(r, c)
  local m = sp:read_u32(SONG)
  local patt = sp:read_u32(m + 0x954 + 4 * sp:read_u16(m + 0xAF2))
  local a = patt + (r * 4 + c) * 6
  return string.format("period %d sample %d cmd %X%02X", sp:read_u16(a + 4), sp:read_u8(a + 1), sp:read_u8(a + 2), sp:read_u8(a))
end

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
  if SYM["_moduleAt"] then s = s .. string.format(", file %s", cstr(SYM["_names"] + 13 * sp:read_u32(SYM["_moduleAt"]), 13)) end
  if SYM["_panel_frames"] then  -- entry.c: switch frames kept from the firmware; of them, decoded by it first
    s = s .. string.format(", panel frames %d late %d", sp:read_u32(SYM["_panel_frames"]), sp:read_u32(SYM["_panel_late"]))
    local nl = sp:read_u32(SYM["_panel_late"])
    for i = 0, (nl < 8 and nl or 8) - 1 do  -- which: ADDR=DATA (DATA 00 is a release: harmless)
      s = s .. string.format(" %02X=%02X", sp:read_u8(SYM["_panel_late_log"] + 2 * i), sp:read_u8(SYM["_panel_late_log"] + 2 * i + 1))
    end
  end
  local L = SYM["_ptLate"]
  if L then  -- main.c: the ticks run under 1, 2, 5, 10, 20, 50 ms late and later; the latest (ms)
    local t = {}
    for i = 0, 6 do t[#t + 1] = tostring(sp:read_u32(L + 4 * i)) end
    s = s .. string.format(", late %s max %.1f ms", table.concat(t, "/"), sp:read_u32(SYM["_ptLateMax"]) / 2001)
  end
  return d .. "; " .. s
end

-- disk.c's list: the modules the folder holds, as PT.BIN read it off the card (PfHost::list), space-separated
local function modules()
  local n, t = SYM["_moduleCount"] and sp:read_u32(SYM["_moduleCount"]) or 0, {}
  for i = 0, n - 1 do t[#t + 1] = cstr(SYM["_names"] + 13 * i, 13) end
  return table.concat(t, " ")
end
local LIST = os.getenv("PT_LIST") or "INTRO.MOD MOD2.MOD TABLE1.MOD TABLE2.MOD TABLE3.MOD TABLE4.MOD"

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
    if EDIT2 then
      local CUR = SYM["_cursor"]
      local fails = 0
      local function CHECK(what, ok, got) E("CHECK %s %s%s", ok and "ok  " or "FAIL", what, got and (" (" .. tostring(got) .. ")") or ""); if not ok then fails = fails + 1 end end
      local function m() return sp:read_u32(SONG) end
      local function row() return sp:read_u8(m() + 0xAE4) end
      local function cell(r, c)  -- period, sample, command, param of row r, channel c of the edited pattern
        local patt = sp:read_u32(m() + 0x954 + 4 * sp:read_u16(m() + 0xAF2))
        local a = patt + (r * 4 + c) * 6
        return sp:read_u16(a + 4), sp:read_u8(a + 1), sp:read_u8(a + 2), sp:read_u8(a)
      end
      local function mode() return sp:read_u8(EDITOR + 0x2170) end
      local function tap(tag, mask)
        if not os.getenv("PT_TRACE") then press(tag, mask); wait(0.4); return end
        -- PT_TRACE=1: the switch frames KN7APP took during the tap (time, frames so far, the tag's byte of panel_real)
        local idx = (tag:sub(1, 3) == "CPR" and 0 or 0x30) + tonumber(tag:match("SEG(%d+)"))
        local F, R = SYM["_panel_frames"], SYM["_panel_real"] + idx
        local f0, t, last = sp:read_u32(F), {}, nil
        local f = field(tag, mask); f:set_value(1)
        local t0, released = now(), false
        while now() < t0 + 0.7 do
          if not released and now() >= t0 + 0.3 then f:set_value(0); released = true end
          local k = string.format("%d:%02X", sp:read_u32(F) - f0, sp:read_u8(R))
          if k ~= last then t[#t + 1] = string.format("%.3f=%s", now() - t0, k); last = k end
          coroutine.yield()
        end
        E("tap %s %02X: %s", tag, mask, table.concat(t, " "))
      end
      local function keys(list) for _, k in ipairs(list) do KEY[k]:set_value(1) end; wait(0.4); for _, k in ipairs(list) do KEY[k]:set_value(0) end; wait(0.5) end
      wait_until(function() return SONG and sp:read_u32(SONG) ~= 0 and EDITOR and sp:read_u8(EDITOR + 0x2E) == 1 end, 60)
      wait(3)
      E("playing: %s", state()); M.video:snapshot()
      CHECK("the folder's modules, sorted", modules() == LIST, modules())
      tap("CPL_SEG0", 0x10)                                    -- START/STOP: stop
      tap("CPR_SEG0", 0x08)                                    -- EASY REC: the edit mode
      CHECK("EASY REC: edit mode", mode() == 1, mode())
      tap("CPL_SEG4", 0x01)                                    -- VARIATION 3: row 32
      CHECK("VARIATION 3: row 32", row() == 32, row())
      tap("CPC_SEG8", 0x40)                                    -- MUTE UP 6: the cursor to channel 1
      local s0 = sp:read_u8(EDITOR + 0x2160)
      keys({ "C3" })                                           -- a note
      local p, sm = cell(32, 0)
      CHECK("C3 into row 32: C-2 (428) and the sample", p == 428 and sm == s0 + 1, p .. "/" .. sm)
      CHECK("the cursor on by the edit skip", row() == 33, row())
      tap("CPC_SEG11", 0x08)                                   -- MUTE DOWN 16: the edit skip 1 -> 0
      CHECK("MUTE DOWN 16: skip 0", sp:read_u8(EDITOR + 0x2184) == 0, sp:read_u8(EDITOR + 0x2184))
      for _ = 1, 3 do tap("CPR_SEG1", 0x02) end               -- PART SELECT RIGHT 1 x3: the command column
      CHECK("PART SELECT RIGHT 1 x3: the command column", sp:read_u8(CUR + 1) == 3, sp:read_u8(CUR + 1))
      tap("CPR_SEG0", 0x20)                                    -- SYNTH: C
      tap("CPR_SEG1", 0x02)
      tap("CPR_SEG0", 0x10)                                    -- STRINGS & VOCAL: 4
      tap("CPR_SEG1", 0x02)
      tap("CPR_SEG4", 0x10)                                    -- PIANO: 0
      local _, _, c, q = cell(33, 0)
      CHECK("SYNTH, STRINGS, PIANO: C40 in row 33", c == 0xC and q == 0x40, string.format("%X%02X", c, q))
      tap("CPR_SEG6", 0x08)                                    -- SOUND EXPLORER: delete the effect
      _, _, c, q = cell(33, 0)
      CHECK("SOUND EXPLORER: the effect deleted", c == 0 and q == 0, string.format("%X%02X", c, q))
      tap("CPL_SEG6", 0x08)                                    -- PADS BANK: undo
      _, _, c, q = cell(33, 0)
      CHECK("PADS BANK: undone", c == 0xC and q == 0x40, string.format("%X%02X", c, q))
      tap("CPR_SEG7", 0x40); tap("CPR_SEG0", 0x02)             -- SET, PANEL MEMORY 1: store C40
      CHECK("SET, PANEL MEMORY 1: macro 1 = C40", sp:read_u16(EDITOR + 0x218E) == 0xC40, string.format("%X", sp:read_u16(EDITOR + 0x218E)))
      tap("CPL_SEG4", 0x01)                                    -- VARIATION 3: row 32
      tap("CPC_SEG8", 0x40)                                    -- the cursor to channel 1, the note column
      tap("CPR_SEG3", 0x01)                                    -- TRANSPOSE +
      p = cell(32, 0)
      CHECK("TRANSPOSE +: C#2 (404)", p == 404, p)
      tap("CPR_SEG7", 0x02)                                    -- R1/R2 OCTAVE -
      p = cell(32, 0)
      CHECK("R1/R2 OCTAVE -: C#1 (808)", p == 808, p)
      tap("CPL_SEG3", 0x10)                                    -- FILL IN 1: insert a row
      local p32, p33 = cell(32, 0), cell(33, 0)
      CHECK("FILL IN 1: row 32 empty, C#1 now in row 33", p32 == 0 and p33 == 808, p32 .. "/" .. p33)
      tap("CPL_SEG3", 0x04)                                    -- FILL IN 2: delete the row
      p = cell(32, 0)
      CHECK("FILL IN 2: C#1 back in row 32", p == 808, p)
      tap("CPL_SEG4", 0x80)                                    -- PAD 2: copy the track
      tap("CPC_SEG9", 0x04)                                    -- MUTE UP 8: the cursor to channel 2
      CHECK("MUTE UP 8: channel 2", sp:read_u8(CUR + 3) == 1, sp:read_u8(CUR + 3))
      tap("CPL_SEG4", 0x20)                                    -- PAD 3: paste the track
      local a1, b1, c1, d1 = cell(33, 0)
      local a2, b2, c2, d2 = cell(33, 1)
      CHECK("PAD 2, PAD 3: the track copied to channel 2", a1 == a2 and b1 == b2 and c1 == c2 and d1 == d2 and c2 == 0xC, string.format("%d %d %X%02X", a2, b2, c2, d2))
      tap("CPC_SEG8", 0x40)                                    -- back to channel 1
      tap("CPL_SEG2", 0x08)                                    -- BALLAD: sample 3
      CHECK("BALLAD: sample 3", sp:read_u8(EDITOR + 0x2160) == 2, sp:read_u8(EDITOR + 0x2160) + 1)
      tap("CPL_SEG7", 0x08)                                    -- AUTO PLAY CHORD OFF/ON: chord entry
      tap("CPL_SEG4", 0x10)                                    -- VARIATION 1: row 0
      keys({ "C3", "E3", "G3" })                               -- a chord
      local n1, s1 = cell(0, 0)
      local n2 = cell(0, 1)
      local n3 = cell(0, 2)
      CHECK("C3 E3 G3 as a chord: C-2 E-2 G-2 across channels 1-3, sample 3", n1 == 428 and n2 == 339 and n3 == 285 and s1 == 3, n1 .. " " .. n2 .. " " .. n3 .. " s" .. s1)
      local len0 = sp:read_u16(m() + 0x12C)
      tap("CPC_SEG5", 0x40)                                    -- MUTE UP 2: PATT of this position +1
      tap("CPC_SEG8", 0x01)                                    -- MUTE UP 3: LEN + 1
      CHECK("MUTE UP 3: the song one position longer", sp:read_u16(m() + 0x12C) == len0 + 1, sp:read_u16(m() + 0x12C))
      local pos0 = sp:read_u16(m() + 0xAF0)
      tap("CPC_SEG5", 0x20)                                    -- MUTE DOWN 1: down the position list
      CHECK("MUTE DOWN 1: the next position", sp:read_u16(m() + 0xAF0) == pos0 + 1, sp:read_u16(m() + 0xAF0))
      tap("CPC_SEG5", 0x10)                                    -- MUTE UP 1: back up it
      CHECK("MUTE UP 1: the position before", sp:read_u16(m() + 0xAF0) == pos0, sp:read_u16(m() + 0xAF0))
      tap("CPR_SEG4", 0x02)                                    -- CONDUCTOR RIGHT 2: insert a position
      CHECK("CONDUCTOR RIGHT 2: a position inserted", sp:read_u16(m() + 0x12C) == len0 + 2, sp:read_u16(m() + 0x12C))
      local hold = field("CPC_SEG11", 0x40); hold:set_value(1); wait(0.3)
      tap("CPR_SEG4", 0x02)                                    -- SHIFT + CONDUCTOR RIGHT 2: delete it
      hold:set_value(0); wait(0.3)
      CHECK("SHIFT + CONDUCTOR RIGHT 2: deleted", sp:read_u16(m() + 0x12C) == len0 + 1, sp:read_u16(m() + 0x12C))
      M.video:snapshot()
      tap("CPC_SEG5", 0x02); wait(1); M.video:snapshot()       -- HELP
      for _ = 1, 4 do tap("CPC_SEG11", 0x20); wait(1); M.video:snapshot() end  -- PAGE DOWN: its pages
      tap("CPC_SEG11", 0x80); wait(1)                          -- EXIT: back
      tap("CPR_SEG7", 0x01); wait(1)                           -- LCDR 3: the Disk Op.
      M.video:snapshot()
      tap("CPL_SEG0", 0x04); wait(1)                           -- LCDL 5: SAVE AS NEW
      M.video:snapshot()
      tap("CPL_SEG0", 0x10)                                    -- START/STOP: yes
      wait_until(function() return cstr(UI, 18):find("SAVED") ~= nil end, 60)
      E("saved: %s", state())
      CHECK("SAVE AS NEW: the module saved", cstr(UI, 18):find("MODULE SAVED") ~= nil, cstr(UI, 18))
      CHECK("SAVE AS NEW: PTNEW01.MOD in the list", (" " .. modules() .. " "):find(" PTNEW01.MOD ", 1, true) ~= nil, modules())
      tap("CPC_SEG11", 0x80); wait(0.5)                        -- EXIT: the Disk Op. closed
      tap("CPC_SEG11", 0x80); wait(0.5)                        -- EXIT: the edit mode off
      CHECK("EXIT: the edit mode off", mode() == 0, mode())
      M.video:snapshot()
      E("CHECKS: %d failed", fails)
      E("end")
      ev:close()
      M:exit()
      return
    end
    if RISK then
      wait_until(function() return SONG and sp:read_u32(SONG) ~= 0 and EDITOR and sp:read_u8(EDITOR + 0x2E) == 1 end, 60)
      wait(4)
      local NOTES = SYM["_notes"]
      local function held() local t = {} for n = 0, 127 do if sp:read_u16(NOTES + 2 * n) ~= 0 then t[#t + 1] = n end end return table.concat(t, ",") end
      local function key(k) KEY[k]:set_value(1); wait(0.4); local h = held(); KEY[k]:set_value(0); wait(0.4); return h end
      E("RISK C4 alone: notes held %s", key("C4"))
      local function try(name, tag, mask) press(tag, mask); wait(1); E("RISK after %s: C4 gives notes %s; %s", name, key("C4"), state()) end
      try("TRANSPOSE +", "CPR_SEG3", 0x01)
      try("TRANSPOSE +", "CPR_SEG3", 0x01)
      try("R1/R2 OCTAVE +", "CPR_SEG6", 0x02)
      try("PIANO (sound group)", "CPR_SEG4", 0x10)
      try("8 & 16 BEAT (rhythm group)", "CPL_SEG2", 0x80)
      try("PANEL MEMORY 1", "CPR_SEG0", 0x02)
      try("PANEL MEMORY SET", "CPR_SEG7", 0x40)
      try("FADE IN", "CPL_SEG3", 0x20)
      try("PERFORMANCE PAD 1", "CPL_SEG6", 0x10)
      try("EASY REC", "CPR_SEG0", 0x08)
      try("SEQUENCER PLAY", "CPR_SEG1", 0x08)
      M.video:snapshot()
      E("end")
      ev:close()
      M:exit()
      return
    end
    if EDIT then
      wait_until(function() return SONG and sp:read_u32(SONG) ~= 0 and EDITOR and sp:read_u8(EDITOR + 0x2E) == 1 end, 60)
      wait(10)
      E("playing: %s", state()); M.video:snapshot()
      press("CPL_SEG0", 0x04); wait(2)                  -- LCDL 5: the spectrum analyzer
      E("LCDL 5 (spectrum)"); M.video:snapshot()
      press("CPC_SEG5", 0x10); wait(2)                  -- MUTE UP 1
      E("MUTE UP 1: %s", state()); M.video:snapshot()
      press("CPL_SEG0", 0x20); wait(1)                  -- LCDL 3: STOP
      press("CPL_SEG0", 0x01); wait(1)                  -- LCDL 4: EDIT
      press("CPC_SEG5", 0x10); wait(1)                  -- MUTE UP 1 again: channel 1 heard
      E("STOP, EDIT, channel 1 unmuted: %s", state()); M.video:snapshot()
      local r0 = sp:read_u8(sp:read_u32(SONG) + 0xAE4)  -- the cursor's row (currRow): the notes go there and on
      for _, k in ipairs({ "C3", "E3", "G3" }) do
        KEY[k]:set_value(1); wait(0.4); KEY[k]:set_value(0); wait(0.6)
      end
      E("keys C3 E3 G3: row %d %s; row %d %s; row %d %s; %s", r0, note(r0, 0), r0 + 1, note(r0 + 1, 0), r0 + 2,
        note(r0 + 2, 0), state())
      M.video:snapshot()
      press("CPL_SEG0", 0x08); wait(4)                  -- LCDL 2: the pattern plays
      E("LCDL 2 (pattern): %s", state()); M.video:snapshot()
      -- editor_t (tests/mame/offsets.c): currSample 0x2160, muted[4] 0x209D
      local function smp() return sp:read_u8(EDITOR + 0x2160) end
      local function muted() local t = {} for c = 0, 3 do t[#t + 1] = sp:read_u8(EDITOR + 0x209D + c) end return table.concat(t, " ") end
      local s0 = smp()
      press("CPR_SEG6", 0x01); wait(0.5); press("CPR_SEG6", 0x01); wait(0.5)   -- LCDR 4 twice: sample +2
      local s2 = smp()
      press("CPR_SEG7", 0x01); wait(0.5)                                      -- LCDR 3: sample -1
      E("LCDR 4, 4, 3: sample %d -> %d -> %d", s0, s2, smp())
      press("CPR_SEG5", 0x01); wait(1)                                        -- LCDR 5: every channel muted
      local m1 = muted()
      press("CPR_SEG5", 0x01); wait(1)                                        -- LCDR 5: none
      E("LCDR 5, 5: muted %s -> %s", m1, muted())
      press("CPL_SEG0", 0x02); wait(2)                                        -- LCDL 1: PLAY (the song)
      local p0 = sp:read_u16(sp:read_u32(SONG) + 0xAF0)
      press("CPC_SEG11", 0x20); wait(1)                                       -- PAGE DOWN: the next position
      E("LCDL 1, PAGE DOWN: position %d -> %d; %s", p0, sp:read_u16(sp:read_u32(SONG) + 0xAF0), state())
      press("CPR_SEG5", 0x10); wait(10)                                       -- LCDR 1: the module before
      E("LCDR 1 (the module before): %s", state()); M.video:snapshot()
      E("end")
      ev:close()
      M:exit()
      return
    end
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
