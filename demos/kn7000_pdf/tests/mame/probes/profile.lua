-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU Affero General Public License v3 or later
--
-- profile.lua -- where the CPU is while the viewer draws pages (run_pdf.sh OUT probes/profile.lua): as pdf_test.lua up to OPEN
-- ($PDF_FILE chosen with the wheel, default the first file), then the PC sampled every frame for $PDF_PROFILE_S seconds
-- (default 120: the first page and the pages drawn ahead after it), each PC's count with the function it lies in
-- (OUT/syms.txt) written to OUT/profile.txt (profile_lines.py names the lines), with a snapshot every 20 s; events.txt
-- has when the first page was shown. The original pdf_test.lua header follows.
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
    knobAt = (knobAt + (n > 0 and 1 or -1)) % 101  -- (0-100, round: the panel model takes a jump over 50 as a wrap)
    knob.user_value = knobAt  -- (an adjuster: the panel model reads its live value, kn_cpanel.cpp)
    wait(0.15)
  end
  wait(0.5)
end

-- the firmware's switch bitmap (KN7APP's fw.panel: Fw's 13th word) and KN7APP's own copy (panel_real)
local function fwPanel(seg) return sp:read_u8(sp:read_u32(SYM["_fw"] + 48) + seg) end
local function ourPanel(seg) return sp:read_u8(SYM["_panel_real"] + seg) end


local PROFILE_S = tonumber(os.getenv("PDF_PROFILE_S") or "") or 120
local FUNCS = {}
for line in io.lines((os.getenv("PDF_SYMS"))) do
  local a, k, n = line:match("^(%x+) (%a) (%S+)$")
  if a and (k == "T" or k == "t") and not n:match("^%.") then FUNCS[#FUNCS + 1] = { tonumber(a, 16), n } end
end
table.sort(FUNCS, function(x, y) return x[1] < y[1] end)
local function fname(pc)
  local lo, hi = 1, #FUNCS
  if #FUNCS == 0 or pc < FUNCS[1][1] then return string.format("?%08X", pc) end
  while lo < hi do local mid = (lo + hi + 1) // 2; if FUNCS[mid][1] <= pc then lo = mid else hi = mid - 1 end end
  return FUNCS[lo][2]
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
  local want = 0
  for i = 0, s32("_nFiles") - 1 do
    if FILE and cstr(SYM["_files"] + i * 20, 13) == FILE then want = i end  -- PfFile: char name[13] (+3), u32 bytes
  end
  turn(want)                                            -- the wheel moves the selection
  wait(1)
  press("CPR_SEG5", 0x10)                               -- R1: OPEN
  E("OPEN file %d", want)
  local count, total, t0, nextSnap, shown = {}, 0, now(), now() + 20, false
  callers = {}
  while now() - t0 < PROFILE_S do
    if not shown and s32("_pageNo") >= 0 then shown = true; E("first page shown after %.1f s: %s", now() - t0, state()) end
    local pc = cpu.state["PC"].value
    count[pc] = (count[pc] or 0) + 1
    local fn = fname(pc)
    if fn:match("sf3$") or fn:match("sf2$") or fn:match("sqrtf") then  -- (a soft-float routine: who called it, from MDR)
      local mdr = cpu.state["MDR"] and cpu.state["MDR"].value or 0
      callers[mdr] = (callers[mdr] or 0) + 1
    end
    total = total + 1
    if now() >= nextSnap then snap(string.format("+%.0f s, yields %d", now() - t0, u32("_yields"))); nextSnap = nextSnap + 20 end
    coroutine.yield()
  end
  E("sampling ended after %.1f s: %s", now() - t0, state())
  local list = {}
  for k, v in pairs(count) do list[#list + 1] = { k, v } end
  table.sort(list, function(x, y) return x[2] > y[2] end)
  local pf = io.open(OUT .. "/profile.txt", "w")
  pf:write(string.format("%d samples over %.1f s, yields %d\n", total, now() - t0, u32("_yields")))
  for i = 1, #list do pf:write(string.format("%6d %08X %s\n", list[i][2], list[i][1], fname(list[i][1]))) end
  pf:close()
  local cf = io.open(OUT .. "/callers.txt", "w")
  cf:write("caller samples (MDR while the PC was in a soft-float routine)\n")
  for k, v in pairs(callers) do cf:write(string.format("%6d %08X %s\n", v, k, fname(k))) end
  cf:close()
  snap("end of sampling")
  E("end")
  ev:close()
  M:exit()
end)
PDF_TICK = emu.add_machine_frame_notifier(function()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then print("profile: ERROR " .. tostring(err)); E("ERROR %s", tostring(err)); M:exit() end
  end
end)
