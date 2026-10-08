// The whole simulation: registration of the gameplay subsystems that are not part of the sim core
// and the hooks between them (see sim.h).
#include "sim.h"
#include "player.h"
#include "demo.h"
#include "effects.h"
#include "projectiles.h"
#include "spells.h"
#include "castle.h"
#include "scenery.h"
#include "creatures.h"
#include "creatures2.h"
#include "creatures3.h"
#include "ai_wizard.h"
#include "sound.h"
#include "input.h"
#include "text.h"
#include "mc_globals.h"
#include "debug_cmd.h"
#include "mode_level.h"
#include "net.h"

void sim_register_gameplay() {
    effects_register_handlers();        // also g_hook_thing_drop_mana_ball
    projectiles_register_handlers();
    spells_register_handlers();
    castle_register_handlers();
    scenery_register_handlers();
    creatures_register_handlers();      // also g_hook_creature_wake_tick
    creatures2_register_handlers();     // round 4: crab / kraken / troll / griffon
    creatures3_register_handlers();     // round 4: emu / genie / type 15 / wyvern
    ai_wizard_register_handlers();      // round 4: also g_hook_ai_record_threat
    sound_register_handlers();          // round 4: g_hook_sound_request / g_hook_sound_fade
    input_register_handlers();          // g_hook_player_local_input
    g_hook_creature_kill_all = creature_kill_all;
    g_hook_input_mouse_center = input_mouse_center;
    g_hook_demo_input_changed = input_changed;
    g_hook_demo_input_snapshot = input_snapshot;
    g_hook_sound_update = sound_update;
    g_hook_music_update = music_update;
    debug_cmd_register();               // round 10: g_hook_port_command / g_hook_debug_tick (debug packets 0x40..)
    mode_level_register();              // round 10 task A: mode level start, movie v3, mode checksum globals
    g_hook_net_ai_seed = [] { return g_ai_rand_seed; };   // net.h checksum part "ai_seed" (round 10 task E)
}

void sim_prepare_movie() {
    // Movie 0 was recorded in 320x200, and the original plays it in that mode in DOSBox
    // (tools/reference): castle capacities depend on it (castle_footprint doubles the building table
    // in 320x200, level_features.h). The "user input aborts the movie" check (input_changed_34090) is
    // off: the reference exe has it patched out, and the port leaves the movie through platform keys.
    g_video_mode_flags = 1;
    // game_main loads data/ftext.dat for movie playback (ENGINE.md "Demo / movie system"): the on-screen
    // notices of a movie are French in the original ("est mort(e)." in the reference at tick 1485).
    text_load(sim_game_dir(), 1);
    input_reset();
    input_mouse_center();
    g_hook_demo_input_changed = nullptr;
}
