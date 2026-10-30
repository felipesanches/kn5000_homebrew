/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * glue.S -- what C cannot say on the MN10300: calls into the firmware, the game task's entry, the
 * firmware search's byte scan, and the firmware's screen procedure without its wallpaper.
 *
 * GCC's calls pass the first two arguments in d0 and d1 and the rest in the caller's frame from (12, sp);
 * the caller reserves 12 bytes below them, the return address and two words the callee may use. The
 * firmware's routines (and the loader's) may store into 4..0x10(sp) of their caller's frame, 16 bytes: a C
 * caller's 12 are not enough, so every firmware call goes through fw_call, which makes a frame of 0x20. */

    .text

/* u32 fw_call(u32 fn, u32 d0, u32 d1, u32 a0, u32 a1): d0, d1, a0, a1 as given, `calls (fn)`, d0 back.
 * At entry: fn = d0, the d0 value = d1, the others at (12, sp), (16, sp), (20, sp). */
    .global _fw_call
_fw_call:
    movm    [d2,d3,a2,a3],(sp)      /* sp -= 16 */
    add     -0x20,sp                /* sp -= 32: the arguments are now 48 bytes further */
    mov     d0,a2
    mov     d1,d0
    mov     (60,sp),d1
    mov     (64,sp),a0
    mov     (68,sp),a1
    calls   (a2)
    ret     [d2,d3,a2,a3],48

/* The game task's entry, as cre_tsk starts it: the kernel's own stack (task.c TASK_KSTACK) is left for the
 * game task's own (task_stack_top, in KN7APP.BIN's bss), where interrupts that come during the task push
 * their frames too; the context switch keeps whichever sp the task has. Never returns. */
    .global _task_entry
_task_entry:
    mov     (_task_stack_top),a0
    mov     a0,sp
    add     -12,sp
    call    _game_task,[],0
1:  bra     1b

/* In ScreenProc's place while the game task runs (fwdraw.c), called as procedures are, `calls (a1)` with d0 the
 * view and d1 the event: the paint event 0x600B2 is answered 0 without DrawWall (and counted); every other goes on to
 * ScreenProc with the registers and the stack as they came (a1, which held this address, is no argument). */
    .global _screen_proc_hook
_screen_proc_hook:
    cmp     0x600b2,d1
    beq     1f
    mov     0x484177ce,a1           /* ScreenProc, program 893 */
    jmp     (a1)
1:  mov     (_screen_proc_skipped),d0
    inc     d0
    mov     d0,(_screen_proc_skipped)
    clr     d0
    rets

/* const u8* find_byte(const u8* from, const u8* to, u32 byte): the first address in [from, to) that holds
 * `byte`, or 0. The firmware search's inner scan (fwfind.c), which DSPTONE and Another World keep in five
 * instructions a byte on a 4-byte boundary: on MAME's calibrated MN103002A timing a scan of the whole flash
 * took 0.88 s that way and 4.85 s with the loop at an odd address (cnv_romdump loader.s, FindSig). */
    .global _find_byte
_find_byte:
    mov     d0,a0                   /* from */
    mov     d1,a1                   /* to */
    mov     (12,sp),d1              /* byte */
    .align  4,0xcb
1:  cmp     a1,a0
    bcc     2f
    movbu   (a0),d0
    inc     a0
    cmp     d1,d0
    bne     1b
    add     -1,a0
    mov     a0,d0
    rets
2:  clr     d0
    rets

/* u32 fw_call3(u32 fn, u32 d0, u32 d1, u32 third): d0 and d1 as given and `third` at (0x10, sp) of the frame the
 * routine is called from, where the firmware's DSP register write takes its third argument (the low half: Another
 * World's dsp.s DspWrite). At entry: fn = d0, the d0 value = d1, the others at (12, sp) and (16, sp). */
    .global _fw_call3
_fw_call3:
    movm    [d2,d3,a2,a3],(sp)      /* sp -= 16 */
    add     -0x20,sp                /* sp -= 32: the arguments are now 48 bytes further */
    mov     d0,a2
    mov     d1,d0
    mov     (64,sp),a0
    mov     a0,(0x10,sp)
    mov     (60,sp),d1
    calls   (a2)
    ret     [d2,d3,a2,a3],48

/* The crash screen's NMI entry (crash.c), where crash_arm points the stub at 0x50000006. A system error (NMICR,
 * 0x34000100, bit 2) goes to crash_main with the stack pointer as the CPU left it (the PSW at 0, the PC at 4); any
 * other NMI goes on to the firmware's handler (crash_fw), which never returns either. */
    .global _crash_nmi
_crash_nmi:
    movhu   (0x34000100),d0
    btst    0x04,d0
    bne     1f
    mov     (_crash_fw),a0
    jmp     (a0)
1:  mov     sp,a0
    mov     a0,d0
    add     -0x20,sp
    call    _crash_main,[],0
2:  bra     2b

#ifdef PF_CRASH_SELFTEST
/* The crash screen's self-test (make crash-selftest): a load the KN7000 faults on (probe PFT7's kind). */
    .global _crash_selftest_load
_crash_selftest_load:
    mov     0,a0
    movbu   (_crash_fw,a0),d0       /* fc 4x, displacement 0x502Bxxxx */
    rets
#endif

/* The watchdog's tick (crash.c wd_arm): the firmware's level-6 system-tick entry (the scheduler's, 1 kHz) begins
 * `movm [d2,d3,a2,a3,other],(sp); add -12,sp; mov psw,d3` (7 bytes, the same on programs 893-942), and wd_arm puts a jmp
 * here in their place. These do them, call wd_check with the address of the PSW and the PC the CPU pushed (above the
 * 48 bytes the movm pushed and the 12), and go back to the entry's eighth byte (the jmp's displacement written by
 * wd_arm). D0, D1, A0 and A1 are the movm's saved copies by then, as the firmware's entry leaves them. */
    .global _wd_tick
    .global _wd_back
_wd_tick:
    movm    [d2,d3,a2,a3,other],(sp)
    add     -12,sp
    mov     sp,a0
    add     60,a0
    mov     a0,d0
    add     -12,sp
    call    _wd_check,[],0
    add     12,sp
    mov     psw,d3
_wd_back:
    .byte   0xDC
    .long   0
