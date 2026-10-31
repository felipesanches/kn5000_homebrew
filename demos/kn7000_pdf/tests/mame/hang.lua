-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU Affero General Public License v3 or later
--
-- hang.lua -- the watchdog's test (run_pdf.sh OUT hang.lua): as quick.lua, then, the first page drawn, the viewer is made
-- to loop without yielding (hang_test, viewer.c); after 4 s + 16 x 0.1 s KN7APP's watchdog (crash.c) paints the PCs:
-- CHECK its screen is up (wd_shown), its report written and saved (wd_escaped 2: with KEEP_SD=1, PDF\PDFLOG.TXT in
-- OUT/sd_after.img), and the PCs lie in the viewer's loop; a snapshot. quick.lua's header follows.
--
-- quick.lua -- a short session (run_pdf.sh OUT quick.lua, a few minutes): the menu, RUN, the list, OPEN the first file,
-- the first page or $PDF_QUICK_S seconds (default 60), EXIT held; with KEEP_SD=1 the card (OUT/sd_after.img) keeps
-- what the viewer wrote, PDF\PDFLOG.TXT among it (tools/card/fatget.py reads it). pdf_test.lua's header follows.
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
--     firmware's) present there, while held; then SOUND GROUP 2 and 3, PANEL MEMORY 8 (the sound group's), VARIATION 2,
--     SOUND EXPLORER and ORGAN & ACCORDION pressed, with a write tap on the borrowed frame buffers: CHECK no firmware code
--     wrote there;
--   * at the end, CHECK the borrowed RAM came back as it went (borrow_bad 0);
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
  return string.format("text %q; files %d pages %d page %d height %d top %d; heap %d KB peak %d KB failed %d; " ..
    "calls %d, switch frames %d (late %d), wheel frames %d (late %d), yields %d; log %q",
    cstr(0x5031BC01, 60), s32("_nFiles"), s32("_pages"), s32("_pageNo"), s32("_pageH"), s32("_top"),
    u32("_heap_used") // 1024, u32("_heap_peak") // 1024, u32("_heap_failed"), u32("_calls"), u32("_panel_frames"),
    u32("_panel_late"), u32("_wheel_frames"), u32("_wheel_late"), u32("_yields"), logLast())
end

-- the borrowed frame buffers (KN7APP borrow.c) and the code that may write there: KN7APP's and PDF.BIN's (its main
-- image, its segments in the program area and ARENA4)
local function ours(pc) return (pc >= 0x502BA000 and pc < 0x502C8000) or (pc >= 0x5031C000 and pc < 0x5037A000) or
  (pc >= 0x502E8000 and pc < 0x5031B000) or (pc >= 0x503BC000 and pc < 0x503FF000) end
local foreign = 0
local function tap_on()
  return sp:install_write_tap(0x500D5000, 0x5011DFFF, "borrow", function(offset, data, mask)
    local pc = cpu.state["PC"].value
    if not ours(pc) then
      foreign = foreign + 1
      if foreign <= 40 then E("FOREIGN write %08X by PC %08X", offset, pc) end
    end
  end)
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
  E("RUN")
  CHECK("the list is up", wait_until(function() return u32("_owned") == 1 end, 240), state())
  wait(1)
  press("CPR_SEG5", 0x10)
  CHECK("the first page drawn", wait_until(function() return s32("_pageNo") >= 0 end, 60), state())
  E("wd_on %d wd_entry %08X wd_last %d now %d", u32("_wd_on"), u32("_wd_entry"), u32("_wd_last"), sp:read_u32(0x500C53F8))
  for _, a in ipairs({ 0x4C03DD8D, 0x4C03DE13 }) do
    local b = {}
    for i = 0, 47 do b[#b + 1] = string.format("%02X", sp:read_u8(a + i)) end
    E("code at %08X: %s", a, table.concat(b, " "))
  end
  sp:write_u32(SYM["_hang_test"], 1)
  E("hang_test set")
  CHECK("the watchdog's screen", wait_until(function() return u32("_wd_shown") == 1 end, 15), u32("_wd_shown"))
  CHECK("the watchdog's report saved on the card", wait_until(function() return u32("_wd_escaped") == 2 end, 30),
    u32("_wd_escaped"))
  E("after: wd_on %d wd_last %d now %d wd_n %d, PC %08X", u32("_wd_on"), u32("_wd_last"), sp:read_u32(0x500C53F8),
    u32("_wd_n"), cpu.state["PC"].value)
  local pcs = {}
  for i = 0, 15 do pcs[#pcs + 1] = string.format("%08X", sp:read_u32(SYM["_wd_pc"] + 4 * i)) end
  E("watchdog PCs: %s", table.concat(pcs, " "))
  wait(1)
  local r = {}
  for _, n in ipairs({ 0x40, 0x42, 0x43, 0x44, 0x46, 0x47, 0x70, 0x71, 0x72, 0x73, 0x74 }) do
    r[#r + 1] = string.format("%02X=%02X", n, sp:read_u8(0x9CC00000 + n))
  end
  E("LCDC %s", table.concat(r, " "))
  snap("the watchdog's screen")
  E("end")
  ev:close()
  M:exit()
end)
PDF_TICK = emu.add_machine_frame_notifier(function()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then print("hang: ERROR " .. tostring(err)); E("ERROR %s", tostring(err)); M:exit() end
  end
end)
