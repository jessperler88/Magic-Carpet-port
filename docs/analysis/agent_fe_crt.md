# carpet.exe: front end / menus + C runtime / DOS glue

Evidence base: decompiled bodies in carpet_all.c, raw bytes of the LE image (strings pushed as imm32 were
recovered by scanning each function's bytes for pointers into 0x510c0-0x51f00 and 0x90000-0x9e800, because
Ghidra's string xrefs miss register/push loads), and the switch jump table of frontend_menu_loop at 0x52038.

## 1. Names (CSV)

```
address,name,comment
0x00033750,config_parse_33750,Command line parser: options (prefix - or /): digijoy anojoy anojoy4 vfx help debug network custom setsound demo<n> detail<n> cheat<n> name<s> level<n> movie<n> roll<n> time password<n> players<n> session<s>; then allocates game state (0x38d03) and config (0x8e7e) via mem_alloc_59870
0x00052070,frontend_menu_loop_52070,One FE iteration: loads sptrs.dat on entry; fe_input_poll; switch(DAT_0012ed2e) via jump table 0x52038 -> screens; music_update; on DAT_0009e504 frees resources and stops music
0x00051ed0,fe_init_state_51ed0,Resets all FE globals (state=6 language select; ed29=7 if sndsetup.inf present else 0 = run sound setup); writes default language.inf
0x00057af0,fe_sndsetup_read_57af0,Reads c:%s\sndsetup.inf / sndsetup.dat (SOUNDFX= / MUSIC= lines) into DAT_0012eb00.. strings; returns nonzero if present
0x00057cc0,fe_input_poll_57cc0,Converts raw keys/mouse/joystick into edge-triggered FE flags DAT_0012ec44..48 and cursor DAT_0012ec3c/3e; input mode DAT_0012ed2f (0 none 1 keyboard 2 mouse)
0x0005a4e0,input_joystick_to_cursor_5a4e0,Joystick/VFX1 polling -> cursor position (clamped to 640x400 in FE) and button flags DAT_0012ee0c/0e/12/14; keyboard emulation DAT_0012ee68/6b/6d/70
0x000521c0,fe_screen_config_521c0,State 1: game config screen (gconfig.dat). Sound-setup wizard steps DAT_0012ed29 0..7 (digital card/port/irq/dma/midi card/...), writes "SOUNDFX = %s %s %s %s" and "MUSIC = %s %s 0 0" to sndsetup.inf; input-device page DAT_0012ed33 -> fe_config_apply_input_device_59330
0x00053070,fe_config_screen_init_53070,Loads sfont0.dat + data/screens/gconfig.dat/.pal; fades in
0x000531e0,fe_config_screen_exit_531e0,Fade out and free config screen resources
0x00052ab0,fe_config_draw_52ab0,Draws option list for current step and the input-device sprite (table 0x51e0e 8-byte entries x,y,sprite,flags)
0x00052bf0,fe_config_option_table_52bf0,Returns {string ptr,id} table for sound step (0x51bbc digital cards 0x51c1c midi cards ... terminated by "!")
0x00052c40,fe_config_draw_options_52c40,Draws the option table with highlight colours (DAT_0012ed2b hovered DAT_0012ed2a selected)
0x00052e20,fe_config_draw_summary_52e20,Draws "%s :" / "%s : %s" summary lines of chosen sound settings
0x00052a70,fe_config_option_valid_52a70,True if hovered index DAT_0012ed2b is before the "!" terminator of the option table
0x00059330,fe_config_apply_input_device_59330,Sets DAT_0009e583 from page flags: bit2 digital joystick (joystick_digital_init 0x24) bit1/0x20 analog (joystick_analog_calibrate 60x30) bit8 VFX1 (vfx1_init_4fdc0 -> state+0x219e)
0x000532b0,fe_screen_main_menu_532b0,State 2: main menu. Hit-test via mask image byte under cursor (DAT_0012ed26 = item id 1..11); item table 0x5167c (6-byte {handler,kind}); kind 1/2 enter load/save slot mode DAT_0012ed2d, kind 3 start; attract mode after 0x12c0 idle ticks cycles intro movie / title / demo level
0x00053d30,fe_main_menu_init_53d30,Loads mmspr.dat sprites (relocated x2 in hi-res), mainmenu.dat/.pal, mmmask.dat; builds bright table; reads save slot names
0x00053c40,fe_main_menu_animate_53c40,Advances the two FLI animations embedded in the main menu every other frame (frames DAT_0012ecda/ece8)
0x00053ad0,fe_input_idle_check_53ad0,Returns 1 when mouse/keys unchanged since last call (attract-mode timer)
0x00053bf0,fe_menu_item_enabled_53bf0,Item 3 needs network (DAT_0009e3c8); 7 and >=8 need slot mode; 11 disabled while game in progress (DAT_0009e500)
0x00053bc0,fe_menu_select_next_53bc0,Keyboard: next enabled item (wraps 1..11)
0x00053b90,fe_menu_select_prev_53b90,Keyboard: previous enabled item
0x000540c0,fe_main_menu_draw_overlay_540c0,Draws 6 save-slot names (PTR_s_Game_One_0009e4e8 centred with font DAT_0009e510) in slot mode else the two menu button sprites (6 and 12)
0x00054010,fe_main_menu_exit_54010,Fade out hide cursor clear free menu resources
0x000593c4,fe_highlight_masked_item_593c4,Copies background to back buffer remapping pixels whose mask byte == item id through the bright table (hover highlight)
0x000579f0,fe_build_bright_table_579f0,Builds 256-entry brightened remap table via palette_find_nearest
0x00058f60,fe_savegame_read_names_58f60,Opens save\carpet%02X.gam 0..5 checks version 4 reads 0x14-byte slot name into 0x9e4e8 table
0x00059030,fe_savegame_load_59030,Loads save slot: ver u32(=4) name[0x14] cfg+0x1d[0x20] cfg+0x3d[0x20] state+0x2195[0xc] checksum u32 state+0x3bd6[0x18] DAT_0012ed30 DAT_0012ed31 state+0x2195[0xc]; level = checksum/4-ed30-ed31
0x000591d0,fe_savegame_save_591d0,Writes the same record with crt_write
0x000541f0,fe_save_slot_dialog_541f0,Plays intro\scroll.dat lets the user type the slot name (ui_text_input) OK/Cancel buttons; returns 1 to save
0x00056e20,fe_confirm_dialog_56e20,Plays intro\scroll.dat and shows Yes(sprite 0x1e)/No(0x24) buttons with optional draw callback; returns 1 on yes
0x00054640,fe_text_entry_begin_54640,Clears key state and runs ui_text_input for the slot name
0x000546e0,fe_dialog_draw_buttons_546e0,Redraws dialog background and button sprites
0x00057617,fe_dialog_scroll_text_57617,Scroll dialog with two ui_text_input fields then returns to main menu (state 2)
0x00058290,ui_text_input_58290,Keyboard text entry: scancode->ASCII tables at 0x51e55/0x51e8e (shifted) width-limited draws with font
0x00057480,fe_menu_new_or_resume_game_57480,Item 1: if game in progress resume (state 5 leave FE) else confirm -> level 0 clear progress record state+0x3bd6 leave FE
0x00057270,fe_menu_quit_57270,Item 4: confirm dialog; yes -> player record byte +0x340f = 1 and DAT_0009e504 = 1 (quit to DOS)
0x000573fc,fe_menu_multiplayer_573fc,Item 3: if network active go to state 4; backs up player record to state+0x2409 and level to cfg+0x13
0x0005744c,fe_menu_start_level_5744c,Item 11: start current cfg level without confirm (DAT_0009e500=0 state=5 leave FE)
0x000574fa,fe_menu_start_level_dup_574fa?,Identical copy of 5744c (unreferenced)
0x00057580,fe_menu_item2_redraw_57580?,Unanalysed handler of item 2 (table 0x5167c): copies background rows (calls gfx_copy_rows); Ghidra did not create a function here
0x0005752c,fe_draw_centered_text_5752c,Centres a string in the clip rect with font DAT_0009e510
0x00057331,fe_set_screen_buffers_57331,Sets DAT_0012ed74 (back buffer) / DAT_000adf68 (background)
0x000573a0,fe_reset_clip_320x200_573a0,gfx_set_clip_window(0 0 320 200)
0x00054bd0,fe_screen_multiplayer_54bd0,State 4: multiplayer lobby (pmulti.dat). Level list scroll DAT_0012ed1e (levels 50+) 8 slots DAT_0009e520 (3 bytes each) session name cfg+0x75 "CARPET%d"; Start -> net_join_session player count -> state+0xa
0x00055630,fe_multiplayer_init_55630,Loads pmultspr.dat pmulti.pal/.dat font
0x00055210,fe_multiplayer_draw_slots_55210,Draws 8 player slot sprites (positions table 0x51d1c) with join/ready state animation
0x00055870,fe_multiplayer_refresh_55870,Clears slot state and redraws lobby
0x000559c0,fe_multiplayer_slot_joined_559c0,Marks slot as joined (DAT_0009e522[n*3]=2) and redraws
0x000557c0,fe_multiplayer_exit_557c0,Fade/clear/free; restarts music when returning to menu
0x00055b00,fe_screen_level_result_55b00,State 5: end-of-level stats (pperf.dat sfont2). Plays levelw1/levelw2 (win) or levelose (lose) FLI then reveals six lines over time (DAT_0012eab4+0x1e..0xb4) with "% 3d %%" and "%dh% 02dm %02ds"; level name from table 0x97490[level]; next state 10 (outro) if level==50 else 2
0x00056940,fe_screen_language_56940,State 6: language select (language.dat/.pal langspr.dat): 4 flags -> cfg+0x97 (0 E 1 F 2 G 3 I); writes language.inf; loads data/{e,f,g,i}text.dat; next state 1
0x00054900,fe_screen_intro_movie_54900,State 0: plays intro\intro.dat FLI (fli_play) frees movie
0x00054ab0,fe_screen_outro_movie_54ab0,State 10: plays intro\outro.dat
0x00056510,fe_screen_intel_logo_56510,State 7: intro\intel.dat
0x000563c0,fe_screen_bullfrog_logo_563c0,State 9: intro\logo.dat
0x00056730,fe_screen_title_56730,State 8: intro\title-01.dat / title-02.dat
0x000508f0,fli_play_508f0,Plays a FLI animation: fli_event_script per frame loops fli_next_frame/fli_present until end or key/mouse abort (DAT_0012eabc)
0x00050de0,fli_set_target_50de0,Stores frame width/height globals DAT_0009e45e/60
0x00050dfd,fli_next_frame_50dfd,Reads chunk size+magic; 0xAF12 -> file header; 0xF1FA -> fli_decode_frame
0x00050e88,fli_decode_frame_50e88,Iterates sub-chunks: 4 COLOR256 skipped (50f60) 7 SS2 (50f71) 15 BRUN (51024)
0x00050f36,fli_read_file_header_50f36,Reads w/h from FLI header
0x00050f60,fli_skip_chunk_50f60,Skip chunk by size
0x00050f71,fli_decode_ss2_50f71,FLI_SS2 word-run delta decoder
0x00051024,fli_decode_brun_51024,FLI_BRUN byte-run decoder
0x00050520,fli_error_unknown_frame_50520,printf "ERROR UNKNOWN FRAME TYPE" (never returns)
0x00050600,fli_present_frame_50600,Calls per-frame callback DAT_0012ea9c waits timing (50430) blits 320x200
0x00050430,fli_wait_frame_50430,Frame pacing against DAT_0012eab4 tick counter; abort on key/mouse
0x00017d80,fli_event_script_17d80,Movie event list (7-byte records {frame cmd arg}): music track / sound bank load / stop synced to FLI frame
0x000236d0,movie_buffer_clear_236d0,Zero the movie buffer DAT_000b2e04
0x00023700,movie_free_23700,Clear + free movie resources
0x00058ab0,ui_draw_text_58ab0,Draws string with font struct (ctrl codes: 1 colour1 2 colour2 3/4 5/6 flags 10 newline); glyphs 6-byte entries {u32 off u8 w u8 h}
0x00058970,ui_text_width_58970,Pixel width of widest line (sum of glyph w-1)
0x00058ba0,ui_draw_glyph_58ba0,Span-encoded glyph blit with clip rect DAT_0012ead0..eadc and colour remap (1->font+6 2->font+7)
0x000589d0,ui_font_init_589d0,Font struct {glyph tab ptr +4 flags=3 +6/+7 colours via palette_find_nearest}
0x000588b0,ui_set_clip_rect_588b0,Sets DAT_0012ead0/d4/d8/dc/e0/e4 (x x2 y y2 w h)
0x00058930,ui_push_clip_rect_58930,Saves clip rect to DAT_0012eae8
0x00058950,ui_pop_clip_rect_58950,Restores clip rect
0x000588f0,ui_clip_rect_width_588f0?,Returns clip rect extent used for centring
0x00060688,ui_draw_sprite_60688,Draw sprite n at x y (table entry +4 = w/h) via gfx_draw_sprite_spans
0x000606c0,ui_draw_sprite_hires_606c0,Same for the hi-res sprite table
0x000606f8,ui_draw_sprite_raw_606f8,Direct wrapper of gfx_draw_sprite_spans
0x0006070d,gfx_draw_sprite_spans_6070d,Span-encoded sprite blitter with clipping to window DAT_0012ed88/98/a8; halves coords in 320x200 mode
0x00062a80,sprite_table_relocate_x2_62a80,Adds base to 6-byte sprite tab offsets and doubles w h (hi-res)
0x00062ad0,sprite_table_relocate_62ad0,Adds base to offsets
0x00062b10,sprite_table_unrelocate_half_62b10,Inverse of 62a80
0x00062b90,sprite_table_unrelocate_62b90,Inverse of 62ad0
0x0004f974,vesa_blit_backbuffer_640_4f974,Copies DAT_0012ed74 to A000 through 4 (480 lines) or 3+ (400 lines) VESA banks (vesa_set_bank between)
0x000610f0,vga_blit_backbuffer_320_610f0,Copies 64000 bytes to A000 (hides/shows cursor)
0x0004cea9,gfx_fill_rows_640_4cea9,Fill n rows of 640 bytes with colour
0x0004ce83,gfx_fill_rows_320_4ce83,Fill n rows of 320 bytes
0x00065bac,gfx_copy_rows_640_65bac,Copy n rows of 640 bytes
0x00065b90,gfx_copy_rows_320_65b90,Copy n rows of 320 bytes
0x00065c10,gfx_set_clip_window_65c10,Sprite clip window DAT_0012ed88 x ed98 y ed80 w eda8 h eda4 x2 ed90 y2
0x0006abbc,gfx_fill_rect_clipped_x2_6abbc,Clipped rect fill (coords doubled)
0x0006acd4,gfx_fill_rect_clipped_6acd4,Clipped rect fill
0x000603f0,gfx_draw_rect_outline_x2_603f0,Four 1-pixel edges via 6abbc
0x000604c0,gfx_draw_rect_outline_604c0,Four edges via 6acd4
0x00060590,gfx_fill_rect_320_60590,Unclipped fill coords halved pitch 320
0x00060610,gfx_fill_rect_640_60610,Unclipped fill pitch 640
0x00060f3c,gfx_put_pixel_320_60f3c,Bounds-checked pixel (coords halved)
0x00060f7c,gfx_put_pixel_640_60f7c,Bounds-checked pixel 640x480
0x00079246,gfx_blit_stereo_640_79246?,Combines back buffer and DAT_000adf70 through a 16-bit LUT into VESA banks (3D-glasses / VFX stereo mode)
0x0007935b,gfx_blit_stereo_320_7935b?,Same for 320x200
0x000793b0,gfx_blend_rows_lut_793b0,Row combiner used by the stereo blits
0x00078dd5,gfx_set_screen_geometry_78dd5,Sets DAT_0009b5fc width DAT_0009b5f4 pitch etc. (non-zero args only)
0x0005ba5c,ui_set_mouse_cursor_5ba5c,Renders sprite entry into 64x64 cursor buffer DAT_0012edf8 (0xfe transparent); 0 = hide
0x0005b35c,mouse_cursor_draw_5b35c,Saves background under cursor to DAT_0012edc8/edb8 and draws cursor buffer
0x0005b7f8,mouse_cursor_hide_5b7f8,Sets DAT_0009e5d4 lock and draws cursor into back buffer before blit
0x0005b850,mouse_cursor_show_5b850,Clears DAT_0009e5d4
0x0005bc14,mouse_init_5bc14,int 33h reset; installs handler (AX=0Ch mask 0x7f) at LAB_0005b86c; allocates 3x0x1000 cursor buffers; DAT_0009e5e4=1
0x0003d070,mouse_reset_3d070,int 33h AX=0; DAT_0009e5e4=0
0x0003ed60,input_init_3ed60,Sets video mode (320x200 or VESA 640x480 per DAT_0012edae) mouse_init ("ERROR : MOUSE DRIVER NOT FOUND.") hides cursor installs keyboard handler
0x0004fb46,keyboard_install_handler_4fb46,Clears 128-byte key table DAT_0012ee20; saves int 9 vector; installs LAB_0004fa28
0x0003ee70,input_shutdown_3ee70,Mouse reset restore int 9 restore text mode net_shutdown timer_restore
0x0005afa0,vga_restore_text_mode_5afa0,int 10h with saved mode DAT_0012edac
0x0004ad0a,timer_restore_4ad0a,Restores PIT and int 8 vector saved at DAT_000b7ca0
0x000613e0,vga_set_mode_320x200_613e0,int 10h 0x0F (save mode) then 0x13; pitch 320 height 200
0x00061480,vesa_set_mode_640x480_61480,Save mode; VESA mode 0x101; pitch 640 height 480
0x00061208,vesa_set_mode_61208,int 10h 4F02 then vesa_get_mode_info
0x00061130,vesa_get_mode_info_61130,4F01 into DOS buffer DAT_0012ef00; granularity to DAT_0012ef04
0x00061300,vesa_get_info_61300,4F00 and strncmp "VESA"
0x0006126c,vesa_set_bank_6126c,4F05 window A and B = (bank<<6)/granularity; debug: palette 0 red when bank out of range
0x00065b10,vga_wait_vsync_65b10,Poll port 3DAh bit 3
0x000302f0,vga_set_palette_302f0,256 x out 3C8/3C9 from 768-byte table (plus VFX1 palette hook)
0x00061ec8,vga_read_palette_61ec8,Reads 768 bytes via 3C7/3C9
0x00061510,vga_palette_fade_61510,Fades current palette to target (or black) in N steps with vsync; previously misnamed timer_sync
0x00061718,vga_fade_reset_61718,Clears fade-in-progress flag DAT_0009e860
0x00060fc0,palette_find_nearest_60fc0,Nearest palette index for RGB (16 entries in 16-colour modes)
0x0002ff10,video_set_mode_2ff10,Chooses 320x200 or 640x480 setter by DAT_0012edae
0x00030350,video_mode_change_pending_30350,Applies requested mode state+0x219b (and stereo flip when VFX enabled)
0x0005be68,video_mode_extra_5be68?,Extra int 10h calls for DAT_0012edae==8 / ==2 modes (AX=7/8 with 0x1400/0xf00)
0x00061f80,mem_pool_init_61f80,Allocates DOS memory regions (dos_alloc_dos_memory) into region table 0x12f520 (12-byte) and block table 0x130120 (18-byte nodes)
0x00059870,mem_alloc_59870,Best-fit allocator over block list (node {ptr size next prev used type}); zero-fills
0x000598f0,mem_alloc_low_598f0,Same but only from regions with a real-mode address (DOS memory): VESA info buffer network buffers
0x00059a10,mem_free_59a10,Marks block free and merges neighbours
0x00059a60,mem_free_split_59a60?,Free with split then merge
0x00059ad0,mem_shrink_block_59ad0,Shrinks a block to new size giving the tail to the next free block
0x00059980,mem_split_block_59980,Splits a block (uses free node from table)
0x00059b90,mem_merge_next_59b90,Merges block with following free block of same type
0x000622a8,mem_update_stats_622a8,Totals at 0x131320 total/free/used DAT_0013132c largest free 0x131330 smallest
0x00059860,mem_set_owner_tag_59860,Sets DAT_0009e560 used by subsequent allocs
0x00059500,mem_init_pools_59500,Memory budget by largest free block: sets DAT_0009e328 class state+0x21a0 texture block size 16/32 (block16.dat/block32.dat) disables music/sound when <1MB; "ERROR : NOT ENOUGH MEMORY."
0x00059760,mem_check_lowmem_59760,DAT_0009e55c = low-memory flag
0x00059810,mem_grow_check_59810,"ERROR : UNABLE TO GROW MEMORY."
0x0006239c,dos_alloc_dos_memory_6239c,DPMI 0x0100 via int386; returns seg:sel
0x00062410,dos_free_all_dos_memory_62410,Frees all regions in 0x12f520
0x0006248c,dos_free_dos_memory_6248c,DPMI 0x0101
0x0005ae80,file_load_resource_list_5ae80,Loads a NULL-terminated list of 0x2c-byte resource records {name +0x1c dest ptr* +0x20 end ptr* +0x24 size +0x28 flags bit0 low-mem}; "ERROR: Allocation %s." / "ERROR: File %s."
0x00065eb0,file_load_resource_65eb0,Allocates (mem_alloc or mem_alloc_low) and loads one record via file_load_rnc; name "*..." = buffer only
0x00065e70,file_free_resource_65e70,mem_free the record's buffer
0x000610c0,file_free_resource_list_610c0,Frees every record of a list
0x00063450,file_get_unpacked_size_63450,Reads 8-byte header: "RNC\x01" -> big-endian unpacked size else file length
0x00061ab0,rnc_unpack_61ab0,RNC ProPack method 1 decompressor (magic 'RNC' 0x01)
0x00061c90,rnc_get_bits_61c90,Bit reader
0x00061c44,bswap32_61c44,Big-endian u32 read
0x00061c4d,rnc_huff_decode_61c4d,Huffman symbol decode
0x00061d13,rnc_huff_build_61d13,Huffman table build
0x00061db0,file_write_whole_61db0,open/write/close helper
0x00061eec,file_length_61eec,lseek end / restore
0x0003cc60,file_missing_3cc60,True if open fails
0x0003cca0,file_copy_3cca0,Copies a file (CD -> HD) using read/write through the back buffer
0x0003cf20,cd_check_files_3cf20,Checks/copies CD files and prints progress with the text-mode console routines
0x0005b02a,crt_printf_5b02a,printf (va wrapper around crt_vfprintf_core with stdout FILE 0xa2856)
0x000603bc,crt_sprintf_603bc,sprintf: __prtf into buffer then NUL terminate (45 callers)
0x0006a0e6,crt_prtf_core_6a0e6,__prtf: '%' parsing loop calls spec parser and converter output via callback
0x0006a395,crt_prtf_parse_spec_6a395,Width/precision/h l L F N flags
0x0006a4d5,crt_prtf_parse_flags_6a4d5,- # + space flags
0x0006a6ef,crt_prtf_convert_arg_6a6ef,Conversion of d i o u x X c s p n
0x0006a538,crt_strnlen_6a538,Length limited to precision
0x0006a55e,crt_wcsnlen_6a55e,Wide variant
0x0006a58f,crt_prtf_zero_pad_6a58f,Inserts leading zeros
0x0006a5f1,crt_prtf_fixed_point_6a5f1?,Prints integer with decimal point (Watcom fixed-point %f replacement)
0x0006a6d4,crt_prtf_float_stub_6a6d4,Calls "Floating-point support not loaded"
0x00070860,crt_no_fp_support_70860,Fatal message helper
0x00066193,crt_vfprintf_core_66193,FILE-based printf core (buffer alloc flush)
0x00062f57,crt_fprintf_62f57,fprintf
0x0006c23a,crt_scnf_core_6c23a,__scnf scanf core
0x0006c452,crt_scnf_parse_spec_6c452,Width/suppression parse
0x0006c52f,crt_scnf_skip_ws_6c52f,Skip whitespace via ctype table 0x937b4
0x0006c566,crt_scnf_char_6c566,%c store
0x0006c5f3,crt_scnf_string_6c5f3,%s store
0x0006c70c,crt_scnf_store_count_6c70c,%n store
0x0006c777,crt_scnf_build_set_6c777,%[...] bitmap
0x0006c7bb,crt_scnf_set_6c7bb,%[...] match
0x0006c8c1,crt_scnf_number_6c8c1,Integer conversions
0x0006cc04,crt_scnf_float_6cc04?,Float conversion
0x0006ce2d,crt_hexdigit_value_6ce2d,0-9 a-f -> value else 16
0x0006ce5c,crt_scnf_getc_6ce5c,Width-limited getc
0x00063169,crt_sscanf_63169,sscanf
0x000631a5,crt_sscanf_wrap_631a5,sscanf wrapper
0x00063257,crt_fscanf_63257,fscanf
0x00063293,crt_fscanf_wrap_63293,fscanf wrapper
0x00061745,crt_stricmp_61745,Case-insensitive compare (ASCII fold)
0x00062bc8,crt_strnicmp_62bc8,Length-limited case-insensitive compare
0x00063520,crt_strcmp_63520,Dword-at-a-time strcmp
0x00065bcd,crt_strncmp_65bcd,strncmp
0x0006ce84,crt_strcpy_6ce84,strcpy
0x0006cea9,crt_strcat_6cea9,strcat
0x000717e6,crt_strlen_717e6,repne scasb strlen
0x0007152e,crt_stpcpy_7152e,Copy returning end pointer
0x00071daf,crt_strchr_71daf,strchr
0x00071dcd,crt_memcpy_71dcd,memcpy (dword then byte)
0x00065e15,crt_memmove_65e15,Overlap-aware copy
0x00066107,crt_memset32_66107,Aligned dword fill (unrolled)
0x000660d0,crt_memset_660d0,memset core used by mem_clear_5afd0
0x0006b4be,crt_memswap_6b4be,Swap two buffers (qsort)
0x0006b4e4,crt_qsort_med3_6b4e4,Median of three compare
0x0006b544,crt_qsort_6b544,qsort
0x00061786,crt_atoi_61786,atoi with ctype table 0x937b4
0x000707bf,crt_utoa_707bf,utoa (radix table "0123456789abcdef...")
0x00070811,crt_itoa_70811,itoa
0x00070895,crt_ultoa_70895,ultoa
0x000708e5,crt_ltoa_708e5,ltoa
0x0006bc0a,crt_tolower_6bc0a,tolower
0x00070942,crt_toupper_70942,toupper
0x0006ab9a,crt_strupr_6ab9a,strupr
0x00062fcf,crt_tmpnam_digit_62fcf,Digit->char for tmpnam
0x00062fdf,crt_tmpnam_62fdf,Builds "t_XXXX.tmp"
0x0006bee2,crt_tmpnam_seed_6bee2,Returns DAT_000a468c
0x00071854,crt_splitpath_71854,_splitpath
0x00071911,crt_makepath_71911,_makepath
0x000718f3,crt_makepath_sep_718f3,Path separator normalisation
0x0006b178,crt_malloc_6b178,malloc
0x0006b186,crt_nmalloc_6b186,Heap search over DAT_000a4670 list (min 12 bytes)
0x0006b8f2,crt_free_6b8f2,free
0x0006b900,crt_nfree_6b900,Find heap block containing ptr and free
0x0006b274,crt_realloc_6b274,realloc
0x0006b434,crt_nrealloc_6b434,realloc core with heap growth
0x0006b289,crt_mem_expand_6b289,In-place block expansion
0x00070997,crt_heap_alloc_block_70997,Allocate from a heap segment's free list
0x00070a4c,crt_heap_free_block_70a4c,Free with coalescing
0x00070b57,crt_heap_unlink_70b57,Unlink heap segment
0x00070b98,crt_heap_shrink_70b98,Release empty segment (int 21h 49h or DPMI 0x502)
0x00070d10,crt_heap_link_70d10,Insert heap segment in address order
0x00070d84,crt_heap_end_check_70d84,Heap end vs segment limit
0x00070dcf,crt_heap_grow_dpmi_70dcf,Grow via DPMI 0x501 or int 21h 48h
0x00070ea1,crt_heap_grow_70ea1,sbrk-style growth
0x00070f70,crt_heap_expand_70f70,Expand heap
0x000710cb,crt_heap_round_size_710cb,Rounds growth size (amblksiz DAT_000ac134)
0x00071144,crt_set_amblksiz_71144,amblksiz = 0x8000
0x00071b4f,crt_sbrk_71b4f,int 21h 4Ah resize
0x00070954,crt_stackavail_70954,ESP - stack low
0x00070966,crt_stack_check_70966,__CHK stack probe
0x00070979,crt_alloca_check_70979,alloca validation
0x00066296,crt_open_core_66296,__open: int 21h 3Dh / 3Ch create / 44h ioctl / 3Eh
0x00066274,crt_open_66274,open
0x000619a0,crt_sopen_619a0,sopen (special mode 0x222)
0x000664ec,crt_read_664ec,read with text-mode CR/^Z handling (int 21h 3Fh)
0x00061a40,crt_read_wrap_61a40,read
0x0006af03,crt_write_6af03,write with LF->CRLF buffering (int 21h 40h append 42h)
0x00061e20,crt_write_wrap_61e20,write
0x0007124b,crt_qwrite_7124b,Raw write
0x00071343,crt_qread_71343,Raw read
0x0006aeb8,crt_lseek_6aeb8,int 21h 42h
0x00061a80,crt_lseek_wrap_61a80,lseek
0x0007114f,crt_tell_7114f,tell
0x0006bfe0,crt_close_6bfe0,int 21h 3Eh
0x000664de,crt_close_wrap_664de,close
0x00061a10,crt_close_wrap2_61a10,close
0x000712c4,crt_unlink_712c4,int 21h 41h
0x0006c015,crt_remove_6c015,remove
0x00061ea6,crt_mkdir_61ea6,int 21h 39h
0x00061e68,crt_access_61e68,int 21h 4300h get attributes
0x0006de66,dos_ioctl_isdevice_6de66,int 21h 4400h bit 7
0x0006de82,crt_get_iomode_6de82,Handle flags table 0xa46e4
0x0006ded8,crt_set_iomode_6ded8,Set handle flags
0x0006adec,crt_setmode_6adec,setmode text/binary
0x00071498,dos_findfirst_71498,int 21h 1Ah set DTA + 4Eh
0x000714e8,dos_dta_copy_714e8,Copies DTA under extender type 9
0x0006d94b,crt_file_exists_6d94b,findfirst == 0
0x00071c12,crt_dos_result_71c12,CF -> errno
0x00062cc0,crt_fopen_mode_62cc0,Parse r/w/a + b t
0x00062d76,crt_fopen_core_62d76,__doopen
0x00062e79,crt_fopen_62e79,fopen
0x00062ea8,crt_fopen_wrap_62ea8,fopen wrapper
0x0006bc1c,crt_allocfp_6bc1c,FILE allocation from table 0xa283c (0x1a bytes each)
0x0006bcbf,crt_freefp_6bcbf,Return FILE to free list
0x0006bcf6,crt_freefp_all_6bcf6,Free FILE list at exit
0x00062f7c,crt_fclose_byfd_62f7c,Find FILE by fd and close
0x00062fa9,crt_fclose_62fa9,fclose
0x00063058,crt_doclose_63058,flush + close
0x0006bee8,crt_fflush_6bee8,fflush
0x0006bfac,crt_ftell_6bfac,ftell
0x0006bd5b,crt_fseek_6bd5b,fseek
0x0006bd14,crt_fseek_inbuf_6bd14,Seek within buffer
0x0006beae,crt_chktty_6beae,Mark stream as tty
0x0006dde6,crt_ioalloc_6dde6,Allocate stream buffer (0x1000 or 1)
0x0006c023,crt_fgetc_6c023,fgetc
0x0006c0ae,crt_filbuf_6c0ae,__filbuf
0x0006c0dd,crt_fill_buffer_6c0dd,Fill buffer / flush output streams
0x0006c198,crt_ungetc_6c198,ungetc
0x0006dd18,crt_flsbuf_6dd18,__flsbuf (text-mode CR insertion)
0x00065fdf,crt_fgets_65fdf,gets/fgets on stdin (FILE 0xa283c) used for "Press return to continue"
0x000711f0,crt_flushall_711f0,flushall
0x000711d6,crt_full_io_exit_711d6,Flush and free streams at exit
0x000712f1,crt_flush_streams_712f1,Flush streams matching flag
0x000660aa,crt_set_errno_660aa,errno = arg
0x000660b6,crt_set_doserrno_660b6,_doserrno = arg
0x0006b108,crt_set_errno_dos_6b108,Map DOS error return -1
0x0006609a,crt_errno_fail_6609a,Set errno and return -1
0x0006dd0c,crt_errno_ptr_6dd0c,&errno (0x131a58)
0x0006dd12,crt_doserrno_ptr_6dd12,&_doserrno (0x131a54)
0x0006b0d1,crt_dos_retcode_6b0d1,CF check helper
0x0006b0eb,crt_dos_retcode2_6b0eb,CF check helper
0x00062542,crt_startup_62542,DOS/4GW startup (int 21h/31h DPMI 0x101) before __CMain
0x0006b95e,crt_cmain_6b95e,__CMain: calls main_3d0a0 then exit
0x0006b9b8,crt_init_rtns_6b9b8,Run init table 0xac13c..0xac154 by priority
0x0006ba03,crt_fini_rtns_6ba03,Run fini table
0x0006ba53,crt_setup_argv_6ba53,Copies command tail DAT_000a4684 and splits into argv
0x0006bb17,crt_parse_cmdline_6bb17,Quote-aware argument splitter
0x0006279f,crt_exit_6279f,exit()
0x000627b9,crt__exit_627b9,_exit: fini rtns + int 21h 4Ch
0x0006275c,crt_fatal_error_6275c,Writes message (int 21h 40h) and terminates
0x0006279e,crt_null_stub_6279e,Empty hook
0x00062794,crt_null_stub2_62794,Empty hook
0x0006a093,crt_init_fpe_table_6a093?,Fills 5 far pointers from FS-relative table
0x0006d999,crt_spawn_6d999,spawn core (.bat/.com/.exe COMSPEC)
0x000719fe,crt_spawnvp_719fe?,spawn wrapper
0x00071a1f,crt_system_71a1f?,spawn via PATH search
0x00071c25,crt_spawn_path_search_71c25,Searches PATH
0x000658d5,crt_system_658d5,system() via COMMAND.COM /c
0x0006d91e,crt_switchar_6d91e,int 21h 3700h switch char
0x0006d96a,crt_exec_6d96a,exec wrapper
0x00071713,dos_exec_71713,int 21h 4Bh EXEC with parameter block 0xac100
0x00071547,crt_build_envblock_71547,Environment block for spawn
0x000716a6,crt_build_cmdtail_716a6,Command tail builder
0x000717ff,crt_copy_env_717ff?,Copy env strings (max 0x92)
0x000631ca,crt_getenv_631ca?,Decompiles to return 0 (getenv stub?)
0x00062c57,dos_get_int_vector_62c57,int 21h 35h / 2502h (extender dependent)
0x00062c8b,dos_set_int_vector_62c8b,int 21h 25h / 2504h
0x00062c4d,dos_outp_62c4d,outp(port val)
0x00062c2c,crt_int_lock_stub_62c2c?,xchg-based field update
0x00065888,dos_get_segregs_65888,Fills SREGS struct
0x000658b2,dos_int386x_658b2,int386x
0x0006d540,dos_int386x_core_6d540,Register marshalling for int386x
0x0006d578,dos_int_call_stub_6d578,Interrupt call stub
0x00061f30,dos_int386_61f30,int386
0x00066f24,dpmi_alloc_locked_dos_mem_66f24,DPMI 0x100 then 6 (get base) and 0x600 (lock)
0x00066e29,dpmi_lock_region_66e29,DPMI 0x600
0x00066e56,dpmi_unlock_region_66e56,DPMI 0x601 (paired with lock in HMI lock/unlock lists)
0x00066eb3,dpmi_get_segment_base_66eb3,DPMI 6
0x00066ede,dpmi_alloc_selector_66ede,DPMI 0 / 7 / 8
0x0007b75f,dos_alloc_realmode_mem_7b75f,DPMI 0x100 or Phar Lap 25C0h
0x0007b7fa,dos_free_realmode_mem_7b7fa,DPMI 0x101 or 25C1h
0x0007b828,dos_realmode_int_7b828,DPMI 0x300 simulate real-mode int or Phar Lap 2511h
0x00067269,dos_vds_lock_region_67269,int 2Fh 1600h Windows check + int 4Bh 810Bh
0x000672a0,dos_vds_unlock_region_672a0,int 4Bh 810Ch
0x00066680,pit_set_rate_66680,out 43h 36h + divisor to 40h (IRQ0 masked meanwhile)
0x000666c6,timer_install_irq0_666c6,Saves int 8 (3508h) installs LAB_00066609 (2508h) and sets PIT divisor
0x000667a9,timer_restore_irq0_667a9,Restores int 8 and PIT 0
0x00066751,pic_mask_irq0_66751,in/out 21h |1
0x0006677d,pic_unmask_irq0_6677d,in/out 21h &~1
0x0005cf9b,snd_timer_init_5cf9b,HMI timer system init (sosTIMERInitSystem)
0x0005d093,snd_timer_register_event_5d093,sosTIMERRegisterEvent (0x1234dc/rate) 16 slots at 0xa3c59
0x0005d060,snd_timer_remove_event_5d060,sosTIMERRemoveEvent
0x00069f9c,snd_opl_write_69f9c,Adlib register write with port delays (port DAT_000a3fa8)
0x0006df9d,snd_gus_poke_b_6df9d,GUS register byte write (base DAT_000a4ec2)
0x0006e001,snd_gus_peek_b_6e001,GUS register byte read
0x0006e069,snd_gus_poke_w_6e069,GUS register word write
0x0006e0e2,snd_gus_peek_w_6e0e2,GUS register word read
0x0006f986,snd_gus_detect_6f986,Probe GUS at base and base+0x400
0x0006f9cc,snd_gus_probe_6f9cc,Register-pattern probe
0x0006fa55,snd_gus_delay_6fa55,Timer-based delay
0x0006fa8a,snd_gus_reset_voices_6fa8a,Reset 32 voices
0x0006fb90,snd_gus_init_voices_6fb90,Voice table init
0x0006fc38,snd_gus_init_regs_6fc38,Register init
0x0007000e,snd_gus_init_dram_7000e,DRAM/DMA init (port+0x802 0x3e)
0x00070154,snd_gus_upload_patches_70154,Patch upload loop
0x000703e2,snd_gus_init_703e2,Top-level GUS init
0x00070480,snd_gus_shutdown_70480,Silence voices
0x0006e712,snd_gus_note_on_6e712,Note on with patch layers
0x0006ecf5,snd_gus_note_off_6ecf5,Note off
0x0006ee0d,snd_gus_note_event_6ee0d,Dispatch note on/off
0x0006f395,snd_gus_controller_6f395,MIDI controller dispatch
0x0006f586,snd_gus_program_change_6f586,Program change
0x0006f72e,snd_gus_pitch_bend_6f72e,Pitch bend
0x0006e6b7,snd_cents_to_freq_6e6b7,Cents (1200/oct) to frequency code
0x0006e689,snd_freq_to_note_6e689,Lookup in 128-entry table 0xa47b8
0x0006def4,snd_gus_channels_init_6def4,30 channel records init
0x0006e164,snd_gus_find_voice_6e164,Voice allocation
0x0006e351,snd_gus_patch_lookup_6e351,Patch map lookup
0x000704cd,snd_opl_set_voice_704cd?,OPL voice programming
0x00070697,snd_opl_alloc_voice_70697?,OPL voice allocation
0x000632c9,snd_hmi_error_string_632c9,Error string table 0xa27c8
0x0006cfaa,snd_hmi_load_digital_driver_6cfaa,Loads hmidrv.386 digital driver for device 0xE000-0xE200
0x0006d210,snd_hmi_load_midi_driver_6d210,Loads hmidrv.386 MIDI driver for device 0x1000-0x1023
0x0006d1a6,snd_hmi_unload_driver_6d1a6,Frees driver slot
0x0005f04d,snd_hmi_lock_code_5f04d,Locks interrupt-time code/data regions (dpmi_lock_region list)
0x0005f47f,snd_hmi_unlock_code_5f47f,Unlocks the same list
0x0005fa77,snd_midi_init_driver_5fa77,sosMIDIInitDriver
0x0005ff24,snd_midi_uninit_driver_5ff24,sosMIDIUnInitDriver
0x0005f8b1,snd_midi_init_system_5f8b1,sosMIDIInitSystem
0x0005fa4d,snd_midi_uninit_system_5fa4d,sosMIDIUnInitSystem
0x0005e53a,snd_midi_init_song_5e53a,sosMIDIInitSong ("HMIMIDIP" header)
0x0005eb38,snd_midi_stop_song_5eb38,sosMIDIStopSong
0x0005eab0,snd_midi_start_song_5eab0,sosMIDIStartSong (registers timer event)
0x0005ec41,snd_midi_song_setup_5ec41,Track pointers setup
0x0005e0d9,snd_midi_reset_channels_5e0d9,All notes off / reset controllers per track
0x0005eedf,snd_midi_read_varlen_5eedf,MIDI variable-length read
0x0005ef56,snd_midi_set_volume_5ef56,Controller 7 scaled volume
0x00060035,snd_midi_all_notes_off_60035,Controllers 0x79/0x7b on 16 channels
0x0005ea6d,snd_midi_clear_song_slot_5ea6d,Clear song slot
0x0005e4ee,snd_midi_set_flag_5e4ee,Swap global DAT_000a2481
0x0006330e,snd_midi_driver_call_6330e,Call driver entry n*0x24
0x000635dd,snd_digi_detect_init_635dd,sosDIGIDetectInit (hmidet.386)
0x0006379a,snd_digi_detect_find_6379a,sosDIGIDetectFindHardware
0x00063d88,snd_digi_detect_settings_63d88,sosDIGIDetectGetSettings
0x00063746,snd_digi_detect_uninit_63746,sosDIGIDetectUnInit
0x00064300,snd_digi_init_system_64300,sosDIGIInitSystem
0x0006435c,snd_digi_uninit_system_6435c,sosDIGIUnInitSystem
0x00064386,snd_digi_init_driver_64386,sosDIGIInitDriver
0x00064ab8,snd_digi_uninit_driver_64ab8,sosDIGIUnInitDriver
0x00064e7c,snd_digi_start_sample_64e7c,sosDIGIStartSample (32 channels 0xc0-byte driver tables)
0x00065485,snd_digi_stop_sample_65485,sosDIGIStopSample
0x0006291e,snd_digi_sample_done_6291e,sosDIGISampleDone
0x00064d38,snd_digi_set_sample_volume_64d38,sosDIGISetSampleVolume
0x00064dc4,snd_digi_set_master_volume_64dc4,Scale all channel volumes
0x00065c70,sound_play_sample_65c70,Game SFX play: find free of 32 channel slots DAT_0012e070 (id bank) priority 0x7fff
0x000627d0,sound_set_sample_volume_627d0,Volume of a playing sample by id
0x000628a4,sound_count_playing_628a4,Active channel count
0x0005c040,sound_stop_all_samples_5c040,Stop all 32 channels
0x0005c990,sound_load_bank0_5c990,Loads data/snds0-0.dat/.tab
0x0005c870,music_load_bank0_5c870,Loads data/music0-0.dat/.tab
0x0005c0a0,music_play_track_5c0a0,Stops current song and starts track n from bank (0x20-byte entries)
0x0001f960,music_stop_1f960,Stop current song (DAT_0009e312)
0x00065b20,music_update_65b20,Per-frame: restart song when finished
0x0005cf40,music_song_done_5cf40,Song slot inactive check
0x0004da10,sound_digital_init_4da10,Maps sndsetup card name (SBPRO SB16 ADLIBG ... GRAVIS) to HMI device id and rate (22050/44100/11025); detect/init driver; "Error : %s"
0x000617e0,sound_shutdown_617e0,Stop samples uninit digital driver/system free bank
0x00061870,music_shutdown_61870,Stop song uninit MIDI free bank
0x0004ee70,net_init_4ee70,Allocates low-memory network buffers (0x42 3x0x800 8x(0x800+0x42)); DAT_0009e3c8=1
0x0004f030,net_join_session_4f030,Session name "TESTER"/cfg; finds slot (returns local player id DAT_0009e3ca) handshakes with up to 8 players
0x0004f530,net_sync_players_4f530,Send/receive to all other players
0x0004efc0,net_shutdown_4efc0,Free network buffers
0x0005a010,joystick_digital_init_5a010,Detect joystick; DAT_0009e583=2
0x0005a060,joystick_analog_calibrate_5a060,Averages 16 reads of centre/extremes into DAT_0012ed46..54 thresholds
0x0004fdc0,vfx1_init_4fdc0,Forte VFX1 headset init (port DAT_0012e624 "VFX" env); sets DAT_0012e628
0x00050180,vfx_glasses_port_50180?,Port writes selecting left/right/both (3D glasses)
0x000503d0,stereo_page_flip_503d0?,Ports 302h/303h page flip and blit
0x00034060,input_snapshot_34060,Snapshot mouse pos + last key into DAT_000b6e84..88
0x0003ed30,text_table_index_3ed30,Builds pointer array over packed NUL-separated strings (text.dat)
0x0005ac80,cpu_detect_5ac80?,Calls 5ace8 and sets cfg+8 = (DAT_0009e59c == 5) (Pentium flag used for detail defaults)
0x0007984e,con_print_string_7984e,Text-mode console print (Watcom graph lib style int 10h cursor)
0x0007968a,con_write_chars_7968a,Writes chars with wrap/scroll
0x000795b4,con_get_cursor_795b4,Cursor position
0x000795e6,con_set_cursor_795e6,Set cursor (int 10h 02h)
0x000799fc,con_init_799fc,Video state init from BIOS data
0x00079cb2,con_set_video_mode_79cb2,int 10h AH=0
0x0007a449,vga_detect_adapter_7a449,int 10h 1A00h display code
0x0007a52f,ega_detect_7a52f,int 10h 1200h BL=10h
0x0007a611,vga_detect_mono_7a611,Port 3BAh vertical sync probe
0x0007ad8a,svga_detect_chipset_7ad8a,"TRIDENT"/"OAK"/"VGA=" env chipset detection
0x00071e00,terrain_sample_height_71e00,Bilinear sample of 256x256 byte map at 0xddfb0 (x y 8.8 fixed)
0x00071f08,terrain_fractal_init_71f08,Diamond-square seed loop over 0x10dfb0 (16-bit heights)
0x00071f92,terrain_fractal_square_step_71f92,Square step with LCG 0x24a1*x+0x24df
0x00072027,terrain_fractal_diamond_step_72027,Diamond step
0x00072147,palette_build_lookup_72147?,Averages 32x32 texture blocks into RGB (palette tables)
0x000722e3,render_sort_triangle_722e3?,Sorts 3 vertices by y then rasterises
```

## 2. ENGINE.md section: front end, CRT and DOS glue

### Command line (config_parse_33750)

Options are matched with `crt_stricmp_61745` against the strings at 0x90664..0x90707 (each option
`-x`/`/x`, optional value in the next argv). Effects (cfg = `*DAT_000adf74`, state = `*DAT_000adf6c`):

| option | effect |
|---|---|
| `digijoy` | `joystick_digital_init_5a010`; if present DAT_0009e583 \|= 2 |
| `anojoy`, `anojoy4` | `joystick_analog_calibrate_5a060`; DAT_0009e583 \|= 1 (0x20 for 4-button variant) |
| `vfx` | `vfx1_init_4fdc0`; state+0x219e = 1, DAT_0009e583 \|= 8, prints "VFX\n" |
| `help` | abort (returns -1) |
| `debug` | DAT_000adfca -> cfg.flags \|= 0x80 |
| `network` | `net_init_4ee70`; if players>1 `net_join_session_4f030` -> state+8 (local player), cfg.flags \|= 0x10; cfg \|= 0x100 |
| `custom` | cfg \|= 0x100 |
| `setsound` | sets a local that is not used afterwards (dead) |
| `demo n` | cfg byte+1 \|= 1 and bit 2<<(n-1) for n=1..5 (0x200..0x2000) |
| `detail n` | n==0 -> state+0x2195/96/97 = 0, +0x2198 = 0x28 |
| `cheat n` | player[local]+0x14 (state + idx*0x801 + 0x3423) = n |
| `name s` | copied to a local (player name, not stored here) |
| `level n` | cfg+0x11 = n |
| `movie n` | cfg+0xd = n |
| `roll n` | cfg+0xd = 0, cfg+0xf = n, cfg.flags \|= 0x120 |
| `time` | cfg.flags \|= 0x40 |
| `password n` | cfg+0x19 = n (int) |
| `players n` | state+0xa = n (default 2) |
| `session s` | cfg+0x75 = s (default "CARPET") |

After parsing: `DAT_0012ef00 = mem_alloc_low(0x100)` (VESA info buffer), `state = mem_alloc(0x38d03)`,
`cfg = mem_alloc(0x8e7e)`, eleven option bytes state+0x21ad..0x21b7 = 1, `cpu_detect_5ac80` -> cfg+8,
state+0x2195/0x2197 = cfg+8 (Pentium), +0x2196/+0x2199/+0x219a = 1, +0x2198 = 0x28, cfg+0x1d = 0,
`DAT_0012edae = 1` (video mode flags: bit0 = 320x200).

### Front-end state machine

`frontend_menu_loop_52070` runs once per frame. `DAT_0012ed2e` selects the screen through the jump
table at 0x52038:

| state | function | screen |
|---|---|---|
| 0 | fe_screen_intro_movie_54900 | intro\intro.dat |
| 1 | fe_screen_config_521c0 | gconfig.dat: sound setup wizard + input device |
| 2 | fe_screen_main_menu_532b0 | mainmenu.dat/.pal, mmspr.dat, mmmask.dat |
| 3 | (none) | |
| 4 | fe_screen_multiplayer_54bd0 | pmulti.dat, pmultspr.dat |
| 5 | fe_screen_level_result_55b00 | pperf.dat; levelw1/levelw2/levelose.dat FLIs |
| 6 | fe_screen_language_56940 | language.dat, langspr.dat |
| 7 | fe_screen_intel_logo_56510 | intro\intel.dat |
| 8 | fe_screen_title_56730 | intro\title-01/02.dat |
| 9 | fe_screen_bullfrog_logo_563c0 | intro\logo.dat |
| 10 | fe_screen_outro_movie_54ab0 | intro\outro.dat |

Flow: `fe_init_state_51ed0` sets state 6 (language) -> 1 (config; the sound wizard is skipped when
sndsetup.inf exists, DAT_0012ed29 = 7) -> 7/9/8/0 (logos, title, intro) -> 2 (main menu). In the main
menu, after 0x12c0 ticks without input (`fe_input_idle_check_53ad0`) the attract counter
`DAT_0012ed34` cycles: 0 -> state 0 (intro movie), 1 -> state 8 (title), 2 -> demo level (sets
`DAT_0009e504`, cfg.flags |= 0x24, cfg+0xa1 = 3, cfg+0xa2 = 200). A level start sets state 5 so the
result screen is shown on return; result screen returns to state 2, or 10 (outro) after level 50.

Main menu items (hit mask byte `DAT_0012ed26`; table 0x5167c of 6-byte `{handler,kind}`):
1 new game / resume (57480), 2 handler 0x57580 (redraw, unanalysed), 3 multiplayer (573fc), 4 quit
(57270), 5..10 kind 1..6: in top-level mode kind 1 = load, 2 = save, 3 = start game; in slot mode
(`DAT_0012ed2d` 1/2) they are the six save slots -> `fe_savegame_load_59030` / `fe_save_slot_dialog_541f0`
+ `fe_savegame_save_591d0`, 11 start level (5744c, only when no game in progress).

Save game `save\carpet%02X.gam` (slot 0..5): u32 version 4; char name[0x14]; cfg+0x1d[0x20];
cfg+0x3d[0x20]; state+0x2195[0xc] (detail options); u32 checksum = (level + DAT_0012ed30 +
DAT_0012ed31)*4; state+0x3bd6[0x18] (campaign progress); u8 DAT_0012ed30; u8 DAT_0012ed31;
state+0x2195[0xc] again. Slot names default "Game One".."Game Six" (0x9e469.., pointers 0x9e4e8).

Level names: pointer table 0x97490, index = level number (entry 0 empty, 1..50 "1. Al Jahan" ..
"50. Volcania", 51.. multiplayer maps "Bussorah" "Bisnagar" "Tartary" "Akkania" "Ryahn" "Zhullor"
"Dombren"). Referenced only from fe_screen_level_result_55b00 (0x55fb3). The multiplayer lobby offers
levels 50 + index. 0x943cc (the class/model table "parent" pointer) points at the string "Text Omitted".

Text rendering: font struct (DAT_0009e508 sfont0, 0x9e510 sfont1, 0x9e518 sfont2) = {u32 glyph table
(6-byte {u32 offset,u8 w,u8 h} entries, ASCII-0x20), u16 flags (bit0/1 enable colour remap, 0x10 use
sprite blitter, 0xc000 draw mode), u8 colour1, u8 colour2}. `ui_draw_text_58ab0(x, y, font, string)`
handles control bytes 1/2 (set colours from next byte), 3..6 flag toggles, 10 newline (advance by
glyph 5 height). Glyph pixels are span encoded: n>0 copy n bytes (bytes 1/2 remapped to the font
colours), n<0 skip, 0 end of row; identical to the sprite format in FORMATS.md.

Screen helpers: back buffer `DAT_0012ed74` (pitch `DAT_0012ed70`, height `DAT_0012ed78`),
background copy `DAT_000adf68` (swapped with the back buffer around overlay drawing). Every screen
has two code paths on `DAT_0012edae & 1`: 320x200 (`vga_blit_backbuffer_320_610f0`, 320-wide
fill/copy) vs 640x480 VESA (`vesa_blit_backbuffer_640_4f974`, sprite tables relocated with doubled
sizes). Mouse coordinates `DAT_0009e5dc/de` are in 640x400 space and halved for hit tests.
Palette fades go through `vga_palette_fade_61510` (the function previously named timer_sync_61510:
it reads the DAC, steps towards target or black with vsync waits; 0x10 steps on fade-out, 0x20 on
fade-in). Mouse cursor is a 64x64 buffer `DAT_0012edf8` rendered by `ui_set_mouse_cursor_5ba5c` from
sptrs.dat entry `DAT_0012ed2e*6` (one pointer sprite per screen state).

FLI player: `fli_play_508f0` -> `fli_next_frame_50dfd` (chunk magic 0xAF12 file header, 0xF1FA frame)
-> `fli_decode_frame_50e88` (sub-chunk types 4 COLOR256 skipped, 7 SS2, 15 BRUN) ->
`fli_present_frame_50600` (callback `DAT_0012ea9c`, pacing by tick counter `DAT_0012eab4`).
`fli_event_script_17d80` syncs sound/music events (7-byte records) to frames. Strings "COLOUR256 ",
"SS2 ", "BRUN ", "COPY ", "PSTAMP " (0x92e9c..) are debug names of chunk types.

### Memory manager (game side, not CRT)

`mem_pool_init_61f80` grabs DOS/DPMI memory into a region table 0x12f520 (12-byte: {addr, size,
real-mode flag}) and a block list at 0x130120 (256 nodes of 18 bytes: +0 ptr, +4 size, +8 next,
+0xc prev, +0x10 owner tag (0 = free), +0x11 region type). `mem_alloc_59870(size)` is best-fit and
zero-fills; `mem_alloc_low_598f0` only takes blocks with a real-mode address (network buffers, VESA
info). `mem_update_stats_622a8` keeps DAT_0013132c = largest free block, which `mem_init_pools_59500`
uses to pick the memory class DAT_0009e328 (0..3), texture block size state+0x21a0 (0x10 ->
data/block16.dat, 0x20 -> block32.dat) and to disable music/digital sound below 1 MB.
`file_load_resource_list_5ae80` loads NUL-terminated lists of 0x2c-byte records (these are the
`data\screens\*.dat` lists at 0x510d0.. in the code segment: name[0x1c], ptr to dest pointer, ptr to
end pointer, size, flags) via `file_load_rnc_3cbe0`; `file_free_resource_list_610c0` frees them.

### Watcom CRT / DOS glue (0x5e000-0x7b9ee)

Identified by behaviour (see CSV): printf family (`crt_prtf_core_6a0e6` with '%' loop, spec parser
6a395, converter 6a6ef; `crt_sprintf_603bc` = __prtf + NUL; `crt_printf_5b02a` = va wrapper onto
stdout FILE 0xa2856 via `crt_vfprintf_core_66193`), scanf family (`crt_scnf_core_6c23a`), heap
(`crt_nmalloc_6b186` over segment list DAT_000a4670, growth via DPMI 0x501 / int 21h 48h), low-level
I/O (int 21h 3Dh/3Ch open in 66296, 3Fh read 664ec with text-mode CR stripping, 40h write 6af03 with
LF->CRLF, 42h lseek 6aeb8, 3Eh close 6bfe0, 41h unlink 712c4, 39h mkdir 61ea6, 4300h access 61e68,
4400h ioctl 6de66, 1Ah/4Eh findfirst 71498, 4Bh exec 71713, 4Ch exit 627b9), stdio (FILE table
0xa283c, 0x1a bytes each; stdin = 0xa283c), errno at 0x131a58 / _doserrno 0x131a54, startup
`crt_startup_62542` -> `crt_cmain_6b95e` -> `main_3d0a0`. Extender type byte `DAT_000a46ae`
(1 = DPMI/DOS4GW, 2..8 = Phar Lap style, 9 = other) selects between int 31h and int 21h 25xxh
variants in the vector/memory helpers. The PIT/PIC helpers (`pit_set_rate_66680`,
`timer_install_irq0_666c6` saving int 8 to DAT_000a2a54) and the DPMI lock lists (`snd_hmi_lock_code_5f04d`)
belong to the HMI Sound Operating System, as do the sos* functions and the Gravis UltraSound driver
(GUS register access through port base DAT_000a4ec2 + 0x802). The 0x79000+ block is Watcom's text
console / adapter detection (int 10h, "TRIDENT", "OAK", "VGA=").

Game code that Watcom placed late in the image: `terrain_fractal_*_71f08/71f92/72027`
(diamond-square over a 256x256 16-bit map at 0x10dfb0 with LCG 0x24a1*x+0x24df),
`terrain_sample_height_71e00` (bilinear over the byte map at 0xddfb0), `render_sort_triangle_722e3`.

### Globals recovered

- DAT_0012ed2e FE state; DAT_0012ed2d load(1)/save(2) slot mode; DAT_0012ed26 hovered menu item;
  DAT_0012ed29 sound-wizard step (7 = done); DAT_0012ed2a/2b selected/hovered option; DAT_0012ed33
  input-device page; DAT_0012ed35 FE flags (bit0 screen initialised, bit1 sndsetup missing, bit3
  start game, bit4 lobby wait, bit5/6 text entry); DAT_0012ed34 attract phase; DAT_0012ed36 attract flags;
  DAT_0012ed14 idle start tick; DAT_0012eab4 FE tick counter; DAT_0012ed10 frame counter;
  DAT_0012ed30 "CARPET%d" counter, DAT_0012ed31 lobby player count; DAT_0012ed1e lobby level scroll.
- Input: DAT_0012ed2f mode; DAT_0012ec3c/3e cursor (2x), DAT_0012ec40/42 click pos; DAT_0012ec44..48
  edge flags; DAT_0009e5dc/de mouse pos; DAT_0012ee0a/0c/0e, ee12/ee14 buttons; DAT_0012eea0 last
  scancode; DAT_0012ee21 Enter, DAT_0012ee3c Escape, DAT_0012ee68/6b/6d/70 cursor keys;
  DAT_0012ee20[128] key table; DAT_0009e583 device flags (1 analog joy, 2 digital, 8 VFX1, 0x20 4-button);
  DAT_0012ed40 joystick present; DAT_0012ed42/44 joystick axes; DAT_0009e5e4 mouse present.
- Video: DAT_0012edae mode flags (bit0 320x200; 2/8 other modes), DAT_0012edac saved BIOS mode,
  DAT_0012ef00 VESA info buffer, DAT_0012ef04 granularity, DAT_0009e860 fade active,
  DAT_0012ead0..e4 text clip rect, DAT_0012ed88/98/80/a8/a4/90 sprite clip window, DAT_0009e5d4 cursor lock.
- Game: DAT_0009e504 leave front end / start level; DAT_0009e500 game in progress (resume available);
  DAT_0009e3c8 network active, DAT_0009e3ca local player, DAT_0009e3cc player count, DAT_0009e3fc/3d0/3d4/3d8/400 net buffers;
  DAT_0009e520 lobby slots (8 x {joined,ready,anim}); DAT_0009e320/321 digital sound ok,
  DAT_0009e30c/30d music ok, DAT_0009e312 current track, DAT_0012e244 rate code, DAT_0009e328 memory class.
- cfg (DAT_000adf74) extra fields: +8 Pentium flag, +0xd movie, +0xf roll, +0x11 level, +0x13 saved
  level, +0x19 password, +0x1d/+0x3d two 0x20-byte strings saved in .gam, +0x75 session name,
  +0x97 language, +0xa1..0xa5 demo parameters, +0xa8 pool pointer, +0xac pool size.
- state (DAT_000adf6c) extra fields: +0xa player count, +0x2195..0x219a detail options, +0x219b
  requested video mode, +0x219e VFX enabled, +0x21a0 texture block size, +0x21ad..0x21b7 option bytes,
  +0x2409 player record backup, +0x3bd6 campaign progress (0x18 bytes, saved), +0x7414 per-player
  10-byte records (byte 0 = level).

### Corrections to existing names

- `timer_sync_61510` is a palette fade (`vga_palette_fade_61510`); the real frame pacing in the FE is
  `fli_wait_frame_50430` / tick counter DAT_0012eab4.
- `FUN_000603bc` is `sprintf` (not a stack probe); the stack probe is `crt_stack_check_70966`.
- cfg byte +0x97 is the language, not the sound card type (sound card names live in sndsetup.inf
  strings DAT_0012eb00..).

### Open questions

- 0x57580 (menu item 2 handler) and 0x579c0 (FLI frame callback used by the scroll dialogs) are not
  functions in Ghidra; they need to be created manually.
- `video_mode_extra_5be68` int 10h AX=7/8 calls for DAT_0012edae == 8/2 and the stereo blits
  (`gfx_blit_stereo_*`) suggest a 3D-glasses/VFX1 page-flipped mode; not verified at runtime.
- `cpu_detect_5ac80` -> what 5ace8 reads (CPUID/flags test) was not checked.
- The dead `setsound` option and the never-set `local_60` (mode 8) path in config_parse indicate
  stripped debug features.
