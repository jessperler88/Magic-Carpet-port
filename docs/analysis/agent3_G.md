# Agent 3, region G: remaining unnamed functions in 0x10000-0x5e000 (2026-10-06)

77 functions read (todo3_G.txt). Result: 59 named (6 of them low confidence `?`), 16 FRAGMENT, 2 DATA.
Most entries are cleanup-pass creations: fall-through continuations of Table-A handlers, switch-case
bodies, 23-byte AI mode setters, DOS/HMI thunks and a few dead editor/debug routines.

```csv
0x00010b34,FRAGMENT,"tail of thing_exists_near_pos_10ac0: jumps back into 0x10af5/0x10af7/0x10b2c (2x2 cell scan via g_cell_things, type match + pos_dist_xyz <= 0x80 -> 1), shared epilogue pops ebp/edi/esi/ebx it never pushed"
0x00010be0,terrain_height_at_offset_10be0,"(short* pos, yaw, dist): terrain_height_at_71e00 at (pos.x + (sin(yaw)*dist>>16), pos.y - (cos(yaw)*dist>>16)); sin = g_trig_table[(yaw&0x7ff)+0x100] (0x987ec), cos at 0x98fec; no callers"
0x00011ce0,stub_return_zero_11ce0,"compiled-out function: full prologue (push ebx/esi/edi, sub esp 8), xor eax, epilogue; returns 0; sits between castle_site_clear_at_pos_11be0 and caseD_0; no callers"
0x00012a80,ai_mode8_thunk_12a80,"thunk: ai_mode8_attack_wizard_12a90(player thing); reached from the AI mode dispatch in player_type1_s1_update_11de0 (modes 9 / 0xd share the attack-wizard handler)"
0x000135e0,ai_goal_disabled_135e0,"calls ai_has_any_attack_spell_154e0(player) then returns 0 regardless (test al,al; xor eax): a disabled goal evaluator between ai_goal_attack_type3_13440 and ai_goal_join_hunt_13600; no callers"
0x00013880,ai_set_mode0_13880,"AI mode setter: playerrec P+0x19f (AI mode) = 0 (choose goal); returns 1; no callers (same shape as ai_set_mode2_138c0)"
0x000138a0,ai_set_mode1_138a0,"P+0x19f = 1 (upgrade castle: ai_mode1_upgrade_castle_12470); returns 1; from ai_choose_goal_12330"
0x000138e0,ai_set_mode3_138e0,"P+0x19f = 3 (fly to castle site: ai_mode3_fly_to_castle_site_12560); returns 1; from ai_choose_goal_12330"
0x00013920,ai_set_mode5_13920,"P+0x19f = 5 (mode 5 handler FUN_00012550 returns 0 = unused); returns 1; no callers"
0x00013940,ai_set_mode6_13940,"P+0x19f = 6 (collect mana: ai_mode6_collect_mana_12830); returns 1; from ai_choose_goal_12330"
0x00013960,ai_set_mode7_13960,"P+0x19f = 7 (attack castle: ai_mode7_attack_castle_12950); returns 1; from ai_choose_goal_12330"
0x00013980,ai_set_mode13_13980,"P+0x19f = 0xd (hunt creature, handled by ai_mode8_attack_wizard_12a90); returns 1; from ai_choose_goal_12330"
0x000139a0,ai_set_mode9_139a0,"P+0x19f = 9 (attack type-3 player, handled by ai_mode8_attack_wizard_12a90); returns 1; from ai_choose_goal_12330"
0x000139c0,ai_set_mode8_139c0,"P+0x19f = 8 (attack wizard: ai_mode8_attack_wizard_12a90); returns 1; from ai_choose_goal_12330"
0x00013a00,ai_set_mode11_13a00,"P+0x19f = 0xb (return home: ai_mode11_return_home_126f0); returns 1; from ai_choose_goal_12330"
0x00014080,thing_signature_14080,"(thing) -> u16 (cls<<7) + type + owner idx (+0x18): target signature the AI goals store in player thing +0x94 and ai_target_valid_140a0 re-checks; 11 callers (all ai_goal_*)"
0x00015420,ai_set_dodge_steer_15420,"P+0x10 (steering value, decays 4/tick) = 0x50: full-rate dodge when ai_find_incoming_projectile_153b0 reports a threat; from player_ai_wizard_tick_11f20"
0x0001884f,FRAGMENT,"tail of creature_idle_seek_leader_18610 (which ends exactly here): thing_set_state_3ea50(thing, state) then add esp 0x10 and pops of ebp/edi/esi/ebx"
0x0001b233,FRAGMENT,"mid-instruction start (bytes 00 c1 fa 1f f7 f9) inside the kraken s38 continuation already recorded as FRAGMENT at 0x1b231; real flow resumes at 0x1b239 (burst counter +0x47=5, one projectile per tick type 0x17 desc DAT_00096ad0 dmg 800)"
0x0001c8a0,skeleton_set_pose_f5_1c8a0?,"(thing): thing_set_sprite_35240(thing, 0xf5); +0x47 = 1; called by creature_skeleton_s55_update_1bb70 when the +0x1a countdown reaches 0 (start of the villager-conversion pose)"
0x0001c8c0,skeleton_reset_convert_timer_1c8c0,"(thing): +0x47 = 0, +0x1a = 400, thing_set_sprite(thing, 0xc9); from skeleton_convert_villager_1c1e0 when the negative +0x1a has counted back up"
0x0001ddb2,FRAGMENT,"fall-through continuation of creature_builder_s74_update_1dc20 (ends exactly here): creature_move_step_181e0; every desc+0x1a/2 ticks: --+0x1a, if 0 or target gone +0x1a=5 and set_state(saved state+1); target_yaw toward target +0x92; within 0xa00 -> +0x1a=0 set_state(saved state); reads the caller's [esp] and pops 4 regs"
0x0001e2f1,FRAGMENT,"mid-instruction start (push ebx; aas) of the continuation of creature_townie_s79_update_1e140: every desc+0x1a ticks, if no target: target_yaw += random (+/-)(rng&0xff + 0x55) and pick the nearest wizard from g_cfg->wizard_list as target +0x92 (speed_cur = speed_base); else see 0x1e41e"
0x0001e41e,FRAGMENT,"jump target inside the 1e2f1 body: if target +0x92 is class 10 type 0x2d (mana ball): beyond 0x800 turn toward it (0x1e4ad) else if ball +0x1a < speed_base set_state(saved), +0x1a=1, ball +0x1a++; otherwise drop target and speed_cur = turn_rate (+0x82)"
0x0001e4ad,FRAGMENT,"tail inside 0x1e41e: pos_angle_to_3e6b0(self pos, target pos) -> target_yaw +0x22, then the shared epilogue (add esp 0x10, pop ebp/edi/esi/ebx)"
0x0001e75b,FRAGMENT,"fall-through continuation of creature_trader_s85_update_1e5c0 (ends exactly here): same wander / nearest-wizard / mana-ball code as 0x1e2f1 but the wizard must be farther than sqrt(0xe100000) (dist^2 > 0xe100000); ends in lea/nop padding at 0x1e973"
0x0001ea52,FRAGMENT,"body of creature_trader_s90_update_1ea50, which the export truncates to its first instruction (push 0x5a): creature_idle_seek_leader_18610(thing, 0x5a); add esp 8; ret"
0x0001ef50,creature_reset_speed_sprite0_1ef50,"(thing): speed_cur (+0x7e) = speed_base (+0x80); thing_set_sprite_35240(thing, 0); from creature_type15_s92_update_1ecd0"
0x00023050,dbg_draw_thing_timers_23050,"(x, y, thing): sprintf(buf, format %d at 0x905bc, thing+0x3a timer_a) drawn with ui_draw_text_4a9a0 colour DAT_000acc18 at (x,y); +0x3b timer_b one line lower (ui_font_line_height_4abd0, colour DAT_000adc17); i16 +0x30 cast_ticks two lines lower (colour DAT_000adb18); 256-byte stack buffer; dead debug overlay"
0x00025c2c,FRAGMENT,"switch-case body of effect_mana_ball_update_25980 (ENGINE: 0x25c2f), entered only when +0x3a != 0: rolling-ball physics - clamp vx/vy (+0x96/+0x98) to +/-0x40, pos scratch DAT_000adfc4 = pos+v, z_vel -= 0x10 (min -0x80), ground via terrain_height_at_10bc0: bounce z_vel = -z_vel/4 (0 when < 0x11); thing_move_to_3e1d0; on ground: merge with another owner's ball (thing_find_collision_other_owner_10980 -> mana_ball_merge_26120), slope push terrain_slope_vector_3e4b0 added to v, friction v = v*250/256; mana_ball_update_sprite_25e20"
0x000280ad,FRAGMENT,"tail of effect_type55_s57_update_28050: if the spawned thing (eax) != 0 copy owner +0x18 from self, then thing_mark_delete_3e3e0(self); pops ebp/edi/esi/ebx"
0x000282ca,FRAGMENT,"loop body of effect_type57_s59_update_28270 (jumps back to 0x282a2): for every list thing of type 0x27 within pos_dist_sq_xy < 0xc40000 set damage slot 4 (+0x72 amount = 100, +0x76 attacker = self idx)"
0x000284cf,dbg_vga_dac0_blue_284cf?,"asm, entry is one 00 byte early - real start 0x284d0: push ebx; xor ch,ch; mov bl,0x3f; xor cl,cl; out 0x3c8,0; out 0x3c9 r=0 g=0 b=0x3f; pop ebx; ret -> palette entry 0 = pure blue (debug flash of the border/background colour); no callers"
0x000342e0,level_build_wall_342e0,"(x0,y0,x1,y1 cells) via level_build_linked_feature_34c40 for Effect type 0x1c Wall: dx,dy = math_wrap_diff_34250(.., 0x100); swap endpoints so dx >= 0; n = max(|dx|,|dy|)/10 + 1 steps; each step: terrain_set_pos_scratch_34280 then thing_create_35690(DAT_000adfc4, 10, 0x1b) twice: state 0x1d with +0x1a = dx/n (x run, remainder on first step) and state 0x1b (dy<0) / 0x1c (dy>=0) with +0x1a = |dy|/n (y run); i.e. a staircase of wall-segment things between the two cells"
0x000353d0,thing_set_extents_353d0,"(thing, xy, h): ext_x +0x50 = ext_y +0x52 = xy; ext_h +0x54 = h; 43 callers (all constructors; thing_set_castle_extents_353f0 is the castle variant)"
0x00036200,thing_alloc_heal_36200?,"thing_alloc_35560 (args passed through) then thing_restore_health_35080 on the new thing; always returns 0; unused constructor stub between scenery_create_2d_dome_36190 and creature_create_dragon_362d0"
0x000385e0,FRAGMENT,"fall-through continuation of projectile_create_type17_38590 (ah = flags byte loaded at 0x385dd is used here): desc +0x9c = DAT_00096a50, flags byte &= ~8 (not collidable), thing_link_cell_3e250, thing_restore_health, sprite 0xd1, thing_set_extents(ext_x*2, ext_h*2); returns thing"
0x0003c150,level_save_file_3c150,"(level n, buf): sprintf(path, format %s/lev%05d.dat at 0x908d4, string levels at 0x90850, n); file_save_61db0(path, buf, 0x979c) == 0x979c (38812 bytes = the MC1 level file size, footer at 0x9790); dead level-editor leftover, no callers"
0x0003c7f0,file_access_3c7f0,"crt_access_61e68(path, 0) passthrough (0 = file exists); from init_early_3c800"
0x0003cc80,file_exists_3cc80,"crt_access_61e68(path, 0) == 0 as bool; from file_copy_3cca0"
0x0003e710,pos_pitch_from_dz_3e710?,"(z0, z1, dist): math_atan2_4cc33(z1 - z0, dist) -> 0..0x7ff; sibling of pos_pitch_to_3e6e0; no callers"
0x00040b50,player_set_under_attack_timer_40b50?,"(thing): if thing->player: P+0x2e = 100 (zeroed in player init 3f360); called when the player takes a hit (player_apply_hits_40b70, 3 sites) and when projectile_pick_target_45f00 selects a class 3 type 0 (human) player"
0x000414a3,FRAGMENT,"tail of player_flyer1_s4_update_413a0 (overlaps its last 3 bytes): state +0x46 = 5, +0x30 = 3, +0x32 = 0; pop esi/ebx"
0x00042000,castle_set_counter30_42000?,"(thing): i16 +0x30 = 2; counterpart of castle_begin_build_stage_41f00 which sets +0x30 = 4 when entering state 5; no callers"
0x000425d3,FRAGMENT,"mid-body of balloon_update_42530 (overlaps it): set flag 0x40 (being collected) and target +0x92 on the ball, thing_collide_105c0 -> collector +0x8c mana += ball +0x8c, mana_owner copied, ball health = max, thing_mark_delete; then terrain_height_at_10bc0 / pos_follow_ground_3e560 / thing_move_to_3e1d0 / player_take_damage_42770"
0x00046e50,spell_dropped_dispatch_46e50,"(thing): spell_dropped_update_46ae0(thing, type +0x41, state +0x46 - 1); the only callee of spell_phase1_common_472f0 (Table A states 1..70 of class 12)"
0x00047586,FRAGMENT,"tail of spell_speedup_update_47420 (ends exactly here): target P+0xc (desired speed) = self speed_base; self speed_cur = owner P+0xc; clear flag 0x80 on the target thing; pop esi/ebx"
0x00049c20,sound_priority_ok_49c20,"(new, cur) -> new - cur >= -8; sound_request_49720 (5 sites) compares the request value against the playing record word DAT_000b7ac4 + id*10 and only replaces the sound when within 8 below it"
0x00049ca0,music_fade_timer_cb_49ca0,"far routine (retf): the 60 Hz HMI timer event installed by music_fade_out_begin_49cd0 (hmi_timer_add_event_5d093): --DAT_000943c8 (MIDI volume), snd_midi_set_volume_5ef56(it); at 0 calls music_fade_out_end_49d10"
0x00049d80,sound_sample_done_49d80,"(owner thing, sample id) -> if sfx on (DAT_0009e320 && DAT_0009e321) finds the channel i in DAT_0012e070[32] {u16 owner, u16 sample} and returns hmi_sample_done_6291e(DAT_0012e1c0 digital handle, i); 1 when sound is off or no channel matches"
0x00049f70,con_get_text_position_49f70,"(int* row, int* col): splits the packed result of con_get_cursor_795b4 (Watcom graph.lib _gettextposition: row in low word, col in high word)"
0x0004b500,game_abort_if_null_4b500,"(p): if p == 0: vga_palette_fade_61510(0, .., 0x10), timer_isr_remove_4ad0a, sound_shutdown_617e0, music_shutdown_61870, game_shutdown_3ee70, crt_exit_6279f(1); out-of-memory style abort helper, no callers"
0x0004bec0,sprite_group_priorities_clear_4bec0,"mem_set_5afd0(DAT_000b9791, 0, 0x211): clears the per-sprite-group load-priority table; from switch_activate_356e0 and sprite_cache_shutdown_4bc80"
0x0004cae0,flag_set_false_4cae0,"(int* p): if p: *p = 0; option-callback style setter next to flag_toggle_4caf0 / flag_set_true_4cb40; no direct callers (table target)"
0x0004caf0,flag_toggle_4caf0,"(int* p): if p: *p = (*p == 0); no direct callers"
0x0004cb40,flag_set_true_4cb40,"(int* p): if p: *p = 1; no direct callers"
0x0004cdb0,DATA,"u16[32] isqrt seed table: seed[n] = round(sqrt(2^n)) = 1,2,2,4,5,8,0xb,0x10,0x16,0x20,0x2d,0x40,0x5a,0x80,0xb5,0x100,0x16a,0x200,0x2d4,0x400,0x5a8,0x800,0xb50,0x1000,0x16a0,0x2000,0x2d41,0x4000,0x5a82,...; math_isqrt_4cd7a indexes it by the highest set bit before Newton iteration"
0x0004ced4,gfx_copy_b400_4ced4,"asm (ebp frame, saves ebx/ecx/edx/esi/edi): rep movsd 0x2d00 dwords = 46080 bytes (320x144) from arg2 to arg1; no callers"
0x0004d0dd,ui_draw_icon_4d0dd,"asm (x, y, icon*{u8* data, u8 w, u8 h}): draws into DAT_0012ed74 with vga_draw_sprite_spans_lo_606f8 when DAT_0012edae&1 (320x200) or vga_draw_sprite_spans_6070d when &8 (640x480), extra args 0,0; NOTE a second asm routine begins at 0x4d140 (same but w,h divided by arg4+1 and arg4/arg5 passed as scale) and needs its own function entry"
0x00051eac,DATA,"tail of the front-end string block (0x510ca-0x51ec8): UK keyboard row strings (shifted digit row !..+ then QWERTYUIOP{} / ASDFGHJKL:@~ / |ZXCVBNM<>?) (this entry starts inside the third row) followed by lea/mov padding up to fe_init_state_51ed0"
0x00054850,fe_draw_save_slot_names_54850,"DAT_0009e516 = palette_find_nearest_60fc0(0x3f,0x3f,0); ui_push_clip_rect_58930; for slot 0..5: ui_set_clip_rect_588b0 from the 16-byte rect table at 0x51d3c + slot*16, ui_draw_text_58ab0(g_font1, 4, (rect.w - font height)/2, name) with names from the pointer table 0x9e4e8 (Game One .. Game Six); ui_pop_clip_rect_58950; no callers (load/save screen)"
0x00056670,fe_flic_loop_frame_56670,"plays the next flic chunk flic_play_chunk_50dfd(DAT_0012eccc, DAT_0012ed74) and stores the advanced pointer; frame counter DAT_0012ecd0 cycles 1..4, at 5 it restarts from the saved frame-1 pointer DAT_0012ecc8 (captured on the first frame); blits with vga_copy_320x200_610f0 (0xc8 rows) or vesa_copy_banked_4f974 (0x1e0); then busy-waits until g_tick >= start+16 unless a mouse click edge (DAT_0012ee0a/0c/0e) or key (DAT_0012eea0) is pending; no callers"
0x00057350,fe_draw_centred_icon_57350,"gfx_set_clip_window_65c10(0x41,0x4b,0xbd,0x2c); sprite record DAT_0012ec64 + 0x2a (w byte +0x2e, h byte +0x2f) drawn centred on (0xbd,0x2c): ui_draw_sprite_60688 in 320x200 mode, vga_fill_rect_606c0 (the hi-res sprite variant per names.csv) otherwise; restores the clip window. NOTE fe_reset_clip_320x200_573a0 starts mid-instruction (inside the call at 0x5739f); its body is this function's else branch and should be deleted"
0x0005ace8,cpu_detect_all_5ace8,"pushes eax..ebp + flags, cpu_identify_5ad03 then cpu_detect_fpu_5adfe, restores; from cpu_detect_5ac80"
0x0005af2c,dos_get_current_drive_5af2c,"(int* out): int 21h AH=19h; *out = AL + 1 (1 = A:); returns 0; from dos_is_cdrom_drive_10010"
0x0005af44,dos_set_current_drive_5af44,"(drive 1-based, int* out): int 21h AH=0Eh DL = drive-1; *out = AL (number of logical drives); returns 0; no callers"
0x0005b01a,crt_srand_5b01a,"srand(seed): seed pointer from FUN_0005aff2 (returns &DAT_0009e5c8, shared with crt_rand_5aff8); if non-null *p = seed; no callers"
0x0005bdd4,mouse_shutdown_5bdd4,"int 33h fn 0 (reset driver) through dos_int86_61f30; mem_free_59a10 of the cursor buffers DAT_0012ede8 / 0012edf8 / 0012edc8 / 0012edb8 when allocated; DAT_0009e5e4 (mouse present) = 0; no callers"
0x0005bfd0,input_keyboard_install_stub_5bfd0,"empty function (prologue, sub esp 0, epilogue); video_input_init_3ed60 calls it in place of input_keyboard_install_4fb46 when g_cfg->flags & 8 (skip-game mode)"
0x0005bfe4,input_keyboard_restore_stub_5bfe4,"empty function; game_shutdown_3ee70 calls it in place of input_keyboard_restore_4fb85 when g_cfg->flags & 8"
0x0005bff8,input_bios_read_key_ascii_5bff8,"calls the BIOS keyboard poll at 0x6622c (not a function in the export: int 16h AH=12h shift state -> DAT_0012eea1 bits 1 shift 2 ctrl 4 alt, AH=11h/10h -> scancode in DAT_0012eea0) then returns DAT_0009e5e8[DAT_0012eea0] (scancode -> ASCII table) when the scancode < 0x80, else 0; no callers"
0x0005c513,fli_file_read_chunk_to_work_5c513,"fli_stream_read_5cac0(fd DAT_0009e700, g_work, chunk size DAT_0009e6e4 - 6): reads the body of the current FLI chunk into the 64 KB work buffer; file-stream FLI player; no callers"
0x0005c531,fli_file_read_chunk_to_screen_5c531,"fli_stream_read_5cac0(DAT_0009e700, DAT_0012ed74 back buffer, DAT_0009e6e4 - 6); from fli_file_play_5c40b"
0x0005c54f,fli_file_read_chunk_to_work_5c54f,"byte-identical duplicate of 5c513 (work buffer target); from fli_file_play_5c40b"
0x0005cf2f,stub_noop_5cf2f,"empty function (prologue/epilogue only) between sound_stop_sample_5cea0 and music_song_done_5cf40; HMI callback placeholder; no callers"
0x0005cf79,stub_noop_5cf79,"empty function between music_song_done_5cf40 and snd_timer_init_5cf9b; no callers"
0x0005d5a9,hmi_timer_dispatch_events_5d5a9,"far routine (retf): per-PIT-tick service of the HMI timer system: for i in 0..15 with callback DAT_000a3c59[i*6] (or flag word at +4) non-zero: accumulator DAT_000a3d39[i] += increment DAT_000a3cf9[i]; when bit 16 (byte +2 & 1) is set clear the high word, set the current event id DAT_000a3d89 from DAT_000a3d79[i] (unless 0xff) and call the callback; loop index in DAT_0012eec0"
```

## ENGINE.md section: region G findings (agent 3, 2026-10-06)

### Function inventory corrections

- `creature_trader_s90_update_1ea50` is truncated to 2 bytes (`push 0x5a`); its body is the listed
  0x1ea52 (`creature_idle_seek_leader_18610(thing, 0x5a)`). Merge 0x1ea52 into it.
- `fe_reset_clip_320x200_573a0` starts mid-instruction (inside the `call 0x60688` at 0x5739f). Its code
  is the 640x480 branch of `fe_draw_centred_icon_57350`; delete it.
- 0x4d0dd (`ui_draw_icon_4d0dd`) contains a second asm routine with its own `push ebp; mov ebp,esp`
  prologue at **0x4d140** (same draw with w,h divided by arg4+1 and a scale argument). It needs a function
  entry; the current 235-byte body spans both.
- 0x284cf starts one byte early; the real entry of the tiny asm "DAC entry 0 = blue" routine is 0x284d0.
- 0x1b233 is a second mis-start of the kraken s38 continuation already recorded as FRAGMENT at 0x1b231.
- 0x6622c is a BIOS keyboard poll routine (int 16h AH=12h/11h/10h) that is not a function in the export;
  it writes the shift state to `DAT_0012eea1` (bit 1 shift, 2 ctrl, 4 alt) and the scancode to
  `DAT_0012eea0`. Called by `input_bios_read_key_ascii_5bff8`.
- Data in the code object: 0x4cdb0 is the `u16[32]` isqrt seed table (`sqrt(2^n)`, indexed by the highest
  set bit of the argument in `math_isqrt_4cd7a`); the front-end string block runs to 0x51ec8 and ends with
  the four UK keyboard-row strings (`!"\$%^&*()_+`, `QWERTYUIOP{}`, `ASDFGHJKL:@~`, `|ZXCVBNM<>?`) used for
  shifted text entry.
- Fall-through handler continuations (no prologue, pop registers pushed by the predecessor):
  0x1ddb2 (builder s74), 0x1e2f1/0x1e41e/0x1e4ad (townie s79), 0x1e75b (trader s85), 0x385e0
  (projectile_create_type17_38590), 0x414a3 (flyer1 s4), 0x425d3 (balloon_update), 0x47586
  (spell_speedup_update), 0x280ad (effect s57), 0x282ca (effect s59), 0x1884f (creature_idle_seek_leader),
  0x10b34 (thing_exists_near_pos). 0x25c2c is the switch-case body of `effect_mana_ball_update_25980`.

### Thing / player fields

- `thing_set_extents_353d0(thing, xy, h)` is the generic bbox setter (ext_x = ext_y = xy, ext_h = h), used
  by 43 constructors; `thing_set_castle_extents_353f0` is the castle variant.
- `thing_signature_14080(thing)` = `(cls << 7) + type + owner_idx` (u16). AI goals store it in the AI player
  thing's `+0x94`; `ai_target_valid_140a0` compares it against the current target to detect recycled slots.
- Player sub-block `P` (thing+0xa0): **P+0x2e** is set to 100 when the player is hit (`player_apply_hits_40b70`)
  or when a projectile picks a human player as target (`projectile_pick_target_45f00`), and zeroed at player
  init - an "under attack" timer (reader not found; speculation). P+0x19f AI modes written by the setters:
  0, 1, 3, 5, 6, 7, 8, 9, 0xb, 0xd (this region) plus 2, 4, 10 already named.
- Mana ball rolling physics (fragment 0x25c2c of `effect_mana_ball_update_25980`, active when +0x3a != 0):
  **+0x96/+0x98 = i16 vx/vy** clamped to +/-0x40 per tick, z_vel (+0x2e) -= 0x10 per tick (min -0x80),
  ground contact bounces with z_vel = -z_vel/4 (killed below 0x11), on the ground the slope vector from
  `terrain_slope_vector_3e4b0` is added to v and v *= 250/256 (friction); balls touching another owner's ball
  merge (`thing_find_collision_other_owner_10980` -> `mana_ball_merge_26120`).
- Effect s59 (`effect_type57_s59_update_28270`, fragment 0x282ca) damages every type-0x27 thing within
  dist^2 0xc40000 through damage slot 4 (`+0x72` amount 100, `+0x76` attacker).

### Level features: walls

`level_build_linked_feature_34c40` dispatches Effect type 0x1c (Wall) to `level_build_wall_342e0`
(0x34570 path / 0x346b0 canyon / 0x34760 ridge are still `LAB_` and need function entries). The wall
builder takes two cell positions, computes the wrapped deltas with `math_wrap_diff_34250(.., 0x100)`,
swaps the endpoints so dx >= 0 and walks `n = max(|dx|,|dy|)/10 + 1` steps. Each step creates two
class 10 **type 0x1b** things at the scratch position from `terrain_set_pos_scratch_34280`: one with
state 0x1d and `+0x1a = dx/n` (run along +x) and one with state 0x1b (dy < 0) or 0x1c (dy >= 0) and
`+0x1a = |dy|/n` (run along y); the division remainder goes on the first step. So the Table A handlers
`effect_steal_mana_s27_update_24fc0` (0x1b), `effect_type26_s28_update_24eb0` (0x1c) and
`effect_type27_s29_update_250b0` (0x1d) are the wall-segment things (-y, +y, +x runs; `+0x1a` = length in
cells) - the "steal mana" name on s27 is inherited from the type-25 Steal mana Table B column and is
probably wrong for this state.

### Sound / music / HMI

- `sound_request_49720` keeps a 10-byte record per sound id at 0xb7ac0 (+0 active, +2 ?, +4 word
  compared by `sound_priority_ok_49c20`: a new request replaces the playing one only if `new - cur >= -8`).
- `sound_sample_done_49d80(owner, sample)` maps the pair through `DAT_0012e070[32]` to a channel and asks
  `hmi_sample_done_6291e(DAT_0012e1c0, channel)`; returns 1 (done) when sound is off or the pair is gone.
- `music_fade_timer_cb_49ca0` is a far routine (ends in `retf`): the 60 Hz HMI timer event used by the
  music fade (decrements `DAT_000943c8`, sets MIDI volume, ends the fade at 0).
- `hmi_timer_dispatch_events_5d5a9` (far) is the PIT-tick service of the HMI timer: 16 slots, callback
  `DAT_000a3c59[i*6]`, increment `DAT_000a3cf9[i]`, 16.16 accumulator `DAT_000a3d39[i]` (fires when bit 16
  sets, high word cleared), per-slot event id `DAT_000a3d79[i]` copied to the current id `DAT_000a3d89`.
  Adds the two id tables to the 5d093 note.

### Front end, FLI, DOS

- `fe_flic_loop_frame_56670`: looping 4-frame flic animation driven from `DAT_0012eccc` (current chunk
  pointer), `DAT_0012ecc8` (frame-1 pointer captured on the first pass), `DAT_0012ecd0` (frame counter
  1..4); 16-tick frame pacing unless input is pending.
- `fe_draw_save_slot_names_54850`: six slot labels "Game One".."Game Six" (pointer table 0x9e4e8) in the
  16-byte rect table at 0x51d3c (code-segment data), font g_font1, colour `DAT_0009e516`.
- `level_save_file_3c150` writes `levels/lev%05d.dat` with exactly **0x979c = 38812 bytes**, confirming the
  level file size (footer at 0x9790 + 12). Dead code (level editor leftover).
- `g_cfg->flags & 8` (skip game) swaps the keyboard ISR install/restore for the empty stubs 0x5bfd0 /
  0x5bfe4.
- `mouse_shutdown_5bdd4` resets the int 33h driver and frees the four cursor buffers
  `DAT_0012ede8/edf8/edc8/edb8`; `dos_get_current_drive_5af2c` / `dos_set_current_drive_5af44` are the
  int 21h AH=19h / 0Eh wrappers (1-based drive numbers); `crt_srand_5b01a` seeds `DAT_0009e5c8`.
- File-stream FLI player: `DAT_0009e700` = file handle, `DAT_0009e6e4` = current chunk size (the three
  read thunks read `size - 6` bytes after the chunk header).

### Open questions

- Who reads P+0x2e (the 100-tick "under attack" value)? No reader found in the export.
- Why do walls use effect *type* 0x1b (not the level-file type 0x1c)? Possibly type 0x1b = "wall segment"
  and 0x1c = "wall descriptor" (the level record that `level_build_linked_feature_34c40` walks).
- `gfx_copy_b400_4ced4` copies 46080 bytes (320x144): the 3D view height with a HUD in 320x200? No callers.
- `flag_set_false/toggle/set_true_4cae0..4cb40` have no direct callers; they are probably menu-option
  callbacks referenced from a pointer table in the data object that the export does not resolve.
