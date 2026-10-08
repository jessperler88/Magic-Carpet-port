# carpet.exe renderer subsystem (FUN_0001fab0 subtree, FUN_0003a8b0, FUN_0002f480)

## 1. Names CSV

```
0x0001fab0,render_frame_1fab0,Per-frame draw dispatcher. switch on player byte +0x3855 (view mode): 0 = 3D view + HUD (render_view_2f6e0 then 43610/42a20 radar, 22870/219f0 bars, scoreboard text), 1 = full-screen map (memset frame; render_view; text legend), 2 = spell-selection panel (24 icons in 4 columns x=0x180..0x27f step 0x40, mouse DAT_0009e5dc/de), 4 = 3D + radar + player list. Also drives the credits scroller (cfg flag 4; PTR_s_Designed_by_0009861c).
0x0002f6e0,render_view_2f6e0,Chooses view path and post-filters: terrain-slope smoothing (DAT_00093f7c/80 from heightmap 0xDDFB0), stereo interlaced (state+0x219e && +0x219b; 640 wide; eye offset DAT_00093b1c = -5/+5), anaglyph (+0x219b==1; eye offset +-pitch/40), SIRDS autostereogram (+0x219b==2; LCG DAT_0012dfb0*0x24a1+0x24df; pixel = pixel[-depth]), mono; then 2x2 smoothing via blend table 0xBD9B0 (+0x219d) or motion-blur blend with previous frame DAT_000adf70 (+0x219c).
0x00029050,render_landscape_29050,Landscape renderer: args (cam_x,cam_y,yaw,cam_z,pitch,roll,zoom). Builds 40x21 vertex grid at DAT_000adf68 (44-byte records), reads 5 maps at 0xCDFB0/0xDDFB0/0xEDFB0/0xFDFB0/0x10DFB0, projects, culls at 20 cells, fog 15..19 cells, draws quads back-to-front as 2 triangles via poly_fill_triangle_722e3 then things in that cell via render_cell_things_*.
0x00028580,render_build_roll_table_28580,Builds Bresenham step table DAT_000b3a10 (3 dwords/entry) and index list DAT_000adf68+0xe7e0 for the screen roll angle (octant DAT_000b587c = angle>>8) using sin/cos 0x987ec/0x98fec; sets DAT_000b5858/5874/5878/5888/5890/5898/589c clip extents. Used by render_sprite_scaled_2ad60 to draw sprites along rolled screen axes.
0x0002f080,render_sky_2f080,Fills viewport with a rotated+scrolled 256x256 sky texture (addressed through a 16-bit segment offset (v<<8|u)); u scroll = yaw*0x8000>>16, rotation by roll (arg), per-row delta table on stack. Used when state+0x2197 (textured sky) else the viewport is cleared to 0xff/0x40.
0x000722e3,poly_fill_triangle_722e3,Triangle rasteriser. Sorts the 3 vertices (int x,y,...) by y, computes 16.16 edge slopes and dispatches on fill mode DAT_0009e309 via 4 jump tables (0x72459/0x72d39/0x7363a/0x73ab9). Modes used by the landscape: 5 = gouraud textured, 7 = flat shaded (shade DAT_0009e308), 0x1a = translucent/water, SIRDS pass writes depth (mode 1). The span fillers (0x72459..0x73f80) are undefined code, ~7 KB.
0x0002c600,render_cell_things_2c600,Draws the linked list of Things standing in one terrain cell (start index = vertex+0x24 from map 0x10DFB0, next = thing+0x14). Per thing: optional shadow (state+0x2196, at terrain_height_at_71e00, mode 8), rotate by yaw (DAT_000b5864/58a0), cull z<0x41 or dist^2>=DAT_000b584c, fog -> DAT_000b5818, pick sprite from table DAT_00097678 (14-byte records) by draw type (+0xc: 0/1/0x15 single, 0x11 8 rotations+mirror, 0x12 16 rotations, 0x13/0x14 remap tables DAT_00093f54/3f64, 0x16-0x24 animated by thing+0x58), ensure loaded (sprite_ensure_loaded_4bdd0), project, then render_sprite_scaled_2ad60.
0x0002dac0,render_cell_things_sirds_2dac0,Same as 2c600 for the SIRDS pass: DAT_000b5818 = (0x1400 - z) * -0x15e + 0x400000 (depth value instead of fog).
0x0002e5a0,render_cell_things_mirrored_2e5a0?,Same as 2c600 for the second-surface pass (state+0x2195): projects with z = -cam_z - thing_z (mirrored), no shadow.
0x0002ad60,render_sprite_scaled_2ad60,Leaf sprite blitter. Scales sprite (DAT_000b582c w signed for mirror, 583c h) to DAT_000b581c x DAT_000b5838 at DAT_000b5828/5824; clips to viewport DAT_0009b600/604; column step table at DAT_000adf68+0x9060 and per-row clip triples at +0xb360; rows walk the roll table DAT_000b3a10. Pixel 0 = transparent. 10 modes in DAT_000b5814: 0 copy, 1 shade (0xB99B0[shade<<8|c]), 2/3 blend with dest (0xBD9B0 both orders), 4/5 tint via blend table with fixed colour, 6/7 blend+shade, 8/9 solid colour (shadow).
0x00071e00,terrain_height_at_71e00,Interpolated terrain height at world (x,y) (cell = byte 1, fraction = byte 0 of each coord): height*0x20 + plane interpolation >> 3 using the same diagonal split as the renderer ((cx+cy)&1).
0x00071f08,terrain_fractal_fill_71f08,Diamond-square fractal on the 16-bit map at 0x10DFB0 (seed value at arg +0xc); 7 octaves; LCG *0x24a1+0x24df. Callers: terrain_build_303f0 and tables_load_or_generate_3eaa0.
0x00071f92,terrain_fractal_square_step_71f92,Square step of 71f08 (fills centre of 2s x 2s cell if still 0).
0x00072027,terrain_fractal_diamond_step_72027,Diamond step of 71f08 (two edge midpoints).
0x00072147,texture_average_colours_72147,For each texture pointer in DAT_0009afec averages a 32x32 block (stride 0x100 bytes/row... actually +0xe1 after 32 pixels => 256-byte rows) through the RGB palette (arg) and finds the nearest palette entry; builds a per-texture average-colour table (used by the overview map 43910/43610 through DAT_000cd9b0).
0x00078dd5,render_set_viewport_78dd5,Sets the software render target: DAT_0009b5f4 dest ptr (DAT_0009b5f0 = dest - pitch), DAT_0009b5f8 texture ptr, DAT_0009b5fc pitch, DAT_0009b600 width, DAT_0009b604 height (each only if non-zero).
0x0002f3c0,render_set_view_window_2f3c0,Computes DAT_000b5810 = offset of the reduced view window for size n (0x28-n cells; 8/12 or 4/5 pixel steps by resolution) and sets the viewport base DAT_0012ed74+offset.
0x0002f320,render_set_view_window_top_2f320,Variant of 2f3c0 used by view mode 4 (offset = pitch - 16*n or -8*n).
0x0002f480,vga_present_frame_2f480,End of tick: if anaglyph (+0x219b==1 and !DAT_00093f74) combines frame and DAT_000adf70 through 8 colour tables DAT_000b2e10..0xb3510 in place; then copies to VRAM: interlaced (7946d) / 16-bit anaglyph (79246, 7935b) / 320x200 (610f0) / banked VESA (4f974).
0x000610f0,vga_copy_320x200_610f0,rep movsd 16000 dwords from DAT_0012ed74 to 0xA0000.
0x0004f974,vesa_copy_banked_4f974,Copies the frame to 0xA0000 in 64 KB banks via vesa_set_bank_6126c (4 x 0x4000 dwords + remainder, 640x480 or x400 (arg==400)).
0x0007946d,vesa_copy_interlaced_7946d,Copies alternate 640-byte rows into two VRAM halves (bank switching) - shutter-glasses / interlaced stereo output.
0x00079246,vesa_present_anaglyph16_79246,Bank-switched present of left (DAT_0012ed74) and right (DAT_000adf70) eye buffers through anaglyph16_combine_793b0 (hi-colour anaglyph).
0x0007935b,vesa_present_anaglyph16_small_7935b,Single-bank variant of 79246.
0x000793b0,anaglyph16_combine_793b0,16-bit pixel = table[left] + table2[right] (two 256-entry word tables at ESI and ESI+0x200) written to 0xA0000.
0x0006126c,vesa_set_bank_6126c,int 10h AX=4F05 window A/B set to (bank<<6)/granularity; also resets DAC index for out-of-range banks.
0x0005b7f8,vga_blit_begin_5b7f8,Sets DAT_0009e5d4=1 (blit in progress, suppresses mouse-cursor interrupt drawing) and calls FUN_0005b35c.
0x0005b850,vga_blit_end_5b850,Clears DAT_0009e5d4.
0x0004bdd0,sprite_ensure_loaded_4bdd0,Make sprite index n resident: size = sprite_group_size_4b850, free = tmap_free_space_4c550; evict up to 4 LRU groups (4b9b0) until it fits; load group (4b8b0); sets cfg+0x95=5 (hourglass/disk icon?). Returns DAT_000b8d3c[n] != 0.
0x0004b850,sprite_group_size_4b850,Sum of chunk sizes (+10 header) of all sprites sharing group id (DAT_000b84f4 10-byte records, +8 = group id, 0x211 = 529 sprites).
0x0004c550,tmap_free_space_4c550,Returns +4 of the tmap cache struct (free bytes).
0x0004b9b0,sprite_cache_evict_lru_4b9b0,Finds the 5 loaded, unlocked (DAT_000b9580[group]==0) groups with the smallest timestamps DAT_000b7cb0[group] and unloads them.
0x0004b8b0,sprite_group_load_4b8b0,For every sprite of the group: tmap_find_chunk_4c560 + tmap_read_chunk_4b5e0 (RNC) + sprite_cache_register_4c880; stamps DAT_000b7cb0 with cfg+4 (frame counter).
0x0004b780,sprite_group_unload_4b780,Frees all sprites of a group (sprite_cache_find_4cb10 / sprite_ptr_clear_4ca50 / sprite_cache_compact_4c610); clears DAT_000b8d3c/0xb84f8/0xb7cb0 entries.
0x0004b690,sprite_group_unload_if_unlocked_4b690,As 4b780 but refuses locked groups (DAT_000b9580).
0x0004b5e0,tmap_read_chunk_4b5e0,Seeks (FUN_00061a80) to chunk offset DAT_000b84f4[n].+4 in tmaps.dat and reads/RNC-decompresses; prints "ERROR decompressing tmap%03d".
0x0004c560,tmap_find_chunk_4c560,Looks up chunk n in the container directory (14-byte entries) via FUN_0004c3d0.
0x0004c880,sprite_cache_register_4c880,Allocates a 0x1c-byte cache slot: +4 ptr to sprite ptr, +0xc 6 (header size), +0xe w*h+6, +0x12 w, +0x14 h (sprite header: +0 flags, +2 w, +4 h, +6 pixels).
0x0004cb10,sprite_cache_find_4cb10,Finds the slot whose +0x1a equals the sprite id.
0x0004ca50,sprite_ptr_clear_4ca50,Zeroes a (ptr,size) pair.
0x0004c610,sprite_cache_compact_4c610,Memmoves following blocks down over a freed block and fixes their back pointers.
0x000284f0,render_set_texture_uv_scale_284f0,Rewrites every non-zero entry of the 32 x 8-dword UV table DAT_00093b48 to (n<<16)-1 (texture size change, e.g. 64 vs 32 texel maps).
0x0003e420,math_rotate_offset_3e420,v[0] += sin(a)*d>>16; v[1] -= cos(a)*d>>16; with pitch p: v[2] -= sin(p)*d>>16 and d*=cos(p). Sin table 0x987ec, cos = +0x800.
0x00057331,render_set_buffers_57331,Sets DAT_0012ed74 (frame buffer) and DAT_000adf68 (work buffer).
0x000156b0,input_keyboard_handler_156b0,Big key switch; relevant: '>' toggles smoothing +0x219d, '?' toggles +0x2195 (second surface), 'A' toggles shadows +0x2196, 'D' cycles 3D mode +0x219b 0->1->2->0 (stereo_mode_enter_2ff50 / stereo_mode_leave_2ff10), sets view size +0x2198=0x28.
0x0002ff50,stereo_mode_enter_2ff50,Enters anaglyph mode: switches VESA mode (0x10e for 320 wide, 0x111 for 640; FUN_0002f680) and builds the DAT_000b2e10..0xb3510 colour tables from the palette.
0x0002ff10,stereo_mode_leave_2ff10,Restores video mode (FUN_00061480 / FUN_000613e0), DAT_00093f74=0.
0x0004a9a0,ui_draw_text_4a9a0,Draws a C string with font DAT_000adfbc (6-byte glyph records at font+(c+1)*6, +4 = advance; font+0xca = space width, +0xcb = line height; control chars 9/0x20 space, 0xa newline); stops at x>0x27f; ram 0x9e5c4 = busy flag.
0x0004abe0,ui_set_font_4abe0,DAT_000adfbc = font table DAT_000adf28[n].
0x0004abc0,ui_font_space_width_4abc0,Returns font+0xca.
0x0004abd0,ui_font_line_height_4abd0,Returns font+0xcb.
0x0004d240,ui_draw_glyph_4d240,Draws one glyph sprite via vga_draw_sprite_spans (606f8 in 320 mode / 6070d in 640 mode).
0x0006070d,vga_draw_sprite_spans_6070d,2D span-encoded sprite blitter into DAT_0012ed74 (pitch DAT_0012ed70): byte n>0 copy n pixels, n<0 skip -n, 0 end of row; clips to DAT_0012ed88/0x12ed98 (x0,y0) and DAT_0012ed80/0x12eda8 (x1,y1); halves size/coords when DAT_0012edae&1 (320x200).
0x000606f8,vga_draw_sprite_spans_lo_606f8,Thin wrapper of 6070d.
0x0006abbc,vga_fill_rect_clipped_6abbc,Horizontal/vertical line or rect fill in 640x400 coordinate space (>>1) on a 320-pitch buffer, clipped to DAT_0012ed88/ed98/eda4/ed90; flag 4 = colour through remap table.
0x000603f0,vga_draw_box_603f0,Four calls of 6abbc = rectangle outline (hi-res variant; 0x604c0 = lo-res).
0x00043610,ui_draw_radar_43610,Circular minimap: per row width from the 256-entry circle table DAT_000cdcb0 (sqrt(1-x^2)), rotated by yaw (sin 0x987ec), samples texture map 0xCDFB0 (-> DAT_000cd9b0 colour) shaded by light map 0xEDFB0 through 0xB99B0 and blended with 0xBD9B0.
0x00043910,ui_draw_map_43910,Rectangular overview map (full-screen map mode): same colour lookup as 43610, 2x2 pixel blocks in 640 mode.
0x00042a20,ui_draw_radar_blips_42a20,Draws Thing markers on the radar: iterates things from the local player's thing (+0x3415), relative position rotated by yaw, animated pulse ((tick&3)+4) via sin table, blips darkened through 0xBD9B0[c*0x100].
0x000219f0,ui_draw_status_bars_219f0,HUD bars (health/mana) of the local player's thing; rect fills FUN_000606c0/60688 (hi/lo), labels from DAT_00097630.
0x00022870,ui_draw_thing_label_22870,Text/icon label for a thing (name table DAT_00097630), boxes via 60610/60590.
0x00022d80,ui_draw_target_panel_22d80?,Panel for the thing referenced by player +0x2a (possessed creature/target); uses ui_fill_rect_blend variants.
0x00021370,ui_draw_player_list_21370,Multiplayer player list: counts players with +0x340b+6 set, names at player+0x3411, rect fills and text.
0x000224e0,ui_fill_rect_blend_224e0,Translucent rectangle: dest = 0xBD9B0[dest<<8|colour].
0x00022610,ui_fill_rect_blend2_22610,Translucent rectangle variant.
0x00023310,ui_shade_rect_23310,Darkens a rectangle: dest = 0xB99B0[shade<<8|dest].
0x0005afd0,mem_set_5afd0,memset(dest,byte,size) wrapper around FUN_000660d0 (args: size,?,?,?,dest,byte,size). NOTE: the existing name models_initialise_5afd0 is wrong; render_frame uses it to clear the frame (DAT_0012ed70*DAT_0012ed78 bytes).
0x0003a8b0,player_commands_process_3a8b0,NOT renderer: per-tick processing of the per-player 10-byte command records at state+0x7413 (join/name 1, leave 2, chat 0x11/0x13 incl. "RATTY" check, spell slot select 0x15-0x19, misc 0x1a-0x1e); increments frame counter cfg+4 and player tick +0x341d; parity bits cfg+0x5e..; network sync (4f360/4f530) and demo recording (3c540).
0x0003c540,demo_record_write_3c540?,When cfg flag 2 (recording) and handle cfg+9==0 opens the demo file (FUN_000619a0) and writes records.
0x00049720,sound_play_at_thing_49720?,Returns if sound disabled (DAT_0009e320/321); distance^2 cutoff 0x9000000 via FUN_0003e970; 68 callers.
0x0003c200,game_reload_state_3c200?,Saves the option bytes state+0x2195..0x21b8, reloads a state file with file_load_rnc_3cbe0 and restores them.
0x0004f360,net_player_sync_4f360?,Network: DAT_0009e3c8 active, DAT_0009e3ca local id, DAT_0009e3cc player count.
0x0004f530,net_players_exchange_4f530?,Network: calls 4f470/4f4d0 for every other player.
0x0003bb50,chat_message_show_3bb50,Sets player+0x44a, formats via FUN_0005ba5c, plays sound, then 3bbd0.
0x0003e080,player_log_position_3e080?,Appends thing x,y (+0x48/+0x4a) into a 14-byte-record list at player+0x24a.
```

## 2. ENGINE.md section: the renderer

### 2.1 Frame flow

```
game_tick_32f90 -> FUN_00032e80
   ... simulation ...
   render_frame_1fab0                      (view mode = player byte +0x3855)
       render_set_view_window_2f3c0(size)  (size = state+0x2198, 0x28 = full; F5/F6/F7 shrink it)
       render_view_2f6e0(cam...)           (stereo/SIRDS/mono selection, post filters)
           render_landscape_29050(cam_x, cam_y, yaw, cam_z, pitch, roll, zoom)   1x or 2x
               render_build_roll_table_28580(roll)
               render_sky_2f080(roll)      or clear
               [per cell] poly_fill_triangle_722e3 x2, then render_cell_things_2c600
                                                     -> render_sprite_scaled_2ad60
       HUD: ui_draw_radar_43610, ui_draw_radar_blips_42a20, ui_draw_status_bars_219f0,
            ui_draw_thing_label_22870, ui_draw_player_list_21370, ui_draw_text_4a9a0 ...
   FUN_0004ad80, FUN_0003ca00 (screenshot/dump when DAT_00094338)
   vga_present_frame_2f480                 (copy DAT_0012ed74 -> 0xA0000)
```

Camera arguments are pushed on the stack by render_frame_1fab0 (Watcom stack args;
Ghidra dropped the pushes, they appear as `in_stack_000000xx` in 2f6e0/29050).
Order seen in render_landscape_29050: +4 cam_x, +8 cam_y, +0xc yaw, +0x10 cam_z,
+0x14 pitch (horizon offset), +0x18 roll, +0x1c zoom/focal scale.

### 2.2 Video / frame buffer globals

| Global | Meaning |
|---|---|
| DAT_0012ed74 | 8-bit back buffer (whole screen). Cleared with mem_set_5afd0(w*h). |
| DAT_0012ed70 / DAT_0012ed78 | screen width / height (0x140 or 0x280; render_view checks `== 0x280`) |
| DAT_0012edae | video-mode flags: bit0 = 320x200 (UI coordinates are in 640x400 space and get halved: 6070d, 6abbc, 2f3c0), bit3 = 640x480. (ENGINE.md calls this "game mode"; the renderer code treats it as resolution.) |
| DAT_0012ed80/ed88/ed90/ed98/eda4/eda8 | 2D clip rectangle used by the span blitter / rect fill |
| DAT_000adf70 | second full-screen buffer: right-eye image for anaglyph/SIRDS, previous frame for the +0x219c blend. Allocated with FUN_00059870(64000) in FUN_00033600 (0 when not enough memory, which disables the 'D' key modes). |
| DAT_000adf68 | 64 KB+ work buffer: terrain vertex grid (840 x 44 = 0x9060 bytes), then sprite column table (+0x9060), per-row clip triples (+0xb360), roll index list (+0xe7e0). Also reused by the front end as a swap buffer and by 3bfc0 as a file buffer. Set by render_set_buffers_57331. |
| DAT_0009b5f4 / f8 / fc / 600 / 604 | current render target: dest, texture, pitch, width, height (render_set_viewport_78dd5). DAT_0009b5f0 = dest - pitch. |
| DAT_000b5810 | byte offset of the reduced view window inside the frame |
| DAT_0009e5d4 | "blit in progress" (mouse cursor lock) |
| DAT_0009e5dc / de | mouse x / y (640-space) |
| DAT_00093f74 | 16-bit colour anaglyph mode active |

VRAM output: 320x200 -> rep movsd 64000 bytes (vga_copy_320x200_610f0); VESA -> 64 KB banks via
int 10h 4F05 (vesa_set_bank_6126c, granularity at ram 0x12ef02); interlaced copy for shutter glasses
(7946d); hi-colour anaglyph through two word tables (793b0).

### 2.3 Map representation (5 parallel 256x256 maps, 0x10000 stride)

| Address | Element | Content (evidence) |
|---|---|---|
| 0xCDFB0 | byte | terrain texture index per cell -> vertex+0x29; DAT_0009afec[idx] = texture pointer (set in tables_load_or_generate_3eaa0); property tables DAT_00093a78 (water/translucent -> fill mode 0x1a), DAT_000939d4 (flat-shaded -> mode 7), DAT_00093930 (-> vertex+0x2b; non-zero suppresses shadows) |
| 0xDDFB0 | byte | height; world height = byte * 0x20 (29050: `h*0x20 - cam_z`; 71e00) |
| 0xEDFB0 | byte | light/shade per vertex: `light*0x100 + 0x80` then scaled by fog -> vertex+0x20 |
| 0xFDFB0 | byte | flags: bit 3 (0x08) = animated (water) - vertex height gets `-(sin(tick*0x40+x*0x80)*sin(..+y*0x80))>>10` and the shade gets the wave term; bits 2..4 (>>2 & 0x1c) = texture rotation/flip selector combined with the view quadrant -> vertex+0x2a -> UV set DAT_00093b48[sel*8] |
| 0x10DFB0 | u16 | index of the first Thing in the cell (0 = none) -> vertex+0x24; list continues through thing+0x14. The same 128 KB is used as the 16-bit scratch for the diamond-square generator (terrain_fractal_fill_71f08). |

Cell index = `(y_cell << 8) | x_cell` (CONCAT11 everywhere); world coordinates are 16-bit with the
cell in the high byte and the fraction in the low byte (1 cell = 256 units; map wraps at 256 cells).
Camera cell = cam >> 8 (with -1 rounding when fraction < 0x80 in render_view).

### 2.4 Camera, angles, trig

* Angles are 11-bit: 0..0x7ff = full circle (`& 0x7ff` everywhere). Sine table of 32-bit 16.16
  values in the data segment at **0x987EC** (sin(a) = `*(int*)(0x987ec + a*4)`), cos(a) = entry +0x800
  (0x98FEC). 0x983EC (= -256) and 0x98BEC (= +256) are used as sin(a-45deg)/sin(a+45deg)
  (render_landscape uses `yaw + 0x100` with those two, i.e. plain cos/sin of yaw for DAT_000b5864/58a0).
  The table therefore spans at least 0x983EC..0x9AFEC (2816 entries). No code writes it.
* Camera globals written by render_landscape_29050: DAT_000b58ac cam_x, DAT_000b58aa cam_y,
  DAT_000b58a8 yaw, DAT_000b5884 cam_z, DAT_000b5864 cos(yaw), DAT_000b58a0 sin(yaw),
  DAT_000b585c sin(roll), DAT_000b5870 cos(roll), DAT_000b5894 = width/2 + eye offset
  (DAT_00093b1c), DAT_000b586c = height/2, DAT_000b588c = horizon offset = pitch*width>>8,
  DAT_000b5854 = focal = sqrt(w^2+h^2) * zoom >> 8 (FUN_0004cd7a = isqrt), DAT_000b58af = shadows on.
* Projection: `sx = x_rot * focal / z`, `sy = (h - cam_z) * focal / z + horizon`, z clamped >= 0x80;
  then screen roll: `x' = cx + (x*cos - y*sin)>>16`, `y' = cy - (x*sin + y*cos)>>16`.
* Player tick counter player+0x341d drives water animation (`*0x40` phase).

### 2.5 Landscape algorithm (render_landscape_29050) and the draw-distance limits

1. Quadrant `q = ((yaw+0x100) >> 9) & 3`; angle inside quadrant `a' = ((yaw+0x100)&0x1ff)-0x100`;
   `local_64/68 = sin/cos(a')`. Per-quadrant 10-byte table at **DAT_00093b20** (+0/+1 start cell
   offset, +2/+3 texture-corner offset, +4/+5 thing-cell offset, +6/+7 row step, +8/+9 column step).
2. Vertex grid: **40 columns x 21 rows** (0x28 x 0x15 = 0x348 = 840 records of **44 bytes**) at
   DAT_000adf68. Column coordinate runs -0x1300..+0x1400 (cells -19..+20 sideways), row coordinate
   -0x100..+0x1400 (cells -1..+20 forward), both in 1/256 cell. Rotated by a' into camera space:
   rec+0 = x_cam, rec+0xc = z_cam.
3. Per vertex: `d2 = x^2 + z^2`; culled (flag 0x02) when `z < -0xff` or `d2 >= DAT_000b584c
   (0x1900000 => 5120 units = 20 cells)`. Fog: full light below DAT_000b5848 (0xE10000 => 15 cells),
   linear fade to 0 at DAT_000b5850 (0x1690000 => 19 cells), divisor DAT_000b5844 (0x880000).
   The same four constants are used for Things in render_cell_things (plus near cull z < 0x41).
4. Flags byte rec+0x26: bit0 = (cx+cy)&1 (which diagonal splits the quad), bit1 = culled, bit2 =
   column is on the far side of the view axis (set when the lateral coordinate >= 0; the row loop
   draws columns left-to-centre then right-to-centre for painter's order), bits 3..6 = off-screen
   left/right/top/bottom, bit7 = translucent texture; rec+0x27 bits 0..3 = second surface
   off-screen, bit4 = flat-shaded texture.
5. Draw loop: rows from far (row 20) to near, 39 quads per row; each quad = 2 triangles via
   poly_fill_triangle_722e3 with (x,y,shade) triples, `DAT_0009e309` = 5/7/0x1a, texture pointer
   `DAT_0009b5f8 = DAT_0009afec[tex]`, UVs from `DAT_00093b48[sel*8..+7]`; then the Thing list of
   the cell (rec+0x24 != 0).
6. Second surface (state+0x2195, key '?'; also when cam_z < 0x1000): rec+0x8 = mirrored height
   `-cam_z - ((wave>>4 + 0x8000) * h >> 10)`, projected to rec+0x18/0x1c and drawn with
   render_cell_things_mirrored_2e5a0 - a ceiling / water-reflection pass.

**To raise the draw distance in a port**: enlarge the grid (0x28/0x15 counts, the 0x1b8-int row
stride = 40 records, 0x2260-int start = row 20, the -0x1300/-0x100 start offsets, the 0x348 loop
count), the DAT_000adf68 layout (the sprite tables start at +0x9060 right after 840 records), and the
four squared-distance constants (0x1900000 / 0x1690000 / 0xE10000 / 0x880000, also the SIRDS depth
scale `0x1400 - z` and the thing cull in 2c600/2dac0/2e5a0). Everything else is data driven.

Vertex record (44 bytes): +0 x_cam, +4 height-cam_z, +8 mirrored height, +0xc z_cam, +0x10 screen x,
+0x14 screen y, +0x18/+0x1c screen x/y of mirrored vertex, +0x20 shade (light<<16 scaled by fog,
plus wave term), +0x24 u16 first thing index, +0x26 flags, +0x27 flags2, +0x29 texture, +0x2a UV
selector, +0x2b texture property.

### 2.6 Things as seen by the renderer

Thing pool: record i at `state + 0x7463 + i*0xa4` (so the first real record, i=1, is at +0x7507).
Fields used: +0x10 flags (0x21 = invisible/hidden, 0x80 = silent), +0x14 next thing in cell, +0x1e
facing angle, +0x48/+0x4a/+0x4c x/y/z (shorts), +0x56 sprite type (index into DAT_00097678), +0x58
animation frame, +0xa0 pointer to owner/player data.
Sprite type table DAT_00097678: 14-byte records: +0 u16 base sprite index, +8 u16 world size,
+0xa byte shade-group (indexes DAT_00093f48 lit / DAT_00093f4e fogged -> pixel mode; non-zero in
DAT_00097682 also means "no shadow"), +0xc byte draw type (see 2c600 in the CSV).
Sprite cache: DAT_000b8d3c[n] = pointer to pointer to sprite (header +0 flags (bit3 = used this
frame), +2 w, +4 h, +6 pixels, raw w*h bytes, 0 = transparent) for 0x211 sprites; DAT_000b84f4 =
tmaps directory (10 bytes/entry, +4 file offset, +8 group id); DAT_000b7cb0[group] = last-use frame
counter for LRU eviction; DAT_000b9580[group] = locked.
Note: 3D sprites are **not** span encoded (render_sprite_scaled walks raw w*h pixels with 0 =
transparent). The span encoding (n>0 copy, n<0 skip, 0 = end of row) is the 2D HUD/font format
handled by vga_draw_sprite_spans_6070d.

### 2.7 Tables

* 0xBD9B0 blend table: `blend[a<<8 | b]` 256x256 (render_view smoothing/motion blur, sprite modes
  2..7, HUD translucency, radar).
* 0xB99B0 shade table: `shade[level<<8 | colour]`, level 0..0x20 (sprite mode 1/6/7, ui_shade_rect,
  radar lighting). This is the start of the tables.dat image.
* 0xCDCB0 256-entry circle profile (sqrt(1-x^2) 8.8): row half-widths of the circular radar
  (ui_draw_radar_43610 `DAT_000cdcb0[i] * radius >> 8`).
* 0xCD9B0 (just below): per-texture colour used by the overview map / radar (`DAT_000cd9b0[tex]`),
  probably produced by texture_average_colours_72147.
* DAT_00093b48: 32 x 8 dwords texture UV corner table (16.16), rescaled by render_set_texture_uv_scale_284f0.
* DAT_000b2e10..DAT_000b3510: 8 x 256 anaglyph colour tables built by stereo_mode_enter_2ff50.
* DAT_000b3a10: roll line table (3 dwords per screen column/row) built per frame by 28580.

### 2.8 Render option bytes in the game-state block

+0x2195 second surface ('?'), +0x2196 shadows ('A'), +0x2197 textured sky (set with 2195 from
cfg byte +8 in config_parse_33750), +0x2198 view window size (0x28 = full), +0x2199/+0x219a HUD
parts, +0x219b 3D mode (0 off, 1 anaglyph, 2 SIRDS; key 'D'), +0x219c motion-blur/ghost blend (set
automatically when the player's thing speed |+0x7e| > 0x50 in single-player), +0x219d 2x2 smoothing
('>'), +0x219e interlaced stereo (set by config_parse when a stereo driver is detected, and by
FUN_00059330), +0x21ad..0x21b7 "option available" bytes (all 1 at startup).

### 2.9 FUN_0003a8b0 verdict

Not part of the renderer and not camera setup: it is the per-player command/message processor
(network and demo playback) - join/leave/name/chat/spell-slot commands from the 10-byte records at
state+0x7413, plus the frame counters. Its callees are sound (49720), network sync (4f360/4f530),
demo recording (3c540), chat (3bb50/3bbd0), position log (3e080) and state reload (3c200).

### 2.10 Open questions

1. The polygon span fillers behind the four jump tables in 722e3 (0x72459..0x73F80, ~7 KB) are not
   defined as functions; fill modes 0x1a (water) and 1 (SIRDS depth) need confirming from them.
2. Which segment the sky texture (render_sky_2f080) and the SIRDS pattern (0x24080 + ...) are
   addressed through (FS/GS base); the C export shows them as near pointers.
3. Exact meaning of the view-mode-4 screen and of player bytes +0x2199/+0x219a.
4. What writes DAT_00093b20 (per-quadrant step table) and DAT_00093930/0x939d4/0x93a78 texture
   property tables - probably static data; check in the data segment.
5. mem_set_5afd0 contradicts the existing name models_initialise_5afd0 ("Initialise Models" is
   printed by its caller, not by it) - rename.
