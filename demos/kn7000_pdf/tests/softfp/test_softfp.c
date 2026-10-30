/* test_softfp.c -- GCC's soft-fp with sfp-machine.h (the MN10300's) against the PC's hardware floats (SSE, so float and
 * double round as IEEE 754 says): every operation the KN7000's code calls, on random bit patterns (NaNs, infinities,
 * subnormals among them) and random ordinary values. Prints the mismatches; exit 1 if any. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef float SF; typedef double DF;
SF t_addsf3(SF, SF); SF t_subsf3(SF, SF); SF t_mulsf3(SF, SF); SF t_divsf3(SF, SF); SF t_negsf2(SF);
DF t_adddf3(DF, DF); DF t_subdf3(DF, DF); DF t_muldf3(DF, DF); DF t_divdf3(DF, DF); DF t_negdf2(DF);
int t_eqsf2(SF, SF); int t_nesf2(SF, SF); int t_gesf2(SF, SF); int t_gtsf2(SF, SF); int t_lesf2(SF, SF); int t_ltsf2(SF, SF);
int t_unordsf2(SF, SF);
int t_eqdf2(DF, DF); int t_gedf2(DF, DF); int t_ledf2(DF, DF); int t_unorddf2(DF, DF);
int32_t t_fixsfsi(SF); uint32_t t_fixunssfsi(SF); SF t_floatsisf(int32_t); SF t_floatunsisf(uint32_t);
int32_t t_fixdfsi(DF); uint32_t t_fixunsdfsi(DF); DF t_floatsidf(int32_t); DF t_floatunsidf(uint32_t);
DF t_extendsfdf2(SF); SF t_truncdfsf2(DF);

static uint64_t s = 88172645463325252ull;
static uint64_t rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
static SF rf(void) {
  uint32_t u = (uint32_t)rnd();
  if (rnd() % 4 == 0) { float f = (float)((int64_t)(rnd() % 2000001) - 1000000) / (float)(1 + rnd() % 1000); return f; }
  SF f; memcpy(&f, &u, 4); return f;
}
static DF rd(void) {
  uint64_t u = rnd();
  if (rnd() % 4 == 0) return (double)((int64_t)(rnd() % 2000001) - 1000000) / (double)(1 + rnd() % 1000);
  DF d; memcpy(&d, &u, 8); return d;
}
static int same_f(SF a, SF b) { uint32_t x, y; memcpy(&x, &a, 4); memcpy(&y, &b, 4); return x == y || (isnan(a) && isnan(b)); }
static int same_d(DF a, DF b) { uint64_t x, y; memcpy(&x, &a, 8); memcpy(&y, &b, 8); return x == y || (isnan(a) && isnan(b)); }
static long bad;
#define CF(name, got, want) do { SF g_ = (got), w_ = (want); if (!same_f(g_, w_) && bad++ < 20) printf("%s(%a, %a): %a, want %a\n", name, a, b, g_, w_); } while (0)
#define CD(name, got, want) do { DF g_ = (got), w_ = (want); if (!same_d(g_, w_) && bad++ < 20) printf("%s(%a, %a): %a, want %a\n", name, x, y, g_, w_); } while (0)
#define CI(name, got, want) do { long g_ = (got), w_ = (want); if (g_ != w_ && bad++ < 20) printf("%s: %ld, want %ld\n", name, g_, w_); } while (0)
int main(void) {
  long n = 0;
  for (int i = 0; i < 3000000; ++i, ++n) {
    SF a = rf(), b = rf();
    CF("add", t_addsf3(a, b), a + b); CF("sub", t_subsf3(a, b), a - b); CF("mul", t_mulsf3(a, b), a * b);
    CF("div", t_divsf3(a, b), a / b); CF("neg", t_negsf2(a), -a);
    if (!isnan(a) && !isnan(b)) {
      CI("eq", t_eqsf2(a, b) == 0, a == b); CI("ne", t_nesf2(a, b) != 0, a != b); CI("ge", t_gesf2(a, b) >= 0, a >= b);
      CI("gt", t_gtsf2(a, b) > 0, a > b); CI("le", t_lesf2(a, b) <= 0, a <= b); CI("lt", t_ltsf2(a, b) < 0, a < b);
    }
    CI("unord", t_unordsf2(a, b) != 0, isnan(a) || isnan(b));
    if (fabsf(a) < 2e9f) CI("fixsfsi", t_fixsfsi(a), (int32_t)a);
    if (a >= 0 && a < 4e9f) CI("fixunssfsi", (long)t_fixunssfsi(a), (long)(uint32_t)a);
    int32_t k = (int32_t)rnd(); uint32_t u = (uint32_t)rnd();
    { SF g = t_floatsisf(k), w = (SF)k; if (!same_f(g, w) && bad++ < 20) printf("floatsisf %d\n", k); }
    { SF g = t_floatunsisf(u), w = (SF)u; if (!same_f(g, w) && bad++ < 20) printf("floatunsisf %u\n", u); }
    { DF g = t_extendsfdf2(a), w = (DF)a; if (!same_d(g, w) && bad++ < 20) printf("extend %a\n", a); }
    DF x = rd(), y = rd();
    CD("dadd", t_adddf3(x, y), x + y); CD("dsub", t_subdf3(x, y), x - y); CD("dmul", t_muldf3(x, y), x * y);
    CD("ddiv", t_divdf3(x, y), x / y); CD("dneg", t_negdf2(x), -x);
    if (!isnan(x) && !isnan(y)) { CI("deq", t_eqdf2(x, y) == 0, x == y); CI("dge", t_gedf2(x, y) >= 0, x >= y); CI("dle", t_ledf2(x, y) <= 0, x <= y); }
    if (fabs(x) < 2e9) CI("fixdfsi", t_fixdfsi(x), (int32_t)x);
    if (x >= 0 && x < 4e9) CI("fixunsdfsi", (long)t_fixunsdfsi(x), (long)(uint32_t)x);
    { DF g = t_floatsidf(k), w = (DF)k; if (!same_d(g, w) && bad++ < 20) printf("floatsidf %d\n", k); }
    { DF g = t_floatunsidf(u), w = (DF)u; if (!same_d(g, w) && bad++ < 20) printf("floatunsidf %u\n", u); }
    { SF g = t_truncdfsf2(x), w = (SF)x; if (!same_f(g, w) && bad++ < 20) printf("trunc %a\n", x); }
  }
  printf("%ld rounds of every operation, %ld mismatches\n", n, bad);
  return bad != 0;
}
