-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU General Public License v3 or later
--
-- keyfall_test.lua -- Keyfall in MAME, through the program menu (run_keyfall.sh OUT):
--   * DISK MENU, STYLE CONVERT, OK: the loader, the menu; the "Keyfall (piano game)" row, RUN;
--   * the songs up: CHECK the card's MIDI files listed; $KF_SONG (default TWINKLE.MID) chosen with DOWN (R3), and the
--     choices set with the soft keys: $KF_PART presses of PART (L1), $KF_TEMPO (default 100) by TEMPO (L2), ACCOMP (L3)
--     off when $KF_ACCOMP=0, $KF_MODE (PLAY, WAIT, DEMO) by MODE (L4); a snapshot;
--   * PLAY (R1): the chart read from the game's memory (play.c play_chart), and each of its notes struck on the keybed
--     (MAME's keybed fields) when the game's clock (play_clock) reaches its time less $KF_LEAD ms, held for 70% of it
--     (60 to 400 ms); in WAIT mode $KF_WAIT_DELAY s (0.3) after the clock has stopped at it; $KF_SKIP notes left out (the 6th, the 16th, ...) and $KF_WRONG keys struck that strike no note
--     (halfway between two notes, a key above the chart's); $KF_NOPRESS=1 strikes nothing;
--   * meanwhile, write taps on MIDI IN 1's ring header (KN7APP fw.midiIn1): each time the game moves the write pointer
--     and each time the firmware's reader catches up, so the accompaniment's latency (from the game's message into the
--     ring to the firmware taking it) is measured; snapshots every $KF_SNAP s;
--   * the results: CHECK the game's counts against what was struck (PERFECT + GOOD + OK = the notes struck, MISSED =
--     those left out, WRONG KEY = the wrong keys), every key struck reached the game, every accompaniment message went
--     into the ring and the firmware took them all, and the time each struck note was judged at (its dt: the key's time
--     as the game saw it, less the note's) summed up;
--   * SONGS (L1), then EXIT held 2.5 s: CHECK the converter's end (request 0xFE).
-- Every CHECK is a "CHECK ok" or "CHECK FAIL" line in $KF_OUT/events.txt, the last lines count them.
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
local WAIT_DELAY = tonumber(os.getenv("KF_WAIT_DELAY") or "") or 0.3
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
-- a snapshot of the whole artwork, and of the LCD alone (snap/lcd-N.png, 640 x 240: the manual's pictures)
local screen
for _, sc in pairs(M.screens) do screen = screen or sc end
local nLcd = 0
local function snap(what)
  M.video:snapshot()
  if screen then screen:snapshot(string.format("lcd-%02d.png", nLcd)) end
  E("snapshot: %s (lcd-%02d.png)", what, nLcd)
  nLcd = nLcd + 1
end

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

-- the game's state (play.c, judge.c Judge, kf.h Chart and ChartNote)
local J = { score = 12, streak = 16, maxStreak = 20, perfect = 24, good = 28, ok = 32, miss = 36, wrong = 40, sumDt = 44, nDt = 48 }
local function judge(k) return sp:read_i32(SYM["_play_judge"] + J[k]) end
local NOTE_SIZE = 24
local function counts()
  return string.format("perfect %d good %d ok %d miss %d wrong %d, score %d, streak %d (longest %d)", judge("perfect"),
    judge("good"), judge("ok"), judge("miss"), judge("wrong"), judge("score"), judge("streak"), judge("maxStreak"))
end
local function perf()
  return string.format("frames %d, loop max %d ms, draw max %d ms, rows %d, messages %d (ring full %d), key-ons %d, clock %d",
    u32("_play_frames"), u32("_play_loop_max"), u32("_play_draw_max"), u32("_play_rows"), u32("_play_out_sent"),
    u32("_play_out_full"), u32("_play_key_ons"), s32("_play_clock"))
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
  local nSongs = s32("_nSongs")
  local want = nil
  for i = 0, nSongs - 1 do
    local name = cstr(SYM["_files"] + i * 20, 13)       -- PfFile: char name[13] (+3), u32 bytes
    E("song %d: %s, %d bytes", i, name, sp:read_u32(SYM["_files"] + i * 20 + 16))
    if name == SONG then want = i end
  end
  CHECK("MIDI files listed, " .. SONG .. " among them", nSongs > 0 and want ~= nil, nSongs)
  if not want then E("end"); ev:close(); M:exit(); return end
  for _ = 1, want do soft(R, 3); wait(0.2) end          -- DOWN
  wait(1.5)
  CHECK(SONG .. " selected and read", s32("_menu_sel") == want and s32("_songOk") == 0,
    string.format("sel %d, songOk %d", s32("_menu_sel"), s32("_songOk")))
  for _ = 1, PARTN do soft(L, 1) end                    -- PART
  for _ = 1, (100 - TEMPO) // 10 do soft(L, 2) end      -- TEMPO
  if not ACCOMP then soft(L, 3) end                     -- ACCOMP off
  for _ = 1, ({ PLAY = 0, WAIT = 1, DEMO = 2 })[MODE] do soft(L, 4) end
  wait(1)
  E("choices: parts %X, tempo %d, accompaniment %d, mode %d", u32("_opt_parts"), s32("_opt_tempo"), s32("_opt_accomp"),
    s32("_opt_mode"))
  snap("the songs")
  soft(R, 1)                                            -- PLAY
  CHECK("the game starts", wait_until(function() return u32("_play_state") == 1 end, 30), perf())
  local playAt = now()
  local chart = sp:read_u32(SYM["_play_chart"])
  local cn, cnote, cnOut = sp:read_u32(chart), sp:read_u32(chart + 4), sp:read_u32(chart + 8)
  local lo, hi = sp:read_u8(chart + 28), sp:read_u8(chart + 29)
  local notes = {}
  for i = 0, cn - 1 do
    local a = cnote + i * NOTE_SIZE
    notes[#notes + 1] = { t = sp:read_i32(a), dur = sp:read_u32(a + 4), note = sp:read_u8(a + 8), a = a }
  end
  E("chart: %d notes, keys %d-%d, %d messages out", cn, lo, hi, cnOut)
  -- what the script strikes
  local skip, wrongAt = {}, {}
  for k = 0, SKIP - 1 do if 5 + 10 * k < cn then skip[5 + 10 * k + 1] = true end end
  local wrongKey = math.min(hi + 1 + (((hi + 1) % 12 == 1 or (hi + 1) % 12 == 3 or (hi + 1) % 12 == 6 or (hi + 1) % 12 == 8 or (hi + 1) % 12 == 10) and 1 or 0), 96)
  for k = 0, WRONG - 1 do
    local i = 3 + 10 * k
    if i + 1 < cn then wrongAt[#wrongAt + 1] = (notes[i + 1].t + notes[i + 2].t) // 2 end
  end
  -- the MIDI IN 1 ring: the game writes its write pointer (+0x0C), the firmware's reader its read pointer (+8)
  local ring = sp:read_u32(SYM["_fw"] + 52)
  local start, last = sp:read_u32(ring), sp:read_u32(ring + 4)
  local size = last - start + 1
  local puts, lat = {}, {}
  local wrNow, rdNow = sp:read_u32(ring + 0x0C), sp:read_u32(ring + 8)
  local function behind(p, q) return (q - p) % size end  -- bytes from p forward to q
  local tap = sp:install_write_tap(ring + 8, ring + 0x0F, "midiring", function(offset, data, mask)
    local t = now()
    if offset == ring + 0x0C then
      wrNow = data
      puts[#puts + 1] = { t = t, wr = data }
    elseif offset == ring + 8 then
      -- the reader moved from rdNow to data: every message whose end lies in that stretch is taken
      local moved = behind(rdNow, data)
      while #puts > 0 do
        local d = behind(rdNow, puts[1].wr)
        if d > moved then break end
        lat[#lat + 1] = (t - puts[1].t) * 1000
        table.remove(puts, 1)
      end
      rdNow = data
    end
  end)
  local nextI, wi, held, pressedN, struck = 1, 1, {}, 0, 0
  local reachedAt = nil
  local lastSnap = now()
  while u32("_play_state") == 1 do
    local clock = s32("_play_clock")
    local t = now()
    for k, r in pairs(held) do
      if t >= r then KEYN[k]:set_value(0); held[k] = nil end
    end
    -- (WAIT: the clock stops at each note; the key is struck $KF_WAIT_DELAY s after it has)
    if MODE == "WAIT" and nextI <= cn and clock >= notes[nextI].t and not reachedAt then reachedAt = t end
    while not NOPRESS and nextI <= cn and clock >= notes[nextI].t - (MODE == "WAIT" and 0 or LEAD) and
          (MODE ~= "WAIT" or (reachedAt and t - reachedAt >= WAIT_DELAY)) do
      reachedAt = nil
      local x = notes[nextI]
      if not skip[nextI] then
        local f = KEYN[x.note]
        if f then
          if held[x.note] then f:set_value(0) end
          f:set_value(1)
          local hold = math.max(0.06, math.min(0.4, 0.7 * x.dur / 1000))
          for j = nextI + 1, cn do  -- (let go before the same key's next note)
            if notes[j].note == x.note then hold = math.min(hold, (notes[j].t - x.t - 40) / 1000); break end
          end
          held[x.note] = t + math.max(hold, 0.03)
          pressedN = pressedN + 1
          struck = struck + 1
        else
          E("no keybed field for note %d", x.note)
        end
      end
      nextI = nextI + 1
    end
    if not NOPRESS and wi <= #wrongAt and clock >= wrongAt[wi] then
      KEYN[wrongKey]:set_value(1); held[wrongKey] = t + 0.08
      pressedN = pressedN + 1
      wi = wi + 1
    end
    if t - lastSnap >= SNAP then snap(string.format("playing, clock %d; %s", clock, counts())); lastSnap = t end
    coroutine.yield()
  end
  for k, _ in pairs(held) do KEYN[k]:set_value(0) end
  local playS = now() - playAt
  wait(1)
  tap:remove()
  E("game over after %.1f s: state %d; %s; %s", playS, u32("_play_state"), counts(), perf())
  snap("the results")
  -- the times the struck notes were judged at
  local dts, hist = {}, {}
  for i = 1, cn do
    local st, dt = sp:read_u8(notes[i].a + 10), sp:read_i16(notes[i].a + 12)
    if st >= 1 and st <= 3 then dts[#dts + 1] = dt end
  end
  table.sort(dts)
  local sum = 0
  for _, d in ipairs(dts) do sum = sum + d; local b = 10 * math.floor(d / 10); hist[b] = (hist[b] or 0) + 1 end
  if #dts > 0 then
    E("judged at (key's time less the note's, ms): min %d median %d max %d mean %.1f, of %d notes (lead %d ms)", dts[1],
      dts[(#dts + 1) // 2], dts[#dts], sum / #dts, #dts, LEAD)
    local keys = {}
    for b, _ in pairs(hist) do keys[#keys + 1] = b end
    table.sort(keys)
    for _, b in ipairs(keys) do E("  dt %4d..%4d ms: %d", b, b + 9, hist[b]) end
  end
  if #lat > 0 then
    table.sort(lat)
    local ls = 0
    for _, v in ipairs(lat) do ls = ls + v end
    E("accompaniment: %d writes into MIDI IN 1's ring taken by the firmware %.1f-%.1f ms later, median %.1f, mean %.1f; %d not taken",
      #lat, lat[1], lat[#lat], lat[(#lat + 1) // 2], ls / #lat, #puts)
  end
  local mode = s32("_opt_mode")
  local done = u32("_play_state") == 2
  CHECK("the song played to its end", done, perf())
  CHECK("every key struck reached the game", u32("_play_key_ons") == pressedN,
    string.format("%d struck, %d key-ons", pressedN, u32("_play_key_ons")))
  if mode == 0 then
    CHECK("the notes struck were struck", judge("perfect") + judge("good") + judge("ok") == struck, counts())
    local skipped = 0
    for _ in pairs(skip) do skipped = skipped + 1 end
    CHECK("the notes left out were missed", judge("miss") == (NOPRESS and cn or skipped), counts())
    CHECK("the wrong keys were wrong keys", judge("wrong") == (NOPRESS and 0 or #wrongAt), counts())
  elseif mode == 1 then
    CHECK("WAIT: every note struck", judge("perfect") + judge("good") + judge("ok") == cn, counts())
    local clockS = s32("_play_clock") / 1000 + 2.5  -- (the clock starts 2.5 s before the song)
    CHECK("WAIT: the clock stopped at the notes", playS > clockS + 0.5 * cn * WAIT_DELAY,
      string.format("%.1f s played, the clock went %.1f s", playS, clockS))
  end
  CHECK("every accompaniment message went into the ring", u32("_play_out_sent") == cnOut,
    string.format("%d of %d (ring full %d times)", u32("_play_out_sent"), cnOut, u32("_play_out_full")))
  CHECK("the firmware took everything put into MIDI IN 1's ring", sp:read_u32(ring + 8) == sp:read_u32(ring + 0x0C),
    string.format("read %08X write %08X", sp:read_u32(ring + 8), sp:read_u32(ring + 0x0C)))
  wait(2)
  E("results: %s", counts())
  soft(L, 1)                                            -- SONGS
  CHECK("SONGS: the song list again", wait_until(function() return u32("_menu_screen") == 1 end, 10))
  wait(1)
  snap("the songs again")
  E("keyfall: %d checks, %d failed", checks, fails)
  local f = field("CPC_SEG11", 0x80); f:set_value(1); wait(2.5); f:set_value(0)
  local ended = wait_until(function() return sp:read_u8(0x5031BC00) == 0xFE end, 30)
  CHECK("EXIT held: the end", ended)
  wait(3)
  snap("after the end")
  E("keyfall: %d checks, %d failed", checks, fails)
  E("end")
  ev:close()
  M:exit()
end)
KF_TICK = emu.add_machine_frame_notifier(function()
  if coroutine.status(co) ~= "dead" then
    local ok, err = coroutine.resume(co)
    if not ok then print("keyfall_test: ERROR " .. tostring(err)); E("ERROR %s", tostring(err)); M:exit() end
  end
end)
