# (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
# Licensed under the terms of the GNU General Public License v3 or later
#
# kn7fm.inc -- KN7FM's memory map and constants (kn7fm.s; README.md). KN7FM is an application of KN7LOAD.CNV
# (technics-roms-disasm kn7000/tools/cnv_romdump/kn7cnv.inc, included first), started from the program menu
# (folder KN7FM\). Everything it writes in the main CPU's memory lies in the application area
# 0x502BA000-0x502E7EFF, which the loader (or the menu) zeroes before it starts the program, below the menu's
# launch block (LB_ADDR).

# ---- the application area ------------------------------------------------------------------------
.equ IMAGE_MAX,     0x502D8000      # the image (header, code, the DSP program, tables, font) ends below
.equ FW_VARS,       0x502D8000      # fwfind.s: what it found (FW_*), then its scratch from +0x40 (FS_*)
.equ FWV_VARS,      0x502D8100      # the firmware RAM addresses found (FWV_* below)
.equ DSP_HOST,      0x502D8400      # dsp.s: the firmware's DSP routines, state; DV_RBBUF at +0x200
.equ M_VARS,        0x502D8800      # 1 KB: the program's variables (V_* below)
.equ NOTES,         0x502D8C00      # two MIDI byte parsers (NS_*): the keybed (MIDI OUT 1), MIDI IN 1
.equ VOICEV,        0x502D8D00      # the voice allocator: NVOICES records of VR_LEN bytes
.equ STAGEBUF,      0x502D8E00      # 18 x u32: a key-on's words (PITCH, OLRS, STATE), as the DSP's STAGE
.equ PIMG,          0x502D8E80      # the patch's DSP words (DSP_PATCH_WORDS), computed
.equ PSENT,         0x502D8F80      # the same, as last uploaded
.equ TMPSTR,        0x502D9080      # 64 bytes: text being built
.equ PATHBUF,       0x502D90C0      # 48 bytes: the folder and a file name
.equ PREFIX,        0x502D90F0      # 16 bytes: the folder ("KN7FM\"), from the menu's launch block
.equ BANK,          0x502D9100      # 32 x 155 bytes: the bank, unpacked voices (VCED), to 0x502DA460
.equ VCED_LEN,      155
.equ FILEBUF,       0x502DA500      # 4,104 bytes: a 32-voice SysEx dump read or to be written, to 0x502DB508
.equ SYX_LEN,       4104
.equ AREA_END,      0x502DB600      # the end of what KN7FM uses (below the launch block, LB_ADDR)

# ---- what fwfind.s finds (FwStep), as in Another World's port --------------------------------------
.equ FW_STEPS,      8               # 0-5 must find what they look for; 6-7 (the effects DSP) may not
.equ FW_OPEN,       FW_VARS + 0x00  # open(a0 = name, a1 = mode) -> d0 (16-bit, < 0 = error)
.equ FW_READ,       FW_VARS + 0x04  # read(a0 = buffer, d0 = bytes) -> d0 = bytes read
.equ FW_CLOSE,      FW_VARS + 0x08
.equ FW_CRE,        FW_VARS + 0x0C  # ER cre_tsk(d0 = id, a0 = &T_CTSK)
.equ FW_STA,        FW_VARS + 0x10  # ER sta_tsk(d0 = id, d1 = start code)
.equ FW_DLY,        FW_VARS + 0x14  # the firmware's yield: d0 = ms
.equ FW_EXD,        FW_VARS + 0x18  # exd_tsk(): exit and delete the calling task
.equ FW_SETEXT,     FW_VARS + 0x1C  # the file layer to the SD card (or a no-op)
.equ FW_RESETEXT,   FW_VARS + 0x20  # back to the floppy (or a no-op)
.equ FW_WRITE,      FW_VARS + 0x24  # write(a0 = buffer, d0 = bytes) -> d0 = bytes written (0 = none)
.equ FWV_KERNEL_MS, FWV_VARS + 0x00 # the kernel's 1 kHz millisecond counter's address
.equ FWV_PANEL,     FWV_VARS + 0x04 # the panel switch bitmaps (CPR; CPL and CPC at +0x30)
.equ FWV_MIDIIN1,   FWV_VARS + 0x08 # MIDI IN 1 receive ring header
.equ FWV_MIDIOUT1,  FWV_VARS + 0x0C # MIDI OUT 1 transmit ring header (the keybed's notes)
.equ FWV_BUILD,     FWV_VARS + 0x10 # the program build (893, 941, ...), 0 = unknown
.equ PANEL_CPL_OFS, 0x30            # CPL - CPR; the CPC board's segments too (CPC_SEGn at +0x30 + n)
.equ QH_START,      0x00            # a firmware byte ring's header: first byte,
.equ QH_LAST,       0x04            #   last byte,
.equ QH_RD,         0x08            #   the consumer's read pointer (the firmware's; never ours),
.equ QH_WR,         0x0C            #   the producer's write pointer,
.equ QH_FREE,       0x10            #   u16: bytes the producer may still write

# ---- the task ---------------------------------------------------------------------------------------
# Every firmware task runs at priority 3 and micro-ITRON does not time-slice, so ours runs at 3 too and yields
# every millisecond (FW_DLY). Slots 10, then 9, are free (Another World's port).
.equ TASK_ID_FIRST, 10
.equ TASK_ID_LAST,  9
.equ TASK_PRI,      3
.equ TASK_STACK,    0x1000
.equ SETTLE_MS,     1500            # the task lets the firmware reach its converter screen first

# ---- variables, at M_VARS ----------------------------------------------------------------------------
.equ V_STATE,       M_VARS + 0x00   # ST_* below
.equ V_CALLS,       M_VARS + 0x04   # converter calls so far
.equ V_STEP,        M_VARS + 0x08   # the firmware search's next step
.equ V_FAIL,        M_VARS + 0x0C   # why KN7FM cannot run: FAIL_* below
.equ V_QUIT,        M_VARS + 0x10   # 1: EXIT was held, the task is to end
.equ V_TASKDONE,    M_VARS + 0x14   # 1: no task runs (ended, or never started)
.equ V_TASKID,      M_VARS + 0x18
.equ V_EXIT_T0,     M_VARS + 0x1C   # when EXIT went down (ms | 1), 0 = up
.equ V_ENDED,       M_VARS + 0x20   # we answered 0xFE
.equ V_FREQ,        M_VARS + 0x24   # a file request from the task: 1 read BANKn.SYX, 2 write it (0 = none)
.equ V_FRES,        M_VARS + 0x28   # its result: 0 none yet, 1 done, 2 failed (the task clears it)
.equ V_FBYTES,      M_VARS + 0x2C   # bytes read or written
.equ V_FILENO,      M_VARS + 0x30   # n of BANKn.SYX (0-9)
.equ V_BANKSRC,     M_VARS + 0x34   # where the bank in memory came from: 0 built in, 1 + n BANKn.SYX
.equ V_PATCH,       M_VARS + 0x38   # the current voice of the bank, 0-31
.equ V_ALG,         M_VARS + 0x3C   # the algorithm whose kernel runs (0-31), 0xFF = none yet
.equ V_ALGBUF,      M_VARS + 0x40   # the PM buffer it runs from (0 / 1)
.equ V_NV,          M_VARS + 0x44   # voices played (NVOICES)
.equ V_MSNOW,       M_VARS + 0x48   # the task's clock this pass
.equ V_BTN,         M_VARS + 0x4C   # buttons down at the last poll (B_* bits)
.equ V_PRESSED,     M_VARS + 0x50   # buttons that went down at this poll
.equ V_MUTEDN,      M_VARS + 0x54   # MUTE UP / DOWN down at the last poll: bit 2c up, 2c + 1 down, column c
.equ V_MUTENEW,     M_VARS + 0x58   # those that went down (or auto-repeat) at this poll
.equ V_REPT0,       M_VARS + 0x5C   # when the held MUTE button went down, or last repeated
.equ V_REPN,        M_VARS + 0x60   # repeats so far
.equ V_PAGE,        M_VARS + 0x64   # the page shown: PG_* below
.equ V_SUB,         M_VARS + 0x68   # its half (0 / 1): pressing the page's key again flips it
.equ V_OPSEL,       M_VARS + 0x6C   # the operator whose envelope is drawn (0 = OP1 .. 5 = OP6)
.equ V_DIRTY,       M_VARS + 0x70   # what to draw: D_* bits
.equ V_COLDIRTY,    M_VARS + 0x74   # bit c: column c's value changed
.equ V_PDIRTY,      M_VARS + 0x78   # 1: the patch changed: its DSP words are to be sent
.equ V_FG,          M_VARS + 0x7C   # drawing colours (RGB565) and text scale
.equ V_BG,          M_VARS + 0x80
.equ V_SCALE,       M_VARS + 0x84
.equ V_LCDSUM,      M_VARS + 0x88   # the title bar's checksum after our last full drawing
.equ V_POLLMS,      M_VARS + 0x8C   # when the buttons were last polled
.equ V_NOTES,       M_VARS + 0x90   # key-ons sent
.equ V_STEALS,      M_VARS + 0x94   # voices taken from a sounding note
.equ V_BUSY,        M_VARS + 0x98   # waits for MSGR6 = 0 that timed out
.equ V_SUSTAIN,     M_VARS + 0x9C   # 1 while the sustain pedal (CC 64 >= 64) is down
.equ V_AGE,         M_VARS + 0xA0   # the allocator's clock: + 1 each key-on and key-off
.equ V_MSG,         M_VARS + 0xA4   # a message for the title bar (a string), 0 = none
.equ V_MSGT0,       M_VARS + 0xA8   # when it was posted
.equ V_SAVEARM,     M_VARS + 0xAC   # SAVE asked once: a second press within ARM_MS writes
.equ V_KEYWANT,     M_VARS + 0xB0   # 1: the keybed's own sound muted (KeysSync)
.equ V_KEYSENT,     M_VARS + 0xB4   # what the instrument was last told
.equ V_DROPS,       M_VARS + 0xB8   # MIDI bytes not put in the MIDI IN 1 ring (full)
.equ V_HB0,         M_VARS + 0xBC   # MSGR7 (the DSP's heartbeat) and when, for the frame rate
.equ V_HBMS,        M_VARS + 0xC0
.equ V_RATE,        M_VARS + 0xC4   # DSP frames per second, last measured
.equ V_REDRAWS,     M_VARS + 0xC8   # full redraws because the LCD no longer showed ours
.equ V_LOOP_T0,     M_VARS + 0xCC
.equ V_TMP,         M_VARS + 0xD0
.equ V_TMP2,        M_VARS + 0xD4
.equ V_PEGSEL,      M_VARS + 0xD8   # 1: the graph shows the pitch envelope
.equ V_DECBUF,      M_VARS + 0x100  # 16 bytes: PutDec's digits

.equ ST_FIRST,      0               # V_STATE
.equ ST_SEARCH,     1               # the firmware search, a step a call
.equ ST_BANK,       2               # BANK0.SYX from the folder, or the built-in bank
.equ ST_DSP,        3               # the DSP: checks, upload, read back, hook
.equ ST_RUN,        4               # the task runs; file requests served
.equ ST_FAIL,       5               # cannot run: the reason is shown, EXIT ends it

.equ FAIL_FW,       0x100           # + the search step that failed
.equ FAIL_DSP,      0x200           # + DV_FAIL
.equ FAIL_TASK,     0x300

# ---- the screen (640 x 240) ----------------------------------------------------------------------------
.equ TITLE_H,       28
.equ ROW_H,         42              # the side buttons' rows, from TITLE_H, as the firmware lays them out
.equ SIDE_W,        96              # the side buttons' labels
.equ STRIP_Y,       198             # the parameter strip: 16 columns of COL_W, above the MUTE buttons
.equ COL_W,         40
.equ DIAG_X,        108             # the algorithm's diagram
.equ DIAG_Y,        48
.equ GRAPH_X,       316             # the envelope's graph
.equ GRAPH_Y,       40
.equ GRAPH_W,       216
.equ GRAPH_H,       140
.equ C_BG,          0x0000
.equ C_TITLE,       0x0010
.equ C_TEXT,        0xFFFF
.equ C_DIM,         0x8410
.equ C_LABEL,       0x3186
.equ C_SEL,         0x03EF          # the page shown, the operator selected
.equ C_CAR,         0x0253          # a carrier's box
.equ C_MOD,         0x4208          # a modulator's box
.equ C_LINE,        0xC618
.equ C_GRID,        0x2104
.equ C_EG,          0xFFE0
.equ C_BAD,         0xF800
.equ C_OK,          0x07E0
.equ C_VAL,         0xFFFF
.equ C_NAME,        0xAD55

.equ D_ALL,         1               # V_DIRTY: everything
.equ D_TITLE,       2               # the title bar
.equ D_DIAG,        4               # the diagram
.equ D_GRAPH,       8               # the envelope
.equ D_STRIP,       0x10            # the strip's names and values
.equ D_SIDE,        0x20            # the side labels

# ---- the buttons ---------------------------------------------------------------------------------------
# B_* bit numbers (ButtonTab): the segment's byte offset from CPR and the switch's bit (DSPTONE, kn7menu).
.equ B_L1,          0
.equ B_L2,          1
.equ B_L3,          2
.equ B_L4,          3
.equ B_R1,          4
.equ B_R2,          5
.equ B_R3,          6
.equ B_R4,          7
.equ B_PGUP,        8
.equ B_PGDN,        9
.equ B_EXIT,        10
.equ NBUTTONS,      11
.equ EXIT_OFS,      0x30 + 11       # EXIT = CPC_SEG11 SW7
.equ EXIT_BIT,      0x80
.equ QUIT_MS,       1500            # hold EXIT this long to leave
.equ POLL_MS,       8               # the panel is read this often
.equ REP_DELAY,     400             # a MUTE button held: repeats after this,
.equ REP_MS,        70              #   every this, faster after REP_FAST repeats
.equ REP_FAST,      10
.equ REP_MS2,       25
.equ ARM_MS,        3000            # SAVE: the second press must come within this
.equ MSG_MS,        2500            # a title-bar message stays this long

.equ PG_OP,         0               # V_PAGE: an operator (V_OPSEL), half A envelope and frequency, B scaling
.equ PG_VOICE,      1               #   the voice: A algorithm and LFO, B the pitch envelope
.equ PG_BANK,       2               #   the bank's file, LOAD, SAVE, the voice's name

# ---- the synthesizer -------------------------------------------------------------------------------------
.equ NVOICES,       8
.equ VR_NOTE,       0               # the voice allocator's records (VOICEV): u8 the key (0xFF = none)
.equ VR_STATE,      1               #   u8: VS_*
.equ VR_AGE,        4               #   u32: V_AGE at its last key-on or key-off
.equ VR_LEN,        8
.equ VS_FREE,       0               # never played, or released long ago (the oldest goes first)
.equ VS_HELD,       1               # key down
.equ VS_SUST,       2               # key up, held by the sustain pedal
.equ VS_REL,        3               # key up: releasing
.equ CMD_POLLS,     40              # MSGR6 reads waiting for the DSP to take the last command
.equ NS_HDR,        0x00            # a MIDI parser (NOTES): u32 the ring's header
.equ NS_RP,         0x04            #   u32: its own read pointer (0 = resync at the next poll)
.equ NS_STATUS,     0x08            #   u8: running status (0 = none)
.equ NS_D1,         0x09            #   u8: the first data byte of the message being collected
.equ NS_CNT,        0x0A            #   u8: data bytes collected (0 / 1)
.equ NS_LEN,        0x10
.equ NOTES_KBD,     NOTES
.equ NOTES_MIDI,    NOTES + NS_LEN
.equ KEY_CC,        11              # the keybed's own sound: expression 0 on channels 1-3 (Another World's)
.equ KEY_CHS,       3
