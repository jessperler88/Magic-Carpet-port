# Naming debt, typed tables and the round-5 ENGINE.md merge (task E, port round 6, 2026-10-07)

Ghidra names, types and labels, the export, `docs/ENGINE.md`, `docs/FORMATS.md` and `docs/analysis/README.md`.
Nothing in `src/` was touched. Only this task ran Ghidra this round; the project was backed up to scratch first,
the export was written to a scratch directory and copied over `ghidra/export/` in one step at the end.

## Result

- **9 functions renamed, 4 functions created** (`snd_midi_song_timer_cb_66898` and three HMI lock markers).
  carpet.exe now has **1,690 functions**, all decompiled (`game_main_32a00` through the ExportAll fallback, as
  before: decompiled=1690 failed=0 via-fallback=1). The rename table below is the diff of the old and new
  `ghidra/export/carpet_functions.csv`: exactly these 13 rows changed, no other function changed its name.
- **19 comments corrected / extended** in `carpet_names.csv` (level restart, cue ops, FLI chain, rivers, win check,
  front-end marker file, game_main / game_tick shape, colour cube, radar blips, level-load resets, music, spiral init,
  ...).
- **Phase-1 leftover tables typed**: spiral ring tables 0xade28 (+ the iterator slots 0xac160), level-name pointers
  0x97490, texture directory 0xb84f4 (+ the four sprite-cache arrays), the 32-entry camera log at PlayerRec+0x24a
  (PlayerRec header synced with `mc_types.h`). Also typed: the resource lists 0x96df0..0x97420 and the tab relocation
  list 0x9744c (this answered a round-4 open question), the HMI event-length tables and the music track map.
- **73 new data labels** (FLI, front end, game flow, sound flags, resource destinations) and Config / GameState fields
  (`frame_time` / `net_time` u32 at +0x99 / +0x9d, `tick_bits`, `demo_file`, `level_music_track`, `start_pos`, ...).
- `docs/ENGINE.md`: new closing section "Port round 5 corrections (merged in round 6)" (merges all six round-5
  "Corrections" sections plus this round's typing work and an open-question table); **25 superseded / open
  statements marked in place** ("*Corrected (round 6 merge)*" / "*Answered (round 6)*"); a precedence note at the top.
  2,500 -> 2,796 lines.
- `docs/FORMATS.md`: new sections "HMI MIDI songs" (the HMP layout) and "FLI movies"; corrected the `intro/*.dat`,
  `screens/*.dat` and `search.dat` rows.
- `docs/analysis/README.md`: the round 4, 5 and 6 report tables.
- `ghidra/scripts/ExportAll.java`: the strings export now looks inside structures (so the file names of the newly
  typed resource lists stay in `carpet_strings.csv`) and no longer lists u8 / i8 tables as empty "strings".

## Integrator commands

Nothing has to be regenerated. None of the renamed functions is a Thing handler, so
`src/mcengine/gen/dispatch_tables.h` does not change; running the generator is a no-op (optional check):

```
cd "C:/Magic Carpet/tools/port" && python gen_dispatch.py
```

(It reads `ghidra/export/carpet_functions.csv`; run it from `tools/port`, never with `C:\Magic Carpet` as the
working directory.) `carpet_names_tables.csv` is unchanged (`tools/analysis/gennames.py` not needed).
Port comments in `src/` that cite the old names (`fli.cpp/.h`, `frontend.cpp`, `game.cpp/.h`, `level_features.h`,
`fli_test.cpp`, `game_test.cpp`) still identify the functions through the address suffix; renaming them is optional.

To reproduce the Ghidra state from the name files (PowerShell; the chain that was run):

```
$GH = "C:\tools\ghidra_12.0.4_PUBLIC\support\analyzeHeadless.bat"; $N = "C:\Magic Carpet\ghidra\names"
& $GH "C:\Magic Carpet\ghidra\project" MC1 -process carpet.exe -noanalysis -scriptPath "C:\Magic Carpet\ghidra\scripts" `
  -postScript RepairFunctions.java "$N\repair_functions_9.txt" -postScript FixConventions.java `
  -postScript ApplyTypes.java "$N\carpet_types.txt" -postScript ApplyNames.java "$N\carpet_names_tables.csv" `
  -postScript ApplyNames.java "$N\carpet_names.csv" -postScript ApplyNames.java "$N\carpet_labels.csv" labels `
  -postScript ExportAll.java <out dir>
```

Results: RepairFunctions created=4 failed=0; FixConventions 4 of 1690; ApplyTypes structs=24 fields=274 globals=51
labels=78 (no errors); ApplyNames 435 / 1552 / 47 applied, missing 0; ExportAll decompiled=1690 failed=0
via-fallback=1. (The final export was a second ExportAll run after the strings fix, on the same database.)

## Requested shared-file changes (integrator)

1. `README.md`, "Where things stand": "carpet.exe: 1,686 functions" -> "1,690 functions"; in the Ghidra paragraph
   after "Round-4 repairs: ..." add: "Round 6: `repair_functions_9.txt` (the HMI song timer callback 0x66898 and its
   lock markers); `carpet_types.txt` now also types the resource lists, the spiral ring tables, the camera log and the
   sprite cache, and labels the FLI / front-end / sound globals (`docs/analysis/names_round6.md`)."
2. `docs/ROADMAP.md`, Phase 1 "Leftovers for later": tick it -
   `- [x] Leftovers (round 6, docs/analysis/names_round6.md): padding "flow targets" resolved; spiral ring tables
   (0xade28), level names (0x97490), texture directory (0xb84f4) and the PlayerRec+0x24a camera log typed; open
   questions answered where the binary allows (table at the end of docs/ENGINE.md).`
   Phase 2: tick the round-6 documentation item if it is listed ("ENGINE.md corrections of the round-5 reports").
3. Optional, `src/` comments: the old names listed in "Integrator commands" above.

## What was merged into ENGINE.md / FORMATS.md

From each round-5 report's "Corrections" section (all items; ENGINE.md "Port round 5 corrections (merged in round
6)" holds them, the in-place markers point there):

| report | merged |
|---|---|
| `port_game.md` | game_main loop shape (full pseudo code), status values 2 / 8 / 4 and the restart path, `level_restart_3d4e0`, PlayerRec.quit semantics, campaign flow and progress block (GameState+0x3bd6 = spell_found), level skip only on the front-end path, sound / music reset before every front-end visit, the front end always in 320x200, title_flag_a, level_load_file_3d160 resets, video_toggle_resolution state, Config+0x99 / +0x9d u32, pacing (none; vsync only in fades; F3 = Thing-update multiplier) |
| `port_frontend.md` | marker file intro.pld vs language.inf, DAT_0009e500 never set, Esc / Enter key indices swapped, menu item 2 = name / call-name dialog, new game clears spell_found, result-screen stats fields, fli_play arguments and DAT_0009e468, sprite window, ui_font_init palette argument, SS2 decoder bug, attract demo level |
| `port_fli.md` | FLI call chain (file player vs memory-stream player, decoded chunk types), per-file arguments and cue scripts, Bullfrog 12-byte header, frames shown, pacing (120 Hz HMI tick vs 119.06 Hz int 8), cue opcode table (the export's K/L comment was wrong), subtitles, placeholder movies, fli_file_play dead (no reference at all) and the dead preload, palette fade arithmetic, effects list with the blue-for-red bug, title_screen_show, the FLI globals table |
| `port_audio.md` | bank suffixes (confirmed; already merged in round 5), HMP layout (FORMATS.md), the timer callback as a function, FF 51 ignored, no loop, song-end reset, channel mapping off, master / layer volume paths, the HMI tables |
| `port_reference2.md` | rivers: counter **per river** (this supersedes the round-5 merge's "shared by all rivers"), no level-start randomness, game_check_level_won gate = flags & 0x110, Config.credits_state, demo_relink needs player 0's Thing, `-level 17` crash, hit_flash record / play difference |
| `port_render_reference.md` | colour cube `c*4+3` (black = entry 12), radar blip colours, SIRDS uses g_rng16, g_rng16 at a movie start (0x2fea for level 38), option bytes during playback, sprite animation table, frame N drawn from the state before tick N, the measured fade-in / red-flash numbers |

FORMATS.md: the HMP layout (header table, reversed delta, no running status, event-length table, timing, song end,
events used), FLI movies (12-byte header, frames shown, chunk types, census, placeholders, globe / scroll / timer.dat
only playable by the memory-stream player), corrected rows for `intro/*.dat`, `screens/*.dat` (three of them are
FLIs) and `search.dat` (6-byte ring records). The `inst.bnk` / `drum.bnk` layout is left to task A's report
(`port_opl.md`), as the briefing says.

In-place markers (25): the startup sketch, the player status word, the PIT-tick paragraph of "Interrupt handlers"
(the game is not paced by it), main-menu item 2, the level-name table size, the FLI player paragraph and both items of
"Two FLI players", the swapped Esc / Enter key indices, DAT_0009e500, the HMI device ids, the credits ticker, the
spiral record size, the round-5 merge's colour cube and river counter, and the answered open questions below.

## Open questions answered (from the binary)

| question (ENGINE.md section) | answer |
|---|---|
| data table 0x96f6c / 0x96f98 holding 0xadf8c / 0xadf7c (round 4 D) | the `dest` fields of the `*PalData` / `*PalMem` records (0x400 bytes each) of the 0x2c-byte resource list at 0x96df0, loaded by `file_load_resource_list_5ae80` (was `mem_alloc_named_5ae80`: it walks the list, frees, then loads / allocates per record; "ERROR: Allocation %s.") from data_load_all_334c0 / video_alloc_buffers_33480. All lists are typed now. |
| the 0x5678 marker of the EMU8000 block 0xa4eb8 (round 4 D) | no reader: the only reference to 0xa4eb8 in the image (dword scan) is `mov eax, 0xa4eb8` at 0x704b6 in `snd_awe32_get_hw_block_704b6`, itself unreferenced. |
| 0x66898 is a 1.2 KB sosMIDI routine without a function (round 3 H) | the song timer callback (the only reference is the push at 0x5ead3 in snd_midi_start_song_5eab0), created as `snd_midi_song_timer_cb_66898`; its lock region is bracketed by the empty markers 0x66887 / 0x66dc0 (refs 0x5f1da / 0x5f1df ...), and 0x66dd1 starts the region of hmi_midi_slot_is_free_66de2 (ends at 0x66e15). The other driver entries listed in that question are still not functions. |
| who reads PlayerRec+0x24a (round 4 B) | the camera log, read by render_frame_1fab0 (round 5); now typed `PosLogEntry log[32]`. |
| why levels 8, 17, 28, 33, 39 are skipped (round 4 B) | partly: the retail exe started with `-level 17` crashes at tick 1-2 (divide by zero in ui_draw_status_bars_219f0, a Thing with max_health 0; port_reference2.md), so at least that level was unfinished; for the others the code shows only the skip. |
| ring record size of DAT_000ade28 (round 2 A said 12 bytes) | **6 bytes** {SpiralCell *cells, u16 count} (`imul esi, esi, 6` at 0x10203); the ring file is data/search.dat (string 0x90004). |
| HMI device-id constants (round 2 D) | 0xa001 MPU-401, 0xa002 OPL2, 0xa004 MT-32, 0xa008 AWE32 (the music bank records name *.GEN / *.HMP / *.ROL per device; port_audio.md). |
| what main-menu item 2 edits (round 2 D) | the player name (Config+0x1d) and call-name (Config+0x3d) (port_frontend.md). |
| 0x57580 / 0x579c0 not functions (front end) | both exist (`fe_menu_text_dialog_57580`, `fe_fli_composite_bg_579c0`). |
| level names after 57 (front end said 51.. = seven multiplayer maps) | the table has 101 entries: 51..70 are twenty multiplayer names ("Bussorah".."Comari"), 71..100 all point at "0". |

Still open (no evidence in the binary): the VIP register values 303h = 1 / 0x10 and 302h = 2; whether the dead
`castle_near_thing_11820` was an early castle-site rule; what height 8 means for the dead ring scan 24d70.

## Rename table (old -> new, from the new export)

| address | old name | new name | evidence |
|---|---|---|---|
| 0x236d0 | movie_buffer_clear_236d0 | movie_subtitle_clear_236d0 | clears the subtitle strip (DAT_000b2e04, 0x4b00) when DAT_0009390a; cue op K (port_fli.md) |
| 0x23700 | movie_free_23700 | movie_subtitle_free_23700 | cue op P: clears the strip, frees the sfont1 list, resets 938fd / 9390a / 9390c |
| 0x3d4e0 | level_finish_3d4e0 | level_restart_3d4e0 | port_game.md: called when (status & 6) == 4, regenerates the level in place |
| 0x50370 | stereo_page_blank_50370 | vfx1_vip_stereo_enter_50370 | agent4_D.md (VFX1 VIP card, symmetric to vfx1_vip_stereo_leave_503d0) |
| 0x50430 | movie_wait_frame_50430 | fli_wait_frame_50430 | port_fli.md |
| 0x50520 | fli_error_unknown_frame_50520 | fli_read_frame_50520 | port_fli.md (reads the frame chunk; the error is its bad-magic path) |
| 0x50600 | movie_frame_present_50600 | fli_decode_present_frame_50600 | port_fli.md |
| 0x5ae80 | mem_alloc_named_5ae80 | file_load_resource_list_5ae80 | walks a 0-terminated ResourceRec list (ENGINE.md already called it this) |
| 0x5c56d | fli_file_skip_chunk_5c56d | fli_file_read_color256_5c56d | agent4_D.md; disassembly 0x5c56d: reads size - 6 bytes into [0xadf8c] |
| 0x66887 | (no function) | hmi_lock_marker_66887 | empty 17-byte marker, lock-region start of 0x66898 |
| 0x66898 | (no function) | snd_midi_song_timer_cb_66898 | port_audio.md; far, retf at 0x66dbf |
| 0x66dc0 | (no function) | hmi_lock_marker_66dc0 | lock-region end of 0x66898 |
| 0x66dd1 | (no function) | hmi_lock_marker_66dd1 | lock-region start of hmi_midi_slot_is_free_66de2 |

Comment-only corrections (carpet_names.csv, prefixed "Round 6:"): 0x17d80 cue_script_step (opcode table),
0x31430 terrain_carve_rivers (per-river counter), 0x3db20 game_check_level_won (flags & 0x110, 17th tick), 0x51ed0
fe_init_state (intro.pld), 0x508f0 fli_play, 0x50dfd / 0x50e88 (memory-stream player), 0x32a00 game_main, 0x32f90
game_tick, 0x334c0 data_load_all (cube +3), 0x42a20 radar blips, 0x3d160 level_load_file (resets), 0x5c0a0
music_play_track, 0x5ef56 snd_midi_set_volume, 0x101b0 spiral_search_init (6-byte records), 0x66876 lock marker,
0x5c264 fli_file_play (no reference at all), 0x3dc10 demo_relink, 0x33600 video_toggle_resolution.

Names not changed where a report only mentioned a function in passing: `timer_tick_isr_3411a` in port_fli.md is a
typo for `timer_tick_isr_34120` (0x3411a is padding); `video_restore_mode_2ff10` (input.h) is the port's name for
`stereo_mode_leave_2ff10`, which is correct; `fli_next_frame_50dfd` / `fli_decode_frame_50e88` in the old ENGINE.md
text are `flic_play_chunk_50dfd` / `flic_decode_frame_50e88` in Ghidra.

## Types and labels added (`ghidra/names/carpet_types.txt`)

New structs: `PlayerMsg` (0x44), `PosLogEntry` (0xe), `StartPos` (6), `SpiralCell` (4), `RingRec` (6), `SpiralIter`
(0x18), `TmapDirEntry` (0xa), `ResourceRec` (0x2c), `TabReloc` (0xc).

Changed structs:
- `PlayerRec`: + quit (+4), index (+7), is_computer (+9), view_entry (+0xe), log_count (+0x10), tick u16 -> u32, cheat
  u16 -> u32, `message char[0x40]` + `message_ticks` -> `messages PlayerMsg[8]`, log_template (+0x23c), `log
  PosLogEntry[32]` (+0x24a), unk44b, `p u8[0x3b2]` (+0x44f); status comment = values 2 / 8 / 4.
- `GameState`: + volcano_thing / volcano_smoke (+0x24 / +0x26), level_music_track (+0x240), flag244, start_pos
  StartPos[8] (+0x23d9).
- `Config`: + demo_file (+9), tick_bits u8[15] (+0x5e, was `level_mana`), session char[0x20] (was 0x21, overlapping
  +0x95), disk_activity (+0x95), frame_time u8 -> u32 (+0x99), net_time u32 (+0x9d); flags comment (0x10 network,
  0x20 recording blocked, 0x100 front end skipped); credits_state comment.
- `g_tick` comment: incremented by timer_tick_isr_34120 (HMI event, 120 Hz) with sound / music, else by the int 8
  handler (119.06 Hz).

New typed globals: `g_spiral_rings` 0xade28 RingRec[32], `g_spiral_iters` 0xac160 SpiralIter[100], `g_tmaps_dir`
0xb84f4 TmapDirEntry*, `g_sprite_group_stamp` 0xb7cb0 u32[529], `g_sprite_ptr` 0xb8d3c u8**[529], `g_sprite_locked`
0xb9580 u8[529], `g_sprite_group_priority` 0xb9791 u8[529], `g_level_names` 0x97490 char*[101], `g_reslist_data`
0x96df0 ResourceRec[14], `g_reslist_etext/ftext/gtext/itext` 0x97058 / 0x970b0 / 0x97108 / 0x97160 [2],
`g_reslist_block16` 0x971b8 [2], `g_reslist_block32` 0x97210 [3], `g_reslist_screen_lo` 0x97294 [5],
`g_reslist_screen_hi` 0x97370 [5], `g_tab_reloc_list` 0x9744c TabReloc[5], `g_hmi_event_len` 0x9fa0e u8[16],
`g_hmi_event_len_fx` 0x9fa1e u8[16], `g_music_track_map` 0x9e65c u32[32].

Labels (names follow the port's where it has one, with the address suffix so `DAT_xxx` comments still match):
- resource destinations: g_search_dat_adf60, g_search_dat_end_adf78, g_building_dat_adf98, g_building_tab_adfb0,
  g_building_tab_end_adfa4, g_font0_dat_adee8, g_font0_tab_adf28, g_font0_tab_end_adf08, g_font1_dat_adeec,
  g_font1_tab_adf2c, g_font1_tab_end_adf0c, g_pal_data_adf8c, g_pal_mem_adf7c, g_pointers_dat_adfb8,
  g_pointers_tab_adfc0, g_pointers_tab_end_adfac, g_palette_adf90, g_text_dat_adf80, g_texture_atlas_adf5c,
  g_sky_adf48, g_hud_spr_dat_adfb4, g_hud_spr_tab_adf94, g_hud_spr_tab_end_adf9c, g_back_buffer_12ed74;
- FLI: g_fli_apply_palette_9e44c, g_fli_abort_on_key_12eabe, g_fli_abort_on_input_change_9e44e,
  g_fli_aborted_12eabc, g_fli_frame_12eac0, g_fli_header_12eaa0, g_fli_frame_header_12e680, g_fli_palette_12e798,
  g_fli_chunk_name_12e698, g_fli_file_pos_9e450, g_cue_index_938f4, g_fli_frame_delay_9e704,
  g_cue_loop_track_12eaba, g_fli_subtitles_enabled_938fc, g_fli_subtitle_strip_938fd, g_fli_subtitle_init_9390a,
  g_fli_subtitle_text_9390c, g_subtitle_font_adfd0, g_subtitle_strip_b2e04, g_subtitle_strip_len_b2e0c,
  g_fli_path_9e708, g_fli_frame_callback_12ea9c, g_fli_mem_ptr_12eab0, g_fli_dest_9e444, g_fli_skip_zero_9e468;
- front end / game / sound: g_fe_leave_9e504, g_fe_reload_12ebdc, g_fe_game_in_progress_9e500, g_fe_session_12ed30,
  g_fe_lobby_players_12ed31, g_fe_network_9e3c8, g_fe_input_flags_9e583, g_fe_input_page_12ed33, g_fe_state_12ed2e,
  g_fe_slot_mode_12ed2d, g_fe_flags_12ed35, g_fe_attract_phase_12ed34, g_fe_attract_flags_12ed36,
  g_fe_demo_saved_level_12ed18, g_fe_demo_saved_spells_12ebc0, g_video_mode_12edae, g_sound_available_9e320,
  g_sound_on_9e321, g_music_available_9e30c, g_music_on_9e30d, g_music_track_9e312, g_music_track_count_9e316,
  g_music_device_12e06e, g_sound_quality_9e328.

Note for readers of the export: these globals now appear under the label names (e.g. `g_fli_frame_delay_9e704`
instead of `DAT_0009e704`); grep for the address suffix to find both.

## Files changed

| file | change |
|---|---|
| `ghidra/names/carpet_names.csv` | 28 rows edited in place (9 renames, 19 comments; each starts with "Round 6:"), 4 rows in a "Round 6" block at the end |
| `ghidra/names/carpet_types.txt` | PlayerRec / GameState / Config updates, 9 structs, 21 typed globals, 73 labels (Round 6 block at the end) |
| `ghidra/names/repair_functions_9.txt` | new: 0x66898, 0x66887, 0x66dc0, 0x66dd1 (the bytes were not disassembled, ApplyNames alone could not create them) |
| `ghidra/scripts/ExportAll.java` | strings export recurses into structures / struct arrays (default address space only) and skips u8 / i8 arrays |
| `ghidra/export/*` | fresh export (1,690 functions) |
| `docs/ENGINE.md`, `docs/FORMATS.md`, `docs/analysis/README.md` | see above |

`carpet_strings.csv` differences against the round-5 export: the 28 resource-list names now report the field length
(28) instead of the string length; the empty "strings" of byte tables (texture property tables 0x93930 / 0x939d4 /
0x93a78, the shade / blend tables, the four maps, the key table) are gone.

## Corrections to ENGINE.md / mc_types.h / carpet_types.txt / names

All applied in this round. For `mc_types.h` (not mine, nothing required): the port's structs already match what was
typed; `carpet_types.txt` now follows `mc_types.h` for PlayerRec's header, Config and the GameState fields above.
