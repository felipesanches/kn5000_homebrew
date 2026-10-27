/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * dsp.c -- the module's four channels, played by the KN7000's effects DSP (IC306, an ADSP-21065L): the main
 * CPU's side. The DSP's side is Another World's eight-voice sample mixer, unchanged (technics-anotherworld
 * kn7000/tools/dsp_mixer.py; tools/build/dsp_c.py makes dsp_mixer.h from it). This file is that port's src/dsp.s in
 * C, routine for routine, and keeps its rules (its header says why):
 *   * nothing drives the host port directly: every access goes through the firmware's own routines, found by their
 *     code (dsp_find: DSPTONE's search), which keep the firmware's lock, its per-frame handshake and its "DSP dead"
 *     guard;
 *   * the DSP must be as the firmware leaves it before anything is touched; everything uploaded is read back; what
 *     is changed is put back: the two CALL slots (units 0 and 9), MSGR4-7, channel 8's registers.
 * The converter entry (entry.c) runs dsp_prepare, then starts the game task. When a module is loaded, the program
 * codes its samples in RAM (the Pinball port read them coded from the card instead) and the converter calls send them
 * (dsp_bank_begin, then dsp_bank_step, a slice of work a call), from the SDRAM bank's first word. Loading the same
 * module again, in this run or a later one, sends nothing: the marker names the bank and the DSP's checksum confirms
 * it. The program queues voice commands (dsp_set) and sends them once a replayer tick (dsp_flush). dsp_quit puts the
 * firmware's effects back before the conversion ends. */
#include "app.h"
#include "dsp_mixer.h"

/* ---- host-port constants (ADSP-21065L TRM Tables E-15, E-16; the firmware's own values) ---- */
#define IOP_SYSCON 0x00
#define IOP_EPB0 0x04
#define IOP_DMAC0 0x1C
#define IOP_DMASTAT 0x37
#define IOP_IIEP0 0x40
#define IOP_IMEP0 0x41
#define IOP_CEP0 0x42
#define DMAC_UP_PM 0xA1  /* the firmware's PM upload: DEN | DTYPE | PMODE 10 (16 -> 48) */
#define DMAC_UP_DM 0x41  /* its DM upload: DEN | PMODE 01 (16 -> 32) */
#define DMAC_CLOSE 0xA0
#define DMAC_RB_PM 0xA5  /* readback (the ROM dumper's pass 7): the same with TRAN */
#define DMAC_RB_DM 0x45
#define DMAC_FLSH 0x2000
#define DMAC_KEEP 0x1FE4
#define DATA_PORT 0x9C4
#define FS_POLLS 1000
#define COPY_POLLS 400
#define CMD_POLLS 20
#define STALL_CALLS 40
#define BANK_MS 400        /* bank work per converter call */
#define WATCH_MS 5000
#define MARK_MAGIC 0x31535450u  /* "PTS1", DM 0x9D00: a module's samples (Another World's is "AWS2", Pinball's "PFS3") */
#define BK_REC_MIN 3
#define RB_WORDS 0x50

Dsp dsp;
static u16 rbbuf[RB_WORDS * 3];

/* ---- the search: DspFind (Another World's dsp.s; technics-roms-disasm dsptone/find_dspw.py runs the same over
 * every program build: found and consistent in 893, 935, 941 and 942) ---- */
#define ANY 0x100
#define END 0xFFFF
static const u16 sig_dspwait[] = {
    0xcf, 0x80, 0xf8, 0xfe, 0xfc, 0xfc, 0xcc, 0xff, 0xff, 0x03, 0x00, 0x42, 0x00, 0xfc,
    0xa8, ANY, ANY, ANY, ANY, 0xa0, 0x00, 0xc9, 0x04, 0xca, 0x32, 0x80, 0x01, 0xfc,
    0x83, 0x5c, 0x01, 0x00, 0x34, 0xca, 0x1b, 0x58, 0x00, 0x8a, 0x01, 0x81, 0xf1, 0x09,
    0x46, 0x00, 0xa0, 0x00, 0xc8, 0x04, 0xca, 0x0c, 0x80, 0xff, 0xfc, 0x82, ANY, ANY,
    ANY, ANY, 0xca, 0x0f, 0xfc, 0xac, 0x5c, 0x01, 0x00, 0x34, 0xf8, 0xec, 0x10, 0xc9,
    0x04, 0xca, 0xdc, 0xdf, 0x80, 0x08, END};
static const u16 sig_dspregs[] = {
    0xf8, 0xfe, 0xf0, 0xf8, 0x97, 0x1c, 0x42, 0x18, 0xf8, 0xbc, 0x18, 0x1c, 0xcd, ANY,
    ANY, 0x00, 0x10, 0xf8, 0xbc, 0x1c, 0xf8, 0xbd, 0x20, 0xcd, ANY, ANY, 0x00, 0x10,
    0xdf, 0x00, 0x10, 0xf8, 0xfe, 0xf0, 0xf8, 0x97, 0x1c, 0x42, 0x18, 0xf8, 0xbc, 0x18,
    0x1c, 0xcd, ANY, ANY, 0x00, 0x10, 0xf8, 0xbc, 0x1c, 0xf8, 0xbd, 0x20, 0xcd, ANY,
    ANY, 0x00, 0x10, 0xdf, 0x00, 0x10, 0xf8, 0xfe, 0xe8, 0x42, 0x20, 0xf8, 0xbc, 0x20,
    0x1c, 0xcd, ANY, ANY, 0x00, 0x10, 0x2c, 0xc4, 0x09, 0xcd, ANY, ANY, 0x00, 0x0c,
    0x18, 0x42, 0x0c, 0x2c, 0xc4, 0x09, 0xcd, ANY, ANY, 0x00, 0x0c, 0x18, 0x42, 0x10,
    0x58, 0x10, 0xf8, 0xc0, 0x10, 0x59, 0x0c, 0xe4, 0x42, 0x14, 0xdf, 0x00, 0x18, 0xf8,
    0xfe, 0xe8, 0x42, 0x20, 0xf8, 0xbc, 0x20, 0x1c, 0xcd, ANY, ANY, 0x00, 0x10, 0x2c,
    0xc4, 0x09, 0xcd, ANY, ANY, 0x00, 0x14, 0x18, 0x42, 0x0c, 0x2c, 0xc4, 0x09, 0xcd,
    ANY, ANY, 0x00, 0x14, 0x18, 0x42, 0x10, 0x58, 0x10, 0xf8, 0xc0, 0x10, 0x59, 0x0c,
    0xe4, 0x42, 0x14, 0xdf, 0x00, 0x18, END};
/* the guarded data write's first 8 bytes, less the dead flag's address (941 0x48404EF8) */
static const u8 write_head[8] = {0xf8, 0x97, 0x1c, 0xf8, 0x93, 0x18, 0xfc, 0xa8};

/* A "call d16, regs, imm8" (cd lo hi regs imm) at a -> its target, *frame its imm8; 0 when there is none. */
static u32 call_target(u32 a, u32* frame) {
  const u8* p = (const u8*)a;
  if (p[0] != 0xCD) return 0;
  *frame = p[4];
  return a + (u32)(s32)(int16_t)(p[1] | (p[2] << 8));
}

/* The `calls` entry of a body its callers allocate `frame` for: body - 3, "add -frame, sp" (f8 fe -frame); or 0. */
static u32 check_entry(u32 body, u32 frame) {
  const u8* p = (const u8*)(body - 3);
  if (p[0] != 0xF8 || p[1] != 0xFE || ((p[2] + frame) & 0xFF)) return 0;
  return body - 3;
}

static u32 rd32u(u32 a) {
  const u8* p = (const u8*)a;
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}


void dsp_find(void) {
  u32 a, n, f;
  dsp.found = 0;
  if (fw_count(sig_dspwait, &a) == 1) {
    dsp.found |= DFB_WAIT;
    dsp.dwait = a;
    dsp.down = rd32u(a + 0x0F);
    dsp.dead = rd32u(a + 0x36);
  }
  if ((n = fw_count(sig_dspregs, &a)) != 1) return;
  dsp.found |= DFB_REGS;
  dsp.regwr = a;
  dsp.regrd = a + 0x3E;
  u32 t = call_target(dsp.regrd + 9, &f), e;  /* the read's body + 6: the index write */
  if (t && (e = check_entry(t, f))) dsp.idxwr = e, dsp.found |= DFB_IDX;
  t = call_target(dsp.regrd + 0x11, &f);     /* the read's body + 0xE: the guarded data read */
  if (t && (e = check_entry(t, f))) {
    dsp.datard = e;
    const u8* b = (const u8*)t;               /* its body: movbu (dead), d0 = fc a8 <dead> */
    if (b[0] == 0xFC && b[1] == 0xA8 && rd32u(t + 2) == dsp.dead) dsp.found |= DFB_DATA;
  }
  t = call_target(dsp.regwr + 0x17, &f);     /* the write's body + 0x14: the DM-word writer */
  if (!t || !check_entry(t, f)) return;
  t = call_target(t + 0x11, &f);             /* its body + 0x11: the guarded data write */
  if (!t || !(e = check_entry(t, f))) return;
  for (int i = 0; i < 8; ++i)
    if (((const u8*)t)[i] != write_head[i]) return;
  if (rd32u(t + 8) != dsp.dead) return;
  dsp.datawr = e;
  dsp.found |= DFB_WRITE;
}

/* ---- the firmware's routines, and what is built on them ---- */
static int dead_flag(void) { return dsp.dead ? RD8(dsp.dead) : 0; }
static int down_flag(void) { return dsp.down ? RD8(dsp.down) : 0; }

/* The firmware's read returns (high << 16) + the low half sign-extended: 0x10000 short when bit 15 is set. */
static u32 dsp_read(u32 reg) {
  u32 v = fw_call(dsp.regrd, reg, 0, 0, 0);
  if (v & 0x8000) v += 0x10000;
  return v;
}

/* The firmware's 2 x 16 register write, low half first; its third argument (the low half) at (0x10, sp). */
static void dsp_write(u32 reg, u32 v) { fw_call3(dsp.regwr, reg, v >> 16, v & 0xFFFF); }
static void dsp_index(u32 i) { fw_call(dsp.idxwr, i, 0, 0, 0); }
static u16 dsp_data(void) { return (u16)fw_call(dsp.datard, 0, 0, 0, 0); }
static void dsp_data_w(u16 v) { fw_call(dsp.datawr, DATA_PORT, v, 0, 0); }

/* A block of words: PM (three halves a word) or DM (two), least significant half first (the firmware's upload). */
static void dsp_upload(u32 addr, u32 words, const u16* halves, u32 mode) {
  dsp_write(IOP_IIEP0, addr);
  dsp_write(IOP_IMEP0, 1);
  dsp_write(IOP_CEP0, words);
  dsp_write(IOP_DMAC0, mode);
  dsp_index(IOP_EPB0);
  for (u32 n = words * (mode == DMAC_UP_PM ? 3 : 2); n; --n) dsp_data_w(*halves++);
  dsp_write(IOP_DMAC0, DMAC_CLOSE);
}

/* The pass-7 readback -> words read (fewer when it stopped early). */
static u32 dsp_readback(u32 addr, u32 words, u16* out, u32 mode) {
  u32 got = 0;
  if (dead_flag()) return 0;
  dsp_write(IOP_DMAC0, DMAC_FLSH);
  dsp_write(IOP_IIEP0, addr);
  dsp_write(IOP_IMEP0, 1);
  dsp_write(IOP_CEP0, words);
  dsp_write(IOP_DMAC0, mode);
  for (; got < words; ++got) {
    u32 polls = 0;
    for (; polls < FS_POLLS; ++polls) {
      if (dead_flag()) goto end;
      if (dsp_read(IOP_DMAC0) & 0xC000) break;  /* FS: EPB0 holds data */
    }
    if (polls == FS_POLLS) break;
    dsp_index(IOP_EPB0);
    *out++ = dsp_data();
    *out++ = dsp_data();
    if (mode & 0x20) *out++ = dsp_data();
  }
end:
  dsp_write(IOP_DMAC0, DMAC_FLSH);
  dsp_write(IOP_DMAC0, dsp.dmac0 & DMAC_KEEP);
  return got;
}

/* 0 when the DSP holds `words` words from addr as uploaded. */
static int verify(u32 addr, u32 words, const u16* was, u32 mode) {
  const u32 per = mode == DMAC_RB_PM ? 3 : 2;
  while (words) {
    const u32 n = words > RB_WORDS ? RB_WORDS : words;
    if (dsp_readback(addr, n, rbbuf, mode) != n) return 1;
    for (u32 i = 0; i < n * per; ++i)
      if (rbbuf[i] != *was++) return 1;
    addr += n, words -= n;
  }
  return 0;
}

/* A slot's 48 bits: 0, or 1 when the readback failed. */
static int read_slot(u32 slot, u32* hi, u32* lo) {
  if (dsp_readback(slot, 1, rbbuf, DMAC_RB_PM) != 1) return 1;
  *lo = rbbuf[0] | ((u32)rbbuf[1] << 16);
  *hi = rbbuf[2];
  return 0;
}

/* "CALL (target) (DB)" into a slot, read back: 0 when it holds. */
static int set_slot(u32 slot, u32 target) {
  const u32 lo = 0x04000000u | target;
  const u16 w[3] = {(u16)lo, (u16)(lo >> 16), (u16)DSP_CALLDB_HI};
  dsp_upload(slot, 1, w, DMAC_UP_PM);
  u32 hi, got;
  return read_slot(slot, &hi, &got) || hi != DSP_CALLDB_HI || got != lo;
}

/* A slot as the firmware keeps it: 0, *lo its low bits; 1 when not readable or not the kernel's form. A slot that
 * points into our PM area already (an earlier run that never put it back) gets the firmware's target first. */
static int found_slot(u32 slot, u32 def, u32* lo) {
  u32 hi;
  if (read_slot(slot, &hi, lo) || hi != DSP_CALLDB_HI || (*lo >> 16) != 0x0400) return 1;
  const u32 target = *lo & 0xFFFF;
  if (target < 0x8000 || target > 0x8FFF) return 1;
  if (target >= DSP_OURS_LO && target < DSP_OURS_HI) {
    if (set_slot(slot, def)) return 1;
    *lo = 0x04000000u | def;
  }
  return 0;
}

static void zero_msgr(void) {
  for (u32 r = DSP_MSGR4; r <= DSP_MSGR7; ++r) dsp_write(r, 0);
}

/* The voices silent, the slots as found, MSGR4-7 = 0, channel 8's registers as found. 0 when all read back. */
static int unhook(void) {
  int bad = 0;
  if (!dsp.hooked) return 0;
  if (dead_flag()) {
    dsp.hooked = 0;
    return 1;
  }
  if (dsp.hooked & 2) {  /* unit 9 first: it reads the mix unit 0 makes */
    if (set_slot(DSP_SLOT9, dsp.slot9 & 0xFFFF)) bad = 1;
    else dsp.hooked &= ~2u;
  }
  if (dsp.hooked & 1) {
    if (set_slot(DSP_SLOT0, dsp.slot0 & 0xFFFF)) bad = 1;
    else dsp.hooked &= ~1u;
  }
  zero_msgr();
  dsp_write(IOP_IIEP0, dsp.iiep0);
  dsp_write(IOP_IMEP0, dsp.imep0);
  dsp_write(IOP_CEP0, dsp.cep0);
  dsp_write(IOP_DMAC0, dsp.dmac0 & DMAC_KEEP);
  dsp.pend = 0;
  return bad;
}

static void give_up(u32 why) {
  if (!dsp.fail) dsp.fail = why;
  unhook();
  dsp.ready = 0;
  dsp.state = DS_OFF;
}

static int hook(void) {
  if (dead_flag()) return 1;
  dsp.hooked |= 1;
  if (set_slot(DSP_SLOT0, DSP_ENTRY0)) goto bad;
  if (dsp.u9) {
    dsp.hooked |= 2;
    if (set_slot(DSP_SLOT9, DSP_ENTRY9)) goto bad;
  }
  return 0;
bad:
  unhook();
  return 1;
}

static void mark_write(u32 magic, u32 id) {
  const u32 w[2] = {magic, id};
  dsp_upload(DSP_MARK, 2, (const u16*)w, DMAC_UP_DM);
}


/* 0: the mixer is loaded and hooked (DS_IDLE: no bank yet); else 1 (DS_OFF, why). */
int dsp_prepare(void) {
  u32 why = DF_NOTFOUND;
  if (dsp.found != DFB_ALL) goto fail;
  why = DF_DEAD;
  if (dead_flag()) goto fail;
  why = DF_NOTDOWN;
  if (!down_flag()) goto fail;
  dsp.syscon = dsp_read(IOP_SYSCON);
  dsp.dmac0 = dsp_read(IOP_DMAC0);
  dsp.iiep0 = dsp_read(IOP_IIEP0);
  dsp.imep0 = dsp_read(IOP_IMEP0);
  dsp.cep0 = dsp_read(IOP_CEP0);
  dsp.dmastat = dsp_read(IOP_DMASTAT);
  why = DF_STATE;  /* SYSCON: 16-bit host bus, low half first; DMAC0 idle; channel 8 not active or chaining */
  if ((dsp.syscon & 0x70) != 0x10 || (dsp.dmac0 & 3) || (dsp.dmastat & 0x10040)) goto fail;
  why = DF_SLOT;
  if (found_slot(DSP_SLOT0, DSP_DEF0, &dsp.slot0)) goto fail;
  dsp.u9 = !found_slot(DSP_SLOT9, DSP_DEF9, &dsp.slot9);
  dsp_upload(DSP_CODE_ADDR, DSP_CODE_WORDS, dsp_code, DMAC_UP_PM);
  dsp_upload(DSP_VARS, DSP_NVARS, (const u16*)dsp_vars, DMAC_UP_DM);
  zero_msgr();
  why = DF_DEAD;
  if (dead_flag()) goto fail;
  why = DF_VERIFY;
  if (verify(DSP_CODE_ADDR, DSP_CODE_WORDS, dsp_code, DMAC_RB_PM) ||
      verify(DSP_VARS, DSP_NVARS, (const u16*)dsp_vars, DMAC_RB_DM))
    goto fail;
  dsp.hb0 = dsp_read(DSP_MSGR7);
  why = DF_HOOK;
  if (hook()) goto fail;
  dsp.state = DS_IDLE;
  return 0;
fail:
  give_up(why);
  return 1;
}

/* The checksum of what is to be in SDRAM, from the bank's first word, asked of the mixer. */
static void sum_ask(void) {
  dsp_write(DSP_MSGR5, dsp.want_words);
  dsp_write(DSP_MSGR4, 2);
  dsp.phase = 1;
}

/* The blocks from their first again (not there, or there but not right). */
static void resend(void) {
  dsp.ptr = dsp.rec_lo;
  dsp.len = dsp.rec_lo + dsp.rec_bytes;
  dsp.sent = dsp.phase = dsp.done = 0;
  mark_write(0, 0);  /* not known to be there until the whole is again */
}

/* A bank from RAM (PfHost::dspSend) into SDRAM: `bytes` of coded blocks at `rec` (tools/dsp_bank.py's records),
 * filling `words` SDRAM words from BANK, whose checksum is `sum`; the marker (DM 0x9D00: MARK_MAGIC and `id`) names
 * what SDRAM holds. 0 when dsp_bank_step has work to do (DS_BANK), 2 when it cannot be (no DSP, or the marker
 * unreadable: given up). A bank the marker names already is not sent: the DSP's checksum decides (dsp_bank_step). */
int dsp_bank_begin(const u32* rec, u32 bytes, u32 words, u32 sum, u32 id) {
  if (dsp.state != DS_IDLE && dsp.state != DS_READY) return 2;
  if (dead_flag() || dsp_readback(DSP_MARK, 2, rbbuf, DMAC_RB_DM) != 2) {
    give_up(DF_VERIFY);
    return 2;
  }
  const u32 magic = rbbuf[0] | ((u32)rbbuf[1] << 16), was = rbbuf[2] | ((u32)rbbuf[3] << 16);
  dsp.rec_lo = (u32)rec;
  dsp.rec_bytes = bytes;
  dsp.want_words = words;
  dsp.want_sum = sum;
  dsp.want_id = id;
  dsp.to_send = words;
  dsp.ready = 0;
  dsp.t0 = now_ms();
  dsp.done = dsp.stalls = dsp.buf = dsp.held = dsp.phase = 0;
  dsp.state = DS_BANK;
  if (magic == MARK_MAGIC && was == id) {
    dsp.ptr = 0;
    dsp.sent = 2;  /* there by the marker: the DSP's checksum decides, nothing is sent */
    sum_ask();
  } else {
    resend();
  }
  return 0;
}

/* 1 when MSGR4 reads 0 (the mixer has finished the last copy) within COPY_POLLS reads. */
static int copy_idle(void) {
  for (u32 i = 0; i < COPY_POLLS; ++i)
    if (dsp_read(DSP_MSGR4) == 0) return 1;
  return 0;
}

/* 0 more to come, 1 the bank is in SDRAM and checked (DS_READY), 2 it failed (given up). BANK_MS of work a call:
 * Another World's BankStep, its blocks taken from RAM: hand them, one staging buffer while the DSP decodes the other,
 * to the mixer's decoder; after the last, the DSP's checksum against the bank's. */
int dsp_bank_step(void) {
  const u32 t0 = now_ms();
  int moved = 0;
  for (;;) {
    if (dead_flag()) {
      give_up(DF_RUNDEAD);
      return 2;
    }
    if (dsp.phase) {  /* the checksum asked for: its answer */
      if (copy_idle()) {
        if (dsp_read(DSP_MSGR5) == dsp.want_sum) {
          if (dsp.sent != 2) {
            mark_write(MARK_MAGIC, dsp.want_id);  /* so that a later run sends nothing */
            if (dead_flag()) {
              give_up(DF_RUNDEAD);
              return 2;
            }
            dsp.sent = 1;
          }
          dsp.ms = now_ms() - dsp.t0;
          dsp.ready = 1;
          dsp.state = DS_READY;
          return 1;
        }
        if (dsp.sent != 2) {
          give_up(DF_SUM);
          return 2;
        }
        resend();  /* found by its marker but not right: all of it sent again */
        moved = 1;
      }
    } else if (!dsp.ptr) {  /* every block sent: the checksum, once the last is decoded */
      if (copy_idle()) {
        sum_ask();
        moved = 1;
      }
    } else {  /* a block in hand: u32 n, then n DM words */
      const u32* rec = (const u32*)dsp.ptr;
      const u32 n = rec[0];
      if (n < BK_REC_MIN || n > DSP_STAGE_HALF || dsp.ptr + 4 + 4 * n > dsp.len || rec[1] + rec[2] > dsp.want_words) {
        give_up(DF_FILE);
        return 2;
      }
      if (!dsp.held) {
        dsp_upload(DSP_STAGE + dsp.buf * DSP_STAGE_HALF, n, (const u16*)(rec + 1), DMAC_UP_DM);
        dsp.held = 1;
      }
      if (copy_idle()) {  /* the block before decoded: hand this one over */
        dsp_write(DSP_MSGR5, dsp.buf);
        dsp_write(DSP_MSGR4, 1);
        dsp.held = 0;
        dsp.buf ^= 1;
        dsp.done += rec[2];
        dsp.ptr += 4 + 4 * n;
        if (dsp.ptr >= dsp.len) dsp.ptr = 0;
        moved = 1;
      }
    }
    if (now_ms() - t0 >= BANK_MS) break;
  }
  if (moved) dsp.stalls = 0;
  else if (++dsp.stalls >= STALL_CALLS) {
    give_up(DF_STALL);
    return 2;
  }
  return 0;
}

void dsp_quit(void) {
  if (dsp.state == DS_NONE || dsp.state == DS_OFF) return;
  unhook();
  dsp.state = DS_OFF;
}

/* ---- the voices (PfHost::dspSet, dspFlush): command records and a mask, sent once a frame ---- */
void dsp_set(u32 v, const PfVoice* p, u32 how) {
  v &= 7;
  u32* c = dsp.cmds + v * DSP_CMD_LEN;
  if (how == 0) {  /* stop */
    dsp.pend = (dsp.pend & ~(0x101u << v)) | (0x10000u << v);
  } else if (how == 1) {  /* start: BASE, INC, END, LOOP, VOL */
    c[0] = p->base, c[1] = p->step, c[2] = p->end, c[3] = p->loop, c[4] = p->volume;
    dsp.pend = (dsp.pend & ~(0x10101u << v)) | (1u << v);
  } else {  /* rate and volume */
    c[1] = p->step, c[4] = p->volume;
    dsp.pend |= 0x100u << v;
  }
}

void dsp_flush(void) {
  if (!dsp.pend) return;
  if (!(dsp.hooked & 1)) {
    dsp.pend = 0;
    return;
  }
  if (dead_flag()) {  /* the firmware has marked the DSP dead: no sound from here on */
    dsp.pend = dsp.hooked = dsp.ready = 0;
    dsp.fail = DF_RUNTIME;
    dsp.state = DS_OFF;
    return;
  }
  u32 polls = 0;
  while (dsp_read(DSP_MSGR6) != 0)
    if (++polls >= CMD_POLLS) {  /* the mixer has not taken the last ones yet: at the next frame */
      ++dsp.busy;
      return;
    }
  u32 records = (dsp.pend | (dsp.pend >> 8)) & 0xFF;
  if (records) {
    u32 first = 0, last = 0;
    while (!(records & (1u << first))) ++first;
    for (last = 7; !(records & (1u << last)); --last) {}
    dsp_upload(DSP_CMDS + first * DSP_CMD_LEN, (last + 1 - first) * DSP_CMD_LEN,
               (const u16*)(dsp.cmds + first * DSP_CMD_LEN), DMAC_UP_DM);
  }
  dsp_write(DSP_MSGR6, dsp.pend);
  ++dsp.flushes;
  dsp.pend = 0;
}
