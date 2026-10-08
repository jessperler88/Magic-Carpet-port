// Creature types emu (10), genie (11), type 15 (the wizard castles' guard archer) and wyvern (16):
// the class-5 Table A handlers 0x1c8f0..0x1d420 and 0x1ea60..0x1f6b0, written against creatures.h.
// Owner: creatures3.cpp. Report: docs/analysis/port_creatures3.md.
//
// States (base = type * 6, see creatures.h): emu 60..65, genie 66..71, type 15 90..95, wyvern 96..101;
// the records 102..119 point at three bare `ret`s (0x1f690 / 0x1f6a0 / 0x1f6b0) and are disabled in
// the table. The +0 states 60 (emu) and 90 (type 15) are already in creatures.cpp; 96 (wyvern +0,
// 0x1f200) is bound here.
//
// Field use beyond creatures.h:
//   genie   flags bit 0 toggled by state 66 (set while hidden in 67, clear while fighting in 68): it
//           vanishes in a puff of 12 class-10 type-1
//           effects, teleports (random spot while hidden, in front of the target when showing up) and
//           reappears); aux = state-66 step counter (0 = start, 1 = second tick) / shots fired in 68
//   type 15 moves on the cell grid (type15_move: one of four headings every 8 ticks, weighted by
//           g_type15_dir_weights, snaps to the cell centre every 16 ticks), sprites 0 / 0xce / 1 like
//           the archer, attacks only things of another owner
//   wyvern  aux = fireballs left in the current burst (15 when it faces the target), one per tick
#pragma once
#include "creatures.h"

// Binds the class-5 Table A handlers of the four types (by original address).
void creatures3_register_handlers();

// ---- helpers other code / the unit test may call -----------------------------------------------------
// genie_vanish_1d220(thing): target = 0, aux = 0, state 66, sound 0xb (the genie hides and teleports away).
void genie_vanish(Thing *t);
// genie_appear_at_target_1d270(thing): no-op without a target; else aux = 0, state 66 and the genie is
// moved to speed_cur * 64 in front of the target (along the target's yaw).
void genie_appear_at_target(Thing *t);
// genie_steal_mana_1d310(thing): while mana < mana_total take the nearest type-0x27 mana ball in sight
// (mana added, ball deleted, class-10 type-0 effect with the genie's owner left behind), sound 0xb.
void genie_steal_mana(Thing *t);
// type15_set_attack_sprite_1ef10(thing): one rng draw, speed 0, sprite 1 (rng % 20 > 10) or 0xce.
void type15_set_attack_sprite(Thing *t);
// type15_set_move_sprite_1ef50(thing): speed_base, sprite 0.
void type15_set_move_sprite(Thing *t);
// type15_move_1ef80(thing): the grid walk described above. A forbidden cell under the creature sends
// it straight to state 94 (dying).
void type15_move(Thing *t);
