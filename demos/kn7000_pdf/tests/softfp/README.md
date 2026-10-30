# GCC's soft-fp against the PC's arithmetic

PDF.BIN does its floating point with GCC's soft-fp (`src/third/softfp/`, GCC 14.2's libgcc/soft-fp with an
`sfp-machine.h` of ours) instead of the MN10300 libgcc's fp-bit.c, which took 45% of the CPU while a page was drawn
(`tests/mame/probes/profile.lua`). The question here: do those routines, with our `sfp-machine.h`, give the results
IEEE 754 arithmetic gives?

    make softfp-test

builds the same sources for the PC (32-bit, each function renamed `t_...`), and `test_softfp.c` runs every operation
PDF.BIN calls (float and double add, subtract, multiply, divide, negate, the comparisons, the conversions to and from
32-bit integers and between float and double) on 3,000,000 random pairs (random bit patterns, NaNs, infinities and
subnormals among them, and random ordinary values), against the PC's SSE arithmetic, which rounds as IEEE 754 says.
A NaN matches any NaN. It prints the mismatches and exits 1 when there is one.

Result (2026-10-30): `3000000 rounds of every operation, 0 mismatches`. On the KN7000 the same sources take
longlong.h's MN10300 `umul_ppmm` (the CPU's `mulu`); the rest is the same C.
