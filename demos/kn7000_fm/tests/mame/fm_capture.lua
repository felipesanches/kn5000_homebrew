-- fm_capture.lua -- fm_test.lua's start and helpers, then one C4 (1 s held, 1 s of release) whose every DSP
-- frame is recorded: a write tap on the engine's MIX gives (FRAME, MIX) per frame in $FM_OUT/capture.txt, for
-- compare_capture.py. (What follows is fm_test.lua's own header.)
-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU General Public License v3 or later
--
-- fm_test.lua -- KN7FM in one MAME session through the program menu (run_fm.sh builds the card and starts MAME):
--   * DISK MENU, STYLE CONVERT, OK: the loader, the menu; KN7FM's row, RUN;
--   * KN7FM finds the firmware, loads the bank and the DSP, hooks units 0 and 9, starts its task: the screen;
--   * notes on the keybed: C4 alone, then a C major chord, then the same on the next voice of the bank (PAGE UP);
--     the DSP's voices (ACTIVE), its mix and the heartbeat are read all along;
--   * the editor: OP2's page (LCDL 2), MUTE UP 1 three times (its R1), VOICE (LCDR 3), BANK (LCDR 4);
--   * EXIT held 2 s: the task ends, the slots go back as found; EXIT; NO at "Continue Next?"; a C4 on the keybed.
-- Writes $FM_OUT/tap.txt (every 0.25 s: the state below), $FM_OUT/events.txt and snapshots.
local M = manager.machine
local cpu = M.devices[":maincpu"]
local sp = cpu.spaces["program"]
local dsp = M.devices[":dsp"]
local dm, pm = dsp.spaces["data"], dsp.spaces["program"]
local OUT = assert(os.getenv("FM_OUT"), "set FM_OUT")
local DEAD = tonumber(os.getenv("FM_DEAD") or "0x500066D8")
local PANEL = tonumber(os.getenv("FM_PANEL") or "0x5006BE4C")
-- KN7FM's addresses (src/kn7fm.inc, src/dsp.inc; tools/fm_dsp.py)
local MV = 0x502D8800
local V_STATE, V_STEP, V_FAIL, V_TASKDONE, V_PATCH = MV, MV + 8, MV + 0xC, MV + 0x14, MV + 0x38
local V_NOTES, V_STEALS, V_BUSY, V_RATE, V_REDRAWS, V_DIRTY = MV + 0x90, MV + 0x94, MV + 0x98, MV + 0xC4, MV + 0xC8, MV + 0x70
local DH = 0x502D8400
local DV_STATE, DV_FAIL, DV_FOUND, DV_HOOKED = DH, DH + 4, DH + 0x28, DH + 0x54
local VOICES, G_MIX, G_FRAME = 0x9D00, 0x9C23, 0x9C26
local menuV, V_NPROG, ORDER, PR_NAME = 0x502C0000, 0x502C0034, 0x502C3800, 0x40   -- the menu's (kn7menu menu.inc)
local NAME = "KN7FM: DX7-style FM synthesizer"

local function now() return M.time:as_double() end
local ev = assert(io.open(OUT .. "/events.txt", "w"))
local function E(...) local s = string.format("%8.2f ", now()) .. string.format(...); print("fm_test: " .. s); ev:write(s, "\n"); ev:flush() end
local function cstr(a, n)
  local s = ""
  for i = 0, (n or 64) - 1 do local c = sp:read_u8(a + i); if c == 0 then break end; s = s .. string.char(c) end
  return s
end
local function field(tag, mask)
  local p = M.ioport.ports[":cpanel:" .. tag] or M.ioport.ports[":" .. tag]
  for _, f in pairs(p.fields) do if f.mask == mask then return f end end
end
local KEY = {}
for _, tag in ipairs({ ":KEYS0", ":KEYS1", ":KEYS2", ":KEYS3" }) do
  local p = M.ioport.ports[tag]
  if p then for _, f in pairs(p.fields) do KEY[f.name] = f end end
end
local function wait(s) local t = now() + s; while now() < t do coroutine.yield() end end
local function wait_until(cond, s) local t = now() + s; while now() < t do if cond() then return true end; coroutine.yield() end; return cond() end
local function press(tag, mask, hold)       -- a panel press, seen in the firmware's own switch bitmap
  local f = field(tag, mask)
  local seg = tonumber(tag:match("SEG(%d+)"))
  local byte = PANEL + seg + (tag:sub(1, 3) == "CPR" and 0 or 0x30)
  for try = 1, 6 do
    f:set_value(1)
    local seen = wait_until(function() return (sp:read_u8(byte) & mask) ~= 0 end, 1.0)
    if seen then wait(hold or 0.3); f:set_value(0); return true end
    f:set_value(0); E("press %s %02X not seen (try %d): again", tag, mask, try); wait(1.5)
  end
  return false
end
local function slot(a) return pm:read_u64(a) & 0xFFFFFFFFFFFF end
local function active()
  local m = 0
  for v = 0, 7 do if dm:read_u32(VOICES + 64 * v + 32 + 24) ~= 0 then m = m | (1 << v) end end
  return m
end
local function s32(v) if v >= 0x80000000 then v = v - 0x100000000 end; return v end

local NOTENUM = { C3 = 48, C4 = 60, E4 = 64, G4 = 67 }
local NDUMP = 0
local function dump(name, addr, len, head)  -- main-CPU memory to OUT/name (after head's u32s), for check_words.py
  local f = assert(io.open(OUT .. "/" .. name, "wb"))
  for _, v in ipairs(head or {}) do f:write(string.pack("<I4", v)) end
  for i = 0, len - 1 do f:write(string.char(sp:read_u8(addr + i))) end
  f:close()
end
local function dump_words(key)
  dump(string.format("pimg_%d.bin", NDUMP), 0x502D8FC0, 67 * 4, { sp:read_u32(V_PATCH) })
  if key then dump(string.format("stage_%d.bin", NDUMP), 0x502D8E00, 18 * 4, { sp:read_u32(V_PATCH), key }) end
  NDUMP = NDUMP + 1
end
-- ---- the tap ------------------------------------------------------------------------------------------------
local tap = assert(io.open(OUT .. "/tap.txt", "w"))
tap:write("t state step fail dvstate dvfail hooked slot0 slot9 frame active mixmax notes steals busy rate redraws dirty dead patch\n")
local T = { next = 0, mixmax = 0, act = 0 }
local function sample()
  local m = math.abs(s32(dm:read_u32(G_MIX)))
  if m > T.mixmax then T.mixmax = m end
  T.act = T.act | active()
  local t = now()
  if t < T.next then return end
  T.next = t + 0.25
  tap:write(string.format("%.2f %d %d %X %d %d %d %012X %012X %d %02X %d %d %d %d %d %d %X %d %d\n",
    t, sp:read_u32(V_STATE), sp:read_u32(V_STEP), sp:read_u32(V_FAIL), sp:read_u32(DV_STATE), sp:read_u32(DV_FAIL),
    sp:read_u32(DV_HOOKED), slot(0x8080), slot(0x80A0), dm:read_u32(G_FRAME), T.act, T.mixmax,
    sp:read_u32(V_NOTES), sp:read_u32(V_STEALS), sp:read_u32(V_BUSY), sp:read_u32(V_RATE), sp:read_u32(V_REDRAWS),
    sp:read_u32(V_DIRTY), sp:read_u8(DEAD), sp:read_u32(V_PATCH)))
  tap:flush()
  T.mixmax, T.act = 0, 0
end

-- ---- the session ------------------------------------------------------------------------------------------------
local function to_converter()               -- DISK MENU, STYLE CONVERT (left 3), OK (right 4) until the loader runs
  press("CPR_SEG1", 0x04); wait(6)
  press("CPL_SEG0", 0x20); wait(8)
  for i = 1, 8 do
    press("CPR_SEG6", 0x01)
    if wait_until(function() return sp:read_u32(0x502B9E00) == 0x53374E4B end, 20) then break end
    E("OK %d not taken; again", i)
  end
end
local function run_menu_entry()             -- the menu's list: our row's left button, then RUN (right 5)
  local ok = wait_until(function() return sp:read_u32(menuV) == 4 end, 150)
  local n, idx = sp:read_u32(V_NPROG), nil
  for i = 0, n - 1 do if cstr(sp:read_u32(ORDER + 4 * i) + PR_NAME) == NAME then idx = idx or i end end
  E("menu: %s, %d programs, KN7FM at %s", ok and "up" or "NOT UP", n, tostring(idx))
  if not idx then return false end
  for _ = 1, idx // 5 do press("CPR_SEG5", 0x20); wait(1) end
  press("CPL_SEG0", ({ 0x02, 0x08, 0x20, 0x01, 0x04 })[idx % 5 + 1]); wait(1)
  press("CPR_SEG5", 0x01)
  ok = wait_until(function() return cstr(0x502BA020, 32) == "KN7FM DX7-STYLE FM SYNTHESIZER" end, 30)
  E("RUN: %q at APP_BASE, launch folder %q", cstr(0x502BA020, 32), cstr(0x502E7F10, 16))
  return ok
end
local function play(keys, secs, label)
  for _, k in ipairs(keys) do KEY[k]:set_value(1) end
  local seen, mm = 0, 0
  local t = now() + secs
  while now() < t do
    seen = seen | active()
    local m = math.abs(s32(dm:read_u32(G_MIX))); if m > mm then mm = m end
    coroutine.yield()
  end
  M.video:snapshot()
  dump_words(NOTENUM[keys[#keys]])
  for _, k in ipairs(keys) do KEY[k]:set_value(0) end
  E("%s: voices active %02X, |MIX| max %d (%.1f dBFS), notes sent %d, busy %d", label, seen, mm,
    20 * math.log(math.max(mm, 1) / 8388607, 10), sp:read_u32(V_NOTES), sp:read_u32(V_BUSY))
  local gone = wait_until(function() return active() == 0 end, 8)
  E("%s: released; all voices ended %s (active %02X)", label, gone and "yes" or "NO", active())
end

local cap = nil
local co = coroutine.create(function()
  wait_until(function() return now() > 30 end, 60)
  to_converter()
  run_menu_entry()
  wait_until(function() local s = sp:read_u32(V_STATE); return s == 4 or s == 5 end, 120)
  E("KN7FM: state %d, DSP state %d fail %d", sp:read_u32(V_STATE), sp:read_u32(DV_STATE), sp:read_u32(DV_FAIL))
  wait(4)
  local f = assert(io.open(OUT .. "/capture.txt", "w"))
  local n = 0
  local vf = assert(io.open(OUT .. "/voice_dumps.txt", "w"))
  local sounding = nil
  cap = dm:install_write_tap(G_MIX, G_MIX, "fmcap", function(offset, data, mask)
    local fr = dm:read_u32(G_FRAME)
    f:write(string.format("%d %d\n", fr, s32(data))); n = n + 1
    if sounding == nil and data ~= 0 then sounding = fr end
    if sounding and (fr == sounding or fr == sounding + 64 or fr == sounding + 2000) then
      local w = {}
      for a = 0x9D00, 0x9D3F do w[#w + 1] = string.format("%08X", dm:read_u32(a)) end
      for a = 0x9C21, 0x9C32 do w[#w + 1] = string.format("%08X", dm:read_u32(a)) end
      vf:write(fr .. " " .. table.concat(w, " ") .. "\n")
    end
  end)
  KEY["C4"]:set_value(1); wait(1.0); KEY["C4"]:set_value(0); wait(1.0)
  cap:remove()
  f:close()
  vf:close()
  E("captured %d frames of MIX", n)
  dump_words(60)
  press("CPC_SEG11", 0x80, 2.0)
  wait_until(function() return sp:read_u32(V_TASKDONE) == 1 end, 20)
  E("end")
  tap:close(); ev:close()
  M:exit()
end)
FM_TICK = emu.add_machine_frame_notifier(function()
  sample()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then E("ERROR %s", tostring(err)); tap:close(); ev:close(); M:exit() end
  end
end)
E("installed")
