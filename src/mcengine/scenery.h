// Scenery (class 2, 0x43ba0..0x43e80) and switches (class 11, 0x4a2a0..0x4a940).
// Owner: scenery.cpp (round 3). Report: docs/analysis/port_castle.md.
//
// Scenery states: 0 tree, 1 burning tree, 2 burnt tree, 3 standing stone, 6 dolmen, 9 bad stone;
// 4, 5, 7, 8, 10..17 are empty handlers (single `ret`).
// Switch states = the level-file switch model (see the handler list in scenery.cpp). A switch keeps
// its SwiId in Thing.owner (+0x18), its trigger box in the extents and a 10 / 16 tick timer in aux.
#pragma once
#include "thing.h"
#include "spatial.h"

// Binds this subsystem's handlers to the class tables (by original address).
void scenery_register_handlers();

// ---- class 2 Table A handlers ----------------------------------------------------------------------
void scenery_tree_update(Thing *t);              // scenery_tree_update_43ba0 (state 0)
void scenery_tree_s1_update(Thing *t);           // scenery_tree_s1_update_43cd0 (state 1: burning)
void scenery_tree_s2_update(Thing *t);           // scenery_tree_s2_update_43d60 (state 2: burnt)
void scenery_standing_stone_update(Thing *t);    // scenery_standing_stone_update_43db0 (state 3); scenery_badstone_update_43e60 (state 9) is identical
void scenery_dolmen_update(Thing *t);            // scenery_dolmen_update_43de0 (state 6)
void scenery_update_none(Thing *t);              // scenery_update_shared_43dd0 / _43e50 / _43e80, switch_type31_s31_update_4a8a0: `ret`

// ---- class 11: switches ----------------------------------------------------------------------------
// switch_test_player_4a8b0(sw, want): every 8th update of the switch: 1 (and sound 0x29 at that
// wizard) when a human wizard (class 3 type 0) of the player list has `thing_collide(sw, wizard) ==
// want`; otherwise the switch is snapped to the ground and 0 is returned.
int  switch_test_player(Thing *sw, int want);
// switch_test_any_player_4a940(sw, want): 1 when any player's Thing has thing_collide(sw, thing) == want.
int  switch_test_any_player(Thing *sw, int want);
// switch_creature_dead_trigger_4a660(sw, creature_type): once the creature list of that type is
// empty (-1: the lists 0..0xb and 0x10), aux counts 0x10 updates and the switch fires
// (switch_activate(SwiId, clear)) and is deleted.
void switch_creature_dead_trigger(Thing *sw, int creature_type);
