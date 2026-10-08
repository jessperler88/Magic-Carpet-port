// Class-10 effect handlers of carpet.exe (0x2376f..0x284c0) that are not terrain shaping: explosions,
// fire, splash, smoke, earthquake, meteor, the volcano's eruption, lightning, rain of fire, teleport,
// mana balls, castle building / raising and the spell area effects.
// Owner: effects.cpp (round 3). Report: docs/analysis/port_effects.md.
//
// The terrain-shaping handlers of the same address range (volcano, crater, walls, paths, canyon,
// ridge, wizard castles, 26320 / 26680) live in level_features.cpp.
#pragma once
#include "thing.h"
#include "spatial.h"

// Binds this subsystem's Table A handlers to the class tables (by original address) and installs
// g_hook_thing_drop_mana_ball (spatial.h).
void effects_register_handlers();

// ---- helpers of the address range that other subsystems call -------------------------------------
// terrain_max_drop_around_2376f (entry 0x23780): the most negative (neighbour - centre) height-map
// difference over the 8 neighbours of the cell (coordinate - 0x80) >> 8; 0 when no neighbour is lower.
int  terrain_max_drop_around(const Pos *pos);
// effect_age_tick_25600: aux++, health--, delete when the health was already below 0 (no caller in
// the retail image).
void effect_age_tick(Thing *t);
// terrain_ring_find_height_ne8_24d70 (dead code in retail): walk the square rings of radius
// `start_radius`..0x1d around the packed cell and return the first cell whose height byte is not 8,
// else the start cell. The original reads the start radius from an uninitialised stack byte; the
// port takes it as a parameter.
unsigned terrain_ring_find_height_ne8(unsigned cell, unsigned start_radius);

// ---- mana balls ----------------------------------------------------------------------------------
// thing_drop_mana_ball_25fe0: when t carries mana (+0x8c > 0) spawn a mana ball (effect 0x27) with
// all of it and t's mana owner, thrown in a random direction; t's mana owner is cleared (its mana
// field is left alone: the callers zero it or die).
void thing_drop_mana_ball(Thing *t);
// mana_ball_merge_26120(a, b): a takes b's mana, the surviving owner is chosen (a player beats an
// effect, the richer player beats the poorer) and b is freed at once (thing_free, not mark_delete).
void mana_ball_merge(Thing *a, Thing *b);
// mana_ball_update_sprite_25e20 lives in constructors.cpp and is declared in constructors.h.
