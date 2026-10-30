-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU Affero General Public License v3 or later
--
-- panel.lua -- what one switch does, with the viewer up (its list): R2, then LCDL 1, then VARIATION 1, each held 1 s, and
-- the panel ring's bytes (0x5006BDB0, 0x5C), its indexes, KN7APP's copy of the switches (panel_real) and the
-- firmware's bitmap logged before and while held. (profile.lua's header follows.)
--
-- profile.lua -- where the CPU is while the viewer draws a page (run_pdf.sh OUT probes/profile.lua): as pdf_test.lua up to OPEN, then
-- the PC sampled every frame for $PDF_PROFILE_S seconds (default 120), each PC's count with the function it lies in
-- (OUT/syms.txt) written to OUT/profile.txt (profile_lines.py names the lines), with a snapshot every 20 s. The original pdf_test.lua header follows.
--
-- pdf_test.lua -- the PDF viewer in MAME, through the program menu (run_pdf.sh OUT):
--   * DISK MENU, STYLE CONVERT, OK: the loader, the menu; the "PDF viewer (MuPDF)" row, RUN;
--   * every 2 s to $PDF_OUT/events.txt: the converter's text, the viewer's state (viewer.c: files listed, the
--     document's pages, the page shown, its height, the line shown at the top), the heap (main.c) and the log's last
--     line; a snapshot at each step;
--   * the list up: CHECK it lists the card's PDFs; then the file named $PDF_FILE (default the first) chosen with the
--     wheel and opened (R1 OPEN), and CHECK its first page drawn;
--   * the wheel turned (the TEMPO/PROGRAM knob's adjuster, +$PDF_TURN steps): CHECK the page scrolled, and the tempo
--     left alone; turned on past the page's end: CHECK the next page drawn; PAGE UP: CHECK the page before;
--   * the panel shared: R2 (taken by the viewer) CHECKed absent from the firmware's switch bitmap, and VARIATION 1 (the
--     firmware's) present there, while held;
--   * the keybed while a page draws: C4 held, CHECKed in the firmware's MIDI OUT 1 ring (its write pointer moved) before
--     the page is done;
--   * EXIT: back to the list; EXIT held 2.5 s: the converter's end (request 0xFE).
local M = manager.machine
local cpu = M.devices[":maincpu"]
local sp = cpu.spaces["program"]
local OUT = assert(os.getenv("PDF_OUT"), "set PDF_OUT")
local NAME = os.getenv("PDF_NAME") or "PDF viewer (MuPDF)"
local FILE = os.getenv("PDF_FILE")
local TURN = tonumber(os.getenv("PDF_TURN") or "") or 4
local menuV, V_NPROG, ORDER, PR_NAME = 0x502C0000, 0x502C0034, 0x502C3800, 0x40   -- the menu's (kn7menu menu.inc)

local SYM = {}
for line in io.lines((assert(os.getenv("PDF_SYMS"), "set PDF_SYMS"))) do
  local a, n = line:match("^(%x+) %a (%S+)$")
  if a then SYM[n] = SYM[n] or tonumber(a, 16) end
end

local function now() return M.time:as_double() end
local ev = assert(io.open(OUT .. "/events.txt", "w"))
local function E(...) local s = string.format("%8.2f ", now()) .. string.format(...); print("pdf_test: " .. s); ev:write(s, "\n"); ev:flush() end
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

-- the log's last line (main.c logLine[18][81], logRows)
local function logLast()
  local rows = s32("_logRows")
  if rows < 1 or not SYM["_logLine"] then return "" end
  local l = cstr(SYM["_logLine"] + 81 * (rows - 1), 80)
  if l == "" and rows > 1 then l = cstr(SYM["_logLine"] + 81 * (rows - 2), 80) end
  return l
end
local function state()
  return string.format("text %q; files %d pages %d page %d height %d top %d; heap %d KB peak %d KB failed %d; log %q",
    cstr(0x5031BC01, 60), s32("_nFiles"), s32("_pages"), s32("_pageNo"), s32("_pageH"), s32("_top"),
    u32("_heap_used") // 1024, u32("_heap_peak") // 1024, u32("_heap_failed"), logLast())
end

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

-- the TEMPO/PROGRAM knob: an adjuster the panel model sends as the wheel's detents
local knob
for _, f in pairs(M.ioport.ports[":TEMPO_KNOB"].fields) do knob = f end
local knobAt = 50
local function turn(n)
  for _ = 1, math.abs(n) do
    knobAt = knobAt + (n > 0 and 1 or -1)
    knob:set_value(knobAt)
    wait(0.15)
  end
  wait(0.5)
end

-- the firmware's switch bitmap (KN7APP's fw.panel: Fw's 13th word) and KN7APP's own copy (panel_real)
local function fwPanel(seg) return sp:read_u8(sp:read_u32(SYM["_fw"] + 48) + seg) end
local function ourPanel(seg) return sp:read_u8(SYM["_panel_real"] + seg) end



local function hex(a, n) local t = {}; for i = 0, n - 1 do t[#t + 1] = string.format("%02X", sp:read_u8(a + i)) end; return table.concat(t, " ") end
local function dump(what)
  E("%s: ring rd %d wr %d [%s]", what, sp:read_u16(0x5006BDAC), sp:read_u16(0x5006BDAE), hex(0x5006BDB0, 0x5C))
  E("  ours CPR %s | CPL %s", hex(SYM["_panel_real"], 10), hex(SYM["_panel_real"] + 0x30, 12))
  local fwp = sp:read_u32(SYM["_fw"] + 48)
  E("  fw   CPR %s | CPL %s", hex(fwp, 10), hex(fwp + 0x30, 12))
  E("  frames %d late %d wheel %d, wheel_at %d, calls %d", u32("_panel_frames"), u32("_panel_late"), u32("_wheel_frames"),
    u32("_wheel_at"), u32("_calls"))
end
local co = coroutine.create(function()
  wait_until(function() return now() > 30 end, 60)
  press("CPR_SEG1", 0x04); wait(6)
  press("CPL_SEG0", 0x20); wait(8)
  for _ = 1, 8 do
    press("CPR_SEG6", 0x01)
    if wait_until(function() return sp:read_u32(0x502B9E00) == 0x53374E4B end, 20) then break end
  end
  wait_until(function() return sp:read_u32(menuV) == 4 end, 150)
  local n, idx = sp:read_u32(V_NPROG), 0
  for i = 0, n - 1 do if cstr(sp:read_u32(ORDER + 4 * i) + PR_NAME) == NAME then idx = i end end
  for _ = 1, idx // 5 do press("CPR_SEG5", 0x20); wait(1) end
  press("CPL_SEG0", ({ 0x02, 0x08, 0x20, 0x01, 0x04 })[idx % 5 + 1]); wait(1)
  press("CPR_SEG5", 0x01)
  wait_until(function() return u32("_owned") == 1 end, 240)
  wait(2)
  dump("before")
  for _, b in ipairs({ { "R2", "CPR_SEG5", 0x20 }, { "LCDL 1", "CPL_SEG0", 0x02 }, { "VARIATION 1", "CPL_SEG4", 0x10 } }) do
    local f = field(b[2], b[3]); f:set_value(1); wait(1)
    dump(b[1] .. " held")
    f:set_value(0); wait(1)
    dump(b[1] .. " let go")
  end
  local knob
  for _, f in pairs(M.ioport.ports[":TEMPO_KNOB"].fields) do knob = f end
  for v = 51, 54 do knob.user_value = v; wait(0.3) end
  wait(1)
  dump("knob 50 -> 54")
  E("end")
  ev:close()
  M:exit()
end)
PDF_TICK = emu.add_machine_frame_notifier(function()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then print("panel: ERROR " .. tostring(err)); E("ERROR %s", tostring(err)); M:exit() end
  end
end)
