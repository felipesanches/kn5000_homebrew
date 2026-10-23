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
-- the DSP program's addresses, from the build (run_fm.sh passes build/dsp_equ.inc)
local EQU = {}
local equ_path = assert(os.getenv("FM_EQU"), "set FM_EQU")
for line in io.lines(equ_path) do
  local k, v = line:match("^%.equ%s+([%w_]+),%s+(0x%x+)")
  if k then EQU[k] = tonumber(v) end
end
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
  dump(string.format("voice_%d.bin", NDUMP), 0x502D9200 + 155 * sp:read_u32(V_PATCH), 155)   -- the voice, as edited
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

local co = coroutine.create(function()
  wait_until(function() return now() > 30 end, 60)
  E("boot: slots %012X %012X, dead %d", slot(0x8080), slot(0x80A0), sp:read_u8(DEAD))
  to_converter()
  run_menu_entry()
  local ok = wait_until(function() local s = sp:read_u32(V_STATE); return s == 4 or s == 5 end, 120)
  E("KN7FM: state %d, search step %d, fail %X, DSP found %02X state %d fail %d hooked %d, slots %012X %012X",
    sp:read_u32(V_STATE), sp:read_u32(V_STEP), sp:read_u32(V_FAIL), sp:read_u32(DV_FOUND), sp:read_u32(DV_STATE),
    sp:read_u32(DV_FAIL), sp:read_u32(DV_HOOKED), slot(0x8080), slot(0x80A0))
  if sp:read_u32(V_STATE) == 5 then                    -- failed: the DSP's memory as it is, for dsp_regions.py
    local f = assert(io.open(OUT .. "/dsp_mem.txt", "w"))
    for _, r in ipairs({ { "pm", 0x8100, 0x8400 }, { "pm", 0x8E00, 0x9000 }, { "dm", 0x9B20, 0x9FA0 } }) do
      for a = r[2], r[3] - 1 do
        local v = (r[1] == "pm") and (pm:read_u64(a) & 0xFFFFFFFFFFFF) or dm:read_u32(a)
        f:write(string.format("%s %04X %012X\n", r[1], a, v))
      end
    end
    f:close()
    E("the DSP's memory dumped (dsp_mem.txt); readback of 0x%04X, stopped early %d", sp:read_u32(DH + 0x68), sp:read_u32(DH + 0x5C))
    dump("rbbuf.bin", DH + 0x200, 0x200)
  end
  wait(4)
  M.video:snapshot()
  E("running: DSP frames %d, rate %d/s, dirty %X", dm:read_u32(G_FRAME), sp:read_u32(V_RATE), sp:read_u32(V_DIRTY))
  if sp:read_u32(V_STATE) == 4 then
    dump("bank.bin", 0x502D9200, 32 * 155)
    play({ "C4" }, 1.5, "C4")
    play({ "C4", "E4", "G4" }, 2.0, "C E G")
    press("CPC_SEG11", 0x10); wait(1)                   -- PAGE UP: the next voice
    E("PAGE UP: patch %d", sp:read_u32(V_PATCH))
    play({ "C3" }, 1.5, "C3 voice 2")
    local BANK, VCED = 0x502D9200, 155
    local function vbyte(voice, off) return sp:read_u8(BANK + VCED * voice + off) end
    local function check(name, ok, fmt, ...) E("CHECK %s: %s; " .. fmt, name, ok and "ok" or "FAIL", ...) end
    press("CPL_SEG0", 0x08); wait(1)                    -- LCDL 2: OP2 (the VCED's operator 4)
    for _ = 1, 3 do press("CPC_SEG5", 0x20); wait(0.3) end  -- MUTE DOWN 1: its R1 - 1, three times
    wait(1); M.video:snapshot()
    local r1 = vbyte(1, 4 * 21)
    check("OP2 R1 down 3", r1 == 96, "R1 = %d", r1)
    press("CPR_SEG7", 0x01); wait(1)                    -- LCDR 3: VOICE
    press("CPC_SEG5", 0x10); wait(1.5)                  -- MUTE UP 1: the algorithm + 1
    local alg, valg = vbyte(1, 134), sp:read_u32(MV + 0x3C)
    check("ALG up", alg == 1 and valg == 1, "ALG byte %d, the kernel running %d, TRAMP %012X", alg, valg, slot(EQU.DSP_TRAMP))
    M.video:snapshot()
    play({ "C3" }, 1.0, "C3 on algorithm 2")
    press("CPR_SEG6", 0x01); wait(1)                    -- LCDR 4: BANK
    for _ = 1, 2 do press("CPC_SEG5", 0x80); wait(0.3) end  -- MUTE DOWN 2: the volume - 2
    wait(1)
    local vol, vw = sp:read_u32(MV + 0xDC), dm:read_u32(EQU.DSP_G_VOL)
    check("VOL down 2", vol == 21 and vw == sp:read_u32(MV + 0xE4), "V_VOL %d, the DSP's VOL %08X", vol, vw)
    press("CPC_SEG5", 0x10); wait(0.5)                  -- MUTE UP 1: FILE 1
    press("CPC_SEG8", 0x01); wait(3)                    -- MUTE UP 3 (CPC_SEG8 SW0): LOAD BANK1.SYX
    local name = ""
    for k = 0, 9 do name = name .. string.char(vbyte(0, 145 + k)) end
    check("LOAD BANK1.SYX", sp:read_u32(MV + 0x34) == 2 and name == "LOADED V01", "bank from %d, voice 1 %q",
      sp:read_u32(MV + 0x34), name)
    M.video:snapshot()
    press("CPC_SEG5", 0x10); wait(0.5)                  -- FILE 2
    press("CPC_SEG8", 0x10); wait(0.5)                  -- MUTE UP 5 (CPC_SEG8 SW4): SAVE, armed
    press("CPC_SEG8", 0x10); wait(4)                    -- again: BANK2.SYX written
    check("SAVE BANK2.SYX", sp:read_u32(MV + 0x2C) == 4104, "bytes written %d, request %d result %d",
      sp:read_u32(MV + 0x2C), sp:read_u32(MV + 0x24), sp:read_u32(MV + 0x28))
    M.video:snapshot()
  end
  press("CPC_SEG11", 0x80, 2.0)                         -- EXIT held 2 s
  local q = wait_until(function() return sp:read_u32(V_TASKDONE) == 1 and sp:read_u32(DV_STATE) ~= 2 end, 20)
  wait(1)
  E("quit %s: task done %d, DSP state %d, hooked %d, slots %012X %012X, MSGR4-7 %X %X %X %X", q and "ok" or "NOT DONE",
    sp:read_u32(V_TASKDONE), sp:read_u32(DV_STATE), sp:read_u32(DV_HOOKED), slot(0x8080), slot(0x80A0),
    dm:read_u32(0x0C), dm:read_u32(0x0D), dm:read_u32(0x0E), dm:read_u32(0x0F))
  M.video:snapshot()
  press("CPC_SEG11", 0x80); wait(4)                     -- EXIT on the message: "Continue Next?"
  press("CPR_SEG6", 0x01); wait(6)                      -- NO (right 4): the main screen
  E("left STYLE CONVERT; slots %012X %012X; a C4 on the keybed", slot(0x8080), slot(0x80A0))
  M.video:snapshot()
  KEY["C4"]:set_value(1); wait(1.0); KEY["C4"]:set_value(0); wait(3)
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
