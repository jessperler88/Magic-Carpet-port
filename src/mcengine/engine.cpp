// Engine glue: initialisation of the renderer side, default camera, the per-tick driver. The
// simulation side (level loading, handler registration) is sim.cpp so it can be linked without the
// renderer.
#include "engine.h"
#include "sim.h"
#include "mc_globals.h"
#include "mc_math.h"
#include "tables.h"
#include "terrain.h"
#include "thing.h"
#include "player.h"
#include "sprites.h"
#include "demo.h"
#include "hud.h"
#include "ui_draw.h"
#include "world_set.h"
#include <cstdio>

const char *engine_game_dir() { return sim_game_dir(); }

// port: the renderer / HUD side of a data-set switch (world_set_select, Hidden Worlds): everything
// engine_init loaded from a set-specific file, including the shade / blend tables (HIDDEN.EXE loads
// data/dtables.dat, built from its palette, instead of data/tables.dat).
static void engine_world_set_changed(int /*set*/) {
    const char *dir = sim_game_dir();
    if (!tables_load_palette(dir))  std::fprintf(stderr, "engine: palette missing for the data set\n");
    if (!tables_load_textures(dir)) std::fprintf(stderr, "engine: texture atlas missing for the data set\n");
    if (!tables_load_sky(dir))      std::fprintf(stderr, "engine: sky missing for the data set\n");
    tables_load_or_generate(dir);               // after palette + atlas: a missing file is regenerated from them
    sprites_shutdown();
    if (!sprites_init(dir))         std::fprintf(stderr, "engine: tmaps missing for the data set\n");
    ui_draw_reload_set_tables();
}

bool engine_init(const char *game_dir) {
    if (!sim_init(game_dir)) return false;
    if (!tables_load_palette(game_dir))  { std::fprintf(stderr, "engine: data/palette.dat missing in %s\n", game_dir); return false; }
    if (!tables_load_textures(game_dir)) { std::fprintf(stderr, "engine: data/block%d.dat missing\n", g_state->texture_block_size); return false; }
    if (!tables_load_or_generate(game_dir)) { std::fprintf(stderr, "engine: tables.dat load/generate failed\n"); return false; }
    if (!tables_load_sky(game_dir))      { std::fprintf(stderr, "engine: data/sky.dat missing\n"); return false; }
    if (!sprites_init(game_dir))         { std::fprintf(stderr, "engine: sprite cache init failed\n"); return false; }
    sim_register_gameplay();
    render_things_install();
    ui_draw_init(game_dir);                     // fonts, HUD sprites, pointers (missing files: HUD stays blank)
    g_hook_frame_state = hud_tick_state;
    g_hook_world_set_changed = engine_world_set_changed;
    g_hook_demo_textures_reload = texture_load_needed;
    g_hook_demo_textures_mark = texture_mark_needed;      // texture_mark_needed_4c130 when a recording starts
    return true;
}

void engine_shutdown() { ui_draw_shutdown(); sprites_shutdown(); mc_globals_shutdown(); }

bool engine_load_level(int index) { return sim_load_level(index); }
bool engine_load_snapshot(const char *gam_rel, const char *map_rel) { return sim_load_snapshot(gam_rel, map_rel); }

Camera engine_default_camera() {
    Camera c{};
    // First player-class THING_INIT of the level, else the map centre.
    int cx = 128, cy = 128;
    for (const ThingInit &t : g_state->level.things) {
        if (t.cls == 3) { cx = t.x; cy = t.y; break; }
    }
    // The demo snapshot (movie/gam00000.dat) has the player at cell centre, z = 0x100, and the
    // position-history entries carry zoom 0x80, which is therefore the default focal scale.
    c.cam_x = cx * 256 + 128;
    c.cam_y = cy * 256 + 128;
    c.yaw = 0;
    int h = g_map_height[mc_cell(cx, cy)] * 0x20;
    c.cam_z = h + 0x180;
    c.pitch = 0;
    c.roll = 0;
    c.zoom = 0x80;
    return c;
}

// The per-tick part of game_tick_update_32e80 that is ported: texture / sprite animation, then the
// simulation (command packets from the movie when one is playing), then the water phase.
bool engine_tick() {
    bool more = true;
    if (!(g_cfg->paused & 1)) texture_anim_update();
    if (demo_playing()) more = demo_step();
    else game_tick_sim();
    g_anim_tick = g_state->players[g_state->local_player & 7].tick;
    return more;
}
