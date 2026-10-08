# THINGS subsystem (entities, per-tick update, dispatch tables, frame composition) - carpet.exe

## 1. CSV (address,name,comment)

```
0x0003dce0,thing_update_all_3dce0,per tick: free things flagged 0x400; rebuild per-class linked lists at cfg+0x8e1e..; creature sounds; hit bookkeeping; mana totals; then for every live thing call tableA[class][state].handler and ++thing->counter(+0x3f)
0x00035690,thing_create_35690,generic create: rec=tableB(class at [esp+8])+type([esp+0xc])*0xe; if rec.enabled && rec.model==type call rec.handler (76 callers)
0x00035685,thing_create_entry_35685,mis-decompiled alternate entry into thing_create_35690 (fragment)
0x0003568e,thing_create_entry_3568e,alternate entry into thing_create_35690 (fragment)
0x00035560,thing_alloc_35560,pop free-stack state+0x251[state+0x28]; fallback pops recyclable stack state+0x11f5[state+0x11f1] and frees that thing; memset 0xa4; defaults +8=300 +0x10=8 +0x7e=0x10 +0x2c=100 +0x18=own index +0x42/+0x43=0xff +0x44=10 +0x3a=0xfa +0x9c=&DAT_96a10 +0xa0=&DAT_b6e90 +4=index+rng
0x0003e3f0,thing_free_3e3f0,unlink from cell list; class=0; push onto free stack state+0x251
0x0003e3e0,thing_mark_delete_3e3e0,sets flag byte +0x11 |= 4 (u32 flag 0x400); freed at start of next thing_update_all (83 callers)
0x0003e330,thing_unlink_cell_3e330,remove from per-cell doubly linked list (DAT_0010dfb0[celly*256+cellx] head; +0x14 next idx; +0x16 prev idx); clears flag 4
0x0003e250,thing_link_cell_3e250,copy pos (x y z u16) into +0x48..+0x4d; insert at head of cell list for (x>>8 y>>8); set flag 4
0x0003e1d0,thing_move_to_3e1d0,if same cell just copy pos else unlink+link (returns 1 if cell changed)
0x0003e220,thing_relink_cell_3e220,unlink then link
0x0003e027,thing_free_all_3e027,frees every thing with class!=0
0x00035460,thing_pool_reset_35460,push all 1000 things onto free stack (index 1 popped first); state+0x7503=&DAT_b6e90 dummy player record
0x00035457,thing_pool_reset_entry_35457,fragment/alternate entry of thing_pool_reset_35460
0x000354c0,thing_pool_rebuild_free_354c0,rebuild free stack from class==0 slots and recyclable stack (state+0x11f1/0x11f5) from live things with flags 0x20400
0x000359b0,thing_free_count_359b0,returns state+0x28 + 1
0x00035240,thing_set_sprite_35240,+0x56=sprite id; +0x58 frame=0; +0x59 from sprite table (14-byte records at 0x97676); +0x4e..+0x55 half extents (bbox) from sprite table
0x00035230,thing_set_mana_from_health_35230,+0x8c mana = maxhealth(+8)/2
0x00035080,thing_restore_health_35080,+0xc health = +8 max health
0x0003ea50,thing_set_state_3ea50,+0x46 = new state (dispatch index)
0x0003ea70,thing_anim_advance_3ea70,if frame(+0x58) < frames(+0x59) ++frame return 0 else nonzero
0x0003da30,cell_kill_things_3da30,for things in a cell: scenery -> mark delete; creatures (not Wyvern/Kraken/Griffon) -> health=-1 and +0x26/+0x28=killer idx
0x0003da00,nop_3da00,empty function
0x0004b570,weather_update_nop_4b570,class 7 update handler = ret
0x000105f0,thing_find_collision_105f0,spiral search cells around thing; returns first thing with flag 8 matching filter class(+0x42)/type(+0x43) (0xff=any) whose bbox overlaps
0x00010730,thing_find_mana_near_10730,spiral search for effect type 0x27/0x28/0x2d (mana ball/?/wizard) not owned by caller (+0x90!=idx) overlapping bbox
0x00010d20,thing_area_damage_10d20,damage(amount [esp+0xc] type bit [esp+8]) to castles (player type 2) and all things in cells within radius(+0x50): adds +0x5a pending damage and +0x5e attacker idx
0x00011160,thing_area_damage_11160,variant of area damage (used by fire effect)
0x00011450,thing_area_damage_11450,variant of area damage (volcano); also sets castle +0x32=0x1e
0x000105c0,thing_collide_105c0,wrapper of bbox overlap test between two things
0x00010530,math_bbox_overlap_10530,|dx|<ax+bx && |dy|<ay+by && |dz+off|<az+bz using extents +0x4e..+0x54
0x00010080,spiral_search_begin_10080,allocate one of 100 iterators (0x18 bytes at 0xac160) over ring offset tables at DAT_000ade28
0x00010120,spiral_search_next_10120,returns next (dx dy) cell offset; 2 when exhausted
0x00010100,spiral_search_end_10100,release iterator
0x000468e0,creature_sound_tick_468e0,walk 20 creature lists + mana-ball list; dead creatures reset +0x3a/+0x3b else creature_sound_timer_46960
0x00046960,creature_sound_timer_46960?,countdown +0x3a (propagated along +0x36 chain); when 0: if dist to listener < 0x2400000 set +0x30=sqrt(dist) and reload 0x10
0x000150f0,hit_record_scores_150f0?,for class-9 projectiles with player owner and target: add 500..5000 to owner-playerrec+0x1cc[target player] (threat table); flag +0x11|=0x20
0x000427d0,mana_totals_update_427d0,player things +0x88 = playerrec+0x142; then players type 2/3 creatures and effects 0x27/0x28/0x2d add +0x8c to owner(+0x90) +0x88; cfg+0xbc = world total
0x000428e0,mana_add_to_owner_428e0,owner=thing[+0x90]; owner+0x88 += +0x8c; cfg+0xbc += +0x8c
0x00026120,mana_ball_merge_26120,merge mana ball b into a (+0x8c sum; choose owner +0x90) and free b
0x0003db20,game_check_level_won_3db20,per player with castle: if (castle mana + rec+0x134)*100/cfg+0x5e > state+0x38c93 for 16 ticks -> status |= 2
0x00034fa0,level_run_terrain_effects_34fa0,loop running update handlers of class-10 things of types 9..0xb 0x1b..0x20 0x2d(state 0x33) 0x32 0x33 until none remain; others marked deleted
0x00034d5e,level_spawn_terrain_effects_34d5e,rebuild pool; spawn level Effect records with DisId==-1; flag; run terrain effects
0x00034e00,level_spawn_effect_record_34e00,THING_INIT record -> pos (Xpos<<8 Ypos<<8 terrain height) -> tableB[class 10][Model].create; wall/path/canyon/ridge with SwiId!=0 -> level_build_linked_feature
0x00034c40,level_build_linked_feature_34c40,walk Parent chain to root then Child chain; apply LAB_342e0 (wall 0x1c) 34570 (path 0x1d) 346b0 (canyon 0x1f) 34760 (ridge 0x32)
0x00034f40,level_flag_terrain_effects_34f40,level records class 10 model 0x1c-0x1d 0x1f 0x32 with DisId==-1 get SwiId=1
0x00035090,effect_wizard_init_35090,finalise Wizard(0x2d) placement: snap to cell; +0x1a=2 +0x80=(w*h)/16 from 6-byte castle-size table[+0x47] +0xc=0x1e +0x2c=2000 +0x1c|=2 z=height<<5
0x000353f0,thing_set_castle_extents_353f0,+0x4e=0xe000 +0x50/+0x52=(w*256+0x500)/2 +0x54=0x4000 from castle-size table
0x00034b40,terrain_height_avg_34b40,average of 4 heightmap corners of a rect (DAT_000ddfb0)
0x000356e0,switch_activate_356e0,spawns level THING_INIT records (copy at state+0x2f945..0x385d3) whose DisId matches; counts creatures into state+0x38c9f
0x0004a8b0,switch_test_player_4a8b0,every 8 ticks: any player thing (type 0) whose bbox overlaps the switch -> sound and return 1
0x0004a940,switch_test_any_player_4a940,loop over player records testing collision with switch
0x0004a350,switch_update_repeat_4a350,class 11 state 3: trigger when player inside then 10-tick cooldown (re-triggerable)
0x0004a460,switch_update_once_4a460,class 11 state 5: trigger (mode 1) then mark delete
0x00018870,creature_ai_step_18870,common creature step: apply pending damage (+0x5a/+0x5e -> +0x28; +0x26 killer when dead); propagate min health along +0x36 chain; creature_move_step; target search
0x000181e0,creature_move_step_181e0,move toward target pos using descriptor(+0x9c) clearance (+0xa/+0xc); terrain check; turn yaw(+0x1e) by angle_turn_step
0x00018150,terrain_slope_at_18150,max diagonal height difference of the 4 heightmap cells around pos
0x000102b0,creature_check_terrain_102b0,flags: 2=height clearance vs descriptor; 1=terrain type mask (desc+0x14); 4=pitch
0x00019b80,creature_dragon_s1_19b80,Dragon state 1 = creature_ai_step + creature_apply_zvel
0x00019c30,creature_apply_zvel_19c30,z += +0x1a; +0x1a -= 5; if z < ground+0x100 then +0x1a=0x96
0x00018050,creature_segment_update_18050,state 120 body segment: delete if parent(+0x34) not a creature; yaw toward parent; follow parent
0x0001a0e0,creature_archer_s25_1a0e0,Archer (type 4) first state: target players within sight radius/fov from descriptor; else skeleton list; else flock to same-type leader (+0x34)
0x0001a830,creature_crab_s31_1a830,Crab (type 5) first state: hunt players; else seek nearest mana ball (+0x92); drops mana ball when +0x8c > +0x88+500
0x0001ac80,creature_crab_s33_1ac80?,Crab state 33 (not read)
0x0001c570,creature_skeleton_s56_1c570?,Skeleton state 56 (not read)
0x0001d540,creature_genie_s72_1d540?,Genie state 72 (not read)
0x000362d0,creature_create_dragon_362d0,needs 16 free slots; type 0 state 1 health 9000 sprite 0x13 + 16 segments (state 120 sprites 0x14..) linked via +0x34 parent/+0x36 child
0x0003650e,creature_create_vulture_3650e,type 1 state 7 health 2000 (table entry 0x36510)
0x00036610,creature_create_bee_36610,type 2 state 13 health 3000
0x00036750,creature_create_worm_36750,type 3 state 19 health 9000 sprite 0x59 + 16 segments
0x0003697e,creature_create_archer_3697e,type 4 state 25 health 1000 (entry 0x36980)
0x00036b30,creature_create_crab_36b30,type 5 state 31 health 5000
0x00036c80,creature_create_kraken_36c80,type 6 state 37 with segments
0x00036f00,creature_create_troll_36f00,type 7 state 43
0x00037000,creature_create_griffon_37000,type 8 state 49 health 10000
0x0003710c,creature_create_skeleton_3710c,type 9 state 54 health 1000 (entry 0x37110)
0x00037260,creature_create_emu_37260,type 10 state 61 health 2000
0x00037370,creature_create_genie_37370,type 11 state 66 health 20000
0x0003749e,creature_create_builder_3749e,type 12 state 73 health 1000 (entry 0x374a0)
0x000375e0,creature_create_townie_375e0,type 13 state 79 health 1000
0x0003772e,creature_create_trader_3772e,type 14 state 85 health 1000 (entry 0x37730)
0x0003784e,creature_create_type15_3784e,type 15 state 91 health 1000 (entry 0x37850)
0x0003797c,creature_create_wyvern_3797c,type 16 state 97 health 100000 (entry 0x37980)
0x00037b5e,weather_create_wind_37b5e,class 7 type 4 state 4 (entry 0x37b60)
0x00035abc,player_create_flyer_35abc,class 3 type 0 state 0 (entry 0x35ac0) flying wizard
0x00035b3c,player_create_type1_35b3c,class 3 type 1 state 1 health 10000 (entry 0x35b40)
0x00035bbc,castle_create_35bbc,class 3 type 2 state 5 health 40000 +0x1c=0x21; +0x96 cell-aligned home pos; +0x9a terrain height (entry 0x35bc0)
0x00035e5e,scenery_create_tree_35e5e,class 2 type 0 state 0 (entry 0x35e60)
0x00035f8e,scenery_create_stone_35f8e,class 2 type 1 state 3 (entry 0x35f90)
0x0003618e,scenery_create_dome5_3618e,class 2 type 5 state 15 (entry 0x36190)
0x00043ba0,scenery_tree_update_43ba0,take damage; when dead spawn fire effect owned by attacker and go to state 1 (burning); if on water (terrain type 1) spawn splash and delete
0x00043db0,scenery_stone_update_43db0?,class 2 state 3 Standing stone (not read)
0x00043de0,scenery_dolmen_update_43de0?,class 2 state 6 (not read)
0x00043e60,scenery_badstone_update_43e60?,class 2 state 9 (not read)
0x0003a210,spell_create_generic_3a210,(pos spellid state totalMana levels flagA flagB cost dmg): class 12 +0x41=spell +0x46=state(spell*3) +0x32=levels +0x88=total +0x8c=total/levels +0x84=cost +0x2c=dmg
0x0003a330,spell_create_fireball_3a330,spell 0 state 0 mana 200 levels 5 dmg 0x7d
0x0003a390,spell_create_heal_3a390,spell 1 state 3 mana 1000 levels 0x15
0x0003a360,spell_create_spell2_3a360,spell 2 (level.py "Alliance"; its handler is a speed boost) state 6 mana 1000 levels 0xfb
0x0003a2e0,spell_create_possession_3a2e0,spell 3 state 9 mana 0x32 levels 3
0x0003a450,spell_create_shield_3a450,spell 4 (table B idx 4)
0x0003a5d0,spell_create_beyond_sight_3a5d0,spell 5
0x0003a3f0,spell_create_earthquake_3a3f0,spell 6
0x0003a480,spell_create_meteor_3a480,spell 7
0x0003a630,spell_create_volcano_3a630,spell 8
0x0003a5a0,spell_create_crater_3a5a0,spell 9
0x0003a3c0,spell_create_teleport_3a3c0,spell 10
0x0003a510,spell_create_rubber_band_3a510,spell 11
0x0003a570,spell_create_invisible_3a570,spell 12
0x0003a540,spell_create_steal_mana_3a540,spell 13
0x0003a4b0,spell_create_rebound_3a4b0,spell 14
0x0003a4e0,spell_create_lightning_3a4e0,spell 15
0x0003a660,spell_create_skeleton_3a660,spell 17
0x0003a600,spell_create_thunderbolt_3a600,spell 18
0x0003a420,spell_create_mana_magnet_3a420,spell 19
0x0003a690,spell_create_fire_wall_3a690,spell 20
0x0003a6c0,spell_create_reverse_speed_3a6c0,spell 21
0x0003a6f0,spell_create_smart_bomb_3a6f0,spell 22
0x0003a720,spell_create_mini_fireball_3a720,spell 23
0x00047130,spell_fireball_update_47130,class 12 state 0: while casting (+0x30>0) spawn projectile(s) from caster (+0x2a) with yaw/pitch/speed; sound; charge mana
0x00047420,spell_speedup_update_47420,class 12 state 6 (spell 2): caster playerrec+0xc speed = +0x80*2 or *3 and caster +0x7e; spawns trail effect; restores at end
0x00047760,spell_shield_update_47760?,class 12 state 12 = spell 4 phase 0 (not read)
0x00047840,spell_earthquake_update_47840?,state 18 = spell 6 (not read)
0x000479f0,spell_meteor_update_479f0?,state 21 = spell 7 (not read)
0x00047d40,spell_crater_update_47d40?,state 27 = spell 9 (not read)
0x00048490,spell_rebound_update_48490?,state 42 = spell 14 (not read)
0x00048510,spell_lightning_update_48510?,state 45 = spell 15 (not read)
0x000492e0,spell_mini_fireball_update_492e0?,state 69 = spell 23 (not read)
0x00046e70,spell_can_cast_46e70,caster mana(+0x8c)>=0 and health>=0 and (cost +0x84==0 or castle mana >= cost) and (+0x88 <= caster mana or mid-cast)
0x00046f20,spell_charge_mana_46f20,at cast start caster +0x84 = -spell total (+0x88); returns 1 when charged
0x00046f90,spell_projectile_origin_46f90,launch position (flags +0x11 bit0/bit1 = hand side) and move projectile + its +0x36 chain
0x000238b0,effect_explosion_update_238b0,countdown +0x1a then life +0xc; on impact: area damage; terrain hit (DAT_cdfb0 cell type 0x1a/10/0xb -> FUN_32150); random +0x2e; sound; anim advance
0x00023dc0,effect_volcano_update_23dc0,life +0xc; random area damage 11450 + sound each tick; at end spawn follow-up effect and delete
0x00024100,effect_white_smoke_update_24100,rise speed +0x7e (0x40..0x80); z+=speed clamped to ground; drift via pos_move_polar for 16 ticks; sprite +0x56 anim
0x000241f0,effect_black_smoke_update_241f0?,class 10 state 14 (not read)
0x00023c20,effect_fire_update_23c20?,class 10 state 6 Fire; calls thing_area_damage_11160
0x00025980,effect_mana_ball_update_25980,class 10 state 41 (type 0x27 Mana ball): owner change (+0x64 -> +0x90); pushed by +0x76; merges with other balls
0x00026680,effect_castle_build_update_26680,class 10 states 48/51: castle construction stages (+0x47 castle size index; 6-byte size table); state 0x33 -> flatten terrain FUN_348b0 -> state 0x34
0x00026f0e,effect_castle_build_s44_26f0e?,castle construction state 44: marks DAT_fdfb0 cells 0x80 (built) over w*h footprint; kills things in cells
0x00038810,effect_create_type2_38810,class 10 type 2 state 2 health 8 flags |0x20001
0x000389d0,effect_create_fire_389d0,type 6 state 6 health 0xf0
0x00038a70,effect_create_type7_38a70,type 7 state 7 health 12
0x0003894e,effect_create_splash_3894e,type 5 (entry 0x38950)
0x00038b0e,effect_create_mini_volcano_38b0e,type 8 (entry 0x38b10)
0x00038b6c,effect_create_volcano_38b6c,type 9 (entry 0x38b70)
0x00038c3e,effect_create_crater_38c3e,type 11 (entry 0x38c40)
0x00038d40,effect_create_white_smoke_38d40,type 13 health rnd%0x17+0x11
0x00038de0,effect_create_black_smoke_38de0,type 14 health rnd%0x21+0x1c
0x00038e80,effect_create_earthquake_38e80,type 15 health 0x80
0x00038f0e,effect_create_meteor_38f0e,type 17 (entry 0x38f10)
0x00038f60,effect_create_type16_38f60,type 16 health rnd%100+100
0x00039050,effect_create_type18_39050,type 18 health 10000
0x0003919c,effect_create_steal_mana_3919c,type 25 (entry 0x391a0)
0x000392fe,effect_create_wall_392fe,type 0x1c Wall -> state 0x1e (entry 0x39300)
0x000393c0,effect_create_path_393c0,type 0x1d Path -> state 0x1f
0x00039360,effect_create_type30_39360,type 0x1e -> state 0x20
0x00039470,effect_create_canyon_39470,type 0x1f Canyon -> state 0x21
0x00039420,effect_create_type32_39420,type 0x20 -> state 0x22
0x00039770,effect_create_type33_39770,type 0x21 -> state 0x23
0x000395a0,effect_create_teleport_395a0,type 0x22 Teleport -> state 0x24
0x0003953e,effect_create_ridge_node_3953e,type 0x32 Ridge node (entry 0x39540)
0x0003983e,effect_create_mana_ball_3983e,type 0x27 Mana ball -> state 0x29 (entry 0x39840)
0x0003992e,effect_create_wizard_3992e,type 0x2d Wizard (entry 0x39930)
0x000448b0,projectile_homing_update_448b0,class 9 state 1: steer to target (+0x92); move; hit mana/wizard; lifetime; on end spawn explosion with owner/yaw/pitch and delete
0x00044fc0,projectile_lightning_update_44fc0,class 9 state 9: unlink; step until cell change; spawn zig-zag bolt segments (class 9 type 9 state 14); collision -> hit effect with +0x92 target and +0x2c damage (quartered vs shielded player)
0x00043f30,projectile_steer_to_target_43f30,+0x22 = angle to target; pitch via pos_pitch_to
0x000440a0,projectile_record_hit_stats_440a0,owner player record +0x157 shots / +0x15b hits
0x00044ea0,projectile_step_44ea0,pick target once (flag 2); move polar; collision test; ground clamp
0x00045f00,projectile_pick_target_45f00,auto-aim by projectile type (0 3 4 0x10 0x12 0x13 -> players in range) using owner descriptor sight radius
0x00043ea0,thing_z_add_half_height_43ea0,+0x4c += +0x4e unless type 2
0x00043ec0,thing_z_sub_half_height_43ec0,+0x4c -= +0x4e unless type 2
0x000405f0,player_dying_update_405f0,class 3 state 2: fall with +0x2e z-velocity (gravity -2); on ground: credit kill to +0x26 killer; death message (playerrec+0x3427+n*0x44 timer +0x3467=100); drop spells (rec+0x214[24] -> state+1 scattered); spawn mana balloon; flag 0x20
0x000416d0,player_respawn_start_416d0,class 3 state 6 -> state 4 (+0x30=0 +0x32=5)
0x00042530,balloon_update_42530,class 3 state 9: fly to target (+0x92): collect own mana ball (+0x8c) or deliver mana to castle (class 3 with +0x1a>0)
0x00042770,player_take_damage_42770,apply +0x5a/+0x5e; playerrec+0x189=4 (hit flash); returns 2 when dead (+0x26=killer)
0x00042010,castle_mana_tick_42010?,castle (+0x1a>0) moves 10% of +0x88 to balloon; updates home pos (+0x96) (partially read)
0x0004cd7a,math_isqrt_4cd7a,integer sqrt (Newton from seed table FUN_0004cdb0)
0x0004cc33,math_atan2_4cc33,atan2(dx dy) -> 0..0x7ff using 256-entry table at 0x9b3ec
0x0003e970,pos_dist_sq_xy_3e970,dx*dx+dy*dy of two pos
0x0003e930,pos_dist_xy_3e930,sqrt(dx*dx+dy*dy)
0x0003e8a0,pos_dist_xyz_3e8a0,sqrt(dx2+dy2+dz2)
0x0003e6b0,pos_angle_to_3e6b0,atan2 yaw from pos a to b
0x0003e6e0,pos_pitch_to_3e6e0,pitch angle from dz and horizontal distance
0x0003e770,angle_diff_3e770,|a-b| mod 0x800 folded to 0..0x400
0x0003e7a0,angle_turn_dir_3e7a0,sign (+1/-1) of shortest turn from a to b
0x0003e800,angle_turn_step_3e800,min(|diff| maxTurn)*dir
0x0003e420,pos_move_polar_3e420,x += sin(yaw)*d; y -= cos(yaw)*d; z -= sin(pitch)*d; sin table 0x987ec cos 0x98fec (0x800 entries 16.16)
0x0003e560,pos_follow_ground_3e560,adjust z toward ground+clearance with speed limit; returns 1 if changed
0x00010bc0,terrain_height_at_10bc0,wrapper of FUN_00071e00(x y) -> terrain height at world pos
0x00010480,terrain_type_mask_at_10480,1<<DAT_000cdfb0[cell] (cell type map 256x256); default 0x800000
0x000103d0,terrain_cell_flag_bit_103d0,1<<(DAT_000fdfb0[cell]&0xf) (cell flags map)
0x000313a0,terrain_finalise_heightmap_313a0,scale u16 scratch map (DAT_0010dfb0) to 0..0xc4 bytes in DAT_000ddfb0 and zero the scratch (which then becomes the cell->thing map)
0x0003d940,terrain_find_cell_spiral_3d940,spiral over cells until terrain_modify_cell_3d620 predicate succeeds
0x0003d620,terrain_modify_cell_3d620,raise/lower heightmap cell by delta (clamped 0..200); update flag map DAT_000fdfb0; redraw via FUN_32760/FUN_324e0
0x00049720,sound_play_at_thing_49720,3D positional sound: distance/angle from listener to thing idx -> volume/pan (DAT_9e320/9e321 enable flags)
0x0001fab0,ui_draw_frame_1fab0,per-frame screen composition: switch on player view mode (playerrec+0x446): 0 = 3D view (viewport renderer HUD) 2 = spell selection screen (24 icons mouse hover) 4 = map/scoreboard; then credits ticker (cfg+0xa1..0xa7) when cfg flag 4
0x0002f3c0,render_set_viewport_2f3c0,DAT_000b5810 = byte offset of 3D viewport in screen buffer (DAT_0012ed74 pitch DAT_0012ed70) from border size; FUN_78dd5 sets draw target
0x0002f320,render_set_viewport_fullwidth_2f320,viewport variant for map view
0x0004abe0,ui_select_font_4abe0,DAT_000adfbc = font table DAT_000adf28[n]
0x0004abc0,ui_font_space_width_4abc0,font+0xca
0x0004abd0,ui_font_height_4abd0,font+0xcb
0x0004a9a0,ui_draw_text_4a9a0,draw string (6-byte glyph records; 9/0x20 space 0xa newline) clipped at x 0x27f; returns end x
0x000603f0,vga_draw_rect_outline_603f0,4 x draw_line FUN_6abbc
0x000604c0,vga_draw_rect_outline_604c0?,variant used in the other video mode
0x000606c0,vga_fill_rect_606c0,FUN_6070d(x y w h colour)
0x00060688,vga_fill_rect_60688?,low-res variant
0x000224e0,ui_draw_sprite_224e0,blit span-encoded sprite (ptr at [esp+0xc]) at x y; half size in mode 1
0x00022820,ui_draw_spell_icon_22820,draw_sprite + FUN_22610
0x00022870,ui_draw_spell_slot_22870,spell icon with charge/level bar for spell thing (+0x30/+0x32); player colour from DAT_97630
0x00022d80,ui_draw_spell_slot_dim_22d80?,spell slot variant for unavailable spells
0x00043610,ui_draw_minimap_frame_43610,round minimap window (circle profile DAT_cdcb0 rotation zoom)
0x00042a20,ui_draw_minimap_42a20,draw player castle (+0x32 of player rec) mana balls into the minimap
0x000219f0,ui_draw_status_bars_219f0,HUD bars (castle/mana/health) for player thing; player colours DAT_97630
0x00021370,ui_draw_scoreboard_21370,per-player boxes/ranking (map screen)
0x0004ad80,ui_draw_debug_overlay_4ad80,text stats when playerrec+0x3410 bit 8
0x000494b0,sound_update_494b0,service sound/music channels (DAT_b7ac0 records) each tick
```

## 2. Notes for docs/ENGINE.md

### 2.1 Thing pool and indices

- Pool: 1000 records of 0xa4 bytes. `thing_base = state+0x7463` is the index-0 sentinel; thing index i is at
  `state+0x7463 + i*0xa4`, so the first real thing (index 1) is at state+0x7507 and the pool ends at state+0x2f503.
  All inter-thing links are u16 indices relative to this base; index 0 = none. Loops of the form
  `p = base + idx*0xa4; while (p != base)` are index-list walks.
- Free stack: `state+0x28` = top (int, -1 when empty), `state+0x251` = int[1000] of thing pointers.
  `thing_pool_reset_35460` pushes indices 1000..1 so index 1 is allocated first.
  Second "recyclable" stack `state+0x11f1`/`state+0x11f5` holds live things with flags 0x20400; `thing_alloc_35560`
  frees one of those when the free stack is empty.
- `thing_mark_delete_3e3e0` only sets flag 0x400; `thing_update_all_3dce0` frees flagged things at the start of the
  next tick (handlers can still reference them within the tick).
- Per-tick lists (singly linked via +0 `next`, terminated by `state+0x7463`), rebuilt each tick in
  `thing_update_all_3dce0`, heads in the config block `DAT_000adf74`:
  - `cfg+0x8e1e + type*4` (20 heads): creatures (class 5) by creature type (+0x41), excluding segments (+0x46==120)
    and dead ones (+0xc<0).
  - `cfg+0x8e6e`: players (class 3, alive, flag 0x10 clear).
  - `cfg+0x8e72`: effects type 0x27/0x28 (mana balls). `cfg+0x8e76`: effects type 0x2d (wizards).
  - `cfg+0x8e7a`: class 9 (projectiles).

### 2.2 Thing struct (0xa4 bytes) - recovered offsets

| off | type | meaning | evidence |
|---|---|---|---|
| +0x00 | ptr | next in per-tick class list | 3dce0 list building |
| +0x04 | u32 | per-thing RNG seed / unique id (`x*0x24a1+0x24df` LCG) | alloc: index + state RNG; handlers advance it |
| +0x08 | i32 | max health (300 default; dragon 9000, castle 40000, player 10000) | creates, 35080 |
| +0x0c | i32 | health / remaining life (effects use it as lifetime; <0 = dead) | 18870, 238b0, 405f0 |
| +0x10 | u32 | flags: 1 ?, 2 initialised/impact done, 4 linked in cell map, 8 collidable (default), 0x10 skip player list, 0x20 dead, 0x40 mana ball being collected, 0x80 sound/shield, 0x100/0x200 projectile hand side, 0x400 marked for delete, 0x2000 processed (class 9), 0x10000 no area damage, 0x20000 recyclable, 0x40000 spell dropped | 3e3e0, 3e250, 105f0, 42530, 354c0, 405f0 |
| +0x14 | u16 | next thing index in same cell | 3e250/3e330 |
| +0x16 | u16 | prev thing index in same cell | 3e330 |
| +0x18 | u16 | owner thing index (self for independent things; projectiles/effects copy the owner's) | alloc, 43ba0, 23dc0, 44fc0 |
| +0x1a | i16 | per-type: z velocity (creatures 19c30), timer (effects/switch), castle level (+0x1a>0 = castle), spell aim byte | 19c30, 238b0, 4a350, 42530 |
| +0x1c | u16 | property flags: bit0 damageable/solid, bit1 castle/wizard; castle 0x21, dragon 1 | 35bbc, 35090, 11160 |
| +0x1e | u16 | yaw (0..0x7ff, 2048 units per turn) | 3e420, 181e0 |
| +0x20 | u16 | pitch | 3e420, 47130 |
| +0x22 | u16 | target yaw | 1a0e0, 43f30 |
| +0x24 | u16 | target pitch | 448b0 |
| +0x26 | u16 | killer thing index (set when health<0) | 18870, 42770, 3da30 |
| +0x28 | u16 | last attacker thing index | 18870 |
| +0x2a | u16 | caster thing index (spell things) | 47130, 22870 |
| +0x2c | u16 | damage value (100 default, 2000 wizard, 0x7d fireball) | alloc, 3a210, 44fc0 |
| +0x2e | i16 | z velocity (player fall) / random jitter | 405f0, 238b0 |
| +0x30 | i16 | spell: remaining cast ticks; creature: distance for sound | 47130, 46960 |
| +0x32 | i16 | spell: total duration/levels; castle: hit timer | 3a210, 11450 |
| +0x34 | u16 | parent / leader thing index (segments -> head; flocking) | 362d0, 18050, 1a0e0 |
| +0x36 | u16 | child / next segment thing index | 362d0, 46960, 18870 |
| +0x38 | u16 | speed (0x60 creatures) | 362d0 |
| +0x3a | u8 | anim/sound countdown timer | 362d0, 46960, 18870 |
| +0x3b | u8 | secondary timer | 46960 |
| +0x3c/+0x3d/+0x3e | u8 | spell flags / burst counter | 3a210, 47130 |
| +0x3f | u8 | tick counter (++ after each handler call; set from per-type counter at create) | 3dce0, 1a0e0 (anim phase) |
| +0x40 | u8 | class (0 free, 2 scenery, 3 player, 5 creature, 7 weather, 9 projectile, 10 effect, 11 switch, 12 spell) | dispatch |
| +0x41 | u8 | type = level-file Model (creature type, spell id, effect type, player type) | creates |
| +0x42 | u8 | collision filter class (0xff any) | 105f0 |
| +0x43 | u8 | collision filter type (0xff any) | 105f0 |
| +0x44 | u8 | 10 default | alloc, 47130 |
| +0x45 | u8 | 0 | 47130 |
| +0x46 | u8 | state = dispatch index into class table A (handler per state) | 3dce0, 3ea50 |
| +0x47 | u8 | castle size index (wizard/castle effects) | 35090, 26680 |
| +0x48 | u16 | pos x (world units; cell = x>>8, 256x256 cells) | 3e250 |
| +0x4a | u16 | pos y | 3e250 |
| +0x4c | i16 | pos z (height) | 3e250, 19c30 |
| +0x4e | i16 | bbox half extent z / ground offset | 35240, 43ea0, 10530 |
| +0x50 | i16 | bbox half extent x (also area-damage radius) | 35240, 10d20 |
| +0x52 | i16 | bbox half extent y | 35240 |
| +0x54 | i16 | bbox half height | 35240, 47130 |
| +0x56 | u16 | sprite id (incremented for animation by some effects) | 35240, 24100 |
| +0x58 | u8 | current frame | 3ea70 |
| +0x59 | u8 | frame count / palette index from sprite table | 35240 |
| +0x5a | i32 | pending damage | 10d20, 18870 |
| +0x5e | u16 | pending damage source (thing index) | 10d20, 18870 |
| +0x60/+0x64 | i32/u16 | mana ball: pending owner change | 25980 |
| +0x76 | u16 | mana ball: pusher thing index | 25980 |
| +0x7e | i16 | speed (0x10 default; dragon 0x1e; smoke rise speed; player speed) | alloc, 24100, 47420 |
| +0x80 | i16 | base speed / castle area | 47420, 35090 |
| +0x82 | u16 | 0x10 (turn rate?) | 362d0 |
| +0x84 | i32 | spell: mana cost (negative while charging) | 3a210, 46e70, 46f20 |
| +0x88 | i32 | total mana (castle/owner accumulator; spell: total mana) | 427d0, 3a210 |
| +0x8c | i32 | mana carried / mana per level | 35230, 428e0, 26120 |
| +0x90 | u16 | mana owner thing index (0 = unowned) | 428e0, 26120, 10730 |
| +0x92 | u16 | target thing index | 1a830, 448b0, 42530 |
| +0x96..+0x9b | u16 x3 | home/origin position (castle: cell-aligned; projectile: launch pos) | 35bbc, 47130, 42010 |
| +0x9c | ptr | sprite/creature descriptor (default DAT_00096a10; dragon DAT_00096b90, worm 96bf0 ...; fields +0xa/+0xc height clearance, +0x10, +0x14 terrain mask, +0x1a anim period, +0x1c sight radius, +0x1e fov) | 362d0, 181e0, 1a0e0 |
| +0xa0 | ptr | owner player record (default dummy DAT_000b6e90; record fields +0xc speed, +0xe, +0x30 player number, +0x32 castle thing idx, +0x134/+0x142 mana, +0x146 aim, +0x157/+0x15b stats, +0x187/+0x189 HUD flash, +0x1cc[] threat table, +0x214[24] spell thing idx, +0x2a4[] spell icon table, +0x394[] spell flags) | 427d0, 47130, 405f0 |

Position struct passed around (`DAT_000adfc4` scratch): `{u16 x, u16 y, i16 z}`; cell coordinates are the high
bytes (`+1`, `+3`). World = 256x256 cells x 256 units.

### 2.3 Class / state dispatch tables (0x943da)

Class table record (18 bytes): +0 table A (update handlers, indexed by **state** +0x46), +4 table B (create
handlers, indexed by **type** +0x41 = level Model), +8 parent ptr, +12 u16 = class id + 1.
Both tables use 14-byte records {parent 0x943cc, u16 model, handler, enabled}.
Create handlers are called via `thing_create_35690(class, type)`; they call `thing_alloc_35560`, fill the
fields, `thing_set_sprite_35240`, `thing_link_cell_3e250`, `thing_restore_health_35080`.

State ranges (table A) from the initial state written by each create handler:

| class | states | type -> initial state |
|---|---|---|
| 2 Scenery | 18 | type*3: Tree 0, Standing stone 3, Dolmen 6, Bad stone 9, dome 12, dome 15 (3 states each: normal/burning/burnt) |
| 3 Player | 11 | type 0 flyer -> 0, type 1 -> 1, type 2 castle -> 5, type 3 -> 7; state 2 = dying fall (405f0), 4/6 respawn (416d0), 9 = mana balloon (42530) |
| 5 Creature | 121 | Dragon 1, Vulture 7, Bee 13, Worm 19, Archer 25, Crab 31, Kraken 37, Troll 43, Griffon 49, Skeleton 54, Emu 61, Genie 66, Builder 73, Townie 79, Trader 85, type15 91, Wyvern 97; 120 = body segment (0x18050); 102..119 disabled |
| 7 Weather | 5 | Wind type 4 -> state 4; all handlers = ret (0x4b570) |
| 9 Projectile | 21 | type == state (0..20); 1 homing (448b0), 9 lightning (44fc0), 14 lightning segment |
| 10 Effect | 62 (gaps 39,47,49,50) | types <=0x13: state==type; 0x17, 0x19..0x1b same; 0x1c Wall->0x1e, 0x1d Path->0x1f, 0x1e->0x20, 0x1f Canyon->0x21, 0x20->0x22, 0x21->0x23, 0x22 Teleport->0x24, 0x24->0x26, 0x25->0x27, 0x26->0x28, 0x27 Mana ball->0x29 (0x25980), 0x28->0x2a; castle building states 0x2a..0x3d (0x25f10..0x27e90; 0x26680 for 48/51) |
| 11 Switch | 32 | create handlers are 32-byte stubs (not functions yet); state 3 repeat trigger, 5 one-shot |
| 12 Spell | 72 | state = spell*3 + phase; phase 0 = spell-specific cast handler, phase 1 = 0x472f0, phase 2 = 0x47300 (common; dropped spell / pickup) |

Level-file THING_INIT (18 bytes: Class, Model, Xpos, Ypos, DisId, SwiSz, SwiId, Parent, Child) is copied into the
game state at `state+0x442` (level buffer) and `state+0x2f945..0x385d3`; records with DisId == -1 are spawned at
level start (`level_spawn_effect_record_34e00` for class 10), the rest by `switch_activate_356e0` when a switch
with that DisId fires. Position = (Xpos<<8, Ypos<<8, terrain height). Parent/Child are 1-based record indices
used by `level_build_linked_feature_34c40` for wall/path/canyon/ridge chains.

### 2.4 Algorithms

- `thing_update_all_3dce0`: advance state RNG; if !(cfg->flags2 & 1) { free things flagged 0x400; clear list
  heads; one pass building class lists; creature_sound_tick (unless cfg flag 0x10); hit_record_scores_150f0;
  mana_totals_update_427d0; second pass: for each live thing `rec = tableA[class] + state*0xe; if rec.model==state
  { if rec.enabled rec.handler(thing); thing->counter++ } else { FUN_000603bc(); thing_mark_delete }` }.
  It is called 1/4/16 times per tick depending on cfg+0x96, i.e. the simulation sub-step count.
- Cell map: `DAT_0010dfb0` = u16[256*256] head thing index per cell (index = celly*256+cellx). Doubly linked via
  +0x14/+0x16. Spatial queries (`thing_find_collision_105f0`, `thing_find_mana_near_10730`, area damage) walk
  cells in a spiral (`spiral_search_*`) and then the cell list, testing `math_bbox_overlap_10530` with the
  +0x4e..+0x54 extents.
- Terrain maps (all 256x256 bytes): `DAT_000ddfb0` height (0..0xc4), `DAT_000cdfb0` cell type (water=1 ...),
  `DAT_000fdfb0` cell flags (bit 0x80 = castle footprint, low nibble = type bits). `DAT_0010dfb0` is reused as
  u16 scratch during generation (`terrain_finalise_heightmap_313a0` zeroes it before use as the cell map).
- Damage: dealers write `+0x5a` (amount, accumulated) and `+0x5e` (attacker index). The victim applies it on its
  next update (creatures in `creature_ai_step_18870`, players in `player_take_damage_42770`, trees in
  `scenery_tree_update_43ba0`), records `+0x28` and, on death, `+0x26`.
- Mana economy: creatures carry +0x8c (half their max health); mana balls (effect 0x27) hold +0x8c with owner
  +0x90; balloons (class 3 state 9) ferry mana to the castle (class 3 type 2, +0x1a = level); the castle's
  +0x8c is what `spell_can_cast_46e70` compares with the spell cost (+0x84). `mana_totals_update_427d0`
  recomputes per-owner totals (+0x88) every tick and `game_check_level_won_3db20` compares castle mana against
  the level's requirement (`state+0x38c93` percent of `cfg+0x5e`).
- `ui_draw_frame_1fab0` (6196 bytes) is **not** simulation: it is the per-frame composition. It selects the
  viewport (`render_set_viewport_2f3c0`), calls the renderer `FUN_0002f6e0`, then draws the HUD (minimap
  43610/42a20, status bars 219f0, spell slots 22870, per-player message queue at playerrec+0x3427 with timers at
  +0x3467, text via ui_draw_text_4a9a0), handles the spell-selection screen (view mode 2: 24 icons at x
  0x180..0x27f step 0x40, mouse `DAT_0009e5dc/0x9e5de`, hovered slot in cfg+0x16) and the map/scoreboard screen
  (mode 4), and finally the credits ticker (`PTR_s_Designed_by_0009861c`) when cfg flag 4 (demo/attract).

### 2.5 Globals

- `DAT_000adf6c` game state: +4 RNG, +8 local player index, +10 player count, +0x28/+0x251 free stack,
  +0x11f1/+0x11f5 recyclable stack, +0x2198 view/border mode, +0x2199/+0x219a HUD enable flags, +0x340b.. player
  records (0x801), +0x442 level record buffer, +0x7463 thing base, +0x2f945 level THING_INIT copy, +0x38c93 win %,
  +0x38c9f creature count.
- `DAT_000adf74` config: +0x16 hovered spell slot, +0x5e level total mana, +0x5f/+0x63 options, +0x96 sub-steps,
  +0xa1..+0xa7 credits ticker state, +0xbc total mana, +0x8e1e.. list heads.
- `DAT_0010dfb0` cell->thing map; `DAT_000ddfb0` heightmap; `DAT_000cdfb0` cell type; `DAT_000fdfb0` cell flags;
  `DAT_000ade28` spiral ring tables; `0xac160` spiral iterators; `DAT_000987ec/0x98fec` sin/cos (16.16, 2048
  entries); `0x9b3ec` atan table; `DAT_000adfc4` position scratch; `DAT_000adfbc` current font; `DAT_0012ed74`
  screen buffer, `DAT_0012ed70` pitch, `DAT_0012ed78` height; `DAT_000b5810` viewport offset; `DAT_0012edae` game
  mode (bit 0 = low-res/half-size layout); `DAT_00097630` player colours; `DAT_00097660` spell screen order.
- Sprite table at 0x97676 (14-byte records: +8/+10 half extents, +0xe palette index) via thing_set_sprite_35240.

### 2.6 Open questions

- Player types: 0 is the flying wizard (used by switches/AI targeting), 2 is the castle (40000 HP, 0x21 flags);
  the roles of types 1 and 3 (10000 HP) and of the level "Flyer1..8" models 4..11 (table B entries 0x359c0..0x35aa0,
  32 bytes each, not read) are unconfirmed.
- Spell 2: level.py calls it "Alliance" but its cast handler (0x47420) multiplies the caster's speed by 2/3 ->
  likely "Accelerate"; check the save-game spell name table.
- Phase-1/2 spell handlers 0x472f0/0x47300 and the switch create stubs 0x39e10.. are not functions in the export.
- Class 9 type numbering vs spells (type 0 fireball?, 1 homing, 9 lightning, 10, 12, 13, 14, 18, 19) needs the
  cast handlers to be read; `FUN_00045f00` auto-aim groups types {0,3,4,0x10,0x12,0x13}.
- Descriptor struct at +0x9c (DAT_00096a10.. per creature) deserves a full layout pass; +0x1a is the anim
  period used as the "think every N ticks" divisor.
- `hit_record_scores_150f0` writes playerrec+0x1cc[8-byte entries]: probably AI threat/aggression weights.
