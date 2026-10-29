/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * kn7.h -- the KN7000 as the port sees it: the loader's contract (technics-roms-disasm
 * kn7000/tools/cnv_romdump/kn7cnv.inc, ABI 1), the program menu's launch block, the LCD, the panel, and the
 * memory map of the port (README.md, "Memory"). Shared by KN7APP.BIN (C) and the table programs (C++). */
#ifndef PF_KN7_H
#define PF_KN7_H
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;

/* ---- the loader (kn7cnv.inc) ---------------------------------------------------------------------- */
#define CNV_BASE 0x502B8000u
#define CNV_END 0x502E8000u
#define SVC 0x502B9E00u           /* the loader's service table: read, never written */
#define APP_BASE 0x502BA000u      /* KN7APP.BIN is loaded and linked here */
#define MAILBOX 0x5031BC00u       /* converter mailbox: byte 0 = our request, request 5's text after it */
#define RQ_TEXT 5                 /* "show this text, call me again": keeps the panel and keybed decoded */
#define RQ_END 0xFE               /* the end of the conversion */
#define LOADER_ABI 1
#define SV_MAGIC 0x00             /* "KN7S" */
#define SV_ABI 0x04
#define SV_OPEN 0x10              /* fmmopen(a0 = name, a1 = mode) -> d0 (16-bit, < 0 = error) */
#define SV_CLOSE 0x14
#define SV_READ 0x18              /* fmmread(a0 = buffer, d0 = bytes) -> d0 = bytes read */
#define SV_WRITE 0x1C
#define SV_RAWREAD 0x20            /* SD sector read(d0 = card sector, d1 = count <= 0x80, a1 = buffer) -> d0 = 0 */
#define SV_SETEXT 0x08            /* the file layer to the SD card (d0 = 1), or a no-op */
#define SV_RESETEXT 0x0C          /* and back */
#define SV_BUILD 0x38             /* the PROGRAM build number */
#define SV_MSCOUNT 0x60           /* the kernel's 1 kHz millisecond counter, or 0 */
#define SV_FOUND 0x64
#define FOUND_FILESIG 0x01
#define FOUND_TABLES 0x02
#define FOUND_RAW 0x04
#define SVC_MAGIC 0x53374E4Bu     /* "KN7S" */
#define APP_MAGIC 0x41374E4Bu     /* "KN7A" */

/* ---- the program menu's launch block (kn7cnv.inc LB_*): read once, never written ------------------- */
#define LB_ADDR 0x502E7F00u
#define LB_LEN 0x100u
#define LB_MAGIC 0x00
#define LB_PREFIX 0x20            /* "PINBALL\": put in front of a file name for SV_OPEN */
#define LB_CHECK 0xFC
#define LAUNCH_MAGIC 0x4C374E4Bu
#define LAUNCH_CHECK 0xB3C8B1B4u

/* ---- the LCD: 640 x 240 RGB565, uncached, 16-bit bus with wait states ----------------------------- */
#define FB 0x9CE00000u
/* Its display memory (the S1D13806's 1280 KB, from FB): the firmware's frame at 0 (640 x 240 x 2 bytes), the program's
 * 8-bit frame at LCD_FRAME (Doom's i_video.c, ProTracker's video.c), the borrowed RAM's contents at LCD_BORROW
 * (borrow.c), and LCD_FREE up to the firmware's ink layer free for the program. */
#define LCD_MEM FB
#define LCD_FRAME 0x50000u
#define LCD_BORROW 0x76000u
#define LCD_FREE 0x108000u
#define LCD_INK 0x136000u
#define FB_W 640
#define FB_H 240
#define FB_STRIDE 1280

/* ---- the panel: the firmware's switch bitmaps (found at run time: fwfind.c). CPR's columns are bytes
 * 0..9 of the bitmap, CPL's and CPC's 0x30 + column (as the MAME driver's CPx_SEGn ports). ------------ */
#define PANEL_CPL 0x30
#define PB(seg_ofs, bit) (((seg_ofs) << 8) | (bit))
#define BTN_EXIT PB(PANEL_CPL + 11, 0x80)        /* CPC_SEG11 SW7 */
#define BTN_PAGE_UP PB(PANEL_CPL + 11, 0x10)     /* CPC_SEG11 SW4 */
#define BTN_START_STOP PB(PANEL_CPL + 0, 0x10)   /* CPL_SEG0 SW4 */
#define BTN_INTRO2 PB(PANEL_CPL + 0, 0x40)       /* CPL_SEG0 SW6: INTRO & ENDING 2 */
#define BTN_INTRO1 PB(PANEL_CPL + 3, 0x01)       /* CPL_SEG3 SW0: INTRO & ENDING 1 */
#define BTN_LCDL1 PB(PANEL_CPL + 0, 0x02)
#define BTN_LCDL2 PB(PANEL_CPL + 0, 0x08)
#define BTN_LCDL3 PB(PANEL_CPL + 0, 0x20)
#define BTN_LCDL4 PB(PANEL_CPL + 0, 0x01)
#define BTN_LCDL5 PB(PANEL_CPL + 0, 0x04)
#define BTN_LCDR1 PB(5, 0x10)
#define BTN_LCDR2 PB(5, 0x20)
#define BTN_LCDR3 PB(7, 0x01)
#define BTN_LCDR4 PB(6, 0x01)
#define BTN_LCDR5 PB(5, 0x01)
#define BTN_SUSTAIN PB(5, 0x08)                  /* CPR_SEG5 SW3 */
#define BTN_PART_R1 PB(1, 0x02)                  /* CPR_SEG1 SW1: PART SELECT RIGHT 1 */
#define BTN_PAGE_DOWN PB(PANEL_CPL + 11, 0x20)   /* CPC_SEG11 SW5 */
#define BTN_OTHER_PARTS PB(PANEL_CPL + 5, 0x01)  /* CPC_SEG5 SW0: OTHER PARTS/TG */
#define BTN_HELP PB(PANEL_CPL + 5, 0x02)         /* CPC_SEG5 SW1: HELP (Another World's vm.inc) */
#define BTN_MSA1 PB(PANEL_CPL + 4, 0x10)         /* CPL_SEG4 SW4: VARIATION & MSA 1 */
#define BTN_MSA2 PB(PANEL_CPL + 4, 0x04)         /* CPL_SEG4 SW2: VARIATION & MSA 2 */
#define BTN_MSA3 PB(PANEL_CPL + 4, 0x01)         /* CPL_SEG4 SW0: VARIATION & MSA 3 */
#define BTN_MSA4 PB(PANEL_CPL + 3, 0x40)         /* CPL_SEG3 SW6: VARIATION & MSA 4 */
#define BTN_SPLIT_POINT PB(PANEL_CPL + 3, 0x80)  /* CPL_SEG3 SW7: SPLIT POINT */

/* ---- the port's memory (README.md, "Memory"). Everything the port writes is in [PF_LO, PF_HI) but the
 * mailbox page and the launch block; nothing in the user-memory pools, IC23 or the loader's area. ------ */
#define PF_LO APP_BASE
#define PF_HI 0x5037A000u          /* 0x5037A000 on: the custom-flash directory edit buffer */
#define MAILBOX_PAGE 0x5031B000u
#define MAILBOX_PAGE_END 0x5031C000u
#define APP_END 0x502C8000u        /* KN7APP.BIN: image, data, bss and the game task's stack (24 KB for Doom), below this */
#define GAME_BASE 0x502E8000u      /* a table program (PFn.BIN): image and bss, from here ... */
#define GAME_END MAILBOX_PAGE      /* ... to the mailbox page */
#define ARENA1_LO APP_END          /* the free RAM the table program takes its memory from */
#define ARENA1_HI LB_ADDR
#define ARENA2_LO MAILBOX_PAGE_END
#define ARENA2_HI PF_HI
/* Two more, outside the converter's area (README.md, "Memory"): RAM that no part of the firmware wrote from boot
 * through a whole converter run in MAME on program 893 (research-scratch aw-memory-plan/taps893_ranges.out),
 * tests/mame/pf_test.lua watches them again while the game runs:
 *   ARENA3: 0x50298000-0x502B7FFF, zeroed at reset and unused by programs 893-941; program 942 (SP7001) keeps its
 *           SD plug-in there, so it is not used on 942;
 *   ARENA4: the zlib arena's (0x503BB800-0x503FFDBF) whole pages above the RTOS pool's tail; the firmware's
 *           uncompress and the homepage loaders set it up afresh each time they use it. */
#define ARENA3_LO 0x50298000u
#define ARENA3_HI 0x502B7400u
#define FWDRAW_RECORDS 0x502B7400u /* ARENA3's last 3 KB: KN7APP's copy of program 893's view records (fwdraw.c) */
#define ARENA4_LO 0x503BC000u
#define ARENA4_HI 0x503FF000u
/* Three more, on programs 893 and 941 only (notes/ram-inventory-2026-10-27: no code or table of theirs points into them;
 * PFT18 kept a pattern in each for two minutes of play on the owner's KN7000, program 893, and none of it changed):
 *   ARENA5: the style memory's backup (893 0x50122E04, 941 0x50122E28, 0x25800 bytes), the disk style load's copy,
 *           made and put back inside that one routine of the main task, which the converter keeps busy;
 *   ARENA6: the work RAM above .bss's last object (the JPEG block table: 893 to 0x501749C4, 941 to 0x501749E8);
 *   ARENA7: the library SRAM above the library (893 0x3DF02 bytes, 941 0x3DF15) to the 3 bytes the boot copies to
 *           0x4C03FFF0: fast SRAM, cached. */
#define ARENA5_LO 0x50123000u
#define ARENA5_HI 0x50148000u
#define ARENA6_LO 0x50175000u
#define ARENA6_HI 0x50180000u
#define ARENA7_LO 0x4C03DF20u
#define ARENA7_HI 0x4C03FFF0u
#define BUILD_SP7001 942

#endif
