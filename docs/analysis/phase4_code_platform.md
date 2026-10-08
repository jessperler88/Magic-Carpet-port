# Phase 4 code platform: what the port offers a new game mode and its debug suite (2026-10-07)

Read-only survey of `src/mcengine`, `src/mcport`, `src/tests` and the round reports, written for the Phase 4
design (PvP RTS/RPG hybrid, offline with bots and online; first deliverable = an in-game tooling / debug
suite usable by the user and by Claude headless). Line numbers are those of the tree on this date.
Sections follow the eight questions of the brief; section 7 ends with the proposed debug suite mapped to
hook points with effort estimates.

---------------------------------------------------------------------------------------------------------

## 1. Networking (net.h / net.cpp / mcport/net_tcp.*)

### Model: deterministic lockstep, host-relayed input, no prediction

- **The simulation never talks to the network except through the command packets.** Per tick
  `player_commands_process` (player.cpp:983) does, when `Config.flags & 0x10` (network / "local chosen"):
  `net_exchange_frame(g_state->commands, 10)` (player.cpp:987) - the 8 x 10-byte `CmdPacket` array
  (`GameState.commands`, mc_types.h:418, layout mc_types.h:16-25: cmd, arg, pad2/arg2, steer_x, steer_y,
  bits, 4 unused bytes) - and then runs **the same loop over all players' packets on every instance**
  (player.cpp:1012-1202). Steering / key bits land in the owner's `PlayerBlock.steer_dx/dy/input_bits`
  (player.cpp:1190-1192); the Thing handlers consume them later in `thing_update_all`.
- `net_exchange_frame` (net.cpp:582-622) is the original's star exchange on top of a NetBIOS-style
  transport: the host receives every client's 10 bytes into `base + p*size` in player order, then sends
  the whole `count*size` block to every client; a client sends its slot and receives the block. The call
  **blocks** (busy-wait `while (cplt == NRC_PENDING) poll()`; `NetLobbyHooks::idle` pumps SDL meanwhile,
  main.cpp:514). There is no input delay, no prediction, no rollback: one network round trip per tick,
  so at 25 Hz the RTT budget is < 40 ms (fine on a LAN; too tight for the internet without a delay ring).
- **Tick synchronisation** is implicit: nobody advances past tick N until the exchange of tick N
  completed. mcport ticks at `tick_hz` (main.cpp:881) and the exchange waits for the slowest peer
  (port_net.md:198-199). `Config.net_time` records the exchange duration (player.cpp:986-988, shown by the
  original debug overlay "Transfer rate", hud.cpp:554-558).
- **Host authority = none over the simulation** (everyone simulates); the host only collects/redistributes.
  `net_host()` is player 0 and migrates to the lowest still-connected player on a leave (net.cpp:469,
  port_net.md:53-56). Player number = first NetBIOS name slot the peer could add (net.cpp:231-262).
- **Join / level choice**: the lobby (frontend.cpp:1826-1968) calls `net_session_join_begin(session,
  players)` (frontend.cpp:1913) and steps it once per frame (`net_session_join_step`, net.h:121-123,
  frontend.cpp:1867-1877). On success `mp_join_done` (frontend.cpp:1794-1824) sets `Config.flags |= 0x10`,
  exchanges the level choice through one command exchange (player 0's wins, frontend.cpp:1808-1811),
  then **agrees the rules** (`net_agree_rules` / `net_apply_rules`, frontend.cpp:1814-1816).
- **NetGameRules** (net.h:134-143): `{thing_slots, possession_range_pct, reserved[2]}` (16 bytes),
  exchanged once via `net_exchange_block` (all-to-all, net.cpp:626-637); everyone adopts the **host's**
  (net.cpp:647-658); `net_apply_rules` forces `thing_pool_force_slots` + `gameplay_force_rules`
  (net.cpp:661-667); released in `game_after_frontend` when a level starts outside a network game
  (game.cpp:157) and in `net_shutdown`. Two reserved u32 are free for new rule fields; anything bigger
  needs a size bump (the block exchange is size-agnostic, every peer must use the same struct).
- **Late join**: the original's hook exists but only for tick 1: a packet with cmd 1 ("join") triggers
  `net_exchange_frame(g_state->players, 0x801)` - the 8 player records (player.cpp:989-1003). There is no
  state transfer for a joiner who arrives after the level started (the whole GameState + maps would be
  needed: `savestate_save_file` has the serialiser, savegame.h:79-93, ~850 KB).
- **Leaving**: commands 2 / 0x1a / 0x1b / 0x1c / 0x1d call `net_player_disconnect(p)` on every peer in the
  same tick (player.cpp:1037, 1156, 1162, 1166, 1175); the slot stays empty, the host migrates. A peer that
  **crashes** stalls everyone (rto/sto 0: no receive timeout, port_net.md:195-197); a host crash leaves the
  clients' arrays diverging - detected, not recovered.
- **Desync detection (port only)**: `net_sync_arm()` in `game_level_begin` (game.cpp:195); then every
  `MC_NET_SYNC`-th exchange (default 1) adds one 8-byte `{exchange no, checksum}` message per direction after
  the packets (net.cpp:586-620). `net_state_checksum()` (net.cpp:519-570) = FNV-1a over `GameState.rng`,
  `g_rng16`, both pool stacks, every Thing (flags bit 0 masked for human flyers and spells), per player the
  record head + P block minus the local-only fields (`start_tick`, HUD flash counters), the four maps and
  the cell heads. A mismatch is **printed once to stderr** (`sync_compare`, net.cpp:505-515) and counted in
  `NetSyncStats` (`net_sync_stats()`, net.h:165-172). Nothing resynchronises. Cost ~0.5 ms/tick Debug.
  The same checksum feeds `MC_TICK_LOG` (main.cpp:341) for offline two-run comparisons.
- **Max players**: 8 everywhere (`GameState.players[8]`, `commands[8]`, `NetContext` slots 8, lobby slots
  `fe_lobby_slot_pos[16]` frontend_tables.h:24-26). 8 is baked into the GameState layout (mc_types.h:417-418)
  and the 0x801-byte PlayerRec stride; more players = a layout change touching snapshots / saves / references.
- **Lobby screens**: main-menu item 3 (enabled only when `g_fe_network == 1`, frontend.cpp:619, 1062-1069)
  -> state 4 `fe_screen_multiplayer` (frontend.cpp:1826): session name button cycles `CARPET<n>`
  (1920-1924), clicking slot s sets the player count (1926-1937), a 20-line level list offers levels
  `0x32 + line` = 50..69 (1889-1903, the multiplayer maps), "Start" (1901-1917) joins. After a network level
  the front end returns to state 4, not the result screen (main.cpp:897).
- **`mcport <dir> network`** (main.cpp:438-440, 508-524): `net_tcp_from_env(force)` (net_tcp.cpp:730-738)
  builds a `TcpTransport` when forced, or `MC_NET=1`, or `MC_NET_HOST` is set; `MC_NET_PORT` (default
  30600) is the **name server** port; `MC_NET_SYNC=n` sets the checksum interval. The first instance binds
  the port and is the name server, later instances connect to it (net_tcp.h:1-16); every instance also
  listens on an **ephemeral session port** and CALLs connect **directly** (full mesh, one TCP stream per
  pair, frames `{u32 len, u8 type, payload}`, net_tcp.cpp:1-7, 53-56).

### What internet play, spectators and reconnect would need

- **NAT**: the mesh requires inbound reachability on every peer (name-server port + session port). The
  exchange itself only uses client<->host sessions (net.cpp:597-621); peer<->peer sessions are used only by
  `net_exchange_block` (rules). Cheapest route: a **relay mode in TcpTransport** (the name server forwards
  `F_DATA` frames between named sessions so only the relay needs a public port) or a dedicated relay/host
  process; the NetBIOS semantics (`NetTransport::submit/poll`, net.h:78-93) stay, so net.cpp is untouched.
  A lobby browser = a list service on the name server (REG/LOOKUP already exist: net_tcp.cpp:53).
- **Lag**: add an input-delay ring (apply packets of tick N at N+d) around player.cpp:985-1004 so the
  exchange of tick N+d overlaps the simulation of N; and a receive timeout path (today `rto/sto` 0).
- **Spectators**: not in the model. A spectator = a peer that receives the packet block but sends none
  (status 1 slot without a PlayerRec); `net_exchange_frame` would need a "receive-only" role.
- **Reconnect**: needs state transfer (above) plus replaying buffered packets; nothing exists.

### Carrying a much bigger per-tick command set (RTS orders)

- Spare room today: `CmdPacket.pad6[4]` (4 unused bytes, mc_types.h:23) and command ids 0x1f..0xff
  (`player_commands_process` switch, player.cpp:1020-1185, default branch ignores unknown cmds). Enough for
  one small order per tick (e.g. `cmd 0x20 arg=order, pad2=unit group, pad6 = cell x/y + target`), not for
  box-selections of dozens of units.
- Variable-length: the transport is message based (one NCB_SEND = one message, chunked above 0x800,
  net.cpp:167-200), so a second exchange per tick of a **length-prefixed order blob per player** is
  natural: `net_exchange_var(blobs)` next to `net_exchange_frame` - host receives each client's message
  (max size agreed in NetGameRules), concatenates with lengths, broadcasts. Apply point: the loop at
  player.cpp:1012-1202 right after `demo_record_playback_step(cmd)` (1017), so the movie recorder sees it
  (demo.cpp:333 writes fixed 10 bytes - an `mvx` version 3 with `{u16 len, bytes}` records is required for
  replay, demo.h:104-115). Orders must reference Things by **slot index + `thing_slot_generation`**
  (thing.h:103) or by a new stable unit id, never by pointer.
- The RTS state itself (unit groups, build queues, selections per player) must live in simulated memory
  that the checksum covers: either new fields in the P block (PlayerRec is full: 0x801 bytes with
  `PlayerBlock` to the end) or a **new port-side state block** hashed by `net_state_checksum` and saved in a
  new savestate chunk (savegame.h:79-93 pattern: tag + length).

---------------------------------------------------------------------------------------------------------

## 2. Determinism

### What makes the sim deterministic now

- **Integer / fixed point only.** Every simulation file (thing, player, ai_wizard, creatures*, effects,
  spells, projectiles, castle, spatial, terrain_gen, level_features) has zero `float`/`double` in code
  (the three grep hits are comments / a `doubled` footprint cache). Angles are 0..0x7ff, positions u16
  (cell = x >> 8, `MC_CELL_UNITS` 256, mc_types.h:548-558), trig is 16.16 tables (`mc_sin/mc_cos`,
  mc_math.h), `mul32` helpers.
- **Three RNGs**, all state: `GameState.rng` (LCG stepped once per `thing_update_all`, thing.cpp:373, and
  per `level_run_terrain_effects`, thing.cpp:455), `Thing.rng` per Thing (mc_types.h:118), `g_rng16`
  (terrain generator, seeded from `GenMap.seed`, mc_math.h:32-33, terrain.h:7); plus the C runtime
  `rand()` seed `g_ai_rand_seed` used only by `ai_choose_attack_spell` (ai_wizard.h:86-89). `g_rng16`,
  `g_ai_rand_seed`, `g_ai_human_wizard`, `g_terrain_nearly_flat` are **outside GameState** and go into
  the savestate `GLOB` chunk (savegame.cpp:217-246). `g_ai_rand_seed` is **not** in `net_state_checksum`
  (net.cpp:519-570) - harmless today because the original never runs AI wizards in a network game, but a
  bot mode online must either hash it or re-seed it per level.
- **Tick order** is fixed: `game_tick_sim` (player.cpp:1206-1222): palette effect, local input ->
  packet, `player_commands_process`, win check, `thing_update_all` x 1/4/16 (`Config.substeps`), sound
  hook, `hud_tick_state`. `thing_update_all` (thing.cpp:372-449) walks slots **1..g_thing_slots-1 in index
  order** three times (free flagged, rebuild class lists in index order, dispatch handlers); the per-class
  lists in Config are rebuilt every tick, so iteration order = slot order. Cell lists (`g_cell_things`,
  spatial.h:12-21 spiral searches) are LIFO-linked (`thing_link_cell`) - order depends on allocation
  history, which is deterministic.
- **Local-player dependence** (the things a lockstep mode must watch): `creature_proximity_wake_timer` is
  switched off in network games because it reads the local player's position (thing.cpp:431; port_net.md:
  176-178); `g_hook_mana_totals_update` is called with the **local** player's Thing (thing.cpp:433-437; it
  matched across two processes in net_test, so it is player-independent in effect, but audit before
  reuse); `hud_tick_state` writes local-only HUD counters (excluded from the checksum, net.h:173-181);
  `g_video_mode_flags` (1 vs 8) changes castle footprints (mc_globals.h:53-57) and is **not** in
  NetGameRules - R in a network level would desync; `Config.substeps` (F3) and pause are gated SINGLE
  (input.h:194, 198), correctly.
- `g_timer_ticks` (real time) only reaches `PlayerBlock.start_tick` (player.h:82-84); excluded from the hash.

### Rules for a new mode

1. No floats in anything the tick runs; integer LCGs seeded from the match seed; never `std::rand`.
2. Iterate pools / containers in index order; no `std::unordered_*` keyed by pointers; no pointer->index
   arithmetic across instances (the port already stores indices in state).
3. New simulated state goes into memory that `net_state_checksum` hashes and the savestate / movie
   snapshot carries (new chunk tags; `GameState` cannot grow: its 0x38d03 layout is asserted,
   mc_types.h:424, and snapshots / references depend on it).
4. Bots are Thing handlers or a per-tick pass after `player_commands_process`, reading only simulated
   state (never `g_state->local_player`, never real time, never the camera).
5. Keep `PortSettings` faithful by default (settings.h:1-9): the mode is opt-in, so the 43 ctest gates
   (reference_test movie 0, 8 levels, pixel references, 33 front-end screens) stay identical.

### Reference / replay harness to reuse

- `reference_test` (tests/reference_test.cpp) diffs every Thing per tick against the original's dumps
  (port_reference.md). It cannot cover a new mode (no original to dump), but the **record + replay
  determinism test** pattern exists twice: `reference_player_test` records a scripted session with the
  port's own recorder and replays it (tests/reference_player_test.cpp:9-21; script grammar 29-49:
  `level L | movie M | stop T`, `T[-T2] steer/keys/cast/cmd/cheat/left/right/book/close/respawn/fly/face...`),
  and config_test `rules_movie` / `rules_state` (port_round8.md:55, port_round9.md:33-36) record, reload and
  compare 300 ticks of checksums.
- Movies: `demo_record_playback_step` (demo.cpp:339-374) is called per packet inside the sim; the
  extended format `mvx%05d.dat` has a 16-byte `DemoExtHeader` (+`DemoExtRules` in version 2,
  demo.h:104-115) followed by raw packets; `gax` carries the port-form GameState + pool extension.
  A new mode needs **version 3**: a mode id + mode seed/options block in the header, variable-length
  order records, and the mode's extra state in the `gax` snapshot. `demo_open` already forces the
  header's pool size and rules (demo.h:101-103) - the same place to force the mode.
- Savestates (`savestate_*`, savegame.h:72-139) are chunked and forward compatible; add a `RTS ` chunk.
- The two-process lockstep test (tests/net_test.cpp:10-18 parts 2-3: `--peer` self-spawn, scripted input
  per process, per-tick checksum files compared) is the template for an online determinism test of the
  new mode; the in-memory `MemLan` (part 1) runs N peers in one process in threads with one `NetContext`
  each (net.h:187-191) - the fastest way to test bots + orders over the wire headless.

---------------------------------------------------------------------------------------------------------

## 3. Game modes and rules plumbing

- **Mode flags today** are bits of `Config.flags` (mc_types.h:444; ENGINE.md:617-623): 2 record, 4
  playback, 0x10 network/"local chosen" (the lobby sets it; `MC_BIND_SINGLE` keys are disabled by it),
  0x20 no recording, 0x100 "movie / custom" (`-custom` option, ENGINE.md:681, otherwise unused), 0x200 no
  double-click book, 0x8000 cheat gate (chat "RATTY", player.cpp:1097-1103). There is **no "game mode"
  enum**; the original has exactly campaign vs network.
- **Rules plumbing (port)**: `PortSettings g_settings` (settings.h:13-83) is read by platform/renderer
  code; the game logic reads **`gameplay_rules()`** (settings.h:89-95, a `GameplayRules` struct, today one
  field), which `gameplay_force_rules()` overrides for movies / network / loaded saves. `NetGameRules`
  (net.h:134) and `DemoExtRules` (demo.h:111) and the `RULE` savestate chunk (savegame.h:88-90) mirror
  it. A Phase 4 `GameplayRules` extension (mode id, seed, map size, bot count, generator knobs, economy
  constants) plugs into all four with one struct change each; config.cpp `k_desc[]` (config.cpp:172-246)
  needs a line per key for `mcport.ini` / `--set` / the pause menu `k_opts` (game_menu.cpp:27-44).
- **Main loop and status bits**: `game_level_begin` (game.cpp:184-198) -> `load_level` ->
  `sim_load_level` -> `loop_start`; `game_level_tick` (200-207) runs one `g_hook_game_tick`
  (`engine_tick`, engine.cpp:63-70: texture anim, `demo_step` or `game_tick_sim`); the loop ends when
  `PlayerRec.quit != 0` or `status & 8` (game.cpp:99-102); `loop_end` (112-133): `(status & 6) == 4` ->
  restart in place (`level_finish` = reload, 236-238), bit 2 -> `GAME_LEVEL_WON`, else LOST; `game_level_end`
  (209-226) computes stats and `Config.level++` on a win. **Win condition** = `game_check_level_won`
  (player.h:36; win_timer reaches 0x10 ticks above `level.win_percent`, port_game.md:64) called once per
  tick when not paused (player.cpp:1211). A new mode gets its own win check and sets `status` bits 2 / 4 /
  8 to drive the same loop, or bypasses `game.cpp` with its own loop (main.cpp has one `Run` enum,
  main.cpp:526, where `RUN_RTS` fits).
- **Level start** (sim.cpp:43-109): reads `levels/levels.tab` + `levels.dat` (46-57), unpacks the
  0x979c-byte `LevelData` into `g_state->level` (85), then: `player_count = level.player_count` unless
  network (87), `terrain_build(level.gen)` (90), `thing_pool_reset`, `terrain_generate_features`,
  `models_initialise`, `switch_activate(0, true)` (spawns the THING_INIT records with dis_id 0 and counts
  creatures/spells), `players_init_records` (97). **Everything from line 58 on works from the in-memory
  `LevelData`** - a free-standing mode needs only a `sim_load_level_data(const LevelData &)` split (or to
  fill `g_state->level` itself and call the tail): `GenMap` (mc_types.h:34-48: seed, off, raise, gnarl,
  river, sourc, snflt, bhlin, bhflt, rkste = the generator sliders), `ThingInit things[1999]` (class,
  model, x, y cell, dis_id, swi_sz, swi_id, parent, child), `player_block[8][0xd8]` (castle position at
  +4, AI aggression/reaction/accuracy at +4/+8/+0xc, start spells +0x10[24], allowed spells +0x74[24],
  mc_types.h:55, player.cpp:277-295), footer `win_percent`, `player_count`, `castle_level[8]`. No
  `levels.tab` entry is needed; there is no "custom level" path in the original beyond the 20 multiplayer
  maps (indices 50..69, lobby frontend.cpp:1903). The map is **256x256 cells hard-coded** (`MC_MAP_SIZE`,
  mc_types.h:548-549; u16 positions wrap at 0x10000; `mc_cell_of` = `(y & 0xff00) | (x >> 8)`), with ~240
  `>> 8 / & 0xff / mc_cell` sites across the sim and both renderers (grep counts: level_features 50,
  terrain_gen 30, spatial 27, render_landscape 28, render_ext 27, ...) - a larger map is a Phase 4 design
  decision with a wide blast radius, not a knob.
- **Players at level start**: `players_init_records` (player.cpp:253-305) zeroes all 8 records, keeps
  `spell_found` and `cheat`, writes `cmd = 1` (join) into every packet so tick 1 spawns everyone, and sets
  **`is_computer = 1` for every player except `local_player` when not network** (268). In a network game
  nobody is AI; the lobby sets `player_count` (frontend.cpp:1909). The AI wizard is Thing class 3 type 1,
  Table A state 1 (`player_type1_s1_update`, ai_wizard.h:21), created by `player_spawn` from
  `rec->is_computer` (player.cpp:316-320). So **bots are already "a player record with is_computer = 1"**;
  a mode with humans + bots online needs an agreed per-slot `is_computer` mask (a NetGameRules field) and
  the AI's `g_ai_rand_seed` in the checksum. AI tunables per player come from the level's player block
  (`ai_aggression/accuracy/reaction`, PlayerBlock 0x20a-0x20e).
- **Free-fly viewer** (`mcport <dir> <level>`, main.cpp:566-567, 802-834, 926-934): `engine_load_level`,
  `g_hook_player_local_input = nullptr` (470-471: nobody steers), ticks at 20 Hz via `engine_tick`, free
  camera `free_camera_step` (265-286: WASD/QE/RF/arrows/ZX/+-, Shift fast, ground clamp) drawn with
  `render_view_frame` (971-975) - **this is already a detached RTS-style camera over a running sim**,
  minus the HUD and minus any input to the players.

---------------------------------------------------------------------------------------------------------

## 4. Front end (frontend.cpp, ui_draw.cpp)

- **Architecture**: frame-stepped port of `frontend_menu_loop_52070`; `fe_frame` (frontend.cpp:2144-2182)
  runs one `dispatch()` (2108-2124: state 0 intro, 1 config, 2 main menu, 4 lobby, 5 result, 6 language,
  7/8/9 logos, 10 outro), always in 320x200 (2147) into double buffers `s_ed74` / `s_adf68` (99-109),
  then `fe_present` (2126-2142) copies the 64000-byte VGA image into `fb` (pixel-doubled when fb >= 640x400)
  and draws the pointer sprite (`s_sptrs` entry per screen). Blocking fades / FLI / waits are "blockers"
  (379-391). Returns `FE_START_LEVEL` with `*level_out = Config.level`, `FE_START_DEMO`, `FE_QUIT`
  (frontend.h:12-19); mcport reacts in main.cpp:864-877. `fe_enter(state)` selects a screen from outside.
- **Main menu layout**: background `data/screens/mainmenu.dat` + `.pal`, sprites `mmspr` (entry 1 = the
  "resume"/hourglass overlay at (0x166, 0xa), entry 2 at (0x150, 0x56): `fe_main_menu_draw_overlay`
  604-609), globe / timer FLIC animations (853-866), and **`mmmask.dat`: a 320x200 byte mask whose value at
  the pointer is the item id** (mm_part_a 1028-1032: `s_item = s_mask[y*320 + x]`, pointer halved from
  640x400 by `hx`, 351). Hover highlight = `fe_highlight` recolours every pixel whose mask byte equals the
  item through the bright table (253-254, 932-934). Items (`fe_menu_items[39]` = 13 x {u32 handler, u16
  kind}, frontend_tables.h:8-12; `item_kind` / `item_has_handler`, 586-587): 1 new/resume game, 2 name
  dialog, 3 multiplayer (needs `g_fe_network`), 4 quit, 5..10 kinds 1..6 (load / save / start, or the six
  slots in slot mode), 11 start level. Enable logic `fe_menu_item_enabled` 613-622. Click = `s_ev0 & 1`
  edge from `fe_input_poll` (316-349, derived from the held state), Enter = click (1033), Tab cycles, Esc /
  right button leaves slot mode (1034-1040). Handlers 1044-1084.
- **Adding the red urn icon (bottom-right, single player + bots / test entry)**: no free mask value is
  needed if the new button is handled **before** the mask lookup: in `mm_part_a` test a rectangle in
  320-space (as the lobby does with `cur_in` / `click_in`, 352-353) and draw the icon in
  `fe_main_menu_draw_overlay` / `overlay_into_bg` so it is part of the background that `fe_highlight`
  copies. That leaves `mmmask.dat`, the 13 original items and every pixel of the 33 reference screens
  untouched **as long as the icon is drawn only when the new mode is enabled** (a `PortSettings` /
  `PlatformOptions` flag, faithful off; `render_reference_fe_test` runs with faithful settings). Mask
  values 12 (ids 0..12 exist in the table, 12 = `{0,0,0}`) could also be painted into a copy of the mask
  at run time if a hover highlight is wanted.
- **The urn sprite**: a spell lying in the world is `spell_create_common` with `thing_set_sprite(t, 0x4d)`
  (constructors.cpp:1539-1566); `SpriteDesc[0x4d]` (gen/core_tables.h:455 `g_sprite_desc_data`, bytes
  1078..1091) = `{base_sprite 116, half_xy 0 (from aspect), half_z 150, shade_group 0, load_priority 0xff
  (resident), draw_type from the tmap}`: **tmaps.dat chunk 116** (a flagged "keep" pickup uses desc 0x118,
  spells.cpp:444). Chunk layout `{u8 flags, u8 draw_type, u16 w, u16 h, w*h pixels, 0 = transparent}`
  (sprites.h:4-6), pixels at `g_sprite_ptr[116] + 6` once `sprite_ensure_loaded(116)` (sprites.h:17, 41;
  resident groups are loaded by `sprite_cache_init`, sprites.h:23-25). The front end does not link the
  sprite cache today (frontend_test links `${MC_SIM_ALL}` without render files), so either read chunk 116
  directly through `mcdata/tmaps` (`mc_tmap_set_load` / `mc_tmap_get`, as `sprite_table_init_sizes`
  does, thing.cpp:337-351) or convert it once to a `UiSprite` (span encoded, ui_draw.h:22-26) and blit
  with `fe_draw_sprite_to` (202-227). The tmap uses the **game palette**, the menu the `mainmenu.pal`
  palette: remap through `palette_find_nearest(s_pal, r, g, b)` (ui_draw.h:63) per pixel at load.
- **New option screen (seed, world size, bots, generator sliders)**: there is no free state (3 is "none",
  dispatch 2111-2123); add state 3 or 11 with the lobby as the template: load a background + `sfont1`
  (1839-1846), `fe_text` / `fe_fill_rect_clipped` / `fe_draw_sprite` primitives (230-250), `cur_in` /
  `click_in` hit rectangles, `g_key_down` for keys, Esc to go back (1888), leave with `g_fe_leave = 1` and
  the result code. Return a new `FeResult` (e.g. `FE_START_RTS`) or reuse `FE_START_LEVEL` with a mode
  flag set on `g_settings`. The multiplayer lobby can host the same options panel for online games before
  "Start"; the host's values then travel in NetGameRules.
- **640x480 front end**: dead in the original (port_net.md:204-210); `fe_frame` forces mode 1 and
  `fe_present` pixel-doubles into a larger fb. In composed mode the whole FE frame is a "no view" frame
  (compose.h:17-18, 79-81): pillarboxed 4:3. A native-resolution options page would have to be drawn by
  a different path (see section 8); the cheap route is to keep the new screens 320x200 like the rest.

---------------------------------------------------------------------------------------------------------

## 5. HUD (hud.cpp, ui_draw.cpp, text)

- **What `render_frame_1fab0` draws** (`frame_pass`, hud.cpp:736-852) by `PlayerRec.input_mode`: 0/3 flight
  = view (`render_view_frame`, 757) + circular radar + blips (762-768, `radar_zoom_pct` applied at 765) +
  hand labels (773-774) + status bars (castle / balloons / wizard health & mana, `ui_draw_status_bars`
  409) + messages (778); 1 help screen (782-800, `k_help_lines`); 2 spell book (657-695: 24 cells 4x6 from
  (0x180, 0xa2/0xc2) step 0x40 x cell_h, hit test on `g_mouse_x/y`, writes `Config.spell_slot` 690) then
  the map screen; 4 map screen. Tail: "MOVIE: n" and the attract credits (812-851). Split: `render_frame` =
  writes + draw, `hud_tick_state(p)` = state writes only (per tick, installed as `g_hook_frame_state`,
  engine.cpp:30), `render_frame_draw` = pixels only (719-734) - so **drawing at frame rate is free of
  game-state side effects**; any new overlay must obey the same rule (no state writes in draw code).
- **Debug overlay** (`ui_draw_debug_overlay`, hud.cpp:536-576): drawn when `PlayerRec.flags & 8`, toggled
  by **Alt+V = cmd 4 arg 8** (input.h:222, needs the cheat gate `Config.flags & 0x8000` = chat line
  "RATTY", player.cpp:1097-1103); shows the original's build strings, level number, "Transfer rate"
  (network exchange time), "GameTurn" (frame time), player tick / timer, "Thing 164, Active N" (live
  Things), sizes; memory statistics are TODO. It is called by main.cpp:956 after `render_frame_draw`.
  `hud_test` renders it (tests/hud_test.cpp:309-319, `hud_test_frame_debug_*.ppm`). It is **text only, font
  1, x = 0x140**: a thin starting point; the pacing overlay (F11, main.cpp:324-331) is the port's own.
- **Text**: HUD fonts = span-sprite tables `data/font0` / `font1` (`g_ui_font_tables`, ui_draw.h:36),
  `ui_set_font(n)`, `ui_draw_text(s, x, y, colour)` / `ui_text_width` / `ui_font_line_height`
  (ui_draw.h:98-108) in the 640-wide virtual screen (halved in 320x200), colours from the colour cube
  (`ui_colour(r,g,b)`, `ui_col_white/red/...`, ui_draw.h:61-75), translucent box behind text
  `ui_draw_text_background`, shading `ui_shade_rect`. Language strings `text_get(i)` (text.h). The pause
  menu (game_menu.cpp:346-387) shows the toolkit in use: `ui_shade_rect` panels + `ui_draw_text` rows.
- **World -> screen projection**: in the faithful Thing renderer `thing_project(xc, h_rel, z)`
  (render_things.cpp:584-590) maps camera-space `(x right, z forward, height rel. camera)` to the rolled
  screen using `g_rcam` (`focal`, `horizon`, `screen_cx/cy`, `sin/cos_roll`, render.h:112-131, filled by
  `render_landscape` each frame); camera space comes from `render_thing` 599-605 (`dx = x - cam_x16`,
  wrap-aware i16, rotated by `cos_yaw/sin_yaw`, culled at `zc > 0x40 && d2 < cull_dist2`). It is `static`
  and writes `g_sprite_blit.x/y`; the extended renderer (render_ext.cpp) has its own copy for the
  display-sized view. **Needed: one exported `render_project_world(const Pos&, int *sx, int *sy, int *depth)`
  for the renderer in use**, plus a per-frame list of projected anchors for things drawn (the probe
  `g_sprite_blit_probe`, sprites.h:83-85, already sees every faithful blit) - then damage numbers and health
  plates are `ui_draw_text` at (sx, sy - h) in the HUD layer (section 8 explains the resolution caveat).
- **Map screen as an RTS overview**: `map_screen` (hud.cpp:697-709) = small view window top-right
  (`render_set_view_window_top(fb, 0x10)`) + **rectangular radar** `ui_draw_radar(0, 0, cx, cy, 0x17e,
  0x17a/0x19e, yaw, scale 0xaa, _, rect 1)` + blips + the player list when the pointer is low. The radar
  (91-135) samples `shade[light << 8 | avg_colour[type]]` per cell (heightmap-lit average texture colour,
  not raw height), rotated by yaw, `scale 0x100 = 1 cell per pixel` (765), so 0xaa over 382 px ~ 254 cells:
  **the map screen already shows essentially the whole 256x256 world** top-down, centred on the player and
  rotated with the view. `ui_draw_map` (290-320) is the unrotated 1 px/cell variant (2x2 blended in 640)
  with no caller. Blips (`ui_draw_radar_blips`, 144-285) already classify things (creatures black, player
  colours, mana gold, castle line). An RTS overview = `ui_draw_radar` at yaw 0 with a chosen scale +
  selection boxes + a cell->screen helper (`u0/v0/A/B` maths at 113-118 inverted), drawn through the same
  2D layer.

---------------------------------------------------------------------------------------------------------

## 6. Input (input.cpp, mcport)

- **Model**: the platform feeds the original's device state (`g_key_down[128]`, `g_key_last`, `g_mouse_x/y`
  in 640x400/480 virtual space, click latches `g_mouse_click_*`, held `g_mouse_held_*`, input.h:16-39)
  through `input_key_event(set1 scancode, down)` / `input_mouse_move` / `input_mouse_button` (input.h:61-81).
  `player_local_input()` (hooked as `g_hook_player_local_input`, called first in `game_tick_sim`) turns
  it into **one CmdPacket per tick** (`player_queue_command`, first command wins; bindings table
  `g_input_bindings[]`, input.h:178-237). mcport: SDL scancode -> set-1 in `set1_scancode` (main.cpp:80-113),
  key releases deferred until after a tick (`deferred_releases`, 757-762), port-only chords swallowed by
  `config_key_action` (config.h:92-93; `[keys]` section: save/load slots, quit_now F12, menu Esc,
  wasd / book_tab flags).
- **Mouse look** in a level: SDL **relative** mode (`plat.set_relative_mouse(true)`, main.cpp:710,
  platform_sdl.cpp:340), a virtual pointer `s_level_mx/my` integrates `mouse_dx/dy` scaled to 640-space
  with sub-pixel remainders (714-737) and is fed as the absolute position; steering = pointer offset from
  the centre (port_input.md:129). In the front end the OS pointer is absolute and mapped (749). Composed
  mode remaps window -> game-frame pixels in `Platform::poll` (platform_sdl.cpp:305-327) and `warp_mouse`.
- **What an RTS "cursor + click-to-select" mode needs**: switch relative mode off in that mode (the
  pointer exists already: `mouse_cursor_draw`, frontend.h:96-98, pointers table entry 1 = the book arrow);
  a **display-resolution** pointer position (today's `in.mouse_x/y` are game-frame pixels, 320x200 or 640x480
  - too coarse to pick units on a 4K view; `compose_display_to_frame` / `window_to_drawable` are the
  mapping, compose.h:91-92, so keep the drawable coordinates alongside); a pick = inverse projection (ray
  from camera through the pixel, intersect the heightmap for a ground cell, or compare against the
  per-frame list of projected Thing anchors / extents); drag-box selection and the mouse wheel
  (`Input.wheel`, platform.h:27). Clicks must become **orders in the packet**, not direct state writes
  (determinism / replay). Hot-keys: the game only reads 1..0, Enter, Space, Esc, I, P, R, [, ], F-keys and
  Shift/Alt/Ctrl combos (input.h:178-237); the RTS mode can take plain letters (W/A/S/D are only read
  when `keys_wasd`).
- **Pause menu as a UI toolkit** (`GameMenu`, game_menu.h:36-91): pages (main / save / load / options),
  rows `{label, value, enabled, action, arg}` (game_menu.cpp:60-66), options table `k_opts[]` driven by
  config keys with choices / ranges / step (27-44), keyboard (`key(sdl_scancode)`), pointer hover + click +
  wheel (`pointer`, `wheel`), drawn with the HUD font into the virtual screen (`draw`, 346-387), returns
  `GameMenuResult` commands that main.cpp executes (766-787); the level is held while open (883-887). It is
  **page / list shaped**: good for a console-less debug menu (spawn lists, toggles, teleport presets), not
  a free-form panel system; a Phase 4 "panel" layer (unit cards, build menu) wants a small immediate-mode
  helper over `ui_draw_*` with the same hit-test discipline (game_menu.cpp:320-339 `pointer`).

---------------------------------------------------------------------------------------------------------

## 7. Existing debug / test tooling, and the proposed Phase 4 debug suite

### Tests (`src/tests/*.cmake`, 43 ctest targets; `mc_unit_test` links `${MC_SIM_CORE}` / `${MC_SIM_ALL}` without the renderer, `mc_test` the whole library)

| test | one line |
|---|---|
| mcdata_tests | C data layer (RNC, files, levels, sprites, tmaps) |
| thing_test | pool / cell-list invariants, dispatch binding, layout vs the level-38 snapshot |
| terrain_test | generated level 38 maps vs the engine's map dump |
| features_test | level features + terrain painting vs the dump; wizard castles |
| constructors_test | every Table B constructor; level-38 static things byte-identical |
| spatial_test | collision searches, area damage, pending damage, castle site tests |
| player_test | player records / spawn / packets vs the snapshot; movie format |
| projectiles_test | class 9 handlers; the snapshot's arrows re-fly bit-exactly |
| spells_test | class 12 cast helpers and phases; snapshot spells reproduced |
| effects_test | class 10 handlers (explosions, mana, meteor, castle levelling...) |
| castle_test | castles, balloons, scenery, switches |
| creatures_test / creatures2_test / creatures3_test | all 17 creature types |
| ai_wizard_test | the computer wizard (modes, goals, spells, threats) |
| input_test | device state -> packets; reproduces all 8551 recorded packets |
| sim_test | whole sim: level 38 for 412 ticks vs the snapshot; movie plays to the end; 9 levels x 3000 ticks |
| game_test | game flow: skip table, FE bracket, win / lose / restart / quit, stats |
| hud_test | 2D layer by construction; flight HUD / book / map / help / debug overlay snapshots (PPM) |
| sprites_test | sprite cache + Thing renderer |
| raster_test / render_test / tables_test | rasteriser, landscape renderer on synthetic data, tables.dat regeneration |
| engine_test | renders level 38 + one frame of every level (PPM) |
| sound_test / audio_test / opl_test | sound manager, mixer + HMP, OPL2 vs the original driver |
| fli_test | FLI decoder, cue scripts, palette effects |
| frontend_test | every front-end screen with scripted input (fe_*.ppm), save-game format |
| net_test | MemLan 3 peers; 2 and 3 processes over TCP in lockstep; lobby join; sound summary |
| reference_test | per-tick diff vs the original over movie 0 (+ `reference_levels`, `reference_gen`, `reference_player` suites) |
| reference_player_test | records scripted sessions and checks the record/replay round trip |
| render_reference_test / render_reference2_test / render_reference_fe_test | pixel-identical frames vs DOSBox dumps (view, 640x480, options, 33 FE screens) |
| pool_test | Thing pool > 1000 slots |
| pacing_test | interpolation math + headless pacing |
| compose_test (+ compose_present_bench) | native-resolution compositor |
| config_test | ini / env / --set precedence, gamepad mapping, save anywhere, rules in movies / states |
| possession_range_test | the one gameplay rule |
| render_ext_test / render_ext_segments_test / render_ext_mana_test | extended renderer |
| game_menu_test | pause menu + `config_set_keys` |

### Headless / scripting switches that exist

- `SDL_VIDEODRIVER=dummy` (+ `SDL_AUDIODRIVER=dummy`) runs mcport without a display (port_pacing.md:116).
- `MC_SHOT=n[,m..]` writes presented frame n as `mcport_shot_n.ppm` (composed output when composing,
  main.cpp:177-213); `tools/port/ppm2png.py` converts; `debug_screenshot` (Alt+X in flight, hud.cpp:769,
  ui_draw.h:135) writes `scrNNNNN.ppm`.
- `MC_TEST_INPUT="frame@x,y[c]+frame@kN+..."` injects mouse moves / clicks / SDL key presses by frame
  number (main.cpp:217-263) - crude (frame numbers are frame-rate dependent) but used for the menu check.
- `MC_TICK_LOG=<file>` (tick + `net_state_checksum` per tick), `MC_FRAME_LOG`, `MC_QUIT_AFTER_TICKS=n`
  (main.cpp:435-437, 590), `MC_TICK_HZ` (any rate: 1000 Hz for fast runs), `MC_NET_*`.
- `--set section.key=value`, `--faithful`, `--config file`, `MC_SAVE_DIR` (config.h:5-18, main.cpp:425-431).
- Savestates: `mcport <dir> load N`, Ctrl/Shift+F1..F10, pause menu; `savestate_save_file(path, name)`
  callable from anywhere between ticks (savegame.h:118-132).
- Movies: Alt+R records (cmd 0xc), `mcport <dir> demo N` plays; `demo_open` / `demo_step` (demo.h:22-27).
- Original cheats (all deterministic packets, so they are replay / network safe): chat "RATTY" opens the
  gate (`Config.flags 0x8000`); Alt+F1..F7 = cmd 0x1e cheats 1..7 (player.cpp:922-980: all spells, 100000
  mana ball, destroy wizards / castles / balloons, heal, kill all creatures; also with player name
  "chronicle"); Shift+C / F (won / lost), Shift+K / L (kill self / own castle), Shift+R (restart), F3
  fast-forward x4 / x16 (`Config.substeps`), P pause; `PlayerRec.cheat == 0xae89e` = the 1,000,000-health
  dummy player (player.cpp:392); `-cheat n` / `-password` (debug_bits bit 1 at 0xf851b9, hud.cpp:575) exist
  in the original's command line only (not parsed by mcport).
- Logging: `printf`/`stderr` only (`notice()` also sets the window title, main.cpp:351-363); no log file,
  no levels. Profiling: the title bar / F11 line (fps, avg/worst frame ms, ticks/s, main.cpp:1000-1028),
  `Platform::last_present_us / last_convert_us` (platform.h:59-60), the pacing counters; no per-system
  tick cost breakdown.
- Thing statistics: `thing_pool_live_count`, `g_thing_alloc_failures`, `thing_dispatch_report` (thing.h:97-100, 35).

### Proposed Phase 4 debug suite, mapped to hook points

Design rule for all of it: **every state-changing debug action is a command packet** (new cmd ids 0x40..,
or a "debug order" record in the variable-length stream), so it is recorded in movies, replayed in tests
and valid in lockstep; read-only tools (overlays, inspector, dumps) run at draw time and never write.
Effort: S = hours, M = a day, L = several days.

| item | hook points | effort |
|---|---|---|
| **In-game console** (text line, history, command parser) | input mode 3 is the chat line (`cmd 0x10/0x11/0x13`, player.cpp:1074-1110; `g_input_scancode_ascii`); add a port-only console mode in main.cpp that takes keys before `input_key_event` (main.cpp:693-704, like the pause menu at 607-616), parses locally, and emits packets through `player_queue_command` / the order stream. Draw with `ui_draw_text` + `ui_shade_rect` after the HUD (main.cpp:954-965). Same parser serves scenario files and a stdin/pipe reader for headless runs | M |
| **Free camera / teleport** | `free_camera_step` + `render_view_frame(fb, cam)` (main.cpp:265-286, 971-975) already exist for the viewer; in a level: a `Camera` override in `frame_pass` (hud.cpp:743 picks `g_render_interp.camera` or `player_camera`) - add a port-only `g_debug_camera` there (render-only, no state); teleport = packet `cmd 0x40 pos` applied in `player_commands_process` via `thing_move_to` (thing.h:132) + `player_log_position` | S/M |
| **Spawn Thing by class/type at cursor** | `thing_create(&pos, cls, type)` (thing.h:155) inside a new packet handler; cursor -> world via the projection inverse (section 6) or the map screen's cell maths (hud.cpp:113-118); table names from `mc_class_name` / `mc_model_name` (mcdata/level.h:45-46) | S (console) + M (cursor pick) |
| **Give mana / god mode** | cheats 2 and 6 exist (player.cpp:948-972); god = `invuln_timer` (PlayerBlock 0x14b) refreshed per tick by a debug flag in simulated state (new chunk) or the 0xae89e dummy semantics (player.cpp:392) | S |
| **Time scale: pause / step / fast-forward** | main.cpp level loop (880-903): `next_tick` clock, `t < 4` cap; add `s_debug_paused`, `s_debug_step_n`, `tick_hz` multiplier; `Config.substeps` (F3) is the sim-level x4/x16 (changes results: only for play); `s_interp.hold()` while paused (pacing.h) | S |
| **Thing inspector** (click a thing -> struct dump) | pick via projected anchors (`g_sprite_blit_probe`, sprites.h:83-85 for the faithful renderer; equivalent in render_ext.cpp) or nearest Thing to the cursor ray; print `Thing` fields (mc_types.h:116-178) + `PlayerBlock` (220-307) with `ui_draw_text` in a panel; `MoveDesc` via `mc_move_desc` | M |
| **Overlays**: cell grid, thing ids, AI mode, paths, spatial cells, tick cost | cell grid = project the 4 corners of cells around the camera (`terrain_height_at`, thing.h:190) and `vga_fill_line`; thing ids / AI mode (`P.ai_mode` 0x19f, `Thing.target/home`) as text at the projected anchor; spatial = `g_cell_things` chain lengths coloured on the map screen radar; tick cost = `plat.ticks_us()` around each phase of `game_tick_sim` (player.cpp:1206-1222) and per-class in `thing_update_all` (thing.cpp:439-448) into a port-only stats struct, drawn like the F11 line (main.cpp:326-331) | M-L |
| **Deterministic scenario files** (`spawn X; wait N; assert ...`) | a line interpreter in `mcport` (headless) and in a new `scenario_test` (mc_unit_test over `${MC_SIM_ALL}`, like reference_player_test's script runner, tests/reference_player_test.cpp:29-49 - extend that grammar with `spawn / give / teleport / assert_health / assert_count / dump`); assertions read state directly; the same file drives mcport via the console | M |
| **`mcport <dir> rts --seed S --size W --bots B` headless with periodic JSON dumps** | new `Run` mode in main.cpp (526) using `sim_load_level_data` (section 3) with a generated `LevelData`; `MC_DUMP_EVERY=n` writes `dump_<tick>.json` (per player: health / mana / castle / units; per Thing summary; checksum) from `sim_tick` (main.cpp:335-343); `MC_QUIT_AFTER_TICKS` ends the run; `MC_SHOT` for pictures; stdout one line per dump so Claude can tail it | M (mode) + S (dumps) |
| **Screenshot on demand** | `MC_SHOT` is by frame number; add console `shot [name]` -> `debug_screenshot` / the composed `maybe_screenshot` body (main.cpp:186-210) by request flag | S |
| **Desync checker** (hash per tick, compare two instances) | online: `MC_NET_SYNC` + `net_sync_stats()` already; add a console `sync` readout and a **per-subsystem hash** (Things by class, players, maps separately, net.cpp:519-570 split into parts) so the first differing part is named; offline: `MC_TICK_LOG` files diffed by a script, plus a `--replay-check movie` mode that plays a movie twice and compares (`config_test rules_movie` pattern) | S/M |
| **Save / load from the console** | `savestate_save_file` / `savestate_load_file` (savegame.h:121-131), `load_state_slot` (main.cpp:401-420) | S |
| **Log file with levels** | wrap the `printf`s in a `mclog(level, fmt)` writing to `<save_dir>/mcport.log` and the console scrollback | S |

Hard dependencies for the suite: (1) the projection export (section 5) - unblocks inspector, overlays,
cursor spawn; (2) the display-resolution pointer (section 6); (3) a port-side simulated state block that
the checksum / snapshot / savestate carry - needed before any debug flag that changes the sim (god mode,
time-scale ticks are fine as they are outside the sim).

---------------------------------------------------------------------------------------------------------

## 8. Resolution / rendering plumbing relevant to UI

- **Frame buffers**: the game frame `Platform::fb` is 320x200 or 640x480 palette indices (`plat.init(..,
  320, 200, 4)` main.cpp:474; `video_mode_changed` resizes on R, 156-160). All 2D code draws into it in the
  640-wide virtual space, halved in lo-res (ui_draw.h:6-12). The front end always draws 320x200 (section 4).
- **Presentation** (platform_sdl.cpp): SDL_Renderer with the `direct3d11` hint (96-98); `present()` = the
  frame scaled with `SDL_RenderSetLogicalSize` (113, 149-159); `present_composed(ComposeOutput)` (192) =
  the hi-res 8-bit **view** texture + the game frame as an 8-bit **HUD layer with a mask**, palette applied
  last (compose.h:1-19, 30-51). Composition is installed when `display.compose = 1` (`compose_install`,
  main.cpp:484-485); the view is rendered by `render_view_ext` at the drawable size (or `view_width/height`),
  point-sampled down into the game frame's view window so the 2D pass draws over a matching picture; the
  HUD layer = pixels the 2D pass changed (diff + inverted second pass, `compose_draw_frame`, compose.h:69-74,
  main.cpp:134-142), scaled integer / fit / filtered (`hud_scale_mode`) with the corner blocks optionally
  anchored to the display corners (`hud_corners`). Mouse mapping through the HUD rectangle (compose.h:91-92).
- **So the UI today is always drawn at 640x480 (or 640x400) virtual resolution and scaled**: text is
  chunky at 4K (integer scale 4-5x), and anything drawn in the 2D pass lands in the HUD layer at that
  resolution - acceptable for damage numbers / health plates in a first version (the F11 line and the
  pause menu already work this way, port_round9.md:57-58), **not** for a native-resolution RTS UI.
- **Native-resolution UI over the 3D view** would need a third layer in `ComposeOutput`: an RGBA (or
  8-bit + mask) overlay at `display_w x display_h` drawn after the view, with its own text renderer (the
  span fonts are 8 px high - scaled up they stay 8 px fonts; a TTF/atlas path or an SDF font would be new
  code) and its own blit list in `Platform::present_composed` (platform_sdl.cpp:192+). The GPU side is
  plain SDL textures, so an extra `SDL_RenderCopy` is cheap; the work is the overlay drawing API and the
  coordinate contract (display pixels, `compose_output().view_x/y/w/h` for the view rectangle,
  `compose_frame_to_display` for HUD-space anchors). The `view_width/height` knob (settings.h:31-32) keeps
  the 3D cost down while the overlay stays sharp.
- Palette: everything is 8-bit through `g_display_palette6` (fades / flashes are palette effects,
  main.cpp:168-174); an RGBA overlay layer must either respect the fade (multiply by the fade level) or be
  excluded from it on purpose (debug overlays should be).
- Headless rendering works through the same path (`SDL_VIDEODRIVER=dummy`; `compose_to_rgb` is the CPU
  reference compositor used by `MC_SHOT`, compose.h:94-97, main.cpp:191-195), so screenshots of a composed
  4K frame are available without a GPU.

---------------------------------------------------------------------------------------------------------

## Appendix: facts most likely to bite

1. `GameState` is a fixed 0x38d03-byte image with 8 players and 1000 Things; the pool extension lives
   outside it (thing.h:39-49). New simulated state must follow the extension's example (outside, hashed,
   chunked), not grow the struct.
2. The map is 256x256 cells with u16 wrap-around coordinates everywhere; "large maps" is a redesign.
3. `is_computer` is decided in `players_init_records` from `local_player` and the network flag only; the
   AI uses a CRT `rand()` seed that the network checksum does not cover.
4. `g_video_mode_flags` is game state (castle sizes) and not agreed over the network.
5. The command packet is 10 fixed bytes, recorded as such; orders bigger than 4 spare bytes need the
   variable-length exchange + movie format v3.
6. Lockstep blocks the whole process in `net_receive` (SDL pumped by the idle hook only); any new mode
   that wants the window responsive during a stall needs a non-blocking exchange or a worker thread
   (NetContext is thread-local, net.h:187-191, which helps).
7. All 2D output is 640-wide virtual resolution scaled; a sharp RTS UI needs a new overlay layer.
8. The front-end reference test is pixel-exact on 33 screens: new menu pixels must be gated by a non-faithful setting.
