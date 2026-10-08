// Fixed-point math of carpet.exe: 11-bit angles (0..0x7ff), 16.16 trig, integer sqrt, LCGs.
#pragma once
#include <cstdint>
#include "gen/core_tables.h"

// sin/cos in 16.16 for an angle in 0..0x7ff (callers mask with MC_ANGLE_MASK). The original table
// at 0x987ec runs to 0x9ff so cos(a) = sin(a + 0x200) never wraps for a <= 0x7ff.
inline int32_t mc_sin(unsigned a) { return g_trig_table_data[a & 0x7ff]; }
inline int32_t mc_cos(unsigned a) { return g_trig_table_data[(a & 0x7ff) + 0x200]; }
// Raw table access for code that was written against g_trig_table[a + 0x100] style indexing
// (g_trig_table in the Ghidra types starts 0x100 entries before the sine table).
inline int32_t mc_trig_raw(unsigned idx_from_987ec) { return g_trig_table_data[idx_from_987ec]; }

// Integer square root, math_isqrt_4cd7a: Newton iteration from a bsr-indexed seed table.
// Returns 0 for 0. Bit-exact with the original (unsigned division, loop until q >= r).
inline uint32_t mc_isqrt(uint32_t v) {
    if (v == 0) return 0;
    int hb = 31;
    while (!(v >> hb)) hb--;
    uint32_t r = g_isqrt_seed_data[hb];
    for (;;) {
        uint32_t q = v / r;
        if ((int32_t)q >= (int32_t)r) return r;
        r = (r + q) >> 1;
    }
}

// Slope -> angle helper used by the aim code: atan(i/256) in angle units, i in 0..257.
inline uint16_t mc_atan_raw(unsigned i) { return g_atan_table_data[i < 258 ? i : 257]; }

// The 16-bit LCG used by the terrain generator and the SIRDS pattern (DAT_0012dfb0).
extern uint16_t g_rng16;
inline uint16_t mc_rng16_next() { g_rng16 = (uint16_t)(g_rng16 * 0x24a1 + 0x24df); return g_rng16; }

// Generic LCG step on a 32-bit seed (Thing.rng, GameState.rng use the same constants).
inline uint32_t mc_lcg(uint32_t s) { return s * 0x24a1u + 0x24dfu; }

// Wrapped difference on a 256-cell torus (math_wrap_diff_34250 with modulus 0x100).
inline int mc_wrap_diff(int a, int b, int modulus = 256) {
    int d = (a - b) % modulus;
    if (d < 0) d += modulus;
    if (d > modulus / 2) d -= modulus;
    return d;
}
