# Player port (player.cpp, demo.cpp) - agent D report, port round 2, 2026-10-06

Files: `src/mcengine/player.h` / `player.cpp`, `demo.h` / `demo.cpp`, `tables/player.tables` ->
`gen/player_tables.h`, `src/tests/player_test.cpp` + `player_test.cmake`, typed `PlayerRec` /
`PlayerBlock` in `mc_types.h`. `player_test.exe` -> exit 0, no warnings; the whole `mcengine` library
still builds with the changed `PlayerRec`. `thing_query.*` was not written (see "Not done").

## Functions translated

| port | original |
|---|---|
| `players_init_records` | players_init_records_3bc10 |
| `players_clear_records` | players_clear_records_3bf60 |
| `player_spawn(rec, thing)` | player_spawn_3f360 (2 stack args: record, Thing; `&things[0]` = create) |
| `player_rebuild_spell_index` | player_rebuild_spell_index_40240 |
| `player_set_input_mode(rec, mode)` | chat_message_show_3bb50 (it sets PlayerRec.input_mode; cursor / sound parts are TODO) |
| `game_tick_sim` | simulation half of game_tick_update_32e80 |
| `player_commands_process` | player_commands_process_3a8b0, every command number of the jump table 0x3a810 |
| `player_log_position`, `player_camera` | player_log_position_3e080; the 8 pushes of render_frame_1fab0 at 0x1fbb9..0x1fc2d |
| `game_check_level_won` | game_check_level_won_3db20 (translated, not stubbed) |
| `player_type0_s0_update` | player_type0_s0_update_402c0 (class 3 state 0) |
| `player_flyer_move`, `player_terrain_collide` | player_flyer_move_3fc00, player_terrain_collide_3fa40 |
| `player_apply_controls`, `player_cast_spell` | player_apply_controls_40e70, player_cast_spell_410f0 |
| `player_apply_hits`, `player_take_damage` | player_apply_hits_40b70, player_take_damage_42770 |
| `player_dying_update`, `player_look_at_killer`, `player_type3_s3_update` | 405f0 (state 2), 409e0, 40ab0 (state 3) |
| `player_set_combat_music_timer`, `player_set_palette_effect` | 40b50, 3f210 |
| `player_note_fire_distance`, `player_note_ridge_distance` | 3f240, 3f2c0 |
| `mana_totals_update`, `mana_add_to_owner` | mana_totals_update_427d0 (installed as `g_hook_mana_totals_update`), 428e0 |
| `castle_apply_level_stats`, `castle_set_level_stats`, `castle_spell_set_capacity` | 42170, 42200, 42370 (needed by player_spawn; jump tables 0x421d8 / 0x42344) |
| `demo_record_playback_step` | demo_record_playback_step_3c540, playback path |
| `demo_load_state`, `demo_load_terrain`, `demo_close` | 3c200 (+ 3dc10 through thing_relink_snapshot), 3c360, 3c7c0 |
| `demo_open`, `demo_playing`, `demo_step`, `demo_packets_read/total` | port API |

`player_register_handlers()` binds 0x402c0, 0x405f0, 0x40ab0 and installs the mana hook.

## The recording (movie/mvi00000.dat) and the snapshot

- Headerless stream of 10-byte `CmdPacket`s, **one per player per tick in player order**, no lengths,
  no checksums. `demo_record_playback_step_3c540(packet*)` is called from inside the per-player loop of
  player_commands_process right before the packet is executed; on playback it `file_read`s 10 bytes into
  the packet.
- 342,010 bytes = **8550 ticks x 4 players x 10 bytes + one last packet** (player 0, `cmd 2` = quit,
  which ends the recording after it was written). So 342,010 does not divide by 40; the remainder is the
  quit packet. Players 1..3 (AI) have all-zero packets. Player 0: cmd 0 x3766, 6 x4676 (no handler: only
  marks a packet with steering / keys), 0x14 x54 (input mode 2 = open spell book), 0x15 x45 / 0x16 x9
  (book slot for the left / right hand), 2 x1. Bytes: +0 cmd, +1 arg, +2 second arg (cmd 0x17),
  +3 / +4 steer x / y (i8), +5 keys (1 faster, 2 slower, 4 / 8 strafe, 0x10 / 0x20 fire left / right),
  +6..9 always 0.
- The snapshot pair is saved when the first packet is recorded, i.e. **in the middle of
  player_commands_process**: tick counters already incremented (player 0 tick 413), player 0's packet
  not yet executed. Playback loads state + terrain at the same point (first call with packet index 0
  while the handle Config+9 is 0), so the first replayed tick starts inside the command loop. The port
  does the same: `demo_open()` only sets Config.movie / flags bit 4, the first `game_tick_sim()` loads.
- Playback ends on a packet with cmd 2 (or a short read, or `input_changed_34090`): demo_close,
  `players[local].status = 8`, cmd cleared; the rest of that tick still runs. The replay is therefore
  8551 sim ticks (8550 full + the quit tick), player tick 413 -> 8963.
- map00000.dat is 0x612c2 bytes: the five maps and then 0x12c2 bytes read to **0xb58b0 =
  g_corner_tex_table** (2401 x 2), not "generator / RNG state at 0x12dfb0".
- demo_load_state keeps the first dword of the state and the 36 option bytes +0x2195..+0x21b8, then
  relinks and calls models_initialise (free stack order changes) and sets active_top = -1.
- What the snapshot shows about the session: player 0 hovered at its start position for all 412 ticks
  (speed 0), had used the cheats "access all spells" (24 spells in slot order = spell id, left-over
  text in the message buffer) and "more mana" (one mana ball with 3,100,000 mana owned by the wizard,
  hence mana_total 3,101,000), and had no spell in either hand (slots 0xff).

## Verification (player_test, numbers from the run)

Against the snapshot (ground truth):
- `mana_totals_update` reproduces Thing.mana_total of all four wizards: 3101000 / 14704 / 10096 / 7024.
- `players_init_records` on the snapshot's level data reproduces, for all 8 records: index,
  is_computer, view_entry 0x1f, log_count 0x20, template + the 31 untouched log entries, names,
  ai_allowed[24]; for the AI records hotkey table, hand slots and the spell-id-per-slot templates
  (= types of the spell Things in the snapshot's slots); pending cmd 1 of the unused players 4..7.
- `player_spawn` (test constructors for class 3 types 0 / 1 / 2 and the 24 spells; the real ones are
  agent C's) reproduces: Thing flags (0xd local, 0xc AI), state, sprite 0x2c / 0x111.., extents after
  thing_set_sprite, max health 10000, P.mana 1000, player_no, the 0x10 fill at P+0x14d, fire / ridge
  distance 0x800, AI aggression / accuracy / reaction from the level block, threat table (0x601f x8 for
  AI, 0 for the human), spell Things per slot + spell index, position of player 0 (start + 0x100 above
  ground = the snapshot position exactly), one castle per AI wizard with level-0 stats (5000 mana), 3
  footprint stamps. `countdown15f == 2000 - (tick - 1)` and regen values (mana_cost = total / 2000,
  health_regen = max / 2000) hold in the snapshot.
- `castle_set_level_stats` matches max health / capacity of the 2 castles in the snapshot (levels 1, 2).
- `player_rebuild_spell_index` reproduces P+0x2a4 of all players; `player_log_position` reproduces the
  live log entry; `player_camera(0)` = (640, 30080, yaw 0, z 384, 0, 0, zoom 128).
- One `player_type0_s0_update` on the snapshot is a fixed point: 0 bytes of the Thing change, only
  P.countdown15f changes in the P block.
- `demo_load_state` + `demo_load_terrain` give the same GameState as the direct load (0 differing bytes
  outside the rebuilt free / recyclable stacks; same free set).

Replay of the whole recording through `game_tick_sim()` (only the player handlers bound):
- 8551 ticks, 34201 of 34201 packets consumed, demo closed, status 8.
- Packet alignment: P.input_bits equals the recorded key byte and both hand slots equal the last
  recorded 0x15 / 0x16 selection in every tick (0 mismatches); the camera log always holds the position
  the tick started from (0 mismatches).
- Flight: path length 607,883 units (2374.5 cells), moving in 8413 ticks, max step 115 units / tick
  (limit: 80 forward + 80 strafe at right angles), max |dyaw| 31 (= 254 / 8), speed / strafe within
  +-0x50, never below ground + 0x80 (0 violations), altitude above ground 128..4292 (97 % of the ticks
  below 0x500; the peaks are flights off the 6400-high plateau), max |dz| 330 (clearance clamp
  at cliffs), 91 ticks over water. Top-down picture (`build_D/Debug/player_replay.png`): one smooth
  curve from the western lake across the map edge and around the northern half, no jumps.
  CSV per tick: `build_D/Debug/player_replay.csv`.
- Synthetic: wall slide of player_terrain_collide (step (+64, +16) into a type-8 cell becomes
  (0, +10); corner refused), hit / knock-back decay (50, -4 per tick, 11 ticks) / regen pause / shield /
  invulnerability, death -> fall 12 ticks -> 24 spells dropped -> state 3 -> spectator turn -> respawn
  by command 0xf.

**Not verifiable**: the replayed path against the path originally flown. The log at PlayerRec+0x24a
is not a history: only entry [log_count - 1] is ever written (entries 0..30 keep the template), so the
"feed the logged steering and compare position deltas" check from the task is not possible, and the
snapshot contains no motion of player 0 at all. The replay also runs in a frozen world (no creatures,
spells, damage), so it must diverge from the original flight at the first Speed-up / Teleport cast or
knock-back. What the replay proves is the packet plumbing and that the movement code is stable and
within its limits for 8550 ticks of real input, not that every position is the original one.

Unported handlers the replay dispatches (thing_dispatch_report): 40, i.e. class 2 tree (43ba0); class 3
AI wizard s1 (11de0), castle s4 (413a0), balloon s9 (42530); creatures archer s25, skeleton s55 / s56,
builder s74, trader s85; projectile s13 (45b60); effects s18 (24810), mana ball s41 (25980), ridge node
s52 (27710); switches s0 / s2; all 24 spell phase-0 handlers and spell phase 1 (472f0).

## Deviations from the original

- Guards where the original reads or divides unchecked: `thing_create` null in player_spawn (returns),
  division by descriptor clearance / spell cost / spell duration / tether unit of 0, Thing indices
  outside 1..999 read from P fields, quick-select key index outside 0..23.
- Hand slot 0xff ("no spell", the initial value): the original reads P+0x214+0xff*4, which is inside
  the *next* player's record (its unused camera log, zero). `spell_slot_raw` emulates that by reading
  the GameState at the same offset.
- Sound, music, mouse cursor, CD check, network (`net_exchange_frame`, `net_player_disconnect`) are
  one-line TODOs; `sound_request_49720` goes to `g_hook_sound_request` when installed. Language-file
  lines ("... is dead", "... has left") are not copied into the message slot (ticks are set).
- Recording (Config.flags & 2) is not ported: the port's GameState holds indices where the original
  holds pointers, so snapshots would not be interchangeable. The record bit is dropped like after a
  failed file creation. Quick save (cmd 10) is a TODO, quick load (cmd 0xb) calls demo_load_state(10000).
- Cheat 7 (kill all creatures, creature_kill_all_17ff0) is a TODO; cheats 1..6 are translated.
- P.start_tick comes from `g_timer_ticks` (DAT_0012eab4), 0 unless the platform layer advances it.
- Spell casting arms the spell Thing (`cast_ticks = duration`, hand flags) but the spell handlers are
  not ported, so nothing is cast and no mana is spent.

## Hooks

Declared here (null = skipped): `g_hook_castle_stamp_footprint(Thing *scratch)` (castle_stamp_footprint_26320,
level features), `g_hook_thing_set_castle_extents(Thing *, int level)` (thing_set_castle_extents_353f0: needs
the castle size table DAT_000adfb0), `g_hook_player_local_input()` (player_local_input_16660),
`g_hook_sound_request(thing, player, sound)`, `g_hook_demo_input_changed()` (input_changed_34090),
`g_hook_demo_textures_reload()` (texture_load_needed_4c1a0 after a state load).
Installed here: `g_hook_mana_totals_update`.
Functions other subsystems call (connect their hooks to these): `player_note_fire_distance` (effect_fire_update_23c20),
`player_note_ridge_distance` (27710), `player_set_combat_music_timer` (projectile_pick_target_45f00),
`player_take_damage` (balloon / castle), `player_set_palette_effect`, `player_spawn`, `player_cast_spell`,
`castle_set_level_stats`, `castle_spell_set_capacity`, `mana_add_to_owner`.

## Shared-file changes

Done (allowed): `mc_types.h` `PlayerRec` is typed: `index` (+7), `is_computer` (+9), `view_entry` (+0xe),
`log_count` (+0x10), `tick` is u32 (+0x12), `cheat` is u32 (+0x18), `PlayerMsg messages[8]` (+0x1c, replaces
`message` / `message_ticks`), `PosLogEntry log_template` (+0x23c) and `log[32]` (+0x24a), `unk44b`, and the P
block as `union { uint8_t p[]; PlayerBlock blk; }` (raw access keeps working). New structs `PlayerBlock`,
`PlayerMsg`, `PosLogEntry`, `PlayerThreat`, all offsets asserted.

Requested:
- `Config`: +9 is the u32 demo file handle (`pad9`; ENGINE.md says +0x12); **+0x5d + k (k = 1..15) is
  `(local tick / k) & 1`**, so `level_mana` (+0x5e) is wrong: it is the tick parity bit (the camera shake
  factor in player_log_position; +0x63 = k 6 gates the dying palette flash); +0x9d u32 network exchange time;
  +0xc0 u32 zeroed by mana_totals_update; +0x8e1a u32 zeroed when recording starts. player.h has
  `cfg_tick_bit(k)` meanwhile.
- `engine_load_snapshot`: also read the 0x12c2 bytes at map offset 0x60000 into `g_corner_tex_table` (or
  call `demo_load_state(0)` + `demo_load_terrain(0)`).
- Game loop: `game_tick_sim()` per tick, camera = `player_camera(g_state->local_player)`,
  `g_anim_tick = players[local].tick`; `demo_open(dir, n)` + `while (demo_step()) render();` for the movie.
- BRIEFING_round2.md / PORTING.md: the last block of map00000.dat is DAT_000b58b0, not 0x12dfb0.

## Corrections / additions for ENGINE.md and carpet_types.txt

- **Camera log** (PlayerRec+0x24a, 32 x 14 bytes): the reader is render_frame_1fab0, entry index
  PlayerRec+0xe; fields are {x, y, z, yaw, pitch, roll, zoom} = render_view's camera (z + 0x80). pitch =
  `tickbit1 * (knock / 16) + pitch_acc / 2 - knock / 8`, roll = P+0x147 (banking), zoom = PlayerRec+0x248
  (command 8 adds to it). Only entry [PlayerRec+0x10 - 1] is written; command 7 moves the view index
  (0..count-2) onto entries that are never written. It is not a network trail.
- **P+0x10 is the strafe speed** (applied at yaw + 0x200, +-0x50, step 0x10, decay 4), not a turn rate.
  Turning is P+0x147: `steer_dx = (2 * steer_x - P+0x147) / 4` per command, `P+0x147 += steer_dx`,
  `yaw += P+0x147 / 8` (max 31 units per tick); pitch the same through P+0x149, Thing.pitch = P+0x149 & 0x7ff.
- **Flight height**: descriptor +0xa (0x400 for MoveDesc 7) is the cruise height, +0xc (0x80) the
  minimum clearance. Positive pitch = down. With pitch input up and forward speed the flown pitch is
  `-ratio * pitch / 256`, ratio = clamp((z - ground - 0x400) * 1024 / 0x400, +-0x100), i.e. the climb
  fades out at the cruise height and reverses above it; diving is unrestricted; at speed 0 the wizard
  sinks 8 per tick while above cruise height. player_terrain_collide weights the two axis directions
  by (0x200 - angle difference) / 0x200 (linear in the angle, not a projection).
- Flight constants are data: 0x94384 step 0x10, 0x94388 / 0x9438c min / max -0x50 / 0x50 (0x9438c is
  also written to Thing.speed_base every tick), 0x94390 knock decay -4, 0x943a4..0x943b0 strafe step /
  min / max / decay.
- P+0x1e[8] u16: times this player killed player n's wizard. P+0x13e is an i32. P+0x304[24] is the
  quick-select key -> book slot table (0xff = free), not a "queue of newly picked slots"; P+0x34c[24]
  (by spell id) gets 0x20 when a spell is chosen through such a key. P+0x14b is an invulnerability
  timer (100 at spawn, 2 inside the own castle: damage slots are cleared while it runs). P+0x189 damage
  flash of player_take_damage. P+0x3ac / +0x3b0 are 0xff until a spell is chosen.
- P+0x20a / +0x20c / +0x20e are AI aggression / accuracy / reaction copied from the level player block
  +4 / +0xc / +8 (region A is right, the "castle position" of region C is wrong); only AI records get
  them. Level player block: +0x10[24] spells the player starts with, +0x74[24] spells allowed. A human
  in the campaign gets the allowed spells whose P+0x37c "found" flag survived players_init_records.
- PlayerRec: +7 u16 own index, +9 is_computer (set for every non-local player unless Config.flags &
  0x10), +0x12 u32 tick, +0x18 u32 (kept by players_clear_records; 0xae89e = 1,000,000 health / mana),
  +0x1c 8 x {char[0x40], u16 ticks, u16 arg} messages by sending player, +0x44b cleared per tick.
  players_init_records queues cmd 1 for all 8 players: the first tick spawns them.
- `chat_message_show_3bb50` should be `player_set_input_mode_3bb50`. Command 6 has no handler.
  Command 0xf = respawn request (no castle: status |= 0xc). Chat line "RATTY" sets Config.flags |= 0x8000.
- **Shield flag 0x4000 is cleared by the first hit it absorbs** (player_apply_hits, 0x40d2a): closes the
  round-4 region C open question. Damage slot 4 = rubber band attach, slot 3 = mana steal.
- player_dying_update: spell slots go back to spell ids (-1 = empty) so the respawn recreates the book;
  the dropped Things get state + 1 and life 200..289; respawn delay of an AI wizard =
  ((255 - reaction) / 8) * 32 + 32 ticks; `active_top` is decremented.
- game_check_level_won divides by Config+0xbc (total_mana), not +0x5e, and is skipped when
  Config.flags & 0x110.
- demo_relink_state_pointers_3dc10 only fixes the players' own Things and the descriptor pointers; the
  other pointers of a saved state are valid only because the state block is loaded at the same address.

## Not done / open

- `thing_query.*` (spiral_search_*, thing_find_collision_105f0 .., thing_add_pending_damage_117c0): nothing
  in the translated player code calls them, so they were left for the projectile / spell port.
- AI wizard (0x11de0), castle and balloon handlers (0x413a0..0x42530), spells.
- Open: what P+0x15f (2000 countdown) and P+0x210 gate; who reads P+0x14d[8]; the identity of the
  0xae89e record.
