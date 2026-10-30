/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * notes.c -- the keys held on the keybed (and over MIDI IN 1), from the firmware's MIDI rings: the bytes the
 * keybed sends to MIDI OUT 1, and those MIDI IN 1 receives. Each ring is read with a read pointer of our own;
 * the firmware's consumer is never touched. As Another World's port reads them (technics-anotherworld
 * kn7000/src/interp.s NotePollSource): note-on sets the channel's bit for the note, note-off (or velocity 0)
 * clears it, CC 120 and 123-127 clear the channel; CC 64, the sustain pedal, sets the channel's pedal bit at 64
 * and up and clears it below (and CC 121 resets it); running status as MIDI has it, realtime bytes skipped,
 * system bytes cancel it. A read pointer outside the ring, or a backlog over half of it (the ring was reset
 * under us), starts again from the newest byte with no notes held.
 * For Keyfall (ABI 15) the presses and releases of channels 1-3 (the parts the keybed plays: RIGHT 1, RIGHT 2, LEFT) are
 * also kept in order with the kernel's millisecond at which they were found (key_events), a key once however many of
 * those parts sound it; and midi_in puts any channel message into MIDI IN 1's ring, as the keybed's mute below does. */
#include "app.h"

#define QH_START 0x00  /* u32: the ring's first byte */
#define QH_LAST 0x04   /* u32: its last byte */
#define QH_WR 0x0C     /* u32: where the producer writes next */

typedef struct Tracker {
  u32 header;
  u32 rp;              /* our read pointer (0: start from the newest byte) */
  u8 status, d1, count;
  u16 mask[128];
  u16 pedal;           /* the channels whose sustain pedal is down */
} Tracker;

static Tracker kbd, midi;

/* ABI 15: the keys pressed and let go on channels 1-3, in order, with their time (PfHost::keyEvents). Written by
 * notes_poll and read by key_events, both on the program's task. */
#define KEY_CH 0x0007u
#define KEV 64
static PfKeyEvent kev[KEV];
static u32 kev_wr, kev_rd, kev_now;
u32 kev_lost;  /* events dropped: the program did not take them in time */

static void kev_put(u8 note, u8 vel, u8 ch, u8 on) {
  if (kev_wr - kev_rd >= KEV) {
    ++kev_lost;
    return;
  }
  PfKeyEvent* e = &kev[kev_wr % KEV];
  e->ms = kev_now, e->note = note, e->velocity = vel, e->channel = ch, e->on = on;
  ++kev_wr;
}

u32 key_events(PfKeyEvent* ev, u32 max) {
  u32 n = 0;
  while (kev_rd != kev_wr && n < max) ev[n++] = kev[kev_rd++ % KEV];
  return n;
}

static u16 held(u8 note) { return (u16)((kbd.mask[note] | midi.mask[note]) & KEY_CH); }
u16 notes[128];
u8 notes_mod_wheel;    /* CC 1, the modulation wheel, as the keybed (or MIDI IN 1) last sent it */

/* The note-ons in the order they came (ABI 11 PfHost::noteOns): a chord, or a key struck twice between two polls, each
 * on its own. Read by the program's task, written by notes_poll on the same task: no lock. */
#define ONS 32
static u8 on_note[ONS], on_vel[ONS];
static u32 on_wr, on_rd;

u32 notes_ons(u8* note, u8* vel, u32 max) {
  u32 n = 0;
  while (on_rd != on_wr && n < max) {
    note[n] = on_note[on_rd % ONS];
    vel[n++] = on_vel[on_rd % ONS];
    ++on_rd;
  }
  return n;
}
u16 notes_sustain;     /* the channels whose sustain pedal is down, from either ring */

void notes_init(void) {
  kbd.header = fw.midiOut1;
  midi.header = fw.midiIn1;
  kbd.rp = midi.rp = 0;
}

static void resync(Tracker* t, u32 wr) {
  t->rp = wr;
  t->status = t->count = 0;
  for (int n = 0; n < 128; ++n) t->mask[n] = 0;
  t->pedal = 0;
}

static void poll(Tracker* t) {
  if (!t->header) return;
  const u32 start = RD32(t->header + QH_START), last = RD32(t->header + QH_LAST);
  const u32 wr = RD32(t->header + QH_WR);  /* once: the producer may be an interrupt */
  if (start == 0 || wr < start || wr > last) return;  /* not set up yet, or moving */
  u32 rp = t->rp;
  if (rp < start || rp > last) return resync(t, wr);
  u32 backlog = wr >= rp ? wr - rp : wr + (last - start + 1) - rp;
  if (backlog > (last - start) / 2) return resync(t, wr);
  while (rp != wr) {
    const u8 b = RD8(rp);
    rp = rp == last ? start : rp + 1;
    if (b >= 0xF8) continue;                       /* realtime */
    if (b >= 0xF0) {                               /* system: cancels running status */
      t->status = t->count = 0;
      continue;
    }
    if (b >= 0x80) {
      t->status = b;
      t->count = 0;
      continue;
    }
    if (!t->status) continue;
    if (t->count == 0) {
      t->d1 = b;
      if ((t->status & 0xE0) != 0xC0) t->count = 1;  /* program change and channel pressure: one byte */
      continue;
    }
    t->count = 0;
    const u16 bit = (u16)(1u << (t->status & 0x0F));
    const u16 was = held(t->d1);
    switch (t->status & 0xF0) {
      case 0x90:  /* (velocity 0: a note-off) */
        if (b) {
          t->mask[t->d1] |= bit;
          if ((bit & KEY_CH) && !was) kev_put(t->d1, b, (u8)(t->status & 0x0F), 1);
          if (on_wr - on_rd < ONS) {
            on_note[on_wr % ONS] = t->d1;
            on_vel[on_wr % ONS] = b;
            ++on_wr;
          }
        } else {
          t->mask[t->d1] &= (u16)~bit;
          if (was && !held(t->d1)) kev_put(t->d1, 0, (u8)(t->status & 0x0F), 0);
        }
        break;
      case 0x80:
        t->mask[t->d1] &= (u16)~bit;
        if (was && !held(t->d1)) kev_put(t->d1, 0, (u8)(t->status & 0x0F), 0);
        break;
      case 0xB0:
        if (t->d1 == 120 || t->d1 >= 123)
          for (int n = 0; n < 128; ++n) {
            const u16 w = held((u8)n);
            t->mask[n] &= (u16)~bit;
            if (w && !held((u8)n)) kev_put((u8)n, 0, (u8)(t->status & 0x0F), 0);
          }
        if (t->d1 == 1) notes_mod_wheel = b;
        if (t->d1 == 64 && b >= 64) t->pedal |= bit;
        else if (t->d1 == 64 || t->d1 == 121) t->pedal &= (u16)~bit;
        break;
    }
  }
  t->rp = rp;
}

void notes_poll(void) {
  kev_now = now_ms();
  poll(&kbd);
  poll(&midi);
  for (int n = 0; n < 128; ++n) notes[n] = kbd.mask[n] | midi.mask[n];
  notes_sustain = kbd.pedal | midi.pedal;
}

/* ---- the keybed's own sound: expression (CC 11) on channels 1-3, the receive parts the keybed plays (RIGHT 1,
 * RIGHT 2, LEFT), into the MIDI IN 1 ring as if received: 0 mutes, 127 brings it back. Expression is a live
 * controller, not a panel setting: nothing of the player's settings changes, and a power cycle resets it anyway.
 * As Another World's port does it (technics-anotherworld kn7000/src/audio.s MidiPut, KeysSync). ---- */
#define QH_FREE 0x10  /* u16: bytes the producer may still write */
static u32 keys_want = 1, keys_sent = 1;  /* 1: the keybed sounds (as the instrument has it when the game starts) */

/* One channel message of n bytes (1 to 3) into MIDI IN 1's ring, whole or not at all (0, or 1 when the ring lacks room).
 * Interrupts are off meanwhile, as in the receive interrupt's own enqueue. */
static int midi_put(const u8* b, u32 n) {
  const u32 h = fw.midiIn1;
  if (!h || n < 1 || n > 3) return 1;
  u32 psw;
  __asm__ volatile("mov psw,%0\n\tand 0xf7ff,psw\n\tnop\n\tnop" : "=d"(psw)::"memory");
  int ok = 0;
  const u16 free = *(volatile u16*)(h + QH_FREE);
  if (free >= n) {
    *(volatile u16*)(h + QH_FREE) = (u16)(free - n);
    u32 wr = RD32(h + QH_WR);
    const u32 start = RD32(h + QH_START), last = RD32(h + QH_LAST);
    for (u32 i = 0; i < n; ++i) {
      *(volatile u8*)wr = b[i];
      wr = wr == last ? start : wr + 1;
    }
    RD32(h + QH_WR) = wr;  /* published last: the consumer stops at the write pointer */
    ok = 1;
  }
  __asm__ volatile("mov %0,psw\n\tnop" ::"d"(psw) : "memory");
  return !ok;
}

static int midi_put3(u8 status, u8 d1, u8 d2) {
  const u8 b[3] = {status, d1, d2};
  return midi_put(b, 3);
}

/* ABI 15 (PfHost::midiIn): a message of the program's; the channels it used are remembered for midi_panic */
static u16 midi_used;
u32 midi_in_sent, midi_in_full;
int midi_in(const u8* msg, u32 n) {
  if (!n || !(msg[0] & 0x80) || msg[0] >= 0xF0) return 0;  /* (channel messages only) */
  const int r = midi_put(msg, n);
  if (r) ++midi_in_full;
  else ++midi_in_sent, midi_used |= (u16)(1u << (msg[0] & 0x0F));
  return r;
}

/* At the end (task.c): on every channel the program used, Reset All Controllers (the sustain pedal up, the pitch bend
 * centred) and All Notes Off; a channel tried again at the next yield while the ring is full. */
void midi_panic(void) {
  for (u8 ch = 0; ch < 16; ++ch) {
    if (!(midi_used & (1u << ch))) continue;
    if (midi_put3((u8)(0xB0 | ch), 121, 0) || midi_put3((u8)(0xB0 | ch), 123, 0)) return;
    midi_used &= (u16)~(1u << ch);
  }
}

void keys_sound(int on) { keys_want = on ? 1 : 0; }
int keys_sound_on(void) { return (int)keys_want; }

/* Tells the instrument what keys_sound asked, when it has not been told yet; tried again while the ring is full. */
void keys_sync(void) {
  if (keys_want == keys_sent) return;
  for (u8 ch = 0; ch < 3; ++ch)
    if (midi_put3((u8)(0xB0 | ch), 11, keys_want ? 127 : 0)) return;  /* (all three again next time) */
  keys_sent = keys_want;
}
