# Analysis working notes (2026-10-06 agent pass)

Raw deliverables of the four parallel analysis passes over `ghidra/export/carpet_all.c`.
Their CSV blocks were merged into `ghidra/names/carpet_names.csv` by
`tools/analysis/merge_names.py` and their prose was appended to `docs/ENGINE.md`;
keep these as the evidence trail (they contain more per-function detail than ENGINE.md).

| File | Scope |
|---|---|
| `briefing.md` | The shared briefing the agents worked from (established facts, naming rules). |
| `agent_render.md` | Landscape/polygon renderer, sprites, VGA/VESA present, HUD drawing. |
| `agent_things.md` | Thing struct, allocation, dispatch tables, spatial index, spawning, handlers. |
| `agent_tick.md` | Palette effects, FLIC anims, input, sound (HMI), timer, NetBIOS, demo/movie. |
| `agent_fe_crt.md` | Front end state machine, config options, save format, Watcom CRT, DOS/DPMI, VGA. |
| `dispatch_tables_dump.txt` | Dump of the class/model handler tables at 0x943da (from `tools/analysis/classtab.py`). |

Names marked `UNCERTAIN:` in the CSV / `?` in these files were not read in depth.

## Round 2 and 3 (2026-10-06, after the function inventory cleanup)

| File | Scope |
|---|---|
| `briefing2.md` | Briefing for rounds 2/3 (tools, caveats about padding starts and fragments, deliverable format). |
| `todo2_A..E.txt`, `todo3_G..H.txt` | Per-region task lists handed to the agents (unnamed functions + uncertain names). |
| `agent2_A.md` | 0x10000-0x1f6b0: wizard AI (player type 1), creature shared states, damage slots. |
| `agent2_B.md` | 0x1f6b0-0x3c000: terrain generation pipeline, terrain painting, castle footprints, levels.dat, movie subtitles. |
| `agent2_C.md` | 0x3c000-0x4c000: castle state machine, projectiles/impact effects, spell phases, player sub-block. |
| `agent2_D.md` | 0x4c000-0x5e000: VFX1 headset driver protocol, sndsetup.inf, two FLI players, NetBIOS fixes. |
| `agent2_E.md` | 0x5e000-0x80000: four internal HMI MIDI drivers, graph.lib, CRT extras. |
| `agent3_G.md`, `agent3_H.md` | Functions that only existed after the cleanup (HMI thunks, dead code, small helpers). |

Their CSV blocks are merged with `tools/analysis/merge_names.py` (agents win over `UNCERTAIN` names,
`FRAGMENT`/`DATA` rows are skipped and the fragments deleted via `ghidra/names/delete_stubs_3.txt`).

## Round 4 (2026-10-06 evening, after the decompiler repairs)

| File | Scope |
|---|---|
| `briefing4.md` | Briefing for the UNCERTAIN-resolution round. |
| `todo4_A..D.txt` | The 47 functions whose curated name still carried `UNCERTAIN:`. |
| `agent4_A.md` | Wizard AI goals, creature helpers, terrain probes (9). |
| `agent4_B.md` | Effect handlers, dead terrain ring scan, mirrored sprite pass, level skip, CD check, player position log (11). |
| `agent4_C.md` | Castle helpers, scenery and spell Table A handlers, creature awake gate, combat-music timer (14). |
| `agent4_D.md` | VFX1 VIP ports, CPU detect, file FLI player, CRT stubs, AWE32 (ex-GUS) and OPL driver internals (13). |

All 47 resolved; `carpet_names.csv` has no `UNCERTAIN:` rows left. Merge rule added to `merge_names.py`: an agent that confirms an UNCERTAIN name replaces its comment.

## Port reports (Phase 2)

| File | Scope |
|---|---|
| `port_terrain.md`, `port_raster.md`, `port_render.md`, `port_tables.md` | Round 1: terrain generator, rasteriser, landscape renderer, tables / textures. |
| `port_thing.md` | Round 2 integrator notes: Thing core, dispatch tables, snapshot loading, struct corrections. |
| `port_features.md` | Level-start features, terrain painting, castle footprints; cell-by-cell comparison with map00000.dat. |
| `port_constructors.md` | All Table B constructors; field-by-field comparison with gam00000.dat. |
| `port_sprites.md` | Sprite cache, animated sprites, Thing renderer (render_cell_things, render_sprite_scaled). |
| `port_player.md` | Player records, command packets, flyer movement, movie format and playback. |

The round-2 briefing is `docs/port/BRIEFING_round2.md`.

## Port reports, round 3 (2026-10-06/07)

| File | Scope |
|---|---|
| `port_core3.md` | Integrator: spatial queries, area damage, castle site tests, sim glue. |
| `port_projectiles.md` | Class 9: flight, steering, impact, target selection (arrows verified against the snapshot). |
| `port_spells.md` | Class 12: pickups, cast helpers, all 24 spells (idle paths verified against the snapshot). |
| `port_effects.md` | Remaining class-10 handlers: explosions, mana balls, teleport, castle raising, spell effects. |
| `port_castle.md` | Castles, balloons, scenery, switches. |
| `port_creatures.md` | Creature shared code, wake timers, nine creature types; the `creatures.h` contract for the rest. |
| `port_input.md` | Local input -> command packets, the full key / mouse binding table, platform-layer contract. |

The round-3 briefing is `docs/port/BRIEFING_round3.md`. Each report ends with corrections to
`docs/ENGINE.md` (summarised there under "Port round 3 corrections").

## Port reports, round 4 (2026-10-06 night)

| File | Scope |
|---|---|
| `port_ai_wizard.md` | The computer wizards' AI (player type 1). |
| `port_creatures2.md`, `port_creatures3.md` | The remaining creature types. |
| `port_hud.md` | HUD, radar, status bars, spell book, frame composition. |
| `port_sound.md` | Game-side sound manager, sample / music banks. |
| `port_reference.md` | The per-tick reference harness (patched carpet.exe in DOSBox) and the movie-0 comparison. |

Their corrections are merged into `docs/ENGINE.md` under "Port round 4 corrections (merged in round 5)".

## Port reports, round 5 (2026-10-07)

| File | Scope |
|---|---|
| `port_audio.md` | Sound output: mixer, HMP parser and sequencer (General MIDI through winmm). |
| `port_frontend.md` | The front end and save games. |
| `port_fli.md` | FLI players, cue scripts, subtitles, palette fades and effects. |
| `port_game.md` | game_main's loop shape, status bits, pacing, campaign flow. |
| `port_reference2.md` | Per-tick references of eight campaign levels and level generation of all 69. |
| `port_render_reference.md` | Pixel comparison of every drawn frame of movie 0. |
| `names_round5.md` | Naming debt: 226 renames (dispatch-table handlers), ENGINE.md merge of the round-4 corrections. |

Their corrections are merged into `docs/ENGINE.md` under "Port round 5 corrections (merged in round 6)" and into
`docs/FORMATS.md` (HMP songs, FLI movies).

## Port reports, round 6 (2026-10-07)

| File | Scope |
|---|---|
| `port_opl.md` | OPL2 FM music: software YM3812, the HMI OPL driver behaviour, inst.bnk / drum.bnk. |
| `port_net.md` | Network: the NetBIOS session / exchange logic over a TCP transport, desync check, front-end leftovers. |
| `port_reference3.md` | References with an active local player, movie recording in the port. |
| `port_render_reference2.md` | Render references at 640x480, the render options, the front end. |
| `names_round6.md` | Naming debt: 13 renames / new functions, typed Phase-1 tables and resource lists, data labels, ENGINE.md / FORMATS.md merge of the round-5 corrections, open questions answered. |

The briefings are `docs/port/BRIEFING_round4.md` .. `BRIEFING_round6.md`.
