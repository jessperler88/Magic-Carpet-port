# Region C (0x3c000-0x4c000): players, castle, projectiles, spells, switches, sound helpers, HUD text, debug

All 88 unnamed functions and the 13 UNCERTAIN names in todo2_C.txt were read (decompiled body, plus disassembly
where the C was garbled). 19 of the 88 are fragments (switch-case bodies or tails of neighbours whose size the
analyzer cut short) and are marked FRAGMENT. Thing = 0xa4-byte record, `P` = the player sub-block that thing+0xa0
points at (P = playerrec + 0x44f, playerrec = state+0x340b + p*0x801), `rnd` = thing+4 LCG (x*0x24a1+0x24df).

## 1. CSV

```csv
0x0003f360,player_spawn_3f360,"(playerrec) (re)spawn a player at level start/respawn: thing_rebuild_lists; start pos = state+0x23d9+p*6 (or own castle pos); create class 3 type 0 flyer if none else reset state (+0x46 = rec+9==1); thing+0xa0 = rec+0x44f; P+0x30 = player no; P+0x14b=100 P+0x15f=2000; create the 24 spell things from P+0x214 templates (negative = none; sealed spells P+0x394[id] get sprite 0x118 and flag 0x40000); per-player flyer sprite 0x2c/0x111..0x117; first spawn: castle pos from state+0x385d7+p*0xd8 (+0x20a/c/e) and if state+0x38c97[p] (castle level) create class 3 type 2 castle + one build effect (FUN_26320) per level then castle_set_level_stats; health 10000 mana 1000 (1000000 each when rec+0x18==&DAT_ae89e - special/debug record?); threat table P+0x1cc = 0x9fdf vs other flyers; type-1 players get P+0x1cc[8]=0x601f P+0x2f4=p*4; P+0x18c/+0x190 = 0x800; cfg+0x96=0 for local player"
0x0003fc00,player_flyer_move_3fc00,"per-tick flight for class 3 state 0 (called from player_type0_s0_update_402c0 and 405f0): yaw += P+0x147/8 (P+0x147/+0x149 += steer P+4/+6); speed +0x7e -> P+0xc by 0x10; pitch from height over terrain vs descriptor clearance (+0x9c+10) clamped +-0x100 -> P+0x1c camera pitch; move by yaw/pitch (3e420) + knockback P+0x16 along P+0x18/+0x1a (decays 4/tick) + pending displacement P+0x198/19a/19c; rubber-band tether to P+0x13a (released when dist>0x13ff or target dead or P+0x13c==1000); terrain collide (3fa40) then thing_move_to; flying sound (water cell type 1 -> other sound); fire/ridge proximity sounds when P+0x18c / P+0x190 < 0x600; local player: P+0x2e countdown and FUN_1f800; random ambient sound every 64 ticks with 1/11 chance"
0x000419a0,castle_manage_balloons_and_guards_419a0,"(castle thing) by castle level +0x1a: balloons = 0 1 1 1 2 2 3 3 and guards = 0 0 0 4 6 14 18 34 for levels 0..7; balloon slots P+0x34[3] (class 3 state 9 things; sprite += player no; +0x90 owner); dead balloon -> FUN_25fe0 + delete; when castle mana (+0x8c) + P+0x134 < capacity (+0x88) a balloon with +0x8c<+0x88 is sent to the nearest free mana ball (castle_find_free_mana_ball_41290) else back to the castle; P+0x126/+0x122 += balloon mana/total; guard slots P+0x54[34] hold class 5 type 15 creatures (castle archers) spawned one per 16 ticks (+0x2e cooldown) at castle pos +(0x80 0x280) yaw 0x200; slots cleared when the guard dies/changes class"
0x00044150,projectile_fly_and_impact_44150,"class 9 shared flight (states 2 3 4 5 6 11 15 16 17 20 via 44a40/44a50): steer to +0x92 target or on first tick (flag 2) auto-aim (45f00) else fly straight; speed +0x7e -> +0x80 by 2; move; collision (105f0): if target has flag 0x8000 (rebound) and target mana >= our mana/4 and +0x44==10 and +0x45 in {1 0x11}: reflect (target mana -= our/4; yaw = +0x1e+0x400 +- rnd%0x2d; pitch mirrored; owner/target swap; life reset; moved to target top) else explode; no collision: below terrain -> type!=4 on water cell (type 1) spawns class 10 type 5 splash else explode; else life +0xc-- and explode when <0; explode = create class +0x44 type +0x45 effect with owner yaw pitch target(+0x92) damage(+0x2c) (+0x45==0x22 teleport: life = +0x2c) + hit stats; delete"
0x0003ef10,player_compute_level_stats_3ef10,"end-of-level statistics for the local player: P+0x37c[24] = spell found flags (P+0x2a4[id]!=0); P+0x16b = % of level spells (state+0x38ca3[24]!=0) collected; P+0x167 = kills*100/state+0x38c9f (creature count); P+0x16f = hits(P+0x15b)*100/shots(P+0x157); P+0x173 = (castle mana + P+0x134)*100/cfg+0xbc world mana; P+0x177 = average of the applicable percentages; P+0x17b = elapsed ticks (DAT_12eab4 - start); called from game_main"
0x00040b70,player_apply_hits_40b70,"(player thing) returns 2 dead / 1 hit / 0: +0x76 pusher != 0 -> rubber band attach: pusher's P+0x13a = self idx P+0x13c = 200 P+0x13e = dist clamped 0x400..0xc00; hit flash P+0x188=4 P+0x17f=0x10; +0x70 != 0 -> steal-mana transfer of +0x6c to thing[+0x70] (class 3) and own +0x8c -= +0x6c; +0x5e pending damage: flag 0x4000 (shield) quarters +0x5a and charges the quarter to +0x8c; health -= +0x5a; knockback P+0x18/+0x1a = angle/pitch to attacker P+0x16 = dmg/10 (0..0x50); palette flash (3f210); sound; dead -> +0x26 = attacker"
0x00046ae0,spell_dropped_update_46ae0,"(spell thing on the ground; args spell id + new state) life +0xc countdown -> delete; sink/follow ground (3e5f0 == -1 -> delete); every 4 ticks for each live flyer (type 0): local player already owns the spell -> flag 1 (dim icon); on bbox overlap: AI players (type 1) lacking it with P+0x31c[id]==1 get P+0x274[id]=200; find first free slot in P+0x214[24] unless spell already owned; flag 0x40000 (dropped by death) -> +0x84 = 0; sound; +0x2a = picker; +0x46 = new state; P+0x214[slot] = idx; P+0x3ac = slot; push slot into P+0x304[10] new-spell queue; returns 1 when picked up"
0x00040e70,player_apply_controls_40e70,"(player thing) P+0 input bits: 1/2 accelerate/decelerate P+0xc by 0x10 (clamp +-0x50 P+0xe=1); 4/8 turn P+0x10 by 0x10 else decay by 4 toward 0 (clamp +-0x50); then player_cast_spell_410f0 twice (left/right hand)"
0x00045530,projectile_castle_seed_update_45530,"class 9 state 10 (castle spell projectile): first tick (flag 2) castle_site_is_free_11be0 - site blocked -> castle_spell_reset_charge_41310 and delete; then home on +0x96 target pos (turn yaw/pitch) accelerate move; on collision or reaching ground: if site free create the castle-build effect with owner and delete"
0x00041720,castle_spill_mana_41720,"(castle) excess = +0x8c + P+0x134 - +0x88 capacity (all of +0x8c when level +0x1a == 0): spawn up to 32 class 10 type 0x27 mana balls (rebuilding the pool if full) each carrying excess/n mana +0x90 = owner random speed 0x10..0x3f upward +0x2e random heading; subtract from castle"
0x0003fa40,player_terrain_collide_3fa40,"(player thing; target pos in DAT_adfc4) terrain type mask 0x100 (impassable) at target: retry with two deflected headings via angle_diff/rotate_offset; returns 0 when all blocked; clamps target z to ground + descriptor clearance (+0x9c+0xc)"
0x000466af,projectile_line_of_fire_clear_466af,"starts on a 1-byte pad (real entry 0x466b0): (shooter target) dist <= descriptor sight radius (+0x9c+0x1c) and |yaw diff|<=0x100 |pitch diff|<=0x38; copies the shooter thing to the stack (collision filter 0xff) and marches it in 0x180 steps toward the target: hits a thing -> 1; terrain above the ray or distance exhausted -> 0; 0 callers in export"
0x000410f0,player_cast_spell_410f0,"(player thing; spell thing; hand flag 0x100/0x200; input bit) if input bit set: spell 2 and 0x15 cancel each other (P+0x2a8/P+0x2ce spell +0x30=0); spell 0x10 castle busy (+0x30!=0) -> sound and return; need +0x8c >= spell +0x88; multi-shot spells (+0x3e) count +0x3d bursts; +0x30 = +0x32 starts casting; sets hand flag clears 0x20; without input bit only continue if +0x3d pending"
0x0003d7ce,terrain_set_cell_height_3d7ce,"starts on 2-byte pad (real entry 0x3d7d0): (x y h) clamp h 0..255; refuse when cell flag 0x80 (castle footprint); DAT_ddfb0[cell]=h; h==0: if all 8 neighbours pass terrain_cell_flag_check_3d5f0 clear low nibble of DAT_fdfb0 else flag=(flag&0xf8)|1; FUN_324e0 redraw; returns 1 only when clamped at (0 0) (odd artifact)"
0x000451fb,FRAGMENT,"switch-case body of projectile_lightning_update_44fc0: spawns zig-zag bolt segments (class 9 type 9 state 14) then hit effect with +0x92 target and +0x2c damage quartered vs rebound-flagged player"
0x00042200,castle_set_level_stats_42200,"(castle; castle spell thing) switch on level +0x1a -> castle_apply_level_stats_42170(castle spellthing health mana): L0 0/5000 L1 20000/10000 L2 40000/20000 L3 40000/40000 L4 60000/80000 L5 60000/160000 L6 80000/320000 L7 80000/30000000"
0x00046470,projectile_target_score_46470,"(shooter target maxYaw maxPitch) with shooter z raised by +0x4e: -1 if |yaw diff|>maxYaw or |pitch diff|>maxPitch or dist>0x1400 else weighted score (cos^2 + 4 sin^2 of both diffs scaled by dist); used by projectile_pick_target_45f00"
0x000404b9,FRAGMENT,"tail of player_type0_s0_update_402c0 (starts mid jump): clamp +0x8c to 0..+0x88 and +0xc to -1..+8; decrement P+0x15f and P+0x210 timers; +0x84 = +0x88/2000 (min 100) or when flag 0x1000 +0x88/200 (min 1000) then clear 0x1000; P+0x155 = maxhealth/2000 or /250"
0x0004a16e,input_mouse_set_range_4a16e,"starts on lea pads (real entry 0x4a170): int33 fn7/fn8 horizontal/vertical limits (x<=0x27e y<=0x18e or 0x1de) through dos_int86; x<<3 y<<3 in 640 mode (DAT_12edae==8); 0 callers"
0x0004a660,switch_creature_dead_trigger_4a660,"(switch thing; creature type or -1) when the creature list cfg+0x8e1e[type] is empty (-1: lists 0..11 and 16 all empty): +0x1a counts down from 0x10 then sound + switch_activate(+0x18 1) + delete; called by the 18 stubs switch_*_trigger_4a780..4a890"
0x00043a70,FRAGMENT,"inner loop of ui_draw_map_43910: writes 2x2 pixels per cell: colour = shade[DAT_edfb0 light<<8 | DAT_cd9b0[DAT_cdfb0 texture]] with blend table DAT_bd9b0 between neighbours"
0x0004265c,FRAGMENT,"switch-case body of balloon_update_42530: deliver mana to the castle when within +0x7e*castle level and castle level>0 (+0x8c moved; +0x90 reset; health restored) else move; then follow ground / move / player_take_damage"
0x000439ec,FRAGMENT,"first part of the ui_draw_map_43910 pixel loop (same 2x2 blended map plot)"
0x000465b0,target_aim_score_465b0,"(a b maxYaw maxPitch) same score as 46470 without the z adjustment: -1 when outside cone or dist>0x1400 else cos^2+4sin^2 weighted by distance; used by projectile_pick_target_45f00"
0x00043bc8,FRAGMENT,"body of scenery_tree_update_43ba0 (tree dies): spawn class 10 fire effect owned by attacker +0x5e with +0x2e = 3/4 height; life rnd%0x3c+0x82 for both; clear flags 0x20008; state 1 burning; flag 0x20000; snap z; on water cell spawn splash and delete"
0x00041f00,castle_begin_build_stage_41f00,"(castle) create class 10 type 42 castle-build effect (39a50): sound; clear flag 0x40; level +0x1a++; state 5 +0x30 = 4; set extents + castle_set_level_stats; owner P+0x32 = castle idx P+0x1a0 = level; effect +0x2a = castle +0x47 = level flag 0x100"
0x00042370,castle_spell_set_capacity_42370,"(castle) if owner flyer alive (state 0/1) and owner has the Castle spell (P+0x2c4 = spell 16 thing): spell +0x88 = 5000 10000 20000 40000 80000 160000 320000 30000000 by castle level and +0x8c = +0x88/+0x32 levels"
0x0004bfb0,sprite_groups_reload_by_priority_4bfb0,"(called by switch_activate after spawning) init cache if DAT_adf58==0; clear DAT_b7cb0; unload groups whose priority byte DAT_b9791[i]==0 (unless locked); then for priority 0xff downwards load every not-yet-loaded group with that priority while sprite_group_size <= tmap_free_space"
0x0004aa81,ui_draw_text_background_4aa81,"starts on lea pads (real entry 0x4aa90): (x y str) walks the string like ui_draw_text: per glyph ui_fill_rect_blend2_22610 (translucent box) advancing by glyph width (font+6*(c+1)+4); 9/0x20 space width; 10 newline; 0xb-0xd skipped; stops at x>0x27f; returns end x"
0x00042460,castle_take_damage_42460,"(castle) health<0 -> 2; pending +0x5e: health -= +0x5a; dead -> +0x26 = attacker return 2; else owner P+0x187 = 4 hit flash return 1; +0x7c == own owner idx (upgrade request from own castle spell) and level<7 -> flag 0x40 (upgrade) and +0x7c = 0"
0x0004bd10,sprite_table_init_sizes_4bd10,"after tmaps_load: for each 14-byte sprite record at DAT_97678 (until +6 and +8 both 0) read its tmap header (into the screen buffer as scratch; missing -> w=h=0xff flag 1); derive the missing world extent +6 or +8 from the header w/h ratio; +0xc = header byte 1 (draw type); then tmaps_close_file"
0x0003e4b0,terrain_slope_vector_3e4b0,"(pos out[2]) from the 4 corner heights of the cell: out[0] = h00-h10-h11+h01 (x slope) out[1] = h00+h10-h11-h01 (y slope); used by mana balls / effects to roll downhill"
0x0003dc10,demo_relink_state_pointers_3dc10,"after loading a saved game state: thing[rec+0x3415]+0xa0 = playerrec+0x44f for each player; every live thing's descriptor pointer +0x9c rebased to DAT_96af0 relative to the local player's descriptor"
0x00043ff0,thing_turn_toward_43ff0,"(a b) +0x22/+0x24 = yaw/pitch to b then turn +0x1e/+0x20 by angle_turn_step (& 0x7ff)"
0x000409e0,player_look_at_killer_409e0,"(dead player thing) turn yaw/pitch toward killer thing +0x26; pitch then zeroed; P+0x149 = pitch P+0x147 = 0 (spectator camera while dead; from player_type3_s3_update_40ab0)"
0x00041404,FRAGMENT,"duplicate/overlapping continuation of player_flyer1_s4_update_413a0 (castle idle state): set extents; castle_manage_balloons_and_guards; collect colliding own mana balls (type 0x27 +0x90 == owner) into +0x8c"
0x000414b6,FRAGMENT,"tail block of player_flyer1_s4_update_413a0: +0x32-- ; castle_spell_reset_charge_41310; snap z to terrain"
0x00041610,castle_spawn_build_effect_2a_41610,"(castle) create class 10 type 0x2a effect with +0x47 = castle level +0x18 owner +0x2a = castle; castle +0x30 = 4 (build sequencer step)"
0x00041670,castle_spawn_build_effect_29_41670,"(castle) create class 10 type 0x29 effect with +0x47 = level +0x18 owner +0x2a = castle; castle +0x30 = 6"
0x00042170,castle_apply_level_stats_42170,"(castle spellthing health mana) +8 = health keeping current damage (health<0: +0xc = health - min(-old health/2)); spellthing +0x88 = mana +0x8c = mana/+0x32; castle +0x88 = mana"
0x00042010,castle_collapse_level_42010,"was castle_mana_tick (UNCERTAIN) - CORRECTED: called from castle state 6 (destroyed): if level>0: 10% of +0x88 mana spilled as mana balls (castle_spill_mana) sound; spawn collapse effect via effect_type51_s53_update_27930 with scratch record at state+0x747b..0x74af; level -1; extents + castle_set_level_stats + castle_spell_reset_charge; level 0 -> owner P+0x32 = 0 and delete"
0x000425d1,FRAGMENT,"switch-case body of balloon_update_42530: collect a mana ball (+0x92 target; flag 0x40): on overlap ball mana += own +0x8c; ball owner = +0x90; ball deleted... then move/follow ground/player_take_damage"
0x0004bbf0,sprite_cache_init_4bbf0,"DAT_adf58 = FUN_4c280(cfg+0xa8 pool cfg+0xac size 0x211 slots) texture cache; DAT_adf50 = 4c7f0(); tmaps_load; clear DAT_b8d3c DAT_b84f8 DAT_b7cb0 (0x844 bytes each); 4c0a0; 4c0f0"
0x0003e9a0,thing_find_in_sight_of_class_3e9a0,"(thing class) scan pool for a live thing of that class with a different owner (+0x18) not dead (flag 0x20) within descriptor sight radius (+0x9c+0x1c) (returns the last match)"
0x0004bee0,sprite_mark_needed_for_model_4bee0,"(a b fallbackSprite) table of 34-byte records at DAT_9649e {u16 a u16 b u16 sprites[15]...} terminated by a negative key: for the record matching (a b) set each listed sprite's group priority (4bf60); no record and fallback>=0 -> set for the fallback sprite; called by switch_activate (a b = class/model of spawned things?)"
0x00040240,player_rebuild_spell_index_40240,"(player thing) zero P+0x2a4[24] then for each slot P+0x214[i] != 0: P+0x2a4[spell id (+0x41) of that thing] = slot thing idx (lookup spell -> thing by id)"
0x0004099c,FRAGMENT,"loop tail of player_dying_update_405f0: re-owns mana balls (type 0x27 with +0x90 == dying player) then sets flag 0x20 dead and decrements state+0x11f1"
0x00040a81,FRAGMENT,"epilogue of player_look_at_killer_409e0"
0x00041290,castle_find_free_mana_ball_41290,"(castle excludeA excludeB) nearest (pos_dist_sq_xyz_3e8f0) mana ball (type 0x27) owned by the castle owner that is not A or B; used by the balloon dispatcher"
0x00041310,castle_spell_reset_charge_41310,"(castle thing; mode) owner's Castle spell thing (P+0x2c4): mode 0 -> +0x30 = 0 else +0x30 = +0x32 - 1 (re-arm after use)"
0x00043ee0,thing_aim_at_43ee0,"(a b) a +0x22/+0x24 = yaw/pitch to b with a's z temporarily raised by +0x4e"
0x00046857,FRAGMENT,"ray-march loop tail of projectile_line_of_fire_clear_466af (same 0xa8 stack frame)"
0x00046d8b,FRAGMENT,"continuation of spell_dropped_update_46ae0 (flyer loop)"
0x00046dd0,spell_phase2_pickup_46dd0,"class 12 phase 2 handler (via spell_phase2_common_47300): DAT_943c4 = 0; spell_dropped_update_46ae0(id); when picked up create a fresh spell thing through class-12 table B (DAT_962b6 + id*0xe) and set its state += 2 (phase 2 = owned copy); DAT_943c4 -> flags |= 1|(old & 0x400); sealed (flag 0x400) -> sprite 0x118"
0x00046e17,FRAGMENT,"tail of spell_phase2_pickup_46dd0"
0x00047584,FRAGMENT,"tail of spell_speedup_update_47420 (starts on 2 zero bytes): restores P+0xc = +0x80 base speed and +0x7e from P+0xc then clears flag 0x80 on the caster"
0x00049c40,sound_fade_player_sound_49c40,"(thing player soundId) if sfx on: sound ids 1 2 5 0x1f (ids 1/2/5 skipped when memory class DAT_9e328==3) for the local player -> FUN_4e400 start fade-out of that (owner sample) channel; called from player_flyer_move"
0x00049ccc,music_fade_out_begin_49ccc,"starts on lea pads (real entry 0x49cd0): if music available and on: DAT_943c9 = 1 and hmi_timer_add_event(60 Hz callback 0x49ca0 which lowers DAT_943c8 MIDI volume then calls 49d10)"
0x00049d10,music_fade_out_end_49d10,"if DAT_943c9: remove the fade timer event; music_stop_1f960; snd_midi_set_volume; DAT_943c8 = 0x7f; DAT_943c9 = 0"
0x00049d50,sound_playing_count_49d50,"if sfx available and on return sound_count_playing_628a4 else 0"
0x00049dc2,FRAGMENT,"tail of an unlisted function at ~0x49d80 that scans the 32 (owner sample) channel table and returns hmi_sample_done / 1"
0x00049de0,ui_sprite_lists_relocate_49de0,"for each sprite table in the linked list (+0xc next): sprite_table_relocate_62ad0 or relocate_x2_62a80 by DAT_12edae bit 0 (used by video_toggle_resolution)"
0x00049e40,ui_sprite_lists_unrelocate_49e40,"inverse of 49de0 (sprite_table_unrelocate_62b90 / _half_62b10)"
0x00049e96,math_line_point_offset_49e96,"starts on lea pads (real entry 0x49ea0): (x0 y0 x1 y1 x y) returns (y - y0) - (x - x0)*(y1-y0)/(x1-x0) (vertical offset of point from the line; x0==x1 -> x0 - x)"
0x00049ede,mem_replace_byte_49ede,"(src? dst old new len): for len bytes of buf replace value old with new"
0x00049f0c,mem_swap_blocks_49f0c,"(a b len) swap len bytes between two buffers"
0x00049f3c,mem_sum_bytes_49f3c,"(buf len) byte checksum"
0x00049ff0,FRAGMENT,"tail of an unlisted string-table search (crt_strnicmp loop at 0x49f62..) returning index or -1"
0x0004ab60,ui_text_width_4ab60,"(str) sum of glyph widths (font DAT_adfbc 6-byte records +4) with 9/0x20 = space width; control bytes <9 ignored"
0x0004ad60,mem_sum_range_4ad60,"(begin end) byte checksum of a pointer range"
0x0004b4a0,dbg_print_mem_blocks_4b4a0,"walks the 18-byte block list at 0x130120 printing 'used %d/size %d' for blocks > 16 bytes"
0x0004b4d0,dbg_print_copyright_4b4d0,"printf 'Copyright (c) 1994 Bullfrog Productions Ltd.' 'All rights reserved.' + DAT_92b3c (called from data_load_all and shutdown)"
0x0004bc80,sprite_cache_shutdown_4bc80,"FUN_4c790 (free texture pool) 4cb50 4bec0; clear the three 0x844 tables; DAT_987e0 = 0; close tmaps handle DAT_987e4"
0x0004bcf0,tmaps_close_file_4bcf0,"close DAT_987e4 (tmaps.dat handle) if open and set -1"
0x0004bf60,sprite_set_group_priority_4bf60,"(sprite) DAT_b9791[group of sprite (DAT_b84f4[base]+8)] = DAT_97683[sprite*14] (sprite record byte +0xb = load priority)"
0x0003c1d1,FRAGMENT,"tail (file close + return 1 with 0x40-byte frame) of a demo file-exists check that starts below 0x3c000"
0x0003c310,demo_state_file_exists_3c310,"sprintf movie/gam%05d-style name (string 0x908f4 'movie') open/close -> 1 if present else 0; used by demo_load_state"
0x0003c4f0,demo_terrain_file_exists_3c4f0,"same for the terrain dump name; used by demo_load_terrain"
0x0003d100,dbg_dump_mem_blocks_3d100,"mem_alloc 5000 then walks the block list at 0x130120 printing 's%7.7d,u%01d' (size used) per block via sprintf/printf; then dos_free_all_dos_memory; 0 callers (debug)"
0x0003d5f0,terrain_cell_flag_check_3d5f0?,"(cell) 1 unless DAT_fdfb0[cell] low 3 bits are 2 3 or 5 (neighbour test used by terrain_modify_cell and terrain_set_cell_height when lowering to 0)"
0x0003daeb,dbg_draw_config_menu_text_3daeb,"starts on 5 zero pad bytes (real entry 0x3daf0): ui_draw_text('Config menu.. 0123456789' at 2 2) with a video-mode (DAT_12edae bit0) check; 0 callers"
0x0003e5f0,pos_sink_or_follow_ground_3e5f0,"(pos ... ) water cell (terrain_cell_flag_bit bit0) and arg==0 -> floor = -0x300; move pos z (+4) toward ground+clearance by speed (25% when near); returns 1 moved 0 none -1 when z reached -0x300 (sunk); used by spell_dropped_update"
0x0003e630,FRAGMENT,"continuation of pos_sink_or_follow_ground_3e5f0"
0x0003e72e,pos_dist_manhattan_3e72e,"starts on 2-byte pad (real entry 0x3e730): |dx|+|dy|+|dz| of two pos"
0x0003e860,pos_dist_chebyshev_xy_3e860,"max(|dx| |dy|) of two pos (used by 12bd0)"
0x0003e8f0,pos_dist_sq_xyz_3e8f0,"dx^2+dy^2+dz^2 (16-bit deltas)"
0x0003f210,player_set_palette_effect_3f210,"(thing effectId) if the thing's owner is the local player cfg+0x98 = effect (palette_effect_update_33010: red tint on hit etc.)"
0x0003f240,player_note_fire_distance_3f240,"(fire effect thing) P(local player)+0x18c = min(dist to fire); called by effect_fire_update_23c20; player_flyer_move plays the fire sound when < 0x600 and resets to 0x800"
0x0003f2c0,player_note_ridge_distance_3f2c0,"(ridge node thing) P(local)+0x190 = min dist; called by effect_ridge_node_s52_update_27710; same proximity-sound mechanism (+0x190)"
0x0003e080,player_log_position_3e080?,"UNCERTAIN kept (low): (playerrec thing) called by player_commands_process_3a8b0 after each player's packet when not paused: writes 14-byte entry [rec+0x10 count - 1] at rec+0x24a: x y z(terrain) yaw (+8) cfg+0x5e*knock/16 + pitch/2 - knock/8 (+0xa) P+0x147 (+0xc) rec+0x248 - a per-player position/orientation log (network/replay?)"
0x00043db0,scenery_stone_update_43db0,"CONFIRMED: class 2 state 3 standing stone: flag 0x20000 (recyclable) and snap z to terrain"
0x00043de0,scenery_dolmen_update_43de0,"CONFIRMED: class 2 state 6 dolmen: any live player thing whose bbox overlaps gets flag 0x1000 (byte +0x11 |= 0x10); snap z"
0x00043e60,scenery_badstone_update_43e60,"CONFIRMED: class 2 state 9 bad stone: same as 43db0"
0x00046960,creature_sound_timer_46960,"CONFIRMED: +0x3a countdown copied along the +0x36 segment chain; at 0 when +0x3b==0: if dist^2 to listener < 0x2400000 then +0x30 = dist and +0x3a = 0x10 (segments +0x12)"
0x00047760,spell_shield_update_47760,"CONFIRMED: spell 4 phase 0: while +0x30>0 and can_cast: caster flag 0x4000 (shield: damage quartered and charged to mana in player_apply_hits) + charge mana; cannot cast -> +0x30 = 1"
0x00047840,spell_earthquake_update_47840,"CONFIRMED: spell 6 phase 0: at cast start spawn a class 9 projectile from the caster with impact class +0x44=10 type +0x45=0xf (Earthquake effect) +0x1a aim from P+0x146 launch pos +0x96 on terrain; charge mana each tick"
0x000479f0,spell_meteor_update_479f0,"CONFIRMED: spell 7: projectile with impact effect type 0x11 (Meteor) copying caster yaw/pitch"
0x00047d40,spell_crater_update_47d40,"CONFIRMED: spell 9: projectile with impact effect type 0xb (Crater)"
0x00048490,spell_rebound_update_48490,"CONFIRMED: spell 14: while casting caster flag 0x8000 (rebound: projectile_fly_and_impact reflects fireballs/meteors); cleared when +0x30 reaches 0"
0x00048510,spell_lightning_update_48510,"CONFIRMED: spell 15: projectile with impact effect type 0x17"
0x000492e0,spell_mini_fireball_update_492e0,"CONFIRMED: spell 23: burst of +0x3d projectiles with impact effect type 0 (explosion)"
```

Fragment list (19): 0x3c1d1, 0x3e630, 0x404b9, 0x4099c, 0x40a81, 0x41404, 0x414b6, 0x425d1, 0x4265c, 0x439ec, 0x43a70,
0x43bc8, 0x451fb, 0x46857, 0x46d8b, 0x46e17, 0x47584 (tail of spell_speedup_update_47420: restores P+0xc speed and
+0x7e and clears flag 0x80), 0x49dc2, 0x49ff0.

Padded starts named at the todo address (real entry in the comment): 0x3d7ce (+2), 0x3daeb (+5), 0x3e72e (+2),
0x466af (+1), 0x49ccc (+4), 0x49e96 (+10), 0x49ede (+2), 0x49f0c (+4), 0x49f3c (+4), 0x4a16e (+2), 0x4aa81 (+15).

## 2. ENGINE.md section: players, castle, projectiles, spells (region C, agent pass 2)

### Player record layout (corrections and new fields)

The player record is `state + 0x340b + p*0x801` (3f360 computes `p = (rec - (state+0x340b)) / 0x801`; the +0x340F
quoted earlier is rec+4). `thing+0xa0` of every thing owned by player p points at **rec+0x44f** (3f360, 3dc10), so
the "owner player record" fields documented as thing+0xa0+X are at rec+0x44f+X. Fields of that sub-block `P` seen
in this pass:

| P+ | meaning | evidence |
|---|---|---|
| +0 | u32 input bits from the command packet (1/2 accel/decel, 4/8 turn) | 40e70, 3a8b0 |
| +4/+6 | i16 steer deltas (mouse) -> added to +0x147/+0x149 | 3fc00, 3a8b0 |
| +0xc | i16 target speed (-0x50..0x50) ; +0xe accelerating flag; +0x10 turn rate (-0x50..0x50) | 40e70 |
| +0x16 | knockback strength (dmg/10, max 0x50, decays 4/tick); +0x18/+0x1a knock yaw/pitch | 40b70, 3fc00 |
| +0x1c | camera pitch (terrain-following pitch & 0x7ff) | 3fc00 |
| +0x30 | player number; +0x32 castle thing idx; +0x34[3] balloon idx; +0x54[34] castle guard idx | 3f360, 419a0 |
| +0x122/+0x126 | balloon mana totals (recomputed each castle tick) | 419a0 |
| +0x134 | mana in transit (added to castle mana in capacity checks) | 41720, 3ef10 |
| +0x13a/+0x13c/+0x13e | rubber-band target idx / timer (200, released at 1000) / length (0x400..0xc00) | 40b70, 3fc00 |
| +0x142 | mana; +0x146 aim byte copied into projectile +0x1a | 3f360, 47840 |
| +0x147/+0x149 | yaw/pitch accumulators (yaw advances by +0x147/8 per tick) | 3fc00 |
| +0x14b | 100; +0x155 health/2000 or /250; +0x15f 2000 countdown; +0x210 timer | 3f360, 404b9 |
| +0x157/+0x15b | shots / hits; +0x167 kills; +0x16b spells %; +0x16f accuracy %; +0x173 mana %; +0x177 overall %; +0x17b start tick / elapsed | 3ef10 |
| +0x17f/+0x188 | hit flash (0x10 / 4); +0x187 castle-hit flash | 40b70, 42460 |
| +0x18c/+0x190 | nearest fire effect / nearest ridge-node distance (reset to 0x800, sound when < 0x600) | 3f240, 3f2c0, 3fc00 |
| +0x198/+0x19a/+0x19c | pending displacement added to the position next tick (teleport / rubber band) | 3fc00 |
| +0x1a0 | castle level; +0x1cc[8] threat table (0x9fdf default vs flyers, 0x601f for AI) | 41f00, 3f360 |
| +0x20a/+0x20c/+0x20e | castle position (copied from the level block `state+0x385d7 + p*0xd8`) | 3f360 |
| +0x214[24] | spell slot -> spell thing idx (templates: negative = empty); +0x274[24] AI "wants spell" timer (200); +0x2a4[24] spell id -> thing idx (40240); +0x304[10] queue of newly picked slots; +0x31c[24] AI allowed; +0x37c[24] found flags; +0x394[24] sealed (-> sprite 0x118, flag 0x40000); +0x3ac selected slot | 3f360, 46ae0, 40240 |

Other per-level state: `state+0x23d9 + p*6` start position, `state+0x38c97[p]` castle level per player,
`state+0x38ca3[24]` spells present in the level, `state+0x38c9f` creature count (3f360, 3ef10).

### Thing flags found here (u32 at +0x10)

0x1000 = standing on a dolmen (43de0 sets it on the player each tick); **0x4000 = shield active** (47760; 40b70
quarters damage and charges it to mana); **0x8000 = rebound active** (48490; 44150 reflects projectiles whose impact
class/type is 10/1 or 10/0x11); 0x40 on a castle = upgrade requested (42460 -> state 5). The earlier note "0x40 mana
ball being collected" applies to balloons only.

Projectile fields: **+0x44 / +0x45 = class/type of the effect spawned on impact** (10/0 explosion for fireballs and
mini fireballs, 10/0xb crater, 10/0xf earthquake, 10/0x11 meteor, 10/0x17 lightning, 10/0x22 teleport whose life
= +0x2c), +0x96 = launch/target position, +0x1a = aim byte from P+0x146, +0x7c on a castle = owner idx of an upgrade
request. Castle +0x1a = level 0..7.

### Castle state machine (class 3 type 2)

* Created in state 5 with +0x30 = 0. **State 5 (41500) = build/upgrade sequencer** on +0x30: 0 -> site check
  (118c0/11980) then `castle_begin_build_stage_41f00` (effect type 42, level++, +0x30 = 4); 4 -> wait; 3 ->
  `castle_spawn_build_effect_2a_41610` (+0x30 = 4); 5 -> `castle_spawn_build_effect_29_41670` (+0x30 = 6); 2 ->
  state 4. The build effects carry the level in +0x47 (castle size index).
* **State 4 (413a0) = active castle**: `castle_take_damage_42460` (2 -> state 6; flag 0x40 -> state 5 upgrade),
  `castle_spill_mana_41720`, extents, `castle_manage_balloons_and_guards_419a0`, pick up own mana balls; +0x32 = 1
  -> state 5 step 3 (rebuild).
* **State 6 (416d0) = destroyed**: `castle_collapse_level_42010` (10% mana spilled, level-1, collapse effect; level 0
  -> castle removed) then back to state 4 with +0x32 = 5.
* Level table (42200/42370): capacity +0x88 = 5000 << level (level 7 = 30,000,000); max health 0, 20000, 40000,
  40000, 60000, 60000, 80000, 80000; balloons 0,1,1,1,2,2,3,3; guards (creature type 15) 0,0,0,4,6,14,18,34.
  The Castle spell (spell 16, P+0x2c4) has its total mana set to the capacity of the next level.
* Creature type 15 ("type15" in level.py) is the castle guard archer spawned by 419a0 at castle pos + (0x80,0x280).

### Spell life cycle

Phase 0 (state = spell*3) is the owned spell's cast handler (+0x30 remaining ticks, +0x32 total). Phase 1
(472f0 -> 46e50 -> `spell_dropped_update_46ae0`) is a spell lying on the ground (dropped on death, 405f0); phase 2
(47300 -> `spell_phase2_pickup_46dd0`) is the level pickup: on contact it creates a fresh spell thing from table B and
sets its state += 2 (**open question**: why +2 and not phase 0; possibly phase 2 = "owned, in book" and phase 0 only
while a template is active - check 402c0/16660 which read P+0x214 things). Casting is started by
`player_cast_spell_410f0` (+0x30 = +0x32; spells 2 and 0x15 are mutually exclusive; spell 16 refuses while busy).

### Misc

* `demo_relink_state_pointers_3dc10` confirms descriptors live in a table at DAT_96af0.. and that saved states
  store raw pointers.
* `sprite_set_group_priority_4bf60` / `sprite_groups_reload_by_priority_4bfb0`: sprite record byte +0xb (0x97683) is
  a load priority, DAT_b9791[group] holds it, and switch_activate reloads sprite groups by priority after spawning.
  Table DAT_9649e (34-byte records keyed by two u16) lists the sprites a (class, model?) needs - verify the key.
* 4bd10 shows sprite record +6/+8 are world extents (one derived from the tmap aspect ratio) and +0xc the tmap
  header draw-type byte.
* Open: identity of the record with +0x18 == &DAT_ae89e in 3f360 (gets 1,000,000 health/mana - attract-mode or
  network dummy player?); exact meaning of the 14-byte log written by 3e080 (rec+0x24a, count rec+0x10); what
  `terrain_cell_flag_check_3d5f0`'s cell-type values 2/3/5 are (low nibble of DAT_fdfb0).
