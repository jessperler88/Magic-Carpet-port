# agent_tick: per-tick non-render / non-thing subsystems of carpet.exe

Scope: everything FUN_00032e80 (game tick) calls other than the renderer and the thing update, plus
input (keyboard/mouse/joystick), timer, sound/music (HMI), network (NetBIOS) and demo ("movie")
record/playback. All names below come from reading the decompiled bodies in carpet_all.c.

## 1. Function names (CSV)

```csv
0x00032e80,game_tick_update_32e80,Simulation+present step called by game_tick_32f90: palette fx -> texture anims -> local input -> cmd processing (3a8b0) -> win check -> things x(1|4|16 by cfg+0x96) -> sound update -> 1fab0 -> debug overlay -> screenshot -> present (2f480)
0x00033010,palette_effect_update_33010,State machine on cfg+0x17 (0/1: fade to black + load data/palette.dat via 32e40; 2: request fade-in; 3: run effect cfg+0x98: 1=fade-in 4 steps; 2=red tint; 3=red+blue; 4=blue; 5=darken -0x20; 6=brighten +0x30/+0x20/+0x20; 7=greyscale) into 0xB6B80 then vga_set_palette; skipped while state+0x219b!=0
0x00032e40,palette_fade_out_and_load_32e40,vga_palette_fade(NULL;16 steps) then clears state+0x245 and file_load_rnc("data/palette.dat")
0x00061510,vga_palette_fade_61510,MISNAMED as timer_sync: reads DAC (3C7/3C9) into 0x12EF10 and blends towards target palette (param_5; default 0x12F210 = black) over param_6 steps; param_7!=0 = incremental one step per call (DAT_0009e860 = in-progress); each step waits vsync (65b10) and writes DAC via 302f0
0x00061718,vga_palette_fade_reset_61718,Sets DAT_0009e860=0 (abort/finish incremental fade)
0x000302f0,vga_set_palette_302f0,out 3C6=FF then 256x(3C8 idx; 3C9 r g b); mirrors to alternate DAC via 50180 when DAT_0009e43c
0x00050180,vga_set_palette_altdac_50180?,Writes 768 bytes to ports DAT_0012e624/DAT_0012e61c (index 7;6;9;10) - secondary display device palette (VR headset?) when DAT_0009e43c
0x00065b10,vga_wait_vsync_65b10,Spin on port 3DA bit 3
0x00061ec8,vga_read_palette_61ec8,out 3C7=0; in 3C9 x768 into buffer
0x00061480,vga_set_mode_640x480_61480,int10 0F00 saves original mode in DAT_0012edac; width 640 height 480; vesa_set_mode(0x101); set palette; 5be68
0x000613e0,vga_set_mode_13h_613e0,int10 AX=0x13; width 320 height 200; set palette
0x00061208,vesa_set_mode_61208,int10 AX=4F02 BX=mode then 61130
0x0006126c,vesa_set_bank_6126c,int10 4F05 window A and B; bank<<6 / granularity (0x12EF02); DAT_0009e85c = current bank
0x0002f680,vesa_mode_supported_2f680,Walks the VESA info block mode list (DAT_0012ef00 +0xe/+0x10 far ptr) for the requested mode (0x10E/0x111 HiColor)
0x0004f974,vga_blit_banked_4f974,Copies DAT_0012ed74 screen buffer to A0000 in 64K banks (4 banks + 0x3A00 dwords for 400 lines; 5 + 0x2C00 for 480); hides/shows mouse cursor around it; the keyboard ISR starts right after it at 0x4FA28
0x00033600,video_toggle_resolution_33600,R key: fade out; free screen; DAT_0012edae 1<->8; mem_alloc_named "*WScreen" 64000 or 0x4B000 -> DAT_000adf70; set mode 13h or 640x480; mouse_init; cursor sprite; cfg+0x17=0 restarts the fade-in
0x0003ed60,video_input_init_3ed60,Once (DAT_00094380): set video mode (by DAT_0012edae); mouse_init (prints "ERROR : MOUSE DRIVER NOT FOUND."); cursor sprite; install keyboard ISR (4fb46) unless cfg&8
0x0003ee70,game_shutdown_3ee70,Restore keyboard vector (4fb85); 5afa0; 4efc0; timer_isr_remove
0x0004fb46,input_keyboard_install_4fb46,Clears 128-byte key table 0x12EE20; saves int 9 vector (62c57) in DAT_0012e390/394; installs LAB_0004fa28 via dos_setvect(9)
0x0004fb85,input_keyboard_restore_4fb85,dos_setvect(9; saved vector)
0x00062c57,dos_getvect_62c57,int21 AH=35h (or 2502h under DOS/4GW) get interrupt vector
0x00062c8b,dos_setvect_62c8b,int21 AH=25h (or 2504h) set protected-mode interrupt vector
0x0005bc14,input_mouse_init_5bc14,int33 fn0 reset; fn0C install event handler LAB_0005b86c mask 0x7F; allocs 3x0x1000 cursor buffers (DAT_0012edf8 sprite filled 0xFE=transparent; DAT_0012edc8/DAT_0012edb8 save-under); fn2 hide; fn0F mickeys if mode&8; DAT_0009e5e4 = mouse present
0x00061f30,dos_int86_61f30,Generic real-mode interrupt call through DPMI (65888/658b2); register block in param_1 (EAX first); int number passed in a register
0x0004a030,input_mouse_set_pos_4a030,Clamps x<=0x27E y<=0x1DE/0x18E; updates DAT_0009e5dc/de and calls int33 fn4 (x<<3;y<<3 in 640-wide modes)
0x0004a000,input_mouse_center_4a000,input_mouse_set_pos(320; 200 or 240)
0x0005ba5c,mouse_cursor_set_sprite_5ba5c,Draws sprite param_5 into the 64x64 cursor buffer (hotspot from sprite+4/+5 -> DAT_0012edf4/f6; halved in mode 1)
0x0005b35c,mouse_cursor_draw_5b35c,Saves background under cursor into both save buffers and blits cursor (0xFE transparent) into DAT_0012ed74
0x0005b740,mouse_cursor_restore_5b740,Copies saved background back into DAT_0012ed74
0x0005b7f8,mouse_cursor_show_5b7f8,If mouse present: DAT_0009e5d4=1 and cursor_draw
0x0005b850,mouse_cursor_hidden_flag_5b850,DAT_0009e5d4 = 0
0x0005b050,mouse_cursor_draw_vram_5b050,Same as 5b35c but writes to A0000 with VESA bank switching
0x0005b560,mouse_cursor_restore_vram_5b560,Restores background in A0000 with bank switching
0x0005a4e0,input_joystick_poll_5a4e0,Reads joystick (DAT_0009e583 type bits 1=analog 2=digital 0x20=4-button; DAT_0012ed40 mode) via 59dc7/59c53; maps axes to arrow-key flags 0x12EE68/70/6B/6D or (button 0x40) to mouse pos; buttons 0x10/0x20 -> left/right mouse events; param_5!=0 = cursor mode with clamp rect (param_6..9) and step (param_10/11); also alt device (DAT_0009e43c) via 50080
0x00059d0e,joy_detect_59d0e,out 201; waits axes to settle; DAT_0012ed40 = joystick A present; DAT_0012ed41 = B present
0x00059bd8,joy_calibrate_digital_59bd8,Times axes once; DAT_0012ed40=2; thresholds DAT_0012ed4a/46/4c/48
0x00059c53,joy_read_digital_59c53,Times port 201 axes; DAT_0012ed42/44 = -1/0/1 vs thresholds; DAT_0012ed6a = raw buttons
0x00059dc7,joy_read_analog_59dc7,Uses 59fad then scales (DAT_0012ed52/4e/54/50 >>11) to -128..128 into DAT_0012ed42/44; DAT_0012ed6a = buttons & F0
0x00059fad,joy_poll_axes_59fad,out 201 0x10 and waits until axis bits (3 or F) clear
0x0005a010,joy_init_digital_5a010,"digijoy" config: detect + digital calibrate
0x0005a060,joy_init_analog_5a060,"anojoy"/"anojoy4" config: detect + analog calibration (776 bytes)
0x00034090,input_changed_34090,Returns 1 once mouse x/y; buttons or last key differ from the snapshot (DAT_000b6e84/86/88); DAT_000adfa8 latch
0x00034060,input_snapshot_34060,Snapshot mouse pos + last key; clear latch (34051 is a duplicate fragment)
0x00058290,ui_text_edit_field_58290,Front-end text entry loop: scancode->ASCII via table 0x51E55 (allowed 0x51D9B; shift DAT_0012ed35&0x20); Backspace 0x0E; Enter 0x1C; Esc 1; Del 0x53; redraw via callback; clears key/mouse state on exit
0x00017a80,ui_button_table_dispatch_17a80,Walks a table of 0x18-byte hotspot records {x1 y1 ? x2 y2 ... +0xc mouse cond +0xd key +0xf/+0x11 keystate idx +0x13 count}; on hit calls player_queue_command
0x00016660,player_local_input_16660,Per-tick local input (skipped in demo playback): mode state+0x3855 per player (0 flight; 1; 2 spell-select; 3 text); keys [ ] adjust state+0x2198 (0x11..0x28); Esc; R=toggle res; Space; I; 1-0 spells; Enter; arrows; left/right click fire spells of thing+0xa0 book (+0x3ac/+0x3b0 selected; +0x214 slots); writes 10-byte command packet at state+0x7413+player*10
0x000156b0,player_function_keys_156b0,P(0x19)=pause (cfg+2 bit1; stops music/sfx); F1 sound toggle (DAT_0009e321); F2 music toggle (DAT_0009e30d); F3 cycle cfg+0x96 (0..2 = thing-update sub-steps 1/4/16); F4..F9 toggle state+0x219d/0x2195/0x2196/0x2199/0x219c (gated by state+0x21ad..0x21b7); F10 map/hicolor mode state+0x219b; each posts an on-screen message (player+0x3467=0x32 ticks)
0x00017270,player_queue_command_17270,Writes command byte (and arg bits) into local player's packet state+0x7413+p*10 (+0 cmd; +1/+5 args); cmd 0x1b/0x1c need status bit 2/4; cmd 0x1e requires player name "chronicle" at player+0x3815 unless cfg+1&0x80 (cheat gate)
0x00015590,player_mouse_steer_15590,Mouse x/y -> -127..127 steering into packet +3/+4 ((x-320)*128/320; (y-200)*128/200 or (y-240)*128/240) when cfg+0x96==0
0x0003db20,player_check_mana_win_3db20,Not in movie/network (cfg&0x110): for each player with castle (thing+0x32) if (castle mana + +0x134)*100/cfg+0x5e > state+0x38c93 for 16 ticks -> status |= 2 (level won)
0x0004be50,texture_anim_update_4be50,For 0x211 texture slots (DAT_000b8d3c ptr table): if flags&8 (animate) and &1; find anim record (4cb10) and decode next FLIC frame (4c9f0)
0x0004c9f0,texture_anim_next_frame_4c9f0,Anim record {+4 data ptr; +8 offset; +0xe first; +0x10 count; +0x12 dest; +0x16 frame}; wraps and calls flic_set_dest + flic_play_chunk
0x0004cb10,anim_find_record_4cb10,Linear search of 0x1C-byte records for +0x1a == id with +4 != 0
0x00050de0,flic_set_dest_50de0,DAT_0009e45e/60 = x;y
0x00050dfd,flic_play_chunk_50dfd,Reads 4-byte size + 2-byte magic; 0xAF12 (FLC header) -> 50f36; 0xF1FA (frame) -> flic_decode_frame; returns next ptr
0x00050e88,flic_decode_frame_50e88,Frame header chunk count; chunk type 4 = COLOR_256 (50f60); 7 = DELTA_FLC (50f71); 15 = BYTE_RUN (51024)
0x00050600,movie_frame_present_50600,FLIC player frame step: optional callback DAT_0012ea9c; wait tick/input (50430); blit (610f0) with 0x1A40 offset when DAT_000938fd
0x00050430,movie_wait_frame_50430,Waits until tick counter DAT_0012eab4 >= DAT_0009e704 (then resets it) or any input (key/mouse) -> DAT_0012eabc=1 (interrupted)
0x00032db0,title_screen_show_32db0,Loads data/smatitle.dat; blits; loads smatitle.pal and fades in (32 steps); sets state+0x245=1
0x00017d80,cue_script_step_17d80,Intro/credits cue interpreter: 7-byte records {u16 time; char op; u16 arg}; index DAT_000938f4; opcodes 'A' set wait (DAT_0009e704); 'B' load music bank; 'E' load sfx bank; 'K'/'L' play music; etc.
0x000494b0,sound_update_494b0,Per tick if sound on and not paused: fade-outs (4e2e0); fade-ins (4dfc0); then drains the 47-entry request table 0xB7AC0 (10 bytes: +0 type 1..4; +2 volume; +4 pan/param; +6 owner; +8 state): 1->4f6f0; 2->4f850; 3->4f7a0; 4->stop (5cea0) or set volume (5cbb0+627d0)
0x00049720,sound_request_49720,Game-side play(thing;player;sound_id): volume from distance (3e930) and pan from angle (3e6b0/3e770); writes request slot [sound_id] type 1 (restart) or 3 (play if not playing); local-player special sounds via 4e0f0
0x0004f8a0,sound_start_sample_4f8a0,Finds a free HMI channel (6291e); records {owner;sample} in DAT_0012e070[32]; volume in DAT_0012e0f0[ch]; fills the HMI sample block at 0x9E32C (ptr = tab[sample*0x20+0x12]; len = tab[+0x1a]-0x10; flags 0x300) and starts (64e7c)
0x0005cbb0,sound_play_sample_5cbb0,Same as 4f8a0 but skips if (owner;sample) already playing; flags 0x4100; volume 0x7FFF
0x0004f6f0,sound_restart_sample_4f6f0,Stops any (owner;sample) channel then start
0x0004f7a0,sound_play_if_idle_4f7a0,Start only if (owner;sample) not already playing
0x0004f850,sound_play_simple_4f850,Start without checks (sample <= DAT_0012e246)
0x0005cea0,sound_stop_sample_5cea0,Stop the channel playing (owner;sample)
0x000627d0,sound_set_sample_volume_627d0,Set target volume (0..0x80) for (owner;sample): HMI volume + table 0x97078
0x0004e2e0,sound_update_fadeout_4e2e0,Channels flagged in DAT_0012e330: volume -= 0x800 per tick; stop below 0x1001
0x0004dfc0,sound_update_fadein_4dfc0,Channels flagged in DAT_0012e270: volume += 0x800 up to DAT_0012e2b0[ch]*0x100-1
0x0005c040,sound_stop_all_5c040,Stops all 32 channels (loops 6291e/65485)
0x0005c990,sound_load_bank_5c990,Loads data/snds%d-%d.dat/.tab via file_load_resource then parses (5ca58)
0x0005c870,music_load_bank_5c870,Loads data/music%d-%d.dat/.tab then 5c924
0x0005c0a0,music_play_track_5c0a0,If music on and track <= DAT_0009e316 and != current (DAT_0009e312): stop current; point HMI song at tab[track*0x20+0x12]; init (5e53a) + start (5eab0); prints "Error : %s" on failure
0x0001f960,music_stop_1f960,Stops/uninits current song (5eb38/5ea6d/60035); DAT_0009e312=0
0x0001f843,music_track_change_1f843,Handles song end / track switch; registers a timer event via 5d093 for fades
0x00061870,music_shutdown_61870,Stop song; uninit MIDI driver (5ff24/5fa4d); remove timer; free
0x0005cf40,music_song_done_5cf40,Checks song handle table 0x9F9EE[track]==0
0x00065eb0,file_load_resource_65eb0,Resource descriptor {name; +0x1c &buf; +0x20 &end; +0x24 size; +0x28 flags}: '*' names are RAM-only buffers (alloc only); else size via 63450; alloc; file_load_rnc; sets end ptr
0x00065e70,file_free_resource_65e70,mem_free(*desc+0x1c) and clear
0x0005ae80,mem_alloc_named_5ae80,Allocates a named buffer (e.g. "*WScreen") through the resource loader
0x00059870,mem_alloc_59870,Best-fit allocator over block list at 0x130120 {ptr;size;next;?;used}; zeroes block
0x000622a8,mem_stats_622a8,Walks block list: total/free/used/largest/smallest into 0x131320..0x131330 (used by debug overlay)
0x0004eda0,net_netbios_submit_4eda0,Builds DPMI regs (AX=0x300 simulate int; BL=0x5C) with ES:BX -> NCB; returns -1 on DPMI failure; NCB+0x31 = completion code (0xFF pending)
0x0004e8f0,net_netbios_detect_4e8f0,Gets int 5Ch vector; submits NCB command 0x7F and expects return code 3 (invalid command => NetBIOS present)
0x0004e530,net_add_name_4e530,NCB 0xB0 (ADD NAME no-wait) with local name NCB+0x1a padded to 15
0x0004e940,net_delete_name_4e940,NCB 0xB1 (DELETE NAME); returns -code or -99
0x0004e600,net_call_4e600,NCB 0x90 (CALL no-wait) to remote name NCB+0xa
0x0004ea10,net_listen_4ea10,NCB 0x91 (LISTEN no-wait) on player slot DAT_0009e400[p]
0x0004e9e0,net_hangup_4e9e0,NCB 0x92 (HANG UP)
0x0004ec80,net_send_4ec80,NCB 0x94 (SEND no-wait) on DAT_0009e3d0
0x0004eb30,net_receive_4eb30,NCB 0x95 (RECEIVE no-wait) on DAT_0009e3d4
0x0004ec10,net_receive_player_4ec10,NCB 0x95 on per-player NCB DAT_0009e3d8[p]
0x0004e870,net_cancel_4e870,NCB 0x35 (CANCEL) of a pending player NCB
0x0004e4b0,net_check_cancel_4e4b0,Esc or click in the Cancel box (x 0x238..0x25E; y 0x60..0x86) sets DAT_0009e3f8 abort flag
0x0004f030,net_session_join_4f030,Lobby: default name "TESTER"; session names "Game One".."Game Six" (0x9E469); up to 8 players (DAT_0009e400[8] NCB ptrs; DAT_0009e3ca = local index; DAT_0009e3cc = player count); add name; listen/call all; wait all connected; status table DAT_0009e420
0x0004ed50,net_build_player_status_4ed50,DAT_0009e420[p] = 2 local / 1 connected / 0
0x0004f530,net_exchange_frame_4f530,Per tick (from 3a8b0): receive from every other player (4f470) then send to all (4f4d0) or vice versa depending on host
0x0004f470,net_receive_from_player_4f470,If connected: 4ecf0 receive then re-listen
0x0004f4d0,net_send_to_player_4f4d0,Sends local packet to player p
0x0003c540,demo_record_playback_step_3c540,cfg bit2 = record: open movie/mvi%05d.dat (cfg+0x12 handle); save state (gam%05d.dat via 3c2c0) + terrain (3c430); then file_write 10-byte packet per tick. cfg bit4 = playback: load state (3c200) + terrain (3c360); file_read 10 bytes per tick; stops on packet cmd 2; short read or any input and sets player status 8
0x0003c200,demo_load_state_3c200,file_load_rnc movie/gam%05d.dat over the 0x38D03-byte game-state block preserving option bytes state+0x2195..0x21b8; rebuilds thing lists (354c0)
0x0003c2c0,demo_save_state_3c2c0,file_save(movie/gam%05d.dat; state; 0x38D03)
0x0003c430,demo_save_terrain_3c430,Writes heightmap 0xCDFB0 (64K) + 3x64K + 128K + 0x12C2 bytes at 0xB58B0
0x0003c360,demo_load_terrain_3c360,Reads the same blocks back
0x0003c7c0,demo_close_3c7c0,Closes cfg+0x12 and clears cfg bits 2|4
0x000354c0,thing_rebuild_lists_354c0,Scans the 1000-thing pool backwards: free list state+0x251 (count state+0x28); active list state+0x11f5 (count +0x11f1) for things with flags(+0x10)&0x20400
0x000359b0,thing_free_count_359b0,Returns state+0x28 + 1
0x0004c130,texture_mark_needed_4c130,Clears 0x211-byte table state+0x2c then marks loaded textures (DAT_000b8d3c[i]!=0); +1 if DAT_000b9580[i]
0x0004c1a0,texture_load_needed_4c1a0,Loads textures flagged in state+0x2c (4b690) and evicts by score (4b850/4c550)
0x0003ca00,debug_screenshot_3ca00,Finds first free file name (<10000); allocs 400000 bytes; writes Bullfrog "mhwanh" header (big-endian w/h from DAT_0012ed70/78; 0x100 palette entries); 6-bit palette <<2; then raw screen DAT_0012ed74; triggered by DAT_00094338
0x0004ad80,debug_overlay_draw_4ad80,If local player status(+0x3410)&8: prints Product name/Magic Carpet/Beta v8.0/Version date Oct 20 1994/Level Number/Sound Number/Carpet %d/Heap %d/Memory (Used/Free)/per-block list with text_draw_string; sets cfg+0x18|=2 if cfg+0x19 == 0xF851B9 (session magic)
0x0004a9a0,text_draw_string_4a9a0,Walks string using font DAT_000adfbc (6-byte glyph recs; +4 advance; 9/0x20 space=+0xCA; 10 newline=+0xCB) drawing glyphs with 4d240; max x 0x27F
0x0004abe0,text_select_font_4abe0,DAT_000adfbc = font table DAT_000adf28[n]
0x0004abd0,text_font_line_height_4abd0,font+0xCB
0x0004abc0,text_font_space_width_4abc0,font+0xCA
0x000603bc,crt_sprintf_603bc,vsprintf (6a0e6) + NUL terminate; 45 callers
0x000619a0,file_open_619a0,open (66296); returns -1 on failure (mode 0x222 creates)
0x00061a10,file_close_61a10,close via 664de
0x00061a40,file_read_61a40,read(handle; buf; len)
0x00061e20,file_write_61e20,write(handle; buf; len)
0x00061db0,file_save_61db0,open/write/close; -1 on failure
0x0003411a,timer_tick_isr_3411a,DAT_0012eab4++ (called from the int 8 handler at LAB_0004ac03)
0x0004ac79,timer_isr_install_4ac79,Saves int 8 vector (DAT_000b7ca0/ca4); programs PIT via out (62c4d x3); installs LAB_0004ac03; cfg+0x19c = 0x2726; DAT_000987dc=1
0x0004ad0a,timer_isr_remove_4ad0a,Reprograms PIT and restores int 8
0x00062c4d,dos_outb_62c4d,out(port; value)
0x0005d093,hmi_timer_add_event_5d093,HMI timer system: 16 slots (0xA3C59 callback; 0xA3CB9 rate Hz; 0xA3CF9 = rate<<16 / (0x1234DC/divisor)); raises PIT rate (5d57b) if needed
0x0005d3a9,hmi_timer_remove_event_5d3a9,Clears slot; recomputes max rate
0x0005d57b,hmi_timer_set_rate_5d57b,DAT_000a3c55 = divisor; program PIT
0x00066680,pit_program_66680,out 43h=36h; 40h lo; 40h hi (with IRQ0 masked at 21h)
0x000667a9,pit_restore_667a9,DPMI call then PIT divisor 0 (18.2 Hz)
0x0006291e,hmi_sample_done_6291e,Searches driver's 32 slots for sample id; 1 if not playing
0x00065485,hmi_stop_sample_65485,Stops the slot playing sample id
0x00064d38,hmi_set_sample_volume_64d38,Sets volume of the slot playing sample id
0x00064e7c,hmi_start_sample_64e7c,Starts a sample on a free slot (1545 bytes)
0x00033750,config_parse_33750,Command words: PLAYER/CARPET; digijoy; anojoy; anojoy4; debug; network (->cfg bit 0x80); custom; setsound; detail; cheat N (cfg+1 bits 2..0x20); name; level (cfg+0x11); movie N (cfg+0xf; cfg|=0x120); session N (cfg+0x19); allocs state block 0x38D03 bytes
0x00061786,crt_atoi_61786,Skips whitespace (ctype table 0x937B4); optional sign; digits
0x0002ff50,render_mapmode_palette_2ff50?,When state+0x219b (map mode): if VESA 0x10E/0x111 available builds 15-bit colour tables 0xB3610/0xB3810; else 4 tinted 256-entry tables (0xB2E10..0xB3510) and an 8x8x4 colour cube palette
0x0002ff10,video_restore_mode_2ff10,Re-set 13h / 640x480 mode and clear DAT_00093f74
0x00030350,mapmode_palette_save_30350,Before frame: if map mode; restore mode/palette (saves mode in DAT_00093fc0)
0x000303b0,mapmode_palette_restore_303b0,After frame: re-enter map mode palette (2ff50) if DAT_00093fc0
0x00050080,altdev_read_50080?,Reads alternate pointing device (DAT_0009e43c/43d) via 4fd20 into DAT_0012e62c..30 (x;y;z) - head tracker / VR peripheral
0x00050120,altdev_command_50120?,Real-mode int with AX=0x6008 and data from DAT_0012e5f4 (alt device)
0x0003bc10,players_init_records_3bc10,Initialises the 8 per-player 0x801-byte records (copies template; sets packet cmd=1; index; flags; 0x20)
0x0003e970,math_dist2_3e970,(dx*dx+dy*dy) of two 2D short points
0x0003e930,math_dist_3e930,sqrt(dx*dx+dy*dy) via 4cd7a
0x0003ed30,text_split_lines_3ed30,Builds a pointer table of N NUL-separated strings (used for data/ftext.dat in movie mode)
```

## 2. ENGINE.md section: tick-side subsystems

### Corrections to existing notes
- `timer_sync_61510` is NOT a timer: it is `vga_palette_fade_61510` (reads the DAC, interpolates to a
  target palette over N steps with a vsync wait per step). The game loop calls it to fade to black.
- `DAT_0012edae` is the screen mode, not single/multiplayer: 1 = 320x200 (mode 13h, "*WScreen" 64000
  bytes), 8 = 640x480 VESA 0x101 (0x4B000 bytes). Bit 0 is tested everywhere as "low-res".
  The R key toggles it at run time (`video_toggle_resolution_33600`).
- cfg bit 0x100 is "movie" (scripted demo playback), not network. Network = cfg bit 0x80.
- The "random event every N ticks FUN_0005c0a0" in game_main is `music_play_track_5c0a0`.
- `FUN_0003a8b0` (4760 bytes) is not renderer: it calls `demo_record_playback_step_3c540` and
  `net_exchange_frame_4f530`, i.e. it is the per-tick command/packet processing for all players.

### Tick order (FUN_00032e80 = game_tick_update_32e80)
1. `palette_effect_update_33010` (only when state+0x219b == 0, i.e. not in map/hicolor mode)
2. `texture_anim_update_4be50` (not when paused, cfg+2 bit 0)
3. `player_local_input_16660` (skipped in demo playback, cfg bit 4)
4. FUN_0003a8b0 (command processing / net exchange / demo IO)
5. `player_check_mana_win_3db20` (not paused)
6. FUN_0003dce0 things, 1 / 4 / 16 passes by cfg+0x96 (F3 cycles 0..2)
7. `sound_update_494b0`
8. FUN_0001fab0
9. cfg+0x99 = frame time (DAT_0012eab4 tick counter delta); `debug_overlay_draw_4ad80`
10. `debug_screenshot_3ca00` if DAT_00094338; FUN_0002f480 present.

### Input
- Keyboard: ISR at LAB_0004fa28 (asm, directly after `vga_blit_banked_4f974`, not a Ghidra function),
  installed by `input_keyboard_install_4fb46` on int 9. Key-down table: byte[128] at 0x12EE20
  indexed by scancode (0x12EE21 = Esc, 0x12EE3C = Enter, 0x12EE68/70/6B/6D = Up/Down/Left/Right).
  Last-pressed scancode: DAT_0012eea0 (consumer clears it to 0). Scancode->ASCII tables at
  0x9E5E8 (in-game text entry) and 0x51E55 (front end); shift state DAT_0012ed35 bit 0x20.
- Mouse: int 33h via `dos_int86_61f30`; event callback LAB_0005b86c (asm, mask 0x7F). Position
  DAT_0009e5dc/de (640x400/480 space, 320x200 code halves it), click position DAT_0009e5d8/da.
  Button events: DAT_0012ee0e left-click, DAT_0012ee0c right-click (cleared by consumer);
  held: DAT_0012ee14 left, DAT_0012ee12 right. DAT_0009e5e4 mouse present. Cursor is a 64x64
  sprite buffer (0xFE transparent) with two save-under buffers.
- Joystick: game port 0x201, polled by `input_joystick_poll_5a4e0` each tick from player input;
  "digijoy"/"anojoy"/"anojoy4" select digital/analog/4-button (DAT_0009e583, DAT_0012ed40).
  Axes map to arrow-key flags, buttons to mouse-button events / Enter.
- Alternate device (DAT_0009e43c/43d, ports DAT_0012e61c/624/62e/630, int AX=0x6008): a
  head-tracking / stereo display peripheral; it receives the palette and provides x/y/z. Flagged `?`.

### Player command packet (10 bytes at state+0x7413 + player*10)
`+0 cmd, +1 arg/bits, +3 steer x, +4 steer y (-127..127 from mouse), +5 bits`. Written by
`player_queue_command_17270` / `player_mouse_steer_15590`, sent/received by the NetBIOS layer and
recorded/played back by the movie system. cmd 0x15/0x16/0x17 carry spell-slot selections (mode 2),
0x1e is the "chronicle" cheat command (player name check unless cfg+1&0x80).

### Player record (0x801 bytes at state+0x340F + p*0x801) fields seen here
`+0x340b win-timer, +0x340d status (bit2 won, bit4, 8 = level aborted), +0x3410 flags (bit8 debug
overlay), +0x3411 active, +0x3415 thing index, +0x341d, +0x3423 (config value), +0x3427 message
text (0x44 bytes, also +0x340b+0x1c variant), +0x3467 message ticks (0x32), +0x3469, +0x3815 name
(9+ chars), +0x3855 input mode (0 flight, 2 spell select, 3 text entry)`. A per-player
0x845 stride is also used for the message fields.

### Game-state block (0x38D03 bytes, DAT_000adf6c)
`+8 local player, +0xa player count, +0x28 free-thing count, +0x2c texture-needed table (0x211),
+0x245/+0x249/+0x24d title flags, +0x251 free list, +0x11f1 active count, +0x11f5 active list,
+0x2195..0x219e option toggles (F4..F10: 0x2195, 0x2196, 0x2199/0x219a, 0x219b map mode, 0x219c,
0x219d, 0x219e), +0x2198 view range (0x11..0x28), +0x21ad..0x21b7 "option allowed" gates,
+0x38c93 required mana percent`. Thing pool indexed from +0x7463 (0xa4 stride, index starts at 1;
+0x7503 = record 1 base used by the win check).

### Config struct (DAT_000adf74)
`+0 flags: 2 record demo, 4 play demo, 8 skip-game, 0x10 local player chosen (custom/network),
0x40, 0x80 network, 0x100 movie, 0x200 (set with movie); +1: bit0 options given, bits 2..0x20 cheat
level 1..5, 0x80 cheat gate; +2 bit0 paused; +0xd, +0xf movie number, +0x11 level, +0x12 demo file
handle, +0x16 selected spell slot, +0x17 palette-fade stage, +0x18 debug bits, +0x19 session magic
(0xF851B9 check), +0x5e level total mana, +0x96 update sub-steps (0..2), +0x98 palette effect,
+0x99 frame time, +0x19c PIT reload (0x2726)`.

### Timer
- int 8 handler LAB_0004ac03 (asm) installed by `timer_isr_install_4ac79` from
  `sound_initialise_34140`; it increments DAT_0012eab4 through `timer_tick_isr_3411a`.
  Reload 0x2726 = 10022 -> 1193182/10022 = 119 Hz (flag: inferred from cfg+0x19c, verify in asm).
- The HMI sound library has its own 16-slot timer event system (`hmi_timer_add_event_5d093`,
  constant 0x1234DC, PIT programming in 66680/667a9); music fades use it.

### Sound (HMI SOS, 32 digital channels)
Tables: DAT_0012e070[32] {owner thing, sample id}, DAT_0012e0f0[32] volume, DAT_0012e330 fade-out
flags, DAT_0012e270 fade-in flags (target DAT_0012e2b0), DAT_0012e246 sample count, 0x97078 target
volumes. Sample bank .tab records are 0x20 bytes (+0x12 data ptr, +0x1a length+0x10). Request table
0xB7AC0: 47 x 10 bytes indexed by sound id. Globals: DAT_0009e320 sfx available, DAT_0009e321 sfx
on, DAT_0009e30c music available, DAT_0009e30d music on, DAT_0009e312 current track,
DAT_0009e316 track count, DAT_0009e328 sound card type.

### Network (NetBIOS, int 5Ch through DPMI 0300h)
NCB layout used: +0 command, +6 LSA/post, +0xa callname(16), +0x1a name(16), +0x31 cmd_cplt.
Commands: 0xB0 add name, 0xB1 delete name, 0x90 call, 0x91 listen, 0x92 hang up, 0x94 send,
0x95 receive, 0x35 cancel, 0x7F presence test. 8 player slots DAT_0009e400, local index
DAT_0009e3ca, count DAT_0009e3cc, abort flag DAT_0009e3f8. No IPX/serial/modem code found in the
decompiled range (no int 7Ah/14h and no COM/modem strings).

### Demo / movie system
Files `movie/mvi%05d.dat` (10-byte packet stream) and `movie/gam%05d.dat` (RNC game-state
snapshot, 0x38D03 bytes) plus a terrain dump (64K heightmap at 0xCDFB0 + 3x64K + 128K + 0x12C2).
Recording: cfg bit 2; playback: cfg bit 4 (config "movie N" sets cfg+0xf and bits 0x120, game_main
then disables sound, loads data/ftext.dat and skips the front end).

### Other data formats
- Textures animate through embedded Autodesk FLIC (0xAF12 / 0xF1FA, chunk types 4/7/15).
- Screenshots are Bullfrog "mhwanh" images (big-endian header, 256-entry 8-bit palette, raw pixels).

### Open questions
- Exact PIT rate and the content of the asm handlers at 0x4AC03 (timer), 0x4FA28 (keyboard),
  0x5B86C (mouse callback): define them as functions in Ghidra.
- Identity of the alternate pointing/display device (DAT_0009e43c). Possibly a VR headset
  (Forte VFX1 / i-glasses style) - it gets a copy of the palette and provides 3 axes.
- Meaning of the F4..F9 option bytes (state+0x2195..0x219e); their on-screen message strings were
  passed in registers and are not recoverable from the decompiler output.
- `FUN_0004d2b0`/`FUN_0004d8f0`/`FUN_0004da10` parse `sndsetup.inf` and set up the HMI drivers
  (card names SBPRO/ADLIBG/SB16FM/PASFM/SBAWE32/GRAVIS...) - not covered in depth here.
