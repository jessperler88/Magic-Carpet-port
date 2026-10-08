# Port round 4 briefing (2026-10-07): AI wizard, remaining creatures, HUD, sound, per-tick reference

Read `docs/port/PORTING.md` first (sources of truth, code conventions, report format), then
`docs/port/BRIEFING_round3.md` (rules, verification ladder, the spatial / sim core) - the rules of
round 3 apply unchanged. This file lists what is new and the six tasks. The engine is described in
`docs/ENGINE.md`; the per-subsystem reports of rounds 2-3 are `docs/analysis/port_*.md` and show the
expected depth of a report.

## State at the start of round 4

- The simulation runs: `sim_test` (`tests/sim_test.cpp`, `${MC_SIM_ALL}`) generates level 38, runs the
  412 ticks before the engine's own snapshot (`movie/gam00000.dat` + `map00000.dat`) and compares slot
  by slot: 347 of 449 Things identical (scenery 150/150, creatures 128/169, effects 51/74, switches 5/5,
  spells 13/39, projectiles 0/4, players 0/8). It then plays the shipped movie (8551 ticks) from the
  snapshot and runs nine campaign levels for 3000 ticks. `thing_dispatch_report` lists what is still
  dispatched without a port - as of today exactly: the AI wizard `0x11de0` (class 3 state 1) and the
  creature handlers crab 0x1a830, kraken 0x1afb0, troll 0x1b410, griffon 0x1b560, emu 0x1c8f0,
  type 15 0x1ea60, wyvern 0x1f210 (and genie, which no tested level spawns).
- The snapshot's three AI wizards (players 1..3, `PlayerRec.is_computer == 1`) have acted for 412 ticks
  in the original but do nothing in the port: that is the most likely cause of most of the remaining
  102 differing slots (creatures of the horde around the AI castles, effects, spells). It is a guess,
  not a proof - task A will tell.
- Placeholders exist for every round-4 subsystem and are already registered / in CMake:
  `ai_wizard.h/.cpp`, `creatures2.h/.cpp`, `creatures3.h/.cpp`, `sound.h/.cpp` (all in
  `sim_register_gameplay()` and `${MC_SIM_ALL}`), `hud.h/.cpp` + `ui_draw.h/.cpp` (renderer side, part
  of the `mcengine` library through the glob; nothing calls them yet). The owner replaces the bodies and
  may rewrite the header.
- `thing.h` already declares `g_hook_ai_record_threat` (called from `thing_update_all` for every player,
  see thing.cpp:350), `g_hook_sound_request` / `g_hook_sound_fade` (every game call site already calls
  `sound_request(thing, player, sound)` / `sound_fade(...)`).
- `player.cpp` already does the AI-specific parts of record init and spawn (`is_computer`,
  `ai_allowed` from the level's player blocks, `ai_aggression` / `ai_accuracy` / `ai_reaction`,
  `ai_mode = 0`, the 0x601f / 0x9fdf threat seeds, the respawn delay from `ai_reaction`), the flyer
  (class 3 type 0, states 0 / 2 / 3), `player_cast_spell`, `player_apply_hits`, `player_camera`.
  `spells.cpp` handles `ai_want_spell` countdowns on the spell side (spells.cpp:394). Read these before
  translating anything that touches a PlayerRec / PlayerBlock (`mc_types.h` names most fields).
- `tools/analysis/mc.py body|tree|callers|strs <addr>` and `tools/analysis/img.py dis|dwords|bytes` are
  the sources (run from `tools/analysis`, never from `C:\Magic Carpet` itself).

## Rules (unchanged from round 3, repeated because they bit)

- Six agents work in the same tree at the same time (no git). **Only create / modify the files your task
  names.** Everything else is read-only; a wanted change to a shared file goes into your report (work
  around it locally with a `static` helper or a raw-offset accessor). Use Edit, not a whole-file Write,
  on a file you did not create; your own placeholder files you may overwrite.
- Functions outside your range: small and nobody's -> `static` helper in your file, listed under "extra
  functions"; another task's -> null-default hook declared in **your** header, listed under "hooks";
  not ported and not yours -> keep the game-state side effects, `// TODO(port): name(args)`, listed.
- Game code passes all arguments on the stack (read the pushes before each call); keep integer widths,
  signedness, division flavour and RNG draw order exactly (`t->rng = mc_lcg(t->rng)`; the global RNG
  helpers are in `mc_math.h`).
- Build in **your own build directory** (X = your letter): once
  `cd "C:/Magic Carpet/src" && cmake --preset msvc-x64 -B ../build_X`, then
  `cmake --build ../build_X --config Debug --target <name>_test` and run
  `../build_X/Debug/<name>_test.exe "C:/Magic Carpet/MagicCarpet/magic"`. Build only your own test
  target (the `mcengine` library, `engine_test` and `mcport` contain the other agents' half-written
  files). Create the sources before the `.cmake`, re-run the configure line after adding it, retry when
  configure trips over another agent's missing file. Zero warnings (/W4).
- Scratch: use **your own subdirectory** of the session scratchpad
  (`<scratchpad>/round4_X/`); agents overwrote each other's scripts in round 3.
- Bash tool pitfalls: `\n` inside heredoc'd python / C strings becomes a real newline and long heredocs
  with quotes fail - write sources and scripts with Write / Edit and run scripts from files. Never run
  python with `C:\Magic Carpet` as the working directory (the `ghidra/` folder shadows a package).
- The original game files under `MagicCarpet/` are never modified. Anything you need to change (task E)
  is done on a copy under `extracted/` or your scratch directory.

## Verification ladder (as in round 3)

1. Translate from the disassembly function by function, keep the structure recognisable.
2. Unit-test the pure pieces by construction (hand-computed from the disassembly).
3. The snapshot is 413 ticks of real play: every field value of your objects must be producible, and
   `sim_test`'s replay (`test_replay_to_snapshot` in `tests/sim_test.cpp`, which sets
   `g_wizard_castle_capacity_shift = 2` for the recording build) is the integration check: copy its
   structure into your own test with your handlers registered and report the slot counts before / after.
4. Smoke: 2000+ ticks on several levels, `check_pool`, `thing_dispatch_report`, statistics a reader can
   judge. Say honestly which of these each function reached.

## Deliverable

Your sources, your unit test (exit 0, zero warnings), and `docs/analysis/port_<name>.md` in the
PORTING.md format: functions translated (name + address), verification (what, how, numbers), deviations
/ gaps, hooks declared or installed, extra functions, TODO(port) call sites, requested shared-file
changes, corrections to `docs/ENGINE.md` / `mc_types.h` / `carpet_types.txt` / the dispatch-table
names. Your final message is a short summary of that report.

## Tasks

### A - AI wizard (`ai_wizard.h`, `ai_wizard.cpp`, `tables/ai_wizard.tables`, `tests/ai_wizard_test.*`, report `port_ai_wizard.md`, build dir `build_A`)

Class 3 type 1, state 1: everything from `player_type1_s1_update_11de0` to `ai_cache_human_wizard_15540`
(60 functions, 13.6 KB; `python mc.py tree 0x11de0 3`): `player_ai_wizard_tick_11f20` (housekeeping:
damage slots, regen, counters, threat drift), the mode handlers (12470 upgrade castle, 12560 fly to
castle site, 12600 approach target, 12680 idle, 126f0 return home, 12830 collect mana, 12950 attack
castle, 12a90 attack wizard / type-3 / creature, 12550 ret 0 for modes 2 / 5), `ai_choose_goal_12330`
with the goal functions 12bd0..13a20 and the 13 mode setters, movement (`ai_wizard_move_13b10`,
`ai_approach_target_140d0`, `ai_set_dodge_steer_15420`), the finders (13ce0, 13ec0, 13fa0, 14010,
`thing_signature_14080`, `ai_target_valid_140a0`), the spell side (`ai_get_spell_thing_13ac0`,
`ai_cast_spell_14240` incl. case 0x10 = castle spell creating class 3 type 2, `ai_spell_ready_14640`,
`ai_castle_spell_ready_14980`, 14aa0, 14ad0, `ai_spawn_spells_14b00`, 14c40,
`ai_choose_attack_spell_14c70`, `ai_choose_castle_attack_spell_14f00`, `ai_has_any_attack_spell_154e0`)
and the threat tracking (`ai_record_threat_from_projectiles_150f0` -> install `g_hook_ai_record_threat`;
`ai_find_incoming_projectile_153b0`, `ai_counter_projectile_15460`). ENGINE.md 2.1 "Computer-controlled
wizard AI" (field table) and agent2_A.md / agent4_A.md are the notes; `DAT_000938c4` (24 x u16 spell
cooldown reloads) and any other data table go through `tables/ai_wizard.tables`.
The AI casts through the ported spell / projectile / castle code (`thing_create`, `player_cast_spell`
in player.h, spells.h, projectiles.h, castle.h - read them: do not re-translate what they export; where
the AI calls a function another subsystem already ports, call that port). Ground truth: the snapshot's
players 1..3 (whole PlayerRec incl. the P block: `ai_mode`, threat table, cooldowns, spell slots,
position log) and their castles / spells / effects after 412 ticks; report `sim_test`-style slot counts
before and after your handler is registered (copy `test_replay_to_snapshot`), and which PlayerBlock
fields of the AI players match the snapshot. Also play the movie from the snapshot (`demo_open` /
`demo_step` as in sim_test) with the AI active: 8551 ticks without a crash, the AI wizards move and cast.

### B - creatures crab, kraken, troll, griffon (`creatures2.h`, `creatures2.cpp`, `tests/creatures2_test.*`, report `port_creatures2.md`, build dir `build_B`)

Class 5, states 30..53, code 0x1a830..0x1bb00 against `creatures.h` (the shared bodies and all attack
callbacks are ported: `creature_ai_step`, `creature_attack_target`, `creature_follow_leader`,
`creature_die`, `creature_apply_damage`, `creature_wander_turn`, ...; read `creatures.cpp` for how the
nine ported types are written and `port_creatures.md` for the state layout `type * 6 + {0 own, 1 main,
2 attack, 3 follow, 4 dying, 5 dead}`). Crab 0x1a830..0x1afa0 (states 31..35, helper 1aef0; state 30 is
already in creatures.cpp), kraken 0x1afb0..0x1b400 (36..41), troll 0x1b410..0x1b550 (42..47), griffon
0x1b560..0x1baf0 (48..53). Levels that spawn them (from sim_test): kraken on the movie level and 44,
troll on 24, griffon on 12, crab on 44. Verification: construction tests, then the movie from the
snapshot (kraken) and levels 12 / 24 / 44 for 3000+ ticks with `thing_dispatch_report` empty for your
states and `check_pool` clean. Note in the report which dispatch-table names are off by one type
(`gen/dispatch_tables.h`), as port_creatures.md did.

### C - creatures emu, genie, type 15, wyvern (`creatures3.h`, `creatures3.cpp`, `tests/creatures3_test.*`, report `port_creatures3.md`, build dir `build_C`)

Class 5 against `creatures.h` as in task B. Emu 0x1c8f0..0x1c940 (states 61..65; 60 is ported), genie
0x1c950..0x1d310 (66..71, incl. 1d220 / 1d270 / 1d310; **not** 1d420 / 1d4b0 / 1d540, which are the
builder's and ported), type 15 0x1ea60..0x1f200 (91..95, incl. 1ef10 / 1ef50 / 1ef80 and the four u16
weights at 0x1ea40 -> `tables/creatures3.tables`), wyvern 0x1f210..0x1f680 (97..101) and the disabled
records 0x1f690..0x1f6b0. Levels: emu and type 15 on 44, wyvern on 24 / 44 and the movie level, type 15
on the movie level; find a genie level with a scan over `sim_load_level(0..69)` and the THING_INIT types.
Verification as in B.

### D - HUD and frame composition (`hud.h`, `hud.cpp`, `ui_draw.h`, `ui_draw.cpp`, `tables/hud.tables`, `tests/hud_test.*`, report `port_hud.md`, build dir `build_D`)

`render_frame_1fab0` (6.2 KB) and what it calls that is not ported: `ui_draw_radar_43610`,
`ui_draw_radar_blips_42a20`, `ui_draw_map_43910`, `ui_draw_status_bars_219f0`, `ui_draw_player_list_21370`,
`ui_draw_thing_label_22870`, `ui_draw_spell_panel_icon_22d80`, `ui_draw_spell_icon_22820`,
`ui_fill_bar_212f0`, the message slots (`PlayerRec.messages`, `PlayerMsg` in mc_types.h), the spell
book screen (`PlayerRec.input_mode == 2`: 24 icons, selection by `input_book_update_selection` in
input.cpp, `Config.spell_slot` and the book cell size are written by **you** - see port_input.md
"requested changes"), `ui_draw_debug_overlay_4ad80` (the F-key debug text; cheap and useful) and
`debug_screenshot_3ca00` (write a PPM/PNG instead of the "mhwanh" format; optional). `ui_draw.cpp`: the
2D layer - `ui_draw_text_4a9a0` / `ui_draw_text_background_4aa90` / `ui_text_width_4ab60` /
`ui_set_font_4abe0` and the font engine 0x58290..0x58f30 (`ui_font_init_589d0`, `ui_draw_glyph_58ba0`,
`ui_draw_text_58ab0`, clip rects 588b0..58950, `ui_font_relocate_glyphs_58f30` - in the port a glyph
table is just the .tab of `data/font0..2.dat`, same 6-byte format as the sprites, `mcdata/sprite.h`
loads it), the icon blitters 0x4d096..0x4d240, the rect fills / blends / outlines 0x224e0..0x233c0,
`ui_copy_block_23140`, `ui_draw_sprite_60688`, `vga_draw_box_603f0`, `vga_draw_rect_outline_640_604c0`,
`gfx_fill_rows_320/640` (4ce83 / 4cea9), `text_split_lines_3ed30`. ENGINE.md: "Renderer 2.1 Frame flow",
2.2 (video globals: `DAT_0012edae` bit0 = 320x200 - UI coordinates are in 640x400 space and halved;
in the port `g_video_mode_flags`, default 8), 2.7 (circle profile 0xcdcb0 and per-texture colour 0xcd9b0
for the radar: `tables.h` may already export them - check), the text-rendering paragraph in "Front
end" (font struct, control bytes), `render.h` (`FrameBuffer`, `render_set_view_window`, `render_view`,
`g_rcam`), `raster.h` (render target, clip), `sprites.h` (`g_sprite_blit` / `render_sprite_scaled` for
icons drawn through the sprite blitter, `sprite_ensure_loaded`), `player.h` (`player_camera`). HUD
icons live in `data/hspr0-0.dat/.tab` and `mspr0-0` (mcdata/sprite.h), the book background in
`data/bookbkg.dat` + `book.pal`, pointers in `data/pointers.dat` - find out which the code reads and
load them in `ui_draw_init`. Export `render_frame(const FrameBuffer &, int player)` from `hud.h` (the
integrator switches `mcport` from `render_set_view_window` + `render_view` to it; do not edit mcport).
Keep the layout math identical to the original (pixel comparison against DOSBox screenshots later);
no floating point. Verification: `hud_test` renders a frame from the snapshot (`engine_init`,
`engine_load_snapshot`, `render_frame`) to `build_D/Debug/hud_test_frame.ppm` in 640x480 and 320x200
and asserts on what you can (non-empty HUD regions, text width of known strings, radar centre colour,
no out-of-bounds writes - the render target clips, your 2D code must too); look at the PPM yourself
(Read tool renders images). Not yours: the front end (`fe_*`), `movie_*` subtitles, network chat.

### E - per-tick reference from the original (`tools/reference/*`, `docs/analysis/port_reference.md`, build dir `build_E` if you build anything, scratch `round4_E`)

Goal: a per-tick dump of the original game's state while it plays the shipped movie, so the port can be
diffed tick by tick (today only one snapshot exists). The package ships DOSBox
(`MagicCarpet/DOSBOX/DOSBox.exe` + `dosbox.conf`, launched by `MagicCarpet.bat`); the game plays movie N
and skips the front end with the command line `carpet movie 0` (config_parse_33750, ENGINE.md "Demo /
movie system", "Command line"); `demo_save_state_3c2c0(n)` writes `movie/gam%05d.dat` (RNC-compressed
GameState, the format `sim_load_snapshot` reads) and `demo_save_terrain_3c430` the map dump; the quick
save command (player.cpp:1041) calls it with 10000 but is disabled during playback (`Config.flags &
0x10`). Options, in order of preference - investigate, pick, and document why:
(1) a patched **copy** of carpet.exe (under `extracted/refgame/`, with copies of the data it needs -
never touch `MagicCarpet/`) that calls `demo_save_state` with an increasing number every N ticks of
playback (or writes the raw 0x38d03-byte GameState + the per-tick RNG / tick counter to one growing
file through the game's own file routines - `demo_record_playback_step_3c540` already has an open file
handle); the LE format is documented in `docs/FORMATS.md` and `tools/mctools/lefile.py` parses it
(code at 0x10000, data at 0x90000, fixups), the padding runs and dead code of the image give you a code
cave (e.g. `dead_castle_site_search_11cef`), and position-independent code (`call $+5; pop`) avoids
new fixups; (2) DOSBox-X's debugger (breakpoint at the end of `game_tick_update_32e80`, `MEMDUMPBIN`)
if it can be driven without a human; (3) a DOSBox build with a two-line patch. Whatever you build,
`tools/reference/` holds the scripts (python, run with `-I` on anything downloaded), and the
deliverable is `extracted/reference/movie0/tick%05d.gam` (or one file with an index) for at least the
first 600 ticks of movie 0 after the snapshot, plus a `compare_reference.py` that loads the port's
`sim_test`-style replay output (or a small new `tests/reference_test.cpp` + `.cmake` you may add, built
with `${MC_SIM_ALL}`) and reports the first tick and the first field where each Thing diverges. Report:
what worked, what did not, how long a run takes, how to regenerate.

### F - sound (`sound.h`, `sound.cpp`, `tables/sound.tables`, `tests/sound_test.*`, `mcdata/sndbank.h/.c`, report `port_sound.md`, build dir `build_F`)

The game-side sound manager, not a driver: `sound_request_49720` (install `g_hook_sound_request`; 32
channels, priorities, positional volume / pan from the listener's thing, repeats), `sound_update_494b0`
(per tick, called from the tick after the simulation: player.cpp:1207), `sound_fade_player_sound_49c40`
(`g_hook_sound_fade`), `sound_priority_ok_49c20`, `sound_playing_count_49d50`, `sound_sample_done_49d80`,
the fade-in / fade-out set 0x4dfc0..0x4e400, `sound_restart_sample_4f6f0` / `sound_play_if_idle_4f7a0` /
`sound_play_simple_4f850` / `sound_start_sample_4f8a0`, the sample layer `sound_play_sample_5cbb0` /
`sound_play_sample_loud_5cd60` / `sound_stop_sample_5cea0` / `sound_stop_all_5c040` /
`sound_set_sample_volume_627d0` / `sound_count_playing_628a4`, the bank loader `sound_load_bank_5c990`
(+ `sound_bank_relocate_5ca58`: `data/snds<set>-<n>.dat/.tab`, which set the level selects - find the
selector) and the music plumbing `music_update_1f800` / `music_stop_1f960` / `music_play_track_5c0a0` /
`music_fade_*` 49ca0..49d10 / `music_song_done_5cf40` as far as it is game logic (track choice, mood,
fades; the HMI MIDI playback itself is **not** this round: expose `music_play_track(track)` etc. through
a platform hook). ENGINE.md "Sound (HMI SOS, 32 digital channels)", "HMI sound drivers" (agent4 notes),
`input.cpp` (which already routes sound / music toggles to `platform(INPUT_REQ_*)` - read its enum and
hook so you fit it). Design: everything below the HMI API becomes a small platform interface declared
in `sound.h` (`struct SoundBackend { start(sample ptr, len, rate, volume, pan, loop) -> handle; stop;
set_volume; is_playing }`, null-default; `mcport` will implement it with SDL audio later - do not edit
mcport). `mcdata/sndbank.c`: parse the sample bank format (find the header layout from the loader and
from `hmi_digi_*` call sites: rate, bits, loop points) and offer `mc_sndbank_load(game_dir, set, n)` +
sample lookup; write the first few samples as .wav to your scratch dir and listen-check by size / rate
plausibility. Verification: construction tests on the channel allocation / priority / volume math,
`sound_test` on level 38 + the movie with a recording backend that logs (tick, sound id, thing, volume,
pan) - report the counts and the first 20 events.
