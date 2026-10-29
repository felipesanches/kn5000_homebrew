/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * folder.c -- the files in the program's folder, read off the card as the program menu finds its programs
 * (kn5000-roms-disasm kn7000/tools/kn7menu/menu_card.s, README.md "How it finds the programs"): raw sectors through
 * the loader's SV_RAWREAD, the routine the firmware's FAT library reads every sector with, and a read-only FAT16
 * reader on top, with the menu's rules and bounds. The firmware's file routines the loader hands over (fmmopen and the
 * rest) open a file by its name; none of them lists a folder. Nothing here writes to the card. On the main task (the
 * entry), served as files.c's requests are. */
#include "app.h"

#define SECS 4            /* sectors read at once */
#define ROOT_MAXSECS 256  /* of the root directory looked at */
#define DIR_MAXCL 64      /* clusters of one folder's directory looked at */

static u8 buf[SECS * 512];
static u8 fatbuf[512];
static u32 spc, fatlba, rootlba, rootsecs, datalba, maxcl, fatsec;

typedef struct Dir {
  u32 cl, lba, left, ncl;  /* the cluster (0: the root), its next sector, the sectors left of it, clusters so far */
  const u8 *p, *end;       /* the entries read, in buf */
  int err;
} Dir;

static u32 rd16(const u8* p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 rd32(const u8* p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

/* count sectors from `sector` into `to` -> 0, or the routine's code; a changed card (6) is read once more */
static u32 raw(u32 sector, u32 count, u8* to) {
  u32 r = 0;
  file_busy = 1;
  for (int tries = 0; tries < 2; ++tries)
    if ((r = fw_call(fw.raw, sector, count, 0, (u32)to)) != 6) break;
  file_busy = 0;
  return r;
}

/* The card's geometry, as the menu's Geometry takes it: sector 0 a boot sector or an MBR whose first partition holds
 * one; 512-byte sectors, one or two FATs, a fixed root directory, FAT16 (4,085 to 65,524 clusters). -> 1 when so. */
static int geometry(void) {
  if (raw(0, 1, buf)) return 0;
  u32 part = 0;
  if (buf[0] != 0xEB && buf[0] != 0xE9) {
    part = rd32(buf + 0x1C6);
    if (raw(part, 1, buf)) return 0;
  }
  spc = buf[0x0D];
  const u32 res = rd16(buf + 0x0E), fats = buf[0x10], ents = rd16(buf + 0x11), spf = rd16(buf + 0x16);
  if (rd16(buf + 0x0B) != 512 || !spc || (spc & (spc - 1)) || !res || !fats || fats > 2 || !ents || !spf) return 0;
  fatlba = part + res;
  rootlba = fatlba + fats * spf;
  rootsecs = (ents + 15) / 16;
  datalba = rootlba + rootsecs;
  u32 total = rd16(buf + 0x13);
  if (!total) total = rd32(buf + 0x20);
  if (total <= datalba - part) return 0;
  const u32 clusters = (total - (datalba - part)) / spc;
  if (clusters < 4085 || clusters >= 65525) return 0;
  maxcl = clusters + 1;
  fatsec = ~0u;
  return 1;
}

/* the cluster after cl in its chain (>= 0xFFF8: the end), or 0 when the FAT could not be read */
static u32 fat_next(u32 cl) {
  const u32 s = fatlba + (cl >> 8);
  if (s != fatsec) {
    fatsec = ~0u;
    if (raw(s, 1, fatbuf)) return 0;
    fatsec = s;
  }
  return rd16(fatbuf + (cl & 0xFF) * 2);
}

static int valid(u32 cl) { return cl >= 2 && cl <= maxcl; }

static void dir_open(Dir* d, u32 cl) {
  d->cl = cl;
  d->p = d->end = buf;
  d->ncl = 1;
  d->err = 0;
  if (!cl) {
    d->lba = rootlba;
    d->left = rootsecs < ROOT_MAXSECS ? rootsecs : ROOT_MAXSECS;
  } else if (valid(cl)) {
    d->lba = datalba + (cl - 2) * spc;
    d->left = spc;
  } else {
    d->left = 0;
    d->err = 1;
  }
}

/* the next 32-byte entry, or 0 after the last one (a first byte of 0), the chain's end, or an error (err) */
static const u8* dir_next(Dir* d) {
  for (;;) {
    if (d->err) return 0;
    if (d->p < d->end) {
      const u8* e = d->p;
      d->p += 32;
      return e[0] ? e : 0;
    }
    if (!d->left) {
      if (!d->cl) return 0;  /* (the root has no next cluster) */
      const u32 n = fat_next(d->cl);
      if (n >= 0xFFF8 || ++d->ncl > DIR_MAXCL) return 0;
      if (!valid(n)) {
        d->err = 1;
        return 0;
      }
      d->cl = n;
      d->lba = datalba + (n - 2) * spc;
      d->left = spc;
    }
    const u32 k = d->left < SECS ? d->left : SECS;
    if (raw(d->lba, k, buf)) {
      d->err = 1;
      return 0;
    }
    d->lba += k;
    d->left -= k;
    d->p = buf;
    d->end = buf + k * 512;
  }
}

/* an entry's 8.3 name as fmmopen takes it ("MOD2.MOD"), or 0 for what is not a file or folder of its own: deleted,
 * a long-name part, the volume label, "." and ".." */
static int entry_name(const u8* e, char* name) {
  if (e[0] == 0xE5 || e[0] == '.' || (e[11] & 0x0F) == 0x0F || (e[11] & 0x08)) return 0;
  int n = 0;
  for (int i = 0; i < 8 && e[i] != ' '; ++i) name[n++] = (char)(i == 0 && e[0] == 0x05 ? 0xE5 : e[i]);
  if (e[8] != ' ') {
    name[n++] = '.';
    for (int i = 8; i < 11 && e[i] != ' '; ++i) name[n++] = (char)e[i];
  }
  name[n] = 0;
  return n > 0;
}

static int same_name(const char* a, const char* b, int bn) {
  int i = 0;
  for (; i < bn && a[i]; ++i) {
    char x = a[i], y = b[i];
    if (x >= 'a' && x <= 'z') x = (char)(x - 32);
    if (y >= 'a' && y <= 'z') y = (char)(y - 32);
    if (x != y) return 0;
  }
  return i == bn && !a[i];
}

/* The files (not folders) in the folder `path` names relative to the root ("PROTRACK\", or "" for the root itself),
 * at most max of them, in the directory's order -> how many, or -1 (no raw sector read), -2 (not a FAT16 card),
 * -3 (no such folder), -4 (a read error, or a broken chain). */
s32 folder_list(const char* path, PfFile* files, u32 max) {
  if (!fw.raw) return -1;
  if (!geometry()) return -2;
  Dir d;
  char name[13];
  dir_open(&d, 0);
  for (const char* s = path; *s;) {  /* each folder of the path, from the root down */
    int n = 0;
    while (s[n] && s[n] != '\\') ++n;
    u32 cl = 0;
    for (const u8* e; n && (e = dir_next(&d)) != 0;)
      if ((e[11] & 0x10) && entry_name(e, name) && same_name(name, s, n)) {
        cl = rd16(e + 26);
        break;
      }
    if (d.err) return -4;
    if (n) {
      if (!cl) return -3;
      dir_open(&d, cl);
    }
    s += n;
    if (*s == '\\') ++s;
  }
  u32 count = 0;
  for (const u8* e; count < max && (e = dir_next(&d)) != 0;)
    if (!(e[11] & 0x10) && entry_name(e, files[count].name)) files[count++].bytes = rd32(e + 28);
  return d.err ? -4 : (s32)count;
}
