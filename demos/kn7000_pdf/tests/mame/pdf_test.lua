-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU Affero General Public License v3 or later
--
-- pdf_test.lua -- the PDF viewer in MAME, through the program menu (run_pdf.sh OUT):
--   * DISK MENU, STYLE CONVERT, OK: the loader, the menu; the "PDF viewer (MuPDF)" row, RUN;
--   * every 2 s to $PDF_OUT/events.txt: the converter's text, the viewer's state (viewer.c: files listed, the
--     document's pages, the page shown, its height, the line shown at the top), the heap (main.c) and the log's last
--     line; a snapshot at each step;
--   * the list up: CHECK it lists the card's PDFs; then the file named $PDF_FILE (default the first) chosen with the
--     wheel and opened (R1 OPEN), and CHECK its first page drawn;
--   * the wheel turned (the TEMPO/PROGRAM knob's adjuster, +$PDF_TURN steps): CHECK the page scrolled, and the tempo
--     left alone;
--   * pages drawn ahead (viewer.c, pagecache.c): CHECK the next page drawn into the cache unasked; CHECK the page shown
--     scrolls while another is drawn; the wheel on past the page's end: CHECK the next page shown at once; PAGE UP:
--     CHECK the page before at once; PAGE DOWN to the last page, not drawn yet: CHECK TOP (R1) answers meanwhile and
--     CHECK the page shown when it is done; CHECK the pages drawn saved on the card once the controls are left alone;
--     EXIT and the document opened again: CHECK its first page from the card at once;
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
-- what MuPDF is doing (view.c view_stage: "" when no page is being drawn)
local function stage() return cstr(sp:read_u32(SYM["_view_stage"]), 16) end
local function idle() return stage() == "" and s32("_pageNo") >= 0 end
-- the page cache (pagecache.c: pg[], PcPage = blob, card, used, rows/saved/failed: 16 bytes)
local function pcEntry(n, off) local pg = sp:read_u32(SYM["_pg"]); return pg ~= 0 and sp:read_u32(pg + 16 * n + off) or 0 end
local function cached(n) return pcEntry(n, 0) ~= 0 end
local function onCard(n) return pcEntry(n, 4) ~= 0 end
local function shownWithin(n, s) local t = now(); local ok = wait_until(function() return s32("_pageNo") == n end, s)
  return ok, now() - t end
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
  CHECK("the list is up", wait_until(function() return s32("_nFiles") >= 0 and SYM["_nFiles"] and u32("_owned") == 1 end, 240),
    state())
  E("viewer up after %.1f s; task priority %d", now() - runAt, u32("_task_pri"))
  wait(2)
  snap("the list")
  local nf = s32("_nFiles")
  CHECK("PDFs listed", nf > 0, nf)
  local want = 0
  for i = 0, nf - 1 do
    local name = cstr(SYM["_files"] + i * 20, 13)    -- PfFile: char name[13] (+3), u32 bytes
    E("file %d: %s", i, name)
    if FILE and name == FILE then want = i end
  end
  turn(want)                                            -- the wheel moves the selection
  wait(1)
  press("CPR_SEG5", 0x10)                               -- R1: OPEN
  local openAt = now()
  E("OPEN")
  -- the keybed while the page draws: C4 held a second, the note in KN7APP's notes[] from the MIDI OUT ring
  wait(1)
  -- (the firmware writes the key's note-on to its MIDI OUT 1 ring: the ring header's write pointer, KN7APP fw.midiOut1
  -- (Fw's 15th word) + QH_WRITE, moves)
  local ring = sp:read_u32(SYM["_fw"] + 56)
  local wr0 = sp:read_u32(ring + 0x0C)
  local keyed = false
  if KEYN[60] then
    KEYN[60]:set_value(1); wait(0.5)
    keyed = sp:read_u32(ring + 0x0C) ~= wr0 and s32("_pageNo") < 0
    KEYN[60]:set_value(0)
  end
  CHECK("the keybed plays while the page draws", keyed, string.format("MIDI OUT write %08X -> %08X, yields %d; %s", wr0,
    sp:read_u32(ring + 0x0C), u32("_yields"), state()))
  local drawn = wait_until(function() return s32("_pageNo") >= 0 end, 600)
  CHECK("the first page drawn", drawn, string.format("%.1f s; %s", now() - openAt, state()))
  wait(1)
  snap("page 1, top")
  local top0 = s32("_top")
  turn(TURN)
  CHECK("the wheel scrolls the page", s32("_top") > top0, string.format("top %d -> %d", top0, s32("_top")))
  E("wheel frames %d, late %d (the firmware decoded them first)", u32("_wheel_frames"), u32("_wheel_late"))
  CHECK("the tempo left alone: every wheel frame the firmware decoded first taken back", u32("_wheel_late") == u32("_wheel_undone"),
    string.format("%d late, %d taken back", u32("_wheel_late"), u32("_wheel_undone")))
  snap("page 1, scrolled")
  -- the panel shared: R2 is the viewer's, VARIATION 1 the firmware's
  local r2, var1 = field("CPR_SEG5", 0x20), field("CPL_SEG4", 0x10)
  -- (one after the other: pressed together, MAME's panel model delivers only one of the two frames, probes/panel.lua)
  r2:set_value(1); wait(1)
  local fwR2, ourR2 = fwPanel(5) & 0x20, ourPanel(5) & 0x20
  r2:set_value(0); wait(1)
  var1:set_value(1); wait(1)
  local fwV1 = fwPanel(0x30 + 4) & 0x10
  var1:set_value(0); wait(1)
  CHECK("R2 reaches the viewer, not the firmware", ourR2 ~= 0 and fwR2 == 0, string.format("ours %02X fw %02X; %s", ourR2,
    fwR2, state()))
  CHECK("VARIATION 1 reaches the firmware", fwV1 ~= 0, string.format("fw %02X", fwV1))
  -- the firmware's own buttons, which it acts on as ever, while the borrowed frame buffers are tapped: no firmware code
  -- may write there (its drawing is off)
  local tap = tap_on()
  for _, b in ipairs({ { "CPR_SEG6", 0x40 }, { "CPR_SEG5", 0x40 }, { "CPR_SEG7", 0x80 }, { "CPL_SEG4", 0x04 },
                       { "CPR_SEG6", 0x08 }, { "CPR_SEG7", 0x08 }, { "CPR_SEG6", 0x40 } }) do
    press(b[1], b[2]); wait(1)
  end
  wait(2)
  tap:remove()
  CHECK("the firmware's buttons: no firmware write in the borrowed RAM", foreign == 0, foreign)
  snap("after the firmware's buttons")
  -- pages drawn ahead of the reader
  local pages = s32("_pages")
  if pages > 1 then
    CHECK("the next page drawn ahead, unasked", wait_until(function() return cached(1) end, 600), state())
    local busy = wait_until(function() return s32("_job") >= 0 end, 60)
    local t0 = s32("_top")
    turn(t0 > 0 and -2 or 2)
    CHECK("the page shown scrolls while another is drawn", busy and s32("_top") ~= t0 and s32("_pageNo") == 0,
      string.format("drawing page %d; top %d -> %d; %s", s32("_job") + 1, t0, s32("_top"), state()))
    for _ = 1, 60 do
      turn(4)
      if s32("_pageNo") == 1 then break end
    end
    local ok, dt = shownWithin(1, 30)
    CHECK("past the end: the next page at once", ok and dt < 3, string.format("%.1f s; %s", dt, state()))
    CHECK("the wheel turned while a page drew: the tempo left alone", u32("_wheel_late") == u32("_wheel_undone"),
      string.format("%d of %d late, %d taken back", u32("_wheel_late"), u32("_wheel_frames"), u32("_wheel_undone")))
    wait(1)
    snap("page 2")
    press("CPC_SEG11", 0x10)                            -- PAGE UP: the page before
    ok, dt = shownWithin(0, 30)
    CHECK("PAGE UP: the page before at once", ok and dt < 3, string.format("%.1f s; %s", dt, state()))
    -- PAGE DOWN to the last page, not drawn ahead: TOP answers while it is drawn, and it is shown when done
    local last = pages - 1
    for _ = 1, last do press("CPC_SEG11", 0x20) end
    local waiting = s32("_want") == last and s32("_pageNo") ~= last
    local shownBefore = s32("_pageNo")
    press("CPR_SEG5", 0x10)                             -- R1: TOP
    CHECK("TOP answers while the page asked for is drawn", waiting and s32("_top") == 0 and s32("_pageNo") == shownBefore,
      string.format("want %d shown %d top %d; %s", s32("_want") + 1, s32("_pageNo") + 1, s32("_top"), state()))
    ok, dt = shownWithin(last, 600)
    CHECK("the page asked for shown when it is drawn", ok, string.format("%.1f s; %s", dt, state()))
    snap("the last page")
    -- the controls left alone: the pages drawn go onto the card
    CHECK("the pages drawn saved on the card", wait_until(function() return onCard(0) and onCard(1) and onCard(last) end,
      600), string.format("on the card: %s %s %s; %s", tostring(onCard(0)), tostring(onCard(1)), tostring(onCard(last)),
      state()))
    -- the document again: its pages from the card
    press("CPC_SEG11", 0x80)                            -- EXIT: the list
    wait_until(function() return s32("_pageH") == 0 end, 30)
    wait(1)
    press("CPR_SEG5", 0x10)                             -- R1: OPEN
    local t1 = now()
    ok = wait_until(function() return s32("_pageNo") == 0 end, 120)
    CHECK("opened again: the first page from the card at once", ok and onCard(0) and now() - t1 < 8,
      string.format("%.1f s; %s", now() - t1, state()))
    snap("opened again")
  end
  E("%s", state())
  press("CPC_SEG11", 0x80)                              -- EXIT: the list
  CHECK("EXIT: the list again", wait_until(function() return s32("_pageH") == 0 end, 30), state())
  wait(1)
  snap("the list again")
  E("viewer: %d checks, %d failed", checks, fails)
  local f = field("CPC_SEG11", 0x80); f:set_value(1); wait(2.5); f:set_value(0)
  local ended = wait_until(function() return sp:read_u8(0x5031BC00) == 0xFE end, 30)
  CHECK("the end: the borrowed RAM back as it was", ended and u32("_borrow_bad") == 0, string.format("ended %s, borrow_bad %d",
    tostring(ended), u32("_borrow_bad")))
  E("ended %s; %s", tostring(ended), state())
  wait(3)
  snap("after the end")
  E("end")
  ev:close()
  M:exit()
end)
PDF_TICK = emu.add_machine_frame_notifier(function()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then print("pdf_test: ERROR " .. tostring(err)); E("ERROR %s", tostring(err)); M:exit() end
  end
end)
