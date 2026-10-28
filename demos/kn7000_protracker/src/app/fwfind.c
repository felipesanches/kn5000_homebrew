/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * fwfind.c -- the run-time firmware search. KN7APP.BIN carries no firmware address: on its first calls it
 * finds what it needs by the code, in the program flash and in the kernel library the boot copies to RAM,
 * exactly as Another World's KN7000 port does (technics-anotherworld kn7000/src/fwfind.s, whose signatures and
 * cross-checks these are; its tools/aw_find_fw.py runs the same searches over programs 893, 935, 941 and 942).
 * One step a converter call (entry.c):
 *   0 panel  the panel decoder's two sites, one hit each, naming the same bitmaps (CPL = CPR + 0x30) in work RAM
 *   1 clock  the yield routine in the library (one hit, its three counter operands equal) and the kernel's 1 kHz
 *            tick handler (one hit, its two counter operands equal to those, and to the loader's own find)
 *   2 files  the loader's service table: magic, ABI, file access found, open/read/close and the drive switch
 *   3 tasks  the firmware's "cre_tsk then sta_tsk(N, 0)" call sites: at least two, all agreeing, both stubs
 *   4 exit   its "clr d0; movbu d0, (X); call exd_tsk; ret" sites: at least two, agreeing, a third stub
 *   5 rings  the library's per-port MIDI ring wrappers: one hit, MIDI IN 1 below MIDI OUT 1, in work RAM
 * and, not needed: fw_find_dial, the TEMPO/PROGRAM wheel's LED. */
#include "app.h"

Fw fw;

#define FL_LO 0x48400000u
#define FL_HI 0x487FFF00u
#define LIB_LO 0x4C000000u
#define LIB_HI 0x4C040000u
#define WR_LO 0x50000000u
#define WR_HI 0x503FFF00u
#define ANY 0x100
#define END 0xFFFF

/* The signatures: 0x00nn = byte nn, ANY = any byte (an operand that moves between builds). The first
 * element is a byte. Another World's fwfind.s carries the same ones, with the addresses they come from. */
static const u16 sig_panel1[] = {0xfc, 0xdc, ANY, ANY, ANY, ANY, 0xf1, 0xd8, 0xf8, 0xe0, 0xc0, 0xc8, 0x08,
                                 0xfc, 0xdc, ANY, ANY, ANY, ANY, 0xf1, 0xd8, 0xf8, 0xe0, 0x0f, 0xf1, 0x60,
                                 0xf0, 0x40, 0xf0, 0x5c, END};
static const u16 sig_panel2[] = {0xfc, 0xdc, ANY, ANY, ANY, ANY, 0xf8, 0xef, 0xc0, 0xc8, 0x08,
                                 0xfc, 0xdc, ANY, ANY, ANY, ANY, 0xf8, 0xb8, 0x10, 0x85, 0x0f, 0xf2, 0x0d,
                                 0xf1, 0x64, END};
static const u16 sig_dly[] = {0xcf, 0xa0, 0xf8, 0xfe, 0xf0, 0xfc, 0xa6, ANY, ANY, ANY, ANY, 0xe2,
                              0xfc, 0xa4, ANY, ANY, ANY, ANY, 0xa2, 0xc7, ANY, 0x00, 0x81,
                              0xcd, ANY, ANY, 0x00, 0x00, 0xfc, 0xa4, ANY, ANY, ANY, ANY,
                              0xa2, 0xc5, ANY, 0x00, 0xdf, 0xa0, 0x18, END};
static const u16 sig_tick[] = {0xcf, 0x80, 0xf8, 0xfe, 0xf0, 0xfc, 0xa4, ANY, ANY, ANY, ANY, 0x40,
                               0xfc, 0x81, ANY, ANY, ANY, ANY, 0xf8, 0xec, 0x01, 0xc8, 0x09,
                               0xdd, ANY, ANY, ANY, ANY, 0x80, 0x0c, 0xfa, 0xfc, 0xff, 0xf7, 0xcb, 0xcb,
                               0xfc, 0xac, 0x18, 0x01, 0x00, 0x34, 0xfa, 0xe0, 0x00, 0xff, 0xf8, 0xe4, 0x01,
                               0xfc, 0x83, 0x18, 0x01, 0x00, 0x34, END};
static const u16 sig_cresta[] = {0xfc, 0xdc, ANY, ANY, ANY, ANY, 0xdd, ANY, ANY, ANY, ANY, 0x00, 0x00,
                                 0x80, ANY, 0x04, 0xdd, ANY, ANY, ANY, ANY, 0x00, 0x00, END};
static const u16 sig_exd[] = {0xfc, 0x82, ANY, ANY, ANY, ANY, 0xdd, ANY, ANY, ANY, ANY, 0x00, 0x00, 0xdf, END};
static const u16 sig_rings[] = {0x2c, 0xff, 0x03, 0xfc, 0x83, ANY, ANY, ANY, ANY, 0xde, 0x00, 0x00,
                                0xf8, 0xfe, 0xf0, 0xfc, 0xdc, ANY, ANY, ANY, ANY, 0x14, 0xcd, ANY, ANY,
                                0x30, 0x08, 0xdf, 0x00, 0x10, 0xf8, 0xfe, 0xf0, 0xfc, 0xdc, ANY, ANY, ANY, ANY,
                                0x14, 0xcd, ANY, ANY, 0x30, 0x08, 0xdf, 0x00, 0x10, END};

#ifndef PF_DIAG
/* SetDialLed's two setters, back to back (893 0x484B0AB3; one hit in 893, 935, 941 and 942: notes/
 * session-2026-10-27 dialled_sig.py): bit 1 of a byte of the LED shadow's bank A, then bit 7 of one of bank B. */
static const u16 sig_dial[] = {0xcf, 0x80, 0x81, 0xfc, 0xaa, ANY, ANY, ANY, ANY, 0x15, 0xf8, 0xe1, 0x01, 0xe5,
                               0xfc, 0xe2, 0xfd, 0xff, 0xff, 0xff, 0xf2, 0x19, 0xfc, 0x86, ANY, ANY, ANY, ANY,
                               0xde, 0x80, 0x04, 0xcf, 0x80, 0x81, 0xfc, 0xaa, ANY, ANY, ANY, ANY, 0x15, 0xf8,
                               0xe1, 0x01, 0xf8, 0xc1, 0x07, 0xfc, 0xe2, 0x7f, 0xff, 0xff, 0xff, 0xf2, 0x19,
                               0xfc, 0x86, ANY, ANY, ANY, ANY, 0xde, 0x80, 0x04, END};
#endif

static u32 rd32u(u32 a) {
  const volatile u8* p = (const volatile u8*)a;
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* The first address in [from, to) where the signature matches, or 0. */
static u32 find_sig(const u16* sig, u32 from, u32 to) {
  while (from < to) {
    const u8* hit = find_byte((const u8*)from, (const u8*)to, sig[0]);
    if (!hit) return 0;
    const u8* p = hit + 1;
    int i = 1;
    for (; sig[i] != END; ++i, ++p)
      if (sig[i] != ANY && *p != sig[i]) break;
    if (sig[i] == END) return (u32)hit;
    from = (u32)hit + 1;
  }
  return 0;
}

/* How many times it matches in [from, to); *first = the first. */
static int count_sig(const u16* sig, u32 from, u32 to, u32* first) {
  int n = 0;
  *first = 0;
  for (u32 at; (at = find_sig(sig, from, to)) != 0; from = at + 1)
    if (n++ == 0) *first = at;
  return n;
}

/* For dsp.c: how many times a signature matches in the program flash; *first = the first. */
int fw_count(const u16* sig, u32* first) { return count_sig(sig, FL_LO, FL_HI, first); }

static int in_work_ram(u32 a) { return a >= WR_LO && a <= WR_HI; }
static int in_lib(u32 a) { return a >= LIB_LO && a < LIB_HI; }
/* A kernel entry stub: "add -4, sp; mov d1, (0, sp)" (f8 fe fc 46 00). */
static int is_stub(u32 a) {
  const u8* p = (const u8*)a;
  return p[0] == 0xf8 && p[1] == 0xfe && p[2] == 0xfc && p[3] == 0x46 && p[4] == 0x00;
}

static int step_panel(void) {
  u32 a, b;
  if (count_sig(sig_panel1, FL_LO, FL_HI, &a) != 1 || count_sig(sig_panel2, FL_LO, FL_HI, &b) != 1) return 1;
  const u32 cpr = rd32u(a + 2), cpl = rd32u(a + 15);
  if (rd32u(b + 2) != cpr || rd32u(b + 13) != cpl || cpl != cpr + PANEL_CPL || !in_work_ram(cpr)) return 1;
  fw.panel = cpr;
  return 0;
}

static int step_clock(void) {
  u32 d, t;
  if (count_sig(sig_dly, LIB_LO, LIB_HI - 0x100, &d) != 1) return 1;
  const u32 ms = rd32u(d + 7);
  if (rd32u(d + 14) != ms || rd32u(d + 30) != ms) return 1;
  if (count_sig(sig_tick, FL_LO, FL_HI, &t) != 1) return 1;
  if (rd32u(t + 7) != ms || rd32u(t + 14) != ms || !in_work_ram(ms)) return 1;
  const u32 loaders = RD32(SVC + SV_MSCOUNT);
  if (loaders != 0 && loaders != ms) return 1;
  fw.dly = d;
  fw.ms = ms;
  return 0;
}

static int step_files(void) {
  if (RD32(SVC + SV_MAGIC) != SVC_MAGIC || RD32(SVC + SV_ABI) != LOADER_ABI) return 1;
  if (!(RD32(SVC + SV_FOUND) & (FOUND_FILESIG | FOUND_TABLES))) return 1;
  fw.open = RD32(SVC + SV_OPEN);
  fw.read = RD32(SVC + SV_READ);
  fw.close = RD32(SVC + SV_CLOSE);
  fw.setext = RD32(SVC + SV_SETEXT);
  fw.resetext = RD32(SVC + SV_RESETEXT);
  fw.write = RD32(SVC + SV_WRITE);   /* optional */
  fw.build = RD32(SVC + SV_BUILD);   /* optional */
  fw.raw = RD32(SVC + SV_FOUND) & FOUND_RAW ? RD32(SVC + SV_RAWREAD) : 0;  /* optional: listing a folder */
  return !fw.open || !fw.read || !fw.close || !fw.setext || !fw.resetext;
}

static int step_tasks(void) {
  u32 cre = 0, sta = 0;
  int n = 0, bad = 0;
  for (u32 at = FL_LO; (at = find_sig(sig_cresta, at, FL_HI)) != 0; ++at) {
    const u8* p = (const u8*)at;
    /* preceded by "mov N, d0" with the same N as the sta_tsk call's */
    if (p[-2] != 0x80 || p[-1] != p[14]) continue;
    const u32 c = at + 6 + rd32u(at + 7), s = at + 16 + rd32u(at + 17);
    if (!in_lib(c) || !in_lib(s)) continue;
    if (n == 0) cre = c, sta = s;
    else if (c != cre || s != sta) bad = 1;
    ++n;
  }
  if (bad || n < 2 || !is_stub(cre) || !is_stub(sta)) return 1;
  fw.cre = cre;
  fw.sta = sta;
  return 0;
}

static int step_exit(void) {
  u32 exd = 0;
  int n = 0, bad = 0;
  for (u32 at = FL_LO; (at = find_sig(sig_exd, at, FL_HI)) != 0; ++at) {
    if (((const u8*)at)[-1] != 0x00) continue;  /* preceded by "clr d0" */
    const u32 e = at + 6 + rd32u(at + 7);
    if (!in_lib(e)) continue;
    if (n == 0) exd = e;
    else if (e != exd) bad = 1;
    ++n;
  }
  if (bad || n < 2 || !is_stub(exd) || exd == fw.cre || exd == fw.sta) return 1;
  fw.exd = exd;
  return 0;
}

static int step_rings(void) {
  u32 r;
  if (count_sig(sig_rings, FL_LO, FL_HI, &r) != 1) return 1;
  const u32 in = rd32u(r + 17), out = rd32u(r + 35);
  if (in >= out || !in_work_ram(in) || !in_work_ram(out)) return 1;
  fw.midiIn1 = in;
  fw.midiOut1 = out;
  return 0;
}

#ifndef PF_DIAG
void fw_find_dial(void) {
  u32 d;
  fw.dial[0] = fw.dial[1] = 0;
  if (count_sig(sig_dial, FL_LO, FL_HI, &d) != 1) return;
  const u32 a = rd32u(d + 5), b = rd32u(d + 36);
  if (rd32u(d + 24) != a || rd32u(d + 57) != b || !in_work_ram(a) || !in_work_ram(b)) return;
  fw.dial[0] = a;
  fw.dial[1] = b;
}
#endif

int fw_step(int step) {
  switch (step) {
    case 0: return step_panel();
    case 1: return step_clock();
    case 2: return step_files();
    case 3: return step_tasks();
    case 4: return step_exit();
    case 5: return step_rings();
    default: return 1;
  }
}
