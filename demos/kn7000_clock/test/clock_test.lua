-- (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
-- Licensed under the terms of the GNU General Public License v3 or later
--
-- MAME check for the KN7000 uptime clock (`make test`). Boots -bios sp7001 with the
-- clock's SD card, presses the orange SD CARD button (input "SD CARD LOAD (SW7)") so
-- the firmware loads BASEEXT.XAP, then at a few times logs:
--   * the kernel task slots 9 and 10 (0x50380024/28; 0xFFFFFFFF = free)
--   * the clock box's border pixel (expected 0xC618 once drawn)
--   * the 8 digit cells read back from the LCD frame as ASCII art, next to the
--     HH:MM:SS computed from the kernel millisecond counter 0x500D3C68
-- and takes a snapshot. Log: $CLOCK_TEST_LOG (default build/clock_test.log).
local M=manager.machine; local sp=M.devices[":maincpu"].spaces["program"]
local out=io.open(os.getenv("CLOCK_TEST_LOG") or "build/clock_test.log","w")
local function L(s) out:write(s.."\n"); out:flush() end
local FB=0x9CE00000; local BOX_X,BOX_Y=489,209; local GX,GY=493,213
local function px(x,y) return sp:read_u16(FB+(y*640+x)*2) end
local function field(tag,name) local p=M.ioport.ports[tag]; if p then for _,f in pairs(p.fields) do if f.name==name then return f end end end end
local SD=field(":cpanel:CPR_SEG1","SD CARD LOAD (SW7)")
L("SD CARD LOAD field found: "..tostring(SD~=nil))
local pressed,released=false,false; local checks={20,28,36}; local i=1
emu.register_periodic(function()
  local t=M.time:as_double()
  if not pressed and t>11 then SD:set_value(1); pressed=true; L(string.format("t=%.2f press SD CARD",t)) end
  if pressed and not released and t>11.4 then SD:set_value(0); released=true end
  if i<=#checks and t>checks[i] then
    i=i+1
    local ms=sp:read_u32(0x500D3C68); local s=ms//1000
    L(string.format("\nt=%.2f  kernel ms=%d -> expected %02d:%02d:%02d", t, ms, (s//3600)%100, (s//60)%60, s%60))
    L(string.format("  task slot 9 = 0x%08X, slot 10 = 0x%08X", sp:read_u32(0x50380024), sp:read_u32(0x50380028)))
    L(string.format("  border pixel (%d,%d) = 0x%04X, pad pixel = 0x%04X", BOX_X, BOX_Y, px(BOX_X,BOX_Y), px(BOX_X+1,BOX_Y+1)))
    for row=0,20,3 do
      local line={}
      for c=0,7 do for x=0,17,3 do line[#line+1]=(px(GX+c*18+x, GY+row)==0xFFFF) and "#" or "." end; line[#line+1]=" " end
      L("  "..table.concat(line))
    end
    M.video:snapshot()
    if i>#checks then M:exit() end
  end
end)
