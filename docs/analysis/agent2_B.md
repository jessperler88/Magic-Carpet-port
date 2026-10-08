# Region B (0x1f6b0-0x3c000): effects, HUD helpers, terrain generation, level loading, constructors

Round-2 pass. 105 unnamed functions + 5 UNCERTAIN names read. Method: decompiled body of every
function (mc.py body), disassembly (img.py dis) wherever the C started mid-flow or on padding,
callers/strings where they existed. Addresses are the todo-list addresses even when the real
entry is a few pad bytes later (noted in the comment).

Headline: 42 of the 105 are FRAGMENTS. 29 of those are the bodies of Table-B constructors: every
constructor entry is an 8-byte stub `push ebx; call thing_alloc_35560; mov ebx,eax` and the gap
finder created a second function at entry+8 (`test eax,eax ...`). The rest are mid-instruction or
mid-loop starts inside neighbouring handlers.

## 1. Names CSV

```csv
0x0001f800,music_update_1f800,"Per-tick music driver. If music available (DAT_0009e30c): no track (DAT_0009e312==0) -> remove pending HMI timer event (DAT_000938fa). Music on and song finished (music_song_done_5cf40) -> restart (snd_midi_song_setup_5ec41/start_5eab0; DAT_000938f8=0 f9=1 fb=0xfe; DAT_0009e310=100). BL arg = requested mood 1/2: when it differs from DAT_000938f9 negate the fade direction DAT_000938fb and add an HMI timer event (fade). Callers FUN_3fc00, ui_draw_status_bars_219f0, cue_script_step_17d80."
0x000212f0,ui_fill_bar_212f0,"HUD bar fill: width = min(arg7, arg9); if > 1 fills the rect via gfx_fill_rect_320_60590 or gfx_fill_rect_640_60610 by DAT_0012edae&1. Caller ui_draw_status_bars_219f0."
0x0002275e,ui_draw_sprite_shaded_2275e,"2 pad bytes (mov ecx,ecx); real entry 0x22760. Draws a span-encoded 2D sprite (arg3 ptr, height byte at +5) at (x,y) through the shade table 0xB99B0[(arg4<<8)|c] into DAT_0012ed74; 320x200 path halves coords, 640 path at 0x2280b."
0x00023094,FRAGMENT,"Starts mid-flow (2 garbage bytes, then push eax/movsx bx,si/call ui_draw_text_4a9a0 with an esp-relative sprintf buffer, no prologue, 0 callers): tail of a 3-line text block drawer (sprintf + ui_draw_text x3 with font line height) that begins before 0x23094."
0x00023140,ui_copy_block_23140,"Copies a 160x50 (320 mode) or 320x100 (640 mode) pixel block inside DAT_0012ed74 from (x0,y0) to (x1,y1) with pitch DAT_0012ed70 (rep movsd 0x14 / 0x28 dwords per row)."
0x000231f3,FRAGMENT,"Mid-instruction start (0x231f0 movsx eax,bx spans 0x231f3): the 640-mode branch of ui_copy_block_23140."
0x00023260,ui_fill_rect_tint_23260,"Rect (x,y,w,h,colour; coords halved in 320 mode): dest = 0xBD9B0[colour*0x100 + dest] (colour-major blend; 224e0 uses the dest-major order)."
0x000233c0,ui_draw_rect_outline_blend_233c0,"1-pixel rectangle outline (x,y,w,h,colour; halved in 320 mode) blended through 0xBD9B0[colour*0x100 + dest]."
0x000234f1,FRAGMENT,"Starts at `jl 0x23480` inside a loop (in_OF/in_SF, unaff_SI): tail of the blended-outline drawer that starts at 0x23500 (lea pads at 0x234fb, mov ecx,ecx at 0x234fe)."
0x00023569,FRAGMENT,"Misaligned start inside a function body: 0x23571 jmp 0x235d7 then rect fills via 60688/606c0 with colour [0xadf94]+0x1e; belongs to the function beginning at 0x23500."
0x00023600,movie_subtitle_init_23600,"Once (DAT_0009390a=1, DAT_000938fd=1): mem_alloc_named; ui_set_clip_rect_588b0; ui_font_init_589d0; DAT_000adfd6 = palette_find_nearest colour; subtitle strip DAT_000b2e04 = DAT_0012ed74 + 0xe100 (= 320*180, bottom 20 rows), DAT_000b2e0c = 0x4b00; clears it (movie_buffer_clear_236d0). Caller cue_script_step_17d80 (FLI event script) - this is the ftext.dat subtitle system."
0x000236a0,movie_subtitle_show_236a0,"If initialised: clear the subtitle strip then ui_draw_text_58ab0 (font DAT_000adfd0, text = arg5) and remember the string in DAT_0009390c."
0x00023740,movie_subtitle_set_colour_23740,"Sets subtitle colour DAT_000adfd6 = arg and redraws the current subtitle DAT_0009390c via movie_subtitle_show_236a0."
0x00023767,terrain_max_drop_around_23767,"10 zero bytes + lea pads; real entry 0x23780. arg = ptr to pos {x,y}; cell = (coord-0x80)>>8; returns the most negative (neighbour - centre) heightmap (0xDDFB0) difference over the 8 neighbours, 0 when nothing is lower (steepest downhill drop)."
0x00023f20,terrain_modify_cells_spiral_23f20,"spiral_search_begin_10080; while spiral_search_next_10120 == 1 call terrain_modify_cell_3d620; spiral_search_end. Applies a height change to every cell of a spiral area. Caller effect_crater_s11_update_23fc0."
0x000245d3,FRAGMENT,"Tail of effect_type16_s16_update_243b0 (ends add esp,8 / pop edi,esi,ebx / ret, ebx = thing): writes thing+0x96 = x/256 and +0x98 = (+0x98*250)/256."
0x00024d70,terrain_ring_find_height_ne8_24d70?,"Real function (jump table 0x24d54 sits in front of it). Ring scan around packed cell (esp+0x14): for radius r..0x1d walks the 4 sides of each square ring and returns the first packed cell whose heightmap byte (0xDDFB0, cmp [ebx+0xddfb0],8) != 8, else the start cell. The start radius local is apparently uninitialised. 0 callers in export."
0x00024e20,terrain_cell_raise_needed_24e20,"Used by the terrain-raising effects (states 27/28/29: 24fc0/24eb0/250b0 raise cells by 0x30): returns 0 (skip) when the x-1 neighbour is texture 8 (0xCDFB0) and the 4 neighbours (x-1, x+1, y+1, y-1) are no higher than h+0x1e; otherwise 1."
0x000255fe,effect_age_tick_255fe,"2 pad bytes; entry 0x25600. thing (stack arg): +0x1a timer++; +0xc life--; thing_mark_delete_3e3e0 when life was already < 0. Generic effect ageing helper (0 callers in export)."
0x00025a9c,FRAGMENT,"Mid-instruction start (0x25a9a mov dl,[eax+0x74a3]) inside effect_mana_ball_s41_update_25980: the 'being collected' branch (flag 0x40) that homes on target +0x92 at speed +0x2e = 0x80 and drops the flag when > 0x400 away."
0x00025e20,mana_ball_update_sprite_25e20,"Mana ball sprite: size level 0..7 = first threshold in DAT_00093910[7] that is >= +0x8c mana; sprite base from the owner (thing +0x90, must be class 3): playerrec+0x30 player 0..7 -> 0x69,0x71,0x79,...,0xa1 (8 sprites per player colour), unowned -> 0x34; if +0x56 differs: thing_set_sprite then thing_set_sprite_halved_35380. Caller effect_create_mana_ball_39840."
0x00025fc3,FRAGMENT,"Starts mid-loop (in_CF): tail of effect_type40_s42_update_25f10 - walks the thing pool reassigning +0x90 owner indices, clears +0x64 and marks the thing deleted."
0x00025fe0,thing_drop_mana_ball_25fe0,"If +0x8c > 0: thing_create_35690 (mana ball) with +0x8c mana and +0x90 owner copied, yaw = own yaw + rnd%0x71 - 0x38, speed +0x7e = 0x10 + rnd%0x30, +0x2e z-vel = (0x400 - height above ground)/8, pushed out by math_rotate_offset_3e420; then own +0x90 = 0 (RNG +4 LCG 0x24a1/0x24df). Callers FUN_19310 (creature death) and FUN_419a0 (player)."
0x00026320,castle_stamp_footprint_26320,"Stamps the castle footprint map (6-byte size table[+0x47]: +0 ptr to span-encoded map, +4 w, +5 h; w,h halved when DAT_0012edae==1) centred on the thing's cell, base height = z>>5. Pass 1 per map byte: 7..0xe -> height = base; high nibble 3 -> base+0xc / base+0x10 terraces; other high nibble -> base + (nib-1)*4; flags low bits -> 1 and terrain_retexture_rect_324e0. Pass 2: paint kind per byte (nibble 0: class (b%7)-1; 1-2: kind 8/9; 3: 10 + (b%16)/3 with orientation b%3; >=4: nib+0xb) via terrain_paint_cell_32150. Caller FUN_3f360."
0x00027660,creature_spawn_random_villager_27660,"rnd%12 (thing +4 LCG) picks a creature via thing_create_35690 and forces its state: 0-1 -> 0x19 Archer, 2-3 -> 0x55 Trader, 4-8 -> 0x4f Townie, 9-11 -> 0x49 Builder; returns the thing or 0. Callers effect_ridge_node_s52_update_27710 and effect_type51_s53_update_27930 (so types 50/51 are village/town spawners, not ridge nodes)."
0x00027f90,thing_apply_pending_damage_27f90,"+0x28 = 0; health (+0xc) < 0 -> return 2; if +0x5e attacker set: health -= +0x5a; dead -> +0x26 = attacker and return 2; else +0x28 = attacker, clear +0x5a/+0x5e, return 1; no pending damage -> 0. Caller effect_ridge_node_s52_update_27710."
0x000282c8,FRAGMENT,"Mid-flow tail (unaff_ESI, garbage first bytes) of effect_type57_s59_update_28270: walks the mana-ball list (type 0x27) within dist^2 < 0xc40000 setting +0x72 = 100 and +0x76 = own index (pusher) - a mana-magnet push."
0x000284c1,FRAGMENT,"One byte into effect_type59_s61_update_284c0 (the 0x30-byte table handler): that handler writes DAC entry 0 = (0, x, 0x3f) through ports 3C8/3C9 - a palette flash."
0x00030500,terrain_smooth_spikes_30500,"Terrain pass: for land cells (flags&7 != 0) take min/max of the 8 neighbours; a cell > 4 above the min (spike) or > 4 below the max (pit) becomes the neighbour average, or (h+avg)/2 when the excess is < 11."
0x00030690,terrain_mark_water_anim_30690,"Clears flag bit 8 everywhere, then sets bit 8 (animated water, see renderer) on cells of height 0 whose 8 neighbours are height 0 and whose 4 texture cells (x,y),(x-1,y),(x-1,y-1),(x,y-1) are texture 0."
0x00030810,terrain_fix_shore_quads_30810,"For each 2x2 quad: if it has both class-0 (water) and class-4 (transition) cells and the lowest water cell is height 0, zero all four heights (flat shoreline)."
0x000308f0,terrain_flags_fill_holes_308f0,"If all 8 neighbours share the same non-zero flag class, set the centre to it (removes 1-cell holes in the class map)."
0x000309f0,terrain_classify_flat_309f0,"arg = threshold. For class-5 cells: range = max-min of the 5-cell cross; range < thr -> class 3, range == thr -> class 4. Then quads containing 3 and 5 but no 2 turn their 3s into 4."
0x00030c50,terrain_insert_transitions_30c50,"For each 2x2 quad counts classes 0/5/3: 3+5 -> the 5s become 4; 3+0 -> the 3s become 4; 0+5 -> the non-zero cells become 4. Class 4 = transition between terrain classes."
0x00030e58,terrain_roughen_class1_30e58,"lea pads; entry 0x30e60. Cells with (flags&7)==1 get height += rnd%9 (16-bit LCG DAT_0012dfb0 * 0x24a1 + 0x24df). 0 callers in export."
0x00030eb0,terrain_assign_textures_30eb0,"Texturing pass. Builds a 7x7x7x7 lookup of corner-class tuples (c0,c1,c2,c3) -> up to 12 textures (0x19-byte buckets, frame buffer DAT_0012ed74 used as scratch) from a 0x94-entry table of 4-byte class tuples, each in 8 rotations/flips (rotation codes 0x10..0x70); copies the first match per tuple to DAT_000b58b0 (2 bytes: texture, rotation). Then for every cell with texture 0 picks a random matching texture into 0xCDFB0 and ORs the rotation into the 0xFDFB0 high bits (texture 1 when no match)."
0x00031310,terrain_build_lightmap_31310,"Light map 0xEDFB0 = 0x20 - (h[x+1,y+1] - h[x-1,y-1]) (NW->SE slope shading); flat (== 0x20) gets rnd%9 + 0x1c; < 0x1c -> (v&3)+0x1c; > 0x28 -> (v&7)+0x28. Resets DAT_0012dfb0 = 0."
0x00031430,terrain_carve_rivers_31430,"args (river count, min source height). flags = 5 (land) / 0 (sea) from height != 0; for each river picks a random land cell above the min height (1000 tries) and runs terrain_trace_river_314e0; finally the texture map 0xCDFB0 = 0xff."
0x000314e0,terrain_trace_river_314e0,"Uses 0xCDFB0 as visited map (3 = unvisited, 0 = visited): from the start cell repeatedly moves to the lowest unvisited of the 8 neighbours, lowering the heightmap to the running minimum, until a sea cell (flag 0) or a dead end (0xff); all visited cells get class 0 (water)."
0x00031650,terrain_mark_lowland_31650,"args (max_height, max_range): copies flags into 0xCDFB0 as scratch; cells whose 9-cell max < max_height, (max-min) <= max_range and class != 0 become class 5."
0x00031800,terrain_mark_interior_31800,"args (max_height, max_range): a class-5 cell whose 9-cell max < max_height, (max-min) <= max_range and whose 8 neighbours are all class 5 or 2 becomes class 2 (interior of smooth lowland)."
0x00031ad0,terrain_mark_steep_31ad0,"arg min_range: non-water cells whose 5-cell cross height range >= arg become class 6; then a class-6 cell whose 8 neighbours mix classes (3/2/5, or any 4 present) becomes class 1."
0x00031e50,terrain_flatten_water_quads_31e50,"Repeat until stable: every 2x2 quad whose 4 cells are class 0 (water) with unequal heights is set to its minimum height."
0x00031f90,terrain_slope_orientation_31f90,"For the quad (cell, x+1, x+1/y+1, y+1): highest corner index (0..3) and second highest; DAT_00093fc4 = (max-min < 9) nearly-flat flag; if the second is within 8 of the highest returns edge code 4..7 (pairs 0-1,1-2,2-3,3-0) else the corner index. Selects rotated slope textures in terrain_paint_cell_32150."
0x00032150,terrain_paint_cell_32150,"Paints cell (arg5) by kind (arg6): kind < 8 -> flag class = kind and terrain_retexture_rect_324e0; 8/9 -> texture 8/9; 0xf -> texture 0xb; 0xa..0xe -> slope texture 0x94298[(orient + 0/0x10/0x20/0x30/0x40 (+8 if nearly flat))*2]; 0x10..0x16 -> tables 0x94218/58/68/78/28/38/48 by orientation (0x12/0x13 add the (x+y)&1 checker bit; 0x10 skips textures 10-12). Table entries {texture, rotation}; writes 0xCDFB0 and 0xFDFB0 = rot | (flags & 0x8f); then sets flag 0x80 (built) on the cell and clears water bit 8 on the quad. Callers explosion scorch 238b0, castle stamp 26320, castle build 26680/26f10."
0x00032430,terrain_set_quad_texture_32430,"Sets texture arg2 on the 4 cells (x,y),(x-1,y),(x-1,y-1),(x,y-1), then recomputes the light map (0x20 - slope, clamped) and clears the water bit for the 3x3 around. Callers raise effects 24eb0/24fc0/250b0."
0x000324b4,FRAGMENT,"Loop tail of terrain_set_quad_texture_32430 (do-while with register-carried counters, 0 callers)."
0x000324e0,terrain_retexture_rect_324e0,"args ((x0,y0) packed, (x1,y1) packed): marks cells of the rect (+1 ring) lacking flag 0x80 with texture 1, gives each marked cell the texture from DAT_000b58b0[corner-class tuple] (random rotation rnd%7<<4 when texture < 8, else the table rotation), recomputes the light map and clears water bits over rect+1. Callers 251e0, 26320, 26680, 32150, terrain_modify_cell_3d620, 3d7ce."
0x00032760,terrain_retexture_rect_force_32760,"Same as terrain_retexture_rect_324e0 but marks every cell of the rect (ignores the 0x80 built flag). Callers 26f10 castle raise, 27930, terrain_modify_cell_3d620."
0x000329c0,level_skip_number_329c0?,"cfg+0x11 (level number): if it is 8, 0x11, 0x1c, 0x21 or 0x27 increment it (those level numbers are skipped). Caller game_main_32a00."
0x00033480,video_alloc_buffers_33480,"mem_alloc_named_5ae80 '*SearchD' (string 0x96df0) and '*WScreen' (size by DAT_0012edae), then FUN_49de0 and FUN_101b0 (spiral-search ring tables). Callers game_main_32a00, data_load_all_334c0."
0x000335e0,game_shutdown_all_335e0,"FUN_4bc80; game_shutdown_3ee70; file_free_resource_list_610c0; FUN_4b4d0. Called once at the end of game_main_32a00."
0x00034220,sound_timer_shutdown_34220,"If DAT_00094341: remove the HMI timer event, sound_shutdown_617e0, music_shutdown_61870; else if DAT_000987dc == 1 restore the PIT (3 x dos_outb) and the int 8 vector DAT_000b7ca0/a4 (dos_setvect); DAT_000987dc = 0. Caller game_main_32a00."
0x00034250,math_wrap_diff_34250,"(a, b, modulus): d = b - a wrapped into [-m/2, m/2] (angle/wrap difference). 0 callers in export."
0x00034280,terrain_set_pos_scratch_34280,"(x0,y0,x1,y1 cells): DAT_000adfc4 = (x0<<8, y0<<8); DAT_000adfc8 z = max(h(x0,y0), h(x1,y1)) << 5. 0 callers in export."
0x00034820,terrain_rect_height_range_34820,"(x, y, dy, dx): max - min of the heightmap at the 4 corners (x,y),(x+dx,y),(x+dx,y+dy),(x,y+dy). Caller FUN_1d420 (creature AI)."
0x000348b0,terrain_smooth_castle_border_348b0,"(.., w, h, size): terrain_smooth_cell_34a40 twice per cell over a w*2 x (size+1) strip and a (h*2+size*2) x (size+1) strip - smooths the terrain ring around a castle footprint. Callers effect_castle_build_update_26680, 26b50."
0x00034a00,terrain_smooth_rect_34a00,"(.., w, h): terrain_smooth_cell_34a40 over a w x h block. Caller effect_type51_s53_update_27930."
0x00034a40,terrain_smooth_cell_34a40,"cell arg: if land (flags&7, h != 0) and neither it nor its 3 quad neighbours (x-1,y-1),(x,y-1),(x-1,y) carry a castle texture (6..0x22), height = average of the 3x3 neighbours that are not castle-textured."
0x00034ba5,terrain_rect_min_height_34ba5,"lea pads; entry 0x34bb0. (x, y, w, h): minimum heightmap value along the rectangle perimeter (two horizontal edges over w, two vertical edges over h), starting from 0xfa. 0 callers in export."
0x00034ed7,level_spawn_effect_direct_34ed7,"Helper of level_spawn_effect_record_34e00 (directly after it): DAT_000adfc4 = (Xpos<<8, Ypos<<8), DAT_000adfc8 = terrain_height_at_10bc0, then calls the class-10 Table-B handler for Model directly (0x957ea + model*0xe, checked non-null) - no enabled check."
0x000352d0,thing_set_sprite_small_352d0,"thing_set_sprite_35240 then +0x50/+0x52 = sprite-table width/2 (0x9767e) and +0x54 = height/2 (0x97680): half-size collision box. Used by scenery constructors and smoke effects (11 callers)."
0x00035300,FRAGMENT,"Mid-flow (unaff_ESI): tail that sets +0x50 = arg and +0x52/+0x54 from the sprite table; shares code with thing_set_sprite_small_352d0."
0x00035340,thing_set_sprite_double_35340,"thing_set_sprite_35240 then doubles +0x50/+0x52/+0x54. Callers FUN_1949e, FUN_19550, projectile type-13 constructors 38360/383cc."
0x00035380,thing_set_sprite_halved_35380,"thing_set_sprite_35240 then halves +0x50/+0x52/+0x54. Caller mana_ball_update_sprite_25e20."
0x00035800,level_spawn_thing_record_35800,"THING_INIT record (u16 Class, Model, Xpos, Ypos, .., f6, f7, f8) -> if tableB[Class][Model].enabled (0x943de + class*0x12, +10): pos = cell centre (x*0x100+0x80) + terrain height, thing_create_35690. Class 10: Model 0x22 (Teleport) gets destination +0x96/+0x98 from f8/f7 cells, 0x2d -> effect_wizard_init_35090, 4 -> owner +0x18 = f6 + extents + restore health; class 11 (Switch): +0x18 = f6 (switch id), extents FUN_353d0, flag 1; class 12 (Spell): state += f6, if f6 > 2 state -= 3 with sprite 0x118 and flag 0x40000 (dropped spell pickup). Caller switch_activate_356e0."
0x00035ca8,FRAGMENT,"Continuation (+8, after push ebx/call thing_alloc) of player_create_type3_35ca0: class 3 type 3 state 7, max health 10000, speed +0x7e = 0x30, +0x88 = 10000, +0x8c = 0, +0x1c = 1, descriptor DAT_00096b30."
0x00036018,FRAGMENT,"Continuation of scenery_create_dolmen_36010: class 2 type 2 state 6, flag 8 cleared (not collidable), +0x1a = thing index % 11 (anim phase), thing_set_sprite_small_352d0 + FUN_353d0 extents."
0x000360a8,FRAGMENT,"Continuation of scenery_create_bad_stone_360a0: class 2 type 3 state 9, flag 8 cleared, +0x1a = index % 11, thing_set_sprite_small_352d0."
0x00036128,FRAGMENT,"Continuation of scenery_create_2d_dome_36120: class 2 type 4 state 0xc, +0x1a = index % 11, thing_set_sprite_small_352d0."
0x00036ac0,crab_update_mana_sprite_36ac0,"Crab (caller creature_crab_s33_1ac80): level = clamp(+0x8c / (+0x88/8), 0, 7); if level > current sprite - 0xb9 add 5000 to max health; thing_set_sprite (0xb9 + level) - the crab grows as it eats mana."
0x00036ea0,creature_troll_init_variant_36ea0,"Troll constructor helper: even tick counter (+0x3f) -> sprite A, +0x47 = 2, max health 2000; odd -> sprite B, +0x47 = 1, max health 4000; thing_set_mana_from_health; health = max. Caller creature_create_troll_36f00."
0x00037cb8,FRAGMENT,"Continuation (+8) of projectile_create_type0_37cb0: class 9 type 0 state 0, speed +0x7e/+0x80 = 0x180, +0x8c = 0x32, lifetime +8 = 0x2000/speed, descriptor DAT_00096ab0, flag 8 cleared."
0x00037d38,FRAGMENT,"Continuation of projectile_create_type1_37d30 (homing): state 1, speed 0x180, life 0x1000/speed, descriptor DAT_00096a50, collision filter class +0x42 = 10 (effects), extents FUN_353d0."
0x00037de8,FRAGMENT,"Continuation of projectile_create_type2_37de0: state 2, speed 0x180, life 0x2000/speed, descriptor DAT_00096a30."
0x00037e68,FRAGMENT,"Continuation of projectile_create_type3_37e60: state 3, speed 0x180, life 0x2000/speed, descriptor DAT_00096a30."
0x00037ee8,FRAGMENT,"Continuation of projectile_create_type4_37ee0: state 4, life 0x2000/speed, descriptor DAT_00096a30."
0x00037f68,FRAGMENT,"Continuation of projectile_create_type5_37f60: state 5, life 0x2000/speed, descriptor DAT_00096a30."
0x00037fe8,FRAGMENT,"Continuation of projectile_create_type6_37fe0: state 6, life 0x2000/speed, descriptor DAT_00096a30."
0x00038068,FRAGMENT,"Continuation of projectile_create_type7_38060: state 7, life 0x2000/speed, descriptor DAT_00096a30."
0x000380e8,FRAGMENT,"Continuation of projectile_create_type8_380e0: state 8, life 0x2000/speed, descriptor DAT_00096a90."
0x00038168,FRAGMENT,"Continuation of projectile_create_type9_38160 (lightning): state 9, life 0xe00/speed, descriptor DAT_00096a90."
0x000381e8,FRAGMENT,"Continuation of projectile_create_type10_381e0: state 10, life 0x2000/speed, descriptor DAT_00096a30."
0x00038268,FRAGMENT,"Continuation of projectile_create_type11_38260: state 0xb, life 0x2000/speed, descriptor DAT_00096a30."
0x000382e8,FRAGMENT,"Continuation of projectile_create_type12_382e0: state 0xc, life 0x800/speed (short range), descriptor DAT_00096a30."
0x00038368,FRAGMENT,"Continuation of projectile_create_type13_38360: state 0xd, life 0x1400/speed, thing_set_sprite_double_35340 (big bbox)."
0x000383cc,projectile_create_type13_long_383cc,"Standalone constructor with its own thing_alloc (0 callers in export): class 9 type 0xd state 0xd, speed 0x180, life 0x1e00/speed (longer than the table version at 38360), flag 8 cleared, thing_set_sprite_double_35340."
0x00038518,FRAGMENT,"Continuation of projectile_create_type16_38510: type 0x10 -> state 0x11, life 0x2000/speed, descriptor DAT_00096ab0."
0x00038598,FRAGMENT,"Continuation of projectile_create_type17_38590: type 0x11 -> state 0x12, life 0x1000/speed, descriptor DAT_00096a50, extents FUN_353d0."
0x000385ff,FRAGMENT,"Mid-flow inside projectile_create_type17_38590 (add esp,4; push 0xd1; call thing_set_sprite_35240): sprite 0xd1 + FUN_353d0 extents."
0x00038638,FRAGMENT,"Continuation of projectile_create_type18_38630: type 0x12 -> state 0x13, life 0x2000/speed, descriptor DAT_00096ab0."
0x000386b8,FRAGMENT,"Continuation of projectile_create_type19_386b0: type 0x13 -> state 0x14, life 0x2000/speed, descriptor DAT_00096ab0."
0x00038784,FRAGMENT,"Mid-flow inside effect_create_explosion_38730 (garbage bytes then thing_set_sprite_35240 + FUN_353d0 extents)."
0x000387b8,FRAGMENT,"Continuation of effect_create_big_explosion_387b0: class 10 type 1 state 1, life 1, damage +0x2c = 400, flags &= ~0x20008, flag 0x200 set."
0x00038878,FRAGMENT,"Continuation of effect_create_type3_38870: class 10 type 3 state 3, life 7, damage 0, +0x1a = 0, flags &= ~0x20008, flag 0x200."
0x000390a8,FRAGMENT,"Continuation of effect_create_type19_390a0: class 10 type 0x13 state 0x13, damage 200, life 0xf0, flags &= ~0x20008 | 0x200 | 1, extents FUN_353d0."
0x00039128,FRAGMENT,"Continuation of effect_create_lightning_39120: class 10 type 0x17 state 0x17, life 8, damage 0x19, flags 0x200 and 1, extents FUN_353d0."
0x000397c0,effect_create_type37_397c0,"Standalone constructor with its own thing_alloc (0 callers in export, absent from the generated table names): class 10 type 0x25 -> state 0x27, life 8, pos from the arg pointer, damage +0x2c = 64000, flag 8 cleared, thing_set_sprite + FUN_353d0 extents."
0x00039aa8,FRAGMENT,"Continuation of effect_create_type44_39aa0: class 10 type 0x2c -> state 0x2e, max health 500, damage 500, flag 8 cleared."
0x00039b08,FRAGMENT,"Continuation of effect_create_crab_egg_39b00: class 10 type 0x34 -> state 0x38, +8 = 100000 (0x186a0), damage 500, +0x1a = 600, mana +0x8c = 500, +0x88 = 2000."
0x00039dc0,switch_create_common_39dc0,"(pos ptr, type, state): thing_alloc_35560; class 0xb Switch, +0x41 = type, +0x46 = state, flags = (flags & ~9) | 1, +0x1a = 0, position (x,y,z) copied from pos; thing_restore_health. Called by all 32 switch constructors 0x39e10..0x3a1f0 (which are 18-byte stubs)."
0x0003a758,FRAGMENT,"Continuation (+8) of class13_create_type0_3a750: class 13 type 0 state 0, flags &= 1, restore health."
0x0003bbd0,player_cheat_check_3bbd0?,"Every 8th frame (cfg+4 & 7 == 0) when (cfg+0x18 & 3) == 1: FUN_10010(); if it returns 0 set the local player's record byte +0x340f (state+0x340f + p*0x801) = 2. Callers player_commands_process_3a8b0 and chat_message_show_3bb50 (after the RATTY/chronicle command)."
0x0003bf60,players_clear_records_3bf60,"Zeroes the 8 x 10-byte command packets at state+0x7413 and the 8 x 0x801 player records at state+0x340b, preserving the dword at record+0x18 (= +0x3423, the config/cheat value). 0 callers in export."
0x0003bfc0,level_load_levels_dat_3bfc0,"(level < 1000, dest): sprintf two names, open the index file (2 path attempts), read 4000 bytes of u32 offsets into DAT_000adf68; entry[level]..entry[level+1] -> lseek, read, rnc_unpack_61ab0 into DAT_000adf68, copy 0x979c bytes to dest, zero the buffer; prints 'ERROR decompressing levels.dat'. Returns 1 / 0. Callers level_load_and_init_3d3b0, level_finish_3d4e0."
0x00022d80,ui_draw_spell_panel_icon_22d80,"CORRECTED (was target panel). (x, y, spell thing, mode): spell-selection icon for a spell thing whose caster (+0x2a) is live: mode 0 blends the background (224e0), else solid 0x12 (idle) / 0x18 (casting, +0x30 != 0); icon index (type+6)*6; when the caster's view mode (+0x3855) != 0 or playerrec+0x34c[type] > 0 decrements that flash counter and highlights (22610) if the thing is in a selected slot (rec+0x304[i] -> rec+0x214[slot]); dims with ui_shade_rect_23310 when cost +0x84 exceeds the castle's mana (+0x8c of thing rec+0x32)."
0x00023c20,effect_fire_update_23c20,"CONFIRMED Fire (class 10 state 6): life-- then FUN_3f240; life < 12: shrink (sprite--, +0x1a--) and a 1/7 chance to spawn a smoke thing (+0x1a 100, life 15, owner copied, sprite+2) unless flag 0x80; else grow until +0x1a == 7 (sprite++); z = terrain + +0x2e; on water (terrain_type_mask_at == 1) delete; thing_area_damage_11160 unless flag 0x100."
0x000241f0,effect_black_smoke_update_241f0,"CONFIRMED (class 10 state 14): life-- (delete when < 0); rise speed +0x7e -= 4 clamped 0x40..0x80; z += speed clamped to the terrain; drifts via math_rotate_offset_3e420 for the first 16 ticks (+0x1a) advancing the sprite every other tick; sprite-- when life < 6; thing_move_to_3e1d0."
0x00026f0e,effect_castle_raise_terrain_s44_26f0e,"CONFIRMED and renamed; 2 pad bytes before the table handler 0x26f10 (castle build state 44). First tick: +0x1a = 0x13, flag 2. While +0x1a > 0 and the castle (+0x2a) +0x32 hit timer == 0: builds int16 height deltas for the footprints of every size <= +0x47 in DAT_000adf68 (cell_kill_things_3da30 when flag 0x100), adds delta/remaining ticks to the heightmap each tick (new land -> class 1 + terrain_retexture_rect_force_32760), paints textures via terrain_paint_cell_32150 every 7th tick, water-bit fixes at ticks 1-2. At 0: +0x1a = -1 (or -0x19 when +0x3c set); counting back up to 0 marks footprint cells 0x80 (clearing bit 8), sets castle +0x30 = 2 and deletes itself."
0x0002e5a0,render_cell_things_mirrored_2e5a0,"CONFIRMED: same loop as render_cell_things_2c600 (cull z < 0x41 / dist^2 >= DAT_000b584c, fog DAT_000b5818, sprite choice by draw type 0/1/0x11/0x12/0x13-0x14/0x15/0x16+, LRU stamp DAT_000b7cb0) but projects with height -cam_z - thing_z (reflection below the surface) and never draws a shadow; used by the second-surface pass (state+0x2195)."
```

## 2. ENGINE.md section: terrain generation, terrain painting, constructors (region B, round 2)

### 2.1 Terrain generation pipeline (terrain_build_303f0)

Order of the passes called by `terrain_build_303f0` after the heightmap exists (loaded 64 KB file or
`terrain_fractal_fill_71f08` + `terrain_generate_313a0`), with the flag-byte (0xFDFB0) low nibble used
as a **terrain class** during generation:

| step | function | effect |
|---|---|---|
| 0 | mem_set(0x10DFB0, 0, 0x20000) | clear the u16 scratch / cell->thing map |
| 1 | terrain_carve_rivers_31430(n, min_h) | class = 5 (land) or 0 (sea) by height != 0; n rivers traced downhill by terrain_trace_river_314e0 (visited map in 0xCDFB0, river cells -> class 0, heights lowered monotonically); texture map := 0xff |
| 2 | terrain_flatten_water_quads_31e50 | class-0 quads levelled to their min height until stable |
| 3 | terrain_classify_flat_309f0(thr) | class 5 with cross range < thr -> 3, == thr -> 4 |
| 4 | terrain_mark_lowland_31650(max_h, max_range) | low smooth non-water -> 5 |
| 5 | terrain_insert_transitions_30c50 | class 4 inserted between 0/3/5 in every 2x2 quad |
| 6 | terrain_mark_interior_31800(max_h, max_range) | class 5 surrounded by 5/2 -> 2 |
| 7 | terrain_mark_steep_31ad0(min_range) | steep -> 6; steep at mixed class boundaries -> 1 |
| 8 | terrain_flags_fill_holes_308f0 | majority fill of single-cell holes |
| 9 | mem_set(0xCDFB0, 0, 0x10000) | clear texture map |
| 10 | terrain_smooth_spikes_30500 | remove 1-cell spikes/pits on land |
| 11 | terrain_fix_shore_quads_30810 | water/transition quads touching height 0 flattened |
| 12 | terrain_assign_textures_30eb0 | textures from the 7^4 corner-class table (below) |
| 13 | terrain_mark_water_anim_30690 | flag bit 8 (animated water) on all-zero water areas with texture 0 |
| 14 | terrain_build_lightmap_31310 | 0xEDFB0 = 0x20 - NW/SE slope, with noise for flat cells |

Resulting class meanings (flag & 7): 0 water, 1 cliff edge at mixed boundaries, 2 interior lowland,
3 flat (higher) land, 4 transition, 5 default land, 6 steep. The generator's RNG is the 16-bit
`DAT_0012dfb0` (LCG *0x24a1 + 0x24df), seeded from the level header (+4) in terrain_build_303f0.

The numeric arguments of steps 1,3,4,6,7 are pushed by terrain_build_303f0 (not visible in the C);
they are the level's terrain parameters - worth reading from the disassembly of 303f0 when porting.

### 2.2 Texture assignment and the corner-class table

`terrain_assign_textures_30eb0` reads a 0x94-entry table of 4-byte class tuples (one tuple of corner
classes per texture, index = texture number; the table is the `""`-addressed data right after the
code, exact address in the disassembly at 0x30eb0) and expands each tuple into the 8 symmetries
(rotation codes 0x10,0x20,...,0x70 stored in the flag high nibble). The lookup is keyed by
`c0*0x157 + c1*0x31 + c2*7 + c3` (7^4 = 2401 buckets of 0x19 bytes: count, 12 texture ids, 12
rotation codes) and built in the frame buffer as scratch. A compact copy (first match: texture,
rotation; `{1,0}` when none) is kept at **DAT_000b58b0** (2401 x 2 bytes) and is what the run-time
retexture functions use:

* `terrain_retexture_rect_324e0(xy0, xy1)` / `terrain_retexture_rect_force_32760`: re-derive the
  texture of every cell in a rectangle from the 4 corner classes (random rotation for textures < 8),
  then rebuild the light map and clear water bits. 324e0 skips cells flagged 0x80 (built-on).
* `terrain_paint_cell_32150(cell, kind)`: the "paint kinds" used by explosions and castle building:
  0..7 class paint (then retexture), 8/9 textures 8/9, 0xf texture 0xb, 0xa..0xe slope textures from
  the 2-byte tables at 0x94298 (+0x10 per kind, +8 when nearly flat), 0x10..0x16 from the tables at
  0x94218/0x94258/0x94268/0x94278/0x94228/0x94238/0x94248 indexed by `terrain_slope_orientation_31f90`
  (highest corner 0..3 or ridge edge 4..7; `DAT_00093fc4` = nearly flat). Sets flag 0x80 on the cell.
* Flag byte 0xFDFB0 layout (consolidated): bits 0-2 class (generation) / low nibble set by paint
  kinds; bit 3 animated water; bits 4-6 texture rotation/flip (renderer: `>>2 & 0x1c` selector);
  bit 7 built-on / castle footprint (also set by the raise effects on their perimeter).
* Texture ids 6..0x22 are castle textures (`terrain_smooth_cell_34a40` refuses to smooth them);
  texture 8 is tested by the terrain-raise effects (`terrain_cell_raise_needed_24e20`).

### 2.3 Castle footprint maps

The 6-byte castle-size table indexed by thing +0x47 is `{u32 map_ptr, u8 w, u8 h}`. The map is
span-encoded exactly like the 2D sprites (0 = end of row, n<0 skip, n>0 copy n bytes). Each byte is
a terrain instruction: 7..0xe flat at the castle base height; high nibble 1/2 -> paint kind 8/9
(castle textures); high nibble 3 -> terrace (+0xc / +0x10 height) with slope kind 10 + (low%16)/3 and
orientation low%3; high nibble >= 4 -> height base + (nib-1)*4 and paint kind nib+0xb.
`castle_stamp_footprint_26320` applies it instantly (level start, caller FUN_3f360),
`effect_castle_raise_terrain_s44_26f0e` animates it over 19 ticks while the player builds, and
`terrain_smooth_castle_border_348b0` smooths the surrounding ring. Oddity: both halve w/h when
`DAT_0012edae == 1` (320x200) - the footprint table is probably shared with the HUD castle icon.

### 2.4 Constructors (Table B) - entry layout and common fields

Every Table-B constructor is `push ebx; call thing_alloc_35560; mov ebx,eax` (8 bytes) followed by
the body (`test eax,eax; je fail; ...`). The gap finder produced a second function at entry+8 for 29
of them; they are listed as FRAGMENT above with the constants they set. Patterns recovered:

* Projectiles (class 9, 0x37cb0..0x386b0): speed +0x7e = +0x80 = 0x180, +0x8c = 0x32, lifetime
  +8 = range/speed with range 0x2000 (most), 0x1000 (type 1 homing, 0x11), 0xe00 (type 9 lightning),
  0x800 (type 0xc), 0x1400/0x1e00 (type 0xd, table vs standalone 383cc); descriptor (+0x9c)
  DAT_00096a30 (types 2-7, 0xa-0xc), DAT_00096a50 (1, 0x11), DAT_00096a90 (8, 9), DAT_00096ab0
  (0, 0x10, 0x12, 0x13); flag 8 (collidable) cleared; type 1 sets collision filter class +0x42 = 10.
  Types 0x10..0x13 map to states 0x11..0x14 (state = type + 1 above 0xf).
* Switches: all 32 table stubs call `switch_create_common_39dc0(pos, type, state)`.
* Scenery (dolmen, bad stone, domes): +0x1a = thing index % 11 as animation phase, half-size bbox
  via `thing_set_sprite_small_352d0`.
* Effects set +0x2c (damage) and +8 (life) and typically `flags &= ~0x20008; flags |= 0x200`.
* Bbox helpers: `FUN_000353d0(thing, xy, z)` sets +0x50/+0x52 = xy and +0x54 = z;
  `thing_set_sprite_double_35340` / `thing_set_sprite_halved_35380` scale the sprite-table extents.
* `effect_create_type37_397c0` (class 10 type 0x25 -> state 0x27, damage 64000) and
  `projectile_create_type13_long_383cc` are standalone constructors with their own thing_alloc
  and no callers in the export - probably reached through pointers or dead.

### 2.5 Level data

* `level_spawn_thing_record_35800` (THING_INIT -> thing): field 6 (SwiId) is reused per class:
  switch id (class 11, -> +0x18), spell phase offset (class 12; > 2 means a dropped spell: state-3,
  sprite 0x118, flag 0x40000), owner for effect type 4; fields 7/8 (Parent/Child) are the
  destination cell of a Teleport (effect 0x22 -> +0x98/+0x96).
* `level_load_levels_dat_3bfc0`: levels are stored in one RNC container: a 4000-byte index of u32
  offsets (room for 1000 levels; the caller checks level < 1000) followed by RNC-packed level records
  that unpack to 0x979c bytes ("ERROR decompressing levels.dat").
* `level_skip_number_329c0`: level numbers 8, 0x11, 0x1c, 0x21, 0x27 are advanced past (speculation:
  these are the campaign positions where a cut-scene/intermission level sits).

### 2.6 Smaller systems

* Mana balls: size thresholds `DAT_00093910[7]` (int); sprites 0x69 + player*8 + size (player from
  owner playerrec+0x30), 0x34 + size when unowned (`mana_ball_update_sprite_25e20`).
  `thing_drop_mana_ball_25fe0` is the shared "drop carried mana" used by creature death and players.
* Village effects: types 50/51 (states 52/53, currently named ridge_node/type51) spawn random
  villagers through `creature_spawn_random_villager_27660` (Archer 2/12, Trader 2/12, Townie 5/12,
  Builder 3/12) and use `thing_apply_pending_damage_27f90`; state 53 also smooths terrain
  (`terrain_smooth_rect_34a00`). The level.py name "Ridge node" for type 50 looks wrong.
* Movie subtitles (ftext.dat): `movie_subtitle_init_23600` / `_show_236a0` / `_set_colour_23740`,
  strip at frame+0xe100 (DAT_000b2e04, size 0x4b00), state DAT_0009390a/0c, colour DAT_000adfd6,
  driven by cue_script_step_17d80.
* Music: `music_update_1f800` state bytes DAT_000938f8..fb (f9 = current mood 1/2, fb = fade step
  +-2, fa = HMI timer event pending), DAT_0009e310 = volume 100.
* Memory names: '*SearchD' (spiral-search buffer, 0x96df0) and '*WScreen' allocated by
  `video_alloc_buffers_33480`.
* Player record byte +0x340f (first byte of the 0x801 record) is set to 2 by `player_cheat_check_3bbd0`.

### 2.7 Open questions

1. `terrain_ring_find_height_ne8_24d70`: the ring-start radius comes from an uninitialised stack byte
   in the decompile; verify in the disassembly whether a register argument initialises [esp+8].
2. What texture 8 is (tested by the raise effects 24fc0/24eb0/250b0 and by 24d70): probably the
   plateau/cliff-top texture produced by paint kind 8.
3. The numeric arguments pushed to the generation passes in terrain_build_303f0 (river count, class
   thresholds) - read the disassembly of 0x303f0 for the exact constants.
4. The 0x94-entry corner-class tuple table and the slope-texture tables 0x94218..0x94298 should be
   dumped and documented as data (they define the whole texture vocabulary).
5. Why castle footprint dimensions are halved in 320x200 mode (26320, 26f10) - if the footprint is
   also the HUD castle icon, the terrain result differs by resolution, which would be a bug.
6. `FUN_00010010` (called by player_cheat_check_3bbd0) and `FUN_0003f240` (fire) are unnamed; the
   player record byte +0x340f meaning (value 2) is unknown.
7. 0x24d10 (effect_rain_of_fire_s26) is only ~0x44 bytes before its jump table at 0x24d54; check
   the function split there.
