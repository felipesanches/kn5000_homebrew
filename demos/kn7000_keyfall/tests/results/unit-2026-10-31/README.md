# Keyfall on the owner's KN7000 (brought back 2026-10-31)

`KFLOG.TXT` and `SCORES.TXT` as Keyfall wrote them in `KEYFALL\` on the owner's SD card, copied off the card by
KN7000's `tools/sdcard/update_sdcard.sh` (KN7000 `sdcard-stage/from-card/20261031-064951/KEYFALL/`, the first copy
that holds them; the card copied at 2026-10-30 17:50 had none). Every later copy up to 2026-11-04 holds the same
log (SHA-1 `f7ce74b1ac32f12fa832bee6cae1263bf5cc48e5`): no other session was logged since.

What the log shows:
- build `3d0fe47` started from the program menu (folder `KEYFALL\`) on **PROGRAM 893**, the firmware search's six
  steps found;
- `KEYFALL.BIN` (22,800 bytes) read by raw reads and its CRC-32 `189E9CE5` matching its header;
- 3 MIDI files in the folder; MINUETG.MID played three times (right hand, tempo 100 %, accompaniment on), scores
  350, 1360 and 4180; then ODEJOY.MID once, score 8270, its 602 accompaniment messages sent with the ring never full;
- the best scores saved to `SCORES.TXT` after each game.
