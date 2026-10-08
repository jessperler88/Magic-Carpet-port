// Creature types crab (5), kraken (6), troll (7), griffon (8): the class-5 Table A handlers
// 0x1a830..0x1bb00 written against creatures.h (shared bodies, attack callbacks, state layout
// base = type * 6). Owner: creatures2.cpp. Report: docs/analysis/port_creatures2.md.
//
// States (Table A index = original address):
//   crab    30 1a820 (creatures.cpp) | 31 1a830 wander / hunt mana | 32 1ac20 attack (volley) |
//           33 1ac80 collect the mana ball | 34 1aed0 dying | 35 1aee0 dead
//   kraken  36 1afa0 idle | 37 1afb0 main | 38 1b000 attack (drag the wizard, fire) | 39 1b390 follow |
//           40 1b3e0 dying | 41 1b3f0 dead
//   troll   42 1b400 idle | 43 1b410 main (+ regeneration) | 44 1b470 attack (fire, pose) |
//           45 1b510 follow | 46 1b530 dying | 47 1b540 dead
//   griffon 48 1b550 idle | 49 1b560 wander, hunt wanted wizards | 50 1b940 attack (fire) |
//           51 1ba60 follow | 52 1ba70 dying (blames the killer) | 53 1baf0 dead
//
// Thing fields as these types use them (in addition to creatures.h):
//   crab:    aux = think divisor of state 33 (15 far away, 3 within 20 * speed_base), target = the
//            mana ball (class 10 type 0x27) it walks to; mana / mana_total: eats mana balls, lays a
//            crab egg (class 10 type 0x34) when mana > mana_total + 500; health regenerates by
//            max_health >> 7 per tick in states 31 / 32
//   kraken:  aux = grip counter of state 38 (-10 when hit, counts up, > 40 -> -90: every 40 ticks of
//            dragging the wizard are followed by 90 ticks of rest); castle_size (+0x47) = shots left
//            of the current burst (5 per think tick in range)
//   troll:   aux = ticks left in the firing pose (30, sprite 0xc6, speed turn_rate; 1 when the state
//            is entered); health := max_health on every think tick of state 43 (see the report)
//   griffon: like the archer it only hunts "wanted" wizards (P+0x210 != 0) and re-arms the flag
#pragma once
#include "creatures.h"

// Binds the Table A handlers of states 31..53 by original address.
void creatures2_register_handlers();

// crab_target_nearest_mana_ball_1aef0(thing): scans the whole pool (slots 1..999) for class 10 type
// 0x27 mana balls and writes the index of the nearest (pos_dist_xyz < 0x10000) to thing.target, 0
// when there is none. No caller in the retail image (the crab states search the mana ball list
// themselves); exported for the test.
void crab_target_nearest_mana_ball(Thing *t);

// The kraken's shot (inlined in creature_kraken_s38_update_1b000): a class-9 type-9 projectile at the
// kraken's position raised by ext_h, impact class 10 type 0x17, descriptor 0x96ad0, damage 800,
// aimed at `target`, collision filter copied from the target. Returns the projectile or null.
Thing *kraken_fire(Thing *t, Thing *target);
